/* bsl_f5529.ld — el BSL B1 del F5529 (el USB de TI, BSD-3, portado a gcc):
   0x1000-0x17FF; las versiones en 0x1008-0x100B, el descriptor de dispositivo
   en 0x107C y las firmas en 0x17F0 [su .map]; la RAM del BSL, 0x2420-0x33FF */
INCLUDE msp430f5529_symbols.ld
INCLUDE ubicaciones.ld
MEMORY {
  ZAREA (rx) : ORIGIN = 0x1000, LENGTH = 0x0008
  VERS  (r)  : ORIGIN = 0x1008, LENGTH = 0x0004
  BSL   (rx) : ORIGIN = 0x100C, LENGTH = 0x0070
  DESC  (r)  : ORIGIN = 0x107C, LENGTH = 0x0012
  BSL2  (rx) : ORIGIN = 0x108E, LENGTH = 0x0762
  SIG   (r)  : ORIGIN = 0x17F0, LENGTH = 0x0010
  RAM   (rw) : ORIGIN = 0x2420, LENGTH = 0x0FE0
}
SECTIONS {
  .zarea : { KEEP(*(.zarea)) } > ZAREA
  .vers  : { KEEP(*(.BSL430_VERSION_VENDOR)) KEEP(*(.BSL430_VERSION_CI)) KEEP(*(.BSL430_VERSION_API)) KEEP(*(.BSL430_VERSION_PI)) } > VERS
  .text0 : { *crt_f5529.o(.text) *crt_f5529.o(.text.arranque) } > BSL
  .desc  : { KEEP(*(.ubica_107C)) } > DESC
  .text  : { *(.text .text.* .lower.text*) *(.rodata .rodata.* .lower.rodata*) *(.data .data.*) } > BSL2
  .bslsig : { KEEP(*(.bslsig)) } > SIG
  .bss (NOLOAD) : { __bss_ini = .; *(.bss .bss.* COMMON .lower.bss*) __bss_fin = .; *(.noinit .noinit.*) } > RAM
  /DISCARD/ : { *(.MSP430.attributes) *(.comment) *(.note*) }
}
