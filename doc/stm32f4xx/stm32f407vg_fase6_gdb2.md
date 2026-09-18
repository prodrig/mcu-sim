# Fase F6 (ampliación 2) — El segundo GDB-stub: enganchado al DAP por dentro

Segunda ampliación de la fase F6. La primera
(`doc/stm32f4xx/stm32f407vg_fase6_gdb.md`) añadió un servidor GDB/RSP **soldado a los
pines** `SWCLK`/`SWDIO`, que habla SWD bit a bit como el firmware de un ST-LINK.
Es el modelo fiel, y por serlo es lento: cada palabra leída cuesta cerca de cien
flancos de reloj simulados.

Aquí se añade un **segundo stub**, en el mismo `namespace stm32`, que **reserva
los cinco pines de depuración sin usarlos** —`PA13`/`SWDIO`, `PA14`/`SWCLK`,
`PA15`/`JTDI`, `PB3`/`SWO` y `PB4`/`NJTRST`— y llega al DAP **por llamada de
función**. Los dos hablan exactamente el mismo protocolo y dan exactamente las
mismas respuestas; lo único que cambia es lo que cuesta simularlos.

**Resultado medido (T97):** leer 256 palabras cuesta **6 240 µs** simulados por
los pines y **16 µs** por el DAP. Son **×390**. En tiempo de pared, 0,29 s
frente a 0,001 s. La suite pasa **1385 de 1385 comprobaciones** (23 nuevas en el
grupo T97), sin tocar ninguna de las 1362 anteriores.

---

## 1. La idea: mismo depurador, distinto cable

Lo primero que había que decidir es **qué se comparte**. Un servidor GDB para un
Cortex-M es, en volumen, casi todo protocolo: el tramado `$cuerpo#suma`, los
paquetes `g`/`G`/`p`/`P`/`m`/`M`/`X`/`c`/`s`/`vCont`/`Z`/`z`/`q…`/`v…`, el
`target.xml`, la gestión de los seis comparadores del FPB y los cuatro del DWT,
y la secuencia de programación de la Flash. Nada de eso depende de por dónde se
llegue al objetivo.

Duplicarlo habría sido un error: dos copias que se separan al primer arreglo. Lo
que se hizo fue **extraer el motor** y dejar el transporte como interfaz.

```
                        ┌───────────────────────────────────┐
   GDB / Eclipse ──TCP──►│      common/gdb_rsp.h             │
                        │  socket · tramado · intérprete    │
                        │  registros · FPB/DWT · Flash      │
                        └───────────────┬───────────────────┘
                                        │ 3 funciones virtuales
                          ┌─────────────┴─────────────┐
                          ▼                           ▼
              verif/gdb_stub.h              core/gdb_stub_dap.h
              (la SONDA)                    (el INTERNO)
                    │                             │
              SwdProbe: bits                DebugSys::ap_read32/
              por PA14/PA13                 ap_write32 (TLM)
                    │                             │
                    ▼                             ▼
             pines ─► SW-DP ─► AHB-AP ─► bus   AHB-AP ─► bus
```

El transporte son tres funciones:

```cpp
virtual bool     dap_enganchar() = 0;
virtual bool     dap_leer(uint32_t a, uint32_t& v) = 0;
virtual bool     dap_escribir(uint32_t a, uint32_t v) = 0;
virtual unsigned dap_leer_bloque(uint32_t a, uint32_t* w, unsigned n);  // opcional
```

La sonda las resuelve con `SwdProbe` (reset de línea, conmutación JTAG→SWD,
`IDCODE`, encendido del DAP, `ACK`, paridad, ráfagas con auto-incremento de
`TAR`). El stub interno las resuelve así:

```cpp
bool dap_enganchar() override { return true; }
bool dap_leer(uint32_t a, uint32_t& v) override {
    return dbg_.ap_read32(a, v) == tlm::TLM_OK_RESPONSE;
}
bool dap_escribir(uint32_t a, uint32_t v) override {
    return dbg_.ap_write32(a, v) == tlm::TLM_OK_RESPONSE;
}
```

