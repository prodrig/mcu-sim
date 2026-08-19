# =============================================================================
# Makefile — CoreMark sobre el modelo SystemC del STM32F407VG (fase F2)
#
# Fuentes de CoreMark (EEMBC, Apache-2.0; ver LICENSE.md) con el port
# "barebones" adaptado: reloj tomado del SysTick y salida a una consola virtual
# en la SRAM que el banco de pruebas vuelca al terminar.
#
#   make                  # genera coremark.bin
#   make ITERATIONS=50    # más iteraciones (más tiempo de simulación)
#
# Nota: una puntuación CoreMark oficial exige al menos 10 s de ejecución real.
# Aquí se usa un número reducido de iteraciones porque el objetivo es validar
# funcionalmente la ISA y medir el rendimiento del modelo, no certificar.
# =============================================================================
CROSS      ?= arm-none-eabi-
CC         := $(CROSS)gcc
OBJCOPY    := $(CROSS)objcopy
ITERATIONS ?= 1

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O2 -g -ffreestanding -fno-builtin -std=gnu99 \
          -DITERATIONS=$(ITERATIONS) -DPERFORMANCE_RUN=1 \
          -DFLAGS_STR="\"-O2 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16\"" \
          -DCOMPILER_VERSION="\"arm-none-eabi-gcc\"" \
          -I. -Wall -Wno-unused-parameter

# -lgcc aporta las rutinas de doble precision por software que usa CoreMark
# para calcular la puntuacion; el Cortex-M4F solo tiene FPU de simple
# precision [II, 5], asi que se ejecutan como codigo Thumb-2 normal.
LDFLAGS := -T ../stm32f407.ld -nostdlib -nostartfiles -Wl,-Map=coremark.map
LDLIBS  := -lgcc

SRCS := core_main.c core_list_join.c core_matrix.c core_state.c core_util.c \
        core_portme.c ee_printf.c cvt.c cm_console.c
OBJS := $(SRCS:.c=.o) startup_cm.o

all: coremark.bin

startup_cm.o: ../startup.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -DENTRY_SYMBOL=cm_start -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

coremark.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

coremark.bin: coremark.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean
