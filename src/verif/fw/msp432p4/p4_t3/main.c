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
 *   10-15  Timer_A y Timer32
 *    10  PWM en modo ascendente: TA0.1 y TA0.2 (P2.4 y P2.5, por el PMAP de
 *        reset) a 1 kHz, 25 % y 50 %; y las interrupciones TA0_0 y TA0_N
 *    11  modo continuo con dos bases de tiempo: TA1.1 y TA1.2 (P7.7 y P7.6)
 *        conmutando cada 1000 y 3000 tics, CCRn += paso en la interrupcion
 *    12  modo ascendente/descendente: PWM centrado en TA2.1 (P5.6)
 *    13  captura: TA3.1 en P10.5, los dos flancos, y COV; y la captura por
 *        programa (CCIS entre GND y VCC)
 *    14  Timer32: periodico, de un disparo, libre y BGLOAD
 *    15  TA1 con su reloj externo, TA1CLK en P7.2, e ID/IDEX
 *    16  un servo en TA0.2 (P2.5, el J2.19 de la BOOSTXL-EDUMKII): 50 Hz,
 *        1,5 ms y luego 2,0 ms, durmiendo entre medias
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

/* --- Timer_A y Timer32 ------------------------------------------------------- */
static volatile uint32_t n_ta0_0 = 0, n_ta0_n = 0, n_taifg = 0;
void TA0_0_IRQHandler(void) {
    TIMER_A0->CCTL[0] &= ~TIMER_A_CCTLN_CCIFG;   /* en el MSP432 no se borra solo */
    n_ta0_0++;
}
void TA0_N_IRQHandler(void) {
    switch (TIMER_A0->IV) {
    case 0x0E: n_taifg++; break;
    default: n_ta0_n++; break;
    }
}
static void m10_pwm(void) {
    /* P2.4 y P2.5: TA0.1 y TA0.2 por el PMAP de reset, salidas con DIR = 1 */
    P2->DIR |= BIT4 | BIT5;
    P2->SEL0 |= BIT4 | BIT5;
    P2->SEL1 &= ~(BIT4 | BIT5);
    TIMER_A0->CCR[0] = 2999;                     /* 3 MHz / 3000 = 1 kHz */
    TIMER_A0->CCR[1] = 749;
    TIMER_A0->CCR[2] = 1499;
    TIMER_A0->CCTL[1] = TIMER_A_CCTLN_OUTMOD_7;
    TIMER_A0->CCTL[2] = TIMER_A_CCTLN_OUTMOD_7;
    TIMER_A0->CCTL[0] = TIMER_A_CCTLN_CCIE;
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR |
                    TIMER_A_CTL_IE;
    NVIC->ISER[0] = (1u << TA0_0_IRQn) | (1u << TA0_N_IRQn);
    buzon.estado = 1;
    while (n_ta0_0 < 10) { }
    buzon.r[0] = n_ta0_0;
    buzon.r[1] = n_taifg;
    buzon.r[2] = n_ta0_n;
    buzon.estado = 2;
    for (;;) { }
}

static volatile uint32_t m11_n1 = 0, m11_n2 = 0;
void TA1_N_IRQHandler(void) {
    switch (TIMER_A1->IV) {
    case 0x02: TIMER_A1->CCR[1] += 1000; m11_n1++; break;
    case 0x04: TIMER_A1->CCR[2] += 3000; m11_n2++; break;
    default: break;
    }
}
static void m11_continuo(void) {
    P7->DIR |= BIT6 | BIT7;                      /* P7.7 = TA1.1, P7.6 = TA1.2 */
    P7->SEL0 |= BIT6 | BIT7;
    TIMER_A1->CCR[1] = 1000;
    TIMER_A1->CCR[2] = 3000;
    TIMER_A1->CCTL[1] = TIMER_A_CCTLN_OUTMOD_4 | TIMER_A_CCTLN_CCIE;
    TIMER_A1->CCTL[2] = TIMER_A_CCTLN_OUTMOD_4 | TIMER_A_CCTLN_CCIE;
    TIMER_A1->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__CONTINUOUS | TIMER_A_CTL_CLR;
    NVIC->ISER[0] = 1u << TA1_N_IRQn;
    buzon.estado = 1;
    while (m11_n1 < 60) { }
    buzon.r[0] = m11_n1;
    buzon.r[1] = m11_n2;
    buzon.estado = 2;
    for (;;) { }
}

