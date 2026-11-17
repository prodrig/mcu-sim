/* f5_cbout.c — testf5, F11 (M4): CBOUT del Comp_B en P1.6 y en la captura
 * CCI1B del Timer1_A3. CB0 (P6.0) contra 1,5 V; TA1 en continuo con SMCLK
 * (1 MHz) captura los dos flancos de CBOUT en CCR1; el banco mueve P6.0 y
 * mira P1.6. Imprime las dos capturas y su diferencia en microsegundos. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void dec(unsigned v) { char b[6]; int i = 0; do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }

volatile unsigned cap[2], n_cap;

int main(void) {
    WDT_A_hold(WDT_A_BASE);
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 = UCSWRST | UCSSEL_2;
    UCA1BR0 = 109; UCA1BR1 = 0; UCA1MCTL = UCBRS_2;
    UCA1CTL1 &= ~UCSWRST;
    {
        Comp_B_initParam p = {0};
        Comp_B_configureReferenceVoltageParam r = {0};
        p.positiveTerminalInput = COMP_B_INPUT0;
        p.negativeTerminalInput = COMP_B_VREF;
        p.powerModeSelect = COMP_B_POWERMODE_NORMALMODE;
        p.outputFilterEnableAndDelayLevel = COMP_B_FILTEROUTPUT_OFF;
        p.invertedOutputPolarity = COMP_B_NORMALOUTPUTPOLARITY;
        Comp_B_init(COMP_B_BASE, &p);
        r.supplyVoltageReferenceBase = COMP_B_VREFBASE1_5V;
        r.lowerLimitSupplyVoltageFractionOf32 = 32;
        r.upperLimitSupplyVoltageFractionOf32 = 32;
        r.referenceAccuracy = COMP_B_ACCURACY_STATIC;
        Comp_B_configureReferenceVoltage(COMP_B_BASE, &r);
        Comp_B_enable(COMP_B_BASE);
    }
    P1DIR |= BIT6; P1SEL |= BIT6;                    /* CBOUT en P1.6 */
    TA1CCTL1 = CM_3 | CCIS_1 | SCS | CAP | CCIE;      /* CCI1B: los dos flancos */
    TA1CTL = TASSEL_2 | MC_2 | TACLR;
    __enable_interrupt();
    cad("listo\n");
    while (n_cap < 2) ;
    cad("cap "); dec(cap[1] - cap[0]); pon('\n');
    for (;;) ;
}

__attribute__((interrupt(TIMER1_A1_VECTOR))) void ta1(void) {
    switch (TA1IV) {
    case TA1IV_TA1CCR1: if (n_cap < 2) cap[n_cap++] = TA1CCR1; break;
    default: break;
    }
}
