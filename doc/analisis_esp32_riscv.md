# ESP32 con núcleo RISC-V en `mcu-sim`

**Análisis de lo que habría que modificar y añadir, y plan por fases** · 14-10-2026

La pregunta: qué haría falta para que `mcu-sim` simule los ESP32 con núcleo
RISC-V (C2, C3, C5, C6, C61, H2, P4) **igual que hoy simula los STM32F4**. Es
decir: el binario de verdad, el que sale de ESP-IDF, Arduino o Zephyr, sin
recompilarlo; pines eléctricos; placas en XML; una consola serie en el
ordenador; descarga y depuración con las herramientas de siempre.

También se responde a dos preguntas concretas: **cómo se descarga y cómo se
depura** el firmware, y **qué parte del código que ya hay sirve**.

---

## 0. Resumen

* **La mitad de fuera del chip ya está hecha y vale tal cual.** El netlist, el
  XML de placas y sistemas, las 20 y pico piezas, el puente serie con RFC 2217,
  la frontera con `mcu-sim-gui`, la capa de red, el freno de tiempo real, el
  motor de GDB/RSP en su parte de protocolo y toda la integración continua
  (con sus invariantes) no saben qué chip hay dentro. Un ESP32-C3 que cumpla
  `mcu_if` las hereda todas. También la placa mixta: **un ESP32-C3 y un STM32F407
  hablando por I²C en la misma placa** sale con lo que ya existe.
* **La mitad de dentro hay que escribirla entera.** El núcleo Cortex-M4F, el
  CoreSight, la matriz AHB, el RCC y los ~40 periféricos son de ARM y de ST. No
  hay nada en `core/`, `bus/`, `rcc/`, `periph/` ni `soc/` que sirva como modelo
  de registros para un ESP32. Lo que sí se aprovecha de ahí son **patrones**: la
  base `BusSlave`, los rasgos como dato (`XxxCaps`), los contadores por
  sucesos, los bloques declarados y no modelados, y `limitaciones()`.
* **Antes de la primera línea RISC-V hace falta una fase 0 de generalización.**
  Hay seis sitios que dan por hecho un STM32. Son pequeños, pero están en las
  interfaces comunes: `mcu_if` (incluye `cortex_m4f.h` y nombra los pads como
  `(puerto, pin)`), `Encapsulado` (16 pines por puerto), `Cableado` y
  `pad_desde_nombre` (analizan `PA5`), `nombres_nodo.h` (`net_A5` → `PA5`),
  `PowerPads` (NRST/BOOT0) y **`GdbRsp`, que mezcla el protocolo de GDB con el
  modelo de depuración de ARMv7-M** (DHCSR, FPB, DWT y la Flash del F407). El
  criterio de cierre es el de siempre: **ningún invariante se mueve ni un
  picosegundo**.
* **El ESP32 se diferencia del STM32 en tres cosas que mandan sobre todo lo
  demás**, y que el plan tiene que atacar en este orden:
  1. **No tiene Flash interna programable por registros.** El programa vive en
     una Flash SPI (externa o dentro del encapsulado). Se ejecuta a través de
     una caché con MMU que la proyecta en `0x4200_0000` y `0x3C00_0000`, y
     tiene su propio formato de imagen (bootloader en `0x0`, tabla de
     particiones en `0x8000` y aplicación en `0x1_0000`).
  2. **El firmware de ESP-IDF llama a la ROM de máscara.** `esp_rom_printf`,
     `ets_delay_us`, las funciones de la caché y de la Flash SPI, `memcpy`…
     están en la ROM, en direcciones fijas, y la aplicación salta a ellas. **Sin
     la imagen de la ROM no arranca ningún firmware de ESP-IDF**, ni de Arduino
     ni de Zephyr. Espressif publica esas ROM como ELF (`esp-rom-elfs`), y
     ESP-IDF ya las instala en la máquina del alumno. Se cargarían desde ahí,
     **sin versionarlas**: es la misma política que con los PDF de ST (§6).
  3. **La depuración es la del RISC-V Debug Spec 0.13 por JTAG**, no SWD +
     CoreSight. El chip trae además un **USB-Serial-JTAG** fijo que da a la vez
     la consola y el JTAG por el mismo USB.
* **Descarga (§4):** cuatro vías, de menor a mayor fidelidad:
  1. la carga directa al arrancar (`mcu-sim placa.xml flash_image.bin`);
  2. **`esptool` por RFC 2217 contra el `PuenteSerie` de la UART0, con el
     auto-reset por DTR/RTS** (`idf.py -p rfc2217://localhost:3355 flash
     monitor`). Esta es la del laboratorio y casi todo está hecho: esptool
     admite `rfc2217://` de serie, y el puente ya mueve DTR y RTS;
  3. la misma, por la parte CDC del USB-Serial-JTAG;
  4. por JTAG desde OpenOCD.
* **Depuración (§5):** tres vías, como hoy hay dos:
  1. un **stub interno pegado al Debug Module** (el equivalente de `--gdb-dap`),
     para `riscv32-esp-elf-gdb`, `idf.py gdb` o VS Code;
  2. un **JTAG por los pines** MTMS/MTDI/MTCK/MTDO, con una sonda que habla el
     protocolo `remote_bitbang` de **OpenOCD**. Con ella funciona el
     `openocd-esp32` de verdad, con `target/esp32c3.cfg`, los puntos de ruptura
     en Flash, `program_esp` y los hilos de FreeRTOS;
  3. el mismo JTAG por el USB-Serial-JTAG, a nivel de transacción.
* **Recomendación:** empezar por el **ESP32-C3**. Es RV32IMC con un solo
  núcleo, sin núcleo LP y sin coma flotante; es el mejor documentado, el que
  tiene más placas baratas y el que mejor soporta el QEMU de Espressif, que
  sirve de contraste. El C6 y el H2 llegan después como «otro descriptor más
  algunos bloques». El P4, con dos núcleos, extensiones propias y sin radio, es
  otro proyecto.
* **Lo que no se modela, y hay que decirlo desde el primer día:** **la radio**
  (Wi-Fi, BLE y 802.15.4). Se declara y no se modela, igual que el SPDIF-RX del
  F446. Un firmware que arranque el Wi-Fi se quedará en la inicialización del
  PHY, y `limitaciones()` lo dirá al montar la placa.

---

## 1. Qué hay hoy, visto desde un chip que no es de ST

### 1.1 Lo que ya es de cualquier chip

La arquitectura del proyecto tiene tres capas (`doc/reutilizacion.md` §2):
arquitectura, integración y familia. Un ESP32 cambia **las tres**. Lo que lo
hace viable es que la frontera exterior del MCU se diseñó para no depender de
ninguna:

* **El MCU termina en los `AnalogNet` de sus pines** (`src/README.md`,
  «Convenciones»). Las piezas se conectan a nodos, no a chips, y por eso
  `multi_mcu.md` §5.5 pudo decir que el catálogo entero vale para dos MCUs «sin
  una línea nueva». Vale igual para un MCU de otra arquitectura.
* **`sim` habla con el chip por `mcu_if`** (`soc/mcu_if.h`), con su factoría
  indexada por familia. Ese fichero ya prevé el caso: *«un tipo de otra familia
  es una clase nueva y una llamada a `REGISTRA_MCU`»*.
* **El puente serie, el de la GUI y los servidores de GDB hablan TCP** por
  `common/red.h`, que es el único fichero que sabe en qué sistema operativo
  corre.

### 1.2 Lo que da por hecho un STM32, y dónde

