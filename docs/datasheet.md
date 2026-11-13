# Datasheet — espNowDemo (Red de Sensores ESP-NOW)

> Documentación de usuario y especificaciones técnicas del proyecto **espNowDemo**
> (netEspNow v1.1-fix1). Complementa al `README.md` con detalles de usuario.

---

## 1. Documentación de usuario detallada

### 1.1 ¿Qué es espNowDemo?

`espNowDemo` es una red inalámbrica de sensores basada en **ESP-NOW** (protocolo de
comunicación directa entre ESP8266 sin necesidad de router ni punto de acceso). Está
pensada para una **maleta de demostración**: un nodo central (Bridge) recopila datos de
varios nodos sensores y los muestra por el puerto serie, donde un operador puede
interrogarlos manualmente con comandos.

### 1.2 Roles en la red

- **Bridge (maestro)** — ID 5. Inicia las comunicaciones, mantiene la tabla de nodos,
  gestiona el autodescubrimiento de MAC y muestra los datos por Serial.
- **Nodo universal (esclavo)** — IDs 0 a 4. Lee una entrada digital y una analógica
  cuando se le solicita y responde con los valores crudos.
- **Nodo DS18B20 (esclavo)** — ID 6. Lee un sensor de temperatura digital y responde
  con la temperatura en °C (×10) y humedad N/A.
- **Nodo GY-906 (esclavo)** — ID 7. Lee un termómetro infrarrojo sin contacto
  (MLX90614) por I2C y responde con la temperatura del objeto y la ambiente en °C (×10).
- **Nodo GY-61 (esclavo)** — ID 8. Muestrea continuamente el eje X de un acelerómetro
  ADXL335 y **clasifica la vibración** en tiempo real (REPOSO / BAJA / MEDIA / ALTA),
  respondiendo con el valor X, el rango de vibración y la categoría.

### 1.3 Ciclo de operación normal

1. Al encender, el Bridge imprime su banner y su MAC, inicializa ESP-NOW y entra en
   **auto-polling** (interrogación automática cada 3 s).
2. Para cada nodo, el Bridge envía un request (directo o por broadcast) y espera
   respuesta durante 2000 ms.
3. La respuesta se muestra formateada por Serial.
4. El operador puede pausar el auto-poll (`auto off`) y usar `ping`/`list`/`stats`.

### 1.4 Comandos disponibles

| Comando | Efecto |
|---|---|
| `help` / `?` | Imprime la ayuda. |
| `ping 0`…`ping 8` | Interroga una vez al nodo indicado. |
| `ping all` | Reanuda el barrido desde el nodo 0. |
| `list` | Tabla de nodos con MAC y estado. |
| `stats` | Contadores de requests/respuestas/timeouts y tasa de éxito. |
| `auto on` / `auto off` | Activa/desactiva el polling automático. |

### 1.5 Interpretación de salidas

- **`Estado digital (D2)`**: `0` = LOW, `1` = HIGH. Según el sensor:
  - Reed: `1` = puerta abierta (contacto abierto), `0` = cerrada.
  - PIR: `1` = movimiento detectado, `0` = reposo.
- **`Valor ADC (A0)`**: 0–1023. Proporcional a la tensión leída (ver calibración).
- **`Temperatura (DS18B20)`**: valor en °C con 1 decimal (ej. `24.5 C`).
- **`Humedad`**: siempre `N/A` en esta red (el DS18B20 no mide humedad).
- **`Temp. objeto (IR)`**: temperatura °C del objeto apuntado por el GY-906 (nodo 7).
- **`Temp. ambiente`**: temperatura °C ambiente medida por el propio MLX90614 (nodo 7).
- **`Acel. X (A0)`**: promedio de las últimas 50 lecturas del eje X (0–1023) del
  GY-61 (nodo 8). En reposo con el eje X horizontal ronda 512 (mitad de escala).
- **`Rango vibracion`**: diferencia `max − min` de la ventana de 50 muestras (0–1023).
  Un rango pequeño indica reposo; uno grande indica vibración.
