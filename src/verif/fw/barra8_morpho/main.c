/* ===========================================================================
 * main.c — Luz que corre en una barra de ánodo común, por los morpho de una
 *          NUCLEO-F446RE, y dos puertos serie: el del ST-LINK y un FT232RL
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
 *
 * Y DOS PUERTOS SERIE, los dos a 115200 8N1:
 *
 *   USART2 (PA2 TX, PA3 RX)    el VCP del ST-LINK: sale por el mismo USB que
 *                              la depuración. Saluda y devuelve lo que llega.
 *   USART1 (PA9 TX, PA10 RX)   el adaptador FT232RL, por CN10.21 y CN10.33.
 *                              Saluda, dice qué LED se enciende en cada paso
 *                              ("D1".."D8") y devuelve lo que llega.
 *
 * Lo que llega se recoge por interrupción en una cola por puerto, y el bucle
 * principal lo devuelve entre paso y paso: un eco no espera a que acabe la
 * vuelta de la barra.
 * ===========================================================================*/
#include "stm32f446xx.h"

#define PASO_MS 50u
#define BRR_115200 139u                      /* 16 MHz / 115200, redondeado */

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; }

/* --- Los dos puertos serie ------------------------------------------------ */
typedef struct {
    USART_TypeDef* u;
    volatile uint8_t cola[64];
    volatile uint32_t esc, lec;
} Puerto;

static Puerto vcp  = { USART2, {0}, 0, 0 };
static Puerto ftdi = { USART1, {0}, 0, 0 };

static void recoge(Puerto* p)
{
    const uint32_t sr = p->u->SR;            /* primero el SR, luego el DR */
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        const uint8_t c = (uint8_t)p->u->DR;
        if (sr & USART_SR_RXNE) {
            p->cola[p->esc % 64u] = c;
            ++p->esc;
        }
    }
}

void USART1_IRQHandler(void) { recoge(&ftdi); }
void USART2_IRQHandler(void) { recoge(&vcp); }

static void manda(Puerto* p, char c)
{
    while ((p->u->SR & USART_SR_TXE) == 0u) { }
    p->u->DR = (uint8_t)c;
}

static void escribe(Puerto* p, const char* s)
{
    while (*s) manda(p, *s++);
}

static void devuelve(Puerto* p)
{
    while (p->lec != p->esc) {
        manda(p, (char)p->cola[p->lec % 64u]);
        ++p->lec;
    }
}

static void espera_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) {
        __WFI();
        devuelve(&vcp);
        devuelve(&ftdi);
    }
}

/* Un pin de GPIOA en su función alternativa 7 (la de las USART 1 a 3) */
static void af7(uint32_t n)
{
    GPIOA->MODER = (GPIOA->MODER & ~(3u << (2u * n))) | (2u << (2u * n));
    if (n < 8u) GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(15u << (4u * n))) | (7u << (4u * n));
    else        GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(15u << (4u * (n - 8u)))) | (7u << (4u * (n - 8u)));
}

static void usart(USART_TypeDef* u, IRQn_Type irq)
{
    u->BRR = BRR_115200;
    u->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(irq);
}

/* --- La barra ------------------------------------------------------------- */
#define MASCARA_C 0x000Fu                   /* PC0..PC3   -> D1..D4 */
#define MASCARA_B 0xF000u                   /* PB12..PB15 -> D5..D8 */

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* Todos a 1 -apagados- ANTES de hacerlos salidas: que no parpadeen */
    GPIOC->BSRR = MASCARA_C;
    GPIOB->BSRR = MASCARA_B;
    for (unsigned i = 0; i < 16u; ++i) {
        if (MASCARA_C & (1u << i))
            GPIOC->MODER = (GPIOC->MODER & ~(3u << (2u * i))) | (1u << (2u * i));
        if (MASCARA_B & (1u << i))
            GPIOB->MODER = (GPIOB->MODER & ~(3u << (2u * i))) | (1u << (2u * i));
    }

    af7(2); af7(3);                          /* USART2: PA2 TX, PA3 RX */
    af7(9); af7(10);                         /* USART1: PA9 TX, PA10 RX */
    usart(USART2, USART2_IRQn);
    usart(USART1, USART1_IRQn);

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    escribe(&vcp,  "barra8_morpho: VCP del ST-LINK (USART2)\r\n");
    escribe(&ftdi, "barra8_morpho: FT232RL (USART1)\r\n");

    for (;;) {
        for (unsigned k = 0; k < 8u; ++k) {
            /* El LED k, a cero; los otros siete, a uno */
            const uint32_t c = k < 4u ? (1u << k) : 0u;
            const uint32_t b = k < 4u ? 0u : (1u << (12u + k - 4u));
            GPIOC->BSRR = (MASCARA_C & ~c) | (c << 16u);
            GPIOB->BSRR = (MASCARA_B & ~b) | (b << 16u);
            const char linea[] = { 'D', (char)('1' + k), '\r', '\n', 0 };
            escribe(&ftdi, linea);
            espera_ms(PASO_MS);
        }
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
