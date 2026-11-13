# espNowDemoLoRaWAN1.1 — Puente LoRaWAN ↔ ESP-NOW de sensores

> **Puente bidireccional** entre una red LoRaWAN (AU915, OTAA Clase A) y una red
> local de sensores ESP-NOW. Permite interrogar nodos de campo (GPIO, ADC,
> temperatura DS18B20, infrarrojo GY-906 y acelerómetro GY-61) de forma remota a
> través de un *downlink* LoRaWAN, y recibir la respuesta como *uplink*.

| | |
|---|---|
| **Versión** | 1.1 (evolución de `espNowDemo`) |
| **Firmwares** | `firmware_cubecell/` + `firmware_bridge/` |
| **Región LoRaWAN** | AU915 · sub-banda 2 · OTAA · Clase A |
| **Librería LoRa** | RadioLib 7.7.1 |
| **Gateway** | Milesight UG56 (privado) |

---

## 1. Descripción general

El proyecto consta de **dos firmwares independientes** que cooperan por un enlace
serial TTL:

1. **`firmware_cubecell/`** — corre en un **Heltec CubeCell HTCC-AB01**
   (ASR6502 + radio SX1262). Es el **nodo LoRaWAN**: hace *join* OTAA a la red,
   envía *heartbeats* periódicos para abrir la ventana RX, recibe *downlinks* de
   texto, los reenvía por serial al bridge y sube la respuesta como *uplink*.
2. **`firmware_bridge/`** — corre en un **ESP8266 NodeMCU v3**. Es el **maestro
   ESP-NOW**: interroga a los nodos sensores (IDs 0–4 universales, 6 DS18B20,
   7 GY-906, 8 GY-61) y responde por serial en un formato compacto de texto.

El vínculo entre ambos es un **cable TTL 3.3 V a 9600 baud 8N1** (CubeCell TX →
Bridge D5, CubeCell RX → Bridge D6, masa común).

### Arquitectura de alto nivel

```
                            ┌───────────────────────────────┐
                            │   Red LoRaWAN (Gateway UG56)  │
                            └───────────────┬───────────────┘
                                  radio LoRa (AU915)
                                            │
                    ┌───────────────────────▼───────────────────────┐
                    │   Heltec CubeCell HTCC-AB01                   │
                    │   (nodo LoRaWAN + puente serial)              │
                    └───────────────────────┬───────────────────────┘
                              UART TTL 3.3V · 9600 8N1 (~1 m)
                                            │
                    ┌───────────────────────▼───────────────────────┐
                    │   Bridge ESP8266 NodeMCU v3 (maestro ESP-NOW) │
                    └───────────────────────┬───────────────────────┘
                                     ESP-NOW (2.4 GHz)
                                            │
              ┌──────────┬──────────┬───────┴───────┬──────────┐
              ▼          ▼          ▼               ▼          ▼
        [Nodo 0]   [Nodo 1..4]  [Nodo 6]        [Nodo 7]    [Nodo 8]
        GPIO+ADC   GPIO+ADC     DS18B20         GY-906 IR   GY-61 accel
```

### Flujo de una orden (request–response)

```
1. Gateway envía downlink LoRaWAN de texto  →  ej: "GET 0"
2. CubeCell lo reenvía TAL CUAL al serial   →  "GET 0\n"
3. Bridge interpreta el comando, interroga al nodo ESP-NOW y junta la respuesta
4. Bridge responde por serial               →  "NODE 0 GPIO=1 ADC=512\n"
5. CubeCell sube la respuesta como uplink   →  payload "NODE 0 GPIO=1 ADC=512"
```

---

## 2. Placas objetivo y especificaciones

### Heltec CubeCell HTCC-AB01 (nodo LoRaWAN)

| Parámetro | Valor |
|---|---|
| **MCU** | ASR6502 (SoC ASR6501 + transceptor LoRa SX1262) |
| **CPU** | Arm Cortex-M0+ @ 48 MHz |
| **Flash** | 128 KB |
| **RAM** | 16 KB |
| **Radio LoRa** | Semtech SX1262 (integrado) |
| **Bandas** | 470–510 / 863–928 MHz |
| **Interfaz de desarrollo** | USB-C (UART + cargador) |
| **Conector de antena** | IPEX / pin (requiere antena) |
| **Lógica** | 3.3 V (NO tolera 5 V) |

