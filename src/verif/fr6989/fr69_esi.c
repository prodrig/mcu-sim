/* fr69_esi.c — testfr5 (M8): el ESI del FR6989 [UG FR5 SLAU367P cap. 37]
 * (registros; -mlarge). El banco pone ESICI0-3 (P9.4-P9.7) a 0,6, 1,8, 1,2
 * y 2,4 V, ESICH0 (P9.0) a 1,0 V y una resistencia de subida en ESICH2
 * (P9.2); en la prueba de la PSM mueve ESICI0/ESICI1 en cuadratura.
 *
 *   osc     ESIHFSEL + ESICLKGON: ESICNT3 cuenta los ciclos del oscilador
 *           en un periodo de ACLK (se lee 1 mientras cuenta).
 *   ram     la RAM de la PSM (0x0E00): con ESIEN = 0 se lee lo escrito; con
 *           ESIEN = 1, 0 y no se escribe.
 *   afe     las entradas directas (ESICA1X) contra el DAC a 1,5 V, por
 *           ESISTART: ESIOUT0-3, ESIIFG1 y ESISTART borrado al acabar.
 *   hist    la histéresis: ESIDAC1R3 (ESIOUT1 = 1) más alto que ESIDAC1R2.
 *   sh      la muestra de ESICH0 (ESISH, ESIEX) contra 0,8 V y 1,2 V.
 *   psm     el ACLK dividido (cada 50 ciclos) y una tabla de cuadratura en la
 *           PSM: ESICNT0 los pasos adelante, ESICNT2 los de atrás (hacia
 *           abajo), ESICNT1 la posición; ESIIFG3 (ESITHR1 = 5), ESIIFG7
 *           (módulo 4), ESIIFG2; ESIO0 (ESIEX) a TA3 CCI2B cuenta las
 *           secuencias.
 *   test    la secuencia de prueba (ESITESTD): ESITCHOUT0 (ESICI2) y
 *           ESITCHOUT1 (ESICI3), y ESITESTD borrado.
 *   iv      ESIIV en su orden de prioridad; escribirlo borra todo.
 *   ex      la excitación (ESITEN, ESIEX, ESILCEN) tira de ESICH2 (P9.2) a
 *           ESIDVSS, y ESIC1OUT (P2.6) da el comparador (el banco los mira).
 *
 * Todo sale por la UART del backchannel (UCA1 en P3.4/P3.5, 115200). */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA1STATW & UCBUSY) ; }

#define ESITSMx(i) (((volatile uint16_t*)&ESITSM0)[i])
#define ESIDAC1R(k) (((volatile uint16_t*)&ESIDAC1R0)[k])
#define REP(n) ((uint16_t)(n) << 11)
#define TCH1(n) ((uint16_t)(n) << 5)                         /* ESITCH1x, bits 6-5 [UG tabla 37-28] */
#define TCH0(n) ((uint16_t)(n) << 3)                         /* ESITCH0x, bits 4-3 */
#define DAC_15V 2048                                          /* 1,5 V con AVCC = 3 V */

static volatile unsigned despiertos, n_stop, n_cap;
__attribute__((interrupt(TIMER1_A0_VECTOR))) void ta1(void) { ++despiertos; __bic_SR_register_on_exit(LPM3_bits); }
__attribute__((interrupt(ESCAN_IF_VECTOR))) void esi(void) { ESIINT2 &= ~ESIIFG1; ++n_stop; }
__attribute__((interrupt(TIMER3_A1_VECTOR))) void ta3(void) { if (TA3IV == TA3IV_TACCR2) ++n_cap; }

