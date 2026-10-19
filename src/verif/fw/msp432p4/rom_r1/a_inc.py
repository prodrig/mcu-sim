#!/usr/bin/env python3
# a_inc.py rom_r1.bin > rom_r1.inc
# La imagen de la ROM R1 como palabras de 32 bits para el simulador, que la
# lleva dentro: asi no depende de donde se ejecute ni de una ruta. La genera
# el Makefile de rom_r1; no se edita a mano.
import sys
d = open(sys.argv[1], 'rb').read()
if len(d) != 32768:
    sys.exit('rom_r1.bin tiene %d bytes; la ventana de la ROM es de 32768' % len(d))
h = 2166136261
for b in d:
    h = ((h ^ b) * 16777619) & 0xFFFFFFFF
print('// GENERADO por verif/fw/msp432p4/rom_r1/a_inc.py a partir de rom_r1.bin:')
print('// NO SE EDITA A MANO. %d bytes, FNV-1a 0x%08X (verif/fw/huellas.txt)' % (len(d), h))
print('#define ROM_R1_FNV 0x%08Xu' % h)
pal = [int.from_bytes(d[i:i + 4], 'little') for i in range(0, len(d), 4)]
for i in range(0, len(pal), 8):
    print(', '.join('0x%08Xu' % p for p in pal[i:i + 8]) + ',')
