# =============================================================================
# Makefile — f3_vcp, el puerto COM virtual de la STM32F3DISCOVERY (S3)
#
# Con la cabecera de dispositivo de ST (../sdk/cmsis_device_f3, Apache-2.0) y
# el CMSIS-Core de los STM32F4 (../../cmsis). Sin HAL. -ffile-prefix-map por la
# huella (T-22).
#
#   make            # genera f3_vcp.elf y f3_vcp.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F303xC -I../sdk/cmsis_device_f3/Include -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../stm32f303vc.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=f3_vcp.map -Wl,--no-warn-rwx-segments

all: f3_vcp.elf f3_vcp.bin

startup.o: ../startup_f3.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

f3_vcp.elf: startup.o main.o ../stm32f303vc.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o -lgcc

f3_vcp.bin: f3_vcp.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean
