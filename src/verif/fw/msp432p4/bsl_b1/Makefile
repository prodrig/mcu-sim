# =============================================================================
# Makefile — bsl_b1, el BSL sustituto del MSP432P401R (fase T5, D4)
#
# Un BSL propio (AGPLv3) con el protocolo de TI, enlazado en su sector de la
# Flash de informacion (0x0020_2000, 8 KB). El simulador lo lleva dentro
# (../../../../soc/msp432p4/bsl_b1.inc), como la ROM R1; su huella esta en
# verif/fw/huellas.txt.
#
#   make            # bsl_b1.out, bsl_b1.bin (8 KB justos) y bsl_b1.inc
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
PYTHON ?= python3

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -Os -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -D__MSP432P401R__ -DNO_MSP_CLASSIC_DEFINES \
          -I../ti -I../../cmsis/Include -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T bsl_b1.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=bsl_b1.map -Wl,--no-warn-rwx-segments -Wl,--entry=bsl_entrada

INC := ../../../../soc/msp432p4/bsl_b1.inc

all: bsl_b1.out bsl_b1.bin $(INC)

bsl_b1.o: bsl_b1.c
	$(CC) $(CFLAGS) -c -o $@ $<

bsl_b1.out: bsl_b1.o bsl_b1.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bsl_b1.o -lgcc

bsl_b1.bin: bsl_b1.out
	$(OBJCOPY) -O binary --pad-to 0x00204000 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 8192 || { echo "bsl_b1.bin no tiene 8 KB"; rm -f $@; exit 1; }

$(INC): bsl_b1.bin a_inc.py
	$(PYTHON) a_inc.py $< > $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
