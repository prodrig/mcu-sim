# =============================================================================
# Makefile — Firmware de demostracion del acelerador criptografico (CRYP y HASH)
#
# Es el firmware de la FASE 6 del plan del F415/F417: usa los dos bloques
# **desde dentro del chip**, con el nucleo ejecutando de verdad, y no desde el
# banco escribiendo registros por fuera.
#
# Se compila con `-DSTM32F417xx` y con la cabecera `stm32f417xx.h` de ST, que
# es la que declara `CRYP` y `HASH`. El startup y el `system_stm32f4xx.c` son
# los mismos del F407 y valen tal cual: el arranque de las dos referencias es
# identico y este firmware no usa ninguna interrupcion, asi que la unica
# posicion de vector que las distingue -la 79, del CRYP- no interviene.
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar.
#
#   make            # genera crypto_demo.bin
#   make dump       # desensamblado para inspección
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
OBJDUMP:= $(CROSS)objdump

CMSIS  := ../cmsis

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F417xx -DHSE_VALUE=8000000 -I$(CMSIS)/Include \
          -ffunction-sections -fdata-sections
LDFLAGS:= -T ../stm32f407.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=crypto_demo.map

OBJS := startup_stm32f407xx.o system_stm32f4xx.o main.o

all: crypto_demo.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f407xx.o: $(CMSIS)/startup_stm32f407xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

crypto_demo.elf: $(OBJS) ../stm32f407.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

crypto_demo.bin: crypto_demo.elf
	$(OBJCOPY) -O binary $< $@

dump: crypto_demo.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
