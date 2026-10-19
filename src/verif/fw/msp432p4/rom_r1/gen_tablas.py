#!/usr/bin/env python3
# =============================================================================
# gen_tablas.py — Las tablas de la ROM sustituta, sacadas de rom.h
#
#   python3 gen_tablas.py ROM_H > tablas.c
#
# La ROM de DriverLib del MSP432P401R es una tabla de tablas en 0x0200_0800
# (ROM_APITABLE): la entrada 0 es la version y la n, la tabla del modulo n.
# Un firmware que llama a MAP_xxx() salta a ROM_xxxTABLE[i], y si el orden no
# es exactamente el de rom.h llama a otra funcion SIN AVISAR. Por eso las
# tablas no se escriben a mano: este guion lee rom.h y las genera, entrada a
# entrada [doc/analisis_msp432.md, §9, riesgo 4].
#
# Se toman las entradas que rom.h declara para TARGET_IS_MSP432P4XX (el
# P401R/P401M); las que solo existen en TARGET_IS_MSP432P4XX_NEXT (P4111,
# P411V, P411Y) quedan como hueco, igual que los indices que no usa nadie.
# Un hueco apunta a `rom_hueco`, que se para en un BKPT: si un firmware llega
# ahi, el depurador lo ve en vez de un salto a 0xFFFFFFFF.
#
# Las tablas de los modulos que el P401R no tiene (SYSCTL_A, FLASHCTL_A y
# LCD_F) apuntan a una tabla de huecos del mismo tamaño que en rom.h.
# =============================================================================
import re
import sys


def lee(ruta):
    s = open(ruta, encoding='latin-1').read()
    # Las tablas de modulo: ROM_xxxTABLE = ROM_APITABLE[n]
    tablas = {}
    for m in re.finditer(r'#define\s+(ROM_\w+TABLE)\s+\(\(unsigned long \*\)\(ROM_APITABLE\[(\d+)\]\)\)', s):
        tablas[m.group(1)] = int(m.group(2))
    # Las entradas: #if <condicion> / #define ROM_nombre ... TABLA[i]) / #endif
    entradas = {}
    todas = {}
    for m in re.finditer(r'#if\s+(.*?)\n#define\s+(ROM_\w+)\b(.*?)#endif', s, re.S):
        cond, nombre, cuerpo = m.group(1), m.group(2), m.group(3)
        t = re.search(r'(ROM_\w+TABLE)\[(\d+)\]', cuerpo)
        if not t:
            continue
        tabla, i = t.group(1), int(t.group(2))
        todas.setdefault(tabla, set()).add(i)
        if re.search(r'defined\(TARGET_IS_MSP432P4XX\)', cond):
            clave = (tabla, i)
            if clave in entradas:
                sys.exit('rom.h: %s[%d] dos veces (%s y %s)' % (tabla, i, entradas[clave], nombre))
            entradas[clave] = nombre[len('ROM_'):]
    return tablas, entradas, todas


def main():
    tablas, entradas, todas = lee(sys.argv[1])
    orden = sorted(tablas.items(), key=lambda kv: kv[1])
    if [n for _, n in orden] != list(range(1, len(orden) + 1)):
        sys.exit('rom.h: los indices de ROM_APITABLE no son 1..%d' % len(orden))
    o = []
    o.append('/* GENERADO por gen_tablas.py a partir de rom.h: NO SE EDITA A MANO.')
    o.append(' * %d entradas para TARGET_IS_MSP432P4XX en %d tablas. */' %
             (len(entradas), len(orden)))
    o.append('#include <ti/devices/msp432p4xx/driverlib/driverlib.h>')
    o.append('#include <stdint.h>')
    o.append('')
    o.append('extern void rom_hueco(void);')
    o.append('#define H ((uint32_t)&rom_hueco)')
    o.append('#define F(f) ((uint32_t)&f)')
    o.append('')
    for tabla, n in orden:
        usados = todas.get(tabla, set())
        tam = (max(usados) + 1) if usados else 1
        filas = []
        for i in range(tam):
            f = entradas.get((tabla, i))
            filas.append('    /* %2d */ %s,' % (i, 'F(%s)' % f if f else 'H'))
        o.append('static const uint32_t %s[%d] __attribute__((used)) = {' % (tabla.lower(), tam))
        o.extend(filas)
        o.append('};')
        o.append('')
    o.append('/* ROM_APITABLE, en 0x0200_0800: la version y las %d tablas */' % len(orden))
    o.append('extern const uint32_t rom_version;')
    o.append('const uint32_t rom_apitable[%d] __attribute__((section(".apitable"), used)) = {'
             % (len(orden) + 1))
    o.append('    ROM_R1_VERSION,')
    for tabla, n in orden:
        o.append('    /* %2d */ (uint32_t)%s,' % (n, tabla.lower()))
    o.append('};')
    print('\n'.join(o))


if __name__ == '__main__':
    main()
