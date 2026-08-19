# Plan de diseño — Modelo SystemC 2.3 del STM32F407VG

Documento de planificación de arquitectura. No contiene implementación: define la
jerarquía de módulos, las interconexiones y las convenciones, y registra las
propuestas de cambio sobre la jerarquía inicial (sección 2) que requieren
aprobación. Fuentes: `doc/informe_revisado.md` (referenciado como [IR, §x]) y
`doc/informe_instrucciones.md` [II].

---

## 1. Criterios de diseño

1. **Fidelidad eléctrica en la frontera:** los 100 pines del LQFP100 se modelan
   con tensión y corriente en `float` (voltios/amperios). Alta impedancia,
   open-drain, pull-up/down, niveles fuera de rango y contención de bus son
   representables. La conversión a digital (0/1/X) ocurre una sola vez, en el
   trigger Schmitt del pad [IR, §2.4]; a partir de ahí todo es `bool`.
2. **Relojes digitales:** osciladores y árbol de reloj generan ondas cuadradas
   `bool`. Frecuencias variables en tiempo de simulación (los PLL y prescalers
   se reprograman), por lo que no se usa `sc_clock` sino generadores propios.
3. **Precisión temporal:** objetivo inicial *loosely-timed* con anotación de
   ciclos (arranca firmware real rápido); la matriz y los puentes están
   diseñados para poder elevarse a *approximately-timed* (arbitraje ciclo a
   ciclo, fases AHB [IR, §6.3]) sin cambiar la topología. (Decisión D2, §6).
4. **Un módulo = un bloque de hardware con interfaz propia.** Lo que en el
   silicio es estado interno (banco de registros de la CPU) es estado C++, no
   un `sc_module`.
5. SystemC 2.3.3+/TLM-2.0, C++11 mínimo (recomendado C++14). Sin dependencias
   externas adicionales.

---

## 2. Análisis de la jerarquía propuesta — propuestas de cambio

### P1. Correcciones de asignación de bus (errores respecto a RM0090 [IR, §6.5])

| En la propuesta | Corrección | Motivo |
| :--- | :--- | :--- |
| GPIOA-E bajo `apb1` | **AHB1**, colgando de la matriz | Los GPIO están en AHB1 (0x4002 0xxx); su velocidad de toggle depende de HCLK [IR, §3] |
| ADC bajo `apb1` | **APB2** (ADC1/2/3 + bloque común) | 0x4001 2000 [IR, §12.13] |
| OTG_HS bajo `apb1` | **AHB1** con puerto maestro propio | 0x4004 0000, DMA interno = 8º maestro de la matriz [IR, §12.23] |
| CRC bajo `apb1` | **AHB1** | 0x4002 3000 |
| OTG_FS bajo `apb2` | **AHB2** | 0x5000 0000 [IR, §12.15] |
| RNG bajo `apb2` | **AHB2** | 0x5006 0800 |
| DCMI bajo `apb2` | **AHB2** | 0x5005 0000 [IR, §12.22] |
| USART2 duplicado en `apb2` | eliminar (queda solo en APB1) | errata de la lista |
| `eth_dma` y `usb_dma` como módulos top | absorber en `eth_mac` y `otg_hs` | el DMA es interno al periférico; hacia fuera solo se ve su puerto maestro AHB [IR, §12.16/12.23] |
| `bxCAN` único | **CAN1 + CAN2** (2 instancias; CAN1 posee el banco de filtros compartido) | [IR, §12.12] |

### P2. Bloques ausentes que hay que añadir

*   **SYSCFG** (APB2): imprescindible — remapeo de 0x0 (boot) y encaminamiento
    GPIO→EXTI [IR, §12.21].
*   **EXTI** (APB2): en la propuesta está dentro del SCB; es un periférico de
    ST, no parte del núcleo. Sus 23 líneas alimentan al NVIC y al wakeup [IR, §9.4].
*   **RTC** (APB1, dominio backup): en la propuesta está dentro de RCC; RCC solo
    contiene el **mux de RTCCLK** [IR, §12.9].
*   **SDIO** (APB2), **FSMC** (AHB3, con sus pines externos), **ETH_MAC** (AHB1).
*   **flash_if**: controlador de Flash (registros FLASH_*, llaves, option bytes)
    que además contiene el **ART** (ver P3).
*   **GPIOF..GPIOI**: existen en el mapa de registros aunque el LQFP100 solo
    saque PH0/PH1 de esos puertos [IR, §3]; los modelamos para fidelidad del
    decodificador (accesos válidos, sin pad físico).

### P3. Reestructuración del núcleo Cortex_M4F

