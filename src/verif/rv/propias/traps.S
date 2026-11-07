/* traps.S — Excepciones de modo M: causa, mtval y mepc (E1)
 *
 * Cada caso provoca una excepción y el manejador guarda mcause en s2, mtval
 * en s3 y mepc en s4, y vuelve a la dirección que el caso ha dejado en s1. Se
 * comprueba contra [Privileged 20211203, tabla 3.6 y §3.1.16] y, además, la
 * traza entera se compara con la de Spike (gen_trazas.py): lo que este
 * fichero escribe en los registros es lo que Spike tiene que escribir. */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, manejador
    csrw  mtvec, t0

    /* 1. Ilegal de 32 bits (OP-V sin la extensión V): causa 2, mtval = la
     *    instrucción */
    la    s1, 1f
    .word 0x00000057
1:  CHEQUEA(1, s2, 2)
    CHEQUEA(2, s3, 0x00000057)

    /* 2. c.unimp (los 16 bits a cero): causa 2, mtval = 0 */
    la    s1, 1f
    .half 0x0000
    .half 0x0001          /* c.nop, para seguir alineados a 4 */
1:  CHEQUEA(3, s2, 2)
    CHEQUEA(4, s3, 0)

    /* 3. ecall en M: causa 11, mtval = 0, mepc = la ecall */
    la    s1, 2f
1:  ecall
2:  CHEQUEA(5, s2, 11)
    CHEQUEA(6, s3, 0)
    la    t5, 1b
    li    a0, 7
    bne   s4, t5, termina

    /* 4. ebreak: causa 3, mtval = su PC */
    la    s1, 2f
1:  .option push
    .option norvc
    ebreak
    .option pop
2:  CHEQUEA(8, s2, 3)
    la    t5, 1b
    li    a0, 9
    bne   s3, t5, termina

    /* 5. c.ebreak: causa 3, mtval = su PC */
    la    s1, 2f
1:  c.ebreak
    c.nop
2:  CHEQUEA(10, s2, 3)
    la    t5, 1b
    li    a0, 11
    bne   s3, t5, termina

    /* 6. Carga desalineada: causa 4, mtval = la dirección */
    la    s5, datos
    addi  s5, s5, 1
    la    s1, 1f
    lw    t1, 0(s5)
1:  CHEQUEA(12, s2, 4)
    li    a0, 13
    bne   s3, s5, termina

    /* 7. Almacenamiento desalineado: causa 6 */
    la    s1, 1f
    sh    t1, 0(s5)
1:  CHEQUEA(14, s2, 6)
    li    a0, 15
    bne   s3, s5, termina

    /* 8. Carga de donde no hay nada: causa 5 */
    li    s5, 0x40000000
    la    s1, 1f
    lw    t1, 0(s5)
1:  CHEQUEA(16, s2, 5)
    li    a0, 17
    bne   s3, s5, termina

    /* 9. Almacenamiento donde no hay nada: causa 7 */
    la    s1, 1f
    sw    t1, 4(s5)
1:  CHEQUEA(18, s2, 7)
    addi  s5, s5, 4
    li    a0, 19
    bne   s3, s5, termina

    /* 10. Búsqueda donde no hay nada: causa 1, mtval = mepc = el destino */
    li    s5, 0x40000000
    la    s1, 1f
    jalr  ra, 0(s5)
1:  CHEQUEA(20, s2, 1)
    li    a0, 21
    bne   s3, s5, termina
    li    a0, 22
    bne   s4, s5, termina

    /* 11. Escribir un CSR de solo lectura: causa 2, mtval = la instrucción */
    la    s1, 1f
2:  .option push
    .option norvc
    csrw  mvendorid, t1
    .option pop
1:  CHEQUEA(23, s2, 2)
    la    t5, 2b
    lw    t5, 0(t5)
    li    a0, 24
    bne   s3, t5, termina

    /* 12. Un CSR que no existe (un hpmcounter de supervisor): causa 2 */
    la    s1, 1f
    csrr  t1, 0x105          /* stvec: no hay modo S */
1:  CHEQUEA(25, s2, 2)

    /* 13. AMO desalineada: causa 6 (también en un LR: causa 4) */
    la    s5, datos
    addi  s5, s5, 2
    la    s1, 1f
    amoadd.w t1, t1, (s5)
1:  CHEQUEA(26, s2, 6)
    la    s1, 1f
    lr.w  t1, (s5)
1:  CHEQUEA(27, s2, 4)

    /* 14. mret con MPP = M y MPIE: MIE vuelve, MPIE a 1 */
    li    t1, 0x1880         /* MPP = M, MPIE = 1 */
    csrs  mstatus, t1
    la    t1, 1f
    csrw  mepc, t1
    mret
1:  csrr  t1, mstatus
    andi  t1, t1, 8
    CHEQUEA(28, t1, 8)
    csrci mstatus, 8
    PASA

    .align 2
manejador:
    csrr  s2, mcause
    csrr  s3, mtval
    csrr  s4, mepc
    csrw  mepc, s1
    mret

    .data
    .align 3
datos: .word 0x11223344, 0x55667788
