/* ===========================================================================
 * e4_edumkii — La BOOSTXL-EDUMKII sobre la MSP-EXP432E401Y, con DriverLib
 *
 * El mismo programa que verif/fw/msp432p4/edumkii_demo, ahora en la
 * BoosterPack 1 de la LaunchPad de Ethernet (placas/msp_exp432e401y_edumkii.xml),
 * con DriverLib del E4. Los pines son los de la tabla 2 de SLAU748B:
 *
 *   joystick       X J1.2 = PE4 (AIN9), Y J3.26 = PE3 (AIN0), SEL J1.5 = PC6
 *   acelerometro   Z J3.25 = PE2 (AIN1)
 *   pulsadores     S1 J4.33 = PL1, S2 J4.32 = PL2
 *   LED RGB        verde J4.38 = PF3, azul J4.37 = PG0
 *   J4.39 = PF2    la luz de la pantalla: el sistema pone J5 en PWM-LCD
 *   zumbador       J4.40 = PF1, a 2 kHz desde la interrupcion del TIMER0
 *                  (el PWM del E4 esta declarado, no modelado)
 *   pantalla       SSI2: CLK J1.7 = PD3, MOSI J2.15 = PD1; CS J2.13 = PN2,
 *                  D/C J4.31 = PL3, RESET J2.17 = PH3
 *   I2C0           SCL J1.9 = PB2, SDA J1.10 = PB3: OPT3001 (0x44) y
 *                  TMP006 (0x40)
 *   backchannel    UART0 (PA0/PA1) a 115200, por JP4/JP5 y J101
 *
 * Cada 100 ms, por el backchannel, la misma linea que el del P4:
 *     joy=2048,2048 acc_z=2867 lux=300.00 t=25.00 s1=0 s2=0
 * La pantalla: un fondo azul oscuro y un recuadro del color de lo pulsado
 * (rojo con S1, verde con S2, azul con el joystick a la derecha). El RGB:
 * verde con S2, azul con el joystick a la derecha; el zumbador, con el
 * pulsador del joystick.
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"

static uint32_t g_clk;

static void habilita(uint32_t p) {
    SysCtlPeripheralEnable(p);
    while (!SysCtlPeripheralReady(p)) { }
}
/* Las esperas, con el TIMER1 de un disparo: exactas a cualquier reloj */
static void espera_us(uint32_t us) {
    TimerDisable(TIMER1_BASE, TIMER_A);
    TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
    TimerLoadSet(TIMER1_BASE, TIMER_A, (g_clk / 1000000u) * us - 1);
    TimerEnable(TIMER1_BASE, TIMER_A);
    while (!(TimerIntStatus(TIMER1_BASE, false) & TIMER_TIMA_TIMEOUT)) { }
}

/* --- El backchannel ---------------------------------------------------------- */
static void uart_pon(char c) { UARTCharPut(UART0_BASE, (uint8_t)c); }
static void uart_texto(const char *s) { while (*s) uart_pon(*s++); }
static void uart_num(uint32_t v) {
    char b[11]; int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) uart_pon(b[--n]);
}
static void uart_fijo2(uint32_t centesimas) {
    uart_num(centesimas / 100);
    uart_pon('.');
    uart_pon((char)('0' + (centesimas / 10) % 10));
    uart_pon((char)('0' + centesimas % 10));
}

/* --- I2C0 -------------------------------------------------------------------- */
static void i2c_espera(void) {
    while (!I2CMasterBusy(I2C0_BASE)) { }
    while (I2CMasterBusy(I2C0_BASE)) { }
}
static void i2c_esc(uint8_t a, uint8_t r, uint16_t v) {
    I2CMasterSlaveAddrSet(I2C0_BASE, a, false);
    I2CMasterDataPut(I2C0_BASE, r);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    i2c_espera();
    I2CMasterDataPut(I2C0_BASE, (uint8_t)(v >> 8));
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_CONT);
    i2c_espera();
    I2CMasterDataPut(I2C0_BASE, (uint8_t)v);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
    i2c_espera();
}
static uint16_t i2c_lee(uint8_t a, uint8_t r) {
    I2CMasterSlaveAddrSet(I2C0_BASE, a, false);
    I2CMasterDataPut(I2C0_BASE, r);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    i2c_espera();
    I2CMasterSlaveAddrSet(I2C0_BASE, a, true);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_RECEIVE_START);
    i2c_espera();
    const uint8_t hi = (uint8_t)I2CMasterDataGet(I2C0_BASE);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);
    i2c_espera();
    const uint8_t lo = (uint8_t)I2CMasterDataGet(I2C0_BASE);
    return (uint16_t)((hi << 8) | lo);
}

