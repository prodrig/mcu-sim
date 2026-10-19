# =============================================================================
# Makefile — sdk_ejemplos: ejemplos de DriverLib del SDK de TI, SIN MODIFICAR,
# contra la ROM sustituta (fase T4)
#
# El cierre de T4 pide «un ejemplo del SDK sin modificar, que usa MAP_,
# funciona con R1». Aqui hay cuatro, copiados tal cual del SimpleLink MSP432P4
# SDK (examples/nortos/MSP_EXP432P401R/driverlib, BSD-3; vease LEEME.md):
#
#   systick_interrupt_gpio_blink       SysTick y GPIO: P1.0 cambia cada
#                                      1 500 000 ciclos de MCLK
#   cs_dco_frequency_tune              CS_setDCOFrequency(8,33 MHz), que calcula
#                                      DCOTUNE con las calibraciones del TLV
#   timer32_periodic_mode_led_toggle   MCLK = REFO a 128 kHz en AM_LF_VCORE0;
#                                      S1 enciende P1.0 un segundo con Timer32
#   uart_pc_echo_12mhz_brclk           eco por la UCA0 a 9600 con SMCLK = 12 MHz
#
# Cada uno con el arranque, el SystemInit y el guion de enlazado de GCC que
# trae el propio SDK (comun/: son el mismo fichero en los cuatro ejemplos), y
# con las opciones de su makefile de gcc. Lo que no es MAP_ -CS_setDCOCentered-
# Frequency, por ejemplo- sale de DriverLib compilada aqui de sus fuentes; lo
# que es MAP_ salta a la ROM por ROM_APITABLE.
#
#   make            # los cuatro .out y .bin
# =============================================================================
CROSS  ?= arm-none-eabi-
CC     := $(CROSS)gcc
AR     := $(CROSS)ar
OBJCOPY:= $(CROSS)objcopy

SDK    := ../sdk
DL     := $(SDK)/ti/devices/msp432p4xx/driverlib
MODULOS:= adc14 aes256 comp_e cpu crc32 cs dma flash fpu gpio i2c interrupt \
          mpu pcm pmap pss ref_a reset rtc_c spi sysctl systick timer32 \
          timer_a uart wdt_a

EJEMPLOS := systick_interrupt_gpio_blink cs_dco_frequency_tune \
            timer32_periodic_mode_led_toggle uart_pc_echo_12mhz_brclk

# Las del makefile de gcc de los ejemplos del SDK, con las rutas de aqui y
# -ffile-prefix-map para que la huella no dependa de donde se compila (T-22)
CFLAGS := -I$(SDK) -I../../cmsis/Include -D__MSP432P401R__ -DDeviceFamily_MSP432P401x \
          -mcpu=cortex-m4 -march=armv7e-m -mthumb -std=c99 -mfloat-abi=hard \
          -mfpu=fpv4-sp-d16 -ffunction-sections -fdata-sections -g -gstrict-dwarf -Wall \
          -ffile-prefix-map=$(CURDIR)=.
LFLAGS := -Wl,-T,comun/gcc/msp432p401r.lds -march=armv7e-m -mthumb -mfloat-abi=hard \
          -mfpu=fpv4-sp-d16 -static -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
          -lgcc -lc -lm -lnosys --specs=nano.specs
# DriverLib, como la libreria que enlaza el SDK; optimizada, como la suya
DLFLAGS:= $(CFLAGS) -Os -g0 -Wno-unused-parameter -Wno-maybe-uninitialized \
          -Wno-strict-aliasing -Wno-parentheses

OUTS := $(foreach e,$(EJEMPLOS),$(e)/$(e).out)
BINS := $(OUTS:.out=.bin)

all: $(OUTS) $(BINS)

obj/dl_%.o: $(DL)/%.c | obj
	$(CC) $(DLFLAGS) -c -o $@ $<
obj:
	mkdir -p obj

obj/libdriverlib.a: $(addprefix obj/dl_,$(addsuffix .o,$(MODULOS)))
	rm -f $@; $(AR) rcs $@ $^

obj/system.o: comun/system_msp432p401r.c | obj
	$(CC) $(CFLAGS) -c -o $@ $<
obj/startup.o: comun/gcc/startup_msp432p401r_gcc.c | obj
	$(CC) $(CFLAGS) -c -o $@ $<

define EJEMPLO
obj/$(1).o: $(1)/$(1).c | obj
	$$(CC) $$(CFLAGS) -c -o $$@ $$<
$(1)/$(1).out: obj/system.o obj/startup.o obj/$(1).o obj/libdriverlib.a comun/gcc/msp432p401r.lds
	$$(CC) obj/system.o obj/startup.o obj/$(1).o obj/libdriverlib.a $$(LFLAGS) \
	    -Wl,-Map,$(1)/$(1).map -o $$@
$(1)/$(1).bin: $(1)/$(1).out
	$$(OBJCOPY) -O binary -j .intvecs -j .text -j .rodata -j .ARM.exidx -j .data $$< $$@
endef
$(foreach e,$(EJEMPLOS),$(eval $(call EJEMPLO,$(e))))

clean:
	rm -rf obj $(OUTS) $(BINS) $(OUTS:.out=.map)

.PHONY: all clean
