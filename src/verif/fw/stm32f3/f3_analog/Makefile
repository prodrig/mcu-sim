# =============================================================================
# Makefile — f3_analog, lo analogico del STM32F303VC (S5)
#
# Con la cabecera de dispositivo de ST (../sdk/cmsis_device_f3, Apache-2.0), las
# funciones en linea de sus LL (../sdk/hal/Inc, BSD-3) y
# el CMSIS-Core de los STM32F4 (../../cmsis). Sin HAL. -ffile-prefix-map por la
# huella (T-22).
#
#   make            # genera f3_analog.elf y f3_analog.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F303xC -I../sdk/cmsis_device_f3/Include -I../../cmsis/Include -I../sdk/hal/Inc \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../stm32f303vc.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=f3_analog.map -Wl,--no-warn-rwx-segments

all: f3_analog.elf f3_analog.bin

startup.o: ../startup_f3.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

f3_analog.elf: startup.o main.o ../stm32f303vc.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o -lgcc

f3_analog.bin: f3_analog.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean
