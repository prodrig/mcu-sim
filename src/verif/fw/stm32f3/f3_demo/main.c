/* ===========================================================================
 * f3_demo — el firmware de verificacion de la fase S1 del STM32F303VC
 *
 * Un modo por grupo de testf3, elegido por el banco en buzon.modo antes de
 * soltar el reset. Solo CMSIS (stm32f303xc.h, Apache-2.0): ni HAL ni BSP, para
 * que lo que se prueba sea el chip y no la biblioteca.
 *
 *   0  arranca con el HSI a 8 MHz, anota IDCODE, F_SIZE, UID y la
 *      calibracion de fabrica, y hace parpadear LD3 (PE9) cada 10 ms
 *   1  72 MHz desde el HSE (los 8 MHz del ST-LINK) en modo CRISTAL, y LD4
 *   2  lo mismo en modo BYPASS
 *   3  la secuencia MALA: 72 MHz con LATENCY = 0 y APB1 sin dividir
 *   4  el GPIO con BRR, y el MCO en PA8: el HSI y luego PLL/2
 *   5  EXTI0 (B1, PA0) por flanco de subida, y la linea 33 por SWIER2
 *   6  una rutina en la CCM, y una escritura en una pagina protegida (RCR)
 *   7  MEM_MODE: lo que hay en 0x0 con la Flash, la memoria de sistema y la
 *      SRAM
 *   8  (S2) el FPEC: borrar la pagina 126, programar dos medias palabras,
 *      PGERR al reprogramar, 0x0000 si se puede, LOCK; y una palabra con PG,
 *      que es error de bus
 *   9  (S2) los bytes de opcion: borrarlos, RDP = 0xAA y DATA0 = 0x5A,
 *      OBL_LAUNCH; y en el arranque siguiente, dejarlos como estaban
 * ===========================================================================*/
#include "stm32f303xc.h"

typedef struct {
    volatile uint32_t modo, arranques, estado, idcode, fsize, uid[3], tscal, vrefint;
    volatile uint32_t r[4], pulsaciones, irq_comp, cfsr, bfar, faults, cfgr, ccm;
    volatile uint32_t extra[3];
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static volatile uint32_t ms;
static void fpec_espera(void) { while (FLASH->SR & FLASH_SR_BSY) {} }
static void fpec_abre(void) {
    if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu; }
}
static void ob_abre(void) {
    fpec_abre();
    FLASH->OPTKEYR = 0x45670123u; FLASH->OPTKEYR = 0xCDEF89ABu;
}
/* Borra los bytes de opcion y deja RDP = 0xAA (nivel 0) y, si no es 0xFF,
 * DATA0; luego OBL_LAUNCH: un reset del sistema que los recarga */
__attribute__((noreturn)) static void ob_reprograma(uint8_t data0) {
    ob_abre();
    /* |= y no =: OPTWRE se pierde si se le escribe un cero */
    FLASH->CR |= FLASH_CR_OPTER;
    FLASH->CR |= FLASH_CR_STRT;
    fpec_espera();
    FLASH->SR = FLASH_SR_EOP;
    FLASH->CR &= ~FLASH_CR_OPTER;
    FLASH->CR |= FLASH_CR_OPTPG;
    *(volatile uint16_t*)0x1FFFF800u = 0x00AAu;               /* RDP: nivel 0 */
    fpec_espera();
    if (data0 != 0xFFu) { *(volatile uint16_t*)0x1FFFF804u = data0; fpec_espera(); }
    buzon.extra[0] = *(volatile uint32_t*)0x1FFFF804u;       /* DATA0 y DATA1 */
    FLASH->SR = FLASH_SR_EOP;
    FLASH->CR = FLASH_CR_OBL_LAUNCH;
    for (;;) {}
}
void SysTick_Handler(void) { ++ms; }
static void systick(uint32_t hz) {
    SysTick->LOAD = hz / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}
static void espera_ms(uint32_t n) { const uint32_t t0 = ms; while (ms - t0 < n) __WFI(); }

void HardFault_Handler(void) {
    buzon.cfsr = SCB->CFSR; buzon.bfar = SCB->BFAR; ++buzon.faults; buzon.estado = 201;
    for (;;) __WFI();
}
void BusFault_Handler(void) {
    buzon.cfsr = SCB->CFSR; buzon.bfar = SCB->BFAR; ++buzon.faults; buzon.estado = 200;
    for (;;) __WFI();
}

