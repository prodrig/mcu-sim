# Fase F6 (ampliación) — Servidor GDB/RSP sobre los pines de depuración

Ampliación de la fase F6 (`doc/stm32f407vg_fase6_debug.md`), que dejó modelado
el subsistema CoreSight completo hasta los pines. Aquí se añade la pieza que
convierte el modelo en un **objetivo de depuración de verdad**: un servidor
GDB/RSP que por un lado habla SWD sobre `SWCLK`/`SWDIO` y por el otro escucha en
un puerto TCP.

Fuentes: `doc/informe_revisado.md` [IR, cap. 13], la especificación *ARM Debug
Interface v5* (ADIv5) para el modelo DP/AP, y la documentación de ARM sobre el
interfaz JTAG/SWD que motivó este trabajo.

**Resultado:** con el modelo corriendo en modo servidor,

```
./build/stm32f407vg --gdb [--port=3333] [imagen.bin]
```

Eclipse CDT, STM32CubeIDE o un `arm-none-eabi-gdb` a pelo se conectan con

```
target extended-remote localhost:3333
```

y obtienen una sesión completa: descarga a Flash, ejecución, parada, paso a
paso, puntos de ruptura, watchpoints y acceso a registros y memoria. La suite
pasa **1362 de 1362 comprobaciones** (37 nuevas en el grupo T96).

---

## 1. Qué es y qué no es

**Es una sonda, no parte del MCU.** El stub vive en `src/verif/`, se suelda a dos
pines y **no ve nada del modelo que no pase por ellos**. No tiene un puntero al
núcleo, ni al bus, ni a la memoria. Todo lo que hace —leer un registro del
procesador, escribir en la SRAM, programar la Flash— lo consigue mandando
paquetes SWD uno detrás de otro, exactamente como el firmware de un ST-LINK.

Esa restricción es deliberada y es lo que hace que la prueba valga: si el stub
pudiera hacer trampa, no estaría verificando nada.

```
   Eclipse / CubeIDE / gdb                      modelo SystemC
   ┌─────────────────┐   TCP 3333   ┌──────────┐  SWD   ┌──────────────┐
   │      GDB        │◄────────────►│ GdbStub  │◄──────►│ PA14 / PA13  │
   └─────────────────┘     RSP      └──────────┘ 2 hilos└──────────────┘
                                                            │
                                                     SWJ-DP → AHB-AP → bus
```

## 2. Ficheros

| Fichero | Contenido |
| :--- | :--- |
| `src/verif/swd_port.h` | **Nuevo.** El maestro SWD a nivel de bit, extraído de la sonda de T94 para que lo compartan la sonda y el stub. Aquí viven todas las peculiaridades de ADIv5 |
| `src/verif/gdb_stub.h` | **Nuevo.** El servidor: socket TCP no bloqueante, tramado del RSP, intérprete de paquetes y programador de Flash |
| `src/verif/gdb_client.h` | **Nuevo.** Un GDB de mentira, para verificar el stub de punta a punta sin depender de que haya un GDB instalado |
| `src/core/debug.h` | Ampliado: el SW-DP del modelo pasa a contestar `WAIT` y `FAULT`, a llevar los bits pegajosos de `CTRL/STAT` y a respetar la frontera de 1 KiB |
| `src/top/sc_main.cpp` | Grupo T96 (37 comprobaciones) y el modo `--gdb` |

---

## 3. Las peculiaridades de ARM sobre JTAG/SWD

Un servidor GDB para un Cortex-M **no es un traductor de paquetes**. La mitad
del trabajo está por debajo, en el modelo de depuración de ARM, y son
precisamente los detalles que hacen que una sonda escrita a la ligera se cuelgue.
Se han modelado **en los dos lados** —en el DP del MCU y en el maestro de la
sonda—, porque solo así se pueden verificar.

### 3.1 El enganche

Reset de línea (≥ 50 unos), secuencia de conmutación JTAG→SWD (`0xE79E`), otro
reset de línea y lectura del `IDCODE`. Sin eso el objetivo **ni contesta**: el
SWJ-DP arranca en modo JTAG.

### 3.2 El encendido del DAP

Antes de tocar el AP hay que pedir `CDBGPWRUPREQ` y `CSYSPWRUPREQ` en
`CTRL/STAT` y **esperar sus acuses**. El modelo lo respeta: mientras no estén,
**el AP contesta `FAULT`**. Es la primera piedra con la que tropieza cualquiera
que escriba un programador desde cero.

### 3.3 `WAIT` no es un error

Un `ACK` de espera (`010`) quiere decir que el DAP está ocupado con el acceso
anterior y que hay que **reintentar el mismo paquete**. El modelo lo genera de
verdad: un acceso del AP que consume tiempo de bus deja el DAP ocupado, y todo
lo que llegue mientras tanto recibe `WAIT`.

Dónde se nota: **el borrado de un sector de Flash tarda un milisegundo**. Durante
todo ese tiempo el DAP contesta `WAIT`, y la sonda hace decenas de reintentos
antes de que la operación termine. Una sonda que se rindiera al primer `WAIT` no
podría programar nada.

### 3.4 `FAULT` y los bits pegajosos

Un acceso que falla en el bus deja **pegado** `STICKYERR` en `CTRL/STAT`, y a
partir de ahí **todos** los accesos al AP contestan `FAULT`. La única forma de
limpiarlo es escribir en `ABORT`. Hasta que no se hace, el DAP está mudo. El
maestro de la sonda lo trata: ante un `FAULT` lee `CTRL/STAT`, escribe `ABORT` y
reintenta.

### 3.5 La fase de datos no ocurre si el `ACK` no es `OK`

Ésta costó encontrarla, y es un buen ejemplo de por qué modelar el protocolo a
nivel de bit merece la pena. Con un `ACK` distinto de `OK`, **el paquete termina
en el propio `ACK`**: no hay 33 bits de datos. El modelo seguía soltándolos, el
anfitrión ya había dado la transacción por terminada y empezaba a gobernar la
línea, y **el modelo eléctrico de los pads denunciaba la pelea con un aviso de
sobrecorriente en PA13**. El síntoma no fue un dato mal leído: fue una corriente
de pin fuera de rango.

### 3.6 La lectura aplazada del AP

El dato de una lectura del AP llega en la transacción **siguiente**, o en
`RDBUFF`. Leer una vez y creerse el resultado es el error más común al escribir
un programador. La sonda lo hace bien: `leer_ap` para pedir, `leer_dp(RDBUFF)`
para recoger.

### 3.7 La frontera de 1 KiB

El auto-incremento de `TAR` **no la cruza**. La sonda trocea todo bloque en esa
frontera y reescribe `TAR` en cada una; el modelo, por su parte, simplemente
deja de incrementar al llegar, que es lo que hace el silicio.

### 3.8 El PPB no es memoria normal

Parar el núcleo, leerle un registro o poner un punto de ruptura **no son
operaciones de memoria**: son escrituras en `DHCSR`, `DCRSR`/`DCRDR`, `FP_COMPn`
y `DWT_COMPn`, con sus llaves (`0xA05F` en `DHCSR`, `KEY` en `FP_CTRL`) y en su
orden. El stub las conoce; para GDB todo eso es transparente.

---

## 4. El protocolo RSP

### 4.1 Lo que implementa

| Familia | Paquetes |
| :--- | :--- |
| Arranque | `qSupported`, `QStartNoAckMode`, `qAttached`, `qC`, `qfThreadInfo`/`qsThreadInfo`, `qOffsets`, `H`, `vMustReplyEmpty` |
| Descripción | `qXfer:features:read:target.xml` |
| Estado | `?`, con `T05` y la causa (`swbreak`, `watch`) |
| Registros | `g`, `G`, `p`, `P` — 23 registros |
| Memoria | `m`, `M`, `X` (con escapado binario) |
| Ejecución | `c`, `s`, `vCont?`, `vCont;c`, `vCont;s`, Ctrl-C |
| Puntos | `Z0`/`z0`, `Z1`/`z1` (FPB), `Z2`-`Z4`/`z2`-`z4` (DWT) |
| Flash | `vFlashErase`, `vFlashWrite`, `vFlashDone` |
| Varios | `qRcmd` (`monitor reset` / `halt` / `resume`), `R`, `k`, `D` |

### 4.2 `target.xml`: sin él, GDB se equivoca de procesador

