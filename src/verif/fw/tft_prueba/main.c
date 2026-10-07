/* ===========================================================================
 * main.c — La prueba del ST7735S de la pantalla TFT, por el SPI1 de una
 *          NUCLEO-F446RE
 *
 * El firmware de `make gui-marcha` M7. El cableado es el de
 * placas/nucleo_f446re_tft.xml (SCK PA5, SDA PA7, CS PB6, A/D PC7, RESET PA9)
 * y el SPI1 el de verif/fw/tft_demo, a 8 MHz. No dibuja nada bonito: pone a
 * prueba, una cosa en cada FASE, lo que el chip tiene que hacer, y la prueba
 * mira la imagen que llega a la ventana en cada una.
 *
 * Las fases empiezan en instantes FIJOS, cada 100 ms desde los 400, para que
 * la prueba sepa qué imagen mirar sin más señal:
 *
 *   400  MADCTL, en la memoria vista de frente (todo con COLMOD 16 bits): un
 *        bloque de 10x10 en la esquina (0, 0) lógica con MADCTL 00h -rojo-,
 *        40h (MX) -verde-, 80h (MY) -azul- y, con 20h (MV), uno amarillo de
 *        20 columnas lógicas en la 40..59, que son las FILAS 40..59;
 *   500  COLMOD 18 bits, un bloque magenta en (50..59, 50..59); y 12 bits, uno
 *        cian en (70..79, 50..59);
 *   600  INVON: todo invertido;
 *   700  INVOFF e IDMON, y un bloque naranja (200, 100, 30) en (100..109,
 *        100..109): en ocho colores, rojo;
 *   800  IDMOFF y el modo parcial, filas 20 a 39 (PTLAR, PTLON): fuera,
 *        blanco;
 *   900  NORON y el desplazamiento: SCRLAR (0, 160, 0) y VSCSAD 10. La línea 0
 *        del panel enseña la fila 10 de la memoria;
 *   1000 NORON otra vez -sin desplazamiento- y DISPOFF: blanca;
 *   1100 DISPON y SLPIN: blanca;
 *   1200 SLPOUT: otra vez la memoria;
 *   1300 las LECTURAS, moviendo los pines a mano: RDDPM, RDDMADCTL,
 *        RDDCOLMOD, RDDIM, RDID1-3, RDDST y un píxel con RAMRD. Las dice por
 *        el VCP;
 *   1400 un RESET y, sin esperar los 5 ms, SLPOUT y DISPON: el chip no los
 *        oye -los pierde, y lo dice-, y sigue dormido: blanca.
 * ===========================================================================*/
#include "stm32f446xx.h"

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; }

static void espera_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) __WFI();
}
static void hasta_ms(uint32_t t) { while (g_ms < t) __WFI(); }

#define PIN_CS   6u   /* PB6 */
#define PIN_AD   7u   /* PC7 */
#define PIN_RST  9u   /* PA9 */

static void cs(int v)  { GPIOB->BSRR = v ? (1u << PIN_CS)  : (1u << (PIN_CS + 16u)); }
static void ad(int v)  { GPIOC->BSRR = v ? (1u << PIN_AD)  : (1u << (PIN_AD + 16u)); }
static void rst(int v) { GPIOA->BSRR = v ? (1u << PIN_RST) : (1u << (PIN_RST + 16u)); }

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

static void spi_manda(uint8_t b)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0u) { }
    *(volatile uint8_t*)&SPI1->DR = b;
}
static void spi_vacia(void)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0u) { }
    while (SPI1->SR & SPI_SR_BSY) { }
}
static void orden(uint8_t c)
{
    spi_vacia();
    ad(0);
    cs(0);
    spi_manda(c);
    spi_vacia();
    ad(1);
}
static void dato(uint8_t d) { spi_manda(d); }
static void suelta(void) { spi_vacia(); cs(1); }
static void orden1(uint8_t c, uint8_t d) { orden(c); dato(d); suelta(); }

static void vcp(const char* s)
{
    while (*s) {
        while ((USART2->SR & USART_SR_TXE) == 0u) { }
        USART2->DR = (uint8_t)*s++;
    }
}

/* --- Las lecturas, moviendo los pines a mano (véase tft_demo) ------------- */
static void sck_gpio(int v) { GPIOA->BSRR = v ? (1u << 5) : (1u << 21); }
static void sda_gpio(int v) { GPIOA->BSRR = v ? (1u << 7) : (1u << 23); }
static void retardo(void) { for (volatile int i = 0; i < 4; ++i) { } }

static uint32_t lee(uint8_t c, int bits, int vacio)
{
    spi_vacia();
    SPI1->CR1 &= ~SPI_CR1_SPE;
    sck_gpio(0);
    modo(GPIOA, 5u, 1u);
    modo(GPIOA, 7u, 1u);
    ad(0);
    cs(0);
    for (int i = 7; i >= 0; --i) {
        sda_gpio((c >> i) & 1);
        retardo();
        sck_gpio(1);
        retardo();
        /* SDA, a entrada ANTES de la bajada del último bit: en esa bajada
         * el chip ya pone el suyo [hoja, 9.4.2]. Después, los dos tirarían
         * del mismo hilo */
        if (i == 0) modo(GPIOA, 7u, 0u);
        sck_gpio(0);
    }
    if (vacio) {
        retardo();
        sck_gpio(1);
        retardo();
        sck_gpio(0);
    }
    uint32_t v = 0;
    for (int i = 0; i < bits; ++i) {
        retardo();
        sck_gpio(1);
        v = (v << 1) | ((GPIOA->IDR >> 7) & 1u);
        retardo();
        sck_gpio(0);
    }
    cs(1);
    ad(1);
    af(GPIOA, 5u, 5u);
    af(GPIOA, 7u, 5u);
    SPI1->CR1 |= SPI_CR1_SPE;
    return v;
}

