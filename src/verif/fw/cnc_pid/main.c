/* ===========================================================================
 * main.c — Un PID de POSICIÓN con un motorreductor de 37 mm, su encoder Hall
 *          y un puente IBT-2, en una NUCLEO-F446RE
 *
 * El firmware de placas/nucleo_f446re_ibt2_jgb37.xml: el eje servo de
 * continua de la fase F1b de doc/analisis_cnc.md (§5.3), la alternativa al
 * paso a paso. El lazo lo cierra el firmware, con el encoder en un
 * temporizador en modo encoder:
 *
 *     IBT-2      RPWM   LPWM   R_EN   L_EN   VCC    GND
 *     Nucleo     D15    D14    A2     A3     +5V    GND
 *     conector   CN5.10 CN5.9  CN8.3  CN8.4  CN6.5  CN6.6
 *     chip       PB8    PB9    PA4    PB0
 *
 *     encoder    A      B      VCC    GND
 *     Nucleo     A0     A1     +3V3   GND
 *     conector   CN8.1  CN8.2  CN7.5  CN7.22
 *     chip       PA0    PA1    VDD    VSS
 *
 * EL PUENTE: R_EN y L_EN altos, y el PWM -el TIM4 en PB8 y PB9, canales 3 y 4,
 * AF2, a 1 kHz: el HSI de 16 MHz entre 16 es 1 MHz, y 1000 cuentas son 1 ms-
 * en RPWM para ir hacia delante (M+ más alto, el motor a la derecha) o en LPWM
 * para ir hacia atrás. CCR3 y CCR4 son el ciclo de trabajo en milésimas.
 *
 * EL ENCODER: el TIM2 con PA0 y PA1 (AF1) en modo encoder, SMS = 011, los dos
 * flancos de los dos canales: 330 pulsos por vuelta del eje de salida son
 * 1320 cuentas, hacia arriba girando a la derecha.
 *
 * EL LAZO, cada milisegundo en el SysTick: el error es la consigna menos la
 * cuenta, y el mando, en milésimas de PWM,
 *
 *     u = KP·e + KI·Σe − KD·(cuenta − cuenta de antes)
 *
 * con la derivada sobre la medida -no salta con la consigna- y la integral que
 * solo suma si el mando no está en el tope (antiwindup). El motorreductor no
 * arranca con menos del 16 % de PWM -su rozamiento de arranque-, así que fuera
 * de una zona muerta de 2 cuentas el mando no baja de ahí, y dentro se deja el
 * motor quieto y la integral se olvida. La consigna: 0 hasta
 * los 100 ms; una vuelta, 1320 cuentas, hasta los 1100; y media vuelta atrás,
 * -660, desde ahí. El LED LD2 (PA5) luce cuando el error es de 4 cuentas o
 * menos.
 * ===========================================================================*/
#include "stm32f446xx.h"

#define KP_NUM   10       /* KP: milésimas de PWM por cuenta */
#define KI_DEN   60       /* KI = 1/60 milésima por cuenta y milisegundo */
#define KD_NUM   250      /* KD: milésimas por cuenta por milisegundo */
#define U_MAX    1000
#define U_MIN    160      /* lo que hace falta para vencer el rozamiento de arranque */
#define ZONA     2        /* cuentas: dentro, el motor se deja quieto */

static volatile uint32_t g_ms;
static volatile int32_t  g_consigna;
static volatile int32_t  g_error;
static int32_t g_integral, g_antes;

static void modo(GPIO_TypeDef* g, uint32_t n, uint32_t m)
{
    g->MODER = (g->MODER & ~(3u << (2u * n))) | (m << (2u * n));
}
static void af(GPIO_TypeDef* g, uint32_t n, uint32_t f)
{
    modo(g, n, 2u);
    if (n < 8u) g->AFR[0] = (g->AFR[0] & ~(15u << (4u * n))) | (f << (4u * n));
    else        g->AFR[1] = (g->AFR[1] & ~(15u << (4u * (n - 8u)))) | (f << (4u * (n - 8u)));
}

/* El mando, de -1000 a 1000: el PWM en RPWM o en LPWM */
static void manda(int32_t u)
{
    if (u > U_MAX) u = U_MAX;
    if (u < -U_MAX) u = -U_MAX;
    if (u >= 0) { TIM4->CCR4 = 0; TIM4->CCR3 = (uint32_t)u; }
    else        { TIM4->CCR3 = 0; TIM4->CCR4 = (uint32_t)(-u); }
}

void SysTick_Handler(void)
{
    ++g_ms;
    if (g_ms == 100) g_consigna = 1320;
    if (g_ms == 1100) g_consigna = -660;
    const int32_t pos = (int32_t)TIM2->CNT;
    const int32_t e = g_consigna - pos;
    int32_t u = KP_NUM * e + g_integral / KI_DEN - KD_NUM * (pos - g_antes);
    g_antes = pos;
    /* Antiwindup: la integral, solo fuera del tope */
    if (u > -U_MAX && u < U_MAX) g_integral += e;
    /* El rozamiento de arranque: fuera de la zona muerta, el mando no baja
     * de lo que hace falta para moverlo; dentro, se deja quieto y la integral
     * se olvida */
    if (e >= -ZONA && e <= ZONA) { u = 0; g_integral = 0; }
    else if (u > 0 && u < U_MIN && e > 0) u = U_MIN;
    else if (u < 0 && u > -U_MIN && e < 0) u = -U_MIN;
    manda(u);
    g_error = e;
    if (e <= 4 && e >= -4) GPIOA->BSRR = 1u << 5;
    else                    GPIOA->BSRR = 1u << (5 + 16);
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM4EN;
    (void)RCC->APB1ENR;

    /* LD2, R_EN y L_EN */
    modo(GPIOA, 5u, 1u);
    modo(GPIOA, 4u, 1u);
    modo(GPIOB, 0u, 1u);

    /* El encoder: TIM2, PA0 y PA1 */
    af(GPIOA, 0u, 1u);
    af(GPIOA, 1u, 1u);
    TIM2->CCMR1 = (1u << TIM_CCMR1_CC1S_Pos) | (1u << TIM_CCMR1_CC2S_Pos);
    TIM2->CCER = 0;
    TIM2->SMCR = 3u << TIM_SMCR_SMS_Pos;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->CNT = 0;
    TIM2->CR1 = TIM_CR1_CEN;

    /* El PWM: TIM4, PB8 y PB9, a 1 kHz */
    af(GPIOB, 8u, 2u);
    af(GPIOB, 9u, 2u);
    TIM4->PSC = 15;
    TIM4->ARR = 999;
    TIM4->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                  (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM4->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM4->CCR3 = 0;
    TIM4->CCR4 = 0;
    TIM4->EGR = TIM_EGR_UG;
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    /* El puente, despierto */
    GPIOA->BSRR = 1u << 4;
    GPIOB->BSRR = 1u << 0;

    /* El lazo: el SysTick a 1 kHz, con el HSI de 16 MHz */
    SysTick->LOAD = 16000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;

    for (;;) __WFI();
}

/* El startup de ST llama a la inicializacion de la libc antes de main(); sin
 * libc, no hay nada que inicializar */
void __libc_init_array(void) { }
