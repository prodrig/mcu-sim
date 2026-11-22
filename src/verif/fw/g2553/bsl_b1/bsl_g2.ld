/* bsl_g2.ld — el BSL B1 del G2553: código en 0x0C00-0x0FEF, la información en 0x0FF0 */
INCLUDE msp430g2553_symbols.ld
MEMORY {
  ROM (rx)  : ORIGIN = 0x0C00, LENGTH = 0x03F0
  ROMINFO (r) : ORIGIN = 0x0FF0, LENGTH = 0x0010
  RAM (rw)  : ORIGIN = 0x0200, LENGTH = 0x0180
}
SECTIONS {
  .entrada : { KEEP(*(.entrada)) } > ROM
  .text    : { *(.text .text.*) *(.rodata .rodata.*) } > ROM
  .info_rom : { KEEP(*(.info_rom)) } > ROMINFO
  .bss (NOLOAD) : { *(.bss .bss.* COMMON .data .data.*) } > RAM
  /DISCARD/ : { *(.MSP430.attributes) *(.comment) *(.note*) }
}
