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
#       cada acople y cada hilo.
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
                     fin, suscribe, instantanea, aviso, T_INSTANTANEA, T_AVISO, T_FIN,
                     M_VENTANA)

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
        if l.strip().startswith("LED %s en " % id_):
            return "encendido" if ": encendido" in l else "apagado"
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
    check(rc == 0 and "0 avisos" in out and "163 nodos" in out,
          "la Nucleo con sus cuatro conectores Arduino valida sin un aviso: 154 nodos del "
          "chip y 9 pines de conector al aire (la alimentacion), ninguno flotante")
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
          j5.findall("pin")[6].get("nodo") == "N/CN5.7",
          "los conectores, ya resueltos: J5.6 es N/u0.PA5, y J5.7 -GND, al aire en las dos- "
          "N/CN5.7")
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
    finally:
        cp.borra()
    return ventana.resumen("SISTEMA")


if __name__ == "__main__":
    sys.exit(main())
