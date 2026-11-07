/* cm_c3.c — Consola y arranque de CoreMark en el ESP32-C3 sin ROM (E3)
 * Como verif/rv/coremark/cm_rv.c: ee_printf() escribe en `cm_console`, que
 * el banco vuelca al acabar; main() pone la CPU a 160 MHz y llama a
 * coremark_main (core_main.c con -Dmain=coremark_main). */
#include <stdint.h>
#include "../c3.h"
#define CM_CONSOLE_MAGIC 0x434F4E53u   /* "CONS" */
volatile struct {
    volatile uint32_t magic;
    volatile uint32_t len;
    volatile char     buf[4096];
} cm_console;
extern int coremark_main(void);
int main(void)
{
    reloj_160();
    cm_console.magic = CM_CONSOLE_MAGIC;
    cm_console.len   = 0;
    buzon.magic = BUZON_MAGIC;
    coremark_main();
    buzon.fin = 1;
    return 0;
}
double modf(double x, double *iptr)
{
    double i = (double)(long long)x;
    if (x >= 0.0) { if (i > x) i -= 1.0; }
    else          { if (i < x) i += 1.0; }
    *iptr = i;
    return x - i;
}
