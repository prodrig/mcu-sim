# Trabajo pendiente de mcu-sim — revisión 1

Inventario consolidado de **todo lo que las siete fases han ido dejando fuera**,
extraído de los veintidós informes de `doc/` y contrastado contra el código de
`src/`. Fecha de corte original: fase F7 cerrada, **1811/1811 comprobaciones,
0 fallos**.

**Puesto al día con los dos planes de familia posteriores** —el del F446 y el
del F415/F417—, que cerraron puntos y abrieron otros. Estado de hoy: **tres
bancos**, `test407` (2117), `test446` (203) y `test417` (164), **0 fallos**, y
el invariante del F407 intacto en `2336217899213 ps`.

## Cómo leer este documento

Ninguna fase se declaró nunca "completada sin limitaciones": **las siete tienen
sección explícita de trabajo pendiente o de limitaciones**. Eso es deliberado —
la convención del proyecto ha sido decir lo que falta en vez de callarlo— y este
documento no es una lista de defectos, es el mapa de esas decisiones.

Cada punto lleva un identificador estable, la fase que lo originó, dónde está
anotado y qué impacto tiene. Las categorías son:

| Prefijo | Categoría | Naturaleza |
| :--- | :--- | :--- |
| **P** | Pendiente de plan | Trabajo previsto que aún no se ha hecho |
| **F** | Función no modelada | Los bits existen y se guardan; falta la máquina |
| **T** | Temporización y física | Simplificaciones de tiempo, pipeline o eléctrica |
| **D** | Dato no disponible en las fuentes | Parámetro inventado o tomado del datasheet |
| **X** | Discrepancia documental | [IR] se contradice, omite o choca con RM0090 — o ST se contradice consigo mismo |
| **V** | Hueco de verificación | Está modelado pero no se ejercita |
| **I** | Deuda de instrumentación y de proyecto | Andamios del banco, higiene del repositorio |

**Verificado en código** marca los puntos que se han comprobado en `src/` al
redactar esta revisión, no solo leído en un informe.

---

## 1. Resumen ejecutivo

- **Un solo pendiente de plan de primer orden**: la elevación del bus de
  *loosely-timed* a *approximately-timed* (**P-01**). Es la última línea de la
  tabla de fases de `smt32f407vg_diseño.md` y arrastra consigo otros cuatro
  puntos que dependen de ella.
- **Y un proyecto entero que empieza**: `mcu-sim-gui`, la contraparte gráfica,
  **en dos procesos** (**P-12**). Repositorio aparte, plan por fases escrito y
  protocolo especificado; de código, todavía nada.
- **Cincuenta y una funciones "bits sin máquina"**: registros que se guardan, se
  enmascaran y se leen correctamente, pero cuya lógica no se ejecuta. Casi todas
  corresponden a caminos que ningún firmware corriente usa, y casi todas están
  bloqueadas por la falta de un dispositivo externo que las ejercite.
- **Ocho discrepancias con el informe técnico [IR]** —más cuatro silencios que
  hubo que resolver por decisión propia, y **dos erratas de ST consigo mismo**
  que el plan del F415/F417 destapó—. Tres son contradicciones internas del
  propio informe y dos son omisiones que hubo que rellenar desde RM0090. Siete
  están resueltas de forma documentada y reversible; **X-04 (`FP_REMAP`) sigue
  abierta**, con la contradicción escrita en un comentario del código.
- **Catorce parámetros sin fuente documental**, todos expuestos como variables
  públicas para poder ajustarlos sin tocar el código.
- **Dos puntos que se perdieron entre fases** y que esta revisión recupera:
  **P-06** (espejo de 0x0 visto por maestros ajenos al núcleo, anotado en F1 como
  "pendiente de F3" y nunca mencionado en F3) y **I-04** (el `TODO` de la tabla
  de funciones alternativas, que ya está obsoleto).

---

## 2. Pendientes de plan (P)

### P-01 — Elevación de la matriz y los puentes de LT a AT
**Fase:** decisión D2 del plan · reiterado en F1 §9, F2 §9, F3 §11, F4-DMA §8,
F7-lowpower §8, F7-dcmi §7, F7-otg §7, F7-eth §7.

Hoy toda la interconexión usa `b_transport`: una llamada bloqueante por
transacción, con el tiempo **anotado** y no simulado. El arbitraje se resuelve a
posteriori con un `busy_until_` por puerto de esclavo, y el maestro que llega
tarde se lleva la penalización sumada a su propia anotación.

Lo que falta es `nb_transport_fw/bw` con las cuatro fases mapeadas sobre las
del AHB, y con ello: **solapamiento real** de la fase de dirección con la de
datos del acceso anterior, **arbitraje ciclo a ciclo** con reordenación,
**ráfagas como unidad** (`HBURST`) y **estados de espera por `HREADY`**.

*Preparación ya existente:* los sockets son `simple_target_socket` /
`simple_initiator_socket` (soportan los dos estilos y traen conversión
automática b↔nb); `AhbExt` ya lleva id de maestro, `HPROT`, `HBURST` y el bit de
exclusivo; los esclavos ya declaran latencia en `access_cycles()`; la matriz ya
tiene estado por esclavo y un puntero `last_granted_[]` **que se escribe y nunca
se lee** — es un gancho puesto para AT. *(Verificado en código:
`bus/ahb_matrix.h:177` y `:208`.)*

*Coste declarado en el plan:* **+50–100 % de esfuerzo y simulación ~10× más
lenta**. Los 23 s actuales de la suite pasarían a minutos.

*Migración por partes posible:* convertir primero la matriz, dejar los esclavos
en LT detrás del adaptador del socket, y pasar a AT solo Flash/ART, SRAM1/2 y
FSMC.

### P-02 — Modo aproximado por ciclos del núcleo
**Fase:** F2 §7 y §9.

El modelo mide **1 410 836 ciclos por iteración de CoreMark**, es decir **≈ 4,7
ciclos por instrucción frente a ≈ 1,5 del Cortex-M4 real**; la puntuación
resultante, **0,71 CoreMark/MHz**, es **pesimista** frente a los **≈ 3,4 del
silicio**. Causa declarada: no hay solapamiento entre búsqueda y ejecución, cada
acceso al bus se factura íntegro, y no se modelan ni la cola de prebúsqueda de
tres palabras ni el reenvío de registros.

Falta: solapamiento fetch/ejecución, cola de prebúsqueda real y coste real de
los saltos. El informe de F2 subraya que **nada de lo verificado depende de
ello**.

### P-03 — Penalización de las excepciones (12 ciclos de entrada)
**Fase:** F2 §9. No modelada, en función del estado de la pila y del contexto
en coma flotante.

### P-04 — Errores dinámicos de FIFO del DMA (*overrun* / *underrun* reales)
**Fase:** F4-DMA §8. **Bloqueado por P-01.**

La FIFO se sirve de forma secuencial dentro de cada concesión, así que **no
puede desbordarse por falta de ancho de banda**. `FEIF` cubre solo la
incompatibilidad de configuración, no el caso dinámico. El propio informe dice
que modelarlo "exigiría el modo AT de la matriz".

### P-05 — Búfer de prebúsqueda: DMI sobre SRAM y Flash
**Fase:** F1 §9 → F2 §9. **Descartado por ahora**, con motivo escrito: "con el
búfer de prebúsqueda y `-O2` el rendimiento actual ya es suficiente, y el DMI
complicaría el modelado del ART". Se recoge aquí para que la decisión no se
pierda, no como tarea abierta.

### P-06 — Espejo de 0x0 a SRAM/FSMC visto por maestros distintos del núcleo
**Fase:** F1, decisión F1-5, anotado literalmente como "**pendiente de F3**".

**Punto perdido entre fases:** el informe de F3 no lo lista ni entre lo resuelto
ni entre lo pendiente. Hoy el alias de 0x0 lo resuelve el router del núcleo y la
Flash mantiene su espejo por defecto; **qué ve un maestro ajeno al núcleo (DMA,
ETH, OTG_HS, AHB-AP) en la dirección 0 con `MEM_MODE` apuntando a SRAM o FSMC
sigue sin decidirse ni verificarse**.

### P-07 — Reenvío del `ack` del DMA a los periféricos
**Fase:** F4-DMA §8, F4-TIM §4.4 y §7.

La línea `ack_out` se genera correctamente y **está cableada a señales en el
top, pero ningún periférico la consume**. *(Verificado en código:
`top/stm32f407vg_bind2.h:408-409` la enlazan a `s_dma1_ack`/`s_dma2_ack`; el
`TODO(F4)` de la línea 410 sigue vivo y ningún periférico lee esas señales.)*

*Consecuencia activa, no teórica* — F4-TIM §4.4 la documenta como **limitación
conocida**: el temporizador emite su petición como pulso de anchura mínima
(1 ps) porque el DMA muestrea nivel; si el controlador estuviera ocupado
sirviendo otro stream en ese instante, **la petición se perdería**, mientras que
el silicio la mantiene hasta el reconocimiento. El efecto es "análogo a un
*overrun*, no un error silencioso de datos".

### P-08 — Bits de opción de la Flash y arranque automático del IWDG
**Fase:** F2 (bits de opción) → F5-RTC/WDG §7.

El "watchdog de hardware" (`WDG_SW = 0` en los bits de opción) **está modelado**
—la entrada `hw_start` lo arranca— pero **el top la tiene atada a cero** porque
los bits de opción siguen pendientes desde F2. *(Verificado en código:
`top/stm32f407vg_bind2.h:147`, `iwdg.hw_start(s_false); // TODO(F2)`.)*

### P-09 — Interrupción 81 (FPU) sin conectar
**Fase:** F2. *(Verificado en código: `top/stm32f407vg_bind2.h:301`,
`// 81 = FPU: TODO(F2) conectar core.fpu … a s_irq[81]`.)* La señal es interna
del núcleo y no está enrutada al NVIC.

### P-10 — Estados de espera exactos de los puentes APB
**Fase:** F1 → anotado para AT. *(Verificado en código: `bus/ahb_decoder.h:117`,
`// TODO(F7): estados de espera exactos según relación HCLK/PCLK en AT`.)*
**Bloqueado por P-01.**

### P-11 — Comprobación automática del límite de 240 mA en VDD/VSS
**Fase:** F3 §11.

`PinMux::total_pin_current()` **existe y funciona**, pero **nadie la llama**:
no hay comprobación automática del límite acumulado del encapsulado.
*(Verificado en código: `pins/pin_mux.h:117` es la única aparición del símbolo
en todo el árbol.)*

### P-12 — La contraparte gráfica: `mcu-sim-gui`, en dos procesos
**Fase:** posterior a F7. **Analizado en `doc/analisis_gui.md`; plan escrito,
repositorio creado y FASE 0 EJECUTADA** — `--gui host:puerto` se reconoce, con
T130 detrás (43 comprobaciones puras) y el invariante intacto.

`doc/analisis_gui.md` comparaba tres escenarios y recomendaba el **2** —un solo
ejecutable Qt con la simulación en su propio hilo—. **La decisión tomada es la
de dos procesos**, el escenario 3, y el motivo por el que el análisis la
descartaba —la distribución: «un alumno tiene que instalar *una* cosa y pulsar
*un* icono»— se convierte en diseño en vez de en excusa: **la GUI escucha
primero y lanza `mcu-sim` como proceso hijo**, con lo que no hay carrera de
arranque, no hay puerto ocupado y no hay cortafuegos.

El proyecto vive en un repositorio aparte, `mcu-sim-gui`, a propósito: `mcu-sim`
tiene que seguir clonándose y compilándose **sin Qt** en cualquier máquina, que
es lo que hace que sus 2 074 comprobaciones valgan en todas partes. Allí están
el plan por fases (`doc/plan_dos_procesos.md`, nueve fases) y la especificación
del protocolo (`doc/protocolo.md`).

**Lo que este repositorio tiene que crecer**, y es lo que cuenta como pendiente
aquí:

