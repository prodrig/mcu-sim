/* ===========================================================================
 * main.c — Firmware de demostración del DMA, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo, igual que el blinky de F3. Ejercita el
 * controlador DMA2 con la secuencia de configuración del manual [IR, §11.7]:
 *
 *   1. copia memoria-a-memoria de 256 bytes SRAM -> SRAM y verificación;
 *   2. interrupción de fin de transferencia por el NVIC (DMA2_Stream0_IRQn);
 *   3. escritura memoria-a-registro: una tabla de valores de GPIOD_BSRR se
 *      vuelca sobre el registro, de modo que el LED de PD12 conmuta sin que la
 *      CPU llegue a tocar el puerto.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define N_WORDS   64u                 /* 256 bytes */
#define N_BSRR    8u

volatile struct {
    volatile uint32_t done;       /* 1 cuando el firmware ha terminado       */
    volatile uint32_t copy_ok;    /* 1 si la copia mem-a-mem es correcta     */
    volatile uint32_t ndtr_end;   /* NDTR al terminar (debe ser 0)           */
    volatile uint32_t irq_count;  /* interrupciones de fin de transferencia  */
    volatile uint32_t bsrr_done;  /* 1 si la secuencia sobre GPIOD_BSRR fue  */
    volatile uint32_t lisr_snap;  /* instantánea de DMA2_LISR                */
} mbox __attribute__((section(".mailbox")));

static uint32_t src[N_WORDS];
static uint32_t dst[N_WORDS];
static uint32_t bsrr_seq[N_BSRR];

static volatile uint32_t g_irq;

void DMA2_Stream0_IRQHandler(void)
{
    if (DMA2->LISR & DMA_LISR_TCIF0) {
        mbox.lisr_snap = DMA2->LISR;          /* estado visto por el manejador */
        DMA2->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0;
        ++g_irq;
    }
}

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

/* Secuencia de configuración de un stream [IR, §11.7] */
static void dma_start(DMA_Stream_TypeDef *st, uint32_t par, uint32_t m0ar,
                      uint32_t ndt, uint32_t cr, uint32_t fcr)
{
    st->CR = 0u;                                  /* 1. deshabilitar        */
    while (st->CR & DMA_SxCR_EN) { }              /* 2. esperar             */
    DMA2->LIFCR = 0x0F7D0F7Du;                    /* 3. limpiar banderas    */
    st->PAR  = par;                               /* 4                      */
    st->M0AR = m0ar;                              /* 5                      */
    st->NDTR = ndt;                               /* 6                      */
    st->FCR  = fcr;                               /* 9                      */
    st->CR   = cr;                                /* 7, 8                   */
    st->CR   = cr | DMA_SxCR_EN;                  /* 10. arrancar           */
}

int main(void)
{
    mbox.done = 0u; mbox.copy_ok = 0u; mbox.ndtr_end = 0xFFFFu;
    mbox.irq_count = 0u; mbox.bsrr_done = 0u; mbox.lisr_snap = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_GPIODEN;

    for (uint32_t i = 0; i < N_WORDS; ++i) { src[i] = 0xA5000000u + i; dst[i] = 0u; }

    /* --- 1. Copia memoria a memoria con interrupción de fin --------------- */
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    dma_start(DMA2_Stream0, (uint32_t)src, (uint32_t)dst, N_WORDS,
              DMA_SxCR_DIR_1 |                         /* DIR = memoria a memoria */
              DMA_SxCR_MINC | DMA_SxCR_PINC |
              DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1 |    /* 32 bits en los dos lados */
              DMA_SxCR_PL_0 | DMA_SxCR_PL_1 |          /* prioridad muy alta       */
              DMA_SxCR_TCIE,
              DMA_SxFCR_DMDIS | DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1);
    while ((DMA2->LISR & DMA_LISR_TCIF0) == 0u && g_irq == 0u) { }

    mbox.ndtr_end  = DMA2_Stream0->NDTR;
    uint32_t ok = 1u;
    for (uint32_t i = 0; i < N_WORDS; ++i) if (dst[i] != src[i]) ok = 0u;
    mbox.copy_ok = ok;

    /* --- 2. El DMA gobierna el LED escribiendo en GPIOD_BSRR -------------- */
    GPIOD->MODER   = (GPIOD->MODER & ~GPIO_MODER_MODER12) | GPIO_MODER_MODER12_0;
    GPIOD->OTYPER &= ~GPIO_OTYPER_OT12;
    for (uint32_t i = 0; i < N_BSRR; ++i)
        bsrr_seq[i] = (i & 1u) ? (1u << (12 + 16)) : (1u << 12);

    /* Destino fijo (MINC = 0) sobre el registro; origen incremental */
    dma_start(DMA2_Stream1, (uint32_t)bsrr_seq, (uint32_t)&GPIOD->BSRR, N_BSRR,
              DMA_SxCR_DIR_1 | DMA_SxCR_PINC |
              DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1,
              DMA_SxFCR_DMDIS | DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1);
    while ((DMA2->LISR & DMA_LISR_TCIF1) == 0u) { }
    DMA2->LIFCR = DMA_LIFCR_CTCIF1;
    mbox.bsrr_done = 1u;

    /* --- 3. Parpadeo observable: un valor por transferencia --------------- */
    for (uint32_t i = 0; i < 6u; ++i) {
        dma_start(DMA2_Stream1, (uint32_t)&bsrr_seq[i & 1u],
                  (uint32_t)&GPIOD->BSRR, 1u,
                  DMA_SxCR_DIR_1 | DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1,
                  DMA_SxFCR_DMDIS | DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1);
        while ((DMA2->LISR & DMA_LISR_TCIF1) == 0u) { }
        DMA2->LIFCR = DMA_LIFCR_CTCIF1;
        for (volatile uint32_t k = 0; k < 20000u; ++k) { }   /* ~0.4 ms a 168 MHz */
    }

    mbox.irq_count = g_irq;
    mbox.done      = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
