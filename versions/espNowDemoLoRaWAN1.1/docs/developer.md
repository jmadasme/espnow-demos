# Developer Guide — espNowDemoLoRaWAN1.1

> Documentación técnica del código fuente de ambos firmwares, diagramas de flujo,
> referencia de funciones y guía de extensión. No modifiques los `.ino`: este
> documento sólo los explica.

---

## 1. Arquitectura general

El proyecto son **dos programas** que se comunican por un enlace serial TTL de
texto plano:

```
firmware_cubecell.ino          firmware_bridge.ino
(CubeCell, LoRaWAN)            (ESP8266, ESP-NOW + serial)
        │                               │
        │   UART 9600 8N1 (texto)       │
        │◀───────────────────────────────▶│
        │  "GET 0"  /  "NODE 0 ..."     │
        │                               │
   RadioLib/SX1262                 esp_now + SoftwareSerial
   LoRaWAN AU915 sub-banda 2       red de nodos sensores
```

### 1.1 firmware_cubecell.ino (nodo LoRaWAN)

Modelo de ejecución:

- `setup()`: inicializa serial (9600), `board_init()` (particiones flash),
  `radio.begin()`, `node.beginOTAA(...)`, configura el *sleep* por `millis()` y el
  `scanGuard`, restaura la sesión persistida (`loadSession()`) y activa
  (`activateOTAA()`), que hace join nuevo o restaura sesión.
- `loop()`: dos tareas concurrentes (cooperativas):
  1. **Modo manual**: lee líneas del serial; si es HEX → uplink; si es `ERASE` →
     borra sesión y reinicia.
  2. **Heartbeat**: cada 30 s intenta (re)join si es necesario y, si está unido,
     envía `0xBB` para abrir la ventana RX. Si hay downlink, llama
     `handleDownlink()`.

### 1.2 firmware_bridge.ino (maestro ESP-NOW + puente serial)

Modelo de ejecución:

- `setup()`: inicializa Serial (115200) y SoftwareSerial (9600), LED, WiFi en
  modo STA, ESP-NOW en rol COMBO, registra callbacks, agrega el peer broadcast y
  los peers conocidos.
- `loop()`: procesa en cada iteración:
  1. `processResponse()` — respuestas ESP-NOW pendientes (desde buffer ISR).
  2. `readSerialCommands()` — comandos humanos (115200).
  3. `readSoftSerialCommands()` — comandos de máquina de la CubeCell (9600).
  4. Timeout de respuesta.
  5. Auto-polling (si está activo) cada `POLL_INTERVAL_MS`.

Los callbacks ESP-NOW (`onDataReceived`, `onDataSent`) se ejecutan en **contexto
ISR** y sólo copian datos a buffers `volatile`; todo el trabajo pesado ocurre en
`loop()`.

---

## 2. firmware_cubecell.ino — funciones clave

### 2.1 Persistencia de sesión LoRaWAN

La persistencia evita re-enrolar (join) en cada arranque. Flujo:

```
setup()
  └─ board_init()                      // inicializa hal_partitions[]
  └─ loadSession()                     // lee flash → buffers RAM
  └─ node.activateOTAA()               // decide: join nuevo o restaura
        ├─ NEW_SESSION      → saveSession()  (guarda en flash)
        └─ SESSION_RESTORED → nada (ya restaurada)

ensureJoined()
  └─ si !isActivated() → activateOTAA() y saveSession() si es nuevo
```

- **`saveSession()`** — obtiene `node.getBufferSession()` y
  `node.getBufferNonces()`, borra la partición `PARAMETER_1` (4096 bytes) y
  escribe ambos buffers con `hal_flash_write()`.
- **`loadSession()`** — lee con `hal_flash_read()` los buffers, y los restaura
  con `node.setBufferNonces()` **primero** y `node.setBufferSession()` después
  (el orden es importante: los nonces llevan la firma de integridad).
- **`ensureJoined()`** — devuelve `true` si ya está activado o si el
  (re)join/restauración tiene éxito; guarda la sesión cuando hay
  `NEW_SESSION`. `false` en caso de error.
- **Comando `ERASE`** — `hal_flash_erase(SESSION_PARTITION, 0, 4096)` + reinicio
  (`CySoftwareReset()`).

Constantes relevantes:

```c
#define SESSION_PARTITION HAL_PARTITION_PARAMETER_1
#define MAX_PAYLOAD        51
#define HEARTBEAT_MS       30000UL
#define RESPONSE_TIMEOUT_MS 5000UL
#define SERIAL_BUF         256
```

