/* oobe.c — la demo de fábrica de la MSP-EXP430G2ET, reescrita a partir de lo
 * que cuenta SLAU772A §1.4 y §3.1 (el main.c de TI no está en doc/refs):
 *
 *   * al arrancar, los dos LED se turnan (LED1 verde, P1.0; LED2 rojo, P1.6);
 *   * S1 (P1.3) pasa al modo «Live Temperature»: se toma la temperatura de
 *     referencia, y desde ahí se mide el sensor del ADC10 cuatro veces por
 *     segundo y se manda al PC por la UART del Timer_A a 9600 8N1 (como la de
 *     TI: «to be aligned with all the other MSP430G2xx MCUs, the demo uses the
 *     Timer UART implementation»), en texto, «nn\r\n» en ºC;
 *   * por encima de la referencia luce el verde, por debajo el rojo, más
 *     cuanto más lejos (PWM por software en la interrupción del perro en modo
 *     intervalo, 32 pasos de 1 ms);
 *   * otra pulsación de S1 recalibra la referencia.
 */
/* A 8 MHz: las rutinas del perro (el PWM) y de la UART del Timer_A caben de
 * sobra en un bit (833 ciclos); a 1 MHz, la del PWM más la de la UART no
 * caben en los 104 de un bit, y la UART pierde bits (lo mismo pasaría en la
 * placa) */
#define UART_TBIT 833
#include "ta_uart.h"
static volatile unsigned char modo, pulsado, pwm_v, pwm_r, paso;
static volatile unsigned tics;
static int referencia;

static int temperatura(void) {
    ADC10CTL1 = INCH_10 | ADC10DIV_3;
    ADC10CTL0 = SREF_1 | ADC10SHT_3 | REFON | ADC10ON;
    __delay_cycles(30);
    ADC10CTL0 |= ENC | ADC10SC;
    while (ADC10CTL1 & ADC10BUSY) ;
    return (int)(((long)ADC10MEM - 673) * 423 / 1024);
}
static void manda(int t) {
    if (t < 0) { ta_putc('-'); t = -t; }
    if (t >= 10) ta_putc('0' + t / 10);
    ta_putc('0' + t % 10); ta_putc('\r'); ta_putc('\n');
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    BCSCTL1 = CALBC1_8MHZ; DCOCTL = CALDCO_8MHZ;
    P1DIR |= BIT0 | BIT6; P1OUT = BIT3;               /* S1 con R6 en la placa */
    P1IES |= BIT3; P1IFG = 0; P1IE |= BIT3;
    ta_uart_init();
    WDTCTL = WDT_MDLY_8;                              /* SMCLK/8192: 1,024 ms, el paso del PWM */
    IE1 |= WDTIE;
    __bis_SR_register(GIE);
    for (;;) {
        if (pulsado) {
            pulsado = 0;
            modo = 1;
            referencia = temperatura();
        }
        if (modo == 0) {
            if (tics >= 244) {                        /* 250 ms */
                tics = 0;
                pwm_v = pwm_v ? 0 : 31; pwm_r = pwm_v ? 0 : 31;
            }
        } else if (tics >= 244) {
            int t, d;
            tics = 0;
            t = temperatura();
            manda(t);
            d = (t - referencia) * 8;
            if (d > 31) d = 31; if (d < -31) d = -31;
            pwm_v = d > 0 ? d : 0; pwm_r = d < 0 ? -d : 0;
        }
        __bis_SR_register(LPM0_bits | GIE);
    }
}

__attribute__((interrupt(WDT_VECTOR))) void wdt(void) {
    paso = (paso + 1) & 31;
    if (paso < pwm_v) P1OUT |= BIT0; else P1OUT &= ~BIT0;
    if (paso < pwm_r) P1OUT |= BIT6; else P1OUT &= ~BIT6;
    if ((++tics & 255) == 0 || pulsado) __bic_SR_register_on_exit(LPM0_bits);
}
__attribute__((interrupt(PORT1_VECTOR))) void p1(void) {
    P1IFG &= ~BIT3;
    pulsado = 1;
    __bic_SR_register_on_exit(LPM0_bits);
}
