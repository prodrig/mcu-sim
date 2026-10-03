#!/usr/bin/env python3
# =============================================================================
# marcha.py — mcu-sim-gui con la simulación en marcha, contra el mcu-sim de verdad
#
# Fase 4 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`): el
# sentido modelo -> pantalla entero. Como `saludo.py`, este script hace de
# VENTANA con su propia implementación del marco (`ventana.py`), arranca el
# `mcu-sim` de verdad con `--gui` y mira lo que llega.
#
# La prueba que pide el plan es la primera:
#
#   M1  `placas/discovery_min.xml` con el blinky, suscrito a `encendido` y
#       `corriente` de LD4 cada 1 ms desde antes de arrancar: EL LED PARPADEA EN
#       LAS INSTANTÁNEAS, seis flancos separados 100 ms, que es lo que programa
#       el firmware (BLINK_MS = 100, N_BLINKS = 6). Sin huecos, sin pérdidas, y
#       con los T_ESTADO que tienen que ir con ellas;
#   M2  y otra vez: la MISMA secuencia, instante a instante y valor a valor. Una
#       suscripción mandada antes de arrancar es reproducible al picosegundo;
#   M3  los avisos de placa: una placa con un conflicto eléctrico los manda como
#       T_AVISO entre T_CATALOGO y T_LISTO, con y sin --valida;
#   M4  en marcha, a tiempo real (RIT_REAL) para que dé tiempo: el latido de
#       T_ESTADO sin suscripción, una suscripción nueva que empieza a dar muestras, un
#       T_PING con su T_PONG, una suscripción a un observable que no existe
#       (T_AVISO, y la anterior sigue), y una vacía que las apaga;
#   M5  la ventana se va en marcha: mcu-sim lo dice y sigue simulando hasta el
#       final de su ventana, y termina con código 0.
#
# La contrapresión —instantáneas que se tiran, avisos que no— no se puede
# provocar con fiabilidad desde aquí: el sistema operativo amortigua megas.
# Está en el banco `testgui`, con un canal en memoria que se atasca a voluntad.
#
#   make -f Makefile.mcu-sim gui-marcha
#   python3 verif/gui/marcha.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, RIT_REAL, check, grupo, arranca, termina, saludo_hasta_listo, fin,
                     suscribe, instantanea, aviso, estado,
                     T_INSTANTANEA, T_AVISO, T_ESTADO, T_PONG, T_FIN, T_ARRANCA, T_PING,
                     M_VENTANA, RIT_LIBRE, N_AVISO, F_CORRIENDO, F_TERMINADA)

PLACA = "placas/discovery_min.xml"
FW = "verif/fw/blinky/blinky.bin"
BLINK_MS = 100           # verif/fw/blinky/main.c
N_BLINKS = 6


def ids_de(cat, pieza, *nombres):
    """Los id_obs de los observables `nombres` de `pieza`, en el catálogo."""
    for pz in ET.fromstring(cat).iter("pieza"):
        if pz.get("id") == pieza:
            por_nombre = {o.get("nombre"): int(o.get("id_obs")) for o in pz.iter("observable")}
            return [por_nombre[n] for n in nombres]
    return None



def hasta_fin(v, seg=120):
    """Todo lo que llegue hasta T_FIN: (instantaneas, avisos, estados, fin)."""
    inst, avs, ests = [], [], []
    while True:
        t, c = v.recibe(seg=seg)
        if t is None:
            return inst, avs, ests, None
        if t == T_INSTANTANEA:
            inst.append(instantanea(c))
        elif t == T_AVISO:
            avs.append(aviso(c))
        elif t == T_ESTADO:
            ests.append(estado(c))
        elif t == T_FIN:
            return inst, avs, ests, fin(c)


def flancos(inst, idx=0):
    """Los instantes en que cambia la muestra `idx`, y hacia dónde."""
    r, prev = [], None
    for t, _, m in inst:
        val = m[idx][1]
        if prev is not None and val != prev:
            r.append((t, val))
        prev = val
    return r


