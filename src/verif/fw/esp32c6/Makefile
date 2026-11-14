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
all: $(PRUEBAS:%=%.elf)

%.elf: %.c $(COMUN) c6.h c6_ram.ld
	$(CC) $(CFLAGS) -o $@ $(COMUN) $< -lgcc

clean:
	rm -f *.elf
.PHONY: all clean
