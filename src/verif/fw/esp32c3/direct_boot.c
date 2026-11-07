/* direct_boot.c — E4: el firmware corre DESDE LA FLASH, por la caché, sin
 * bootloader ni ESP-IDF (Direct Boot, c3_flash.ld). Saluda por la UART0,
 * suma una tabla de constantes que se lee por el bus D (la ventana
 * 0x3C00_0000) y deja en el buzón dónde está ejecutando, la suma y la causa
 * del reset. Es el escalón entre E3 (todo en la SRAM) y ESP-IDF. */
#include "c3.h"
static const uint32_t tabla[64] = {
#define T(i) (0x9E3779B9u * (i) + 0x7F4A7C15u)
    T(0), T(1), T(2), T(3), T(4), T(5), T(6), T(7), T(8), T(9), T(10), T(11), T(12), T(13), T(14), T(15),
    T(16), T(17), T(18), T(19), T(20), T(21), T(22), T(23), T(24), T(25), T(26), T(27), T(28), T(29), T(30), T(31),
    T(32), T(33), T(34), T(35), T(36), T(37), T(38), T(39), T(40), T(41), T(42), T(43), T(44), T(45), T(46), T(47),
    T(48), T(49), T(50), T(51), T(52), T(53), T(54), T(55), T(56), T(57), T(58), T(59), T(60), T(61), T(62), T(63),
};
int main(void) {
    uint32_t pc;
    __asm__ volatile("auipc %0, 0" : "=r"(pc));
    uart_flush(0);                    /* lo que la ROM aún tenga en la FIFO */
    reloj_160();
    uart_init(0);
    uart_puts(0, "Direct Boot: desde la Flash, por la cache\r\n");
    uint32_t s = 0;
    for (unsigned i = 0; i < 64; ++i) s ^= tabla[i] + i;
    buzon.res[0] = pc;
    buzon.res[1] = (uint32_t)(uintptr_t)tabla;
    buzon.res[2] = s;
    buzon.res[3] = RTC_RESET_STATE & 0x3Fu;
    buzon.magic = BUZON_MAGIC;
    uart_flush(0);
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
