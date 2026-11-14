#!/usr/bin/env python3
# gen_i2s.py — Vectores de A-law y mu-law del I2S del C3 (E8): ocho muestras
# de 16 bits de semilla fija y lo que dan lin2alaw/alaw2lin y
# lin2ulaw/ulaw2lin de audioop (audioop-lts desde Python 3.13), la segunda
# fuente: nada del modelo. Escribe i2s_vec.h.
import audioop, random, struct, sys

rnd = random.Random(0xC312)
X = [0, 1, -1, 1000, -1000, 32767, -32768] + [rnd.randint(-32768, 32767)]
lin = struct.pack("<8h", *X)
A = list(audioop.lin2alaw(lin, 2))
U = list(audioop.lin2ulaw(lin, 2))
AX = list(struct.unpack("<8h", audioop.alaw2lin(bytes(A), 2)))
UX = list(struct.unpack("<8h", audioop.ulaw2lin(bytes(U), 2)))

def h(v):
    return ", ".join("0x%04X" % (x & 0xFFFF) for x in v)

with open(sys.argv[1] if len(sys.argv) > 1 else "i2s_vec.h", "w") as f:
    f.write("/* i2s_vec.h — generado por gen_i2s.py (semilla 0xC312): no editar */\n")
    f.write("static const uint16_t I2S_X[8] = {%s};\n" % h(X))
    f.write("static const uint16_t I2S_A[8] = {%s};    /* los códigos A-law */\n" % h(A))
    f.write("static const uint16_t I2S_AX[8] = {%s};   /* y expandidos */\n" % h(AX))
    f.write("static const uint16_t I2S_U[8] = {%s};    /* mu-law */\n" % h(U))
    f.write("static const uint16_t I2S_UX[8] = {%s};\n" % h(UX))