/* --- La pantalla: CS PN2, D/C PL3, RESET PH3 --------------------------------- */
static void spi(uint8_t v) { SSIDataPut(SSI2_BASE, v); }
static void spi_fin(void) { while (SSIBusy(SSI2_BASE)) { } }
static void lcd_orden(uint8_t c) {
    spi_fin();
    GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_3, 0);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_2, 0);
    spi(c);
    spi_fin();
    GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_3, GPIO_PIN_3);
}
static void lcd_fin(void) { spi_fin(); GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_2, GPIO_PIN_2); }
static void lcd_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c) {
    lcd_orden(0x2A); spi(0); spi(x); spi(0); spi((uint8_t)(x + w - 1));
    lcd_orden(0x2B); spi(0); spi(y); spi(0); spi((uint8_t)(y + h - 1));
    lcd_orden(0x2C);
    for (uint32_t n = (uint32_t)w * h; n > 0; --n) { spi((uint8_t)(c >> 8)); spi((uint8_t)c); }
    lcd_fin();
}

/* --- ADC0: una conversion por la secuencia 3 --------------------------------- */
static uint32_t adc(uint32_t canal) {
    ADCSequenceDisable(ADC0_BASE, 3);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, canal | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);
    ADCIntClear(ADC0_BASE, 3);
    ADCProcessorTrigger(ADC0_BASE, 3);
    while (!ADCIntStatus(ADC0_BASE, 3, false)) { }
    ADCIntClear(ADC0_BASE, 3);
    uint32_t v;
    ADCSequenceDataGet(ADC0_BASE, 3, &v);
    return v;
}

/* --- El zumbador: PF1 cambia a 4 kHz mientras suena -------------------------- */
static volatile bool g_suena;
void TIMER0A_IRQHandler(void) {
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    if (g_suena) GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_1) ^ GPIO_PIN_1);
    else GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0);
}

