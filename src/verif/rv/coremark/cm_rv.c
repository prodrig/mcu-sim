/* ===========================================================================
 * cm_rv.c — Consola y arranque de CoreMark en el SoC de laboratorio RISC-V
 *
 * Lo mismo que verif/fw/coremark/cm_console.c, para el laboratorio de E1
 * (doc/analisis_esp32_riscv.md): ee_printf() escribe en el buzón
 * `cm_console` de la RAM, que el banco vuelca al terminar, y main() vuelve
 * a rt/inicio.S, que acaba por HTIF. core_main.c se compila con
 * -Dmain=coremark_main.
 * ===========================================================================*/
#include <stdint.h>

#define CM_CONSOLE_MAGIC 0x434F4E53u   /* "CONS" */

volatile struct {
    volatile uint32_t magic;
    volatile uint32_t len;
    volatile char     buf[4096];
} cm_console __attribute__((section(".mailbox")));

extern int coremark_main(void);

int main(void)
{
    cm_console.magic = CM_CONSOLE_MAGIC;
    cm_console.len   = 0;
    coremark_main();
    return 0;
}

/* modf() para cvt.c: el enlazado es -nostdlib (ver cm_console.c) */
double modf(double x, double *iptr)
{
    double i = (double)(long long)x;
    if (x >= 0.0) { if (i > x) i -= 1.0; }
    else          { if (i < x) i += 1.0; }
    *iptr = i;
    return x - i;
}
