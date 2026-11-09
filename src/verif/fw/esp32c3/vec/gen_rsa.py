#!/usr/bin/env python3
# gen_rsa.py — Vectores del RSA del C3 (E8): una clave RSA-1024 de semilla
# fija y los resultados esperados, calculados con los enteros de Python (la
# segunda fuente: nada del modelo). Escribe rsa_vec.h.
import random, sys

def primo(rnd, bits):
    while True:
        p = rnd.getrandbits(bits) | (1 << (bits - 1)) | 1
        if all(pow(a, p - 1, p) == 1 for a in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37)):
            return p

rnd = random.Random(0xC3E8)
e = 65537
while True:
    p, q = primo(rnd, 512), primo(rnd, 512)
    if p != q and (p - 1) % e and (q - 1) % e:
        break
M = p * q
d = pow(e, -1, (p - 1) * (q - 1))
n = 32                                   # palabras
R = 1 << (32 * n)
r = R * R % M
mp = (-pow(M, -1, 1 << 32)) % (1 << 32)
X = rnd.getrandbits(1020)
S = pow(X, d, M)                         # firma
V = pow(S, e, M)                         # comprobación: = X
A, B = rnd.getrandbits(1023) % M, rnd.getrandbits(1023) % M
MM = A * B % M
P1, P2 = rnd.getrandbits(512), rnd.getrandbits(512)
PR = P1 * P2
assert V == X

def pal(v, k):
    return ", ".join("0x%08Xu" % ((v >> (32 * i)) & 0xFFFFFFFF) for i in range(k))

with open(sys.argv[1] if len(sys.argv) > 1 else "rsa_vec.h", "w") as f:
    f.write("/* rsa_vec.h — generado por gen_rsa.py (semilla 0xC3E8): no editar */\n")
    f.write("#define RSA_N %d\n#define RSA_MP 0x%08Xu\n" % (n, mp))
    for nom, v, k in (("RSA_M", M, n), ("RSA_R", r, n), ("RSA_D", d, n), ("RSA_E", e, n),
                      ("RSA_X", X, n), ("RSA_S", S, n), ("RSA_A", A, n), ("RSA_B", B, n),
                      ("RSA_AB", MM, n), ("RSA_P1", P1, 16), ("RSA_P2", P2, 16), ("RSA_PR", PR, 32)):
        f.write("static const uint32_t %s[%d] = {%s};\n" % (nom, k, pal(v, k)))
