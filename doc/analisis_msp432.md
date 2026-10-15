# MSP432P4xx y MSP432E4xx en `mcu-sim`

**Qué hay que modificar y añadir, y plan por fases** · 14-10-2026

La pregunta es qué haría falta para que `mcu-sim` simule los dos MCUs de Texas
Instruments con núcleo Cortex-M4F, el **MSP432P4xx** y el **MSP432E4xx**, **igual
que hoy simula los STM32F4**. Eso significa ejecutar el binario que sale de Code
Composer Studio, del SimpleLink SDK o de `arm-none-eabi-gcc` sin recompilarlo,
con pines eléctricos, placas en XML y consola serie, y descargarlo y depurarlo
con las herramientas de siempre.

Se contestan también dos preguntas concretas: **cómo se descarga y cómo se
depura** el firmware (§4 y §5), y **qué parte del código que ya hay sirve**
(§3).

El documento es hermano de `doc/analisis_esp32_riscv.md` y usa su misma
estructura. La diferencia de fondo entre los dos casos explica casi todo lo que
sigue: **en el ESP32 cambia la arquitectura, y aquí solo cambia el
fabricante.**

> **Estado (2026-10-14, rama `msp432`): hechas T0 y T1.** Ningún invariante
> de las cinco suites se movió al hacer T0. El MSP432P401R arranca, cambia de
> reloj, avisa de lo que el silicio no avisa, resetea por sus cuatro caminos y
> corre CoreMark; su banco es `make testp4` (96 comprobaciones,
> `254192855661 ps`). Lo que T1 deja pendiente, con su letra, está en **P-16**
> de `doc/todo.md`.
>
> Dos cosas aprendidas al hacerlo, que el plan no decía: el pulso de reset
> tiene que durar más que el cuanto de la CPU (20 µs, como el RCC de los
> STM32), o los resets que pide el propio firmware se pierden; y el `.out` de
> CCS es un ELF con la ruta absoluta de quien compila en su DWARF, así que su
> huella solo es estable con `-ffile-prefix-map`.
>
> **2026-10-15: hecha T2.** El FLCTL entero (protección, programación inmediata
> y en ráfaga, borrado de sector y masivo, read-while-write entre bancos), su
> `programador_nv_if` para el `load` de GDB, `SYSCTL_PERIHALT_CTL`, y la misma
> sesión de GDB por `depuracion="dap"` y por `"pines"` con los mismos
> resultados. `make testp4`: 148 comprobaciones, `resto` `331699188968 ps`.
> Las configuraciones de CCS y VS Code están en `src/ide/msp432/`, probadas con
> `gdb-multiarch` por consola y por MI pero **no dentro de los IDE**. Y un
> arreglo del stub común: `monitor reset halt` soltaba el núcleo tras el
> *vector catch*.
>
> **2026-10-15: hecha T3**, en nueve entregas: las funciones de periférico en
> los pines y el PMAP; la eUSCI en UART, SPI e I2C; Timer_A y Timer32; ADC14 y
> REF_A con el sensor de temperatura; la uDMA; CRC32, AES256, RTC_C y COMP_E;
> los modos de bajo consumo y el consumo; la BOOSTXL-EDUMKII con cuatro
> piezas nuevas y su sistema de dos placas; y los ejemplos de DriverLib con
> DriverLib de verdad. `make testp4`: 356 comprobaciones, `resto`
> `4941243771818 ps`; `make p4-edumkii`. Lo que queda dicho está en P-16
> (letras p-x) de `doc/todo.md`.

---

## 0. Resumen

* **El núcleo entero vale tal cual.** Thumb-2, FPv4-SP, NVIC, MPU, SysTick,
  excepciones y lockup son los mismos que en el F407. El SW-DP, el AHB-AP, el
  FPB, el DWT y el ITM también son de ARM. Los dos MSP432 implementan **3 bits
  de prioridad**, y el NVIC ya se sabe construir así: `CoreCaps::prio_bits` está
  parametrizado y lo comprueba **T127** con `CORE_M4F_MINIMO`. Los dos stubs de
  GDB, la sonda SWD por los pines, el bit-banding y el *quantum* LT se heredan
  sin escribirlos de nuevo.
* **Todo lo que está fuera del chip también vale.** Netlist, XML de placas y
  sistemas, el catálogo de piezas (incluida la **TFT ST7735**, que es la del
  Educational BoosterPack MKII), el puente serie con RFC 2217, la frontera con
  `mcu-sim-gui`, la red, el freno de tiempo real, la integración continua y sus
  invariantes. Además los firmwares de prueba se compilan con **el mismo
  `arm-none-eabi-gcc`** que ya usa el proyecto. No entra una segunda cadena de
  herramientas, al contrario que con el ESP32.
* **Lo que hay que escribir son los periféricos y el SoC**, y no se parecen a
  los de ST:
  * el **P4** hereda sus bloques del MSP430: eUSCI, Timer_A, ADC14, CS, PCM,
    WDT_A, RTC_C, REF_A, COMP_E, AES256 y CRC32. Lleva además puertos de 8 bits
    con los nombres `P1.0`…`P10.7` y `PJ.x`;
  * el **E4** es un **Tiva C TM4C129 rebautizado** con criptografía. Lleva el
    SYSCTL de Tiva, el GPIO con el dato enmascarado por la dirección, UART y SSI
    de estilo PrimeCell, GPTM, I²C, EEPROM, Ethernet con PHY integrado, USB OTG
    de Mentor, CAN de Bosch (C_CAN) e Hibernation;
  * los dos comparten el **µDMA PL230 de ARM**, que se escribe una vez y vale
    para los dos.
* **Antes hace falta una fase 0 pequeña**, y está **mucho más acotada que la
  E0 del ESP32**. Hay siete sitios donde el código da por hecho un STM32 dentro
  de lo que es «de ARM»:
  * el alias de arranque en `0x0` del `CoreRouter`;
  * la ventana del PPB de fabricante (`0xE004_0000`…), que hoy va siempre al
    bloque de depuración y al DBGMCU de ST, cuando el P4 tiene allí su **SYSCTL**
    (`0xE004_3000`);
  * la ROM table y su JEP106;
  * la **secuencia de programación de la Flash del F407** dentro de
    `GdbRsp`;
  * `ImageLoader(SocF4&)` con `addr::FLASH_BASE = 0x0800_0000`;
  * la fuente de referencia del SysTick;
  * el **analizador de nombres de pin**, que ya acepta `P3.12` pero con otro
    significado (§1.2).

  El criterio de cierre es el de siempre: **ningún invariante se mueve ni un
  picosegundo**.
* **Descarga (§4)**, de menor a mayor fidelidad:
  * **D1**, carga directa (`.bin`, `.hex`, y además **ELF**, que es el `.out`
    de CCS, y **TI-TXT**);
  * **D2**, el `load` de GDB contra el stub interno, con la secuencia del
    FLCTL o de la `FMC` en vez de la del F407;
  * **D3**, **OpenOCD de verdad**, con su controlador de Flash `msp432`, por
    una sonda SWD que habla `remote_bitbang`;
  * **D4**, los cargadores serie de TI (el BSL del P4 y el cargador en ROM del
    E4) por el `PuenteSerie`;
  * **D5**, opcional y tardía: una sonda CMSIS-DAP por USB/IP.
* **Depuración (§5).** Las vías **G1** (`--gdb-dap`) y **G2** (sonda SWD por los
  pines) **son las que ya existen** y funcionan en cuanto el chip esté montado.
  **G3** añade OpenOCD por `remote_bitbang`, y de paso sirve también al STM32.
  La que **no** se puede ofrecer barata es el depurador nativo de CCS, que habla
  con un XDS110 por USB. Desde CCS se depura con el lanzamiento *GDB Hardware
  Debugging*, igual que hoy desde STM32CubeIDE.
* **La ROM importa, pero menos que en el ESP32.** Los ejemplos del SDK llaman a
  la **DriverLib en ROM** con las macros `MAP_`. Esas llamadas pasan por una
  **tabla de punteros en una dirección fija** (`ROM_APITABLE = 0x0200_0800` en
  el P4). Por eso se puede **reconstruir una ROM sustituta compilando la
  DriverLib del SDK**, que tiene licencia BSD-3, con la misma tabla, y
  versionarla. La ROM de verdad, volcada de una placa, queda como opción
  `--rom` y no se versiona (§6).
* **Recomendación:** hacer **primero el MSP432P401R**. Es el de la LaunchPad
  roja, el del TI-RSLK y el que más se ve en las aulas. El **E4** viene
  después, como segunda familia. Y conviene hacer **este puerto antes que el
  del ESP32**: con la mitad del riesgo, valida la fase 0 que los dos comparten,
  porque el núcleo y la depuración ya están probados.
* **Estado comercial, dicho desde el principio.** TI anunció en 2021 que **no
  habrá productos MSP432 nuevos**, y su sucesor de bajo consumo es la familia
  MSPM0, con Cortex-M0+. Las piezas y las LaunchPads siguen a la venta y hay
  muchos laboratorios montados con ellas. Eso justifica el soporte didáctico,
  pero hace poco aconsejable invertir en la cola larga de la familia (§8, T8).
  Una ventaja lateral: **el E4 abre la puerta al TM4C129**, que es casi el
  mismo silicio, y deja cerca el **TM4C123** de la popular EK-TM4C123GXL.

