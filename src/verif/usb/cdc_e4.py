#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =============================================================================
# cdc_e4.py — El puerto serie USB del MSP432E401Y desde un terminal de verdad:
#             e4_usb (CDC-ACM) por el puente de clase de `mcu-sim --usb`
#
# El puente (parts/puente_usb.h) hace de PC: enumera el CDC del firmware y lo
# saca a un puerto del puente serie. Este programa es el terminal al otro lado:
#
#   * con RFC 2217 y pySerial (`serial_for_url("rfc2217://...")`), como lo
#     abriria miniterm: los baudios, la paridad y los bits de parada que se
#     ponen en pySerial LLEGAN al firmware como SET_LINE_CODING, y DTR y RTS
#     como SET_CONTROL_LINE_STATE. Se comprueba preguntandole al propio
#     firmware: un "?" solo le hace contestar con lo que ha recibido;
#   * con TCP en crudo, con un socket: el eco en mayusculas, y un bloque de
#     4 KB que tiene que volver entero y en orden.
#
#   python3 cdc_e4.py --prueba [--sim B]   arranca build/mcu-sim con la placa
#                                          y e4_usb, las dos veces
#   python3 cdc_e4.py PUERTO               contra un mcu-sim que ya escucha
#                                          en rfc2217:PUERTO
#   python3 cdc_e4.py --prueba --f3        lo mismo con la STM32F3DISCOVERY y
#                                          f3_usb (TinyUSB), que habla igual (S6)
# =============================================================================
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

FW = "verif/fw/msp432e4/e4_usb/e4_usb.out"
PLACA = "placas/msp_exp432e401y.xml"
NOMBRE = "e4_usb"
# El del STM32F303 (S6): el mismo protocolo, con TinyUSB
FW_F3 = "verif/fw/stm32f3/f3_usb/f3_usb.bin"
PLACA_F3 = "placas/stm32f3discovery.xml"


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def lee_hasta(leer, n, plazo):
    """Lee hasta tener n bytes o hasta el plazo (s)"""
    fin = time.time() + plazo
    d = b""
    while len(d) < n and time.time() < fin:
        d += leer(n - len(d))
    return d


def con_pyserial(puerto, check):
    import serial
    s = serial.serial_for_url("rfc2217://127.0.0.1:%d?timeout=5" % puerto, baudrate=230400,
                              parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, timeout=0.5)
    try:
        time.sleep(0.3)                                     # que lleguen SET_LINE_CODING y las lineas
        s.reset_input_buffer()
        s.write(b"?")
        r = s.readline()
        check(r == b"230400 8N1 3\r\n", "pySerial abre a 230400 8N1: el firmware ha recibido 230400 8N1 y "
              "DTR+RTS (%r)" % r)
        s.write(b"hola desde pyserial")
        r = lee_hasta(s.read, 19, 5)
        check(r == b"HOLA DESDE PYSERIAL", "el eco del CDC por RFC 2217 (%r)" % r)
        s.baudrate = 9600
        s.parity = serial.PARITY_EVEN
        s.stopbits = serial.STOPBITS_TWO
        s.dtr = False
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"?")
        r = s.readline()
        check(r == b"9600 8E2 2\r\n", "cambiar baudios, paridad, parada y DTR en pySerial: SET_LINE_CODING "
              "9600 8E2 y SET_CONTROL_LINE_STATE 2 en el firmware (%r)" % r)
    finally:
        s.close()


def con_socket(puerto, check):
    c = socket.create_connection(("127.0.0.1", puerto), timeout=10)
    c.settimeout(0.5)

    def leer(n):
        try:
            return c.recv(n)
        except socket.timeout:
            return b""
    try:
        time.sleep(0.3)
        c.sendall(b"tcp en crudo")
        r = lee_hasta(leer, 12, 5)
        check(r == b"TCP EN CRUDO", "el eco del CDC por TCP en crudo (%r)" % r)
        blq = bytes((ord("a") + i % 26) for i in range(4096))
        t0 = time.time()
        c.sendall(blq)
        r = lee_hasta(leer, 4096, 30)
        check(r == blq.upper(), "4096 bytes de ida y vuelta, enteros y en orden (%d recibidos, %.2f s)"
              % (len(r), time.time() - t0))
    finally:
        c.close()


def arranca(sim, destino, log):
    args = [sim, PLACA, FW, "--usb", destino, "--tiempo-real"]
    return subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)


def espera_enumerado(nombre_log, plazo=30):
    fin = time.time() + plazo
    while time.time() < fin:
        with open(nombre_log) as f:
            if "[usb] enumerado" in f.read():
                return True
        time.sleep(0.1)
    return False


def main():
    ap = argparse.ArgumentParser(description="El CDC-ACM del MSP432E401Y por el puente USB")
    ap.add_argument("puerto", nargs="?", type=int)
    ap.add_argument("--prueba", action="store_true")
    ap.add_argument("--f3", action="store_true", help="la STM32F3DISCOVERY con f3_usb")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    a = ap.parse_args()
    global FW, PLACA, NOMBRE
    if a.f3:
        FW, PLACA, NOMBRE = FW_F3, PLACA_F3, "f3_usb"
    fallos = 0

    def check(cond, texto):
        nonlocal fallos
        print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
        if not cond:
            fallos += 1
        return cond

    print("--- el CDC-ACM de %s por el puente USB de clase (U2) ---" % NOMBRE)
    if not a.prueba:
        if not a.puerto:
            ap.error("falta el PUERTO (o --prueba)")
        con_pyserial(a.puerto, check)
    else:
        for modo, quien in (("rfc2217", con_pyserial), ("tcp", con_socket)):
            puerto = puerto_libre()
            log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
            p = arranca(a.sim, "%s:%d" % (modo, puerto), log)
            try:
                if check(espera_enumerado(log.name), "%s: el puente enumera el CDC de %s" % (modo, NOMBRE)):
                    quien(puerto, check)
            except (OSError, ImportError) as e:
                check(False, "%s: %s" % (modo, e))
            finally:
                p.terminate()
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
                log.close()
                if fallos:
                    print(open(log.name).read()[-3000:])
                os.unlink(log.name)
    print("TOTAL cdc %s: %s" % (NOMBRE, "todo OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
