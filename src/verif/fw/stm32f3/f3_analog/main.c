/* ===========================================================================
 * f3_analog — lo analogico del STM32F303VC (fase S5)
 *
 * Con la cabecera de dispositivo de ST y sus controladores LL (solo las
 * funciones en linea de stm32f3xx_ll_*.h, sin la HAL). Lee el modo en la
 * primera palabra del buzon (0x2000_0000) y deja ahi lo que mide, para que
 * testf3 lo compare con lo que pone el banco. Todos a 72 MHz por el HSE, con
 * los ADC al reloj asincrono del PLL (RCC_CFGR2.ADCxyPRES = /1), y cada uno
 * con el ritual completo: regulador, 10 us, calibracion simple (y diferencial
 * donde hace falta), ADEN hasta ADRDY.
 *
 *   1  TEMPERATURA  ADC1: el sensor (IN16) y VREFINT (IN18) a 601,5 ciclos;
 *                   VDDA con VREFINT_CAL y la temperatura con TS_CAL1/TS_CAL2,
 *                   con las macros de LL y en centesimas
 *   2  DIFERENCIAL  ADC1: IN6 diferencial (PC0 contra PC1) e IN8 simple (PC2),
 *                   por la interrupcion de EOC (ADC1_2_IRQn)
 *   3  DUAL + DMA   ADC1 (IN7, PC1) y ADC2 (IN5, PC4) en simultaneo regular,
 *                   continuo, con MDMA = 10 por el DMA1 canal 1 de una vez:
 *                   16 palabras de ADC_CDR
 *   4  COMP Y FRENO TIM1 PWM en PE9 (CH1) a 10 kHz; COMP4 (PB0 contra VREFINT)
 *                   con OUTSEL al freno de TIM1 y la linea 30 del EXTI
 *   5  PGA x4       OPAMP1 en PGA de 4, entrada PA1, salida PA2; ADC1 lee PA2
 *                   (IN3) y PA1 (IN2)
 *   6  DAC Y TIM3   DAC1 canal 1 con TSEL = 001 y DAC1_TRIG1_RMP: el TRGO del
 *                   TIM3; ADC2 lee PA4 (IN1)
 *
 * Cada vuelta de los modos 1, 2, 4 y 5 suma `vueltas`; el banco cambia las
 * entradas y espera a la siguiente.
 * ===========================================================================*/
#include "stm32f303xc.h"
#include "stm32f3xx_ll_adc.h"
#include "stm32f3xx_ll_comp.h"
#include "stm32f3xx_ll_opamp.h"
#include "stm32f3xx_ll_dac.h"

typedef struct {
    volatile uint32_t modo, estado, vueltas, errores;
    volatile int32_t  r[12];          /* lo que mide cada modo */
    volatile uint32_t dma[16];        /* el modo 3: ADC_CDR, 16 veces */
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static volatile uint32_t ms, n_irq_adc, n_irq_comp;
void SysTick_Handler(void) { ++ms; }
static void espera_ms(uint32_t n) { const uint32_t t = ms; while (ms - t < n) {} }
static void espera_us(uint32_t n) { for (volatile uint32_t i = 0; i < n * 20u; ++i) {} }

static void pll72(void) {
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL | RCC_CFGR_PPRE1)) |
                RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL9 | RCC_CFGR_PPRE1_DIV2;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SysTick->LOAD = 72000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    /* ADC12_CK y ADC34_CK: el PLL sin dividir (72 MHz) */
    RCC->CFGR2 = (RCC->CFGR2 & ~(RCC_CFGR2_ADCPRE12 | RCC_CFGR2_ADCPRE34)) |
                 RCC_CFGR2_ADCPRE12_DIV1 | RCC_CFGR2_ADCPRE34_DIV1;
}

/* Un pin en analogico */
static void analogico(GPIO_TypeDef* g, unsigned pin) { g->MODER |= 3u << (2 * pin); }

