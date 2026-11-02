/* ===========================================================================
 * main.c — UN BLDC CONMUTADO POR EL MCU, EN SEIS PASOS CON SUS HALL, EN
 *          SENOIDAL Y CON FOC: una NUCLEO-F446RE con una X-NUCLEO-IHM07M1 y
 *          un Nanotec DB42S01
 *
 * El firmware de la fase F7 de doc/analisis_cnc.md (§5.4): sin controlador
 * que lo conmute por dentro, el MCU decide en cada momento qué tensión lleva
 * cada fase del motor. Es el de placas/nucleo_ihm07m1_db42.xml.
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
 * LA FOC (M 2), el control de campo orientado: lo que se manda es la
 * CORRIENTE, y en los ejes del rotor. Cada periodo del PWM:
 *
 *   1. LA CORRIENTE DE LAS TRES FASES, de los tres shunts de la IHM07M1 y sus
 *      amplificadores: i = (offset - x)·3,3 / 4095 / (1,528·0,33), A por
 *      cuenta del ADC; el offset, medido al arrancar con el puente apagado.
 *      Un shunt solo ve su fase mientras conduce el transistor de abajo: con
 *      el PWM centrado, en el pico del contador, cuando los tres están abajo.
 *      Ahí lo pide el TIM1: el canal 4, en PWM 2 con CCR4 = ARR - 30, sube
 *      OC4REF 0,36 us antes del pico, y OC4REF es su TRGO (MMS = 111), el
 *      disparo de los grupos inyectados de los tres ADC (JEXTSEL = 0001). Cada
 *      ADC convierte una fase -el 1 PA0, el 2 PC1, el 3 PC0-, los tres a la
 *      vez, en 0,71 us. El ciclo, como mucho el 94 %: abajo, 3 us.
 *   2. CLARKE Y PARK: las tres corrientes, a un vector (alfa, beta), y ese
 *      vector, a los ejes del rotor: d, a lo largo del imán, y q, en fase con
 *      la f.e.m. -90° por delante del imán-. El ángulo, el de los Hall
 *      interpolado, como en senoidal.
 *   3. DOS PI, uno por eje: id a cero, iq a lo que se pide. Su salida, la
 *      tensión en d y en q, hasta lo que da el bus.
 *   4. PARK Y CLARKE AL REVÉS, y la inyección del mínimo y el máximo -lo
 *      mismo que el vector espacial-: cada fase, su ciclo.
 *
 * Con el lazo de velocidad (R), el PI de cada milisegundo da iq en vez del
 * ciclo, con su signo: para cambiar de sentido no hace falta parar. Con I,
 * iq directamente: el par. La tensión del bus no se mide -el modelo de la
 * IHM07M1 no lleva el divisor de VIN-: se da por 24 V.
 *
 * La interrupción de los ADC, con lo que llama, va en la RAM, y la raíz con
 * VSQRT. En el modelo tarda unos 30 us de los 50 del periodo: cuenta unas
 * tres veces los ciclos del silicio (P-02 de doc/todo.md). `?` dice la peor.
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
 *     M 0|1|2    seis pasos (0), senoidal (1) o FOC (2)
 *     I A        en FOC: esa iq, sin lazo de velocidad; negativa, hacia atrás
 *     S          para: los EN abajo
 *     P ms       espera
 *     ?          rpm, ciclo -en FOC, id e iq-, Hall y fallos
 *     C          en FOC: las corrientes de las tres fases, las de la última
 *                medida
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
static volatile int g_modo;                /* 0 seis pasos, 1 senoidal, 2 FOC */
#define g_seno (g_modo != 0)                  /* las tres fases con PWM */

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

/* El modo de salida de cada canal del TIM1: PWM 1 (110) o forzado a bajo (100).
 * El canal 4, siempre en PWM 2: su OC4REF es el disparo de los ADC */
