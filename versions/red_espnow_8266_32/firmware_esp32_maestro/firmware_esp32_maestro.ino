/**
 * Firmware ESP32 - Maestro Central ESP-NOW
 * ==========================================
 * Placa: ESP32 DevKit v1 (ESP-WROOM-32)
 * 
 * Funcion:
 *   - Interroga secuencialmente a cada nodo ESP8266 via ESP-NOW
 *   - Envia un request (tipo=0) con el node_id destino
 *   - Espera la respuesta (tipo=1) con ID + contador correlativo
 *   - Muestra los datos por Serial
 *
 * Protocolo ESP-NOW:
 *   Request  (ESP32 -> ESP8266): {tipo=0, node_id=X, counter=0}
 *   Response (ESP8266 -> ESP32): {tipo=1, node_id=X, counter=N, uptime_ms}
 *
 * Compatible con ESP32 Arduino Core 3.x 
 *
 * PINOUT ESP32 DevKit v1:
 *   - GPIO2  -> LED_BUILTIN
 *   - GPIO1  -> TX (Serial)
 *   - GPIO3  -> RX (Serial)
 *   - Resto  -> Sin uso en este firmware
 */

#include <WiFi.h>
#include <esp_now.h>

// ============================================================
// Configuracion de la red ESP-NOW
// ============================================================

// Intervalo entre polls (milisegundos)
#define POLL_INTERVAL_MS  3000

// Baudrate del monitor serial
#define SERIAL_BAUD       115200

// LED indicador
#define LED_PIN           GPIO_NUM_2

// MAC de broadcast
const uint8_t ESP_NOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============================================================
// Tipos de mensaje (debe coincidir con firmware ESP8266)
// ============================================================
#define MSG_TYPE_REQUEST  0
#define MSG_TYPE_RESPONSE 1

// ============================================================
// Estructura del mensaje (debe coincidir con ESP8266)
// ============================================================
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 0=request, 1=response
  uint8_t  node_id;     // ID del nodo (0-4)
  uint32_t counter;     // Contador correlativo
  uint32_t uptime_ms;   // Tiempo de actividad del nodo
} EspNowMessage;

// ============================================================
// Tabla de nodos ESP8266 en la red
// ============================================================
// IMPORTANTE: Reemplazar estas MAC con las reales de tus nodos.
// Para obtener la MAC de un nodo, compila y sube el firmware
// ESP8266 y mira la salida del Serial: "MAC de este nodo: XX:XX:XX:XX:XX:XX"
//
// Las MACs se aprenden automaticamente al recibir la primera respuesta
// al recibir la primera respuesta de cada nodo.
#define NUM_NODES 6

typedef struct {
  uint8_t  id;
  uint8_t  mac[6];
  bool     known;       // true si la MAC ha sido aprendida
} EspNowNode;

// MACs conocidas de las ESP8266 ya programadas
// known=true  -> se envia directo a esa MAC
// known=false -> se envia broadcast y se aprende automaticamente
EspNowNode nodes[NUM_NODES] = {
  { 0, {0x84, 0xF3, 0xEB, 0x7C, 0x99, 0x1E}, true },
  { 1, {0x80, 0x7D, 0x3A, 0x2B, 0xE3, 0x59}, true },
  { 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, false },
  { 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, false },
  { 4, {0x8C, 0xAA, 0xB5, 0x0F, 0x39, 0x7F}, true },
  { 5, {0x50, 0x02, 0x91, 0xDE, 0x0D, 0x7B}, true },
};

// Indice del nodo actual que se esta interrogando
uint8_t current_node_idx = 0;

// Timestamp del ultimo poll
unsigned long last_poll_time = 0;

// Flag: esperando respuesta
bool waiting_response = false;

// Timeout para considerar perdida de respuesta
#define RESPONSE_TIMEOUT_MS 1000
unsigned long request_sent_time = 0;

// Estadisticas
uint32_t total_requests = 0;
uint32_t total_responses = 0;
uint32_t total_timeouts = 0;

// ============================================================
// Callback: datos recibidos via ESP-NOW
// API ESP32 Core 3.x: recibe esp_now_recv_info_t
// ============================================================
void onDataReceived(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  // Extraer MAC origen
  const uint8_t *mac = recv_info->src_addr;

  if (len != sizeof(EspNowMessage)) {
    Serial.printf("[ERROR] Tamano invalido: %d (esperado %d)\n",
                  len, sizeof(EspNowMessage));
    return;
  }

  EspNowMessage *msg = (EspNowMessage *)data;

  if (msg->msg_type == MSG_TYPE_RESPONSE) {
    uint8_t id = msg->node_id;

    // Validar que el ID recibido sea valido
    if (id >= NUM_NODES) {
      Serial.printf("[WARN] ID de nodo invalido: %d\n", id);
      return;
    }

    // Actualizar MAC en la tabla si no la conocemos
    if (!nodes[id].known) {
      memcpy(nodes[id].mac, mac, 6);
      nodes[id].known = true;
      Serial.printf("[APRENDIZAJE] MAC del nodo %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

      // Agregar peer con MAC conocida para futuros envios directos
      esp_now_peer_info_t peerInfo;
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, mac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.printf("[WARN] No se pudo agregar peer para nodo %d\n", id);
      }
    }

    total_responses++;
    waiting_response = false;

    // Calcular RTT aproximado
    uint32_t rtt = millis() - request_sent_time;

    Serial.println("----------------------------------------");
    Serial.printf("RESPUESTA RECIBIDA - Nodo #%d\n", id);
    Serial.printf("  Contador correlativo : %u\n", msg->counter);
    Serial.printf("  Tiempo activo nodo   : %u ms (%u s)\n",
                  msg->uptime_ms, msg->uptime_ms / 1000);
    Serial.printf("  RTT                   : %u ms\n", rtt);
    Serial.println("----------------------------------------");

    // Parpadeo de confirmacion
    digitalWrite(LED_PIN, HIGH);
    delay(30);
    digitalWrite(LED_PIN, LOW);
  }
}

