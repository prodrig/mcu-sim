/* ===========================================================================
 * main.c — Dos motores de continua con su encoder tras un L298N, con un
 *          servo de aguja, un encoder de mando, una pantalla de pie y un
 *          FTDI, en una NUCLEO-F446RE
 *
 * El firmware de placas/nucleo_f446re_encoder_lcd_servo_2xmcc.xml: el
 * sistema de placas/nucleo_f446re_encoder_tft_servo.xml, con el mismo
 * cableado, más un módulo L298N, dos motorreductores con un encoder de 24 PPR
 * cada uno y la batería de los motores:
 *
 *     L298N      ENA   IN1   IN2   IN3   IN4   ENB   GND
 *     Nucleo     D15   A2    A3    A4    A5    D14   GND
 *     conector   CN5.10 CN8.3 CN8.4 CN8.5 CN8.6 CN5.9 CN5.7
 *     chip       PB8   PA4   PB0   PC1   PC0   PB9   VSS
 *
 *     encoder A  A      B      VCC    GND        encoder B  A      B      VCC     GND
 *     Nucleo     A0     A1     +3V3   GND        Nucleo     D7     D8     IOREF   GND
 *     conector   CN8.1  CN8.2  CN7.5  CN7.22     conector   CN9.8  CN5.1  CN7.12  CN10.9
 *     chip       PA0    PA1    VDD    VSS        chip       PA8    PA9    VDD     VSS
 *
 * y los del sistema sin motores: el servo en D5 (PB4, TIM3_CH1), el encoder
 * de mando en D2, D3 y D4, la pantalla en D13, D11, D10, D9 y D6, y el FTDI
 * en la USART3 por PC10 y PC11.
 *
 * LOS MOTORES: la VELOCIDAD es el PWM de ENA y ENB, el TIM4 en PB8 y PB9
 * (canales 3 y 4, AF2) a 1 kHz -el reloj del temporizador, el HSI de 16 MHz,
 * entre 16 es 1 MHz, y 1000 cuentas son 1 ms-: CCR3 y CCR4 son el ciclo de
 * trabajo en milésimas. El SENTIDO, IN1 e IN2 (IN3 e IN4): adelante, IN1 alto
 * e IN2 bajo; atrás, al revés. Con el PWM a cero, las dos bajas y ENA bajo:
 * el motor va libre. Los dos motores llevan lo mismo.
 *
 * Y CADA UNO SU ENCODER, contado por un temporizador en MODO ENCODER: el TIM2
 * con PA0 y PA1 (AF1) el del motor A, el TIM1 con PA8 y PA9 (AF1) el del B.
 * SMS = 011, los dos flancos de los dos canales: cuatro cuentas por pulso,
 * 96 por vuelta del eje de salida, y hacia arriba girando a la derecha -con
 * el borne + del motor, OUT1, más alto: hacia delante-. Cada 250 ms se mira
 * cuánto han contado: rpm = cuentas · 60 / (96 · 0,25) = cuentas · 2,5.
 *
 * EL MANDO, el KY-040 de 24 PPR: cada clic es un 10 % más de PWM -o menos-,
 * de -100 % (atrás) a +100 %; apretar el eje los para. Por el FTDI, '+', '-'
 * y '0' hacen lo mismo.
 *
 * EL SERVO hace de AGUJA de un velocímetro: el de la velocidad que se mide en
 * el motor A, de -250 rpm (a -90 grados) a +250 (a +90): el pulso son 1500 us
 * más 2 us por rpm.
 *
 * LA PANTALLA, de pie: arriba "MOTORES" y el PWM pedido; en medio, una barra
 * por motor -del centro a la derecha hacia delante, a la izquierda hacia
 * atrás; 300 rpm son media pantalla- con sus rpm debajo.
 *
 * Y POR EL FTDI, 115200 8N1, una línea cuando algo cambia:
 * "2xmcc: pwm +60%, A +238 rpm, B +238 rpm".
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
#define PIN_CLK 10u   /* PA10, D2: el encoder de mando */
#define PIN_DT   3u   /* PB3,  D3  */
#define PIN_SW   5u   /* PB5,  D4  */
#define PIN_PWM  4u   /* PB4,  D5: el servo, TIM3_CH1 */
#define PIN_TX  10u   /* PC10, CN7.1: USART3_TX, al RX del FTDI */
#define PIN_RX  11u   /* PC11, CN7.2: USART3_RX, del TX del FTDI */
#define PIN_ENA  8u   /* PB8,  D15: TIM4_CH3 */
#define PIN_ENB  9u   /* PB9,  D14: TIM4_CH4 */
#define PIN_IN1  4u   /* PA4,  A2  */
#define PIN_IN2  0u   /* PB0,  A3  */
#define PIN_IN3  1u   /* PC1,  A4  */
#define PIN_IN4  0u   /* PC0,  A5  */

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

/* --- El mando: la cuenta, en clics del 10 % ----------------------------------- */
#define CLICS_MAX 10                         /* 10 x 10 % = 100 % */
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

