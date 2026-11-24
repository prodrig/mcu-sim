#!/usr/bin/env python3
# =============================================================================
# comprueba_exporta_mcu.py — `mcu-sim --exporta-mcu` de TODOS los MCU, y lo
# que tiene que cumplir cada uno (fase B2 de doc/analisis_build.md)
#
#   python3 verif/comprueba_exporta_mcu.py build/mcu-sim [CARPETA]
#   make exporta-mcu-comprueba
#
# Por qué un guion y no un grupo de test407: `--exporta-mcu` CONSTRUYE el
# chip, y en una suite ya en marcha no se puede elaborar nada más. Aquí se
# lanza mcu-sim una vez por tipo -los que dice su `--help`-, se guarda cada
# XML en CARPETA (build/mcus-modelo por omisión, para mirarlos) y se
# comprueba:
#
#   * que mcu-sim acaba bien y escribe XML bien formado, sin nada más en la
#     salida estándar;
#   * que hay tantos <pad> como dice gpio=, y ninguno repetido;
#   * que el `modo` de cada función cabe en lo que elige el firmware de su
#     familia: AF 0-15 en un STM32, PxSEL 1-3 en un MSP432P4, PCTL 1-15 en un
#     MSP432E4, la función 0 del IO_MUX en un ESP32, y PxMAPy 1-31 en el PMAP;
#     y PxSEL1:PxSEL0 (o PxSEL + 2·PxSEL2) 1-3 en un MSP430; en una matriz,
#     el índice de la señal, 0-127;
#   * que ninguna familia va sin inventario (los MSP430 y la matriz de los
#     ESP32 lo tienen desde B4), y que todas tienen funciones: si una lo
#     pierde, se ve aquí.
#
# Termina con 0 si todo cuadra, y con una tabla de lo que hay.
# =============================================================================
import os
import subprocess
import sys
import xml.dom.minidom as minidom

MODOS = [  # prefijo de familia, rango del modo de una funcion fija
    ("STM32", 0, 15),
    ("MSP432P4", 1, 3),
    ("MSP432E4", 1, 15),
    ("ESP32", 0, 0),
    ("MSP430", 1, 3),
]
SIN_INVENTARIO = ()   # familias que lo dicen y no lo tienen: desde B4, ninguna


def tipos(sim):
    r = subprocess.run([sim, "--help"], capture_output=True, text=True)
    t = r.stdout
    i = t.find("Los MCUs que se saben construir son:")
    if i < 0:
        sys.exit("no encuentro la lista de MCUs en `%s --help`" % sim)
    linea = t[i:].splitlines()[1]
    return [x.strip() for x in linea.split(",") if x.strip()]


def comprueba(tipo, texto, fallos):
    def mal(m):
        fallos.append("%s: %s" % (tipo, m))
    try:
        doc = minidom.parseString(texto)
    except Exception as e:  # noqa: BLE001
        mal("XML mal formado: %s" % e)
        return None
    raiz = doc.documentElement
    if raiz.tagName != "mcu" or raiz.getAttribute("tipo").upper() != tipo.upper():
        mal("la raiz no es <mcu tipo=\"%s\">" % tipo)
        return None
    fam = raiz.getAttribute("familia")
    pads = raiz.getElementsByTagName("pad")
    nombres = [p.getAttribute("nombre") for p in pads]
    if len(nombres) != int(raiz.getAttribute("gpio") or -1):
        mal("%d <pad> y gpio=%s" % (len(nombres), raiz.getAttribute("gpio")))
    if len(set(nombres)) != len(nombres):
        mal("pads repetidos")
    fijas = [f for p in pads for f in p.getElementsByTagName("funcion")]
    sin = raiz.getElementsByTagName("sin_inventario")
    espera_sin = fam.startswith(SIN_INVENTARIO)
    if espera_sin and not sin:
        mal("la familia %s deberia decir que no tiene inventario" % fam)
    if not espera_sin and sin:
        mal("la familia %s ha perdido su inventario: %s" % (fam, sin[0].getAttribute("motivo")))
    if not espera_sin and not fijas:
        mal("ninguna funcion en ningun pad")
    if sin and not sin[0].getAttribute("motivo"):
        mal("<sin_inventario> sin motivo")
    rango = next(((a, b) for pre, a, b in MODOS if fam.startswith(pre)), None)
    for f in fijas:
        if f.getAttribute("analogica") == "si" and not f.hasAttribute("modo"):
            continue
        m = f.getAttribute("modo")
        if rango is None or not m.isdigit() or not rango[0] <= int(m) <= rango[1]:
            mal("modo=%r fuera de %s en %s" % (m, rango, f.parentNode.getAttribute("nombre")))
            break
    mapeables = 0
    for mp in raiz.getElementsByTagName("mapeo"):
        fs = mp.getElementsByTagName("funcion")
        mapeables += len(fs)
        if mp.getAttribute("tipo") == "pmap":
            if not fs:
                mal("un PMAP sin funciones mapeables")
            for f in fs:
                m = f.getAttribute("modo")
                if not m.isdigit() or not 1 <= int(m) <= 31:
                    mal("PxMAPy=%r fuera de 1-31" % m)
                    break
        elif mp.getAttribute("tipo") == "matriz":
            if mp.getAttribute("inventario") == "no":
                mal("la matriz sin inventario")
            for f in fs:
                m = f.getAttribute("modo")
                if not m.isdigit() or not 0 <= int(m) <= 127:
                    mal("indice de la matriz %r fuera de 0-127" % m)
                    break
    alim = len(raiz.getElementsByTagName("alimentacion"))
    if not alim:
        mal("sin ninguna patilla de alimentacion")
    return (fam, raiz.getAttribute("encapsulado"), len(nombres), len(fijas), mapeables,
            alim, "no" if sin else "si")


def main():
    if len(sys.argv) < 2:
        sys.exit("uso: comprueba_exporta_mcu.py MCU-SIM [CARPETA]")
    sim = sys.argv[1]
    dest = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(sim), "mcus-modelo")
    os.makedirs(dest, exist_ok=True)
    fallos, filas = [], []
    for t in tipos(sim):
        r = subprocess.run([sim, "--exporta-mcu", t], capture_output=True, text=True)
        with open(os.path.join(dest, t + ".modelo.xml"), "w", encoding="utf-8") as f:
            f.write(r.stdout)
        if r.returncode != 0:
            err = [x for x in r.stderr.splitlines() if "exporta-mcu" in x]
            fallos.append("%s: acaba con %d: %s" % (t, r.returncode, " ".join(err)))
            continue
        fila = comprueba(t, r.stdout, fallos)
        if fila:
            filas.append((t,) + fila)
    print("%-18s %-10s %-9s %5s %6s %5s %5s %s" % ("tipo", "familia", "encaps.", "pads",
                                                  "func.", "map.", "alim", "inventario"))
    for f in filas:
        print("%-18s %-10s %-9s %5d %6d %5d %5d %s" % f)
    print("%d MCU, XML en %s" % (len(filas), dest))
    if fallos:
        print("\n%d FALLOS:" % len(fallos))
        for x in fallos:
            print("  " + x)
        sys.exit(1)
    print("todo cuadra")


if __name__ == "__main__":
    main()
