# =============================================================================
# Makefile — bsl_b1, el BSL USB de TI del F5529 (M9): las fuentes BSD-3 de
# MSPDebugStack 3.15.1.1 (ThirdParty/BSL430_Firmware, el de la eZ-FET) en ti/,
# pasadas a msp430-elf-gcc por portar.py, para la LaunchPad (PID 0x0200, XT2
# en P5.2/P5.3) y en su versión de Flash (RAM_WRITE_ONLY_BSL: contraseña,
# RX Data Block Fast y Load PC; el resto lo da el BSL de RAM que carga la
# herramienta). Con -flto cabe en los 2 KB: 0x1000-0x17FF. Va dentro del
# simulador (soc/msp430/f5529/bsl_f5529.inc); su huella, en verif/fw/huellas.txt.
#   make [MSPGCC=<prefijo>]
# =============================================================================
MSPGCC ?= msp430-elf-
REFS   ?= ../../../../../doc/refs/msp430
TI     ?= $(REFS)/ti/msp430/include_gcc
PYTHON ?= python3
CFLAGS := -mmcu=msp430f5529 -msmall -Os -flto -ffunction-sections -fdata-sections \
          -DRAM_WRITE_ONLY_BSL_DEF -DF5529_LP -I$(TI) -I. -Iti -include gcc_iar.h -w \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -L$(TI) -L. -T bsl_f5529.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=bsl_f5529.map
INC    := ../../../../soc/msp430/f5529/bsl_f5529.inc
FUENTES:= $(wildcard ti/*.c)

all: bsl_f5529.out bsl_f5529.bin $(INC)

crt_f5529.o: crt_f5529.S
	$(MSPGCC)gcc -mmcu=msp430f5529 -msmall -c -o $@ $<

bsl_f5529.out: crt_f5529.o $(FUENTES) bsl_f5529.ld ubicaciones.ld gcc_iar.h
	$(MSPGCC)gcc $(CFLAGS) $(LDFLAGS) -o $@ crt_f5529.o $(FUENTES) -lgcc

bsl_f5529.bin: bsl_f5529.out
	$(MSPGCC)objcopy -O binary -j .zarea -j .vers -j .text0 -j .desc -j .text -j .bslsig --pad-to 0x1800 --gap-fill 0xFF $< $@
	@test $$(wc -c < $@) -eq 2048 || { echo "bsl_f5529.bin no tiene 2 KB"; rm -f $@; exit 1; }

$(INC): bsl_f5529.bin ../../msp430_bsl_inc.py
	$(PYTHON) ../../msp430_bsl_inc.py $< BSL_F5529 > $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
