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
#
# Y, si hacen falta (S3: los ejemplos de dos placas):
#
#   OBJ      la carpeta de los objetos (obj por omision), para compilar varios
#            ejemplos en la misma carpeta
#   DEFS     definiciones de mas (-DMASTER_BOARD): los interruptores que el
#            readme del ejemplo dice que se pongan al compilar
#   SIN_DEF  una macro que main.c DEFINE y que el readme dice que se quite
#            para la otra placa (TRANSMITTER_BOARD). Se compila una COPIA de
#            main.c sin esa linea, en $(OBJ); el fuente no se toca
# =============================================================================
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SDK     := ../sdk
CPU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OBJ     ?= obj
DEFS    ?=
SIN_DEF ?=
CFLAGS  := $(CPU) -Os -g -std=gnu11 -Wall -DSTM32F303xC -DUSE_HAL_DRIVER \
           -ffunction-sections -fdata-sections -ffile-prefix-map=$(CURDIR)=. \
           $(DEFS) -I$(EJ)/Inc -I$(SDK)/hal/Inc -I$(SDK)/cmsis_device_f3/Include \
           -I../../cmsis/Include -I$(SDK)/bsp
LDFLAGS := $(CPU) --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
           -T$(EJ)/SW4STM32/STM32F303VCTx_FLASH.ld -Wl,-Map=$(NOMBRE).map \
           -Wl,--no-warn-rwx-segments
FUENTES := $(wildcard $(EJ)/Src/*.c)
ifneq ($(SIN_DEF),)
FUENTES := $(filter-out %/main.c,$(FUENTES)) $(OBJ)/main.c
endif
SRCS    := $(FUENTES) $(SDK)/bsp/stm32f3_discovery.c \
           $(filter-out %_template.c,$(wildcard $(SDK)/hal/Src/stm32f3xx_hal*.c))
OBJS    := $(addprefix $(OBJ)/,$(notdir $(SRCS:.c=.o))) $(OBJ)/startup.o

all: $(NOMBRE).elf $(NOMBRE).bin

vpath %.c $(sort $(dir $(SRCS)))

$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

ifneq ($(SIN_DEF),)
$(OBJ)/main.c: $(EJ)/Src/main.c
	@mkdir -p $(OBJ)
	sed '/^#define $(SIN_DEF)\b/d' $< > $@

$(OBJ)/main.o: $(OBJ)/main.c
	$(CC) $(CFLAGS) -c -o $@ $<
endif

$(OBJ)/startup.o: $(EJ)/SW4STM32/startup_stm32f303xc.s
	@mkdir -p $(OBJ)
	$(CC) $(CPU) -x assembler-with-cpp -c -o $@ $<

$(NOMBRE).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(NOMBRE).bin: $(NOMBRE).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(OBJ) *.elf *.bin *.map

.PHONY: all clean
