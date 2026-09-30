#!/usr/bin/env python3
# =============================================================================
# captura_rfc2217.py — Una sesión RFC 2217 de verdad, byte a byte
#
# Fase D4 del puente UART (P-14, doc/analisis_puente_serie.md §10). El códec de
# common/telnet2217.h no se prueba solo contra el texto de la RFC: la RFC la
# leen los clientes cada uno a su manera, y lo que importa es lo que MANDAN.
#
# Esto monta, en la misma máquina y sin hardware, los dos extremos que trae
# pySerial -el cliente `rfc2217://` y el servidor `rfc2217.PortManager` sobre
# un puerto `loop://`- y graba lo que pasa por el socket en cada sentido,
# paso a paso. El resultado es verif/vectores/rfc2217_pyserial.vec, que lee
# `make rfc2217`.
#
#   python3 verif/vectores/captura_rfc2217.py > verif/vectores/rfc2217_pyserial.vec
#
# Con `--contra PUERTO` no monta el servidor de pySerial: se pone de PROXY
# delante de otro servidor -el de `prueba_rfc2217 --servidor PUERTO`, que usa el
# códec de este proyecto- y graba lo mismo. Es la prueba de que el cliente de
# pySerial se entiende con un servidor PASIVO, que es la política de mcu-sim:
#
#   ./build/prueba_rfc2217 --servidor 47356 &
#   python3 verif/vectores/captura_rfc2217.py --contra 47356 \
#       > verif/vectores/rfc2217_pyserial_mcusim.vec
#
# El fichero se VERSIONA, como los firmwares: regenerarlo con otra versión de
# pySerial puede dar otros bytes, y eso es una decisión, no un trámite.
#
# FORMATO: una línea por fragmento que pasa por el socket.
#     # comentario
#     = paso <nombre>          empieza un paso de la sesión
#     c> <hex>                 del cliente al servidor
#     s> <hex>                 del servidor al cliente
# =============================================================================
import socket
import sys
import threading
import time

import serial
import serial.rfc2217 as rfc

grabado = []          # (sentido, bytes, paso)
paso = ["arranque"]
cerrojo = threading.Lock()


def graba(sentido, datos):
    if datos:
        with cerrojo:
            grabado.append((sentido, bytes(datos), paso[0]))


class Conexion:
    """Lo que PortManager usa para escribir: el socket, grabando."""
    def __init__(self, s):
        self.s = s

    def write(self, datos):
        graba("s>", datos)
        self.s.sendall(datos)


def servidor(escucha, listo):
    listo.set()
    c, _ = escucha.accept()
    c.settimeout(0.05)
    ser = serial.serial_for_url("loop://", timeout=0)
    pm = rfc.PortManager(ser, Conexion(c))
    while True:
        try:
            d = c.recv(4096)
            if not d:
                break
            graba("c>", d)
            ser.write(b"".join(pm.filter(d)))
        except socket.timeout:
            pass
        except OSError:
            break
        pendiente = ser.read(ser.in_waiting or 1)
        if pendiente:
            Conexion(c).write(b"".join(pm.escape(pendiente)))
        pm.check_modem_lines()


def proxy(escucha, listo, destino):
    """Entre el cliente y otro servidor, grabando los dos sentidos."""
    listo.set()
    c, _ = escucha.accept()
    s = socket.create_connection(("127.0.0.1", destino))

    def bombea(de, a, sentido):
        while True:
            try:
                d = de.recv(4096)
            except OSError:
                d = b""
            if not d:
                try:
                    a.shutdown(socket.SHUT_WR)
                except OSError:
                    pass
                return
            graba(sentido, d)
            a.sendall(d)

    threading.Thread(target=bombea, args=(s, c, "s>"), daemon=True).start()
    bombea(c, s, "c>")


def main():
    contra = None
    if len(sys.argv) == 3 and sys.argv[1] == "--contra":
        contra = int(sys.argv[2])
    escucha = socket.socket()
    escucha.bind(("127.0.0.1", 0))
    escucha.listen(1)
    puerto = escucha.getsockname()[1]
    listo = threading.Event()
    if contra:
        threading.Thread(target=proxy, args=(escucha, listo, contra),
                         daemon=True).start()
    else:
        threading.Thread(target=servidor, args=(escucha, listo),
                         daemon=True).start()
    listo.wait()

    def cambia(nombre):
        time.sleep(0.4)
        paso[0] = nombre

    cambia("abrir")
    cli = serial.serial_for_url(
        "rfc2217://127.0.0.1:%d" % puerto, baudrate=115200, timeout=1)
    cambia("datos con 0xFF y CR")
    cli.write(b"Hola\r\xff\x00fin")
    cli.read(9)
    cambia("baudios 9600")
    cli.baudrate = 9600
    cambia("paridad par y 7 bits")
    cli.bytesize = 7
    cli.parity = serial.PARITY_EVEN
    cambia("dos bits de parada")
    cli.stopbits = serial.STOPBITS_TWO
    cambia("RTS bajo y DTR bajo")
    cli.rts = False
    cli.dtr = False
    cambia("break")
    cli.send_break(0.05)
    cambia("purgar")
    cli.reset_input_buffer()
    cli.reset_output_buffer()
    cambia("cerrar")
    cli.close()
    time.sleep(0.4)

    if contra:
        print("# Sesion RFC 2217 entre el cliente rfc2217:// de pySerial %s y el"
              % serial.VERSION)
        print("# servidor de `prueba_rfc2217 --servidor` (el codec de mcu-sim),")
        print("# grabada por verif/vectores/captura_rfc2217.py --contra.")
    else:
        print("# Sesion RFC 2217 entre el cliente rfc2217:// y el PortManager de")
        print("# pySerial %s, grabada por verif/vectores/captura_rfc2217.py."
              % serial.VERSION)
    print("# c> del cliente al servidor; s> del servidor al cliente.")
    actual = None
    for sentido, datos, p in grabado:
        if p != actual:
            print("= paso %s" % p)
            actual = p
        print("%s %s" % (sentido, datos.hex()))


if __name__ == "__main__":
    main()
