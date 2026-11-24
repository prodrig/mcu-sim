# =============================================================================
# mcus_comun.py — Lo común a todas las fuentes de patillaje de MCU: el modelo
# de datos, el CONTRASTE con lo que simula mcu-sim, el .mcu.xml y los dos SVG
#
# Fases B3 y B4 de doc/analisis_build.md (decisión D1-A). Cada fuente -Open
# Pin Data para ST, SysConfig y los datasheets para TI, los datasheets y
# ESP-IDF para Espressif- produce un `Chip` con sus patillas y las funciones
# de cada una. Aquí:
#
#   1. se CONTRASTA con lo que dice el modelo (`mcu-sim --exporta-mcu`, B2):
#      los pads que salen tienen que ser los mismos, y cada función que el
#      modelo registra -con su número de selección- tiene que estar en la
#      fuente, en el mismo pad y con el mismo número. Si no, es un error de
#      uno de los dos, y el generador se para y dice cuál;
#   2. se marca cada función de la fuente como `simulada` o no: sí si el
#      modelo la registra -en los STM32F4, que no dan el nombre, la del mismo
#      pin, el mismo AF y el mismo periférico que la señal del modelo-; las que no se eligen con un número (las
#      analógicas, los osciladores) lo dicen POR PERIFÉRICO, con la lista
#      de cada fuente (`por="periferico"`);
#   3. se escriben `TIPO.mcu.xml`, `TIPO.detalle.svg` y `TIPO.simple.svg`.
#
# EL DIBUJO. Los dos SVG son el encapsulado visto desde arriba, A ESCALA, en
# unidades de 0.1 mm como los de placas/, y SVG 1.2 Tiny (perfil de autor de
# mcu-sim-gui, doc/analisis-uso-ilustraciones.md §11):
#   detalle  el cuerpo, cada patilla en su sitio con id="$.N" -N es su
#            posición: 21, o A1 en una rejilla de bolas- y, fuera, su número y
#            su nombre. Es el que se usa para conectar en mcu-sim-build
#   simple   el cuerpo y las patillas, sin rótulos, con la referencia encima.
#            Es el que va por omisión al SVG de una placa
# La raíz de los dos es <g id="$">: mcu-sim-build cambia `$` por el id del
# MCU en la placa (§4.2 del análisis).
# =============================================================================
import re
import xml.dom.minidom as minidom
from xml.sax.saxutils import escape, quoteattr

ESCALA = 10          # unidades del SVG por mm


class ErrorContraste(Exception):
    pass


class Funcion:
    def __init__(self, nombre, modo=None, analogica=False):
        self.nombre = nombre
        self.modo = modo            # el número que elige el firmware, o None
        self.analogica = analogica
        self.simulada = False
        self.por = ""               # "periferico" si se decide así
        self.sentido = ""           # en la matriz de un ESP32: entrada o salida


class Patilla:
    def __init__(self, pos, nombre, clase, pad=None):
        self.pos = str(pos)         # 21, o A1
        self.nombre = nombre        # como lo escribe la fuente
        self.clase = clase          # es, alimentacion, reset, arranque, control
        self.pad = pad              # el pad del modelo (PA5, P1.0), si es E/S
        self.nodo = pad or ""       # el nodo del XML de placa
        self.funciones = []


class Chip:
    def __init__(self, tipo, fuente):
        self.tipo = tipo
        self.fuente = fuente        # de dónde salen los datos, con versión
        self.familia = ""
        self.encapsulado = ""
        self.patillas = []
        self.mapeo = None           # dict(tipo, puertos, funciones) o None
        self.avisos = []


# --- El modelo (B2) ----------------------------------------------------------
class Modelo:
    """Lo que dice `mcu-sim --exporta-mcu TIPO`."""

    def __init__(self, ruta):
        d = minidom.parse(ruta).documentElement
        self.tipo = d.getAttribute("tipo")
        self.familia = d.getAttribute("familia")
        self.encapsulado = d.getAttribute("encapsulado")
        self.patillas = int(d.getAttribute("patillas"))
        self.inventario = not d.getElementsByTagName("sin_inventario")
        self.pads = {}              # nombre -> [(modo, nombre, senal, analogica)]
        for p in d.getElementsByTagName("pad"):
            fs = []
            for f in p.getElementsByTagName("funcion"):
                m = f.getAttribute("modo")
                fs.append((int(m) if m else None, f.getAttribute("nombre"),
                           f.getAttribute("senal"), f.getAttribute("analogica") == "si"))
            self.pads[p.getAttribute("nombre")] = fs
        self.alimentacion = [a.getAttribute("nombre")
                             for a in d.getElementsByTagName("alimentacion")]
        self.mapeables = []
        self.mapeo = None
        self.puertos_mapeo = ""
        for m in d.getElementsByTagName("mapeo"):
            self.mapeo = m.getAttribute("tipo")
            self.puertos_mapeo = m.getAttribute("puertos")
            for f in m.getElementsByTagName("funcion"):
                mm = f.getAttribute("modo")
                self.mapeables.append((int(mm) if mm else None, f.getAttribute("nombre"),
                                       f.getAttribute("senal")))


def nodo_de_alimentacion(nombre, modelo):
    """El nodo del modelo para una patilla de alimentación o control de la
    fuente: `VCAP_1` es `VCAP1`, `VDDA/VREF+` es el primero que exista."""
    for parte in re.split(r"[/]", nombre):
        for cand in (parte, parte.replace("_", ""), parte.split("-")[0]):
            if cand in modelo.alimentacion:
                return cand
    return ""


# Cómo se llama en los manuales el periférico de una señal del modelo
_PERIFERICO = {"OTG_HS": ("USB_OTG_HS",), "OTG_FS": ("USB_OTG_FS",), "FSMC": ("FSMC", "FMC"),
               "QSPI": ("QUADSPI",), "RCC": ("RCC_MCO",), "CEC": ("CEC", "HDMI_CEC")}


def mismo_periferico(senal, nombre):
    p = senal.split(".")[0].upper()
    m = re.match(r"^(I2S\d)EXT$", p)
    cands = _PERIFERICO.get(p, (p,))
    if m:
        cands = (m.group(1) + "_EXT",)
    if re.match(r"^SPI\d$", p):
        cands = (p, "I2S" + p[3:])
    n = nombre.upper()
    return any(n.startswith(c + "_") or n == c or n.startswith(c) for c in cands)


def contrasta(chip, modelo, simulado_por_periferico, nombres_iguales=False):
    """Une la fuente con el modelo. Se para (ErrorContraste) si no cuadran."""
    if chip.encapsulado != modelo.encapsulado:
        raise ErrorContraste("%s: la fuente es del %s y el modelo del %s" %
                             (chip.tipo, chip.encapsulado, modelo.encapsulado))
    chip.familia = modelo.familia
    es = [p for p in chip.patillas if p.clase == "es"]
    pads_fuente = {p.pad for p in es}
    pads_modelo = set(modelo.pads)
    if pads_fuente != pads_modelo:
        raise ErrorContraste("%s: los pads no son los mismos. Solo en la fuente: %s. Solo "
                             "en el modelo: %s" % (chip.tipo,
                                                   sorted(pads_fuente - pads_modelo),
                                                   sorted(pads_modelo - pads_fuente)))
    if len(chip.patillas) != modelo.patillas and not chip.encapsulado.startswith(("WLCSP", "UFBGA", "BGA")):
        chip.avisos.append("la fuente trae %d patillas y el encapsulado del modelo dice %d"
                           % (len(chip.patillas), modelo.patillas))
    # Las funciones del modelo, una a una, tienen que estar en la fuente
    faltan = []
    for p in es:
        for (modo, nombre, senal, analogica) in modelo.pads[p.pad]:
            if modo is None:
                cand = [f for f in p.funciones if f.analogica and (not nombre or f.nombre == nombre)]
            else:
                cand = [f for f in p.funciones if f.modo == modo]
                if nombre and nombres_iguales:
                    cand = [f for f in cand if f.nombre == nombre]
            if not cand:
                if modo is not None and "_evt_out" in senal and modo == 15:
                    continue      # EVENTOUT: ST lo da como modo de E/S, no como señal
                fam = getattr(chip, "solo_familia", {}).get((p.pad, modo))
                if fam:
                    chip.avisos.append("el modelo registra %s en %s (modo %d), que la familia "
                                       "tiene y este encapsulado no lista" % (fam, p.pad, modo))
                    continue
                faltan.append("%s %s%s (%s)" % (p.pad, ("modo %d" % modo) if modo is not None else "",
                                                (" " + nombre) if nombre else "", senal or "-"))
                continue
            # Si la señal del modelo dice el periférico, solo las de ese
            if modo is not None and not nombre and senal and "." in senal:
                mismas = [f for f in cand if mismo_periferico(senal, f.nombre)]
                if mismas:
                    cand_marca = mismas
                else:
                    cand_marca = cand
            else:
                cand_marca = cand
            for f in cand_marca:
                f.simulada = True
            # Con el número basta para decir si se simula; el periférico de la
            # señal del modelo tiene que ser además el de alguna función con
            # ese número. Si no, se avisa: es un número bien puesto a un
            # periférico que el chip no tiene ahí
            if modo is not None and not nombre and senal and "." in senal:
                if not any(mismo_periferico(senal, f.nombre) for f in cand):
                    chip.avisos.append("%s modo %d: el modelo pone %s, y la fuente %s"
                                       % (p.pad, modo, senal, ", ".join(f.nombre for f in cand)))
    if faltan:
        raise ErrorContraste("%s: el modelo simula %d funciones que la fuente no tiene en ese "
                             "pad con ese modo: %s" % (chip.tipo, len(faltan), "; ".join(faltan)))
    # Lo que no se elige con un número: por periférico
    for p in es:
        for f in p.funciones:
            if f.modo is None and not f.simulada:
                f.simulada = any(re.match(r, f.nombre) for r in simulado_por_periferico)
                f.por = "periferico"
    # La alimentación y el control: su nodo
    for p in chip.patillas:
        if p.clase != "es":
            p.nodo = nodo_de_alimentacion(p.nombre, modelo)
            if not p.nodo:
                chip.avisos.append("la patilla %s (%s) no tiene nodo en el modelo"
                                   % (p.pos, p.nombre))


