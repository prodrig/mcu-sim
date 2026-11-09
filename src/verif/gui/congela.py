#!/usr/bin/env python3
# =============================================================================
# congela.py — La ventana y --stopped-core-stops-sim (common/congela.h)
#
# Con un núcleo parado por el depurador el tiempo simulado no avanza, pero el
# enlace con la ventana sigue atendido: los T_ESTADO dicen F_CONGELADA (4) y
# todos el mismo instante, el modelo contesta a T_PING, y T_PARA lo termina.
# Hace de ventana con `ventana.py`, arranca el `mcu-sim` de verdad con --gui y
# --gdb-dap, y para el núcleo con gdb-multiarch.
#
#   W1  sin depurador, F_CORRIENDO y el tiempo avanza
#   W2  GDB conectado (el stub para el núcleo): F_CONGELADA, el mismo instante
#       durante 1,5 s de pared, y los T_PING contestados
#   W3  `monitor resume`: otra vez F_CORRIENDO, y el tiempo avanza
#   W4  parado otra vez, T_PARA: T_FIN con M_PARA
#
#   make -f Makefile.mcu-sim gui-congela
#   python3 verif/gui/congela.py [--sim build/mcu-sim] [--gdb-arm gdb-multiarch]
# =============================================================================
import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time

import ventana
from ventana import (Ventana, check, grupo, arranca, termina, saludo_hasta_listo, fin, estado,
                     arranque, T_ESTADO, T_PONG, T_FIN, T_PING, T_PARA, M_PARA,
                     F_CORRIENDO, F_CONGELADA, RIT_LIBRE)

PLACA = "placas/discovery_min.xml"
FW = "verif/fw/blinky/blinky.bin"


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def estados(v, seg):
    """Los T_ESTADO (y los T_PONG) que llegan en `seg` segundos de pared"""
    es, pongs = [], 0
    h0 = time.time()
    while time.time() - h0 < seg:
        t, c = v.recibe(seg=0.2)
        if t == T_ESTADO:
            es.append(estado(c))
        elif t == T_PONG:
            pongs += 1
    return es, pongs


def gdb_bg(gdb, puerto, cmds):
    a = [gdb, "-q", "-batch", "-ex", "set architecture arm", "-ex", "target remote localhost:%d" % puerto]
    for c in cmds:
        a += ["-ex", c]
    return subprocess.Popen(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim"))
    ap.add_argument("--gdb-arm", default=None)
    a = ap.parse_args()
    gdb = a.gdb_arm or shutil.which("gdb-multiarch") or shutil.which("arm-none-eabi-gdb")
    if not gdb:
        print("  [OMITE] hace falta gdb-multiarch o arm-none-eabi-gdb")
        sys.exit(0)
    grupo("W La ventana con --stopped-core-stops-sim y un nucleo parado por GDB")
    pg = puerto_libre()
    v = Ventana()
    p = arranca(a.sim, [PLACA, FW, "--gdb-dap", "--port=%d" % pg, "--stopped-core-stops-sim"], v.puerto)
    try:
        if not check(v.acepta(), "mcu-sim se conecta a la ventana"):
            return
        _, _, cat, listo = saludo_hasta_listo(v)
        if not check(listo, "saluda y esta lista"):
            return
        arranque(v, RIT_LIBRE)
        es, _ = estados(v, 1.0)
        check(es and es[-1][0] == F_CORRIENDO and es[-1][1] > es[0][1],
              "W1: sin depurador, F_CORRIENDO y el tiempo avanza (%s ms)"
              % (es[-1][1] / 1e6 if es else "?"))
        # GDB para el núcleo y lo deja parado 4 s
        g = gdb_bg(gdb, pg, ["info registers pc", "shell sleep 4", "monitor resume", "detach"])
        h0 = time.time()
        while time.time() - h0 < 10:
            es, _ = estados(v, 0.3)
            if es and es[-1][0] == F_CONGELADA:
                break
        for k in range(3):
            v.manda(T_PING)
        es, pongs = estados(v, 1.5)
        check(len(es) >= 3 and all(e[0] == F_CONGELADA for e in es) and len(set(e[1] for e in es)) == 1,
              "W2: con el nucleo parado por GDB, %d T_ESTADO, todos F_CONGELADA en t = %s ms"
              % (len(es), es[0][1] / 1e6 if es else "?"))
        check(pongs == 3, "W2: y contesta a los tres T_PING (%d)" % pongs)
        g.communicate(timeout=60)
        es, _ = estados(v, 1.5)
        check(es and es[-1][0] == F_CORRIENDO and es[-1][1] > es[0][1],
              "W3: monitor resume: F_CORRIENDO otra vez y el tiempo avanza")
        # Parado otra vez, y T_PARA
        g = gdb_bg(gdb, pg, ["info registers pc", "shell sleep 5", "detach"])
        h0 = time.time()
        while time.time() - h0 < 10:
            es, _ = estados(v, 0.3)
            if es and es[-1][0] == F_CONGELADA:
                break
        v.manda(T_PARA)
        t_fin = None
        h0 = time.time()
        while time.time() - h0 < 10:
            t, c = v.recibe(seg=0.5)
            if t == T_FIN:
                t_fin = fin(c)
                break
        check(t_fin is not None and t_fin[0] == M_PARA, "W4: congelada, T_PARA la termina: T_FIN con M_PARA")
        g.kill()
    finally:
        rc, out, err = termina(p)
        check(rc == 0, "mcu-sim sale con codigo 0 (%s)" % rc)
        check("[congela]" in out, "su salida dice [congela]")
        v.cierra()
    print("TOTAL GUI CONGELA: %d comprobaciones OK, %d fallos" % (ventana.ok, ventana.fallos))
    sys.exit(1 if ventana.fallos else 0)


if __name__ == "__main__":
    main()
