#!/usr/bin/env python3
# =============================================================================
# dibuja_esp.py — Genera los dibujos de las placas de Espressif de
#                 doc/analisis_esp32_riscv.md §9 (fase E13):
#                 esp32c3_devkitm1.svg, esp32c3_supermini.svg,
#                 esp32c3_y_f407.svg y esp32c6_devkitc1.svg
#
# Los SVG estan versionados; esto es como se hicieron, para rehacerlos si
# cambia una placa. Una unidad es 0,1 mm. Son aproximaciones dibujadas a mano,
# no fotos ni ilustraciones del fabricante: el orden de los pines de las
# tiras es el de las guias de usuario de Espressif (ESP32-C3-DevKitM-1 y
# ESP32-C6-DevKitC-1 v1.2, tablas de J1 y J3) y el de la serigrafia de la
# SuperMini; las medidas, [de modelo]. Cada pieza visible del XML y cada pin
# de conector llevan su id; los rotulos de los pines salen del <pin nodo=> de
# cada Conector del XML, asi que dicen el nombre del simulador (GPIO8,
# CHIP_PU...). `make gui-sistema`, en su grupo C22, comprueba que no falta
# ninguno.
#
#   python3 placas/dibuja_esp.py [CARPETA]     (desde src/; por omision, placas/)
# =============================================================================
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else AQUI
P = 25.4  # paso de 0,1"

PCB, BORDE_PCB = "#1c1c1c", "#000000"
PCB_AZUL = "#1f3a68"
SERIGRAFIA = "#ffffff"
ORO, ORO_B = "#d4ac0d", "#9a7d0a"
NEGRO = "#1a1a1a"
METAL, METAL_B = "#c9ccd1", "#8e9399"


class Svg:
    def __init__(self, w, h, comentario):
        self.w, self.h = w, h
        self.c = comentario
        self.e = []

    def add(self, s):
        self.e.append(s)

    def rect(self, x, y, w, h, fill, stroke=None, sw=1.5, rx=0, id=None):
        a = ' id="%s"' % id if id else ""
        st = ' stroke="%s" stroke-width="%g"' % (stroke, sw) if stroke else ""
        r = ' rx="%g"' % rx if rx else ""
        self.add('<rect%s x="%.1f" y="%.1f" width="%.1f" height="%.1f"%s fill="%s"%s/>'
                 % (a, x, y, w, h, r, fill, st))

    def circ(self, cx, cy, r, fill, stroke=None, sw=1.5, id=None):
        a = ' id="%s"' % id if id else ""
        st = ' stroke="%s" stroke-width="%g"' % (stroke, sw) if stroke else ""
        self.add('<circle%s cx="%.1f" cy="%.1f" r="%.1f" fill="%s"%s/>' % (a, cx, cy, r, fill, st))

    def text(self, x, y, t, size=14, fill=SERIGRAFIA, anchor="middle", bold=False, rot=None):
        w = ' font-weight="bold"' if bold else ""
        tr = ' transform="rotate(%g %.1f %.1f)"' % (rot, x, y) if rot else ""
        t = t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add('<text x="%.1f" y="%.1f" font-family="sans-serif" font-size="%g" fill="%s" '
                 'text-anchor="%s"%s%s>%s</text>' % (x, y, size, fill, anchor, w, tr, t))

    def abre(self, id=None):
        self.add('<g%s>' % (' id="%s"' % id if id else ""))

    def cierra(self):
        self.add('</g>')

    def guarda(self, nombre):
        with open(os.path.join(OUT, nombre), "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n<!--%s-->\n' % self.c)
            f.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%gmm" height="%gmm">\n'
                    % (self.w, self.h, self.w / 10, self.h / 10))
            for e in self.e:
                f.write("  " + e + "\n")
            f.write("</svg>\n")


def nodos_conector(placa, id_):
    """{nombre de pin: nodo} de un conector de placas/<placa>.xml"""
    with open(os.path.join(AQUI, placa + ".xml"), encoding="utf-8") as f:
        x = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
    m = re.search(r'<componente tipo="Conector" id="%s"[^>]*>(.*?)</componente>' % id_, x, re.S)
    return dict(re.findall(r'<pin nombre="([^"]+)" nodo="([^"]+)"', m.group(1)))


def pin(s, id, x, y, lado=16):
    s.rect(x - lado / 2, y - lado / 2, lado, lado, ORO, ORO_B, 1, id=id)


def tipo_de(n):
    """Los colores del rotulo de un pin, como en dibuja_st.py"""
    if n in ("VDD3P3", "VDDA3P3", "5V"):
        return "#e74c3c", "#ffffff"
    if n == "GND":
        return "#2c3e50", "#ffffff"
    if n in ("CHIP_PU", "GPIO8", "GPIO9", "GPIO15"):        # el reset y los de arranque
        return "#f39c12", "#ffffff"
    if n == "NC":
        return "#bdc3c7", "#566573"
    return "#aed6f1", "#1b2631"


def etiqueta(s, x, y, t, izquierda, ancho=74, alto=19, size=11):
    f, c = tipo_de(t)
    cx = x - ancho / 2 if izquierda else x + ancho / 2
    s.rect(cx - ancho / 2, y - alto / 2, ancho, alto, f, "#ffffff", 1, rx=3)
    s.text(cx, y + size * 0.36, t, size=size, fill=c, bold=True)


def tira(s, placa, id_, x, y0, n, izquierda):
    """Una tira de 1xn en vertical, el pin 1 arriba; los rotulos por fuera"""
    nodos = nodos_conector(placa, id_)
    s.abre(id_)
    s.rect(x - P / 2, y0 - P / 2, P, n * P, NEGRO, "#000000", 1)
    for k in range(n):
        pin(s, "%s.%d" % (id_, k + 1), x, y0 + k * P)
    s.cierra()
    for k in range(n):
        nodo = nodos.get(str(k + 1), "NC")
        etiqueta(s, x - 22 if izquierda else x + 22, y0 + k * P, nodo, izquierda)
    s.text(x, y0 - 22, id_, size=12, bold=True)


def boton(s, id, x, y, tam=50, rotulo=""):
    s.abre(id)
    s.rect(x - tam / 2, y - tam / 2, tam, tam, METAL, METAL_B, 1.5, rx=3)
    s.circ(x, y, tam * 0.3, "#2c3e50")
    s.cierra()
    if rotulo:
        s.text(x, y + tam / 2 + 16, rotulo, size=11)


def led(s, id, x, y, color, tam=24):
    # El elemento de la pieza es el que la ventana enciende (efecto brillo)
    s.rect(x - tam / 2 - 3, y - tam / 2 - 3, tam + 6, tam + 6, "#f4f4f4", "#999999", 1, rx=2)
    s.circ(x, y, tam / 2 - 1, color, id=id)


def usb_c(s, x, y, rotulo, id=None):
    s.abre(id)
    s.rect(x - 45, y - 32, 90, 64, METAL, METAL_B, 2, rx=14)
    s.rect(x - 30, y - 10, 60, 20, "#555555", rx=8)
    s.cierra()
    s.text(x, y - 40, rotulo, size=10)


def chip(s, x, y, w, h, l1, l2=None, id=None, size=13):
    s.rect(x - w / 2, y - h / 2, w, h, "#2b2b2b", "#555555", 1.5, rx=4, id=id)
    s.text(x, y - (6 if l2 else -5), l1, size=size, bold=True, fill="#d0d0d0")
    if l2:
        s.text(x, y + 16, l2, size=size - 2, fill="#bbbbbb")


def modulo(s, x, y, w, h, nombre, antena=True):
    """Un modulo con su blindaje y su antena impresa arriba"""
    if antena:
        s.rect(x, y, w, 70, PCB_AZUL, "#0e1f3a", 1.5)
        for k in range(6):
            s.add('<path d="M %.1f %.1f h %.1f v 30 h %.1f v -30" fill="none" stroke="%s" stroke-width="3"/>'
                  % (x + 20 + k * 30, y + 20, 15, 15, ORO))
        y += 70
        h -= 70
    s.rect(x, y, w, h, METAL, METAL_B, 2, rx=4)
    s.text(x + w / 2, y + 40, nombre, size=14, bold=True, fill="#333333")
    return y


