#!/usr/bin/env python3
# =============================================================================
# ordenes.py — Las órdenes de mcu-sim-gui, contra el mcu-sim de verdad
#
# Fase 5 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`): el
# sentido pantalla -> modelo. Como `saludo.py` y `marcha.py`, este script hace
# de VENTANA con su propia implementación del marco (`ventana.py`), arranca el
# `mcu-sim` de verdad con `--gui` y mira lo que vuelve. Sin GUI ninguna: es la
# prueba que pide el plan.
#
#   O1  EL EJEMPLO DEL ENUNCIADO (`doc/protocolo.md` §5): cuatro órdenes sobre
#       el pulsador B1 de `placas/discovery_min.xml`, en UN T_ORDENES enviado
#       antes de T_ARRANCA, con deltas. Los cuatro T_ORDEN_HECHA traen
#       EXACTAMENTE 1,00 s, 1,50 s, 4,00 s y 4,22 s; llegan a medida que se
#       aplican, entre las instantáneas, y no de golpe; y `pulsado`, muestreado
#       cada 10 ms, vale 1 justo en [1,00, 1,50) y en [4,00, 4,22);
#   O2  y otra vez: los mismos ecos y las mismas instantáneas, al nanosegundo.
#       Una secuencia enviada antes de arrancar es reproducible;
#   O3  las tres que hacen falta al lado, más dos: una pieza que no existe
#       (RES_PIEZA), un mando que no existe (RES_MANDO), un valor fuera de
#       rango (recorte, RES_RANGO y un T_AVISO detrás), un delta de cero (dos
#       órdenes en el mismo instante, en el orden del mensaje) y un T_ORDENES
#       malformado (un T_AVISO, y no se aplica ninguna). En varios mensajes
#       antes de arrancar: la primera orden de cada uno es absoluta;
#   O4  en marcha, a tiempo real (RIT_REAL) para que dé tiempo: la primera
#       orden es relativa al instante en que el modelo la lee, que no se sabe de
#       antemano —eso es lo que la hace irrepetible—, pero las siguientes
#       guardan sus deltas exactos.
#
#   make -f Makefile.mcu-sim gui-ordenes
#   python3 verif/gui/ordenes.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import struct
import sys
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, RIT_REAL, check, grupo, arranca, termina, saludo_hasta_listo, fin,
                     suscribe, instantanea, aviso, ordenes, hecha,
                     T_INSTANTANEA, T_AVISO, T_ESTADO, T_ORDEN_HECHA, T_FIN, T_ARRANCA,
                     M_VENTANA, RIT_LIBRE, N_AVISO,
                     RES_OK, RES_PIEZA, RES_MANDO, RES_RANGO)

PLACA = "placas/discovery_min.xml"
FW = "verif/fw/blinky/blinky.bin"
S = 1000000000           # un segundo, en ns
MS = 1000000


def pieza_de(cat, pid):
    """(idx de pieza, {nombre de mando: idx}, {nombre de observable: id_obs})."""
    for pz in ET.fromstring(cat).iter("pieza"):
        if pz.get("id") == pid:
            mandos = {m.get("nombre"): int(m.get("idx")) for m in pz.iter("mando")}
            obs = {o.get("nombre"): int(o.get("id_obs")) for o in pz.iter("observable")}
            return int(pz.get("idx")), mandos, obs
    return None, {}, {}


def n_piezas(cat):
    return len(list(ET.fromstring(cat).iter("pieza")))



def hasta_fin(v, seg=120):
    """Todo lo que llegue hasta T_FIN, EN ORDEN: [(tipo, cosa)], y el T_FIN."""
    r = []
    while True:
        t, c = v.recibe(seg=seg)
        if t is None:
            return r, None
        if t == T_INSTANTANEA:
            r.append((t, instantanea(c)))
        elif t == T_ORDEN_HECHA:
            r.append((t, hecha(c)))
        elif t == T_AVISO:
            r.append((t, aviso(c)))
        elif t == T_FIN:
            return r, fin(c)