Este es el inventario de lo que hay que tocar antes de nada. Sale de leer las
interfaces comunes, no de buscar «F407».

| Sitio | Qué supone | Por qué estorba a un ESP32 |
| :--- | :--- | :--- |
| `soc/mcu_if.h` | Incluye `core/cortex_m4f.h` para `DebugCaps`; `caps()` devuelve un `McuCaps`; `nodo_analogico(puerto, pin)` | Un ESP32 no tiene `CoreCaps` de NVIC ni `ArbolReloj` de RCC, y sus pines no tienen puerto: son `GPIO0`…`GPIO21` |
| `top/mcu_caps.h` | `McuCaps` = `CoreCaps` (NVIC, MPU, FPv4) + `MemCaps` (sectores de Flash interna) + `ArbolReloj` + `Periferia` (ETH, DCMI, CRYP…) + `Conectividad` (matriz AHB) + `idcode` (DBGMCU) | Casi todos los campos son de ST. Es el descriptor de una **familia**, y debe seguir siéndolo; lo que falla es que `mcu_if` lo exija |
| `pins/encapsulado.h` | `uint16_t puerto[N_PUERTOS_SILICIO]`: 16 pines por puerto | El C3 tiene 22 GPIO en un solo banco; el C6 tiene 31 y el P4 55 |
| `pins/pin_mux.h` | `Cableado` indexado por `(puerto, pin)` con `N_PORT_PINS = 16`; `pad_desde_nombre` analiza `PA5`; mux por función alternativa AF0-15 | El ESP32 tiene **matriz GPIO**: cualquier señal de periférico a cualquier pin, más un IO_MUX con funciones directas |
| `common/nombres_nodo.h` | `net_A5` → `PA5` | Hay que preguntarle el nombre al chip |
| `pins/power_pads.h` | VDD, VDDA, VBAT, **NRST**, **BOOT0** | El ESP32 tiene **EN** (CHIP_PU) y pines de arranque (*strapping*) que son GPIO normales: en el C3, GPIO2, GPIO8 y GPIO9 |
| `common/gdb_rsp.h` | Además del protocolo, el modelo de depuración de ARMv7-M: DHCSR/DCRSR/DCRDR, 23 registros de perfil M, FPB (6) y DWT (4), y **la secuencia de programación de la Flash del F407** en `0x4002_3C00` | Todo eso es de ARM y de ST. El RISC-V para y lee registros por el Debug Module, y la Flash del ESP32 no se programa así |
| `verif/image_loader.h` | `ImageLoader(SocF4&)`, `.bin` en `FLASH_BASE` e Intel HEX | Ya está detrás de `carga_firmware()`. Falta el formato de imagen del ESP32 y **ELF**, que también le vendría bien al STM32 |

Ninguno es grande. Todos están en el camino de cualquier chip nuevo que no sea
de ST, así que la fase 0 se paga una vez: después, un RP2040, un nRF52 o un
GD32VF103 entran por la misma puerta.

---

## 2. En qué se diferencia un ESP32-C3 de un STM32F4

Esto no pretende ser un manual; es la lista de lo que cambia el **diseño**. Las
cifras son del catálogo y del TRM de Espressif. **Antes de escribirlas en un
descriptor deben contrastarse con dos fuentes**, como manda la regla del
proyecto: el TRM y las cabeceras `soc/*_reg.h` de ESP-IDF (§6.2).

| | STM32F407 | ESP32-C3 |
| :--- | :--- | :--- |
| **Núcleo** | Cortex-M4F, Thumb-2, FPv4-SP, NVIC, MPU | RV32IMC (Zicsr, Zifencei), 160 MHz, segmentado en 4 etapas, PMP, sin FPU ni atómicas |
| **Interrupciones** | NVIC de 82 líneas y 4 bits de prioridad | **Matriz de interrupciones** (62 fuentes) → 31 interrupciones de CPU, con prioridad, umbral y tipo flanco/nivel por línea. El controlador es propio de Espressif; los chips posteriores lo cambian (los más recientes adoptan CLIC) |
| **Código** | Flash interna de 1 MB en `0x0800_0000`, programada por `FLASH_KEYR/CR/SR` | **Flash SPI** fuera del núcleo, leída por SPI0 a través de **caché + MMU** (páginas de 64 KB) y proyectada en `0x4200_0000` (instrucciones) y `0x3C00_0000` (datos). Se programa con SPI1 |
| **RAM** | SRAM1/2, CCM y BKPSRAM | 400 KB de SRAM, **la misma memoria vista por dos buses** (IRAM en `0x4037_C000` y DRAM en `0x3FC8_0000`), más 8 KB de RTC FAST |
| **ROM** | System Memory con el bootloader de ST, que casi nadie usa desde el firmware | **384 KB de ROM que el firmware usa todo el rato**: arranque, cargador serie, y una biblioteca de funciones enlazada por dirección (`esp32c3.rom.ld`) |
| **Arranque** | Vector de reset en la Flash, BOOT0 | La ROM lee los pines de arranque y luego carga el **bootloader de segunda etapa** de la Flash en IRAM, que a su vez lee la tabla de particiones y monta la aplicación en la MMU. Escribe por la UART0 a 115200: `rst:0x1 (POWERON_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)` |
| **Pines** | Puertos A-I de 16 pines con AF0-15 | 22 GPIO, **matriz GPIO** + IO_MUX; fuerza de salida en 4 niveles (`FUN_DRV`) en vez de `OSPEEDR` |
| **Reloj** | RCC: HSI/HSE/PLL/PLLI2S, AHB/APB | Cristal de 40 MHz, PLL de 480 MHz, CPU a 80/160 MHz, APB a 80 MHz. Lo gobiernan el SYSTEM y el RTC_CNTL |
| **Configuración** | Bytes de opción | **eFuse**: dirección MAC, revisión del chip, selección del JTAG, cifrado de Flash, arranque seguro |
| **Depuración** | SWJ-DP + AHB-AP + CoreSight (FPB, DWT, ITM) | **RISC-V Debug 0.13**: TAP JTAG → DTM → Debug Module, 8 *triggers* (puntos de ruptura y de vigilancia hardware), y el **USB-Serial-JTAG** integrado |
| **Radio** | — | Wi-Fi 4 y BLE 5 |

Tres consecuencias para el modelo:

1. **«Cargar el firmware» no es escribir la Flash interna.** Es escribir una
   pieza de Flash SPI (que en el C3FH4 va dentro del encapsulado y en el C3 a
   secas está en la placa), y dejar que la ROM y la caché hagan el resto. El
   modelo de memoria del STM32, donde la Flash es un esclavo del bus, no se
   parece en nada.
2. **El tiempo de ejecución depende de la caché.** Un fallo de caché cuesta una
   lectura SPI. Para el invariante basta un modelo LT de aciertos y fallos;
   pero tiene que existir, porque ejecutar desde IRAM o desde Flash se nota
   (`IRAM_ATTR` existe por eso).
3. **Los pines de arranque son GPIO.** GPIO9 es el botón BOOT de la placa *y*
   un pin que el firmware puede usar después. El valor que importa es el que
   tenía en el flanco de subida de EN. Eso se modela en el pad, no en `sim`.

---

## 3. Qué se aprovecha del código que ya hay

### 3.1 Tal cual, sin tocar una línea

