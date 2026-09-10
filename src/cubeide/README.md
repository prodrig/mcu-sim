# Depurar desde STM32CubeIDE contra el simulador

`simulador.launch` es una configuración de lanzamiento lista para importar. Es el
camino corto: lo que hace falta es que el alumno no tenga que elegir nada.

## Los tres pasos

**1. Lanzar el simulador y dejarlo escuchando.** Sin firmware si lo va a cargar
el IDE, que es lo normal:

```
./build/sim placas/discovery_min.xml --gdb
```

Tiene que decir `[gdb] escuchando en localhost:3333` **antes** de darle a
depurar. No termina solo: se sale con Ctrl-C.

**2. Importar la configuración.** En CubeIDE, *File → Import… → Run/Debug →
Launch Configurations*, y elegir esta carpeta.

**3. Ajustar tres cosas.** En la configuración importada, sustituir
`TU_PROYECTO` por el nombre real del proyecto y por la ruta de su `.elf`
—normalmente `Debug/TU_PROYECTO.elf`—; y comprobar, en *Debugger → GDB
Command*, que ahí está **`arm-none-eabi-gdb`, con su ruta completa**. Lo
siguiente explica por qué eso no es un detalle.

## El otro error: «Truncated register 18 in remote 'g' packet»

Si el depurador que se lanza es el **`gdb` del PC** en vez del de ARM, la sesión
llega más lejos —negocia, lee `target.xml`, enumera hilos— y muere al pedir los
registros:

```
Error message from debugger back end:
Truncated register 18 in remote 'g' packet
```

Y la causa está escrita en el **primer** paquete de la sesión, que `--traza-gdb`
enseña:

```
qSupported:...;xmlRegisters=i386;error-message+
```

`xmlRegisters=i386`. Ese GDB es un depurador de PC. Las cuentas salen justas:
el modelo manda **23 registros de 32 bits = 184 caracteres**, y un GDB de i386
espera 8 registros de 32 bits, `EIP`, `EFLAGS` y seis de segmento —16 × 8 = 128
caracteres— y a continuación los de x87, que son de **80 bits**, o sea 20
caracteres cada uno: 128 + 20 + 20 = 168, y el registro 18 necesitaría llegar a
188. Hay 184. **Truncado en el registro 18**, exactamente.

No es que el paquete esté mal: es que lo está leyendo quien no debe. Un GDB de
x86 no entiende `<architecture>arm</architecture>` aunque se lo mandemos, porque
no lleva ARM dentro.

**El simulador lo dice ahora en cuanto pasa**, sin esperar al fallo:

```
[gdb] AVISO: el depurador conectado dice ser para 'i386', no para ARM.
[gdb]        Esto es el `gdb` del PC, no arm-none-eabi-gdb. Fallara en el paquete `g`
[gdb]        con "Truncated register ... in remote 'g' packet".
[gdb]        En el IDE: Debugger > GDB Command, con la RUTA COMPLETA de arm-none-eabi-gdb.
```

El `arm-none-eabi-gdb` de CubeIDE está dentro de su instalación, por la zona de

```
STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.<version>/tools/bin/
```

Merece la pena poner la ruta entera y no confiar en el `PATH`.

Depurar es entonces lo de siempre: se para en `main`, se pone un punto de
ruptura, se mira una variable, se avanza. La Flash la programa el propio GDB con
`load`, porque el stub implementa `vFlashErase` y `vFlashWrite`, igual que
programaría la del chip.

## El tercero: se carga el programa y el núcleo se bloquea

Síntoma: la sesión llega **entera** —carga, `Pf=` con el PC, `vCont;c`— y
entonces el IDE empieza a leer memoria en `0xffffffc0`, `0x0`, `0xffffff80` y
acaba matando la sesión con `vKill`. Esas direcciones son la firma de un núcleo
**bloqueado**: PC en `0xFFFFFFFE`, que es donde acaba un Cortex-M tras un
HardFault sin vector válido.

La causa está unos paquetes antes, en cómo se cargó el programa:

