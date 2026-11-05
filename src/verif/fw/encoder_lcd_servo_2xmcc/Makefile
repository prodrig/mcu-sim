# =============================================================================
# Makefile — Dos motores con encoder tras un L298N, un servo, un encoder,
#            una pantalla y un FTDI en una NUCLEO-F446RE
#
# Se compila con la cabecera de dispositivo de ST y el CMSIS-Core oficiales,
# incluidos en ../cmsis (Apache-2.0), y con el startup y el system_stm32f4xx.c
# de ST sin modificar. El resultado es un firmware indistinguible del que se
# grabaria en una NUCLEO-F446RE.
#
# Las mismas opciones que el blinky de la Nucleo (../blinky446): es el
# firmware de placas/nucleo_f446re_encoder_lcd_servo_2xmcc.xml (vease main.c).
#
#   make            # genera encoder_lcd_servo_2xmcc.bin
#   make dump       # desensamblado para inspeccion
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
OBJDUMP:= $(CROSS)objdump

CMSIS  := ../cmsis

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -DSTM32F446xx -DHSE_VALUE=8000000 -I$(CMSIS)/Include \
          -ffunction-sections -fdata-sections
LDFLAGS:= -T ../stm32f446.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=encoder_lcd_servo_2xmcc.map

OBJS := startup_stm32f446xx.o system_stm32f4xx.o main.o

all: encoder_lcd_servo_2xmcc.bin

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

system_stm32f4xx.o: $(CMSIS)/system_stm32f4xx.c
	$(CC) $(CFLAGS) -c -o $@ $<

startup_stm32f446xx.o: $(CMSIS)/startup_stm32f446xx.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

encoder_lcd_servo_2xmcc.elf: $(OBJS) ../stm32f446.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lgcc

encoder_lcd_servo_2xmcc.bin: encoder_lcd_servo_2xmcc.elf
	$(OBJCOPY) -O binary $< $@

dump: encoder_lcd_servo_2xmcc.elf
	$(OBJDUMP) -d -S $< | less

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: all clean dump
