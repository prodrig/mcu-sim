# =============================================================================
# Makefile — e4_demo, el firmware de verificacion de la fase T6 del MSP432E401Y
#
# Con la cabecera de dispositivo de TI (../sdk, BSD-3) y el CMSIS-Core oficial
# que ya usan los STM32 (../../cmsis, Apache-2.0). Mismo compilador que los
# firmwares de los STM32 y del P4. -ffile-prefix-map por la huella (T-22).
#
#   make            # genera e4_demo.out y e4_demo.bin
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
          -Wl,-Map=e4_demo.map -Wl,--no-warn-rwx-segments

all: e4_demo.out e4_demo.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

e4_demo.out: startup.o main.o ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o -lgcc

e4_demo.bin: e4_demo.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
