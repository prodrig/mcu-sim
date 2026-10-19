# =============================================================================
# Makefile — rom_r1, la ROM sustituta del MSP432E401Y (fase T6, opcion R1)
#
# DriverLib del E4 entera (../sdk, BSD-3, sin modificar) compilada con el mismo
# compilador que los firmwares, enlazada en la ventana de la ROM con las tablas
# que pide rom.h, en su orden, y el cargador serie propio (cargador.c). Las
# tablas las escribe gen_tablas.py leyendo rom.h: nunca a mano
# [doc/analisis_msp432.md, §9, riesgo 4].
#
#   make            # tablas.c, rom_r1.out, rom_r1.bin (256 KB justos)
#                   # y ../../../../soc/msp432e4/rom_r1.inc, lo que lleva
#                   # dentro el simulador
#
# La imagen esta versionada, con su huella en verif/fw/huellas.txt, igual que
# los firmwares: regenerarla con otra cadena cruzada da otra ROM (T-22).
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
NM     := $(CROSS)nm
OBJCOPY:= $(CROSS)objcopy
PYTHON ?= python3

SDK    := ../sdk
DL     := $(SDK)/ti/devices/msp432e4/driverlib
MODULOS:= adc aes can comp cpu crc des eeprom emac epi flash fpu gpio hibernate \
          i2c interrupt lcd mpu onewire pwm qei shamd5 ssi sw_crc sysctl sysexc \
          systick timer uart udma usb watchdog

# La version que devuelve ROM_APITABLE[0]. La de la ROM de TI no esta
# publicada; esta es la de la R1.
VERSION:= 0x00000001

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -Os -g -ffreestanding -fno-builtin -std=gnu11 -Wall \
          -Wno-unused-parameter -Wno-maybe-uninitialized \
          -Werror=implicit-function-declaration \
          -D__MSP432E401Y__ -DROM_R1_VERSION=$(VERSION) \
          -I$(SDK) -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T rom_r1.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=rom_r1.map -Wl,--no-warn-rwx-segments

DLOBJS := $(addsuffix .o,$(MODULOS))
OBJS   := cabecera.o cargador.o cargador_eth.o tablas.o $(DLOBJS)
INC    := ../../../../soc/msp432e4/rom_r1.inc

all: rom_r1.out rom_r1.bin $(INC)

# Las funciones que de verdad hay en DriverLib: las casillas de rom.h sin
# fuente (SMBus, SPI Flash, las Update* que no son UART) quedan en hueco
simbolos.txt: $(DLOBJS)
	$(NM) -g --defined-only $(DLOBJS) | awk '$$2 == "T" {print $$3}' | sort -u > $@

tablas.c: gen_tablas.py $(DL)/rom.h simbolos.txt
	$(PYTHON) gen_tablas.py $(DL)/rom.h simbolos.txt > $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: $(DL)/%.c
	$(CC) $(CFLAGS) -g0 -Wno-strict-aliasing -Wno-parentheses -w -c -o $@ $<

rom_r1.out: $(OBJS) rom_r1.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

rom_r1.bin: rom_r1.out verifica_tablas.py
	$(OBJCOPY) -O binary -j .vectores -j .apitable -j .text -j .relleno $< $@
	$(PYTHON) verifica_tablas.py $(CC) $(DL)/rom.h $< $@ || { rm -f $@; exit 1; }

$(INC): rom_r1.bin a_inc.py
	$(PYTHON) a_inc.py $< > $@

clean:
	rm -f *.o *.out *.bin *.map tablas.c simbolos.txt

.PHONY: all clean
