/* i2s_mas.c — E8: lo que quedaba del I2S. Los pines: GPIO0 BCK, GPIO2 WS,
 * GPIO3 SD (sale y entra por el mismo pad) y GPIO4 MCLK. El canal 0 de la
 * GDMA manda y el 1 recibe (periférico 3).
 *   A  el receptor MAESTRO sin SIG_LOOPBACK y el transmisor ESCLAVO con su
 *      BCK y su WS: Philips de 16 bits; luego, sin BCK, TX_HUNG
 *   B  el transmisor maestro y el receptor ESCLAVO: MSB, canales de 32 bits
 *      con datos de 24 (24_FILL_EN); luego, sin BCK, RX_HUNG
 *   C  A-law y mu-law, en SIG_LOOPBACK: comprimir al mandar (se reciben los
 *      códigos) y expandir al recibir (se mandan los códigos)
 *   D  PDM en crudo en estéreo, de vuelta por el receptor PDM
 *   E  PCM a PDM: la densidad de unos sigue a la muestra (0,5 y -0,5)
 *   F  MCLK: el I2S_CLK del TX por GPIO4 (el banco lo mide) y, de vuelta por
 *      I2S_MCLK_in, como reloj del RX
 *   G  TX_TIMING: SD un periodo de I2S_CLK detrás y WS medio (el banco lo
 *      mide en los pines)
 *   H  TX_TDM_SKIP_MSK_EN con dos canales apagados y RX_STOP_MODE = 1
 *   res[0]  bits: 0 A datos; 1 A TX_HUNG; 2 B datos; 3 B RX_HUNG; 4-7 C (A
 *           comprime, A expande, mu comprime, mu expande); 8 D; 9 E primera
 *           mitad; 10 E segunda; 11 G datos; 12 H datos; 13 H el segundo
 *           descriptor sin tocar
 *   res[1]  unos en E (bits 15-0 primera mitad, 31-16 segunda)
 *   res[2]  el desfase de A (bytes);  res[3]  la fase (6 F, 7 G: el banco
 *           mide);  res[4]  el desfase de B */
#include "c3.h"
#include "i2s_vec.h"
#define I2S(o)   R(0x6002D000u + (o))
#define GDMA(o)  R(0x6003F000u + (o))

typedef struct { volatile uint32_t w0; volatile uint32_t buf; volatile uint32_t sig; } dscr_t;
static uint8_t tx[256] __attribute__((aligned(4)));
static uint8_t rx[256] __attribute__((aligned(4)));
static dscr_t dtx, drx[4];

