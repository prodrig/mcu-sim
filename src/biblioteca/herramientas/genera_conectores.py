#!/usr/bin/env python3
# =============================================================================
# genera_conectores.py — Los conectores con nombre: perfiles, conjuntos y su
# cableado en cada placa (fase B6 de doc/analisis_build.md)
#
#   python3 genera_conectores.py [SALIDA]   (por omisión, biblioteca/conectores)
#
# Escribe:
#   PERFIL.conector.xml   una tira con nombre (arduino_uno_r3.digital_alto,
#   PERFIL.svg            st_morpho.cn7, boosterpack.j2...): su forma y el USO
#                         de cada pin, de la norma o del manual de la placa;
#   CONJUNTO.conjunto.xml varios perfiles con su posición relativa (los seis
#   CONJUNTO.svg          de una Nucleo-64) y un <cableado> por cada placa del
#                         repositorio que los lleva: qué nodo va a cada pin;
#   CONJUNTO.PLACA.svg    el conjunto con el uso Y el nodo de cada pin en esa
#                         placa.
#
# DE DÓNDE SALE CADA COSA:
#   * los usos, de la norma: Arduino UNO R3 y ST morpho de UM1724 Rev 17
#     (tablas 19 y 29); BoosterPack de 20 y 40 pines de SLAA542 (figuras 3 y
#     4). Los de las tiras propias de una placa (las P1/P2 de la
#     STM32F3DISCOVERY, las de las DevKit de los ESP32), del propio XML de la
#     placa, que cita su manual: no hay norma;
#   * la geometría, del SVG de una placa de referencia: la posición del pin 1
#     de cada tira, hacia dónde crece y dónde cae la segunda fila;
#   * los <cableado>, de los XML de las placas (src/placas), que son la
#     verdad del simulador.
#
# Y SE CONTRASTA (si algo no cuadra, no se escribe nada y acaba mal):
#   * cada tira de una placa tiene la forma de su perfil (filas, columnas) y
#     pines dentro de rango;
#   * un pin de masa del perfil va a una masa en la placa, uno de
#     alimentación a una alimentación, uno de reset a un reset, y uno «NC»
#     no se usa; una E/S de una placa con MCU va a un pad de ese MCU (de
#     biblioteca/mcus, B3 y B4);
#   * la geometría reconstruida cae en cada pin del SVG de la placa de
#     referencia, a menos de 0,2 mm.
# =============================================================================
import os
import re
import sys
import xml.dom.minidom as minidom
from xml.sax.saxutils import escape

AQUI = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(AQUI, "..", ".."))
PLACAS = os.path.join(SRC, "placas")
MCUS = os.path.join(SRC, "biblioteca", "mcus")
SALIDA = os.path.join(SRC, "biblioteca", "conectores")

# --- Los perfiles de norma ----------------------------------------------------
# pin: (uso, clase, otros). Clases: es, alimentacion, masa, referencia, reset,
# control (un uso que la norma propone y una placa puede cambiar), nc
UM1724_19 = "UM1724 Rev 17, tabla 19 (Arduino UNO R3 en las Nucleo-64)"
UM1724_29 = "UM1724 Rev 17, tabla 29 (ST morpho en las Nucleo-64)"
SLAA542 = "SLAA542, figuras 3 y 4 (LaunchPad y BoosterPack de 20 y 40 pines)"

