#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =============================================================================
# eco_e4.py — El otro extremo del cable del MSP432E401Y: una pila IPv4 mínima
#             que habla con el ejemplo de lwIP (e4_eth) por el puente Ethernet
#
# El puente (`--eth qemu:PUERTO`) saca las tramas del EMAC por TCP con el
# protocolo de `-netdev stream` de QEMU: cada trama, con su longitud delante
# en 32 bits big-endian. Este programa se conecta como lo haría gvproxy y hace
# de ordenador en 192.168.127.1, con su propia ARP, ICMP, UDP y TCP —lo justo
# para una prueba, escrito aquí sin bibliotecas—, y comprueba:
#
#   * ARP: el MCU contesta a quién tiene 192.168.127.2, con su MAC
#   * ICMP: un ping con 32 bytes vuelve igual
#   * UDP: el eco del puerto 7 devuelve el datagrama
#   * TCP: el eco del puerto 7 (RFC 862): tres pasos, datos, eco y cierre
#   * y en todo lo que manda el MCU, que las sumas de comprobación de IP,
#     ICMP, UDP y TCP están BIEN: lwIP las deja a cero y las pone el EMAC
#     (COE), así que esto prueba el modelo del EMAC, no a lwIP
#
#   python3 eco_e4.py PUERTO              contra un mcu-sim que ya escucha
#   python3 eco_e4.py --prueba [--sim B]  arranca build/mcu-sim con la placa
#                                         y e4_eth, y hace lo mismo
#   python3 eco_e4.py --prueba --bootp    arranca con e4_bootp, que llama a
#                                         ROM_UpdateEMAC: aqui se contesta al
#                                         BOOTP y se sirve e4_eth.bin por TFTP,
#                                         y despues, lo mismo de arriba con lo
#                                         que se ha grabado
# =============================================================================
import argparse
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

MI_MAC = bytes.fromhex("0200000000fe")
MI_IP = bytes([192, 168, 127, 1])
SU_IP = bytes([192, 168, 127, 2])
SU_MAC_ESPERADA = bytes.fromhex("020000e40001")
DIFUSION = b"\xff" * 6


def suma(d, s=0):
    if len(d) % 2:
        d += b"\0"
    for i in range(0, len(d), 2):
        s += (d[i] << 8) | d[i + 1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return s


def chk(d, s=0):
    return (~suma(d, s)) & 0xFFFF


class Cable:
    def __init__(self, puerto, plazo=30.0):
        t0 = time.time()
        while True:
            try:
                self.s = socket.create_connection(("127.0.0.1", puerto), timeout=1)
                break
            except OSError:
                if time.time() - t0 > plazo:
                    raise
                time.sleep(0.2)
        self.s.settimeout(0.2)
        self.buf = b""
        self.su_mac = None

    def manda(self, t):
        self.s.sendall(struct.pack(">I", len(t)) + t)

    def recibe(self, plazo):
        fin = time.time() + plazo
        while time.time() < fin:
            if len(self.buf) >= 4:
                n = struct.unpack(">I", self.buf[:4])[0]
                if len(self.buf) >= 4 + n:
                    t, self.buf = self.buf[4:4 + n], self.buf[4 + n:]
                    if self.atiende(t):
                        continue
                    return t
            try:
                d = self.s.recv(65536)
                if not d:
                    return None
                self.buf += d
            except socket.timeout:
                pass
        return None

    # Lo que se contesta solo: las peticiones ARP del MCU
    def atiende(self, t):
        if len(t) >= 42 and t[12:14] == b"\x08\x06" and t[20:22] == b"\x00\x01" and t[38:42] == MI_IP:
            r = t[6:12] + MI_MAC + b"\x08\x06" + struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2)
            r += MI_MAC + MI_IP + t[22:28] + t[28:32]
            self.manda(r)
            return True
        return False

    def eth(self, tipo, carga):
        return (self.su_mac or DIFUSION) + MI_MAC + struct.pack(">H", tipo) + carga

    def ip(self, proto, carga, ident=0x1234):
        h = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(carga), ident, 0x4000, 64, proto, 0, MI_IP, SU_IP)
        h = h[:10] + struct.pack(">H", chk(h)) + h[12:]
        return self.eth(0x0800, h + carga)

    def espera_ip(self, proto, plazo=10.0):
        fin = time.time() + plazo
        while time.time() < fin:
            t = self.recibe(fin - time.time())
            if t is None:
                return None
            if t[12:14] == b"\x08\x00" and t[23] == proto:
                return t
        return None


