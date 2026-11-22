#!/usr/bin/env python3
# =============================================================================
# dibuja_ti.py — Genera los dibujos de las placas de TI:
#                msp_exp432p401r.svg, boostxl_edumkii.svg, msp_exp432e401y.svg
#                msp_exp430g2et.svg, msp_exp430f5529lp.svg, msp_exp430fr4133.svg y
#                msp_exp430fr2311.svg, y (M12) msp_exp430fr5969.svg,
#                msp_exp430fr5994.svg y msp_exp430fr6989.svg
#
# Los SVG estan versionados; esto es como se hicieron, para rehacerlos si
# cambia una placa. Una unidad es 0,1 mm. Cada pieza visible del XML y cada
# pin de conector llevan su id, y cada posicion de un jumper es un elemento
# PIEZA@valor dentro de la pieza (doc/parts.md §2.6). `make gui-sistema`, en
# su grupo C19, comprueba que no falta ninguno.
#
#   python3 placas/dibuja_ti.py [CARPETA]     (desde src/; por omision, placas/)
# =============================================================================
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
P = 25.4  # paso de 0,1"

ROJO_PCB, BORDE_PCB = "#c8161d", "#7d0a0e"
SERIGRAFIA = "#ffffff"
ORO, ORO_B = "#d4ac0d", "#9a7d0a"
NEGRO = "#1a1a1a"


class Svg:
    def __init__(self, w, h, comentario):
        self.w, self.h = w, h
        self.c = comentario
        self.e = []

    def add(self, s):
        self.e.append(s)

    def rect(self, x, y, w, h, fill, stroke=None, sw=1.5, rx=0, id=None, extra=""):
        a = ' id="%s"' % id if id else ""
        st = ' stroke="%s" stroke-width="%g"' % (stroke, sw) if stroke else ""
        r = ' rx="%g"' % rx if rx else ""
        self.add('<rect%s x="%.1f" y="%.1f" width="%.1f" height="%.1f"%s fill="%s"%s%s/>'
                 % (a, x, y, w, h, r, fill, st, extra))

    def circ(self, cx, cy, r, fill, stroke=None, sw=1.5, id=None):
        a = ' id="%s"' % id if id else ""
        st = ' stroke="%s" stroke-width="%g"' % (stroke, sw) if stroke else ""
        self.add('<circle%s cx="%.1f" cy="%.1f" r="%.1f" fill="%s"%s/>' % (a, cx, cy, r, fill, st))

    def text(self, x, y, t, size=14, fill=SERIGRAFIA, anchor="middle", bold=False, id=None, rot=None):
        a = ' id="%s"' % id if id else ""
        b = ' font-weight="bold"' if bold else ""
        tr = ' transform="rotate(%g %.1f %.1f)"' % (rot, x, y) if rot else ""
        t = t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add('<text%s x="%.1f" y="%.1f" font-family="Arial" font-size="%g"%s fill="%s" '
                 'text-anchor="%s"%s>%s</text>' % (a, x, y, size, b, fill, anchor, tr, t))

    def line(self, x1, y1, x2, y2, stroke, sw=2, dash=None):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self.add('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%g"%s/>'
                 % (x1, y1, x2, y2, stroke, sw, d))

    def poly(self, pts, fill, stroke=None, sw=1.5, id=None):
        a = ' id="%s"' % id if id else ""
        st = ' stroke="%s" stroke-width="%g"' % (stroke, sw) if stroke else ""
        self.add('<polygon%s points="%s" fill="%s"%s/>' % (a, " ".join("%.1f,%.1f" % p for p in pts), fill, st))

    def abre(self, id=None, transform=None):
        a = ' id="%s"' % id if id else ""
        t = ' transform="%s"' % transform if transform else ""
        self.add("<g%s%s>" % (a, t))

    def cierra(self):
        self.add("</g>")

    def guarda(self, nombre):
        s = ['<?xml version="1.0" encoding="UTF-8"?>', "<!--" + self.c + "-->",
             '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%gmm" height="%gmm">'
             % (self.w, self.h, self.w / 10, self.h / 10)]
        s += ["  " + x for x in self.e]
        s.append("</svg>")
        with open(OUT + "/" + nombre, "w", encoding="utf-8") as f:
            f.write("\n".join(s) + "\n")


# --- Las etiquetas de los pines, con los colores de placas/nucleo_f446re.svg ---
def tipo_de(n):
    if n in ("3V3", "5V", "+3.3V", "+5V", "VDD", "DVCC"):
        return "#e74c3c", "#ffffff"
    if n in ("GND", "DVSS"):
        return "#2c3e50", "#ffffff"
    if n in ("RST", "RESET", "WAKE"):
        return "#f39c12", "#ffffff"
    if n in ("-", "NC", "VREF"):
        return "#bdc3c7", "#566573"
    return "#aed6f1", "#1b2631"


def etiqueta(s, x, y, n, ancho=78, alto=19, size=13):
    """Una etiqueta de pin centrada en (x, y)"""
    f, t = tipo_de(n)
    s.rect(x - ancho / 2, y - alto / 2, ancho, alto, f, "#ffffff", 1, rx=3)
    s.text(x, y + size * 0.36, n, size=size, fill=t, bold=True)


def pin(s, id, x, y, lado=16):
    s.rect(x - lado / 2, y - lado / 2, lado, lado, ORO, ORO_B, 1, id=id)


def tira_vertical(s, id, x, y0, ids, ancho_cuerpo=P):
    """Una tira de pines en columna: ids de arriba abajo"""
    s.abre(id)
    s.rect(x - ancho_cuerpo / 2, y0 - P / 2, ancho_cuerpo, P * len(ids), NEGRO, "#000000", 1)
    for k, pid in enumerate(ids):
        pin(s, pid, x, y0 + k * P)
    s.cierra()


def tira_horizontal(s, id, x0, y, ids):
    s.abre(id)
    s.rect(x0 - P / 2, y - P / 2, P * len(ids), P, NEGRO, "#000000", 1)
    for k, pid in enumerate(ids):
        pin(s, pid, x0 + k * P, y)
    s.cierra()


def jumper(s, id, pines, variantes, cuerpo=True):
    """pines: [(pin_id, x, y)]; variantes: {valor: [(x1, y1, x2, y2), ...]}
    Cada variante es un puente (o varios) dibujado DENTRO del elemento"""
    s.abre(id)
    if cuerpo:
        xs = [p[1] for p in pines]
        ys = [p[2] for p in pines]
        s.rect(min(xs) - P / 2, min(ys) - P / 2, max(xs) - min(xs) + P, max(ys) - min(ys) + P,
               NEGRO, "#000000", 1)
    for pid, x, y in pines:
        pin(s, pid, x, y, 14)
    for v, puentes in variantes.items():
        s.abre("%s@%s" % (id, v))
        for (x1, y1, x2, y2) in puentes:
            xa, xb = min(x1, x2), max(x1, x2)
            ya, yb = min(y1, y2), max(y1, y2)
            s.rect(xa - 10, ya - 10, xb - xa + 20, yb - ya + 20, "#2b2b2b", "#000000", 1.5, rx=4)
            s.rect(xa - 5, ya - 5, xb - xa + 10, yb - ya + 10, "#4a4a4a", None, rx=2)
        s.cierra()
    s.cierra()


def boton(s, id, x, y, tam=60, color="#2c3e50"):
    s.abre(id)
    s.rect(x - tam / 2, y - tam / 2, tam, tam, "#bdc3c7", "#7f8c8d", 2, rx=5)
    s.circ(x, y, tam * 0.33, color, "#000000", 1.5)
    s.cierra()


def led_rgb(s, id, x, y, r=14):
    """Un LED RGB: un encapsulado transparente con los tres chips dentro. El
    elemento es el encapsulado entero: la ventana pinta encima la mezcla"""
    s.abre(id)
    s.rect(x - r * 1.4, y - r * 1.4, r * 2.8, r * 2.8, "#f5f5f5", "#999999", 1, rx=3)
    s.circ(x, y, r, "#eef2f3", "#9aa5a8", 1.2)
    for dx, dy, c in ((-0.38, -0.3, "#e74c3c"), (0.38, -0.3, "#2ecc71"), (0, 0.4, "#3498db")):
        s.rect(x + dx * r - r * 0.17, y + dy * r - r * 0.17, r * 0.34, r * 0.34, c, None)
    s.cierra()


def led(s, id, x, y, color, r=10):
    s.rect(x - r * 1.3, y - r * 0.9, r * 2.6, r * 1.8, "#f5f5f5", "#999999", 1, rx=2)
    s.circ(x, y, r * 0.75, color, "#555555", 1.2, id=id)


def cristal(s, id, x, y, w, h, texto):
    s.abre(id)
    s.rect(x, y, w, h, "#d0d3d4", "#7f8c8d", 1.5, rx=h / 2)
    s.cierra()
    s.text(x + w / 2, y + h + 16, texto, size=12)


def chip(s, x, y, w, h, l1, l2=None, rot=0, id=None):
    cx, cy = x + w / 2, y + h / 2
    if rot:
        s.abre(id, "rotate(%g %.1f %.1f)" % (rot, cx, cy))
    elif id:
        s.abre(id)
    s.rect(x, y, w, h, "#2c2c2c", "#000000", 1.5, rx=3)
    s.circ(x + 12, y + 12, 4, "#7f8c8d")
    if rot or id:
        s.cierra()
    s.text(cx, cy + (0 if l2 else 5), l1, size=14, fill="#ffffff")
    if l2:
        s.text(cx, cy + 18, l2, size=11, fill="#bdc3c7")


def agujero(s, x, y, r=16):
    s.circ(x, y, r, "#ffffff", "#7d0a0e", 2)


def usb_micro(s, x, y, ancho=90, alto=55, id=None):
    if id:
        s.abre(id)
    s.rect(x - ancho / 2, y, ancho, alto, "#95a5a6", "#7f8c8d", 2, rx=5)
    s.rect(x - ancho / 2 + 12, y + 10, ancho - 24, 12, "#5d6d7e", None, rx=2)
    if id:
        s.cierra()


# =============================================================================
# MSP-EXP432P401R
# =============================================================================
def p401r():
    com = """
  MSP-EXP432P401R, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp432p401r.xml,
  que lo encuentra solo porque se llama como ella (doc/parts.md, "El
  dibujo"). La disposicion es la de la figura 2 de SLAU597F: el XDS110 y el
  USB arriba, el bloque de aislamiento J101 de lado a lado, S1 y S2 en los
  bordes, las cuatro tiras BoosterPack con el chip en medio, los LED abajo y
  el conector del borde, J5, al pie.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, una unidad es 0,1 mm -la placa mide
  unos 64 x 98 mm, y a los lados quedan los rotulos- y cada pieza viva con el
  id de su pieza en el XML: LED1, LED2 (el RGB, un RGBLed: la ventana pinta
  encima la mezcla de sus tres canales), S1, S2, el RESET S3, los
  cristales Y1 e Y2, el USB, el backchannel BCL y los conectores J1-J7, cada
  pin con su id (J1.1 ... J2.10), para que los hilos y los acoples de un
  sistema lleguen a SU pin. Las tiras BoosterPack van como en la
  serigrafia: J1 (1-10) y J3 (21-30) a la izquierda, J4 (40-31) y J2 (20-11)
  a la derecha, de arriba abajo; el pin k de J2 es el 10+k, y J2.10 (el 20,
  GND) es el de arriba. Los rotulos, con los colores de los pinouts: el pin
  del chip en azul, la alimentacion en rojo, la masa en negro y RST en
  naranja.

  Los JUMPERS ensenan su puente: J101 (diez, todos puestos), JP8-JP11 (los
  de los LED), cada posicion como un elemento PIEZA@valor dentro de la
  pieza; mcu-sim deja la que dice el XML (src/common/svg_variantes.h).

  BCL#destino es un rotulo: mcu-sim escribe ahi el destino de verdad del
  backchannel al mandar el dibujo.
"""
    W, H = 1100, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 232, 30, 636, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    # El USB del XDS110, arriba
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (XDS110)", size=12)
    # El XDS110
    chip(s, cx - 175, by + 90, 110, 110, "XDS110", "-ET", id="XDS")
    s.text(cx - 120, by + 222, "XDS110-ET (sonda)", size=12)
    s.circ(cx - 10, by + 110, 8, "#e74c3c", "#7f8c8d", 1)
    s.circ(cx + 15, by + 110, 8, "#2ecc71", "#7f8c8d", 1)
    s.text(cx + 2, by + 95, "LED101 LED102", size=10)
    s.rect(cx + 40, by + 90, 70, 50, "#2c2c2c", "#000", 1, rx=2)
    s.text(cx + 75, by + 155, "EnergyTrace", size=11)
    # RESET S3, arriba a la derecha
    boton(s, "S3", bx + bw - 95, by + 75, 56)
    s.text(bx + bw - 95, by + 125, "RESET (S3)", size=12)
    # El backchannel
    s.abre("BCL")
    s.rect(cx + 100, by + 165, 200, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 200, by + 185, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 200, by + 203, "eUSCI_A0 (P1.2, P1.3)", size=11, fill="#7e5109")
    s.text(cx + 200, by + 225, "RFC 2217 en localhost:3356", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    # J101, el bloque de aislamiento: 10 columnas, pares arriba (XDS110) e
    # impares abajo (el chip)
    y_par, y_impar = by + 290, by + 290 + P
    x0 = cx - 4.5 * P
    pines = []
    puentes = []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "RST", "TMS", "TCK", "TDO", "TDI"]
    for k in range(10):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(10)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    s.text(bx + 12, y_par - 2, "XDS110", size=11, anchor="start")
    s.text(bx + 12, y_impar + 22, "MSP432P401R", size=11, anchor="start")
    # S1 y S2, en los bordes
    boton(s, "S1", bx + 28, by + 395, 52)
    s.text(bx + 62, by + 400, "S1", size=13, anchor="start")
    boton(s, "S2", bx + bw - 28, by + 395, 52)
    s.text(bx + bw - 62, by + 400, "S2", size=13, anchor="end")
    # Las BoosterPack: J1 y J2 a 45,72 mm (1,8")
    yb = by + 455
    xj1 = cx - 228.6
    xj3 = xj1 + P
    xj2 = cx + 228.6
    xj4 = xj2 - P
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J3", xj3, yb, ["J3.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J4", xj4, yb, ["J4.%d" % k for k in range(10, 0, -1)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj3, "21"), (xj4, "40"), (xj2, "20")):
        s.text(x, yb - 22, t, size=12, bold=True)
    s.text(xj1, yb + 10 * P + 8, "J1", size=12)
    s.text(xj3, yb + 10 * P + 8, "J3", size=12)
    s.text(xj4, yb + 10 * P + 8, "J4", size=12)
    s.text(xj2, yb + 10 * P + 8, "J2", size=12)
    j1 = ["3V3", "P6.0", "P3.2", "P3.3", "P4.1", "P4.3", "P1.5", "P4.6", "P6.5", "P6.4"]
    j3 = ["5V", "GND", "P6.1", "P4.0", "P4.2", "P4.4", "P4.5", "P4.7", "P5.4", "P5.5"]
    j4 = ["P2.7", "P2.6", "P2.4", "P5.6", "P6.6", "P6.7", "P2.3", "P5.1", "P3.5", "P3.7"]
    j2 = ["GND", "P2.5", "P3.0", "P5.7", "RST", "P1.6", "P1.7", "P5.0", "P5.2", "P3.6"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, xj3 + 52, y, j3[k], 64, 18, 12)
        etiqueta(s, xj4 - 52, y, j4[k], 64, 18, 12)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    # El chip, en medio
    chip(s, cx - 60, yb + 50, 120, 120, "MSP432", "P401R", rot=0)
    s.text(cx, yb + 195, "MSP1", size=11)
    cristal(s, "Y2", cx - 45, yb + 10, 90, 22, "Y2 48 MHz")
    cristal(s, "Y1", cx + 70, yb + 140, 22, 40, "")
    s.text(cx + 81, yb + 198, "Y1", size=11)
    s.text(cx + 81, yb + 210, "32 kHz", size=9)
    # J6 (3V3) y J7 (5V): tiras de tres
    yj = by + 770
    tira_vertical(s, "J6", bx + 40, yj, ["J6.3V3", "J6.GND1", "J6.GND2"])
    s.text(bx + 62, yj + 4, "3V3", size=11, anchor="start")
    s.text(bx + 62, yj + P + 4, "GND", size=11, anchor="start")
    s.text(bx + 62, yj + 2 * P + 4, "GND", size=11, anchor="start")
    s.text(bx + 40, yj - 20, "J6", size=12)
    tira_vertical(s, "J7", bx + bw - 40, yj, ["J7.5V", "J7.GND1", "J7.GND2"])
    s.text(bx + bw - 62, yj + 4, "5V", size=11, anchor="end")
    s.text(bx + bw - 62, yj + P + 4, "GND", size=11, anchor="end")
    s.text(bx + bw - 62, yj + 2 * P + 4, "GND", size=11, anchor="end")
    s.text(bx + bw - 40, yj - 20, "J7", size=12)
    # Los LED y sus jumpers
    yl = by + 760
    for k, (jp, nom) in enumerate((("JP8", "LED1"), ("JP9", "R"), ("JP10", "G"), ("JP11", "B"))):
        x = cx - 150 + k * 70 + (30 if k else 0)
        jumper(s, jp, [(jp + ".1", x, yl - 12), (jp + ".2", x, yl - 12 + P)],
               {"1-2": [(x, yl - 12, x, yl - 12 + P)]})
        s.text(x, yl - 40, jp, size=10)
        if k:
            s.text(x + 18, yl + 10, nom, size=10, anchor="start")
    led(s, "LED1", cx - 150, yl + 58, "#e74c3c", 11)
    s.text(cx - 150, yl + 88, "LED1 (rojo)", size=11)
    led_rgb(s, "LED2", cx + 50, yl + 62, 15)
    s.text(cx + 50, yl + 100, "LED2 (RGB)", size=11)
    # J5, el conector del borde: 2x19, impares arriba
    yj5 = by + bh - 75
    x5 = cx - 9 * P
    s.abre("J5")
    s.rect(x5 - P / 2, yj5 - P / 2, 19 * P, 2 * P, NEGRO, "#000", 1)
    for k in range(19):
        pin(s, "J5.%d" % (2 * k + 1), x5 + k * P, yj5)
        pin(s, "J5.%d" % (2 * k + 2), x5 + k * P, yj5 + P)
    s.cierra()
    s.text(x5 - 22, yj5 + 18, "J5", size=12, anchor="end")
    s.text(x5, yj5 - 20, "1", size=10)
    s.text(x5 + 18 * P, yj5 - 20, "37", size=10)
    s.text(cx, yj5 + 2 * P + 14, "Pines sin usar (P7, P8, P9, P10)", size=11)
    # Rotulos de la placa
    s.text(cx + 75, by + 52, "MSP-EXP432P401R", size=20, bold=True)
    s.text(cx + 75, by + 72, "Texas Instruments", size=12)
    s.guarda("msp_exp432p401r.svg")


# =============================================================================
# BOOSTXL-EDUMKII
# =============================================================================
def edumkii():
    com = """
  BOOSTXL-EDUMKII, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/boostxl_edumkii.xml.
  La disposicion es la de la figura 2-1 de SLAU599B: la placa en forma de
  pastilla, el joystick a la izquierda, la pantalla entre las dos parejas de
  tiras, y a la derecha el servo, el acelerometro, los pulsadores, el RGB y
  el zumbador.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm -unos 110 x 52 mm- y cada pieza viva con su
  id del XML: JOY_SEL (la palanca, que se aprieta), JOY_X y JOY_Y (las
  flechas de cada eje), MIC, ACC_X/Y/Z, S1, S2, D1 (el RGB, un RGBLed), BUZ, OPT3001,
  TMP006, D3 y D5 (los de alimentacion), y LCD: la ZONA ACTIVA del vidrio,
  de 128 de ancho por 160 de alto como el Tft128x160 del XML -el de la
  placa es de 128x128, y las 32 filas de abajo no tienen vidrio-. Los
  conectores J1-J4, J8 (el servo) y GATOR (las pinzas), cada pin con su id;
  las tiras, como en la serigrafia: J1 (1-10) y J3 (21-30) a la izquierda, J4
  (40-31) y J2 (20-11) a la derecha.

  J5 ENSENA SU PUENTE: J5@RGB-PWM (de fabrica, J4.39 al rojo del RGB) y
  J5@PWM-LCD (a la luz de la pantalla); mcu-sim deja el que diga el XML o el
  <jumper> del sistema.
"""
    W, H = 1300, 620
    s = Svg(W, H, com)
    bx, by, bw, bh = 100, 50, 1100, 520
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=bh / 2)
    # Las tiras: 1,8" entre J1 y J2
    yb = by + 140
    xj1, xj2 = cx - 228.6, cx + 228.6
    xj3, xj4 = xj1 + P, xj2 - P
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J3", xj3, yb, ["J3.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J4", xj4, yb, ["J4.%d" % k for k in range(10, 0, -1)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj3, "21"), (xj4, "40"), (xj2, "20")):
        s.text(x, yb - 20, t, size=12, bold=True)
    # La pantalla, entre las tiras: el marco y la zona activa (128 x 160)
    s.rect(cx - 160, by + 40, 320, 440, "#f2f3f4", "#909497", 2, rx=6)
    s.rect(cx - 145, by + 60, 290, 360, "#111418", "#000", 1.5, rx=3)
    s.rect(cx - 128, by + 75, 256, 320, "#2a3036", "#0b0c0e", 1, id="LCD")
    s.rect(cx - 100, by + 435, 200, 28, "#b7950b", "#7d6608", 1)
    s.text(cx, by + 430, "LCD1 128x128", size=11, fill="#566573")
    # El joystick, a la izquierda
    jx, jy = bx + 150, by + 245
    s.circ(jx, jy, 125, "#f4f6f6", "#bdc3c7", 3)
    s.abre("JOY_X")
    s.poly([(jx - 110, jy), (jx - 90, jy - 20), (jx - 90, jy + 20)], "#ffffff", "#7f8c8d", 1.5)
    s.poly([(jx + 110, jy), (jx + 90, jy - 20), (jx + 90, jy + 20)], "#ffffff", "#7f8c8d", 1.5)
    s.cierra()
    s.abre("JOY_Y")
    s.poly([(jx, jy - 110), (jx - 20, jy - 90), (jx + 20, jy - 90)], "#ffffff", "#7f8c8d", 1.5)
    s.poly([(jx, jy + 110), (jx - 20, jy + 90), (jx + 20, jy + 90)], "#ffffff", "#7f8c8d", 1.5)
    s.cierra()
    s.abre("JOY_SEL")
    s.circ(jx, jy, 75, "#1c1c1c", "#000", 2)
    s.circ(jx, jy, 52, "#2d2d2d", "#555", 2)
    s.cierra()
    s.text(jx + 20, jy + 160, "JOYSTICK", size=12, bold=True)
    s.text(jx + 20, jy + 178, "X J1.2, Y J3.26, SEL J1.5", size=10)
    # El microfono, abajo a la izquierda de las tiras
    s.circ(xj1 - 55, by + 440, 30, "#2c2c2c", "#000", 2, id="MIC")
    s.text(xj1 - 55, by + 488, "MIC", size=11)
    # J5, el jumper de J4.39, arriba a la izquierda
    jx5, jy5 = xj1 - 75, by + 55
    pines = [("J5.RGB", jx5, jy5), ("J5.PWM", jx5, jy5 + P), ("J5.LCD", jx5, jy5 + 2 * P)]
    jumper(s, "J5", pines, {"RGB-PWM": [(jx5, jy5, jx5, jy5 + P)],
                            "PWM-LCD": [(jx5, jy5 + P, jx5, jy5 + 2 * P)]})
    s.text(jx5 - 22, jy5 + 4, "RGB", size=10, anchor="end")
    s.text(jx5 - 22, jy5 + P + 4, "J4.39", size=10, anchor="end")
    s.text(jx5 - 22, jy5 + 2 * P + 4, "LCD", size=10, anchor="end")
    s.text(jx5 + 22, jy5 + P + 4, "J5", size=12, anchor="start", bold=True)
    # Las pinzas: G2 (masa) a la izquierda y G1 (J4.34) a la derecha
    s.abre("GATOR")
    for pid, x in (("GATOR.G2", xj1 + 5), ("GATOR.G1", xj2 - 5)):
        s.abre(pid)
        s.rect(x - 34, by + 4, 68, 40, "#c8a951", "#8a7330", 2, rx=10)
        s.circ(x - 14, by + 24, 9, "#7b6a3c")
        s.circ(x + 14, by + 24, 9, "#7b6a3c")
        s.cierra()
    s.cierra()
    s.text(xj1 + 5, by + 60, "GND", size=10)
    s.text(xj2 - 5, by + 60, "J4.34", size=10)
    # Los dos sensores I2C, arriba en medio
    s.rect(cx - 110, by + 8, 28, 28, "#2c2c2c", "#000", 1, id="OPT3001")
    s.rect(cx + 82, by + 8, 28, 28, "#2c2c2c", "#000", 1, id="TMP006")
    s.text(cx - 125, by + 27, "OPT3001", size=10, anchor="end")
    s.text(cx + 125, by + 27, "TMP006", size=10, anchor="start")
    # A la derecha: el servo, el acelerometro, los pulsadores, el RGB y el
    # zumbador
    rx0 = xj2 + 85
    jumper_like = [("J8.GND", rx0, by + 70), ("J8.PWR", rx0, by + 70 + P), ("J8.SIG", rx0, by + 70 + 2 * P)]
    s.abre("J8")
    s.rect(rx0 - P / 2, by + 70 - P / 2, P, 3 * P, NEGRO, "#000", 1)
    for pid, x, y in jumper_like:
        pin(s, pid, x, y, 14)
    s.cierra()
    for k, t in enumerate(("GND", "PWR", "SIG")):
        s.text(rx0 + 20, by + 74 + k * P, t, size=9, anchor="start")
    s.text(rx0, by + 50, "SERVO (J8)", size=10)
    # El acelerometro: tres ejes
    ax, ay = rx0 - 20, by + 175
    s.rect(ax - 10, ay - 10, 92, 46, "#2c2c2c", "#000", 1, rx=3)
    for k, e in enumerate("XYZ"):
        s.rect(ax + k * 28 - 2, ay - 2, 20, 30, "#3d3d3d", "#777", 1, id="ACC_" + e)
        s.text(ax + k * 28 + 8, ay + 19, e, size=10)
    s.text(ax + 36, ay + 52, "ACELEROMETRO", size=9)
    # Los pulsadores, en rombo
    for pid, y in (("S1", by + 260), ("S2", by + 350)):
        x = bx + bw - 120
        s.abre(pid)
        s.poly([(x, y - 42), (x + 42, y), (x, y + 42), (x - 42, y)], "#1c1c1c", "#000", 2)
        s.circ(x, y, 20, "#bdc3c7", "#7f8c8d", 2)
        s.cierra()
        s.text(x - 55, y + 5, pid, size=12, anchor="end")
    # El RGB: los tres colores
    lx, ly = rx0 + 5, by + 300
    led_rgb(s, "D1", lx, ly, 17)
    s.text(lx, ly + 44, "RGB (D1)", size=10)
    # El zumbador
    s.abre("BUZ")
    s.circ(rx0 + 10, by + 420, 48, "#1c1c1c", "#000", 2)
    s.circ(rx0 + 10, by + 420, 10, "#ffffff")
    s.cierra()
    s.text(rx0 + 75, by + 455, "BUZ", size=11, anchor="start")
    # Los LED de alimentacion, abajo en medio
    led(s, "D5", cx - 205, by + bh - 35, "#2ecc71", 8)
    s.text(cx - 205, by + bh - 50, "5V", size=10)
    led(s, "D3", cx + 205, by + bh - 35, "#2ecc71", 8)
    s.text(cx + 205, by + bh - 50, "3V3", size=10)
    s.text(cx, by + bh - 20, "BOOSTXL-EDUMKII", size=16, bold=True)
    s.guarda("boostxl_edumkii.svg")


# =============================================================================
# MSP-EXP432E401Y
# =============================================================================
def e401y():
    com = """
  MSP-EXP432E401Y, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp432e401y.xml.
  La disposicion y la serigrafia son las de la figura 3 de SLAU748B (los
  jumpers de fabrica): de arriba abajo, el USB y el XDS110, el bloque J101,
  la BoosterPack 1 (J1-J4), los jumpers JP1, JP4 y JP5, la BoosterPack 2
  (J5-J8) con el chip, y abajo el RJ45 y el USB del chip; a la derecha, los
  98 agujeros de J10.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm -la placa mide 63,5 x 174 mm- y cada pieza
  viva con su id del XML: D1-D4, USR_SW1, USR_SW2, RESET, WAKE_SW, Y1, Y3,
  USB, BCL, y los conectores J1-J8, J10, J12 y J13, cada pin con su id. Las
  tiras van como la serigrafia: J1 (1-10) y J3 (21-30) a la izquierda, J4
  (40-31) y J2 (20-11) a la derecha, de arriba abajo; igual J5, J7, J8 y J6.

  LOS JUMPERS ENSENAN SU PUENTE: J101 (diez), JP1 (de donde salen los 5 V:
  5-6 es el XDS110), JP4 y JP5 (1-2+3-4 en vertical, la UART0 al XDS110; o
  1-3+2-4 en horizontal, el CAN0 a la BoosterPack 2), JP6 y JP7.

  BCL#destino es un rotulo: mcu-sim escribe ahi el destino de verdad del
  backchannel al mandar el dibujo.
"""
    W, H = 1100, 1840
    s = Svg(W, H, com)
    bx, by, bw, bh = 230, 50, 635, 1740
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=15)
    for x, y in ((bx + 25, by + 25), (bx + bw - 25, by + 25)):
        agujero(s, x, y, 14)
    X = lambda u: bx + u   # posiciones de la figura 3, en unidades desde la esquina
    Y = lambda u: by + u
    # Arriba: el USB del XDS110 y RESET
    usb_micro(s, X(126), Y(-22), id="USB")
    s.text(X(126), Y(55), "USB (XDS110)", size=12)
    boton(s, "RESET", X(444), Y(30), 48)
    s.text(X(444), Y(72), "RESET", size=12)
    chip(s, X(55), Y(140), 140, 140, "XDS110", id="XDS")
    s.circ(X(230), Y(110), 7, "#e74c3c", "#7f8c8d", 1)
    s.circ(X(255), Y(110), 7, "#2ecc71", "#7f8c8d", 1)
    s.text(X(242), Y(95), "LED101 LED102", size=10)
    # El backchannel
    s.abre("BCL")
    s.rect(X(225), Y(150), 190, 90, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(X(320), Y(172), "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(X(320), Y(190), "UART0 (PA0, PA1) por JP4/JP5", size=10, fill="#7e5109")
    s.text(X(320), Y(214), "RFC 2217 en localhost:3357", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    s.text(X(320), Y(232), "XDS110 Class Application/User UART", size=8, fill="#7e5109")
    # TI y el nombre
    s.text(X(470), Y(132), "TEXAS INSTRUMENTS", size=15, bold=True)
    s.text(X(560), Y(260), "MSP-EXP432E401Y", size=16, rot=90, anchor="start")
    # J101: diez columnas, pares (XDS110) arriba, impares (el chip) abajo
    y_par, y_impar = Y(380), Y(380) + P
    x0 = X(100)
    pines, puentes = [], []
    for k in range(10):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(10)): puentes})
    for k, n in enumerate(["GND", "5V", "3V3", "RXD<<", "TXD>>", "RST", "TMS", "TCK", "TDO", "TDI"]):
        s.text(x0 + k * P, y_impar + 26, n, size=9, rot=-90, anchor="end")
    s.text(x0 + 10 * P, y_par + 8, "J101", size=12, anchor="start", bold=True)
    s.text(X(20), Y(370), "XDS110", size=11, anchor="start")
    s.line(bx, y_par + P / 2, X(570), y_par + P / 2, "#ffffff", 2, "8 6")
    # Los pulsadores de usuario, en los bordes
    boton(s, "USR_SW1", X(22), Y(470), 44)
    s.text(X(48), Y(462), "USR_SW1", size=11, anchor="start")
    boton(s, "USR_SW2", X(560), Y(470), 44)
    s.text(X(534), Y(462), "USR_SW2", size=11, anchor="end")
    # Las dos BoosterPack: J1 en x=60 y J2 a 1,8"
    def booster(y0, ids, labs, nombres):
        xj1 = X(60)
        xj2 = xj1 + 457.2
        xj3, xj4 = xj1 + P, xj2 - P
        a, c, d, b = ids
        tira_vertical(s, a, xj1, y0, ["%s.%d" % (a, k) for k in range(1, 11)])
        tira_vertical(s, c, xj3, y0, ["%s.%d" % (c, k) for k in range(1, 11)])
        tira_vertical(s, d, xj4, y0, ["%s.%d" % (d, k) for k in range(10, 0, -1)])
        tira_vertical(s, b, xj2, y0, ["%s.%d" % (b, k) for k in range(10, 0, -1)])
        for x, t, n in ((xj1, "1", a), (xj3, "21", c), (xj4, "40", d), (xj2, "20", b)):
            s.text(x, y0 - 20, t, size=11, bold=True)
            s.text(x, y0 + 10 * P + 6, n, size=11)
        la, lc, ld, lb = labs
        for k in range(10):
            y = y0 + k * P
            etiqueta(s, bx - 48, y, la[k], 70)
            etiqueta(s, xj3 + 46, y, lc[k], 56, 17, 11)
            etiqueta(s, xj4 - 46, y, ld[k], 56, 17, 11)
            etiqueta(s, bx + bw + 48, y, lb[k], 70)
        s.text((xj3 + xj4) / 2, y0 + 10 * P + 6, nombres, size=12)
    booster(Y(508), ("J1", "J3", "J4", "J2"),
            (["3V3", "PE4", "PC4", "PC5", "PC6", "PE5", "PD3", "PC7", "PB2", "PB3"],
             ["5V", "GND", "PE0", "PE1", "PE2", "PE3", "PD7", "PD6", "PM4", "PM5"],
             ["PF1", "PF2", "PF3", "PG0", "PL4", "PL5", "PL0", "PL1", "PL2", "PL3"],
             ["GND", "PM3", "PH2", "PH3", "RST", "PD1", "PD0", "PN2", "PN3", "PP2"]),
            "BoosterPack 1")
    booster(Y(1146), ("J5", "J7", "J8", "J6"),
            (["3V3", "PD2", "PP0", "PP1", "*RX", "*TX", "PQ0", "PP4", "PN5", "PN4"],
             ["5V", "GND", "PB4", "PB5", "PK0", "PK1", "PK2", "PK3", "PA4", "PA5"],
             ["PG1", "PK4", "PK5", "PM0", "PM1", "PM2", "PH0", "PH1", "PK6", "PK7"],
             ["GND", "PM7", "PP5", "PA7", "RST", "PQ2", "PQ3", "PP3", "PQ1", "PM6"]),
            "BoosterPack 2")
    # El cabezal del BSL, de adorno
    s.rect(X(165), Y(555), 200, 85, "none", "#ffffff", 2)
    for k in range(5):
        s.circ(X(205) + k * 30, Y(585), 9, "none", "#ffffff", 2)
        s.circ(X(205) + k * 30, Y(612), 9, "none", "#ffffff", 2)
    s.text(X(170), Y(548), "BSL", size=11, anchor="start")
    # J13 (5V) a la izquierda y J12 (3V3) a la derecha: GND, GND y la tension
    def tira3(id, x, nombres, textos, lado):
        tira_vertical(s, id, x, Y(788), ["%s.%s" % (id, n) for n in nombres])
        for k, t in enumerate(textos):
            s.text(x + lado * 22, Y(792) + k * P, t, size=10, anchor="start" if lado > 0 else "end")
        s.text(x, Y(788) + 3 * P + 4, id, size=11)
    tira3("J13", X(60), ["GND2", "GND1", "5V"], ["GND", "GND", "5V"], 1)
    tira3("J12", X(517), ["GND2", "GND1", "3V3"], ["GND", "GND", "3V3"], -1)
    # JP2, de adorno (la medida de corriente del chip)
    s.rect(X(356), Y(727), 20, 46, NEGRO, "#000", 1)
    s.rect(X(358), Y(729), 16, 42, "#4a4a4a", None, rx=2)
    s.text(X(390), Y(755), "JP2", size=10, anchor="start")
    # JP6 (PQ4) y JP7 (PD6)
    for jp, x, n in (("JP6", X(288), "PQ4"), ("JP7", X(339), "PD6")):
        jumper(s, jp, [(jp + ".1", x, Y(865)), (jp + ".2", x, Y(865) + P)], {"1-2": [(x, Y(865), x, Y(865) + P)]})
        s.text(x, Y(848), n, size=10)
        s.text(x, Y(865) + P + 26, jp, size=10)
    # JP1: 2 filas x 3 columnas; cada columna, una fuente
    xs = [X(161), X(187), X(213)]
    ya, yb2 = Y(992), Y(1018)
    pines = []
    for c in range(3):
        pines.append(("JP1.%d" % (2 * c + 1), xs[c], ya))
        pines.append(("JP1.%d" % (2 * c + 2), xs[c], yb2))
    jumper(s, "JP1", pines, {"%d-%d" % (2 * c + 1, 2 * c + 2): [(xs[c], ya, xs[c], yb2)] for c in range(3)})
    s.text(X(150), Y(978), "JP1", size=10, anchor="start")
    for k, t in enumerate(("5V-EXT", "5V-OTG", "5V-XDS")):
        s.text(X(140), Y(990) + k * 26, t, size=11, anchor="end", bold=True)
    # JP5 y JP4, 2x2: los impares arriba y los pares abajo
    for jp, xa in (("JP5", X(262)), ("JP4", X(340))):
        xb2 = xa + P
        pines = [(jp + ".1", xa, ya), (jp + ".2", xa, yb2), (jp + ".3", xb2, ya), (jp + ".4", xb2, yb2)]
        jumper(s, jp, pines, {"1-2+3-4": [(xa, ya, xa, yb2), (xb2, ya, xb2, yb2)],
                              "1-3+2-4": [(xa, ya, xb2, ya), (xa, yb2, xb2, yb2)]})
        s.text(xa + P / 2, Y(975), jp, size=10)
    # La leyenda de JP4/JP5
    lx, ly = X(400), Y(980)
    s.rect(lx, ly, 170, 100, "none", "#ffffff", 1.5)
    s.text(lx + 85, ly + 18, "XDS110 | J5 *RX *TX", size=10)
    s.text(lx + 85, ly + 50, "|| UART0 | PD4 PD5", size=10)
    s.text(lx + 85, ly + 80, "= UART2 | PA0 PA1", size=10)
    # El chip, girado 45 grados, y los cristales
    chip(s, X(240), Y(1460), 140, 140, "MSP432", "E401Y", rot=45)
    s.text(X(190), Y(1440), "U1", size=11)
    cristal(s, "Y1", X(410), Y(1450), 50, 22, "Y1 25 MHz")
    cristal(s, "Y3", X(424), Y(1520), 22, 60, "")
    s.text(X(455), Y(1555), "Y3 32 kHz", size=10, anchor="start")
    # Los LED de usuario y WAKE, abajo a la derecha
    for k, (d, n) in enumerate((("D4", "PF0"), ("D3", "PF4"), ("D2", "PN0"), ("D1", "PN1"))):
        y = Y(1425) + k * 26
        led(s, d, X(545), y, "#2ecc71", 9)
        s.text(X(520), y + 4, d, size=10, anchor="end")
    boton(s, "WAKE_SW", X(480), Y(1700), 44)
    s.text(X(480), Y(1660), "WAKE", size=11)
    # El RJ45 y el USB del chip, de adorno
    s.rect(X(15), Y(1610), 180, 155, "#bdc3c7", "#7f8c8d", 3, rx=4)
    s.rect(X(45), Y(1645), 120, 65, "#7f8c8d", None, rx=3)
    s.text(X(105), Y(1600), "Ethernet (eth=)", size=11)
    usb_micro(s, X(345), Y(1690), 95, 55)
    s.text(X(345), Y(1680), "USB0 (usb=)", size=11)
    # J10, los 98 agujeros del borde derecho: impares a la izquierda
    xi, xp = X(594), X(619)
    s.abre("J10")
    for k in range(49):
        y = Y(508) + k * 25.5
        s.circ(xi, y, 8, ORO, ORO_B, 1, id="J10.%d" % (2 * k + 1))
        s.circ(xp, y, 8, ORO, ORO_B, 1, id="J10.%d" % (2 * k + 2))
    s.cierra()
    for k, t in ((0, "1"), (1, "3"), (5, "11"), (10, "21"), (15, "31"), (30, "61"), (35, "71"), (40, "81"), (45, "91")):
        s.text(xi - 14, Y(512) + k * 25.5, t, size=9, anchor="end")
    s.text((xi + xp) / 2, Y(485), "J10", size=11)
    s.guarda("msp_exp432e401y.svg")

# =============================================================================
# MSP-EXP430G2ET
# =============================================================================
def g2et():
    com = """
  MSP-EXP430G2ET, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp430g2et.xml. La
  disposicion, la de la figura 1 de SLAU772A: el eZ-FET y su USB arriba, el
  bloque de aislamiento J101 (siete puentes, de lado a lado), el zocalo DIP-20
  del MSP430G2553 en medio con las tiras BoosterPack J1 (1-10) a la izquierda
  y J2 (20-11) a la derecha, y abajo los LED con sus jumpers (J6, J7 y los
  tres de J8 del RGB), S1 (P1.3) y S2 (RESET).

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm (la placa mide unos 51 x 66 mm sin el
  eZ-FET; aqui, la placa entera). Cada pieza viva con el id de su pieza en
  el XML: LED1, LED2, LED3 (RGBLed), S1, S2, USB, BCL y los conectores J1, J2
  y J101, cada pin con su id. Los jumpers ensenan su puente (J101, J6, J7 y
  J8): cada posicion es un elemento PIEZA@valor. BCL#destino es un rotulo.
"""
    W, H = 1000, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 230, 30, 540, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    # El eZ-FET y su USB, arriba
    usb_micro(s, cx, by - 22, id="USB")
    s.text(cx, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 160, by + 80, 90, 90, "eZ-FET", "F5528", id="EZFET")
    s.text(cx - 115, by + 190, "eZ-FET lite", size=12)
    s.abre("BCL")
    s.rect(cx - 40, by + 85, 220, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 70, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 70, by + 123, "USCI_A0 (P1.1, P1.2)", size=11, fill="#7e5109")
    s.text(cx + 70, by + 145, "RFC 2217 en localhost:3356", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    # J101: siete columnas, pares arriba (eZ-FET), impares abajo (el chip)
    y_par, y_impar = by + 250, by + 250 + P
    x0 = cx - 3 * P
    pines, puentes = [], []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"]
    for k in range(7):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(7)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    s.text(bx + 12, y_par - 2, "eZ-FET", size=11, anchor="start")
    s.text(bx + 12, y_impar + 22, "MSP430G2553", size=11, anchor="start")
    # Las BoosterPack: J1 (1-10) a la izquierda, J2 (20-11) a la derecha
    yb = by + 400
    xj1, xj2 = cx - 228.6 + 20, cx + 228.6 - 20
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    s.text(xj1, yb - 22, "1", size=12, bold=True)
    s.text(xj2, yb - 22, "20", size=12, bold=True)
    s.text(xj1, yb + 10 * P + 8, "J1", size=12)
    s.text(xj2, yb + 10 * P + 8, "J2", size=12)
    j1 = ["3V3", "P1.0", "P1.1", "P1.2", "P1.3", "P1.4", "P1.5", "P2.0", "P2.1", "P2.2"]
    j2 = ["GND", "P2.6", "P2.7", "TEST", "RST", "P1.7", "P1.6", "P2.5", "P2.4", "P2.3"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    # El zocalo DIP-20 con el chip
    s.rect(cx - 70, yb - 15, 140, 10 * P + 5, "#1f1f1f", "#000", 1.5, rx=4)
    chip(s, cx - 55, yb, 110, 10 * P - 25, "MSP430", "G2553IN20")
    s.text(cx, yb + 10 * P + 25, "XU1 (DIP-20)", size=11)
    cristal(s, "Y1", cx - 30, yb + 10 * P + 40, 60, 18, "Y1 32 kHz (sin montar)")
    # Los LED y sus jumpers
    yl = by + 760
    def puente_v(id, x, y):
        jumper(s, id, [(id + ".1", x, y), (id + ".2", x, y + P)], {"1-2": [(x, y, x, y + P)]})
    puente_v("J6", cx - 190, yl)
    puente_v("J7", cx - 110, yl)
    s.text(cx - 190, yl - 22, "J6", size=10)
    s.text(cx - 110, yl - 22, "J7", size=10)
    led(s, "LED1", cx - 190, yl + 90, "#2ecc71", 11)
    s.text(cx - 190, yl + 120, "LED1 P1.0", size=11)
    led(s, "LED2", cx - 110, yl + 90, "#e74c3c", 11)
    s.text(cx - 110, yl + 120, "LED2 P1.6", size=11)
    x8 = cx - 10
    pines8 = []
    for k in range(3):
        pines8.append(("J8.%d" % (2 * k + 1), x8 + k * P, yl))
        pines8.append(("J8.%d" % (2 * k + 2), x8 + k * P, yl + P))
    jumper(s, "J8", pines8, {"1-2+3-4+5-6": [(x8 + k * P, yl, x8 + k * P, yl + P) for k in range(3)]})
    s.text(x8 + P, yl - 22, "J8 (G R B)", size=10)
    led_rgb(s, "LED3", x8 + P, yl + 95, 15)
    s.text(x8 + P, yl + 132, "LED3 (RGB)", size=11)
    boton(s, "S1", cx + 130, yl + 70, 56)
    s.text(cx + 130, yl + 120, "S1 (P1.3)", size=12)
    boton(s, "S2", cx + 215, yl + 70, 56)
    s.text(cx + 215, yl + 120, "S2 RESET", size=12)
    s.text(cx, by + bh - 50, "MSP-EXP430G2ET", size=20, bold=True)
    s.text(cx, by + bh - 30, "Texas Instruments", size=12)
    s.guarda("msp_exp430g2et.svg")

# =============================================================================
# MSP-EXP430F5529LP
# =============================================================================
def f5529lp():
    com = """
  MSP-EXP430F5529LP, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp430f5529lp.xml. La
  disposicion, la de la figura 8 de SLAU533D: el eZ-FET y su USB arriba, el
  bloque de aislamiento J101, el F5529 en medio con las cuatro tiras
  BoosterPack (J1 y J3 a la izquierda, J4 y J2 a la derecha), los LED y los
  pulsadores abajo, y el USB del F5529 al pie.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm. Cada pieza viva con el id de su pieza en
  el XML (LED1, LED2, S1, S2, S3, USB, BCL, J101, J1-J4, cada pin con su id).
"""
    W, H = 1100, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 232, 30, 636, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 175, by + 80, 100, 100, "eZ-FET", "lite", id="EZFET")
    s.abre("BCL")
    s.rect(cx + 20, by + 85, 240, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 140, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 140, by + 123, "UCA1 (P4.4, P4.5)", size=11, fill="#7e5109")
    s.text(cx + 140, by + 145, "RFC 2217 en localhost:3357", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    boton(s, "S3", bx + bw - 60, by + 210, 50)
    s.text(bx + bw - 60, by + 252, "RESET (S3)", size=11)
    y_par, y_impar = by + 260, by + 260 + P
    x0 = cx - 3 * P
    pines, puentes = [], []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"]
    for k in range(7):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(7)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    yb = by + 410
    xj1 = cx - 228.6
    xj3 = xj1 + P
    xj2 = cx + 228.6
    xj4 = xj2 - P
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J3", xj3, yb, ["J3.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J4", xj4, yb, ["J4.%d" % k for k in range(10, 0, -1)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj3, "21"), (xj4, "40"), (xj2, "20")):
        s.text(x, yb - 22, t, size=12, bold=True)
    j1 = ["3V3", "P6.5", "P3.4", "P3.3", "P1.6", "P6.6", "P3.2", "P2.7", "P4.2", "P4.1"]
    j3 = ["5V", "GND", "P6.0", "P6.1", "P6.2", "P6.3", "P6.4", "P7.0", "P3.6", "P3.5"]
    j4 = ["P2.5", "P2.4", "P1.5", "P1.4", "P1.3", "P1.2", "P4.3", "P4.0", "P3.7", "P8.2"]
    j2 = ["GND", "P2.0", "P2.2", "P7.4", "RST", "P3.0", "P3.1", "P2.6", "P2.3", "P8.1"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, xj3 + 52, y, j3[k], 64, 18, 12)
        etiqueta(s, xj4 - 52, y, j4[k], 64, 18, 12)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    chip(s, cx - 65, yb + 50, 130, 130, "MSP430", "F5529")
    s.text(cx, yb + 200, "U1", size=11)
    cristal(s, "Y2", cx - 45, yb + 10, 90, 20, "Y2 32 kHz")
    cristal(s, "Y1", cx - 45, yb + 215, 90, 20, "Y1 4 MHz")
    yl = by + 790
    led(s, "LED1", cx - 150, yl, "#e74c3c", 11)
    s.text(cx - 150, yl + 30, "LED1 P1.0", size=11)
    led(s, "LED2", cx - 70, yl, "#2ecc71", 11)
    s.text(cx - 70, yl + 30, "LED2 P4.7", size=11)
    boton(s, "S1", bx + 40, yl + 60, 52)
    s.text(bx + 40, yl + 105, "S1 P2.1", size=11)
    boton(s, "S2", bx + bw - 40, yl + 60, 52)
    s.text(bx + bw - 40, yl + 105, "S2 P1.1", size=11)
    usb_micro(s, cx + 120, by + bh - 50, id="USBF5")
    s.text(cx + 120, by + bh - 60, "USB (F5529, M5)", size=11)
    s.text(cx - 60, by + bh - 50, "MSP-EXP430F5529LP", size=18, bold=True)
    s.guarda("msp_exp430f5529lp.svg")


# =============================================================================
# MSP-EXP430FR4133
# =============================================================================
def fr4133lp():
    com = """
  MSP-EXP430FR4133, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp430fr4133.xml. La
  disposicion, la de la figura 1 de SLAU595B: el eZ-FET y su USB arriba, el
  bloque de aislamiento J101, las dos tiras BoosterPack de 20 pines (J1 a la
  izquierda, J2 a la derecha), el vidrio FH-1138P en medio, el FR4133 y el
  cristal Y1 debajo, y los LED (LED1 tras JP1) y los pulsadores abajo.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm. Cada pieza viva con el id de su pieza en
  el XML (LCD1, LED1, LED2, S1, S2, S3, USB, BCL, J101, JP1, J1, J2, cada pin
  con su id). Los jumpers ensenan su puente (J101, JP1). BCL#destino es un
  rotulo.
"""
    W, H = 1000, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 200, 30, 600, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 175, by + 80, 100, 100, "eZ-FET", "lite", id="EZFET")
    s.abre("BCL")
    s.rect(cx + 20, by + 85, 240, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 140, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 140, by + 123, "UCA0 (P1.0, P1.1)", size=11, fill="#7e5109")
    s.text(cx + 140, by + 145, "RFC 2217 en localhost:3361", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    boton(s, "S3", bx + 60, by + 130, 50)
    s.text(bx + 60, by + 172, "RESET (S3)", size=11)
    y_par, y_impar = by + 230, by + 230 + P
    x0 = cx - 3 * P
    pines, puentes = [], []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"]
    for k in range(7):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(7)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    yb = by + 380
    xj1, xj2 = bx + 40, bx + bw - 40
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj2, "20")):
        s.text(x, yb - 22, t, size=12, bold=True)
    j1 = ["3V3", "P8.1", "P1.1", "P1.0", "P2.7", "P8.0", "P5.1", "P2.5", "P8.2", "P8.3"]
    j2 = ["GND", "P1.7", "P1.6", "P5.0", "RST", "P5.2", "P5.3", "P1.3", "P1.4", "P1.5"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    # El vidrio: seis caracteres y los simbolos de arriba
    s.abre("LCD1")
    s.rect(cx - 200, yb - 10, 400, 130, "#b8c4a8", "#5d6d5e", 3, rx=6)
    s.rect(cx - 185, yb + 5, 370, 100, "#cfdcc0", None, rx=3)
    s.cierra()
    for k in range(6):
        s.rect(cx - 168 + k * 58, yb + 30, 44, 66, "#c2d0b2", "#9aaa8c", 1, rx=2)
    s.text(cx, yb + 22, "TMR  HRT  REC  !  ANT  BATT", size=10, fill="#5d6d5e")
    s.text(cx, yb + 140, "LCD1 FH-1138P", size=12)
    chip(s, cx - 65, yb + 160, 130, 130, "MSP430", "FR4133")
    s.text(cx, yb + 310, "MSP1", size=11)
    cristal(s, "Y1", cx + 90, yb + 200, 70, 20, "Y1 32 kHz")
    yl = by + 800
    jumper(s, "JP1", [("JP1.1", cx - 150, yl - 50), ("JP1.2", cx - 150, yl - 50 + P)],
           {"1-2": [(cx - 150, yl - 50, cx - 150, yl - 50 + P)]})
    s.text(cx - 150, yl - 72, "JP1", size=10)
    led(s, "LED1", cx - 150, yl + 20, "#e74c3c", 11)
    s.text(cx - 150, yl + 50, "LED1 P1.0", size=11)
    led(s, "LED2", cx + 150, yl + 20, "#2ecc71", 11)
    s.text(cx + 150, yl + 50, "LED2 P4.0", size=11)
    boton(s, "S1", bx + 60, yl + 100, 52)
    s.text(bx + 60, yl + 145, "S1 P1.2", size=11)
    boton(s, "S2", bx + bw - 60, yl + 100, 52)
    s.text(bx + bw - 60, yl + 145, "S2 P2.6", size=11)
    s.text(cx, by + bh - 40, "MSP-EXP430FR4133", size=18, bold=True)
    s.guarda("msp_exp430fr4133.svg")


# =============================================================================
# MSP-EXP430FR2311
# =============================================================================
def fr2311lp():
    com = """
  MSP-EXP430FR2311, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp430fr2311.xml. La
  disposicion, la de la figura 1 de SLAU664B: el eZ-FET y su USB arriba, el
  bloque de aislamiento J101, las dos tiras BoosterPack de 20 pines (J1 a la
  izquierda, J2 a la derecha), el FR2311 en medio, el fotodiodo D1 con R3 y
  sus puentes J7, J8 y J9 debajo, y los LED (tras J10 y J11) y S1 abajo.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm. Cada pieza viva con el id de su pieza en
  el XML (D1, R3, LED1, LED2, S1, S2, USB, BCL, J101, J7-J11, J1, J2, cada pin
  con su id). Los jumpers ensenan su puente. BCL#destino es un rotulo.
"""
    W, H = 1000, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 200, 30, 600, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 175, by + 80, 100, 100, "eZ-FET", "lite", id="EZFET")
    s.abre("BCL")
    s.rect(cx + 20, by + 85, 240, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 140, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 140, by + 123, "UCA0 (P1.7, P1.6)", size=11, fill="#7e5109")
    s.text(cx + 140, by + 145, "RFC 2217 en localhost:3362", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    boton(s, "S2", bx + 60, by + 130, 50)
    s.text(bx + 60, by + 172, "RESET (S2)", size=11)
    y_par, y_impar = by + 230, by + 230 + P
    x0 = cx - 3 * P
    pines, puentes = [], []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"]
    for k in range(7):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(7)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    yb = by + 380
    xj1, xj2 = bx + 40, bx + bw - 40
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj2, "20")):
        s.text(x, yb - 22, t, size=12, bold=True)
    j1 = ["3V3", "P1.4", "P1.6", "P1.7", "P1.0", "P1.5", "P2.3", "P2.1", "P1.3", "P1.2"]
    j2 = ["GND", "P2.0", "P2.2", "NC", "RST", "P2.4", "P2.5", "P2.7", "P2.6", "P1.1"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    chip(s, cx - 65, yb + 10, 130, 130, "MSP430", "FR2311")
    s.text(cx, yb + 160, "MSP1", size=11)
    # El fotodiodo con R3, tras J7 (OA0-), J8 (OA0O) y J9 (OA0+)
    yd = yb + 230
    for k, (jp, nom) in enumerate((("J9", "OA0+"), ("J7", "OA0-"), ("J8", "OA0O"))):
        x = cx - 100 + k * 100
        jumper(s, jp, [(jp + ".1", x, yd), (jp + ".2", x, yd + P)], {"1-2": [(x, yd, x, yd + P)]})
        s.text(x, yd - 22, "%s %s" % (jp, nom), size=10)
    s.abre("D1")
    s.rect(cx - 30, yd + 60, 60, 40, "#2c3e50", "#000000", 1.5, rx=4)
    s.circ(cx, yd + 80, 12, "#85c1e9", "#1b4f72", 1.5)
    s.cierra()
    s.text(cx, yd + 118, "D1 SFH 2701", size=11)
    s.abre("R3")
    s.rect(cx + 60, yd + 70, 60, 20, "#d5b895", "#7e5109", 1.2, rx=4)
    s.cierra()
    s.text(cx + 90, yd + 108, "R3 2,37M", size=10)
    yl = by + 800
    for k, (jp, lid, col, nom) in enumerate((("J10", "LED1", "#e74c3c", "LED1 P1.0"), ("J11", "LED2", "#2ecc71", "LED2 P2.0"))):
        x = cx - 150 + 300 * k
        jumper(s, jp, [(jp + ".1", x, yl - 50), (jp + ".2", x, yl - 50 + P)], {"1-2": [(x, yl - 50, x, yl - 50 + P)]})
        s.text(x, yl - 72, jp, size=10)
        led(s, lid, x, yl + 20, col, 11)
        s.text(x, yl + 50, nom, size=11)
    boton(s, "S1", bx + 60, yl + 100, 52)
    s.text(bx + 60, yl + 145, "S1 P1.1", size=11)
    s.text(cx, by + bh - 40, "MSP-EXP430FR2311", size=18, bold=True)
    s.guarda("msp_exp430fr2311.svg")


def fr2355lp():
    com = """
  MSP-EXP430FR2355, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/msp_exp430fr2355.xml. La
  disposicion, la de la figura 1 de SLAU680: el eZ-FET y su USB arriba, el
  bloque de aislamiento J101, las cuatro tiras BoosterPack de 10 pines (J1 y
  J3 a la izquierda, J4 y J2 a la derecha), el FR2355 en medio, el fotodiodo
  D1 con R3 y sus puentes J7, J8 y J9 debajo, y los LED (tras J10 y J11), S1
  y S2 abajo.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm. Cada pieza viva con el id de su pieza en
  el XML (D1, R3, LED1, LED2, S1, S2, S3, USB, BCL, J101, J7-J11, J1-J4, cada
  pin con su id). Los jumpers ensenan su puente. BCL#destino es un rotulo.
"""
    W, H = 1100, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 200, 30, 700, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 175, by + 80, 100, 100, "eZ-FET", "", id="EZFET")
    s.abre("BCL")
    s.rect(cx + 20, by + 85, 240, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 140, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 140, by + 123, "UCA1 (P4.3, P4.2)", size=11, fill="#7e5109")
    s.text(cx + 140, by + 145, "RFC 2217 en localhost:3363", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    boton(s, "S3", bx + 60, by + 130, 50)
    s.text(bx + 60, by + 172, "RESET (S3)", size=11)
    y_par, y_impar = by + 230, by + 230 + P
    x0 = cx - 3 * P
    pines, puentes = [], []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"]
    for k in range(7):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(7)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    yb = by + 380
    xj1, xj3, xj4, xj2 = bx + 40, bx + 40 + P, bx + bw - 40 - P, bx + bw - 40
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J3", xj3, yb, ["J3.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J4", xj4, yb, ["J4.%d" % k for k in range(10, 0, -1)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj3, "21"), (xj4, "40"), (xj2, "20")):
        s.text(x, yb - 22, t, size=12, bold=True)
    j1 = ["3V3", "P1.5", "P1.6", "P1.7", "P3.6", "P5.2", "P4.5", "P3.4", "P1.3", "P1.2"]
    j3 = ["5V", "GND", "P1.4", "P5.3", "P5.1", "P5.0", "P5.4", "P1.1", "P3.5", "P3.1"]
    j4 = ["P2.1", "P6.0", "P6.1", "P6.2", "P6.3", "P6.4", "P3.7", "P2.4", "P3.3", "P3.2"]
    j2 = ["GND", "P2.0", "P2.2", "P4.0", "RST", "P4.6", "P4.7", "P4.4", "P2.5", "P3.0"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, xj3 + 55, y, j3[k], 60)
        etiqueta(s, xj4 - 55, y, j4[k], 60)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    chip(s, cx - 70, yb + 5, 140, 140, "MSP430", "FR2355")
    s.text(cx, yb + 165, "MSP1", size=11)
    s.rect(cx + 90, yb + 60, 30, 14, "#bdc3c7", "#7f8c8d", 1, rx=3)
    s.text(cx + 105, yb + 92, "Y1 32k", size=9)
    # El fotodiodo con R3, tras J9 (OA2+), J7 (OA2-) y J8 (OA2O)
    yd = yb + 230
    for k, (jp, nom) in enumerate((("J9", "OA2+"), ("J7", "OA2-"), ("J8", "OA2O"))):
        x = cx - 100 + k * 100
        jumper(s, jp, [(jp + ".1", x, yd), (jp + ".2", x, yd + P)], {"1-2": [(x, yd, x, yd + P)]})
        s.text(x, yd - 22, "%s %s" % (jp, nom), size=10)
    s.abre("D1")
    s.rect(cx - 30, yd + 60, 60, 40, "#2c3e50", "#000000", 1.5, rx=4)
    s.circ(cx, yd + 80, 12, "#85c1e9", "#1b4f72", 1.5)
    s.cierra()
    s.text(cx, yd + 118, "D1", size=11)
    s.abre("R3")
    s.rect(cx + 60, yd + 70, 60, 20, "#d5b895", "#7e5109", 1.2, rx=4)
    s.cierra()
    s.text(cx + 90, yd + 108, "R3 2,37M", size=10)
    yl = by + 800
    for k, (jp, lid, col, nom) in enumerate((("J10", "LED1", "#e74c3c", "LED1 P1.0"), ("J11", "LED2", "#2ecc71", "LED2 P6.6"))):
        x = cx - 150 + 300 * k
        jumper(s, jp, [(jp + ".1", x, yl - 50), (jp + ".2", x, yl - 50 + P)], {"1-2": [(x, yl - 50, x, yl - 50 + P)]})
        s.text(x, yl - 72, jp, size=10)
        led(s, lid, x, yl + 20, col, 11)
        s.text(x, yl + 50, nom, size=11)
    boton(s, "S1", bx + 90, yl + 60, 52)
    s.text(bx + 90, yl + 105, "S1 P4.1", size=11)
    boton(s, "S2", bx + bw - 90, yl + 60, 52)
    s.text(bx + bw - 90, yl + 105, "S2 P2.3", size=11)
    s.text(cx, by + bh - 40, "MSP-EXP430FR2355", size=18, bold=True)
    s.guarda("msp_exp430fr2355.svg")


def lp2476():
    com = """
  LP-MSP430FR2476, dibujada a mano: una aproximacion, no una foto ni una
  ilustracion del fabricante. Es el dibujo de placas/lp_msp430fr2476.xml. La
  disposicion, la de la figura 2 de SLAU802: el eZ-FET y su USB arriba, el
  bloque de aislamiento J101, las cuatro tiras BoosterPack de 10 pines (J1 y
  J3 a la izquierda, J4 y J2 a la derecha), el FR2476 en medio, el sensor U1
  con J9 debajo, y los LED (LED1 tras J7, el RGB tras J8), S1 y S2 abajo.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm. Cada pieza viva con el id de su pieza en
  el XML (U1, LED1, LED2, S1, S2, S3, USB, BCL, J101, J7-J9, J1-J4, cada pin
  con su id). Los jumpers ensenan su puente. BCL#destino es un rotulo.
"""
    W, H = 1100, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 200, 30, 700, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 175, by + 80, 100, 100, "eZ-FET", "", id="EZFET")
    s.abre("BCL")
    s.rect(cx + 20, by + 85, 240, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 140, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 140, by + 123, "UCA0 (P1.4, P1.5)", size=11, fill="#7e5109")
    s.text(cx + 140, by + 145, "RFC 2217 en localhost:3364", size=12, fill="#1b2631", bold=True, id="BCL#destino")
    boton(s, "S3", bx + 60, by + 130, 50)
    s.text(bx + 60, by + 172, "RESET (S3)", size=11)
    y_par, y_impar = by + 230, by + 230 + P
    x0 = cx - 3 * P
    pines, puentes = [], []
    nombres = ["GND", "5V", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"]
    for k in range(7):
        x = x0 + k * P
        pines.append(("J101.%d" % (2 * k + 2), x, y_par))
        pines.append(("J101.%d" % (2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, "J101", pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(7)): puentes})
    for k, n in enumerate(nombres):
        s.text(x0 + k * P, y_impar + 30, n, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, "J101", size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    yb = by + 380
    xj1, xj3, xj4, xj2 = bx + 40, bx + 40 + P, bx + bw - 40 - P, bx + bw - 40
    tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J3", xj3, yb, ["J3.%d" % k for k in range(1, 11)])
    tira_vertical(s, "J4", xj4, yb, ["J4.%d" % k for k in range(10, 0, -1)])
    tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
    for x, t in ((xj1, "1"), (xj3, "21"), (xj4, "40"), (xj2, "20")):
        s.text(x, yb - 22, t, size=12, bold=True)
    j1 = ["3V3", "P1.6", "P2.5", "P2.6", "P2.2", "P5.4", "P3.5", "P4.5", "P1.3", "P1.2"]
    j3 = ["5V", "GND", "P1.7", "P4.3", "P4.4", "P5.3", "P1.0", "P1.1", "P5.7", "P3.7"]
    j4 = ["P5.2", "P5.1", "P5.0", "P4.7", "P6.0", "P3.3", "P6.1", "P6.2", "P4.1", "P3.1"]
    j2 = ["GND", "P4.6", "P2.1", "P2.0", "RST", "P3.2", "P3.6", "P4.2", "P2.7", "P2.4"]
    for k in range(10):
        y = yb + k * P
        etiqueta(s, bx - 50, y, j1[k], 74)
        etiqueta(s, xj3 + 55, y, j3[k], 60)
        etiqueta(s, xj4 - 55, y, j4[k], 60)
        etiqueta(s, bx + bw + 50, y, j2[k], 74)
    chip(s, cx - 70, yb + 5, 140, 140, "MSP430", "FR2476")
    s.text(cx, yb + 165, "MSP1", size=11)
    s.rect(cx + 90, yb + 60, 30, 14, "#bdc3c7", "#7f8c8d", 1, rx=3)
    s.text(cx + 105, yb + 92, "Y1 (sin R2/R3)", size=9)
    # El sensor de temperatura U1 (TMP235) con J9
    yd = yb + 230
    jumper(s, "J9", [("J9.1", cx - 100, yd), ("J9.2", cx - 100, yd + P)], {"1-2": [(cx - 100, yd, cx - 100, yd + P)]})
    s.text(cx - 100, yd - 22, "J9 VDD", size=10)
    s.abre("U1")
    s.rect(cx - 30, yd + 5, 60, 40, "#2c3e50", "#000000", 1.5, rx=4)
    s.cierra()
    s.text(cx, yd + 30, "TMP235", size=9, fill="#ffffff")
    s.text(cx, yd + 65, "U1 (A1, P1.1)", size=11)
    yl = by + 800
    jumper(s, "J7", [("J7.1", cx - 150, yl - 50), ("J7.2", cx - 150, yl - 50 + P)], {"1-2": [(cx - 150, yl - 50, cx - 150, yl - 50 + P)]})
    s.text(cx - 150, yl - 72, "J7", size=10)
    led(s, "LED1", cx - 150, yl + 20, "#2ecc71", 11)
    s.text(cx - 150, yl + 50, "LED1 P1.0", size=11)
    x8 = cx + 120
    pines8 = []
    for k in range(3):
        pines8.append(("J8.%d" % (2 * k + 1), x8 + k * P, yl - 50 + P))
        pines8.append(("J8.%d" % (2 * k + 2), x8 + k * P, yl - 50))
    jumper(s, "J8", pines8, {"1-2+3-4+5-6": [(x8 + k * P, yl - 50, x8 + k * P, yl - 50 + P) for k in range(3)]})
    s.text(x8 + P, yl - 72, "J8 (B G R)", size=10)
    led_rgb(s, "LED2", x8 + P, yl + 25, 15)
    s.text(x8 + P, yl + 55, "LED2 (RGB)", size=11)
    boton(s, "S1", bx + 90, yl + 60, 52)
    s.text(bx + 90, yl + 105, "S1 P4.0", size=11)
    boton(s, "S2", bx + bw - 90, yl + 60, 52)
    s.text(bx + bw - 90, yl + 105, "S2 P2.3", size=11)
    s.text(cx, by + bh - 40, "LP-MSP430FR2476", size=18, bold=True)
    s.guarda("lp_msp430fr2476.svg")

# =============================================================================
# Las LaunchPad de las FR5xx/FR6xx (M12): MSP-EXP430FR5969, FR5994 y FR6989
# =============================================================================
def lp_fr5(fichero, titulo, chip2, jid, jnombres, leds, botones, bcl, conectores=None,
           lcd=False, sd=False):
    com = """
  %s, dibujada a mano: una aproximacion, no una foto ni una ilustracion del
  fabricante. Es el dibujo de placas/%s.xml. La disposicion, la de las
  LaunchPad de TI: el eZ-FET y su USB arriba, el bloque de aislamiento %s, el
  chip en medio%s, y los LED y los pulsadores abajo.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, una unidad es 0,1 mm. Cada pieza viva con el id de su pieza en
  el XML; el jumper ensena su puente; BCL#destino es un rotulo.
""" % (titulo, fichero, jid, " (con el vidrio LCD1)" if lcd else (" y la ranura microSD" if sd else ""))
    W, H = 1000, 1060
    s = Svg(W, H, com)
    bx, by, bw, bh = 200, 30, 600, 980
    cx = bx + bw / 2
    s.rect(bx, by, bw, bh, ROJO_PCB, BORDE_PCB, 5, rx=18)
    for x, y in ((bx + 30, by + 30), (bx + bw - 30, by + 30), (bx + 30, by + bh - 30), (bx + bw - 30, by + bh - 30)):
        agujero(s, x, y)
    usb_micro(s, cx - 120, by - 22, id="USB")
    s.text(cx - 120, by + 52, "USB (eZ-FET)", size=12)
    chip(s, cx - 175, by + 80, 100, 100, "eZ-FET", "lite", id="EZFET")
    s.abre("BCL")
    s.rect(cx + 20, by + 85, 240, 72, "#fef9e7", "#d68910", 2, rx=5)
    s.cierra()
    s.text(cx + 140, by + 105, "Backchannel UART", size=12, fill="#7e5109", bold=True)
    s.text(cx + 140, by + 123, bcl[0], size=11, fill="#7e5109")
    s.text(cx + 140, by + 145, "RFC 2217 en localhost:%d" % bcl[1], size=12, fill="#1b2631", bold=True,
           id="BCL#destino")
    boton(s, "S3", bx + 60, by + 130, 50)
    s.text(bx + 60, by + 172, "RESET (S3)", size=11)
    n = len(jnombres)
    y_par, y_impar = by + 230, by + 230 + P
    x0 = cx - (n - 1) / 2.0 * P
    pines, puentes = [], []
    for k in range(n):
        x = x0 + k * P
        pines.append(("%s.%d" % (jid, 2 * k + 2), x, y_par))
        pines.append(("%s.%d" % (jid, 2 * k + 1), x, y_impar))
        puentes.append((x, y_par, x, y_impar))
    jumper(s, jid, pines, {"+".join("%d-%d" % (2 * k + 1, 2 * k + 2) for k in range(n)): puentes})
    for k, t in enumerate(jnombres):
        s.text(x0 + k * P, y_impar + 30, t, size=9, rot=-90, anchor="end")
    s.text(x0 - 30, y_par + 5, jid, size=13, anchor="end", bold=True)
    s.line(bx, y_par + P / 2, bx + bw, y_par + P / 2, "#ffffff", 2, "8 6")
    yb = by + 380
    if conectores:
        xj1, xj2 = bx + 40, bx + bw - 40
        tira_vertical(s, "J1", xj1, yb, ["J1.%d" % k for k in range(1, 11)])
        tira_vertical(s, "J2", xj2, yb, ["J2.%d" % k for k in range(10, 0, -1)])
        for x, t in ((xj1, "1"), (xj2, "20")):
            s.text(x, yb - 22, t, size=12, bold=True)
        j1, j2 = conectores
        for k in range(10):
            y = yb + k * P
            etiqueta(s, bx - 50, y, j1[k], 74)
            etiqueta(s, bx + bw + 50, y, j2[k], 74)
    yc = yb
    if lcd:
        s.abre("LCD1")
        s.rect(cx - 200, yb - 10, 400, 130, "#b8c4a8", "#5d6d5e", 3, rx=6)
        s.rect(cx - 185, yb + 5, 370, 100, "#cfdcc0", None, rx=3)
        s.cierra()
        for k in range(6):
            s.rect(cx - 168 + k * 58, yb + 30, 44, 66, "#c2d0b2", "#9aaa8c", 1, rx=2)
        s.text(cx, yb + 22, "TMR  HRT  REC  !  ANT  BATT", size=10, fill="#5d6d5e")
        s.text(cx, yb + 140, "LCD1 FH-1138P", size=12)
        yc = yb + 160
    chip(s, cx - 65, yc, 130, 130, "MSP430", chip2)
    s.text(cx, yc + 150, "U1", size=11)
    cristal(s, "Y4", cx + 90, yc + 40, 70, 20, "32 kHz")
    if sd:
        s.abre("SD")
        s.rect(cx - 230, yb + 10, 140, 110, "#bdc3c7", "#566573", 3, rx=6)
        s.rect(cx - 215, yb + 25, 110, 60, "#2c3e50", None, rx=3)
        s.cierra()
        s.text(cx - 160, yb + 105, "microSD", size=12, fill="#1b2631", bold=True)
    yl = by + 800
    for (lid, color, txt), x in zip(leds, (cx - 150, cx + 150)):
        led(s, lid, x, yl + 20, color, 11)
        s.text(x, yl + 50, txt, size=11)
    for (bid, txt), x in zip(botones, (bx + 60, bx + bw - 60)):
        boton(s, bid, x, yl + 100, 52)
        s.text(x, yl + 145, txt, size=11)
    s.text(cx, by + bh - 40, titulo, size=18, bold=True)
    s.guarda(fichero + ".svg")


def fr5969lp():
    lp_fr5("msp_exp430fr5969", "MSP-EXP430FR5969", "FR5969", "J13",
           ["GND", "V+", "RXD<<", "TXD>>", "SBW RST", "SBW TST"],
           [("LED1", "#e74c3c", "LED1 P4.6"), ("LED2", "#2ecc71", "LED2 P1.0")],
           [("S1", "S1 P4.5"), ("S2", "S2 P1.1")], ("UCA0 (P2.0, P2.1)", 3364),
           conectores=(["3V3", "P4.2", "P2.6", "P2.5", "P4.3", "P2.4", "P2.2", "P3.4", "P3.5", "P3.6"],
                       ["GND", "P1.2", "P3.0", "-", "RST", "P1.6", "P1.7", "P1.5", "P1.4", "P1.3"]))


def fr5994lp():
    lp_fr5("msp_exp430fr5994", "MSP-EXP430FR5994", "FR5994", "J101",
           ["GND", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"],
           [("LED1", "#e74c3c", "LED1 P1.0"), ("LED2", "#2ecc71", "LED2 P1.1")],
           [("S1", "S1 P5.6"), ("S2", "S2 P5.5")], ("UCA0 (P2.0, P2.1)", 3364), sd=True)


def fr6989lp():
    lp_fr5("msp_exp430fr6989", "MSP-EXP430FR6989", "FR6989", "J101",
           ["GND", "3V3", "RXD<<", "TXD>>", "SBW RST", "SBW TST"],
           [("LED1", "#e74c3c", "LED1 P1.0"), ("LED2", "#2ecc71", "LED2 P9.7")],
           [("S1", "S1 P1.1"), ("S2", "S2 P1.2")], ("UCA1 (P3.4, P3.5)", 3364), lcd=True)


p401r()
edumkii()
e401y()
g2et()
f5529lp()
fr4133lp()
fr2311lp()
fr2355lp()
lp2476()
fr5969lp()
fr5994lp()
fr6989lp()
