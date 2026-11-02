/* ===========================================================================
 * main.c — UN BLDC CONMUTADO POR EL MCU, EN SEIS PASOS CON SUS HALL: una
 *          NUCLEO-F446RE con una X-NUCLEO-IHM07M1 y un Nanotec DB42S01
 *
 * El firmware de la fase F7 de doc/analisis_cnc.md (§5.4): sin controlador
 * que lo conmute por dentro, el MCU decide en cada momento qué dos fases del
 * motor llevan corriente. Es el de placas/nucleo_ihm07m1_db42.xml.
 *
 * LOS SEIS PASOS. Los tres Hall dicen en qué sector de 60° eléctricos está el
 * rotor; en cada uno se alimentan las dos fases cuya f.e.m. está plana -una
 * en +, otra en -- y la tercera se deja abierta:
 *
 *     Hall (H1 H2 H3)   101   100   110   010   011   001
 *     arriba (PWM)       U     U     V     V     W     W
 *     abajo (a masa)     V     W     W     U     U     V
 *
 * La de arriba lleva el PWM de 20 kHz del TIM1 en su IN -alto, a VS; bajo, a
 * masa-; la de abajo, IN forzado a bajo; la abierta, su EN bajo. Hacia atrás,
 * al revés: arriba y abajo cambiados.
 *
 * EL TIM2 ESCUCHA LOS HALL en su interfaz de sensores: CR2.TI1S pone el XOR de
 * CH1, CH2 y CH3 en TI1, y el canal 1 captura en cada flanco de cualquiera,
 * con el contador puesto a cero por el mismo flanco (modo esclavo reset): CCR1
 * es el tiempo entre dos cambios de Hall, en us. En su interrupción, el
 * firmware conmuta; si en 100 ms no hay cambios, el motor está parado.
 *
 * EL LAZO DE VELOCIDAD, cada milisegundo (SysTick): un PI sobre las rpm
 * medidas que mueve el ciclo del PWM, hasta el 95 %.
 *
 * EN SENOIDAL (M 1): las tres fases llevan PWM y la tensión que se les pone
 * gira con el rotor, una senoide por fase, desfasadas 120°. El ángulo
 * eléctrico sale de los Hall: el sector, y dentro de él lo que ha pasado desde
 * el último cambio contra lo que duró el anterior (el CNT del TIM2 contra
 * CCR1). Se pone al día en cada periodo del PWM, en la interrupción de
 * actualización del TIM1 (RCR = 1: una de cada dos del modo centrado, 20
 * kHz). Es la FOC sin su lazo de corriente: la tensión, no la corriente,
 * va en fase con la f.e.m.
 *
 * LA PROTECCIÓN: DIAG-EN del L6230 va a PA6, la entrada de freno del TIM1
 * (BKE, activa baja). Una sobrecorriente la baja: el TIM1 quita MOE en el acto
 * -el PWM se apaga sin esperar a nadie- y su interrupción lo cuenta, apaga
 * los EN y enciende el LED rojo (PB2).
 *
 * LO QUE ENTIENDE, línea a línea por la USART2 (el VCP, 115200 8N1):
 *
 *     R rpm      a esa velocidad, con el lazo; negativa, hacia atrás
 *     D %        ese ciclo, sin lazo; negativo, hacia atrás (en senoidal, la
 *                amplitud de la senoide)
 *     M 0|1      seis pasos (0) o senoidal (1)
 *     S          para: los EN abajo
 *     P ms       espera
 *     ?          rpm, ciclo, Hall y fallos
 * ===========================================================================*/
#include "stm32f446xx.h"
#include <stdint.h>

#define HCLK   84000000u
#define PCLK1  42000000u
#define TIMCLK 84000000u

#define PWM_ARR   2100u            /* centrado: 84 MHz / (2·2100) = 20 kHz */
#define PARES     4u               /* los del DB42S01 (estimados) */
#define CICLO_MAX 0.95f

