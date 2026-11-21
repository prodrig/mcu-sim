# =============================================================================
# Makefile — bsl_b1, el BSL propio de las FR5xx/FR6xx (M9): AGPLv3, sin código
# de TI. Va dentro del simulador (soc/msp430/fr2/bsl_fr5.inc); su huella está en
# verif/fw/huellas.txt.
#   make [MSPGCC=<prefijo>]     # bsl_fr5.out, bsl_fr5.bin (2 KB) y bsl_fr5.inc
# =============================================================================
MSPGCC ?= msp430-elf-
REFS   ?= ../../../../../doc/refs/msp430
TI     ?= $(REFS)/ti/msp430/include_gcc
PYTHON ?= python3
CFLAGS := -mmcu=msp430fr5969 -mlarge -mcode-region=lower -mdata-region=lower -Os -g -ffreestanding \
          -I$(TI) -Wall -Wextra -ffunction-sections -fdata-sections -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -L$(TI) -T bsl_fr5.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=bsl_fr5.map -Wl,--entry=bsl_z
INC    := ../../../../soc/msp430/fr2/bsl_fr5.inc

all: bsl_fr5.out bsl_fr5.bin $(INC)

bsl_fr5.out: bsl_fr5.c crt_fr5.S bsl_fr5.ld
	$(MSPGCC)gcc $(CFLAGS) $(LDFLAGS) -o $@ crt_fr5.S bsl_fr5.c -lgcc

bsl_fr5.bin: bsl_fr5.out
	$(MSPGCC)objcopy -O binary -j .zarea -j .text --pad-to 0x1800 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 2048 || { echo "bsl_fr5.bin no tiene 2 KB"; rm -f $@; exit 1; }

$(INC): bsl_fr5.bin ../../msp430_bsl_inc.py
	$(PYTHON) ../../msp430_bsl_inc.py $< BSL_FR5 > $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