Es la pieza que más se olvida. Sin una descripción del objetivo, GDB supone un
**ARM clásico** con sus ocho registros de coma flotante FPA, y el paquete `g` no
cuadra: pide 26 registros donde hay 17. El stub sirve una descripción con
`org.gnu.gdb.arm.m-profile` (r0-r12, sp, lr, pc, xPSR) y
`org.gnu.gdb.arm.m-system` (MSP, PSP, PRIMASK, BASEPRI, FAULTMASK, CONTROL): 23
registros, que es exactamente lo que un Cortex-M puede enseñar por
`DCRSR`/`DCRDR`.

### 4.3 Puntos de ruptura: `Z0` también va al FPB

GDB pide `Z0` para un punto **software** —escribir un `BKPT` en el código— y
`Z1` para uno **hardware**. En una Flash, escribir es carísimo: hay que borrar
un sector entero. El stub, como cualquier servidor serio para Cortex-M, resuelve
**los dos** con comparadores del FPB, y lo anuncia en `qSupported` con
`swbreak+;hwbreak+`. Son seis; cuando se acaban, contesta error y GDB lo
gestiona.

Los watchpoints (`Z2`, `Z3`, `Z4`) van a los cuatro comparadores del DWT, con su
máscara calculada a partir de la longitud que pide GDB.

### 4.4 El aviso de parada

Cuando el objetivo se para **solo** —un punto de ruptura, un watchpoint— el stub
tiene que avisar a GDB sin que nadie se lo pida. Mientras el objetivo corre, el
stub sondea `DHCSR.S_HALT` cada pocos milisegundos simulados y, en cuanto lo ve
parado, manda el `T05` con la causa leída de `SCB_DFSR`.

---

## 5. La descarga a la Flash

Es la prueba de fuego, y donde se ve que el stub es una sonda y no un atajo.

**Un `load` de GDB sobre `0x0800 0000` no puede ser una escritura al bus.** La
Flash de un STM32 no se escribe escribiendo: se programa por su controlador. El
stub hace la secuencia entera **por SWD**, igual que un ST-LINK:

1. **Desbloquear**: `FLASH_KEYR` ← `0x45670123`, luego `0xCDEF89AB`.
2. **Borrar**: por cada sector tocado, `FLASH_CR` ← `SER | SNB | PSIZE`, luego
   `STRT`, y esperar a que `FLASH_SR.BSY` baje. Aquí es donde el DAP contesta
   `WAIT` durante un milisegundo entero.
3. **Programar**: `FLASH_CR` ← `PG | PSIZE=32 bits`, y palabra a palabra por
   `TAR`/`DRW`.
4. **Bloquear**: `FLASH_CR.LOCK`.

El mapa de sectores del F407 (4 de 16 KiB, 1 de 64 KiB, 7 de 128 KiB) lo lleva
el stub, igual que un programador real lleva su propia descripción del
dispositivo.

La prueba lo comprueba de punta a punta: borra el sector 0, verifica que la
Flash queda a unos, programa cuatro palabras de código, y **pone el PC encima y
lo ejecuta**.

---

## 6. Verificación

### 6.1 Dentro de la suite: T96, 37 comprobaciones

El banco hace de GDB con `gdb_client.h` —un cliente RSP de verdad sobre un
socket TCP— y recorre una sesión completa. La única peculiaridad del cliente es
que tiene que **ceder el paso**: el stub es un proceso de SystemC y solo avanza
cuando avanza el tiempo simulado.

Lo que se comprueba:

* el apretón de manos (`qSupported`, `QStartNoAckMode`) y `target.xml`;
* que al conectarse el stub **para** el objetivo y lo dice con `T05`;
* los 23 registros por `g`, y la escritura por `P`;
* memoria: lectura, escritura, y **escritura no alineada de un byte**, que
  obliga al stub a leer-modificar-escribir sobre un AP de 32 bits;
* paso a paso instrucción a instrucción, verificando el registro que cambia;
* un punto de ruptura `Z1`, con el aviso espontáneo cuando el objetivo se para;
* un watchpoint `Z2` y su retirada;
* `monitor halt`;
* y la descarga a Flash completa, terminando por ejecutar lo que se acaba de
  programar.

```
qSupported -> PacketSize=1000;qXfer:features:read+;QStartNoAckMode+;swbreak+;hwbreak+;vContSupported+
? -> T050d:00c00120;0f:02010008;thread:1;
g -> 184 bytes (23 registros)
tras continuar: T05swbreak:;0d:00c00120;0f:06020008;thread:1;
programadas 1 palabras; Flash[0x300] = 0xE7FE205A
```

