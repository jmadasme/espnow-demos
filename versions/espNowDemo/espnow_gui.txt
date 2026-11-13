#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP-NOW Demo — Panel de Control (Escritorio)
============================================
Interfaz gráfica premium para operar la red ESP-NOW a través del Bridge.

- Conexión serie al Bridge (115200 baud).
- Lanza los comandos: ping <id>, ping all, list, stats, auto on/off, help.
- Tarjetas de estado por nodo (0-8) actualizadas en vivo.
- Consola con el log de la comunicación.

Requisitos:
    pip install PyQt6 pyserial

Uso:
    python3 espnow_gui.py
"""

import sys
import serial
import serial.tools.list_ports
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QFont, QColor
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QPushButton, QComboBox, QFrame, QSizePolicy,
    QMessageBox, QSpacerItem, QGraphicsOpacityEffect
)

BAUD = 115200

# Nombre de la red (sin referencia a ESP-NOW)
APP_ACRONYM = "RAF"
APP_FULL = "Red Autogestionada Fractal"

# ----------------------------------------------------------------------
# Colores del tema (oscuro premium)
# ----------------------------------------------------------------------
BG        = "#0d1117"
BG_HEADER = "#161b22"
CARD      = "#1c2128"
CARD_HI   = "#232a33"
BORDER    = "#30363d"
ACCENT    = "#2dd4bf"   # teal
ACCENT_DK = "#14b8a6"
TEXT      = "#e6edf3"
MUTED     = "#8b949e"
GREEN     = "#3fb950"
RED       = "#f85149"
YELLOW    = "#d29922"
BLUE      = "#58a6ff"

# ----------------------------------------------------------------------
# Información de los nodos: (emoji, propósito, detalle técnico)
# ----------------------------------------------------------------------
NODE_INFO = {
    0: ("⚡", "Consumo eléctrico", "Sensor de consumo"),
    1: ("🔌", "Contacto eléctrico", "Estado del contacto"),
    2: ("🔊", "Volumen", "Nivel de sonido"),
    3: ("🔘", "Botón", "Pulsador digital"),
    4: ("🚪", "Sensor de puerta", "Apertura / cierre"),
    6: ("🌡️", "Temperatura contacto", "Sonda DS18B20"),
    7: ("🔥", "Temperatura IR", "GY-906 infrarrojo"),
    8: ("🌀", "Acelerómetro", "Vibración / inclinación"),
}


def apply_style(app: QApplication):
    app.setStyleSheet(f"""
        QMainWindow, QWidget {{ background-color: {BG}; color: {TEXT};
                                font-family: 'Segoe UI', 'Ubuntu', 'DejaVu Sans', 'Noto Color Emoji'; }}
        QLabel {{ background: transparent; }}
        #header {{ background-color: {BG_HEADER}; border-bottom: 1px solid {BORDER}; }}
        #appTitle {{ font-size: 20px; font-weight: 700; color: {TEXT}; }}
        #appSub {{ font-size: 11px; color: {MUTED}; }}
        #statusDot {{ border-radius: 7px; }}
        QComboBox {{
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
        QPushButton#roundBtn {{
            background-color: {CARD_HI}; color: #facc15; border: 2px solid {ACCENT_DK};
            border-radius: 23px; font-size: 22px; font-weight: 800;
        }}
        QPushButton#roundBtn:hover {{ background-color: {ACCENT_DK}; color: #06251f; }}
        QPushButton#roundBtn:pressed {{ background-color: #0d9488; }}
        QFrame#card {{
            background-color: {CARD}; border: 1px solid {BORDER}; border-radius: 10px;
        }}
        QFrame#card[hovered="true"] {{
            background-color: {CARD_HI}; border: 1px solid {ACCENT};
        }}
        QFrame#card[online="true"] {{ border-color: {ACCENT_DK}; }}
    """)


# ----------------------------------------------------------------------
# Hilo lector del puerto serie (no bloquea la UI)
# ----------------------------------------------------------------------
class SerialReader(QThread):
    line_received = pyqtSignal(str)

    def __init__(self, ser, parent=None):
        super().__init__(parent)
        self.ser = ser
        self._running = True

    def run(self):
        while self._running:
            try:
                data = self.ser.readline()
            except Exception:
                break
            if data:
                self.line_received.emit(data.decode("utf-8", errors="replace").rstrip())

    def stop(self):
        self._running = False
        self.wait(1500)


# ----------------------------------------------------------------------
# Tarjeta de un nodo
# ----------------------------------------------------------------------
class NodeCard(QFrame):
    ping_requested = pyqtSignal(int)

    def __init__(self, node_id, parent=None):
        super().__init__(parent)
        self.node_id = node_id
        self.setObjectName("card")
        self.setProperty("online", "false")
        self.setMinimumHeight(120)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        emoji, purpose, detail = NODE_INFO[node_id]
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 12, 14, 12)
        lay.setSpacing(6)

        top = QHBoxLayout()
        self.id_label = QLabel(f"{emoji} {purpose}")
        self.id_label.setStyleSheet(f"font-size: 14px; font-weight: 700; color: {ACCENT};")
        self.dot = QLabel("●")
        self.dot.setObjectName("statusDot")
        self.dot.setStyleSheet(f"color: {MUTED}; font-size: 13px;")
        self.num_label = QLabel(f"#{node_id}")
        self.num_label.setStyleSheet(f"font-size: 10px; color: {MUTED};")
        top.addWidget(self.id_label)
        top.addStretch()
        top.addWidget(self.num_label)
        top.addWidget(self.dot)
        lay.addLayout(top)

        self.desc_label = QLabel(detail)
        self.desc_label.setStyleSheet(f"font-size: 10px; color: {MUTED};")
        lay.addWidget(self.desc_label)

        self.value_label = QLabel("— sin datos —")
        self.value_label.setStyleSheet(f"font-size: 11px; color: {MUTED};")
        self.value_label.setWordWrap(True)
        lay.addWidget(self.value_label)

        # Boton circular de consulta, justificado abajo a la derecha
        bottom = QHBoxLayout()
        bottom.setContentsMargins(0, 4, 12, 10)
        bottom.addStretch()
        self.ping_btn = QPushButton("?")
        self.ping_btn.setObjectName("roundBtn")
        self.ping_btn.setFixedSize(46, 46)
        self.ping_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.ping_btn.setToolTip("Consultar estado")
        # Efecto de opacidad para la animacion de pulso
        self._opacity = QGraphicsOpacityEffect(self)
        self.ping_btn.setGraphicsEffect(self._opacity)
        self._anim = QPropertyAnimation(self._opacity, b"opacity")
        self._anim.setDuration(180)
        self._anim.setEasingCurve(QEasingCurve.Type.InOutQuad)
        self.ping_btn.clicked.connect(self._on_consult)
        bottom.addWidget(self.ping_btn)
        lay.addLayout(bottom)

    def _on_consult(self):
        # animacion de flash (baja y sube la opacidad) + emitir la senal
        self._anim.setKeyValueAt(0.0, 1.0)
        self._anim.setKeyValueAt(0.5, 0.35)
        self._anim.setKeyValueAt(1.0, 1.0)
        self._anim.start()
        self.ping_requested.emit(self.node_id)

    def set_online(self, online: bool):
        self.setProperty("online", "true" if online else "false")
        self.dot.setStyleSheet(f"color: {GREEN if online else MUTED}; font-size: 13px;")
        self.style().unpolish(self)
        self.style().polish(self)

    def set_value(self, text: str):
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
        self.setWindowTitle(f"{APP_ACRONYM} · Panel de Control")
        self.resize(1040, 780)
        self.ser = None
        self.reader = None
        self._response_buf = []
        self._capturing = False
        self._ready = False
        self.rx_count = 0

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ---- Header ----
        header = QFrame()
        header.setObjectName("header")
        hl = QHBoxLayout(header)
        hl.setContentsMargins(20, 14, 20, 14)
        title_box = QVBoxLayout()
        title_box.setSpacing(2)
        t = QLabel(f"{APP_ACRONYM} Control Panel")
        t.setObjectName("appTitle")
        s = QLabel(f"{APP_FULL} · Sensores inalámbricos · Bridge maestro")
        s.setObjectName("appSub")
        title_box.addWidget(t)
        title_box.addWidget(s)
        hl.addLayout(title_box)
        hl.addStretch()

        # estado de conexión
        self.status_dot = QLabel("●")
        self.status_dot.setObjectName("statusDot")
        self.status_dot.setStyleSheet(f"color: {RED}; font-size: 14px;")
        self.status_text = QLabel("Desconectado")
        self.status_text.setStyleSheet(f"font-weight: 600; color: {MUTED};")
        self.rx_label = QLabel("RX: 0")
        self.rx_label.setStyleSheet(f"font-weight: 700; color: {ACCENT}; font-size: 13px;")
        hl.addWidget(self.rx_label)
        hl.addWidget(self.status_dot)
        hl.addWidget(self.status_text)
        root.addWidget(header)

        # ---- Cuerpo ----
        body = QWidget()
        bl = QVBoxLayout(body)
        bl.setContentsMargins(20, 18, 20, 18)
        bl.setSpacing(14)
        root.addWidget(body)

        # Fila de conexión
        conn = QHBoxLayout()
        conn.setSpacing(8)
        conn.addWidget(QLabel("Puerto:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(180)
        self.refresh_ports()
        conn.addWidget(self.port_combo)
        btn_refresh = QPushButton("⟳")
        btn_refresh.setFixedWidth(40)
        btn_refresh.clicked.connect(self.refresh_ports)
        conn.addWidget(btn_refresh)
        conn.addWidget(QLabel("115200 baud"))
        conn.addStretch()
        self.btn_connect = QPushButton("Conectar")
        self.btn_connect.setObjectName("primary")
        self.btn_connect.clicked.connect(self.toggle_connection)
        conn.addWidget(self.btn_connect)
        bl.addLayout(conn)

        # Barra de comandos rápidos
        cmds = QHBoxLayout()
        cmds.setSpacing(8)
        for label, cmd in [
            ("Ping todos", "ping all"),
            ("Listar nodos", "list"),
            ("Estadísticas", "stats"),
            ("Auto ON", "auto on"),
            ("Auto OFF", "auto off"),
            ("Ayuda", "help"),
        ]:
            b = QPushButton(label)
            b.clicked.connect(lambda _, c=cmd: self.send_command(c))
            cmds.addWidget(b)
        cmds.addStretch()
        bl.addLayout(cmds)

        # Grid de nodos (3 columnas)
        grid = QGridLayout()
        grid.setSpacing(12)
        self.cards = {}
        for i, nid in enumerate(NODE_INFO):
            card = NodeCard(nid)
            card.ping_requested.connect(lambda n: self.send_command(f"ping {n}"))
            self.cards[nid] = card
            grid.addWidget(card, i // 3, i % 3)
        bl.addLayout(grid, stretch=1)

    # ------------------------------------------------------------------
    # Puertos serie
    # ------------------------------------------------------------------
    def refresh_ports(self):
        current = self.port_combo.currentText()
        self.port_combo.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo.addItems(ports)
        if current in ports:
            self.port_combo.setCurrentText(current)

    # ------------------------------------------------------------------
    # Conexión
    # ------------------------------------------------------------------
    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_combo.currentText()
        if not port:
            QMessageBox.warning(self, "Sin puerto", "No hay puerto serie seleccionado.")
            return
        try:
            self.ser = serial.Serial(port, BAUD, timeout=1)
        except Exception as e:
            QMessageBox.critical(self, "Error", f"No se pudo abrir {port}:\n{e}")
            return
        self.reader = SerialReader(self.ser)
        self.reader.line_received.connect(self.on_line)
        self.reader.start()
        self.btn_connect.setText("Desconectar")
        self.btn_connect.setObjectName("danger")
        self.btn_connect.style().unpolish(self.btn_connect)
        self.btn_connect.style().polish(self.btn_connect)
        self.set_status(True)
        self._ready = False
        self.status_text.setText("Conectando...")
        self.status_text.setStyleSheet(f"font-weight: 600; color: {YELLOW};")
        # El bridge se resetea al abrir el puerto; esperar a que arranque
        QTimer.singleShot(2500, self._mark_ready)

    def _mark_ready(self):
        self._ready = True
        self.set_status(True)
        # Desactivar el auto-polling para dejar el panel limpio
        # (las consultas se hacen manualmente con el boton circular)
        self.send_command("auto off")

    def disconnect(self):
        self._ready = False
        if self.reader:
            self.reader.stop()
            self.reader = None
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.btn_connect.setText("Conectar")
        self.btn_connect.setObjectName("primary")
        self.btn_connect.style().unpolish(self.btn_connect)
        self.btn_connect.style().polish(self.btn_connect)
        self.set_status(False)

    def set_status(self, connected: bool):
        if connected:
            self.status_dot.setStyleSheet(f"color: {GREEN}; font-size: 14px;")
            self.status_text.setText("Conectado")
            self.status_text.setStyleSheet(f"font-weight: 600; color: {GREEN};")
        else:
            self.status_dot.setStyleSheet(f"color: {RED}; font-size: 14px;")
            self.status_text.setText("Desconectado")
            self.status_text.setStyleSheet(f"font-weight: 600; color: {MUTED};")

    # ------------------------------------------------------------------
    # Envío de comandos
    # ------------------------------------------------------------------
    def send_command(self, cmd):
        if not (self.ser and self.ser.is_open):
            QMessageBox.warning(self, "Sin conexión", "No estás conectado al bridge.")
            return
        if not self._ready:
            return
        try:
            self.ser.write((cmd + "\n").encode())
            self.ser.flush()
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Error al enviar:\n{e}")

    # ------------------------------------------------------------------
    # Procesamiento de líneas recibidas del serial
    # ------------------------------------------------------------------
    def on_line(self, line):
        self.rx_count += 1
        self.rx_label.setText(f"RX: {self.rx_count}")
        self.parse_line(line)

    def parse_line(self, line):
        # Detectar inicio de una respuesta de nodo
        if "RESPUESTA RECIBIDA - Nodo #" in line:
            self._capturing = True
            self._response_buf = [line]
            return
        if self._capturing:
            self._response_buf.append(line)
            if line.strip() == "----------------------------------------" or \
               line.startswith("----------------------------------------"):
                self._capturing = False
                self.process_response(self._response_buf)
                self._response_buf = []

    def process_response(self, block):
        text = "\n".join(block)
        nid = None
        for line in block:
            if "RESPUESTA RECIBIDA - Nodo #" in line:
                try:
                    nid = int(line.split("#")[-1].strip())
                except Exception:
                    return
        if nid is None or nid not in self.cards:
            return
        card = self.cards[nid]
        card.set_online(True)

        parts = []
        for key in ("Estado digital (D2)", "Valor ADC (A0)",
                    "Temperatura (DS18B20)", "Temp. objeto (IR)", "Temp. ambiente",
                    "Acel. X (A0)", "Rango vibracion", "Estado", "RTT"):
            for line in block:
                if line.lstrip().startswith(key):
                    parts.append(line.strip())
                    break
        if parts:
            card.set_value("\n".join(parts))
        else:
            card.set_value("Recibido")

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