# --- El .mcu.xml -------------------------------------------------------------
def mcu_xml(chip):
    def funcion(f, sangria):
        a = ' nombre=%s' % quoteattr(f.nombre)
        if f.modo is not None:
            a += ' modo="%d"' % f.modo
        if f.analogica:
            a += ' analogica="si"'
        a += ' simulada="%s"' % ("si" if f.simulada else "no")
        if f.por:
            a += ' por="%s"' % f.por
        if f.sentido:
            a += ' sentido="%s"' % f.sentido
        return "%s<funcion%s/>\n" % (sangria, a)

    def orden(f):
        return (f.modo is None, f.modo if f.modo is not None else 0, f.nombre)

    x = ['<?xml version="1.0" encoding="UTF-8"?>\n',
         "<!-- El patillaje del %s y lo que puede hacer cada patilla, contrastado con lo\n"
         "     que simula el modelo de mcu-sim. Lo genera biblioteca/herramientas/genera_mcus.py\n"
         "     (doc/analisis_build.md, B3 y B4): no se edita a mano. -->\n" % chip.tipo,
         '<mcu tipo=%s familia=%s encapsulado=%s patillas="%d" fuente=%s contrastado="si" '
         'version="1">\n' % (quoteattr(chip.tipo), quoteattr(chip.familia),
                             quoteattr(chip.encapsulado), len(chip.patillas),
                             quoteattr(chip.fuente))]
    for p in sorted(chip.patillas, key=lambda p: clave_pos(p.pos)):
        a = '<patilla n=%s nombre=%s clase="%s" nodo=%s' % (
            quoteattr(p.pos), quoteattr(p.nombre), p.clase, quoteattr(p.nodo))
        fs = sorted(p.funciones, key=orden)
        if not fs:
            x.append("  %s/>\n" % a)
            continue
        x.append("  %s>\n" % a)
        x.extend(funcion(f, "    ") for f in fs)
        x.append("  </patilla>\n")
    if chip.mapeo:
        m = chip.mapeo
        x.append('  <mapeo tipo=%s puertos=%s>\n' % (quoteattr(m["tipo"]),
                                                    quoteattr(m.get("puertos", ""))))
        x.extend(funcion(f, "    ") for f in sorted(m["funciones"], key=orden))
        x.append("  </mapeo>\n")
    x.append("</mcu>\n")
    return "".join(x)


def clave_pos(pos):
    m = re.match(r"^([A-Z]*)(\d+)$", pos)
    return (m.group(1), int(m.group(2))) if m else (pos, 0)


