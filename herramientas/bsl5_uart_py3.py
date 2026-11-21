#!/usr/bin/env python3
# =============================================================================
# bsl5_uart_py3.py — `python -m msp430.bsl5.uart` de python-msp430-tools, con
# lo que le falta para Python 3 (M9 de doc/analisis_msp430.md)
#
# La versión de PyPI (0.10) y la de GitHub (agosto de 2026) llevan en
# msp430/bsl5 restos de Python 2 que la rompen antes de la primera respuesta:
#   * crc_update() hace ord() de bytes;
#   * three_bytes() y los mensajes del borrado y del reset son str;
#   * bsl() compara el byte de cabecera con '\x80', calcula el CRC de la
#     respuesta sobre la cabecera además del núcleo y compara una tupla con
#     un entero;
#   * check_answer() y las longitudes esperadas no cuentan el byte 0x3A/0x3B;
#   * buffer_size se queda en None (detect_buffer_size está comentado);
#   * -e manda una contraseña mala y espera un BSL5Error, pero el BSL de las
#     FRAM no contesta nada al borrado [BSLFR SLAU550G §4.3.2.2: «If a mass
#     erase is initiated, no command is returned»]: lo que llega es un
#     BSL5Timeout, que no es un BSL5Error, y la herramienta se para.
# Aquí se sustituyen esas piezas y se usa todo lo demás de la herramienta tal
# cual: las opciones, la secuencia de TEST y RST, program_file, verify_by_file,
# --password y -e (el borrado por contraseña mala, como hace ella), -r.
#
#   python3 herramientas/bsl5_uart_py3.py -p rfc2217://localhost:3370 \
#       --invert-reset -e -P blink.hex -V -r
# con placas/msp_exp430fr5969_bsl.xml (el adaptador de placas/bsl_adaptador.xml):
# RST por DTR (sin inversor: --invert-reset) y TEST por RTS.
# =============================================================================
import functools
import struct
import sys
import time

import msp430.bsl5.bsl5 as bsl5
import msp430.bsl5.uart as uart


def crc_update(crc, byte):
    x = ((crc >> 8) ^ byte) & 0xff
    x ^= x >> 4
    return ((crc << 8) ^ (x << 12) ^ (x << 5) ^ x) & 0xffff


def tres(address):
    return bytes([address & 0xff, (address >> 8) & 0xff, (address >> 16) & 0xff])


def bsl(self, cmd, message=b'', expect=None, receive_response=True):
    """Una orden: 0x80, NL, NH, el núcleo y su CRC; devuelve el núcleo de la respuesta."""
    message = bytes(message)
    nucleo = bytes([cmd]) + message
    txdata = struct.pack('<BH', 0x80, len(nucleo)) + nucleo
    txdata += struct.pack('<H', functools.reduce(crc_update, nucleo, 0xffff))
    self.serial.write(txdata)
    if self.ignore_answer:
        return b''
    ans = b''
    for _ in range(self.extra_timeout or 1):
        ans = self.serial.read(1)
        if ans:
            break
    if ans != b'\x00':
        if ans:
            raise bsl5.BSL5Error('BSL reports error: %s' % uart.BSL5_UART_ERROR_CODES.get(ans[0], 'unknown error'))
        raise bsl5.BSL5Error('No ACK received (timeout)')
    if not receive_response:
        return b''
    head = self.serial.read(3)
    if len(head) != 3:
        raise bsl5.BSL5Timeout('timeout while reading answer (header)')
    pi, length = struct.unpack('<BH', head)
    if pi != 0x80:
        raise bsl5.BSL5Error('received bad PI, expected 0x80 (got 0x%02x)' % pi)
    data = self.serial.read(length)
    crc = self.serial.read(2)
    if len(data) != length or len(crc) != 2:
        raise bsl5.BSL5Timeout('timeout while reading answer')
    if struct.unpack('<H', crc)[0] != functools.reduce(crc_update, data, 0xffff):
        raise bsl5.BSL5Error('CRC error in answer')
    if expect is not None and length != expect + 1:
        raise bsl5.BSL5Error('expected %d bytes, got %d bytes' % (expect, length - 1))
    return data


