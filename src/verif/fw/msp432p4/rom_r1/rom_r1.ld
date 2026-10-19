/* ===========================================================================
 * rom_r1.ld — La ROM sustituta en su ventana: 0x0200_0000, 32 KB
 *
 *   0x0200_0000  .cabecera   la firma y rom_hueco
 *   0x0200_0800  .apitable   ROM_APITABLE: version y 28 punteros a tabla
 *   ...          .text       las tablas de modulo y DriverLib
 *
 * Una ROM no tiene RAM propia: lo que DriverLib deja en .data es de solo
 * lectura en las funciones que llegan aqui (los arrays de punteros a
 * registros de flash.c), y se queda en la ROM con su valor inicial. Nada puede
 * ir a .bss: si una funcion de las tablas pidiera una variable, el enlace
 * falla, porque en el silicio no la tendria.
 * ===========================================================================*/
MEMORY
{
    ROM (rx) : ORIGIN = 0x02000000, LENGTH = 32K
}

SECTIONS
{
    .cabecera 0x02000000 : { KEEP(*(.cabecera.firma)) . = ALIGN(4); KEEP(*(.cabecera.hueco)) } > ROM
    .apitable 0x02000800 : { KEEP(*(.apitable)) } > ROM
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(4);
    } > ROM
    .relleno : {
        FILL(0xFF);
        . = ORIGIN(ROM) + LENGTH(ROM) - 1;
        BYTE(0xFF)
    } > ROM
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(vtable) *(.ARM.exidx*) *(.ARM.extab*) }
}

ASSERT(SIZEOF(.bss) == 0, "la ROM R1 no puede tener variables: una funcion de las tablas pide RAM")
ASSERT(ADDR(.apitable) == 0x02000800, "ROM_APITABLE tiene que estar en 0x0200_0800")
