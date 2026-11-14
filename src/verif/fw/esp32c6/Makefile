# =============================================================================
# verif/fw/esp32c6/Makefile — El firmware de prueba del ESP32-C6 (E9)
#
# Los ELF se versionan (como los del C3): `make testc6` no necesita la cadena
# de RISC-V. Para regenerarlos: riscv64-unknown-elf o riscv32-esp-elf
# (CROSS=riscv32-esp-elf-), RV32IMAC.
# =============================================================================
CROSS  ?= riscv64-unknown-elf-
CC     := $(CROSS)gcc
ARCH   := -march=rv32imac_zicsr_zifencei -mabi=ilp32
CFLAGS := $(ARCH) -O2 -g -ffreestanding -fno-builtin -nostdlib -nostartfiles -Wall \
          -Wl,--no-warn-rwx-segments -T c6_ram.ld
COMUN  := startup.S buzon.c

PRUEBAS := blinky uart intc
all: $(PRUEBAS:%=%.elf) direct_boot.bin

%.elf: %.c $(COMUN) c6.h c6_ram.ld
	$(CC) $(CFLAGS) -o $@ $(COMUN) $< -lgcc

# Direct Boot, desde la Flash (c6_flash.ld); la imagen es el .bin
direct_boot.elf: direct_boot.c $(COMUN) c6.h c6_flash.ld
	$(CC) $(ARCH) -O2 -g -ffreestanding -fno-builtin -nostdlib -nostartfiles -Wall \
	      -Wl,--no-warn-rwx-segments -T c6_flash.ld -o $@ $(COMUN) $< -lgcc
direct_boot.bin: direct_boot.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -f *.elf direct_boot.bin
.PHONY: all clean
