/* c3_iram.ld — El firmware de prueba del ESP32-C3 SIN ROM (E3)
 *
 * Todo en la SRAM1, que el bus I ve en 0x4038_0000 y el D en 0x3FC8_0000:
 * el código por la vista I y los datos por la D, a continuación, en la MISMA
 * memoria física (los datos empiezan en la D donde acaba el código en la I).
 * La pila, al final de la SRAM1. El simulador carga el ELF tal cual y
 * arranca en _start (E3 no tiene ROM ni Flash). */
ENTRY(_start)
MEMORY {
    IRAM (rx)  : ORIGIN = 0x40380000, LENGTH = 0x60000
    DRAM (rw)  : ORIGIN = 0x3FC80000, LENGTH = 0x60000
}
SECTIONS {
    .text : {
        KEEP(*(.text.init))
        . = ALIGN(256);
        KEEP(*(.vectores))
        *(.text .text.*)
        . = ALIGN(16);
        _etext = .;
    } > IRAM
    .data ORIGIN(DRAM) + (_etext - ORIGIN(IRAM)) : {
        _sdata = .;
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > DRAM
    .bss (NOLOAD) : {
        _sbss = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DRAM
    __global_pointer$ = _sdata + 0x800;
    _stack_top = ORIGIN(DRAM) + LENGTH(DRAM) - 16;
}
