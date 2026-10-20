/* ===========================================================================
 * stm32f303vc.ld — Mapa de los firmwares de verificacion propios del F303VC
 *
 * 256 KB de Flash en 0x0800_0000, 40 KB de SRAM en 0x2000_0000 y 8 KB de CCM
 * en 0x1000_0000 [stm32f303xc.h; SW4STM32: STM32F303VCTx_FLASH.ld]. El primer
 * bloque de la SRAM es el buzon con el banco de pruebas, como en los demas
 * chips: NOLOAD, y por eso sobrevive a los resets. `.ccmtext` es el codigo que
 * se copia a la CCM y se ejecuta desde alli (la CCM del F303 esta en el bus
 * de instrucciones).
 * ===========================================================================*/
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 40K
    CCM   (rwx) : ORIGIN = 0x10000000, LENGTH = 8K
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

    _siccm = LOADADDR(.ccmtext);
    .ccmtext :
    {
        . = ALIGN(4);
        _sccm = .;
        *(.ccmtext*)
        . = ALIGN(4);
        _eccm = .;
    } > CCM AT> FLASH

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
