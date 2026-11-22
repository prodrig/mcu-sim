/* g2_wdt.c — el perro guardián del G2553 (testg2): sin refrescarlo, PUC; tras
 * el PUC, WDTIFG en IFG1 */
#include <msp430.h>
static void pon(char c) { while (!(IFG2 & UCA0TXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
int main(void) {
    const unsigned char f = IFG1;
    WDTCTL = WDTPW | WDTHOLD;
    BCSCTL1 = CALBC1_1MHZ; DCOCTL = CALDCO_1MHZ;
    P1SEL = BIT1 | BIT2; P1SEL2 = BIT1 | BIT2;
    UCA0CTL1 = UCSWRST | UCSSEL_2; UCA0BR0 = 104; UCA0BR1 = 0; UCA0MCTL = UCBRS_1; UCA0CTL1 &= ~UCSWRST;
    if (f & WDTIFG) { cad("reinicio por WDT\n"); IFG1 &= ~WDTIFG; while (UCA0STAT & UCBUSY) ; for (;;) __bis_SR_register(LPM4_bits); }
    cad("perro en marcha\n");
    WDTCTL = WDTPW | WDTCNTCL | WDTIS0;    /* SMCLK/8192: 8,192 ms */
    for (;;) ;
}
