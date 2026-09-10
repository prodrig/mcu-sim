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
| **F4** | DMA1/2, USART, TIM, EXTI/SYSCFG | **completada** |
| **F5** | Resto de periféricos: SPI/I2S, I2C, ADC, DAC, RTC, watchdogs, SDIO, CRC/RNG y bxCAN | **completada** |
| **F6** | Debug: SWJ-DP/AHB-AP, Core Debug, FPB, DWT, ITM/TPIU, ROM table, DBGMCU y los dos servidores GDB/RSP | **completada** |
| **F7** (bajo consumo) | Sleep/Stop/Standby, PVD, WKUP, LPENR y modelo de consumo IDD | **completada** |
| **F7** (DCMI) | Interfaz de cámara: captura, recorte, sincronismo embebido y DMA | **completada** |
| **F7** (FSMC) | Bus externo: los cuatro bancos, el ciclo asíncrono en los pines, NAND y ECC | **completada** |
| **F7** (OTG) | USB doble rol: transceptor en los pines, endpoints, canales, FIFOs y el DMA del HS | **completada** |
| **F7** (ETH) | Ethernet 10/100: MII/RMII en los pines, MDIO, descriptores, filtrado, MMC y PTP | **completada** |
| F7 (resto) | afinado AT | pendiente |

`make test` compila y ejecuta la suite de verificación acumulada (1899
comprobaciones autocomprobables: 124 de F1 + 12 de F2 + 80 de F3 + 343 de F4
(DMA, UART/USART, TIM y EXTI/SYSCFG) + 675 de F5 (SPI/I2S, I2C, ADC, DAC, RTC y
perros guardianes, SDIO, CRC/RNG y bxCAN) + 151 de F6 (depuración y los dos
servidores GDB) + 426 de F7 (116 de bajo consumo, 61 del DCMI, 54 del FSMC,
113 del USB OTG y 82 del Ethernet) + 88 del netlist;
código de
salida 0 si todas pasan, en unos 23 s). Verificado con SystemC 2.3.4 / g++ 13 / C++17
y arm-none-eabi-gcc 13.2.

`make asan` corre esa misma suite con AddressSanitizer y UndefinedBehaviorSanitizer,
y hoy sale limpia: **0 fugas y 0 avisos**. No hay que poner `ASAN_OPTIONS` a
mano; el ejecutable trae su propia configuración, porque ASan sin
`detect_stack_use_after_return=0` es incompatible con las corrutinas de SystemC
y revienta antes de la primera comprobación (véase `common/asan_opciones.h`).

**Las dos hay que ejecutarlas desde `src/`**, que es lo que hace `make`. Las
imágenes de firmware se cargan por rutas relativas, y desde otro directorio
fallan diecisiete comprobaciones por un motivo que no tiene nada que ver con lo
que se estaba mirando.

Las pruebas T15-T17, T25, T31, T37, T43, T48, T55, T61, T66, T70, T79, T82, T88, T95 y T103 necesitan los firmwares del repositorio; se
compilan con `make -C verif/fw`, `make -C verif/fw/coremark`,
`make -C verif/fw/blinky`, `make -C verif/fw/dma_demo`,
`make -C verif/fw/uart_demo`, `make -C verif/fw/tim_demo`,
`make -C verif/fw/exti_demo`, `make -C verif/fw/spi_demo` y
`make -C verif/fw/i2c_demo`, `make -C verif/fw/adc_demo`,
`make -C verif/fw/dac_demo`, `make -C verif/fw/sdio_demo`,
`make -C verif/fw/crc_rng_demo`, `make -C verif/fw/can_demo`,
`make -C verif/fw/debug_demo` y `make -C verif/fw/lowpower_demo`
(requieren `arm-none-eabi-gcc`). Sin ellos, esas pruebas informan de que falta
la imagen. `F2_SKIP_COREMARK=1` omite la ejecución de CoreMark.

```
make -f Makefile.stm32 test           # o: cp Makefile.stm32 Makefile && make test
make -f Makefile.stm32 asan           # la misma suite con ASan + UBSan
make -f Makefile.stm32 run IMG=fw.bin # carga una imagen y simula
```

## Las tres plataformas

**Un solo Makefile.** La plataforma se detecta sola —por `OS=Windows_NT` o por
`uname`— y se puede forzar con una variable, que es lo único que hay que
ajustar:

```
make                                  # Linux o macOS, nativo
make PLATAFORMA=windows               # Windows con MinGW-w64 (MSYS2 o Git Bash)
make PLATAFORMA=windows CXX=x86_64-w64-mingw32-g++   # cruzado desde Linux
make plataforma                       # qué ha decidido: compilador, rutas, bibliotecas
```

Las tres variables que deciden todo son `PLATAFORMA` (`linux` | `macos` |
`windows`), `SYSTEMC_HOME` y `CXX`. `make plataforma` las imprime, y es lo
primero que hay que mirar cuando la compilación falla en una máquina nueva.

**Todo el modelo es C++17 y `<systemc>` salvo un fichero.** La única parte que
sabe en qué sistema operativo corre es `common/red.h`, que traduce entre los
sockets de Berkeley y Winsock: el descriptor es `int` y −1 en POSIX pero un
`SOCKET` **sin signo** en Windows —así que `if (s < 0)` allí es *siempre falso*
y el error se traga en silencio—, se cierra con `close` o con `closesocket`, y
el «ahora mismo no hay datos» se llama `EAGAIN` o `WSAEWOULDBLOCK`. Y una
tercera diferencia que no es de Windows sino de macOS: sin `MSG_NOSIGNAL`,
escribir en un socket que el otro extremo cerró manda un `SIGPIPE` que mataría
el proceso; allí se evita con `SO_NOSIGPIPE` al crear el socket.

Los dos usuarios de esa capa son los servidores de GDB (`common/gdb_rsp.h`) y el
cliente de RSP con el que la suite se prueba a sí misma
(`verif/gdb_client.h`). Ningún otro fichero de `src/` incluye una cabecera del
sistema.

```
make red        # compila y ejecuta 13 comprobaciones de la capa de red
make red PLATAFORMA=windows CXX=x86_64-w64-mingw32-g++    # solo compila y enlaza
```

`make red` no necesita SystemC, así que corre en cualquier sitio y sirve para
validar una plataforma nueva antes de pelearse con la biblioteca.

| Plataforma | Estado | Comprobado |
| :--- | :--- | :--- |
| Linux, g++ 13 | **verificado** | 1899/1899 comprobaciones, `make red` 13/13, ASan limpio |
| Linux, clang | **verificado** | 1899/1899, mismo tiempo simulado al picosegundo |
| Windows, MinGW-w64 | **compila y enlaza** (cruzado con g++ 13-win32) | `make red` genera un PE32+ sin avisos; **falta ejecutarlo en Windows y construir SystemC allí** |
| macOS, clang | **la rama específica compila** | Se fuerza la combinación de macOS —sin `MSG_NOSIGNAL`, con `SO_NOSIGPIPE`— y compila con g++ y con clang; **falta probarlo en un Mac** |

Lo que en Windows y macOS **no** está verificado es lo mismo en los dos casos:
construir la biblioteca de SystemC y ejecutar allí. El modelo no usa nada
exótico, pero eso no es una demostración.

En Windows, además, MinGW usa por omisión el `printf` de msvcrt, que **no
entiende `%llu`**, y el modelo lo usa veintiocho veces; el Makefile pasa
`-D__USE_MINGW_ANSI_STDIO=1` para arreglarlo. Y enlaza con
`-static-libgcc -static-libstdc++`, de modo que el `.exe` no necesita las DLL de
MinGW instaladas: para repartirlo, eso no es un lujo.

**Cuando la compilación falla**, `doc/compilacion.md` es la guía completa: qué se
puede ajustar sin tocar el `Makefile` (`SYSTEMC_HOME`, `CXXSTD`, `EXTRA`,
`EXTRA_LD`) y un catálogo de errores ordenado **por síntoma**, empezando por el
más común de todos —el `undefined reference` a `sc_api_version_…`, que no
significa que falte la biblioteca sino que se compiló con otras opciones que las
tuyas.

**El netlist de la placa.** Todo lo que se suelda fuera del encapsulado vive en
`parts/` y se DECLARA: nodos, instancias y conexiones nominales. No queda ni un
`new` de pieza externa en `sc_main.cpp`. Volcarlo no simula nada.

```
./build/stm32f407vg --netlist            # la placa DECLARADA: nodos, instancias,
                                         # parámetros, referencias y conexiones
./build/stm32f407vg --inventario         # la placa CONSTRUIDA, vista desde el modelo
./build/stm32f407vg --valida             # comprueba la placa sin simular
```

