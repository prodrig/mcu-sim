#!/usr/bin/env python3
# =============================================================================
# sistema.py — Conectores, y varias placas enchufadas entre si (<sistema>)
#
# Un `Conector` es una pieza que no conduce: dice que unos nodos salen de la
# placa. Un <sistema> junta varias placas -cada una de su fichero, o escrita
# dentro- y dice como se enchufan: <acopla> dos conectores pin a pin (o en
# espejo) y <hilo> dos nodos sueltos. Todo lo de la placa A se llama A/...
# Este script lo prueba contra el `mcu-sim` de verdad, por la consola y, como
# los otros de esta carpeta, haciendo de VENTANA (`ventana.py`).
#
#   C1  un conector en una placa: un pin soldado a un pad es ese pad, uno al
#       aire es un nodo propio, y los errores de su declaracion;
#   C2  placas/nucleo_y_shield.xml: la Nucleo y un shield sin MCU, sus cuatro
#       conectores Arduino acoplados, y el blinky de la Nucleo encendiendo a
#       la vez su LD2 y el LED del shield, que esta en OTRA placa;
#   C3  espejo y cruce: dos conectores de 2x3 cara a cara, uno de 1x4, y un
#       hilo que cruza dos pines;
#   C4  dos placas con un MCU cada una: dos u0 que no se pisan, un hilo de
#       pad a pad entre chips, y la linea de ordenes, que ya no dice a cual;
#   C5  T_PLACA en la version 2 del protocolo -un <sistema>, con sus placas
#       y sus acoples- y en la 1 -lo mismo con la raiz <placa>-;
#   C6  los errores del sistema, cada uno con lo que hay que hacer;
#   C7  una PILA (placas/pila_pc104.xml): un acople de tres conectores, el
#       mismo pin en las tres placas, y sus errores;
#   C8  lo que T_PLACA cuenta de cada placa para poder DIBUJAR el sistema:
#       sus piezas, sus chips, sus conectores con su forma, y que placas une
#       cada acople y cada hilo;
#   C9  VDD, VSS, NRST y BOOT0 entre chips: un reset sujeto en una placa
#       para tambien el chip de la otra si sus NRST estan unidos.
#   C10 el DIBUJO de cada placa: el SVG que se llama como ella o el que dice
#       `ilustracion=`, la tabla de enlaces, el que pone el montaje; uno por
#       fichero en T_ILUSTRACION, con sus placas; y lo que falla, que es un
#       aviso -un fichero que no esta, uno que no es SVG, uno enorme- o un
#       error -una tabla que nombra una pieza que no hay-.
#   C11 la barra de 8 LEDs (placas/barra8_*.xml): LEDs con LAS DOS PATILLAS a
#       la vista y el comun saliendo por el conector, a una fuente, a masa,
#       al aire o al pin de otra placa (placas/barra8_en_nucleo.xml).
#   C12 los pines de conector con NOMBRE (`nombres="COM D1 ..."`) y sus
#       errores; los morpho CN7 y CN10 de la Nucleo, cada pin a su pad; y
#       placas/nucleo_f446re_barra8ac_azul.xml, la barra azul por los morpho.
#
#   make -f Makefile.mcu-sim gui-sistema
#   python3 verif/gui/sistema.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Codigo de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, check, grupo, arranca, termina, saludo_hasta_listo,
                     fin, suscribe, instantanea, aviso, resto_del_saludo, T_INSTANTANEA,
                     T_AVISO, T_FIN, M_VENTANA)

MS = 1000000
SRC = os.getcwd()


def corre(sim, args, seg=60):
    p = subprocess.run([sim] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=seg, cwd=SRC)
    return (p.returncode, p.stdout.decode("utf-8", "replace"),
            p.stderr.decode("utf-8", "replace"))


class Carpeta:
    """Una carpeta temporal con ficheros XML escritos a mano."""

    def __init__(self):
        self.d = tempfile.mkdtemp(prefix="sistema")

    def escribe(self, nombre, texto):
        ruta = os.path.join(self.d, nombre)
        with open(ruta, "w", encoding="utf-8") as f:
            f.write(texto)
        return ruta

    def borra(self):
        shutil.rmtree(self.d, ignore_errors=True)


def led(out, id_):
    """'encendido', 'apagado' o None, del informe final de un LED."""
    for l in out.splitlines():
        l = l.strip()
        if l.startswith("LED %s en " % id_) or l.startswith("LED %s entre " % id_):
            return "encendido" if ": encendido" in l else "apagado"
    return None


def medida(out, id_):
    """Lo que va entre parentesis en el informe de un LED: '3.30 V, 0.75 mA'"""
    for l in out.splitlines():
        l = l.strip()
        if l.startswith("LED %s " % id_) and "(" in l:
            return l[l.rindex("(") + 1:l.rindex(")")]
    return None


def nodo_de_led(out, id_):
    for l in out.splitlines():
        l = l.strip()
        if l.startswith("LED %s en " % id_):
            return l[len("LED %s en " % id_):].split(":")[0]
    return None


# ---------------------------------------------------------------------------
# C1
# ---------------------------------------------------------------------------
def c1_conector(sim, cp):
    grupo("C1 Un conector en una placa")
    rc, out, err = corre(sim, ["placas/nucleo_f446re.xml", "--valida"])
    check(rc == 0 and "9 componentes, 170 nodos, 0 avisos" in out,
          "la Nucleo con sus cuatro conectores Arduino y sus dos morpho valida sin un aviso: "
          "154 nodos del chip y 16 pines de conector al aire -4 de los Arduino (+5V, VIN, "
          "AREF y un NC) y 12 de los morpho (E5V, +5V, VIN, U5V y los NC)-, ninguno flotante")
    placa = cp.escribe("c1.xml", """<placa nombre="c1">
  <mcu tipo="STM32F407VG" id="u0"/>
  <componente tipo="Conector" id="CN1" filas="1" columnas="4">
    <pin nombre="1" nodo="PD12"/>
  </componente>
  <componente tipo="Led" id="LA" a_vss="si"><pin nombre="anodo" nodo="CN1.1"/></componente>
  <componente tipo="Fuente" id="F"><pin nombre="pin" nodo="CN1.2"/></componente>
  <componente tipo="Led" id="LB" a_vss="si"><pin nombre="anodo" nodo="CN1.2"/></componente>
</placa>
""")
    rc, out, err = corre(sim, [placa, "--ms=1"])
    check(rc == 0 and nodo_de_led(out, "LA") == "PD12",
          "un LED en CN1.1, que esta soldado a PD12, ES un LED en PD12")
    check(nodo_de_led(out, "LB") == "CN1.2" and led(out, "LB") == "encendido",
          "uno en CN1.2, al aire, es un nodo propio: con la Fuente en el, luce")
    malas = [
        ('<componente tipo="Conector" id="J" filas="1"/>', "falta columnas", "sin columnas"),
        ('<componente tipo="Conector" id="J" columnas="3" numeracion="raro"/>',
         "numeracion", "numeracion que no es zigzag ni filas"),
        ('<componente tipo="Conector" id="J" columnas="3"><pin nombre="4" nodo="PA1"/>'
         '</componente>', "del 1 al 3", "un pin 4 en un conector de 3"),
        ('<componente tipo="Conector" id="P1" columnas="3"/>', "seria el nombre de un pad",
         "un conector llamado P1, cuyo pin 1 seria PB1"),
    ]
    for xml, dice, que in malas:
        f = cp.escribe("mal.xml", '<placa nombre="m"><mcu tipo="STM32F407VG" id="u0"/>%s</placa>'
                       % xml)
        rc, out, err = corre(sim, [f, "--valida"])
        check(rc != 0 and dice in err, "%s: error que lo dice" % que)


