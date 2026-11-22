/* fr59_blink.c — M9: el programa que el BSL carga y arranca en el FR5969:
 * LED1 (P1.0) y LED2 (P4.6) cambian cada ~50 ms (DCO a 1 MHz de omisión).
 * Antes deja en 0x1C00 y 0x1C02 lo que lee del BSL: la Z-Area (0x1000) se
 * ve; 0x1010, no (0x3FFF) [BSLFR §3.2.1.1]. */
#include <msp430.h>
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    *(volatile unsigned*)0x1C00 = *(volatile unsigned*)0x1000;
    *(volatile unsigned*)0x1C02 = *(volatile unsigned*)0x1010;
    P1DIR |= BIT0; P4DIR |= BIT6;
    P1OUT &= ~BIT0; P4OUT |= BIT6;
    PM5CTL0 &= ~LOCKLPM5;
    for (;;) {
        __delay_cycles(50000);
        P1OUT ^= BIT0; P4OUT ^= BIT6;
    }
}
