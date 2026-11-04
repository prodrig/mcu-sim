#!/usr/bin/env python3
# =============================================================================
# contenido.py — La memoria no volatil, de una simulacion a la siguiente
#
# El atributo `contenido="fichero.hex"` de un <mcu> o de un <componente> con
# memoria no volatil -la Flash de un MCU, la EEPROM de un MSP432E4, una
# I2cEeprom, una SdCard, una ExtNand- la CARGA de ese fichero al arrancar y la
# GUARDA en el al acabar, en Intel HEX (common/memoria_nv.h). En un <sistema>,
# <memoria ref> o <mcu ref contenido> mandan sobre la placa, y --contenido
# ID=FICHERO sobre los dos. Contra el `mcu-sim` de verdad:
#
#   K1  EL XML Y LA LINEA DE ORDENES: una pieza sin memoria no volatil no
#       admite contenido=, ni <memoria ref> a ella; quien manda sobre quien;
#       un id que no existe o dicho dos veces; --memorias, que es lo que lee
#       el dialogo de lanzamiento de mcu-sim-gui, y --argumentos; y --valida
#       no escribe nada;
#   K2  LA FLASH DE UN MCU: el fichero se crea con el firmware dentro, igual
#       que el .bin; sin firmware, arranca lo que hay en el y hace lo mismo;
#       el firmware se graba ENCIMA y los datos de otras paginas siguen; y lo
#       mismo en un STM32F303, un MSP432P401R -con su Flash de informacion,
#       pero no su TLV- y un MSP432E401Y, con su EEPROM en 0x400A_F000;
#   K3  LAS PIEZAS: la I2cEeprom, de ida y vuelta; la SdCard y la ExtNand del
#       banco, enteras;
#   K4  EL FINAL: con Ctrl-C (SIGINT) y con SIGTERM tambien se guarda; con
#       T_PARA desde la ventana, tambien; y un fichero que no se puede leer
#       no se pisa;
#   K5  LOS BYTES DE OPCION de un STM32F4 (0x1FFF_C000) y un F3
#       (0x1FFF_F800): de fabrica no salen; programados, van y vuelven; con
#       WDG_SW = 0, el IWDG arranca solo; RDP en el nivel 1 o 2 se avisa, y
#       --restore-rdp[=ID] lo devuelve al de fabrica;
#   K6  LA OTP de un STM32F4 (0x1FFF_7800), con sus bytes de bloqueo: va y
#       vuelve, y por GDB -como la programaria un firmware-, un bloque se
#       programa hasta que su byte de bloqueo es 0x00, y el bloqueo dura de
#       una simulacion a la siguiente.
#
#   make -f Makefile.mcu-sim gui-contenido
#   python3 verif/gui/contenido.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Codigo de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import socket
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, check, grupo, arranca, termina, saludo_hasta_listo,
                     fin, T_FIN, M_PARA, T_PARA, RIT_LIBRE)


def corre(sim, args, seg=120):
    p = subprocess.run([sim] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=seg)
    return (p.returncode, p.stdout.decode("utf-8", "replace"),
            p.stderr.decode("utf-8", "replace"))


def lee_hex(ruta):
    """{direccion: byte} de un Intel HEX, comprobando cada suma"""
    m, alta = {}, 0
    for l in open(ruta):
        l = l.strip()
        if not l:
            continue
        b = bytes.fromhex(l[1:])
        assert sum(b) & 0xFF == 0, "suma mala en " + l
        n, dirr, tipo = b[0], (b[1] << 8) | b[2], b[3]
        if tipo == 0:
            for k in range(n):
                m[alta + dirr + k] = b[4 + k]
        elif tipo == 4:
            alta = ((b[4] << 8) | b[5]) << 16
        elif tipo == 1:
            break
    return m


