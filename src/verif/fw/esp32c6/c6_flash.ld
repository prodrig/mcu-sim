/* c6_flash.ld — El firmware de prueba del ESP32-C6 en DIRECT BOOT (E9)
 *
 * La ROM, al ver 0xAEDB041D dos veces al principio de la Flash
 * (ets_loader_is_direct_boot_jump), proyecta la Flash en la ventana de la
 * caché (0x4200_0000, la misma para los buses I y D: ROM_Direct_Boot_MMU_Init)
 * y salta a 0x4200_0008 (ets_run_direct_boot). Sin bootloader ni cabecera de
 * imagen. Aquí:
 *   * el código y las constantes, en la ventana, tras las dos palabras;
 *   * .bss y la pila, en la HP SRAM (sin .data: nada que copiar).
 * `objcopy -O binary` da la imagen de la Flash desde el desplazamiento 0. */
ENTRY(_start)
MEMORY {
    FLASH (rx) : ORIGIN = 0x42000000, LENGTH = 4M
    RAM (rw)   : ORIGIN = 0x40800000, LENGTH = 0x70000
}
SECTIONS {
    .text : {
        LONG(0xAEDB041D)
        LONG(0xAEDB041D)
        KEEP(*(.text.init))
        . = ALIGN(256);
        KEEP(*(.vectores))
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(16);
    } > FLASH
    .data (NOLOAD) : { _sdata = .; *(.data .data.* .sdata .sdata.*) _edata = .; } > RAM
    ASSERT(_edata == _sdata, "direct boot: sin .data con valor inicial")
    .bss (NOLOAD) : {
        _sbss = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
    __global_pointer$ = _sbss + 0x800;
    _stack_top = ORIGIN(RAM) + LENGTH(RAM) - 16;
}
