/* bsl_fr2.ld — el BSL B1 de FR23xx-FR26xx: el BSL1 en 0x1000-0x17FF, el BSL2
 * en 0xFFC00-0xFFFFF, las variables desde 0x2200 (la pila, encima, hasta 0x23FF) */
INCLUDE msp430fr2476_symbols.ld
MEMORY {
  BSL1 (rx) : ORIGIN = 0x1000,  LENGTH = 0x0800
  BSL2X (rx) : ORIGIN = 0xFFC00, LENGTH = 0x0400
  RAM (rw)  : ORIGIN = 0x2200,  LENGTH = 0x0180
}
SECTIONS {
  .zarea  : { KEEP(*(.zarea)) } > BSL1
  .text   : { *(.text .text.*) *(.rodata .rodata.*) *(.lower.text*) *(.lower.rodata*) } > BSL1
  .bsl2   : { *(.bsl2 .bsl2.*) } > BSL2X
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON .data .data.* .lower.bss* .lower.data*) . = ALIGN(2); __bss_end = .; } > RAM
  /DISCARD/ : { *(.MSP430.attributes) *(.comment) *(.note*) *(.upper.*) *(.either.*) }
}
