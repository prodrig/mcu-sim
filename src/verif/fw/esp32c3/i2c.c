/* i2c.c — E8: el I2C del C3 contra una 24Cxx (0x50) en SDA = GPIO1 y
 * SCL = GPIO10, con pull-ups en la placa y drenador abierto en los pads.
 * Como i2c_eeprom de ESP-IDF, con la lista de órdenes:
 *   escribir: RSTART, WRITE 6 (0xA0, 0x10, 4 datos), STOP
 *   leer:     RSTART, WRITE 2 (0xA0, 0x10), RSTART, WRITE 1 (0xA1),
 *             READ 3 con ACK, READ 1 con NACK, STOP
 *   res[0]  INT_RAW tras escribir  res[1..4] lo leído  res[5] INT_RAW tras leer
 *   res[6]  un NACK: la dirección 0x51 no contesta */
#include "c3.h"
#define I2C(o) R(0x60013000u + (o))
#define CMD(op, n, chk) (((op) << 11) | ((chk) ? (1u << 8) : 0u) | (n))
#define LEER(n, nack) ((3u << 11) | ((nack) ? (1u << 10) : 0u) | (n))

static void pin_od(unsigned g, unsigned sig) {
    IO_MUX(g) = IOMUX_GPIO | IOMUX_FUN_IE | IOMUX_FUN_WPU | (2u << 10);
    GPIO_PIN(g) = 1u << 2;                      /* PAD_DRIVER: drenador abierto */
    GPIO_FUNC_OUT(g) = sig;                     /* OE del periférico */
    GPIO_FUNC_IN(sig) = g | (1u << 6);
}
static uint32_t corre(void) {
    I2C(0x24) = 0x3FFFu;
    I2C(0x04) = (1u << 4) | (1u << 5);          /* MS_MODE, TRANS_START */
    uint32_t r;
    while (!((r = I2C(0x20)) & ((1u << 7) | (1u << 10) | (1u << 3)))) {}
    return r;
}
int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    pin_od(1, 54);                               /* SDA */
    pin_od(10, 53);                              /* SCL */
    I2C(0x54) = 1u << 21;                        /* SCLK_ACTIVE, XTAL 40 MHz, sin dividir */
    I2C(0x00) = 199;                             /* 100 kHz: 200 bajo + 200 alto */
    I2C(0x38) = 199;
    /* Escribir */
    I2C(0x18) = 0x408Bu | (3u << 12);  I2C(0x18) = 0x408Bu;   /* vaciar las FIFO */
    const uint8_t w[6] = {0xA0, 0x10, 0xC0, 0xFF, 0xEE, 0x42};
    for (unsigned i = 0; i < 6; ++i) I2C(0x1C) = w[i];
    I2C(0x58) = CMD(6, 0, 0); I2C(0x5C) = CMD(1, 6, 1); I2C(0x60) = CMD(2, 0, 0);
    buzon.res[0] = corre();
    espera_us(100);
    /* Leer */
    I2C(0x18) = 0x408Bu | (3u << 12);  I2C(0x18) = 0x408Bu;
    I2C(0x1C) = 0xA0; I2C(0x1C) = 0x10; I2C(0x1C) = 0xA1;
    I2C(0x58) = CMD(6, 0, 0); I2C(0x5C) = CMD(1, 2, 1); I2C(0x60) = CMD(6, 0, 0);
    I2C(0x64) = CMD(1, 1, 1); I2C(0x68) = LEER(3, 0); I2C(0x6C) = LEER(1, 1); I2C(0x70) = CMD(2, 0, 0);
    buzon.res[5] = corre();
    for (unsigned i = 0; i < 4; ++i) buzon.res[1 + i] = I2C(0x1C) & 0xFFu;
    /* Un esclavo que no está */
    I2C(0x18) = 0x408Bu | (3u << 12);  I2C(0x18) = 0x408Bu;
    I2C(0x1C) = 0xA2;
    I2C(0x58) = CMD(6, 0, 0); I2C(0x5C) = CMD(1, 1, 1); I2C(0x60) = CMD(2, 0, 0);
    buzon.res[6] = corre();
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
