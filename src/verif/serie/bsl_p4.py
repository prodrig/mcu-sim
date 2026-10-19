#!/usr/bin/env python3
# =============================================================================
# bsl_p4.py — Programar un MSP432P4 por su BSL, como BSL Scripter de TI,
#             contra el simulador o contra un chip
#
# El BSL del MSP432P4 (el B1 del simulador, verif/fw/msp432p4/bsl_b1, o el de
# TI) habla por la UART del backchannel, 8E1 [SLAU622; segunda fuente: el
# código de BSL Scripter 3.4]:
#
#   sincronía   0xFF; el BSL mide los baudios y contesta 0x00
#   paquete     0x80, longitud (LE), datos, CRC-CCITT de los datos (LE)
#   respuesta   ACK (0x00) y, según la orden, 0x80 len [0x3B código] o
#               [0x3A datos] CRC
#
#   python3 verif/serie/bsl_p4.py PUERTO fichero [-r DIR | --reset]
#
#   PUERTO   lo que entienda pySerial: COM5, /dev/ttyACM0, o
#            rfc2217://localhost:3356 (el backchannel de msp_exp432p401r.xml)
#   fichero  TI-TXT (.txt), Intel HEX (.hex) o binario (.bin, en 0x0)
#   --password F   la contraseña (256 bytes en TI-TXT, como pass256_*.txt
#            de BSL Scripter); sin ella, 256 x 0xFF: la de un chip en blanco
#   -r DIR   LOAD_PC_32 al acabar; --reset, REBOOT_RESET (por omisión)
#   -b B     baudios (9600; el BSL los mide de la sincronía)
#
# El simulador, con la placa y SIN firmware, deja la Flash en blanco y el
# código de arranque entra en el BSL, como en un chip nuevo:
#
#   ./build/mcu-sim placas/msp_exp432p401r.xml --sin-firmware
#   python3 verif/serie/bsl_p4.py rfc2217://localhost:3356 programa.txt
#
# BSL Scripter de TI vale igual por un puerto serie del sistema: en Windows
# con un redirector de COM a RFC 2217 y `MODE P4xx UART 9600 COMn PARITY` en
# el guion; en Linux y macOS, con socat y el puente en TCP en crudo
# (`--serie BCL=tcp:PUERTO` y 9600 8E1 en la placa).
#
# --prueba lo hace de punta a punta: arranca el mcu-sim (build/mcu-sim, o
# --sim RUTA) con la placa en un puerto libre, programa por el BSL B1 el
# ejemplo uart_pc_echo_12mhz_brclk de TI (sin modificar), lo arranca con
# REBOOT_RESET y comprueba su eco por el mismo puerto (`make bsl-p4`). Con
# --scripter RUTA (o BSL_SCRIPTER=RUTA) además pasa el guion de ejemplo de
# BSL Scripter de TI por un pty de socat (solo Linux y macOS).
# =============================================================================
import argparse
import os
import shutil
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

MENSAJES = {0x00: "bien", 0x01: "la escritura no se comprueba", 0x02: "fallo de la Flash",
            0x04: "BSL cerrado", 0x05: "contrasena mala", 0x07: "orden desconocida"}


class Error(Exception):
    pass


def crc(datos):
    c = 0xFFFF
    for d in datos:
        x = ((c >> 8) ^ d) & 0xFF
        x ^= x >> 4
        c = ((c << 8) ^ (x << 12) ^ (x << 5) ^ x) & 0xFFFF
    return c


def lee_imagen(ruta):
    """{dirección: bytes} de un TI-TXT, Intel HEX o binario (en 0x0)"""
    trozos = {}
    if ruta.lower().endswith(".bin"):
        return {0: open(ruta, "rb").read()}
    texto = open(ruta).read()
    if ruta.lower().endswith(".hex"):
        base = 0
        for l in texto.split():
            if not l.startswith(":"):
                continue
            b = bytes.fromhex(l[1:])
            n, a, t = b[0], (b[1] << 8) | b[2], b[3]
            if t == 0:
                trozos.setdefault(base + a, bytearray()).extend(b[4:4 + n])
            elif t == 4:
                base = ((b[4] << 8) | b[5]) << 16
            elif t == 2:
                base = ((b[4] << 8) | b[5]) << 4
    else:
        a = None
        for l in texto.split("\n"):
            l = l.strip()
            if not l or l.lower() == "q":
                continue
            if l.startswith("@"):
                a = int(l[1:], 16)
                trozos[a] = bytearray()
            else:
                trozos[a].extend(bytes.fromhex(l))
    # juntar los que siguen uno a otro
    res = {}
    for a in sorted(trozos):
        for b in res:
            if b + len(res[b]) == a:
                res[b].extend(trozos[a])
                break
        else:
            res[a] = bytearray(trozos[a])
    return res


class Bsl:
    def __init__(self, puerto, baudios=9600, plazo=5.0):
        self.s = serial.serial_for_url(puerto, baudrate=baudios, parity=serial.PARITY_EVEN,
                                       timeout=plazo)

    def cierra(self):
        self.s.close()

    def _byte(self):
        b = self.s.read(1)
        if not b:
            raise Error("el BSL no contesta")
        return b[0]

    def sincroniza(self):
        self.s.reset_input_buffer()
        for _ in range(10):
            self.s.write(b"\xFF")
            b = self.s.read(1)
            if b == b"\x00":
                return
        raise Error("sin respuesta a la sincronia (0xFF)")

    def orden(self, datos, respuesta=True):
        self.s.write(b"\x80" + struct.pack("<H", len(datos)) + bytes(datos) +
                     struct.pack("<H", crc(datos)))
        ack = self._byte()
        if ack != 0x00:
            raise Error("ACK 0x%02X" % ack)
        if not respuesta:
            return None
        if self._byte() != 0x80:
            raise Error("respuesta sin 0x80")
        n = struct.unpack("<H", self.s.read(2))[0]
        d = self.s.read(n)
        c = struct.unpack("<H", self.s.read(2))[0]
        if len(d) != n or crc(d) != c:
            raise Error("respuesta con el CRC mal")
        return d

    def mensaje(self, datos, que):
        r = self.orden(datos)
        if len(r) != 2 or r[0] != 0x3B:
            raise Error("%s: respuesta inesperada %s" % (que, r.hex()))
        if r[1] != 0:
            raise Error("%s: %s (0x%02X)" % (que, MENSAJES.get(r[1], "?"), r[1]))

    def password(self, pw=b"\xFF" * 256):
        self.mensaje(b"\x21" + pw, "RX_PASSWORD")

    def borrado_masivo(self):
        self.mensaje(b"\x15", "MASS_ERASE")

    def escribe(self, dir_, datos, trozo=256):
        for i in range(0, len(datos), trozo):
            self.mensaje(b"\x20" + struct.pack("<I", dir_ + i) + bytes(datos[i:i + trozo]),
                         "RX_DATA_BLOCK_32 en 0x%X" % (dir_ + i))

    def crc_de(self, dir_, n):
        r = self.orden(b"\x26" + struct.pack("<IH", dir_, n))
        if len(r) != 3 or r[0] != 0x3A:
            raise Error("CRC_CHECK_32: respuesta inesperada")
        return r[1] | (r[2] << 8)

    def version(self):
        r = self.orden(b"\x19")
        return r[1:]

    def load_pc(self, dir_):
        self.orden(b"\x27" + struct.pack("<I", dir_), respuesta=False)

    def reset(self):
        self.s.write(b"\x80\x01\x00\x25\x37\x95")


def programa(a):
    trozos = lee_imagen(a.fichero)
    pw = b"\xFF" * 256
    if a.password:
        p = lee_imagen(a.password)
        pw = bytes(p[min(p)])[:256]
    b = Bsl(a.puerto, a.b)
    try:
        b.sincroniza()
        b.password(pw)
        b.borrado_masivo()
        t0 = time.time()
        total = 0
        for d in sorted(trozos):
            b.escribe(d, trozos[d])
            if b.crc_de(d, len(trozos[d])) != crc(trozos[d]):
                raise Error("el CRC de 0x%X no cuadra" % d)
            total += len(trozos[d])
        print("  %d bytes en %d trozo(s), en %.1f s; CRC comprobado" %
              (total, len(trozos), time.time() - t0))
        if a.r is not None:
            b.load_pc(a.r)
            print("  LOAD_PC 0x%08X" % a.r)
        else:
            b.reset()
            print("  REBOOT_RESET")
    finally:
        b.cierra()


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Sim:
    def __init__(self, sim, extra):
        self.log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
        self.p = subprocess.Popen([sim, "placas/msp_exp432p401r.xml", "--sin-firmware"] + extra,
                                  stdout=self.log, stderr=subprocess.STDOUT)

    def espera(self, puerto):
        t0 = time.time()
        while time.time() - t0 < 30:
            if self.p.poll() is not None:
                return False
            try:
                socket.create_connection(("127.0.0.1", puerto), timeout=1).close()
                time.sleep(0.3)
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def para(self, mostrar):
        self.p.terminate()
        try:
            self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
        self.log.close()
        if mostrar:
            print(open(self.log.name).read()[-3000:])
        os.unlink(self.log.name)


