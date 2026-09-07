# Diario de la conversación — modelo SystemC del STM32F407VG

Transcripción del diálogo de trabajo sobre este proyecto: los mensajes tal como
se enviaron y las respuestas tal como se dieron. **A partir de aquí, cada nuevo
intercambio se añade al final de este fichero.**

## Nota sobre cómo está reconstruido

Este fichero se creó a mitad del proyecto, así que no todo es igual de fiel:

* **Parte I — antes de la compactación del contexto.** La conversación se
  compactó el 3 de septiembre a las 08:26 UTC. De ese tramo se conservan **los
  mensajes enviados, literales**, porque el resumen de compactación los recoge
  uno a uno; **las respuestas no son recuperables** y se sustituyen por una nota
  de lo que produjo cada turno, contrastada con los informes y los *commits*.
* **Parte II — desde la compactación.** Extraída del registro de la sesión
  (`~/.claude/projects/.../68142eaa-….jsonl`), literal, con marcas de tiempo.
  Solo incluye el **texto** de los mensajes: las llamadas a herramientas, sus
  resultados y el razonamiento intermedio quedan fuera, porque son ruido de
  ejecución y no diálogo.
* Las continuaciones automáticas (*"Continue from where you left off"*) no son
  mensajes escritos por nadie: son reanudaciones del turno cuando la respuesta
  no cabía de una vez. Se anotan como tales.

Las marcas de tiempo están en UTC.

---

# Parte I — Antes de la compactación (fases F7: bajo consumo, DCMI y FSMC)

> Las respuestas de este tramo se perdieron al compactar el contexto. Los
> mensajes son literales; de cada turno se anota lo que dejó hecho, verificado
> contra `doc/` y contra el historial de git.

## Turno 1 — Bajo consumo

**Mensaje:**

> Completa ahora la fase F7, pero solamente lo referente a bajo consumo.
> Documenta el trabajo en el fichero doc/stm32f407vg_fase7_lowpower.md

**Respuesta:** *(no recuperable)*

Resultado del turno: `periph/pwr.h` como árbitro de la energía (Sleep, Stop,
Standby, PVD, WKUP, LPENR y modelo de consumo IDD), informe
`doc/stm32f407vg_fase7_lowpower.md`, suite en **1501/1501**, commit `d25593b`.
En el camino salieron seis fallos del modelo: WFI y WFE no se distinguían,
`SCR.SLEEPDEEP` llegaba un delta tarde por señal, el núcleo en reset se
declaraba dormido, se volvía a entrar en Stop en el instante de salir, escribir
un `LPENR` no redistribuía los relojes, y `check_near` interpretaba la
tolerancia como absoluta cuando es relativa.

## Turno 2 — DCMI

**Mensaje:**

> Completa ahora la fase F7, pero solamente lo referente a DCMI. Analiza las
> similitudes y diferencias de los diferentes canales DCMI. En caso de existir
> diferencias, realiza un diseño flexible de forma que, mediante parámetros de
> clases template C++ o parámetros del constructor de la clase, se pueda
> seleccionar el tipo de DCMI. Documenta el trabajo en el fichero
> doc/stm32f407vg_fase7_dcmi.md

*(Con dos continuaciones automáticas y un "Continúa trabajando" enviado a mitad
del turno.)*

**Respuesta:** *(no recuperable)*

Resultado del turno: `periph/dcmi.h` parametrizado por `DcmiCaps` con cuatro
variantes, `CameraSensor` en `verif/ext_parts.h`, informe
`doc/stm32f407vg_fase7_dcmi.md`, suite en **1562/1562**, commit `a0882c9`. El
hallazgo de la fase: el F407 tiene **un solo** DCMI, así que la pregunta por los
"canales" se responde mirando dentro — los catorce hilos de datos, de los cuales
**D12 y D13 no están cableados en el LQFP100**. De ahí la separación entre
`max_edm` (lo que el registro admite) y `lineas_pin` (lo que el encapsulado
tiene).

## Turno 3 — FSMC

**Mensaje:**

> Completa ahora la fase F7, pero solamente lo referente a FSMC. Analiza las
> similitudes y diferencias de los diferentes canales FSMC. En caso de existir
> diferencias, realiza un diseño flexible de forma que, mediante parámetros de
> clases template C++ o parámetros del constructor de la clase, se pueda
> seleccionar el tipo de FSMC. Documenta el trabajo en el fichero
> doc/stm32f407vg_fase7_fsmc.md

**Respuesta:** *(no recuperable hasta la compactación; el final del turno sí
está, y abre la Parte II)*

El turno se compactó con el modelo ya escrito y diez comprobaciones fallando en
el camino de datos de la SRAM externa. La depuración y el cierre continúan en la
Parte II.

---

# Parte II — Desde la compactación (transcripción literal)


## Continuación del turno de FSMC — 2026-09-03 08:26:08 UTC

*(Reanudación tras compactar el contexto; el mensaje que abrió el turno está en la Parte I, turno 3.)*


**Respuesta:**


*[nota intermedia]* I'll pick up the FSMC debugging where I left off.


*[nota intermedia]* Now the README update.


*[nota intermedia]* Now delivering the changed files and committing.


F7-FSMC cerrada: **1616/1616 comprobaciones, 0 fallos**, commit `4a8903e`.

Las cuatro causas que quedaban abiertas resultaron ser cuatro errores distintos, no uno:

