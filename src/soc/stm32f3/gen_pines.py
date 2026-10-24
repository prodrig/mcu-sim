#!/usr/bin/env python3
# gen_pines.py — Las patillas y las funciones alternativas de la familia del
# STM32F303xB/C (el die 0x422: F302xB/C, F303xB/C y F358xC), sacadas de la
# base de pines de ST (STM32_open_pin_data, BSD-3), nunca escritas a mano
# [doc/analisis_stm32f303.md, §2.4 y §8 S0 y S9].
#
#   cd src/soc/stm32f3
#   python3 gen_pines.py
#
# Lee pindata/ y escribe, para que se versionen:
#
#   f3_patillas_<enc>.inc  {posicion, "nombre", tipo} de cada patilla de un
#                          encapsulado y su mascara por puerto de lo que sale
#                          (`F3_MASCARA_<ENC>`), con su recuento. Uno por
#                          encapsulado del F303 (LQFP100, LQFP64, LQFP48) y
#                          otro del F358 (el mismo, con NPOR donde el F303
#                          tiene PB2);
#   f3_pines.inc           {puerto, pin, AF, "SENAL"} de cada funcion
#                          alternativa de cada pin del LQFP100, que contiene a
#                          los demas: el GPIO es del die, no del encapsulado;
#   f3_lineas.inc          lo que cada linea (F302, F358) no tiene del F303,
#                          segun las IP que la base de pines da a su LQFP100;
#   f3_analogico.inc       {adc, canal, puerto, pin} de cada entrada ADCx_INy
#                          que sale a un pin del LQFP100 (S5).
#
# El guion se niega a escribir si lo que lee no cuadra: si el numero de E/S
# que declara la base (`IONb`) no es el de la mascara, si una AF no es un
# numero de 0 a 15, si un encapsulado no cabe en el LQFP100, o si el F302 no
# tiene las mismas patillas que el F303 en el mismo encapsulado.
import re
import sys
import xml.etree.ElementTree as ET

NS = {'s': 'http://dummy.com'}
LETRAS = 'ABCDEF'
GPIO = 'pindata/GPIO-STM32F303_gpio_v1_0_Modes.xml'
# id -> fichero; el primero es la referencia (el LQFP100 del F303)
ENCAPSULADOS = [
    ('LQFP100', 'pindata/STM32F303V(B-C)Tx.xml'),
    ('LQFP64', 'pindata/STM32F303R(B-C)Tx.xml'),
    ('LQFP48', 'pindata/STM32F303C(B-C)Tx.xml'),
    ('LQFP100_F358', 'pindata/STM32F358VCTx.xml'),
    ('LQFP64_F358', 'pindata/STM32F358RCTx.xml'),
    ('LQFP48_F358', 'pindata/STM32F358CCTx.xml'),
]
# El F302 tiene las patillas del F303: se comprueba, no se genera
IGUALES = [
    ('LQFP100', 'pindata/STM32F302V(B-C)Tx.xml'),
    ('LQFP64', 'pindata/STM32F302R(B-C)Tx.xml'),
    ('LQFP48', 'pindata/STM32F302C(B-C)Tx.xml'),
]
# Lo que cada linea no tiene, mirado en su LQFP100 (en los pequenos faltan
# ademas las IP que no tienen patillas: UART4/5 en el LQFP48)
LINEAS = [
    ('F302', 'pindata/STM32F302V(B-C)Tx.xml'),
    ('F358', 'pindata/STM32F358VCTx.xml'),
]


def pin_de(nombre):
    """'PF0-OSC_IN' -> (5, 0); 'VDD' -> None"""
    m = re.match(r'^P([A-F])(\d{1,2})(?:[-_].*)?$', nombre)
    if not m:
        return None
    return LETRAS.index(m.group(1)), int(m.group(2))


def lee(fichero):
    """(raiz, patillas ordenadas, mascara, ionb, ips)"""
    mcu = ET.parse(fichero).getroot()
    ionb = int(mcu.find('s:IONb', NS).text)
    patillas = []
    mascara = [0] * len(LETRAS)
    for p in mcu.findall('s:Pin', NS):
        pos, nom, tipo = int(p.get('Position')), p.get('Name'), p.get('Type')
        patillas.append((pos, nom, tipo))
        if tipo == 'I/O':
            q = pin_de(nom)
            if q is None:
                sys.exit('%s: patilla de E/S sin puerto: %s' % (fichero, nom))
            mascara[q[0]] |= 1 << q[1]
    patillas.sort()
    if [x[0] for x in patillas] != list(range(1, len(patillas) + 1)):
        sys.exit('%s: las posiciones no son 1..%u' % (fichero, len(patillas)))
    n_io = sum(bin(m).count('1') for m in mascara)
    if n_io != ionb:
        sys.exit('%s: la base dice %u E/S y la mascara tiene %u' % (fichero, ionb, n_io))
    ips = set(i.get('InstanceName') for i in mcu.findall('s:IP', NS))
    return mcu, patillas, mascara, n_io, ips


CAB = ('// GENERADO por gen_pines.py a partir de la base de pines de ST\n'
       '// (STM32_open_pin_data, BSD-3; pindata/): NO SE EDITA A MANO.\n')


