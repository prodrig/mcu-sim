#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# gen_regs_esp.py — Los registros de un bloque de Espressif y su valor de reset,
#                   sacados de las cabeceras de ESP-IDF (soc/<chip>/register)
#
# E4 de doc/analisis_esp32_riscv.md, §8.2: la segunda fuente de «mapa,
# registros y valores de reset» son las cabeceras `soc/*_reg.h` de ESP-IDF
# (Apache-2.0). Cada campo trae su valor de reset en un comentario:
#
#   #define EXTMEM_ICACHE_CTRL1_REG   (DR_REG_EXTMEM_BASE + 0x004)
#   /* EXTMEM_ICACHE_SHUT_DBUS : R/W ;bitpos:[1] ;default: 1'b1 ; */
#
# y aquí se suman en el valor de reset del registro. Sale un .inc con una
# tabla por bloque: {desplazamiento, reset, máscara de lo escribible}. La usan
# los bloques que guardan (BloqueGuardado) y los modelos que parten de ellos.
#
#   python3 gen_regs_esp.py <esp-idf>/components/soc/esp32c3/register/soc \
#           extmem:EXTMEM spi_mem:SPI_MEM ... > regs_c3.inc
#
# Cada argumento es <cabecera sin _reg.h>:<prefijo C++>. Los campos RO, SC
# (autolimpiables), WO y WT no cuentan como escribibles. Las cabeceras usan
# dos estilos de comentario; se leen los dos.
# =============================================================================
import re
import sys
from pathlib import Path

RE_REG = re.compile(r"^#define\s+([A-Z0-9_]+_REG)(\(i\))?\s+\(\s*[A-Za-z0-9_]+(\(i\))?\s*\+\s*(0x[0-9A-Fa-f]+)\s*\)")
# Dos estilos: «/* X : R/W ;bitpos:[3:0] ;default: 4'b0 ; */» (y «default: 0 ;»)
# y «/** X : R/W; bitpos: [15:0]; default: 0;»
RE_CAMPO = re.compile(r"/\*\*?\s*([A-Z0-9_]+)\s*:\s*([A-Z/]+)\s*;\s*bitpos:\s*\[(\d+)(?::(\d+))?\]\s*;"
                      r"\s*default:\s*(?:(\d+)'([bhdBHD]?))?([0-9a-fA-FxX_]+)")


def valor(base, digitos):
    d = digitos.replace("_", "")
    if base is None:
        return int(d, 0)
    b = base.lower()
    if b == "b":
        return int(d, 2)
    if b == "h":
        return int(d, 16)
    return int(d, 10)


def bloque(ruta):
    regs = []
    actual = None
    for linea in Path(ruta).read_text(errors="replace").splitlines():
        m = RE_REG.match(linea.strip())
        if m:
            actual = [m.group(1), int(m.group(4), 16), 0, 0]
            regs.append(actual)
            continue
        m = RE_CAMPO.search(linea)
        if m and actual is not None:
            _, acc, hi, lo, ancho_txt, base, dig = m.groups()
            if ancho_txt is None:
                base = None
            hi = int(hi)
            lo = int(lo) if lo is not None else hi
            ancho = hi - lo + 1
            v = valor(base, dig) & ((1 << ancho) - 1)
            actual[2] |= v << lo
            if acc.upper() in ("R/W", "RW", "R/W/SS", "R/W/SC", "R/W/SS/SC", "R/W/WTC", "R/W/WTC/SS"):
                actual[3] |= ((1 << ancho) - 1) << lo
    # Un registro puede salir dos veces si la cabecera repite el bloque: el primero
    vistos = {}
    for r in regs:
        vistos.setdefault(r[1], r)
    return sorted(vistos.values(), key=lambda r: r[1])


def main():
    d = Path(sys.argv[1])
    print("// Generado por soc/esp/gen_regs_esp.py desde las cabeceras de ESP-IDF")
    print("// (components/soc/<chip>/register/soc, Apache-2.0). No editar.")
    print("// {desplazamiento, valor de reset, bits escribibles}")
    for arg in sys.argv[2:]:
        cab, pre = arg.split(":")
        rs = bloque(d / f"{cab}_reg.h")
        print(f"inline const RegEsp REGS_{pre}[] = {{")
        for nombre, off, rst, w in rs:
            print(f"    {{0x{off:03X}, 0x{rst & 0xFFFFFFFF:08X}u, 0x{w & 0xFFFFFFFF:08X}u}},  // {nombre}")
        print("};")


if __name__ == "__main__":
    main()
