#!/usr/bin/env python3
# =============================================================================
# verifica_tablas.py — La ROM R1 del E4 contra rom.h, por un camino que no es
#                      el suyo
#
#   python3 verifica_tablas.py CC ROM_H rom_r1.out rom_r1.bin
#
# gen_tablas.py lee rom.h con expresiones regulares. Esto lo comprueba de otra
# manera: le pide al PREPROCESADOR DE C que expanda cada macro ROM_xxx como la
# vera un firmware compilado para el E401Y, saca de la expansion la tabla y la
# casilla, y mira en el ELF ya enlazado a que simbolo apunta. Tiene que ser la
# funcion del mismo nombre sin el ROM_, o rom_hueco si DriverLib no la tiene
# (SMBus, SPI Flash y las Update* que no son UART). Ni una mal, y las cuentas
# tienen que dar las de esta version de rom.h: si cambia, se revisa a mano.
#
# Es la prueba que pide el riesgo 4 de §9 de doc/analisis_msp432.md.
# =============================================================================
import re
import struct
import subprocess
import sys

BASE = 0x01000000
APITABLE = 0x01000010
ESPERADAS = 858          # casillas de funcion en rom.h
HUECOS = 85              # de ellas, sin fuente que se pueda compilar aqui


def main():
    cc, rom_h, elf, imagen = sys.argv[1:5]
    texto = open(rom_h, encoding='latin-1').read()
    nombres = sorted(set(re.findall(r'#define\s+(ROM_\w+)', texto)))
    nombres = [n for n in nombres if not n.endswith('TABLE') and n not in ('ROM_VERSION', 'ROM_pvAESTable')]
    fuente = '#include "%s"\n' % rom_h + ''.join('@%d@ %s\n' % (i, n) for i, n in enumerate(nombres))
    pp = subprocess.run([cc, '-E', '-P', '-D__MSP432E401Y__', '-x', 'c', '-'],
                        input=fuente, capture_output=True, text=True, check=True).stdout
    casillas = {}
    for linea in pp.splitlines():
        m = re.match(r'@(\d+)@ (.*)', linea)
        if not m:
            continue
        nombre, exp = nombres[int(m.group(1))], m.group(2)
        t = re.search(r'0x01000010\)\[(\d+)\]\)\)\[(\d+)\]', exp.replace(' ', ''))
        if not t:
            sys.exit('no entiendo la expansion de %s: %s' % (nombre, exp))
        casillas[nombre] = (int(t.group(1)), int(t.group(2)))

    binario = open(imagen, 'rb').read()
    if len(binario) != 0x40000:
        sys.exit('rom_r1.bin tiene %d bytes; la ventana es de 256 KB' % len(binario))
    nm = subprocess.run([cc.replace('gcc', 'nm'), elf], capture_output=True, text=True,
                        check=True).stdout
    sim = {}
    for l in nm.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'tT':
            sim.setdefault(int(p[0], 16), p[2])

    def palabra(a):
        return struct.unpack_from('<I', binario, a - BASE)[0]

    malas, huecos = [], []
    for nombre, (tabla, casilla) in sorted(casillas.items()):
        t = palabra(APITABLE + 4 * tabla)
        f = palabra(t + 4 * casilla)
        quien = sim.get(f & ~1, '?')
        if not (f & 1):
            malas.append('%s: la casilla tiene 0x%08X, sin el bit Thumb' % (nombre, f))
        elif quien == 'rom_hueco':
            huecos.append(nombre)
        elif quien != nombre[len('ROM_'):]:
            malas.append('%s: tabla %d casilla %d apunta a %s' % (nombre, tabla, casilla, quien))
    for m in malas:
        print('  [rom] MAL ' + m)
    # Los vectores del cargador
    if palabra(BASE) != 0x20004000 or sim.get(palabra(BASE + 4) & ~1) != 'cargador_reset':
        malas.append('vectores')
        print('  [rom] MAL los vectores: la pila o el reset no son los del cargador')
    if malas or len(casillas) != ESPERADAS or len(huecos) != HUECOS:
        sys.exit('  [rom] %d entradas (%d huecos), %d mal (se esperaban %d, %d huecos y ninguna mal)' %
                 (len(casillas), len(huecos), len(malas), ESPERADAS, HUECOS))
    print('  [rom] las %d entradas de rom.h para el E401Y apuntan a su funcion (%d al hueco: '
          'SMBus, SPI Flash, Update* sin fuente)' % (len(casillas), len(huecos)))


if __name__ == '__main__':
    main()
