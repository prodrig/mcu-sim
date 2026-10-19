#!/usr/bin/env python3
# =============================================================================
# verifica_tablas.py — La ROM R1 contra rom.h, por un camino que no es el suyo
#
#   python3 verifica_tablas.py CC ROM_H rom_r1.out rom_r1.bin
#
# gen_tablas.py lee rom.h con expresiones regulares. Esto lo comprueba de otra
# manera: le pide al PREPROCESADOR DE C que expanda cada macro ROM_xxx tal y
# como la vera un firmware compilado para el P401R (-D__MSP432P401R__, que es
# lo que activa TARGET_IS_MSP432P4XX en rom.h), saca de la expansion la tabla
# y la casilla, y mira en el ELF ya enlazado a que simbolo apunta esa casilla.
# Tiene que ser la funcion del mismo nombre, sin el ROM_. Una sola que no lo
# sea y el Makefile falla.
#
# Es la prueba que pide el riesgo 4 de §9 de doc/analisis_msp432.md: «un
# desplazamiento mal en una tabla llama a otra funcion sin avisar».
# =============================================================================
import re
import struct
import subprocess
import sys


def main():
    cc, rom_h, elf, imagen = sys.argv[1:5]
    texto = open(rom_h, encoding='latin-1').read()
    nombres = sorted(set(re.findall(r'#define\s+(ROM_\w+)', texto)))
    nombres = [n for n in nombres if not n.endswith('TABLE') and n != 'ROM_VERSION']
    # Una linea por macro; el preprocesador deja sin tocar las que no existen
    # para el P401R (las de TARGET_IS_MSP432P4XX_NEXT).
    fuente = '#include "%s"\n' % rom_h + ''.join('@%d@ %s\n' % (i, n) for i, n in enumerate(nombres))
    pp = subprocess.run([cc, '-E', '-P', '-D__MSP432P401R__', '-x', 'c', '-'],
                        input=fuente, capture_output=True, text=True, check=True).stdout
    casillas = {}
    for linea in pp.splitlines():
        m = re.match(r'@(\d+)@ (.*)', linea)
        if not m:
            continue
        nombre, exp = nombres[int(m.group(1))], m.group(2)
        if exp.strip() == nombre:
            continue                       # no existe para el P401R
        t = re.search(r'0x02000800\)\[(\d+)\]\)\)\[(\d+)\]', exp.replace(' ', ''))
        if not t:
            sys.exit('no entiendo la expansion de %s: %s' % (nombre, exp))
        casillas[nombre] = (int(t.group(1)), int(t.group(2)))

    # La imagen de la ventana y los simbolos del ELF
    binario = open(imagen, 'rb').read()
    nm = subprocess.run([cc.replace('gcc', 'nm'), elf], capture_output=True, text=True,
                        check=True).stdout
    sim = {}
    for l in nm.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'tT':
            sim.setdefault(int(p[0], 16), p[2])
    base = 0x02000000

    def palabra(a):
        return struct.unpack_from('<I', binario, a - base)[0]

    malas = []
    for nombre, (tabla, casilla) in sorted(casillas.items()):
        t = palabra(0x02000800 + 4 * tabla)
        f = palabra(t + 4 * casilla)
        quien = sim.get(f & ~1, '?')
        if not (f & 1):
            malas.append('%s: la casilla tiene 0x%08X, sin el bit Thumb' % (nombre, f))
        elif quien != nombre[len('ROM_'):]:
            malas.append('%s: tabla %d casilla %d apunta a %s' % (nombre, tabla, casilla, quien))
    for m in malas:
        print('  [rom] MAL ' + m)
    if malas or len(casillas) != 424:
        sys.exit('  [rom] %d entradas, %d mal (se esperaban 424 y ninguna mal)' %
                 (len(casillas), len(malas)))
    print('  [rom] las %d entradas de rom.h para el P401R apuntan a su funcion' % len(casillas))


if __name__ == '__main__':
    main()
