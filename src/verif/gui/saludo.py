#!/usr/bin/env python3
# =============================================================================
# saludo.py — El saludo con mcu-sim-gui, contra el mcu-sim de verdad
#
# Fase 3 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`).
# Este script hace de VENTANA: escucha en un puerto libre, arranca el
# `mcu-sim` de verdad con `--gui 127.0.0.1:PUERTO` y una placa de verdad
# (`placas/discovery_min.xml`), y hace el saludo con él.
#
# EL MARCO ESTÁ ESCRITO AQUÍ OTRA VEZ, en Python, y es a propósito: los dos
# extremos de verdad -mcu-sim y mcu-sim-gui- comparten `proto_io.h`, así que un
# error en ese fichero no lo cazaría ninguno de los dos. Esta implementación
# no comparte nada con aquella salvo `doc/protocolo.md`.
#
# Lo que pide el plan, y alguna cosa más:
#
#   S1  la placa y el catálogo: cada componente de la placa está en el
#       catálogo con su id y su tipo, y declara lo que su tipo declara;
#   S2  sin T_ARRANCA, mcu-sim ESPERA: sigue vivo, no gasta CPU, contesta a
#       T_PING, y cuando por fin llega T_PARA dice en su T_FIN que el tiempo
#       simulado sigue en CERO. «Para siempre sin avanzar un picosegundo»;
#   S3  con T_ARRANCA simula su ventana y termina con un T_FIN que lleva el
#       instante final. Y la simulación es LA MISMA que sin --gui: los mismos
#       LEDs y el mismo número de deltas. La conexión no la toca;
#   S4  --valida --gui: la placa y el catálogo, y T_FIN sin T_LISTO;
#   S5  los errores: nadie escuchando, una GUI que no habla ninguna versión,
#       una que cierra antes de arrancar. Código 2 y un mensaje que lo dice.
#
# La CPU se mide con /proc en Linux, y con psutil si está instalado en las
# demás; si no hay ninguna de las dos, esa comprobación se salta diciéndolo.
#
#   make -f Makefile.mcu-sim gui-saludo
#   python3 verif/gui/saludo.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import socket
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, check, grupo, claves, arranca, termina, saludo_hasta_listo,
                     fin, cpu_de, T_HOLA, T_PLACA, T_CATALOGO, T_LISTO, T_PONG, T_FIN,
                     T_VERSION, T_SUSCRIBE, T_ARRANCA, T_ORDENES, T_PARA, T_PING,
                     M_VENTANA, M_PARA, RIT_LIBRE)


# --- Las pruebas -------------------------------------------------------------
PLACA = "placas/discovery_min.xml"
FW = "verif/fw/blinky/blinky.bin"
MS = "50"


