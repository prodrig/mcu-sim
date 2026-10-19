/* ===========================================================================
 * e4_t3 — Los perifericos del MSP432E401Y con DriverLib de TI (fase T6,
 * entrega 3)
 *
 * DriverLib del E4 (BSD-3) compilada de sus fuentes. Todo a 120 MHz desde el
 * cristal de 25 MHz, con SysCtlClockFreqSet, como en los ejemplos del SDK.
 *
 *   modo 0  UART0 (el backchannel, PA0/PA1) a 115200: un saludo y eco en
 *           mayusculas por interrupcion de RX y de tiempo de espera
 *   modo 1  GPTM: TIMER0 de 32 bits periodico de 1 ms; TIMER2A PWM de 10 kHz
 *           al 25 % en PA4; TIMER3A captura de tiempo en PA6; TIMER4A cuenta
 *           de flancos en PM4
 *   modo 2  I2C0 (PB2/PB3) con una 24C02, y SSI2 (PD3/PD2/PD1/PD0) en bucle
 *           por los pines
 *   modo 3  ADC0: AIN0 (PE3) por el procesador, el sensor de temperatura, y
 *           AIN1 (PE2) disparado por TIMER1
 *   modo 4  uDMA: memoria a memoria por software, y el TX de la UART0
 *   modo 5  hibernacion: el RTC, su alarma, y despertar de hibernar con ella
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/inc/hw_timer.h"
#include "ti/devices/msp432e4/driverlib/inc/hw_uart.h"
#include "ti/devices/msp432e4/driverlib/types.h"

typedef struct {
    volatile uint32_t modo;          /* 0x00 */
    volatile uint32_t arranques;     /* 0x04 */
    volatile uint32_t estado;        /* 0x08 */
    volatile uint32_t sysclk;        /* 0x0C lo que devuelve SysCtlClockFreqSet */
    volatile uint32_t r[24];         /* 0x10 resultados de cada modo */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static uint32_t g_clk;

static void reloj_120(void) {
    g_clk = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL |
                               SYSCTL_CFG_VCO_480, 120000000);
    buzon.sysclk = g_clk;
}
static void habilita(uint32_t p) {
    SysCtlPeripheralEnable(p);
    while (!SysCtlPeripheralReady(p)) { }
}

