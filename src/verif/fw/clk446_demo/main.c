/* ===========================================================================
 * main.c — El reloj de una NUCLEO-F446RE a 180 MHz (hito H4 de la fase 3)
 *
 * Esto es, paso a paso, lo que hace el `SystemClock_Config()` que STM32CubeIDE
 * genera para una Nucleo-F446RE cuando se le pide el máximo: HSI de 16 MHz,
 * PLL a 180 MHz y **over-drive**. Está escrito con los registros a la vista en
 * vez de con llamadas al HAL —el HAL no está en este repositorio— pero la
 * secuencia es la misma y, sobre todo, **las esperas son las mismas**:
 *
 *   1. PWR encendido y escala de tensión 1 (VOS = 11);
 *   2. ODEN = 1 y ESPERAR a ODRDY;
 *   3. ODSWEN = 1 y ESPERAR a ODSWRDY;
 *   4. Flash a 5 estados de espera;
 *   5. PLL: M = 16, N = 360, P = 2 -> 180 MHz; arrancar y esperar PLLRDY;
 *   6. prescalers -AHB /1, APB1 /4, APB2 /2- y conmutar SYSCLK al PLL.
 *
 * LAS DOS ESPERAS DEL MEDIO SON EL HITO. Si el modelo no levanta ODRDY, este
 * firmware se queda en el paso 2 **para siempre**, igual que se quedaría en la
 * placa si el regulador no arrancara. Que eso ocurra —y que el banco lo
 * compruebe— vale tanto como que funcione el camino bueno: un modelo que
 * llegara a 180 MHz SIN pasar por aquí estaría mintiendo.
 *
 * El firmware deja en el buzón lo que ha conseguido, y además mide de verdad:
 * cuenta milisegundos con el SysTick y los compara con el número de ciclos que
 * deberían haber pasado.
 * ===========================================================================*/
#include "stm32f446xx.h"

#define SYSCLK_HZ     180000000u
#define PASOS_MAX     2000000u      /* tope de vueltas de cada espera */

/* Buzón compartido con el banco (véase verif/fw/stm32f446.ld) */
volatile struct {
    volatile uint32_t done;      /* 1 = llegó al final                       */
    volatile uint32_t sysclk;    /* SYSCLK que el propio firmware calcula    */
    volatile uint32_t etapa;     /* hasta dónde llegó (por si se cuelga)     */
    volatile uint32_t od;        /* PWR_CSR: ODRDY y ODSWRDY, tal cual       */
    volatile uint32_t ticks;     /* tics de SysTick                          */
    volatile uint32_t pclk1;     /* PCLK1 calculado                          */
    volatile uint32_t pclk2;     /* PCLK2 calculado                          */
} mbox __attribute__((section(".mailbox")));

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; ++mbox.ticks; }

/* Espera con tope: devuelve 0 si la condición nunca se cumple. Un firmware de
 * verdad se quedaría colgado; aquí hay tope para que el banco pueda DISTINGUIR
 * «se colgó esperando ODRDY» de «se colgó en otro sitio», que es justo lo que
 * la prueba del camino malo necesita saber. */
#define ESPERA(cond)  ({                                                    \
    uint32_t _n = 0;                                                        \
    while (!(cond) && ++_n < PASOS_MAX) { }                                 \
    (_n < PASOS_MAX);                                                       \
})

int main(void)
{
    mbox.done = 0; mbox.sysclk = 0; mbox.etapa = 0; mbox.od = 0;
    mbox.ticks = 0; mbox.pclk1 = 0; mbox.pclk2 = 0;

    /* 1. El PWR necesita su reloj antes de nada, y la escala de tensión 1 es
     *    la única desde la que se puede pedir over-drive.                   */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;                 /* VOS = 11: escala 1            */
    mbox.etapa = 1;

    /* 2. Over-drive: encender y ESPERAR. Aquí es donde se cuelga un modelo
     *    que no lo tenga.                                                   */
    PWR->CR |= PWR_CR_ODEN;
    if (!ESPERA(PWR->CSR & PWR_CSR_ODRDY)) { mbox.etapa = 0xE2; for(;;){} }
    mbox.etapa = 2;

    /* 3. Conmutar el over-drive y ESPERAR otra vez.                         */
    PWR->CR |= PWR_CR_ODSWEN;
    if (!ESPERA(PWR->CSR & PWR_CSR_ODSWRDY)) { mbox.etapa = 0xE3; for(;;){} }
    mbox.etapa = 3;
    mbox.od = (PWR->CSR >> 16) & 3u;       /* ODRDY | ODSWRDY               */

    /* 4. Flash: 5 estados de espera para 180 MHz a 3,3 V [RM0390, tabla 5]. */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;
    mbox.etapa = 4;

    /* 5. El PLL desde el HSI: VCO_IN = 16/16 = 1 MHz, VCO_OUT = 360 MHz,
     *    P = 2 -> 180 MHz. Q = 8 deja los 45 MHz que el USB no puede usar,
     *    y es lo que pone CubeMX cuando se prioriza el SYSCLK.              */
    RCC->CR |= RCC_CR_HSION;
    if (!ESPERA(RCC->CR & RCC_CR_HSIRDY)) { mbox.etapa = 0xE5; for(;;){} }
    RCC->PLLCFGR = (16u  << RCC_PLLCFGR_PLLM_Pos) |
                   (360u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u   << RCC_PLLCFGR_PLLP_Pos) |   /* 00 = /2 */
                   (8u   << RCC_PLLCFGR_PLLQ_Pos) |
                   (2u   << RCC_PLLCFGR_PLLR_Pos) |   /* R: solo en el F446 */
                   RCC_PLLCFGR_PLLSRC_HSI;
    RCC->CR |= RCC_CR_PLLON;
    if (!ESPERA(RCC->CR & RCC_CR_PLLRDY)) { mbox.etapa = 0xE6; for(;;){} }
    mbox.etapa = 5;

    /* 6. Prescalers y conmutación. APB1 /4 = 45 MHz y APB2 /2 = 90 MHz, que
     *    son los topes CON over-drive: sin él, el modelo avisa.             */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    if (!ESPERA((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL)) {
        mbox.etapa = 0xE7; for(;;){}
    }
    mbox.etapa = 6;

    SystemCoreClockUpdate();
    mbox.sysclk = SystemCoreClock;
    mbox.pclk1  = SystemCoreClock / 4u;
    mbox.pclk2  = SystemCoreClock / 2u;

    /* Y una medida de verdad, no un cálculo: mil tics de SysTick a 1 kHz son
     * un segundo, y eso solo sale bien si el modelo corre a la frecuencia que
     * el firmware cree.                                                     */
    SysTick_Config(SystemCoreClock / 1000u);
    while (g_ms < 20u) { __WFI(); }

    mbox.done = 1;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C. */
void __libc_init_array(void) { }
