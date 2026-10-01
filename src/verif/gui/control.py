#!/usr/bin/env python3
# =============================================================================
# control.py — El control de la simulación desde mcu-sim-gui, contra el mcu-sim
# de verdad
#
# Fase 6 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`):
# T_PAUSA, T_SIGUE, T_PASO, T_PARA y el ritmo de T_ARRANCA. Como los otros
# scripts de aquí, hace de VENTANA con su propia implementación del marco
# (`ventana.py`) y arranca el `mcu-sim` de verdad con `--gui`.
#
#   C1  LA PRUEBA QUE PIDE EL PLAN: arrancar a tiempo real, pausar, y ver que el
#       tiempo simulado DEJA DE AVANZAR —los T_ESTADO de la pausa dicen todos el
#       mismo instante— pero el modelo SIGUE CONTESTANDO a T_PING, sin gastar
#       CPU; seguir, ver que avanza otra vez —a su ritmo, sin correr para
#       recuperar la pausa—, y parar: T_FIN con M_PARA y el resumen de siempre;
#   C2  a demanda: arranca en pausa en t = 0 y no se mueve; cada T_PASO avanza
#       EXACTAMENTE lo que dice; dos seguidos se suman; T_SIGUE no vale aquí; y
#       lo simulado a pasos es lo mismo que lo simulado de un tirón;
#   C3  la ventana de tiempo: la de T_ARRANCA manda sobre la de la línea de
#       órdenes; sin ninguna, no hay fin, y si la ventana se va mcu-sim se para
#       solo, porque ya no queda nadie que pueda pararlo;
#   C4  lo que no vale: T_PASO con otro ritmo, un T_PASO que no mide 8 bytes,
#       y que el --tiempo-real de la línea de órdenes deja de mandar.
#
#   make -f Makefile.mcu-sim gui-control
#   python3 verif/gui/control.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien. Tarda unos
# segundos: C1 va a tiempo real.
# =============================================================================
import argparse
import os
import struct
import sys
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, check, grupo, arranca, termina, saludo_hasta_listo, fin,
                     suscribe, instantanea, aviso, estado, arranque, paso, cpu_de,
                     T_INSTANTANEA, T_AVISO, T_ESTADO, T_PONG, T_FIN, T_PING,
                     T_PAUSA, T_SIGUE, T_PASO, T_PARA,
                     M_VENTANA, M_PARA, N_AVISO, F_CORRIENDO, F_PAUSADA, F_TERMINADA,
                     RIT_REAL, RIT_LIBRE, RIT_DEMANDA)

PLACA = "placas/discovery_min.xml"
FW = "verif/fw/blinky/blinky.bin"
MS = 1000000


def id_led(cat):
    for pz in ET.fromstring(cat).iter("pieza"):
        if pz.get("id") == "LD4":
            for o in pz.iter("observable"):
                if o.get("nombre") == "encendido":
                    return int(o.get("id_obs"))
    return None


def hasta(v, cond, seg=5.0):
    """Recibe hasta que cond(tipo, cuerpo) sea cierta. Devuelve (tipo, cuerpo) o
    (None, None), y lo recibido por el camino en v.camino."""
    v.camino = []
    fin_espera = time.time() + seg
    while time.time() < fin_espera:
        t, c = v.recibe(seg=max(0.01, fin_espera - time.time()))
        if t is None:
            return None, None
        v.camino.append((t, c))
        if cond(t, c):
            return t, c
    return None, None


def es_estado(fase=None, t_min=None):
    def f(t, c):
        if t != T_ESTADO:
            return False
        e = estado(c)
        return (fase is None or e[0] == fase) and (t_min is None or e[1] >= t_min)
    return f


def conecta(sim, args):
    v = Ventana()
    p = arranca(sim, args, v.puerto)
    if not v.acepta():
        return v, p, None
    _, _, cat, listo = saludo_hasta_listo(v)
    return v, p, cat if listo else None


