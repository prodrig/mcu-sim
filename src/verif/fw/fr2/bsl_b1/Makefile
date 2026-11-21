# =============================================================================
# Makefile — bsl_b1, el BSL propio de las FR2xx/FR4xx (M10): AGPLv3, sin código
# de TI. Dos imágenes de bsl_fr2.c, dentro del simulador:
#   bsl_fr2  FR23xx-FR26xx: el BSL1 (bsl_fr2.bin, 2 KB, 0x1000) y el BSL2
#            (bsl_fr2b.bin, 1 KB, 0xFFC00) -> soc/msp430/fr2/bsl_fr2.inc
#   bsl_fr4  FR4xx: 1 KB en 0x1000 -> soc/msp430/fr2/bsl_fr4.inc
# Sus huellas están en verif/fw/huellas.txt.
#   make [MSPGCC=<prefijo>]
# =============================================================================
MSPGCC ?= msp430-elf-
REFS   ?= ../../../../../doc/refs/msp430
TI     ?= $(REFS)/ti/msp430/include_gcc
PYTHON ?= python3
COMUN  := -Os -g -ffreestanding -I$(TI) -I../../../fr2476/shim -Wall -Wextra -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
INCDIR := ../../../../soc/msp430/fr2

all: bsl_fr2.bin bsl_fr2b.bin bsl_fr4.bin $(INCDIR)/bsl_fr2.inc $(INCDIR)/bsl_fr4.inc

bsl_fr2.out: bsl_fr2.c crt_fr2.S bsl_fr2.ld
	$(MSPGCC)gcc -mmcu=msp430fr2476 -mlarge -mcode-region=lower -mdata-region=lower $(COMUN) \
	    -L$(TI) -T bsl_fr2.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=bsl_fr2.map -Wl,--entry=bsl_z \
	    -o $@ crt_fr2.S bsl_fr2.c -lgcc
bsl_fr4.out: bsl_fr2.c crt_fr2.S bsl_fr4.ld
	$(MSPGCC)gcc -mmcu=msp430fr4133 -DBSL_FR4 $(COMUN) \
	    -L$(TI) -T bsl_fr4.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=bsl_fr4.map -Wl,--entry=bsl_z \
	    -o $@ crt_fr2.S bsl_fr2.c -lgcc

bsl_fr2.bin: bsl_fr2.out
	$(MSPGCC)objcopy -O binary -j .zarea -j .text --pad-to 0x1800 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 2048 || { echo "bsl_fr2.bin no tiene 2 KB"; rm -f $@; exit 1; }
bsl_fr2b.bin: bsl_fr2.out
	$(MSPGCC)objcopy -O binary -j .bsl2 --pad-to 0x100000 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 1024 || { echo "bsl_fr2b.bin no tiene 1 KB"; rm -f $@; exit 1; }
bsl_fr4.bin: bsl_fr4.out
	$(MSPGCC)objcopy -O binary -j .zarea -j .text --pad-to 0x1400 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 1024 || { echo "bsl_fr4.bin no tiene 1 KB"; rm -f $@; exit 1; }

$(INCDIR)/bsl_fr2.inc: bsl_fr2.bin bsl_fr2b.bin ../../msp430_bsl_inc.py
	{ $(PYTHON) ../../msp430_bsl_inc.py bsl_fr2.bin BSL_FR2; \
	  echo '};'; echo 'inline const uint8_t IMAGEN2[] = {'; \
	  $(PYTHON) ../../msp430_bsl_inc.py bsl_fr2b.bin BSL_FR2B; } > $@
$(INCDIR)/bsl_fr4.inc: bsl_fr4.bin ../../msp430_bsl_inc.py
	$(PYTHON) ../../msp430_bsl_inc.py $< BSL_FR4 > $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