def de(r, tipo):
    return [x for t, x in r if t == tipo]


# ---------------------------------------------------------------------------
# O1 y O2
# ---------------------------------------------------------------------------
def enunciado(sim):
    """El ejemplo de doc/protocolo.md §5. Devuelve (b1, lo recibido, fin, rc)."""
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "5000"], v.puerto)
    try:
        if not v.acepta():
            return None, None, None, None
        _, _, cat, listo = saludo_hasta_listo(v)
        b1, mandos, obs = pieza_de(cat, "B1")
        if b1 is None or not listo:
            return None, None, None, None
        pulsar = mandos["pulsar"]
        suscribe(v, 10 * MS, [obs["pulsado"]])
        ordenes(v, [(1000000000, b1, pulsar, 1.0),   # pulsa en t = 1,00 s
                    (500000000,  b1, pulsar, 0.0),   # suelta en t = 1,50 s
                    (2500000000, b1, pulsar, 1.0),   # pulsa en t = 4,00 s
                    (220000000,  b1, pulsar, 0.0)])  # suelta en t = 4,22 s
        arranque(v)
        r, f = hasta_fin(v)
        rc, _, _ = termina(p)
        return (b1, pulsar), r, f, rc
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def o1_o2(sim):
    grupo("O1 El ejemplo del enunciado: cuatro ordenes antes de arrancar")
    b1, r, f, rc = enunciado(sim)
    if not check(r is not None and f is not None,
                 "mcu-sim se conecta, recibe las cuatro ordenes en un T_ORDENES antes "
                 "de T_ARRANCA, simula 5 s y termina"):
        return
    ecos = de(r, T_ORDEN_HECHA)
    check([e[0] for e in ecos] == [1 * S, 1500 * MS, 4 * S, 4220 * MS],
          "cuatro T_ORDEN_HECHA en EXACTAMENTE 1,00 s, 1,50 s, 4,00 s y 4,22 s (%s)"
          % ", ".join("%.9f" % (e[0] / S) for e in ecos))
    check(all(e[1:3] == b1 and e[4] == RES_OK for e in ecos) and
          [e[3] for e in ecos] == [1.0, 0.0, 1.0, 0.0],
          "todas a B1.pulsar, con su valor -pulsa, suelta, pulsa, suelta- y RES_OK")
    # A medida que se aplican: lo ultimo que llega antes de cada eco es la
    # instantanea de 10 ms antes. La de su mismo instante -que ya ve la orden-
    # sale detras: en cada vuelta del enlace, los ecos van primero.
    previa = []
    for k, (t, x) in enumerate(r):
        if t == T_ORDEN_HECHA:
            antes = [y[0] for u, y in r[:k] if u == T_INSTANTANEA]
            previa.append(antes[-1] if antes else None)
    check(len(ecos) == 4 and previa == [e[0] - 10 * MS for e in ecos],
          "y llegan a medida que se aplican, cada uno justo detras de la instantanea "
          "de 10 ms antes, no de golpe al final")
    inst = de(r, T_INSTANTANEA)
    ts = [t for t, _, _ in inst]

    def esperado(t):
        return 1.0 if (1 * S <= t < 1500 * MS) or (4 * S <= t < 4220 * MS) else 0.0
    check(len(inst) == 500 and ts == [10 * MS * (k + 1) for k in range(500)] and
          all(per == 0 for _, per, _ in inst),
          "quinientas instantaneas, una cada 10 ms, sin perder ninguna")
    malas = [t for t, _, m in inst if m[0][1] != esperado(t)]
    check(not malas,
          "y `pulsado` vale 1 justo en [1,00, 1,50) y [4,00, 4,22): la muestra de "
          "1,00 ve la pulsacion de 1,00, y la de 1,50 ya la ve suelta%s"
          % ("" if not malas else " (mal en %s)" % ", ".join("%g" % (t / S) for t in malas[:5])))
    check(f[0] == M_VENTANA and rc == 0 and not de(r, T_AVISO),
          "sin un solo aviso, y T_FIN al final de la ventana, con codigo 0")

    grupo("O2 Y otra vez: reproducible al nanosegundo")
    _, r2, _, _ = enunciado(sim)
    check(r2 is not None and de(r2, T_ORDEN_HECHA) == ecos,
          "los mismos cuatro ecos, con los mismos instantes y valores")
    check(r2 is not None and de(r2, T_INSTANTANEA) == inst and
          [t for t, _ in r2] == [t for t, _ in r],
          "y las mismas quinientas instantaneas, intercaladas con los ecos en el "
          "mismo orden")