/* El ritual: regulador, calibracion(es) y ADEN [RM0316 15.3.6-15.3.9] */
static void enciende(ADC_TypeDef* a, int diferencial) {
    LL_ADC_EnableInternalRegulator(a);
    espera_us(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);
    LL_ADC_StartCalibration(a, LL_ADC_SINGLE_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(a)) {}
    if (diferencial) {
        LL_ADC_StartCalibration(a, LL_ADC_DIFFERENTIAL_ENDED);
        while (LL_ADC_IsCalibrationOnGoing(a)) {}
    }
    espera_us(1);
    LL_ADC_Enable(a);
    while (!LL_ADC_IsActiveFlag_ADRDY(a)) {}
}
/* Una conversion de un canal, por sondeo */
static uint32_t una(ADC_TypeDef* a, uint32_t canal) {
    LL_ADC_REG_SetSequencerRanks(a, LL_ADC_REG_RANK_1, canal);
    LL_ADC_REG_SetSequencerLength(a, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_StartConversion(a);
    while (!LL_ADC_IsActiveFlag_EOC(a)) {}
    return LL_ADC_REG_ReadConversionData12(a);
}

/* --- 1: el sensor de temperatura ------------------------------------------ */
static void temperatura(void) {
    RCC->AHBENR |= RCC_AHBENR_ADC12EN;
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_ASYNC_DIV1);
    LL_ADC_SetCommonPathInternalCh(ADC12_COMMON, LL_ADC_PATH_INTERNAL_TEMPSENSOR | LL_ADC_PATH_INTERNAL_VREFINT);
    enciende(ADC1, 0);
    /* 601,5 ciclos a 72 MHz: 8,4 us, mas de los 2,2 de TS_temp */
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SAMPLINGTIME_601CYCLES_5);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_VREFINT, LL_ADC_SAMPLINGTIME_601CYCLES_5);
    buzon.estado = 1;
    for (;;) {
        const uint32_t ts = una(ADC1, LL_ADC_CHANNEL_TEMPSENSOR);
        const uint32_t vr = una(ADC1, LL_ADC_CHANNEL_VREFINT);
        const uint32_t vdda = __LL_ADC_CALC_VREFANALOG_VOLTAGE(vr, LL_ADC_RESOLUTION_12B);
        buzon.r[0] = (int32_t)ts;
        buzon.r[1] = (int32_t)vr;
        buzon.r[2] = (int32_t)vdda;
        buzon.r[3] = __LL_ADC_CALC_TEMPERATURE(vdda, ts, LL_ADC_RESOLUTION_12B);
        /* La misma cuenta de RM0316 15.3.31, en centesimas */
        const int32_t c1 = (int32_t)*TEMPSENSOR_CAL1_ADDR, c2 = (int32_t)*TEMPSENSOR_CAL2_ADDR;
        const int32_t d = (int32_t)(ts * vdda / TEMPSENSOR_CAL_VREFANALOG);
        buzon.r[4] = (d - c1) * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) * 100 / (c2 - c1) +
                     TEMPSENSOR_CAL1_TEMP * 100;
        buzon.r[5] = (int32_t)ADC1->CALFACT;
        ++buzon.vueltas;
        espera_ms(1);
    }
}

/* --- 2: una conversion diferencial, por interrupcion ---------------------- */
static volatile uint32_t sec[2];
static volatile unsigned rango;
void ADC1_2_IRQHandler(void) {
    if (LL_ADC_IsActiveFlag_EOC(ADC1)) {
        sec[rango & 1u] = LL_ADC_REG_ReadConversionData12(ADC1);    /* borra EOC */
        ++rango;
    }
    if (LL_ADC_IsActiveFlag_EOS(ADC1)) { LL_ADC_ClearFlag_EOS(ADC1); ++n_irq_adc; }
}
static void diferencial(void) {
    RCC->AHBENR |= RCC_AHBENR_ADC12EN | RCC_AHBENR_GPIOCEN;
    analogico(GPIOC, 0); analogico(GPIOC, 1); analogico(GPIOC, 2);
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_ASYNC_DIV1);
    LL_ADC_SetChannelSingleDiff(ADC1, LL_ADC_CHANNEL_6, LL_ADC_DIFFERENTIAL_ENDED);
    enciende(ADC1, 1);
    /* 181,5 ciclos (2,5 us): que la interrupcion lea el rango 1 antes de que
     * acabe el 2; con 19,5 el dato del 2 llega con el 1 sin leer (OVR) */
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_6, LL_ADC_SAMPLINGTIME_181CYCLES_5);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_8, LL_ADC_SAMPLINGTIME_181CYCLES_5);
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_6);
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_2, LL_ADC_CHANNEL_8);
    LL_ADC_REG_SetSequencerLength(ADC1, LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS);
    LL_ADC_EnableIT_EOC(ADC1);
    LL_ADC_EnableIT_EOS(ADC1);
    NVIC_EnableIRQ(ADC1_2_IRQn);
    buzon.estado = 1;
    for (;;) {
        const uint32_t n = n_irq_adc;
        rango = 0;
        LL_ADC_REG_StartConversion(ADC1);
        while (n_irq_adc == n) {}
        buzon.r[0] = (int32_t)sec[0];
        buzon.r[1] = (int32_t)sec[1];
        /* La tension diferencial en mV: (2 * code / 4095 - 1) * VREF+ */
        buzon.r[2] = ((int32_t)sec[0] * 2 - 4095) * 3300 / 4095;
        buzon.r[3] = (int32_t)n_irq_adc;
        buzon.r[4] = (int32_t)ADC1->DIFSEL;
        buzon.r[5] = (int32_t)ADC1->CALFACT;
        ++buzon.vueltas;
        espera_ms(1);
    }
}