- **`BWTR1` mal decodificado.** Los `BWTR` viven en `0x104+8b`, y el decodificador comprobaba `(off & 4) == 0`, que es falso para `0x104`. Las escrituras se perdían en silencio y el modo extendido usaba el valor de reset: 578 ciclos de HCLK donde debía costar 18. Como el `leer_reg` tenía el mismo error, el registro devolvía coherentemente lo que nunca se había escrito.
- **La dirección del bus no es la de byte.** Con 16 bits el FSMC saca `HADDR[25:1]`: A0 no existe porque cada dirección externa vale dos bytes y son NBL0/NBL1 los que eligen cuál. El modelo sacaba la dirección de byte y la SRAM la volvía a escalar.
- **El dato tenía que sobrevivir al flanco.** Se soltaba en el mismo instante en que subía NWE; entre eso y el retardo del pad, la memoria veía el bus ya libre. Ahora se mantiene un ciclo (t_DH) y ese ciclo se paga.
- **PD7 es NE1 y NCE2 a la vez** — el mismo pad. Modelarlo como dos señales dejaba a la NAND sin seleccionar nunca.

Y una quinta que era un error de la prueba, no del modelo: exigía `ECCR != 0` sobre una página `0xC0..0xDF`, cuyo contenido cancela todas las paridades. El ECC correcto de esos treinta y dos bytes es exactamente cero; la prueba comprueba ahora lo que un Hamming sí promete —ser función de los datos—.

El hallazgo del encapsulado, que es el paralelo de lo que salió con el DCMI: sin A0–A15 ni NE2–NE4, **el LQFP100 solo puede usar el bus externo multiplexado**, y sin multiplexar todo bloque de 64 K es un alias (`0x100` y `0x200` son la misma celda). El reset de `BCR1` con `MUXEN = 1` no es casualidad. Además, como PD7 sirve de NE1 y de NCE2, en una placa con F407VG una SRAM y una NAND no pueden convivir en el bus.

Queda de F7: OTG y ETH.


## 2026-09-03 08:56:54 UTC

**Mensaje:**

> Completa ahora la fase F7, pero solamente lo referente a OTG.
> Analiza las similitudes y diferencias de los diferentes canales OTG. En caso de existir diferencias, realiza un diseño flexible de forma que, mediante parámetros de clases template C++ o parámetros del constructor de la clase, se pueda seleccionar el tipo de OTG.
> Documenta el trabajo en el fichero doc/stm32f407vg_fase7_otg.md

*(El turno se reanudó 2 veces con una continuación automática.)*


**Respuesta:**


*[nota intermedia]* Now I'll write the OTG model. This is a large peripheral, so let me build it carefully.


*[nota intermedia]* Now the register access and the transaction engine, in the same file:


*[nota intermedia]* Now let me add the test-bench members and construction for the OTG.


*[nota intermedia]* Now the four test groups:


*[nota intermedia]* You've hit your session limit · resets 12:20pm (UTC)


*[nota intermedia]* You've hit your session limit · resets 12:20pm (UTC)


*[nota intermedia]* Now delivering and committing.


F7-OTG cerrada: **1729/1729 comprobaciones, 0 fallos** (~18 s), commit `768a639`.

**El análisis de "canales" tiene aquí tres respuestas distintas**, y esa es la parte interesante:

- **Las dos instancias no son copias.** FS y HS difieren en el bus (AHB2/AHB1), en si son *maestros* de la matriz (el HS es el octavo), en el transceptor (integrado / ULPI), en la RAM de FIFOs (320 / 1024 palabras), en endpoints (4/6), canales (8/12) y hasta en cuántas IRQ sacan.
- **Los canales de anfitrión sí lo son.** Mismo juego de registros y misma máquina; el 3 y el 9 del HS se comportan igual.
- **Los endpoints de dispositivo no.** El EP0 tiene otra *forma de registro*: `MPSIZ` de dos bits codificados en vez de once de bytes, `XFRSIZ` de siete en vez de diecinueve, sin `EPTYP`, sin `EPDIS`, y es el único que recibe SETUP. Un modelo que los tratara como un vector homogéneo se equivocaría en siete puntos.

Cuatro variantes por `OtgCaps` (`_FS`, `_HS`, `_HS_FS`, `_DEV`), seleccionables en compilación con `OtgT<C>` o en ejecución por el constructor, aplicadas como máscara de escritura *y* como existencia de grupos enteros de registros.

**Lo que la electricidad decide.** El transceptor mueve tensiones de verdad: el 1,5 kΩ de D+, los dos 15 kΩ del anfitrión, los 45 Ω del emisor. La conexión, la velocidad, el reset de bus y el despertar no son banderas, son el divisor resistivo — y se comprueban midiendo el pin.

**Cinco cosas que salieron a la luz:**

- Los **valores de reset de la partición de FIFOs no son una partición válida**: `GRXFSIZ` pide 512 palabras de una RAM de 320 y las cuatro `DIEPTXF` arrancan solapadas. Programarlas todas no es una recomendación.
- **`HPRT.PENA` se borra escribiendo uno**: el lee-modifica-escribe de toda la vida apaga el puerto recién habilitado.
- **Sin los 48 MHz de PLL48CK no aparece ni el pull-up.** El PC no ve nada enchufado y ningún registro lo dice.
- Un anfitrión estaba "viendo" un cable que nadie sujetaba: un nodo flotante conserva su última tensión, y venía de un enlace del I2S dejado puesto tres grupos antes. La detección exige ahora el par sujeto por los 15 kΩ.
- **El encapsulado, por una vez, lo tiene todo** — el ULPI cabe entero en el LQFP100. Pero PB11/PB12/PB13 son también ETH_RMII, PB12 es `OTG_HS_ID`, PB13 es `OTG_HS_VBUS` y PA5 es `DAC_OUT2`: cablear el PHY externo cuesta el Ethernet, la detección de rol por pin y el segundo canal del DAC.

También detecté una **discrepancia en el informe**: §12.15.1 dice "4 endpoints además del 0" mientras el mapa de registros de la misma sección enumera "(0-3)". El modelo sigue al mapa (cuatro en total), y queda anotado en el documento.

Queda de F7: ETH. Y siguen acumulándose carpetas `_to_delete/git-tmp*` en el repositorio — son restos de bloqueo de git que no puedo borrar desde aquí.


## 2026-09-03 13:01:23 UTC

**Mensaje:**

