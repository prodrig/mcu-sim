#!/usr/bin/env python3
# =============================================================================
# interop.py — El puente UART contra clientes de verdad (P-14, fase D6)
#
# `testserie` prueba el puente con un terminal hecho con el códec del propio
# proyecto. Eso demuestra que el puente hace lo que el proyecto cree que dice
# la RFC; no demuestra que lo entiendan los programas que va a usar un alumno.
# Esto sí: arranca el `mcu-sim` DE VERDAD -el ejecutable, con una placa XML y el
# firmware `vcp_demo`- y le conecta:
#
#   * el cliente `rfc2217://` de pySerial, que es el que usan miniterm, los
#     scripts de Python y buena parte de los redirectores;
#   * el cliente `socket://` de pySerial, contra el modo TCP en crudo;
#   * socat haciendo un pty de un puerto TCP (Linux y macOS), que es la receta
#     del §7.4 para tener un puerto serie del sistema, abierto con pySerial como
#     un puerto serie más.
#
# Lo que se mira es lo que vería el alumno: el eco, que un cambio de baudios en
# el terminal deja al firmware sin entender nada y volver lo arregla, que la
# paridad del terminal llega a la USART, que el control de flujo evita perder
# bytes, que las líneas de módem se leen, que un break no rompe la línea, y que
# cerrar y volver a abrir el terminal funciona.
#
#   cd src && python3 verif/serie/interop.py            (o `make interop`)
#
# Opciones: --sim RUTA (por omisión build/mcu-sim, .exe en Windows),
# --sin-socat, --sin-tiempo-real. Necesita pySerial (`pip install pyserial`, o
# `python3-serial` en Debian y Ubuntu); socat, si está, se usa.
#
# EL TIEMPO AQUÍ ES DE PARED, y no hay invariante que contrastar: el simulador
# corre con --tiempo-real, como lo correría el alumno, y un cliente de verdad
# no sabe esperar en tiempo simulado. Por eso las comprobaciones son de las que
# no dependen de lo rápida que sea la máquina: un eco completo, un eco que NO
# es el que se mandó, todo lo que se manda con control de flujo vuelve.
# =============================================================================
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

try:
    import serial
except ImportError:
    print("  [FALLO] falta pySerial: pip install pyserial (o python3-serial)")
    sys.exit(2)

ok = 0
fallos = 0


def check(cond, texto):
    global ok, fallos
    if cond:
        ok += 1
        print("  [OK  ] " + texto)
    else:
        fallos += 1
        print("  [FALLO] " + texto)
    sys.stdout.flush()
    return cond


