/* fr23_basico.c — testfr2311 (M7): lo que hace un programa de la FR2311 con la
 * DriverLib de TI (la del paquete de software de la MSP-EXP430FR2311):
 *
 *   arranque  el FLL a 8 MHz desde REFO; la UCA0 (el backchannel, P1.7/P1.6)
 *             a 115200; el ID de la TLV.
 *   Timer_B   TB0 continuo con CNTL = 8 bits y TBIE: los desbordamientos en
 *             10 ms; TB1 en ascendente con TB1.1 en P2.0 (reset/set) y su
 *             latch con CLLD = 1: TB1CCR1 se lee al momento, la salida
 *             cambia en el siguiente periodo.
 *   eCOMP0    C0 (P1.0, que pone el banco) contra su DAC de 6 bits con VDD
 *             (CPDACBUF1 = 20, CPDACBUF2 = 30, CPDACSW); CPIFG cuando el
 *             banco sube C0.
 *   SAC0      el OA en modo GP con la red del banco: OA0+ (P1.4) a 0,5 V y
 *             ganancia 3 con 20k/10k en OA0-; el ADC lee OA0O (A3).
 *   TIA0      con Rf del banco entre TRI0O (P1.5) y TRI0- (P1.6) y una
 *             corriente en TRI0-: el ADC lee TRI0O (A5). P1.6 y P1.7 son
 *             la UART: se sueltan un rato.
 *
 * Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }
static void espera_ms(unsigned ms) { while (ms--) __delay_cycles(8000); }

volatile unsigned desb, cpirq;

static void uart(void) {
    EUSCI_A_UART_initParam p = {0};
    P1SEL0 |= BIT6 | BIT7; P1SEL1 &= ~(BIT6 | BIT7);
    p.selectClockSource = EUSCI_A_UART_CLOCKSOURCE_SMCLK;
    p.clockPrescalar = 4; p.firstModReg = 5; p.secondModReg = 0x55;        /* 115200 con SMCLK = 8 MHz */
    p.parity = EUSCI_A_UART_NO_PARITY; p.msborLsbFirst = EUSCI_A_UART_LSB_FIRST;
    p.numberofStopBits = EUSCI_A_UART_ONE_STOP_BIT; p.uartMode = EUSCI_A_UART_MODE;
    p.overSampling = EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION;
    EUSCI_A_UART_init(EUSCI_A0_BASE, &p);
    EUSCI_A_UART_enable(EUSCI_A0_BASE);
}
static unsigned adc(uint8_t canal) {
    ADC_init(ADC_BASE, ADC_SAMPLEHOLDSOURCE_SC, ADC_CLOCKSOURCE_ADCOSC, ADC_CLOCKDIVIDER_1);
    ADC_enable(ADC_BASE);
    ADC_setupSamplingTimer(ADC_BASE, ADC_CYCLEHOLD_16_CYCLES, ADC_MULTIPLESAMPLESDISABLE);
    ADC_configureMemory(ADC_BASE, canal, ADC_VREFPOS_AVCC, ADC_VREFNEG_AVSS);
    ADC_clearInterrupt(ADC_BASE, ADC_COMPLETED_INTERRUPT);
    ADC_startConversion(ADC_BASE, ADC_SINGLECHANNEL);
    while (!(ADCIFG & ADCIFG0)) ;
    const unsigned r = ADC_getResults(ADC_BASE);
    ADC_disable(ADC_BASE);
    return r;
}

