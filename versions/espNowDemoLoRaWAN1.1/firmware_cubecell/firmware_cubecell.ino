/*
 * firmware_cubecell.ino — espNowDemoLoRaWAN1.1
 * ==============================================
 * Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262)
 * Region: AU915 sub-banda 2 | OTAA Clase A | RadioLib 7.7.1
 *
 * Rol: Puente LoRaWAN ↔ Serial (TTL 9600) en modo REQUEST-RESPONSE.
 *
 * FLUJO:
 *   1. Cada 30 s envia un heartbeat (0xBB) que abre la ventana RX.
 *   2. Si el gateway responde con un comando downlink (texto, ej "GET 0"),
 *      lo reenvia TAL CUAL al serial (para el bridge ESP8266).
 *   3. Lee la respuesta del bridge por serial (una linea, timeout 5 s).
 *   4. Envia esa respuesta como uplink LoRaWAN.
 *
 * Protocolo serial (texto plano, 9600, TTL directo):
 *   Downlink  "GET 0"     → serial "GET 0\n"  → bridge responde "NODE 0 GPIO=1 ADC=512\n"
 *   Downlink  "GET ALL"   → serial "GET ALL\n" → bridge responde todas las lineas
 *   Downlink  "LIST"      → serial "LIST\n"    → bridge responde la tabla
 *
 * WORKAROUND ASR6502: millis() sleep + scanGuard=200 (timer bug).
 */

#include <RadioLib.h>
#include "credentials.h"

// Acceso a la flash del CubeCell (para persistir la sesion LoRaWAN)
extern "C" {
#include "hal/soc/flash.h"
// board_init() inicializa la tabla de particiones (hal_partitions[]).
// No se llama automaticamente en el main() del core, hay que llamarla aqui.
void board_init(void);
}

// Diagnostico por serial
#define debug(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

/* ===================================================================
 *  PINES SX1262
 *  P4_3=35(NSS), P4_6=38(DIO1), P5_7=47(RST), P4_7=39(BUSY)
 * =================================================================== */
Module mod(35, 38, 47, 39);
SX1262 radio(&mod);

const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);

/* ===================================================================
 *  BUFFERS Y CONSTANTES
 * =================================================================== */
#define MAX_PAYLOAD         51
#define HEARTBEAT_MS        30000UL   /* heartbeat cada 30 s */
#define RESPONSE_TIMEOUT_MS 5000UL    /* espera maxima por la respuesta del bridge */
#define SERIAL_BUF          256

uint8_t  downlinkData[MAX_PAYLOAD];
size_t   downlinkLen = 0;
uint32_t lastUplinkMs = 0;

/* Buffer para el modo "uplink manual" (HEX por serial → uplink) */
uint8_t  txBuffer[MAX_PAYLOAD];
size_t   txLen = 0;
char     serialLine[SERIAL_BUF];
size_t   serialPos = 0;

const uint8_t heartbeatPayload[] = { 0xBB };
const size_t  heartbeatLen = 1;

/* ===================================================================
 *  PERSISTENCIA DE SESION LoRaWAN (evita re-join en cada arranque)
 *  - getBufferSession()/getBufferNonces() extraen la sesion como bytes.
 *  - Se guardan en la flash (particion PARAMETER_1).
 *  - En el arranque se restauran con setBufferSession()/setBufferNonces().
 * =================================================================== */
#define SESSION_PARTITION HAL_PARTITION_PARAMETER_1

static uint8_t persistSession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
static uint8_t persistNonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];

