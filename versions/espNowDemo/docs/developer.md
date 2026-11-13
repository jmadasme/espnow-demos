# Documentación para desarrolladores — espNowDemo

> Documentación técnica del código fuente del proyecto **espNowDemo** (netEspNow
> v1.1-fix1). Dirigida a quien quiera entender, modificar o extender el firmware.

---

## 1. Arquitectura general

El proyecto se compone de **dos tipos de firmware**:

1. **`firmware_bridge`** — Maestro. Inicia las comunicaciones, mantiene estado de la
   red y expone la interfaz Serial.
2. **Nodos esclavos**:
   - `firmware_nodo_0` … `firmware_nodo_4` — Esclavo universal (digital + ADC).
   - `firmware_nodo_6` — Esclavo DS18B20 (temperatura OneWire).
   - `firmware_nodo_7` — Esclavo GY-906 / MLX90614 (temperatura IR, I2C).
   - `firmware_nodo_8` — Esclavo GY-61 / ADXL335 (acelerómetro, clasificación de
     vibración, analógico).

### 1.1 Patrón de diseño

- **Modelo request/response síncrono por sondeo:** el Bridge pregunta y espera; los
  nodos nunca inician comunicación.
- **Procesamiento en dos niveles (ISR → loop):** los *callbacks* ESP-NOW corren en
  contexto de interrupción y **solo copian datos mínimos** a buffers `volatile`. Todo el
  trabajo pesado (lectura de sensores, Serial, `esp_now_send`) ocurre en `loop()`.
- **Autodescubrimiento:** el Bridge aprende las MAC de los nodos a partir de las
  respuestas recibidas por broadcast.

### 1.2 Mapa de archivos

```
espNowDemo/
├── firmware_bridge/firmware_bridge.ino   # Maestro / Bridge
├── firmware_nodo_0/firmware_nodo_0.ino   # Nodo universal (ID 0)
├── firmware_nodo_1/...                   # (idéntico, ID 1)
├── firmware_nodo_2/...                   # (idéntico, ID 2)
├── firmware_nodo_3/...                   # (idéntico, ID 3)
├── firmware_nodo_4/...                   # (idéntico, ID 4)
├── firmware_nodo_6/firmware_nodo_6.ino   # Nodo DS18B20 (ID 6)
├── firmware_nodo_7/firmware_nodo_7.ino   # Nodo GY-906 / MLX90614 (ID 7)
└── firmware_nodo_8/firmware_nodo_8.ino   # Nodo GY-61 / ADXL335 (ID 8)
```

> Los archivos `firmware_nodo_1..4` son copias exactas de `firmware_nodo_0` salvo
> `#define NODE_ID`. Se mantienen como carpetas separadas para poder compilar/cargar
> cada nodo sin editar el código cada vez.

---

## 2. Bridge — arquitectura del sketch

### 2.1 Estructuras de datos

```c
// Mensaje universal (13 B) — compartido con nodos universales
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 0=request, 1=response
  uint8_t  node_id;
  uint8_t  gpio_state;
  uint16_t adc_value;
  uint32_t counter;
  uint32_t uptime_ms;
} EspNowMessage;

// Respuesta de sensor (14 B) — nodo 6
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 2
  uint8_t  node_id;     // 6
  int16_t  temp_x10;
  int16_t  hum_x10;
  uint32_t counter;
  uint32_t uptime_ms;
} EspNowDhtMessage;

// Respuesta IR (14 B) — nodo 7
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;       // 3
  uint8_t  node_id;        // 7
  int16_t  obj_temp_x10;   // temp objeto °C * 10
  int16_t  amb_temp_x10;   // temp ambiente °C * 10
  uint32_t counter;
  uint32_t uptime_ms;
} EspNowIrMessage;

// Respuesta acelerómetro/vibración (16 B) — nodo 8
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 4
  uint8_t  node_id;     // 8
  uint16_t accel_x;     // valor X actual (promedio de la ventana)
  uint16_t vib_range;   // rango de vibración (max - min)
  uint8_t  vib_level;   // 0=reposo, 1=baja, 2=media, 3=alta
  uint8_t  reserved;    // reservado (0)
  uint32_t counter;
  uint32_t uptime_ms;
} EspNowAccelMessage;

// Entrada de la tabla de nodos
typedef struct {
  uint8_t id;
  uint8_t mac[6];
  bool    known;
} EspNowNode;
```

