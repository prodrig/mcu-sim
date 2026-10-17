/* ===========================================================================
 * rom_r1.ld — La ROM sustituta del MSP432E4 en su ventana: 0x0100_0000, 256 KB
 *
 *   0x0100_0000  .vectores   pila, reset (el cargador), NMI y HardFault
 *   0x0100_0010  .apitable   ROM_APITABLE: version y punteros a tabla
 *   ...          .text       las tablas de modulo, DriverLib y el cargador
 *
 * Una ROM no tiene RAM propia: lo que DriverLib deja en .data es de solo
 * lectura en las funciones que llegan aqui, y se queda en la ROM con su valor
 * inicial. Nada puede ir a .bss: si una funcion de las tablas -o el cargador-
 * pidiera una variable, el enlace falla. El cargador vive en la pila.
 * ===========================================================================*/
MEMORY
{
    ROM (rx) : ORIGIN = 0x01000000, LENGTH = 256K
}

SECTIONS
{
    .vectores 0x01000000 : { KEEP(*(.vectores)) } > ROM
    .apitable 0x01000010 : { KEEP(*(.apitable)) } > ROM
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

ASSERT(SIZEOF(.bss) == 0, "la ROM R1 no puede tener variables: una funcion de las tablas o el cargador pide RAM")
ASSERT(ADDR(.apitable) == 0x01000010, "ROM_APITABLE tiene que estar en 0x0100_0010")
ASSERT(SIZEOF(.vectores) == 16, "delante de ROM_APITABLE caben justo cuatro vectores")