| | Qué | Fase del plan |
| :--- | :--- | :--- |
| a | ~~`--gui host:puerto`, con `localhost:3344` por omisión~~ **HECHO en la fase 0**, con las seis formas, sus siete errores y el aviso cuando el host no es la propia máquina. **Sin el argumento, nada cambia**: 2117/203/164 y `2336217899213 ps` | 0 |
| b | `Observable` / `Mando` en `ExtPartBase`, y las tres primeras piezas que los declaran (`Led`, `Button`, `Crystal`) | 1 |
| c | La instantánea y la cola de órdenes: los dos `SC_THREAD` de la frontera | 1 |
| d | `conecta(host, puerto)` y `escucha(host, puerto)` en `common/red.h`, **al lado** de las de bucle local y sin sustituirlas | 2 |
| e | El saludo antes de `sc_start()`, y `--valida --gui` | 3 |
| f | Los avisos de `SC_REPORT` desviados al socket | 4 |
| g | `--argumentos`: que el programa vuelque su lista de opciones, para que el diálogo de lanzamiento de la GUI no envejezca | 7 |
| h | `--sesion fichero.xml`: reproducir una sesión grabada **sin GUI**, que es lo que devuelve el determinismo que la interactividad quita | 8 |

**El riesgo que hay que vigilar, y tiene su mitigación escrita:** los dos
`SC_THREAD` de la frontera se construyen siempre —la elaboración de SystemC es
estática— y un proceso que despierta **mueve el invariante**. Sin `--gui` tienen
que esperar sobre un evento que nadie notifica nunca. Que eso funciona está
probado: la fase 4 del plan del F415/F417 metió el CRYP y el HASH enteros en la
elaboración del F407 sin que `2336217899213 ps` se moviera un picosegundo.

---

## 3. Funciones no modeladas — "bits sin máquina" (F)

Registros que se guardan, se enmascaran según los rasgos de la variante y se
leen correctamente; lo que no hay es una máquina que los ejecute.

### Núcleo y depuración

| Id | Qué falta | Fase | Motivo declarado |
| :--- | :--- | :--- | :--- |
| **F-01** | **TAP JTAG** completo (máquina IR/DR con `BYPASS`, `IDCODE`, `ABORT`) | F6-debug §11, F6-gdb §8 | "Ninguna herramienta corriente lo necesita en un F407". *Impacto:* un IDE forzado a JTAG **no se conectará**; en SWD sí |
| **F-02** | **ETM** (traza de instrucciones) | F6-debug §11 | Aparece en la ROM table con su identificación "como en el silicio", pero es una entrada funcionalmente vacía. En el LQFP100 "los pines de traza paralela ni siquiera están todos disponibles" |
| **F-03** | **Modo Manchester del TPIU** y **formateador CoreSight** (mezcla ITM/ETM) | F6-debug §11 | Se selecciona `SPPR` y se guarda `FFCR`, pero la salida por SWO es siempre NRZ, "que es el modo que usa todo el mundo" |
| **F-04** | **Más de un AP y más de un núcleo** | F6-gdb §8 | `APSEL` distinto de cero "no lleva a ninguna parte"; el stub anuncia un único hilo |
| **F-05** | **Reenvío de la traza ITM a GDB** por el canal `O` del RSP | F6-gdb §8 | El ITM saca la traza por SWO, pero el stub no la reenvía |
| **F-06** | **`vFlashWrite` con tamaños menores de 32 bits** | F6-gdb §8 | Programa siempre a 32 bits, lo que **exige VDD ≥ 2,7 V** |

### Periféricos de F4

| Id | Qué falta | Fase | Motivo declarado |
| :--- | :--- | :--- | :--- |
| **F-07** | **Modulación IrDA en el pin** (SIR: un cero como pulso de 3/16 de bit) | F4-UART §8 | `IREN`/`IRLP` se aceptan y el marco se transmite; hace falta al conectar un transceptor infrarrojo |
| **F-08** | **Smartcard: reintento automático ante `NACK` y preescalador del reloj de tarjeta** | F4-UART §8 | El tiempo de guarda de `GTPR` sí retrasa `TC` |
| **F-09** | **Modo mute y despertar por dirección** (`CR1.WAKE`/`RWU`, `CR2.ADD`) | F4-UART §8 | "Los bits existen, la máquina de silenciamiento no" |
| **F-10** | **Filtro digital de entrada de los temporizadores** (`ICxF`, `ETF`) | F4-TIM §7 | Se aplican polaridad y `ICxPSC`, pero no se filtra por número de muestras. "Afecta solo a entradas con rebotes" |
| **F-11** | **Precarga de configuración de canal** (`CCPC`/`CCUS`, transferencia diferida en el evento COM) | F4-TIM §7 | `COMIF`/`COMG` funcionan. Es lo que usa el control de motores sin escobillas con sensores Hall |
| **F-12** | **Rearme automático tras el freno (`AOE`)** | F4-TIM §7 | El bit es escribible y el freno actúa, pero `MOE` no se restaura solo en el siguiente update |
| **F-13** | **`ETPS`** (preescalador de ETR) y **el escalón de `SMCR.MSM`** | F4-TIM §7 | — |
| **F-14** | **Efecto eléctrico de `SYSCFG_CMPCR`** sobre los tiempos de conmutación del pad | F4-EXTI §8 | La celda de compensación se modela como "lista de inmediato" |

### Periféricos de F5

| Id | Qué falta | Fase | Motivo declarado |
| :--- | :--- | :--- | :--- |
| **F-15** | **Formato de trama TI del SPI (`CR2.FRF`)** | F5-SPI §7 | El bit es escribible y `FRE` existe; el modelo usa **siempre** el formato Motorola |
| **F-16** | **PCM del I2S (`I2SSTD = 11`) y `PCMSYNC`** | F5-SPI §7 | Los bits se aceptan y enmascaran; la trama de sincronismo corto/largo no está |
| **F-17** | **`I2SSRC`: reloj de audio externo por I2S_CKIN** | F5-SPI §7 | El modelo usa la salida R del PLLI2S; el pin alternativo no está enrutado |
| **F-18** | **Modo ARP de SMBus** (resolución dinámica de direcciones) | F5-I2C §6 | `CR1.ENARP` está reconocido y la estructura existe; "ningún dispositivo del banco lo usa" |
| **F-19** | **Disparos del ADC por evento de captura/comparación** (`TIMx_CHy` en `EXTSEL`/`JEXTSEL`) | F5-ADC §6 | **Dependencia:** el modelo del temporizador no exporta el evento CC en crudo, solo su petición de DMA, condicionada por `DIER`. Esos huecos de la tabla quedan a cero |
| **F-20** | **Disparo del ADC por EXTI11 y EXTI15**, y **del DAC por EXTI9 (`TSEL = 110`)** | F5-ADC §6, F5-DAC §6 | **Dependencia:** el EXTI no exporta esas líneas como señal de disparo |
| **F-21** | **Modos múltiples de ADC: disparo alterno e inyectado simultáneo** | F5-ADC §6 | Sí están el independiente y los regulares simultáneo y entrelazado, dual y triple |
| **F-22** | **Calibración del RTC** (`CALIBR` gruesa y `CALR` suave) | F5-RTC §7 | Los registros guardan lo que se les escribe pero **no alteran la marcha del calendario** |
| **F-23** | **Desplazamiento fino del RTC** (`SHIFTR` con `ADD1S`/`SUBFS`, y `SHPF`) | F5-RTC §7 | Fuera |
| **F-24** | **Subsegundos de las alarmas del RTC** (`ALRMASSR`/`ALRMBSSR`) | F5-RTC §7 | Se guardan y enmascaran; la comparación se hace solo sobre el segundo entero |
| **F-25** | **Flujo continuo de la MMC** (`DCTRL.DTMODE = 1`) | F5-SDIO §6 | **Bloqueado:** exigiría una MMC en `ext_parts.h` |
| **F-26** | **Lectura-espera de SD I/O** (`RWSTART`/`RWSTOP`/`RWMOD`, `CMD.SDIOSuspend`) | F5-SDIO §6 | **Bloqueado:** exigiría una tarjeta SD I/O en `ext_parts.h` |
| **F-27** | **CE-ATA** (`CMD.ENCMDcompl`, `CMD.nIEN`, `STA.CEATAEND`) | F5-SDIO §6 | Ídem |
| **F-28** | **Comunicación disparada por tiempo del bxCAN (`TTCM`)** con sus marcas en `TDTxR` | F5-CAN §7 | "Ninguna red CAN corriente lo usa" |
| **F-29** | **Recuperación de bus-off con la secuencia de 128 × 11 bits recesivos** | F5-CAN §7 | **Simplificación explícita:** el modelo la resuelve **al instante** cuando `ABOM` está puesto |
| **F-30** | **Despertar por actividad del bus del bxCAN (`AWUM`)** | F5-CAN §7 | — |

### Periféricos de F7

| Id | Qué falta | Fase | Motivo declarado |
| :--- | :--- | :--- | :--- |
| **F-31** | **Submuestreo del DCMI** (`BSM`/`OEBS`/`LSM`/`OELS`) | F7-DCMI §6 | Modelado **como eje de rasgos, no como función**: "el F407 no los tiene y el informe no los describe" |
| **F-32** | **Ráfaga síncrona del FSMC en los pines** | F7-FSMC §6 | `BURSTEN`, `CLKDIV` y `DATLAT` se usan solo para calcular el coste; no se saca `FSMC_CLK` por PD3 ni se encadenan los datos. "Con el LQFP100 esto es poco menos que académico" |
| **F-33** | **Ciclos de bus del banco 4 (PC Card) del FSMC** | F7-FSMC §6 | Los registros se comportan como deben; "en este encapsulado no hay por dónde sacarlos" |
| **F-34** | **FIFO de escritura del FSMC** (`SR.FEMPT` siempre a uno) | F7-FSMC §6 | Un firmware que espere a `FEMPT` no se bloqueará, "pero tampoco medirá la latencia real de una FIFO" |
| **F-35** | **`ECCPS`: tamaños de bloque de ECC distintos de 256 bytes** | F7-FSMC §6 | Se guarda; el acumulador no cambia de tamaño |
| **F-36** | **PSRAM síncrona con NWAIT durante la ráfaga y modo `WRAPMOD`** | F7-FSMC §6 | No se modelan más allá de sus bits |
| **F-37** | **Protocolo ULPI del OTG_HS** (registros del PHY externo por `DIR`/`NXT`/`STP`) | F7-OTG §6 | Los doce pines existen y los bits de `GUSBCFG` se guardan. "Un núcleo HS con ULPI mueve datos como si el transceptor fuera transparente" |
| **F-38** | **Alta velocidad USB de verdad**: negociación de *chirp* K/J y transacciones split reales (`HCSPLT`) | F7-OTG §6 | `ENUMSPD` puede quedar en `00` y las tramas bajan a 125 µs, pero no hay negociación |
| **F-39** | **SRP y HNP del OTG** | F7-OTG §6 | `GOTGCTL`/`GOTGINT` guardan y notifican; la negociación de sesión y el traspaso de rol no se simulan paso a paso |
| **F-40** | **Planificador de transferencias isócronas y de interrupción del USB** | F7-OTG §6 | `EPTYP` se guarda; no hay reparto por microtrama entre FIFO periódica y no periódica: todos los canales se atienden en cada trama |
| **F-41** | **Concentradores (hubs) USB** | F7-OTG §6 | Un solo dispositivo por puerto; sin direccionamiento de más de un aparato |
| **F-42** | **Control de flujo del Ethernet** (tramas PAUSE) | F7-ETH §6 | `MACFCR` guarda y enmascara; el MAC no las emite ni las interpreta |
| **F-43** | **Descarga de suma de comprobación IP/TCP/UDP** | F7-ETH §6 | `IPCO`, `CSTF` y `TDES0.CIC` se guardan; no se rellena ni verifica nada |
| **F-44** | **Semidúplex con colisiones** (retroceso exponencial y reintentos) | F7-ETH §6 | `DM`, `BL`, `DC`, `RD` se guardan y `COL` es entrada; el modelo trabaja en dúplex completo |
| **F-45** | **Descriptores mejorados de ocho palabras del Ethernet** | F7-ETH §6 | Con los sellos PTP dentro del descriptor. Hoy el sello va en las palabras 2 y 3 del de transmisión |
| **F-46** | **PPS y alarma PTP** (`PTPTTHR`/`PTPTTLR`) | F7-ETH §6 | No hay disparo ni alarma |
| **F-47** | **Filtrado VLAN por etiqueta** | F7-ETH §6 | `MACVLANTR` se guarda |
| **F-48** | **Contadores MMC completos** | F7-ETH §6 | El modelo lleva **cinco**, no los treinta y tantos del bloque |