def check_answer(self, data):
    if data[0] == 0x3b:
        if data[1] == 0:
            return
        raise bsl5.BSL5Error(bsl5.BSL5_ERROR_CODES.get(data[1], 'unknown error response 0x%02x' % data[1]))
    if data[0] != 0x3a:
        raise bsl5.BSL5Error('unknown response 0x%02x' % data[0])


def BSL_RX_DATA_BLOCK(self, address, data):
    self.check_answer(self.bsl(bsl5.BSL_RX_DATA_BLOCK, tres(address) + bytes(data), expect=1))


def BSL_RX_DATA_BLOCK_FAST(self, address, data):
    self.bsl(bsl5.BSL_RX_DATA_BLOCK_FAST, tres(address) + bytes(data), receive_response=False)


def BSL_TX_DATA_BLOCK(self, address, length):
    answer = self.bsl(bsl5.BSL_TX_DATA_BLOCK, tres(address) + struct.pack('<H', length), expect=length)
    self.check_answer(answer)
    return answer[1:]


def BSL_RX_PASSWORD(self, password):
    self.check_answer(self.bsl(bsl5.BSL_RX_PASSWORD, bytes(password), expect=1))


def BSL_LOAD_PC(self, address):
    self.bsl(bsl5.BSL_LOAD_PC, tres(address), receive_response=False)


def BSL_VERSION(self):
    answer = self.bsl(bsl5.BSL_VERSION, expect=4)
    self.check_answer(answer)
    return struct.unpack('<BBBBB', answer)[1:]


def memory_write(self, address, data):
    data = bytes(data)
    if len(data) & 1:
        data += b'\xff'
    while data:
        block, data = data[:self.buffer_size], data[self.buffer_size:]
        self.BSL_RX_DATA_BLOCK(address, block)
        address += len(block)


def open_connection(self):
    """La de la herramienta, con bytes en las contraseñas y el búfer de 240."""
    self.buffer_size = 240
    self.open(self.options.port, ignore_answer=self.options.ignore_answer)
    self.control_delay = self.options.control_delay
    if self.options.test_on_tx:
        self.testOnTX = True
    if self.options.invert_test:
        self.invertTEST = True
    if self.options.invert_reset:
        self.invertRST = True
    if self.options.swap_reset_test:
        self.swapResetTest = True
    self.set_TEST(True)
    self.set_RST(True)
    if self.options.start_pattern:
        self.start_bsl()
    if self.options.do_mass_erase:
        self.logger.info('Mass erase...')
        try:
            self.BSL_RX_PASSWORD(b'\xff' * 30 + b'\0' * 2)
        except bsl5.BSL5Exception:
            pass                                    # sin respuesta: es lo que borra
        time.sleep(1)
        self.BSL_RX_PASSWORD(b'\xff' * 32)
        self.remove_action(self.mass_erase)
    elif self.options.password is not None:
        import msp430.memory
        self.BSL_RX_PASSWORD(msp430.memory.load(self.options.password).get_range(0xffe0, 0xffff))
    if self.options.speed is not None:
        self.set_baudrate(self.options.speed)
    v = self.BSL_VERSION()
    self.logger.info('BSL version %s' % '.'.join('%02X' % x for x in v))


for nombre, f in [('bsl', bsl), ('check_answer', check_answer), ('BSL_RX_DATA_BLOCK', BSL_RX_DATA_BLOCK),
                  ('BSL_RX_DATA_BLOCK_FAST', BSL_RX_DATA_BLOCK_FAST), ('BSL_TX_DATA_BLOCK', BSL_TX_DATA_BLOCK),
                  ('BSL_RX_PASSWORD', BSL_RX_PASSWORD), ('BSL_LOAD_PC', BSL_LOAD_PC), ('BSL_VERSION', BSL_VERSION),
                  ('memory_write', memory_write)]:
    setattr(uart.SerialBSL5, nombre, f)
uart.SerialBSL5Target.open_connection = open_connection
uart.crc_update = crc_update

if __name__ == '__main__':
    uart.main()
