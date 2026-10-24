# STM32F303 en `mcu-sim` y `mcu-sim-gui`

**Qué hay que modificar y añadir, con la STM32F3DISCOVERY como sistema de
pruebas, y plan por fases** · 19-10-2026

La pregunta es qué haría falta para que `mcu-sim` simule la familia
**STM32F303** de ST **igual que hoy simula los STM32F4 y los MSP432**: ejecutar
sin recompilar el binario que sale de STM32CubeIDE o de `arm-none-eabi-gcc`,
con pines eléctricos, placas en XML y su dibujo en `mcu-sim-gui`, consola serie,
y descarga y depuración con las herramientas de siempre. El chip de referencia
es el **STM32F303VC** (LQFP100, 256 KB de Flash) y el sistema de pruebas, la
placa **STM32F3DISCOVERY** (MB1035), con su XML y su SVG.

El documento es hermano de `doc/analisis_msp432.md` y usa su misma estructura.
La diferencia de fondo entre los dos casos explica casi todo lo que sigue:
**con el MSP432 cambiaba el fabricante; aquí no cambia, pero cambia la
generación de los periféricos.** El F303 es el primer chip del proyecto con la
**segunda generación de IP de ST** —la de las familias F0, F3, L0, L4, G0, G4 y
posteriores—, y eso hace que una parte de lo que hay que escribir valga, más
adelante, para media docena de familias.

---

## Estado: S0 a S5 y S7 a S9 hechas (24-10-2026)

En la rama `stm32f303`, un commit por fase. Las siete suites de antes no se
mueven ni un picosegundo (`ci/comprueba_invariante.sh`, las siete en verde con
los tres commits encima), y hay una octava, **`testf3`**, con su línea en
`verif/invariantes.txt` (**193** comprobaciones; «el resto»,
**5 997 559 497 838 ps**: el total sin F19, que habla con un socket de verdad).
Las cifras son las de después del contraste con la documentación de ST
(abajo: con DS9118, UM1570 Rev 7 y el esquemático, y con RM0316 Rev 10).

**S0, fuentes y rasgos.** Versionados, sin tocar: `cmsis_device_f3` v2.3.9, la
HAL v1.5.9, el BSP de la Discovery v2.1.7 con los cuatro controladores de
sensores y tres ejemplos de STM32CubeF3 v1.12.0 (`verif/fw/stm32f3/sdk/`, con
su `LEEME.md`), y los dos XML de la base de pines (`soc/stm32f3/pindata/`).
`soc/stm32f3/gen_pines.py` genera de ellos el LQFP100 (87 E/S) y las 399
funciones alternativas. Los rasgos de §3.2, todos con el valor del F4 por
omisión: `GpioCaps` (AHB2 y `BRR`), `ExtiCaps` (36 líneas en dos tandas, las
directas solo despiertan), `TimCaps` v3 (seis canales, `OCxM[3]`, PWM
combinado y asimétrico, `GC5C`, BK2, `MMS2`, `UIFREMAP`, `SMS[3]`),
`IwdgCaps` (ventana), `CrcCaps` (polinomio y anchura programables, inversión
de entrada y salida), `RtcCaps` (sin `CALIBR`, 16 respaldos), CAN de 14 bancos
sin reparto, `TSEL` del DAC como tabla, la congelación del DBGMCU como dato, el
SPI con `fifo_v3` dentro de `spi.h` (no hizo falta `spi_v3.h`: el F4 no se
movió) y la CCM ejecutable (`MapaRam::ccm_ejecutable`). Los grupos R1-R7 de
`testf3` los ejercen sobre piezas sueltas.

**S1, el chip y la placa.** `F3Caps`, `REGISTRA_MCU(STM32F3, …)` y
`STM32F303VC` en el catálogo; `SocF3` (`soc/stm32f3/stm32f303.h`) con el
núcleo de 82 IRQ y 4 bits, `RccF3` (`rcc/rcc_f3.h`: HSI, HSE en cristal y en
bypass, PLL, los preescaladores, CSS, MCO y sus avisos), `FlashF1`
(`mem/flash_f1.h`: latencia, prebúsqueda, `HLFCYA` y el aviso), la SRAM de
40 KB, la CCM de 8 KB ejecutable con `SYSCFG_RCR`, la memoria de sistema con el
UID, `F_SIZE` y la calibración de fábrica, BOOT0/nBOOT1 y `MEM_MODE`, GPIO A-F,
EXTI, SYSCFG y PWR; lo demás, DECLARADO con su fase. `placas/stm32f3discovery.xml`
(sin sensores ni VCP) y su dibujo, `placas/stm32f3discovery.svg`, de
`placas/dibuja_st.py`; `make f3-discovery`; la prueba cruzada G8 de
`mcu-sim-gui`. **Cierre, cumplido**: `GPIO_IOToggle` y `GPIO_EXTI` de ST, sin
modificar, encienden la rosa y responden a B1 (F14, F15); los dos modos del HSE
llegan a 72 MHz sin un aviso (F8, F9); CoreMark corre y da los CRC canónicos
(F13); la ventana enseña la Discovery con la rosa encendiéndose (G8).

**S2, descarga y depuración.** El FPEC entero en `FlashF1` (llaves y bloqueo
duro, `PG` por medias palabras con error de bus para bytes y palabras, `PGERR`,
`WRPRTERR`, `PER`/`MER`/`STRT`, `OPTER`/`OPTPG` con `OPTWRE`, `OBL_LAUNCH` con
su reset y `OBLRSTF`, `BSY`/`EOP` y su interrupción, la CPU parada mientras
borra); `ProgramadorF3` (`soc/stm32f3/f3_programador.h`), el `load` de GDB por
las dos vías -para él, la sonda aprendió a escribir **medias palabras**
(`nv_escribir16`, `CSW.Size = 01`)-; `src/ide/stm32f3/` (CubeIDE, VS Code y
los dos `.cfg` de OpenOCD); RDP DECLARADO. **Cierre, cumplido**: `load` de GDB
por los pines y por el DAP con el mismo resultado (F19); OpenOCD con
`target/stm32f3x.cfg` sin tocar programa, verifica y el firmware nuevo
arranca, por `remote_bitbang` (O5 de `make openocd`) y por CMSIS-DAP (C6 de
`make cmsis-dap`), y GDB por su servidor hace `load` y `compare-sections`
(G3/D3); `FLASH_EraseProgram` de ST pasa (F18: 48 páginas, 49 152 medias
palabras, LD3).

**S3, los periféricos de segunda generación.** `UsartV2`
(`periph/usart_v2.h`, con rasgos: síncrona, flujo, RTO, DE, `SWAP`/`INV`/
`MSBFIRST`, modo mudo, ruptura por `RQR`; USART1-3 y UART4/5, estas sin DMA),
`DmaCanal` (`periph/dma_canal.h`: DMA1 de 7 canales y DMA2 de 5, prioridad y
luego el canal más bajo, circular, HT/TC/TE, la tabla de peticiones de RM0316
§13.4.7 con los cuatro remapeos de `SYSCFG_CFGR1`), el SPI v3 y el I2S de las
SPI2/3, `FmpI2c` en I2C1/2 con el reloj de `RCC_CFGR3` (el esclavo, reescrito:
direcciones de 7 y 10 bits, `OA2MSK`, estiramiento de `RXNE`/`TXIS`/`ADDR`,
`NACKF`, `STOPF`), los TIM v3 (TIM1/8 avanzados, TIM2 de 32 bits, TIM3/4,
TIM6/7, TIM15/16/17, con la red de ITR de RM0316 tabla 79 y el IRTIM), IWDG con
ventana y que se para con el reset del sistema, WWDG, RTC (con la
resincronización de `RSF`), CRC programable y Sleep/Stop/Standby en el PWR (los
tiempos de despertar de DS9118, el PVD de la tabla 27, `WKUP1-3`, los bits de
opción `nRST_STOP`/`nRST_STDBY`/`WDG_SW`). La placa gana el **VCP** (la USART1
en PC4/PC5, UM1570 §6.2.3) y hay un sistema de dos, `placas/dos_f3discovery.xml`,
con los tres buses de los readme y `placas/pullup_i2c.xml`; `make f3-vcp`
(con `verif/fw/stm32f3/f3_vcp`) prueba los dos. **Cierre, cumplido**: los 29
ejemplos de §7.2 marcados S3, **sin modificar**, en `verif/fw/stm32f3/ejemplos_s3`
(42 `.bin` con huella: los de dos placas en sus dos papeles y
`PWR_CurrentConsumption` en sus cuatro modos), pasan en `testf3`: F22-F31 con
dos Discovery cableadas (UART, SPI e I²C, sondeo, IT y DMA, y
`I2C_TwoBoards_AdvComIT`), F32-F39 los ocho TIM, F40-F42 los perros, F43-F46
el RTC y las bases de tiempos sin SysTick, F47-F53 el bajo consumo; y el VCP
con `make f3-vcp`. `testf3` sube a **296** comprobaciones (de 193) y «el
resto», a **99 913 429 462 079 ps**; las siete suites de antes no se mueven.

**S4, los sensores de la placa** (21-10-2026). `parts/mems_st.h`: la
`Postura` (alabeo, cabeceo y rumbo, tres mandos; la placa gira hasta donde
dicen a 90 °/s, y de ese giro sale la velocidad angular), `L3gd20` e
`I3g4250d` por SPI y `Lsm303dlhc` y `Lsm303agr` por I2C, con sus mapas de
registros, tasas, fondos de escala, pasos altos, interrupciones y DRDY
(`doc/parts.md` §4.14). La placa los monta según su **revisión**:
`<placa revision="E">` (la de por omisión) y `revision="A-D"`/`"E-"` en cada
componente, que un sistema puede cambiar (`placas/stm32f3discovery_d.xml`); el
dibujo rotula el chip que va (`U5@L3GD20`). Con ellos, R27 y R28, los 10 kΩ
del I2C1 [MB1035, hoja 4]. **Cierre, cumplido**: el ejemplo `BSP` de ST, sin
modificar, detecta los sensores de **las dos revisiones** por su `WHO_AM_I` y
enciende el LED del lado que baja, con el acelerómetro inclinado y con el
giróscopo girando (F56, F57); `f3_mems`, sin BSP, lee los tres sensores de cada
revisión y lo que dicen cuadra con la `Postura` cuenta a cuenta, también la
brújula, en cada fondo de escala (F54, F55); y la **demostración** de ST,
compilada desde una copia de STM32CubeF3 (SLA0044: no se versiona), arranca, y
con el giróscopo enciende el LED del lado que baja (F58, `make f3-demo`).
`testf3` sube a **328** comprobaciones (de 296) y «el resto», a
**115 237 619 112 941 ps**, igual en dos pasadas; las siete suites de antes no
se mueven.

**S5, lo analógico** (24-10-2026). Cuatro bloques nuevos, todos con su
porqué en la cabecera:

* **`periph/adc_v3.h`**, el ADC de segunda generación, **por parejas**
  (`AdcParV3`: maestro en 0x000, esclavo en 0x100, registros comunes en
  0x300, como `ADC1_2_COMMON` de CMSIS), dos en el SoC (ADC12 y ADC34). El
  ritual entero —ADVREGEN 10→00→01 y sus 10 µs, ADCAL simple y diferencial
  (112 ciclos, CALFACT), ADEN hasta ADRDY—, y lo que el silicio deja hacer mal
  lo avisa: sin regulador no arranca; antes de los 10 µs o sin calibrar
  convierte con un desplazamiento de 16 cuentas [de modelo]. 18 canales con
  DIFSEL, muestreo de 1,5 a 601,5 ciclos y RES; 16 regulares y 4 inyectados
  con la cola de contextos (JQM, JQOVF); continuo, discontinuo, JAUTO,
  AUTDLY; OFR1-4 con signo, alineación, OVR/OVRMOD; los tres perros; DMA de
  una vez o circular, con `DmaCanal::al_terminar` como DMA_EOT; y los modos
  duales (simultáneo regular e inyectado, entrelazado con DELAY, alterno),
  con MDMA y CDR. Los 64 disparos (tablas 89-92) son señales: TRGO, TRGO2,
  OCxREF (S5 las saca de `TimerBase`, rasgo `internas`) y las líneas 2, 9, 11
  y 15 del EXTI. El reloj, `ADCxy_CK` del RCC (nuevo: `K_ADC12`, `K_ADC34`) o
  HCLK/1, /2, /4 por CKMODE. Las entradas son funciones: los pines salen de la
  base de pines (`gen_pines.py` escribe `f3_analogico.inc`, 50 entradas); las
  internas, del SoC: el sensor (V25 = 1,43 V, 4,3 mV/°C, a la temperatura que
  pone el banco: `SocF3::set_die_temp`), VREFINT (1,23 V), VBAT/2 y VREFOPAMPx.
* **El DAC1**: el `DacBase` de siempre con los disparos del F3 (TSEL 001 es
  TIM8 o, con `DAC1_TRIG1_RMP`, TIM3), su DMA (DMA2 3/4 o DMA1 3/4 con los
  remapeos) y su IRQ con la del TIM6. Arreglado para todos: con TEN = 1,
  encender el canal ya no carga DOR (lo hace el disparo, RM0316 §16.5.4).
* **`periph/comp.h`**, los siete comparadores, y **`periph/opamp.h`**, los
  cuatro amplificadores. Sus registros están en la ventana del SYSCFG, que
  ahora se los **delega** (`SyscfgF3::analogico`). Los comparadores comparan
  los nodos de los pines (o VREFINT y sus fracciones, o PA4/PA5, donde pone el
  DAC) con histéresis y POL, y su salida va al bit OUT, a su línea del EXTI
  (21, 22, 29-33), a los pines COMPx_OUT y a los frenos de TIM1/TIM8
  (`brk_acth`, `brk2_acth`). Los amplificadores conducen VOUT (PA2, PA6, PB1,
  PB12) con 10 Ω: seguidor y PGA al instante, recortados a los raíles; el
  modo autónomo integra el error con 8,2 MHz de GBW y 4,7 V/µs, y cierra
  cualquier red del banco; con CALON, OUTCAL. El F302, que no tiene COMP3/5/7
  ni OPAMP3/4, ve esos registros reservados.
