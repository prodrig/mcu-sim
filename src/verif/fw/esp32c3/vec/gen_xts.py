#!/usr/bin/env python3
# gen_xts.py — Vectores del XTS_AES del C3 (E8): una clave de semilla fija,
# cuatro líneas de texto en claro y su texto cifrado en la Flash, calculado
# como espsecure de esptool (`_flash_encryption_operation_aes_xts`: XTS de
# `cryptography` sobre unidades de 128 bytes dadas la vuelta; la segunda
# fuente, nada del modelo). La clave del eFuse es la del fichero al revés,
# como la quema espefuse. Escribe xts_vec.h.
import random, struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def espsecure_cifra(key, flash_address, indata):
    n = len(indata)
    pad_left = flash_address % 0x80
    indata = b"\x00" * pad_left + indata
    pad_right = len(indata) % 0x80
    if pad_right:
        indata += b"\x00" * (0x80 - pad_right)
    out = []
    for i in range(0, len(indata), 0x80):
        tweak = struct.pack("<I", (flash_address - pad_left + i) & ~0x7F) + b"\x00" * 12
        enc = Cipher(algorithms.AES(key), modes.XTS(tweak)).encryptor()
        out.append(enc.update(indata[i:i + 0x80][::-1])[::-1])
    return b"".join(out)[pad_left:pad_left + n]

rnd = random.Random(0xC3E5)
KEY = bytes(rnd.getrandbits(8) for _ in range(32))
LINEAS = ((0x10000, 32), (0x10020, 32), (0x10040, 32), (0x10070, 16))
PLANO = bytes(rnd.getrandbits(8) for _ in range(112))
C = {}
o = 0
for a, n in LINEAS:
    C[a] = espsecure_cifra(KEY, a, PLANO[o:o + n])[:n]
    assert len(C[a]) == n
    o += n

def w(b):
    return ", ".join("0x%08Xu" % int.from_bytes(b[i:i + 4], "little") for i in range(0, len(b), 4))

with open(sys.argv[1] if len(sys.argv) > 1 else "xts_vec.h", "w") as f:
    f.write("/* xts_vec.h — generado por gen_xts.py (semilla 0xC3E5): no editar */\n")
    f.write("static const uint8_t XTS_EFUSE[32] = {%s};\n" % ", ".join("0x%02X" % x for x in KEY[::-1]))
    f.write("static const uint32_t XTS_PLANO[28] = {%s};\n" % w(PLANO))
    f.write("static const uint32_t XTS_CIFRADO[28] = {%s};\n" % w(b"".join(C[a] for a, _ in LINEAS)))