def escribe_hex(ruta, m):
    """Un Intel HEX de {direccion: byte}, de 16 en 16"""
    lin, alta = [], None
    dirs = sorted(m)
    i = 0
    while i < len(dirs):
        a = dirs[i]
        if (a >> 16) != alta:
            alta = a >> 16
            r = bytes([2, 0, 0, 4, alta >> 8, alta & 0xFF])
            lin.append(":" + (r + bytes([(-sum(r)) & 0xFF])).hex().upper())
        d = []
        while i < len(dirs) and dirs[i] == a + len(d) and len(d) < 16 and \
                ((a + len(d)) >> 16) == alta:
            d.append(m[dirs[i]])
            i += 1
        r = bytes([len(d), (a >> 8) & 0xFF, a & 0xFF, 0]) + bytes(d)
        lin.append(":" + (r + bytes([(-sum(r)) & 0xFF])).hex().upper())
    lin.append(":00000001FF")
    open(ruta, "w").write("\n".join(lin) + "\n")


def leds(out):
    return [l.strip() for l in out.splitlines() if l.strip().startswith("LED ")]


def eeprom_xml(d, nombre, contenido=None, led=False):
    c = ' contenido="%s"' % contenido if contenido else ""
    x = ('<placa nombre="ee">\n'
         '  <mcu tipo="STM32F407VG" id="u0"/>\n'
         '  <nodo id="PB6" bus="si"/><nodo id="PB7" bus="si"/>\n'
         '  <componente tipo="I2cEeprom" id="U5" dir="0x50"%s>\n'
         '    <pin nombre="scl" nodo="PB6"/><pin nombre="sda" nodo="PB7"/>\n'
         '  </componente>\n' % c)
    if led:
        x += ('  <componente tipo="Led" id="L1" contenido="x.hex">\n'
              '    <pin nombre="anodo" nodo="PD12"/><pin nombre="catodo" nodo="VSS"/>\n'
              '  </componente>\n')
    x += '</placa>\n'
    ruta = os.path.join(d, nombre)
    open(ruta, "w").write(x)
    return ruta


