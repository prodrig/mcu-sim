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
#       final de su ventana, y termina con código 0;
#   M6  LA PANTALLA de `placas/nucleo_f446re_tft.xml`, una TFT de 128x160 con
#       su ST7735S que dibuja `verif/fw/tft_demo`: la imagen está en el
#       catálogo, detrás de todos los observables, y se pide en la misma
#       suscripción; llega en T_IMAGEN solo cuando cambia; blanca mientras el
#       chip duerme, con ruido al encenderla, y al final el dibujo, píxel a
#       píxel donde debe estar; el firmware lee su identificación por SDA; y
#       sin la retroiluminación, o sin alimentación, lo que se vería;
#   M7  y el CHIP, cosa a cosa, con `verif/fw/tft_prueba` en el mismo montaje:
#       MADCTL (MX, MY y MV), 18 y 12 bits por píxel, INVON, IDMON, el modo
#       parcial, el desplazamiento vertical, DISPOFF, SLPIN y SLPOUT, cada uno
#       en su imagen; las lecturas por SDA -RDDPM, RDDMADCTL, RDDCOLMOD,
#       RDDIM, RDID1-3, RDDST y RAMRD-; y las órdenes que se pierden por
#       llegar antes de 5 ms tras el reset.
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
                     M_VENTANA, RIT_LIBRE, N_AVISO, F_CORRIENDO, F_TERMINADA, T_IMAGEN, imagen)

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
            # Una placa SIN MCU -no declara ninguno-: 10 ms van con --ms=, porque
            # un segundo posicional seria un firmware y sin chip se rechaza.
            p = arranca(sim, [placa, "--ms=10"] + (["--valida"] if valida else []), v.puerto)
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


# ---------------------------------------------------------------------------
# M6
# ---------------------------------------------------------------------------
TFT = "placas/nucleo_f446re_tft.xml"


def pantalla(sim, sistema, ms=600, periodo_ms=50):
    """Las imágenes y las muestras de `encendida` de T/TFT: (cat, imagenes,
    instantaneas, rc, out)."""
    v = Ventana()
    p = arranca(sim, [sistema, "--ms=%d" % ms, "--serie", "N/VCP=memoria"], v.puerto)
    try:
        if not v.acepta():
            return None
        _, _, cat, listo = saludo_hasta_listo(v, version=2)
        pz = [x for x in ET.fromstring(cat).iter("pieza") if x.get("id") == "T/TFT"]
        if not pz or not listo:
            return None
        enc = [int(o.get("id_obs")) for o in pz[0].iter("observable")
               if o.get("nombre") == "encendida"]
        img = [int(i.get("id_obs")) for i in pz[0].iter("imagen")]
        suscribe(v, periodo_ms * 1000000, enc + img)
        arranque(v)
        ims, inst = [], []
        while True:
            t, c = v.recibe(seg=120)
            if t is None or t == T_FIN:
                break
            if t == T_IMAGEN:
                ims.append(imagen(c))
            elif t == T_INSTANTANEA:
                inst.append(instantanea(c))
        rc, out, err = termina(p)
        return cat, ims, inst, rc, out
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def pixel(im, x, y):
    """El color del punto (x, y) de la pantalla EN SU POSTURA APAISADA -la del
    dibujo, con MADCTL = 0x60-, que en la imagen de 128x160 es la fila x y la
    columna 127 - y."""
    k = (x * 128 + (127 - y)) * 3
    return tuple(im[6][k:k + 3])