# ---------------------------------------------------------------------------
# C1
# ---------------------------------------------------------------------------
def c1_pausa(sim):
    grupo("C1 Pausar, que el tiempo no avance y que conteste; seguir; parar")
    v, p, cat = conecta(sim, [PLACA, FW])            # sin ventana de tiempo
    try:
        if not check(cat is not None, "mcu-sim se conecta y saluda, sin ventana de tiempo"):
            return
        arranque(v, RIT_REAL, 1.0)
        t, c = hasta(v, es_estado(F_CORRIENDO, 200 * MS))
        check(t == T_ESTADO, "a tiempo real, el tiempo simulado avanza: %.0f ms"
              % (estado(c)[1] / 1e6 if c else -1))

        # --- La pausa ---------------------------------------------------------
        v.manda(T_PAUSA)
        t, c = hasta(v, es_estado(F_PAUSADA))
        t_p = estado(c)[1] if c else None
        check(t_p is not None, "T_PAUSA: un T_ESTADO con la fase PAUSADA, en t = %s ms"
              % (t_p / 1e6 if t_p else "?"))
        cpu0 = cpu_de(p.pid)
        h0 = time.time()
        ests, pongs = [], 0
        for k in range(3):
            v.manda(T_PING)
        suscribe(v, 10 * MS, [id_led(cat)])          # en pausa no da muestras
        insts = 0
        while time.time() - h0 < 1.2:
            t, c = v.recibe(seg=0.3)
            if t == T_ESTADO:
                ests.append(estado(c))
            elif t == T_PONG:
                pongs += 1
            elif t == T_INSTANTANEA:
                insts += 1
        cpu1 = cpu_de(p.pid)
        check(len(ests) >= 3 and all(e[0] == F_PAUSADA and e[1] == t_p for e in ests),
              "durante 1,2 s de pausa llegan %d T_ESTADO, todos PAUSADA y todos en el "
              "MISMO instante simulado: el tiempo no avanza" % len(ests))
        check(len(ests) >= 2 and ests[-1][3] == ests[0][3],
              "ni un delta: %d al principio y al final" % (ests[0][3] if ests else -1))
        check(pongs == 3, "y el modelo sigue oyendo: tres T_PING, tres T_PONG")
        check(insts == 0, "una suscripcion en pausa no da muestras: no pasa el tiempo")
        if cpu0 is not None and cpu1 is not None:
            check(cpu1 - cpu0 < 0.15,
                  "y en pausa no gasta CPU: %.3f s en 1,2 s" % (cpu1 - cpu0))

        # --- Sigue ----------------------------------------------------------------
        v.manda(T_SIGUE)
        t, c = hasta(v, es_estado(F_CORRIENDO))
        check(t == T_ESTADO and estado(c)[1] >= t_p,
              "T_SIGUE: un T_ESTADO CORRIENDO, enseguida")
        h1 = time.time()
        t, c = hasta(v, lambda t, c: t == T_INSTANTANEA and instantanea(c)[0] >= t_p + 500 * MS)
        lleva = time.time() - h1
        check(t == T_INSTANTANEA,
              "y el tiempo avanza: llegan muestras otra vez, hasta t_pausa + 500 ms")
        check(0.3 < lleva < 1.5,
              "a tiempo real otra vez: 500 ms simulados tras la pausa tardan %.2f s "
              "de pared, sin correr para recuperar el segundo largo que estuvo "
              "parado" % lleva)

        # --- Para -------------------------------------------------------------------
        v.manda(T_PARA)
        t, c = hasta(v, lambda t, c: t == T_FIN)
        f = fin(c) if t == T_FIN else None
        ests = [estado(x) for y, x in v.camino if y == T_ESTADO]
        check(f is not None and f[0] == M_PARA and f[1] == 0 and f[2] > t_p + 500 * MS,
              "T_PARA en marcha: T_FIN con motivo M_PARA, codigo 0, en t = %s ms"
              % (f[2] / 1e6 if f else "?"))
        check(ests and ests[-1][0] == F_TERMINADA and f is not None and ests[-1][1] == f[2],
              "con un T_ESTADO TERMINADA justo antes, en el mismo instante")
        rc, out, err = termina(p)
        check(rc == 0 and "la ventana pidio parar" in out and "LED LD4" in out and
              "simulados" in out,
              "mcu-sim sale con 0, dice que la ventana pidio parar y da su resumen de "
              "siempre: lo simulado y como acaba cada LED")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


