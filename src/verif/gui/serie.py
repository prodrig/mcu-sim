#!/usr/bin/env python3
# =============================================================================
# serie.py — Un puente serie en memoria con la ventana como terminal, contra el
# mcu-sim de verdad
#
# T_SERIE y T_SERIE_ENVIA (`mcu-sim-gui/doc/protocolo.md` §4): un PuenteSerie
# cuyo destino es `memoria` no abre ningún puerto, y la ventana hace de
# terminal en su propia pestaña. Como los otros scripts de aquí, hace de
# VENTANA con su propia implementación del marco (`ventana.py`).
#
#   S1  el catálogo lo anuncia: `<serie destino="memoria" formato=...>` dentro
#       de la <pieza> del puente; y con el puente por red, no;
#   S2  lo que imprime el firmware llega en T_SERIE, con el índice de su pieza
#       y en el orden en que salió; lo que se manda con T_SERIE_ENVIA llega al
#       MCU (vcp_demo lo devuelve), con bytes que no son texto incluidos; y el
#       resumen de mcu-sim cuenta los mismos bytes que han llegado;
#   S3  lo que no vale: a una pieza que no es un puente en memoria, sin
#       cabecera, o que no mide lo que dice. Un T_AVISO cada uno, y la
#       simulación sigue.
#
#   make -f Makefile.mcu-sim gui-serie
#   python3 verif/gui/serie.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import re
import struct
import sys
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, check, grupo, arranca, termina, saludo_hasta_listo, fin,
                     aviso, arranque, T_AVISO, T_FIN, T_PARA, T_SERIE, T_SERIE_ENVIA,
                     M_PARA, N_AVISO, RIT_LIBRE)

PLACA = "placas/nucleo_f446re_vcp.xml"
FW = "verif/fw/vcp_demo/vcp_demo.bin"


def pieza_serie(cat):
    """(idx, formato) del primer <serie> del catálogo, o (None, None)."""
    for pz in ET.fromstring(cat).iter("pieza"):
        s = pz.find("serie")
        if s is not None:
            return int(pz.get("idx")), s.get("formato"), s.get("destino"), pz.get("id")
    return None, None, None, None


def idx_de(cat, ident):
    for pz in ET.fromstring(cat).iter("pieza"):
        if pz.get("id") == ident:
            return int(pz.get("idx"))
    return None


def serie(cuerpo):
    """(t_sim_ns, pieza, bytes)"""
    t, p, _, n = struct.unpack("<QHHI", cuerpo[:16])
    return t, p, cuerpo[16:16 + n] if len(cuerpo) == 16 + n else None


def envia(v, pieza, datos, n=None):
    v.manda(T_SERIE_ENVIA, struct.pack("<QHHI", 0, pieza, 0,
                                       len(datos) if n is None else n) + datos)


class Recogida:
    """Lo que va llegando: los bytes de cada pieza y los avisos."""

    def __init__(self, v):
        self.v = v
        self.bytes = {}
        self.avisos = []
        self.malos = 0
        self.orden_ok = True
        self.t_ant = 0

    def hasta(self, cond, seg=10.0):
        fin_espera = time.time() + seg
        while time.time() < fin_espera:
            if cond():
                return True
            t, c = self.v.recibe(seg=max(0.01, fin_espera - time.time()))
            if t is None:
                return cond()
            if t == T_SERIE:
                ts, p, b = serie(c)
                if b is None:
                    self.malos += 1
                    continue
                if ts < self.t_ant:
                    self.orden_ok = False
                self.t_ant = ts
                self.bytes[p] = self.bytes.get(p, b"") + b
            elif t == T_AVISO:
                self.avisos.append(aviso(c))
        return cond()