Los dos dan los mismos 43 componentes, y la suite (T121) comprueba en las dos
direcciones que dicen lo mismo. Son distintos porque el primero es lo que se
pidió construir —con sus parámetros y sus referencias entre componentes— y el
segundo lo que hay.

**El modelo con la placa en un fichero.** Los MCUs, la placa que diga el XML y el
firmware que se les pase. Para cambiar de placa no hay que recompilar.

```
make sim
./build/sim placa.xml [firmware.bin] [ms]   # simula
./build/sim placa.xml --ms=2                # el tiempo, sin firmware por delante
./build/sim placa.xml --valida              # solo comprueba la placa
./build/sim placa.xml --gdb --port=3333     # stub de GDB por los pines SWD
./build/sim placa.xml --gdb-dap             # o el stub interno contra el DAP
./build/sim --help                          # y los tipos que sabe construir
```

Antes de simular valida dos veces y sin simular: la DECLARACIÓN (nodo
inexistente, pad que este encapsulado no saca, identificador repetido,
referencia hacia delante, tipo desconocido) y lo ELÉCTRICO (dos piezas
conduciendo el mismo nodo, un nodo externo que nadie gobierna). Véase
`doc/stm32f407vg_parts_paso3.md`, y `doc/parts.md` para el catálogo de
componentes con sus parámetros.

**Varios MCUs.** Una placa puede declarar los chips que lleva, cada uno con su
firmware, su modo de depuración y su puerto de GDB:

```xml
<mcu tipo="STM32F407VG" id="u0" depuracion="dap"   puerto_gdb="3333"/>
<mcu tipo="STM32F407VG" id="u1" depuracion="pines" puerto_gdb="3334"/>
```

Sin ningún `<mcu>` la placa lleva un STM32F407VG implícito y los nodos se llaman
`PD12`, que es como han sido todas hasta ahora. Con uno declarado valen los dos
nombres, `PD12` y `u0.PD12`. **Con dos o más solo vale el cualificado**, y cada
chip lleva lo suyo en el XML: un firmware o un puerto sueltos en la línea de
órdenes ya no dicen a cuál y se rechazan nombrando los MCUs.

Con algún stub escuchando, `sim` no se detiene solo: se pueden abrir dos
sesiones de GDB a la vez, una por chip y cada una en su puerto. `placas/dos_mcu.xml`
es esa placa —dos F407 hablando por I2C—; la comparación entre los dos modos de
depuración y el resto está en `doc/stm32f407vg_multi_mcu.md`, §5.

**Puentes entre pines.** Dos pines pueden ser el MISMO punto eléctrico, no dos
puntos parecidos:

```xml
<nodo id="n_puente" externo="si" une="PB9 PD3"/>
```

Los pads que `une` nombra dejan de crear su propio `AnalogNet` y comparten este,
así que la superposición los resuelve juntos: bidireccional, sin retardo, y si
los dos conducen a la vez el conflicto sale —media tensión y sobrecorriente en
los dos pads—. Hay que declararlo porque el pad ata su nodo a un `sc_port` en el
constructor del MCU y un `sc_port` no se reata; por eso el fichero se lee ANTES
de construir el MCU. Un puente es permanente: para un enlace unidireccional que
haya que soldar y despegar entre pruebas está `SignalLink`. El banco lleva el
puente PB9–PD3 y lo comprueba T122; la comparación entre las dos formas está en
`doc/stm32f407vg_multi_mcu.md`, §4.5.

En `placas/` hay cuatro:

| Fichero | Qué es |
| :--- | :--- |
| `discovery_min.xml` | Lo mínimo: cristal, LED y pulsador |
| `led_azul_5v.xml` | Un LED azul de 3,0 V colgado de 5 V con el cátodo al pin |
| `banco.xml` | La placa entera de la suite: 43 componentes de 20 tipos |
| `dos_mcu.xml` | Dos STM32F407 hablando por I2C, cada uno con su puerto de GDB |

`banco.xml` está **generado** por el propio modelo y versionado a propósito. Se
regenera con

```
./build/stm32f407vg --netlist > placas/banco.xml
```

y como el volcado es determinista, `git diff --exit-code placas/banco.xml`
después de regenerarlo dice si la placa del banco ha cambiado sin querer. Es la
única forma de ver ese cambio: en `sc_main.cpp` está repartido por el bloque de
elaboración, y aquí sale en una línea de diff.

