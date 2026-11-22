/* f5_rtc_cnt.c — testf5, F10 (M4): el RTC_A en modo contador, con la DriverLib.
 *   A: el contador de 32 bits desde ACLK (REFO) con el evento de 8 bits:
 *      cuatro RTCTEVIFG en LPM3, cada 256/32768 s, y RTCNT = 0x04xx.
 *   B: la cadena entera: RT0PS de ACLK ÷256 (128 Hz), RT1PS de RT0PS ÷128
 *      (1 Hz) y el contador de RT1PS, desde 0xFFFFFFFE con el evento de 32
 *      bits; RT1PSIFG cada 128 cuentas de RT1PS (1 s). Al segundo, RT1PSIFG
 *      y RTCNT = 0xFFFFFFFF; a los dos, el desborde de 32 bits.
 *   C: RTCHOLD para el contador.
 *   Con el DCO de fábrica (SMCLK = 1 MHz), la UCA1 a 9600; ACLK = REFO. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex8(uint32_t v) { int i; for (i = 28; i >= 0; i -= 4) pon("0123456789ABCDEF"[(v >> i) & 15]); }
static uint32_t cnt(void) { return ((uint32_t)RTCTIM1 << 16) | RTCTIM0; }

volatile unsigned n_tev, n_ps1;

int main(void) {
    uint32_t a, b;
    WDT_A_hold(WDT_A_BASE);
    UCS_initClockSignal(UCS_ACLK, UCS_REFOCLK_SELECT, UCS_CLOCK_DIVIDER_1);
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 = UCSWRST | UCSSEL_2;
    UCA1BR0 = 109; UCA1BR1 = 0; UCA1MCTL = UCBRS_2;
    UCA1CTL1 &= ~UCSWRST;
    __enable_interrupt();

    /* A */
    RTC_A_initCounter(RTC_A_BASE, RTC_A_CLOCKSELECT_ACLK, RTC_A_COUNTERSIZE_8BIT);
    RTC_A_setCounterValue(RTC_A_BASE, 0);
    RTC_A_clearInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT);
    RTC_A_enableInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT);
    cad("A\n");
    RTC_A_startClock(RTC_A_BASE);
    while (n_tev < 4) __bis_SR_register(LPM3_bits | GIE);
    a = cnt();
    cad("tev8 "); hex8(a); pon('\n');

    /* B */
    RTC_A_holdClock(RTC_A_BASE);
    RTC_A_disableInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT);
    RTC_A_initCounter(RTC_A_BASE, RTC_A_CLOCKSELECT_RT1PS, RTC_A_COUNTERSIZE_32BIT);
    RTC_A_setCounterValue(RTC_A_BASE, 0xFFFFFFFEul);
    RTC_A_initCounterPrescale(RTC_A_BASE, RTC_A_PRESCALE_0, RTC_A_PSCLOCKSELECT_ACLK, RTC_A_PSDIVIDER_256);
    RTC_A_initCounterPrescale(RTC_A_BASE, RTC_A_PRESCALE_1, RTC_A_PSCLOCKSELECT_RT0PS, RTC_A_PSDIVIDER_128);
    RTCPS = 0;
    RTC_A_definePrescaleEvent(RTC_A_BASE, RTC_A_PRESCALE_1, RTC_A_PSEVENTDIVIDER_128);
    RTC_A_clearInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT | RTC_A_PRESCALE_TIMER1_INTERRUPT);
    RTC_A_enableInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT | RTC_A_PRESCALE_TIMER1_INTERRUPT);
    n_tev = 0;
    cad("B\n");
    RTC_A_startClock(RTC_A_BASE);
    while (n_ps1 < 1) __bis_SR_register(LPM3_bits | GIE);
    cad("ps1 "); hex8(cnt()); pon('\n');
    while (n_tev < 1) __bis_SR_register(LPM3_bits | GIE);
    cad("tev32 "); hex8(cnt()); pon('\n');

    /* C */
    RTC_A_holdClock(RTC_A_BASE);
    a = cnt();
    __delay_cycles(2000000);
    b = cnt();
    cad(a == b ? "hold ok\n" : "hold mal\n");
    for (;;) __bis_SR_register(LPM4_bits);
}

__attribute__((interrupt(RTC_VECTOR))) void rtc(void) {
    switch (RTCIV) {
    case RTCIV_RTCTEVIFG: ++n_tev; break;
    case RTCIV_RT1PSIFG: ++n_ps1; break;
    default: break;
    }
    __bic_SR_register_on_exit(LPM3_bits);
}
