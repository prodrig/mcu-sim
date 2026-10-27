/* ===========================================================================
 * main.c — Un servo que se mueve con un encoder de 24 PPR, una pantalla de
 *          pie que lo dice y un adaptador FTDI que lo cuenta, en una
 *          NUCLEO-F446RE
 *
 * El firmware de placas/nucleo_f446re_encoder_tft_servo.xml: cuatro módulos
 * cableados a la Nucleo, como se haría con unos cables de colores:
 *
 *     servo      P1.PWM  P1.VCC  P1.GND
 *     Nucleo     D5      +5V     GND
 *     chip       PB4     (USB)   VSS
 *
 *     encoder    P1.CLK  P1.DT   P1.SW   P1.VCC  P1.GND
 *     Nucleo     D2      D3      D4      +3V3    GND
 *     chip       PA10    PB3     PB5     VDD     VSS
 *
 *     pantalla   P1.SCK  P1.SDA  P1.CS   P1.AD   P1.RESET  P1.VCC  P1.LED  P1.GND
 *     Nucleo     D13     D11     D10     D9      D6        +3V3    IOREF   GND
 *     chip       PA5     PA7     PB6     PC7     PB10      VDD     VDD     VSS
 *
 *     FTDI       P1.RX   P1.TX   P1.GND
 *     Nucleo     CN7.1   CN7.2   CN7.8
 *     chip       PC10    PC11    VSS
 *
 * EL SERVO: la señal es el TIM3, canal 1, en PB4 (AF2), en PWM: el reloj del
 * temporizador -el HSI, 16 MHz- entre 16 es 1 MHz, y 20.000 cuentas son los
 * 20 ms del periodo. CCR1 es la anchura del pulso EN MICROSEGUNDOS: 1500 el
 * centro, 1000 los -90 grados y 2000 los +90. Arranca en el centro, y el
 * servo, que empieza donde se quedó, va hacia él.
 *
 * EL ENCODER es de 24 clics y 24 pulsos por vuelta (placas/ky040_24ppr.xml):
 * en cada clic los dos contactos se cierran y se abren, un ciclo entero. Hay
 * UN flanco de bajada de CLK por clic, y en él DT dice el sentido: alto, a la
 * derecha; bajo, a la izquierda. Cada clic son 7,5 grados -una vuelta del
 * mando lleva el servo de punta a punta, doce clics a cada lado del centro-.
 * CLK interrumpe en la bajada (EXTI10). Los pull-ups de CLK y DT son los del
 * módulo; el del pulsador, SW, el interno de PB5. Apretar el eje lleva el
 * servo al CENTRO.
 *
 * LA PANTALLA, DE PIE con el conector abajo (giro="90" en el sistema):
 * MADCTL 0x00, la postura de fábrica, x de 0 a 127 de izquierda a derecha e
 * y de 0 a 159 de arriba abajo. Arriba "SERVO"; en medio, un cuadrante de
 * -90 a +90 grados con una aguja que apunta adonde se ha pedido; y debajo el
 * ángulo y la anchura del pulso. El SPI1 y la iniciación son los de
 * ../servo_demo, con el RESET en D6 (PB10): D7 y D8 se quedan libres para el
 * TIM1 del sistema ampliado (placas/nucleo_f446re_encoder_lcd_servo_2xmcc.xml).
 *
 * EL FTDI, por la USART3 (PC10 su TX, PC11 su RX; AF7), a 115200 8N1: una
 * línea con cada cambio, "encoder_tft_servo: angulo +7.5, pulso 1541 us". Y
 * escucha: '+' es un clic a la derecha, '-' uno a la izquierda y '0' el
 * centro, como el mando.
 * ===========================================================================*/
#include "stm32f446xx.h"

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; }

static void espera_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) __WFI();
}

/* --- Los pines ------------------------------------------------------------ */
#define PIN_CS   6u   /* PB6,  D10: la pantalla */
#define PIN_AD   7u   /* PC7,  D9  */
#define PIN_RST 10u   /* PB10, D6  */
#define PIN_CLK 10u   /* PA10, D2: el encoder */
#define PIN_DT   3u   /* PB3,  D3  */
#define PIN_SW   5u   /* PB5,  D4  */
#define PIN_PWM  4u   /* PB4,  D5: el servo, TIM3_CH1 */
#define PIN_TX  10u   /* PC10, CN7.1: USART3_TX, al RX del FTDI */
#define PIN_RX  11u   /* PC11, CN7.2: USART3_RX, del TX del FTDI */

