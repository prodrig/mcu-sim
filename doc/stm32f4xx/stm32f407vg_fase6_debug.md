# Fase F6 — Sistema de depuración: DAP, FPB, DWT, ITM, TPIU y DBGMCU

Informe de implementación de la **fase F6** del plan `doc/stm32f4xx/smt32f407vg_diseño.md`.
Continúa a las fases F1-F5, que quedaron cerradas con el bxCAN
(`doc/stm32f4xx/stm32f407vg_fase5_can.md`).
Fuentes: `doc/refs/stm32f407xx/informe_revisado.md` [IR, cap. 13] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance:** el subsistema de depuración del STM32F407VG **completo y hasta los
pines**: el Debug Access Port con el protocolo SWD a nivel de bit sobre
PA13/PA14, el AHB-AP, los registros de Core Debug, la unidad FPB, la unidad
DWT, el ITM con su salida de traza por el pin SWO (PB3), la ROM table de
auto-descubrimiento y el bloque DBGMCU de ST.

**Resultado:** el subsistema está completo y verificado. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **1325 de 1325 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 675 de F5 + **91 nuevas de F6**,
código de salida 0, en unos 6 s). **Con esto queda cerrada la fase F6.**

---

## 1. Lo que hace distinta a esta fase

Todas las fases anteriores modelaban periféricos: bloques que cuelgan de un bus,
tienen registros y hacen algo con unos pines. **La depuración no es eso.** Es
una infraestructura **paralela al flujo de ejecución** [IR, §13-Implicaciones]:

* no aparece en el mapa de memoria del sistema —vive en el PPB, que es privado—;
* **el DMA no puede tocarla**, y ningún maestro que no sea el núcleo o el DAP;
* y, sobre todo, **puede parar el núcleo entre dos instrucciones**, cambiarle
  los registros y leer toda la memoria mientras está parado.

Eso obliga a una decisión de arquitectura que condiciona todo lo demás: **la CPU
tiene que llamar al subsistema de depuración**, no al revés. El FPB mira cada
búsqueda de instrucción, el DWT mira cada acceso a datos, el Core Debug decide
si el hilo de ejecución sigue o se suspende.

### 1.1 El contrato: `core_debug_if`

Se ha resuelto igual que la CPU habla con el SCS —por una interfaz abstracta—,
en un fichero propio (`src/core/debug_if.h`) para que `cpu.h` no tenga que
conocer `debug.h`: son dos módulos que se incluyen en el orden contrario.

```cpp
class core_debug_if {
    virtual bool dbg_halt_now() const = 0;   // ¿parado ahora mismo?
    virtual bool dbg_enabled() const = 0;    // DHCSR.C_DEBUGEN
    virtual void dbg_request_halt(uint32_t dfsr_bits) = 0;
    virtual bool dbg_take_step() = 0;        // DHCSR.C_STEP
    virtual sc_core::sc_event& dbg_wake() = 0;
    virtual int  dbg_fetch(uint32_t addr, uint32_t& hw, uint32_t& alt) = 0;  // FPB
    virtual void dbg_data(uint32_t a, unsigned n, bool wr, uint32_t v) = 0;  // DWT
    virtual void dbg_cycles(unsigned cycles, unsigned lsu_extra) = 0;
    virtual void dbg_exception(int excp, bool entry) = 0;
    virtual bool dbg_vector_catch(int excp) = 0;
    virtual void dbg_reset() = 0;
};
```

**Con el puntero a nulo el núcleo se comporta exactamente como antes de esta
fase**: no hay ni un `if` de más en el camino caliente que no sea la
comprobación del propio puntero. Es lo que permitió que las 1234 comprobaciones
de F1-F5 siguieran pasando sin tocar una sola de ellas.

---

## 2. El Debug Access Port, hasta los pines

### 2.1 Los pines están vivos desde el reset

Es la característica que hace útil a una sonda, y el modelo la respeta: PA13,
PA14 y PA15 arrancan en **AF0**, con pull-up en SWDIO y pull-down en SWCLK
[IR, §13.1]. La prueba lo comprueba leyendo los registros de reset del GPIO:

```
tras el reset, GPIOA_MODER = 0xA8000000, PUPDR = 0x64000000
```