def devkitc1():
    pl = "esp32c6_devkitc1"
    com = """
  ESP32-C6-DevKitC-1 v1.2, dibujada a mano: una aproximacion, no una foto ni
  una ilustracion del fabricante. Es el dibujo de placas/esp32c6_devkitc1.xml,
  y lo hace placas/dibuja_esp.py. Una unidad, 0,1 mm. Las tiras J1 y J3, en
  el orden de la guia de usuario de Espressif; sus rotulos, los nodos del
  simulador. Los ids son los del XML: u0 (el chip, en el modulo), U2 (la
  Flash), LED (el WS2812), BOOT, RESET, UART (el puente USB-UART), Q1 (el
  auto-reset), J1 y J3 con sus pines J1.1-J1.16 y J3.1-J3.16.
"""
    W, H = 600, 660
    s = Svg(W, H, com)
    BX, BY, BW, BH = 150, 20, 300, 620
    s.rect(BX, BY, BW, BH, PCB, BORDE_PCB, 3, rx=12)
    # El modulo, arriba: la Flash dentro del blindaje
    y = modulo(s, BX + 30, BY + 10, BW - 60, 250, "ESP32-C6-WROOM-1")
    chip(s, BX + BW / 2 - 50, y + 110, 90, 70, "ESP32-C6", "u0", id="u0")
    chip(s, BX + BW / 2 + 60, y + 110, 70, 50, "U2", "8 MB", id="U2", size=12)
    # Las tiras
    y0 = BY + 160
    tira(s, pl, "J1", BX + 22, y0, 16, True)
    tira(s, pl, "J3", BX + BW - 22, y0, 16, False)
    # El LED RGB, los botones y los dos USB-C, abajo
    led(s, "LED", BX + BW / 2, BY + 315, "#e0e0e0")
    s.text(BX + BW / 2, BY + 350, "RGB GPIO8", size=11)
    chip(s, BX + BW / 2, BY + 430, 60, 40, "Q1", id="Q1", size=11)
    boton(s, "BOOT", BX + 80, BY + 470, rotulo="BOOT")
    boton(s, "RESET", BX + BW - 80, BY + 470, rotulo="RESET")
    chip(s, BX + BW / 2, BY + 520, 56, 36, "UART", id="UART", size=10)
    usb_c(s, BX + 70, BY + BH - 30, "UART")
    usb_c(s, BX + BW - 70, BY + BH - 30, "USB")
    s.text(BX + BW / 2, BY + 380, "ESP32-C6-DevKitC-1", size=12, bold=True)
    s.guarda(pl + ".svg")


def devkitm1():
    pl = "esp32c3_devkitm1"
    com = """
  ESP32-C3-DevKitM-1, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/esp32c3_devkitm1.xml, y
  lo hace placas/dibuja_esp.py. Una unidad, 0,1 mm. Las tiras J1 y J3, en el
  orden de la guia de usuario de Espressif; sus rotulos, los nodos del
  simulador. Los ids son los del XML: u0 (el chip, en el modulo
  ESP32-C3-MINI-1), LED (el WS2812), BOOT, RESET, UART (el puente USB-UART),
  Q1 (el auto-reset), J1 y J3 con sus pines J1.1-J1.15 y J3.1-J3.15.
"""
    W, H = 600, 600
    s = Svg(W, H, com)
    BX, BY, BW, BH = 150, 20, 300, 560
    s.rect(BX, BY, BW, BH, PCB, BORDE_PCB, 3, rx=12)
    y = modulo(s, BX + 50, BY + 10, BW - 100, 200, "ESP32-C3-MINI-1")
    chip(s, BX + BW / 2, y + 80, 90, 60, "ESP32-C3", "u0", id="u0")
    y0 = BY + 130
    tira(s, pl, "J1", BX + 22, y0, 15, True)
    tira(s, pl, "J3", BX + BW - 22, y0, 15, False)
    led(s, "LED", BX + BW / 2, BY + 270, "#e0e0e0")
    s.text(BX + BW / 2, BY + 305, "RGB GPIO8", size=11)
    chip(s, BX + BW / 2, BY + 360, 60, 40, "Q1", id="Q1", size=11)
    boton(s, "BOOT", BX + 80, BY + 410, rotulo="BOOT")
    boton(s, "RESET", BX + BW - 80, BY + 410, rotulo="RESET")
    chip(s, BX + BW / 2, BY + 455, 56, 36, "UART", id="UART", size=10)
    usb_c(s, BX + BW / 2, BY + BH - 30, "USB (UART)")
    s.guarda(pl + ".svg")