# ---------------------------------------------------------------------------
# K1
# ---------------------------------------------------------------------------
def k1_xml(sim, d):
    grupo("K1 El XML y la linea de ordenes: quien manda sobre quien")
    mal = eeprom_xml(d, "mal.xml", led=True)
    rc, out, err = corre(sim, [mal, "--valida"])
    check(rc != 0 and "un Led no tiene memoria no volatil" in err + out and "linea 7" in err + out,
          "contenido= en un Led es un error, con su linea: solo lo admiten los MCU, la "
          "I2cEeprom, la SdCard y la ExtNand")

    placa = eeprom_xml(d, "ee.xml", os.path.join(d, "placa.hex"))
    sis = os.path.join(d, "sis.xml")
    open(sis, "w").write(
        '<sistema nombre="s">\n'
        '  <placa id="A" fichero="%s"/>\n'
        '  <memoria ref="A/U5" contenido="%s"/>\n'
        '  <mcu ref="A/u0" contenido="%s"/>\n'
        '</sistema>\n' % (placa, os.path.join(d, "sis_u5.hex"), os.path.join(d, "sis_f.hex")))
    rc, out, err = corre(sim, [sis, "--memorias"])
    mem = {}
    try:
        x = out[out.index("<memorias"):]
        mem = {m.get("id"): m for m in ET.fromstring(x).iter("memoria")}
    except (ValueError, ET.ParseError):
        pass
    u0, u5 = mem.get("A/u0"), mem.get("A/U5")
    check(rc == 0 and u0 is not None and u0.get("clase") == "mcu" and
          u0.get("que") == "Flash, OTP y bytes de opcion" and u0.get("contenido", "").endswith("sis_f.hex") and
          u5 is not None and u5.get("clase") == "pieza" and u5.get("tipo") == "I2cEeprom" and
          u5.get("contenido", "").endswith("sis_u5.hex"),
          "--memorias: el chip -su Flash- y la EEPROM, cada una con el contenido del "
          "<sistema>, que manda sobre el de la placa (<mcu ref> y <memoria ref>)")
    rc, out, err = corre(sim, [placa, "--memorias"])
    check(rc == 0 and 'id="u0"' in out and 'contenido=""' in out and
          'contenido="%s"' % os.path.join(d, "placa.hex") in out,
          "y la placa sola: el de la placa, y el chip sin ninguno")

    cli = os.path.join(d, "cli.hex")
    rc, out, err = corre(sim, [sis, "--ms=1", "--contenido", "A/U5=" + cli])
    check(rc == 0 and "contenido de A/U5: 64 bytes a " + cli in out and
          os.path.exists(cli) and not os.path.exists(os.path.join(d, "sis_u5.hex")),
          "--contenido A/U5=... manda sobre el <sistema>: la EEPROM va a su fichero, y el "
          "del sistema ni se crea")
    rc, out, err = corre(sim, [sis, "--ms=1", "--contenido", "A/U9=x.hex"])
    check(rc != 0 and "no hay ningun MCU ni pieza con memoria no volatil" in err and
          "A/u0, A/U5" in err,
          "un --contenido de un id que no es de nadie es un error, que dice los que hay")
    rc, out, err = corre(sim, [placa, "--ms=1", "--contenido", "U5=a.hex",
                               "--contenido", "U5=b.hex"])
    check(rc != 0 and "se dice dos veces" in err, "y el mismo id dos veces, tambien")
    open(os.path.join(d, "sis_mal.xml"), "w").write(
        '<sistema nombre="s">\n  <placa id="A" fichero="%s"/>\n'
        '  <memoria ref="A/L9" contenido="x.hex"/>\n</sistema>\n' % placa)
    rc, out, err = corre(sim, [os.path.join(d, "sis_mal.xml"), "--valida"])
    check(rc != 0 and "no tiene ningun MCU ni pieza que se llame L9" in err + out,
          "<memoria ref> a algo que la placa no tiene: error, con su linea")

    rc, out, err = corre(sim, [placa, "--valida"])
    check(rc == 0 and not os.path.exists(os.path.join(d, "placa.hex")),
          "--valida lee el contenido= y no escribe nada")
    rc, out, err = corre(sim, ["--argumentos"])
    ops = {}
    try:
        ops = {o.get("nombre"): o for o in ET.fromstring(out[out.index("<"):]).iter("opcion")}
    except (ValueError, ET.ParseError):
        pass
    c, m = ops.get("--contenido"), ops.get("--memorias")
    check(c is not None and c.get("repetible") == "si" and c.get("con_gui") != "no" and
          m is not None and m.get("forma") == "accion" and m.get("con_gui") == "no",
          "--argumentos ofrece --contenido, repetible; y --memorias, una accion que la "
          "ventana no ofrece: la usa ella")