| Qué | Dónde | Para qué en el ESP32 |
| :--- | :--- | :--- |
| Nodos eléctricos y superposición | `common/analog_net.h` | Igual |
| Generador de reloj reprogramable | `common/clock_gen.h` | El cristal de 40 MHz, el PLL, el reloj de la CPU y el de APB |
| Esclavo TLM con *byte enables*, gating y latencia por dominio | `common/periph_base.h` | La base de todos los periféricos del ESP32 |
| Netlist, XML, `<sistema>`, `<acopla>`, `une=`, validación | `parts/netlist*.h`, `xml_min.h` | Placas ESP32 y placas mixtas |
| Factoría de piezas con ayuda obligatoria | `parts/part_factory.h`, `part_help.h` | Las piezas nuevas (§7) |
| **Catálogo de piezas** | `parts/ext_parts.h`, `tft_st7735.h`, `servo.h`… | LED, pulsadores (BOOT y EN), cristal, resistencias, bus I²C con EEPROM 24Cxx, **bus CAN con sus transceptores (el TWAI del C3 es un CAN 2.0)**, TFT ST7735 por SPI, servo, KY-040, FT232RL |
| **Puente serie** con TCP en crudo y RFC 2217, DTR/RTS y *break* | `parts/puente_serie.h`, `motor_uart.h`, `common/telnet2217.h`, `serie_destino.h`, `formato_uart.h` | **La consola y la descarga con esptool** (§4.2) |
| Frontera con `mcu-sim-gui` | `common/gui_*.h`, `protocolo.h`, `proto_io.h`, `parts/enlace_gui.h`… | Igual. El protocolo nombra los nodos con cadenas, y `GPIO9` es tan válida como `PA5` |
| Capa de red | `common/red.h` | Igual |
| Freno de tiempo real, `--espera-terminal`, varios MCUs con un puerto de GDB cada uno | `top/sim_main.cpp` | Igual |
| Huella de firmware, opciones de ASan, guarda de *tick* | `common/huella_fw.h`, `asan_opciones.h`, `guarda_tick.h` | Igual |
| Integración continua por invariante de tiempo simulado | `ci/*.sh`, `.github/workflows/suites.yml`, `verif/invariantes.txt` | Un banco más, `testc3`, con su cifra |
| GDB de pega para las pruebas | `verif/gdb_client.h` | Probar el stub RISC-V sin `gdb` instalado |
| Los fuentes de CoreMark | `verif/fw/coremark/` | Con otro `core_portme`, la primera carga útil del núcleo RISC-V |

### 3.2 Con una generalización pequeña (fase E0)

| Qué | Cambio |
| :--- | :--- |
| `mcu_if` | Quitar el `#include "../core/cortex_m4f.h"`, para lo que `DebugCaps` pasa a un fichero común. Cambiar `caps()` por un `McuInfo` mínimo (nombre, familia, encapsulado y pads), con `McuCaps` como descriptor **propio** de los STM32F4. Sustituir `nodo_analogico(puerto, pin)` por `nodo_analogico(const std::string& pad)` y añadir `pads()`, que enumera los pads con su nombre |
| `Encapsulado` | De `uint16_t puerto[]` a una lista de pads con nombre, o a máscaras de 64 bits. Se conserva la comprobación de que el recuento coincide con el datasheet |
| `Cableado`, `pad_desde_nombre`, `NodeMap::registra_mcu` | Que el MCU diga cómo se llaman sus pads, para que el netlist no los adivine. El ESP32 acepta `GPIO9` y el alias `IO9`, que es como lo serigrafían las placas |
| `nombres_nodo.h` | El nombre del nodo sale del pad, no del nombre del objeto SystemC |
| `PowerPads` | Que los pines de alimentación y control sean una lista por familia: VDD, VDDA, NRST y BOOT0 en el STM32; VDD, VDD3P3_RTC y EN en el ESP32 |
| `GdbRsp` | Partirlo en **protocolo** (socket, tramado, paquetes `g/G/p/P/m/M/X/c/s/vCont/Z/z/q/v`, `monitor`, `qXfer`, traza) y **arquitectura**: una interfaz `depurador_arq_if` con registros y `target.xml`, parar/seguir/paso, puntos de ruptura y de vigilancia, *reset* y programación de memoria no volátil. El ARMv7-M se queda **con la misma secuencia de accesos al DAP**, que es lo que mantiene el invariante |
| `ImageLoader` / `carga_firmware` | Añadir ELF (lo usan los dos mundos) y el formato de imagen del ESP32 |
| `cubeide/simulador.launch` | Su equivalente para Espressif-IDE (Eclipse) y un `launch.json` para VS Code |

### 3.3 Como algoritmo, no como periférico

| Qué | Dónde | En el ESP32 |
| :--- | :--- | :--- |
| AES | `periph/cryp_algo.h` | El acelerador AES del C3 (ECB y los modos por bloques, con DMA) |
| SHA-1 | `periph/hash_algo.h` | El acelerador SHA del C3 hace SHA-1, SHA-224 y SHA-256. **El fichero ya prevé el SHA-2** («el día que haga falta un SHA-2 para otra familia, entra aquí»). Hace falta también para verificar las imágenes que comprueba la ROM |
| Vectores de prueba de cifrado | `verif/vectores/` | Los mismos, más los de SHA-256 |

### 3.4 Lo que no sirve como código, pero sí como patrón

`core/` (Thumb-2, FPv4, SCS, CoreSight), `bus/` (matriz AHB 8×7, bit-banding),
`rcc/`, `soc/`, `top/soc_f4*.h` y todo `periph/` son de ARM o de ST. Pero de
ellos se hereda la manera de hacer las cosas:

* **los rasgos como dato** (`UsartCaps`, `TimCaps`, `CoreCaps`): un
  `RvCoreCaps` (extensiones, número de *triggers*, regiones PMP, `misa`,
  `marchid`/`mimpid`) que el C3, el C6 HP, el C6 LP y el P4 instancian con
  números distintos;
* **la CPU con el estado en un `struct`** y el sistema por una interfaz C++, no
  por TLM (`core_sys_if`), con el **quantum LT y la anotación de ciclos**;
* **`debug_if.h`**, el contrato por el que la CPU llama al depurador en cada
  búsqueda y cada acceso. En RISC-V el mismo contrato sirve para los
  *triggers*, `ebreak` y `dcsr.step`;
* **los contadores por sucesos** de `timers.h`, para el SYSTIMER de 52 bits y
  los TIMG;
* **`bloque_declarado.h`**: la radio, el RSA, el HMAC y el DS (firma digital)
  contestan, no guardan y avisan la primera vez;
* **`limitaciones()`** en voz alta al montar la placa.

### 3.5 Los repositorios `nanorv` y `nanorv-ocd`

`nanorv` es un RV32I multiciclo en **VHDL** con bus APB. No hay código que
pasar a SystemC, pero aporta tres cosas:

* su `crt0.s`, su `linker.ld` y su `makefile` con `riscv64-unknown-elf-gcc` de
  MSYS2: la base de los firmwares de prueba *bare-metal* de la fase E1;
* `debugging.txt`, que es una lista de errores típicos de un RV32I (extensión
  de signo en cargas de byte, desplazamientos al alinear) que conviene
  convertir en pruebas;
* la experiencia de compilar RISC-V en Windows, que es la plataforma más
  delicada del proyecto.

`nanorv-ocd/nanorv.cfg` es un borrador de configuración de OpenOCD para un
USB-Blaster que no llegó a cerrarse. No se reutiliza, pero apunta a lo mismo que
la vía 2 de §5: **OpenOCD hablando con un Debug Module RISC-V propio**. Aquí el
cable sería `remote_bitbang` por TCP, que es mucho más sencillo que un
USB-Blaster.

