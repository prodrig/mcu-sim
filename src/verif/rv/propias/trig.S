/* trig.S — Triggers mcontrol (tipo 2) con action = 0: excepción de
 * breakpoint (E1)
 *
 * [Debug 0.13.2, §5.2.9]. Se prueban desde U, para que el manejador (en M,
 * con el bit `m` del trigger a cero) no los vuelva a disparar: así no
 * importa si el núcleo tiene tcontrol o no. Se comprueban: ejecución (mtval =
 * PC), carga y almacenamiento (mtval = la dirección del dato, timing «antes»:
 * la carga no escribe rd y el almacenamiento no escribe memoria), match ≥ y
 * <, el bit hit, y que con el bit `u` a cero no salta. Se compara con Spike. */
#include "prueba.h"
#define MC_TIPO2   0x20000000
#define MC_DMODE   0x08000000
#define MC_U       0x8
#define MC_M       0x40
#define MC_EXEC    0x4
#define MC_STORE   0x2
#define MC_LOAD    0x1
#define MC_HIT     0x100000
    .text
    .globl main
main:
    la    t0, manejador
    csrw  mtvec, t0
    /* PMP abierto a U */
    li    t0, -1
    csrw  pmpaddr0, t0
    li    t0, 0x1F
    csrw  pmpcfg0, t0
    /* ¿Cuántos triggers? tselect se queda en el último que existe */
    li    t0, 0
    csrw  tselect, t0
    csrr  t1, tselect
    CHEQUEA(1, t1, 0)
    /* El tipo tras el reset depende de la versión de la especificación (2 en
     * la 0.13.2; 15, «desactivado», en la 1.0, que es lo que hace Spike): se
     * escribe un mcontrol vacío y se comprueba que se queda */
    li    t1, MC_TIPO2
    csrw  tdata1, t1
    csrr  t1, tdata1
    srli  t1, t1, 28
    CHEQUEA(2, t1, 2)                  /* tipo 2 */

    /* 1. Ejecución en U: mcause 3, mtval = mepc = el PC */
    la    t1, objetivo
    csrw  tdata2, t1
    li    t1, MC_TIPO2 | MC_U | MC_EXEC
    csrw  tdata1, t1
    la    s1, 1f
    li    s6, 0
    call  a_u
    call  objetivo
1:  CHEQUEA(3, s2, 3)
    la    t5, objetivo
    li    a0, 4
    bne   s3, t5, termina
    li    a0, 5
    bne   s4, t5, termina
    CHEQUEA(6, s6, 0)                  /* `objetivo` no llegó a ejecutarse */
    csrr  t1, tdata1
    li    t2, MC_HIT
    and   t1, t1, t2
    CHEQUEA(7, t1, MC_HIT)

    /* 2. Carga en U: mtval = la dirección; rd no cambia */
    la    s5, dato
    csrw  tdata2, s5
    li    t1, MC_TIPO2 | MC_U | MC_LOAD
    csrw  tdata1, t1
    li    s7, 0x55
    la    s1, 1f
    call  a_u
    lw    s7, 0(s5)
1:  CHEQUEA(8, s2, 3)
    li    a0, 9
    bne   s3, s5, termina
    CHEQUEA(10, s7, 0x55)

    /* 3. Almacenamiento en U con match ≥ (2): salta en dato+4; la memoria no
     *    cambia */
    li    t1, MC_TIPO2 | MC_U | MC_STORE | (2 << 7)
    csrw  tdata1, t1
    la    s1, 1f
    li    t2, 0x77
    call  a_u
    sw    t2, 4(s5)
1:  CHEQUEA(11, s2, 3)
    addi  t5, s5, 4
    li    a0, 12
    bne   s3, t5, termina
    lw    t1, 4(s5)
    CHEQUEA(13, t1, 0x12345678)

    /* 4. match < (3): un almacenamiento por encima no salta */
    li    t1, MC_TIPO2 | MC_U | MC_STORE | (3 << 7)
    csrw  tdata1, t1
    li    s2, 0
    la    s1, 1f
    call  a_u
    sw    t2, 4(s5)
    ecall
1:  CHEQUEA(14, s2, 8)
    lw    t1, 4(s5)
    CHEQUEA(15, t1, 0x77)

    /* 5. Sin el bit u, en U no salta */
    la    t1, objetivo
    csrw  tdata2, t1
    li    t1, MC_TIPO2 | MC_M | MC_EXEC
    csrw  tdata1, t1
    csrw  tdata1, zero                 /* y lo dejamos apagado para M */
    li    t1, MC_TIPO2 | MC_EXEC
    csrw  tdata1, t1
    li    s2, 0
    li    s6, 0
    la    s1, 1f
    call  a_u
    call  objetivo
    ecall
1:  CHEQUEA(16, s2, 8)
    CHEQUEA(17, s6, 1)

    /* 6. Desde M no se puede poner dmode */
    li    t1, MC_TIPO2 | MC_DMODE | MC_U | MC_EXEC
    csrw  tdata1, t1
    csrr  t1, tdata1
    li    t2, MC_DMODE
    and   t1, t1, t2
    CHEQUEA(18, t1, 0)
    csrw  tdata1, zero
    PASA

objetivo:
    li    s6, 1
    ret

a_u:
    li    t1, 0x1800
    csrc  mstatus, t1
    csrw  mepc, ra
    mret

    .align 2
manejador:
    csrr  s2, mcause
    csrr  s3, mtval
    csrr  s4, mepc
    li    t1, 0x1800
    csrs  mstatus, t1
    csrw  mepc, s1
    mret

    .data
    .align 3
dato: .word 0xAABBCCDD, 0x12345678
