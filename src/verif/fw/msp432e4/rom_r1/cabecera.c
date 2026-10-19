/* ===========================================================================
 * cabecera.c — Lo que la ROM sustituta del MSP432E4 lleva delante de
 *              ROM_APITABLE, y la firma
 *
 * La ROM del E4 empieza en 0x0100_0000 con los cuatro primeros vectores de su
 * cargador de arranque -la pila, el reset, el NMI y el HardFault- y en
 * 0x0100_0010 esta ROM_APITABLE [rom.h; 1F: P-17 b]. Con el alias de arranque
 * (RMCTL.BA = 1) la ROM se ve en 0x0, y el nucleo sale del reset con esos
 * vectores: el reset es el cargador serie de cargador.c.
 *
 * Que hace el NMI o el HardFault de la ROM de TI no lo dice ningun documento
 * publico; aqui van al hueco, que se para en un BKPT.
 * ===========================================================================*/
#include <stdint.h>

extern void cargador_reset(void);
void rom_hueco(void);

/* La pila del cargador: los primeros 16 KB de la SRAM, que nadie mas usa
 * mientras corre (la aplicacion aun no ha arrancado). */
__attribute__((section(".vectores"), used))
const uint32_t rom_vectores[4] = {
    0x20004000u,
    (uint32_t)&cargador_reset,
    (uint32_t)&rom_hueco,
    (uint32_t)&rom_hueco,
};

__attribute__((section(".rodata.firma"), used))
const char rom_firma[] =
    "mcu-sim ROM R1 del MSP432E4: DriverLib (SimpleLink MSP432E4 SDK, BSD-3) "
    "compilada con arm-none-eabi-gcc y colocada con las tablas de rom.h, y un "
    "cargador serie propio (UART0, protocolo de las herramientas de TI). NO es "
    "la ROM de TI: mismas funciones, en el mismo sitio de la tabla, otras "
    "instrucciones. doc/analisis_msp432.md, T6.";

/* Una casilla de tabla sin funcion en esta ROM. Llegar aqui es saltar a una
 * funcion que esta ROM no tiene: se para en un BKPT, que con depurador
 * detiene el nucleo y sin el es un HardFault. */
__attribute__((section(".text.rom_hueco"), used, naked, noreturn))
void rom_hueco(void) {
    __asm volatile("bkpt #0x52\n\tb rom_hueco");
}
