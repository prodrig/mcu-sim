# =============================================================================
# Makefile — Las sumas paralelas y sus banderas GE, con SEL, y el strlen de newlib
#            (regresion del nucleo que destapo grblHAL, fase F3 de la CNC)
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar. Las mismas opciones que el blinky de la Nucleo
# (../blinky446): se prueba en `make gui-cnc` (C7) (vease
# main.c).
#
#   make            # genera simd_ge.bin
#   make dump       # desensamblado para inspeccion
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
OBJDUMP:= $(CROSS)objdump

CMSIS  := ../cmsis

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -I.  -std=gnu11 -Wall -Wextra \
          -DSTM32F446xx -DHSE_VALUE=8000000 -I$(CMSIS)/Include \
          -ffunction-sections -fdata-sections
LDFLAGS:= -T ../stm32f446.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=simd_ge.map

OBJS := startup_stm32f446xx.o system_stm32f4xx.o main.o

all: simd_ge.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f446xx.o: $(CMSIS)/startup_stm32f446xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

simd_ge.elf: $(OBJS) ../stm32f446.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lc_nano -lgcc

simd_ge.bin: simd_ge.elf
	$(OBJCOPY) -O binary $< $@

dump: simd_ge.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
