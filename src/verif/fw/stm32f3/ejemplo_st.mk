# =============================================================================
# ejemplo_st.mk — Como se compila un ejemplo de STM32CubeF3 SIN MODIFICARLO
#
# Lo que cada Makefile de ejemplo dice antes de incluir esto:
#
#   NOMBRE   el ejecutable (GPIO_IOToggle -> GPIO_IOToggle.elf y .bin)
#   EJ       la carpeta del ejemplo, dentro de sdk/ejemplos
#
# Se usan SUS fuentes (Src/), SU configuracion de la HAL (Inc/), SU arranque y
# SU guion de enlace (los de SW4STM32), la HAL entera -lo que la configuracion
# no activa compila a nada- y el BSP de la placa. Las opciones son las de
# SW4STM32, salvo -Os (por tamano) y -ffile-prefix-map (por la huella, T-22).
# Con newlib-nano, como CubeIDE.
#
#   make            # genera $(NOMBRE).elf y $(NOMBRE).bin
# =============================================================================
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SDK     := ../sdk
CPU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  := $(CPU) -Os -g -std=gnu11 -Wall -DSTM32F303xC -DUSE_HAL_DRIVER \
           -ffunction-sections -fdata-sections -ffile-prefix-map=$(CURDIR)=. \
           -I$(EJ)/Inc -I$(SDK)/hal/Inc -I$(SDK)/cmsis_device_f3/Include \
           -I../../cmsis/Include -I$(SDK)/bsp
LDFLAGS := $(CPU) --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
           -T$(EJ)/SW4STM32/STM32F303VCTx_FLASH.ld -Wl,-Map=$(NOMBRE).map \
           -Wl,--no-warn-rwx-segments
SRCS    := $(wildcard $(EJ)/Src/*.c) $(SDK)/bsp/stm32f3_discovery.c \
           $(filter-out %_template.c,$(wildcard $(SDK)/hal/Src/stm32f3xx_hal*.c))
OBJS    := $(addprefix obj/,$(notdir $(SRCS:.c=.o))) obj/startup.o

all: $(NOMBRE).elf $(NOMBRE).bin

vpath %.c $(sort $(dir $(SRCS)))

obj/%.o: %.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj/startup.o: $(EJ)/SW4STM32/startup_stm32f303xc.s
	@mkdir -p obj
	$(CC) $(CPU) -x assembler-with-cpp -c -o $@ $<

$(NOMBRE).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(NOMBRE).bin: $(NOMBRE).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf obj *.elf *.bin *.map

.PHONY: all clean
