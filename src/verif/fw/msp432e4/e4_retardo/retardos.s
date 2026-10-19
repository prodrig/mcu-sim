/* ===========================================================================
 * retardos.s — El bucle de SysCtlDelay (subs/bne/bx), en cuatro sitios
 *
 * Para e4_retardo: el mismo bucle colocado a mano respecto a las lineas de
 * 256 bits de la Flash, y uno en la SRAM.
 * ===========================================================================*/
    .syntax unified
    .thumb

    .section .text.retardos,"ax"
    .balign 32
    .global d_alineado
    .thumb_func
d_alineado:            @ todo el bucle al principio de una linea
    subs r0, #1
    bne d_alineado
    bx lr

    .balign 32
    .space 28
    .global d_cruza28
    .thumb_func
d_cruza28:             @ el bucle en la misma linea, pero la busqueda adelantada
    subs r0, #1        @ del nucleo ya toca la siguiente
    bne d_cruza28
    bx lr

    .balign 32
    .space 30
    .global d_cruza30
    .thumb_func
d_cruza30:             @ subs al final de una linea, bne en la siguiente
    subs r0, #1
    bne d_cruza30
    bx lr

    .section .data.retardo_ram,"aw"
    .balign 4
    .global d_ram
    .type d_ram, %function     @ si no, en una seccion de datos no lleva el bit de Thumb
    .thumb_func
d_ram:                 @ en la SRAM: sin estados de espera
    subs r0, #1
    bne d_ram
    bx lr