Eso es **todo** el segundo stub. Que quepa en diez líneas es la prueba de que la
factorización estaba bien hecha.

## 2. Por qué esto no es hacer trampa

La objeción evidente: si el depurador puede llamar a funciones del modelo, ¿qué
está verificando? La respuesta es que **no se salta el modelo de depuración,
solo el cable**. De `ap_read32` hacia dentro, el camino es idéntico al de la
sonda:

| Lo que sigue igual | Lo que desaparece |
| :--- | :--- |
| El AHB-AP entra por `ahb_ap`, que el router del núcleo trata como el S-bus: MPU, remapeo de `0x0`, arbitraje en la matriz | El reset de línea de ≥50 unos |
| Parar el núcleo sigue siendo escribir `DHCSR` con la llave `0xA05F` | La secuencia de conmutación JTAG→SWD (`0xE79E`) |
| Leer un registro sigue siendo la pareja `DCRSR`/`DCRDR` con su `S_REGRDY` | La lectura del `IDCODE` y el `CDBGPWRUPREQ`/`CSYSPWRUPREQ` |
| Los puntos de ruptura siguen siendo comparadores del FPB con su `KEY` | Los `ACK` `OK`/`WAIT`/`FAULT` y los bits pegajosos de `CTRL/STAT` |
| La Flash sigue programándose por `FLASH_KEYR`/`CR`/`SR`, palabra a palabra, esperando `BSY`, con su 1 ms de borrado [IR, §5.5-5.9] | La paridad, el *turnaround* y la frontera de 1 KiB del `TAR` |

Lo que se pierde es **protocolo de transporte**, no comportamiento del MCU. Y
esa es exactamente la línea por la que hay que elegir (§5).

## 3. Los pines: reservados, aunque no se usen

El encargo era explícito: el segundo stub debe **reservar** los pines de
depuración, incluido `SWO`, aunque no los use. Eso se modela así:

- **La función AF0 sigue registrada en el mux** para `PA13`, `PA14`, `PA15`,
  `PB3` y `PB4`. El firmware que quiera usarlos como GPIO tiene que
  reprogramarlos igual que en el silicio, y mientras no lo haga siguen siendo
  del puerto de depuración. Desde fuera del encapsulado se ven en su nivel de
  reposo, con la misma impedancia de siempre.
- **El frente SWD deja de escuchar.** `DebugSys::set_pines_debug(false)` hace
  que `swd_proc` ignore los flancos de `SWCLK` y que `SWDIO` no se conduzca
  nunca: una sonda soldada ahí no engancha, no obtiene `IDCODE` y no consigue
  que el DP conteste un solo paquete.
- **El SWO deja de emitir.** El pin `PB3` se queda en reposo alto y la FIFO del
  TPIU se vacía, para que el ITM no se atasque si el firmware sigue escribiendo
  en los puertos de estímulo.

Es decir: los pines existen, están asignados y no los puede usar nadie más, pero
están **mudos**. El firmware que se depure se comporta igual con un stub que con
el otro —misma tabla de pines, mismo consumo de recursos, mismos registros— que
es la condición para que la elección sea *solo* una cuestión de velocidad.

## 4. Cómo se elige: un parámetro del núcleo

La elección **no es una opción del banco de pruebas**, sino un rasgo del propio
núcleo, con la misma receta que el resto del modelo (`UsartCaps`, `TimCaps`,
`SpiCaps`, `I2cCaps`, `AdcCaps`, `DacCaps`, `SdioCaps`, `CanCaps`): un `struct`
`constexpr` de rasgos, unas instancias con nombre, una clase base **no
plantilla** que los toma **por valor** y un alias de plantilla para fijarlos en
compilación.

```cpp
enum class DebugAttach { Pines, Interno };          // common/gdb_rsp.h

struct DebugCaps {                                  // core/cortex_m4f.h
    DebugAttach attach;     // pines expuestos o reservados
    unsigned    puerto;     // puerto TCP del stub interno (0 = no escuchar)
};
inline constexpr DebugCaps DBG_PINES  { DebugAttach::Pines,   0    };
inline constexpr DebugCaps DBG_INTERNO{ DebugAttach::Interno, 3333 };
```

