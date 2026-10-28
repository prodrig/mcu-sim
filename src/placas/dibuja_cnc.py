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


if __name__ == "__main__":
    nema17()
    st5918()
    pololu_a4988()