# ---------------------------------------------------------------------------
# O3
# ---------------------------------------------------------------------------
def o3_validacion(sim):
    grupo("O3 Pieza que no existe, rango, delta 0 y un T_ORDENES malformado")
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "500"], v.puerto)
    try:
        v.acepta()
        _, _, cat, _ = saludo_hasta_listo(v)
        b1, mandos, obs = pieza_de(cat, "B1")
        ld4, _, _ = pieza_de(cat, "LD4")
        pulsar = mandos["pulsar"]
        nada = n_piezas(cat)                     # el primer indice que no existe
        suscribe(v, 10 * MS, [obs["pulsado"]])
        # Cada mensaje empieza en un instante ABSOLUTO: antes de arrancar
        ordenes(v, [(100 * MS, nada, 0, 1.0)])                         # no hay tal pieza
        ordenes(v, [(110 * MS, ld4, 0, 1.0), (0, b1, 7, 1.0)])         # sin ese mando
        ordenes(v, [(150 * MS, b1, pulsar, 5.0)])                       # por encima
        ordenes(v, [(200 * MS, b1, pulsar, 1.0), (0, b1, pulsar, 0.0)])  # delta 0
        ordenes(v, [(300 * MS, b1, pulsar, -2.0)])                      # por debajo
        v.manda(ventana.T_ORDENES, b"\x00" * 20)                        # malformado
        arranque(v)
        r, f = hasta_fin(v)
        rc, _, _ = termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    if not check(f is not None and rc == 0, "mcu-sim simula 500 ms y termina con codigo 0"):
        return
    ecos = de(r, T_ORDEN_HECHA)
    avs = de(r, T_AVISO)
    check(len(ecos) == 7, "siete ordenes, siete ecos: ninguna se descarta en silencio")
    if len(ecos) != 7:
        return
    check(ecos[0][0] == 100 * MS and ecos[0][1] == nada and ecos[0][4] == RES_PIEZA,
          "una pieza que no existe (%d): RES_PIEZA, en su instante" % nada)
    check(ecos[1][4] == RES_MANDO and ecos[1][1] == ld4 and
          ecos[2][4] == RES_MANDO and ecos[2][2] == 7 and ecos[2][0] == 110 * MS,
          "un LED, que no tiene mandos, y un mando 7 de B1: RES_MANDO las dos")
    check(ecos[3][0] == 150 * MS and ecos[3][4] == RES_RANGO and ecos[3][3] == 1.0 and
          ecos[6][0] == 300 * MS and ecos[6][4] == RES_RANGO and ecos[6][3] == 0.0,
          "un 5 se recorta a 1 y un -2 a 0, con RES_RANGO, y se aplican")
    rango = [a for a in avs if "fuera de rango" in a[3]]
    check(len(rango) == 2 and rango[0][0] == N_AVISO and rango[0][1] == 150 * MS and
          "B1.pulsar" in rango[0][3] and "pasa del maximo" in rango[0][3] and
          "no llega al minimo" in rango[1][3],
          "cada recorte, con su T_AVISO en el mismo instante: \"%s\""
          % (rango[0][3] if rango else "?"))
    k_eco = [k for k, (t, x) in enumerate(r) if t == T_ORDEN_HECHA and x[4] == RES_RANGO]
    check(all(k + 1 < len(r) and r[k + 1][0] == T_AVISO for k in k_eco),
          "y el aviso va justo detras de su eco")
    check(ecos[4][0] == ecos[5][0] == 200 * MS and
          [ecos[4][3], ecos[5][3]] == [1.0, 0.0] and ecos[4][4] == ecos[5][4] == RES_OK,
          "delta 0: pulsa y suelta en el MISMO instante, 200 ms, en el orden del mensaje")
    inst = {t: m[0][1] for t, _, m in de(r, T_INSTANTANEA)}
    check(inst.get(200 * MS) == 0.0 and inst.get(150 * MS) == 1.0 and
          inst.get(290 * MS) == 0.0,
          "y la muestra de 200 ms ve lo ultimo, suelto; la de 150 ms, el recorte a 1")
    malo = [a for a in avs if "20 bytes" in a[3]]
    check(len(malo) == 1 and malo[0][1] == 0 and "no se aplica ninguna" in malo[0][3],
          "un T_ORDENES de 20 bytes: un T_AVISO en t = 0, y ninguna orden (\"%s\")"
          % (malo[0][3] if malo else "?"))


