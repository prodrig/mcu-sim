#!/usr/bin/env python3
# gen_pines.py — Las patillas y las funciones alternativas del STM32F303VC,
# sacadas de la base de pines de ST (STM32_open_pin_data, BSD-3), nunca
# escritas a mano [doc/analisis_stm32f303.md, §2.4 y §8 S0].
#
#   cd src/soc/stm32f3
#   python3 gen_pines.py pindata/"STM32F303V(B-C)Tx.xml" \
#           pindata/GPIO-STM32F303_gpio_v1_0_Modes.xml
#
# Escribe dos ficheros, que se versionan:
#
#   f3_patillas.inc  {posicion, "nombre", tipo} de las cien patillas del
#                    LQFP100, y al final la mascara por puerto de lo que sale
#                    (`F3_MASCARA_LQFP100`), con su recuento;
#   f3_pines.inc     {puerto, pin, AF, "SENAL"} de cada funcion alternativa de
#                    cada pin que sale en el encapsulado.
#
# El guion se niega a escribir si lo que lee no cuadra: si el numero de E/S
# que declara la base (`IONb`) no es el de la mascara, o si una AF no es un
# numero de 0 a 15.
import re
import sys
import xml.etree.ElementTree as ET

NS = {'s': 'http://dummy.com'}
LETRAS = 'ABCDEF'


def pin_de(nombre):
    """'PF0-OSC_IN' -> (5, 0); 'VDD' -> None"""
    m = re.match(r'^P([A-F])(\d{1,2})(?:[-_].*)?$', nombre)
    if not m:
        return None
    return LETRAS.index(m.group(1)), int(m.group(2))


def main():
    if len(sys.argv) != 3:
        sys.exit('uso: gen_pines.py MCU.xml GPIO-Modes.xml')
    mcu = ET.parse(sys.argv[1]).getroot()
    gpio = ET.parse(sys.argv[2]).getroot()

    ionb = int(mcu.find('s:IONb', NS).text)
    patillas = []
    mascara = [0] * len(LETRAS)
    for p in mcu.findall('s:Pin', NS):
        pos, nom, tipo = int(p.get('Position')), p.get('Name'), p.get('Type')
        patillas.append((pos, nom, tipo))
        if tipo == 'I/O':
            q = pin_de(nom)
            if q is None:
                sys.exit('patilla de E/S sin puerto: %s' % nom)
            mascara[q[0]] |= 1 << q[1]
    patillas.sort()
    if [x[0] for x in patillas] != list(range(1, len(patillas) + 1)):
        sys.exit('las posiciones no son 1..%u' % len(patillas))
    n_io = sum(bin(m).count('1') for m in mascara)
    if n_io != ionb:
        sys.exit('la base dice %u E/S y la mascara tiene %u' % (ionb, n_io))

    af = []
    for g in gpio.findall('s:GPIO_Pin', NS):
        q = pin_de(g.get('Name'))
        if q is None or not (mascara[q[0]] >> q[1]) & 1:
            continue                       # no sale en el LQFP100
        for s in g.findall('s:PinSignal', NS):
            for v in s.findall('s:SpecificParameter/s:PossibleValue', NS):
                m = re.match(r'^GPIO_AF(\d+)_', v.text or '')
                if not m or not 0 <= int(m.group(1)) <= 15:
                    sys.exit('AF que no se entiende: %s %s' % (g.get('Name'), v.text))
                af.append((q[0], q[1], int(m.group(1)), s.get('Name')))
    af = sorted(set(af))

    cab = ('// GENERADO por gen_pines.py a partir de la base de pines de ST\n'
           '// (STM32_open_pin_data, BSD-3; pindata/): NO SE EDITA A MANO.\n')
    with open('f3_patillas.inc', 'w', newline='\n') as f:
        f.write(cab)
        f.write('// Las %u patillas del LQFP100 del STM32F303V(B-C)Tx: '
                '{posicion, nombre, tipo}.\n' % len(patillas))
        f.write('// Tipos: I/O, Power, Reset, Boot, MonoIO.\n')
        f.write('#ifndef F3_PATILLA\n#define F3_PATILLA(pos, nombre, tipo)\n#endif\n')
        for pos, nom, tipo in patillas:
            f.write('F3_PATILLA(%3u, "%s", "%s")\n' % (pos, nom, tipo))
        f.write('#undef F3_PATILLA\n')
        f.write('// %u E/S [IONb = %u]: A..F\n' % (n_io, ionb))
        f.write('#define F3_MASCARA_LQFP100 %s\n'
                % ', '.join('0x%04Xu' % m for m in mascara))
        f.write('#define F3_N_GPIO_LQFP100 %uu\n' % n_io)
    with open('f3_pines.inc', 'w', newline='\n') as f:
        f.write(cab)
        f.write('// Las %u funciones alternativas de los pines que salen en el '
                'LQFP100:\n// {puerto (0 = A), pin, AF, senal}.\n' % len(af))
        for p, i, n, s in af:
            f.write('{%u, %2u, %2u, "%s"},  // P%s%u\n' % (p, i, n, s, LETRAS[p], i))
    print('%u patillas, %u E/S, %u funciones alternativas' % (len(patillas), n_io, len(af)))


if __name__ == '__main__':
    main()