# ---------------------------------------------------------------------------
# C2
# ---------------------------------------------------------------------------
def c2_nucleo_y_shield(sim):
    grupo("C2 Una Nucleo con un shield enchufado (placas/nucleo_y_shield.xml)")
    rc, out, err = corre(sim, ["placas/shield_leds.xml", "--valida"])
    check(rc == 0 and "SIN MCU" in out, "el shield solo, sin chip, valida")
    rc, out, err = corre(sim, ["placas/nucleo_y_shield.xml", "--valida"])
    check(rc == 0 and "sistema 'nucleo-y-shield': 2 placas: N (nucleo-f446re), "
          "S (shield-leds)" in out and out.count("[acopla]") == 4 and "0 avisos" in out,
          "el sistema valida: dos placas, cuatro acoples y ni un aviso")
    check("mcu N/u0: verif/fw/blinky446/blinky446.bin" in out,
          "el chip se llama N/u0, y lleva el firmware que dice el <sistema>")
    # El blinky446 conmuta PA5 cada 100 ms. Se mira en una ventana de tiempo
    # en la que esta encendido y en otra en que no.
    estados = {}
    for ms in (250, 350):
        rc, out, err = corre(sim, ["placas/nucleo_y_shield.xml", "--ms=%d" % ms])
        estados[ms] = (led(out, "N/LD2"), led(out, "S/LD_D13"), nodo_de_led(out, "S/LD_D13"))
    check(estados[250][:2] == ("encendido", "encendido") and
          estados[350][:2] == ("apagado", "apagado") and estados[250][2] == "N/u0.PA5",
          "el blinky enciende y apaga A LA VEZ el LD2 de la Nucleo y el LED del shield: el "
          "D13 del shield es N/u0.PA5 (%s)" % estados)
    check(led(out, "S/LD_PWR") == "encendido" and nodo_de_led(out, "S/LD_PWR") == "N/u0.VDD",
          "y el LED de alimentacion del shield luce con el VDD de la Nucleo: su J6.4 es el "
          "+3V3, soldado a N/u0.VDD")


# ---------------------------------------------------------------------------
# C3
# ---------------------------------------------------------------------------
SIS_ESPEJO = """<sistema nombre="espejo">
  <placa id="A" nombre="fuentes">
    <componente tipo="Conector" id="J" filas="2" columnas="3"/>
    <componente tipo="Fuente" id="F"><pin nombre="pin" nodo="J.1"/></componente>
    <componente tipo="Conector" id="K" filas="1" columnas="4"/>
    <componente tipo="Fuente" id="G"><pin nombre="pin" nodo="K.1"/></componente>
    <componente tipo="Conector" id="H" filas="1" columnas="4"/>
    <componente tipo="Fuente" id="E"><pin nombre="pin" nodo="H.3"/></componente>
  </placa>
  <placa id="B" nombre="leds">
    <componente tipo="Conector" id="J" filas="2" columnas="3"/>
    <componente tipo="Led" id="J1" a_vss="si"><pin nombre="anodo" nodo="J.1"/></componente>
    <componente tipo="Led" id="J2" a_vss="si"><pin nombre="anodo" nodo="J.2"/></componente>
    <componente tipo="Conector" id="K" filas="1" columnas="4"/>
    <componente tipo="Led" id="K1" a_vss="si"><pin nombre="anodo" nodo="K.1"/></componente>
    <componente tipo="Led" id="K4" a_vss="si"><pin nombre="anodo" nodo="K.4"/></componente>
    <componente tipo="Conector" id="H" filas="1" columnas="4"/>
    <componente tipo="Led" id="H5" a_vss="si"><pin nombre="anodo" nodo="H.4"/></componente>
  </placa>
  <acopla a="A/J" b="B/J"%s/>
  <acopla a="A/K" b="B/K"%s/>
  <hilo a="A/H.3" b="B/H.4"/>
</sistema>
"""


def c3_espejo(sim, cp):
    grupo("C3 Pin a pin, en espejo, y un hilo que cruza")
    f = cp.escribe("espejo_no.xml", SIS_ESPEJO % ("", ""))
    rc, out, err = corre(sim, [f, "--ms=1"])
    check(rc == 0 and led(out, "B/J1") == "encendido" and led(out, "B/J2") == "apagado" and
          led(out, "B/K1") == "encendido" and led(out, "B/K4") == "apagado",
          "pin a pin: la Fuente del 1 de A enciende el LED del 1 de B, en el 2x3 y en el 1x4")
    f = cp.escribe("espejo_si.xml", SIS_ESPEJO % (' espejo="si"', ' espejo="si"'))
    rc, out, err = corre(sim, [f, "--ms=1"])
    check(rc == 0 and led(out, "B/J1") == "apagado" and led(out, "B/J2") == "encendido",
          "en espejo, un 2x3 en zigzag da la vuelta a las filas: el 1 cae sobre el 2")
    check(led(out, "B/K1") == "apagado" and led(out, "B/K4") == "encendido",
          "y uno de una sola fila, a las columnas: el 1 cae sobre el 4")
    check(led(out, "B/H5") == "encendido" and nodo_de_led(out, "B/H5") == "A/H.3",
          "un <hilo> une el 3 de A con el 4 de B, que no estan enfrente")


# ---------------------------------------------------------------------------
# C4
# ---------------------------------------------------------------------------
def c4_dos_mcus(sim, cp):
    grupo("C4 Dos placas con un MCU cada una")
    nucleo = os.path.join(SRC, "placas", "nucleo_f446re.xml")
    f = cp.escribe("dos.xml", """<sistema nombre="dos-nucleo">
  <placa id="A" fichero="%s"/>
  <placa id="B" fichero="%s"/>
  <hilo a="A/CN9.2" b="B/CN9.1"/>
  <hilo a="A/PA3" b="B/PA2"/>
  <mcu ref="A/u0" puerto_gdb="3401"/>
</sistema>
""" % (nucleo, nucleo))
    rc, out, err = corre(sim, [f, "--valida"])
    check(rc == 0 and "2 MCU(s)" in out and "mcu A/u0" in out and "mcu B/u0" in out and
          "0 avisos" in out,
          "la misma placa dos veces: A/u0 y B/u0, sin pisarse, y la UART cruzada con dos "
          "hilos -uno por conector, otro de pad a pad, con PA3 resuelto en SU placa-")
    check("gdb por pines en el puerto 3401" in out,
          "el <sistema> le pone puerto de GDB al chip de una placa sin tocar su fichero")
    rc, out, err = corre(sim, [f, "verif/fw/blinky446/blinky446.bin", "--valida"])
    check(rc != 0 and "no dicen a cual" in err and '<mcu ref="A/u0"' in err,
          "un firmware suelto ya no dice a cual: el error dice como escribirlo en el sistema")
    rc, out, err = corre(sim, ["placas/nucleo_y_shield.xml", "--valida", "--mcu",
                               "STM32F446RC"])
    check(rc == 0 and "N/u0: --mcu STM32F446RC sustituye al STM32F446RE" in out,
          "con un solo chip en todo el sistema, --mcu sigue mandando sobre su tipo")


