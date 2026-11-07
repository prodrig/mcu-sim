/* vec.S — mtvec: sus modos, su legalización, y el modo vectorizado con
 * excepciones (E1)
 *
 * [Privileged 20211203, §3.1.7]: en modo vectorizado las excepciones van a la
 * base y las interrupciones a base + 4·causa. Los modos 2 y 3 están
 * reservados; este núcleo, como Spike, los convierte en 0 y 1. Se compara
 * con Spike. */
#include "prueba.h"
    .text
    .globl main
main:
    /* 1. Modo 2 y modo 3 se leen como 0 y 1 */
    la    t0, tabla
    ori   t1, t0, 2
    csrw  mtvec, t1
    csrr  t2, mtvec
    li    a0, 1
    bne   t2, t0, termina
    ori   t1, t0, 3
    csrw  mtvec, t1
    csrr  t2, mtvec
    ori   t1, t0, 1
    li    a0, 2
    bne   t2, t1, termina

    /* 2. Vectorizado: una ecall va a la base */
    li    s2, 0
    la    s1, 1f
    ecall
1:  CHEQUEA(3, s2, 11)

    /* 3. mepc: el bit 0 no se puede escribir */
    li    t1, 0x80000003
    csrw  mepc, t1
    csrr  t2, mepc
    li    t1, 0x80000002
    li    a0, 4
    bne   t2, t1, termina

    /* 4. mscratch y mcause guardan lo que se les escribe */
    li    t1, 0xCAFEBABE
    csrw  mscratch, t1
    csrr  t2, mscratch
    li    a0, 5
    bne   t2, t1, termina
    PASA

    .option push
    .option norvc
    .align 8
tabla:
    j     excepcion
    .fill 31, 4, 0x00000013     /* nops: ninguna interrupción esperada */
    .option pop
excepcion:
    csrr  s2, mcause
    csrw  mepc, s1
    mret
