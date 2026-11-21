/* bsl_fr4.ld — el BSL B1 de las FR4xx: 1 KB en 0x1000-0x13FF, las variables desde 0x2200 (la pila, encima) */
INCLUDE msp430fr4133_symbols.ld
MEMORY {
  BSL1 (rx) : ORIGIN = 0x1000, LENGTH = 0x0400
  RAM (rw)  : ORIGIN = 0x2200, LENGTH = 0x0180
}
SECTIONS {
  .zarea  : { KEEP(*(.zarea)) } > BSL1
  .text   : { *(.text .text.*) *(.rodata .rodata.*) *(.lower.text*) *(.lower.rodata*) } > BSL1
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON .data .data.* .lower.bss* .lower.data*) . = ALIGN(2); __bss_end = .; } > RAM
  /DISCARD/ : { *(.MSP430.attributes) *(.comment) *(.note*) *(.upper.*) *(.either.*) }
}
