/* ext.S — Las extensiones del fabricante: una instrucción custom-0 y un CSR
 * propio (E1, solo en el laboratorio)
 *
 * El banco engancha al hart una rv_ext_if que hace, con custom-0 (0x0B),
 * funct3 = 0, funct7 = 0: rd = rs1·2 + rs2; y una rv_csr_ext_if con un CSR
 * 0x7C0 de lectura y escritura que devuelve lo que se le escribió y le suma 1
 * después de cada lectura. Sin ellas, la instrucción es ilegal: el banco lo
 * prueba con el mismo ELF, que entonces acaba con el código 2. */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, manejador
    csrw  mtvec, t0
    li    s2, 0
    li    a1, 20
    li    a2, 2
    la    s1, 1f
    .insn r CUSTOM_0, 0, 0, a3, a1, a2
    CHEQUEA(1, a3, 42)
1:  CHEQUEA(2, s2, 0)
    /* funct3 = 1 no lo atiende la extensión: ilegal */
    la    s1, 1f
    .insn r CUSTOM_0, 1, 0, a3, a1, a2
1:  CHEQUEA(3, s2, 2)
    li    s2, 0
    li    t1, 100
    la    s1, 1f
    csrw  0x7C0, t1
    csrr  t2, 0x7C0
    csrr  t2, 0x7C0
    CHEQUEA(4, t2, 101)
1:  CHEQUEA(5, s2, 0)
    PASA

    .align 2
manejador:
    csrr  s2, mcause
    csrw  mepc, s1
    mret