> `EspNowDhtMessage` y `EspNowIrMessage` comparten el mismo tamaño (14 B); se
> distinguen por el valor de `msg_type` (2 vs 3). `EspNowAccelMessage` es de 16 B y se
> distingue por `msg_type == 4`. El Bridge usa `g_response_data[sizeof(EspNowAccelMessage)]`
> (16 B) como buffer genérico para cubrir el mensaje más grande.

### 2.2 Estado global

| Variable | Tipo | Propósito |
|---|---|---|
| `nodes[NUM_NODES]` | `EspNowNode[9]` | Tabla de nodos (slots 0–4, 5=self, 6=DS18B20, 7=IR, 8=acelerómetro). |
| `current_node_idx` | `uint8_t` | Índice del nodo en turno para el auto-poll. |
| `last_polled_node_id` | `uint8_t` | ID del último nodo interrogado (para el timeout). |
| `auto_poll_enabled` | `bool` | Flag de auto-polling (on/off). |
| `last_poll_time` | `unsigned long` | `millis()` del último request. |
| `waiting_response` | `volatile bool` | Flag de "respuesta en vuelo". |
| `request_sent_time` | `unsigned long` | `millis()` del request en vuelo. |
| `total_requests/responses/timeouts` | `uint32_t` | Estadísticas. |
| `g_last_response_mac[6]` | `volatile uint8_t[6]` | MAC de la última respuesta (escrito en ISR). |
| `g_response_data[16]` | `volatile uint8_t[16]` | Payload de la última respuesta (ISR; 16 B = mensaje más grande). |
| `g_response_len` | `volatile uint8_t` | Longitud del payload (ISR). |
| `g_response_received` | `volatile bool` | Flag de respuesta pendiente (ISR). |

### 2.3 `setup()` — secuencia

1. Inicia Serial a 115200 y muestra banner.
2. Configura `LED_PIN` como salida (apagado = HIGH).
3. `WiFi.mode(WIFI_STA)` + `WiFi.disconnect()` (ESP-NOW no necesita conexión WiFi).
4. Guarda la MAC propia en `my_mac`.
5. Reserva el slot 5 con la MAC propia (`known = true`).
6. `esp_now_init()` → si falla, `ESP.restart()` tras 5 s.
7. `esp_now_set_self_role(ESP_NOW_ROLE_COMBO)`.
8. Registra callbacks `onDataReceived` / `onDataSent`.
9. Añade el peer broadcast `FF:FF:FF:FF:FF:FF`.
10. Añade peers directos ya conocidos (en arranque solo el propio, slot 5, que se salta).
11. Parpadeo de arranque (3×) y `last_poll_time = millis()`.

### 2.4 `loop()` — secuencia

```
loop()
 ├─ processResponse()          # volcar respuesta ISR → Serial (y aprender MAC)
 ├─ readSerialCommands()       # leer línea de comandos del usuario
 ├─ if waiting_response && (now - request_sent_time > 2000 ms)
 │    → timeout: waiting_response=false; total_timeouts++
 ├─ if auto_poll_enabled && !waiting_response && (now - last_poll_time >= 3000 ms)
 │    → avanzar si el nodo es self; sendRequest(id); current_node_idx++
 └─ cada 30 s (si auto-poll) → printStats()
```

> **Detalle importante (bug corregido):** el timeout usa un `millis()` **fresco** (no el
> `now` capturado al inicio del loop) para evitar el *underflow* cuando el request se
> envía en la misma iteración (p. ej. vía `ping`) y `request_sent_time > now`.

---

## 3. Bridge — funciones (API)