# ---------------------------------------------------------------------------
# C5
# ---------------------------------------------------------------------------
def saludo_valida(sim, version):
    v = Ventana()
    p = arranca(sim, ["placas/nucleo_y_shield.xml", "--valida"], v.puerto)
    try:
        if not v.acepta():
            return None, None, None
        hola, placa, cat, _ = saludo_hasta_listo(v, valida=True, version=version)
        termina(p)
        return hola, placa, cat
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def c5_t_placa(sim):
    grupo("C5 T_PLACA: un <sistema> en la version 2, una <placa> en la 1")
    hola, placa, cat = saludo_valida(sim, 2)
    if not check(placa is not None, "saludo en la version 2"):
        return
    r = ET.fromstring(placa)
    pl = [(x.get("id"), x.get("nombre"), x.get("fichero")) for x in r.findall("placa")]
    check(r.tag == "sistema" and r.get("nombre") == "nucleo-y-shield" and
          pl == [("N", "nucleo-f446re", "nucleo_f446re.xml"), ("S", "shield-leds",
                                                              "shield_leds.xml")],
          "la raiz es <sistema>, con una <placa id nombre fichero> por placa")
    comps = {c.get("id"): c for c in r.findall("componente")}
    d13 = comps.get("S/LD_D13")
    check(d13 is not None and d13.find("pin").get("nodo") == "N/u0.PA5" and
          [m.get("id") for m in r.findall("mcu")] == ["N/u0"],
          "todo con su nombre cualificado: el LED del shield, en N/u0.PA5; el chip, N/u0")
    j5 = comps.get("S/J5")
    check(j5 is not None and len(j5.findall("pin")) == 10 and
          j5.findall("pin")[5].get("nodo") == "N/u0.PA5" and
          j5.findall("pin")[6].get("nodo") == "N/u0.VSS" and
          j5.findall("pin")[7].get("nodo") == "N/CN5.8",
          "los conectores, ya resueltos: J5.6 es N/u0.PA5, J5.7 -GND- la masa del chip, "
          "N/u0.VSS, y J5.8 -AREF, al aire en las dos- N/CN5.8")
    check([(a.get("a"), a.get("b")) for a in r.findall("acopla")] ==
          [("N/CN5", "S/J5"), ("N/CN6", "S/J6"), ("N/CN8", "S/J8"), ("N/CN9", "S/J9")],
          "y al final los cuatro <acopla>, para que la ventana sepa que se enchufo con que")
    ids_cat = [p.get("id") for p in ET.fromstring(cat).iter("pieza")]
    check("S/LD_D13" in ids_cat and "N/LD2" in ids_cat and "N/CN5" in ids_cat,
          "el catalogo usa los mismos nombres")
    check(hola.get("mcu") == "STM32F446RE" and
          hola.get("firmware") == "verif/fw/blinky446/blinky446.bin",
          "T_HOLA dice el tipo del chip y su firmware, el que puso el <sistema>")
    hola1, placa1, cat1 = saludo_valida(sim, 1)
    if not check(placa1 is not None, "saludo en la version 1"):
        return
    r1 = ET.fromstring(placa1)
    check(r1.tag == "placa" and r1.get("nombre") == "nucleo-y-shield" and
          not r1.findall("placa") and not r1.findall("acopla") and
          len(r1.findall("componente")) == len(r.findall("componente")),
          "en la 1, la raiz de siempre, <placa>, con las mismas piezas y sin lo que solo "
          "tiene un sistema: una ventana vieja lo pinta, sin agrupar")


# ---------------------------------------------------------------------------
# C6
# ---------------------------------------------------------------------------
def c6_errores(sim, cp):
    grupo("C6 Los errores de un sistema")
    cp.escribe("shield.xml", open(os.path.join(SRC, "placas", "shield_leds.xml"),
                                  encoding="utf-8").read())
    cp.escribe("nucleo.xml", open(os.path.join(SRC, "placas", "nucleo_f446re.xml"),
                                  encoding="utf-8").read())
    casos = [
        ('<placa id="A/B" fichero="shield.xml"/>', "el id de una placa", "un id con barra"),
        ('<placa id="A" fichero="shield.xml"/><placa id="A" fichero="shield.xml"/>',
         "id repetido", "dos placas con el mismo id"),
        ('<placa id="A" fichero="no_existe.xml"/>', "no se puede abrir",
         "un fichero que no esta"),
        ('<placa id="A" fichero="shield.xml"/><placa id="B" fichero="shield.xml"/>'
         '<acopla a="A/LD_D13" b="B/J5"/>', "no un Conector",
         "acoplar algo que no es un conector"),
        ('<placa id="A" fichero="shield.xml"/><placa id="B" fichero="shield.xml"/>'
         '<acopla a="A/J5" b="B/J6"/>', "no tienen los mismos pines",
         "conectores de 10 y de 8 pines"),
        ('<placa id="A" fichero="shield.xml"/><placa id="B" fichero="shield.xml"/>'
         '<placa id="C" fichero="shield.xml"/>'
         '<acopla a="A/J5" b="B/J5"/><acopla a="A/J5" b="C/J5"/>', "ya esta en otro acople",
         "un conector enchufado a dos"),
        ('<placa id="A" fichero="shield.xml"/><placa id="B" fichero="shield.xml"/>'
         '<acopla a="A/J5" b="X/J5"/>', "no hay ninguna placa 'X'",
         "una placa que no esta en el sistema"),
        ('<placa id="A" fichero="shield.xml"/><placa id="B" fichero="shield.xml"/>'
         '<hilo a="A/J5.1" b="B/no_hay"/>', "no es ningun nodo",
         "un hilo a un nodo que no existe"),
        ('<placa id="A" fichero="shield.xml"/><placa id="B" fichero="shield.xml"/>'
         '<hilo a="A/J5.1" b="B/PA5"/>', "la placa B no lleva ninguno",
         "un hilo a un pin de MCU de una placa sin MCU"),
        ('<placa id="A" fichero="nucleo.xml"/><mcu ref="A/u7" firmware="x.bin"/>',
         "no hay ningun MCU", "<mcu ref> a un chip que no esta"),
        ('<placa id="A" nombre="a"><componente tipo="Led" id="L"><pin nombre="anodo" '
         'nodo="B/x"/></componente></placa>', "lleva '/'",
         "una placa que nombra un nodo de otra: eso solo se dice en el sistema"),
        ('<placa id="A" fichero="shield.xml"/><cable a="x" b="y"/>',
         "elemento desconocido dentro de <sistema>", "un elemento que no existe"),
    ]
    for cuerpo, dice, que in casos:
        f = cp.escribe("mal_sis.xml", '<sistema nombre="mal">%s</sistema>' % cuerpo)
        rc, out, err = corre(sim, [f, "--valida"])
        check(rc != 0 and dice in (out + err), "%s: error que lo dice%s"
              % (que, "" if dice in (out + err) else " (dijo: %s)" % (err.strip()[-160:])))
    f = cp.escribe("sis_en_sis.xml", '<sistema nombre="x"><placa id="A" fichero="mal_sis.xml"/>'
                   '</sistema>')
    cp.escribe("mal_sis.xml", '<sistema nombre="y"><placa id="Z" fichero="shield.xml"/></sistema>')
    rc, out, err = corre(sim, [f, "--valida"])
    check(rc != 0 and "un sistema dentro de otro" in err, "un sistema dentro de otro, todavia no")
    f = cp.escribe("sin_mcu_sis.xml", '<sistema nombre="s"><placa id="A" fichero="shield.xml"/>'
                   '</sistema>')
    rc, out, err = corre(sim, [f, "--valida", "--mcu", "STM32F446RE"])
    check(rc != 0 and "--mcu no pone un MCU" in err,
          "--mcu en un sistema sin chips: no se sabe en que placa iria")


