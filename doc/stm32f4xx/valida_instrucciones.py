#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
valida_instrucciones.py — Validación automática de las codificaciones de
informe_instrucciones.md contra un ensamblador ARM real.

Para cada caso de prueba, ensambla la instrucción con GNU as (arm-none-eabi)
en modo Thumb/Cortex-M4F y compara los bytes generados con la codificación
esperada derivada de las tablas del documento (sección indicada en cada caso).

Uso:
    python3 valida_instrucciones.py [-v] [--filtro TEXTO]

Requisitos (uno de los dos):
    * arm-none-eabi-as + arm-none-eabi-objcopy  (binutils-arm-none-eabi /
      GNU Arm Embedded Toolchain; en Windows: añadir su carpeta bin al PATH)
    * llvm-mc (se usa como alternativa si no hay binutils ARM)

Convención del campo "esperado": halfwords en hexadecimal en orden de
programa ("hw1 hw2" para 32 bits, "hw1" para 16 bits), tal como aparecen
en las tablas del documento. El script se encarga del orden little-endian.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# Casos de prueba: (sección del documento, asm, esperado, [n_bytes_a_comparar])
# Si el asm contiene varias instrucciones (etiquetas de salto, bloques IT),
# n_bytes limita la comparación a la primera instrucción.
# ---------------------------------------------------------------------------
C = []  # (seccion, asm, esperado_halfwords, nbytes|None)

def add(seccion, asm, esperado, nbytes=None):
    C.append((seccion, asm, esperado, nbytes))

