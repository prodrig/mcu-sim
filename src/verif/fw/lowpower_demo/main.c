/* ===========================================================================
 * main.c — Firmware de demostración de los modos de bajo consumo, con CMSIS
 *
 * Compilado con la cabecera de dispositivo de ST (stm32f407xx.h) y el
 * CMSIS-Core oficiales, sin una sola línea adaptada al modelo: es el mismo
 * binario que se grabaría en una placa.
 *
 * Recorre los tres modos en el orden en que los recorre cualquiera que
 * necesite que su cacharro dure con una pila:
 *
 *   1. SLEEP. Se duerme con WFE y lo despierta un evento del EXTI. Antes de
 *      dormir, limpia los LPEN de lo que no usa: es la diferencia entre 39 mA
 *      y 15 mA, y no cuesta nada.
 *   2. STOP. SLEEPDEEP con PDDS = 0, regulador en bajo consumo y Flash
 *      dormida. Al volver, MIDE EL RELOJ: el sistema arranca con HSI a 16 MHz
 *      y el PLL está apagado. Quien no lo sepa, se pasa el resto del programa
 *      corriendo diez veces más despacio sin enterarse.
 *   3. STANDBY. Limpia WUF, habilita el pin WKUP y se apaga. De ahí no se
 *      "vuelve": se ARRANCA otra vez, por el vector de reset, con la SRAM
 *      vacía. Por eso el firmware guarda su cuenta de arranques en la BKPSRAM,
 *      que es la única memoria que sobrevive, y lo primero que hace al
 *      arrancar es mirar SBF para saber de dónde viene.
 *
 * El buzón en 0x2000 0000 (que se pierde en el Standby, y eso también se
 * comprueba) deja el resultado a la vista del banco de pruebas.
 * ===========================================================================*/
#include "stm32f407xx.h"

volatile struct {
    volatile uint32_t done;        /* 1 cuando el firmware ha terminado    */
    volatile uint32_t etapa;       /* hasta dónde llegó                    */
    volatile uint32_t hclk;        /* HCLK al arrancar                     */
    volatile uint32_t hclk_stop;   /* HCLK justo despues de salir de Stop  */
    volatile uint32_t desde_stby;  /* 1 si este arranque viene de Standby  */
    volatile uint32_t arranques;   /* cuenta guardada en la BKPSRAM        */
    volatile uint32_t marca;       /* marca en SRAM: se pierde en Standby  */
    volatile uint32_t lpen_antes;  /* AHB1LPENR antes de dormir            */
} mbox __attribute__((section(".mailbox")));

#define ETAPA_ARRANQUE 1u
#define ETAPA_SLEEP    2u
#define ETAPA_STOP     3u
#define ETAPA_STANDBY  4u
#define ETAPA_VUELTA   5u

#define MARCA 0xC0FFEE42u

/* La BKPSRAM es la única RAM que sobrevive al Standby [IR, §14.7]. */
#define BKP ((volatile uint32_t *)0x40024000u)

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

/* PA0 como línea EXTI0 en modo EVENTO por flanco de subida. Se usa el camino
 * de evento -no el de interrupción- porque un WFE se despierta con él sin
 * necesidad de manejador, y porque es el mismo pin que hace de WKUP. */
static void exti0_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    GPIOA->MODER &= ~GPIO_MODER_MODER0;              /* entrada */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0;      /* PA0 */
    EXTI->RTSR |= EXTI_RTSR_TR0;
    EXTI->EMR  |= EXTI_EMR_MR0;                      /* evento, no interrupcion */
    EXTI->PR    = EXTI_PR_PR0;
}

/* Dormirse de verdad con WFE: primero un SEV+WFE para VACIAR el registro de
 * evento (que puede venir armado de una excepción anterior) y luego el WFE que
 * de verdad duerme. Es el idiom canónico de ARM; sin él, el primer WFE vuelve
 * en el acto y el firmware no llega a dormirse nunca. */
static void dormir_wfe(void)
{
    __SEV();
    __WFE();
    __WFE();
}

