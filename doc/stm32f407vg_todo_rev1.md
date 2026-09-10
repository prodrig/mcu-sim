# Trabajo pendiente del modelo SystemC del STM32F407VG — revisión 1

Inventario consolidado de **todo lo que las siete fases han ido dejando fuera**,
extraído de los veintidós informes de `doc/` y contrastado contra el código de
`src/`. Fecha de corte: fase F7 cerrada, **1811/1811 comprobaciones, 0 fallos**.

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
| **X** | Discrepancia con el informe técnico | [IR] se contradice, omite o choca con RM0090 |
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
- **Cuarenta y ocho funciones "bits sin máquina"**: registros que se guardan, se
  enmascaran y se leen correctamente, pero cuya lógica no se ejecuta. Casi todas
  corresponden a caminos que ningún firmware corriente usa, y casi todas están
  bloqueadas por la falta de un dispositivo externo que las ejercite.
- **Ocho discrepancias con el informe técnico [IR]** —más cuatro silencios que
  hubo que resolver por decisión propia—. Tres son contradicciones internas del
  propio informe y dos son omisiones que hubo que rellenar desde RM0090. Siete
  están resueltas de forma documentada y reversible; **X-04 (`FP_REMAP`) sigue
  abierta**, con la contradicción escrita en un comentario del código.
- **Trece parámetros sin fuente documental**, todos expuestos como variables
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

---

## 6. Discrepancias con el informe técnico [IR] (X)

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
| **I-07** | **`Makefile.stm32` no se llama `Makefile`** porque el puente remoto no permite escribir ese nombre; se sugería renombrarlo | F0 | Divergencia con lo documentado en el plan (§5: "`src/ Makefile, README.md`") |
| **I-08** | **Restricciones de entorno**: la biblioteca SystemC de Ubuntu está construida con **C++17** y el estándar debe coincidir o falla el enlazado (`sc_api_version...`); `simple_target_socket_optional` exige **SystemC ≥ 2.3.3** | F0 | Requisitos mínimos a dejar escritos en el README |
| **I-09** | **Carpetas `_to_delete/git-tmp*` acumuladas en el repositorio** por los restos de bloqueo de git del puente remoto | — | Limpieza pendiente; no se pueden borrar desde el contenedor |
| **I-11** | ~~**Paso 2: `Netlist` en memoria**~~ | — | **HECHO Y CERRADO.** `parts/netlist.h` + `parts/netlist_parts.h`; **la placa entera** —43 piezas de 20 tipos— se declara y la construye el netlist, y no queda ni un `new` de pieza externa en `sc_main.cpp`; 29 comprobaciones nuevas (T121). El ejercicio destapó tres cosas: los nodos son del circuito y no del componente (`CanWire` tenía el suyo), no todo lo que une dos componentes es un nodo (de ahí `Instancia::refs`) y la ida y vuelta necesita las dos direcciones. De paso se cerró una fuga de F5 (`can_bus`, `xcvr1`, `xcvr2` y `nodo_ext` no se destruían) y se evitó un uso después de liberar en el destructor. Véase `doc/stm32f407vg_parts_paso2.md` |
| **I-12** | ~~**Paso 3: lector de XML y validador eléctrico**~~ | — | **HECHO.** `parts/part_factory.h` (registro cadena→creador con auto-registro), `parts/xml_min.h` (lector de XML estricto, sin dependencias), `parts/netlist_xml.h` y el ejecutable `./build/sim placa.xml firmware.bin`. La validación eléctrica encontró **siete conducciones simultáneas ciertas** en la placa del banco —dos buses I2C reales, el cable en Y del CAN y cuatro pines compartidos entre grupos de prueba— que vivían implícitas en `i2c_bus()`, `adc_links()` y compañía; declararlas dejó la placa en 0 avisos. El cortocircuito PA2/PB11 de F7-ETH está reconstruido en T121 y salta **antes de simular**. Véase `doc/stm32f407vg_parts_paso3.md` |
| **I-13** | **Paso 4: generador de SVG desde el netlist**, con `id` estables por componente y por nodo, y opcionalmente coloreado desde una traza | — | **Pendiente, y ya tiene de dónde salir**: el netlist lleva nodos, terminales, parámetros, referencias y la marca de qué nodos hay que crear |
| **I-14** | **La validación eléctrica avisa por conducción simultánea, no por tensiones incompatibles.** Dos piezas que conducen el MISMO nivel sobre un nodo no son un cortocircuito, y hoy se avisa igual | — | **Falso positivo conocido y acotado.** Distinguirlo exigiría que cada terminal declarase qué tensión y qué impedancia presenta, información que hoy solo existe dentro del constructor de cada pieza. Se calla declarando el nodo con `nodo_bus()`, y declararlo documenta |
| **I-15** | ~~**Nodos compartidos entre pines: cada pad creaba su `AnalogNet` y lo ataba a un `sc_port`**~~ | multi-MCU §4.3 | **HECHO.** `Cableado` en `pins/pin_mux.h`, el atributo `une=` en el XML, `Netlist::nodo_une()` y `cableado_desde_netlist()`. Un pad solo deja de crear su nodo si un `<nodo … une="…">` lo nombra, así que el coste para una placa sin puentes es cero. El banco lleva el puente PB9–PD3 y **T122** lo comprueba, incluido lo que ninguna pista (`SignalLink`) puede dar: con los dos pines conduciendo, el nodo se queda a 1,65 V y los dos pads avisan de sobrecorriente. Véase `doc/stm32f407vg_multi_mcu.md`, §4.3 y §4.5 |
| **I-16** | ~~**El volcado del inventario daba nombres de nodo ambiguos** (`basename()` descarta la jerarquía) y **`registra_mcu()` sin prefijo se pisaba a sí mismo en silencio**~~ | multi-MCU §7.1 y §7.2 | **HECHO.** `common/nombres_nodo.h` parte del nombre jerárquico y cualifica el pad con su MCU cuando hay más de uno; `registra_mcu(prefijo, …)` y un `SC_REPORT_ERROR` en `NodeMap::registra()` cuando un nombre se da de alta dos veces con dos `AnalogNet` distintos. Con un solo MCU el volcado sale idéntico al anterior, que era la condición para hacerlo sin migrar nada |
| **I-17** | ~~**Dos fallos mudos que solo se manifiestan con DOS MCUs**: los `static bool warned` de `periph/adc.h` y `periph/sdio.h` y el puerto de GDB único~~ | multi-MCU §7.3 y §7.4 | **HECHO**, al mismo tiempo que I-20, que es cuando su síntoma pasó de imposible a inevitable. Los dos avisos son ahora miembros `mutable` (`aviso_adcclk_`, `aviso_ck_`), así que cada chip avisa de lo suyo; el puerto sale del atributo `puerto_gdb` de cada `<mcu>`, y `DebugCaps` ya lo llevaba por instancia, de modo que el modelo no hubo que tocarlo |
| **I-20** | ~~**Varios MCUs en una placa, con un stub de GDB por chip**~~ | multi-MCU §5 | **HECHO.** El elemento `<mcu tipo id firmware depuracion puerto_gdb>` en `parts/netlist.h` y `parts/netlist_xml.h`; `sim` monta uno o varios chips, cada uno con su firmware, su modo de depuración (`pines` o `dap`) y su puerto TCP, y con algún stub escuchando no se detiene solo. La regla de compatibilidad —ningún `<mcu>`: nombres desnudos; uno: los dos nombres y mandan los argumentos de la línea de órdenes; dos o más: solo cualificados y un argumento global se rechaza nombrando los MCUs— deja valer sin tocar todo lo escrito hasta hoy. `placas/dos_mcu.xml` son dos F407 hablando por I2C y depurables a la vez en 3333 y 3334; **T123** cubre la capa de declaración con 28 comprobaciones |
| **I-21** | **No hay comprobación automática del MONTAJE de varios MCUs.** T123 cubre la declaración —leer `<mcu>`, resolver `u0.PD12`, rechazar lo ambiguo, el chip inexistente y el puerto repetido—, pero que dos chips se construyan, arranquen y se hablen solo se verifica a mano corriendo `sim` sobre `placas/dos_mcu.xml` | multi-MCU §8, paso 6 | **Hueco conocido.** La suite monta un único `dut` del que cuelgan la mitad de sus 1899 comprobaciones, y meter un segundo dentro sería duplicar la elaboración del banco entero para probar otra cosa. El sitio natural es un banco aparte que ejecute `sim` sobre las placas de `placas/`, y no existe |
| **I-22** | ~~**El modelo solo compilaba en Linux**~~ | analisis_gui §19.3 | **HECHO A MEDIAS, y lo que falta está acotado.** Toda la dependencia del sistema operativo se ha recogido en `common/red.h` —traducción entre sockets de Berkeley y Winsock, más `SO_NOSIGPIPE` para macOS— y ningún otro fichero de `src/` incluye ya una cabecera del sistema. El `Makefile.stm32` es único para las tres plataformas, con detección automática y `PLATAFORMA=` para forzarla, sufijo `.exe`, `-lws2_32`, `-D__USE_MINGW_ANSI_STDIO=1` (MinGW no entiende `%llu` sin él, y el modelo lo usa 28 veces) y enlazado estático de las DLL de MinGW. `verif/prueba_red.cpp` (`make red`) ejercita la capa con 13 comprobaciones sin necesitar SystemC. **Verificado**: Linux con g++ y con clang, 1899/1899 y el mismo tiempo simulado; el cruce a MinGW-w64 compila y enlaza un PE32+ sin un aviso; la rama de macOS compila forzando su combinación de macros. **Falta**: construir SystemC para MinGW y para macOS, y ejecutar allí |
| **I-23** | **Sin verificación en Windows ni en macOS.** El cruce con MinGW demuestra que el código compila y enlaza, no que funcione; y de macOS solo se ha compilado la rama específica | analisis_gui §19.3 | **Hueco conocido.** Lo que falta es tener la biblioteca de SystemC en las dos plataformas y pasar allí `make red` y `make test`. Es el paso 0b del orden de trabajo del análisis de la GUI, y para un programa que se reparte a alumnos no es opcional |
| **I-24** | **Se sigue en SystemC 2.3.4 (IEEE 1666-2011) pudiendo estar en la línea 3.0 (IEEE 1666-2023)** | analisis_systemc3 | **Decisión consciente, no olvido.** El inventario de la API que el modelo usa —`sc_module`, procesos, señales, `sc_prim_channel`, `sc_spawn` y TLM-2.0, nada más— es tan central que no se espera ningún cambio, y el único `#include` de cabeceras internas de SystemC ya se ha eliminado. Pero **ninguna ventaja conocida de la 3.0 ataca un problema que tengamos** —el coste lo domina el número de despertares, no el planificador— y sí tiene un coste: hoy la 2.3.4 se instala con `apt` y la 3.0 habría que compilarla. **La pregunta está subordinada a I-23**: quien construya SystemC para Windows y macOS que pruebe las dos versiones a la vez |
| **I-25** | **`sim` no tiene freno de tiempo real, y la única forma de que no termine es abrir un puerto TCP.** Con un stub escuchando la simulación corre indefinidamente —que es lo que se quiere— pero **a toda velocidad: medido, 98,6 % de un núcleo** esperando sin firmware. Y no hay manera de dejarlo andando *sin* abrir el puerto | analisis_gui §16 | **Hueco conocido, con dos caras.** El freno es la cara didáctica: un alumno que deje el simulador esperando a que STM32CubeIDE se enganche está quemando un núcleo y la batería, y además el tiempo simulado se le va mucho más rápido que el suyo, lo que hace inútil cualquier temporización que quiera observar «en vivo» (un LED que parpadea a 1 Hz simulado pasa 200 veces por segundo). La otra cara es que «correr sin parar» y «aceptar un depurador» son hoy la misma opción, cuando son dos cosas. Lo natural es una opción de tiempo real (`--tiempo-real`, atando el avance simulado al reloj de pared) y otra de duración (`--ms=0` o `--sin-fin`), y las dos son baratas: el sitio es el mismo bucle de `top/sim_main.cpp`. **Es requisito de la GUI**, que necesita las dos: sin freno no hay nada que mirar, y sin correr indefinidamente no hay sesión |
| **I-26** | **`sim` acepta como firmware cualquier argumento que no reconozca**, incluida una opción mal escrita: `sim placa.xml --netlist` intenta cargar un fichero llamado `--netlist` y muere con «no se puede cargar --netlist» | — | **Errata de usabilidad, barata.** `--netlist`, `--inventario` y `--valida` son opciones de `stm32f407vg` y solo la última existe en `sim`, así que confundirlas es lo esperable. Bastaría rechazar todo argumento posicional que empiece por `-` remitiendo a `--help` |
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
| **P** — Pendientes de plan | 11 |
| **F** — Funciones no modeladas | 48 |
| **T** — Temporización y física | 21 |
| **D** — Datos sin fuente | 13 |
| **X** — Discrepancias y silencios de [IR] | 12 |
| **V** — Huecos de verificación | 9 |
| **I** — Deuda de instrumentación y proyecto | 26 *(ocho cerradas: I-11, I-12, I-15, I-16, I-17, I-18, I-20 e I-22)* |
| **Total** | **140** |

De los 140, **uno solo** (P-01) es un pendiente de plan de primer orden; **once**
son trabajo acotado y barato (bloque 1 y 2 de la sección 10); y **la gran
mayoría** son decisiones conscientes de alcance, cada una con su motivo escrito
en el informe que la originó.