def supermini():
    pl = "esp32c3_supermini"
    com = """
  ESP32-C3 SuperMini, dibujada a mano: una aproximacion, no una foto. Es el
  dibujo de placas/esp32c3_supermini.xml, y lo hace placas/dibuja_esp.py. Una
  unidad, 0,1 mm. Las dos tiras de ocho, J1 y J2, en el orden de la
  serigrafia de la placa (no hay guia de un fabricante); sus rotulos, los
  nodos del simulador. Los ids son los del XML: u0 (el ESP32-C3FH4), D1 (el
  LED azul de GPIO8), BOOT, RESET, UART0 (el puente en RX/TX), J1 y J2 con
  sus pines.
"""
    W, H = 460, 270
    s = Svg(W, H, com)
    BX, BY, BW, BH = 100, 20, 260, 225
    s.rect(BX, BY, BW, BH, PCB_AZUL, "#0e1f3a", 3, rx=10)
    usb_c(s, BX + BW / 2, BY + 30, "USB")
    chip(s, BX + BW / 2, BY + 120, 80, 70, "C3FH4", "u0", id="u0", size=12)
    boton(s, "BOOT", BX + 70, BY + 70, tam=36, rotulo="BOOT")
    boton(s, "RESET", BX + BW - 70, BY + 70, tam=36, rotulo="RST")
    led(s, "D1", BX + BW / 2 - 70, BY + 180, "#2e86c1", tam=16)
    s.text(BX + BW / 2 - 70, BY + 205, "D1", size=10)
    s.rect(BX + BW / 2 + 40, BY + 170, 60, 26, "#2b2b2b", "#555555", 1, rx=3, id="UART0")
    s.text(BX + BW / 2 + 70, BY + 188, "UART0", size=10, fill="#d0d0d0")
    y0 = BY + 30
    tira(s, pl, "J1", BX + 18, y0, 8, True)
    tira(s, pl, "J2", BX + BW - 18, y0, 8, False)
    s.guarda(pl + ".svg")


def c3_y_f407():
    pl = "esp32c3_y_f407"
    com = """
  La placa de placas/esp32c3_y_f407.xml (fase E3): un ESP32-C3 y un
  STM32F407 que hablan por UART. No es una placa que exista; el dibujo, de
  placas/dibuja_esp.py, la pinta como un esquema: los dos chips (c3 y f4), el
  cristal del F407 (X1), las dos lineas de la UART y los dos puentes que
  escuchan (C3 y F4). Una unidad, 0,1 mm.
"""
    W, H = 620, 330
    s = Svg(W, H, com)
    s.rect(10, 10, W - 20, H - 20, "#2e7d32", "#1b5e20", 3, rx=10)
    chip(s, 150, 150, 160, 160, "ESP32-C3", "c3", id="c3", size=16)
    chip(s, 470, 150, 160, 160, "STM32F407", "f4", id="f4", size=16)
    s.abre("X1")
    s.rect(440, 255, 60, 26, METAL, METAL_B, 1.5, rx=13)
    s.cierra()
    s.text(470, 298, "X1 8 MHz", size=11)
    # Las dos lineas
    for y, t, ida in ((120, "GPIO21 (U0TXD) -> PA3", True), (180, "PA2 -> GPIO20 (U0RXD)", False)):
        s.add('<line x1="230" y1="%d" x2="390" y2="%d" stroke="%s" stroke-width="4"/>' % (y, y, ORO))
        s.add('<polygon points="%s" fill="%s"/>' % (
            ("390,%d 378,%d 378,%d" % (y, y - 7, y + 7)) if ida else ("230,%d 242,%d 242,%d" % (y, y - 7, y + 7)),
            ORO))
        s.text(310, y - 10, t, size=11)
    s.rect(270, 230, 34, 22, "#2b2b2b", "#555555", 1, rx=3, id="C3")
    s.text(287, 270, "C3", size=10)
    s.rect(316, 230, 34, 22, "#2b2b2b", "#555555", 1, rx=3, id="F4")
    s.text(333, 270, "F4", size=10)
    s.guarda(pl + ".svg")


devkitc1()
devkitm1()
supermini()
c3_y_f407()
