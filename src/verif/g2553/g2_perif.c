/* g2_perif.c — testg2, G6 (P-20): los módulos del G2553 que M3 dejó
 * declarados, y el resto de funciones de los pines.
 *   1  USCI_B0 en SPI maestro con UCLISTEN (el lazo interno): tres bytes
 *   2  USCI_B0 en I2C maestro contra un OPT3001 (0x44) en P1.6/P1.7: sus dos
 *      identificadores (0x7E y 0x7F) con escritura + reinicio + lectura, y
 *      NACK de una dirección que no está (0x45)
 *   3  Comp_A+: CA0 (P1.0) contra 0,5·Vcc; CAOUT y la interrupción del flanco
 *   4  ADC10 con la DTC: la secuencia A3..A0 con MSC, a un bloque de 4
 *   5  Timer1_A3: captura en CCI0A (P2.0) de un 1 kHz de fuera; y TA0.1 en
 *      P1.6 (PWM al 25 % con CCR0 = 999)
 * Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(IFG2 & UCA0TXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(unsigned v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(unsigned v) { char b[6]; int i = 0; do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }

static unsigned i2c_lee16(unsigned char reg) {
    unsigned char hi, lo;
    UCB0CTL1 |= UCTR | UCTXSTT;                     /* escritura: el registro */
    while (!(IFG2 & UCB0TXIFG)) ;
    UCB0TXBUF = reg;
    while (!(IFG2 & UCB0TXIFG)) ;                   /* ya ha salido (y el siguiente puede ir) */
    UCB0CTL1 &= ~UCTR;
    UCB0CTL1 |= UCTXSTT;                            /* reinicio: lectura */
    while (UCB0CTL1 & UCTXSTT) ;
    while (!(IFG2 & UCB0RXIFG)) ;
    hi = UCB0RXBUF;
    UCB0CTL1 |= UCTXSTP;                            /* el último lleva la parada */
    while (!(IFG2 & UCB0RXIFG)) ;
    lo = UCB0RXBUF;
    while (UCB0CTL1 & UCTXSTP) ;
    return ((unsigned)hi << 8) | lo;
}

volatile unsigned char ca_irq;
static unsigned adc[4];

int main(void) {
    unsigned i, a, b;
    WDTCTL = WDTPW | WDTHOLD;
    BCSCTL1 = CALBC1_1MHZ; DCOCTL = CALDCO_1MHZ;
    P1SEL = BIT1 | BIT2; P1SEL2 = BIT1 | BIT2;
    UCA0CTL1 = UCSWRST | UCSSEL_2; UCA0BR0 = 104; UCA0BR1 = 0; UCA0MCTL = UCBRS_1; UCA0CTL1 &= ~UCSWRST;

    /* 1: SPI */
    P1SEL |= BIT5 | BIT6 | BIT7; P1SEL2 |= BIT5 | BIT6 | BIT7;
    UCB0CTL1 = UCSWRST | UCSSEL_2;
    UCB0CTL0 = UCCKPH | UCMSB | UCMST | UCSYNC;
    UCB0BR0 = 4; UCB0BR1 = 0;
    UCB0STAT = UCLISTEN;
    UCB0CTL1 &= ~UCSWRST;
    cad("spi");
    {
        static const unsigned char t[3] = {0xA5, 0x3C, 0x81};
        for (i = 0; i < 3; ++i) {
            while (!(IFG2 & UCB0TXIFG)) ;
            UCB0TXBUF = t[i];
            while (!(IFG2 & UCB0RXIFG)) ;
            pon(' '); hex(UCB0RXBUF, 2);
        }
    }
    pon('\n');

    /* 2: I2C */
    UCB0CTL1 = UCSWRST;
    UCB0CTL0 = UCMST | UCMODE_3 | UCSYNC;
    UCB0CTL1 = UCSSEL_2 | UCSWRST;
    UCB0BR0 = 10; UCB0BR1 = 0;                       /* 100 kHz */
    UCB0STAT = 0;
    UCB0I2CSA = 0x44;
    UCB0CTL1 &= ~UCSWRST;
    a = i2c_lee16(0x7E);
    b = i2c_lee16(0x7F);
    cad("i2c "); hex(a, 4); pon(' '); hex(b, 4);
    UCB0I2CSA = 0x45;
    UCB0CTL1 |= UCTR | UCTXSTT;
    while ((UCB0CTL1 & UCTXSTT) && !(UCB0STAT & UCNACKIFG)) ;
    if (UCB0STAT & UCNACKIFG) { cad(" nack"); UCB0CTL1 |= UCTXSTP; UCB0STAT &= ~UCNACKIFG; while (UCB0CTL1 & UCTXSTP) ; }
    pon('\n');
    UCB0CTL1 = UCSWRST;
    P1SEL &= ~(BIT5 | BIT6 | BIT7); P1SEL2 &= ~(BIT5 | BIT6 | BIT7);

    /* 3: Comp_A+ */
    CAPD = BIT0;
    CACTL2 = P2CA0;                                  /* + : CA0 (P1.0) */
    CACTL1 = CARSEL | CAREF_2 | CAON;                /* - : 0,5·Vcc */
    cad("ca "); pon((CACTL2 & CAOUT) ? '1' : '0');
    CACTL1 |= CAIE;
    CACTL1 &= ~CAIFG;
    __bis_SR_register(GIE);
    pon('\n');
    while (!ca_irq) ;
    cad("ca irq "); pon((CACTL2 & CAOUT) ? '1' : '0'); pon('\n');
    CACTL1 = 0;

    /* 4: ADC10 + DTC */
    ADC10CTL1 = INCH_3 | CONSEQ_1;
    ADC10CTL0 = ADC10SHT_2 | MSC | ADC10ON;
    ADC10AE0 = BIT0 | BIT3;
    ADC10DTC1 = 4;
    ADC10SA = (unsigned)adc;
    ADC10CTL0 |= ENC | ADC10SC;
    while (ADC10CTL1 & ADC10BUSY) ;
    cad("adc");
    for (i = 0; i < 4; ++i) { pon(' '); dec(adc[i]); }
    pon(ADC10CTL0 & ADC10IFG ? '+' : '-');
    pon('\n');

    /* 5: captura y PWM */
    P1DIR |= BIT6; P1SEL |= BIT6;                     /* TA0.1 */
    TA0CCR0 = 999; TA0CCR1 = 250; TA0CCTL1 = OUTMOD_7;
    TA0CTL = TASSEL_2 | MC_1 | TACLR;
    P2SEL |= BIT0;                                    /* CCI0A de TA1 */
    TA1CCTL0 = CM_1 | CCIS_0 | SCS | CAP;
    TA1CTL = TASSEL_2 | MC_2 | TACLR;
    TA1CCTL0 &= ~CCIFG;
    while (!(TA1CCTL0 & CCIFG)) ;
    a = TA1CCR0; TA1CCTL0 &= ~CCIFG;
    while (!(TA1CCTL0 & CCIFG)) ;
    b = TA1CCR0;
    cad("cap "); dec(b - a); pon('\n');
    for (;;) __bis_SR_register(LPM0_bits);
}

__attribute__((interrupt(COMPARATORA_VECTOR))) void ca(void) { CACTL1 &= ~CAIFG; ca_irq = 1; }
