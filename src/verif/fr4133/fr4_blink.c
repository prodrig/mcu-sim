/* fr4_blink.c — blink para cargar con el BSL de las FR4xx (M10): LED1 (P4.0)
 * cambia cada ~20 ms con el reloj de después del reset. No toca P1.0/P1.1:
 * son la UART del BSL, que los deja con su función. AGPLv3, mcu-sim. */
#include <msp430.h>
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    P4OUT &= ~BIT0;
    P4DIR |= BIT0;
    for (;;) {
        P4OUT ^= BIT0;
        __delay_cycles(20000);
    }
}
