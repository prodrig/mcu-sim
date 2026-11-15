/* uhci.c — E8: el UHCI con la UART1 en LOOPBACK a 1 Mbaud, como el driver
 * «UART DMA» de ESP-IDF (uhci.c): la GDMA manda por el canal 0 y recibe por
 * el 1 (periférico 2). Tres pruebas:
 *   A  100 bytes sin separadores; la trama acaba por reposo (UART_IDLE_EOF):
 *      un descriptor de 64 lleno y otro con 36 y suc_eof.
 *   B  SEPER_EN: 8 bytes con 0xC0, 0xDB y 0x11 salen como C0 01 DB DC 02 DB DD
 *      03 11 DB DC 04 C0 (el banco lo ve en la línea) y vuelven decodificados,
 *      con suc_eof en el separador del final.
 *   C  LEN_EOF_EN con PKT_THRS = 10 y reposo: 25 bytes en tramas de 10, 10 y 5.
 *   res[0]  bits: 0 A, los datos; 1 A, los descriptores; 2 B, los datos y el
 *           descriptor; 3 B, TX_START y RX_START; 4 C, los datos; 5 C, los
 *           descriptores; 6 la UART1 no ha metido nada en su FIFO; 7 DATE
 *   res[1..3]  las DW0 de los descriptores de C */
#include "c3.h"
#define UHCI(o)  R(0x60014000u + (o))
#define GDMA(o)  R(0x6003F000u + (o))
#define U1 1

typedef struct { volatile uint32_t w0; volatile uint32_t buf; volatile uint32_t sig; } dscr_t;
static uint8_t tx[128], rx[192];
static dscr_t dtx, drx[3];

static uint32_t a20(const volatile void *p) { return (uint32_t)p & 0xFFFFFu; }
static void manda(const uint8_t *p, unsigned n) {
    dtx.w0 = n | (n << 12) | (1u << 30) | (1u << 31);
    dtx.buf = (uint32_t)p; dtx.sig = 0;
    GDMA(0x0C) = 0x1FFF;
    GDMA(0xE0) = a20(&dtx) | (1u << 21);
    while (!(GDMA(0x00) & (1u << 8))) {}             /* OUT_TOTAL_EOF */
}
static void prepara_rx(unsigned n, unsigned tam) {
    for (unsigned i = 0; i < sizeof rx; ++i) rx[i] = 0xEE;
    for (unsigned i = 0; i < n; ++i) {
        drx[i].w0 = tam | (1u << 31);
        drx[i].buf = (uint32_t)(rx + 64 * i);
        drx[i].sig = i + 1 < n ? (uint32_t)&drx[i + 1] : 0;
    }
    GDMA(0x1C) = 0x1FFF;
    GDMA(0x140) = a20(&drx[0]) | (1u << 22);
}
static void espera_eof(unsigned cuantos) {
    unsigned k = 0;
    uint64_t lim = systimer_ahora() + 20000u * 16u;
    while (k < cuantos && systimer_ahora() < lim) {
        if (GDMA(0x10) & 2u) { GDMA(0x1C) = 2u; ++k; }
    }
}
static int igual(const uint8_t *a, const uint8_t *b, unsigned n) {
    for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    if (UHCI(0x80) == 0x2007170u) ok |= 1u << 7;
    /* U1RXD, a un 1 fijo (GPIO_MATRIX_CONST_ONE_INPUT): la línea en reposo,
     * como con el pin conectado; si no, la UART1 ve un break continuo */
    GPIO_FUNC_IN(9) = 0x1Eu | (1u << 6);
    /* La UART1: SCLK = APB, 1 Mbaud, 8N1, LOOPBACK */
    UART_CLK_CONF(U1) = (UART_CLK_CONF(U1) & ~((3u << 20) | (0xFFu << 12))) | (1u << 20);
    UART_CLKDIV(U1) = 80u;
    UART_CONF0(U1) = (3u << 2) | (1u << 4) | (1u << 14);
    UART_ID(U1) |= 1u << 31;
    while (UART_ID(U1) & (1u << 31)) {}
    /* La GDMA: canal 0 de salida y 1 de entrada, al UHCI (2) */
    GDMA(0x100) = 2;
    GDMA(0x160) = 2;
    /* A: sin separadores, la trama acaba por reposo */
    UHCI(0x00) = (1u << 11);                          /* uhci_ll_init: CLK_EN */
    UHCI(0x00) = (1u << 11) | (1u << 3) | (1u << 8);  /* UART1_CE, UART_IDLE_EOF_EN */
    for (unsigned i = 0; i < 100; ++i) tx[i] = (uint8_t)(i * 7 + 3);
    prepara_rx(2, 64);
    manda(tx, 100);
    espera_eof(1);
    if (igual(rx, tx, 100)) ok |= 1u << 0;
    if (drx[0].w0 == (64u | (64u << 12)) && drx[1].w0 == (64u | (36u << 12) | (1u << 30))) ok |= 1u << 1;
    /* B: SEPER_EN con los escapes de reset */
    static const uint8_t b8[8] = {0x01, 0xC0, 0x02, 0xDB, 0x03, 0x11, 0xC0, 0x04};
    UHCI(0x10) = 0x1FF;
    UHCI(0x00) = (1u << 11) | (1u << 3) | (1u << 5);
    for (unsigned i = 0; i < 8; ++i) tx[i] = b8[i];
    prepara_rx(1, 64);
    manda(tx, 8);
    espera_eof(1);
    if (igual(rx, b8, 8) && drx[0].w0 == (64u | (8u << 12) | (1u << 30))) ok |= 1u << 2;
    if ((UHCI(0x04) & 3u) == 3u) ok |= 1u << 3;
    /* C: por longitud (10) y por reposo */
    UHCI(0x00) = (1u << 11) | (1u << 3) | (1u << 8) | (1u << 9);
    UHCI(0x7C) = 10;
    for (unsigned i = 0; i < 25; ++i) tx[i] = (uint8_t)(0xA0 + i);
    prepara_rx(3, 16);
    manda(tx, 25);
    espera_eof(3);
    if (igual(rx, tx, 10) && igual(rx + 64, tx + 10, 10) && igual(rx + 128, tx + 20, 5)) ok |= 1u << 4;
    if (drx[0].w0 == (16u | (10u << 12) | (1u << 30)) && drx[1].w0 == (16u | (10u << 12) | (1u << 30)) &&
        drx[2].w0 == (16u | (5u << 12) | (1u << 30))) ok |= 1u << 5;
    for (unsigned i = 0; i < 3; ++i) buzon.res[1 + i] = drx[i].w0;
    if (((UART_STATUS(U1)) & 0x3FFu) == 0) ok |= 1u << 6;
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