- **`Estado`**: clasificación de vibración del nodo 8 → `REPOSO` (0), `VIBRACION BAJA`
  (1), `VIBRACION MEDIA` (2) o `VIBRACION ALTA` (3).
- **`RTT`**: tiempo de ida y vuelta (ms). Incluye la lectura de sensores.

---

## 2. Especificaciones técnicas

### 2.1 Placa base (todas las tarjetas)

| Parámetro | Valor |
|---|---|
| Placa | NodeMCU v3 (ESP-12E, ESP8266EX) |
| Conversor USB | CP2102 |
| FQBN | `esp8266:esp8266:nodemcuv2` |
| CPU | Tensilica L106 32-bit @ 80 MHz (160 MHz máx.) |
| RAM de usuario | ~80 KB |
| Flash | 4 MB |
| WiFi | 802.11 b/g/n, 2.4 GHz (modo Station para ESP-NOW) |
| Tensión de lógica | 3.3 V |
| Tensión de alimentación | 5 V (USB) o VIN 5–9 V |
| ADC | 1 canal A0, 10 bits (0–1023), 0–1.0 V nativo |
| GPIO digitales | D0–D8 (algunos con restricciones de arranque) |
| UART | UART0 (TX=GPIO1, RX=GPIO3), 115200 baud |
| Consumo típico | ~70–80 mA activo, ~20 µA en deep-sleep (no usado) |

### 2.2 Protocolo ESP-NOW

| Parámetro | Valor |
|---|---|
| Rol Bridge | `ESP_NOW_ROLE_COMBO` |
| Rol Nodo | `ESP_NOW_ROLE_COMBO` |
| Cifrado | Ninguno |
| Canal WiFi | 1 (por defecto del peer) |
| Tamaño mensaje universal | 13 bytes (`__attribute__((packed))`) |
| Tamaño mensaje sensor (DS18B20) | 14 bytes (`__attribute__((packed))`) |
| Tamaño mensaje IR (GY-906) | 14 bytes (`__attribute__((packed))`) |
| Tamaño mensaje aceleración (GY-61) | 16 bytes (`__attribute__((packed))`) |
| Timeout de respuesta | 2000 ms |
| Intervalo de polling | 3000 ms |
| Autodescubrimiento | Broadcast `FF:FF:FF:FF:FF:FF` |

### 2.3 Estructuras de mensaje

**Universal (13 B):**

| Campo | Tipo | Bytes | Descripción |
|---|---|---|---|
| msg_type | uint8_t | 1 | 0=request, 1=response |
| node_id | uint8_t | 1 | ID del nodo |
| gpio_state | uint8_t | 1 | Estado digital 0/1 |
| adc_value | uint16_t | 2 | ADC 0–1023 |
| counter | uint32_t | 4 | Contador correlativo |
| uptime_ms | uint32_t | 4 | Tiempo activo (ms) |

**Sensor (14 B):**

| Campo | Tipo | Bytes | Descripción |
|---|---|---|---|
| msg_type | uint8_t | 1 | 2=sensor response |
| node_id | uint8_t | 1 | 6 |
| temp_x10 | int16_t | 2 | °C × 10 |
| hum_x10 | int16_t | 2 | % × 10 (N/A = -9999) |
| counter | uint32_t | 4 | Contador correlativo |
| uptime_ms | uint32_t | 4 | Tiempo activo (ms) |

**IR GY-906 (14 B) — nodo 7:**

| Campo | Tipo | Bytes | Descripción |
|---|---|---|---|
| msg_type | uint8_t | 1 | 3=IR response |
| node_id | uint8_t | 1 | 7 |
| obj_temp_x10 | int16_t | 2 | Temp. objeto °C × 10 |
| amb_temp_x10 | int16_t | 2 | Temp. ambiente °C × 10 |
| counter | uint32_t | 4 | Contador correlativo |
| uptime_ms | uint32_t | 4 | Tiempo activo (ms) |

