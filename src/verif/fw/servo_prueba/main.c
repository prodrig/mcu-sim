/* ===========================================================================
 * main.c — La prueba del servo: una secuencia de pulsos por el TIM3 de una
 *          NUCLEO-F446RE
 *
 * El firmware de la prueba M8 de verif/gui/marcha.py. La señal es la de
 * ../servo_demo -el TIM3, canal 1, en PB4 (D5), a 1 MHz y con 20 ms de
 * periodo: CCR1 es la anchura del pulso en microsegundos-, y lo que cambia es
 * qué pide y cuándo. Cada cambio se escribe 10 ms antes de un periodo, y
 * CCR1 lleva precarga: el pulso nuevo es el del periodo que empieza en el
 * instante redondo -300 ms, 600 ms...-. A la vez lo dice por el VCP.
 *
 *     instante   pulso     lo que tiene que pasar
 *     0 ms       1500 us   al centro, desde donde esté
 *     300 ms     2000 us   a +90: 90 grados a la velocidad del servo
 *     600 ms     1500 us   al centro otra vez
 *     900 ms     1505 us   dentro de la banda muerta (10 us): no se mueve
 *     1000 ms    1520 us   fuera de ella: +3,6 grados
 *     1100 ms    2500 us   fuera del rango: al tope, +90, y se avisa
 *     1400 ms    1000 us   hacia -90...
 *     1460 ms    sin señal ...y a los 60 ms sin pulsos, el analógico suelta
 *                          el motor y se queda donde esté
 *     1700 ms    1000 us   vuelve la señal: sigue hasta -90
 *     1900 ms     200 us   un pulso que no lo es: se ignora, y se avisa
 *
 * Un pulso de 0 us es la salida a cero todo el periodo: sin señal.
 * ===========================================================================*/
#include "stm32f446xx.h"

static volatile uint32_t g_ms;

void SysTick_Handler(void) { ++g_ms; }

static void hasta_ms(uint32_t t)
{
    while (g_ms < t) __WFI();
}

static void modo(GPIO_TypeDef* g, uint32_t n, uint32_t m)
{
    g->MODER = (g->MODER & ~(3u << (2u * n))) | (m << (2u * n));
}
static void af(GPIO_TypeDef* g, uint32_t n, uint32_t f)
{
    modo(g, n, 2u);
    if (n < 8u) g->AFR[0] = (g->AFR[0] & ~(15u << (4u * n))) | (f << (4u * n));
    else        g->AFR[1] = (g->AFR[1] & ~(15u << (4u * (n - 8u)))) | (f << (4u * (n - 8u)));
}

static void vcp(const char* s)
{
    while (*s) {
        while ((USART2->SR & USART_SR_TXE) == 0u) { }
        USART2->DR = (uint8_t)*s++;
    }
}

static void pide(uint32_t us, const char* que)
{
    TIM3->CCR1 = us;
    vcp(que);
}

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN | RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;

    SystemCoreClockUpdate();
    af(GPIOA, 2u, 7u);                       /* USART2_TX: el VCP */
    USART2->BRR = 139u;                      /* 16 MHz / 115200 */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;

    /* TIM3 a 1 MHz, 20 ms, PWM 1 con precarga, en PB4; el SysTick a la vez,
     * para que los periodos empiecen en los milisegundos redondos */
    TIM3->PSC = 15u;
    TIM3->ARR = 19999u;
    TIM3->CCR1 = 1500u;
    TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM3->CCER = TIM_CCER_CC1E;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    af(GPIOB, 4u, 2u);                       /* PB4: TIM3_CH1 */
    SysTick_Config(SystemCoreClock / 1000u);
    TIM3->CR1 |= TIM_CR1_CEN;
    vcp("servo_prueba: 1500\r\n");

    hasta_ms(290);  pide(2000u, "servo_prueba: 2000\r\n");
    hasta_ms(590);  pide(1500u, "servo_prueba: 1500\r\n");
    hasta_ms(890);  pide(1505u, "servo_prueba: 1505\r\n");
    hasta_ms(990);  pide(1520u, "servo_prueba: 1520\r\n");
    hasta_ms(1090); pide(2500u, "servo_prueba: 2500\r\n");
    hasta_ms(1390); pide(1000u, "servo_prueba: 1000\r\n");
    hasta_ms(1450); pide(0u,    "servo_prueba: sin senal\r\n");
    hasta_ms(1690); pide(1000u, "servo_prueba: 1000\r\n");
    hasta_ms(1890); pide(200u,  "servo_prueba: 200\r\n");
    hasta_ms(2000);
    vcp("servo_prueba: fin\r\n");
    for (;;) __WFI();
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
