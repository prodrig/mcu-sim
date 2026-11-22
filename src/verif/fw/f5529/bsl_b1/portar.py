#!/usr/bin/env python3
# portar.py — pasa de IAR a msp430-elf-gcc las fuentes BSD-3 del BSL USB de TI
# (MSPDebugStack 3.15.1.1, ThirdParty/BSL430_Firmware) que hay en ti/. Se
# corrió una vez sobre la copia sin tocar; los cambios quedan versionados (las
# cabeceras de licencia de TI, intactas). Lo que hace:
#   * `#pragma location = X` + `__no_init T n;`  -> `extern T n;` y la dirección
#     en ubicaciones.ld (PROVIDE), para que el BSL de RAM las encuentre donde
#     las deja el de Flash;
#   * `__no_init` -> la sección .noinit; `x[] @ 0xNNNN` -> la sección .ubica_NNNN; `@ "SEG"` -> la sección .SEG;
#   * fuera `#pragma required`, `<intrinsics.h>` y el `__low_level_init` de IAR
#     (lo hace crt_f5529.S); `0920h` -> 0x0920 en el ensamblador en línea.
import re, sys, glob, os
loc = []
for f in glob.glob('ti/*.c') + glob.glob('ti/*.h'):
    s = open(f, encoding='latin-1').read()
    o = s
    def pon(m):
        addr, decl = m.group(1), m.group(2)
        decl = decl.replace('__no_init', '').replace('volatile', 'volatile').strip()
        nombre = re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*;', decl).group(1)
        loc.append((nombre, addr))
        return 'extern %s /* en %s: ubicaciones.ld */' % (decl, addr)
    s = re.sub(r'#pragma\s+location\s*=\s*(0x[0-9A-Fa-f]+)\s*\n\s*(__no_init[^;]*;)', pon, s)
    s = re.sub(r'extern\s+__no_init', 'extern', s)
    s = re.sub(r'__no_init\s+', '__attribute__((section(".noinit"))) ', s)
    s = re.sub(r'(\w+)\s*@\s*"(\w+)"\s*=', r'\1 __attribute__((section(".\2"), used)) =', s)
    s = re.sub(r'#pragma\s+required\s*=\s*\w+\s*\n', '', s)
    s = re.sub(r'\]\s*@\s*0x([0-9A-Fa-f]+)\s*=', r'] __attribute__((section(".ubica_\1"), used)) =', s)
    s = s.replace('#include <intrinsics.h>', '')
    s = re.sub(r'([0-9A-Fa-f]{4})h"', r'0x\1"', s)
    if s != o:
        open(f, 'w', encoding='latin-1').write(s)
with open('ubicaciones.ld', 'w') as f:
    f.write('/* GENERADO por portar.py: las variables que el BSL USB de TI tiene en una dirección fija */\n')
    for n, a in loc:
        f.write('PROVIDE(%s = %s);\n' % (n, a))
print(len(loc), 'ubicaciones')
