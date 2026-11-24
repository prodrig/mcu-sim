# =============================================================================
# fuente_ti.py — El patillaje de los MSP430 y los MSP432 desde sus datasheets
# (fase B4b de doc/analisis_build.md)
#
# Lee biblioteca/fuentes/ti/TIPO.pines, la transcripción que escribe
# extrae_ti.py de las tablas del datasheet: la posición de cada patilla y,
# por pad, cada función con el número que la elige -PxSEL, PxSEL2,
# PxSEL1/PxSEL0, PCTL- y los valores del PMAP.
#
# EL CONTRASTE ES ESTRICTO, tanto como el propio hardware: cada función que el
# modelo simula tiene que estar en la fuente EN EL MISMO PAD, CON EL MISMO
# NÚMERO Y CON EL MISMO NOMBRE; y cada valor del PMAP que el modelo acepta,
# con el mismo mnemónico. Lo que no cuadra para el generador. Lo analógico
# que el datasheet no elige con PxSEL (las entradas del ADC10 del G2, que van
# por ADC10AE) se contrasta por nombre.
#
# El modelo no siempre nombra como TI, y eso se traduce aquí sin adivinar:
#   * el MSP432P4 da el nombre corto de la línea («simo», «TA») y el
#     periférico en la señal (eusci_b0.simo_out, timer_a2.signal_1);
#   * el MSP432E4, la plantilla de su manual («UnRx», «I2CnSCL») y el número
#     en la señal (uart3.rx);
#   * el LCD_C del FR6989 numera sus pines Lx, como los registros LCDSx: el
#     datasheet llama Sx a los de segmento y COM0-COM3 a L60-L63;
#   * y TI escribe algunas señales de dos formas según el datasheet
#     (EQUIVALENTES): el disparo del Timer_B es TBxTRG en las FR2 y TBxOUTH
#     en las FR5/FR6; la salida del eCOMP, COMPx.O, COMPxOUT o COUT.
# =============================================================================
import os
import re

from mcus_comun import Chip, ErrorContraste, Funcion, Patilla, nodo_de_alimentacion

AQUI = os.path.dirname(os.path.abspath(__file__))
CARPETA = os.path.join(AQUI, "..", "fuentes", "ti")

PAD = re.compile(r"^(P\d+|PJ)\.\d+$|^P[A-T]\d$")
CLASES = [
    (r"^(RST|NRST|RSTN|RST/NMI|NMI|RESET)", "reset"),
    (r"^(TEST|SBWTCK|TCK|TMS|TDI|TDO|SWCLK|SWDIO|JTAG)", "control"),
    (r"^(D?V(CC|DD|SS|CORE|BAT|BUS|USB|DDA|DDC|SSA|REF)|A?V(CC|SS)|DV|AV|VDD|GND|LDO|VUSB|V18|"
     r"VDDC|VDDA|VREF)", "alimentacion"),
]

# Lo que el modelo simula sin registrarlo en un pad -lo analógico que un
# periférico lee directamente del nodo- se da por simulado POR PERIFÉRICO
SIMULADO_POR_PERIFERICO = {
    "MSP430G2": [r"^X(IN|OUT)$"],
    "MSP430F5": [r"^X(IN|OUT)$", r"^XT2(IN|OUT)$"],
    "MSP430FR2": [r"^X(IN|OUT)$", r"^LFX(IN|OUT)$", r"^HFX(IN|OUT)$"],
    "MSP432P4": [],
    "MSP432E4": [r"^OSC\d$"],
}

# Una señal que TI escribe de dos formas según el datasheet
EQUIVALENTES = [
    (r"^TB(\d)TRG$", [r"TB\1OUTH"]),           # FR2 / FR5-FR6
    (r"^COMP(\d)\.O$", [r"COMP\1OUT"]),        # FR2355 / FR2476
    (r"^COMP0\.O$", ["COUT"]),                 # FR2311: su único eCOMP
]