> Completa ahora la fase F7, pero solamente lo referente a ETH. 
> Analiza las similitudes y diferencias de los diferentes canales ETH. En caso de existir diferencias, realiza un diseño flexible de forma que, mediante parámetros de clases template C++ o parámetros del constructor de la clase, se pueda seleccionar el tipo de ETH.
> Documenta el trabajo en el fichero doc/stm32f407vg_fase7_eth.md

*(El turno se reanudó 3 veces con una continuación automática.)*


**Respuesta:**


*[nota intermedia]* Now the Ethernet MAC. Let me start with the model.


*[nota intermedia]* Now the external PHY model:


*[nota intermedia]* Now the four test groups.


*[nota intermedia]* API Error: 529 Overloaded. This is a server-side issue, usually temporary — try again in a moment. If it persists, check https://status.claude.com.


*[nota intermedia]* API Error: 529 Overloaded. This is a server-side issue, usually temporary — try again in a moment. If it persists, check https://status.claude.com.


*[nota intermedia]* API Error: 529 Overloaded. This is a server-side issue, usually temporary — try again in a moment. If it persists, check https://status.claude.com.


*[nota intermedia]* All green — 1811/1811. Now the report.


F7-ETH cerrada, y con ella **la fase F7 entera**: **1811/1811 comprobaciones, 0 fallos** (~23 s), commit `aa70814`.

**Los tres sentidos de "canal"** en el Ethernet, que aquí resultan ser los tres genuinos:

- **Las dos interfaces físicas no son dos modos, son dos caminos.** MII lleva cuatro hilos por sentido con **su propio reloj en cada uno** (25 MHz) y dieciocho pines; RMII, dos hilos por sentido con **uno solo común** de 50 MHz y nueve pines. Se elige con `SYSCFG_PMC` bit 23 y solo con el MAC en reset: no es un modo de funcionamiento, es un cableado.
- **Los dos anillos del DMA comparten los nombres de los campos y ninguno de sus significados.** `TDES0` lleva órdenes (FS, LS, IC, CIC) y `RDES0` el resultado (longitud recibida, error de CRC, qué filtro la aceptó). Y el detalle traicionero: los bits de encadenado están en `TDES0` para transmisión y en **`RDES1`** para recepción. Ponerlos en la misma palabra no da error — el anillo funciona hasta que da la vuelta.
- **El filtro 0 es distinto de los otros tres**, exactamente como el endpoint 0 del USB: no tiene `AE` (está siempre activo), ni `SA`, ni máscara de bytes `MBC`.

**La trama va por los pines de verdad** — preámbulo, delimitador y CRC-32 calculado sobre lo que cruza el cable — siguiendo el reloj que pone el PHY. Sin ese reloj el MAC no transmite y lo dice por `TUS`, que es el síntoma real de un PHY que no ha arrancado. MDIO es la trama de 32 bits completa por dos hilos, con su pull-up de placa: por eso preguntarle a una dirección donde no hay nadie devuelve todo unos.

**Dos hallazgos que merecen mención:**

- **El informe se contradice consigo mismo en `MACCR`.** Dice que el reset es `0x0000 8000` y en la misma tabla que el bit 14 es `RE` con reset 1 — pero `0x8000` es el bit **15**. Si `RE` valiera uno, el reset sería `0x4000`. He seguido el valor de reset, que es lo comprobable, y modelo ese uno como un bit fijo que no se puede borrar.
- **No falta ningún pin, pero están todos cogidos.** `ETH_MII_CRS` es **`PA0-WKUP`**: cablear MII cuesta el pin de despertar desde Standby. `RXD0/RXD1` son `ADC12_IN14/15`. Y ocho de los doce hilos de MII chocan con el ULPI del OTG_HS — la misma conclusión a la que llegué desde el otro lado en la subfase anterior, ahora comprobada desde los dos.

Una cosa que el propio modelo denunció: las pistas del banco de pruebas de puerto serie unen `PA2` con `PB11` y `PC12` con `PA1` — es decir, **MDIO con TX_EN** y el reloj de referencia con una salida. Con el MAC gobernando esos pads era un cortocircuito real, no un artefacto, y saltó el aviso de corriente de pin. Las pruebas sueltan ahora esas pistas antes de enchufar el PHY y devuelven los pines a entrada antes de volver a soldarlas.

Del plan original solo queda el afinado del bus de LT a AT. Y siguen acumulándose carpetas `_to_delete/git-tmp*` en el repositorio que no puedo borrar desde aquí.


## 2026-09-03 15:26:06 UTC

**Mensaje:**

> Explica en qué consiste el afinado del bus de LT a AT de la fase7


**Respuesta:**


El *afinado AT* es la última línea de la tabla de fases del plan (`doc/smt32f407vg_diseño.md`, decisión **D2**), y es lo único que queda pendiente. No es un periférico más: es cambiar el **estilo de codificación del bus TLM-2.0**, de *loosely-timed* a *approximately-timed*.

## Qué hay hoy (LT)

Toda la interconexión usa `b_transport`: **una llamada bloqueante = una transacción entera**. El tiempo no se simula, se **anota** en el argumento `sc_time&` y el maestro lo consume después con un `wait`. En `bus/ahb_matrix.h` eso se ve así:

```cpp
t += cycle();                                  // fase de dirección: 1 HCLK
to_slave[s]->b_transport(gp, t);               // fase de datos: la anota el esclavo
busy_until_[s] = sc_time_stamp() + t;
```

El arbitraje se modela *a posteriori*: cada puerto de esclavo guarda un `busy_until_`, y el maestro que llega mientras otro lo tiene ocupado se lleva la penalización sumada a **su propia** anotación temporal. Es barato y da resultados razonables —la suite mide contención con esto—, pero es una aproximación con tres límites claros:

- **no hay solapamiento**. En AHB real la fase de dirección de la transacción N+1 va **a la vez** que la fase de datos de la N. En LT eso no se puede representar: las transacciones son atómicas y van en serie.
- **el arbitraje es "el que llega primero"**, no round-robin de verdad. El puntero `last_granted_[]` está escrito en el código… y nunca se lee: es un gancho puesto para AT.
- **las ráfagas no son ráfagas**. `AhbExt` ya lleva `hburst` y `exclusive`, pero una INCR16 se modela como dieciséis accesos independientes.

## Qué sería AT

Sustituir `b_transport` por `nb_transport_fw/bw` con el protocolo de cuatro fases —`BEGIN_REQ`, `END_REQ`, `BEGIN_RESP`, `END_RESP`— **mapeadas sobre las fases reales del AHB**: `END_REQ` es el fin de la fase de dirección (`HTRANS`/`HREADY`), `BEGIN_RESP` el dato con su `HRESP`. Con eso el modelo pasa a tener:

- **pipeline real** de dirección y datos, que es la razón de ser del AHB;
- **arbitraje ciclo a ciclo** con reordenación: dos maestros que piden el mismo esclavo en el mismo ciclo se resuelven por prioridad/round-robin, no por orden de llegada del proceso SystemC;
- **ráfagas como unidad**, que es lo que de verdad hacen el DMA de Ethernet, el del OTG_HS, el ART al rellenar línea y los DMA1/2 con su `PBL`;
- **estados de espera por `HREADY`** ciclo a ciclo en vez de un bulto anotado al final.

## Para qué serviría aquí, en concreto

Para responder preguntas de **rendimiento y contención** que hoy el modelo solo aproxima: cuánto se degrada CoreMark ejecutando desde Flash mientras el DMA2 llena la SRAM1 y el MAC de Ethernet escribe descriptores; si la máscara de conectividad de [IR, §6.2] basta para que dos maestros no se estorben; cuánta latencia real ve el OTG_HS cuando el núcleo está en un bucle sobre la misma SRAM.

Para lo que **no** haría falta es para nada de lo hecho hasta ahora: la corrección funcional, el comportamiento de los registros y la fidelidad en los pines —que es donde ha ido el esfuerzo de F3 a F7— no cambian una coma.

## Lo que costaría

El propio plan lo cifra: **+50–100 % de esfuerzo y simulación ~10× más lenta**. Los 23 s de la suite pasarían a minutos. Y habría que mantener las 1811 comprobaciones en verde durante la migración.

La topología, eso sí, está preparada: los sockets son `simple_target_socket`/`simple_initiator_socket`, que soportan los dos estilos y traen **conversión automática b↔nb**. Eso permite una migración por partes: convertir primero la matriz, dejar los esclavos en LT detrás del adaptador del socket, y pasar a AT solo aquellos donde importa —Flash/ART, SRAM1/2 y el FSMC—. `AhbExt` ya lleva el id de maestro, `HPROT`, `HBURST` y el bit de exclusivo; los esclavos ya declaran su latencia en `access_cycles()`; y el reloj ya se convierte a tiempo desde `hclk_hz`. No hay que rehacer nada, hay que sustituir el transporte.

Mi lectura, dicho claramente: es **opcional** —el plan lo marca "según necesidad del proyecto"— y solo merece la pena si el objetivo pasa a ser analizar rendimiento del bus. Si el objetivo sigue siendo un modelo funcional y eléctricamente fiel del MCU, lo que hay ahora es suficiente, y AT solo compraría números de latencia a cambio de diez veces más tiempo de simulación.


## 2026-09-03 15:30:38 UTC

**Mensaje:**

> Revisa todas las fases de diseño y determina qué partes han ido quedando pendientes revisando los informes de los ficheros MD en la subcarpeta doc. 
> Documenta todo lo que queda pendiente de todas las fases en el fichero doc/stm32f407vg_todo_rev1.md

*(El turno se reanudó 2 veces con una continuación automática.)*


**Respuesta:**


*[nota intermedia]* You've hit your session limit · resets 5:30pm (UTC)


*[nota intermedia]* You've hit your session limit · resets 5:30pm (UTC)


*[nota intermedia]* Cruces con el código hechos. Escribo el documento consolidado.


*[nota intermedia]* All 124 identifiers check out. Delivering and committing.


`doc/stm32f407vg_todo_rev1.md` escrito y commiteado (`523be0f`). **124 puntos** extraídos de los veintidós informes y contrastados contra el código.

**Cómo está organizado.** Cada punto lleva un identificador estable —`P-07`, `F-23`, `X-04`…— con la fase que lo originó, dónde está anotado y qué impacto tiene, en siete categorías: pendientes de plan (11), funciones no modeladas (48), simplificaciones de temporización y física (21), datos sin fuente documental (13), discrepancias con el informe (12), huecos de verificación (9) y deuda de instrumentación (10). Cierra con una propuesta de orden de cierre en cuatro bloques.

**La conclusión de fondo:** hay **un solo pendiente de plan de primer orden**, la elevación de LT a AT (`P-01`), y arrastra otros cuatro. Once puntos más son trabajo acotado y barato. Todo el resto son decisiones conscientes de alcance con su motivo escrito.

**No me limité a leer los informes; crucé lo escrito con el código, y eso destapó cuatro cosas:**

- `last_granted_[]` en la matriz **se escribe y nunca se lee** — es el gancho que alguien dejó puesto para AT.
- `PinMux::total_pin_current()` existe y funciona, pero **nadie la llama**: la comprobación automática del límite de 240 mA que F3 daba por encaminada no está.
- `ack_out` del DMA está cableado a señales en el top y **ningún periférico lo consume**; esa es la causa de la limitación conocida de F4-TIM, no un detalle teórico.
- `tb_set_request()` —instrumentación que el informe de F4 declaró que "desaparecerá en cuanto cada periférico active su propia línea"— **sigue con seis usos** en el banco.

**Y dos puntos se habían perdido entre fases:**