* **El TSC**, declarado, como estaba.

El firmware es propio, **`verif/fw/stm32f3/f3_analog`**, con CMSIS y las
funciones en línea de los LL de ST (ST no trae ejemplos analógicos para esta
placa), y el banco pone seis fuentes de tensión. **Cierre, cumplido**: F63 lee
el sensor y VREFINT y calcula VDDA con `VREFINT_CAL` (3300 mV) y la
temperatura con `TS_CAL1`/`TS_CAL2` —a mano y con `__LL_ADC_CALC_TEMPERATURE`—
a 25, -40, 85 y 105 °C, a ±0,5 °C de la del banco; F64 convierte IN6 en
diferencial (PC0 contra PC1, ±1 V: 2669 y 1426 cuentas) por la IRQ 18; F65
pone ADC1 y ADC2 en simultáneo regular con MDMA y el DMA1 canal 1, 16
palabras de CDR, y los para a los dos con ADSTP; F66 hace que el COMP4 (PB0
contra VREFINT) frene el TIM1 por BRK_ACTH —MOE a cero, BIF y PE9 quieto— y
avise por la línea 30 (IRQ 65); F67 pone el OPAMP1 en PGA de 4 (0,5 V en PA1,
2 V en PA2, leídos por el ADC1) y lo ve recortar; y F68 dispara el DAC1 con el
TRGO del TIM3 por el remapeo y el ADC2 lo lee en PA4. `testf3` sube a
**404 (de 375)** comprobaciones y «el resto», a **116 195 423 487 941 ps** (igual en dos pasadas); los
grupos de antes no se mueven ni un picosegundo (lo analógico, conectado y sin
usar, no cuesta nada). Coste (riesgo 7): los 70 ms simulados de F63-F68, con el ADC en continuo a casi 1 Msps en F65, cuestan 0,6 s de reloj en esta máquina; no ha hecho falta agrupar conversiones.

**S7, el cargador de sistema** (21-10-2026). El de ST no se puede distribuir:
en la memoria de sistema va uno **propio**, `soc/stm32f3/cargador/` (C con
CMSIS, 2 KB), que `gen_sysmem.sh` compila y convierte en `sysmem.inc`
(versionado, con la huella del `.bin`: el modelo no necesita el compilador
cruzado). Habla el protocolo de **AN3155** por la USART1 (PA9/PA10) y la USART2
(PD5/PD6) a 8E1: mide la velocidad con los dos flancos de bajada del `0x7F`
(ocho bits, con el SysTick a los 8 MHz del HSI; la USART v2 del modelo no tiene
auto-baud), y atiende GET, GET VERSION (3.1), GET ID (`0x0422`), READ, GO
(VTOR, MSP y lo que tocó, a su estado de reset), WRITE (Flash por el FPEC en
medias palabras, SRAM y los bytes de opción con su `OBL_LAUNCH`), EXTENDED
ERASE (masivo y por páginas), WRITE UNPROTECT y READOUT UNPROTECT; WRITE
PROTECT y READOUT PROTECT se anuncian y contestan NACK [de modelo]. Con RDP en
el nivel 1, READ, WRITE y GO contestan NACK. Se reserva los 5 KB de SRAM del
de ST (hasta `0x2000_1400`, lo que `stm32flash` da por libre) y sigue dejando
`'MSYS'` en `0x2000_0000` (F4). La Discovery entra en él con un puente entre
BOOT0 y VDD, que en P2 son los pines 19 y 20: `placas/stm32f3discovery_boot0.xml`
lo pone y cuelga un FT232RL (`ftdi_ft232rl.xml`) de la USART1. **Cierre,
cumplido**: `stm32flash` 0.7, sin modificar, reconoce el chip, borra, escribe
`f3_vcp.bin`, lo verifica y lo arranca, y el VCP de la placa da su saludo
(`make f3-stm32flash`, por un pty y RFC 2217 con `verif/pty_rfc2217.py`; con
`-m 8n1`, porque un pty de Linux no guarda la paridad); y en `testf3`, un
anfitrión de AN3155 escrito en el banco recorre todas las órdenes por las dos
USART, a 115 200 y a 57 600, con RDP, el desbloqueo y los bytes de opción
(F59, F60). `testf3` sube a **362** comprobaciones (de 328) y «el resto», a
**116 110 029 112 941 ps**; las siete suites de antes no se mueven.

**Lo que S7 ha destapado, para todos los chips:** el GPIO ponía `IDR` a cero en
su reset (el del sistema o el de `AHBRSTR`) y no lo volvía a muestrear hasta que
un pad cambiase. El cargador, tras un `GO` que reinicia los puertos y un reset,
leía PD6 a cero con la línea alta y tomaba por arranque lo que no lo era. Ahora
se muestrea al salir del reset; las siete suites, al picosegundo.

**S8, la ventana y la documentación** (21-10-2026). Lo opcional de §6, hecho
sin tocar el protocolo: en `mcu-sim-gui`, el panel de una pieza con un
`alabeo` y un `cabeceo` continuos lleva un **mando de inclinación**, un círculo
con un punto que se arrastra hacia el lado que baja y que no ordena nada por su
cuenta: mueve los dos deslizadores, que son los que ordenan (G1c de
`prueba_ventana`). Y la **aguja del rumbo**: la tabla de enlaces admite
`observable=` con `giro` y `angulo`, para que el efecto lo mueva un observable
que no es el que se elegiría solo; `MOV` deja de ser invisible y es una aguja
entre U5 y U3 (`dibuja_st.py`) con
`<enlace pieza="MOV" elemento="MOV" efecto="angulo" observable="rumbo"/>`
(I14b de `prueba_ilustracion`, y dos comprobaciones más en `gui-sistema`, 181).
La guía de usuario, `doc/protocolo.md` de la GUI y `doc/parts.md` (§2.6 y la
`Postura`) lo dicen. Ninguna suite se mueve: es de la ventana y de los XML.

**S9, el resto del die 0x422** (21-10-2026). Quince referencias en el
catálogo, un `F3Caps` cada una (`soc/stm32f3/f3_desc.h`): F303VB/VC (LQFP100),
RB/RC (LQFP64) y CB/CC (LQFP48); F302 en los mismos seis; y F358VC/RC/CC.
`gen_pines.py` genera ahora los seis encapsulados (`f3_patillas_*.inc`: los
tres del F303, que son los del F302 —lo comprueba—, y los tres del F358, que
tiene NPOR donde el F303 tiene PB2) y `f3_lineas.inc`, lo que cada línea no
tiene según la base de pines: el **F302**, ADC3/4, COMP3/5/7, OPAMP3/4, TIM7 y
TIM8, y la CCM (`stm32f302xc.h` de CMSIS no la define); el **F358**, el USB. Lo
que no tiene es una **ventana reservada** (`BloqueAusente`: lee cero, se pierde
lo escrito y avisa una vez diciendo que el chip no lo tiene), y lo dice
`limitaciones()`. El cargador de S7 saca la SRAM de `F_SIZE` (32 KB en los xB)
y deja de ofrecer la CCM, que el F302 no tiene; y su medida de la velocidad
descarta lo que no es un `0x7F` (el arranque tiene que durar un bit y lo que
sigue, siete): con la carga de la máquina, `make f3-stm32flash` fallaba una vez
de cada pocas porque el cargador tomaba por `0x7F` algo que no lo era (la
conexión del terminal, probablemente). **Cierre, cumplido**: F61
contrasta los quince sin tiempo simulado; F62 enciende un F302CB, un F303RB y un
F358RC, comprueba por el bus `F_SIZE`, la SRAM, la CCM, el TIM7, el TIM8 y el
USB que faltan, y habla AN3155 con el cargador del F302CB por PA9/PA10 de su
LQFP48. `testf3` sube a **375 (de 362)** comprobaciones y «el resto», a
**116 125 283 487 941 ps** (igual en dos pasadas). Los otros dos dies, `0x446` (F303xD/E) y `0x438` (F303x6/8 y
F334), tienen su plan en `doc/analisis_f303xD-E_f303x6_8_F334.md`, con la lista
de lo que hay que bajar a `doc/refs/`.

**Los ejes, y la demo.** La placa: x al este (LD7), y al norte (LD3), z hacia
fuera. El giróscopo, x al este e y al norte: así lo usan el ejemplo BSP y la
demo, que coinciden (un giro con el borde de un LED bajando enciende ese LED).
El acelerómetro, x al norte e y al oeste: así lo usa el ejemplo BSP (el LED del
lado que baja); la **demo** usa otra tabla, girada un cuarto de vuelta
(x > 0 enciende LD7 y y < 0, LD3), que con z hacia fuera no es una rotación de
la del BSP sino una reflexión: con los ejes del modelo, la demo enciende LD6 con
el norte abajo, y F58 comprueba eso. El magnetómetro del AGR, x al sur e y al
este, el de la brújula de LED del libro «Discovery» de Rust para esta placa; el
del DLHC, los del acelerómetro. Los tres últimos, de una sola fuente
(P-18 t).

**Lo que S4 ha destapado fuera de los sensores**, y arreglado:

* **El I2C (`FmpI2c`, también el del F446)**: `TC` no se borraba al poner
  `START` o `STOP`, y un firmware que lo espera tras la subdirección seguía de
  largo; `TCR` no se borraba al escribir `NBYTES`, y una escritura con `RELOAD`
  (la de `HAL_I2C_Mem_Write`) se quedaba esperando. Y **SCL y SDA son de drenador
  abierto por el periférico**: el BSP de la Discovery pone PB6/PB7 en `AF_PP`
  con pull-down, y en la placa funciona. Con `OTYPER` = 1 no cambia nada; las
  siete suites de antes, al picosegundo.
* **El núcleo del F303 sincroniza antes de escribir a un periférico**
  (`Cpu::sync_escritura_perif`): con el quantum de 1 µs, dos escrituras de GPIO
  seguidas llegaban en el mismo instante y el pulso del CS del giróscopo entre
  dos lecturas del BSP (unos 0,2 µs) desaparecía. Solo en el F303: los F4 y los
  MSP432 siguen como estaban.

**Lo que S3 ha destapado fuera del F303** (las siete suites siguen al
picosegundo con los cuatro arreglos):

* **El núcleo, al volver de una excepción**, desapilaba `xPSR` con la máscara
  0xF900FDFF, que perdía IT[1:0] y GE; y la instrucción de vuelta borraba el
  ITSTATE recién desapilado. Una interrupción dentro de un `ITTTT` volvía con el
  bloque roto (lo destapó `I2C_IsErrorOccurred` de la HAL). Ahora 0xFF0FFDFF, y
  el ITSTATE se respeta.
* **El MSP no se guardaba antes de leer la trama de vuelta.** Si una
  interrupción anidada volvía en mitad de un manejador que luego seguía
  desapilando, la vuelta final leía la trama unas palabras más abajo y saltaba
  con T = 0 (`HAL_TimeBase_RTC_WKUP`: el RTC_WKUP se apropia del EXTI0 en
  `HAL_ResumeTick`).
* **WFI se despertaba con cualquier excepción pendiente**, también con las que
  no pueden entrar. ARMv7-M B1.5.19: solo las que se tomarían con PRIMASK = 0.
  `PWR_STANDBY` entra en Standby desde el manejador del EXTI0, con el SysTick
  de su misma prioridad pendiente, y no se dormía.
* **Un acceso a un periférico sin reloj** era un error de bus; en el F3 lee cero
  y la escritura se pierde (`BusSlave::sin_reloj_ignora`): `HAL_UART_DeInit`
  escribe antes de que `MspInit` abra el reloj.

**Y en el modelo del F303**, lo que los ejemplos han pedido: el RTC guarda en
`DR` lo que se escribe aunque no sea una fecha (`HAL_TimeBase_RTC_ALARM` pone
la fecha a cero y la alarma a día 0); el reset de Standby no saca el pin NRST
(si no, el PWR lo tomaba por un NRST externo y despertaba al instante);
`EWUP1` deja vivo el pad de PA0 en Standby; el esclavo SPI no pierde lo que
se cargó antes del primer flanco; la captura de un TIM con reinicio por
esclavo ve la cuenta de antes del reinicio; y el TI1 del TIM16 se remapea al
MCO (`TIM16_OR`), que es como `IWDG_Reset` mide el LSI.

**Lo que es del ejemplo y no del modelo:** `TIM_Asymetric` con la HAL v1.5.9
usa el mismo manejador para TIM8 y TIM1, y `HAL_TIM_PWM_Start` del TIM8 vuelve
con `HAL_ERROR`: enciende LD3, el error. F35 lo comprueba así, con el TIM1 en
marcha y el TIM8 configurado. `PWR_CurrentConsumption` mide corriente, que el
modelo no tiene: F50-F53 comprueban que entra en el modo y sale por donde dice
su readme. `FreeRTOS_ThreadCreation`, opcional, no está (habría que versionar
FreeRTOS para el F3).

**Lo que se ha decidido distinto de lo previsto:**

* **La matriz** (riesgo 6): decodificadores directos, como los MSP432, y no
  `AhbMatrix`. **Decidido en S3**, con los DMA dentro: cada DMA es un iniciador
  con su socket al decodificador (sin la CCM, como en el silicio) y factura un
  ciclo de HCLK por fase, sin contención con la CPU. Ningún ejemplo de ST lo
  nota; lo que se pierde es la medida de contención.