static void m12_updown(void) {
    P5->DIR |= BIT6;                             /* P5.6 = TA2.1 */
    P5->SEL0 |= BIT6;
    TIMER_A2->CCR[0] = 1000;
    TIMER_A2->CCR[1] = 250;
    TIMER_A2->CCTL[1] = TIMER_A_CCTLN_OUTMOD_2;  /* conmuta en CCR1, a 0 en CCR0 */
    TIMER_A2->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UPDOWN | TIMER_A_CTL_CLR;
    buzon.estado = 1;
    /* Que TAIFG llega al pasar por cero */
    while (!(TIMER_A2->CTL & TIMER_A_CTL_IFG)) { }
    buzon.r[0] = TIMER_A2->R;
    for (;;) { }
}

static void m13_captura(void) {
    P10->DIR &= ~BIT5;                           /* P10.5 = TA3.1, entrada CCI1A */
    P10->SEL0 |= BIT5;
    TIMER_A3->CCTL[1] = TIMER_A_CCTLN_CAP | TIMER_A_CCTLN_CM__BOTH | TIMER_A_CCTLN_SCS |
                        TIMER_A_CCTLN_CCIS__CCIA;
    TIMER_A3->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__CONTINUOUS | TIMER_A_CTL_CLR;
    buzon.estado = 1;
    uint16_t c[5];
    for (int k = 0; k < 5; ++k) {
        while (!(TIMER_A3->CCTL[1] & TIMER_A_CCTLN_CCIFG)) { }
        c[k] = TIMER_A3->CCR[1];
        TIMER_A3->CCTL[1] &= ~TIMER_A_CCTLN_CCIFG;
    }
    for (int k = 0; k < 4; ++k) buzon.r[k] = (uint16_t)(c[k + 1] - c[k]);
    /* Sin borrar CCIFG, dos capturas mas: COV */
    while (!(TIMER_A3->CCTL[1] & TIMER_A_CCTLN_CCIFG)) { }
    uint16_t a = TIMER_A3->CCR[1];
    while (TIMER_A3->CCR[1] == a) { }
    buzon.r[4] = TIMER_A3->CCTL[1] & TIMER_A_CCTLN_COV;
    /* La captura por programa: CCIS de GND a VCC es un flanco de subida */
    TIMER_A3->CCTL[2] = TIMER_A_CCTLN_CAP | TIMER_A_CCTLN_CM__RISING | TIMER_A_CCTLN_CCIS__GND;
    uint16_t antes = TIMER_A3->R;
    TIMER_A3->CCTL[2] ^= TIMER_A_CCTLN_CCIS0;    /* GND -> VCC */
    uint16_t cap = TIMER_A3->CCR[2];
    buzon.r[5] = TIMER_A3->CCTL[2] & TIMER_A_CCTLN_CCIFG;
    buzon.r[6] = (uint16_t)(cap - antes);
    buzon.r[7] = TIMER_A3->CCTL[2] & TIMER_A_CCTLN_CCI;   /* VCC: 1 */
    buzon.estado = 2;
    for (;;) { }
}

