#!/usr/bin/env python3
# =============================================================================
# esptool_c6.py — esptool DE VERDAD contra el ESP32-C6 simulado, por sus dos
#                 USB (E9.6 de doc/analisis_esp32_riscv.md: D2 y D3)
#
# La ESP32-C6-DevKitC-1 (placas/esp32c6_devkitc1.xml), con la Flash de 8 MB
# del modulo EN BLANCO (--sin-firmware): la ROM rev0 del C6 de verdad.
#
#   D2, por el USB del puente (la UART0, con DTR/RTS y el auto-reset):
#     1. chip-id con el stub a 115200: el reset a descarga, el SYNC, los eFuse
#        (ESP32-C6, QFN40, v0.2) y el stub en marcha
#     2. flash-id --no-stub: el JEDEC de la Flash del modulo (c8/4017, 8 MB)
#     3. write-flash a 921600 con el stub: hello_world entero (bootloader,
#        tabla de particiones y aplicacion), comprimido, con su MD5
#     4. verify-flash --no-stub: el MD5 de la aplicacion, por la ROM
#     5. write-flash --no-stub a 921600 de la tabla de particiones, por la ROM
#     6. y el arranque: «Hello world!» por el mismo puerto
#   D3, por el USB del chip (el CDC-ACM del USB-Serial-JTAG, sin transistores):
#     7. chip-id --before usb-reset con el stub: el reset lo hace el
#        periferico (rst:0x15, boot:0x4), y la ROM y el stub hablan por el USB
#     8. write-flash de la tabla de particiones por el USB, y hello_world
#        vuelve a arrancar
#
# Los plazos de esptool son de reloj de pared y estan hechos para el chip de
# verdad: el stub del C6 descomprime y escribe cada bloque de 16 KB DENTRO de
# su interrupcion de la UART, y el simulador tarda en eso unos 3 s de reloj
# de pared, justo el plazo de FLASH_DEFL_DATA. Si vence, esptool reenvia el
# bloque mientras la interrupcion sigue ocupada, la FIFO de la UART se
# desborda (como se desbordaria en la placa) y la descarga se pierde. Por eso
# la prueba le da a esptool un fichero de configuracion (ESPTOOL_CFGFILE) con
# plazos mas largos, y --flash-rapida (programar y borrar la Flash al 1 %),
# como usb_c3.py.
#
# Sin esptool se OMITE, salvo con ESPTOOL_OBLIGATORIO=1.
#
#   python3 verif/esp/esptool_c6.py [--sim build/mcu-sim] [--esptool RUTA]
#   make -f Makefile.mcu-sim esptool-c6
# =============================================================================
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from esptool_c3 import busca_esptool, puerto_libre  # noqa: E402

PLACA = os.path.join("placas", "esp32c6_devkitc1.xml")
IDF = os.path.join("build", "idf-c6", "v5.5")


def prueba(a):
    fallos = 0

    def check(cond, texto):
        nonlocal fallos
        print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
        if not cond:
            fallos += 1
        return cond

    et = busca_esptool(a)
    if not et:
        if os.environ.get("ESPTOOL_OBLIGATORIO") == "1":
            check(False, "ESPTOOL_OBLIGATORIO=1 y no hay esptool")
            return 1
        print("  [OMITE] no hay esptool (pip install esptool, o --esptool RUTA)")
        return 0
    try:
        import serial
    except ImportError:
        if os.environ.get("ESPTOOL_OBLIGATORIO") == "1":
            check(False, "ESPTOOL_OBLIGATORIO=1 y no hay pySerial")
            return 1
        print("  [OMITE] falta pySerial")
        return 0
    hw = os.path.join(IDF, "hello_world")
    if not os.path.exists(os.path.join(hw, "flash_args")):
        check(False, "falta %s: make -f Makefile.mcu-sim build/idf-c6/.extraido" % hw)
        return 1
    ver = subprocess.run(et + ["version"], capture_output=True, text=True).stdout.strip().splitlines()
    print("--- esptool (%s) contra %s, por sus dos USB ---" % (ver[-1] if ver else "?", PLACA))

    pu, pc = puerto_libre(), puerto_libre()
    uart, usb = "rfc2217://127.0.0.1:%d" % pu, "rfc2217://127.0.0.1:%d" % pc
    log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    p = subprocess.Popen([a.sim, PLACA, "--sin-firmware", "--flash-rapida", "--serie", "UART=rfc2217:%d" % pu,
                          "--usb", "u0=rfc2217:%d" % pc], stdout=log, stderr=subprocess.STDOUT)

    def texto():
        with open(log.name, errors="replace") as f:
            return f.read()

    cfg = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
    cfg.write("[esptool]\ntimeout = 20\nmem_end_rom_timeout = 2\nerase_write_timeout_per_mb = 200\n"
              "md5_timeout_per_mb = 40\n")
    cfg.close()
    entorno = dict(os.environ, ESPTOOL_CFGFILE=cfg.name)

    def esptool(url, *args, cwd=None, plazo=600):
        r = subprocess.run(et + ["--port", url] + list(args), capture_output=True, text=True, cwd=cwd,
                           timeout=plazo, env=entorno)
        if a.v or r.returncode:
            print((r.stdout + r.stderr)[-1500:])
        return r.returncode, r.stdout + r.stderr

    def lee(url, hasta, plazo=90, dtr=False):
        s = serial.serial_for_url(url, do_not_open=True, baudrate=115200, timeout=1)
        s.dtr = dtr
        s.rts = False
        s.open()
        d = b""
        fin = time.time() + plazo
        while time.time() < fin and hasta not in d:
            d += s.read(256)
        s.close()
        return d

    try:
        t0 = time.time()
        while True:
            if p.poll() is not None or time.time() - t0 > 30:
                check(False, "el simulador escucha en el %d" % pu)
                return 1
            try:
                socket.create_connection(("127.0.0.1", pu), timeout=1).close()
                break
            except OSError:
                time.sleep(0.1)
        time.sleep(0.5)

        # --- D2: la UART0, por el puente ---
        t = time.time()
        rc, o = esptool(uart, "chip-id")
        check(rc == 0 and "Chip type:          ESP32-C6 (QFN40) (revision v0.2)" in o and
              "Stub flasher running" in o,
              "D2 chip-id (stub, 115200): ESP32-C6 (QFN40) v0.2, el stub en marcha (%.0f s)" % (time.time() - t))

        t = time.time()
        rc, o = esptool(uart, "--no-stub", "flash-id")
        check(rc == 0 and "Manufacturer: c8" in o and "Device: 4017" in o and "8MB" in o,
              "D2 flash-id --no-stub: la Flash del modulo, JEDEC c8/4017, 8 MB (%.0f s)" % (time.time() - t))

        t = time.time()
        rc, o = esptool(uart, "-b", "921600", "write-flash", "@flash_args", cwd=hw)
        check(rc == 0 and o.count("Hash of data verified") == 3 and "Changing baud rate to 921600" in o,
              "D2 write-flash a 921600 con el stub: hello_world (3 imagenes, MD5 bien) (%.0f s)" % (time.time() - t))

        t = time.time()
        rc, o = esptool(uart, "--no-stub", "verify-flash", "0x10000", "hello_world.bin", cwd=hw)
        check(rc == 0 and "verification successful" in o.lower(),
              "D2 verify-flash --no-stub: el MD5 de la ROM cuadra con hello_world.bin (%.0f s)" % (time.time() - t))

        t = time.time()
        rc, o = esptool(uart, "--no-stub", "-b", "921600", "write-flash", "0x8000",
                        os.path.join("partition_table", "partition-table.bin"), cwd=hw)
        check(rc == 0 and "Hash of data verified" in o and "Hard resetting via RTS pin" in o,
              "D2 write-flash --no-stub a 921600: la tabla de particiones, por la ROM (%.0f s)" % (time.time() - t))

        d = lee(uart, b"Hello world!")
        check(b"Hello world!" in d, "D2 y hello_world arranca: \"Hello world!\" por la UART0")

        # --- D3: el USB-Serial-JTAG ---
        t0 = time.time()
        while "[usb] enumerado" not in texto() and time.time() - t0 < 60:
            time.sleep(0.2)
        check("enumerado 303A:1001" in texto(), "D3 el PC enumera el USB del chip: 303A:1001")
        t = time.time()
        rc, o = esptool(usb, "--before", "usb-reset", "chip-id")
        check(rc == 0 and "Chip type:          ESP32-C6" in o and "Stub flasher running" in o,
              "D3 chip-id --before usb-reset por el USB, con el stub (%.0f s)" % (time.time() - t))
        check("rst:0x15 (USB_UART_HPSYS),boot:0x4" in texto() or "rst:0x15" in texto(),
              "D3 el reset del periferico por DTR/RTS: rst:0x15, a descarga")
        t = time.time()
        rc, o = esptool(usb, "--before", "usb-reset", "write-flash", "0x8000",
                        os.path.join("partition_table", "partition-table.bin"), cwd=hw)
        check(rc == 0 and "Hash of data verified" in o,
              "D3 write-flash de la tabla de particiones por el USB (%.0f s)" % (time.time() - t))
        d = lee(usb, b"Hello world!", dtr=True)
        check(b"Hello world!" in d, "D3 y hello_world vuelve a arrancar: \"Hello world!\" por el USB")
    except subprocess.TimeoutExpired as e:
        check(False, "esptool no acaba: %s" % e)
    finally:
        p.terminate()
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
        log.close()
        os.unlink(cfg.name)
        if fallos:
            print(open(log.name, errors="replace").read()[-3000:])
        os.unlink(log.name)
    print("TOTAL esptool C6: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


def main():
    ap = argparse.ArgumentParser(description="esptool contra el ESP32-C6 simulado")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    ap.add_argument("--esptool", default=None)
    ap.add_argument("-v", action="store_true", help="lo que dice esptool, entero")
    a = ap.parse_args()
    sys.exit(prueba(a))


if __name__ == "__main__":
    main()
