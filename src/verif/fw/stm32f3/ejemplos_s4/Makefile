# =============================================================================
# Los ejemplos de STM32CubeF3 de la fase S4, SIN MODIFICAR [doc/analisis_stm32f303.md
# §7.2]: hoy, Examples/BSP, el que lee el giroscopo y el acelerometro con el
# BSP de la placa (MEMS=1 de ejemplo_st.mk). Los objetos, en obj/<nombre>.
#
# La demostracion de ST (Demonstrations/) es SLA0044 y no se versiona: se
# compila desde una copia de STM32CubeF3 con `make demo STM32CUBEF3=...`
# (vease el objetivo f3-demo de Makefile.mcu-sim).
#
#   make            # BSP.bin
# =============================================================================
SDKE := ../sdk/ejemplos
ST    = $(MAKE) --no-print-directory -f ../ejemplo_st.mk

all: BSP.bin

BSP.bin: FORCE
	+@$(ST) NOMBRE=BSP EJ=$(SDKE)/BSP OBJ=obj/BSP MEMS=1

# LA DEMOSTRACION DE ST, desde una copia de STM32CubeF3 con su middleware USB
# (Middlewares/ST/STM32_USB_Device_Library, el nucleo y la clase HID). Deja
# demo/Demo.bin, que NO se versiona: es SLA0044 [doc/analisis_stm32f303.md §9,
# riesgo 3]. testf3 la prueba si esta (F58)
STM32CUBEF3 ?=
DEMO = $(STM32CUBEF3)/Projects/STM32F3-Discovery/Demonstrations
USBD = $(STM32CUBEF3)/Middlewares/ST/STM32_USB_Device_Library
demo:
	@test -n "$(STM32CUBEF3)" || { echo "  make demo STM32CUBEF3=/ruta/a/STM32CubeF3"; exit 1; }
	@test -f $(USBD)/Core/Src/usbd_core.c || { echo "  falta $(USBD) (el submodulo stm32_mw_usb_device)"; exit 1; }
	@mkdir -p demo
	+@$(ST) NOMBRE=demo/Demo EJ=$(DEMO) OBJ=obj/Demo MEMS=1 \
	    GUION=$(DEMO)/SW4STM32/STM32F3-Discovery_Demo/STM32F303VCTx_FLASH.ld \
	    MAS_SRC="../sdk/hal/Src/stm32f3xx_ll_usb.c $(USBD)/Core/Src/usbd_core.c $(USBD)/Core/Src/usbd_ctlreq.c $(USBD)/Core/Src/usbd_ioreq.c $(USBD)/Class/HID/Src/usbd_hid.c" \
	    MAS_INC="$(USBD)/Core/Inc $(USBD)/Class/HID/Inc"

clean:
	rm -rf obj *.elf *.bin *.map demo

FORCE:
.PHONY: all clean demo FORCE