Y en el constructor del núcleo, la elección entera:

```cpp
explicit CortexM4F(sc_core::sc_module_name nm, DebugCaps c = DBG_PINES) : ... {
    if (caps.attach == DebugAttach::Interno) {
        debug.set_pines_debug(false);                       // reservados, mudos
        if (caps.puerto) gdb = new GdbStubDap("gdb", debug, caps.puerto);
    }
    ...
}
```

Las **tres formas** de usarlo:

```cpp
// 1) En tiempo de ejecución, que es lo que necesita una línea de órdenes:
DebugCaps c = DBG_PINES;
if (quiero_rapido) { c.attach = DebugAttach::Interno; c.puerto = puerto_cli; }
Stm32F407VG dut{"dut", c};

// 2) En tiempo de compilación, con el alias en el estilo de la casa:
CortexM4F_Pines  nucleo_fiel{"core"};      // = CoreT<DBG_PINES>
CortexM4F_GdbDap nucleo_rapido{"core"};    // = CoreT<DBG_INTERNO>

// 3) Por omisión: Stm32F407VG dut{"dut"};  -> pines expuestos, sin stub interno
```

El `Stm32F407VG` lo reenvía tal cual, de modo que el modo se elige en **una sola
línea** del banco. Desde la línea de órdenes:

```
./build/test407 --gdb     [--port=3333] [imagen.bin]   # sonda por los pines
./build/test407 --gdb-dap [--port=3333] [imagen.bin]   # stub interno al DAP
```

En modo `--gdb-dap` la sonda del banco se construye con puerto 0 —existe pero no
escucha—, porque dos servidores no comparten un puerto TCP.

## 5. **Cómo elegir entre los dos stubs**

Esta es la sección que importa. La regla corta:

> **Usa el stub interno (`--gdb-dap`) para depurar firmware.
> Usa la sonda (`--gdb`) para depurar el propio puerto de depuración.**

Desarrollada:

| Situación | Stub | Por qué |
| :--- | :--- | :--- |
| Desarrollo diario de firmware: descargar, poner puntos de ruptura, ir paso a paso, mirar variables | **Interno** | Es lo mismo, pero entre ×3 (descarga) y ×390 (lectura de memoria) más barato. No hay ninguna razón para pagar los flancos |
| Sesión larga desde Eclipse CDT o STM32CubeIDE, con ventanas de memoria y de variables que se refrescan solas | **Interno** | Cada refresco de una ventana de memoria son cientos de palabras. Por los pines, el IDE se arrastra |
| Verificar que un ST-LINK, un J-Link o un OpenOCD reales se entienden con el modelo | **Sonda** | Es el único que ejercita el SW-DP: reset de línea, conmutación, `IDCODE`, `ACK`, paridad, *turnaround* |
| Verificar el propio SW-DP: `WAIT`, `FAULT`, bits pegajosos, `ABORT`, frontera de 1 KiB, lectura aplazada | **Sonda** | El stub interno entra por debajo de todo eso |
| Comprobar que el firmware libera o conserva correctamente `PA13`/`PA14`/`PB3` | **Sonda** | Hace falta que alguien mire los pines de verdad |
| Trazas por SWO (`ITM`), analizador de traza colgado de `PB3` | **Sonda** | Con los pines reservados, el SWO no emite |
| Depurar firmware que además usa `PA15`, `PB3` o `PB4` como GPIO | Cualquiera | Los dos reservan los pines igual; el firmware los libera igual |
| Regresión automática que arranca el modelo, descarga y ejecuta sin intervención | **Interno** | Menos tiempo simulado y menos tiempo de pared, sin perder nada que la regresión mire |
| Medir cuánto tarda una descarga *real* con un programador *real* | **Sonda** | El tiempo del SWD es parte de la respuesta |

Y el matiz que conviene no ignorar: **el stub interno no acelera el MCU**. Si el
modelo está corriendo libre y lo que tarda es simular el propio STM32, cambiar
de stub no cambia eso. Lo que el stub interno abarata es **el coste del
depurador**, que es lo que domina en cuanto GDB empieza a hablar mucho.