static uint32_t a20(const volatile void *p) { return (uint32_t)p & 0xFFFFFu; }
static void gdma_tx(unsigned n) {
    dtx.w0 = n | (n << 12) | (1u << 30) | (1u << 31);
    dtx.buf = (uint32_t)tx; dtx.sig = 0;
    GDMA(0x0C) = 0x1FFF;
    GDMA(0x100) = 3;
    GDMA(0xE0) = a20(&dtx) | (1u << 21);
}
static void gdma_rx(unsigned n, unsigned tam) {
    for (unsigned i = 0; i < sizeof rx; ++i) rx[i] = 0xEE;
    for (unsigned i = 0; i < n; ++i) {
        drx[i].w0 = tam | (1u << 31);
        drx[i].buf = (uint32_t)(rx + 64 * i);
        drx[i].sig = i + 1 < n ? (uint32_t)&drx[i + 1] : 0;
    }
    GDMA(0x1C) = 0x1FFF;
    GDMA(0x160) = 3;
    GDMA(0x140) = a20(&drx[0]) | (1u << 22);
}
static void para_rx(void) { GDMA(0x140) = 1u << 21; }
static int espera_eof(unsigned n, uint32_t us) {
    unsigned k = 0;
    uint64_t lim = systimer_ahora() + (uint64_t)us * 16u;
    while (k < n && systimer_ahora() < lim) if (GDMA(0x10) & 2u) { GDMA(0x1C) = 2u; ++k; }
    return k == n;
}
/* I2S_CLK = 160 MHz / n; BCK = I2S_CLK / bck */
static void relojes(unsigned n_tx, unsigned n_rx) {
    I2S(0x34) = n_tx | (1u << 26) | (2u << 27) | (1u << 29);
    I2S(0x3C) = 1u << 9;
    I2S(0x30) = n_rx | (1u << 26) | (2u << 27);
    I2S(0x38) = 1u << 9;
}
/* CONF1: ws_ancho-1, bck-1, bits-1, mitad-1, canal-1, msb_shift */
static uint32_t conf1(unsigned ws, unsigned bck, unsigned bits, unsigned mitad, unsigned canal, unsigned shift) {
    return (ws - 1u) | ((bck - 1u) << 7) | ((bits - 1u) << 13) | ((mitad - 1u) << 18) | ((canal - 1u) << 24) | (shift << 29);
}
static void tx_conf(uint32_t c) { I2S(0x24) = c | (1u << 8); while (I2S(0x24) & (1u << 8)) {} I2S(0x24) = c | 3u; I2S(0x24) = c; }
static void rx_conf(uint32_t c) { I2S(0x20) = c | (1u << 8); while (I2S(0x20) & (1u << 8)) {} I2S(0x20) = c | 3u; I2S(0x20) = c; }
static void arranca_tx(uint32_t c) { I2S(0x24) = c | (1u << 2); }
static void arranca_rx(uint32_t c) { I2S(0x20) = c | (1u << 2); }
static int igual(const uint8_t *a, const uint8_t *b, unsigned n) {
    for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
/* El primer byte de rx[0..n) que no es cero */
static unsigned primero(unsigned n) {
    unsigned i = 0;
    while (i < n && rx[i] == 0) ++i;
    return i & ~3u;
}
static unsigned unos16(uint16_t v) { unsigned k = 0; while (v) { k += v & 1u; v >>= 1; } return k; }

#define TDM   (1u << 19)
#define PDM   (1u << 20)
#define BYP   (1u << 12)
#define IZQ   (1u << 15)
#define STOP  (1u << 13)
#define ESCL  (1u << 3)
#define LOOP  (1u << 27)
#define FILL  (1u << 16)
#define MONO  (1u << 5)

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    for (unsigned p = 0; p <= 4; ++p) if (p != 1) IO_MUX(p) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    I2S(0x18) = 0xF;
    I2S(0x60) = 0x810u;                               /* HUNG: 16 periodos de I2S_CLK */

    /* A: RX maestro (BCK 2 MHz) y TX esclavo */
    relojes(10, 10);
    GPIO_FUNC_OUT(0) = 16; GPIO_FUNC_OUT(2) = 17; GPIO_FUNC_OUT(3) = 15;
    GPIO_FUNC_IN(13) = 0 | (1u << 6); GPIO_FUNC_IN(14) = 2 | (1u << 6); GPIO_FUNC_IN(15) = 3 | (1u << 6);
    {
        const uint32_t c1 = conf1(16, 8, 16, 16, 16, 1);
        I2S(0x2C) = c1; I2S(0x28) = c1;
        I2S(0x54) = 3u | (1u << 16); I2S(0x50) = 3u | (1u << 16);
        const uint32_t txc = TDM | BYP | IZQ | STOP | ESCL, rxc = TDM | BYP | IZQ;
        tx_conf(txc); rx_conf(rxc);
        for (unsigned i = 0; i < 64; ++i) tx[i] = (uint8_t)(0x11 + 7 * i);
        I2S(0x64) = 64;
        gdma_rx(3, 64);
        gdma_tx(64);
        arranca_tx(txc);                              /* espera el BCK del maestro */
        arranca_rx(rxc);
        espera_eof(2, 3000);
        I2S(0x20) = rxc;                              /* sin BCK: el esclavo se cuelga */
        espera_us(20);
        if (I2S(0x0C) & 8u) ok |= 1u << 1;
        I2S(0x24) = txc;
        para_rx();
        const unsigned d = primero(128);
        buzon.res[2] = d;
        if (d + 64 <= 192 && igual(rx + d, tx, 64)) ok |= 1u << 0;
    }

    /* B: TX maestro y RX esclavo; MSB, 32 bits de canal, 24 de dato */
    GPIO_FUNC_OUT(0) = 13; GPIO_FUNC_OUT(2) = 14; GPIO_FUNC_OUT(3) = 15;
    GPIO_FUNC_IN(16) = 0 | (1u << 6); GPIO_FUNC_IN(17) = 2 | (1u << 6); GPIO_FUNC_IN(15) = 3 | (1u << 6);
    I2S(0x18) = 0xF;
    {
        const uint32_t c1 = conf1(32, 8, 24, 32, 32, 0);
        I2S(0x2C) = c1; I2S(0x28) = c1;
        const uint32_t txc = TDM | BYP | IZQ | STOP | FILL, rxc = TDM | BYP | IZQ | FILL | ESCL;
        tx_conf(txc); rx_conf(rxc);
        for (unsigned i = 0; i < 16; ++i) {
            const uint32_t v = (0x123456u * (i + 1)) & 0xFFFFFFu;
            tx[4 * i] = (uint8_t)v; tx[4 * i + 1] = (uint8_t)(v >> 8); tx[4 * i + 2] = (uint8_t)(v >> 16); tx[4 * i + 3] = 0;
        }
        I2S(0x64) = 32;
        gdma_rx(2, 64);
        gdma_tx(64);
        arranca_rx(rxc);
        arranca_tx(txc);
        espera_eof(1, 3000);
        while (!(I2S(0x6C) & 1u)) {}                  /* el TX acaba */
        I2S(0x24) = txc;
        espera_us(20);
        if (I2S(0x0C) & 4u) ok |= 1u << 3;            /* RX_HUNG: sin BCK */
        I2S(0x20) = rxc;
        para_rx();
        /* Dónde empieza: el primer dato recibido, en lo mandado */
        unsigned d = 0;
        while (d < 64 && !igual(rx, tx + d, 4)) d += 4;
        buzon.res[4] = d;
        if (d <= 32 && igual(rx, tx + d, 32)) ok |= 1u << 2;
    }

    /* C: A-law y mu-law en SIG_LOOPBACK (BCK 4 MHz) */
    GPIO_FUNC_OUT(0) = 13; GPIO_FUNC_OUT(2) = 14; GPIO_FUNC_OUT(3) = 15;
    GPIO_FUNC_IN(15) = 3 | (1u << 6);
    for (unsigned k = 0; k < 4; ++k) {
        relojes(10, 10);
        const uint32_t c1 = conf1(16, 4, 16, 16, 16, 1);
        I2S(0x2C) = c1; I2S(0x28) = c1;
        /* k = 0: TX comprime A; 1: RX expande A; 2: TX comprime mu; 3: RX expande mu */
        const unsigned conf_tx = (k & 2u) ? 3u : 1u, conf_rx = (k & 2u) ? 2u : 0u;
        uint32_t txc = TDM | IZQ | STOP | LOOP, rxc = TDM | IZQ;
        const uint16_t *src, *esp;
        if (!(k & 1u)) { txc |= conf_tx << 10; rxc |= BYP; src = I2S_X; esp = (k & 2u) ? I2S_U : I2S_A; }
        else { txc |= BYP; rxc |= conf_rx << 10; src = (k & 2u) ? I2S_U : I2S_A; esp = (k & 2u) ? I2S_UX : I2S_AX; }
        tx_conf(txc); rx_conf(rxc);
        for (unsigned i = 0; i < 8; ++i) { tx[2 * i] = (uint8_t)src[i]; tx[2 * i + 1] = (uint8_t)(src[i] >> 8); }
        I2S(0x64) = 16;
        gdma_rx(1, 16);
        gdma_tx(16);
        arranca_rx(rxc);
        arranca_tx(txc);
        espera_eof(1, 1000);
        while (!(I2S(0x6C) & 1u)) {}
        I2S(0x24) = txc; I2S(0x20) = rxc;
        int bien = 1;
        for (unsigned i = 0; i < 8; ++i) if ((uint16_t)(rx[2 * i] | rx[2 * i + 1] << 8) != esp[i]) bien = 0;
        if (bien) ok |= 1u << (4 + k);
    }

    /* D: PDM en crudo, estéreo, 16 bits; el RX PDM en SIG_LOOPBACK */
    {
        relojes(10, 10);
        const uint32_t c1 = conf1(1, 8, 16, 16, 16, 0);
        I2S(0x2C) = c1; I2S(0x28) = c1;
        I2S(0x50) = 3u;
        const uint32_t txc = PDM | IZQ | STOP | LOOP | BYP, rxc = PDM | IZQ | BYP;
        tx_conf(txc); rx_conf(rxc);
        static const uint16_t w[4] = {0xA5C3, 0x0FF0, 0x1234, 0xFEDC};
        for (unsigned i = 0; i < 4; ++i) { tx[2 * i] = (uint8_t)w[i]; tx[2 * i + 1] = (uint8_t)(w[i] >> 8); }
        I2S(0x64) = 8;
        gdma_rx(1, 8);
        gdma_tx(8);
        arranca_rx(rxc);
        arranca_tx(txc);
        espera_eof(1, 1000);
        while (!(I2S(0x6C) & 1u)) {}
        I2S(0x24) = txc; I2S(0x20) = rxc;
        if (igual(rx, tx, 8)) ok |= 1u << 8;
    }

    /* E: PCM a PDM, mono: dos muestras de 0,5 y dos de -0,5, 32 periodos de
     * WS cada una; el RX PDM guarda solo el canal izquierdo */
    {
        I2S(0x40) = (I2S(0x40) & ~((1u << 25) | (0xFu << 1) | (1u << 24))) | (1u << 25) | (1u << 1);
        const uint32_t txc = PDM | IZQ | STOP | LOOP | BYP | MONO, rxc = PDM | IZQ | BYP;
        tx_conf(txc); rx_conf(rxc);
        I2S(0x50) = 1u;
        static const int16_t m[4] = {16384, 16384, -16384, -16384};
        for (unsigned i = 0; i < 4; ++i) { tx[2 * i] = (uint8_t)m[i]; tx[2 * i + 1] = (uint8_t)((uint16_t)m[i] >> 8); }
        I2S(0x64) = 16;
        gdma_rx(1, 16);
        gdma_tx(8);
        arranca_rx(rxc);
        arranca_tx(txc);
        espera_eof(1, 2000);
        while (!(I2S(0x6C) & 1u)) {}
        I2S(0x24) = txc; I2S(0x20) = rxc;
        I2S(0x40) &= ~(1u << 25);
        unsigned u1 = 0, u2 = 0;
        for (unsigned i = 0; i < 4; ++i) u1 += unos16((uint16_t)(rx[2 * i] | rx[2 * i + 1] << 8));
        for (unsigned i = 4; i < 8; ++i) u2 += unos16((uint16_t)(rx[2 * i] | rx[2 * i + 1] << 8));
        buzon.res[1] = u1 | (u2 << 16);
        if (u1 >= 44 && u1 <= 52) ok |= 1u << 9;      /* 64 x 0,75 = 48 */
        if (u2 >= 12 && u2 <= 20) ok |= 1u << 10;     /* 64 x 0,25 = 16 */
    }

    /* F: MCLK = I2S_CLK del TX (160 / 10 = 16 MHz) por GPIO4, y de vuelta
     * como reloj del RX (CLK_SEL = 3) */
    relojes(10, 10);
    GPIO_FUNC_OUT(4) = 12;
    GPIO_FUNC_IN(12) = 4 | (1u << 6);
    I2S(0x30) = 2u | (1u << 26) | (3u << 27);         /* RX: I2S_MCLK_in / 2 */
    buzon.res[3] = 6;
    while (buzon.res[3] == 6) {}                       /* el banco mide */
    GPIO_FUNC_OUT(4) = 128;
    I2S(0x30) = 10u | (1u << 26) | (2u << 27);

    /* G: TX_TIMING: SD_OUT_DM = 1 y WS_OUT_DM = 2; BCK 2 MHz, Philips */
    {
        const uint32_t c1 = conf1(16, 8, 16, 16, 16, 1);
        I2S(0x2C) = c1; I2S(0x28) = c1;
        I2S(0x54) = 3u | (1u << 16); I2S(0x50) = 3u | (1u << 16);
        I2S(0x5C) = 1u | (2u << 16);
        const uint32_t txc = TDM | BYP | IZQ | STOP | LOOP, rxc = TDM | BYP | IZQ;
        tx_conf(txc); rx_conf(rxc);
        for (unsigned i = 0; i < 64; ++i) tx[i] = (uint8_t)(0x5A ^ (13 * i));
        I2S(0x64) = 64;
        gdma_rx(1, 64);
        gdma_tx(64);
        arranca_rx(rxc);
        buzon.res[3] = 7;
        arranca_tx(txc);
        espera_eof(1, 3000);
        while (!(I2S(0x6C) & 1u)) {}
        I2S(0x24) = txc; I2S(0x20) = rxc;
        I2S(0x5C) = 0;
        buzon.res[3] = 8;
        if (igual(rx, tx, 64)) ok |= 1u << 11;
    }

    /* H: cuatro canales, el 1 y el 3 apagados; la memoria trae los cuatro
     * (SKIP_MSK) y los apagados mandan CONF_SIGLE_DATA; RX_STOP_MODE = 1 */
    {
        const uint32_t c1 = conf1(1, 4, 16, 32, 16, 1);
        I2S(0x2C) = c1; I2S(0x28) = c1;
        I2S(0x54) = 5u | (3u << 16) | (1u << 20);
        I2S(0x50) = 0xFu | (3u << 16);
        I2S(0x68) = 0xABCDu;
        const uint32_t txc = TDM | BYP | IZQ | STOP | LOOP, rxc = TDM | BYP | IZQ | (1u << 13);
        tx_conf(txc); rx_conf(rxc);
        for (unsigned i = 0; i < 32; ++i) { tx[2 * i] = (uint8_t)(i + 1); tx[2 * i + 1] = (uint8_t)(0x40 + i); }
        I2S(0x64) = 16;
        gdma_rx(2, 16);
        gdma_tx(64);
        arranca_rx(rxc);
        arranca_tx(txc);
        espera_eof(1, 2000);
        while (!(I2S(0x6C) & 1u)) {}
        espera_us(20);
        I2S(0x24) = txc; I2S(0x20) = rxc;
        int bien = 1;
        for (unsigned f = 0; f < 2; ++f)
            for (unsigned c = 0; c < 4; ++c) {
                const uint16_t v = (uint16_t)(rx[8 * f + 2 * c] | rx[8 * f + 2 * c + 1] << 8);
                const unsigned j = 4 * f + c;
                const uint16_t e = (c & 1u) ? 0xABCDu : (uint16_t)(tx[2 * j] | tx[2 * j + 1] << 8);
                if (v != e) bien = 0;
            }
        if (bien) ok |= 1u << 12;
        if ((drx[1].w0 >> 31) && rx[64] == 0xEE) ok |= 1u << 13;
    }

    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
