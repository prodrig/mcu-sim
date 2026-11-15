#!/usr/bin/env python3
# =============================================================================
# openocd_c6.py — openocd-esp32 y GDB de verdad contra el ESP32-C6 simulado
#
# E9.6 de doc/analisis_esp32_riscv.md (G1, G2, G3). El simulador, con la
# DevKitC-1 y el JTAG en los pines (--jtag=pines: como quemar
# EFUSE_DIS_USB_JTAG) o en su USB, y target/esp32c6.cfg de openocd-esp32 SIN
# TOCAR:
#
#   O1  por remote_bitbang (--openocd): el IDCODE 0x0000DC25, examine
#       (misa, la revision del chip por los eFuse) y halt
#   O2  program_esp de hello_world entero (bootloader, particiones y app) en
#       la Flash EN BLANCO, con verify, y reset: hello_world arranca
#   O3  lo mismo que O1 por la sonda CMSIS-DAP por TCP en JTAG (--cmsis-dap):
#       hace falta un OpenOCD con `cmsis-dap backend tcp`
#   O4  GDB (riscv32-esp-elf-gdb) contra el stub interno G1 (--gdb-dap), como
#       `idf.py gdb`: monitor reset halt, thb app_main, continue, bt. Necesita
#       el ELF de la compilacion de hello_world (--elf): no se versiona
#   O5  GDB A TRAVES de openocd-esp32 (remote_bitbang): monitor reset halt,
#       thb app_main, continue, bt e `info threads` con las tareas de
#       FreeRTOS (main e IDLE), que OpenOCD saca de la memoria del chip
#   O6  G3: el JTAG del USB-Serial-JTAG (de fabrica, sin --jtag), por el
#       remote_bitbang del puente USB: IDCODE, examine y halt
# E10, EL NUCLEO LP (con lp_core/lp_timer_interrupt de ESP-IDF, el LP en
# marcha con su alarma):
#   O7  target/esp32c6-lpcore.cfg de openocd-esp32, sin tocar (HP y LP en SMP
#       por el mismo TAP): examina los dos harts, halt, y el pc del LP esta en
#       la LP SRAM
#   O8  GDB a traves de ese OpenOCD (RTOS hwthread): `info threads` con los
#       dos nucleos, esp32c6.hp.cpu0 y esp32c6.lp.cpu, y el pc del segundo
#   O9  GDB contra el stub interno G1 (--gdb-dap), sin ELF: `info threads`
#       con dos hilos, uno por hart, y el pc del LP en la LP SRAM
#
# --flash-rapida deja los tiempos de programar y borrar la Flash al 1 %: el
# flasher de openocd-esp32 espera con plazos de reloj de pared.
#
# Sin openocd-esp32 (--openocd, $OPENOCD_ESP32, o un `openocd` del PATH que
# tenga target/esp32c6.cfg) la prueba se OMITE diciendolo, salvo con
# OPENOCD_OBLIGATORIO=1. Sin GDB o sin --elf, O4 se omite.
#
#   python3 verif/esp/openocd_c6.py [--sim build/mcu-sim] [--openocd RUTA]
#                                   [--gdb riscv32-esp-elf-gdb] [--elf hello_world.elf]
#   make -f Makefile.mcu-sim openocd-c6 OPENOCD_ESP32=/ruta/a/openocd
# =============================================================================
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

IDF = os.path.join("build", "idf-c6", "v5.5")
PLACA = os.path.join("placas", "esp32c6_devkitc1.xml")
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
    def __init__(self, sim, extra, fw=None, sondea=True):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
        self.puerto = puerto_libre()
        args = [sim, PLACA, fw or os.path.join(IDF, "hello_world", "flash_args"),
                "--serie", "UART=memoria", "--usb", "u0=memoria", "--jtag=pines", "--port=%d" % self.puerto,
                "--ms=100000000"] + extra
        self.p = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)
        t0 = time.time()
        self.listo = False
        # Sin sondear (sondea=False): una conexion al puerto de GDB para el
        # chip al conectarse, y se quedaria parado
        if not sondea:
            time.sleep(1)
            self.listo = self.p.poll() is None
        while sondea and time.time() - t0 < 30 and self.p.poll() is None:
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
            "transport select jtag\nsource [find target/esp32c6.cfg]\n" % puerto)
    f.close()
    return f.name


