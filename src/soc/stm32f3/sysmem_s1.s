/* ===========================================================================
 * sysmem_s1.s — Lo que hay en la memoria de sistema del F303 del modelo (S1)
 *
 * La imagen de ST (el cargador de AN2606/AN3155) no se puede distribuir. Hasta
 * que llegue el cargador propio (fase S7), en 0x1FFF_D800 hay esto: una tabla
 * de vectores con su pila y un arranque que deja la marca 'MSYS' en la primera
 * palabra de la SRAM -para que se vea que el chip arranco aqui- y se aparca en
 * `wfe`. Lo ensambla `gen_sysmem.sh` y se versiona el .inc.
 * ===========================================================================*/
    .syntax unified
    .cpu cortex-m4
    .thumb
    .section .text
    .org 0
    .word 0x20001000            /* MSP inicial */
    .word arranque + 1          /* Reset */
    .word bucle + 1             /* NMI */
    .word bucle + 1             /* HardFault */
    .org 0x100
    .thumb_func
arranque:
    ldr   r0, =0x20000000
    ldr   r1, =0x5359534D       /* 'MSYS' */
    str   r1, [r0]
    .thumb_func
bucle:
    wfe
    b     bucle
    .ltorg
