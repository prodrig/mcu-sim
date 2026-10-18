#!/usr/bin/env python3
# =============================================================================
# gen_tablas.py — Las tablas de la ROM sustituta del MSP432E4, sacadas de rom.h
#
#   python3 gen_tablas.py ROM_H SIMBOLOS > tablas.c
#
# La ROM del E4 es, como la del P4, una tabla de tablas: ROM_APITABLE en
# 0x0100_0010, con la version en la casilla 0 y en la n la tabla del modulo n
# [rom.h]. Un firmware que llama a ROM_xxx() o MAP_xxx() salta por
# ROM_xxxTABLE[i], y si el orden no es exactamente el de rom.h llama a otra
# funcion SIN AVISAR: por eso las tablas no se escriben a mano.
#
# A diferencia del rom.h del P4, el del E4 no lleva condiciones: todas sus
# casillas son de todos los E4. Pero no todas tienen fuente en DriverLib: las
# de SMBus, SPI Flash y las Update* de I2C, SSI, USB y EMAC son de TivaWare
# (utils/, boot_loader/), cuya licencia no se puede combinar con la AGPLv3.
# Esas casillas quedan como HUECO, que se para en un BKPT. SIMBOLOS es la
# lista de funciones que SI se han compilado (la saca el Makefile con nm), mas
# UpdateUART, que es del cargador propio.
#
# ROM_pvAESTable no es una funcion: es la DIRECCION de la casilla 7 de la
# tabla de software, donde la ROM de TI guarda sus tablas de AES. La R1 no las
# lleva (P-17 j).
# =============================================================================
import re
import sys


def lee(ruta):
    s = open(ruta, encoding='latin-1').read()
    tablas = {}
    for m in re.finditer(r'#define\s+(ROM_\w+TABLE)\s+\(\(uint32_t \*\)\(ROM_APITABLE\[(\d+)\]\)\)', s):
        tablas[m.group(1)] = int(m.group(2))
    entradas = {}
    for m in re.finditer(r'#define\s+ROM_(\w+)\s*\\\s*\n(.*?)(?=\n#define|\n#endif|\Z)', s, re.S):
        nombre, cuerpo = m.group(1), m.group(2)
        t = re.search(r'(ROM_\w+TABLE)\[(\d+)\]', cuerpo)
        if not t or nombre.endswith('TABLE'):
            continue
        clave = (t.group(1), int(t.group(2)))
        if clave in entradas:
            sys.exit('rom.h: %s[%d] dos veces (%s y %s)' % (clave[0], clave[1], entradas[clave], nombre))
        entradas[clave] = nombre
    return tablas, entradas


def main():
    tablas, entradas = lee(sys.argv[1])
    hay = set(l.strip() for l in open(sys.argv[2]) if l.strip())
    hay.add('UpdateUART')
    hay.add('UpdateEMAC')                 # el cargador por Ethernet propio (T7)
    orden = sorted(tablas.items(), key=lambda kv: kv[1])
    tam = {}
    for (tabla, i) in entradas:
        tam[tabla] = max(tam.get(tabla, 0), i + 1)
    o = ['/* GENERADO por gen_tablas.py a partir de rom.h: NO SE EDITA A MANO. */',
         '#include <stdint.h>', '',
         'extern void rom_hueco(void);',
         '#define H ((uint32_t)&rom_hueco)',
         '#define F(f) ((uint32_t)&f)', '']
    usadas, huecos = 0, []
    protos = set()
    filas_por_tabla = {}
    for tabla, n in orden:
        filas = []
        for i in range(tam.get(tabla, 1)):
            f = entradas.get((tabla, i))
            if f and f in hay and not f.startswith('pv'):
                filas.append('    /* %2d */ F(%s),' % (i, f))
                protos.add(f)
                usadas += 1
            else:
                filas.append('    /* %2d */ H,%s' % (i, ('   /* %s */' % f) if f else ''))
                if f:
                    huecos.append(f)
        filas_por_tabla[tabla] = filas
    for p in sorted(protos):
        o.append('extern void %s(void);' % p)
    o.append('')
    for tabla, n in orden:
        o.append('static const uint32_t %s[%d] __attribute__((used)) = {' %
                 (tabla.lower(), len(filas_por_tabla[tabla])))
        o.extend(filas_por_tabla[tabla])
        o.append('};')
        o.append('')
    maximo = max(n for _, n in orden)
    o.append('/* ROM_APITABLE, en 0x0100_0010: la version y las tablas (las que rom.h no')
    o.append(' * nombra quedan a 0) */')
    o.append('const uint32_t rom_apitable[%d] __attribute__((section(".apitable"), used)) = {' % (maximo + 1))
    o.append('    ROM_R1_VERSION,')
    por_indice = {n: t for t, n in orden}
    for k in range(1, maximo + 1):
        t = por_indice.get(k)
        o.append('    /* %2d */ %s,' % (k, '(uint32_t)%s' % t.lower() if t else '0'))
    o.append('};')
    o.append('')
    o.append('/* %d casillas con su funcion y %d huecos: %s */' %
             (usadas, len(huecos), ' '.join(sorted(huecos)) if len(huecos) < 400 else '...'))
    print('\n'.join(o))
    sys.stderr.write('  [rom] %d casillas con su funcion, %d huecos\n' % (usadas, len(huecos)))


if __name__ == '__main__':
    main()