# ---------------------------------------------------------------------------
# C7
# ---------------------------------------------------------------------------
def c7_pila(sim, cp):
    grupo("C7 Una pila PC/104: un acople de tres")
    rc, out, err = corre(sim, ["placas/pila_pc104.xml", "--valida"])
    check(rc == 0 and "[acopla] CPU/J1, L1/J1 y L2/J1, en pila" in out and
          "0 avisos" in out,
          "la pila valida: un acople de tres conectores, y ni un aviso")
    est = {}
    for ms in (150, 250):
        rc, out, err = corre(sim, ["placas/pila_pc104.xml", "--ms=%d" % ms])
        est[ms] = (led(out, "L1/LD1"), led(out, "L2/LD1"), nodo_de_led(out, "L2/LD1"))
    check(est[150][:2] == ("apagado", "apagado") and est[250][:2] == ("encendido", "encendido")
          and est[250][2] == "CPU/u0.PD12",
          "el pin 1 es el mismo hilo en las tres placas: el blinky de la CPU enciende y "
          "apaga a la vez el LD1 de los dos modulos, que son el mismo fichero (%s)" % est)
    cp.escribe("cpu.xml", open(os.path.join(SRC, "placas", "pc104_cpu.xml"),
                               encoding="utf-8").read())
    cp.escribe("leds.xml", open(os.path.join(SRC, "placas", "pc104_leds.xml"),
                                encoding="utf-8").read())
    cp.escribe("corto.xml", '<placa nombre="corto"><componente tipo="Conector" id="J1" '
                            'filas="2" columnas="20"/></placa>')
    placas = ('<placa id="A" fichero="cpu.xml"/><placa id="B" fichero="leds.xml"/>'
              '<placa id="C" fichero="leds.xml"/><placa id="D" fichero="corto.xml"/>')
    casos = [
        ('<acopla conectores="A/J1 B/J1 C/J1" espejo="si"/>', "dos y solo dos",
         "una pila en espejo"),
        ('<acopla conectores="A/J1 B/J1 D/J1"/>', "no tienen los mismos pines",
         "un conector de 40 en una pila de 64"),
        ('<acopla conectores="A/J1"/>', "al menos dos", "una pila de uno"),
        ('<acopla conectores="A/J1 B/J1 A/J1"/>', "aparece dos veces",
         "el mismo conector dos veces en la pila"),
        ('<acopla a="A/J1" b="B/J1" conectores="A/J1 B/J1"/>', "las dos cosas no",
         "a= y b= junto con conectores="),
        ('<acopla a="A/J1" b="B/J1"/><acopla a="B/J1" b="C/J1"/>', "pila PC/104",
         "la pila hecha de acoples de dos: el error dice como escribirla"),
    ]
    for cuerpo, dice, que in casos:
        f = cp.escribe("mal_pila.xml", '<sistema nombre="mal">%s%s</sistema>' % (placas, cuerpo))
        rc, out, err = corre(sim, [f, "--valida"])
        check(rc != 0 and dice in (out + err), "%s: error que lo dice%s"
              % (que, "" if dice in (out + err) else " (dijo: %s)" % (err.strip()[-160:])))


# ---------------------------------------------------------------------------
# C8
# ---------------------------------------------------------------------------
def c8_dibujable(sim):
    grupo("C8 Lo que T_PLACA cuenta de cada placa, para poder dibujarla")
    v = Ventana()
    p = arranca(sim, ["placas/pila_pc104.xml", "--valida"], v.puerto)
    try:
        v.acepta()
        _, placa, _, _ = saludo_hasta_listo(v, valida=True, version=2)
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    if not check(placa is not None, "saludo en la version 2"):
        return
    r = ET.fromstring(placa)
    pls = {x.get("id"): x for x in r.findall("placa")}
    check(list(pls) == ["CPU", "L1", "L2"] and pls["CPU"].get("piezas") == "2" and
          pls["L1"].get("piezas") == "3" and pls["L1"].get("fichero") == "pc104_leds.xml",
          "una <placa> por placa, en orden, con cuantas piezas lleva cada una")
    m = pls["CPU"].findall("mcu")
    check(len(m) == 1 and m[0].get("ref") == "CPU/u0" and m[0].get("tipo") == "STM32F407VG" and
          not pls["L1"].findall("mcu"),
          "con sus chips: la CPU lleva CPU/u0, un STM32F407VG; los modulos, ninguno")
    cs = [(c.get("ref"), c.get("filas"), c.get("columnas"), c.get("numeracion"),
           c.get("acople")) for p_ in pls.values() for c in p_.findall("conector")]
    check(cs == [("CPU/J1", "2", "32", "zigzag", "0"), ("L1/J1", "2", "32", "zigzag", "0"),
                 ("L2/J1", "2", "32", "zigzag", "0")],
          "y sus conectores con su forma -2x32, en zigzag- y el acople en el que estan")
    a = r.findall("acopla")
    check(len(a) == 1 and a[0].get("n") == "0" and
          a[0].get("conectores") == "CPU/J1 L1/J1 L2/J1" and
          a[0].get("placas") == "CPU L1 L2" and a[0].get("a") is None,
          "el acople dice sus tres conectores y las tres placas que une; sin a= ni b=, "
          "que solo tienen sentido con dos")
    v = Ventana()
    p = arranca(sim, ["placas/nucleo_y_shield.xml", "--valida"], v.puerto)
    try:
        v.acepta()
        _, placa, _, _ = saludo_hasta_listo(v, valida=True, version=2)
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    r = ET.fromstring(placa) if placa else None
    a = r.findall("acopla") if r is not None else []
    check(len(a) == 4 and a[0].get("a") == "N/CN5" and a[0].get("b") == "S/J5" and
          a[0].get("conectores") == "N/CN5 S/J5" and a[0].get("placas") == "N S",
          "uno de dos lleva ademas a= y b=, como lo lee una ventana de la primera "
          "version de los sistemas")


