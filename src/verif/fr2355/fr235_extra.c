/* fr235_extra.c — testfr2355 J4 y J5 (M7): lo que faltaba del FR2355
 * (registros y, para la ROM, sus tablas; -mlarge):
 *
 *   ROM       la DriverLib de la ROM en 0xFAC00 por las tablas de TI
 *             (rom_gcc.h, de rom_driverlib_fr235x.h): la versión, GPIO,
 *             CRC y MPY32 llamados por la tabla.
 *   IR        la modulación de SYSCFG1 en P1.7 (UCA0TXD): la portadora,
 *             TB0.2 a 38 kHz; la envolvente, TB1.2 a 0; el dato por
 *             IRDATA; ASK, FSK e IRPSEL.
 *   TB0OUTH   TB0.1 en P1.6 a alta impedancia con el eCOMP0 a 1 (TB0TRGSEL
 *             = 0) o con TB0TRG (P1.2) a 1 (TB0TRGSEL = 1).
 *   MFM       la UCB1 de esclava SPI del MFM, con SMCLK a 3 MHz (375 kbps):
 *             un paquete de 32 bytes por MFM.TX (P5.1), que arranca TB2.0,
 *             y otro que llega por MFM.RX (P5.0); el fin, por TB2 CCI0B.
 *
 * Todo sale por la UCA1 (P4.3) a 115200 en texto. */
#include <msp430.h>
#include <stdint.h>
#include "rom/rom_gcc.h"

/* Las constantes de la DriverLib que usan las llamadas (de sus cabeceras) */
#define GPIO_PORT_P3             3
#define GPIO_PIN0                0x0001
#define MPY32_MULTIPLY_UNSIGNED  0x00

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA1STATW & UCBUSY) ; }
static void espera_ms(unsigned ms) { while (ms--) __delay_cycles(24000); }

