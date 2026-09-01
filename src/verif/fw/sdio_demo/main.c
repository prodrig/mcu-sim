/* ===========================================================================
 * main.c — Firmware de demostración del SDIO, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * Hace lo que hace un driver de tarjeta SD de verdad, y en el mismo orden:
 *
 *   1. PLL con Q = 7 -> PLL48CK = 48 MHz, que es lo que alimenta al SDIO.
 *   2. Los nueve pines a AF12, en push-pull, muy rápidos y con pull-up: en la
 *      placa las líneas del bus SD llevan pull-up, y sin él la tarjeta no ve
 *      niveles altos limpios cuando nadie gobierna la línea.
 *   3. Arranque a menos de 400 kHz, como manda la norma, para la fase de
 *      identificación: CMD0, CMD8, ACMD41 hasta que la tarjeta deja de estar
 *      ocupada, CMD2 (CID), CMD3 (RCA), CMD9 (CSD), CMD7 (seleccionar).
 *   4. Bus a cuatro hilos con ACMD6 y subida del reloj a régimen normal.
 *   5. CMD17: un bloque de 512 bytes vaciando la FIFO a mano, y CMD24: un
 *      bloque de 512 bytes llenándola.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado        */
    volatile uint32_t etapa;     /* hasta dónde llegó (para depurar fallos)  */
    volatile uint32_t rca;       /* dirección relativa que dio la tarjeta    */
    volatile uint32_t cid0;      /* primera palabra del CID (respuesta larga)*/
    volatile uint32_t ck_hz;     /* SDIO_CK de régimen, calculado por él     */
    volatile uint32_t leidos;    /* bytes leídos con CMD17                   */
    volatile uint32_t suma;      /* suma de control de lo leído              */
    volatile uint32_t escritos;  /* bytes entregados con CMD24               */
    volatile uint32_t ancho;     /* 4 si el bus quedó a cuatro hilos         */
    volatile uint32_t sta;       /* SDIO_STA al final de la última operación */
} mbox __attribute__((section(".mailbox")));

#define ETAPA_RELOJ    1u
#define ETAPA_PINES    2u
#define ETAPA_CMD0     3u
#define ETAPA_CMD8     4u
#define ETAPA_ACMD41   5u
#define ETAPA_CMD2     6u
#define ETAPA_CMD3     7u
#define ETAPA_CMD9     8u
#define ETAPA_CMD7     9u
#define ETAPA_ANCHO   10u
#define ETAPA_LEER    11u
#define ETAPA_ESCRIBIR 12u

/* Banderas de STA que dan por acabado un comando */
#define STA_CMD_FIN  (SDIO_STA_CMDREND | SDIO_STA_CMDSENT | \
                      SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL)
#define STA_ERR_CMD  (SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL)
#define STA_ERR_DAT  (SDIO_STA_DTIMEOUT | SDIO_STA_DCRCFAIL | \
                      SDIO_STA_RXOVERR  | SDIO_STA_TXUNDERR)

#define SDIOCLK_HZ   48000000u
#define PLAZO        200000u          /* vueltas de espera antes de rendirse */

static uint8_t bloque[512];

/* Entre el final de un comando y el principio del siguiente la norma SD exige
 * NCC = 8 ciclos de SDIO_CK. Si no se respetan, el host empieza a gobernar la
 * línea CMD mientras la tarjeta todavía la suelta, y en la placa eso es una
 * pelea de etapas de salida sobre el mismo nodo. Un ciclo de SDIO_CK vale
 * (CLKDIV + 2) ciclos de SDIOCLK, y cada vuelta de este bucle sale por unos
 * pocos ciclos de CPU, así que se toma un margen ancho. */
static void ncc(void)
{
    const uint32_t div = (SDIO->CLKCR & SDIO_CLKCR_CLKDIV) + 2u;
    for (volatile uint32_t i = 0; i < div * 6u; ++i) { }
}

/* Vacía lo que haya quedado en la FIFO: una transferencia que acabó en error
 * deja palabras dentro, y la siguiente arrancaría con basura. */
static void fifo_vaciar(void)
{
    for (uint32_t i = 0; i < 64u && (SDIO->STA & SDIO_STA_RXDAVL); ++i)
        (void)SDIO->FIFO;
}

