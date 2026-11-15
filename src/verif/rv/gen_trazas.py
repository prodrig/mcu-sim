#!/usr/bin/env python3
# =============================================================================
# gen_trazas.py — Las huellas de las trazas de Spike, para el lockstep de E1
#
# E1 de doc/analisis_esp32_riscv.md. Ejecuta cada ELF en Spike, el simulador
# de referencia de RISC-V International, con el mismo mapa que el SoC de
# laboratorio, y resume su traza de instrucciones retiradas en una huella:
#
#   spike --isa=rv32imac_zicsr_zifencei_zicntr --priv=MU -m0x80000000:0x400000 \
#         -l --log-commits <elf>
#
# Por cada instrucción retirada con PC >= 0x8000_0000 (lo de antes es la ROM
# de arranque de Spike) entran en un FNV-1a de 64 bits, como cuatro palabras
# de 32 bits little-endian: el PC, la instrucción tal como está en memoria
# (16 o 32 bits), el registro entero escrito (0xFFFFFFFF si ninguno; x0 no
# cuenta) y su valor (0 si ninguno). Las instrucciones que provocan una
# excepción no se retiran y no entran (ni en Spike ni en `RvHart::traza`).
# La traza acaba en el almacenamiento a `tohost`, incluido: Spike mira tohost
# cada cierto número de instrucciones y sigue un rato en el bucle final, y el
# laboratorio para en el acto; lo que viene detrás no es del programa.
#
# El banco `testrv` calcula lo mismo con el gancho `traza` del hart y compara
# la cuenta y la huella. Si no cuadran, `--volcado DIR` deja aquí la traza
# entera de Spike en texto (pc insn rd valor por línea) y el banco, con
# RV_TRAZA=DIR, la suya en el mismo formato: un diff dice dónde se separan.
#
#   python3 gen_trazas.py --spike ~/spike/bin/spike elf/*.elf > trazas_spike.inc
# =============================================================================
import argparse
import os
import re
import subprocess
import sys

FNV0 = 0xcbf29ce484222325
FNVP = 0x100000001b3
M64 = (1 << 64) - 1
LINEA = re.compile(r"^core\s+\d+:\s+(\d)\s+0x([0-9a-f]+)\s+\(0x([0-9a-f]+)\)(.*)$")
XREG = re.compile(r"\bx(\d+)\s+0x([0-9a-f]+)")
MEMW = re.compile(r"\bmem\s+0x([0-9a-f]+)\s+0x[0-9a-f]+")


def fnv(h, w):
    for k in range(4):
        h ^= (w >> (8 * k)) & 0xFF
        h = (h * FNVP) & M64
    return h


def tohost(nm, elf):
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    m = re.search(r"^([0-9a-f]+)\s+\w\s+tohost$", out, re.M)
    if not m:
        sys.exit(f"gen_trazas: {elf} no tiene el símbolo tohost")
    return int(m.group(1), 16)


def traza(spike, nm, elf):
    th = tohost(nm, elf)
    p = subprocess.run([spike, "--isa=rv32imac_zicsr_zifencei_zicntr", "--priv=MU",
                        "-m0x80000000:0x400000", "-l", "--log-commits", elf],
                       capture_output=True, text=True, timeout=600)
    filas = []
    for ln in p.stderr.splitlines():
        m = LINEA.match(ln)
        if not m:
            continue
        pc = int(m.group(2), 16) & 0xFFFFFFFF
        if pc < 0x80000000:
            continue
        insn = int(m.group(3), 16)
        rd, val = 0xFFFFFFFF, 0
        x = XREG.search(m.group(4))
        if x and int(x.group(1)) != 0:
            rd, val = int(x.group(1)), int(x.group(2), 16) & 0xFFFFFFFF
        filas.append((pc, insn, rd, val))
        w = MEMW.search(m.group(4))
        if w and int(w.group(1), 16) & 0xFFFFFFFF == th:
            break
    return p.returncode, filas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spike", default="spike")
    ap.add_argument("--nm", default="riscv64-unknown-elf-nm")
    ap.add_argument("--volcado", default=None)
    ap.add_argument("elfs", nargs="+")
    a = ap.parse_args()
    print("// trazas_spike.inc — GENERADO por verif/rv/gen_trazas.py; no editar.")
    print("// {ELF, instrucciones retiradas, huella FNV-1a de (pc, insn, rd, valor)}")
    ver = subprocess.run([a.spike, "--help"], capture_output=True, text=True)
    m = re.search(r"Spike RISC-V ISA Simulator ([^\s]+)", ver.stdout + ver.stderr)
    print(f"// Spike {m.group(1) if m else '(versión desconocida)'}")
    for elf in a.elfs:
        rc, filas = traza(a.spike, a.nm, elf)
        if rc != 0:
            sys.exit(f"gen_trazas: {elf} acaba con el código {rc} en Spike")
        h = FNV0
        for f in filas:
            for w in f:
                h = fnv(h, w)
        nombre = os.path.basename(elf)
        print(f'{{"{nombre}", {len(filas)}u, 0x{h:016x}ull}},')
        if a.volcado:
            os.makedirs(a.volcado, exist_ok=True)
            with open(os.path.join(a.volcado, nombre + ".spike.txt"), "w") as f:
                for pc, insn, rd, val in filas:
                    f.write(f"{pc:08x} {insn:08x} {rd:08x} {val:08x}\n")


if __name__ == "__main__":
    main()
