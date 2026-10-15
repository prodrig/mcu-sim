#!/usr/bin/env python3
# =============================================================================
# contrasta_ti.py — Las tablas de la R1 contra la ROM DE TI, casilla a casilla
#
#   python3 contrasta_ti.py ROM_H rom_r1.out ROM_TI.out
#
# verifica_tablas.py comprueba que cada casilla de la R1 apunta a la funcion
# que dice rom.h. Esto comprueba que rom.h dice lo mismo que el SILICIO: lee
# ROM_APITABLE de la imagen de la ROM de TI, sigue cada casilla que rom.h
# declara para el P401R y mira, por los simbolos de esa imagen, a que funcion
# llega. Tiene que ser la del mismo nombre.
#
# La imagen de TI es la que instala Code Composer Studio para depurar dentro
# de la ROM, con simbolos:
#
#   ccs_base/emulation/gel/images/msp432p401r/revC/msp432_driverlib_rom_image.out
#
# Es de TI y NO se versiona (doc/fuentes.md, §0). Sin ella, este guion no
# hace falta para nada: es una segunda opinion, no un paso del Makefile.
#
# Ademas cuenta los HUECOS de la R1 en los que la ROM de TI si tiene una
# funcion: son casillas que rom.h no declara para el P401R a proposito (en
# esas, MAP_ llama a la copia de la Flash), y la R1 las deja en rom_hueco.
#
# El 2026-10-15, con la imagen rev. C: las 424 casillas coinciden, y hay 12
# huecos con funcion en la de TI (doc/analisis_msp432.md, T4).
# =============================================================================
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tablas  # noqa: E402

CROSS = os.environ.get('CROSS', 'arm-none-eabi-')
APITABLE = 0x02000800


def imagen(elf):
    """La memoria cargable del ELF, desde su primer segmento, y esa base."""
    cab = subprocess.run([CROSS + 'readelf', '-lW', elf], capture_output=True, text=True,
                         check=True).stdout
    base = min(int(l.split()[2], 16) for l in cab.splitlines() if l.strip().startswith('LOAD'))
    fd, ruta = tempfile.mkstemp()
    os.close(fd)
    try:
        subprocess.run([CROSS + 'objcopy', '-O', 'binary', '--gap-fill', '0', elf, ruta],
                       check=True)
        return open(ruta, 'rb').read(), base
    finally:
        os.unlink(ruta)


def simbolos(elf):
    """Direccion (sin el bit Thumb) -> nombres de funcion."""
    s = {}
    salida = subprocess.run([CROSS + 'nm', elf], capture_output=True, text=True,
                            check=True).stdout
    for linea in salida.splitlines():
        p = linea.split()
        if len(p) == 3 and p[1] in 'Tt' and not p[2].startswith('.text'):
            s.setdefault(int(p[0], 16) & ~1, []).append(p[2])
    return s


def main():
    if len(sys.argv) != 4:
        sys.exit('uso: contrasta_ti.py ROM_H rom_r1.out ROM_TI.out')
    rom_h, _r1, ti = sys.argv[1:4]
    tablas, entradas, todas = gen_tablas.lee(rom_h)
    datos, base = imagen(ti)
    sim = simbolos(ti)

    def w(a):
        return struct.unpack_from('<I', datos, a - base)[0]

    bien = mal = 0
    for (tabla, i), nombre in sorted(entradas.items()):
        f = w(w(APITABLE + 4 * tablas[tabla]) + 4 * i) & ~1
        if nombre in sim.get(f, []):
            bien += 1
        else:
            mal += 1
            print('  MAL %s[%d]: rom.h dice %s, la ROM de TI llega a %s'
                  % (tabla, i, nombre, sim.get(f)))
    # Un hueco cuenta si la funcion de la ROM de TI es del MISMO modulo que la
    # tabla (su prefijo, el de las casillas que rom.h si declara): una
    # casilla mas alla del final de la tabla de TI cae en la tabla siguiente
    prefijos = {}
    for (tabla, _i), nombre in entradas.items():
        prefijos.setdefault(tabla, set()).add(nombre.split('_')[0])
    huecos = []
    for tabla, k in sorted(tablas.items(), key=lambda kv: kv[1]):
        for i in range(max(todas.get(tabla, {0})) + 1):
            if (tabla, i) in entradas:
                continue
            try:
                f = w(w(APITABLE + 4 * k) + 4 * i) & ~1
            except struct.error:
                continue
            nombres = [n for n in sim.get(f, []) if n.split('_')[0] in prefijos.get(tabla, ())]
            if nombres:
                huecos.append('%s[%d] = %s' % (tabla, i, nombres[0]))
    print('  [ti] %d casillas de rom.h coinciden con la ROM de TI, %d no' % (bien, mal))
    print('  [ti] %d huecos de la R1 con funcion en la de TI (MAP_ usa ahi la de la Flash):'
          % len(huecos))
    for h in huecos:
        print('         ' + h)
    sys.exit(1 if mal else 0)


if __name__ == '__main__':
    main()
