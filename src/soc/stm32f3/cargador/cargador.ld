/* cargador.ld — el cargador de sistema del F303 del modelo (S7), en la memoria
 * de sistema: 8 KB en 0x1FFF_D800, menos lo de fabrica del final (el UID en
 * 0x1FFF_F7AC, la calibracion y F_SIZE). Usa la SRAM de 0x2000_0004 a
 * 0x2000_13FF, los 5 KB que se reserva el de ST [AN2606; stm32flash,
 * dev_table: el 0x422 deja libre desde 0x2000_1400]; en 0x2000_0000 deja la
 * marca 'MSYS' del modelo. */
MEMORY
{
    SYS (rx)  : ORIGIN = 0x1FFFD800, LENGTH = 0x1F80
    RAM (rwx) : ORIGIN = 0x20000004, LENGTH = 0x13FC
}
ENTRY(arranque)
SECTIONS
{
    .text : {
        KEEP(*(.vectores))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _fin_texto = .;
    } > SYS
    .data : AT(_fin_texto) {
        _data = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM
    .bss (NOLOAD) : {
        _bss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
    _pila = ORIGIN(RAM) + LENGTH(RAM);
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
