/* g2_bsl_app.S — la aplicación que el banco (G9) carga con el BSL de usuario
 * de la G2xx (g2_bsl_usuario.S): empieza en 0xF800, donde salta el BSL, y
 * hace parpadear el LED verde (P1.6) en lugar del rojo de la que trae. Cada
 * vuelta suma uno en 0x0200 (RAM), para ver que corre. AGPLv3, de mcu-sim. */
#include <msp430.h>
        .section .text
        .global main
main:   mov.w   #WDTPW+WDTHOLD, &WDTCTL
        mov.w   #0x0280, SP
        bis.b   #BIT6, &P1DIR
        bic.b   #BIT0, &P1OUT
        bis.b   #BIT0, &P1DIR
        clr.w   &0x0200
vuelta: xor.b   #BIT6, &P1OUT
        inc.w   &0x0200
        mov.w   #10000, R15
espera: dec.w   R15
        jnz     espera
        jmp     vuelta
        .section .vector_reset, "a", @progbits
        .word   0xFFFF                       /* el BSL no la escribe: ahí va la suma */
