/* ===========================================================================
 * El cargador de sistema del STM32F303 del modelo (fase S7)
 *
 * El de ST no se puede distribuir: este es PROPIO, con el protocolo de AN3155
 * ("USART protocol used in the STM32 bootloader") y lo que AN2606 dice del
 * F303xB/C: USART1 en PA9 (TX) y PA10 (RX), USART2 en PD5 (TX) y PD6 (RX), 8
 * bits con paridad par y un bit de parada, la velocidad que diga el 0x7F del
 * anfitrion, PID 0x0422 y la version 3.1 con el borrado extendido (0x44).
 *
 *   0x00 GET          0x01 GET_VERSION   0x02 GET_ID
 *   0x11 READ         0x21 GO            0x31 WRITE (Flash, SRAM, opciones)
 *   0x44 EXT. ERASE   0x73 WRITE UNPROTECT   0x92 READOUT UNPROTECT
 *   0x63 WRITE PROTECT y 0x82 READOUT PROTECT: se anuncian, como en el de ST,
 *   pero contestan NACK [de modelo: no implementados]
 *
 * LA VELOCIDAD: mira las dos lineas de recepcion por GPIO hasta que una baja
 * -el bit de arranque del 0x7F- y mide con el SysTick, a 8 MHz del HSI, hasta
 * el siguiente flanco de bajada, que es el bit 7 del 0x7F: ocho bits. Con eso,
 * BRR. Es lo que hace el de ST con un temporizador, sin la autodeteccion de la
 * USART, que el modelo no tiene.
 *
 * Solo CMSIS. Lo compila `make` aqui y `gen_sysmem.sh` lo mete en sysmem.inc.
 * ===========================================================================*/
#include "stm32f303xc.h"

#define ACK  0x79u
#define NACK 0x1Fu
#define VERSION 0x31u

extern uint32_t _fin_texto, _data, _edata, _bss, _ebss, _pila;
int main(void);

void arranque(void) {
    uint32_t* s = &_fin_texto;
    for (uint32_t* d = &_data; d < &_edata;) *d++ = *s++;
    for (uint32_t* d = &_bss; d < &_ebss;) *d++ = 0;
    main();
    for (;;) {}
}
static void colgado(void) { for (;;) {} }
__attribute__((section(".vectores"), used))
static void* const vectores[16] = {
    &_pila, (void*)arranque, (void*)colgado, (void*)colgado, (void*)colgado,
    (void*)colgado, (void*)colgado, 0, 0, 0, 0, (void*)colgado, (void*)colgado,
    0, (void*)colgado, (void*)colgado,
};

static USART_TypeDef* u;

static void pon(uint8_t b) {
    while (!(u->ISR & USART_ISR_TXE)) {}
    u->TDR = b;
}
static uint8_t toma(void) {
    for (;;) {
        const uint32_t isr = u->ISR;
        if (isr & USART_ISR_RXNE) return (uint8_t)u->RDR;
        if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
            u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_PECF;
    }
}

/* ---- La velocidad, del 0x7F ------------------------------------------------ */
static uint32_t ahora(void) { return SysTick->VAL; }
static uint32_t pasado(uint32_t t0, uint32_t t1) { return (t0 - t1) & 0xFFFFFFu; }

static void autobaudios(void) {
    const uint32_t a10 = 1u << 10, d6 = 1u << 6;
    volatile uint32_t* idr;
    uint32_t pin;
    for (;;) {
        if (!(GPIOA->IDR & a10)) { idr = &GPIOA->IDR; pin = a10; u = USART1; break; }
        if (!(GPIOD->IDR & d6))  { idr = &GPIOD->IDR; pin = d6;  u = USART2; break; }
    }
    const uint32_t t0 = ahora();
    while (!(*idr & pin)) {}                 /* fin del arranque */
    while (*idr & pin) {}                    /* el bit 7 del 0x7F, a cero */
    const uint32_t t1 = ahora();
    const uint32_t bit = (pasado(t0, t1) + 4u) / 8u;
    while (!(*idr & pin)) {}                 /* paridad y parada, a uno */
    const uint32_t t2 = ahora();
    while (pasado(t2, ahora()) < 2u * bit) {}
    /* La USART: 8 bits + paridad par (M0 = 1, PCE), al reloj del bus (8 MHz) */
    if (u == USART1) {
        GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFFu << 4)) | (0x77u << 4);
        GPIOA->MODER  = (GPIOA->MODER & ~(0xFu << 18)) | (0xAu << 18);
    } else {
        GPIOD->AFR[0] = (GPIOD->AFR[0] & ~(0xFFu << 20)) | (0x77u << 20);
        GPIOD->MODER  = (GPIOD->MODER & ~(0xFu << 10)) | (0xAu << 10);
    }
    u->BRR = bit;
    u->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    while ((u->ISR & (USART_ISR_TEACK | USART_ISR_REACK)) != (USART_ISR_TEACK | USART_ISR_REACK)) {}
    pon(ACK);
}