def sumas_bien(t):
    """¿Están bien las sumas de una trama IPv4 que manda el MCU?"""
    ihl = (t[14] & 0xF) * 4
    total = struct.unpack(">H", t[16:18])[0]
    ip = t[14:14 + ihl]
    if suma(ip) != 0xFFFF:
        return False, "la de la cabecera IP"
    proto = t[23]
    carga = t[14 + ihl:14 + total]
    if proto == 1:
        return (suma(carga) == 0xFFFF, "la de ICMP")
    pseudo = suma(ip[12:20] + struct.pack(">BBH", 0, proto, len(carga)))
    return (suma(carga, pseudo) == 0xFFFF, "la de %s" % ("TCP" if proto == 6 else "UDP"))


def tcp(c, sport, dport, seq, ack, flags, datos=b"", ventana=8192):
    h = struct.pack(">HHIIBBHHH", sport, dport, seq, ack, 5 << 4, flags, ventana, 0, 0)
    pseudo = suma(MI_IP + SU_IP + struct.pack(">BBH", 0, 6, len(h) + len(datos)))
    s = chk(h + datos, pseudo)
    h = h[:16] + struct.pack(">H", s) + h[18:]
    return c.ip(6, h + datos)


def conversa(puerto, check, imagen=None):
    c = Cable(puerto)
    if imagen is not None and not bootp_tftp(c, imagen, check):
        return
    # --- ARP --------------------------------------------------------------
    pide = struct.pack(">HHBBH", 1, 0x0800, 6, 4, 1) + MI_MAC + MI_IP + b"\0" * 6 + SU_IP
    respuesta = None
    for _ in range(50):                       # el enlace sube 1 ms tras el arranque
        c.manda(c.eth(0x0806, pide))
        fin = time.time() + 0.5
        while time.time() < fin:
            t = c.recibe(fin - time.time())
            if t is None:
                break
            if t[12:14] == b"\x08\x06" and t[20:22] == b"\x00\x02" and t[28:32] == SU_IP:
                respuesta = t
                break
        if respuesta:
            break
    if not check(respuesta is not None, "ARP: 192.168.127.2 contesta"):
        return
    c.su_mac = respuesta[22:28]
    check(c.su_mac == SU_MAC_ESPERADA, "con la MAC del ejemplo, %s" % c.su_mac.hex(":"))

    # --- ICMP -------------------------------------------------------------
    carga = bytes(range(32))
    eco = struct.pack(">BBHHH", 8, 0, 0, 0x4534, 1) + carga
    eco = eco[:2] + struct.pack(">H", chk(eco)) + eco[4:]
    c.manda(c.ip(1, eco))
    t = c.espera_ip(1)
    check(t is not None and t[34] == 0 and t[42:42 + 32] == carga, "ping: la respuesta trae los 32 bytes")
    if t is not None:
        ok, que = sumas_bien(t)
        check(ok, "y sus sumas (IP e ICMP) las ha puesto bien el EMAC")

    # --- UDP --------------------------------------------------------------
    datos = b"hola por UDP desde el ordenador"
    u = struct.pack(">HHHH", 40001, 7, 8 + len(datos), 0) + datos
    pseudo = suma(MI_IP + SU_IP + struct.pack(">BBH", 0, 17, len(u)))
    u = u[:6] + struct.pack(">H", chk(u, pseudo)) + u[8:]
    c.manda(c.ip(17, u))
    t = c.espera_ip(17)
    check(t is not None and t[42:42 + len(datos)] == datos, "UDP 7: el datagrama vuelve igual")
    if t is not None:
        ok, que = sumas_bien(t)
        check(ok, "y su suma de UDP la ha puesto el EMAC")

    # --- TCP --------------------------------------------------------------
    sp, isn = 40002, 1000
    c.manda(tcp(c, sp, 7, isn, 0, 0x02))                       # SYN
    t = c.espera_ip(6)
    sa = t is not None and (t[47] & 0x12) == 0x12
    check(sa, "TCP 7: SYN, y vuelve SYN+ACK")
    if not sa:
        return
    ok, que = sumas_bien(t)
    check(ok, "con la suma de TCP bien puesta por el EMAC")
    su_seq = struct.unpack(">I", t[38:42])[0] + 1
    c.manda(tcp(c, sp, 7, isn + 1, su_seq, 0x10))              # ACK
    msg = b"Hola, eco TCP por el puente Ethernet de mcu-sim\r\n"
    c.manda(tcp(c, sp, 7, isn + 1, su_seq, 0x18, msg))         # PSH+ACK
    recibido = b""
    fin = time.time() + 10
    while time.time() < fin and len(recibido) < len(msg):
        t = c.espera_ip(6, fin - time.time())
        if t is None:
            break
        doff = (t[46] >> 4) * 4
        total = struct.unpack(">H", t[16:18])[0]
        d = t[34 + doff:14 + total]
        if d:
            recibido += d
            su_seq += len(d)
            c.manda(tcp(c, sp, 7, isn + 1 + len(msg), su_seq, 0x10))
    check(recibido == msg, "los %d bytes vuelven por el eco" % len(msg))
    c.manda(tcp(c, sp, 7, isn + 1 + len(msg), su_seq, 0x11))   # FIN+ACK
    fin_visto = False
    fin = time.time() + 10
    while time.time() < fin:
        t = c.espera_ip(6, fin - time.time())
        if t is None:
            break
        if t[47] & 0x01:
            fin_visto = True
            c.manda(tcp(c, sp, 7, isn + 2 + len(msg), struct.unpack(">I", t[38:42])[0] + 1, 0x10))
            break
    check(fin_visto, "y el cierre: FIN de los dos lados")