# ---------------------------------------------------------------------------
# C9
# ---------------------------------------------------------------------------
SIS_RESET = """<sistema nombre="reset">
  <placa id="A" nombre="a">
    <mcu tipo="STM32F407VG" id="u0"/>
    <nodo id="PH0"/>
    <componente tipo="Crystal" id="X" vdd="3.3"><pin nombre="osc_in" nodo="PH0"/></componente>
    <componente tipo="Led" id="L" a_vss="si"><pin nombre="anodo" nodo="PD12"/></componente>
    <componente tipo="Button" id="R" normalmente="cerrado" rebote="no">
      <pin nombre="pin" nodo="NRST"/>
    </componente>
  </placa>
  <placa id="B" nombre="b">
    <mcu tipo="STM32F407VG" id="u0"/>
    <nodo id="PH0"/>
    <componente tipo="Crystal" id="X" vdd="3.3"><pin nombre="osc_in" nodo="PH0"/></componente>
    <componente tipo="Led" id="L" a_vss="si"><pin nombre="anodo" nodo="PD12"/></componente>
    <componente tipo="Conector" id="J" filas="1" columnas="2">
      <pin nombre="1" nodo="VSS"/><pin nombre="2" nodo="BOOT0"/>
    </componente>
  </placa>
  %s
  <mcu ref="A/u0" firmware="verif/fw/blinky/blinky.bin"/>
  <mcu ref="B/u0" firmware="verif/fw/blinky/blinky.bin"/>
</sistema>
"""


def c9_alimentacion(sim, cp):
    grupo("C9 VDD, masa, NRST y BOOT0 entre chips")
    f = cp.escribe("reset0.xml", SIS_RESET % "")
    rc, out, err = corre(sim, [f, "--ms=250"])
    check(rc == 0 and led(out, "A/L") == "apagado" and led(out, "B/L") == "encendido" and
          "308 nodos" in out,
          "sin unir nada, el pulsador NC de A sujeta el reset de A, y B arranca y "
          "enciende su LED")
    f = cp.escribe("reset1.xml", SIS_RESET % '<hilo a="A/NRST" b="B/NRST"/>')
    rc, out, err = corre(sim, [f, "--ms=250"])
    check(rc == 0 and led(out, "A/L") == "apagado" and led(out, "B/L") == "apagado" and
          "307 nodos" in out and "0 avisos" in out,
          "con los NRST unidos por un hilo, el reset de A para tambien a B: un nodo menos, "
          "y ni un aviso")
    f = cp.escribe("reset2.xml", SIS_RESET % (
        '<hilo a="A/VDD" b="B/VDD"/><hilo a="A/VSS" b="B/J.1"/>'
        '<hilo a="A/BOOT0" b="B/J.2"/>'))
    rc, out, err = corre(sim, [f, "--ms=250"])
    check(rc == 0 and led(out, "B/L") == "encendido" and "305 nodos" in out and
          "0 avisos" in out,
          "VDD, VSS -por un pin de conector- y BOOT0 de los dos chips unidos: tres nodos "
          "menos, y B arranca igual")
    f = cp.escribe("une_nrst.xml", """<placa nombre="dos">
  <mcu tipo="STM32F407VG" id="u0"/><mcu tipo="STM32F407VG" id="u1"/>
  <nodo id="rst" une="u0.NRST u1.NRST"/>
  <componente tipo="Button" id="R" rebote="no"><pin nombre="pin" nodo="rst"/></componente>
</placa>
""")
    rc, out, err = corre(sim, [f, "--valida"])
    check(rc == 0 and "0 avisos" in out,
          "y en una sola placa, un <nodo une> con los NRST de dos chips y un pulsador de "
          "reset para los dos")


# ---------------------------------------------------------------------------
# C10
# ---------------------------------------------------------------------------
def saludo_con_dibujos(sim, args, version=2):
    """(rc, salida, placa, ilustraciones, avisos) de un --valida con ventana"""
    v = Ventana()
    p = arranca(sim, args + ["--valida"], v.puerto)
    try:
        if not v.acepta():
            return None, "", None, [], []
        _, placa, _, _ = saludo_hasta_listo(v, valida=True, version=version)
        resto_del_saludo(v)
        rc, out, err = termina(p)
        return rc, out + err, placa, v.ilustraciones, v.avisos_placa
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


MODULO = """<placa nombre="modulo"%s>
  %s
  <nodo id="a"/>
  <componente tipo="Led" id="LD" a_vss="si"><pin nombre="anodo" nodo="a"/></componente>
  <componente tipo="Fuente" id="F" v="3.3"><pin nombre="pin" nodo="a"/></componente>
</placa>
"""
SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><circle id="%s" cx="5" cy="5" r="2"/></svg>'