* **Los grupos de `testf3`** no son los de §7.1: R1-R7 (S0), F0-F15 (S1),
  F16-F21 (S2), F22-F53 (S3, uno por ejemplo de ST) y F54-F58 (S4; F58, la
  demo, solo con `make f3-demo`). La tabla de §7.1 queda como plan de S5 en
  adelante.
* **Cada ejemplo de S3 arranca desde un encendido**, no desde un reset por el
  pin: VBAT va con VDD en la Discovery, y sin cortarla el RTC, `PWR_CSR` y las
  banderas de `RCC_CSR` llegan del ejemplo anterior (`PWR_STANDBY` despertaba
  al instante con el despertador que había dejado `PWR_STOP`).
* **`make f3-dos` es `make f3-vcp`**: el sistema de dos placas se valida y se
  arranca ahí; el intercambio, que necesita pulsar B1, es de `testf3`.
* **El IWDG del F3 se para con el reset del sistema** (`IwdgCaps::para_con_reset`);
  el del F4 no se ha tocado, y queda la duda de si debería (P-18 p).
* **El MCO del ST-LINK sin onda** (`ExtClock onda="no"`): ocho millones de
  flancos por segundo costaban tres veces el resto de la placa. La pieza dice
  su frecuencia al HSE, que arranca igual en cristal y en bypass.
* **`HAL_RCC_ClockConfig` del F3** cambia `SW` antes de dividir APB1, así que
  durante unas instrucciones PCLK1 vale 72 MHz. El RCC mira los topes 20 us
  después de cada cambio, y solo avisa si siguen rotos.

**Contrastado con DS9118, UM1570 Rev 7 y el esquemático MB1035 rev. D.1**
(20-10-2026, en `doc/refs/stm32f303/`; `doc/fuentes.md` §1). Lo que dicen y lo
que ha cambiado por ello:

| Qué | Lo que dicen | Qué ha cambiado |
| :--- | :--- | :--- |
| P1 y P2 | La tabla 7 de UM1570 Rev 7 y la hoja 1 del esquemático coinciden pin a pin con el XML (`EXP1`/`EXP2`): el emparejamiento deducido de la Rev 3 era el bueno | Con los puentes de fábrica (tabla 6) **P2.5/P2.6 (PF0/PF1, SB17/SB18) y P2.41/P2.42 (PA12/PA11, SB21/SB22) no están conectados**: el XML ya no los une |
| Las resistencias de la rosa | 510 Ω en los dos verdes (R43 en LD6, R44 en LD7) y 680 Ω en los otros seis (hoja 4) | El XML las tenía en los azules |
| B1 | VDD → R37 (100 Ω) → pulsador → R38 (330 Ω) → SB20 → PA0, y R4 (220 kΩ) a masa (hoja 4) | `r_cerrado` de 330 a 430 Ω |
| LD1, BOOT0, B2, X3, MCO | LD1 con R1 de 1 kΩ en 3V; BOOT0 a masa por R35 (510 Ω); R39 (el pull-up de NRST) y X3 sin montar; el MCO por SB12 y R48 (hojas 2-4) | Nada: ya era así |
| El FPEC | t_prog 40/**53,5**/60 µs; página y borrado masivo, 20-40 ms sin típico (tabla 47) | La media palabra, de 40 a **53,5 µs**; las páginas siguen en 20 ms (el mínimo) |
| El POR | t_RSTTEMPO 1,5/**2,5**/4,5 ms (tabla 26) | El RCC lo espera tras cada encendido; F5 lo comprueba |
| Relojes | HSI 8 MHz y arranque de 1-2 µs; LSI 40 kHz y 85 µs; HSE 2 ms; LSE 2 s; PLL 1-24 MHz de entrada, 16-72 de salida y t_LOCK de 200 µs; HSE de 4-32 MHz con cristal y 1-32 en bypass; APB1 hasta 36 MHz (tablas 24 y 40-46) | El aviso del HSE distingue cristal y bypass; lo demás ya era así |
| La latencia de la Flash | 0 hasta 24 MHz, 1 hasta 48, 2 hasta 72 (§3.3 y §6.3.5) | Nada: ya era así |
| La calibración | V25 = 1,43 V, 4,3 mV/°C, VREFINT 1,23 V; las direcciones de las tablas 29 y 79 | TS_CAL1 y TS_CAL2 se calculaban como si 1,43 V fuera a 30 °C: ahora **1748 y 1321** (eran 1774 y 1347) |

**Contrastado con RM0316 Rev 10** (enero de 2024, 20-10-2026, en
`doc/refs/stm32f303/`). Confirma, y el código lo cita ya por sección: la
prebúsqueda de dos bloques de 64 bits (§4.2.2), las latencias y `ACR` = 0x30
(§4.5.1), la programación por medias palabras con su error de bus, `PGERR` con
la excepción del cero y `WRPRTERR` (§4.2.3), la protección por pares de
páginas y el último bit para la 62 en adelante (§4.3.2, §5), `OPTERR` y el
comparador que no mira lo borrado (§5), `AHBENR` = 0x14, `BDCR` = 0x18 y la
regla de `HSEBYP` (§9), la CCM de 1 KB por página sin estados de espera y
`SYSCFG_RCR` (§3.3), los modos de arranque (§3.5), `CFGR1` = 0x7C00_000X
(§12.1.1), `EXTI_IMR1` = 0x1F80_0000 e `IMR2` = 0xFFFF_FFFC (§14.3), el IWDG
con ventana (§25.3.2), los PWM combinado y asimétrico y `GC5Cx` (§20), los 14
bancos del bxCAN (§31.3.4), el `DBGMCU_IDCODE` con `REV_ID` 0x1003 = revisión
Y y la congelación de `APB1_FZ`/`APB2_FZ` (§33), y el formato del UID (§34.1).

Y lo que ha cambiado por él (comprobado en F20, F21, R4 y R6 de `testf3`):

| Qué | RM0316 | Antes |
| :--- | :--- | :--- |
| Una llave mal en `FLASH_KEYR` | Error de bus (HardFault) y el FPEC cerrado hasta el reset (§4.2.3) | Cerraba, sin error |
| `FLASH_CR` cerrado | Solo admite `OBL_LAUNCH`; con `BSY`, ningún registro se escribe (§4.2.3) | Ignoraba también `OBL_LAUNCH`; con `BSY` se escribía |
| `STRT` y `FLASH_AR` | `STRT` se lee a uno mientras dura el borrado (§4.5.5); `AR` guarda la última dirección usada (§4.5.6) | `STRT` a cero; `AR`, solo lo escrito |
| Programar un byte de opción | Solo si está borrado, **sin** la excepción del cero (§4.2.3) | Admitía el cero |
| El nivel de RDP | Del byte **y** su complemento: 0xAA/0x55, 0xCC/0x33, lo demás nivel 1 (tabla 8) | Solo del byte |
| Volver al nivel 0 | Programar RDP = 0xAA borra antes la Flash entera; en el nivel 2, ni `OPTER` ni programar RDP (`WRPRTERR`) (§4.3.1) | No borraba; el nivel 2 no bloqueaba nada |
| Cuándo se cargan los bytes de opción | En **cada** reset del sistema (§5) | Solo al encender y con `OBL_LAUNCH` |
| `HSEON` y `HSION` | No se apagan si alimentan SYSCLK, también a través del PLL (§9.4.1) | Solo el HSI, y solo directo |
| `LSEBYP` y `MCOF` | `LSEBYP` solo con el LSE apagado (§9.4.9); `MCOF` (bit 28) a uno cuando el MCO ha cambiado (§9.4.2) | Sin ninguna de las dos |
| `SYSCFG_CFGR1` | El bit 14 (`TIM7_DAC1_CH2_DMA_RMP`) existe en el F303xC (§12.1.1) | No se dejaba escribir |
| CRC | Escribir `CRC_INIT` carga ya `CRC_DR` (§6.3.3) | Solo con `CR.RESET` |
| SPI v3 | Un `DS` «no usado» se queda en 0111 (§30.9.2) | Se guardaba tal cual |

**Lo que queda de modelo, porque RM0316 no lo dice** (P-18 de `doc/todo.md`):
que una llave con `CR` ya abierto lo cierre; que escribir en una página
protegida de la CCM sea un error de bus; que `REV_OUT` refleje solo los
`POLYSIZE` bits; el umbral de 8 MHz del aviso de `HLFCYA`; el muestreo de BOOT0
durante el reset (el silicio lo hace en el cuarto flanco de SYSCLK); el NMI del
CSS como un pulso; el valor de `HSICAL` y del byte USER de fábrica; y la vf de
cada color de LED, que el esquemático no da.

---

## Cómo leer este documento, y qué crédito darle

Cada dato lleva su fuente. Las que se han podido **leer por máquina** desde aquí
son las de más crédito, porque se compararon con un programa y no a ojo:

| Etiqueta | Qué es | Cómo se ha usado |
| :--- | :--- | :--- |
| `[CMSIS]` | `stm32f303xc.h` de `STMicroelectronics/cmsis_device_f3` (Apache-2.0) | Mapa de memoria, vectores, bits; **comparación estructura a estructura con `stm32f407xx.h`** (§2.2) |
| `[HAL]` | `stm32f3xx_hal_driver` (BSD-3) | Constantes (página de 2 KB, latencias, PMA del USB, direcciones de calibración) |
| `[BSP]` | `stm32f3discovery-bsp` y los controladores `stm32-l3gd20`, `-lsm303dlhc`, `-i3g4250d`, `-lsm303agr` (BSD-3) | Pines de la placa, cómo detecta ST los sensores, cómo los configura |
| `[CUBE]` | `STM32CubeF3/Projects/STM32F3-Discovery` | Los ejemplos de la placa (BSD-3) y la demostración (SLA0044) |
| `[PINDATA]` | `STM32_open_pin_data`: `STM32F303V(B-C)Tx.xml` y `GPIO-STM32F303_gpio_v1_0_Modes.xml` | Versiones de IP de cada bloque (§2.3), encapsulado y **tabla de funciones alternativas comparada con la del F407** (§2.4) |
| `[UM1570]` | Manual de la placa, Rev 3 (feb. 2013), leído de una copia pública | Tabla 6 (pin ↔ función de la placa), relojes, puentes de soldadura, JP3 |
| `[ST-web]` | Ficha de la STM32F3DISCOVERY y sus *data briefs* DB1739 Rev 4 y 5 | ST-LINK/V2 frente a V2-B según la revisión |
| `[stm32flash]` | `dev_table.c` de stm32flash (GPL-2.0+) | DEV_ID `0x422`, memoria de sistema, bytes de opción |
| `[OCD]` | `target/stm32f3x.cfg` del OpenOCD que ya construye el CI | Qué controlador de Flash usa OpenOCD (`stm32f1x`) |
| `[RM0316]`, `[DS9118]`, `[AN2606]` | Manual de referencia, hoja de datos del F303xB/C y nota de los cargadores | **No se han podido bajar desde este entorno** (§9, riesgo 1). Lo que se cita de ellos va marcado **[1F]** hasta contrastarlo. **RM0316 Rev 10, DS9118, UM1570 Rev 7 y el esquemático MB1035 están en `doc/refs/stm32f303/` desde el 20-10-2026** («Estado», arriba); AN2606, no |

La regla del proyecto sigue en pie: **ningún dato entra en un descriptor sin dos
fuentes**. Lo que aquí solo tiene una lleva **[1F]** y es trabajo de la fase S0.

---

## 0. Resumen

* **El núcleo vale tal cual, y todo lo que está fuera del chip también.** El
  F303 lleva el mismo Cortex-M4F r0p1 que el F407, con **4 bits de prioridad,
  MPU y FPU** `[CMSIS]`, y **82 posiciones de vector** (de la 0 a la 81, como el
  F407, pero con otro significado). Stubs de GDB, sonda SWD, OpenOCD por
  `remote_bitbang` y **CMSIS-DAP por TCP** (la X2 de `doc/analisis_xds110.md`),
  netlist, sistemas, puente serie, frontera con la GUI, CI e invariantes: todo
  se hereda.
* **Lo de la fase 0 de los MSP432 ya está hecho, y aquí se nota.** `McuDesc`
  con descriptor propio por familia, `programador_nv_if`, ELF en el cargador:
  el F303 entra por la puerta que abrió T0. Lo que queda por generalizar es
  poco y es **de ST**, no de ARM (§3.2).
* **Pero no es «un F4 más» como el F446.** El F446 se construyó como una
  derivada de `SocF4` porque era el mismo *die* con otro árbol de reloj. Aquí,
  de los veinte bloques de registros que los dos chips nombran igual, **solo
  seis son idénticos** (CAN, DAC, DBGMCU, PWR, WWDG y, solo en apariencia, SPI)
  y la base de pines de ST da **otra versión de IP** a casi todo lo demás
  (§2.3). Derivar de `SocF4` sería la «etiqueta falsa» que `top/mcu_caps.h` dice
  que no se debe poner. **Hace falta un SoC nuevo, `SocF3`, con su familia
  `STM32F3` y su descriptor**, que reutiliza bloques de `periph/` por sus rasgos.
* **Lo que se reutiliza, por rasgos:** GPIO (más `BRR`), temporizadores (con
  las novedades de la versión 3: seis canales, `OCxM` de 4 bits, *break2*),
  **el I²C entero** —es la misma IP que el `FMPI2C1` del F446, `i2c2_v1_1`, y ya
  está modelada en `periph/fmpi2c.h`—, bxCAN, DAC, RTC, perros, CRC, EXTI y el
  modelo de osciladores de `rcc/osc_pll.h`.
* **Lo que hay que escribir:** RCC y controlador de Flash (los del F1, no los
  del F4), **DMA por canales**, **USART de segunda generación** (`ISR`/`ICR`,
  `RDR`/`TDR`), el **ADC nuevo** (cuatro convertidores con regulador,
  calibración y entradas diferenciales), el **USB de dispositivo** (FSDEV, con
  su memoria de paquetes), **7 comparadores y 4 amplificadores operacionales**,
  y el SYSCFG del F3. El TSC (sensor táctil) se declara.
