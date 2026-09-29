/* ===========================================================================
 * main.c — Firmware del puente UART: eco por la USART2 (P-14, fase D2)
 *
 * El lado del MCU de las pruebas de `make testserie`. Usa la cabecera de
 * dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin adaptar nada al
 * modelo, y hace lo que haría un firmware de alumno con el puerto COM
 * virtual de una Nucleo: saluda y devuelve lo que recibe.
 *
 *   USART2, 115200 8N1, PA2 = TX, PA3 = RX, PA1 = RTS (todo AF7)
 *   HSI a 16 MHz, SIN PLL: APB1 = 16 MHz. No hace falta cristal, y a 16 MHz
 *   el modelo ejecuta diez veces menos instrucciones por milisegundo
 *   simulado que a 168, que en un banco que espera tramas se nota.
 *
 * Lo que ejercita del modelo, y por eso lleva cada cosa:
 *
 *   - recepción por interrupción RXNE, con las banderas de error leídas del
 *     SR ANTES del DR, que es la secuencia que las borra [RM0090, 30.6.1];
 *   - CR3.RTSE: el RTS lo mueve el hardware, alto mientras RXNE está puesto.
 *     Es lo que el puente lee en su terminal `cts`;
 *   - CR2.LINEN: un break del puente levanta LBD, que se cuenta;
 *   - la orden 0x13 (DC3, el XOFF de toda la vida) hace que el firmware deje
 *     de leer durante 5 ms. Con el control de flujo del puente, el puente
 *     espera; sin él, la USART se desborda (ORE). Es la prueba de RTS/CTS.
 *
 * Solo devuelve los bytes que llegan SIN error. Los que llegan mal se cuentan
 * y se tiran: así el banco distingue «llegó basura» de «llegó y se devolvió».
 *
 * El buzón en 0x2000 0000 permite al banco leer lo que ha pasado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define BAUD      115200u
#define PCLK1     16000000u
#define ORDEN_PAUSA 0x13u          /* DC3: deja de leer 5 ms */
#define PAUSA_MS  5u

volatile struct {
    volatile uint32_t listo;       /* 1 cuando ha saludado                   */
    volatile uint32_t rx;          /* tramas recibidas (buenas y malas)      */
    volatile uint32_t eco;         /* bytes devueltos                        */
    volatile uint32_t fe;          /* errores de trama                       */
    volatile uint32_t ne;          /* ruido                                  */
    volatile uint32_t pe;          /* paridad                                */
    volatile uint32_t ore;         /* desbordamientos                        */
    volatile uint32_t lbd;         /* breaks LIN detectados                  */
    volatile uint32_t pausas;      /* órdenes 0x13 atendidas                 */
    volatile uint32_t brr;         /* el BRR que ha calculado                */
    volatile uint32_t ultimo;      /* el último byte leído del DR            */
    volatile uint32_t ms;          /* milisegundos desde el arranque         */
} mbox __attribute__((section(".mailbox")));

static volatile uint8_t  cola[256];
static volatile uint32_t esc, lec;

void SysTick_Handler(void) { ++mbox.ms; }

void USART2_IRQHandler(void)
{
    const uint32_t sr = USART2->SR;             /* primero el SR...          */
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        const uint8_t c = (uint8_t)USART2->DR;  /* ...y después el DR        */
        ++mbox.rx;
        mbox.ultimo = c;
        if (sr & USART_SR_FE)  ++mbox.fe;
        if (sr & USART_SR_NE)  ++mbox.ne;
        if (sr & USART_SR_PE)  ++mbox.pe;
        if (sr & USART_SR_ORE) ++mbox.ore;
        if ((sr & (USART_SR_FE | USART_SR_NE | USART_SR_PE)) == 0u) {
            const uint32_t sig = (esc + 1u) & 0xFFu;
            if (sig != lec) { cola[esc] = c; esc = sig; }
        }
    }
}

static void pin_af7(uint32_t pin)
{
    GPIOA->MODER   = (GPIOA->MODER   & ~(3u << (2 * pin))) | (2u << (2 * pin));
    GPIOA->OTYPER &= ~(1u << pin);
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~(3u << (2 * pin))) | (3u << (2 * pin));
    GPIOA->PUPDR   = (GPIOA->PUPDR   & ~(3u << (2 * pin))) | (1u << (2 * pin));
    GPIOA->AFR[0]  = (GPIOA->AFR[0]  & ~(0xFu << (4 * pin))) | (7u << (4 * pin));
}

/* BRR con divisor fraccionario, tal como lo calcula el driver de ST */
static uint32_t brr_for(uint32_t pclk, uint32_t baud)
{
    const uint32_t div100 = (25u * pclk) / (4u * baud);
    const uint32_t mant   = div100 / 100u;
    const uint32_t frac   = (((div100 - mant * 100u) * 16u) + 50u) / 100u;
    return (mant << 4) | (frac & 0xFu);
}

static void putc2(uint8_t c)
{
    while ((USART2->SR & USART_SR_TXE) == 0u) { }
    USART2->DR = c;
}

static void puts2(const char *s) { while (*s) putc2((uint8_t)*s++); }

int main(void)
{
    mbox.listo = 0u; mbox.rx = 0u; mbox.eco = 0u; mbox.fe = 0u; mbox.ne = 0u;
    mbox.pe = 0u; mbox.ore = 0u; mbox.lbd = 0u; mbox.pausas = 0u;
    mbox.ultimo = 0u; mbox.ms = 0u;

    SysTick_Config(PCLK1 / 1000u);              /* HSI: HCLK = PCLK1 = 16 MHz */

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    pin_af7(1u); pin_af7(2u); pin_af7(3u);      /* RTS, TX, RX               */

    mbox.brr = brr_for(PCLK1, BAUD);
    USART2->CR1 = 0u;
    USART2->BRR = mbox.brr;
    USART2->CR2 = USART_CR2_LINEN;              /* detección de break        */
    USART2->CR3 = USART_CR3_RTSE;               /* RTS por hardware          */
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    NVIC_EnableIRQ(USART2_IRQn);

    puts2("vcp_demo listo\r\n");
    mbox.listo = 1u;

    for (;;) {
        if (USART2->SR & USART_SR_LBD) {
            USART2->SR = ~USART_SR_LBD;         /* rc_w0: solo se borra LBD  */
            ++mbox.lbd;
        }
        if (lec == esc) { __WFI(); continue; }
        const uint8_t c = cola[lec];
        lec = (lec + 1u) & 0xFFu;
        if (c == ORDEN_PAUSA) {
            /* Dejar de leer: sin RXNEIE nadie saca el DR, RXNE se queda
             * puesto y el hardware sube RTS. */
            USART2->CR1 &= ~USART_CR1_RXNEIE;
            const uint32_t t0 = mbox.ms;
            while ((mbox.ms - t0) < PAUSA_MS) { }
            USART2->CR1 |= USART_CR1_RXNEIE;
            ++mbox.pausas;
            continue;
        }
        putc2(c);
        ++mbox.eco;
    }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
