#!/usr/bin/env python3
# =============================================================================
# pty_rfc2217.py — un puerto serie de mentira para las herramientas que solo
# saben abrir uno (stm32flash, por ejemplo), hacia un PuenteSerie por RFC 2217
#
#   python3 verif/pty_rfc2217.py PUERTO ENLACE [BAUDIOS] [FORMATO]
#
# Crea un pseudoterminal, lo enlaza en ENLACE (un fichero: /tmp/f3tty) y pasa
# los bytes de un lado a otro con rfc2217://localhost:PUERTO, que configura con
# BAUDIOS (115200) y FORMATO (8E1, el del cargador de AN3155). Lo que la
# herramienta haga con el termios del pty no llega al puente: los baudios y el
# formato son los de aqui. Sale cuando el simulador cierra la conexion.
#
# Necesita pySerial (python3 -m pip install pyserial). Lo usa `make
# f3-stm32flash` con placas/stm32f3discovery_boot0.xml.
# =============================================================================
import os
import select
import sys
import time
import tty

try:
    import serial
except ImportError:
    sys.exit("pty_rfc2217: hace falta pySerial (python3 -m pip install pyserial)")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__ if __doc__ else "uso: pty_rfc2217.py PUERTO ENLACE [BAUDIOS] [FORMATO]")
    puerto, enlace = int(sys.argv[1]), sys.argv[2]
    baudios = int(sys.argv[3]) if len(sys.argv) > 3 else 115200
    fmt = sys.argv[4] if len(sys.argv) > 4 else "8E1"
    bits, paridad, parada = int(fmt[0]), fmt[1].upper(), float(fmt[2:])
    # El simulador puede tardar en escuchar: se reintenta unos segundos
    s = None
    for _ in range(100):
        try:
            s = serial.serial_for_url("rfc2217://localhost:%d" % puerto, baudrate=baudios,
                                      bytesize=bits, parity=paridad,
                                      stopbits=parada, timeout=0)
            break
        except (serial.SerialException, OSError):
            time.sleep(0.1)
    if s is None:
        sys.exit("pty_rfc2217: nadie escucha en localhost:%d" % puerto)
    # Que el puente aplique los baudios y el formato antes de que nadie hable:
    # el enlace aparece despues, y quien lo espera no manda nada antes
    time.sleep(0.5)
    maestro, esclavo = os.openpty()
    tty.setraw(maestro)
    tty.setraw(esclavo)
    nombre = os.ttyname(esclavo)
    try:
        os.unlink(enlace)
    except FileNotFoundError:
        pass
    os.symlink(nombre, enlace)
    print("pty_rfc2217: %s (%s) <-> rfc2217://localhost:%d a %d %s" %
          (enlace, nombre, puerto, baudios, fmt), flush=True)
    try:
        while True:
            r, _, _ = select.select([maestro], [], [], 0.002)
            if r:
                d = os.read(maestro, 4096)
                if d:
                    s.write(d)
            d = s.read(4096)
            if d:
                os.write(maestro, d)
    except (serial.SerialException, OSError, KeyboardInterrupt):
        pass
    finally:
        try:
            os.unlink(enlace)
        except OSError:
            pass


if __name__ == "__main__":
    main()