def lee(tipo):
    f = os.path.join(CARPETA, tipo + ".pines")
    if not os.path.exists(f):
        return None
    d = {"patillas": [], "funciones": [], "pmap": {}}
    with open(f, encoding="utf-8") as fh:
        for linea in fh:
            linea = linea.strip()
            if not linea or linea.startswith("#"):
                continue
            p = linea.split()
            if p[0] == "documento":
                d["documento"] = p[1]
            elif p[0] == "encapsulado":
                d["encapsulado"] = p[1]
            elif p[0] == "patilla":
                d["patillas"].append((p[1], p[2]))
            elif p[0] == "funcion":
                d["funciones"].append((p[1], None if p[2] == "-" else int(p[2]), p[3],
                                       len(p) > 4 and p[4] == "analogica"))
            elif p[0] == "pmap":
                d["pmap"][p[1]] = int(p[2])
    return d


def clase(nombre):
    for pat, c in CLASES:
        if re.match(pat, nombre):
            return c
    return "control"


def chip(tipo, modelo):
    if not tipo.startswith("MSP4"):
        return None
    d = lee(tipo)
    if d is None:
        raise FileNotFoundError("%s: no hay biblioteca/fuentes/ti/%s.pines (extrae_ti.py)" % (tipo, tipo))
    c = Chip(tipo, "TI %s" % d["documento"])
    c.encapsulado = d["encapsulado"]
    por_pad = {}
    for pos, nombre in d["patillas"]:
        partes = nombre.split("/")
        pad = next((s for s in partes if PAD.match(s)), None)
        pt = Patilla(pos, nombre, "es" if pad else clase(partes[0]), pad)
        if pad:
            por_pad[pad] = pt
        c.patillas.append(pt)
    for pad, modo, nombre, an in d["funciones"]:
        if pad in por_pad:
            por_pad[pad].funciones.append(Funcion(nombre, modo, an))
    c.pmap = d["pmap"]
    return c


# --- Los nombres del modelo, en el idioma de TI ------------------------------
def _equivalentes(n):
    out = [n]
    for pat, alts in EQUIVALENTES:
        if re.match(pat, n):
            out += [re.sub(pat, a, n) for a in alts]
    return out


def nombres_ti(familia, nombre, senal):
    """Los nombres de TI que puede ser la función `nombre` del modelo."""
    if not nombre:
        return []
    m = re.match(r"([a-z_0-9]*[a-z_])(\d+)\.(.*)$", senal or "")
    per, num, lin = (m.group(1), m.group(2), m.group(3)) if m else ("", "", "")
    out = []
    for n in nombre.split("/"):
        out += _equivalentes(n)
        if familia == "MSP432E4" and num and "n" in n:
            out.append(n.replace("n", num, 1))
        if familia == "MSP432P4" and per:
            if per == "eusci_a":
                out.append("UCA%s%s" % (num, n.upper()))
            elif per == "eusci_b":
                out.append("UCB%s%s" % (num, n.upper()))
            elif per == "timer_a" and n == "TA":
                k = re.match(r"signal_(\d+)", lin)
                if k:
                    out += ["TA%s.%s" % (num, k.group(1)), "TA%s.CCI%sA" % (num, k.group(1))]
            elif per == "timer_a" and n == "TACLK":
                out.append("TA%sCLK" % num)
        lx = re.match(r"^L(\d+)$", n)
        if lx and familia == "MSP430FR2":
            k = int(lx.group(1))
            out.append("S%d" % k)                       # LCD_C: Lx es el segmento Sx
            if k >= 60:
                out.append("COM%d" % (k - 60))          # COM0-COM3 en L60-L63
    return out


def nombres_pmap(familia, nombre, senal):
    """Los mnemónicos PM_ que puede ser una mapeable del modelo."""
    return ["PM_" + n.replace("PM_", "") for n in nombres_ti(familia, nombre, senal)]


def iguales(a, b):
    return a.upper().replace(".", "") == b.upper().replace(".", "")


def contrasta(c, modelo):
    """El contraste estricto (ver la cabecera). Se para con ErrorContraste."""
    if c.encapsulado != modelo.encapsulado:
        raise ErrorContraste("%s: la fuente es del %s y el modelo del %s" %
                             (c.tipo, c.encapsulado, modelo.encapsulado))
    c.familia = modelo.familia
    es = {p.pad: p for p in c.patillas if p.clase == "es"}
    if set(es) != set(modelo.pads):
        raise ErrorContraste("%s: los pads no son los mismos. Solo en la fuente: %s. Solo en el "
                             "modelo: %s" % (c.tipo, sorted(set(es) - set(modelo.pads)),
                                             sorted(set(modelo.pads) - set(es))))
    if len(c.patillas) != modelo.patillas:
        raise ErrorContraste("%s: la fuente trae %d patillas y el encapsulado del modelo %d"
                             % (c.tipo, len(c.patillas), modelo.patillas))
    faltan = []
    for pad, p in es.items():
        for (modo, nombre, senal, analogica) in modelo.pads[pad]:
            cands = nombres_ti(modelo.familia, nombre, senal)
            if cands:
                ok = [f for f in p.funciones if any(iguales(f.nombre, n) for n in cands) and
                      (f.modo == modo or (analogica and (modo is None or f.modo is None)))]
            else:
                # Sin nombre: el tipo 3 de las FR2, «analógica o cristal». Lo
                # analógico o el cristal de la fuente CON ESE NÚMERO
                ok = [f for f in p.funciones if f.modo == modo and
                      (f.analogica or re.match(r"^(X(IN|OUT)|XT2(IN|OUT)|LFX(IN|OUT)|HFX(IN|OUT))$", f.nombre))]
            if not ok:
                faltan.append("%s %s %s (%s)" % (pad, "-" if modo is None else modo,
                                                 nombre or "sin nombre", senal or "-"))
                continue
            for f in ok:
                f.simulada = True
    # El PMAP: cada valor que el modelo acepta, con su mnemónico
    if modelo.mapeables:
        if not c.pmap:
            faltan.append("el modelo tiene PMAP y la fuente no trae su tabla")
        hechos = set()
        for (v, nombre, senal) in modelo.mapeables:
            cands = nombres_pmap(modelo.familia, nombre, senal)
            ok = [mn for mn, val in c.pmap.items() if val == v and any(iguales(mn, n) for n in cands)]
            if not ok:
                faltan.append("PMAP %d %s (%s): la fuente da %s" % (
                    v, nombre, senal or "-",
                    ", ".join(mn for mn, val in c.pmap.items() if val == v) or "nada"))
            hechos.update(ok)
        fs = []
        for mn, v in sorted(c.pmap.items(), key=lambda x: (x[1], x[0])):
            if mn in ("PM_NONE",):
                continue
            f = Funcion(mn, v, mn == "PM_ANALOG")
            f.simulada = mn in hechos
            fs.append(f)
        c.mapeo = {"tipo": "pmap", "puertos": modelo_puertos(modelo), "funciones": fs}
    if faltan:
        raise ErrorContraste("%s: el modelo simula %d funciones que el datasheet no tiene así "
                             "(pad, número, nombre): %s" % (c.tipo, len(faltan), "; ".join(faltan)))
    # Lo analógico que el modelo lee sin registrarlo: por periférico
    pats = SIMULADO_POR_PERIFERICO.get(modelo.familia, [])
    for p in es.values():
        for f in p.funciones:
            if not f.simulada and f.analogica and any(re.match(r, f.nombre) for r in pats):
                f.simulada = True
                f.por = "periferico"
    for p in c.patillas:
        if p.clase != "es":
            p.nodo = nodo_de_alimentacion(p.nombre.split("/")[0], modelo)
            if not p.nodo:
                c.avisos.append("la patilla %s (%s) no tiene nodo en el modelo" % (p.pos, p.nombre))


def modelo_puertos(modelo):
    return getattr(modelo, "puertos_mapeo", "")


def simulado_por_periferico(familia):
    return SIMULADO_POR_PERIFERICO.get(familia, [])