/* --- Los motores ------------------------------------------------------------- */
static void pin(GPIO_TypeDef* g, uint32_t n, int v)
{
    g->BSRR = v ? (1u << n) : (1u << (n + 16u));
}
/* El PWM en %, de -100 a +100: el sentido en los IN y el ciclo en los EN */
static void motores(int pwm)
{
    const int adelante = pwm > 0, atras = pwm < 0;
    const uint32_t ciclo = (uint32_t)(pwm < 0 ? -pwm : pwm) * 10u;   /* milesimas */
    pin(GPIOA, PIN_IN1, adelante);
    pin(GPIOB, PIN_IN2, atras);
    pin(GPIOC, PIN_IN3, adelante);
    pin(GPIOC, PIN_IN4, atras);
    TIM4->CCR3 = ciclo;                      /* 1000: siempre alto */
    TIM4->CCR4 = ciclo;
}

/* --- El servo: la aguja de las rpm del motor A ------------------------------- */
static uint32_t pulso_de_rpm(int rpm)
{
    int p = 1500 + 2 * rpm;
    if (p < 1000) p = 1000;
    if (p > 2000) p = 2000;
    return (uint32_t)p;
}

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
    static const uint8_t porcentaje[7] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
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
        case '%': return porcentaje;
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

static const uint16_t FONDO = RGB(0, 24, 64);
static const uint16_t VERDE = RGB(0, 220, 80);
static const uint16_t ROJO = RGB(255, 60, 40);
static const uint16_t GRIS = RGB(60, 70, 100);

/* El PWM pedido, arriba */
static void rotulo_pwm(int pwm)
{
    char t[16], *p = entero(t, pwm, 1);
    pega(p, "%");
    rect(0, 26, ANCHO, 16, FONDO);
    centrado(26, t, 2u, RGB(255, 255, 0), FONDO);
}

/* La barra de un motor: del centro (x = 64) hacia la derecha si va hacia
 * delante, hacia la izquierda si va hacia atras; 300 rpm son 60 pixeles */
#define BARRA_X 64u
#define BARRA_MAX 60
static void barra(uint32_t y, int rpm)
{
    int n = rpm * BARRA_MAX / 300;
    if (n > BARRA_MAX) n = BARRA_MAX;
    if (n < -BARRA_MAX) n = -BARRA_MAX;
    rect(BARRA_X - BARRA_MAX, y, 2u * BARRA_MAX + 1u, 14u, GRIS);
    if (n > 0) rect(BARRA_X + 1u, y, (uint32_t)n, 14u, VERDE);
    if (n < 0) rect(BARRA_X - (uint32_t)(-n), y, (uint32_t)(-n), 14u, ROJO);
    rect(BARRA_X, y - 2u, 1u, 18u, RGB(255, 255, 255));
}
static void rotulo_rpm(uint32_t y, const char* quien, int rpm)
{
    char t[24], *p = pega(t, quien);
    p = entero(p, rpm, 1);
    pega(p, " RPM");
    rect(0, y, ANCHO, 8, FONDO);
    centrado(y, t, 1u, RGB(255, 255, 255), FONDO);
}