/* --- La USART2 ------------------------------------------------------------ */
#define RXN 1024u
#define TXN 2048u
static volatile uint8_t g_rx[RXN];
static volatile uint32_t g_rx_w, g_rx_r;
static volatile uint8_t g_tx[TXN];
static volatile uint32_t g_tx_w, g_tx_r;

void USART2_IRQHandler(void)
{
    const uint32_t sr = USART2->SR;
    if (sr & USART_SR_RXNE) {
        const uint8_t c = (uint8_t)USART2->DR;
        if (((g_rx_w + 1u) % RXN) != g_rx_r) {
            g_rx[g_rx_w] = c;
            g_rx_w = (g_rx_w + 1u) % RXN;
        }
    } else if (sr & USART_SR_ORE) {
        (void)USART2->DR;
    }
    if ((sr & USART_SR_TXE) && (USART2->CR1 & USART_CR1_TXEIE)) {
        if (g_tx_r != g_tx_w) {
            USART2->DR = g_tx[g_tx_r];
            g_tx_r = (g_tx_r + 1u) % TXN;
        } else {
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}
static void envia_c(char c)
{
    while (((g_tx_w + 1u) % TXN) == g_tx_r) __WFI();
    g_tx[g_tx_w] = (uint8_t)c;
    g_tx_w = (g_tx_w + 1u) % TXN;
    USART2->CR1 |= USART_CR1_TXEIE;
}
static void envia(const char* s) { while (*s) envia_c(*s++); }
static void envia_u(uint32_t x)
{
    char b[12];
    int i = 0;
    do { b[i++] = (char)('0' + x % 10u); x /= 10u; } while (x);
    while (i) envia_c(b[--i]);
}
static void envia_f(float v, int dec)
{
    if (v < 0.0f) { envia_c('-'); v = -v; }
    uint32_t m = 1;
    for (int k = 0; k < dec; ++k) m *= 10u;
    const uint32_t x = (uint32_t)(v * (float)m + 0.5f);
    envia_u(x / m);
    if (dec) {
        envia_c('.');
        uint32_t f = x % m;
        for (uint32_t d = m / 10u; d; d /= 10u) { envia_c((char)('0' + f / d)); f %= d; }
    }
}

/* --- Las fases ------------------------------------------------------------ */
static volatile int g_dir = 1;             /* +1 adelante, -1 atrás */
static volatile int g_marcha;              /* conmutando */
static volatile float g_ciclo;             /* 0..CICLO_MAX */
static volatile uint32_t g_fallos;
static volatile uint32_t g_conmutaciones;
static volatile int g_seno;                /* conmutación senoidal */

static inline uint32_t hall(void)
{
    const uint32_t h1 = (GPIOA->IDR >> 15) & 1u;
    const uint32_t h2 = (GPIOB->IDR >> 3) & 1u;
    const uint32_t h3 = (GPIOB->IDR >> 10) & 1u;
    return (h1 << 2) | (h2 << 1) | h3;
}

/* Del código de los Hall, el sector: 101 100 110 010 011 001 son 0..5 */
static int sector(uint32_t h)
{
    static const int8_t s[8] = {-1, 5, 3, 4, 1, 0, 2, -1};
    return s[h & 7u];
}

/* Las fases de arriba y de abajo de cada sector, hacia delante: 0 U, 1 V, 2 W */
static const uint8_t ARRIBA[6] = {0, 0, 1, 1, 2, 2};
static const uint8_t ABAJO[6]  = {1, 2, 2, 0, 0, 1};

static void pon_ciclo(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > CICLO_MAX) c = CICLO_MAX;
    g_ciclo = c;
    if (g_seno) return;                       /* los pone la interrupción */
    const uint32_t ccr = (uint32_t)(c * (float)PWM_ARR);
    TIM1->CCR1 = ccr;
    TIM1->CCR2 = ccr;
    TIM1->CCR3 = ccr;
}

/* Los EN de las fases: PC10, PC11 y PC12 */
static void pon_en(int u, int v, int w)
{
    GPIOC->BSRR = (u ? (1u << 10) : (1u << 26)) | (v ? (1u << 11) : (1u << 27)) |
                  (w ? (1u << 12) : (1u << 28));
}

/* El modo de salida de cada canal del TIM1: PWM 1 (110) o forzado a bajo (100) */
static void pon_modos(int pwm_u, int pwm_v, int pwm_w)
{
    const uint32_t m1 = (pwm_u ? 6u : 4u) << TIM_CCMR1_OC1M_Pos | TIM_CCMR1_OC1PE |
                        (pwm_v ? 6u : 4u) << TIM_CCMR1_OC2M_Pos | TIM_CCMR1_OC2PE;
    const uint32_t m2 = (pwm_w ? 6u : 4u) << TIM_CCMR2_OC3M_Pos | TIM_CCMR2_OC3PE;
    TIM1->CCMR1 = m1;
    TIM1->CCMR2 = m2;
}

static void apaga(void)
{
    g_marcha = 0;
    pon_en(0, 0, 0);
    pon_modos(0, 0, 0);
}

/* Las dos fases que tocan en el sector de los Hall de ahora */
static void conmuta(void)
{
    if (!g_marcha || g_seno) return;
    const int s = sector(hall());
    if (s < 0) { apaga(); return; }
    int a = ARRIBA[s], b = ABAJO[s];
    if (g_dir < 0) { const int t = a; a = b; b = t; }
    int en[3] = {0, 0, 0}, pwm[3] = {0, 0, 0};
    en[a] = en[b] = 1;
    pwm[a] = 1;
    pon_modos(pwm[0], pwm[1], pwm[2]);
    pon_en(en[0], en[1], en[2]);
    ++g_conmutaciones;
}

/* --- En senoidal ------------------------------------------------------------ */
#define PI_F 3.14159265f
static float seno(float x)                 /* x en radianes, cualquiera */
{
    while (x > PI_F) x -= 2.0f * PI_F;
    while (x < -PI_F) x += 2.0f * PI_F;
    const float y = 1.27323954f * x - 0.405284735f * x * (x < 0.0f ? -x : x);
    return 0.225f * (y * (y < 0.0f ? -y : y) - y) + y;
}
static uint32_t periodo_hall(void);
static int sentido_hall(void);
static int sector_hall(void);

/* El ángulo eléctrico, en grados, de los Hall */
static float angulo_e(void)
{
    const int s = sector_hall();
    if (s < 0) return 0.0f;
    const uint32_t p = periodo_hall();
    if (!p) return 60.0f * (float)s + 30.0f;
    float f = (float)TIM2->CNT / (float)p;
    if (f > 1.0f) f = 1.0f;
    return sentido_hall() < 0 ? 60.0f * (float)(s + 1) - 60.0f * f : 60.0f * (float)s + 60.0f * f;
}

/* Cada periodo del PWM: las tres senoides. La f.e.m. de U tiene su máximo a
 * 60° eléctricos (sin(θ + 30°)); la tensión, en fase con ella: hacia atrás,
 * con el signo cambiado */
void TIM1_UP_TIM10_IRQHandler(void)
{
    TIM1->SR = ~TIM_SR_UIF;
    if (!g_marcha || !g_seno) return;
    const float th = (angulo_e() + 30.0f) * PI_F / 180.0f;
    const float m = (float)g_dir * g_ciclo;
    const float a = 0.5f * (float)PWM_ARR;
    TIM1->CCR1 = (uint32_t)(a + a * m * seno(th));
    TIM1->CCR2 = (uint32_t)(a + a * m * seno(th - 2.0943951f));
    TIM1->CCR3 = (uint32_t)(a + a * m * seno(th - 4.1887902f));
}

/* --- La velocidad, de los Hall -------------------------------------------- */
static volatile uint32_t g_periodo;        /* us entre dos cambios; 0, parado */
static volatile int g_sentido;             /* el medido */
static volatile int g_sector_ant = -1;

void TIM2_IRQHandler(void)
{
    const uint32_t sr = TIM2->SR;
    if (sr & TIM_SR_CC1IF) {
        g_periodo = TIM2->CCR1;               /* lee y borra CC1IF */
        const int s = sector(hall());
        if (s >= 0 && g_sector_ant >= 0) {
            const int d = (s - g_sector_ant + 6) % 6;
            if (d == 1) g_sentido = 1;
            else if (d == 5) g_sentido = -1;
        }
        g_sector_ant = s;
        conmuta();
    }
    if (sr & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        g_periodo = 0;                        /* 100 ms sin cambios: parado */
        g_sector_ant = sector(hall());
        conmuta();
    }
}

static uint32_t periodo_hall(void) { return g_periodo; }
static int sentido_hall(void) { return g_sentido; }
static int sector_hall(void) { return sector(hall()); }

static float rpm(void)
{
    const uint32_t p = g_periodo;
    if (!p) return 0.0f;
    return (float)g_sentido * 60.0e6f / ((float)p * 6.0f * (float)PARES);
}

/* --- El fallo: DIAG-EN, por la entrada de freno del TIM1 ------------------ */
static volatile int g_dijo_fallo;
void TIM1_BRK_TIM9_IRQHandler(void)
{
    if (TIM1->SR & TIM_SR_BIF) {
        TIM1->SR = ~TIM_SR_BIF;
        TIM1->DIER &= ~TIM_DIER_BIE;          /* una vez por fallo */
        apaga();
        ++g_fallos;
        g_dijo_fallo = 1;
        GPIOB->BSRR = 1u << 2;                /* el LED rojo */
    }
}

/* --- El lazo: cada milisegundo -------------------------------------------- */
static volatile uint32_t g_ms;
static volatile int g_lazo;                /* con lazo de velocidad */
static volatile float g_rpm_obj, g_integral;
static const float KP = 0.0004f, KI = 0.004f;   /* ciclo por rpm, y por rpm·s */

void SysTick_Handler(void)
{
    ++g_ms;
    if (!g_marcha || !g_lazo) return;
    const float obj = g_rpm_obj < 0.0f ? -g_rpm_obj : g_rpm_obj;
    float med = rpm();
    if (med < 0.0f) med = -med;
    const float e = obj - med;
    float i = g_integral + KI * e * 0.001f;
    float c = KP * e + i;
    if (c > CICLO_MAX) c = CICLO_MAX;
    else if (c < 0.0f) c = 0.0f;
    else g_integral = i;                     /* sin acumular en los topes */
    pon_ciclo(c);
}

/* Arranca hacia `dir` con el ciclo que haya: rearma el freno si hizo falta */
static int arranca(int dir)
{
    if (!(GPIOA->IDR & (1u << 6))) {
        envia("error: DIAG-EN sigue bajo\r\n");
        return 0;
    }
    if (!(TIM1->BDTR & TIM_BDTR_MOE)) {
        TIM1->SR = ~TIM_SR_BIF;
        TIM1->DIER |= TIM_DIER_BIE;
        TIM1->BDTR |= TIM_BDTR_MOE;
        GPIOB->BSRR = 1u << 18;               /* el LED, apagado */
    }
    g_dir = dir;
    g_marcha = 1;
    g_sector_ant = sector(hall());
    if (g_seno) { pon_modos(1, 1, 1); pon_en(1, 1, 1); }
    else conmuta();
    return 1;
}

static void espera(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < ms) __WFI();
}

