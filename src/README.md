# Modelo SystemC del STM32F407VG

Estructura generada según `doc/smt32f407vg_diseño.md` (plan aprobado, propuestas
P1-P8 aplicadas; bus TLM-2.0 LT preparado para AT). Referencias en comentarios:
[IR, §x] = `doc/informe_revisado.md`; [II] = `doc/informe_instrucciones.md`.

## Estado por fases

| Fase | Contenido | Estado |
| :--- | :--- | :--- |
| **F0** | Esqueleto completo: 62 módulos instanciados y conectados | completada |
| **F1** | Infraestructura: relojes, resets, matriz LT, Flash/SRAM, cargador | **completada** |
| **F2** | CPU: ISA completa + excepciones + NVIC/SysTick | **completada** |
| **F3** | Pads/pin_mux/GPIO y RCC eléctrico completos | **completada** |
| F4 | DMA1/2, USART, TIM avanzados, EXTI/SYSCFG | **DMA, UART/USART y TIM completados**; EXTI/SYSCFG pendiente |
| F5 | Resto de periféricos | pendiente |
| F6 | Debug (DAP/FPB/DWT/ITM), DBGMCU, trazas | pendiente |
| F7 | Bajo consumo, OTG/ETH/FSMC/DCMI, afinado AT | pendiente |

`make test` compila y ejecuta la suite de verificación acumulada (475
comprobaciones autocomprobables: 124 de F1 + 12 de F2 + 80 de F3 + 71 del DMA,
70 de UART/USART y 118 de los temporizadores de F4; código de salida 0 si todas
pasan, en menos de 3 s). Verificado con SystemC 2.3.4 / g++ 13 / C++17 y
arm-none-eabi-gcc 13.2.

Las pruebas T15-T17, T25, T31, T37 y T43 necesitan los firmwares del repositorio; se
compilan con `make -C verif/fw`, `make -C verif/fw/coremark`,
`make -C verif/fw/blinky`, `make -C verif/fw/dma_demo`,
`make -C verif/fw/uart_demo` y `make -C verif/fw/tim_demo`
(requieren `arm-none-eabi-gcc`). Sin ellos, esas pruebas informan de que falta
la imagen. `F2_SKIP_COREMARK=1` omite la ejecución de CoreMark.

```
make -f Makefile.stm32 test           # o: cp Makefile.stm32 Makefile && make test
make -f Makefile.stm32 run IMG=fw.bin # carga una imagen y simula
```

## Estructura

| Carpeta | Contenido |
| :--- | :--- |
| `common/` | Tipos de bus y extensión AHB, mapa de memoria, sectores de Flash y tabla de estados de espera (`ahb_types.h`); nodo analógico de pin (`analog_net.h`); generador de reloj reprogramable (`clock_gen.h`); clase base de esclavo con byte enables y respuestas AHB (`periph_base.h`) |
| `pins/` | `pad.h` (frontera V/I float <-> digital, Schmitt con histéresis, open-drain, pulls, rango y corriente), `pin_mux.h` (pads + mux AF + ruta analógica), `af_types.h` (tipos del mux), `power_pads.h` (VDD/NRST/BOOT0, POR/PDR/BOR) |
| `bus/` | `ahb_matrix.h` (8x7 con máscara de conectividad y arbitraje), `ahb_decoder.h` (decodificadores de segmento + puente AHB-APB), `bitband.h` (alias de bit-banding) |
| `mem/` | `flash_if.h` (Flash 1 MB + ART + registros FLASH + option bytes + cargador), `sram.h` (SRAM1/2, BKPSRAM, CCM) |
| `rcc/` | `rcc.h` (banco de registros, árbol de reloj, gating, controlador de reset), `osc_pll.h` (HSI/HSE/LSI/LSE, PLL, PLLI2S) |
| `core/` | `cortex_m4f.h` (router I/D/S/CCM/PPB con alias de 0x0 y bit-banding), `cpu.h` (bucle fetch/decode/execute, excepciones, prebúsqueda), `cpu_state.h` (RegFile y utilidades arquitectónicas), `cpu_exec16.h` / `cpu_exec32.h` (ISA completa [II]), `fpu.h` (FPv4-SP), `scs.h` (SCB + NVIC + SysTick + MPU), `debug.h` (DAP/CoreSight/DBGMCU + transactor AHB-AP) |
| `periph/` | un fichero por familia de periférico, todos derivados de `BusSlave`. `usart.h` es un único modelo parametrizado del que salen los tipos `Usart` y `Uart` (véase `doc/stm32f407vg_fase4_uart.md`), y `timers.h` uno del que salen los seis tipos de temporizador del F407 (véase `doc/stm32f407vg_fase4_tim.md`) |
| `verif/` | `bus_test_master.h` (maestro de bus de verificación), `image_loader.h` (carga de .bin/.hex y tabla de vectores), `decoder_vectors.h` (254 vectores generados desde `doc/valida_instrucciones.py` por `gen_decoder_vectors.py`), `ext_parts.h` (circuitería externa de placa: cristal, reloj, LED, pulsador, resistencia, driver, pista entre pines), `fw/` (firmware de autocomprobación, *port* bare-metal de CoreMark, CMSIS oficial, blinky de referencia y demostraciones del DMA, de los puertos serie y de los temporizadores) |
| `top/` | `stm32f407vg.h` + `stm32f407vg_bind2.h` (netlist/contrato de integración), `sc_main.cpp` (suite de verificación F1+F2) |

## Convenciones

* **Pines:** el exterior del MCU son los `AnalogNet` (uno por pin). Cualquier
  circuito externo se registra como driver Thevenin `{V, Rout}`; el canal
  resuelve tensión y corriente en `float`. La digitalización (0/1/X) ocurre
  solo en `Pad` (Schmitt VIH/VIL con histéresis y detección de fuera de rango).
* **Relojes:** ondas cuadradas `bool` generadas por `ClockGen` (frecuencia
  reprogramable en caliente) + señal `double` con la frecuencia para anotación.
* **Bus:** TLM-2.0 `b_transport` con extensión `AhbExt` (maestro, HPROT,
  ráfaga, exclusivos). La elevación a AT (arbitraje por ciclo) está prevista
  en `AhbMatrix` sin cambiar la topología.
* **Escritura de puertos:** cuando el estado de un módulo lo actualizan tanto
  sus procesos internos como el `b_transport` de su banco de registros (que
  corre en el proceso del maestro), un único proceso `publish_proc` escribe los
  puertos de salida. SystemC no admite dos escritores sobre un `sc_signal`.
* **IRQ/DMA:** vectores de señales con la numeración exacta de los informes
  (IRQ0-81; celdas DMA `stream*8+canal` según RM0090 tablas 42/43).
* **Núcleo:** el estado arquitectónico vive en una `struct` C++ (`RegFile`) y
  el SCS se ofrece a la CPU por una interfaz C++ (`core_sys_if`), no por TLM:
  el núcleo lo consulta varias veces por instrucción. El acceso del *software*
  a esos mismos registros sí pasa por el bus PPB.
* **Velocidad de simulación:** `ClockGen::set_waveform(false)` (y
  `Rcc::set_internal_waveforms(false)`, que incluye osciladores, PLLs y MCO)
  mantiene la frecuencia publicada pero deja de conmutar la señal; es lo que
  permite ejecutar firmware largo. Los consumidores dirigidos por eventos
  (SysTick, muestreo del IDR) usan `freq_hz`, no los flancos.
* **Gating de reloj:** el bit de `RCC_xxxENR` habilita la puerta de forma
  combinacional (`BusSlave::clk_en_live` apunta al estado interno del RCC). El
  puerto `clk_en` sigue existiendo para los procesos internos del periférico.
* **Familias de periférico parametrizadas:** cuando varias instancias salen del
  mismo bloque de diseño con recursos distintos (USART/UART, los seis tipos de
  TIM), el modelo es único: una `struct` de rasgos `constexpr` describe la
  variante, una clase base la recibe por el constructor (selección en tiempo de
  ejecución) y una plantilla `X<const Caps&>` la fija en el tipo (selección en
  tiempo de compilación). Los rasgos gobiernan las máscaras de escritura de los
  registros, de modo que lo que la variante no tiene lee cero como en el silicio.
* **Contadores dirigidos por sucesos:** los temporizadores no se evalúan flanco
  a flanco de TIMCLK; saltan al siguiente suceso (comparación o desbordamiento)
  calculando el tiempo a partir de la frecuencia, e interpolan `CNT` en las
  lecturas intermedias. El coste de simulación depende del número de sucesos y
  no de la frecuencia del reloj.
* **Frontera del MCU:** el modelo termina en los `AnalogNet` de los pines.
  Cualquier componente de placa (cristal, LED, pulsador) es del banco de
  pruebas y vive en `verif/ext_parts.h`.