volatile uint8_t tx_mfm[32], rx_mfm[32];
volatile unsigned n_tx, n_rx, n_fin;

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    FRCTL0 = FRCTLPW | NWAITS_2;
    P2SEL1 |= BIT6 | BIT7;
    PM5CTL0 &= ~LOCKLPM5;
    do { CSCTL7 &= ~(XT1OFFG | DCOFFG); SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
    __bis_SR_register(SCG0);
    CSCTL3 |= SELREF__XT1CLK;
    CSCTL0 = 0;
    CSCTL1 = DCORSEL_7;
    CSCTL2 = FLLD_0 + 731;
    __delay_cycles(3);
    __bic_SR_register(SCG0);
    while (CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)) ;
    CSCTL4 = SELMS__DCOCLKDIV | SELA__XT1CLK;
    P4SEL0 |= BIT2 | BIT3;
    UCA1CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA1BRW = 13;
    UCA1MCTLW = (0x25 << 8) | UCOS16;
    UCA1CTLW0 &= ~UCSWRST;
    cad("extra FR2355\n");

    /* La ROM: la versión y tres módulos por sus tablas */
    {
        ROM_WDT_A_hold(WDT_A_BASE);
        const uint32_t v = ROM_DRIVERLIB_getVersion();
        ROM_GPIO_setAsOutputPin(GPIO_PORT_P3, GPIO_PIN0);
        ROM_GPIO_setOutputHighOnPin(GPIO_PORT_P3, GPIO_PIN0);
        const uint8_t p3 = P3OUT & BIT0;
        ROM_CRC_setSeed(CRC_BASE, 0xFFFF);
        ROM_CRC_set16BitData(CRC_BASE, 0x1234);
        ROM_CRC_set16BitData(CRC_BASE, 0xABCD);
        const uint16_t crc = ROM_CRC_getResult(CRC_BASE);
        CRCINIRES = 0xFFFF; CRCDI = 0x1234; CRCDI = 0xABCD;
        const uint16_t crc2 = CRCINIRES;
        ROM_MPY32_setOperandOne16Bit(MPY32_MULTIPLY_UNSIGNED, 1234);
        ROM_MPY32_setOperandTwo16Bit(5678);
        const uint32_t m = (uint32_t)ROM_MPY32_getResult();
        cad("rom v "); hex(v, 8); cad(" p3 "); dec(p3); cad(" crc "); dec(crc == crc2); cad(" mpy "); dec((long)m); pon('\n');
        P3OUT = 0; P3DIR = 0;
    }

    /* IR: P1.7 de UCA0TXD (la UCA0 en UART, quieta); TB0.2 la portadora a
     * 38 kHz (24 MHz / 632), TB1.2 a 0 */
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 13; UCA0MCTLW = (0x25 << 8) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
    P1SEL0 |= BIT7; P1SEL1 &= ~BIT7;
    TB0CCR0 = 632 - 1; TB0CCR2 = 316; TB0CCTL2 = OUTMOD_7;
    TB0CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
    TB1CCTL2 = OUTMOD_0;                                 /* OUT = 0 */
    SYSCFG1 = IREN | IRDSSEL | IRDATA;                   /* ASK, dato 1: la portadora */
    cad("ir ask1\n"); fin_tx(); espera_ms(2);
    SYSCFG1 = IREN | IRDSSEL;                            /* ASK, dato 0: nada */
    cad("ir ask0\n"); fin_tx(); espera_ms(2);
    SYSCFG1 = IREN | IRDSSEL | IRPSEL;                   /* invertida: a 1 */
    cad("ir inv\n"); fin_tx(); espera_ms(2);
    SYSCFG1 = IREN | IRDSSEL | IRMSEL;                   /* FSK, dato 0: la primera, la portadora */
    cad("ir fsk0\n"); fin_tx(); espera_ms(2);
    SYSCFG1 = IREN | IRDSSEL | IRMSEL | IRDATA;          /* FSK, dato 1: la segunda, TB1.2 a 0 */
    cad("ir fsk1\n"); fin_tx(); espera_ms(2);
    SYSCFG1 = 0;
    cad("ir fin\n"); fin_tx(); espera_ms(2);
    P1SEL0 &= ~BIT7;

    /* TB0OUTH: TB0.1 en P1.6 (P1SEL = 10), un PWM; el eCOMP0 con P1.0
     * contra la de 1,2 V */
    P1DIR |= BIT6; P1SEL1 |= BIT6; P1SEL0 &= ~BIT6;
    TB0CCR1 = 316; TB0CCTL1 = OUTMOD_7;
    P1SEL0 |= BIT0; P1SEL1 |= BIT0;
    CP0CTL0 = CPPEN | CPPSEL_0 | CPNEN | CPNSEL_2;
    CP0CTL1 = CPEN;
    cad("outh ecomp\n"); fin_tx(); espera_ms(4);
    CP0CTL1 = 0;
    SYSCFG2 |= TB0TRGSEL;                                /* el pin TB0TRG (P1.2, 10) */
    P1DIR &= ~BIT2; P1SEL1 |= BIT2; P1SEL0 &= ~BIT2;
    cad("outh pin\n"); fin_tx(); espera_ms(4);
    SYSCFG2 &= ~TB0TRGSEL; P1SEL1 &= ~(BIT2 | BIT6);
    TB0CTL = 0;

    /* MFM: SMCLK a 3 MHz (DIVS /8: la UART se para hasta volver) */
    for (unsigned i = 0; i < 32; ++i) tx_mfm[i] = (uint8_t)(0x80 | (i * 7));
    UCB1CTLW0 = UCSWRST | UCSYNC | UCMODE_2 | UCMSB;     /* esclava SPI de 4 hilos */
    UCB1CTLW0 &= ~UCSWRST;
    UCB1TXBUF = tx_mfm[0]; n_tx = 1;
    cad("mfm\n"); fin_tx();
    CSCTL5 = DIVS__8;
    P5SEL1 |= BIT0 | BIT1; P5SEL0 &= ~(BIT0 | BIT1);
    UCB1IE = UCTXIE | UCRXIE;
    __bis_SR_register(GIE);
    /* TB2.0: el flanco que arranca la transmisión (OUTMOD 1, set) */
    TB2CCTL0 = OUTMOD_1;
    TB2CCR0 = 30;
    TB2CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
    while (!(TB2CCTL0 & CCIFG)) ;
    /* y después TB2 CCR0 de captura de CCI0B: el fin del paquete */
    TB2CCTL0 = CM_1 | CCIS_1 | CAP | SCS | CCIE;
    TB2CTL = TBSSEL__SMCLK | MC__CONTINUOUS | TBCLR;
    while (n_fin < 1) ;
    n_rx = 0;
    /* la recepción: el banco manda su paquete por P5.0 */
    P3DIR |= BIT1; P3OUT |= BIT1;                        /* «listo», para el banco */
    while (n_fin < 2) ;
    P3OUT &= ~BIT1;
    UCB1IE = 0;
    CSCTL5 = DIVS__1;
    __delay_cycles(1000);
    cad("mfm tx "); dec(n_tx); cad(" rx "); dec(n_rx);
    {
        uint16_t s = 0;
        for (unsigned i = 0; i < 32; ++i) s = (uint16_t)(s * 31 + rx_mfm[i]);
        cad(" suma "); hex(s, 4); cad(" r0 "); hex(rx_mfm[0], 2); cad(" r31 "); hex(rx_mfm[31], 2);
    }
    pon('\n');
    cad("fin\n");
    for (;;) __bis_SR_register(LPM0_bits);
}

__attribute__((interrupt(EUSCI_B1_VECTOR))) void ucb1(void) {
    switch (UCB1IV) {
    case UCIV__UCRXIFG: {
        const uint8_t b = UCB1RXBUF;
        if (n_fin >= 1 && n_rx < 32) rx_mfm[n_rx++] = b;
        break;
    }
    case UCIV__UCTXIFG:
        if (n_tx < 32) UCB1TXBUF = tx_mfm[n_tx++];
        else UCB1IE &= ~UCTXIE;
        break;
    default: break;
    }
}
__attribute__((interrupt(TIMER2_B0_VECTOR))) void tb2(void) { ++n_fin; }
