/* uart.c — E9: la UART0 saluda por GPIO16 a 115200 8N1 y hace eco de lo que
 * le llega por GPIO17 (en mayúsculas), con la interrupción de RX por la
 * matriz y el INTPRI (línea 5, prioridad 2; mie.5). La causa del reset y los
 * pines de arranque, en el buzón. */
#include "c6.h"
static volatile uint32_t n_rx;
void isr_5(void) {
    while (UART_STATUS(0) & 0xFFu) {
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
    buzon.res[0] = LP_CLKRST_RESET_CAUSE & 0x1Fu;
    buzon.res[1] = GPIO_STRAP;
    buzon.res[3] = UART_DATE(0);
    uart_puts(0, "Hola desde el ESP32-C6\r\n");
    /* RX: umbral 1 y espera de 10 bits (TOUT_CONF) */
    UART_CONF1(0) = (UART_CONF1(0) & ~0xFFu) | 1u;
    UART_TOUT_CONF(0) = 1u | (10u << 2);
    UART_INT_CLR(0) = 0xFFFFFFFFu;
    UART_INT_ENA(0) = 1u | (1u << 8);
    irq_off();
    INT_MAP(SRC_UART0) = 5;
    INT_PRI(5) = 2;
    INT_ENABLE |= 1u << 5;
    CSR_S(mie, 1u << 5);
    fence();
    irq_on();
    for (;;) { buzon.res[2] = n_rx; __asm__ volatile("wfi"); }
}
