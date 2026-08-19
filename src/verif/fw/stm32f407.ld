/* ===========================================================================
 * stm32f407.ld — Script de enlazado del firmware de verificación (fase F2)
 *
 * Mapa real del STM32F407VG [IR, §5.2, §5.3, §15.1]:
 *   FLASH   1024 KB en 0x0800 0000
 *   SRAM1+2  128 KB contiguos en 0x2000 0000 (112 KB + 16 KB)
 *   CCM       64 KB en 0x1000 0000 (solo accesible por el bus D del núcleo)
 * ===========================================================================*/
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
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

    /* Sección de prueba en la CCM (verifica que solo la alcanza el bus D) */
    .ccm (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccmram)
        . = ALIGN(4);
    } > CCM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) }
}