---

## 1. Qué hay hoy, visto desde un Cortex-M4F que no es de ST

### 1.1 Lo que ya es de cualquier Cortex-M4F

`doc/reutilizacion.md` §2 divide el modelo en tres capas: arquitectura,
integración y familia. Un MSP432 **no cambia la primera**, cambia números en la
segunda y cambia entera la tercera. Es exactamente el caso para el que se
diseñaron `CoreCaps`, `MemCaps` y `McuCaps`, y la diferencia con el ESP32 se
nota:

| Capa | ¿Cambia con un MSP432? | Qué se hace |
| :--- | :--- | :--- |
| **Arquitectura** (`core/cpu*.h`, `fpu.h`, `scs.h`, bit-banding, PPB de ARM) | **No** | Nada |
| **Integración** (`CoreCaps`, `MapaFlash`, `MapaRam`, topes de reloj) | Sí, en números | Un descriptor por referencia: 3 bits de prioridad, IRQ, MPU de 8 regiones, Flash en `0x0` |
| **Familia** (`periph/`, `rcc/`, `pins/`, `soc/`) | **Sí, entera** | Modelo nuevo, tras `mcu_if` y con `REGISTRA_MCU(MSP432P4, …)` |

### 1.2 Lo que da por hecho un STM32 dentro de lo que es «de ARM»

Este inventario sale de leer el código y no de buscar «F407». En el ESP32 la
lista es más larga porque también cambia la arquitectura; aquí basta con
**separar lo que ST añadió al núcleo de lo que es de ARM**.

| Sitio | Qué supone | Por qué estorba a un MSP432 |
| :--- | :--- | :--- |
| `core/cortex_m4f.h`, `CoreRouter::remap()` | Todo acceso por debajo de `0x0010_0000` pasa por `apply_boot_alias()` según `boot_mode` (BOOT0/`SYSCFG_MEMRMP`) | En los MSP432 la Flash **está** en `0x0000_0000` y no hay alias. Con el alias puesto, el núcleo leería el vector de reset en `0x0800_0000`, donde no hay nada |
| `core/cortex_m4f.h`, `route_data()` | `is_ppb(a)` y no SCS → `dbg_isk_`: todo el PPB que no es del SCS va al bloque de depuración | El P4 tiene registros **del chip** en el PPB de fabricante: **SYSCTL en `0xE004_3000`** (de ahí lee OpenOCD el tamaño de la Flash, en `0xE004_3020`) y el FLCTL de depuración. Hace falta una ventana del PPB externo que el SoC pueda reclamar |
| `core/debug.h` | `B_DBGMCU = 0xE004_2000` con el `DBGMCU_IDCODE` de ST; la ROM table con los identificadores de ST; ETM declarado | En el P4 no hay DBGMCU. El que un depurador usa para identificar el chip es el **Device Descriptor (TLV)** en `0x0020_100C`; en el E4 son `DID0`/`DID1` en `0x400F_E000`. La congelación de periféricos en las paradas va por SYSCTL (P4) o por cada periférico (E4) |
| `common/gdb_rsp.h` | `FLASH_R = 0x4002_3C00`, llaves `0x4567_0123`/`0xCDEF_89AB`, sectores de 16/64/128 KB del F407 | El P4 programa con el **FLCTL** (protección por banco y sector, programación inmediata o en ráfaga, borrado de sectores de 4 KB). El E4 programa con `FMA`/`FMD`/`FMC`/`FMC2`, con su llave de escritura, y borra sectores de 16 KB |
| `verif/image_loader.h` | `ImageLoader(SocF4&)`, base por omisión `addr::FLASH_BASE` (`0x0800_0000`), `.bin` e Intel HEX | Hacen falta la base del chip (`MapaFlash::base` ya existe), **ELF** (el `.out` de CCS) y **TI-TXT** (lo que sacan `hex470`/`tiobj2bin`, UniFlash y el BSL) |
| `rcc/` + `soc_f4.h`: `systick_ext` = HCLK/8 | La referencia externa del SysTick | Es propia de cada integración. En el E4 es el PIOSC/4. En el P4 hay que comprobarla en el TRM |
| `pins/pin_mux.h`: `pad_desde_nombre`, `puerto_desde`, `N_GPIO_PORTS = 9`, `N_PORT_PINS = 16` | **T125** ya acepta `P3.12`, pero con un significado concreto: *el número de puerto es el índice*, así que `P0.0` es `PA0` y `P1.0` es **`PB0`**. Además las letras van de la A a la I | En el P4, `P1.0` es **el primer puerto**, no hay `P0`, existen `P10.x` y existe `PJ.x`. En el E4 los puertos van de la **A a la T saltándose la I y la O**: hasta 18 puertos. **El nombre lo tiene que interpretar el chip**, no una regla global |
| `pins/power_pads.h` | VDD, VDDA, VBAT, **NRST**, **BOOT0** | Ninguno de los dos tiene BOOT0. El P4 tiene `RSTn/NMI`, varios AVCC/DVCC y DCOR (el condensador del DC-DC). El E4 tiene `RST`, `VBAT` y `HIB`/`WAKE` para el módulo de hibernación |
| `top/mcu_caps.h` | `McuCaps` = `CoreCaps` + `MemCaps` + `ArbolReloj` (PLL de ST) + `Periferia` (ETH, DCMI, CRYP…) + `Conectividad` (matriz 8×7) + `idcode` | El descriptor es de la familia STM32F4, y debe seguir siéndolo. Lo que falla es que `mcu_if::caps()` lo exija. Es el mismo punto que la E0 del ESP32 |

**Ninguno es grande, y casi todos son el mismo trabajo que la E0 del ESP32**
(`mcu_if` sin `McuCaps`, pads con nombre, `PowerPads` por familia, `GdbRsp`
partido, ELF en el cargador). Los dos que solo aparecen aquí son el **alias de
`0x0`** y la **ventana del PPB de fabricante**. Son los que separan «Cortex-M de
ST» de «Cortex-M», y le servirán igual a un nRF52, un LPC o un SAM.

---

## 2. Las dos familias frente al F407

No pretende ser un manual: es la lista de lo que cambia el **diseño**. Los
valores salen de las hojas de datos, de los manuales técnicos y de las
cabeceras de TI (`msp432p401r.h`, `msp432e401y.h`). **Antes de escribirlos en
un descriptor hay que contrastar cada uno con dos fuentes**, como manda la
regla del proyecto (§6.3).

