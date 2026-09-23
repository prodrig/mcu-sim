# Fase F2 — Núcleo Cortex-M4F: ISA completa, excepciones, NVIC y SysTick

Informe de implementación de la fase **F2** del plan `doc/stm32f4xx/smt32f407vg_diseño.md`
(§7). Continúa a `doc/stm32f4xx/smt32f407vg_fase0.md` (esqueleto y contrato de
integración) y a `doc/stm32f4xx/stm32f407vg_fase1.md` (infraestructura: relojes, reset,
matriz AHB, memorias). Fuentes: `doc/refs/stm32f407xx/informe_revisado.md` [IR] y
`doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance según el plan:** *"CPU: fetch/decode/execute ISA entera [II] +
excepciones + NVIC/SysTick"*.
**Criterio de salida:** *"`valida_instrucciones.py` como generador de tests de
decodificador; CoreMark sin periféricos"*.

**Resultado:** criterio cumplido. El modelo compila sin avisos con
`-Wall -Wextra -O2` y la suite de verificación pasa **136 de 136
comprobaciones** (124 heredadas de F1 + 12 nuevas de F2, código de salida 0).
Las 254 codificaciones de `doc/stm32f4xx/valida_instrucciones.py` se reconocen y consumen
el número de bytes correcto; un firmware bare-metal propio ejecuta 103
autocomprobaciones sobre el modelo sin discrepancias; y **CoreMark 1.0 (EEMBC)
compilado con `arm-none-eabi-gcc -O2 -mfpu=fpv4-sp-d16 -mfloat-abi=hard` se
ejecuta sobre el modelo y valida sus cuatro CRC contra los valores canónicos
del *2K performance run***. Verificado con SystemC 2.3.4 / g++ 13.3 / C++17 y
arm-none-eabi-gcc 13.2.1.

---

## 1. Resumen ejecutivo

| Bloque | Estado tras F2 |
| :--- | :--- |
| Banco de registros ARMv7E-M | Completo: R0-R12, MSP/PSP, LR, PC, xPSR (APSR+IPSR+EPSR), PRIMASK, FAULTMASK, BASEPRI, CONTROL, S0-S31, FPSCR |
| Thumb de 16 bits [II, §1] | Completo, incluidos bloques IT, `CBZ/CBNZ`, `SVC`, `BKPT`, hints |
| Thumb-2 de 32 bits [II, §2-§4] | Completo: LDM/STM, exclusivos, dual, `TBB/TBH`, DP con inmediato y con registro desplazado, inmediato plano, saltos y `MSR/MRS`, carga/almacenamiento simple, DP de registro, DSP/SIMD, multiplicación, MAC largo y división |
| FPv4-SP [II, §5] | Completo: aritmética, `VMLA/VMLS/VFMA/VFNMA`, comparaciones, conversiones, transferencias núcleo↔FP (32 y 64 bits), `VLDR/VSTR/VLDM/VSTM/VPUSH/VPOP`, FPSCR con excepciones acumulativas y flush-to-zero |
| Excepciones [IR, §9] | Completas: trama básica y extendida, `STKALIGN`, *lazy FP stacking*, EXC_RETURN, retorno con validación, encadenamiento (*tail-chaining*), escalado a HardFault y LOCKUP |
| NVIC | 82 IRQ con estados inactive/pending/active, ISER/ICER/ISPR/ICPR/IABR/IPR/STIR, prioridades de 4 bits en [7:4], agrupación PRIGROUP |
| SCB | CPUID, ICSR, VTOR, AIRCR, SCR, CCR, SHPR1-3, SHCSR, CFSR, HFSR, DFSR, MMFAR, BFAR, AFSR, CPACR, FPCCR/FPCAR/FPDSCR |
| SysTick | Contador descendente de 24 bits, COUNTFLAG, selección FCLK / HCLK/8, dirigido por eventos |
| MPU | 8 regiones, comprobación por acceso (XN, AP, SRD, tamaño, solapamiento por prioridad) |
| Modos de bajo consumo del núcleo | `WFI`/`WFE`/`SEV`, registro de eventos, salidas `sleeping`/`sleepdeep` hacia RCC/PWR |
| Depuración | Parada/arranque por el AHB-AP, acceso al banco de registros vía `DebugSys::cpu_reg`, sonda del decodificador |
| Verificación | 3 grupos nuevos (T15 decodificador, T16 firmware, T17 CoreMark) y dos firmwares reales compilados en el propio repositorio |

Código nuevo o reescrito en esta fase: **≈4 700 líneas** de modelo y
verificación (`core/`, `verif/`, `top/sc_main.cpp`), más el *port* bare-metal de
CoreMark.

---

## 2. Ficheros de la fase

### 2.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `src/core/cpu_state.h` | Banco de registros y utilidades arquitectónicas: ITSTATE, `AddWithCarry`, `Shift_C`, `DecodeImmShift`, `ThumbExpandImm_C`, saturaciones con Q, extensión de signo, `CLZ`, `BitCount`, `REV/REV16/REVSH/RBIT`, `ConditionPassed`, numeración de excepciones y constantes EXC_RETURN |
| `src/core/cpu_exec16.h` | Ejecución de todo el Thumb de 16 bits [II, §1.1-§1.8] |
| `src/core/cpu_exec32.h` | Ejecución de todo el Thumb-2 de 32 bits y de la FPU [II, §2-§5] |
| `src/verif/decoder_vectors.h` | 254 vectores de decodificación **generados** desde `doc/stm32f4xx/valida_instrucciones.py` |
| `src/verif/gen_decoder_vectors.py` | Generador del fichero anterior (ensambla con `arm-none-eabi-as` y compara con la codificación documentada) |
| `src/verif/fw/startup.s`, `stm32f407.ld`, `test_isa.c`, `Makefile` | Firmware bare-metal de autocomprobación (103 comprobaciones) |
| `src/verif/fw/coremark/` | CoreMark 1.0 de EEMBC (Apache-2.0, `LICENSE.md` incluido) con `core_portme.c/h`, `ee_printf` y `cm_console.c` adaptados al modelo |

### 2.2 Reescritos o ampliados

| Fichero | Cambios |
| :--- | :--- |
| `src/core/cpu.h` | Bucle fetch/decode/execute completo, búfer de prebúsqueda, monitor exclusivo, faults precisos, entrada y retorno de excepción, *lazy stacking*, LOCKUP, sonda del decodificador, sincronización por quantum |
| `src/core/scs.h` | Interfaz `core_sys_if`, NVIC completo, banco SCB completo, MPU de 8 regiones, SysTick dirigido por eventos |
| `src/core/fpu.h` | `FpuCore::execute` con todo [II, §5]; el módulo `Fpu` conserva la línea de IRQ 81 |
| `src/core/cortex_m4f.h` | Conexión CPU↔SCS↔FPU↔Debug y del `clk_hz` del SysTick |
| `src/core/debug.h` | Acceso del DCRSR al banco de registros del núcleo |
| `src/common/clock_gen.h` | `set_waveform(bool)`: permite desactivar la generación de la onda cuadrada conservando la frecuencia |
| `src/rcc/rcc.h` | `set_internal_waveforms(bool)`: aplica lo anterior a HCLK/PCLK1/PCLK2/TIMCLK/STCLK |
| `src/top/sc_main.cpp` | Grupos T15, T16 y T17 |
| `src/Makefile.mcu-sim` | `-O2` y `verif/*.h` en las dependencias |

---

## 3. Núcleo de ejecución

### 3.1 Estructura

El núcleo es un intérprete: `Cpu::exec_proc` es un `SC_THREAD` que, mientras
`rst_n` esté alto y no haya petición de parada del depurador, ejecuta
`Cpu::step()`. Cada paso:

1. atiende una excepción pendiente si la prioridad de ejecución lo permite
   (`check_exceptions`);
2. busca la primera media palabra (`fetch_hw`);
3. decide la longitud según `hw1[15:11] ∈ {11101, 11110, 11111}` [II, §1];
4. fija `R15 = PC + 4` (valor arquitectónico visible por la instrucción);
5. evalúa la condición del bloque IT;
6. ejecuta en `exec_16` o `exec_32`;
7. avanza ITSTATE, actualiza el PC y consume el tiempo de la instrucción.

El banco de registros es una `struct` C++ (`RegFile`, plan P3), no submódulos
SystemC: el coste de un `sc_signal` por registro sería prohibitivo y no aporta
nada observable en un modelo *loosely-timed*. La `struct` es accesible desde
`DebugSys` (registro DCRSR) y desde el banco de pruebas.

### 3.2 Prebúsqueda

`fetch_hw` mantiene un búfer de **una palabra**: una lectura de 32 bits sirve
las dos medias palabras alineadas, de modo que una instrucción de 16 bits
alineada no genera transacción de bus. El búfer se invalida en todo salto,
entrada y retorno de excepción, y al comenzar la sonda del decodificador. No
pretende modelar la cola de prebúsqueda real del Cortex-M4 (que es de tres
palabras y se recarga especulativamente); su función es reducir a la mitad el
tráfico de fetch, que es lo que domina el tiempo de simulación. El ART de la
Flash sigue viendo los accesos que sí se emiten.

### 3.3 Accesos a memoria

* `mem_read`/`mem_write` implementan `MemU` (admite no alineado) y `MemA`
  (exige alineación) [II, §4.1]. El trap de no alineado depende de
  `CCR.UNALIGN_TRP`; los accesos que lo exigen por arquitectura (dual,
  exclusivos, LDM/STM, FP) pasan `aligned_req = true`.
* Toda transacción se filtra antes por la MPU (`mpu_ok`), incluido el fetch
  (comprobación XN) [IR, §10.4].
* Los errores de bus se traducen a BusFault preciso (`BFSR.PRECISERR`, con
  BFAR válido) o a `BFSR.IBUSERR` si vienen del fetch.
* El monitor exclusivo local guarda dirección y tamaño; `STREX` falla si no hay
  reserva o si la dirección no coincide, y `CLREX`, las excepciones y los
  cambios de contexto la borran. La transacción se marca además con
  `AhbExt.exclusive` para que la matriz pueda arbitrar en fases posteriores.

### 3.4 Temporización

Modelo *loosely-timed* con anotación de ciclos [II, §7]: cada instrucción
declara sus ciclos (`cycles_`) y cada transacción devuelve su latencia en el
argumento de tiempo de `b_transport`. Ambos se acumulan en un tiempo local que
se sincroniza con el planificador cuando supera el **quantum** (1 µs por
defecto, público). Esto es lo que permite arrancar firmware real: sin quantum,
cada instrucción provocaría un cambio de contexto de la simulación.

---

## 4. ISA implementada

La ISA implementada es exactamente la de `doc/refs/stm32f407xx/informe_instrucciones.md`. La
tabla resume la correspondencia entre secciones del documento, funciones del
modelo y vectores de decodificación probados en T15.

| [II] | Contenido | Función | Vectores |
| :--- | :--- | :--- | ---: |
| §1.1 | Desplazamiento, suma y resta de 16 bits | `exec_16` | 7 |
| §1.2 | Inmediato de 8 bits | `exec_16` | 4 |
| §1.3 | Procesamiento de datos de registro bajo | `exec_16` | 16 |
| §1.4 | Registro alto, `BX`/`BLX` | `exec_16` | 5 |
| §1.5 | Carga/almacenamiento | `exec_16` | 17 |
| §1.6 | `ADR`, `ADD SP` | `exec_16` | 3 |
| §1.7 | Misceláneas (pila, `IT`, `CBZ`, extensiones, `REV`, hints) | `exec_16` | 20 |
| §1.8 | Saltos condicionales, `SVC`, `B` incondicional | `exec_16` | 6 |
| §2.1 | Múltiples, dual y exclusivos | `exec_32_ldstm` | 4 |
| §2.2 | `LDM`/`STM`, `PUSH`/`POP`, `TBB`/`TBH` | `exec_32_ldstm` | 11 |
| §2.3 | Procesamiento de datos con registro desplazado | `exec_32_dp_reg_shift` | 18 |
| §3.1 | Procesamiento de datos con inmediato modificado | `exec_32_dp_imm` | 6 |
| §3.2 | Inmediato plano (`MOVW`/`MOVT`, `SSAT`, `BFI`, `SBFX`…) | `exec_32_dp_imm` | 11 |
| §3.3 | Saltos, `MSR`/`MRS`, barreras, hints | `exec_32_branch_misc` | 10 |
| §4.1 | Carga/almacenamiento simple | `exec_32_ldst` | 13 |
| §4.2 | Procesamiento de datos de registro | `exec_32_dp_reg` | 34 |
| §4.3 | DSP/SIMD paralelas y `*XT*`, `USAD8`, `SEL`, `QADD`… | `exec_32_dp_reg` | 19 |
| §4.4 | Multiplicación, MAC largo y división | `exec_32_mul_div` | 10 |
| §5.2 | Aritmética FP de tres registros | `FpuCore::execute` | 13 |
| §5.3 | Aritmética FP de dos registros y `VMOV` inmediato | `FpuCore::execute` | 13 |
| §5.4 | Transferencias núcleo ↔ FP y `VMRS`/`VMSR` | `FpuCore::execute` | 7 |
| §5.5 | Carga/almacenamiento FP | `exec_32_fp_ldst` | 7 |
| | **Total** | | **254** |

Notas de implementación que conviene registrar:

* **ITSTATE**. La propia instrucción `IT` carga el estado y **no** lo avanza; lo
  avanzan las instrucciones del bloque. Un salto tomado dentro del bloque lo
  borra. Sin esa distinción, la primera instrucción condicional del bloque se
  ejecuta con la condición equivocada.
* **`SVC` frente a faults precisos**. `SVC` es una excepción síncrona de una
  instrucción que se considera **completada**: la dirección apilada es la de la
  instrucción siguiente. Un fault preciso apila la dirección de la instrucción
  que falló. El núcleo distingue ambos casos con `ret_next_instr_`.
* **`LoadWritePC` frente a `BXWritePC`**. Un `POP {…, PC}` acepta un valor
  EXC_RETURN y dispara el retorno de excepción; un `BX` con bit 0 a cero genera
  `UsageFault.INVSTATE`. Esta última comprobación es la que detectó, en el
  firmware de CoreMark, una tabla de vectores mal generada.
* **Transferencias FP de 64 bits**. En FPv4-SP los registros D solo existen para
  las transferencias y cargas de 64 bits: `Dm = M:Vm` ocupa la pareja
  `S[2m], S[2m+1]`. El ABI *hard-float* de GCC usa `VMOV Dm, Rt, Rt2` para
  devolver un `double` incluso cuando la aritmética de doble precisión es
  software; sin esa codificación, cualquier firmware que imprima un `double`
  se descarrila.

---

## 5. Excepciones, NVIC, SysTick y MPU

### 5.1 Modelo de excepciones [IR, §9]

* **Arbitraje**. `Scs::pending_exception()` devuelve la excepción pendiente de
  mayor prioridad cuyo nivel supere la prioridad de ejecución actual, calculada
  a partir de la excepción activa, PRIMASK, FAULTMASK y BASEPRI, con la
  agrupación PRIGROUP de AIRCR.
* **Entrada**. Apila la trama básica (R0-R3, R12, LR, PC, xPSR) y, si el
  contexto FP está activo (CONTROL.FPCA) y `FPCCR.ASPEN`, la extendida
  (S0-S15 + FPSCR) o reserva su espacio si `FPCCR.LSPEN` (*lazy stacking*),
  anotando `FPCAR`. Aplica el alineamiento a 8 bytes de `CCR.STKALIGN`
  reflejándolo en el bit 9 del xPSR apilado.
* **EXC_RETURN**. Se compone según el modo y la pila de origen y según si hay
  contexto FP (`0xFFFFFFE1/E9/ED` frente a `0xFFFFFFF1/F9/FD`).
* **Retorno**. Valida el valor de EXC_RETURN y el estado de la excepción que
  retorna; ante inconsistencias genera `UsageFault.INVPC`. Restaura la trama,
  el contexto FP si procede y el estado de la pila.
* **Encadenamiento**. Si al terminar el retorno queda una excepción pendiente
  de prioridad suficiente, se entra en ella sin desapilar/reapilar
  (*tail-chaining*).
* **Escalado**. Un fault configurable deshabilitado, o un fault durante el
  apilado o durante otro fault de igual o mayor prioridad, escala a HardFault
  con `HFSR.FORCED`; un fault durante el apilado del HardFault lleva a LOCKUP,
  que el modelo señala con `halted_on_lockup` y detiene la ejecución (en
  hardware el núcleo queda con PC = 0xFFFFFFFE).
* **Lazy FP stacking**. La primera instrucción FP tras una entrada con reserva
  perezosa vuelca S0-S15 y FPSCR en la dirección de `FPCAR` antes de ejecutarse
  (`lazy_fp_check`); un error de bus en ese volcado produce `BFSR.LSPERR`.

### 5.2 NVIC

82 líneas de interrupción [IR, §9.1.2] con detección de flanco sobre las
entradas `irq_in`, más NMI y la salida del SysTick. Registros ISER/ICER/ISPR/
ICPR/IABR/IPR y STIR, con prioridades de 4 bits alojadas en los bits [7:4] de
cada byte, como en el STM32F407. `SHCSR` refleja y permite forzar el estado de
las excepciones de sistema.

### 5.3 SysTick

Reescrito para funcionar **por eventos**: en lugar de decrementar el contador en
cada flanco del reloj (a 168 MHz eso son 3,4·10⁸ activaciones por segundo
simulado), el módulo toma la frecuencia del dominio de `clk_hz`, calcula el
instante del siguiente paso por cero y programa un único evento. `cvr_now()`
reconstruye el valor actual del contador a partir del tiempo transcurrido, de
modo que una lectura de `SYST_CVR` sigue devolviendo un valor coherente en
cualquier instante. Cambiar `SYST_RVR`, `SYST_CVR` o la fuente de reloj
recalcula la base (`rebase()`).

Esta es la fuente de tiempo que usa el *port* de CoreMark
(`barebones_clock()`), y por tanto la que produce la cifra "Total ticks" de su
informe.

### 5.4 MPU

Ocho regiones con `MPU_RNR/RBAR/RASR`, comprobación de tamaño, subregiones
(SRD), permisos por privilegio (AP), ejecución (XN) y prioridad de la región de
número más alto en caso de solapamiento. Se consulta en cada fetch y en cada
acceso a datos; el fallo produce MemManage con `MMFSR.IACCVIOL`/`DACCVIOL` y
`MMFAR` válido cuando corresponde.

---

## 6. Verificación

### 6.1 T15 — Decodificador dirigido por `valida_instrucciones.py`

`doc/stm32f4xx/valida_instrucciones.py` es el script que valida que la codificación
documentada en [II] coincide con la que produce `arm-none-eabi-as`.
`src/verif/gen_decoder_vectors.py` lo reutiliza como **generador de casos de
prueba**: extrae las 254 instrucciones, las ensambla, y emite
`src/verif/decoder_vectors.h` con la sección de [II], el texto en ensamblador y
la codificación en orden de programa.

El banco de pruebas usa la **sonda del decodificador** (`Cpu::probe`): coloca la
codificación en una zona de trabajo, detiene el bucle normal, ejecuta una única
instrucción con los faults *atrapados* en lugar de tomados, y devuelve el número
de bytes consumidos y los bits de CFSR que la instrucción habría provocado. Se
comprueba que:

* ninguna de las 254 genera `UsageFault.UNDEFINSTR` — es decir, todas se
  reconocen;
* todas consumen 2 o 4 bytes según su longitud real.

La sonda respeta la propiedad negativa: una media palabra `UDF` sí produce
`UNDEFINSTR`, de modo que la prueba no pasaría con un decodificador que
aceptase cualquier cosa.

### 6.2 T16 — Firmware bare-metal de autocomprobación

`src/verif/fw/test_isa.c` (664 líneas) se compila con
`arm-none-eabi-gcc -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard` y se
enlaza con un `startup.s` y un *linker script* propios contra el mapa real del
F407. Ejecuta **103 comprobaciones** agrupadas en:

1. aritmética, lógicas y banderas (incluidos `ADC/SBC`, saturaciones, `Q`);
2. desplazamientos y rotaciones con acarreo;
3. memoria: tamaños, signo, no alineados, `LDM/STM`, dual, exclusivos,
   bit-banding;
4. multiplicación, división (con `CCR.DIV_0_TRP`), MAC largo y DSP;
5. bloques `IT`, `CBZ/CBNZ`, `TBB/TBH` y saltos;
6. excepciones: `SVC`, `PendSV`, IRQ del NVIC con prioridades, `BASEPRI`,
   `PRIMASK`, cambio de pila MSP/PSP, `UsageFault` por división por cero;
7. FPU: aritmética, comparaciones y banderas en APSR, conversiones,
   `VMRS/VMSR`, `VPUSH/VPOP`.

El resultado se publica en un buzón en 0x2000 0000 que el banco de pruebas lee
con `peek32`. La ejecución completa consume **2 850 instrucciones y 7
excepciones**, sin discrepancias y sin LOCKUP.

### 6.3 T17 — CoreMark 1.0 (criterio de salida)

CoreMark de EEMBC (Apache-2.0; `LICENSE.md` incluido en el repositorio) se
compila **sin biblioteca C** (`-nostdlib -nostartfiles`) con el mismo `startup.s`
y el mismo *linker script*. El *port* (`core_portme.c/h`) usa el SysTick como
fuente de tiempo (`CLOCKS_PER_SEC = 16 000 000`, la frecuencia del HSI tras el
reset) y `ee_printf` escribe en una consola virtual en SRAM que el banco de
pruebas vuelca por pantalla. `cm_console.c` aporta además `modf`, que
`cvt.c` necesita para formatear los `double` del informe.

Salida de la ejecución estándar de la suite (`ITERATIONS = 1`):

```
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 1410836
Total time (secs): 0.088177
Iterations/Sec   : 11.340794
Iterations       : 1
Compiler version : arm-none-eabi-gcc
Compiler flags   : -O2 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0xe714
```

Los cuatro CRC coinciden con los valores canónicos del *2K performance run*, que
es la comprobación de corrección que el propio benchmark define: cualquier error
en la ISA, en el modelo de memoria o en la temporización de las excepciones los
altera. El banco de pruebas comprueba esos cuatro valores literalmente.

CoreMark exige además al menos 10 s de ejecución para que una puntuación sea
publicable; con una iteración eso no se cumple y el benchmark lo señala. La
ejecución oficial se puede lanzar con la imagen de más iteraciones:

```sh
make -C verif/fw/coremark clean && make -C verif/fw/coremark ITERATIONS=150
F2_CM_BUDGET_MS=40000 ./build/test407
```

Ejecutada así, el modelo produce una puntuación publicable y el propio benchmark
declara la ejecución válida:

```
2K performance run parameters for coremark.
CoreMark Size    : 666
Total ticks      : 211639945
Total time (secs): 13.227497
Iterations/Sec   : 11.340014
Iterations       : 150
Compiler version : arm-none-eabi-gcc
Compiler flags   : -O2 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16
Memory location  : STACK
seedcrc          : 0xe9f5
[0]crclist       : 0xe714
[0]crcmatrix     : 0x1fd7
[0]crcstate      : 0x8e3a
[0]crcfinal      : 0x242d
Correct operation validated. See README.md for run and reporting rules.
CoreMark 1.0 : 11.340014 / arm-none-eabi-gcc -O2 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 / STACK
```

`crcfinal` cambia porque acumula sobre todas las iteraciones; `crclist`,
`crcmatrix` y `crcstate` son los CRC de cada carga de trabajo y no dependen del
número de iteraciones, por lo que son los que compara el banco de pruebas.
La ejecución completa son **44 035 554 instrucciones en 13,24 s simulados**,
que el anfitrión resuelve en 188 s.

### 6.4 Resultado de la suite

```
Resumen F1: 124 comprobaciones OK, 0 fallos
Resumen F2:  12 comprobaciones OK, 0 fallos
TOTAL     : 136 comprobaciones OK, 0 fallos
```

---

## 7. Rendimiento del modelo

| Métrica | Valor |
| :--- | ---: |
| Instrucciones de modelo por segundo de anfitrión (CoreMark, `-O2`) | 215 000 – 234 000 |
| Relación tiempo simulado / tiempo de anfitrión a 16 MHz | ≈ 1 : 14 |
| Puntuación CoreMark del modelo a 16 MHz | 11,34 (0,71 CoreMark/MHz) |

Tres cambios concentran casi toda la mejora obtenida durante la fase (el punto
de partida eran ~70 000 instrucciones/s):

1. **Desactivación de la onda cuadrada de los relojes internos** durante cargas
   que no la observan. Generar HCLK a 16 MHz cuesta 3,2·10⁷ activaciones por
   segundo simulado, y a 168 MHz, 3,4·10⁸. `ClockGen::set_waveform(false)`
   mantiene la frecuencia publicada en `freq_hz` —que es lo que consultan los
   consumidores dirigidos por eventos— y deja de conmutar la señal.
2. **SysTick dirigido por eventos** (§5.3), por la misma razón.
3. **Búfer de prebúsqueda de una palabra** (§3.2) más compilación con `-O2`.

**Precisión temporal.** El modelo mide 1 410 836 ciclos por iteración de
CoreMark, es decir ≈ 4,7 ciclos por instrucción, frente a ≈ 1,5 en el Cortex-M4
real; la puntuación resultante, 0,71 CoreMark/MHz, es pesimista frente a los
≈ 3,4 CoreMark/MHz del silicio. Es el comportamiento esperado de un modelo
*loosely-timed* sin *pipeline*: cada acceso al bus se factura íntegro, no hay
solapamiento entre fetch y ejecución, y no se modela la cola de prebúsqueda ni
el reenvío de registros. La temporización aproximada por ciclos queda anotada
como trabajo de fases posteriores (§9); nada de lo verificado en F2 depende de
ella.

---

## 8. Decisiones de diseño de esta fase

| # | Decisión | Motivo |
| :--- | :--- | :--- |
| **F2-1** | El núcleo es un intérprete con el banco de registros en una `struct` C++, no en submódulos ni señales | Coste de simulación: un `sc_signal` por registro multiplicaría por órdenes de magnitud el número de eventos sin nada observable a cambio en un modelo LT |
| **F2-2** | El SCS se expone a la CPU por una interfaz C++ (`core_sys_if`) y no por sockets TLM | El núcleo consulta la prioridad de ejecución y el estado de excepción varias veces por instrucción; una transacción TLM por consulta sería insostenible. El acceso del *software* a esos mismos registros sí pasa por el bus PPB |
| **F2-3** | Los relojes internos pueden generar frecuencia sin generar onda (`set_waveform`) | Permite ejecutar firmware largo a velocidad útil sin perder la información de frecuencia; los consumidores por eventos (SysTick, temporizadores futuros) no notan la diferencia |
| **F2-4** | SysTick se modela por eventos, reconstruyendo `CVR` a partir del tiempo | Equivalente observable al contador real y con coste independiente de la frecuencia |
| **F2-5** | Búfer de prebúsqueda de una palabra en lugar de la cola de tres del Cortex-M4 | Captura el efecto dominante (mitad de accesos de fetch) sin comprometerse con una temporización de *pipeline* que este modelo no puede sostener |
| **F2-6** | Los vectores del decodificador se **generan** desde `doc/stm32f4xx/valida_instrucciones.py` y no se escriben a mano | El plan lo pide explícitamente; además garantiza que la prueba y la documentación no puedan divergir |
| **F2-7** | La sonda del decodificador atrapa los faults en lugar de tomarlos | Permite comprobar la propiedad "esta codificación se reconoce" sin montar un manejador de excepciones por vector |
| **F2-8** | CoreMark se incorpora al repositorio con su licencia y un *port* propio, en vez de enlazarse desde fuera | El criterio de salida de la fase debe ser reproducible con `make` sin red |
| **F2-9** | `startup.s` habilita CPACR.CP10/CP11 antes de llamar al programa | Es lo que hace `SystemInit()` en el arranque estándar de ST; sin ello la primera instrucción VFP —incluida la `VMOV Dm,Rt,Rt2` que el ABI *hard-float* usa para devolver un `double`— genera `UsageFault.NOCP`. El modelo hacía lo correcto; el firmware no |

---

## 9. Trabajo pendiente heredado a fases posteriores

**Heredado de F1 y ya resuelto en F2:** bucle fetch/decode/execute con la ISA
completa, excepciones, monitores exclusivos sobre `AhbExt.exclusive` y
comprobación XN/MPU antes de emitir cada transacción. Queda pendiente el uso de
DMI sobre SRAM y Flash para el fetch (la infraestructura existe en
`Sram::dmi`): con el búfer de prebúsqueda y `-O2` el rendimiento actual ya es
suficiente, y el DMI complicaría el modelado del ART.

**F3 (pines y RCC eléctrico):** sin cambios respecto de lo anotado en el informe
de F1 (CSS y NMI, detección eléctrica de cristal, MCO1/MCO2, umbrales de
POR/PDR/BOR, `RCC_SSCGR`, frecuencia de dominio en cada `BusSlave`).

**F6 (depuración) — preparado en F2:** `DebugSys` ya alcanza el banco de
registros del núcleo y puede detener y arrancar la ejecución; faltan los
registros DHCSR/DCRSR/DCRDR completos, los puntos de ruptura del FPB, los
*watchpoints* del DWT y la traza ITM.

**Temporización (F4 y posteriores):**

* modo aproximado por ciclos: solapamiento fetch/ejecución, cola de prebúsqueda
  de tres palabras y coste real de los saltos, para acercar la métrica de §7 al
  silicio;
* elevación de la matriz a AT (`nb_transport` con HTRANS/HBURST), ya preparada
  desde F1;
* modelado de la penalización de las excepciones (12 ciclos de entrada) en
  función del estado de la pila y del contexto FP.

**Cobertura de la ISA:** las 254 codificaciones de [II] están cubiertas por el
decodificador y una parte sustancial por la ejecución (firmware de T16 y
CoreMark). Falta una prueba de ejecución sistemática instrucción a instrucción
con comparación contra un modelo de referencia; se propone para F6, cuando el
depurador permita cargar y comparar estados completos del banco de registros.