### `void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len)`
**Callback ISR.** Valida longitud y tipo (13 B → `EspNowMessage` con `msg_type==1`;
14 B → `EspNowDhtMessage`/`EspNowIrMessage` con `msg_type==2` (DS18B20) o `msg_type==3`
(IR); 16 B → `EspNowAccelMessage` con `msg_type==4` (GY-61)). Copia MAC + payload +
longitud a buffers `volatile` y activa `g_response_received`. **No hace Serial ni esperas.**

### `void onDataSent(uint8_t *mac, uint8_t status)`
**Callback ISR.** Si el envío falló (`status != 0`), limpia `waiting_response`.

### `void sendRequest(uint8_t node_id)`
Arma un `EspNowMessage` de request (`msg_type=0`) y lo envía. Si la MAC del nodo es
conocida, envía directo (registrando el peer si hace falta); si no, envía por broadcast.
Al éxito, incrementa `total_requests` y fija `waiting_response` + `request_sent_time`.

### `void processResponse()`
Llamado desde `loop`. Si `g_response_received`, copia los buffers ISR, valida el ID,
aprende la MAC (si era desconocida) y la registra como peer, e imprime la respuesta
formateada según el tipo: universal (`1`), DS18B20 (`2`), IR/GY-906 (`3`) o
acelerómetro/GY-61 (`4`). Parpadea el LED de confirmación.

### `void processSerialCommand(const char *cmd)`
Despacha los comandos de usuario: `help/?`, `list`, `stats`, `auto on/off`,
`ping <id>`, `ping all`. Devuelve mensajes de error ante entradas inválidas.

### `void readSerialCommands()`
Lee bytes del Serial y acumula una línea (hasta `SERIAL_BUF_SIZE`=32); al detectar
`\n`/`\r`, llama a `processSerialCommand`.

### `bool isSelfNode(uint8_t node_id)`
Devuelve `true` si la MAC del slot coincide con la MAC propia (evita auto-interrogarse).

### `void printStats()`
Imprime contadores, tasa de éxito y estado de la tabla.

---

## 4. Nodo universal — arquitectura

### 4.1 Estructuras y estado

- Misma `EspNowMessage` de 13 bytes.
- `NODE_ID` definido por `#define` (0–4).
- Estado compartido ISR↔loop: `g_request_pending` (volatile bool) y `g_master_mac[6]`
  (volatile), más `response_counter` y `master_peer_added`.

### 4.2 `setup()` — secuencia

1. Serial 115200 + banner con `NODE_ID` y MAC.
2. `pinMode(LED_PIN, OUTPUT)`; LED apagado (HIGH).
3. `pinMode(GPIO_DIGITAL=4, INPUT_PULLUP)` (D2).
4. `WiFi.mode(WIFI_STA)` + disconnect.
5. `esp_now_init()` + `set_self_role(COMBO)` + registrar callbacks.
6. Parpadeo de arranque (3×).

### 4.3 `loop()` — secuencia

```
loop()
 ├─ heartbeat: cada 1000 ms, LED ON 20 ms (activo LOW)
 ├─ if g_request_pending
 │    → g_request_pending=false; sendResponse()
 └─ delay(1)
```

### 4.4 Funciones del nodo universal

#### `void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len)`
**ISR.** Valida `len == sizeof(EspNowMessage)`, `msg_type==0` (request) y
`node_id == NODE_ID`. Copia la MAC del maestro y activa `g_request_pending`.

#### `void onDataSent(uint8_t *mac, uint8_t status)`
**ISR.** No hace nada.

#### `void sendResponse()`
Incrementa `response_counter`, lee `digitalRead(GPIO_DIGITAL)` y `analogRead(ADC_PIN)`
**en ese instante**, arma la respuesta, registra el peer del maestro la primera vez y
envía por ESP-NOW. Imprime una línea de depuración.

---

## 5. Nodo 6 (DS18B20) — arquitectura

### 5.1 Estructuras y estado

- `EspNowMessage` (13 B) para recibir requests.
- `EspNowSensorMessage` (14 B) para responder con `temp_x10`/`hum_x10`.
- Objetos `OneWire oneWire(4)` y `DallasTemperature sensors(&oneWire)`.
- `SENSOR_INVALID = -9999`.
- Estado: `g_request_pending`, `g_master_mac[6]`, `response_counter`, `master_peer_added`.

