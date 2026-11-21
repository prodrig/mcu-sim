#!/usr/bin/env python3
# msp430_bsl_inc.py IMAGEN.bin PREFIJO > imagen.inc
# Un BSL B1 de MSP430 (G2553, F5529, FR5xx) como bytes para el simulador, que
# lo lleva dentro. Lo generan los Makefile de cada BSL; no se edita a mano.
# Solo van los bytes hasta el último que no es 0xFF; el simulador rellena el
# resto. La huella (FNV-1a) es la de la imagen entera (verif/fw/huellas.txt).
import sys
d = open(sys.argv[1], 'rb').read()
p = sys.argv[2]
h = 2166136261
for b in d:
    h = ((h ^ b) * 16777619) & 0xFFFFFFFF
n = len(d)
while n and d[n - 1] == 0xFF:
    n -= 1
print('// GENERADO por verif/fw/msp430_bsl_inc.py a partir de %s: NO SE EDITA A MANO.' % sys.argv[1].split('/')[-1])
print('// %d bytes, FNV-1a 0x%08X (verif/fw/huellas.txt); aquí los %d primeros, el resto es 0xFF' % (len(d), h, n))
print('#define %s_FNV 0x%08Xu' % (p, h))
print('#define %s_TAM %du' % (p, len(d)))
print('#define %s_BYTES %du' % (p, n))
for i in range(0, n, 16):
    print(', '.join('0x%02X' % b for b in d[i:min(i + 16, n)]) + ',')
