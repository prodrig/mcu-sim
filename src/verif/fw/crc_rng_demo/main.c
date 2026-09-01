/* ===========================================================================
 * main.c — Firmware de demostración del CRC y del RNG, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * Hace las dos cosas para las que existen estos dos bloques:
 *
 *   1. CRC. Calcula el CRC de un bloque de 256 palabras DOS VECES —una con el
 *      periférico y otra con una rutina en C— y compara. Que coincidan no es
 *      un detalle: es la prueba de que el bloque implementa el CRC-32/MPEG-2 y
 *      no el CRC-32 de zip, que es el error clásico. De paso mide con el
 *      SysTick lo que cuesta cada camino, que es la razón de que el bloque
 *      exista. Y usa CRC_IDR para lo que está: guardar un dato mientras se
 *      reinicia el cálculo.
 *
 *   2. RNG. Enciende el generador, cosecha 64 palabras sondeando DRDY y hace
 *      con ellas las dos comprobaciones de cordura que haría cualquiera:
 *      contar los bits a uno y mirar que no se repita ninguna.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define N_PALABRAS   256u
#define N_ALEATORIOS  64u
#define PLAZO     2000000u          /* vueltas de espera antes de rendirse */

volatile struct {
    volatile uint32_t done;       /* 1 cuando el firmware ha terminado       */
    volatile uint32_t crc_hw;     /* CRC del bloque, por el periférico       */
    volatile uint32_t crc_sw;     /* el mismo CRC, por la rutina en C        */
    volatile uint32_t mpeg;       /* CRC-32/MPEG-2 de "123456789"            */
    volatile uint32_t idr;        /* lo que CRC_IDR guardó entre dos cálculos*/
    volatile uint32_t ciclos_hw;  /* coste del camino por hardware           */
    volatile uint32_t ciclos_sw;  /* coste del camino por software           */
    volatile uint32_t n_rnd;      /* palabras cosechadas del RNG             */
    volatile uint32_t unos;       /* bits a uno entre todas ellas            */
    volatile uint32_t distintos;  /* cuántas son distintas de todas las demás*/
} mbox __attribute__((section(".mailbox")));

static uint32_t bloque[N_PALABRAS];
static uint32_t rnd[N_ALEATORIOS];

static void clock_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) { }
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    /* M = 8, N = 336, P = 2, Q = 7: SYSCLK = 168 MHz y PLL48CK = 48 MHz, que es
     * de donde cuelga el RNG —no de HCLK— [IR, §4.4 y §12.19]. */
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SystemCoreClockUpdate();
}

/* El SysTick como cronómetro de ciclos. Cuenta HACIA ABAJO desde su recarga,
 * así que el coste es la diferencia al revés. El DWT sería más cómodo, pero
 * pertenece a la unidad de traza y aquí basta con esto. */
static void crono_init(void)
{
    SysTick->LOAD = 0x00FFFFFFu;
    SysTick->VAL  = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}
static uint32_t crono(void) { return SysTick->VAL & 0x00FFFFFFu; }
static uint32_t crono_dif(uint32_t a, uint32_t b)   /* a antes, b después */
{
    return (a - b) & 0x00FFFFFFu;
}

/* La MISMA división polinómica, en C. Sobre bytes, con el más significativo
 * por delante, valor inicial 0xFFFFFFFF, sin inversión y sin XOR final: eso es
 * el CRC-32/MPEG-2. Si esta rutina y el periférico coinciden, el periférico
 * hace lo que dice el manual. */
static uint32_t crc32_sw(const uint8_t *d, uint32_t n, uint32_t crc)
{
    for (uint32_t i = 0; i < n; ++i) {
        crc ^= (uint32_t)d[i] << 24;
        for (uint32_t k = 0; k < 8u; ++k)
            crc = (crc & 0x80000000u) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
    }
    return crc;
}