| | STM32F407VG | **MSP432P401R** | **MSP432E401Y** |
| :--- | :--- | :--- | :--- |
| **Núcleo** | Cortex-M4F, 168 MHz | Cortex-M4F, **48 MHz** | Cortex-M4F, **120 MHz** |
| **NVIC** | 82 líneas, 4 bits | Hasta `PORT6_IRQn`, **3 bits** | ≈ 114 líneas, **3 bits** |
| **MPU / FPU** | 8 regiones / FPv4-SP | 8 / FPv4-SP | 8 / FPv4-SP |
| **Flash** | 1 MB en `0x0800_0000`, alias en `0x0` | **256 KB en `0x0000_0000`**, dos bancos (lectura mientras se escribe), sectores de 4 KB; **Flash de información** de 16 KB en `0x0020_0000` con el **TLV** y el **BSL** | **1 MB en `0x0000_0000`**, cuatro bancos con *prefetch*, sectores de 16 KB; **EEPROM de 6 KB** con su propio controlador |
| **SRAM** | 112 + 16 + 64 (CCM) KB | 64 KB en `0x2000_0000`, **con alias de código en `0x0100_0000`**, en bancos que se apagan y se retienen por separado | 256 KB en `0x2000_0000` |
| **ROM** | System Memory (bootloader de ST) | **32 KB en `0x0200_0000`: DriverLib**, con `ROM_APITABLE` en `0x0200_0800` | **ROM en `0x0100_0000`**: cargador serie, DriverLib (PDL), tablas de AES y de CRC |
| **Arranque** | BOOT0 + vector en Flash | Un **código de arranque de TI** se ejecuta en cada reset antes del vector: procesa el **buzón de la Flash** (BSL, borrado de fábrica, zonas protegidas) y luego salta a la aplicación | Arranque en ROM: si la Flash está en blanco o `BOOTCFG` lo pide por un pin, entra en el **cargador en ROM**; si no, salta a `0x0` |
| **Identificación** | `DBGMCU_IDCODE` | **TLV** (`0x0020_100C`, ID y revisión) | `DID0`/`DID1` (`0x400F_E000`) |
| **Reloj** | RCC: HSI/HSE/PLL | **CS**: DCO (1,5-48 MHz, ajustado con la calibración del TLV), HFXT, LFXT, VLO, REFO, MODOSC y SYSOSC → MCLK, HSMCLK, SMCLK, ACLK y BCLK; con llave `0x695A` | **SYSCTL** de Tiva: PIOSC de 16 MHz, MOSC, PLL (VCO de 480 MHz), `RSCLKCFG`, y **`MEMTIM0`** para los tiempos de Flash y EEPROM |
| **Energía** | PWR, Stop/Standby | **PCM** (`AM_LDO_VCORE0/1`, `AM_DCDC`, LPM0/3/4/3.5/4.5) + **PSS**. Para pasar de 24 MHz hace falta VCORE1 **y** los estados de espera del FLCTL | Sleep/Deep-sleep + **Hibernation** (con RTC y VBAT) |
| **Pines** | Puertos A-I de 16, AF0-15 | **P1-P10 y PJ, de 8 bits**, accesibles a pares de 16 (PA = P1:P2). `PxSEL0/PxSEL1`, `PxREN` + `PxOUT` para elegir *pull-up* o *pull-down*, `PxDS` con alta corriente en algunos pines, interrupción con `PxIV` en P1-P6, y **PMAP** (mapeo de funciones) en P2, P3 y P7 | Puertos **A-T, de 8 bits**. **`GPIODATA` enmascarado por los bits [9:2] de la dirección**, `AFSEL` + `PCTL` (4 bits por pin), **`DEN`** (sin él el pin no es digital), fuerza de 2/4/8/12 mA, y **`LOCK`/`CR`** para los pines protegidos (PC0-3, PD7, PE7). Cada puerto tiene una apertura AHB y otra APB |
| **Periféricos** | ST | eUSCI_A ×4 (UART/SPI/IrDA), eUSCI_B ×4 (SPI/I²C), Timer_A ×4, **Timer32** (doble temporizador de ARM), ADC14 (14 bits, 1 Msps), COMP_E ×2, REF_A, RTC_C, WDT_A, **µDMA PL230** (8 canales), AES256, CRC32 | UART ×8, QSSI ×4, I²C ×10, GPTM ×8 (16/32), PWM, QEI, ADC ×2 (12 bits, secuenciadores), comparadores, **µDMA PL230** (32 canales), **CAN ×2 (C_CAN)**, **USB OTG (Mentor)**, **Ethernet MAC + PHY**, EPI, AES/DES/SHA-MD5, CRC, EEPROM y Hibernation; LCD en el E411Y |
| **Depuración** | SWJ-DP, DBGMCU | JTAG de 4 hilos y SWD + SWO. Pines dedicados `SWCLKTCK` y `SWDIOTMS`; `PJ.4`/`PJ.5` = TDI/TDO-SWO. Bloqueo de JTAG/SWD y zonas IP protegidas | JTAG/SWD en **PC0-PC3**. Desbloqueo por la secuencia de *debug port unlock* |
| **Placa típica** | Discovery, Nucleo | **MSP-EXP432P401R** (XDS110-ET a bordo, consola por el *backchannel* en P1.2/P1.3) | **MSP-EXP432E401Y** (XDS110 a bordo, consola por UART0 en PA0/PA1, RJ45) |

Cinco consecuencias para el modelo:

1. **Un chip que arranca con su propio código antes que el del alumno.** En el
   P4, el código de arranque de TI procesa el buzón de la Flash. En el E4, la
   ROM decide entre el cargador y la aplicación. Ninguno de los dos se puede
   ignorar sin más: un P4 «virgen» entra en el BSL al encenderlo, y un E4 con
   la Flash en blanco entra en el cargador en ROM.
2. **Los firmwares de TI leen datos de calibración.** En el P4,
   `CS_setDCOFrequency()` lee las constantes del DCO en el **TLV**, y el ADC y
   el sensor de temperatura también tienen las suyas allí. Si el TLV está vacío
   (`0xFF…`), el reloj sale mal **sin ningún error**. Hay que rellenarlo con
   valores típicos y decirlo.
3. **El reloj tiene precondiciones que el silicio no comprueba.** Subir el P4 a
   48 MHz sin VCORE1 o sin estado de espera, o el E4 a 120 MHz sin `MEMTIM0`,
   no da un error: da lecturas de Flash erróneas. El modelo debe hacer lo que
   ya hace el RCC del F407 con los topes, que es **avisar con nombre y
   apellidos**. Si además puede reproducir el fallo de forma determinista, por
   ejemplo con un *bus fault* declarado, mejor.
4. **Los periféricos son de otro linaje.** El P4 es un MSP430 por dentro
   (registros de 16 bits, contraseñas como `WDTPW = 0x5A`, vectores de
   interrupción `IV`). El E4 es un Tiva (registros PrimeCell, `RCGC`/`SRCR`/`PR`
   por periférico, **registros `PRx` que el firmware sondea** hasta que el
   periférico está listo). Del código de `periph/` de ST no se puede reutilizar
   ningún registro.
5. **Pero hay IP compartida**, y merece la pena buscarla con dos fuentes (§3.3):
   el **µDMA** es el PL230 de ARM en los dos, el **Timer32** del P4 es el doble
   temporizador de ARM, y la **MAC Ethernet del E4** es, con mucha
   probabilidad, de la misma familia DesignWare de Synopsys que la del F407.

---

## 3. Qué se aprovecha del código que ya hay

### 3.1 Tal cual, sin tocar una línea

| Qué | Dónde | Para qué en el MSP432 |
| :--- | :--- | :--- |
| **El núcleo** | `core/cpu.h`, `cpu_exec16.h`, `cpu_exec32.h`, `cpu_state.h`, `fpu.h` | Igual: Thumb-2, FPv4-SP, excepciones, lockup y anotación de ciclos |
| **SCS** (NVIC, SCB, SysTick, MPU) | `core/scs.h`, `core/core_caps.h` | Con `prio_bits = 3` y el `n_irq` de cada chip. **Ya probado con 3 bits** en T127 |
| Contrato CPU ↔ depurador | `core/debug_if.h` | Igual |
| **SW-DP, AHB-AP, FPB, DWT, ITM y TPIU** | `core/debug.h` (salvo DBGMCU y ROM table, §3.2) | Igual: son de ARM |
| **Stub interno de GDB** (`--gdb-dap`) | `core/gdb_stub_dap.h` | G1 (§5.1), sin cambios |
| **Sonda SWD por los pines** | `verif/gdb_stub.h`, `verif/swd_port.h` | G2 (§5.2), sin cambios |
| Bit-banding | `bus/bitband.h` | Las regiones de ARM son las mismas, y el SDK de TI usa `BITBAND_PERI` |
| Nodos eléctricos, generador de reloj, esclavo TLM | `common/analog_net.h`, `clock_gen.h`, `periph_base.h` | Base de todos los periféricos nuevos |
| **El pad eléctrico** | `pins/pad.h` | Igual. La fuerza de salida (`PxDS` en el P4, `DRxR` en el E4) se traduce a impedancia, como hoy `OSPEEDR` |
| Netlist, XML, `<sistema>`, `une=`, validación, factoría de piezas | `parts/netlist*.h`, `xml_min.h`, `part_factory.h` | Placas MSP432 y **placas mixtas** (un P4 y un F407 por I²C) |
| **Catálogo de piezas** | `parts/ext_parts.h`, `tft_st7735.h`, `servo.h`… | LED (P1.0 y el RGB en P2.0-2.2), pulsadores (P1.1, P1.4), cristales, EEPROM I²C, servo, KY-040, **la TFT ST7735 del BOOSTXL-EDUMKII**, FT232RL, y el bus CAN con sus transceptores para el E4 |
| **Puente serie** con TCP en crudo, RFC 2217, DTR/RTS y *break* | `parts/puente_serie.h`, `motor_uart.h`, `common/telnet2217.h`… | La consola del *backchannel* del XDS110 y los cargadores serie (D4) |
| Frontera con `mcu-sim-gui`, red, freno de tiempo real, `--espera-terminal`, varios MCUs con un GDB cada uno | `common/gui_*.h`, `red.h`, `top/sim_main.cpp` | Igual |
| Huella de firmware, ASan, guarda de *tick* | `common/huella_fw.h`, `asan_opciones.h`, `guarda_tick.h` | Igual |
| CI por invariante de tiempo simulado | `ci/`, `.github/workflows/suites.yml`, `verif/invariantes.txt` | Los bancos `testp4` y `teste4`, cada uno con su cifra |
| GDB de pega | `verif/gdb_client.h` | Probar la descarga y la depuración sin `gdb` instalado |
| CoreMark | `verif/fw/coremark/` | Otro `core_portme`, con la misma cadena de herramientas |
| Modelo de consumo IDD de la fase F7 | `periph/pwr.h` y lo que cuelga de él | El P4 se vende por su consumo, y LPM3/LPM4.5 son prácticas típicas. La curva de consumo del modelo es lo más parecido a EnergyTrace que se puede ofrecer |

### 3.2 Con una generalización pequeña (fase T0)

**Coincide en gran parte con la E0 de `doc/analisis_esp32_riscv.md`.** Si se
hace una, la otra se reduce a lo específico de cada caso. Se marca con ★ lo que
solo pide el MSP432.