def grupo(nombre):
    print("--- %s ---" % nombre)
    sys.stdout.flush()


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Simulador:
    """Un mcu-sim con la placa vcp_rfc2217.xml y vcp_demo, en un puerto."""

    def __init__(self, sim, modo, puerto, tiempo_real, log,
                 placa="placas/vcp_rfc2217.xml"):
        self.puerto = puerto
        args = [sim, placa, "verif/fw/vcp_demo/vcp_demo.bin",
                "--serie", "VCP=%s:%d" % (modo, puerto)]
        if tiempo_real:
            args.append("--tiempo-real")
        self.log = open(log, "w")
        self.p = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)

    def espera_escucha(self, seg=30.0):
        # Se sabe que escucha cuando acepta una conexión. Esa conexión de
        # prueba se cierra en el acto: para el puente es un cliente que se va,
        # y el siguiente entra sin más (D-5).
        t0 = time.time()
        while time.time() - t0 < seg:
            if self.p.poll() is not None:
                return False
            try:
                c = socket.create_connection(("127.0.0.1", self.puerto), timeout=1)
                c.close()
                time.sleep(0.2)          # que el firmware salude y arranque
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def para(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(10)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        self.log.close()


def lee(p, n, seg=3.0):
    """Hasta n bytes, o lo que haya llegado en `seg` segundos."""
    r = b""
    t0 = time.time()
    while len(r) < n and time.time() - t0 < seg:
        r += p.read(n - len(r))
    return r


def calla(p, seg=0.3):
    """Lo que llegue en un rato: para comprobar lo que NO vuelve."""
    time.sleep(seg)
    return p.read(p.in_waiting or 0) if p.in_waiting else b""


TODOS = bytes(v for v in range(256) if v != 0x13)     # 0x13: la pausa de vcp_demo


def eco(p, datos, seg=3.0):
    p.write(datos)
    return lee(p, len(datos), seg)


# ---------------------------------------------------------------------------
def rfc2217(sim, tiempo_real, dir_log):
    grupo("I1 pySerial rfc2217:// contra mcu-sim")
    puerto = puerto_libre()
    s = Simulador(sim, "rfc2217", puerto, tiempo_real,
                  os.path.join(dir_log, "interop_rfc2217.log"))
    try:
        if not check(s.espera_escucha(), "mcu-sim escucha en rfc2217:%d" % puerto):
            return
        url = "rfc2217://127.0.0.1:%d" % puerto
        p = serial.serial_for_url(url, baudrate=115200, timeout=0.5)
        check(p.is_open, "el cliente abre " + url + " (negocia COM-PORT)")
        p.reset_input_buffer()
        check(eco(p, b"Hola\r\n") == b"Hola\r\n", "eco a 115200")
        check(eco(p, TODOS) == TODOS,
              "los 255 valores de byte, 0xFF y 0x00 incluidos, van y vuelven")
        check(eco(p, b"\r\x00\r\n") == b"\r\x00\r\n",
              "un NUL detras de un CR no se pierde: la sesion es BINARY")

        grupo("I2 Los baudios del terminal llegan a la USART")
        p.baudrate = 9600
        check(p.baudrate == 9600, "el terminal pasa a 9600")
        p.write(b"Hola")
        r = calla(p, 0.5)
        check(b"Hola" not in r,
              "el firmware, a 115200, no entiende nada: sin eco de Hola (%d bytes)" % len(r))
        p.baudrate = 115200
        time.sleep(0.1)
        p.reset_input_buffer()
        check(eco(p, b"de vuelta\r\n") == b"de vuelta\r\n",
              "de vuelta a 115200, el eco vuelve")

        grupo("I3 La paridad del terminal llega a la USART")
        p.parity = serial.PARITY_EVEN            # 8E1 contra un firmware en 8N1
        time.sleep(0.05)
        p.reset_input_buffer()
        p.write(b"A")                            # dos unos: paridad 0
        check(calla(p, 0.3) == b"",
              "8E1: una A lleva paridad 0, que la USART toma por una parada "
              "mala; sin eco")
        check(eco(p, b"C", 1.0) == b"C",
              "una C lleva paridad 1, que la USART toma por la parada: vuelve")
        p.parity = serial.PARITY_NONE
        time.sleep(0.05)
        p.reset_input_buffer()
        check(eco(p, b"8N1\r\n") == b"8N1\r\n", "vuelta a 8N1")

        grupo("I4 Lineas de modem, RTS, DTR, break y purga")
        check(p.cts and p.dsr and p.cd,
              "CTS, DSR y DCD activas (NOTIFY-MODEMSTATE)")
        p.rts = False
        p.dtr = False
        p.rts = True
        p.dtr = True
        p.send_break(0.01)
        p.reset_input_buffer()
        p.reset_output_buffer()
        check(eco(p, b"tras el break\r\n") == b"tras el break\r\n",
              "RTS, DTR, un break y las dos purgas, y la linea sigue viva")

        grupo("I5 Control de flujo pedido por el terminal")
        rafaga = bytes(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcd")
        p.reset_input_buffer()
        p.write(b"\x13" + rafaga)                # 0x13: el firmware deja de leer 5 ms
        r = lee(p, len(rafaga), 1.0)
        check(len(r) < len(rafaga),
              "sin control de flujo, una rafaga de 40 durante una pausa del "
              "firmware desborda la USART: vuelven %d" % len(r))
        time.sleep(0.1)
        p.rtscts = True
        p.reset_input_buffer()
        p.write(b"\x13" + rafaga)                # 0x13: el firmware deja de leer 5 ms
        check(lee(p, len(rafaga)) == rafaga,
              "con rtscts=True, una rafaga de 40 durante una pausa del "
              "firmware vuelve entera")
        p.rtscts = False
        p.close()

        grupo("I6 Cerrar y volver a abrir el terminal")
        p = serial.serial_for_url(url, baudrate=115200, timeout=0.5)
        p.reset_input_buffer()
        check(eco(p, b"otra vez\r\n") == b"otra vez\r\n", "el eco, con el terminal reabierto")
        q = serial.serial_for_url(url, baudrate=115200, timeout=0.5)
        q.reset_input_buffer()
        check(eco(q, b"el nuevo\r\n") == b"el nuevo\r\n",
              "un segundo terminal sustituye al primero (D-5), y el eco va a el")
        q.close()
        try:
            p.close()
        except Exception:
            pass
    except Exception as e:               # pySerial se queja con excepciones
        check(False, "excepcion: %s: %s" % (type(e).__name__, e))
    finally:
        s.para()


PLACA_FIJA = """<?xml version="1.0" encoding="UTF-8"?>
<placa nombre="vcp-fija">
  <componente tipo="PuenteSerie" id="VCP" host="rfc2217:3355"
              baudios="115200" formato="8N1" flujo="%s" muestra="si">
    <pin nombre="rx"  nodo="PA2"/>
    <pin nombre="tx"  nodo="PA3"/>
    <pin nombre="cts" nodo="PA1"/>
  </componente>
</placa>
"""


def fijos(sim, tiempo_real, dir_log):
    # Una placa con la linea FIJADA en el XML (115200 8N1), servida por RFC
    # 2217. El puente contesta a lo que pida el terminal con lo que HAY (D-14),
    # y pySerial, al ver que no es lo que pidio, lo dice con una excepcion. Es
    # lo que tiene que ver el alumno: su terminal no le deja creer que ha
    # cambiado algo que la placa tiene fijo. Y tiene una consecuencia que hay
    # que saber: con flujo="rtscts" en el XML, pySerial NO ABRE si no se le
    # pide rtscts=True, porque al abrir manda «sin control de flujo».
    d = tempfile.mkdtemp(prefix="placa")
    try:
        for flujo in ("no", "rtscts"):
            grupo("I7 pySerial contra la linea fija en el XML, flujo=%s (D-14)" % flujo)
            placa = os.path.join(d, "fija_%s.xml" % flujo)
            with open(placa, "w") as f:
                f.write(PLACA_FIJA % flujo)
            puerto = puerto_libre()
            s = Simulador(sim, "rfc2217", puerto, tiempo_real,
                          os.path.join(dir_log, "interop_fija_%s.log" % flujo), placa)
            try:
                if not check(s.espera_escucha(), "mcu-sim escucha en rfc2217:%d" % puerto):
                    continue
                url = "rfc2217://127.0.0.1:%d" % puerto
                if flujo == "rtscts":
                    try:
                        serial.serial_for_url(url, baudrate=115200, timeout=0.5)
                        check(False, "abrir sin rtscts deberia dar error en pySerial")
                    except ValueError as e:
                        check("control" in str(e),
                              "abrir sin rtscts=True: pySerial se niega (\"%s\")" % e)
                p = serial.serial_for_url(url, baudrate=115200, timeout=0.5,
                                          rtscts=(flujo == "rtscts"))
                check(eco(p, b"fija\r\n") == b"fija\r\n",
                      "con lo que dice el XML, abre y hay eco")
                try:
                    p.baudrate = 9600
                    check(False, "pedir 9600 deberia dar error en pySerial")
                except ValueError as e:
                    check("baudrate" in str(e),
                          "pedir 9600: pySerial avisa de que no se acepta (\"%s\")" % e)
                p.close()
            except Exception as e:
                check(False, "excepcion: %s: %s" % (type(e).__name__, e))
            finally:
                s.para()
    finally:
        shutil.rmtree(d, ignore_errors=True)


def crudo(sim, tiempo_real, dir_log, con_socat):
    grupo("I8 pySerial socket:// contra el modo TCP en crudo")
    puerto = puerto_libre()
    s = Simulador(sim, "tcp", puerto, tiempo_real,
                  os.path.join(dir_log, "interop_tcp.log"))
    try:
        if not check(s.espera_escucha(), "mcu-sim escucha en tcp:%d" % puerto):
            return
        p = serial.serial_for_url("socket://127.0.0.1:%d" % puerto, timeout=0.5)
        check(eco(p, b"Hola\r\n") == b"Hola\r\n", "eco")
        check(eco(p, TODOS) == TODOS, "los 255 valores de byte, sin escapar nada")
        p.close()

        if not con_socat:
            return
        grupo("I9 socat: un pty del sistema sobre el puerto TCP (receta del 7.4)")
        d = tempfile.mkdtemp(prefix="vcp")
        enlace = os.path.join(d, "vcp")
        so = subprocess.Popen(
            ["socat", "pty,link=%s,raw,echo=0" % enlace,
             "tcp:127.0.0.1:%d" % puerto],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            t0 = time.time()
            while not os.path.exists(enlace) and time.time() - t0 < 10:
                time.sleep(0.05)
            if not check(os.path.exists(enlace), "socat crea " + enlace):
                return
            p = serial.Serial(enlace, 115200, timeout=0.5)
            time.sleep(0.2)
            p.reset_input_buffer()
            check(eco(p, b"por el pty\r\n") == b"por el pty\r\n",
                  "eco a traves del pty, abierto como un puerto serie")
            check(eco(p, TODOS) == TODOS, "los 255 valores de byte, tambien por el pty")
            p.close()
        finally:
            so.terminate()
            so.wait()
            shutil.rmtree(d, ignore_errors=True)
    except Exception as e:
        check(False, "excepcion: %s: %s" % (type(e).__name__, e))
    finally:
        s.para()


def main():
    a = argparse.ArgumentParser(description="El puente UART contra clientes de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    a.add_argument("--sin-socat", action="store_true")
    a.add_argument("--sin-tiempo-real", action="store_true")
    o = a.parse_args()
    o.sim = os.path.normpath(o.sim)          # en Windows, con sus barras
    if not os.path.exists(o.sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % o.sim)
        return 2
    con_socat = (not o.sin_socat) and os.name != "nt" and shutil.which("socat")
    print("pySerial %s, %s, socat: %s" % (
        serial.VERSION, sys.platform,
        "si" if con_socat else "no (%s)" % ("Windows" if os.name == "nt" else
                                            "no esta" if not o.sin_socat else "--sin-socat")))
    dir_log = os.path.dirname(o.sim) or "."
    tr = not o.sin_tiempo_real
    rfc2217(o.sim, tr, dir_log)
    fijos(o.sim, tr, dir_log)
    crudo(o.sim, tr, dir_log, con_socat)
    print("\n=====================================================")
    print("TOTAL INTEROP : %d comprobaciones OK, %d fallos" % (ok, fallos))
    print("=====================================================")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