### El acelerador criptográfico del F415/F417

Los tres puntos que siguen vienen del plan del F415/F417
(`doc/stm32f4xx/stm32f4xx_vs_415xx.md`). La columna *Fase* nombra la fase de
**ese** plan y la sección de **ese** informe, no las de las siete originales.

| Id | Qué falta | Fase | Motivo declarado |
| :--- | :--- | :--- | :--- |
| **F-49** | **El formato interno de los `HASH_CSRx`** | F415-2 §16.1 | Los 51 registros salvan y restauran el contexto **de verdad** —hay una prueba que intercala dos mensajes y los termina bien—, pero el formato de cada palabra **es el de este modelo, no el de ST**. Un firmware que *interprete* el contenido de un CSR no funcionaría; ninguno lo hace, porque **ST tampoco documenta el formato**: lo único que el silicio promete es que guardar los 51 y volverlos a escribir restaura el estado |
| **F-50** | **Clave de HMAC cuyo tamaño no es múltiplo de ocho bits** | F415-2 §16.2 | `HASH_STR.NBLW` admite cualquier número de bits y el silicio deja meter así la clave; la RFC 2104 no dice qué hacer con una clave que no acaba en byte entero y el HAL de ST nunca la genera. El modelo **avisa por `SC_REPORT_WARNING` y recorta al byte**, en vez de inventarse un relleno que nadie podría contrastar |
| **F-51** | **La copia de la clave preparada en `K0..K3`** | F415-3 §17.2 | Al preparar la clave para descifrar en AES (`ALGOMODE = 111`) el manual dice que el resultado «se copia de vuelta» en los registros de clave. Esos registros son **de solo escritura** [RM0090 Rev 22, §23.6.10, todos los bits marcados `w`], así que la copia **no es observable desde el firmware**. El modelo hace lo que sí se ve: cobra los ciclos, mantiene `BUSY` y deja `CRYPEN` a cero al terminar |

---

## 4. Temporización y física (T)

| Id | Simplificación | Fase | Impacto declarado |
| :--- | :--- | :--- | :--- |
| **T-01** | **Búfer de prebúsqueda de una palabra** en vez de la cola de tres del Cortex-M4 | F2 §3.2 | Reduce a la mitad el tráfico de fetch; no pretende modelar la cola real |
| **T-02** | **Los contadores de los temporizadores cuentan por tiempo, no por flancos**, con `CNT` interpolado | F4-TIM §4.1 | "Indistinguible de un contador ciclo a ciclo" para el observador |
| **T-03** | **SysTick modelado por eventos**, reconstruyendo `CVR` a partir del tiempo | F2 §5.3 | — |
| **T-04** | **Relojes internos que generan frecuencia sin generar onda** (`set_waveform`) | F2 §4 | Cualquier consumidor futuro que dependa de flancos reales debe tenerlo en cuenta |
| **T-05** | **`IDR` muestreado calculando el instante con `clk_hz`** en vez de esperar flanco de HCLK | F3 §3.5 | "Observacionalmente equivalente"; necesario para poder apagar la onda de los relojes |
| **T-06** | **Retardo de pad fijo, no función de la carga capacitiva** | F3 §3.3, diferido a F7 | Un pin lento que recibe cambios más rápidos que su retardo **colapsa las transiciones**: es la aproximación del límite de frecuencia, no una rampa |
| **T-07** | **Sin jitter instantáneo del espectro ensanchado**: se aplica solo el desplazamiento medio | F3 §6.4 | "Multiplicaría el número de eventos sin que ningún consumidor pudiera observarlo" |
| **T-08** | **Latencia de estabilización del DAC como retardo de transporte, no como rampa RC** | F5-DAC §3.2 | "El plan pide modelar la latencia, no la forma del transitorio" |
| **T-09** | **Paralelismo real de los dos puertos del DMA no modelado**; un solo hilo por controlador | F4-DMA §3.1 | En LT "los dos puertos no pueden operar literalmente a la vez"; lo observable se reproduce anotando latencias |
| **T-10** | **FIFO del DCMI modelada por palabras, no por bytes** | F7-DCMI §6 | Un desbordamiento real puede perder parte de una palabra; aquí se pierde la palabra entera |
| **T-11** | **Sensor del banco sin `t_su`/`t_h` ni jitter en PIXCLK** | F7-DCMI §6 | Solo cambia los datos en el flanco contrario |
| **T-12** | **Latencia del DAP interno no modelada** (`--gdb-dap`) | F6-gdb2 §8 | Correcto para depurar; "no serviría para estimar el ancho de banda de un AP real" |
| **T-13** | **Corrientes de consumo lineales con la frecuencia**, sin temperatura, sin dependencia de VDD y sin distinguir un periférico de otro | F7-lowpower §7 | "Sirve para comparar modos y ver caídas de tensión; **no sirve para certificar una autonomía**" |
| **T-14** | **Tiempos de despertar como parámetros, no como física** (13 / 40 / 375 µs del datasheet) | F7-lowpower §7 | Excepción: el del HSI sí sale del modelo del oscilador |
| **T-15** | **Reloj PTP leído del tiempo de simulación** en vez de acumular `PTPSSIR` en cada tick | F7-ETH §6 | — |
| **T-16** | **La velocidad del stub GDB depende del ritmo de simulación** (atiende el socket cada 100 µs simulados) | F6-gdb §8 | "No es un problema de corrección sino de ritmo" |
| **T-17** | **Capa física de paquetes, no de bits, en el USB**: sin NRZI, sin relleno de bits, sin CRC5/CRC16 ni reintentos por CRC | F7-OTG §3.1 y §6 | **Frontera deliberada**, escrita en la cabecera del fichero: "simular el bitstream no aporta nada a un modelo de MCU" |
| **T-18** | **Capa eléctrica del par trenzado y autonegociación fuera del Ethernet** | F7-ETH §3.1 | Es cosa del PHY, al otro lado de los pines; la autonegociación se resuelve por MDIO como en el silicio |

### Tres divergencias de comportamiento, no solo de precisión

| Id | Divergencia | Fase |
| :--- | :--- | :--- |
| **T-19** | **LOCKUP**: el modelo lo señala con `halted_on_lockup` y detiene la ejecución; **en hardware el núcleo queda con `PC = 0xFFFFFFFE`** | F2 §5.1 |
| **T-20** | **`VOS` afecta al consumo pero no a la frecuencia máxima**: el modelo **permite correr a 168 MHz en escala 3**, cosa que el silicio no admite. Único caso catalogado de comportamiento **más permisivo** que el hardware | F7-lowpower §7 |
| **T-21** | **Un evento del EXTI despierta también de un `WFI`**. En ARM el registro de evento no debe despertar de un `WFI`. La distinción está hecha **a medias**: el núcleo ya no despierta con el registro de evento, pero sí con el nivel de la entrada | F7-lowpower §7 |

---

## 5. Datos no disponibles en las fuentes (D)

Todos expuestos como **parámetros públicos** para poder ajustarlos sin tocar el
código. Ninguno es una constante verificada contra el silicio.

| Id | Parámetro | Valor usado | Fase |
| :--- | :--- | :--- | :--- |
| **D-01** | Tiempos de programación y borrado de Flash (`t_prog`, `t_erase`, `t_mass_erase`) | ⚠ no disponibles en [IR] | F1 §4.1 |
| **D-02** | `HSICAL`, calibración de fábrica del HSI | **0x10** (el informe lo da como `XX` en `0x0000 XX83`) | F1 §5.1 |
| **D-03** | Tiempo de enganche del PLL (`Pll::t_lock_s`) | **200 µs** | F1 §5.2 |
| **D-04** | Tiempo "VDD estable → primera instrucción" (`t_rst_release`) | **20 µs** por defecto frente a los 0,5–3,0 ms reales, "para no penalizar el tiempo de simulación". Asignarle 1,5 ms reproduce el silicio | F1 §5.4 |
| **D-05** | Impedancia del driver de salida por nivel de `OSPEEDR` (`r_on_speed`) | **{55, 40, 30, 25} Ω** | F3 §3.3 |
| **D-06** | Tiempo de transición por nivel de `OSPEEDR` (`t_pd_speed`) | **{50, 4, 2, 1} ns** | F3 §3.3 |
| **D-07** | Umbrales de POR/PDR y histéresis del BOR | **1,72 V subida / 1,68 V bajada** | F3 §7 |
| **D-08** | Tomas del LFSR del generador de ruido del DAC | Polinomio documentado `x¹²+x⁶+x⁴+x+1`; la verificación comprueba **propiedades, no una secuencia**, para no "inventarse un dato que no está en el informe" | F5-DAC §3.4 |
| **D-09** | Corrientes de consumo de todos los modos | ⚠ **[IR, cap. 14] no da ni una sola corriente ni un solo tiempo de despertar**; valores típicos del datasheet a 3,3 V y 25 °C | F7-lowpower §3.1 |
| **D-10** | Umbrales del PVD (`PLS[2:0]`) | ⚠ [IR, §14.6.1] remite al datasheet sin reproducir la tabla; se usan los típicos del F405/407 (2,0–2,9 V) con 100 mV de histéresis | F7-lowpower §4.4 |
| **D-11** | Matriz ITRx de los temporizadores | ⚠ **[IR] no la recoge en absoluto**; cableada desde RM0090 y aislada en el *netlist*. "Corregirla es cambiar ocho líneas de datos; ninguna prueba de `timers.h` depende de ella" | F4-TIM §4.3 |
| **D-12** | `CID` (Core ID) de los dos núcleos USB | Valores distintos elegidos como rasgo de variante; el valor exacto es específico del dispositivo | F7-OTG §2 |
| **D-13** | Celdas de DMA de los I2SxEXT | ⚠ **[IR] no recoge su asignación en las tablas de [IR, §11.4]**; sus líneas de petición quedan **sin conectar** | F5-SPI §7 |
| **D-14** | Ciclos de la preparación de clave del AES (`ALGOMODE = 111`) | ⚠ **la tabla 111 del [RM0090 Rev 22] no le da un número propio**: da los ciclos por bloque de cada algoritmo y calla sobre la preparación. El modelo cobra **lo mismo que una ronda de su tamaño de clave** (14, 16 o 18 ciclos) y lo dice en el comentario del `nucleo_proc`, en vez de cobrar cero | F415-3 §17.2 |

---

## 6. Discrepancias con el informe técnico [IR], y erratas de ST (X)

Ocho en total. Todas resueltas de forma documentada y **reversible**.

