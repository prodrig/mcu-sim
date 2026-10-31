# =============================================================================
# Makefile — grblHAL SIN MODIFICAR para la Nucleo-F446RE con la CNC Shield V3
#            (fase F3 de doc/analisis_cnc.md, «F3: grblHAL»)
#
# grblHAL es GPL-3: ni sus fuentes ni su binario se versionan aqui. Este
# Makefile los baja de GitHub en un commit FIJADO -el del driver STM32F4xx; sus
# submodulos, en los que fija el propio driver- y construye grblhal.bin con el
# compilador cruzado, sin PlatformIO: lo mismo que el entorno
# [env:nucleo_f446re_protoneer] de su platformio.ini (BOARD_PROTONEER_3XX y
# NUCLEO_F446, sin my_machine.h).
#
#   make            # baja las fuentes (la primera vez) y genera grblhal.bin
#   make limpia     # borra lo compilado; `make borra` tambien las fuentes
#
# Lo prueba `make gui-cnc` (C7), y se salta si no esta grblhal.bin.
# =============================================================================
REPO   := https://github.com/grblHAL/STM32F4xx.git
COMMIT := a6318ac92f079f49fab91687530f832997396edc
# Los submodulos que hacen falta para compilar (los demas no se bajan)
SUBMODS:= grbl eeprom keypad laser motors trinamic odometer fans spindle plugins \
          embroidery bluetooth sdcard

R      := fuentes
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

DEF := -DSTM32F446xx -DUSE_HAL_DRIVER -DOVERRIDE_MY_MACHINE -DBOARD_PROTONEER_3XX= \
       -DNUCLEO_F446= -D_USE_IOCTL=1 -D_USE_WRITE=1 -D_VOLUMES=1
INC := -I$(R) -I$(R)/Inc -I$(R)/boards -I$(R)/Drivers/CMSIS/Include \
       -I$(R)/Drivers/CMSIS/Device/ST/STM32F4xx/Include -I$(R)/Drivers/STM32F4xx_HAL_Driver/Inc \
       -I$(R)/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I$(R)/FatFs -I$(R)/FatFs/STM \
       -I$(R)/Drivers/FATFS/Target -I$(R)/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
       -I$(R)/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I$(R)/USB_DEVICE/App \
       -I$(R)/USB_DEVICE/Target
CPU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS := $(CPU) -Os -std=gnu11 -ffunction-sections -fdata-sections -w $(DEF) $(INC)
DIRS := Src boards $(SUBMODS) Drivers/STM32F4xx_HAL_Driver/Src

all: grblhal.bin

$(R)/.bajado:
	rm -rf $(R)
	git init -q $(R)
	git -C $(R) remote add origin $(REPO)
	git -C $(R) fetch -q --depth 1 origin $(COMMIT)
	git -C $(R) checkout -q FETCH_HEAD
	git -C $(R) submodule update -q --init --depth 1 $(SUBMODS)
	touch $@

# Las fuentes se listan cuando ya estan bajadas: una segunda pasada de make
grblhal.bin: $(R)/.bajado
	$(MAKE) --no-print-directory compila

SRC = $(filter-out %_template.c,$(foreach d,$(DIRS),$(wildcard $(R)/$(d)/*.c)))
OBJ = $(patsubst $(R)/%.c,o/%.o,$(SRC)) o/startup.o

compila: $(OBJ)
	$(CC) $(CPU) -T$(R)/STM32F446RETX_FLASH.ld --specs=nano.specs --specs=nosys.specs \
	  -Wl,--gc-sections -Wl,-Map=grblhal.map -u _printf_float -o grblhal.elf $(OBJ) -lm
	$(OBJCOPY) -O binary grblhal.elf grblhal.bin
	$(CROSS)size grblhal.elf

o/%.o: $(R)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

o/startup.o: $(R)/StartupF446/startup_stm32f446retx.s
	@mkdir -p o
	$(CC) $(CPU) -c $< -o $@

limpia:
	rm -rf o grblhal.elf grblhal.bin grblhal.map

borra: limpia
	rm -rf $(R)

# Lo que llama `make cleanfw`: lo compilado, no las fuentes
clean: limpia

.PHONY: all compila limpia borra clean
