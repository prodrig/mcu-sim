/* ===========================================================================
 * stm32f446.ld — Script de enlazado del firmware del STM32F446RE
 *
 * Mapa real del STM32F446RE [RM0390, §2.2; DS10693]:
 *   FLASH    512 KB en 0x0800 0000   (la mitad que el F407VG)
 *   SRAM1+2  128 KB contiguos en 0x2000 0000 (112 KB + 16 KB)
 *   CCM      NO EXISTE
 *
 * Las dos diferencias con `stm32f407.ld` son exactamente esas, y las dos son
 * comprobables desde el modelo: un firmware enlazado para 1 MB no cabe aquí, y
 * una sección `.ccmram` no tiene dónde ir. Que este fichero no declare la CCM
 * no es una omisión: es lo que dice el silicio.
 * ===========================================================================*/
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* La cima de pila queda al final de la SRAM */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text) *(.text*)
        *(.rodata) *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Buzón de resultados en una dirección fija conocida por el banco de
       pruebas: primer bloque de la SRAM. */
    .mailbox (NOLOAD) :
    {
        . = ALIGN(4);
        KEEP(*(.mailbox))
        . = ALIGN(4);
    } > SRAM

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data) *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss) *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }
}
