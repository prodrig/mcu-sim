# STM32CubeF3, lo justo (S0, S3 y S4 del F303)

Lo que necesitan los firmwares de `verif/fw/stm32f3/`, copiado **sin tocar**
de los repositorios de ST en GitHub. Se versiona -como el SDK de TI de los
MSP432- para que los `.bin` de la suite se puedan rehacer en cualquier sitio
con `make fwf3` y den la misma huella (`verif/fw/huellas.txt`).

| Carpeta | De dónde | Versión | Licencia |
| :--- | :--- | :--- | :--- |
| `cmsis_device_f3/` | `STMicroelectronics/cmsis_device_f3`: `Include/stm32f303xc.h`, `stm32f3xx.h`, `system_stm32f3xx.h` y `Source/Templates/system_stm32f3xx.c` | v2.3.9 | Apache-2.0 (`LICENSE.md`) |
| `hal/` | `STMicroelectronics/stm32f3xx_hal_driver`: `Inc/` y `Src/` | v1.5.9 | BSD-3 (`LICENSE.md`) |
| `bsp/` | STM32CubeF3, `Drivers/BSP/STM32F3-Discovery` y, de `Drivers/BSP/Components`, los controladores de los cuatro sensores de las dos revisiones de la placa (`l3gd20`, `i3g4250d`, `lsm303dlhc`, `lsm303agr`) | v2.1.7 | BSD-3 (`LICENSE.md`) |
| `bsp/Components/Common/` | (S4) `gyro.h` y `accelero.h`, de `STMicroelectronics/stm32-bsp-common` (el submódulo `Drivers/BSP/Components/Common` de STM32CubeF3 v1.12.0, commit `1e18c5a`): las interfaces que los cuatro controladores incluyen | v4.0.1 | BSD-3 (en su cabecera) |
| `tinyusb/` | (S6) `hathach/tinyusb`, la etiqueta `0.18.0` (commit `86ad6e56`): de `src/`, `tusb.c`, `tusb.h`, `tusb_option.h`, `common/`, `device/`, `osal/`, `class/cdc/` (`cdc.h`, `cdc_device.*`), `class/hid/` (`hid.h`, `hid_device.*`) y `portable/st/stm32_fsdev/`, y su `LICENSE`. Lo usa `f3_usb`, el CDC + HID de S6 | 0.18.0 | MIT (`LICENSE`) |
| `ejemplos/` | STM32CubeF3 v1.12.0, `Projects/STM32F3-Discovery/Examples/`: `GPIO/GPIO_IOToggle`, `GPIO/GPIO_EXTI` y `FLASH/FLASH_EraseProgram` (S1 y S2), y desde S3 otros 29: `UART/UART_TwoBoards_Com{Polling,IT,DMA}`, `SPI/SPI_FullDuplex_Com{Polling,IT,DMA}`, `I2C/I2C_TwoBoards_Com{Polling,IT,DMA}` e `I2C_TwoBoards_AdvComIT`, `TIM/TIM_{PWMOutput,DMA,TimeBase,Asymetric,InputCapture,Combined,ComplementarySignals,PWMInput}`, `IWDG/IWDG_{Reset,WindowMode}`, `WWDG/WWDG_Example`, `RTC/RTC_Calendar`, `HAL/HAL_TimeBase_{RTC_ALARM,RTC_WKUP,TIM}` y `PWR/PWR_{STOP,STANDBY,PVD,CurrentConsumption}`; y desde S4, `BSP`, el que lee el giróscopo y el acelerómetro con el BSP de la placa; cada uno con su `Inc/`, su `Src/`, su `readme.txt` y, de `SW4STM32/`, su arranque y su guion de enlazado | v1.12.0 (commit `928cd199`) | BSD-3 (`LICENSE.md`) |

## Cómo se compilan

`verif/fw/stm32f3/ejemplo_st.mk`: los fuentes del ejemplo, el BSP de la placa
y la HAL entera (los `*_template.c` no), con `-Os`, `--specs=nano.specs
--specs=nosys.specs`, y el arranque y el guion de enlazado del propio ejemplo
(los de SW4STM32). Cada ejemplo tiene su carpeta con un `Makefile` de tres
líneas (`gpio_iotoggle/`, `gpio_exti/`, `flash_eraseprogram/`).

Los de S3 se compilan todos juntos en `verif/fw/stm32f3/ejemplos_s3/`, con su
`Makefile`: un `.bin` por ejemplo, con los objetos en `obj/<nombre>`. Los de
dos placas, dos veces, con el interruptor que dice su `readme.txt`: los SPI y
los I²C con `-DMASTER_BOARD` para el maestro (`_maestro`; su `main.c` lo trae
comentado) y sin él para el esclavo (`_esclavo`); los UART, con
`TRANSMITTER_BOARD` para el que manda (`_tx`; su `main.c` lo trae puesto) y,
para el otro (`_rx`), con una copia en `obj/` del `main.c` sin esa línea
(`SIN_DEF=` de `ejemplo_st.mk`). `PWR_CurrentConsumption` elige su modo con
una macro de `stm32f3xx_lp_modes.h`: el `.bin` sin sufijo es el de fábrica
(`SLEEP_MODE`), y los otros tres llevan `-D<MODO>` y el modo en el nombre.
**Ningún fuente de ST se toca.** `FreeRTOS_ThreadCreation`, opcional en §7.2
del análisis, no está: haría falta versionar FreeRTOS para el F3.

El de S4, `BSP`, en `verif/fw/stm32f3/ejemplos_s4/`, con `MEMS=1` de
`ejemplo_st.mk`: compila además el BSP de los sensores y sus cuatro
controladores (sin `MEMS`, ningún otro `.bin` cambia). La **demostración** de
la placa (`Projects/STM32F3-Discovery/Demonstrations`) es **SLA0044** y usa el
middleware USB de ST, también SLA0044: no se versiona. `make -C
verif/fw/stm32f3/ejemplos_s4 demo STM32CUBEF3=/ruta` la compila desde una
copia de STM32CubeF3 con `Middlewares/ST/STM32_USB_Device_Library`, a
`ejemplos_s4/demo/`, que git ignora; `make f3-demo` la compila y la prueba.

Los firmwares propios (`f3_demo/`, `coremark/`, `f3_vcp/`, `f3_mems/`) usan solo las cabeceras
CMSIS, con su arranque y su guion de enlazado (`startup_f3.s`, generado por
`gen_startup.py`, y `stm32f303vc.ld`).

## Lo que NO está

La memoria de sistema de ST (su cargador AN3155) no se puede distribuir: el
modelo lleva uno propio con el mismo protocolo por USART1 y USART2
(`src/soc/stm32f3/cargador/`, fase S7 de `doc/analisis_stm32f303.md`).
