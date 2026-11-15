#!/usr/bin/env python3
# =============================================================================
# gen_dec.py — Los vectores del decodificador MSP430, contra binutils
#
# M1 de doc/analisis_msp430.md. Desensambla con msp430-elf-objdump el código
# de los programas versionados de verif/m430/elf (los diferenciales, los de
# ciclos y las pruebas propias) y deja, por cada código de operación distinto
# (con su palabra de extensión, si la tiene), cuántas palabras ocupa la
# instrucción y qué clase es según el mnemónico de binutils. test430 comprueba
# que m430_decodifica() y m430_palabras_op() dicen lo mismo.
#
# Salida (versionada): verif/m430/vectores_dec.inc
# =============================================================================
import glob, os, re, subprocess, sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from gen_dif import herramienta

CLASES = {
    'calla': 'Calla', 'reti': 'Reti', 'pushm': 'PushM', 'popm': 'PopM',
    'rrcm': 'RotM', 'rram': 'RotM', 'rlam': 'RotM', 'rrum': 'RotM',
    'mova': 'Direccion', 'cmpa': 'Direccion', 'adda': 'Direccion', 'suba': 'Direccion',
    'reta': 'Direccion', 'bra': 'Direccion',
}
F2 = {'rrc', 'swpb', 'rra', 'sxt', 'push', 'call', 'rru'}

def clase(mn):
    base = mn.split('.')[0]
    if base in CLASES:
        return CLASES[base]
    if base.startswith('j'):
        return 'Salto'
    b = base[:-1] if base.endswith('x') and base[:-1] in F2 | {'rrc', 'rra', 'swpb', 'sxt', 'push', 'pop', 'rru'} else base
    if b in F2 or b == 'pop' and False:
        return 'F2'
    return 'F1'

def main():
    od = herramienta('msp430-elf-objdump')
    vistos = {}
    for elf in sorted(glob.glob(os.path.join(AQUI, 'elf', '*.elf'))):
        cpux = not os.path.basename(elf).endswith('_cpu.elf') and '_cpu_' not in os.path.basename(elf)
        out = subprocess.run([od, '-d', '-j', '.text', '-j', '.upper.text', elf], capture_output=True,
                             text=True).stdout
        lineas = out.splitlines()
        i = 0
        while i < len(lineas):
            m = re.match(r'\s+([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t(\S+)', lineas[i])
            i += 1
            if not m:
                continue
            bs = m.group(2).split()
            # objdump parte las instrucciones largas en dos líneas
            while i < len(lineas) and re.match(r'\s+[0-9a-f]+:\t((?:[0-9a-f]{2} )+)\s*$', lineas[i]):
                bs += lineas[i].split('\t')[1].split()
                i += 1
            mn = m.group(3)
            if mn.startswith('.') or mn in ('nop',):
                pass
            ws = [int(bs[k], 16) | (int(bs[k + 1], 16) << 8) for k in range(0, len(bs), 2)]
            if not ws:
                continue
            ext = (ws[0] & 0xF800) == 0x1800
            if mn.startswith('rpt'):
                mn = lineas[i - 1].split('{')[-1].split()[0] if '{' in lineas[i - 1] else mn
            c = clase(mn.replace('rpt', ''))
            if not cpux and c in ('Direccion', 'RotM', 'PushM', 'PopM', 'Calla'):
                continue                  # datos en .text que objdump lee como CPUX
            op = ws[1] if ext and len(ws) > 1 else ws[0]
            if ext and not ((op >> 12) >= 4 or (op & 0xFC00) == 0x1000):
                continue                  # una extensión delante de algo que no la admite: datos
            # Una por forma: los registros de trabajo (R4-R15) cuentan como uno, y
            # los saltos, por condición y signo
            def forma(w):
                if (w >> 12) >= 4:
                    s_, d_ = (w >> 8) & 15, w & 15
                    return (w & 0xF0F0) | (min(s_, 4) << 8) | min(d_, 4)
                if (w >> 13) == 1:
                    return w & 0xFE00
                return w
            clave = (forma(ws[0]), forma(op) if ext else 0, cpux)
            if clave in vistos:
                continue
            vistos[clave] = (ws[0], op if ext else 0, len(ws), c, mn)
    filas = []
    for (_, _, cpux), (w0, w1, n, c, mn) in sorted(vistos.items()):
        filas.append(f'    {{0x{w0:04X}, 0x{w1:04X}, {int(cpux)}, {n}, M430Clase::{c}}},   // {mn}')
    open(os.path.join(AQUI, 'vectores_dec.inc'), 'w').write(
        '// GENERADO por verif/m430/gen_dec.py desde msp430-elf-objdump (binutils 2.34):\n'
        '// {palabra (o extensión), código (si hay extensión), CPUX, palabras, clase}\n' + '\n'.join(filas) + '\n')
    print(f'gen_dec: {len(filas)} vectores')

if __name__ == '__main__':
    main()
