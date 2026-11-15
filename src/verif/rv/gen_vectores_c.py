#!/usr/bin/env python3
# =============================================================================
# gen_vectores_c.py — Vectores de expansión de las instrucciones comprimidas
#
# E1 de doc/analisis_esp32_riscv.md. El oráculo es el ensamblador de GNU: cada
# instrucción se ensambla dos veces, con `.option rvc` (sale la comprimida) y
# con `.option norvc` (sale la de 32 bits a la que equivale), y el par es un
# vector. Así `rvd::expande_c` no se contrasta con una tabla escrita a mano,
# que tendría los mismos errores que el código, sino con una implementación
# independiente. Se añaden las codificaciones RESERVADAS de RV32C, que
# `expande_c` tiene que rechazar (expansión 0 en la tabla).
#
#   python3 gen_vectores_c.py [--cc riscv64-unknown-elf-gcc] > vectores_c.inc
#
# Es determinista (semilla fija): regenerar no cambia el fichero.
# =============================================================================
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

R = random.Random(20261107)
REGS = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1",
        "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"]
RP = REGS[8:16]           # los registros x8-x15 de las formas «prima»
NZ = [r for r in REGS if r != "zero"]


def pares():
    """Instrucciones (en su forma de 32 bits) que la ensambladora comprime."""
    v = []
    for _ in range(12):
        v.append(f"addi {R.choice(RP)}, sp, {4 * R.randint(1, 255)}")
        v.append(f"lw {R.choice(RP)}, {4 * R.randint(0, 31)}({R.choice(RP)})")
        v.append(f"sw {R.choice(RP)}, {4 * R.randint(0, 31)}({R.choice(RP)})")
        r = R.choice(NZ)
        v.append(f"addi {r}, {r}, {R.choice([i for i in range(-32, 32) if i])}")
        v.append(f"jal ra, .{R.randrange(-2048, 2048, 2):+d}")
        v.append(f"li {R.choice(NZ)}, {R.randint(-32, 31)}")
        v.append(f"addi sp, sp, {16 * R.choice([i for i in range(-32, 32) if i])}")
        r = R.choice([x for x in NZ if x != "sp"])
        v.append(f"lui {r}, {R.choice(list(range(1, 32)) + list(range(0xFFFE0, 0x100000)))}")
        r = R.choice(RP)
        v.append(f"srli {r}, {r}, {R.randint(1, 31)}")
        r = R.choice(RP)
        v.append(f"srai {r}, {r}, {R.randint(1, 31)}")
        r = R.choice(RP)
        v.append(f"andi {r}, {r}, {R.randint(-32, 31)}")
        for op in ("sub", "xor", "or", "and"):
            r = R.choice(RP)
            v.append(f"{op} {r}, {r}, {R.choice(RP)}")
        v.append(f"jal zero, .{R.randrange(-2048, 2048, 2):+d}")
        v.append(f"beq {R.choice(RP)}, zero, .{R.randrange(-256, 256, 2):+d}")
        v.append(f"bne {R.choice(RP)}, zero, .{R.randrange(-256, 256, 2):+d}")
        r = R.choice(NZ)
        v.append(f"slli {r}, {r}, {R.randint(1, 31)}")
        v.append(f"lw {R.choice(NZ)}, {4 * R.randint(0, 63)}(sp)")
        v.append(f"jalr zero, 0({R.choice(NZ)})")
        v.append(f"add {R.choice(NZ)}, zero, {R.choice(NZ)}")
        v.append(f"jalr ra, 0({R.choice(NZ)})")
        r = R.choice(NZ)
        v.append(f"add {r}, {r}, {R.choice(NZ)}")
        v.append(f"sw {R.choice(REGS)}, {4 * R.randint(0, 63)}(sp)")
    v += ["addi zero, zero, 0", "ebreak"]
    return v