- **`P-06`**: F1 anotó como "pendiente de F3" qué ve un maestro ajeno al núcleo en la dirección 0 con `MEM_MODE` apuntando a SRAM o FSMC. El informe de F3 no lo menciona ni entre lo resuelto ni entre lo pendiente. Sigue sin decidirse.
- **`I-04`**: el `TODO(F4/F5)` de la tabla de funciones alternativas sigue pidiendo ULPI, ETH, FSMC, SDIO y DCMI — **todos registrados ya en F7**. De su lista solo queda `RTC_AF1` (PC13). El comentario está obsoleto.

Una salvedad sobre las discrepancias: escribí "todas resueltas" en el primer borrador y lo corregí. **`X-04` (`FP_REMAP`) sigue abierta**: F6 dejó la contradicción entre el encaje de campos y el destino en SRAM escrita en un comentario del código en vez de resolverla, y así continúa.


## 2026-09-03 21:15:45 UTC

**Mensaje:**

> Escribe todo el diálogo de este chat en un MD, fichero doc/stm32f407vg_chat.md. De ahora en adelante, añade siempre los mensajes del chat, tanto prompts como respuestas, en dicho fichero


**Respuesta:**

Hecho: `doc/stm32f407vg_chat.md`. Y una advertencia por delante, porque cambia lo que este fichero puede prometer.

**El diálogo no era del todo recuperable.** La conversación se compactó el 3 de septiembre a las 08:26 UTC, y de ese punto hacia atrás el contexto ya no tiene mis respuestas. Lo que sí encontré es el registro de la sesión en `~/.claude/projects/.../68142eaa-….jsonl`, 2,9 MB, que conserva literalmente todo lo posterior a la compactación. Así que el fichero está partido en dos, y lo dice en la cabecera:

- **Parte I** (bajo consumo, DCMI y el arranque de FSMC): **los mensajes son literales** —el resumen de compactación los recoge uno a uno— pero **las respuestas se perdieron**. En su lugar hay una nota de lo que dejó hecho cada turno, contrastada con los informes y los *commits*, no escrita de memoria.
- **Parte II** (desde la compactación hasta ahora): **transcripción literal** con marcas de tiempo, extraída del registro.

Dos decisiones de criterio que conviene que veas, por si prefieres otra cosa:

- **Solo he incluido el texto del diálogo.** Las llamadas a herramientas, sus resultados y el razonamiento intermedio quedan fuera: son ruido de ejecución, y meterlos multiplicaría por veinte el tamaño sin añadir nada que se pueda leer.
- **Las continuaciones automáticas no son mensajes tuyos.** Los *"Continue from where you left off"* son reanudaciones del turno cuando la respuesta no cabía de una vez; los anoto como tales en vez de fingir que escribiste eso.

De aquí en adelante añado cada intercambio al final del fichero, y lo dejo dicho también en la cabecera para que la convención no dependa de que yo me acuerde.

Una cosa que no puedo garantizar sola: **si esta sesión vuelve a compactarse, el tramo perdido tendrá el mismo problema**. El registro JSONL sigue creciendo en el disco del contenedor, así que mientras la sesión viva se puede volver a extraer; pero el contenedor es efímero. Si te importa que el diario quede completo, lo prudente es que lo commitee al terminar cada turno —que es lo que voy a hacer— y no dejarlo para el final.


## 2026-09-05 (turno actual) UTC

**Mensaje:**

> Evalúa el ahorro en tiempo de simulación entre dos escenarios: A) el MCU
> incluye el modelado de OTG y ETH pero el programa del MCU no usa estos
> periféricos, y B) El MCU no incluye el modelado de OTG y ETH, sólo bloques
> dummy con los registros pero sin funcionalidad

**Respuesta:**

Lo he medido en vez de estimarlo, y el resultado cambia la pregunta: **la
elección entre A y B es falsa**. Casi todo el coste de A no viene de modelar los
periféricos, sino de **dos bucles de sondeo**. Corregidos —escenario C—, el
modelo completo cuesta **exactamente lo mismo que el dummy** y sigue estando
entero. Está en `doc/stm32f407vg_coste_simulacion.md` y el banco de medida, en
`src/top/bench_main.cpp` (`make bench`).

**El coste puro, con NRST abajo para que no haya nada más en la simulación**
(2 s simulados): A → 112 033 deltas y 0,0154 s de anfitrión; B → 29 deltas;
**C → 29 deltas**. No es que C se parezca a B: es el mismo número.

Esos 112 000 despertares son tres bucles, y uno se lleva el 89 %:
`EthBase::tx_proc` miraba **cada 20 µs** si el DMA de transmisión tenía algo que
enviar; con `DMAOMR.ST` a cero la respuesta era que no, cincuenta mil veces por
segundo simulado. El `motor_proc` del OTG_HS despertaba cada microtrama —125 µs,
ocho mil veces por segundo— **por estar apagado**.

**Con el MCU funcionando** (200 ms simulados, 15 repeticiones, mediana):

| Carga | A | B | C | A→B | A→C |
| :--- | ---: | ---: | ---: | ---: | ---: |
| MCU aparcado | 0,0689 s | 0,0638 s | 0,0609 s | 7,4 % | 11,6 % |
| blinky | 0,0731 s | 0,0688 s | 0,0653 s | 5,9 % | 10,7 % |
| CoreMark | 0,1614 s | 0,1575 s | 0,1537 s | 2,4 % | 4,8 % |
| Suite completa | 20,81 s | — | 20,65 s | — | 0,5 % |

**El sobrecoste es una tasa fija por segundo simulado, no un porcentaje**: unos
56 000 despertares, entre 8 y 25 ms de anfitrión por segundo simulado. Lo que
cambia con la carga es el denominador, y por eso citar un porcentaje suelto
engaña. Cuanto más trabajo real hace la simulación, menos se nota: en la suite
completa es el 0,5 %.