static void cs(int v)  { GPIOB->BSRR = v ? (1u << PIN_CS)  : (1u << (PIN_CS + 16u)); }
static void ad(int v)  { GPIOC->BSRR = v ? (1u << PIN_AD)  : (1u << (PIN_AD + 16u)); }
static void rst(int v) { GPIOB->BSRR = v ? (1u << PIN_RST) : (1u << (PIN_RST + 16u)); }

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

/* --- El encoder: la cuenta, en clics de 7,5 grados ---------------------------- */
#define CLICS_MAX 12                         /* 12 x 7,5 = 90 grados */
static volatile int g_cuenta;

static void suma(int d)
{
    int c = g_cuenta + d;
    if (c > CLICS_MAX) c = CLICS_MAX;        /* en el tope, otro clic no hace nada */
    if (c < -CLICS_MAX) c = -CLICS_MAX;
    g_cuenta = c;
}

void EXTI15_10_IRQHandler(void)
{
    EXTI->PR = 1u << PIN_CLK;
    /* La bajada de CLK: un clic. DT alto, a la derecha */
    suma(((GPIOB->IDR >> PIN_DT) & 1u) ? 1 : -1);
}

/* --- El servo: el angulo va en DECIMAS de grado ---------------------------- */
static uint32_t pulso_de(int d10) { return (uint32_t)(1500 + d10 * 5 / 9); }

/* --- El FTDI: la USART3 ----------------------------------------------------- */
static volatile uint8_t g_rx[16];
static volatile uint32_t g_rx_pon, g_rx_toma;

void USART3_IRQHandler(void)
{
    if (USART3->SR & (USART_SR_RXNE | USART_SR_ORE)) {
        const uint8_t b = (uint8_t)USART3->DR;   /* tambien limpia ORE */
        const uint32_t n = (g_rx_pon + 1u) % sizeof g_rx;
        if (n != g_rx_toma) { g_rx[g_rx_pon] = b; g_rx_pon = n; }
    }
}
static int recibe(void)
{
    if (g_rx_toma == g_rx_pon) return -1;
    const int b = g_rx[g_rx_toma];
    g_rx_toma = (g_rx_toma + 1u) % sizeof g_rx;
    return b;
}
static void ftdi(const char* s)
{
    while (*s) {
        while ((USART3->SR & USART_SR_TXE) == 0u) { }
        USART3->DR = (uint8_t)*s++;
    }
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

/* Un entero con su signo delante (+5, -90, 0) o sin él */
static char* entero(char* d, int v, int signo)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) *d++ = '-';
    else if (signo && v > 0) *d++ = '+';
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}
/* Unas décimas como número con un decimal: +7.5, -90.0, 0.0 */
static char* decimas(char* d, int v)
{
    if (v < 0) { *d++ = '-'; v = -v; }
    else if (v > 0) *d++ = '+';
    d = entero(d, v / 10, 0);
    *d++ = '.';
    *d++ = (char)('0' + v % 10);
    *d = 0;
    return d;
}
static char* pega(char* d, const char* s)
{
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

/* --- Dibujar -------------------------------------------------------------- */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))
#define ANCHO 128u
#define ALTO  160u

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

/* Letras de 5x7, una fila por byte (el bit 4 a la izquierda). '*' es el
 * circulito de los grados */
static const uint8_t* glifo(char ch)
{
    static const uint8_t blanco[7] = {0};
    static const uint8_t guion[7]  = {0, 0, 0, 0x1F, 0, 0, 0};
    static const uint8_t mas[7]    = {0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0};
    static const uint8_t grado[7]  = {0x0C, 0x12, 0x12, 0x0C, 0, 0, 0};
    static const uint8_t dos_p[7]  = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0};
    static const uint8_t barra[7]  = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    static const uint8_t punto[7]  = {0, 0, 0, 0, 0, 0x0C, 0x0C};
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
    switch (ch) {
        case '-': return guion;
        case '+': return mas;
        case '*': return grado;
        case ':': return dos_p;
        case '/': return barra;
        case '.': return punto;
        default:  return blanco;
    }
}

