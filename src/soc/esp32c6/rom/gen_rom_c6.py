#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# gen_rom_c6.py — La ROM del ESP32-C6 como .inc, desde el ELF de esp-rom-elfs
#
# E9 de doc/analisis_esp32_riscv.md, como gen_rom_c3.py (R1, §8.1). La ROM
# de verdad: los segmentos cargables del ELF de espressif/esp-rom-elfs
# (Apache-2.0) cuya dirección física cae en la ventana de la ROM
# (0x4000_0000-0x4004_FFFF, 320 KB [TRM-C6 tabla 5.3-1]; el C6 no tiene bus
# D aparte: la misma dirección por los dos).
#
# LOS DATOS INICIALES. Como en el C3, la ROM copia al arrancar sus variables
# con valor inicial desde una imagen que lleva dentro, y el ELF no la trae en
# sus segmentos cargables. En el C6 la tabla está entre `_data_table_start`
# y `_data_table_end` y sus entradas son de 12 bytes, {destino, fin,
# origen}; los destinos caen en la HP SRAM (0x4080_0000-0x4087_FFFF). Las
# secciones .data_* y .data.interface* se copian en origen + (dirección -
# destino). Sale idéntica, byte a byte, a la esp32c6-rom.bin del QEMU de
# Espressif (los 320 KB).
#
#   python3 gen_rom_c6.py esp32c6_rev0_rom.elf > rom_c6_rev0.inc
# =============================================================================
import hashlib
import struct
import sys

BASE, TAM = 0x40000000, 0x50000


def imagen(ruta):
    d = open(ruta, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        sys.exit(f"{ruta}: no es un ELF de 32 bits little-endian")
    phoff, = struct.unpack_from("<I", d, 28)
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    rom = bytearray(TAM)
    for i in range(phnum):
        tipo, off, vaddr, paddr, filesz, memsz, flags, al = struct.unpack_from("<8I", d, phoff + i * phentsize)
        if tipo != 1 or filesz == 0 or not (BASE <= paddr < BASE + TAM):
            continue
        if paddr + filesz > BASE + TAM:
            sys.exit(f"{ruta}: un segmento se sale de la ROM")
        rom[paddr - BASE:paddr - BASE + filesz] = d[off:off + filesz]
    interfaz(d, rom)
    datos_iniciales(d, rom)
    return bytes(rom), hashlib.sha256(d).hexdigest()


# Las secciones .rodata.interface* (las tablas que ESP-IDF lee de la ROM en
# direcciones fijas: ets_rom_layout_p en 0x3FF1_FFFC, las de la radio...)
# tampoco van en un segmento cargable: se copian por su dirección, la del bus D
# (0x3FF0_0000 es 0x4004_0000).
def interfaz(d, rom):
    for nom, tipo, dirs, off, tam in secciones(d):
        if tipo != 1 or not tam:
            continue
        if BASE <= dirs < BASE + TAM:
            o = dirs - BASE
        else:
            continue
        if o + tam <= TAM:
            rom[o:o + tam] = d[off:off + tam]


def secciones(d):
    shoff, = struct.unpack_from("<I", d, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)
    sh = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
    strtab = sh[shstrndx]
    nombre = lambda o: d[strtab[4] + o:d.index(b"\0", strtab[4] + o)].decode()
    return [(nombre(x[0]), x[1], x[3], x[4], x[5]) for x in sh]   # nombre, tipo, dir, off, tam


def simbolo(d, quien):
    for nom, tipo, dirs, off, tam in secciones(d):
        if tipo != 2:                                             # SHT_SYMTAB
            continue
        sec = secciones(d)
        link = None
        shoff, = struct.unpack_from("<I", d, 32)
        shentsize, = struct.unpack_from("<H", d, 46)
        for i, x in enumerate(sec):
            if x[0] == nom and x[3] == off:
                link = struct.unpack_from("<10I", d, shoff + i * shentsize)[6]
        stroff = struct.unpack_from("<10I", d, shoff + link * shentsize)[4]
        for k in range(tam // 16):
            n, v, _, _, _, _ = struct.unpack_from("<IIIBBH", d, off + 16 * k)
            if d[stroff + n:d.index(b"\0", stroff + n)].decode() == quien:
                return v
    sys.exit(f"sin el símbolo {quien}")


def datos_iniciales(d, rom):
    ini, fin = simbolo(d, "_data_table_start"), simbolo(d, "_data_table_end")
    secs = [s for s in secciones(d) if s[1] == 1 and s[4] and 0x40800000 <= s[2] < 0x40880000]
    for e in range(ini, fin, 12):
        dst, dst_fin, org = struct.unpack_from("<3I", rom, e - BASE)
        if not (BASE <= org < BASE + TAM) or dst_fin <= dst:
            continue
        for nom, _, dirs, off, tam in secs:
            if dst <= dirs and dirs + tam <= dst_fin:
                o = org + dirs - dst - BASE
                rom[o:o + tam] = d[off:off + tam]


def main():
    rom, h = imagen(sys.argv[1])
    nombre = sys.argv[1].rsplit("/", 1)[-1]
    print(f"// Generado por gen_rom_c6.py desde {nombre} (esp-rom-elfs, Apache-2.0)")
    print(f"// SHA-256 del ELF: {h}")
    print(f"// {TAM // 4} palabras desde 0x{BASE:08X}. No editar.")
    pal = struct.unpack(f"<{TAM // 4}I", rom)
    # Se recortan los ceros del final
    n = len(pal)
    while n and pal[n - 1] == 0:
        n -= 1
    for i in range(0, n, 8):
        print(",".join(f"0x{w:08X}" for w in pal[i:i + 8]) + ",")


if __name__ == "__main__":
    main()
