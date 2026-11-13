# Datasheet — espNowDemoLoRaWAN1.1

> Documentación de usuario, especificaciones técnicas y guía de configuración y
> solución de problemas del puente **LoRaWAN ↔ ESP-NOW**.

---

## 1. Descripción del producto

El sistema `espNowDemoLoRaWAN1.1` es un **puente de telemetría** que permite
consultar, desde la red LoRaWAN, una red local de sensores ESP-NOW sin
infraestructura WiFi/IP. Su objetivo es llegar a sensores de campo (GPIO/ADC,
temperatura, infrarrojo y vibración) usando la cobertura de un gateway LoRaWAN.

Se compone de:

- **Nodo LoRaWAN**: Heltec CubeCell HTCC-AB01.
- **Puente ESP-NOW**: ESP8266 NodeMCU v3.
- **Enlace local**: UART TTL 3.3 V @ 9600 baud entre ambos.

---

## 2. Especificaciones técnicas

### 2.1 Heltec CubeCell HTCC-AB01

| Parámetro | Valor |
|---|---|
| MCU | ASR6502 (ASR6501 + SX1262) |
| CPU | Arm Cortex-M0+ @ 48 MHz |
| Flash | 128 KB |
| RAM | 16 KB |
| Radio LoRa | Semtech SX1262 |
| Bandas soportadas | 470–510 MHz, 863–928 MHz |
| Potencia TX máx. | ~22 dBm |
| Sensibilidad RX | ~-137 dBm @ SF12 BW125 |
| Consumo deep sleep | ~1.5 µA (modo sleep del MCU) |
| Antena | IPEX / pin (requiere antena externa) |
| USB | USB-C (programación + alimentación) |
| Tensión de trabajo | 3.3 V (lógica no tolerante a 5 V) |

Configuración de radio en este proyecto:

| Parámetro | Valor |
|---|---|
| Región | `AU915` |
| Sub-banda | `2` |
| Activación | OTAA |
| Clase | A |
| Pines SX1262 | NSS=35, DIO1=38, RST=47, BUSY=39 |
| Librería | RadioLib 7.7.1 |

### 2.2 Bridge ESP8266 NodeMCU v3

| Parámetro | Valor |
|---|---|
| MCU | ESP8266 (ESP-12E) |
| CPU | Tensilica L106 @ 80 MHz |
| Flash | 4 MB |
| RAM | ~80 KB |
| WiFi | 802.11 b/g/n (usado sólo en modo ESP-NOW) |
| ESP-NOW | Modo COMBO (TX+RX), canal por defecto |
| SoftwareSerial | D5=RX, D6=TX @ 9600 |
| USB-Serial | CP2102 |

### 2.3 Sensores de la red ESP-NOW (nodos remotos)

| Nodo ID | Sensor | Respuesta compacta |
|---|---|---|
| 0–4 | GPIO digital (D2) + ADC (A0) | `NODE <id> GPIO=<0|1> ADC=<0-1023>` |
| 5 | *(reservado — es el propio bridge)* | — |
| 6 | DS18B20 (temperatura) | `NODE 6 TEMP=<°C, 1 decimal>` |
| 7 | GY-906 MLX90614 (IR) | `NODE 7 OBJ=<°C> AMB=<°C>` |
| 8 | GY-61 ADXL335 (acelerómetro) | `NODE 8 X=<valor> RANGO=<rango> ESTADO=<nivel>` |

---

## 3. Librerías requeridas e instalación

### 3.1 CubeCell (firmware_cubecell)

| Librería | Versión | Tipo |
|---|---|---|
| **RadioLib** | 7.7.1 | Requerida |
| Soporte de placa **CubeCell** (Heltec) | última | Núcleo de placa |

Instalación con Arduino CLI:

```bash
# 1) Núcleo CubeCell (Heltec)
arduino-cli config add board_manager.additional_urls \
  https://resource.heltec.cn/download/package_CubeCell_index.json
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell

# 2) Librería RadioLib (7.7.1)
arduino-cli lib install "RadioLib@7.7.1"

# 3) Verificar
arduino-cli core list
arduino-cli lib list | grep -i radiolib
```

> La persistencia de sesión usa la HAL interna del CubeCell (`hal/soc/flash.h`,
> `board_init()`, `hal_flash_read/write`). No requiere librería externa; forma
> parte del núcleo Heltec CubeCell.

### 3.2 Bridge (firmware_bridge)

| Librería | Tipo |
|---|---|
| `ESP8266WiFi` | Núcleo ESP8266 (incluida) |
| `espnow` | Núcleo ESP8266 (incluida) |
| `SoftwareSerial` | Núcleo ESP8266 (incluida) |