static void informa(void)
{
    envia("rpm ");
    envia_f(rpm(), 0);
    envia(" ciclo ");
    envia_f(g_ciclo * 100.0f, 1);
    envia("% hall ");
    const uint32_t h = hall();
    envia_c((char)('0' + ((h >> 2) & 1u)));
    envia_c((char)('0' + ((h >> 1) & 1u)));
    envia_c((char)('0' + (h & 1u)));
    envia(" fallos ");
    envia_u(g_fallos);
    envia(g_seno ? " senoidal" : " seis pasos");
    envia(g_marcha ? " en marcha\r\n" : " parado\r\n");
}

static float numero(const char* s)
{
    while (*s == ' ') ++s;
    int neg = 0;
    if (*s == '-') { neg = 1; ++s; }
    float v = 0.0f, d = 0.0f;
    for (; *s; ++s) {
        if (*s >= '0' && *s <= '9') {
            if (d > 0.0f) { v += (float)(*s - '0') * d; d *= 0.1f; }
            else v = v * 10.0f + (float)(*s - '0');
        } else if (*s == '.' && d == 0.0f) d = 0.1f;
        else break;
    }
    return neg ? -v : v;
}

static void ejecuta(char* l)
{
    while (*l == ' ') ++l;
    const char c = l[0] >= 'a' && l[0] <= 'z' ? (char)(l[0] - 32) : l[0];
    if (!c) return;
    switch (c) {
        case 'R': {
            const float r = numero(l + 1);
            g_rpm_obj = r;
            if (r == 0.0f) { apaga(); pon_ciclo(0.0f); break; }
            const int dir = r < 0.0f ? -1 : 1;
            if (g_marcha && dir != g_dir) {    /* hacia el otro lado: primero, a parar */
                apaga();
                pon_ciclo(0.0f);
                while (g_periodo) __WFI();
            }
            if (!g_marcha) { g_integral = 0.0f; pon_ciclo(0.05f); }
            g_lazo = 1;
            if (!arranca(dir)) return;
            break;
        }
        case 'D': {
            const float d = numero(l + 1);
            g_lazo = 0;
            if (d == 0.0f) { apaga(); pon_ciclo(0.0f); break; }
            pon_ciclo((d < 0.0f ? -d : d) / 100.0f);
            if (!arranca(d < 0.0f ? -1 : 1)) return;
            break;
        }
        case 'S': apaga(); pon_ciclo(0.0f); g_lazo = 0; break;
        case 'M':
            apaga();
            g_lazo = 0;
            g_seno = numero(l + 1) != 0.0f;
            if (g_seno) TIM1->DIER |= TIM_DIER_UIE;
            else TIM1->DIER &= ~TIM_DIER_UIE;
            pon_ciclo(0.0f);
            break;
        case 'P': espera((uint32_t)numero(l + 1)); break;
        case '?': informa(); break;
        default:
            envia("error: no entiendo '");
            envia(l);
            envia("'\r\n");
            return;
    }
    envia("ok\r\n");
}

