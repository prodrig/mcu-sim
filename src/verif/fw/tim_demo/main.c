/* ===========================================================================
 * main.c — Firmware de demostración de los temporizadores, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo. Ejercita, a la vez, cuatro familias distintas
 * del mismo bloque de temporizador:
 *
 *   TIM4 (propósito general, 16 bits)  PWM de 1 kHz al 25 % sobre PD12 (AF2),
 *                                      que es el LED verde de la placa;
 *   TIM3 (propósito general, 16 bits)  captura de entrada en PB4 (AF2), unido
 *                                      a PD12 por una pista de la placa: mide
 *                                      el periodo del PWM que genera TIM4;
 *   TIM2 (propósito general, 32 bits)  base de tiempos libre que demuestra que
 *                                      el contador pasa de 0xFFFF;
 *   TIM7 (básico)                      interrupción de update cada milisegundo.
 *
 * Que el mismo driver mínimo (PSC/ARR/EGR/CEN) sirva para los cuatro es lo que
 * comprueba la parametrización del modelo.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define PWM_HZ     1000u        /* frecuencia del PWM de TIM4                */
#define PWM_DUTY   25u          /* ciclo de trabajo, en tanto por ciento     */
#define N_TICKS    10u          /* interrupciones de TIM7 antes de terminar  */

volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado        */
    volatile uint32_t pwm_ok;    /* 1 si el PWM de TIM4 quedó en marcha      */
    volatile uint32_t up_irq;    /* interrupciones de update de TIM7         */
    volatile uint32_t cap_us;    /* periodo medido por la captura de TIM3    */
    volatile uint32_t cnt32;     /* CNT de TIM2 (32 bits) al terminar        */
    volatile uint32_t timclk;    /* TIMCLK1 que ha usado                     */
} mbox __attribute__((section(".mailbox")));

static volatile uint32_t tick_count;

void TIM7_IRQHandler(void)
{
    if (TIM7->SR & TIM_SR_UIF) {
        TIM7->SR = (uint16_t)~TIM_SR_UIF;
        ++tick_count;
    }
}

static void clock_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) { }
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SystemCoreClockUpdate();
}

/* Un pin en función alternativa (la misma rutina para todos los canales) */
static void pin_af(GPIO_TypeDef *g, uint32_t pin, uint32_t af)
{
    g->MODER   = (g->MODER   & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->OTYPER &= ~(1u << pin);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * pin))) | (3u << (2 * pin));
    g->PUPDR  &= ~(3u << (2 * pin));
    if (pin < 8u) g->AFR[0] = (g->AFR[0] & ~(0xFu << (4 * pin))) | (af << (4 * pin));
    else          g->AFR[1] = (g->AFR[1] & ~(0xFu << (4 * (pin - 8u)))) |
                              (af << (4 * (pin - 8u)));
}

/* Driver mínimo de base de tiempos: el MISMO para el básico, los de propósito
 * general de 16 bits y el de 32 bits. Solo usa lo que todos comparten. */
static void tim_base(TIM_TypeDef *t, uint32_t psc, uint32_t arr)
{
    t->CR1  = 0u;
    t->PSC  = psc;
    t->ARR  = arr;
    t->EGR  = TIM_EGR_UG;            /* carga PSC y ARR, reinicia el contador */
    t->SR   = 0u;                    /* borra el UIF de la reinicialización   */
    t->CR1  = TIM_CR1_CEN;
}

int main(void)
{
    mbox.done = 0u; mbox.pwm_ok = 0u; mbox.up_irq = 0u;
    mbox.cap_us = 0u; mbox.cnt32 = 0u; mbox.timclk = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN |
                    RCC_APB1ENR_TIM4EN | RCC_APB1ENR_TIM7EN;

    /* TIMCLK1 = 2 x PCLK1 porque el prescaler del APB1 no es 1 [IR, 4.4] */
    const uint32_t pclk1  = SystemCoreClock / 4u;
    const uint32_t timclk = 2u * pclk1;
    mbox.timclk = timclk;

    /* Un paso de cuenta por microsegundo en los tres temporizadores */
    const uint32_t psc_1us = (timclk / 1000000u) - 1u;

    pin_af(GPIOD, 12u, 2u);          /* PD12 = TIM4_CH1 (LED verde)          */
    pin_af(GPIOB,  4u, 2u);          /* PB4  = TIM3_CH1 (entrada de captura) */

    /* --- TIM2: base de tiempos libre de 32 bits -------------------------- */
    tim_base(TIM2, 0u, 0xFFFFFFFFu);

    /* --- TIM4: PWM modo 1 al 25 % sobre el canal 1 ----------------------- */
    const uint32_t period = (1000000u / PWM_HZ) - 1u;     /* en microsegundos */
    TIM4->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM4->CCER  = TIM_CCER_CC1E;
    TIM4->CCR1  = ((period + 1u) * PWM_DUTY) / 100u;
    TIM4->CR1   = TIM_CR1_ARPE;
    tim_base(TIM4, psc_1us, period);
    TIM4->CR1  |= TIM_CR1_ARPE;

    /* --- TIM3: captura de entrada en el canal 1, flanco de subida -------- */
    TIM3->CCMR1 = TIM_CCMR1_CC1S_0;                       /* CC1S = 01 (TI1)  */
    TIM3->CCER  = TIM_CCER_CC1E;
    tim_base(TIM3, psc_1us, 0xFFFFu);

    /* --- TIM7: interrupción de update cada milisegundo ------------------- */
    TIM7->DIER = TIM_DIER_UIE;
    tim_base(TIM7, psc_1us, 999u);
    NVIC_EnableIRQ(TIM7_IRQn);

    /* --- Espera activa: recoge dos capturas y cuenta las interrupciones --- */
    uint32_t cap_prev = 0u, cap_last = 0u, n_cap = 0u;
    for (uint32_t guard = 0; guard < 4000000u && tick_count < N_TICKS; ++guard) {
        if (TIM3->SR & TIM_SR_CC1IF) {
            cap_prev = cap_last;
            cap_last = TIM3->CCR1;                        /* leerlo borra CC1IF */
            ++n_cap;
        }
    }

    if (n_cap >= 2u) mbox.cap_us = (cap_last - cap_prev) & 0xFFFFu;
    mbox.up_irq = tick_count;
    mbox.cnt32  = TIM2->CNT;
    mbox.pwm_ok = ((TIM4->CR1 & TIM_CR1_CEN) != 0u) &&
                  (TIM4->CCR1 == ((period + 1u) * PWM_DUTY) / 100u) &&
                  (n_cap >= 2u);

    TIM7->CR1 = 0u;                                       /* para el reloj de 1 ms */
    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