---

## 4. Descarga del firmware

### 4.1 Vía D1 · Carga directa al arrancar

Igual que hoy `mcu-sim placa.xml fw.bin`, pero con los formatos del ESP32:

```
./build/mcu-sim placas/esp32c3_devkitm1.xml build/flash_image.bin   # imagen completa
./build/mcu-sim placas/esp32c3_devkitm1.xml build/flash_args        # la lista de idf.py
./build/mcu-sim placas/esp32c3_devkitm1.xml verif/fw/esp32c3/blinky.elf  # bare-metal
```

* **Imagen completa** (`esptool merge-bin`, o el `qemu_flash.bin` que genera
  `idf.py qemu`): se escribe en la pieza de Flash SPI desde el desplazamiento 0.
  Es el formato que ya usa el QEMU de Espressif, y eso lo hace intercambiable.
* **`flash_args` / `flasher_args.json`**: los ficheros que deja `idf.py build`,
  con cada `.bin` y su desplazamiento (bootloader, tabla de particiones y
  aplicación).
* **ELF *bare-metal***: para los firmwares de prueba que no usan ESP-IDF. Se
  cargan sus segmentos en IRAM/DRAM y el *reset* salta a su punto de entrada sin
  pasar por la ROM. Es el «arranque directo» que permite probar el núcleo y los
  periféricos **antes de tener la ROM, la caché y la Flash** (fase E2).
* **eFuse** desde un fichero, compatible con el `qemu_efuse.bin` de ESP-IDF,
  con un valor por omisión razonable: MAC, revisión y sin cifrado.

**Lo que no hace D1: inventarse la ROM.** Un firmware de ESP-IDF cargado así
necesita la ROM para arrancar (§6.1). Si no la encuentra, `sim` lo dice con
nombre y apellidos y no monta otra cosa.

### 4.2 Vía D2 · `esptool` por la UART0, con auto-reset (la del laboratorio)

Es la que más se parece a una placa con su CP2102 o su CH340, y **casi todo lo
que necesita ya existe**:

* **`esptool` admite puertos remotos RFC 2217** con
  `--port rfc2217://host:puerto`, **con baudios a medida y auto-reset por
  DTR/RTS** (documentación de esptool, «Remote Serial Ports»). Con
  `socket://` en crudo no hay auto-reset ni cambio de baudios, así que tiene que
  ser RFC 2217;
* **el `PuenteSerie` ya habla RFC 2217**, con `baudios="host"` para que esptool
  suba a 460 800 o 921 600 tras sincronizar, y **mueve DTR y RTS** desde el
  terminal (`doc/puente_serie.md` §6).

Falta una pieza: el **circuito de auto-reset** de las placas de Espressif, dos
transistores cruzados que convierten DTR y RTS en EN y GPIO9. En el C3, DTR va
a GPIO9 y RTS a EN; afirmar los dos a la vez no resetea, por cómo están
cruzados los transistores; y hace falta un condensador de 1 a 10 µF entre EN y
masa para que el reset sea fiable (esptool, «Boot Mode Selection»). Encaja de
lleno en el modelo eléctrico: es una pieza `AutoResetEsp` con dos transistores
y un RC en EN, que se cuelga de las patillas DTR/RTS del puente.

```xml
<placa nombre="esp32c3-devkitm-1">
  <mcu tipo="ESP32-C3FH4" id="u0" depuracion="dm" puerto_gdb="3333"/>
  <componente tipo="PuenteSerie" id="USB_UART" puerto="3355" baudios="host">
    <pin nombre="rx" nodo="GPIO21"/>   <!-- U0TXD -->
    <pin nombre="tx" nodo="GPIO20"/>   <!-- U0RXD -->
    <pin nombre="dtr" nodo="n_dtr"/>  <pin nombre="rts" nodo="n_rts"/>
  </componente>
  <componente tipo="AutoResetEsp" id="Q1">
    <pin nombre="dtr" nodo="n_dtr"/>  <pin nombre="rts" nodo="n_rts"/>
    <pin nombre="en"  nodo="EN"/>     <pin nombre="boot" nodo="GPIO9"/>
  </componente>
  ...
</placa>
```

Y desde la máquina del alumno:

```
idf.py -p rfc2217://localhost:3355 flash monitor
esptool --port rfc2217://localhost:3355 --baud 921600 write-flash 0x0 flash_image.bin
pio run -t upload --upload-port rfc2217://localhost:3355
```

Para el **Arduino IDE**, que solo enseña puertos COM del sistema, valen las
recetas de redirector de `doc/puente_serie.md` §4 (com0com, tty0tty, ttynvt),
que ya están escritas para el STM32.

**Lo que tiene que pasar dentro del chip para que esto funcione** es lo que
convierte esta vía en la más fiel: la ROM de verdad muestrea GPIO9 al subir EN,
entra en el cargador serie, contesta al protocolo SLIP de esptool, recibe el
*stub* de esptool en RAM y lo ejecuta, y el *stub* programa la Flash con SPI1 y
las funciones de Flash de la ROM. Si todo eso pasa sobre el modelo, **la ROM,
la UART, los pines de arranque, el SPI1, la caché y la Flash quedan verificados
de punta a punta con una herramienta que no hemos escrito nosotros**. Es la
misma idea que hacer que STM32CubeIDE se enganche al stub de GDB.

**El matiz del tiempo.** esptool hace la secuencia de reset con esperas de reloj
de pared (decenas de milisegundos entre DTR y RTS). Si el simulador va más
deprisa que el tiempo real, esas esperas son más largas en tiempo simulado, y
no pasa nada. Si va más despacio, la distancia en tiempo simulado entre «GPIO9
abajo» y «EN arriba» puede quedarse por debajo del tiempo de subida del RC de
EN, y el chip arranca en modo normal. Hay dos remedios, y conviene tener los
dos: recomendar `--tiempo-real` en la receta, y que el puente garantice un
**mínimo de tiempo simulado** entre dos cambios de control consecutivos.

### 4.3 Vía D3 · Por el USB-Serial-JTAG

El C3, el C6, el H2, el C5 y el P4 traen un periférico USB **de función fija**:
el sistema ve un CDC-ACM (la consola) y un JTAG, sin que el firmware tenga que
hacer nada. Al contrario que el OTG del F407, no es programable, lo que lo hace
mucho más sencillo de puentear:

* su parte CDC se saca por **RFC 2217** con el mismo código del puente serie,
  pero colgado del periférico y no de unos pines. Es exactamente la vía U2 de
  `doc/analisis_puente_usb.md`, salvo que aquí el «anfitrión» es trivial porque
  el dispositivo no se enumera desde el firmware;
* **el reset a modo descarga por este USB lo hace el propio periférico** al
  interpretar DTR/RTS del CDC. Así que esptool funciona por aquí sin el circuito
  de transistores: es como se programan las placas «SuperMini» que solo tienen
  el USB nativo;
* con el **servidor USB/IP** de aquel mismo análisis (U1), el sistema vería un
  `303a:1001` de verdad, y `esptool` y el `esp_usb_jtag` de OpenOCD
  funcionarían sin saber que hay un simulador. Es opcional y llega tarde.

### 4.4 Vía D4 · Por JTAG

