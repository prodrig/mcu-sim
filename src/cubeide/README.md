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

**3. Ajustar dos cosas.** En la configuración importada, sustituir
`TU_PROYECTO` por el nombre real del proyecto y por la ruta de su `.elf`
—normalmente `Debug/TU_PROYECTO.elf`—. Nada más.

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
