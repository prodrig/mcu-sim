/* ej_adc_temp.c — el sensor de temperatura del ADC10 con la referencia de
 * 1,5 V (INCH_10), con la conversión de los ejemplos de TI de los G2xx3:
 * ºC = (ADC - 673)·423/1024. Lo saca por la USCI_A0 cada 100 ms (el
 * Timer1_A3 con VLO... aquí, SMCLK y CCR0 en modo ascendente). Escrito aquí. */
#include <msp430.h>
static void pon(char c) { while (!(IFG2 & UCA0TXIFG)) ; UCA0TXBUF = c; }
int main(void) {
    long t;
    WDTCTL = WDTPW | WDTHOLD;
    BCSCTL1 = CALBC1_1MHZ; DCOCTL = CALDCO_1MHZ;
    P1SEL = BIT1 | BIT2; P1SEL2 = BIT1 | BIT2;
    UCA0CTL1 |= UCSSEL_2; UCA0BR0 = 104; UCA0BR1 = 0; UCA0MCTL = UCBRS0; UCA0CTL1 &= ~UCSWRST;
    ADC10CTL1 = INCH_10 | ADC10DIV_3;
    ADC10CTL0 = SREF_1 | ADC10SHT_3 | REFON | ADC10ON | ADC10IE;
    for (;;) {
        ADC10CTL0 |= ENC | ADC10SC;
        __bis_SR_register(LPM0_bits | GIE);
        t = ((long)ADC10MEM - 673) * 423 / 1024;
        pon('T'); pon(' ');
        if (t < 0) { pon('-'); t = -t; }
        if (t >= 10) pon('0' + t / 10);
        pon('0' + t % 10); pon('\r'); pon('\n');
        __delay_cycles(100000);
    }
}
__attribute__((interrupt(ADC10_VECTOR))) void adc(void) { __bic_SR_register_on_exit(LPM0_bits); }
