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

**El stub responde también a `ReadAPEx` y `WriteAPEx`** desde la versión que
acompaña a este fichero, así que la sonda de ST podría llegar a funcionar. Pero
el **formato de la respuesta** no está publicado por ST: lo que devuelve el
modelo —el valor en texto, `0xE00FF003`— es una conjetura razonada, no un hecho
verificado. Mientras no se confirme contra el IDE, **esta configuración es la
que se sabe que funciona**. Para comprobar si la otra ya pasa:

```
./build/sim placas/discovery_min.xml --gdb --traza-gdb
```

imprime cada paquete recibido, y el último antes de `cliente desconectado` dice
exactamente dónde se atascó.
