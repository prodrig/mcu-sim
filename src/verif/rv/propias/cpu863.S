/* cpu863.S — Accesos desalineados en hardware y la errata CPU-863 del
 * ESP32-C6 (E1, solo en el laboratorio, con RvDesalineado::HardwareCpu863)
 *
 * Una carga desalineada que cruza una frontera de palabra, seguida de un
 * almacenamiento a una región PMP distinta de la de la segunda palabra,
 * falla con un error de acceso a la carga (causa 5) [errata del ESP32-C6,
 * CPU-863]. Si el almacenamiento cae en la misma región, o la carga no cruza
 * la palabra, o el almacenamiento usa el resultado de la carga, no pasa
 * nada. No se compara con Spike (Spike no tiene la errata). */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, manejador
    csrw  mtvec, t0
    /* PMP: 0 = zona_a (64 B, RW), 1 = todo lo demás (RWX) */
    la    t0, zona_a
    srli  t0, t0, 2
    ori   t0, t0, 7
    csrw  pmpaddr0, t0
    li    t0, -1
    csrw  pmpaddr1, t0
    li    t0, 0x1F1B
    csrw  pmpcfg0, t0

    /* 1. Carga desalineada sin almacenamiento detrás: va bien en hardware */
    la    s5, zona_a
    lw    t1, 1(s5)
    li    t2, 0x44332211 >> 8 | 0x55 << 24
    li    a0, 1
    bne   t1, t2, termina

    /* 2. Cruza a la palabra siguiente (misma región) y almacena en otra
     *    región: falla con causa 5 y mtval = la dirección de la carga */
    la    s6, zona_b
    addi  s7, s5, 3
    li    s2, 0
    la    s1, 1f
    lw    t1, 0(s7)
    sw    t1, 0(s6)
1:  CHEQUEA(2, s2, 5)
    li    a0, 3
    bne   s3, s7, termina

    /* 3. Igual, pero el almacenamiento en la misma región: va bien */
    li    s2, 0
    lw    t1, 0(s7)
    sw    t1, 16(s5)
    CHEQUEA(4, s2, 0)

    /* 4. La carga no cruza (lh en +1): va bien */
    lh    t1, 1(s5)
    sw    t1, 0(s6)
    CHEQUEA(5, s2, 0)

    /* 5. El almacenamiento depende de la carga (base = rd): va bien */
    la    t4, zona_b
    sw    t4, 19(s5)
    addi  s8, s5, 19
    lw    t3, 0(s8)
    sw    t1, 0(t3)
    CHEQUEA(6, s2, 0)

    /* 6. Un almacenamiento desalineado también va bien */
    sw    t1, 1(s6)
    lw    t2, 1(s6)
    li    a0, 7
    bne   t1, t2, termina
    PASA

    .align 2
manejador:
    csrr  s2, mcause
    csrr  s3, mtval
    csrw  mepc, s1
    mret

    .data
    .align 6
zona_a: .word 0x44332211, 0x88776655
    .fill 14, 4, 0
zona_b: .fill 4, 4, 0