// Guardar la sesion actual en flash
static void saveSession(void) {
    uint8_t* s = node.getBufferSession();
    uint8_t* n = node.getBufferNonces();
    uint32_t off = 0;
    hal_flash_erase(SESSION_PARTITION, 0, 4096);
    off = 0;
    hal_flash_write(SESSION_PARTITION, &off, s, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    hal_flash_write(SESSION_PARTITION, &off, n, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    debug("[PERSIST] Sesion guardada en flash (%d + %d bytes)\n",
          (int)RADIOLIB_LORAWAN_SESSION_BUF_SIZE, (int)RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

// Restaurar la sesion desde flash (true si es valida y queda activada)
// Cargar la sesion desde flash (solo restaura los buffers; la activacion
// final la hace activateOTAA(), que distingue restaurar vs join nuevo).
static void loadSession(void) {
    uint32_t off = 0;
    hal_flash_read(SESSION_PARTITION, &off, persistSession, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    hal_flash_read(SESSION_PARTITION, &off, persistNonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    // ORDEN IMPORTANTE: primero los nonces (su firma), luego la sesion
    node.setBufferNonces(persistNonces);
    node.setBufferSession(persistSession);
}

/* ===================================================================
 *  WORKAROUND: Sleep basado en millis()
 * =================================================================== */
static void radioSleepDelay(RadioLibTime_t ms) {
    if (ms == 0) return;
    uint32_t start = millis();
    while ((millis() - start) < ms) { /* NOP */ }
}

/* ===================================================================
 *  DIAGNOSTICO
 * =================================================================== */
static const char* stateDecode(int16_t s) {
    switch (s) {
        case RADIOLIB_ERR_NONE:               return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:     return "CHIP_NOT_FOUND";
        case RADIOLIB_ERR_RX_TIMEOUT:         return "RX_TIMEOUT";
        case RADIOLIB_ERR_MIC_MISMATCH:       return "MIC_MISMATCH";
        case RADIOLIB_ERR_NETWORK_NOT_JOINED: return "NOT_JOINED";
        case RADIOLIB_ERR_NO_JOIN_ACCEPT:     return "NO_JOIN_ACCEPT";
        case RADIOLIB_LORAWAN_NEW_SESSION:    return "NEW_SESSION";
        case RADIOLIB_LORAWAN_SESSION_RESTORED: return "SESSION_RESTORED";
        default:                              return "UNKNOWN";
    }
}

/* ===================================================================
 *  REINTENTAR JOIN
 * =================================================================== */
static bool ensureJoined(void) {
    if (node.isActivated()) return true;
    debug("[REJOIN] Intentando re-join...\n");
    int state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("[REJOIN] +++ EXITOSO +++\n");
        saveSession();
        return true;
    }
    if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("[REJOIN] +++ SESION RESTAURADA +++\n");
        return true;
    }
    debug("[REJOIN] Fallo: %d (%s)\n", state, stateDecode(state));
    return false;
}

/* ===================================================================
 *  PARSER HEX: "DE AD BE EF" o "DEADBEEF" → bytes
 * =================================================================== */
static size_t parseHexString(const char* input, uint8_t* output, size_t maxOut) {
    size_t outLen = 0;
    char clean[256];
    size_t cleanLen = 0;
    for (size_t i = 0; input[i] != '\0' && cleanLen < sizeof(clean) - 1; i++) {
        char c = input[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            clean[cleanLen++] = c;
        }
    }
    clean[cleanLen] = '\0';
    if (cleanLen == 0 || cleanLen % 2 != 0) return 0;
    for (size_t i = 0; i < cleanLen && outLen < maxOut; i += 2) {
        char byteStr[3] = { clean[i], clean[i + 1], '\0' };
        output[outLen++] = (uint8_t)strtoul(byteStr, NULL, 16);
    }
    return outLen;
}

/* ===================================================================
 *  MOSTRAR DOWNLINK EN SERIAL (modo manual)
 * =================================================================== */
static void printDownlink(const uint8_t* data, size_t len) {
    debug("[DOWNLINK] ");
    for (size_t i = 0; i < len; i++) debug("%02X ", data[i]);
    debug("→ \"");
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        debug("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    debug("\"\n");
}

/* ===================================================================
 *  ENVIAR PAYLOAD POR LORAWAN Y MOSTRAR DOWNLINK (modo manual)
 * =================================================================== */
static int sendAndReceiveManual(const uint8_t* data, size_t len) {
    if (len == 0) return RADIOLIB_ERR_NONE;
    debug(">> Enviando: ");
    for (size_t i = 0; i < len; i++) debug("%02X ", data[i]);
    downlinkLen = sizeof(downlinkData);
    int state = node.sendReceive(data, len, 1, downlinkData, &downlinkLen, false, NULL, NULL);
    if (state == RADIOLIB_ERR_NONE) {
        debug("→ OK (sin downlink)\n");
    } else if (state > 0) {
        debug("→ OK + downlink (%d bytes)\n", (int)downlinkLen);
        printDownlink(downlinkData, downlinkLen);
    } else {
        debug("→ ERROR %d (%s)\n", state, stateDecode(state));
    }
    return state;
}

/* ===================================================================
 *  PROCESAR LINEA SERIAL (modo manual: HEX → uplink)
 * =================================================================== */
static void processSerialLine(const char* line) {
    if (line[0] == '\0') return;

    /* Comando especial: ERASE (borrar sesion persistida y re-enrolar limpio) */
    if (strcmp(line, "ERASE") == 0 || strcmp(line, "erase") == 0) {
        hal_flash_erase(SESSION_PARTITION, 0, 4096);
        debug("[ERASE] Sesion borrada. Reiniciando...\n");
        delay(500);
        CySoftwareReset();
        return;
    }

    txLen = parseHexString(line, txBuffer, MAX_PAYLOAD);
    if (txLen == 0) {
        debug("?? Error: escribe hex valido. Ej: DE AD BE EF\n> ");
        Serial.flush();
        return;
    }
    if (!ensureJoined()) {
        debug("!! Sin sesion LoRaWAN, reintentando...\n");
        radioSleepDelay(5000);
        if (!ensureJoined()) {
            debug("!! No se pudo recuperar sesion\n> ");
            Serial.flush();
            return;
        }
    }
    sendAndReceiveManual(txBuffer, txLen);
    lastUplinkMs = millis();
    debug("> ");
    Serial.flush();
}

/* ===================================================================
 *  LEER UNA LINEA DEL SERIAL CON TIMEOUT (respuesta del bridge)
 * =================================================================== */
static String readSerialLine(uint32_t timeoutMs) {
    String line = "";
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        while (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                return line;   /* fin de linea */
            }
            if (line.length() < (unsigned)(MAX_PAYLOAD - 1)) {
                line += c;
            }
        }
        delay(10);
    }
    return line;   /* timeout o linea parcial */
}

/* ===================================================================
 *  MANEJAR UN DOWNLINK: reenviar al serial, leer respuesta, subirla
 * =================================================================== */
static void handleDownlink(const uint8_t* data, size_t len) {
    debug("[DOWNLINK] %d bytes: ", (int)len);
    for (size_t i = 0; i < len; i++) debug("%02X ", data[i]);
    debug("→ \"");
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        debug("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    debug("\"\n");

    /* 1) Reenviar el comando al bridge (texto + newline) */
    Serial.write(data, len);
    Serial.write('\n');
    Serial.flush();

    /* 2) Leer la respuesta del bridge */
    String resp = readSerialLine(RESPONSE_TIMEOUT_MS);
    resp.trim();

    if (resp.length() == 0) {
        debug("[BRIDGE] Sin respuesta (timeout)\n");
        return;
    }

    debug("[BRIDGE] %s\n", resp.c_str());

    /* 3) Subir la respuesta como uplink LoRaWAN */
    if (!ensureJoined()) {
        debug("!! Sin sesion LoRaWAN\n");
        return;
    }
    int state = node.sendReceive((uint8_t*)resp.c_str(), resp.length(), 1,
                                 downlinkData, &downlinkLen, false, NULL, NULL);
    if (state == RADIOLIB_ERR_NONE || state > 0) {
        debug("[UPLINK] respuesta enviada (%d bytes)\n", (int)resp.length());
    } else {
        debug("[UPLINK] ERROR %d (%s)\n", state, stateDecode(state));
    }
}

/* ===================================================================
 *  SETUP
 * =================================================================== */
void setup() {
    Serial.begin(9600);
    delay(3000);
    debug("\n=== CubeCell LoRaWAN↔Serial Bridge (espNowDemoLoRaWAN1.1) ===\n");

    /* Inicializar tabla de particiones de flash (para persistencia) */
    board_init();

    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("Radio init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Radio OK\n");

    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("Node init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Node OK\n");

    node.setSleepFunction(radioSleepDelay);
    node.scanGuard = 200;
    debug("scanGuard=%dms, millis sleep\n", node.scanGuard);

    debug("Verificando sesion persistida...\n");
    loadSession();                 // restaura buffers (si hay sesion valida)
    state = node.activateOTAA();   // activa: restaura o hace join nuevo
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("+++ JOINED +++\n");
        saveSession();
    } else if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("+++ SESION RESTAURADA (sin re-join) +++\n");
    } else {
        debug("Join FAILED: %d (%s)\n", state, stateDecode(state));
    }

    lastUplinkMs = millis();

    debug("=== Listo. Serial 9600 8N1 ===\n");
    debug("Modo manual: escribe HEX + Enter → uplink.\n");
    debug("Modo automatico: heartbeat cada 30 s abre RX para downlinks.\n> ");
    Serial.flush();
}

