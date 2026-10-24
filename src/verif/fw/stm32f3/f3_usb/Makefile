# =============================================================================
# Makefile — f3_usb, el USB de usuario con TinyUSB (S6)
#
# TinyUSB 0.18.0 (../sdk/tinyusb, MIT), la cabecera de dispositivo de ST
# (../sdk/cmsis_device_f3, Apache-2.0) y el CMSIS-Core de los STM32F4
# (../../cmsis). Sin HAL. -ffile-prefix-map por la huella (T-22).
#
#   make            # genera f3_usb.elf y f3_usb.bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy
TUSB   := ../sdk/tinyusb/src

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -Os -g -ffreestanding -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
          -DSTM32F303xC -I. -I$(TUSB) -I../sdk/cmsis_device_f3/Include -I../../cmsis/Include \
          -ffunction-sections -fdata-sections \
          -ffile-prefix-map=$(CURDIR)=.
LDFLAGS:= -T ../stm32f303vc.ld -nostartfiles -Wl,--gc-sections --specs=nano.specs \
          --specs=nosys.specs -Wl,-Map=f3_usb.map -Wl,--no-warn-rwx-segments

SRC := main.c usb_descriptors.c $(TUSB)/tusb.c $(TUSB)/common/tusb_fifo.c \
       $(TUSB)/device/usbd.c $(TUSB)/device/usbd_control.c \
       $(TUSB)/class/cdc/cdc_device.c $(TUSB)/class/hid/hid_device.c \
       $(TUSB)/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
OBJ := startup.o $(patsubst %.c,obj/%.o,$(notdir $(SRC)))

all: f3_usb.elf f3_usb.bin

obj:
	mkdir -p obj
startup.o: ../startup_f3.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<
vpath %.c . $(TUSB) $(TUSB)/common $(TUSB)/device $(TUSB)/class/cdc $(TUSB)/class/hid \
      $(TUSB)/portable/st/stm32_fsdev
obj/%.o: %.c tusb_config.h | obj
	$(CC) $(CFLAGS) -c -o $@ $<

f3_usb.elf: $(OBJ) ../stm32f303vc.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

f3_usb.bin: f3_usb.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf obj *.o *.elf *.bin *.map

.PHONY: all clean