| Qué | Cambio |
| :--- | :--- |
| `mcu_if` | `caps()` → un `McuInfo` mínimo (nombre, familia, encapsulado y pads). `McuCaps` queda como descriptor **propio** de los STM32F4. `nodo_analogico(const std::string& pad)` y `pads()`. *(Compartido con E0)* |
| Nombres de pad | `pad_desde_nombre` deja de ser una regla global: **el chip la aporta**. Para el P4: `P1.0`…`P10.7` y `PJ.0`…`PJ.5`, más el alias `P1_0` (el que se usa en código). Para el E4: `PA0`…`PT7` sin I ni O. T125 se mantiene **intacta** para los STM32. *(Compartido con E0; el caso `P1.0` es ★)* |
| `Encapsulado`, `Cableado`, `N_GPIO_PORTS`, `N_PORT_PINS` | Pads con nombre o máscaras por puerto. Ocho pines por puerto caben de sobra en dieciséis, pero **18 puertos no caben en 9** |
| `PowerPads` | Lista por familia: sin BOOT0; `RSTn/NMI` en el P4; `RST`, `HIB` y `WAKE` en el E4. *(Compartido)* |
| ★ `CoreRouter::remap()` | `boot_mode` pasa a ser **opcional**: `AliasArranque::Ninguno` deja `0x0` tal cual. El F407 sigue usando el suyo |
| ★ Ventana del PPB de fabricante | Un `sc_port` opcional del núcleo hacia el SoC para el rango que **no** es de ARM (`0xE004_0000`…`0xE00F_EFFF`, salvo los componentes de depuración que estén en la ROM table). El STM32 enchufa ahí su DBGMCU, que **sale** de `debug.h`; el P4 enchufa su SYSCTL. Así debug.h queda de ARM |
| ★ ROM table | Los identificadores de componente y el JEP106 del fabricante (TI = `0x017`) pasan a ser datos del descriptor |
| ★ Referencia externa del SysTick | Ya es un puerto (`systick_ext`). Solo hay que dejar de suponer que la aporta el RCC |
| `GdbRsp` | Separar el **programador de memoria no volátil** en una interfaz `programador_nv_if` (rangos, borrar, escribir, terminar). El F407 conserva **la misma secuencia de accesos al DAP**, que es lo que mantiene T96/T97 al paquete. *(La E0 parte además la arquitectura; aquí no hace falta, porque es ARMv7-M)* |
| `ImageLoader` / `carga_firmware` | Base de la Flash tomada del chip; **ELF** (el `.out` de CCS y el `.elf` de GCC) y **TI-TXT**. *(ELF es compartido)* |
| `cubeide/simulador.launch` | Su equivalente para CCS (§5.4) y un `launch.json` para VS Code + Cortex-Debug |

### 3.3 IP compartida y algoritmos

| Qué | Dónde está hoy | En el MSP432 |
| :--- | :--- | :--- |
| **MAC Ethernet DesignWare** | `periph/eth_mac.h` (descriptores, filtrado, MMC, PTP, MDIO) | La EMAC del E4 tiene la misma organización: MAC + DMA con descriptores de propiedad `OWN`, filtrado de tramas, MMC y PTP, con otro mapa de desplazamientos (por ejemplo, `EMACDMABUSMOD` en `0xC00` frente a `ETH_DMABMR` en `0x1000`). **Si se confirma con dos fuentes que es la misma IP**, el motor se reaprovecha separando *motor* y *mapa*. Si no, sirve de patrón muy cercano |
| AES, DES y TDES | `periph/cryp_algo.h` | El AES del E4 y el AES256 del P4 (AES-128/192/256). El DES/TDES del E4 |
| MD5 y SHA-1 | `periph/hash_algo.h` | El SHA/MD5 del E4. **SHA-224 y SHA-256 hay que añadirlos**, y el fichero ya lo prevé |
| Vectores de cifrado | `verif/vectores/` | Los mismos, más los de SHA-2 |
| CRC-32 | `periph/crc.h` | El CRC32 del P4 (CRC-16-CCITT y CRC-32, con inversión de bits) y el CRC del E4 (polinomios configurables). El algoritmo es una generalización del que ya está |
| Bus CAN, transceptores y temporización de bit | `parts/` + la parte de bus de `periph/can.h` | El C_CAN del E4 tiene **otro modelo de registros** (objetos de mensaje en una RAM interna), pero el bus, el arbitraje y los transceptores son los mismos |
| Transceptor USB en los pines y aparejos del puente USB | `periph/otg.h` (la parte eléctrica), `doc/analisis_puente_usb.md` | El OTG del E4 es de Mentor (MUSB), **no** el DWC OTG de ST: el controlador es nuevo. Lo que sí vale es el lado eléctrico y el lado del ordenador (U1/U2) |
| Puente Ethernet | `doc/analisis_puente_ethernet.md` | Con un matiz: **el PHY del E401Y está dentro del chip**. El puente no se engancha al MII en los pines, como en el F407, sino a la frontera interna MAC↔PHY o al par diferencial, a nivel de trama |

### 3.4 Lo que no sirve como código, pero sí como patrón

`rcc/`, `bus/ahb_matrix.h` (la matriz 8×7 de ST), `soc/`, `top/soc_f4*.h` y
todo `periph/` salvo lo citado son de ST. De ellos se hereda la manera de hacer
las cosas:

* **los rasgos como dato**: un `EusciCaps` (A o B), un `TimerACaps` (número de
  CCR), un `GptmCaps`, y el descriptor de cada referencia;
* **los contadores por sucesos** de `timers.h`, para Timer_A, Timer32 y GPTM;
* **`bloque_declarado.h`** para lo que no se modela: EPI, LCD del E411Y, QEI,
  zonas IP protegidas del P4, el DC-DC como fuente de ruido…;
* **el aviso de topes del RCC**, trasladado al CS/PCM/FLCTL del P4 y al
  SYSCTL/`MEMTIM0` del E4;
* **`limitaciones()`** en voz alta al montar la placa;
* **la estructura de bancos y fases**: un banco por familia con su invariante.

---

## 4. Descarga del firmware

### 4.1 Lo que usa el alumno con un MSP432 de verdad

Para centrar las vías conviene recordar cómo se descarga en la placa:

* **CCS** (Eclipse hasta la 12.x; Theia desde la 20) compila con el TI Arm
  Compiler, TI Arm Clang o GCC, genera un **`.out` (ELF)** y descarga con el
  **XDS110** de la LaunchPad a través de la pila de emulación de TI.
* **UniFlash** / `dslite` hacen lo mismo desde fuera del IDE, con un `.ccxml`.
* **OpenOCD** trae un controlador de Flash `msp432` para los P4 (P401x y P411x)
  y los E4 (E401Y y E411Y). Programa con un algoritmo en RAM y lee la identidad
  del chip en `0x0020_100C` (P4) o en `DID0`/`DID1` (E4). TI recomienda
  OpenOCD para usar GDB con Cortex-M; su *XDS GDB Agent* está en
  mantenimiento.
* **Cargadores serie**: el BSL del P4 (UART en P1.2/P1.3, I²C o SPI; se entra
  con la pieza virgen o con un pin configurado en el buzón de la Flash), y el
  cargador en ROM del E4 (UART0, SSI0, I²C0 y, según el manual, USB DFU y
  Ethernet). Las herramientas son **BSL Scripter**, el *MSP432 Security and
  Update Tool* o, en el mundo Tiva, **`sflash`** y **LM Flash Programmer**.

### 4.2 Vía D1 · Carga directa al arrancar

```
./build/mcu-sim placas/msp_exp432p401r.xml Debug/blinky.out          # ELF de CCS
./build/mcu-sim placas/msp_exp432p401r.xml blinky.hex                 # Intel HEX
./build/mcu-sim placas/msp_exp432p401r.xml blinky.txt                 # TI-TXT
./build/mcu-sim placas/msp_exp432e401y.xml build/eth_echo.elf
```

* **ELF**: se cargan los segmentos `PT_LOAD` por su **LMA**, porque los datos
  inicializados viajan en Flash y los copia el arranque. Cada segmento se
  contrasta con el mapa del chip, y un segmento fuera de la Flash o la SRAM es
  un error con nombre, no un byte perdido. Es lo que más útil será también para
  el STM32.
* **TI-TXT**: `@dirección` seguido de bytes en hexadecimal y una `q` final. Es
  trivial de leer y es el formato del BSL y de UniFlash.
* **Flash de información y TLV**: el modelo carga un **TLV por omisión** con
  valores típicos y admite `--tlv fichero.txt`, para quien vuelque el de su
  placa. Así se reproduce un chip concreto, con su calibración del DCO.

### 4.3 Vía D2 · `load` de GDB contra el stub interno

Es la que usarán casi todos, porque es la que usan **CCS en modo GDB, VS Code
con Cortex-Debug y `arm-none-eabi-gdb`**. Hoy ya existe para el F407: el stub
implementa `vFlashErase`/`vFlashWrite` y convierte los `M`/`X` sobre la Flash
en la secuencia de programación (`cubeide/README.md`, «El tercero»).

Lo que cambia es **la secuencia**, que sale de `GdbRsp` hacia un
`programador_nv_if` por familia (§3.2):

| | P4 (FLCTL) | E4 (`FMC`) |
| :--- | :--- | :--- |
| Proteger o desproteger | `FLCTL_BANKx_MAIN_WEPROT` por sector | `FMPPEn` (protección de programa) y la llave de `FMC` |
| Borrar | `FLCTL_ERASE_SECTADDR` + `FLCTL_ERASE_CTLSTAT`, sector de 4 KB | `FMA` + `FMC.ERASE` + llave, sector de 16 KB |
| Programar | Palabra inmediata o ráfaga de 128 bits (`FLCTL_PRG_CTLSTAT`, `PRGBRST_*`) | Palabra (`FMD` + `FMC.WRITE`) o búfer de 32 palabras (`FMC2.WRBUF`) |
| Esperar | Bits de estado de `FLCTL_PRG_CTLSTAT` / `ERASE_CTLSTAT` | `FMC` hasta que se borra el bit, y `FCRIS` |

**Por qué no es hacer trampa**: por el mismo argumento de `gdb_stub_dap.h`. El
stub *es* la sonda, y una sonda de verdad no escribe la Flash a pelo: ejecuta
esa secuencia por el DAP. El controlador de Flash del modelo es el que la
valida, rechaza escrituras sin desproteger y cuenta los tiempos de borrado.

### 4.4 Vía D3 · OpenOCD de verdad, por SWD

Es la vía que **ejercita el modelo con código que no hemos escrito**, el
equivalente de esptool en el análisis del ESP32. OpenOCD:

1. identifica el chip leyendo el TLV (P4) o `DID0`/`DID1` (E4) por el AHB-AP,
   y **en el P4 lee `0xE004_3020`** para el tamaño de la Flash, lo que obliga a
   tener hecha la ventana del PPB de T0;
2. copia su algoritmo de Flash a la SRAM y lo **ejecuta en el núcleo
   simulado**;
3. habla con él por un buzón en la SRAM y le pasa los datos en dos búferes.

Si eso funciona, **el núcleo, la SRAM, el FLCTL o la `FMC`, las paradas y la
reanudación quedan verificados de punta a punta**.

Lo que hace falta es que OpenOCD llegue a los pines. La propuesta es una
variante de la pieza de sonda que, en lugar del GDB de `verif/gdb_stub.h`,
hable hacia el ordenador el protocolo **`remote_bitbang`** de OpenOCD, que
admite SWD en las versiones recientes:

```
openocd -c "adapter driver remote_bitbang" -c "remote_bitbang host localhost" \
        -c "remote_bitbang port 4444" -c "transport select swd" \
        -f target/ti/msp432.cfg \
        -c "program Debug/blinky.out verify reset exit"
```

*(La ruta del `.cfg` depende de la versión de OpenOCD; en las antiguas era
`target/ti_msp432.cfg`. Hay que fijar la versión mínima en `doc/fuentes.md`.)*

El precio es el de la sonda SWD de hoy: cada bit es un flanco y cada flanco un
suceso, del orden de ×390 frente al stub interno (T97). Esta pieza **sirve tal
cual al STM32**, que gana OpenOCD sin ST-LINK.

### 4.5 Vía D4 · Cargadores serie de TI por el `PuenteSerie`

* **P4, BSL por UART** (eUSCI_A0, P1.2 RX / P1.3 TX; en la LaunchPad son los
  del *backchannel* del XDS110). Hace falta el **código de arranque** (§6.2),
  que muestrea el pin de invocación configurado en el buzón o detecta la pieza
  virgen, y la **imagen del BSL** en la Flash de información. OpenOCD
  identifica los sectores 2 y 3 de la información como BSL.
* **E4, cargador en ROM por UART0** (PA0/PA1), con el protocolo de paquetes de
  Tiva: tamaño, *checksum* y `ACK 0xCC`/`NAK 0x33`; órdenes `PING 0x20`,
  `DOWNLOAD 0x21`, `RUN 0x22`, `GET_STATUS 0x23`, `SEND_DATA 0x24` y
  `RESET 0x25`; y autobaudio. Se entra con la Flash en blanco o con el pin de
  `BOOTCFG`.

Del lado del ordenador, BSL Scripter y `sflash` abren un puerto COM. Valen las
**recetas de redirector de `doc/puente_serie.md` §4** (com0com, tty0tty,
ttynvt), que ya están escritas. `sflash` es código abierto (BSD) y sirve además
como cliente de prueba en el CI, como `cliente_2217.h`.

La versión por **Ethernet** del cargador del E4 (BOOTP + TFTP) llegaría con el
puente Ethernet, y la versión por **USB DFU**, con el puente USB. Las dos son
tardías.

### 4.6 Vía D5 · Una sonda CMSIS-DAP por USB/IP (opcional)

El XDS110 es **compatible con CMSIS-DAP** además de con el protocolo XDS de TI.
Con el servidor USB/IP de `doc/analisis_puente_usb.md` (U1), el modelo podría
presentarse en el sistema como una sonda CMSIS-DAP v2. Así funcionarían sin
saber que hay un simulador **pyOCD, OpenOCD (`cmsis-dap`), Keil e IAR**, y
OpenOCD lo haría sin el coste bit a bit de D3.

**Lo que esta vía no da es el depurador nativo de CCS ni UniFlash**: los dos
hablan el protocolo propio del XDS110. Su descripción pública más completa es
el controlador `xds110` de OpenOCD, pero emularlo es otro proyecto. Queda como
pregunta abierta (§9).

### 4.7 Resumen de la descarga

| Vía | Herramienta del alumno | Qué ejercita del modelo | Coste | Fase |
| :--- | :--- | :--- | :--- | :--- |
| D1 carga directa | ninguna (`mcu-sim … fw.out`) | Mapa de memoria y arranque | Nulo | T1 |
| **D2 `load` de GDB** | CCS (GDB Hardware Debugging), VS Code + Cortex-Debug, `arm-none-eabi-gdb` | AHB-AP + FLCTL / `FMC` | Bajo: un `programador_nv_if` por familia | **T2** (P4), T6 (E4) |
| D3 OpenOCD por SWD | `openocd … program` | DAP, núcleo, SRAM y Flash ejecutando código ajeno | Medio: la sonda `remote_bitbang` | T5 |
| D4 cargador serie | BSL Scripter, `sflash`, con redirector | Arranque, UART, ROM/BSL y Flash | Medio (P4: falta la imagen del BSL) | T5 (P4), T6 (E4) |
| D5 CMSIS-DAP por USB/IP | pyOCD, Keil, IAR, OpenOCD | DAP | Alto; depende del puente USB | T8 |

---

## 5. Depuración del firmware

### 5.1 Vía G1 · Stub interno pegado al DAP (`--gdb-dap`): **ya existe**

No hay que escribirla: es `core/gdb_stub_dap.h` con el núcleo de siempre.
Funciona en cuanto el chip se monta, con estos cambios:

* `target.xml`: el mismo de ARMv7-M con FPU;
* programación de la Flash: la de §4.3;
* `monitor reset` / `monitor reset halt`: `SYSRESETREQ`, como hoy. En el P4,
  el reset tiene matices (reset «duro» y «blando», y la fuente que lo pide
  queda anotada en RSTCTL), y el modelo debe dejarlo registrado igual;
* la congelación de temporizadores y del perro guardián en las paradas, que en
  el STM32 decide el DBGMCU, la decide en el P4 `SYS_PERIHALT_CTL` y en el E4
  cada periférico (`GPTMCTL.TASTALL`…).

```
./build/mcu-sim placas/msp_exp432p401r.xml --gdb-dap --port=3333 --tiempo-real
arm-none-eabi-gdb Debug/blinky.out -ex "target extended-remote :3333" -ex load
```

### 5.2 Vía G2 · Sonda SWD por los pines: **ya existe**

`verif/gdb_stub.h` + la sonda de los pines, enganchada a `SWCLKTCK`/`SWDIOTMS`
(P4) o a `PC0`/`PC1` (E4). El protocolo es el mismo SW-DP de ARM. Lo que hay
que modelar en el chip es **lo que TI pone alrededor**:

* en el E4, que PC0-PC3 son pines **protegidos con `LOCK`/`CR`** y que la
  función JTAG/SWD es la que tienen al salir del reset;
* en el P4, el **bloqueo de JTAG/SWD** que se programa por el buzón de la
  Flash. Hay que contrastar en el TRM cómo se hace por la sonda el
  **desbloqueo con borrado de fábrica**. Mientras no se modele, se declara.

### 5.3 Vía G3 · OpenOCD por `remote_bitbang`

La misma sonda de D3. Da todo lo que da OpenOCD: los hilos de FreeRTOS y de
TI-RTOS en GDB, `program`, `flash erase_sector` y `mdw`. Además es la única vía
por la que **un depurador que no hemos escrito** habla con el DAP del modelo
bit a bit.

### 5.4 Los IDE