/* ---- Las memorias ----------------------------------------------------------- */
static uint32_t flash_fin(void) { return 0x08000000u + (uint32_t)(*(volatile uint16_t*)0x1FFFF7CCu) * 1024u; }
static int en(uint32_t a, uint32_t n, uint32_t b, uint32_t f) { return a >= b && a + n <= f && a + n >= a; }
static int legible(uint32_t a, uint32_t n) {
    return en(a, n, 0x08000000u, flash_fin()) || en(a, n, 0x20000000u, 0x2000A000u) ||
           en(a, n, 0x10000000u, 0x10002000u) || en(a, n, 0x1FFFD800u, 0x1FFFF810u);
}
static int escribible(uint32_t a, uint32_t n) {
    return en(a, n, 0x08000000u, flash_fin()) || en(a, n, 0x20001400u, 0x2000A000u) ||
           en(a, n, 0x10000000u, 0x10002000u) || en(a, n, 0x1FFFF800u, 0x1FFFF810u);
}

/* Con RDP en el nivel 1, READ, WRITE y GO contestan NACK [AN3155 §3.4-3.6] */
static int protegida(void) { return (FLASH->OBR & FLASH_OBR_RDPRT) != 0; }

static void fpec_espera(void) { while (FLASH->SR & FLASH_SR_BSY) {} }
static void fpec_abre(void) {
    if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu; }
}
static int fpec_bien(void) {
    const uint32_t sr = FLASH->SR;
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR;
    return !(sr & (FLASH_SR_PGERR | FLASH_SR_WRPERR));
}
static int programa(uint32_t a, const uint8_t* d, uint32_t n) {
    fpec_abre();
    FLASH->CR |= FLASH_CR_PG;
    int ok = 1;
    for (uint32_t i = 0; i < n && ok; i += 2) {
        const uint16_t v = (uint16_t)(d[i] | ((i + 1 < n ? d[i + 1] : 0xFFu) << 8));
        *(volatile uint16_t*)(a + i) = v;
        fpec_espera();
        ok = fpec_bien();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return ok;
}
static int borra_pagina(uint32_t a) {
    fpec_abre();
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = a;
    FLASH->CR |= FLASH_CR_STRT;
    fpec_espera();
    FLASH->CR &= ~FLASH_CR_PER;
    return fpec_bien();
}
static int borra_todo(void) {
    fpec_abre();
    FLASH->CR |= FLASH_CR_MER;
    FLASH->CR |= FLASH_CR_STRT;
    fpec_espera();
    FLASH->CR &= ~FLASH_CR_MER;
    return fpec_bien();
}
/* Los bytes de opcion: se borran todos, se programan los que lleguen (las
 * medias palabras de 0x1FFF_F800) y se recargan con un reset (OBL_LAUNCH) */
__attribute__((noreturn)) static void opciones(const uint8_t* d, uint32_t n) {
    fpec_abre();
    FLASH->OPTKEYR = 0x45670123u; FLASH->OPTKEYR = 0xCDEF89ABu;
    FLASH->CR |= FLASH_CR_OPTER;
    FLASH->CR |= FLASH_CR_STRT;
    fpec_espera();
    FLASH->SR = FLASH_SR_EOP;
    FLASH->CR &= ~FLASH_CR_OPTER;
    FLASH->CR |= FLASH_CR_OPTPG;
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        *(volatile uint16_t*)(0x1FFFF800u + i) = d[i];   /* el byte; el complemento lo pone el FPEC */
        fpec_espera();
        FLASH->SR = FLASH_SR_EOP;
    }
    FLASH->CR = FLASH_CR_OBL_LAUNCH;
    for (;;) {}
}

/* ---- El protocolo ----------------------------------------------------------- */
static int direccion(uint32_t* a) {
    uint8_t b[4], x = 0;
    for (int i = 0; i < 4; ++i) { b[i] = toma(); x ^= b[i]; }
    if (toma() != x) return 0;
    *a = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return 1;
}
static uint8_t buf[260];

static const uint8_t ORDENES[] = {0x00, 0x01, 0x02, 0x11, 0x21, 0x31, 0x44, 0x63, 0x73, 0x82, 0x92};

