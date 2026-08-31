/* ===========================================================================
 * main.c — Firmware de demostración de USART y UART, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo. Un mini driver, idéntico para las dos
 * variantes del periférico, se aplica a:
 *
 *   1. USART2 -> USART3 (PA2 -> PB11 y PB10 -> PA3, AF7) a 115200 8N1, con la
 *      recepción atendida por la interrupción RXNE del NVIC;
 *   2. UART4 -> UART5 (PA0 -> PD2 y PC12 -> PA1, AF8), por sondeo.
 *
 * Que el mismo código sirva para las dos es justamente lo que comprueba la
 * parametrización del modelo: la UART responde igual en todo lo que comparte
 * con la USART.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define BAUD      115200u
#define MSG       "STM32 USART OK"
#define MSG_LEN   14u

volatile struct {
    volatile uint32_t done;        /* 1 cuando el firmware ha terminado      */
    volatile uint32_t usart_ok;    /* 1 si USART2 -> USART3 llegó intacto    */
    volatile uint32_t uart_ok;     /* 1 si UART4  -> UART5  llegó intacto    */
    volatile uint32_t rx_irq;      /* interrupciones de recepción atendidas  */
    volatile uint32_t brr;         /* BRR calculado por el firmware          */
    volatile uint32_t pclk1;       /* PCLK1 que ha usado                     */
} mbox __attribute__((section(".mailbox")));

static volatile char     rx_buf[32];
static volatile uint32_t rx_len;
static volatile uint32_t rx_irq;

void USART3_IRQHandler(void)
{
    if (USART3->SR & USART_SR_RXNE) {
        const char c = (char)(USART3->DR & 0xFFu);
        if (rx_len < sizeof rx_buf) rx_buf[rx_len++] = c;
        ++rx_irq;
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

/* Un pin en función alternativa (la misma rutina para AF7 y AF8) */
static void pin_af(GPIO_TypeDef *g, uint32_t pin, uint32_t af)
{
    g->MODER   = (g->MODER   & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->OTYPER &= ~(1u << pin);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * pin))) | (3u << (2 * pin));
    g->PUPDR   = (g->PUPDR   & ~(3u << (2 * pin))) | (1u << (2 * pin));
    if (pin < 8u) g->AFR[0] = (g->AFR[0] & ~(0xFu << (4 * pin))) | (af << (4 * pin));
    else          g->AFR[1] = (g->AFR[1] & ~(0xFu << (4 * (pin - 8u)))) |
                              (af << (4 * (pin - 8u)));
}

/* BRR con divisor fraccionario, tal como lo calcula el driver de ST */
static uint32_t brr_for(uint32_t pclk, uint32_t baud)
{
    const uint32_t div100 = (25u * pclk) / (4u * baud);      /* USARTDIV x 100 */
    const uint32_t mant   = div100 / 100u;
    const uint32_t frac   = (((div100 - mant * 100u) * 16u) + 50u) / 100u;
    return (mant << 4) | (frac & 0xFu);
}

/* Mini driver común a USART y UART: solo usa lo que ambas variantes tienen */
static void serial_init(USART_TypeDef *u, uint32_t pclk, uint32_t baud, uint32_t cr1)
{
    u->CR1 = 0u;
    u->BRR = brr_for(pclk, baud);
    u->CR2 = 0u;
    u->CR3 = 0u;
    u->CR1 = cr1 | USART_CR1_UE;
}
static void serial_putc(USART_TypeDef *u, char c)
{
    while ((u->SR & USART_SR_TXE) == 0u) { }
    u->DR = (uint8_t)c;
}
static int serial_getc(USART_TypeDef *u, uint32_t timeout)
{
    while (timeout--) if (u->SR & USART_SR_RXNE) return (int)(u->DR & 0xFFu);
    return -1;
}

int main(void)
{
    mbox.done = 0u; mbox.usart_ok = 0u; mbox.uart_ok = 0u;
    mbox.rx_irq = 0u; mbox.brr = 0u; mbox.pclk1 = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN | RCC_APB1ENR_USART3EN |
                    RCC_APB1ENR_UART4EN  | RCC_APB1ENR_UART5EN;

    const uint32_t pclk1 = SystemCoreClock / 4u;             /* APB1 = HCLK/4 */
    mbox.pclk1 = pclk1;
    mbox.brr   = brr_for(pclk1, BAUD);

    /* USART2 (AF7): PA2 = TX, PA3 = RX ; USART3 (AF7): PB10 = TX, PB11 = RX */
    pin_af(GPIOA, 2u, 7u);  pin_af(GPIOA, 3u, 7u);
    pin_af(GPIOB, 10u, 7u); pin_af(GPIOB, 11u, 7u);
    /* UART4 (AF8): PA0 = TX, PA1 = RX ; UART5 (AF8): PC12 = TX, PD2 = RX */
    pin_af(GPIOA, 0u, 8u);  pin_af(GPIOA, 1u, 8u);
    pin_af(GPIOC, 12u, 8u); pin_af(GPIOD, 2u, 8u);

    /* --- 1. USART2 -> USART3 con recepción por interrupción --------------- */
    serial_init(USART2, pclk1, BAUD, USART_CR1_TE);
    serial_init(USART3, pclk1, BAUD, USART_CR1_RE | USART_CR1_RXNEIE);
    NVIC_EnableIRQ(USART3_IRQn);

    for (uint32_t i = 0; i < MSG_LEN; ++i) serial_putc(USART2, MSG[i]);
    for (uint32_t t = 0; t < 2000000u && rx_len < MSG_LEN; ++t) { __NOP(); }

    uint32_t ok = (rx_len == MSG_LEN);
    for (uint32_t i = 0; i < MSG_LEN; ++i) if (rx_buf[i] != MSG[i]) ok = 0u;
    mbox.usart_ok = ok;
    mbox.rx_irq   = rx_irq;

    /* --- 2. El MISMO driver sobre la variante reducida -------------------- */
    serial_init(UART4, pclk1, BAUD, USART_CR1_TE);
    serial_init(UART5, pclk1, BAUD, USART_CR1_RE);
    uint32_t ok2 = 1u;
    for (uint32_t i = 0; i < MSG_LEN; ++i) {
        serial_putc(UART4, MSG[i]);
        if (serial_getc(UART5, 200000u) != (int)(uint8_t)MSG[i]) ok2 = 0u;
    }
    mbox.uart_ok = ok2;

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