* **La inversión rinde fuera del F303.** USART v2, DMA por canales, FSDEV y el
  I²C nuevo son los de F0, L0, L4, G0 y G4. Por eso se proponen como **módulos
  genéricos de `periph/`**, no como cosas de `soc/stm32f3/`.
* **La placa (§5).** La STM32F3DISCOVERY tiene la rosa de **ocho LED**
  (PE8-PE15), el pulsador azul a VDD en PA0, el reset, el **reloj de 8 MHz que
  da el MCO del ST-LINK** en PF0, el **puerto COM virtual en USART1 (PC4/PC5)**
  desde la revisión C, **giróscopo por SPI1** y **brújula-acelerómetro por
  I2C1**, el USB de usuario y el puente JP3 para medir IDD. **Hay dos
  revisiones de sensores**: hasta la D, L3GD20 + LSM303DLHC; desde la E,
  I3G4250D + LSM303AGR. El BSP de ST distingue cuál hay leyendo `WHO_AM_I`, así
  que el modelo puede ofrecer las dos con un atributo.
* **Piezas nuevas (§5.4):** los cuatro sensores y una **`Postura`** que les da
  a todos la misma orientación, para que acelerómetro, giróscopo y brújula
  cuenten la misma historia. En la GUI funcionan con los mandos que ya hay; un
  mando de inclinación más cómodo es opcional (§6).
* **Las pruebas (§7):** un banco `testf3` con su invariante; `make f3-discovery`
  con la placa; los **ejemplos de STM32CubeF3 para esta placa sin modificar**
  (BSD-3), incluidos los *TwoBoards* como **un sistema de dos Discovery**;
  OpenOCD con `target/stm32f3x.cfg` por CMSIS-DAP; y la prueba cruzada de la GUI.
* **Descarga y depuración (§4):** D1 a D3 y G1 a G3 salen casi solas. Lo único
  nuevo de verdad es el `programador_nv_if` del controlador de Flash del F1, y
  **el de OpenOCD (`stm32f1x`) corre su cargador en el núcleo simulado**. El
  cargador de sistema de ST (D4, AN3155) se puede sustituir por uno propio,
  como en el E4, y probarlo con `stm32flash`: es opcional.
* **Siete trampas que ha destapado la investigación** y que el plan tiene que
  respetar, porque cualquiera de ellas hace que el modelo falle donde la placa
  funciona, o al revés:
  1. **El SPI tiene la misma estructura de registros y otro comportamiento.**
     `SPI_TypeDef` es idéntica, pero el bit 11 de `CR1` es `DFF` en el F4 y
     `CRCL` en el F3; el tamaño de dato está en `CR2.DS` (4 a 16 bits), hay
     FIFO [1F: de 32 bits] y **un acceso de byte a `DR` mete un byte, no media
     palabra**: HAL escribe y lee `DR` con punteros de `uint8_t` `[HAL]`. Una
     comparación por nombres diría «se reutiliza».
  2. **El ejemplo de ST arranca el HSE en modo cristal con el MCO del
     ST-LINK** (`RCC_HSE_ON`, no `RCC_HSE_BYPASS`) `[CUBE]`, y en la placa
     funciona. El modelo no puede ser más estricto que eso. Hoy no lo es
     (`rcc/osc_pll.h` acepta cualquier fuente en modo cristal y toma la
     frecuencia nominal, 8 MHz, que aquí coincide), y una prueba tiene que
     fijarlo.
  3. **El BSP deja `I2C_TIMINGR` a cero** (`Init.Timing` no se rellena) `[BSP]`,
     con el reloj del I²C del HSI a 8 MHz. El I²C va mucho más deprisa de lo
     nominal y los sensores lo aguantan. El modelo puede **avisar**, pero no
     debe romper la transferencia.
  4. **El BSP habla con el giróscopo en modo SPI 0** (CPOL = 0, CPHA = 0)
     `[BSP]`, y el L3GD20 está especificado en modo 3. Funciona porque los dos
     muestrean en el flanco de subida. El modelo del sensor tiene que muestrear
     en la subida y cambiar en la bajada, **sin exigir el nivel de reposo**.
  5. **El acelerómetro del LSM303DLHC no documenta `WHO_AM_I`**, y el BSP
     depende de leer `0x33` en `0x0F` `[BSP]`. El modelo tiene que contestarlo.
  6. **Las funciones alternativas no se pueden copiar del F407.** De 399 pares
     (pin, señal) del F303 solo 148 están también en el F407, y de esos **39
     tienen otro número de AF**: TIM1 va por AF1 en el F4 y por AF2, AF4, AF6
     o AF11 en el F303 según el pin; TIM8, por AF3 frente a AF4 o AF10;
     UART4/5, por AF8 frente a AF5 `[PINDATA]`. La tabla se **genera** de la
     base de pines, como la del E4.
  7. **USB y CAN comparten** los 512 bytes de memoria de paquetes [1F: RM0316]
     y los vectores 19 y 20 (`USB_HP_CAN_TX`, `USB_LP_CAN_RX0`), salvo que `SYSCFG_CFGR1.
     USB_IT_RMP` mueva los del USB a 74-76 `[CMSIS]`.
* **Tamaño y recomendación.** Es un puerto **medio-grande**: más que el F446,
  menos que el E4 con su T7. El plan (§8) tiene diez fases, S0-S9, con su banco
  y su cierre cada una. **S0-S4 —chip, descarga, periféricos de aula y los
  sensores de la placa— ya son un resultado completo**: la Discovery entera
  menos lo analógico y el USB. Y conviene hacer **S3 pensando en el F0 y el
  G4**: es lo que más se reutilizará después.

---

## 1. Lo que hay hoy, visto desde un STM32 de segunda generación

### 1.1 Lo que ya no estorba

La T0 del plan de los MSP432 separó lo que es de ARM de lo que es de ST, y el F303
cae del lado bueno de casi todas esas líneas:

| Lo que hizo T0 | Para qué le sirve al F303 |
| :--- | :--- |
| `McuDesc` con un descriptor **propio** por familia (`soc/mcu_desc.h`) | El F303 lleva el suyo, `F3Caps`, sin rellenar un `McuCaps` del F4 con ceros |
| `REGISTRA_MCU(FAMILIA, …)` y la factoría | `REGISTRA_MCU(STM32F3, …)`: `--mcu STM32F303VC` y `<mcu tipo="STM32F303VC">` |
| `programador_nv_if` fuera de `GdbRsp` | El `load` de GDB con la secuencia del controlador de Flash del F1 |
| ELF, `.bin` e Intel HEX en el cargador | El `.elf` de CubeIDE, tal cual |
| La ventana del PPB de fabricante | No hace falta: el F303 tiene su DBGMCU donde el F4 (`0xE004_2000`) `[CMSIS]` |
| El alias de arranque como dato (`alias_arranque`) | El F303 **sí** lo usa: BOOT0 + nBOOT1 y `SYSCFG_CFGR1.MEM_MODE` |
| El catálogo de piezas, `<sistema>`, `Conector`, la ilustración | La Discovery, sus conectores P1/P2 y un sistema de dos placas |

### 1.2 Lo que da por hecho un STM32F4 dentro de lo que es «de ST»

Este inventario sale de leer el código. Ninguno es grande; casi todos son el
mismo tipo de trabajo: **un rasgo más en un `*Caps`** que el F4 pone a `false`.

| Sitio | Qué supone | Por qué estorba al F303 |
| :--- | :--- | :--- |
| `core/cortex_m4f.h`, `bt_ibus()` | Toda búsqueda de instrucción por debajo de `0x2000_0000` va al ICode de la matriz; la CCM solo se alcanza por el camino de **datos** (`route_data`) | En el F303 la CCM de 8 KB (`0x1000_0000`) **está en el bus de instrucciones** y se puede ejecutar de ella, que es para lo que ST la pone [1F: RM0316 §3.3]. Hoy, saltar a una rutina en la CCM daría un fallo de bus. Hace falta un rasgo `ccm_ejecutable` en `MapaRam` |
| `periph/gpio_port.h` | El banco del F4: `MODER`…`AFRH` | El F303 añade **`BRR`** en `0x28` `[CMSIS]`, y LL y HAL lo usan para poner pines a cero |
| `periph/exti.h` | Una sola tanda de registros (líneas 0-22 en el F407) | El F303 tiene **36 líneas** en dos tandas (`IMR2`…`PR2`, líneas 32-35) `[CMSIS]` |
| `periph/timers.h`, `TimCaps` | Hasta 4 canales, `OCxM` de 3 bits, un solo *break* | TIM1 y TIM8 tienen **CCR5 y CCR6**, `OCxM[3]` (PWM asimétrico y combinado, OPM redisparable), **BK2**, `MMS2`, `UIFREMAP` y `SMS[3]` `[CMSIS]`. TIM15/16/17 son tipos nuevos (1-2 canales con salida complementaria y BDTR) |
| `periph/watchdog.h`, `Iwdg` | Sin ventana | `IWDG_WINR` y `SR.WVU` `[CMSIS]` |
| `periph/crc.h` | El CRC-32 fijo del F4 | El F303 tiene **polinomio programable** (`POL`, `POLYSIZE` de 7/8/16/32), `INIT` y `REV_IN`/`REV_OUT` `[CMSIS]` |
| `periph/rtc.h` | 20 registros de respaldo y la calibración gruesa `CALIBR` | 16 registros y sin `CALIBR` `[CMSIS]`. La IP es `rtc2_v2_4` frente a `rtc2_v2_3` `[PINDATA]` |
| `periph/can.h`, `CanCaps` | Dos bxCAN que se reparten 28 bancos de filtro | **Un solo** bxCAN; el número de bancos hay que contrastarlo [1F: 14, RM0316] |
| `top/mcu_caps.h`, `McuCaps` | El descriptor del F4: `ArbolReloj`, `Periferia`, `Conectividad` de ocho maestros | Se queda como está. El F303 lleva **su** descriptor |
| `bus/ahb_types.h`, `BusMaster` y `BusSlaveId` | Los ocho maestros y siete esclavos del F4 | El F303 tiene cinco maestros (I, D, S, DMA1, DMA2) y otros esclavos (Flash, SRAM, CCM por I/D, AHB1, AHB2 con los GPIO, **AHB3 con los ADC**) [1F: RM0316 §2.1]. La matriz se puede reaprovechar con su tabla, como hizo el F446; o el `SocF3` usa decodificadores sin matriz, como los MSP432 (§3.4) |
| `rcc/rcc.h` | El RCC del F4: `PLLCFGR` (M/N/P/Q), `AHB1ENR`… | El del F3 es el del F1 ampliado: `CFGR.PLLMUL`, `CFGR2` (PREDIV y relojes de los ADC) y `CFGR3` (relojes de USART, I²C y TIM1/8) `[CMSIS]`. Modelo nuevo; los osciladores (`osc_pll.h`) valen |
| `mem/flash_if.h` | El controlador de Flash del F4: sectores, `PSIZE`, `OPTCR` | El del F1: **páginas de 2 KB, programación por medias palabras, `AR`, `OBR`, `WRPR`** `[CMSIS] [HAL]` |
| `pins/power_pads.h` y el encapsulado | Los pads del F4: VCAP_1/2, PH0/PH1 | En el LQFP100 del F303, **92 de 100 posiciones son las mismas que en el F407**; las ocho que cambian son justo esas: no hay VCAP, y `OSC_IN`/`OSC_OUT` son **PF0/PF1**, no PH0/PH1 `[PINDATA]` |

---

## 2. El STM32F303VC frente al STM32F407VG

### 2.1 Las dos piezas, de un vistazo

| | STM32F407VG | **STM32F303VC** |
| :--- | :--- | :--- |
| **Núcleo** | Cortex-M4F, 168 MHz | Cortex-M4F, **72 MHz** `[PINDATA]` |
| **NVIC** | 82 posiciones, 4 bits | **82 posiciones** (0-81), 4 bits, MPU, FPU `[CMSIS]` |
| **Flash** | 1 MB, sectores de 16-128 KB | **256 KB** en `0x0800_0000`, **páginas de 2 KB**, programación por **medias palabras** `[HAL] [stm32flash]` |
| **Latencia** | 0-7 estados según tensión | **0, 1 o 2** (hasta 24, 48 y 72 MHz), con *prefetch* y acceso de medio ciclo (`HLFCYA`) `[CMSIS] [HAL]` [1F: las fronteras] |
| **SRAM** | 112 + 16 KB, y 64 KB de CCM solo de datos | **40 KB** en `0x2000_0000` y **8 KB de CCM** en `0x1000_0000`, **ejecutable** y con protección de escritura por página (`SYSCFG_RCR`) `[CMSIS]` |
| **Memoria de sistema** | 30 KB, cargador de ST | **8 KB en `0x1FFF_D800`**; bytes de opción en `0x1FFF_F800` `[stm32flash]` |
| **Datos de fábrica** | UID y F_SIZE | UID en `0x1FFF_F7AC`, **F_SIZE en `0x1FFF_F7CC`**, y la **calibración del sensor de temperatura y de VREFINT** en `0x1FFF_F7B8`/`7C2`/`7BA` `[CMSIS] [HAL]` |
| **Identificación** | `DBGMCU_IDCODE` DEV_ID `0x413` | DEV_ID **`0x422`** (el mismo *die* que F302xB/C y F358) `[stm32flash] [OCD]` |
| **Reloj** | HSI 16 MHz, PLL M/N/P/Q, 168 MHz | **HSI de 8 MHz, LSI de 40 kHz** `[HAL]`; PLL ×2…×16 desde HSI/2 o HSE/PREDIV; APB1 ≤ 36 MHz; **ADC12 y ADC34 con su preescalador del PLL**; TIM1/TIM8 hasta 144 MHz (PLL×2); USART e I²C con selector de reloj [1F: RM0316 §9] |
| **Buses** | AHB1 con los GPIO en `0x4002_0000` | **GPIO en AHB2 (`0x4800_0000`)**, **ADC en AHB3 (`0x5000_0000`)**, DMA/RCC/Flash/CRC/TSC en AHB1 (`0x4002_0000`…) `[CMSIS]`. Los GPIO quedan fuera de la región de *bit-band* de periféricos |
| **Pines** | Puertos A-E en el LQFP100, AF0-15 | **A-F, 87 E/S** (PF0, 1, 2, 4, 6, 9 y 10) `[PINDATA]`, AF0-15 con **otra tabla** (§2.4); pines **TT/TTa** (no tolerantes a 5 V) junto a los FT [1F: DS9118] |
| **Analógico** | 3 ADC de 12 bits, 2 DAC | **4 ADC de 12 bits** (5 Msps, diferenciales, calibración), **DAC1 de 2 canales**, **7 comparadores**, **4 amplificadores operacionales** `[PINDATA]` [1F: cifras] |
| **Comunicaciones** | USART1-3/6, UART4/5, SPI1-3, I²C1-3, 2 CAN, OTG FS/HS, ETH | USART1-3, UART4/5 (**v2**), SPI1-3 (con I²S en 2 y 3, **FIFO**), **I²C1-2 (v2)**, **1 CAN**, **USB FS de dispositivo** `[PINDATA]` |
| **Temporizadores** | 14 | TIM1/8 (avanzados, 6 canales), TIM2 (32 bits), TIM3/4, TIM6/7, **TIM15/16/17**; IRTIM `[PINDATA] [CMSIS]` |
| **Otros** | RNG, criptografía, SDIO, FSMC, DCMI | **TSC** (táctil), CRC programable, `FPU_IRQn` (81) con la máscara en `SYSCFG_CFGR1.FPU_IE` `[CMSIS]` |

