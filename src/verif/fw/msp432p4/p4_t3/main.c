/* ===========================================================================
 * p4_t3 — El firmware de verificacion de la fase T3 del MSP432P401R
 *
 * Como p4_demo: la cabecera de TI y nada mas, cada registro por su nombre, y
 * un solo binario con MODOS que el banco escribe en el buzon antes de soltar
 * el reset. Un grupo de modos por periferico:
 *
 *   1-7    eUSCI_A en UART (el backchannel de la LaunchPad, en P1.2/P1.3)
 *     1  eco a 115200 8N1 con SMCLK = 12 MHz, por interrupcion y UCA0IV
 *     2  9600 7E2 con SMCLK = 3 MHz; y los errores de trama y paridad con
 *        UCRXEIE = 0, que NO llegan a RXBUF
 *     3  desbordamiento y break en los dos sentidos
 *     4  UCLISTEN a 1 Mbaudio: 64 caracteres por dentro, y el orden de las
 *        banderas en el IV (STT, RX, TX, TXCPT)
 *     5  multiprocesador por bit de direccion, con UCDORM
 *     6  multiprocesador por linea ociosa, con UCDORM
 *     7  el PMAP: la UCA1 en sus pines de reset (P2.2/P2.3) y luego la UCA2
 *        mapeada en P2.3
 *
 * El buzon: modo, arranques, estado y un vector de resultados r[] cuyo
 * significado depende del modo (lo dice cada modo).
 * ===========================================================================*/
#ifndef NO_MSP_CLASSIC_DEFINES
#define NO_MSP_CLASSIC_DEFINES
#endif
#include "msp432p401r.h"

uint32_t SystemCoreClock = 3000000;
void SystemInit(void) {}

