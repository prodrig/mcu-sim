/* ===========================================================================
 * main.c — Firmware de demostración del bxCAN, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * Hace lo que hace un driver de CAN de verdad, y en el mismo orden:
 *
 *   1. Reloj a 168 MHz, con lo que PCLK1 queda en 42 MHz. De ahí sale el
 *      tiempo de bit, y el firmware lo calcula él mismo para publicarlo.
 *   2. PD0/PD1 a AF9. CAN_TX es una salida push-pull normal; el hilo en Y
 *      -dominante contra recesivo- está del otro lado del transceptor.
 *   3. LA TRAMPA CLÁSICA, y por eso está aquí: se enciende el reloj de CAN1
 *      AUNQUE el bus que se use fuera CAN2, porque los 28 bancos de filtros
 *      son de CAN1 y sin su reloj no hay manera de configurarlos.
 *   4. Secuencia de arranque: salir de SLEEP, entrar en inicialización
 *      (INRQ/INAK), programar BTR -que SOLO se deja escribir ahí-, configurar
 *      un banco de filtros con FINIT, y volver a marcha normal.
 *   5. Transmitir un marco y esperar a que alguien lo asienta; recibir los que
 *      lleguen por la FIFO 0 y liberarlos con RFOM0.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define PLAZO   4000000u        /* vueltas de espera antes de rendirse */

volatile struct {
    volatile uint32_t done;      /* 1 cuando el firmware ha terminado       */
    volatile uint32_t etapa;     /* hasta dónde llegó                       */
    volatile uint32_t enviados;  /* marcos transmitidos y asentidos         */
    volatile uint32_t recibidos; /* marcos recogidos de la FIFO 0           */
    volatile uint32_t id_rx;     /* identificador del primero que llegó     */
    volatile uint32_t dato_rx;   /* sus cuatro primeros bytes               */
    volatile uint32_t esr;       /* CAN_ESR al terminar                     */
    volatile uint32_t bitrate;   /* velocidad calculada por el firmware     */
} mbox __attribute__((section(".mailbox")));

#define ETAPA_RELOJ   1u
#define ETAPA_PINES   2u
#define ETAPA_INIT    3u
#define ETAPA_FILTRO  4u
#define ETAPA_MARCHA  5u

/* --- Tiempo de bit ------------------------------------------------------- */
/* PCLK1 = 42 MHz. Con BRP = 5 (divide por 6) el cuanto vale 1/7 us; con
 * 1 + TS1(13) + TS2(2) = 16 cuantos, el bit dura 16/7 us y salen 437,5 kbit/s,
 * que no es un valor de catalogo. Con BRP = 6 (divide por 7) el cuanto es
 * 1/6 us y 14 cuantos dan 2,333 us. La combinacion limpia para 500 kbit/s con
 * 42 MHz es BRP = 6, TS1 = 10, TS2 = 1: 42/7 = 6 MHz de cuanto y 12 cuantos
 * por bit -> 500 kbit/s exactos, con el punto de muestreo al 91,7 %. */
#define CAN_BRP   6u            /* campo BRP: divide por BRP+1 = 7          */
#define CAN_TS1   9u            /* campo TS1: dura TS1+1 = 10 cuantos       */
#define CAN_TS2   0u            /* campo TS2: dura TS2+1 = 1 cuanto         */
#define CAN_SJW   0u

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

/* Pin a AF9, push-pull y rapido. CAN_TX no tiene nada de particular como pin:
 * la naturaleza de colector abierto del bus esta en el transceptor. */
static void pin_af9(GPIO_TypeDef *g, uint32_t pin)
{
    g->MODER   = (g->MODER   & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->OTYPER &= ~(1u << pin);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->PUPDR  &= ~(3u << (2 * pin));
    if (pin < 8u) g->AFR[0] = (g->AFR[0] & ~(0xFu << (4 * pin))) |
                              (9u << (4 * pin));
    else          g->AFR[1] = (g->AFR[1] & ~(0xFu << (4 * (pin - 8u)))) |
                              (9u << (4 * (pin - 8u)));
}

int main(void)
{
    mbox.done = 0u; mbox.etapa = 0u; mbox.enviados = 0u; mbox.recibidos = 0u;
    mbox.id_rx = 0u; mbox.dato_rx = 0u; mbox.esr = 0u; mbox.bitrate = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    /* Los DOS relojes. El de CAN2 porque se va a usar; el de CAN1 porque los
     * filtros de CAN2 viven en CAN1 y sin el no se pueden tocar. Aqui se usa
     * CAN1, pero la linea queda para que se vea la dependencia. */
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN | RCC_APB1ENR_CAN2EN;
    mbox.etapa = ETAPA_RELOJ;

    pin_af9(GPIOD, 0u);                     /* PD0 CAN1_RX */
    pin_af9(GPIOD, 1u);                     /* PD1 CAN1_TX */
    mbox.etapa = ETAPA_PINES;

    /* --- Inicializacion ------------------------------------------------- */
    /* Salir de SLEEP y entrar en INIT. El bloque CONFIRMA con INAK: hay que
     * esperarlo, no basta con escribir el bit [IR, 12.12]. */
    CAN1->MCR = CAN_MCR_INRQ;
    {
        uint32_t i = 0;
        while (((CAN1->MSR & CAN_MSR_INAK) == 0u) && (i < PLAZO)) ++i;
        if (i >= PLAZO) { mbox.done = 1u; for (;;) { __WFI(); } }
    }
    /* BTR solo se deja escribir aqui. Es una proteccion del silicio: cambiar
     * el tiempo de bit en marcha desincronizaria a todo el bus. */
    CAN1->BTR = (CAN_BRP << CAN_BTR_BRP_Pos) | (CAN_TS1 << CAN_BTR_TS1_Pos) |
                (CAN_TS2 << CAN_BTR_TS2_Pos) | (CAN_SJW << CAN_BTR_SJW_Pos);
    mbox.etapa = ETAPA_INIT;
    /* El propio firmware publica la velocidad que le ha salido. */
    {
        const uint32_t pclk1 = SystemCoreClock / 4u;
        const uint32_t cuantos = 1u + (CAN_TS1 + 1u) + (CAN_TS2 + 1u);
        mbox.bitrate = pclk1 / ((CAN_BRP + 1u) * cuantos);
    }

    /* --- Un banco de filtros --------------------------------------------- */
    /* FINIT protege el banco mientras se toca: sin el, un marco a medio
     * filtrar veria los registros cambiando bajo sus pies. */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1u;                      /* desactivar el banco 0         */
    CAN1->FS1R |= 1u;                       /* escala de 32 bits             */
    CAN1->FM1R &= ~1u;                      /* modo identificador + mascara  */
    CAN1->FFA1R &= ~1u;                     /* -> FIFO 0                     */
    CAN1->sFilterRegister[0].FR1 = 0x320u << 21;   /* identificador 0x320    */
    CAN1->sFilterRegister[0].FR2 = 0x7F0u << 21;   /* mascara: 0x32x         */
    CAN1->FA1R |= 1u;                       /* activar                       */
    CAN1->FMR &= ~CAN_FMR_FINIT;
    mbox.etapa = ETAPA_FILTRO;

    /* --- A marcha normal -------------------------------------------------- */
    CAN1->MCR = 0u;                          /* fuera INRQ y fuera SLEEP     */
    {
        uint32_t i = 0;
        while (((CAN1->MSR & CAN_MSR_INAK) != 0u) && (i < PLAZO)) ++i;
        if (i >= PLAZO) { mbox.done = 1u; for (;;) { __WFI(); } }
    }
    mbox.etapa = ETAPA_MARCHA;

    /* --- Transmitir ------------------------------------------------------- */
    /* Se carga el buzon 0 y se pide el envio escribiendo TXRQ. A partir de esa
     * escritura el buzon es del hardware: TME0 baja y no vuelve a subir hasta
     * que el marco sale o se aborta. */
    CAN1->sTxMailBox[0].TDLR = 0x44332211u;
    CAN1->sTxMailBox[0].TDHR = 0u;
    CAN1->sTxMailBox[0].TDTR = 4u;                       /* DLC = 4          */
    CAN1->sTxMailBox[0].TIR  = (0x123u << 21) | CAN_TI0R_TXRQ;
    {
        uint32_t i = 0;
        while (((CAN1->TSR & CAN_TSR_RQCP0) == 0u) && (i < PLAZO)) ++i;
        /* TXOK0 quiere decir que el marco salio Y que ALGUIEN lo asintio. Sin
         * un segundo nodo en el bus no se pone nunca, por perfecto que sea el
         * firmware: es el fallo mas comun al montar el primer nodo. */
        if (CAN1->TSR & CAN_TSR_TXOK0) mbox.enviados = 1u;
        CAN1->TSR = CAN_TSR_RQCP0;                       /* w1c              */
    }

    /* --- Recibir ---------------------------------------------------------- */
    /* Se sondea FMP0 (mensajes pendientes) y se libera cada marco con RFOM0.
     * Sin liberarlos, la FIFO se llena a los tres y desborda. */
    {
        uint32_t n = 0, i = 0;
        while (n < 3u && i < PLAZO) {
            if ((CAN1->RF0R & CAN_RF0R_FMP0) != 0u) {
                const uint32_t rir = CAN1->sFIFOMailBox[0].RIR;
                const uint32_t rdl = CAN1->sFIFOMailBox[0].RDLR;
                if (n == 0u) {
                    mbox.id_rx  = (rir >> 21) & 0x7FFu;
                    mbox.dato_rx = rdl;
                }
                ++n;
                mbox.recibidos = n;
                CAN1->RF0R = CAN_RF0R_RFOM0;             /* liberar          */
            }
            ++i;
        }
    }

    /* Los tres bits de estado del nodo: aviso, pasivo y bus-off. Un driver de
     * verdad los mira antes de dar el bus por bueno. */
    mbox.esr = CAN1->ESR & 7u;
    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
