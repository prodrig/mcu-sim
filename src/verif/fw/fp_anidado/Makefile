# =============================================================================
# Makefile — Una interrupcion anidada no puede borrar los registros FP del hilo
#            principal (regresion del nucleo, fase F3 de la CNC)
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar. Las mismas opciones que el blinky de la Nucleo
# (../blinky446): se prueba en `make gui-cnc` (C6) (vease
# main.c).
#
#   make            # genera fp_anidado.bin
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
          -Wl,-Map=fp_anidado.map

OBJS := startup_stm32f446xx.o system_stm32f4xx.o main.o

all: fp_anidado.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f446xx.o: $(CMSIS)/startup_stm32f446xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

fp_anidado.elf: $(OBJS) ../stm32f446.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

fp_anidado.bin: fp_anidado.elf
	$(OBJCOPY) -O binary $< $@

dump: fp_anidado.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