```
X8000000,188:...   X8000188,f78:...   X8001100,fa0:...
```

Paquetes **`X`**, o sea escrituras de memoria a pelo sobre `0x08000000`. Y **la
Flash no se escribe escribiendo**: el controlador ignora esas escrituras
mientras no se desbloquee y se ponga `PG`. En el modelo eso es literal
—`mem/flash_if.h` responde `TLM_GENERIC_ERROR_RESPONSE` con `LOCK` puesto— y por
SWD ese rechazo se convierte en un bit pegajoso de `CTRL/STAT`, no en un error
del paquete. Es decir: **la carga se pierde en silencio**, exactamente como en el
silicio. Luego se salta a `main`, allí hay `0xFF`, y de ahí al bloqueo.

**Corregido: el stub ya reconoce el rango.** Una sonda de verdad no escribe la
Flash a pelo, ejecuta la secuencia de programación, y este stub *es* la sonda.
Un `M`/`X` sobre `0x0800….` borra el sector la primera vez que se toca —solo la
primera, o cada trozo destruiría el anterior— y programa. Comprobado de punta a
punta: `verif/fw/blinky/blinky.bin` cargado por paquetes `X`, leído de vuelta
byte a byte idéntico, `SP`/`PC` puestos, `continue`, y al parar el PC estaba en
`0x080003C4`, dentro del programa.

Sigue habiendo un camino mejor y no está hecho: **anunciar el mapa de memoria**
(`qXfer:memory-map:read`), que es lo que hace que GDB use `vFlashErase` /
`vFlashWrite` —ya implementados— en vez de `X`. Sin mapa, GDB cae a `X`. Es el
punto **I-29**.

## El cuarto: el paso a paso cae siempre en `SysTick_Handler`

Síntoma: la sesión funciona, se llega a `main`, y al dar *step over* el
depurador aterriza en `SysTick_Handler` una vez tras otra, de modo que avanzar
por el código es imposible.

**No es del simulador ni del IDE: era del modelo, y ya está corregido.** Cuando
una sonda da un paso escribe en `DHCSR` dos bits juntos: `C_STEP` **y
`C_MASKINTS`**. El segundo dice «mientras doy este paso, las excepciones
configurables se quedan pendientes» —NMI y HardFault pasan igual, que no se
enmascaran—. Sin él, cada paso se come la interrupción que hubiera pendiente, y
en un proyecto de CubeIDE, con SysTick latiendo a 1 kHz, siempre hay una.

En el modelo el bit **se guardaba y no lo consultaba nadie**, y el stub tampoco
lo ponía. Ahora sí: `core_debug_if::dbg_mask_ints()`,
`Cpu::check_exceptions()` lo respeta y `GdbRsp::paso()` escribe
`C_STEP|C_MASKINTS|C_DEBUGEN`, quitándolo al reanudar —un `continue` tiene que
volver a recibir sus interrupciones o no se parecería a la realidad—.

Medido con el control negativo, mismo firmware y misma interrupción pendiente:

| DHCSR del paso | PC después | IPSR |
| :--- | :--- | :--- |
| `0xA05F0005` (sin `C_MASKINTS`, como antes) | `0x0800027E` | **15** — dentro de `SysTick_Handler` |
| `0xA05F000D` (con `C_MASKINTS`, ahora) | `0x0800018C` | **0** — sigue en el programa |

**Y una cosa que esto NO arregla,** por si se nota al reanudar: en el modelo el
SysTick **sigue contando con el núcleo parado**, y además el tiempo simulado
corre muy deprisa mientras nadie ejecuta. O sea que mientras miras una variable
pueden pasar segundos simulados y al continuar hay una interrupción esperando
siempre. En ARMv7-M el contador del SysTick no decrementa con el procesador
detenido en Debug state; el modelo aún no lo hace. Es **I-31**, y va de la mano
del freno de tiempo real (**I-25**).

## Se para en `Reset_Handler` y no en `main`

Eso es la configuración, no el simulador. En la pestaña *Startup* de
`GDB Hardware Debugging`:

* **Set breakpoint at:** `main` — marcado;
* **Resume** — marcado.

Sin eso, GDB deja el PC en el punto de entrada y para ahí, que es exactamente
`Reset_Handler`. El `simulador.launch` de esta carpeta ya lo trae puesto.

## Por qué esta configuración y no la de siempre

Porque la de siempre no funciona, y el mensaje que da despista.

El tipo de lanzamiento normal de CubeIDE —`STM32 C/C++ Application`— con
cualquiera de las dos sondas de ST, `ST-LINK (ST-LINK GDB server)` o
`ST-LINK (OpenOCD)`, comprueba **antes de nada** que al otro extremo del puerto
esté el servidor de ST hablando con una sonda ST-LINK física. Lo hace con una
orden `monitor` propia suya:

```
monitor ReadAPEx 0x0 0xF8
```

—leer el registro `BASE` del AP 0, que apunta a la ROM table—. Si no obtiene la
respuesta que espera, se despide (`D`) y acusa al servidor:

```
Could not verify ST device! Please verify that the latest version of
the GDB-server is used for the connection
```

Lo cual, leído literalmente, manda a buscar el problema donde no está: el
identificador del chip es correcto —`DBGMCU_IDCODE` = `0x10016413`, DEV_ID
`0x413`— y el stub responde bien a todo lo que es GDB estándar.

Esta configuración usa `GDB Hardware Debugging`, que se conecta a un GDB remoto
por TCP y no da nada por supuesto sobre quién sirve el puerto. Que es lo que el
simulador es: un servidor de GDB, no un ST-LINK.

**El stub responde a `ReadAPEx` y `WriteAPEx`, y aun así no basta.** Probado
contra el IDE, con la traza de entrada y de salida delante:

```
[gdb] <- qRcmd,5265616441504578203078302030784638      (= ReadAPEx 0x0 0xF8)
[gdb] -> 307845303046463030330a                        (= "0xE00FF003\n")
[gdb] <- D
```

El stub contesta —con el valor correcto: `0xE00FF003` es el `BASE` del AP 0, el
puntero a la ROM table— y **ST se despide igual, sin reintentar y sin pedir nada
más**. Si estuviera identificando el dispositivo de verdad, con ese puntero
recorrería la ROM table con paquetes `m`, que el stub sirve; no lo hace. O sea
que lo que rechaza no es el valor: es el formato de la respuesta, o el hecho
mismo de que quien contesta no sea su servidor.

**Y ahí se acaba lo que se puede averiguar sin documentación de ST.** El
protocolo de sus órdenes `monitor` no está publicado; seguir es probar formatos
a ciegas, sin saber cuántas órdenes más vendrían detrás si se acertara con esta.
Por eso la configuración de `GDB Hardware Debugging` de esta carpeta no es un
apaño temporal: **es el camino soportado**, y el que se reparte.

Para mirar una sesión por dentro:

```
./build/sim placas/discovery_min.xml --gdb --traza-gdb
```

imprime cada paquete recibido **y cada respuesta**, más la fecha en que se
construyó el binario:

```
[gdb] traza de paquetes activada (stub construido el Sep 10 2026 14:02:44)
[gdb] <- qRcmd,5265616441504578203078302030784638
[gdb] -> 307845303046463030330a
```

Esas dos líneas son las que hacen falta para saber en qué mitad está el
problema. Si tras el `qRcmd` **no** aparece un `->` con la respuesta, o la
respuesta es `(vacia)`, el binario que corre no lleva `ReadAPEx`: falta
recompilar (`make sim`). Si sí aparece —`307845...` es `0xE00FF003\n` en
hexadecimal— entonces el stub contestó y **fue ST quien no aceptó el formato**,
que es lo único que aquí no se puede saber sin documentación suya.

Y la fecha de construcción está ahí por un motivo práctico: cuando un fallo se
persigue a base de cambios en el stub, la primera pregunta ante una traza que no
cambia es si lo que está corriendo lleva el cambio dentro.
