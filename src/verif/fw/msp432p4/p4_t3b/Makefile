# =============================================================================
# Makefile — p4_t3b, el segundo firmware de verificacion de la fase T3
# (analogicos, DMA, RTC, cifrado y bajo consumo)
#
# Con la cabecera de dispositivo de TI (../ti, BSD-3) y el CMSIS-Core oficial
# que ya usan los STM32 (../../cmsis, Apache-2.0). Mismo compilador que los
# firmwares de los STM32: no hace falta una segunda cadena de herramientas.
#
# -ffile-prefix-map quita del ELF la ruta de la maquina que compila: sin el,
# la informacion de depuracion lleva el directorio absoluto y la huella del
# .out cambiaria de un ordenador a otro (T-22).
#
# Sale en dos formatos, y el banco usa el ELF: es el `.out` de Code Composer
# Studio, y cargarlo es lo que prueba el cargador de ELF de T0.
#
#   make            # genera p4_t3b.out y p4_t3b.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O1 -g -ffreestanding -fno-builtin -std=gnu11 -Wall -Wextra \
          -D__MSP432P401R__ -DNO_MSP_CLASSIC_DEFINES \
          -I../ti -I../../cmsis/Include -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../msp432p401r.ld -nostdlib -nostartfiles -Wl,--gc-sections \
          -Wl,-Map=p4_t3b.map

all: p4_t3b.out p4_t3b.bin

startup.o: ../startup_p4_t3.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

p4_t3b.out: startup.o main.o ../msp432p401r.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.o main.o -lgcc

p4_t3b.bin: p4_t3b.out
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.out *.bin *.map

.PHONY: all clean
