# =============================================================================
# Makefile — freertos_blinky: la demo de FreeRTOS para la LaunchPad MSP432,
# SIN MODIFICAR, contra la ROM sustituta (fase T4)
#
# El cierre de T4 pide «un ejemplo con FreeRTOS o TI-RTOS arranca y conmuta
# tareas». Es la demo que mantiene FreeRTOS para esta placa
# (FreeRTOS/Demo/CORTEX_M4F_MSP432_LaunchPad_IAR_CCS_Keil, MIT): en su modo
# «blinky» -configCREATE_SIMPLE_TICKLESS_DEMO = 1, el de su FreeRTOSConfig.h-
# una tarea manda un valor por una cola cada segundo, otra lo recibe y
# enciende P1.0 50 ms, y entre medias el nucleo duerme con el modo tickless.
# Usa DriverLib por MAP_ (main.c y FreeRTOSConfig.h), asi que pasa por la ROM.
#
# Lo de la demo esta en demo/, tal cual; el nucleo de FreeRTOS, en kernel/
# (FreeRTOS-Kernel V11.3.1, la revision a la que apunta el repositorio de la
# demo, MIT), solo lo que hace falta; el puerto es el de GCC para Cortex-M4F.
# La demo trae arranque para IAR, CCS y Keil, no para GCC: se usa el del SDK de
# TI, el mismo de ../sdk_ejemplos. Vease LEEME.md.
#
#   make            # freertos_blinky.out y .bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
OBJCOPY:= $(CROSS)objcopy

SDK    := ../sdk
DL     := $(SDK)/ti/devices/msp432p4xx/driverlib
COMUN  := ../sdk_ejemplos/comun
LIBDL  := ../sdk_ejemplos/obj/libdriverlib.a
K      := kernel

CFLAGS := -I demo -I $(K)/include -I $(K)/portable/GCC/ARM_CM4F -I $(DL) \
          -I$(SDK) -I../../cmsis/Include -D__MSP432P401R__ -DDeviceFamily_MSP432P401x \
          -mcpu=cortex-m4 -march=armv7e-m -mthumb -std=c99 -mfloat-abi=hard \
          -mfpu=fpv4-sp-d16 -ffunction-sections -fdata-sections -g -gstrict-dwarf \
          -O1 -Wall -Wno-unused-variable -ffile-prefix-map=$(CURDIR)=.
LFLAGS := -Wl,-T,$(COMUN)/gcc/msp432p401r.lds -march=armv7e-m -mthumb -mfloat-abi=hard \
          -mfpu=fpv4-sp-d16 -static -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
          -Wl,-Map,freertos_blinky.map -lgcc -lc -lm -lnosys --specs=nano.specs

FUENTES:= demo/main.c demo/SimplyBlinkyDemo/main_blinky.c \
          $(K)/tasks.c $(K)/queue.c $(K)/list.c $(K)/timers.c \
          $(K)/portable/GCC/ARM_CM4F/port.c $(K)/portable/MemMang/heap_4.c \
          $(COMUN)/system_msp432p401r.c $(COMUN)/gcc/startup_msp432p401r_gcc.c
OBJS   := $(addprefix obj/,$(notdir $(FUENTES:.c=.o)))
vpath %.c $(sort $(dir $(FUENTES)))

all: freertos_blinky.out freertos_blinky.bin

obj:
	mkdir -p obj
obj/%.o: %.c | obj
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIBDL):
	$(MAKE) -C ../sdk_ejemplos obj/libdriverlib.a

freertos_blinky.out: $(OBJS) $(LIBDL) $(COMUN)/gcc/msp432p401r.lds
	$(CC) $(OBJS) $(LIBDL) $(LFLAGS) -o $@

freertos_blinky.bin: freertos_blinky.out
	$(OBJCOPY) -O binary -j .intvecs -j .text -j .rodata -j .ARM.exidx -j .data $< $@

clean:
	rm -rf obj freertos_blinky.out freertos_blinky.bin freertos_blinky.map

.PHONY: all clean
