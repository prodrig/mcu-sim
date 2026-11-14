#!/usr/bin/env python3
# gen_hmac.py — Vectores del HMAC del C3 (E8): tres claves de eFuse de
# semilla fija, mensajes de varias longitudes y los resultados que da el
# módulo hmac de Python (la segunda fuente: nada del modelo). Escribe
# hmac_vec.h.
import hmac, hashlib, random, sys

rnd = random.Random(0xC3A5)
K = [bytes(rnd.getrandbits(8) for _ in range(32)) for _ in range(3)]   # KEY0 (subida), KEY1 (JTAG), KEY2 (DS)
LONG = (32, 64, 100, 200)
MSG = bytes(rnd.getrandbits(8) for _ in range(max(LONG)))
H = [hmac.new(K[0], MSG[:n], hashlib.sha256).digest() for n in LONG]
JT = hmac.new(K[1], b"\x00" * 32, hashlib.sha256).digest()
DS = hmac.new(K[2], b"\xff" * 32, hashlib.sha256).digest()
# Lo que se escribe en WR_JTAG: de la última palabra a la primera, cada una
# con sus bytes al revés (big-endian), como ets_jtag_enable_temporarily
JW = [int.from_bytes(JT[28 - 4 * k:32 - 4 * k], "big") for k in range(8)]

def b(v):
    return ", ".join("0x%02X" % x for x in v)

with open(sys.argv[1] if len(sys.argv) > 1 else "hmac_vec.h", "w") as f:
    f.write("/* hmac_vec.h — generado por gen_hmac.py (semilla 0xC3A5): no editar */\n")
    for i, k in enumerate(K):
        f.write("static const uint8_t HMAC_K%d[32] = {%s};\n" % (i, b(k)))
    f.write("static const uint8_t HMAC_MSG[%d] = {%s};\n" % (len(MSG), b(MSG)))
    for n, h in zip(LONG, H):
        f.write("static const uint8_t HMAC_H%d[32] = {%s};\n" % (n, b(h)))
    f.write("static const uint8_t HMAC_JT[32] = {%s};\n" % b(JT))
    f.write("static const uint8_t HMAC_DS[32] = {%s};\n" % b(DS))
    f.write("static const uint32_t HMAC_JW[8] = {%s};\n" % ", ".join("0x%08Xu" % w for w in JW))
