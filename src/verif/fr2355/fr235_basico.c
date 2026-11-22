/* fr235_basico.c — testfr2355 (M7): lo que hace un programa de la FR2355
 * (registros, sin DriverLib):
 *
 *   arranque  XT1 (P2.6/P2.7) y el FLL a 24 MHz con NWAITS = 2, como la demo
 *             de fábrica; la UCA1 (el backchannel, P4.3/P4.2) a 115200; el
 *             ID de la TLV.
 *   ADC       de 12 bits: DVCC (canal 15) y la referencia compartida a 2,0
 *             V (REFVSEL = 01, canal 13) contra AVCC; la misma a 8 bits.
 *   ICC       TB0 CCR0 con nivel 3 y el P1 con nivel 0: el P1 anida dentro
 *             de la rutina de TB0 (con GIE), y TB0 no dentro de la del P1;
 *             sin ICCEN, cualquiera anida. ICCSC e ICCMVS dentro.
 *   SAC       el DAC del SAC3 (DVCC, DACLSEL = 10: lo carga TB2.1) en
 *             seguidor; el SAC1 en PGA inversor en cascada (MSEL = 11,
 *             ganancia 1) sobre su DAC a 1,65 V; el ADC lee su salida (A5)
 *             por dentro. DACIFG por SAC1_SAC3. El eCOMP1 compara OA1O
 *             (CPPSEL = 101) con la de 1,2 V (CPNSEL = 010).
 *   Timer_B   TB3.6 en P6.5 (7 CCR): PWM del 30 % y su TB3IV = 0x0C.
 *   MPY32     un producto con signo de 32 x 16.
 *
 * Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void espera_ms(unsigned ms) { while (ms--) __delay_cycles(24000); }

volatile char orden[16];
volatile unsigned n_orden, sc_dentro, mvs_dentro, dacifg, tb3n;
static void anota(char c) { if (n_orden < sizeof orden - 1) orden[n_orden++] = c; }

static unsigned adc(unsigned canal, unsigned res) {
    ADCCTL0 &= ~ADCENC;
    ADCCTL0 = ADCSHT_2 | ADCON;
    ADCCTL1 = ADCSHP | ADCSSEL_0;                    /* MODOSC */
    ADCCTL2 = res;
    ADCMCTL0 = ADCSREF_0 | canal;
    ADCIFG = 0;
    ADCCTL0 |= ADCENC | ADCSC;
    while (!(ADCIFG & ADCIFG0)) ;
    const unsigned r = ADCMEM0;
    ADCCTL0 &= ~(ADCENC | ADCON);
    return r;
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    FRCTL0 = FRCTLPW | NWAITS_2;
    P2SEL1 |= BIT6 | BIT7;
    PM5CTL0 &= ~LOCKLPM5;
    do { CSCTL7 &= ~(XT1OFFG | DCOFFG); SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
    __bis_SR_register(SCG0);
    CSCTL3 |= SELREF__XT1CLK;
    CSCTL0 = 0;
    CSCTL1 = DCORSEL_7;
    CSCTL2 = FLLD_0 + 731;
    __delay_cycles(3);
    __bic_SR_register(SCG0);
    while (CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)) ;
    CSCTL4 = SELMS__DCOCLKDIV | SELA__XT1CLK;
    /* UCA1: 115200 con 24 MHz (UCOS16, UCBR = 13, UCBRS = 0x25) */
    P4SEL0 |= BIT2 | BIT3;
    UCA1CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA1BRW = 13;
    UCA1MCTLW = (0x25 << 8) | UCOS16;
    UCA1CTLW0 &= ~UCSWRST;
    cad("hola FR2355 id "); hex(*(volatile uint16_t*)0x1A04, 4); pon('\n');

    /* ADC de 12 bits; la referencia compartida a 2,0 V */
    cad("adc vcc "); dec(adc(ADCINCH_15, ADCRES_2)); pon('\n');
    PMMCTL0_H = PMMPW_H;
    PMMCTL2 = INTREFEN | REFVSEL_1;
    while (!(PMMCTL2 & REFGENRDY)) ;
    cad("adc ref "); dec(adc(ADCINCH_13, ADCRES_2)); cad(" 8b "); dec(adc(ADCINCH_13, ADCRES_0)); pon('\n');
    PMMCTL2 = 0;

    /* ICC: TB0 CCR0 (ILSR21) nivel 3, P1 (ILSR3) nivel 0 */
    __bic_SR_register(GIE);
    ICCILSR0 = ~ILSR3;                               /* P1: nivel 0; el resto, 3 */
    ICCSC |= ICCEN;
    cad("icc sc "); hex(ICCSC, 4); pon('\n');
    P1IE = BIT0; P1IFG = 0;
    TB0CCTL0 = CCIE;
    __bis_SR_register(GIE);
    n_orden = 0;
    TB0CCTL0 |= CCIFG;                               /* TB0 primero: el P1 anida */
    while (n_orden < 4) ;
    orden[n_orden] = 0;
    cad("icc a "); cad((const char*)orden); cad(" sc "); hex(sc_dentro, 4); cad(" mvs "); hex(mvs_dentro, 4); pon('\n');
    n_orden = 0;
    P1IFG |= BIT0;                                   /* el P1 primero: TB0 espera */
    while (n_orden < 4) ;
    orden[n_orden] = 0;
    cad("icc b "); cad((const char*)orden); cad(" sc "); hex(ICCSC, 4); pon('\n');
    __bic_SR_register(GIE);
    ICCSC &= ~ICCEN;
    __bis_SR_register(GIE);
    n_orden = 0;
    P1IFG |= BIT0;                                   /* sin ICC: TB0 anida */
    while (n_orden < 4) ;
    orden[n_orden] = 0;
    cad("icc c "); cad((const char*)orden); pon('\n');
    P1IE = 0; TB0CCTL0 = 0;

    /* SAC3: DAC (DVCC, DACLSEL = 10) en seguidor; SAC1: PGA inversor en cascada */
    SAC3DAC = DACEN | DACIE | DACLSEL_2;
    SAC3DAT = 0x0C00;
    SAC3PGA = MSEL_1;
    SAC3OA = SACEN | OAEN | NMUXEN | NSEL_1 | PMUXEN | PSEL_1;
    SAC1DAC = DACEN;
    SAC1DAT = 0x07FF;
    SAC1PGA = MSEL_3 | (1 << 4);
    SAC1OA = SACEN | OAEN | NMUXEN | NSEL_1 | PMUXEN | PSEL_1;
    CP1CTL0 = CPPEN | CPPSEL_5 | CPNEN | CPNSEL_2;
    CP1CTL1 = CPEN;
    __delay_cycles(240);
    cad("sac antes "); dec(adc(ADCINCH_5, ADCRES_2)); cad(" cp1 "); dec(CP1CTL1 & CPOUT); pon('\n');
    /* TB2.1 (reset/set, 1 kHz) carga el latch */
    TB2CCR0 = 24000 - 1;
    TB2CCR1 = 12000;
    TB2CCTL1 = OUTMOD_7;
    TB2CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
    espera_ms(3);
    TB2CTL = 0;
    cad("sac tras "); dec(adc(ADCINCH_5, ADCRES_2)); cad(" cp1 "); dec(CP1CTL1 & CPOUT); cad(" dacifg "); dec(dacifg); pon('\n');
    SAC1OA = 0; SAC3OA = 0; SAC3DAC = 0; SAC1DAC = 0; CP1CTL1 = 0;

    /* TB3.6 en P6.5: 30 % con 1 kHz */
    P6DIR |= BIT5; P6SEL0 |= BIT5;
    TB3CCR0 = 24000 - 1;
    TB3CCR6 = 7200;
    TB3CCTL6 = OUTMOD_7 | CCIE;
    TB3CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
    cad("tb3\n");
    espera_ms(10);
    TB3CCTL6 = OUTMOD_7;
    cad("tb3 ccr6 "); dec(tb3n > 5); pon('\n');

    /* MPY32: -100000 x 3000 con signo */
    MPYS32L = (uint16_t)(-100000L);
    MPYS32H = (uint16_t)((-100000L) >> 16);
    OP2 = 3000;
    __delay_cycles(10);
    cad("mpy "); dec((long)(((uint32_t)RES1 << 16) | RES0)); pon('\n');

    cad("fin\n");
    for (;;) __bis_SR_register(LPM0_bits);
}

__attribute__((interrupt(TIMER0_B0_VECTOR))) void tb0(void) {
    anota('T');
    sc_dentro = ICCSC;
    __bis_SR_register(GIE);
    if (n_orden == 1) P1IFG |= BIT0;                 /* en la vuelta a: el P1 (nivel 0) entra aquí */
    __delay_cycles(200);
    anota('t');
}
__attribute__((interrupt(PORT1_VECTOR))) void p1(void) {
    P1IFG &= ~BIT0;
    anota('P');
    mvs_dentro = ICCMVS;
    __bis_SR_register(GIE);
    if (n_orden == 1) TB0CCTL0 |= CCIFG;             /* en las vueltas b y c */
    __delay_cycles(200);
    anota('p');
}
__attribute__((interrupt(SAC1_SAC3_VECTOR))) void sac13(void) { if (SAC3IV == 4) ++dacifg; }
__attribute__((interrupt(TIMER3_B1_VECTOR))) void tb3(void) { if (TB3IV == TBIV__TBCCR6) ++tb3n; }
