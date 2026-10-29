/* ===========================================================================
 * main.c — UN CONTROL NUMÉRICO PEQUEÑO PARA LA FRESADORA 3020: G-code por el
 *          VCP de una NUCLEO-F446RE con una CNC Shield V3
 *
 * El firmware propio de la fase F3 de doc/analisis_cnc.md (§8.2): lo que hace
 * falta para mover una fresadora de tres ejes con G-code, escrito para que
 * cada cosa que falle se vea en su código. Habla como un GRBL 1.1 -sus "ok",
 * su informe de estado con `?`, sus alarmas- para que Candle, UGS o CNCjs se
 * conecten a la máquina simulada por el PuenteSerie, pero NO es GRBL: es un
 * subconjunto, y lo dice en su saludo.
 *
 * LO QUE ENTIENDE, línea a línea por la USART2 (el VCP, 115200 8N1):
 *
 *     G0 G1          rápido y a avance (F en mm/min)
 *     G2 G3          arcos en el plano XY con I y J (y Z a la vez: hélice)
 *     G4 P           espera, en segundos
 *     G20 G21        pulgadas y mm
 *     G28            a la posición de casa (0, 0, 0 de máquina), en rápido
 *     G38.2          sondeo: hacia donde diga, hasta que la sonda toque
 *     G90 G91        absolutas e incrementales
 *     G92            pone las cotas de trabajo
 *     G17 G54 G94    los únicos que hay: se aceptan
 *     M3 M4 M5 S     el cabezal: el PWM de D11, de 0 a S1000
 *     M8 M9          la refrigeración (A3)
 *     M2 M30         fin de programa
 *     $H $X $$ $G $I el homing, quitar la alarma, ajustes, estado, versión
 *     ? ! ~ Ctrl-X   en cualquier momento: estado, retener, seguir, reset
 *
 * LOS PINES, los de GRBL 1.1 en una CNC Shield sobre el Arduino de la Nucleo
 * (doc/analisis_cnc.md §8.1):
 *
 *     STEP X, Y, Z   D2 PA10, D3 PB3, D4 PB5
 *     DIR X, Y, Z    D5 PB4, D6 PB10, D7 PA8
 *     ENABLE         D8 PA9 (activo bajo, el de los tres drivers)
 *     LÍMITE X, Y, Z D9 PC7, D10 PB6, D12 PA6 (con pull-up: pisado, bajo)
 *     CABEZAL        D11 PA7, TIM3_CH2 a 1 kHz; su sentido, D13 PA5 (LD2)
 *     ABORTAR        A0 PA0; RETENER A1 PA1; SEGUIR A2 PA4 (pull-up, bajo)
 *     REFRIGERACIÓN  A3 PB0
 *     SONDA          A5 PC0 (pull-up: tocando, bajo)
 *
 * EL MOVIMIENTO. Cada línea con movimiento es un BLOQUE: una recta o un arco
 * con su longitud, su avance máximo y su aceleración, que salen de los de cada
 * eje según cuánto le toque. Cada bloque empieza y acaba parado, con un
 * perfil trapezoidal: los arcos se recorren enteros a su avance, y las
 * esquinas de un cuadrado se paran. El bucle principal parte el bloque en
 * SEGMENTOS de 1 ms -los pasos que le tocan a cada eje en ese milisegundo- y
 * la interrupción del TIM6, a 10 kHz, los reparte con Bresenham: en la primera
 * vuelta de cada segmento pone DIR, y los pasos van en las 9 siguientes. El
 * pulso de STEP dura una vuelta, 100 us. El TIM6 solo corre mientras hay algo
 * que mover.
 *
 * LAS COORDENADAS DE MÁQUINA. Tras el homing ($H), la casa es (0, 0, 0): X e Y
 * en sus finales de abajo, Z arriba -como en GRBL, Z baja hacia negativo-. Los
 * finales de carrera paran la máquina en seco fuera del homing (límites
 * duros), y ABORTAR -el segundo contacto de la seta- también.
 * ===========================================================================*/
#include "stm32f446xx.h"
#include <stdint.h>

/* --- Los ajustes de la máquina ($$) ------------------------------------- */
#define NEJES 3
static float g_pasos_mm[NEJES] = {200.0f, 200.0f, 800.0f};  /* Tr8x8, Tr8x8, Tr8x2 a 1/8 */
static float g_vmax[NEJES]     = {20.0f, 20.0f, 8.0f};      /* mm/s */
static float g_acel[NEJES]     = {300.0f, 300.0f, 150.0f};  /* mm/s² */
static const float S_MAX = 1000.0f;                         /* S de PWM al 100 % */
static const float HOMING_RAPIDO = 10.0f, HOMING_LENTO = 1.0f, HOMING_SEPARA = 1.0f;

#define TICK_HZ     10000u
#define SEG_TICKS   10u           /* 1 ms */
#define SEG_S       0.001f

