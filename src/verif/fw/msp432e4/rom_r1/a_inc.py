#!/usr/bin/env python3
# a_inc.py rom_r1.bin > rom_r1.inc
# La imagen de la ROM R1 del E4 como palabras de 32 bits para el simulador,
# que la lleva dentro: asi no depende de donde se ejecute ni de una ruta. La
# genera el Makefile de rom_r1; no se edita a mano.
#
# La ventana es de 256 KB y la R1 ocupa unos 27: solo van las palabras hasta
# la ultima que no es 0xFFFFFFFF, y el simulador rellena el resto con 0xFF. La
# huella (FNV-1a) es la de los 256 KB enteros, la de rom_r1.bin.
import sys
d = open(sys.argv[1], 'rb').read()
if len(d) != 262144:
    sys.exit('rom_r1.bin tiene %d bytes; la ventana de la ROM es de 262144' % len(d))
h = 2166136261
for b in d:
    h = ((h ^ b) * 16777619) & 0xFFFFFFFF
pal = [int.from_bytes(d[i:i + 4], 'little') for i in range(0, len(d), 4)]
n = len(pal)
while n and pal[n - 1] == 0xFFFFFFFF:
    n -= 1
print('// GENERADO por verif/fw/msp432e4/rom_r1/a_inc.py a partir de rom_r1.bin:')
print('// NO SE EDITA A MANO. %d bytes, FNV-1a 0x%08X (verif/fw/huellas.txt);' % (len(d), h))
print('// aqui las %d primeras palabras, el resto es 0xFFFFFFFF' % n)
print('#define ROM_R1_E4_FNV 0x%08Xu' % h)
print('#define ROM_R1_E4_PALABRAS %du' % n)
for i in range(0, n, 8):
    print(', '.join('0x%08Xu' % p for p in pal[i:min(i + 8, n)]) + ',')
