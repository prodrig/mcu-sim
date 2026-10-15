# =============================================================================
# Makefile — driverlib_t3, los ejemplos de DriverLib de la fase T3 del
# MSP432P401R, contra el modelo
#
# Con DriverLib de TI de verdad, compilada aqui desde sus fuentes (../sdk, la
# copia del SDK de TI con su licencia BSD-3, solo los modulos que hacen
# falta) y llamada SIN la ROM (sin MAP_/ROM_: la ROM llega en T4). Es la
# segunda fuente de las secuencias: si el modelo y DriverLib discrepan en un
# bit, aqui se ve.
#
# Mismo compilador y mismas opciones que p4_t3, con -ffile-prefix-map para
# que la huella del .out no dependa de donde se compila (T-22).
#
#   make            # genera driverlib_t3.out y driverlib_t3.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

SDK    := ../sdk
DL     := $(SDK)/ti/devices/msp432p4xx/driverlib
MODULOS:= adc14 cpu cs dma gpio interrupt pcm timer_a uart wdt_a flash sysctl

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall \
          -D__MSP432P401R__ -DNO_MSP_CLASSIC_DEFINES \
          -I$(SDK) -I$(SDK)/ti/devices/msp432p4xx/inc -I../ti -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../msp432p401r.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=driverlib_t3.map

OBJS := startup.o main.o $(addsuffix .o,$(MODULOS))

all: driverlib_t3.out driverlib_t3.bin

startup.o: ../startup_p4_t3.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: $(DL)/%.c
	$(CC) $(CFLAGS) -g0 -Wno-unused-parameter -Wno-maybe-uninitialized -c -o $@ $<

driverlib_t3.out: $(OBJS) ../msp432p401r.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

driverlib_t3.bin: driverlib_t3.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