static void get(void) {
    pon(ACK);
    pon(sizeof ORDENES);                       /* N = bytes que siguen - 1 */
    pon(VERSION);
    for (unsigned i = 0; i < sizeof ORDENES; ++i) pon(ORDENES[i]);
    pon(ACK);
}
static void lee(void) {
    uint32_t a;
    if (protegida()) { pon(NACK); return; }
    pon(ACK);
    if (!direccion(&a) || !legible(a, 1)) { pon(NACK); return; }
    pon(ACK);
    const uint8_t n = toma();
    if ((uint8_t)~n != toma() || !legible(a, (uint32_t)n + 1u)) { pon(NACK); return; }
    pon(ACK);
    for (uint32_t i = 0; i <= n; ++i) pon(*(volatile uint8_t*)(a + i));
}
static void escribe(void) {
    uint32_t a;
    if (protegida()) { pon(NACK); return; }
    pon(ACK);
    if (!direccion(&a)) { pon(NACK); return; }
    pon(ACK);
    const uint8_t n = toma();
    uint8_t x = n;
    for (uint32_t i = 0; i <= n; ++i) { buf[i] = toma(); x ^= buf[i]; }
    const uint32_t len = (uint32_t)n + 1u;
    if (toma() != x || !escribible(a, len)) { pon(NACK); return; }
    if (a >= 0x1FFFF800u && a < 0x1FFFF810u) { pon(ACK); opciones(buf, len); }
    int ok = 1;
    if (a >= 0x08000000u && a < 0x08100000u) ok = programa(a, buf, len);
    else for (uint32_t i = 0; i < len; ++i) *(volatile uint8_t*)(a + i) = buf[i];
    pon(ok ? ACK : NACK);
}
static void borra(void) {
    pon(ACK);
    const uint8_t h = toma(), l = toma();
    const uint16_t n = (uint16_t)((h << 8) | l);
    uint8_t x = h ^ l;
    if (n >= 0xFFFDu) {                        /* masivo (y los dos bancos: hay uno) */
        if (toma() != x) { pon(NACK); return; }
        pon(borra_todo() ? ACK : NACK);
        return;
    }
    if (n > 127u) { pon(NACK); return; }
    uint16_t p[128];
    for (uint32_t i = 0; i <= n; ++i) {
        const uint8_t a = toma(), b = toma();
        x ^= a ^ b;
        p[i] = (uint16_t)((a << 8) | b);
    }
    if (toma() != x) { pon(NACK); return; }
    const uint32_t paginas = (flash_fin() - 0x08000000u) / 2048u;
    int ok = 1;
    for (uint32_t i = 0; i <= n && ok; ++i)
        ok = p[i] < paginas && borra_pagina(0x08000000u + (uint32_t)p[i] * 2048u);
    pon(ok ? ACK : NACK);
}
/* Sin proteccion contra escritura y RDP = 0xAA, con el reset de OBL_LAUNCH.
 * La de lectura, ademas, borra la Flash antes [AN3155] */
static void desprotege(int lectura) {
    pon(ACK);
    if (lectura) borra_todo();
    pon(ACK);
    static const uint8_t ob[2] = {0xAA, 0x55};
    opciones(ob, 2);
}
__attribute__((noreturn)) static void salta(uint32_t a) {
    /* Lo que el cargador ha tocado, como estaba [AN2606: "de-initializes"] */
    u->CR1 = 0;
    SysTick->CTRL = 0;
    RCC->APB2RSTR = RCC_APB2RSTR_USART1RST; RCC->APB2RSTR = 0;
    RCC->APB1RSTR = RCC_APB1RSTR_USART2RST; RCC->APB1RSTR = 0;
    RCC->AHBRSTR = RCC_AHBRSTR_GPIOARST | RCC_AHBRSTR_GPIODRST; RCC->AHBRSTR = 0;
    RCC->APB2ENR = 0;
    RCC->APB1ENR = 0;
    RCC->AHBENR = 0x14u;
    SCB->VTOR = a;
    const uint32_t sp = *(volatile uint32_t*)a, pc = *(volatile uint32_t*)(a + 4u);
    __asm volatile ("msr msp, %0\n bx %1" :: "r"(sp), "r"(pc));
    for (;;) {}
}
static void go(void) {
    uint32_t a;
    if (protegida()) { pon(NACK); return; }
    pon(ACK);
    if (!direccion(&a) || !(en(a, 8, 0x08000000u, flash_fin()) || en(a, 8, 0x20000000u, 0x2000A000u))) {
        pon(NACK);
        return;
    }
    pon(ACK);
    while (!(u->ISR & USART_ISR_TC)) {}
    salta(a);
}

int main(void) {
    *(volatile uint32_t*)0x20000000u = 0x5359534Du;      /* 'MSYS': el modelo arranco aqui */
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    /* RX con pull-up mientras se espera; TX en AF7 */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << 20)) | (1u << 20);
    GPIOD->PUPDR = (GPIOD->PUPDR & ~(3u << 12)) | (1u << 12);
    SysTick->LOAD = 0xFFFFFFu;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    autobaudios();
    for (;;) {
        const uint8_t c = toma();
        if ((uint8_t)(c ^ toma()) != 0xFFu) { pon(NACK); continue; }
        switch (c) {
            case 0x00: get(); break;
            case 0x01: pon(ACK); pon(VERSION); pon(0); pon(0); pon(ACK); break;
            case 0x02: pon(ACK); pon(1); pon(0x04); pon(0x22); pon(ACK); break;
            case 0x11: lee(); break;
            case 0x21: go(); break;
            case 0x31: escribe(); break;
            case 0x44: borra(); break;
            case 0x73: desprotege(0); break;
            case 0x92: desprotege(1); break;
            default:   pon(NACK); break;
        }
    }
}