def cfg_lp(puerto):
    f = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
    f.write("adapter driver remote_bitbang\nremote_bitbang host localhost\nremote_bitbang port %d\n"
            "transport select jtag\nsource [find target/esp32c6-lpcore.cfg]\n" % puerto)
    f.close()
    return f.name


def espera_lp(s, plazo=180):
    """Hasta que ESP-IDF ha arrancado el LP (lo dice por la consola)"""
    fin = time.time() + plazo
    while time.time() < fin and "LP core loaded with firmware" not in s.texto():
        time.sleep(0.5)
    time.sleep(1)
    return "LP core loaded with firmware" in s.texto()


def en_lp_sram(t, tras):
    i = t.find(tras)
    if i < 0:
        return False
    m = re.match(r"\s*(0x[0-9a-fA-F]+)", t[i + len(tras):])
    if not m:
        return False
    v = int(m.group(1), 16)
    return 0x50000000 <= v < 0x50004000


def cfg_dap(puerto):
    f = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
    f.write("adapter driver cmsis-dap\ncmsis-dap backend tcp\ncmsis-dap tcp host localhost\n"
            "cmsis-dap tcp port %d\ntransport select jtag\nadapter speed 5000\n"
            "source [find target/esp32c6.cfg]\n" % puerto)
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
    ap = argparse.ArgumentParser(description="openocd-esp32 y GDB contra el ESP32-C6 simulado")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    ap.add_argument("--openocd", default=None)
    ap.add_argument("--gdb", default=None)
    ap.add_argument("--elf", default=None, help="hello_world.elf de la compilacion de idf-v5.5")
    a = ap.parse_args()
    oblig = os.environ.get("OPENOCD_OBLIGATORIO") == "1"
    openocd, scripts = busca_openocd(a)
    if not openocd:
        if oblig:
            check(False, "OPENOCD_OBLIGATORIO=1 y no hay openocd-esp32 (con target/esp32c6.cfg)")
            sys.exit(1)
        print("  [OMITE] no hay openocd-esp32 (--openocd RUTA o $OPENOCD_ESP32)")
        sys.exit(0)
    print("--- openocd-esp32 contra %s (target/esp32c6.cfg, sin tocar) ---" % PLACA)

    # O1 y O2: remote_bitbang
    s = Sim(a.sim, ["--openocd", "--flash-rapida"], fw="--sin-firmware")
    try:
        if check(s.listo, "O1: el simulador sirve remote_bitbang en el %d" % s.puerto):
            cfg = cfg_rb(s.puerto)
            o = ocd(openocd, scripts, cfg, ["init", "halt", "resume", "shutdown"], 120)
            check("tap/device found: 0x0000dc25" in o, "O1: el IDCODE por los pines MTMS-MTDO: 0x0000dc25")
            check("Examination succeed" in o and "Chip revision v0.2" in o,
                  "O1: examine (DM 0.13, misa) y la revision del chip, de los eFuse: v0.2")
            check("Target halted" in o, "O1: halt")
            hw = os.path.join(IDF, "hello_world")
            t = time.time()
            o = ocd(openocd, scripts, cfg, ["program_esp %s 0x0 verify" % os.path.join(hw, "bootloader", "bootloader.bin"),
                                            "program_esp %s 0x8000 verify" % os.path.join(hw, "partition_table", "partition-table.bin"),
                                            "program_esp %s 0x10000 verify reset exit" % os.path.join(hw, "hello_world.bin")], 900)
            check(o.count("** Programming Finished") == 3 and o.count("** Verify OK **") == 3,
                  "O2: program_esp de hello_world entero (3 imagenes) con verify (%.0f s)" % (time.time() - t))
            if o.count("** Verify OK **") != 3:
                print("\n".join(l for l in o.splitlines() if "Error" in l or "rror" in l)[-1500:])
            fin = time.time() + 120
            while time.time() < fin and "Hello world!" not in s.texto():
                time.sleep(0.5)
            check("Hello world!" in s.texto(), "O2: reset, y hello_world arranca")
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
                check("tap/device found: 0x0000dc25" in o and "Examination succeed" in o and "Target halted" in o,
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
    # O5: GDB a traves de OpenOCD, con FreeRTOS
    if gdb and a.elf:
        s = Sim(a.sim, ["--openocd"])
        po = None
        try:
            if check(s.listo, "O5: el simulador sirve remote_bitbang en el %d" % s.puerto):
                cfg = cfg_rb(s.puerto)
                pg = puerto_libre()
                po = subprocess.Popen([openocd] + (["-s", scripts] if scripts else []) +
                                      ["-f", cfg, "-c", "gdb port %d" % pg, "-c", "telnet port disabled",
                                       "-c", "tcl port disabled"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(3)
                r = subprocess.run([gdb, "-q", "-batch", "-ex", "set remotetimeout 60",
                                    "-ex", "target extended-remote localhost:%d" % pg,
                                    "-ex", "monitor reset halt", "-ex", "maintenance flush register-cache",
                                    "-ex", "thbreak app_main", "-ex", "continue", "-ex", "bt 3",
                                    "-ex", "info threads", "-ex", "detach", a.elf],
                                   capture_output=True, text=True, timeout=600)
                o = r.stdout + r.stderr
                check("hit Temporary breakpoint 1, app_main ()" in o,
                      "O5: GDB por openocd-esp32: reset halt, thb app_main y continue: para en app_main")
                check('"main" (Name: main)' in o and '"IDLE" (Name: IDLE)' in o,
                      "O5: info threads: las tareas de FreeRTOS (main e IDLE)")
                os.unlink(cfg)
        finally:
            if po:
                po.terminate()
                po.wait(10)
            s.fin()
    # O6: G3, el JTAG del USB-Serial-JTAG (de fabrica)
    pj = puerto_libre()
    log6 = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
    p6 = subprocess.Popen([a.sim, PLACA, os.path.join(IDF, "hello_world", "flash_args"), "--serie", "UART=memoria",
                           "--usb", "u0=memoria,jtag=%d" % pj, "--ms=100000000"], stdout=log6, stderr=subprocess.STDOUT)
    try:
        t0 = time.time()
        while time.time() - t0 < 60 and "[usb] enumerado" not in open(log6.name, errors="replace").read():
            time.sleep(0.2)
        cfg = cfg_rb(pj)
        o = ocd(openocd, scripts, cfg, ["init", "halt", "resume", "shutdown"], 300)
        os.unlink(cfg)
        check("tap/device found: 0x0000dc25" in o and "Examination succeed" in o and "Target halted" in o,
              "O6: G3, por el JTAG del USB-Serial-JTAG (de fabrica): IDCODE, examine y halt")
    finally:
        p6.terminate()
        try:
            p6.wait(10)
        except subprocess.TimeoutExpired:
            p6.kill()
        os.unlink(log6.name)
    # O7-O9: el nucleo LP (E10)
    lp = os.path.join(IDF, "lp_lp_timer_interrupt", "flash_args")
    if not os.path.exists(os.path.join(scripts or "", "target", "esp32c6-lpcore.cfg")):
        print("  [OMITE] O7-O8: este OpenOCD no tiene target/esp32c6-lpcore.cfg")
    else:
        s = Sim(a.sim, ["--openocd"], fw=lp)
        try:
            if check(s.listo, "O7: el simulador sirve remote_bitbang en el %d, con lp_timer_interrupt" % s.puerto):
                espera_lp(s)
                cfg = cfg_lp(s.puerto)
                o = ocd(openocd, scripts, cfg, ["init", "sleep 2000", "halt", "targets esp32c6.lp.cpu", "reg pc",
                                                "resume", "shutdown"], 300)
                check("[esp32c6.hp.cpu0] Examination succeed" in o and "[esp32c6.lp.cpu] Examination succeed" in o,
                      "O7: target/esp32c6-lpcore.cfg: los dos harts por el mismo TAP, examinados")
                if not check(en_lp_sram(o, "[esp32c6.lp.cpu] Target halted, PC=") and en_lp_sram(o, "pc (/32): "),
                             "O7: halt de los dos; el pc del LP, en la LP SRAM"):
                    print(o[-1500:])
                os.unlink(cfg)
        finally:
            s.fin()
        if gdb:
            s = Sim(a.sim, ["--openocd"], fw=lp)
            po = None
            try:
                if check(s.listo, "O8: el simulador sirve remote_bitbang en el %d" % s.puerto):
                    espera_lp(s)
                    cfg = cfg_lp(s.puerto)
                    pg = puerto_libre()
                    po = subprocess.Popen([openocd] + (["-s", scripts] if scripts else []) +
                                          ["-f", cfg, "-c", "gdb port %d" % pg, "-c", "telnet port disabled",
                                           "-c", "tcl port disabled"], stdout=subprocess.PIPE,
                                          stderr=subprocess.STDOUT, text=True)
                    # Hasta que OpenOCD ha examinado los dos y escucha a GDB
                    fin = time.time() + 120
                    vio = ""
                    while time.time() < fin and "for gdb connections" not in vio and po.poll() is None:
                        vio += po.stdout.readline()
                    threading.Thread(target=lambda: [None for _ in po.stdout], daemon=True).start()
                    r = subprocess.run([gdb, "-q", "-batch", "-ex", "set remotetimeout 60",
                                        "-ex", "target extended-remote localhost:%d" % pg, "-ex", "info threads",
                                        "-ex", "thread 2", "-ex", "p/x $pc", "-ex", "detach"],
                                       capture_output=True, text=True, timeout=300)
                    o = r.stdout + r.stderr
                    if not check('"esp32c6.hp.cpu0"' in o and '"esp32c6.lp.cpu"' in o,
                                 "O8: GDB por openocd-esp32 (hwthread): info threads, el HP y el LP"):
                        print(vio[-1500:] + o[-1500:])
                    check(en_lp_sram(o, "$1 = "), "O8: thread 2: el pc del LP, en la LP SRAM")
                    os.unlink(cfg)
            finally:
                if po:
                    po.terminate()
                    po.wait(10)
                s.fin()
    if gdb:
        s = Sim(a.sim, ["--gdb-dap"], fw=lp, sondea=False)
        try:
            if check(s.listo and espera_lp(s), "O9: el simulador sirve GDB (G1) en el %d, con lp_timer_interrupt"
                     " y el LP en marcha" % s.puerto):
                r = subprocess.run([gdb, "-q", "-batch", "-ex", "set remotetimeout 30",
                                    "-ex", "target extended-remote localhost:%d" % s.puerto, "-ex", "info threads",
                                    "-ex", "thread 2", "-ex", "p/x $pc", "-ex", "detach"],
                                   capture_output=True, text=True, timeout=300)
                o = r.stdout + r.stderr
                check("Thread 1 " in o and "Thread 2 " in o, "O9: GDB contra G1: info threads, dos hilos (un hart cada uno)")
                if not check(en_lp_sram(o, "$1 = "), "O9: thread 2: el pc del LP, en la LP SRAM"):
                    print(o[-1500:])
        finally:
            s.fin()
    print("TOTAL openocd C6: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    sys.exit(1 if fallos else 0)


if __name__ == "__main__":
    main()
