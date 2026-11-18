/* rom.ld — la ROM de bibliotecas del FR2355 (genera_rom.py, Makefile.rom):
 * 0xFAC00-0xFFBFF, 20 KB [DS SLASEC4D tabla 6-4]; sus variables, en la RAM
 * diminuta 0x0006-0x001F, la que TI reserva para la ROM [el descriptor] */
OUTPUT_ARCH(msp430)
INCLUDE msp430fr2355_symbols.ld
ENTRY(rom_raiz)
MEMORY {
  ROM (rx)     : ORIGIN = 0xFAC00, LENGTH = 0x5000
  RAMROM (rw)  : ORIGIN = 0x0006,  LENGTH = 0x001A
}
SECTIONS {
  .rom.raiz   : { KEEP(*(.rom.raiz)) } > ROM
  .rom.tablas : { KEEP(*(.rom.tablas)) } > ROM
  .text       : { *(.text .text.* .lower.text .lower.text.* .upper.text .upper.text.* .either.text .either.text.*) } > ROM
  .rodata     : { *(.rodata .rodata.* .lower.rodata* .upper.rodata* .either.rodata*) } > ROM
  .bss (NOLOAD) : { *(.bss .bss.* .lower.bss* .upper.bss* .either.bss* COMMON) } > RAMROM
  .data       : { *(.data .data.* .lower.data* .upper.data* .either.data*) } > RAMROM
  /DISCARD/   : { *(.comment) *(.MSP430.attributes) }
}
