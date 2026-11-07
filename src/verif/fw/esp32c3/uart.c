/* uart.c — E3: la UART0 saluda por GPIO21 a 115200 8N1 y hace eco de lo que
 * le llega por GPIO20 (en mayúsculas), con la interrupción de RX por el INTC
 * (línea 3, prioridad 2). La causa del reset y los pines de arranque, en el
 * buzón. */
#include "c3.h"
static volatile uint32_t n_rx;
void isr_3(void) {
    while (UART_STATUS(0) & 0x3FFu) {
        char c = (char)UART_FIFO(0);
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        uart_putc(0, c);
        ++n_rx;
    }
    UART_INT_CLR(0) = 0x1FFu;
}
int main(void) {
    reloj_160();
    uart_init(0);
    buzon.magic = BUZON_MAGIC;
    buzon.res[0] = RTC_RESET_STATE & 0x3Fu;
    buzon.res[1] = GPIO_STRAP;
    uart_puts(0, "Hola desde el ESP32-C3\r\n");
    /* RX: umbral 1 y espera de 10 bits */
    UART_CONF1(0) = (UART_CONF1(0) & ~0x1FFu) | 1u | (1u << 21);
    UART_INT_CLR(0) = 0xFFFFFFFFu;
    UART_INT_ENA(0) = 1u | (1u << 8);
    irq_off();
    INT_MAP(SRC_UART0) = 3;
    INT_PRI(3) = 2;
    INT_ENABLE |= 1u << 3;
    fence();
    irq_on();
    for (;;) { buzon.res[2] = n_rx; __asm__ volatile("wfi"); }
}