Con la vía de depuración 2 (§5.2), OpenOCD programa la Flash con
`program_esp fichero.bin 0x10000 verify`. Por dentro, carga en RAM un
algoritmo de Flash que usa las funciones de la ROM. Es la más lenta, porque
cada bit del JTAG es un suceso de simulación, y por eso es la última. Sirve
para comprobar que el Debug Module y el acceso al bus de sistema aguantan una
carga de verdad.

El `load` de GDB contra el stub interno **solo vale para programas que se
ejecutan desde RAM**. Un ELF de ESP-IDF lleva segmentos en direcciones de la
caché (`0x4200_0000`), no desplazamientos de Flash, y escribirlos ahí sería más
permisivo que el silicio. Contra la Flash, el stub contesta con un error que
dice cómo descargar (D2), en vez de hacer como que funciona.

### 4.5 Resumen de la descarga

| Vía | Herramienta del alumno | Qué ejercita del modelo | Coste | Fase |
| :--- | :--- | :--- | :--- | :--- |
| D1 carga directa | ninguna (`mcu-sim … fw`) | La Flash SPI y, con ROM, el arranque | Nulo | E2 (ELF), E3 (imagen) |
| **D2 esptool por UART0 + RFC 2217** | `idf.py flash`, `esptool`, PlatformIO, Arduino con redirector | ROM, pines de arranque, UART, SPI1, Flash y auto-reset eléctrico | Bajo: solo falta `AutoResetEsp` | **E4** |
| D3 esptool por USB-Serial-JTAG | las mismas, con el puerto del CDC | USB-Serial-JTAG (CDC + reset) | Medio | E6 |
| D4 OpenOCD `program_esp` | `idf.py openocd` + `program_esp` | DM, acceso al bus de sistema y algoritmo en RAM | Lento | E5 |

---

## 5. Depuración del firmware

### 5.1 Vía G1 · Stub interno pegado al Debug Module (el `--gdb-dap` del RISC-V)

Es la traducción directa de `core/gdb_stub_dap.h`. El stub hace llamadas de
función a la interfaz DMI del Debug Module, sin pines ni bits, y **todo lo que
hay del DM hacia dentro es el modelo**:

* parar y seguir es escribir `dmcontrol.haltreq` / `resumereq` y leer
  `dmstatus`;
* los registros se leen con **comandos abstractos** (`command`, `data0`) y, si
  hiciera falta, con el *program buffer*;
* la memoria se lee y escribe por el **acceso al bus de sistema** (`sbcs`,
  `sbaddress0`, `sbdata0`), que entra en el bus como un maestro más, igual que
  hoy el AHB-AP entra por el S-bus;
* los **puntos de ruptura hardware** son los 8 *triggers* `mcontrol`
  (`tselect`/`tdata1`/`tdata2`); los de software, un `c.ebreak` escrito en IRAM
  con `dcsr.ebreakm` puesto; un paso es `dcsr.step`.

En el lado de GDB cambian el `target.xml` (`org.gnu.gdb.riscv.cpu`, 33
registros, más los CSR que se quieran enseñar) y las órdenes `monitor`. Para
`idf.py gdb` y la extensión de VS Code hace falta al menos `monitor reset halt`
y `monitor reset`, que es la secuencia que ESP-IDF manda al conectar (`mon reset
halt`, `maintenance flush register-cache`, `thb app_main`, `c`).

Se usa así, con las mismas reglas que hoy:

```
./build/mcu-sim placas/esp32c3_devkitm1.xml flash_image.bin --gdb-dm --port=3333 --tiempo-real
riscv32-esp-elf-gdb build/hello.elf -ex "target extended-remote :3333"
```

**Por qué no es hacer trampa**: por el mismo argumento de `gdb_stub_dap.h`. Se
salta el cable (el TAP, el DTM y el desplazamiento de registros), no el
comportamiento: parar sigue siendo `haltreq`, y un punto de ruptura sigue
siendo un *trigger* con sus limitaciones. Con los 8 ocupados, `next` se
comporta como `step`, igual que en la placa.

### 5.2 Vía G2 · JTAG por los pines y OpenOCD de verdad

El equivalente de la sonda SWD (`verif/gdb_stub.h` y `SondaSwd`). Hace falta:

* en el chip: un **TAP JTAG** con su máquina de estados de 16 estados,
  registro de instrucción de 5 bits, `IDCODE`, `BYPASS` y los dos del RISC-V,
  **`dtmcs` y `dmi`**, sobre MTMS/MTDI/MTCK/MTDO (GPIO4 a GPIO7 en el C3). La
  selección entre el JTAG de los pines y el del USB depende de eFuse y de un
  pin de arranque, y eso también se modela;
* fuera del chip: una pieza **`SondaJtag`** que conduce TCK/TMS/TDI y lee TDO
  por los nodos, y que habla hacia el ordenador el protocolo **`remote_bitbang`
  de OpenOCD**. Es un protocolo de texto de un carácter por orden (`0`-`7`
  para escribir TCK/TMS/TDI, `R` para leer TDO, `r`-`u` para los resets), y
  cabe en una tarde.

Con eso, el alumno usa el **`openocd-esp32` de Espressif sin tocarlo**:

```
openocd -c "adapter driver remote_bitbang" -c "remote_bitbang host localhost" \
        -c "remote_bitbang port 4444" -f target/esp32c3.cfg
```

Y tiene todo lo que da OpenOCD: puntos de ruptura en Flash (OpenOCD los emula
con los *triggers*), hilos de FreeRTOS en GDB, `program_esp`, la *core dump* y
`apptrace`. Lo que no hay que hacer es reimplementar nada de eso en el stub
interno.

El precio es el de siempre: cada acceso son decenas de flancos de TCK, y cada
flanco es un suceso. Si con SWD la diferencia medida fue de ×390 (T97), aquí
será del mismo orden. G2 es para quien necesita OpenOCD o quiere ver el
protocolo, y G1 para todo lo demás. El criterio para elegir es el mismo de
`stm32f407vg_fase6_gdb2.md`.

### 5.3 Vía G3 · El JTAG del USB-Serial-JTAG

El mismo TAP, alimentado por el periférico USB en vez de por los pines. Sin
USB/IP, se expone igualmente como `remote_bitbang` por TCP, pero **sin
simulación bit a bit en los pines**: es rápido como G1 y OpenOCD lo ve como G2.
Es el modo por omisión de las placas que solo tienen USB nativo.

### 5.4 Resumen de la depuración

| Vía | `depuracion=` | Herramienta | Fiel a | Velocidad | Fase |
| :--- | :--- | :--- | :--- | :--- | :--- |
| G1 stub interno al DM | `dm` | `riscv32-esp-elf-gdb`, `idf.py gdb`, VS Code, Espressif-IDE | Debug Module y hart | Rápida | **E5** |
| G2 JTAG por pines + `remote_bitbang` | `pines` | `openocd-esp32` + GDB | TAP, DTM, DM y pines | Lenta | E5 |
| G3 JTAG del USB-Serial-JTAG | `usb` | `openocd-esp32` + GDB | TAP, DTM y DM | Media | E6 |

Y el mismo atributo `puerto_gdb` de `multi_mcu.md` §5.1. Una placa con un C3 y
un F407 se depura con dos sesiones a la vez, una por chip.

---

## 6. La ROM, las fuentes y las licencias

### 6.1 La ROM: imprescindible, y sin versionar

