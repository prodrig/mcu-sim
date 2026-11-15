#!/usr/bin/env python3
# =============================================================================
# esptool_c3.py — esptool DE VERDAD contra el ESP32-C3 simulado, por la UART0
#
# (No se llama esptool.py: `import esptool` se importaria a si mismo.)
#
# E5 de doc/analisis_esp32_riscv.md (D2). La placa placas/esp32c3_devkitm1.xml
# lleva lo que lleva la de verdad entre el CP2102N y el chip: el PuenteSerie
# de la UART0 con su DTR y su RTS y el reinicio automatico (AutoResetEsp: los
# dos transistores y el RC de CHIP_PU). Con `reinicio="esp"` el puente hace el
# reinicio de esptool en tiempo simulado, como esp_rfc2217_server con una placa
# de verdad. Lo demas es la ROM del C3 (la de verdad, rev. 3): detecta los
# baudios del SYNC (UART_LOWPULSE/HIGHPULSE), habla SLIP, carga el stub de
# esptool en la SRAM y lo arranca, o escribe ella misma (--no-stub).
#
#   python3 verif/esp/esptool_c3.py [--sim build/mcu-sim] [--esptool RUTA]
#
# Lo que se comprueba, por rfc2217://127.0.0.1:PUERTO (un puerto libre):
#
#   1. chip-id, con el stub, a 115200: el reinicio a descarga, el SYNC, la
#      lectura de los eFuse y el stub en marcha
#   2. flash-id --no-stub: el JEDEC de la Flash interna (GigaDevice, 4 MB)
#   3. write-flash a 921600 con el stub: blink entero (bootloader, tabla de
#      particiones y aplicacion), comprimido, con su verificacion (MD5)
#   4. verify-flash --no-stub: el MD5 de la aplicacion, calculado por la ROM
#   5. write-flash --no-stub a 921600: la tabla de particiones, sin comprimir,
#      por la ROM (CHANGE_BAUDRATE, FLASH_BEGIN/DATA/END)
#   6. y el arranque: tras el «Hard resetting via RTS pin», blink arranca y
#      saluda por el mismo puerto a 115200
#
# Sin esptool (ni en --esptool, ni en $ESPTOOL, ni en el PATH, ni como modulo
# de Python) la prueba se OMITE diciendolo, salvo con ESPTOOL_OBLIGATORIO=1
# (el CI), que falla. Hace falta pySerial (va con esptool).
#
#   make -f Makefile.mcu-sim esptool-c3
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


def busca_esptool(a):
    """La orden de esptool, como lista, o None."""
    for c in (a.esptool, os.environ.get("ESPTOOL")):
        if c:
            return [c]
    for n in ("esptool", "esptool.py"):
        r = shutil.which(n)
        if r:
            return [r]
    try:
        import esptool  # noqa: F401
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        return None


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


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
    ver = subprocess.run(et + ["version"], capture_output=True, text=True).stdout.strip().splitlines()
    print("--- esptool (%s) contra placas/esp32c3_devkitm1.xml, por RFC 2217 ---" % (ver[-1] if ver else "?"))

    for d in ("hello_world", "blink"):
        if not os.path.exists(os.path.join(IDF, d, "flash_args")):
            check(False, "falta %s/%s: make -f Makefile.mcu-sim build/idf/.extraido" % (IDF, d))
            return 1

    puerto = puerto_libre()
    url = "rfc2217://127.0.0.1:%d" % puerto
    log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    p = subprocess.Popen([a.sim, "placas/esp32c3_devkitm1.xml", os.path.join(IDF, "hello_world", "flash_args"),
                          "--serie", "UART=rfc2217:%d" % puerto], stdout=log, stderr=subprocess.STDOUT)

    def esptool(*args, cwd=None, plazo=600):
        r = subprocess.run(et + ["--port", url] + list(args), capture_output=True, text=True, cwd=cwd,
                           timeout=plazo)
        if a.v:
            print(r.stdout + r.stderr)
        return r.returncode, r.stdout + r.stderr

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
        time.sleep(0.5)

        t = time.time()
        rc, o = esptool("chip-id")
        check(rc == 0 and "Chip type:          ESP32-C3" in o and "Stub flasher running" in o,
              "chip-id (stub, 115200): ESP32-C3, el stub en marcha (%.0f s)" % (time.time() - t))
        check("revision v0.4" in o, "la revision de los eFuse: v0.4")

        t = time.time()
        rc, o = esptool("--no-stub", "flash-id")
        check(rc == 0 and "Manufacturer: c8" in o and "Device: 4016" in o and "4MB" in o,
              "flash-id --no-stub: JEDEC c8/4016, 4 MB (%.0f s)" % (time.time() - t))

        blink = os.path.join(IDF, "blink")
        t = time.time()
        rc, o = esptool("-b", "921600", "write-flash", "@flash_args", cwd=blink)
        check(rc == 0 and o.count("Hash of data verified") == 3 and "Changing baud rate to 921600" in o,
              "write-flash a 921600 con el stub: blink (3 imagenes, MD5 bien) (%.0f s)" % (time.time() - t))

        t = time.time()
        rc, o = esptool("--no-stub", "verify-flash", "0x10000", "blink.bin", cwd=blink)
        check(rc == 0 and "verification successful" in o.lower(),
              "verify-flash --no-stub: el MD5 de la ROM cuadra con blink.bin (%.0f s)" % (time.time() - t))

        t = time.time()
        rc, o = esptool("--no-stub", "-b", "921600", "write-flash", "0x8000",
                        os.path.join("partition_table", "partition-table.bin"), cwd=blink)
        check(rc == 0 and "Hash of data verified" in o and "Hard resetting via RTS pin" in o,
              "write-flash --no-stub a 921600: la tabla de particiones, por la ROM (%.0f s)" % (time.time() - t))

        # El arranque: el puerto abierto SIN activar DTR ni RTS (como
        # idf.py monitor), a 115200
        s = serial.serial_for_url(url, do_not_open=True, baudrate=115200, timeout=1)
        s.dtr = False
        s.rts = False
        s.open()
        visto = b""
        fin = time.time() + 60
        while time.time() < fin and b"Turning the LED" not in visto:
            visto += s.read(256)
        s.close()
        check(b"Turning the LED" in visto, "y blink arranca: \"Turning the LED\" por la UART0")
    except subprocess.TimeoutExpired as e:
        check(False, "esptool no acaba: %s" % e)
    finally:
        p.terminate()
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
        log.close()
        if fallos:
            print(open(log.name, errors="replace").read()[-3000:])
        os.unlink(log.name)
    print("TOTAL esptool C3: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


def main():
    ap = argparse.ArgumentParser(description="esptool contra el ESP32-C3 simulado")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    ap.add_argument("--esptool", default=None)
    ap.add_argument("-v", action="store_true", help="lo que dice esptool, entero")
    a = ap.parse_args()
    sys.exit(prueba(a))


if __name__ == "__main__":
    main()