/* --- Los pines ----------------------------------------------------------- */
static void modo(GPIO_TypeDef* g, uint32_t n, uint32_t m)
{
    g->MODER = (g->MODER & ~(3u << (2u * n))) | (m << (2u * n));
}
static void pullup(GPIO_TypeDef* g, uint32_t n)
{
    g->PUPDR = (g->PUPDR & ~(3u << (2u * n))) | (1u << (2u * n));
}
static void af(GPIO_TypeDef* g, uint32_t n, uint32_t f)
{
    modo(g, n, 2u);
    if (n < 8u) g->AFR[0] = (g->AFR[0] & ~(15u << (4u * n))) | (f << (4u * n));
    else        g->AFR[1] = (g->AFR[1] & ~(15u << (4u * (n - 8u)))) | (f << (4u * (n - 8u)));
}
static inline void pon(GPIO_TypeDef* g, uint32_t n, int alto)
{
    g->BSRR = alto ? (1u << n) : (1u << (n + 16u));
}
static GPIO_TypeDef* const STEP_P[NEJES] = {GPIOA, GPIOB, GPIOB};
static const uint32_t STEP_N[NEJES] = {10u, 3u, 5u};
static GPIO_TypeDef* const DIR_P[NEJES] = {GPIOB, GPIOB, GPIOA};
static const uint32_t DIR_N[NEJES] = {4u, 10u, 8u};
static GPIO_TypeDef* const LIM_P[NEJES] = {GPIOC, GPIOB, GPIOA};
static const uint32_t LIM_N[NEJES] = {7u, 6u, 6u};
static int limite(int k) { return (LIM_P[k]->IDR & (1u << LIM_N[k])) == 0u; }
static int sonda(void) { return (GPIOC->IDR & 1u) == 0u; }
static int abortar(void) { return (GPIOA->IDR & 1u) == 0u; }

/* --- Matemáticas sin libm -------------------------------------------------- */
static float raiz(float x)
{
    float r;
    if (x <= 0.0f) return 0.0f;
    __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
}
static float absf(float x) { return x < 0.0f ? -x : x; }
#define PI_F 3.14159265358979f
/* seno en [-pi, pi], con un polinomio de grado 11 */
static float seno(float x)
{
    while (x > PI_F) x -= 2.0f * PI_F;
    while (x < -PI_F) x += 2.0f * PI_F;
    if (x > 0.5f * PI_F) x = PI_F - x;
    else if (x < -0.5f * PI_F) x = -PI_F - x;
    const float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f *
                (1.0f - x2 / 72.0f * (1.0f - x2 / 110.0f)))));
}
static float coseno(float x) { return seno(x + 0.5f * PI_F); }
/* atan2, con la serie de atan reducida a [0, 1] */
static float atan01(float t)
{
    /* atan(t) = 2·atan(t / (1 + sqrt(1 + t²))): t en [0, 0,42] */
    const float u = t / (1.0f + raiz(1.0f + t * t));
    const float u2 = u * u;
    return 2.0f * u * (1.0f - u2 * (1.0f / 3.0f - u2 * (1.0f / 5.0f - u2 * (1.0f / 7.0f -
                       u2 * (1.0f / 9.0f)))));
}
static float atan2f_(float y, float x)
{
    const float ax = absf(x), ay = absf(y);
    if (ax == 0.0f && ay == 0.0f) return 0.0f;
    float a = ay <= ax ? atan01(ay / ax) : 0.5f * PI_F - atan01(ax / ay);
    if (x < 0.0f) a = PI_F - a;
    return y < 0.0f ? -a : a;
}

/* --- La USART2: un anillo de recepción grande y otro de envío ------------- */
#define RXN 8192u
#define TXN 1024u
static volatile uint8_t g_rx[RXN];
static volatile uint32_t g_rx_w, g_rx_r;
static volatile uint8_t g_tx[TXN];
static volatile uint32_t g_tx_w, g_tx_r;
static volatile int g_pide_estado, g_retener, g_seguir, g_reset;