**Aceleración GY-61 (16 B) — nodo 8:**

| Campo | Tipo | Bytes | Descripción |
|---|---|---|---|
| msg_type | uint8_t | 1 | 4=aceleración response |
| node_id | uint8_t | 1 | 8 |
| accel_x | uint16_t | 2 | Valor X actual (promedio ventana, 0–1023) |
| vib_range | uint16_t | 2 | Rango de vibración (max − min, 0–1023) |
| vib_level | uint8_t | 1 | 0=reposo, 1=baja, 2=media, 3=alta |
| reserved | uint8_t | 1 | Reservado (0) |
| counter | uint32_t | 4 | Contador correlativo |
| uptime_ms | uint32_t | 4 | Tiempo activo (ms) |

### 2.4 Sensores utilizados

| Sensor | Tipo | Salida | Alimentación | Nodo |
|---|---|---|---|---|
| MC-38 (reed) | Digital, contacto | Abierto/cerrado | — | 0 |
| HC-SR501 (PIR) | Digital | 3.3 V en detección | 5–20 V | 1 |
| LDR | Analógica (resistiva) | 0–3.3 V (divisor) | — | 2 |
| MQ135 | Analógica | 0–5 V (AO) | 5 V | 3 |
| LM35 / TMP36 | Analógica | 10 mV/°C | 4–20 V / 2.7–5.5 V | 4 |
| DS18B20 | Digital OneWire | °C digital | 3.0–5.5 V | 6 |
| GY-906 (MLX90614) | I2C (0x5A) | 2× temp °C (objeto + ambiente) | 3.3–5 V (reg. interno) | 7 |
| GY-61 (ADXL335) | Analógica (3 ejes) | 0–3.3 V por eje (X/Y/Z) | 3.3 V | 8 |

### 2.5 Especificaciones del GY-906 / MLX90614 (nodo 7)

| Parámetro | Valor |
|---|---|
| Sensor | Melexis MLX90614 (termómetro IR sin contacto) |
| Interfaz | I2C, dirección **0x5A** (por defecto) |
| Rango temp. objeto | **-70 °C a +380 °C** |
| Rango temp. ambiente | **-40 °C a +125 °C** |
| Precisión (objeto) | ±0.5 °C (típ. 0–50 °C) |
| Resolución | 0.02 °C |
| Campo de visión (FOV) | ~90° |
| Alimentación | 3.3 V o 5 V (el módulo GY-906 regula a 3.3 V) |
| Pull-ups I2C | 4.7 kΩ integrados en el módulo |
| Pines NodeMCU | SDA = D2 (GPIO4), SCL = D1 (GPIO5) |

### 2.6 Especificaciones del GY-61 / ADXL335 (nodo 8)

| Parámetro | Valor |
|---|---|
| Sensor | Analog Devices **ADXL335** (acelerómetro MEMS de 3 ejes) |
| Módulo | GY-61 (placa breakout con regulador y salidas X_OUT/Y_OUT/Z_OUT) |
| Interfaz | **Analógica** — una salida por eje (0–3.3 V) |
| Fondo de escala | **±3 g** por eje |
| Sensibilidad | ~**300 mV/g** (nominal) |
| Rango de salida | 0 a VDD (en reposo ≈ VDD/2 = ~1.65 V) |
| Alimentación | **3.3 V** (algunos GY-61 traen regulador y aceptan 5 V) |
| Consumo | ~350 µA (típico) |
| Ejes conectados | Solo **X** (`X_OUT` → A0); Y y Z sin conectar (ESP8266 tiene 1 ADC) |
| Pin NodeMCU | A0 (analógico) |
| Ruido (X/Y) | ~0.75 mg/√Hz |

> **Por qué solo el eje X:** el ESP8266 dispone de **un único ADC** (A0). Para una
> clasificación de vibración basta un eje; si se desea medir otro, se cablea `Y_OUT` o
> `Z_OUT` a A0 (el código no cambia).

