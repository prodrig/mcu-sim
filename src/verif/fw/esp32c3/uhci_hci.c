/* uhci_hci.c — E8: lo que quedaba del UHCI, con la UART1 en LOOPBACK a
 * 1 Mbaud como uhci.c: la cabecera de HCI (H5), el CRC, el ACK, el envío
 * rápido, WAIT_SW_START y HUNG. La GDMA manda por el canal 0 y recibe por el
 * 1 (periférico 2). Los vectores, de vec/gen_hci.py (hci_vec.h).
 *   D  una trama fiable con DIC y la suma y el ack mal: el UHCI los arregla,
 *      añade el CRC y la recibe con SAVE_HEAD (14 bytes), RX_HEAD y ACK_NUM 1
 *   E  la misma otra vez: seq 0 cuando se espera 1, RX_ERR_CAUSE 2, err_eof
 *   F  DIC sin ENCODE_CRC_EN y dos bytes de más: OUTLINK_EOF_ERR y, al
 *      recibir, RX_ERR_CAUSE 6 (CRC); con CRC_DISABLE se acepta
 *   G  sin TX_CHECK_SUM_RE y la suma mal: RX_ERR_CAUSE 1
 *   H  el envío rápido único (Q0): SEND_S_REG_Q y SINGLE_SEND_EN se borra
 *   I  el envío rápido siempre (Q1): tres tramas y SEND_A_REG_Q
 *   J  WAIT_SW_START: ENCODE_STATE 7 y nada sale hasta SW_START
 *   K  HUNG: una trama sin EOF da TX_HUNG; lo recibido sin canal de entrada,
 *      RX_HUNG, y llega (con el EOF por reposo) al arrancar el canal
 *   res[0]  bits: 0 D datos y descriptor; 1 D RX_HEAD y ACK_NUM; 2 E; 3 F
 *           OUTLINK_EOF_ERR; 4 F causa 6; 5 F con CRC_DISABLE; 6 G; 7 H; 8 I;
 *           9 J espera; 10 J sale; 11 K TX_HUNG; 12 K RX_HUNG; 13 K los datos
 *   res[1]  RX_HEAD de D;  res[2]  STATE0 tras E;  res[3]  DW0 de E;
 *   res[4]  DW0 de K */
#include "c3.h"
#include "hci_vec.h"
#define UHCI(o)  R(0x60014000u + (o))
#define GDMA(o)  R(0x6003F000u + (o))
#define U1 1
#define C0_BASE  ((1u << 11) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 9))  /* CLK_EN, UART1_CE, SEPER, HEAD, CRC_REC, LEN_EOF */
#define ENC_CRC  (1u << 10)

typedef struct { volatile uint32_t w0; volatile uint32_t buf; volatile uint32_t sig; } dscr_t;
static uint8_t tx[64], rx[256];
static dscr_t dtx, drx[4];

static uint32_t a20(const volatile void *p) { return (uint32_t)p & 0xFFFFFu; }
static void arranca_tx(const uint8_t *p, unsigned n, int eof) {
    dtx.w0 = n | (n << 12) | (eof ? (1u << 30) : 0u) | (1u << 31);
    dtx.buf = (uint32_t)p; dtx.sig = 0;
    GDMA(0x0C) = 0x1FFF;
    GDMA(0xE0) = a20(&dtx) | (1u << 21);
}
static void manda(const uint8_t *p, unsigned n) {
    arranca_tx(p, n, 1);
    while (!(GDMA(0x00) & (1u << 8))) {}             /* OUT_TOTAL_EOF */
}
static void prepara_rx(unsigned n, unsigned tam, int arranca) {
    for (unsigned i = 0; i < sizeof rx; ++i) rx[i] = 0xEE;
    for (unsigned i = 0; i < n; ++i) {
        drx[i].w0 = tam | (1u << 31);
        drx[i].buf = (uint32_t)(rx + 64 * i);
        drx[i].sig = i + 1 < n ? (uint32_t)&drx[i + 1] : 0;
    }
    GDMA(0x1C) = 0x1FFF;
    if (arranca) GDMA(0x140) = a20(&drx[0]) | (1u << 22);
}
/* Espera a que el canal 1 levante alguno de `mascara` (IN_SUC_EOF 2, IN_ERR_EOF 4) */
static int espera_in(uint32_t mascara) {
    uint64_t lim = systimer_ahora() + 2000u * 16u;
    while (systimer_ahora() < lim) if (GDMA(0x10) & mascara) return 1;
    return 0;
}
static int igual(const uint8_t *a, const uint8_t *b, unsigned n) {
    for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void espera_linea(void) { espera_us(150); }

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    GPIO_FUNC_IN(9) = 0x1Eu | (1u << 6);              /* U1RXD a 1 */
    UART_CLK_CONF(U1) = (UART_CLK_CONF(U1) & ~((3u << 20) | (0xFFu << 12))) | (1u << 20);
    UART_CLKDIV(U1) = 80u;
    UART_CONF0(U1) = (3u << 2) | (1u << 4) | (1u << 14);
    UART_ID(U1) |= 1u << 31;
    while (UART_ID(U1) & (1u << 31)) {}
    /* Hasta enrutar U1RXD, la entrada de la UART1 ha estado a 0 desde el
     * reset: lo que haya empezado a recibir con el reloj de reset, que acabe */
    espera_us(500);
    GDMA(0x100) = 2;
    GDMA(0x160) = 2;
    UHCI(0x00) = (1u << 11);
    UHCI(0x00) = C0_BASE | ENC_CRC;
    UHCI(0x14) = 0x33u | (1u << 3);                   /* lo de reset y SAVE_HEAD */
    UHCI(0x10) = 0x1FF;

    /* D */
    for (unsigned i = 0; i < sizeof HCI_D_TX; ++i) tx[i] = HCI_D_TX[i];
    prepara_rx(1, 64, 1);
    manda(tx, sizeof HCI_D_TX);
    espera_in(2);
    if (igual(rx, HCI_D_CAB, 4) && igual(rx + 4, HCI_D_TX + 4, 10) &&
        drx[0].w0 == (64u | (14u << 12) | (1u << 30))) ok |= 1u << 0;
    buzon.res[1] = UHCI(0x2C);
    if (UHCI(0x2C) == (HCI_D_CAB[0] | HCI_D_CAB[1] << 8 | HCI_D_CAB[2] << 16 | (uint32_t)HCI_D_CAB[3] << 24) &&
        (UHCI(0x28) & 7u) == 1u) ok |= 1u << 1;
    espera_linea();

    /* E: la misma; ahora se espera seq 1 */
    prepara_rx(1, 64, 1);
    manda(tx, sizeof HCI_D_TX);
    espera_in(4);
    buzon.res[2] = UHCI(0x18);
    buzon.res[3] = drx[0].w0;
    if ((GDMA(0x10) & 4u) && !(GDMA(0x10) & 2u) && (drx[0].w0 & (1u << 28)) && (UHCI(0x18) & 7u) == 2u &&
        GDMA(0x14C) == (uint32_t)&drx[0]) ok |= 1u << 2;
    espera_linea();

    /* F: DIC sin ENCODE_CRC_EN; los dos últimos bytes hacen de CRC (mal) */
    static const uint8_t f[10] = {0x40, 0x41, 0x00, 0x00, 1, 2, 3, 4, 0x12, 0x34};
    for (unsigned i = 0; i < 10; ++i) tx[i] = f[i];
    UHCI(0x00) = C0_BASE;
    UHCI(0x10) = 0x1FF;
    prepara_rx(1, 64, 1);
    manda(tx, 10);
    espera_in(6);
    if (UHCI(0x04) & (1u << 6)) ok |= 1u << 3;
    if ((GDMA(0x10) & 4u) && (UHCI(0x18) & 7u) == 6u) ok |= 1u << 4;
    espera_linea();
    UHCI(0x14) = 0x33u | (1u << 3) | (1u << 2);       /* CRC_DISABLE */
    prepara_rx(1, 64, 1);
    manda(tx, 10);
    espera_in(6);
    if ((GDMA(0x10) & 2u) && !(GDMA(0x10) & 4u) && drx[0].w0 == (64u | (8u << 12) | (1u << 30)) &&
        igual(rx + 4, f + 4, 4)) ok |= 1u << 5;
    espera_linea();

    /* G: la suma mal y sin TX_CHECK_SUM_RE */
    static const uint8_t g[6] = {0x00, 0x21, 0x00, 0x00, 0xAB, 0xCD};
    for (unsigned i = 0; i < 6; ++i) tx[i] = g[i];
    UHCI(0x14) = 0x23u | (1u << 3);
    prepara_rx(1, 64, 1);
    manda(tx, 6);
    espera_in(6);
    if ((GDMA(0x10) & 4u) && (UHCI(0x18) & 7u) == 1u) ok |= 1u << 6;
    espera_linea();

    /* H: el rápido único, Q0 */
    UHCI(0x14) = 0x33u | (1u << 3);
    UHCI(0x00) = C0_BASE | ENC_CRC;
    UHCI(0x34) = 0x00003100u;                         /* tipo 1, largo 3; ack y suma, del UHCI */
    UHCI(0x38) = 0x00CCBBAAu;
    UHCI(0x10) = 0x1FF;
    prepara_rx(1, 64, 1);
    UHCI(0x30) = (0u) | (1u << 3);
    espera_in(2);
    static const uint8_t h3[3] = {0xAA, 0xBB, 0xCC};
    if ((UHCI(0x04) & (1u << 4)) && !(UHCI(0x30) & (1u << 3)) && igual(rx, HCI_H_CAB, 4) && igual(rx + 4, h3, 3) &&
        drx[0].w0 == (64u | (7u << 12) | (1u << 30))) ok |= 1u << 7;
    espera_linea();

    /* I: siempre, Q1 (solo la cabecera, tipo 3), hasta tener tres */
    UHCI(0x3C) = 0x00000300u;
    UHCI(0x10) = 0x1FF;
    prepara_rx(4, 16, 1);
    UHCI(0x30) = (1u << 4) | (1u << 7);
    {
        unsigned k = 0;
        uint64_t lim = systimer_ahora() + 2000u * 16u;
        while (k < 3 && systimer_ahora() < lim) if (GDMA(0x10) & 2u) { GDMA(0x1C) = 2u; ++k; }
        UHCI(0x30) = 0;
        espera_linea();
        GDMA(0x80 + 0xC0) = 1u << 21;                 /* para el canal de entrada */
        int bien = k == 3 && (UHCI(0x04) & (1u << 5));
        for (unsigned i = 0; i < 3; ++i)
            bien = bien && drx[i].w0 == (16u | (4u << 12) | (1u << 30)) && rx[64 * i + 1] == 0x03;
        if (bien) ok |= 1u << 8;
    }

    /* J: WAIT_SW_START */
    static const uint8_t j[6] = {0x00, 0x24, 0x00, 0x00, 0x5A, 0xA5};
    for (unsigned i = 0; i < 6; ++i) tx[i] = j[i];
    UHCI(0x14) = 0x33u | (1u << 3) | (1u << 7);
    prepara_rx(1, 64, 1);
    arranca_tx(tx, 6, 1);
    espera_us(100);
    if ((UHCI(0x1C) & 7u) == 7u && (drx[0].w0 >> 31) && rx[0] == 0xEE) ok |= 1u << 9;
    UHCI(0x14) = 0x33u | (1u << 3) | (1u << 7) | (1u << 8);
    espera_in(2);
    if (!(UHCI(0x14) & (1u << 8)) && rx[4] == 0x5A && rx[5] == 0xA5 && drx[0].w0 == (64u | (6u << 12) | (1u << 30)))
        ok |= 1u << 10;
    espera_linea();

    /* K: HUNG, sin separadores ni cabecera y con el final por reposo */
    UHCI(0x14) = 0x33u;
    UHCI(0x00) = (1u << 11) | (1u << 3) | (1u << 8);
    UHCI(0x10) = 0x1FF;
    static const uint8_t k3[3] = {0x31, 0x32, 0x33};
    for (unsigned i = 0; i < 3; ++i) tx[i] = k3[i];
    prepara_rx(1, 64, 0);                             /* sin arrancar el canal de entrada */
    arranca_tx(tx, 3, 0);                             /* sin EOF ni siguiente */
    espera_us(400);
    if (UHCI(0x04) & (1u << 3)) ok |= 1u << 11;
    if (UHCI(0x04) & (1u << 2)) ok |= 1u << 12;
    GDMA(0x140) = a20(&drx[0]) | (1u << 22);
    espera_in(2);
    buzon.res[4] = drx[0].w0;
    if (igual(rx, k3, 3) && drx[0].w0 == (64u | (3u << 12) | (1u << 30))) ok |= 1u << 13;
    UHCI(0x00) = (1u << 11) | (1u << 3) | 3u;         /* TX_RST y RX_RST */
    UHCI(0x00) = (1u << 11) | (1u << 3);

    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
