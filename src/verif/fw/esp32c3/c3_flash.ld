/* c3_flash.ld — El firmware de prueba del ESP32-C3 en DIRECT BOOT (E4)
 *
 * La ROM, al ver 0xAEDB041D dos veces al principio de la Flash, proyecta los
 * primeros 4 MB de la Flash en las dos ventanas de la caché, 0x4200_0000 (bus
 * I) y 0x3C00_0000 (bus D), y salta a 0x4200_0008 (ets_run_direct_boot). Sin
 * bootloader ni cabecera de imagen. Aquí:
 *   * el código, en la ventana I, tras las dos palabras mágicas;
 *   * las constantes, en la ventana D, en el MISMO desplazamiento de la Flash
 *     al que van a parar en la imagen (VMA en 0x3C.., LMA tras el código);
 *   * .bss y la pila, en la SRAM1 (sin .data: nada que copiar).
 * `objcopy -O binary` da la imagen de la Flash desde el desplazamiento 0. */
ENTRY(_start)
MEMORY {
    IROM (rx) : ORIGIN = 0x42000000, LENGTH = 4M
    DROM (r)  : ORIGIN = 0x3C000000, LENGTH = 4M
    DRAM (rw) : ORIGIN = 0x3FC80000, LENGTH = 0x60000
}
SECTIONS {
    .text : {
        LONG(0xAEDB041D)
        LONG(0xAEDB041D)
        KEEP(*(.text.init))
        . = ALIGN(256);
        KEEP(*(.vectores))
        *(.text .text.*)
        . = ALIGN(16);
        _etext = .;
    } > IROM
    .rodata ORIGIN(DROM) + (_etext - ORIGIN(IROM)) : AT(_etext) {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(16);
    } > DROM
    .data (NOLOAD) : { _sdata = .; *(.data .data.* .sdata .sdata.*) _edata = .; } > DRAM
    ASSERT(_edata == _sdata, "direct boot: sin .data con valor inicial")
    .bss (NOLOAD) : {
        _sbss = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DRAM
    __global_pointer$ = _sbss + 0x800;
    _stack_top = ORIGIN(DRAM) + LENGTH(DRAM) - 16;
}
