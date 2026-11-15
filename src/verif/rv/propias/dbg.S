/* dbg.S — El programa que se depura en E2 (no se comprueba solo)
 *
 * Hart 0: un bucle que cuenta en s0 y en `contador`, llama a `objetivo` en
 * cada vuelta (donde van los puntos de ruptura) y escribe la cuenta en
 * `vigilada` cada ocho vueltas (donde va el watchpoint). Hart 1 (si lo hay):
 * otro bucle que cuenta en `contador1`. Ninguno acaba. */
#include "prueba.h"
    .text
    .globl main
main:
    li    s0, 0
    la    s1, contador
    la    s2, vigilada
1:  addi  s0, s0, 1
    sw    s0, 0(s1)
    call  objetivo
    andi  t0, s0, 7
    bnez  t0, 1b
    sw    s0, 0(s2)
    j     1b

    .globl objetivo
    .align 2
objetivo:
    addi  a0, a0, 1
    ret

    .globl otro_hart
otro_hart:
    la    t1, contador1
2:  lw    t2, 0(t1)
    addi  t2, t2, 1
    sw    t2, 0(t1)
    j     2b

    .data
    .align 4
    .globl contador, contador1, vigilada
contador:  .word 0
contador1: .word 0
vigilada:  .word 0