### 6.2 Fuera de la suite: un proceso externo de verdad

La verificación anterior corre dentro del mismo ejecutable. Para descartar
cualquier duda, el modo `--gdb` se ha probado con un **proceso independiente**
conectándose por TCP:

```
[gdb] escuchando en localhost:3355
[gdb] cliente conectado
qSupported -> PacketSize=1000;qXfer:features:read+;QStartNoAckMode+;swbreak+;h
?          -> T050d:00c00120;0f:02010008;thread:1;
g          -> 23 registros; pc = 02010008  sp = 00c00120
m 8000000  -> 00c0012001010008
monitor rst-> OK
[gdb] cliente desconectado
```

Las dos primeras palabras de la Flash son el vector de reset: MSP `0x2001C000` y
`Reset_Handler` `0x08000101`, leídas por dos hilos desde fuera del proceso.

---

## 7. Cómo usarlo desde un IDE

**Línea de órdenes:**

```
./build/stm32f407vg --gdb --port=3333 mi_firmware.bin
```

En ese modo **no se ejecuta la suite**: el modelo arranca, abre el puerto y se
queda esperando. Si se da una imagen, se precarga en la Flash; si no, será GDB
quien la descargue con `load`.

**Desde `arm-none-eabi-gdb`:**

```
target extended-remote localhost:3333
load
monitor reset halt
break main
continue
```

**Desde Eclipse CDT o STM32CubeIDE:** una configuración *GDB Hardware
Debugging* (o *Debug Configurations → GDB OpenOCD/ST-LINK* con el servidor ya
lanzado a mano), con host `localhost` y puerto `3333`. El stub anuncia
`vContSupported+`, `swbreak+` y `hwbreak+`, que es lo que los plugins esperan
para no caer en modos de compatibilidad.

---

## 8. Limitaciones, dichas claramente

* **No hay TAP JTAG.** La secuencia de conmutación JTAG→SWD se reconoce y se
  ejecuta, pero quien habla es el SWD. Un IDE configurado explícitamente en
  JTAG no se conectará; en SWD, que es lo normal, sí.
* **Un solo AP y un solo núcleo.** `APSEL` distinto de cero no lleva a ninguna
  parte, y el stub anuncia un único hilo.
* **Sin traza en vivo hacia GDB.** El ITM saca la traza por el pin SWO
  (T93/T95), pero el stub no la reenvía por el canal `O` del RSP.
* **La velocidad depende de la simulación.** El stub atiende el socket cada
  100 µs *simulados*; si el modelo va mucho más despacio que el tiempo real,
  GDB puede notar latencia. No es un problema de correción sino de ritmo.
* **`vFlashWrite` programa a 32 bits**, que exige VDD ≥ 2,7 V. Es lo que hace
  cualquier programador cuando la alimentación lo permite.

---

## 9. Lo que salió a la luz al hacerlo

Tres cosas, todas del lado del **modelo**, no del stub, y las tres son fieles al
hardware:

1. **La fase de datos tras un `ACK` que no es `OK`.** El modelo la seguía
   emitiendo y se peleaba con el anfitrión por la línea. Lo delató el modelo
   eléctrico de los pads con un aviso de **sobrecorriente en PA13** — un error
   de protocolo detectado por una corriente de pin, que es exactamente lo que
   pasaría en un laboratorio.
2. **Un acceso largo del AP no puede bloquear la máquina del SWD.** El borrado
   de un sector de Flash consume un milisegundo, y el modelo lo estaba pagando
   *dentro* del hilo que atiende los flancos de `SWCLK`, con lo que perdía
   pulsos y se desincronizaba. La respuesta correcta es la del silicio: el DAP
   **pospone** el acceso y contesta `WAIT` a lo que llegue mientras tanto.
3. **El PPB es privado de verdad.** El maestro de pruebas del banco no lo
   alcanza, así que la prueba del perro guardián de la fase F5 —que gobernaba
   la línea de congelación a mano— pasó a escribir `DBGMCU_APB1_FZ` por el
   AHB-AP. Un cambio pequeño que hace la prueba mucho más honesta.