### 2.2 Los registros, estructura a estructura

Comparadas por programa las `typedef struct` de `stm32f407xx.h` (la vendida en
`src/verif/fw/cmsis/`) y de `stm32f303xc.h` —nombres, orden y huecos—:

| Resultado | Bloques |
| :--- | :--- |
| **Idénticos** | `CAN` (y sus tres subestructuras), `DAC`, `DBGMCU`, `PWR`, **`SPI`**, `WWDG` |
| **Distintos, con registros añadidos** | `GPIO` (+`BRR`), `EXTI` (+la segunda tanda), `TIM` (+`CCMR3`, `CCR5`, `CCR6`), `IWDG` (+`WINR`), `CRC` (+`INIT`, `POL`), `RTC` (−`CALIBR`, −`BKP16R`…`BKP19R`) |
| **Distintos de raíz** | `ADC` y `ADC_Common`, `DMA` (de *streams* a canales), `FLASH`, `I2C`, `RCC`, `SYSCFG`, `USART` |
| **Solo en el F303** | `COMP`, `OPAMP`, `TSC`, `USB` (FSDEV), `DMA_Channel`, `OB` (bytes de opción mapeados) |
| **Solo en el F407** | `DCMI`, `ETH`, `FSMC`, `RNG`, `SDIO`, `USB_OTG_*` |

Dos lecturas. La buena: los bloques «con registros añadidos» son extensiones
de lo que hay, y el modelo ya está hecho para extenderse por rasgos. La que obliga
a ir con cuidado: **`SPI` sale idéntico y no lo es** (trampa 1 de §0). Una
estructura igual no prueba que la IP sea la misma; lo prueba su versión (§2.3).
Y `PWR` también sale igual con otros bits dentro (el F303 no tiene `VOS` ni
*over-drive*): se reutiliza el bloque, no sus máscaras.

### 2.3 La versión de cada IP, según la base de pines de ST

`[PINDATA]` da a cada instancia una versión de IP, que es lo más cercano a «es el
mismo diseño» que publica ST:

| Bloque | F407 | F446 | **F303** | Veredicto |
| :--- | :--- | :--- | :--- | :--- |
| bxCAN | `bxcan1_v1_1` | `bxcan1_v1_1` | `bxcan1_v1_1` | **El mismo**: `periph/can.h` |
| WWDG | `wwdg1_v1_0` | `wwdg1_v1_0` | `wwdg1_v1_0` | **El mismo** |
| **I²C** | `i2c1_v1_5` | `i2c1_v1_5` (+ **FMPI2C1 `i2c2_v1_1`**) | **`i2c2_v1_1`** | **El `FMPI2C1` del F446**: `periph/fmpi2c.h` |
| TIM | `gptimer2_v2_x` | `gptimer2_v2_x` | `gptimer2_v3_x` | Extensión por rasgos de `timers.h` |
| SPI/I²S | `spi2s1_v2_2` | `spi2s1_v2_2` | `spi2s1_v3_0` | Extensión **profunda** de `spi.h` (FIFO, `DS`) |
| USART | `sci2_v1_2` | `sci2_v1_2` | `sci2_v2_1` | **Nuevo** |
| IWDG | `iwdg1_v1_1` | `iwdg1_v1_1` | `iwdg1_v2_0` | Extensión (ventana) |
| CRC | `integtest1_v1_0` | `integtest1_v1_0` | `integtest1_v2_0` | Extensión (polinomio) |
| RTC | `rtc2_v2_3` | `rtc2_v2_3` | `rtc2_v2_4` | Reutilizable con rasgos |
| DAC | `dacif_v1_1` | `dacif_v1_1` | `F3_dacif_v1_1` | Reutilizable; cambian los disparos |
| ADC | `aditf2_v1_1` | `aditf2_v1_1` | **`aditf5_v1_1`** | **Nuevo** |
| DMA | `STM32F417_dma_v2_0` | `STM32F446_dma_v2_0` | **`STM32F303_dma_v1_0`** | **Nuevo** (por canales) |
| RCC | `STM32F417-rcc` | `STM32F446-rcc` | **`STM32F303_rcc`** | **Nuevo** |
| USB | `otgfs1`/`otghs1` | ídem | **`usb1_v2_0_STM32F3xx`** | **Nuevo** (FSDEV) |
| COMP, OPAMP, TSC | — | — | `comp_v1_3_d422`, `tsmc018_ull_opamp_v1_0`, `tsc1_v1_0` | **Nuevos** |

### 2.4 Las funciones alternativas, comparadas

Comparadas por programa las tablas de AF de la base de pines
(`GPIO-STM32F417_gpio_v1_0_Modes.xml` frente a `GPIO-STM32F303_gpio_v1_0_Modes.xml`):

* el F407 tiene 431 pares (pin, señal) y el F303, 399;
* **148** pares están en los dos; **39** de ellos con **otro número de AF**:
  22 de TIM1 (AF1 en el F4; AF2, AF4, AF6 o AF11 en el F303), 9 de TIM8 (AF3
  frente a AF4 o AF10), 2 de UART4 y 2 de UART5 (AF8 frente a AF5), y 4 del I²S
  (`I2S2_MCK` y los datos de los bloques de extensión);
* lo que solo tiene el F303: 82 `EVENTOUT`, 34 del TSC, y TIM15/16/17,
  comparadores, IR, USB y CAN en sitios nuevos.

O sea: **no hay nada que copiar de `soc/f4_mapa_af.h`** y sí mucho que generar.
El E4 ya tiene el patrón (`soc/msp432e4/gen_pines.py` → `e4_pines.inc`): un
guion lee la base de pines y escribe la tabla, y la tabla se versiona.

### 2.5 Cinco consecuencias para el modelo

1. **El reloj tiene precondiciones que el silicio no comprueba**, igual que en
   el F4 y el MSP432: 72 MHz con 0 o 1 estados de espera, APB1 por encima de
   36 MHz, el USB sin sus 48 MHz exactos o un ADC por encima de su reloj
   máximo dan errores silenciosos en la placa. El modelo **avisa con nombre y
   apellidos**, como ya hace el RCC del F4.
2. **El ADC tiene un ritual de arranque**: regulador (`ADVREGEN` 00 → 01 y
   esperar 10 µs `[HAL]`), calibración (`ADCAL`, con `CALFACT` distinto para
   modo simple y diferencial) y `ADEN` hasta `ADRDY`. HAL lo hace siempre; un
   firmware a mano que se salte un paso **lee basura en el silicio**. El modelo
   debe reproducirlo, o al menos avisar.
3. **La calibración de fábrica se lee de la memoria de sistema.** Un firmware
   que convierte la temperatura con `TS_CAL1`/`TS_CAL2` necesita valores
   plausibles en `0x1FFF_F7B8` y `0x1FFF_F7C2`, como el MSP432 necesitaba su
   TLV. Se rellenan con valores típicos y se dice.
4. **Hay bloques que se comparten**: la memoria de paquetes del USB con el CAN,
   los vectores 19 y 20 entre USB y CAN, la 24-26 entre TIM1 y TIM15/16/17, la
   54 entre TIM6 y el DAC `[CMSIS]`. Es el mismo patrón de puertas `Or2` que ya
   tiene `SocF4`.
5. **Un mismo pin puede tener dos tolerancias.** El F303 mezcla pines FT
   (5 V) con TT y TTa (solo 3,6 V) [1F: DS9118]. El pad eléctrico del modelo
   puede avisar si una placa pone 5 V en un pin TT: es el error más caro que un
   alumno puede cometer con este chip.

---

## 3. Qué se aprovecha del código que ya hay

### 3.1 Tal cual, sin tocar una línea

| Qué | Dónde | Para qué en el F303 |
| :--- | :--- | :--- |
| **El núcleo** y el SCS | `core/cpu*.h`, `fpu.h`, `scs.h`, `core_caps.h` | Igual, con `n_irq = 82` y 4 bits |
| **SW-DP, AHB-AP, FPB, DWT, ITM, TPIU**, DBGMCU | `core/debug.h` | Igual. El DBGMCU con DEV_ID `0x422` y su tabla de congelación del F3 |
| Stubs de GDB y sonda SWD | `core/gdb_stub_dap.h`, `verif/gdb_stub.h`, `verif/swd_port.h` | G1 y G2 (§4) |
| Sondas de red | `verif/sonda_openocd.h`, `verif/sonda_cmsis_dap.h` | G3 y D3 con `target/stm32f3x.cfg` |
| Bit-banding | `bus/bitband.h` | SRAM y periféricos de `0x4000_0000` (los GPIO, en AHB2, quedan fuera, como en el silicio) |
| **El alias de arranque** | `core/cortex_m4f.h`, `apply_boot_alias` | BOOT0, nBOOT1 y `MEM_MODE`: Flash, memoria de sistema o SRAM en `0x0` |
| Osciladores, cristal frente a *bypass*, medida de frecuencia | `rcc/osc_pll.h` | HSE desde el MCO del ST-LINK, LSE, HSI, LSI |
| **I²C de segunda generación** | `periph/fmpi2c.h` | I2C1 e I2C2, con el reloj que dé `CFGR3` |
| bxCAN y el motor CAN | `periph/can.h`, `can_motor.h` | El CAN, con un solo controlador |
| Netlist, XML, sistemas, piezas, puente serie, GUI, red, CI | `parts/`, `common/`, `top/sim_main.cpp`, `ci/` | La Discovery y su sistema de dos placas |
| `Led`, `Button`, `ExtClock`, `Crystal`, `Jumper`, `Rpull`, `Conector`, `PuenteSerie`, `UsbHostRig`, `CanTransceiver` | `parts/ext_parts.h` y vecinos | Todo lo que hay en la placa menos los sensores |
| El puente USB de clase (U2, CDC → RFC 2217) | `parts/puente_usb.h`, `usb_dev_if` | El CDC por el USB de usuario, en cuanto el FSDEV implemente `usb_dev_if` |
| CoreMark | `verif/fw/coremark/` | Otro `core_portme` |
| Huellas, ASan, guarda de *tick*, invariantes | `common/huella_fw.h`, `verif/invariantes.txt` | El banco `testf3` |

### 3.2 Con una generalización pequeña (fase S0)

**Ninguna mueve un invariante.** Todas son un rasgo nuevo, a `false` en los F4:

| Qué | Cambio |
| :--- | :--- |
| `MapaRam` y `CoreRouter` | `ccm_ejecutable`: la búsqueda de instrucción en la ventana de la CCM va al puerto `ccm`, no al ICode |
| `GpioPort` | `brr` (registro en `0x28`) |
| `Exti` | `n_lineas` hasta 64 y la segunda tanda de registros (`IMR2`…`PR2`) |
| `TimCaps` | `channels` hasta 6 (CCR5/6 internos, `CCMR3`), `ocm_4bits`, `break2`, `mms2`, `uif_remap`, `sms_4bits`; tres variantes nuevas (TIM15, TIM16/17) |
| `Iwdg` | `ventana` (`WINR`, `WVU`) |
| `CrcUnit` | `polinomio_programable` (`POL`, `POLYSIZE`, `INIT`, `REV_IN`/`REV_OUT`, escrituras de 8/16/32 bits). El CCM del E4 ya calcula cuatro polinomios (`periph/msp432e4/ccm.h`): de ahí sale el algoritmo |
| `Rtc` | `calibr_grueso` y `n_respaldo` |
| `CanCaps` | `n_bancos_filtro` y un único controlador maestro |
| `DacCaps` | La tabla de disparos (`TSEL`) como dato |
| DBGMCU | DEV_ID y bits de congelación (`APB1_FZ`/`APB2_FZ`) como dato del descriptor |
| `spi.h` | ★ El rasgo `fifo_v3`: `DS` en `CR2`, `CRCL` en lugar de `DFF`, FIFO de 4 bytes con `FRXTH`/`FRLVL`/`FTLVL`, **empaquetado por anchura de acceso a `DR`**, `NSSP`, `LDMA_TX/RX`. Es la extensión más grande de la lista y la que más fácil sale mal (§9, riesgo 5) |