/* --- 3: dos ADC en simultaneo, con un DMA para los dos --------------------- */
static void dual(void) {
    RCC->AHBENR |= RCC_AHBENR_ADC12EN | RCC_AHBENR_GPIOCEN | RCC_AHBENR_DMA1EN;
    analogico(GPIOC, 1); analogico(GPIOC, 4);
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_ASYNC_DIV1);
    enciende(ADC1, 0);
    enciende(ADC2, 0);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_7, LL_ADC_SAMPLINGTIME_61CYCLES_5);
    LL_ADC_SetChannelSamplingTime(ADC2, LL_ADC_CHANNEL_5, LL_ADC_SAMPLINGTIME_61CYCLES_5);
    LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_7);
    LL_ADC_REG_SetSequencerRanks(ADC2, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_5);
    LL_ADC_REG_SetContinuousMode(ADC1, LL_ADC_REG_CONV_CONTINUOUS);
    LL_ADC_REG_SetContinuousMode(ADC2, LL_ADC_REG_CONV_CONTINUOUS);
    LL_ADC_SetMultimode(ADC12_COMMON, LL_ADC_MULTI_DUAL_REG_SIMULT);
    LL_ADC_SetMultiDMATransfer(ADC12_COMMON, LL_ADC_MULTI_REG_DMA_LIMIT_RES12_10B);
    /* El DMA1, canal 1: de ADC12_COMMON->CDR a buzon.dma, 32 bits, 16 veces */
    DMA1_Channel1->CPAR = (uint32_t)&ADC12_COMMON->CDR;
    DMA1_Channel1->CMAR = (uint32_t)&buzon.dma[0];
    DMA1_Channel1->CNDTR = 16;
    DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_PSIZE_1 | DMA_CCR_MSIZE_1 | DMA_CCR_EN;
    buzon.estado = 1;
    const uint32_t t0 = ms;
    LL_ADC_REG_StartConversion(ADC1);           /* el maestro arranca a los dos */
    while (!(DMA1->ISR & DMA_ISR_TCIF1)) { if (ms - t0 > 50) { ++buzon.errores; break; } }
    LL_ADC_REG_StopConversion(ADC1);
    while (LL_ADC_REG_IsStopConversionOngoing(ADC1)) {}
    buzon.r[0] = (int32_t)DMA1_Channel1->CNDTR;
    buzon.r[1] = (int32_t)(ADC12_COMMON->CCR & (ADC_CCR_DUAL | ADC_CCR_MDMA));
    buzon.r[2] = (int32_t)ADC1->ISR;
    buzon.r[3] = (int32_t)(ADC1->CR & ADC_CR_ADSTART);
    buzon.estado = 2;
    for (;;) {}
}

/* --- 4: un comparador que frena el TIM1 ------------------------------------ */
void COMP4_5_6_IRQHandler(void) {
    if (EXTI->PR & (1u << 30)) { EXTI->PR = 1u << 30; ++n_irq_comp; }
}
static void freno(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOEEN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_SYSCFGEN;
    /* PE9: TIM1_CH1 (AF2) */
    GPIOE->MODER = (GPIOE->MODER & ~(3u << 18)) | (2u << 18);
    GPIOE->AFR[1] = (GPIOE->AFR[1] & ~(0xFu << 4)) | (2u << 4);
    analogico(GPIOB, 0);
    /* 10 kHz al 50 %: 72 MHz / 7200 */
    TIM1->PSC = 0;
    TIM1->ARR = 7200 - 1;
    TIM1->CCR1 = 3600;
    TIM1->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM1->CCER = TIM_CCER_CC1E;
    TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_BKE | TIM_BDTR_BKP;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;
    TIM1->CR1 = TIM_CR1_CEN;
    /* COMP4: PB0 contra VREFINT, al freno de TIM1 (BRK_ACTH), con histeresis baja */
    LL_COMP_SetInputPlus(COMP4, LL_COMP_INPUT_PLUS_IO1);
    LL_COMP_SetInputMinus(COMP4, LL_COMP_INPUT_MINUS_VREFINT);
    LL_COMP_SetInputHysteresis(COMP4, LL_COMP_HYSTERESIS_LOW);
    LL_COMP_SetOutputSelection(COMP4, LL_COMP_OUTPUT_TIM1_BKIN);
    LL_COMP_SetOutputPolarity(COMP4, LL_COMP_OUTPUTPOL_NONINVERTED);
    EXTI->RTSR |= 1u << 30;
    EXTI->IMR |= 1u << 30;
    NVIC_EnableIRQ(COMP4_5_6_IRQn);
    LL_COMP_Enable(COMP4);
    buzon.estado = 1;
    for (;;) {
        buzon.r[0] = (int32_t)TIM1->BDTR;
        buzon.r[1] = (int32_t)TIM1->SR;
        buzon.r[2] = (int32_t)COMP4->CSR;
        buzon.r[3] = (int32_t)n_irq_comp;
        buzon.r[4] = (int32_t)LL_COMP_ReadOutputLevel(COMP4);
        ++buzon.vueltas;
        espera_ms(1);
    }
}

