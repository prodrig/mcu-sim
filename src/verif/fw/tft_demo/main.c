/* ===========================================================================
 * main.c — Una pantalla TFT de 1,8" (128x160, ST7735S) por el SPI1 de una
 *          NUCLEO-F446RE
 *
 * El firmware de placas/nucleo_f446re_tft.xml: el módulo cableado a los
 * conectores Arduino de la Nucleo, como se haría con unos cables de colores:
 *
 *     pantalla   P1.SCK  P1.SDA  P1.CS   P1.AD   P1.RESET  P1.VCC  P1.LED  P1.GND
 *     Arduino    D13     D11     D10     D9      D8        +3V3    IOREF   GND
 *     chip       PA5     PA7     PB6     PC7     PA9       VDD     VDD     VSS
 *
 * SCK y SDA son el SPI1 (AF5) en maestro, modo 0, ocho bits, a 8 MHz -el HSI
 * de 16 MHz entre dos-: 125 ns de ciclo, holgado frente a los 66 que admite el
 * ST7735S al escribir. CS, A/D y RESET son pines normales. D13 es también el
 * LED verde de la Nucleo, LD2: parpadea con el reloj del SPI, como en la placa.
 *
 * Lo que hace, en orden:
 *
 *   1. el reset de la pantalla -RESET a cero 1 ms- y los 120 ms que pide;
 *   2. lee su identificación, RDDID (04h), MOVIENDO LOS PINES A MANO: el SPI
 *      del STM32 no sabe leer por el mismo hilo por el que escribe sin
 *      complicarse, y el ST7735S contesta por SDA. Lo dice por el VCP
 *      (USART2, 115200 8N1): "tft_demo: ST7735S ID 7C89F0";
 *   3. la despierta (SLPOUT, y sus 120 ms), le pone 16 bits por píxel
 *      (COLMOD 05h) y la postura apaisada con el conector a la derecha
 *      (MADCTL 60h: MV y MX), y la enciende (DISPON). Desde aquí, y hasta que
 *      se borre, la pantalla enseña lo que tenga su memoria: ruido;
 *   4. dibuja: el fondo azul oscuro, "MCU-SIM" en grande, ocho barras de color
 *      -blanco, amarillo, cian, verde, magenta, rojo, azul y negro, 20 píxeles
 *      cada una- y debajo "ST7735S 128X160" y el ID leído;
 *   5. y para siempre, un cuadrado naranja que va y viene y un contador, cada
 *      40 ms.
 *
 * Las coordenadas son las de la postura apaisada: x de 0 a 159, de izquierda
 * a derecha, e y de 0 a 127, de arriba abajo.
 * ===========================================================================*/
#include "stm32f446xx.h"

#define ANCHO 160u
#define ALTO  128u

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; }

static void espera_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) __WFI();
}

/* --- Los pines que no son del SPI ---------------------------------------- */
#define PIN_CS   6u   /* PB6, D10 */
#define PIN_AD   7u   /* PC7, D9  */
#define PIN_RST  9u   /* PA9, D8  */

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

/* --- El SPI1 -------------------------------------------------------------- */
static void spi_manda(uint8_t b)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0u) { }
    *(volatile uint8_t*)&SPI1->DR = b;
}
/* Que salga el último bit antes de tocar A/D o CS */
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

/* --- El VCP: la USART2, por PA2 ------------------------------------------- */
static void vcp(const char* s)
{
    while (*s) {
        while ((USART2->SR & USART_SR_TXE) == 0u) { }
        USART2->DR = (uint8_t)*s++;
    }
}

/* --- La lectura de RDDID, moviendo los pines a mano ----------------------- *
 * Con el SPI apagado, PA5 (SCK) y PA7 (SDA) son pines normales. Los ocho bits
 * de la orden se ponen en SDA y el chip los toma en cada subida de SCK; luego
 * SDA pasa a entrada, va un ciclo de reloj vacío y llegan 24 bits, que el chip
 * pone en cada bajada y aquí se leen en cada subida [ST7735S, figura 20]. */
static void sck_gpio(int v) { GPIOA->BSRR = v ? (1u << 5) : (1u << 21); }
static void sda_gpio(int v) { GPIOA->BSRR = v ? (1u << 7) : (1u << 23); }
static void retardo(void) { for (volatile int i = 0; i < 4; ++i) { } }

static uint32_t lee_id(void)
{
    spi_vacia();
    SPI1->CR1 &= ~SPI_CR1_SPE;
    sck_gpio(0);
    modo(GPIOA, 5u, 1u);                     /* PA5: salida */
    modo(GPIOA, 7u, 1u);                     /* PA7: salida, para la orden */
    ad(0);
    cs(0);
    for (int i = 7; i >= 0; --i) {
        sda_gpio((0x04 >> i) & 1);
        retardo();
        sck_gpio(1);
        retardo();
        /* SDA, a entrada ANTES de la bajada del último bit: en esa bajada
         * el chip ya pone el suyo [hoja, 9.4.2]. Después, los dos tirarían
         * del mismo hilo */
        if (i == 0) modo(GPIOA, 7u, 0u);
        sck_gpio(0);
    }
    retardo();
    sck_gpio(1);                             /* el ciclo vacío */
    retardo();
    sck_gpio(0);
    uint32_t id = 0;
    for (int i = 0; i < 24; ++i) {
        retardo();
        sck_gpio(1);
        id = (id << 1) | ((GPIOA->IDR >> 7) & 1u);
        retardo();
        sck_gpio(0);
    }
    cs(1);
    ad(1);
    af(GPIOA, 5u, 5u);                       /* y otra vez el SPI */
    af(GPIOA, 7u, 5u);
    SPI1->CR1 |= SPI_CR1_SPE;
    return id;
}