### Bridge ESP8266 NodeMCU v3 (maestro ESP-NOW)

| Parámetro | Valor |
|---|---|
| **MCU** | ESP8266 (ESP-12E) |
| **CPU** | Tensilica L106 @ 80 MHz |
| **Flash** | 4 MB |
| **RAM** | ~80 KB disponibles |
| **WiFi** | 802.11 b/g/n (se usa sólo para ESP-NOW) |
| **USB-Serial** | CP2102 |
| **Lógica** | 3.3 V (pines NO toleran 5 V) |

---

## 3. Tabla de pines (pinout)

### CubeCell HTCC-AB01 — pines usados

| Pin CubeCell | Nombre GPIO | Función en el proyecto |
|---|---|---|
| `NSS` (CS) | `P4_3` = 35 | Chip-select de la radio SX1262 |
| `DIO1` | `P4_6` = 38 | Interrupción de la radio SX1262 |
| `RST` | `P5_7` = 47 | Reset de la radio SX1262 |
| `BUSY` | `P4_7` = 39 | Busy de la radio SX1262 |
| `TX0` | UART0 TX | Salida serial hacia el bridge (9600 8N1) |
| `RX0` | UART0 RX | Entrada serial desde el bridge (9600 8N1) |
| `GND` | — | Masa común con el bridge |

> Los pines de la radio (`Module mod(35, 38, 47, 39)`) ya vienen cableados
> internamente en la placa HTCC-AB01; la declaración en código es la que le
> indica a RadioLib dónde está cada señal.

### Bridge ESP8266 NodeMCU v3 — pines usados

| Pin NodeMCU | GPIO | Función en el proyecto |
|---|---|---|
| `D5` | GPIO14 | **SoftwareSerial RX** — recibe órdenes de la CubeCell |
| `D6` | GPIO12 | **SoftwareSerial TX** — responde a la CubeCell |
| `D4` | GPIO2 | `LED_BUILTIN` (azul, activo en LOW) — confirmación |
| `D9`/`TX` | GPIO1 | Serial del monitor (115200 baud) |
| `D10`/`RX` | GPIO3 | Serial del monitor (115200 baud) |
| `GND` | — | Masa común con la CubeCell |

> Los pines `D2` (GPIO4) y `A0` quedan **sin uso** en el bridge: sólo los nodos
> sensores leen sus propios sensores.

---

## 4. Diagrama de conexión

### 4.1 Conexión TTL CubeCell ↔ Bridge (OBLIGATORIO — masa común)

```
  ┌───────────────────────────────┐          ┌───────────────────────────────┐
  │  Heltec CubeCell HTCC-AB01    │          │   ESP8266 NodeMCU v3 (bridge) │
  │                               │          │                               │
  │  TX0  ─────────────────────────┼──────────▶  D5 (GPIO14)  SoftwareSerial RX │
  │  RX0  ◀────────────────────────┼──────────┼─  D6 (GPIO12)  SoftwareSerial TX │
  │  GND  ─────────────────────────┼──────────▶  GND  (masa común)            │
  │                               │          │                               │
  │  (USB-C al PC para monitor)   │          │  (micro-USB al PC para monitor)│
  └───────────────────────────────┘          └───────────────────────────────┘
```

Reglas de conexión:

| De CubeCell | A Bridge | Notas |
|---|---|---|
| `TX0` | `D5` (RX) | CubeCell envía comandos (`GET 0`, `LIST`) |
| `RX0` | `D6` (TX) | Bridge responde (`NODE 0 GPIO=…`) |
| `GND` | `GND` | **Masa común obligatoria** (sin ella no hay comunicación) |

> ⚠️ Ambos lados son lógica **3.3 V**. La CubeCell **no tolera 5 V** en RX0;
> no conectes directamente un TX de 5 V de un Arduino al RX0 de la CubeCell.
> La longitud del cable debe ser corta (~1 m máximo) a 9600 baud.

### 4.2 Red ESP-NOW (bridge → nodos sensores)

El bridge interroga por radio ESP-NOW (sin WiFi IP) a los nodos. Cada nodo
sensor es un ESP8266 con la misma struct de 13 bytes. Los IDs son:

| Nodo ID | Tipo | Sensor / respuesta |
|---|---|---|
| 0–4 | Universal | `GPIO=<0|1> ADC=<0-1023>` |
| 5 | *reservado* | Es el propio bridge (self) — no se interroga |
| 6 | DS18B20 | `TEMP=<grados C con 1 decimal>` |
| 7 | GY-906 (IR) | `OBJ=<temp objeto> AMB=<temp ambiente>` |
| 8 | GY-61 (acelerómetro) | `X=<valor> RANGO=<rango> ESTADO=<REPOSO|BAJA|MEDIA|ALTA>` |

El bridge usa **autodescubrimiento de MAC**: si no conoce la MAC de un nodo,
envía un *broadcast* (`FF:FF:FF:FF:FF:FF`) y aprende la MAC de la respuesta,
guardándola en su tabla para futuros envíos directos.

---

## 5. Protocolo serial (texto plano, 9600 8N1)

### 5.1 Comandos (CubeCell → Bridge)

| Comando | Descripción |
|---|---|
| `GET <id>` | Interroga un nodo específico. Responde `NODE <id> …` |
| `GET ALL` | Interroga todos los nodos conocidos. Respuestas `NODE <id> …` y termina con `END` |
| `LIST` | Lista la tabla de nodos (MAC por nodo). Termina con `END` |

### 5.2 Respuestas (Bridge → CubeCell)

| Respuesta | Significado |
|---|---|
| `NODE 0 GPIO=1 ADC=512` | Nodo universal (GPIO + ADC) |
| `NODE 6 TEMP=23.4` | Nodo DS18B20 (temperatura) |
| `NODE 6 TEMP=ERR` | Error de lectura del sensor |
| `NODE 7 OBJ=26.1 AMB=22.3` | Nodo IR (temp objeto + ambiente) |
| `NODE 8 X=512 RANGO=40 ESTADO=BAJA` | Nodo acelerómetro (X, rango, nivel de vibración) |
| `ERROR NODE 4 TIMEOUT` | El nodo no respondió en el tiempo límite |
| `ERROR BAD ID 99` | ID fuera de rango |
| `ERROR UNKNOWN 'FOO'` | Comando no reconocido |
| `BUSY` | El bridge está ocupado con otro request |
| `END` | Fin de una respuesta multi-línea (`GET ALL`, `LIST`) |

---

## 6. Configuración LoRaWAN

### 6.1 Región y parámetros de radio

| Parámetro | Valor |
|---|---|
| **Región** | `AU915` |
| **Sub-banda** | `2` (canales 8–15) |
| **Modo de activación** | OTAA (Over-The-Air Activation) |
| **Clase** | A (RX1/RX2 sólo tras un uplink) |
| **Data rate inicial** | LoRaWAN por defecto (DR0–DR5) |

El gateway **debe** estar configurado para escuchar la **sub-banda 2 de AU915**,
de lo contrario no oirá los uplinks ni podrá programar downlinks. En el gateway
UG56, configura la máscara de canales / sub-banda en `2`.

### 6.2 Registro del dispositivo en el Network Server

Antes de compilar, registra el dispositivo en tu Network Server (TTN /
ChirpStack / Helium / el propio gateway UG56 embebido) con estas claves OTAA:

| Campo | Fuente en código |
|---|---|
| **DevEUI** | `devEui` (en `credentials.h`) |
| **JoinEUI / AppEUI** | `joinEui` (en `credentials.h`) |
| **AppKey** | `appKey` (en `credentials.h`) |
| **NwkKey** | `nwkKey` (en `credentials.h`, igual a AppKey para LoRaWAN 1.0.x) |

> 🔒 **Nunca publiques `credentials.h`.** Está en `.gitignore`. La plantilla es
> `credentials.example.h`. Ver [docs/datasheet.md](docs/datasheet.md) §Credenciales.

### 6.3 Formato de payload y decoder

**Payload de heartbeat (uplink):** un solo byte `0xBB` (187 decimal).

**Payload de respuesta (uplink):** texto ASCII plano, por ejemplo
`NODE 0 GPIO=1 ADC=512`.

**Downlink:** texto ASCII plano, por ejemplo `GET 0`, `GET ALL` o `LIST`.

Como los payloads son **texto plano**, no es estrictamente necesario un decoder
JavaScript en el Network Server: el string aparece directamente en el payload.
No obstante, si el NS exige un decoder o quieres normalizar el campo, puedes usar
un decoder trivial que convierta bytes a texto UTF-8:

```js
// decoder.js — opcional (payloads de texto plano)
function Decode(fPort, bytes) {
  var txt = "";
  for (var i = 0; i < bytes.length; i++) {
    txt += String.fromCharCode(bytes[i]);
  }
  return {
    fPort: fPort,
    text: txt
  };
}
```

**Paso a paso para pegar el decoder en el NS:**

1. Entra a tu aplicación/dispositivo en el Network Server.
2. Ve a **Payload formatters** → **Uplink**.
3. Selecciona **Custom Javascript formatter**.
4. Pega el contenido de `decoder.js` (arriba).
5. Guarda. Los próximos uplinks mostrarán el campo `text` con el string legible.

> Nota: la CubeCell usa el **FPort 1** para todos los uplinks
> (`node.sendReceive(..., 1, ...)`). Los downlinks que programes en el gateway
> también deben dirigirse al FPort 1.

---

## 7. Persistencia de sesión LoRaWAN

La CubeCell **persiste la sesión LoRaWAN en flash** para no re-enrolar (join) en
cada arranque, lo que evita agotar el *DevNonce* y acelera el reinicio.

- **`saveSession()`** — copia `getBufferSession()` (sesión) y
  `getBufferNonces()` (nonces) a la partición `PARAMETER_1` con
  `hal_flash_write()`.
- **`loadSession()`** — lee los buffers de flash con `hal_flash_read()` y los
  restaura con `setBufferNonces()` + `setBufferSession()` (orden importante:
  primero los nonces).
- **`activateOTAA()`** devuelve `NEW_SESSION` (join nuevo) o
  `SESSION_RESTORED` (restaurado sin re-join).
- **`board_init()`** inicializa la tabla de particiones de flash; se llama
  explícitamente en `setup()` porque el *core* del CubeCell no lo hace
  automáticamente.

**Comando `ERASE`:** escribe `ERASE` + Enter en el monitor serial (CuteCom) para
borrar la sesión persistida (`hal_flash_erase`) y reiniciar con un join limpio.
Úsalo si cambias las credenciales o si la sesión quedó corrupta.

---

## 8. Compilación y carga

### Requisitos

- [Arduino CLI](https://arduino.github.io/arduino-cli/) instalado.
- Núcleo **CubeCell** de Heltec instalado.
- Núcleo **ESP8266** instalado.
- Librería **RadioLib 7.7.1** instalada (ver `docs/datasheet.md` §Librerías).

### 8.1 Crear credenciales (CubeCell)

```bash
cd firmware_cubecell
cp credentials.example.h credentials.h
# edita credentials.h con tus DevEUI / JoinEUI / AppKey / NwkKey
```

### 8.2 Compilar y cargar la CubeCell

```bash
# Compilar
arduino-cli compile \
  --fqbn CubeCell:CubeCell:CubeCell-Board \
  firmware_cubecell

# Transferir a la placa (ajusta el puerto)
arduino-cli upload \
  -p /dev/ttyUSB0 \
  --fqbn CubeCell:CubeCell:CubeCell-Board \
  firmware_cubecell
```

### 8.3 Compilar y cargar el bridge ESP8266

```bash
# Compilar
arduino-cli compile \
  --fqbn esp8266:esp8266:nodemcuv2 \
  firmware_bridge

# Transferir a la placa
arduino-cli upload \
  -p /dev/ttyUSB1 \
  --fqbn esp8266:esp8266:nodemcuv2 \
  firmware_bridge
```

> El bridge **no** necesita `credentials.h`; sus librerías (`ESP8266WiFi`,
> `espnow`, `SoftwareSerial`) forman parte del núcleo ESP8266.

### 8.4 Monitores

- **CubeCell:** `arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=9600`
- **Bridge:** `arduino-cli monitor -p /dev/ttyUSB1 -c baudrate=115200`

---

## 9. Ejemplos de uso realistas

### 9.1 Salida del monitor serial de la CubeCell (9600, CuteCom)

```
=== CubeCell LoRaWAN↔Serial Bridge (espNowDemoLoRaWAN1.1) ===
Radio OK
Node OK
scanGuard=200ms, millis sleep
Verificando sesion persistida...
+++ JOINED +++
[PERSIST] Sesion guardada en flash (N + M bytes)
=== Listo. Serial 9600 8N1 ===
Modo manual: escribe HEX + Enter → uplink.
Modo automatico: heartbeat cada 30 s abre RX para downlinks.
>
[HEARTBEAT]
```

### 9.2 Downlink `GET 0` (llega tras un heartbeat, se procesa y se responde)

```
[HEARTBEAT]
[DOWNLINK] 5 bytes: 47 45 54 20 30 → "GET 0"
[BRIDGE] NODE 0 GPIO=1 ADC=512
[UPLINK] respuesta enviada (20 bytes)
```

### 9.3 Uplink manual por HEX (modo de prueba)

```
> DE AD BE EF
>> Enviando: DE AD BE EF
→ OK + downlink (5 bytes)
[DOWNLINK] 47 45 54 20 30 → "GET 0"
>
```

### 9.4 Timeout del bridge (el nodo no respondió)

```
[HEARTBEAT]
[DOWNLINK] 5 bytes: 47 45 54 20 34 → "GET 4"
[BRIDGE] ERROR NODE 4 TIMEOUT
[UPLINK] respuesta enviada (22 bytes)
```

### 9.5 Comando `ERASE`

```
> ERASE
[ERASE] Sesion borrada. Reiniciando...
```

### 9.6 Salida del monitor serial del bridge (115200)

```
============================================
  ESP8266 MAESTRO - Red ESP-NOW (Bridge)
  v1.1-LoRaWAN - Sensores + Serial CubeCell
============================================
  SoftwareSerial (CubeCell): D5=RX D6=TX 9600
MAC del ESP8266 maestro (bridge): 84:F3:EB:...
[OK] ESP-NOW inicializado correctamente
...
----------------------------------------
RESPUESTA RECIBIDA - Nodo #0
  MAC                 : 5E:CF:7F:...
  Estado digital (D2) : 1  (HIGH)
  Valor ADC (A0)      : 512 (0-1023)
  Contador correlativo: 142
  Tiempo activo nodo  : 812345 ms (812 s)
  RTT                 : 12 ms
----------------------------------------
```

---

## 10. Advertencias y precauciones

1. **Antena de la CubeCell:** nunca enciendas/transmitas sin antena conectada;
   puedes dañar el amplificador de RF del SX1262.
2. **Niveles lógicos 3.3 V:** tanto CubeCell como ESP8266 usan lógica de 3.3 V.
   No conectes TX de 5 V al RX0 de la CubeCell.
3. **Masa común:** sin el `GND` compartido entre CubeCell y bridge, el serial TTL
   no funcionará (niveles flotantes / datos corruptos).
4. **DevNonce / JoinNonce:** si cambias las credenciales o ves errores de
   `NO_JOIN_ACCEPT` / `JOIN_NONCE_INVALID`, usa el comando `ERASE` para borrar la
   sesión persistida y re-enrolar limpio. No reutilices una identidad con
   DevNonce ya gastado en un NS distinto.
5. **Sub-banda del gateway:** verifica que el gateway UG56 esté en **AU915
   sub-banda 2**, coincidiendo con `subBand = 2`.
6. **Duty cycle / fair access:** en AU915 hay límites de tiempo de aire; no hagas
   uplinks agresivos. El heartbeat está a 30 s por diseño.
7. **`credentials.h`:** **no lo subas a Git** (está en `.gitignore`). Publica sólo
   la plantilla `credentials.example.h`.
8. **Longitud de cable TTL:** mantén el cable CubeCell↔bridge corto (~1 m) para
   evitar ruido a 9600 baud.

---

## 11. Estructura del repositorio

```
espNowDemoLoRaWAN1.1/
├── firmware_cubecell/
│   ├── firmware_cubecell.ino      # Nodo LoRaWAN (CubeCell)
│   ├── credentials.example.h      # Plantilla de credenciales (público)
│   └── credentials.h              # Credenciales reales (gitignored)
├── firmware_bridge/
│   └── firmware_bridge.ino        # Maestro ESP-NOW + puente serial (ESP8266)
├── docs/
│   ├── datasheet.md               # Documentación de usuario y especificaciones
│   └── developer.md               # Documentación para desarrolladores
├── .gitignore
└── README.md
```

## 12. Documentación adicional

- **[docs/datasheet.md](docs/datasheet.md)** — Datasheet, guía de usuario,
  especificaciones, instalación de librerías y solución de problemas.
- **[docs/developer.md](docs/developer.md)** — Explicación del código, funciones
  clave, diagrama de flujo y guía de extensión.