/* --- modo 0: la UART -------------------------------------------------------- */
void UART0_IRQHandler(void) {
    const uint32_t st = UARTIntStatus(UART0_BASE, true);
    UARTIntClear(UART0_BASE, st);
    buzon.r[0]++;
    if (st & UART_INT_RT) buzon.r[1]++;
    while (UARTCharsAvail(UART0_BASE)) {
        int32_t c = UARTCharGetNonBlocking(UART0_BASE);
        if (c >= 'a' && c <= 'z') c -= 32;
        UARTCharPutNonBlocking(UART0_BASE, (uint8_t)c);
        buzon.r[2]++;
    }
}
static void uart0(void) {
    habilita(SYSCTL_PERIPH_GPIOA);
    habilita(SYSCTL_PERIPH_UART0);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTConfigSetExpClk(UART0_BASE, g_clk, 115200,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
}
static void puts0(const char *s) { while (*s) UARTCharPut(UART0_BASE, (uint8_t)*s++); }

/* --- modo 1: los temporizadores ---------------------------------------------- */
void TIMER0A_IRQHandler(void) {
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    buzon.r[0]++;
}
void TIMER3A_IRQHandler(void) {
    TimerIntClear(TIMER3_BASE, TIMER_CAPA_EVENT);
    const uint32_t n = buzon.r[2]++;
    if (n < 3) buzon.r[3 + n] = TimerValueGet(TIMER3_BASE, TIMER_A);
}
void TIMER4A_IRQHandler(void) {
    TimerIntClear(TIMER4_BASE, TIMER_CAPA_MATCH);
    buzon.r[6]++;
}
static void temporizadores(void) {
    habilita(SYSCTL_PERIPH_TIMER0);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, g_clk / 1000 - 1);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntEnable(INT_TIMER0A);
    /* PWM en PA4 (T2CCP0): 10 kHz, en alto el 25 % */
    habilita(SYSCTL_PERIPH_GPIOA);
    habilita(SYSCTL_PERIPH_TIMER2);
    GPIOPinConfigure(GPIO_PA4_T2CCP0);
    GPIOPinTypeTimer(GPIO_PORTA_BASE, GPIO_PIN_4);
    TimerConfigure(TIMER2_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM);
    const uint32_t per = g_clk / 10000;                 /* 12000: cabe en 16 bits */
    TimerLoadSet(TIMER2_BASE, TIMER_A, per - 1);
    TimerMatchSet(TIMER2_BASE, TIMER_A, per - per / 4 - 1);
    /* Captura de tiempo en PA6 (T3CCP0), flancos de subida */
    habilita(SYSCTL_PERIPH_TIMER3);
    GPIOPinConfigure(GPIO_PA6_T3CCP0);
    GPIOPinTypeTimer(GPIO_PORTA_BASE, GPIO_PIN_6);
    TimerConfigure(TIMER3_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME);
    TimerControlEvent(TIMER3_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
    TimerLoadSet(TIMER3_BASE, TIMER_A, 0xFFFF);
    TimerPrescaleSet(TIMER3_BASE, TIMER_A, 0xFF);
    TimerIntEnable(TIMER3_BASE, TIMER_CAPA_EVENT);
    IntEnable(INT_TIMER3A);
    /* Cuenta de flancos en PM4 (T4CCP0): 10 flancos */
    habilita(SYSCTL_PERIPH_GPIOM);
    habilita(SYSCTL_PERIPH_TIMER4);
    GPIOPinConfigure(GPIO_PM4_T4CCP0);
    GPIOPinTypeTimer(GPIO_PORTM_BASE, GPIO_PIN_4);
    TimerConfigure(TIMER4_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_COUNT);
    TimerControlEvent(TIMER4_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
    TimerLoadSet(TIMER4_BASE, TIMER_A, 10);
    TimerMatchSet(TIMER4_BASE, TIMER_A, 0);
    TimerIntEnable(TIMER4_BASE, TIMER_CAPA_MATCH);
    IntEnable(INT_TIMER4A);
    IntMasterEnable();
    TimerEnable(TIMER0_BASE, TIMER_A);
    TimerEnable(TIMER2_BASE, TIMER_A);
    TimerEnable(TIMER3_BASE, TIMER_A);
    TimerEnable(TIMER4_BASE, TIMER_A);
    buzon.estado = 10;
    while (buzon.r[0] < 20) { }
    buzon.r[1] = TimerValueGet(TIMER0_BASE, TIMER_A);
    buzon.r[7] = (HWREG(TIMER4_BASE + TIMER_O_CTL) & TIMER_CTL_TAEN) ? 1 : 0;
}

/* --- modo 2: I2C y SSI ----------------------------------------------------------- */
static void i2c_espera(void) {
    while (!I2CMasterBusy(I2C0_BASE)) { }
    while (I2CMasterBusy(I2C0_BASE)) { }
}
static void i2c_spi(void) {
    habilita(SYSCTL_PERIPH_GPIOB);
    habilita(SYSCTL_PERIPH_I2C0);
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
    I2CMasterInitExpClk(I2C0_BASE, g_clk, false);            /* 100 kHz */
    /* Escribir 4 bytes desde la direccion 0x10 */
    static const uint8_t dat[4] = {0x11, 0x22, 0x33, 0x44};
    I2CMasterSlaveAddrSet(I2C0_BASE, 0x50, false);
    I2CMasterDataPut(I2C0_BASE, 0x10);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    i2c_espera();
    for (unsigned i = 0; i < 4; ++i) {
        I2CMasterDataPut(I2C0_BASE, dat[i]);
        I2CMasterControl(I2C0_BASE, i == 3 ? I2C_MASTER_CMD_BURST_SEND_FINISH
                                           : I2C_MASTER_CMD_BURST_SEND_CONT);
        i2c_espera();
    }
    buzon.r[0] = I2CMasterErr(I2C0_BASE);
    /* Leerlos: puntero y lectura en rafaga con START repetido */
    I2CMasterSlaveAddrSet(I2C0_BASE, 0x50, false);
    I2CMasterDataPut(I2C0_BASE, 0x10);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    i2c_espera();
    I2CMasterSlaveAddrSet(I2C0_BASE, 0x50, true);
    uint32_t leido = 0;
    for (unsigned i = 0; i < 4; ++i) {
        I2CMasterControl(I2C0_BASE, i == 0 ? I2C_MASTER_CMD_BURST_RECEIVE_START
                                   : i == 3 ? I2C_MASTER_CMD_BURST_RECEIVE_FINISH
                                            : I2C_MASTER_CMD_BURST_RECEIVE_CONT);
        i2c_espera();
        leido |= (I2CMasterDataGet(I2C0_BASE) & 0xFFu) << (8 * i);
    }
    buzon.r[1] = leido;
    /* Un esclavo que no esta: NACK de la direccion */
    I2CMasterSlaveAddrSet(I2C0_BASE, 0x33, false);
    I2CMasterDataPut(I2C0_BASE, 0);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND);
    i2c_espera();
    buzon.r[2] = I2CMasterErr(I2C0_BASE);
    /* SSI2 a 1 MHz, modo 0, 8 bits, con XDAT0 unido a XDAT1 por fuera */
    habilita(SYSCTL_PERIPH_GPIOD);
    habilita(SYSCTL_PERIPH_SSI2);
    GPIOPinConfigure(GPIO_PD3_SSI2CLK);
    GPIOPinConfigure(GPIO_PD2_SSI2FSS);
    GPIOPinConfigure(GPIO_PD1_SSI2XDAT0);
    GPIOPinConfigure(GPIO_PD0_SSI2XDAT1);
    GPIOPinTypeSSI(GPIO_PORTD_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    SSIConfigSetExpClk(SSI2_BASE, g_clk, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 1000000, 8);
    SSIEnable(SSI2_BASE);
    uint32_t basura;
    while (SSIDataGetNonBlocking(SSI2_BASE, &basura)) { }
    static const uint8_t spi[4] = {0xA5, 0x3C, 0x00, 0xFF};
    for (unsigned i = 0; i < 4; ++i) SSIDataPut(SSI2_BASE, spi[i]);
    while (SSIBusy(SSI2_BASE)) { }
    uint32_t v = 0, x;
    for (unsigned i = 0; i < 4; ++i) { SSIDataGet(SSI2_BASE, &x); v |= (x & 0xFFu) << (8 * i); }
    buzon.r[3] = v;
}

/* --- modo 3: el ADC ----------------------------------------------------------------- */
void ADC0SS1_IRQHandler(void) {
    ADCIntClear(ADC0_BASE, 1);
    uint32_t v[4];
    const int32_t n = ADCSequenceDataGet(ADC0_BASE, 1, v);
    if (n > 0 && buzon.r[3] < 4) buzon.r[4 + buzon.r[3]] = v[0];
    buzon.r[3]++;
}
static void adc(void) {
    habilita(SYSCTL_PERIPH_GPIOE);
    habilita(SYSCTL_PERIPH_ADC0);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3 | GPIO_PIN_2);
    /* SS3: AIN0 por el procesador */
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);
    ADCIntClear(ADC0_BASE, 3);
    ADCProcessorTrigger(ADC0_BASE, 3);
    while (!ADCIntStatus(ADC0_BASE, 3, false)) { }
    ADCIntClear(ADC0_BASE, 3);
    uint32_t v;
    ADCSequenceDataGet(ADC0_BASE, 3, &v);
    buzon.r[0] = v;
    /* SS2: el sensor de temperatura */
    ADCSequenceConfigure(ADC0_BASE, 2, ADC_TRIGGER_PROCESSOR, 1);
    ADCSequenceStepConfigure(ADC0_BASE, 2, 0, ADC_CTL_TS | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 2);
    ADCIntClear(ADC0_BASE, 2);
    ADCProcessorTrigger(ADC0_BASE, 2);
    while (!ADCIntStatus(ADC0_BASE, 2, false)) { }
    ADCIntClear(ADC0_BASE, 2);
    ADCSequenceDataGet(ADC0_BASE, 2, &v);
    buzon.r[1] = v;
    buzon.r[2] = (uint32_t)(1475 - (2475 * v) / 4096);     /* decimas de grado [DS] */
    /* SS1: AIN1, disparado por TIMER1 cada ms */
    habilita(SYSCTL_PERIPH_TIMER1);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER1_BASE, TIMER_A, g_clk / 1000 - 1);
    TimerControlTrigger(TIMER1_BASE, TIMER_A, true);
    TimerADCEventSet(TIMER1_BASE, TIMER_ADC_TIMEOUT_A);
    ADCSequenceConfigure(ADC0_BASE, 1, ADC_TRIGGER_TIMER, 2);
    ADCSequenceStepConfigure(ADC0_BASE, 1, 0, ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 1);
    ADCIntClear(ADC0_BASE, 1);
    ADCIntEnable(ADC0_BASE, 1);
    IntEnable(INT_ADC0SS1);
    IntMasterEnable();
    TimerEnable(TIMER1_BASE, TIMER_A);
    while (buzon.r[3] < 4) { }
    TimerDisable(TIMER1_BASE, TIMER_A);
}