*   **ART fuera del núcleo** → dentro de `flash_if`. En el silicio el ART es
    parte de la interfaz Flash de ST (lo controla FLASH_ACR [IR, §5.2.3]), no
    del macro Cortex-M4.
*   **registers_Rx / xPSR / PRIMASK / FAULTMASK / BASEPRI / CONTROL como
    submódulos** → sustituir por una `struct RegFile` (dato miembro de la CPU).
    No son bloques con interfaz de bus; como módulos añadirían overhead de
    simulación y complejidad sin ganancia de fidelidad.
*   **DAP**: reorganizar como `SWJ_DP` (JTAG+SWD conmutables por secuencia) +
    `AHB_AP` **dentro** del DAP (en la propuesta AHB_AP quedaba suelto) [IR, §13.2].
*   **DBGMCU** se mantiene en el subsistema de debug (bloque ST en 0xE004 2000).
*   **CCM conectada directamente al D-bus de la CPU**, sin pasar por la matriz
    [IR, §5.3]: el router interno del núcleo decide D-bus→CCM o D-bus→matriz.
*   SCS = SCB + NVIC + SysTick + MPU (+ registros FPU-SCS) como en la propuesta,
    menos EXTI (ver P2).

### P4. RCC

*   **PH0/PH1 no son hijos de RCC**: son pads del puerto H con función
    analógica de oscilador. HSE se conecta a ellos a través de `pin_mux`
    (igual que LSE con PC14/PC15, que faltaban).
*   Añadir submódulos: `clock_tree` (prescalers AHB/APB1/APB2, mux SW, MCO1/2),
    `reset_ctrl` (POR/PDR/BOR, NRST, WWDG/IWDG/SW/low-power [IR, §4.1]) y
    salidas PLL48CK, RTCCLK, relojes por-periférico con gating (ENR/LPENR).
*   CSS → línea NMI al NVIC.

### P5. Modelo eléctrico de pines (requisito del proyecto)

*   Canal **`AnalogNet`** (uno por pin físico): cada conectado publica un
    **equivalente Thevenin** `{V_drv (float), R_out (float)}`; alta impedancia =
    R_out = ∞. El canal resuelve en cada delta la tensión del nodo
    `V_pin = Σ(V_i/R_i)/Σ(1/R_i)` y la corriente por cada driver
    `I_i = (V_i - V_pin)/R_i` (floats). El testbench externo es un driver más.
*   **`Pad`** (uno por pin de E/S): lado analógico = driver del AnalogNet según
    MODER/OTYPER/OSPEEDR/PUPDR (push-pull ≈ Rout baja a VDD/VSS; open-drain =
    solo NMOS; pull-up/down 40 kΩ [IR, §2.4]; entrada/analógico = Hi-Z); lado
    digital = Schmitt trigger con VIH/VIL e histéresis → `0/1/X`, y detección
    de fuera de rango absoluto (V < -0.3 V o > VDD+0.3 V) con aviso y flag.
*   Pines de alimentación (**VDD, VSS, VDDA, VSSA, VREF+, VBAT, VCAP1/2**) como
    entradas float a `power_pads` (alimenta el modelo de POR/BOR/PVD del PWR).
*   **NRST**: pad open-drain bidireccional con pull-up interno (los resets
    internos también lo llevan a nivel bajo [IR, §4.1]). **BOOT0/BOOT1**:
    muestreo en el 4º flanco de SYSCLK tras reset [IR, §2.3].
*   ADC/DAC/osciladores acceden al **nodo analógico** del pad (float), no al
    Schmitt.

### P6. Relojes

*   `ClockGen`: generador de onda cuadrada `sc_signal<bool>` con semiperiodo
    reprogramable en caliente (los osciladores HSI/HSE/LSI/LSE y las salidas
    PLL/prescaler son instancias). Señal paralela `sc_signal<double> freq_hz`
    para observabilidad y para módulos que computen tiempos sin contar flancos.
*   Distribución: HCLK, FCLK, PCLK1, PCLK2, TIMCLK_APB1 (=2×PCLK1 si PPRE1>1),
    TIMCLK_APB2, PLL48CK, RTCCLK, LSI→IWDG, HCLK/8→SysTick ext [IR, §4.3/4.4].

### P7. Interconexión TLM (ver decisión D2)

*   `ahb_matrix`: 8 puertos target (I-bus, D-bus, S-bus, DMA1_M, DMA2_M,
    DMA2_P, ETH_M, OTG_HS_M) × 7 puertos initiator (Flash-I vía ART, Flash-D,
    SRAM1, SRAM2, AHB1, AHB2, FSMC), con la **máscara de conectividad** de
    [IR, §6.2] y arbitraje round-robin por esclavo.