| Id | Discrepancia | Resolución adoptada | Fase |
| :--- | :--- | :--- | :--- |
| **X-01** | **¿Alcanza DMA2 la Flash?** [IR, §6.2] dice "No" en la tabla de conectividad; [IR, §11.1.1] dice que DMA2 "soporta transferencias memoria-a-memoria y acceso a la memoria Flash". **Contradicción interna** | A favor de §11.1.1: se añade el camino DMA2-M/DMA2-P → Flash-D a la máscara. Reversión documentada: "basta con quitar `S::FLASH_DCODE` de esas dos filas" | F4-DMA §9 |
| **X-02** | **`USART_CR1` incompleto**: [IR, §12.4.3-D] **omite TCIE, IDLEIE, PEIE, WAKE, RWU y SBK** | Se implementa el registro completo con la disposición estándar: "sin ella, el manejador de interrupción más común no funcionaría" | F4-UART §8 |
| **X-03** | **La matriz ITRx no está en [IR]** | Cableada desde RM0090 y dejada en el *netlist*, no en el modelo | F4-TIM §4.3 |
| **X-04** | **`FP_REMAP`**: con `REMAP` en [28:5] y `RMPSPT` en el 29, el campo **no alcanza la SRAM**, y sin embargo [IR, §13.7.1] dice que el destino del remapeado está en SRAM | Se guarda la dirección **entera** alineada a 32 bytes y **la contradicción queda escrita en un comentario** "en vez de fingir que no hay contradicción". **No resuelta** | F6-debug §4 |
| **X-05** | **Gating durante Sleep**: el manual admite **dos lecturas**. La literal ("en Sleep manda solo el `LPENR`") rompía once comprobaciones de fases anteriores | Gating en Sleep = `ENR AND LPENR`. Los dos criterios solo difieren con `EN=0` y `LPEN=1`, observable únicamente con un maestro ajeno a la CPU. Las dos lecturas quedan anotadas en `rcc.h` | F7-lowpower §4.1 |
| **X-06** | **`PWR_CR` se contradice consigo mismo**: reset `0x0000 0000` y a la vez `VOS[1:0] = 00` con "00: Reservado" | Se mantiene `0x0000 C000` (VOS = escala 1). "Es una contradicción de la fuente, no una decisión libre" | F7-lowpower §4.3 |
| **X-07** | **Endpoints del OTG_FS**: [IR, §12.15.1] dice "4 endpoints **además del** endpoint 0", pero el mapa de registros de la misma sección enumera "(0-3)" y solo `DIEPTXF1-3` | Se sigue **el mapa de registros**: cuatro en total, EP0 incluido, "que es el que se puede comprobar" | F7-OTG §1.1 |
| **X-08** | **`ETH_MACCR` se contradice consigo mismo**: reset `0x0000 8000` y a la vez "bit 14 = `RE`, reset 1" — pero `0x8000` **es el bit 15**; si `RE` valiera uno el reset sería `0x4000` | Se sigue **el valor de reset**; el bit 15 se modela como reservado fijo a uno. **Advertencia añadida:** "el resto de la tabla de bits de esa sección tampoco cuadra con la disposición estándar del bloque" | F7-ETH §4.1 |

### Silencios de [IR] resueltos por decisión propia

| Id | Caso | Decisión |
| :--- | :--- | :--- |
| **X-09** | **Reconfigurar `EXTICR` no genera flanco espurio**. "**No está dictado por [IR], que no dice nada del caso**. El silicio real **puede** producir ese pulso" | Se elige el comportamiento que no sorprende al firmware correcto. **Divergencia consciente respecto del silicio**, fijada por T46 | F4-EXTI §3.3 |
| **X-10** | **`PR` se levanta aunque la línea esté enmascarada en `IMR`**; [IR, §9.4.2] describe `PR` sin mencionar máscaras | `PR` se levanta con `IMR = 0` y la máscara solo decide si la petición llega al NVIC. En cambio `SWIER` **sí** se condiciona a que la línea esté desenmascarada, siguiendo el manual de referencia y no [IR]: **dos reglas de fuentes distintas** | F4-EXTI §3.4 |
| **X-11** | **Reinterpretación de `NDTR`**: la lectura literal de [IR, §11.3.1] es "inconsistente con el empaquetado" | El modelo cuenta en bytes y expone `NDTR` como elementos del **ancho de origen**. Con `PSIZE = MSIZE` las dos lecturas coinciden | F4-DMA §3.4 |
| **X-12** | **Variantes del CRC programable exigirían registros (`CRC_INIT`, `CRC_POL`) que no están en [IR]** y que este dispositivo no tiene | Se decide **no parametrizar** CRC/RNG por rasgos: "montar una `struct` de rasgos para describir un espacio con un solo punto sería maquinaria sin contrapartida". Polinomio y valor inicial quedan como constantes con nombre en un solo sitio | F5-CRC/RNG §1 |

### Erratas de la documentación de ST, y la regla que las desempata

No son discrepancias con [IR] sino **de ST consigo mismo**, encontradas al
preparar el F415/F417. Ninguna afecta al modelo —las dos se resolvieron antes de
escribir una línea— pero las dos harían perder una tarde a quien las encuentre
por su cuenta, y por eso quedan aquí.

| Id | Errata | Resolución adoptada | Fase |
| :--- | :--- | :--- | :--- |
| **X-13** | **La sección 23.6.2 del [RM0090 Rev 22] está mal titulada**: el manual tiene dos secciones seguidas, 23.6.1 y 23.6.2, tituladas las dos «CRYP control register (CRYP_CR) for STM32F415/417xx». La segunda **es la del F42x/F43x** —su figura lleva `GCM_CCMPH[1:0]` en 17:16 y `ALGOMODE[3]` en el 19, reservados en el F415/F417—, y el índice repite el error | Regla del proyecto: **el mapa de registros manda sobre la descripción bit a bit** cuando las dos se contradicen, porque es el que ST mantiene por referencia. Las tablas 114 y 115 sí están bien tituladas y se distinguen solas: la 114 termina en `0x4C` y la 115 en `0x8C`. El modelo se escribió desde la **114** | F415-0 §12.2 |
| **X-14** | **`stm32f417xx.h` no declara `DCMI_CR_CRE` ni `SYSCFG_PMC_MII_RMII`**, que `stm32f407xx.h` **de la misma versión del paquete** (V1.28.3) sí declara. Los dos chips llevan el mismo DCMI y el mismo SYSCFG | **No es una diferencia de silicio sino una omisión de ST en su cabecera**: el `[RM0090]` es un solo documento para los dos y no distingue. El modelo no lee nada de ahí. Advertencia de método que esto destapó: comparar cabeceras de **versiones distintas** mezcla diferencias de chip con diferencias de edición, y el `stm32f407xx.h` vendido en `verif/fw/cmsis/` es de otra versión | F415-0 §12.1 |

---

## 7. Huecos de verificación (V)

Cosas que **están modeladas** pero que la suite no ejercita.

| Id | Hueco | Fase |
| :--- | :--- | :--- |
| **V-01** | **Ejecución sistemática instrucción a instrucción contra un modelo de referencia.** Las 254 codificaciones de [II] están cubiertas por el **decodificador**, y "una parte sustancial" por la **ejecución** (firmware de T16 y CoreMark), pero falta la comparación completa de estados del banco de registros. Propuesto para F6, **no hecho** | F2 §9 |
| **V-02** | **CoreMark publicable.** Con `ITERATIONS = 1` no se alcanzan los 10 s mínimos que exige el benchmark; hace falta lanzar la imagen de 150 iteraciones (13,24 s simulados / 188 s de anfitrión) | F2 §7 |
| **V-03** | **Funcionamiento solo con la pila (VBAT).** La corriente por VBAT se calcula cuando falta VDD, pero **no hay prueba con solo la pila puesta** | F7-lowpower §7 |
| **V-04** | **Modo JPEG del DCMI.** Modelado, **sin prueba**: "el DCMI no interpreta el contenido y la prueba solo repetiría la del flujo continuo" | F7-DCMI §6 |
| **V-05** | **Firmware CMSIS de demostración del DCMI.** Único bloque sin él; el camino sensor → DCMI → DMA → SRAM se verifica solo desde el banco. Siguiente paso natural declarado: un `HAL_DCMI_Start_DMA` | F7-DCMI §6 |
| **V-06** | **`CRCNEXT` de extremo a extremo en el SPI.** El modelo calcula, compara y levanta `CRCERR`, pero la prueba "comprueba la coincidencia de los registros más que la secuencia completa de envío del CRC al final del bloque" | F5-SPI §7 |
| **V-07** | **`DATLEN` de 24 y 32 bits con `CHLEN = 32` en el I2S.** La ruta está escrita y el registro se respeta, pero **solo se verifica la combinación 16/16** | F5-SPI §7 |
| **V-08** | **`PINCOS` del DMA.** Implementado como incremento forzado de 4 bytes; "falta contrastarlo cuando existan periféricos de 32 bits con acceso empaquetado" | F4-DMA §8 |
| **V-09** | **Regresión que solo use `--gdb-dap` dejaría de ejercitar el SW-DP por completo.** Mitigado hoy porque T94 y T96 siguen usando los pines — es una condición a mantener, no un hueco actual | F6-gdb2 §8 |
| **V-10** | **La posición 80, compartida, con el HASH y el RNG pidiendo a la vez.** El OR de dos entradas está puesto y verificado por un lado —el grupo I1 del banco del F417 comprueba que el HASH la levanta y que al enmascararlo se cae—, pero **no hay ninguna prueba con las dos fuentes activas simultáneamente**, que es donde un OR mal cableado se nota: con el RNG pidiendo, bajar el HASH no debe bajar la línea | F415-4 §18 |

---

## 8. Deuda de instrumentación y de proyecto (I)