static uint32_t largo(const char* s) { uint32_t n = 0; while (s[n]) ++n; return n; }

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
/* Centrado en la pantalla */
static void centrado(uint32_t y, const char* s, uint32_t e, uint16_t tinta, uint16_t fondo)
{
    texto((ANCHO - largo(s) * 6u * e) / 2u, y, s, e, tinta, fondo);
}

/* --- El cuadrante ------------------------------------------------------------ *
 * Centrado en (CX, CY), de -90 grados -a la izquierda- a +90 -a la derecha-,
 * con el 0 arriba. Sin coma flotante: el seno de 0 a 90 grados cada 2,5, por
 * mil. Los ángulos van en décimas de grado, múltiplos de 25 */
#define CX 64
#define CY 100
static const int16_t seno[37] = {0, 44, 87, 131, 174, 216, 259, 301, 342, 383, 423, 462, 500,
                                 537, 574, 609, 643, 676, 707, 737, 766, 793, 819, 843, 866,
                                 887, 906, 924, 940, 954, 966, 976, 985, 991, 996, 999, 1000};
static int sen(int d10) { return d10 < 0 ? -seno[-d10 / 25] : seno[d10 / 25]; }
static int cosn(int d10) { return seno[(900 - (d10 < 0 ? -d10 : d10)) / 25]; }

/* Un cuadradito de lado `l` centrado en el punto del radio r a d10 décimas */
static void punto(int d10, int r, uint32_t l, uint16_t c)
{
    const int x = CX + sen(d10) * r / 1000, y = CY - cosn(d10) * r / 1000;
    rect((uint32_t)(x - (int)l / 2), (uint32_t)(y - (int)l / 2), l, l, c);
}

static void aguja(int d10, uint16_t c)
{
    for (int r = 10; r <= 44; r += 2) punto(d10, r, 3u, c);
}

static const uint16_t FONDO = RGB(0, 24, 64);
static const uint16_t NARANJA = RGB(255, 128, 0);

static void cuadrante(void)
{
    for (int d = -900; d <= 900; d += 150)
        punto(d, 52, (d % 450) ? 3u : 5u, RGB(255, 255, 255));
    texto(2, CY + 6, "-90", 1u, RGB(200, 200, 200), FONDO);
    texto(CX - 2, CY - 66, "0", 1u, RGB(200, 200, 200), FONDO);
    texto(ANCHO - 20u, CY + 6, "+90", 1u, RGB(200, 200, 200), FONDO);
}

static void buje(void) { rect(CX - 4, CY - 4, 9u, 9u, RGB(160, 160, 160)); }

/* El ángulo y el pulso, debajo */
static void rotulos(int d10, uint32_t pulso)
{
    char t[24], *p;
    p = decimas(t, d10);
    pega(p, "*");
    rect(0, 114, ANCHO, 16, FONDO);
    centrado(114, t, 2u, RGB(255, 255, 255), FONDO);
    p = pega(t, "PULSO ");
    p = entero(p, (int)pulso, 0);
    pega(p, " US");
    centrado(136, t, 1u, RGB(255, 255, 0), FONDO);
}