# ---------------------------------------------------------------------------
# C2
# ---------------------------------------------------------------------------
def a_pasos(sim, pasos):
    """A demanda, con los pasos dados. Devuelve (estados de pausa tras cada paso,
    instantaneas, avisos, fin, estado inicial)."""
    v, p, cat = conecta(sim, [PLACA, FW])
    try:
        if cat is None:
            return None
        suscribe(v, 10 * MS, [id_led(cat)])
        arranque(v, RIT_DEMANDA)
        t, c = hasta(v, es_estado(F_PAUSADA))
        inicial = estado(c) if c else None
        tras, insts, avs = [], [], []
        quieto = None
        for k, ns in enumerate(pasos):
            if k == 0:
                # Antes del primer paso: medio segundo de pared sin moverse
                time.sleep(0.5)
                v.manda(T_PING)
                t, c = hasta(v, lambda t, c: t == T_PONG)
                quieto = [estado(x) for y, x in v.camino if y == T_ESTADO]
            if isinstance(ns, tuple):            # varios pasos seguidos, sin esperar
                for n in ns:
                    paso(v, n)
            elif ns is None:
                v.manda(T_SIGUE)
                t, c = hasta(v, lambda t, c: t == T_AVISO)
                avs.append(aviso(c) if c else None)
                continue
            else:
                paso(v, ns)
            objetivo = (tras[-1][1] if tras else inicial[1]) + (sum(ns) if isinstance(ns, tuple) else ns)
            t, c = hasta(v, es_estado(F_PAUSADA, objetivo), seg=20)
            insts += [instantanea(x) for y, x in v.camino if y == T_INSTANTANEA]
            avs += [aviso(x) for y, x in v.camino if y == T_AVISO]
            tras.append(estado(c) if c else (None, None, None, None))
        v.manda(T_PARA)
        t, c = hasta(v, lambda t, c: t == T_FIN)
        insts += [instantanea(x) for y, x in v.camino if y == T_INSTANTANEA]
        f = fin(c) if t == T_FIN else None
        termina(p)
        return tras, insts, avs, f, inicial, quieto
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def de_un_tiron(sim, ventana_ns):
    v, p, cat = conecta(sim, [PLACA, FW, "5000"])
    try:
        if cat is None:
            return None, None
        suscribe(v, 10 * MS, [id_led(cat)])
        arranque(v, RIT_LIBRE, 1.0, ventana_ns)
        insts = []
        while True:
            t, c = v.recibe(seg=60)
            if t == T_INSTANTANEA:
                insts.append(instantanea(c))
            elif t in (T_FIN, None):
                f = fin(c) if t == T_FIN else None
                break
        termina(p)
        return insts, f
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def c2_demanda(sim):
    grupo("C2 A demanda: en pausa desde t = 0, y pasos exactos")
    r = a_pasos(sim, [100 * MS, 250 * MS, 0, None, (50 * MS, 50 * MS), 150 * MS])
    if not check(r is not None, "mcu-sim se conecta y arranca a demanda"):
        return
    tras, insts, avs, f, inicial, quieto = r
    check(inicial is not None and inicial[0] == F_PAUSADA and inicial[1] == 0,
          "arranca EN PAUSA, en t = 0: a demanda no avanza sin T_PASO")
    check(quieto is not None and all(e[1] == 0 for e in quieto),
          "y medio segundo despues sigue en t = 0, contestando a T_PING")
    ts = [e[1] for e in tras]
    check(ts[:3] == [100 * MS, 350 * MS, 350 * MS] and all(e[0] == F_PAUSADA for e in tras),
          "T_PASO de 100 ms: en pausa otra vez en EXACTAMENTE 100 ms; otro de 250, en "
          "350; uno de 0, se queda en 350 (%s)" % ", ".join("%g" % (t / 1e6) for t in ts))
    check(len(avs) >= 1 and avs[0] is not None and avs[0][0] == N_AVISO and
          "T_PASO" in avs[0][3],
          "T_SIGUE a demanda: un T_AVISO, porque aqui solo se avanza con T_PASO (\"%s\")"
          % (avs[0][3] if avs and avs[0] else "?"))
    check(ts[3:] == [450 * MS, 600 * MS],
          "dos pasos de 50 ms seguidos se suman: 450; y uno mas de 150: 600")
    check(f is not None and f[0] == M_PARA and f[2] == 600 * MS,
          "T_PARA en pausa: T_FIN con M_PARA en los mismos 600 ms")
    libre, f2 = de_un_tiron(sim, 600 * MS)
    a_pasos_t = [(t, m) for t, _, m in insts]
    libre_t = [(t, m) for t, _, m in libre or []]
    # Se compara lo de antes de 600 ms. La muestra de 600 cae en el mismo
    # instante en que el paso se para, y T_PARA llega antes de que se tome
    a_pasos_t = [x for x in a_pasos_t if x[0] < 600 * MS]
    libre_t = [x for x in libre_t if x[0] < 600 * MS]
    check(len(a_pasos_t) == 59 and a_pasos_t == libre_t,
          "y lo simulado a pasos es lo MISMO que de un tiron, con ritmo libre: las "
          "59 muestras del LED hasta 600 ms, instante a instante y valor a valor")