/* ===================================================================
 *  LOOP
 * =================================================================== */
void loop() {
    /* 1) Modo manual: leer serial (HEX → uplink, para pruebas por CuteCom) */
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialPos > 0) {
                serialLine[serialPos] = '\0';
                processSerialLine(serialLine);
                serialPos = 0;
            }
        } else if (serialPos < SERIAL_BUF - 1) {
            serialLine[serialPos++] = c;
        }
    }

    /* 2) Heartbeat periodico: reintenta join si hace falta, abre RX para downlinks */
    if ((millis() - lastUplinkMs) >= HEARTBEAT_MS) {
        lastUplinkMs = millis();

        /* Si no esta unido, intentar (re)join */
        if (!node.isActivated()) {
            if (ensureJoined()) {
                debug("[HEARTBEAT] Re-join OK\n");
            }
        }

        /* Si ya esta unido, enviar heartbeat (abre ventana RX) */
        if (node.isActivated()) {
            downlinkLen = sizeof(downlinkData);
            int state = node.sendReceive(heartbeatPayload, heartbeatLen, 1,
                                         downlinkData, &downlinkLen, false, NULL, NULL);
            debug("[HEARTBEAT] state=%d downlinkLen=%d\n", (int)state, (int)downlinkLen);

            if (state > 0 && downlinkLen > 0) {
                /* Hubo downlink → request-response con el bridge */
                handleDownlink(downlinkData, downlinkLen);
            }
        }
    }

    delay(10);
}