#### Algoritmo de clasificación de vibración

El nodo 8 **no** usa librerías externas; todo el procesamiento se hace por software:

1. **Muestreo continuo:** lee `analogRead(A0)` cada **10 ms** (≈100 Hz).
2. **Ventana deslizante:** mantiene un buffer circular con las últimas **50 muestras**
   (~500 ms de historial).
3. **Cálculo del rango:** tras cada muestra calcula `promedio` (Σ/n) y
   `rango = max − min` de la ventana.
4. **Categorización:** compara el rango contra tres umbrales:

| Nivel | `vib_level` | Rango (max−min) |
|---|---|---|
| REPOSO | 0 | `< 12` (`VIB_TH_REST`) |
| VIBRACIÓN BAJA | 1 | `12 … 39` |
| VIBRACIÓN MEDIA | 2 | `40 … 99` |
| VIBRACIÓN ALTA | 3 | `≥ 100` (`VIB_TH_MED`) |

Los umbrales son constantes calibrables en el firmware: `VIB_TH_REST=12`,
`VIB_TH_LOW=40`, `VIB_TH_MED=100` (valores ADC 0–1023).

### 2.7 Rendimiento

- **Latencia nodo universal:** ~10–15 ms (RTT típico).
- **Latencia nodo 6 (DS18B20):** ~750–800 ms (lectura a 12 bits bloquea ~750 ms).
- **Latencia nodo 7 (GY-906):** ~50–150 ms (I2C; puede subir si hay reintentos por bus
  flaky).
- **Latencia nodo 8 (GY-61):** ~10–20 ms (responde al instante; la clasificación ya está
  calculada en segundo plano, no bloquea).
- **Ciclo completo de 8 nodos:** ~4–5 s (el nodo 6 domina la latencia).
- **Tasa de éxito esperada:** >95 % en condiciones normales (interior).

---

## 3. Librerías requeridas

| Librería | Versión | Necesaria para | Instalación |
|---|---|---|---|
| ESP8266WiFi | (core) | Todos | Incluida en el core esp8266 |
| espnow | (core) | Todos | Incluida en el core esp8266 |
| OneWire | 2.3.8 | Nodo 6 | Library Manager / arduino-cli |
| DallasTemperature | 4.0.6 | Nodo 6 | Library Manager / arduino-cli |
| Adafruit MLX90614 | 2.1.6 | Nodo 7 | Library Manager / arduino-cli |
| Adafruit BusIO | (dependencia) | Nodo 7 | Library Manager / arduino-cli |

> **El nodo 8 (GY-61 / ADXL335) no requiere librerías externas.** Solo usa `analogRead()`
> y las librerías del core (`ESP8266WiFi.h`, `espnow.h`).

### 3.1 Instalación con arduino-cli

```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266

# Nodo 6
arduino-cli lib install OneWire
arduino-cli lib install "DallasTemperature"

# Nodo 7 (GY-906)
arduino-cli lib install "Adafruit MLX90614 Library"
arduino-cli lib install "Adafruit BusIO"
```

### 3.2 Instalación manual (Arduino IDE)

1. **Core esp8266:** Archivo → Preferencias → "Gestor de URLs de tarjetas adicionales"
   → `https://arduino.esp8266.com/stable/package_esp8266com_index.json`.
   Luego Herramientas → Placa → Gestor de tarjetas → buscar `esp8266` → instalar.
2. **OneWire / DallasTemperature:** Herramientas → Gestor de librerías → buscar e instalar.
3. **Adafruit MLX90614:** Herramientas → Gestor de librerías → buscar
   "Adafruit MLX90614 Library" → instalar (arrastra **Adafruit BusIO** como dependencia).

### 3.3 Versiones verificadas

- OneWire **2.3.8** (por Paul Stoffregen)
- DallasTemperature **4.0.6** (por Miles Burton)
- Adafruit MLX90614 **2.1.6** (por Adafruit) — depende de Adafruit BusIO.