**Depuración interactiva desde un IDE.** El modelo lleva DOS servidores
GDB/RSP, con el mismo protocolo y las mismas respuestas, que se diferencian
solo en cómo llegan al DAP:

```
./build/stm32f407vg --gdb     [--port=3333] [imagen.bin]   # por los pines SWD
./build/stm32f407vg --gdb-dap [--port=3333] [imagen.bin]   # por dentro, al DAP
```

Ninguno de los dos ejecuta la suite: levantan el modelo y abren el puerto.
Desde Eclipse CDT, STM32CubeIDE o un `arm-none-eabi-gdb` a pelo, `target
extended-remote localhost:3333` da una sesión completa —descarga a Flash
incluida— igual que contra una placa con un ST-LINK.

- `--gdb` es la SONDA (`verif/gdb_stub.h`): se suelda a PA13/PA14 y habla SWD
  bit a bit, con su reset de línea, sus ACK y su paridad. Es el modo fiel, y el
  único que verifica el propio protocolo de transporte.
  Véase `doc/stm32f407vg_fase6_gdb.md`.
- `--gdb-dap` es el stub INTERNO (`core/gdb_stub_dap.h`): el núcleo reserva los
  cinco pines de depuración (PA13/14/15, PB3-SWO y PB4) sin usarlos y crea
  dentro de sí un stub pegado al AHB-AP. Un acceso pasa de ~100 flancos de
  SWCLK a una transacción TLM: **x390** en tiempo simulado (medido en T97).
  El criterio para elegir uno u otro está en
  `doc/stm32f407vg_fase6_gdb2.md`.

La elección es un parámetro del núcleo, no una opción del banco:
`CortexM4F core{"core", DBG_PINES}` o `DBG_INTERNO`, con los alias
`CortexM4F_Pines` y `CortexM4F_GdbDap` para fijarlo en tiempo de compilación.

## Estructura

