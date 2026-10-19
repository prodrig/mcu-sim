/* ===========================================================================
 * cabecera.c — Lo que la ROM sustituta lleva delante de ROM_APITABLE
 *
 * La ROM del MSP432P401R ocupa 0x0200_0000-0x0200_7FFF, y la tabla de tablas
 * esta en 0x0200_0800 [rom.h]. Que hay en los primeros 2 KB del silicio no lo
 * dice ningun documento publico, y ningun firmware lo lee; aqui va una firma
 * legible para que quien vuelque la ventana sepa que esta mirando la ROM R1
 * del simulador y no la de TI, y el hueco al que apuntan las entradas vacias
 * de las tablas.
 * ===========================================================================*/
#include <stdint.h>

__attribute__((section(".cabecera.firma"), used))
const char rom_firma[] =
    "mcu-sim ROM R1: DriverLib (SimpleLink MSP432P4 SDK, BSD-3) compilada con "
    "arm-none-eabi-gcc y colocada con las tablas de rom.h. NO es la ROM de TI: "
    "mismas funciones, en el mismo sitio de la tabla, otras instrucciones. "
    "doc/analisis_msp432.md, opcion R1.";

/* Una entrada de tabla que rom.h no asigna al P401R. Llegar aqui es saltar a
 * una funcion que esta ROM no tiene: se para en un BKPT, que con depurador
 * detiene el nucleo y sin el es un HardFault. */
__attribute__((section(".cabecera.hueco"), used, naked, noreturn))
void rom_hueco(void) {
    __asm volatile("bkpt #0x52\n\tb rom_hueco");
}
