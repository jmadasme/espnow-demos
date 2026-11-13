# ESP-NOW Demos · ESP-NOW · LoRaWAN

![Estado](https://img.shields.io/badge/estado-preservado-2A9D8F?style=flat-square)
![Versiones](https://img.shields.io/badge/versiones-3-457B9D?style=flat-square)
![Sección](https://img.shields.io/badge/sección-iot-5B7C99?style=flat-square)
![Licencia](https://img.shields.io/badge/licencia-MIT-06141b?style=flat-square)

> Demos de campo con ESP-NOW y LoRaWAN: bridge, nodos, GUI en Python y dos radios cooperando.

Este repositorio reúne **3 versiones** de ESP-NOW Demos en `versions/`.
Cada carpeta conserva el contenido original de su etapa, sin `.git`, dependencias
instaladas ni binarios pesados. La documentación de operación y de código está en
[`docs/`](docs/), y el sitio del proyecto en [`docs/index.html`](docs/index.html).

## Índice de versiones

| Versión | Qué aporta | Archivos |
|---|---|---:|
| [`espNowDemo`](versions/espNowDemo/) | Maleta de demostración: bridge, nueve nodos de sensores y GUIs en Python; basada en netEspNow v1.1-fix1. | 33 |
| [`espNowDemoLoRaWAN1.1`](versions/espNowDemoLoRaWAN1.1/) | Dos firmwares que cooperan: ESP-NOW para el tramo local y LoRaWAN para el enlace largo. | 7 |
| [`red_espnow_8266_32`](versions/red_espnow_8266_32/) | Firmwares maestro y nodo para una red mixta de ESP8266 y ESP32. | 4 |

La versión de referencia de esta familia es **`espNowDemo`**: es la que se usa
para el README base y para los documentos canónicos. Las demás se mantienen
completas para que se pueda reconstruir la evolución y comparar decisiones.

## Arranque rápido

```bash
git clone https://github.com/jmadasme/espnow-demos.git
cd espnow-demos/versions/espNowDemo
ls
# cargá bridge y nodos; luego ejecutá la GUI de Python
```

Las credenciales LoRaWAN y WiFi de la demo no se incluyen. La GUI requiere Python y las dependencias de su carpeta.

## Documentación

- [`docs/datasheet.md`](docs/datasheet.md) — instalación, operación y límites.
- [`docs/developer.md`](docs/developer.md) — arquitectura, módulos y extensión.
- [`docs/REFERENCES.md`](docs/REFERENCES.md) — estándares, librerías y fuentes.
- [`docs/index.html`](docs/index.html) — página del proyecto (GitHub Pages).

## Alcance y honestidad

## espNowDemo — Red de Sensores Inalámbrica ESP-NOW (Demostración / Maleta)

> **Versión:** netEspNow v1.1-fix1 · **Topología:** Estrella · **Plataforma:** ESP8266 (NodeMCU v3 / ESP-12E)

**espNowDemo** es una red de sensores inalámbrica basada en **ESP-NOW** pensada para
**ferias, demos y maletas didácticas**. Un nodo **Bridge (maestro)** interroga
secuencialmente a una serie de **nodos sensores** (esclavos) y muestra los datos por
el puerto **Serial** (UART0, 115200 baud). El operador humano controla la red con
comandos de texto.

Sigue la filosofía *"simple y robusta"* de la versión v1.1:

- Sin gateway intermedio (el Bridge es el punto central).
- Sin cifrado ESP-NOW.
- Sin EEPROM (las MAC se aprenden por **autodescubrimiento** vía broadcast).
- Sin watchdog (si un nodo no responde, el Bridge hace *timeout* y sigue).

> 🖥️ **Panel de control (GUI):** este proyecto incluye una aplicación de escritorio
> en Python (`espnow_gui.py`, PyQt6) para operar la red con tarjetas visuales por nodo,
> botón de consulta circular y efecto hover. Ver [docs/gui.md](docs/gui.md).
>
> 🚰 **Extensión Zigbee (GUI de válvulas):** `valvulas_gui.py` (+ `valvulas.py`), una
> GUI con el mismo estilo RAF que controla las válvulas de agua Zigbee (SONOFF SWV-NH)
> y el interruptor ON/OFF vía MQTT/Zigbee2MQTT. Se ejecuta en la Raspberry Pi que corre
> el broker (por defecto `127.0.0.1:1883`, configurable): `python3 valvulas_gui.py`.
> Requiere `pip install paho-mqtt` + PyQt6.
>
> 📡 **Nodo LoRaWAN (GUI de radio):** `cubecell_gui.py` (PyQt6) es el panel de escritorio
> estilo RAF para operar el nodo **Heltec CubeCell HTCC-AB01** conectado por USB
> (CP2102, 9600 8N1): terminal serial en vivo, envío de datos (HEX o ASCII), recepción
> de downlinks e indicadores de sesión/radio. Detección automática del puerto del nodo.
> Ejecutar: `python3 cubecell_gui.py`. Ver [docs/gui_cubecell.md](docs/gui_cubecell.md).

---

### Índice

1. [Descripción general](#descripción-general)
2. [Placa objetivo y especificaciones](#placa-objetivo-y-especificaciones)
3. [Principio de funcionamiento](#principio-de-funcionamiento)
4. [Forma de operar (comandos)](#forma-de-operar-comandos)
5. [Pinout (tabla de pines)](#pinout-tabla-de-pines)
6. [Diagrama de conexión](#diagrama-de-conexión)
7. [Compilación y carga](#compilación-y-carga)
8. [Ejemplos de uso](#ejemplos-de-uso)
9. [Calibración](#calibración)
10. [Advertencias y precauciones](#advertencias-y-precauciones)

---

### Descripción general

La red está formada por **9 firmwares**, todos para **ESP8266 NodeMCU v3 (ESP-12E)**:

| Firmware | Rol | ID | Descripción |
|---|---|---|---|
| `firmware_bridge` | Maestro/Bridge | 5 (self) | Interroga a los nodos, muestra datos por Serial, gestiona comandos. |
| `firmware_nodo_0` … `firmware_nodo_4` | Esclavo universal | 0…4 | Lee 1 entrada digital + 1 entrada analógica (ADC). |
| `firmware_nodo_6` | Esclavo DS18B20 | 6 | Lee temperatura con DS18B20 (OneWire). |
| `firmware_nodo_7` | Esclavo GY-906 | 7 | Lee temperatura IR sin contacto con MLX90614 (I2C). |
| `firmware_nodo_8` | Esclavo GY-61 | 8 | Lee aceleración del eje X (ADXL335) y **clasifica la vibración** en tiempo real. |

Los **5 nodos universales** son exactamente el mismo código; solo cambia
`#define NODE_ID`. Esto permite "clonar" un nodo genérico y cablearle distintos
sensores físicos según la necesidad.

#### Sensores de la maleta (cableado físico)

El mapeo de sensores **no está hardcodeado** en el firmware: cualquier nodo universal
puede leer cualquier sensor compatible. La asignación recomendada para la maleta es:

| Nodo | Sensor | Pin | Tipo de lectura |
|---|---|---|---|
| 0 | Reed magnético **MC-38** (puerta) | D2 (GPIO4) | Digital (pull-up) |
| 1 | PIR **HC-SR501** (movimiento) | D2 (GPIO4) | Digital |
| 2 | **LDR** (luz) | A0 | Analógica (0–1023) |
| 3 | **MQ135** (calidad de aire) | A0 | Analógica (0–1023) |
| 4 | **LM35 / TMP36** (temperatura) | A0 | Analógica (0–1023) |
| 6 | **DS18B20** (temperatura, sonda acero) | D2 (GPIO4) | OneWire (digital) |
| 7 | **GY-906 / MLX90614** (temperatura IR sin contacto) | D1 (GPIO5) + D2 (GPIO4) | I2C (dirección 0x5A) |
| 8 | **GY-61 / ADXL335** (acelerómetro 3 ejes / vibración) | A0 (X_OUT) | Analógica (eje X, 0–1023) |

---

### Placa objetivo y especificaciones

- **Placa:** NodeMCU v3 (módulo **ESP-12E**, chip ESP8266EX, conversor USB **CP2102**).
- **FQBN (arduino-cli / Arduino IDE):** `esp8266:esp8266:nodemcuv2`
- **CPU:** Tensilica L106 32-bit @ 80 MHz (hasta 160 MHz).
- **RAM:** ~80 KB disponibles para el usuario.
- **Flash:** 4 MB (SPIFFS no utilizado en este proyecto).
- **WiFi:** 802.11 b/g/n (2.4 GHz) — usado solo en modo *Station* para ESP-NOW (no se conecta a ningún AP).
- **GPIO digitales:** hasta 11 utilizables; **entradas de 3.3 V** (¡NO toleran 5 V!).
- **ADC:** 1 canal (A0), resolución 10 bits (0–1023), rango 0–1.0 V nativo.
- **Alimentación:** 5 V por micro-USB (regulador interno a 3.3 V), pin `3V3`, o pin `VIN` (5–9 V). Lógica a 3.3 V.
- **Puerto Serial:** UART0 a 115200 baud (pines TX/GPIO1 y RX/GPIO3).

> ⚠️ El ADC del ESP8266 mide **0 – 1.0 V** de forma nativa. La placa NodeMCU suele
> incluir un divisor resistivo (220 kΩ / 100 kΩ) que amplía el rango a **0 – 3.3 V**.
> Ver [Advertencias y precauciones](#advertencias-y-precauciones).

---

### Principio de funcionamiento

La red usa una **topología en estrella** con un único maestro (Bridge):

1. El **Bridge** envía un mensaje *request* (`msg_type=0`) dirigido a un `node_id`,
   por broadcast `FF:FF:FF:FF:FF:FF` si desconoce su MAC, o directo si ya la aprendió.
2. El **nodo** cuyo `node_id` coincide lee sus sensores **en ese instante** y responde
   con un mensaje *response* (`msg_type=1` universal, `msg_type=2` DS18B20,
   `msg_type=3` IR/GY-906 o `msg_type=4` acelerómetro/GY-61).
3. El **Bridge** recibe la respuesta (en un *callback* ESP-NOW que corre en contexto
   ISR), la copia a un buffer y la procesa en `loop()`.
4. Si no llega respuesta en **2000 ms**, se declara *timeout* y se pasa al siguiente nodo.
5. El ciclo se repite cada **3000 ms** (auto-polling), o bajo demanda con `ping`.

#### Protocolo ESP-NOW (estructuras empaquetadas)

**Mensaje universal — 13 bytes** (`EspNowMessage`), usado por Bridge y nodos 0–4:

```c
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 0 = request (bridge→nodo), 1 = response (nodo→bridge)
  uint8_t  node_id;     // ID destino/origen
  uint8_t  gpio_state;  // estado digital 0/1
  uint16_t adc_value;   // valor ADC 0–1023
  uint32_t counter;     // contador correlativo del nodo
  uint32_t uptime_ms;   // tiempo de actividad del nodo (ms)
} EspNowMessage;
```

**Mensaje de sensor — 14 bytes** (`EspNowSensorMessage` / `EspNowDhtMessage`), nodo 6:

```c
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 2 = respuesta de sensor (nodo 6)
  uint8_t  node_id;     // 6
  int16_t  temp_x10;    // temperatura °C * 10  (ej: 245 = 24.5 °C)
  int16_t  hum_x10;     // humedad % * 10        (ej: 455 = 45.5 %)
  uint32_t counter;     // contador correlativo
  uint32_t uptime_ms;   // tiempo de actividad (ms)
} EspNowSensorMessage;
```

- `temp_x10 = -9999` → lectura inválida (sensor desconectado).
- `hum_x10 = -9999` → **N/A** (el DS18B20 no mide humedad).

**Mensaje IR (GY-906) — 14 bytes** (`EspNowIrMessage`), nodo 7:

```c
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;       // 3 = respuesta IR (nodo 7)
  uint8_t  node_id;        // 7
  int16_t  obj_temp_x10;   // temperatura del objeto °C * 10
  int16_t  amb_temp_x10;   // temperatura ambiente °C * 10
  uint32_t counter;        // contador correlativo
  uint32_t uptime_ms;      // tiempo de actividad (ms)
} EspNowIrMessage;
```

- `obj_temp_x10 = -9999` o `amb_temp_x10 = -9999` → lectura inválida (error I2C).

**Mensaje de aceleración/vibración (GY-61) — 16 bytes** (`EspNowAccelMessage`), nodo 8:

```c
typedef struct __attribute__((packed)) {
  uint8_t  msg_type;    // 4 = respuesta de aceleración (nodo 8)
  uint8_t  node_id;     // 8
  uint16_t accel_x;     // valor X actual (promedio de la ventana, 0–1023)
  uint16_t vib_range;   // rango de vibración (max - min de la ventana, 0–1023)
  uint8_t  vib_level;   // 0=reposo, 1=vibración baja, 2=media, 3=alta
  uint8_t  reserved;    // reservado (0)
  uint32_t counter;     // contador correlativo
  uint32_t uptime_ms;   // tiempo de actividad (ms)
} EspNowAccelMessage;
```

- `vib_level`: **0** = REPOSO, **1** = VIBRACIÓN BAJA, **2** = VIBRACIÓN MEDIA,
  **3** = VIBRACIÓN ALTA (según el rango `max−min` de la ventana de muestras).

#### Autodescubrimiento de MAC

El Bridge arranca con los slots 0–4, 6, 7 y 8 en estado **DESCONOCIDO** (MAC en ceros).
Cuando debe interrogar a un nodo desconocido, envía el request por **broadcast**. Al
recibir la respuesta, **aprende** la MAC (del campo `mac` del callback), la guarda en la
tabla y registra un *peer* directo. A partir de ahí, la comunicación es punto a punto.

> El slot **5** queda reservado para el propio Bridge (auto-identificación) y nunca se
> interroga.

---

### Forma de operar (comandos)

Conecta el Bridge por USB a un ordenador, abre el **monitor Serial a 115200 baud** y
escribe los siguientes comandos (terminados con ENTER):

| Comando | Descripción |
|---|---|
| `help` / `?` | Muestra la lista de comandos disponibles. |
| `ping <id>` | Interroga **una vez** a un nodo concreto (`0`-`4`, `6`-`8`). |
| `ping all` | Interroga a **todos** los nodos en secuencia. |
| `list` | Muestra la tabla de nodos y su estado (BRIDGE / CONOCIDO / DESCONOCIDO). |
| `stats` | Muestra estadísticas: requests, respuestas, timeouts y tasa de éxito. |
| `auto on` | Activa la interrogación automática (cada 3 s). |
| `auto off` | Desactiva la interrogación automática (solo responde a comandos). |

El Bridge arranca con **auto-polling activado** (`auto on` por defecto). Cada 30 s, si
el auto-poll está activo, imprime automáticamente las estadísticas.

---

### Pinout (tabla de pines)

#### Bridge (maestro) — NodeMCU v3

| Pin Arduino | GPIO | Modo | Componente conectado | Notas |
|---|---|---|---|---|
| D4 | GPIO2 | OUTPUT | LED_BUILTIN (azul) | Confirmación visual; **activo en LOW** (`LED_ON = LOW`). |
| D9 / TX | GPIO1 | OUTPUT | UART0 TX | Serial 115200 baud (hacia el ordenador). |
| D10 / RX | GPIO3 | INPUT | UART0 RX | Serial 115200 baud (desde el ordenador). |
| D5 | GPIO14 | — | *(reservado)* | Reservado SoftwareSerial RX (Fase 2, LoRaWAN). |
| D6 | GPIO12 | — | *(reservado)* | Reservado SoftwareSerial TX (Fase 2, LoRaWAN). |
| D2 | GPIO4 | — | *(sin uso)* | Solo los nodos leen sensores. |
| A0 | — | — | *(sin uso)* | Solo los nodos leen sensores. |
| 3V3 | — | Power | Alimentación 3.3 V | Salida regulada. |
| GND | — | Power | Tierra común | — |
| VIN/5V | — | Power | Alimentación 5 V | Entrada USB/regulador. |

#### Nodos universales 0–4 — NodeMCU v3

| Pin Arduino | GPIO | Modo | Componente conectado | Notas |
|---|---|---|---|---|
| D4 | GPIO2 | OUTPUT | LED_BUILTIN (azul) | **Heartbeat** (parpadea cada 1 s); activo en LOW. |
| D2 | GPIO4 | INPUT_PULLUP | Sensor digital (reed / PIR) | Pull-up interno; el sensor cierra a GND. |
| A0 | — | INPUT (analógico) | Sensor analógico (LDR / MQ135 / LM35) | Lectura 0–1023. |
| D5 | GPIO14 | — | *(reservado)* | Reservado SoftwareSerial RX (Fase 2). |
| D6 | GPIO12 | — | *(reservado)* | Reservado SoftwareSerial TX (Fase 2). |
| D9 / TX | GPIO1 | OUTPUT | UART0 TX | Serial de depuración (115200). |
| D10 / RX | GPIO3 | INPUT | UART0 RX | Serial de depuración (115200). |
| 3V3 | — | Power | Alimentación sensores | — |
| GND | — | Power | Tierra común | — |

#### Nodo 6 (DS18B20) — NodeMCU v3

| Pin Arduino | GPIO | Modo | Componente conectado | Notas |
|---|---|---|---|---|
| D2 | GPIO4 | INPUT | **DS18B20 DATA** | Bus OneWire; con pull-up 4.7 kΩ a 3V3. |
| 3V3 | — | Power | DS18B20 VCC (rojo) | — |
| GND | — | Power | DS18B20 GND (negro) | — |
| D4 | GPIO2 | — | *(libre)* | El nodo 6 **no** usa LED; queda disponible. |
| D5 | GPIO14 | — | *(reservado)* | Reservado SoftwareSerial RX (Fase 2). |
| D6 | GPIO12 | — | *(reservado)* | Reservado SoftwareSerial TX (Fase 2). |
| D9 / TX | GPIO1 | OUTPUT | UART0 TX | Serial de depuración (115200). |
| D10 / RX | GPIO3 | INPUT | UART0 RX | Serial de depuración (115200). |

#### Nodo 7 (GY-906 / MLX90614) — NodeMCU v3

| Pin Arduino | GPIO | Modo | Componente conectado | Notas |
|---|---|---|---|---|
| D2 | GPIO4 | I2C SDA | **GY-906 SDA** | Bus I2C; el módulo trae pull-ups. |
| D1 | GPIO5 | I2C SCL | **GY-906 SCL** | Bus I2C; el módulo trae pull-ups. |
| D4 | GPIO2 | OUTPUT | LED_BUILTIN (azul) | **Heartbeat** (parpadea cada 1 s); activo en LOW. |
| 3V3 | — | Power | GY-906 VIN | El módulo regula a 3.3 V internamente. |
| GND | — | Power | GY-906 GND | Tierra común. |
| D5 | GPIO14 | — | *(reservado)* | Reservado SoftwareSerial RX (Fase 2). |
| D6 | GPIO12 | — | *(reservado)* | Reservado SoftwareSerial TX (Fase 2). |
| D9 / TX | GPIO1 | OUTPUT | UART0 TX | Serial de depuración (115200). |
| D10 / RX | GPIO3 | INPUT | UART0 RX | Serial de depuración (115200). |

#### Nodo 8 (GY-61 / ADXL335) — NodeMCU v3

| Pin Arduino | GPIO | Modo | Componente conectado | Notas |
|---|---|---|---|---|
| A0 | — | INPUT (analógico) | **GY-61 X_OUT** | Salida del eje X del ADXL335 (0–1023). |
| D4 | GPIO2 | OUTPUT | LED_BUILTIN (azul) | **Heartbeat** (parpadea cada 1 s); activo en LOW. |
| 3V3 | — | Power | GY-61 VCC | El ADXL335 debe alimentarse a **3.3 V** (ver precauciones). |
| GND | — | Power | GY-61 GND | Tierra común. |
| D5 | GPIO14 | — | *(reservado)* | Reservado SoftwareSerial RX (Fase 2). |
| D6 | GPIO12 | — | *(reservado)* | Reservado SoftwareSerial TX (Fase 2). |
| D9 / TX | GPIO1 | OUTPUT | UART0 TX | Serial de depuración (115200). |
| D10 / RX | GPIO3 | INPUT | UART0 RX | Serial de depuración (115200). |

> ⚠️ **Ejes Y y Z:** el módulo GY-61 expone `X_OUT`, `Y_OUT` y `Z_OUT`, pero el
> ESP8266 **solo tiene un ADC** (A0). Por eso este nodo monitoriza únicamente el **eje X**.
> Para medir otro eje, cablea `Y_OUT` o `Z_OUT` a A0 (manteniendo el resto igual).

#### Tabla de MACs reales (tarjetas utilizadas)

| Dispositivo | MAC |
|---|---|
| Bridge | `48:3F:DA:05:86:36` |
| Nodo 0 | `84:F3:EB:7C:99:1E` |
| Nodo 1 | `80:7D:3A:2B:E3:59` |
| Nodo 2 | `8C:AA:B5:0F:39:7F` |
| Nodo 3 | `50:02:91:DE:0D:7B` |
| Nodo 4 | `48:3F:DA:AA:8E:6D` |
| Nodo 6 | `48:3F:DA:05:FC:85` |
| Nodo 7 | `E0:98:06:23:BA:E9` |
| Nodo 8 | `48:3F:DA:05:9C:C8` |

> ℹ️ Las MAC no necesitan estar hardcodeadas: el Bridge las aprende por
> autodescubrimiento. La tabla es útil para verificación manual.

---

### Diagrama de conexión

A continuación se describe, sensor por sensor, cómo cablear cada nodo de la maleta.
Todas las conexiones se hacen sobre la **NodeMCU v3** del nodo correspondiente.

#### Nodo 0 — Reed magnético MC-38 (detección de puerta/ventana)

```
                NodeMCU v3 (nodo 0)
                    ┌─────────────┐
 MC-38 ── (A) ──────┤ D2 (GPIO4)  │  INPUT_PULLUP
 MC-38 ── (B) ──────┤ GND         │
                    └─────────────┘
```

- **MC-38** tiene 2 cables (no polarizado). Uno a **D2 (GPIO4)** y el otro a **GND**.
- Con el imán cerca, el contacto se **cierra** y D2 lee **LOW** (0). Con la puerta
  abierta, el contacto se abre y D2 lee **HIGH** (1) gracias al pull-up interno.
- No necesita componentes externos (usa `INPUT_PULLUP`).

#### Nodo 1 — PIR HC-SR501 (detección de movimiento)

```
                     HC-SR501
                 ┌──────────────┐
   VCC ──────────┤ VCC (5V)     │
   GND ──────────┤ GND          │
   OUT ──────────┤ D2 (GPIO4)   │  (NodeMCU)
                 └──────────────┘
```

- **VCC** del PIR → **VIN/5V** de la NodeMCU (el HC-SR501 funciona de 5 a 20 V).
- **GND** → **GND**.
- **OUT** → **D2 (GPIO4)**. La salida del PIR es de ~3.3 V cuando detecta movimiento.
- Ajusta los dos potenciómetros del PIR: **retardo** (tiempo en HIGH) y **sensibilidad**
  (alcance). Normalmente se deja el jumper en modo *re-triggerable*.
- Como el firmware usa `INPUT_PULLUP` y el PIR empuja su salida activa, en reposo
  suele leer LOW (0) y en detección HIGH (1).

#### Nodo 2 — LDR (nivel de luz)

```
               ┌─────────────┐
   3V3 ────────┤ LDR         │
               ├───┐         │
               │   └─────────┤ A0 (ADC)
   GND ────────┤ R 10 kΩ     │
               └─────────────┘
```

- Forma un **divisor de tensión**: un extremo del LDR a **3V3**, el otro extremo al
  punto medio (que va a **A0**) y, desde ese mismo punto medio, una **resistencia de
  10 kΩ a GND**.
- A más luz, menor resistencia del LDR → mayor tensión en A0 (valor ADC más alto).
- El LDR no es polarizado; puede ir en cualquier orientación física.

#### Nodo 3 — MQ135 (calidad de aire / gases)

```
                     MQ135 (módulo)
                 ┌──────────────────────┐
   VCC ──────────┤ VCC (5V)             │
   GND ──────────┤ GND                  │
   AO  ──────────┤ A0 (ADC)  (NodeMCU)  │
   DO  (opcional)┤ —                    │
                 └──────────────────────┘
```

- **VCC** del módulo MQ135 → **VIN/5V** (el calentador del sensor requiere 5 V y
  corriente; no alimentarlo desde 3V3).
- **GND** → **GND** (tierra común con la NodeMCU).
- **AO** (salida analógica) → **A0**.
- El **DO** (salida digital con umbral) no se usa en este proyecto (dejarlo sin conectar).
- ⚠️ **Precalentamiento:** el MQ135 necesita **24–48 h** de precalentamiento para lecturas
  estables; para demos basta con dejar que caliente unos minutos.
- Ajustar el potenciómetro del módulo solo afecta al umbral del comparador (DO), no a AO.

#### Nodo 4 — LM35 / TMP36 (temperatura analógica)

```
                LM35 / TMP36 (TO-92)
              ┌────────────────────┐
  VIN/5V ─────┤ VCC (pin central)  │   (LM35: 4-20V · TMP36: 2.7-5.5V)
  A0 ─────────┤ VOUT                │
  GND ────────┤ GND                 │
              └────────────────────┘
```

- **LM35**: mirando el lado plano, patillas = `VCC (izq) · VOUT (centro) · GND (der)`.
  Rango 0–100 °C, salida **10 mV/°C** (0 V = 0 °C).
- **TMP36**: pinout distinto = `VCC · VOUT · GND`, salida **10 mV/°C** con offset de
  0.5 V (0.5 V = 0 °C, 0.75 V = 25 °C).
- **VOUT → A0**. El valor ADC debe convertirse a °C por software (ver [Calibración](#calibración)).
  La red **no** aplica conversión automática: envía el ADC crudo 0–1023.
- **Importante:** alimentar el sensor a **3.3 V o 5 V** según el modelo. El ADC del
  NodeMCU lee hasta ~1 V (o 3.3 V con divisor) — si el sensor entrega más, recorta a 1023.

#### Nodo 6 — DS18B20 (temperatura digital, sonda de acero)

```
                    DS18B20 (3 cables)
              ┌───────────────────────────┐
   3V3 ───────┤ VCC (rojo)                │
   GND ───────┤ GND (negro)               │
   D2 (GPIO4)─┤ DATA (amarillo/blanco)    │
              ├───┬───────────────────────┤
   3V3 ───────┤   │  R 4.7 kΩ (pull-up)   │
              └───┴───────────────────────┘
```

- **VCC (rojo)** → **3V3**, **GND (negro)** → **GND**, **DATA (amarillo)** → **D2 (GPIO4)**.
- Colocar una **resistencia de pull-up de 4.7 kΩ entre DATA y 3V3** (indispensable para
  OneWire). Muchas sondas ya la traen integrada en el cable.
- El firmware usa librerías **OneWire** + **DallasTemperature** y envía la temperatura
  real en °C (ya convertida), a diferencia de los nodos analógicos.
- La lectura bloquea ~750 ms (resolución 12 bits); el Bridge tiene timeout de 2000 ms
  para dar margen.

#### Nodo 7 — GY-906 / MLX90614 (termómetro IR sin contacto)

```
                    GY-906 (MLX90614)
              ┌─────────────────────────────┐
   3V3 ───────┤ VIN (VCC)                   │
   GND ───────┤ GND                         │
   D2 (GPIO4)─┤ SDA                         │   (NodeMCU v3)
   D1 (GPIO5)─┤ SCL                         │
              └─────────────────────────────┘
```

- **VIN** → **3V3** (o **5V/VIN**; el módulo GY-906 trae un regulador de 3.3 V y
  *pull-ups* I2C de 4.7 kΩ integrados).
- **GND** → **GND** (tierra común).
- **SDA** → **D2 (GPIO4)** y **SCL** → **D1 (GPIO5)**: son los pines I2C por defecto del
  NodeMCU (`Wire.begin()` sin argumentos).
- El sensor se alimenta y se consulta por I2C en la dirección **0x5A** (por defecto).
- El firmware usa la librería **Adafruit MLX90614** y envía **dos** temperaturas:
  la del **objeto** apuntado (`obj_temp`) y la **ambiente** (`amb_temp`).
- ⚠️ **I2C a 3.3 V:** el bus debe quedar a nivel lógico de 3.3 V (el GY-906 ya se
  encarga de esto con su regulador y *pull-ups*). No añadas *pull-ups* externos a 5 V.

#### Nodo 8 — GY-61 / ADXL335 (acelerómetro, clasificación de vibración)

```
                    GY-61 (ADXL335)
              ┌─────────────────────────────┐
   3V3 ───────┤ VCC                          │
   GND ───────┤ GND                          │
   A0  ───────┤ X_OUT                        │   (NodeMCU v3)
              │ Y_OUT  ── (no usado)         │
              │ Z_OUT  ── (no usado)         │
              └─────────────────────────────┘
```

- **VCC** → **3V3** (el ADXL335 está diseñado para alimentarse a 3.3 V; algunos módulos
  GY-61 traen regulador y aceptan 5 V en `VCC`/`VIN`, pero lo seguro es usar 3.3 V).
- **GND** → **GND** (tierra común).
- **X_OUT** → **A0**: salida analógica del eje X. En reposo (1 g sobre el eje vertical
  correspondiente) la salida ronda la **mitad de escala** (~1.65 V → ADC ≈ 512 con el
  divisor de la NodeMCU).
- **Y_OUT** y **Z_OUT** se dejan sin conectar (el ESP8266 solo tiene un ADC).
- **Conexiones opcionales del módulo:** algunos GY-61 traen `ST` (self-test) y pines
  extra; en este proyecto se dejan sin conectar.

#### Resumen visual de la red

```
                    ┌──────────────────────┐
                    │   BRIDGE (maestro)   │  Serial → PC (115200)
                    │   NodeMCU v3         │
                    └──────────┬───────────┘
                               │  ESP-NOW (802.11, sin AP)
         ┌──────────┬──────────┼──────────┬──────────┬──────────┬──────────┬──────────┐
         │          │          │          │          │          │          │          │
    ┌────┴───┐ ┌────┴───┐ ┌────┴───┐ ┌────┴───┐ ┌────┴───┐ ┌────┴───┐ ┌────┴───┐ ┌────┴───┐
    │ Nodo 0 │ │ Nodo 1 │ │ Nodo 2 │ │ Nodo 3 │ │ Nodo 4 │ │ Nodo 6 │ │ Nodo 7 │ │ Nodo 8 │
    │ Reed   │ │ PIR    │ │ LDR    │ │ MQ135  │ │ LM35   │ │ DS18B20│ │ GY-906 │ │ GY-61  │
    └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘
```

---

### Compilación y carga

> Se usa **arduino-cli**. Todos los comandos se ejecutan desde la raíz del proyecto
> (donde está `README.md`) o desde la carpeta de cada firmware.

#### 1. Instalar arduino-cli (si no está instalado)

```bash
# Instalar arduino-cli
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Verificar instalación
arduino-cli version
```

#### 2. Instalar el core de ESP8266

```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

#### 3. Instalar las librerías (necesarias para los nodos 6 y 7)

```bash
# Nodo 6 (DS18B20)
arduino-cli lib install OneWire
arduino-cli lib install "DallasTemperature"

# Nodo 7 (GY-906 / MLX90614)
arduino-cli lib install "Adafruit MLX90614 Library"
arduino-cli lib install "Adafruit BusIO"
```

> `ESP8266WiFi.h` y `espnow.h` ya vienen incluidas en el core `esp8266:esp8266`, no
> requieren instalación adicional. `Wire.h` (I2C) también forma parte del core.
> **El nodo 8 (GY-61) no usa librerías externas**: solo `analogRead()` y el core ESP8266.

#### 4. Compilar cada firmware

```bash
# Bridge (maestro)
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_bridge

# Nodos universales (0-4)
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_0
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_1
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_2
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_3
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_4

# Nodo 6 (DS18B20)
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_6

# Nodo 7 (GY-906)
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_7

# Nodo 8 (GY-61 / acelerómetro)
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_8
```

#### 5. Cargar en cada placa

Conecta la placa por USB, identifica el puerto (`arduino-cli board list`) y sube:

```bash
# Ejemplo: /dev/ttyUSB0
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 firmware_bridge
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 firmware_nodo_0
# ... etc. (nodos 1-4, 6, 7 y 8 con su carpeta correspondiente)
```

> 💡 **Tip:** compila y sube un nodo a la vez. **No subas el firmware de un nodo a dos
> placas con el mismo ID**: colisionarían. Cada carpeta ya trae su `NODE_ID` correcto.

#### 6. Abrir el monitor Serial

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

---

### Ejemplos de uso

#### Arranque del Bridge

```
============================================
  ESP8266 MAESTRO - Red ESP-NOW (Bridge)
  v1.1-fix1 - Sensores digital + ADC
============================================

MAC del ESP8266 maestro (bridge): 48:3F:DA:05:86:36

  MAC bridge: 48:3F:DA:05:86:36
  * Slot Nodo 5 reservado para el bridge (self)

[OK] ESP-NOW inicializado correctamente

Iniciando interrogacion de nodos...
Intervalo entre polls: 3000 ms
Timeout de respuesta:  2000 ms
----------------------------------------
```

#### Auto-polling (respuesta de un nodo universal)

```
[ENVIO] Request broadcast para nodo 0
----------------------------------------
RESPUESTA RECIBIDA - Nodo #0
  MAC                 : 84:F3:EB:7C:99:1E
  Estado digital (D2) : 1  (HIGH)
  Valor ADC (A0)      : 512 (0-1023)
  Contador correlativo: 3
  Tiempo activo nodo  : 15230 ms (15 s)
  RTT                 : 12 ms
----------------------------------------
```

#### Respuesta del nodo 6 (DS18B20)

```
[ENVIO] Request -> Nodo 6 [48:3F:DA:05:FC:85]
----------------------------------------
RESPUESTA RECIBIDA - Nodo #6
  MAC                 : 48:3F:DA:05:FC:85
  Temperatura (DS18B20): 24.5 C
  Humedad              : N/A
  Contador correlativo: 7
  Tiempo activo nodo  : 20000 ms (20 s)
  RTT                 : 780 ms
----------------------------------------
```

#### Respuesta del nodo 7 (GY-906 / MLX90614)

```
[ENVIO] Request -> Nodo 7 [E0:98:06:23:BA:E9]
----------------------------------------
RESPUESTA RECIBIDA - Nodo #7
  MAC                 : E0:98:06:23:BA:E9
  Temp. objeto (IR)   : 36.7 C
  Temp. ambiente      : 24.1 C
  Contador correlativo: 4
  Tiempo activo nodo  : 12500 ms (12 s)
  RTT                 : 95 ms
----------------------------------------
```

> `Temp. objeto (IR)` es la temperatura del **objeto/superficie** a la que apunta el
> sensor (sin contacto). `Temp. ambiente` es la temperatura del propio sensor. Si el
> I2C falla, aparecerá `ERROR LECTURA` en ambos campos.

#### Respuesta del nodo 8 (GY-61 / ADXL335) — vibración

```
[ENVIO] Request -> Nodo 8 [48:3F:DA:05:9C:C8]
----------------------------------------
RESPUESTA RECIBIDA - Nodo #8
  MAC                 : 48:3F:DA:05:9C:C8
  Acel. X (A0)        : 512
  Rango vibracion     : 8
  Estado              : REPOSO
  Contador correlativo: 3
  Tiempo activo nodo  : 15400 ms (15 s)
  RTT                 : 15 ms
----------------------------------------
```

Y cuando el sensor vibra (p. ej. golpeando suavemente la maleta):

```
----------------------------------------
RESPUESTA RECIBIDA - Nodo #8
  Acel. X (A0)        : 520
  Rango vibracion     : 132
  Estado              : VIBRACION ALTA
  ...
----------------------------------------
```

> `Acel. X (A0)` es el promedio de las últimas 50 lecturas del eje X. `Rango vibracion`
> es `max − min` de esa ventana. `Estado` es la clasificación: `REPOSO`, `VIBRACION
> BAJA`, `VIBRACION MEDIA` o `VIBRACION ALTA` (ver [Calibración](#calibración) para los
> umbrales).

#### Comando `ping 7`

```
> ping 7
[CMD] Preguntando a Nodo 7...
[ENVIO] Request -> Nodo 7 [E0:98:06:23:BA:E9]
----------------------------------------
RESPUESTA RECIBIDA - Nodo #7
  Temp. objeto (IR)   : 36.7 C
  Temp. ambiente      : 24.1 C
  ...
----------------------------------------
```

#### Comando `list`

```
=== TABLA DE NODOS ===
  Nodo 0: CONOCIDO [84:F3:EB:7C:99:1E]
  Nodo 1: CONOCIDO [80:7D:3A:2B:E3:59]
  Nodo 2: DESCONOCIDO [00:00:00:00:00:00]
  Nodo 3: CONOCIDO [50:02:91:DE:0D:7B]
  Nodo 4: DESCONOCIDO [00:00:00:00:00:00]
  Nodo 5: BRIDGE [48:3F:DA:05:86:36]
  Nodo 6: CONOCIDO [48:3F:DA:05:FC:85]
  Nodo 7: CONOCIDO [E0:98:06:23:BA:E9]
  Nodo 8: CONOCIDO [48:3F:DA:05:9C:C8]
========================
```

#### Comando `stats`

```
=== ESTADISTICAS ===
  Requests enviados:  142
  Respuestas OK:      138
  Timeouts:           4
  Tasa de exito:      97%
  Nodos conocidos:
    Nodo 0: CONOCIDO [84:F3:EB:7C:99:1E]
    ...
====================
```

#### Comando `ping` dirigido y `ping all`

```
> ping 3
[CMD] Preguntando a Nodo 3...
[ENVIO] Request -> Nodo 3 [50:02:91:DE:0D:7B]
----------------------------------------
RESPUESTA RECIBIDA - Nodo #3
  ...
----------------------------------------

> ping all
[CMD] Preguntando a TODOS los nodos...
```

#### Timeout (nodo apagado o fuera de rango)

```
[ENVIO] Request broadcast para nodo 2
[TIMEOUT] Nodo 2 - No se recibio respuesta (2000 ms)
```

---

### Calibración

#### Nodos analógicos (0–1023)

Los nodos universales **no convierten** el ADC a unidades físicas: envían el valor crudo
(0–1023). La conversión se puede hacer en el lado del PC o en una futura versión del
firmware. Fórmulas útiles:

- **LM35** (0.5 V = 0 °C no aplica; salida 10 mV/°C):
  `V = adc * Vref / 1023; tempC = V * 100`. Con Vref = 1.0 V: `tempC = adc * 100 / 1023`.
- **TMP36** (offset 0.5 V): `V = adc * Vref / 1023; tempC = (V - 0.5) * 100`.
- **LDR / MQ135**: valores relativos; calibrar midiendo el ADC con luz/aire conocidos y
  estableciendo umbrales propios. El MQ135 requiere curva logarítmica para ppm.

#### Nodo 6 (DS18B20)

No requiere calibración: el DS18B20 entrega temperatura digital ya calibrada en fábrica.
El firmware la convierte a `temp_x10` (×10) automáticamente.

#### Nodo 7 (GY-906 / MLX90614)

No requiere calibración: el MLX90614 entrega temperatura IR ya calibrada en fábrica. El
firmware convierte a `obj_temp_x10` y `amb_temp_x10` (×10) automáticamente. Para medidas
precisas, apunta el sensor al objeto a **corta distancia** (el campo de visión FOV es
~90°; la zona medida crece con la distancia) y evita reflejos de superficies brillantes.

#### Nodo 8 (GY-61 / ADXL335) — umbrales de vibración

El nodo 8 no mide aceleración en "g" directamente: muestrea el eje X cada 10 ms
(mantiene una **ventana de 50 muestras** ≈ 500 ms) y calcula el **rango** (`max − min`)
de la ventana. Ese rango es el que se compara contra **tres umbrales calibrables**
(`#define` en `firmware_nodo_8.ino`):

| Nivel | `vib_level` | Condición (rango = max−min) | Umbral |
|---|---|---|---|
| REPOSO | 0 | `rango < VIB_TH_REST` | `< 12` |
| VIBRACIÓN BAJA | 1 | `VIB_TH_REST ≤ rango < VIB_TH_LOW` | `12–39` |
| VIBRACIÓN MEDIA | 2 | `VIB_TH_LOW ≤ rango < VIB_TH_MED` | `40–99` |
| VIBRACIÓN ALTA | 3 | `rango ≥ VIB_TH_MED` | `≥ 100` |

Los umbrales por defecto son `VIB_TH_REST=12`, `VIB_TH_LOW=40`, `VIB_TH_MED=100`
(valores de ADC, 0–1023). Para calibrarlos:

1. Con el sensor **en reposo** sobre la mesa, observa el `Rango vibracion` reportado por
   el Bridge (`ping 8`). El ruido eléctrico típico del ADC produce un rango pequeño
   (por eso el umbral de reposo es 12). Si en reposo reporta rangos mayores, sube
   `VIB_TH_REST` por encima del ruido observado.
2. **Golpea suavemente** la maleta para simular vibración baja/media y anota el rango.
3. Ajusta `VIB_TH_LOW` y `VIB_TH_MED` entre los valores observados de cada categoría
   (p. ej. media ≈ 40–100, alta > 100).
4. Recompila y recarga el nodo 8.

> El ADXL335 tiene una sensibilidad nominal de **~300 mV/g** y fondo de escala **±3 g**.
> Con el divisor de la NodeMCU, `accel_x` cambia ~100 cuentas de ADC por cada "g".
> En reposo con el eje X horizontal, `accel_x` ≈ 512 (mitad de escala, 0 g).

#### Referencia de tensión del ADC

El ADC del ESP8266 usa `Vref` nominal de 1.0 V. Las placas NodeMCU v3 suelen llevar un
divisor que mapea 0–3.3 V → 0–1.0 V, de modo que 1023 ≈ 3.3 V. Si tus lecturas analógicas
están saturadas (siempre 1023), revisa el divisor de tu placa o añade un divisor externo.

---

### Advertencias y precauciones

- ⚠️ **Voltajes de GPIO: solo 3.3 V.** Los GPIO del ESP8266 **no toleran 5 V**. No
  conectes salidas de 5 V directamente a pines GPIO; usa divisor resistivo si el sensor
  entrega 5 V.
- ⚠️ **Pines de arranque:** en el ESP8266, GPIO0 (D3), GPIO2 (D4) y GPIO15 (D8) tienen
  estados especiales durante el arranque. **GPIO2/D4 debe estar en HIGH al arrancar**;
  por eso es el pin del LED (con LED apagado = HIGH) y no se debe cargar con sensores
  en los nodos universales. El nodo 6 deja D4 libre a propósito.
- ⚠️ **GPIO1 (TX) y GPIO3 (RX):** los usa el Serial; no los uses para sensores.
- ⚠️ **GPIO2/D4 = LED_BUILTIN en los nodos universales:** si lo usas para otra cosa,
  perderás el heartbeat. Y recuerda que el LED es **activo en LOW** (enciende con `LOW`).
- ⚠️ **ADC (A0):** rango 0–1.0 V nativo. No excedas la tensión de entrada o dañarás el
  pin. Con divisor de la NodeMCU, el máximo práctico es 3.3 V.
- ⚠️ **Alimentación de sensores:** el MQ135 y el PIR requieren **5 V** (usar VIN/5V).
  El DS18B20 y el LDR pueden ir a 3V3. Nunca alimentes el MQ135 desde 3V3.
- ⚠️ **Resistencia pull-up del DS18B20:** imprescindible (4.7 kΩ entre DATA y 3V3). Sin
  ella, el sensor reportará error de lectura (`Temperatura: ERROR LECTURA`).
- ⚠️ **I2C a 3.3 V (nodo 7):** el bus I2C del GY-906 debe operar a **3.3 V**. El módulo
  ya trae regulador y *pull-ups* de 4.7 kΩ a 3.3 V; **no** añadas *pull-ups* a 5 V ni
  conectes SDA/SCL a lógica de 5 V, o dañarás los GPIO del ESP8266.
- ⚠️ **Alimentación del GY-61 a 3.3 V (nodo 8):** el ADXL335 está especificado para
  alimentarse a **3.3 V**. Alimenta el módulo desde el pin **3V3**, no desde 5 V, salvo
  que tu módulo GY-61 documente explícitamente un regulador onboard que acepte 5 V.
- ⚠️ **Solo un ADC (nodo 8):** el ESP8266 tiene un único ADC (A0); por eso se monitoriza
  solo el **eje X** del GY-61. La salida del ADXL335 en reposo es ~1.65 V (la mitad de
  escala); si usas un módulo que entrega la señal desplazada, `accel_x` será distinto de
  512 en reposo (recalibra los umbrales si hace falta).
- ⚠️ **Salida analógica del ADXL335:** la salida X_OUT puede variar de 0 a 3.3 V. No
  excedas el fondo de escala del ADC de la NodeMCU (0–3.3 V con divisor, 0–1 V nativo).
- ⚠️ **Precalentamiento MQ135:** espera unos minutos (idealmente 24–48 h) para lecturas
  estables del calentador.
- ⚠️ **No duplicar IDs:** cada nodo debe tener un `NODE_ID` único (0–4, 6–8). Dos nodos con
  el mismo ID responderían simultáneamente y corromperían la tabla del Bridge.
- ⚠️ **ESP-NOW no cifra:** los mensajes viajan en claro. Adecuado para demos, no para
  datos sensibles.
- ⚠️ **Alcance:** ESP-NOW en ESP8266 alcanza típicamente decenas de metros en interiores
  (depende de obstáculos). Un *timeout* suele significar nodo apagado o fuera de rango.
- ⚠️ **Polaridad del DS18B20:** invertir VCC/GND puede dañar el sensor. Verifica
  rojo=VCC, negro=GND, amarillo=DATA.

---

Licencia MIT © 2026 jmadasme. Las versiones se publican como material de
referencia; los nombres de hardware y marcas pertenecen a sus respectivos dueños.