def main():
    if len(sys.argv) != 1:
        sys.exit('uso: gen_pines.py   (desde src/soc/stm32f3, lee pindata/)')
    leidos = {}
    for id_, f in ENCAPSULADOS:
        mcu, patillas, mascara, n_io, ips = lee(f)
        leidos[id_] = (mcu, patillas, mascara, n_io, ips)
        with open('f3_patillas_%s.inc' % id_.lower(), 'w', newline='\n') as o:
            o.write(CAB)
            o.write('// Las %u patillas del %s del %s: {posicion, nombre, tipo}.\n'
                    % (len(patillas), mcu.get('Package'), mcu.get('RefName')))
            o.write('// Tipos: I/O, Power, Reset, Boot, MonoIO.\n')
            o.write('#ifndef F3_PATILLA\n#define F3_PATILLA(pos, nombre, tipo)\n#endif\n')
            for pos, nom, tipo in patillas:
                o.write('F3_PATILLA(%3u, "%s", "%s")\n' % (pos, nom, tipo))
            o.write('#undef F3_PATILLA\n')
            o.write('// %u E/S [IONb = %u]: A..F\n' % (n_io, n_io))
            o.write('#define F3_MASCARA_%s %s\n'
                    % (id_, ', '.join('0x%04Xu' % m for m in mascara)))
            o.write('#define F3_N_GPIO_%s %uu\n' % (id_, n_io))
    ref = leidos['LQFP100'][2]
    for id_, (_, _, m, _, _) in leidos.items():
        if any(m[i] & ~ref[i] for i in range(len(LETRAS))):
            sys.exit('%s tiene pines que el LQFP100 no tiene' % id_)
    for id_, f in IGUALES:
        if lee(f)[1] != leidos[id_][1]:
            sys.exit('%s: no tiene las patillas del F303 en el %s' % (f, id_))

    gpio = ET.parse(GPIO).getroot()
    af = []
    for g in gpio.findall('s:GPIO_Pin', NS):
        q = pin_de(g.get('Name'))
        if q is None or not (ref[q[0]] >> q[1]) & 1:
            continue                       # no sale en el LQFP100
        for s in g.findall('s:PinSignal', NS):
            for v in s.findall('s:SpecificParameter/s:PossibleValue', NS):
                m = re.match(r'^GPIO_AF(\d+)_', v.text or '')
                if not m or not 0 <= int(m.group(1)) <= 15:
                    sys.exit('AF que no se entiende: %s %s' % (g.get('Name'), v.text))
                af.append((q[0], q[1], int(m.group(1)), s.get('Name')))
    af = sorted(set(af))
    with open('f3_pines.inc', 'w', newline='\n') as f:
        f.write(CAB)
        f.write('// Las %u funciones alternativas de los pines que salen en el '
                'LQFP100:\n// {puerto (0 = A), pin, AF, senal}.\n' % len(af))
        for p, i, n, s in af:
            f.write('{%u, %2u, %2u, "%s"},  // P%s%u\n' % (p, i, n, s, LETRAS[p], i))

    ips303 = leidos['LQFP100'][4]
    with open('f3_lineas.inc', 'w', newline='\n') as f:
        f.write(CAB)
        f.write('// Lo que cada linea del die 0x422 no tiene del F303, en su LQFP100:\n'
                '// las IP que la base de pines da al F303V(B-C)Tx y no a ella.\n')
        for linea, fich in LINEAS:
            ips = lee(fich)[4]
            if ips - ips303:
                sys.exit('%s tiene IP que el F303 no tiene: %s' % (linea, sorted(ips - ips303)))
            f.write('#define F3_SIN_%s "%s"\n' % (linea, ' '.join(sorted(ips303 - ips))))
    # Las entradas de los ADC, de las señales de cada patilla [PINDATA]
    ain = []
    for p in leidos['LQFP100'][0].findall('s:Pin', NS):
        q = pin_de(p.get('Name'))
        if q is None:
            continue
        for sg in p.findall('s:Signal', NS):
            m = re.match(r'^ADC([1-4])_IN(\d+)$', sg.get('Name'))
            if m:
                ain.append((int(m.group(1)), int(m.group(2)), q[0], q[1]))
    ain.sort()
    if len(set((a, c) for a, c, _, _ in ain)) != len(ain):
        sys.exit('un canal de ADC en dos pines')
    with open('f3_analogico.inc', 'w', newline='\n') as f:
        f.write(CAB)
        f.write('// Las %u entradas de los ADC que salen al LQFP100: {adc, canal, puerto (0 = A),\n'
                '// pin}. ADC12_INx sale como ADC1_INx y ADC2_INx en el mismo pin.\n' % len(ain))
        for a, c, p_, i in ain:
            f.write('{%u, %2u, %u, %2u},  // ADC%u_IN%u en P%s%u\n' % (a, c, p_, i, a, c, LETRAS[p_], i))
    print('%s; %u funciones alternativas'
          % (', '.join('%s %u E/S' % (k, v[3]) for k, v in leidos.items()), len(af)))


if __name__ == '__main__':
    main()