Consecuencia práctica: **una sonda puede rescatar un chip cuyo firmware no
arranca**, porque no hace falta que ningún programa configure nada.

### 2.2 El protocolo SWD, bit a bit

No hay atajo. El SW-DP es una máquina de estados sensible a los flancos de
SWCLK que implementa el protocolo entero:

| Fase | Ciclos | Quién gobierna |
| :--- | :---: | :--- |
| Reset de línea | ≥ 50 unos | anfitrión |
| Conmutación JTAG→SWD | 16 bits: `0xE79E` | anfitrión |
| Paquete de petición | 8: arranque, APnDP, RnW, A[2:3], paridad, parada, park | anfitrión |
| **Turnaround** | 1 | **nadie** |
| ACK | 3 (`001` = OK) | objetivo |
| Datos + paridad | 33 | objetivo (lectura) / anfitrión (escritura, tras otro turnaround) |
| Reposo | ≥ 1 cero | anfitrión |

El anfitrión gobierna SWDIO en el flanco de **bajada** y el objetivo la muestrea
en el de **subida**; cuando contesta, los papeles se invierten. **SWDIO es
bidireccional de verdad**: la sonda del banco suelta el nodo analógico —alta
impedancia— durante el turnaround y mientras habla el objetivo. Si los dos
gobernasen a la vez, el modelo de pads denunciaría la pelea.

La sonda del banco (`SwdProbe` en `verif/ext_parts.h`) es el otro extremo del
cable de un ST-LINK: dos hilos soldados a PA14 y PA13, y encima de ellos las
operaciones que usa cualquier herramienta —`halt`, `resume`, `step`,
`mem_read32`, `leer_reg`—.

### 2.3 El AHB-AP

`CSW`/`TAR`/`DRW`/`IDR` sobre el router del núcleo: el depurador **ve el mismo
mapa que la CPU**, PPB incluido [IR, §13.2]. Dos detalles que son del hardware y
que están modelados porque una sonda real depende de ellos:

* **la lectura del AP llega con un ciclo de retraso**: la que se pide ahora se
  recoge en la siguiente, o en `RDBUFF`;
* **`CSW.AddrInc`** auto-incrementa `TAR`, que es lo que permite volcar un
  bloque de memoria sin reescribir la dirección en cada palabra.

---

## 3. Core Debug: parar, andar y mirar

### 3.1 La llave

`DHCSR` no se deja escribir sin `0xA05F` en la parte alta [IR, §13.4.1]. No es
un capricho: es lo que impide que un firmware descarrilado se pare a sí mismo.
La prueba lo verifica escribiendo sin llave y comprobando que **no pasa
absolutamente nada**.

### 3.2 Parar saca al núcleo del sueño

Éste fue el hallazgo que más costó, y es fiel al hardware. El bucle de
ejecución tenía un estado de sueño (`WFI`) y otro de parada. Al parar un núcleo
que estaba dormido y reanudarlo, **volvía al sueño**, de modo que el depurador
no podía arrancar nada tras enganchar la sonda sobre un `WFI` — que es
exactamente la situación más común. Un núcleo detenido **no está dormido**: la
parada lo saca del sueño, y así está ahora.

### 3.3 Paso a paso

`C_STEP` con `C_HALT` a cero ejecuta **una instrucción** y vuelve a parar. Se
implementa donde tiene que estar: dentro del propio bucle de ejecución, con el
núcleo formalmente detenido. La prueba avanza cinco instrucciones de un
programita cargado a mano y comprueba que `r0` va tomando 0, 1, 3, 7 y 15, y que
el PC avanza dos bytes por instrucción de 16 bits.

### 3.4 Los registros del núcleo

`DCRSR`/`DCRDR` dan acceso a R0-R12, SP, LR, la **dirección de retorno de
depuración** (el PC que se ejecutará al reanudar), xPSR, MSP, PSP y el grupo
CONTROL/FAULTMASK/BASEPRI/PRIMASK. Y no solo de lectura: el depurador **escribe**
los registros, que es lo que permite cargar un programa en RAM y apuntar el PC
encima de él. Todas las pruebas del FPB y del DWT lo usan así.

### 3.5 La causa de la parada

Va a `SCB_DFSR`, con un bit por causa: `HALTED`, `BKPT`, `DWTTRAP`, `VCATCH` y
`EXTERNAL`. Cada mecanismo deposita el suyo, y la prueba lo comprueba en los
tres casos que sabe provocar.

---

## 4. FPB: puntos de ruptura y parcheo de la Flash

La unidad es **un filtro sobre la búsqueda de instrucción**, y así está
enganchada: en `fetch_hw`, **antes de la memoria**. Si un comparador casa, la
transacción original hacia la Flash **no llega a ocurrir** [IR,
§13-Implicaciones].

Seis comparadores de instrucción y dos literales, con los dos modos:

* **Punto de ruptura** (`REPLACE` = 01/10/11): el FPB **inyecta el opcode
  `BKPT` (0xBE00)** en lugar de la instrucción. El núcleo lo ejecuta y, con
  `C_DEBUGEN` puesto, se para **en la propia instrucción** —la dirección de
  retorno es la del `BKPT`, no la siguiente—. La prueba pone un punto en la
  media palabra alta de una palabra y comprueba que el núcleo se detiene ahí,
  con el registro en el valor exacto de las dos instrucciones anteriores:

```
el FPB para en pc = 0x08000206 con r0 = 3 (DFSR = 0x00000002)
```

* **Parcheo** (`REPLACE` = 00): la instrucción se sirve desde una tabla en SRAM.
  Es para lo que nació la unidad —corregir un error en una Flash ya grabada— y
  la prueba lo demuestra sustituyendo `adds r0,#2 ; adds r0,#4` por
  `adds r0,#32 ; adds r0,#64` sin tocar un byte de la Flash:

```
con el parche activo, r0 = 105 (sin parche seria 15)
```

Y la llave: `FP_CTRL.ENABLE` solo cambia si se escribe `KEY` a la vez.

**Una decisión de modelado, anotada.** `FP_REMAP` documenta `REMAP` en los bits
[28:5] y `RMPSPT` en el 29. Con ese encaje, el campo **no alcanza la SRAM**, que
empieza justo en 2^29 — y sin embargo el propio [IR, §13.7.1] dice que el
destino del remapeado está en SRAM. El modelo guarda la dirección **entera**
alineada a 32 bytes, que es el campo del silicio más el bit de región, y lo deja
dicho en el comentario en vez de fingir que no hay contradicción.

---

## 5. DWT: contar y vigilar

### 5.1 `CYCCNT` cuenta ciclos retirados, no tiempo

Se alimenta de la **contabilidad de ciclos del propio modelo** —la misma que
anota cada instrucción— y no de una estimación aparte. La consecuencia se
comprueba explícitamente: **con el núcleo parado, `CYCCNT` se para también**.
Un contador derivado del tiempo simulado seguiría corriendo y mentiría en cuanto
el depurador tocase el sistema.

Y no cuenta si no está `DEMCR.TRCENA`: ese bit es el interruptor general del
DWT y del ITM [IR, §13.4.3], y la prueba lo verifica en los dos sentidos.

Los cinco contadores de perfil (`CPICNT`, `EXCCNT`, `SLEEPCNT`, `LSUCNT`,
`FOLDCNT`) salen de la misma fuente: los ciclos de más de cada instrucción,
separando los que son de acceso a memoria.

### 5.2 Watchpoints

Cuatro comparadores con su máscara y su función (4: lectura/escritura, 5:
lectura, 6: escritura). Se enganchan en `mem_read`/`mem_write`, **después** del
acceso: un watchpoint **no impide** la operación, la observa. La prueba lo dice
literalmente comprobando que el dato vigilado sí llegó a memoria, y que vigilando
solo lecturas la misma escritura no dispara nada.

---

## 6. ITM y TPIU: la traza que sale por un pin

### 6.1 El candado

El ITM arranca **cerrado**: hasta que no se escribe `0xC5ACCE55` en `ITM_LAR`
no acepta configuración. Es el candado de CoreSight y es lo primero que hace
cualquier driver de traza; la prueba comprueba las dos caras.

### 6.2 El empaquetado

