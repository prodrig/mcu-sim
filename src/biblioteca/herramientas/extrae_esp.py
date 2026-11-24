#!/usr/bin/env python3
# =============================================================================
# extrae_esp.py — Transcribe el patillaje de los ESP32-C3 y ESP32-C6 de sus
# datasheets a biblioteca/fuentes/esp/TIPO.pines (fase B4b de
# doc/analisis_build.md)
#
#   python3 extrae_esp.py REFS [--cache DIR] [TIPO...]
#
# REFS es doc/refs de mcu-sim. Cada datasheet de Espressif trae en su apéndice
# una tabla «Pin Overview» con todo: la posición de cada patilla, su nombre,
# su tipo, sus funciones analógicas, las del LP IO MUX (C6) y las tres del IO
# MUX con su número (F0, F1 -siempre el GPIO- y F2). El MODO de una función
# es ese número de función del IO MUX, el mismo que usa el modelo
# (MCU_SEL de IO_MUX_GPIOn_REG). Lo analógico y lo del LP IO MUX no se elige
# con MCU_SEL: van sin número.
#
# Las señales de la MATRIZ GPIO no están en el datasheet: están en el TRM y,
# legibles por máquina, en ESP-IDF (components/soc/<chip>/include/soc/
# gpio_sig_map.h), que fuente_esp.py lee directamente de
# biblioteca/fuentes/esp/<chip>/.
#
# Mismo formato que las de TI (extrae_ti.py): texto, una línea por dato.
# =============================================================================
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extrae_ti import limpia, paginas  # noqa: E402

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(AQUI, "..", "fuentes", "esp")

# tipo: (pdf bajo REFS, documento, encapsulado, patillas de la tabla)
CHIPS = {
    "ESP32-C3": ("esp32-c3/esp32-c3_datasheet_en.pdf", "ESP32-C3 Series Datasheet v2.4", "QFN32", 33),
    "ESP32-C3FH4": ("esp32-c3/esp32-c3_datasheet_en.pdf", "ESP32-C3 Series Datasheet v2.4", "QFN32", 33),
    "ESP32-C3FN4": ("esp32-c3/esp32-c3_datasheet_en.pdf", "ESP32-C3 Series Datasheet v2.4", "QFN32", 33),
    "ESP32-C3FH4X": ("esp32-c3/esp32-c3_datasheet_en.pdf", "ESP32-C3 Series Datasheet v2.4", "QFN32", 33),
    "ESP32-C3FH8X": ("esp32-c3/esp32-c3_datasheet_en.pdf", "ESP32-C3 Series Datasheet v2.4", "QFN32", 33),
    "ESP32-C6": ("esp32-c6/esp32-c6_datasheet_en.pdf", "ESP32-C6 Series Datasheet v1.1", "QFN40", 41),
    "ESP32-C6FH4": ("esp32-c6/esp32-c6_datasheet_en.pdf", "ESP32-C6 Series Datasheet v1.1", "QFN32", 33),
    "ESP32-C6FH8": ("esp32-c6/esp32-c6_datasheet_en.pdf", "ESP32-C6 Series Datasheet v1.1", "QFN32", 33),
}
# Las patillas que una variante no conecta: en la C3FH4X, la 19 a la 24 (las
# del bus de la Flash, que va dentro) [DS-C3 v2.4, nota 9 de la tabla 2-1]
NO_CONECTADAS = {"ESP32-C3FH4X": {"19", "20", "21", "22", "23", "24"}}


def tabla_resumen(pags, n_patillas):
    """La tabla del apéndice con `n_patillas` filas: [fila], cabeceras."""
    for pg in pags:
        for t in pg["tablas"]:
            filas = [[limpia(c) for c in f] for f in t]
            if len(filas) < 3 or not filas[0] or "IOMUXFunction" not in filas[0] or \
                    "AnalogFunction" not in filas[0]:
                continue
            datos = [f for f in filas[2:] if f[0] and f[0].isdigit()]
            if len(datos) == n_patillas:
                return filas[0], filas[1], datos
    return None, None, None


def columnas(c0, c1, grupo):
    """Los índices de las columnas 0, 1, 2... de un grupo de la cabecera."""
    i = c0.index(grupo)
    out = []
    for j in range(i, len(c1)):
        if j > i and c0[j] and c0[j] != grupo:
            break
        if c1[j] and c1[j].isdigit():
            out.append(j)
    return out


def transcribe(tipo, refs, cache):
    pdf, doc, enc, n = CHIPS[tipo]
    pags = paginas(os.path.join(refs, pdf), cache)
    c0, c1, datos = tabla_resumen(pags, n)
    if not datos:
        raise SystemExit("%s: no encuentro la tabla «Pin Overview» de %d patillas" % (tipo, n))
    an = columnas(c0, c1, "AnalogFunction")
    lp = columnas(c0, c1, "LPIOMUXFunction") if "LPIOMUXFunction" in c0 else []
    io = columnas(c0, c1, "IOMUXFunction")
    lineas = ["# %s, %s: el patillaje del datasheet de Espressif (%s)," % (tipo, enc, doc),
              "# tabla «Pin Overview» del apéndice. Lo escribe",
              "# biblioteca/herramientas/extrae_esp.py; no se edita a mano.",
              "# patilla POSICION NOMBRE",
              "# funcion PAD MODO NOMBRE [analogica]   (MODO: la función del IO MUX, F0-F2;",
              "#                                       - lo analógico y el LP IO MUX)",
              "documento %s" % doc.replace(" ", "_"),
              "encapsulado %s" % enc]
    nc = NO_CONECTADAS.get(tipo, set())
    funcs = []
    for f in datos:
        pos, nombre, tipo_p = f[0], f[1], f[2]
        nombre = re.sub(r"^\d+\|", "", nombre or "")
        if pos in nc:
            lineas.append("patilla %s NC" % pos)
            continue
        pad = f[io[1]] if len(io) > 1 and f[io[1]] and re.match(r"^GPIO\d+$", f[io[1]]) else None
        nom = nombre if not pad or nombre == pad else "%s/%s" % (nombre, pad)
        lineas.append("patilla %s %s" % (pos, nom))
        if not pad:
            continue
        for k, j in enumerate(io):
            v = f[j]
            if v and k != 1 and not re.match(r"^GPIO\d+$", v):
                funcs.append("funcion %s %d %s" % (pad, k, v))
        for j in an:
            if f[j]:
                funcs.append("funcion %s - %s analogica" % (pad, f[j]))
        for j in lp:
            if f[j] and not re.match(r"^LP_GPIO\d+$", f[j]):
                funcs.append("funcion %s - %s" % (pad, f[j]))
    return "\n".join(lineas + funcs) + "\n"


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit("uso: extrae_esp.py REFS [--cache DIR] [TIPO...]")
    refs = args.pop(0)
    cache = None
    if args[:1] == ["--cache"]:
        cache = args[1]
        args = args[2:]
    os.makedirs(SALIDA, exist_ok=True)
    for tipo in args or sorted(CHIPS):
        texto = transcribe(tipo, refs, cache)
        with open(os.path.join(SALIDA, tipo + ".pines"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(texto)
        print("%-14s %3d patillas %3d funciones" % (tipo, texto.count("\npatilla "),
                                                    texto.count("\nfuncion ")))


if __name__ == "__main__":
    main()
