# =============================================================================
# Makefile — CoreMark sobre el STM32F303VC (fase S1)
#
# Los MISMOS fuentes y el mismo port que el CoreMark del F407 (../../coremark:
# EEMBC, Apache-2.0), con lo que es del chip y nada mas:
#
#   ../stm32f303vc.ld   256 KB de Flash y 40 KB de SRAM, el buzon al principio
#   ../startup_f3.s     82 IRQ, entrada en cm_start
#
# Corre a los 8 MHz del reset (HSI): el objetivo es validar el nucleo con la
# integracion del F303, no medir.
#
#   make            # genera coremark_f3.bin
# =============================================================================
CROSS      ?= arm-none-eabi-
CC         := $(CROSS)gcc
OBJCOPY    := $(CROSS)objcopy
ITERATIONS ?= 1
CM         := ../../coremark

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O2 -g -ffreestanding -fno-builtin -std=gnu99 \
          -DITERATIONS=$(ITERATIONS) -DPERFORMANCE_RUN=1 \
          -DFLAGS_STR="\"-O2 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16\"" \
          -DCOMPILER_VERSION="\"arm-none-eabi-gcc\"" \
          -I$(CM) -Wall -Wno-unused-parameter -ffile-prefix-map=$(CURDIR)=.
LDFLAGS := -T ../stm32f303vc.ld -nostdlib -nostartfiles -Wl,-Map=coremark_f3.map \
           -Wl,--no-warn-rwx-segments
LDLIBS  := -lgcc

SRCS := core_main.c core_list_join.c core_matrix.c core_state.c core_util.c \
        core_portme.c ee_printf.c cvt.c cm_console.c
OBJS := $(SRCS:.c=.o) startup_cm.o

all: coremark_f3.bin

startup_cm.o: ../startup_f3.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -DENTRY_SYMBOL=cm_start -c -o $@ $<

%.o: $(CM)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

coremark_f3.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

coremark_f3.bin: coremark_f3.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean
