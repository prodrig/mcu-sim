/* ===========================================================================
 * f3_mems — los sensores de la STM32F3DISCOVERY (fase S4 del F303VC)
 *
 * Lee los tres -giroscopo por SPI1, acelerometro y magnetometro por I2C1-
 * sin BSP y sin HAL, y deja en el buzon (0x2000_0000) lo que dicen, para que
 * testf3 lo compare con la Postura. Distingue las dos revisiones de la placa
 * por los WHO_AM_I, como el BSP de ST: 0xD4 (L3GD20) o 0xD3 (I3G4250D) en el
 * giroscopo; 0x40 en WHO_AM_I_M (0x4F) del magnetometro del LSM303AGR, que el
 * LSM303DLHC no tiene (el suyo dice 'H43' en IRA/IRB/IRC).
 *
 *   giroscopo     95 Hz, 500 dps (17,5 mdps por cuenta)
 *   acelerometro  50 Hz, 2 g, alta resolucion (12 bits a la izquierda)
 *   magnetometro  DLHC: 30 Hz, 1,3 gauss (1100 / 980 cuentas por gauss);
 *                 AGR: 100 Hz, continuo (1,5 mG por cuenta)
 *
 * Cada 10 ms una lectura de los tres, y LD3 (PE9) cambia. Solo CMSIS.
 * ===========================================================================*/
#include "stm32f303xc.h"

typedef struct {
    volatile uint32_t estado, vueltas, who_g, who_a, who_m, rev, errores;
    volatile int32_t  g[3], a[3], m[3];
    volatile uint32_t st_g, st_a, st_m;
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static volatile uint32_t ms;
void SysTick_Handler(void) { ++ms; }

static void pll72(void) {
    RCC->CR |= RCC_CR_HSEON;
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

/* --- SPI1: el giroscopo, CS en PE3, modo 0, 72 / 16 = 4,5 MHz ----------- */
static uint8_t spi(uint8_t b) {
    while (!(SPI1->SR & SPI_SR_TXE)) {}
    *(volatile uint8_t*)&SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) {}
    return *(volatile uint8_t*)&SPI1->DR;
}
static void g_lee(uint8_t reg, uint8_t* d, unsigned n) {
    GPIOE->BRR = 1u << 3;
    spi((uint8_t)(reg | 0x80u | (n > 1 ? 0x40u : 0u)));
    for (unsigned i = 0; i < n; ++i) d[i] = spi(0);
    while (SPI1->SR & SPI_SR_BSY) {}
    GPIOE->BSRR = 1u << 3;
}
static void g_escribe(uint8_t reg, uint8_t v) {
    GPIOE->BRR = 1u << 3;
    spi(reg);
    spi(v);
    while (SPI1->SR & SPI_SR_BSY) {}
    GPIOE->BSRR = 1u << 3;
}

/* --- I2C1: la brujula, 100 kHz con el HSI (TIMINGR de RM0316, tabla 87) -- */
static int i2c_espera(uint32_t bit) {
    uint32_t n = 0;
    while (!(I2C1->ISR & bit)) {
        if (I2C1->ISR & I2C_ISR_NACKF) { I2C1->ICR = I2C_ICR_NACKCF; ++buzon.errores; return 0; }
        if (++n > 200000u) { ++buzon.errores; return 0; }
    }
    return 1;
}
static int i2c_lee(uint8_t dir, uint8_t sub, uint8_t* d, unsigned n) {
    I2C1->CR2 = (uint32_t)(dir << 1) | (1u << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (!i2c_espera(I2C_ISR_TXIS)) return 0;
    I2C1->TXDR = sub;
    if (!i2c_espera(I2C_ISR_TC)) return 0;
    I2C1->CR2 = (uint32_t)(dir << 1) | (n << I2C_CR2_NBYTES_Pos) | I2C_CR2_RD_WRN |
                I2C_CR2_AUTOEND | I2C_CR2_START;
    for (unsigned i = 0; i < n; ++i) {
        if (!i2c_espera(I2C_ISR_RXNE)) return 0;
        d[i] = (uint8_t)I2C1->RXDR;
    }
    if (!i2c_espera(I2C_ISR_STOPF)) return 0;
    I2C1->ICR = I2C_ICR_STOPCF;
    return 1;
}
static int i2c_escribe(uint8_t dir, uint8_t sub, uint8_t v) {
    I2C1->CR2 = (uint32_t)(dir << 1) | (2u << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
    if (!i2c_espera(I2C_ISR_TXIS)) return 0;
    I2C1->TXDR = sub;
    if (!i2c_espera(I2C_ISR_TXIS)) return 0;
    I2C1->TXDR = v;
    if (!i2c_espera(I2C_ISR_STOPF)) return 0;
    I2C1->ICR = I2C_ICR_STOPCF;
    return 1;
}
#define ACC 0x19u
#define MAG 0x1Eu

int main(void) {
    pll72();
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOEEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    /* PE3 (CS) salida a 1, PE9 (LD3) salida */
    GPIOE->BSRR = 1u << 3;
    GPIOE->MODER = (GPIOE->MODER & ~((3u << 6) | (3u << 18))) | (1u << 6) | (1u << 18);
    /* PA5-PA7 en AF5 (SPI1) */
    GPIOA->MODER = (GPIOA->MODER & ~(0x3Fu << 10)) | (0x2Au << 10);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFFFu << 20)) | (0x555u << 20);
    /* PB6, PB7 en AF4 (I2C1), drenador abierto: los pull-up son R27 y R28 */
    GPIOB->OTYPER |= (1u << 6) | (1u << 7);
    GPIOB->MODER = (GPIOB->MODER & ~(0xFu << 12)) | (0xAu << 12);
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFFu << 24)) | (0x44u << 24);

    SPI1->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
    SPI1->CR1 = SPI_CR1_MSTR | (3u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_SPE;
    I2C1->TIMINGR = 0x10420F13u;
    I2C1->CR1 = I2C_CR1_PE;

    uint8_t b[6];
    g_lee(0x0F, b, 1);
    buzon.who_g = b[0];
    if (i2c_lee(ACC, 0x0F, b, 1)) buzon.who_a = b[0];
    if (i2c_lee(MAG, 0x4F, b, 1)) buzon.who_m = b[0];
    const int agr = buzon.who_m == 0x40u;
    if (!agr && i2c_lee(MAG, 0x0A, b, 3))
        buzon.who_m = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];   /* 'H43' */
    buzon.rev = (buzon.who_g == 0xD3u && agr) ? 'E' : (buzon.who_g == 0xD4u && !agr) ? 'D' : '?';

    g_escribe(0x23, 0x10);             /* CTRL_REG4: 500 dps */
    g_escribe(0x20, 0x0F);             /* CTRL_REG1: 95 Hz, encendido, XYZ */
    i2c_escribe(ACC, 0x23, 0x08);      /* CTRL_REG4_A: 2 g, HR */
    i2c_escribe(ACC, 0x20, 0x47);      /* CTRL_REG1_A: 50 Hz, XYZ */
    if (agr) {
        i2c_escribe(MAG, 0x60, 0x0C);  /* CFG_REG_A_M: 100 Hz, continuo */
    } else {
        i2c_escribe(MAG, 0x00, 0x14);  /* CRA_REG_M: 30 Hz */
        i2c_escribe(MAG, 0x01, 0x20);  /* CRB_REG_M: 1,3 gauss */
        i2c_escribe(MAG, 0x02, 0x00);  /* MR_REG_M: continuo */
    }
    buzon.estado = 1;

    uint32_t t = ms;
    for (;;) {
        while (ms - t < 10u) __WFI();
        t += 10u;
        g_lee(0x27, b, 1); buzon.st_g = b[0];
        g_lee(0x28, b, 6);
        for (int i = 0; i < 3; ++i) buzon.g[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
        if (i2c_lee(ACC, 0x27, b, 1)) buzon.st_a = b[0];
        if (i2c_lee(ACC, 0x28 | 0x80, b, 6))
            for (int i = 0; i < 3; ++i) buzon.a[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
        if (agr) {
            if (i2c_lee(MAG, 0x67, b, 1)) buzon.st_m = b[0];
            if (i2c_lee(MAG, 0x68, b, 6))
                for (int i = 0; i < 3; ++i) buzon.m[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
        } else {
            if (i2c_lee(MAG, 0x09, b, 1)) buzon.st_m = b[0];
            if (i2c_lee(MAG, 0x03, b, 6)) {      /* X, Z, Y; el alto primero */
                buzon.m[0] = (int16_t)((b[0] << 8) | b[1]);
                buzon.m[2] = (int16_t)((b[2] << 8) | b[3]);
                buzon.m[1] = (int16_t)((b[4] << 8) | b[5]);
            }
        }
        ++buzon.vueltas;
        GPIOE->ODR ^= 1u << 9;
    }
}
