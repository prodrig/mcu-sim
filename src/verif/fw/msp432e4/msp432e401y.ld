/* ===========================================================================
 * msp432e401y.ld — Mapa de los firmwares de verificacion del MSP432E401Y
 *
 * 1 MB de Flash en 0x0 y 256 KB de SRAM en 0x2000_0000 [msp432e401y.h:
 * FLASH_BASE, SRAM_BASE]. El primer bloque de la SRAM es el buzon con el
 * banco de pruebas, como en el P4 y en los STM32: NOLOAD, y por eso
 * sobrevive a los resets.
 * ===========================================================================*/
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 1024K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

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
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }
}
