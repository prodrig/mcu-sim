/* ===========================================================================
 * main.c — Luz que corre en una barra de 8 LEDs, desde una NUCLEO-F446RE
 *
 * El firmware de placas/barra8_en_nucleo.xml: una barra de ocho LEDs con su
 * común (placas/barra8_*.xml) cableada a los pines Arduino de la Nucleo, como
 * en el ejemplo de los módulos DM41A08, que usa D9..D2 para D1..D8:
 *
 *     LED de la barra   D1   D2   D3   D4    D5   D6   D7   D8   común
 *     pin Arduino       D9   D8   D7   D6    D5   D4   D3   D2   D10
 *     pin del chip      PC7  PA9  PA8  PB10  PB4  PB5  PB3  PA10 PB6
 *
 * EL COMÚN VA A UN PIN, no a VDD ni a masa, y por eso el mismo programa vale
 * para las dos barras. Hace dos vueltas, una detrás de otra y para siempre:
 *
 *   1. común ALTO, y un solo pin BAJO cada 50 ms, de D1 a D8: es la luz que
 *      corre en una barra de ÁNODO común; una de cátodo común no luce, porque
 *      todos sus LEDs quedan sin tensión o al revés;
 *   2. común BAJO, y un solo pin ALTO cada 50 ms, de D1 a D8: la luz que corre
 *      en una barra de CÁTODO común; ahora es la de ánodo común la que no
 *      luce.
 *
 * Una vuelta son 400 ms, y las dos, 800. El reloj es el HSI de 16 MHz, el
 * del arranque: no hace falta más para encender LEDs.
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

/* Los ocho LEDs, de D1 a D8, y el común */
typedef struct { GPIO_TypeDef* puerto; uint32_t n; } Pin;
static const Pin LEDS[8] = {
    {GPIOC, 7}, {GPIOA, 9}, {GPIOA, 8}, {GPIOB, 10},
    {GPIOB, 4}, {GPIOB, 5}, {GPIOB, 3}, {GPIOA, 10},
};
static const Pin COMUN = {GPIOB, 6};

static void salida(Pin p)
{
    p.puerto->MODER   = (p.puerto->MODER & ~(3u << (2u * p.n))) | (1u << (2u * p.n));
    p.puerto->OTYPER &= ~(1u << p.n);
    p.puerto->PUPDR  &= ~(3u << (2u * p.n));
}

static void pon(Pin p, int alto)
{
    p.puerto->BSRR = alto ? (1u << p.n) : (1u << (p.n + 16u));
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    for (unsigned i = 0; i < 8u; ++i) salida(LEDS[i]);
    salida(COMUN);

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    for (;;) {
        /* vuelta = 0: común alto, el LED encendido es el pin BAJO (ánodo común);
         * vuelta = 1: común bajo, el LED encendido es el pin ALTO (cátodo común) */
        for (int vuelta = 0; vuelta < 2; ++vuelta) {
            const int reposo = vuelta == 0;          /* el nivel de los apagados */
            pon(COMUN, vuelta == 0);
            for (unsigned k = 0; k < 8u; ++k) {
                for (unsigned i = 0; i < 8u; ++i) pon(LEDS[i], i == k ? !reposo : reposo);
                espera_ms(PASO_MS);
            }
        }
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
