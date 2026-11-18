/* fr69_errata.c — testfr5 (M8): las erratas del FR6989 que se modelan y no
 * tiene el FR5969 [SLAZ517AG] (registros; -mlarge):
 *
 *   TA22   las salidas de TA0 cambian al entrar en LPM3: TA0.1 en P1.6 (11),
 *          a 1 con el Timer parado (OUTMOD 4, sin comparaciones), pasa a 0
 *          en LPM3 (el banco lo mira).
 *   RTC12  RTCTCOK (RTCTCMP) se pierde al despertar de LPM3.5: se escribe
 *          RTCTCMP (RTCTCOK a 1), LPM3.5 con el RTC_C y el LFXT, y despierta
 *          (RTCRDYIE) con RTCTCOK a 0.
 *
 * Todo sale por la UART del backchannel (UCA1 en P3.4/P3.5, 115200). */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void fin_tx(void) { while (UCA1STATW & UCBUSY) ; }

__attribute__((persistent)) volatile uint16_t etapa = 0;
static volatile unsigned despiertos;

__attribute__((interrupt(TIMER1_A0_VECTOR))) void ta1(void) { ++despiertos; __bic_SR_register_on_exit(LPM3_bits); }
__attribute__((interrupt(RTC_VECTOR))) void rtc(void) { (void)RTCIV; }

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    const uint16_t rst = SYSRSTIV;
    PJSEL0 |= BIT4 | BIT5;
    PM5CTL0 &= ~LOCKLPM5;
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                     /* 8 MHz */
    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL4 &= ~LFXTOFF;
    do { CSCTL5 &= ~LFXTOFFG; SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
    CSCTL0_H = 0;
    P3SEL0 |= BIT4 | BIT5; P3SEL1 &= ~(BIT4 | BIT5);
    UCA1CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA1BRW = 4;
    UCA1MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA1CTLW0 &= ~UCSWRST;
    if (etapa == 3) {                                       /* tras LPM3.5 */
        etapa = 0;
        cad("rtc12 despierta rstiv "); hex(rst, 2); cad(" tcmp "); hex(RTCTCMP, 4); pon('\n');
        fin_tx();
        for (;;) ;
    }
    cad("hola fr6989\n");

    /* TA22: TA0.1 en P1.6 (11) a 1, el Timer parado */
    {
        TA0CCTL1 = OUTMOD_0 | OUT;
        TA0CCTL1 = OUTMOD_4 | OUT;
        P1DIR |= BIT6; P1SEL0 |= BIT6; P1SEL1 |= BIT6;
        cad("ta22 activo\n");
        fin_tx();
        __delay_cycles(80000);                              /* 10 ms en activo */
        TA1CTL = TASSEL__ACLK | MC__CONTINUOUS | TACLR;
        TA1CCR0 = 1638;                                     /* 50 ms */
        TA1CCTL0 = CCIE;
        __bis_SR_register(LPM3_bits | GIE);
        __no_operation();
        TA1CCTL0 = 0; TA1CTL = 0;
        cad("ta22 fin\n");
    }
    /* RTC12: RTCTCMP con su RTCTCOK, y LPM3.5 */
    {
        RTCCTL0_H = RTCKEY_H;
        RTCCTL13 = RTCHOLD | RTCMODE;
        RTCTIM0 = 0x0000;
        RTCCTL13 = RTCMODE;
        RTCTCMP = RTCTCMPS | 5;
        const uint16_t tcmp = RTCTCMP;
        RTCCTL0_L = RTCRDYIE;
        RTCCTL0_H = 0;
        cad("rtc12 tcmp "); hex(tcmp, 4); pon('\n');
        fin_tx();
        etapa = 3;
        PMMCTL0_H = PMMPW_H;
        PMMCTL0_L |= PMMREGOFF;
        __bis_SR_register(LPM3_bits);
        etapa = 0;
        cad("rtc12 no entra\n");
    }
    for (;;) ;
}
