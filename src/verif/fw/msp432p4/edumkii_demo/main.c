/* ===========================================================================
 * edumkii_demo — La BOOSTXL-EDUMKII sobre la MSP-EXP432P401R
 *
 * Lo que haria una primera practica con la placa, con la cabecera de TI y
 * nada mas (placas/msp_exp432p401r_edumkii.xml):
 *
 *   - MCLK y SMCLK a 12 MHz (DCO, VCORE0, un estado de espera)
 *   - la pantalla por la UCB0 en SPI: la despierta y pinta un fondo y un
 *     recuadro del color de lo que se pulsa
 *   - el OPT3001 y el TMP006 por la UCB1 en I2C, en continuo
 *   - cada 100 ms: el joystick (A15 y A9), el eje Z del acelerometro (A11),
 *     la luz y la temperatura del chip del TMP006, por el backchannel
 *     (UCA0, 115200 8N1), en una linea:
 *         joy=2048,2048 acc_z=2867 lux=300.00 t=25.00 s1=0 s2=0
 *   - el LED RGB: rojo con S1, verde con S2, azul con el joystick a la
 *     derecha; y el zumbador a 2 kHz con el pulsador del joystick
 * ===========================================================================*/
#ifndef NO_MSP_CLASSIC_DEFINES
#define NO_MSP_CLASSIC_DEFINES
#endif
#include "msp432p401r.h"

uint32_t SystemCoreClock = 12000000;
void SystemInit(void) {}

/* --- Tiempo: el Timer32_2 de un disparo, a 12 MHz ---------------------------- */
static void espera_us(uint32_t us) {
    TIMER32_2->CONTROL = 0;
    TIMER32_2->LOAD = us * 12u;
    TIMER32_2->INTCLR = 1;
    TIMER32_2->CONTROL = TIMER32_CONTROL_SIZE | TIMER32_CONTROL_ONESHOT | TIMER32_CONTROL_ENABLE;
    while (!TIMER32_2->RIS) { }
}

/* --- El backchannel ---------------------------------------------------------- */
static void uart_pon(char c) {
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_TXIFG)) { }
    EUSCI_A0->TXBUF = (uint8_t)c;
}
static void uart_texto(const char* s) { while (*s) uart_pon(*s++); }
static void uart_num(uint32_t v) {
    char b[11]; int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) uart_pon(b[--n]);
}
static void uart_fijo2(uint32_t centesimas) {      /* 1234 -> "12.34" */
    uart_num(centesimas / 100);
    uart_pon('.');
    uart_pon((char)('0' + (centesimas / 10) % 10));
    uart_pon((char)('0' + centesimas % 10));
}

/* --- I2C por la UCB1 -------------------------------------------------------- */
static void i2c_esc(uint8_t a, uint8_t r, uint16_t v) {
    EUSCI_B1->I2CSA = a;
    EUSCI_B1->IFG = 0;
    EUSCI_B1->CTLW0 |= EUSCI_B_CTLW0_TR | EUSCI_B_CTLW0_TXSTT;
    const uint8_t d[3] = { r, (uint8_t)(v >> 8), (uint8_t)v };
    for (int i = 0; i < 3; ++i) {
        while (!(EUSCI_B1->IFG & EUSCI_B_IFG_TXIFG0)) { }
        EUSCI_B1->TXBUF = d[i];
    }
    while (!(EUSCI_B1->IFG & EUSCI_B_IFG_TXIFG0)) { }
    EUSCI_B1->CTLW0 |= EUSCI_B_CTLW0_TXSTP;
    while (EUSCI_B1->CTLW0 & EUSCI_B_CTLW0_TXSTP) { }
}
static uint16_t i2c_lee(uint8_t a, uint8_t r) {
    EUSCI_B1->I2CSA = a;
    EUSCI_B1->IFG = 0;
    EUSCI_B1->CTLW0 |= EUSCI_B_CTLW0_TR | EUSCI_B_CTLW0_TXSTT;
    while (!(EUSCI_B1->IFG & EUSCI_B_IFG_TXIFG0)) { }
    EUSCI_B1->TXBUF = r;
    while (EUSCI_B1->CTLW0 & EUSCI_B_CTLW0_TXSTT) { }
    while (!(EUSCI_B1->IFG & EUSCI_B_IFG_TXIFG0)) { }
    EUSCI_B1->CTLW0 &= ~EUSCI_B_CTLW0_TR;
    EUSCI_B1->CTLW0 |= EUSCI_B_CTLW0_TXSTT;
    while (EUSCI_B1->CTLW0 & EUSCI_B_CTLW0_TXSTT) { }
    while (!(EUSCI_B1->IFG & EUSCI_B_IFG_RXIFG0)) { }
    const uint8_t hi = (uint8_t)EUSCI_B1->RXBUF;
    EUSCI_B1->CTLW0 |= EUSCI_B_CTLW0_TXSTP;
    while (!(EUSCI_B1->IFG & EUSCI_B_IFG_RXIFG0)) { }
    const uint8_t lo = (uint8_t)EUSCI_B1->RXBUF;
    while (EUSCI_B1->CTLW0 & EUSCI_B_CTLW0_TXSTP) { }
    return (uint16_t)((hi << 8) | lo);
}

