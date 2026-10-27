#!/usr/bin/env python3
# =============================================================================
# dibuja_motores.py — Genera los dibujos del sistema de los dos motores:
#                     l298n.svg, motor_tt_encoder.svg y bateria_2x18650.svg
#
# Los SVG estan versionados; esto es como se hicieron, para rehacerlos si
# cambia una placa. Dibujados a mano -una aproximacion, no una foto-, con el
# perfil de autor de mcu-sim-gui (doc/analisis-uso-ilustraciones.md §11): SVG
# 1.2 Tiny, geometria como atributo, tamano en milimetros. Una unidad es
# 0,1 mm. Cada pieza visible del XML y cada pin de conector llevan su id, y
# cada posicion de un jumper es un elemento PIEZA@valor dentro de la pieza
# (doc/parts.md §2.6). `make gui-sistema`, en su grupo C21, comprueba que no
# falta ninguno.
#
#   python3 placas/dibuja_motores.py [CARPETA]     (desde src/; por omision, placas/)
# =============================================================================
import math
import os
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else AQUI
P = 25.4  # paso de 0,1"

ORO, ORO_B = "#d4ac0d", "#9a7d0a"
BLANCO = "#f5f5f5"


def f(x):
    return ("%.1f" % x).rstrip("0").rstrip(".")


class Svg:
    def __init__(self, w, h, comentario):
        self.w, self.h, self.c, self.e = w, h, comentario, []

    def add(self, s):
        self.e.append(s)

    def rect(self, x, y, w, h, fill, stroke=None, sw=1.5, rx=0, id=None, extra=""):
        a = ' id="%s"' % id if id else ""
        s = ' stroke="%s" stroke-width="%s"' % (stroke, f(sw)) if stroke else ""
        r = ' rx="%s"' % f(rx) if rx else ""
        return '<rect%s x="%s" y="%s" width="%s" height="%s"%s fill="%s"%s%s/>' % (
            a, f(x), f(y), f(w), f(h), r, fill, s, extra)

    def circ(self, cx, cy, r, fill, stroke=None, sw=1.5, id=None, extra=""):
        a = ' id="%s"' % id if id else ""
        s = ' stroke="%s" stroke-width="%s"' % (stroke, f(sw)) if stroke else ""
        return '<circle%s cx="%s" cy="%s" r="%s" fill="%s"%s%s/>' % (a, f(cx), f(cy), f(r), fill, s, extra)

    def text(self, x, y, t, size=10, fill=BLANCO, anchor="start", bold=True, extra=""):
        b = ' font-weight="bold"' if bold else ""
        an = ' text-anchor="%s"' % anchor if anchor != "start" else ""
        return '<text x="%s" y="%s" font-family="Arial" font-size="%s"%s fill="%s"%s%s>%s</text>' % (
            f(x), f(y), f(size), b, fill, an, extra, t)

    def guarda(self, nombre):
        s = ['<?xml version="1.0" encoding="UTF-8"?>', "<!--", self.c.rstrip(), "-->",
             '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%smm" height="%smm">'
             % (self.w, self.h, f(self.w / 10), f(self.h / 10))]
        s += ["  " + x for x in self.e]
        s.append("</svg>")
        with open(os.path.join(OUT, nombre), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(s) + "\n")


def tornillo(s, id_, cx, cy):
    """El tornillo de una borna: ahi engancha el hilo"""
    s.add('<g id="%s">' % id_)
    s.add("  " + s.circ(cx, cy, 17, "#cfd8dc", "#78909c", 1.5))
    s.add("  " + s.rect(cx - 12, cy - 2.5, 24, 5, "#546e7a", rx=1))
    s.add("</g>")


def pin_tira(s, id_, cx, cy):
    """Un pin derecho de una tira: el cuadrado dorado"""
    s.add(s.rect(cx - 6, cy - 6, 12, 12, ORO, ORO_B, 0.8, id=id_))