/* --- El arranque ----------------------------------------------------------- */
static void reloj(void)
{
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = (16u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (1u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   (2u << RCC_PLLCFGR_PLLR_Pos);              /* PLLP = 4; fuente, HSI */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW)) |
                RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
}

static void af(GPIO_TypeDef* g, uint32_t n, uint32_t f)
{
    g->MODER = (g->MODER & ~(3u << (2u * n))) | (2u << (2u * n));
    g->AFR[n >> 3] = (g->AFR[n >> 3] & ~(15u << (4u * (n & 7u)))) | (f << (4u * (n & 7u)));
}
static void salida(GPIO_TypeDef* g, uint32_t n)
{
    g->MODER = (g->MODER & ~(3u << (2u * n))) | (1u << (2u * n));
}

int main(void)
{
    /* LO PRIMERO, los Hall: PB3 sale del reset como JTDO, una salida, y el H2
     * del motor, de colector abierto, tira de él a masa. Cuanto antes sea una
     * entrada del TIM2, antes se acaba la pelea */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    af(GPIOA, 15u, 1u);
    af(GPIOB, 3u, 1u);
    af(GPIOB, 10u, 1u);
    reloj();
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN | RCC_APB1ENR_TIM2EN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;

    /* Los EN, abajo, antes que nada */
    pon_en(0, 0, 0);
    salida(GPIOC, 10u);
    salida(GPIOC, 11u);
    salida(GPIOC, 12u);
    salida(GPIOB, 2u);                        /* el LED */
    /* IN1..IN3: TIM1_CH1..CH3 en PA8, PA9 y PA10; DIAG-EN: TIM1_BKIN en PA6 */
    af(GPIOA, 8u, 1u);
    af(GPIOA, 9u, 1u);
    af(GPIOA, 10u, 1u);
    af(GPIOA, 6u, 1u);

    /* La USART2 del VCP: PA2 y PA3, AF7, 115200 */
    af(GPIOA, 2u, 7u);
    af(GPIOA, 3u, 7u);
    USART2->BRR = (PCLK1 + 57600u) / 115200u;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_IRQn);

    /* EL TIM1: PWM centrado de 20 kHz en CH1..CH3, con la entrada de freno */
    TIM1->PSC = 0;
    TIM1->ARR = PWM_ARR;
    TIM1->CR1 = TIM_CR1_CMS_0 | TIM_CR1_ARPE;
    pon_modos(0, 0, 0);
    pon_ciclo(0.0f);
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E;
    TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_BKE;  /* BKP = 0: frena con PA6 bajo */
    TIM1->RCR = 1;                            /* una actualización por periodo */
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;
    TIM1->DIER = TIM_DIER_BIE;                /* la de actualización, solo en senoidal */
    TIM1->CR1 |= TIM_CR1_CEN;
    NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 0);
    NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
    NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 1);
    NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);

    /* EL TIM2: el interfaz de los Hall. TI1S: el XOR de los tres en TI1; CC1
     * captura TRC en cada flanco, y el mismo flanco pone CNT a cero */
    TIM2->PSC = TIMCLK / 1000000u - 1u;       /* 1 us */
    TIM2->ARR = 100000u;                      /* 100 ms sin cambios: parado */
    TIM2->CR2 = TIM_CR2_TI1S;
    TIM2->CCMR1 = 3u << TIM_CCMR1_CC1S_Pos | 3u << TIM_CCMR1_IC1F_Pos;
    TIM2->SMCR = 4u << TIM_SMCR_TS_Pos | 4u << TIM_SMCR_SMS_Pos;   /* TI1F_ED, reset */
    TIM2->CCER = TIM_CCER_CC1E;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_CC1IE | TIM_DIER_UIE;
    TIM2->CR1 = TIM_CR1_URS | TIM_CR1_CEN;    /* el reset del esclavo no da UIF */
    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);

    /* El SysTick, a 1 kHz */
    SysTick->LOAD = HCLK / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    NVIC_SetPriority(SysTick_IRQn, 2);

    envia("\r\nseis pasos (F7): DB42S01 en una X-NUCLEO-IHM07M1, PWM de 20 kHz, ");
    envia_u(PARES);
    envia(" pares de polos; Hall ");
    const uint32_t h = hall();
    envia_c((char)('0' + ((h >> 2) & 1u)));
    envia_c((char)('0' + ((h >> 1) & 1u)));
    envia_c((char)('0' + (h & 1u)));
    envia("\r\n");

    static char lin[64];
    unsigned n = 0;
    for (;;) {
        if (g_dijo_fallo) {
            g_dijo_fallo = 0;
            envia("FALLO: DIAG-EN del L6230 bajo, sobrecorriente: PWM apagado por el freno del TIM1\r\n");
        }
        if (g_rx_r == g_rx_w) { __WFI(); continue; }
        const char ch = (char)g_rx[g_rx_r];
        g_rx_r = (g_rx_r + 1u) % RXN;
        if (ch == '\r' || ch == '\n') {
            lin[n] = 0;
            n = 0;
            ejecuta(lin);
        } else if (n + 1u < sizeof lin) {
            lin[n++] = ch;
        }
    }
}

/* El startup de ST llama a la inicialización de la libc antes de main(); sin
 * libc, no hay nada que inicializar */
void __libc_init_array(void) { }