PERFILES = {
    "arduino_uno_r3.digital_bajo": ("Arduino UNO R3: D0..D7", 1, 8, UM1724_19, [
        ("D0", "es", "UART_RX"), ("D1", "es", "UART_TX"), ("D2", "es", ""), ("D3", "es", "PWM"),
        ("D4", "es", ""), ("D5", "es", "PWM"), ("D6", "es", "PWM"), ("D7", "es", "")]),
    "arduino_uno_r3.digital_alto": ("Arduino UNO R3: D8..D15", 1, 10, UM1724_19, [
        ("D8", "es", ""), ("D9", "es", "PWM"), ("D10", "es", "SPI_CS PWM"), ("D11", "es", "SPI_MOSI PWM"),
        ("D12", "es", "SPI_MISO"), ("D13", "es", "SPI_SCK"), ("GND", "masa", ""),
        ("AREF", "referencia", ""), ("D14", "es", "I2C_SDA"), ("D15", "es", "I2C_SCL")]),
    "arduino_uno_r3.analogico": ("Arduino UNO R3: A0..A5", 1, 6, UM1724_19, [
        ("A0", "es", "ADC"), ("A1", "es", "ADC"), ("A2", "es", "ADC"), ("A3", "es", "ADC"),
        ("A4", "es", "ADC I2C_SDA"), ("A5", "es", "ADC I2C_SCL")]),
    "arduino_uno_r3.alimentacion": ("Arduino UNO R3: alimentacion", 1, 8, UM1724_19, [
        ("NC", "nc", ""), ("IOREF", "alimentacion", ""), ("RESET", "reset", ""),
        ("+3V3", "alimentacion", ""), ("+5V", "alimentacion", ""), ("GND", "masa", ""),
        ("GND", "masa", ""), ("VIN", "alimentacion", "")]),
}


def _morpho(especiales):
    pines = []
    for n in range(1, 39):
        u = especiales.get(n)
        pines.append(u if u else ("E/S", "es", ""))
    return pines


PERFILES["st_morpho.cn7"] = ("ST morpho: CN7", 2, 19, UM1724_29, _morpho({
    5: ("VDD", "alimentacion", ""), 6: ("E5V", "alimentacion", ""), 7: ("BOOT0", "control", ""),
    8: ("GND", "masa", ""), 9: ("NC", "nc", ""), 10: ("NC", "nc", ""), 11: ("NC", "nc", ""),
    12: ("IOREF", "alimentacion", ""), 14: ("RESET", "reset", ""), 16: ("+3V3", "alimentacion", ""),
    18: ("+5V", "alimentacion", ""), 19: ("GND", "masa", ""), 20: ("GND", "masa", ""),
    22: ("GND", "masa", ""), 24: ("VIN", "alimentacion", ""), 26: ("NC", "nc", ""),
    33: ("VBAT", "alimentacion", "")}))
PERFILES["st_morpho.cn10"] = ("ST morpho: CN10", 2, 19, UM1724_29, _morpho({
    7: ("AVDD", "alimentacion", ""), 8: ("U5V", "alimentacion", ""), 9: ("GND", "masa", ""),
    10: ("NC", "nc", ""), 18: ("NC", "nc", ""), 20: ("GND", "masa", ""), 32: ("AGND", "masa", ""),
    36: ("NC", "nc", ""), 38: ("NC", "nc", "")}))

# BoosterPack: cada tira se numera 1..10 en las placas; `estandar` es el
# número de la norma (J2.1 es el 11). El uso es la RECOMENDACIÓN de la norma
# («TI recommends that the pins are used as shown»): solo masa, alimentación
# y reset se exigen; el resto es «control» o E/S
BP = {
    "boosterpack.j1": ("BoosterPack: J1 (1-10)", 0, [
        ("+3V3", "alimentacion", ""), ("Analog In", "es", ""), ("UART_RX", "es", "LaunchPad In"),
        ("UART_TX", "es", "LaunchPad Out"), ("GPIO", "es", "interrupcion"), ("SPI_A_SCLK", "es", ""),
        ("SPI_B_SCLK", "es", ""), ("GPIO", "es", "Cap Touch"), ("GPIO", "es", "Cap Touch"),
        ("GPIO", "es", "Cap Touch")]),
    "boosterpack.j2": ("BoosterPack: J2 (11-20)", 10, [
        ("GPIO", "es", "Cap Touch"), ("GPIO", "es", "Cap Touch"), ("GPIO", "es", "Cap Touch"),
        ("SPI_B_SOMI", "es", "I2C_SCL"), ("SPI_B_SIMO", "es", "I2C_SDA"), ("RESET", "reset", ""),
        ("TEST", "control", "Spy-Bi-Wire"), ("GPIO", "es", "interrupcion"), ("Timer Out", "es", ""),
        ("GND", "masa", "")]),
    "boosterpack.j3": ("BoosterPack: J3 (21-30)", 20, [
        ("+5V", "alimentacion", ""), ("GND", "masa", "")] + [("Analog", "es", "")] * 8),
    "boosterpack.j4": ("BoosterPack: J4 (31-40)", 30, [("Timer", "es", "")] * 10),
}
for _p, (_t, _base, _pines) in BP.items():
    PERFILES[_p] = (_t, 1, 10, SLAA542, [(u, c, o, _base + i + 1) for i, (u, c, o) in enumerate(_pines)])