Un paquete de fuente software lleva una cabecera con el **número de puerto y el
tamaño**, y detrás la carga con el byte menos significativo por delante. El
modelo lo genera literalmente así, de modo que **el tamaño del acceso cambia el
paquete**: escribir un byte, una media palabra o una palabra en un puerto de
estímulo produce tramas distintas. `ITM_TER` filtra puerto a puerto.

### 6.3 El pin

En modo NRZ (`TPIU_SPPR` = 10) el pin SWO es **literalmente una línea serie
asíncrona**: bit de arranque, ocho de dato con el menos significativo por
delante y bit de parada, a f_SWO = HCLK/(`ACPR`+1). El analizador del banco
(`SwoReceiver`, colgado de PB3) es por tanto un receptor de UART con el
desempaquetado del ITM encima — que es exactamente lo que hay dentro de una
herramienta de traza real.

```
HCLK = 16000000 Hz, ACPR = 15 -> f_SWO = 1000000 bit/s
por SWO llegaron 10 bytes, 5 mensajes: "F6 OK"
```

---

## 7. ROM table y DBGMCU

La **ROM table** es lo primero que lee una herramienta al conectarse: la lista de
componentes presentes, cada uno con su desplazamiento y su bit de presencia
[IR, §13.3]. Está completa, con las seis entradas y el cero final, y cada bloque
de 4 KiB tiene sus registros de identificación de CoreSight.

El **DBGMCU** es el bloque propio de ST, y hace dos cosas:

* **`IDCODE` = 0x1001 6413**: dice que este chip es un STM32F405/407 revisión
  1001. Es lo que usa una herramienta para saber a qué se ha conectado.
* **Las líneas de congelación.** Cuando el núcleo está parado y el bit
  correspondiente de `DBGMCU_APB1_FZ`/`APB2_FZ` está puesto, el temporizador o
  el perro guardián **deja de contar**. Sin eso, un `timeout` saltaría
  artificialmente en mitad de una sesión de depuración — y con perro guardián
  de por medio, el chip se reiniciaría solo cada vez que se para en un punto de
  ruptura.

Esa segunda función ya se usaba en la fase F5, pero con el banco de pruebas
gobernando la señal a mano. **Ahora se hace como en la placa**: la prueba del
WWDG escribe `DBGMCU_APB1_FZ` por el AHB-AP y para el núcleo, y la línea la
genera el propio DBGMCU.

---

## 8. Verificación

Siete grupos, **91 comprobaciones**.

### 8.1 T89 — El PPB, la ROM table y los bancos

Que el PPB **es privado** (el maestro de pruebas del banco no llega, el AHB-AP
sí), la ROM table entera, el `IDCODE`, los valores de reset de FPB, DWT y TPIU,
la llave de `DHCSR` y el candado del ITM.

### 8.2 T90 — Parar, andar y mirar

Parada, comprobación de que parado está parado, lectura y **escritura** de los
registros del núcleo, cinco pasos con verificación instrucción a instrucción,
reanudación y la causa en `DFSR`.

### 8.3 T91 — FPB

Punto de ruptura en una media palabra concreta, retirada del comparador,
deshabilitación de la unidad entera, y el parcheo desde SRAM.

### 8.4 T92 — DWT

`CYCCNT` con y sin `TRCENA`, su parada con el núcleo, los contadores de perfil,
un watchpoint de escritura con su `MATCHED` y su `DFSR`, y la comprobación de
que vigilando lecturas no salta con una escritura.

### 8.5 T93 — ITM y SWO

Un mensaje de texto por el puerto 0 recuperado **desde el pin**, el efecto del
tamaño del acceso, el filtrado por `ITM_TER` y el apagado global por `TRCENA`.

### 8.6 T94 — La sonda, por los pines

La sesión completa de una herramienta real: pines en AF0 desde el reset,
secuencia de enganche (reset de línea + `0xE79E` + reset), `IDCODE` del SW-DP,
identificación del AHB-AP, lectura y escritura de memoria, volcado de un bloque
con auto-incremento, y **parada del núcleo, lectura y escritura de sus
registros, y reanudación, todo por dos hilos**:

```
la sonda lee IDCODE = 0x2BA01477 en 3 paquetes
AHB-AP: IDR = 0x24770011, BASE = 0xE00FF003
la sonda ve pc = 0x0800020A
en toda la sesion: 56 paquetes con ACK OK, 0 con fallo
```