### 2.2 Otras funciones

| Función | Propósito |
|---|---|
| `stateDecode(int16_t)` | Traduce códigos de error de RadioLib a texto legible |
| `radioSleepDelay(ms)` | *Sleep* por `millis()` (workaround del timer del ASR6502) |
| `parseHexString(in,out,max)` | Convierte `"DE AD BE EF"` o `"DEADBEEF"` a bytes |
| `printDownlink(data,len)` | Imprime un downlink en HEX y ASCII |
| `sendAndReceiveManual(data,len)` | Envía un uplink (modo manual) y muestra el downlink |
| `processSerialLine(line)` | Maneja una línea del serial: `ERASE` o HEX→uplink |
| `readSerialLine(timeout)` | Lee una línea del serial con timeout (respuesta del bridge) |
| `handleDownlink(data,len)` | Reenvía el downlink al bridge, lee la respuesta y la sube |

### 2.3 `handleDownlink()` en detalle

```c
static void handleDownlink(const uint8_t* data, size_t len) {
    // 1) imprime el downlink (HEX + ASCII)
    // 2) Serial.write(data, len); Serial.write('\n');   → reenvía al bridge
    // 3) String resp = readSerialLine(RESPONSE_TIMEOUT_MS); resp.trim();
    // 4) si resp vacío → "[BRIDGE] Sin respuesta (timeout)" y retorna
    // 5) ensureJoined(); node.sendReceive(resp.c_str(), resp.length(), 1, ...)
    //    → sube la respuesta como uplink
}
```

---

## 3. firmware_bridge.ino — funciones clave

### 3.1 Estructuras de mensaje ESP-NOW

El formato base son **13 bytes** empaquetados (`packed`):

```c
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 0=request, 1=response
  uint8_t  node_id;     // 0-8
  uint8_t  gpio_state;  // digital
  uint16_t adc_value;   // ADC 0-1023
  uint32_t counter;     // contador correlativo
  uint32_t uptime_ms;   // uptime
} EspNowMessage;
```

Structs adicionales (mismo esquema, tamaños 14 y 16 bytes):

- `EspNowDhtMessage` (14 B) — `msg_type=2`, `temp_x10`, `hum_x10`.
- `EspNowIrMessage` (14 B) — `msg_type=3`, `obj_temp_x10`, `amb_temp_x10`.
- `EspNowAccelMessage` (16 B) — `msg_type=4`, `accel_x`, `vib_range`,
  `vib_level`, `reserved`.

> `EspNowDhtMessage` e `EspNowIrMessage` comparten tamaño (14 B); en el callback
> se distingue por `msg_type`.

### 3.2 Callbacks (ISR)

| Callback | Propósito |
|---|---|
| `onDataReceived(mac,data,len)` | Valida `msg_type`/longitud, copia MAC + datos a buffers `volatile` (`g_last_response_mac`, `g_response_data`, `g_response_len`), marca `g_response_received=true`. |
| `onDataSent(mac,status)` | Si `status != 0`, limpia `waiting_response`. |

### 3.3 Funciones principales

| Función | Propósito |
|---|---|
| `sendRequest(node_id)` | Construye un request, envía a la MAC conocida o broadcast, actualiza contadores |
| `sendMachineResponse(type,data,id)` | Escribe la respuesta **compacta** por SoftwareSerial (`NODE <id> ...`) |
| `processResponse()` | Copia del buffer ISR, autodescubrimiento de MAC, enruta a SoftwareSerial o Serial |
| `processSerialCommand(cmd)` | Comandos humanos: `ping`, `list`, `stats`, `auto on/off`, `help` |
| `readSerialCommands()` | Lee línea del Serial (115200) |
| `processMachineCommand(cmd)` | Comandos de máquina: `GET <id>`, `GET ALL`, `LIST` |
| `readSoftSerialCommands()` | Lee línea del SoftwareSerial (9600, de la CubeCell) |
| `isSelfNode(id)` | Detecta si el nodo es el propio bridge (por MAC) |
| `printStats()` | Imprime estadísticas (requests/responses/timeouts) |

### 3.4 `processMachineCommand()` en detalle

- **`LIST`** — itera los nodos, imprime `NODE <id> <MAC>` o `NODE <id> UNKNOWN`,
  termina con `END`.
- **`GET ALL`** — para cada nodo conocido (no self), envía request con
  `g_response_to_softserial=true` y espera la respuesta (bucle con
  `processResponse()`); en timeout imprime `ERROR NODE <id> TIMEOUT`. Termina con
  `END`.