# --- Los encapsulados ---------------------------------------------------------
# tipo de dibujo, paso entre patillas (mm), y cuerpo (mm) si no se deduce
ENCAPSULADOS = {
    "LQFP48": ("qfp", 0.5, 7.0), "LQFP64": ("qfp", 0.5, 10.0),
    "LQFP80": ("qfp", 0.5, 12.0), "LQFP100": ("qfp", 0.5, 14.0),
    "LQFP144": ("qfp", 0.5, 20.0), "LQFP176": ("qfp", 0.5, 24.0),
    "TQFP128": ("qfp", 0.4, 14.0),
    "QFN32": ("qfn", 0.5, 5.0), "QFN40": ("qfn", 0.4, 5.0),
    "VQFN24": ("qfn", 0.5, 4.0), "VQFN48": ("qfn", 0.5, 7.0),
    "PDIP20": ("dip", 2.54, None), "TSSOP20": ("sop", 0.65, None),
    "TSSOP28": ("sop", 0.65, None),
    "WLCSP90": ("bga", 0.4, None), "WLCSP81": ("bga", 0.4, None),
}


def geometria(encapsulado, posiciones):
    """Para cada posición: (x, y, ancho, alto) de la patilla, el lado al que
    sale su rótulo ('i', 'd', 'a', 'b' o 'c' de centro) y el cuerpo. En mm."""
    tipo, paso, cuerpo = ENCAPSULADOS[encapsulado]
    g = {}
    if tipo in ("qfp", "qfn"):
        n = len(posiciones)
        # Una QFN con 4k+1 patillas: la última es la almohadilla de debajo
        # (la 33 del QFN32 de los ESP32, a GND), en el centro
        almohadilla = tipo == "qfn" and n % 4 == 1
        if almohadilla:
            n -= 1
        k = n // 4
        L = cuerpo
        pata = 1.0 if tipo == "qfp" else 0.0
        largo = 1.0 if tipo == "qfp" else 0.5
        ancho = paso * 0.55
        ini = (L - (k - 1) * paso) / 2.0
        for j in range(n):
            pos = str(j + 1)
            s, t = divmod(j, k)
            if s == 0:    # izquierda, de arriba abajo
                g[pos] = (-pata, ini + t * paso, "i")
            elif s == 1:  # abajo, de izquierda a derecha
                g[pos] = (ini + t * paso, L + pata, "b")
            elif s == 2:  # derecha, de abajo arriba
                g[pos] = (L + pata, L - ini - t * paso, "d")
            else:         # arriba, de derecha a izquierda
                g[pos] = (L - ini - t * paso, -pata, "a")
        res = {}
        for pos, (x, y, lado) in g.items():
            if lado in "id":
                x0 = (-largo if lado == "i" else L) if tipo == "qfp" else (0 if lado == "i" else L - largo)
                res[pos] = (x0, y - ancho / 2, largo, ancho, lado)
            else:
                y0 = (-largo if lado == "a" else L) if tipo == "qfp" else (0 if lado == "a" else L - largo)
                res[pos] = (x - ancho / 2, y0, ancho, largo, lado)
        if almohadilla:
            a = L * 0.6
            res[str(n + 1)] = ((L - a) / 2, (L - a) / 2, a, a, "c")
        return res, (0.0, 0.0, L, L), tipo
    if tipo in ("dip", "sop"):
        n = len(posiciones)
        k = n // 2
        largo_cuerpo = k * paso + (1.2 if tipo == "dip" else 0.6)
        ancho_cuerpo = 6.35 if tipo == "dip" else 4.4
        pata = 1.0
        ancho = paso * (0.6 if tipo == "dip" else 0.45)
        ini = (largo_cuerpo - (k - 1) * paso) / 2
        res = {}
        for j in range(n):
            pos = str(j + 1)
            if j < k:
                y = ini + j * paso
                res[pos] = (-pata, y - ancho / 2, pata, ancho, "i")
            else:
                y = largo_cuerpo - ini - (j - k) * paso
                res[pos] = (ancho_cuerpo, y - ancho / 2, pata, ancho, "d")
        return res, (0.0, 0.0, ancho_cuerpo, largo_cuerpo), tipo
    # bga / wlcsp: filas con letra, columnas con número
    filas = sorted({re.match(r"[A-Z]+", p).group(0) for p in posiciones})
    cols = sorted({int(re.search(r"\d+", p).group(0)) for p in posiciones})
    res = {}
    d = paso * 0.6
    for p in posiciones:
        f = filas.index(re.match(r"[A-Z]+", p).group(0))
        c = cols.index(int(re.search(r"\d+", p).group(0)))
        cx, cy = paso * (c + 1), paso * (f + 1)
        res[p] = (cx - d / 2, cy - d / 2, d, d, "c")
    return res, (0.0, 0.0, paso * (len(cols) + 1), paso * (len(filas) + 1)), tipo