| Id | Deuda | Fase | Estado |
| :--- | :--- | :--- | :--- |
| **I-01** | **`DmaCtrl::tb_set_request()`** — instrumentación que "no corresponde a ningún registro del silicio", declarada como algo que "**desaparecerá de las pruebas en cuanto cada periférico active su propia línea**" | F4-DMA §5.1 | **Sigue viva:** 6 usos en `top/sc_main.cpp` *(verificado en código)*. Ligada a P-07 |
| **I-02** | **Puerto `from_tb` de la matriz** — socket que "no corresponde a hardware", creado porque en F1 no existían los maestros DMA/ETH/OTG_HS | F1 §3.1 | Los tres maestros ya existen; el puerto sigue siendo necesario para las pruebas de conectividad, pero conviene revisar si aún hace falta para todo lo que hoy lo usa |
| **I-03** | **`AnalogNet` no tiene desregistro.** Un componente externo destruido seguía cargando el pin; paliado en el destructor de `ExtPart` dejando el driver en alta impedancia | F3 §9 | **Limitación estructural del canal que persiste**: hay Hi-Z, no desregistro real. *(Paso 1 de `parts/`: deja de ser un problema práctico —`ExtPartBase::set_enabled(false)` pone en Hi-Z todos los drivers de la pieza, y con `R_HIZ = 1e12` frente a 30 Ω eso es eléctricamente invisible. Sigue siendo la razón por la que una pieza ausente del SVG se construirá DESCONECTADA y no se dejará de construir.)* |
| **I-04** | **`TODO(F4/F5)` de la tabla de funciones alternativas obsoleto** | F3 §4 | *(Verificado en código: `top/stm32f407vg_bind2.h:766` sigue pidiendo "TIM CHx, CAN, SDIO, FSMC, ETH, ULPI, DCMI, RTC_AF1", pero **todos salvo `RTC_AF1` (PC13, tamper/timestamp) están ya registrados**: AF10 ULPI, AF11 ETH, AF12 FSMC/SDIO, AF13 DCMI.)* **Actualizar el comentario y dejar solo `RTC_AF1`** |
| **I-05** | **`SignalLink` es unidireccional por construcción.** No sirve para un hilo compartido de verdad: el medio dúplex y el bus open-drain se modelan conectando los pines al mismo `AnalogNet` | F4-UART §5.1 | Limitación asumida de la pieza. *(Paso 1: la asimetría es ahora visible desde fuera —el terminal `origen` sale marcado `pasivo` en el netlist y el `destino` no—, así que un validador puede comprobarla en vez de confiar en que se recuerde.)* |
| **I-06** | **Falta de dispositivos externos en `parts/ext_parts.h`** que bloquean F-25, F-26 y F-27: no hay **tarjeta SD I/O** ni **MMC** | F5-SDIO §6 | Bloqueante de tres funciones |
| **I-07** | **`Makefile.mcu-sim` no se llama `Makefile`** porque el puente remoto no permite escribir ese nombre; se sugería renombrarlo | F0 | Divergencia con lo documentado en el plan (§5: "`src/ Makefile, README.md`") |
| **I-08** | **Restricciones de entorno**: la biblioteca SystemC de Ubuntu está construida con **C++17** y el estándar debe coincidir o falla el enlazado (`sc_api_version...`); `simple_target_socket_optional` exige **SystemC ≥ 2.3.3** | F0 | Requisitos mínimos a dejar escritos en el README |
| **I-09** | **Carpetas `_to_delete/git-tmp*` acumuladas en el repositorio** por los restos de bloqueo de git del puente remoto | — | Limpieza pendiente; no se pueden borrar desde el contenedor |
| **I-11** | ~~**Paso 2: `Netlist` en memoria**~~ | — | **HECHO Y CERRADO.** `parts/netlist.h` + `parts/netlist_parts.h`; **la placa entera** —43 piezas de 20 tipos— se declara y la construye el netlist, y no queda ni un `new` de pieza externa en `sc_main.cpp`; 29 comprobaciones nuevas (T121). El ejercicio destapó tres cosas: los nodos son del circuito y no del componente (`CanWire` tenía el suyo), no todo lo que une dos componentes es un nodo (de ahí `Instancia::refs`) y la ida y vuelta necesita las dos direcciones. De paso se cerró una fuga de F5 (`can_bus`, `xcvr1`, `xcvr2` y `nodo_ext` no se destruían) y se evitó un uso después de liberar en el destructor. Véase `doc/stm32f4xx/stm32f407vg_parts_paso2.md` |
| **I-12** | ~~**Paso 3: lector de XML y validador eléctrico**~~ | — | **HECHO.** `parts/part_factory.h` (registro cadena→creador con auto-registro), `parts/xml_min.h` (lector de XML estricto, sin dependencias), `parts/netlist_xml.h` y el ejecutable `./build/mcu-sim placa.xml firmware.bin`. La validación eléctrica encontró **siete conducciones simultáneas ciertas** en la placa del banco —dos buses I2C reales, el cable en Y del CAN y cuatro pines compartidos entre grupos de prueba— que vivían implícitas en `i2c_bus()`, `adc_links()` y compañía; declararlas dejó la placa en 0 avisos. El cortocircuito PA2/PB11 de F7-ETH está reconstruido en T121 y salta **antes de simular**. Véase `doc/stm32f4xx/stm32f407vg_parts_paso3.md` |
| **I-13** | **Paso 4: generador de SVG desde el netlist**, con `id` estables por componente y por nodo, y opcionalmente coloreado desde una traza | — | **Pendiente, y ya tiene de dónde salir**: el netlist lleva nodos, terminales, parámetros, referencias y la marca de qué nodos hay que crear |
| **I-14** | **La validación eléctrica avisa por conducción simultánea, no por tensiones incompatibles.** Dos piezas que conducen el MISMO nivel sobre un nodo no son un cortocircuito, y hoy se avisa igual | — | **Falso positivo conocido y acotado.** Distinguirlo exigiría que cada terminal declarase qué tensión y qué impedancia presenta, información que hoy solo existe dentro del constructor de cada pieza. Se calla declarando el nodo con `nodo_bus()`, y declararlo documenta |
| **I-15** | ~~**Nodos compartidos entre pines: cada pad creaba su `AnalogNet` y lo ataba a un `sc_port`**~~ | multi-MCU §4.3 | **HECHO.** `Cableado` en `pins/pin_mux.h`, el atributo `une=` en el XML, `Netlist::nodo_une()` y `cableado_desde_netlist()`. Un pad solo deja de crear su nodo si un `<nodo … une="…">` lo nombra, así que el coste para una placa sin puentes es cero. El banco lleva el puente PB9–PD3 y **T122** lo comprueba, incluido lo que ninguna pista (`SignalLink`) puede dar: con los dos pines conduciendo, el nodo se queda a 1,65 V y los dos pads avisan de sobrecorriente. Véase `doc/multi_mcu.md`, §4.3 y §4.5 |
| **I-16** | ~~**El volcado del inventario daba nombres de nodo ambiguos** (`basename()` descarta la jerarquía) y **`registra_mcu()` sin prefijo se pisaba a sí mismo en silencio**~~ | multi-MCU §7.1 y §7.2 | **HECHO.** `common/nombres_nodo.h` parte del nombre jerárquico y cualifica el pad con su MCU cuando hay más de uno; `registra_mcu(prefijo, …)` y un `SC_REPORT_ERROR` en `NodeMap::registra()` cuando un nombre se da de alta dos veces con dos `AnalogNet` distintos. Con un solo MCU el volcado sale idéntico al anterior, que era la condición para hacerlo sin migrar nada |
| **I-17** | ~~**Dos fallos mudos que solo se manifiestan con DOS MCUs**: los `static bool warned` de `periph/adc.h` y `periph/sdio.h` y el puerto de GDB único~~ | multi-MCU §7.3 y §7.4 | **HECHO**, al mismo tiempo que I-20, que es cuando su síntoma pasó de imposible a inevitable. Los dos avisos son ahora miembros `mutable` (`aviso_adcclk_`, `aviso_ck_`), así que cada chip avisa de lo suyo; el puerto sale del atributo `puerto_gdb` de cada `<mcu>`, y `DebugCaps` ya lo llevaba por instancia, de modo que el modelo no hubo que tocarlo |
| **I-20** | ~~**Varios MCUs en una placa, con un stub de GDB por chip**~~ | multi-MCU §5 | **HECHO.** El elemento `<mcu tipo id firmware depuracion puerto_gdb>` en `parts/netlist.h` y `parts/netlist_xml.h`; `sim` monta uno o varios chips, cada uno con su firmware, su modo de depuración (`pines` o `dap`) y su puerto TCP, y con algún stub escuchando no se detiene solo. La regla de compatibilidad —ningún `<mcu>`: nombres desnudos; uno: los dos nombres y mandan los argumentos de la línea de órdenes; dos o más: solo cualificados y un argumento global se rechaza nombrando los MCUs— deja valer sin tocar todo lo escrito hasta hoy. `placas/dos_mcu.xml` son dos F407 hablando por I2C y depurables a la vez en 3333 y 3334; **T123** cubre la capa de declaración con 28 comprobaciones |
| **I-21** | **No hay comprobación automática del MONTAJE de varios MCUs.** T123 cubre la declaración —leer `<mcu>`, resolver `u0.PD12`, rechazar lo ambiguo, el chip inexistente y el puerto repetido—, pero que dos chips se construyan, arranquen y se hablen solo se verifica a mano corriendo `sim` sobre `placas/dos_mcu.xml` | multi-MCU §8, paso 6 | **Hueco conocido.** La suite monta un único `dut` del que cuelgan la mitad de sus 1935 comprobaciones, y meter un segundo dentro sería duplicar la elaboración del banco entero para probar otra cosa. El sitio natural es un banco aparte que ejecute `sim` sobre las placas de `placas/`, y no existe |
| **I-22** | ~~**El modelo solo compilaba en Linux**~~ | analisis_gui §19.3 | **HECHO A MEDIAS, y lo que falta está acotado.** Toda la dependencia del sistema operativo se ha recogido en `common/red.h` —traducción entre sockets de Berkeley y Winsock, más `SO_NOSIGPIPE` para macOS— y ningún otro fichero de `src/` incluye ya una cabecera del sistema. El `Makefile.mcu-sim` es único para las tres plataformas, con detección automática y `PLATAFORMA=` para forzarla, sufijo `.exe`, `-lws2_32`, `-D__USE_MINGW_ANSI_STDIO=1` (MinGW no entiende `%llu` sin él, y el modelo lo usa 28 veces) y enlazado estático de las DLL de MinGW. `verif/prueba_red.cpp` (`make red`) ejercita la capa con 13 comprobaciones sin necesitar SystemC. **Verificado**: Linux con g++ y con clang, 1935/1935 y el mismo tiempo simulado; el cruce a MinGW-w64 compila y enlaza un PE32+ sin un aviso; la rama de macOS compila forzando su combinación de macros. **Falta**: construir SystemC para MinGW y para macOS, y ejecutar allí |
| **I-23** | **Sin verificación en Windows ni en macOS.** El cruce con MinGW demuestra que el código compila y enlaza, no que funcione; y de macOS solo se ha compilado la rama específica | analisis_gui §19.3 | **Hueco conocido.** Lo que falta es tener la biblioteca de SystemC en las dos plataformas y pasar allí `make red` y `make test407`. Es el paso 0b del orden de trabajo del análisis de la GUI, y para un programa que se reparte a alumnos no es opcional |
| **I-24** | **Se sigue en SystemC 2.3.4 (IEEE 1666-2011) pudiendo estar en la línea 3.0 (IEEE 1666-2023)** | analisis_systemc3 | **Decisión consciente, no olvido.** El inventario de la API que el modelo usa —`sc_module`, procesos, señales, `sc_prim_channel`, `sc_spawn` y TLM-2.0, nada más— es tan central que no se espera ningún cambio, y el único `#include` de cabeceras internas de SystemC ya se ha eliminado. Pero **ninguna ventaja conocida de la 3.0 ataca un problema que tengamos** —el coste lo domina el número de despertares, no el planificador— y sí tiene un coste: hoy la 2.3.4 se instala con `apt` y la 3.0 habría que compilarla. **La pregunta está subordinada a I-23**: quien construya SystemC para Windows y macOS que pruebe las dos versiones a la vez |
| **I-25** | ~~**`sim` no tenía freno de tiempo real**~~ | analisis_gui §16 | **HECHO.** `--tiempo-real` ata el avance simulado al reloj de pared, y `--tiempo-real=<factor>` lo escala (`=4`, cuatro veces más rápido; `=0.5`, a la mitad para poder mirar). El proceso **se duerme** hasta que el reloj de pared alcanza al simulado, lo que dentro de un proceso de SystemC detiene toda la simulación —las corrutinas comparten hilo—, que es justo lo que se busca. Solo frena: si el modelo va más lento que el tiempo real no duerme ni acumula deuda. **Medido**: 2000 ms simulados salen en 0,033 s sin freno, **2,000 s** con `--tiempo-real` y 0,500 s con `=4`; y esperando a GDB la CPU baja de **99,6 % a 5,3 %**. Con esto un LED que parpadea a 1 Hz parpadea a 1 Hz y se puede mirar, que para un simulador didáctico no es un lujo. **Sigue abierto lo otro que decía este punto**: «correr sin parar» y «aceptar un depurador» siguen siendo la misma opción (`--gdb`), cuando son dos cosas distintas |
| **I-26** | **`sim` acepta como firmware cualquier argumento que no reconozca**, incluida una opción mal escrita: `sim placa.xml --netlist` intenta cargar un fichero llamado `--netlist` y muere con «no se puede cargar --netlist» | — | **Errata de usabilidad, barata.** `--netlist`, `--inventario` y `--valida` son opciones de `stm32f407vg` y solo la última existe en `sim`, así que confundirlas es lo esperable. Bastaría rechazar todo argumento posicional que empiece por `-` remitiendo a `--help` |
| **I-27** | **La sonda `ST-LINK` de STM32CubeIDE no se puede usar contra el simulador, y no por falta de datos sino por un protocolo cerrado.** El IDE manda `monitor ReadAPEx 0x0 0xF8` y, no le guste lo que le guste, se despide con `D`: «Could not verify ST device!» | analisis_gui §17 | **Cerrado como decisión, con las dos ramas probadas.** (a) **Lo que se reparte**: `cubeide/simulador.launch` + `cubeide/README.md`, `GDB Hardware Debugging` con `Generic TCP/IP`, que **funciona**. (b) **Suplantar el protocolo de ST: intentado y refutado.** El stub implementa `ReadAPEx`/`WriteAPEx` —`dap_leer_ap()`/`dap_escribir_ap()` en los dos stubs, `DebugSys::ap_registro()`— y contesta el valor correcto (`0xE00FF003`, el `BASE` del AP 0). **Verificado contra el IDE con la traza de entrada y salida**: el stub responde y ST se marcha igual, sin reintentar y **sin recorrer la ROM table con paquetes `m`**, que es lo que haría si estuviera identificando el chip de verdad. Luego lo que rechaza no es el valor sino el formato —o el simple hecho de que al otro lado no esté su servidor—, y eso no se averigua sin documentación que ST no publica. Seguir sería probar formatos a ciegas sin saber cuántas órdenes más vendrían detrás. **La capacidad implementada se queda**: no estorba, es fiel (un AP tiene esos registros) y deja el camino abierto si algún día aparece la especificación |
| **I-28** | ~~**El SW-DP ignoraba APSEL**~~ | F6 | **HECHO, y encontrado por accidente** al probar `ReadAPEx 0x1 0xF8`: cualquier AP contestaba lo que el AP 0, porque `ejecuta_lectura()`/`ejecuta_escritura()` solo miraban `SELECT[7:4]` (APBANKSEL) y no `SELECT[31:24]`. En el silicio un AP que no existe lee ceros, **y así es como una sonda cuenta cuántos hay**: recorriendo IDR hasta que sale 0. Con el fallo, esa enumeración no terminaba nunca. Corregido en `core/debug.h` en los dos caminos, y `DebugSys::ap_registro()` sigue el mismo criterio |
| **I-29** | **El stub no anuncia el mapa de memoria** (`qXfer:memory-map:read`), así que GDB no sabe dónde hay Flash y **cae a paquetes `X`** para cargar el programa en vez de usar `vFlashErase`/`vFlashWrite`, que están implementados desde F6 y no los usa nadie | F6 | **Paliado, no cerrado.** El síntoma —la carga perdida en silencio y el núcleo bloqueado— está resuelto por el otro lado: `escribir_bytes()` reconoce el rango de Flash y ejecuta la secuencia de programación, que es lo que hace una sonda de verdad, y eso arregla a **cualquier** cliente, anuncie o no mapa. Anunciarlo sería lo canónico y además le diría a GDB qué es RAM y qué no; se dejó fuera porque un mapa incompleto hace que GDB **se niegue a leer** lo que no aparece en él —los periféricos, el PPB— y eso no se puede verificar sin un GDB de ARM, que en el contenedor no hay |
| **I-30** | ~~**`DHCSR.C_MASKINTS` no lo miraba nadie**~~ | F6 | **HECHO.** El bit se guardaba —está en la máscara `0x2F` de `DebugSys::escribir()`— pero **ningún sitio lo consultaba**, así que no enmascaraba nada, y el stub tampoco lo ponía al dar un paso. Consecuencia en la práctica: en un proyecto de STM32CubeIDE, con SysTick latiendo a 1 kHz, **cada `step` se comía la interrupción pendiente** y el depurador aterrizaba en `SysTick_Handler`; «paso sobre esta línea» se convertía en «entra en el manejador», una y otra vez, y el paso a paso quedaba inutilizable. Ahora `core_debug_if::dbg_mask_ints()`, `Cpu::check_exceptions()` deja pendientes las excepciones configurables mientras esté puesto —NMI y HardFault pasan siempre, que no se enmascaran— y `GdbRsp::paso()` escribe `C_STEP|C_MASKINTS|C_DEBUGEN`, quitándolo al reanudar. **Medido con el control negativo**: mismo firmware, misma interrupción pendiente, mismo paso; sin el bit, `IPSR=15` y PC en el manejador; con él, `IPSR=0` y PC en el programa |
| **I-31** | ~~**El SysTick contaba con el núcleo parado en depuración**~~ | F6 | **HECHO.** ARMv7-M §B3.3.1: el contador del SysTick no decrementa con el procesador detenido en Debug state —es parte del núcleo, no un periférico—. `SysTick` tiene ahora una entrada `parado`, atada a `sig_halted_` en `cortex_m4f.h`: al parar congela el valor, al reanudar sigue desde él, y el tiempo detenido sencillamente no existe para ese contador. El detalle que costó una relectura: `halt_proc()` corre con la señal YA a true, así que tiene que calcular el valor con los ticks **brutos** —de ahí `elapsed_ticks_brutos()`—, o el contador retrocedería. **Medido**: núcleo parado, dos segundos de reloj de pared (muchísimos simulados), CVR idéntico; tras reanudar, CVR movido |
| **I-32** | ~~**El HSE arrancaba en modo BYPASS con un cristal pasivo colgado de OSC_IN**~~ | F1 §4.2 | **HECHO.** `Oscillator::source_present()` solo comprobaba que el nodo **no estuviera al aire**, y `effective_hz()` caía a la frecuencia nominal si no había podido medir nada; como un `Crystal` polariza el pin a VDD/2 por 1 MΩ, un `RCC_HSE_BYPASS` sobre un cristal **arrancaba**. En el silicio no: con `BYPASS` el amplificador está apagado y OSC_IN es una entrada digital, así que un resonador pasivo no da nada, `HSERDY` no sube y el firmware se queda esperando o cae en su `Error_Handler`. **El simulador era MÁS PERMISIVO que el chip**, que es la dirección de error que peor le viene a un proyecto didáctico: el alumno lo ve funcionar aquí y colgarse en la placa. Ahora `fuente_valida()` separa los dos modos con lo único que hay, la electricidad del pin: **en bypass hace falta que el pin CONMUTE** (que haya una frecuencia medida), en modo cristal basta con que haya algo conectado. Y la medida deja de ser pegajosa: si el pin se queda al aire se borra, o al colgar otra cosa del mismo pin el bypass arrancaría con la frecuencia del reloj que ya no está. **Verificado con firmware real de STM32CubeIDE sobre `discovery_min`** —`HSE_BYPASS`+cristal acaba en `Error_Handler` con HSI a 16 MHz; `HSE_BYPASS`+`ExtClock` y `HSE_ON`+cristal llegan a `main` con SYSCLK a 90 MHz— y con tres comprobaciones nuevas en **T21** para que no vuelva. *(De camino se descartó un cambio que parecía natural y era falso: usar en modo cristal la frecuencia medida en el pin en vez de la nominal. Rompió tres pruebas —HCLK a 252 MHz en vez de 168— y está bien que las rompiera: lo que oscila ahí es el amplificador contra el cuarzo, y quien manda es el corte del cristal, no lo que se lea en el nodo.)* |
| **I-33** | ~~**Una entrada ya sujeta antes de arrancar la simulación se leía como 0 para siempre**~~ | F3 | **HECHO, y era el peor de todos los encontrados hasta ahora.** `Pad::pad_proc()` escribía `din = false` y **se ponía a esperar** un cambio en el nodo o en la configuración. Un nodo que ya está gobernado cuando empieza la simulación —una resistencia de pull externa, que conduce desde que se construye— no genera ningún evento después, así que el pad **nunca llegaba a mirarlo**: el IDR decía 0 con el pin a 3,3 V, indefinidamente. El banco no lo veía porque sus entradas las mueve un `Driver` en marcha, y mover el nodo sí despierta al pad. Es exactamente el mismo fallo que ya se había corregido en `Led` —«evaluar ANTES de esperar», con su comentario y todo— y que nadie miró en el `Pad`. Aquí es peor: en el LED el que se equivocaba era el modelo; aquí es **el firmware que lo lee**, y el alumno acabaría buscando el error en su código. Arreglado mirando una vez antes del bucle. **Medido**: tres pines sujetos a 3,3 V desde el arranque pasan de leerse `0` a leerse `1`, con el mismo tiempo simulado al picosegundo y 1935/1935 |
| **I-34** | ~~**El pulsador solo podía ir a masa**~~ | F3 | **HECHO.** `Button` tiene ahora `v_cerrado` (0 V por omisión). Hacía falta para describir la placa de verdad: en la STM32F4-Discovery el botón de usuario lleva PA0 **a VDD** y una resistencia externa lo sujeta abajo, que es la razón de que CubeMX genere para esa placa un EXTI por flanco de **subida** sin pull interno. Con un pulsador a masa ese flanco no llega nunca y el firmware parece roto sin estarlo |
| **I-35** | ~~**El pulsador solo podía ser normalmente abierto**~~ | F3 | **HECHO.** `Button` tiene ahora `normalmente`, con `abierto` (el de siempre) y `cerrado`. Lo que conduce pasa a ser **«pulsado XOR normalmente cerrado»**, y una pieza desoldada no conduce nunca, sea del tipo que sea. No es una rareza: los finales de carrera, las setas de emergencia y los detectores de puerta se cablean NC **a propósito**, para que un cable cortado se vea igual que una pulsación y la máquina pare; descrito como NA, el montaje parece funcionar hasta el día en que se corta el cable. Una palabra que no sea una de las dos es **error de netlist**, no un `abierto` silencioso. **T124**, trece comprobaciones sobre nodos propios del banco —no sobre pines del MCU: lo que se prueba es la pieza, y colgarla de un pad obligaría a tocar la placa para probar un componente— incluyendo que `pressed()` (el dedo) y `cerrado()` (el contacto) son opuestos en un NC |
| **I-36** | ~~**Los pines solo se podían nombrar a la manera de ST**~~ | F3 | **HECHO.** `pad_desde_nombre()` acepta ahora `PD12`, `PD.12`, `P3.12` y `P312`, con prefijo de MCU o sin él. El trabajo de verdad estuvo en que **el punto ya estaba ocupado**: en `u0.PD12` separa el chip del pad y en `P3.12` el puerto del pin, y se distinguen mirando si detrás del último punto solo hay dígitos. La ambigüedad de la forma sin punto se resuelve por **el puerto más pequeño** (`P111` = `P1.11`), que además es estable frente a un chip con más puertos. Y la pieza que evita acordarse en veinte sitios: `nombre_canonico_pad()`, aplicado **a la entrada** —`Instancia::pin`, `nodo_externo`, `nodo_bus`, `nodo_une`, `NodeMap`—, de modo que quien declara `<nodo id="P3.12"/>` y quien conecta `nodo="PD.12"` acaban en la misma clave y los volcados siguen hablando con una sola voz. **T125**, veinte comprobaciones, **sin coste de tiempo simulado**: la función es pura y el invariante no se movió |
| **I-37** | ~~**`--mcu TIPO` existe, pero solo hay un tipo que construir**~~ | multi-MCU §6.1 | **HECHO.** Ya hay once: los once miembros de la familia F405/F407, del `F405RG` en LQFP64 al `F407IG` en LQFP176. Salen de un catálogo (`CATALOGO_MCU` en `top/mcu_caps.h`) que `--mcu`, `<mcu tipo=>`, `--help` y el mensaje de error leen solos, así que un miembro nuevo no obliga a tocar `sim_main.cpp`. Lo que hizo falta para poder hacerlo fue cerrar antes **I-39** (los rasgos como dato) y el obstáculo de multi-MCU §6.3: el encapsulado era una función estática, y sin convertirlo en dato no hay diferencia entre un LQFP64 y un LQFP176. Y lo que **no** se ha hecho, a propósito, es meter un F446 con otros números: un chip de otra familia tiene otro árbol de reloj y otros periféricos, y `mcu_por_nombre` lo rechaza nombrando los que sí hay. Sigue sin haber `FabricaMcu` con auto-registro, y ahora se sabe por qué no hace falta: los once son EL MISMO MODELO con distintos rasgos, no once clases; la factoría hará falta el día que haya otra familia. **Añadido en la fase 1 del plan del F446** (`doc/stm32f4xx/stm32f407vg_vs_446re.md` §16): `soc/mcu_if.h` trae la interfaz `mcu_if` y `FabricaMcu`, un registro `familia → creador` con auto-registro por macro, y `sim_main.cpp` ya no hace `new Stm32F407VG` sino `FabricaMcu::crea(descriptor, ...)`. Se indexa por **familia** y no por nombre de pieza, que es lo que permite que los once compartan un creador; una familia sin modelo enlazado devuelve `nullptr` y `sim` lo dice nombrando las que sí sabe construir, **nunca monta otro chip en su lugar**. **T129**, diez comprobaciones del contrato de la factoría que no construyen ni un MCU, porque la elaboración de SystemC ya ha terminado cuando esa prueba corre |
| **I-38** | ~~**Los componentes del XML no se explicaban solos**~~ | F3 | **HECHO.** `sim --help COMPONENTE` imprime la ficha de cualquiera de los veintiuno: qué hace, sus terminales, sus atributos con el valor por omisión de cada uno, las notas que importan y un `<componente>` de ejemplo que copiar. Lo interesante no es la orden sino **dónde vive el texto**. La tentación era una tabla en el fichero que la imprime, y esa tabla se queda vieja el día que alguien añade un parámetro al creador y no se acuerda de ella — y una ayuda que miente es peor que no tenerla, porque quien la lee ya no vuelve al código. Así que la ficha **viaja con el registro**: `REGISTRA_PARTE` pasa a tener tres argumentos —tipo, ayuda y creador— y el del medio no es opcional, de modo que **no se puede dar de alta una pieza sin explicarla**. No es una convención que haya que recordar; es lo que exige la macro, y eso es lo que responde a «añade esta capacidad a todos los componentes que se añadan en el futuro». Como el compilador no puede juzgar si un texto dice algo, `Fabrica::sin_documentar()` enumera las que se registren con una ayuda de adorno, `--help` avisa si hay alguna y **T126** lo comprueba: veintiuna comprobaciones que **recorren la factoría**, no una lista escrita a mano, y por eso siguen valiendo para las piezas de mañana. De camino, dos cosas pequeñas: la búsqueda de `--help` no distingue mayúsculas —para *preguntar* da igual cómo se escriba; para *describir una placa* no, y eso no cambia— y el error de tipo desconocido dice ahora `Se escribe 'Led': el tipo distingue mayúsculas` cuando eso es lo único que falla. **Sin coste de tiempo simulado**: todo es consulta de un mapa estático, y el invariante sigue en `2336217899213 ps` con 1956/1956 |
| **I-39** | ~~**Las piezas del núcleo y de las memorias estaban soldadas al F407**~~ | reutilizacion | **HECHO.** Casi nada de lo que hay dentro de este modelo es *del F407* —el núcleo es un Cortex-M4F con licencia y el controlador de Flash es el mismo en toda la familia—, pero eso era cierto en el papel y falso en el código: `N_IRQ = 82` era una constante global, `bool enabled_[N_IRQ]` un array de tamaño fijo, la máscara de prioridad un `0xF0` escrito a mano en tres sitios y el tamaño de la Flash un `addr::FLASH_SIZE` leído desde dentro de `FlashIf`. Ahora los rasgos son **datos**: `CoreCaps` (líneas de IRQ, bits de prioridad, regiones de MPU, FPU, CPUID), `MapaFlash`/`MapaRam` (tamaños, tabla de sectores, curva de estados de espera) y `McuCaps`, que los junta con los topes de reloj. Los reciben `Scs`, `Mpu`, `CortexM4F`, `FlashIf`, `AhbMatrix`, `Rcc` y el top, **todos con el F407 por omisión**, de modo que un modelo construido como siempre es el de siempre. Tres cosas que el cambio arregla de camino y no eran el encargo: `MPU_TYPE` lee 0 en un núcleo sin MPU y `CPACR` se queda a cero en uno sin FPU —que es como CMSIS averigua que no los hay—; el aviso del RCC imprime **el tope del chip que se monta** en vez de un «> 168 MHz» a pelo; y la máscara de `SHPR3` dejaba fuera el byte de DebugMonitor, que ARMv7-M §B3.2.10 sí implementa. La frontera está escrita en `McuCaps::familia` y es la parte honesta del punto: **dos chips de la misma familia se distinguen con un descriptor; dos familias distintas necesitan modelo nuevo**, y llamar F446 a un F407 con otros números sería justo la clase de mentira que I-32 e I-33 vinieron a quitar. **T127**, 38 comprobaciones que no miran los `struct` sino que montan un NVIC de 32 líneas con 3 bits de prioridad y una Flash de 256 KB con 6 sectores **como módulos de verdad, al lado del F407 y en la misma simulación**. Sigue soldado, y anotado en la §7 del documento: los números de IRQ de cada periférico. El encapsulado se cerró en I-37 y la tabla de AF salió del cuerpo del top en la fase 1 del plan del F446 —`soc/f4_mapa_af.h`, 169 llamadas movidas verbatim con una definición fuera de clase—, aunque siguen siendo llamadas y no una tabla de datos: el F446 resultó tener la MISMA tabla salvo los huecos del AF11 de Ethernet, así que convertirla en dato dejó de ser urgente. Suite 1993/1993 y el invariante intacto en `2336217899213 ps`; ASan+UBSan limpios |
| **I-40** | ~~**El reparto de bolas del WLCSP90 no está contrastado**~~ | reutilizacion §9.4, vs_446re §15.8 | **HECHO, y menos mal que estaba marcado.** El aviso decía que el recuento (72 E/S) estaba verificado y el reparto bola a bola no. Al abrir el DS8626 entero —figura 17, el diagrama de bolas— y contrastarlo con `STM32_open_pin_data` de ST, resultó que **la reconstrucción era falsa en casi todo**: al puerto C le faltan tres bolas sueltas (PC1, PC4, PC5), al D le faltan dos (PD3, PD13), del E sale **la mitad alta** `PE7..PE15` y no la baja, y **aparecen PI0 y PI1**, que ni se contemplaban. El recuento cuadraba **por casualidad**: 72 bits mal repartidos siguen siendo 72. Es la demostración de por qué `Encapsulado::coherente()` no basta y por qué el `verificado = false` tenía que existir — con el mapa falso, `sim` aceptaba en silencio un LED en PC4, que en ese encapsulado no hay dónde soldar. Las dos fuentes de ST coinciden puerto a puerto (16/16/13/14/9/2/2), así que ahora va `verificado = true` y el aviso desaparece. **El detalle que más sorprende: PI0 y PI1 salen en un encapsulado de 90 bolas y NO en el LQFP144 de 144 patillas**; no es que a más patillas, más E/S. Tres comprobaciones nuevas en **T128**, y un efecto inmediato que se ve solo: la placa Discovery **no cabe en un WLCSP90** porque le falta PD13, el pin del LED naranja |
| **I-41** | ~~**El SysTick no interrumpía NUNCA a las frecuencias cuyo tick no cae en un número entero de picosegundos**~~ | F2/F6, vs_446re §17.3 | **HECHO, y no era del chip nuevo.** Apareció montando el blinky del F446 a 84 MHz: el contador corría —`SysTick->VAL` se movía—, `CTRL` valía 7 y `LOAD` 83 999, y la interrupción no llegaba jamás; el mismo firmware sobre el modelo del **F407** hacía exactamente lo mismo, así que el fallo llevaba dentro desde la fase 6. La causa: `tick_proc()` esperaba **N veces el periodo de un tick redondeado a picosegundos**, y un tick a 84 MHz dura 11 904,7619… ps; redondeado hacia arriba y multiplicado por 84 000, el contador queda **pasado** del cero, la condición de disparo —«¿vale exactamente cero?»— no se cumple nunca y el bucle vuelve a esperar otra vuelta entera para pasarse otra vez. Por qué no se había visto: las dos frecuencias que usa la suite redondean hacia ABAJO (168 MHz, 5 952,38 ps) o caen justas (16 MHz, 62 500), y por abajo el bucle converge en dos vueltas. **84 MHz no es un caso raro: es la mitad de 168 y es lo que sale de un PLL con P = 4.** La corrección calcula el **instante absoluto** del cruce desde la base del contador en vez de sumar periodos, y dispara cuando **han pasado** los ticks que faltaban en vez de exigir el cero exacto. El invariante del F407 no se mueve ni un picosegundo, porque a 168 MHz el camino es el mismo. La regresión la cubre la suite del F446, que corre su blinky justo a 84 MHz |
| **I-42** | ~~**Una prueba del I2S depende del reloj de PARED**~~ | F5 (SPI/I2S), vs_446re §20.4 | **CERRADA EN LA FASE 5, y el diagnostico estaba equivocado.** La anotacion decia que la prueba cambiaba de resultado segun lo rapido que fuera la maquina, porque al construir un segundo chip en el banco del F407 la simulacion se volvio trece veces mas lenta y los contadores pasaron de «23 muestras (21 correctas)» a «20 (18)». **No es el reloj de pared**: la misma suite compilada con ASan y UBSan -varias veces mas lenta- da los MISMOS numeros y el MISMO picosegundo. Lo que cambio al anadir un chip no fue la velocidad sino **el orden en que SystemC despierta los procesos**, que depende de cuantos modulos hay en la simulacion. La distincion importa porque cambia que hay que arreglar, y de paso deja ver que la consecuencia era menor de lo que la anotacion daba a entender: las comprobaciones del T53 son por RANGO, de modo que el **veredicto** nunca cambio; lo que se movia eran los numeros impresos. Lo que si faltaba era comprobar lo que NO puede moverse, y ahora esta: *un enlace de audio que funciona no pierde muestras* (`got3 == sent-1` y `gotx == sent-1`), sin gastar un picosegundo porque son variables ya contadas. Y las dos muestras «incorrectas» de las veintitres tambien estan explicadas: son **ceros del arranque del enlace** -el esclavo engancha el reloj en cuanto el maestro mueve CK y WS, y durante las primeras tramas la linea de datos todavia es silencio-, no datos corrompidos |
| **I-43** | ~~**La mitad baja de `RCC_CIR` estaba desplazada un bit**~~ | F1 (RCC), vs_446re §18.3 | **HECHO, y es el segundo fallo del F407 que destapa el puerto del F446.** El modelo ponía `LSIRDYF` en el bit 1, los `xxxRDYIE` en 9..14 y los bits de limpieza en 17..22, copiando fielmente la tabla de `[IR, §4.6]`. **Esa tabla está mal**: la cabecera de ST —`RCC_CIR_LSIRDYF_Pos`, `RCC_CIR_LSIRDYIE_Pos` y `RCC_CIR_LSIRDYC_Pos`, idénticas en `stm32f407xx.h` y en `stm32f446xx.h`— los pone en **0**, **8** y **16**. Apareció al colocar `PLLSAIRDYF`, que es del F446 y obligó a mirar dónde estaban sus vecinos. Lo que lo hizo sobrevivir es que los DOS EXTREMOS del registro sí estaban bien —`CSSF` en el 7 y `CSSC` en el 23— y son los únicos que la suite comprobaba: la prueba del CSS pasaba, y con ella parecía que el registro entero estaba bien. La consecuencia para un alumno era de las que no se diagnostican: habilitar `RCC_CIR_HSERDYIE` con la constante de CMSIS y no recibir nunca la interrupción. Corregido, con una comprobación nueva en **T22** que mira en qué bit cayó el uno de `HSERDYF` y que **no cuesta tiempo simulado** —los flags de RDY son pegajosos—, de modo que el invariante no se mueve. Queda también anotado que la tabla del informe interno tiene ese error, para quien la consulte |
| **I-44** | ~~**Las máscaras de `RCC_xxxENR`/`RSTR`/`LPENR` estaban escritas a mano, y tres estaban mal**~~ | F1 (RCC), vs_446re §19.3 | **HECHO, y es el TERCER fallo del F407 que destapa el puerto del F446.** Las veinte máscaras «qué bits existen en este registro» venían de la fase 1, copiadas a mano. Al necesitar las del F446 se extrajeron **contando los `RCC_xxx_yyy_Pos` de las cabeceras de ST** —`stm32f407xx.h` y `stm32f446xx.h`, las dos vendidas en `verif/fw/cmsis/`— y, puestas las dos columnas una al lado de otra, saltaron tres: (1) **`RCC_AHB1ENR` usaba la máscara del `AHB1LPENR`**, `0x7E6791FF` en vez de `0x7E7411FF` —dejaba encender bits que no existen y prohibía los dos del OTG HS—; (2) **`RCC_AHB2ENR`** admitía los bits 4 y 5, que son el CRYP y el HASH de un **F417**; (3) y la primera versión de la fase 4 se dejó fuera **el bit 16 del `APB1ENR` del F446, que es el SPDIF-RX**: el bloque estaba en su sitio, con su ventana decodificada, y su reloj no se podía encender. Los tres son de la misma familia que el SysTick (I-41) y el `RCC_CIR` (I-43): **el modelo era más permisivo que el silicio**, que es la peor clase de error que puede tener un simulador didáctico —el alumno enciende el reloj de algo que su chip no lleva, se lo lee de vuelta y se lo cree—. Corregido con una tabla por familia (`Rcc::MascarasRcc`) cuyos veinte números salen de las cabeceras, y con comprobaciones en **T02** y en el grupo **E1** del banco del F446 que **no cuestan tiempo simulado**: preguntan `bits_implementados()` en vez de escribir unos por el bus. El invariante no se mueve |
| **I-45** | ~~**El AF11 del Ethernet seguia registrado en el mux de los chips que no llevan Ethernet**~~ | F1 (mapa AF), vs_446re §20.2 | **HECHO, y es el CUARTO fallo que destapa el puerto.** `bind_mapa_af()` registraba las dieciocho entradas del MAC sin preguntar si el chip lo lleva. El modulo del Ethernet se construye siempre -la elaboracion de SystemC es estatica- y su ventana de bus no la decodifica nadie en un F405 ni en un F446, pero poner `AFR = 11` en PA2 de un F446 conectaba el pad a un periferico que ese chip NO TIENE. El documento de comparacion afirma en §9.2 que el AF11 es el unico numero de funcion alternativa que el F446 vacia entero; la prueba cruzada de la fase 5 lo comprobo por primera vez y resulto ser falso. Corregido con un `if (mcu.perif.eth)`, lo que **arregla tambien los dos F405**, que llevaban lo mismo desde la fase 1. El invariante no se mueve |
| **I-46** | ~~**La ventana de la CCM contestaba en un chip sin CCM**~~ | F1 (matriz), vs_446re §20.2 | **HECHO.** `decodifica_mapa()` mandaba `0x1000_0000` al esclavo de la Flash cuando `ccm_size` era cero: el netlist decia que ahi contesta una memoria. La prueba que habia (A2 del banco del F446) solo miraba que no devolviera el codigo `-2`, el de la CCM, y por eso pasaba. Ahora un chip sin CCM deja esa ventana SIN DECODIFICAR. **Queda anotado y no arreglado** que el RESTO de la region de codigo por encima de la CCM y por debajo de la memoria de sistema tambien esta reservado en las dos piezas y el modelo lo sigue mandando al esclavo de la Flash, que lo rechaza: el efecto visto desde el firmware es el correcto -error de bus- y la etiqueta del netlist no lo es. Arreglarlo es recortar la region de codigo entera |
| **I-47** | ~~**El mapa de canales de DMA del F446 no esta modelado**~~ | F4 (DMA), vs_446re §21 | **HECHO EN LA FASE 6, con dos fuentes de ST que coinciden celda a celda.** La fase 5 decidio no inventar la tabla porque no encontraba una fuente de ST legible por maquina; estaba en **la base de datos de STM32CubeMX instalada en la maquina**, que es de donde el repositorio publico `STM32_open_pin_data` -el `[PINDATA]` de la fase 4- saca su subconjunto de pines. Un fichero por IP: `DMA-STM32F417_dma_v2_0_Modes.xml` para el F407VG y `DMA-STM32F446_dma_v2_0_Modes.xml` para el F446RE, que es lo que el `Version=` de cada descriptor nombra. Hay que cruzar dos cosas -el `RefMode` da el CANAL de cada peticion y el arbol de `Mode` da el STREAM-, asi que las 128 celdas se extrajeron por maquina. **Contrastadas con las Tablas 28 y 29 de [RM0390] Rev 9, leidas de la pagina del PDF: las 128 coinciden.** Importa porque un mapa de DMA mal copiado no falla, no avisa y no transfiere. Cableadas las celdas del FMPI2C1, los dos SAI, el SPI4 y el QUADSPI -que de paso gano su `CR.DMAEN` y su linea de peticion-, y dejadas sin fuente a proposito las dos del SPDIF-RX, cuyo bloque esta declarado y no modelado. La prueba que lo cierra es el grupo H5 del banco del F446: una transferencia entera por la celda del SAI1_A, y el mismo montaje con `CHSEL` cambiado, donde no se mueve un dato |
| **I-48** | ~~**Cinco celdas de DMA de los I2SxEXT estaban al aire en el F407**~~ | F4 (DMA), vs_446re §21.3 | **HECHO, y no lo destapo el F446 sino la fuente que trajo.** El codigo las tenia sin cablear con un comentario honesto -«[IR] no recoge las celdas de DMA de los bloques de extension del I2S»- desde que se escribio el modelo del DMA. La base de datos de ST SI las trae: `I2S3ext_RX` en DMA1 0/3 y 2/2, `I2S2ext_RX` en 3/3, `I2S2ext_TX` en 4/2 e `I2S3ext_TX` en 5/2. Cableadas, con una comprobacion en **T26** que no cuesta tiempo simulado. La misma comprobacion fija lo contrario para el CRYP y el HASH -DMA2, canal 2 de los streams 5, 6 y 7-, que siguen sin fuente A PROPOSITO porque son de un F415/F417 y este chip no lleva el acelerador |
| **I-49** | **El bit de reloj del FSMC existe en un encapsulado cuyo bus externo el modelo declara ausente** | F1 (RCC) / F7 (FSMC), vs_415xx §15.4 | **ABIERTO, y lo destapa la fase 1 del plan del F415/F417.** Al pasar las mascaras de `RCC_xxxENR` de ser por familia a ser por referencia se quitaron los bits del Ethernet, la camara, el CRYP, el HASH y el RNG, cada uno con su fuente: la cabecera de ST de esa referencia. **Con el FSMC no se pudo**, porque su ausencia en el LQFP64 no es un rasgo de la referencia sino del ENCAPSULADO -no hay pines donde sacar el bus- y ST no publica una cabecera por encapsulado. Queda por tanto una incoherencia dentro del modelo: en un `STM32F405RG` la **ventana** del FSMC esta sin decodificar -tocarla da error de bus, que es lo que `Periferia::fsmc = false` significa hoy- y a la vez su **bit de reloj** `RCC_AHB3ENR.FSMCEN` se puede encender. Las dos cosas no pueden ser verdad a la vez. Las dos salidas son legitimas y ninguna esta verificada: (a) el bloque existe en el die y lo que falta son pines, con lo que el bit se queda y **la ventana deberia contestar**; (b) ST no lo vende con FSMC y entonces se van los dos. No se decide a ojo: lo cierra el RM0090 §37 o una medida sobre una placa con un F405RG. Mientras tanto queda escrito, que es lo que se hace con lo que no se sabe |
| **I-10** | **Referencia cruzada errónea en el informe de F1 §1**: la fila del RCC dice "Completo salvo lo eléctrico (**ver §6**)", pero el contenido pendiente está en **§9** | F1 | Errata documental |

