/* bsl_fr5.ld — el BSL B1 de las FR5xx/FR6xx: código en 0x1000-0x17FF, datos en 0x1C00-0x1FFF */
INCLUDE msp430fr5969_symbols.ld
MEMORY {
  BSL (rx)  : ORIGIN = 0x1000, LENGTH = 0x0800
  RAM (rw)  : ORIGIN = 0x1C00, LENGTH = 0x0300
}
SECTIONS {
  .zarea  : { KEEP(*(.zarea)) } > BSL
  .text   : { *(.text .text.*) *(.rodata .rodata.*) *(.lower.text*) *(.lower.rodata*) } > BSL
  .bss (NOLOAD) : { *(.bss .bss.* COMMON .data .data.* .lower.bss* .lower.data*) } > RAM
  /DISCARD/ : { *(.MSP430.attributes) *(.comment) *(.note*) *(.upper.*) *(.either.*) }
}
