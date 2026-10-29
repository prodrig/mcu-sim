#!/usr/bin/env python3
# =============================================================================
# dibuja_cnc.py — Genera los dibujos de las piezas de la fresadora CNC
#                 (doc/analisis_cnc.md): los motores, sus drivers y sus módulos
#
# Los SVG estan versionados; esto es como se hicieron, para rehacerlos si
# cambia una placa. Dibujados a mano -una aproximacion, no una foto-, con el
# perfil de autor de mcu-sim-gui (doc/analisis-uso-ilustraciones.md §11): SVG
# 1.2 Tiny, geometria como atributo, tamano en milimetros. Una unidad es
# 0,1 mm. Cada pieza visible del XML y cada pin de conector llevan su id
# (doc/parts.md §2.6); `make gui-sistema`, en su grupo C21, comprueba que no
# falta ninguno. Usa lo comun de dibuja_motores.py.
#
#   python3 placas/dibuja_cnc.py [CARPETA]     (desde src/; por omision, placas/)
# =============================================================================
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dibuja_motores as dm                      # noqa: E402
from dibuja_motores import Svg, f, pin_tira, tornillo, ORO, ORO_B, BLANCO, P   # noqa: E402

if len(sys.argv) > 1:
    dm.OUT = sys.argv[1]

GRIS_MOTOR, GRIS_MOTOR_B = "#455a64", "#263238"


def cara_nema(s, cx, cy, lado, tornillos, boss, eje, nombre, id_eje="MOTOR", marca="#f5f5f5"):
    """La cara de un motor NEMA vista de frente: el cuadrado con sus cuatro
    agujeros, el resalte redondo y el EJE, que gira (efecto `angulo`): lleva
    un circulo sin pintar centrado en el, para que el centro de su caja sea el
    eje, y su plano -la marca- arriba en el 0"""
    h = lado / 2
    s.add(s.rect(cx - h, cy - h, lado, lado, GRIS_MOTOR, GRIS_MOTOR_B, 2, rx=lado * 0.06))
    s.add(s.rect(cx - h + 8, cy - h + 8, lado - 16, lado - 16, "none", "#607d8b", 1.2,
                 rx=lado * 0.05))
    d = tornillos / 2
    for sx in (-1, 1):
        for sy in (-1, 1):
            s.add(s.circ(cx + sx * d, cy + sy * d, lado * 0.045, "#1c1c1c", "#000000", 1))
    s.add(s.circ(cx, cy, boss / 2, "#78909c", "#37474f", 1.5))
    s.add(s.circ(cx, cy, boss / 2 - 10, "#90a4ae"))
    s.add('<g id="%s">' % id_eje)
    s.add("  " + s.circ(cx, cy, boss / 2 + 4, "none", "none"))
    s.add("  " + s.circ(cx, cy, eje / 2, "#cfd8dc", "#546e7a", 1.5))
    s.add("  " + s.rect(cx - eje * 0.32, cy - eje / 2 + 1, eje * 0.64, eje * 0.22, "#78909c"))
    s.add("  " + s.rect(cx - 3, cy - boss / 2 + 6, 6, boss / 2 - eje / 2 - 8, marca))
    s.add("</g>")
    s.add(s.text(cx, cy + h - 18, nombre, 13, "#cfd8dc", "middle"))


def hilos_motor(s, x0, y0, colores, nombres, id_con="M", paso=60):
    """Los cuatro hilos de un paso a paso, con su terminal: cada terminal es el
    pin de su conector"""
    s.add('<g id="%s">' % id_con)
    for k, (col, n) in enumerate(zip(colores, nombres)):
        y = y0 + k * paso
        s.add('  <g id="%s.%s">' % (id_con, n))
        s.add("    " + s.rect(x0, y - 4, 40, 8, col))
        s.add("    " + s.circ(x0 + 50, y, 12, col, "#000000", 1.2))
        s.add("  </g>")
    s.add("</g>")
    for k, n in enumerate(nombres):
        s.add(s.text(x0 + 70, y0 + k * paso + 5, n, 13, "#263238"))