---

## 9. Límites del encapsulado, ya modelados

No son deuda: son propiedades del LQFP100 que el modelo **reproduce y verifica**.
Se recogen aquí porque condicionan cualquier trabajo futuro sobre esos bloques.

- **GPIOF..GPIOI existen en el mapa de registros pero no tienen pad**: accesos
  válidos, sin pin físico (decisión del plan, P1).
- **DCMI**: `D12` y `D13` no existen. `EDM = 11` se escribe y el DCMI captura
  sin protestar; los dos bits altos de cada píxel valen siempre cero. **No hay
  bandera de error ni aviso** — el modelo añade un `SC_REPORT_WARNING`, "que es
  más de lo que hace el silicio". Verificado en T107.
- **FSMC**: faltan `A0–A15`, `NE2/NE3/NE4` y todo el banco 4. Consecuencia: **el
  bus externo solo se puede usar multiplexado**; sin multiplexar, `0x6000_0100`
  y `0x6000_0200` son la misma celda (T111). Y **PD7 es a la vez NE1 y NCE2**:
  una SRAM y una NAND **no pueden convivir** en el bus.
- **TIM8**: las salidas complementarias en pines de los puertos E/H no están.
- **ADC3**: canales IN4–IN9, IN14 e IN15 sin pin.
- **OTG_HS / ETH**: los pines existen todos, pero **están todos cogidos**.
  `ULPI_D4/D5/D6` son `ETH_RMII_TX_EN/TXD0/TXD1` (con MII el solape sube a ocho
  de doce): **Ethernet y USB de alta velocidad por PHY externo no caben juntos**
  — conclusión confirmada desde los dos lados. Con ULPI, el OTG_HS **se queda
  sin pin ID y sin sensado de VBUS** (PB12 y PB13 doblan función). `ULPI_CK` es
  `DAC_OUT2` (PA5). Y **`ETH_MII_CRS` es `PA0-WKUP`**: cablear MII **cuesta el
  pin de despertar desde Standby**, y no hay otro.

