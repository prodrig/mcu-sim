/* c6_ram.ld — El firmware de prueba del ESP32-C6 en arranque directo (E9)
 *
 * Todo en la HP SRAM (0x4080_0000, 512 KB), que es la misma para los buses I
 * y D: código, datos y pila seguidos. El simulador carga el ELF tal cual y
 * arranca en _start, sin la ROM. Los últimos 64 KB los usa la ROM del chip
 * (su .bss y su pila): aquí no se tocan, por si se arranca luego por ella. */
ENTRY(_start)
MEMORY {
    RAM (rwx) : ORIGIN = 0x40800000, LENGTH = 0x70000
}
SECTIONS {
    .text : {
        KEEP(*(.text.init))
        . = ALIGN(256);
        KEEP(*(.vectores))
        . = ALIGN(256);
        KEEP(*(.vectores_u))
        *(.text .text.*)
        . = ALIGN(16);
    } > RAM
    .data : {
        _sdata = .;
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM
    .bss (NOLOAD) : {
        _sbss = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
    __global_pointer$ = _sdata + 0x800;
    _stack_top = ORIGIN(RAM) + LENGTH(RAM) - 16;
}
