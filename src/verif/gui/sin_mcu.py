#!/usr/bin/env python3
# =============================================================================
# sin_mcu.py — Una placa sin MCU, y las fuentes y masas con limite de corriente
#
# El MCU ya no se supone: se declara con <mcu> en el XML o con --mcu, y si no
# hay ni lo uno ni lo otro, la placa va SIN MCU. Lo que queda entonces es el
# modelo electrico y la ventana, y para eso hacen falta las dos piezas nuevas:
# `Fuente` (v voltios) y `Gnd` (0 V), cada una con su limite de corriente
# opcional y dos observables, `corriente` y `sobrecorriente`. Este script hace
# de VENTANA, como los otros de esta carpeta (`ventana.py`), contra el
# `mcu-sim` de verdad con `placas/fuente_y_masa.xml`.
#
#   N1  la placa sin MCU arranca: T_HOLA con `mcu=` vacio, ni <mcu> en
#       T_PLACA, y en el catalogo F1 y G1 con `corriente` (mA) y
#       `sobrecorriente`, que lleva `alarma="si"`;
#   N2  LA FUENTE: con los dos LEDs entrega ~8,5 mA; con CORTO pulsado se
#       limita a sus 20 mA justos, `sobrecorriente` = 1, sale un T_AVISO en
#       ese mismo instante y los LEDs se apagan; al soltarlo, todo vuelve;
#   N3  LA MASA: lo mismo con G1 (10 mA), el LED LD3 y el pulsador SUBE;
#   N4  lo que no tiene sentido sin MCU se rechaza diciendo por que: un
#       firmware, --gdb, un nodo llamado PD12 -con la pista de <mcu> y de
#       --mcu-, y un limite_ma que no es un numero. Dos fuentes en un nodo se
#       avisan, como todo conflicto electrico;
#   N5  --mcu y <mcu> a la vez: gana --mcu, y lo dice; con dos <mcu>, --mcu
#       no sabe a cual cambiar y es un error. Y banco.xml, que no declara
#       ninguno, solo monta con --mcu.
#
#   make -f Makefile.mcu-sim gui-sin-mcu
#   python3 verif/gui/sin_mcu.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, check, grupo, arranca, termina, saludo_hasta_listo,
                     fin, suscribe, instantanea, aviso, ordenes, hecha,
                     T_INSTANTANEA, T_AVISO, T_ORDEN_HECHA, T_FIN, M_VENTANA, RES_OK)

PLACA = "placas/fuente_y_masa.xml"
MS = 1000000


def corre(sim, args, seg=60):
    p = subprocess.run([sim] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=seg)
    return (p.returncode, p.stdout.decode("utf-8", "replace"),
            p.stderr.decode("utf-8", "replace"))


def piezas(cat):
    """{id: (idx, {mando: idx}, {observable: (id_obs, elemento)})}"""
    r = {}
    for pz in ET.fromstring(cat).iter("pieza"):
        r[pz.get("id")] = (int(pz.get("idx")),
                           {m.get("nombre"): int(m.get("idx")) for m in pz.iter("mando")},
                           {o.get("nombre"): (int(o.get("id_obs")), o)
                            for o in pz.iter("observable")})
    return r


def hasta_fin(v, seg=60):
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
# N1
# ---------------------------------------------------------------------------
def n1_arranca(sim):
    grupo("N1 Una placa sin MCU arranca, y el catalogo trae las fuentes")
    v = Ventana()
    p = arranca(sim, [PLACA, "--valida"], v.puerto)
    try:
        if not check(v.acepta(), "mcu-sim se conecta a la ventana"):
            return
        hola, placa, cat, _ = saludo_hasta_listo(v, valida=True)
        rc, out, _ = termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    check(hola is not None and hola.get("mcu") == "" and hola.get("firmware") == "",
          "T_HOLA con mcu= y firmware= vacios: no hay chip")
    check(placa is not None and b"<mcu" not in placa,
          "T_PLACA sin un solo <mcu>")
    check(rc == 0 and "SIN MCU" in out,
          "y --valida lo dice en su resumen: \"%s\""
          % next((l for l in out.splitlines() if "placa '" in l), "?").strip())
    if cat is None:
        return
    pz = piezas(cat)
    for pid in ("F1", "G1"):
        obs = pz.get(pid, (0, {}, {}))[2]
        cor = obs.get("corriente", (0, None))[1]
        sob = obs.get("sobrecorriente", (0, None))[1]
        check(cor is not None and cor.get("unidad") == "mA" and cor.get("alarma") is None and
              sob is not None and sob.get("alarma") == "si",
              "%s ofrece `corriente` en mA y `sobrecorriente`, que es una alarma" % pid)
    otros = [o for _, (_, _, obs) in pz.items() for n, (_, o) in obs.items()
             if n != "sobrecorriente" and o.get("alarma") is not None]
    check(not otros, "y ningun otro observable se marca como alarma")