### 5.2 `setup()` — secuencia

1. Serial 115200 + banner con `NODE_ID`=6 y MAC.
2. WiFi Station + disconnect.
3. `sensors.begin()` y `sensors.getDeviceCount()` (imprime nº de DS18B20 encontrados).
4. ESP-NOW init + role + callbacks.
5. **Sin LED heartbeat** (D4/GPIO2 libre a propósito).

### 5.3 `loop()` — secuencia

```
loop()
 ├─ if g_request_pending
 │    → g_request_pending=false; sendSensorResponse()
 └─ delay(1)
```

### 5.4 Funciones del nodo 6

#### `void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len)`
**ISR.** Solo lee los 2 primeros bytes (`msg_type` y `node_id`) y activa el flag si es
un request dirigido a `NODE_ID`.

#### `void onDataSent(uint8_t *mac, uint8_t status)`
**ISR.** No hace nada.

#### `void sendSensorResponse()`
Incrementa el contador, llama `sensors.requestTemperatures()` (bloquea ~750 ms),
lee `getTempCByIndex(0)`. Si es `DEVICE_DISCONNECTED_C` o `isnan`, envía
`SENSOR_INVALID`; si no, `temp_x10 = tempC * 10`. `hum_x10` siempre `SENSOR_INVALID`.
Envía el mensaje de 14 bytes al maestro.

---

## 6. Nodo 7 (GY-906 / MLX90614) — arquitectura

### 6.1 Estructuras y estado

- `EspNowMessage` (13 B) para **recibir** requests (misma struct que los universales).
- `EspNowIrMessage` (14 B) para **responder** con `obj_temp_x10` y `amb_temp_x10`.
- Objeto global `Adafruit_MLX90614 mlx` (requiere `Adafruit_MLX90614.h` y `Wire.h`).
- Constantes: `MSG_TYPE_IR_RESPONSE = 3`, `SENSOR_INVALID = -9999`, `HEARTBEAT_MS = 1000`.
- Estado compartido ISR↔loop: `g_request_pending`, `g_master_mac[6]`,
  `response_counter`, `master_peer_added`, `last_heartbeat`.

### 6.2 `setup()` — secuencia

1. Serial 115200 + banner con `NODE_ID`=7 y MAC.
2. `pinMode(LED_PIN, OUTPUT)`; LED apagado (HIGH).
3. `WiFi.mode(WIFI_STA)` + `WiFi.disconnect()`.
4. `Wire.begin()` — inicializa I2C en los pines por defecto del NodeMCU:
   **SDA = D2 (GPIO4), SCL = D1 (GPIO5)**.
5. `mlx.begin()` — detecta el MLX90614 en **0x5A**. Si falla, imprime
   `[MLX90614] ERROR: no se encontro el sensor en I2C` pero **continúa** (el nodo
   seguirá respondiendo con `ERROR LECTURA` hasta que el sensor se conecte).
6. `esp_now_init()` + `set_self_role(COMBO)` + registrar callbacks.
7. Parpadeo de arranque (3×) y `last_heartbeat = millis()`.

### 6.3 `loop()` — secuencia

```
loop()
 ├─ heartbeat: cada 1000 ms, LED ON 20 ms (activo LOW)
 ├─ if g_request_pending
 │    → g_request_pending=false; sendIrResponse()
 └─ delay(1)
```

### 6.4 Funciones del nodo 7

#### `void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len)`
**ISR.** Solo lee los 2 primeros bytes (`msg_type` y `node_id`); si es un request
(`msg_type=0`) dirigido a `NODE_ID`=7, copia la MAC del maestro y activa
`g_request_pending`.

#### `void onDataSent(uint8_t *mac, uint8_t status)`
**ISR.** No hace nada.