# ---------------------------------------------------------------------------
# K2
# ---------------------------------------------------------------------------
def k2_flash(sim, d):
    grupo("K2 La Flash de un MCU: el firmware, y lo que haya encima")
    fw = "verif/fw/blinky/blinky.bin"
    f = os.path.join(d, "f407.hex")
    rc, out1, err = corre(sim, ["placas/discovery_min.xml", fw, "--ms=400",
                                "--contenido", "u0=" + f])
    m = lee_hex(f) if os.path.exists(f) else {}
    b = open(fw, "rb").read()
    check(rc == 0 and "no existe; arranca como siempre" in out1 and
          all(m.get(0x08000000 + i) == b[i] for i in range(len(b))) and
          min(m) == 0x08000000 and max(m) < 0x08000000 + len(b) + 16,
          "la primera vez el fichero no existe: arranca como siempre, y al acabar el "
          "fichero tiene la Flash, en 0x0800_0000: el blinky, byte a byte (%d bytes; lo "
          "borrado no se escribe)" % len(m))
    rc, out2, err = corre(sim, ["placas/discovery_min.xml", "--ms=400",
                                "--contenido", "u0=" + f])
    check(rc == 0 and "arranca lo que haya en su contenido" in out2 and
          leds(out2) == leds(out1) and leds(out1),
          "sin firmware, el chip arranca lo que hay en el contenido: el blinky, con los "
          "LED como con el .bin (%s)" % "; ".join(leds(out2)[:1]))

    # Datos del programa en otra pagina: el firmware se graba encima, y siguen
    dat = {0x08080000 + i: (0x5A + i) & 0xFF for i in range(32)}
    escribe_hex(f, dat)
    rc, out, err = corre(sim, ["placas/discovery_min.xml", fw, "--ms=50",
                               "--contenido", "u0=" + f])
    m = lee_hex(f)
    check(rc == 0 and all(m.get(a) == v for a, v in dat.items()) and
          all(m.get(0x08000000 + i) == b[i] for i in range(len(b))),
          "con firmware y contenido, el firmware se graba ENCIMA: al acabar estan los "
          "dos, el programa en 0x0800_0000 y los datos que tenia en 0x0808_0000")

    # Las otras familias
    casos = [
        ("STM32F303", "placas/stm32f3discovery.xml",
         "verif/fw/stm32f3/gpio_iotoggle/GPIO_IOToggle.bin", 0x08000000),
        ("MSP432P401R", "placas/msp_exp432p401r.xml",
         "verif/fw/msp432p4/p4_demo/p4_demo.out", 0x00000000),
        ("MSP432E401Y", "placas/msp_exp432e401y.xml",
         "verif/fw/msp432e4/e4_demo/e4_demo.out", 0x00000000),
    ]
    for nombre, pl, fw2, base in casos:
        f2 = os.path.join(d, nombre + ".hex")
        rc, out, err = corre(sim, [pl, fw2, "--ms=20", "--contenido", "u0=" + f2])
        m = lee_hex(f2) if os.path.exists(f2) else {}
        sp = sum(m.get(base + k, 0) << (8 * k) for k in range(4))
        check(rc == 0 and m and min(m) == base and 0x20000000 <= sp < 0x20100000,
              "%s: su Flash desde 0x%08X, con la pila inicial del firmware al principio "
              "(0x%08X), %d bytes" % (nombre, base, sp, len(m)))

    # El MSP432P401R: el sector 0 de la Flash de informacion si; el TLV, no
    f2 = os.path.join(d, "p4_info.hex")
    escribe_hex(f2, {0x00200000 + i: i for i in range(16)})
    rc, out, err = corre(sim, ["placas/msp_exp432p401r.xml", "verif/fw/msp432p4/p4_demo/p4_demo.out",
                               "--ms=5", "--contenido", "u0=" + f2])
    m = lee_hex(f2)
    check(rc == 0 and all(m.get(0x00200000 + i) == i for i in range(16)),
          "MSP432P401R: el sector 0 de su Flash de informacion (0x0020_0000) va y vuelve")
    escribe_hex(f2, {0x00201000: 1})
    antes = open(f2).read()
    rc, out, err = corre(sim, ["placas/msp_exp432p401r.xml", "--ms=1", "--contenido", "u0=" + f2])
    check(rc != 0 and "0x00201000 no es de la memoria" in err and open(f2).read() == antes,
          "pero el TLV (0x0020_1000) no es del contenido -lo pone tlv=-: un fichero con el "
          "es un error, y el fichero no se toca")

    # El MSP432E401Y: su EEPROM, en la ventana de sus registros
    f2 = os.path.join(d, "e4_ee.hex")
    ee = {0x400AF000 + i: (0x30 + i) & 0xFF for i in range(64)}
    escribe_hex(f2, ee)
    rc, out, err = corre(sim, ["placas/msp_exp432e401y.xml", "verif/fw/msp432e4/e4_demo/e4_demo.out",
                               "--ms=5", "--contenido", "u0=" + f2])
    m = lee_hex(f2)
    check(rc == 0 and "(Flash y EEPROM)" in out and all(m.get(a) == v for a, v in ee.items()) and
          max(m) < 0x400AF000 + 6 * 1024,
          "MSP432E401Y: la Flash y la EEPROM; la EEPROM, en 0x400A_F000, va y vuelve")


