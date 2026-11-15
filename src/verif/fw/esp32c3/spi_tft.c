/* spi_tft.c — E8: el GP-SPI2 del C3 con un ST7735 (Tft128x160), como el
 * ejemplo spi_master de ESP-IDF: SCLK = GPIO6, MOSI = GPIO7, CS0 = GPIO10 y
 * D/C = GPIO3 (un GPIO). Las órdenes, de W0; los 10 x 10 píxeles (200 bytes,
 * rojo en RGB565), por la GDMA (canal 0, OUT_PERI_SEL = 0: SPI2), con un
 * descriptor.
 *   res[0] transacciones con TRANS_DONE   res[1] INT_RAW del canal 0 */
#include "c3.h"
#define SPI(o)  R(0x60024000u + (o))
#define GDMA(o) R(0x6003F000u + (o))

typedef struct { uint32_t dw0; const void *buf; const void *sig; } dscr_t;
static uint8_t pix[200];
static dscr_t d0;
static unsigned n_trans;

static void dc(int datos) { if (datos) GPIO_OUT_W1TS = 1u << 3; else GPIO_OUT_W1TC = 1u << 3; }
static void manda(void) {
    SPI(0x38) = 1u << 12;
    SPI(0x00) = 1u << 24;                                     /* USR */
    while (!(SPI(0x3C) & (1u << 12))) {}
    ++n_trans;
}
static void byte(int datos, uint8_t b) {
    dc(datos);
    SPI(0x30) = 0;                                            /* sin DMA */
    SPI(0x10) = 1u << 27;                                     /* USR_MOSI */
    SPI(0x1C) = 7;
    SPI(0x98) = b;
    manda();
}
static void orden(uint8_t o, const uint8_t *d, unsigned n) {
    byte(0, o);
    for (unsigned i = 0; i < n; ++i) byte(1, d[i]);
}
int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    const unsigned pins[3] = {6, 7, 10}, sigs[3] = {63, 65, 68};
    for (unsigned i = 0; i < 3; ++i) {
        IO_MUX(pins[i]) = IOMUX_GPIO | (2u << 10);
        GPIO_FUNC_OUT(pins[i]) = sigs[i];
    }
    IO_MUX(3) = IOMUX_GPIO | (2u << 10);
    GPIO_FUNC_OUT(3) = 128;                                   /* GPIO_OUT */
    GPIO_ENABLE_W1TS = 1u << 3;
    SPI(0x0C) = (3u << 18) | (1u << 12) | (1u << 6) | 1u;    /* 80 / (4 x 2) = 10 MHz */
    SPI(0x20) = 0;                                            /* modo 0, CS0 */
    espera_us(120000);                                        /* 120 ms tras el reset del panel */
    orden(0x11, 0, 0);                                        /* SLPOUT */
    espera_us(120000);
    const uint8_t colmod = 0x05, caset[4] = {0, 0, 0, 9}, raset[4] = {0, 0, 0, 9};
    orden(0x3A, &colmod, 1);
    orden(0x2A, caset, 4);
    orden(0x2B, raset, 4);
    orden(0x29, 0, 0);                                        /* DISPON */
    for (unsigned i = 0; i < 200; i += 2) { pix[i] = 0xF8; pix[i + 1] = 0x00; }
    d0.dw0 = 200u | (200u << 12) | (1u << 30) | (1u << 31);
    d0.buf = pix;
    d0.sig = 0;
    byte(0, 0x2C);                                            /* RAMWR */
    dc(1);
    GDMA(0x44) = 1u << 3;                                     /* CLK_EN */
    GDMA(0x100) = 0;                                          /* canal 0 -> SPI2 */
    GDMA(0xD0) = 1u << 2;                                     /* OUT_AUTO_WRBACK */
    GDMA(0xE0) = ((uint32_t)&d0 & 0xFFFFFu) | (1u << 21);    /* OUTLINK_START */
    SPI(0x30) = 1u << 28;                                     /* DMA_TX_ENA */
    SPI(0x10) = 1u << 27;
    SPI(0x1C) = 200 * 8 - 1;
    manda();
    buzon.res[0] = n_trans;
    buzon.res[1] = GDMA(0x00);
    buzon.res[2] = d0.dw0;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