### 8.7 T95 — Firmware real con CMSIS

`verif/fw/debug_demo/` es el uso cotidiano del subsistema, escrito contra la
cabecera de ST y **ejecutado por el Cortex-M4 del modelo**: `printf` por SWO con
`ITM_SendChar`, medida de un bucle con `DWT_CYCCNT`, y detección del depurador
leyendo `DHCSR.C_DEBUGEN`.

```
etapa = 4 | HCLK = 168000000 | SWO = 2000000 bit/s | depurador visto = 1
1000 vueltas medidas en 10005 ciclos (10.01 ciclos por vuelta)
por SWO llegaron 66 bytes: "STM32F407 F6 listo
 ciclos=10005
"
```

---

## 9. Tres hallazgos

1. **Parar no es dormir.** Un núcleo detenido sobre un `WFI` volvía al sueño al
   reanudarse. Es el caso más común al enganchar una sonda, y hasta que no se
   arregló el depurador no podía arrancar nada.
2. **El pull-up del turnaround.** Al soltar los dos extremos la línea SWDIO
   queda alta por el pull-up del pad, y ese uno se tomaba por el bit de arranque
   del paquete siguiente, desincronizando toda la sesión. La solución es la del
   protocolo: **ciclos de reposo entre paquetes**, y un objetivo que exige ver
   la línea baja antes de admitir un arranque.
3. **El nivel indefinido del primer bit.** El analizador de SWO tomaba el nivel
   del nodo antes de que nadie lo gobernase por un bit de arranque y perdía el
   primer carácter. Se arregla exigiendo ver la línea **en reposo** antes de
   buscar el flanco — que es lo que hace cualquier receptor asíncrono de verdad.

---

## 10. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/core/debug_if.h` | **nuevo** | El contrato por el que la CPU llama al depurador |
| `src/core/debug.h` | **reescrito** | El subsistema CoreSight completo (≈940 líneas; antes eran los esqueletos de F1) |
| `src/core/cpu.h` | ampliado | Enganches del FPB en la búsqueda, del DWT en los accesos, parada/paso a paso y captura de vectores |
| `src/core/cpu_exec16.h` | ampliado | `BKPT` para el núcleo con `C_DEBUGEN` en vez de escalar a HardFault |
| `src/core/scs.h` | ampliado | `set_dfsr` en la interfaz del SCS |
| `src/core/cortex_m4f.h` | ampliado | Enlace CPU ↔ depurador y la frecuencia de HCLK |
| `src/verif/ext_parts.h` | ampliado | `SwdProbe` (la sonda) y `SwoReceiver` (el analizador de traza) |
| `src/top/sc_main.cpp` | ampliado | Grupos T89-T95 (91 comprobaciones) y la prueba del WWDG pasada al DBGMCU real |
| `src/verif/fw/debug_demo/` | **nuevo** | Firmware con CMSIS (`main.c`, `Makefile`) |
| `src/README.md` | actualizado | **Fase F6 marcada como completada** |

---

## 11. Trabajo pendiente

Del subsistema quedan fuera, anotados en el código, tres caminos que ninguna
herramienta corriente necesita en un F407:

* el **TAP JTAG** propiamente dicho. La secuencia de conmutación JTAG→SWD se
  reconoce y se ejecuta, pero quien habla es el SWD, que es lo que usa cualquier
  sonda moderna; un TAP completo (IR/DR, `BYPASS`, `IDCODE`, `ABORT`) exigiría
  además el encadenamiento con el TAP de contorno de ST;
* el **ETM**. Aparece en la ROM table con su identificación, como en el
  silicio, pero no genera traza de instrucciones: en el LQFP100 los pines de
  traza paralela ni siquiera están todos disponibles;
* el **modo Manchester** del TPIU y el **formateador CoreSight** de mezcla
  ITM/ETM. Se selecciona `SPPR` y se guarda `FFCR`, pero la serialización por
  SWO se hace en NRZ, que es el modo que usa todo el mundo y el único que
  decodifica una herramienta corriente.

Del plan queda la **fase F7**: bajo consumo (Stop/Standby completos),
OTG_FS/OTG_HS, Ethernet, FSMC, DCMI y el afinado a nivel de ciclo.