# --- Los conjuntos ----------------------------------------------------------
# geometria: (placa de referencia, {miembro: id de su tira en esa placa})
# placas: {placa: {id de la tira en la placa: miembro}}
CONJUNTOS = {
    "nucleo64": dict(
        titulo="Nucleo-64: Arduino UNO R3 + ST morpho",
        miembros={"CN5": "arduino_uno_r3.digital_alto", "CN6": "arduino_uno_r3.alimentacion",
                  "CN8": "arduino_uno_r3.analogico", "CN9": "arduino_uno_r3.digital_bajo",
                  "CN7": "st_morpho.cn7", "CN10": "st_morpho.cn10"},
        geometria="nucleo_f446re",
        placas={"nucleo_f446re": None, "x_nucleo_ihm07m1": {"CN7": "CN7", "CN10": "CN10"}}),
    "arduino_uno_r3": dict(
        titulo="Arduino UNO R3 (los cuatro conectores de un shield)",
        miembros={"CN5": "arduino_uno_r3.digital_alto", "CN6": "arduino_uno_r3.alimentacion",
                  "CN8": "arduino_uno_r3.analogico", "CN9": "arduino_uno_r3.digital_bajo"},
        geometria="cnc_shield_v3:J",
        placas={"cnc_shield_v3": {"J5": "CN5", "J6": "CN6", "J8": "CN8", "J9": "CN9"},
                "shield_leds": {"J5": "CN5", "J6": "CN6", "J8": "CN8", "J9": "CN9"},
                "nucleo_f446re": {"CN5": "CN5", "CN6": "CN6", "CN8": "CN8", "CN9": "CN9"}}),
    "boosterpack40": dict(
        titulo="LaunchPad / BoosterPack de 40 pines (J1-J4)",
        miembros={"J1": "boosterpack.j1", "J2": "boosterpack.j2", "J3": "boosterpack.j3", "J4": "boosterpack.j4"},
        geometria="msp_exp432p401r",
        placas={"msp_exp432p401r": None, "msp_exp432e401y": None, "msp_exp430f5529lp": None,
                "msp_exp430fr2355": None, "lp_msp430fr2476": None, "boostxl_edumkii": None}),
    "boosterpack20": dict(
        titulo="LaunchPad / BoosterPack de 20 pines (J1 y J2)",
        miembros={"J1": "boosterpack.j1", "J2": "boosterpack.j2"},
        geometria="msp_exp430g2et",
        placas={"msp_exp430g2et": None, "msp_exp430fr2311": None, "msp_exp430fr2433": None,
                "msp_exp430fr4133": None, "msp_exp430fr5969": None}),
}

