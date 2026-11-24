# `fuentes/st/`: STM32 Open Pin Data

Copia de los ficheros de **STM32 Open Pin Data** que usa
`biblioteca/herramientas/fuente_st.py` (fase B3 de `doc/analisis_build.md`). Es
la base de datos de patillas de STM32CubeMX, con licencia BSD-3-Clause
(`LICENSE`, la misma que la de `soc/stm32f3/pindata/`).

* **Repositorio**: `github.com/STMicroelectronics/STM32_open_pin_data`.
* **Commit**: `45c04599e2e0a788c5ed9b6218dc5a28a36041f7`.
* **Ficheros**:
  * `mcu/STM32F405*`, `STM32F407*`, `STM32F415*`, `STM32F417*` y
    `STM32F446*`, solo los encapsulados que modela mcu-sim. Faltan los BGA
    (`Hx`, `Jx`);
  * `mcu/IP/GPIO-STM32F417_gpio_v1_0_Modes.xml` y
    `GPIO-STM32F446_gpio_v1_0_Modes.xml`, con el número de AF de cada señal.

Los del F3 están en `soc/stm32f3/pindata/`, de donde ya salía la tabla AF del
modelo del F3 (`gen_pines.py`).

**No se editan.** Para pasar a otro commit hay que copiar los mismos ficheros
de ese commit, cambiar `FUENTE` en `fuente_st.py` y ejecutar
`make biblioteca-mcus`. El contraste con el modelo dirá si algo cambió.
