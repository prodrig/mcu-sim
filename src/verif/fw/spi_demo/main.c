/* ===========================================================================
 * main.c — Firmware de demostración de SPI e I2S, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo. Ejercita las dos variantes del bloque:
 *
 *   1. SPI1 (APB2) como MAESTRO y SPI2 (APB1) como ESCLAVO, unidos por cuatro
 *      pistas de la placa: PA5->PB13 (SCK), PA7->PB15 (MOSI), PB14->PA6 (MISO)
 *      y PA4->PB12 (NSS). El mismo mini driver sirve para los dos extremos,
 *      que es lo que comprueba que el modelo es uno solo.
 *
 *   2. El modo I2S del SPI2, que el SPI1 NO tiene: se programa I2SCFGR en los
 *      dos y se comprueba que en el SPI1 el registro está reservado y lee cero,
 *      exactamente como en el silicio [IR, §12.7].
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define N_BYTES   8u

volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado         */
    volatile uint32_t spi_ok;    /* 1 si los N_BYTES viajaron intactos        */
    volatile uint32_t nbytes;    /* bytes intercambiados                      */
    volatile uint32_t cr1;       /* SPI1_CR1 con el que trabajó               */
    volatile uint32_t i2s_ok;    /* 1 si solo el SPI2 acepta el modo I2S      */
    volatile uint32_t pclk2;     /* PCLK2 que ha usado                        */
} mbox __attribute__((section(".mailbox")));

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

/* Mini driver comun a los dos extremos: solo usa lo que ambos comparten */
static void spi_init(SPI_TypeDef *s, uint32_t cr1)
{
    s->CR1 = 0u;
    s->CR2 = 0u;
    s->CR1 = cr1 | SPI_CR1_SPE;
}
static int spi_xfer(SPI_TypeDef *m, SPI_TypeDef *sl, uint8_t tx_m, uint8_t tx_s,
                    uint8_t *rx_s)
{
    uint32_t guard;
    sl->DR = tx_s;                        /* el esclavo carga su dato primero */
    m->DR  = tx_m;                        /* y el maestro pone el reloj       */
    for (guard = 0; guard < 200000u; ++guard)
        if ((m->SR & SPI_SR_RXNE) && (sl->SR & SPI_SR_RXNE)) break;
    if (guard >= 200000u) return -1;
    *rx_s = (uint8_t)(sl->DR & 0xFFu);
    return (int)(m->DR & 0xFFu);
}

int main(void)
{
    mbox.done = 0u; mbox.spi_ok = 0u; mbox.nbytes = 0u;
    mbox.cr1 = 0u; mbox.i2s_ok = 0u; mbox.pclk2 = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    const uint32_t pclk2 = SystemCoreClock / 2u;          /* APB2 = HCLK/2 */
    mbox.pclk2 = pclk2;

    /* SPI1 (AF5): PA4 NSS, PA5 SCK, PA6 MISO, PA7 MOSI */
    pin_af(GPIOA, 4u, 5u); pin_af(GPIOA, 5u, 5u);
    pin_af(GPIOA, 6u, 5u); pin_af(GPIOA, 7u, 5u);
    /* SPI2 (AF5): PB12 NSS, PB13 SCK, PB14 MISO, PB15 MOSI */
    pin_af(GPIOB, 12u, 5u); pin_af(GPIOB, 13u, 5u);
    pin_af(GPIOB, 14u, 5u); pin_af(GPIOB, 15u, 5u);

    /* Maestro: NSS por software en alto; BR = 5 -> PCLK2/64 = 1,3 MHz */
    const uint32_t cr1_m = SPI_CR1_MSTR | (5u << SPI_CR1_BR_Pos) |
                           SPI_CR1_SSM | SPI_CR1_SSI;
    spi_init(SPI1, cr1_m);
    /* Esclavo: NSS por software en bajo, es decir, seleccionado */
    spi_init(SPI2, SPI_CR1_SSM);
    mbox.cr1 = SPI1->CR1;

    static const uint8_t msg[N_BYTES] = {'S','P','I','-','C','M','S','I'};
    uint32_t ok = 1u, n = 0u;
    for (uint32_t i = 0; i < N_BYTES; ++i) {
        uint8_t got_s = 0u;
        const int got_m = spi_xfer(SPI1, SPI2, msg[i], (uint8_t)(msg[i] ^ 0xFFu),
                                   &got_s);
        if (got_m < 0) { ok = 0u; break; }
        if (got_s != msg[i]) ok = 0u;                       /* maestro -> esclavo */
        if ((uint8_t)got_m != (uint8_t)(msg[i] ^ 0xFFu)) ok = 0u;  /* y al reves */
        ++n;
    }
    mbox.spi_ok = ok;
    mbox.nbytes = n;

    /* --- El modo I2S existe en el SPI2 y no en el SPI1 -------------------- */
    SPI1->CR1 = 0u; SPI2->CR1 = 0u;
    const uint32_t cfg = SPI_I2SCFGR_I2SMOD | SPI_I2SCFGR_I2SCFG_1;  /* maestro TX */
    SPI1->I2SCFGR = cfg;
    SPI2->I2SCFGR = cfg;
    mbox.i2s_ok = ((SPI1->I2SCFGR == 0u) && (SPI2->I2SCFGR == cfg)) ? 1u : 0u;
    SPI2->I2SCFGR = 0u;

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