## 6. Ficheros

| Fichero | Contenido |
| :--- | :--- |
| `src/common/gdb_rsp.h` | **Nuevo.** El motor del RSP, sin transporte: socket, tramado, intérprete, mapa de 23 registros, `target.xml`, FPB/DWT y programador de Flash. Las tres funciones de transporte son virtuales puras |
| `src/core/gdb_stub_dap.h` | **Nuevo.** El segundo stub: el mismo motor con el transporte resuelto por `DebugSys::ap_read32`/`ap_write32` |
| `src/verif/gdb_stub.h` | Reescrito sobre el motor común: de 750 líneas a 100, todas de transporte SWD. El comportamiento visto por GDB no cambia (lo garantiza T96, intacto) |
| `src/core/cortex_m4f.h` | `DebugCaps`, `DBG_PINES`/`DBG_INTERNO`, el constructor con rasgos, los alias `CortexM4F_Pines`/`CortexM4F_GdbDap` y la creación condicional del stub interno |
| `src/core/debug.h` | `set_pines_debug()`: los pines quedan reservados pero mudos (el frente SWD no escucha, el SWO no emite) |
| `src/top/stm32f407vg.h` | El top reenvía los rasgos al núcleo |
| `src/top/sc_main.cpp` | Grupo T97 (23 comprobaciones) y el modo `--gdb-dap` |

## 7. Verificación

### 7.1 Dentro de la suite: T97, 23 comprobaciones

El banco instancia **la misma clase** que crearía el núcleo en modo interno
(`GdbStubDap`, apuntando a `dut->core.debug`) en el puerto 3334, y compara los
dos caminos punto por punto. El núcleo de la suite sigue en modo `Pines`, de
modo que T93 (SWO), T94 (sonda) y T96 (stub de pines) no se enteran de nada.

1. **El núcleo de la suite expone los pines** y por eso *no* crea ningún stub
   interno (`dut->core.gdb == nullptr`).
2. **Son intercambiables.** La misma sesión —`qSupported`, `?`, `P0f=`, `P00=`,
   `g`, `m8000000,10`, `D`— por los dos stubs, comparando las respuestas *como
   cadenas*: mismas capacidades, mismo banco de 23 registros, mismo contenido de
   memoria.
3. **Los pines se reservan de verdad.** Con `set_pines_debug(false)`, la sonda
   soldada a `PA13`/`PA14` **no** obtiene el `IDCODE` y el contador
   `swd_packets()` del `DebugSys` no avanza ni una unidad. El stub interno, en
   cambio, sigue leyendo la Flash por el DAP. Al volver a exponerlos, la sonda
   recupera el `IDCODE 0x2BA01477` a la primera.
4. **La ganancia, medida.** 256 palabras por cada camino, mismo dato al final.
5. **La descarga, medida.** Borrado de sector + 512 B programados por cada
   camino, comprobando el contenido resultante en la Flash.

Lo que imprime:

```
    stub de pines en :3333, stub del DAP en :3334
    256 palabras por los PINES: 6240 us simulados, 0.293 s de pared
    256 palabras por el DAP   :   16 us simulados, 0.001 s de pared
    ganancia: x390 en tiempo simulado
    descarga de 512 B por los PINES: 10400 us simulados
    descarga de 512 B por el DAP   :  3600 us simulados
```

### 7.2 Las dos cifras, y por qué son distintas

| Trabajo | Pines | DAP | Ganancia |
| :--- | ---: | ---: | ---: |
| Leer una palabra | 24,4 µs | 62,5 ns | **×390** |
| Leer 256 palabras (una ventana de memoria del IDE) | 6 240 µs | 16 µs | **×390** |
| Borrar un sector y programar 512 B | 10 400 µs | 3 600 µs | **×2,9** |