| Opción | Qué es | Veredicto |
| :--- | :--- | :--- |
| **R1. La ROM de verdad** | Se carga el ELF de `esp-rom-elfs` (Espressif), que ESP-IDF ya instala en `~/.espressif/tools/esp-rom-elfs/<versión>/` (en Windows, en `%USERPROFILE%\.espressif\…`), con el fichero de la revisión del chip (`esp32c3_rev3_rom.elf`…) | **La recomendada.** Es el único camino por el que corren sin cambios ESP-IDF, Arduino-ESP32 y Zephyr, y por el que esptool encuentra al otro lado el cargador que espera |
| R2. Arranque «de alto nivel» | `sim` lee la imagen y monta el bootloader o la aplicación sin ejecutar el código de arranque | **No sirve sola**: la aplicación sigue llamando a la biblioteca de la ROM. Puede ser útil como opción de depuración del modelo, nunca como modo por omisión |
| R3. Sin ROM | Solo firmware *bare-metal* | Es lo que usan las suites en E1-E2. No es un modo para alumnos |

La ROM es de Espressif y **no se versiona**. Es exactamente el caso de los PDF de
ST y de los informes internos (`doc/fuentes.md` §0): el repositorio dice qué
fichero, de qué versión y de dónde se baja. `sim` lo busca solo en la
instalación de ESP-IDF, admite `--rom ruta` y, si no lo encuentra, lo dice y no
arranca. Antes de decidir qué hace la integración continua hay que **comprobar
la licencia de `esp-rom-elfs`**. Si permite descargarlo en el CI, las pruebas
de ESP-IDF corren allí; si no, esas pruebas informan de que falta la imagen,
igual que hoy las que necesitan un firmware sin compilar.

**La ROM depende de la revisión del chip.** La revisión vive en eFuse y va en
el descriptor (la v0.3, la v0.4 y la v1.1 del C3 traen ROM distintas). Es el
mismo tipo de dato que `DBGMCU_IDCODE`: si no cuadra, las herramientas dan
errores que despistan.

### 6.2 Las dos fuentes de cada dato

| Papel | STM32 (hoy) | ESP32 |
| :--- | :--- | :--- |
| Manual de referencia (no se versiona) | RM0090, RM0390 | **ESP32-C3 Technical Reference Manual** |
| Datasheet (no se versiona) | DS8626… | ESP32-C3 Series Datasheet (pines de arranque, encapsulados, eléctrico) |
| Cabeceras del fabricante (se versionan) | CMSIS, Apache-2.0 | **`components/soc/esp32c3/register/soc/*_reg.h` y `*_struct.h` de ESP-IDF**, Apache-2.0 (compatible con AGPLv3 en este sentido, como CMSIS) |
| Base de pines | `STM32_open_pin_data` | `soc/io_mux_reg.h`, `gpio_sig_map.h` y el datasheet |
| Arquitectura | ARMv7-M ARM | **RISC-V Unprivileged / Privileged ISA** y **RISC-V Debug Spec 0.13.2** |
| Referencia de comportamiento | — | El **QEMU de Espressif** (C3 y C6) y **Spike** para la ISA |

**Sobre el QEMU de Espressif.** Es GPL-2.0 y **no se copia código** de él. Se
usa como segunda opinión de comportamiento: qué contesta un registro sin
documentar en el arranque, cómo se rellena la imagen de Flash. Cada vez que se
use, se anota como fuente, igual que hoy se anota la discrepancia entre dos
revisiones de un datasheet. **Spike** (BSD) sirve además como **ISS de
referencia**: ejecutar el mismo ELF en los dos y comparar la traza instrucción a
instrucción es la versión RISC-V de los 254 vectores de
`valida_instrucciones.py`.

**Registros sin documentar.** El arranque de ESP-IDF toca registros analógicos
que el TRM no describe (la calibración del PLL por el bus `regi2c`, el control
de la radio). La regla del proyecto se aplica tal cual: o se modelan con dos
fuentes, o se declaran (contestan, no guardan y avisan una vez). En ningún caso
se deja que el firmware siga sin decirlo.

---

## 7. Lo que hay que escribir

### 7.1 Ficheros nuevos, por carpeta

| Carpeta | Ficheros | Qué |
| :--- | :--- | :--- |
| `core/rv/` | `rv_caps.h`, `rv_state.h`, `rv_cpu.h`, `rv_exec.h` (I, M, C, A, Zicsr, Zifencei; F para el P4), `rv_csr.h`, `rv_pmp.h` | El núcleo: decodificación, CSR de modo máquina (y de usuario si el chip lo tiene), traps, `wfi`, PMP y anotación de ciclos |
| `core/rv/` | `rv_debug.h` (Debug Module 0.13: `dmcontrol`, `dmstatus`, `abstractcs`, `command`, `data*`, `progbuf*`, `sbcs`, `haltsum`; modo depuración del hart, `dcsr`/`dpc`), `rv_trigger.h`, `rv_jtag.h` (TAP + DTM) | La depuración |
| `common/` | `gdb_rsp.h` partido en `gdb_rsp.h` y `gdb_arq_if.h`; `gdb_arq_armv7m.h` y `gdb_arq_riscv.h` | §3.2 |
| `soc/esp32c3/` | `esp32c3_mapa.h` (direcciones, fuentes de interrupción y señales de la matriz GPIO), `esp32c3.h` (el SoC), `esp32c3_mcu.h` (adaptador a `mcu_if` y `REGISTRA_MCU(ESP32C3, …)`) | El chip |
| `bus/esp/` | `esp_bus.h` | El decodificador de direcciones con los alias IRAM/DRAM de la misma SRAM y las ventanas de caché |
| `mem/esp/` | `esp_cache_mmu.h` (caché de 16 KB, MMU de 64 KB por página, coste de fallo), `esp_rom.h` (carga del ELF de la ROM), `esp_efuse.h` | La memoria |
| `periph/esp/` | `system.h`, `rtc_cntl.h` (razones de reset, pines de arranque, perros guardianes RWDT y SWD), `intmatrix.h`, `systimer.h`, `timg.h`, `uart.h`, `gpio_matrix.h`, `io_mux.h`, `spi_mem.h` (SPI0/SPI1), `gpspi.h`, `i2c.h`, `ledc.h`, `rmt.h`, `gdma.h`, `adc.h`, `twai.h`, `usb_serial_jtag.h`, `sha.h`, `aes.h`, `rng.h` y los declarados | Los periféricos |
| `pins/esp/` | `esp_pad.h` (umbrales y los cuatro niveles de `FUN_DRV` sobre el mismo `Pad` eléctrico), `esp_pinmux.h` | Los pines |
| `parts/` | `SpiFlash` (W25Q/GD25Q: lectura, Fast/DIO/QIO, programación de página, borrado de sector, SFDP, JEDEC ID), `AutoResetEsp`, `SondaJtag` (`remote_bitbang`), `Ws2812` (el LED RGB del DevKitM-1, gobernado por RMT) | Las piezas |
| `placas/` | `esp32c3_devkitm1.xml` y `.svg`, `esp32c3_supermini.xml`, `esp32c3_y_f407.xml` (dos familias en una placa) | Las placas |
| `top/` | `sc_main_c3.cpp` | El banco `testc3`, un ejecutable aparte con su invariante |
| `verif/fw/esp32c3/` | `startup.S`, `esp32c3.ld`, y *blinky*, *uart*, *systimer*, *gpio_matrix*, *ledc*, *rmt*, *i2c*, *spi*, *debug*, CoreMark | Firmwares *bare-metal* versionados, con su huella |
| `verif/fw/esp32c3/idf/` | `hello_world`, `blink` y `uart_echo`, compilados con ESP-IDF y versionados (Apache-2.0) | Las pruebas con la ROM |

