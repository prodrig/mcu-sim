/* ===========================================================================
 * main.c — Blinky de una NUCLEO-F446RE (criterio de salida de la fase 2)
 *
 * Firmware "de verdad", igual que el blinky del F407: usa la cabecera de
 * dispositivo de ST (stm32f446xx.h), el CMSIS-Core del Cortex-M4 (core_cm4.h)
 * y el startup y el system_stm32f4xx.c oficiales, **sin ninguna adaptación al
 * modelo**. Eso es justamente lo que lo hace una prueba: si las direcciones,
 * los bits o el mapa de memoria del modelo no fueran los del F446, este
 * programa no funcionaría, porque quien decide dónde está cada registro es la
 * cabecera de ST y no nosotros.
 *
 * La placa es la NUCLEO-F446RE de ST:
 *   LD2  LED de usuario, en **PA5** (en la Discovery era PD12, que en un
 *        LQFP64 ni siquiera sale)
 *   B1   pulsador azul, en **PC13**, a masa y con pull-up interno
 *
 * QUÉ RELOJ PROGRAMA, Y POR QUÉ NO 180 MHz. Este blinky se queda en 84 MHz
 * desde el HSI. Llegar a los 180 MHz del F446 exige la secuencia de over-drive
 * del PWR (`ODEN` -> `ODRDY` -> `ODSWEN` -> `ODSWRDY`), que es lo que hace el
 * `SystemClock_Config()` que genera STM32CubeIDE y lo que la **fase 3** del
 * plan tiene que modelar. Con el modelo de hoy, esperar a `ODRDY` sería
 * esperar para siempre — y eso es exactamente el criterio de aceptación de esa
 * fase, que el modelo se cuelgue si no lo levanta.
 *
 * El buzón en el primer bloque de la SRAM permite al banco comprobar el
 * progreso sin mirar pines.
 * ===========================================================================*/
#include "stm32f446xx.h"

#define SYSCLK_HZ     84000000u
#define BLINK_MS      100u
#define N_BLINKS      6u

/* Buzón compartido con el banco de pruebas (véase el linker script) */
volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado        */
    volatile uint32_t sysclk;    /* SYSCLK medido por el propio firmware     */
    volatile uint32_t toggles;   /* número de conmutaciones del LED          */
    volatile uint32_t button;    /* nivel leído en PC13 al final             */
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
    /* 1. Flash: 2 estados de espera para 84 MHz a 3,3 V [RM0390, tabla 5],
     *    con prefetch y las dos cachés. La regla es la misma que en el F407
     *    -un estado más cada 30 MHz-; lo que cambia es el techo.          */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_2WS;

    /* 2. HSI: 16 MHz, y en una Nucleo es lo que hay sin tocar los puentes.
     *    Arranca solo tras el reset, pero se espera por si acaso.         */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) { }

    /* 3. Prescalers: AHB /1, APB1 /2 (42 MHz), APB2 /1 (84 MHz) */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    /* 4. PLL desde el HSI: VCO_IN = 16/16 = 1 MHz, VCO_OUT = 336 MHz,
     *    P = 4 -> 84 MHz, Q = 7 -> 48 MHz.                                */
    RCC->PLLCFGR = (16u  << RCC_PLLCFGR_PLLM_Pos) |
                   (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (1u   << RCC_PLLCFGR_PLLP_Pos) |   /* 01 = /4 */
                   (7u   << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSI;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }

    /* 5. SYSCLK = PLL */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }

    SystemCoreClockUpdate();
}

static void gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;

    /* PA5: salida push-pull, sin pull, velocidad baja (LD2) */
    GPIOA->MODER   = (GPIOA->MODER   & ~GPIO_MODER_MODER5)  | GPIO_MODER_MODER5_0;
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~GPIO_OSPEEDER_OSPEEDR5);
    GPIOA->PUPDR   = (GPIOA->PUPDR   & ~GPIO_PUPDR_PUPDR5);

    /* PC13: entrada con pull-up. En la Nucleo, B1 cierra a masa. */
    GPIOC->MODER &= ~GPIO_MODER_MODER13;
    GPIOC->PUPDR  = (GPIOC->PUPDR & ~GPIO_PUPDR_PUPDR13) | GPIO_PUPDR_PUPDR13_0;
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
        GPIOA->BSRR = (i & 1u) ? (GPIO_BSRR_BR_5) : (GPIO_BSRR_BS_5);
        ++mbox.toggles;
        delay_ms(BLINK_MS);
    }

    GPIOA->BSRR = GPIO_BSRR_BS_5;        /* LD2 encendido al terminar */
    mbox.button = (GPIOC->IDR & GPIO_IDR_ID13) ? 1u : 0u;
    mbox.done   = 1u;

    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