/* --- La pantalla: CS P5.0, D/CX P3.7, RST P5.7, luz P2.6 ------------------- */
static void spi(uint8_t v) {
    while (!(EUSCI_B0->IFG & EUSCI_B_IFG_TXIFG)) { }
    EUSCI_B0->TXBUF = v;
}
static void spi_fin(void) { while (EUSCI_B0->STATW & EUSCI_B_STATW_SPI_BUSY) { } }
static void lcd_orden(uint8_t c) {
    spi_fin();
    P3->OUT &= ~BIT7; P5->OUT &= ~BIT0;
    spi(c);
    spi_fin();
    P3->OUT |= BIT7;
}
static void lcd_fin(void) { spi_fin(); P5->OUT |= BIT0; }
static void lcd_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c) {
    lcd_orden(0x2A); spi(0); spi(x); spi(0); spi((uint8_t)(x + w - 1));
    lcd_orden(0x2B); spi(0); spi(y); spi(0); spi((uint8_t)(y + h - 1));
    lcd_orden(0x2C);
    for (uint32_t n = (uint32_t)w * h; n > 0; --n) { spi((uint8_t)(c >> 8)); spi((uint8_t)c); }
    lcd_fin();
}

/* --- ADC14: una conversion de un canal, 12 bits contra AVCC ---------------- */
static uint32_t adc(uint32_t canal) {
    ADC14->CTL0 &= ~ADC14_CTL0_ENC;
    ADC14->MCTL[0] = canal;
    ADC14->CLRIFGR0 = 1;
    ADC14->CTL0 |= ADC14_CTL0_ENC | ADC14_CTL0_SC;
    while (!(ADC14->IFGR0 & 1)) { }
    return ADC14->MEM[0];
}