*   `ahb1_decoder` y `ahb2_decoder`: decodificadores de segmento que reparten a
    los periféricos AHB y a los dos puentes `ahb_apb_bridge` (APB1 42 MHz,
    APB2 84 MHz, con estados de espera de sincronización [IR, §6.4]).
*   Extensión TLM propia `AhbExt` (id de maestro, HPROT, HBURST, exclusivo)
    para fidelidad del protocolo y para los monitores exclusivos LDREX/STREX.
*   Accesos a región reservada → respuesta de error (BusFault) [IR, §6.5-nota].

### P8. Señalización IRQ / DMA / eventos

*   `sc_vector<sc_signal<bool>> irq[82]` → NVIC (numeración exacta [IR, §9.1.2]).
*   EXTI: 16 líneas desde pin_mux/GPIO (seleccionadas por SYSCFG_EXTICR) + 7
    internas (PVD, RTC×3, USB×2, ETH) [IR, §9.4.1]; salidas irq + wakeup→PWR.
*   DMA: pares req/ack por (stream, canal) según tablas [IR, §11.4]; el mux de
    canal está dentro de cada DMA (CHSEL).
*   Eventos del núcleo: `sleeping`, `sleepdeep` (→PWR), `event_in/out`
    (WFE/SEV), `systick_calib`.

---

## 3. Jerarquía revisada (propuesta a aprobar)

```
stm32f407vg (top)
 +- pins/
 |   +- pads[100]            (Pad: modelo V/I float + Schmitt; incluye PH0/PH1, PC14/PC15)
 |   +- pad_nrst, pad_boot0  (BOOT1 = PB2)
 |   +- power_pads           (VDD/VSS/VDDA/VSSA/VREF+/VBAT/VCAP -> float)
 |   +- pin_mux              (mux AF0-AF15 por pin, ruta analogica ADC/DAC/osc)
 +- rcc
 |   +- hsi, hse, lsi, lse   (osciladores; HSE/LSE conectados a pads via pin_mux)
 |   +- pll, plli2s
 |   +- clock_tree           (SW mux, prescalers, MCO1/2, RTCCLK mux, gating ENR)
 |   +- reset_ctrl           (POR/BOR/NRST/WWDG/IWDG/SW/CSS->NMI)
 +- cortex_m4f
 |   +- cpu                  (fetch/decode/exec [II]; RegFile como struct; monitores LDREX)
 |   +- fpu                  (FPv4-SP; lazy stacking con SCS)
 |   +- scs
 |   |   +- scb  (+CFSR/HFSR/VTOR/AIRCR...)   [IR, §9.6/10.2]
 |   |   +- nvic (82 IRQ + prioridades)       [IR, §9.2]
 |   |   +- systick                            [IR, §10.3]
 |   |   +- mpu                                [IR, §10.4]
 |   +- debug
 |       +- dap { swj_dp {jtag, swd}, ahb_ap } [IR, §13.2]
 |       +- fpb, dwt, itm, etm, tpiu, rom_table, dbgmcu  [IR, §13.3-13.9]
 +- ahb_matrix               (8 maestros x 7 esclavos, tabla [IR, §6.2])
 +- flash_if                 (Flash 1MB + ART + registros FLASH + option bytes)
 +- sram1 (112K), sram2 (16K), bkpsram (4K)
 +- ccm (64K)                (conectada al D-bus del nucleo, NO a la matriz)
 +- ahb1_decoder
 |   +- gpioA..gpioI, crc, rcc(regs), flash_if(regs), bkpsram, dma1, dma2,
 |   |  eth_mac, otg_hs, ahb_apb1, ahb_apb2
 +- ahb2_decoder
 |   +- otg_fs, dcmi, rng
 +- fsmc                     (AHB3; pines de bus externo via pin_mux)
 +- ahb_apb1 -> apb1: TIM2..TIM5, TIM6, TIM7, TIM12..TIM14, RTC, WWDG, IWDG,
 |               SPI2, SPI3(+I2S), USART2, USART3, UART4, UART5, I2C1..I2C3,
 |               CAN1, CAN2, PWR, DAC
 +- ahb_apb2 -> apb2: TIM1, TIM8, TIM9..TIM11, USART1, USART6, ADC(1..3+comun),
                 SDIO, SPI1, SYSCFG, EXTI
```

DMA1/DMA2, ETH_MAC y OTG_HS son esclavos AHB1 (configuración) **y** maestros de
la matriz (transferencias).

---

## 4. Matriz de interconexiones del top