def s1_s2_espera(sim):
    grupo("S1 La placa y el catalogo de " + PLACA)
    v = Ventana()
    p = arranca(sim, [PLACA, FW, MS], v.puerto)
    try:
        if not check(v.acepta(), "mcu-sim se conecta a la ventana"):
            return
        hola, placa, cat, listo = saludo_hasta_listo(v)
        check(hola is not None and hola.get("protocolo_max") == "2" and
              hola.get("placa") == PLACA and hola.get("modo") == "simula" and
              hola.get("mcu") == "STM32F407VG" and hola.get("firmware") == FW and
              hola.get("pid") == str(p.pid),
              "T_HOLA dice la version, la placa, el MCU, el firmware, el modo y su pid")
        check(placa is not None and cat is not None and listo,
              "y tras T_VERSION llegan T_PLACA, T_CATALOGO y T_LISTO")
        if placa is None or cat is None:
            return
        comps = [(c.get("id"), c.get("tipo"))
                 for c in ET.fromstring(placa).iter("componente")]
        piezas = {pz.get("id"): pz for pz in ET.fromstring(cat).iter("pieza")}
        check(len(comps) == 11, "la placa trae sus 11 componentes")
        check(all(i in piezas and piezas[i].get("tipo") == t for i, t in comps),
              "y CADA UNO esta en el catalogo con el mismo id y el mismo tipo: la "
              "ventana puede casar los dos XML sin conocer un tipo")
        leds = [i for i, t in comps if t == "Led"]
        check(len(leds) == 4 and all(
              i in piezas and [o.get("nombre") for o in piezas[i].iter("observable")] ==
              ["encendido", "corriente"] for i in leds),
              "los cuatro LEDs declaran encendido y corriente")
        check(all(i in piezas and
                  [m.get("nombre") for m in piezas[i].iter("mando")] == ["pulsar", "rebote_ms", "rebotes"]
                  for i, t in comps if t == "Button"),
              "los pulsadores, los mandos pulsar, rebote_ms y rebotes")
        reb = {i: [m for m in piezas[i].iter("mando") if m.get("nombre") == "rebote_ms"]
               for i in ("B1", "B2") if i in piezas}
        cuantos = [m for m in piezas["B1"].iter("mando") if m.get("nombre") == "rebotes"]
        check(len(cuantos) == 1 and cuantos[0].get("tipo") == "discreto" and
              cuantos[0].get("min") == "1" and cuantos[0].get("max") == "9" and
              cuantos[0].get("valor") == "5",
              "y B1 dice cuantas veces rebota: un `discreto` de 1 a 9, que vale 5")
        check(len(reb) == 2 and all(len(x) == 1 for x in reb.values()) and
              reb["B1"][0].get("tipo") == "continuo" and reb["B1"][0].get("max") == "20" and
              reb["B1"][0].get("valor") == "2" and reb["B2"][0].get("valor") == "0" and
              all(m.get("valor") == "0" for m in piezas["B1"].iter("mando")
                  if m.get("nombre") == "pulsar"),
              "y cada mando dice lo que vale: B1 rebota 2 ms -lo de la placa por "
              "omision-, B2 no -lleva rebote=\"no\"-, y los dos estan sueltos")
        check(all(i in piezas and not list(piezas[i]) for i, t in comps if t == "Rpull"),
              "y las resistencias, nada")

        grupo("S2 Sin T_ARRANCA, mcu-sim espera: sin CPU y sin tiempo simulado")
        c0 = cpu_de(p.pid)
        time.sleep(2.0)
        c1 = cpu_de(p.pid)
        check(p.poll() is None, "dos segundos despues sigue vivo")
        if c0 is None or c1 is None:
            print("  [SALTA] no hay /proc ni psutil: la CPU no se puede medir aqui")
        else:
            check(c1 - c0 < 0.2, "y en esos dos segundos ha gastado %.3f s de CPU: "
                                 "duerme, no gira" % (c1 - c0))
        v.manda(T_SUSCRIBE, struct.pack("<IIII", 1000000, 0, 1, 0) + struct.pack("<H", 2))
        v.manda(T_PING)
        t, _ = v.recibe()
        check(t == T_PONG, "un T_SUSCRIBE se ignora (es de la fase 4), y un T_PING tiene "
                           "su T_PONG: espera, pero escucha")
        v.manda(T_PARA)
        t, cuerpo = v.recibe()
        f = fin(cuerpo)
        check(t == T_FIN and f is not None and f[0] == M_PARA and f[2] == 0,
              "a T_PARA contesta T_FIN con el tiempo simulado en CERO: no ha "
              "avanzado un picosegundo")
        rc, out, err = termina(p)
        check(rc == 0 and "simulados" not in out,
              "y termina con codigo 0 sin haber simulado nada")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def s3_arranca(sim):
    grupo("S3 Con T_ARRANCA simula su ventana, y el modelo hace lo mismo que sin --gui")
    sin = subprocess.run([sim, PLACA, FW, MS], capture_output=True, text=True, timeout=120)
    v = Ventana()
    p = arranca(sim, [PLACA, FW, MS], v.puerto)
    try:
        if not v.acepta():
            check(False, "mcu-sim se conecta")
            return
        saludo_hasta_listo(v)
        # Una suscripcion (fase 4) y una orden (fase 5): ninguna de las dos
        # puede cambiar lo que hace el modelo. La suscripcion solo lee; la
        # orden, desde la fase 5, SI se aplica -pulsa B1, la pieza 6, en
        # t = 1 ms-, pero el blinky no mira PA0, asi que los LEDs no cambian.
        v.manda(T_SUSCRIBE, struct.pack("<IIII", 1000000, 0, 1, 0) + struct.pack("<H", 2))
        v.manda(T_ORDENES, struct.pack("<QHHf", 1000000, 6, 0, 1.0))
        v.manda(T_ARRANCA, struct.pack("<IfQ", RIT_LIBRE, 1.0, 0))   # la ventana de la linea de ordenes
        # Desde la fase 4 llegan instantaneas y estados antes de T_FIN
        t, cuerpo = v.recibe(seg=120)
        while t is not None and t != T_FIN:
            t, cuerpo = v.recibe(seg=120)
        f = fin(cuerpo)
        # El instante final no es la ventana a secas: antes de ella, `run()`
        # simula el arranque electrico -10 us con todo a cero y 100 us hasta
        # soltar NRST-. Asi que son 50 ms y 110 us, y eso es lo que tiene que
        # decir: el instante REAL, no el pedido.
        esp = int(MS) * 1000000 + 110000
        check(t == T_FIN and f is not None and f[0] == M_VENTANA and f[1] == 0 and
              f[2] == esp,
              "al acabar la ventana llega T_FIN con el instante final real: %s ms "
              "mas los 110 us del arranque electrico (%s ns)"
              % (MS, f[2] if f else "?"))
        rc, out, err = termina(p)
        check(rc == 0 and "gui: la ventana dice 'arranca'" in out,
              "y termina con codigo 0")

        def simulacion(texto):
            # Lo que el MODELO dice de si mismo: como acaba cada LED, con su
            # tension y su corriente. Los deltas ya no: desde la fase 4, con
            # --gui hay un proceso mas -el que atiende la conexion cada 100 us-
            # que despierta y suma deltas sin tocar nada del modelo. Que el
            # modelo hace lo mismo lo dicen los LEDs y el instante final de
            # arriba; que las instantaneas se repiten al picosegundo, `marcha.py`.
            return [l.strip() for l in texto.splitlines() if l.strip().startswith("LED ")]
        a, b = simulacion(sin.stdout), simulacion(out)
        check(len(a) == 4 and a == b,
              "los mismos cuatro LEDs, con la misma tension y la misma corriente, que "
              "sin --gui: la ventana, lo que se suscriba y lo que ordene no tocan el "
              "modelo")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def s4_valida(sim):
    grupo("S4 --valida --gui: la placa y el catalogo, y nada mas")
    v = Ventana()
    p = arranca(sim, [PLACA, "--valida"], v.puerto)
    try:
        if not v.acepta():
            check(False, "mcu-sim se conecta")
            return
        hola, placa, cat, _ = saludo_hasta_listo(v, valida=True)
        check(hola is not None and hola.get("modo") == "valida", "T_HOLA dice modo=valida")
        # Entre el catalogo y T_FIN, el dibujo de la placa (T_ILUSTRACION): la
        # Discovery tiene el suyo, placas/discovery_min.svg
        dibujos = 0
        t, cuerpo = v.recibe()
        while t == ventana.T_ILUSTRACION:
            dibujos += 1
            t, cuerpo = v.recibe()
        f = fin(cuerpo)
        check(placa is not None and cat is not None and t == T_FIN and f is not None and
              f[2] == 0 and dibujos == 1,
              "T_PLACA, T_CATALOGO, su dibujo y T_FIN, sin T_LISTO y sin esperar a nadie")
        rc, out, err = termina(p)
        check(rc == 0, "y termina con codigo 0")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def s5_errores(sim):
    grupo("S5 Los errores: codigo 2, y dicho")
    # Nadie escuchando: un puerto que se acaba de cerrar
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    puerto = s.getsockname()[1]
    s.close()
    t0 = time.time()
    p = arranca(sim, [PLACA, MS], puerto)
    rc, out, err = termina(p)
    check(rc == 2 and "no hay nadie escuchando" in err and time.time() - t0 < 15,
          "sin ventana escuchando: codigo 2 enseguida, y lo dice")

    v = Ventana()
    p = arranca(sim, [PLACA, MS], v.puerto)
    try:
        v.acepta()
        v.recibe()
        v.manda(T_VERSION, b"protocolo=0\n")
        rc, out, err = termina(p)
        check(rc == 2 and "ninguna version" in err,
              "una ventana que no habla ninguna version: codigo 2, y lo dice")
    finally:
        v.cierra()

    v = Ventana()
    p = arranca(sim, [PLACA, MS], v.puerto)
    try:
        v.acepta()
        saludo_hasta_listo(v)
        v.c.close()
        v.c = None
        rc, out, err = termina(p)
        check(rc == 2 and "cerro" in err and "simulados" not in out,
              "una ventana que se cierra antes de arrancar: codigo 2, sin simular")
    finally:
        v.cierra()


def main():
    a = argparse.ArgumentParser(description="El saludo con mcu-sim-gui, contra el mcu-sim de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    s1_s2_espera(sim)
    s3_arranca(sim)
    s4_valida(sim)
    s5_errores(sim)
    return ventana.resumen("SALUDO")


if __name__ == "__main__":
    sys.exit(main())
