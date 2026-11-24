# mcu-sim

**Un microcontrolador que no existe, y se comporta como si existiera.**

[![suites](https://github.com/prodrig/mcu-sim/actions/workflows/suites.yml/badge.svg)](https://github.com/prodrig/mcu-sim/actions/workflows/suites.yml)

`mcu-sim` es un modelo en **SystemC 2.3.4** de microcontroladores con tres
núcleos distintos: **Cortex-M4F** (los STM32 y los MSP432), **RISC-V** (los
ESP32-C3 y C6) y **MSP430** (las LaunchPad de TI). Ejecuta el **binario de
verdad** —el que sale de STM32CubeIDE, Code Composer Studio, ESP-IDF o
`msp430-elf-gcc`, sin recompilar ni adaptar nada— sobre ese núcleo simulado
instrucción a instrucción, con sus periféricos y con sus pines.

Está hecho para una situación concreta: **un alumno que no tiene la placa
delante**. Desarrolla en su IDE como siempre, y en vez de programar la placa,
lanza su firmware contra este programa y observa lo mismo que
observaría con un osciloscopio y un depurador.

---

## Qué modela

**Doce familias de tres fabricantes, 65 referencias**, cada una con sus
diferencias reales, no una sola con interruptores:

| Familia | Referencias | Placa de pruebas | De qué va |
| :--- | ---: | :--- | :--- |
| **STM32F405 / F407** | 11 | STM32F4DISCOVERY | La pieza principal. Es la que tiene la suite grande |
| **STM32F415 / F417** | 10 | una mínima, `cripto_f417.xml` | Las mismas, con el acelerador criptográfico (AES, DES, HASH) |
| **STM32F446** | 8 | NUCLEO-F446RE | La prueba cruzada: lo que el F446 **no** lleva y decodifica distinto, y lo que lleva de más (FMPI2C, QUADSPI, SAI) |
| **STM32F302 / F303 / F358** | 15 | STM32F3DISCOVERY | La **segunda generación de periféricos de ST**, la de las F0, F3, L4 y G4: otro DMA, otro ADC, otro USB; y los sensores de la placa (`doc/analisis_stm32f303.md`) |
| **MSP432P401R** | 1 | MSP-EXP432P401R | **Otro fabricante**, el mismo núcleo: Texas Instruments. Con su ROM de DriverLib y su BSL (`doc/analisis_msp432.md`) |
| **MSP432E401Y** | 1 | MSP-EXP432E401Y | El otro MSP432 de TI, con Ethernet, USB, CAN y criptografía |
| **ESP32-C3** | 5 | ESP32-C3-DevKitM-1, DevKitC-02 y SuperMini | **Otro núcleo y otro fabricante**: RISC-V de Espressif, con su ROM, ESP-IDF, la Flash SPI con caché y el USB-Serial-JTAG (`doc/analisis_esp32_riscv.md`) |
| **ESP32-C6** | 3 | ESP32-C6-DevKitC-1 | El siguiente RISC-V de Espressif: el núcleo HP y el núcleo LP como segundo hart, con sus periféricos propios |
| **MSP430G2553** | 2 | MSP-EXP430G2ET | **El tercer núcleo**: el MSP430 de 16 bits de TI, en su versión más sencilla (`doc/analisis_msp430.md`) |
| **MSP430F5529** | 1 | MSP-EXP430F5529LP | La CPUX de 20 bits, con USB; y es el chip de la **eZ-FET**, la sonda de las LaunchPad, que corre emulada con el firmware de TI |
| **MSP430FR2433 / FR4133 / FR2311 / FR2355 / FR2476** | 5 | sus cinco LaunchPad | La familia FR2xx/FR4xx, con FRAM, el LCD del FR4133 y los analógicos del FR2311 y el FR2355 |
| **MSP430FR5969 / FR5994 / FR6989** | 3 | sus tres LaunchPad | La familia FR5xx/FR6xx: la FRAM con MPU, el LEA del FR5994 y el ESI y el LCD del FR6989 |

`mcu-sim --help` las lista una a una, y `tipo=` en el XML de una placa elige
entre ellas.

**Los núcleos** ejecutan su juego de instrucciones de verdad: Thumb-2 con FPU,
excepciones, NVIC, MPU y lockup; RV32IMC y RV32IMAC con sus CSR, el Debug
Module de RISC-V y el controlador de interrupciones de Espressif; y la CPU,
CPUX y CPUXv2 del MSP430, con su EEM y su JTAG/Spy-Bi-Wire. **Los
periféricos** responden en sus registros y con sus tiempos:
* en los STM32F4, RCC, DMA, temporizadores, USART, SPI/I²S, I²C, ADC, DAC,
  RTC, SDIO, CRC/RNG, bxCAN, DCMI, FSMC, USB OTG y Ethernet;
* en el F303, sus versiones de segunda generación —USART, DMA por canales,
  ADC por parejas, comparadores, amplificadores, USB de dispositivo— y el
  cargador de sistema;
* en los MSP432, los de TI: eUSCI, Timer_A, ADC14 o el ADC de Tiva, uDMA,
  la Flash con su controlador; y en el E4, además, EMAC, USB, C_CAN y el
  acelerador criptográfico;
* en los ESP32, los de Espressif: GPIO con su matriz, UART, SPI, I²C, I²S,
  LEDC, RMT, TWAI, GDMA, SYSTIMER, TIMG, SAR ADC, la criptografía (SHA, AES,
  RSA, HMAC, DS, XTS-AES) y, en el C6, MCPWM, PCNT, ETM y el dominio LP;
* en los MSP430, los de cada familia: Basic Clock y UCS o CS, Timer_A/B,
  USCI y eUSCI, ADC10, ADC12 y el ADC de las FR, comparadores, SAC, LCD,
  DMA, MPY32, RTC, la FRAM con su MPU, el USB del F5529, el LEA y el ESI, y
  los BSL de TI.

Lo que no está modelado lo dice cada chip al montarse, y está en
`doc/todo.md`.

**Los pines son eléctricos.** No son `0` y `1`: son tensiones y corrientes en
coma flotante, con alta impedancia, *pull-up* y *pull-down*, salida en colector
abierto o en contrapunto, e impedancia de salida según su velocidad. Dos
salidas peleándose por un nodo dan lo que darían de verdad. Eso permite colgar
del chip **piezas externas** —76 tipos: LED, pulsadores, cristales, memorias,
sensores, pantallas, servos, motores con sus drivers, una fresadora CNC
entera— y montar placas, o varias enchufadas entre sí, desde un fichero XML
(`doc/parts.md`).

**Se depura como una placa.** Hay servidor GDB, por la sonda soldada a los
pines o por dentro, así que `arm-none-eabi-gdb`, `riscv32-esp-elf-gdb` y
`msp430-elf-gdb` se conectan igual que a la sonda de la placa. Y también
OpenOCD sin modificar, por `remote_bitbang` o CMSIS-DAP, el OpenOCD de
Espressif por el USB-Serial-JTAG, `mspdebug` y la `libmsp430` de TI contra
la eZ-FET emulada, y las herramientas de carga de cada fabricante:
`stm32flash` contra el cargador del F303, `sflash` contra el del E4,
`esptool` contra la ROM de los ESP32, y BSL Scripter contra el BSL del P4 y
los de los MSP430.

**La ventana va aparte**: `mcu-sim-gui` (Qt 6), en su propio repositorio,
enseña la placa con su dibujo, deja tocarla y lanza el simulador.

---

## Cómo se compila

En Linux, dos líneas:

```bash
sudo apt install libsystemc-dev
cd src && make -f Makefile.mcu-sim test407
```

En **macOS** son igual de pocas: `brew install systemc` y a compilar. En
**Windows (MSYS2, en el entorno UCRT64)** hay que construir SystemC, que es la
única plataforma sin paquete —`ci/construye_systemc.sh` lo hace—; está contado paso a paso en `doc/compilacion.md`, junto con el
catálogo de fallos por síntoma, que es la parte que ahorra tardes.

**Y si no quieres compilar nada**, hay ejecutables publicados en *Releases* para
Windows, Linux y los dos macOS, construidos por la integración continua y
publicados **solo si en su plataforma pasaron todas las suites y sus
invariantes coincidieron al picosegundo**. Qué descargar, qué instalar —en macOS,
un `brew install systemc`— y cómo convencer a Gatekeeper: **`doc/ejecutables.md`**.

No hace falta ningún compilador cruzado —ni de ARM, ni de RISC-V, ni de
MSP430—: los firmwares que usan las pruebas **vienen en el repositorio**.

---

## Cómo se sabe que funciona

Veinte bancos de pruebas, que se ejecutan en cada cambio y en cuatro
plataformas. Cada uno da sus comprobaciones y el tiempo simulado que tarda,
que es su **invariante** (en `resto`, sin lo que hablan con un GDB de verdad):

```
make test407     # 2165 comprobaciones    2281249034555 ps (resto)   STM32F407
make test446     #  204                   1033367277932 ps           STM32F446
make test417     #  165                       718988288 ps           STM32F417
make testf3      #  428                 116875480154660 ps (resto)   STM32F303
make testp4      #  406                  22456139139423 ps (resto)   MSP432P401R
make teste4      #  296                   4505148675235 ps (resto)   MSP432E401Y
make testrv      #  473                     63207020000 ps (resto)   el núcleo RISC-V
make testc3      #  313                  18846968240195 ps (resto)   ESP32-C3
make testc6      #  202                  53183701458348 ps (resto)   ESP32-C6
make test430     #  776                   8295637000000 ps (resto)   el núcleo MSP430
make testg2      #   51                  12414990473648 ps           MSP430G2553
make testf5      #   62                  26455455250005 ps           MSP430F5529
make testfr2     #   36                  24351397000000 ps           MSP430FR2433
make testfr4     #   31                  35407103000000 ps           MSP430FR4133
make testfr2311  #   16                   1496903000000 ps           MSP430FR2311
make testfr2355  #   37                   7190426667008 ps           MSP430FR2355
make testfr2476  #   25                   7577023000000 ps           MSP430FR2476
make testfr5     #   99                  42490696000000 ps           FR5969, FR5994 y FR6989
make testserie   #  189                    400677589564 ps           el puente serie
make testgui     #  145                     76450000000 ps           el protocolo de la ventana
```

**La cifra que importa es la segunda.** Un cambio puede dejar las 2 151
comprobaciones en verde y haber cambiado el comportamiento del modelo; lo que
lo delata es que el reloj simulado se mueva. Las cifras están escritas una sola
vez, en `src/verif/invariantes.txt`, y la integración continua las contrasta
**al picosegundo** en las cuatro plataformas: si no coinciden, es un fallo y no
un aviso.

Donde pone `resto` se contrasta esa cifra: el total menos lo que tarda un GDB
de verdad en contestar por un socket, que depende de la máquina (T-16). El del
F407 se ha movido cuatro veces desde la fase 7, y las cuatro a propósito:
* un tick de más del IWDG (T-23);
* dos pruebas nuevas, T131 y T132;
* las reglas de error del CAN (T-24).

Cada una está explicada en el propio `invariantes.txt`.

**Y contra el mundo de fuera**, con programas que no son nuestros:
* la ventana, `mcu-sim-gui`, por su protocolo (`make gui-saludo`,
  `gui-sistema`, `gui-cnc`...);
* OpenOCD (el de siempre y el de Espressif), pySerial y socat;
* `esptool` contra el C3 y el C6, y el firmware de la eZ-FET de TI con la
  `libmsp430` (`make testezfet`, `ezfet-tilib`);
* `sflash` y BSL Scripter;
* lwIP por el puente Ethernet y TinyUSB por el USB.

Casi todo eso va también en el CI de Linux. BSL Scripter y la eZ-FET, no: el
primero solo se baja de la web de TI, y las imágenes de la segunda son de la
MSP Debug Stack, que no se puede versionar.

---

## Cómo está escrito, y por qué así

Tres reglas que explican casi todas las decisiones del repositorio:

**El modelo nunca es más permisivo que el silicio.** Si el chip da error de
bus, aquí se da error de bus. Lo que no está modelado **está declarado**, con
su identificador, en `doc/todo.md`: hoy son más de 170 puntos, más las listas
de cada familia, cada uno con el motivo escrito; y `doc/resumen_todo_*.md`
los ordena por familia, de más a menos grave para un programa que tenga que
funcionar igual en el chip. No es una lista de deberes, es el mapa de hasta dónde se puede confiar
en lo que sale.

**Una comprobación que no puede fallar es decoración, y se quita.** Ha pasado
más de una vez y está anotado cuándo.

**Dos fuentes para cada dato.** Ningún valor de registro, de pin, de canal de
DMA o de vector de interrupción entra sin estar contrastado en dos sitios, y
cuando las fuentes se contradicen —que pasa— la discrepancia queda escrita con
la resolución que se tomó.

---

## La documentación está en `doc/`

Esto es la portada; lo que hay debajo es bastante más largo.

| Documento | Para qué |
| :--- | :--- |
| `doc/ejecutables.md` | **Empieza aquí si solo quieres usarlo.** Qué descargar en cada plataforma y cómo saltarse a Gatekeeper |
| `doc/compilacion.md` | **Empieza aquí si algo no compila.** Las tres plataformas, y un catálogo de fallos por síntoma |
| `TERCEROS.md` | El software ajeno que lleva dentro, con qué licencia, y qué de eso viaja en el ejecutable |
| `doc/todo.md` | Lo que no está modelado, lo que se sabe que diverge del silicio y lo que falta por verificar. **El documento más útil del proyecto** |
| `doc/resumen_todo_*.md` | Lo mismo, por familia (STM32, MSP432, ESP32, MSP430) y ordenado por cuánto puede esconder que un programa falle en el chip, con lo que cuesta cada punto |
| `doc/integracion_continua.md` | Qué comprueba el CI y por qué el criterio es el tiempo simulado |
| `doc/parts.md` | **Para montar una placa.** El formato del XML —chips, nodos, piezas, sistemas de varias placas, el dibujo y la vista 3D— y el catálogo de las 69 piezas |
| `doc/puente_serie.md` | El puerto serie del MCU en tu ordenador: terminales, puertos COM y herramientas de carga |
| `doc/multi_mcu.md` | Placas con varios chips, y depurar dos a la vez |
| `doc/fuentes.md` | Qué manual, qué revisión y de dónde se baja. Los PDF de ST y de TI **no** están en el repositorio: son suyos, y **los informes técnicos internos tampoco**, por la razón que explica su §0 |
| `doc/analisis_*.md` | Los análisis y planes de cada ampliación, con su estado al principio: los MSP432, el STM32F303, los ESP32-C3 y C6, los MSP430, el puente serie, el USB y el Ethernet, las placas enchufadas, la ventana, la sonda XDS110, la fresadora CNC y la radio |
| `doc/stm32f4xx/` | Los planes de fase que originaron cada parte del modelo del F4, las comparativas entre piezas y los prompts con que se escribieron los informes |
| `src/README.md` | El recorrido por el código: qué hay en cada carpeta y por qué |
| `doc/chat.md` | El diario de trabajo, sin editar. Incluye los diagnósticos equivocados |

**Lo que no viene en el repositorio, y hay que decirlo antes de que sorprenda.**
Los comentarios del modelo del F4 citan más de mil veces un informe técnico
interno, `[IR, §x]` y `[II, §y]`, que **no está aquí**: se escribió con la
regla de reproducir el manual de ST en vez de remitir a él, y eso lo convierte
en obra derivada de un documento que no es nuestro. Cada cita sigue diciendo
qué sección respalda el código; para leerla hace falta el RM0090, que es de
donde salió. `doc/fuentes.md` dice cuál y en qué revisión.

**Y por qué los hashes de la documentación no existen aquí.** Los informes
estuvieron versionados hasta el **23 de septiembre de 2026**, y sacarlos exigió
reescribir la historia pública entera con `git filter-repo`. No se ha perdido
ningún commit —están los mismos, en el mismo orden y con los mismos mensajes—
pero **todos cambiaron de hash**, así que los que cita `doc/todo.md`,
`doc/chat.md` y `doc/integracion_continua.md` —`4a14566`, `45c39f7`,
`97aabd1`…— son de la historia anterior y no resuelven. Se localizan por el
asunto del commit, que sí es el mismo.

---

## Estado

Siete fases del F407 cerradas, y encima, una ampliación tras otra, cada una
con su plan en `doc/`:

| Qué | Plan | Estado |
| :--- | :--- | :--- |
| Las familias F446 y F415/F417 | `doc/stm32f4xx/` | Hechas |
| El puente UART hasta el ordenador | `doc/analisis_puente_serie.md` | Hecho |
| La ventana, `mcu-sim-gui`, en dos procesos | `doc/analisis_gui.md` y el plan del otro repositorio | Fases 0 a 7 hechas |
| Varias placas enchufadas: `<sistema>` | `doc/analisis_placas_conectadas.md` | Hecho |
| Los MSP432P401R y MSP432E401Y | `doc/analisis_msp432.md` | T0 a T7 hechas; T8, el resto de la familia, pendiente |
| Los puentes Ethernet y USB, y la sonda XDS110 | `doc/analisis_puente_ethernet.md`, `analisis_puente_usb.md` y `analisis_xds110.md` | Hechos en lo que piden los MSP432 y el F303 |
| El STM32F303 y la STM32F3DISCOVERY | `doc/analisis_stm32f303.md` | S0 a S9 hechas, para el die 0x422 (F303xB/C, F302xB/C, F358xC); los dies 0x446 y 0x438, analizados (`analisis_f303xD-E_f303x6_8_F334.md`); el 0x439 y el 0x432, sin analizar (`doc/parts.md` §2.0) |
| Una fresadora CNC de tres ejes | `doc/analisis_cnc.md` | F0 a F8 hechas |
| El núcleo RISC-V y los ESP32-C3 y C6 | `doc/analisis_esp32_riscv.md` | E0 a E11 y E13 hechas; E12, la radio paravirtual, pendiente (depende del análisis de la radio) |
| El núcleo MSP430 y diez MSP430 con sus LaunchPad | `doc/analisis_msp430.md` | M0 a M12 hechas: los diez chips, sus BSL, la eZ-FET emulada con el firmware de TI y sus placas en la ventana |
| Los puentes Wi-Fi y Bluetooth: módulos AT, procesadores de red, HCI y la radio del chip | `doc/analisis_wifi_bl.md` | Analizados; nada hecho |
| El `contenido` de las memorias no volátiles: bytes de opción, OTP, TLV, BSL y registros del E4 | `doc/analisis_contenido.md` | Analizado, y hecho en cinco parches |

**Verificado en las cuatro plataformas** —Linux (Ubuntu 22.04, 24.04 y 26.04),
Windows, macOS Apple Silicon y macOS Intel— por la integración continua, que
en cada empujón pasa todas las suites **y comprueba que el tiempo simulado no
se mueva un picosegundo**.

Y esas mismas cifras salen con **tres versiones de SystemC** —la 2.3.3 de
Ubuntu 22.04, la 2.3.4 de Ubuntu 24.04 y Windows, y la 3.0.2 de Ubuntu 26.04 y
Homebrew—, dos compiladores y tres sistemas operativos. No salió gratis: en
noviembre de 2026 hubo que pasar a enteros los cálculos de tiempo que daban
otra cifra con la 3.0.2 (I-55 e I-56 de `doc/todo.md`).

La contraparte gráfica vive en un repositorio aparte, **`mcu-sim-gui`** (Qt 6),
y se comunica con éste por un socket. `mcu-sim` tiene que seguir compilándose
**sin Qt** en cualquier máquina, que es lo que hace que sus pruebas valgan en
todas partes.

## Licencia

**GNU Affero General Public License, versión 3** (`LICENSE`).

En las palabras que importan para lo que es esto: **puedes usarlo, estudiarlo,
modificarlo y repartirlo libremente**, y si lo haces —añadir el core de otro
microcontrolador, un periférico nuevo, una placa— **tus cambios tienen que
quedar disponibles bajo esta misma licencia**. Esa obligación es el motivo de
elegirla y no un efecto secundario: lo que se construya encima de un simulador
didáctico debe poder usarlo el siguiente que lo necesite.

**Y la «A» de AGPL cubre un caso que aquí no es hipotético.** La GPL corriente
solo obliga a publicar cuando se *distribuye* el programa; un departamento que
monte mcu-sim en un servidor y lo ofrezca a sus alumnos por la red no
distribuiría nada y no tendría que compartir nada. La AGPL cierra ese hueco:
ofrecerlo por red cuenta igual que repartirlo.

El programa lo dice de sí mismo —`mcu-sim --licencia` imprime la licencia,
dónde está el fuente y el software ajeno que lleva dentro—, lo cual no es
adorno: es como el aviso viaja cuando alguien copia solo el ejecutable.

**El software de terceros conserva la suya**, que es más permisiva y compatible:
SystemC, CMSIS y CoreMark son Apache-2.0 —las demás, como las de los SDK de
ST y de TI que acompañan a algunos firmwares de prueba, están en
`TERCEROS.md`—, y `libstdc++`/`libgcc` van con la GCC
Runtime Library Exception. El inventario, y qué parte viaja en cada paquete,
está en **`TERCEROS.md`**. Apache-2.0 es compatible con AGPLv3 en esta
dirección; con la GPLv2 no lo habría sido, así que esa nunca estuvo sobre la
mesa.
