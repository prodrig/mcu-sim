#!/usr/bin/env python3
# =============================================================================
# gdb_g2.py — msp430-elf-gdb DE VERDAD contra la MSP-EXP430G2ET simulada
#             (P-20 de doc/todo.md; cierre de M2 y M3 de doc/analisis_msp430.md)
#
# El guion de depuración de siempre, con el GDB de la cadena de TI
# (msp430-gcc de Mitto Systems), por los dos caminos:
#
#   --gdb-dap   el stub contra el núcleo (DbgDirectoM430)
#   --gdb       la sonda de bancada por Spy-Bi-Wire en TEST/RST (DbgSondaM430):
#               el `load` programa la Flash con el controlador y TCLK
#
# y en cada uno: load de g2_basico.elf (la Flash se compara con el ELF),
# ruptura en main por software, `monitor reset halt`, next, ruptura por
# hardware en la rutina del Timer0_A3, vigilancia de `ticks` (escritura),
# paso de instrucción, Ctrl-C (SIGINT a GDB) con la CPU en marcha, y que el
# perro no muerda con la CPU parada (GENCLKCTRL = 0x26, el de la herramienta).
#
# Y la MSP-EXP430F5529LP por los dos caminos (M4): --gdb-dap y --gdb, este
# por SBW con las secuencias de Xv2 (DbgSondaXv2): load en la Flash de 5xx con
# las rutinas del Replicator430Xv2, ruptura en 0x10000, finish, stepi, Ctrl-C.
#
#   python3 verif/g2553/gdb_g2.py [--sim build/mcu-sim] [--gdb msp430-elf-gdb]
#
# Sin msp430-elf-gdb, lo dice y sale con 0 (no es una suite con invariante).
# =============================================================================
import argparse
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

fallos = 0


def check(c, t):
    global fallos
    print("  [%s] %s" % ("OK  " if c else "FALLO", t))
    if not c:
        fallos += 1
    return c


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Sim:
    def __init__(self, sim, args, puerto):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
        self.p = subprocess.Popen([sim] + args + ["--port=%d" % puerto, "--ms=100000000"],
                                  stdout=self.log, stderr=subprocess.STDOUT)
        t0 = time.time()
        self.listo = False
        while time.time() - t0 < 30 and self.p.poll() is None:
            if ("localhost:%d" % puerto) in self.texto():
                self.listo = True
                break
            time.sleep(0.2)

    def texto(self):
        with open(self.log.name, errors="replace") as f:
            return f.read()

    def fin(self):
        self.p.terminate()
        try:
            self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
        t = self.texto()
        os.unlink(self.log.name)
        return t


def gdb(prog, puerto, cmds, elf, plazo=600):
    a = [prog, "-q", "-batch", "-ex", "set remotetimeout 60", "-ex", "set pagination off",
         "-ex", "target extended-remote localhost:%d" % puerto]
    for c in cmds:
        a += ["-ex", c]
    a.append(elf)
    r = subprocess.run(a, capture_output=True, text=True, timeout=plazo)
    return r.stdout + r.stderr


def una(sim, modo, prog, elf, xml):
    pu = puerto_libre()
    s = Sim(sim, [xml, modo], pu)
    try:
        if not check(s.listo, "%s: el stub escucha" % modo):
            return
        o = gdb(prog, pu, [
            "load",
            "compare-sections",
            "break main",
            "monitor reset halt",
            "p/x $pc",
            "continue",
            "next", "next",
            "p/x {unsigned short}0x0120",
            "hbreak t0",
            "continue",
            "p/x $pc",
            "delete",
            "watch ticks",
            "continue",
            "p ticks",
            "delete",
            "stepi",
            "p/x $pc",
            "set {unsigned short}0x0120 = 0x5a00",       # el perro en marcha (32 ms)...
            "shell sleep 1",                             # ...y parado no muerde
            "p/x {unsigned short}0x0120",
            "p/x $pc",
        ], elf)
        # Ctrl-C: GDB en marcha con `continue`, y un SIGINT al proceso, como
        # quien pulsa Ctrl-C en la consola
        a = [prog, "-q", "-batch", "-ex", "set remotetimeout 60", "-ex",
             "target extended-remote localhost:%d" % pu, "-ex", "continue", "-ex", "p/x $pc", elf]
        g = subprocess.Popen(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(4)
        g.send_signal(signal.SIGINT)
        try:
            o2 = g.communicate(timeout=120)[0]
        except subprocess.TimeoutExpired:
            g.kill()
            o2 = g.communicate()[0]
        o += o2
        vec = re.search(r"\$1 = (0x[0-9a-f]+)", o)
        check("Start address" in o and "MIS-MATCHED" not in o and o.count("matched.") >= 4,
              "%s: load y compare-sections (todas las secciones casan)" % modo)
        check(vec is not None and int(vec.group(1), 16) == 0xc02e,
              "%s: monitor reset halt: el PC en el vector de reset (0xc02e, __crt0_start)" % modo)
        check("Breakpoint 1, main ()" in o and "WDTCTL = WDTPW | WDTHOLD;" in o,
              "%s: la ruptura en main y next" % modo)
        check(re.search(r"\$2 = 0x6980", o) is not None, "%s: después de next, WDTCTL = 0x6980" % modo)
        check(re.search(r"(Hardware assisted )?[Bb]reakpoint 2, t0 \(\)", o) is not None,
              "%s: hbreak en la rutina del Timer0_A3" % modo)
        check(re.search(r"Hardware watchpoint 3: ticks\s+Old value = 0\s+New value = 1", o) is not None,
              "%s: watch ticks (0 -> 1)" % modo)
        m = re.findall(r"\$(\d+) = (0x[0-9a-f]+)", o)
        valores = dict((int(k), int(v, 16)) for k, v in m)
        # $5 el PC tras stepi; $6 WDTCTL en marcha (0x6900) tras 1 s de pared
        # parado; $7 el PC, el mismo: el perro no ha mordido
        check(valores.get(6) == 0x6900 and valores.get(5) is not None and valores.get(5) == valores.get(7),
              "%s: el perro en marcha no muerde con la CPU parada (GENCLKCTRL)" % modo)
        check("Program received signal SIGINT" in o2 and re.search(r"\$1 = 0x[0-9a-f]+", o2) is not None,
              "%s: Ctrl-C (interrupt) para la CPU en marcha" % modo)
        if fallos:
            print(o[-3000:])
    finally:
        vivo = s.p.poll() is None
        s.fin()
        check(vivo, "%s: el simulador sigue en pie tras las dos sesiones" % modo)


def una_f5(sim, prog, modo):
    """El F5529LP por --gdb-dap o por los pines (--gdb: la sonda de bancada por
    SBW con las secuencias de Xv2): load en la Flash de 5xx (por encima de
    0x10000 también), una ruptura en una función de 0x10000 (modelo grande),
    finish, stepi y Ctrl-C"""
    elf = os.path.join("verif", "f5529", "elf", "f5_basico.elf")
    pu = puerto_libre()
    s = Sim(sim, [os.path.join("placas", "msp_exp430f5529lp.xml"), modo], pu)
    n = "F5529 %s" % modo
    try:
        if not check(s.listo, "%s: el stub escucha" % n):
            return
        o = gdb(prog, pu, ["load", "compare-sections", "break arriba", "monitor reset halt", "continue",
                           "p/x $pc", "p x", "finish", "stepi", "p/x $pc", "p/x $sp"], elf)
        a = [prog, "-q", "-batch", "-ex", "set remotetimeout 60", "-ex",
             "target extended-remote localhost:%d" % pu, "-ex", "continue", "-ex", "p/x $pc", elf]
        g = subprocess.Popen(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(4)
        g.send_signal(signal.SIGINT)
        try:
            o2 = g.communicate(timeout=120)[0]
        except subprocess.TimeoutExpired:
            g.kill()
            o2 = g.communicate()[0]
        check("Start address" in o and "MIS-MATCHED" not in o and o.count("matched.") >= 4,
              "%s: load en la Flash de 5xx (también la de 0x10000) y compare-sections" % n)
        check(re.search(r"Breakpoint 1, (0x[0-9a-f]+ in )?arriba \(x=41\)", o) is not None and
              re.search(r"\$1 = 0x10000", o) is not None,
              "%s: ruptura en arriba(), en 0x10000 (modelo grande), con x = 41" % n)
        check("Value returned is $3 = 124" in o, "%s: finish devuelve 124" % n)
        m = re.findall(r"\$(\d+) = (0x[0-9a-f]+)", o)
        v = dict((int(k), int(x, 16)) for k, x in m)
        check(v.get(4) is not None and 0x4400 <= v[4] <= 0x243ff and v.get(5) is not None and 0x2400 <= v[5] <= 0x4400,
              "%s: stepi: el PC en la Flash y el SP en la RAM" % n)
        check("Program received signal SIGINT" in o2 and re.search(r"\$1 = 0x[0-9a-f]+", o2) is not None,
              "%s: Ctrl-C (interrupt) para la CPU en marcha" % n)
        if fallos:
            print(o[-2500:])
            print(o2[-1500:])
    finally:
        vivo = s.p.poll() is None
        s.fin()
        check(vivo, "%s: el simulador sigue en pie tras las dos sesiones" % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default="build/mcu-sim")
    ap.add_argument("--gdb", default=None)
    a = ap.parse_args()
    prog = a.gdb or shutil.which("msp430-elf-gdb")
    if not prog:
        print("  [OMITIDO] no hay msp430-elf-gdb (--gdb RUTA)")
        return 0
    elf = os.path.join("verif", "g2553", "elf", "g2_basico.elf")
    xml = os.path.join("placas", "msp_exp430g2et.xml")
    for modo in ("--gdb-dap", "--gdb"):
        print("\n--- %s ---" % modo)
        una(a.sim, modo, prog, elf, xml)
    for modo in ("--gdb-dap", "--gdb"):
        print("\n--- F5529 %s ---" % modo)
        una_f5(a.sim, prog, modo)
    print("\nTOTAL GDB G2: %s" % ("0 fallos" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