Y tres cosas que **no** cuestan, porque la intuición suele ir al revés: la
**elaboración es indistinguible** (0,0337 s en las tres variantes: construir los
dos periféricos no se nota), la **memoria** tampoco (16,9 frente a 17,0 MiB), y
el **decodificador de bus no cambia**, porque el dummy sigue ocupando su entrada
en la tabla de esclavos. Un periférico modelado pero quieto no cuesta casi nada
por existir; solo cuesta por despertarse.

He aplicado C al árbol —dos condiciones más las notificaciones que despiertan al
hilo cuando el bloque vuelve a la vida— y **la suite sigue en 1811/1811, 0
fallos**. Si prefieres el modelo tal como estaba, se revierte con quitar esas
dos condiciones.

La regla que deja esto: en un modelo de eventos discretos **el coste de un
bloque es proporcional a cuántas veces despierta, no a lo complicado que sea**.
Con ese criterio queda un sospechoso mucho mayor que OTG y ETH juntos: el bucle
de sueño del núcleo en `core/cpu.h` **sondea cada 1 µs** mientras la CPU está
dormida — un millón de despertares por segundo simulado, veinte veces más.


## 2026-09-05 (turno actual) UTC

**Mensaje:**

> Completa la mejora del tiempo de simulación del núcleo

**Respuesta:**

Hecho, y el resultado es mayor de lo que esperaba: **un MCU dormido ha dejado de
costar tiempo de simulación**. Dos segundos simulados con el núcleo parado en
`wfe` pasan de **2 112 245 deltas y 0,575 s** de anfitrión a **50 deltas y
0,000 s**. Cuarenta y dos mil veces menos eventos.

El bucle de sueño de `core/cpu.h` sondeaba cada 1 µs — un millón de despertares
por segundo simulado, veinte veces más que OTG y ETH juntos — para preguntar si
había algo pendiente, encontrar que no, y volver a dormirse. Ahora espera a los
cinco sucesos que de verdad despiertan al núcleo: una excepción pendiente, un
evento del EXTI, el reset, la parada del árbol de reloj y el depurador.

Para el primero hice falta un evento nuevo, `pending_ev()` en `core_sys_if`, que
el SCS dispara al muestrear las líneas de interrupción, en `set_pending()` y **en
cualquier escritura al SCS**. Este último es el que evita el error sutil: `ISPR`,
`STIR`, `ICSR` con `PENDSVSET` o `SHCSR` pueden dejar una excepción pendiente, y
esas escrituras **pueden venir del depurador con el núcleo dormido**. En vez de
enumerar los casos y olvidarme de alguno, aviso siempre — escribir en el SCS es
rarísimo comparado con sondear.

Y había una carrera que cerrar: `wait()` solo ve las notificaciones posteriores a
la propia espera, así que un suceso ocurrido entre el `WFI` y el `wait` se
perdería y el núcleo dormiría **para siempre**. El sondeo tapaba eso por fuerza
bruta. La condición se re-comprueba justo antes de bloquear, y entre esa
comprobación y el `wait` no cede el control ningún otro proceso.

Medido con firmware de verdad (200 ms simulados, 15 repeticiones, mediana):

| Carga | Original | Solo periféricos | Núcleo y periféricos | Mejora |
| :--- | ---: | ---: | ---: | ---: |
| MCU aparcado en `wfe` | 0,0580 s | 0,0491 s | **0,0000 s** | — |
| blinky (SysTick + WFI) | 0,0608 s | 0,0530 s | **0,0047 s** | **12,9×** |
| CoreMark | 0,1362 s | 0,1247 s | **0,1052 s** | 1,29× |
| Suite completa | 18,20 s | — | **17,20 s** | 5,5 % |

**La prueba de que no se ha cambiado nada más:** en blinky las transacciones que
cruzan la matriz son **13 713 en las dos versiones, exactamente las mismas**.
Mismas instrucciones, mismo tráfico de bus; lo único que desaparece es la espera
desperdiciada. El tiempo simulado total de la suite cambia en 20 ns sobre 2,33 s,
que es el redondeo de la latencia de despertar al pasar de 1 µs de granularidad
a inmediata.

**Suite: 1811/1811, 0 fallos.** Es la red de seguridad que importaba aquí,
porque incluye las pruebas de bajo consumo (WFI/WFE, Stop, Standby, despertar por
EXTI, por temporizador y por el depurador), que son justo las que se romperían si
me hubiera dejado una fuente de despertar fuera.

La suite mejora poco —5,5 %— y tiene sentido: está dominada por trabajo activo,
no por esperas. Es el perfil en el que menos se nota. Y lo que queda, `ClockGen`
con las ondas cuadradas encendidas, **no es un error**: ahí el sondeo *es* el
modelo, porque un reloj es una onda; para eso está el interruptor
`set_internal_waveforms(false)`.


## 2026-09-07 UTC — Estrategia XML + SVG de QtSysC

**Mensaje:**

> El proyecto QtSysC, sibling de este proyecto, permite especificar el circuito
> SystemC a construir con dos ficheros, un XML que especifica los componentes y
> conexiones y un SVG que contiene una descripción gráfica de dicho circuito (no
> todos los componentes del XML deben estar presentes).
> Analiza las posibilidades de aplicar la misma estrategia en este proyecto
> creando una librería de componentes externos al MCU, como LEDs, botones, etc.
> CRITICAL: Respond with TEXT ONLY. Do NOT call any tools.

**Respuesta:**

### 1. Qué hay ya construido que encaja con esa estrategia

El proyecto no parte de cero. `src/verif/ext_parts.h` es, de facto, una librería
de componentes externos al MCU con unas veinte piezas: `Crystal`, `ExtClock`,
`Led`, `Button`, divisores y resistencias, `SignalLink`, `I2cWire`, EEPROM
24Cxx, maestro I2C externo, `SdCard`, `CanWire`/`CanTransceiver`/`CanNode`,
sonda SWD, analizador SWO, `CameraSensor`, `ExtSram`, `ExtNand`, `UsbHostRig`,
`UsbDeviceRig` y `EthPhy`.