typedef struct {
    volatile uint32_t modo;              /* 0x00 lo pone el banco             */
    volatile uint32_t arranques;         /* 0x04                              */
    volatile uint32_t estado;            /* 0x08 hasta donde se ha llegado    */
    volatile uint32_t senal;             /* 0x0C lo escribe el banco          */
    volatile uint32_t r[60];             /* 0x10 resultados                   */
    volatile uint8_t  buf[256];          /* 0x100 datos                       */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static void para_perro(void) { WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD; }

static void dco(uint32_t rsel) {
    CS->KEY = CS_KEY_VAL;
    CS->CTL0 = rsel;                    /* DCOTUNE = 0: el centro exacto */
    CS->KEY = 0;
}
static void espera_senal(uint32_t v) { while (buzon.senal != v) { } }

/* --- UART ------------------------------------------------------------------ */

static void uart_init(EUSCI_A_Type* u, uint16_t ctlw0, uint16_t brw, uint16_t mctlw) {
    u->CTLW0 = EUSCI_A_CTLW0_SWRST;
    u->CTLW0 = EUSCI_A_CTLW0_SWRST | ctlw0;
    u->BRW = brw;
    u->MCTLW = mctlw;
    u->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
}
static void uart_pon(EUSCI_A_Type* u, uint8_t c) {
    while (!(u->IFG & EUSCI_A_IFG_TXIFG)) { }
    u->TXBUF = c;
}
static void uart_texto(EUSCI_A_Type* u, const char* s) { while (*s) uart_pon(u, (uint8_t)*s++); }
static void uart_espera_fin(EUSCI_A_Type* u) { while (u->STATW & EUSCI_A_STATW_BUSY) { } }

/* 115200 con BRCLK = 12 MHz: N = 104,17 -> UCOS16, BR = 6, BRF = 8, BRS = 0x20
 * (la tabla de baudios de la guia de usuario) */
#define M_115200_12M  (EUSCI_A_MCTLW_OS16 | (8u << EUSCI_A_MCTLW_BRF_OFS) | (0x20u << EUSCI_A_MCTLW_BRS_OFS))
/* 9600 con 3 MHz: N = 312,5 -> BR = 19, BRF = 8, BRS = 0xAA */
#define M_9600_3M     (EUSCI_A_MCTLW_OS16 | (8u << EUSCI_A_MCTLW_BRF_OFS) | (0xAAu << EUSCI_A_MCTLW_BRS_OFS))

static void pines_uca0(void) {
    P1->SEL0 |= BIT2 | BIT3;            /* UCA0RXD, UCA0TXD: primarias */
    P1->SEL1 &= ~(BIT2 | BIT3);
}

/* Modo 1: el eco, por interrupcion */
static volatile uint32_t eco_n = 0;
void EUSCIA0_IRQHandler(void) {
    switch (EUSCI_A0->IV) {
    case 0x02: {                        /* RXIFG */
        uint8_t c = (uint8_t)EUSCI_A0->RXBUF;
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        buzon.buf[eco_n & 0xFF] = c;
        eco_n++;
        buzon.r[0] = eco_n;
        EUSCI_A0->TXBUF = c;            /* hay sitio: un caracter cada vez */
        break;
    }
    default:
        buzon.r[1]++;                   /* nada mas deberia llegar */
        break;
    }
}
static void m1_eco(void) {
    dco(CS_CTL0_DCORSEL_3);             /* 12 MHz */
    pines_uca0();
    uart_init(EUSCI_A0, EUSCI_A_CTLW0_SSEL__SMCLK, 6, M_115200_12M);
    uart_texto(EUSCI_A0, "Hola desde el MSP432\r\n");
    uart_espera_fin(EUSCI_A0);
    buzon.r[2] = EUSCI_A0->IFG;         /* TXIFG y TXCPTIFG */
    EUSCI_A0->IE = EUSCI_A_IE_RXIE;
    NVIC->ISER[0] = 1u << EUSCIA0_IRQn;
    buzon.estado = 1;
    for (;;) { __WFI(); }
}

/* Modo 2: 9600 7E2, y los errores */
static void m2_formato(void) {
    pines_uca0();
    uart_init(EUSCI_A0, EUSCI_A_CTLW0_SSEL__SMCLK | EUSCI_A_CTLW0_SEVENBIT |
              EUSCI_A_CTLW0_PEN | EUSCI_A_CTLW0_PAR | EUSCI_A_CTLW0_SPB, 19, M_9600_3M);
    uart_texto(EUSCI_A0, "7E2");
    uart_espera_fin(EUSCI_A0);
    buzon.estado = 1;
    /* Dos caracteres buenos: eco */
    for (int k = 0; k < 2; ++k) {
        while (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG)) { }
        uart_pon(EUSCI_A0, (uint8_t)EUSCI_A0->RXBUF);
    }
    buzon.estado = 2;
    /* Ahora el banco manda en 8N1: errores. Con UCRXEIE = 0 no hay RXIFG */
    uint32_t vueltas = 0;
    while (!(EUSCI_A0->STATW & EUSCI_A_STATW_RXERR)) { ++vueltas; }
    while (EUSCI_A0->STATW & EUSCI_A_STATW_BUSY) { }
    buzon.r[0] = EUSCI_A0->STATW;
    buzon.r[1] = EUSCI_A0->IFG;
    (void)EUSCI_A0->RXBUF;              /* leerlo borra los errores */
    buzon.r[2] = EUSCI_A0->STATW;
    /* Y con UCRXEIE = 1 el caracter con error SI llega */
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_RXEIE;
    EUSCI_A0->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
    buzon.estado = 3;
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG)) { }
    buzon.r[3] = EUSCI_A0->STATW;
    buzon.r[4] = EUSCI_A0->RXBUF;
    buzon.estado = 4;
    for (;;) { }
}

