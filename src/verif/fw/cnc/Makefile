# =============================================================================
# Makefile — Un control numerico pequeno para la fresadora 3020
#            con una CNC Shield V3 sobre una NUCLEO-F446RE (fase F3 de la CNC)
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar. Las mismas opciones que el blinky de la Nucleo
# (../blinky446): es el firmware de placas/cnc_3020.xml (vease
# main.c).
#
#   make            # genera cnc.bin
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
          -Wl,-Map=cnc.map

OBJS := startup_stm32f446xx.o system_stm32f4xx.o main.o

all: cnc.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f446xx.o: $(CMSIS)/startup_stm32f446xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

cnc.elf: $(OBJS) ../stm32f446.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

cnc.bin: cnc.elf
	$(OBJCOPY) -O binary $< $@

dump: cnc.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