def bootp_tftp(c, imagen, check):
    """El servidor BOOTP (RFC 951) y TFTP (RFC 1350) del cargador de la ROM."""
    fin = time.time() + 60
    mac = None
    hecho = False
    bloques = 0
    puerto = 0
    tid = 40069
    while time.time() < fin and not hecho:
        t = c.recibe(fin - time.time())
        if t is None:
            break
        if t[12:14] != b"\x08\x00" or t[23] != 17:
            continue
        sp, dp = struct.unpack(">HH", t[34:38])
        d = t[42:]
        if dp == 67 and d[0] == 1:
            mac = t[6:12]
            c.su_mac = mac
            r = bytearray(300)
            r[0:3] = b"\x02\x01\x06"
            r[4:8] = d[4:8]
            r[16:20] = SU_IP
            r[20:24] = MI_IP
            r[28:34] = d[28:34]
            r[108:108 + 10] = b"e4_eth.bin"
            r[236:241] = bytes([99, 130, 83, 99, 255])
            c.manda(udp(c, 67, 68, bytes(r)))
        elif dp == 69 and d[1] == 1:
            nombre = d[2:].split(b"\0")[0]
            check(nombre == b"e4_eth.bin", "TFTP: el cargador pide %s" % nombre.decode())
            puerto = sp
            c.manda(udp(c, tid, puerto, b"\x00\x03\x00\x01" + imagen[:512]))
        elif dp == tid and d[1] == 4:
            n = struct.unpack(">H", d[2:4])[0]
            bloques += 1
            if n * 512 > len(imagen):
                hecho = True
                break
            c.manda(udp(c, tid, puerto, struct.pack(">HH", 3, n + 1) + imagen[n * 512:(n + 1) * 512]))
    check(mac == bytes.fromhex("020000e40000"),
          "BOOTP: la peticion llega por difusion, con la MAC por omision del cargador, %s"
          % (mac.hex(":") if mac else "-"))
    check(hecho, "TFTP: %d bloques de 512 de e4_eth.bin, asentidos, el ultimo corto incluido" % bloques)
    c.su_mac = None
    return hecho


def udp(c, sp, dp, datos):
    u = struct.pack(">HHHH", sp, dp, 8 + len(datos), 0) + datos
    pseudo = suma(MI_IP + SU_IP + struct.pack(">BBH", 0, 17, len(u)))
    u = u[:6] + struct.pack(">H", chk(u, pseudo)) + u[8:]
    return c.ip(17, u)


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def main():
    ap = argparse.ArgumentParser(description="Eco TCP/UDP con el MSP432E401Y por el puente Ethernet")
    ap.add_argument("puerto", nargs="?", type=int)
    ap.add_argument("--prueba", action="store_true")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim" + (".exe" if os.name == "nt" else "")))
    ap.add_argument("--pcap", default=None)
    ap.add_argument("--bootp", action="store_true")
    a = ap.parse_args()
    fallos = 0

    def check(cond, texto):
        nonlocal fallos
        print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
        if not cond:
            fallos += 1
        return cond

    if a.bootp:
        print("--- ROM_UpdateEMAC: BOOTP y TFTP de e4_eth por el puente Ethernet, y su eco ---")
    else:
        print("--- el eco de lwIP (e4_eth) por el puente Ethernet, con el protocolo de QEMU ---")
    imagen = open("verif/fw/msp432e4/e4_eth/e4_eth.bin", "rb").read() if a.bootp else None
    if not a.prueba:
        if not a.puerto:
            ap.error("falta el PUERTO (o --prueba)")
        conversa(a.puerto, check, imagen)
    else:
        puerto = puerto_libre()
        destino = "qemu:%d" % puerto + (",pcap=%s" % a.pcap if a.pcap else "")
        log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
        fw = "verif/fw/msp432e4/%s.out" % ("e4_bootp/e4_bootp" if a.bootp else "e4_eth/e4_eth")
        args = [a.sim, "placas/msp_exp432e401y.xml", fw, "--eth", destino, "--tiempo-real"]
        p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        try:
            conversa(puerto, check, imagen)
        except OSError as e:
            check(False, "conexion con el simulador: %s" % e)
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
    print("TOTAL eco_e4: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
