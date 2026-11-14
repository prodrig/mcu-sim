#!/usr/bin/env python3
# gen_hci.py — Vectores de la cabecera de HCI del UHCI del C3 (E8): las
# tramas de la prueba D-H de uhci_hci.c, la cabecera ya arreglada (ack y suma)
# y lo que debe pasar por la línea en D, con el formato de H5 (Bluetooth Core
# Vol 4 Part D, «Three-wire UART»). La segunda fuente del CRC es
# binascii.crc_hqx (CRC-CCITT MSB primero, sin reflejar) sobre los bytes dados
# la vuelta: es lo mismo que el crc_ccitt reflejado de Linux seguido de
# bitrev16 (hci_h5.c), que se comprueba aquí. Escribe hci_vec.h.
import binascii, sys

def rev8(b):
    return int("{:08b}".format(b)[::-1], 2)

def crc_h5(data):
    """El CRC de H5 tal como sale: el byte alto primero."""
    return binascii.crc_hqx(bytes(rev8(b) for b in data), 0xFFFF)

def crc_linux(data):                       # crc_ccitt + bitrev16, para comparar
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x8408 if c & 1 else c >> 1
    return int("{:016b}".format(c)[::-1], 2)

assert crc_linux(b"123456789") == int("{:016b}".format(0x6F91)[::-1], 2)
for t in (b"", b"\x00", b"123456789", bytes(range(40))):
    assert crc_h5(t) == crc_linux(t)

def cab(seq, ack, dic, fiable, tipo, largo):
    h = [seq | ack << 3 | dic << 6 | fiable << 7, tipo | (largo & 15) << 4, largo >> 4]
    return h + [(~sum(h)) & 0xFF]

def slip(bs):
    o = []
    for b in bs:
        o += {0xC0: [0xDB, 0xDC], 0xDB: [0xDB, 0xDD]}.get(b, [b])
    return o

# D: fiable, seq 0, DIC, tipo 2, 10 bytes; el programa pone ack = 5 y la
# suma mal (0x55): el UHCI pone su ACK_NUM (0) y la suma
D_PAY = [0x10, 0xC0, 0x22, 0xDB, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88]
D_TX = [0x80 | 5 << 3 | 0x40, 0x02 | 10 << 4, 0x00, 0x55] + D_PAY
D_CAB = cab(0, 0, 1, 1, 2, 10)
c = crc_h5(D_CAB + D_PAY)
D_LINEA = [0xC0] + slip(D_CAB + D_PAY + [c >> 8, c & 0xFF]) + [0xC0]
# H: el rápido Q0: tipo 1, 3 bytes (AA BB CC), con el ack que tenga entonces (1)
H_CAB = cab(0, 1, 0, 0, 1, 3)

def b(v):
    return ", ".join("0x%02X" % x for x in v)

with open(sys.argv[1] if len(sys.argv) > 1 else "hci_vec.h", "w") as f:
    f.write("/* hci_vec.h — generado por gen_hci.py: no editar */\n")
    f.write("static const uint8_t HCI_D_TX[%d] = {%s};\n" % (len(D_TX), b(D_TX)))
    f.write("static const uint8_t HCI_D_CAB[4] = {%s};\n" % b(D_CAB))
    f.write("static const uint8_t HCI_D_LINEA[%d] = {%s};\n" % (len(D_LINEA), b(D_LINEA)))
    f.write("static const uint8_t HCI_H_CAB[4] = {%s};\n" % b(H_CAB))
