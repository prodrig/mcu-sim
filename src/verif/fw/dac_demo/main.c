/* ===========================================================================
 * main.c — Firmware de demostración del DAC, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * Genera una rampa de 16 pasos por DAC_OUT1 (PA4) y comprueba de paso el
 * registro DUAL, que es lo único del bloque que no pertenece a un canal sino a
 * la pareja: una sola escritura carga los dos a la vez.
 *
 * Los dos pines de salida se ponen en MODO ANALÓGICO. Es lo que aparta el
 * buffer del pad para que el amplificador del DAC gobierne el nodo él solo; si
 * se olvidara, las dos etapas de salida pelearían, igual que en la placa.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define N_PASOS   16u
#define VREF_MV   3300u

volatile struct {
    volatile uint32_t done;    /* 1 cuando el firmware ha terminado          */
    volatile uint32_t dor;     /* DOR1 al terminar la rampa                  */
    volatile uint32_t pasos;   /* pasos de rampa generados                   */
    volatile uint32_t mv;      /* DOR1 final pasado a milivoltios            */
    volatile uint32_t dual;    /* 1 si el registro dual cargó los dos canales */
    volatile uint32_t pclk1;   /* PCLK1 con el que ha trabajado              */
} mbox __attribute__((section(".mailbox")));

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

/* Pin en MODO ANALÓGICO: es lo que aparta el pad del nodo para que la etapa de
 * salida del DAC lo gobierne sola. */
static void pin_analog(GPIO_TypeDef *g, uint32_t pin)
{
    g->MODER |= (3u << (2 * pin));
    g->PUPDR &= ~(3u << (2 * pin));
}

static void espera(uint32_t n) { for (volatile uint32_t i = 0; i < n; ++i) { } }

int main(void)
{
    mbox.done = 0u; mbox.dor = 0u; mbox.pasos = 0u;
    mbox.mv = 0u; mbox.dual = 0u; mbox.pclk1 = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;

    pin_analog(GPIOA, 4u);               /* PA4 = DAC_OUT1 */
    pin_analog(GPIOA, 5u);               /* PA5 = DAC_OUT2 */

    mbox.pclk1 = SystemCoreClock / 4u;   /* APB1 = HCLK/4 */

    /* Los dos canales, con el buffer DESCONECTADO (BOFF = 1) para que la
     * salida recorra toda la escala de raíl a raíl. Sin disparo: el dato pasa
     * a DOR en cuanto se escribe [IR, §12.14.1]. */
    DAC->CR = DAC_CR_EN1 | DAC_CR_BOFF1 | DAC_CR_EN2 | DAC_CR_BOFF2;
    espera(200u);

    /* --- Rampa de 16 pasos por el canal 1 ------------------------------- */
    uint32_t pasos = 0u;
    for (uint32_t i = 0; i < N_PASOS; ++i) {
        const uint32_t code = (i * 4095u) / (N_PASOS - 1u);
        DAC->DHR12R1 = code;
        espera(2000u);                   /* deja que la salida se estabilice */
        if (DAC->DOR1 == code) ++pasos;
    }
    mbox.pasos = pasos;
    mbox.dor   = DAC->DOR1;
    mbox.mv    = (DAC->DOR1 * VREF_MV) / 4095u;

    /* --- El registro DUAL: una escritura, los dos canales --------------- */
    /* Es lo único del bloque que no es de un canal sino de la pareja, y es lo
     * que permite actualizarlos en el mismo instante. */
    DAC->DHR12RD = 0x0111u | (0x0222u << 16);
    espera(2000u);
    mbox.dual = ((DAC->DOR1 == 0x0111u) && (DAC->DOR2 == 0x0222u)) ? 1u : 0u;

    /* Se deja la salida a fondo de escala, que es lo que mide el banco */
    DAC->DHR12R1 = 4095u;
    espera(2000u);
    mbox.dor = DAC->DOR1;
    mbox.mv  = (DAC->DOR1 * VREF_MV) / 4095u;

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
