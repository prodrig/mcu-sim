#!/usr/bin/env python3
# =============================================================================
# verif/openocd/riscv_lab.py — G2 de E2 (doc/analisis_esp32_riscv.md): OpenOCD
# genérico y GDB de verdad contra el laboratorio RISC-V
#
#   L1  OpenOCD `target create riscv` por remote_bitbang (JTAG): examine,
#       halt, reg, mdw, load_image, resume y reset halt
#   L2  lo mismo por CMSIS-DAP en JTAG (`cmsis-dap backend tcp`)
#   L3  dos harts por remote_bitbang: dos targets con -coreid y `target smp`;
#       halt para los dos, y cada uno donde tiene que estar
#   L4  GDB por el servidor de GDB de OpenOCD: registros, un punto de ruptura,
#       continue, stepi y monitor reset halt
#   L5  el mismo GDB contra el stub G1 de mcu-sim (testrv --servir=gdb), sin
#       OpenOCD: el mismo resultado
#
# El laboratorio lo sirve `build/testrv --servir=...` con verif/rv/elf/dbg.elf.
# L1-L4 necesitan un OpenOCD con `cmsis-dap backend tcp` (de git posterior al
# 2025-09-01; ci/construye_openocd.sh); L4-L5, un GDB con RISC-V
# (gdb-multiarch o riscv64-unknown-elf-gdb). Lo que falte se OMITE diciéndolo,
# salvo con OPENOCD_OBLIGATORIO=1.
#
#   make -f Makefile.mcu-sim openocd-rv OPENOCD=/ruta/a/bin/openocd     (desde src/)
# =============================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import programa as pr                                    # noqa: E402
from programa import check, grupo, puerto_libre, corre, palabras   # noqa: E402

DBG = "verif/rv/elf/dbg.elf"
IDCODE = "0x10ab1001"


class Lab:
    """testrv --servir=...; se espera a que diga que escucha."""

    def __init__(self, testrv, modo, puerto, harts=1):
        self.log = tempfile.TemporaryFile(mode="w+")
        self.p = subprocess.Popen([testrv, "--servir=" + modo, "--port=%d" % puerto,
                                   "--harts=%d" % harts, DBG],
                                  stdout=self.log, stderr=subprocess.STDOUT, text=True)
        self.listo = False
        t0 = time.time()
        while time.time() - t0 < 20 and self.p.poll() is None:
            if "escuchando" in self.texto():
                self.listo = True
                break
            time.sleep(0.05)

    def texto(self):
        self.log.seek(0)
        return self.log.read()

    def fin(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()
        return self.texto()


def simbolos():
    """Las direcciones de dbg.elf, leídas del propio ELF (sin nm)"""
    import struct
    d = open(DBG, "rb").read()
    shoff, = struct.unpack_from("<I", d, 32)
    shentsize, shnum = struct.unpack_from("<HH", d, 46)
    secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    r = {}
    for s in secs:
        if s[1] != 2:                   # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for k in range(s[5] // 16):
            nom, val = struct.unpack_from("<II", d, s[4] + 16 * k)
            n = d[strtab[4] + nom:d.index(b"\0", strtab[4] + nom)].decode()
            r[n] = val
    return r


def ocd_rb(ocd, puerto, gdb_puerto=0):
    return [ocd, "-c", "adapter driver remote_bitbang", "-c", "remote_bitbang host localhost",
            "-c", "remote_bitbang port %d" % puerto, "-c", "transport select jtag",
            "-c", "telnet port disabled", "-c", "tcl port disabled",
            "-c", ("gdb port %d" % gdb_puerto) if gdb_puerto else "gdb port disabled",
            "-c", "jtag newtap riscv cpu -irlen 5 -expected-id " + IDCODE]


def ocd_dap(ocd, puerto):
    return [ocd, "-c", "adapter driver cmsis-dap", "-c", "cmsis-dap backend tcp",
            "-c", "cmsis-dap tcp host localhost", "-c", "cmsis-dap tcp port %d" % puerto,
            "-c", "transport select jtag", "-c", "adapter speed 4000",
            "-c", "telnet port disabled", "-c", "tcl port disabled", "-c", "gdb port disabled",
            "-c", "jtag newtap riscv cpu -irlen 5 -expected-id " + IDCODE]


def ordenes(*cs):
    a = []
    for c in cs:
        a += ["-c", c]
    return a


def reg(sal, nombre):
    m = re.findall(r"^%s \(/32\): (0x[0-9a-f]+)" % nombre, sal, re.M)
    return [int(x, 16) for x in m]


def basico(a, modo, ocd_fn, nombre, sym, prog):
    grupo(nombre)
    p = puerto_libre()
    lab = Lab(a.testrv, modo, p)
    if not check(lab.listo, "el laboratorio escucha (%s)" % modo):
        print(lab.fin())
        return
    t0 = time.time()
    rc, sal, _ = corre(ocd_fn(a.openocd, p) + ordenes(
        "target create riscv.cpu riscv -tap riscv.cpu", "init", "halt", "reg pc", "reg fp",
        "mdw 0x80000000 2", "load_image %s 0x80100000 bin" % prog, "mdw 0x80100000 3",
        "resume", "sleep 20", "halt", "reg fp", "reset halt", "reg pc", "resume", "shutdown"), 180)
    dt = time.time() - t0
    lab.fin()
    pcs, fps = reg(sal, "pc"), reg(sal, "fp")
    check("tap/device found: " + IDCODE in sal and "Examination succeed" in sal and
          "misa=0x40101105" in sal, "examine: el IDCODE, el DM 0.13 y misa = RV32IMAC")
    check(len(pcs) == 2 and sym["main"] <= pcs[0] < sym["objetivo"] + 4,
          "halt y reg pc: en el bucle de main")
    check(palabras(sal, 0x80000000) == [0xf14022f3, 0x04029e63], "mdw: el principio de _start")
    check(palabras(sal, 0x80100000) == [0x00000513, 0x00550513, 0x00100073],
          "load_image de un .bin a la RAM, y mdw lo lee")
    check(len(fps) == 2 and fps[1] > fps[0], "resume y halt: s0 ha seguido contando")
    check(len(pcs) == 2 and pcs[1] == 0x80000000, "reset halt: pc = 0x80000000")
    check(rc == 0, "OpenOCD acaba bien (%.1f s)" % dt)
    if rc != 0:
        print(sal[-3000:])


def dos_harts(a, sym):
    grupo("L3 dos harts por remote_bitbang: dos targets, target smp")
    p = puerto_libre()
    lab = Lab(a.testrv, "bitbang", p, harts=2)
    if not check(lab.listo, "el laboratorio de dos harts escucha"):
        print(lab.fin())
        return
    rc, sal, _ = corre(ocd_rb(a.openocd, p) + ordenes(
        "target create riscv.cpu0 riscv -tap riscv.cpu -coreid 0",
        "target create riscv.cpu1 riscv -tap riscv.cpu -coreid 1",
        "target smp riscv.cpu0 riscv.cpu1", "init", "halt",
        "targets riscv.cpu0", "reg pc", "targets riscv.cpu1", "reg pc",
        "resume", "shutdown"), 180)
    lab.fin()
    pcs = reg(sal, "pc")
    check(sal.count("Examination succeed") >= 2, "los dos harts examinados (hartsel)")
    check(len(pcs) == 2 and sym["main"] <= pcs[0] < sym["objetivo"] + 4 and
          sym["otro_hart"] <= pcs[1] < sym["otro_hart"] + 24,
          "halt los para a los dos: el 0 en main, el 1 en otro_hart")
    check(rc == 0, "OpenOCD acaba bien")
    if rc != 0:
        print(sal[-3000:])


def gdb_script(sym, gdb_puerto):
    return [
        "set confirm off", "set pagination off", "set architecture riscv:rv32",
        "target extended-remote localhost:%d" % gdb_puerto,
        "monitor reset halt", "flushregs", "p/x $pc", "break *0x%x" % sym["objetivo"], "continue", "p/x $pc",
        "stepi", "p/x $pc", "p/x $s0", "delete", "monitor reset halt", "flushregs", "p/x $pc",
        "detach",
    ]


def sesion_gdb(gdb, sym, puerto):
    cmd = [gdb, "-nx", "-batch"]
    for c in gdb_script(sym, puerto):
        cmd += ["-ex", c]
    rc, sal, _ = corre(cmd, 120)
    vals = [int(x, 16) for x in re.findall(r"^\$\d+ = (0x[0-9a-f]+)", sal, re.M)]
    return rc, sal, vals


def informa_gdb(que, rc, sal, vals, sym):
    ok = len(vals) == 5
    check(ok and vals[0] == 0x80000000, que + ": conectar y reset halt, pc = 0x80000000")
    check(ok and vals[1] == sym["objetivo"], que + ": break en objetivo y continue")
    check(ok and vals[2] == sym["objetivo"] + 2, que + ": stepi (c.addi, 2 bytes)")
    check(ok and vals[4] == 0x80000000, que + ": monitor reset halt otra vez")
    if not ok:
        print(sal[-3000:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--testrv", default="build/testrv")
    ap.add_argument("--openocd", default=os.environ.get("OPENOCD", "openocd"))
    ap.add_argument("--gdb", default=os.environ.get("GDB_RV", ""))
    a = ap.parse_args()
    oblig = os.environ.get("OPENOCD_OBLIGATORIO") == "1"
    sym = simbolos()
    prog = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
    prog.write(bytes.fromhex("13050000" "13055500" "73001000"))
    prog.close()

    hay_ocd = bool(shutil.which(a.openocd) or os.path.exists(a.openocd))
    if hay_ocd:
        _, v, _ = corre([a.openocd, "-c", "adapter driver cmsis-dap", "-c", "cmsis-dap backend tcp",
                         "-c", "shutdown"], 20)
        if "invalid command name" in v or "Unknown" in v and "backend" in v:
            hay_ocd = False
    gdb = a.gdb or shutil.which("riscv64-unknown-elf-gdb") or shutil.which("gdb-multiarch") or ""

    if hay_ocd:
        basico(a, "bitbang", ocd_rb, "L1 OpenOCD por remote_bitbang (JTAG)", sym, prog.name)
        basico(a, "cmsis-dap", ocd_dap, "L2 OpenOCD por CMSIS-DAP (JTAG)", sym, prog.name)
        dos_harts(a, sym)
    else:
        print("\n(OMITIDO L1-L4: no hay un OpenOCD con `cmsis-dap backend tcp` en %s)" % a.openocd)
        if oblig:
            check(False, "OpenOCD obligatorio")

    if gdb and hay_ocd:
        grupo("L4 GDB por el servidor de GDB de OpenOCD (remote_bitbang)")
        p, g = puerto_libre(), puerto_libre()
        lab = Lab(a.testrv, "bitbang", p)
        ocd = subprocess.Popen(ocd_rb(a.openocd, p, g) + ordenes(
            "target create riscv.cpu riscv -tap riscv.cpu", "init", "halt"),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
        rc, sal, vals = sesion_gdb(gdb, sym, g)
        ocd.terminate()
        ocd.wait(10)
        lab.fin()
        informa_gdb("GDB-OpenOCD", rc, sal, vals, sym)
    if gdb:
        grupo("L5 el mismo GDB contra el stub G1 de mcu-sim")
        g = puerto_libre()
        lab = Lab(a.testrv, "gdb", g)
        rc, sal, vals = sesion_gdb(gdb, sym, g)
        lab.fin()
        informa_gdb("GDB-G1", rc, sal, vals, sym)
    else:
        print("\n(OMITIDO L4-L5: no hay un GDB con RISC-V)")

    os.unlink(prog.name)
    print("\nTOTAL OPENOCD-RV: %d comprobaciones OK, %d fallos" % (pr.OK, pr.MAL))
    return 1 if pr.MAL else 0


if __name__ == "__main__":
    sys.exit(main())
