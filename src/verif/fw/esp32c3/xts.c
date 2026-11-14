/* xts.c — E8: el cifrado de la Flash (XTS_AES). Escribe cuatro líneas
 * cifradas con la secuencia de spi_flash_chip_generic_write_encrypted (el
 * texto en claro a PLAIN, TRIGGER, RELEASE y un PP del SPI1 con el texto en
 * claro en W0-W7, que el hardware cambia por el cifrado) y las lee de vuelta
 * por el SPI1 (cifradas) y por la caché (en claro). El banco quema antes
 * KEY5 = XTS_EFUSE con propósito 4 y SPI_BOOT_CRYPT_CNT = 1; los resultados
 * son los de espsecure (xts_vec.h, de gen_xts.py).
 *   res[0]  bits de lo que sale bien:
 *           0 sin los permisos de SYSTEM_EXTERNAL_DEVICE_ENCRYPT_DECRYPT_CONTROL,
 *             TRIGGER no hace nada
 *           1 STATE: 1 justo tras TRIGGER, 2 al acabar, 3 tras RELEASE y 0
 *             tras DESTROY
 *           2 el SPI1 lee de la Flash el texto cifrado de espsecure
 *           3 la caché (0x3C01_0000, la página 1) lee el texto en claro
 *           4 la línea de 16 bytes (LINESIZE = 0) en 0x10070
 *           5 lo que no se ha escrito (0x10060) sigue borrado en la Flash
 *   res[1]  la primera palabra que lee el SPI1
 *   res[2]  DATE */
#include "c3.h"
#include "xts_vec.h"
#define XTS(o)   R(0x600CC000u + (o))
#define SPI1(o)  R(0x60002000u + (o))
#define SYS_EXT_CIFRADO R(0x600C0044u)
#define EXTMEM(o) R(0x600C4000u + (o))

static void espera_spi(void) { while (SPI1(0x00)) {} }
static void wip(void) {
    do { SPI1(0x00) = 1u << 27; espera_spi(); } while (SPI1(0x2C) & 1u);
}
static void wren(void) { SPI1(0x00) = 1u << 30; espera_spi(); }
static uint32_t estado_ok;
/* Una línea de `n` bytes (16 o 32) en `a`, con el texto en claro p */
static void escribe(uint32_t a, const uint32_t *p, unsigned n) {
    while (XTS(0x58) != 0) {}
    XTS(0x44) = 0;                                    /* DESTINATION: la Flash */
    XTS(0x40) = n >> 5;
    XTS(0x48) = a;
    for (unsigned i = 0; i < n / 4; ++i) XTS(((a % 32u) + 4 * i) % 32u) = p[i];
    XTS(0x4C) = 1;
    if (XTS(0x58) != 1) estado_ok = 0;
    while (XTS(0x58) == 1) {}
    if (XTS(0x58) != 2) estado_ok = 0;
    XTS(0x50) = 1;
    while (XTS(0x58) != 3) {}
    wren();
    for (unsigned i = 0; i < n / 4; ++i) SPI1(0x58 + 4 * i) = p[i];
    SPI1(0x04) = a | (n << 24);
    SPI1(0x00) = 1u << 25;                            /* PP */
    espera_spi();
    wip();
    XTS(0x54) = 1;
    if (XTS(0x58) != 0) estado_ok = 0;
}
static void lee_spi1(uint32_t a, uint32_t *d, unsigned n) {
    SPI1(0x28) = 8 * n - 1;                           /* MISO_DLEN */
    SPI1(0x04) = a;
    SPI1(0x00) = 1u << 31;                            /* READ */
    espera_spi();
    for (unsigned i = 0; i < n / 4; ++i) d[i] = SPI1(0x58 + 4 * i);
}

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0, d[28];
    buzon.res[2] = XTS(0x5C);
    /* Sin permiso: TRIGGER no cifra */
    SYS_EXT_CIFRADO = 0;
    XTS(0x40) = 1;
    XTS(0x48) = 0x10000;
    XTS(0x4C) = 1;
    if (XTS(0x58) == 0) ok |= 1u << 0;
    /* El sector 0x10000, borrado */
    /* ENABLE_SPI_MANUAL_ENCRYPT y ENABLE_DOWNLOAD_MANUAL_ENCRYPT, como
     * spi_flash_encrypt_ll_enable, y ENABLE_DOWNLOAD_G0CB_DECRYPT: así vale
     * en los dos modos de arranque */
    SYS_EXT_CIFRADO = 0xDu;
    wren();
    SPI1(0x04) = 0x10000;
    SPI1(0x00) = 1u << 24;                            /* SE */
    espera_spi();
    wip();
    estado_ok = 1;
    escribe(0x10000, XTS_PLANO, 32);
    escribe(0x10020, XTS_PLANO + 8, 32);
    escribe(0x10040, XTS_PLANO + 16, 32);
    escribe(0x10070, XTS_PLANO + 24, 16);
    if (estado_ok) ok |= 1u << 1;
    SYS_EXT_CIFRADO = 0x4u;                          /* solo el descifrado */
    /* Por el SPI1: cifrado */
    int bien = 1;
    lee_spi1(0x10000, d, 64);
    lee_spi1(0x10040, d + 16, 32);
    lee_spi1(0x10070, d + 24, 16);
    buzon.res[1] = d[0];
    for (unsigned i = 0; i < 28; ++i) if (d[i] != XTS_CIFRADO[i]) bien = 0;
    if (bien) ok |= 1u << 2;
    lee_spi1(0x10060, d, 16);
    if (d[0] == 0xFFFFFFFFu && d[3] == 0xFFFFFFFFu) ok |= 1u << 5;
    /* Por la caché: en claro */
    EXTMEM(0x000) |= 1u;                              /* ICACHE_CTRL.ENABLE */
    EXTMEM(0x004) = 0;                                /* sin SHUT_IBUS ni SHUT_DBUS */
    R(0x600C5004u) = 1;                               /* MMU[1] = la página 1 */
    const volatile uint32_t *c = (const volatile uint32_t *)0x3C010000u;
    bien = 1;
    for (unsigned i = 0; i < 24; ++i) if (c[i] != XTS_PLANO[i]) bien = 0;
    if (bien) ok |= 1u << 3;
    bien = 1;
    for (unsigned i = 0; i < 4; ++i) if (c[28 + i] != XTS_PLANO[24 + i]) bien = 0;
    if (bien) ok |= 1u << 4;
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
