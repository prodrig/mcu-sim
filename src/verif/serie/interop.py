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
import threading
import time

try:
    import serial
    import serial.rfc2217 as rfc
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


def check_bytes(obtenido, esperado, texto):
    """Como check(obtenido == esperado), pero si falla dice QUE llego: cuantos
    bytes, el primero distinto y los de alrededor. Un «[FALLO] los 255 valores»
    sin mas no distingue un byte comido de una rafaga cortada por el plazo, y
    las dos cosas tienen arreglos distintos. Paso de verdad en el CI de macOS
    el 2026-10-01 (I9, por el pty de socat), y aquel log no tenia con que
    contestar."""
    if check(obtenido == esperado, texto):
        return True
    n = min(len(obtenido), len(esperado))
    i = next((k for k in range(n) if obtenido[k] != esperado[k]), n)
    print("          llegaron %d de %d bytes; el primero distinto, en la posicion %d"
          % (len(obtenido), len(esperado), i))
    print("          esperado alli: %s" % esperado[max(0, i - 4):i + 8].hex(" "))
    print("          llego alli:    %s" % obtenido[max(0, i - 4):i + 8].hex(" "))
    sys.stdout.flush()
    return False


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
                 placa="placas/vcp_rfc2217.xml", extra=(), host_puerto=None):
        self.puerto = puerto
        destino = "%s:%s" % (modo, host_puerto) if host_puerto else "%s:%d" % (modo, puerto)
        args = [sim, placa, "verif/fw/vcp_demo/vcp_demo.bin",
                "--serie", "VCP=" + destino] + list(extra)
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
        check_bytes(eco(p, TODOS), TODOS,
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
  <mcu tipo="STM32F407VG" id="u0"/>
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
        check_bytes(eco(p, TODOS), TODOS, "los 255 valores de byte, sin escapar nada")
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
            check_bytes(eco(p, TODOS), TODOS, "los 255 valores de byte, tambien por el pty")
            p.close()
        finally:
            so.terminate()
            so.wait()
            shutil.rmtree(d, ignore_errors=True)
    except Exception as e:
        check(False, "excepcion: %s: %s" % (type(e).__name__, e))
    finally:
        s.para()


SALUDO = b"vcp_demo listo\r\n"


def abre_cuando_escuche(url, seg=30.0, **kw):
    """Abre `url` en cuanto el puente escuche, SIN conexión de prueba: con
    --espera-terminal, una conexión de prueba ya contaría como el terminal."""
    t0 = time.time()
    while True:
        try:
            return serial.serial_for_url(url, **kw)
        except (serial.SerialException, OSError):
            if time.time() - t0 > seg:
                raise
            time.sleep(0.1)


def espera_terminal(sim, tiempo_real, dir_log, con_socat):
    # --espera-terminal (fase D7): el MCU no arranca hasta que el terminal
    # esta conectado y ha terminado de configurar el puerto, asi que el saludo
    # de vcp_demo -lo primero que imprime- tiene que llegar. Sin la opcion se
    # pierde (D-6), que es lo que se ve en I1-I9. Y de paso la Nucleo-F446RE
    # con su VCP, con el mismo firmware: el RCC, el GPIOA y la USART2 del F446
    # estan donde en el F407.
    casos = [("rfc2217", "placas/nucleo_f446re_vcp.xml", "rfc2217://127.0.0.1:%d",
              "Nucleo-F446RE, rfc2217://"),
             ("tcp", "placas/vcp_rfc2217.xml", "socket://127.0.0.1:%d",
              "F407, socket://")]
    for modo, placa, url, nombre in casos:
        grupo("I10 --espera-terminal: el saludo llega (%s)" % nombre)
        puerto = puerto_libre()
        s = Simulador(sim, modo, puerto, tiempo_real,
                      os.path.join(dir_log, "interop_espera_%s.log" % modo),
                      placa, ["--espera-terminal"])
        try:
            p = abre_cuando_escuche(url % puerto, baudrate=115200, timeout=0.5)
            check(lee(p, len(SALUDO)) == SALUDO,
                  "lo primero que llega es el saludo: " + repr(SALUDO))
            check(eco(p, b"eco\r\n") == b"eco\r\n", "y despues, el eco")
            p.close()
        except Exception as e:
            check(False, "excepcion: %s: %s" % (type(e).__name__, e))
        finally:
            s.para()
    if not con_socat:
        return
    # socat con `wait-slave`: no se conecta al puente hasta que alguien abre el
    # pty. Sin eso se conectaria al arrancar, el MCU saludaria a un pty que no
    # ha abierto nadie, y el saludo se perderia. Es la receta de
    # doc/puente_serie.md.
    grupo("I11 --espera-terminal con socat wait-slave: el saludo llega por el pty")
    puerto = puerto_libre()
    s = Simulador(sim, "tcp", puerto, tiempo_real,
                  os.path.join(dir_log, "interop_espera_socat.log"),
                  "placas/vcp_rfc2217.xml", ["--espera-terminal"])
    d = tempfile.mkdtemp(prefix="vcp")
    enlace = os.path.join(d, "vcp")
    so = None
    try:
        time.sleep(1.0)                          # que el puente escuche
        so = subprocess.Popen(
            ["socat", "pty,link=%s,raw,echo=0,wait-slave" % enlace,
             "tcp:127.0.0.1:%d" % puerto],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        while not os.path.exists(enlace) and time.time() - t0 < 10:
            time.sleep(0.05)
        time.sleep(1.0)                          # el alumno tarda en abrir picocom
        p = serial.Serial(enlace, 115200, timeout=0.5)
        check(lee(p, len(SALUDO)) == SALUDO, "el saludo llega por el pty")
        check(eco(p, b"eco\r\n") == b"eco\r\n", "y despues, el eco")
        p.close()
    except Exception as e:
        check(False, "excepcion: %s: %s" % (type(e).__name__, e))
    finally:
        if so:
            so.terminate()
            so.wait()
        shutil.rmtree(d, ignore_errors=True)
        s.para()


# ---------------------------------------------------------------------------
# El modo cliente (fase D8)
# ---------------------------------------------------------------------------
def lee_socket(c, n, seg=3.0):
    r = b""
    t0 = time.time()
    c.settimeout(0.1)
    while len(r) < n and time.time() - t0 < seg:
        try:
            x = c.recv(n - len(r))
            if not x:
                break
            r += x
        except socket.timeout:
            pass
    return r


def cliente_tcp(sim, tiempo_real, dir_log):
    # mcu-sim se conecta a un servidor que TODAVIA NO EXISTE: tiene que
    # reintentar, conectarse cuando aparezca, y volver a hacerlo si se cae.
    grupo("I12 tcp-cliente: mcu-sim se conecta a un servidor, reintentando")
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    puerto = srv.getsockname()[1]
    srv.close()                                  # todavia no escucha nadie
    log = os.path.join(dir_log, "interop_tcp_cliente.log")
    s = Simulador(sim, "tcp-cliente", 0, tiempo_real, log, extra=["--espera-terminal"],
                  host_puerto="127.0.0.1:%d" % puerto)
    try:
        time.sleep(2.5)                          # dos o tres intentos fallidos
        check(s.p.poll() is None, "mcu-sim sigue vivo sin servidor al otro lado")
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", puerto))
        srv.listen(1)
        srv.settimeout(5)
        c, _ = srv.accept()
        check(True, "cuando aparece el servidor, mcu-sim se conecta")
        check(lee_socket(c, len(SALUDO)) == SALUDO,
              "y con --espera-terminal el saludo es lo primero que llega")
        c.sendall(b"cliente\r\n")
        check(lee_socket(c, 9) == b"cliente\r\n", "el eco")
        c.close()                                # el servidor se va...
        c, _ = srv.accept()                      # ...y mcu-sim vuelve
        c.sendall(b"otra\r\n")
        check(lee_socket(c, 6) == b"otra\r\n",
              "si el servidor cierra, mcu-sim se reconecta solo, y el eco sigue")
        c.close()
        srv.close()
    except Exception as e:
        check(False, "excepcion: %s: %s" % (type(e).__name__, e))
    finally:
        s.para()
    with open(log) as f:
        texto = f.read()
    check("se reintenta cada segundo" in texto,
          "y lo dice en la consola: '... se reintenta cada segundo'")


class PtySerial(serial.Serial):
    """Un pty no tiene lineas de modem: TIOCMGET da ENOTTY. El PortManager de
    pySerial las lee al activarse COM-PORT, y sin esto se cae. Es por lo que
    `rfc2217_server.py` no sirve TAL CUAL sobre un pty de socat."""
    def _linea(self, nombre):
        try:
            return getattr(serial.Serial, nombre).fget(self)
        except OSError:
            return False
    cts = property(lambda self: self._linea("cts"))
    dsr = property(lambda self: self._linea("dsr"))
    ri = property(lambda self: self._linea("ri"))
    cd = property(lambda self: self._linea("cd"))


def cliente_rfc2217(sim, tiempo_real, dir_log):
    # Un servidor RFC 2217 hecho con el PortManager de pySerial, que es lo que
    # hay dentro de su `rfc2217_server.py`, sobre un extremo de un par de ptys
    # de socat; en el otro extremo, el «terminal» del alumno. mcu-sim hace de
    # CLIENTE: se conecta y configura el puerto remoto con su linea.
    grupo("I13 rfc2217-cliente: contra el servidor de pySerial, sobre un pty")
    d = tempfile.mkdtemp(prefix="d8")
    A, B = os.path.join(d, "a"), os.path.join(d, "b")
    so = subprocess.Popen(["socat", "pty,link=%s,raw,echo=0" % A,
                           "pty,link=%s,raw,echo=0" % B],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    s = None
    try:
        t0 = time.time()
        while not (os.path.exists(A) and os.path.exists(B)) and time.time() - t0 < 10:
            time.sleep(0.05)
        remoto = PtySerial(A, 9600, timeout=0)   # el servidor lo abre a 9600
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 0))
        srv.listen(1)
        puerto = srv.getsockname()[1]
        fallo = []

        def servidor():
            try:
                c, _ = srv.accept()
                c.settimeout(0.02)

                class Conexion:
                    def write(self, x):
                        c.sendall(x)
                pm = rfc.PortManager(remoto, Conexion())
                while True:
                    try:
                        x = c.recv(4096)
                        if not x:
                            break
                        remoto.write(b"".join(pm.filter(x)))
                    except socket.timeout:
                        pass
                    n = remoto.in_waiting
                    if n:
                        c.sendall(b"".join(pm.escape(remoto.read(n))))
                    pm.check_modem_lines()
            except OSError:
                pass
            except Exception as e:                # que no muera en silencio
                fallo.append(e)

        threading.Thread(target=servidor, daemon=True).start()
        terminal = serial.Serial(B, 115200, timeout=0.5)
        s = Simulador(sim, "rfc2217-cliente", 0, tiempo_real,
                      os.path.join(dir_log, "interop_rfc2217_cliente.log"),
                      extra=["--espera-terminal"], host_puerto="127.0.0.1:%d" % puerto)
        check(lee(terminal, len(SALUDO), 10.0) == SALUDO,
              "el saludo llega al terminal, a traves del servidor RFC 2217")
        check(eco(terminal, b"por rfc2217\r\n") == b"por rfc2217\r\n", "y el eco")
        check(remoto.baudrate == 115200,
              "mcu-sim configuro el puerto del servidor: de 9600 a 115200 (%d)"
              % remoto.baudrate)
        check(remoto.bytesize == 8 and remoto.parity == serial.PARITY_NONE and
              remoto.stopbits == serial.STOPBITS_ONE, "y en 8N1")
        check(not fallo, "el servidor no ha fallado" +
              ("" if not fallo else ": %r" % fallo[0]))
        terminal.close()
    except Exception as e:
        check(False, "excepcion: %s: %s" % (type(e).__name__, e))
    finally:
        if s:
            s.para()
        so.terminate()
        so.wait()
        shutil.rmtree(d, ignore_errors=True)


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
    espera_terminal(o.sim, tr, dir_log, con_socat)
    cliente_tcp(o.sim, tr, dir_log)
    if con_socat:
        cliente_rfc2217(o.sim, tr, dir_log)
    print("\n=====================================================")
    print("TOTAL INTEROP : %d comprobaciones OK, %d fallos" % (ok, fallos))
    print("=====================================================")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