/* Modo 3: desbordamiento y break */
static void m3_errores(void) {
    dco(CS_CTL0_DCORSEL_3);
    pines_uca0();
    uart_init(EUSCI_A0, EUSCI_A_CTLW0_SSEL__SMCLK | EUSCI_A_CTLW0_BRKIE, 6, M_115200_12M);
    buzon.estado = 1;
    espera_senal(1);                    /* el banco ya ha mandado "123" */
    buzon.r[0] = EUSCI_A0->STATW;       /* OE */
    buzon.r[1] = EUSCI_A0->RXBUF;       /* el ultimo */
    buzon.r[2] = EUSCI_A0->STATW;       /* y leer RXBUF lo borra */
    buzon.estado = 2;
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG)) { }   /* el break, con UCBRKIE */
    buzon.r[3] = EUSCI_A0->STATW;
    buzon.r[4] = EUSCI_A0->RXBUF;
    /* Un break de vuelta: UCTXBRK y un 0 en TXBUF */
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_TXBRK;
    uart_pon(EUSCI_A0, 0);
    uart_espera_fin(EUSCI_A0);
    buzon.r[5] = EUSCI_A0->CTLW0 & EUSCI_A_CTLW0_TXBRK;   /* se borra solo */
    uart_pon(EUSCI_A0, 'K');
    uart_espera_fin(EUSCI_A0);
    buzon.estado = 3;
    for (;;) { }
}

/* Modo 4: por dentro, a 1 Mbaudio, y el orden del IV */
static volatile uint32_t m4_iv[16];
static volatile uint32_t m4_niv = 0;
static void m4_listen(void) {
    dco(CS_CTL0_DCORSEL_3);
    EUSCI_A2->CTLW0 = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A2->BRW = 12;
    EUSCI_A2->MCTLW = 0;
    EUSCI_A2->STATW = EUSCI_A_STATW_LISTEN;
    EUSCI_A2->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
    uint32_t malos = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        EUSCI_A2->TXBUF = (uint8_t)(i * 3 + 1);
        while (!(EUSCI_A2->IFG & EUSCI_A_IFG_RXIFG)) { }
        if ((uint8_t)EUSCI_A2->RXBUF != (uint8_t)(i * 3 + 1)) ++malos;
    }
    buzon.r[0] = malos;
    /* Un caracter mas, mirando el IV en cada paso: todas las banderas a la
     * vez y leidas en orden de prioridad */
    while (EUSCI_A2->STATW & EUSCI_A_STATW_BUSY) { }
    EUSCI_A2->IFG = 0;
    EUSCI_A2->IE = EUSCI_A_IE_RXIE | EUSCI_A_IE_TXIE | EUSCI_A_IE_STTIE | EUSCI_A_IE_TXCPTIE;
    EUSCI_A2->TXBUF = 0x5A;
    while (EUSCI_A2->STATW & EUSCI_A_STATW_BUSY) { }
    for (;;) {
        uint32_t v = EUSCI_A2->IV;
        if (!v || m4_niv >= 16) break;
        m4_iv[m4_niv++] = v;
    }
    buzon.r[1] = m4_niv;
    for (uint32_t i = 0; i < m4_niv; ++i) buzon.r[2 + i] = m4_iv[i];
    buzon.r[10] = EUSCI_A2->RXBUF;
    buzon.estado = 1;
    for (;;) { }
}