# ---------------------------------------------------------------------------
# C3
# ---------------------------------------------------------------------------
def c3_ventana(sim):
    grupo("C3 La ventana de tiempo")
    insts, f = de_un_tiron(sim, 30 * MS)
    check(f is not None and f[0] == M_VENTANA and f[2] == 30110000,
          "la de T_ARRANCA manda sobre la de la linea de ordenes: 30 ms y no 5000, "
          "mas los 110 us del arranque electrico (%s ns)" % (f[2] if f else "?"))

    v, p, cat = conecta(sim, [PLACA, FW])
    try:
        arranque(v, RIT_LIBRE)
        t, c = hasta(v, es_estado(F_CORRIENDO, 50 * MS), seg=10)
        corre = t == T_ESTADO
        time.sleep(0.2)
        t2, c2 = hasta(v, es_estado(F_CORRIENDO), seg=5)
        check(corre and t2 == T_ESTADO and estado(c2)[1] > 1000 * MS,
              "sin ventana de tiempo, ni aqui ni en la linea de ordenes, no hay fin: "
              "con ritmo libre ya va por %.0f ms" % (estado(c2)[1] / 1e6 if c2 else -1))
        v.cierra()
        rc, out, err = termina(p, seg=20)
        check(rc == 0 and "no queda quien la pare" in err and "LED LD4" in out,
              "y si la ventana se va, mcu-sim se para solo -nadie podria ya- y da su "
              "resumen")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


# ---------------------------------------------------------------------------
# C4
# ---------------------------------------------------------------------------
def c4_lo_que_no(sim):
    grupo("C4 Lo que no vale")
    v, p, cat = conecta(sim, [PLACA, FW, "1000", "--tiempo-real"])
    try:
        h0 = time.time()
        arranque(v, RIT_LIBRE)
        t, c = hasta(v, lambda t, c: t == T_FIN, seg=30)
        lleva = time.time() - h0
        rc, out, err = termina(p)
        check(t == T_FIN and fin(c)[0] == M_VENTANA and rc == 0 and lleva < 0.8 and
              "--tiempo-real no se aplica" in out and "simulados 1000.000 ms" in out,
              "el ritmo lo dice la ventana: con --tiempo-real en la linea de ordenes "
              "y ritmo libre en T_ARRANCA, 1000 ms simulados tardan %.2f s, y lo dice"
              % lleva)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()

    v, p, cat = conecta(sim, [PLACA, FW, "2000"])
    try:
        arranque(v, RIT_REAL, 1.0)
        paso(v, 10 * MS)
        t, c = hasta(v, lambda t, c: t == T_AVISO, seg=10)
        a = aviso(c) if c else None
        check(a is not None and "solo tiene sentido con ritmo a demanda" in a[3],
              "T_PASO a tiempo real: T_AVISO, y se ignora (\"%s\")" % (a[3] if a else "?"))
        v.manda(T_PARA)
        t, c = hasta(v, lambda t, c: t == T_FIN)
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()

    v, p, cat = conecta(sim, [PLACA, FW])
    try:
        arranque(v, RIT_DEMANDA)
        hasta(v, es_estado(F_PAUSADA))
        v.manda(T_PASO, b"\x01\x02\x03")
        t, c = hasta(v, lambda t, c: t == T_AVISO)
        a = aviso(c) if c else None
        check(a is not None and "3 bytes" in a[3],
              "un T_PASO de 3 bytes: T_AVISO (\"%s\")" % (a[3] if a else "?"))
        v.manda(T_PARA)
        t, c = hasta(v, lambda t, c: t == T_FIN)
        check(t == T_FIN and fin(c)[2] == 0, "y sigue en t = 0, hasta que se para")
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def main():
    a = argparse.ArgumentParser(description="el control de la simulacion, contra el mcu-sim de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    c1_pausa(sim)
    c2_demanda(sim)
    c3_ventana(sim)
    c4_lo_que_no(sim)
    return ventana.resumen("CONTROL")


if __name__ == "__main__":
    sys.exit(main())
