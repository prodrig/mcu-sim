/* ej_uart_usci.c — la UART de la USCI_A0 a 9600 con el DCO calibrado a
 * 1 MHz, eco de lo que llega con LPM0 y la interrupción de RX: lo que hace el
 * ejemplo clásico de TI de los G2xx3 (USCI_A0, UART 9600, eco). Escrito aquí. */
#include <msp430.h>
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    BCSCTL1 = CALBC1_1MHZ; DCOCTL = CALDCO_1MHZ;
    P1SEL = BIT1 | BIT2; P1SEL2 = BIT1 | BIT2;
    UCA0CTL1 |= UCSSEL_2;
    UCA0BR0 = 104; UCA0BR1 = 0; UCA0MCTL = UCBRS0;
    UCA0CTL1 &= ~UCSWRST;
    IE2 |= UCA0RXIE;
    __bis_SR_register(LPM0_bits | GIE);
    return 0;
}
__attribute__((interrupt(USCIAB0RX_VECTOR))) void rx(void) {
    while (!(IFG2 & UCA0TXIFG)) ;
    UCA0TXBUF = UCA0RXBUF;
}