/* Modos 5 y 6: multiprocesador, por dentro */
static void mp(uint16_t modo) {
    dco(CS_CTL0_DCORSEL_3);
    EUSCI_A2->CTLW0 = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_SSEL__SMCLK | modo | EUSCI_A_CTLW0_DORM;
    EUSCI_A2->BRW = 12;
    EUSCI_A2->MCTLW = 0;
    EUSCI_A2->STATW = EUSCI_A_STATW_LISTEN;
    EUSCI_A2->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
    uint32_t n = 0;
    /* Un dato que no es direccion, dormido: no llega */
    EUSCI_A2->TXBUF = 0x11;
    while (EUSCI_A2->STATW & EUSCI_A_STATW_BUSY) { }
    buzon.r[n++] = EUSCI_A2->IFG & EUSCI_A_IFG_RXIFG;           /* 0 */
    /* Una direccion: llega, con UCADDR/UCIDLE */
    EUSCI_A2->CTLW0 |= EUSCI_A_CTLW0_TXADDR;
    EUSCI_A2->TXBUF = 0x42;
    while (EUSCI_A2->STATW & EUSCI_A_STATW_BUSY) { }
    buzon.r[n++] = EUSCI_A2->IFG & EUSCI_A_IFG_RXIFG;           /* 1 */
    buzon.r[n++] = EUSCI_A2->STATW & EUSCI_A_STATW_ADDR_IDLE;   /* 2 */
    buzon.r[n++] = EUSCI_A2->RXBUF;                             /* 0x42 */
    buzon.r[n++] = EUSCI_A2->CTLW0 & EUSCI_A_CTLW0_TXADDR;      /* 0: se borra solo */
    /* Despierto: los datos llegan, sin la marca de direccion. En linea
     * ociosa hace falta que el dato vaya PEGADO a la direccion: se escribe
     * mientras sale la anterior */
    EUSCI_A2->CTLW0 &= ~EUSCI_A_CTLW0_DORM;
    EUSCI_A2->CTLW0 |= EUSCI_A_CTLW0_TXADDR;
    EUSCI_A2->TXBUF = 0x43;
    while (!(EUSCI_A2->IFG & EUSCI_A_IFG_TXIFG)) { }
    EUSCI_A2->TXBUF = 0x55;
    while (!(EUSCI_A2->IFG & EUSCI_A_IFG_RXIFG)) { }
    buzon.r[n++] = EUSCI_A2->RXBUF;                             /* 0x43 */
    while (EUSCI_A2->STATW & EUSCI_A_STATW_BUSY) { }
    buzon.r[n++] = EUSCI_A2->IFG & EUSCI_A_IFG_RXIFG;           /* 1 */
    buzon.r[n++] = EUSCI_A2->STATW & EUSCI_A_STATW_ADDR_IDLE;   /* 0 */
    buzon.r[n++] = EUSCI_A2->RXBUF;                             /* 0x55 */
    buzon.estado = 1;
    for (;;) { }
}

/* Modo 7: el PMAP */
static void m7_pmap(void) {
    dco(CS_CTL0_DCORSEL_3);
    /* La UCA1 en sus pines de reset: P2.2 = UCA1RXD, P2.3 = UCA1TXD */
    P2->SEL0 |= BIT2 | BIT3;
    P2->SEL1 &= ~(BIT2 | BIT3);
    uart_init(EUSCI_A1, EUSCI_A_CTLW0_SSEL__SMCLK, 6, M_115200_12M);
    uart_texto(EUSCI_A1, "A1");
    uart_espera_fin(EUSCI_A1);
    buzon.estado = 1;
    /* La UCA2 a P2.3, en lugar de la UCA1 */
    PMAP->KEYID = PMAP_KEYID_VAL;
    P2MAP->PMAP_REGISTER3 = PMAP_UCA2TXD;
    PMAP->KEYID = 0;
    buzon.r[0] = P2MAP->PMAP_REGISTER3;
    buzon.r[1] = PMAP->CTL & PMAP_CTL_LOCKED;
    /* Cerrado y sin PMAPRECFG: ya no se puede tocar */
    PMAP->KEYID = PMAP_KEYID_VAL;
    P2MAP->PMAP_REGISTER3 = PMAP_UCA1TXD;
    PMAP->KEYID = 0;
    buzon.r[2] = P2MAP->PMAP_REGISTER3;
    uart_init(EUSCI_A2, EUSCI_A_CTLW0_SSEL__SMCLK, 6, M_115200_12M);
    uart_texto(EUSCI_A2, "A2");
    uart_espera_fin(EUSCI_A2);
    uart_texto(EUSCI_A1, "no");          /* la UCA1 ya no tiene pin */
    uart_espera_fin(EUSCI_A1);
    buzon.estado = 2;
    for (;;) { }
}

int main(void) {
    para_perro();
    buzon.arranques++;
    switch (buzon.modo) {
    case 1: m1_eco(); break;
    case 2: m2_formato(); break;
    case 3: m3_errores(); break;
    case 4: m4_listen(); break;
    case 5: mp(EUSCI_A_CTLW0_MODE_2); break;
    case 6: mp(EUSCI_A_CTLW0_MODE_1); break;
    case 7: m7_pmap(); break;
    default: break;
    }
    for (;;) { }
}