#### `void sendIrResponse()`
Incrementa el contador y lee el MLX90614 **con reintentos**: hasta **3 intentos** de
`mlx.readObjectTempC()` + `mlx.readAmbientTempC()`, saliendo del bucle cuando ambos
valores son válidos (no `isnan`) y con `delay(100)` entre intentos. Si tras los
reintentos algún valor sigue siendo `NAN`, envía `SENSOR_INVALID` en ese campo; si no,
convierte a `(int16_t)(temp * 10.0f)`. Registra el peer del maestro la primera vez y
envía el `EspNowIrMessage` (14 B). Imprime una línea de depuración (`obj=… amb=…
contador=… send=OK/FAIL`).

> **Diferencia clave con el nodo 6:** la lectura I2C es rápida (~10–50 ms) pero puede
> fallar intermitentemente si el cableado está flojo; por eso se añaden reintentos en
> lugar de bloquear 750 ms como hace la conversión OneWire.

---

## 7. Nodo 8 (GY-61 / ADXL335) — arquitectura

El nodo 8 es **diferente al resto**: a diferencia de los nodos 0–7, que leen sus sensores
*solo cuando el Bridge los interroga*, el nodo 8 **muestrea continuamente** el acelerómetro
en segundo plano y **clasifica la vibración en tiempo real**. Cuando llega un request,
simplemente entrega la clasificación ya calculada (sin leer nada nuevo en ese instante).

### 7.1 Estructuras y estado

- `EspNowMessage` (13 B) para **recibir** requests (misma struct que los universales).
- `EspNowAccelMessage` (16 B) para **responder** con `accel_x`, `vib_range`, `vib_level`.
- Constantes de clasificación: `N_SAMPLES=50`, `SAMPLE_INTERVAL_MS=10`,
  `VIB_TH_REST=12`, `VIB_TH_LOW=40`, `VIB_TH_MED=100`.
- Constantes de nivel: `VIB_REPOSO=0`, `VIB_BAJA=1`, `VIB_MEDIA=2`, `VIB_ALTA=3`.
- **Buffer circular** `buf[N_SAMPLES]` (uint16_t) + `buf_count` (muestras válidas) +
  `buf_idx` (índice de escritura).
- Estadísticas actuales: `current_avg`, `current_range`, `current_level`.
- Estado compartido ISR↔loop: `g_request_pending`, `g_master_mac[6]`,
  `response_counter`, `master_peer_added`, `last_heartbeat`, `last_sample`.

### 7.2 `setup()` — secuencia

1. Serial 115200 + banner con `NODE_ID`=8 y MAC.
2. `pinMode(LED_PIN, OUTPUT)`; LED apagado (HIGH).
3. `WiFi.mode(WIFI_STA)` + `WiFi.disconnect()`.
4. `esp_now_init()` + `set_self_role(COMBO)` + registrar callbacks.
5. Parpadeo de arranque (3×), `last_heartbeat = millis()`, `last_sample = millis()`.

> No hay `pinMode(A0, ...)`: en ESP8266 `analogRead(A0)` no lo requiere (igual que en
> los nodos universales).

### 7.3 `loop()` — secuencia

A diferencia de los demás nodos, el loop del nodo 8 **muestrea cada 10 ms**:

```
loop()
 ├─ if now - last_sample >= 10 ms
 │    → last_sample = now; sampleAccel()        # muestrear + reclasificar
 ├─ heartbeat: cada 1000 ms, LED ON 5 µs (pulso corto, activo LOW)
 ├─ if g_request_pending
 │    → g_request_pending=false; sendAccelResponse()   # entrega clasificación actual
 └─ delay(1)
```

> El heartbeat usa `delayMicroseconds(5000)` (5 ms) en lugar de `delay(20)` para no
> interrumpir el ritmo de muestreo de 10 ms.

### 7.4 Funciones del nodo 8

#### `uint8_t categorizeVibration(uint16_t range)`
Función pura: devuelve `VIB_REPOSO` si `range < VIB_TH_REST`, `VIB_BAJA` si
`range < VIB_TH_LOW`, `VIB_MEDIA` si `range < VIB_TH_MED`, y `VIB_ALTA` en caso
contrario.