/* --- Dibujar -------------------------------------------------------------- */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

static void ventana(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    orden(0x2A);                             /* CASET */
    dato(0); dato((uint8_t)x0); dato(0); dato((uint8_t)x1);
    orden(0x2B);                             /* RASET */
    dato(0); dato((uint8_t)y0); dato(0); dato((uint8_t)y1);
    orden(0x2C);                             /* RAMWR */
}

static void rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    ventana(x, y, x + w - 1u, y + h - 1u);
    for (uint32_t n = w * h; n > 0u; --n) {
        dato((uint8_t)(c >> 8));
        dato((uint8_t)c);
    }
    suelta();
}

/* Letras de 5x7, una fila por byte (el bit 4 a la izquierda) */
static const uint8_t* glifo(char ch)
{
    static const uint8_t blanco[7] = {0};
    static const uint8_t guion[7]  = {0, 0, 0, 0x1F, 0, 0, 0};
    static const uint8_t igual[7]  = {0, 0, 0x1F, 0, 0x1F, 0, 0};
    static const uint8_t cifras[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
    static const uint8_t letras[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
    if (ch >= '0' && ch <= '9') return cifras[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return letras[ch - 'A'];
    if (ch == '-') return guion;
    if (ch == '=') return igual;
    return blanco;
}

/* Un texto, cada letra en su celda de 6x8 por `e`, con su fondo */
static void texto(uint32_t x, uint32_t y, const char* s, uint32_t e, uint16_t tinta,
                  uint16_t fondo)
{
    for (; *s; ++s, x += 6u * e) {
        const uint8_t* g = glifo(*s);
        ventana(x, y, x + 6u * e - 1u, y + 8u * e - 1u);
        for (uint32_t f = 0; f < 8u * e; ++f) {
            const uint32_t fila = f / e;
            for (uint32_t c = 0; c < 6u * e; ++c) {
                const uint32_t col = c / e;
                const int on = fila < 7u && col < 5u && ((g[fila] >> (4u - col)) & 1u);
                const uint16_t k = on ? tinta : fondo;
                dato((uint8_t)(k >> 8));
                dato((uint8_t)k);
            }
        }
        suelta();
    }
}

static void hex(char* d, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; --i, v >>= 4) d[i] = "0123456789ABCDEF"[v & 15u];
    d[n] = 0;
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    /* Los pines: CS y RESET arriba ANTES de ser salidas */
    cs(1); rst(1); ad(1);
    modo(GPIOB, PIN_CS, 1u);
    modo(GPIOC, PIN_AD, 1u);
    modo(GPIOA, PIN_RST, 1u);
    af(GPIOA, 5u, 5u);                       /* SPI1_SCK  */
    af(GPIOA, 7u, 5u);                       /* SPI1_MOSI */
    af(GPIOA, 2u, 7u);                       /* USART2_TX: el VCP */
    USART2->BRR = 139u;                      /* 16 MHz / 115200 */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;

    /* Maestro, modo 0, 8 bits, NSS por software, f_PCLK / 2 = 8 MHz */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_SPE;

    /* 1. El reset */
    espera_ms(5);
    rst(0);
    espera_ms(1);
    rst(1);
    espera_ms(125);                          /* los 120 ms, y algo mas */

    /* 2. Quién es */
    char id[8], linea[48] = "tft_demo: ST7735S ID ";
    hex(id, lee_id(), 6);
    {
        char* p = linea;
        while (*p) ++p;
        for (int i = 0; id[i]; ++i) *p++ = id[i];
        *p++ = '\r'; *p++ = '\n'; *p = 0;
    }
    vcp(linea);

    /* 3. Despertarla, el formato, la postura, y encenderla */
    orden(0x11); suelta();                   /* SLPOUT */
    espera_ms(125);                          /* los 120 ms, y algo mas */
    orden(0x3A); dato(0x05); suelta();       /* COLMOD: 16 bits */
    orden(0x36); dato(0x60); suelta();       /* MADCTL: MV y MX */
    orden(0x13); suelta();                   /* NORON */
    orden(0x29); suelta();                   /* DISPON: ahora se ve el ruido */
    espera_ms(10);

    /* 4. El dibujo */
    const uint16_t fondo = RGB(0, 24, 64);
    rect(0, 0, ANCHO, ALTO, fondo);
    texto(8, 6, "MCU-SIM", 2u, RGB(255, 255, 255), fondo);
    static const uint16_t barras[8] = {
        RGB(255, 255, 255), RGB(255, 255, 0), RGB(0, 255, 255), RGB(0, 255, 0),
        RGB(255, 0, 255), RGB(255, 0, 0), RGB(0, 0, 255), RGB(0, 0, 0)};
    for (uint32_t k = 0; k < 8u; ++k) rect(k * 20u, 30u, 20u, 40u, barras[k]);
    texto(8, 76, "ST7735S 128X160", 1u, RGB(255, 255, 0), fondo);
    {
        char t[16] = "ID ";
        for (int i = 0; id[i]; ++i) t[3 + i] = id[i];
        t[9] = 0;
        texto(8, 88, t, 1u, RGB(0, 255, 0), fondo);
    }
    vcp("tft_demo: dibujado\r\n");

    /* 5. Lo que se mueve */
    uint32_t x = 0, n = 0;
    int dx = 4;
    for (;;) {
        rect(x, 104u, 12u, 12u, fondo);
        if ((int)x + dx < 0 || x + (uint32_t)dx + 12u > 100u) dx = -dx;
        x = (uint32_t)((int)x + dx);
        rect(x, 104u, 12u, 12u, RGB(255, 128, 0));
        char t[8];
        hex(t, n++, 4);
        texto(112, 106, t, 1u, RGB(255, 255, 255), fondo);
        espera_ms(40);
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