- **`GET <id>`** — valida el id, responde `NODE <id> SELF` si es el bridge,
  `BUSY` si está ocupado, o envía el request en modo máquina.

### 3.5 Enrutamiento de respuestas

La flag `g_response_to_softserial` decide el destino de la respuesta:

- `true` → la respuesta se envía **sólo** por SoftwareSerial en formato compacto
  (a la CubeCell), sin imprimir el formato verbose.
- `false` → la respuesta se imprime en Serial en formato legible (humano).

---

## 4. Diagrama de flujo

### 4.1 CubeCell — `loop()`

```
loop()
 │
 ├─ while Serial.available()
 │     └─ leer línea completa
 │           ├─ "ERASE"  → hal_flash_erase + CySoftwareReset()
 │           └─ HEX      → ensureJoined() → sendAndReceiveManual()
 │
 ├─ if (millis()-lastUplinkMs >= 30000)
 │     ├─ if !isActivated() → ensureJoined()
 │     └─ if isActivated()
 │           ├─ sendReceive(0xBB)   [abre RX]
 │           └─ if downlink → handleDownlink()
 │                 ├─ reenviar a serial + '\n'
 │                 ├─ readSerialLine(5000)
 │                 └─ sendReceive(respuesta)  [uplink]
 │
 └─ delay(10)
```

### 4.2 Bridge — `loop()`

```
loop()
 │
 ├─ processResponse()            // respuestas ESP-NOW pendientes
 │     └─ si g_response_to_softserial → sendMachineResponse()
 │
 ├─ readSerialCommands()         // humano (115200)
 ├─ readSoftSerialCommands()     // máquina (9600) → processMachineCommand()
 │
 ├─ timeout check (waiting_response)
 │     └─ si expiró → ERROR NODE <id> TIMEOUT (por softSerial si era máquina)
 │
 ├─ auto-poll (cada 3000 ms) si activo y sin respuesta en vuelo
 │     └─ sendRequest(nodo actual)
 │
 └─ stats cada 30 s (si auto-poll activo)
```

### 4.3 Flujo completo de una orden remota (downlink)

```
Gateway ──downlink "GET 0"──▶ CubeCell (tras heartbeat)
                                 │
                                 ├─ Serial.write("GET 0\n")
                                 ▼
                            Bridge (SoftSerial RX)
                                 │
                                 ├─ processMachineCommand("GET 0")
                                 ├─ sendRequest(0) via ESP-NOW
                                 ├─ espera respuesta / timeout
                                 ▼
                            Nodo 0 (ESP-NOW response)
                                 │
                                 ▼
                            Bridge → softSerial "NODE 0 GPIO=1 ADC=512\n"
                                 │
                                 ▼
                            CubeCell readSerialLine() → sendReceive(resp)
                                 │
                                 ▼
                            Gateway ◀──uplink "NODE 0 GPIO=1 ADC=512"──
```

---

## 5. Guía de modificación y extensión

### 5.1 Agregar un sensor nuevo

**En el bridge (`firmware_bridge.ino`):**

1. Define un nuevo `msg_type` (ej. `#define MSG_TYPE_XXX_RESPONSE 5`).
2. Define la struct `packed` correspondiente (mantén `msg_type` y `node_id` como
   **primeros dos bytes** para que `processResponse()` pueda leerlos
   genéricamente).
3. Añade un `else if` en `onDataReceived()` para validar `len == sizeof(...)`.
4. Añade un `else if` en `sendMachineResponse()` para el formato compacto
   (`NODE <id> ...`).
5. Añade el bloque de impresión verbose en `processResponse()`.

**En el nodo sensor** (fuera de este repo): envía la respuesta con el mismo
`msg_type` y el `node_id` correcto.

### 5.2 Cambiar el payload de heartbeat / respuesta

- **Heartbeat:** modifica `heartbeatPayload[]` (actualmente `{ 0xBB }`) y
  `heartbeatLen`.
- **Respuesta:** el payload de uplink es la respuesta textual del bridge. Si
  quieres un payload binario estructurado, cambia `handleDownlink()` para
  parsear la respuesta y construir tus propios bytes antes de `sendReceive()`.

### 5.3 Cambiar la región / sub-banda

En `firmware_cubecell.ino`:

```c
const LoRaWANBand_t Region = AU915;   // cambiar a EU868, US915, etc.
const uint8_t subBand = 2;            // cambiar la sub-banda
```