# Las tiras PROPIAS de una placa, sin norma: su perfil sale de la placa (el uso
# de cada pin es su nodo, que el XML cita de su manual)
DE_PLACA = {
    "discovery_f3": ("STM32F3DISCOVERY: P1 y P2", "stm32f3discovery",
                     {"EXP1": "discovery_f3.p1", "EXP2": "discovery_f3.p2"},
                     "UM1570 Rev 7, tabla 7, como la transcribe placas/stm32f3discovery.xml"),
    "esp32c3_devkitm1": ("ESP32-C3-DevKitM-1: J1 y J3", "esp32c3_devkitm1",
                         {"J1": "esp32c3_devkitm1.j1", "J3": "esp32c3_devkitm1.j3"},
                         "la guía de la ESP32-C3-DevKitM-1, como la transcribe placas/esp32c3_devkitm1.xml"),
    "esp32c6_devkitc1": ("ESP32-C6-DevKitC-1: J1 y J3", "esp32c6_devkitc1",
                         {"J1": "esp32c6_devkitc1.j1", "J3": "esp32c6_devkitc1.j3"},
                         "la guía de la ESP32-C6-DevKitC-1, como la transcribe placas/esp32c6_devkitc1.xml"),
}

MASA = re.compile(r"^(D|A)?(VSS|GND)\w*$|^GND\d*$|^AGND$|^VSSA$|^GNDP?$", re.I)
RESET = re.compile(r"^(N?RST\w*|RESET|NRST|RSTN|CHIP_(EN|PU)|EN)$", re.I)


# --- Las placas ---------------------------------------------------------------
class Placa:
    def __init__(self, nombre):
        self.nombre = nombre
        # Sin los comentarios: algunas placas llevan «--» dentro, que el
        # lector de mcu-sim acepta y uno de XML estricto no
        texto = open(os.path.join(PLACAS, nombre + ".xml"), encoding="utf-8").read()
        d = minidom.parseString(re.sub(r"<!--.*?-->", "", texto, flags=re.S))
        raiz = d.documentElement
        self.mcus = [m.getAttribute("tipo") for m in raiz.getElementsByTagName("mcu")
                     if m.parentNode == raiz]
        self.tiras = {}
        for c in raiz.getElementsByTagName("componente"):
            if c.getAttribute("tipo") != "Conector" or c.parentNode != raiz:
                continue
            pines = {}
            for p in c.getElementsByTagName("pin"):
                pines[p.getAttribute("nombre")] = p.getAttribute("nodo")
            self.tiras[c.getAttribute("id")] = dict(
                filas=int(c.getAttribute("filas") or 1), columnas=int(c.getAttribute("columnas") or 1),
                numeracion=c.getAttribute("numeracion") or "zigzag", nombres=c.getAttribute("nombres"),
                pines=pines)

    def pads(self):
        out = set()
        for t in self.mcus:
            f = os.path.join(MCUS, t + ".mcu.xml")
            if os.path.exists(f):
                for p in minidom.parse(f).getElementsByTagName("patilla"):
                    if p.getAttribute("clase") == "es":
                        out.add(p.getAttribute("nodo"))
        return out

    def posiciones(self):
        """{id del elemento: (x, y)} de los pines de conector del SVG, en mm."""
        f = os.path.join(PLACAS, self.nombre + ".svg")
        out = {}
        for m in re.finditer(r'<rect id="([A-Za-z0-9_]+\.\d+)" x="([\d.]+)" y="([\d.]+)" '
                             r'width="([\d.]+)" height="([\d.]+)"', open(f, encoding="utf-8").read()):
            out[m.group(1)] = ((float(m.group(2)) + float(m.group(4)) / 2) / 10,
                               (float(m.group(3)) + float(m.group(5)) / 2) / 10)
        return out


def numero(k, filas, columnas):
    """(columna, fila) del pin k con numeración en zigzag (la de las tiras de
    estas placas)."""
    if filas == 1:
        return k - 1, 0
    return (k - 1) // 2, (k - 1) % 2


