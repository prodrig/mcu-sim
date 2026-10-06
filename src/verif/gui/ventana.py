# =============================================================================
# ventana.py — Una ventana de mcu-sim-gui mínima, en Python, para las pruebas
#
# Lo que comparten `saludo.py` (fase 3), `marcha.py` (fase 4) y `ordenes.py`
# (fase 5): el marco del protocolo escrito OTRA VEZ a partir de
# `doc/protocolo.md`, sin compartir nada con `proto_io.h` —que es el mismo
# fichero en los dos extremos de verdad, así que un error en él no lo cazaría
# ninguno de los dos—, una ventana que escucha y saluda, y el contador de
# comprobaciones.
# =============================================================================
import os
import socket
import struct
import subprocess
import sys
import time

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
T_ILUSTRACION = 0x0005
T_INSTANTANEA, T_AVISO, T_ESTADO, T_ORDEN_HECHA = 0x0010, 0x0011, 0x0012, 0x0013
T_PONG, T_FIN = 0x0014, 0x001F
T_VERSION, T_SUSCRIBE, T_ARRANCA, T_ORDENES, T_PARA, T_PING = (
    0x8000, 0x8001, 0x8002, 0x8006, 0x8007, 0x8008)
T_PAUSA, T_SIGUE, T_PASO = 0x8003, 0x8004, 0x8005
M_VENTANA, M_PARA, M_ERROR = 0, 1, 2
N_INFO, N_AVISO, N_ERROR, N_FATAL = 0, 1, 2, 3
F_CORRIENDO, F_TERMINADA = 1, 3
RIT_REAL, RIT_LIBRE, RIT_DEMANDA = 0, 1, 2
F_ESPERANDO, F_PAUSADA = 0, 2
RES_OK, RES_PIEZA, RES_MANDO, RES_RANGO, RES_TARDE = 0, 1, 2, 3, 4


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
        # La version EN USO: la 1 -la del saludo- hasta que T_VERSION elige
        # otra (saludo_hasta_listo). Desde ahi, la llevan las cabeceras.
        self.version = 1

    def acepta(self, seg=20.0):
        self.srv.settimeout(seg)
        try:
            self.c, _ = self.srv.accept()
            return True
        except socket.timeout:
            return False

    def manda(self, tipo, cuerpo=b""):
        self.c.sendall(CAB.pack(MAGIA, self.version, tipo, len(cuerpo), self.sec) + cuerpo)
        self.sec += 1

    def recibe(self, seg=10.0):
        """(tipo, cuerpo), o (None, None) si se cierra o se acaba el plazo."""
        fin = time.time() + seg
        while True:
            if len(self.buf) >= CAB.size:
                magia, ver, tipo, lon, _ = CAB.unpack(self.buf[:CAB.size])
                if magia != MAGIA or ver not in (1, self.version):
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


def saludo_hasta_listo(v, valida=False, version=1):
    """El saludo desde la ventana. Devuelve (hola, placa, catalogo, listo).
    `version` es la del protocolo que elige: la 2 recibe un <sistema> en
    T_PLACA si lo es, la 1 lo recibe con la raiz <placa> de siempre."""
    t, hola = v.recibe()
    if t != T_HOLA:
        return None, None, None, False
    v.manda(T_VERSION, ("protocolo=%d\ngui=saludo.py\n" % version).encode())
    v.version = version
    t1, placa = v.recibe()
    t2, cat = v.recibe()
    if t1 != T_PLACA or t2 != T_CATALOGO:
        return claves(hola), None, None, False
    # Desde la fase 4, los avisos de placa llegan aqui, entre T_CATALOGO y
    # T_LISTO. Se guardan en `v.avisos_placa`. Y antes que ellos, desde las
    # ilustraciones, los dibujos de las placas: en `v.ilustraciones`, como
    # (cabeceras, svg) -véase `ilustracion()`-.
    v.avisos_placa = []
    v.ilustraciones = []
    if valida:
        return claves(hola), placa, cat, False
    while True:
        t3, c3 = v.recibe()
        if t3 == T_ILUSTRACION:
            v.ilustraciones.append(ilustracion(c3))
            continue
        if t3 != T_AVISO:
            return claves(hola), placa, cat, t3 == T_LISTO
        v.avisos_placa.append(aviso(c3))


def ilustracion(cuerpo):
    """Un T_ILUSTRACION: (cabeceras, svg). Las cabeceras hasta la primera
    linea en blanco, como en T_HOLA; detras, el SVG tal cual."""
    cab, _, svg = cuerpo.partition(b"\n\n")
    return claves(cab), svg


def resto_del_saludo(v, seg=10.0):
    """Con --valida, lo que llega tras el catalogo hasta T_FIN: los dibujos y
    los avisos, en `v.ilustraciones` y `v.avisos_placa`."""
    while True:
        t, c = v.recibe(seg)
        if t is None or t == T_FIN:
            return t == T_FIN
        if t == T_ILUSTRACION:
            v.ilustraciones.append(ilustracion(c))
        elif t == T_AVISO:
            v.avisos_placa.append(aviso(c))


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


# --- Los cuerpos de la fase 4 ---------------------------------------------------
def suscribe(v, periodo_ns, ids):
    v.manda(T_SUSCRIBE, struct.pack("<IIII", periodo_ns & 0xFFFFFFFF, periodo_ns >> 32,
                                    len(ids), 0) + b"".join(struct.pack("<H", i) for i in ids))


def instantanea(cuerpo):
    """(t_sim_ns, perdidas, [(id, valor), ...])"""
    t, n, perdidas = struct.unpack("<QII", cuerpo[:16])
    m = [struct.unpack("<HHf", cuerpo[16 + 8 * k:24 + 8 * k]) for k in range(n)]
    return t, perdidas, [(i, v) for i, _, v in m]


def aviso(cuerpo):
    """(nivel, t_sim_ns, origen, texto)"""
    nivel, lon, t = struct.unpack("<IIQ", cuerpo[:16])
    resto = cuerpo[16:]
    return nivel, t, resto[:lon].decode("utf-8", "replace"), resto[lon:].decode("utf-8", "replace")


def estado(cuerpo):
    """(fase, t_sim_ns, t_pared_s, deltas)"""
    fase, _, t, pared, deltas = struct.unpack("<IIQdQ", cuerpo)
    return fase, t, pared, deltas


# --- Los cuerpos de la fase 5 ---------------------------------------------------
def ordenes(v, lista):
    """Un T_ORDENES con [(t_sim_ns, pieza, mando, valor), ...] (doc/protocolo.md §5)."""
    v.manda(T_ORDENES, b"".join(struct.pack("<QHHf", t, p, m, x) for t, p, m, x in lista))


def hecha(cuerpo):
    """(t_sim_ns, pieza, mando, valor, resultado)"""
    t, p, m, x, r, _ = struct.unpack("<QHHfII", cuerpo)
    return t, p, m, x, r


# --- T_ARRANCA, y el control de la fase 6 ---------------------------------------
def arranque(v, ritmo=RIT_LIBRE, factor=1.0, ventana_ns=0):
    """T_ARRANCA. ventana_ns = 0 es la de mcu-sim: la de su linea de ordenes, o
    sin fin si no se le dio ninguna."""
    v.manda(T_ARRANCA, struct.pack("<IfQ", ritmo, factor, ventana_ns))


def paso(v, ns):
    v.manda(T_PASO, struct.pack("<Q", ns))


def resumen(nombre):
    print("\n=====================================================")
    print("TOTAL %s : %d comprobaciones OK, %d fallos" % (nombre, ok, fallos))
    print("=====================================================")
    return 1 if fallos else 0
