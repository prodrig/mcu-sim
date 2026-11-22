/* f5_blink.c — M9: el programa que el BSL USB carga en el F5529: LED1 (P1.0)
 * cambia cada ~50 ms (DCO de omisión, ~1 MHz). */
#include <msp430.h>
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    P1DIR |= BIT0;
    for (;;) { __delay_cycles(50000); P1OUT ^= BIT0; }
}