# ---------------------------------------------------------------------------
# O4
# ---------------------------------------------------------------------------
def o4_en_marcha(sim):
    grupo("O4 En marcha: relativas al instante en que se leen")
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "3000"], v.puerto)
    try:
        v.acepta()
        _, _, cat, _ = saludo_hasta_listo(v)
        b1, mandos, obs = pieza_de(cat, "B1")
        pulsar = mandos["pulsar"]
        suscribe(v, 10 * MS, [obs["pulsado"]])
        arranque(v, RIT_REAL)
        visto = 0                                # el ultimo instante del modelo que se ha visto
        fin_espera = time.time() + 5
        while visto < 200 * MS and time.time() < fin_espera:
            t, c = v.recibe(seg=5)
            if t == T_INSTANTANEA:
                visto = instantanea(c)[0]
        ordenes(v, [(100 * MS, b1, pulsar, 1.0), (50 * MS, b1, pulsar, 0.0),
                    (0, b1, pulsar, 1.0), (30 * MS, b1, pulsar, 0.0)])
        ecos, inst = [], []
        fin_espera = time.time() + 5
        while len(ecos) < 4 and time.time() < fin_espera:
            t, c = v.recibe(seg=5)
            if t == T_ORDEN_HECHA:
                ecos.append(hecha(c))
            elif t == T_INSTANTANEA:
                inst.append(instantanea(c))
        p.kill()
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    check(len(ecos) == 4 and all(e[4] == RES_OK for e in ecos),
          "las cuatro se aplican, con RES_OK")
    if len(ecos) != 4:
        return
    t0 = ecos[0][0]
    check(t0 >= visto + 100 * MS,
          "la primera, 100 ms despues de un instante que la ventana no puede saber: "
          "%.4f s, y nunca antes de lo ultimo que vio mas 100 ms (%.4f s)"
          % (t0 / S, (visto + 100 * MS) / S))
    check([e[0] - t0 for e in ecos] == [0, 50 * MS, 50 * MS, 80 * MS] and
          [e[3] for e in ecos] == [1.0, 0.0, 1.0, 0.0],
          "y las demas guardan sus deltas EXACTOS: +50 ms, +0 y +30 ms, en su orden")
    pul = [(t, m[0][1]) for t, _, m in inst]
    check(all(val == (1.0 if t0 <= t < t0 + 80 * MS else 0.0) for t, val in pul) and
          any(val == 1.0 for _, val in pul),
          "y las instantaneas lo ven: pulsado de %.2f a %.2f s seguido, porque la "
          "suelta y la pulsacion de +50 ms caen en el mismo instante y la muestra "
          "ve la ultima" % (t0 / S, (t0 + 80 * MS) / S))


def main():
    a = argparse.ArgumentParser(description="las ordenes de mcu-sim-gui, contra el mcu-sim de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    o1_o2(sim)
    o3_validacion(sim)
    o4_en_marcha(sim)
    return ventana.resumen("ORDENES")


if __name__ == "__main__":
    sys.exit(main())
