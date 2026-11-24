# =============================================================================
# fuente_st.py — El patillaje de los STM32 desde STM32 Open Pin Data (B3)
#
# Open Pin Data (BSD-3-Clause, la licencia va con los ficheros) es la base de
# datos de patillas de STM32CubeMX. De cada referencia y encapsulado trae
# `mcu/STM32F446R(C-E)Tx.xml` -cada patilla con su posición, su tipo y sus
# señales- y, por familia, `mcu/IP/GPIO-..._Modes.xml` -el número de AF de
# cada señal en cada pin-. Los ficheros que usa mcu-sim están copiados en el
# repositorio, del commit que dice FUENTE: los de los F4 y el F446 en
# biblioteca/fuentes/st/, y los del F3 donde ya estaban, en
# soc/stm32f3/pindata/ (de ahí sale también la tabla AF del modelo del F3).
#
# Lo que no se elige con un número de AF -las entradas del ADC, la salida
# del DAC, los osciladores; en el F3 también los comparadores y los
# amplificadores- se da por simulado POR PERIFÉRICO, con las listas de abajo:
# son los periféricos analógicos que el modelo tiene. El resto de lo que no
# va por AF (el despertar, las del RTC, los disparos externos del ADC) se
# marca como no simulado.
# =============================================================================
import glob
import os
import re
import xml.etree.ElementTree as ET

from mcus_comun import Chip, Funcion, Patilla

# El F3 se contrasta también por nombre: su tabla AF del modelo sale de aquí
NOMBRES_IGUALES = {"STM32F3": True}

FUENTE = "STM32_open_pin_data@45c04599e2e0a788c5ed9b6218dc5a28a36041f7"
AQUI = os.path.dirname(os.path.abspath(__file__))
CARPETAS = [os.path.join(AQUI, "..", "fuentes", "st"),
            os.path.join(AQUI, "..", "..", "soc", "stm32f3", "pindata")]

ANALOGICAS = r"^(ADC\d*_IN\d+|DAC\d?_OUT\d|RCC_OSC|COMP\d+_IN|OPAMP\d+_V(IN|OUT))"
SIMULADO_POR_PERIFERICO = {
    "STM32F4": [r"^ADC\d_IN\d+$", r"^DAC_OUT\d$", r"^RCC_OSC"],
    "STM32F446": [r"^ADC\d_IN\d+$", r"^DAC_OUT\d$", r"^RCC_OSC"],
    "STM32F3": [r"^ADC\d_IN\d+$", r"^DAC\d?_OUT\d$", r"^RCC_OSC", r"^COMP\d+_IN[MP]",
                r"^OPAMP\d+_VIN[MP]", r"^OPAMP\d+_VOUT"],
}
CLASES = {"I/O": "es", "Power": "alimentacion", "Reset": "reset", "Boot": "arranque",
          "MonoIO": "control"}


def _ns(raiz):
    return raiz.tag.split("}")[0] + "}" if raiz.tag.startswith("{") else ""


def _patron(refname):
    # STM32F407V(E-G)Tx -> STM32F407V[EG]: el tipo de mcu-sim no lleva el sufijo
    base = re.sub(r"[A-Z]x$", "", refname)
    return "^" + re.sub(r"\(([^)]*)\)", lambda m: "[" + m.group(1).replace("-", "") + "]", base) + "$"


def _fichero(tipo, encapsulado):
    for d in CARPETAS:
        for f in sorted(glob.glob(os.path.join(d, "STM32*.xml"))):
            r = ET.parse(f).getroot()
            if re.match(_patron(r.get("RefName")), tipo) and r.get("Package") == encapsulado:
                return f, r
    return None, None


def _modos(fichero_mcu, raiz):
    ns = _ns(raiz)
    ver = [ip.get("Version") for ip in raiz.iter(ns + "IP") if ip.get("Name") == "GPIO"][0]
    f = os.path.join(os.path.dirname(fichero_mcu), "GPIO-%s_Modes.xml" % ver)
    r = ET.parse(f).getroot()
    ns = _ns(r)
    af = {}       # (pad, señal) -> número de AF
    for gp in r.iter(ns + "GPIO_Pin"):
        pad = gp.get("Name").split("-")[0].split(" ")[0]
        for s in gp.iter(ns + "PinSignal"):
            for v in s.iter(ns + "PossibleValue"):
                m = re.match(r"GPIO_AF(\d+)_", v.text or "")
                if m:
                    af[(pad, s.get("Name"))] = int(m.group(1))
    return af


def chip(tipo, modelo):
    """El Chip de `tipo`, o None si no es de ST."""
    if not tipo.startswith("STM32"):
        return None
    f, raiz = _fichero(tipo, modelo.encapsulado)
    if not f:
        raise FileNotFoundError("%s: no hay fichero de Open Pin Data para el %s" %
                                (tipo, modelo.encapsulado))
    af = _modos(f, raiz)
    ns = _ns(raiz)
    c = Chip(tipo, FUENTE + ":" + os.path.basename(f))
    c.encapsulado = raiz.get("Package")
    for p in raiz.iter(ns + "Pin"):
        nombre = p.get("Name")
        clase = CLASES.get(p.get("Type"), "control")
        pad = None
        if clase == "es":
            pad = re.split(r"[- ]", nombre)[0]
        pt = Patilla(p.get("Position"), nombre, clase, pad)
        for s in p.iter(ns + "Signal"):
            n = s.get("Name")
            if n == "GPIO":
                continue
            pt.funciones.append(Funcion(n, af.get((pad, n)), bool(re.match(ANALOGICAS, n))))
        c.patillas.append(pt)
    # Lo que la tabla de la FAMILIA da en un pad y el fichero de ESTE
    # encapsulado no lista (el bus de memoria externa en un LQFP64, que no lo
    # puede usar): si el modelo lo registra, es un aviso, no un error
    en_paquete = {(p.pad, f.nombre) for p in c.patillas if p.pad for f in p.funciones}
    c.solo_familia = {(pad, modo): n for (pad, n), modo in af.items()
                      if (pad, n) not in en_paquete}
    return c


def simulado_por_periferico(familia):
    return SIMULADO_POR_PERIFERICO.get(familia, [])