La descarga gana mucho menos, y es **correcto que así sea**: el borrado de un
sector cuesta 1 ms y cada palabra programada cuesta 16 µs *dentro del
controlador de Flash* [IR, §5.9]. Eso es comportamiento del MCU, y ninguno de
los dos stubs se lo salta. Un stub que "descargara instantáneamente" estaría
mintiendo sobre el dispositivo, no acelerando la simulación. Por eso T97
comprueba explícitamente que la descarga por el DAP **sigue costando más de
1 ms**: la ganancia tiene un techo, y el techo es el silicio.

### 7.3 Fuera de la suite: un proceso externo

Con el modelo levantado en modo servidor por el camino interno,

```
./build/test407 --gdb-dap --port=3401 verif/fw/test_isa.bin
```

un cliente RSP externo (proceso aparte, sin nada que ver con SystemC) obtiene:

```
qSupported: PacketSize=1000;qXfer:features:read+;QStartNoAckMode+;swbreak+;hwbreak+…
?         : T050d:00c00120;0f:02010008;thread:1;
m8000000  : 00c0012001010008dd010008dd010008
50 lecturas de 256 B en 2.200 s de pared
```

—la misma sesión que da `--gdb`, con el `SP` inicial y el vector de reset
correctos de la imagen cargada. La única diferencia visible en el arranque es la
línea del banner:

```
  Enganche: DAP interno (pines reservados, rapido)
```

## 8. Limitaciones, dichas claramente

- **El stub interno no verifica el SWD.** Es su razón de ser, pero conviene
  decirlo: una regresión que solo use `--gdb-dap` dejaría de ejercitar el SW-DP
  por completo. Por eso T94 y T96 siguen ahí y siguen usando los pines.
- **Con los pines reservados no hay traza SWO.** Si se quiere ITM por `PB3`,
  hay que estar en modo `Pines`. (El ITM sigue funcionando; lo que no hay es
  quién lo saque por el pin.)
- **No modela la latencia del DAP interno.** Un acceso por el AHB-AP cuesta lo
  que cueste la transacción en el bus, sin añadir el tiempo del CoreSight. Para
  un depurador eso es correcto —lo que se mide en el bus es real— pero no
  serviría para estimar el ancho de banda de un AP real.
- **Un solo cliente a la vez por stub**, como en la primera ampliación.
- **Los dos stubs no pueden escuchar en el mismo puerto**, y por eso en modo
  `--gdb-dap` la sonda del banco se construye sin puerto.

## 9. Lo que salió a la luz al hacerlo

- **La factorización pagó sola.** Extraer el motor a `common/gdb_rsp.h` dejó el
  stub de los pines en 100 líneas y el nuevo en 10. Que T96 —37 comprobaciones
  de una sesión completa de GDB— pasara sin tocar una sola línea después de
  mover 650 líneas de sitio fue la mejor prueba de que la extracción era
  correcta.
- **"Reservar" un pin es un estado, no una ausencia.** La primera tentación fue
  dejar los pines sin conectar en modo interno. Habría sido un error: en el
  silicio los pines *existen* y *están asignados* aunque nadie los use, y el
  firmware tiene que liberarlos explícitamente. Modelarlo como "el mux los sigue
  reservando, el frente SWD no escucha" es lo que hace que el firmware se
  comporte igual en los dos modos.
- **La ganancia no es una sola cifra.** Fue tentador quedarse con el ×390 del
  titular. Medir también la descarga —×2,9— es más honesto y más útil: dice al
  usuario que el segundo stub acelera *el depurador*, no *el dispositivo*.
- **Comparar cadenas es la mejor prueba de equivalencia.** En vez de comprobar
  que los dos stubs "funcionan", T97 compara byte a byte lo que contestan al
  mismo paquete. Si algún día divergen —porque alguien toque solo uno de los
  dos—, la prueba lo dirá inmediatamente y con el diagnóstico ya hecho.

---

**Estado:** fase F6 cerrada con sus dos ampliaciones. **1385/1385
comprobaciones**, 0 fallos, compilación limpia con `-Wall -Wextra -O2`. El
modelo tiene dos servidores GDB/RSP intercambiables: uno fiel al cable y otro
rápido, y una sola línea decide cuál.
