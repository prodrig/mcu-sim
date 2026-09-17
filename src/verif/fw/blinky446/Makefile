# =============================================================================
# Makefile — Blinky de una NUCLEO-F446RE (criterio de salida de la fase 2)
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar. El resultado es un firmware indistinguible del que se
# grabaria en una NUCLEO-F446RE.
#
# Dos diferencias con el blinky del F407, y las dos importan:
#   -DSTM32F446xx     elige la cabecera de dispositivo: otra tabla de vectores,
#                     otro juego de perifericos, sin ETH ni RNG
#   ../stm32f446.ld   512 KB de Flash y ninguna CCM
#
#   make            # genera blinky446.bin
#   make dump       # desensamblado para inspeccion
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
OBJDUMP:= $(CROSS)objdump

CMSIS  := ../cmsis

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F446xx -DHSE_VALUE=8000000 -I$(CMSIS)/Include \
          -ffunction-sections -fdata-sections
LDFLAGS:= -T ../stm32f446.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=blinky446.map

OBJS := startup_stm32f446xx.o system_stm32f4xx.o main.o

all: blinky446.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f446xx.o: $(CMSIS)/startup_stm32f446xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

blinky446.elf: $(OBJS) ../stm32f446.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

blinky446.bin: blinky446.elf
	$(OBJCOPY) -O binary $< $@

dump: blinky446.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
