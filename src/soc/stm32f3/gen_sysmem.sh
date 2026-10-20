#!/bin/sh
# gen_sysmem.sh — ensambla sysmem_s1.s y escribe sysmem_s1.inc (se versiona)
set -e
T=$(mktemp -d)
arm-none-eabi-as -mcpu=cortex-m4 -mthumb -o "$T/s.o" sysmem_s1.s
arm-none-eabi-ld -e 0x1FFFD800 -Ttext=0x1FFFD800 -o "$T/s.elf" "$T/s.o"
arm-none-eabi-objcopy -O binary "$T/s.elf" "$T/s.bin"
{
  echo "// GENERADO por gen_sysmem.sh a partir de sysmem_s1.s: NO SE EDITA A MANO."
  od -An -v -tx1 "$T/s.bin" | sed -E 's/ ([0-9a-f]{2})/0x\1, /g'
} > sysmem_s1.inc
rm -rf "$T"
