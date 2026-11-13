# Panel de Control RAF — GUI de escritorio

Aplicación de escritorio en Python (PyQt6) para operar la red de sensores a través del Bridge.

La GUI presenta la red bajo el acrónimo **RAF** (*Red Autogestionada Fractal*), un nombre de marca para la demo, sin referencia técnica a la tecnología subyacente.

## Requisitos

```bash
pip install -r requirements.txt
```

Dependencias:
- **PyQt6** — interfaz gráfica.
- **pyserial** — comunicación serie con el Bridge.

## Ejecución

```bash
python3 espnow_gui.py
```

## Flujo de uso

1. Seleccionar el puerto serie del Bridge (ej. `/dev/ttyUSB0`).
2. Clic en **Conectar**.
3. La app espera ~2.5 s (el Bridge se resetea al abrir el puerto) y, al quedar listo, envía `auto off` para silenciar el auto-polling.
4. Consultar cada nodo con el botón circular `?` de su tarjeta.

## Interfaz

- **Tarjetas por nodo**: cada una muestra su **propósito** (⚡ consumo eléctrico, 🔌 contacto, 🔊 volumen, 🔘 botón, 🚪 puerta, 🌡️ temperatura contacto, 🔥 temperatura IR, 🌀 acelerómetro) con un emoji alusivo.
- **Botón circular** `?` en cada tarjeta: consulta el estado del nodo y muestra el resultado dentro de la misma tarjeta (con animación de flash al presionar).
- **Efecto hover**: al pasar el mouse, la tarjeta se ilumina sutilmente.
- **Barra de comandos**: `Ping todos`, `Listar nodos`, `Estadísticas`, `Auto ON/OFF`, `Ayuda`.
- **Header**: estado de conexión (punto + texto) y contador `RX` (bytes/líneas recibidas).

## Mapeo de propósitos

| # | Propósito | Emoji |
|---|---|---|
| 0 | Consumo eléctrico | ⚡ |
| 1 | Contacto eléctrico | 🔌 |
| 2 | Volumen | 🔊 |
| 3 | Botón | 🔘 |
| 4 | Sensor de puerta | 🚪 |
| 6 | Temperatura contacto | 🌡️ |
| 7 | Temperatura IR | 🔥 |
| 8 | Acelerómetro | 🌀 |

> El nodo 5 (Bridge/maestro) no se muestra: es el coordinador y no aporta información de sensor.

## Notas

- La GUI lee la salida del Bridge a **115200 baud** y parsea los bloques `RESPUESTA RECIBIDA - Nodo #N` para actualizar las tarjetas.
- Si el puerto está ocupado (ej. CuteCom abierto), cerrar la otra aplicación antes de conectar.
