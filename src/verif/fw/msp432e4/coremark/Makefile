# =============================================================================
# Makefile — CoreMark sobre el MSP432E401Y (fase T6)
#
# Los MISMOS fuentes y el mismo port que el CoreMark del F407 (../../coremark:
# EEMBC, Apache-2.0), con dos cambios que son los del chip y nada mas:
#
#   ../msp432e401y.ld   la Flash en 0x0 y 256 KB de SRAM
#   ../startup_e4.s     111 IRQ; los perros del E4 arrancan apagados
#                       y no hay que pararlos
#
# Corre a los 16 MHz del reset (PIOSC): el objetivo es validar el nucleo con la
# integracion de TI, no medir.
#
#   make            # genera coremark_e4.bin
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
          -I$(CM) -Wall -Wno-unused-parameter
LDFLAGS := -T ../msp432e401y.ld -nostdlib -nostartfiles -Wl,-Map=coremark_e4.map
LDLIBS  := -lgcc

SRCS := core_main.c core_list_join.c core_matrix.c core_state.c core_util.c \
        core_portme.c ee_printf.c cvt.c cm_console.c
OBJS := $(SRCS:.c=.o) startup_cm.o

all: coremark_e4.bin

startup_cm.o: ../startup_e4.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -DENTRY_SYMBOL=cm_start -c -o $@ $<

%.o: $(CM)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

coremark_e4.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

coremark_e4.bin: coremark_e4.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean
