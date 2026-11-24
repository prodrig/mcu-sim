#!/usr/bin/env python3
# =============================================================================
# dibuja_piezas.py — El dibujo de cada tipo de pieza y su <forma> (fase B5 de
# doc/analisis_build.md)
#
#   python3 dibuja_piezas.py [CARPETA]       (por omisión, biblioteca/piezas)
#
# Por cada TIPO.pieza.xml (la ficha que exporta mcu-sim, B1) escribe:
#
#   TIPO.svg        el dibujo, visto desde arriba, a escala, en unidades de
#                   0,1 mm. La raíz de la pieza es <g id="$">; cada terminal,
#                   un elemento con id "$.terminal" (la almohadilla o el
#                   extremo del hilo). Al componer una placa, mcu-sim-build
#                   cambia "$" por el id de la pieza (LD2, y LD2.anodo);
#   TIPO.forma.xml  dónde está cada terminal en el dibujo y hacia dónde sale:
#                   <ancla terminal elemento lado x y>. Para las piezas
#                   PARAMÉTRICAS (Conector, Jumper: su dibujo depende de sus
#                   atributos) no hay SVG: <forma generador="conector"/>, y el
#                   dibujo lo hace mcu-sim-build (D5).
#
# Los dibujos son propios (perfil de autor de mcu-sim-gui,
# doc/analisis-uso-ilustraciones.md §11): SVG 1.2 Tiny, geometría en
# atributos, sin filtros, máscaras, scripts ni imágenes, viewBox y tamaño en
# mm. Salen de un puñado de PLANTILLAS (un módulo con su tira de pines, un
# integrado, un componente de patas, un motor...), y cada tipo dice cuál usa
# y con qué colores, en PIEZAS. No se editan a mano: se regeneran con
# `make biblioteca-piezas`, y `comprueba_piezas.py` dice si están al día.
# =============================================================================
import os
import sys
import xml.dom.minidom as minidom
from xml.sax.saxutils import escape

AQUI = os.path.dirname(os.path.abspath(__file__))
CARPETA = os.path.join(AQUI, "..", "piezas")

PASO = 25.4            # 2,54 mm, en unidades de 0,1 mm
PAD = 8.0              # radio de una almohadilla
ORO = "#c9a227"
LETRA = "sans-serif"


# --- La ficha -----------------------------------------------------------------
def terminales(ruta):
    """(terminales en orden, series abiertas?, sin terminales?) de una ficha."""
    d = minidom.parse(ruta)
    if d.getElementsByTagName("sin_terminales"):
        return [], False, True
    out, abierta = [], False
    for g in d.getElementsByTagName("grupo"):
        for n in g.childNodes:
            if n.nodeType != n.ELEMENT_NODE:
                continue
            if n.tagName == "terminal":
                out.append(n.getAttribute("nombre"))
            elif n.tagName == "serie":
                h = n.getAttribute("hasta")
                if not h or not h.isdigit():
                    abierta = True
                    continue
                for k in range(int(n.getAttribute("desde")), int(h) + 1):
                    out.append("%s%d" % (n.getAttribute("prefijo"), k))
    vistos = []
    for t in out:
        if t not in vistos:
            vistos.append(t)
    return vistos, abierta, False