#### `void sampleAccel()`
Núcleo del algoritmo:

1. Escribe `analogRead(ADC_PIN)` en `buf[buf_idx]` y avanza `buf_idx`
   (`(buf_idx + 1) % N_SAMPLES`); incrementa `buf_count` hasta `N_SAMPLES`.
2. Recorre la ventana calculando **mínimo, máximo y suma**.
3. Actualiza `current_avg = sum / buf_count`, `current_range = max − min` y
   `current_level = categorizeVibration(current_range)`.

> Complejidad O(N) por muestra (N=50): barato y suficiente a 100 Hz. Al arrancar, antes
> de llenar la ventana, el cálculo se hace solo sobre las muestras válidas (`buf_count`).

#### `void onDataReceived(uint8_t *mac, uint8_t *data, uint8_t len)`
**ISR.** Lee los 2 primeros bytes (`msg_type`, `node_id`); si es un request (`msg_type=0`)
dirigido a `NODE_ID`=8, copia la MAC del maestro y activa `g_request_pending`.

#### `void onDataSent(uint8_t *mac, uint8_t status)`
**ISR.** No hace nada.

#### `void sendAccelResponse()`
Incrementa `response_counter`, arma `EspNowAccelMessage` con `accel_x = current_avg`,
`vib_range = current_range`, `vib_level = current_level`, `reserved = 0`,
`counter = response_counter`, `uptime_ms = millis()`. Registra el peer del maestro la
primera vez y envía el mensaje (16 B). Imprime una línea de depuración
(`x=… rango=… nivel=… contador=… send=OK/FAIL`).

> **Clave del diseño:** la lectura no se hace al recibir el request (no bloquea), porque
> la clasificación ya está lista en `current_*`. Por eso el RTT del nodo 8 es mínimo.

---

## 8. Diagrama de flujo de la lógica principal

### 8.1 Bridge

```
              ┌─────────────┐
              │   setup()   │
              └──────┬──────┘
                     ▼
              ┌─────────────┐
              │   loop()    │◄───────────────┐
              └──────┬──────┘                │
     ┌───────────────┼───────────────┐       │
     ▼               ▼               ▼       │
 processResponse  readSerial      timeout?   │
 (imprime +       (comandos)      (2 s)      │
  aprende MAC)        │               │      │
     │                │               ▼      │
     │                │         total_timeouts++
     │                │               │      │
     └──────────────► ▼               │      │
              ┌───────────────┐       │      │
              │ auto_poll &   │       │      │
              │ !waiting &    │       │      │
              │ intervalo?    │       │      │
              └──────┬────────┘       │      │
                     │ sí             │      │
                     ▼                │      │
              sendRequest(id)         │      │
              current_node_idx++      │      │
                     │                │      │
                     └────────────────┴──────┘
```

### 8.2 Nodo (universal, DS18B20, GY-906 y GY-61)

```
        ┌───────────────────────┐
        │ onDataReceived (ISR)  │
        │ request para mi ID?   │
        └───────────┬───────────┘
                    │ sí (flag + MAC)
                    ▼
        ┌───────────────────────┐
        │        loop()         │
        │  g_request_pending?   │
        └───────────┬───────────┘
                    │ sí
                    ▼
        ┌─────────────────────────────┐
        │  sendResponse() /           │
        │  sendSensorResponse() /     │
        │  sendIrResponse() /         │
        │  sendAccelResponse()        │
        │  (lee sensores + envía)     │
        └─────────────────────────────┘
```