/* --- 5: el OPAMP1 en PGA de 4, y el ADC1 que lo lee ------------------------ */
static void pga(void) {
    RCC->AHBENR |= RCC_AHBENR_ADC12EN | RCC_AHBENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    analogico(GPIOA, 1); analogico(GPIOA, 2);
    LL_OPAMP_SetFunctionalMode(OPAMP1, LL_OPAMP_MODE_PGA);
    LL_OPAMP_SetPGAGain(OPAMP1, LL_OPAMP_PGA_GAIN_4);
    LL_OPAMP_SetInputNonInverting(OPAMP1, LL_OPAMP_INPUT_NONINVERT_IO0);   /* PA1 */
    LL_OPAMP_Enable(OPAMP1);
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_ASYNC_DIV1);
    enciende(ADC1, 0);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_2, LL_ADC_SAMPLINGTIME_19CYCLES_5);
    LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_3, LL_ADC_SAMPLINGTIME_19CYCLES_5);
    buzon.estado = 1;
    for (;;) {
        buzon.r[0] = (int32_t)una(ADC1, LL_ADC_CHANNEL_3);     /* VOUT, PA2 */
        buzon.r[1] = (int32_t)una(ADC1, LL_ADC_CHANNEL_2);     /* VINP, PA1 */
        buzon.r[2] = (int32_t)OPAMP1->CSR;
        ++buzon.vueltas;
        espera_ms(1);
    }
}

/* --- 6: el DAC al ritmo del TIM3, por el remapeo del F3 --------------------- */
static volatile uint32_t paso;
void TIM3_IRQHandler(void) {
    static const uint16_t rampa[4] = {512, 1536, 2560, 3584};
    TIM3->SR = 0;
    LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, rampa[++paso & 3u]);
}
static void dac(void) {
    RCC->AHBENR |= RCC_AHBENR_ADC12EN | RCC_AHBENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_DAC1EN | RCC_APB1ENR_TIM3EN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    analogico(GPIOA, 4);
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_DAC1_TRIG1_RMP;           /* TSEL = 001: TIM3_TRGO */
    LL_DAC_SetTriggerSource(DAC1, LL_DAC_CHANNEL_1, LL_DAC_TRIG_EXT_TIM3_TRGO);
    LL_DAC_EnableTrigger(DAC1, LL_DAC_CHANNEL_1);
    LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, 512);
    LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_ASYNC_DIV1);
    enciende(ADC2, 0);
    LL_ADC_SetChannelSamplingTime(ADC2, LL_ADC_CHANNEL_1, LL_ADC_SAMPLINGTIME_61CYCLES_5);
    espera_ms(1);
    buzon.r[0] = (int32_t)DAC1->DOR1;            /* sin disparo, sigue a cero */
    buzon.r[1] = (int32_t)una(ADC2, LL_ADC_CHANNEL_1);
    /* TIM3 a 1 kHz, TRGO en la actualizacion (MMS = 010) */
    TIM3->PSC = 72 - 1;
    TIM3->ARR = 1000 - 1;
    TIM3->CR2 = 2u << TIM_CR2_MMS_Pos;
    TIM3->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM3_IRQn);
    TIM3->CR1 = TIM_CR1_CEN;
    buzon.estado = 1;
    for (;;) {
        const uint32_t p = paso;
        while (paso == p) {}
        espera_us(100);                       /* dentro del periodo, despues del disparo */
        buzon.r[2] = (int32_t)DAC1->DOR1;
        buzon.r[3] = (int32_t)una(ADC2, LL_ADC_CHANNEL_1);
        buzon.r[4] = (int32_t)paso;
        ++buzon.vueltas;
    }
}

int main(void) {
    pll72();
    switch (buzon.modo) {
        case 1: temperatura(); break;
        case 2: diferencial(); break;
        case 3: dual(); break;
        case 4: freno(); break;
        case 5: pga(); break;
        case 6: dac(); break;
        default: break;
    }
    buzon.estado = 0xEEu;
    for (;;) {}
}
