#!/usr/bin/env python3
# =============================================================================
# openocd_c3.py — openocd-esp32 y GDB de verdad contra el ESP32-C3 simulado
#
# E6 de doc/analisis_esp32_riscv.md (G1, G2, D4). El simulador, con la
# DevKitM-1 y el JTAG en los pines (--jtag=pines: como quemar
# EFUSE_DIS_USB_JTAG), y target/esp32c3.cfg de openocd-esp32 SIN TOCAR:
#
#   O1  por remote_bitbang (--openocd): el IDCODE 0x00005C25, examine
#       (misa, la revision del chip por los eFuse) y halt
#   O2  program_esp de blink.bin en 0x10000, con verify y reset (D4: el
#       flasher de Espressif corre en el chip simulado), y blink arranca
#   O3  lo mismo que O1 por la sonda CMSIS-DAP por TCP en JTAG (--cmsis-dap):
#       hace falta un OpenOCD con `cmsis-dap backend tcp`
#   O4  GDB (riscv32-esp-elf-gdb) contra el stub interno G1 (--gdb-dap), como
#       `idf.py gdb`: monitor reset halt, thb app_main, continue, bt. Necesita
#       el ELF de la compilacion de hello_world (--elf): no se versiona
#
# --flash-rapida deja los tiempos de programar y borrar la Flash al 1 %: el
# flasher de openocd-esp32 espera con plazos de reloj de pared.
#
# Sin openocd-esp32 (--openocd, $OPENOCD_ESP32, o un `openocd` del PATH que
# tenga target/esp32c3.cfg) la prueba se OMITE diciendolo, salvo con
# OPENOCD_OBLIGATORIO=1. Sin GDB o sin --elf, O4 se omite.
#
#   python3 verif/esp/openocd_c3.py [--sim build/mcu-sim] [--openocd RUTA]
#                                   [--gdb riscv32-esp-elf-gdb] [--elf hello_world.elf]
#   make -f Makefile.mcu-sim openocd-c3 OPENOCD_ESP32=/ruta/a/openocd
# =============================================================================
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

IDF = os.path.join("build", "idf", "v5.5")
fallos = 0


def check(cond, texto):
    global fallos
    print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
    if not cond:
        fallos += 1
    return cond


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Sim:
    def __init__(self, sim, extra):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
        self.puerto = puerto_libre()
        args = [sim, "placas/esp32c3_devkitm1.xml", os.path.join(IDF, "hello_world", "flash_args"),
                "--serie", "UART=memoria", "--jtag=pines", "--port=%d" % self.puerto] + extra
        self.p = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)
        t0 = time.time()
        self.listo = False
        while time.time() - t0 < 30 and self.p.poll() is None:
            try:
                socket.create_connection(("127.0.0.1", self.puerto), timeout=1).close()
                self.listo = True
                break
            except OSError:
                time.sleep(0.1)

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


def ocd(openocd, scripts, cfg, cmds, plazo=600):
    a = [openocd] + (["-s", scripts] if scripts else []) + ["-f", cfg, "-c", "gdb port disabled",
                                                           "-c", "telnet port disabled",
                                                           "-c", "tcl port disabled"]
    for c in cmds:
        a += ["-c", c]
    r = subprocess.run(a, capture_output=True, text=True, timeout=plazo)
    return r.stdout + r.stderr


def cfg_rb(puerto):
    f = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
    f.write("adapter driver remote_bitbang\nremote_bitbang host localhost\nremote_bitbang port %d\n"
            "transport select jtag\nsource [find target/esp32c3.cfg]\n" % puerto)
    f.close()
    return f.name


def cfg_dap(puerto):
    f = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
    f.write("adapter driver cmsis-dap\ncmsis-dap backend tcp\ncmsis-dap tcp host localhost\n"
            "cmsis-dap tcp port %d\ntransport select jtag\nadapter speed 5000\n"
            "source [find target/esp32c3.cfg]\n" % puerto)
    f.close()
    return f.name


def busca_openocd(a):
    for c in (a.openocd, os.environ.get("OPENOCD_ESP32"), shutil.which("openocd")):
        if c and (os.path.exists(c) or shutil.which(c)):
            raiz = os.path.dirname(os.path.dirname(os.path.realpath(shutil.which(c) or c)))
            sc = os.path.join(raiz, "share", "openocd", "scripts")
            if os.path.exists(os.path.join(sc, "target", "esp32c3.cfg")):
                return c, sc
    return None, None


def main():
    ap = argparse.ArgumentParser(description="openocd-esp32 y GDB contra el ESP32-C3 simulado")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    ap.add_argument("--openocd", default=None)
    ap.add_argument("--gdb", default=None)
    ap.add_argument("--elf", default=None, help="hello_world.elf de la compilacion de idf-v5.5")
    a = ap.parse_args()
    oblig = os.environ.get("OPENOCD_OBLIGATORIO") == "1"
    openocd, scripts = busca_openocd(a)
    if not openocd:
        if oblig:
            check(False, "OPENOCD_OBLIGATORIO=1 y no hay openocd-esp32 (con target/esp32c3.cfg)")
            sys.exit(1)
        print("  [OMITE] no hay openocd-esp32 (--openocd RUTA o $OPENOCD_ESP32)")
        sys.exit(0)
    print("--- openocd-esp32 contra placas/esp32c3_devkitm1.xml (target/esp32c3.cfg, sin tocar) ---")

    # O1 y O2: remote_bitbang
    s = Sim(a.sim, ["--openocd", "--flash-rapida"])
    try:
        if check(s.listo, "O1: el simulador sirve remote_bitbang en el %d" % s.puerto):
            cfg = cfg_rb(s.puerto)
            o = ocd(openocd, scripts, cfg, ["init", "halt", "resume", "shutdown"], 120)
            check("tap/device found: 0x00005c25" in o, "O1: el IDCODE por los pines MTMS-MTDO: 0x00005c25")
            check("Examination succeed" in o and "Chip revision v0.4" in o,
                  "O1: examine (DM 0.13, misa) y la revision del chip, de los eFuse: v0.4")
            check("Target halted" in o, "O1: halt")
            t = time.time()
            o = ocd(openocd, scripts, cfg, ["program_esp %s 0x10000 verify reset exit"
                                            % os.path.join(IDF, "blink", "blink.bin")], 600)
            check("** Programming Finished" in o and "** Verify OK **" in o,
                  "O2: program_esp de blink.bin en 0x10000 con verify (%.0f s)" % (time.time() - t))
            fin = time.time() + 60
            while time.time() < fin and "Turning the LED" not in s.texto():
                time.sleep(0.5)
            check("Turning the LED" in s.texto(), "O2: reset, y blink arranca")
            os.unlink(cfg)
    finally:
        log = s.fin()
        if fallos:
            print(log[-2000:])

    # O3: CMSIS-DAP
    s = Sim(a.sim, ["--cmsis-dap"])
    try:
        if check(s.listo, "O3: el simulador sirve CMSIS-DAP por TCP en el %d" % s.puerto):
            cfg = cfg_dap(s.puerto)
            o = ocd(openocd, scripts, cfg, ["init", "halt", "resume", "shutdown"], 120)
            if "cmsis-dap backend" in o and "invalid" in o.lower() and "tcp" in o:
                print("  [OMITE] O3: este OpenOCD no tiene `cmsis-dap backend tcp`")
            else:
                check("tap/device found: 0x00005c25" in o and "Examination succeed" in o and "Target halted" in o,
                      "O3: por CMSIS-DAP en JTAG: IDCODE, examine y halt")
            os.unlink(cfg)
    finally:
        s.fin()

    # O4: GDB contra G1, como idf.py gdb
    gdb = a.gdb or shutil.which("riscv32-esp-elf-gdb")
    if not gdb or not a.elf:
        print("  [OMITE] O4: hace falta riscv32-esp-elf-gdb y --elf hello_world.elf")
    else:
        s = Sim(a.sim, ["--gdb-dap"])
        try:
            if check(s.listo, "O4: el simulador sirve GDB (G1) en el %d" % s.puerto):
                r = subprocess.run([gdb, "-q", "-batch", "-ex", "set remotetimeout 30",
                                    "-ex", "target extended-remote localhost:%d" % s.puerto,
                                    "-ex", "monitor reset halt", "-ex", "maintenance flush register-cache",
                                    "-ex", "thbreak app_main", "-ex", "continue", "-ex", "bt", "-ex", "detach",
                                    a.elf], capture_output=True, text=True, timeout=300)
                o = r.stdout + r.stderr
                check("Temporary breakpoint 1, app_main ()" in o, "O4: monitor reset halt, thb app_main y continue: para en app_main")
                check("main_task" in o and "vPortTaskWrapper" in o, "O4: bt: app_main <- main_task <- vPortTaskWrapper")
        finally:
            s.fin()
    print("TOTAL openocd C3: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    sys.exit(1 if fallos else 0)


if __name__ == "__main__":
    main()