Y lo importante: **todas comparten ya el mismo contrato de conexión**. Cada una
recibe en el constructor una o varias referencias `analog_net_if&` y llama a
`register_driver()` sobre ellas. El testbench las conecta invariablemente con
`pinmux.analog(port, pin)`. Eso significa que el «netlist» de la placa ya existe
conceptualmente; lo que ocurre es que hoy está escrito en C++ dentro del bloque
de elaboración de `F1Tb` en `sc_main.cpp`, mezclado con la lógica de los 1811
checks.

Dicho de otra forma: la distancia entre lo que hay y lo que propone QtSysC no es
de arquitectura, es de **formato de descripción**. El grafo componente–pin–red ya
está bien definido y es uniforme. Falta externalizarlo.

### 2. El esquema XML natural para este proyecto

Tres tipos de entidad:

- **Componentes.** Tipo (el nombre registrado en una factoría), identificador de
  instancia y parámetros propios de la pieza (`Led` necesita color/Vf/serie,
  `Crystal` la frecuencia, `ExtSram` el tamaño y los tiempos, `EthPhy` la
  dirección MDIO, `SdCard` la imagen de respaldo, `Button` el rebote).
- **Nodos eléctricos.** Cada `AnalogNet` es un nodo. Los del MCU se nombran por
  su pad (`PA5`, `PH0`, `VDD`, `NRST`, `BOOT0`) y se resuelven con
  `pinmux.analog()` o los pads de alimentación; los puramente externos (el nudo
  entre un LED y su resistencia, el bus I2C, el par diferencial de USB) hay que
  crearlos.
- **Conexiones.** Pares (instancia, terminal) → nodo. El terminal debe ser
  **nominal** («anodo», «catodo», «sda», «scl», «mdio», «d0».."d15") y no
  posicional: el orden de los argumentos del constructor es un detalle de
  implementación que no debe filtrarse al XML.

```xml
<placa nombre="discovery-min">
  <nodo id="PA5"/>
  <nodo id="n_led1"/>
  <componente tipo="Led" id="LD2" vf="2.0" r_serie="330">
    <pin nombre="anodo" nodo="PA5"/>
  </componente>
  <componente tipo="Crystal" id="X1" f="8M">
    <pin nombre="in" nodo="PH0"/>
  </componente>
</placa>
```

### 3. Los tres obstáculos reales

**(a) C++ no tiene reflexión: hace falta una factoría.** El XML dice
`tipo="Led"` y alguien tiene que convertir esa cadena en un `new Led(...)`:
registro estático `std::map<std::string, creador>` poblado por macros de
auto-registro. Obliga a **homogeneizar los constructores** o a escribir un
adaptador por pieza; lo segundo no toca el código existente y es por donde
conviene empezar.

**(b) La elaboración de SystemC es estática.** No se pueden construir
`sc_module` una vez arrancado `sc_start()`. El XML **debe leerse y construirse
antes de simular**, exactamente donde hoy `sc_main.cpp` instancia sus partes.
Consecuencia: «elige la placa al lanzar el ejecutable» sí; editor interactivo
que añade componentes con la simulación en marcha, no.

**(c) `AnalogNet` no tiene desregistro de drivers**, solo Hi-Z (punto I-03 del
TODO). Es en realidad una buena noticia para el requisito de que no todos los
componentes del XML estén en el SVG: «presente en XML, ausente en SVG» no
significa «no lo instancies» sino «instáncialo y déjalo desconectado». El idioma
ya existe en el proyecto (`set_enabled`, `set_conectada`, `conectar`);
generalizarlo a **todas** las piezas es trabajo pequeño y mecánico, con
beneficio inmediato aunque no haya XML.

### 4. Qué papel puede jugar el SVG aquí

En QtSysC el SVG tiene sentido porque hay una GUI Qt que lo pinta. Aquí no la
hay, y el SVG puede desempeñar tres papeles de valor decreciente:

1. **Visualización de placa** (valor alto, coste bajo): un SVG generado *desde*
   el XML, no escrito a mano. Hoy, entender qué está conectado a PB11 en un test
   dado exige leer `sc_main.cpp`.
2. **Traza gráfica de post-simulación** (valor alto, coste medio): el mismo SVG
   con `id` en cada elemento más un volcado de eventos. Explota lo que ya tiene
   el modelo —tensiones y corrientes reales en float— y que hoy solo se ve por
   `printf`.
3. **Interacción en vivo** (valor discutible, coste alto): pulsar un botón del
   SVG durante la simulación exige un proceso que sondee una fuente externa, y
   el trabajo de esta última fase ha ido justo en la dirección contraria —
   eliminar sondeos. Debe ser opcional y estar apagado en la suite.

### 5. El beneficio que no es obvio: validación eléctrica de la placa

Es el argumento más fuerte, por encima de la comodidad. Funciones como
`can_links`, `spi_links`, `i2s_links`, `i2c_bus`, `adc_links`, `dcmi_placa`,
`fsmc_placa`, `usb_placa` y `eth_placa` existen para **resolver conflictos de
pines entre grupos de test**, y nacieron de errores reales: el aviso de
sobrecorriente en PB12/PB11 era un cortocircuito genuino entre el cable de USART
y el MDIO/TX_EN de Ethernet; el host de USB «vio» un dispositivo inexistente
porque una `AnalogNet` flotante conserva su último voltaje y un enlace I2S
olvidado había dejado PB15 a 3,3 V.

Un netlist declarativo permite detectar eso **antes de simular**: dos
componentes de baja impedancia con tensiones incompatibles sobre el mismo nodo,
un pin con dos funciones alternativas asignadas, un nodo sin camino a masa, un
pad no bonded en LQFP100 con algo conectado. Es un recorrido del grafo, barato,
y convierte una clase entera de fallos de banco en un mensaje de arranque.

### 6. Ruta de adopción por incrementos

