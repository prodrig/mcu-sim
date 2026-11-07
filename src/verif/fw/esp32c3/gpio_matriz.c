/* gpio_matriz.c — E3: la matriz GPIO. El banco une GPIO4 con GPIO5 y GPIO6
 * con GPIO7 por fuera.
 *   1. La UART1 sale por GPIO4 (OUT_SEL = U1TXD, 9) y entra por GPIO5
 *      (U1RXD_in, 9, IN_SEL = 5): lo que manda vuelve.
 *   2. La GPIO dedicada: cpu_gpio_out0 (señal 28) por GPIO6 y cpu_gpio_in0
 *      desde GPIO7, por los CSR cpu_gpio_* (0x803-0x805).
 *   3. La interrupción de pin: subida en GPIO7 (fuente 16, línea 8).
 *   4. Entradas constantes: IN_SEL 0x1E da 1 y 0x1F da 0; INV la invierte. */
#include "c3.h"
static volatile uint32_t n_gpio_int;
void isr_8(void) {
    if (GPIO_STATUS & (1u << 7)) ++n_gpio_int;
    GPIO_STATUS_W1TC = 0xFFFFFFFFu;
}
int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    /* 1. UART1 por la matriz */
    uart_init(1);
    IO_MUX(4) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    IO_MUX(5) = IOMUX_GPIO | IOMUX_FUN_IE | IOMUX_FUN_WPU | (2u << 10);
    GPIO_FUNC_OUT(4) = 9;                     /* U1TXD, OE del periférico */
    GPIO_FUNC_IN(9) = 5 | (1u << 6);          /* U1RXD desde GPIO5 */
    const char *m = "matriz";
    for (const char *p = m; *p; ++p) uart_putc(1, *p);
    uart_flush(1);
    char r[8] = {0}; int n = 0;
    while ((UART_STATUS(1) & 0x3FFu) && n < 7) r[n++] = (char)UART_FIFO(1);
    buzon.res[0] = (uint32_t)n;
    buzon.res[1] = (uint32_t)(r[0] | (r[1] << 8) | (r[2] << 16) | (r[5] << 24));
    /* 2. GPIO dedicada */
    IO_MUX(6) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    IO_MUX(7) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    GPIO_FUNC_OUT(6) = 28;                    /* cpu_gpio_out0, OE = cpu_gpio_out_oen0 */
    GPIO_FUNC_IN(28) = 7 | (1u << 6);         /* cpu_gpio_in0 desde GPIO7 */
    CSR_W(0x803, 1u);                         /* cpu_gpio_oen */
    CSR_W(0x805, 1u);                         /* cpu_gpio_out */
    for (volatile int i = 0; i < 20; ++i) {}
    uint32_t a = CSR_R(0x804) & 1u;
    uint32_t a_in = (GPIO_IN >> 7) & 1u;
    CSR_W(0x805, 0u);
    for (volatile int i = 0; i < 20; ++i) {}
    uint32_t b = CSR_R(0x804) & 1u;
    buzon.res[2] = a | (b << 1) | (a_in << 2);
    /* 3. Interrupción de pin: subida en GPIO7 */
    irq_off();
    GPIO_PIN(7) = (1u << 7) | (1u << 13);     /* INT_TYPE = subida, INT_ENA = CPU */
    INT_MAP(SRC_GPIO) = 8; INT_PRI(8) = 3; INT_THRESH = 1;
    INT_ENABLE |= 1u << 8;
    fence();
    irq_on();
    for (int k = 0; k < 3; ++k) {
        CSR_W(0x805, 1u); for (volatile int i = 0; i < 20; ++i) {}
        CSR_W(0x805, 0u); for (volatile int i = 0; i < 20; ++i) {}
    }
    irq_off();
    buzon.res[3] = n_gpio_int;
    /* 4. Entradas constantes */
    GPIO_FUNC_IN(28) = 0x1E | (1u << 6);
    for (volatile int i = 0; i < 5; ++i) {}
    uint32_t c1 = CSR_R(0x804) & 1u;
    GPIO_FUNC_IN(28) = 0x1F | (1u << 6);
    for (volatile int i = 0; i < 5; ++i) {}
    uint32_t c0 = CSR_R(0x804) & 1u;
    GPIO_FUNC_IN(28) = 0x1F | (1u << 6) | (1u << 5);
    for (volatile int i = 0; i < 5; ++i) {}
    uint32_t ci = CSR_R(0x804) & 1u;
    buzon.res[4] = c1 | (c0 << 1) | (ci << 2);
    /* 5. Pull-up y pull-down del IO_MUX en un pin suelto (GPIO10) */
    IO_MUX(10) = IOMUX_GPIO | IOMUX_FUN_IE | IOMUX_FUN_WPU;
    for (volatile int i = 0; i < 20; ++i) {}
    uint32_t pu = (GPIO_IN >> 10) & 1u;
    IO_MUX(10) = IOMUX_GPIO | IOMUX_FUN_IE | IOMUX_FUN_WPD;
    for (volatile int i = 0; i < 20; ++i) {}
    uint32_t pd = (GPIO_IN >> 10) & 1u;
    buzon.res[5] = pu | (pd << 1);
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