int main(void)
{
    mbox.done = 0u; mbox.crc_hw = 0u; mbox.crc_sw = 0u; mbox.mpeg = 0u;
    mbox.idr = 0u; mbox.ciclos_hw = 0u; mbox.ciclos_sw = 0u;
    mbox.n_rnd = 0u; mbox.unos = 0u; mbox.distintos = 0u;

    clock_init();
    crono_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;

    /* --- El valor canónico, para que no quepa duda de qué CRC es este ---- */
    /* "123456789" da 0x0376E6E7 en cualquier implementación correcta del
     * MPEG-2. Es la piedra de toque de la rutina en C; el periférico se
     * compara luego contra ella. */
    static const uint8_t patron[9] = {'1','2','3','4','5','6','7','8','9'};
    mbox.mpeg = crc32_sw(patron, 9u, 0xFFFFFFFFu);

    /* --- Un bloque de 256 palabras, por los dos caminos ------------------ */
    for (uint32_t i = 0; i < N_PALABRAS; ++i)
        bloque[i] = 0x5A5A0000u + i * 2654435761u;    /* algo sin estructura */

    /* Por hardware. RESET deja CRC_DR en 0xFFFFFFFF, que es el valor inicial
     * del polinomio; no hay que escribirlo a mano [IR, §12.20.2]. */
    CRC->CR = CRC_CR_RESET;
    const uint32_t h0 = crono();
    for (uint32_t i = 0; i < N_PALABRAS; ++i) CRC->DR = bloque[i];
    const uint32_t hw = CRC->DR;
    const uint32_t h1 = crono();
    mbox.crc_hw = hw;
    mbox.ciclos_hw = crono_dif(h0, h1);

    /* Por software, sobre los MISMOS bits. El periférico come palabras de 32
     * bits y las digiere con el más significativo por delante, así que el
     * flujo equivalente de bytes es la palabra en BIG ENDIAN. */
    uint32_t sw = 0xFFFFFFFFu;
    const uint32_t s0 = crono();
    for (uint32_t i = 0; i < N_PALABRAS; ++i) {
        const uint32_t w = bloque[i];
        const uint8_t b[4] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16),
                               (uint8_t)(w >>  8), (uint8_t)(w) };
        sw = crc32_sw(b, 4u, sw);
    }
    const uint32_t s1 = crono();
    mbox.crc_sw = sw;
    mbox.ciclos_sw = crono_dif(s0, s1);

    /* --- CRC_IDR: el registro de recado ---------------------------------- */
    /* Es de ocho bits y NO participa en el cálculo, y sobre todo NO se lo
     * lleva CRC_CR.RESET. Para eso está: guardar algo entre dos cálculos. */
    CRC->IDR = 0xA5u;
    CRC->CR = CRC_CR_RESET;
    CRC->DR = 0x11223344u;
    mbox.idr = CRC->IDR & 0xFFu;

    /* --- El RNG ---------------------------------------------------------- */
    RNG->CR = RNG_CR_RNGEN;
    uint32_t n = 0u;
    for (uint32_t i = 0; i < PLAZO && n < N_ALEATORIOS; ++i) {
        const uint32_t sr = RNG->SR;
        /* Un driver de verdad mira los DOS errores antes que el dato: uno para
         * la generación y el otro dice que lo que salga no es de fiar. */
        if (sr & (RNG_SR_SEIS | RNG_SR_CEIS)) break;
        if (sr & RNG_SR_DRDY) rnd[n++] = RNG->DR;
    }
    RNG->CR = 0u;
    mbox.n_rnd = n;

    /* Las dos comprobaciones de cordura que haría cualquiera con una fuente de
     * ruido: el equilibrio de unos y ceros, y que no se repita ninguna. */
    uint32_t unos = 0u, distintos = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t w = rnd[i];
        while (w) { unos += w & 1u; w >>= 1; }
        uint32_t repetida = 0u;
        for (uint32_t k = 0; k < n; ++k)
            if (k != i && rnd[k] == rnd[i]) repetida = 1u;
        if (!repetida) ++distintos;
    }
    mbox.unos = unos;
    mbox.distintos = distintos;

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