# ---------------------------------------------------------------------------
# EL NEMA 17 DE LA 3018
# ---------------------------------------------------------------------------
def nema17():
    s = Svg(700, 500, """
  MOTOR PASO A PASO NEMA 17 DE LA FRESADORA 3018, dibujado a mano: una
  aproximacion, no una foto. Es el dibujo de placas/nema17_3018.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 70 x 50 mm- y cada pieza con el id de su pieza en el XML. Visto de
  frente, con el eje hacia quien mira: la cara de 42 mm y sus cuatro hilos.

    MOTOR  el EJE, que gira con el angulo del rotor (el efecto `angulo`):
           dibujado en el 0, con la marca blanca arriba, y positivo es a la
           derecha. Lleva un circulo sin pintar centrado en el eje
    M      los cuatro hilos: M.A1 (negro), M.A2 (verde), M.B1 (rojo) y
           M.B2 (azul), con su terminal
""")
    s.add(s.rect(0, 0, 700, 500, "#eceff1", rx=10))
    cara_nema(s, 240, 250, 420, 310, 220, 50, "NEMA 17  1,8°")
    hilos_motor(s, 500, 160, ("#212121", "#2e7d32", "#c62828", "#1565c0"),
                ("A1", "A2", "B1", "B2"))
    s.add(s.text(560, 450, "0,25 N·m  1,3 A", 13, "#263238", "middle"))
    s.guarda("nema17_3018.svg")


# ---------------------------------------------------------------------------
# EL ST5918L3008 DE NANOTEC
# ---------------------------------------------------------------------------
def st5918():
    s = Svg(800, 600, """
  MOTOR PASO A PASO NANOTEC ST5918L3008-A (NEMA 23), dibujado a mano: una
  aproximacion, no una foto. Es el dibujo de placas/st5918l3008.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 80 x 60 mm- y cada pieza con el id de su pieza en el XML. Visto de
  frente: la cara de 56 mm y sus cuatro hilos, ya unidos en paralelo los
  ocho del motor.

    MOTOR  el EJE, que gira con el angulo del rotor (el efecto `angulo`)
    M      los cuatro hilos: M.A1, M.A2, M.B1 y M.B2
""")
    s.add(s.rect(0, 0, 800, 600, "#eceff1", rx=10))
    cara_nema(s, 290, 300, 540, 474, 380, 64, "ST5918L3008  NEMA 23")
    hilos_motor(s, 590, 200, ("#212121", "#2e7d32", "#c62828", "#1565c0"),
                ("A1", "A2", "B1", "B2"), paso=70)
    s.add(s.text(660, 560, "1,87 N·m  4,2 A paralelo", 13, "#263238", "middle"))
    s.guarda("st5918l3008.svg")


# ---------------------------------------------------------------------------
# EL MODULO POLOLU DEL A4988
# ---------------------------------------------------------------------------
def pololu_a4988():
    s = Svg(204, 152 + 60, """
  MODULO POLOLU DEL A4988 (driver de paso a paso), dibujado a mano: una
  aproximacion, no una foto. Es el dibujo de placas/pololu_a4988.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: el modulo mide 20,3 x 15,2 mm, y debajo van sus rotulos- y cada
  pieza con el id de su pieza en el XML. Visto desde arriba, con los pines de
  la logica a la izquierda:

    U1     el A4988, en el centro
    (el potenciometro de Vref es el mando `vref` de U1: se dibuja, sin id)
    R1     la resistencia de 10 kohm de SLEEP a VDD
    J1     la tira de la izquierda: EN, MS1, MS2, MS3, RST, SLP, STEP, DIR
    J2     la de la derecha: VMOT, GND1, 2B, 2A, 1A, 1B, VLOG (el VDD del
           serigrafiado: la logica) y GND2
""")
    s.add(s.rect(0, 0, 204, 152, "#1b5e20", "#0d3311", 1.2, rx=4))
    # Las dos tiras de ocho, a los lados. En el modulo de verdad van a 2,54 mm;
    # aqui, a 1,6 mm, para que quepan en su placa a escala
    s.add('<g id="J1">')
    for k, n in enumerate(("EN", "MS1", "MS2", "MS3", "RST", "SLP", "STEP", "DIR")):
        pin_tira(s, "J1." + n, 12, 76 - 3.5 * 16 + k * 16)
    s.add("</g>")
    s.add('<g id="J2">')
    for k, n in enumerate(("VMOT", "GND1", "2B", "2A", "1A", "1B", "VLOG", "GND2")):
        pin_tira(s, "J2." + n, 192, 76 - 3.5 * 16 + k * 16)
    s.add("</g>")
    for k, n in enumerate(("EN", "MS1", "MS2", "MS3", "RST", "SLP", "STP", "DIR")):
        s.add(s.text(22, 76 - 3.5 * 16 + k * 16 + 3, n, 6.5, BLANCO, bold=False))
    for k, n in enumerate(("VMT", "GND", "2B", "2A", "1A", "1B", "VDD", "GND")):
        s.add(s.text(182, 76 - 3.5 * 16 + k * 16 + 3, n, 6.5, BLANCO, "end", bold=False))
    s.add('<g id="U1">')
    s.add("  " + s.rect(72, 52, 50, 50, "#1c1c1c", "#000000", 1, rx=2))
    s.add("  " + s.text(97, 80, "A4988", 8, "#e0e0e0", "middle"))
    s.add("</g>")
    s.add(s.circ(104, 124, 13, "#cfd8dc", "#78909c", 1))
    s.add(s.rect(102, 113, 4, 22, "#546e7a"))
    s.add('<g id="R1">')
    s.add("  " + s.rect(70, 116, 16, 8, "#212121", "#9e9e9e", 0.6))
    s.add("</g>")
    s.add(s.text(102, 190, "POLOLU A4988", 12, "#263238", "middle"))
    s.guarda("pololu_a4988.svg")


def bornes(s, id_con, x, y0, nombres, colores, paso=80, ancho=60, izq=False):
    """Una borna de tornillo de varias vias, en vertical: cada tornillo es su pin"""
    s.add(s.rect(x - ancho / 2, y0 - 40, ancho, paso * len(nombres), "#1565c0", "#0d47a1", 1.5, rx=3))
    s.add('<g id="%s">' % id_con)
    for k, n in enumerate(nombres):
        tornillo(s, "%s.%s" % (id_con, n), x, y0 + k * paso)
    s.add("</g>")
    for k, (n, col) in enumerate(zip(nombres, colores)):
        if izq:
            s.add(s.text(x - ancho / 2 - 8, y0 + k * paso + 5, n, 14, col, "end"))
        else:
            s.add(s.text(x + ancho / 2 + 8, y0 + k * paso + 5, n, 14, col))


def disco_motor(s, cx, cy, r, ventanas, color="#263238"):
    """El disco o el ventilador del eje, que GIRA (efecto `angulo`), con su marca"""
    s.add('<g id="MOTOR">')
    s.add("  " + s.circ(cx, cy, r + 8, "none", "none"))
    s.add("  " + s.circ(cx, cy, r, color, "#101820", 2))
    for k in range(ventanas):
        a = k * 360.0 / ventanas
        s.add("  " + s.rect(cx - 7, cy - r + 8, 14, r * 0.35, "#90a4ae",
                            extra=' transform="rotate(%s %s %s)"' % (f(a), f(cx), f(cy))))
    s.add("  " + s.circ(cx, cy, r * 0.25, "#9e9e9e", "#616161", 1.5))
    s.add("  " + s.rect(cx - 4, cy - r + 4, 8, r * 0.7, BLANCO))
    s.add("</g>")


# ---------------------------------------------------------------------------
# EL CABEZAL: EL MOTOR 775
# ---------------------------------------------------------------------------
def motor_775():
    s = Svg(900, 460, """
  MOTOR 775 DE 24 V, EL CABEZAL, dibujado a mano: una aproximacion, no una
  foto. Es el dibujo de placas/motor_775.xml, que lo encuentra solo porque se
  llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 90 x 46 mm- y cada pieza con el id de su pieza en el XML. De lado:
  la carcasa de 42 mm, el ventilador visto desde la fresa a la izquierda y
  las dos bornas atras.

    MOTOR  el ventilador del eje, que GIRA con el angulo (efecto `angulo`)
    M      los terminales: M.ROJO (+) y M.NEGRO (-)
""")
    s.add(s.rect(250, 90, 470, 280, "#b0bec5", "#546e7a", 2, rx=18))
    for k in range(6):
        s.add(s.rect(300 + k * 60, 90, 10, 280, "#90a4ae"))
    s.add(s.rect(720, 140, 40, 180, "#455a64", "#263238", 1.5, rx=6))
    s.add(s.text(485, 245, "775  24 V", 22, "#263238", "middle"))
    disco_motor(s, 140, 230, 120, 9, "#37474f")
    s.add('<g id="M">')
    s.add('  <g id="M.ROJO">')
    s.add("    " + s.rect(760, 180, 50, 14, "#c62828"))
    s.add("    " + s.circ(820, 187, 14, "#c62828", "#7f0000", 1.2))
    s.add("  </g>")
    s.add('  <g id="M.NEGRO">')
    s.add("    " + s.rect(760, 266, 50, 14, "#212121"))
    s.add("    " + s.circ(820, 273, 14, "#212121", "#000000", 1.2))
    s.add("  </g>")
    s.add("</g>")
    s.add(s.text(850, 193, "+", 22, "#c62828", "middle"))
    s.add(s.text(850, 281, "-", 26, "#212121", "middle"))
    s.guarda("motor_775.svg")


# ---------------------------------------------------------------------------
# EL MODULO MOSFET DEL CABEZAL
# ---------------------------------------------------------------------------
def modulo_mosfet():
    s = Svg(500, 340, """
  MODULO MOSFET DEL CABEZAL, dibujado a mano: una aproximacion, no una foto.
  Es el dibujo de placas/modulo_mosfet.xml, que lo encuentra solo porque se
  llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 50 x 34 mm- y cada pieza con el id de su pieza en el XML:

    Q1     el MOSFET, en su TO-220, en el centro
    R1     la resistencia de la puerta a masa
    X1     la borna de la alimentacion: X1.VIN (+) y X1.GNDP (-)
    X2     la del motor: X2.MAS (+) y X2.MENOS (-)
    P1     la senal: P1.SIG y P1.GND
""")
    s.add(s.rect(0, 0, 500, 340, "#0d47a1", "#08306b", 2, rx=10))
    bornes(s, "X1", 60, 90, ("VIN", "GNDP"), (BLANCO, BLANCO))
    bornes(s, "X2", 440, 90, ("MAS", "MENOS"), (BLANCO, BLANCO), izq=True)
    s.add('<g id="Q1">')
    s.add("  " + s.rect(190, 40, 120, 60, "#9e9e9e", "#616161", 1.5, rx=4))
    s.add("  " + s.circ(250, 70, 14, "#424242"))
    s.add("  " + s.rect(190, 100, 120, 110, "#1c1c1c", "#000000", 1.5, rx=3))
    for k in range(3):
        s.add("  " + s.rect(205 + k * 40, 210, 10, 40, "#bdbdbd"))
    s.add("  " + s.text(250, 165, "IRLZ44N", 14, "#e0e0e0", "middle"))
    s.add("</g>")
    s.add('<g id="R1">')
    s.add("  " + s.rect(120, 270, 40, 18, "#212121", "#9e9e9e", 0.8))
    s.add("</g>")
    s.add(s.rect(200, 262, 30, 34, "#212121", "#000000", 1, rx=3))
    s.add(s.text(215, 316, "D1", 11, BLANCO, "middle", False))
    s.add(s.rect(300, 290, 2 * P + 12, 26, "#1c1c1c", "#000000", 1, rx=2))
    s.add('<g id="P1">')
    pin_tira(s, "P1.SIG", 318, 303)
    pin_tira(s, "P1.GND", 318 + P, 303)
    s.add("</g>")
    s.add(s.text(318, 284, "SIG", 11, BLANCO, "middle"))
    s.add(s.text(318 + P, 284, "GND", 11, BLANCO, "middle"))
    s.guarda("modulo_mosfet.svg")


# ---------------------------------------------------------------------------
# EL MODULO IBT-2
# ---------------------------------------------------------------------------
def ibt2():
    s = Svg(500, 520, """
  MODULO IBT-2, PUENTE EN H CON DOS BTS7960, dibujado a mano: una
  aproximacion, no una foto. Es el dibujo de placas/ibt2.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 50 x 52 mm- y cada pieza con el id de su pieza en el XML:

    U1, U2  los dos BTS7960, bajo el disipador: el de M+ y el de M-
    X1      la borna de la alimentacion: X1.BMAS (B+) y X1.BMENOS (B-)
    X2      la del motor: X2.MMAS (M+) y X2.MMENOS (M-)
    P1      la tira de ocho de la logica: RPWM, LPWM, R_EN, L_EN, R_IS, L_IS,
            VCC y GND
""")
    s.add(s.rect(0, 0, 500, 520, "#c62828", "#7f0000", 2, rx=10))
    s.add(s.rect(70, 30, 360, 220, "#263238", "#000000", 1.5, rx=6))
    for k in range(9):
        s.add(s.rect(84 + k * 38, 40, 18, 200, "#37474f"))
    s.add('<g id="U1">')
    s.add("  " + s.rect(110, 270, 110, 70, "#1c1c1c", "#000000", 1.5, rx=3))
    s.add("  " + s.text(165, 312, "BTS7960", 13, "#e0e0e0", "middle"))
    s.add("</g>")
    s.add('<g id="U2">')
    s.add("  " + s.rect(280, 270, 110, 70, "#1c1c1c", "#000000", 1.5, rx=3))
    s.add("  " + s.text(335, 312, "BTS7960", 13, "#e0e0e0", "middle"))
    s.add("</g>")
    s.add(s.text(165, 358, "R (M+)", 11, BLANCO, "middle"))
    s.add(s.text(335, 358, "L (M-)", 11, BLANCO, "middle"))
    # Las bornas, arriba a los lados
    s.add(s.rect(4, 40, 56, 160, "#1565c0", "#0d47a1", 1.5, rx=3))
    s.add('<g id="X1">')
    tornillo(s, "X1.BMAS", 32, 80)
    tornillo(s, "X1.BMENOS", 32, 160)
    s.add("</g>")
    s.add(s.text(32, 34, "B+  B-", 11, BLANCO, "middle"))
    s.add(s.rect(440, 40, 56, 160, "#1565c0", "#0d47a1", 1.5, rx=3))
    s.add('<g id="X2">')
    tornillo(s, "X2.MMAS", 468, 80)
    tornillo(s, "X2.MMENOS", 468, 160)
    s.add("</g>")
    s.add(s.text(468, 34, "M+  M-", 11, BLANCO, "middle"))
    # La tira de la logica, abajo
    nombres = ("RPWM", "LPWM", "R_EN", "L_EN", "R_IS", "L_IS", "VCC", "GND")
    x0 = 250 - 3.5 * P
    s.add(s.rect(x0 - 14, 440, 7 * P + 28, 28, "#1c1c1c", "#000000", 1, rx=2))
    s.add('<g id="P1">')
    for k, n in enumerate(nombres):
        pin_tira(s, "P1." + n, x0 + k * P, 454)
    s.add("</g>")
    for k, n in enumerate(nombres):
        s.add(s.text(x0 + k * P, 492, n.replace("_", ""), 7.5, BLANCO, "middle"))
    s.add(s.text(250, 420, "IBT-2  43 A", 14, BLANCO, "middle"))
    s.guarda("ibt2.svg")


# ---------------------------------------------------------------------------
# EL MOTORREDUCTOR DE 37 mm CON SU ENCODER
# ---------------------------------------------------------------------------
def motor_jgb37():
    s = Svg(1000, 420, """
  MOTORREDUCTOR JGB37-520 CON ENCODER HALL, dibujado a mano: una aproximacion,
  no una foto. Es el dibujo de placas/motor_jgb37_encoder.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 100 x 42 mm- y cada pieza con el id de su pieza en el XML. De lado:
  el eje de salida a la izquierda, la reductora dorada, el motor y, atras, la
  plaquita del encoder con su cable.

    MOTOR  el disco del eje de salida, visto de frente a la izquierda, que
           GIRA con el angulo (efecto `angulo`)
    M      los hilos del motor: M.ROJO (+) y M.BLANCO
    P1     los del encoder: P1.VCC, P1.GND, P1.A y P1.B
""")
    disco_motor(s, 140, 210, 110, 6, "#455a64")
    s.add(s.rect(270, 110, 260, 200, "#d4ac0d", "#9a7d0a", 2, rx=10))
    s.add(s.text(400, 215, "1:30", 20, "#5d4300", "middle"))
    s.add(s.rect(530, 120, 270, 180, "#b0bec5", "#546e7a", 2, rx=12))
    s.add(s.text(665, 215, "JGB37 12 V", 18, "#263238", "middle"))
    s.add(s.rect(800, 100, 40, 220, "#1b5e20", "#0d3311", 1.5, rx=4))
    hilos = (("M.ROJO", "#c62828"), ("M.BLANCO", "#eceff1"), ("P1.GND", "#212121"),
             ("P1.VCC", "#1565c0"), ("P1.A", "#f9a825"), ("P1.B", "#2e7d32"))
    s.add('<g id="M">')
    for k, (id_, col) in enumerate(hilos[:2]):
        y = 130 + k * 32
        s.add('  <g id="%s">' % id_)
        s.add("    " + s.rect(840, y - 4, 70, 8, col, "#000000", 0.6))
        s.add("    " + s.circ(920, y, 11, col, "#000000", 1.2))
        s.add("  </g>")
    s.add("</g>")
    s.add('<g id="P1">')
    for k, (id_, col) in enumerate(hilos[2:]):
        y = 130 + (k + 2) * 32
        s.add('  <g id="%s">' % id_)
        s.add("    " + s.rect(840, y - 4, 70, 8, col, "#000000", 0.6))
        s.add("    " + s.circ(920, y, 11, col, "#000000", 1.2))
        s.add("  </g>")
    s.add("</g>")
    for k, (id_, col) in enumerate(hilos):
        s.add(s.text(940, 130 + k * 32 + 5, id_.split(".")[1], 12, "#263238"))
    s.guarda("motor_jgb37_encoder.svg")


# ---------------------------------------------------------------------------
# EL BLDC DB59L024035-A
# ---------------------------------------------------------------------------
def db59():
    s = Svg(800, 600, """
  MOTOR BLDC NANOTEC DB59L024035-A (NEMA 23), dibujado a mano: una
  aproximacion, no una foto. Es el dibujo de placas/db59l024035_a.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 80 x 60 mm- y cada pieza con el id de su pieza en el XML. Visto de
  frente: la cara de 56 mm y sus dos cables.

    MOTOR  el EJE, que gira con el rotor (el efecto `angulo`)
    M      las tres fases: M.U, M.V y M.W
    P1     el cable de los Hall: P1.VCC, P1.GND, P1.H1, P1.H2 y P1.H3
""")
    s.add(s.rect(0, 0, 800, 600, "#eceff1", rx=10))
    cara_nema(s, 290, 300, 540, 474, 380, 80, "DB59L024035-A  BLDC")
    hilos_motor(s, 590, 70, ("#f9a825", "#c62828", "#212121"), ("U", "V", "W"), paso=50)
    hilos_motor(s, 590, 260, ("#c62828", "#212121", "#1565c0", "#eceff1", "#2e7d32"),
                ("VCC", "GND", "H1", "H2", "H3"), id_con="P1", paso=50)
    s.add(s.text(660, 560, "24 V  0,47 N·m  3500 rpm", 13, "#263238", "middle"))
    s.guarda("db59l024035_a.svg")


