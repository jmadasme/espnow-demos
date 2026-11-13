#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
valvulas_gui.py — Panel de control Zigbee (estilo RAF)
======================================================
Interfaz gráfica para controlar los dispositivos Zigbee (válvulas SONOFF
SWV-NH y el interruptor ON/OFF) vía MQTT/Zigbee2MQTT. Reutiliza la lógica
de `valvulas.py` (mismo directorio).

Cada tarjeta permite:
  - Leer el estado real (botón circular "?") — fuerza un "get".
  - Ejecutar acciones (Abrir/Cerrar o Encender/Apagar).
  - Ver el estado ON/OFF y, en válvulas, las métricas (batería, caudal, volumen).

Requisitos:
    pip install PyQt6 pyserial paho-mqtt

Uso:
    python3 valvulas_gui.py

El host MQTT es configurable en el panel (por defecto 127.0.0.1, para
ejecutarse en la propia Raspberry Pi donde corre Mosquitto/Zigbee2MQTT).
"""

import sys
import time
import json

from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QPushButton, QComboBox, QFrame, QSizePolicy, QMessageBox,
    QGraphicsOpacityEffect, QLineEdit
)

import valvulas  # ValvulasMqtt, VALVULAS, INTERRUPTORES, Z2M_PREFIX

# ----------------------------------------------------------------------
# Colores del tema (misma línea gráfica del panel RAF)
# ----------------------------------------------------------------------
BG        = "#0d1117"
BG_HEADER = "#161b22"
CARD      = "#1c2128"
CARD_HI   = "#232a33"
BORDER    = "#30363d"
ACCENT    = "#2dd4bf"
ACCENT_DK = "#14b8a6"
TEXT      = "#e6edf3"
MUTED     = "#8b949e"
GREEN     = "#3fb950"
RED       = "#f85149"
YELLOW    = "#d29922"

APP_ACRONYM = "RAF"
APP_FULL = "Red Autogestionada Fractal"

# ----------------------------------------------------------------------
# Dispositivos Zigbee (alias -> metadatos de la tarjeta)
# ----------------------------------------------------------------------
DEVICES = [
    {"alias": "valvula_riego",      "emoji": "💧", "nombre": "Válvula riego",      "tipo": "valvula"},
    {"alias": "valvula_principal",  "emoji": "🚰", "nombre": "Válvula principal",  "tipo": "valvula"},
    {"alias": "interruptor",        "emoji": "💡", "nombre": "Interruptor",        "tipo": "interruptor"},
]

CONFIRM_TIMEOUT = 20
READ_TIMEOUT = 15

# ----------------------------------------------------------------------
# Operaciones MQTT (bloqueantes) — reutilizan la lógica de menu_valvulas
# ----------------------------------------------------------------------
def _topic_name(alias):
    if alias in valvulas.VALVULAS:
        return valvulas.VALVULAS[alias]
    return valvulas.INTERRUPTORES[alias]


def refresh_state_fn(mq, alias):
    """Fuerza una lectura (get) y devuelve el estado real ('ON'/'OFF'/'?')."""
    topic_name = _topic_name(alias)
    topic = f"{valvulas.Z2M_PREFIX}/{topic_name}"
    for _ in range(2):
        mq.received.pop(topic, None)
        try:
            mq.publish_json(f"{topic}/get", {"state": ""})
        except ConnectionError:
            return "?"
        payload = mq.wait_for(topic, lambda v: v is not None, timeout=READ_TIMEOUT)
        if isinstance(payload, dict) and "state" in payload:
            return payload["state"]
    return "?"


def valve_command_fn(mq, alias, new_state):
    topic_name = valvulas.VALVULAS[alias]
    topic = f"{valvulas.Z2M_PREFIX}/{topic_name}"
    mq.received.pop(topic, None)
    try:
        mq.publish_json(f"{topic}/get", {"state": ""})
        mq.wait_for(topic, lambda v: v is not None, timeout=READ_TIMEOUT)
    except ConnectionError:
        pass
    mq.received.pop(topic, None)
    try:
        mq.publish_json(f"{topic}/set", {"state": new_state})
    except ConnectionError as e:
        return False, str(e), None
    confirmed = mq.wait_for(
        topic,
        lambda v: isinstance(v, dict) and v.get("state") == new_state,
        timeout=CONFIRM_TIMEOUT,
    )
    if confirmed is None:
        return False, f"Sin confirmación de '{alias}' en {CONFIRM_TIMEOUT}s", None
    return True, f"'{alias}' quedó en {new_state}", confirmed


def switch_command_fn(mq, alias, new_state):
    topic_name = valvulas.INTERRUPTORES[alias]
    topic = f"{valvulas.Z2M_PREFIX}/{topic_name}"
    mq.received.pop(topic, None)
    try:
        mq.publish_json(f"{topic}/set", {"state": new_state})
    except ConnectionError as e:
        return False, str(e), None
    confirmed = mq.wait_for(
        topic,
        lambda v: isinstance(v, dict) and v.get("state") == new_state,
        timeout=CONFIRM_TIMEOUT,
    )
    if confirmed is None:
        return False, f"Sin confirmación de '{alias}' en {CONFIRM_TIMEOUT}s", None
    return True, f"'{alias}' quedó en {new_state}", confirmed


def _fmt(value, unit="", decimals=None):
    if value is None or value == "":
        return "-"
    try:
        f = float(value)
        if decimals is None:
            text = f"{int(f)}" if f == int(f) else f"{f:.2f}"
        else:
            text = f"{f:.{decimals}f}"
        return f"{text}{unit}"
    except (TypeError, ValueError):
        return f"{value}{unit}"


def _metrics_text(payload):
    """Resume las métricas de una válvula a partir de su payload."""
    if not isinstance(payload, dict):
        return ""
    parts = []
    battery = payload.get("battery")
    if battery is not None:
        parts.append(f"🔋 {_fmt(battery, ' %')}")
    flow = payload.get("flow")
    if flow is not None:
        try:
            lpm = float(flow) * 1000 / 60
            parts.append(f"🌊 {_fmt(flow, ' m³/h', 3)} ({_fmt(lpm, ' L/min', 1)})")
        except (TypeError, ValueError):
            parts.append(f"🌊 {flow} m³/h")
    vol = payload.get("real_time_irrigation_volume")
    if vol is not None:
        parts.append(f"🧾 {_fmt(vol, ' L')}")
    daily = payload.get("daily_irrigation_volume")
    if daily is not None:
        parts.append(f"📅 {_fmt(daily, ' L/día')}")
    return "  ·  ".join(parts)


def apply_style(app):
    app.setStyleSheet(f"""
        QMainWindow, QWidget {{ background-color: {BG}; color: {TEXT};
                                font-family: 'Segoe UI', 'Ubuntu', 'DejaVu Sans', 'Noto Color Emoji'; }}
        QLabel {{ background: transparent; }}
        #header {{ background-color: {BG_HEADER}; border-bottom: 1px solid {BORDER}; }}
        #appTitle {{ font-size: 20px; font-weight: 700; }}
        #appSub {{ font-size: 11px; color: {MUTED}; }}
        QComboBox, QLineEdit {{
            background-color: {CARD}; color: {TEXT}; border: 1px solid {BORDER};
            border-radius: 6px; padding: 6px 10px; min-height: 18px;
        }}
        QComboBox::drop-down {{ border: none; width: 20px; }}
        QComboBox QAbstractItemView {{
            background-color: {CARD}; color: {TEXT};
            border: 1px solid {BORDER}; selection-background-color: {ACCENT_DK};
        }}
        QPushButton {{
            background-color: {CARD}; color: {TEXT}; border: 1px solid {BORDER};
            border-radius: 6px; padding: 7px 14px; font-weight: 600;
        }}
        QPushButton:hover {{ background-color: {CARD_HI}; border-color: {ACCENT_DK}; }}
        QPushButton:pressed {{ background-color: {ACCENT_DK}; }}
        QPushButton#primary {{
            background-color: {ACCENT}; color: #06251f; border: none; font-weight: 700;
        }}
        QPushButton#primary:hover {{ background-color: {ACCENT_DK}; }}
        QPushButton#danger {{ color: {RED}; }}
        QPushButton#actionOn {{ background-color: {GREEN}; color: #04120a; border: none; }}
        QPushButton#actionOff {{ background-color: {RED}; color: #1a0503; border: none; }}
        QPushButton#actionOn:hover, QPushButton#actionOff:hover {{ background-color: #57d95e; }}
        QPushButton#actionOff:hover {{ background-color: #ff6b66; }}
        QPushButton#roundBtn {{
            background-color: {CARD_HI}; color: #facc15; border: 2px solid {ACCENT_DK};
            border-radius: 23px; font-size: 22px; font-weight: 800;
        }}
        QPushButton#roundBtn:hover {{ background-color: {ACCENT_DK}; color: #06251f; }}
        QFrame#card {{
            background-color: {CARD}; border: 1px solid {BORDER}; border-radius: 10px;
        }}
        QFrame#card[hovered="true"] {{
            background-color: {CARD_HI}; border: 1px solid {ACCENT};
        }}
        QFrame#card[online="true"] {{ border-color: {ACCENT_DK}; }}
    """)


# ----------------------------------------------------------------------
# Hilo para operaciones MQTT bloqueantes (no congela la UI)
# ----------------------------------------------------------------------
class MqttOp(QThread):
    ok = pyqtSignal(object)
    err = pyqtSignal(str)

    def __init__(self, fn, *args, parent=None):
        super().__init__(parent)
        self.fn = fn
        self.args = args

    def run(self):
        try:
            result = self.fn(*self.args)
            self.ok.emit(result)
        except Exception as e:
            self.err.emit(str(e))


# ----------------------------------------------------------------------
# Tarjeta de dispositivo
# ----------------------------------------------------------------------
class DeviceCard(QFrame):
    read_requested = pyqtSignal(str)      # alias
    action_requested = pyqtSignal(str, str)  # alias, new_state

    def __init__(self, alias, emoji, nombre, tipo, parent=None):
        super().__init__(parent)
        self.alias = alias
        self.tipo = tipo
        self.setObjectName("card")
        self.setProperty("online", "false")
        self.setMinimumHeight(170)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(16, 14, 16, 14)
        lay.setSpacing(8)

        # Título: emoji + nombre + estado
        top = QHBoxLayout()
        self.title = QLabel(f"{emoji} {nombre}")
        self.title.setStyleSheet(f"font-size: 15px; font-weight: 700; color: {ACCENT};")
        self.dot = QLabel("●")
        self.dot.setStyleSheet(f"color: {MUTED}; font-size: 13px;")
        self.state_label = QLabel("?")
        self.state_label.setStyleSheet(f"font-weight: 700; color: {MUTED};")
        top.addWidget(self.title)
        top.addStretch()
        top.addWidget(self.state_label)
        top.addWidget(self.dot)
        lay.addLayout(top)

        # Métricas / detalle
        self.value_label = QLabel("— sin datos —")
        self.value_label.setStyleSheet(f"font-size: 11px; color: {MUTED};")
        self.value_label.setWordWrap(True)
        lay.addWidget(self.value_label)

        lay.addStretch()

        # Fila de acciones + botón circular de lectura
        bottom = QHBoxLayout()
        bottom.setSpacing(8)
        if tipo == "valvula":
            self.btn_on = QPushButton("Abrir")
            self.btn_on.setObjectName("actionOn")
            self.btn_on.setCursor(Qt.CursorShape.PointingHandCursor)
            self.btn_on.clicked.connect(lambda: self.action_requested.emit(self.alias, "ON"))
            self.btn_off = QPushButton("Cerrar")
            self.btn_off.setObjectName("actionOff")
            self.btn_off.setCursor(Qt.CursorShape.PointingHandCursor)
            self.btn_off.clicked.connect(lambda: self.action_requested.emit(self.alias, "OFF"))
            bottom.addWidget(self.btn_on)
            bottom.addWidget(self.btn_off)
        else:
            self.btn_on = QPushButton("Encender")
            self.btn_on.setObjectName("actionOn")
            self.btn_on.setCursor(Qt.CursorShape.PointingHandCursor)
            self.btn_on.clicked.connect(lambda: self.action_requested.emit(self.alias, "ON"))
            self.btn_off = QPushButton("Apagar")
            self.btn_off.setObjectName("actionOff")
            self.btn_off.setCursor(Qt.CursorShape.PointingHandCursor)
            self.btn_off.clicked.connect(lambda: self.action_requested.emit(self.alias, "OFF"))
            bottom.addWidget(self.btn_on)
            bottom.addWidget(self.btn_off)

        bottom.addStretch()
        self.read_btn = QPushButton("?")
        self.read_btn.setObjectName("roundBtn")
        self.read_btn.setFixedSize(46, 46)
        self.read_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.read_btn.setToolTip("Leer estado")
        self._opacity = QGraphicsOpacityEffect(self)
        self.read_btn.setGraphicsEffect(self._opacity)
        self._anim = QPropertyAnimation(self._opacity, b"opacity")
        self._anim.setDuration(180)
        self._anim.setEasingCurve(QEasingCurve.Type.InOutQuad)
        self.read_btn.clicked.connect(self._on_read)
        bottom.addWidget(self.read_btn)
        lay.addLayout(bottom)

    def _on_read(self):
        self._anim.setKeyValueAt(0.0, 1.0)
        self._anim.setKeyValueAt(0.5, 0.35)
        self._anim.setKeyValueAt(1.0, 1.0)
        self._anim.start()
        self.read_requested.emit(self.alias)

    def set_state(self, state):
        """Actualiza el estado ON/OFF y el color del punto."""
        if state == "ON":
            self.state_label.setText("ON")
            self.state_label.setStyleSheet(f"font-weight: 700; color: {GREEN};")
            self.dot.setStyleSheet(f"color: {GREEN}; font-size: 13px;")
        elif state == "OFF":
            self.state_label.setText("OFF")
            self.state_label.setStyleSheet(f"font-weight: 700; color: {RED};")
            self.dot.setStyleSheet(f"color: {RED}; font-size: 13px;")
        else:
            self.state_label.setText("?")
            self.state_label.setStyleSheet(f"font-weight: 700; color: {MUTED};")
            self.dot.setStyleSheet(f"color: {MUTED}; font-size: 13px;")

    def set_metrics(self, text):
        if text:
            self.value_label.setText(text)
            self.value_label.setStyleSheet(f"font-size: 11px; color: {TEXT};")

    def enterEvent(self, event):
        self.setProperty("hovered", True)
        self.style().unpolish(self)
        self.style().polish(self)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.setProperty("hovered", False)
        self.style().unpolish(self)
        self.style().polish(self)
        super().leaveEvent(event)


# ----------------------------------------------------------------------
# Ventana principal
# ----------------------------------------------------------------------
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_ACRONYM} · Válvulas Zigbee")
        self.resize(900, 620)
        self.mq = None
        self._ops = []

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Header
        header = QFrame()
        header.setObjectName("header")
        hl = QHBoxLayout(header)
        hl.setContentsMargins(20, 14, 20, 14)
        tb = QVBoxLayout()
        tb.setSpacing(2)
        t = QLabel(f"{APP_ACRONYM} — Válvulas Zigbee")
        t.setObjectName("appTitle")
        s = QLabel(f"{APP_FULL} · Control de dispositivos via MQTT")
        s.setObjectName("appSub")
        tb.addWidget(t)
        tb.addWidget(s)
        hl.addLayout(tb)
        hl.addStretch()
        self.status_dot = QLabel("●")
        self.status_dot.setStyleSheet(f"color: {RED}; font-size: 14px;")
        self.status_text = QLabel("Desconectado")
        self.status_text.setStyleSheet(f"font-weight: 600; color: {MUTED};")
        hl.addWidget(self.status_dot)
        hl.addWidget(self.status_text)
        root.addWidget(header)

        # Cuerpo
        body = QWidget()
        bl = QVBoxLayout(body)
        bl.setContentsMargins(20, 18, 20, 18)
        bl.setSpacing(14)
        root.addWidget(body)

        # Fila de conexión MQTT
        conn = QHBoxLayout()
        conn.setSpacing(8)
        conn.addWidget(QLabel("Broker MQTT:"))
        self.host_edit = QLineEdit("127.0.0.1")
        self.host_edit.setFixedWidth(140)
        conn.addWidget(self.host_edit)
        conn.addWidget(QLabel("Puerto:"))
        self.port_edit = QLineEdit("1883")
        self.port_edit.setFixedWidth(60)
        conn.addWidget(self.port_edit)
        conn.addStretch()
        self.btn_connect = QPushButton("Conectar")
        self.btn_connect.setObjectName("primary")
        self.btn_connect.clicked.connect(self.toggle_connection)
        conn.addWidget(self.btn_connect)
        bl.addLayout(conn)

        # Barra de acciones globales
        cmds = QHBoxLayout()
        cmds.setSpacing(8)
        self.btn_refresh = QPushButton("⟳ Actualizar todo")
        self.btn_refresh.clicked.connect(self.refresh_all)
        cmds.addWidget(self.btn_refresh)
        cmds.addStretch()
        bl.addLayout(cmds)

        # Grid de tarjetas
        grid = QGridLayout()
        grid.setSpacing(12)
        self.cards = {}
        for i, dev in enumerate(DEVICES):
            card = DeviceCard(dev["alias"], dev["emoji"], dev["nombre"], dev["tipo"])
            card.read_requested.connect(self.read_state)
            card.action_requested.connect(self.do_action)
            self.cards[dev["alias"]] = card
            grid.addWidget(card, i // 3, i % 3)
        bl.addLayout(grid, stretch=1)

        # Temporizador para refrescar estados desde la caché MQTT
        self._timer = QTimer(self)
        self._timer.setInterval(1500)
        self._timer.timeout.connect(self._poll_states)
        self._timer.start()

    # ------------------------------------------------------------------
    # Conexión MQTT
    # ------------------------------------------------------------------
    def toggle_connection(self):
        if self.mq is not None:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        host = self.host_edit.text().strip() or "127.0.0.1"
        try:
            port = int(self.port_edit.text().strip())
        except ValueError:
            port = 1883
        valvulas.MQTT_HOST = host
        valvulas.MQTT_PORT = port

        self.mq = valvulas.ValvulasMqtt()
        try:
            self.mq.connect()
        except ConnectionError as e:
            QMessageBox.critical(self, "Error", str(e))
            self.mq = None
            return

        # Suscribirse a todos los topics de dispositivos
        for dev in DEVICES:
            topic_name = _topic_name(dev["alias"])
            self.mq.subscribe(f"{valvulas.Z2M_PREFIX}/{topic_name}")

        self.btn_connect.setText("Desconectar")
        self.btn_connect.setObjectName("danger")
        self.btn_connect.style().unpolish(self.btn_connect)
        self.btn_connect.style().polish(self.btn_connect)
        self.set_status(True)
        self.refresh_all()

    def disconnect(self):
        if self.mq:
            self.mq.disconnect()
        self.mq = None
        self.btn_connect.setText("Conectar")
        self.btn_connect.setObjectName("primary")
        self.btn_connect.style().unpolish(self.btn_connect)
        self.btn_connect.style().polish(self.btn_connect)
        self.set_status(False)
        for card in self.cards.values():
            card.set_state("?")
            card.set_metrics("— sin datos —")

    def set_status(self, connected):
        if connected:
            self.status_dot.setStyleSheet(f"color: {GREEN}; font-size: 14px;")
            self.status_text.setText("Conectado")
            self.status_text.setStyleSheet(f"font-weight: 600; color: {GREEN};")
        else:
            self.status_dot.setStyleSheet(f"color: {RED}; font-size: 14px;")
            self.status_text.setText("Desconectado")
            self.status_text.setStyleSheet(f"font-weight: 600; color: {MUTED};")

    # ------------------------------------------------------------------
    # Lecturas y acciones (en hilo aparte)
    # ------------------------------------------------------------------
    def refresh_all(self):
        if not self.mq:
            return
        for dev in DEVICES:
            self.read_state(dev["alias"])

    def read_state(self, alias):
        if not self.mq:
            return
        op = MqttOp(refresh_state_fn, self.mq, alias)
        op.ok.connect(lambda r, a=alias: self._on_state_read(a, r))
        op.err.connect(lambda e, a=alias: self._on_state_read(a, "?"))
        self._ops.append(op)
        op.finished.connect(lambda: self._cleanup(op))
        op.start()

    def _on_state_read(self, alias, state):
        card = self.cards.get(alias)
        if card:
            card.set_state(state)
        # También capturar métricas si hay payload
        if self.mq:
            topic_name = _topic_name(alias)
            topic = f"{valvulas.Z2M_PREFIX}/{topic_name}"
            payload = self.mq.received.get(topic)
            if isinstance(payload, dict):
                card.set_metrics(_metrics_text(payload))

    def do_action(self, alias, new_state):
        if not self.mq:
            QMessageBox.warning(self, "Sin conexión", "No estás conectado al broker MQTT.")
            return
        dev = next((d for d in DEVICES if d["alias"] == alias), None)
        if dev is None:
            return
        if dev["tipo"] == "valvula":
            fn = valve_command_fn
        else:
            fn = switch_command_fn
        op = MqttOp(fn, self.mq, alias, new_state)
        op.ok.connect(lambda r: self._on_action_done(r))
        op.err.connect(lambda e: self._on_action_error(e))
        self._ops.append(op)
        op.finished.connect(lambda: self._cleanup(op))
        op.start()

    def _on_action_done(self, result):
        ok, msg, payload = result
        if ok:
            # Refrescar el estado y métricas del dispositivo afectado
            self.refresh_all()
        else:
            QMessageBox.warning(self, "Sin confirmación", msg)

    def _on_action_error(self, msg):
        QMessageBox.critical(self, "Error", msg)

    def _cleanup(self, op):
        if op in self._ops:
            self._ops.remove(op)

    # ------------------------------------------------------------------
    # Polling de estados desde la caché MQTT
    # ------------------------------------------------------------------
    def _poll_states(self):
        if not self.mq:
            return
        for dev in DEVICES:
            topic_name = _topic_name(dev["alias"])
            topic = f"{valvulas.Z2M_PREFIX}/{topic_name}"
            payload = self.mq.received.get(topic)
            card = self.cards[dev["alias"]]
            if isinstance(payload, dict):
                state = payload.get("state")
                if state in ("ON", "OFF"):
                    card.set_state(state)
                if dev["tipo"] == "valvula":
                    card.set_metrics(_metrics_text(payload))

    def closeEvent(self, event):
        self.disconnect()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName(APP_ACRONYM)
    apply_style(app)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