def geometria(placa, ids, perfiles):
    """{miembro: (x, y, dx, dy, fx, fy)} en mm desde el pin 1 del primero:
    el pin 1, el paso a la columna siguiente y a la otra fila."""
    pos = placa.posiciones()
    g, errores = {}, []
    for miembro, tira in ids.items():
        _, filas, columnas, _, _ = perfiles[miembro]
        try:
            p1 = pos["%s.1" % tira]
            sig = pos["%s.%d" % (tira, 3 if filas == 2 else 2)]
            otra = pos["%s.2" % tira] if filas == 2 else p1
        except KeyError as e:
            errores.append("%s: el SVG no tiene el pin %s" % (placa.nombre, e))
            continue
        g[miembro] = (p1[0], p1[1], sig[0] - p1[0], sig[1] - p1[1], otra[0] - p1[0], otra[1] - p1[1])
        # La prueba: la geometría reconstruida cae en cada pin del dibujo
        for k in range(1, filas * columnas + 1):
            c, fi = numero(k, filas, columnas)
            x = p1[0] + c * g[miembro][2] + fi * g[miembro][4]
            y = p1[1] + c * g[miembro][3] + fi * g[miembro][5]
            q = pos.get("%s.%d" % (tira, k))
            if q is None or abs(q[0] - x) > 0.2 or abs(q[1] - y) > 0.2:
                errores.append("%s: el pin %s.%d no cae donde dice la geometría" % (placa.nombre, tira, k))
                break
    if g:
        x0 = min(v[0] for v in g.values())
        y0 = min(v[1] for v in g.values())
        g = {m: (v[0] - x0, v[1] - y0) + v[2:] for m, v in g.items()}
    return g, errores


def contrasta(conjunto, perfiles, miembros, placa, mapa):
    """[(miembro, pin, nodo)] del cableado de `placa`, y los errores."""
    errores, cableado = [], []
    pads = placa.pads()
    for tira, miembro in mapa.items():
        if tira not in placa.tiras:
            errores.append("%s: no tiene la tira %s" % (placa.nombre, tira))
            continue
        t = placa.tiras[tira]
        titulo, filas, columnas, _, pines = perfiles[miembros[miembro]]
        if (t["filas"], t["columnas"]) != (filas, columnas):
            errores.append("%s.%s: es de %dx%d y su perfil %s de %dx%d" % (
                placa.nombre, tira, t["filas"], t["columnas"], miembros[miembro], filas, columnas))
            continue
        for k, nodo in sorted(t["pines"].items(), key=lambda x: int(x[0]) if x[0].isdigit() else 0):
            if not k.isdigit() or not 1 <= int(k) <= filas * columnas:
                errores.append("%s.%s.%s: no es un pin de la tira" % (placa.nombre, tira, k))
                continue
            uso, clase = pines[int(k) - 1][0], pines[int(k) - 1][1]
            n = nodo.split(".", 1)[1] if "." in nodo and nodo.split(".", 1)[0] in ("u0", "u1") else nodo
            malo = None
            if clase == "nc":
                malo = "es NC y la placa lo usa"
            elif clase == "masa" and not MASA.match(n):
                malo = "es masa"
            elif clase == "alimentacion" and (MASA.match(n) or n in pads):
                malo = "es alimentación"
            elif clase == "reset" and placa.mcus and not RESET.match(n):
                # (en un shield, sin MCU, la red que va al RESET se llama
                # como quiera: la del CNC Shield V3 es su E-stop)
                malo = "es reset"
            elif clase == "es" and pads and n not in pads and not MASA.match(n) and placa.mcus:
                malo = "es una E/S y no va a un pad de %s" % "/".join(placa.mcus)
            if malo:
                errores.append("%s.%s.%s (%s) va a %s: el perfil dice que %s" % (
                    placa.nombre, tira, k, uso, nodo, malo))
            cableado.append((miembro, int(k), nodo))
    return cableado, errores


# --- La escritura ------------------------------------------------------------
def f(v):
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


CABECERA = ("<!-- %s (doc/analisis_build.md, B6).\n"
            "     Lo genera biblioteca/herramientas/genera_conectores.py: no se edita a mano. -->\n")