# ---------------------------------------------------------------------------
# EL CONTROLADOR TIPO C5-E
# ---------------------------------------------------------------------------
def controlador_c5e():
    s = Svg(700, 560, """
  UN CONTROLADOR DE MOTOR TIPO C5-E, dibujado a mano: una caja generica, no
  la de Nanotec. Es el dibujo de placas/controlador_tipo_c5e.xml, que lo
  encuentra solo porque se llama como ella. Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 70 x 56 mm- y cada pieza con el id de su pieza en el XML. Visto de
  frente, con sus bornas:

    U1  el controlador, en el centro
    X1  la alimentacion: VCC y GND, arriba a la izquierda
    X2  el motor: U, V y W, arriba a la derecha
    X3  los Hall: 5V, GND, H1, H2 y H3, a la derecha
    X4  el encoder: 5V, GND, A y B, a la derecha, debajo
    X5  el mando: STEP, DIR, EN, BRK, ERR y GND, abajo
""")
    s.add(s.rect(0, 0, 700, 560, "#37474f", "#1c262b", 2, rx=12))
    s.add(s.rect(20, 20, 660, 520, "#455a64", "#263238", 1.2, rx=8))
    s.add('<g id="U1">')
    s.add("  " + s.rect(170, 170, 300, 170, "#263238", "#101820", 1.5, rx=6))
    s.add("  " + s.text(320, 245, "CONTROLADOR", 18, "#eceff1", "middle"))
    s.add("  " + s.text(320, 275, "tipo C5-E  12-48 V  10 A", 13, "#b0bec5", "middle"))
    s.add("  " + s.text(320, 305, "reloj y direccion, lazo cerrado", 11, "#90a4ae", "middle"))
    s.add("</g>")

    def borna(id_con, nombres, x0, y0, dx, dy, rotulo_dy):
        n = len(nombres)
        w = abs(dx) * (n - 1) + 50 if dx else 50
        h = abs(dy) * (n - 1) + 50 if dy else 50
        s.add(s.rect(x0 - 25, y0 - 25, w, h, "#2e7d32", "#1b5e20", 1.5, rx=4))
        s.add('<g id="%s">' % id_con)
        for k, nm in enumerate(nombres):
            tornillo(s, "%s.%s" % (id_con, nm), x0 + k * dx, y0 + k * dy)
        s.add("</g>")
        for k, nm in enumerate(nombres):
            if dx:
                s.add(s.text(x0 + k * dx, y0 + rotulo_dy, nm, 10, BLANCO, "middle"))
            else:
                s.add(s.text(x0 - 34, y0 + k * dy + 4, nm, 10, BLANCO, "end"))

    borna("X1", ("VCC", "GND"), 70, 80, 50, 0, 44)
    borna("X2", ("U", "V", "W"), 470, 80, 50, 0, 44)
    borna("X3", ("5V", "GND", "H1", "H2", "H3"), 630, 150, 0, 44, 0)
    borna("X4", ("5V", "GND", "A", "B"), 630, 382, 0, 42, 0)
    borna("X5", ("STEP", "DIR", "EN", "BRK", "ERR", "GND"), 100, 480, 56, 0, -36)
    s.guarda("controlador_tipo_c5e.svg")


