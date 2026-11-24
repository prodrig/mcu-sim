# =============================================================================
# fuente_esp.py — El patillaje de los ESP32-C3 y ESP32-C6 desde sus datasheets
# y ESP-IDF (fase B4b de doc/analisis_build.md)
#
# Dos fuentes, las dos en biblioteca/fuentes/esp/:
#   * TIPO.pines, la transcripción de la tabla «Pin Overview» del datasheet
#     (extrae_esp.py): cada patilla y, por GPIO, sus funciones del IO MUX con
#     su número (F0, F2), las analógicas y las del LP IO MUX;
#   * <chip>/gpio_sig_map.h de ESP-IDF v5.3.1 (Apache-2.0): el índice de
#     cada señal de la MATRIZ GPIO, de entrada (_IN_IDX) o de salida
#     (_OUT_IDX). Es la versión legible por máquina de la tabla del TRM.
#
# El contraste es tan estricto como el de TI: cada función del IO MUX que el
# modelo simula tiene que estar en el mismo GPIO, con el mismo número y el
# mismo nombre; y cada señal de la matriz que el modelo conecta, con el mismo
# índice, el mismo nombre y el mismo sentido.
# =============================================================================
import os
import re

from mcus_comun import Chip, ErrorContraste, Funcion, Patilla

AQUI = os.path.dirname(os.path.abspath(__file__))
CARPETA = os.path.join(AQUI, "..", "fuentes", "esp")
IDF = "ESP-IDF v5.3.1 (c8fc5f643b7a7b0d3b182d3df610844e3dc9bd74)"

PAD = re.compile(r"^GPIO\d+$")
CLASES = [
    (r"^(CHIP_EN|CHIP_PU)$", "reset"),
    (r"^(VDD|GND)", "alimentacion"),
]
# Lo que el modelo simula sin registrarlo en un GPIO: los cristales de 32 kHz
SIMULADO_POR_PERIFERICO = {"ESP32C3": [], "ESP32C6": []}


def _chip_idf(tipo):
    return "esp32c6" if tipo.startswith("ESP32-C6") else "esp32c3"


def matriz(tipo):
    """{nombre: índice} de gpio_sig_map.h (sin los alias, que son #define de
    otro nombre)."""
    f = os.path.join(CARPETA, _chip_idf(tipo), "gpio_sig_map.h")
    out = {}
    with open(f, encoding="utf-8") as fh:
        for linea in fh:
            m = re.match(r"#define\s+(\w+)_IDX\s+(\d+)\s*(//.*)?$", linea)
            if m:
                out[m.group(1)] = int(m.group(2))
    return out


def sentido_de(nombre):
    """El sentido que dice el nombre: _IN es entrada, _OUT salida. Otros
    (TWAI_TX, LEDC_LS_SIG_OUT0) no lo dicen con el sufijo."""
    if nombre.endswith("_IN"):
        return "entrada"
    if nombre.endswith("_OUT"):
        return "salida"
    return ""


def chip(tipo, modelo):
    if not tipo.startswith("ESP32"):
        return None
    ruta = os.path.join(CARPETA, tipo + ".pines")
    if not os.path.exists(ruta):
        raise FileNotFoundError("%s: no hay biblioteca/fuentes/esp/%s.pines (extrae_esp.py)" % (tipo, tipo))
    c = Chip(tipo, "")
    por_pad = {}
    doc = ""
    with open(ruta, encoding="utf-8") as fh:
        for linea in fh:
            linea = linea.strip()
            if not linea or linea.startswith("#"):
                continue
            p = linea.split()
            if p[0] == "documento":
                doc = p[1].replace("_", " ")
            elif p[0] == "encapsulado":
                c.encapsulado = p[1]
            elif p[0] == "patilla":
                partes = p[2].split("/")
                pad = next((s for s in partes if PAD.match(s)), None)
                cl = "es" if pad else next((k for pat, k in CLASES if re.match(pat, partes[0])), "control")
                pt = Patilla(p[1], p[2], cl, pad)
                if pad:
                    por_pad[pad] = pt
                c.patillas.append(pt)
            elif p[0] == "funcion" and p[1] in por_pad:
                por_pad[p[1]].funciones.append(
                    Funcion(p[3], None if p[2] == "-" else int(p[2]), len(p) > 4 and p[4] == "analogica"))
    c.fuente = "Espressif %s; %s gpio_sig_map.h" % (doc, IDF)
    c.matriz = matriz(tipo)
    return c


