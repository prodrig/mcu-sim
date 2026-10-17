# =============================================================================
# Makefile — e4_nv, la Flash y la EEPROM del MSP432E401Y con DriverLib (T6)
#
# DriverLib del E4 (../sdk, BSD-3) compilada de sus fuentes: solo los modulos
# que hacen falta. Mismo compilador; -ffile-prefix-map por la huella (T-22).
#
#   make            # genera e4_nv.out y e4_nv.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
DL     := ../sdk/ti/devices/msp432e4/driverlib

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall \
          -D__MSP432E401Y__ -I../sdk -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../msp432e401y.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=e4_nv.map -Wl,--no-warn-rwx-segments

OBJS := startup.o main.o flash.o eeprom.o sysctl.o

all: e4_nv.out e4_nv.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: $(DL)/%.c
	$(CC) $(CFLAGS) -g0 -Wno-unused-parameter -c -o $@ $<

e4_nv.out: $(OBJS) ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

e4_nv.bin: e4_nv.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
