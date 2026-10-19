/* ===========================================================================
 * msp432p401r.ld — Mapa de los firmwares de verificacion del MSP432P401R
 *
 * 256 KB de Flash EN 0x0 (no en 0x0800_0000: no hay alias de arranque) y
 * 64 KB de SRAM en 0x2000_0000 [msp432p401r.h: FLASH_BASE, SRAM_BASE]. El
 * primer bloque de la SRAM es el buzon con el banco de pruebas, igual que en
 * los STM32: NOLOAD, y por eso sobrevive a los resets.
 * ===========================================================================*/
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
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