---

## 10. Propuesta de orden de cierre

Ordenado por relación entre valor y coste, no por fase.

### Bloque 1 — Barato y desbloquea a otros

1. **P-07** (reenvío del `ack` del DMA) → cierra la limitación conocida de
   F4-TIM §4.4 y permite retirar **I-01** (`tb_set_request`).
2. **I-04** (actualizar el `TODO` obsoleto de la tabla AF) y **I-10** (errata de
   referencia cruzada): son dos líneas.
3. **P-11** (llamar a `total_pin_current()` desde una comprobación automática):
   la función ya existe.
4. **P-09** (IRQ 81 de la FPU) y **P-08** (bits de opción → `hw_start` del IWDG).
5. **F-20** (exportar las líneas del EXTI como disparo) → desbloquea el disparo
   por EXTI del ADC y del DAC de una vez.
6. **F-19** (exportar el evento CC en crudo del temporizador) → cierra los
   huecos de `EXTSEL`/`JEXTSEL` del ADC.

### Bloque 2 — Decisiones pendientes de tomar

7. **P-06** (espejo de 0x0 para maestros ajenos al núcleo): antes que
   implementar, hay que **decidir** qué debe verse. Es el punto que se perdió
   entre F1 y F3.
8. **X-04** (`FP_REMAP`): la contradicción sigue abierta; conviene resolverla
   contra RM0090 y cerrar la nota.
