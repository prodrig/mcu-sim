/* xts_date.c — E8: XTS_AES_DATE_REG, como el chip: 0x20200623 de reset y 30
 * bits de lectura y escritura [xts_aes_reg.h de ESP-IDF, esp32c3.svd].
 *   res[0]  bits: 0 el valor de reset; 1 se escribe (los 30 bits bajos)
 *   res[1]  lo leído al arrancar;  res[2]  lo leído tras escribir 0xFFFFFFFF */
#include "c3.h"
#define XTS(o)  R(0x600CC000u + (o))

int main(void) {
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    const uint32_t d0 = XTS(0x5C);
    buzon.res[1] = d0;
    if (d0 == 0x20200623u) ok |= 1u;
    XTS(0x5C) = 0xFFFFFFFFu;
    const uint32_t d1 = XTS(0x5C);
    buzon.res[2] = d1;
    XTS(0x5C) = 0x12345678u;
    if (d1 == 0x3FFFFFFFu && XTS(0x5C) == 0x12345678u) ok |= 2u;
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