/* --- Dibujar ---------------------------------------------------------------- */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

static void zona(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    orden(0x2A); dato(0); dato((uint8_t)x0); dato(0); dato((uint8_t)x1);
    orden(0x2B); dato(0); dato((uint8_t)y0); dato(0); dato((uint8_t)y1);
}
static void rect16(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    zona(x, y, x + w - 1u, y + h - 1u);
    orden(0x2C);
    for (uint32_t n = w * h; n > 0u; --n) { dato((uint8_t)(c >> 8)); dato((uint8_t)c); }
    suelta();
}

static void hex(char* d, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; --i, v >>= 4) d[i] = "0123456789ABCDEF"[v & 15u];
    d[n] = 0;
}
static void dice(const char* que, uint32_t v, int n)
{
    char h[12];
    hex(h, v, n);
    vcp(que);
    vcp(h);
    vcp(" ");
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    cs(1); rst(1); ad(1);
    modo(GPIOB, PIN_CS, 1u);
    modo(GPIOC, PIN_AD, 1u);
    modo(GPIOA, PIN_RST, 1u);
    af(GPIOA, 5u, 5u);
    af(GPIOA, 7u, 5u);
    af(GPIOA, 2u, 7u);
    USART2->BRR = 139u;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_SPE;

    espera_ms(5);
    rst(0); espera_ms(1); rst(1);
    espera_ms(125);
    orden(0x11); suelta();
    espera_ms(125);
    orden1(0x3A, 0x05);
    orden1(0x36, 0x00);
    orden(0x29); suelta();
    rect16(0, 0, 128, 160, RGB(0, 0, 0));
    vcp("tft_prueba: lista\r\n");

    /* 400: MADCTL */
    hasta_ms(400);
    rect16(0, 0, 10, 10, RGB(255, 0, 0));
    orden1(0x36, 0x40);
    rect16(0, 0, 10, 10, RGB(0, 255, 0));
    orden1(0x36, 0x80);
    rect16(0, 0, 10, 10, RGB(0, 0, 255));
    orden1(0x36, 0x20);
    rect16(40, 0, 20, 10, RGB(255, 255, 0));
    orden1(0x36, 0x00);

    /* 500: 18 y 12 bits */
    hasta_ms(500);
    orden1(0x3A, 0x06);
    zona(50, 50, 59, 59);
    orden(0x2C);
    for (int n = 0; n < 100; ++n) { dato(0xFC); dato(0x00); dato(0xFC); }
    suelta();
    orden1(0x3A, 0x03);
    zona(70, 50, 79, 59);
    orden(0x2C);
    for (int n = 0; n < 50; ++n) { dato(0x0F); dato(0xF0); dato(0xFF); }  /* dos cian */
    suelta();
    orden1(0x3A, 0x05);

    /* 600: inversión */
    hasta_ms(600);
    orden(0x21); suelta();

    /* 700: ocho colores */
    hasta_ms(700);
    orden(0x20); suelta();
    orden(0x39); suelta();
    rect16(100, 100, 10, 10, RGB(200, 100, 30));

    /* 800: modo parcial, filas 20 a 39 */
    hasta_ms(800);
    orden(0x38); suelta();
    orden(0x30); dato(0); dato(20); dato(0); dato(39); suelta();
    orden(0x12); suelta();

    /* 900: desplazamiento */
    hasta_ms(900);
    orden(0x13); suelta();
    orden(0x33); dato(0); dato(0); dato(0); dato(160); dato(0); dato(0); suelta();
    orden(0x37); dato(0); dato(10); suelta();

    /* 1000: sin desplazamiento, y DISPOFF */
    hasta_ms(1000);
    orden(0x13); suelta();
    orden(0x28); suelta();

    /* 1100: DISPON y SLPIN */
    hasta_ms(1100);
    orden(0x29); suelta();
    orden(0x10); suelta();

    /* 1200: SLPOUT */
    hasta_ms(1200);
    orden(0x11); suelta();

    /* 1340: las lecturas, pasados los 120 ms del SLPOUT */
    hasta_ms(1340);
    vcp("tft_prueba: ");
    dice("0A=", lee(0x0A, 8, 0), 2);
    dice("0B=", lee(0x0B, 8, 0), 2);
    dice("0C=", lee(0x0C, 8, 0), 2);
    dice("0D=", lee(0x0D, 8, 0), 2);
    dice("DA=", lee(0xDA, 8, 0), 2);
    dice("DB=", lee(0xDB, 8, 0), 2);
    dice("DC=", lee(0xDC, 8, 0), 2);
    dice("09=", lee(0x09, 32, 1), 8);
    zona(0, 0, 0, 0);
    suelta();
    dice("2E=", lee(0x2E, 24, 1), 6);
    vcp("\r\n");

    /* 1400: un reset, y órdenes sin esperar los 5 ms */
    hasta_ms(1400);
    rst(0); espera_ms(1); rst(1);
    orden(0x11); suelta();
    orden(0x29); suelta();
    vcp("tft_prueba: fin\r\n");
    for (;;) __WFI();
}

void __libc_init_array(void) { }