# ---------------------------------------------------------------------------
# K3
# ---------------------------------------------------------------------------
def k3_piezas(sim, d):
    grupo("K3 Las piezas: la I2cEeprom, la SdCard y la ExtNand")
    f = os.path.join(d, "u5.hex")
    placa = eeprom_xml(d, "ee3.xml", f)
    rc, out, err = corre(sim, [placa, "--ms=2"])
    m = lee_hex(f) if os.path.exists(f) else {}
    check(rc == 0 and len(m) == 64 and all(m[i] == 0xA0 + i for i in range(64)),
          "la I2cEeprom sin fichero arranca como siempre -su patron, A0, A1...- y lo guarda "
          "entero, sus 64 bytes")
    nuevo = {i: (255 - i) for i in range(64)}
    escribe_hex(f, nuevo)
    antes = open(f).read()
    rc, out, err = corre(sim, [placa, "--ms=2"])
    check(rc == 0 and "contenido de U5: 64 bytes de" in out and open(f).read() == antes,
          "con un fichero, lo carga y lo vuelve a guardar igual")
    args = []
    for i in ("card", "xnand"):
        args += ["--contenido", "%s=%s" % (i, os.path.join(d, i + ".hex"))]
    rc, out, err = corre(sim, ["placas/banco.xml", "--mcu", "STM32F407VG", "--ms=2"] + args)
    sd = lee_hex(os.path.join(d, "card.hex")) if rc == 0 else {}
    nd = lee_hex(os.path.join(d, "xnand.hex")) if rc == 0 else {}
    check(len(sd) == 2048 and all(sd[i] == (i * 7 + 3) & 0xFF for i in range(2048)) and
          len(nd) == 8192 and all(v == 0xFF for v in nd.values()),
          "la SdCard (2 KB, con su patron) y la ExtNand (8 KB, borrada) del banco, enteras")


