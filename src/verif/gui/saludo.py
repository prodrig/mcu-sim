#!/usr/bin/env python3
# =============================================================================
# saludo.py — El saludo con mcu-sim-gui, contra el mcu-sim de verdad
#
# Fase 3 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`).
# Este script hace de VENTANA: escucha en un puerto libre, arranca el
# `mcu-sim` de verdad con `--gui 127.0.0.1:PUERTO` y una placa de verdad
# (`placas/discovery_min.xml`), y hace el saludo con él.
#
# EL MARCO ESTÁ ESCRITO AQUÍ OTRA VEZ, en Python, y es a propósito: los dos
# extremos de verdad -mcu-sim y mcu-sim-gui- comparten `proto_io.h`, así que un
# error en ese fichero no lo cazaría ninguno de los dos. Esta implementación
# no comparte nada con aquella salvo `doc/protocolo.md`.
#
# Lo que pide el plan, y alguna cosa más:
#
#   S1  la placa y el catálogo: cada componente de la placa está en el
#       catálogo con su id y su tipo, y declara lo que su tipo declara;
#   S2  sin T_ARRANCA, mcu-sim ESPERA: sigue vivo, no gasta CPU, contesta a
#       T_PING, y cuando por fin llega T_PARA dice en su T_FIN que el tiempo
#       simulado sigue en CERO. «Para siempre sin avanzar un picosegundo»;
#   S3  con T_ARRANCA simula su ventana y termina con un T_FIN que lleva el
#       instante final. Y la simulación es LA MISMA que sin --gui: los mismos
#       LEDs y el mismo número de deltas. La conexión no la toca;
#   S4  --valida --gui: la placa y el catálogo, y T_FIN sin T_LISTO;
#   S5  los errores: nadie escuchando, una GUI que no habla ninguna versión,
#       una que cierra antes de arrancar. Código 2 y un mensaje que lo dice.
#
# La CPU se mide con /proc en Linux, y con psutil si está instalado en las
# demás; si no hay ninguna de las dos, esa comprobación se salta diciéndolo.
#
#   make -f Makefile.mcu-sim gui-saludo
#   python3 verif/gui/saludo.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import socket
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

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


def grupo(t):
    print("--- %s ---" % t)
    sys.stdout.flush()


# --- El marco, desde doc/protocolo.md §2 -------------------------------------
MAGIA = 0x3147534D
CAB = struct.Struct("<IHHII")          # magia, version, tipo, longitud, secuencia

T_HOLA, T_PLACA, T_CATALOGO, T_LISTO = 0x0001, 0x0002, 0x0003, 0x0004
T_PONG, T_FIN = 0x0014, 0x001F
T_VERSION, T_SUSCRIBE, T_ARRANCA, T_ORDENES, T_PARA, T_PING = (
    0x8000, 0x8001, 0x8002, 0x8006, 0x8007, 0x8008)
M_VENTANA, M_PARA = 0, 1
RIT_LIBRE = 1


class Ventana:
    """Un extremo de pantalla mínimo, sobre un socket de verdad."""

    def __init__(self):
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(1)
        self.puerto = self.srv.getsockname()[1]
        self.c = None
        self.buf = b""
        self.sec = 0

    def acepta(self, seg=20.0):
        self.srv.settimeout(seg)
        try:
            self.c, _ = self.srv.accept()
            return True
        except socket.timeout:
            return False

    def manda(self, tipo, cuerpo=b""):
        self.c.sendall(CAB.pack(MAGIA, 1, tipo, len(cuerpo), self.sec) + cuerpo)
        self.sec += 1

    def recibe(self, seg=10.0):
        """(tipo, cuerpo), o (None, None) si se cierra o se acaba el plazo."""
        fin = time.time() + seg
        while True:
            if len(self.buf) >= CAB.size:
                magia, ver, tipo, lon, _ = CAB.unpack(self.buf[:CAB.size])
                if magia != MAGIA or ver != 1:
                    raise ValueError("cabecera mala: magia %08X version %d" % (magia, ver))
                if len(self.buf) >= CAB.size + lon:
                    cuerpo = self.buf[CAB.size:CAB.size + lon]
                    self.buf = self.buf[CAB.size + lon:]
                    return tipo, cuerpo
            quedan = fin - time.time()
            if quedan <= 0:
                return None, None
            self.c.settimeout(quedan)
            try:
                d = self.c.recv(65536)
            except socket.timeout:
                return None, None
            except OSError:
                return None, None
            if not d:
                return None, None
            self.buf += d

    def cierra(self):
        for s in (self.c, self.srv):
            if s:
                try:
                    s.close()
                except OSError:
                    pass


def claves(texto):
    d = {}
    for linea in texto.decode("utf-8").splitlines():
        if "=" in linea:
            k, v = linea.split("=", 1)
            d[k] = v
    return d


