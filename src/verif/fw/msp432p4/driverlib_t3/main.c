/* ===========================================================================
 * driverlib_t3 — Los ejemplos de DriverLib de la fase T3, contra el modelo
 *
 * Los cuatro que pide el plan (doc/analisis_msp432.md, T3), escritos como
 * los del SDK de TI -las mismas llamadas, en el mismo orden- y con DriverLib
 * compilada de sus fuentes, sin la ROM. El banco escribe el modo en el buzon:
 *
 *   1  uart_pc_echo         la UCA0 a 115200 con SMCLK = 12 MHz
 *                           (eUSCI_UART_Config de la guia), por interrupcion:
 *                           devuelve lo que llega por el backchannel
 *   2  timer_a_pwm          Timer_A_generatePWM: TA0.1 en P2.4, 1 kHz al 25 %
 *   3  adc14_single_conv.   A0 (P5.5) con ADC14_toggleConversionTrigger y la
 *                           interrupcion de MEM0
 *   4  dma_memory           memoria a memoria en modo automatico con
 *                           DMA_setChannelTransfer, por software, y DMA_INT1
 * ===========================================================================*/
#include <ti/devices/msp432p4xx/driverlib/adc14.h>
#include <ti/devices/msp432p4xx/driverlib/cs.h>
#include <ti/devices/msp432p4xx/driverlib/dma.h>
#include <ti/devices/msp432p4xx/driverlib/flash.h>
#include <ti/devices/msp432p4xx/driverlib/gpio.h>
#include <ti/devices/msp432p4xx/driverlib/interrupt.h>
#include <ti/devices/msp432p4xx/driverlib/pcm.h>
#include <ti/devices/msp432p4xx/driverlib/timer_a.h>
#include <ti/devices/msp432p4xx/driverlib/uart.h>
#include <ti/devices/msp432p4xx/driverlib/wdt_a.h>

uint32_t SystemCoreClock = 3000000;
void SystemInit(void) {}

typedef struct {
    volatile uint32_t modo;
    volatile uint32_t arranques;
    volatile uint32_t estado;
    volatile uint32_t senal;
    volatile uint32_t r[60];
    volatile uint8_t  buf[256];
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

/* --- 1: uart_pc_echo -------------------------------------------------------- */
/* 115200 con 12 MHz: la tabla de la guia de usuario (6, 8, 0x20, OS16) */
static const eUSCI_UART_Config uart_cfg = {
    EUSCI_A_UART_CLOCKSOURCE_SMCLK, 6, 8, 0x20,
    EUSCI_A_UART_NO_PARITY, EUSCI_A_UART_LSB_FIRST, EUSCI_A_UART_ONE_STOP_BIT,
    EUSCI_A_UART_MODE, EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION
};
void EUSCIA0_IRQHandler(void) {
    const uint32_t st = UART_getEnabledInterruptStatus(EUSCI_A0_BASE);
    if (st & EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG) {
        const uint8_t c = UART_receiveData(EUSCI_A0_BASE);
        UART_transmitData(EUSCI_A0_BASE, c);
        buzon.r[0]++;
    }
}
static void uart_pc_echo(void) {
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P1, GPIO_PIN2 | GPIO_PIN3,
                                               GPIO_PRIMARY_MODULE_FUNCTION);
    CS_setDCOCenteredFrequency(CS_DCO_FREQUENCY_12);
    UART_initModule(EUSCI_A0_BASE, &uart_cfg);
    UART_enableModule(EUSCI_A0_BASE);
    UART_enableInterrupt(EUSCI_A0_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT);
    Interrupt_enableInterrupt(INT_EUSCIA0);
    Interrupt_enableMaster();
    buzon.r[1] = CS_getSMCLK();
    buzon.estado = 1;
    for (;;) { }
}

/* --- 2: timer_a_pwm --------------------------------------------------------- */
static const Timer_A_PWMConfig pwm_cfg = {
    TIMER_A_CLOCKSOURCE_SMCLK, TIMER_A_CLOCKSOURCE_DIVIDER_1,
    3000, TIMER_A_CAPTURECOMPARE_REGISTER_1, TIMER_A_OUTPUTMODE_RESET_SET, 750
};
static void timer_a_pwm(void) {
    GPIO_setAsPeripheralModuleFunctionOutputPin(GPIO_PORT_P2, GPIO_PIN4,
                                                GPIO_PRIMARY_MODULE_FUNCTION);
    Timer_A_generatePWM(TIMER_A0_BASE, &pwm_cfg);
    buzon.estado = 1;
    for (;;) { }
}