---

## 4. Guía de solución de problemas comunes

| Síntoma | Causa probable | Solución |
|---|---|---|
| Bridge no imprime nada | Baud incorrecto / cable mal | Monitor a 115200; revisar TX/RX (GPIO1/GPIO3). |
| `[FATAL] Error al inicializar ESP-NOW` | Core esp8266 incorrecto o WiFi mal configurado | Reinstalar core; verificar `WiFi.mode(WIFI_STA)`. |
| `[TIMEOUT] Nodo X - No se recibio respuesta` | Nodo apagado, fuera de rango o ID equivocado | Encender nodo; verificar `NODE_ID`; acercar tarjetas. |
| Nodo 6 reporta `Temperatura: ERROR LECTURA` | Pull-up 4.7 kΩ ausente / polaridad invertida | Añadir pull-up entre DATA y 3V3; revisar rojo/negro/amarillo. |
| Nodo 6 reporta `DS18B20 sensors found: 0` | Sensor mal conectado o dañado | Revisar cableado; probar otro sensor. |
| Nodo 7 imprime `[MLX90614] ERROR: no se encontro el sensor en I2C` | I2C mal cableado o dirección distinta | Verificar SDA→D2, SCL→D1, VIN→3V3, GND→GND; probar escaneo I2C. |
| Nodo 7 reporta `ERROR LECTURA` (obj/amb) | Lecturas I2C fallidas tras 3 reintentos / cables flojos | Revisar cables SCL/SDA; asegurar alimentación estable; acercar/apretar conexiones. |
| Nodo 7 con lecturas erráticas | SDA/SCL flojos, *pull-ups* duplicados o bus con interferencias | Reapretar cables; no añadir pull-ups externos (el módulo ya los trae); acortar cableado. |
| Nodo 8 siempre `REPOSO` aunque vibre | `X_OUT` mal conectado a A0, o umbrales demasiado altos | Verificar X_OUT→A0 y VCC→3V3; recalibrar `VIB_TH_*` (ver README → Calibración). |
| Nodo 8 `Acel. X` fijo en 0 o 1023 | Salida saturada (X_OUT fuera de rango del ADC) | Revisar alimentación a 3.3 V; verificar que X_OUT en reposo ronde 512. |
| Nodo 8 `Rango vibracion` muy alto en reposo | Ruido eléctrico / fuente inestable / X_OUT flotante | Conectar X_OUT firmemente; alimentar desde 3V3 estable; subir `VIB_TH_REST` si hace falta. |
| Nodo 8 siempre `VIBRACION ALTA` | Umbrales demasiado bajos o sensor con ruido | Subir `VIB_TH_LOW`/`VIB_TH_MED`; revisar cableado y alimentación. |
| `list` muestra `DESCONOCIDO` siempre | Nodo no responde al broadcast | Verificar que el nodo esté encendido y con el ID correcto. |
| ADC siempre `1023` | Tensión mayor que el fondo de escala / divisor | Revisar divisor; bajar tensión de entrada a ≤3.3 V (≤1 V nativo). |
| LED no parpadea (nodos 0–4) | GPIO2/D4 ocupado o LED activo en HIGH erróneo | Recordar: LED enciende con LOW. Verificar `pinMode`. |
| Dos nodos responden igual | IDs duplicados | Asignar `NODE_ID` único a cada placa. |
| PIR siempre en HIGH | Sensibilidad/retardo mal ajustados | Ajustar potenciómetros; esperar ~60 s de estabilización inicial. |
| MQ135 lecturas erráticas | Precalentamiento insuficiente | Esperar varios minutos (idealmente 24 h). |

### 4.1 Pasos de diagnóstico rápido

1. **Aislar el problema:** conectar un solo nodo + el Bridge, ver si responde.
2. **Verificar serial del nodo:** cada nodo imprime su banner y MAC al arrancar
   (conecta el USB del nodo a un PC).