/* Un rato en LPM3 (ACLK = LFXT) */
static void lpm3(unsigned tics) {
    TA1CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR;
    TA1CCR0 = tics;
    TA1CCTL0 = CCIE;
    __bis_SR_register(LPM3_bits | GIE);
    __no_operation();
    TA1CCTL0 = 0;
    TA1CTL = 0;
}
/* Una secuencia por ESISTART; espera a que acabe */
static void una(void) {
    ESITSM = ESITSMTRG_2 | ESIDIV1__1;
    ESITSM |= ESISTART;
    while (ESITSM & ESISTART) ;
}
/* La TSM: para cada canal de `m`, asentar (4 ciclos) y medir (2) con ESIRSON */
static void tsm_canales(unsigned m, uint16_t extra) {
    unsigned i = 1;
    ESITSM0 = 0;
    for (unsigned k = 0; k < 4; ++k) {
        if (!(m & (1u << k))) continue;
        const uint16_t t1 = (k & 1) ? ESITESTS1 : 0;       /* para la prueba: ch1 y ch3 con ESITESTS1 */
        ESITSMx(i++) = REP(3) | ESIDAC | ESICA | t1 | extra | k;
        ESITSMx(i++) = REP(1) | ESIDAC | ESICA | ESIRSON | t1 | extra | k;
    }
    ESITSMx(i) = ESISTOP;
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PJSEL0 |= BIT4 | BIT5;
    PM5CTL0 &= ~LOCKLPM5;
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                     /* 8 MHz */
    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL4 &= ~LFXTOFF;
    do { CSCTL5 &= ~LFXTOFFG; SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
    CSCTL0_H = 0;
    P3SEL0 |= BIT4 | BIT5; P3SEL1 &= ~(BIT4 | BIT5);
    UCA1CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA1BRW = 4;
    UCA1MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA1CTLW0 &= ~UCSWRST;
    P9SEL0 = 0xFF; P9SEL1 = 0xFF;                          /* ESICH0-3 y ESICI0-3 */
    cad("hola esi\n");

    /* El oscilador */
    {
        ESIOSC = ESICLKFQ5 | ESIHFSEL | ESICLKGON;
        const uint16_t a = ESICNT3;
        lpm3(4);
        const uint16_t b = ESICNT3;
        ESIOSC = ESICLKFQ5 | ESIHFSEL;
        cad("osc antes "); dec(a); cad(" cnt3 "); dec(b); pon('\n');
    }
    /* La RAM de la PSM */
    {
        volatile uint8_t* r = (volatile uint8_t*)0x0E00;
        ESICTL = 0;
        r[5] = 0x5A; r[6] = 0xA5;
        const uint8_t a = r[5], b = r[6];
        ESICTL = ESIEN;
        const uint8_t c = r[5];
        r[5] = 0x11;
        ESICTL = 0;
        const uint8_t d = r[5];
        cad("ram "); hex(a, 2); pon(' '); hex(b, 2); cad(" en "); hex(c, 2); cad(" luego "); hex(d, 2); pon('\n');
    }
    /* Las entradas directas */
    {
        for (unsigned k = 0; k < 8; ++k) ESIDAC1R(k) = DAC_15V;
        ESIAFE = ESICA1X;
        tsm_canales(0xF, 0);
        ESIINT2 = 0;
        ESICTL = ESIEN;
        una();
        const uint16_t ppu = ESIPPU, ifg = ESIINT2;
        cad("afe ppu "); hex(ppu, 4); cad(" ifg1 "); dec((ifg & ESIIFG1) ? 1 : 0); cad(" start "); dec((ESITSM & ESISTART) ? 1 : 0); pon('\n');
        /* La histéresis de ESICI1 (1,8 V): con ESIOUT1 = 1, ESIDAC1R3 a 2,4 V */
        ESICTL = 0;
        ESIDAC1R(3) = 3277;
        ESICTL = ESIEN;
        unsigned h[3];
        for (unsigned i = 0; i < 3; ++i) { una(); h[i] = (ESIPPU & ESIOUT1) ? 1 : 0; }
        cad("hist "); dec(h[0]); pon(' '); dec(h[1]); pon(' '); dec(h[2]); pon('\n');
        ESICTL = 0;
        ESIDAC1R(3) = DAC_15V;
    }
    /* La muestra de ESICH0 (1,0 V) */
    {
        ESIAFE = ESISH;
        ESITSM0 = 0;
        ESITSM1 = REP(3) | ESIEX | ESILCEN;                    /* muestrear */
        ESITSM2 = REP(3) | ESIDAC | ESICA;
        ESITSM3 = REP(1) | ESIDAC | ESICA | ESIRSON;
        ESITSM4 = ESISTOP;
        ESIDAC1R0 = ESIDAC1R1 = 1092;                          /* 0,8 V */
        ESICTL = ESIEN;
        una();
        const unsigned a = ESIPPU & ESIOUT0;
        ESICTL = 0;
        ESIDAC1R0 = ESIDAC1R1 = 1638;                          /* 1,2 V */
        ESICTL = ESIEN;
        una();
        const unsigned b = ESIPPU & ESIOUT0;
        cad("sh "); dec(a ? 1 : 0); pon(' '); dec(b ? 1 : 0); pon('\n');
        ESICTL = 0;
    }
    /* La PSM en cuadratura: ESIOUT0 = PPUS1, ESIOUT1 = PPUS2 */
    {
        volatile uint8_t* r = (volatile uint8_t*)0x0E00;
        static const uint8_t sig[4] = {1, 3, 0, 2};           /* 00 -> 01 -> 11 -> 10 -> 00 */
        for (unsigned i = 0; i < 128; ++i) r[i] = 0;
        for (unsigned p = 0; p < 4; ++p)
            for (unsigned c = 0; c < 4; ++c) {
                uint8_t q = (uint8_t)(c << 3);
                if (c == sig[p]) q |= 0x02;                    /* Q1: adelante */
                if (p == sig[c]) q |= 0x04;                    /* Q2: atrás */
                r[(p << 3) | c] = q;
            }
        for (unsigned k = 0; k < 8; ++k) ESIDAC1R(k) = DAC_15V;
        ESIAFE = ESICA1X;
        tsm_canales(0x3, 0);
        ESITSMx(5) = ESISTOP;                                  /* ESITSM1-4: los dos canales */
        ESITSM4 |= ESIEX;                                      /* ESIEX en la última medida: ESIO0 */
        ESICTL = ESIS1SEL_0 | ESIS2SEL_1;
        ESIPSM = ESIV2SEL | ESICNT0EN | ESICNT1EN | ESICNT2EN | ESICNT0RST | ESICNT1RST | ESICNT2RST;
        ESITHR1 = 5;
        ESIINT2 = ESIIS0_1 | ESIIS2_0;
        ESIINT1 = ESIIE1;
        TA3CCTL2 = CM_1 | CCIS_1 | CAP | SCS | CCIE;           /* ESIO0: CCI2B */
        TA3CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR;
        n_stop = n_cap = 0;
        ESICTL |= ESIEN;
        ESITSM = ESITSMTRG_1 | ESIDIV2__1 | ESIDIV3A_2 | ESIDIV3B_2;   /* 5 · 5 · 2 = 50 ciclos */
        cad("psm listo\n");
        fin_tx();
        lpm3(8192);                                            /* 250 ms: el banco mueve las entradas */
        ESITSM = 0;                                            /* parada */
        lpm3(4);
        const uint16_t c0 = ESICNT0, c1 = ESICNT1, c2 = ESICNT2, ifg = ESIINT2;
        cad("psm cnt0 "); dec(c0); cad(" cnt1 "); dec((int16_t)c1); cad(" cnt2 "); hex(c2, 4);
        cad(" ifg3 "); dec((ifg & ESIIFG3) ? 1 : 0); cad(" ifg7 "); dec((ifg & ESIIFG7) ? 1 : 0);
        cad(" ifg2 "); dec((ifg & ESIIFG2) ? 1 : 0); cad(" stop "); dec(n_stop); cad(" cap "); dec(n_cap); pon('\n');
        TA3CTL = 0; TA3CCTL2 = 0;
        /* La secuencia de prueba: canal 0 de prueba = ESICI2 (1,2 V), canal 1 = ESICI3 (2,4 V) */
        ESIINT1 = 0;
        ESICTL = (ESICTL & ~(ESITCH00 | ESITCH01 | ESITCH10 | ESITCH11)) | TCH0(2) | TCH1(3) | ESITESTD;
        ESITSM = ESITSMTRG_1 | ESIDIV2__1 | ESIDIV3A_2 | ESIDIV3B_2;
        lpm3(330);                                             /* ~10 ms */
        ESITSM = 0;
        lpm3(4);
        cad("test tch "); hex((ESIPPU >> 8) & 3, 1); cad(" testd "); dec((ESICTL & ESITESTD) ? 1 : 0); pon('\n');
    }
    /* ESIIV: las banderas que quedan, en orden */
    {
        __disable_interrupt();                                 /* lpm3 deja GIE: aquí se mira ESIIV a mano */
        ESIINT2 |= ESIIFG0 | ESIIFG5 | ESIIFG6 | ESIIFG8;
        ESIINT1 = ESIIE0 | ESIIE1 | ESIIE2 | ESIIE3 | ESIIE4 | ESIIE5 | ESIIE6 | ESIIE7 | ESIIE8;
        cad("iv");
        for (unsigned i = 0; i < 10; ++i) { const uint16_t v = ESIIV; pon(' '); hex(v, 2); if (!v) break; }
        ESIINT2 |= ESIIFG1 | ESIIFG7;
        ESIIV = 0;
        cad(" borra "); hex(ESIINT2 & 0x1FF, 3); pon('\n');
        ESIINT1 = 0;
        ESICTL = 0;
    }
    /* La excitación de ESICH2 y ESIC1OUT en P2.6 */
    {
        P2SEL1 |= BIT6; P2SEL0 &= ~BIT6; P2DIR |= BIT6;        /* ESIC1OUT */
        ESIAFE = ESITEN | ESICA1X;
        ESIDAC1R4 = ESIDAC1R5 = 1000;                          /* ~0,73 V: ESICI2 (1,2 V) por encima */
        ESITSM0 = 0;
        ESITSM1 = REP(31) | ESICLK | ESIEX | ESILCEN | ESICA | ESIDAC | 2;   /* ~1 ms con ACLK */
        ESITSM2 = ESISTOP;
        ESICTL = ESIEN;
        cad("ex listo\n");
        fin_tx();
        for (unsigned i = 0; i < 20; ++i) una();
        ESICTL = 0;
        cad("fin esi\n");
        fin_tx();
    }
    for (;;) ;
}