static void pon_modos(int pwm_u, int pwm_v, int pwm_w)
{
    const uint32_t m1 = (pwm_u ? 6u : 4u) << TIM_CCMR1_OC1M_Pos | TIM_CCMR1_OC1PE |
                        (pwm_v ? 6u : 4u) << TIM_CCMR1_OC2M_Pos | TIM_CCMR1_OC2PE;
    const uint32_t m2 = (pwm_w ? 6u : 4u) << TIM_CCMR2_OC3M_Pos | TIM_CCMR2_OC3PE |
                        7u << TIM_CCMR2_OC4M_Pos;
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
/* Lo que corre en cada periodo del PWM, en la RAM: desde la Flash, con sus
 * estados de espera, la FOC no cabe en los 50 us */
#define EN_RAM __attribute__((section(".data.ram"), long_call, noinline))
EN_RAM static float seno(float x)                 /* x en radianes, cualquiera */
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
EN_RAM static float angulo_e(void)
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
    if (!g_marcha || g_modo != 1) return;
    const float th = (angulo_e() + 30.0f) * PI_F / 180.0f;
    const float m = (float)g_dir * g_ciclo;
    const float a = 0.5f * (float)PWM_ARR;
    TIM1->CCR1 = (uint32_t)(a + a * m * seno(th));
    TIM1->CCR2 = (uint32_t)(a + a * m * seno(th - 2.0943951f));
    TIM1->CCR3 = (uint32_t)(a + a * m * seno(th - 4.1887902f));
}


/* --- La FOC ----------------------------------------------------------------- */
static volatile uint32_t g_ms;
#define VBUS     24.0f                     /* V: no se mide */
#define CICLO_FOC 0.94f                    /* abajo, al menos 3 us: para medir */
#define IQ_MAX   2.0f                      /* A: por debajo de los 2,8 del L6230 */
#define A_CUENTA (3.3f / 4095.0f / (1.528f * 0.33f))
#define RAIZ3    1.7320508f
/* Los PI de corriente, para unos 500 Hz con Lf = 2,9 mH y Rf = 1,75 ohm:
 * Kp = Lf·w, Ki = Rf·w */
static const float KP_I = 9.0f, KI_I = 5500.0f;   /* V/A y V/(A·s) */
static const float DT_PWM = 50e-6f;

static volatile float g_off[3];            /* las cuentas del ADC con i = 0 */
static volatile float g_i[3];              /* A, por fase: las de la última medida */
static volatile float g_id, g_iq, g_iq_obj;
static float g_int_d, g_int_q;

