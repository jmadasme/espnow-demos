# GUI de Válvulas Zigbee — Panel de control MQTT

Aplicación de escritorio en Python (PyQt6) para operar las **válvulas de agua Zigbee**
(SONOFF SWV-NH) y el **interruptor ON/OFF** vía MQTT/Zigbee2MQTT, con el mismo estilo
visual RAF (*Red Autogestionada Fractal*) que la GUI de ESP-NOW (`espnow_gui.py`).

Se ejecuta en la Raspberry Pi donde corre el broker **Mosquitto** y **Zigbee2MQTT**
(por defecto conecta a `127.0.0.1:1883`, configurable para control remoto).

## Requisitos

```bash
pip install -r requirements.txt
```

Dependencias:
- **PyQt6** — interfaz gráfica.
- **paho-mqtt** — cliente MQTT (habla con Mosquitto/Zigbee2MQTT).
- **pyserial** — requerida por la GUI de ESP-NOW (mismo `requirements.txt`).

La lógica MQTT se reutiliza de `valvulas.py` (mismo directorio): `ValvulasMqtt`,
`VALVULAS`, `INTERRUPTORES`, `Z2M_PREFIX`.

## Ejecución

```bash
python3 valvulas_gui.py
```

> En la Raspberry Pi (demo): `cd /home/jma/valvulas-zigbee && python3 valvulas_gui.py`

## Flujo de uso

1. Dejar el **broker MQTT** y **Zigbee2MQTT** activos (`systemctl status mosquitto`,
   `docker compose up -d` en `/home/jma/valvulas-zigbee`).
2. Abrir la GUI. El host/puerto MQTT aparecen en el panel (por defecto `127.0.0.1:1883`).
3. Clic en **Conectar** — la app se suscribe a los topics de los 3 dispositivos y
   lanza una lectura inicial de todos.
4. **Leer** un dispositivo: botón circular `?` de su tarjeta (fuerza un `get` real).
5. **Accionar**: `Abrir`/`Cerrar` en válvulas, `Encender`/`Apagar` en el interruptor —
   la app espera la confirmación real del dispositivo (`optimistic: false`).

![estilo] Las 3 tarjetas siguen el mismo patrón visual de la GUI ESP-NOW: emoji,
estado ON/OFF, métricas, botón circular `?` con animación y efecto hover.

## Interfaz

- **Panel de conexión**: host MQTT, puerto y botón Conectar/Desconectar.
- **Tarjetas por dispositivo**:

| Dispositivo | Emoji | Tipo | Acciones |
|---|---|---|---|
| valvula_riego | 💧 | Válvula (batería) | Abrir / Cerrar |
| valvula_principal | 🚰 | Válvula (batería) | Abrir / Cerrar |
| interruptor | 💡 | Interruptor (red) | Encender / Apagar |

- **Métricas (válvulas)**: batería (%), caudal (m³/h y L/min), volumen de sesión (L)
  y volumen diario (L/día), tomadas del payload publicado por Zigbee2MQTT.
- **Botón circular** `?`: lectura forzada (`get`) con animación de flash.
- **Botón `⟳ Actualizar todo`**: lee el estado de los 3 dispositivos.
- **Header**: título RAF y estado de conexión (punto + texto).
- **Polling suave**: cada 1.5 s la GUI refresca las tarjetas desde el último payload
  recibido (si un `get` falla por sueño profundo, la caché sigue actualizándose).

## Topics MQTT utilizados

| Topic | Uso |
|---|---|
| `zigbee2mqtt/<dispositivo>` | Estado publicado por Zigbee2MQTT (suscrito) |
| `zigbee2mqtt/<dispositivo>/get` | Lectura forzada: `{"state": ""}` |
| `zigbee2mqtt/<dispositivo>/set` | Comando: `{"state": "ON"}` / `{"state": "OFF"}` |

Las válvulas SONOFF SWV-NH son *end devices* a batería que duermen; por eso el `get`
es necesario para "despertarlas" y leer su estado real. El interruptor va alimentado
a red y responde de inmediato.

## Solución de problemas

| Síntoma | Causa / solución |
|---|---|
| No conecta al broker | ¿Mosquitto activo? `systemctl status mosquitto`. Conectar a otro host: escribir la IP en el campo **Broker MQTT**. |
| Dispositivo muestra `?` / "sin datos" | End device dormido. Pulsar `?` de nuevo (el get lo despierta) o esperar su check-in periódico (2–5 min si se reinició la RPi). |
| Sin confirmación al accionar | El dispositivo no respondió en 20 s: comprobar batería, rango Zigbee y logs (`docker compose logs -f` en valvulas-zigbee). |
| La GUI no abre en la RPi | `pip install PyQt6 paho-mqtt` (PyQt6 se instala con wheel `manylinux_2_28` arm64; requiere pip reciente). |

## Notas técnicas

- Las operaciones bloqueantes (lectura/confirmación, hasta 15–20 s) corren en
  `QThread` — la interfaz nunca se congela.
- La GUI espera confirmación **real** del dispositivo (no optimista), igual que la
  CLI `valvulas.py`: publica `set`, borra la caché del topic y espera que el payload
  reporte el estado deseado antes de dar el OK.
- Para ejecutar desde el notebook contra la RPi, usar `192.168.1.58:1883` como broker
  (la RPi y el broker deben compartir red).