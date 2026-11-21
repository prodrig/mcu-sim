# =============================================================================
# Makefile — bsl_b1, el BSL propio del G2553 (M9): AGPLv3, sin código de TI.
# Va dentro del simulador (soc/msp430/g2553/bsl_g2.inc); su huella está en
# verif/fw/huellas.txt.
#   make [MSPGCC=<prefijo>]     # bsl_g2.out, bsl_g2.bin (1 KB) y bsl_g2.inc
# =============================================================================
MSPGCC ?= msp430-elf-
REFS   ?= ../../../../../doc/refs/msp430
TI     ?= $(REFS)/ti/msp430/include_gcc
PYTHON ?= python3
CFLAGS := -mmcu=msp430g2553 -Os -g -ffreestanding -I$(TI) -Wall -Wextra -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -L$(TI) -T bsl_g2.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=bsl_g2.map -Wl,--entry=bsl_entrada
INC    := ../../../../soc/msp430/g2553/bsl_g2.inc

all: bsl_g2.out bsl_g2.bin $(INC)

bsl_g2.out: bsl_g2.c crt_g2.S bsl_g2.ld
	$(MSPGCC)gcc $(CFLAGS) $(LDFLAGS) -o $@ crt_g2.S bsl_g2.c -lgcc

bsl_g2.bin: bsl_g2.out
	$(MSPGCC)objcopy -O binary -j .entrada -j .text -j .info_rom --pad-to 0x1000 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 1024 || { echo "bsl_g2.bin no tiene 1 KB"; rm -f $@; exit 1; }

$(INC): bsl_g2.bin ../../msp430_bsl_inc.py
	$(PYTHON) ../../msp430_bsl_inc.py $< BSL_G2 > $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