# ---------------------------------------------------------------------------
# UN EJE Z COMPLETO, DE LADO
# ---------------------------------------------------------------------------
def eje_z():
    s = Svg(700, 1300, """
  UN EJE Z COMPLETO -NEMA 17, ACOPLE, HUSILLO Tr8x2 Y CARRO-, visto de lado,
  dibujado a mano: una aproximacion, no una foto. Es el dibujo de
  placas/eje_z_tr8.xml, que lo encuentra solo porque se llama como ella.
  Generado por placas/dibuja_cnc.py.

  Perfil de autor (mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11): SVG
  1.2 Tiny, geometria como atributo, tamano en milimetros -una unidad es
  0,1 mm: 70 x 130 mm-, A ESCALA 1:2 -cada mm de la maquina son 5 unidades-
  y cada pieza con el id de su pieza en el XML:

    MOTOR  el NEMA 17, arriba
    M      sus cuatro hilos
    A      el acople de espiral
    H      el husillo, de 200 mm
    C      el carro con el cabezal, DIBUJADO EN EL TOPE DE ABAJO (posicion 0):
           sube con su posicion, 5 unidades por mm (efecto `desplaza`)
    COTA   el display de su posicion (efecto `display` de la transmision Z)
""")
    s.add(s.rect(0, 0, 700, 1300, "#eceff1", rx=10))
    # La columna: el perfil de aluminio de detras
    s.add(s.rect(150, 300, 60, 900, "#b0bec5", "#78909c", 1.5))
    # El motor, arriba: 42 mm a 1:2
    s.add('<g id="MOTOR">')
    s.add("  " + s.rect(170, 60, 210, 200, GRIS_MOTOR, GRIS_MOTOR_B, 2, rx=8))
    s.add("  " + s.text(275, 165, "NEMA 17", 16, "#cfd8dc", "middle"))
    s.add("</g>")
    hilos_motor(s, 420, 80, ("#212121", "#2e7d32", "#c62828", "#1565c0"),
                ("A1", "A2", "B1", "B2"), paso=45)
    # El acople, de 5 a 8
    s.add('<g id="A">')
    s.add("  " + s.rect(255, 260, 40, 60, "#90a4ae", "#455a64", 1.5, rx=4))
    for k in range(5):
        s.add("  " + s.rect(255, 268 + k * 10, 40, 3, "#455a64"))
    s.add("</g>")
    # El husillo: 200 mm, de 320 a 1170 (con sus apoyos)
    s.add('<g id="H">')
    s.add("  " + s.rect(268, 320, 14, 850, "#cfd8dc", "#78909c", 1))
    for k in range(43):
        s.add("  " + s.rect(268, 330 + k * 19.5, 14, 4, "#90a4ae"))
    s.add("</g>")
    s.add(s.rect(240, 320, 70, 26, "#37474f", rx=3))
    s.add(s.rect(240, 1150, 70, 26, "#37474f", rx=3))
    # El carro, en el tope de abajo: su centro en y = 1100, y sube 5 por mm
    s.add('<g id="C">')
    s.add("  " + s.rect(200, 1040, 150, 110, "#546e7a", "#263238", 2, rx=6))
    s.add("  " + s.rect(255, 1060, 40, 70, "#c9a227", "#7d6608", 1.2, rx=3))
    s.add("  " + s.rect(350, 1060, 90, 70, "#78909c", "#37474f", 1.5, rx=6))
    s.add("  " + s.rect(440, 1080, 12, 30, "#9e9e9e"))
    s.add("  " + s.text(275, 1030, "Z", 18, "#263238", "middle"))
    s.add("</g>")
    # La cota
    s.add(s.rect(470, 560, 200, 70, "#1b1b1b", "#000000", 2, rx=6, id="COTA"))
    s.add(s.text(570, 660, "COTA Z", 14, "#263238", "middle"))
    s.add(s.text(350, 1250, "Tr8x2  200 mm  1,2 kg", 15, "#263238", "middle"))
    s.guarda("eje_z_tr8.svg")


if __name__ == "__main__":
    nema17()
    st5918()
    pololu_a4988()
    motor_775()
    modulo_mosfet()
    ibt2()
    motor_jgb37()
    db59()
    controlador_c5e()
    eje_z()