/* --- modo 4: la uDMA -------------------------------------------------------------- */
static uint8_t tabla[1024] __attribute__((aligned(1024)));
static uint32_t origen[256], destino[256];
static const char mensaje[] = "uDMA a la UART0\r\n";
void UDMA_IRQHandler(void) {
    /* En el E4 no hay registro de estado: la linea UDMA es la del software */
    buzon.r[0]++;
}
static void udma(void) {
    habilita(SYSCTL_PERIPH_UDMA);
    uDMAEnable();
    uDMAControlBaseSet(tabla);
    for (unsigned i = 0; i < 256; ++i) { origen[i] = 0xC0DE0000u | i; destino[i] = 0; }
    uDMAChannelAttributeDisable(UDMA_CH30_SW, UDMA_ATTR_ALL);
    uDMAChannelControlSet(UDMA_CH30_SW | UDMA_PRI_SELECT,
                          UDMA_SIZE_32 | UDMA_SRC_INC_32 | UDMA_DST_INC_32 | UDMA_ARB_8);
    uDMAChannelTransferSet(UDMA_CH30_SW | UDMA_PRI_SELECT, UDMA_MODE_AUTO,
                           origen, destino, 256);
    IntEnable(INT_UDMA);
    IntMasterEnable();
    uDMAChannelEnable(UDMA_CH30_SW);
    uDMAChannelRequest(UDMA_CH30_SW);
    while (buzon.r[0] == 0) { }
    unsigned bien = 0;
    for (unsigned i = 0; i < 256; ++i) bien += destino[i] == (0xC0DE0000u | i);
    buzon.r[1] = bien;
    /* El TX de la UART0 por DMA, canal 9 */
    uart0();
    UARTDMAEnable(UART0_BASE, UART_DMA_TX);
    uDMAChannelAssign(UDMA_CH9_UART0TX);
    uDMAChannelAttributeDisable(UDMA_CH9_UART0TX, UDMA_ATTR_ALL);
    uDMAChannelControlSet(UDMA_CH9_UART0TX | UDMA_PRI_SELECT,
                          UDMA_SIZE_8 | UDMA_SRC_INC_8 | UDMA_DST_INC_NONE | UDMA_ARB_4);
    uDMAChannelTransferSet(UDMA_CH9_UART0TX | UDMA_PRI_SELECT, UDMA_MODE_BASIC,
                           (void *)mensaje, (void *)(UART0_BASE + UART_O_DR), sizeof mensaje - 1);
    UARTIntEnable(UART0_BASE, UART_INT_DMATX);
    IntEnable(INT_UART0);
    uDMAChannelEnable(UDMA_CH9_UART0TX);
    while (uDMAChannelIsEnabled(UDMA_CH9_UART0TX)) { }
    buzon.r[2] = 1;
}

