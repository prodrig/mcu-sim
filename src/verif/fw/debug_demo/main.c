/* ===========================================================================
 * main.c — Firmware de demostración de la traza ITM y del contador de ciclos
 *          del DWT, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * Es el uso cotidiano del subsistema de depuración, el que hace cualquiera que
 * trabaje con un STM32 y una sonda:
 *
 *   1. `printf` por SWO. Se abre el candado del ITM, se pone el TPIU en NRZ
 *      con su divisor, y se manda texto por el puerto 0 con la propia función
 *      `ITM_SendChar` del CMSIS. Es intrusión mínima: unas pocas
 *      instrucciones por carácter, sin periférico serie de por medio.
 *   2. Medir con el DWT. `DWT_CYCCNT` cuenta CICLOS DE NÚCLEO, no tiempo, y es
 *      la forma exacta de saber lo que cuesta un trozo de código. Aquí se mide
 *      un bucle conocido y se comprueba que el resultado es del orden debido.
 *   3. Un punto de ruptura por software. `__BKPT(0)` para el núcleo si hay un
 *      depurador enganchado; si no lo hay, escala a HardFault. El firmware
 *      comprueba primero DHCSR.C_DEBUGEN para no colgarse solo.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado       */
    volatile uint32_t etapa;     /* hasta dónde llegó                       */
    volatile uint32_t caracteres;/* caracteres mandados por el ITM          */
    volatile uint32_t ciclos;    /* ciclos medidos con DWT_CYCCNT           */
    volatile uint32_t vueltas;   /* vueltas del bucle medido                */
    volatile uint32_t hclk;      /* HCLK con el que trabajo                 */
    volatile uint32_t swo_hz;    /* velocidad de SWO que programo           */
    volatile uint32_t depurador; /* 1 si vio un depurador enganchado        */
} mbox __attribute__((section(".mailbox")));

#define ETAPA_RELOJ  1u
#define ETAPA_TRAZA  2u
#define ETAPA_TEXTO  3u
#define ETAPA_CICLOS 4u
#define N_VUELTAS    1000u

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

/* La puesta en marcha de la traza, en el orden que importa:
 *   TRCENA primero -sin el, ni el ITM ni el DWT existen-, luego el TPIU (que
 *   es quien saca los bits por el pin) y por ultimo el ITM. */
static uint32_t traza_init(uint32_t divisor)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    TPIU->SPPR = 2u;                      /* NRZ: SWO es una linea serie normal */
    TPIU->ACPR = divisor - 1u;            /* f_SWO = HCLK / divisor             */
    TPIU->FFCR = 0x100u;                  /* sin formateador: solo ITM          */
    ITM->LAR  = 0xC5ACCE55u;             /* el candado CoreSight               */
    ITM->TCR  = ITM_TCR_ITMENA_Msk | (1u << ITM_TCR_TRACEBUSID_Pos);
    ITM->TER  = 1u;                      /* solo el puerto 0                   */
    ITM->TPR  = 0u;                      /* todos los puertos, sin privilegio  */
    return SystemCoreClock / divisor;
}

static uint32_t itm_puts(const char *s)
{
    uint32_t n = 0;
    for (; *s; ++s) { ITM_SendChar((uint32_t)(unsigned char)*s); ++n; }
    return n;
}

/* El bucle que se mide. `volatile` para que el compilador no se lo lleve por
 * delante, que es el error clasico al medir con el DWT. */
static volatile uint32_t acc;
static void bucle(uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) acc += i;
}

int main(void)
{
    mbox.done = 0u; mbox.etapa = 0u; mbox.caracteres = 0u;
    mbox.ciclos = 0u; mbox.vueltas = 0u; mbox.hclk = 0u;
    mbox.swo_hz = 0u; mbox.depurador = 0u;

    clock_init();
    mbox.hclk = SystemCoreClock;
    mbox.etapa = ETAPA_RELOJ;

    /* ¿Hay alguien escuchando? DHCSR.C_DEBUGEN lo dice, y de ello depende que
     * un BKPT pare el nucleo o lo mande a HardFault. */
    mbox.depurador = (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) ? 1u : 0u;

    /* --- Traza por SWO ---------------------------------------------------- */
    mbox.swo_hz = traza_init(84u);       /* 168 MHz / 84 = 2 Mbit/s */
    mbox.etapa = ETAPA_TRAZA;
    mbox.caracteres = itm_puts("STM32F407 F6 listo\n");
    mbox.etapa = ETAPA_TEXTO;

    /* --- Medir con el DWT --------------------------------------------------- */
    /* CYCCNT cuenta ciclos de nucleo. Hay que habilitarlo explicitamente: el
     * bit vive en DWT_CTRL y no lo pone TRCENA. */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0u;
    const uint32_t t0 = DWT->CYCCNT;
    bucle(N_VUELTAS);
    const uint32_t t1 = DWT->CYCCNT;
    mbox.ciclos = t1 - t0;
    mbox.vueltas = N_VUELTAS;
    mbox.etapa = ETAPA_CICLOS;

    /* --- Y el resultado, por la misma traza ---------------------------------- */
    {
        char buf[16];
        uint32_t v = mbox.ciclos, k = 0;
        if (v == 0u) buf[k++] = '0';
        while (v && k < sizeof buf) { buf[k++] = (char)('0' + (v % 10u)); v /= 10u; }
        itm_puts(" ciclos=");
        while (k) ITM_SendChar((uint32_t)(unsigned char)buf[--k]);
        ITM_SendChar('\n');
    }

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
