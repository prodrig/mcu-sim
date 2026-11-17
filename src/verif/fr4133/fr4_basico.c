/* fr4_basico.c — testfr4 (M7): lo que hace un programa de la FR4133 con la
 * DriverLib de TI (la del paquete de software de la MSP-EXP430FR4133) y su
 * vidrio FH-1138P:
 *
 *   arranque  XT1 (Y1, P4.1/P4.2); el FLL a 8 MHz desde XT1; la UCA0 (el
 *             backchannel, P1.0/P1.1) a 115200; el ID de la TLV; el LCD_E en
 *             4-mux como lo pone hal_LCD.c de la demo (L0-L3 COM, la bomba
 *             de carga a 2,96 V): «HOLA43» en LCDM; «FR4133» en la memoria
 *             de parpadeo con LCDDISP; el parpadeo de todo (LCDBLKMOD = 10);
 *             las tramas con LCDFRMIE; el sensor de temperatura; Timer0_A3 a
 *             1 ms; y LPM3.5 con el LCD encendido («LPM35») y el RTC desde
 *             XT1 para despertar (100 ms).
 *   LPM3.5    el despertar: SYSRSTIV = 8, la BAKMEM y el LCD siguen (sin
 *             volver a configurarlo); «FIN».
 *
 * La fase va en la FRAM (.persistent). Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

#define LCDM  ((volatile uint8_t*)0x0620)
#define LCDMW ((volatile uint16_t*)0x0620)
#define LCDBMW ((volatile uint16_t*)0x0640)

/* Las posiciones y los caracteres de hal_LCD.c (la demo de TI, BSD) */
static const uint8_t POS[6] = {4, 6, 8, 10, 2, 18};
static const uint8_t CIFRA[10][2] = {
    {0xFC, 0x28}, {0x60, 0x20}, {0xDB, 0x00}, {0xF3, 0x00}, {0x67, 0x00},
    {0xB7, 0x00}, {0xBF, 0x00}, {0xE4, 0x00}, {0xFF, 0x00}, {0xF7, 0x00}};
static const uint8_t LETRA[26][2] = {
    {0xEF, 0x00}, {0xF1, 0x50}, {0x9C, 0x00}, {0xF0, 0x50}, {0x9F, 0x00}, {0x8F, 0x00}, {0xBD, 0x00},
    {0x6F, 0x00}, {0x90, 0x50}, {0x78, 0x00}, {0x0E, 0x22}, {0x1C, 0x00}, {0x6C, 0xA0}, {0x6C, 0x82},
    {0xFC, 0x00}, {0xCF, 0x00}, {0xFC, 0x02}, {0xCF, 0x02}, {0xB7, 0x00}, {0x80, 0x50}, {0x7C, 0x00},
    {0x0C, 0x28}, {0x6C, 0x0A}, {0x00, 0xAA}, {0x00, 0xB0}, {0x90, 0x28}};
static uint16_t glifo(char c) {
    if (c >= '0' && c <= '9') return CIFRA[c - '0'][0] | (CIFRA[c - '0'][1] << 8);
    if (c >= 'A' && c <= 'Z') return LETRA[c - 'A'][0] | (LETRA[c - 'A'][1] << 8);
    return 0;
}
static void escribe(volatile uint16_t* m, const char* s) { for (int i = 0; i < 6; ++i) m[POS[i] / 2] = glifo(s[i]); }

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }
static void espera_ms(unsigned ms) { while (ms--) __delay_cycles(8000); }

__attribute__((section(".persistent"))) volatile uint16_t fase = 0;
volatile unsigned ticks, tramas;

