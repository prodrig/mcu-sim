/* f5_basico.c — testf5, F2 (M4): lo que hace un programa de la F5529LP con
 * la DriverLib de TI (la del paquete de software de la placa):
 *   VCORE a 3 con PMM_setVCore, el FLL a 25 MHz desde REFO con
 *   UCS_initFLLSettle, la UCA1 (el backchannel, P4.4/P4.5) a 115200,
 *   la Flash (borrar el segmento D de información y escribir con FlashCtl),
 *   el sensor de temperatura del ADC12_A con el REF a 1,5 V y la calibración
 *   de la TLV, el DMA (un bloque de RAM a RAM), el MPY32 (32x32 por libgcc),
 *   el CRC16 (CRC-CCITT de «123456789»), Timer0_A5 a 1 ms, y una función
 *   por encima de 0x10000 (-mlarge). Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

static void pon(char c) { while (!USCI_A_UART_getInterruptStatus(USCI_A1_BASE, USCI_A_UART_TRANSMIT_INTERRUPT_FLAG)) ; USCI_A_UART_transmitData(USCI_A1_BASE, c); }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }

volatile unsigned ticks;
static uint16_t origen[16], destino[16];

__attribute__((noinline, section(".upper.text"))) uint32_t arriba(uint32_t x) { return x * 3u + 1u; }

int main(void) {
    WDT_A_hold(WDT_A_BASE);
    PMM_setVCore(PMM_CORE_LEVEL_3);
    UCS_initClockSignal(UCS_FLLREF, UCS_REFOCLK_SELECT, UCS_CLOCK_DIVIDER_1);
    UCS_initClockSignal(UCS_ACLK, UCS_REFOCLK_SELECT, UCS_CLOCK_DIVIDER_1);
    UCS_initFLLSettle(25000, 763);                 /* 25 MHz = 763 x 32768 */
    /* la UCA1 a 115200 con SMCLK = 25 MHz: UCBR = 13, UCBRF = 9, UCOS16 [UG5 tabla 34-5] */
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P4, GPIO_PIN4 | GPIO_PIN5);
    {
        USCI_A_UART_initParam p = {0};
        p.selectClockSource = USCI_A_UART_CLOCKSOURCE_SMCLK;
        p.clockPrescalar = 13; p.firstModReg = 9; p.secondModReg = 0;
        p.parity = USCI_A_UART_NO_PARITY; p.msborLsbFirst = USCI_A_UART_LSB_FIRST;
        p.numberofStopBits = USCI_A_UART_ONE_STOP_BIT; p.uartMode = USCI_A_UART_MODE;
        p.overSampling = USCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION;
        USCI_A_UART_init(USCI_A1_BASE, &p);
        USCI_A_UART_enable(USCI_A1_BASE);
    }
    cad("hola F5529 "); dec(UCS_getMCLK() / 1000); cad(" kHz\n");
    /* Flash: el segmento D de información (0x1800) */
    FlashCtl_eraseSegment((uint8_t*)0x1800);
    {
        uint16_t d[2] = {0x5529, 0xBEEF};
        FlashCtl_write16(d, (uint16_t*)0x1800, 2);
    }
    cad("flash "); hex(*(volatile uint16_t*)0x1800, 4); pon(' '); hex(*(volatile uint16_t*)0x1802, 4);
    cad(FlashCtl_performEraseCheck((uint8_t*)0x1804, 60) == STATUS_SUCCESS ? " ok\n" : " mal\n");
    /* ADC12_A: el sensor con REF a 1,5 V */
    Ref_setReferenceVoltage(REF_BASE, REF_VREF1_5V);
    Ref_enableReferenceVoltage(REF_BASE);
    ADC12_A_init(ADC12_A_BASE, ADC12_A_SAMPLEHOLDSOURCE_SC, ADC12_A_CLOCKSOURCE_ADC12OSC, ADC12_A_CLOCKDIVIDER_1);
    ADC12_A_enable(ADC12_A_BASE);
    ADC12_A_setupSamplingTimer(ADC12_A_BASE, ADC12_A_CYCLEHOLD_256_CYCLES, ADC12_A_CYCLEHOLD_4_CYCLES, ADC12_A_MULTIPLESAMPLESDISABLE);
    {
        ADC12_A_configureMemoryParam m = {0};
        m.memoryBufferControlIndex = ADC12_A_MEMORY_0;
        m.inputSourceSelect = ADC12_A_INPUT_TEMPSENSOR;
        m.positiveRefVoltageSourceSelect = ADC12_A_VREFPOS_INT;
        m.negativeRefVoltageSourceSelect = ADC12_A_VREFNEG_AVSS;
        m.endOfSequence = ADC12_A_NOTENDOFSEQUENCE;
        ADC12_A_configureMemory(ADC12_A_BASE, &m);
    }
    ADC12_A_startConversion(ADC12_A_BASE, ADC12_A_MEMORY_0, ADC12_A_SINGLECHANNEL);
    while (ADC12_A_isBusy(ADC12_A_BASE)) ;
    {
        const long t30 = *(volatile uint16_t*)0x1A1A, t85 = *(volatile uint16_t*)0x1A1C;
        const long r = ADC12_A_getResults(ADC12_A_BASE, ADC12_A_MEMORY_0);
        cad("temp "); dec(((r - t30) * (85 - 30)) * 10 / (t85 - t30) + 300); cad(" dC\n");
    }
    /* DMA: 16 palabras de RAM a RAM, en bloque, por software */
    {
        unsigned i, mal = 0;
        DMA_initParam p = {0};
        for (i = 0; i < 16; ++i) { origen[i] = 0x1000 + 7 * i; destino[i] = 0; }
        p.channelSelect = DMA_CHANNEL_0; p.transferModeSelect = DMA_TRANSFER_BLOCK;
        p.transferSize = 16; p.triggerSourceSelect = DMA_TRIGGERSOURCE_0;
        p.transferUnitSelect = DMA_SIZE_SRCWORD_DSTWORD; p.triggerTypeSelect = DMA_TRIGGER_RISINGEDGE;
        DMA_init(&p);
        DMA_setSrcAddress(DMA_CHANNEL_0, (uint32_t)(uintptr_t)origen, DMA_DIRECTION_INCREMENT);
        DMA_setDstAddress(DMA_CHANNEL_0, (uint32_t)(uintptr_t)destino, DMA_DIRECTION_INCREMENT);
        DMA_enableTransfers(DMA_CHANNEL_0);
        DMA_startTransfer(DMA_CHANNEL_0);
        for (i = 0; i < 16; ++i) if (destino[i] != 0x1000 + 7 * i) ++mal;
        cad("dma "); dec(16 - mal); cad(DMA_getInterruptStatus(DMA_CHANNEL_0) ? " ifg\n" : " -\n");
    }
    /* MPY32 (las rutinas de multiplicar de libgcc lo usan) */
    {
        volatile uint32_t a = 123456789ul, b = 1000ul;
        volatile int32_t c = -77777, e = 3;
        volatile uint64_t x = (uint64_t)a * b;
        cad("mpy "); hex((uint32_t)(x >> 32), 8); hex((uint32_t)x, 8); pon(' '); dec(c * e); pon('\n');
    }
    /* CRC16 de «123456789» por CRCDIRB, semilla 0xFFFF: el CRC-CCITT normal, 0x29B1 */
    {
        const char* s = "123456789";
        CRC_setSeed(CRC_BASE, 0xFFFF);
        while (*s) CRC_set8BitDataReversed(CRC_BASE, (uint8_t)*s++);
        cad("crc "); hex(CRC_getResult(CRC_BASE), 4); pon('\n');
    }
    cad("arriba "); hex((uint32_t)(uintptr_t)&arriba, 5); pon(' '); dec(arriba(41)); pon('\n');
    /* Timer0_A5 a 1 ms con SMCLK = 25 MHz y la interrupción de CCR0 */
    TA0CCR0 = 25000 - 1; TA0CCTL0 = CCIE; TA0CTL = TASSEL_2 | MC_1 | TACLR;
    __bis_SR_register(GIE);
    while (ticks < 10) ;
    cad("ta0 10 ms\n");
    for (;;) __bis_SR_register(LPM0_bits | GIE);
}

__attribute__((interrupt(TIMER0_A0_VECTOR))) void t0(void) { ++ticks; }
