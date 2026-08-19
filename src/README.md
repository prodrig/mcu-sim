# Modelo SystemC del STM32F407VG

Estructura generada según `doc/smt32f407vg_diseño.md` (plan aprobado, propuestas
P1-P8 aplicadas; bus TLM-2.0 LT preparado para AT). Referencias en comentarios:
[IR, §x] = `doc/informe_revisado.md`; [II] = `doc/informe_instrucciones.md`.

## Estado por fases

| Fase | Contenido | Estado |
| :--- | :--- | :--- |
| **F0** | Esqueleto completo: 62 módulos instanciados y conectados | completada |
| **F1** | Infraestructura: relojes, resets, matriz LT, Flash/SRAM, cargador | **completada** |
| F2 | CPU: ISA completa + excepciones + NVIC/SysTick | pendiente |
| F3 | Pads/pin_mux/GPIO y RCC eléctrico completos | pendiente |
| F4 | DMA1/2, USART, TIM avanzados, EXTI/SYSCFG | pendiente |
| F5 | Resto de periféricos | pendiente |
| F6 | Debug (DAP/FPB/DWT/ITM), DBGMCU, trazas | pendiente |
| F7 | Bajo consumo, OTG/ETH/FSMC/DCMI, afinado AT | pendiente |

`make test` compila y ejecuta la suite de verificación de F1 (124
comprobaciones autocomprobables; código de salida 0 si todas pasan).
Verificado con SystemC 2.3.4 / g++ 13 / C++17.

```
make -f Makefile.stm32 test           # o: cp Makefile.stm32 Makefile && make test
make -f Makefile.stm32 run IMG=fw.bin # carga una imagen y simula
```

## Estructura

| Carpeta | Contenido |
| :--- | :--- |
| `common/` | Tipos de bus y extensión AHB, mapa de memoria, sectores de Flash y tabla de estados de espera (`ahb_types.h`); nodo analógico de pin (`analog_net.h`); generador de reloj reprogramable (`clock_gen.h`); clase base de esclavo con byte enables y respuestas AHB (`periph_base.h`) |
| `pins/` | `pad.h` (frontera V/I float <-> digital, Schmitt), `pin_mux.h` (pads + mux AF + ruta analógica), `power_pads.h` (VDD/NRST/BOOT0) |
| `bus/` | `ahb_matrix.h` (8x7 con máscara de conectividad y arbitraje), `ahb_decoder.h` (decodificadores de segmento + puente AHB-APB), `bitband.h` (alias de bit-banding) |
| `mem/` | `flash_if.h` (Flash 1 MB + ART + registros FLASH + option bytes + cargador), `sram.h` (SRAM1/2, BKPSRAM, CCM) |
| `rcc/` | `rcc.h` (banco de registros, árbol de reloj, gating, controlador de reset), `osc_pll.h` (HSI/HSE/LSI/LSE, PLL, PLLI2S) |
| `core/` | `cortex_m4f.h` (router I/D/S/CCM/PPB con alias de 0x0 y bit-banding), `cpu.h` (RegFile + ISA [II]), `fpu.h`, `scs.h`, `debug.h` (DAP/CoreSight/DBGMCU + transactor AHB-AP) |
| `periph/` | un fichero por familia de periférico, todos derivados de `BusSlave` |
| `verif/` | `bus_test_master.h` (maestro de bus de verificación), `image_loader.h` (carga de .bin/.hex y tabla de vectores) |
| `top/` | `stm32f407vg.h` + `stm32f407vg_bind2.h` (netlist/contrato de integración), `sc_main.cpp` (suite de verificación F1) |

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