int main(void)
{
    /* --- ¿De dónde venimos? ------------------------------------------------
     * Lo primero de todo, antes de tocar nada: mirar SBF. Es la única forma de
     * distinguir un arranque normal de una vuelta del Standby, porque los dos
     * empiezan en el vector de reset [IR, §14.5.3]. Y para poder leerlo hay que
     * encender antes el reloj del propio PWR. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_DBP;                       /* llave del dominio backup */
    RCC->AHB1ENR |= RCC_AHB1ENR_BKPSRAMEN;
    PWR->CSR |= PWR_CSR_BRE;                     /* regulador de backup */
    while ((PWR->CSR & PWR_CSR_BRR) == 0u) { }

    if (PWR->CSR & PWR_CSR_SBF) {
        BKP[0] += 1u;                            /* la BKPSRAM sí sobrevive */
        mbox.desde_stby = 1u;
        mbox.arranques  = BKP[0];
        mbox.marca      = 0u;                    /* la SRAM venía vacía */
        mbox.etapa      = ETAPA_VUELTA;
        PWR->CR |= PWR_CR_CSBF | PWR_CR_CWUF;    /* dejarlo limpio */
        mbox.done = 1u;
        for (;;) { __WFI(); }
    }

    clock_init();
    BKP[0] = 1u;
    mbox.arranques = BKP[0];
    mbox.hclk  = SystemCoreClock;
    mbox.marca = MARCA;                          /* esto se perderá */
    mbox.etapa = ETAPA_ARRANQUE;
    exti0_init();

    /* --- 1. SLEEP ----------------------------------------------------------
     * Antes de dormir, quitar el reloj en Sleep a lo que no hace falta. Los
     * LPENR existen exactamente para esto y su valor de reset es "todo
     * encendido", así que quien no los toque paga por periféricos que ni usa. */
    mbox.lpen_antes = RCC->AHB1LPENR;
    RCC->AHB1LPENR = RCC_AHB1LPENR_GPIOALPEN;    /* solo GPIOA */
    RCC->AHB2LPENR = 0u;
    RCC->APB1LPENR = RCC_APB1LPENR_PWRLPEN;      /* y el PWR, que hace falta */
    RCC->APB2LPENR = RCC_APB2LPENR_SYSCFGLPEN;
    mbox.etapa = ETAPA_SLEEP;
    dormir_wfe();                                /* <- el banco manda el evento */
    EXTI->PR = EXTI_PR_PR0;

    /* --- 2. STOP -----------------------------------------------------------
     * Regulador en bajo consumo y Flash dormida: es la configuración de Stop
     * que menos gasta. Al despertar, medir el reloj. */
    PWR->CR &= ~PWR_CR_PDDS;                     /* Stop, no Standby */
    PWR->CR |= PWR_CR_LPDS | PWR_CR_FPDS;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    mbox.etapa = ETAPA_STOP;
    dormir_wfe();                                /* <- y otro evento */
    SCB->SCR &= ~(uint32_t)SCB_SCR_SLEEPDEEP_Msk;
    EXTI->PR = EXTI_PR_PR0;
    SystemCoreClockUpdate();
    mbox.hclk_stop = SystemCoreClock;            /* 16 MHz: HSI. La trampa. */
    /* Y una copia en la BKPSRAM, porque lo que viene ahora se lleva la SRAM
     * por delante y el banco de pruebas tiene que poder leerlo despues. */
    BKP[1] = SystemCoreClock;
    clock_init();                                /* y hay que reprogramarlo */

    /* --- 3. STANDBY --------------------------------------------------------
     * La secuencia entera, en el orden que importa: habilitar el pin de
     * despertar, LIMPIAR WUF (si no, no se entra) y poner PDDS. De aquí no se
     * vuelve: el MCU se apaga y el siguiente arranque empieza por el reset. */
    mbox.etapa = ETAPA_STANDBY;
    mbox.done  = 1u;                             /* el banco ya puede mirar */
    BKP[2] = ETAPA_STANDBY;                      /* esto si sobrevivira */
    PWR->CSR |= PWR_CSR_EWUP;                    /* PA0 = WKUP */
    PWR->CR  |= PWR_CR_CWUF | PWR_CR_CSBF;
    PWR->CR  |= PWR_CR_PDDS;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    dormir_wfe();
    for (;;) { }                                 /* no se llega aquí */
}

/* El startup de ST llama a __libc_init_array; sin libc, se resuelve aquí. */
void __libc_init_array(void) { }
