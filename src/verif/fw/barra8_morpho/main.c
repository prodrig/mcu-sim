/* ===========================================================================
 * main.c — Luz que corre en una barra de ánodo común, por los morpho de una
 *          NUCLEO-F446RE
 *
 * El firmware de placas/nucleo_f446re_barra8ac_azul.xml: una barra de ocho
 * LEDs de ÁNODO COMÚN (placas/barra8_anodo_comun_azul.xml) con su común a VDD
 * -CN7.5- y cada LED a un pin del chip por los conectores morpho:
 *
 *     LED de la barra   P1.D1  P1.D2  P1.D3  P1.D4  P1.D5  P1.D6  P1.D7  P1.D8
 *     pin del chip      PC0    PC1    PC2    PC3    PB12   PB13   PB14   PB15
 *     morpho            CN7.38 CN7.36 CN7.35 CN7.37 CN10.16 CN10.30 CN10.28 CN10.26
 *
 * Con el ánodo común a VDD, un LED luce con SU pin a CERO. La luz corre de D1
 * a D8, un LED cada 50 ms -400 ms la vuelta-, para siempre. Los ocho pines de
 * un puerto se escriben de una vez con BSRR: los de C son los bits 0 a 3, y
 * los de B los 12 a 15. El reloj es el HSI de 16 MHz, el del arranque.
 * ===========================================================================*/
#include "stm32f446xx.h"

#define PASO_MS 50u

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; }

static void espera_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) { __WFI(); }
}

#define MASCARA_C 0x000Fu                   /* PC0..PC3   -> D1..D4 */
#define MASCARA_B 0xF000u                   /* PB12..PB15 -> D5..D8 */

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;

    /* Todos a 1 -apagados- ANTES de hacerlos salidas: que no parpadeen */
    GPIOC->BSRR = MASCARA_C;
    GPIOB->BSRR = MASCARA_B;
    for (unsigned i = 0; i < 16u; ++i) {
        if (MASCARA_C & (1u << i))
            GPIOC->MODER = (GPIOC->MODER & ~(3u << (2u * i))) | (1u << (2u * i));
        if (MASCARA_B & (1u << i))
            GPIOB->MODER = (GPIOB->MODER & ~(3u << (2u * i))) | (1u << (2u * i));
    }

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    for (;;) {
        for (unsigned k = 0; k < 8u; ++k) {
            /* El LED k, a cero; los otros siete, a uno */
            const uint32_t c = k < 4u ? (1u << k) : 0u;
            const uint32_t b = k < 4u ? 0u : (1u << (12u + k - 4u));
            GPIOC->BSRR = (MASCARA_C & ~c) | (c << 16u);
            GPIOB->BSRR = (MASCARA_B & ~b) | (b << 16u);
            espera_ms(PASO_MS);
        }
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