/* La rosa: PE8..PE15 de salida */
static void leds(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOEEN;
    GPIOE->MODER = (GPIOE->MODER & 0x0000FFFFu) | 0x55550000u;
}
__attribute__((noreturn)) static void parpadea(uint32_t pin) {
    for (;;) { GPIOE->ODR ^= 1u << pin; espera_ms(10); }
}

/* 72 MHz desde el HSE por el PLL (x9). `bien`: LATENCY 2 y APB1 a /2 antes de
 * cambiar; si no, LATENCY 0 y APB1 a 72 MHz, que es lo que el modelo avisa */
static int pll72(int bypass, int bien) {
    uint32_t n = 0;
    if (bypass) RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) if (++n > 2000000u) return 0;
    buzon.r[0] = n;
    if (bien) FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL | RCC_CFGR_PPRE1)) |
                RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL9 | (bien ? RCC_CFGR_PPRE1_DIV2 : 0u);
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    buzon.cfgr = RCC->CFGR;
    buzon.r[1] = RCC->CR;
    buzon.r[2] = FLASH->ACR;
    systick(72000000u);
    return 1;
}

/* Desde la CCM: se copia en el arranque y se ejecuta alli */
__attribute__((section(".ccmtext"), noinline)) uint32_t en_ccm(uint32_t x) {
    uint32_t pc;
    __asm volatile ("mov %0, pc" : "=r"(pc));
    buzon.r[1] = pc;
    uint32_t a = 0;
    for (uint32_t i = 0; i < x; ++i) a = a * 3u + i;
    return a;
}

void EXTI0_IRQHandler(void) {
    EXTI->PR = 1u;
    ++buzon.pulsaciones;
    GPIOE->ODR ^= 1u << 9;
}
void COMP7_IRQHandler(void) {
    if (EXTI->PR2 & 2u) { EXTI->PR2 = 2u; ++buzon.irq_comp; }
}