static void clock_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) { }
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    /* M = 8, N = 336, P = 2, Q = 7: VCO = 336 MHz, SYSCLK = 168 MHz y, lo que
     * aquí importa, PLL48CK = 336/7 = 48 MHz para el SDIO [IR, §4.4]. */
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SystemCoreClockUpdate();
}

/* Pin a función alternativa 12, push-pull, muy rápido y con pull-up. El bus SD
 * es de colector abierto en la fase de arranque por parte de la tarjeta, y los
 * pull-up son los que sostienen el nivel alto cuando nadie gobierna. */
static void pin_af12(GPIO_TypeDef *g, uint32_t pin)
{
    g->MODER   = (g->MODER   & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->OTYPER &= ~(1u << pin);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * pin))) | (3u << (2 * pin));
    g->PUPDR   = (g->PUPDR   & ~(3u << (2 * pin))) | (1u << (2 * pin));
    if (pin < 8u) g->AFR[0] = (g->AFR[0] & ~(0xFu << (4 * pin))) |
                              (12u << (4 * pin));
    else          g->AFR[1] = (g->AFR[1] & ~(0xFu << (4 * (pin - 8u)))) |
                              (12u << (4 * (pin - 8u)));
}

/* Manda un comando y espera a que la CPSM acabe. resp: 0 ninguna, 1 corta,
 * 3 larga. Devuelve las banderas de STA con las que terminó, o 0 si se agotó
 * el plazo del propio bucle. */
static uint32_t sdio_cmd(uint32_t idx, uint32_t arg, uint32_t resp)
{
    SDIO->ICR = 0xFFFFFFFFu;
    SDIO->ARG = arg;
    SDIO->CMD = idx | (resp << 6) | SDIO_CMD_CPSMEN;
    for (uint32_t i = 0; i < PLAZO; ++i) {
        const uint32_t s = SDIO->STA;
        if (s & STA_CMD_FIN) { SDIO->CMD = 0u; ncc(); return s; }
    }
    SDIO->CMD = 0u;
    ncc();
    return 0u;
}

/* Un comando de aplicación: CMD55 con la RCA y a continuación el ACMD. */
static uint32_t sdio_acmd(uint32_t idx, uint32_t arg, uint32_t resp, uint32_t rca)
{
    if (!(sdio_cmd(55u, rca << 16, 1u) & SDIO_STA_CMDREND)) return 0u;
    return sdio_cmd(idx, arg, resp);
}

