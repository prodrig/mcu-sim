/* fr2_blink.c — blink para cargar con el BSL de las FR2xx/FR4xx (M10): P1.0
 * cambia cada ~20 ms con el reloj de después del reset. Sirve, tal cual, para
 * la FR2433 (LED1), la FR4133 (LED2) y la FR2476 (LED1). AGPLv3, mcu-sim. */
#include <msp430.h>
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    P1OUT &= ~BIT0;
    P1DIR |= BIT0;
    for (;;) {
        P1OUT ^= BIT0;
        __delay_cycles(20000);
    }
}