int main(void) {
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;
    /* 12 MHz: VCORE0 basta, con un estado de espera en la Flash */
    FLCTL->BANK0_RDCTL = (FLCTL->BANK0_RDCTL & ~FLCTL_BANK0_RDCTL_WAIT_MASK) | FLCTL_BANK0_RDCTL_WAIT_1;
    FLCTL->BANK1_RDCTL = (FLCTL->BANK1_RDCTL & ~FLCTL_BANK1_RDCTL_WAIT_MASK) | FLCTL_BANK1_RDCTL_WAIT_1;
    CS->KEY = CS_KEY_VAL;
    CS->CTL0 = CS_CTL0_DCORSEL_3;
    CS->KEY = 0;

    /* El backchannel: UCA0 en P1.2/P1.3, 115200 con 12 MHz */
    P1->SEL0 |= BIT2 | BIT3;
    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW = 6;
    EUSCI_A0->MCTLW = EUSCI_A_MCTLW_OS16 | (8u << EUSCI_A_MCTLW_BRF_OFS) | (0x20u << EUSCI_A_MCTLW_BRS_OFS);
    EUSCI_A0->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
    uart_texto("BOOSTXL-EDUMKII\r\n");

    /* Los pulsadores, con su pull-up: S1 P5.1, S2 P3.5, el del joystick P4.1 */
    P5->REN |= BIT1; P5->OUT |= BIT1;
    P3->REN |= BIT5; P3->OUT |= BIT5;
    P4->REN |= BIT1; P4->OUT |= BIT1;
    /* El LED RGB y el zumbador, como GPIO: P2.6, P2.4, P5.6; P2.7 por TA0.4 */
    P2->DIR |= BIT4 | BIT6; P5->DIR |= BIT6;
    TIMER_A0->CCR[0] = 5999;                     /* 2 kHz con SMCLK = 12 MHz */
    TIMER_A0->CCR[4] = 3000;
    TIMER_A0->CCTL[4] = TIMER_A_CCTLN_OUTMOD_7;
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR;
    P2->DIR |= BIT7;

    /* Los analogicos: A15 (P6.0), A9 (P4.4), A11 (P4.2) */
    /* De GPIO a terciaria de una vez, con SELC: sin pasar por la primaria
       ni por la secundaria */
    P6->SELC = BIT0;
    P4->SELC = BIT4 | BIT2;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_2 | ADC14_CTL0_SSEL__MODCLK;
    ADC14->CTL1 = ADC14_CTL1_RES__12BIT;

    /* I2C: UCB1 en P6.4/P6.5 a 100 kHz; los dos sensores en continuo */
    P6->SEL0 |= BIT4 | BIT5;
    EUSCI_B1->CTLW0 = EUSCI_B_CTLW0_SWRST | EUSCI_B_CTLW0_MODE_3 | EUSCI_B_CTLW0_MST |
                      EUSCI_B_CTLW0_SYNC | EUSCI_B_CTLW0_SSEL__SMCLK;
    EUSCI_B1->BRW = 120;
    EUSCI_B1->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
    i2c_esc(0x44, 0x01, 0xC610);
    i2c_esc(0x40, 0x02, 0x7000);

    /* La pantalla: UCB0 en SPI a 6 MHz */
    P5->OUT |= BIT0 | BIT7; P5->DIR |= BIT0 | BIT7;
    P3->OUT |= BIT7; P3->DIR |= BIT7;
    P1->SEL0 |= BIT5 | BIT6;
    EUSCI_B0->CTLW0 = EUSCI_B_CTLW0_SWRST | EUSCI_B_CTLW0_CKPH | EUSCI_B_CTLW0_MSB |
                      EUSCI_B_CTLW0_MST | EUSCI_B_CTLW0_SYNC | EUSCI_B_CTLW0_SSEL__SMCLK;
    EUSCI_B0->BRW = 2;
    EUSCI_B0->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
    P5->OUT &= ~BIT7; espera_us(20); P5->OUT |= BIT7;
    espera_us(120000);
    lcd_orden(0x11); lcd_fin();
    espera_us(120000);
    lcd_orden(0x3A); spi(0x05); lcd_fin();
    lcd_orden(0x36); spi(0x00); lcd_fin();
    lcd_orden(0x29); lcd_fin();
    lcd_rect(0, 0, 128, 128, 0x0010);            /* fondo azul oscuro */

    uint16_t ultimo = 0xFFFF;
    for (;;) {
        const uint32_t jx = adc(ADC14_MCTLN_INCH_15), jy = adc(ADC14_MCTLN_INCH_9);
        const uint32_t az = adc(ADC14_MCTLN_INCH_11);
        const uint16_t r = i2c_lee(0x44, 0x00);
        const uint32_t lux = (uint32_t)(1u << (r >> 12)) * (r & 0xFFFu);   /* centilux */
        const int16_t ta = (int16_t)i2c_lee(0x40, 0x01);
        const int32_t tc = ((int32_t)(ta >> 2) * 100) / 32;
        const int s1 = !(P5->IN & BIT1), s2 = !(P3->IN & BIT5), sel = !(P4->IN & BIT1);

        /* El RGB y el zumbador */
        if (s1) P2->OUT |= BIT6; else P2->OUT &= ~BIT6;
        if (s2) P2->OUT |= BIT4; else P2->OUT &= ~BIT4;
        if (jx > 3000) P5->OUT |= BIT6; else P5->OUT &= ~BIT6;
        if (sel) P2->SEL0 |= BIT7; else { P2->SEL0 &= ~BIT7; P2->OUT &= ~BIT7; }

        /* El recuadro, del color de lo pulsado */
        const uint16_t c = (uint16_t)((s1 ? 0xF800 : 0) | (s2 ? 0x07E0 : 0) | (jx > 3000 ? 0x001F : 0));
        if (c != ultimo) { lcd_rect(32, 32, 64, 64, c ? c : 0x8410); ultimo = c; }

        uart_texto("joy="); uart_num(jx); uart_pon(','); uart_num(jy);
        uart_texto(" acc_z="); uart_num(az);
        uart_texto(" lux="); uart_fijo2(lux);
        uart_texto(" t="); if (tc < 0) { uart_pon('-'); uart_fijo2((uint32_t)-tc); } else uart_fijo2((uint32_t)tc);
        uart_texto(" s1="); uart_num((uint32_t)s1); uart_texto(" s2="); uart_num((uint32_t)s2);
        uart_texto("\r\n");
        espera_us(100000);
    }
}
