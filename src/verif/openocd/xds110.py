#!/usr/bin/env python3
# =============================================================================
# verif/openocd/xds110.py — La sonda XDS110 como pieza de la placa (X1 de
# doc/analisis_xds110.md)
#
#   X1  Las dos LaunchPad llevan su sonda: el XDS110-ET de la P401R y el
#       XDS110 de la E401Y, del lado de la sonda de J101.
#   X2  El 3V3 es del LDO de la sonda: con el puente, el chip arranca y el LDO
#       entrega su consumo; SIN EL PUENTE 3V3, el chip no arranca.
#   X3  La traza SWO por la sonda, en memoria: lo que el firmware escribe en el
#       puerto 0 del ITM llega entero, en los dos chips (p4_swo y e4_swo); en
#       crudo, con las cabeceras del ITM; y con otros bit/s que los del TPIU,
#       basura, como en la placa.
#   X4  La traza por TCP, con --espera-terminal: un cliente la lee.
#   X5  Lo que no vale: --swo en una placa sin Xds110, un swo_formato que no
#       existe.
#   X6  Con OpenOCD (si lo hay): la sonda va por J101. Sin el puente de
#       TMS_SWDIO, OpenOCD no encuentra el DAP; con el, si.
#
#   make -f Makefile.mcu-sim xds110        # desde src/
#   python3 verif/openocd/xds110.py [--sim build/mcu-sim] [--openocd RUTA]
# =============================================================================
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import programa as pr                                    # noqa: E402
from programa import check, grupo, puerto_libre, corre   # noqa: E402
import cmsis_dap as cd                                   # noqa: E402

P401R = "placas/msp_exp432p401r.xml"
E401Y = "placas/msp_exp432e401y.xml"
P4_DEMO = "verif/fw/msp432p4/p4_demo/p4_demo.out"
E4_DEMO = "verif/fw/msp432e4/e4_demo/e4_demo.out"
P4_SWO = "verif/fw/msp432p4/p4_swo/p4_swo.out"
E4_SWO = "verif/fw/msp432e4/e4_swo/e4_swo.out"
TODOS = 'puente="1 2, 3 4, 5 6, 7 8, 9 10, 11 12, 13 14, 15 16, 17 18, 19 20"'


def variante(placa, cambios):
    """Una copia de la placa, en su misma carpeta (por el dibujo), con cambios"""
    with open(placa) as f:
        t = f.read()
    for a, b in cambios:
        assert a in t, a
        t = t.replace(a, b, 1)
    fd, ruta = tempfile.mkstemp(suffix=".xml", prefix="_xds_", dir=os.path.dirname(placa))
    with os.fdopen(fd, "w") as f:
        f.write(t)
    return ruta


def sin_puente(placa, par):
    return variante(placa, [(TODOS, TODOS.replace(par + ", ", ""))])


def ldo_ma(sal):
    m = re.search(r"XDS110 XDS: 3V3 (-?[0-9.]+) mA", sal)
    return float(m.group(1)) if m else None


