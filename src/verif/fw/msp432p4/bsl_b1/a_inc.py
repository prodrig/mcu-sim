#!/usr/bin/env python3
# a_inc.py bsl_b1.bin > bsl_b1.inc
# La imagen del BSL B1 del P4 como palabras de 32 bits para el simulador,
# que la lleva dentro: asi no depende de donde se ejecute ni de una ruta. La
# genera el Makefile de bsl_b1; no se edita a mano.
#
# El sector es de 8 KB: solo van las palabras hasta
# la ultima que no es 0xFFFFFFFF, y el simulador rellena el resto con 0xFF. La
# huella (FNV-1a) es la de los 8 KB enteros, la de bsl_b1.bin.
import sys
d = open(sys.argv[1], 'rb').read()
if len(d) != 8192:
    sys.exit('bsl_b1.bin tiene %d bytes; el sector del BSL es de 8192' % len(d))
h = 2166136261
for b in d:
    h = ((h ^ b) * 16777619) & 0xFFFFFFFF
pal = [int.from_bytes(d[i:i + 4], 'little') for i in range(0, len(d), 4)]
n = len(pal)
while n and pal[n - 1] == 0xFFFFFFFF:
    n -= 1
print('// GENERADO por verif/fw/msp432p4/bsl_b1/a_inc.py a partir de bsl_b1.bin:')
print('// NO SE EDITA A MANO. %d bytes, FNV-1a 0x%08X (verif/fw/huellas.txt);' % (len(d), h))
print('// aqui las %d primeras palabras, el resto es 0xFFFFFFFF' % n)
print('#define BSL_B1_FNV 0x%08Xu' % h)
print('#define BSL_B1_PALABRAS %du' % n)
for i in range(0, n, 8):
    print(', '.join('0x%08Xu' % p for p in pal[i:min(i + 8, n)]) + ',')
