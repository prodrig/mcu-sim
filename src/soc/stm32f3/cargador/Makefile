# =============================================================================
# El cargador de sistema del F303 del modelo (S7): cargador.bin, que
# ../gen_sysmem.sh convierte en ../sysmem.inc (se versiona el .inc).
# Solo CMSIS; -ffile-prefix-map por la reproducibilidad (T-22).
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
FW     := ../../../verif/fw
CFLAGS := -mcpu=cortex-m4 -mthumb -Os -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F303xC -I$(FW)/stm32f3/sdk/cmsis_device_f3/Include -I$(FW)/cmsis/Include \
          -ffunction-sections -fdata-sections -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T cargador.ld -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-Map=cargador.map \
          -Wl,--no-warn-rwx-segments

all: cargador.bin

cargador.elf: main.c cargador.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c -lgcc

cargador.bin: cargador.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin *.map

.PHONY: all clean
