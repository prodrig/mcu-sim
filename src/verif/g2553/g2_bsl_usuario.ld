/* g2_bsl_usuario.ld — el BSL de usuario de la G2xx (M9): el BSL en la
 * información desde 0x1000 y la aplicación en 0xF800-0xFFDF, como el
 * lnk_msp430g2231.cmd de MSPBSL; el vector de reset, al BSL. Son 242 bytes:
 * pisan la información A hasta 0x10F1 (la TLV del ADC10 del G2553), como en
 * el G2231 del original, y dejan las calibraciones del DCO (0x10F6-0x10FF). */
INCLUDE msp430g2553_symbols.ld
MEMORY {
  BSLINFO (rx)  : ORIGIN = 0x1000, LENGTH = 0x00F6
  FLASH (rx) : ORIGIN = 0xF800, LENGTH = 0x07E0
  RESETVEC (r) : ORIGIN = 0xFFFE, LENGTH = 0x0002
}
SECTIONS {
  .bsl  : { KEEP(*(.bsl)) } > BSLINFO
  .text : { *(.text .text.*) } > FLASH
  __reset_vector : { KEEP(*(.vector_reset)) } > RESETVEC
  /DISCARD/ : { *(.MSP430.attributes) *(.comment) *(.note*) }
}