def _f(v):
    s = "%.1f" % (v * ESCALA)
    return s[:-2] if s.endswith(".0") else s


def svg(chip, detalle):
    """El SVG del encapsulado, detallado o simple."""
    pos = [p.pos for p in chip.patillas]
    geo, (bx, by, bw, bh), tipo = geometria(chip.encapsulado, pos)
    por_pos = {p.pos: p for p in chip.patillas}
    paso = ENCAPSULADOS[chip.encapsulado][1]
    letra = min(max(paso * 0.6, 0.25), 1.6) if tipo != "bga" else paso * 0.22
    rotulos = []
    margen = 0.6
    if detalle:
        mas_largo = max(len("%s %s" % (p.pos, p.nombre)) for p in chip.patillas)
        if tipo != "bga":
            margen = 0.4 + mas_largo * letra * 0.62
    xs = [g[0] for g in geo.values()] + [g[0] + g[2] for g in geo.values()] + [bx, bx + bw]
    ys = [g[1] for g in geo.values()] + [g[1] + g[3] for g in geo.values()] + [by, by + bh]
    arriba = 0.0
    if detalle and tipo == "bga":
        arriba = 1.2       # la referencia va encima: dentro taparía las bolas
    x0, y0 = min(xs) - margen, min(ys) - margen - arriba
    W, H = max(xs) + margen - x0, max(ys) + margen - y0
    out = ['<svg xmlns="http://www.w3.org/2000/svg" version="1.2" baseProfile="tiny" '
           'viewBox="0 0 %s %s" width="%.2fmm" height="%.2fmm">\n' % (_f(W), _f(H), W, H)]
    out.append("<!-- %s, %s: el %s de mcu-sim-build (doc/analisis_build.md, B3 y B4).\n"
               "     Lo genera biblioteca/herramientas/genera_mcus.py: no se edita a mano.\n"
               "     Unidades de 0.1 mm. Cada patilla es $.N, con N su posicion. -->\n"
               % (chip.tipo, chip.encapsulado, "dibujo detallado" if detalle else "dibujo simple"))
    out.append('<g id="$" transform="translate(%s,%s)" font-family="sans-serif">\n'
               % (_f(-x0), _f(-y0)))
    color_pata = "#b8b8b8" if tipo != "bga" else "#c8c8c8"
    out.append('  <rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="#2b2b2b" stroke="#111" '
               'stroke-width="%s"/>\n' % (_f(bx), _f(by), _f(bw), _f(bh), _f(min(bw, bh) * 0.02),
                                         _f(0.05)))
    # La marca de la patilla 1 (o A1)
    if tipo == "bga":
        mx, my = bx + paso * 0.35, by + paso * 0.35
    else:
        mx = bx + min(bw, bh) * 0.08 + 0.3
        my = by + min(bw, bh) * 0.08 + 0.3 if tipo != "dip" else by + 0.6
    out.append('  <circle cx="%s" cy="%s" r="%s" fill="#555"/>\n' %
               (_f(mx), _f(my), _f(max(min(bw, bh) * 0.03, 0.12))))
    for p in sorted(chip.patillas, key=lambda p: clave_pos(p.pos)):
        x, y, w, h, lado = geo[p.pos]
        attr_id = ' id="$.%s"' % p.pos if detalle else ""
        if tipo == "bga":
            out.append('  <circle%s cx="%s" cy="%s" r="%s" fill="%s"/>\n' %
                       (attr_id, _f(x + w / 2), _f(y + h / 2), _f(w / 2), color_pata))
        elif lado == "c":
            # La almohadilla de debajo de una QFN: vista desde arriba no se
            # ve; su contorno, discontinuo
            out.append('  <rect%s x="%s" y="%s" width="%s" height="%s" fill="none" stroke="%s" '
                       'stroke-width="%s" stroke-dasharray="%s %s"/>\n' %
                       (attr_id, _f(x), _f(y), _f(w), _f(h), color_pata, _f(paso * 0.15),
                        _f(paso * 0.6), _f(paso * 0.4)))
        else:
            out.append('  <rect%s x="%s" y="%s" width="%s" height="%s" fill="%s"/>\n' %
                       (attr_id, _f(x), _f(y), _f(w), _f(h), color_pata))
        if not detalle:
            continue
        if lado == "c" and tipo != "bga":
            rotulos.append('  <text x="%s" y="%s" font-size="%s" text-anchor="middle" fill="#ddd">%s</text>\n'
                           % (_f(x + w / 2), _f(y + h - letra * 0.5), _f(letra * 0.8),
                              escape("%s %s (debajo)" % (p.pos, p.nombre))))
            continue
        texto = escape("%s %s" % (p.pos, p.nombre))
        col = "#1a5fb4" if p.clase == "es" else ("#c01c28" if p.clase == "alimentacion" else "#6a3d9a")
        fs = _f(letra)
        if lado == "i":
            rotulos.append('  <text x="%s" y="%s" font-size="%s" text-anchor="end" fill="%s">%s</text>\n'
                           % (_f(x - 0.2), _f(y + h / 2 + letra * 0.35), fs, col, texto))
        elif lado == "d":
            rotulos.append('  <text x="%s" y="%s" font-size="%s" fill="%s">%s</text>\n'
                           % (_f(x + w + 0.2), _f(y + h / 2 + letra * 0.35), fs, col, texto))
        elif lado == "a":
            cx, cy = x + w / 2 + letra * 0.35, y - 0.2
            rotulos.append('  <text x="%s" y="%s" font-size="%s" fill="%s" '
                           'transform="rotate(-90,%s,%s)">%s</text>\n'
                           % (_f(cx), _f(cy), fs, col, _f(cx), _f(cy), texto))
        elif lado == "b":
            cx, cy = x + w / 2 + letra * 0.35, y + h + 0.2
            rotulos.append('  <text x="%s" y="%s" font-size="%s" text-anchor="end" fill="%s" '
                           'transform="rotate(-90,%s,%s)">%s</text>\n'
                           % (_f(cx), _f(cy), fs, col, _f(cx), _f(cy), texto))
        else:   # bola: la posición encima y el nombre debajo, dentro de su celda
            rotulos.append('  <text x="%s" y="%s" font-size="%s" text-anchor="middle" fill="#fff">%s</text>\n'
                           % (_f(x + w / 2), _f(y + h / 2 + letra * 0.35), fs, escape(p.pos)))
            rotulos.append('  <text x="%s" y="%s" font-size="%s" text-anchor="middle" fill="%s">%s</text>\n'
                           % (_f(x + w / 2), _f(y + h + letra * 1.1),
                              _f(min(letra * 0.8, paso * 0.95 / (0.58 * max(len(p.nombre), 1)))), "#ddd",
                              escape(p.nombre)))
    out.extend(rotulos)
    # La referencia, en el cuerpo
    tam = min(bw / max(len(chip.tipo), 1) * 1.5, bh * 0.18)
    cx, cy = bx + bw / 2, by + bh / 2
    if tipo in ("dip", "sop"):
        out.append('  <text x="%s" y="%s" font-size="%s" text-anchor="middle" fill="#ddd" '
                   'transform="rotate(-90,%s,%s)">%s</text>\n'
                   % (_f(cx), _f(cy + tam * 0.35), _f(min(bh / max(len(chip.tipo), 1) * 1.5, bw * 0.5)),
                      _f(cx), _f(cy), escape(chip.tipo)))
    elif arriba:
        out.append('  <text x="%s" y="%s" font-size="%s" text-anchor="middle" fill="#333">%s</text>\n'
                   % (_f(cx), _f(by - 0.35), _f(min(0.5, bw / (0.62 * len(chip.tipo)))), escape(chip.tipo)))
    else:
        out.append('  <text x="%s" y="%s" font-size="%s" text-anchor="middle" fill="#ddd">%s</text>\n'
                   % (_f(cx), _f(cy + tam * 0.35), _f(tam), escape(chip.tipo)))
    out.append("</g>\n</svg>\n")
    return "".join(out)
