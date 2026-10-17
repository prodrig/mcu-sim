#!/usr/bin/env python3
# gen_pines.py PIN_MAP_H > e4_pines.inc
# La tabla de funciones de pin del MSP432E401Y (UART, I2C, SSI y GPTM), sacada
# de pin_map.h de DriverLib (__MSP432E401Y__), nunca escrita a mano: cada
# GPIO_Pxn_FUNC = puerto << 16 | (4 * pin) << 8 | PCTL [DL: GPIOPinConfigure].
#   python3 gen_pines.py ../../verif/fw/msp432e4/sdk/ti/devices/msp432e4/driverlib/pin_map.h > e4_pines.inc
import re
import sys

s = open(sys.argv[1], encoding='latin-1').read()
a = s.index('#ifdef __MSP432E401Y__')
b = s.index('#endif // __MSP432E401Y__')
quiero = re.compile(r'^(U\dRX|U\dTX|I2C\dSCL|I2C\dSDA|SSI\d(CLK|FSS|XDAT0|XDAT1)|T\dCCP[01])$')
o = ['// GENERADO a partir de pin_map.h de DriverLib (__MSP432E401Y__): NO SE EDITA A MANO.',
     '// Las funciones de UART, I2C, SSI y GPTM de cada pin: {puerto, pin, PCTL, nombre}.']
for m in re.finditer(r'#define GPIO_P([A-Z])(\d)_(\w+)\s+0x([0-9A-Fa-f]+)', s[a:b]):
    letra, pin, fn, val = m.group(1), int(m.group(2)), m.group(3), int(m.group(4), 16)
    port, shift, pctl = (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xF
    if shift != 4 * pin:
        sys.exit('pin_map.h: %s no cuadra' % m.group(0))
    if quiero.match(fn):
        o.append('{%2u, %u, %2u, "%s"},  // P%s%u' % (port, pin, pctl, fn, letra, pin))
print('\n'.join(o))
