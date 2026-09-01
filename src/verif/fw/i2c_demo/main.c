/* ===========================================================================
 * main.c — Firmware de demostración de I2C, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * El montaje es el de una placa cualquiera: una EEPROM serie 24Cxx en la
 * dirección 0x50 colgada de PB6 (SCL) y PB7 (SDA) con sus resistencias de
 * pull-up. El firmware:
 *
 *   1. Sube el reloj a 168 MHz con el PLL sobre el HSE de 8 MHz, dejando
 *      PCLK1 = 42 MHz (APB1 = HCLK/4).
 *   2. Programa el I2C1 en modo estándar a 100 kHz calculando CCR y TRISE
 *      con las fórmulas del manual: CCR = PCLK1 / (2 * f_SCL) = 210 y
 *      TRISE = PCLK1 * 1000 ns + 1 = 43.
 *   3. Escribe cuatro bytes en la EEPROM: [dirección de dispositivo | W]
 *      [puntero][d0][d1][d2][d3][STOP].
 *   4. Los relee con START REPETIDO —el patrón obligado del protocolo de la
 *      24Cxx— y los compara.
 *
 * Los pines se configuran en OPEN-DRAIN, como manda un bus I2C: el MCU nunca
 * fuerza un uno, y el nivel alto lo dan las resistencias de la placa. Si se
 * configurasen push-pull, en el modelo se vería el mismo cortocircuito que en
 * el silicio.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define EE_ADDR   0x50u        /* dirección de 7 bits de la EEPROM           */
#define EE_PTR    0x28u        /* posición donde escribimos                  */
#define N_BYTES   4u
#define TMO       200000u      /* vueltas máximas de espera de una bandera   */

volatile struct {
    volatile uint32_t done;    /* 1 cuando el firmware ha terminado          */
    volatile uint32_t wr_ok;   /* 1 si la escritura completa fue reconocida  */
    volatile uint32_t rd_ok;   /* 1 si lo releído coincide con lo escrito    */
    volatile uint32_t nbytes;  /* bytes de ida y vuelta                      */
    volatile uint32_t ccr;     /* CCR calculado por el propio firmware       */
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

/* Pin de función alternativa en OPEN-DRAIN: lo que exige un bus I2C */
static void pin_af_od(GPIO_TypeDef *g, uint32_t pin, uint32_t af)
{
    g->MODER   = (g->MODER   & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->OTYPER |= (1u << pin);                      /* colector abierto       */
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * pin))) | (3u << (2 * pin));
    g->PUPDR  &= ~(3u << (2 * pin));               /* pull-up en la placa    */
    if (pin < 8u) g->AFR[0] = (g->AFR[0] & ~(0xFu << (4 * pin))) | (af << (4 * pin));
    else          g->AFR[1] = (g->AFR[1] & ~(0xFu << (4 * (pin - 8u)))) |
                              (af << (4 * (pin - 8u)));
}

/* Espera a que se levante alguna de las banderas de SR1. Devuelve SR1, o 0
 * si se agota el plazo o si el esclavo no ha reconocido (AF). */
static uint32_t wait_sr1(uint32_t flags)
{
    for (uint32_t i = 0; i < TMO; ++i) {
        const uint32_t sr1 = I2C1->SR1;
        if (sr1 & I2C_SR1_AF) return 0u;
        if (sr1 & flags) return sr1;
    }
    return 0u;
}

static void clear_addr(void) { (void)I2C1->SR1; (void)I2C1->SR2; }

/* Pide el STOP y espera a que el hardware lo borre. Sin esta espera, la
 * siguiente petición de START llegaría con el STOP todavía pendiente y el
 * bloque generaría un START REPETIDO en vez de soltar el bus: es el error
 * clásico al encadenar dos transferencias, y el modelo lo reproduce. */
static void stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
    for (uint32_t i = 0; i < TMO && (I2C1->CR1 & I2C_CR1_STOP); ++i) { }
}