def c10_dibujos(sim, cp):
    grupo("C10 El dibujo de cada placa: T_ILUSTRACION")
    rc, out, placa, il, av = saludo_con_dibujos(sim, ["placas/nucleo_y_shield.xml"])
    check(rc == 0 and len(il) == 1 and il[0][0].get("placas") == "N" and
          il[0][0].get("fichero") == "nucleo_f446re.svg" and b'id="LD2"' in il[0][1] and
          il[0][1].lstrip().startswith(b"<?xml"),
          "la Nucleo del sistema lleva su dibujo sin decirlo -se llama como ella-: un "
          "T_ILUSTRACION, para la placa N, con el SVG tal cual")
    check("dibujo N: nucleo_f446re.svg (49 kB)" in out and "dibujo S: ninguno" in out,
          "y la consola dice el de cada placa: N el suyo, S ninguno")
    r = ET.fromstring(placa) if placa else None
    check(r is not None and all(x.get("ilustracion") is None for x in r.findall("placa")),
          "en T_PLACA, nada: el dibujo que se encuentra sin decirlo no es de la placa")
    rc, out, placa, il, av = saludo_con_dibujos(sim, ["placas/nucleo_f446re.xml"], 1)
    check(rc == 0 and len(il) == 1 and il[0][0].get("placas") == "" and
          "dibujo: nucleo_f446re.svg" in out,
          "una placa suelta, tambien en la version 1: placas= vacio quiere decir ella")
    rc, out, placa, il, av = saludo_con_dibujos(sim, ["placas/pila_pc104.xml"])
    check(rc == 0 and il == [] and "dibujo CPU: ninguno" in out,
          "sin dibujos, ningun T_ILUSTRACION")

    cp.escribe("dib.svg", SVG % "led")
    cp.escribe("otro.svg", SVG % "LD")
    cp.escribe("mod.xml", MODULO % (' ilustracion="dib.svg"',
                                    '<ilustracion><enlace pieza="LD" elemento="led" '
                                    'efecto="brillo"/></ilustracion>'))
    f = cp.escribe("pila.xml", """<sistema nombre="dos-modulos">
  <placa id="M1" fichero="mod.xml"/>
  <placa id="M2" fichero="mod.xml"/>
  <placa id="M3" fichero="mod.xml" ilustracion="otro.svg"/>
</sistema>
""")
    rc, out, placa, il, av = saludo_con_dibujos(sim, [f])
    pl = {i[0].get("fichero"): i[0].get("placas") for i in il}
    check(rc == 0 and len(il) == 2 and pl == {"dib.svg": "M1 M2", "otro.svg": "M3"},
          "uno por FICHERO, con las placas que lo usan: dib.svg para M1 y M2, una vez; y "
          "otro.svg para M3, que es el que pone el montaje")
    check("dibujo M2: dib.svg, el mismo que M1" in out, "y la consola lo dice")
    r = ET.fromstring(placa) if placa else None
    pls = {x.get("id"): x for x in r.findall("placa")} if r is not None else {}
    e1 = pls["M1"].findall("ilustracion/enlace") if "M1" in pls else []
    check(pls.get("M1") is not None and pls["M1"].get("ilustracion") == "dib.svg" and
          [(e.get("pieza"), e.get("elemento"), e.get("efecto")) for e in e1] ==
          [("LD", "led", "brillo")],
          "T_PLACA dice el dibujo declarado de cada placa y su tabla de enlaces, con el "
          "nombre de la pieza en SU placa")
    check(pls.get("M3") is not None and pls["M3"].get("ilustracion") == "otro.svg" and
          not pls["M3"].findall("ilustracion"),
          "el del montaje manda, y sin la tabla de la placa, que era de su dibujo")
    rc, out, placa, il, av = saludo_con_dibujos(sim, [cp.d + "/mod.xml"], 1)
    r = ET.fromstring(placa) if placa else None
    check(r is not None and r.get("ilustracion") == "dib.svg" and
          [e.get("elemento") for e in r.findall("ilustracion/enlace")] == ["led"],
          "una placa suelta: en su raiz, como se escribe en el fichero")

    # Lo que falla: avisos, y la simulacion sigue
    cp.escribe("falta.xml", MODULO % (' ilustracion="no_esta.svg"', ""))
    cp.escribe("texto.svg", "esto no es un dibujo\n")
    cp.escribe("texto.xml", MODULO % (' ilustracion="texto.svg"', ""))
    cp.escribe("gordo.svg", "<svg>" + " " * (2 * 1024 * 1024) + "</svg>")
    cp.escribe("gordo.xml", MODULO % (' ilustracion="gordo.svg"', ""))
    for fich, que, texto in (("falta.xml", "no se encuentra su dibujo", "un fichero que no esta"),
                             ("texto.xml", "no parece un SVG", "uno que no es SVG"),
                             ("gordo.xml", "y el maximo es 2048", "uno de mas de 2 MiB")):
        rc, out, placa, il, av = saludo_con_dibujos(sim, [os.path.join(cp.d, fich)])
        check(rc == 0 and il == [] and any(que in a[3] for a in av) and "[dibujo]" in out,
              "%s: un aviso -en la consola y a la ventana- y no se manda" % texto)
    # Y los errores de la placa: no se monta
    for txt, que, texto in (
            (MODULO % (' ilustarcion="x.svg"', ""), "atributo desconocido: ilustarcion",
             "un atributo de la raiz mal escrito ya no se ignora"),
            (MODULO % ("", '<ilustracion><enlace pieza="LD9" elemento="x"/></ilustracion>'),
             "no tiene ninguna pieza 'LD9'", "una tabla que nombra una pieza que no hay"),
            (MODULO % ("", '<ilustracion><enlace pieza="LD" elemento="x" efecto="luz"/>'
                       '</ilustracion>'), "efecto 'luz' desconocido", "un efecto que no existe"),
            (MODULO % ("", '<ilustracion><enlace pieza="LD" elemento="x"/>'
                       '<enlace pieza="LD" elemento="y"/></ilustracion>'),
             "ya esta en la tabla", "una pieza dos veces")):
        f = cp.escribe("mal.xml", txt)
        rc, out, err = corre(sim, [f, "--valida"])
        check(rc != 0 and que in err, "%s: \"%s\"" % (texto, que))


# ---------------------------------------------------------------------------
# C11
# ---------------------------------------------------------------------------
# La barra, con una placa de pruebas sin MCU enfrente, acoplada a su P1 -de
# nueve pines, por numero: el 1 de J cae en P1.COM y el k+1 en P1.Dk-: lo que
# se pone en cada pin lo dicen las piezas de P
SIS_BARRA = """<sistema nombre="banco-barra">
  <placa id="B" fichero="%s"/>
  <placa id="P">
    <componente tipo="Conector" id="J" filas="1" columnas="9"/>
    %s
  </placa>
  <acopla a="B/P1" b="P/J"/>
</sistema>
"""
COM = 1                         # el pin de J que cae en P1.COM


def D(k):
    """El pin de J que cae en P1.Dk"""
    return k + 1


def fuente(pin, v=3.3):
    return ('<componente tipo="Fuente" id="F%d" v="%g"><pin nombre="pin" nodo="J.%d"/>'
            '</componente>' % (pin, v, pin))


def masa(pin):
    return '<componente tipo="Gnd" id="G%d"><pin nombre="pin" nodo="J.%d"/></componente>' % (
        pin, pin)