# ---------------------------------------------------------------------------
# K4
# ---------------------------------------------------------------------------
def k4_final(sim, d):
    grupo("K4 El final: Ctrl-C, SIGTERM, T_PARA, y un fichero malo")
    fw = "verif/fw/blinky/blinky.bin"
    if os.name != "nt":
        for sen, nom, puerto in ((signal.SIGINT, "SIGINT", 3391), (signal.SIGTERM, "SIGTERM", 3392)):
            f = os.path.join(d, "sig_%s.hex" % nom)
            p = subprocess.Popen([sim, "placas/discovery_min.xml", fw, "--gdb", "--port=%d" % puerto,
                                  "--tiempo-real", "--contenido", "u0=" + f],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(2.0)
            p.send_signal(sen)
            try:
                out, err = p.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                p.kill()
                out, err = p.communicate()
            out = out.decode("utf-8", "replace")
            check(p.returncode == 0 and ("senal %d" % int(sen)) in out and
                  os.path.exists(f) and len(lee_hex(f)) > 1000,
                  "%s con un GDB esperando -la simulacion sin fin-: acaba en orden y "
                  "guarda la Flash" % nom)
    else:
        print("  (SIGINT y SIGTERM: no en Windows)")

    f = os.path.join(d, "gui.hex")
    v = Ventana()
    p = arranca(sim, ["placas/discovery_min.xml", fw, "--contenido", "u0=" + f], v.puerto)
    try:
        ok = v.acepta() and saludo_hasta_listo(v, version=2)[3]
        if ok:
            arranque(v, RIT_LIBRE)
            time.sleep(0.5)
            v.manda(T_PARA)
            while True:
                t, c = v.recibe(seg=30)
                if t is None or t == T_FIN:
                    break
        rc, out, err = termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    check(ok and "contenido de u0:" in out and "bytes a " + f in out and os.path.exists(f),
          "con la ventana, T_PARA acaba la simulacion y la Flash se guarda antes del T_FIN")

    f = os.path.join(d, "malo.hex")
    open(f, "w").write("esto no es un Intel HEX\n")
    rc, out, err = corre(sim, ["placas/discovery_min.xml", fw, "--ms=5", "--contenido", "u0=" + f])
    check(rc != 0 and "no es un registro Intel HEX" in err and
          open(f).read() == "esto no es un Intel HEX\n",
          "un fichero que no se puede leer es un error, y no se pisa al acabar")


# ---------------------------------------------------------------------------
# K5
# ---------------------------------------------------------------------------
def k5_opciones(sim, d):
    grupo("K5 Los bytes de opcion de los STM32, y --restore-rdp")
    fw = "verif/fw/blinky/blinky.bin"
    pl = "placas/discovery_min.xml"
    OB4 = 0x1FFFC000

    f = os.path.join(d, "ob4.hex")
    rc, out0, err = corre(sim, [pl, fw, "--ms=1500", "--contenido", "u0=" + f])
    m = lee_hex(f)
    check(rc == 0 and "(Flash, OTP y bytes de opcion)" in out0 and m and max(m) < 0x08100000,
          "STM32F407: sus bytes de opcion son del contenido, pero los de fabrica no salen "
          "en el fichero: es su referencia")

    # WDG_SW = 0 (bit 5 de USER): el IWDG arranca solo y el blinky, que no lo
    # refresca, se resetea. Acaba con otros LED
    escribe_hex(f, {OB4: 0xDF, OB4 + 1: 0xAA})
    rc, out, err = corre(sim, [pl, fw, "--ms=1500", "--contenido", "u0=" + f])
    m = lee_hex(f)
    check(rc == 0 and leds(out) != leds(out0) and m.get(OB4) == 0xDF and m.get(OB4 + 1) == 0xAA and
          all(m.get(OB4 + k) == 0xFF for k in range(2, 16)),
          "STM32F407: con WDG_SW = 0 el IWDG arranca solo -el blinky no lo refresca y acaba "
          "con otros LED-, y los 16 bytes del bloque vuelven al fichero")

    # RDP en el nivel 2, y --restore-rdp
    escribe_hex(f, {OB4 + 1: 0xCC})
    rc, out, err = corre(sim, [pl, fw, "--ms=5", "--contenido", "u0=" + f])
    m = lee_hex(f)
    check(rc == 0 and "RDP en el nivel 2" in out and "no lo aplica" in out and
          m.get(OB4 + 1) == 0xCC,
          "STM32F407: RDP en el nivel 2 se avisa -el modelo del F4 no lo aplica-, y se guarda")
    rc, out, err = corre(sim, [pl, fw, "--ms=5", "--contenido", "u0=" + f, "--restore-rdp"])
    m = lee_hex(f)
    check(rc == 0 and "--restore-rdp en u0: RDP del nivel 2 al de fabrica" in out and
          not any(a >= OB4 for a in m),
          "STM32F407: --restore-rdp lo devuelve al de fabrica, y los bytes de opcion, ya de "
          "fabrica, no vuelven al fichero")

    # El F3: su bloque de 0x1FFF_F800, con complementos
    OB3 = 0x1FFFF800
    pl3, fw3 = "placas/stm32f3discovery.xml", "verif/fw/stm32f3/gpio_iotoggle/GPIO_IOToggle.bin"
    f3 = os.path.join(d, "ob3.hex")
    rc, out, err = corre(sim, [pl3, fw3, "--ms=5", "--contenido", "u0=" + f3])
    m = lee_hex(f3)
    check(rc == 0 and m and max(m) < 0x08100000,
          "STM32F303: sus bytes de opcion de fabrica tampoco salen")
    escribe_hex(f3, {OB3: 0xCC, OB3 + 1: 0x33, OB3 + 4: 0x5A, OB3 + 5: 0xA5})
    rc, out, err = corre(sim, [pl3, fw3, "--ms=5", "--contenido", "u0=" + f3])
    m = lee_hex(f3)
    check(rc == 0 and "RDP en el NIVEL 2" in out and m.get(OB3) == 0xCC and m.get(OB3 + 1) == 0x33 and
          m.get(OB3 + 4) == 0x5A and m.get(OB3 + 5) == 0xA5 and m.get(OB3 + 2) == 0xFF,
          "STM32F303: DATA0 = 0x5A y RDP en el nivel 2, con sus complementos, van y vuelven, "
          "y el nivel 2 se avisa")
    rc, out, err = corre(sim, [pl3, fw3, "--ms=5", "--contenido", "u0=" + f3, "--restore-rdp=u0"])
    m = lee_hex(f3)
    check(rc == 0 and "RDP del nivel 2 al de fabrica" in out and m.get(OB3) == 0xAA and
          m.get(OB3 + 1) == 0x55 and m.get(OB3 + 4) == 0x5A,
          "STM32F303: --restore-rdp=u0 deja RDP en 0xAA/0x55 aunque este en el nivel 2; "
          "DATA0 sigue")

    rc, out, err = corre(sim, [pl, fw, "--ms=1", "--restore-rdp=u9"])
    rc2, out2, err2 = corre(sim, ["placas/msp_exp432p401r.xml", "--ms=1", "--restore-rdp"])
    check(rc != 0 and "--restore-rdp=u9" in err and rc2 != 0 and "ninguno de los MCUs" in err2,
          "--restore-rdp con un id que no es de un STM32, o en una placa sin STM32: error")


# ---------------------------------------------------------------------------
# K6
# ---------------------------------------------------------------------------
class Rsp:
    """Lo justo de GDB remoto: leer y escribir memoria, como un depurador"""
    def __init__(self, puerto, seg=20):
        t0 = time.time()
        while True:
            try:
                self.c = socket.create_connection(("127.0.0.1", puerto), timeout=5)
                break
            except OSError:
                if time.time() - t0 > seg:
                    raise
                time.sleep(0.2)
        self.c.settimeout(20)
        self.b = b""

    def pide(self, cmd):
        self.c.sendall(("$%s#%02x" % (cmd, sum(cmd.encode()) & 0xFF)).encode())
        while not (b"$" in self.b and b"#" in self.b[self.b.index(b"$"):] and
                   len(self.b) - self.b.index(b"#", self.b.index(b"$")) >= 3):
            self.b += self.c.recv(4096)
        i = self.b.index(b"$")
        j = self.b.index(b"#", i)
        r = self.b[i + 1:j].decode()
        self.b = self.b[j + 3:]
        self.c.sendall(b"+")
        return r

    def w(self, a, data):
        return self.pide("M%x,%x:%s" % (a, len(data), data.hex()))

    def w32(self, a, v):
        return self.w(a, v.to_bytes(4, "little"))

    def r32(self, a):
        return int.from_bytes(bytes.fromhex(self.pide("m%x,4" % a)), "little")


FLASH_R = 0x40023C00
OTP = 0x1FFF7800


def programa(g, a, datos, psize):
    """PG con el PSIZE dado (0: byte, 2: palabra); devuelve FLASH_SR, y lo limpia"""
    g.w32(FLASH_R + 0x10, (psize << 8) | 1)
    g.w(a, datos)
    sr = g.r32(FLASH_R + 0x0C)
    g.w32(FLASH_R + 0x0C, 0xF3)
    return sr


def con_gdb(sim, f, puerto, que):
    """El F407 con su stub de GDB y `contenido`; `que(g)` por GDB; y Ctrl-C"""
    p = subprocess.Popen([sim, "placas/discovery_min.xml", "verif/fw/blinky/blinky.bin", "--gdb",
                          "--port=%d" % puerto, "--tiempo-real", "--contenido", "u0=" + f],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    r = None
    try:
        g = Rsp(puerto)
        g.pide("?")
        g.w32(FLASH_R + 0x04, 0x45670123)                # KEYR: abre CR
        g.w32(FLASH_R + 0x04, 0xCDEF89AB)
        r = que(g)
        g.c.close()
    finally:
        p.send_signal(signal.SIGINT)
        try:
            p.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()
    return r


def k6_otp(sim, d):
    grupo("K6 La OTP de un STM32F4, con su bloqueo")
    fw = "verif/fw/blinky/blinky.bin"
    f = os.path.join(d, "otp.hex")
    dat = {OTP + i: (0xC0 + i) & 0xFF for i in range(32)}
    dat[OTP + 512] = 0x00                                 # bloque 0 bloqueado
    escribe_hex(f, dat)
    rc, out, err = corre(sim, ["placas/discovery_min.xml", fw, "--ms=5", "--contenido", "u0=" + f])
    m = lee_hex(f)
    check(rc == 0 and "(Flash, OTP y bytes de opcion)" in out and
          all(m.get(a) == v for a, v in dat.items()) and
          not any(OTP + 32 <= a < OTP + 512 for a in m),
          "STM32F407: la OTP (0x1FFF_7800) y sus bytes de bloqueo (0x1FFF_7A00) van y vuelven, "
          "sin lo que esta a 0xFF")
    if os.name == "nt":
        print("  (la OTP por GDB: con Ctrl-C para acabar, no en Windows)")
        return

    f = os.path.join(d, "otp_gdb.hex")

    def primera(g):
        a = programa(g, OTP + 0x20, (0x12345678).to_bytes(4, "little"), 2)
        # El byte de bloqueo del bloque 1 a 0x00, en una palabra: los otros
        # tres a 0xFF no programan nada
        b = programa(g, OTP + 0x200, bytes([0xFF, 0x00, 0xFF, 0xFF]), 2)
        c = programa(g, OTP + 0x24, (0x9ABCDEF0).to_bytes(4, "little"), 2)
        return a, b, c, g.r32(OTP + 0x24)
    a, b, c, v = con_gdb(sim, f, 3393, primera)
    m = lee_hex(f) if os.path.exists(f) else {}
    check((a & 0xF3) == 1 and (b & 0xF3) == 1 and (c & 0x10) and v == 0xFFFFFFFF and
          m.get(OTP + 0x20) == 0x78 and m.get(OTP + 0x201) == 0x00,
          "por GDB, como un firmware: el bloque 1 se programa (EOP), su byte de bloqueo a 0x00 "
          "lo cierra, y programarlo otra vez da WRPERR y no escribe; al acabar, la OTP y el "
          "bloqueo estan en el fichero (SR 0x%X, 0x%X, 0x%X)" % (a, b, c))

    def segunda(g):
        return (programa(g, OTP + 0x28, (1).to_bytes(4, "little"), 2),
                programa(g, OTP + 0x40, (0x55AA55AA).to_bytes(4, "little"), 2))
    c1, c2 = con_gdb(sim, f, 3394, segunda)
    m = lee_hex(f) if os.path.exists(f) else {}
    check((c1 & 0x10) and (c2 & 0xF3) == 1 and m.get(OTP + 0x40) == 0xAA and
          m.get(OTP + 0x28) == 0xFF,
          "en la simulacion siguiente el bloque 1 sigue bloqueado (WRPERR); el 2, no")


def main():
    a = argparse.ArgumentParser(description="la memoria no volatil, de una simulacion a otra")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    d = tempfile.mkdtemp(prefix="contenido_")
    try:
        k1_xml(sim, d)
        k2_flash(sim, d)
        k3_piezas(sim, d)
        k4_final(sim, d)
        k5_opciones(sim, d)
        k6_otp(sim, d)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    return ventana.resumen("CONTENIDO")


if __name__ == "__main__":
    sys.exit(main())
