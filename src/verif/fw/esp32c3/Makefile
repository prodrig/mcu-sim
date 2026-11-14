# =============================================================================
# verif/fw/esp32c3/Makefile — El firmware de prueba del ESP32-C3 sin ROM (E3)
#
# Los ELF se versionan (como los .bin de verif/fw): `make testc3` no necesita
# la cadena de RISC-V. Para regenerarlos hace falta riscv64-unknown-elf-gcc
# (el de las distribuciones vale: RV32IMC, ilp32) y picolibc para CoreMark.
# =============================================================================
CROSS  ?= riscv64-unknown-elf-
CC     := $(CROSS)gcc
ARCH   := -march=rv32imc_zicsr_zifencei -mabi=ilp32
CFLAGS := $(ARCH) -O2 -g -ffreestanding -fno-builtin -nostdlib -nostartfiles -Wall \
          -Wl,--no-warn-rwx-segments -T c3_iram.ld
COMUN  := startup.S buzon.c

PRUEBAS := blinky uart intc gpio_matriz ledc e8 i2c spi_tft twai rsa hmac ds xts
all: $(PRUEBAS:%=%.elf) coremark_c3.elf direct_boot.bin

%.elf: %.c $(COMUN) c3.h c3_iram.ld
	$(CC) $(CFLAGS) -o $@ $(COMUN) $< -lgcc

# E4: Direct Boot, desde la Flash (c3_flash.ld); la imagen es el .bin
direct_boot.elf: direct_boot.c $(COMUN) c3.h c3_flash.ld
	$(CC) $(ARCH) -O2 -g -ffreestanding -fno-builtin -nostdlib -nostartfiles -Wall \
	      -Wl,--no-warn-rwx-segments -T c3_flash.ld -o $@ $(COMUN) $< -lgcc
direct_boot.bin: direct_boot.elf
	$(CROSS)objcopy -O binary $< $@

CM      := ../coremark
CMFLAGS := $(ARCH) -O2 -ffreestanding -fno-builtin -std=gnu99 -specs=picolibc.specs \
           -DITERATIONS=1 -DPERFORMANCE_RUN=1 -DFLAGS_STR="\"-O2 -march=rv32imc\"" \
           -DCOMPILER_VERSION="\"riscv64-unknown-elf-gcc\"" -I$(CM) -Wall -Wno-unused-parameter
CMSRC   := $(CM)/core_list_join.c $(CM)/core_matrix.c $(CM)/core_state.c \
           $(CM)/core_util.c $(CM)/ee_printf.c $(CM)/cvt.c \
           coremark/core_portme_c3.c coremark/cm_c3.c

coremark_c3.elf: $(CMSRC) $(CM)/core_main.c $(COMUN) c3.h c3_iram.ld
	$(CC) $(CMFLAGS) -Dmain=coremark_main -c -o core_main_c3.o $(CM)/core_main.c
	$(CC) $(CMFLAGS) -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments -T c3_iram.ld \
	      -o $@ $(COMUN) core_main_c3.o $(CMSRC) -lgcc
	rm -f core_main_c3.o

clean:
	rm -f *.elf direct_boot.bin
.PHONY: all clean
