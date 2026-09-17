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

`make test` compila y ejecuta la suite de verificación acumulada (**2045**
comprobaciones autocomprobables, y `make test446` las **43** del segundo chip —
véase más abajo—: 127 de F1 + 12 de F2 + 80 de F3 + 343 de F4
(DMA, UART/USART, TIM y EXTI/SYSCFG) + 675 de F5 (SPI/I2S, I2C, ADC, DAC, RTC y
perros guardianes, SDIO, CRC/RNG y bxCAN) + 151 de F6 (depuración y los dos
servidores GDB) + 426 de F7 (116 de bajo consumo, 61 del DCMI, 54 del FSMC,
113 del USB OTG y 82 del Ethernet) + 121 del netlist;
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

## El segundo chip: STM32F446RE

Desde la fase 2 del plan de `doc/stm32f407vg_vs_446re.md` hay **dos familias**
montables, y `tipo=` en el XML despacha de verdad entre ellas:

```
./build/sim placas/nucleo_f446re.xml --valida
./build/sim placas/nucleo_f446re.xml verif/fw/blinky446/blinky446.bin 700
make test446          # la suite del F446, que es un ejecutable APARTE
```

El F446RE es **el die de la familia F4 menos lo que no lleva**: sin Ethernet,
sin RNG, sin CCM, sin los bloques de extensión del I2S, con 512 KB de Flash, con
su propio LQFP64 —50 E/S, que no son las 51 del LQFP64 del F405RG: falta PB11—,
con 97 posiciones de vector, siete maestros en la matriz y otro `DBGMCU_IDCODE`.
Lo que un bloque ausente tiene ahí no es un periférico apagado: es **espacio
reservado**, y tocarlo da error de bus.

**Está a medias y lo dice.** `sim` imprime, al montar la placa, la lista de lo
que el modelo todavía no hace —el árbol de reloj del F446 es aún el del F407, y
los seis periféricos nuevos no están—, con la fase del plan en la que le toca a
cada cosa. Un modelo incompleto no es un problema; uno que no lo dice, sí.

**Su suite es otro ejecutable**, y no unas pruebas más en `sc_main.cpp`, porque
construir un segundo chip dentro del banco del F407 **mueve su tiempo simulado**:
un chip no es inerte. Dos chips en la misma simulación son una placa de dos
chips.

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
| Linux, g++ 13 | **verificado** | 2045/2045 comprobaciones, 43/43 del F446, `make red` 13/13, ASan limpio |
| Linux, clang | **verificado** | mismo resultado y mismo tiempo simulado al picosegundo |
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
./build/sim placa.xml --mcu TIPO            # el MCU implicito (STM32F407VG)
./build/sim --help                          # y los tipos que sabe construir
```

Antes de simular valida dos veces y sin simular: la DECLARACIÓN (nodo
inexistente, pad que este encapsulado no saca, identificador repetido,
referencia hacia delante, tipo desconocido) y lo ELÉCTRICO (dos piezas
conduciendo el mismo nodo, un nodo externo que nadie gobierna). Véase
`doc/stm32f407vg_parts_paso3.md`, y `doc/parts.md` para el catálogo de
componentes con sus parámetros.

### Que no termine hasta que tú lo digas

Por omisión `sim` simula **100 ms y para**. Eso está bien para mirar una placa,
y no para trabajar con ella: lo normal es querer el simulador encendido mientras
tú programas, depuras o pulsas cosas, y que se apague cuando lo digas tú.

**La forma de hacerlo es pedir un stub de GDB.** Cualquiera de las tres opciones
sirve, porque lo que hace que no termine es que haya un puerto escuchando:

```
./build/sim placa.xml --gdb                 # sonda SWD por PA13/PA14, puerto 3333
./build/sim placa.xml --gdb-dap             # el stub interno contra el DAP
./build/sim placa.xml --port=3333           # implica --gdb
./build/sim placa.xml fw.bin --gdb          # lo normal: con firmware dentro
```

Y lo dice al arrancar:

```
[gdb] escuchando en localhost:3333
esperando a GDB; la simulacion no se detiene sola (Ctrl-C para salir)
```

A partir de ahí el tiempo simulado avanza indefinidamente y **se sale con
Ctrl-C**. No hace falta que GDB llegue a conectarse: el puerto se abre y la
simulación corre igual, así que `--gdb` vale también como «déjalo andando». Es
justo el modo de uso al que apunta el objetivo del proyecto —tener el simulador
esperando mientras STM32CubeIDE compila y se engancha—, y es el mismo modo en el
que dos chips se depuran a la vez, cada uno en su puerto.

**Tres consecuencias que conviene saber antes de dejarlo encendido:**

**1. Se salta el informe final.** El resumen de los LEDs se imprime cuando
termina la ventana de `--ms`, y en este modo no termina nunca. Con un stub, lo
que se ve por dentro se ve por GDB.

**2. Sin freno, corre tan deprisa como pueda y se come un núcleo.** Para eso
está `--tiempo-real`, que ata el avance simulado al reloj de pared:

```
./build/sim placa.xml --gdb --tiempo-real      # un segundo por segundo
./build/sim placa.xml --tiempo-real=4          # cuatro veces más rápido
./build/sim placa.xml --tiempo-real=0.5        # a la mitad, para mirar despacio
```

Medido: 2000 ms simulados salen en **0,033 s** sin freno, **2,000 s** con
`--tiempo-real` y **0,500 s** con `=4`; y esperando a GDB la CPU baja de
**99,6 % a 5,3 %**, porque dormir es dormir. Sin él, un LED que parpadea a 1 Hz
parpadea doscientas veces por segundo y no hay nada que mirar. El freno **solo
frena**: si el modelo va más lento que el tiempo real, sigue sin dormir y sin
acumular deuda.

**3. Redirigido a un fichero, los mensajes se pierden al matarlo.** En un
terminal la salida es línea a línea y se ve todo; con `> log.txt` es por bloques,
y un Ctrl-C se lleva lo que quedara en el buffer.

**Lo que NO es una forma de conseguirlo:** poner un `--ms` enorme. Termina igual,
solo que más tarde, no atiende a nada mientras tanto y —con el núcleo dormido o
en un bucle— llega al final en un suspiro: `--ms=10000` cuesta lo mismo que
`--ms=1` porque el coste va con los sucesos, no con el tiempo.

### Conectar un IDE: qué tipo de sonda elegir

**Esto no es un ST-LINK, es un servidor de GDB.** El stub habla el protocolo
remoto de GDB (RSP) por TCP, que es lo que habla `arm-none-eabi-gdb`; no habla
USB, ni el protocolo propietario de la sonda ST-LINK, ni el de su servidor.
Cualquier IDE que sepa «conéctate a un GDB remoto en este puerto» funciona.

**En STM32CubeIDE hay que usar `GDB Hardware Debugging`, no `ST-LINK`.**
Elegir `ST-LINK (ST-LINK GDB server)` y apuntarlo a `localhost:3333` falla con
un mensaje que despista mucho:

```
Could not verify ST device! Please verify that the latest version of
the GDB-server is used for the connection
```

No lo dice GDB: lo dice la capa de ST, que da por hecho que al otro lado está
*su* servidor hablando con una sonda ST-LINK de verdad y comprueba que así sea.
No lo está, así que corta. En el simulador se ve exactamente eso —`cliente
conectado` seguido de `cliente desconectado`— y **no es un fallo del stub**:
respondiendo desde un cliente RSP crudo, `qSupported`, `?`, `g`, `m` y
`qXfer:features:read:target.xml` contestan bien, y `m e0042000,4` devuelve
`13640110`, o sea el `DBGMCU_IDCODE` correcto `0x10016413` —DEV_ID `0x413`
(STM32F405/407/415/417), REV_ID `0x1001`—. El identificador está bien; lo que no
encaja es el tipo de sonda.

La configuración que sí vale, en **Run → Debug Configurations → GDB Hardware
Debugging**:

| Pestaña | Campo | Valor |
| :--- | :--- | :--- |
| Main | C/C++ Application | el `.elf` del proyecto |
| Debugger | GDB Command | el `arm-none-eabi-gdb` que trae CubeIDE |
| Debugger | Use remote target | ✔ |
| Debugger | JTAG Device | `Generic TCP/IP` |
| Debugger | Host / Port | `localhost` / `3333` |
| Startup | Reset and Delay, Halt | desmarcados (los hace `monitor reset` / `monitor halt`) |
| Startup | Load image, Load symbols | ✔ — el stub implementa `vFlashErase` / `vFlashWrite` |

**Compruébalo antes por la línea de órdenes**, que separa los dos problemas en
diez segundos: si esto funciona, el stub está bien y lo que falta es ajustar el
IDE.

```
arm-none-eabi-gdb tu_programa.elf
(gdb) target remote localhost:3333
(gdb) monitor reset
(gdb) load
(gdb) break main
(gdb) continue
```

**Más corto todavía: `cubeide/simulador.launch`.** Es esa misma configuración ya
hecha, para importar con *File → Import… → Run/Debug → Launch Configurations*;
solo hay que sustituir `TU_PROYECTO` por el nombre del proyecto y la ruta de su
`.elf`. Véase `cubeide/README.md`.

**Qué es exactamente lo que rechaza la sonda de ST.** Con la traza puesta se ve
que la sesión no muere por nada de GDB: `qSupported`, `target.xml`, `?`, `g` y
las lecturas de memoria se responden bien, y el último paquete antes del adiós
(`D`) es una orden `monitor` propia de ST:

```
monitor ReadAPEx 0x0 0xF8
```

—leer el registro `BASE` del AP 0, el puntero a la ROM table—. **El stub la
responde** desde ahora, y también `WriteAPEx`, con el valor en texto
(`0xE00FF003`); pero **el formato exacto que ST espera no está publicado**, así
que eso es una conjetura razonada y no un hecho verificado. Si el IDE arrancara
con la sonda de ST, mejor; mientras no se confirme, la configuración de arriba
es la que se sabe que funciona.

**Y si aun así se desconecta, la traza dice por qué:**

```
./build/sim placa.xml --gdb --traza-gdb
```

imprime cada paquete RSP que llega, y **el último antes de `cliente
desconectado` es el que no le gustó al otro extremo**. Un `+$#00` —respuesta
vacía— a un paquete que el cliente considere obligatorio es la forma habitual de
que una sesión se caiga sin más explicación.

**Varios MCUs.** Una placa puede declarar los chips que lleva, cada uno con su
firmware, su modo de depuración y su puerto de GDB:

```xml
<mcu tipo="STM32F407VG" id="u0" depuracion="dap"   puerto_gdb="3333"/>
<mcu tipo="STM32F407VG" id="u1" depuracion="pines" puerto_gdb="3334"/>
```

Sin ningún `<mcu>` la placa lleva un STM32F407VG implícito y los nodos se llaman
`PD12`, que es como han sido todas hasta ahora. **`--mcu TIPO` cambia el tipo de
ese implícito** —`--mcu stm32f407vg`, en mayúsculas o minúsculas— y **no pisa lo
que diga el XML**: una placa que declara sus chips ya ha dicho cuáles son, y la
línea de órdenes no tiene por qué saberlo mejor.

Con un tipo que el programa no modele, el error lo dice y lista los que hay:

```
mcu (implicito): no se sabe construir un 'STM32F446RE'.
Los tipos que este programa modela son: STM32F407VG.
Un MCU distinto no es un parametro: es otro modelo, con su mapa de memoria,
sus perifericos y su encapsulado.
```

Que es la situación de hoy: **el único MCU modelado es el STM32F407VG**. La
opción existe para que el día que haya un segundo no haya que cambiar la interfaz
—y para que entre tanto el fallo sea claro en vez de silencioso—. Con uno declarado valen los dos
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
| `discovery_min.xml` | La **STM32F4DISCOVERY** entera: HSE de 8 MHz, LSE **declarado y desoldado** (como el zócalo vacío de la tarjeta), los cuatro LEDs, el pulsador azul (a VDD) y el negro de reset (a NRST), BOOT0/BOOT1 y los pines de depuración |
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
| `common/` | Tipos de bus y extensión AHB, las regiones de la ARQUITECTURA ARMv7-M (código, SRAM, periférico, bit-banding, PPB) y los cuatro modos de energía (`ahb_types.h`, que desde la fase 1 del plan del F446 **ya no contiene nada del F407**: las bases de los periféricos, la tabla de sectores y los topes de frecuencia viven en `soc/f4_mapa_perif.h`, que se incluye desde allí para que ningún fichero tuviera que cambiar su `#include`); nodo analógico de pin (`analog_net.h`); generador de reloj reprogramable (`clock_gen.h`); clase base de esclavo con byte enables y respuestas AHB (`periph_base.h`); el motor del Remote Serial Protocol de GDB, compartido por los dos stubs y con el transporte como interfaz virtual (`gdb_rsp.h`) |
| `pins/` | `encapsulado.h` (EL ENCAPSULADO COMO DATO: una máscara de 16 bits por puerto que dice pin a pin qué sale al plástico, con los seis de la familia —LQFP64, WLCSP90, LQFP100, LQFP144, LQFP176 y UFBGA176— y el recuento de E/S del datasheet como contraste; era una función estática y era el obstáculo que impedía describir los otros diez miembros), `pad.h` (frontera V/I float <-> digital, Schmitt con histéresis, open-drain, pulls, rango y corriente), `pin_mux.h` (pads + mux AF + ruta analógica), `af_types.h` (tipos del mux), `power_pads.h` (VDD/NRST/BOOT0, POR/PDR/BOR y la CARGA que el MCU presenta sobre VDD y VBAT según su modo de energía, medible en float con su caída de tensión) |
| `bus/` | `conectividad.h` (QUÉ MAESTRO ALCANZA A QUÉ ESCLAVO, en su propio fichero porque es un rasgo del chip y lo lleva el descriptor: `CONN_STM32F407VG` con ocho maestros y `CONN_STM32F446` con siete, que es la misma tabla con la fila del DMA del Ethernet a cero), `ahb_matrix.h` (8x7 con la CONECTIVIDAD COMO DATO —`Conectividad`, una fila de máscara por maestro, que se pasa al constructor igual que el `Encapsulado` o el `MapaRam`: una fila a cero es un maestro que ese chip no tiene, y el F446, con siete maestros, es el mismo módulo con otra tabla— y arbitraje; decodifica los rangos de RAM del `MapaRam` con el que se construye), `ahb_decoder.h` (decodificadores de segmento + puente AHB-APB), `bitband.h` (alias de bit-banding) |
| `mem/` | `mem_caps.h` (los RASGOS de las memorias —tamaños, tabla de sectores de la Flash y curva de estados de espera— como DATO y no como constante global: es lo que permite montar el mismo controlador con 256 KB y seis sectores; véase `doc/stm32f407vg_reutilizacion.md`), `flash_if.h` (Flash + ART + registros FLASH + option bytes + cargador, dimensionada por su `MapaFlash`), `sram.h` (SRAM1/2, BKPSRAM, CCM, todas con base y tamaño por constructor) |
| `rcc/` | `rcc.h` (banco de registros, árbol de reloj, gating, controlador de reset), `osc_pll.h` (HSI/HSE/LSI/LSE, PLL, PLLI2S) |
| `core/` | `core_caps.h` (los RASGOS del núcleo: cuántas líneas de interrupción, cuántos bits de prioridad, cuántas regiones de MPU y qué FPU. Es lo que ST decide al integrar un Cortex-M4F con licencia, y lo que distingue a un F407 de otro chip del mismo núcleo; `Scs`, `Mpu` y `CortexM4F` lo toman por constructor con el F407 por omisión), `cortex_m4f.h` (router I/D/S/CCM/PPB con alias de 0x0 y bit-banding), `cpu.h` (bucle fetch/decode/execute, excepciones, prebúsqueda), `cpu_state.h` (RegFile y utilidades arquitectónicas), `cpu_exec16.h` / `cpu_exec32.h` (ISA completa [II]), `fpu.h` (FPv4-SP), `scs.h` (SCB + NVIC + SysTick + MPU), `debug_if.h` (el contrato por el que la CPU llama al depurador en cada búsqueda, cada acceso y cada excepción) y `debug.h` (el subsistema CoreSight completo: SWJ-DP con el protocolo SWD a nivel de bit, AHB-AP, Core Debug, FPB, DWT, ITM/TPIU con salida por SWO, ROM table y DBGMCU; véase `doc/stm32f407vg_fase6_debug.md`) y `gdb_stub_dap.h` (el segundo servidor GDB, el que se engancha al DAP por dentro cuando el núcleo se construye con `DBG_INTERNO`; véase `doc/stm32f407vg_fase6_gdb2.md`) |
| `periph/` | un fichero por familia de periférico, todos derivados de `BusSlave`. `usart.h` es un único modelo parametrizado del que salen los tipos `Usart` y `Uart` (véase `doc/stm32f407vg_fase4_uart.md`), `timers.h` uno del que salen los seis tipos de temporizador del F407 (véase `doc/stm32f407vg_fase4_tim.md`), `spi.h` uno del que salen las cinco instancias de SPI/I2S, incluidos los bloques de extensión I2SxEXT (véase `doc/stm32f407vg_fase5_spi.md`), `i2c.h` uno del que salen los tres I2C/SMBus, que en el F407 resultan ser idénticos (véase `doc/stm32f407vg_fase5_i2c.md`), `adc.h` uno del que salen los tres convertidores, que en cambio SÍ se diferencian —entradas internas, papel de maestro y canales disponibles— (véase `doc/stm32f407vg_fase5_adc.md`), `dac.h` uno del que salen las variantes de uno y dos canales (véase `doc/stm32f407vg_fase5_dac.md`), `rtc.h` y `watchdog.h` los periféricos de sistema que sobreviven al reset o al fallo del reloj (véase `doc/stm32f407vg_fase5_rtc_wdog.md`), `sdio.h` el bloque de tarjetas SD/SD I/O/MMC, modelado a nivel de bit sobre los nueve pines del bus (véase `doc/stm32f407vg_fase5_sdio.md`), `crc.h` la unidad de cálculo CRC y `rng.h` el generador de números aleatorios, con su fuente de ruido y sus dos condiciones de error (dos ficheros desde la fase 1 del plan del F446, porque no son un periférico con dos mitades sino dos periféricos que la documentación agrupó: el F446 lleva el primero y no el segundo. `crc_rng.h` sigue existiendo como inclusión de los dos, para que nada que lo usara tuviera que cambiar; véase `doc/stm32f407vg_fase5_crc_rng.md`), `can.h` uno del que salen los dos bxCAN, que se diferencian en algo esencial —CAN1 es el dueño de los 28 bancos de filtros y CAN2 no tiene ventana de filtros propia— (véase `doc/stm32f407vg_fase5_can.md`), `dcmi.h` uno del que salen las cuatro variantes del interfaz de cámara, cuyo eje principal no es un bit del registro sino cuántos de los catorce hilos de datos tiene cableados el encapsulado (véase `doc/stm32f407vg_fase7_dcmi.md`), `fsmc.h` el controlador del bus externo, que es el primer periférico cuyos rasgos son POR BANCO y no del bloque entero —los cuatro bancos del FSMC no comparten ni el mapa de registros: BCR/BTR/BWTR el 1, PCR/SR/PMEM/PATT/ECCR los NAND y PIO4 el de PC Card— y cuyo eje decisivo vuelve a ser el encapsulado: sin A0-A15 ni NE2-NE4, este chip solo puede usar el bus multiplexado (véase `doc/stm32f407vg_fase7_fsmc.md`), `otg.h` los dos controladores USB On-The-Go, que son el caso mas extremo de rasgos por instancia del proyecto —el FS y el HS se diferencian en el bus, en el transceptor, en el tamaño de la RAM de FIFOs, en cuantos endpoints y canales gobiernan, en si son MAESTROS de la matriz y hasta en cuantas IRQ sacan— y donde ademas la palabra «canal» significa tres cosas distintas: las dos instancias (que no son copias), los canales de anfitrion (que si lo son) y los endpoints de dispositivo (donde el EP0 tiene otra forma de registro) (véase `doc/stm32f407vg_fase7_otg.md`), `eth_mac.h` el Ethernet MAC 10/100, donde «canal» vuelve a significar tres cosas distintas —las dos interfaces fisicas MII y RMII, que no son dos modos sino dos CAMINOS con distinto numero de hilos y de relojes; los dos anillos del DMA, cuyos descriptores comparten los nombres de los campos y ninguno de sus significados; y los cuatro filtros de direccion, donde el 0 no tiene ni habilitacion ni mascara de bytes— (véase `doc/stm32f407vg_fase7_eth.md`), y `pwr.h`, que no es un periférico más sino el ÁRBITRO DE LA ENERGÍA: decide en cuál de los cuatro modos está el MCU, ordena al RCC parar los relojes o apagar el dominio de 1,2 V, vigila VDD con el PVD y calcula la corriente que el chip pide por sus pines (véase `doc/stm32f407vg_fase7_lowpower.md`) |
| `parts/` | LIBRERÍA DE COMPONENTES EXTERNOS al MCU. `part_factory.h` (el registro `cadena → creador y AYUDA`, con macro de auto-registro: el único sitio donde un nombre escrito por una persona se convierte en un objeto), `part_help.h` (la ficha de una pieza —qué hace, terminales, atributos del XML con su valor por omisión, notas y ejemplo— que imprime `sim --help COMPONENTE`; va PEGADA AL REGISTRO y no en una tabla aparte, porque una tabla aparte se queda vieja y una ayuda que miente es peor que ninguna, y la macro la EXIGE, de modo que una pieza nueva no se puede dar de alta sin explicarse), `xml_min.h` (lector de XML mínimo y ESTRICTO, sin dependencias: lo que no entiende es un error con línea y columna, nunca una suposición), `netlist_xml.h` (del árbol XML al netlist), `netlist.h` (el netlist en memoria: NODOS con nombre de esquemático —`PA5`, `VDD`, o creados para lo que no es un pin—, INSTANCIAS con tipo, identificador, parámetros y REFERENCIAS a otras instancias, y CONEXIONES nominales terminal→nodo; construye antes de `sc_start` porque la elaboración de SystemC es estática, valida la declaración sin simular y vuelca a XML), `netlist_parts.h` (el catálogo: una entrada de factoría por tipo —creador Y ficha de ayuda en la misma llamada—, más los ayudantes tipados con los que se monta una placa desde C++), `part_base.h` (`ExtPartBase`: terminales con NOMBRE —no por posición del constructor—, un único `set_enabled(bool)` para soldar y desoldar, inventario global de piezas y volcado del netlist en XML; véase `doc/stm32f407vg_parts_paso1.md`), `ext_parts.h` (circuitería externa de placa: cristal, reloj, LED, pulsador, resistencia, driver, pista entre pines, hilo de bus I2C con pull-up, EEPROM 24Cxx, maestro I2C externo, tarjeta SD a nivel de pin, el bus CAN completo —hilo cableado en Y con su terminador, transceptores y un nodo CAN externo que habla el protocolo—, una sonda SWD que habla el protocolo bit a bit por PA13/PA14, un analizador de traza SWO colgado de PB3 y un sensor de imagen CMOS que genera cuadros enteros sobre PIXCLK/HSYNC/VSYNC y doce hilos de datos, con sincronismo por hardware o embebido, una SRAM asíncrona de 64 K que se suelda al bus externo del FSMC —engancha la dirección baja de los propios hilos de datos con NL y guarda en el flanco de subida de NWE, byte a byte según NBL0/NBL1— y una NAND de 16 páginas que entiende mandatos, direcciones y datos por CLE/ALE, y los dos extremos de un cable USB —un PC que da los 5 V de VBUS, pone los dos 15 kohm a masa y hace el reset con un SE0 largo, y un aparato que declara su existencia con el 1,5 kohm de D+ y se entera del reset porque lo VE— y el PHY de Ethernet, que pone los relojes del camino de datos, habla MDIO bit a bit con sus registros de la norma y hace de buzon de tramas con su CRC-32) |
| `soc/` | LO QUE ES «DE ESTE CHIP» Y NO «DE ESTA ARQUITECTURA», separado en la fase 1 del plan del F446. `stm32f446.h` (la clase **`Stm32F446`**, que la fase 2 añadió: el die de la familia con el descriptor del F446RE —512 KB, sin CCM, sin Ethernet, sin RNG, sin los bloques de extensión del I2S, LQFP64 propio, 97 posiciones de vector, siete maestros de matriz e IDCODE `0x1000 0421`— y con un `limitaciones()` que enumera en voz alta lo que el modelo todavía NO hace, que `sim` imprime al montar la placa), `mcu_if.h` (la INTERFAZ de un MCU vista desde `sim` —registrar sus nodos, entregar el nodo de un pad, alimentarse, cargar firmware y los dos interruptores de traza— y `FabricaMcu`, el registro `familia → creador` que convierte el `tipo=` del XML en un objeto de verdad en vez de comprobarlo contra una lista. Se busca por FAMILIA y no por nombre de pieza, porque los once miembros del F405/407 son la misma clase con descriptores distintos; una familia sin modelo enlazado devuelve `nullptr` y `sim` lo dice con nombre, nunca monta otro chip en su lugar), `stm32f4_mcu.h` (el adaptador de la familia F405/407 a esa interfaz, que además guarda los cuatro índices de driver de la alimentación y el orden de arranque de una placa real), `f4_mapa_perif.h` (las bases de los periféricos, la tabla de sectores de la Flash, `N_IRQ`, `N_EXTI` y los topes de frecuencia) y `f4_mapa_af.h` (las 169 conexiones de la tabla de funciones alternativas, sacadas del cuerpo del top; el F446 resultó tener la MISMA tabla salvo los huecos del AF11 de Ethernet) |
| `verif/` | `bus_test_master.h` (maestro de bus de verificación), `image_loader.h` (carga de .bin/.hex y tabla de vectores), `decoder_vectors.h` (254 vectores generados desde `doc/valida_instrucciones.py` por `gen_decoder_vectors.py`), `swd_port.h` (el maestro SWD a nivel de bit, con el tratamiento de WAIT/FAULT y demás peculiaridades de ADIv5), `gdb_stub.h` (el servidor GDB/RSP de los pines: solo el transporte, porque el protocolo vive en `common/gdb_rsp.h`) y `gdb_client.h` (un GDB de mentira para verificarlo), `fw/` (firmware de autocomprobación, *port* bare-metal de CoreMark, CMSIS oficial, blinky de referencia y demostraciones del DMA, de los puertos serie, de los temporizadores, del EXTI, del SPI, del I2C, del ADC, del DAC, del SDIO, del CRC/RNG, del bxCAN y de la traza ITM/DWT) |
| `top/` | `soc_f4.h` + `soc_f4_bind2.h` (el netlist de integración del DIE de la familia F4: se llamaba `stm32f407vg.h` y `Stm32F407VG`, y la fase 2 lo renombró porque nunca fue «del F407VG» —los once miembros del F405/407 y el F446 son este mismo netlist con otro descriptor—; `using Stm32F407VG = SocF4;` deja valer lo escrito), `sc_main_f446.cpp` (**la suite del F446**, un ejecutable APARTE: construir un segundo chip dentro del banco del F407 mueve su tiempo simulado, así que dos chips son dos simulaciones; `make test446`), `mcu_caps.h` (el DESCRIPTOR de un microcontrolador: junta los rasgos del núcleo, el mapa de memoria y los topes de reloj; su campo `familia` marca la frontera de lo recombinable —dos chips de la misma familia se distinguen con un descriptor, dos familias distintas necesitan modelo nuevo—), `sc_main.cpp` (suite de verificación acumulada del F407) |

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