# ---- 1.1 Desplazamiento inmediato / suma-resta de 3 operandos -------------
add("1.1", "lsls r1, r2, #3",  "00D1")  # 00000 00011 010 001
add("1.1", "lsrs r1, r2, #3",  "08D1")
add("1.1", "asrs r1, r2, #3",  "10D1")
add("1.1", "adds r1, r2, r3",  "18D1")
add("1.1", "subs r1, r2, r3",  "1AD1")
add("1.1", "adds r1, r2, #5",  "1D51")
add("1.1", "subs r1, r2, #5",  "1F51")
# ---- 1.2 MOV/CMP/ADD/SUB imm8 ---------------------------------------------
add("1.2", "movs r3, #42",     "232A")
add("1.2", "cmp r3, #42",      "2B2A")
add("1.2", "adds r3, #42",     "332A")
add("1.2", "subs r3, #42",     "3B2A")
# ---- 1.3 Procesamiento de datos registro-registro -------------------------
add("1.3", "ands r1, r2",      "4011")
add("1.3", "eors r1, r2",      "4051")
add("1.3", "lsls r1, r2",      "4091")
add("1.3", "lsrs r1, r2",      "40D1")
add("1.3", "asrs r1, r2",      "4111")
add("1.3", "adcs r1, r2",      "4151")
add("1.3", "sbcs r1, r2",      "4191")
add("1.3", "rors r1, r2",      "41D1")
add("1.3", "tst r1, r2",       "4211")
add("1.3", "rsbs r1, r2, #0",  "4251")
add("1.3", "cmp r1, r2",       "4291")
add("1.3", "cmn r1, r2",       "42D1")
add("1.3", "orrs r1, r2",      "4311")
add("1.3", "muls r1, r2, r1",  "4351")
add("1.3", "bics r1, r2",      "4391")
add("1.3", "mvns r1, r2",      "43D1")
# ---- 1.4 Datos especiales / BX --------------------------------------------
add("1.4", "add r8, r9",       "44C8")   # 01000100 1 1001 000
add("1.4", "cmp r8, r9",       "45C8")
add("1.4", "mov r8, r9",       "46C8")
add("1.4", "bx lr",            "4770")
add("1.4", "blx r3",           "4798")
# ---- 1.5 Cargas/almacenamientos de 16 bits --------------------------------
add("1.5", "ldr r1, [pc, #16]",   "4904")
add("1.5", "str r1, [r2, r3]",    "50D1")
add("1.5", "strh r1, [r2, r3]",   "52D1")
add("1.5", "strb r1, [r2, r3]",   "54D1")
add("1.5", "ldrsb r1, [r2, r3]",  "56D1")
add("1.5", "ldr r1, [r2, r3]",    "58D1")
add("1.5", "ldrh r1, [r2, r3]",   "5AD1")
add("1.5", "ldrb r1, [r2, r3]",   "5CD1")
add("1.5", "ldrsh r1, [r2, r3]",  "5ED1")
add("1.5", "str r1, [r2, #20]",   "6151")
add("1.5", "ldr r1, [r2, #20]",   "6951")
add("1.5", "strb r1, [r2, #5]",   "7151")
add("1.5", "ldrb r1, [r2, #5]",   "7951")
add("1.5", "strh r1, [r2, #10]",  "8151")
add("1.5", "ldrh r1, [r2, #10]",  "8951")
add("1.5", "str r1, [sp, #16]",   "9104")
add("1.5", "ldr r1, [sp, #16]",   "9904")
# ---- 1.6 Direcciones y SP --------------------------------------------------
add("1.6", "add r1, sp, #16",  "A904")
add("1.6", "add sp, sp, #24",  "B006")
add("1.6", "sub sp, sp, #24",  "B086")
# ---- 1.7 Misceláneas -------------------------------------------------------
add("1.7", "cbz r1, 1f\nnop\nnop\n1:",  "B109", 2)  # offset 2 -> i=0, imm5=1
add("1.7", "cbnz r1, 1f\nnop\nnop\n1:", "B909", 2)
add("1.7", "sxth r1, r2",     "B211")
add("1.7", "sxtb r1, r2",     "B251")
add("1.7", "uxth r1, r2",     "B291")
add("1.7", "uxtb r1, r2",     "B2D1")
add("1.7", "push {r0, r2, lr}",  "B505")
add("1.7", "pop {r1, r3, pc}",   "BD0A")
add("1.7", "cpsid i",         "B672")
add("1.7", "cpsie i",         "B662")
add("1.7", "rev r1, r2",      "BA11")
add("1.7", "rev16 r1, r2",    "BA51")
add("1.7", "revsh r1, r2",    "BAD1")
add("1.7", "bkpt #5",         "BE05")
add("1.7", "it eq\naddeq r0, r0, r1", "BF08", 2)
add("1.7", "nop",             "BF00")
add("1.7", "yield",           "BF10")
add("1.7", "wfe",             "BF20")
add("1.7", "wfi",             "BF30")
add("1.7", "sev",             "BF40")
# ---- 1.8 Múltiples, saltos, SVC -------------------------------------------
add("1.8", "stmia r2!, {r0, r1, r3}",  "C20B")
add("1.8", "ldmia r2!, {r0, r1, r3}",  "CA0B")
add("1.8", "beq 1f\nnop\n1:",  "D000", 2)   # offset 0: beq@0, PC=4, label@4
add("1.8", "svc #5",           "DF05")
add("1.8", "b 1f\nnop\n1:",    "E000", 2)   # offset 0: b@0, PC=4, label@4
add("1.8", "udf #3",           "DE03")
# ---- 2.1 Múltiples 32 bits -------------------------------------------------
add("2.1", "stmia.w r2!, {r4-r9}",     "E8A2 03F0")
add("2.1", "ldmia.w r2!, {r4-r9}",     "E8B2 03F0")
add("2.1", "stmdb r2!, {r4-r9}",       "E922 03F0")
add("2.1", "ldmdb r2!, {r4-r9}",       "E932 03F0")
# ---- 2.2 Exclusivos, dual, tablas -----------------------------------------
add("2.2", "strex r1, r2, [r3, #4]",   "E843 2101")
add("2.2", "ldrex r1, [r3, #4]",       "E853 1F01")
add("2.2", "strd r1, r2, [r3, #8]",    "E9C3 1202")
add("2.2", "ldrd r1, r2, [r3, #8]!",   "E9F3 1202")
add("2.2", "strexb r1, r2, [r3]",      "E8C3 2F41")
add("2.2", "strexh r1, r2, [r3]",      "E8C3 2F51")
add("2.2", "ldrexb r1, [r3]",          "E8D3 1F4F")
add("2.2", "ldrexh r1, [r3]",          "E8D3 1F5F")
add("2.2", "tbb [r1, r2]",             "E8D1 F002")
add("2.2", "tbh [r1, r2, lsl #1]",     "E8D1 F012")
add("2.2", "clrex",                    "F3BF 8F2F")
# ---- 2.3 DP con registro desplazado ---------------------------------------
add("2.3", "ands.w r1, r2, r3, lsl #4", "EA12 1103")
add("2.3", "tst.w r2, r3, lsl #4",      "EA12 1F03")
add("2.3", "bics.w r1, r2, r3",         "EA32 0103")
add("2.3", "orr r1, r2, r3, lsr #8",    "EA42 2113")
add("2.3", "lsl.w r1, r3, #5",          "EA4F 1143")
add("2.3", "orn r1, r2, r3",            "EA62 0103")
add("2.3", "mvn.w r1, r3",              "EA6F 0103")
add("2.3", "eors.w r1, r2, r3",         "EA92 0103")
add("2.3", "teq r2, r3",                "EA92 0F03")
add("2.3", "pkhbt r1, r2, r3, lsl #4",  "EAC2 1103")
add("2.3", "pkhtb r1, r2, r3, asr #4",  "EAC2 1123")
add("2.3", "adds.w r1, r2, r3",         "EB12 0103")
add("2.3", "cmn.w r2, r3",              "EB12 0F03")
add("2.3", "adcs.w r1, r2, r3",         "EB52 0103")
add("2.3", "sbcs.w r1, r2, r3",         "EB72 0103")
add("2.3", "subs.w r1, r2, r3",         "EBB2 0103")
add("2.3", "cmp.w r2, r3",              "EBB2 0F03")
add("2.3", "rsb r1, r2, r3",            "EBC2 0103")
# ---- 3.1 DP con inmediato modificado --------------------------------------
add("3.1", "and r1, r2, #0xFF00FF00",   "F002 21FF")
add("3.1", "movs.w r1, #0xAA",          "F05F 01AA")
add("3.1", "mvn r1, #0x55555555",       "F06F 3155")
add("3.1", "add.w r1, r2, #0x1FE00",    "F502 31FF")
add("3.1", "cmp.w r2, #0x12",           "F1B2 0F12")
add("3.1", "subs.w r1, r2, #0x12",      "F1B2 0112")
# ---- 3.2 Inmediato binario plano ------------------------------------------
add("3.2", "addw r1, r2, #0xABC",       "F602 21BC")
add("3.2", "movw r1, #0xABCD",          "F64A 31CD")
add("3.2", "subw r1, r2, #0xABC",       "F6A2 21BC")
add("3.2", "movt r1, #0xABCD",          "F6CA 31CD")
add("3.2", "ssat r1, #8, r2, lsl #4",   "F302 1107")
add("3.2", "ssat16 r1, #8, r2",         "F322 0107")
add("3.2", "sbfx r1, r2, #5, #7",       "F342 1146")
add("3.2", "bfi r1, r2, #5, #7",        "F362 114B")
add("3.2", "bfc r1, #5, #7",            "F36F 114B")
add("3.2", "usat r1, #8, r2",           "F382 0108")
add("3.2", "ubfx r1, r2, #5, #7",       "F3C2 1146")
# ---- 3.3 Saltos, MSR/MRS, barreras ----------------------------------------
add("3.3", "beq.w 1f\n1:",              "F000 8000", 4)
add("3.3", "b.w 1f\n1:",                "F000 B800", 4)
add("3.3", "bl 1f\n1:",                 "F000 F800", 4)
add("3.3", "mrs r1, primask",           "F3EF 8110")
add("3.3", "msr primask, r1",           "F381 8810")
add("3.3", "dsb sy",                    "F3BF 8F4F")
add("3.3", "dmb sy",                    "F3BF 8F5F")
add("3.3", "isb sy",                    "F3BF 8F6F")
add("3.3", "nop.w",                     "F3AF 8000")
add("3.3", "udf.w #0x1234",             "F7F1 A234")
# ---- 4.1 Carga/almacenamiento simple de 32 bits ---------------------------
add("4.1", "str.w r1, [r2, #0x123]",    "F8C2 1123")
add("4.1", "ldr.w r1, [r2, #0x123]",    "F8D2 1123")
add("4.1", "strb.w r1, [r2, #0x45]",    "F882 1045")
add("4.1", "ldrb.w r1, [r2, #0x45]",    "F892 1045")
add("4.1", "strh.w r1, [r2, #0x45]",    "F8A2 1045")
add("4.1", "ldrh.w r1, [r2, #0x45]",    "F8B2 1045")
add("4.1", "ldrsb.w r1, [r2, #0x45]",   "F992 1045")
add("4.1", "ldrsh.w r1, [r2, #0x45]",   "F9B2 1045")
add("4.1", "str r1, [r2, #-4]",         "F842 1C04")
add("4.1", "ldr r1, [r2], #4",          "F852 1B04")
add("4.1", "str.w r1, [r2, r3, lsl #2]","F842 1023")
add("4.1", "ldr.w r1, [pc, #8]",        "F8DF 1008")
add("4.1", "pld [r2, #8]",              "F892 F008")
# ---- 4.2 DP de registro ----------------------------------------------------
add("4.2", "lsl.w r1, r2, r3",          "FA02 F103")
add("4.2", "lsls.w r1, r2, r3",         "FA12 F103")
add("4.2", "lsr.w r1, r2, r3",          "FA22 F103")
add("4.2", "asr.w r1, r2, r3",          "FA42 F103")
add("4.2", "ror.w r1, r2, r3",          "FA62 F103")
add("4.2", "sxth.w r1, r2",             "FA0F F182")
add("4.2", "sxtah r1, r2, r3, ror #8",  "FA02 F193")
add("4.2", "uxth.w r1, r2",             "FA1F F182")
add("4.2", "sxtb16 r1, r2",             "FA2F F182")
add("4.2", "uxtb16 r1, r2",             "FA3F F182")
add("4.2", "sxtb.w r1, r2",             "FA4F F182")
add("4.2", "uxtb.w r1, r2",             "FA5F F182")
# SIMD paralelas (op1 x prefijo)
add("4.2", "sadd16 r1, r2, r3",         "FA92 F103")
add("4.2", "qadd16 r1, r2, r3",         "FA92 F113")
add("4.2", "shadd16 r1, r2, r3",        "FA92 F123")
add("4.2", "uadd16 r1, r2, r3",         "FA92 F143")
add("4.2", "uqadd16 r1, r2, r3",        "FA92 F153")
add("4.2", "uhadd16 r1, r2, r3",        "FA92 F163")
add("4.2", "sasx r1, r2, r3",           "FAA2 F103")
add("4.2", "ssax r1, r2, r3",           "FAE2 F103")
add("4.2", "ssub16 r1, r2, r3",         "FAD2 F103")
add("4.2", "sadd8 r1, r2, r3",          "FA82 F103")
add("4.2", "ssub8 r1, r2, r3",          "FAC2 F103")
add("4.2", "usub8 r1, r2, r3",          "FAC2 F143")
# Misceláneas y saturantes
add("4.2", "qadd r1, r2, r3",           "FA83 F182")
add("4.2", "qdadd r1, r2, r3",          "FA83 F192")
add("4.2", "qsub r1, r2, r3",           "FA83 F1A2")
add("4.2", "qdsub r1, r2, r3",          "FA83 F1B2")
add("4.2", "rev.w r1, r2",              "FA92 F182")
add("4.2", "rev16.w r1, r2",            "FA92 F192")
add("4.2", "rbit r1, r2",               "FA92 F1A2")
add("4.2", "revsh.w r1, r2",            "FA92 F1B2")
add("4.2", "sel r1, r2, r3",            "FAA2 F183")
add("4.2", "clz r1, r2",                "FAB2 F182")
# ---- 4.3 Multiplicación de 32 bits ----------------------------------------
add("4.3", "mul r1, r2, r3",            "FB02 F103")
add("4.3", "mla r1, r2, r3, r4",        "FB02 4103")
add("4.3", "mls r1, r2, r3, r4",        "FB02 4113")
add("4.3", "smulbb r1, r2, r3",         "FB12 F103")
add("4.3", "smultt r1, r2, r3",         "FB12 F133")
add("4.3", "smlabb r1, r2, r3, r4",     "FB12 4103")
add("4.3", "smuad r1, r2, r3",          "FB22 F103")
add("4.3", "smuadx r1, r2, r3",         "FB22 F113")
add("4.3", "smlad r1, r2, r3, r4",      "FB22 4103")
add("4.3", "smulwb r1, r2, r3",         "FB32 F103")
add("4.3", "smlawb r1, r2, r3, r4",     "FB32 4103")
add("4.3", "smusd r1, r2, r3",          "FB42 F103")
add("4.3", "smlsd r1, r2, r3, r4",      "FB42 4103")
add("4.3", "smmul r1, r2, r3",          "FB52 F103")
add("4.3", "smmulr r1, r2, r3",         "FB52 F113")
add("4.3", "smmla r1, r2, r3, r4",      "FB52 4103")
add("4.3", "smmls r1, r2, r3, r4",      "FB62 4103")
add("4.3", "usad8 r1, r2, r3",          "FB72 F103")
add("4.3", "usada8 r1, r2, r3, r4",     "FB72 4103")
# ---- 4.4 Larga y división --------------------------------------------------
add("4.4", "smull r1, r2, r3, r4",      "FB83 1204")
add("4.4", "sdiv r1, r2, r3",           "FB92 F1F3")
add("4.4", "umull r1, r2, r3, r4",      "FBA3 1204")
add("4.4", "udiv r1, r2, r3",           "FBB2 F1F3")
add("4.4", "smlal r1, r2, r3, r4",      "FBC3 1204")
add("4.4", "smlalbb r1, r2, r3, r4",    "FBC3 1284")
add("4.4", "smlald r1, r2, r3, r4",     "FBC3 12C4")
add("4.4", "smlsld r1, r2, r3, r4",     "FBD3 12C4")
add("4.4", "umlal r1, r2, r3, r4",      "FBE3 1204")
add("4.4", "umaal r1, r2, r3, r4",      "FBE3 1264")
# ---- 5.2 Aritmética FP -----------------------------------------------------
add("5.2", "vmla.f32 s0, s1, s2",       "EE00 0A81")
add("5.2", "vmls.f32 s0, s1, s2",       "EE00 0AC1")
add("5.2", "vnmls.f32 s0, s1, s2",      "EE10 0A81")
add("5.2", "vnmla.f32 s0, s1, s2",      "EE10 0AC1")
add("5.2", "vmul.f32 s0, s1, s2",       "EE20 0A81")
add("5.2", "vnmul.f32 s0, s1, s2",      "EE20 0AC1")
add("5.2", "vadd.f32 s0, s1, s2",       "EE30 0A81")
add("5.2", "vsub.f32 s0, s1, s2",       "EE30 0AC1")
add("5.2", "vdiv.f32 s0, s1, s2",       "EE80 0A81")
add("5.2", "vfnms.f32 s0, s1, s2",      "EE90 0A81")
add("5.2", "vfnma.f32 s0, s1, s2",      "EE90 0AC1")
add("5.2", "vfma.f32 s0, s1, s2",       "EEA0 0A81")
add("5.2", "vfms.f32 s0, s1, s2",       "EEA0 0AC1")
# ---- 5.3 Dos registros / inmediato ----------------------------------------
add("5.3", "vmov.f32 s0, #1.0",         "EEB7 0A00")
add("5.3", "vmov.f32 s0, s1",           "EEB0 0A60")
add("5.3", "vabs.f32 s0, s1",           "EEB0 0AE0")
add("5.3", "vneg.f32 s0, s1",           "EEB1 0A60")
add("5.3", "vsqrt.f32 s0, s1",          "EEB1 0AE0")
add("5.3", "vcmp.f32 s0, s1",           "EEB4 0A60")
add("5.3", "vcmpe.f32 s0, s1",          "EEB4 0AE0")
add("5.3", "vcmp.f32 s0, #0.0",         "EEB5 0A40")
add("5.3", "vcvt.f32.s32 s0, s1",       "EEB8 0AE0")
add("5.3", "vcvt.f32.u32 s0, s1",       "EEB8 0A60")
add("5.3", "vcvt.s32.f32 s0, s1",       "EEBD 0AE0")
add("5.3", "vcvtr.s32.f32 s0, s1",      "EEBD 0A60")
add("5.3", "vcvt.u32.f32 s0, s1",       "EEBC 0AE0")
# ---- 5.4 Transferencias ----------------------------------------------------
add("5.4", "vmov s1, r2",               "EE00 2A90")
add("5.4", "vmov r2, s1",               "EE10 2A90")
add("5.4", "vmov s2, s3, r1, r2",       "EC42 1A11")
add("5.4", "vmov r1, r2, s2, s3",       "EC52 1A11")
add("5.4", "vmrs r1, fpscr",            "EEF1 1A10")
add("5.4", "vmrs APSR_nzcv, fpscr",     "EEF1 FA10")
add("5.4", "vmsr fpscr, r1",            "EEE1 1A10")
# ---- 5.5 Cargas/almacenamientos FP ----------------------------------------
add("5.5", "vldr s1, [r2, #8]",         "EDD2 0A02")
add("5.5", "vstr s1, [r2, #8]",         "EDC2 0A02")
add("5.5", "vldr s1, [r2, #-8]",        "ED52 0A02")
add("5.5", "vpush {s2, s3}",            "ED2D 1A02")
add("5.5", "vpop {s2, s3}",             "ECBD 1A02")
add("5.5", "vldmia r2!, {s2, s3}",      "ECB2 1A02")
add("5.5", "vstmia r2, {s2, s3}",       "EC82 1A02")