static float limita(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

static inline float raiz(float x)            /* VSQRT: 14 ciclos, sin libm */
{
    float r;
    __asm volatile("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
}

/* En el pico del contador, los tres ADC han medido: la FOC entera */
/* Lo que tarda, medido con el SysTick: la entrada, la salida y lo que se
 * lleva el bus no los cuenta el DWT, y aquí importan */
static volatile uint32_t g_isr_max;
EN_RAM void ADC_IRQHandler(void)
{
    const uint32_t t0 = SysTick->VAL;
    if (!(ADC1->SR & ADC_SR_JEOC)) return;
    while (!(ADC2->SR & ADC_SR_JEOC) || !(ADC3->SR & ADC_SR_JEOC)) { }
    const float xa = (float)ADC1->JDR1, xb = (float)ADC2->JDR1, xc = (float)ADC3->JDR1;
    ADC1->SR = ~ADC_SR_JEOC;
    ADC2->SR = ~ADC_SR_JEOC;
    ADC3->SR = ~ADC_SR_JEOC;
    const float ia = (g_off[0] - xa) * A_CUENTA;
    const float ib = (g_off[1] - xb) * A_CUENTA;
    const float ic = (g_off[2] - xc) * A_CUENTA;
    g_i[0] = ia; g_i[1] = ib; g_i[2] = ic;
    if (!g_marcha || g_modo != 2) return;
    /* Clarke, con las tres */
    const float al = (2.0f * ia - ib - ic) * (1.0f / 3.0f);
    const float be = (ib - ic) * (1.0f / RAIZ3);
    /* Park: el eje d, 150° por detrás del ángulo de los Hall -la f.e.m. de U
     * tiene su máximo a 60°, y el imán va 90° por detrás de ella- */
    const float th = (angulo_e() - 150.0f) * (PI_F / 180.0f);
    const float sn = seno(th), cs = seno(th + 0.5f * PI_F);
    const float id = al * cs + be * sn;
    const float iq = -al * sn + be * cs;
    g_id = id; g_iq = iq;
    /* Los dos PI, con su tope: la tensión que cabe con el bus */
    const float vmax = CICLO_FOC * VBUS / RAIZ3;
    const float ed = -id, eq = g_iq_obj - iq;
    const float nd = g_int_d + (KI_I * DT_PWM) * ed;
    const float nq = g_int_q + (KI_I * DT_PWM) * eq;
    float vd = KP_I * ed + nd, vq = KP_I * eq + nq;
    const float m2 = vd * vd + vq * vq;
    if (m2 > vmax * vmax) {                   /* saturado: a escala, sin acumular */
        const float f = vmax / raiz(m2);
        vd *= f; vq *= f;
    } else {
        g_int_d = nd; g_int_q = nq;
    }
    /* Park y Clarke al revés */
    const float va = vd * cs - vq * sn;
    const float vb = vd * sn + vq * cs;
    const float v0 = va;
    const float v1 = -0.5f * va + (0.5f * RAIZ3) * vb;
    const float v2 = -0.5f * va - (0.5f * RAIZ3) * vb;
    /* El mínimo y el máximo, al centro: cada fase, su ciclo */
    float mx = v0, mn = v0;
    if (v1 > mx) mx = v1; else if (v1 < mn) mn = v1;
    if (v2 > mx) mx = v2; else if (v2 < mn) mn = v2;
    const float c = 0.5f * (mx + mn);
    const float k = (float)PWM_ARR / VBUS, med = 0.5f * (float)PWM_ARR;
    const float tope = CICLO_FOC * (float)PWM_ARR;
    TIM1->CCR1 = (uint32_t)limita(med + (v0 - c) * k, 0.0f, tope);
    TIM1->CCR2 = (uint32_t)limita(med + (v1 - c) * k, 0.0f, tope);
    TIM1->CCR3 = (uint32_t)limita(med + (v2 - c) * k, 0.0f, tope);
    int32_t d = (int32_t)t0 - (int32_t)SysTick->VAL;
    if (d < 0) d += (int32_t)(HCLK / 1000u);
    if ((uint32_t)d > g_isr_max) g_isr_max = (uint32_t)d;
}

/* Los ADC: cada uno, una fase en su grupo inyectado. Primero, el offset de
 * cada una con el puente apagado: 64 conversiones por software */
static void adc_inicia(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN | RCC_APB2ENR_ADC3EN;
    (void)RCC->APB2ENR;
    GPIOA->MODER |= 3u << (2u * 0u);           /* PA0, analógica */
    GPIOC->MODER |= 3u << (2u * 0u) | 3u << (2u * 1u);   /* PC0 y PC1 */
    ADC123_COMMON->CCR = ADC_CCR_ADCPRE_0;     /* 84 / 4 = 21 MHz */
    ADC_TypeDef* const a[3] = {ADC1, ADC2, ADC3};
    static const uint32_t canal[3] = {0u, 11u, 10u};
    for (int k = 0; k < 3; ++k) {
        a[k]->CR1 = 0;
        a[k]->SMPR1 = 0;
        a[k]->SMPR2 = 0;                       /* 3 ciclos: 0,71 us por conversión */
        a[k]->JSQR = canal[k] << ADC_JSQR_JSQ4_Pos;   /* JL = 0: una, la de JSQ4 */
        a[k]->CR2 = ADC_CR2_ADON;
    }
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < 2u) __WFI();            /* la estabilización */
    float suma[3] = {0.0f, 0.0f, 0.0f};
    for (int n = 0; n < 64; ++n)
        for (int k = 0; k < 3; ++k) {
            a[k]->SR = 0;
            a[k]->CR2 |= ADC_CR2_JSWSTART;
            while (!(a[k]->SR & ADC_SR_JEOC)) { }
            suma[k] += (float)a[k]->JDR1;
        }
    for (int k = 0; k < 3; ++k) {
        g_off[k] = suma[k] / 64.0f;
        a[k]->SR = 0;
    }
    ADC1->CR1 = ADC_CR1_JEOCIE;
    NVIC_SetPriority(ADC_IRQn, 1);
    NVIC_EnableIRQ(ADC_IRQn);
}

/* Los disparos del TIM1, solo en FOC: TRGO en flanco de subida (JEXTSEL =
 * 0001). En los otros modos no se mide, y no cuesta */
static void adc_disparos(int si)
{
    ADC_TypeDef* const a[3] = {ADC1, ADC2, ADC3};
    for (int k = 0; k < 3; ++k)
        a[k]->CR2 = ADC_CR2_ADON | (si ? ADC_CR2_JEXTEN_0 | (1u << ADC_CR2_JEXTSEL_Pos) : 0u);
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
static volatile int g_lazo;                /* con lazo de velocidad */
static volatile float g_rpm_obj, g_integral;
static const float KP = 0.0004f, KI = 0.004f;   /* ciclo por rpm, y por rpm·s */
static const float KP_W = 0.0008f, KI_W = 0.02f;   /* en FOC: A por rpm, y por rpm·s */

void SysTick_Handler(void)
{
    ++g_ms;
    if (!g_marcha || !g_lazo) return;
    if (g_modo == 2) {                       /* en FOC, da iq: con signo */
        const float e = g_rpm_obj - rpm();
        const float i = g_integral + KI_W * e * 0.001f;
        const float q = KP_W * e + i;
        if (q > IQ_MAX) g_iq_obj = IQ_MAX;
        else if (q < -IQ_MAX) g_iq_obj = -IQ_MAX;
        else { g_iq_obj = q; g_integral = i; }
        return;
    }
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
    if (g_modo == 2) {
        g_int_d = g_int_q = 0.0f;
        TIM1->CCR1 = TIM1->CCR2 = TIM1->CCR3 = PWM_ARR / 2u;   /* tensión cero */
    }
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
    if (g_modo == 2) {
        envia(" id ");
        envia_f(g_id, 2);
        envia(" iq ");
        envia_f(g_iq, 2);
        envia(" A");
    } else {
        envia(" ciclo ");
        envia_f(g_ciclo * 100.0f, 1);
        envia("%");
    }
    envia(" hall ");
    const uint32_t h = hall();
    envia_c((char)('0' + ((h >> 2) & 1u)));
    envia_c((char)('0' + ((h >> 1) & 1u)));
    envia_c((char)('0' + (h & 1u)));
    envia(" fallos ");
    envia_u(g_fallos);
    envia(g_modo == 2 ? " foc" : (g_modo ? " senoidal" : " seis pasos"));
    envia(g_marcha ? " en marcha" : " parado");
    if (g_modo == 2) {                        /* lo que tarda la FOC, la peor vez */
        envia("; la FOC tarda ");
        envia_f((float)g_isr_max / (float)(HCLK / 1000000u), 1);
        envia(" us de 50");
    }
    envia("\r\n");
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
            if (g_modo == 2) {                 /* en FOC, sin parar al cambiar de sentido */
                if (!g_marcha) { g_integral = 0.0f; g_iq_obj = 0.0f; }
                g_lazo = 1;
                if (!g_marcha && !arranca(1)) return;
                break;
            }
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
        case 'I': {
            if (g_modo != 2) { envia("error: I, solo en FOC (M 2)\r\n"); return; }
            const float q = numero(l + 1);
            g_lazo = 0;
            g_iq_obj = limita(q, -IQ_MAX, IQ_MAX);
            if (!g_marcha && !arranca(1)) return;
            break;
        }
        case 'D': {
            const float d = numero(l + 1);
            if (g_modo == 2) { envia("error: en FOC, I o R\r\n"); return; }
            g_lazo = 0;
            if (d == 0.0f) { apaga(); pon_ciclo(0.0f); break; }
            pon_ciclo((d < 0.0f ? -d : d) / 100.0f);
            if (!arranca(d < 0.0f ? -1 : 1)) return;
            break;
        }
        case 'S': apaga(); pon_ciclo(0.0f); g_lazo = 0; g_iq_obj = 0.0f; break;
        case 'M': {
            apaga();
            g_lazo = 0;
            g_iq_obj = 0.0f;
            const float m = numero(l + 1);
            g_modo = m >= 2.0f ? 2 : (m != 0.0f ? 1 : 0);
            if (g_modo == 1) TIM1->DIER |= TIM_DIER_UIE;
            else TIM1->DIER &= ~TIM_DIER_UIE;
            adc_disparos(g_modo == 2);
            pon_ciclo(0.0f);
            break;
        }
        case 'P': espera((uint32_t)numero(l + 1)); break;
        case 'C':                              /* las tres corrientes medidas */
            envia("ia ");
            envia_f(g_i[0], 3);
            envia(" ib ");
            envia_f(g_i[1], 3);
            envia(" ic ");
            envia_f(g_i[2], 3);
            envia(" A\r\n");
            break;
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
    TIM1->CCR4 = PWM_ARR - 30u;               /* 0,36 us antes del pico */
    TIM1->CR2 = 7u << TIM_CR2_MMS_Pos;        /* TRGO = OC4REF: los ADC */
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

    /* Los ADC de la corriente, con su offset */
    adc_inicia();

    envia("\r\nseis pasos, senoidal y FOC (F7): DB42S01 en una X-NUCLEO-IHM07M1, PWM de 20 kHz, ");
    envia_u(PARES);
    envia(" pares de polos; offset de la corriente ");
    for (int k = 0; k < 3; ++k) { envia_f(g_off[k], 0); envia(k < 2 ? " " : ""); }
    envia(" cuentas; Hall ");
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
