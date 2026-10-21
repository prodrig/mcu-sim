#!/usr/bin/env python3
# =============================================================================
# dibuja_st.py — Genera el dibujo de la STM32F3DISCOVERY: stm32f3discovery.svg
#
# El SVG esta versionado; esto es como se hizo, para rehacerlo si cambia la
# placa. Una unidad es 0,1 mm. Cada pieza visible del XML y cada pin de
# conector llevan su id, y cada posicion de un jumper es un elemento
# PIEZA@valor dentro de la pieza (doc/parts.md §2.6). `make gui-sistema`, en
# su grupo C19, comprueba que no falta ninguno.
#
# Los rotulos de EXP1 y EXP2 salen del propio XML (los nodos de cada pin), asi
# que el dibujo no puede decir una cosa y la placa otra.
#
#   python3 placas/dibuja_st.py [CARPETA]     (desde src/; por omision, placas/)
# =============================================================================
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else AQUI
P = 25.4  # paso de 0,1"

PCB, BORDE_PCB = "#f4f6f6", "#95a5a6"      # la Discovery es blanca
SERIGRAFIA = "#1b2631"
ORO, ORO_B = "#d4ac0d", "#9a7d0a"
NEGRO = "#1a1a1a"


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
        with open(os.path.join(OUT, nombre), "w", encoding="utf-8") as f:
            f.write("\n".join(s) + "\n")


# --- Las etiquetas de los pines, con los colores de placas/nucleo_f446re.svg ---
def tipo_de(n):
    if n in ("3V", "5V", "VDD"):
        return "#e74c3c", "#ffffff"
    if n in ("VSS", "GND"):
        return "#2c3e50", "#ffffff"
    if n in ("NRST", "BOOT0"):
        return "#f39c12", "#ffffff"
    if n in ("-", "NC"):
        return "#bdc3c7", "#566573"
    return "#aed6f1", "#1b2631"


def etiqueta(s, x, y, n, ancho=64, alto=19, size=12):
    f, t = tipo_de(n)
    s.rect(x - ancho / 2, y - alto / 2, ancho, alto, f, "#ffffff", 1, rx=3)
    s.text(x, y + size * 0.36, n, size=size, fill=t, bold=True)


def pin(s, id, x, y, lado=16):
    s.rect(x - lado / 2, y - lado / 2, lado, lado, ORO, ORO_B, 1, id=id)


def boton(s, id, x, y, tam=60, color="#2c3e50"):
    s.abre(id)
    s.rect(x - tam / 2, y - tam / 2, tam, tam, "#bdc3c7", "#7f8c8d", 2, rx=5)
    s.circ(x, y, tam * 0.33, color, "#000000", 1.5)
    s.cierra()


def led(s, id, x, y, color, r=10):
    s.rect(x - r * 1.3, y - r * 0.9, r * 2.6, r * 1.8, "#ffffff", "#999999", 1, rx=2)
    s.circ(x, y, r * 0.75, color, "#555555", 1.2, id=id)


def usb_mini(s, x, y, ancho=80, alto=60, id=None):
    if id:
        s.abre(id)
    s.rect(x - ancho / 2, y, ancho, alto, "#95a5a6", "#7f8c8d", 2, rx=4)
    s.rect(x - ancho / 2 + 12, y + 10, ancho - 24, 12, "#5d6d7e", None, rx=2)
    if id:
        s.cierra()


def chip(s, x, y, w, h, l1, l2=None, size=14):
    s.rect(x, y, w, h, "#2c2c2c", "#000000", 1.5, rx=3)
    s.circ(x + 10, y + 10, 4, "#7f8c8d")
    s.text(x + w / 2, y + h / 2 + (0 if l2 else 5), l1, size=size, fill="#ffffff")
    if l2:
        s.text(x + w / 2, y + h / 2 + 18, l2, size=11, fill="#bdc3c7")


def nodos_conector(id_):
    """{nombre de pin: nodo} de un conector de placas/stm32f3discovery.xml"""
    with open(os.path.join(AQUI, "stm32f3discovery.xml"), encoding="utf-8") as f:
        x = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
    m = re.search(r'<componente tipo="Conector" id="%s"[^>]*>(.*?)</componente>' % id_, x, re.S)
    return dict(re.findall(r'<pin nombre="([^"]+)" nodo="([^"]+)"', m.group(1)))


def tira(s, id_, x_ext, x_int, y0, izquierda):
    """Una tira de 2x25 en vertical: el pin 1 arriba en la columna exterior,
    el 2 a su lado en la interior, y asi hasta el 49/50. Los rotulos de la
    columna exterior van fuera de la placa; los de la interior, dentro"""
    n = nodos_conector(id_)
    s.abre(id_)
    xa, xb = min(x_ext, x_int), max(x_ext, x_int)
    s.rect(xa - P / 2, y0 - P / 2, xb - xa + P, 25 * P, NEGRO, "#000000", 1)
    for k in range(25):
        y = y0 + k * P
        pin(s, "%s.%d" % (id_, 2 * k + 1), x_ext, y)
        pin(s, "%s.%d" % (id_, 2 * k + 2), x_int, y)
    s.cierra()
    sig = -1 if izquierda else 1
    for k in range(25):
        y = y0 + k * P
        etiqueta(s, x_ext + sig * 50, y, n.get(str(2 * k + 1), "-"))
        etiqueta(s, x_int - sig * 50, y, n.get(str(2 * k + 2), "-"))
    s.text(x_ext + sig * 4, y0 - 26, "1", size=12)


def discovery():
    com = """
  STM32F3DISCOVERY (MB1035), dibujada a mano: una aproximacion, no una foto ni
  una ilustracion del fabricante. Es el dibujo de placas/stm32f3discovery.xml,
  que lo encuentra solo porque se llama como ella (doc/parts.md, "El dibujo").
  La disposicion es la de la figura 3 de UM1570: el ST-LINK y su USB (CN1)
  arriba, el chip en medio con la rosa de ocho LED alrededor, los sensores al
  lado, B1 (USER, azul) y B2 (RESET, negro) abajo, y las dos tiras de 2x25,
  P1 a la izquierda y P2 a la derecha, con el pin 1 arriba.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, una unidad es 0,1 mm -la placa mide
  unos 66 x 97 mm, y a los lados quedan los rotulos- y cada pieza viva con el
  id de su pieza en el XML: LD1, LD3-LD10, B1, B2, el puente IDD JP3, el
  cristal X3 (sin montar, como en la placa), el MCO del ST-LINK, el USB CN1
  y las tiras EXP1 (P1) y EXP2 (P2), cada pin con su id (EXP1.1 ...
  EXP2.50). En el modelo las tiras no pueden llamarse P1 y P2: «P1.5» es el
  pad PB5. Los rotulos salen del XML, nodo a nodo, y llevan los colores de
  los pinouts: el pin del chip en azul, la alimentacion en rojo, la masa en
  negro, NRST y BOOT0 en naranja.

  Los sensores (S4): U5, el giroscopo, y U3, el acelerometro y magnetometro,
  con el nombre del chip de cada revision como variante -U5@L3GD20 o
  U5@I3G4250D, U3@LSM303DLHC o U3@LSM303AGR-; mcu-sim deja el que se monta.

  JP3 ensena su puente: JP3@1-2 (puesto, de fabrica); mcu-sim deja el que
  dice el XML (src/common/svg_variantes.h).
"""
    BX, BY, BW, BH = 170, 30, 660, 970
    W, H = BX * 2 + BW, BY * 2 + BH
    s = Svg(W, H, com)
    s.rect(BX, BY, BW, BH, PCB, BORDE_PCB, 3, rx=18)
    for cx, cy in ((BX + 30, BY + 30), (BX + BW - 30, BY + 30),
                   (BX + 30, BY + BH - 30), (BX + BW - 30, BY + BH - 30)):
        s.circ(cx, cy, 14, "#ffffff", BORDE_PCB, 2)
    s.text(BX + BW / 2, BY + BH - 190, "STM32F3DISCOVERY", size=18, bold=True)

    # --- Arriba: el ST-LINK, su USB y la alimentacion ------------------------
    cx = BX + BW / 2
    usb_mini(s, cx, BY - 8, id="CN1")
    s.text(cx + 62, BY + 30, "CN1", size=12)
    s.text(cx, BY + 80, "USB ST-LINK", size=11)
    # El ST-LINK es tambien el VCP (S3): la USART1, PC4/PC5, sale por el USB
    s.abre("VCP")
    chip(s, cx - 60, BY + 110, 120, 80, "ST-LINK/V2", "STM32F103", size=13)
    s.cierra()
    # VCP#destino es un rotulo: mcu-sim escribe ahi el destino de verdad del
    # puente (el puerto, o «en memoria»), como en la Nucleo
    s.text(cx, BY + 208, "VCP: RFC 2217 en localhost:3355", size=10, fill="#1b2631", bold=True,
           id="VCP#destino")
    s.abre("MCO")
    s.rect(cx + 75, BY + 150, 36, 16, "#d0d3d4", "#7f8c8d", 1, rx=3)
    s.cierra()
    s.text(cx + 93, BY + 182, "MCO 8 MHz", size=10)
    led(s, "LD1", cx - 110, BY + 120, "#e74c3c")
    s.text(cx - 110, BY + 150, "LD1 PWR", size=10)
    s.text(cx - 110, BY + 100, "LD2 COM", size=10, fill="#7f8c8d")
    # JP3, el puente IDD: dos pines en vertical
    jx, jy = cx + 150, BY + 120
    s.abre("JP3")
    s.rect(jx - P / 2, jy - P / 2, P, 2 * P, NEGRO, "#000000", 1)
    pin(s, "JP3.1", jx, jy, 14)
    pin(s, "JP3.2", jx, jy + P, 14)
    s.abre("JP3@1-2")
    s.rect(jx - 10, jy - 10, 20, P + 20, "#2b2b2b", "#000000", 1.5, rx=4)
    s.rect(jx - 5, jy - 5, 10, P + 10, "#4a4a4a", None, rx=2)
    s.cierra()
    s.cierra()
    s.text(jx + 28, jy + 16, "JP3 IDD", size=11, anchor="start")
    s.line(BX + 120, BY + 220, BX + BW - 120, BY + 220, "#bdc3c7", 2, dash="8 6")

    # --- El chip, la rosa y los sensores ---------------------------------------
    my = BY + 470
    chip(s, cx - 70, my - 70, 140, 140, "STM32F303", "VCT6")
    # La rosa: LD3 al norte y en el sentido de las agujas del reloj
    rosa = (("LD3", "#e74c3c"), ("LD5", "#f39c12"), ("LD7", "#2ecc71"), ("LD9", "#3498db"),
            ("LD10", "#e74c3c"), ("LD8", "#f39c12"), ("LD6", "#2ecc71"), ("LD4", "#3498db"))
    import math
    R = 150
    for k, (id_, col) in enumerate(rosa):
        a = math.radians(-90 + 45 * k)
        x, y = cx + R * math.cos(a), my + R * math.sin(a)
        led(s, id_, x, y, col)
        s.text(cx + (R - 40) * math.cos(a), my + (R - 40) * math.sin(a) + 4, id_, size=11)
    # Los sensores (S4): U5 y U3, y debajo el nombre del chip de la revision
    # que se monta, como variante (U5@L3GD20 / U5@I3G4250D ...)
    s.abre("U5")
    chip(s, cx - 150, my + 200, 60, 60, "U5", size=11)
    s.cierra()
    for v in ("L3GD20", "I3G4250D"):
        s.text(cx - 120, my + 280, v, size=10, fill="#7f8c8d", id="U5@" + v)
    s.abre("U3")
    chip(s, cx + 90, my + 200, 60, 60, "U3", size=11)
    s.cierra()
    for v in ("LSM303DLHC", "LSM303AGR"):
        s.text(cx + 120, my + 280, v, size=10, fill="#7f8c8d", id="U3@" + v)
    # X3, el cristal de 32 kHz: SIN MONTAR (huella vacia)
    s.abre("X3")
    s.rect(cx + 50, BY + 250, 60, 24, "none", "#7f8c8d", 1.5, rx=12)
    s.cierra()
    s.text(cx + 80, BY + 292, "X3 (sin montar)", size=10, fill="#7f8c8d")

    # --- Abajo: los pulsadores y el USB de usuario -----------------------------
    by = BY + BH - 120
    boton(s, "B1", cx - 150, by, color="#2e86c1")
    s.text(cx - 150, by + 50, "B1 USER", size=12)
    boton(s, "B2", cx + 150, by, color="#1a1a1a")
    s.text(cx + 150, by + 50, "B2 RESET", size=12)
    usb_mini(s, cx, BY + BH - 52, ancho=70, alto=60)
    s.text(cx, by - 10, "USB USER", size=11)

    # --- Las tiras ----------------------------------------------------------
    y0 = BY + 160
    tira(s, "EXP1", BX + 34, BX + 34 + P, y0, True)
    tira(s, "EXP2", BX + BW - 34, BX + BW - 34 - P, y0, False)
    s.text(BX + 46, y0 - 30, "P1", size=14, bold=True)
    s.text(BX + BW - 46, y0 - 30, "P2", size=14, bold=True)
    s.guarda("stm32f3discovery.svg")


discovery()
