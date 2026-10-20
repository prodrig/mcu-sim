# =============================================================================
# Los ejemplos de STM32CubeF3 de la fase S3, SIN MODIFICAR [doc/analisis_stm32f303.md
# §7.2], todos en esta carpeta, cada uno con sus objetos en obj/<nombre>.
#
# Los de dos placas se compilan dos veces, con el interruptor que dice su
# readme: los SPI y los I2C con -DMASTER_BOARD para el maestro (su main.c lo
# trae comentado), y los UART sin la linea `#define TRANSMITTER_BOARD` para el
# receptor (su main.c la trae puesta: se compila una copia sin ella, en obj/;
# el fuente no se toca).
#
# PWR_CurrentConsumption elige su modo con una macro de stm32f3xx_lp_modes.h
# (SLEEP_MODE si no se da otra): el .bin sin sufijo es ese, el de por defecto;
# los otros tres se compilan con -D<MODO> y llevan el modo en el nombre.
#
#   make            # todos los .bin
# =============================================================================
SDKE := ../sdk/ejemplos

UNA  := TIM_PWMOutput TIM_DMA TIM_TimeBase TIM_Asymetric TIM_InputCapture \
        TIM_Combined TIM_ComplementarySignals TIM_PWMInput \
        IWDG_Reset IWDG_WindowMode WWDG_Example RTC_Calendar \
        HAL_TimeBase_RTC_ALARM HAL_TimeBase_RTC_WKUP HAL_TimeBase_TIM \
        PWR_STOP PWR_STANDBY PWR_PVD PWR_CurrentConsumption
UART := UART_TwoBoards_ComPolling UART_TwoBoards_ComIT UART_TwoBoards_ComDMA
MS   := SPI_FullDuplex_ComPolling SPI_FullDuplex_ComIT SPI_FullDuplex_ComDMA \
        I2C_TwoBoards_ComPolling I2C_TwoBoards_ComIT I2C_TwoBoards_ComDMA \
        I2C_TwoBoards_AdvComIT

BINS := $(addsuffix .bin,$(UNA)) \
        $(addsuffix _tx.bin,$(UART)) $(addsuffix _rx.bin,$(UART)) \
        $(addsuffix _maestro.bin,$(MS)) $(addsuffix _esclavo.bin,$(MS)) \
        PWR_CurrentConsumption_STOP_RTC_MODE.bin PWR_CurrentConsumption_STANDBY_MODE.bin \
        PWR_CurrentConsumption_STANDBY_RTC_MODE.bin

ST = $(MAKE) --no-print-directory -f ../ejemplo_st.mk

all: $(BINS)

%.bin: FORCE
	+@n=$*; \
	case $$n in \
	  *_tx)      e=$${n%_tx};      $(ST) NOMBRE=$$n EJ=$(SDKE)/$$e OBJ=obj/$$n ;; \
	  *_rx)      e=$${n%_rx};      $(ST) NOMBRE=$$n EJ=$(SDKE)/$$e OBJ=obj/$$n SIN_DEF=TRANSMITTER_BOARD ;; \
	  *_maestro) e=$${n%_maestro}; $(ST) NOMBRE=$$n EJ=$(SDKE)/$$e OBJ=obj/$$n DEFS=-DMASTER_BOARD ;; \
	  *_esclavo) e=$${n%_esclavo}; $(ST) NOMBRE=$$n EJ=$(SDKE)/$$e OBJ=obj/$$n ;; \
	  PWR_CurrentConsumption_*) m=$${n#PWR_CurrentConsumption_}; \
	             $(ST) NOMBRE=$$n EJ=$(SDKE)/PWR_CurrentConsumption OBJ=obj/$$n DEFS=-D$$m ;; \
	  *)         $(ST) NOMBRE=$$n EJ=$(SDKE)/$$n OBJ=obj/$$n ;; \
	esac

clean:
	rm -rf obj *.elf *.bin *.map

FORCE:
.PHONY: all clean FORCE
