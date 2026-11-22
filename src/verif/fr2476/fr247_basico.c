/* fr247_basico.c — testfr2476 (M7): lo que hace un programa de la FR2476
 * (registros, sin DriverLib; -mlarge):
 *
 *   arranque  el FLL a 16 MHz desde REFO con NWAITS = 1; la UCA0 (el
 *             backchannel, P1.4/P1.5) a 115200; el ID de la TLV.
 *   FRAM alta código (.upper.text) y una tabla (.upper.rodata) por encima de
 *             0x10000, llamados y leídos con direcciones de 20 bits; una
 *             palabra en 0x17F00 que no se escribe con PFWP y sí sin él.
 *   ADC       de 12 bits: DVCC (canal 15) y la referencia compartida a 2,5 V
 *             (REFVSEL = 10, canal 13) contra AVCC.
 *   Timer_A   TA2.1 en P3.3 (25 %) y, con TA2RMP, en P5.7.
 *   Timer_B   TB0.6 en P4.4 (10 %): el Timer0_B7.
 *   eCOMP0    P1.1 contra la de 1,2 V (CPNSEL = 010): 0 y, cuando el banco
 *             sube P1.1, 1.
 *   P6        una interrupción en P6.1 (PORT6, P6IV = 4).
 *
 * Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void espera_ms(unsigned ms) { while (ms--) __delay_cycles(16000); }

volatile unsigned p6irq, p6iv;

__attribute__((section(".upper.rodata"))) const uint16_t tabla_alta[4] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
__attribute__((section(".upper.text"), noinline)) uint16_t suma_alta(const uint16_t* t, unsigned n) {
    uint16_t s = 0;
    while (n--) s += *t++;
    return s;
}

static unsigned adc(unsigned canal) {
    ADCCTL0 &= ~ADCENC;
    ADCCTL0 = ADCSHT_2 | ADCON;
    ADCCTL1 = ADCSHP;
    ADCCTL2 = ADCRES_2;
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
    FRCTL0 = FRCTLPW | NWAITS_1;
    PM5CTL0 &= ~LOCKLPM5;
    __bis_SR_register(SCG0);
    CSCTL3 = SELREF__REFOCLK;
    CSCTL0 = 0;
    CSCTL1 = DCORSEL_5;
    CSCTL2 = FLLD_0 + 487;
    __delay_cycles(3);
    __bic_SR_register(SCG0);
    while (CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)) ;
    CSCTL4 = SELMS__DCOCLKDIV | SELA__REFOCLK;
    /* UCA0: 115200 con 16 MHz (UCOS16, UCBR = 8, UCBRF = 10, UCBRS = 0xF7) */
    P1SEL0 |= BIT4 | BIT5;
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 8;
    UCA0MCTLW = (0xF7 << 8) | (10 << 4) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
    cad("hola FR2476 id "); hex(*(volatile uint16_t*)0x1A04, 4); pon('\n');

    /* La FRAM de arriba de 0x10000 */
    {
        const uint32_t f = (uint32_t)(uintptr_t)&suma_alta, t = (uint32_t)(uintptr_t)tabla_alta;
        cad("alta f "); hex(f, 5); cad(" t "); hex(t, 5); cad(" suma "); hex(suma_alta(tabla_alta, 4), 4); pon('\n');
        volatile uint16_t* p = (volatile uint16_t*)(uintptr_t)0x17F00UL;
        *p = 0x2476;
        const uint16_t a = *p;
        SYSCFG0 = FRWPPW | DFWP;                       /* PFWP = 0 */
        *p = 0x2476;
        const uint16_t b = *p;
        SYSCFG0 = FRWPPW | DFWP | PFWP;
        cad("alta pfwp "); hex(a, 4); cad(" sin "); hex(b, 4); pon('\n');
    }

    /* ADC de 12 bits; la referencia compartida a 2,5 V */
    cad("adc vcc "); dec(adc(ADCINCH_15));
    PMMCTL0_H = PMMPW_H;
    PMMCTL2 = INTREFEN | REFVSEL_2;
    while (!(PMMCTL2 & REFGENRDY)) ;
    cad(" ref "); dec(adc(ADCINCH_13)); pon('\n');
    PMMCTL2 = 0;

    /* TA2.1 en P3.3 y, reasignado, en P5.7 */
    P3DIR |= BIT3; P3SEL0 |= BIT3;
    P5DIR |= BIT7; P5SEL0 |= BIT7;
    TA2CCR0 = 16000 - 1;
    TA2CCR1 = 4000;
    TA2CCTL1 = OUTMOD_7;
    TA2CTL = TASSEL__SMCLK | MC__UP | TACLR;
    cad("ta2 def\n");
    espera_ms(10);
    SYSCFG3 |= TA2RMP;
    cad("ta2 rmp\n");
    espera_ms(10);
    TA2CTL = 0; SYSCFG3 &= ~TA2RMP;

    /* TB0.6 en P4.4 (P4SEL = 10): 10 % */
    P4DIR |= BIT4; P4SEL1 |= BIT4;
    TB0CCR0 = 16000 - 1;
    TB0CCR6 = 1600;
    TB0CCTL6 = OUTMOD_7;
    TB0CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
    cad("tb0\n");
    espera_ms(10);
    TB0CTL = 0;

    /* eCOMP0: P1.1 contra la de 1,2 V */
    P1SEL0 |= BIT1; P1SEL1 |= BIT1;
    CP0CTL0 = CPPEN | CPPSEL_0 | CPNEN | CPNSEL_2;
    CP0CTL1 = CPEN;
    __delay_cycles(160);
    cad("cp "); dec(CP0CTL1 & CPOUT); cad(" espera\n");
    while (!(CP0CTL1 & CPOUT)) ;
    cad("cp sube\n");
    CP0CTL1 = 0;

    /* P6.1: entrada con pull-up y su interrupción */
    P6DIR &= ~BIT1; P6REN |= BIT1; P6OUT |= BIT1;
    P6IES |= BIT1; P6IFG = 0; P6IE |= BIT1;
    __bis_SR_register(GIE);
    cad("p6 espera\n");
    while (!p6irq) ;
    cad("p6 irq "); dec(p6irq); cad(" iv "); dec(p6iv); pon('\n');

    cad("fin\n");
    for (;;) __bis_SR_register(LPM0_bits);
}

__attribute__((interrupt(PORT6_VECTOR))) void p6(void) { p6iv = P6IV; ++p6irq; }