# Reservadas o no definidas en RV32C (sin F ni D) [Unpriv. 20191213, §16.8]
RESERVADAS = [
    (0x0000, "c.addi4spn con nzuimm = 0 (todo ceros)"),
    (0x0004, "c.addi4spn con nzuimm = 0"),
    (0x6101, "c.addi16sp con nzimm = 0"),
    (0x6081, "c.lui con nzimm = 0"),
    (0x4002, "c.lwsp con rd = 0"),
    (0x8002, "c.jr con rs1 = 0"),
    (0x1082, "c.slli con shamt[5] = 1 (RV32)"),
    (0x8001 | (1 << 12), "c.srli con shamt[5] = 1 (RV32)"),
    (0x8401 | (1 << 12), "c.srai con shamt[5] = 1 (RV32)"),
    (0x9C01, "c.subw (solo RV64)"),
    (0x9C21, "c.addw (solo RV64)"),
    (0x9C41, "reservada (CA, funct2 = 2, bit 12 = 1)"),
    (0x9C61, "reservada (CA, funct2 = 3, bit 12 = 1)"),
    (0x2000, "c.fld sin D"),
    (0x6000, "c.flw sin F"),
    (0xA000, "c.fsd sin D"),
    (0xE000, "c.fsw sin F"),
    (0x8000, "reservada (C0, funct3 = 4)"),
    (0x2002, "c.fldsp sin D"),
    (0x6002, "c.flwsp sin F"),
    (0xA002, "c.fsdsp sin D"),
    (0xE002, "c.fswsp sin F"),
]


def ensambla(cc, lineas, rvc):
    with tempfile.TemporaryDirectory() as d:
        s = os.path.join(d, "v.S")
        o = os.path.join(d, "v.o")
        with open(s, "w") as f:
            f.write(".option norelax\n.option %s\n" % ("rvc" if rvc else "norvc"))
            for ln in lineas:
                f.write(f"{ln}\n")
        subprocess.run([cc, "-march=rv32imac_zicsr", "-mabi=ilp32", "-c", s, "-o", o],
                       check=True)
        objdump = cc[:-3] + "objdump" if cc.endswith("gcc") else "riscv64-unknown-elf-objdump"
        out = subprocess.run([objdump, "-d", "-M", "no-aliases", o], check=True,
                             capture_output=True, text=True).stdout
    r = []
    for m in re.finditer(r"^\s*[0-9a-f]+:\s+([0-9a-f]{4,8})\s", out, re.M):
        r.append(int(m.group(1), 16) if len(m.group(1)) == 4 else (int(m.group(1), 16), 32))
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default="riscv64-unknown-elf-gcc")
    a = ap.parse_args()
    ins = pares()
    c = ensambla(a.cc, ins, True)
    n = ensambla(a.cc, ins, False)
    if len(c) != len(ins) or len(n) != len(ins):
        sys.exit("gen_vectores_c: no cuadran las instrucciones ensambladas")
    vistos = set()
    filas = []
    for txt, cc_, nn in zip(ins, c, n):
        if isinstance(cc_, tuple):       # no se ha comprimido: no es un vector
            continue
        assert isinstance(nn, tuple)
        if cc_ in vistos:
            continue
        vistos.add(cc_)
        filas.append((cc_, nn[0], txt))
    print("// vectores_c.inc — GENERADO por verif/rv/gen_vectores_c.py; no editar.")
    print("// {comprimida, expansión (0 = reservada: expande_c tiene que rechazarla)}")
    print(f"// {len(filas)} válidas (GNU as, .option rvc / norvc) y {len(RESERVADAS)} reservadas")
    for cc_, nn, txt in filas:
        print(f"{{0x{cc_:04x}u, 0x{nn:08x}u}},  // {txt}")
    for cc_, txt in RESERVADAS:
        print(f"{{0x{cc_:04x}u, 0x00000000u}},  // {txt}")


if __name__ == "__main__":
    main()
