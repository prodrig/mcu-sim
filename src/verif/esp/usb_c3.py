#!/usr/bin/env python3
# =============================================================================
# usb_c3.py — El USB-Serial-JTAG del ESP32-C3 simulado, con herramientas de
#             verdad (E7 de doc/analisis_esp32_riscv.md: D3 y G3)
#
# La SuperMini (placas/esp32c3_supermini.xml) solo tiene el USB-C del chip.
# El simulador hace de PC: enumera el USB-Serial-JTAG (303A:1001), lleva su
# CDC-ACM a RFC 2217 y su JTAG a un servidor remote_bitbang
# (--usb rfc2217:PUERTO,jtag=PUERTO2):
#
#   U1  esptool --before usb-reset por el CDC: chip-id con el stub y
#       write-flash de la tabla de particiones (con su MD5): el reset a modo
#       descarga lo hace el periferico con DTR/RTS, sin transistores, y la ROM
#       y el stub hablan por el USB; despues hello_world vuelve a arrancar.
#       PENDIENTE: un write-flash grande (el bootloader entero) se queda sin
#       respuesta al FLASH_DEFL_END (doc/analisis_esp32_riscv.md, E7)
#   U2  la consola de ESP-IDF por el USB (la secundaria de hello_world):
#       el puerto abierto con DTR activo y RTS no, como un terminal que no
#       quiere reiniciar
#   U3  openocd-esp32 por G3 (el JTAG del USB-Serial-JTAG, de fabrica):
#       IDCODE, examine y halt; se omite sin openocd-esp32
#
# Sin esptool se OMITE, salvo con ESPTOOL_OBLIGATORIO=1.
#
#   python3 verif/esp/usb_c3.py [--sim build/mcu-sim] [--esptool RUTA] [--openocd RUTA]
#   make -f Makefile.mcu-sim usb-c3
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


def main():
    ap = argparse.ArgumentParser(description="El USB-Serial-JTAG del ESP32-C3 simulado")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    ap.add_argument("--esptool", default=None)
    ap.add_argument("--openocd", default=None)
    a = ap.parse_args()
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import esptool_c3 as ec
    import openocd_c3 as oc
    et = ec.busca_esptool(a)
    if not et:
        if os.environ.get("ESPTOOL_OBLIGATORIO") == "1":
            check(False, "ESPTOOL_OBLIGATORIO=1 y no hay esptool")
            sys.exit(1)
        print("  [OMITE] no hay esptool")
        sys.exit(0)
    import serial
    print("--- el USB-Serial-JTAG de placas/esp32c3_supermini.xml, por RFC 2217 y remote_bitbang ---")
    pu, pj = puerto_libre(), puerto_libre()
    url = "rfc2217://127.0.0.1:%d" % pu
    log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
    p = subprocess.Popen([a.sim, "placas/esp32c3_supermini.xml", os.path.join(IDF, "hello_world", "flash_args"),
                          "--usb", "rfc2217:%d,jtag=%d" % (pu, pj), "--ms=100000000"],
                         stdout=log, stderr=subprocess.STDOUT)

    def texto():
        with open(log.name, errors="replace") as f:
            return f.read()
    try:
        t0 = time.time()
        while "[usb] enumerado" not in texto() and time.time() - t0 < 60 and p.poll() is None:
            time.sleep(0.2)
        check("enumerado 303A:1001" in texto(), "el PC enumera el USB-Serial-JTAG: 303A:1001, CDC-ACM")
        # U2 primero: hello_world esta en la Flash
        s = serial.serial_for_url(url, do_not_open=True, baudrate=115200, timeout=1)
        s.dtr = True
        s.rts = False
        s.open()
        d = b""
        fin = time.time() + 60
        while time.time() < fin and b"Restarting in" not in d:
            d += s.read(256)
        s.close()
        check(b"Hello world!" in d and b"Restarting in" in d,
              "U2: la consola de ESP-IDF por el USB: \"Hello world!\" y la cuenta atras")
        # U3: openocd-esp32 por el JTAG del USB
        openocd, scripts = oc.busca_openocd(a)
        if not openocd:
            print("  [OMITE] U3: no hay openocd-esp32")
        else:
            cfg = oc.cfg_rb(pj)
            o = oc.ocd(openocd, scripts, cfg, ["init", "halt", "resume", "shutdown"], 300)
            os.unlink(cfg)
            check("tap/device found: 0x00005c25" in o and "Examination succeed" in o and "Target halted" in o,
                  "U3: openocd-esp32 por G3 (el JTAG del USB): IDCODE, examine y halt")
        # U1: esptool por el CDC, con el reset del periferico
        t = time.time()
        r = subprocess.run(et + ["--port", url, "--before", "usb-reset", "chip-id"],
                           capture_output=True, text=True, timeout=300)
        o = r.stdout + r.stderr
        check(r.returncode == 0 and "Chip type:          ESP32-C3" in o and "Stub flasher running" in o,
              "U1: esptool --before usb-reset chip-id por el USB, con el stub (%.0f s)" % (time.time() - t))
        check("rst:0x15 (USB_UART_CHIP_RESET),boot:0x4" in texto(),
              "U1: el reset por DTR/RTS del periferico: rst:0x15, boot:0x4 (descarga)")
        t = time.time()
        r = subprocess.run(et + ["--port", url, "--before", "usb-reset", "write-flash", "0x8000",
                                 os.path.join("partition_table", "partition-table.bin")],
                           capture_output=True, text=True, cwd=os.path.join(IDF, "blink"), timeout=300)
        o = r.stdout + r.stderr
        check(r.returncode == 0 and "Hash of data verified" in o,
              "U1: write-flash de la tabla de particiones por el USB, con su MD5 (%.0f s)" % (time.time() - t))
        n0 = texto().count("Hello world!")
        fin = time.time() + 60
        while time.time() < fin and texto().count("Hello world!") <= n0:
            time.sleep(0.5)
        check(texto().count("Hello world!") > n0, "U1: y tras el reset, hello_world vuelve a arrancar")
    finally:
        p.terminate()
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
        if fallos:
            print(texto()[-2500:])
        os.unlink(log.name)
    print("TOTAL usb C3: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    sys.exit(1 if fallos else 0)


if __name__ == "__main__":
    main()
