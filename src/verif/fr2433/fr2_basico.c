/* fr2_basico.c — testfr2 (M6): lo que hace un programa de la FR2433 con la
 * DriverLib de TI (la del paquete de software de la MSP-EXP430FR2433):
 *
 *   arranque  el FLL a 16 MHz desde REFO con el recorte de DCOFTRIM por
 *             programa (CS_initFLLCalculateTrim) y un estado de espera de la
 *             FRAM; la UCA0 (el backchannel, P1.4/P1.5) a 115200; el ID de la
 *             TLV; el sensor de temperatura con la referencia de 1,5 V del
 *             PMM y la calibración de la TLV; el MPY32; el CRC16; la
 *             protección de la FRAM (SYSCFG0: PFWP y DFWP); Timer0_A3 a 1 ms;
 *             y LPM3.5 con el RTC (VLO) para despertar: antes, la BAKMEM,
 *             una variable en RAM y P1.1 arriba.
 *   LPM3.5    el despertar: SYSRSTIV = 8 y PMMLPM5IFG, RTCIFG, la BAKMEM
 *             sigue y la RAM no; P1.1 sigue arriba hasta borrar LOCKLPM5. Y
 *             LPM4.5 (el RTC parado) con P2.3 para despertar.
 *   LPM4.5    el despertar por P2.3: P2IFG.3 puesto; la BAKMEM, perdida.
 *
 * La fase va en la FRAM (.persistent). Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }

__attribute__((section(".persistent"))) volatile uint16_t fase = 0;
__attribute__((section(".persistent"))) volatile uint16_t prot = 0x1111;
__attribute__((section(".noinit"))) volatile uint16_t en_ram;
volatile unsigned ticks;

static void uart(void) {
    EUSCI_A_UART_initParam p = {0};
    P1SEL0 |= BIT4 | BIT5;
    p.selectClockSource = EUSCI_A_UART_CLOCKSOURCE_SMCLK;
    p.clockPrescalar = 8; p.firstModReg = 10; p.secondModReg = 0xF7;      /* 115200 con SMCLK = 16 MHz */
    p.parity = EUSCI_A_UART_NO_PARITY; p.msborLsbFirst = EUSCI_A_UART_LSB_FIRST;
    p.numberofStopBits = EUSCI_A_UART_ONE_STOP_BIT; p.uartMode = EUSCI_A_UART_MODE;
    p.overSampling = EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION;
    EUSCI_A_UART_init(EUSCI_A0_BASE, &p);
    EUSCI_A_UART_enable(EUSCI_A0_BASE);
}
static void reloj16(void) {
    CS_initFLLParam t = {0};
    FRAMCtl_configureWaitStateControl(FRAMCTL_ACCESS_TIME_CYCLES_1);   /* > 8 MHz: un estado de espera */
    CS_initClockSignal(CS_FLLREF, CS_REFOCLK_SELECT, CS_CLOCK_DIVIDER_1);
    CS_initClockSignal(CS_ACLK, CS_REFOCLK_SELECT, CS_CLOCK_DIVIDER_1);
    CS_initFLLCalculateTrim(16000, 488, &t);                             /* 16 MHz = 488 x 32768 */
    CS_clearAllOscFlagsWithTimeout(1000);
}
static uint16_t rstiv(void) {
    /* todas las causas pendientes, en una máscara (bit = valor/2) */
    uint16_t m = 0, v;
    while ((v = SYSRSTIV) != 0) m |= 1u << (v / 2);
    return m;
}

int main(void) {
    const uint16_t ifg = PMMIFG;
    WDT_A_hold(WDT_A_BASE);
    const uint16_t causas = rstiv();
    if (fase == 1) {
        /* Despierto de LPM3.5: los pines siguen quietos un rato (LOCKLPM5) */
        __delay_cycles(4000);
        reloj16();
        uart();
        P1DIR |= BIT0;
        PMM_unlockLPM5();
        cad("lpm35 rstiv "); hex(causas, 4); cad(" ifg "); hex(ifg, 4);
        cad(" rtc "); hex(RTCCTL & RTCIF, 1); cad(" bak "); hex(BAKMEM0, 4);
        cad(en_ram == 0x1234 ? " ram sigue\n" : " ram perdida\n");
        /* LPM4.5: el RTC parado; despierta P2.3 (con su resistencia a VCC) */
        P2DIR &= ~BIT3; P2OUT |= BIT3; P2REN |= BIT3; P2IES |= BIT3; P2IFG = 0; P2IE |= BIT3;
        RTCCTL = 0;
        SYSCFG0 = FRWPPW; fase = 2; SYSCFG0 = FRWPPW | PFWP | DFWP;
        cad("lpm45 entra\n");
        fin_tx();
        PMMCTL0_H = PMMPW_H; PMMCTL0_L |= PMMREGOFF; PMMCTL0_L &= ~SVSHE; PMMCTL0_H = 0;
        __bis_SR_register(LPM4_bits);
        for (;;) ;
    }
    if (fase == 2) {
        const uint8_t p2 = P2IFG;
        reloj16();
        uart();
        PMM_unlockLPM5();
        cad("lpm45 rstiv "); hex(causas, 4); cad(" ifg "); hex(ifg, 4); cad(" p2ifg "); hex(p2, 2);
        cad(" bak "); hex(BAKMEM0, 4); pon('\n');
        SYSCFG0 = FRWPPW; fase = 3; SYSCFG0 = FRWPPW | PFWP | DFWP;
        cad("fin\n");
        for (;;) __bis_SR_register(LPM0_bits);
    }

    /* --- El arranque --- */
    reloj16();
    uart();
    P1DIR |= BIT0 | BIT1;
    P1OUT &= ~(BIT0 | BIT1);
    PMM_unlockLPM5();
    cad("hola FR2433 "); dec(CS_getMCLK() / 1000); cad(" kHz rstiv "); hex(causas, 4); pon('\n');
    cad("fll dco "); dec(CSCTL0 & 0x1FF); cad(" trim "); dec((CSCTL1 >> 4) & 7); cad(" id "); hex(*(volatile uint16_t*)0x1A04, 4); pon('\n');
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
    /* MPY32 (las rutinas de multiplicar de libgcc lo usan) */
    {
        volatile uint32_t a = 123456789ul, b = 1000ul;
        volatile int32_t c = -77777, e = 3;
        volatile uint64_t x = (uint64_t)a * b;
        cad("mpy "); hex((uint32_t)(x >> 32), 8); hex((uint32_t)x, 8); pon(' '); dec(c * e); pon('\n');
    }
    /* CRC16 de «123456789» por CRCDIRB, semilla 0xFFFF: 0x29B1 */
    {
        const char* s = "123456789";
        CRC_setSeed(CRC_BASE, 0xFFFF);
        while (*s) CRC_set8BitDataReversed(CRC_BASE, (uint8_t)*s++);
        cad("crc "); hex(CRC_getResult(CRC_BASE), 4); pon('\n');
    }
    /* La protección de la FRAM: con PFWP no se escribe; una contraseña mala
     * no la quita; sin PFWP, sí. Y la de información con DFWP */
    {
        uint16_t a, b, c, d;
        prot = 0x2222;                             /* protegida (PFWP del reset) */
        a = prot;
        SYSCFG0 = 0x0000;                          /* sin FRWPPW: no cambia */
        prot = 0x2222;
        b = prot;
        SYSCFG0 = FRWPPW;                          /* abierta */
        prot = 0x3333;
        c = prot;
        *(volatile uint16_t*)0x1800 = 0x5678;
        SYSCFG0 = FRWPPW | DFWP;                   /* la información, cerrada otra vez */
        *(volatile uint16_t*)0x1800 = 0x9999;
        d = *(volatile uint16_t*)0x1800;
        SYSCFG0 = FRWPPW | PFWP | DFWP;
        cad("fram "); hex(a, 4); pon(' '); hex(b, 4); pon(' '); hex(c, 4); pon(' '); hex(d, 4); pon('\n');
    }
    /* Timer0_A3 a 1 ms con SMCLK = 16 MHz y la interrupción de CCR0 */
    TA0CCR0 = 16000 - 1; TA0CCTL0 = CCIE; TA0CTL = TASSEL_2 | MC_1 | TACLR;
    __bis_SR_register(GIE);
    while (ticks < 10) ;
    TA0CTL = 0;
    __bic_SR_register(GIE);
    cad("ta0 10 ms\n");
    /* LPM3.5 con el RTC desde el VLO: 500 cuentas (50 ms) */
    BAKMEM0 = 0xBEEF;
    en_ram = 0x1234;
    P1OUT |= BIT1;                                 /* P1.1 arriba: se queda así en LPM3.5 */
    SYSCFG0 = FRWPPW; fase = 1; SYSCFG0 = FRWPPW | PFWP | DFWP;
    RTCMOD = 499;
    RTCCTL = RTCSS__VLOCLK | RTCSR | RTCPS__1;
    (void)RTCIV;
    RTCCTL |= RTCIE;
    cad("lpm35 entra\n");
    fin_tx();
    PMMCTL0_H = PMMPW_H; PMMCTL0_L |= PMMREGOFF; PMMCTL0_L &= ~SVSHE; PMMCTL0_H = 0;
    __bis_SR_register(LPM3_bits);
    for (;;) ;
}

__attribute__((interrupt(TIMER0_A0_VECTOR))) void t0(void) { ++ticks; }