### 3.3 Lo nuevo, que conviene escribir pensando en otras familias

| Módulo propuesto | Qué modela | Quién más lo lleva | Patrón o pieza cercana |
| :--- | :--- | :--- | :--- |
| `periph/usart_v2.h` | `ISR`/`ICR`, `RDR`/`TDR`, `OVER8`, *auto-baud*, `RTOR`, carácter de dirección, `SWAP`, inversiones, `MSBFIRST`, `DE` para RS-485, despertar de Stop | F0, F7, L0, L4, G0, G4… | `usart.h` (el motor de línea, que hay que **sacar** a una pieza común) y `motor_uart.h` |
| `periph/dma_canal.h` | DMA por canales: `CCR`, `CNDTR`, `CPAR`, `CMAR`, `ISR`/`IFCR`, prioridades, circular, memoria a memoria; **peticiones fijas por canal** | F0, F1, L0, L4 (con `CSELR`), G4 (con DMAMUX) | `dma.h` del F4 (la arbitración y el lado del bus) |
| `periph/usb_fs.h` | FSDEV: `EPnR` con sus bits que **cambian al escribir un 1** (`STAT_TX/RX`, `DTOG`) y **se borran al escribir un 0** (`CTR`), `CNTR`, `ISTR`, `FNR`, `DADDR`, `BTABLE`, memoria de paquetes de 512 bytes con **acceso 2×16** `[HAL]` | F0, F1, L0, L4, G4 | `usb_dev_if` (la frontera del E4 y del OTG) |
| `periph/adc_v3.h` | Regulador, calibración, simple/diferencial, `SMPR` de 1,5 a 601,5 ciclos, 16 regulares, inyectados con cola de contextos (`JQM`), `OFR1-4`, tres perros (AWD1-3), modo dual por pares, DMA, entradas internas (temperatura, VBAT/2, VREFINT, salidas de los OPAMP) | L4, G4 (casi el mismo) | `adc.h` del F4 para muestrear los nodos analógicos y el tiempo de conversión |
| `periph/comp.h`, `periph/opamp.h` | 7 comparadores (entradas de pin, DAC, fracciones de VREFINT; salidas a pines, *break* de TIM, `OCREF_CLR`, EXTI) y 4 amplificadores (seguidor, PGA ×2/4/8/16, calibración) | F3, G4, L4 | `comp_e.h` del P4 |
| `rcc/rcc_f3.h` | El RCC del F3, con su árbol entero y los avisos | (el patrón del F0/F1) | `rcc/rcc.h` como patrón; `osc_pll.h` tal cual |
| `mem/flash_f1.h` | Controlador de Flash del F1 (`FPEC`): `ACR`, llaves, `PG`/`PER`/`MER`/`OPTPG`/`OPTER`/`STRT`, `AR`, bytes de opción con `OBL_LAUNCH`, *prefetch* | F0, F1, F3 | `periph/msp432e4/flash.h` (prebúsqueda por líneas) y `flash_if.h` |
| `soc/stm32f3/syscfg_f3.h` | `CFGR1` (remapeos de DMA, `USB_IT_RMP`, Fm+ por pin, `FPU_IE`, `MEM_MODE`), `EXTICR1-4`, `RCR`, `CFGR2` | — (cada familia el suyo) | `syscfg.h` para `EXTICR` |
| `periph/bloque_declarado.h` (uso) | El **TSC** y los OPAMP en su calibración fina | — | Ya existe |

### 3.4 El SoC: por qué una clase nueva y no una derivada de `SocF4`

El F446 entró como `class Stm32F446 : public SocF4` porque **era el mismo die
con otro árbol de reloj**, y `soc/stm32f446.h` lo argumenta. El F303 no cumple
esa condición: cambian el RCC, el controlador de Flash, el DMA, los buses (GPIO
en AHB2, ADC en AHB3), la USART, el ADC y la mitad del SYSCFG. Heredar de
`SocF4` obligaría a **tapar** más de lo que se aprovecha.

Lo que se propone es `soc/stm32f3/stm32f303.h` con `class SocF3`, construido
como los MSP432 —un SoC propio que instancia bloques de `periph/` con sus
rasgos— y su descriptor `F3Caps` en `soc/stm32f3/f3_desc.h` con lo que el
catálogo necesita (nombre, familia `STM32F3`, encapsulado, Flash y SRAM, DEV_ID,
qué bloques lleva cada referencia). La pregunta abierta de diseño es **la
matriz** (§9, riesgo 6): reutilizar `AhbMatrix` con una tabla de cinco
maestros, como el F446 hizo con siete, o decodificadores directos, como los
MSP432. La primera conserva la contención entre CPU y DMA que el modelo del F4
ya mide.

---

## 4. Descarga y depuración

### 4.1 Lo que usa el alumno con una Discovery de verdad

STM32CubeIDE con el ST-LINK de la placa (servidor GDB de ST), STM32CubeProgrammer
para grabar, a veces OpenOCD (`board/stm32f3discovery.cfg`) y, en clase, el
puerto COM virtual del ST-LINK/V2-B. El cargador de sistema por USART o por USB
DFU lo usa poca gente con esta placa, porque el ST-LINK ya está ahí.

### 4.2 Las vías

| Vía | Herramienta | Qué ejercita del modelo | Coste | Fase |
| :--- | :--- | :--- | :--- | :--- |
| **D1** carga directa | `mcu-sim placa.xml fw.elf` | Cargador (ya sabe ELF) y la base de la Flash del descriptor | Nulo | S1 |
| **D2** `load` de GDB | `arm-none-eabi-gdb`, CubeIDE (*GDB Hardware Debugging*) | **`programador_nv_if` del FPEC**: llaves, borrado de páginas de 2 KB, medias palabras | Pequeño | S2 |
| **D3** OpenOCD | `openocd -f interface/cmsis-dap.cfg -c "cmsis-dap backend tcp" -f target/stm32f3x.cfg` (o `remote_bitbang`) | El controlador `stm32f1x` de OpenOCD `[OCD]`: **su cargador corre en el núcleo simulado** y escribe el FPEC, como el de `msp432` en el P4 | Pequeño, una vez hecho D2 | S2 |
| **D4** cargador de sistema | `stm32flash` por el `PuenteSerie` (RFC 2217) | **Un cargador propio** en `0x1FFF_D800` con el protocolo AN3155 (documentado), porque la imagen de ST no se puede distribuir | Medio | S7: **hecho** (`make f3-stm32flash`) |
| **D5** USB DFU | `dfu-util` | El FSDEV y un DFU propio | Medio-grande | Fuera del plan |
| **G1** `--gdb-dap` | GDB | Nada nuevo | Nulo | S1 |
| **G2** sonda SWD por los pines (PA13/PA14) | GDB | Nada nuevo | Nulo | S1 |
| **G3** OpenOCD por `remote_bitbang` o CMSIS-DAP | GDB por OpenOCD, CubeIDE, VS Code + Cortex-Debug | DEV_ID, `F_SIZE`, congelación del DBGMCU (`stm32f3x_default_examine_end` escribe `0xE004_2004` y `0xE004_2008` `[OCD]`) | Nulo | S2 |

Lo que **no** se puede ofrecer barato es el ST-LINK de verdad por USB: CubeIDE
con su servidor GDB de ST y CubeProgrammer hablan con un ST-LINK físico. El
camino desde CubeIDE es el de hoy, `cubeide/simulador.launch`, con un hermano
para el F3 (la SVD del F303 para la vista de registros, si su licencia lo deja;
§9, riesgo 3). Y VS Code + Cortex-Debug con OpenOCD, como en
`src/ide/msp432/`.

---

## 5. El sistema de pruebas: la STM32F3DISCOVERY

### 5.1 Lo que hay en la placa

| Qué | Dónde | Fuente |
| :--- | :--- | :--- |
| STM32F303VCT6, LQFP100 | — | `[UM1570] [ST-web]` |
| **LD1** rojo, 3,3 V; **LD2** rojo/verde, comunicación del ST-LINK | Del lado del ST-LINK | `[ST-web]` |
| **LD3-LD10**, la rosa de los vientos: LD3 PE9 rojo, LD4 PE8 azul, LD5 PE10 naranja, LD6 PE15 verde, LD7 PE11 verde, LD8 PE14 naranja, LD9 PE12 azul, LD10 PE13 rojo | Entre B1 y B2 | `[BSP] [UM1570]`; colores también en `[ST-web]` |
| Resistencias de los LED: 510 Ω (dos) y 680 Ω (seis) | — | `[UM1570]`, **sin poder casar cuál con cuál** [1F] |
| **B1** azul, usuario, en **PA0** por SB20, **a VDD** (activo a nivel alto) con 220 kΩ y 100 nF | — | `[UM1570]` (valores) y `[BSP]` (`GPIO_MODE_IT_RISING`, sin pull); el circuito exacto, del esquemático [1F] |
| **B2** negro, reset, en **NRST** por SB23 | — | `[UM1570]` |
| **8 MHz en PF0-OSC_IN desde el MCO del ST-LINK** (SB12 cerrado); X2 sin montar | — | `[UM1570]` |
| **X3** (32,768 kHz) **sin montar** | — | `[UM1570]` |
| **BOOT0** a masa por 510 Ω (SB19) | — | `[UM1570]` |
| **JP3** (IDD): con el puente, el MCU alimentado; sin él, un amperímetro | — | `[UM1570]` |
| **ST-LINK/V2** en las revisiones A y B; **ST-LINK/V2-B** desde la C, con **puerto COM virtual** | — | `[ST-web]`, DB1739 |
| **VCP en USART1: PC4 (TX) y PC5 (RX)**, por SB13/SB15 | — | `[UM1570]` (tabla 6 y esquemático) y Zephyr (consola en PC4/PC5) |
| **Giróscopo** en **SPI1**: SCK PA5, MISO PA6, MOSI PA7, **CS PE3**, INT1 **PE0**, INT2/DRDY **PE1** | U5 | `[BSP] [UM1570]` |
| **Brújula y acelerómetro** en **I2C1**: SCL **PB6**, SDA **PB7**, DRDY **PE2**, INT1 **PE4**, INT2 **PE5**; direcciones `0x32` (acelerómetro) y `0x3C` (magnetómetro), en 8 bits | U3 | `[BSP] [UM1570]` |
| **USB USER** (Mini-B) en PA11 (DM) y PA12 (DP), por SB21/SB22, con resistencia de 1,5 kΩ a 3 V en DP | — | `[UM1570]`; que esa resistencia sea el *pull-up* fijo, [1F] |
| **P1 y P2**, dos tiras de 25×2 con todas las E/S, 5 V, 3 V, VDD, GND, NRST, BOOT0 | — | `[UM1570]` tabla 6. **La lectura por máquina perdió las columnas**: hay que transcribirla en S0 con el esquemático como segunda fuente |
| Medidas | 97 × 66 mm | `[UM1570]` figura 10 [1F] |

### 5.2 Las dos revisiones de sensores

| Revisión de la PCB | Giróscopo (U5) | Brújula (U3) | Fuente |
| :--- | :--- | :--- | :--- |
| A-D | **L3GD20**, `WHO_AM_I = 0xD4` (o `0xD5`, «L3GD20_TR») | **LSM303DLHC**: acelerómetro `0x33` en `0x0F` (sin documentar), magnetómetro con `IRA/IRB/IRC` | `[BSP]` y el foro de ST |
| E y posteriores | **I3G4250D**, `WHO_AM_I = 0xD3` | **LSM303AGR**: acelerómetro `0x33`, magnetómetro `0x40` en `0x4F` | `[BSP]`, el foro de ST y Zephyr (que llama a sus mitades LIS2DH y LIS2MDL) |

El BSP prueba primero uno y luego el otro por su `WHO_AM_I`, así que **un
firmware de ST funciona con las dos** sin recompilar. El modelo hace lo mismo:
`<placa ... revision="D">` o `revision="E"` (por omisión, E: es la que se
vende hoy), y el XML monta los sensores de esa revisión. El dibujo los rotula
con una variante (`U5@L3GD20` / `U5@I3G4250D`, §2.6 de `doc/parts.md`).

### 5.3 El XML: `placas/stm32f3discovery.xml`

Sigue el patrón de `placas/discovery_min.xml` (la MB997) y de
`placas/msp_exp432p401r.xml`: comentarios con lo que conviene saber antes de
usarla, cada pieza con su referencia del esquemático, y un nodo por pin que se
usa. Un esbozo de lo que lleva, para fijar nombres —**no es el fichero**:

```xml
<placa nombre="stm32f3discovery" revision="E">
  <mcu tipo="STM32F303VC" id="u0"/>

  <!-- Relojes [UM1570 §6.x]: 8 MHz del MCO del ST-LINK en PF0; X3 sin montar -->
  <componente tipo="ExtClock" id="MCO" hz="8e6"> <pin nombre="out" nodo="PF0"/> </componente>
  <componente tipo="Crystal" id="X3" vdd="3.3" conectada="no"> ... </componente>

  <!-- La rosa: LD3 (N) ... en el sentido de las agujas -->
  <componente tipo="Led" id="LD3" a_vss="si" vf="2.0" r="510"> ... PE9 ... </componente>   <!-- rojo -->
  ... LD4-LD10 ...

  <componente tipo="Button" id="B1" v_cerrado="3.3" r_cerrado="330"> ... PA0 ... </componente>
  <componente tipo="Rpull" id="R4" v="0" r="220000"> ... PA0 ... </componente>
  <componente tipo="Button" id="B2" r_cerrado="10" rebote="no"> ... NRST ... </componente>
  <componente tipo="Rpull" id="R_BOOT0" v="0" r="510"> ... BOOT0 ... </componente>
  <componente tipo="Jumper" id="JP3" ...> ... </componente>

  <!-- El VCP del ST-LINK/V2-B -->
  <componente tipo="PuenteSerie" id="VCP" host="rfc2217:3359" baudios="host" formato="8N1">
    <pin nombre="rx" nodo="PC4"/> <pin nombre="tx" nodo="PC5"/>
  </componente>

  <!-- Los sensores (S4): la misma Postura para los tres -->
  <componente tipo="Postura" id="MOV"/>
  <componente tipo="L3gd20" id="U5" postura="MOV" ejes="..."> SPI1 + PE3, PE0, PE1 </componente>
  <componente tipo="Lsm303dlhc" id="U3" postura="MOV" ejes="..."> I2C1 + PE2, PE4, PE5 </componente>
  <componente tipo="Rpull" ...> los pull-up del I2C </componente>

  <!-- USB USER y los dos conectores de 25x2 -->
  <componente tipo="Conector" id="P1" filas="25" columnas="2"> ... </componente>
  <componente tipo="Conector" id="P2" filas="25" columnas="2"> ... </componente>
</placa>
```

