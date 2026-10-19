#!/usr/bin/env python3
# =============================================================================
# sflash_e4.py — Programar un MSP432E4 por su cargador serie de la ROM, como
#                `sflash` de TivaWare, contra el simulador o contra un chip
#
# El cargador de la ROM R1 del simulador (verif/fw/msp432e4/rom_r1/cargador.c)
# habla el protocolo serie de las herramientas de TI [SPMU301, «TivaWare Boot
# Loader User's Guide»]: dos 0x55 para medir los baudios, paquetes con
# tamano y suma, y las ordenes PING, DOWNLOAD, SEND_DATA, GET_STATUS, RUN y
# RESET. Lo hablan `sflash` y LM Flash Programmer de TI; esto es lo mismo en
# Python con pySerial, para no depender de ellos. Con un chip de verdad sirve
# igual: su ROM habla el mismo protocolo.
#
#   python3 verif/serie/sflash_e4.py PUERTO fichero.bin [-p DIR] [-r DIR]
#
#   PUERTO   lo que entienda pySerial: COM5, /dev/ttyACM0, o una URL:
#            rfc2217://localhost:3357 (el backchannel de la placa
#            msp_exp432e401y.xml) o socket://localhost:PUERTO
#   -p DIR   donde va la imagen (0 por omision)
#   -r DIR   RUN al acabar: salta ahi (con -r 0, a la aplicacion recien
#            descargada: el cargador pone VTOR, la pila y salta a su reset)
#   --reset  RESET al acabar, en vez de RUN
#   -b B     baudios (115200)
#   -l N     bytes por SEND_DATA (64; el maximo del paquete son 252)
#   -d       sin los 0x55: el cargador ya sabe los baudios (ROM_UpdateUART)
#
# El simulador, con la placa y SIN firmware, deja la Flash en blanco y el chip
# arranca su cargador, como uno nuevo:
#
#   ./build/mcu-sim placas/msp_exp432e401y.xml --sin-firmware
#   python3 verif/serie/sflash_e4.py rfc2217://localhost:3357 \
#           verif/fw/msp432e4/e4_t3/e4_t3.bin -r 0
#
# En Windows, con un redirector de puerto COM a RFC 2217 (doc/puente_serie.md)
# vale el COM que cree, y tambien el `sflash.exe` de TivaWare:
#   sflash -c 7 -p 0 -r 0 e4_t3.bin
#
# --prueba hace eso mismo de punta a punta: arranca el mcu-sim (build/mcu-sim,
# o --sim RUTA) con la placa en un puerto libre, programa e4_t3.bin, lo
# arranca y espera su saludo por el mismo puerto (`make sflash-e4`).
# =============================================================================
import argparse
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

try:
    import serial
except ImportError:
    print("falta pySerial: pip install pyserial (o python3-serial)")
    sys.exit(2)

ACK, NAK = 0xCC, 0x33
PING, DOWNLOAD, RUN, GET_STATUS, SEND_DATA, RESET = 0x20, 0x21, 0x22, 0x23, 0x24, 0x25
ESTADOS = {0x40: "SUCCESS", 0x41: "UNKNOWN_CMD", 0x42: "INVALID_CMD",
           0x43: "INVALID_ADR", 0x44: "FLASH_FAIL"}


class Error(Exception):
    pass


class Cargador:
    def __init__(self, puerto, baudios=115200, plazo=5.0):
        self.s = serial.serial_for_url(puerto, baudrate=baudios, timeout=plazo)

    def cierra(self):
        self.s.close()

    def _byte(self):
        b = self.s.read(1)
        if not b:
            raise Error("el cargador no contesta")
        return b[0]

    def _respuesta(self):
        b = 0
        while b == 0:
            b = self._byte()
        return b

    def autobaudios(self):
        self.s.reset_input_buffer()
        self.s.write(b"\x55\x55")
        if self._respuesta() != ACK:
            raise Error("los 0x55 no tienen ACK")

    def paquete(self, datos, intentos=3):
        cab = bytes([len(datos) + 2, sum(datos) & 0xFF])
        for _ in range(intentos):
            self.s.write(cab + datos)
            r = self._respuesta()
            if r == ACK:
                return
            if r != NAK:
                raise Error("respuesta 0x%02X: ni ACK ni NAK" % r)
        raise Error("NAK %d veces" % intentos)

    def estado(self):
        self.paquete(bytes([GET_STATUS]))
        n = 0
        while n == 0:
            n = self._byte()
        suma = self._byte()
        d = self.s.read(n - 2)
        if len(d) != n - 2 or (sum(d) & 0xFF) != suma:
            self.s.write(b"\x00" + bytes([NAK]))
            raise Error("el paquete de estado llega mal")
        self.s.write(b"\x00" + bytes([ACK]))
        return d[0]

    def exige(self, que):
        e = self.estado()
        if e != 0x40:
            raise Error("%s: %s (0x%02X)" % (que, ESTADOS.get(e, "?"), e))

    def ping(self):
        self.paquete(bytes([PING]))

    def descarga(self, imagen, dir_, trozo=64, avance=None):
        self.paquete(bytes([DOWNLOAD]) + struct.pack(">II", dir_, len(imagen)))
        self.exige("DOWNLOAD")
        for i in range(0, len(imagen), trozo):
            self.paquete(bytes([SEND_DATA]) + imagen[i:i + trozo])
            self.exige("SEND_DATA en 0x%X" % (dir_ + i))
            if avance:
                avance(min(i + trozo, len(imagen)), len(imagen))

    def run(self, dir_):
        self.paquete(bytes([RUN]) + struct.pack(">I", dir_))

    def reset(self):
        self.paquete(bytes([RESET]))


def programa(a):
    imagen = open(a.fichero, "rb").read()
    c = Cargador(a.puerto, a.b)
    try:
        if not a.d:
            c.autobaudios()
        c.ping()
        t0 = time.time()

        def avance(n, total):
            sys.stdout.write("\r  %6d de %d bytes" % (n, total))
            sys.stdout.flush()
        c.descarga(imagen, a.p, a.l, avance)
        print("\r  %d bytes en 0x%08X, en %.1f s" % (len(imagen), a.p, time.time() - t0))
        if a.reset:
            c.reset()
            print("  RESET")
        elif a.r is not None:
            c.run(a.r)
            print("  RUN 0x%08X" % a.r)
    finally:
        c.cierra()


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def prueba(a):
    """De punta a punta contra el simulador: Flash en blanco, cargador de la
    ROM, e4_t3.bin por RFC 2217, RUN y su saludo por el mismo puerto."""
    puerto = puerto_libre()
    log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    args = [a.sim, "placas/msp_exp432e401y.xml", "--sin-firmware",
            "--serie", "BCL=rfc2217:%d" % puerto]
    p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
    fallos = 0

    def check(cond, texto):
        nonlocal fallos
        print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
        if not cond:
            fallos += 1
        return cond
    try:
        t0 = time.time()
        while True:
            if p.poll() is not None or time.time() - t0 > 30:
                check(False, "el simulador escucha en el %d" % puerto)
                return 1
            try:
                socket.create_connection(("127.0.0.1", puerto), timeout=1).close()
                break
            except OSError:
                time.sleep(0.1)
        time.sleep(0.3)
        print("--- sflash por RFC 2217 contra el cargador de la ROM R1 del MSP432E401Y ---")
        img = open("verif/fw/msp432e4/e4_t3/e4_t3.bin", "rb").read()
        c = Cargador("rfc2217://127.0.0.1:%d" % puerto, 115200, plazo=20)
        try:
            c.autobaudios()
            check(True, "dos 0x55 a 115200: ACK (el cargador ha medido los baudios)")
            c.ping()
            check(True, "PING: ACK")
            check(c.estado() == 0x40, "GET_STATUS: SUCCESS")
            t1 = time.time()
            c.descarga(img, 0, 64)
            check(True, "e4_t3.bin (%d bytes) en 0x0, a trozos de 64, en %.1f s de pared"
                  % (len(img), time.time() - t1))
            c.run(0)
            check(True, "RUN 0x0: ACK")
            fin = time.time() + 20
            saludo = b""
            while time.time() < fin and b"\r\n" not in saludo:
                saludo += c.s.read(1)
            check(saludo.endswith(b"Hola desde el MSP432E4\r\n"),
                  "y la aplicacion saluda por el mismo puerto: %r" % saludo[-26:])
        except Error as e:
            check(False, str(e))
        finally:
            c.cierra()
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
    print("TOTAL sflash_e4: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


def main():
    ap = argparse.ArgumentParser(description="El cargador serie de la ROM del MSP432E4")
    ap.add_argument("puerto", nargs="?")
    ap.add_argument("fichero", nargs="?")
    ap.add_argument("-p", type=lambda x: int(x, 0), default=0)
    ap.add_argument("-r", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--reset", action="store_true")
    ap.add_argument("-b", type=int, default=115200)
    ap.add_argument("-l", type=int, default=64)
    ap.add_argument("-d", action="store_true")
    ap.add_argument("--prueba", action="store_true")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    a = ap.parse_args()
    if a.prueba:
        sys.exit(prueba(a))
    if not a.puerto or not a.fichero:
        ap.error("hacen falta PUERTO y fichero (o --prueba)")
    if not 4 <= a.l <= 252:
        ap.error("-l entre 4 y 252")
    try:
        programa(a)
    except Error as e:
        print("  error: %s" % e)
        sys.exit(1)


if __name__ == "__main__":
    main()
