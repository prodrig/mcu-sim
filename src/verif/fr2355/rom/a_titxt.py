#!/usr/bin/env python3
# a_titxt.py — un ELF a TI-TXT (por objcopy -O ihex), con direcciones de 20 bits
#   python3 a_titxt.py <elf> <salida.txt> <objcopy>
import subprocess, sys, tempfile, os
elf, sal, oc = sys.argv[1:4]
fd, hx = tempfile.mkstemp(suffix='.hex'); os.close(fd)
subprocess.check_call([oc, '-O', 'ihex', '-j', '.rom.raiz', '-j', '.rom.tablas', '-j', '.text', '-j', '.rodata', elf, hx])
mem, base = {}, 0
for l in open(hx):
    l = l.strip()
    if not l.startswith(':'): continue
    n, a, t = int(l[1:3], 16), int(l[3:7], 16), int(l[7:9], 16)
    d = bytes.fromhex(l[9:9 + 2 * n])
    if t == 0:
        for i, b in enumerate(d): mem[base + a + i] = b
    elif t == 2: base = int.from_bytes(d, 'big') << 4
    elif t == 4: base = int.from_bytes(d, 'big') << 16
os.unlink(hx)
out, ant = [], None
for a in sorted(mem):
    if ant is None or a != ant + 1:
        out.append('@%x' % a); col = 0
    out.append('%02X' % mem[a]); ant = a
# 16 por línea
lineas, fila = [], []
for x in out:
    if x.startswith('@'):
        if fila: lineas.append(' '.join(fila)); fila = []
        lineas.append(x)
    else:
        fila.append(x)
        if len(fila) == 16: lineas.append(' '.join(fila)); fila = []
if fila: lineas.append(' '.join(fila))
lineas.append('q')
open(sal, 'w').write('\n'.join(lineas) + '\n')
print('%s: %d bytes, de 0x%X a 0x%X' % (sal, len(mem), min(mem), max(mem)))