def blinky(sim, ms="1000"):
    v = Ventana()
    p = arranca(sim, [PLACA, FW, ms], v.puerto)
    try:
        if not v.acepta():
            return None, None, None, None, None
        _, _, cat, listo = saludo_hasta_listo(v)
        ids = ids_de(cat, "LD4", "encendido", "corriente")
        suscribe(v, 1000000, ids)                  # cada 1 ms, desde t = 0
        arranque(v)
        inst, avs, ests, f = hasta_fin(v)
        rc, out, err = termina(p)
        return ids, inst, ests, f, rc
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def m1_m2(sim):
    grupo("M1 El LED parpadea en las instantaneas, con el periodo del firmware")
    ids, inst, ests, f, rc = blinky(sim)
    if not check(inst is not None and ids is not None and len(inst) > 0,
                 "mcu-sim se conecta, se suscribe a encendido y corriente de LD4, y "
                 "llegan instantaneas"):
        return
    ts = [t for t, _, _ in inst]
    check(all(b - a == 1000000 for a, b in zip(ts, ts[1:])) and ts[0] == 1000000 and
          len(ts) == 1000,
          "mil instantaneas, una por milisegundo de 1 a 1000 ms, sin un hueco")
    check(all(p == 0 for _, p, _ in inst), "y sin perder ninguna")
    check(all([i for i, _ in m] == ids for _, _, m in inst),
          "cada una con los dos observables pedidos, en el orden pedido")
    check(all(m[0][1] in (0.0, 1.0) for _, _, m in inst), "encendido vale 0 o 1")
    fl = flancos(inst)
    intervalos = [b[0] - a[0] for a, b in zip(fl, fl[1:])]
    check(len(fl) == N_BLINKS and [d for _, d in fl] == [1.0, 0.0] * (N_BLINKS // 2),
          "seis flancos, alternando: se enciende, se apaga, tres veces (%s)"
          % ", ".join("%g ms %s" % (t / 1e6, "on" if d else "off") for t, d in fl))
    check(len(intervalos) == N_BLINKS - 1 and
          all(abs(d - BLINK_MS * 1000000) <= 1000000 for d in intervalos),
          "separados %d ms, que es lo que programa el firmware, con la resolucion "
          "del muestreo, 1 ms (%s)" % (BLINK_MS, ", ".join("%g" % (d / 1e6) for d in intervalos)))
    on = [m[1][1] for _, _, m in inst if m[0][1] == 1.0]
    off = [m[1][1] for _, _, m in inst if m[0][1] == 0.0]
    check(on and off and min(on) > 0.5 and max(off) == 0.0,
          "y la corriente acompana: %.2f mA encendido, 0 apagado" % (sum(on) / len(on)))
    check(len(ests) >= len(inst) // 50 and
          all(b[1] >= a[1] and b[3] >= a[3] for a, b in zip(ests, ests[1:])),
          "llegan T_ESTADO (%d), con el tiempo simulado y los deltas sin retroceder"
          % len(ests))
    check(ests and ests[-1][0] == F_TERMINADA and f is not None and ests[-1][1] == f[2] and
          all(e[0] == F_CORRIENDO for e in ests[:-1]),
          "el ultimo, TERMINADA y en el instante del T_FIN; los demas, CORRIENDO")
    check(f is not None and f[0] == M_VENTANA and f[2] == 1000110000 and rc == 0,
          "y T_FIN al acabar la ventana, en 1000 ms y 110 us, con codigo 0")

    grupo("M2 Y otra vez: la misma secuencia, al picosegundo")
    _, inst2, _, _, _ = blinky(sim)
    check(inst2 is not None and inst2 == inst,
          "las mil instantaneas de la segunda ejecucion son identicas a las de la "
          "primera, instante a instante y valor a valor")


def m3_placa(sim):
    grupo("M3 Los avisos de placa, antes de arrancar")
    d = tempfile.mkdtemp(prefix="placa")
    try:
        placa = os.path.join(d, "conflicto.xml")
        with open(placa, "w") as f:
            f.write('<placa nombre="conflicto">\n'
                    '  <nodo id="suelto" externo="si"/>\n'
                    '  <componente tipo="Rpull" id="RA" v="3.3" r="10000">\n'
                    '    <pin nombre="a" nodo="suelto"/>\n'
                    '  </componente>\n'
                    '  <componente tipo="Rpull" id="RB" v="0" r="10000">\n'
                    '    <pin nombre="a" nodo="suelto"/>\n'
                    '  </componente>\n'
                    '</placa>\n')
        for valida in (False, True):
            v = Ventana()
            p = arranca(sim, [placa, "10"] + (["--valida"] if valida else []), v.puerto)
            try:
                v.acepta()
                _, pl, cat, listo = saludo_hasta_listo(v, valida=valida)
                if valida:
                    # sin T_LISTO: los avisos y luego T_FIN
                    t, c = v.recibe()
                    while t == T_AVISO:
                        v.avisos_placa.append(aviso(c))
                        t, c = v.recibe()
                    llego = t == T_FIN
                else:
                    llego = listo
                av = v.avisos_placa
                check(llego and len(av) == 1 and av[0][0] == N_AVISO and av[0][1] == 0 and
                      av[0][2] == "placa" and "conducen a la vez RA.a y RB.a" in av[0][3],
                      "%s: un T_AVISO de origen 'placa', en t = 0, antes de %s: \"%s\""
                      % ("--valida" if valida else "simulando",
                         "T_FIN" if valida else "T_LISTO", av[0][3] if av else "?"))
                if not valida:
                    v.manda(ventana.T_PARA)
                rc, out, err = termina(p)
                check(rc == 0 and "conducen a la vez" in err,
                      "y la consola lo sigue diciendo, como siempre")
            finally:
                if p.poll() is None:
                    p.kill()
                v.cierra()
    finally:
        shutil.rmtree(d, ignore_errors=True)


def m4_en_marcha(sim):
    grupo("M4 En marcha: latido, suscripciones, T_PING")
    v = Ventana()
    # 3 s simulados a tiempo real: tiempo de pared para hablar con el
    p = arranca(sim, [PLACA, FW, "3000"], v.puerto)
    try:
        v.acepta()
        _, _, cat, _ = saludo_hasta_listo(v)
        ids = ids_de(cat, "LD4", "encendido")
        arranque(v, RIT_REAL)
        t, c = v.recibe(seg=5)
        e = estado(c) if t == T_ESTADO else None
        check(e is not None and e[0] == F_CORRIENDO,
              "sin suscripcion no hay instantaneas, pero el latido de T_ESTADO llega "
              "solo: t = %s ms" % (e[1] / 1e6 if e else "?"))
        suscribe(v, 10000000, ids)                 # cada 10 ms
        inst = []
        fin_espera = time.time() + 5
        while len(inst) < 5 and time.time() < fin_espera:
            t, c = v.recibe(seg=5)
            if t == T_INSTANTANEA:
                inst.append(instantanea(c))
        check(len(inst) == 5 and all(i[0] % 10000000 == 0 for i in inst) and
              all(b[0] - a[0] == 10000000 for a, b in zip(inst, inst[1:])),
              "una suscripcion en marcha da muestras desde el siguiente multiplo de "
              "10 ms (%s ms...)" % (inst[0][0] / 1e6 if inst else "?"))
        v.manda(T_PING)
        suscribe(v, 10000000, [9999])
        visto_pong, rechazo = False, None
        fin_espera = time.time() + 5
        while not (visto_pong and rechazo) and time.time() < fin_espera:
            t, c = v.recibe(seg=5)
            if t == T_PONG:
                visto_pong = True
            elif t == T_AVISO:
                rechazo = aviso(c)
        check(visto_pong, "un T_PING en marcha tiene su T_PONG")
        check(rechazo is not None and rechazo[0] == N_AVISO and "9999" in rechazo[3] and
              "sigue la anterior" in rechazo[3],
              "una suscripcion a un observable que no existe: T_AVISO, y lo dice (\"%s\")"
              % (rechazo[3] if rechazo else "?"))
        t, c = v.recibe(seg=5)
        while t not in (T_INSTANTANEA, None):
            t, c = v.recibe(seg=5)
        check(t == T_INSTANTANEA, "y la anterior sigue dando muestras")
        suscribe(v, 10000000, [])
        time.sleep(0.2)
        # Lo que estuviera ya en camino, fuera
        while True:
            v.c.settimeout(0.05)
            try:
                if not v.c.recv(65536):
                    break
            except Exception:
                break
        v.buf = b""
        llegan = 0
        fin_espera = time.time() + 1.0
        while time.time() < fin_espera:
            t, c = v.recibe(seg=0.3)
            if t == T_INSTANTANEA:
                llegan += 1
        check(llegan == 0, "y una suscripcion vacia las apaga")
        p.kill()                     # parar en marcha es de control.py (fase 6)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def m5_se_va(sim):
    grupo("M5 La ventana se va en marcha: mcu-sim sigue")
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "1500"], v.puerto)
    try:
        v.acepta()
        _, _, cat, _ = saludo_hasta_listo(v)
        suscribe(v, 10000000, ids_de(cat, "LD4", "encendido"))
        arranque(v, RIT_REAL)
        t, _ = v.recibe(seg=5)
        while t not in (T_INSTANTANEA, None):
            t, _ = v.recibe(seg=5)
        v.cierra()
        rc, out, err = termina(p, seg=30)
        check(rc == 0 and "la ventana cerro la conexion; se sigue simulando" in err,
              "la ventana se cierra tras la primera instantanea: mcu-sim lo dice")
        check("simulados 1500.000 ms" in out,
              "y simula su ventana entera, los 1500 ms, y termina con codigo 0")
    finally:
        if p.poll() is None:
            p.kill()


def main():
    a = argparse.ArgumentParser(description="mcu-sim-gui en marcha, contra el mcu-sim de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    m1_m2(sim)
    m3_placa(sim)
    m4_en_marcha(sim)
    m5_se_va(sim)
    return ventana.resumen("MARCHA")


if __name__ == "__main__":
    sys.exit(main())