3. **Verificar `NODE_ID`:** el banner debe mostrar el ID correcto.
4. **Comprobar MAC aprendida:** en el Bridge, `list` debe pasar de DESCONOCIDO a CONOCIDO.

---

## 5. Compatibilidad con otras placas

| Placa | Compatible | Notas |
|---|---|---|
| NodeMCU v3 (ESP-12E) | ✅ Sí (objetivo) | FQBN `nodemcuv2` |
| NodeMCU v1.0 (ESP-12E) | ✅ Sí | Mismo FQBN; mismo pinout D0–D8. |
| Wemos D1 mini / D1 mini Pro | ✅ Sí | FQBN `d1_mini`; GPIO y `LED_BUILTIN` = GPIO2 (activo LOW). |
| Wemos D1 mini Pro | ✅ Sí | Igual que D1 mini; ojo con pin D4/GPIO2. |
| ESP-01 / ESP-01S | ⚠️ Parcial | Solo 2 GPIO útiles (GPIO0/GPIO2); sin ADC accesible. No apto para nodos analógicos. |
| ESP-12F (módulo suelto) | ✅ Sí | Requiere circuito de programación externo. |
| ESP32 (cualquier) | ❌ No directo | `espnow.h` y API difieren (usar `esp_now_*` de ESP-IDF). Requiere adaptación. |
| Arduino Uno/Nano/Mega | ❌ No | Sin WiFi nativo; ESP-NOW no disponible. |
| ESP8266 con core < 3.0 | ⚠️ | `esp_now_set_self_role()` es de la API antigua; verificar versión del core. |

> ⚠️ **Nota sobre el core:** este proyecto usa la API antigua de ESP-NOW
> (`esp_now_init`, `esp_now_set_self_role`, `esp_now_add_peer`, `esp_now_send`,
> `esp_now_register_recv_cb`, `esp_now_register_send_cb`). Es compatible con el core
> `esp8266` clásico (2.x). En cores muy nuevos, algunas funciones quedan en desuso pero
> siguen funcionando con avisos.

### 5.1 Adaptación a Wemos D1 mini

- Cambiar FQBN a `esp8266:esp8266:d1_mini`.
- `LED_BUILTIN` sigue siendo GPIO2 (activo LOW). El pin `D2` en Wemos D1 mini = GPIO4,
  igual que NodeMCU, por lo que **el código funciona sin cambios de pines**.
- El divisor del ADC de la D1 mini es 220 kΩ / 100 kΩ (mismo rango 0–3.3 V).
- **Nodo 7 (I2C):** los pines I2C por defecto de la D1 mini son también SDA=D2 (GPIO4) y
  SCL=D1 (GPIO5), por lo que el GY-906 se cablea igual (`Wire.begin()` sin argumentos).
- **Nodo 8 (analógico):** funciona idéntico en D1 mini; `A0` es el mismo ADC único.
  Cablear `X_OUT` → A0 y alimentar el GY-61 a 3V3. Sin cambios de código.

---

## 6. Alimentación y consumos

- **Bridge:** alimentar por USB (5 V). Consumo ~80 mA.
- **Nodos universales:** por USB o por VIN (5–9 V). Los sensores de 5 V (PIR, MQ135)
  deben tomar 5 V del pin VIN/5V cuando el nodo se alimenta por USB.
- **Nodo 6:** el DS18B20 consume poco; puede alimentarse a 3.3 V.
- **Nodo 7:** el GY-906 consume ~1–2 mA (I2C); alimentar a 3.3 V o 5 V (el módulo regula).
- **Nodo 8:** el GY-61/ADXL335 consume ~350 µA; alimentar **solo a 3.3 V** (desde el pin
  `3V3` de la NodeMCU). Si el módulo GY-61 documenta un regulador onboard, puede
  aceptarse 5 V en `VIN`, pero la opción segura es 3.3 V.
- Para la **maleta**, se recomienda una fuente USB de 2 A compartida o una batería
  *power bank* con varios puertos, con tierras comunes si se usan fuentes distintas.