int main(void)
{
    mbox.done = 0u; mbox.etapa = 0u; mbox.rca = 0u; mbox.cid0 = 0u;
    mbox.ck_hz = 0u; mbox.leidos = 0u; mbox.suma = 0u; mbox.escritos = 0u;
    mbox.ancho = 0u; mbox.sta = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
    mbox.etapa = ETAPA_RELOJ;

    /* --- Los nueve pines del bus [IR, §12.17.1] -------------------------- */
    pin_af12(GPIOC, 12u);                       /* SDIO_CK   */
    pin_af12(GPIOD,  2u);                       /* SDIO_CMD  */
    pin_af12(GPIOC,  8u);                       /* SDIO_D0   */
    pin_af12(GPIOC,  9u);                       /* SDIO_D1   */
    pin_af12(GPIOC, 10u);                       /* SDIO_D2   */
    pin_af12(GPIOC, 11u);                       /* SDIO_D3   */
    mbox.etapa = ETAPA_PINES;

    /* --- Arranque: menos de 400 kHz, un hilo ----------------------------- */
    /* SDIO_CK = SDIOCLK / (CLKDIV + 2). Con CLKDIV = 118 salen 400 kHz justos,
     * así que se toma 178 para quedarse holgadamente por debajo. */
    SDIO->POWER = SDIO_POWER_PWRCTRL;           /* 11: bloque encendido      */
    SDIO->CLKCR = 178u | SDIO_CLKCR_CLKEN;
    SDIO->DTIMER = 0x00FFFFFFu;
    for (volatile uint32_t i = 0; i < 20000u; ++i) { }   /* 74 ciclos y más  */

    /* CMD0: a reposo. No lleva respuesta, así que lo que se espera es CMDSENT */
    if (!(sdio_cmd(0u, 0u, 0u) & SDIO_STA_CMDSENT)) { mbox.done = 1u; for(;;){} }
    mbox.etapa = ETAPA_CMD0;

    /* CMD8: tensión de trabajo y patrón de comprobación. La tarjeta lo
     * devuelve tal cual si es v2.0 o posterior. */
    uint32_t s = sdio_cmd(8u, 0x1AAu, 1u);
    if ((s & SDIO_STA_CMDREND) && (SDIO->RESP1 & 0xFFFu) == 0x1AAu)
        mbox.etapa = ETAPA_CMD8;

    /* ACMD41 hasta que la tarjeta deja de estar ocupada (bit 31 del OCR) */
    uint32_t lista = 0u;
    for (uint32_t i = 0; i < 200u && !lista; ++i) {
        if (sdio_acmd(41u, 0x40FF8000u, 1u, 0u) & SDIO_STA_CMDREND)
            lista = (SDIO->RESP1 >> 31) & 1u;
    }
    if (!lista) { mbox.done = 1u; for (;;) { __WFI(); } }
    mbox.etapa = ETAPA_ACMD41;

    /* CMD2: el CID entero, 136 bits repartidos en RESP1..RESP4 */
    if (sdio_cmd(2u, 0u, 3u) & SDIO_STA_CMDREND) {
        mbox.cid0 = SDIO->RESP1;
        mbox.etapa = ETAPA_CMD2;
    }

    /* CMD3: la tarjeta publica su dirección relativa */
    uint32_t rca = 0u;
    if (sdio_cmd(3u, 0u, 1u) & SDIO_STA_CMDREND) {
        rca = SDIO->RESP1 >> 16;
        mbox.rca = rca;
        mbox.etapa = ETAPA_CMD3;
    }

    /* CMD9: el CSD, otra respuesta larga, esta vez dirigida */
    if (sdio_cmd(9u, rca << 16, 3u) & SDIO_STA_CMDREND) mbox.etapa = ETAPA_CMD9;

    /* CMD7: seleccionar la tarjeta. A partir de aquí atiende transferencias */
    if (sdio_cmd(7u, rca << 16, 1u) & SDIO_STA_CMDREND) mbox.etapa = ETAPA_CMD7;

    /* --- Cuatro hilos y reloj de régimen -------------------------------- */
    sdio_cmd(16u, 512u, 1u);                        /* longitud de bloque   */
    if (sdio_acmd(6u, 2u, 1u, rca) & SDIO_STA_CMDREND) {
        /* El ancho hay que ponerlo EN LOS DOS EXTREMOS: el ACMD6 convence a la
         * tarjeta, WIDBUS convence al host. */
        /* CLKDIV = 10 -> SDIO_CK = 4 MHz. El bloque llega hasta 48 MHz, pero
         * aqui las dos transferencias se hacen SONDEANDO la FIFO desde la CPU,
         * y por encima de unos pocos MHz el nucleo no da abasto: la FIFO de 32
         * palabras se desborda (RXOVERR) o se vacia (TXUNDERR). Para ir al
         * maximo hay que servirla por DMA, que es justamente lo que hace un
         * driver de verdad [IR, 12.17.2]. */
        SDIO->CLKCR = (SDIO->CLKCR & ~(SDIO_CLKCR_CLKDIV | SDIO_CLKCR_WIDBUS)) |
                      10u | SDIO_CLKCR_WIDBUS_0;    /* CLKDIV = 10, 4 hilos */
        mbox.ck_hz = SDIOCLK_HZ / (10u + 2u);
        mbox.ancho = 4u;
        mbox.etapa = ETAPA_ANCHO;
    }

    /* --- CMD17: leer un bloque ------------------------------------------ */
    /* La DPSM se arma ANTES del comando: la tarjeta empieza a soltar datos en
     * cuanto responde, y si la máquina de datos no está ya esperando se pierde
     * el bit de arranque [IR, §12.17.2]. */
    SDIO->ICR = 0xFFFFFFFFu;
    SDIO->DLEN = 512u;
    SDIO->DCTRL = (9u << SDIO_DCTRL_DBLOCKSIZE_Pos) | SDIO_DCTRL_DTDIR |
                  SDIO_DCTRL_DTEN;                  /* tarjeta -> host      */
    sdio_cmd(17u, 0u, 1u);

    /* Se vacía A RÁFAGAS de ocho palabras guiadas por RXFIFOHF, que es como lo
     * hace un driver de verdad: comprobar el estado palabra a palabra cuesta
     * dos accesos de bus por dato y la FIFO se desborda. */
    uint32_t n = 0u;
    for (uint32_t i = 0; i < PLAZO && n < 512u; ++i) {
        const uint32_t st = SDIO->STA;
        if (st & STA_ERR_DAT) break;
        if (st & SDIO_STA_RXFIFOHF) {
            for (uint32_t k = 0; k < 8u && n < 512u; ++k) {
                const uint32_t w = SDIO->FIFO;
                bloque[n + 0u] = (uint8_t)(w      );
                bloque[n + 1u] = (uint8_t)(w >>  8);
                bloque[n + 2u] = (uint8_t)(w >> 16);
                bloque[n + 3u] = (uint8_t)(w >> 24);
                n += 4u;
            }
        } else if ((st & SDIO_STA_DATAEND) && (st & SDIO_STA_RXDAVL)) {
            const uint32_t w = SDIO->FIFO;     /* la cola del bloque */
            bloque[n + 0u] = (uint8_t)(w      );
            bloque[n + 1u] = (uint8_t)(w >>  8);
            bloque[n + 2u] = (uint8_t)(w >> 16);
            bloque[n + 3u] = (uint8_t)(w >> 24);
            n += 4u;
        }
    }
    for (uint32_t i = 0; i < PLAZO; ++i)
        if (SDIO->STA & (SDIO_STA_DATAEND | STA_ERR_DAT)) break;
    mbox.sta = SDIO->STA;
    SDIO->DCTRL = 0u;
    fifo_vaciar();
    mbox.leidos = n;
    uint32_t suma = 0u;
    for (uint32_t i = 0; i < n; ++i) suma += bloque[i];
    mbox.suma = suma;
    if (n == 512u) mbox.etapa = ETAPA_LEER;

    /* --- CMD24: escribir un bloque -------------------------------------- */
    /* Aquí el orden es el contrario: primero el comando, y la DPSM se arranca
     * con la FIFO ya cebada, que es lo que evita el desbordamiento por abajo. */
    for (uint32_t i = 0; i < 512u; ++i) bloque[i] = (uint8_t)(0xA0u + (i & 0x1Fu));
    SDIO->ICR = 0xFFFFFFFFu;
    SDIO->DLEN = 512u;
    sdio_cmd(24u, 0u, 1u);

    uint32_t m = 0u;
    for (uint32_t k = 0; k < 8u; ++k) {             /* cebado de la FIFO    */
        SDIO->FIFO = (uint32_t)bloque[m] | ((uint32_t)bloque[m + 1u] << 8) |
                     ((uint32_t)bloque[m + 2u] << 16) |
                     ((uint32_t)bloque[m + 3u] << 24);
        m += 4u;
    }
    SDIO->DCTRL = (9u << SDIO_DCTRL_DBLOCKSIZE_Pos) | SDIO_DCTRL_DTEN;
    for (uint32_t i = 0; i < PLAZO && m < 512u; ++i) {
        const uint32_t st = SDIO->STA;
        if (st & STA_ERR_DAT) break;
        if (!(st & SDIO_STA_TXFIFOHE)) continue;
        for (uint32_t k = 0; k < 8u && m < 512u; ++k) {
            SDIO->FIFO = (uint32_t)bloque[m] | ((uint32_t)bloque[m + 1u] << 8) |
                         ((uint32_t)bloque[m + 2u] << 16) |
                         ((uint32_t)bloque[m + 3u] << 24);
            m += 4u;
        }
    }
    for (uint32_t i = 0; i < PLAZO; ++i)
        if (SDIO->STA & (SDIO_STA_DATAEND | STA_ERR_DAT)) break;
    mbox.sta = SDIO->STA;
    SDIO->DCTRL = 0u;
    mbox.escritos = m;
    if (m == 512u) mbox.etapa = ETAPA_ESCRIBIR;

    SDIO->POWER = 0u;
    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
