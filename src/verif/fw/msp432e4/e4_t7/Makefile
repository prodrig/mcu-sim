# =============================================================================
# Makefile — e4_t7, los perifericos de la T7 del MSP432E401Y con DriverLib (CCM, CAN...)
#
# DriverLib del E4 (../sdk, BSD-3) compilada de sus fuentes: solo los modulos
# que hacen falta. Mismo compilador; -ffile-prefix-map por la huella (T-22).
#
#   make            # genera e4_t7.out y e4_t7.bin
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
          -Wl,-Map=e4_t7.map -Wl,--no-warn-rwx-segments

OBJS := startup.o main.o can_e4.o sysctl.o aes.o des.o shamd5.o crc.o can.o gpio.o interrupt.o cpu.o

all: e4_t7.out e4_t7.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c buzon.h
	$(CC) $(CFLAGS) -c -o $@ $<

can_e4.o: can_e4.c buzon.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: $(DL)/%.c
	$(CC) $(CFLAGS) -g0 -Wno-unused-parameter -c -o $@ $<

e4_t7.out: $(OBJS) ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

e4_t7.bin: e4_t7.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
