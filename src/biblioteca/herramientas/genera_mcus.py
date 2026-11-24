#!/usr/bin/env python3
# =============================================================================
# genera_mcus.py — biblioteca/mcus/: el patillaje de cada MCU, contrastado con
# el modelo, y sus dos dibujos (fases B3 y B4 de doc/analisis_build.md)
#
#   python3 genera_mcus.py MODELO SALIDA [TIPO...]
#   make biblioteca-mcus            (lo regenera en biblioteca/mcus)
#
# MODELO es la carpeta de `make exporta-mcu-comprueba`, con un
# TIPO.modelo.xml por MCU: lo que simula mcu-sim (B2). Por cada tipo se busca
# su fuente -fuente_st, fuente_ti, fuente_esp-, se contrasta con el modelo
# (mcus_comun.contrasta) y se escriben en SALIDA
#   TIPO.mcu.xml  TIPO.detalle.svg  TIPO.simple.svg
#
# Un contraste que falla hace que el generador acabe mal, con todos los
# motivos, y ese MCU no se escribe: un patillaje que no cuadra con el modelo
# no se publica. Los avisos -una patilla de
# alimentación sin nodo en el modelo- se cuentan y se listan al final.
# =============================================================================
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mcus_comun as comun  # noqa: E402

FUENTES = []
for nombre in ("fuente_st", "fuente_ti", "fuente_esp"):
    try:
        FUENTES.append(__import__(nombre))
    except ImportError:
        pass


def main():
    if len(sys.argv) < 3:
        sys.exit("uso: genera_mcus.py MODELO SALIDA [TIPO...]")
    modelo_dir, salida = sys.argv[1], sys.argv[2]
    pedidos = sys.argv[3:]
    os.makedirs(salida, exist_ok=True)
    modelos = sorted(glob.glob(os.path.join(modelo_dir, "*.modelo.xml")))
    if not modelos:
        sys.exit("no hay ningun TIPO.modelo.xml en %s: make exporta-mcu-comprueba" % modelo_dir)
    hechos, sin_fuente, avisos, errores = [], [], [], []
    for ruta in modelos:
        m = comun.Modelo(ruta)
        if pedidos and m.tipo not in pedidos:
            continue
        chip, fuente = None, None
        for f in FUENTES:
            chip = f.chip(m.tipo, m)
            if chip:
                fuente = f
                break
        if not chip:
            sin_fuente.append(m.tipo)
            continue
        try:
            comun.contrasta(chip, m, fuente.simulado_por_periferico(m.familia),
                            nombres_iguales=getattr(fuente, "NOMBRES_IGUALES", {}).get(m.familia, False))
        except comun.ErrorContraste as e:
            errores.append(str(e))
            continue
        if hasattr(fuente, "completa"):
            fuente.completa(chip, m)
        for a in chip.avisos:
            avisos.append("%s: %s" % (m.tipo, a))
        for nombre, texto in ((m.tipo + ".mcu.xml", comun.mcu_xml(chip)),
                              (m.tipo + ".detalle.svg", comun.svg(chip, True)),
                              (m.tipo + ".simple.svg", comun.svg(chip, False))):
            with open(os.path.join(salida, nombre), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(texto)
        n_f = sum(len(p.funciones) for p in chip.patillas)
        n_s = sum(1 for p in chip.patillas for f in p.funciones if f.simulada)
        hechos.append((m.tipo, chip.encapsulado, len(chip.patillas), n_f, n_s))
    for t, e, n, nf, ns in hechos:
        print("%-18s %-9s %4d patillas %5d funciones, %4d simuladas" % (t, e, n, nf, ns))
    print("%d MCU en %s" % (len(hechos), salida))
    if sin_fuente:
        print("sin fuente todavia: %s" % ", ".join(sin_fuente))
    if avisos:
        print("%d avisos:" % len(avisos))
        for a in avisos:
            print("  " + a)
    if errores:
        # Se escriben los que cuadran, pero ninguno de los que no: un
        # patillaje que no cuadra con el modelo no se publica
        print("%d CONTRASTES QUE FALLAN:" % len(errores))
        for e in errores:
            print("  " + e)
        sys.exit(1)


if __name__ == "__main__":
    main()
