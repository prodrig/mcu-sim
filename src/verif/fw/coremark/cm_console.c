/* ===========================================================================
 * cm_console.c — Consola virtual y arranque de CoreMark en el modelo
 *
 * CoreMark escribe su salida con ee_printf(); aquí se implementa el destino:
 * un buzón en la SRAM que el banco de pruebas SystemC vuelca por pantalla al
 * terminar la simulación.
 *
 * No corresponde a hardware del STM32F407VG: es la instrumentación mínima que
 * necesita un benchmark bare-metal. En la fase F4, cuando el USART esté
 * implementado, este destino se sustituirá por USART1.
 * ===========================================================================*/
#include <stdint.h>

#define CM_CONSOLE_MAGIC 0x434F4E53u   /* "CONS" */

volatile struct {
    volatile uint32_t magic;
    volatile uint32_t len;
    volatile char     buf[4096];
} cm_console __attribute__((section(".mailbox")));

/* Marca de finalización que el banco de pruebas espera */
volatile uint32_t cm_done __attribute__((section(".mailbox")));

extern int  main(void);
extern void coremark_timer_init(void);

/* El manejador de reset llama a cm_start en lugar de a main directamente */
void cm_start(void)
{
    cm_console.magic = CM_CONSOLE_MAGIC;
    cm_console.len   = 0;
    cm_done          = 0;
    main();
    cm_done = 1;
    for (;;) { __asm volatile("wfi"); }
}

/* ---------------------------------------------------------------------------
 * modf() para el port bare-metal: la usa cvt.c de CoreMark al formatear los
 * dobles del informe de resultados. Se implementa aquí para no arrastrar la
 * biblioteca matemática completa (el enlazado es -nostdlib).
 * -------------------------------------------------------------------------*/
double modf(double x, double *iptr)
{
    double i;
    if (x >= 0.0) {
        i = (double)(long long)x;
        if (i > x) i -= 1.0;
    } else {
        i = (double)(long long)x;
        if (i < x) i += 1.0;
    }
    *iptr = i;
    return x - i;
}