/* --- modo 5: la hibernacion ------------------------------------------------------ */
void HIB_IRQHandler(void) {
    const uint32_t s = HibernateIntStatus(true);
    HibernateIntClear(s);
    if (s & HIBERNATE_INT_RTC_MATCH_0) buzon.r[0]++;
}
static void hibernacion(void) {
    habilita(SYSCTL_PERIPH_HIBERNATE);
    if (HibernateIsActive() && (SysCtlResetCauseGet() & SYSCTL_CAUSE_HIB)) {
        /* Despierta de hibernar: lo que habia en la SRAM se ha perdido, y el
         * modo lo guarda el modulo, en su memoria de bateria */
        uint32_t d[2];
        HibernateDataGet(d, 2);
        buzon.modo = d[0];
        buzon.r[3] = d[1];
        buzon.r[4] = HibernateRTCGet();
        buzon.r[5] = HibernateIntStatus(false);
        buzon.r[6] = 1;
        return;
    }
    HibernateEnableExpClk(g_clk);
    HibernateRTCEnable();
    HibernateRTCSet(100);
    HibernateRTCMatchSet(0, 102);
    HibernateIntEnable(HIBERNATE_INT_RTC_MATCH_0);
    IntEnable(INT_HIBERNATE);
    IntMasterEnable();
    while (buzon.r[0] == 0) { }
    buzon.r[1] = HibernateRTCGet();
    buzon.r[2] = HibernateRTCSSGet();
    /* A hibernar, con la alarma un segundo despues */
    const uint32_t d[2] = {5, 0x5EEDu};
    HibernateDataSet((uint32_t *)d, 2);
    HibernateIntDisable(HIBERNATE_INT_RTC_MATCH_0);
    HibernateRTCMatchSet(0, HibernateRTCGet() + 1);
    HibernateWakeSet(HIBERNATE_WAKE_RTC);
    HibernateRequest();
    for (;;) { }
}

int main(void) {
    /* Despertar de hibernar es arrancar con la SRAM perdida: el modo lo sabe
     * la causa del reset, no el buzon */
    if (SysCtlResetCauseGet() & SYSCTL_CAUSE_HIB) buzon.modo = 5;
    buzon.arranques++;
    buzon.estado = 1;
    reloj_120();
    switch (buzon.modo) {
    case 0:
        uart0();
        UARTFIFOEnable(UART0_BASE);
        UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
        IntEnable(INT_UART0);
        IntMasterEnable();
        puts0("Hola desde el MSP432E4\r\n");
        break;
    case 1: temporizadores(); break;
    case 2: i2c_spi(); break;
    case 3: adc(); break;
    case 4: udma(); break;
    case 5: hibernacion(); break;
    default: break;
    }
    buzon.estado = 100;
    for (;;) { }
}
