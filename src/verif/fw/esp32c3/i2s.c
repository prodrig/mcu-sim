/* i2s.c — E8: el I2S en Philips, 16 bits y estéreo, en dúplex con
 * SIG_LOOPBACK (el receptor con el BCK y el WS del transmisor), como
 * i2s_std de ESP-IDF: BCK = 160 MHz / (13 + 1/48) / 8 = 1,536 MHz y 48 kHz.
 * BCK, WS y SD por GPIO0, GPIO2 y GPIO3; SD vuelve a entrar por GPIO3. Se
 * mandan 32 tramas (128 bytes) por el canal 0 de la GDMA y se reciben por el
 * canal 1, en dos descriptores de 64 bytes (RXEOF_NUM = 64).
 *   res[0]  bits de lo que sale bien:
 *           0 lo recibido es lo mandado
 *           1 los dos descriptores de entrada: length 64, suc_eof y owner 0
 *           2 la GDMA: IN_SUC_EOF e IN_DONE en el canal 1, OUT_EOF en el 0
 *           3 el I2S: TX_DONE y RX_DONE, y STATE.TX_IDLE a 1 al acabar
 *           4 TX_UPDATE y RX_UPDATE se bajan solos
 *   res[1]  la primera palabra recibida
 *   res[2]  DATE
 *   res[3]  1 mientras manda (el banco mide BCK y WS en los pines) */
#include "c3.h"
#define I2S(o)   R(0x6002D000u + (o))
#define GDMA(o)  R(0x6003F000u + (o))
#define N_TR 32

typedef struct { volatile uint32_t w0; volatile uint32_t buf; volatile uint32_t sig; } dscr_t;
static uint16_t tx[2 * N_TR] __attribute__((aligned(4)));
static uint8_t rx[128] __attribute__((aligned(4)));
static dscr_t dtx, drx[2];

static uint32_t a20(const volatile void *p) { return (uint32_t)p & 0xFFFFFu; }

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    for (unsigned i = 0; i < N_TR; ++i) {
        tx[2 * i] = (uint16_t)(0x1000u + i * 0x111u);
        tx[2 * i + 1] = (uint16_t)(0xF000u ^ (i * 0x0F0Fu));
    }
    for (unsigned i = 0; i < sizeof rx; ++i) rx[i] = 0xEE;
    buzon.res[2] = I2S(0x80);
    /* Los pines: BCK, WS y SD fuera; SD dentro por el mismo GPIO3 */
    IO_MUX(0) = IOMUX_GPIO | (2u << 10);
    IO_MUX(2) = IOMUX_GPIO | (2u << 10);
    IO_MUX(3) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    GPIO_FUNC_OUT(0) = 13;
    GPIO_FUNC_OUT(2) = 14;
    GPIO_FUNC_OUT(3) = 15;
    GPIO_FUNC_IN(15) = 3 | (1u << 6);
    /* El reloj: PLL de 160 MHz, N = 13 y b/a = 1/48 (X = 47, Y = 0, Z = 1),
     * BCK_DIV = 8; los dos, como i2s_ll_tx/rx_set_mclk y set_bck_div_num */
    I2S(0x34) = 13u | (1u << 26) | (2u << 27) | (1u << 29);
    I2S(0x3C) = 1u | (0u << 9) | (47u << 18);
    I2S(0x30) = 13u | (1u << 26) | (2u << 27);
    I2S(0x38) = 1u | (0u << 9) | (47u << 18);
    /* Philips, 16 bits, dos canales: CONF1 = WS_WIDTH 15, BCK_DIV 7, BITS 15,
     * HALF_SAMPLE 15, CHAN_BITS 15 y MSB_SHIFT */
    const uint32_t conf1 = 15u | (7u << 7) | (15u << 13) | (15u << 18) | (15u << 24) | (1u << 29);
    I2S(0x2C) = conf1;
    I2S(0x28) = conf1;
    I2S(0x54) = 3u | (1u << 16);                       /* canales 0 y 1 de 2 */
    I2S(0x50) = 3u | (1u << 16);
    /* TX: TDM, sin compresión, STOP_EN, alineado a la izquierda y
     * SIG_LOOPBACK; RX: TDM, sin compresión, alineado a la izquierda */
    const uint32_t txc = (1u << 12) | (1u << 13) | (1u << 15) | (1u << 19) | (1u << 27);
    const uint32_t rxc = (1u << 12) | (1u << 15) | (1u << 19);
    I2S(0x24) = txc | (1u << 8);
    I2S(0x20) = rxc | (1u << 8);
    int upd = !(I2S(0x24) & (1u << 8)) && !(I2S(0x20) & (1u << 8));
    I2S(0x24) = txc | 3u; I2S(0x24) = txc;             /* los reset */
    I2S(0x20) = rxc | 3u; I2S(0x20) = rxc;
    if (upd) ok |= 1u << 4;
    I2S(0x64) = 64;                                    /* RXEOF_NUM */
    I2S(0x18) = 0xF;
    /* La GDMA: el canal 0 sale, el 1 entra; los dos al periférico 3 */
    dtx.w0 = 128u | (128u << 12) | (1u << 30) | (1u << 31);
    dtx.buf = (uint32_t)tx; dtx.sig = 0;
    for (unsigned i = 0; i < 2; ++i) {
        drx[i].w0 = 64u | (1u << 31);
        drx[i].buf = (uint32_t)(rx + 64 * i);
        drx[i].sig = i ? 0 : (uint32_t)&drx[1];
    }
    GDMA(0x0C) = 0x1FFF; GDMA(0x1C) = 0x1FFF;
    GDMA(0x100) = 3;                                   /* OUT_PERI_SEL ch0 */
    GDMA(0x160) = 3;                                   /* IN_PERI_SEL ch1 */
    GDMA(0x140) = a20(&drx[0]) | (1u << 22);           /* INLINK_START ch1 */
    GDMA(0xE0) = a20(&dtx) | (1u << 21);               /* OUTLINK_START ch0 */
    /* En marcha: primero el receptor */
    I2S(0x20) = rxc | (1u << 8); while (I2S(0x20) & (1u << 8)) {}
    I2S(0x20) = rxc | (1u << 2);
    I2S(0x24) = txc | (1u << 8); while (I2S(0x24) & (1u << 8)) {}
    I2S(0x24) = txc | (1u << 2);
    buzon.res[3] = 1;
    while (!(I2S(0x0C) & 2u)) {}                       /* TX_DONE */
    espera_us(30);
    while (!(I2S(0x6C) & 1u)) {}                       /* TX_IDLE */
    buzon.res[3] = 2;
    int bien = 1;
    const uint8_t *t8 = (const uint8_t *)tx;
    for (unsigned i = 0; i < sizeof rx; ++i) if (rx[i] != t8[i]) bien = 0;
    if (bien) ok |= 1u << 0;
    buzon.res[1] = *(uint32_t *)rx;
    if (drx[0].w0 == (64u | (64u << 12) | (1u << 30)) && drx[1].w0 == (64u | (64u << 12) | (1u << 30))) ok |= 1u << 1;
    if ((GDMA(0x10) & 3u) == 3u && (GDMA(0x00) & (1u << 4))) ok |= 1u << 2;
    if ((I2S(0x0C) & 3u) == 3u && (I2S(0x6C) & 1u)) ok |= 1u << 3;
    I2S(0x24) = txc; I2S(0x20) = rxc;
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