def c11_barra(sim, cp):
    grupo("C11 Una barra de 8 LEDs con el comun en el conector")
    bien = True
    for comun in ("anodo", "catodo"):
        for col in ("rojo", "azul"):
            rc, out, err = corre(sim, ["placas/barra8_%s_comun_%s.xml" % (comun, col),
                                       "--valida"])
            bien = bien and rc == 0 and \
                "SIN MCU, 9 componentes, 9 nodos, 0 avisos" in out and \
                ("dibujo: barra8_%s.svg" % col) in out
    check(bien, "las cuatro barras -anodo o catodo comun, roja o azul- validan sin un aviso: "
          "el conector y ocho LEDs, nueve nodos, y el dibujo de su color")

    def banco(barra, piezas):
        f = cp.escribe("banco.xml", SIS_BARRA % (os.path.join(SRC, "placas", barra),
                                                 "\n    ".join(piezas)))
        return corre(sim, [f, "--ms=1"])

    rc, out, err = banco("barra8_anodo_comun_rojo.xml",
                         [fuente(COM), masa(D(1)), fuente(D(2)), masa(D(3))])
    check(rc == 0 and led(out, "B/D1") == "encendido" and
          medida(out, "B/D1") == "3.30 V, 0.75 mA" and led(out, "B/D3") == "encendido",
          "anodo comun a 3,3 V: el LED con su pin a masa luce, con (3,3 - 1,8) / 2 kohm = "
          "0,75 mA: \"%s\"" % medida(out, "B/D1"))
    check(led(out, "B/D2") == "apagado" and medida(out, "B/D2") == "0.00 V, 0.00 mA",
          "el que tiene su pin tambien a 3,3 V no: no hay tension entre sus patillas")
    check(led(out, "B/D4") == "apagado" and medida(out, "B/D4") == "un extremo al aire" and
          "LED B/D1 entre B/P1.COM y B/P1.D1" in out,
          "ni el de un pin al aire, y se dice; el informe dice entre que nodos esta cada uno")
    rc, out, err = banco("barra8_anodo_comun_rojo.xml", [masa(D(1)), masa(D(3))])
    check(rc == 0 and all(led(out, "B/D%d" % k) == "apagado" for k in range(1, 9)) and
          medida(out, "B/D1") == "un extremo al aire",
          "con el comun al aire no luce ninguno: no va por dentro ni a VDD ni a masa")
    rc, out, err = banco("barra8_catodo_comun_azul.xml",
                         [masa(COM), fuente(D(1)), fuente(D(3), 5.0), masa(D(2))])
    check(rc == 0 and medida(out, "B/D1") == "3.30 V, 0.15 mA" and
          medida(out, "B/D3") == "5.00 V, 1.00 mA" and led(out, "B/D2") == "apagado",
          "catodo comun a masa, azul: con 3,3 V en su pin pasan 0,15 mA y con 5 V, 1 mA; "
          "el de un pin a masa no luce")
    rc, out, err = banco("barra8_catodo_comun_azul.xml", [fuente(COM), masa(D(1))])
    check(rc == 0 and led(out, "B/D1") == "apagado" and
          medida(out, "B/D1") == "-3.30 V, 0.00 mA",
          "y al reves -el comun alto y el pin a masa- es un diodo en inversa, y no conduce")

    f = cp.escribe("led_mal.xml", """<placa nombre="mal">
  <nodo id="a" externo="si"/><nodo id="k" externo="si"/>
  <componente tipo="Fuente" id="F"><pin nombre="pin" nodo="a"/></componente>
  <componente tipo="Led" id="L" a_vss="no">
    <pin nombre="anodo" nodo="a"/><pin nombre="catodo" nodo="k"/>
  </componente>
</placa>
""")
    rc, out, err = corre(sim, [f, "--valida"])
    todo = out + err                     # SystemC lo dice por la salida estandar
    check(rc != 0 and "L: con 'anodo' y 'catodo' en sus nodos" in todo and "'a_vss'" in todo,
          "con las dos patillas, a_vss no tiene sentido, y escribirlo es un error que lo dice")

    rc, out, err = corre(sim, ["placas/barra8_en_nucleo.xml", "--valida"])
    check(rc == 0 and "1 MCU(s), 18 componentes, 170 nodos, 0 avisos" in out and
          "dibujo B: barra8_rojo.svg" in out,
          "placas/barra8_en_nucleo.xml: la barra cableada a la Nucleo con nueve hilos, el "
          "comun a D10; ni un aviso")

    def encendidos(out):
        return [k for k in range(1, 9) if led(out, "B/D%d" % k) == "encendido"]

    def corre_ms(fichero, lista):
        vistos = []
        for ms in lista:
            rc, out, err = corre(sim, [fichero, "--ms=%d" % ms])
            vistos.append(encendidos(out) if rc == 0 else None)
        return vistos

    vistos = corre_ms("placas/barra8_en_nucleo.xml", (25, 75, 375, 425, 475))
    check(vistos == [[1], [2], [8], [], []],
          "con el anodo comun en un pin a 1, la luz corre de D1 a D8 cada 50 ms; y con el "
          "pin a 0, la barra se apaga entera: %s" % vistos)
    rc, out, err = corre(sim, ["placas/barra8_en_nucleo.xml", "--ms=75"])
    check("LED B/D2 entre N/u0.PB6 y N/u0.PA9: encendido" in out,
          "el comun es el PB6 de la Nucleo y el D2, su PA9: los hilos unen los nodos de las "
          "dos placas")
    with open(os.path.join(SRC, "placas", "barra8_en_nucleo.xml"), encoding="utf-8") as fx:
        sis = fx.read()
    for de, a in (("nucleo_f446re.xml", "nucleo_f446re.xml"),
                  ("barra8_anodo_comun_rojo.xml", "barra8_catodo_comun_azul.xml")):
        sis = sis.replace('fichero="%s"' % de,
                          'fichero="%s"' % os.path.join(SRC, "placas", a))
    vistos = corre_ms(cp.escribe("barra_catodo.xml", sis), (75, 425, 475, 775))
    check(vistos == [[], [1], [2], [8]],
          "con la de catodo comun, el mismo firmware: apagada mientras el comun esta a 1, "
          "y la luz corre cuando pasa a 0: %s" % vistos)


