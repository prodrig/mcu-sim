# =============================================================================
# Makefile — e4_rom, un firmware que solo usa la DriverLib de la ROM (T6)
#
# Todo por MAP_xxx(), que va a la ROM R1 (o a la de TI con --rom); de DriverLib
# solo se enlaza SysCtlClockFreqSet. Cabeceras de TI (../sdk, BSD-3) y CMSIS.
# Mismo compilador que los demas firmwares; -ffile-prefix-map por la huella.
#
#   make            # genera e4_rom.out y e4_rom.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra -Wno-old-style-declaration \
          -Werror=implicit-function-declaration \
          -D__MSP432E401Y__ -I../sdk -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../msp432e401y.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=e4_rom.map -Wl,--no-warn-rwx-segments

all: e4_rom.out e4_rom.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

# sysctl.o solo por SysCtlClockFreqSet, la unica sin casilla en rom.h; con
# --gc-sections no entra nada mas de ella
sysctl.o: ../sdk/ti/devices/msp432e4/driverlib/sysctl.c
	$(CC) $(CFLAGS) -g0 -Wno-unused-parameter -Wno-extra -c -o $@ $<

e4_rom.out: startup.o main.o sysctl.o ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o sysctl.o -lgcc

e4_rom.bin: e4_rom.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