```bash
arduino-cli config add board_manager.additional_urls \
  https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

No se necesitan librerías externas para el bridge.

---

## 4. Guía de configuración del Network Server

### 4.1 Registrar el dispositivo (OTAA)

Independientemente del NS (TTN, ChirpStack, Helium o el gateway UG56 embebido),
registra un **end-device** con:

| Campo | Valor (fuente: `credentials.h`) |
|---|---|
| JoinEUI / AppEUI | `joinEui` |
| DevEUI | `devEui` |
| AppKey | `appKey` (16 bytes) |
| NwkKey | `nwkKey` (igual a AppKey en LoRaWAN 1.0.x) |
| Clase | A |
| Región | AU915 |
| Sub-banda / canales | 2 |

> 🔒 Usa la **plantilla** `credentials.example.h` y rellénala con los valores que
> te dé el NS. **Nunca** pongas las claves reales en el repositorio público.

### 4.2 Configurar el gateway UG56

1. Asegura que el gateway está en **AU915**.
2. Configura la **sub-banda 2** (canales 8–15) para que coincida con
   `subBand = 2` del firmware.
3. Verifica que el gateway tenga conectividad al NS (TTN/ChirpStack/embebido) y
   que esté en línea.
4. Para enviar un downlink de prueba, programa un downlink de texto al **FPort 1**
   con el contenido `GET 0` (sin comillas, sin salto de línea).

### 4.3 Payload formatter (decoder) en el NS

Los payloads son texto plano, así que el decoder es trivial. Pégalo en
**Payload formatters → Uplink → Custom Javascript**:

```js
function Decode(fPort, bytes) {
  var txt = "";
  for (var i = 0; i < bytes.length; i++) {
    txt += String.fromCharCode(bytes[i]);
  }
  return { fPort: fPort, text: txt };
}
```

Esto hará que el campo `text` muestre, por ejemplo, `NODE 0 GPIO=1 ADC=512` o el
heartbeat `\xBB`.

---

## 5. Guía de solución de problemas

### 5.1 La CubeCell no hace join (NO_JOIN_ACCEPT / -1116)

- **Síntoma:** `Join FAILED: -1116 (NO_JOIN_ACCEPT)`.
- **Causas comunes:**
  1. Credenciales incorrectas en `credentials.h` (DevEUI/JoinEUI/AppKey no
     coinciden con las registradas en el NS).
  2. Gateway fuera de línea o en otra sub-banda.
  3. Sub-banda incorrecta (debe ser `2`).
  4. Sin antena conectada (señal demasiado débil).
- **Solución:**
  1. Verifica las credenciales con el NS.
  2. Confirma sub-banda 2 en el gateway.
  3. Conecta la antena.
  4. Prueba con el comando `ERASE` para borrar la sesión y re-enrolar limpio.

### 5.2 JOIN_NONCE_INVALID / DevNonce agotado

- **Síntoma:** el NS rechaza el join con `JOIN_NONCE_INVALID`.
- **Causa:** la misma identidad (DevEUI) se usó previamente y el DevNonce ya fue
  gastado (normalmente tras re-enrolar contra otro NS o tras un reinicio de la
  sesión en el NS).
- **Solución:**
  1. Genera una **nueva identidad** (nuevo DevEUI/AppKey) y regístrala en el NS.
  2. En la CubeCell usa `ERASE` para borrar la sesión persistida.
  3. No reutilices una identidad entre NS distintos.

### 5.3 No hay uplinks en el NS

- **Síntoma:** la CubeCell dice `[HEARTBEAT]` pero el NS no recibe nada.
- **Causas:**
  1. Gateway en sub-banda distinta a la del dispositivo.
  2. FPort mal interpretado (todo va por FPort 1).
  3. El dispositivo no hizo join (no hay `+++ JOINED +++`).
- **Solución:**
  1. Revisa el log del gateway (¿recibe el join request?).
  2. Asegúrate de ver `+++ JOINED +++` o `+++ SESION RESTAURADA +++`.
  3. Verifica sub-banda 2 y cobertura.

### 5.4 Downlink no llega a la CubeCell

- **Síntoma:** envías un downlink `GET 0` y la CubeCell no imprime `[DOWNLINK]`.
- **Causas:**
  1. Clase A: el downlink sólo se entrega **después de un uplink** (en RX1/RX2).
     Si no hay heartbeat, no hay ventana RX.
  2. El downlink se programó en un FPort distinto de 1.
  3. La ventana RX ya expiró (RX1 ~1 s tras el uplink).
- **Solución:**
  1. Envía el downlink **inmediatamente después** de un heartbeat.
  2. Usa FPort 1.
  3. Si el NS permite "downlink confirmed", no lo uses (es Clase A no confirmada
     en este firmware).

### 5.5 El bridge responde `ERROR NODE <id> TIMEOUT`

- **Síntoma:** la CubeCell recibe `ERROR NODE 4 TIMEOUT`.
- **Causas:**
  1. El nodo sensor está apagado o fuera de alcance.
  2. La MAC del nodo no se aprendió correctamente (autodescubrimiento).
  3. `RESPONSE_TIMEOUT_MS` (2000 ms) insuficiente para un sensor lento
     (p. ej. DS18B20, ~750 ms).
- **Solución:**
  1. Enciende el nodo y verifica con el comando humano `ping <id>` (115200).
  2. Revisa la tabla con `list`.
  3. Si usas sensores más lentos, sube `RESPONSE_TIMEOUT_MS` en el bridge.

### 5.6 El serial CubeCell↔bridge no funciona

- **Síntoma:** la CubeCell envía `GET 0` y no llega respuesta.
- **Causas:**
  1. Falta la **masa común** entre ambas placas.
  2. Cableado TX/RX cruzado.
  3. Velocidad distinta (debe ser 9600 en SoftwareSerial del bridge).
  4. Cable demasiado largo / ruido.
- **Solución:**
  1. Verifica GND↔GND.
  2. Confirma CubeCell TX → D5, CubeCell RX → D6.
  3. Confirma 9600 8N1 en ambos lados.
  4. Acorta el cable.

### 5.7 Reinicio repetido tras `esp_now_init()` (bridge)

- **Síntoma:** el bridge imprime `[FATAL] Error al inicializar ESP-NOW`.
- **Causa:** rol de WiFi mal configurado o flash corrupta del ESP8266.
- **Solución:** regraba el firmware, verifica que `WiFi.mode(WIFI_STA)` se
  ejecute antes de `esp_now_init()`. Si persiste, borra la flash del ESP8266.

---

## 6. Nota sobre credenciales

- **`credentials.example.h`** — plantilla **pública** con valores en cero.
- **`credentials.h`** — archivo **real** con las claves. Está en `.gitignore` y
  **no debe subirse** a ningún repositorio.

Para generar tu archivo de credenciales:

```bash
cd firmware_cubecell
cp credentials.example.h credentials.h
# edita credentials.h con tus valores OTAA
```

Formato esperado (RadioLib usa endianidad LSB-first en los `uint64_t`):

```c
uint64_t joinEui = 0x...;   // JoinEUI/AppEUI
uint64_t devEui  = 0x...;   // DevEUI
uint8_t  appKey[] = { ... }; // 16 bytes
uint8_t  nwkKey[] = { ... }; // 16 bytes (igual a AppKey en LoRaWAN 1.0.x)
```

---

## 7. Tabla de compatibilidad con otras placas LoRaWAN

| Placa | MCU / radio | ¿Compatible? | Notas |
|---|---|---|---|
| Heltec CubeCell HTCC-AB01 | ASR6502 + SX1262 | ✅ Referencia | Firmware actual |
| Heltec CubeCell Board (V2) | ASR6502 + SX1262 | ✅ Directo | Mismo núcleo; verificar pines de radio |
| Heltec WiFi LoRa 32 (V2/V3) | ESP32 + SX1262/1276 | ⚠️ Adaptar | Cambiar pines de `Module`, usar núcleo ESP32 |
| LilyGO T-Beam | ESP32 + SX1276/1262 | ⚠️ Adaptar | Cambiar pines y `LoRaWANBand_t` si aplica |
| TTGO LoRa32 | ESP32 + SX1276 | ⚠️ Adaptar | SX1276 (no SX1262): `SX1276 radio(&mod)` |
| Arduino MKR WAN 1300 | SAMD21 + SX1276 | ⚠️ Adaptar | Usar `SX1276` y reescribir persistencia (no HAL CubeCell) |

> El código de **persistencia de sesión** (`hal_flash_*`, `board_init()`) es
> específico del núcleo Heltec CubeCell. Si portas a otra placa, sustitúyelo por
> el mecanismo de almacenamiento correspondiente (EEPROM/preferences/SPIFFS).

---

## 8. Esquema de conexión rápida (referencia)

```
CubeCell TX0 ──────────▶ Bridge D5 (GPIO14, SoftwareSerial RX)
CubeCell RX0 ◀────────── Bridge D6 (GPIO12, SoftwareSerial TX)
CubeCell GND ────────── Bridge GND   (¡masa común obligatoria!)
```

Ambos a 3.3 V lógicos. Cable corto (< 1 m).