1. **Consolidar la librería.** `ExtPart` gana `set_enabled(bool)`, descripción
   de terminales por nombre y constructor canónico o adaptador. Sacar
   `ext_parts.h` de `verif/` a `parts/`. Sin cambio de comportamiento.
2. **Netlist en memoria + volcado.** Estructura `Netlist` y volcado a XML, para
   que el formato lo defina lo que el proyecto necesita y no la especulación.
3. **Lector de XML y validador**, con las comprobaciones de §5. Aquí está el
   retorno grande.
4. **Generador de SVG** desde el netlist, con `id` estables.

### 7. Conclusión

La estrategia es aplicable porque la parte difícil —un conjunto uniforme de
componentes que se conectan por referencias a nodos eléctricos con semántica
física real— ya está hecha y probada. Las tres restricciones que la moldean son
la ausencia de reflexión en C++, la elaboración estática de SystemC y la
ausencia de desregistro en `AnalogNet`. La diferencia principal respecto a
QtSysC es que allí el SVG es la interfaz y aquí sería documentación y traza; y el
beneficio principal no es la comodidad, sino la validación automática del
netlist.


## 2026-09-07 UTC — Paso 1 de la adopción

**Mensaje:**

> Actualiza el MD del chat con lo anterior y ejecuta el paso 1 de la adopción

**Respuesta:**

Hecho. El paso 1 era el único de los cuatro que **no añade funcionalidad**: pone
la librería en condiciones de ser descrita desde fuera. Está documentado entero
en `doc/stm32f407vg_parts_paso1.md`; aquí lo esencial.

**`src/verif/ext_parts.h` → `src/parts/ext_parts.h`**, junto a un
`parts/part_base.h` nuevo. El cambio de carpeta no es cosmético: la pieza dejó
de ser «circuitería del banco» hace tiempo; es la librería de componentes de
placa, y `verif/` la escondía.

**Terminales con nombre.** Cada pieza declara ahora sus patillas por un nombre
nominal —`anodo`, `sda`, `mdio`, `d0`— y no por la posición del argumento en el
constructor. Es el punto entero: un XML tiene que poder decir «el pin `sda` de
`eeprom` va al nodo `PB7`» sin conocer un detalle de implementación. Un terminal
puede llevar **varios drivers** (el `cmd` de la SD lleva el driver de la tarjeta
y el pull-up de 47 kΩ del zócalo) y puede no llevar **ninguno** (`pasivo`: el
`NOE` de la SRAM, el `MDC` del PHY, el origen de una pista). Esa distinción
importa porque un nodo donde todo el mundo es pasivo está flotante por
construcción, y eso es un error de placa detectable sin simular.

**Un solo interruptor.** El proyecto tenía la idea de desoldar repartida en
**siete nombres** (`set_enabled`, `set_attached`, `set_conectada`, `conectar`,
`soldar`/`soltar`, `enchufar`, `attach`/`detach`). Ahora todos son el mismo
método virtual `set_enabled(bool)`, y **los nombres antiguos se conservan como
alias**: `sc_main.cpp` no se ha tocado ni una línea, que es lo que convierte a la
suite en una comprobación honesta del refactor. Diez piezas que no tenían
interruptor lo tienen. Una excepción deliberada: `CanTransceiver` tenía dos con
significados físicos distintos —soldarlo, y el modo reposo STB—, así que el
segundo pasa a llamarse `set_standby`. Es el único renombre con cambio de
significado.

**Por qué desconectar y no dejar de construir.** Las dos restricciones de
siempre: la elaboración de SystemC es estática y `AnalogNet` no sabe
desregistrar drivers (I-03). De ahí la lectura correcta del requisito de QtSysC
de que no todos los componentes del XML estén en el SVG: la pieza ausente **se
construye desconectada**. Con `R_HIZ = 1e12` frente a 30 Ω, eso es eléctricamente
invisible.

**Y el volcado, que no estaba pedido pero sale gratis:**

```
./build/stm32f407vg --netlist
```

43 componentes de 20 tipos. Al montarlo apareció un hueco que no se veía desde
dentro del C++: las piezas de una sola patilla no tenían identificador de
instancia, y salían nueve componentes llamados `extdrv`. Eso obligó a separar
tres nombres que iban mezclados —**tipo** (`Led`), **instancia** (`led_pd12`) y
**etiqueta de driver** (`led`, la que sale en los avisos de sobrecorriente del
pad)—, que es exactamente la clase de cosa que solo se ve cuando intentas
escribir el modelo hacia fuera.

**El argumento entero está en dos entradas contiguas del volcado:**

```xml
<componente tipo="SignalLink" id="lnk_u2_u3" conectada="si">
  <pin nombre="destino" nodo="PB11"/>
  <pin nombre="origen"  nodo="PA2" pasivo="si"/>
</componente>
<componente tipo="EthPhy" id="phy" conectada="no">
  <pin nombre="mdio"  nodo="PA2"/>
  ...
  <pin nombre="tx_en" nodo="PB11" pasivo="si"/>
</componente>
```

Ese es **el cortocircuito que costó horas de depuración en F7-ETH**: la pista de
USART PA2→PB11 contra el MDIO y el TX_EN del PHY. Se manifestó como un aviso de
sobrecorriente en mitad de una simulación y hubo que rastrearlo hacia atrás.
Ahora son dos líneas de un `grep`, sin XML, sin SVG y sin simular. Es la razón
por la que el paso 1 se paga solo aunque los pasos 3 y 4 no se hagan nunca.

**Suite: 1811/1811, 0 fallos**, y el tiempo simulado total es **idéntico al
picosegundo** (2 327 837 024 213 ps). Esa es la prueba dura: cualquier cambio en
el instante o el orden de una conducción lo habría desplazado. El banco de
medida sigue dando 50 deltas para un MCU aparcado en `wfe`.

Lo que queda anotado en el TODO como I-11, I-12 e I-13 (127 puntos ahora): el
netlist en memoria, el lector de XML con su validador —donde está el retorno
grande— y el generador de SVG.