void USART2_IRQHandler(void)
{
    const uint32_t sr = USART2->SR;
    if (sr & USART_SR_RXNE) {
        const uint8_t c = (uint8_t)USART2->DR;
        /* Los de tiempo real no van al anillo */
        if (c == '?') g_pide_estado = 1;
        else if (c == '!') g_retener = 1;
        else if (c == '~') g_seguir = 1;
        else if (c == 0x18) g_reset = 1;
        else if (((g_rx_w + 1u) % RXN) != g_rx_r) {
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
static void envia_n(float v, int dec)
{
    char b[24];
    int i = 0;
    if (v < 0.0f) { envia_c('-'); v = -v; }
    float m = 1.0f;
    for (int k = 0; k < dec; ++k) m *= 10.0f;
    uint32_t x = (uint32_t)(v * m + 0.5f);
    uint32_t ent = x, fr = 0;
    if (dec > 0) { ent = x / (uint32_t)m; fr = x % (uint32_t)m; }
    do { b[i++] = (char)('0' + ent % 10u); ent /= 10u; } while (ent);
    while (i) envia_c(b[--i]);
    if (dec > 0) {
        envia_c('.');
        for (int k = dec - 1; k >= 0; --k) {
            uint32_t d = fr;
            for (int j = 0; j < k; ++j) d /= 10u;
            envia_c((char)('0' + d % 10u));
        }
    }
}
static void envia_entero(int32_t v) { envia_n((float)v, 0); }

/* --- Los segmentos: lo que la interrupción reparte ------------------------ */
typedef struct {
    int32_t pasos[NEJES];          /* con signo */
} Segmento;
#define SEGN 32u
static volatile Segmento g_seg[SEGN];
static volatile uint32_t g_seg_w, g_seg_r;

static volatile int32_t g_pos[NEJES];      /* la posición de máquina, en pasos */
static volatile uint32_t g_para;           /* parar al pisar: bits de límites (0-2) y sonda (3) */
static volatile int g_parado;              /* lo que paró: 1 + k, o 4 la sonda */
static volatile int g_limites_duros;       /* activos fuera del homing */
static volatile int g_alarma_isr;          /* un límite duro o ABORTAR, desde la interrupción */

/* El segmento en curso */
static uint32_t t_tick;
static int32_t s_n[NEJES], s_acc[NEJES], s_dir[NEJES];
static int s_activo;

static volatile uint32_t g_ms;              /* un reloj de milisegundos: el SysTick */
void SysTick_Handler(void) { ++g_ms; }

/* EL TIM6 SOLO CORRE CON ALGO QUE HACER: parado, la CPU duerme de verdad -y la
 * simulación no paga 10 000 interrupciones por segundo de máquina quieta- */
static uint32_t g_pulsos;                    /* los STEP que están arriba */
static inline void tim6_para(void) { TIM6->CR1 = 0; }
static inline void tim6_arranca(void) { if (!(TIM6->CR1 & TIM_CR1_CEN)) TIM6->CR1 = TIM_CR1_CEN; }

void TIM6_DAC_IRQHandler(void)
{
    TIM6->SR = 0;
    /* El pulso de la vuelta anterior, abajo */
    if (g_pulsos) {
        for (int k = 0; k < NEJES; ++k) if (g_pulsos & (1u << k)) pon(STEP_P[k], STEP_N[k], 0);
        g_pulsos = 0;
    }
    /* Parar en seco: lo que se pide (homing, sonda) o un límite duro */
    uint32_t pisado = 0;
    for (int k = 0; k < NEJES; ++k) if (limite(k)) pisado |= 1u << k;
    if (sonda()) pisado |= 8u;
    if (g_para & pisado) {
        g_parado = (pisado & g_para & 8u) ? 4 : 1 + __builtin_ctz(pisado & g_para);
        g_para = 0;
        s_activo = 0;
        g_seg_r = g_seg_w;
        return;
    }
    if (s_activo && ((g_limites_duros && pisado & 7u) || abortar())) {
        g_alarma_isr = abortar() ? 3 : 1;
        s_activo = 0;
        g_seg_r = g_seg_w;
        return;
    }
    if (!s_activo) {
        if (g_seg_r == g_seg_w) { tim6_para(); return; }
        /* Tras una parada en seco, nada hasta que el bucle principal limpie */
        if (g_parado || g_alarma_isr) { g_seg_r = g_seg_w; return; }
        /* Un segmento nuevo: DIR en esta vuelta, los pasos en las siguientes */
        const volatile Segmento* s = &g_seg[g_seg_r];
        for (int k = 0; k < NEJES; ++k) {
            const int32_t p = s->pasos[k];
            s_dir[k] = p < 0 ? -1 : 1;
            s_n[k] = p < 0 ? -p : p;
            s_acc[k] = 0;
            pon(DIR_P[k], DIR_N[k], p >= 0);
        }
        g_seg_r = (g_seg_r + 1u) % SEGN;
        s_activo = 1;
        t_tick = 0;
        return;
    }
    /* Bresenham sobre las SEG_TICKS - 1 vueltas del segmento */
    ++t_tick;
    for (int k = 0; k < NEJES; ++k) {
        s_acc[k] += s_n[k];
        if (s_acc[k] >= (int32_t)(SEG_TICKS - 1u)) {
            s_acc[k] -= (int32_t)(SEG_TICKS - 1u);
            pon(STEP_P[k], STEP_N[k], 1);
            g_pulsos |= 1u << k;
            g_pos[k] += s_dir[k];
        }
    }
    if (t_tick >= SEG_TICKS - 1u) s_activo = 0;
}

static int segmentos_libres(void) { return (int)((g_seg_r + SEGN - g_seg_w - 1u) % SEGN); }
static int segmentos_vacios(void) { return g_seg_r == g_seg_w && !s_activo; }

/* --- Los bloques ----------------------------------------------------------- */
typedef struct {
    int arco;                      /* 0 recta; 1 G2 (horario); -1 G3 */
    float ini[NEJES], fin[NEJES];  /* mm de máquina */
    float centro[2], radio, ang0, barrido;
    float largo, vmax, acel;
} Bloque;
#define BLQN 16u
static Bloque g_blq[BLQN];
static uint32_t g_blq_w, g_blq_r;
static int blq_libres(void) { return (int)((g_blq_r + BLQN - g_blq_w - 1u) % BLQN); }

/* El ejecutor: el bloque en curso, dónde va y a cuánto */
static int e_activo;
static float e_s, e_v;
static int32_t e_objetivo[NEJES];    /* los pasos ya mandados hasta ahora */

/* El estado */
enum { REPOSO, MARCHA, RETENIDO, CASA, ALARMA, SONDEO };
static int g_estado = REPOSO;
static int g_alarma;
static float g_wco[NEJES];           /* G92: trabajo = máquina - wco */
static int g_absolutas = 1, g_pulgadas = 0, g_modo = 0;  /* G0 */
static float g_avance = 600.0f;      /* mm/min */
static float g_s = 0.0f;
static int g_cabezal = 5, g_refrigera = 0;
static float g_ultimo[NEJES];        /* el destino del último bloque, en mm de máquina */

static float pos_mm(int k) { return (float)g_pos[k] / g_pasos_mm[k]; }

/* El punto del bloque a la distancia s del principio */
static void punto(const Bloque* b, float s, float* p)
{
    if (!b->arco) {
        const float f = b->largo > 0.0f ? s / b->largo : 1.0f;
        for (int k = 0; k < NEJES; ++k) p[k] = b->ini[k] + (b->fin[k] - b->ini[k]) * f;
        return;
    }
    const float f = b->largo > 0.0f ? s / b->largo : 1.0f;
    const float a = b->ang0 + b->barrido * f;
    p[0] = b->centro[0] + b->radio * coseno(a);
    p[1] = b->centro[1] + b->radio * seno(a);
    p[2] = b->ini[2] + (b->fin[2] - b->ini[2]) * f;
}

/* Parte el bloque en curso en segmentos de 1 ms mientras quepan */
static void genera(void)
{
    while (segmentos_libres() > 0) {
        if (!e_activo) {
            if (g_blq_r == g_blq_w) return;
            e_activo = 1;
            e_s = 0.0f;
            e_v = 0.0f;
            for (int k = 0; k < NEJES; ++k)
                e_objetivo[k] = (int32_t)(g_blq[g_blq_r].ini[k] * g_pasos_mm[k] +
                                          (g_blq[g_blq_r].ini[k] >= 0 ? 0.5f : -0.5f));
        }
        Bloque* b = &g_blq[g_blq_r];
        const float a = b->acel, queda = b->largo - e_s;
        float v;
        if (g_estado == RETENIDO) {
            v = e_v - a * SEG_S;
            if (v <= 0.0f) { e_v = 0.0f; return; }   /* parado, esperando ~ */
        } else {
            v = e_v + a * SEG_S;
            if (v > b->vmax) v = b->vmax;
            const float vfren = raiz(2.0f * a * queda);
            if (v > vfren) v = vfren;
            if (v < a * SEG_S) v = a * SEG_S;          /* que siempre avance */
        }
        float s = e_s + 0.5f * (e_v + v) * SEG_S;
        int fin = 0;
        if (s >= b->largo - 1e-6f) { s = b->largo; fin = 1; }
        e_s = s;
        e_v = v;
        float p[NEJES];
        if (fin) for (int k = 0; k < NEJES; ++k) p[k] = b->fin[k];
        else punto(b, s, p);
        Segmento seg;
        for (int k = 0; k < NEJES; ++k) {
            const float x = p[k] * g_pasos_mm[k];
            const int32_t o = (int32_t)(x + (x >= 0 ? 0.5f : -0.5f));
            int32_t d = o - e_objetivo[k];
            if (d > (int32_t)(SEG_TICKS - 1u)) d = (int32_t)(SEG_TICKS - 1u);
            if (d < -(int32_t)(SEG_TICKS - 1u)) d = -(int32_t)(SEG_TICKS - 1u);
            seg.pasos[k] = d;
            e_objetivo[k] += d;
        }
        g_seg[g_seg_w] = seg;
        g_seg_w = (g_seg_w + 1u) % SEGN;
        tim6_arranca();
        if (fin) {
            /* Lo que no cupo por el tope de pasos por segmento, en uno más */
            int32_t resto = 0;
            for (int k = 0; k < NEJES; ++k) {
                const float x = b->fin[k] * g_pasos_mm[k];
                const int32_t o = (int32_t)(x + (x >= 0 ? 0.5f : -0.5f));
                resto |= o - e_objetivo[k];
            }
            if (resto) { e_s = b->largo - 1e-3f; continue; }
            e_activo = 0;
            g_blq_r = (g_blq_r + 1u) % BLQN;
        }
    }
}

static int en_marcha(void) { return e_activo || g_blq_r != g_blq_w || !segmentos_vacios(); }

/* Los límites de un bloque según su dirección: el avance y la aceleración de
 * cada eje, divididos por cuánto le toca */
static void limites(Bloque* b, const float* u, float f_mm_s, int rapido)
{
    float v = rapido ? 1e9f : f_mm_s, a = 1e9f;
    for (int k = 0; k < NEJES; ++k) {
        const float c = absf(u[k]);
        if (c < 1e-6f) continue;
        if (g_vmax[k] / c < v) v = g_vmax[k] / c;
        if (g_acel[k] / c < a) a = g_acel[k] / c;
    }
    b->vmax = v;
    b->acel = a;
}

static void tiempo_real(void);
static int espera_hueco(void)
{
    while (blq_libres() == 0) {
        if (g_estado == ALARMA) return 0;
        genera();
        tiempo_real();
        __WFI();
    }
    return g_estado != ALARMA;
}

static void recta(const float* fin, int rapido, float f_mm_s)
{
    float d[NEJES], l2 = 0.0f;
    for (int k = 0; k < NEJES; ++k) { d[k] = fin[k] - g_ultimo[k]; l2 += d[k] * d[k]; }
    const float l = raiz(l2);
    if (l < 1e-5f) return;
    if (!espera_hueco()) return;
    Bloque* b = &g_blq[g_blq_w];
    b->arco = 0;
    for (int k = 0; k < NEJES; ++k) { b->ini[k] = g_ultimo[k]; b->fin[k] = fin[k]; d[k] /= l; }
    b->largo = l;
    limites(b, d, f_mm_s, rapido);
    g_blq_w = (g_blq_w + 1u) % BLQN;
    for (int k = 0; k < NEJES; ++k) g_ultimo[k] = fin[k];
}

static int arco(const float* fin, float i, float j, int horario, float f_mm_s)
{
    const float cx = g_ultimo[0] + i, cy = g_ultimo[1] + j;
    const float r = raiz(i * i + j * j);
    const float r2 = raiz((fin[0] - cx) * (fin[0] - cx) + (fin[1] - cy) * (fin[1] - cy));
    if (r < 1e-4f || absf(r - r2) > 0.005f + 0.001f * r) return 33;   /* GRBL: error 33 */
    const float a0 = atan2f_(-j, -i), a1 = atan2f_(fin[1] - cy, fin[0] - cx);
    float barrido = a1 - a0;
    if (horario) { if (barrido >= -1e-6f) barrido -= 2.0f * PI_F; }
    else         { if (barrido <= 1e-6f) barrido += 2.0f * PI_F; }
    if (!espera_hueco()) return 0;
    Bloque* b = &g_blq[g_blq_w];
    b->arco = horario ? 1 : -1;
    for (int k = 0; k < NEJES; ++k) { b->ini[k] = g_ultimo[k]; b->fin[k] = fin[k]; }
    b->centro[0] = cx;
    b->centro[1] = cy;
    b->radio = r;
    b->ang0 = a0;
    b->barrido = barrido;
    const float lxy = absf(barrido) * r, dz = fin[2] - g_ultimo[2];
    b->largo = raiz(lxy * lxy + dz * dz);
    /* El arco va por X e Y a la vez: el más lento de los dos manda */
    const float u[NEJES] = {1.0f, 1.0f, dz / (b->largo > 0 ? b->largo : 1.0f)};
    limites(b, u, f_mm_s, 0);
    g_blq_w = (g_blq_w + 1u) % BLQN;
    for (int k = 0; k < NEJES; ++k) g_ultimo[k] = fin[k];
    return 0;
}

/* --- El cabezal y la refrigeración ---------------------------------------- */
static void cabezal(void)
{
    uint32_t ccr = 0;
    if (g_cabezal != 5) {
        float s = g_s > S_MAX ? S_MAX : (g_s < 0 ? 0 : g_s);
        ccr = (uint32_t)(s / S_MAX * 1000.0f + 0.5f);
    }
    TIM3->CCR2 = ccr;
    pon(GPIOA, 5u, g_cabezal == 4);
    pon(GPIOB, 0u, g_refrigera);
}

/* --- Las alarmas --------------------------------------------------------- */
static void para_todo(void)
{
    __disable_irq();
    g_seg_r = g_seg_w;
    s_activo = 0;
    g_parado = 0;
    __enable_irq();
    e_activo = 0;
    g_blq_r = g_blq_w;
    for (int k = 0; k < NEJES; ++k) g_ultimo[k] = pos_mm(k);
}
static void alarma(int n)
{
    para_todo();
    g_estado = ALARMA;
    g_alarma = n;
    g_cabezal = 5;
    cabezal();
    envia("ALARM:");
    envia_entero(n);
    envia("\r\n");
}

/* --- El informe de estado ------------------------------------------------ */
static void estado(void)
{
    static const char* const nombre[] = {"Idle", "Run", "Hold", "Home", "Alarm", "Run"};
    int e = g_estado;
    if (e == REPOSO && en_marcha()) e = MARCHA;
    envia("<");
    envia(nombre[e]);
    envia("|MPos:");
    for (int k = 0; k < NEJES; ++k) {
        envia_n(pos_mm(k), 3);
        if (k < NEJES - 1) envia(",");
    }
    envia("|FS:");
    envia_n(g_estado == REPOSO && !en_marcha() ? 0.0f : e_v * 60.0f, 0);
    envia(",");
    envia_n(g_cabezal != 5 ? g_s : 0.0f, 0);
    envia("|Pn:");
    if (limite(0)) envia("X");
    if (limite(1)) envia("Y");
    if (limite(2)) envia("Z");
    if (sonda()) envia("P");
    envia("|WCO:");
    for (int k = 0; k < NEJES; ++k) {
        envia_n(g_wco[k], 3);
        if (k < NEJES - 1) envia(",");
    }
    envia(">\r\n");
}

/* Lo de tiempo real y la generación, mientras se espera */
static void tiempo_real(void)
{
    if (g_pide_estado) { g_pide_estado = 0; estado(); }
    if (g_retener) {
        g_retener = 0;
        if (g_estado == REPOSO && en_marcha()) g_estado = RETENIDO;
    }
    if (g_seguir) {
        g_seguir = 0;
        if (g_estado == RETENIDO) g_estado = REPOSO;
    }
    if (abortar() && g_estado != ALARMA && !g_alarma_isr) g_alarma_isr = 3;
    if (g_alarma_isr) {
        const int n = g_alarma_isr;
        alarma(n);
        g_alarma_isr = 0;
        envia(n == 3 ? "[MSG:Abortado: la seta o A0]\r\n" : "[MSG:Limite duro: un final pisado]\r\n");
    }
}

static void espera_parado(void)
{
    while (en_marcha() && g_estado != ALARMA) { genera(); tiempo_real(); __WFI(); }
}

/* Moverse en línea recta hasta que se pise lo de `mascara`, sin pasar de
 * `dist` mm en el eje k. Devuelve 1 si se pisó */
static int busca(int k, float dist, float v_mm_s, uint32_t mascara)
{
    float fin[NEJES];
    for (int j = 0; j < NEJES; ++j) { g_ultimo[j] = pos_mm(j); fin[j] = g_ultimo[j]; }
    fin[k] += dist;
    g_parado = 0;
    g_para = mascara;
    recta(fin, 0, v_mm_s);
    while (en_marcha() && !g_parado && g_estado != ALARMA) {
        genera();
        tiempo_real();
        __WFI();
    }
    g_para = 0;
    const int ok = g_parado != 0;
    para_todo();
    return ok;
}

static int homing(void)
{
    g_limites_duros = 0;
    g_estado = CASA;
    /* Z arriba, después X e Y abajo */
    static const int orden[NEJES] = {2, 0, 1};
    static const float sentido[NEJES] = {-1.0f, -1.0f, 1.0f};
    for (int n = 0; n < NEJES; ++n) {
        const int k = orden[n];
        const float s = sentido[k];
        if (limite(k)) busca(k, -s * 5.0f, HOMING_RAPIDO, 0);   /* pisado: primero, fuera */
        if (!busca(k, s * 400.0f, HOMING_RAPIDO, 1u << k)) goto mal;
        busca(k, -s * HOMING_SEPARA, HOMING_RAPIDO, 0);
        if (!busca(k, s * 2.0f * HOMING_SEPARA, HOMING_LENTO, 1u << k)) goto mal;
        busca(k, -s * HOMING_SEPARA, HOMING_RAPIDO, 0);
        __disable_irq();
        g_pos[k] = 0;
        __enable_irq();
        g_ultimo[k] = 0.0f;
    }
    g_estado = REPOSO;
    g_limites_duros = 1;
    return 1;
mal:
    g_limites_duros = 1;
    alarma(9);      /* GRBL: no encuentra el final */
    return 0;
}

/* --- El intérprete de G-code ----------------------------------------------- */
static int lee_numero(const char** p, float* v)
{
    const char* s = *p;
    int neg = 0, hay = 0;
    float x = 0.0f, d = 0.0f;
    while (*s == ' ') ++s;
    if (*s == '-') { neg = 1; ++s; } else if (*s == '+') ++s;
    while (*s >= '0' && *s <= '9') { x = x * 10.0f + (float)(*s - '0'); ++s; hay = 1; }
    if (*s == '.') {
        ++s;
        d = 0.1f;
        while (*s >= '0' && *s <= '9') { x += d * (float)(*s - '0'); d *= 0.1f; ++s; hay = 1; }
    }
    if (!hay) return 0;
    *v = neg ? -x : x;
    *p = s;
    return 1;
}

static void responde(int error)
{
    if (error) { envia("error:"); envia_entero(error); envia("\r\n"); }
    else envia("ok\r\n");
}

static void saludo(void)
{
    envia("\r\nGrbl 1.1f ['$' for help]\r\n");
    envia("[MSG:mcu-sim cnc: un control propio, no es GRBL]\r\n");
}

static int ajustes(void)
{
    static const char* const nombres[] = {"$100=", "$101=", "$102=", "$110=", "$111=", "$112=",
                                          "$120=", "$121=", "$122=", "$30="};
    const float v[] = {g_pasos_mm[0], g_pasos_mm[1], g_pasos_mm[2], g_vmax[0] * 60.0f,
                       g_vmax[1] * 60.0f, g_vmax[2] * 60.0f, g_acel[0], g_acel[1], g_acel[2],
                       S_MAX};
    envia("$22=1\r\n$23=0\r\n");
    for (unsigned k = 0; k < sizeof v / sizeof *v; ++k) {
        envia(nombres[k]);
        envia_n(v[k], 3);
        envia("\r\n");
    }
    return 0;
}

/* Una línea de G-code. Devuelve el código de error, o 0 */
static int linea(char* l)
{
    /* Mayúsculas y sin blancos ni comentarios */
    char* w = l;
    int par = 0;
    for (char* r = l; *r; ++r) {
        char c = *r;
        if (c == '(') { par = 1; continue; }
        if (c == ')') { par = 0; continue; }
        if (par) continue;
        if (c == ';') break;
        if (c == ' ' || c == '\t' || c == '\r') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        *w++ = c;
    }
    *w = 0;
    if (!l[0]) return 0;
    if (l[0] == '$') {
        if (l[1] == 'H' && !l[2]) {
            if (!homing()) return -1;          /* la alarma ya lo ha dicho */
            return 0;
        }
        if (l[1] == 'X' && !l[2]) {
            if (g_estado == ALARMA) {
                g_estado = REPOSO;
                envia("[MSG:Caution: Unlocked]\r\n");
            }
            return 0;
        }
        if (!l[1] || (l[1] == '$' && !l[2])) {
            if (l[1]) return ajustes();
            envia("[HLP:$$ $# $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H ~ ! ? ctrl-x]\r\n");
            return 0;
        }
        if (l[1] == 'G' && !l[2]) {
            envia(g_modo == 0 ? "[GC:G0" : g_modo == 1 ? "[GC:G1" : g_modo == 2 ? "[GC:G2" : "[GC:G3");
            envia(" G54 G17 ");
            envia(g_pulgadas ? "G20" : "G21");
            envia(g_absolutas ? " G90" : " G91");
            envia(" G94 M");
            envia_entero(g_cabezal);
            envia(g_refrigera ? " M8" : " M9");
            envia(" T0 F");
            envia_n(g_avance, 0);
            envia(" S");
            envia_n(g_s, 0);
            envia("]\r\n");
            return 0;
        }
        if (l[1] == 'I' && !l[2]) { envia("[VER:1.1f.mcu-sim:]\r\n[OPT:,15,128]\r\n"); return 0; }
        if (l[1] == '#' && !l[2]) {
            envia("[G92:");
            for (int k = 0; k < NEJES; ++k) { envia_n(g_wco[k], 3); if (k < 2) envia(","); }
            envia("]\r\n");
            return 0;
        }
        return 3;       /* GRBL: orden $ desconocida */
    }
    if (g_estado == ALARMA) return 9;   /* GRBL: bloqueado por una alarma */

    /* Las palabras */
    int g_mov = -1, g_otro = -1, m = -1, sondeo = 0;
    int hay[NEJES] = {0, 0, 0}, hay_ij = 0;
    float val[NEJES] = {0, 0, 0}, i = 0, j = 0, p = 0, f = -1.0f, s = -1.0f;
    const char* c = l;
    while (*c) {
        const char letra = *c++;
        float v;
        if (!lee_numero(&c, &v)) return 2;      /* GRBL: número que falta */
        switch (letra) {
            case 'G': {
                const int g = (int)(v * 10.0f + 0.5f);
                if (g == 0 || g == 10 || g == 20 || g == 30) g_mov = g / 10;
                else if (g == 382) { sondeo = 1; }
                else if (g == 40 || g == 280 || g == 920) g_otro = g;
                else if (g == 900) g_absolutas = 1;
                else if (g == 910) g_absolutas = 0;
                else if (g == 200) g_pulgadas = 1;
                else if (g == 210) g_pulgadas = 0;
                else if (g == 170 || g == 540 || g == 940 || g == 490 || g == 800) { /* nada */ }
                else return 20;                  /* GRBL: G no admitido */
                break;
            }
            case 'M': m = (int)(v + 0.5f); break;
            case 'X': hay[0] = 1; val[0] = v; break;
            case 'Y': hay[1] = 1; val[1] = v; break;
            case 'Z': hay[2] = 1; val[2] = v; break;
            case 'I': i = v; hay_ij = 1; break;
            case 'J': j = v; hay_ij = 1; break;
            case 'P': p = v; break;
            case 'F': f = v; break;
            case 'S': s = v; break;
            case 'N': break;
            default: return 20;
        }
    }
    const float esc = g_pulgadas ? 25.4f : 1.0f;
    if (f > 0) g_avance = f * esc;
    if (s >= 0) { g_s = s; cabezal(); }
    if (m >= 0) {
        if (m == 3 || m == 4 || m == 5) {
            espera_parado();
            g_cabezal = m;
            cabezal();
        } else if (m == 8 || m == 9) {
            espera_parado();
            g_refrigera = m == 8;
            cabezal();
        } else if (m == 2 || m == 30) {
            espera_parado();
            g_cabezal = 5;
            g_refrigera = 0;
            cabezal();
        } else if (m != 0 && m != 1) {
            return 20;
        }
    }
    if (g_mov >= 0) g_modo = g_mov;
    /* El destino, en mm de máquina */
    float fin[NEJES];
    const int mueve = hay[0] || hay[1] || hay[2];
    for (int k = 0; k < NEJES; ++k) {
        if (!hay[k]) fin[k] = g_ultimo[k];
        else if (g_absolutas) fin[k] = val[k] * esc + g_wco[k];
        else fin[k] = g_ultimo[k] + val[k] * esc;
    }
    if (g_otro == 40) {                   /* G4: espera */
        espera_parado();
        const uint32_t t0 = g_ms, ms = (uint32_t)(p * 1000.0f + 0.5f);
        while (g_ms - t0 < ms) { tiempo_real(); __WFI(); }
        return 0;
    }
    if (g_otro == 920) {                  /* G92: lo de ahora es lo que se dice */
        for (int k = 0; k < NEJES; ++k)
            if (hay[k]) g_wco[k] = g_ultimo[k] - val[k] * esc;
        return 0;
    }
    if (g_otro == 280) {                  /* G28: a casa, en rápido */
        if (mueve) recta(fin, 1, 0);
        const float casa[NEJES] = {0, 0, 0};
        float z[NEJES] = {g_ultimo[0], g_ultimo[1], 0.0f};
        recta(z, 1, 0);                   /* primero Z arriba */
        recta(casa, 1, 0);
        return 0;
    }
    if (sondeo) {                         /* G38.2: hasta que toque */
        if (!mueve) return 26;
        if (sonda()) { alarma(4); return -1; }   /* GRBL: la sonda ya tocaba */
        espera_parado();
        int k = hay[2] ? 2 : hay[0] ? 0 : 1;
        g_estado = SONDEO;
        const float v = g_avance / 60.0f;
        const int ok = busca(k, fin[k] - pos_mm(k), v, 8u);
        g_estado = REPOSO;
        envia("[PRB:");
        for (int n = 0; n < NEJES; ++n) { envia_n(pos_mm(n), 3); if (n < 2) envia(","); }
        envia(ok ? ":1]\r\n" : ":0]\r\n");
        if (!ok) { alarma(5); return -1; }
        return 0;
    }
    if (!mueve) return 0;
    if (g_modo == 0) recta(fin, 1, 0);
    else if (g_modo == 1) {
        if (g_avance <= 0) return 22;     /* GRBL: sin avance */
        recta(fin, 0, g_avance / 60.0f);
    } else {
        if (!hay_ij) return 26;
        const int e = arco(fin, i * esc, j * esc, g_modo == 2, g_avance / 60.0f);
        if (e) return e;
    }
    return 0;
}

/* --- El reloj: 84 MHz del PLL con el HSI ----------------------------------
 * Con los 16 MHz del HSI, la interrupción de los pasos -20 000 veces por
 * segundo, entonces- se comía la CPU, y la USART perdía bytes. Con el PLL a 84 MHz: HSI
 * entre 16 es 1 MHz, por 336 son 336, entre 4 son 84. APB1 a 42 MHz (sus
 * temporizadores, a 84); la Flash con 2 esperas y su caché, la ART */
#define HCLK   84000000u
#define PCLK1  42000000u
#define TIMCLK 84000000u
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

/* --- El arranque --------------------------------------------------------- */
int main(void)
{
    reloj();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM6EN;
    (void)RCC->APB1ENR;

    /* Los drivers, deshabilitados mientras se configura todo */
    pon(GPIOA, 9u, 1);
    modo(GPIOA, 9u, 1u);
    for (int k = 0; k < NEJES; ++k) {
        pon(STEP_P[k], STEP_N[k], 0);
        modo(STEP_P[k], STEP_N[k], 1u);
        modo(DIR_P[k], DIR_N[k], 1u);
        modo(LIM_P[k], LIM_N[k], 0u);
        pullup(LIM_P[k], LIM_N[k]);
    }
    /* Abortar, retener, seguir y la sonda, con pull-up */
    pullup(GPIOA, 0u);
    pullup(GPIOA, 1u);
    pullup(GPIOA, 4u);
    pullup(GPIOC, 0u);
    /* Sentido del cabezal y refrigeración */
    modo(GPIOA, 5u, 1u);
    modo(GPIOB, 0u, 1u);

    /* El PWM del cabezal: TIM3_CH2 en PA7, a 1 kHz */
    af(GPIOA, 7u, 2u);
    TIM3->PSC = TIMCLK / 1000000u - 1u;
    TIM3->ARR = 999;
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM3->CCER = TIM_CCER_CC2E;
    TIM3->CCR2 = 0;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    /* La USART2 del VCP: PA2 y PA3, AF7, 115200 */
    af(GPIOA, 2u, 7u);
    af(GPIOA, 3u, 7u);
    USART2->BRR = (PCLK1 + 57600u) / 115200u;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, 2);
    NVIC_EnableIRQ(USART2_IRQn);

    /* Los pasos: TIM6 a TICK_HZ, 10 kHz */
    TIM6->PSC = 0;
    TIM6->ARR = TIMCLK / TICK_HZ - 1u;
    TIM6->DIER = TIM_DIER_UIE;
    /* El SysTick, a 1 kHz: el reloj de milisegundos */
    SysTick->LOAD = HCLK / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    NVIC_SetPriority(TIM6_DAC_IRQn, 1);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    /* Los drivers, habilitados (EN activo bajo) */
    pon(GPIOA, 9u, 0);
    g_limites_duros = 1;
    saludo();

    static char lin[128];
    unsigned n = 0;
    int largo = 0;
    for (;;) {
        genera();
        tiempo_real();
        if (g_reset) {
            g_reset = 0;
            para_todo();
            if (g_estado == ALARMA) g_estado = ALARMA;
            else g_estado = REPOSO;
            g_cabezal = 5;
            cabezal();
            g_rx_r = g_rx_w;
            n = 0;
            saludo();
            continue;
        }
        /* Una línea entera, si hay sitio para su bloque */
        if (blq_libres() > 0 && g_rx_r != g_rx_w) {
            const char c = (char)g_rx[g_rx_r];
            g_rx_r = (g_rx_r + 1u) % RXN;
            if (c == '\n' || c == '\r') {
                if (n || largo) {
                    lin[n] = 0;
                    const int e = largo ? 14 : linea(lin);   /* GRBL: línea demasiado larga */
                    if (e >= 0) responde(e);
                    n = 0;
                    largo = 0;
                }
            } else if (n < sizeof lin - 1u) {
                lin[n++] = c;
            } else {
                largo = 1;
            }
            continue;
        }
        __WFI();
    }
}

/* El startup de ST llama a la inicialización de la libc antes de main(); sin
 * libc, no hay nada que inicializar */
void __libc_init_array(void) { }
