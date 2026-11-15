#!/usr/bin/env python3
# =============================================================================
# placa_c6.py — La ESP32-C6-DevKitC-1 simulada con sus dos USB (E9.5 de
#               doc/analisis_esp32_riscv.md)
#
# El `mcu-sim` de verdad con placas/esp32c6_devkitc1.xml:
#
#   B1  la placa monta: el ESP32-C6 en QFN40, la Flash de 8 MB del modulo
#       en SPICS0, el puente USB-UART con el auto-reset, BOOT, RESET y el LED
#   B2  ESP-IDF v5.5 hello_world por su flash_args: la consola por los DOS
#       USB, el del puente (la UART0 en GPIO16/17, por RFC 2217) y el del
#       chip (el CDC-ACM del USB-Serial-JTAG, que el PC enumera, por RFC
#       2217): «Hello world!» y la cuenta atras en los dos; el chip ve la
#       Flash de 8 MB del modulo
#   B3  un ELF bare-metal (uart.elf) en la HP SRAM, sin ROM: su saludo por
#       la UART0
#   B4  sin firmware: la ROM arranca y lee la Flash en blanco
#
# Necesita pySerial (el CDC del USB y la UART por RFC 2217); sin el, B2 se
# OMITE, salvo con PYSERIAL_OBLIGATORIO=1.
#
#   python3 verif/esp/placa_c6.py [--sim build/mcu-sim]
#   make -f Makefile.mcu-sim placa-c6
# =============================================================================
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

PLACA = os.path.join("placas", "esp32c6_devkitc1.xml")
IDF = os.path.join("build", "idf-c6", "v5.5")
FW = os.path.join("verif", "fw", "esp32c6")
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


def corre(sim, args, plazo=600):
    r = subprocess.run([sim, PLACA] + args, capture_output=True, text=True, errors="replace", timeout=plazo)
    return r.returncode, r.stdout + r.stderr


def lee_hasta(s, fin_txt, plazo):
    d = b""
    fin = time.time() + plazo
    while time.time() < fin and fin_txt not in d:
        d += s.read(256)
    return d


def main():
    ap = argparse.ArgumentParser(description="La ESP32-C6-DevKitC-1 simulada")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    a = ap.parse_args()
    print("--- %s ---" % PLACA)

    # B3 y B1: uart.elf, sin red
    rc, o = corre(a.sim, [os.path.join(FW, "uart.elf"), "--serie", "UART=memoria", "--usb", "u0=memoria", "--ms=40"])
    check(rc == 0 and "placa 'esp32c6-devkitc-1': 1 MCU(s), 7 componentes" in o and " 0 avisos" in o,
          "B1: la placa monta: el C6, la Flash, el puente con el auto-reset, BOOT, RESET y el LED, sin avisos")
    check("[UART] Hola desde el ESP32-C6" in o, "B3: uart.elf en la HP SRAM, sin ROM: su saludo por GPIO16")
    # B4: sin firmware
    rc, o = corre(a.sim, ["--sin-firmware", "--serie", "UART=memoria", "--usb", "u0=memoria", "--ms=60"])
    check(rc == 0 and "ESP-ROM:esp32c6" in o and "invalid header" in o,
          "B4: sin firmware, la ROM arranca y la Flash en blanco no tiene cabecera")

    # B2: los dos USB por RFC 2217
    try:
        import serial
    except ImportError:
        if os.environ.get("PYSERIAL_OBLIGATORIO") == "1":
            check(False, "PYSERIAL_OBLIGATORIO=1 y no hay pySerial")
        else:
            print("  [OMITE] B2: no hay pySerial")
        return
    pu, pc = puerto_libre(), puerto_libre()
    log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
    p = subprocess.Popen([a.sim, PLACA, os.path.join(IDF, "hello_world", "flash_args"),
                          "--serie", "UART=rfc2217:%d" % pu, "--usb", "u0=rfc2217:%d" % pc,
                          "--ms=100000000"], stdout=log, stderr=subprocess.STDOUT)

    def texto():
        with open(log.name, errors="replace") as f:
            return f.read()
    try:
        t0 = time.time()
        while "esperando" not in texto() and "rfc2217" not in texto() and time.time() - t0 < 30:
            time.sleep(0.2)
        # Los dos terminales, abiertos con DTR y RTS quietos (no reinician)
        su = serial.serial_for_url("rfc2217://127.0.0.1:%d" % pu, do_not_open=True, baudrate=115200, timeout=1)
        su.dtr = False
        su.rts = False
        fin = time.time() + 30
        while True:
            try:
                su.open()
                break
            except serial.SerialException:
                if time.time() > fin:
                    raise
                time.sleep(0.5)
        t0 = time.time()
        while "[usb] enumerado" not in texto() and time.time() - t0 < 120:
            time.sleep(0.05)
        check("enumerado 303A:1001" in texto(), "B2: el PC enumera el USB del chip: 303A:1001, CDC-ACM")
        sc = serial.serial_for_url("rfc2217://127.0.0.1:%d" % pc, do_not_open=True, baudrate=115200, timeout=0.2)
        sc.dtr = True
        sc.rts = False
        sc.open()
        su.timeout = 0.2
        du, dc = b"", b""
        fin = time.time() + 180
        while time.time() < fin and not (b"Restarting in" in du and b"Restarting in" in dc):
            du += su.read(256)
            dc += sc.read(256)
        su.close()
        sc.close()
        check(b"Hello world!" in du and b"Restarting in" in du,
              "B2: la consola de ESP-IDF por el USB del puente (la UART0): \"Hello world!\" y la cuenta atras")
        check(b"Hello world!" in dc and b"Restarting in" in dc,
              "B2: y por el USB del chip (la secundaria, el USB-Serial-JTAG)")
        check(b"Detected size(8192k)" in du, "B2: el chip ve la Flash de 8 MB del modulo")
    finally:
        p.terminate()
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
        os.unlink(log.name)


if __name__ == "__main__":
    main()
    print("\nplaca-c6: %s" % ("BIEN" if fallos == 0 else "%d FALLOS" % fallos))
    sys.exit(1 if fallos else 0)
