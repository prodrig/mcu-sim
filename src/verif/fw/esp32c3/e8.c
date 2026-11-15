/* e8.c — E8: el RMT con dos WS2812, el SAR ADC y el sensor de temperatura, y
 * el AES con los vectores de FIPS-197 (C.1 y C.3).
 *   res[0]  1: el RMT ha mandado la trama (TX_END)
 *   res[1]  ADC1 canal 0 (GPIO0) a 0 dB      res[2]  lo mismo a 11 dB
 *   res[3]  TSENS_OUT
 *   res[4..7]  AES-128 cifrado (C.1)          res[8..11] AES-256 cifrado (C.3)
 *   res[12..15] AES-256 descifrado: el texto en claro de nuevo */
#include "c3.h"
#define RMT(o)  R(0x60016000u + (o))
#define ADC(o)  R(0x60040000u + (o))
#define AES(o)  R(0x6003A000u + (o))

static uint32_t bit_ws(int uno) {             /* 10 MHz: 0,8/0,45 us o 0,4/0,85 us */
    return uno ? (8u | (1u << 15) | (5u << 16)) : (4u | (1u << 15) | (8u << 16));
}
static void aes(uint32_t modo, const uint32_t *k, unsigned nk, const uint32_t *in, volatile uint32_t *out) {
    for (unsigned i = 0; i < nk; ++i) AES(4 * i) = k[i];
    for (unsigned i = 0; i < 4; ++i) AES(0x20 + 4 * i) = in[i];
    AES(0x40) = modo;
    AES(0x48) = 1;
    while (AES(0x4C)) {}
    for (unsigned i = 0; i < 4; ++i) out[i] = AES(0x30 + 4 * i);
}

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    /* --- RMT: dos LED, GRB: (R 0x10, G 0x20, B 0x30) y (R 0xFF, G 0, B 0x80) --- */
    IO_MUX(8) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    GPIO_FUNC_OUT(8) = 51;                       /* RMT_SIG_OUT0 */
    RMT(0x68) = (1u << 31) | (1u << 24);         /* CLK_EN, APB, SCLK_DIV_NUM 0: 80 MHz */
    const uint32_t grb[2] = {0x201030u, 0x00FF80u};
    unsigned k = 0;
    for (unsigned l = 0; l < 2; ++l)
        for (int b = 23; b >= 0; --b) R(0x60016400u + 4 * k++) = bit_ws((grb[l] >> b) & 1u);
    R(0x60016400u + 4 * k) = 0;                  /* el final */
    RMT(0x40) = 1u;                              /* TX_END_CH0 */
    RMT(0x10) = (2u << 16) | (8u << 8) | (1u << 6) | (1u << 1) | 1u;   /* MEM 2, DIV 8, IDLE bajo, RD_RST, START */
    while (!(RMT(0x38) & 1u)) {}
    RMT(0x44) = 1u;
    buzon.res[0] = 1;
    espera_us(100);                              /* el RESET de los LED */
    /* --- SAR ADC: GPIO0, canal 0 de ADC1, a 0 dB y a 11 dB --- */
    for (unsigned at = 0; at < 2; ++at) {
        ADC(0x20) = (1u << 31) | (0u << 25) | ((at ? 3u : 0u) << 23);
        ADC(0x20) |= 1u << 29;
        while (!(ADC(0x44) & (1u << 31))) {}
        ADC(0x4C) = 1u << 31;
        buzon.res[1 + at] = ADC(0x2C) & 0xFFFu;
        ADC(0x20) = 0;
    }
    ADC(0x58) = 1u << 22;                        /* TSENS_PU */
    espera_us(300);
    buzon.res[3] = ADC(0x58) & 0xFFu;
    /* --- AES, FIPS-197 --- */
    const uint32_t k128[4] = {0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu};
    const uint32_t k256[8] = {0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu,
                              0x13121110u, 0x17161514u, 0x1B1A1918u, 0x1F1E1D1Cu};
    const uint32_t claro[4] = {0x33221100u, 0x77665544u, 0xBBAA9988u, 0xFFEEDDCCu};
    aes(0, k128, 4, claro, &buzon.res[4]);
    aes(2, k256, 8, claro, &buzon.res[8]);
    uint32_t c[4];
    for (unsigned i = 0; i < 4; ++i) c[i] = buzon.res[8 + i];
    aes(6, k256, 8, c, &buzon.res[12]);
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