/* --- 3: adc14_single_conversion_repeat ------------------------------------ */
void ADC14_IRQHandler(void) {
    const uint64_t st = ADC14_getEnabledInterruptStatus();
    ADC14_clearInterruptFlag(st);
    if (ADC_INT0 & st) {
        const uint32_t n = buzon.r[0];
        if (n < 8) buzon.r[1 + n] = ADC14_getResult(ADC_MEM0);
        buzon.r[0] = n + 1;
        if (n + 1 < 8) ADC14_toggleConversionTrigger();
    }
}
static void adc14_single_conversion(void) {
    FlashCtl_setWaitState(FLASH_BANK0, 1);
    FlashCtl_setWaitState(FLASH_BANK1, 1);
    PCM_setCoreVoltageLevel(PCM_VCORE1);
    CS_setDCOCenteredFrequency(CS_DCO_FREQUENCY_48);
    ADC14_enableModule();
    ADC14_initModule(ADC_CLOCKSOURCE_MCLK, ADC_PREDIVIDER_1, ADC_DIVIDER_1, 0);
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P5, GPIO_PIN5,
                                               GPIO_TERTIARY_MODULE_FUNCTION);
    ADC14_configureSingleSampleMode(ADC_MEM0, true);
    ADC14_configureConversionMemory(ADC_MEM0, ADC_VREFPOS_AVCC_VREFNEG_VSS,
                                    ADC_INPUT_A0, false);
    ADC14_enableSampleTimer(ADC_MANUAL_ITERATION);
    ADC14_enableConversion();
    ADC14_enableInterrupt(ADC_INT0);
    Interrupt_enableInterrupt(INT_ADC14);
    Interrupt_enableMaster();
    buzon.r[10] = CS_getMCLK();
    buzon.estado = 1;
    ADC14_toggleConversionTrigger();
    while (buzon.r[0] < 8) { }
    buzon.estado = 2;
    for (;;) { }
}

/* --- 4: dma_memory ---------------------------------------------------------- */
static uint8_t tabla[1024] __attribute__((aligned(1024)));
static uint32_t origen[64], destino[64];
static volatile uint32_t dma_fin = 0;
void DMA_INT1_IRQHandler(void) { dma_fin++; }
static void dma_memory(void) {
    for (int i = 0; i < 64; ++i) { origen[i] = 0x51DE0000u + (uint32_t)i; destino[i] = 0; }
    DMA_enableModule();
    DMA_setControlBase(tabla);
    DMA_setChannelControl(UDMA_PRI_SELECT | DMA_CH0_RESERVED0,
                          UDMA_SIZE_32 | UDMA_SRC_INC_32 | UDMA_DST_INC_32 | UDMA_ARB_8);
    DMA_setChannelTransfer(UDMA_PRI_SELECT | DMA_CH0_RESERVED0, UDMA_MODE_AUTO,
                           origen, destino, 64);
    DMA_assignInterrupt(DMA_INT1, 0);
    Interrupt_enableInterrupt(INT_DMA_INT1);
    Interrupt_enableMaster();
    DMA_assignChannel(DMA_CH0_RESERVED0);
    DMA_enableChannel(0);
    DMA_requestSoftwareTransfer(0);
    while (!dma_fin) { }
    uint32_t bien = 0;
    for (int i = 0; i < 64; ++i) bien += (destino[i] == 0x51DE0000u + (uint32_t)i);
    buzon.r[0] = bien;
    buzon.r[1] = DMA_isChannelEnabled(0);
    buzon.r[2] = DMA_getChannelMode(UDMA_PRI_SELECT | DMA_CH0_RESERVED0);
    buzon.estado = 1;
    for (;;) { }
}

int main(void) {
    WDT_A_holdTimer();
    buzon.arranques++;
    switch (buzon.modo) {
    case 1: uart_pc_echo(); break;
    case 2: timer_a_pwm(); break;
    case 3: adc14_single_conversion(); break;
    case 4: dma_memory(); break;
    default: break;
    }
    for (;;) { }
}
