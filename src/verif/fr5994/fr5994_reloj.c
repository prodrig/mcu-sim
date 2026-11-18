/* fr5994_reloj.c — testfr5 (M8): el HFXT y el RAMCTL del FR5994 (registros;
 * -mlarge). El banco pone un cristal de 16 MHz en HFXIN/HFXOUT (PJ.6/PJ.7) o,
 * para el bypass, un reloj de 12 MHz en HFXIN.
 *
 *   hfxt      con cristal y HFFREQ = 2 (8-16 MHz): las faltas no se van hasta
 *             que arranca; lo que tarda, en tics de ACLK (el LFXT): tSTART,HFXT;
 *             después SMCLK = HFXT.
 *   hffreq    HFFREQ = 3 (16-24 MHz) con el mismo cristal: no oscila; la
 *             falta sigue y SMCLK cae a MODCLK.
 *   lpm       HFXTOFF = 1 y solo SMCLK lo usa: en LPM3 (SMCLK parado) se
 *             apaga, y al despertar arranca otra vez (HFXTOFFG).
 *   bypass    HFXTBYPASS = 1 con un reloj en HFXIN: arranca con el contador
 *             de 1024 ciclos; con ENSTFCNT2 = 0, sin él.
 *   ram       RCCTL0: la clave (0x69 al leer; de byte o con otra clave, no se
 *             escribe); el sector 2 (0x2C00-0x3BFF) con RCRS2OFF = 1 pierde lo
 *             que tenía tras LPM3 (y el sector 0, no); con RCRS2OFF = 2 sigue
 *             apagado al despertar (se lee 0x3FFF y no se escribe) hasta que
 *             RCRS2OFF vuelve a 0.
 *
 * Todo sale por la UART (UCA0, 115200, SMCLK = DCO a 8 MHz) en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }

static volatile unsigned despiertos;

/* SMCLK otra vez del DCO, para la UART */
static void smclk_dco(void) {
    CSCTL0_H = CSKEY_H;
    CSCTL2 = (CSCTL2 & ~(SELS0 | SELS1 | SELS2)) | SELS__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
}
/* Espera a que se vayan las faltas (o `max` tics de ACLK); devuelve los tics */
static unsigned sin_faltas(unsigned max) {
    const unsigned t0 = TA0R;
    do {
        CSCTL5 &= ~(LFXTOFFG | HFXTOFFG);
        SFRIFG1 &= ~OFIFG;
        if ((unsigned)(TA0R - t0) > max) break;
    } while (SFRIFG1 & OFIFG);
    return (unsigned)(TA0R - t0);
}
/* SMCLK en kHz: TA1 lo cuenta durante 32 tics de ACLK */
static unsigned long smclk_khz(void) {
    TA1CTL = TASSEL__SMCLK | MC__CONTINUOUS | TACLR;
    const unsigned t0 = TA0R;
    while ((unsigned)(TA0R - t0) < 1) ;
    TA1CTL |= TACLR;
    const unsigned t1 = TA0R;
    while ((unsigned)(TA0R - t1) < 32) ;
    const unsigned n = TA1R;
    TA1CTL = 0;
    return (unsigned long)n * 1024UL / 1000UL;           /* n · 32768 / 32 / 1000 */
}
/* Un rato en LPM3, despierta con TA0.0 (ACLK) */
static void lpm3(unsigned tics) {
    TA0CCR0 = TA0R + tics;
    TA0CCTL0 = CCIE;
    __bis_SR_register(LPM3_bits | GIE);
    __no_operation();
    TA0CCTL0 = 0;
}
__attribute__((interrupt(TIMER0_A0_VECTOR))) void ta0(void) {
    ++despiertos;
    __bic_SR_register_on_exit(LPM3_bits);
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    PJSEL0 |= BIT4 | BIT5;                                  /* el LFXT */
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                     /* 8 MHz */
    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL4 &= ~LFXTOFF;
    CSCTL0_H = 0;
    P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1);
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 4;
    UCA0MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
    TA0CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR;
    CSCTL0_H = CSKEY_H;
    sin_faltas(1000);
    CSCTL0_H = 0;
    cad("hola reloj\n");

    /* El HFXT con cristal: HFFREQ = 2 */
    {
        CSCTL0_H = CSKEY_H;
        CSCTL4 = (CSCTL4 & ~(HFFREQ_3 | HFXTBYPASS)) | HFFREQ_2;
        PJSEL0 |= BIT6 | BIT7;
        CSCTL4 &= ~HFXTOFF;
        const unsigned c5 = CSCTL5 & HFXTOFFG;
        const unsigned t = sin_faltas(1000);
        CSCTL2 = (CSCTL2 & ~(SELS0 | SELS1 | SELS2)) | SELS__HFXTCLK;        /* SMCLK = HFXT/2 */
        CSCTL3 = DIVA__1 | DIVS__2 | DIVM__1;
        CSCTL0_H = 0;
        const unsigned ok = (SFRIFG1 & OFIFG) ? 0 : 1;
        const unsigned long k = smclk_khz();
        smclk_dco();
        cad("hfxt falta "); dec(c5 ? 1 : 0); cad(" tics "); dec(t); cad(" ok "); dec(ok); cad(" smclk "); dec(k); pon('\n');
        fin_tx();
    }
    /* HFFREQ = 3: con 16 MHz no oscila */
    {
        CSCTL0_H = CSKEY_H;
        CSCTL4 = (CSCTL4 & ~HFFREQ_3) | HFFREQ_3;
        const unsigned t = sin_faltas(200);
        const unsigned f = (CSCTL5 & HFXTOFFG) ? 1 : 0;
        CSCTL0_H = 0;
        /* SMCLK = HFXT/2 en falta: MODCLK/2 */
        CSCTL0_H = CSKEY_H;
        CSCTL2 = (CSCTL2 & ~(SELS0 | SELS1 | SELS2)) | SELS__HFXTCLK;
        CSCTL3 = DIVA__1 | DIVS__2 | DIVM__1;
        CSCTL0_H = 0;
        const unsigned long k = smclk_khz();
        smclk_dco();
        cad("hffreq falta "); dec(f); cad(" tics "); dec(t); cad(" smclk "); dec(k); pon('\n');
        fin_tx();
        CSCTL0_H = CSKEY_H;
        CSCTL4 = (CSCTL4 & ~HFFREQ_3) | HFFREQ_2;
        sin_faltas(1000);
        CSCTL0_H = 0;
    }
    /* HFXTOFF = 1 y solo SMCLK lo usa: LPM3 lo apaga */
    {
        CSCTL0_H = CSKEY_H;
        CSCTL2 = (CSCTL2 & ~(SELS0 | SELS1 | SELS2)) | SELS__HFXTCLK;
        CSCTL3 = DIVA__1 | DIVS__2 | DIVM__1;
        CSCTL4 |= HFXTOFF;
        sin_faltas(1000);
        const unsigned antes = CSCTL5 & HFXTOFFG;
        CSCTL0_H = 0;
        lpm3(33);                                           /* ~1 ms */
        const unsigned despues = CSCTL5 & HFXTOFFG;
        CSCTL0_H = CSKEY_H;
        const unsigned t = sin_faltas(1000);
        CSCTL0_H = 0;
        smclk_dco();
        cad("lpm antes "); dec(antes ? 1 : 0); cad(" despues "); dec(despues ? 1 : 0); cad(" tics "); dec(t);
        cad(" desp "); dec(despiertos); pon('\n');
        fin_tx();
    }
    /* Bypass: el banco pone 12 MHz en HFXIN */
    {
        cad("bypass listo\n");
        fin_tx();
        __delay_cycles(80000);                              /* 10 ms: el banco cambia de cristal a reloj */
        CSCTL0_H = CSKEY_H;
        CSCTL4 |= HFXTOFF;
        PJSEL0 &= ~(BIT6 | BIT7);
        sin_faltas(100);
        CSCTL4 = (CSCTL4 & ~HFFREQ_3) | HFFREQ_2 | HFXTBYPASS;
        PJSEL0 |= BIT6;
        CSCTL4 &= ~HFXTOFF;
        const unsigned t = sin_faltas(1000);
        const unsigned ok = (SFRIFG1 & OFIFG) ? 0 : 1;
        CSCTL2 = (CSCTL2 & ~(SELS0 | SELS1 | SELS2)) | SELS__HFXTCLK;
        CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
        CSCTL0_H = 0;
        const unsigned long k = smclk_khz();
        smclk_dco();
        cad("bypass tics "); dec(t); cad(" ok "); dec(ok); cad(" smclk "); dec(k); pon('\n');
        fin_tx();
    }
    /* RAMCTL */
    {
        volatile uint16_t* s2 = (volatile uint16_t*)0x3000;          /* sector 2 (LEA) */
        volatile uint16_t* s0 = (volatile uint16_t*)0x1F00;          /* sector 0 */
        const uint16_t r0 = RCCTL0;
        RCCTL0_L = RCRS2OFF0;                                          /* de byte: nada */
        const uint16_t r1 = RCCTL0;
        RCCTL0 = 0x1200 | RCRS2OFF0;                                   /* otra clave: nada */
        const uint16_t r2 = RCCTL0;
        *s2 = 0xA5A5; *s0 = 0x5A5A;
        RCCTL0 = RCKEY | RCRS2OFF0;                                    /* 1: se apaga en LPM3 */
        const uint16_t r3 = RCCTL0;
        lpm3(10);
        const uint16_t d1 = *s2, d0 = *s0;
        *s2 = 0x1234;
        const uint16_t d2 = *s2;
        RCCTL0 = RCKEY | RCRS2OFF1;                                    /* 2: sigue apagado */
        lpm3(10);
        const uint16_t e1 = *s2;
        *s2 = 0x4321;
        const uint16_t e2 = *s2;
        RCCTL0 = RCKEY;                                                /* vuelve */
        *s2 = 0x4321;
        const uint16_t e3 = *s2;
        cad("ram clave "); hex(r0, 4); pon(' '); hex(r1, 4); pon(' '); hex(r2, 4); pon(' '); hex(r3, 4);
        cad(" off1 "); hex(d1, 4); pon(' '); hex(d0, 4); pon(' '); hex(d2, 4);
        cad(" off2 "); hex(e1, 4); pon(' '); hex(e2, 4); pon(' '); hex(e3, 4); pon('\n');
        fin_tx();
    }
    cad("fin reloj\n");
    for (;;) ;
}
