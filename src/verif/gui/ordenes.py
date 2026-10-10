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
#   O5  EL BOTON DE RESET, como en la placa: B2 de `placas/discovery_min.xml`
#       y de `placas/nucleo_f446re.xml`, sobre NRST. Pulsado, el chip se queda
#       en reset -el LED del blinky se apaga al momento, y sigue apagado- y al soltarlo
#       arranca otra vez desde la Flash: el blinky empieza de nuevo, con su
#       primer flanco a la misma distancia del arranque que la primera vez.
#   O6  EL KY-040 (`placas/ky040.xml`): su encoder y su pulsador, con un banco
#       de pruebas por hilos -3,3 V en VCC, masa, un pull-up en SW y un LED que
#       luce con cada línea a cero-. Un clic a la derecha baja CLK y, 1 ms
#       después, DT; la vuelta entera es el código Gray de dos bits -un solo
#       cambio cada vez-, primero CLK a la derecha y primero DT a la izquierda;
#       en cada clic las dos líneas iguales; `posicion` da la vuelta -de 0 a
#       29 por la izquierda-; y apretar el eje baja SW.
#   O7  EL SERVO CON SU ENCODER (`placas/nucleo_f446re_servo.xml`): la Nucleo,
#       el servo, el KY-040 y la pantalla de pie, con `verif/fw/servo_demo`.
#       El servo empieza en un ángulo al azar -otro en cada simulación- y el
#       firmware lo lleva al centro; girar el encoder lo mueve 5 grados por
#       clic, hasta el tope; apretar el eje lo centra; y sujetarlo -el mando
#       `bloquear`- lo para donde esté, gasta lo de un servo bloqueado -más
#       de lo que da el USB de la Nucleo, que lo avisa- y al soltarlo sigue.
#       La pantalla, con MADCTL 0x00, enseña la aguja donde debe.
#
#   make -f Makefile.mcu-sim gui-ordenes
#   python3 verif/gui/ordenes.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import shutil
import struct
import sys
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, RIT_REAL, check, grupo, arranca, termina, saludo_hasta_listo, fin,
                     suscribe, instantanea, aviso, ordenes, hecha, imagen, T_IMAGEN,
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