Verifica que el gateway y el NS estén configurados en la misma región/sub-banda.
Tras cambiar región, ejecuta `ERASE` para re-enrolar limpio.

### 5.4 Cambiar los pines de la radio (SX1262)

En `firmware_cubecell.ino`:

```c
Module mod(35, 38, 47, 39);   // NSS, DIO1, RST, BUSY
SX1262 radio(&mod);
```

Ajusta estos números si usas una variante de CubeCell con distinto mapeo de pines
(consulta el esquemático de tu placa).

### 5.5 Cambiar la velocidad / pines del serial CubeCell↔bridge

- **CubeCell:** `Serial.begin(9600)` (usa la UART0 del ASR6502).
- **Bridge:** `#define SOFT_SERIAL_RX D5`, `#define SOFT_SERIAL_TX D6`,
  `#define SOFT_SERIAL_BAUD 9600`. Si cambias la velocidad, cámbiala en **ambos**
  lados.

### 5.6 Ajustar timeouts e intervalos

| Constante | Archivo | Valor | Efecto |
|---|---|---|---|
| `HEARTBEAT_MS` | cubecell | 30000 | Frecuencia de heartbeat |
| `RESPONSE_TIMEOUT_MS` (cubecell) | cubecell | 5000 | Espera de la respuesta del bridge |
| `POLL_INTERVAL_MS` | bridge | 3000 | Auto-polling ESP-NOW |
| `RESPONSE_TIMEOUT_MS` (bridge) | bridge | 2000 | Timeout de un nodo ESP-NOW |

> Si agregas un sensor más lento que el DS18B20, sube
> `RESPONSE_TIMEOUT_MS` del bridge (2000 ms) para darle margen.

### 5.7 Portar a otra placa LoRaWAN

El bloque específico del CubeCell es la **persistencia de sesión**:

```c
extern "C" {
#include "hal/soc/flash.h"
void board_init(void);
}
```

Sustitúyelo por el almacenamiento nativo de tu placa (EEPROM/Preferences/SPIFFS)
manteniendo la misma lógica: guardar `getBufferSession()`/`getBufferNonces()` y
restaurar con `setBufferSession()`/`setBufferNonces()`.

---

## 6. API de funciones (resumen)

### CubeCell

```c
static void   saveSession(void);
static void   loadSession(void);
static bool   ensureJoined(void);
static size_t parseHexString(const char*, uint8_t*, size_t);
static void   printDownlink(const uint8_t*, size_t);
static int    sendAndReceiveManual(const uint8_t*, size_t);
static void   processSerialLine(const char*);
static String readSerialLine(uint32_t timeoutMs);
static void   handleDownlink(const uint8_t*, size_t);
static const char* stateDecode(int16_t);
static void   radioSleepDelay(RadioLibTime_t ms);
```

### Bridge

```c
void onDataReceived(uint8_t*, uint8_t*, uint8_t);   // ISR
void onDataSent(uint8_t*, uint8_t);                 // ISR
void sendRequest(uint8_t node_id);
void sendMachineResponse(uint8_t type, void* data, uint8_t id);
void processResponse();
void processSerialCommand(const char* cmd);
void readSerialCommands();
void processMachineCommand(const char* cmd);
void readSoftSerialCommands();
bool isSelfNode(uint8_t node_id);
void printStats();
```

---

## 7. Notas de implementación y gotchas

1. **ISR vs loop:** los callbacks ESP-NOW no deben hacer `Serial.print`, `delay`
   ni `memcpy` grandes; sólo copian a buffers `volatile`. El procesado real
   ocurre en `loop()`.
2. **ASR6502 timer bug:** el `sleep` de RadioLib se implementa con `millis()` +
   `scanGuard=200` porque el timer nativo del CubeCell no es fiable en este
   escenario.
3. **Orden de restauración de sesión:** primero `setBufferNonces()`, luego
   `setBufferSession()`.
4. **Underflow de `millis()` en el bridge:** el timeout usa `millis()` **fresco**
   (no el `now` capturado al inicio del loop) para evitar el desbordamiento
   cuando el request se envía en la misma iteración.
5. **Autodescubrimiento de MAC:** si la MAC de un nodo es desconocida, se usa
   broadcast `FF:FF:FF:FF:FF:FF`; la MAC se aprende de la respuesta y se registra
   el peer directo.
6. **Slot 5 reservado:** el nodo 5 se reserva para el propio bridge
   (auto-identificación por MAC) y nunca se interroga.