// ============================================================
// Callback: datos enviados (confirmacion de envio)
// API ESP32 Core 3.x: recibe wifi_tx_info_t
// ============================================================
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    waiting_response = false;
    Serial.printf("[ERROR] Fallo al enviar request (status=%d)\n", status);
  }
}

// ============================================================
// Enviar request a un nodo especifico
// ============================================================
void sendRequest(uint8_t node_id) {
  EspNowMessage request;
  request.msg_type = MSG_TYPE_REQUEST;
  request.node_id = node_id;
  request.counter = 0;
  request.uptime_ms = 0;

  const uint8_t *target_mac = nodes[node_id].mac;

  // Si no conocemos la MAC, usamos broadcast
  bool use_broadcast = !nodes[node_id].known;

  if (use_broadcast) {
    target_mac = ESP_NOW_BROADCAST_MAC;
  }

  esp_err_t result = esp_now_send(target_mac, (uint8_t *)&request, sizeof(EspNowMessage));

  if (result == ESP_OK) {
    total_requests++;
    waiting_response = true;
    request_sent_time = millis();

    if (use_broadcast) {
      Serial.printf("[ENVIO] Request broadcast para nodo %d\n", node_id);
    } else {
      Serial.printf("[ENVIO] Request -> Nodo %d [%02X:%02X:%02X:%02X:%02X:%02X]\n",
                    node_id,
                    target_mac[0], target_mac[1], target_mac[2],
                    target_mac[3], target_mac[4], target_mac[5]);
    }
  } else {
    Serial.printf("[ERROR] Error al enviar request: %d\n", result);
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println();
  Serial.println("============================================");
  Serial.println("  ESP32 MAESTRO - Red ESP-NOW");
  Serial.println("  6x ESP8266 + 1x ESP32");
  Serial.println("============================================");
  Serial.println();

  // LED indicador
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Inicializar WiFi en modo Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("MAC del ESP32 maestro: ");
  Serial.println(WiFi.macAddress());
  Serial.println();

  // Inicializar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[FATAL] Error al inicializar ESP-NOW");
    Serial.println("Reiniciando en 5 segundos...");
    delay(5000);
    ESP.restart();
  }

  // Registrar callbacks (API Core 3.x)
  esp_now_register_recv_cb(onDataReceived);
  esp_now_register_send_cb(onDataSent);

  // Registrar peer broadcast (para descubrimiento)
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, ESP_NOW_BROADCAST_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[WARN] No se pudo agregar peer broadcast");
  }

  Serial.println("[OK] ESP-NOW inicializado correctamente");
  Serial.println();
  Serial.println("Iniciando interrogacion de nodos...");
  Serial.printf("Intervalo entre polls: %d ms\n", POLL_INTERVAL_MS);
  Serial.printf("Timeout de respuesta:  %d ms\n", RESPONSE_TIMEOUT_MS);
  Serial.println("----------------------------------------");
  Serial.println();

  // Parpadeo de inicio
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }

  last_poll_time = millis();
}

// ============================================================
// Loop principal
// ============================================================
void loop() {
  unsigned long now = millis();

  // Verificar timeout de respuesta
  if (waiting_response && (now - request_sent_time > RESPONSE_TIMEOUT_MS)) {
    waiting_response = false;
    total_timeouts++;
    Serial.println("[TIMEOUT] No se recibio respuesta a tiempo");
    Serial.printf("  Nodo %d - Tiempo: %u ms\n",
                  nodes[current_node_idx].id,
                  now - request_sent_time);
    Serial.println();
  }

  // Enviar nuevo request si ha pasado el intervalo y no estamos esperando
  if (!waiting_response && (now - last_poll_time >= POLL_INTERVAL_MS)) {
    last_poll_time = now;

    // Enviar request al nodo actual
    sendRequest(nodes[current_node_idx].id);

    // Avanzar al siguiente nodo
    current_node_idx = (current_node_idx + 1) % NUM_NODES;
  }

  // Mostrar estadisticas cada 30 segundos
  static unsigned long last_stats_time = 0;
  if (now - last_stats_time >= 30000) {
    last_stats_time = now;
    Serial.println("=== ESTADISTICAS ===");
    Serial.printf("  Requests enviados:  %u\n", total_requests);
    Serial.printf("  Respuestas OK:      %u\n", total_responses);
    Serial.printf("  Timeouts:           %u\n", total_timeouts);

    Serial.println("  Nodos conocidos:");
    for (int i = 0; i < NUM_NODES; i++) {
      Serial.printf("    Nodo %d: %s [%02X:%02X:%02X:%02X:%02X:%02X]\n",
                    nodes[i].id,
                    nodes[i].known ? "CONOCIDO" : "DESCONOCIDO",
                    nodes[i].mac[0], nodes[i].mac[1], nodes[i].mac[2],
                    nodes[i].mac[3], nodes[i].mac[4], nodes[i].mac[5]);
    }
    Serial.println("====================");
    Serial.println();
  }
}