| Dominio | Señales / sockets | Productor → Consumidores |
| :--- | :--- | :--- |
| **TLM** | 8×target/7×init en matriz; decodificadores AHB1/AHB2; 2 puentes APB; icode/dcode→flash_if; dcode-CCM directo | según §3 |
| **Relojes** | hclk, fclk, pclk1, pclk2, timclk1, timclk2, pll48ck, rtcclk, lsi_clk, hse_raw, lse_raw + freq_hz de cada uno | RCC → todos; gating por periférico dentro de RCC (vector `clk_en[]`) |
| **Resets** | por_n, sys_rst_n, periph_rst_n[] (RSTR), bkp_rst_n | RCC.reset_ctrl → todos; NVIC.SYSRESETREQ y IWDG/WWDG → RCC |
| **IRQ** | irq[0..81], nmi, exti_wakeup[] | periféricos → NVIC (números [IR, §9.1.2]); EXTI→PWR (wakeup) |
| **DMA** | dma1_req[s][c] / ack, dma2_req[s][c] / ack | tablas [IR, §11.4] |
| **Pines** | AnalogNet[100]; por pad: bundles digitales {out, oe, od, speed, pupd, analog_en}; af_out/af_in por periférico | pads ↔ pin_mux ↔ GPIO/periféricos; ADC/DAC/HSE/LSE por ruta analógica |
| **Núcleo** | irq_req/ack+número (NVIC↔CPU), excepción/stacking, FPU lazy (SCS↔FPU), sleeping/sleepdeep (CPU→PWR/RCC), STIR, systick_clk_sel | |
| **Debug** | SWDIO/SWCLK/JTAG (pads AF0), swo (PB3), trace port; AHB_AP→S-bus de la matriz; freeze[] (DBGMCU→timers/watchdogs) | [IR, §13] |

---

## 5. Estructura de ficheros `src/` (esqueleto a generar)

```
src/
  Makefile, README.md
  common/  analog_net.h  clock_gen.h  ahb_types.h  periph_base.h
  pins/    pad.h  pin_mux.h  power_pads.h
  bus/     ahb_matrix.h  ahb_decoder.h  ahb_apb_bridge.h
  mem/     flash_if.h  sram.h (sram1/2/bkpsram/ccm)
  rcc/     rcc.h  osc_pll.h
  core/    cortex_m4f.h  cpu.h  fpu.h  scs.h  debug.h
  periph/  gpio_port.h  timers.h  usart.h  spi.h  i2c.h  can.h  adc.h  dac.h
           watchdog.h  rtc.h  exti.h  syscfg.h  pwr.h  crc_rng.h  sdio.h
           fsmc.h  dcmi.h  eth_mac.h  otg.h  dma.h
  top/     stm32f407vg.h  sc_main.cpp
```

Cada módulo dummy declara **todos sus puertos** (TLM, relojes, reset, IRQ, DMA,
pines) y stubs `// TODO(fase N)`. El top `stm32f407vg.h` instancia y **conecta
todo** — es el contrato de integración del proyecto.

---

## 6. Decisiones que requieren aprobación

*   **D1 — Propuestas P1..P8**: aplicar todas (recomendado) o indicar cuáles no.
*   **D2 — Estilo de bus**: (a) *recomendado*: TLM-2.0 LT con anotación de
    tiempo y quantum, topología preparada para AT (fase posterior); (b) AT
    (nb_transport, fases) desde el inicio — máxima fidelidad de arbitraje,
    +50-100 % de esfuerzo y simulación ~10× más lenta; (c) nivel de señal AHB
    (HADDR/HTRANS/... como sc_signal) — no recomendado como base (coste muy
    alto); puede añadirse después como adaptador para bloques concretos.

---

## 7. Fases de implementación propuestas

| Fase | Contenido | Criterio de salida |
| :--- | :--- | :--- |
| **F0** | Esqueleto src/ completo (este entregable) | compila y elabora (sc_start arranca y termina) |
| **F1** | Infraestructura: ClockGen, reset, matriz LT, flash/sram, cargador de binario | lectura/escritura de memoria desde un maestro de prueba |
| **F2** | CPU: fetch/decode/execute ISA entera [II] + excepciones + NVIC/SysTick | `valida_instrucciones.py` como generador de tests de decodificador; CoreMark sin periféricos |
| **F3** | Pads/pin_mux/GPIO/RCC completos (modelo eléctrico) | blinky real compilado con CMSIS |
| **F4** | DMA1/2, USART, TIM avanzados, EXTI/SYSCFG | firmware con drivers HAL básicos |
| **F5** | Resto de periféricos (ADC/DAC, SPI/I2C/CAN, SDIO, RTC, watchdogs...) | suite HAL |
| **F6** | Debug (DAP/FPB/DWT/ITM), DBGMCU, trazas | conexión de un GDB-stub al DAP |
| **F7** | Bajo consumo, OTG/ETH/FSMC/DCMI, afinado AT de la matriz | según necesidad del proyecto |
```
