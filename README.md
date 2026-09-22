# mcu-sim

**Un microcontrolador STM32F4 que no existe, y se comporta como si existiera.**

[![suites](https://github.com/prodrig/mcu-sim/actions/workflows/suites.yml/badge.svg)](https://github.com/prodrig/mcu-sim/actions/workflows/suites.yml)

`mcu-sim` es un modelo en **SystemC 2.3.4** de varios microcontroladores de la
familia STM32F4. Ejecuta el **binario de verdad** —el `.bin` que sale de
STM32CubeIDE, sin recompilar ni adaptar nada— sobre un núcleo Cortex-M4F
simulado instrucción a instrucción, con sus periféricos y con sus pines.

Está hecho para una situación concreta: **un alumno que no tiene la placa
delante**. Desarrolla en STM32CubeIDE como siempre, y en vez de programar una
NUCLEO, lanza su firmware contra este programa y observa lo mismo que
observaría con un osciloscopio y un depurador.

---

## Qué modela

**Tres piezas**, con sus diferencias reales, no una sola con interruptores:

| | Referencia | De qué va |
| :--- | :--- | :--- |
| **F407VG** | LQFP100, 168 MHz | La pieza principal. Es la que tiene la suite grande |
| **F446RE** | NUCLEO-F446RE | La prueba cruzada: lo que el F446 **no** lleva y decodifica distinto |
| **F415 / F417** | — | El acelerador criptográfico (AES, DES, HASH) |

**El núcleo** ejecuta Thumb-2 de verdad, con FPU, excepciones, NVIC, MPU y
lockup. **Los periféricos** —RCC, DMA, temporizadores, USART, SPI/I²S, I²C,
ADC, DAC, RTC, SDIO, CRC/RNG, bxCAN, DCMI, FSMC, USB OTG, Ethernet— responden
en sus registros y con sus tiempos.

**Los pines son eléctricos.** No son `0` y `1`: son tensiones y corrientes en
coma flotante, con alta impedancia, *pull-up* y *pull-down*, salida en colector
abierto o en contrapunto, e impedancia de salida según `OSPEEDR`. Dos salidas
peleándose por un nodo dan lo que darían de verdad. Eso permite colgar del
chip piezas externas —LED, pulsadores, cristal, memorias— y montar placas
completas desde un fichero XML.

**Se depura como una placa.** Hay servidor GDB, por la sonda soldada a los
pines o contra el DAP interno, así que `arm-none-eabi-gdb` se conecta igual que
a un ST-LINK.

---

## Cómo se compila

En Linux, dos líneas:

```bash
sudo apt install libsystemc-dev
cd src && make -f Makefile.mcu-sim test407
```

En **Windows (MSYS2)** y en **macOS** hay que construir SystemC antes; está
contado paso a paso en `doc/compilacion.md`, junto con el catálogo de fallos
por síntoma, que es la parte que ahorra tardes.

No hace falta compilador cruzado de ARM: los firmwares que usan las pruebas
**vienen en el repositorio**.

---

## Cómo se sabe que funciona

Tres bancos de pruebas, que se ejecutan en cada cambio y en cuatro plataformas:

```
make test407    # 2118 comprobaciones    2336217899213 ps
make test446    #  204                   1033367277932 ps
make test417    #  165                    718988288 ps
```

**La cifra que importa es la segunda.** Un cambio puede dejar las 2 118
comprobaciones en verde y haber cambiado el comportamiento del modelo; lo que
lo delata es que el reloj simulado se mueva. El del F407 lleva valiendo lo
mismo **al picosegundo** desde hace once planes de trabajo, y vale lo mismo en
Linux y en Windows. Por eso la integración continua lo trata como un fallo y no
como un aviso.

---

## Cómo está escrito, y por qué así

Tres reglas que explican casi todas las decisiones del repositorio:

**El modelo nunca es más permisivo que el silicio.** Si el chip da error de
bus, aquí se da error de bus. Lo que no está modelado **está declarado**, con
su identificador, en `doc/todo.md`: hoy son 171 puntos, cada uno con el motivo
escrito. No es una lista de deberes, es el mapa de hasta dónde se puede confiar
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
| `doc/compilacion.md` | **Empieza aquí si algo no compila.** Las tres plataformas, y un catálogo de fallos por síntoma |
| `doc/todo.md` | Lo que no está modelado, lo que se sabe que diverge del silicio y lo que falta por verificar. **El documento más útil del proyecto** |
| `doc/integracion_continua.md` | Qué comprueba el CI y por qué el criterio es el tiempo simulado |
| `doc/fuentes.md` | Qué manual, qué revisión y de dónde se baja. Los PDF de ST **no** están en el repositorio: son suyos |
| `doc/stm32f4xx/` | Los informes técnicos que originaron cada fase, y la tabla de codificaciones de instrucciones |
| `src/README.md` | El recorrido por el código: qué hay en cada carpeta y por qué |
| `doc/chat.md` | El diario de trabajo, sin editar. Incluye los diagnósticos equivocados |

---

## Estado

Siete fases cerradas más dos planes de familia. **Verificado en Linux y en
Windows**; macOS entra ahora por la integración continua y hasta que no haya
ejecutado no se dará por bueno.

La contraparte gráfica vive en un repositorio aparte, **`mcu-sim-gui`** (Qt 6),
y se comunica con éste por un socket. `mcu-sim` tiene que seguir compilándose
**sin Qt** en cualquier máquina, que es lo que hace que sus pruebas valgan en
todas partes.

## Licencia

**Pendiente de decidir.** El material de terceros trae la suya
(`src/verif/fw/cmsis/LICENSE-*.md`, `src/verif/fw/coremark/LICENSE.md`); el
código propio, todavía no. Hasta que la haya, no hay permiso de uso concedido,
cosa que hay que arreglar antes de repartirlo a nadie.
