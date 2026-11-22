/* fr59_errata.c — testfr5 (M8): las erratas del FR5969 que se modelan
 * [SLAZ473AG] (registros; -mlarge). Cada una, tal y como la describe TI:
 *
 *   REF9    con REFON = 1 el Comp_E no tiene la referencia: C2 (P1.2) a 0,6 V
 *           contra 1,2 V da CEOUT = 1; pidiéndola el Comp_E (CEREFL) con
 *           REFON = 0, CEOUT = 0.
 *   COMP10  CEON = 0 y CEOUTPOL = 1 con COUT en P3.5: el pin se invierte en
 *           LPM3 (el banco lo mira).
 *   ADC41   en LPM3, con ADC12OSC y sin REFON: TB0 (SHS = 2) no dispara el
 *           ADC12_B; TA0 (SHS = 1), sí.
 *   ADC42   en repetición de un canal, un SC con la conversión en curso para
 *           el ADC (BUSY a 1); se recupera con ENC = 0 y ON = 0.
 *   ADC71   tras un disparo de TA0, ENC y SC a la vez: BUSY a 1 y SC sin
 *           borrar.
 *   AES1    escribir AESAXIN con AESBUSY no pone AESERRFG.
 *   WDG5    con el perro en marcha, LPM4.5 entra igual; despierta con P1.1.
 *
 * Todo sale por la UART (UCA0, 115200, SMCLK = DCO a 8 MHz) en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }

__attribute__((persistent)) volatile uint16_t etapa = 0;
static volatile unsigned n_adc, despiertos;

/* Un rato en LPM3; despierta TA1.0 (ACLK = VLO, ~9,4 kHz) */
static void lpm3(unsigned tics) {
    TA1CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR;
    TA1CCR0 = tics;
    TA1CCTL0 = CCIE;
    __bis_SR_register(LPM3_bits | GIE);
    __no_operation();
    TA1CCTL0 = 0;
    TA1CTL = 0;
}
__attribute__((interrupt(TIMER1_A0_VECTOR))) void ta1(void) { ++despiertos; __bic_SR_register_on_exit(LPM3_bits); }
__attribute__((interrupt(ADC12_VECTOR))) void adc12(void) { (void)ADC12MEM0; ++n_adc; }

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    const uint16_t rst = SYSRSTIV;
    PM5CTL0 &= ~LOCKLPM5;
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                     /* 8 MHz */
    CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
    P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1);
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 4;
    UCA0MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
    if (etapa == 7) {                                       /* tras LPM4.5 */
        etapa = 0;
        cad("wdg5 despierta rstiv "); hex(rst, 2); pon('\n');
        fin_tx();
        for (;;) ;
    }
    cad("hola errata\n");

    /* REF9: C2 (P1.2, a 0,6 V por el banco) contra la referencia de 1,2 V */
    {
        P1SEL0 |= BIT2; P1SEL1 |= BIT2;
        CECTL0 = CEIPEN | CEIPSEL_2;
        CECTL2 = CEREFL_1 | CERS_3 | CERSEL;
        REFCTL0 = REFVSEL_0 | REFON;
        CECTL1 = CEON;
        __delay_cycles(8000);
        const unsigned con = (CECTL1 & CEOUT) ? 1 : 0;
        REFCTL0 = 0;
        __delay_cycles(8000);
        const unsigned sin = (CECTL1 & CEOUT) ? 1 : 0;
        cad("ref9 con refon "); dec(con); cad(" sin "); dec(sin); pon('\n');
    }
    /* COMP10: CEON = 0 y CEOUTPOL = 1, COUT en P3.5 */
    {
        CECTL1 = CEOUTPOL;
        P3DIR |= BIT5; P3SEL1 |= BIT5; P3SEL0 &= ~BIT5;
        cad("comp10 activo\n");
        fin_tx();
        __delay_cycles(80000);                              /* 10 ms en activo */
        lpm3(470);                                          /* ~50 ms en LPM3 */
        cad("comp10 fin\n");
        P3SEL1 &= ~BIT5; P3DIR &= ~BIT5;
    }
    /* ADC41: TB0 CCR0 (SHS = 2) y TA0 CCR1 (SHS = 1) cada ~2 ms (ACLK) en LPM3 */
    {
        ADC12CTL0 = ADC12SHT0_2 | ADC12ON;
        ADC12CTL1 = ADC12SHS_2 | ADC12CONSEQ_2 | ADC12SSEL_0;
        ADC12CTL2 = ADC12RES_2;
        ADC12MCTL0 = ADC12INCH_31;
        ADC12CTL3 = ADC12BATMAP;
        ADC12IER0 = ADC12IE0;
        ADC12CTL0 |= ADC12ENC;
        TB0CCR0 = 19; TB0CCTL0 = OUTMOD_4;                   /* TB0.0 cambia cada 20 tics */
        TB0CTL = TBSSEL__ACLK | MC__UP | TBCLR;
        n_adc = 0;
        lpm3(470);
        const unsigned tb0 = n_adc;
        TB0CTL = 0;
        ADC12CTL0 &= ~ADC12ENC;
        ADC12CTL1 = ADC12SHS_1 | ADC12CONSEQ_2 | ADC12SSEL_0;
        ADC12CTL0 |= ADC12ENC;
        TA0CCR0 = 19; TA0CCR1 = 10; TA0CCTL1 = OUTMOD_7;     /* TA0.1: un pulso cada 20 tics */
        TA0CTL = TASSEL__ACLK | MC__UP | TACLR;
        n_adc = 0;
        lpm3(470);
        const unsigned ta0 = n_adc;
        TA0CTL = 0;
        ADC12CTL0 &= ~ADC12ENC;
        ADC12IER0 = 0;
        cad("adc41 tb0 "); dec(tb0); cad(" ta0 "); dec(ta0); pon('\n');
    }
    /* ADC71: tras un disparo de TA0 (el de arriba), ENC y SC a la vez con SHS = 0 */
    {
        ADC12CTL1 = ADC12SHP | ADC12CONSEQ_0 | ADC12SSEL_0;
        ADC12CTL0 = ADC12SHT0_2 | ADC12ON;
        ADC12CTL0 |= ADC12ENC | ADC12SC;
        __delay_cycles(800);
        const unsigned busy = (ADC12CTL1 & ADC12BUSY) ? 1 : 0, sc = (ADC12CTL0 & ADC12SC) ? 1 : 0;
        ADC12CTL0 &= ~ADC12ENC;
        ADC12CTL0 &= ~ADC12ON;
        cad("adc71 busy "); dec(busy); cad(" sc "); dec(sc); cad(" luego "); dec((ADC12CTL1 & ADC12BUSY) ? 1 : 0); pon('\n');
    }
    /* ADC42: repetición de un canal con muestreo largo; un SC con la conversión en curso */
    {
        ADC12CTL1 = ADC12SHP | ADC12CONSEQ_2 | ADC12SSEL_0;
        ADC12CTL0 = ADC12SHT0_15 | ADC12ON;                 /* 512 ciclos de MODOSC: ~100 µs */
        ADC12IFGR0 = 0;
        ADC12CTL0 |= ADC12ENC;
        ADC12CTL0 |= ADC12SC;
        __delay_cycles(80);
        ADC12CTL0 |= ADC12SC;                               /* con la conversión en curso */
        __delay_cycles(8000);                               /* 1 ms: unas diez conversiones */
        const unsigned busy = (ADC12CTL1 & ADC12BUSY) ? 1 : 0, ifg = (ADC12IFGR0 & ADC12IFG0) ? 1 : 0;
        const unsigned tov = (ADC12IFGR2 & ADC12TOVIFG) ? 1 : 0;
        ADC12CTL0 &= ~ADC12ENC;
        ADC12CTL0 &= ~ADC12ON;                              /* la salida: ENC = 0 y ON = 0 */
        ADC12CTL0 |= ADC12ON;
        ADC12IFGR0 = 0;
        ADC12CTL0 |= ADC12ENC | 0;
        ADC12CTL0 |= ADC12SC;
        __delay_cycles(8000);
        const unsigned otra = (ADC12IFGR0 & ADC12IFG0) ? 1 : 0;
        ADC12CTL0 &= ~ADC12ENC;
        while (ADC12CTL1 & ADC12BUSY) ;
        ADC12CTL0 = 0;
        cad("adc42 busy "); dec(busy); cad(" ifg "); dec(ifg); cad(" tov "); dec(tov); cad(" otra "); dec(otra); pon('\n');
    }
    /* AES1: AESAXIN con AESBUSY */
    {
        AESACTL0 = AESOP_0 | AESKL_0;
        for (unsigned i = 0; i < 8; ++i) AESAKEY = 0x0100u * (2 * i + 1) + 2 * i;
        for (unsigned i = 0; i < 8; ++i) AESADIN = 0x1100u * (i + 1);
        const unsigned b = (AESASTAT & AESBUSY) ? 1 : 0;
        AESAXIN = 0x1234;
        const unsigned e = (AESACTL0 & AESERRFG) ? 1 : 0;
        while (AESASTAT & AESBUSY) ;
        AESADIN = 0x0000;                                   /* DIN con AESBUSY = 0: sin error */
        cad("aes1 busy "); dec(b); cad(" errfg "); dec(e); pon('\n');
    }
    /* WDG5: el perro en marcha (ACLK = VLO, 2^23: ~15 min) y LPM4.5 */
    {
        cad("wdg5 entra\n");
        fin_tx();
        etapa = 7;
        P1DIR &= ~BIT1; P1REN |= BIT1; P1OUT |= BIT1;
        P1IES |= BIT1; P1IFG = 0; P1IE |= BIT1;
        WDTCTL = WDTPW | WDTSSEL__ACLK | WDTIS__8192K;
        PMMCTL0_H = PMMPW_H;
        PMMCTL0_L |= PMMREGOFF;
        __bis_SR_register(LPM4_bits);
        etapa = 0;
        cad("wdg5 no entra\n");
    }
    for (;;) ;
}