def puente(s, id_, x0, x1, cy):
    """El puente de plastico de un jumper, sobre dos pines vecinos"""
    s.add('<g id="%s">' % id_)
    s.add("  " + s.rect(x0 - 10, cy - 9, x1 - x0 + 20, 18, "#0a0a0a", "#e8e8e8", 2, rx=3,
                        extra=' fill-opacity="0.45"'))
    s.add("  " + s.rect(x0, cy - 2.5, x1 - x0, 5, "#e0e0e0"))
    s.add("</g>")


# ---------------------------------------------------------------------------
# EL MODULO L298N
# ---------------------------------------------------------------------------
def l298n():
    s = Svg(430, 430, """
  MODULO L298N, DOBLE PUENTE EN H, dibujado a mano: una aproximacion, no una
  foto. Es el dibujo de placas/l298n.xml, que lo encuentra solo porque se
  llama como ella. Generado por placas/dibuja_motores.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: el modulo mide 43 x 43 mm- y cada pieza con el id de su pieza en el
  XML:

    U1    el L298 bajo su disipador, arriba
    U2    el regulador 78M05
    D1    el LED rojo de los 5 V, que luce con la logica
    MA, MB, X1   las tres bornas azules: cada tornillo es su pin (MA.OUT1,
          MA.OUT2; MB.OUT3, MB.OUT4; X1.12V, X1.GND, X1.5V). Un hilo engancha
          en el tornillo
    JA, P1, JB   la tira de ocho pines de abajo: JA.5V y JA.ENA, P1.IN1 a
          P1.IN4, JB.ENB y JB.5V. Los puentes de ENA y ENB son VARIANTES
          (JA@5V-ENA, JB@ENB-5V): mcu-sim deja la que dice el jumper, o
          ninguna si se ha quitado
    JP5   el puente 5V-EN, con su variante JP5@REG-5V
""")
    s.add(s.rect(0, 0, 430, 430, "#b71c1c", "#7f0000", 2, rx=12))
    for x, y in ((24, 24), (406, 24), (24, 406), (406, 406)):
        s.add(s.circ(x, y, 15, "#cfcfcf", "#8a8a8a", 1.5))
        s.add(s.circ(x, y, 8, "#3a0000"))
    # El L298 bajo el disipador, con sus quince patas
    s.add('<g id="U1">')
    s.add("  " + s.rect(112, 6, 206, 168, "#1c1c1c", "#000000", 1.5, rx=4))
    for k in range(10):
        s.add("  " + s.rect(124 + k * 19, 12, 8, 156, "#2f2f2f"))
    s.add("  " + s.rect(150, 174, 130, 22, "#111111"))
    for k in range(15):
        s.add("  " + s.rect(154 + k * 8.4, 196, 3.5, 16 + (k % 2) * 8, "#bdbdbd"))
    s.add("  " + s.text(215, 189, "L298N", 11, "#e0e0e0", "middle"))
    s.add("</g>")
    # Dos condensadores y el regulador
    for cx in (128, 302):
        s.add(s.circ(cx, 262, 24, "#212121", "#000000", 1.5))
        s.add(s.circ(cx, 262, 18, "#37474f"))
        s.add(s.rect(cx - 2, 240, 4, 44, "#90a4ae"))
    s.add('<g id="U2">')
    s.add("  " + s.rect(193, 236, 44, 12, "#bdbdbd", "#757575", 1))
    s.add("  " + s.rect(188, 246, 54, 40, "#1a1a1a", "#000000", 1, rx=2))
    s.add("  " + s.text(215, 270, "78M05", 9, "#e0e0e0", "middle"))
    s.add("</g>")
    s.add(s.text(215, 302, "U2", 8, BLANCO, "middle", False))
    # El LED de los 5 V
    s.add('<g id="D1">')
    s.add("  " + s.rect(140, 318, 18, 10, "#ff5252", "#7f0000", 1, rx=2))
    s.add("</g>")
    s.add(s.text(149, 312, "D1", 8, BLANCO, "middle", False))
    # Las bornas de los motores
    s.add(s.text(38, 170, "MOTOR A", 9, BLANCO, "middle"))
    s.add(s.rect(8, 178, 60, 114, "#1565c0", "#0d47a1", 1.5, rx=3))
    s.add(s.rect(8, 192, 6, 30, "#0a2a5a"))
    s.add(s.rect(8, 248, 6, 30, "#0a2a5a"))
    s.add('<g id="MA">')
    tornillo(s, "MA.OUT1", 40, 207)
    tornillo(s, "MA.OUT2", 40, 263)
    s.add("</g>")
    s.add(s.text(74, 211, "OUT1", 9))
    s.add(s.text(74, 267, "OUT2", 9))
    s.add(s.text(392, 170, "MOTOR B", 9, BLANCO, "middle"))
    s.add(s.rect(362, 178, 60, 114, "#1565c0", "#0d47a1", 1.5, rx=3))
    s.add(s.rect(416, 192, 6, 30, "#0a2a5a"))
    s.add(s.rect(416, 248, 6, 30, "#0a2a5a"))
    s.add('<g id="MB">')
    tornillo(s, "MB.OUT3", 390, 207)
    tornillo(s, "MB.OUT4", 390, 263)
    s.add("</g>")
    s.add(s.text(356, 211, "OUT3", 9, BLANCO, "end"))
    s.add(s.text(356, 267, "OUT4", 9, BLANCO, "end"))
    # La borna de la alimentacion
    s.add(s.rect(46, 364, 172, 58, "#1565c0", "#0d47a1", 1.5, rx=3))
    s.add('<g id="X1">')
    for nombre, x in (("12V", 74), ("GND", 132), ("5V", 190)):
        tornillo(s, "X1." + nombre, x, 393)
    s.add("</g>")
    for t, x in (("+12V", 74), ("GND", 132), ("+5V", 190)):
        s.add(s.text(x, 357, t, 9, BLANCO, "middle"))
    # El 5V-EN
    s.add('<g id="JP5">')
    s.add("  " + s.rect(48, 310, 50.4 + 12, 22, "#1c1c1c", "#000000", 1, rx=2))
    pin_tira(s, "JP5.REG", 60, 321)
    pin_tira(s, "JP5.5V", 60 + P, 321)
    puente(s, "JP5@REG-5V", 60, 60 + P, 321)
    s.add("</g>")
    s.add(s.text(73, 303, "5V-EN", 8, BLANCO, "middle"))
    # La tira de abajo: 5V ENA | IN1..IN4 | ENB 5V
    x0, yt = 234.0, 362.0
    xs = [x0 + k * P for k in range(8)]
    s.add(s.rect(xs[0] - 13, yt - 12, xs[7] - xs[0] + 26, 24, "#1c1c1c", "#000000", 1, rx=2))
    nombres = ["5V", "ENA", "IN1", "IN2", "IN3", "IN4", "ENB", "5V"]
    for k, t in enumerate(nombres):
        s.add(s.text(xs[k], yt - 18, t, 7.5, BLANCO, "middle"))
    s.add('<g id="JA">')
    pin_tira(s, "JA.5V", xs[0], yt)
    pin_tira(s, "JA.ENA", xs[1], yt)
    puente(s, "JA@5V-ENA", xs[0], xs[1], yt)
    s.add("</g>")
    s.add('<g id="P1">')
    for k in range(4):
        pin_tira(s, "P1.IN%d" % (k + 1), xs[2 + k], yt)
    s.add("</g>")
    s.add('<g id="JB">')
    pin_tira(s, "JB.ENB", xs[6], yt)
    pin_tira(s, "JB.5V", xs[7], yt)
    puente(s, "JB@ENB-5V", xs[6], xs[7], yt)
    s.add("</g>")
    s.add(s.text(330, 410, "L298N", 16, BLANCO, "middle"))
    s.guarda("l298n.svg")


# ---------------------------------------------------------------------------
# EL MOTORREDUCTOR TT CON SU ENCODER
# ---------------------------------------------------------------------------
def motor():
    s = Svg(940, 480, """
  MOTORREDUCTOR TT CON UN ENCODER DE 24 PPR, dibujado a mano: una
  aproximacion, no una foto. Es el dibujo de placas/motor_tt_encoder.xml, que
  lo encuentra solo porque se llama como ella. Generado por
  placas/dibuja_motores.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 94 x 48 mm- y cada pieza con el id de su pieza en el XML. Visto
  desde el lado del encoder, con el eje de salida hacia quien mira: la caja
  amarilla de la reductora, el motor plateado a la derecha con sus dos
  terminales, y en el eje el DISCO del encoder con su plaquita arriba.

    MOTOR  el disco de 24 ventanas, que GIRA con el angulo del eje (el
           efecto `angulo`): dibujado en el 0, con la marca blanca arriba, y
           positivo es a la derecha. Lleva dentro un circulo sin pintar
           centrado en el eje que alcanza mas que el disco: el centro de su
           caja es el eje, y gira sobre el
    M      los terminales del motor, con su hilo: M.ROJO (+) y M.NEGRO (-)
    P1     la tira de la plaquita del encoder: P1.VCC, P1.GND, P1.A, P1.B
""")
    cx, cy, r = 190.0, 262.0, 150.0
    # La caja de la reductora y el motor
    s.add(s.rect(70, 150, 560, 224, "#f4c20d", "#b8860b", 2, rx=22))
    s.add(s.rect(84, 164, 532, 196, "none", "#d4a20a", 1.2, rx=16))
    for x, y in ((560, 182), (560, 342), (430, 182), (430, 342)):
        s.add(s.circ(x, y, 9, "#b8860b"))
        s.add(s.circ(x, y, 4, "#5d4300"))
    s.add(s.text(500, 270, "TT 1:48", 18, "#5d4300", "middle"))
    s.add(s.rect(630, 166, 232, 192, "#cfd8dc", "#78909c", 2, rx=6))
    for k in range(5):
        s.add(s.rect(650 + k * 40, 166, 6, 192, "#b0bec5"))
    s.add(s.rect(862, 186, 26, 152, "#263238", "#000000", 1.5, rx=4))
    s.add(s.rect(888, 214, 16, 8, "#bdbdbd"))
    s.add(s.rect(888, 302, 16, 8, "#bdbdbd"))
    # Los hilos y los terminales
    s.add('<g id="M">')
    s.add('  <g id="M.ROJO">')
    s.add("    " + s.rect(904, 213, 14, 10, "#d32f2f"))
    s.add("    " + s.circ(920, 218, 11, "#d32f2f", "#7f0000", 1.2))
    s.add("  </g>")
    s.add('  <g id="M.NEGRO">')
    s.add("    " + s.rect(904, 301, 14, 10, "#212121"))
    s.add("    " + s.circ(920, 306, 11, "#212121", "#000000", 1.2))
    s.add("  </g>")
    s.add("</g>")
    s.add(s.text(918, 196, "+", 16, "#d32f2f", "middle"))
    s.add(s.text(918, 340, "-", 18, "#212121", "middle"))
    # La plaquita del encoder, arriba, con su horquilla sobre el borde del disco
    s.add(s.rect(96, 8, 188, 92, "#1565c0", "#0d47a1", 1.5, rx=4))
    s.add(s.rect(150, 86, 24, 46, "#212121", "#000000", 1, rx=2))
    s.add(s.rect(206, 86, 24, 46, "#212121", "#000000", 1, rx=2))
    s.add(s.rect(116, 28, 4 * P + 2, 24, "#1c1c1c", "#000000", 1, rx=2))
    s.add('<g id="P1">')
    for k, n in enumerate(("VCC", "GND", "A", "B")):
        pin_tira(s, "P1." + n, 129 + k * P, 40)
    s.add("</g>")
    for k, n in enumerate(("VCC", "GND", "A", "B")):
        s.add(s.text(129 + k * P, 70, n, 8, BLANCO, "middle"))
    s.add(s.text(270, 94, "24 PPR", 8, BLANCO, "end"))
    # El disco, que gira
    s.add('<g id="MOTOR">')
    s.add("  " + s.circ(cx, cy, r + 8, "none", "none"))
    s.add("  " + s.circ(cx, cy, r, "#263238", "#101820", 2))
    for k in range(24):
        a = k * 15
        s.add("  " + s.rect(cx - 9, cy - r + 10, 18, 30, "#cfd8dc",
                            extra=' transform="rotate(%s %s %s)"' % (f(a), f(cx), f(cy))))
    s.add("  " + s.circ(cx, cy, r - 50, "#37474f", "#101820", 1))
    s.add("  " + s.circ(cx, cy, 30, "#9e9e9e", "#616161", 1.5))
    s.add("  " + s.rect(cx - 9, cy - 9, 18, 18, "#f4c20d", "#b8860b", 1))
    s.add("  " + s.circ(cx, cy - r + 64, 9, BLANCO))
    s.add("  " + s.rect(cx - 3, cy - r + 72, 6, 36, BLANCO))
    s.add("</g>")
    s.guarda("motor_tt_encoder.svg")


# ---------------------------------------------------------------------------
# EL PORTAPILAS
# ---------------------------------------------------------------------------
def bateria():
    s = Svg(800, 440, """
  PORTAPILAS DE DOS 18650 EN SERIE, dibujado a mano: una aproximacion, no una
  foto. Es el dibujo de placas/bateria_2x18650.xml, que lo encuentra solo
  porque se llama como ella. Generado por placas/dibuja_motores.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 80 x 44 mm- y cada pieza con el id de su pieza en el XML:

    BAT    la etiqueta de 7,4 V, entre las dos celdas: la Fuente. Deja ver
           lo que entrega
    P1     los dos hilos: P1.BAT, el rojo, y P1.GND, el negro
""")
    s.add(s.rect(10, 10, 690, 420, "#212121", "#000000", 2, rx=10))
    for k, (y, color, pos_der) in enumerate(((36, "#1e88e5", True), (234, "#43a047", False))):
        s.add("  " + s.rect(56, y, 600, 170, color, "#0b2545", 1.5, rx=30))
        s.add("  " + s.rect(56, y + 18, 600, 8, "#ffffff", extra=' fill-opacity="0.25"'))
        xn = 660 if pos_der else 40
        s.add("  " + s.rect(xn, y + 60, 16, 50, "#cfd8dc", "#78909c", 1, rx=3))
        s.add("  " + s.text(356, y + 98, "18650  3,7 V", 22, BLANCO, "middle"))
        s.add("  " + s.text(620 if pos_der else 92, y + 100, "+", 26, BLANCO, "middle"))
    # La etiqueta de la bateria: la Fuente, que deja ver lo que entrega
    s.add('<g id="BAT">')
    s.add("  " + s.rect(296, 200, 120, 40, "#fdd835", "#212121", 1.5, rx=4))
    s.add("  " + s.text(356, 228, "7,4 V", 20, "#212121", "middle"))
    s.add("</g>")
    for y in (36, 234):
        s.add(s.rect(20, y + 40, 18, 90, "#b0bec5"))
        s.add(s.rect(678, y + 40, 18, 90, "#b0bec5"))
    s.add(s.rect(700, 92, 50, 10, "#d32f2f"))
    s.add(s.rect(700, 330, 50, 10, "#212121", "#555555", 1))
    s.add('<g id="P1">')
    s.add('  <g id="P1.BAT">')
    s.add("    " + s.circ(762, 97, 13, "#d32f2f", "#7f0000", 1.2))
    s.add("  </g>")
    s.add('  <g id="P1.GND">')
    s.add("    " + s.circ(762, 335, 13, "#212121", "#000000", 1.2))
    s.add("  </g>")
    s.add("</g>")
    s.add(s.text(762, 136, "+7,4 V", 12, "#d32f2f", "middle"))
    s.add(s.text(762, 314, "GND", 12, "#212121", "middle"))
    s.guarda("bateria_2x18650.svg")


if __name__ == "__main__":
    l298n()
    motor()
    bateria()