### 7.2 Cambios en lo que hay

* `sim_main.cpp`: las opciones `--rom`, `--efuse`, `--gdb-dm` y `--flash-args`;
  `--mcu ESP32-C3`; el informe final con los LEDs que dependen de GPIO.
* `Makefile.mcu-sim`: las fuentes por familia, los objetivos `testc3`,
  `asanc3` y `fwc3` (con `riscv32-esp-elf-gcc` o el `riscv64-unknown-elf-gcc`
  de MSYS2 con `-march=rv32imc_zicsr_zifencei -mabi=ilp32`).
* `verif/invariantes.txt` y `ci/`: el invariante del C3.
* `doc/todo.md`: un punto nuevo con sus subpuntos, igual que P-12 y P-14.
* `TERCEROS.md` y `doc/fuentes.md`: la ROM, el TRM, las cabeceras de ESP-IDF y
  Spike.

---

## 8. Plan por fases

El método es el de los puentes y los planes de familia. Fases pequeñas, cada una
con su criterio de cierre; un banco propio (`testc3`) con su invariante; y, como
condición de todas, **sin mover los invariantes de `test407`, `test446`,
`test417`, `testserie` y `testgui`**.

### E0 · Generalización neutral

**Qué:** §3.2 entero: `mcu_if` sin ARM, pads con nombre, `Encapsulado` y
`Cableado` generales, `PowerPads` por familia, `GdbRsp` partido en protocolo y
arquitectura, ELF en el cargador.

**Cierre:**
* las cinco suites con **las mismas comprobaciones y el mismo tiempo simulado
  al picosegundo**, en las cuatro plataformas;
* `T96` y `T97` (los dos stubs de GDB) idénticos paquete a paquete con la
  traza puesta;
* un `McuFalso` de prueba (sin núcleo, con pads llamados `GPIO0`…`GPIO7`) que se
  monta en una placa XML con un LED y se valida con `--valida`. Demuestra que la
  puerta está abierta antes de tener el chip.

**Tamaño:** medio. Es mecánico, pero toca las interfaces que usa todo.

### E1 · Núcleo RV32IMC

**Qué:** `core/rv/` sin depuración: el núcleo, los CSR de modo máquina, los
traps, PMP y la anotación de ciclos con quantum LT. Se monta sobre una RAM de
laboratorio y un controlador de interrupciones de pega, igual que las piezas de
laboratorio de T127.

**Cierre:**
* **`riscv-tests`** (`rv32ui`, `rv32um`, `rv32uc` y `rv32mi`, entorno `-p`) y
  la parte RV32IMC de **`riscv-arch-test`**, todos en verde;
* **traza contra Spike** idéntica en un programa de prueba propio y en CoreMark;
* CoreMark compilado para RV32IMC termina con su CRC correcto;
* los errores de `nanorv/application/debugging.txt` convertidos en pruebas
  (extensión de signo de `lb`/`lh`, cargas no alineadas).

**Tamaño:** medio. Es bastante menos que el Thumb-2 de F2: la ISA base son unas
40 instrucciones y la C se expande a la base.

### E2 · ESP32-C3 con arranque directo

**Qué:** el SoC sin ROM ni caché: mapa de memoria con los alias IRAM/DRAM;
SYSTEM y reloj (40 MHz → PLL → 160/80 MHz); matriz de interrupciones y
controlador de la CPU; SYSTIMER; TIMG0/1 con sus perros guardianes; RTC_CNTL
mínimo; UART0/1; GPIO con matriz e IO_MUX sobre pads eléctricos; eFuse desde
fichero; pines de arranque muestreados en el flanco de EN. El adaptador
`mcu_if` y `REGISTRA_MCU(ESP32C3, …)`. La placa `esp32c3_devkitm1.xml` con el
pulsador BOOT en GPIO9, el de EN, y el LED. Firmwares *bare-metal* cargados
como ELF (D1).

**Cierre:**
* `mcu-sim placas/esp32c3_devkitm1.xml verif/fw/esp32c3/blinky.elf` enciende y
  apaga un LED en GPIO;
* `uart.elf` saluda por el `PuenteSerie` en la UART0 y devuelve lo que recibe
  (las recetas de `doc/puente_serie.md` valen sin cambiar una línea);
* el `SYSTIMER` interrumpe con el periodo programado;
* **`esp32c3_y_f407.xml`**: un C3 y un F407 hablándose por UART en la misma
  placa, cada uno con su firmware;
* `testc3` en el CI con su invariante, y `asanc3` limpio.

**Tamaño:** grande. Es el equivalente de las fases F1 y F3 del F407 juntas.

### E3 · Flash SPI, caché, MMU y la ROM

**Qué:** la pieza `SpiFlash` (en la placa o dentro del encapsulado, según el
descriptor: C3 frente a C3FH4); SPI0 y SPI1 (`spi_mem`); caché con MMU y coste
de fallo; carga de la ROM (`esp_rom.h`, `--rom`, búsqueda en la instalación de
ESP-IDF); formatos de imagen y `flash_args`; SHA-256 en `hash_algo.h`; los
registros sin documentar que toca el arranque, cada uno modelado o declarado.

**Cierre:**
* con la ROM de verdad y un `hello_world` de ESP-IDF sin Wi-Fi, la consola
  enseña la cabecera de la ROM (`rst:0x1 (POWERON_RESET),boot:0xc
  (SPI_FAST_FLASH_BOOT)`), el bootloader de segunda etapa, la tabla de
  particiones y `Hello world!`, y `esp_restart()` vuelve a empezar;
* un `blink` de Arduino-ESP32 y uno de Zephyr arrancan;
* con GPIO9 a masa al subir EN, la ROM escribe `DOWNLOAD_BOOT` y se queda
  esperando;
* `limitaciones()` dice qué no hay: radio, RSA, HMAC, DS, cifrado de Flash y
  arranque seguro.

**Tamaño:** grande, y **el de más riesgo**: es donde aparecen los registros sin
documentar.

### E4 · Descarga con esptool (D2)

**Qué:** la pieza `AutoResetEsp` (dos transistores y el RC de EN); el mínimo de
tiempo simulado entre cambios de DTR/RTS en el puente; la placa con su
`PuenteSerie` en UART0; las recetas en `doc/puente_serie.md`.

**Cierre:**
* una prueba en Python, al estilo de `gui-marcha`, que lanza `sim`, ejecuta
  `esptool --port rfc2217://localhost:3355 chip-id`, `write-flash` y
  `verify-flash`, **con el *stub* de esptool y con `--no-stub`**, y después
  comprueba que el firmware nuevo arranca;
* `idf.py -p rfc2217://localhost:3355 flash monitor` documentado y probado a
  mano en las tres plataformas;
* el invariante de `testserie` sin moverse.

**Tamaño:** pequeño. Todo lo caro lo pusieron E3 y P-14.

### E5 · Depuración (G1 y G2)

**Qué:** el Debug Module 0.13 con su modo depuración, los 8 *triggers*, el
acceso al bus de sistema y el *program buffer*; `gdb_arq_riscv.h`; el stub
interno (`depuracion="dm"`); el TAP con DTM en GPIO4-7; la pieza `SondaJtag`
con `remote_bitbang`; las configuraciones de Espressif-IDE y VS Code.

**Cierre:**
* con `verif/gdb_client.h`: parar, leer los 32 registros y el `pc`, poner y
  quitar un punto de ruptura hardware en Flash y uno software en IRAM, un
  *watchpoint*, un paso y `monitor reset halt`;
