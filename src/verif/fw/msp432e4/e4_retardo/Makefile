# =============================================================================
# Makefile — e4_retardo, lo que tarda una vuelta de SysCtlDelay a 120 MHz
#
# DriverLib por MAP_xxx() (la ROM) y, de sysctl.c, SysCtlClockFreqSet y
# SysCtlDelay (la de la Flash, para medirla). retardos.s, el mismo bucle en
# sitios elegidos. Cabeceras de TI (../sdk, BSD-3) y CMSIS.
# Mismo compilador que los demas firmwares; -ffile-prefix-map por la huella.
#
#   make            # genera e4_retardo.out y e4_retardo.bin
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
          -Wl,-Map=e4_retardo.map -Wl,--no-warn-rwx-segments

all: e4_retardo.out e4_retardo.bin

startup.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

retardos.o: retardos.s
	$(CC) $(CFLAGS) -c -o $@ $<

# sysctl.o por SysCtlClockFreqSet y SysCtlDelay; con --gc-sections no entra
# nada mas de ella
sysctl.o: ../sdk/ti/devices/msp432e4/driverlib/sysctl.c
	$(CC) $(CFLAGS) -g0 -Wno-unused-parameter -Wno-extra -c -o $@ $<

e4_retardo.out: startup.o main.o retardos.o sysctl.o ../msp432e401y.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o retardos.o sysctl.o -lgcc

e4_retardo.bin: e4_retardo.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