static volatile uint32_t n_t32_1 = 0, n_t32_2 = 0, n_t32_c = 0;
void T32_INT1_IRQHandler(void) { TIMER32_1->INTCLR = 1; n_t32_1++; }
void T32_INT2_IRQHandler(void) { TIMER32_2->INTCLR = 1; n_t32_2++; }
void T32_INTC_IRQHandler(void) { n_t32_c++; }
static void m14_timer32(void) {
    buzon.r[0] = TIMER32_1->CONTROL;             /* el de reset: IE */
    /* T32_1: periodico, 32 bits, /1: LOAD = 29999 -> 10 ms a 3 MHz */
    TIMER32_1->LOAD = 29999;
    TIMER32_1->CONTROL = TIMER32_CONTROL_SIZE | TIMER32_CONTROL_MODE | TIMER32_CONTROL_IE |
                         TIMER32_CONTROL_ENABLE;
    /* T32_2: de un disparo, /16, 16 bits: 3749 -> 20 ms */
    TIMER32_2->LOAD = 3749;
    TIMER32_2->CONTROL = TIMER32_CONTROL_ONESHOT | TIMER32_CONTROL_PRESCALE_1 |
                         TIMER32_CONTROL_IE | TIMER32_CONTROL_ENABLE;
    NVIC->ISER[0] = (1u << T32_INT1_IRQn) | (1u << T32_INT2_IRQn);
    buzon.estado = 1;
    /* (la CPU ve los contadores avanzar a saltos de su cuanto, 1 us: entre
     * las dos lecturas tiene que pasar mas que eso) */
    uint32_t v0 = TIMER32_1->VALUE;
    for (volatile int k = 0; k < 20; ++k) { }
    uint32_t v1 = TIMER32_1->VALUE;
    buzon.r[1] = (v0 > v1);                      /* cuenta hacia abajo */
    buzon.r[9] = v0; buzon.r[10] = v1;
    while (n_t32_1 < 5) { }
    buzon.r[2] = n_t32_1;
    buzon.r[3] = n_t32_2;                        /* el de un disparo: una vez */
    buzon.r[4] = TIMER32_2->VALUE;               /* y se queda en cero */
    /* BGLOAD: la recarga cambia sin tocar la cuenta en curso */
    TIMER32_1->BGLOAD = 59999;
    uint32_t a = n_t32_1;
    while (n_t32_1 < a + 3) { }
    buzon.r[5] = TIMER32_1->LOAD;
    /* Libre, 16 bits: al pasar por cero da la vuelta a 0xFFFF */
    TIMER32_2->CONTROL = 0;
    TIMER32_2->LOAD = 100;
    TIMER32_2->CONTROL = TIMER32_CONTROL_ENABLE;  /* libre, 16 bits, sin IE */
    while (!TIMER32_2->RIS) { }
    buzon.r[6] = TIMER32_2->VALUE;
    buzon.r[7] = TIMER32_2->MIS;                 /* sin IE: 0 */
    NVIC->ISER[0] = 1u << T32_INTC_IRQn;
    TIMER32_2->CONTROL |= TIMER32_CONTROL_IE;     /* la combinada */
    while (n_t32_c == 0) { }
    buzon.r[8] = n_t32_c;
    buzon.estado = 2;
    for (;;) { }
}

static void m15_taclk(void) {
    P7->DIR &= ~BIT2;                            /* P7.2: TA1CLK con DIR = 0 */
    P7->SEL0 |= BIT2;
    TIMER_A1->EX0 = TIMER_A_EX0_IDEX__5;
    TIMER_A1->CTL = TIMER_A_CTL_SSEL__TACLK | TIMER_A_CTL_ID__2 |
                    TIMER_A_CTL_MC__CONTINUOUS | TIMER_A_CTL_CLR;
    buzon.estado = 1;
    espera_senal(1);
    buzon.r[0] = TIMER_A1->R;
    buzon.estado = 2;
    for (;;) { }
}

static void m16_servo(void) {
    P2->DIR |= BIT5;                             /* P2.5 = TA0.2 */
    P2->SEL0 |= BIT5;
    TIMER_A0->CCR[0] = 15000 - 1;                /* 3 MHz / 4 = 750 kHz: 20 ms */
    TIMER_A0->CCR[2] = 1125 - 1;                 /* 1,5 ms */
    TIMER_A0->CCTL[2] = TIMER_A_CCTLN_OUTMOD_7;
    TIMER_A0->CCTL[0] = TIMER_A_CCTLN_CCIE;      /* despierta cada periodo */
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_ID__4 | TIMER_A_CTL_MC__UP |
                    TIMER_A_CTL_CLR;
    NVIC->ISER[0] = 1u << TA0_0_IRQn;
    buzon.estado = 1;
    while (buzon.senal != 1) { __WFI(); }
    TIMER_A0->CCR[2] = 1500 - 1;                 /* 2,0 ms */
    buzon.estado = 2;
    for (;;) { __WFI(); }
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
    case 10: m10_pwm(); break;
    case 11: m11_continuo(); break;
    case 12: m12_updown(); break;
    case 13: m13_captura(); break;
    case 14: m14_timer32(); break;
    case 15: m15_taclk(); break;
    case 16: m16_servo(); break;
    default: break;
    }
    for (;;) { }
}