# ---------------------------------------------------------------------------
# O5
# ---------------------------------------------------------------------------
def flancos_con_reset(sim, placa, fw, led, pulsa_ms, suelta_ms, total_ms=900):
    """Los flancos del LED [(ms, valor)] con B2 pulsado de `pulsa_ms` a
    `suelta_ms`, o sin tocarlo si pulsa_ms es None."""
    v = Ventana()
    p = arranca(sim, [placa, fw, str(total_ms)], v.puerto)
    try:
        if not v.acepta():
            return None
        _, _, cat, listo = saludo_hasta_listo(v)
        b2, mandos, _ = pieza_de(cat, "B2")
        _, _, obs = pieza_de(cat, led)
        if b2 is None or not listo:
            return None
        suscribe(v, 10 * MS, [obs["encendido"]])
        if pulsa_ms is not None:
            ordenes(v, [(pulsa_ms * MS, b2, mandos["pulsar"], 1.0),
                        ((suelta_ms - pulsa_ms) * MS, b2, mandos["pulsar"], 0.0)])
        arranque(v)
        r, _ = hasta_fin(v)
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    out, prev = [], None
    for t, (ts, _, m) in [(t, x) for t, x in r if t == T_INSTANTANEA]:
        if m[0][1] != prev:
            out.append((ts // MS, m[0][1]))
            prev = m[0][1]
    return out


def o5_reset(sim):
    grupo("O5 El boton de RESET (B2): el chip se queda en reset y arranca al soltarlo")
    for placa, fw, led in (("placas/discovery_min.xml", FW, "LD4"),
                           ("placas/nucleo_f446re.xml", "verif/fw/blinky446/blinky446.bin",
                            "LD2")):
        libre = flancos_con_reset(sim, placa, fw, led, None, None)
        # Se pulsa con el LED ENCENDIDO -de 210 a 310 ms-, para que se vea
        # apagarse en cuanto el chip entra en reset
        con = flancos_con_reset(sim, placa, fw, led, 250, 550)
        if not check(libre and con, "%s: dos ejecuciones" % placa):
            continue
        primero = libre[0][0]              # del arranque al primer flanco
        durante = [x for x in con if 250 <= x[0] <= 550]
        despues = [x for x in con if x[0] > 550]
        check(con[:3] == libre[:3] and durante and all(val == 0.0 for _, val in durante) and
              durante[0][0] <= 260,
              "%s: hasta pulsar, lo mismo que sin tocarlo; pulsado, el LED apagado: el "
              "chip esta en reset (%s)" % (placa, con))
        check(despues and despues[0][1] == 1.0 and
              abs(despues[0][0] - (550 + primero)) <= 10 and
              [b - a for (a, _), (b, _) in zip(despues, despues[1:])] ==
              [b - a for (a, _), (b, _) in zip(libre, libre[1:])][:len(despues) - 1],
              "%s: al soltarlo arranca otra vez: el primer flanco a %d ms del arranque, "
              "como la primera vez, y el mismo parpadeo" % (placa, primero))


# ---------------------------------------------------------------------------
# O6
# ---------------------------------------------------------------------------
# El banco del KY-040: lo que pondria quien lo monta -la VCC, la masa y un
# pull-up en SW, que el modulo no lleva- y un LED en cada linea, con el anodo
# por dentro a 3,3 V: luce cuando la linea esta a cero. Con 1 Mohm de serie no
# carga nada.
BANCO_KY040 = """<placa nombre="banco-ky040">
  <componente tipo="Conector" id="J" filas="5" columnas="1" nombres="CLK DT SW VCC GND"/>
  <nodo id="J.CLK" bus="si"/>
  <nodo id="J.DT" bus="si"/>
  <nodo id="J.SW" bus="si"/>
  <componente tipo="Fuente" id="V33" v="3.3"><pin nombre="pin" nodo="J.VCC"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="J.GND"/></componente>
  <componente tipo="Rpull" id="RSW" v="3.3" r="10000"><pin nombre="a" nodo="J.SW"/></componente>
  <componente tipo="Led" id="LCLK" a_vss="no" vdd="3.3" vf="1.0" r="1000000">
    <pin nombre="catodo" nodo="J.CLK"/></componente>
  <componente tipo="Led" id="LDT" a_vss="no" vdd="3.3" vf="1.0" r="1000000">
    <pin nombre="catodo" nodo="J.DT"/></componente>
  <componente tipo="Led" id="LSW" a_vss="no" vdd="3.3" vf="1.0" r="1000000">
    <pin nombre="catodo" nodo="J.SW"/></componente>
</placa>
"""
SISTEMA_KY040 = """<sistema nombre="prueba-ky040">
  <placa id="K" fichero="%s"/>
  <placa id="T" fichero="banco_ky040.xml"/>
  <hilo a="K/P1.CLK" b="T/J.CLK"/>
  <hilo a="K/P1.DT"  b="T/J.DT"/>
  <hilo a="K/P1.SW"  b="T/J.SW"/>
  <hilo a="K/P1.VCC" b="T/J.VCC"/>
  <hilo a="K/P1.GND" b="T/J.GND"/>
</sistema>
"""


def ky040_con_ordenes(sim, sis, lista, total_ms=150):
    """Las muestras del KY-040, cada 0,5 ms: [(ms, CLK, DT, SW, posicion,
    contacto_a, contacto_b)], con los NIVELES de las lineas (1 alto, 0 bajo),
    tras las `lista` de (ms, pieza, mando, valor) absolutos."""
    v = Ventana()
    p = arranca(sim, [sis, "--ms=%d" % total_ms], v.puerto)
    try:
        if not v.acepta():
            return None
        _, _, cat, listo = saludo_hasta_listo(v)
        enc, me, oe = pieza_de(cat, "K/ENC")
        sw, ms_, _ = pieza_de(cat, "K/SW1")
        lineas = [pieza_de(cat, "T/" + x)[2].get("encendido") for x in ("LCLK", "LDT", "LSW")]
        if enc is None or sw is None or None in lineas or not listo:
            return None
        ids = lineas + [oe["posicion"], oe["contacto_a"], oe["contacto_b"]]
        suscribe(v, MS // 2, ids)
        mando = {"girar": (enc, me["girar"]), "pulsar": (sw, ms_["pulsar"])}
        t, ords = 0, []
        for ms, que, val in lista:
            ords.append((ms * MS - t, mando[que][0], mando[que][1], float(val)))
            t = ms * MS
        ordenes(v, ords)
        arranque(v)
        r, _ = hasta_fin(v)
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    out = []
    for ts, _, m in de(r, T_INSTANTANEA):
        x = dict(m)
        out.append((ts / MS,) + tuple(1 - int(x[i]) for i in lineas) +
                   tuple(int(x[i]) for i in ids[3:]))
    return out


def o6_ky040(sim):
    grupo("O6 El KY-040: el encoder en codigo Gray, y el pulsador del eje")
    r = subprocess.run([sim, "placas/ky040.xml", "--valida"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=60)
    out = r.stdout.decode("utf-8", "replace")
    check(r.returncode == 0 and "placa 'ky040': SIN MCU, 5 componentes, 5 nodos, 0 avisos" in out,
          "placas/ky040.xml se valida: el conector, el encoder, sus dos pull-ups y el "
          "pulsador, sin un aviso")
    tmp = tempfile.mkdtemp(prefix="ky040_")
    try:
        with open(os.path.join(tmp, "banco_ky040.xml"), "w", encoding="utf-8") as f:
            f.write(BANCO_KY040)
        sis = os.path.join(tmp, "sistema.xml")
        with open(sis, "w", encoding="utf-8") as f:
            f.write(SISTEMA_KY040 % os.path.abspath("placas/ky040.xml"))
        r = subprocess.run([sim, sis, "--valida"], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=60)
        out = r.stdout.decode("utf-8", "replace")
        check(r.returncode == 0 and "SIN MCU, 12 componentes, 5 nodos, 0 avisos" in out and
              "dibujo K: ky040.svg" in out,
              "con su banco por hilos, tambien; y lleva su dibujo")
        # Un clic a la derecha; dos mas; cuatro a la izquierda -pasando por el
        # 0, hasta la -1, que es la 29-; y el eje apretado de 120 a 130 ms
        m = ky040_con_ordenes(sim, sis, [(10, "girar", 1), (20, "girar", 3), (40, "girar", -1),
                                         (120, "pulsar", 1), (130, "pulsar", 0)])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if not check(m and len(m) == 300, "el sistema con el banco corre 150 ms, con una muestra "
                 "cada 0,5 ms"):
        return
    en = {x[0]: x[1:] for x in m}

    def en_t(ms):
        return en[ms]
    check(all(en_t(t)[:3] == (1, 1, 1) for t in (0.5, 5.0, 9.5)),
          "en reposo, CLK, DT y SW altos: los pull-ups de la placa -R2 y R3, colgados de la "
          "VCC del conector- y el del banco")
    check(en_t(10.5)[:2] == (0, 1) and en_t(11.5)[:2] == (0, 0) and en_t(11.5)[3] == 1,
          "un clic a la derecha: CLK baja primero, y DT 1 ms despues; la posicion, 1 "
          "(%s, %s)" % (en_t(10.5), en_t(11.5)))
    # La secuencia de estados de (CLK, DT), sin repetidos
    seq = []
    for x in m:
        if not seq or seq[-1] != x[1:3]:
            seq.append(x[1:3])
    gray = all(abs(a[0] - b[0]) + abs(a[1] - b[1]) == 1 for a, b in zip(seq, seq[1:]))
    check(gray and len(seq) == 15,
          "toda la vuelta en codigo Gray: catorce cambios, y en cada uno cambia UNA linea "
          "(%s)" % " ".join("%d%d" % s for s in seq))
    # Tres clics a la derecha, de 11 a 00, y cuatro a la izquierda, de 00 a 00
    der = [(1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0)]
    izq = [(0, 1), (1, 1), (1, 0), (0, 0), (0, 1), (1, 1), (1, 0), (0, 0)]
    check(seq[:7] == der and seq[7:] == izq,
          "a la derecha, CLK DT = 11, 01, 00, 10, 11... -CLK va delante-; a la izquierda, "
          "al reves: 00, 01, 11, 10, 00... -DT va delante-")
    clics = [en_t(t) for t in (9.5, 15.0, 25.0, 30.0, 35.0, 45.0, 50.0, 55.0, 60.0, 70.0)]
    check(all(c[0] == c[1] and c[0] == (0 if c[3] % 2 else 1) for c in clics),
          "en cada clic, las dos lineas iguales: altas en las posiciones pares, bajas en "
          "las impares")
    check([en_t(t)[3] for t in (15.0, 30.0, 45.0, 50.0, 55.0, 60.0, 70.0)] ==
          [1, 3, 2, 1, 0, 29, 29],
          "la posicion: 1, 3, y a la izquierda 2, 1, 0 y 29 -da la vuelta-")
    check(all(en_t(t)[4:] == ((1, 1) if en_t(t)[3] % 2 else (0, 0))
              for t in (15.0, 30.0, 45.0, 70.0)),
          "y los contactos que deja ver el encoder, cerrados en las impares")
    check(en_t(119.5)[2] == 1 and all(en_t(t)[2] == 0 for t in (120.5, 125.0, 129.5)) and
          en_t(140.0)[2] == 1 and all(en_t(t)[:2] == en_t(119.5)[:2] for t in (120.5, 140.0)),
          "apretar el eje baja SW, y soltarlo lo sube -con su rebote-, sin tocar CLK ni DT")


# ---------------------------------------------------------------------------
# O7
# ---------------------------------------------------------------------------
SERVO = "placas/nucleo_f446re_servo.xml"


def o7_servo(sim):
    grupo("O7 El servo con su encoder y su pantalla: girar, centrar y sujetar el eje")
    r = subprocess.run([sim, SERVO, "--valida"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=60)
    out = r.stdout.decode("utf-8", "replace")
    check(r.returncode == 0 and "4 placas: N (nucleo-f446re), S (servo-sg90), K (ky040), "
          "T (tft-128x160)" in out and "1 MCU(s), 20 componentes, 170 nodos, 0 avisos" in out and
          "dibujo T: tft_128x160.svg (6 kB, girado 90)" in out,
          "%s se valida sin avisos: la Nucleo, el servo, el encoder y la pantalla, de pie" % SERVO)
    # Donde empieza: al azar, otro cada vez
    inis = []
    for _ in range(3):
        r = subprocess.run([sim, SERVO, "--ms=1", "--serie", "N/VCP=memoria"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        o = r.stdout.decode("utf-8", "replace")
        k = o.find("empezo en ")
        if k >= 0:
            inis.append(float(o[k + 10:].split()[0]))
    check(len(inis) == 3 and all(-90 <= x <= 90 for x in inis) and len(set(inis)) >= 2,
          "el eje empieza DONDE SE QUEDO: un angulo al azar entre -90 y +90, otro en cada "
          "simulacion (%s)" % inis)

    v = Ventana()
    p = arranca(sim, [SERVO, "--ms=1400", "--serie", "N/VCP=memoria"], v.puerto)
    try:
        if not check(v.acepta(), "con la ventana"):
            return
        _, _, cat, listo = saludo_hasta_listo(v, version=2)
        enc, me, _ = pieza_de(cat, "K/ENC")
        sw, ms_, _ = pieza_de(cat, "K/SW1")
        sv, msv, osv = pieza_de(cat, "S/SERVO")
        _, _, ousb = pieza_de(cat, "N/USB")
        img = [int(i.get("id_obs")) for pz in ET.fromstring(cat).iter("pieza")
               if pz.get("id") == "T/TFT" for i in pz.iter("imagen")]
        if not check(listo and None not in (enc, sw, sv) and len(img) == 1 and
                     "bloquear" in msv and "corriente" in ousb,
                     "en el catalogo, el encoder, su pulsador, el servo con `bloquear`, el USB "
                     "de la Nucleo y la imagen de la pantalla"):
            return
        ids = [osv["angulo"], osv["pulso"], osv["corriente"], ousb["corriente"],
               ousb["sobrecorriente"]]
        suscribe(v, MS, ids + img)
        lista = [(400, enc, me["girar"], 3), (600, enc, me["girar"], -2),
                 (800, sw, ms_["pulsar"], 1), (850, sw, ms_["pulsar"], 0),
                 (900, enc, me["girar"], 30),
                 (1000, sv, msv["bloquear"], 1), (1100, sv, msv["bloquear"], 0)]
        t, ords = 0, []
        for ms, pz_, m, val in lista:
            ords.append((ms * MS - t, pz_, m, float(val)))
            t = ms * MS
        ordenes(v, ords)
        arranque(v)
        inst, ims = [], []
        while True:
            tt, c = v.recibe(seg=120)
            if tt is None or tt == T_FIN:
                break
            if tt == T_INSTANTANEA:
                inst.append(instantanea(c))
            elif tt == T_IMAGEN:
                ims.append(imagen(c))
        rc, out, err = termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    if not check(rc == 0 and len(inst) == 1400, "corre 1400 ms con una muestra cada ms"):
        return
    en = {}
    for ts, _, m in inst:
        x = dict(m)
        en[round(ts / MS)] = tuple(x[i] for i in ids)

    def ang(ms):
        return en[ms][0]
    ini = ang(1)
    check(("empezo en %+.1f grados" % ini) in out and abs(ang(390)) < 1e-4 and
          "[N/VCP] servo_demo: angulo 0, pulso 1500 us" in out,
          "al arrancar, el servo esta en %.1f grados, y el firmware lo lleva al centro" % ini)

    def grados(us):
        return (us - 1000) * 0.18 - 90
    check(abs(ang(590) - grados(1583)) < 1e-3 and abs(en[590][1] - 1583) < 1 and
          "angulo +15, pulso 1583 us" in out,
          "tres clics a la derecha: +15 grados, un pulso de 1583 us (%.2f grados)" % ang(590))
    check(abs(ang(790) - grados(1445)) < 1e-3 and "angulo -10, pulso 1445 us" in out,
          "cinco a la izquierda, hasta la cuenta -2: -10 grados (%.2f)" % ang(790))
    check(abs(ang(890)) < 1e-4 and out.count("servo_demo: angulo 0, pulso 1500 us") >= 2,
          "apretar el eje lo lleva al centro")
    bloq = ang(1001)
    check(0 < bloq < 90 and all(ang(t) == bloq for t in range(1001, 1100, 10)) and
          en[1050][2] > 400 and en[1050][4] == 1 and en[990][2] < 200,
          "veintisiete clics a la derecha lo mandan al tope, +90; sujeto a los 1000 ms, se "
          "queda en %.1f grados y pide lo de un servo bloqueado: el USB de la Nucleo se "
          "limita a sus 500 mA (%.0f mA) y lo avisa" % (bloq, en[1050][2]))
    check(abs(ang(1390) - 90) < 1e-4 and en[1390][4] == 0 and en[1390][2] < 10 and
          "angulo +90, pulso 2000 us" in out and "N/USB: sobrecorriente" in out,
          "al soltarlo sigue hasta +90, y vuelve a gastar lo de quieto")
    ult = ims[-1] if ims else None

    def px(x, y):
        k = (y * 128 + x) * 3
        return tuple(ult[6][k:k + 3])
    naranja = ult and px(94, 100)
    check(ult is not None and naranja[0] > 240 and 120 < naranja[1] < 140 and naranja[2] < 10 and
          px(64, 70) == (0, 24, 65) and px(64, 4) == (0, 24, 65),
          "la pantalla, de pie con MADCTL 0x00: la aguja naranja apunta a la derecha, a +90 "
          "(%s), y donde estuvo en el 0 ya solo hay fondo" % (naranja,))


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
    o5_reset(sim)
    o6_ky040(sim)
    o7_servo(sim)
    return ventana.resumen("ORDENES")


if __name__ == "__main__":
    sys.exit(main())
