/* ===========================================================================
 * f3_vcp — el puerto COM virtual de la STM32F3DISCOVERY (fase S3 del F303VC)
 *
 * La USART1 en PC4 (TX) y PC5 (RX), que en la placa van al ST-LINK/V2-B
 * [UM1570, §6.2.3], a 115200 8N1 con los 72 MHz del HSE (el MCO del
 * ST-LINK). Saluda, y luego cada 100 ms una linea y LD3 (PE9) cambia; lo que
 * llega por RX se devuelve en mayusculas. Solo CMSIS, sin HAL.
 *
 *   ./build/mcu-sim placas/stm32f3discovery.xml \
 *       verif/fw/stm32f3/f3_vcp/f3_vcp.bin --ms=350 --serie VCP=memoria
 * ===========================================================================*/
#include "stm32f303xc.h"

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

static void pon(char c) {
    while (!(USART1->ISR & USART_ISR_TXE)) {}
    USART1->TDR = (uint8_t)c;
}
static void dice(const char* s) { while (*s) pon(*s++); }
static void numero(uint32_t n) {
    char b[11]; int i = 0;
    do { b[i++] = (char)('0' + n % 10u); n /= 10u; } while (n);
    while (i) pon(b[--i]);
}

int main(void) {
    pll72();
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN | RCC_AHBENR_GPIOEEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    GPIOE->MODER = (GPIOE->MODER & ~(3u << 18)) | (1u << 18);          /* PE9: LD3 */
    /* PC4 y PC5 en AF7 (USART1_TX, USART1_RX) [DS9118, tabla 14] */
    GPIOC->MODER = (GPIOC->MODER & ~(0xFu << 8)) | (0xAu << 8);
    GPIOC->AFR[0] = (GPIOC->AFR[0] & ~(0xFFu << 16)) | (0x77u << 16);
    /* USART1 en PCLK2 = 72 MHz: 72e6 / 115200 = 625 */
    USART1->BRR = 625u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    while ((USART1->ISR & (USART_ISR_TEACK | USART_ISR_REACK)) != (USART_ISR_TEACK | USART_ISR_REACK)) {}

    dice("f3_vcp: la USART1 por PC4/PC5, 115200 8N1, SYSCLK 72 MHz\r\n");
    uint32_t n = 0, t = ms;
    for (;;) {
        if (USART1->ISR & USART_ISR_ORE) USART1->ICR = USART_ICR_ORECF;
        if (USART1->ISR & USART_ISR_RXNE) {
            char c = (char)USART1->RDR;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            pon(c);
        }
        if (ms - t >= 100u) {
            t += 100u;
            GPIOE->ODR ^= 1u << 9;
            dice("tic "); numero(++n); dice("\r\n");
        }
    }
}