def perfil_xml(nombre, perfil):
    titulo, filas, columnas, fuente, pines = perfil
    s = ['<?xml version="1.0" encoding="UTF-8"?>', CABECERA.rstrip() % ("El perfil de conector " + nombre),
         '<conector perfil="%s" version="1" filas="%d" columnas="%d" numeracion="zigzag" titulo="%s" fuente="%s">'
         % (nombre, filas, columnas, escape(titulo), escape(fuente))]
    for k, p in enumerate(pines, 1):
        a = ' otros="%s"' % escape(p[2]) if p[2] else ""
        e = ' estandar="%d"' % p[3] if len(p) > 3 else ""
        s.append('  <pin n="%d" uso="%s" clase="%s"%s%s/>' % (k, escape(p[0]), p[1], a, e))
    s.append("</conector>")
    return "\n".join(s) + "\n"


COLOR = {"es": "#1a5fb4", "alimentacion": "#c01c28", "masa": "#333333", "referencia": "#9141ac",
         "reset": "#e66100", "control": "#986a44", "nc": "#9a9996"}


def _tira(miembro, perfil, nodos, g, fuera):
    """Una tira dibujada en vertical, con sus rótulos al lado: ([elementos],
    caja) en coordenadas propias. `g` es su geometría real (para saber si el
    pin 1 va arriba o abajo y de qué lado cae la segunda fila); `fuera`, -1
    o 1, el lado de los rótulos de una tira de una fila."""
    _, filas, columnas, _, pines = perfil
    x, y, dx, dy, fx, fy = g
    abajo_arriba = (dy < 0) if abs(dy) > abs(dx) else (dx < 0)
    segunda = 1 if (fx if abs(dy) > abs(dx) else fy) >= 0 else -1
    els, caja = [], [1e9, 1e9, -1e9, -1e9]

    def amplia(x0, y0, x1, y1):
        caja[0], caja[1], caja[2], caja[3] = min(caja[0], x0), min(caja[1], y0), max(caja[2], x1), max(caja[3], y1)
    for k in range(1, filas * columnas + 1):
        c, fi = numero(k, filas, columnas)
        py = (columnas - 1 - c if abajo_arriba else c) * 25.4
        px = fi * 25.4 * segunda
        uso, clase = pines[k - 1][0], pines[k - 1][1]
        els.append('<rect id="$.%s.%d" x="%s" y="%s" width="16" height="16" fill="#d4ac0d" '
                   'stroke="#9a7d0a" stroke-width="1"/>' % (miembro, k, f(px - 8), f(py - 8)))
        amplia(px - 8, py - 8, px + 8, py + 8)
        if filas == 2:
            lado = -1 if (fi == 0) == (segunda > 0) else 1
        else:
            lado = fuera
        texto = "%d %s" % (k, uso) + (" %s" % nodos[(miembro, k)] if (miembro, k) in nodos else "")
        tx = px + lado * 14
        w = len(texto) * 5.4
        els.append('<text x="%s" y="%s" font-size="9" text-anchor="%s" fill="%s">%s</text>' % (
            f(tx), f(py + 3), "start" if lado > 0 else "end", COLOR.get(clase, "#000"), escape(texto)))
        amplia(tx if lado > 0 else tx - w, py - 6, tx + w if lado > 0 else tx, py + 6)
    xc = (caja[0] + caja[2]) / 2
    els.append('<text x="%s" y="%s" font-size="11" font-weight="bold" fill="#333" text-anchor="middle">%s</text>'
               % (f(xc), f(caja[1] - 6), escape(miembro)))
    amplia(xc - 20, caja[1] - 18, xc + 20, caja[1])
    return els, caja


