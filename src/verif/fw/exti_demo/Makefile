# =============================================================================
# Makefile — Firmware de demostracion de EXTI y SYSCFG con CMSIS (fase F4)
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar.
#
#   make            # genera exti_demo.bin
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
          -Wl,-Map=exti_demo.map

OBJS := startup_stm32f407xx.o system_stm32f4xx.o main.o

all: exti_demo.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f407xx.o: $(CMSIS)/startup_stm32f407xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

exti_demo.elf: $(OBJS) ../stm32f407.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

exti_demo.bin: exti_demo.elf
	$(OBJCOPY) -O binary $< $@

dump: exti_demo.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
