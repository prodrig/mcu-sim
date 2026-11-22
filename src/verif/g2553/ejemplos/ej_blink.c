/* ej_blink.c — «Blink LED» de la MSP-EXP430G2ET [SLAU772A §3.2]: el perro
 * parado, P1.0 de salida, y P1.0 cambia en un bucle de espera por software.
 * Escrito aquí a partir de la descripción de la guía (el proyecto de TI no
 * está en doc/refs). */
#include <msp430.h>
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    P1DIR |= BIT0;
    for (;;) {
        volatile unsigned i;
        P1OUT ^= BIT0;
        for (i = 10000; i; --i) ;
    }
}