def dibujo(nombre, titulo, miembros, perfiles, geo, cableado=None):
    """El SVG de un perfil o de un conjunto, DESPIEZADO: cada tira con el uso
    (y el nodo, con un cableado) de cada pin al lado, en los colores de los
    pinouts de ST, en el orden y la orientación de la placa pero separadas lo
    justo para que los rótulos no se pisen. La posición real de cada tira está
    en el .conjunto.xml."""
    nodos = {(m, k): n for m, k, n in (cableado or [])}
    xc = sum(g[0] for g in geo.values()) / len(geo)
    # Las columnas: las tiras que caen a la misma x (a menos de 1 mm), de
    # izquierda a derecha; dentro de una columna, de arriba abajo
    orden = sorted(geo, key=lambda m: (geo[m][0], geo[m][1]))
    columnas = []
    for m in orden:
        if columnas and abs(geo[m][0] - geo[columnas[-1][0]][0]) < 1:
            columnas[-1].append(m)
        else:
            columnas.append([m])
    els, x = [], 0.0
    alto = 0.0
    for col in columnas:
        col.sort(key=lambda m: geo[m][1])
        dibujos = []
        for m in col:
            fuera = -1 if geo[m][0] < xc or len(geo) == 1 and False else 1
            if len(geo) == 1:
                fuera = 1
            dibujos.append((m,) + _tira(m, perfiles[miembros[m]], nodos, geo[m], fuera))
        ancho = max(c[2] - c[0] for _, _, c in dibujos)
        y = 0.0
        for m, e, c in dibujos:
            ox, oy = x - c[0], y - c[1]
            els.append('<g transform="translate(%s,%s)">' % (f(ox), f(oy)))
            els += ["  " + t for t in e]
            els.append("</g>")
            y += c[3] - c[1] + 30
        alto = max(alto, y - 30)
        x += ancho + 40
    m = 20
    w, h = max(x - 40, len(titulo) * 7.2) + 2 * m, alto + 2 * m + 30
    out = ('<svg xmlns="http://www.w3.org/2000/svg" version="1.2" baseProfile="tiny" viewBox="0 0 %s %s" '
           'width="%smm" height="%smm">\n' % (f(w), f(h), "%.2f" % (w / 10), "%.2f" % (h / 10)))
    out += CABECERA % ("%s: %s. Despiezado; unidades de 0.1 mm; cada pin es $.TIRA.N" % (nombre, titulo))
    out += '<g id="$" transform="translate(%s,%s)" font-family="sans-serif">\n' % (f(m), f(m + 30))
    out += '  <text x="0" y="-16" font-size="12" font-weight="bold" fill="#000">%s</text>\n' % escape(titulo)
    out += "".join("  %s\n" % e for e in els) + "</g>\n</svg>\n"
    return out


def conjunto_xml(nombre, titulo, miembros, geo, cableados, fuente_geo):
    s = ['<?xml version="1.0" encoding="UTF-8"?>', CABECERA.rstrip() % ("El conjunto de conectores " + nombre),
         "<!-- x, y: el pin 1 de cada tira, en mm; dx, dy: el paso a la columna",
         "     siguiente; fx, fy: el paso a la otra fila. De %s. -->" % fuente_geo,
         '<conjunto nombre="%s" version="1" titulo="%s">' % (nombre, escape(titulo))]
    for m, p in miembros.items():
        x, y, dx, dy, fx, fy = geo[m]
        s.append('  <miembro id="%s" perfil="%s" x="%s" y="%s" dx="%s" dy="%s" fx="%s" fy="%s"/>'
                 % (m, p, f(x), f(y), f(dx), f(dy), f(fx), f(fy)))
    for placa, mcus, cab in cableados:
        s.append('  <cableado placa="placas/%s.xml"%s>' % (placa, ' mcu="%s"' % " ".join(mcus) if mcus else ""))
        for m, k, n in cab:
            s.append('    <pin p="%s.%d" nodo="%s"/>' % (m, k, escape(n)))
        s.append("  </cableado>")
    s.append("</conjunto>")
    return "\n".join(s) + "\n"


def genera(salida):
    errores = []
    perfiles = dict(PERFILES)
    conjuntos = {k: dict(v) for k, v in CONJUNTOS.items()}
    # Los perfiles de placa: el uso de cada pin es su nodo
    for nombre, (titulo, placa, tiras, fuente) in DE_PLACA.items():
        pl = Placa(placa)
        miembros = {}
        for tira, perfil in tiras.items():
            t = pl.tiras[tira]
            pines = []
            for k in range(1, t["filas"] * t["columnas"] + 1):
                n = t["pines"].get(str(k))
                if n is None:
                    pines.append(("NC", "nc", ""))
                elif MASA.match(n):
                    pines.append((n, "masa", ""))
                elif n in pl.pads():
                    pines.append((n, "es", ""))
                elif RESET.match(n):
                    pines.append((n, "reset", ""))
                else:
                    pines.append((n, "alimentacion", ""))
            perfiles[perfil] = ("%s: %s" % (titulo.split(":")[0], tira), t["filas"], t["columnas"], fuente, pines)
            miembros[tira] = perfil
        conjuntos[nombre] = dict(titulo=titulo, miembros=miembros, geometria=placa, placas={placa: None})
    os.makedirs(salida, exist_ok=True)
    escritos = []
    for nombre, perfil in sorted(perfiles.items()):
        _, filas, columnas, _, pines = perfil
        if len(pines) != filas * columnas:
            errores.append("%s: %d usos para %d pines" % (nombre, len(pines), filas * columnas))
            continue
        escritos.append((nombre + ".conector.xml", perfil_xml(nombre, perfil)))
        geo = {nombre.split(".")[-1].upper(): (0, 0, 2.54 if filas == 1 else 2.54, 0, 0, 2.54 if filas == 2 else 0)}
        m = {nombre.split(".")[-1].upper(): nombre}
        escritos.append((nombre + ".svg", dibujo(nombre, perfil[0], m, perfiles, geo)))
    for nombre, c in sorted(conjuntos.items()):
        ref = c["geometria"].split(":")[0]
        pl = Placa(ref)
        ids = {m: m for m in c["miembros"]}
        if c["placas"].get(ref):
            ids = {m: t for t, m in c["placas"][ref].items()}
        geo, e = geometria(pl, ids, {m: perfiles[p] for m, p in c["miembros"].items()})
        errores += ["%s: %s" % (nombre, x) for x in e]
        if len(geo) != len(c["miembros"]):
            continue
        cableados = []
        for placa, mapa in c["placas"].items():
            p = Placa(placa)
            mapa = mapa or {m: m for m in c["miembros"]}
            cab, e = contrasta(nombre, perfiles, c["miembros"], p, mapa)
            errores += ["%s: %s" % (nombre, x) for x in e]
            cableados.append((placa, p.mcus, cab))
            escritos.append(("%s.%s.svg" % (nombre, placa), dibujo(
                "%s.%s" % (nombre, placa), "%s en %s" % (c["titulo"], placa), c["miembros"], perfiles, geo, cab)))
        escritos.append((nombre + ".conjunto.xml", conjunto_xml(
            nombre, c["titulo"], c["miembros"], geo, cableados, "placas/%s.svg" % ref)))
        escritos.append((nombre + ".svg", dibujo(nombre, c["titulo"], c["miembros"], perfiles, geo)))
    if errores:
        return errores, 0
    for fn in os.listdir(salida):
        if fn.endswith((".xml", ".svg")):
            os.remove(os.path.join(salida, fn))
    for fn, texto in escritos:
        with open(os.path.join(salida, fn), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(texto)
    return [], len(escritos)


def main():
    salida = sys.argv[1] if len(sys.argv) > 1 else SALIDA
    errores, n = genera(salida)
    for e in errores:
        print("ERROR " + e)
    if errores:
        print("%d errores: no se escribe nada" % len(errores))
        sys.exit(1)
    print("%d ficheros en %s: %d perfiles y %d conjuntos, contrastados con sus placas" % (
        n, os.path.relpath(salida), len(PERFILES) + sum(len(v[2]) for v in DE_PLACA.values()),
        len(CONJUNTOS) + len(DE_PLACA)))


if __name__ == "__main__":
    main()