# ---------------------------------------------------------------------------
# S1 y S2
# ---------------------------------------------------------------------------
def s1_s2(sim):
    grupo("S1 El catalogo anuncia el puente en memoria")
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "--serie", "VCP=memoria"], v.puerto)
    try:
        if not check(v.acepta(), "mcu-sim se conecta"):
            return
        _, _, cat, listo = saludo_hasta_listo(v)
        if not check(listo and cat is not None, "y saluda hasta T_LISTO"):
            return
        idx, formato, destino, ident = pieza_serie(cat)
        check(ident == "VCP" and destino == "memoria",
              "la <pieza> VCP trae <serie destino=\"memoria\">: %s, %s" % (ident, destino))
        check(formato == "115200 8N1", "con su formato, para la pestana: '%s'" % formato)
        check(idx == idx_de(cat, "VCP"), "y T_SERIE la nombra por su idx de pieza: %s" % idx)

        grupo("S2 Lo que imprime el firmware llega en T_SERIE, y lo tecleado llega al MCU")
        arranque(v, RIT_LIBRE)
        r = Recogida(v)
        saludo = b"vcp_demo listo\r\n"
        check(r.hasta(lambda: saludo in r.bytes.get(idx, b"")),
              "el saludo de vcp_demo llega entero: %r" % r.bytes.get(idx, b"")[:40])
        check(set(r.bytes) == {idx}, "y solo para esa pieza")
        antes = len(r.bytes.get(idx, b""))
        envia(v, idx, b"hola\r\n")
        check(r.hasta(lambda: r.bytes.get(idx, b"")[antes:] == b"hola\r\n"),
              "T_SERIE_ENVIA 'hola\\r\\n': el firmware lo devuelve tal cual: %r"
              % r.bytes.get(idx, b"")[antes:])
        # Bytes que no son texto: el cero, el 0xFF, y lo que va entre medias
        bin_ = bytes([0x00, 0x01, 0x7F, 0x80, 0xA5, 0xFF, 0x0D, 0x0A])
        antes = len(r.bytes.get(idx, b""))
        envia(v, idx, bin_)
        check(r.hasta(lambda: r.bytes.get(idx, b"")[antes:] == bin_),
              "y ocho bytes que no son texto (el 0x00 y el 0xFF incluidos), iguales")
        # Uno grande, que va en varias vueltas del enlace. Sin el 0x13: para
        # vcp_demo es la orden de dejar de leer 5 ms (su prueba de RTS/CTS)
        grande = bytes(b if b != 0x13 else 0x14 for b in ((i * 7 + 3) & 0xFF for i in range(1500)))
        antes = len(r.bytes.get(idx, b""))
        envia(v, idx, grande)
        check(r.hasta(lambda: len(r.bytes.get(idx, b"")) - antes >= len(grande), 20.0) and
              r.bytes.get(idx, b"")[antes:antes + len(grande)] == grande,
              "1500 bytes de golpe vuelven todos y en orden")
        check(r.malos == 0 and r.orden_ok,
              "cada T_SERIE mide lo que dice, y sus instantes no van hacia atras")
        check(not r.avisos, "sin un aviso: %s" % [a[3] for a in r.avisos][:2])
        total = len(r.bytes.get(idx, b""))
        v.manda(T_PARA)
        t, c = None, None
        fin_espera = time.time() + 10
        while time.time() < fin_espera:
            t, c = v.recibe(seg=1.0)
            if t == T_SERIE:
                total += len(serie(c)[2] or b"")
            if t in (None, T_FIN):
                break
        f = fin(c) if t == T_FIN else None
        check(f is not None and f[0] == M_PARA, "T_PARA: T_FIN con M_PARA")
    finally:
        v.cierra()
        rc, out, _ = termina(p)
    m = re.search(r"serie VCP: (\d+) bytes del MCU", out)
    check(rc == 0 and m is not None and int(m.group(1)) == total,
          "el resumen de mcu-sim cuenta %s bytes del MCU, los mismos que han llegado (%d)"
          % (m.group(1) if m else "?", total))


# ---------------------------------------------------------------------------
# S1b: por red, no se anuncia
# ---------------------------------------------------------------------------
def s1b_red(sim):
    grupo("S1b Con el puente por red, el catalogo no trae <serie>")
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "--serie", "VCP=tcp:39871", "--valida"], v.puerto)
    try:
        conecto = v.acepta()
        if not check(conecto, "mcu-sim se conecta con --valida"):
            return
        _, _, cat, _ = saludo_hasta_listo(v, valida=True)
        idx, _, _, _ = pieza_serie(cat) if cat else (None,) * 4
        check(cat is not None and idx is None, "y ninguna pieza trae <serie>")
    finally:
        v.cierra()
        termina(p)


# ---------------------------------------------------------------------------
# S3
# ---------------------------------------------------------------------------
def s3_malos(sim):
    grupo("S3 Lo que no vale: un T_AVISO cada uno, y la simulacion sigue")
    v = Ventana()
    p = arranca(sim, [PLACA, FW, "--serie", "VCP=memoria"], v.puerto)
    try:
        if not check(v.acepta(), "mcu-sim se conecta"):
            return
        _, _, cat, listo = saludo_hasta_listo(v)
        if not check(listo, "y saluda"):
            return
        idx, _, _, _ = pieza_serie(cat)
        ld2 = idx_de(cat, "LD2")
        arranque(v, RIT_LIBRE)
        r = Recogida(v)
        r.hasta(lambda: b"listo" in r.bytes.get(idx, b""))
        envia(v, ld2, b"x")
        check(r.hasta(lambda: len(r.avisos) >= 1) and "no es un puente serie" in r.avisos[0][3],
              "a LD2, que no es un puente: '%s'" % (r.avisos[0][3] if r.avisos else "?"))
        v.manda(T_SERIE_ENVIA, b"\x01\x02\x03")
        check(r.hasta(lambda: len(r.avisos) >= 2) and "sin cabecera" in r.avisos[1][3],
              "sin cabecera: '%s'" % (r.avisos[1][3] if len(r.avisos) > 1 else "?"))
        envia(v, idx, b"abc", n=5)
        check(r.hasta(lambda: len(r.avisos) >= 3) and "no mide lo que dice" in r.avisos[2][3],
              "que no mide lo que dice: '%s'" % (r.avisos[2][3] if len(r.avisos) > 2 else "?"))
        check(all(a[0] == N_AVISO for a in r.avisos[:3]), "los tres de nivel AVISO")
        antes = len(r.bytes.get(idx, b""))
        envia(v, idx, b"sigue\n")
        check(r.hasta(lambda: r.bytes.get(idx, b"")[antes:] == b"sigue\n"),
              "y el puente sigue: lo siguiente vuelve")
        v.manda(T_PARA)
    finally:
        v.cierra()
        termina(p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default="build/mcu-sim")
    a = ap.parse_args()
    s1_s2(a.sim)
    s1b_red(a.sim)
    s3_malos(a.sim)
    return ventana.resumen("GUI-SERIE")


if __name__ == "__main__":
    sys.exit(main())