static void dilo(int d10, uint32_t pulso)
{
    char t[64], *p = pega(t, "encoder_tft_servo: angulo ");
    p = decimas(p, d10);
    p = pega(p, ", pulso ");
    p = entero(p, (int)pulso, 0);
    pega(p, " us\r\n");
    ftdi(t);
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN | RCC_APB1ENR_TIM3EN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    /* EL SERVO, lo primero: TIM3 a 1 MHz, 20 ms de periodo, en el centro */
    TIM3->PSC = 15u;                         /* 16 MHz / 16 = 1 MHz */
    TIM3->ARR = 19999u;                      /* 20.000 us */
    TIM3->CCR1 = pulso_de(0);
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;   /* PWM 1 */
    TIM3->CCER = TIM_CCER_CC1E;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    af(GPIOB, PIN_PWM, 2u);                  /* PB4: TIM3_CH1 */
    TIM3->CR1 |= TIM_CR1_CEN;

    /* EL ENCODER: CLK y DT entradas -con los pull-ups del modulo-, SW con
     * el pull-up interno, y CLK interrumpe en la bajada */
    modo(GPIOA, PIN_CLK, 0u);
    modo(GPIOB, PIN_DT, 0u);                 /* PB3 nace como JTDO: entrada */
    modo(GPIOB, PIN_SW, 0u);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << (2u * PIN_SW))) | (1u << (2u * PIN_SW));
    SYSCFG->EXTICR[2] &= ~SYSCFG_EXTICR3_EXTI10;   /* EXTI10 del puerto A */
    EXTI->FTSR |= 1u << PIN_CLK;
    EXTI->PR = 1u << PIN_CLK;
    EXTI->IMR |= 1u << PIN_CLK;
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    /* EL FTDI: la USART3 en PC10 y PC11, 115200 8N1, con su interrupcion de
     * recepcion. El RX con pull-up, por si el adaptador no esta */
    af(GPIOC, PIN_TX, 7u);
    af(GPIOC, PIN_RX, 7u);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3u << (2u * PIN_RX))) | (1u << (2u * PIN_RX));
    USART3->BRR = 139u;                      /* 16 MHz / 115200 */
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART3_IRQn);
    ftdi("encoder_tft_servo: TIM3 a 50 Hz en PB4; al centro\r\n");

    /* LA PANTALLA: los pines, CS y RESET arriba ANTES de ser salidas */
    cs(1); rst(1); ad(1);
    modo(GPIOB, PIN_CS, 1u);
    modo(GPIOC, PIN_AD, 1u);
    modo(GPIOB, PIN_RST, 1u);
    af(GPIOA, 5u, 5u);                       /* SPI1_SCK  */
    af(GPIOA, 7u, 5u);                       /* SPI1_MOSI */

    /* Maestro, modo 0, 8 bits, NSS por software, f_PCLK / 2 = 8 MHz */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI1->CR1 |= SPI_CR1_SPE;

    espera_ms(5);
    rst(0);
    espera_ms(1);
    rst(1);
    espera_ms(125);                          /* los 120 ms, y algo mas */
    orden(0x11); suelta();                   /* SLPOUT */
    espera_ms(125);
    orden(0x3A); dato(0x05); suelta();       /* COLMOD: 16 bits */
    orden(0x36); dato(0x00); suelta();       /* MADCTL: de pie, conector abajo */
    orden(0x13); suelta();                   /* NORON */
    rect(0, 0, ANCHO, ALTO, FONDO);          /* borrada ANTES de encenderla */
    orden(0x29); suelta();                   /* DISPON */

    centrado(6, "SERVO", 2u, RGB(255, 255, 255), FONDO);
    cuadrante();
    centrado(150, "7.5* POR CLIC", 1u, RGB(140, 160, 200), FONDO);

    int d10 = 0, sw_antes = 1;
    aguja(d10, NARANJA);
    buje();
    rotulos(d10, pulso_de(d10));
    dilo(d10, pulso_de(d10));

    for (;;) {
        /* El eje apretado: al centro */
        const int sw = (int)((GPIOB->IDR >> PIN_SW) & 1u);
        if (!sw && sw_antes) g_cuenta = 0;
        sw_antes = sw;
        /* Lo que llegue por el FTDI */
        for (int b; (b = recibe()) >= 0;) {
            __disable_irq();                 /* la cuenta es tambien del EXTI */
            if (b == '+') suma(1);
            else if (b == '-') suma(-1);
            else if (b == '0') g_cuenta = 0;
            __enable_irq();
        }
        const int g = g_cuenta * 75;
        if (g != d10) {
            const uint32_t p = pulso_de(g);
            TIM3->CCR1 = p;                  /* en el siguiente periodo */
            aguja(d10, FONDO);
            aguja(g, NARANJA);
            buje();
            d10 = g;
            rotulos(g, p);
            dilo(g, p);
        }
        espera_ms(2);
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