| Carpeta | Contenido |
| :--- | :--- |
| `common/` | Tipos de bus y extensión AHB, mapa de memoria, sectores de Flash, tabla de estados de espera y los cuatro modos de energía (`ahb_types.h`); nodo analógico de pin (`analog_net.h`); generador de reloj reprogramable (`clock_gen.h`); clase base de esclavo con byte enables y respuestas AHB (`periph_base.h`); el motor del Remote Serial Protocol de GDB, compartido por los dos stubs y con el transporte como interfaz virtual (`gdb_rsp.h`) |
| `pins/` | `pad.h` (frontera V/I float <-> digital, Schmitt con histéresis, open-drain, pulls, rango y corriente), `pin_mux.h` (pads + mux AF + ruta analógica), `af_types.h` (tipos del mux), `power_pads.h` (VDD/NRST/BOOT0, POR/PDR/BOR y la CARGA que el MCU presenta sobre VDD y VBAT según su modo de energía, medible en float con su caída de tensión) |
| `bus/` | `ahb_matrix.h` (8x7 con máscara de conectividad y arbitraje), `ahb_decoder.h` (decodificadores de segmento + puente AHB-APB), `bitband.h` (alias de bit-banding) |
| `mem/` | `flash_if.h` (Flash 1 MB + ART + registros FLASH + option bytes + cargador), `sram.h` (SRAM1/2, BKPSRAM, CCM) |
| `rcc/` | `rcc.h` (banco de registros, árbol de reloj, gating, controlador de reset), `osc_pll.h` (HSI/HSE/LSI/LSE, PLL, PLLI2S) |
| `core/` | `cortex_m4f.h` (router I/D/S/CCM/PPB con alias de 0x0 y bit-banding), `cpu.h` (bucle fetch/decode/execute, excepciones, prebúsqueda), `cpu_state.h` (RegFile y utilidades arquitectónicas), `cpu_exec16.h` / `cpu_exec32.h` (ISA completa [II]), `fpu.h` (FPv4-SP), `scs.h` (SCB + NVIC + SysTick + MPU), `debug_if.h` (el contrato por el que la CPU llama al depurador en cada búsqueda, cada acceso y cada excepción) y `debug.h` (el subsistema CoreSight completo: SWJ-DP con el protocolo SWD a nivel de bit, AHB-AP, Core Debug, FPB, DWT, ITM/TPIU con salida por SWO, ROM table y DBGMCU; véase `doc/stm32f407vg_fase6_debug.md`) y `gdb_stub_dap.h` (el segundo servidor GDB, el que se engancha al DAP por dentro cuando el núcleo se construye con `DBG_INTERNO`; véase `doc/stm32f407vg_fase6_gdb2.md`) |
| `periph/` | un fichero por familia de periférico, todos derivados de `BusSlave`. `usart.h` es un único modelo parametrizado del que salen los tipos `Usart` y `Uart` (véase `doc/stm32f407vg_fase4_uart.md`), `timers.h` uno del que salen los seis tipos de temporizador del F407 (véase `doc/stm32f407vg_fase4_tim.md`), `spi.h` uno del que salen las cinco instancias de SPI/I2S, incluidos los bloques de extensión I2SxEXT (véase `doc/stm32f407vg_fase5_spi.md`), `i2c.h` uno del que salen los tres I2C/SMBus, que en el F407 resultan ser idénticos (véase `doc/stm32f407vg_fase5_i2c.md`), `adc.h` uno del que salen los tres convertidores, que en cambio SÍ se diferencian —entradas internas, papel de maestro y canales disponibles— (véase `doc/stm32f407vg_fase5_adc.md`), `dac.h` uno del que salen las variantes de uno y dos canales (véase `doc/stm32f407vg_fase5_dac.md`), `rtc.h` y `watchdog.h` los periféricos de sistema que sobreviven al reset o al fallo del reloj (véase `doc/stm32f407vg_fase5_rtc_wdog.md`), `sdio.h` el bloque de tarjetas SD/SD I/O/MMC, modelado a nivel de bit sobre los nueve pines del bus (véase `doc/stm32f407vg_fase5_sdio.md`), `crc_rng.h` la unidad de cálculo CRC y el generador de números aleatorios, este último con su fuente de ruido y sus dos condiciones de error (véase `doc/stm32f407vg_fase5_crc_rng.md`), `can.h` uno del que salen los dos bxCAN, que se diferencian en algo esencial —CAN1 es el dueño de los 28 bancos de filtros y CAN2 no tiene ventana de filtros propia— (véase `doc/stm32f407vg_fase5_can.md`), `dcmi.h` uno del que salen las cuatro variantes del interfaz de cámara, cuyo eje principal no es un bit del registro sino cuántos de los catorce hilos de datos tiene cableados el encapsulado (véase `doc/stm32f407vg_fase7_dcmi.md`), `fsmc.h` el controlador del bus externo, que es el primer periférico cuyos rasgos son POR BANCO y no del bloque entero —los cuatro bancos del FSMC no comparten ni el mapa de registros: BCR/BTR/BWTR el 1, PCR/SR/PMEM/PATT/ECCR los NAND y PIO4 el de PC Card— y cuyo eje decisivo vuelve a ser el encapsulado: sin A0-A15 ni NE2-NE4, este chip solo puede usar el bus multiplexado (véase `doc/stm32f407vg_fase7_fsmc.md`), `otg.h` los dos controladores USB On-The-Go, que son el caso mas extremo de rasgos por instancia del proyecto —el FS y el HS se diferencian en el bus, en el transceptor, en el tamaño de la RAM de FIFOs, en cuantos endpoints y canales gobiernan, en si son MAESTROS de la matriz y hasta en cuantas IRQ sacan— y donde ademas la palabra «canal» significa tres cosas distintas: las dos instancias (que no son copias), los canales de anfitrion (que si lo son) y los endpoints de dispositivo (donde el EP0 tiene otra forma de registro) (véase `doc/stm32f407vg_fase7_otg.md`), `eth_mac.h` el Ethernet MAC 10/100, donde «canal» vuelve a significar tres cosas distintas —las dos interfaces fisicas MII y RMII, que no son dos modos sino dos CAMINOS con distinto numero de hilos y de relojes; los dos anillos del DMA, cuyos descriptores comparten los nombres de los campos y ninguno de sus significados; y los cuatro filtros de direccion, donde el 0 no tiene ni habilitacion ni mascara de bytes— (véase `doc/stm32f407vg_fase7_eth.md`), y `pwr.h`, que no es un periférico más sino el ÁRBITRO DE LA ENERGÍA: decide en cuál de los cuatro modos está el MCU, ordena al RCC parar los relojes o apagar el dominio de 1,2 V, vigila VDD con el PVD y calcula la corriente que el chip pide por sus pines (véase `doc/stm32f407vg_fase7_lowpower.md`) |
| `parts/` | LIBRERÍA DE COMPONENTES EXTERNOS al MCU. `part_factory.h` (el registro `cadena → creador`, con macro de auto-registro: el único sitio donde un nombre escrito por una persona se convierte en un objeto), `xml_min.h` (lector de XML mínimo y ESTRICTO, sin dependencias: lo que no entiende es un error con línea y columna, nunca una suposición), `netlist_xml.h` (del árbol XML al netlist), `netlist.h` (el netlist en memoria: NODOS con nombre de esquemático —`PA5`, `VDD`, o creados para lo que no es un pin—, INSTANCIAS con tipo, identificador, parámetros y REFERENCIAS a otras instancias, y CONEXIONES nominales terminal→nodo; construye antes de `sc_start` porque la elaboración de SystemC es estática, valida la declaración sin simular y vuelca a XML), `netlist_parts.h` (el catálogo: una entrada de factoría por tipo, más los ayudantes tipados con los que se monta una placa desde C++), `part_base.h` (`ExtPartBase`: terminales con NOMBRE —no por posición del constructor—, un único `set_enabled(bool)` para soldar y desoldar, inventario global de piezas y volcado del netlist en XML; véase `doc/stm32f407vg_parts_paso1.md`), `ext_parts.h` (circuitería externa de placa: cristal, reloj, LED, pulsador, resistencia, driver, pista entre pines, hilo de bus I2C con pull-up, EEPROM 24Cxx, maestro I2C externo, tarjeta SD a nivel de pin, el bus CAN completo —hilo cableado en Y con su terminador, transceptores y un nodo CAN externo que habla el protocolo—, una sonda SWD que habla el protocolo bit a bit por PA13/PA14, un analizador de traza SWO colgado de PB3 y un sensor de imagen CMOS que genera cuadros enteros sobre PIXCLK/HSYNC/VSYNC y doce hilos de datos, con sincronismo por hardware o embebido, una SRAM asíncrona de 64 K que se suelda al bus externo del FSMC —engancha la dirección baja de los propios hilos de datos con NL y guarda en el flanco de subida de NWE, byte a byte según NBL0/NBL1— y una NAND de 16 páginas que entiende mandatos, direcciones y datos por CLE/ALE, y los dos extremos de un cable USB —un PC que da los 5 V de VBUS, pone los dos 15 kohm a masa y hace el reset con un SE0 largo, y un aparato que declara su existencia con el 1,5 kohm de D+ y se entera del reset porque lo VE— y el PHY de Ethernet, que pone los relojes del camino de datos, habla MDIO bit a bit con sus registros de la norma y hace de buzon de tramas con su CRC-32) |
| `verif/` | `bus_test_master.h` (maestro de bus de verificación), `image_loader.h` (carga de .bin/.hex y tabla de vectores), `decoder_vectors.h` (254 vectores generados desde `doc/valida_instrucciones.py` por `gen_decoder_vectors.py`), `swd_port.h` (el maestro SWD a nivel de bit, con el tratamiento de WAIT/FAULT y demás peculiaridades de ADIv5), `gdb_stub.h` (el servidor GDB/RSP de los pines: solo el transporte, porque el protocolo vive en `common/gdb_rsp.h`) y `gdb_client.h` (un GDB de mentira para verificarlo), `fw/` (firmware de autocomprobación, *port* bare-metal de CoreMark, CMSIS oficial, blinky de referencia y demostraciones del DMA, de los puertos serie, de los temporizadores, del EXTI, del SPI, del I2C, del ADC, del DAC, del SDIO, del CRC/RNG, del bxCAN y de la traza ITM/DWT) |
| `top/` | `stm32f407vg.h` + `stm32f407vg_bind2.h` (netlist/contrato de integración), `sc_main.cpp` (suite de verificación acumulada F1-F4) |

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
* **Detección asíncrona:** el EXTI no muestrea sus entradas con un reloj: su
  detector de flanco es un `SC_METHOD` sensible a las señales de pin, de modo
  que —como en el silicio— reconoce pulsos más cortos que un ciclo y sigue
  funcionando con los relojes parados.
* **Frontera del MCU:** el modelo termina en los `AnalogNet` de los pines.
  Cualquier componente de placa (cristal, LED, pulsador) es del banco de
  pruebas y vive en `parts/ext_parts.h`.