int main(void) {
    WDT_A_hold(WDT_A_BASE);
    CS_initClockSignal(CS_FLLREF, CS_REFOCLK_SELECT, CS_CLOCK_DIVIDER_1);
    CS_initClockSignal(CS_ACLK, CS_REFOCLK_SELECT, CS_CLOCK_DIVIDER_1);
    CS_initFLLSettle(8000, 244);
    uart();
    P2DIR |= BIT0;
    PMM_unlockLPM5();
    cad("hola FR2311 "); dec(CS_getMCLK() / 1000); cad(" kHz id "); hex(*(volatile uint16_t*)0x1A04, 4); pon('\n');

    /* Timer_B: TB0 continuo de 8 bits a 1 MHz (SMCLK / 8): un desbordamiento cada 256 us */
    __bis_SR_register(GIE);
    TB0CTL = TBSSEL__SMCLK | ID__8 | MC__CONTINUOUS | CNTL_3 | TBCLR | TBIE;
    espera_ms(10);
    TB0CTL = 0;
    cad("tb0 cntl8 "); dec(desb); pon('\n');
    /* TB1.1 en P2.0: periodo 1000 (8 kHz), CCR1 = 250 con CLLD = 1; luego 750 */
    P2SEL0 |= BIT0; P2SEL1 &= ~BIT0;
    TB1CCR0 = 1000 - 1;
    TB1CCTL1 = OUTMOD_7 | CLLD_1;
    TB1CCR1 = 250;
    TB1CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
    espera_ms(5);
    cad("tb1 25\n");
    espera_ms(5);
    TB1CCR1 = 750;
    cad("tb1 ccr1 "); dec(TB1CCR1); pon('\n');
    espera_ms(5);
    TB1CTL = 0; P2SEL0 &= ~BIT0; P2OUT &= ~BIT0;

    /* eCOMP0: C0 (P1.0) contra el DAC (VDD, BUF1 = 20, BUF2 = 30) */
    P1SEL0 |= BIT0; P1SEL1 |= BIT0;
    CPCTL0 = CPPEN | CPPSEL_0 | CPNEN | CPNSEL_6;
    CPDACCTL = CPDACEN | CPDACBUFS;                      /* VDD; el búfer, por CPDACSW */
    CPDACDATA = (30 << 8) | 20;
    CPCTL1 = CPEN | CPHSEL_1;
    __delay_cycles(80);
    cad("ecomp buf1 "); dec(CPCTL1 & CPOUT);
    CPDACCTL |= CPDACSW;
    __delay_cycles(80);
    cad(" buf2 "); dec(CPCTL1 & CPOUT); pon('\n');
    CPINT = 0;
    CPCTL1 |= CPIE;
    cad("ecomp espera\n");
    while (!cpirq) ;
    cad("ecomp irq "); dec(cpirq); cad(" out "); dec(CPCTL1 & CPOUT); pon('\n');
    CPCTL1 = 0;

    /* SAC0 en GP: OA0+ (P1.4), OA0- (P1.2), OA0O (P1.3), con la red del banco */
    P1SEL0 |= BIT2 | BIT3 | BIT4; P1SEL1 |= BIT2 | BIT3 | BIT4;
    SAC_OA_init(SAC0_BASE, SAC_OA_POSITIVE_INPUT_SOURCE_EXTERNAL, SAC_OA_NEGATIVE_INPUT_SOURCE_EXTERNAL);
    SAC_OA_selectPowerMode(SAC0_BASE, SAC_OA_POWER_MODE_HIGH_SPEED_HIGH_POWER);
    SAC_OA_enable(SAC0_BASE);
    SAC_enable(SAC0_BASE);
    __delay_cycles(200);
    cad("sac adc "); dec(adc(ADC_INPUT_A3)); pon('\n');
    SAC_OA_disable(SAC0_BASE);
    SAC_disable(SAC0_BASE);

    /* TIA0: P1.5 (TRI0O), P1.6 (TRI0-), P1.7 (TRI0+); la UART se suelta un rato */
    cad("tia\n");
    fin_tx();
    espera_ms(20);
    P1SEL0 |= BIT5 | BIT6 | BIT7; P1SEL1 |= BIT5 | BIT6 | BIT7;
    TRI0CTL = TRIEN;
    __delay_cycles(400);
    {
        const unsigned r = adc(ADC_INPUT_A5);
        TRI0CTL = 0;
        espera_ms(20);
        P1SEL1 &= ~(BIT5 | BIT6 | BIT7); P1SEL0 &= ~BIT5;
        cad("tia adc "); dec(r); pon('\n');
    }
    cad("fin\n");
    for (;;) __bis_SR_register(LPM0_bits);
}

__attribute__((interrupt(TIMER0_B1_VECTOR))) void tb0n(void) { if (TB0IV == TBIV__TBIFG) ++desb; }
__attribute__((interrupt(ECOMP0_VECTOR))) void ecomp(void) { if (CPIV == 2) ++cpirq; }