# ---------------------------------------------------------------------------
# N2 y N3
# ---------------------------------------------------------------------------
def limita(sim, rail, boton, led, lim, normal):
    """Pulsa `boton` de 10 a 20 ms y mira `rail` y `led` cada milisegundo."""
    v = Ventana()
    p = arranca(sim, [PLACA, "--ms=30"], v.puerto)
    try:
        if not v.acepta():
            return None
        _, _, cat, listo = saludo_hasta_listo(v)
        pz = piezas(cat)
        f_obs, b, l_obs = pz[rail][2], pz[boton], pz[led][2]
        ids = [f_obs["corriente"][0], f_obs["sobrecorriente"][0], l_obs["encendido"][0]]
        suscribe(v, 1 * MS, ids)
        ordenes(v, [(10 * MS, b[0], b[1]["pulsar"], 1.0),
                    (10 * MS, b[0], b[1]["pulsar"], 0.0)])
        arranque(v)
        r, f = hasta_fin(v)
        rc, out, _ = termina(p)
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()
    if f is None:
        return None
    m = {}
    for t, _, val in de(r, T_INSTANTANEA):
        d = dict(val)
        m[t] = (d[ids[0]], d[ids[1]], d[ids[2]])
    return m, de(r, T_AVISO), de(r, T_ORDEN_HECHA), f, rc, out


def n2_n3(sim):
    for g, rail, boton, led, lim, normal, que in (
            ("N2", "F1", "CORTO", "LD1", 20.0, (8.0, 9.0), "fuente"),
            ("N3", "G1", "SUBE", "LD3", 10.0, (3.5, 4.5), "masa")):
        grupo("%s La %s %s: %g mA de limite" % (g, que, rail, lim))
        x = limita(sim, rail, boton, led, lim, normal)
        if not check(x is not None, "mcu-sim simula 30 ms con la ventana y termina"):
            continue
        m, avs, ecos, f, rc, out = x
        check(len(m) == 30 and f[0] == M_VENTANA and rc == 0,
              "treinta instantaneas, una por milisegundo, y T_FIN con codigo 0")
        check(len(ecos) == 2 and all(e[4] == RES_OK for e in ecos),
              "%s pulsado en t = 10 ms y suelto en t = 20 ms" % boton)
        # La muestra de 10 ms y la de 20 ms YA ven la orden, pero no todavia
        # su consecuencia electrica: el nodo tarda unos deltas en asentarse, y
        # el muestreador no los espera (lo explica `muestreador()` en
        # parts/frontera_gui.h). Se miran las de despues.
        antes = [m.get(t * MS) for t in range(1, 10)]
        durante = [m.get(t * MS) for t in range(11, 20)]
        despues = [m.get(t * MS) for t in range(21, 31)]
        if None in antes + durante + despues:
            check(False, "faltan instantaneas")
            continue
        check(all(normal[0] < c < normal[1] and s == 0 and e == 1 for c, s, e in antes),
              "antes: %.2f mA, sin sobrecorriente y %s encendido" % (antes[-1][0], led))
        check(all(abs(c - lim) < lim * 1e-3 and s == 1 and e == 0 for c, s, e in durante),
              "con %s pulsado: %.3f mA -el limite, ni uno mas-, `sobrecorriente` = 1 "
              "y %s apagado" % (boton, durante[0][0], led))
        check(all(normal[0] < c < normal[1] and s == 0 and e == 1 for c, s, e in despues),
              "al soltar, vuelve: %.2f mA, sin sobrecorriente, %s encendido"
              % (despues[-1][0], led))
        sobre = [a for a in avs if "sobrecorriente" in a[3]]
        check(len(sobre) == 1 and sobre[0][1] == 10 * MS and sobre[0][3].startswith(rail + ":")
              and ("%g mA" % lim) in sobre[0][3] and que in sobre[0][3],
              "un T_AVISO al entrar en limitacion, en t = 10 ms y no en cada vuelta: "
              "\"%s\"" % (sobre[0][3] if sobre else "?"))
        check(("%s %s en" % ("Fuente" if que == "fuente" else "Gnd", rail)) in out and
              "1 episodio(s) de sobrecorriente" in out,
              "y el informe final lo cuenta: un episodio")


# ---------------------------------------------------------------------------
# N4
# ---------------------------------------------------------------------------
def placa_tmp(cuerpo):
    f = tempfile.NamedTemporaryFile("w", suffix=".xml", delete=False, encoding="utf-8")
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<placa nombre="tmp">\n%s\n</placa>\n'
            % cuerpo)
    f.close()
    return f.name


