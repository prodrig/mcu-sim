/* lab.ld — Programas para el SoC de laboratorio RISC-V (y para Spike)
 * E1 de doc/analisis_esp32_riscv.md. La RAM empieza en 0x8000_0000 y tiene
 * 4 MB, como la de `SocLabRv` y la de `spike -m0x80000000:0x400000`. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;
  .text.init : { *(.text.init) }
  . = ALIGN(0x100);
  .tohost : { *(.tohost) }
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
  .data : { *(.data .data.* .sdata .sdata.*) }
  .mailbox : { *(.mailbox) }
  .bss : { __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); __bss_end = .; }
  . = ALIGN(16);
  _end = .;
  __stack_top = 0x80000000 + 0x400000 - 16;
}
