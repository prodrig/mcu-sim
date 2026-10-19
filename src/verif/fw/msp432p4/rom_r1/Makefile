# =============================================================================
# Makefile — rom_r1, la ROM sustituta del MSP432P401R (fase T4, opcion R1)
#
# DriverLib entera (../sdk, BSD-3, sin modificar) compilada con el mismo
# compilador que los firmwares, y enlazada en la ventana de la ROM con las
# tablas que pide rom.h, en su orden. Las tablas las escribe gen_tablas.py
# leyendo rom.h: nunca a mano [doc/analisis_msp432.md, §9, riesgo 4].
#
#   make            # tablas.c, rom_r1.out, rom_r1.bin (32 KB justos)
#                   # y ../../../../soc/msp432p4/rom_r1.inc, lo que lleva
#                   # dentro el simulador
#
# La imagen esta versionada, con su huella en verif/fw/huellas.txt, igual que
# los firmwares: regenerarla con otra cadena cruzada da otra ROM y mueve el
# tiempo simulado de todo lo que la llama (T-22).
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
PYTHON ?= python3

SDK    := ../sdk
DL     := $(SDK)/ti/devices/msp432p4xx/driverlib
# Todos los modulos de DriverLib; los de otros chips (flash_a, sysctl_a,
# lcd_f) se compilan vacios para el P401R.
MODULOS:= adc14 aes256 comp_e cpu crc32 cs dma flash fpu gpio i2c interrupt \
          mpu pcm pmap pss ref_a reset rtc_c spi sysctl systick timer32 \
          timer_a uart wdt_a

# La version que devuelve ROM_VERSION (ROM_APITABLE[0]). La de la ROM de TI
# no esta publicada; esta es la de la R1, y el TLV sintetico dice lo mismo en
# ROM_DRVLIB_REV.
VERSION:= 0x00000001

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -Os -g -ffreestanding -fno-builtin -std=gnu11 -Wall \
          -Wno-unused-parameter -Wno-maybe-uninitialized \
          -Werror=implicit-function-declaration \
          -D__MSP432P401R__ -DNO_MSP_CLASSIC_DEFINES -DROM_R1_VERSION=$(VERSION) \
          -I$(SDK) -I$(SDK)/ti/devices/msp432p4xx/inc -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T rom_r1.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=rom_r1.map -Wl,--no-warn-rwx-segments

OBJS := cabecera.o tablas.o $(addsuffix .o,$(MODULOS))
INC  := ../../../../soc/msp432p4/rom_r1.inc

all: rom_r1.out rom_r1.bin $(INC)

tablas.c: gen_tablas.py $(DL)/rom.h
	$(PYTHON) gen_tablas.py $(DL)/rom.h > $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: $(DL)/%.c
	$(CC) $(CFLAGS) -g0 -Wno-strict-aliasing -Wno-parentheses -c -o $@ $<

rom_r1.out: $(OBJS) rom_r1.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

rom_r1.bin: rom_r1.out verifica_tablas.py
	$(OBJCOPY) -O binary -j .cabecera -j .apitable -j .text -j .relleno $< $@
	$(PYTHON) verifica_tablas.py $(CC) $(DL)/rom.h $< $@ || { rm -f $@; exit 1; }

$(INC): rom_r1.bin a_inc.py
	$(PYTHON) a_inc.py $< > $@

# Segunda opinion, opcional: las tablas contra la ROM de TI que trae CCS
# (no se versiona). make contrasta ROM_TI=.../msp432_driverlib_rom_image.out
contrasta: rom_r1.out
	@test -n "$(ROM_TI)" || { echo "make contrasta ROM_TI=<msp432_driverlib_rom_image.out de CCS>"; exit 2; }
	$(PYTHON) contrasta_ti.py $(DL)/rom.h rom_r1.out "$(ROM_TI)"

clean:
	rm -f *.o *.out *.bin *.map tablas.c

.PHONY: all clean contrasta