* el mismo guion, **con los mismos resultados**, por G1 y por G2; se mide la
  razón de velocidades, como T97;
* **`openocd-esp32` sin modificar** se engancha por `remote_bitbang`, lee el
  IDCODE del C3 y hace `program_esp` + `verify` (D4);
* `idf.py gdb` contra G1 llega a `app_main`.

**Tamaño:** grande. La mitad es el Debug Module y la otra mitad el TAP.

### E6 · Periféricos de aula y USB-Serial-JTAG

**Qué**, por orden de uso en prácticas: LEDC (PWM: el servo y los LED ya
existen), RMT (con la pieza `Ws2812`), GP-SPI2 (con la TFT ST7735), I²C (EEPROM
24Cxx), ADC SAR (sobre los nodos analógicos), GDMA, TWAI (con el bus CAN que
ya existe) y el sensor de temperatura. El **USB-Serial-JTAG**: el CDC por
RFC 2217 (D3), el reset a modo descarga por DTR/RTS del CDC, y su JTAG (G3).
AES y SHA con sus vectores.

**Cierre:** un firmware de prueba y una comprobación por periférico, como en F4
y F5 del F407, y los ejemplos de ESP-IDF correspondientes (`ledc_basic`,
`led_strip`, `i2c_eeprom`, `twai_network`…) contra las piezas de `parts/`.

**Tamaño:** grande, pero divisible: cada periférico es una entrega.

### E7 · El resto de la familia

| Chip | Lo que añade sobre el C3 | Qué supone |
| :--- | :--- | :--- |
| **ESP32-C6** | RV32IMAC; **un segundo núcleo LP** (otra instancia de la misma CPU con otro `RvCoreCaps`); PCR en vez de SYSTEM; otro controlador de interrupciones | El primer chip con **dos harts** de distinta clase: el DM con `hartsel` |
| **ESP32-H2** | RV32IMAC a 96 MHz, sin Wi-Fi | Casi solo descriptor y reloj |
| **ESP32-C2** (ESP8684) | RV32IMC, menos RAM, más ROM, sin USB-Serial-JTAG | Descriptor y quitar bloques |
| **ESP32-C5 / C61** | RV32IMAC, controlador CLIC | Un controlador de interrupciones nuevo |
| **ESP32-P4** | Dos núcleos HP RV32IMAFC con extensiones propias (SIMD, bucles hardware) más uno LP, sin radio, con USB HS, MIPI y mucho más | Otro proyecto: F, multihart simétrico y extensiones no estándar |

Las cifras de cada chip se escriben en su descriptor **con dos fuentes**
(datasheet y `soc/` de ESP-IDF), como las 29 referencias de ST.

Los coprocesadores ULP-RISC-V del ESP32-S2 y el S3 quedan fuera: el núcleo
principal de esos chips es Xtensa, y sin él el ULP no tiene sentido.

### E8 · `mcu-sim-gui`

Los dibujos de las placas (`esp32c3_devkitm1.svg`). El protocolo no cambia,
porque nombra los nodos con cadenas. Hay que comprobar que la ventana no da por
hecho nombres de la forma `Pxn` en ninguna parte.

### Orden y dependencias

```
E0 ──► E1 ──► E2 ──► E3 ──┬─► E4 (esptool)
                          ├─► E5 (GDB, OpenOCD) ──► G3 en E6
                          └─► E6 (periféricos) ──► E7 (familia)
E8 en paralelo desde E2
```

E4 y E5 se pueden hacer en cualquier orden después de E3. **Se recomienda E4
primero**: es pequeña, y con ella el alumno ya trabaja con su flujo de siempre
(`idf.py flash monitor`) aunque todavía tenga que depurar a base de `printf`.

---

## 9. Riesgos y preguntas abiertas

1. **La licencia de la ROM.** Decide si el CI puede ejecutar las pruebas de
   ESP-IDF o solo las *bare-metal*. Hay que resolverla antes de E3.
2. **Los registros sin documentar del arranque y del PHY.** Son el gran
   desconocido. Mitigación: empezar con un `hello_world` compilado **sin** Wi-Fi
   ni Bluetooth, y anotar cada registro que toque, con su fuente.
3. **El tiempo de esptool** (§4.2). Si no se resuelve, el auto-reset fallará al
   azar en máquinas lentas, que es el peor tipo de fallo.
4. **La caché.** Un modelo de aciertos y fallos sin geometría real dará
   tiempos que no son los de la placa. Hay que decidir cuánta fidelidad se pide
   y dejarlo escrito en `doc/todo.md`.
5. **La radio no está.** Las prácticas que la usan (MQTT, servidor web, BLE) no
   se pueden hacer en `mcu-sim`. Es la diferencia más visible con QEMU, que
   tampoco la modela pero ofrece una Ethernet virtual (OpenCores). Una MAC
   Ethernet por SPI (W5500, ENC28J60) colgada como pieza sería la forma de
   tener red sin modelar la radio, y reutilizaría el trabajo del puente
   Ethernet.
6. **Dos toolchains en el CI.** `arm-none-eabi-gcc` y `riscv32-esp-elf-gcc`.
   Como hoy, los firmwares se versionan y el compilador cruzado solo hace falta
   para regenerarlos (T-22).
7. **Revisiones del chip.** Si la ROM no coincide con la revisión que dice el
   eFuse, ESP-IDF avisa o se niega a arrancar. El descriptor tiene que fijar la
   pareja.
8. **¿El C3 o el C6 primero?** El C6 tiene más futuro (Wi-Fi 6, Thread/Zigbee,
   núcleo LP); el C3 es más sencillo y está en más aulas. Este análisis
   recomienda **el C3**, porque cada fase cierra antes y deja hecho lo que el C6
   reutiliza.

---

## 10. Fuentes consultadas

* Repositorio `mcu-sim`: `README.md`, `src/README.md`, `doc/multi_mcu.md`,
  `doc/reutilizacion.md`, `doc/puente_serie.md`,
  `doc/analisis_puente_usb.md`, `soc/mcu_if.h`, `top/mcu_caps.h`,
  `core/core_caps.h`, `core/cpu.h`, `core/debug_if.h`,
  `core/gdb_stub_dap.h`, `common/gdb_rsp.h`, `common/periph_base.h`,
  `common/ahb_types.h`, `pins/pin_mux.h`, `pins/encapsulado.h`,
  `verif/image_loader.h`, `parts/motor_uart.h`, `periph/hash_algo.h`,
  `src/Makefile.mcu-sim`.
* Repositorios `nanorv` y `nanorv-ocd` (README, `application/makefile`,
  `application/debugging.txt`, `nanorv.cfg`).
* Espressif, documentación de esptool: «Remote Serial Ports» y «Boot Mode
  Selection» (ESP32-C3), consultadas el 14-10-2026.
* Espressif, ESP-IDF Programming Guide (ESP32-C3): «QEMU Emulator» y «JTAG
  Debugging — Tips and Quirks», consultadas el 14-10-2026.
* Espressif Developer Portal, «Zephyr + Espressif QEMU» (09-2026): qué chips
  emula el QEMU de Espressif.
* Repositorio `espressif/esp-rom-elfs` (ELF de las ROM).
* *Pendientes de incorporar a `doc/refs/` y a `doc/fuentes.md`, sin
  versionarlos:* ESP32-C3 Technical Reference Manual, ESP32-C3 Series
  Datasheet, RISC-V Debug Specification 0.13.2 y RISC-V ISA Manual
  (Unprivileged y Privileged).
