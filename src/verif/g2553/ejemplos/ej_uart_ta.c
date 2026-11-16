/* ej_uart_ta.c — la UART por el Timer_A (ta_uart.h): un saludo y eco, con
 * LPM0 entre carácter y carácter. */
#include "ta_uart.h"
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    BCSCTL1 = CALBC1_1MHZ; DCOCTL = CALDCO_1MHZ;
    ta_uart_init();
    __bis_SR_register(GIE);
    ta_puts("TA UART\r\n");
    for (;;) {
        __bis_SR_register(LPM0_bits | GIE);
        if (ta_rx_listo) { ta_rx_listo = 0; ta_putc(ta_rx_byte); }
    }
}