# --- El lienzo ---------------------------------------------------------------
def f(v):
    s = ("%.1f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


class Lienzo:
    def __init__(self):
        self.el = []
        self.anclas = []          # (terminal, x, y, lado)
        self.caja = [1e9, 1e9, -1e9, -1e9]

    def _amplia(self, x0, y0, x1, y1):
        c = self.caja
        c[0], c[1], c[2], c[3] = min(c[0], x0), min(c[1], y0), max(c[2], x1), max(c[3], y1)

    def rect(self, x, y, w, h, fill, rx=0, stroke=None, sw=0, id_=None):
        a = ' id="%s"' % id_ if id_ else ""
        r = ' rx="%s"' % f(rx) if rx else ""
        s = ' stroke="%s" stroke-width="%s"' % (stroke, f(sw)) if stroke else ""
        self.el.append('<rect%s x="%s" y="%s" width="%s" height="%s"%s fill="%s"%s/>'
                       % (a, f(x), f(y), f(w), f(h), r, fill, s))
        self._amplia(x, y, x + w, y + h)

    def circulo(self, cx, cy, r, fill, stroke=None, sw=0, id_=None):
        a = ' id="%s"' % id_ if id_ else ""
        s = ' stroke="%s" stroke-width="%s"' % (stroke, f(sw)) if stroke else ""
        self.el.append('<circle%s cx="%s" cy="%s" r="%s" fill="%s"%s/>' % (a, f(cx), f(cy), f(r), fill, s))
        self._amplia(cx - r, cy - r, cx + r, cy + r)

    def linea(self, x0, y0, x1, y1, color, sw):
        self.el.append('<path d="M%s %sL%s %s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round"/>'
                       % (f(x0), f(y0), f(x1), f(y1), color, f(sw)))
        self._amplia(min(x0, x1) - sw, min(y0, y1) - sw, max(x0, x1) + sw, max(y0, y1) + sw)

    def trazo(self, d, fill="none", stroke=None, sw=0, caja=None):
        s = ' stroke="%s" stroke-width="%s"' % (stroke, f(sw)) if stroke else ""
        self.el.append('<path d="%s" fill="%s"%s/>' % (d, fill, s))
        if caja:
            self._amplia(*caja)

    def texto(self, x, y, t, tam, color="#fff", ancla="middle", giro=0, negrita=False):
        g = ' transform="rotate(%s %s %s)"' % (f(giro), f(x), f(y)) if giro else ""
        b = ' font-weight="bold"' if negrita else ""
        self.el.append('<text x="%s" y="%s" font-size="%s" text-anchor="%s" fill="%s"%s%s>%s</text>'
                       % (f(x), f(y), f(tam), ancla, color, b, g, escape(t)))
        w = len(t) * tam * 0.6
        if giro > 0:            # hacia abajo
            self._amplia(x - tam, y - tam * 0.3, x + tam, y + w)
        elif giro < 0:          # hacia arriba
            self._amplia(x - tam, y - w, x + tam, y + tam * 0.3)
        else:
            dx = {"middle": w / 2, "start": 0, "end": w}[ancla]
            self._amplia(x - dx, y - tam, x - dx + w, y + tam * 0.3)

    def pad(self, term, x, y, lado, forma="circulo"):
        """La almohadilla de un terminal: el elemento con id $.terminal."""
        if forma == "cuadro":
            self.rect(x - PAD, y - PAD, 2 * PAD, 2 * PAD, ORO, id_="$." + term)
        else:
            self.circulo(x, y, PAD, ORO, stroke="#7a5f0f", sw=1.2, id_="$." + term)
            self.circulo(x, y, PAD * 0.4, "#3a2f0a")
        self.anclas.append((term, x, y, lado))

    def svg(self, tipo, margen=20):
        x0, y0, x1, y1 = self.caja
        x0 -= margen; y0 -= margen; x1 += margen; y1 += margen
        w, h = x1 - x0, y1 - y0
        cab = ('<svg xmlns="http://www.w3.org/2000/svg" version="1.2" baseProfile="tiny" '
               'viewBox="0 0 %s %s" width="%smm" height="%smm">\n' % (f(w), f(h), "%.2f" % (w / 10), "%.2f" % (h / 10)))
        cab += ("<!-- %s: el dibujo de la pieza para mcu-sim-build (doc/analisis_build.md, B5).\n"
                "     Lo genera biblioteca/herramientas/dibuja_piezas.py: no se edita a mano.\n"
                "     Unidades de 0.1 mm. La pieza es $, y cada terminal $.terminal. -->\n" % tipo)
        cab += '<g id="$" transform="translate(%s,%s)" font-family="%s">\n' % (f(-x0), f(-y0), LETRA)
        self.dx, self.dy = -x0, -y0
        return cab + "".join("  %s\n" % e for e in self.el) + "</g>\n</svg>\n", w, h


# --- Las plantillas ----------------------------------------------------------
def _etiquetas_tira(L, terms, xs, y, arriba):
    """Los nombres de una tira de pines, en vertical, por dentro de la placa."""
    for t, x in zip(terms, xs):
        if arriba:
            L.texto(x + 4, y + PAD + 6, t, 11, giro=90, ancla="start")
        else:
            L.texto(x + 4, y - PAD - 6, t, 11, giro=-90, ancla="start")


def modulo(L, tipo, terms, color="#1f5f9e", titulo=None, icono=None, alto=None, zona=120, ancho_min=220):
    """Una placa pequeña (un módulo, un breakout, un banco de pruebas) con sus
    terminales en una tira de 2,54 mm abajo, o en dos (arriba y abajo) si son
    muchos. De arriba abajo, sin pisarse: la tira de arriba y sus nombres, el
    título, el icono, los nombres de la tira de abajo y la tira."""
    n = len(terms)
    filas = [terms] if n <= 12 else [terms[:(n + 1) // 2], terms[(n + 1) // 2:]]
    ancho_tira = max(len(r) for r in filas) * PASO
    rotulo = max(len(t) for t in terms) * 6.6 + 16       # lo que ocupa un nombre en vertical
    zona_icono = zona if icono else 0
    ancho = max(ancho_tira + 40, ancho_min, len(titulo or tipo) * 13 + 60)
    y = 0
    if len(filas) == 2:
        y_arriba = 22
        y = y_arriba + PAD + rotulo
    y_titulo = y + 34
    y_icono = y_titulo + 10 + zona_icono / 2
    y_abajo = y_titulo + 12 + zona_icono + rotulo + PAD + 4
    alto = max(alto or 0, y_abajo + 22)
    y_abajo = alto - 22
    L.rect(0, 0, ancho, alto, color, rx=12, stroke="#0c2c4a", sw=1.5)
    for hx, hy in ((14, 14), (ancho - 14, 14), (14, alto - 14), (ancho - 14, alto - 14)):
        L.circulo(hx, hy, 6, "#e8e8e8", stroke="#888", sw=1)
    L.texto(ancho / 2, y_titulo, titulo or tipo, 13, negrita=True)
    if icono:
        icono(L, ancho / 2, y_icono)
    for k, fila in enumerate(filas):
        abajo = len(filas) == 1 or k == 1
        y = y_abajo if abajo else y_arriba
        x0 = (ancho - len(fila) * PASO) / 2 + PASO / 2
        xs = [x0 + i * PASO for i in range(len(fila))]
        L.rect(xs[0] - PASO / 2, y - PASO / 2, len(fila) * PASO, PASO, "#1a1a1a", rx=2)
        for t, x in zip(fila, xs):
            L.pad(t, x, y, "abajo" if abajo else "arriba", "cuadro" if t == fila[0] else "circulo")
        _etiquetas_tira(L, fila, xs, y, not abajo)


def integrado(L, tipo, terms, titulo=None, ancho=None):
    """Un integrado de dos filas (SOIC, TSOP, DIP), los terminales repartidos
    a izquierda y derecha, con su nombre por dentro."""
    n = len(terms)
    izq, dch = terms[:(n + 1) // 2], terms[(n + 1) // 2:]
    filas = max(len(izq), len(dch))
    paso = 12.7 if filas > 6 else PASO
    alto = filas * paso + 40
    larga = max(len(t) for t in terms)
    ancho = ancho or max(160, 2 * larga * 7.2 + 50)
    L.rect(0, 0, ancho, alto, "#262626", rx=4, stroke="#111", sw=1)
    L.circulo(14, 14, 5, "#555")
    L.texto(ancho / 2, alto / 2 + 5, titulo or tipo, 13, color="#ddd", giro=-90 if alto > ancho else 0)
    for i, t in enumerate(izq):
        y = 20 + paso / 2 + i * paso
        L.rect(-14, y - 3, 14, 6, "#b8b8b8")
        L.pad(t, -14 - PAD * 0.6, y, "izquierda")
        L.texto(8, y + 4, t, 10, color="#9cf", ancla="start")
    for i, t in enumerate(reversed(dch)):
        y = 20 + paso / 2 + i * paso
        L.rect(ancho, y - 3, 14, 6, "#b8b8b8")
        L.pad(t, ancho + 14 + PAD * 0.6, y, "derecha")
        L.texto(ancho - 8, y + 4, t, 10, color="#9cf", ancla="end")


def patas(L, tipo, terms, cuerpo, separacion=PASO, largo=80):
    """Un componente de patas hacia abajo (LED, condensador, TO-92...), visto
    desde arriba: el cuerpo y una pata por terminal."""
    n = len(terms)
    xs = [(i - (n - 1) / 2) * separacion for i in range(n)]
    cuerpo(L)
    for t, x in zip(terms, xs):
        L.linea(x, 20, x, largo, "#9a9a9a", 5)
        L.pad(t, x, largo, "abajo")
        L.texto(x + 4, largo + PAD + 8, t, 11, color="#333", giro=90, ancla="start")


def axial(L, tipo, terms, colores, largo=200):
    """Un componente axial (una resistencia), horizontal."""
    L.linea(-largo / 2, 0, largo / 2, 0, "#9a9a9a", 5)
    L.rect(-60, -22, 120, 44, colores[0], rx=18, stroke="#8a6a3a", sw=1.2)
    for i, c in enumerate(colores[1:]):
        L.rect(-40 + i * 22, -22, 9, 44, c)
    L.pad(terms[0], -largo / 2, 0, "izquierda")
    L.pad(terms[1], largo / 2, 0, "derecha")
    L.texto(-largo / 2, 26, terms[0], 11, color="#333")
    L.texto(largo / 2, 26, terms[1], 11, color="#333")


def motor(L, tipo, terms, cuadrado=False, color="#8a8f96"):
    """Un motor visto desde la cara del eje, con sus terminales en una tira
    debajo (los hilos de un motor llegan siempre a un conector)."""
    if cuadrado:
        L.rect(-210, -210, 420, 420, "#2d2d2d", rx=30, stroke="#111", sw=2)
        for x, y in ((-155, -155), (155, -155), (-155, 155), (155, 155)):
            L.circulo(x, y, 16, "#bbb")
        L.circulo(0, 0, 110, "#555", stroke="#777", sw=3)
    else:
        L.circulo(0, 0, 180, color, stroke="#4a4f56", sw=3)
        L.circulo(0, 0, 70, "#c0c4c8", stroke="#777", sw=2)
    L.circulo(0, 0, 25, "#ddd", stroke="#666", sw=2)
    L.texto(0, -120 if not cuadrado else -170, tipo, 22, color="#fff", negrita=True)
    y = 280
    n = len(terms)
    xs = [(i - (n - 1) / 2) * PASO for i in range(n)]
    L.rect(xs[0] - PASO / 2 - 6, y - PASO / 2 - 6, n * PASO + 12, PASO + 12, "#f2f2f2", rx=3, stroke="#999", sw=1)
    for t, x in zip(terms, xs):
        L.linea(x, 180 if abs(x) < 170 else 150, x, y - PAD, "#c62828" if t == terms[0] else "#444", 4)
        L.pad(t, x, y, "abajo")
        L.texto(x + 4, y + PAD + 8, t, 11, color="#333", giro=90, ancla="start")


def to220(L, tipo, terms, aleta=None):
    """Un TO-220 visto de frente: la aleta, el cuerpo y tres patas. `aleta`
    es el terminal que sale por la aleta, si lo hay."""
    L.rect(-52, -150, 104, 60, "#b8b8b8", stroke="#888", sw=1)
    L.circulo(0, -120, 18, "#fff", stroke="#888", sw=1)
    L.rect(-52, -90, 104, 90, "#222", rx=3)
    L.texto(0, -38, tipo, 13, color="#ddd")
    for i, t in enumerate(terms[:3]):
        x = (i - 1) * PASO
        L.linea(x, 0, x, 90, "#b8b8b8", 8)
        L.pad(t, x, 90, "abajo")
        L.texto(x + 4, 90 + PAD + 8, t, 11, color="#333", giro=90, ancla="start")
    if aleta:
        L.pad(aleta, 0, -150, "arriba")
        L.texto(30, -156, aleta, 11, color="#333", ancla="start")


def simbolo(L, tipo, terms, dibujo, lado="abajo"):
    """Una pieza de un terminal o dos que el modelo trata como fuente o
    sumidero (un pulsador contra masa, una fuente, un reloj): su dibujo y el
    terminal debajo."""
    dibujo(L)
    n = len(terms)
    for i, t in enumerate(terms):
        x = (i - (n - 1) / 2) * PASO * 2
        L.linea(x, 50, x, 100, "#666", 4)
        L.pad(t, x, 100, lado)
        L.texto(x, 100 + PAD + 14, t, 11, color="#333")


def icono(L, tipo, glifo):
    """Una pieza sin terminales (lo mecánico, las cotas): una ficha con su
    nombre. Se coloca en el lienzo, pero no se conecta."""
    L.rect(0, 0, 260, 140, "#eef1f4", rx=14, stroke="#7a8794", sw=2)
    glifo(L)
    L.texto(130, 125, tipo, 15, color="#2b3a48", negrita=True)


# --- Glifos ------------------------------------------------------------------
def g_chip(L, x, y, w=70):
    L.rect(x - w / 2, y - w / 2 + 8, w, w, "#1d1d1d", rx=3)
    for k in range(4):
        L.rect(x - w / 2 - 6, y - w / 2 + 16 + k * (w - 16) / 3, 6, 3, "#aaa")
        L.rect(x + w / 2, y - w / 2 + 16 + k * (w - 16) / 3, 6, 3, "#aaa")


def g_pantalla(L, x, y):
    L.rect(x - 150, y - 75, 300, 150, "#111", rx=4)
    L.rect(x - 135, y - 62, 270, 124, "#2a4d6e")


def g_tarjeta(L, x, y):
    L.trazo("M%s %sL%s %sL%s %sL%s %sL%s %sZ" % (
        f(x - 45), f(y - 50), f(x + 25), f(y - 50), f(x + 45), f(y - 30), f(x + 45), f(y + 50), f(x - 45), f(y + 50)),
        fill="#333", stroke="#999", sw=1.5, caja=(x - 45, y - 50, x + 45, y + 50))
    for k in range(6):
        L.rect(x - 38 + k * 13, y - 44, 7, 18, ORO)


def g_usb(L, x, y):
    L.rect(x - 40, y - 22, 80, 44, "#c8c8c8", rx=3, stroke="#888", sw=1)
    L.rect(x - 30, y - 8, 60, 16, "#333")


def g_mando(L, x, y):
    L.circulo(x, y, 40, "#333", stroke="#111", sw=2)
    L.circulo(x, y, 26, "#9aa", stroke="#666", sw=2)


def g_palanca(L, x, y):
    L.rect(x - 40, y - 20, 80, 40, "#222", rx=4)
    L.linea(x - 30, y - 26, x + 50, y - 46, "#ccc", 6)


def g_conmutador(L, x, y):
    L.rect(x - 40, y - 26, 80, 52, "#ddd", rx=4, stroke="#888", sw=1)
    L.rect(x - 8, y - 46, 16, 30, "#555", rx=4)


def g_disipador(L, x, y):
    for k in range(7):
        L.rect(x - 60 + k * 18, y - 40, 8, 80, "#9aa3ab")


def g_bornes(L, x, y):
    for k in range(3):
        L.rect(x - 60 + k * 42, y - 22, 36, 44, "#2e7d32", rx=3)
        L.circulo(x - 42 + k * 42, y, 9, "#bbb")


# --- Los tipos -----------------------------------------------------------------
def _led(color):
    def c(L):
        L.circulo(0, 0, 50, color, stroke="#555", sw=2)
        L.circulo(-15, -15, 12, "#ffffff")
        L.trazo("M45 -24L45 24", stroke="#555", sw=3)
    return c


def _rgb(L):
    L.circulo(0, 0, 50, "#f4f4f4", stroke="#777", sw=2)
    L.circulo(-18, -8, 10, "#e33")
    L.circulo(2, -18, 10, "#3c3")
    L.circulo(14, 8, 10, "#36e")


def _condensador(L):
    L.circulo(0, 0, 50, "#1d3f8a", stroke="#0b1f4a", sw=2)
    L.trazo("M-50 0A50 50 0 0 1 -35 -35L-20 -20A28 28 0 0 0 -28 0Z", fill="#cfd8e8")
    L.texto(20, 8, "+", 26, color="#fff")


def _fotodiodo(L):
    L.circulo(0, 0, 30, "#222", stroke="#555", sw=2)
    L.rect(-14, -14, 28, 28, "#555")


def _to92(L):
    L.trazo("M-40 20L40 20A45 45 0 0 0 -40 20Z", fill="#222", caja=(-45, -40, 45, 22))


def _opto(L):
    pass


def _pulsador(L):
    L.rect(-55, -55, 110, 110, "#222", rx=6)
    L.circulo(0, 0, 32, "#444", stroke="#111", sw=2)
    for x, y in ((-45, -45), (45, -45), (-45, 45), (45, 45)):
        L.circulo(x, y, 6, "#888")


def _masa(L):
    for k, w in enumerate((70, 46, 22)):
        L.linea(-w / 2, k * 14, w / 2, k * 14, "#333", 6)
    L.linea(0, -40, 0, 0, "#333", 5)
    L.trazo("M0 28L0 50", stroke="#666", sw=4, caja=(-35, -40, 35, 50))


def _fuente(etiqueta):
    def c(L):
        L.rect(-70, -60, 140, 110, "#f0c419", rx=10, stroke="#a88300", sw=2)
        L.texto(0, 5, etiqueta, 26, color="#3a2e00", negrita=True)
    return c


def _buffer(L):
    L.trazo("M-50 -50L50 0L-50 50Z", fill="#e8eef5", stroke="#345", sw=3, caja=(-50, -50, 50, 50))


def _oscilador(L):
    L.rect(-60, -50, 120, 100, "#c8c8c8", rx=8, stroke="#888", sw=1.5)
    L.trazo("M-40 0L-30 0L-30 -20L-10 -20L-10 20L10 20L10 -20L30 -20L30 0L40 0", stroke="#333", sw=3)


def _cristal(L):
    L.rect(-70, -30, 140, 60, "#cfd3d6", rx=28, stroke="#8a8f94", sw=2)


def _rpull(L):
    L.linea(-50, -60, 50, -60, "#c62828", 5)
    L.texto(60, -54, "VDD", 12, color="#c62828", ancla="start")
    L.linea(0, -60, 0, -40, "#9a9a9a", 4)
    L.rect(-14, -40, 28, 80, "#d8b98a", rx=8, stroke="#8a6a3a", sw=1)
    L.linea(0, 40, 0, 50, "#9a9a9a", 4)


def _ws2812(L):
    L.rect(-50, -50, 100, 100, "#f6f6f6", stroke="#999", sw=1.5)
    L.circulo(0, 0, 36, "#fff", stroke="#bbb", sw=2)
    L.rect(-10, -10, 20, 20, "#333")


def _sonda(L):
    L.trazo("M-60 -40L60 -40L20 30L-20 30Z", fill="#d9d9d9", stroke="#777", sw=2, caja=(-60, -40, 60, 30))


def _zumbador(L):
    L.circulo(0, 0, 60, "#1b1b1b", stroke="#000", sw=2)
    L.circulo(0, 0, 10, "#555")
    L.texto(28, -30, "+", 20, color="#ddd")


def _cable(L):
    L.trazo("M-80 0C-40 -40 40 40 80 0", stroke="#555", sw=10, caja=(-80, -30, 80, 30))


def _engranaje(L):
    L.circulo(130, 60, 36, "#9aa3ab", stroke="#5c656d", sw=3)
    L.circulo(130, 60, 12, "#eef1f4")


def _flecha(L):
    L.linea(60, 60, 200, 60, "#5c656d", 6)
    L.trazo("M200 45L225 60L200 75Z", fill="#5c656d")


def _rueda(L):
    L.circulo(130, 60, 40, "none", stroke="#5c656d", sw=6)
    L.linea(90, 60, 170, 60, "#5c656d", 4)
    L.linea(130, 20, 130, 100, "#5c656d", 4)


def _caja3d(L):
    L.trazo("M90 40L150 25L190 45L130 60Z", fill="#c9d3dc", stroke="#5c656d", sw=2)
    L.trazo("M90 40L130 60L130 100L90 80Z", fill="#9aa9b6", stroke="#5c656d", sw=2)
    L.trazo("M130 60L190 45L190 85L130 100Z", fill="#b5c2cd", stroke="#5c656d", sw=2)


def _mosfet(L, tipo):
    """El TO-220 y, al lado, su diodo de libre circulación: `libre` es su
    cátodo (el + de la carga), con diodo=si."""
    to220(L, tipo, ["puerta", "drenador", "fuente"])
    x = 3 * PASO
    L.rect(x - 12, -60, 24, 70, "#222", rx=4)
    L.rect(x - 12, -6, 24, 8, "#ccc")
    L.linea(x, 10, x, 90, "#9a9a9a", 5)
    L.pad("libre", x, 90, "abajo")
    L.texto(x + 4, 90 + PAD + 8, "libre", 11, color="#333", giro=90, ancla="start")


def _servo(L, tipo, terms):
    L.rect(-160, -80, 320, 160, "#1f4fa8", rx=8, stroke="#0d2a5e", sw=2)
    L.rect(-200, -20, 40, 40, "#1f4fa8")
    L.rect(160, -20, 40, 40, "#1f4fa8")
    L.circulo(-180, 0, 8, "#fff")
    L.circulo(180, 0, 8, "#fff")
    L.circulo(-80, 0, 50, "#e6e6e6", stroke="#aaa", sw=2)
    L.circulo(-80, 0, 14, "#fff", stroke="#aaa", sw=2)
    L.texto(60, 8, tipo, 22, color="#fff", negrita=True)
    colores = {"gnd": "#5b3a1e", "vcc": "#d32f2f", "pwm": "#f39c12"}
    for i, t in enumerate(terms):
        x = 100 + (i - 1) * PASO
        L.linea(x, 80, x, 220, colores.get(t, "#444"), 6)
        L.pad(t, x, 220, "abajo")
        L.texto(x + 4, 220 + PAD + 8, t, 11, color="#333", giro=90, ancla="start")


def _lcd(L, tipo, terms):
    """El cristal de un LCD de segmentos: el vidrio y sus 38 patillas abajo."""
    n = len(terms)
    ancho = n * PASO + 60
    L.rect(0, 0, ancho, 300, "#c9d8c5", rx=6, stroke="#8a9a86", sw=2)
    L.rect(30, 30, ancho - 60, 200, "#e6efe2", stroke="#b3c2ae", sw=1)
    L.texto(ancho / 2, 145, tipo, 30, color="#55654f", negrita=True)
    for i, t in enumerate(terms):
        x = 30 + PASO / 2 + i * PASO
        L.rect(x - 5, 300, 10, 60, "#bbb")
        L.pad(t, x, 360, "abajo")
        L.texto(x + 4, 360 + PAD + 8, t, 11, color="#333", giro=90, ancla="start")


def _opto(L, tipo, terms):
    integrado(L, tipo, terms, titulo="Opto", ancho=130)


# tipo -> función(L, tipo, terms)
def M(color="#1f5f9e", titulo=None, icono=None, alto=None, zona=120, ancho_min=220):
    return lambda L, t, ts: modulo(L, t, ts, color, titulo, icono, alto, zona, ancho_min)


AZUL, VERDE, ROJO, MORADO, NEGRO, GRIS = "#1f5f9e", "#2e6b34", "#9c2b2b", "#5b3e8a", "#2b2b2b", "#59626b"
PIEZAS = {
    # Pasivos y discretos
    "Resistencia": lambda L, t, ts: axial(L, t, ts, ["#d8b98a", "#8b4513", "#000", "#8b4513", "#d4af37"]),
    "Condensador": lambda L, t, ts: patas(L, t, ts, _condensador),
    "Led": lambda L, t, ts: patas(L, t, ts, _led("#e53935")),
    "RGBLed": lambda L, t, ts: patas(L, t, ts, _rgb),
    "Fotodiodo": lambda L, t, ts: patas(L, t, ts, _fotodiodo),
    "Tmp235": lambda L, t, ts: patas(L, t, ts, _to92),
    "Mosfet": lambda L, t, ts: _mosfet(L, t),
    "Regulador": lambda L, t, ts: to220(L, t, ["entrada", "gnd", "salida"]),
    "Optoacoplador": lambda L, t, ts: integrado(L, t, ["anodo", "catodo", "emisor", "colector"], "Opto", ancho=150),
    "Button": lambda L, t, ts: simbolo(L, t, ts, _pulsador),
    "Gnd": lambda L, t, ts: simbolo(L, t, ts, _masa),
    "Fuente": lambda L, t, ts: simbolo(L, t, ts, _fuente("V")),
    "FuenteAjustable": lambda L, t, ts: simbolo(L, t, ts, _fuente("V~")),
    "Driver": lambda L, t, ts: simbolo(L, t, ts, _buffer),
    "ExtClock": lambda L, t, ts: simbolo(L, t, ts, _oscilador),
    "Crystal": lambda L, t, ts: simbolo(L, t, ts, _cristal),
    "Rpull": lambda L, t, ts: simbolo(L, t, ts, _rpull),
    "Ws2812": lambda L, t, ts: simbolo(L, t, ts, _ws2812),
    "Zumbador": lambda L, t, ts: simbolo(L, t, ts, _zumbador),
    "SwoReceiver": lambda L, t, ts: simbolo(L, t, ts, _sonda),
    "Sonda": lambda L, t, ts: simbolo(L, t, ts, _sonda),
    "CanWire": lambda L, t, ts: simbolo(L, t, ts, _cable),
    "SignalLink": lambda L, t, ts: simbolo(L, t, ts, _cable),
    # Motores
    "MotorCC": lambda L, t, ts: motor(L, t, ts),
    "MotorBLDC": lambda L, t, ts: motor(L, t, ts, color="#5d6670"),
    "MotorPaP": lambda L, t, ts: motor(L, t, ts, cuadrado=True),
    "Freno": lambda L, t, ts: motor(L, t, ts, color="#7b5c3a"),
    "Servo": _servo,
    "LcdFh1138p": _lcd,
    # Integrados sueltos
    "ExtNand": lambda L, t, ts: integrado(L, t, ts, "NAND"),
    "ExtSram": lambda L, t, ts: integrado(L, t, ts, "SRAM"),
    "EthPhy": lambda L, t, ts: integrado(L, t, ts, "PHY"),
    "SpiFlash": lambda L, t, ts: integrado(L, t, ts, "Flash"),
    "I2cEeprom": lambda L, t, ts: integrado(L, t, ts, "EEPROM", ancho=140),
    # Módulos y sensores
    "Amplificador": M(VERDE, icono=lambda L, x, y: g_chip(L, x, y)),
    "AnalizadorCan": M(GRIS, "Analizador CAN"),
    "AutoResetEsp": M(NEGRO, "Auto-reset ESP"),
    "CanNode": M(GRIS, "Nodo CAN"),
    "CanTransceiver": M(AZUL, "Transceptor CAN", icono=lambda L, x, y: g_chip(L, x, y, 50)),
    "ChopperFrenado": M(ROJO, "Chopper", icono=g_disipador),
    "ControladorMotor": M(NEGRO, "Controlador", icono=g_disipador),
    "DriverPaP": M(VERDE, "Driver PaP", icono=lambda L, x, y: g_chip(L, x, y, 60)),
    "Encoder": M(AZUL, "Encoder", icono=g_mando),
    "FinalCarrera": M(NEGRO, "Final de carrera", icono=g_palanca),
    "GeneradorPasos": M(GRIS, "Generador de pasos"),
    "I2cExtMaster": M(GRIS, "Maestro I2C"),
    "Interruptor": M(GRIS, icono=g_conmutador),
    "L298": M(ROJO, icono=g_disipador),
    "L3gd20": M(MORADO, icono=lambda L, x, y: g_chip(L, x, y, 40)),
    "I3g4250d": M(MORADO, icono=lambda L, x, y: g_chip(L, x, y, 40)),
    "Lsm303agr": M(MORADO, icono=lambda L, x, y: g_chip(L, x, y, 40)),
    "Lsm303dlhc": M(MORADO, icono=lambda L, x, y: g_chip(L, x, y, 40)),
    "Opt3001": M(MORADO, icono=lambda L, x, y: g_chip(L, x, y, 30)),
    "Tmp006": M(MORADO, icono=lambda L, x, y: g_chip(L, x, y, 30)),
    "MedioPuente": M(ROJO, "Medio puente", icono=g_disipador),
    "PuenteSerie": M(ROJO, "USB-serie", icono=g_usb),
    "PuenteTrifasico": M(ROJO, "Puente trifasico", icono=g_disipador),
    "SdCard": M(AZUL, "SD", icono=g_tarjeta),
    "SdSpi": M(AZUL, "SD (SPI)", icono=g_tarjeta),
    "Tft128x160": M(ROJO, "TFT 128x160", icono=g_pantalla, zona=170, ancho_min=340),
    "UsbDeviceRig": M(GRIS, "USB dispositivo", icono=g_usb),
    "UsbHostRig": M(GRIS, "USB anfitrion", icono=g_usb),
    "Xds110": M(NEGRO, "XDS110", icono=g_usb),
    # Sin terminales: se colocan, no se conectan
    "Acople": lambda L, t, ts: icono(L, t, _engranaje),
    "Carro": lambda L, t, ts: icono(L, t, _flecha),
    "Cotas": lambda L, t, ts: icono(L, t, _flecha),
    "Husillo": lambda L, t, ts: icono(L, t, _flecha),
    "Pieza": lambda L, t, ts: icono(L, t, _caja3d),
    "Postura": lambda L, t, ts: icono(L, t, _caja3d),
    "Reductora": lambda L, t, ts: icono(L, t, _engranaje),
    "Transmision": lambda L, t, ts: icono(L, t, _engranaje),
    "Volante": lambda L, t, ts: icono(L, t, _rueda),
}
# Las paramétricas: su dibujo depende de sus atributos y lo hace mcu-sim-build
# (D5). conector: una tira de filas x columnas con su numeración y sus
# nombres; bus: un hilo con tantas líneas como tenga; modulo: una placa con
# una tira de pines, de los terminales fijos más la serie del atributo
GENERADORES = {
    "Conector": "conector",
    "Jumper": "conector",
    "I2cWire": "bus",
    "CameraSensor": "modulo",
}


def forma_xml(tipo, svg, w, h, anclas, dx, dy, generador=None, sin=False):
    s = ['<?xml version="1.0" encoding="UTF-8"?>',
         "<!-- %s: dónde está cada terminal en su dibujo (doc/analisis_build.md, B5)." % tipo,
         "     Lo genera biblioteca/herramientas/dibuja_piezas.py: no se edita a mano.",
         "     x, y en unidades de 0.1 mm, desde la esquina de arriba a la izquierda",
         "     del viewBox; lado: hacia dónde sale el hilo. -->"]
    if generador:
        s.append('<forma tipo="%s" version="1" generador="%s"/>' % (tipo, generador))
    elif sin:
        s.append('<forma tipo="%s" version="1" svg="%s.svg" ancho="%s" alto="%s" sin_terminales="si"/>'
                 % (tipo, tipo, f(w), f(h)))
    else:
        s.append('<forma tipo="%s" version="1" svg="%s.svg" ancho="%s" alto="%s">' % (tipo, tipo, f(w), f(h)))
        for t, x, y, lado in anclas:
            s.append('  <ancla terminal="%s" elemento="$.%s" lado="%s" x="%s" y="%s"/>'
                     % (escape(t), escape(t), lado, f(x + dx), f(y + dy)))
        s.append("</forma>")
    return "\n".join(s) + "\n"


def genera(carpeta):
    tipos = sorted(fn[:-len(".pieza.xml")] for fn in os.listdir(carpeta) if fn.endswith(".pieza.xml"))
    errores = []
    for tipo in tipos:
        ts, abierta, sin = terminales(os.path.join(carpeta, tipo + ".pieza.xml"))
        svg_ruta = os.path.join(carpeta, tipo + ".svg")
        if tipo in GENERADORES:
            texto = forma_xml(tipo, None, 0, 0, [], 0, 0, generador=GENERADORES[tipo])
            if os.path.exists(svg_ruta):
                os.remove(svg_ruta)
        else:
            if tipo not in PIEZAS:
                errores.append("%s: no tiene dibujo en PIEZAS ni generador en GENERADORES" % tipo)
                continue
            if abierta:
                errores.append("%s: tiene una serie abierta: tiene que ser paramétrica (GENERADORES)" % tipo)
                continue
            L = Lienzo()
            PIEZAS[tipo](L, tipo, ts)
            svg, w, h = L.svg(tipo)
            with open(svg_ruta, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(svg)
            texto = forma_xml(tipo, svg_ruta, w, h, L.anclas, L.dx, L.dy, sin=sin)
        with open(os.path.join(carpeta, tipo + ".forma.xml"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(texto)
    for t in sorted(set(PIEZAS) | set(GENERADORES)):
        if t not in tipos:
            errores.append("%s: está en dibuja_piezas.py y ya no es un tipo de mcu-sim" % t)
    return tipos, errores


def main():
    carpeta = sys.argv[1] if len(sys.argv) > 1 else CARPETA
    tipos, errores = genera(carpeta)
    for e in errores:
        print("ERROR " + e)
    print("%d tipos: %d dibujos, %d paramétricos" % (
        len(tipos), sum(1 for t in tipos if t in PIEZAS), sum(1 for t in tipos if t in GENERADORES)))
    sys.exit(1 if errores else 0)


if __name__ == "__main__":
    main()
