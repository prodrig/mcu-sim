/* ===========================================================================
 * main.c — Firmware de demostración de EXTI y SYSCFG, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo. Ejercita los dos caminos del EXTI:
 *
 *   1. CAMINO DE EVENTO. La línea 1 se desenmascara SOLO en EXTI_EMR (no en
 *      EXTI_IMR) y se dispara por software con EXTI_SWIER. El pulso de evento
 *      arma el registro de evento del núcleo, de modo que el WFI/WFE siguiente
 *      vuelve de inmediato SIN pasar por ningún manejador de interrupción.
 *
 *   2. CAMINO DE INTERRUPCIÓN. SYSCFG_EXTICR1 encamina la línea 0 al puerto A,
 *      de forma que el pulsador de usuario de PA0 (con pull-up interno) genera
 *      un flanco de bajada que entra por el vector EXTI0. El manejador cuenta
 *      la pulsación y conmuta el LED verde de PD12.
 *
 * Entre pulsaciones el núcleo espera en WFI, que es como se usa el EXTI en un
 * sistema real: el consumo lo marca el tiempo que la CPU pasa dormida.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define N_PRESSES   4u

volatile struct {
    volatile uint32_t done;       /* 1 cuando el firmware ha terminado        */
    volatile uint32_t n_press;    /* interrupciones EXTI0 atendidas           */
    volatile uint32_t n_evt;      /* pulsos de evento consumidos por el WFE   */
    volatile uint32_t pr_seen;    /* EXTI_PR tal como lo vio el manejador     */
    volatile uint32_t exticr;     /* SYSCFG_EXTICR1 programado                */
    volatile uint32_t woke;       /* 1 si el WFE volvio por el evento         */
    volatile uint32_t armed;      /* 1 cuando el EXTI ya esta configurado     */
} mbox __attribute__((section(".mailbox")));

static volatile uint32_t n_press;
static volatile uint32_t pr_seen;

void EXTI0_IRQHandler(void)
{
    pr_seen = EXTI->PR & 0x1u;
    EXTI->PR = 0x1u;                     /* rc_w1: escribir uno borra          */
    GPIOD->ODR ^= (1u << 12);            /* conmuta el LED verde de la placa   */
    ++n_press;
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

int main(void)
{
    mbox.done = 0u; mbox.n_press = 0u; mbox.n_evt = 0u;
    mbox.pr_seen = 0u; mbox.exticr = 0u; mbox.woke = 0u; mbox.armed = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;      /* los EXTICR viven en SYSCFG */

    /* PA0: entrada con pull-up (el pulsador cierra a masa) */
    GPIOA->MODER &= ~(3u << (2 * 0));
    GPIOA->PUPDR  = (GPIOA->PUPDR & ~(3u << (2 * 0))) | (1u << (2 * 0));
    /* PD12: salida push-pull (LED verde) */
    GPIOD->MODER  = (GPIOD->MODER  & ~(3u << (2 * 12))) | (1u << (2 * 12));
    GPIOD->OSPEEDR= (GPIOD->OSPEEDR& ~(3u << (2 * 12))) | (1u << (2 * 12));
    GPIOD->BSRR   = (1u << (12 + 16));         /* LED apagado */

    /* --- 1. Camino de EVENTO: linea 1 solo en EMR ----------------------- */
    /* Sin IMR no hay interrupcion ni entrada en ningun manejador: el pulso
     * de evento arma el registro de evento del nucleo y el WFE vuelve. Si el
     * modelo perdiera el pulso, este WFE no volveria nunca.                */
    EXTI->IMR  &= ~(1u << 1);
    EXTI->EMR  |=  (1u << 1);
    EXTI->SWIER =  (1u << 1);
    __WFE();
    mbox.woke  = 1u;
    mbox.n_evt = 1u;
    EXTI->EMR   &= ~(1u << 1);
    EXTI->SWIER  = 0u;
    EXTI->PR     = (1u << 1);

    /* --- 2. Camino de INTERRUPCION: PA0 -> linea 0 -> vector EXTI0 ------- */
    /* SYSCFG_EXTICR1[3:0] = 0000 selecciona el puerto A para la linea 0.   */
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~0xFu) | 0x0u;
    mbox.exticr = SYSCFG->EXTICR[0];
    EXTI->RTSR &= ~(1u << 0);                  /* solo flanco de bajada     */
    EXTI->FTSR |=  (1u << 0);
    EXTI->PR    =  (1u << 0);                  /* linea limpia              */
    EXTI->IMR  |=  (1u << 0);
    NVIC_SetPriority(EXTI0_IRQn, 2);
    NVIC_EnableIRQ(EXTI0_IRQn);

    mbox.armed = 1u;                           /* el banco ya puede pulsar  */

    /* Entre pulsaciones, la CPU duerme */
    for (uint32_t guard = 0; guard < 200000u && n_press < N_PRESSES; ++guard) {
        __WFI();
        mbox.n_press = n_press;
        mbox.pr_seen = pr_seen;
    }

    mbox.n_press = n_press;
    mbox.pr_seen = pr_seen;
    mbox.done    = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