# ---------------------------------------------------------------------------
# Backends de ensamblado
# ---------------------------------------------------------------------------
PROLOGO = ".syntax unified\n.cpu cortex-m4\n.fpu fpv4-sp-d16\n.thumb\n.text\n"

def _which(*names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    return None

GNU_AS = _which("arm-none-eabi-as")
GNU_OBJCOPY = _which("arm-none-eabi-objcopy")
LLVM_MC = _which("llvm-mc")

def ensambla_gnu(asm):
    with tempfile.TemporaryDirectory() as td:
        s, o, b = (os.path.join(td, x) for x in ("t.s", "t.o", "t.bin"))
        open(s, "w").write(PROLOGO + asm + "\n")
        r = subprocess.run([GNU_AS, "-mthumb", "-o", o, s],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, r.stderr.strip()
        r = subprocess.run([GNU_OBJCOPY, "-O", "binary", "--only-section=.text", o, b],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, r.stderr.strip()
        return open(b, "rb").read(), None

def ensambla_llvm(asm):
    r = subprocess.run(
        [LLVM_MC, "--triple=thumbv7em-none-eabi", "--mattr=+vfp4d16sp,+dsp",
         "--filetype=obj", "-o", "-"],
        input=(PROLOGO + asm + "\n").encode(), capture_output=True)
    if r.returncode != 0:
        return None, r.stderr.decode().strip()
    # extraer .text del objeto ELF con llvm-objcopy si existe; si no, parse minimo
    objcopy = _which("llvm-objcopy", "objcopy")
    if not objcopy:
        return None, "llvm-objcopy no disponible"
    with tempfile.TemporaryDirectory() as td:
        o, b = os.path.join(td, "t.o"), os.path.join(td, "t.bin")
        open(o, "wb").write(r.stdout)
        r2 = subprocess.run([objcopy, "-O", "binary", "--only-section=.text", o, b],
                            capture_output=True, text=True)
        if r2.returncode != 0:
            return None, r2.stderr.strip()
        return open(b, "rb").read(), None

def esperado_a_bytes(esp):
    out = bytearray()
    for hw in esp.split():
        v = int(hw, 16)
        out += bytes((v & 0xFF, v >> 8))  # halfword little-endian
    return bytes(out)

def hexhw(data):
    """bytes -> 'hw1 hw2 ...' para mostrar."""
    hws = []
    for i in range(0, len(data) - len(data) % 2, 2):
        hws.append("%04X" % (data[i] | (data[i+1] << 8)))
    return " ".join(hws)

def main():
    ap = argparse.ArgumentParser(description="Valida informe_instrucciones.md contra un ensamblador ARM")
    ap.add_argument("-v", "--verbose", action="store_true", help="muestra también los casos correctos")
    ap.add_argument("--filtro", default="", help="ejecuta solo los casos cuyo asm contenga este texto")
    args = ap.parse_args()

    if GNU_AS and GNU_OBJCOPY:
        backend, nombre = ensambla_gnu, "GNU as (%s)" % GNU_AS
    elif LLVM_MC:
        backend, nombre = ensambla_llvm, "llvm-mc (%s)" % LLVM_MC
    else:
        sys.exit("ERROR: no se encontró arm-none-eabi-as ni llvm-mc en el PATH.\n"
                 "Instala GNU Arm Embedded Toolchain o binutils-arm-none-eabi.")

    print("Backend: %s" % nombre)
    ok = fallos = errores = 0
    for seccion, asm, esp, nbytes in C:
        if args.filtro and args.filtro not in asm:
            continue
        data, err = backend(asm)
        etiqueta = asm.split("\n")[0]
        if data is None:
            errores += 1
            print("  [ERROR ] §%-4s %-32s -> no ensambla: %s" % (seccion, etiqueta, err))
            continue
        expb = esperado_a_bytes(esp)
        n = nbytes if nbytes else len(expb)
        got = data[:n]
        if got == expb[:n]:
            ok += 1
            if args.verbose:
                print("  [OK    ] §%-4s %-32s = %s" % (seccion, etiqueta, esp))
        else:
            fallos += 1
            print("  [FALLO ] §%-4s %-32s esperado %s ; ensamblador %s"
                  % (seccion, etiqueta, esp, hexhw(got)))
    total = ok + fallos + errores
    print("\nResultado: %d/%d correctos, %d discrepancias, %d errores de ensamblado"
          % (ok, total, fallos, errores))
    sys.exit(1 if (fallos or errores) else 0)

if __name__ == "__main__":
    main()
