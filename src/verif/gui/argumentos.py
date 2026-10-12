#!/usr/bin/env python3
# =============================================================================
# argumentos.py — Que `mcu-sim --argumentos` dice la verdad
#
# Fase 7 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`): la
# ventana construye su diálogo de lanzamiento con lo que vuelca
# `mcu-sim --argumentos`, para no llevar una segunda lista de opciones que
# envejezca. Pero dentro de `mcu-sim` la lista sigue estando dos veces —la tabla
# que se vuelca y el bucle que lee `argv`—, y esto es lo que vigila que no se
# separen:
#
#   A1  el volcado es XML bien formado, con los dos posicionales y cada opción
#       con su forma y su tipo;
#   A2  toda opción que cite `--help` está en el volcado, y toda la del volcado
#       en `--help`: una opción nueva que solo se añada a uno de los dos falla
#       aquí;
#   A3  cada opción del volcado —con su valor por omisión, o con su ejemplo— la
#       acepta `mcu-sim` de verdad: con `--valida`, sobre `discovery_min.xml`,
#       sale con 0 y no se queja. Y una que no existe, no.
#   A4  el firmware chip a chip (plan §42 de `mcu-sim-gui`): `--mcus` lista los
#       MCUs de una placa o de un sistema, y `--firmware ID=FICHERO` y
#       `--sin-firmware[=ID]` cambian el de cada uno, tambien con dos; lo que
#       no vale es un error que dice por que.
#
#   make -f Makefile.mcu-sim gui-argumentos
#   python3 verif/gui/argumentos.py [--sim build/mcu-sim]
#
# Sin SystemC en marcha ni red: unos segundos. Hay que ejecutarlo desde src/.
# =============================================================================
import argparse
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

import ventana
from ventana import check, grupo

PLACA = "placas/discovery_min.xml"
FORMAS = {"bandera", "valor", "valor_opcional", "accion"}
TIPOS = {"numero", "entero", "texto", "eleccion", "fichero"}