def n4_rechazos(sim):
    grupo("N4 Lo que no tiene sentido sin MCU, rechazado diciendo por que")
    rc, out, err = corre(sim, [PLACA, "verif/fw/blinky/blinky.bin"])
    check(rc != 0 and "no lleva MCU" in err and "firmware" in err,
          "un firmware: no tiene donde cargarse")
    rc, out, err = corre(sim, [PLACA, "--gdb"])
    check(rc != 0 and "no lleva MCU" in err and "GDB" in err, "--gdb: no hay nucleo")
    rc, out, err = corre(sim, [PLACA, "--ondas"])
    check(rc != 0 and "no lleva MCU" in err, "--ondas: son los relojes del MCU")
    tmp = []
    try:
        tmp.append(placa_tmp('  <nodo id="PD12"/>\n'
                             '  <componente tipo="Led" id="L" a_vss="si">\n'
                             '    <pin nombre="anodo" nodo="PD12"/>\n  </componente>'))
        rc, out, err = corre(sim, [tmp[-1], "--valida"])
        check(rc != 0 and "PD12" in err and '<mcu tipo="STM32F407VG" id="u0"/>' in err and
              "--mcu STM32F407VG" in err,
              "un nodo PD12 en una placa sin MCU: error, con la pista de <mcu> y --mcu")
        rc, out, err = corre(sim, [tmp[-1], "--valida", "--mcu", "STM32F407VG"])
        check(rc == 0, "y la misma placa con --mcu STM32F407VG, valida")
        tmp.append(placa_tmp('  <nodo id="x"/>\n'
                             '  <componente tipo="Fuente" id="F"><pin nombre="pin" nodo="x"/></componente>\n'
                             '  <componente tipo="Gnd" id="G"><pin nombre="pin" nodo="x"/></componente>'))
        rc, out, err = corre(sim, [tmp[-1], "--valida"])
        check("[elec] nodo x: dos fuentes a la vez" in err and "1 avisos" in out,
              "una Fuente y una Gnd en el mismo nodo: un cortocircuito declarado, "
              "y la validacion electrica lo avisa")
        tmp.append(placa_tmp('  <nodo id="x"/>\n'
                             '  <componente tipo="Fuente" id="F" limite_ma="mucho">'
                             '<pin nombre="pin" nodo="x"/></componente>'))
        rc, out, err = corre(sim, [tmp[-1], "--valida"])
        check(rc != 0 and "limite_ma" in (out + err), "limite_ma=\"mucho\": error")
    finally:
        for t in tmp:
            os.unlink(t)


# ---------------------------------------------------------------------------
# N5
# ---------------------------------------------------------------------------
def n5_mcu(sim):
    grupo("N5 --mcu y <mcu> a la vez: gana --mcu")
    rc, out, err = corre(sim, ["placas/discovery_min.xml", "--valida"])
    check(rc == 0 and "mcu u0" in out, "discovery_min.xml declara su u0 y valida")
    rc, out, err = corre(sim, ["placas/discovery_min.xml", "--valida", "--mcu", "STM32F417VG"])
    check(rc == 0 and "--mcu STM32F417VG sustituye al STM32F407VG" in (out + err),
          "con --mcu STM32F417VG manda --mcu, y lo dice")
    rc, out, err = corre(sim, ["placas/discovery_min.xml", "--valida", "--mcu", "STM32F446RE"])
    check(rc != 0 and "--mcu STM32F446RE sustituye" in (out + err) and
          "no sale al encapsulado LQFP64" in err,
          "y con --mcu STM32F446RE tambien, aunque sus LEDs no tengan donde ir: "
          "el LQFP64 no saca PD12..PD15")
    rc, out, err = corre(sim, ["placas/dos_mcu.xml", "--valida", "--mcu", "STM32F446RE"])
    check(rc != 0 and "no dice a cual" in err,
          "con dos <mcu>, --mcu no sabe a cual cambiar: error")
    rc, out, err = corre(sim, ["placas/banco.xml", "--valida"])
    check(rc != 0 and "no lleva MCU" in err,
          "banco.xml no declara ninguno: sin --mcu no monta")
    rc, out, err = corre(sim, ["placas/banco.xml", "--valida", "--mcu", "STM32F407VG"])
    check(rc == 0, "y con --mcu STM32F407VG, si")


def main():
    a = argparse.ArgumentParser(description="placas sin MCU, Fuente y Gnd")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    n1_arranca(sim)
    n2_n3(sim)
    n4_rechazos(sim)
    n5_mcu(sim)
    return ventana.resumen("SIN MCU")


if __name__ == "__main__":
    sys.exit(main())
