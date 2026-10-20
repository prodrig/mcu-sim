# STM32CubeF3, lo justo (S0 del F303)

Lo que necesitan los firmwares de `verif/fw/stm32f3/`, copiado **sin tocar**
de los repositorios de ST en GitHub. Se versiona -como el SDK de TI de los
MSP432- para que los `.bin` de la suite se puedan rehacer en cualquier sitio
con `make fwf3` y den la misma huella (`verif/fw/huellas.txt`).

| Carpeta | De dónde | Versión | Licencia |
| :--- | :--- | :--- | :--- |
| `cmsis_device_f3/` | `STMicroelectronics/cmsis_device_f3`: `Include/stm32f303xc.h`, `stm32f3xx.h`, `system_stm32f3xx.h` y `Source/Templates/system_stm32f3xx.c` | v2.3.9 | Apache-2.0 (`LICENSE.md`) |
| `hal/` | `STMicroelectronics/stm32f3xx_hal_driver`: `Inc/` y `Src/` | v1.5.9 | BSD-3 (`LICENSE.md`) |
| `bsp/` | STM32CubeF3, `Drivers/BSP/STM32F3-Discovery` y, de `Drivers/BSP/Components`, los controladores de los cuatro sensores de las dos revisiones de la placa (`l3gd20`, `i3g4250d`, `lsm303dlhc`, `lsm303agr`) | v2.1.7 | BSD-3 (`LICENSE.md`) |
| `ejemplos/` | STM32CubeF3 v1.12.0, `Projects/STM32F3-Discovery/Examples/`: `GPIO/GPIO_IOToggle`, `GPIO/GPIO_EXTI` y `FLASH/FLASH_EraseProgram`, cada uno con su `Inc/`, su `Src/`, su `readme.txt` y, de `SW4STM32/`, su arranque y su guion de enlazado | v1.12.0 | BSD-3 (`LICENSE.md`) |

## Cómo se compilan

`verif/fw/stm32f3/ejemplo_st.mk`: los fuentes del ejemplo, el BSP de la placa
y la HAL entera (los `*_template.c` no), con `-Os`, `--specs=nano.specs
--specs=nosys.specs`, y el arranque y el guion de enlazado del propio ejemplo
(los de SW4STM32). Cada ejemplo tiene su carpeta con un `Makefile` de tres
líneas (`gpio_iotoggle/`, `gpio_exti/`, `flash_eraseprogram/`).

Los firmwares propios (`f3_demo/`, `coremark/`) usan solo las cabeceras
CMSIS, con su arranque y su guion de enlazado (`startup_f3.s`, generado por
`gen_startup.py`, y `stm32f303vc.ld`).

## Lo que NO está

La memoria de sistema de ST (su cargador AN3155) no se puede distribuir: el
modelo arranca ahí un stub propio (`src/soc/stm32f3/sysmem_s1.s`) y el
cargador es la fase S7, opcional, de `doc/analisis_stm32f303.md`.