int main(void) {
    g_clk = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL |
                               SYSCTL_CFG_VCO_480, 120000000);
    habilita(SYSCTL_PERIPH_GPIOA); habilita(SYSCTL_PERIPH_GPIOB); habilita(SYSCTL_PERIPH_GPIOC);
    habilita(SYSCTL_PERIPH_GPIOD); habilita(SYSCTL_PERIPH_GPIOE); habilita(SYSCTL_PERIPH_GPIOF);
    habilita(SYSCTL_PERIPH_GPIOG); habilita(SYSCTL_PERIPH_GPIOH); habilita(SYSCTL_PERIPH_GPIOL);
    habilita(SYSCTL_PERIPH_GPION);
    habilita(SYSCTL_PERIPH_TIMER1);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_ONE_SHOT);

    /* El backchannel */
    habilita(SYSCTL_PERIPH_UART0);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTConfigSetExpClk(UART0_BASE, g_clk, 115200,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    uart_texto("BOOSTXL-EDUMKII\r\n");

    /* Los pulsadores: la BoosterPack lleva sus pull-ups, y el pin, el suyo */
    GPIOPinTypeGPIOInput(GPIO_PORTL_BASE, GPIO_PIN_1 | GPIO_PIN_2);
    GPIOPadConfigSet(GPIO_PORTL_BASE, GPIO_PIN_1 | GPIO_PIN_2, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    GPIOPinTypeGPIOInput(GPIO_PORTC_BASE, GPIO_PIN_6);
    GPIOPadConfigSet(GPIO_PORTC_BASE, GPIO_PIN_6, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    /* El RGB (verde, azul), la luz de la pantalla y el zumbador */
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    GPIOPinTypeGPIOOutput(GPIO_PORTG_BASE, GPIO_PIN_0);
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_3, 0);
    GPIOPinWrite(GPIO_PORTG_BASE, GPIO_PIN_0, 0);
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_PIN_2);          /* la luz */
    habilita(SYSCTL_PERIPH_TIMER0);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, g_clk / 4000 - 1);           /* 2 kHz */
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
    IntMasterEnable();
    TimerEnable(TIMER0_BASE, TIMER_A);

    /* Los analogicos: AIN9 (PE4), AIN0 (PE3), AIN1 (PE2) */
    habilita(SYSCTL_PERIPH_ADC0);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_2);
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);

    /* I2C0 a 100 kHz; los dos sensores en continuo */
    habilita(SYSCTL_PERIPH_I2C0);
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
    I2CMasterInitExpClk(I2C0_BASE, g_clk, false);
    i2c_esc(0x44, 0x01, 0xC610);
    i2c_esc(0x40, 0x02, 0x7000);

    /* La pantalla: SSI2 a 10 MHz, modo 0, y sus tres GPIO */
    GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_2);
    GPIOPinTypeGPIOOutput(GPIO_PORTL_BASE, GPIO_PIN_3);
    GPIOPinTypeGPIOOutput(GPIO_PORTH_BASE, GPIO_PIN_3);
    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_2, GPIO_PIN_2);
    GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_3, GPIO_PIN_3);
    GPIOPinWrite(GPIO_PORTH_BASE, GPIO_PIN_3, GPIO_PIN_3);
    habilita(SYSCTL_PERIPH_SSI2);
    GPIOPinConfigure(GPIO_PD3_SSI2CLK);
    GPIOPinConfigure(GPIO_PD1_SSI2XDAT0);
    GPIOPinTypeSSI(GPIO_PORTD_BASE, GPIO_PIN_1 | GPIO_PIN_3);
    SSIConfigSetExpClk(SSI2_BASE, g_clk, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 10000000, 8);
    SSIEnable(SSI2_BASE);
    GPIOPinWrite(GPIO_PORTH_BASE, GPIO_PIN_3, 0);
    espera_us(20);
    GPIOPinWrite(GPIO_PORTH_BASE, GPIO_PIN_3, GPIO_PIN_3);
    espera_us(120000);
    lcd_orden(0x11); lcd_fin();
    espera_us(120000);
    lcd_orden(0x3A); spi(0x05); lcd_fin();
    lcd_orden(0x36); spi(0x00); lcd_fin();
    lcd_orden(0x29); lcd_fin();
    lcd_rect(0, 0, 128, 128, 0x0010);                                /* fondo azul oscuro */

    uint16_t ultimo = 0xFFFF;
    for (;;) {
        const uint32_t jx = adc(ADC_CTL_CH9), jy = adc(ADC_CTL_CH0);
        const uint32_t az = adc(ADC_CTL_CH1);
        const uint16_t r = i2c_lee(0x44, 0x00);
        const uint32_t lux = (uint32_t)(1u << (r >> 12)) * (r & 0xFFFu);   /* centilux */
        const int16_t ta = (int16_t)i2c_lee(0x40, 0x01);
        const int32_t tc = ((int32_t)(ta >> 2) * 100) / 32;
        const int s1 = !GPIOPinRead(GPIO_PORTL_BASE, GPIO_PIN_1);
        const int s2 = !GPIOPinRead(GPIO_PORTL_BASE, GPIO_PIN_2);
        const int sel = !GPIOPinRead(GPIO_PORTC_BASE, GPIO_PIN_6);

        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_3, s2 ? GPIO_PIN_3 : 0);
        GPIOPinWrite(GPIO_PORTG_BASE, GPIO_PIN_0, jx > 3000 ? GPIO_PIN_0 : 0);
        g_suena = sel;

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
