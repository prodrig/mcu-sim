/* ta_uart.h — una UART de 9600 baudios con el Timer0_A3 (SMCLK = 1 MHz), con
 * los pines de la G2ET: TXD en P1.2 (TA0.1, CCR1) y RXD en P1.1 (CCI0A,
 * CCR0), que son los que el eZ-FET lleva al PC. Es el esquema del ejemplo de
 * TI de la UART por el Timer_A de los G2xx: cada bit, una comparación; la
 * salida la pone OUTMOD (1, marca; 5, espacio); la recepción captura el
 * flanco de bajada del arranque y muestrea SCCI a mitad de cada bit.
 * Escrito aquí. */
#ifndef TA_UART_H
#define TA_UART_H
#include <msp430.h>
#ifndef UART_TBIT
#define UART_TBIT        104      /* SMCLK / 9600, con SMCLK = 1 MHz */
#endif
#define UART_TBIT_DIV_2  (UART_TBIT / 2)
static volatile unsigned ta_tx_dato;
static volatile unsigned char ta_tx_bits;
static volatile unsigned char ta_rx_bits, ta_rx_dato, ta_rx_listo, ta_rx_byte;

static void ta_uart_init(void) {
    TA0CCTL1 = OUT;                                   /* TXD en reposo: marca */
    TA0CCTL0 = SCS | CM_2 | CAP | CCIE;               /* RXD: el flanco del arranque */
    TA0CTL = TASSEL_2 | MC_2 | TACLR;
    P1SEL |= BIT1 | BIT2; P1SEL2 &= ~(BIT1 | BIT2);
    P1DIR |= BIT2; P1DIR &= ~BIT1;
}
static void ta_putc(unsigned char c) {
    while (TA0CCTL1 & CCIE) ;
    ta_tx_bits = 10;
    TA0CCR1 = TA0R + UART_TBIT;
    ta_tx_dato = ((unsigned)c | 0x100) << 1;
    TA0CCTL1 = OUTMOD_1 | CCIE;
}
static void ta_puts(const char* s) { while (*s) ta_putc((unsigned char)*s++); }

__attribute__((interrupt(TIMER0_A1_VECTOR))) void ta_uart_tx_isr(void) {
    switch (TA0IV) {
    case TA0IV_TACCR1:
        TA0CCR1 += UART_TBIT;
        if (ta_tx_bits == 0) { TA0CCTL1 &= ~CCIE; break; }
        if (ta_tx_dato & 1) TA0CCTL1 &= ~OUTMOD2; else TA0CCTL1 |= OUTMOD2;
        ta_tx_dato >>= 1;
        --ta_tx_bits;
        break;
    default: break;
    }
}
__attribute__((interrupt(TIMER0_A0_VECTOR))) void ta_uart_rx_isr(void) {
    TA0CCR0 += UART_TBIT;
    if (TA0CCTL0 & CAP) {                             /* el arranque */
        TA0CCTL0 &= ~CAP;
        TA0CCR0 += UART_TBIT_DIV_2;
        ta_rx_bits = 8; ta_rx_dato = 0;
    } else if (ta_rx_bits) {
        ta_rx_dato >>= 1;
        if (TA0CCTL0 & SCCI) ta_rx_dato |= 0x80;
        if (--ta_rx_bits == 0) {
            ta_rx_byte = ta_rx_dato; ta_rx_listo = 1;
            TA0CCTL0 |= CAP;                         /* al siguiente arranque */
            __bic_SR_register_on_exit(LPM0_bits);
        }
    }
}
#endif