9. **I-06** (SD I/O y MMC en `ext_parts.h`): desbloquea F-25, F-26 y F-27 de un
   golpe.

### Bloque 3 — El grande

10. **P-01** (LT → AT), que arrastra **P-04** y **P-10**, y que es condición
    previa para que **P-02** y **P-03** tengan sentido.

### Bloque 4 — Solo si el proyecto lo pide

Todo el resto de la sección 3: son caminos que ningún firmware corriente usa y
que están correctamente representados a nivel de registro. Antes de abordar
cualquiera de ellos conviene preguntarse **qué firmware lo va a ejercitar**,
porque en casi todos los casos la respuesta ha sido, hasta ahora, ninguno.

---

## 11. Recuento

| Categoría | Puntos |
| :--- | ---: |
| **P** — Pendientes de plan | 12 |
| **F** — Funciones no modeladas | 51 |
| **T** — Temporización y física | 21 |
| **D** — Datos sin fuente | 14 |
| **X** — Discrepancias, silencios de [IR] y erratas de ST | 14 |
| **V** — Huecos de verificación | 10 |
| **I** — Deuda de instrumentación y proyecto | 47 *(veintiocho cerradas: I-11, I-12, I-15, I-16, I-17, I-20, I-22, I-25, I-28, I-30, I-31, I-32, I-33, I-34, I-35, I-36, I-37, I-38, I-39, I-40, I-41, I-42, I-43, I-44, I-45, I-46, I-47 e I-48)* |
| **Total** | **169** |

De los 169, **uno solo** (P-01) es un pendiente de plan de primer orden; **once**
son trabajo acotado y barato (bloque 1 y 2 de la sección 10); y **la gran
mayoría** son decisiones conscientes de alcance, cada una con su motivo escrito
en el informe que la originó.

> **Dos correcciones del recuento, dichas en vez de calladas.** Al revisar esta
> tabla para dar de alta lo del F415/F417 se contaron las filas una a una y
> salieron dos errores propios: (1) el **total no sumaba** —la columna daba 162
> y la casilla decía 159—, y (2) la fila de la **I** contaba hasta el
> identificador más alto, no las filas que hay: **I-18 e I-19 no existen**, así
> que son 47 puntos y no 49, y la lista de cerradas nombraba un I-18 inexistente.
> Ahora el total **es la suma de la columna**: 12 + 51 + 21 + 14 + 14 + 10 + 47.