def corre(sim, args):
    r = subprocess.run([sim] + args, capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout, r.stderr


def xml_de(salida):
    """El XML de --argumentos. SystemC escribe su cabecera de copyright antes de
    que empiece el programa, así que se busca donde empieza el elemento."""
    i = salida.find("<argumentos")
    j = salida.find("</argumentos>")
    return salida[i:j + len("</argumentos>")] if i >= 0 and j > i else ""


def main():
    a = argparse.ArgumentParser(description="que mcu-sim --argumentos dice la verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2

    grupo("A1 El volcado")
    rc, out, err = corre(sim, ["--argumentos"])
    texto = xml_de(out)
    try:
        raiz = ET.fromstring(texto)
    except ET.ParseError as e:
        raiz = None
        print("    " + str(e))
    if not check(rc == 0 and raiz is not None and raiz.tag == "argumentos" and
                 raiz.get("programa") == "mcu-sim",
                 "mcu-sim --argumentos sale con 0 y vuelca XML bien formado"):
        return ventana.resumen("ARGUMENTOS")
    pos = [p.get("nombre") for p in raiz.iter("posicional")]
    ops = list(raiz.iter("opcion"))
    nombres = [x.get("nombre") for x in ops]
    check(pos == ["placa", "firmware"] and
          [p.get("obligatorio") for p in raiz.iter("posicional")] == ["si", "no"],
          "dos posicionales: placa, obligatoria, y firmware, que no")
    check(len(nombres) == len(set(nombres)) and
          all(x.get("forma") in FORMAS for x in ops) and
          all(x.get("forma") in ("bandera", "accion") or x.get("tipo") in TIPOS for x in ops) and
          all(x.get("ayuda") for x in ops),
          "%d opciones, sin repetir, cada una con su forma, su tipo y su ayuda" % len(ops))
    mcu = [x for x in ops if x.get("nombre") == "--mcu"]
    valores = [v.text for v in mcu[0].iter("valor")] if mcu else []
    check(mcu and mcu[0].get("tipo") == "eleccion" and "STM32F407VG" in valores and
          mcu[0].get("omision") is None and mcu[0].get("ejemplo") in valores,
          "--mcu es una eleccion entre los %d MCUs del catalogo, SIN omision -sin "
          "<mcu> ni --mcu la placa va sin MCU- y con un ejemplo que esta entre ellos"
          % len(valores))
    gdb = [x.get("nombre") for x in ops if x.get("grupo") == "gdb"]
    check(gdb == ["--gdb", "--gdb-dap"], "--gdb y --gdb-dap, en el mismo grupo: se excluyen")
    no_gui = sorted(x.get("nombre") for x in ops
                    if x.get("con_gui") == "no" and x.get("forma") != "accion")
    check(no_gui == ["--gui", "--tiempo-real"],
          "con --gui no tienen sentido --gui (la pone la ventana) ni --tiempo-real "
          "(el ritmo lo dice ella)")

    grupo("A2 El volcado y --help dicen las mismas opciones")
    rc, ayuda, _ = corre(sim, ["--help"])
    en_ayuda = set(re.findall(r"(?<![\w-])(--[a-z][a-z-]*)", ayuda))
    en_ayuda.add("--help")          # se explica en la primera linea, sin escribirse
    faltan = sorted(en_ayuda - set(nombres))
    sobran = sorted(set(nombres) - en_ayuda)
    check(not faltan, "toda opcion que cita --help esta en el volcado%s"
          % ("" if not faltan else ": faltan " + ", ".join(faltan)))
    check(not sobran, "y toda la del volcado, en --help%s"
          % ("" if not sobran else ": sobran " + ", ".join(sobran)))

    grupo("A3 mcu-sim acepta cada una")
    malas = []
    probadas = 0
    for x in ops:
        n, forma = x.get("nombre"), x.get("forma")
        if forma == "accion" or n == "--gui":
            continue                     # --gui con --valida querria una ventana
        if forma == "bandera":
            arg = [n]
        else:
            v = x.get("omision") or x.get("ejemplo")
            if n == "--serie":
                continue                 # necesita una placa con un PuenteSerie
            arg = [n + "=" + v] if v else [n]
        rc, out, err = corre(sim, [PLACA, "--valida"] + arg)
        probadas += 1
        if rc != 0 or "uso:" in err:
            malas.append(" ".join(arg) + " (codigo %s)" % rc)
    check(not malas and probadas >= 10,
          "las %d que se pueden probar con --valida -cada una con su valor por "
          "omision o su ejemplo- salen con 0%s"
          % (probadas, "" if not malas else ": fallan " + ", ".join(malas)))
    rc, out, err = corre(sim, [PLACA, "--valida", "--serie=VCP=rfc2217:4000"])
    check("serie" in (out + err) and "uso:" not in err,
          "--serie se lee como opcion (con una placa sin ese puente, lo dice)")
    rc, out, err = corre(sim, [PLACA, "--valida", "--no-existe"])
    check(rc != 0,
          "y una que no existe no se acepta en silencio (codigo %s)" % rc)

    grupo("A4 El firmware de cada MCU: --mcus, --firmware y --sin-firmware")
    rc, out, err = corre(sim, ["placas/nucleo_f446re_servo.xml", "--mcus"])
    lista = out[out.find("<mcus"):out.find("</mcus>") + len("</mcus>")]
    try:
        m = ET.fromstring(lista).findall("mcu")
    except ET.ParseError:
        m = []
    check(rc == 0 and len(m) == 1 and m[0].get("id") == "N/u0" and
          m[0].get("tipo") == "STM32F446RE" and
          m[0].get("firmware") == "verif/fw/servo_demo/servo_demo.bin",
          "--mcus de un sistema: su chip, con el id del sistema, el tipo y el firmware "
          "del XML")
    rc, out, err = corre(sim, ["placas/dos_mcu.xml", "--mcus"])
    lista = out[out.find("<mcus"):out.find("</mcus>") + len("</mcus>")]
    try:
        m = [x.get("id") for x in ET.fromstring(lista).findall("mcu")]
    except ET.ParseError:
        m = []
    rc2, out2, _ = corre(sim, ["placas/fuente_y_masa.xml", "--mcus"])
    check(rc == 0 and m == ["u0", "u1"] and rc2 == 0 and "<mcus" in out2 and
          "<mcu " not in out2,
          "de una placa con dos, los dos; de una sin ninguno, la lista vacia")
    rc, out, err = corre(sim, ["placas/dos_mcu.xml", "--valida",
                               "--firmware", "u1=b.bin", "--firmware=u0=a.bin"])
    check(rc == 0 and "mcu u0: a.bin" in out and "mcu u1: b.bin" in out,
          "con dos MCUs, cada uno con el suyo: --firmware ID=FICHERO, con o sin igual")
    rc, out, err = corre(sim, ["placas/nucleo_f446re_servo.xml", "--valida", "--sin-firmware"])
    rc2, out2, _ = corre(sim, ["placas/dos_mcu.xml", "--valida", "--sin-firmware",
                               "--firmware=u1=b.bin"])
    check(rc == 0 and "mcu N/u0: sin firmware" in out and rc2 == 0 and
          "mcu u0: sin firmware" in out2 and "mcu u1: b.bin" in out2,
          "--sin-firmware deja sin firmware al que lo traia del XML; sin id, a todos, "
          "y un --firmware con su id manda sobre eso")
    rc, out, err = corre(sim, ["placas/nucleo_f446re_servo.xml", "--valida",
                               "--sin-firmware=N/u0"])
    check(rc == 0 and "mcu N/u0: sin firmware" in out,
          "--sin-firmware=ID, solo ese")
    malos = [
        (["placas/dos_mcu.xml", "--firmware=u7=a.bin"],
         "--firmware u7: no hay ningun MCU con ese id. Los que hay: u0, u1"),
        (["placas/dos_mcu.xml", "--firmware=u0=a.bin", "--sin-firmware=u0"],
         "se dice dos veces"),
        (["placas/dos_mcu.xml", "--firmware=u0"], "hace falta ID=FICHERO"),
        (["placas/discovery_min.xml", "x.bin", "--firmware=u0=a.bin"],
         "dilo de una sola manera"),
        (["placas/fuente_y_masa.xml", "--sin-firmware"], "--sin-firmware no tiene sentido"),
    ]
    bien = []
    for args, frase in malos:
        rc, out, err = corre(sim, args + ["--valida"])
        bien.append(rc != 0 and frase in out + err)
    check(all(bien),
          "y lo que no vale se dice: un id que no esta -con los que hay-, el mismo chip "
          "dos veces, sin fichero, junto al firmware posicional, y en una placa sin MCU "
          "(%s)" % bien)
    return ventana.resumen("ARGUMENTOS")


if __name__ == "__main__":
    sys.exit(main())