int main(void) {
    ++buzon.arranques;
    buzon.idcode = DBGMCU->IDCODE;
    buzon.fsize = *(volatile uint16_t*)FLASHSIZE_BASE;
    for (unsigned i = 0; i < 3; ++i) buzon.uid[i] = ((volatile uint32_t*)UID_BASE)[i];
    buzon.tscal = *(volatile uint16_t*)0x1FFFF7B8u | ((uint32_t)*(volatile uint16_t*)0x1FFFF7C2u << 16);
    buzon.vrefint = *(volatile uint16_t*)0x1FFFF7BAu;
    buzon.cfgr = RCC->CFGR;
    switch (buzon.modo) {
    case 0:
        systick(8000000u);
        leds();
        buzon.estado = 100;
        parpadea(9);
    case 1:
    case 2:
        if (!pll72(buzon.modo == 2, 1)) { buzon.estado = 90; for (;;) __WFI(); }
        leds();
        buzon.estado = 100;
        parpadea(8);
    case 3:
        if (!pll72(0, 0)) { buzon.estado = 90; for (;;) __WFI(); }
        leds();
        buzon.estado = 100;
        parpadea(8);
    case 4:
        leds();
        GPIOE->BSRR = 0x0000FF00u;
        buzon.r[0] = GPIOE->ODR;
        GPIOE->BRR = 0x00005500u;
        buzon.r[1] = GPIOE->ODR;
        buzon.r[2] = *(volatile uint32_t*)(GPIOE_BASE + 0x28u);   /* BRR lee cero */
        RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
        GPIOA->OSPEEDR |= 3u << 16;
        GPIOA->AFR[1] &= ~0xFu;                                   /* PA8: AF0, MCO */
        GPIOA->MODER = (GPIOA->MODER & ~(3u << 16)) | (2u << 16);
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_MCO) | RCC_CFGR_MCO_HSI;
        systick(8000000u);
        buzon.estado = 101;
        espera_ms(2);
        pll72(0, 1);
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_MCO) | RCC_CFGR_MCO_PLL;   /* PLL/2: 36 MHz */
        buzon.estado = 102;
        for (;;) __WFI();
    case 5:
        leds();
        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
        SYSCFG->EXTICR[0] &= ~0xFu;                               /* EXTI0 <- PA0 */
        EXTI->RTSR |= 1u;
        EXTI->IMR |= 1u;
        NVIC_EnableIRQ(EXTI0_IRQn);
        EXTI->IMR2 |= 2u;                                         /* linea 33 */
        NVIC_EnableIRQ(COMP7_IRQn);
        EXTI->SWIER2 = 2u;
        buzon.r[0] = EXTI->IMR2;
        buzon.estado = 100;
        for (;;) __WFI();
    case 6: {
        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        buzon.ccm = en_ccm(10);
        buzon.r[0] = (uint32_t)&en_ccm;
        SYSCFG->RCR |= 1u << 7;                                   /* la ultima pagina */
        buzon.r[2] = SYSCFG->RCR;
        *(volatile uint32_t*)0x10001800u = 0x5A5A5A5Au;           /* pagina 6: se puede */
        buzon.r[3] = *(volatile uint32_t*)0x10001800u;
        SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
        buzon.estado = 150;
        *(volatile uint32_t*)0x10001C00u = 0xA5A5A5A5u;           /* pagina 7: protegida */
        buzon.estado = 151;                                       /* no deberia llegar */
        for (;;) __WFI();
    }
    case 7:
        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        buzon.r[0] = *(volatile uint32_t*)0x0u;                   /* la Flash: _estack */
        SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | 1u;
        buzon.r[1] = *(volatile uint32_t*)0x0u;                   /* la memoria de sistema */
        SYSCFG->CFGR1 |= 3u;
        buzon.r[2] = *(volatile uint32_t*)0x4u;                   /* la SRAM: buzon.arranques */
        SYSCFG->CFGR1 &= ~SYSCFG_CFGR1_MEM_MODE;
        buzon.r[3] = SYSCFG->CFGR1;
        buzon.estado = 100;
        for (;;) __WFI();
    case 8: {
        volatile uint16_t* const p = (volatile uint16_t*)0x0803F000u;     /* pagina 126 */
        fpec_abre();
        buzon.r[0] = FLASH->CR;                                   /* sin LOCK */
        FLASH->CR = FLASH_CR_PER;
        FLASH->AR = 0x0803F000u;
        FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
        fpec_espera();
        buzon.extra[0] = FLASH->SR;                               /* EOP */
        FLASH->SR = FLASH_SR_EOP;
        buzon.r[1] = *(volatile uint32_t*)0x0803F000u;            /* borrada */
        FLASH->CR = FLASH_CR_PG;
        p[0] = 0x1234u; fpec_espera();
        p[1] = 0xABCDu; fpec_espera();
        FLASH->SR = FLASH_SR_EOP;
        buzon.r[2] = *(volatile uint32_t*)0x0803F000u;            /* 0xABCD1234 */
        p[0] = 0x5555u; fpec_espera();                            /* no borrada: PGERR */
        buzon.extra[1] = FLASH->SR;
        FLASH->SR = FLASH_SR_PGERR | FLASH_SR_EOP;
        p[0] = 0x0000u; fpec_espera();                            /* 0x0000 si se puede */
        FLASH->SR = FLASH_SR_EOP;
        buzon.extra[2] = *(volatile uint32_t*)0x0803F000u;        /* 0xABCD0000 */
        FLASH->CR = FLASH_CR_LOCK;
        buzon.r[3] = FLASH->CR;
        fpec_abre();
        FLASH->CR = FLASH_CR_PG;
        SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
        buzon.estado = 150;
        *(volatile uint32_t*)0x0803F004u = 0u;                    /* una palabra: error de bus */
        __DSB();
        buzon.estado = 151;                                       /* no deberia llegar */
        for (;;) __WFI();
    }
    case 9:
        if (buzon.arranques == 1) ob_reprograma(0x5Au);
        if (buzon.arranques == 2) {
            buzon.r[0] = FLASH->OBR;
            buzon.r[1] = RCC->CSR;
            buzon.r[2] = buzon.extra[0];
            ob_reprograma(0xFFu);
        }
        buzon.r[3] = FLASH->OBR;
        buzon.estado = 110;
        for (;;) __WFI();
    default:
        buzon.estado = 99;
        for (;;) __WFI();
    }
}