def nodo(nombre, modelo):
    """El nodo del modelo de una patilla de alimentación: VDDA1 es VDDA;
    CHIP_EN, el nombre del datasheet v2.4 del C3, es el CHIP_PU del modelo
    (y de los datasheets anteriores)."""
    for n in (nombre, re.sub(r"\d+$", "", nombre), {"CHIP_EN": "CHIP_PU"}.get(nombre, "")):
        if n and n in modelo.alimentacion:
            return n
    return ""


def contrasta(c, modelo):
    if c.encapsulado != modelo.encapsulado:
        raise ErrorContraste("%s: la fuente es del %s y el modelo del %s" %
                             (c.tipo, c.encapsulado, modelo.encapsulado))
    c.familia = modelo.familia
    es = {p.pad: p for p in c.patillas if p.clase == "es"}
    if set(es) != set(modelo.pads):
        raise ErrorContraste("%s: los GPIO no son los mismos. Solo en la fuente: %s. Solo en el "
                             "modelo: %s" % (c.tipo, sorted(set(es) - set(modelo.pads)),
                                             sorted(set(modelo.pads) - set(es))))
    if len(c.patillas) != modelo.patillas:
        raise ErrorContraste("%s: la fuente trae %d patillas y el encapsulado del modelo %d"
                             % (c.tipo, len(c.patillas), modelo.patillas))
    faltan = []
    for pad, p in es.items():
        for (modo, nombre, senal, analogica) in modelo.pads[pad]:
            # El IO MUX, por número y nombre; lo analógico (el SAR ADC lee el
            # nodo, sin MCU_SEL), por nombre
            ok = [f for f in p.funciones if f.nombre == nombre and
                  (f.modo == modo or (analogica and modo is None and f.analogica))]
            if not ok:
                faltan.append("%s F%s %s" % (pad, modo, nombre))
            for f in ok:
                f.simulada = True
    # La matriz: cada señal que el modelo conecta, con su índice; y su
    # sentido, si el nombre lo dice
    hechas = {}
    for (v, nombre, senal) in modelo.mapeables:
        if c.matriz.get(nombre) != v:
            faltan.append("matriz %d %s (%s): ESP-IDF da %s" % (
                v, nombre, senal, c.matriz.get(nombre, "nada con ese nombre")))
        elif sentido_de(nombre) and sentido_de(nombre) != senal:
            faltan.append("matriz %d %s: el modelo la tiene de %s" % (v, nombre, senal))
        hechas[nombre] = senal
    fs = []
    for n, v in sorted(c.matriz.items(), key=lambda x: (x[1], x[0])):
        f = Funcion(n, v)
        f.simulada = n in hechas
        f.sentido = sentido_de(n) or hechas.get(n, "")
        fs.append(f)
    c.mapeo = {"tipo": "matriz", "puertos": modelo.puertos_mapeo, "funciones": fs}
    if faltan:
        raise ErrorContraste("%s: el modelo simula %d funciones que el datasheet o ESP-IDF no "
                             "tienen así: %s" % (c.tipo, len(faltan), "; ".join(faltan)))
    for p in c.patillas:
        if p.clase != "es":
            p.nodo = nodo(p.nombre.split("/")[0], modelo) if p.nombre != "NC" else ""
            if not p.nodo and p.nombre != "NC":
                c.avisos.append("la patilla %s (%s) no tiene nodo en el modelo" % (p.pos, p.nombre))


def simulado_por_periferico(familia):
    return SIMULADO_POR_PERIFERICO.get(familia, [])