# ---------------------------------------------------------------------------
# C12
# ---------------------------------------------------------------------------
def c12_nombres_y_morpho(sim, cp):
    grupo("C12 Pines de conector con nombre, y los morpho de la Nucleo")
    f = cp.escribe("nombres.xml", """<placa nombre="nombres">
  <mcu tipo="STM32F407VG" id="u0"/>
  <componente tipo="Conector" id="P1" filas="1" columnas="4" nombres="COM D1 D2 D3">
    <pin nombre="COM" nodo="PD12"/>
  </componente>
  <componente tipo="Conector" id="J" filas="1" columnas="3" nombres="- X -">
    <pin nombre="3" nodo="PD13"/>
  </componente>
  <componente tipo="Fuente" id="F"><pin nombre="pin" nodo="P1.D1"/></componente>
  <componente tipo="Led" id="LC" a_vss="si"><pin nombre="anodo" nodo="P1.COM"/></componente>
  <componente tipo="Led" id="L1" a_vss="si"><pin nombre="anodo" nodo="P1.D1"/></componente>
  <componente tipo="Led" id="LJ" a_vss="si"><pin nombre="anodo" nodo="J.3"/></componente>
  <componente tipo="Led" id="LX" a_vss="si"><pin nombre="anodo" nodo="J.X"/></componente>
  <componente tipo="Led" id="LB" a_vss="si"><pin nombre="anodo" nodo="P1.1"/></componente>
</placa>
""")
    rc, out, err = corre(sim, [f, "--ms=1"])
    check(rc == 0 and nodo_de_led(out, "LC") == "PD12",
          "nombres=\"COM D1 D2 D3\": el pin 1 se suelda por su nombre (COM, a PD12), y un "
          "LED en P1.COM esta en PD12")
    check(nodo_de_led(out, "LJ") == "PD13" and nodo_de_led(out, "LX") == "J.X",
          "con nombres=\"- X -\", el 3 se sigue llamando por su numero (J.3, a PD13) y el "
          "2 por su nombre (J.X)")
    check(nodo_de_led(out, "L1") == "P1.D1" and led(out, "L1") == "encendido",
          "uno al aire se llama por su nombre, P1.D1, y es un nodo propio: la Fuente lo "
          "enciende")
    check(nodo_de_led(out, "LB") == "PB1",
          "y P1.1 NO es el pin 1 de P1, que se llama COM: es el pad PB1, como siempre")

    malas = [
        ('nombres="A B C"', "hay 3 y el conector tiene 4", "tres nombres para cuatro pines"),
        ('nombres="A B A C"', "'A' esta dos veces", "un nombre repetido"),
        ('nombres="A 2B C D"', "'2B' no vale", "un nombre que empieza por cifra"),
        ('nombres="A VDD C D"', "seria la patilla VDD de un MCU 'X'",
         "un nombre que haria de X.VDD la alimentacion de un chip"),
        ('nombres="A PA0 C D"', "seria el nombre de un pad",
         "uno que haria de X.PA0 un pad de un chip X"),
    ]
    f = cp.escribe("mal.xml", '<placa nombre="m"><mcu tipo="STM32F407VG" id="u0"/>'
                   '<componente tipo="Conector" id="P1" columnas="3" nombres="A - C"/></placa>')
    rc, out, err = corre(sim, [f, "--valida"])
    check(rc != 0 and "'P1.2' seria el nombre de un pad (PB2)" in err,
          "un P1 con un pin sin nombre: P1.2 seria PB2, y se dice")
    for atr, dice, que in malas:
        f = cp.escribe("mal.xml", '<placa nombre="m"><mcu tipo="STM32F407VG" id="u0"/>'
                       '<componente tipo="Conector" id="X" columnas="4" %s/></placa>' % atr)
        rc, out, err = corre(sim, [f, "--valida"])
        check(rc != 0 and dice in err, "%s: error que lo dice" % que)
    f = cp.escribe("mal.xml", '<placa nombre="m"><mcu tipo="STM32F407VG" id="u0"/>'
                   '<componente tipo="Conector" id="X" columnas="2" nombres="A B">'
                   '<pin nombre="Z" nodo="PA1"/></componente></placa>')
    rc, out, err = corre(sim, [f, "--valida"])
    check(rc != 0 and "el pin 'Z' no existe" in err and "o por su nombre: A B" in err,
          "un pin que no es ni un numero ni un nombre: el error dice cuales hay")

    # Los morpho: cada pin a su pad, y los de alimentacion a la suya
    nucleo = os.path.join(SRC, "placas", "nucleo_f446re.xml")
    f = cp.escribe("morpho.xml", """<sistema nombre="morpho">
  <placa id="N" fichero="%s"/>
  <placa id="P">
    <componente tipo="Conector" id="J" filas="1" columnas="6"/>
    <componente tipo="Led" id="L1" a_vss="si"><pin nombre="anodo" nodo="J.1"/></componente>
    <componente tipo="Led" id="L2" a_vss="si"><pin nombre="anodo" nodo="J.2"/></componente>
    <componente tipo="Led" id="L3" a_vss="si"><pin nombre="anodo" nodo="J.3"/></componente>
    <componente tipo="Led" id="L4" a_vss="si"><pin nombre="anodo" nodo="J.4"/></componente>
    <componente tipo="Led" id="L5" a_vss="si"><pin nombre="anodo" nodo="J.5"/></componente>
    <componente tipo="Led" id="L6" a_vss="si"><pin nombre="anodo" nodo="J.6"/></componente>
  </placa>
  <hilo a="P/J.1" b="N/CN7.5"/>
  <hilo a="P/J.2" b="N/CN7.38"/>
  <hilo a="P/J.3" b="N/CN10.16"/>
  <hilo a="P/J.4" b="N/CN10.11"/>
  <hilo a="P/J.5" b="N/CN7.14"/>
  <hilo a="P/J.6" b="N/CN10.32"/>
</sistema>
""" % nucleo)
    rc, out, err = corre(sim, [f, "--ms=1"])
    nodos = [nodo_de_led(out, "P/L%d" % k) for k in range(1, 7)]
    check(rc == 0 and nodos == ["N/u0.VDD", "N/u0.PC0", "N/u0.PB12", "N/u0.PA5",
                                "N/u0.NRST", "N/u0.VSSA"] and led(out, "P/L1") == "encendido",
          "los morpho van a los pines de la tarjeta: CN7.5 es VDD -y un LED ahi luce-, "
          "CN7.38 PC0, CN10.16 PB12, CN10.11 PA5 -el D13-, CN7.14 el RESET y CN10.32 "
          "AGND: %s" % nodos)

    # El sistema de ejemplo: la barra azul por los morpho, el comun a VDD
    ej = "placas/nucleo_f446re_barra8ac_azul.xml"
    rc, out, err = corre(sim, [ej, "--valida"])
    check(rc == 0 and "2 placas: N (nucleo-f446re), B (barra8-anodo-comun-azul)" in out and
          "1 MCU(s), 18 componentes, 170 nodos, 0 avisos" in out,
          "%s: la Nucleo y la barra de anodo comun azul, nueve hilos, ni un aviso -el comun "
          "es bus aunque el hilo lo lleve a VDD-" % ej)
    rc, out, err = corre(sim, [ej, "--ms=75"])
    pines = []
    for k in range(1, 9):
        for l in out.splitlines():
            l = l.strip()
            if l.startswith("LED B/D%d entre " % k):
                pines.append(l.split(" entre ")[1].split(":")[0])
    check(pines == ["N/u0.VDD y N/u0.P%s" % p for p in
                    ("C0", "C1", "C2", "C3", "B12", "B13", "B14", "B15")],
          "P1.D1..D8 en PC0..PC3 y PB12..PB15, y P1.COM en VDD, por CN7 y CN10")
    vistos = []
    for ms in (25, 75, 225, 375, 425):
        rc, out, err = corre(sim, [ej, "--ms=%d" % ms])
        vistos.append([k for k in range(1, 9) if led(out, "B/D%d" % k) == "encendido"])
    check(vistos == [[1], [2], [5], [8], [1]] and medida(out, "B/D1") == "3.29 V, 0.15 mA",
          "y el firmware hace correr la luz, un LED cada 50 ms, cada uno con su pin a cero; "
          "azul con 3,3 V, 0,15 mA: %s" % vistos)

    v = Ventana()
    p = arranca(sim, [ej, "--valida"], v.puerto)
    try:
        v.acepta()
        _, placa, _, _ = saludo_hasta_listo(v, valida=True, version=2)
        termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    if not check(placa is not None, "saludo en la version 2"):
        return
    r = ET.fromstring(placa)
    cs = {c.get("ref"): c for pl in r.findall("placa") for c in pl.findall("conector")}
    check(sorted(cs) == ["B/P1", "N/CN10", "N/CN5", "N/CN6", "N/CN7", "N/CN8", "N/CN9"] and
          cs["N/CN7"].get("filas") == "2" and cs["N/CN7"].get("columnas") == "19" and
          cs["B/P1"].get("nombres") == "COM D1 D2 D3 D4 D5 D6 D7 D8" and
          cs["N/CN7"].get("nombres") is None,
          "T_PLACA cuenta los seis conectores de la Nucleo -los morpho, de 2x19- y el P1 de "
          "la barra con los nombres de sus pines")


# ---------------------------------------------------------------------------
def main():
    a = argparse.ArgumentParser(description="conectores y sistemas de placas")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.abspath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    cp = Carpeta()
    try:
        c1_conector(sim, cp)
        c2_nucleo_y_shield(sim)
        c3_espejo(sim, cp)
        c4_dos_mcus(sim, cp)
        c5_t_placa(sim)
        c6_errores(sim, cp)
        c7_pila(sim, cp)
        c8_dibujable(sim)
        c9_alimentacion(sim, cp)
        c10_dibujos(sim, cp)
        c11_barra(sim, cp)
        c12_nombres_y_morpho(sim, cp)
    finally:
        cp.borra()
    return ventana.resumen("SISTEMA")


if __name__ == "__main__":
    sys.exit(main())