def m6_pantalla(sim, cp):
    grupo("M6 La pantalla TFT: la imagen, cuando cambia, y lo que enseña")
    r = subprocess.run([sim, "placas/tft_128x160.xml", "--valida"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=60)
    out = r.stdout.decode("utf-8", "replace")
    check(r.returncode == 0 and "SIN MCU, 2 componentes, 8 nodos, 0 avisos" in out and
          "dibujo: tft_128x160.svg" in out,
          "placas/tft_128x160.xml se valida sin avisos: sus entradas las pone quien la "
          "conecte; y lleva su dibujo")
    res = pantalla(sim, TFT)
    if not check(res is not None, "%s corre con la ventana" % TFT):
        return
    cat, ims, inst, rc, out = res
    raiz = ET.fromstring(cat)
    n_obs = len(list(raiz.iter("observable")))
    imgs = [(p.get("id"), i.attrib) for p in raiz.iter("pieza") for i in p.iter("imagen")]
    check(len(imgs) == 1 and imgs[0][0] == "T/TFT" and imgs[0][1].get("nombre") == "pantalla" and
          imgs[0][1].get("ancho") == "128" and imgs[0][1].get("alto") == "160" and
          imgs[0][1].get("formato") == "rgb888" and int(imgs[0][1].get("id_obs")) == n_obs,
          "el catalogo trae la imagen de la pantalla, 128x160 en RGB888, con el id_obs "
          "detras de los %d observables: %s" % (n_obs, imgs))
    ts = [x[0] // 1000000 for x in ims]
    check(rc == 0 and len(ims) >= 4 and all(t % 50 == 0 for t in ts) and len(ims) < 12 and
          all(len(x[6]) == 128 * 160 * 3 and x[3:5] == (128, 160) for x in ims),
          "las imagenes llegan en la rejilla de 50 ms, y SOLO cuando cambian: %d de 12 "
          "(en %s ms)" % (len(ims), ts))
    blanca = ims[0][6] == b"\xff" * (128 * 160 * 3)
    check(ts[0] == 50 and blanca and abs(ims[0][5] - 1.0) < 1e-3,
          "la primera, a los 50 ms: BLANCA, con la luz entera -la media de los 50 ms: %.4f, "
          "que la placa tarda unos us en dar tension-. El chip duerme tras el reset y el "
          "cristal es de los normalmente blancos" % ims[0][5])
    ruido = [x for x in ims if len(set(x[6][k:k + 3] for k in range(0, len(x[6]), 3))) > 2000]
    check(len(ruido) >= 1,
          "al encenderla (DISPON) antes de borrarla, RUIDO: lo que tenga la memoria al dar "
          "tension")
    ult = ims[-1]
    barras = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255),
              (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    vistas = [pixel(ult, 20 * k + 10, 50) for k in range(8)]
    check(vistas == barras,
          "al final, las ocho barras de color donde las pinta el firmware, en la postura "
          "apaisada (MADCTL 0x60): %s" % vistas)
    check(pixel(ult, 150, 120) == (0, 24, 65) and pixel(ult, 0, 0) == (0, 24, 65),
          "el fondo, el azul RGB565 (0, 24, 64) del firmware, en 6 bits por color y de vuelta "
          "a 8: (0, 24, 65)")
    titulo = sum(1 for x in range(8, 92) for y in range(6, 20) if pixel(ult, x, y) == (255, 255, 255))
    check(titulo > 150, "y el titulo MCU-SIM en blanco arriba: %d pixeles" % titulo)
    enc = [m[0][1] for _, _, m in inst]
    check(enc and enc[0] == 0.0 and enc[-1] == 1.0,
          "`encendida` empieza a 0 -dormida- y acaba a 1")
    check("[N/VCP] tft_demo: ST7735S ID 7C89F0" in out,
          "el firmware lee la identificacion del chip, RDDID, por SDA moviendo los pines a "
          "mano: 7C89F0")
    check("TFT T/TFT: ensena su memoria; 16 bits por pixel, MADCTL 0x60" in out and
          "luz 26.2 mA" in out,
          "y al acabar, mcu-sim lo dice: ensena su memoria, 16 bits por pixel, MADCTL 0x60, "
          "y la luz")

    # Sin retroiluminación, y sin alimentación
    with open(TFT, encoding="utf-8") as f:
        sis = f.read().replace('fichero="nucleo_f446re.xml"',
                               'fichero="%s"' % os.path.abspath("placas/nucleo_f446re.xml"))
        sis = sis.replace('fichero="tft_128x160.xml"',
                          'fichero="%s"' % os.path.abspath("placas/tft_128x160.xml"))
        sis = sis.replace('verif/fw/tft_demo/tft_demo.bin',
                          os.path.abspath("verif/fw/tft_demo/tft_demo.bin"))
    sin_luz = cp("sin_luz.xml", sis.replace('<hilo a="T/P1.LED"   b="N/CN6.2"/>', ''))
    res = pantalla(sim, sin_luz, ms=400)
    if check(res is not None, "sin el hilo de LED corre igual"):
        _, ims, _, rc, out = res
        check(ims and all(x[5] == 0.0 for x in ims) and
              "luz 0.0 mA (a oscuras: negra)" in out,
              "y sin retroiluminacion la luz de todas las imagenes es 0 -se veria negra-, "
              "y mcu-sim lo dice")
    sin_vcc = cp("sin_vcc.xml", sis.replace('<hilo a="T/P1.VCC"   b="N/CN6.4"/>', ''))
    res = pantalla(sim, sin_vcc, ms=400)
    if check(res is not None, "sin el hilo de VCC corre igual"):
        _, ims, _, rc, out = res
        check(len(ims) == 1 and ims[0][6] == b"\xff" * (128 * 160 * 3) and
              "TFT T/TFT: sin tension" in out and "ST7735S ID 000000" in out,
              "y sin alimentacion: el chip no contesta (ID 000000) y se ve BLANCA con la "
              "luz -el cristal sin tension deja pasar la luz-, una sola imagen")


def m7_chip(sim, cp):
    grupo("M7 El ST7735S, cosa a cosa: MADCTL, formatos, modos, lecturas y el reset")
    with open(TFT, encoding="utf-8") as f:
        sis = f.read()
    for de in ("nucleo_f446re.xml", "tft_128x160.xml"):
        sis = sis.replace('fichero="%s"' % de,
                          'fichero="%s"' % os.path.abspath(os.path.join("placas", de)))
    sis = sis.replace("verif/fw/tft_demo/tft_demo.bin",
                      os.path.abspath("verif/fw/tft_prueba/tft_prueba.bin"))
    res = pantalla(sim, cp("prueba.xml", sis), ms=1600, periodo_ms=10)
    if not check(res is not None and res[3] == 0, "tft_prueba corre 1600 ms con la ventana"):
        return
    _, ims, _, rc, out = res

    def en(t_ms):
        """La imagen que se ve en t_ms: la última que llegó antes"""
        r = [x for x in ims if x[0] <= t_ms * 1000000]
        return r[-1] if r else None

    def c(im, x, y):          # la imagen como es: x columna (0-127), y fila (0-159)
        k = (y * 128 + x) * 3
        return tuple(im[6][k:k + 3])
    R, G, B, Y = (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)
    M, C, N, W = (255, 0, 255), (0, 255, 255), (0, 0, 0), (255, 255, 255)
    i = en(490)
    check(i and [c(i, 5, 5), c(i, 122, 5), c(i, 5, 155), c(i, 5, 50), c(i, 64, 80)] ==
          [R, G, B, Y, N],
          "MADCTL: el mismo bloque en la esquina logica (0, 0) cae arriba a la izquierda "
          "(00h), arriba a la derecha (MX), abajo a la izquierda (MY), y con MV las columnas "
          "40..59 son las FILAS 40..59")
    i = en(590)
    check(i and [c(i, 55, 55), c(i, 75, 55)] == [M, C],
          "COLMOD 18 bits -un byte por color- y 12 bits -dos pixeles en tres bytes-: "
          "magenta y cian")
    i = en(690)
    check(i and [c(i, 5, 5), c(i, 64, 80), c(i, 55, 55)] == [C, W, G],
          "INVON: todo invertido -el rojo, cian; el negro, blanco; el magenta, verde-")
    i = en(790)
    check(i and [c(i, 105, 105), c(i, 5, 5), c(i, 75, 55)] == [R, R, C],
          "IDMON, ocho colores: el naranja (200, 100, 30) se ve ROJO -solo el bit alto de "
          "cada color-, y los colores puros, igual")
    i = en(890)
    check(i and [c(i, 5, 5), c(i, 5, 50), c(i, 64, 30), c(i, 64, 20), c(i, 64, 39),
                 c(i, 64, 40)] == [W, W, N, N, N, W],
          "el modo parcial, filas 20 a 39: dentro, la memoria; fuera, blanco")
    i = en(990)
    check(i and [c(i, 5, 0), c(i, 5, 145), c(i, 5, 150), c(i, 5, 159)] == [N, B, R, R],
          "el desplazamiento, VSCSAD 10: la linea 0 del panel ensena la fila 10, y la "
          "roja de arriba aparece abajo, en la 150")
    blanca = b"\xff" * (128 * 160 * 3)
    i, j = en(1090), en(1190)
    check(i and j and i[6] == blanca and j[6] == blanca,
          "DISPOFF, y despues SLPIN: blanca las dos veces")
    i = en(1290)
    check(i and [c(i, 5, 5), c(i, 122, 5), c(i, 105, 105)] == [R, G, (207, 101, 24)],
          "SLPOUT: otra vez la memoria, como estaba -el naranja ya sin los ocho colores: "
          "(200, 100, 30) en RGB565, y de 6 bits a 8, (207, 101, 24)-")
    check("tft_prueba: 0A=9C 0B=00 0C=05 0D=00 DA=7C DB=89 DC=F0 09=80530400 2E=FC0000" in out,
          "las lecturas por SDA: RDDPM 9C (despierta, normal, encendida), MADCTL 00, COLMOD "
          "05, RDDIM 00, los tres ID 7C 89 F0, RDDST 80530400 y el pixel (0, 0) con RAMRD, "
          "rojo en 18 bits: FC 00 00")
    check("pad: corriente" not in out,
          "sin peleas en SDA: el firmware la suelta antes de la bajada del ultimo bit de la "
          "orden, cuando el chip empieza a contestar")
    i = en(1590)
    check(i and i[6] == blanca and
          "orden 0x11 a los 0.0" in out and "y la orden se pierde" in out and
          "TFT T/TFT: 2 ordenes perdidas por llegar antes de 5 ms tras el reset" in out and
          "TFT T/TFT: dormida (blanca)" in out,
          "un RESET y SLPOUT y DISPON sin esperar los 5 ms: el chip no los oye, lo dice, y "
          "sigue dormido: blanca")


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
    tmp = tempfile.mkdtemp(prefix="marcha_")
    try:
        def cp(nombre, texto):
            ruta = os.path.join(tmp, nombre)
            with open(ruta, "w", encoding="utf-8") as f:
                f.write(texto)
            return ruta
        m6_pantalla(sim, cp)
        m7_chip(sim, cp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return ventana.resumen("MARCHA")


if __name__ == "__main__":
    sys.exit(main())
