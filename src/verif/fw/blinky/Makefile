# =============================================================================
# Makefile — Blinky con CMSIS (criterio de salida de la fase F3)
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar. El resultado es un firmware indistinguible del que se
# grabaría en una placa STM32F4-Discovery.
#
#   make            # genera blinky.bin
#   make dump       # desensamblado para inspección
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
OBJDUMP:= $(CROSS)objdump

CMSIS  := ../cmsis

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F407xx -DHSE_VALUE=8000000 -I$(CMSIS)/Include \
          -ffunction-sections -fdata-sections
LDFLAGS:= -T ../stm32f407.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=blinky.map

OBJS := startup_stm32f407xx.o system_stm32f4xx.o main.o

all: blinky.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f407xx.o: $(CMSIS)/startup_stm32f407xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

blinky.elf: $(OBJS) ../stm32f407.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

blinky.bin: blinky.elf
	$(OBJCOPY) -O binary $< $@

dump: blinky.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