def prueba(a):
    fallos = 0

    def check(cond, texto):
        nonlocal fallos
        print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
        sys.stdout.flush()
        if not cond:
            fallos += 1
        return cond

    print("--- BSL del MSP432P401R por RFC 2217: el B1 y el ejemplo de eco de TI ---")
    puerto = puerto_libre()
    s = Sim(a.sim, ["--serie", "BCL=rfc2217:%d" % puerto])
    try:
        if not check(s.espera(puerto), "el simulador escucha en el %d" % puerto):
            return 1
        img = open("verif/fw/msp432p4/sdk_ejemplos/uart_pc_echo_12mhz_brclk/"
                   "uart_pc_echo_12mhz_brclk.bin", "rb").read()
        b = Bsl("rfc2217://127.0.0.1:%d" % puerto, 9600, plazo=20)
        try:
            b.sincroniza()
            check(True, "0xFF a 9600 8E1: el BSL contesta 0x00")
            try:
                b.password(b"\x00" * 256)
                check(False, "una contrasena mala se rechaza")
            except Error as e:
                check("contrasena mala" in str(e), "una contrasena mala se rechaza (0x05)")
            b.password()
            check(True, "la de un chip en blanco (256 x 0xFF) abre el BSL")
            v = b.version()
            check(len(v) == 10, "TX_BSL_VERSION: %s" % v.hex(" "))
            b.borrado_masivo()
            check(True, "MASS_ERASE")
            t0 = time.time()
            b.escribe(0, img)
            check(b.crc_de(0, len(img)) == crc(img),
                  "uart_pc_echo (%d bytes) en 0x0, y su CRC_CHECK cuadra (%.1f s de pared)"
                  % (len(img), time.time() - t0))
            b.reset()
            check(True, "REBOOT_RESET")
            # El ejemplo hace eco a 9600 8N1: el puerto cambia de formato
            time.sleep(0.5)
            b.s.parity = serial.PARITY_NONE
            b.s.reset_input_buffer()
            b.s.write(b"hola BSL")
            eco = b.s.read(8)
            check(eco == b"hola BSL", "y el ejemplo de TI arranca y hace eco: %r" % eco)
        except Error as e:
            check(False, str(e))
        finally:
            b.cierra()
    finally:
        s.para(fallos > 0)

    scripter = a.scripter or os.environ.get("BSL_SCRIPTER")
    if scripter:
        fallos += prueba_scripter(a, scripter, check)
    print("TOTAL bsl_p4: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


def prueba_scripter(a, scripter, check):
    """El guion de ejemplo de BSL Scripter (script_P4xx_uart.txt) por un pty"""
    if not shutil.which("socat"):
        print("  [----] sin socat: no se prueba BSL Scripter")
        return 0
    print("--- BSL Scripter de TI contra el B1, por un pty de socat ---")
    ejemplos = os.path.join(os.path.dirname(scripter), "ScriptExampleLinux", "P4xx_uart")
    if not os.path.isdir(ejemplos):
        check(False, "no encuentro %s" % ejemplos)
        return 1
    d = tempfile.mkdtemp()
    for f in os.listdir(ejemplos):
        shutil.copy(os.path.join(ejemplos, f), d)
    placa = os.path.join(d, "placa.xml")
    with open("placas/msp_exp432p401r.xml") as fi, open(placa, "w") as fo:
        fo.write(fi.read().replace('baudios="host" formato="8N1"', 'baudios="9600" formato="8E1"'))
    puerto = puerto_libre()
    # BSL Scripter exige un nombre /dev/tty...
    tty = "/dev/ttyBSLsim%d" % puerto
    s = subprocess.Popen([a.sim, placa, "--sin-firmware", "--serie", "BCL=tcp:%d" % puerto,
                          "--tiempo-real", "--espera-terminal"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    so = subprocess.Popen(["socat", "PTY,link=%s,raw,echo=0" % tty, "TCP:127.0.0.1:%d" % puerto],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    guion = os.path.join(d, "script_P4xx_uart.txt")
    t = open(guion).read()
    t = "\n".join(("MODE P4xx UART 9600 %s PARITY" % tty) if l.startswith("MODE") else l
                  for l in t.split("\n"))
    open(guion, "w").write(t)
    try:
        r = subprocess.run([scripter, guion], cwd=d, capture_output=True, text=True, timeout=200)
        sal = r.stdout
        ok = ("Initialization of P4xx BSL succeed" in sal and "BSL Password is correct" in sal and
              "Mass Erase is successful" in sal and "Vendor:" in sal)
        check(ok, "el guion de TI: sincronia, contrasena, borrado, Blink_LED, version, "
                  "lectura de 16 KB y reboot")
        leido = os.path.join(d, "ReadBlock_MSP432P401R.txt")
        blink = lee_imagen(os.path.join(d, "Blink_LED.txt"))
        img = lee_imagen(leido) if os.path.exists(leido) else {}
        coincide = bool(img) and all(bytes(img[min(img)])[k:k + len(v)] == bytes(v)
                                     for k, v in blink.items())
        check(coincide, "y lo que lee de vuelta es Blink_LED")
        return 0 if ok and coincide else 1
    except (subprocess.TimeoutExpired, OSError) as e:
        check(False, "BSL Scripter: %s" % e)
        return 1
    finally:
        so.terminate()
        s.terminate()
        s.wait(10)
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description="El BSL del MSP432P4")
    ap.add_argument("puerto", nargs="?")
    ap.add_argument("fichero", nargs="?")
    ap.add_argument("--password")
    ap.add_argument("-r", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--reset", action="store_true")
    ap.add_argument("-b", type=int, default=9600)
    ap.add_argument("--prueba", action="store_true")
    ap.add_argument("--scripter")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    a = ap.parse_args()
    if a.prueba:
        sys.exit(prueba(a))
    if not a.puerto or not a.fichero:
        ap.error("hacen falta PUERTO y fichero (o --prueba)")
    try:
        programa(a)
    except Error as e:
        print("  error: %s" % e)
        sys.exit(1)


if __name__ == "__main__":
    main()
