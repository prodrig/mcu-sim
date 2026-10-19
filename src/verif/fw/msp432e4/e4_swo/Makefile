# =============================================================================
# Makefile — e4_swo, la traza SWO por el ITM (X1 de doc/analisis_xds110.md)
#
# Con la cabecera de dispositivo de TI (../sdk, BSD-3) y el CMSIS-Core oficial
# que ya usan los STM32 (../../cmsis, Apache-2.0). Mismo compilador que los
# firmwares de los STM32 y del P4. -ffile-prefix-map por la huella (T-22).
#
#   make            # genera e4_swo.out y e4_swo.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -D__MSP432E401Y__ -I../sdk -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../msp432e401y.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=e4_swo.map -Wl,--no-warn-rwx-segments

all: e4_swo.out e4_swo.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

e4_swo.out: startup.o main.o ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o -lgcc

e4_swo.bin: e4_swo.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
