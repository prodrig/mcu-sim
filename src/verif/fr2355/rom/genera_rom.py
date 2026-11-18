#!/usr/bin/env python3
# genera_rom.py — La ROM de bibliotecas del MSP430FR2355 (M7 de
# doc/analisis_msp430.md), hecha aquí: la DriverLib de TI (BSD, la del paquete
# de la MSP-EXP430FR2355) compilada en 0xFAC00, con las tablas en el orden que
# documenta TI en rom_driverlib_fr235x.h (la única interfaz pública de su ROM).
#
#   python3 genera_rom.py <rom_driverlib_fr235x.h> <salida.c> <salida_gcc.h>
#
# Escribe:
#   rom_tablas.c   la tabla raíz en 0xFAC00 ([0] versión, [1] 0: la FFT no
#                  está, [2] y [3] la versión de la DriverLib y luego una
#                  tabla por módulo, en el índice de TI; los huecos, 0) y las
#                  tablas de cada módulo, con un puntero de 32 bits por
#                  función
#   rom_gcc.h      la cabecera de TI para msp430-elf-gcc -mlarge: las mismas
#                  macros ROM_x sin el atributo __cc_rom ni la guarda de los
#                  compiladores de TI e IAR
import re, sys

src, out_c, out_h = sys.argv[1:4]
s = open(src).read()
defs = re.findall(r'#define (ROM_(\w+))\s*\\\s*\n\s*\(\(((?:(?!#define).)*?)\)\s*(ROM_\w+TABLE)\[(\d+)\]\)', s, re.S)
mods = dict(re.findall(r'#define (ROM_\w+TABLE)\s+\(\(uint32_t \*\)\(ROM_DRIVERLIB_APITABLE\[(\d+)\]\)\)', s))
tablas = {}
for nom, fun, sig, tab, i in defs:
    tablas.setdefault(tab, {})[int(i)] = fun
c = ['/* rom_tablas.c: generado por genera_rom.py; no se edita a mano */',
     '#include <stdint.h>', '#include "driverlib.h"', '']
for tab, fs in sorted(tablas.items()):
    for f in sorted(set(fs.values())):
        pass
    n = max(fs) + 1
    ent = ', '.join('(void (*)(void))%s' % fs[k] if k in fs else '0' for k in range(n))
    c.append('__attribute__((section(".rom.tablas"), used)) void (* const %s[%d])(void) = {%s};' % (tab.lower(), n, ent))
napi = max(int(v) for v in mods.values()) + 1
api = ['0'] * napi
api[0] = '(void (*)(void))(uintptr_t)((DRIVERLIB_VER_PATCH << 8) | DRIVERLIB_VER_BUILD)'
api[1] = '(void (*)(void))(uintptr_t)((DRIVERLIB_VER_MAJOR << 8) | DRIVERLIB_VER_MINOR)'
for tab, k in mods.items():
    if tab in tablas:
        api[int(k)] = '(void (*)(void))%s' % tab.lower()
c.append('')
c.append('/* La raíz, en 0xFAC00: la versión, la FFT (0: no está) y la tabla de la DriverLib */')
raiz = ['(void (*)(void))(uintptr_t)getVersion()', '0'] + api
c.append('__attribute__((section(".rom.raiz"), used)) void (* const rom_raiz[%d])(void) = {\n    %s};' % (len(raiz), ',\n    '.join(raiz)))
open(out_c, 'w').write('\n'.join(c) + '\n')
# La cabecera para gcc
i = s.index('#if (defined(__TI_COMPILER_VERSION__) && defined(__LARGE_CODE_MODEL__))')
j = s.index('\n', s.index('(__CODE_MODEL__==__CODE_MODEL_LARGE__))', i))
k = s.index('#else\n#error "Small code model')
cuerpo = s[j + 1:k]
h = ['/* rom_gcc.h: las macros ROM_x de rom_driverlib_fr235x.h (TI, BSD) para',
     ' * msp430-elf-gcc -mlarge; generado por genera_rom.py */',
     '#ifndef ROM_GCC_H', '#define ROM_GCC_H', '#include <stdint.h>', '#include <stdbool.h>',
     '#define __cc_rom', '#ifndef ROMLIB_START', '#define ROMLIB_START 0xFAC00UL', '#endif',
     cuerpo, '#endif']
open(out_h, 'w').write('\n'.join(h) + '\n')
print('%d funciones en %d tablas; la DriverLib con %d entradas' % (len(defs), len(tablas), napi))