/* START (o START REPETIDO) + dirección. dir = 0 escritura, 1 lectura. */
static int addr_phase(uint32_t dir)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (!wait_sr1(I2C_SR1_SB)) return 0;
    I2C1->DR = (EE_ADDR << 1) | dir;
    if (!wait_sr1(I2C_SR1_ADDR)) { stop(); return 0; }
    return 1;
}

static int ee_write(uint8_t ptr, const uint8_t *d, uint32_t n)
{
    if (!addr_phase(0u)) return 0;
    clear_addr();
    if (!wait_sr1(I2C_SR1_TXE)) { stop(); return 0; }
    I2C1->DR = ptr;
    for (uint32_t i = 0; i < n; ++i) {
        if (!wait_sr1(I2C_SR1_TXE)) { stop(); return 0; }
        I2C1->DR = d[i];
    }
    if (!wait_sr1(I2C_SR1_BTF | I2C_SR1_TXE)) { stop(); return 0; }
    stop();
    return 1;
}

static int ee_read(uint8_t ptr, uint8_t *d, uint32_t n)
{
    /* Primero se coloca el puntero con una escritura sin STOP */
    if (!addr_phase(0u)) return 0;
    clear_addr();
    if (!wait_sr1(I2C_SR1_TXE)) { stop(); return 0; }
    I2C1->DR = ptr;
    if (!wait_sr1(I2C_SR1_BTF | I2C_SR1_TXE)) { stop(); return 0; }

    /* START REPETIDO y giro a lectura, sin soltar el bus */
    I2C1->CR1 |= I2C_CR1_ACK;
    if (!addr_phase(1u)) return 0;
    clear_addr();
    for (uint32_t i = 0; i < n; ++i) {
        /* Antes del penúltimo byte se retira el ACK y se pide el STOP: así el
         * último llega con NACK y el bus queda libre. */
        if (i + 2u == n) I2C1->CR1 = (I2C1->CR1 & ~I2C_CR1_ACK) | I2C_CR1_STOP;
        if (n == 1u && i == 0u) I2C1->CR1 = (I2C1->CR1 & ~I2C_CR1_ACK) | I2C_CR1_STOP;
        if (!wait_sr1(I2C_SR1_RXNE)) { stop(); return 0; }
        d[i] = (uint8_t)(I2C1->DR & 0xFFu);
    }
    return 1;
}

int main(void)
{
    mbox.done = 0u; mbox.wr_ok = 0u; mbox.rd_ok = 0u;
    mbox.nbytes = 0u; mbox.ccr = 0u; mbox.pclk1 = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* I2C1 en AF4: PB6 = SCL, PB7 = SDA */
    pin_af_od(GPIOB, 6u, 4u);
    pin_af_od(GPIOB, 7u, 4u);

    const uint32_t pclk1 = SystemCoreClock / 4u;         /* APB1 = HCLK/4 */
    const uint32_t ccr   = pclk1 / (2u * 100000u);       /* modo estándar */
    mbox.pclk1 = pclk1;
    mbox.ccr   = ccr;

    I2C1->CR1 = I2C_CR1_SWRST;                           /* reset del bloque */
    I2C1->CR1 = 0u;
    I2C1->CR2 = pclk1 / 1000000u;                        /* FREQ en MHz = 42 */
    I2C1->CCR = ccr;
    I2C1->TRISE = (pclk1 / 1000000u) + 1u;               /* 1000 ns = 43 */
    I2C1->CR1 = I2C_CR1_PE;

    static const uint8_t msg[N_BYTES] = { 'I', '2', 'C', '!' };
    uint8_t got[N_BYTES] = { 0, 0, 0, 0 };

    mbox.wr_ok = (uint32_t)ee_write(EE_PTR, msg, N_BYTES);

    uint32_t n = 0u, ok = 0u;
    if (mbox.wr_ok && ee_read(EE_PTR, got, N_BYTES)) {
        ok = 1u;
        for (uint32_t i = 0; i < N_BYTES; ++i) {
            if (got[i] != msg[i]) ok = 0u;
            else ++n;
        }
    }
    mbox.rd_ok  = ok;
    mbox.nbytes = n;

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
