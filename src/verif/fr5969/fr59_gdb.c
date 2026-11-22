/* fr59_gdb.c — para msp430-elf-gdb (gdb_g2.py, M8): el TA0 con su rutina
 * (t0, ticks); al empezar, la MPU (los tres segmentos de lectura y
 * ejecución, sin escritura: un `load` tiene que apagarla) y la IPE sobre
 * 0x13000-0x13FFF con un secreto que el depurador no puede leer. */
#include <msp430.h>
#include <stdint.h>

volatile unsigned ticks;

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    /* El secreto, antes de encapsularlo */
    *(volatile uint16_t*)(uintptr_t)0x13000UL = 0xC0DE;
    MPUCTL0 = MPUPW;
    MPUIPSEGB1 = 0x1300; MPUIPSEGB2 = 0x1400;
    MPUIPC0 = MPUIPENA;
    MPUSEGB1 = 0x0800; MPUSEGB2 = 0x0C00;
    MPUSAM = MPUSEG1RE | MPUSEG1XE | MPUSEG2RE | MPUSEG2XE | MPUSEG3RE | MPUSEG3XE | MPUSEGIRE | MPUSEGIWE;
    MPUCTL0 = MPUPW | MPUENA;
    MPUCTL0_H = 0;
    P1DIR |= BIT0;
    TA0CCR0 = 1000;
    TA0CCTL0 = CCIE;
    TA0CTL = TASSEL__SMCLK | MC__UP | TACLR;
    __bis_SR_register(GIE);
    for (;;) { __no_operation(); }
}

__attribute__((interrupt(TIMER0_A0_VECTOR))) void t0(void) { ++ticks; P1OUT ^= BIT0; }