static void gpio(void) {
    P4SEL0 |= BIT1 | BIT2;                        /* XIN, XOUT */
    P1SEL0 |= BIT0 | BIT1;                        /* UCA0TXD, UCA0RXD */
    P4DIR |= BIT0; P4OUT &= ~BIT0;                /* LED2 */
    PMM_unlockLPM5();
}
static void reloj8(void) {
    CS_setExternalClockSource(32768);
    CS_turnOnXT1(CS_XT1_DRIVE_1);
    CS_initClockSignal(CS_FLLREF, CS_XT1CLK_SELECT, CS_CLOCK_DIVIDER_1);
    CS_initClockSignal(CS_ACLK, CS_XT1CLK_SELECT, CS_CLOCK_DIVIDER_1);
    CS_initFLLSettle(8000, 244);                  /* 8 MHz = 244 x 32768 */
}
static void uart(void) {
    EUSCI_A_UART_initParam p = {0};
    p.selectClockSource = EUSCI_A_UART_CLOCKSOURCE_SMCLK;
    p.clockPrescalar = 4; p.firstModReg = 5; p.secondModReg = 0x55;        /* 115200 con SMCLK = 8 MHz */
    p.parity = EUSCI_A_UART_NO_PARITY; p.msborLsbFirst = EUSCI_A_UART_LSB_FIRST;
    p.numberofStopBits = EUSCI_A_UART_ONE_STOP_BIT; p.uartMode = EUSCI_A_UART_MODE;
    p.overSampling = EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION;
    EUSCI_A_UART_init(EUSCI_A0_BASE, &p);
    EUSCI_A_UART_enable(EUSCI_A0_BASE);
}
static void lcd(void) {
    /* Como Init_LCD() de hal_LCD.c */
    LCD_E_setPinAsLCDFunctionEx(LCD_E_BASE, LCD_E_SEGMENT_LINE_0, LCD_E_SEGMENT_LINE_26);
    LCD_E_setPinAsLCDFunctionEx(LCD_E_BASE, LCD_E_SEGMENT_LINE_36, LCD_E_SEGMENT_LINE_39);
    LCD_E_initParam ip = LCD_E_INIT_PARAM;
    ip.clockDivider = LCD_E_CLOCKDIVIDER_3;
    ip.muxRate = LCD_E_4_MUX;
    ip.segments = LCD_E_SEGMENTS_ENABLED;
    LCD_E_init(LCD_E_BASE, &ip);
    LCD_E_setVLCDSource(LCD_E_BASE, LCD_E_INTERNAL_REFERENCE_VOLTAGE, LCD_E_EXTERNAL_SUPPLY_VOLTAGE);
    LCD_E_setVLCDVoltage(LCD_E_BASE, LCD_E_REFERENCE_VOLTAGE_2_96V);
    LCD_E_enableChargePump(LCD_E_BASE);
    LCD_E_setChargePumpFreq(LCD_E_BASE, LCD_E_CHARGEPUMP_FREQ_16);
    LCD_E_clearAllMemory(LCD_E_BASE);
    LCD_E_setPinAsCOM(LCD_E_BASE, LCD_E_SEGMENT_LINE_0, LCD_E_MEMORY_COM0);
    LCD_E_setPinAsCOM(LCD_E_BASE, LCD_E_SEGMENT_LINE_1, LCD_E_MEMORY_COM1);
    LCD_E_setPinAsCOM(LCD_E_BASE, LCD_E_SEGMENT_LINE_2, LCD_E_MEMORY_COM2);
    LCD_E_setPinAsCOM(LCD_E_BASE, LCD_E_SEGMENT_LINE_3, LCD_E_MEMORY_COM3);
    LCD_E_selectDisplayMemory(LCD_E_BASE, LCD_E_DISPLAYSOURCE_MEMORY);
    LCD_E_on(LCD_E_BASE);
}
static uint16_t rstiv(void) {
    uint16_t m = 0, v;
    while ((v = SYSRSTIV) != 0) m |= 1u << (v / 2);
    return m;
}