static void dilo(int pwm, int ra, int rb)
{
    char t[64], *p = pega(t, "2xmcc: pwm ");
    p = entero(p, pwm, 1);
    p = pega(p, "%, A ");
    p = entero(p, ra, 1);
    p = pega(p, " rpm, B ");
    p = entero(p, rb, 1);
    pega(p, " rpm\r\n");
    ftdi(t);
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM4EN |
                    RCC_APB1ENR_TIM2EN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_SYSCFGEN | RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000u);

    /* LOS MOTORES, parados, lo primero: los IN bajos y salidas; el TIM4 a
     * 1 kHz con el ciclo a cero en PB8 y PB9 */
    motores(0);
    modo(GPIOA, PIN_IN1, 1u);
    modo(GPIOB, PIN_IN2, 1u);
    modo(GPIOC, PIN_IN3, 1u);
    modo(GPIOC, PIN_IN4, 1u);
    TIM4->PSC = 15u;                         /* 16 MHz / 16 = 1 MHz */
    TIM4->ARR = 999u;                        /* 1000 us: 1 kHz */
    TIM4->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                  (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;   /* PWM 1 */
    TIM4->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM4->CR1 = TIM_CR1_ARPE;
    TIM4->EGR = TIM_EGR_UG;
    af(GPIOB, PIN_ENA, 2u);                  /* PB8: TIM4_CH3 */
    af(GPIOB, PIN_ENB, 2u);                  /* PB9: TIM4_CH4 */
    TIM4->CR1 |= TIM_CR1_CEN;

    /* SUS ENCODERS: TIM2 (PA0, PA1) y TIM1 (PA8, PA9) en modo encoder 3,
     * TI1 y TI2 sin invertir, contando los cuatro flancos */
    af(GPIOA, 0u, 1u);
    af(GPIOA, 1u, 1u);
    af(GPIOA, 8u, 1u);
    af(GPIOA, 9u, 1u);
    TIM2->CCMR1 = (1u << TIM_CCMR1_CC1S_Pos) | (1u << TIM_CCMR1_CC2S_Pos);
    TIM2->SMCR = 3u << TIM_SMCR_SMS_Pos;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->CR1 = TIM_CR1_CEN;
    TIM1->CCMR1 = (1u << TIM_CCMR1_CC1S_Pos) | (1u << TIM_CCMR1_CC2S_Pos);
    TIM1->SMCR = 3u << TIM_SMCR_SMS_Pos;
    TIM1->ARR = 0xFFFFu;
    TIM1->CR1 = TIM_CR1_CEN;

    /* EL SERVO: TIM3 a 1 MHz, 20 ms de periodo, la aguja en el 0 */
    TIM3->PSC = 15u;
    TIM3->ARR = 19999u;
    TIM3->CCR1 = pulso_de_rpm(0);
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM3->CCER = TIM_CCER_CC1E;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    af(GPIOB, PIN_PWM, 2u);                  /* PB4: TIM3_CH1 */
    TIM3->CR1 |= TIM_CR1_CEN;

    /* EL MANDO: CLK y DT entradas -con los pull-ups del modulo-, SW con el
     * pull-up interno, y CLK interrumpe en la bajada */
    modo(GPIOA, PIN_CLK, 0u);
    modo(GPIOB, PIN_DT, 0u);                 /* PB3 nace como JTDO: entrada */
    modo(GPIOB, PIN_SW, 0u);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << (2u * PIN_SW))) | (1u << (2u * PIN_SW));
    SYSCFG->EXTICR[2] &= ~SYSCFG_EXTICR3_EXTI10;   /* EXTI10 del puerto A */
    EXTI->FTSR |= 1u << PIN_CLK;
    EXTI->PR = 1u << PIN_CLK;
    EXTI->IMR |= 1u << PIN_CLK;
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    /* EL FTDI: la USART3 en PC10 y PC11, 115200 8N1 */
    af(GPIOC, PIN_TX, 7u);
    af(GPIOC, PIN_RX, 7u);
    GPIOC->PUPDR = (GPIOC->PUPDR & ~(3u << (2u * PIN_RX))) | (1u << (2u * PIN_RX));
    USART3->BRR = 139u;                      /* 16 MHz / 115200 */
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART3_IRQn);
    ftdi("2xmcc: L298N con PWM de 1 kHz en PB8 y PB9; encoders en TIM2 y TIM1\r\n");

    /* LA PANTALLA: los pines, CS y RESET arriba ANTES de ser salidas */
    cs(1); rst(1); ad(1);
    modo(GPIOB, PIN_CS, 1u);
    modo(GPIOC, PIN_AD, 1u);
    modo(GPIOB, PIN_RST, 1u);
    af(GPIOA, 5u, 5u);                       /* SPI1_SCK  */
    af(GPIOA, 7u, 5u);                       /* SPI1_MOSI */
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

    centrado(4, "MOTORES", 2u, RGB(255, 255, 255), FONDO);
    texto(4, 62, "A", 1u, RGB(200, 200, 200), FONDO);
    texto(4, 104, "B", 1u, RGB(200, 200, 200), FONDO);
    centrado(144, "10% POR CLIC", 1u, RGB(140, 160, 200), FONDO);

    int pwm = 0, ra = 0, rb = 0, sw_antes = 1;
    rotulo_pwm(pwm);
    barra(58, ra);
    rotulo_rpm(78, "A ", ra);
    barra(100, rb);
    rotulo_rpm(120, "B ", rb);
    dilo(pwm, ra, rb);

    uint32_t ca = TIM2->CNT, cb = TIM1->CNT, t_med = g_ms;
    for (;;) {
        /* El eje apretado: parados */
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
        int cambia = 0;
        const int p = g_cuenta * 10;
        if (p != pwm) {
            pwm = p;
            motores(pwm);
            rotulo_pwm(pwm);
            cambia = 1;
        }
        /* Cada 250 ms, lo que han contado los encoders */
        if (g_ms - t_med >= 250u) {
            t_med += 250u;
            const uint32_t na = TIM2->CNT, nb = TIM1->CNT;
            const int da = (int)(int32_t)(na - ca);
            const int db = (int)(int16_t)(uint16_t)(nb - cb);
            ca = na;
            cb = nb;
            const int a = da * 5 / 2, b = db * 5 / 2;
            if (a != ra) { ra = a; barra(58, ra); rotulo_rpm(78, "A ", ra); cambia = 1; }
            if (b != rb) { rb = b; barra(100, rb); rotulo_rpm(120, "B ", rb); cambia = 1; }
            TIM3->CCR1 = pulso_de_rpm(ra);
        }
        if (cambia) dilo(pwm, ra, rb);
        espera_ms(2);
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
