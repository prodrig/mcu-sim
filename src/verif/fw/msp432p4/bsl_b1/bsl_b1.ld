/* ===========================================================================
 * bsl_b1.ld — El BSL B1 en su sector: 0x0020_2000, 8 KB
 *
 *   0x0020_2000  .entrada   la direccion de bsl_entrada
 *   0x0020_2010  .version   diez bytes
 *   0x0020_2020  .text      el codigo y sus constantes
 *
 * Sin RAM propia: nada puede ir a .data ni a .bss (el BSL no tiene arranque
 * de C; vive en la pila).
 * ===========================================================================*/
MEMORY
{
    BSL (rx) : ORIGIN = 0x00202000, LENGTH = 8K
}
SECTIONS
{
    .entrada 0x00202000 : { KEEP(*(.entrada)) } > BSL
    .version 0x00202010 : { KEEP(*(.version)) } > BSL
    .text 0x00202020 : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > BSL
    .data : { *(.data .data.*) } > BSL
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
ASSERT(SIZEOF(.bss) == 0 && SIZEOF(.data) == 0, "el BSL B1 no puede tener variables globales")