int main(void) {
    WDT_A_hold(WDT_A_BASE);
    const uint16_t causas = rstiv();
    if (fase == 1) {
        /* Despierto de LPM3.5: el LCD sigue con lo que tenía, sin tocarlo */
        gpio();
        reloj8();
        uart();
        cad("lpm35 rstiv "); hex(causas, 4); cad(" bak "); hex(BAKMEM0, 4);
        cad(" lcdctl0 "); hex(LCDCTL0, 4); pon('\n');
        espera_ms(40);
        cad("lcd sigue\n");
        escribe(LCDMW, "FIN   ");
        SYSCFG0 = 0; fase = 2; SYSCFG0 = PFWP | DFWP;     /* sin contraseña en las FR413x [UG FR2 figura 1-35] */
        espera_ms(40);
        cad("fin\n");
        for (;;) __bis_SR_register(LPM0_bits);
    }

    /* --- El arranque --- */
    gpio();
    reloj8();
    uart();
    cad("hola FR4133 "); dec(CS_getMCLK() / 1000); cad(" kHz rstiv "); hex(causas, 4);
    cad(" id "); hex(*(volatile uint16_t*)0x1A04, 4); pon('\n');
    lcd();
    escribe(LCDMW, "HOLA43");
    LCDM[12] = 0x04;                              /* LCDM12 bit 2: L24 con COM2, el corazón (HRT) [SLAU595B tabla 4] */
    espera_ms(40);
    cad("lcd hola\n");
    /* La memoria de parpadeo como segunda memoria (LCDDISP) */
    escribe(LCDBMW, "FR4133");
    LCD_E_selectDisplayMemory(LCD_E_BASE, LCD_E_DISPLAYSOURCE_BLINKINGMEMORY);
    espera_ms(40);
    cad("lcd bm\n");
    LCD_E_selectDisplayMemory(LCD_E_BASE, LCD_E_DISPLAYSOURCE_MEMORY);
    /* Las tramas: LCDFRMIFG cada 2 x 4 fases de fLCD = 32768 / (3 x 16) */
    tramas = 0;
    LCD_E_clearInterrupt(LCD_E_BASE, LCD_E_FRAME_INTERRUPT);
    LCD_E_enableInterrupt(LCD_E_BASE, LCD_E_FRAME_INTERRUPT);
    __bis_SR_register(GIE);
    espera_ms(100);
    LCD_E_disableInterrupt(LCD_E_BASE, LCD_E_FRAME_INTERRUPT);
    __bic_SR_register(GIE);
    cad("lcd tramas "); dec(tramas); pon('\n');
    /* Todo parpadea: fBLINK = fLCD / (4 x 2^(5+2)) = 1,33 Hz */
    LCD_E_setBlinkingControl(LCD_E_BASE, LCD_E_BLINK_FREQ_CLOCK_PRESCALAR_128, LCD_E_BLINK_MODE_ALL_SEGMENTS);
    cad("lcd parpadea\n");
    espera_ms(2000);
    LCD_E_setBlinkingControl(LCD_E_BASE, LCD_E_BLINK_FREQ_CLOCK_PRESCALAR_128, LCD_E_BLINK_MODE_DISABLED);
    espera_ms(60);                                /* dos tramas para verlo */
    cad("lcd quieto\n");
    /* El sensor de temperatura con la referencia de 1,5 V */
    ADC_init(ADC_BASE, ADC_SAMPLEHOLDSOURCE_SC, ADC_CLOCKSOURCE_ADCOSC, ADC_CLOCKDIVIDER_1);
    ADC_enable(ADC_BASE);
    ADC_setupSamplingTimer(ADC_BASE, ADC_CYCLEHOLD_1024_CYCLES, ADC_MULTIPLESAMPLESDISABLE);
    ADC_configureMemory(ADC_BASE, ADC_INPUT_TEMPSENSOR, ADC_VREFPOS_INT, ADC_VREFNEG_AVSS);
    while (PMM_REFGEN_NOTREADY == PMM_getVariableReferenceVoltageStatus()) ;
    PMM_enableTempSensor();
    PMM_enableInternalReference();
    ADC_startConversion(ADC_BASE, ADC_SINGLECHANNEL);
    while (!(ADCIFG & ADCIFG0)) ;
    {
        const long t30 = *(volatile uint16_t*)0x1A1A, t85 = *(volatile uint16_t*)0x1A1C;
        const long r = ADC_getResults(ADC_BASE);
        cad("temp "); dec(((r - t30) * (85 - 30)) * 10 / (t85 - t30) + 300); cad(" dC adc "); dec(r); pon('\n');
    }
    ADC_disable(ADC_BASE);
    PMM_disableTempSensor();
    PMM_disableInternalReference();
    /* Timer0_A3 a 1 ms con SMCLK = 8 MHz; TA1.1 (P4.0, LED2) a 1 kHz y 25 % */
    TA0CCR0 = 8000 - 1; TA0CCTL0 = CCIE; TA0CTL = TASSEL_2 | MC_1 | TACLR;
    P4SEL0 |= BIT0;
    TA1CCR0 = 8000 - 1; TA1CCR1 = 2000; TA1CCTL1 = OUTMOD_7; TA1CTL = TASSEL_2 | MC_1 | TACLR;
    __bis_SR_register(GIE);
    while (ticks < 10) ;
    TA0CTL = 0;
    __bic_SR_register(GIE);
    cad("ta0 10 ms\n");
    espera_ms(5);
    TA1CTL = 0; P4SEL0 &= ~BIT0;
    /* LPM3.5 con el LCD encendido y el RTC desde XT1: 3277 cuentas (100 ms) */
    escribe(LCDMW, "LPM35 ");
    LCDM[12] = 0;
    BAKMEM0 = 0x4133;
    SYSCFG0 = 0; fase = 1; SYSCFG0 = PFWP | DFWP;
    RTCMOD = 3276;
    RTCCTL = RTCSS__XT1CLK | RTCSR | RTCPS__1;
    (void)RTCIV;
    RTCCTL |= RTCIE;
    cad("lpm35 entra\n");
    fin_tx();
    PMMCTL0_H = PMMPW_H; PMMCTL0_L |= PMMREGOFF; PMMCTL0_L &= ~SVSHE; PMMCTL0_H = 0;
    __bis_SR_register(LPM3_bits);
    for (;;) ;
}

__attribute__((interrupt(TIMER0_A0_VECTOR))) void t0(void) { ++ticks; }
__attribute__((interrupt(LCD_E_VECTOR))) void lcd_isr(void) {
    switch (LCDIV) {
    case 8: ++tramas; break;               /* LCDFRMIFG (leer LCDIV la borra) */
    default: break;
    }
}
