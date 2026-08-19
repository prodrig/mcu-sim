# =============================================================================
# Makefile — Firmware de verificación de la fase F2
#
# Compila con la toolchain ARM bare-metal para Cortex-M4F y produce el .bin que
# el banco de pruebas carga en la Flash del modelo.
#
#   make            # genera test_isa.bin
#   make dump       # desensamblado para inspección
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
OBJDUMP:= $(CROSS)objdump

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -Wall -Wextra \
          -ffunction-sections -fdata-sections
LDFLAGS:= -T stm32f407.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=test_isa.map

OBJS := startup.o test_isa.o

all: test_isa.bin

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

test_isa.elf: $(OBJS) stm32f407.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

test_isa.bin: test_isa.elf
	$(OBJCOPY) -O binary $< $@

dump: test_isa.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