| IDE | Cómo | Estado |
| :--- | :--- | :--- |
| **CCS (Eclipse, hasta 12.x)** | Lanzamiento **GDB Hardware Debugging** de CDT, contra `localhost:3333`, con el `arm-none-eabi-gdb` que CCS instala con el compilador GCC. Es el equivalente exacto de `cubeide/simulador.launch` | **Escrito en T2** (`src/ide/msp432/ccs/`), probado con `gdb-multiarch` por MI, no dentro de CCS. Con el compilador de TI (`.out` de TI CGT), GDB lee bien el ELF, pero la información de depuración DWARF de TI CGT puede dar sorpresas: probar las dos cadenas |
| **CCS 20+ (Theia)** | Hay que comprobar si admite un servidor GDB remoto genérico | Abierto (§9) |
| **CCS, depurador nativo** | Necesita un XDS110 | **No**, salvo con D5 + emulación del protocolo XDS |
| **VS Code + Cortex-Debug** | `"servertype": "external"`, `"gdbTarget": "localhost:3333"`, y el SVD del chip (TI publica los SVD de los dos en los CMSIS packs) | **Escrito en T2** (`src/ide/msp432/vscode/launch.json`, también con `cppdbg`), sin probar dentro de VS Code |
| **Keil / IAR** | CMSIS-DAP | Solo con D5 |
| **Energia** (Arduino para MSP432) | Sube con DSLite | Proyecto parado; no se prioriza |

### 5.5 Resumen de la depuración

| Vía | `depuracion=` | Herramienta | Fiel a | Velocidad | Fase |
| :--- | :--- | :--- | :--- | :--- | :--- |
| G1 stub interno al DAP | `dap` | `arm-none-eabi-gdb`, CCS (GDB HW), VS Code | AHB-AP, núcleo y FPB/DWT | Rápida | **T2** |
| G2 sonda SWD por pines | `pines` | El mismo | Además SW-DP, pines y bloqueos | Lenta | T2 |
| G3 OpenOCD por `remote_bitbang` | `pines` + sonda OpenOCD | `openocd` + GDB | Igual que G2, con OpenOCD | Lenta | T5 |
| G4 CMSIS-DAP por USB/IP | `pines` | pyOCD, OpenOCD, Keil, IAR | DAP | Media | T8 |

---

## 6. La ROM, el arranque, el TLV y las fuentes

### 6.1 La ROM: reconstruible, al contrario que en el ESP32

En el P4 la ROM es **DriverLib**. Los ejemplos del SDK llaman a `MAP_xxx()`, y
esa macro elige la versión en ROM (`ROM_xxx`) cuando existe para el objetivo
(`TARGET_IS_MSP432P4XX`). Cada `ROM_xxx` es un salto **a través de una tabla
de tablas**: `ROM_APITABLE = 0x0200_0800`, la entrada 0 es la versión y las
siguientes apuntan a la tabla de cada módulo (`ROM_ADC14TABLE[0]` es
`ADC14_enableModule`…). En el E4 la idea es la misma, con la ROM en
`0x0100_0000` (en TivaWare, la tabla está en `0x0100_0010`; hay que contrastarlo
con el `rom.h` del SDK de E4).

| Opción | Qué es | Veredicto |
| :--- | :--- | :--- |
| **R1. ROM sustituta construida** | Compilar la DriverLib del SDK (BSD-3) con `arm-none-eabi-gcc`, colocar en `ROM_APITABLE` **las mismas tablas, en el mismo orden que `rom.h`**, y enlazarlo todo en la ventana de la ROM | **La recomendada por omisión.** Se puede **versionar**, porque BSD-3 es compatible con AGPLv3, igual que CMSIS. Con ella funcionan sin cambios los ejemplos que usan `MAP_`. Diverge del silicio en el **número de instrucciones**, y por eso en los tiempos, y eso queda escrito en `doc/todo.md` |
| R2. ROM de verdad, volcada | `--rom rom_p4.bin`, volcada de una LaunchPad con OpenOCD (`dump_image rom.bin 0x02000000 0x8000`) o con UniFlash | **Opcional.** Es de TI y **no se versiona**: misma política que los PDF de ST (`doc/fuentes.md` §0). Sirve para medir la divergencia de R1 |
| R3. Sin ROM | La ventana devuelve error de bus | Para los firmwares *bare-metal* de las suites que no usan `MAP_`/`ROM_` |

El **cargador en ROM del E4** no forma parte de la DriverLib. Para D4 hay dos
caminos: compilar el cargador que trae el SDK (BSD) y colocarlo en la ventana
de la ROM, o implementarlo «de alto nivel» en C++. El primero es más fiel y se
puede versionar.

### 6.2 El arranque

* **P4.** El código de arranque de TI se ejecuta en cada reset. Se modela **de
  alto nivel**, en C++ y con sus tiempos: lee el buzón de la Flash (BSL, borrado
  de fábrica, bloqueo de JTAG), aplica la configuración y salta al vector de
  `0x0`. Las **zonas IP protegidas** y el **arranque seguro** quedan
  declarados.
* **E4.** Con `BOOTCFG` y la Flash en blanco se decide entre cargador y
  aplicación. Lo mismo: de alto nivel, o con el cargador de §6.1.

### 6.3 El TLV

El *Device Descriptor* del P4 (Flash de información, `0x0020_1000`) lleva la
identidad del chip y **las calibraciones**: el DCO, el ADC, la referencia y el
sensor de temperatura. El modelo lo genera con **valores típicos tomados de dos
fuentes** (la tabla del TLV del datasheet y `tlv.h` del SDK). `--tlv` permite
cargar el de una placa concreta, y `limitaciones()` dice que es un TLV
sintético.

### 6.4 Las dos fuentes de cada dato

| Papel | STM32 (hoy) | MSP432 |
| :--- | :--- | :--- |
| Manual técnico (no se versiona) | RM0090, RM0390 | **MSP432P4xx Technical Reference Manual** (SLAU356) y **MSP432E4 Technical Reference Manual** (SLAU723) |
| Datasheet (no se versiona) | DS8626… | MSP432P401R (SLAS826) y MSP432E401Y/E411Y: pines, TLV, tiempos de Flash, BSL |
| Cabeceras del fabricante (se versionan) | CMSIS, Apache-2.0 | `msp432p401r.h`, `msp432e401y.h` y los SVD de los CMSIS packs; DriverLib (BSD-3) |
| Base de pines | `STM32_open_pin_data` | Datasheet + SysConfig (los ficheros de datos de dispositivo del SDK) |
| Herramientas de referencia | — | **OpenOCD** (`src/flash/nor/msp432.c`, GPL-2.0): se usa como prueba y como **segunda opinión de comportamiento**, no se copia código. Igual con el controlador `xds110` |
| Cargadores | — | SLAU622 (BSL del P4), SLAU746 (cargador del E4), `sflash` (BSD) |

---

## 7. Lo que hay que escribir

### 7.1 Ficheros nuevos, por carpeta

| Carpeta | Ficheros | Qué |
| :--- | :--- | :--- |
| `soc/msp432p4/` | `p4_mapa.h` (direcciones, vectores, señales PMAP), `msp432p401r.h` (el SoC), `msp432p4_mcu.h` (adaptador a `mcu_if` y `REGISTRA_MCU(MSP432P4, …)`) | El chip |
| `periph/msp432p4/` | `sysctl.h`, `rstctl.h`, `pcm.h`, `pss.h`, `cs.h`, `flctl.h`, `wdt_a.h`, `port.h` (+ PMAP), `eusci_a.h`, `eusci_b.h`, `timer_a.h`, `timer32.h`, `adc14.h`, `ref_a.h`, `comp_e.h`, `rtc_c.h`, `aes256.h`, `crc32.h` | Los periféricos |
| `soc/msp432e4/` | `e4_mapa.h`, `msp432e401y.h`, `msp432e4_mcu.h` (`REGISTRA_MCU(MSP432E4, …)`) | El chip |
| `periph/msp432e4/` | `sysctl.h` (PLL, `MEMTIM0`, `RCGC`/`SRCR`/`PR`), `gpio.h` (dato enmascarado, `DEN`, `LOCK`/`CR`, aperturas AHB/APB), `flash_ctrl.h`, `eeprom.h`, `uart.h`, `ssi.h`, `i2c.h`, `gptm.h`, `pwm.h`, `adc.h`, `comp.h`, `hib.h`, `wdt.h`, `emac.h` (+ PHY interno), `usb_musb.h`, `ccan.h`, `aes.h`, `des.h`, `shamd5.h`, `crc.h`, y los declarados (EPI, LCD, QEI…) | Los periféricos |
| `periph/arm/` | `udma_pl230.h`; `dualtimer.h` si el Timer32 lo confirma | **IP de ARM**, compartida por las dos familias |
| `mem/` | `rom_tabla.h` (ventana de ROM con imagen por fichero), `tlv.h`, `nv_programador.h` (la interfaz de §3.2) | La memoria |
| `common/` | `elf_loader.h`, `titxt_loader.h` | Los formatos |
| `parts/` | `SondaOpenOcd` (SWD por `remote_bitbang`), `LaunchPadXds110` (el *backchannel* como `PuenteSerie` en sus pines y el reset de la sonda) | Las piezas |
| `placas/` | `msp_exp432p401r.xml` + `.svg`, `boostxl_edumkii.xml` (TFT ST7735, joystick, zumbador, LED RGB, OPT3001 y TMP006 por I²C), `msp_exp432e401y.xml` + `.svg`, `msp432_y_f407.xml` | Las placas |
| `top/` | `sc_main_p4.cpp`, `sc_main_e4.cpp` | Los bancos `testp4` y `teste4`, ejecutables aparte |
| `verif/fw/msp432p4/`, `verif/fw/msp432e4/` | Arranque y *linker script* del SDK para GCC (BSD), *blinky*, *uart*, *timer_a*, *adc14*, *dma*, *lpm*, *debug*, CoreMark; un ejemplo del SDK con `MAP_` | Firmwares versionados con su huella |
| `verif/rom/` | El *makefile* de la ROM sustituta y su huella | §6.1, R1 |
| `cubeide/` (o `ide/`) | `ccs_simulador.launch`, `vscode_launch.json` | §5.4 |

### 7.2 Cambios en lo que hay

* **T0 entera** (§3.2): `core/cortex_m4f.h` (alias opcional, ventana del PPB),
  `core/debug.h` (DBGMCU fuera, ROM table por descriptor),
  `common/gdb_rsp.h` (`programador_nv_if`), `pins/pin_mux.h` (nombres por
  chip), `pins/power_pads.h`, `soc/mcu_if.h`, `verif/image_loader.h`.
* `sim_main.cpp`: `--mcu MSP432P401R`, `--rom`, `--tlv`; el informe final de
  LEDs en P1.0 y P2.x.
* `Makefile.mcu-sim`: fuentes por familia; `testp4`, `teste4`, `asanp4`,
  `fwp4`, `rom-p4`.
* `verif/invariantes.txt` y `ci/`: dos invariantes más.
* `doc/todo.md`: un punto nuevo con sus subpuntos, como P-12 y P-14.
* `TERCEROS.md` y `doc/fuentes.md`: DriverLib (BSD-3), cabeceras y SVD de TI,
  manuales, OpenOCD como referencia.

---

## 8. Plan por fases

Es el método de los planes de familia y de los puentes. Fases pequeñas, cada una
con su criterio de cierre, un banco propio con su invariante y, como condición
de todas, **sin mover los invariantes de `test407`, `test446`, `test417`,
`testserie` y `testgui`**.

### T0 · Generalización neutral

**Qué:** §3.2. Si la E0 del ESP32 ya está hecha, solo lo marcado con ★.

**Cierre:**
* las cinco suites con **las mismas comprobaciones y el mismo tiempo simulado
  al picosegundo**, en las cuatro plataformas;
* T96 y T97 idénticos paquete a paquete con la traza puesta, porque el
  `programador_nv_if` del F407 emite los mismos accesos;
* T125 intacta para el STM32, y una prueba nueva de nombres TI: `P1.0` →
  primer puerto, `P10.7`, `PJ.4`, `u0.P1.0`, y `P0.0` rechazado;
* ELF: el `blinky.elf` del F407 cargado por ELF da **la misma huella en Flash**
  que su `.bin`.

**Tamaño:** pequeño-medio. Lo delicado es no mover T96/T97.

### T1 · MSP432P401R: el chip que arranca y parpadea

**Qué:**
* el descriptor, con 3 bits, el `n_irq` de la cabecera y la Flash en `0x0`;
* el mapa de memoria: la Flash principal y la de información con el TLV, la
  SRAM con su alias en `0x0100_0000`, y la ventana de ROM vacía (R3);
* SYSCTL en el PPB, RSTCTL, PCM mínimo (AM_LDO_VCORE0/1), CS (DCO con la
  calibración del TLV, REFO, VLO, MODOSC, y LFXT/HFXT como piezas de cristal);
* FLCTL de lectura con estados de espera y el aviso de §2 (consecuencia 3);
* WDT_A, que **arranca encendido**: el primer escollo de cualquier alumno;
* puertos P1-P10/PJ sobre los pads eléctricos, con PMAP e interrupciones `IV`;
* el código de arranque de alto nivel, el adaptador `mcu_if` y la placa
  `msp_exp432p401r.xml`.

**Cierre:**
* `blinky` (P1.0) y el LED RGB, con pulsadores en P1.1/P1.4 por interrupción;
* subir a 48 MHz con la secuencia del SDK (VCORE1 → estados de espera → DCO), y
  **el aviso, si se omite un paso**;
* CoreMark con su CRC correcto;
* que el WDT_A sin parar reinicie el chip, con la causa en RSTCTL;
* `testp4` en el CI con su invariante, y `asanp4` limpio.

**Tamaño:** grande, comparable a F1 + F3 del F407, pero **sin la F2**: el núcleo
ya está.

### T2 · Descarga y depuración del P4 (D2, G1 y G2)

**Qué:** el FLCTL completo (protección, programación inmediata y en ráfaga,
borrado, lectura mientras se escribe entre bancos); su `programador_nv_if`;
`SYS_PERIHALT_CTL`; la placa con `depuracion="dap"` y `"pines"`; el `.launch`
de CCS y el `launch.json` de VS Code.

**Cierre:**
* con `verif/gdb_client.h`: `load` de un ELF, lectura de vuelta idéntica,
  punto de ruptura en Flash, *watchpoint*, paso y `monitor reset halt`, **por
  G1 y por G2 con los mismos resultados**;
* un firmware que reprograma su propia Flash con la DriverLib (`FlashCtl_*`);
* CCS (Eclipse) y VS Code probados a mano y documentados, con el catálogo de
  fallos por síntoma como el de `cubeide/README.md`.

**Tamaño:** medio. **Con esta fase el alumno ya trabaja con el P4.**

### T3 · Periféricos de aula del P4

**Qué**, por orden de uso en prácticas:
* eUSCI_A en modo UART, con el *backchannel* como `PuenteSerie`;
* Timer_A (PWM y captura: el servo ya existe) y Timer32;
* eUSCI_B en I²C y SPI, con la EEPROM 24Cxx y la TFT ST7735;
* ADC14 sobre los nodos analógicos, con REF_A, y el sensor de temperatura del
  TLV;
* µDMA PL230 (`periph/arm/`);
* RTC_C, COMP_E, AES256 y CRC32;
* los modos LPM0/3/4/3.5/4.5 del PCM con el modelo IDD.

La placa **BOOSTXL-EDUMKII**: joystick, zumbador, RGB, TFT y los sensores
OPT3001 y TMP006 como piezas I²C nuevas.

**Cierre:** un firmware y un grupo de comprobaciones por periférico, como en F4
y F5. Los ejemplos de DriverLib correspondientes (`uart_pc_echo`,
`timer_a_pwm`, `adc14_single_conversion`, `dma_*`) contra las piezas de
`parts/`.

**Tamaño:** grande, pero divisible: cada periférico es una entrega.

### T4 · La ROM del P4

**Qué:** R1 (ROM sustituta construida y versionada, con su huella) y R2
(`--rom`), y el TLV por fichero.

**Cierre:**
* un ejemplo del SDK **sin modificar**, que usa `MAP_`, funciona con R1;
* con una ROM volcada (R2), el mismo ejemplo funciona, y se anota la diferencia
  de tiempo simulado entre R1 y R2;
* un ejemplo con FreeRTOS o TI-RTOS del SDK arranca y conmuta tareas.

**Tamaño:** pequeño-medio. El riesgo está en el orden de las tablas.

### T5 · OpenOCD y BSL (D3, G3 y D4 del P4)

**Qué:** la pieza `SondaOpenOcd` por `remote_bitbang` (SWD), las recetas, y el
BSL por UART si se consigue la imagen (§9).

**Cierre:**
* una prueba en Python, al estilo de `gui-marcha`, que lanza `sim`, ejecuta
  `openocd … program blinky.out verify reset exit` **con el controlador
  `msp432` sin modificar** y comprueba que el firmware nuevo arranca;
* **la misma prueba contra un F407** con `target/stm32f4x.cfg`;
* si hay BSL: BSL Scripter, por el redirector, programa y verifica.

**Tamaño:** medio.

### T6 · MSP432E401Y: la segunda familia

**Qué:**
* el descriptor, con la Flash en `0x0`, 1 MB y 256 KB;
* SYSCTL (PIOSC, MOSC, PLL, `RSCLKCFG`, `MEMTIM0`, y `RCGC`/`SRCR`/`PR` con el
  **retardo de «periférico listo»**);
* GPIO A-T con el dato enmascarado, `DEN` y `LOCK`/`CR`;
* el controlador de Flash con su `programador_nv_if`, y la EEPROM;
* UART (el *backchannel* en PA0/PA1), GPTM, SSI, I²C y ADC;
* el µDMA (que ya existe desde T3) y el Watchdog;
* Hibernation con su RTC;
* la ROM (DriverLib de la E4 reconstruida y el cargador del SDK) y su cargador
  serie (D4);
* la placa `msp_exp432e401y.xml`.

**Cierre:** como T1 + T2 juntas: *blinky* (PN0/PN1/PF0/PF4), 120 MHz con el aviso
de `MEMTIM0`, CoreMark, G1/G2 y D2, `sflash` por el redirector, y `teste4` con
su invariante.

**Tamaño:** grande. La mitad que no es de periféricos ya la dejó hecha el P4.

### T7 · Conectividad y criptografía del E4

**Qué:**
* EMAC + PHY interno, reaprovechando `eth_mac.h` si se confirma la IP (§3.3), y
  su enganche al puente Ethernet;
* USB (MUSB) y su enganche al puente USB (U2);
* C_CAN ×2 con el bus CAN que ya existe;
* AES/DES/SHA-MD5 con `cryp_algo.h` y `hash_algo.h` (y SHA-2);
* EPI y LCD declarados.

**Cierre:** los ejemplos del SDK correspondientes (eco TCP con lwIP, CDC por USB,
CAN entre dos E4 de la misma placa, los vectores de cifrado), y el cargador
por Ethernet (BOOTP/TFTP) si el puente lo permite.

**Tamaño:** grande; cada bloque es una entrega.

### T8 · El resto de la familia y lo opcional

| Qué | Supone |
| :--- | :--- |
| **MSP432P401M**, **P4011/P4111** (hasta 2 MB, LCD_F en el P4111) | Descriptores; el LCD_F declarado o modelado |
| **MSP432E411Y** | Descriptor; el LCD declarado |
| **TM4C129x** | El mismo silicio que el E4, sin cripto: descriptores |
| **TM4C123** (EK-TM4C123GXL) | **Otra clase**: el SYSCTL antiguo (`RCC`/`RCC2`) y otros periféricos, pero la misma escuela. Muy usado en docencia |
| D5/G4 CMSIS-DAP por USB/IP | Depende del puente USB |
| `mcu-sim-gui` | Los dibujos de las LaunchPads y la EDUMKII. Comprobar que la ventana no da por hecho nombres de la forma `Pxn` sin punto |

Dado el estado comercial (§0), **no se recomienda pasar de los descriptores**
salvo petición concreta de un laboratorio.

### Orden y dependencias

```
T0 ──► T1 ──► T2 ──┬─► T3 ──► T4
                   ├─► T5 (OpenOCD, BSL)
                   └─► T6 (E4) ──► T7 ──► T8
mcu-sim-gui en paralelo desde T1
```

T5 puede ir en cualquier momento después de T2. Como sirve también al STM32,
tiene sentido adelantarla si hay demanda de OpenOCD.

**Relación con el plan del ESP32.** T0 y E0 comparten la mitad del trabajo. Se
recomienda **T0 → T1 → T2 antes que E1**: T0 se cierra contra un chip que
ejecuta el mismo núcleo, y cualquier fallo se ve sin el ruido de una
arquitectura nueva. Cuando llegue la E0, solo quedará partir `GdbRsp` por
arquitectura.

---

## 9. Riesgos y preguntas abiertas

1. **La imagen del BSL del P4.** Viene programada de fábrica en la Flash de
   información. Hay que averiguar si TI la distribuye (y con qué licencia) o
   si solo se puede volcar de una placa. Sin ella, D4 en el P4 se queda en «el
   arranque entra en el BSL y espera».
2. **CCS 20 (Theia) y GDB remoto.** Si no admite un servidor GDB genérico, el
   camino desde CCS es la versión 12.x o VS Code. Hay que comprobarlo antes de
   prometer nada en clase.
3. **El depurador nativo de CCS y UniFlash.** Solo con un XDS110 emulado. ¿Vale
   la pena emularlo? La respuesta depende de cuántos alumnos usen CCS sin GDB.
4. **El orden de las tablas de la ROM sustituta.** Un desplazamiento mal en una
   tabla llama a otra función sin avisar. Mitigación: generar las tablas **a
   partir de `rom.h`** con un guion, nunca a mano, y una prueba que llame a cada
   entrada y compruebe su efecto.
5. **El TLV sintético.** Con valores típicos, el DCO sale a la frecuencia
   nominal exacta. Una placa real no, y quien compare tiempos lo notará. Se
   documenta en `doc/todo.md`.
6. **¿La EMAC del E4 es la IP de Synopsys?** Si lo es, T7 se abarata mucho. Hay
   que confirmarlo con dos fuentes (TRM y cabeceras) antes de planificar.
7. **Las zonas IP protegidas y el bloqueo de JTAG del P4.** Son de las pocas
   cosas que un alumno puede usar para **inutilizar** una placa real. El modelo
   debería reproducirlas, porque es mejor equivocarse en el simulador. Pero
   cuestan, y de momento se declaran.
8. **El estado de la línea.** Sin productos nuevos, las cabeceras y el SDK no se
   actualizarán más. Eso es bueno para la estabilidad de las dos fuentes y malo
   para quien busque futuro. El plan está escrito para que **T0-T3 (P4) sea un
   resultado completo por sí solo**.
9. **El nombre del espacio de nombres.** Todo el código vive en `namespace
   stm32`. No estorba, pero chirriará. Se propone **no** renombrarlo dentro de
   este plan: es un cambio mecánico que conviene hacer aparte, con el CI como
   red.

---

## 10. Fuentes consultadas

* Repositorio `mcu-sim` (commit `ed413bd`): `README.md`, `src/README.md`,
  `doc/multi_mcu.md`, `doc/reutilizacion.md`, `doc/analisis_puente_usb.md`,
  `doc/analisis_puente_ethernet.md`, `doc/todo.md`, `cubeide/README.md`,
  `soc/mcu_if.h`, `soc/stm32f446.h`, `top/mcu_caps.h`, `core/core_caps.h`,
  `core/cortex_m4f.h`, `core/debug.h`, `core/gdb_stub_dap.h`,
  `common/gdb_rsp.h`, `common/ahb_types.h`, `pins/pin_mux.h`,
  `verif/image_loader.h`, y la prueba T125 en `top/sc_main.cpp`.
* `doc/analisis_esp32_riscv.md` (proyecto), por la fase 0 compartida.
* [OpenOCD, `src/flash/nor/msp432.c`](https://openocd.org/doc/doxygen/html/msp432_8c_source.html): familias que admite, algoritmo en RAM, registros de identificación (`0x0020_100C`, `0xE004_3020`, `DID0`/`DID1`) y sectores BSL de la Flash de información.
* [TI, SimpleLink MSP432P4 SDK, DriverLib `rom.h`](https://software-dl.ti.com/simplelink/esd/simplelink_msp432_sdk/3.40.00.05/docs/driverlib/msp432p4xx/html/rom_8h_source.html): `ROM_APITABLE = 0x02000800` y la tabla de tablas.
* [TI, XDS110 Debug Probe](https://software-dl.ti.com/ccs/esd/documents/xdsdebugprobes/emu_xds110.html): compatibilidad CMSIS-DAP, SWD/SWO y *backchannel* UART.
* [TI, XDS GDB Agent](https://software-dl.ti.com/ccs/esd/documents/xdsdebugprobes/emu_xds_gdb_agent.html): en mantenimiento; OpenOCD como opción preferida para Cortex.
* [TI, SLAU746, Bootloader for MSP432E4](https://ti.com/lit/slau746): interfaces y protocolo de paquetes del cargador.
* [TI E2E, «MSP432 boot loader»](https://e2e.ti.com/support/microcontrollers/msp-low-power-microcontrollers-group/msp430/f/msp-low-power-microcontroller-forum/536554/msp432-boot-loader): BSL en pieza virgen, pin de invocación, UART en P1.2/P1.3 y el TLV.
* [TI E2E, invocación del BSL por hardware en el MSP432P401R](https://e2e.ti.com/support/microcontrollers/msp-low-power-microcontrollers-group/msp430/f/msp-low-power-microcontroller-forum/849023/msp432p401r-doubt-in-msp432-bsl-hardware-invocation-sequence): buzón de la Flash.
* [SimpleLink Academy, MSP432E4 bootloader](https://dev.ti.com/tirex/explore/content/simplelink_academy_msp432e4sdk_3_30_01_00/modules/msp432/msp432e4_bootloader/msp432e4_bootloader.html): BSL Scripter con `MODE E4xx UART`.
* [TI, SLAU723A (TRM del MSP432E4), índice](https://www.ti.com/document-viewer/lit/html/SLAU723A/rom-romsect): contenido de la ROM.
* [Wikipedia, «TI MSP432»](https://en.wikipedia.org/wiki/TI_MSP432) y [foro de Digi-Key, «MSP432Pxx Family Discontinued?»](https://forum.digikey.com/t/msp432pxx-family-discontinued/16424): estado comercial y relación del E4 con el TM4C129.
* *Pendientes de incorporar a `doc/refs/` y a `doc/fuentes.md`, sin
  versionarlos:* SLAU356 (TRM del P4), SLAS826 (datasheet del P401R), SLAU723
  (TRM del E4), datasheets del E401Y/E411Y, SLAU622 (BSL del P4) y la guía de
  usuario de las dos LaunchPads.
* **Sin verificar todavía con dos fuentes**, y marcados así en el texto: el
  `n_irq` exacto de cada chip, la dirección de la tabla de ROM del E4, la
  referencia del SysTick en el P4, si la EMAC del E4 es la IP de Synopsys, las
  interfaces exactas del cargador en ROM del E4 y la compatibilidad de CCS 20
  con un GDB remoto.
