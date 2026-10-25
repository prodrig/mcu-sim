/* ===========================================================================
 * f3_can — el bxCAN del STM32F303VC entre dos STM32F3DISCOVERY (fase S6)
 *
 * Solo CMSIS. CAN_RX en PD0 y CAN_TX en PD1 (AF7), a 500 kbit/s: APB1 a
 * 36 MHz, preescalado 4 (9 MHz de cuanto), 1 + 13 + 4 = 18 cuantos, el punto
 * de muestreo al 77,8 %. Cada placa con su transceptor en el banco, y el hilo
 * con su terminador. El modo, en la primera palabra del buzon (0x2000_0000):
 *
 *   1  EL QUE PREGUNTA  cada milisegundo, el 0x123 con un contador; cada cinco,
 *                       ademas, un 0x155 (que el otro filtra) y un 0x345 (que
 *                       el otro lleva a su FIFO 1). Acepta solo el 0x124
 *   2  EL QUE CONTESTA  dos bancos de filtro: el 0 en mascara, solo el 0x123,
 *                       a la FIFO 0; el 1, 0x300-0x3FF, a la FIFO 1. A cada
 *                       0x123 con n contesta un 0x124 con n + 1
 *
 * Las dos FIFO por interrupcion: la 20 (USB_LP_CAN_RX0, la que comparte con
 * el USB) y la 21 (CAN_RX1).
 * ===========================================================================*/
#include "stm32f303xc.h"

typedef struct {
    volatile uint32_t modo, estado, enviados, buenas;
    volatile uint32_t rx0, rx1, malas, esr;
    volatile uint32_t ultimo_id, ultimo_dato, irq20, irq21;
    volatile uint32_t perdidos, sin_buzon;
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static volatile uint32_t ms;
void SysTick_Handler(void) { ++ms; }

static void reloj(void) {
    RCC->CR |= RCC_CR_HSEBYP | RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL | RCC_CFGR_PPRE1)) |
                RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL9 | RCC_CFGR_PPRE1_DIV2;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SysTick->LOAD = 72000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

/* Un filtro de 32 bits en modo mascara: id estandar y su mascara */
static void filtro(unsigned banco, uint32_t id, uint32_t mascara, unsigned fifo) {
    CAN->FA1R &= ~(1u << banco);
    CAN->FS1R |= 1u << banco;                 /* 32 bits */
    CAN->FM1R &= ~(1u << banco);              /* mascara */
    if (fifo) CAN->FFA1R |= 1u << banco; else CAN->FFA1R &= ~(1u << banco);
    CAN->sFilterRegister[banco].FR1 = id << 21;
    CAN->sFilterRegister[banco].FR2 = (mascara << 21) | (1u << 2);   /* y que sea estandar */
    CAN->FA1R |= 1u << banco;
}

static void can_inicia(int contesta) {
    RCC->AHBENR |= RCC_AHBENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_CANEN;
    /* PD0 (RX) y PD1 (TX) en AF7 */
    GPIOD->MODER = (GPIOD->MODER & ~0xFu) | 0xAu;
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~0xFFu) | 0x77u;
    /* A inicializacion */
    CAN->MCR = (CAN->MCR & ~CAN_MCR_SLEEP) | CAN_MCR_INRQ;
    while (!(CAN->MSR & CAN_MSR_INAK)) {}
    CAN->MCR |= CAN_MCR_ABOM | CAN_MCR_TXFP;
    CAN->BTR = (3u << CAN_BTR_BRP_Pos) | (12u << CAN_BTR_TS1_Pos) | (3u << CAN_BTR_TS2_Pos) |
               (0u << CAN_BTR_SJW_Pos);
    /* Los filtros */
    CAN->FMR |= CAN_FMR_FINIT;
    if (contesta) {
        filtro(0, 0x123, 0x7FF, 0);
        filtro(1, 0x300, 0x700, 1);
    } else {
        filtro(0, 0x124, 0x7FF, 0);
    }
    CAN->FMR &= ~CAN_FMR_FINIT;
    CAN->IER = CAN_IER_FMPIE0 | CAN_IER_FMPIE1;
    NVIC_EnableIRQ(USB_LP_CAN_RX0_IRQn);
    NVIC_EnableIRQ(CAN_RX1_IRQn);
    /* Fuera de inicializacion: espera los 11 recesivos */
    CAN->MCR &= ~CAN_MCR_INRQ;
    while (CAN->MSR & CAN_MSR_INAK) {}
}

static int manda(uint32_t id, uint8_t dato) {
    if (!(CAN->TSR & CAN_TSR_TME0) && !(CAN->TSR & CAN_TSR_TME1) && !(CAN->TSR & CAN_TSR_TME2)) {
        ++buzon.sin_buzon;
        return 0;
    }
    const unsigned b = (CAN->TSR & CAN_TSR_CODE) >> CAN_TSR_CODE_Pos;
    CAN->sTxMailBox[b].TDTR = 1;                       /* un byte */
    CAN->sTxMailBox[b].TDLR = dato;
    CAN->sTxMailBox[b].TDHR = 0;
    CAN->sTxMailBox[b].TIR = (id << 21) | CAN_TI0R_TXRQ;
    ++buzon.enviados;
    return 1;
}

static volatile uint32_t pend_resp, pend_n;
void USB_LP_CAN_RX0_IRQHandler(void) {
    ++buzon.irq20;
    while (CAN->RF0R & CAN_RF0R_FMP0) {
        const uint32_t id = CAN->sFIFOMailBox[0].RIR >> 21;
        const uint32_t d = CAN->sFIFOMailBox[0].RDLR & 0xFFu;
        CAN->RF0R = CAN_RF0R_RFOM0;
        ++buzon.rx0;
        buzon.ultimo_id = id;
        buzon.ultimo_dato = d;
        if (buzon.modo == 2) {
            if (id == 0x123) { pend_n = d + 1u; pend_resp = 1; } else ++buzon.malas;
        } else {
            /* la k-esima respuesta (desde 0) contesta al 0x123 con n = k + 1: trae k + 2 */
            if (id == 0x124 && d == ((buzon.buenas + 2u) & 0xFFu)) ++buzon.buenas; else ++buzon.malas;
        }
    }
    if (CAN->RF0R & CAN_RF0R_FOVR0) { CAN->RF0R = CAN_RF0R_FOVR0; ++buzon.perdidos; }
}
void CAN_RX1_IRQHandler(void) {
    ++buzon.irq21;
    while (CAN->RF1R & CAN_RF1R_FMP1) {
        const uint32_t id = CAN->sFIFOMailBox[1].RIR >> 21;
        CAN->RF1R = CAN_RF1R_RFOM1;
        ++buzon.rx1;
        if (id < 0x300 || id > 0x3FF) ++buzon.malas;
    }
}

int main(void) {
    reloj();
    const uint32_t modo = buzon.modo;
    if (modo != 1 && modo != 2) { buzon.estado = 0xEE; for (;;) {} }
    can_inicia(modo == 2);
    buzon.estado = 1;
    uint32_t n = 0, t = ms;
    for (;;) {
        buzon.esr = CAN->ESR;
        if (modo == 2) {
            if (pend_resp) { if (manda(0x124, (uint8_t)pend_n)) pend_resp = 0; }
            continue;
        }
        if (n >= 20 || ms == t) continue;
        t = ms;
        ++n;
        manda(0x123, (uint8_t)n);
        if (n % 5 == 0) { manda(0x155, 0); manda(0x345, (uint8_t)n); }
    }
}
