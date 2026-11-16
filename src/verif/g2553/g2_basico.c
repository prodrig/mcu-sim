/* g2_basico.c — la prueba de testg2 (M3 de doc/analisis_msp430.md): lo que
 * hace cualquier ejemplo de la G2ET con los módulos del G2553. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(IFG2 & UCA0TXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void dec(unsigned v) { char b[6]; int i = 0; do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }

volatile unsigned ticks;

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    if (CALBC1_1MHZ == 0xFF) for (;;) ;
    BCSCTL1 = CALBC1_1MHZ;                 /* el DCO a 1 MHz, con lo de fábrica */
    DCOCTL = CALDCO_1MHZ;
    BCSCTL3 = LFXT1S_2;                    /* ACLK = VLO */
    P1DIR = BIT0 | BIT6;                   /* los LED */
    P1OUT = BIT3;                          /* S2 con su resistencia hacia arriba */
    P1REN = BIT3;
    P1IES = BIT3; P1IFG = 0; P1IE = BIT3;
    /* la UART por P1.1/P1.2 a 9600 baudios con SMCLK = 1 MHz [UG2 tabla 15-4] */
    P1SEL = BIT1 | BIT2; P1SEL2 = BIT1 | BIT2;
    UCA0CTL1 = UCSWRST | UCSSEL_2;
    UCA0BR0 = 104; UCA0BR1 = 0; UCA0MCTL = UCBRS_1;
    UCA0CTL1 &= ~UCSWRST;
    cad("hola G2553\n");
    /* la Flash: borrar INFO D y escribir una palabra (fFTG = 1 MHz/3 = 333 kHz) */
    FCTL2 = FWKEY | FSSEL_1 | FN1;
    FCTL3 = FWKEY;
    FCTL1 = FWKEY | ERASE;
    *(volatile unsigned*)0x1000 = 0;
    FCTL1 = FWKEY | WRT;
    *(volatile unsigned*)0x1000 = 0x1234;
    FCTL1 = FWKEY;
    FCTL3 = FWKEY | LOCK;
    cad(*(volatile unsigned*)0x1000 == 0x1234 && *(volatile unsigned*)0x1002 == 0xFFFF ? "flash ok\n" : "flash mal\n");
    /* el sensor de temperatura del ADC10 con la referencia de 1,5 V */
    ADC10CTL1 = INCH_10 | ADC10DIV_3;
    ADC10CTL0 = SREF_1 | ADC10SHT_3 | REFON | ADC10ON;
    ADC10CTL0 |= ENC | ADC10SC;
    while (ADC10CTL1 & ADC10BUSY) ;
    cad("adc "); dec(ADC10MEM); pon('\n');
    /* Timer0_A3: 1 ms con SMCLK; CCR0 cambia P1.0 */
    TA0CCR0 = 999;
    TA0CCTL0 = CCIE;
    TA0CTL = TASSEL_2 | MC_1 | TACLR;
    IE2 |= UCA0RXIE;
    __bis_SR_register(GIE);
    for (;;) {
        __bis_SR_register(LPM0_bits | GIE);
        if (ticks == 20) { cad("20 ms\n"); }
    }
}

__attribute__((interrupt(TIMER0_A0_VECTOR))) void t0(void) {
    P1OUT ^= BIT0;
    if (++ticks == 20) __bic_SR_register_on_exit(LPM0_bits);
}
__attribute__((interrupt(PORT1_VECTOR))) void p1(void) { P1IFG &= ~BIT3; P1OUT ^= BIT6; }
__attribute__((interrupt(USCIAB0RX_VECTOR))) void rx(void) { UCA0TXBUF = UCA0RXBUF + 1; }
