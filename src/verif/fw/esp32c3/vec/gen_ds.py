#!/usr/bin/env python3
# gen_ds.py — Vectores del DS del C3 (E8): una clave RSA-1024 de semilla
# fija, la clave del HMAC del eFuse y el texto cifrado C, construido como
# gen_digital_signature_tests.py de ESP-IDF (AES-256-CBC de `cryptography`:
# la segunda fuente, nada del modelo). Además, dos C estropeados: uno con un
# byte cambiado (falla el MD) y otro con el relleno β mal (falla el relleno,
# el MD está bien). Escribe ds_vec.h.
import hashlib, hmac, random, struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def primo(rnd, bits):
    while True:
        p = rnd.getrandbits(bits) | (1 << (bits - 1)) | 1
        if all(pow(a, p - 1, p) == 1 for a in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)):
            return p

rnd = random.Random(0xC3D5)
e, BITS, MAX = 65537, 1024, 3072
while True:
    p, q = primo(rnd, BITS // 2), primo(rnd, BITS // 2)
    if p != q and (p - 1) % e and (q - 1) % e:
        break
M = p * q
Y = pow(e, -1, (p - 1) * (q - 1))
rinv = (1 << (2 * BITS)) % M
mprime = (-pow(M, -1, 1 << 32)) & 0xFFFFFFFF
L = BITS // 32 - 1
HK = bytes(rnd.getrandbits(8) for _ in range(32))        # la clave del eFuse (KEY3, propósito 7)
IV = bytes(rnd.getrandbits(8) for _ in range(16))
X = rnd.getrandbits(BITS - 4)
Z = pow(X, Y, M)
dskey = hmac.new(HK, b"\xff" * 32, hashlib.sha256).digest()

def le(v, bits):
    return v.to_bytes(bits // 8, "little")

def cifra(beta=b"\x08" * 8):
    md = hashlib.sha256(le(Y, MAX) + le(M, MAX) + le(rinv, MAX) + struct.pack("<II", mprime, L) + IV).digest()
    P = le(Y, MAX) + le(M, MAX) + le(rinv, MAX) + md + struct.pack("<II", mprime, L) + beta
    assert len(P) == 1200
    enc = Cipher(algorithms.AES(dskey), modes.CBC(IV)).encryptor()
    return enc.update(P) + enc.finalize()

C = cifra()
CMAL = bytearray(C); CMAL[500] ^= 0x01                     # en M: el MD no casa
CPAD = cifra(b"\x08" * 7 + b"\x07")                        # el relleno, mal

def w(b):
    return ", ".join("0x%08Xu" % int.from_bytes(b[i:i + 4], "little") for i in range(0, len(b), 4))

with open(sys.argv[1] if len(sys.argv) > 1 else "ds_vec.h", "w") as f:
    f.write("/* ds_vec.h — generado por gen_ds.py (semilla 0xC3D5): no editar */\n")
    f.write("#define DS_N %d\n" % (L + 1))
    f.write("static const uint8_t DS_HK[32] = {%s};\n" % ", ".join("0x%02X" % x for x in HK))
    f.write("static const uint32_t DS_IV[4] = {%s};\n" % w(IV))
    f.write("static const uint32_t DS_X[%d] = {%s};\n" % (L + 1, w(le(X, BITS))))
    f.write("static const uint32_t DS_Z[%d] = {%s};\n" % (L + 1, w(le(Z, BITS))))
    for nom, c in (("DS_C", C), ("DS_CMAL", bytes(CMAL)), ("DS_CPAD", CPAD)):
        f.write("static const uint32_t %s[300] = {%s};\n" % (nom, w(c)))