> El flujo de *respuesta* es idéntico en los cuatro tipos de nodo; solo cambia la función
> de respuesta y el tipo de mensaje emitido (`1` universal, `2` DS18B20, `3` IR,
> `4` acelerómetro). El nodo 8 se distingue además por su **muestreo continuo en
> `loop()`** (ver [§7.3](#73-loop--secuencia)), que corre siempre, en paralelo a la
> espera de requests.

---

## 9. Guía de modificación y extensión

### 9.1 Cambiar parámetros de red

En `firmware_bridge`:

| Parámetro | `#define` | Valor actual |
|---|---|---|
| Intervalo de polling | `POLL_INTERVAL_MS` | 3000 ms |
| Timeout de respuesta | `RESPONSE_TIMEOUT_MS` | 2000 ms |
| Baudrate Serial | `SERIAL_BAUD` | 115200 |
| Tamaño buffer comandos | `SERIAL_BUF_SIZE` | 32 |

### 9.2 Agregar un nuevo nodo universal (clonar)

1. Copiar la carpeta `firmware_nodo_0` a `firmware_nodo_N`.
2. Cambiar `#define NODE_ID N`.
3. **Actualizar el Bridge:** incrementar `NUM_NODES` y añadir una entrada en la tabla
   `nodes[]` con el nuevo ID y `known = false`.
4. Compilar y cargar en una placa nueva.

### 9.3 Agregar un nodo con un sensor nuevo (digital o analógico)

- **Sensor digital en D2:** no requiere cambios de código; cablear a D2 (GPIO4) y GND.
  El nodo ya lee `digitalRead(GPIO_DIGITAL)`.
- **Sensor analógico en A0:** no requiere cambios; cablear a A0 (respetando 0–1 V /
  0–3.3 V con divisor). El valor llega como `adc_value` crudo.
- Si necesitas **convertir unidades**, modifica `sendResponse()` del nodo para enviar el
  valor convertido (o cambia el `adc_value` por una magnitud física), o convierte en el
  lado del PC.

### 9.4 Agregar un nodo con un sensor I2C nuevo

El nodo 7 sirve de plantilla para cualquier sensor I2C:

1. Copiar `firmware_nodo_7` a `firmware_nodo_N` y cambiar `NODE_ID`.
2. Sustituir el objeto `Adafruit_MLX90614` por la librería del sensor nuevo e
   inicializarlo en `setup()` con `Wire.begin()` (SDA=D2, SCL=D1).
3. Ajustar `sendIrResponse()` (o renombrarla) para leer el sensor y rellenar un
   `EspNowIrMessage` propio (o definir una nueva struct de 14 B si cambia el payload).
4. Registrar el nuevo nodo en el Bridge (`NUM_NODES`, tabla `nodes[]`) y, si el payload
   cambia, añadir un `msg_type` nuevo y su formateo en `processResponse()`.

### 9.5 Agregar un tipo de mensaje nuevo

1. Definir la nueva `struct` con `__attribute__((packed))` en **ambos lados** (Bridge y
   nodo) manteniendo el mismo layout de bytes.
2. Asignar un `msg_type` nuevo (p. ej. `5`) que no colisione (ya usados: 0–4).
3. En el Bridge, en `onDataReceived`, aceptar la nueva longitud/tipo, y en
   `processResponse` formatear la salida.
4. Registrar el nuevo nodo en la tabla si es necesario.

### 9.6 Añadir autenticación o cifrado (fuera del alcance v1.1)

Para cifrado, usar la API de claves ESP-NOW (`esp_now_set_kok`) o migrar a la API nueva
(`esp_now_add_peer` con `lmk`). Nota: la versión v1.1-fix1 **no** implementa cifrado por
filosofía de simplicidad.

### 9.7 Conectar LoRaWAN (Fase 2)

Los pines **D5 (GPIO14, RX)** y **D6 (GPIO12, TX)** están reservados para un
`SoftwareSerial` que conecte un módulo LoRaWAN por cable serial TTL-TTL (texto plano).
Para habilitarlo, instanciar `SoftwareSerial lora(RX=14, TX=12)` y añadir el manejo en
`loop()`. No usar esos pines para sensores.

---

## 10. Convenciones y buenas prácticas del código

- **ISR vs loop:** los *callbacks* ESP-NOW (`onDataReceived`, `onDataSent`) corren en
  contexto ISR. **Prohibido** hacer `Serial.print`, `delay` o `memcpy` grandes dentro.
  Solo copiar a buffers `volatile` y poner flags.
- **Flags atómicos:** usar `volatile bool` de 1 byte para la señalización ISR→loop
  (escrituras atómicas en AVR/ESP8266).
- **Estructuras `packed`:** obligatorio `__attribute__((packed))` para que el layout de
  bytes coincida entre emisor y receptor.
- **`LED_ON`/`LED_OFF`:** el LED de la NodeMCU es activo en LOW; usar las macros para
  no confundirse.
- **No bloquear en loop:** evitar `delay` largos (salvo los 20 ms del heartbeat y el
  `delay(1)` de guarda). La lectura DS18B20 (~750 ms) se acepta por diseño, y por eso
  el Bridge usa timeout de 2000 ms. El nodo 8 usa `delayMicroseconds(5000)` en su
  heartbeat y **no bloquea**: el muestreo (cada 10 ms) y la clasificación son no
  bloqueantes y corren siempre en `loop()`.

---

## 11. Diagrama de datos (resumen)

```
Bridge (maestro)                          Nodo (esclavo)
     │                                        │
     │── request {0, id, 0,0,0,0} ──────────► │   (broadcast o directo)
     │                                        │ lee sensores
     │◄─ response {1, id, gpio, adc, ctr, up}─│   (nodo universal 0-4)
     │                                        │
     │── request {0, 6, ...} ───────────────► │   (nodo 6)
     │◄─ response {2, 6, temp, -9999, ctr, up}│   (DS18B20)
     │                                        │
     │── request {0, 7, ...} ───────────────► │   (nodo 7)
     │◄─ response {3, 7, obj, amb, ctr, up}───│   (GY-906 / IR)
     │                                        │
     │── request {0, 8, ...} ───────────────► │   (nodo 8)
     │◄─ response {4, 8, x, rango, nivel, 0, │   (GY-61 / acelerómetro)
     │             ctr, up}                   │
     │                                        │
     │   Serial.print → PC (115200 baud)      │
```

> El nodo 8 muestrea continuamente en segundo plano; la "respuesta" solo empaqueta la
> clasificación ya calculada (`accel_x`, `vib_range`, `vib_level`).

---

## 12. Referencia rápida de constantes

| Constante | Valor | Archivo |
|---|---|---|
| `POLL_INTERVAL_MS` | 3000 | bridge |
| `RESPONSE_TIMEOUT_MS` | 2000 | bridge |
| `SERIAL_BAUD` | 115200 | todos |
| `NUM_NODES` | 9 | bridge |
| `MSG_TYPE_REQUEST` | 0 | todos |
| `MSG_TYPE_RESPONSE` | 1 | todos |
| `MSG_TYPE_SENSOR_RESP` / `MSG_TYPE_DHT_RESPONSE` | 2 | nodo 6 / bridge |
| `MSG_TYPE_IR_RESPONSE` | 3 | nodo 7 / bridge |
| `MSG_TYPE_ACCEL_RESPONSE` | 4 | nodo 8 / bridge |
| `SENSOR_INVALID` / `DHT_INVALID` | -9999 | nodo 6 / bridge |
| `GPIO_DIGITAL` | 4 (D2) | nodo universal |
| `ADC_PIN` | A0 | nodo universal + nodo 8 |
| `ONE_WIRE_BUS` | 4 (D2) | nodo 6 |
| `SDA` / `SCL` (I2C) | 4 (D2) / 5 (D1) | nodo 7 |
| `N_SAMPLES` | 50 | nodo 8 |
| `SAMPLE_INTERVAL_MS` | 10 | nodo 8 |
| `VIB_TH_REST` / `VIB_TH_LOW` / `VIB_TH_MED` | 12 / 40 / 100 | nodo 8 |
| `VIB_REPOSO` / `VIB_BAJA` / `VIB_MEDIA` / `VIB_ALTA` | 0 / 1 / 2 / 3 | nodo 8 |
| `HEARTBEAT_MS` | 1000 | nodo universal + nodo 7 + nodo 8 |
| `LED_PIN` | LED_BUILTIN (GPIO2) | bridge + nodo universal + nodo 7 + nodo 8 |