Lo que el fichero tiene que decir en su cabecera, como hace el de la MB997:
**el pulsador azul va a VDD**; **el reloj llega del ST-LINK** y funciona igual
con `RCC_HSE_ON` que con `RCC_HSE_BYPASS` (trampa 2); **X3 no está**, y el
RTC con LSE se cuelga como en la placa; el **VCP solo existe desde la revisión
C** y es la USART1 en PC4/PC5, no la USART2 de las Nucleo; y qué revisión de
sensores lleva.

**Un sistema de dos placas**, `placas/dos_f3discovery.xml`, une dos Discovery
con los hilos que piden los ejemplos *TwoBoards* de ST —UART, SPI e I²C— y su
masa, con un `<hilo>` por cable. Es la mejor prueba del `<sistema>` con dos MCUs
del mismo tipo, y los ejemplos de ST la necesitan tal cual (§7.2).

### 5.4 Las piezas nuevas: los sensores y su `Postura`

| Pieza | Bus | Qué hace | Lo que no se puede olvidar |
| :--- | :--- | :--- | :--- |
| `Postura` | — | La orientación de la placa (alabeo, cabeceo, rumbo), su **velocidad angular** y el campo magnético de la Tierra en el sitio. Mandos continuos; si se mueve un mando, la placa **gira hasta ahí** en un tiempo finito y la velocidad angular sale de ese movimiento | Sin ella, acelerómetro, brújula y giróscopo serían nueve mandos independientes y contarían historias distintas. La demo de ST enciende los LED **mientras la placa se mueve** y los apaga al parar (`[CUBE]`, `readme.txt`): necesita velocidad angular, no solo ángulos |
| `L3gd20` / `I3g4250d` | SPI (4 hilos; 3 hilos e I²C opcionales) | Registros `0x0F`-`0x38`: `CTRL_REG1-5`, `STATUS`, `OUT_X/Y/Z` (auto-incremento con el bit MS de la dirección), FIFO, umbrales e INT1/INT2/DRDY. Fondo de escala 250/500/2000 dps | **Modo 0 y modo 3** (trampa 4). `WHO_AM_I` 0xD4 / 0xD3 |
| `Lsm303dlhc` | I²C, **dos esclavos** (`0x19` y `0x1E`, en 7 bits) | Acelerómetro (`CTRL_REG1-6_A`, `OUT_X/Y/Z_A`, auto-incremento con el bit 7 de la subdirección, INT1/INT2) y magnetómetro (`CRA/CRB/MR_REG_M`, **datos en orden X, Z, Y** y en *big-endian*, `SR_REG_M`, DRDY) `[BSP]` | `0x33` en `0x0F` del acelerómetro (trampa 5); el orden X-Z-Y |
| `Lsm303agr` | I²C, dos esclavos | El de la revisión E: acelerómetro estilo LIS2DH, magnetómetro estilo LIS2MDL (`WHO_AM_I_M = 0x40`, *little-endian*) | Otro mapa del magnetómetro |

**Los ejes, y por qué hacen falta dos fuentes.** Cada sensor va soldado con su
propia orientación respecto a la placa, y la demo de ST decide qué LED encender
según el signo de cada eje. Un eje cambiado de signo hace que la placa
«caiga» hacia el lado equivocado sin ningún error. El atributo `ejes="..."` de
cada sensor (`"x,-y,z"`, por ejemplo) se fija en S0 con **dos fuentes**: el
dibujo de ejes de UM1570 o de las hojas de datos, y lo que hace la demo de ST.

Las cuatro piezas se apoyan en lo que ya hay: `I2cReg16` y la `Opt3001` de
`parts/boosterpack.h` (registro y dirección por I²C), la `SdCard` (esclavo SPI
que conduce MISO), y el patrón de mandos y observables de `part_base.h`.

### 5.5 El SVG: `placas/stm32f3discovery.svg`

Dibujado **por un guion**, `placas/dibuja_st.py`, como hace `dibuja_ti.py` para
las tres placas de TI: se versionan el guion y el SVG. SVG 1.2 Tiny, medidas en
milímetros (97 × 66, a contrastar), un id por pieza viva y por pin de conector,
y lo que ya sabe hacer la ventana:

| Elemento | id | Efecto |
| :--- | :--- | :--- |
| Los ocho LED de la rosa, en círculo alrededor del chip | `LD3`…`LD10` | `brillo` |
| LD1 y LD2 | `LD1`, `LD2` | `brillo` (LD1 sigue a la `Fuente` de 3 V; LD2, decorado) |
| B1 (azul) y B2 (negro) | `B1`, `B2` | `hundido` |
| JP3 | `JP3`, con `JP3@1-2` y `JP3@abierto` | Variante |
| U5 y U3, con el nombre del sensor de la revisión | `U5@L3GD20`, `U5@I3G4250D`, `U3@LSM303DLHC`, `U3@LSM303AGR` | Variante |
| El ST-LINK, con el rótulo del VCP | `VCP#destino` | Rótulo |
| P1 y P2 | `P1.1`…`P2.50` | Pines del conector |
| Los dos Mini-B (ST-LINK y USB USER) | — | Decorado |

El grupo **C19** de `verif/gui/sistema.py` ya comprueba, para las placas de TI,
que cada pieza visible y cada pin tienen su elemento; se extiende a esta placa.

---

## 6. `mcu-sim-gui`

**No hace falta ningún cambio de código para que la Discovery funcione en la
ventana.** La GUI no sabe de familias: el catálogo de MCUs le llega de
`mcu-sim --volcado`, los dibujos le llegan en el saludo, y los mandos y
observables de las piezas nuevas son de los tipos que ya sabe pintar
(`continuo`, `boton`, observables con unidad). Lo que sí hay que hacer:

| Qué | Dónde | Fase |
| :--- | :--- | :--- |
| Prueba cruzada **G8**: la Discovery con su dibujo, sus piezas, sus tipos y los observables de la `Postura` y de los sensores | `tests/prueba_cruzada.cpp` | S1 (sin sensores) y S4 (hecho: los sensores de la revisión E y los tres mandos de la `Postura`) |
| El sistema de dos Discovery en la ilustración, con su `lienzo` y sus `<ruta>` | `placas/dos_f3discovery.xml` | S3 |
| **Opcional:** un mando de **inclinación** —un círculo que se arrastra, como un nivel de burbuja— para las piezas que declaran a la vez `alabeo` y `cabeceo`. Es **solo de la ventana**: agrupa dos mandos `continuo` que ya existen, sin tocar el protocolo | `src/panel.cpp`, `src/ilustracion.cpp` | S8 |
| **Opcional:** la rosa de la brújula: que el dibujo enseñe el rumbo de la `Postura` (efecto `angulo`, que ya existe, sobre una aguja dibujada) | El SVG y su tabla de enlaces | S8 |
| La guía de usuario: la Discovery y los sensores | `doc/guia de usuario.md` | S4 |

---

## 7. Las pruebas

### 7.1 El banco: `testf3`

`top/sc_main_f3.cpp`, con su invariante en `verif/invariantes.txt` y su paso en
`ci/pasa_suites.sh`. Grupos previstos, a la manera de `testp4`/`teste4`:

| Grupo | Qué comprueba | Fase |
| :--- | :--- | :--- |
| F0 | Huellas de los firmwares (`verif/fw/huellas.txt`) | S1 |
| F1-F4 | Arranque desde la Flash, desde la memoria de sistema y desde la SRAM (BOOT0, nBOOT1, `MEM_MODE`); el IDCODE y `F_SIZE`; la CCM ejecutable | S1 |
| F5-F8 | El reloj: HSI, HSE desde el MCO en **los dos modos**, PLL a 72 MHz con los avisos (latencia, APB1, USB), CSS, MCO de salida; SysTick; CoreMark | S1 |
| F9-F11 | GPIO con `BRR`, EXTI de 36 líneas, la rosa de LED y el pulsador | S1 |
| F12-F15 | FPEC: borrado de página, programación de medias palabras, errores (`PGERR`, `WRPRTERR`), bytes de opción; D2 y G1/G2 | S2 |
| F16-F30 | USART v2, SPI v3 (empaquetado por anchura de acceso, FIFO, `DS` de 4 a 16), I²C (con `TIMINGR = 0`), DMA por canales y sus remapeos, TIM v3 (PWM asimétrico y combinado, BK2), IWDG con ventana, WWDG, RTC, CRC programable, bajo consumo | S3 |
| F31-F34 | Los sensores con la `Postura`, en las dos revisiones | S4 (hechos como F54-F58) |
| F35-F42 | ADC (regulador, calibración, diferencial, dual, AWD, temperatura con la calibración de fábrica), DAC, COMP, OPAMP | S5 |
| F43-F46 | USB FS con el anfitrión de banco (`UsbHostRig`), y CAN | S6 |

### 7.2 Los ejemplos de ST, sin modificar

STM32CubeF3 trae para esta placa **39 ejemplos y 3 aplicaciones** con licencia
**BSD-3** —se pueden versionar— y una demostración con licencia **SLA0044**, que
no `[CUBE]`. Se
compilan con el mismo `arm-none-eabi-gcc`, con un `Makefile` propio que toma las
fuentes de HAL, el BSP y los ejemplos (versionados en `verif/fw/stm32f3/sdk/`,
como el SDK de TI en `verif/fw/msp432e4/sdk/`), y sus `.bin` llevan huella.

| Ejemplos | Qué ejercitan | Fase |
| :--- | :--- | :--- |
| `GPIO_IOToggle`, `GPIO_EXTI`, `Cortex/*`, `RCC_ClockConfig` | Esqueleto, reloj, EXTI, MPU | S1 |
| `FLASH_EraseProgram`, `FLASH_WriteProtection` | El FPEC y los bytes de opción | S2 |
| `UART_TwoBoards_*`, `SPI_FullDuplex_*`, `I2C_TwoBoards_*` | Los periféricos de segunda generación, **con dos Discovery** (§5.3) | S3 |
| `TIM_*` (8, incluidos `TIM_Asymetric` y `TIM_Combined`) | TIM v3, que el F4 no tiene | S3 |
| `IWDG_*`, `WWDG_Example`, `RTC_Calendar`, `HAL_TimeBase_*`, `PWR_*` | Perros (con ventana), RTC, bajo consumo | S3 |
| `FreeRTOS_ThreadCreation` | Un RTOS con su *tick* | S3 |
| `Examples/BSP` | **Los sensores de la placa, con el BSP de ST** | S4 |
| La demostración (`Demonstrations/`) | Rosa, giróscopo, acelerómetro y ratón USB: el cierre natural | S4 y S6, **sin versionar** (desde `doc/refs/`, como el BSL de TI) |
| `USB_Device/HID_Standalone`, `DFU_Standalone` | El FSDEV. **El middleware USB de ST es SLA0044**: o se usan sin versionar, o se prueba con TinyUSB (MIT), que soporta el FSDEV del F3 | S6 |

### 7.3 Lo demás

* `make f3-discovery`: la placa con la rosa girando y el pulsador, mirando lo
  que sale (como `make p4-edumkii`).
* ~~`make f3-dos`~~ `make f3-vcp` (S3): el VCP con `f3_vcp`, y el sistema de
  dos Discovery validado y en marcha con `UART_TwoBoards_ComIT`.
* `make openocd` y `make cmsis-dap`: un caso más cada uno, con
  `target/stm32f3x.cfg` (`program … verify reset`).
* `make serie`: el VCP por RFC 2217 (el de la Discovery es el mismo
  `PuenteSerie` que el de la Nucleo; los dos de `dos_f3discovery.xml` se
  reparten el 3355 y el 3356).
* `verif/gui/sistema.py` (C19 y recuento de piezas) y `verif/gui/argumentos.py`
  (el catálogo crece en uno; el recuento ya no está escrito a mano).
* En `mcu-sim-gui`, la prueba cruzada G8.

---

## 8. Plan por fases

El método de siempre: fases pequeñas, cada una con su cierre y su banco, y como
condición de todas, **sin mover un picosegundo los invariantes de `test407`,
`test446`, `test417`, `testserie`, `testgui`, `testp4` y `teste4`**. Las fases se
llaman **S0-S9** para no confundirlas con las T de los MSP432 ni con las X del
XDS110.

### S0 · Fuentes y generalización neutral

**Hecha el 20-10-2026** (arriba, «Estado»). Las fuentes de ST que no se
alcanzan desde aquí (RM0316, DS9118, AN2606) siguen sin estar en `doc/refs/`.

**Qué:**
* las fuentes, en `doc/refs/` (sin versionar si su licencia no lo deja):
  RM0316, DS9118, UM1570 (la revisión actual) y su esquemático, AN2606, la SVD;