def arranca(sim, args, puerto):
    return subprocess.Popen([sim] + args + ["--gui", "127.0.0.1:%d" % puerto],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def termina(p, seg=20.0):
    try:
        out, err = p.communicate(timeout=seg)
        return p.returncode, out.decode("utf-8", "replace"), err.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        p.kill()
        out, err = p.communicate()
        return None, out.decode("utf-8", "replace"), err.decode("utf-8", "replace")


def saludo_hasta_listo(v, valida=False):
    """El saludo desde la ventana. Devuelve (hola, placa, catalogo, listo)."""
    t, hola = v.recibe()
    if t != T_HOLA:
        return None, None, None, False
    v.manda(T_VERSION, b"protocolo=1\ngui=saludo.py\n")
    t1, placa = v.recibe()
    t2, cat = v.recibe()
    if t1 != T_PLACA or t2 != T_CATALOGO:
        return claves(hola), None, None, False
    if valida:
        return claves(hola), placa, cat, False
    t3, _ = v.recibe()
    return claves(hola), placa, cat, t3 == T_LISTO


def fin(cuerpo):
    if cuerpo is None or len(cuerpo) != 16:
        return None
    return struct.unpack("<IiQ", cuerpo)       # motivo, codigo, t_sim_ns


# --- La CPU de otro proceso --------------------------------------------------
def cpu_de(pid):
    """Segundos de CPU gastados por `pid`, o None si no hay manera de saberlo."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            campos = f.read().rsplit(")", 1)[1].split()
        hz = os.sysconf("SC_CLK_TCK")
        return (int(campos[11]) + int(campos[12])) / hz      # utime + stime
    except (OSError, IndexError, ValueError):
        pass
    try:
        import psutil
        t = psutil.Process(pid).cpu_times()
        return t.user + t.system
    except Exception:
        return None


# --- Las pruebas -------------------------------------------------------------
PLACA = "placas/discovery_min.xml"
FW = "verif/fw/blinky/blinky.bin"
MS = "50"


def s1_s2_espera(sim):
    grupo("S1 La placa y el catalogo de " + PLACA)
    v = Ventana()
    p = arranca(sim, [PLACA, FW, MS], v.puerto)
    try:
        if not check(v.acepta(), "mcu-sim se conecta a la ventana"):
            return
        hola, placa, cat, listo = saludo_hasta_listo(v)
        check(hola is not None and hola.get("protocolo_max") == "1" and
              hola.get("placa") == PLACA and hola.get("modo") == "simula" and
              hola.get("mcu") == "STM32F407VG" and hola.get("firmware") == FW and
              hola.get("pid") == str(p.pid),
              "T_HOLA dice la version, la placa, el MCU, el firmware, el modo y su pid")
        check(placa is not None and cat is not None and listo,
              "y tras T_VERSION llegan T_PLACA, T_CATALOGO y T_LISTO")
        if placa is None or cat is None:
            return
        comps = [(c.get("id"), c.get("tipo"))
                 for c in ET.fromstring(placa).iter("componente")]
        piezas = {pz.get("id"): pz for pz in ET.fromstring(cat).iter("pieza")}
        check(len(comps) == 11, "la placa trae sus 11 componentes")
        check(all(i in piezas and piezas[i].get("tipo") == t for i, t in comps),
              "y CADA UNO esta en el catalogo con el mismo id y el mismo tipo: la "
              "ventana puede casar los dos XML sin conocer un tipo")
        leds = [i for i, t in comps if t == "Led"]
        check(len(leds) == 4 and all(
              i in piezas and [o.get("nombre") for o in piezas[i].iter("observable")] ==
              ["encendido", "corriente"] for i in leds),
              "los cuatro LEDs declaran encendido y corriente")
        check(all(i in piezas and
                  [m.get("nombre") for m in piezas[i].iter("mando")] == ["pulsar"]
                  for i, t in comps if t == "Button"),
              "los pulsadores, el mando pulsar")
        check(all(i in piezas and not list(piezas[i]) for i, t in comps if t == "Rpull"),
              "y las resistencias, nada")

        grupo("S2 Sin T_ARRANCA, mcu-sim espera: sin CPU y sin tiempo simulado")
        c0 = cpu_de(p.pid)
        time.sleep(2.0)
        c1 = cpu_de(p.pid)
        check(p.poll() is None, "dos segundos despues sigue vivo")
        if c0 is None or c1 is None:
            print("  [SALTA] no hay /proc ni psutil: la CPU no se puede medir aqui")
        else:
            check(c1 - c0 < 0.2, "y en esos dos segundos ha gastado %.3f s de CPU: "
                                 "duerme, no gira" % (c1 - c0))
        v.manda(T_SUSCRIBE, struct.pack("<IIII", 1000000, 0, 1, 0) + struct.pack("<H", 2))
        v.manda(T_PING)
        t, _ = v.recibe()
        check(t == T_PONG, "un T_SUSCRIBE se ignora (es de la fase 4), y un T_PING tiene "
                           "su T_PONG: espera, pero escucha")
        v.manda(T_PARA)
        t, cuerpo = v.recibe()
        f = fin(cuerpo)
        check(t == T_FIN and f is not None and f[0] == M_PARA and f[2] == 0,
              "a T_PARA contesta T_FIN con el tiempo simulado en CERO: no ha "
              "avanzado un picosegundo")
        rc, out, err = termina(p)
        check(rc == 0 and "simulados" not in out,
              "y termina con codigo 0 sin haber simulado nada")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def s3_arranca(sim):
    grupo("S3 Con T_ARRANCA simula su ventana, y la misma simulacion que sin --gui")
    sin = subprocess.run([sim, PLACA, FW, MS], capture_output=True, text=True, timeout=120)
    v = Ventana()
    p = arranca(sim, [PLACA, FW, MS], v.puerto)
    try:
        if not v.acepta():
            check(False, "mcu-sim se conecta")
            return
        saludo_hasta_listo(v)
        # Lo que la fase 4 y la 5 usaran, mandado ahora: tiene que no cambiar nada
        v.manda(T_SUSCRIBE, struct.pack("<IIII", 1000000, 0, 1, 0) + struct.pack("<H", 2))
        v.manda(T_ORDENES, struct.pack("<QHHf", 1000000, 6, 0, 1.0))
        v.manda(T_ARRANCA, struct.pack("<IfQ", RIT_LIBRE, 1.0, 0))
        t, cuerpo = v.recibe(seg=120)
        f = fin(cuerpo)
        # El instante final no es la ventana a secas: antes de ella, `run()`
        # simula el arranque electrico -10 us con todo a cero y 100 us hasta
        # soltar NRST-. Asi que son 50 ms y 110 us, y eso es lo que tiene que
        # decir: el instante REAL, no el pedido.
        esp = int(MS) * 1000000 + 110000
        check(t == T_FIN and f is not None and f[0] == M_VENTANA and f[1] == 0 and
              f[2] == esp,
              "al acabar la ventana llega T_FIN con el instante final real: %s ms "
              "mas los 110 us del arranque electrico (%s ns)"
              % (MS, f[2] if f else "?"))
        rc, out, err = termina(p)
        check(rc == 0 and "gui: la ventana dice 'arranca'" in out,
              "y termina con codigo 0")

        def simulacion(texto):
            # Lo que dice la simulacion de si misma: los deltas y los LEDs. No
            # el tiempo de anfitrion, que es de la maquina.
            r = []
            for l in texto.splitlines():
                if l.startswith("simulados"):
                    r.append(l.split("(")[1])
                elif l.strip().startswith("LED "):
                    r.append(l.strip())
            return r
        a, b = simulacion(sin.stdout), simulacion(out)
        check(len(a) == 5 and a == b,
              "los mismos deltas y los mismos cuatro LEDs que sin --gui: la ventana, "
              "lo que se suscriba y lo que ordene en esta fase no tocan la "
              "simulacion (%s)" % (a[0] if a else "?"))
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def s4_valida(sim):
    grupo("S4 --valida --gui: la placa y el catalogo, y nada mas")
    v = Ventana()
    p = arranca(sim, [PLACA, "--valida"], v.puerto)
    try:
        if not v.acepta():
            check(False, "mcu-sim se conecta")
            return
        hola, placa, cat, _ = saludo_hasta_listo(v, valida=True)
        check(hola is not None and hola.get("modo") == "valida", "T_HOLA dice modo=valida")
        t, cuerpo = v.recibe()
        f = fin(cuerpo)
        check(placa is not None and cat is not None and t == T_FIN and f is not None and
              f[2] == 0,
              "T_PLACA, T_CATALOGO y T_FIN, sin T_LISTO y sin esperar a nadie")
        rc, out, err = termina(p)
        check(rc == 0, "y termina con codigo 0")
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def s5_errores(sim):
    grupo("S5 Los errores: codigo 2, y dicho")
    # Nadie escuchando: un puerto que se acaba de cerrar
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    puerto = s.getsockname()[1]
    s.close()
    t0 = time.time()
    p = arranca(sim, [PLACA, MS], puerto)
    rc, out, err = termina(p)
    check(rc == 2 and "no hay nadie escuchando" in err and time.time() - t0 < 15,
          "sin ventana escuchando: codigo 2 enseguida, y lo dice")

    v = Ventana()
    p = arranca(sim, [PLACA, MS], v.puerto)
    try:
        v.acepta()
        v.recibe()
        v.manda(T_VERSION, b"protocolo=0\n")
        rc, out, err = termina(p)
        check(rc == 2 and "ninguna version" in err,
              "una ventana que no habla ninguna version: codigo 2, y lo dice")
    finally:
        v.cierra()

    v = Ventana()
    p = arranca(sim, [PLACA, MS], v.puerto)
    try:
        v.acepta()
        saludo_hasta_listo(v)
        v.c.close()
        v.c = None
        rc, out, err = termina(p)
        check(rc == 2 and "cerro" in err and "simulados" not in out,
              "una ventana que se cierra antes de arrancar: codigo 2, sin simular")
    finally:
        v.cierra()


def main():
    a = argparse.ArgumentParser(description="El saludo con mcu-sim-gui, contra el mcu-sim de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    s1_s2_espera(sim)
    s3_arranca(sim)
    s4_valida(sim)
    s5_errores(sim)
    print("\n=====================================================")
    print("TOTAL SALUDO : %d comprobaciones OK, %d fallos" % (ok, fallos))
    print("=====================================================")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