def lineas_swo(sal):
    return re.findall(r"^\s+\| (.*)$", sal, re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default="build/mcu-sim")
    ap.add_argument("--openocd", default=os.environ.get("OPENOCD", "openocd"))
    a = ap.parse_args()
    sim = a.sim

    grupo("X1 Las dos LaunchPad llevan su sonda, del lado de la sonda de J101")
    rc, sal, _ = corre([sim, P401R, "--valida"], 60)
    check(rc == 0 and "sonda XDS: XDS110-ET, 3V3 de su LDO (500 mA)" in sal and "0 avisos" in sal,
          "la MSP-EXP432P401R lleva el XDS110-ET, con su LDO de 500 mA")
    rc, sal, _ = corre([sim, E401Y, "--valida"], 60)
    check(rc == 0 and "sonda XDS: XDS110, 3V3 de su LDO (500 mA)" in sal and "0 avisos" in sal,
          "la MSP-EXP432E401Y lleva el XDS110, sin EnergyTrace")

    grupo("X2 El 3V3 es del LDO de la sonda, por el puente de J101")
    rc, sal, _ = corre([sim, P401R, P4_DEMO, "--ms=30"], 120)
    i = ldo_ma(sal)
    check(rc == 0 and i is not None and 0.2 < i < 1.0,
          "con el puente, el P401R arranca a 3 MHz y el LDO entrega su consumo: %s mA" % i)
    sin = sin_puente(P401R, "5 6")
    try:
        rc, sal, _ = corre([sim, sin, P4_DEMO, "--ms=30"], 120)
    finally:
        os.remove(sin)
    check(rc == 0 and ldo_ma(sal) == 0.0 and "LED LED1 en P1.0: apagado" in sal and
          "LED RGB LED2: apagado" in sal,
          "SIN EL PUENTE 3V3 el chip no arranca: los LED apagados y el LDO, a cero")
    rc, sal, _ = corre([sim, E401Y, E4_DEMO, "--ms=20"], 120)
    check(rc == 0 and "LED D2 en PN0: encendido" in sal,
          "con el puente, el E401Y arranca y enciende D2")
    sin = sin_puente(E401Y, "5 6")
    try:
        rc, sal, _ = corre([sim, sin, E4_DEMO, "--ms=20"], 120)
    finally:
        os.remove(sin)
    check(rc == 0 and "LED D2 en PN0: apagado" in sal, "y sin el puente 3V3, no")

    grupo("X3 La traza SWO por la sonda, en memoria")
    for placa, fw, chip in ((P401R, P4_SWO, "MSP432P401R"), (E401Y, E4_SWO, "MSP432E401Y")):
        rc, sal, _ = corre([sim, placa, fw, "--swo", "memoria", "--ms=20"], 120)
        l = lineas_swo(sal)
        esperado = ["hola desde el %s por SWO, linea %d" % (chip, k) for k in (1, 2, 3)]
        check(rc == 0 and l == esperado and "129 mensajes ITM" in sal,
              "%s: las tres lineas del ITM, enteras, y 129 mensajes" % chip)
    crudo = variante(P401R, [('<componente tipo="Xds110" id="XDS" et="si">',
                              '<componente tipo="Xds110" id="XDS" et="si" swo="memoria" '
                              'swo_formato="crudo">')])
    try:
        rc, sal, _ = corre([sim, crudo, P4_SWO, "--ms=20"], 120)
    finally:
        os.remove(crudo)
    check(rc == 0 and "SWO 258 bytes" in sal and "\x01h\x01o\x01l\x01a" in sal,
          "en crudo, los 258 bytes de la linea, con la cabecera del ITM delante de cada letra")
    rc, sal, _ = corre([sim, P401R, P4_SWO, "--swo", "memoria", "--swo-bitrate=500000",
                        "--ms=20"], 120)
    check(rc == 0 and not any("hola" in x for x in lineas_swo(sal)),
          "con 500 kbit/s en la sonda y 1 Mbit/s en el TPIU, no llega nada legible")

    grupo("X4 La traza por TCP, con --espera-terminal")
    p = puerto_libre()
    log = tempfile.TemporaryFile(mode="w+")
    proc = subprocess.Popen([sim, P401R, P4_SWO, "--swo", "tcp:%d" % p, "--serie", "BCL=memoria",
                             "--espera-terminal", "--ms=20"], stdout=log, stderr=subprocess.STDOUT)
    recibido = b""
    try:
        t0 = time.time()
        s = None
        while time.time() - t0 < 20 and s is None:
            try:
                s = socket.create_connection(("127.0.0.1", p), timeout=1)
            except OSError:
                time.sleep(0.1)
        if s:
            s.settimeout(1.0)
            t0 = time.time()
            while time.time() - t0 < 20 and recibido.count(b"\n") < 3:
                try:
                    b = s.recv(4096)
                except socket.timeout:
                    continue
                if not b:
                    break
                recibido += b
            s.close()
        proc.wait(30)
    finally:
        if proc.poll() is None:
            proc.kill()
    log.seek(0)
    texto = log.read()
    check(b"hola desde el MSP432P401R por SWO, linea 3\n" in recibido and
          "SWO de XDS" in texto,
          "el cliente recibe las tres lineas: el chip espera a que se conecte")

    grupo("X5 Lo que no vale")
    rc, sal, _ = corre([sim, "placas/nucleo_f446re.xml", "--swo", "memoria", "--valida"], 60)
    check(rc != 0 and "no lleva ninguna sonda Xds110" in sal, "--swo en una placa sin Xds110")
    malo = variante(P401R, [('<componente tipo="Xds110" id="XDS" et="si">',
                             '<componente tipo="Xds110" id="XDS" et="si" swo="memoria" '
                             'swo_formato="manchester">')])
    try:
        rc, sal, _ = corre([sim, malo, "--valida"], 60)
    finally:
        os.remove(malo)
    check(rc != 0 and "swo_formato=\"manchester\" no vale" in sal,
          "swo_formato=\"manchester\": el XDS110 solo lleva NRZ, y se dice")

    valido, que = pr.openocd_valido(a.openocd)
    if valido and not cd.tiene_tcp(a.openocd):
        valido, que = False, "OpenOCD sin el backend tcp de cmsis-dap"
    if not valido:
        print("\n  [--  ] X6 OMITIDA: " + que)
        if os.environ.get("OPENOCD_OBLIGATORIO") == "1":
            check(False, "OPENOCD_OBLIGATORIO=1 y no hay un OpenOCD que sirva")
    else:
        grupo("X6 La sonda de OpenOCD va por J101")

        def examina(placa):
            p = puerto_libre()
            s = cd.Sim(sim, placa, P4_DEMO, p)
            rc, sal, _ = corre(cd.ocd_dap(a.openocd, p, "target/ti/msp432.cfg") +
                               ["-c", "init", "-c", "shutdown"], 60)
            s.fin()
            return s.listo and "Examination succeed" in sal

        check(examina(P401R), "con J101 entero, OpenOCD encuentra el Cortex-M4 por la sonda")
        sin = sin_puente(P401R, "13 14")
        try:
            ok = examina(sin)
        finally:
            os.remove(sin)
        check(not ok, "sin el puente de TMS_SWDIO, no: la sonda no llega al chip")

    print("\nTOTAL XDS110: %d comprobaciones OK, %d fallos" % (pr.OK, pr.MAL))
    return 0 if pr.MAL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