* versionadas: `stm32f303xc.h` y el arranque de `cmsis_device_f3` (Apache-2.0),
  la HAL y el BSP con los cuatro controladores de sensores (BSD-3), y los dos XML
  de la base de pines;
* `soc/stm32f3/gen_pines.py`: encapsulado y tabla de AF desde `[PINDATA]`;
* transcribir con dos fuentes lo que solo tiene una: la **tabla 6 de UM1570**
  (P1/P2), la **tabla de peticiones de DMA** del RM0316 (segunda fuente: los
  `dmas` de Zephyr o libopencm3), las **tolerancias FT/TT** del DS9118, los
  **ejes** de los sensores, los bancos de filtro del CAN y las fronteras de
  latencia;
* las generalizaciones de §3.2, todas como rasgos a `false` en el F4.

**Cierre:** las siete suites con las mismas comprobaciones y el mismo tiempo
simulado, en las cuatro plataformas; un banco de rasgos (como T127) que
construye un GPIO con `BRR`, un EXTI de 36 líneas, un TIM con seis canales y un
CRC de 16 bits, y comprueba cada uno.

**Tamaño:** medio. Lo delicado es el rasgo `fifo_v3` del SPI, que toca el
corazón de `spi.h`.

### S1 · El STM32F303VC arranca y gira la rosa

**Hecha el 20-10-2026** (arriba, «Estado»).

**Qué:**
* `F3Caps`, `REGISTRA_MCU(STM32F3, …)`, `STM32F303VC` en el catálogo;
* `SocF3`: núcleo, matriz o decodificadores, Flash de lectura (latencia,
  *prefetch*, `HLFCYA` y el aviso), SRAM, **CCM ejecutable**, memoria de sistema
  con UID, `F_SIZE` y la calibración de fábrica, bytes de opción de lectura;
* `rcc_f3.h` con su árbol entero y sus avisos; PWR básico;
* GPIO A-F en AHB2, EXTI de 36 líneas, `syscfg_f3.h` (`EXTICR`, `MEM_MODE`),
  DBGMCU con `0x422`;
* encapsulado LQFP100 y AF generados;
* **la placa, primera versión**: `stm32f3discovery.xml` sin sensores ni VCP, y
  **su SVG completo** (`dibuja_st.py`), con los sensores dibujados aunque aún no
  estén en el XML;
* `testf3` F0-F11, `make f3-discovery`, prueba cruzada G8 sin sensores.

**Cierre:** `GPIO_IOToggle` y `GPIO_EXTI` de ST **sin modificar** encienden la
rosa y responden a B1; los dos modos del HSE llegan a 72 MHz; CoreMark corre;
la ventana enseña la Discovery con la rosa encendiéndose.

**Tamaño:** grande. Es la fase con más código nuevo de una vez (RCC y SoC).

### S2 · Descarga y depuración

**Hecha el 20-10-2026** (arriba, «Estado»).

**Qué:** `mem/flash_f1.h` (FPEC completo, con bytes de opción y `OBL_LAUNCH`);
su `programador_nv_if`; G1/G2/G3 y D2/D3; `src/ide/stm32f3/` (CubeIDE, VS Code,
la configuración de OpenOCD); RDP declarado.

**Cierre:** `load` de GDB por las dos vías con el mismo resultado; OpenOCD
programa y verifica con `target/stm32f3x.cfg` por CMSIS-DAP y por
`remote_bitbang`, y el firmware nuevo arranca; `FLASH_EraseProgram` de ST pasa.

**Tamaño:** pequeño-medio.

### S3 · Los periféricos de segunda generación

**Qué:** `usart_v2.h` (y el VCP en la placa), SPI v3, I²C (las dos instancias
de `FmpI2c` con su reloj), `dma_canal.h` con su tabla y los remapeos del SYSCFG,
TIM v3 (TIM1/8/2/3/4/6/7/15/16/17 e IRTIM), IWDG con ventana, WWDG, RTC, CRC
programable, Sleep/Stop/Standby; `placas/dos_f3discovery.xml`.

**Cierre:** los ejemplos de ST de §7.2 marcados S3, **sin modificar**, incluidos
los *TwoBoards* sobre el sistema de dos placas; `make serie` con el VCP.

**Tamaño:** grande. **Es la fase que más vale fuera del F303**, y conviene
escribirla para el F0 y el G4.

**Hecha el 20-10-2026** (arriba, «Estado»).

### S4 · Los sensores de la placa

**Qué:** `parts/mems_st.h` con `Postura`, `L3gd20`, `I3g4250d`, `Lsm303dlhc` y
`Lsm303agr`; la placa con los sensores y `revision=`; `doc/parts.md` §4.x;
la G8 con los sensores.

**Hecha el 21-10-2026** (arriba, «Estado»).

**Cierre:** el ejemplo `BSP` de ST, sin modificar, detecta los sensores de **las
dos revisiones** y lee lo que dice la `Postura` dentro de la cuantización de
cada fondo de escala; la demostración de ST, compilada desde `doc/refs/`,
enciende el LED del lado hacia el que se inclina la placa.

**Tamaño:** medio.

### S5 · Lo analógico

**Qué:** `adc_v3.h` (los cuatro, con sus dos bloques comunes), el DAC con los
disparos del F3, `comp.h` y `opamp.h`, el TSC declarado; firmware propio con LL
(ST no trae ejemplos de ADC para esta placa).

**Cierre:** la temperatura interna con `TS_CAL1`/`TS_CAL2` da lo que dice el
banco; una conversión diferencial; dos ADC en modo dual con DMA; un comparador
que dispara el *break* de TIM1; un OPAMP en PGA ×4 visto por el ADC.

**Tamaño:** grande. El ADC solo es tan grande como el de todo el F4.

**Hecha el 24-10-2026** («Estado»): F63-F68 de `testf3`, con `f3_analog`.

### S6 · USB y CAN

**Qué:** `usb_fs.h` sobre `usb_dev_if`; el puente U2 (CDC) y una prueba HID con
`UsbHostRig`; el bxCAN con un solo controlador y la memoria compartida con el
USB; `USB_IT_RMP`.

**Cierre:** un CDC (TinyUSB o el de ST desde `doc/refs/`) enumera y hace eco por
`--usb`; el ratón de la demostración mueve el cursor en el anfitrión de banco;
CAN entre las dos Discovery con transceptores.

**Tamaño:** medio-grande.

### S7 · El cargador de sistema (opcional)

**Qué:** un cargador propio en `0x1FFF_D800` con el protocolo AN3155 por la
USART1 (PA9/PA10) y la USART2, como el cargador R1 del E4; una variante de la
placa con BOOT0 alto (SB19 abierto y la resistencia de 10 kΩ, que viene sin
montar `[UM1570]`).

**Cierre:** `stm32flash`, sin modificar, programa la Flash por el
`PuenteSerie`.

**HECHA el 21-10-2026** («Estado», arriba). La variante de la placa es un
sistema con un puente BOOT0-VDD en P2 (19-20), lo que se hace con una
Discovery de verdad, en vez de desoldar SB19.

**Tamaño:** medio.

### S8 · La GUI y la documentación

**Qué:** lo opcional de §6 (el mando de inclinación, la aguja del rumbo); la guía
de usuario; `doc/parts.md`; la entrada **P-18** de `doc/todo.md` con lo
declarado.

**HECHA el 21-10-2026** («Estado», arriba).

**Tamaño:** pequeño.

### S9 · El resto de la familia

**Qué:** las referencias del mismo *die* `0x422` —F303VB (128 KB/32 KB),
F303CB/CC (LQFP48), F303RB/RC (LQFP64)— son descriptor y encapsulado; las del
F302xB/C y el F358, también, quitando bloques. El **F303xD/E** (`0x446`: 512 KB,
FMC, TIM20, I²C3, memoria de paquetes de 1 KB con acceso 1×16) y el
**F303x6/8/F334** (`0x438`: HRTIM) son **otros dies** y quedan fuera, como el
F446 quedó fuera de la familia del F407 hasta su propio plan `[stm32flash]
[HAL]`.

**Tamaño:** pequeño para el `0x422`; un plan aparte para los otros.

**HECHA el 21-10-2026** para el `0x422` («Estado», arriba); el plan de los otros
dos dies es `doc/analisis_f303xD-E_f303x6_8_F334.md`.

### Orden y dependencias

```
S0 ──► S1 ──► S2 ──► S3 ──┬─► S4 ──► S8
                          ├─► S5
                          ├─► S6
                          └─► S7 (opcional)
S9 en cualquier momento después de S1 (solo el die 0x422)
mcu-sim-gui en paralelo desde S1
```

S4 va justo después de S3 porque los sensores necesitan SPI e I²C, y porque con
S4 la placa queda **completa para un curso** sin lo analógico ni el USB.

---

## 9. Riesgos y preguntas abiertas

1. **Las fuentes primarias no se alcanzan desde este entorno.** El RM0316, el
   DS9118 y la revisión actual de UM1570 están en st.com, y `WebFetch` necesita
   que los pidas tú. Lo que aquí se cita de ellos lleva **[1F]**. S0 empieza por
   tenerlos en `doc/refs/`. **Resuelto (20-10-2026)**: DS9118, UM1570 Rev 7,
   el esquemático y RM0316 Rev 10 están en `doc/refs/stm32f303/`; AN2606 (los
   cargadores) hará falta en S7.
2. **La tabla 6 de UM1570 se leyó con las columnas perdidas.** Los pines de los
   LED, sensores, VCP y USB salen bien porque tienen una segunda fuente (BSP,
   Zephyr), pero P1/P2 hay que transcribirlo con el esquemático. **Resuelto
   (20-10-2026)**: la tabla 7 de la Rev 7 y la hoja 1 del esquemático
   coinciden con el XML.
3. **Licencias.** Ejemplos, HAL y BSP son BSD-3 y CMSIS es Apache-2.0: se
   versionan. **La demostración y el middleware USB son SLA0044**, que limita el
   uso a dispositivos de ST: no se versionan, como el BSL de TI. La SVD y la
   base de pines hay que comprobarlas antes de copiarlas.
4. **Los ejes de los sensores.** Si un signo está mal, la demo «cae» hacia el
   lado equivocado sin ningún error. Dos fuentes, y una prueba con la demo.
5. **El SPI v3 dentro de `spi.h`.** Es la extensión que más fácil puede mover
   un invariante del F4. Si en S0 no sale limpia, la alternativa es
   `periph/spi_v3.h` aparte. Se decide midiendo, no de antemano.
6. **La matriz.** Reutilizar `AhbMatrix` con cinco maestros conserva la
   contención CPU/DMA, pero obliga a nombrar en `BusSlaveId` un esclavo de CCM
   que el F4 no tiene. Los decodificadores directos son más simples y pierden
   esa medida. **Recomendación:** la matriz, para que los tiempos de DMA se
   comparen con los del F4. **Decidido en S3 (20-10-2026)**: decodificadores
   directos («Estado»); ningún ejemplo de ST nota la diferencia.
7. **El coste del ADC.** Cuatro ADC a 5 Msps en continuo con DMA son muchos
   sucesos. Hay que medir el coste de simulación en S5 (`doc/coste_simulacion.md`)
   y, si hace falta, agrupar conversiones como hacen ya los temporizadores.
   **Medido en S5 (24-10-2026)**: F63-F68, 70 ms simulados con el ADC en
   continuo a casi 1 Msps en F65, cuestan 0,6 s de reloj; no ha hecho falta.
8. **Revisión A/B frente a C+.** Sin VCP en las primeras. La placa modela la
   actual (V2-B) y lo dice; la variante sin VCP es quitar una pieza (el
   `PuenteSerie` VCP, desde S3).
9. **`L3GD20_TR` (`0xD5`).** El BSP la acepta y no hay hoja de datos pública. Se
   modela la `0xD4` y se dice.
10. **El nombre del espacio de nombres**, `stm32`, por fin encaja. No hay nada
    que hacer.

---

## 10. Fuentes consultadas

* `STMicroelectronics/cmsis_device_f3`, `stm32f303xc.h` (Apache-2.0).
* `STMicroelectronics/stm32f3xx_hal_driver` (BSD-3): `stm32f3xx_ll_usb.h`
  (`PMA_ACCESS`), `stm32f3xx_hal_flash*.h`, `stm32f3xx_ll_adc.h`.
* `STMicroelectronics/STM32CubeF3`, `Projects/STM32F3-Discovery` (ejemplos BSD-3,
  demostración SLA0044) y su `.gitmodules`.
* `STMicroelectronics/stm32f3discovery-bsp`, `stm32-l3gd20`, `stm32-lsm303dlhc`,
  `stm32-i3g4250d`, `stm32-lsm303agr` (BSD-3); `stm32-mw-usb-device` (SLA0044).
* `STMicroelectronics/STM32_open_pin_data`: `STM32F303V(B-C)Tx.xml`,
  `STM32F407V(E-G)Tx.xml`, `STM32F446R(C-E)Tx.xml`,
  `GPIO-STM32F303_gpio_v1_0_Modes.xml`, `GPIO-STM32F417_gpio_v1_0_Modes.xml`.
* UM1570 Rev 3, *Discovery kit for STM32F303xx microcontrollers* (copia
  pública: git.ondrovo.com, `gex-platform-info/F303_DISCO`).
* *Data briefs* DB1739 Rev 4 (farnell.com) y Rev 5 (components.ru), y la ficha de
  la STM32F3DISCOVERY en st.com.
* Foro de ST, «please document properly revisions of development boards this
  time: stm32f3discovery» (cambios de la revisión D a la E).
* Documentación de Zephyr, placa `stm32f3_disco` (revisiones, consola en PC4/PC5,
  MCO de 8 MHz).
* stm32flash, `dev_table.c` (DEV_ID `0x422`, memoria de sistema, bytes de opción).
* OpenOCD, `target/stm32f3x.cfg` (controlador `stm32f1x`, DBGMCU).
