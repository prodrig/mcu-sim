/* ===========================================================================
 * main.c — Blinky de referencia compilado con CMSIS (criterio de salida de F3)
 *
 * Firmware "de verdad": usa la cabecera de dispositivo de ST (stm32f407xx.h),
 * el CMSIS-Core del Cortex-M4 (core_cm4.h) y el startup y el system_stm32f4xx.c
 * oficiales, sin ninguna adaptación al modelo. Hace lo que hace el ejemplo
 * clásico de la placa STM32F4-Discovery:
 *
 *   1. programa los estados de espera de la Flash y el acelerador ART;
 *   2. arranca el HSE con el cristal de 8 MHz y engancha el PLL a 168 MHz
 *      (M=8, N=336, P=2, Q=7), con PCLK1 = 42 MHz y PCLK2 = 84 MHz;
 *   3. conmuta SYSCLK al PLL y espera a que SWS lo confirme;
 *   4. configura PD12 como salida push-pull (LED verde) y PA0 como entrada con
 *      pull-down (pulsador de usuario);
 *   5. usa el SysTick a 1 ms y WFI para parpadear cada BLINK_MS.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el progreso.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define HSE_HZ        8000000u
#define SYSCLK_HZ     168000000u
#define BLINK_MS      100u
#define N_BLINKS      6u

/* Buzón compartido con el banco de pruebas (véase el linker script) */
volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado        */
    volatile uint32_t sysclk;    /* SYSCLK medido por el propio firmware     */
    volatile uint32_t toggles;   /* número de conmutaciones del LED          */
    volatile uint32_t button;    /* nivel leído en PA0 al final              */
    volatile uint32_t ticks;     /* interrupciones de SysTick atendidas      */
} mbox __attribute__((section(".mailbox")));

static volatile uint32_t g_ms;

void SysTick_Handler(void)
{
    ++g_ms;
    ++mbox.ticks;
}

static void clock_init(void)
{
    /* 1. Flash: 5 estados de espera para 168 MHz a 3.3 V, con prefetch,
     *    caché de instrucciones y de datos [IR, §5.2.2, §5.2.3].          */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;

    /* 2. HSE con cristal externo */
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) { }

    /* 3. Prescalers: AHB /1, APB1 /4 (42 MHz), APB2 /2 (84 MHz) */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* 4. PLL: VCO_IN = 8/8 = 1 MHz, VCO_OUT = 336 MHz, P = 2 -> 168 MHz,
     *    Q = 7 -> 48 MHz para USB/SDIO/RNG [IR, §4.3.1].                  */
    RCC->PLLCFGR = (8u  << RCC_PLLCFGR_PLLM_Pos) |
                   (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u  << RCC_PLLCFGR_PLLP_Pos) |    /* 00 = /2 */
                   (7u  << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }

    /* 5. SYSCLK = PLL */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }

    SystemCoreClockUpdate();
}

static void gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOAEN;

    /* PD12: salida push-pull, sin pull, velocidad baja (LED verde) */
    GPIOD->MODER   = (GPIOD->MODER   & ~GPIO_MODER_MODER12)   | GPIO_MODER_MODER12_0;
    GPIOD->OTYPER &= ~GPIO_OTYPER_OT12;
    GPIOD->OSPEEDR = (GPIOD->OSPEEDR & ~GPIO_OSPEEDR_OSPEED12);
    GPIOD->PUPDR   = (GPIOD->PUPDR   & ~GPIO_PUPDR_PUPD12);

    /* PA0-WKUP: entrada con pull-down (el pulsador cierra a VDD en la placa;
     * aquí basta con demostrar el camino de entrada del pad al IDR).       */
    GPIOA->MODER &= ~GPIO_MODER_MODER0;
    GPIOA->PUPDR  = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD0) | GPIO_PUPDR_PUPD0_1;
}

static void delay_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) { __WFI(); }
}

int main(void)
{
    /* El buzón vive en una sección NOLOAD: lo inicializa el propio firmware. */
    mbox.done = 0u; mbox.sysclk = 0u; mbox.toggles = 0u;
    mbox.button = 0u; mbox.ticks = 0u;

    clock_init();
    gpio_init();

    mbox.sysclk = SystemCoreClock;

    /* SysTick a 1 kHz desde el reloj del procesador */
    SysTick_Config(SystemCoreClock / 1000u);

    for (uint32_t i = 0; i < N_BLINKS; ++i) {
        GPIOD->BSRR = (i & 1u) ? (GPIO_BSRR_BR12) : (GPIO_BSRR_BS12);
        ++mbox.toggles;
        delay_ms(BLINK_MS);
    }

    GPIOD->BSRR = GPIO_BSRR_BR12;        /* LED apagado al terminar */
    mbox.button = (GPIOA->IDR & GPIO_IDR_ID0) ? 1u : 0u;
    mbox.done   = 1u;

    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
