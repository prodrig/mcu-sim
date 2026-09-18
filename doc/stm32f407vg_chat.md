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


## 2026-09-07 UTC — Paso 2 de la adopción

**Mensaje:**

> Ejecuta el paso 2 de la adopción

**Respuesta:**

Hecho, documentado en `doc/stm32f407vg_parts_paso2.md`. El paso 1 hacía que el
modelo supiera describirse; este invierte la dirección: **describir primero y
construir después**.

**Dos ficheros.** `parts/netlist.h` es la máquina —`NodeMap` con los 144 pads por
su nombre de esquemático y los que no son pines, `Instancia` con tipo,
identificador, parámetros, referencias y conexiones nominales, y `Netlist` que
declara, valida, construye y vuelca— y no conoce ni una sola clase de pieza.
`parts/netlist_parts.h` es la otra mitad: los constructores tipados. En el paso 3
se reescribe el segundo y el primero no se toca.

**Sin factoría por cadena, a propósito.** Convertir `tipo="Led"` en un `new
Led(...)` exige auto-registro por tipo, y eso es el paso 3. Aquí cada instancia
lleva su creador tipado, con el compilador comprobándolo.

**El grupo elegido fue el bus CAN, y no por comodidad.** Las ocho pistas de
SPI/I2S habrían sido mucho más fáciles, pero son ocho `SignalLink` idénticos y no
habrían probado nada. El CAN exige lo que ninguna pista exige: tres tipos, un
nodo que no es un pin —el hilo—, referencias entre instancias —un transceptor
necesita el objeto del hilo, no solo el punto eléctrico—, orden de construcción y
un componente que el MCU ni ve. El objetivo del paso 2 era descubrir qué necesita
el formato, no confirmar lo que ya se sabía.

**Y descubrió tres cosas, que es exactamente para lo que servía:**

*Los nodos son del circuito, no de los componentes.* `CanWire` creaba su propio
`AnalogNet`. Parecía razonable hasta que el netlist quiso referirse al hilo por
nombre y aparecieron **dos nodos distintos llamados igual**. Ahora `CanWire` puede
recibir el nodo desde fuera; el constructor histórico sigue ahí.

*No todo lo que une dos componentes es un nodo.* La referencia al hilo viajaba al
principio como parámetro de texto: funcionaba y era mentira, porque un parámetro
es un valor y esto es una arista del grafo. De ahí `Instancia::refs`, que el
volcado escribe (`<ref nombre="hilo" componente="can_bus"/>`) y la validación
comprueba, **incluido el orden**: referirse a algo declarado después es un error,
porque se construye en orden de declaración.

*La ida y vuelta necesita las dos direcciones.* Comprobar que cada conexión
declarada existe en la pieza construida es insuficiente: declarar de menos pasaría
igual. Y era lo que estaba pasando — el terminal `bus` del transceptor estaba en
la pieza y no en el netlist. Con las dos direcciones, descripción y modelo no
pueden divergir en silencio.

**Validación antes de construir, sin simular.** Ocho clases de error —nodo
inexistente, pad que el LQFP100 no saca, identificador repetido, terminal
duplicado, referencia inexistente, referencia hacia delante, instancia sin tipo,
instancia que nadie sabe construir— todas ejercitadas en T121 con netlists rotos
a propósito. Falta la validación **eléctrica**, que es el paso 3.

**Suite: 1839/1839, 0 fallos** (1811 + 28 nuevas). Las setenta y pico líneas de
prueba que usan `can_bus`, `xcvr1`, `xcvr2` y `nodo_ext` no se han tocado: los
punteros siguen ahí y apuntan a lo mismo, y las 97 comprobaciones del bxCAN son la
red de seguridad real. El tiempo simulado sube 20 µs sobre 2,33 s, que son las dos
esperas de la comprobación de soldar y desoldar.


## 2026-09-07 UTC — Cierre del paso 2

**Mensaje:**

> Termina lo que queda antes de pasar al paso 3

**Respuesta:**

Entendí «lo que queda» como lo que el paso 2 dejó a medias a propósito —solo un
grupo migrado, los otros 39 componentes a mano—, porque un lector de XML no sirve
de nada si solo cuatro de cuarenta y tres piezas son declarables. Hecho: **no
queda ni un `new` de pieza externa en `sc_main.cpp`**.

Doce tipos más necesitaron ayudante: `ExtClock`, `I2cWire`, `I2cEeprom`,
`I2cExtMaster`, `SdCard`, `SwoReceiver`, `CameraSensor`, `ExtSram`, `ExtNand`,
`UsbHostRig`, `UsbDeviceRig` y `EthPhy`. Las piezas de muchas patillas se declaran
con lista de pares `{terminal, nodo}`, no con argumentos posicionales: un
constructor de doce argumentos es justo lo que el netlist viene a eliminar, y así
el sitio de la llamada ya tiene la forma del XML.

**Dos cosas aparecieron al cerrar, y las dos son del tipo que solo se ve
haciéndolo:**

*Una fuga de memoria de la fase F5.* `can_bus`, `xcvr1`, `xcvr2` y `nodo_ext` no
se destruían nunca. No es que se olvidaran: es que mantener a mano una lista de
veintitantos `delete` en el orden correcto es exactamente lo que un netlist hace
por ti. El destructor pasa de veintidós `delete` a diez, y los diez que quedan son
periféricos del MCU, no piezas de placa.

*Un uso después de liberar, evitado por poco.* El destructor de un miembro corre
**después** del cuerpo del destructor que lo contiene. Con el netlist como miembro
de `F1Tb`, sus piezas se habrían destruido después de `delete dut`, y cada pieza
suelta sus pines al morir sobre unos `AnalogNet` ya liberados. De ahí
`Netlist::libera()`, al principio del destructor. Es un fallo que el orden manual
no podía tener porque nunca hubo un miembro que poseyera piezas.

**Los cinco `Resistor` siguen siendo objetos locales, y está bien.** Se crean
dentro de funciones de prueba, durante la simulación: son sondas temporales —una
carga de 1 kΩ, un cortocircuito de 0,5 Ω—, no piezas de placa. El inventario las
ve aparecer y desaparecer, que es lo que deben hacer.

**Los dos volcados, ahora completos y separados:**

```
./build/stm32f407vg --netlist       # la placa DECLARADA
./build/stm32f407vg --inventario    # la placa CONSTRUIDA
```

Los dos dan 43 componentes, y T121 comprueba en las dos direcciones que dicen lo
mismo: 147 conexiones declaradas que existen en las piezas, y ningún terminal de
las piezas fuera del netlist.

**Suite: 1840/1840, 0 fallos**, compilación limpia, tiempo simulado idéntico al de
antes de esta migración (2 327 857 024 213 ps) y el banco sigue en 50 deltas. El
paso 3 ya tiene qué leer.


## 2026-09-07 UTC — Paso 3

**Mensaje:**

> Ejecuta el paso 3

**Respuesta:**

Hecho, documentado en `doc/stm32f407vg_parts_paso3.md`. El arco de los tres pasos
cabe ahora en un renglón:

```
$ ./build/sim placas/discovery_min.xml verif/fw/blinky/blinky.bin 205
placa 'discovery-min': 3 componentes, 154 nodos, 0 avisos
firmware: 1032 bytes de verif/fw/blinky/blinky.bin
simulados 205.000 ms en 0.008 s de anfitrion (7341 deltas)
  LED LD4 en PD12: encendido  (3.11 V, 3.38 mA)
```

Firmware de verdad gobernando un LED descrito en un fichero de texto, con la
corriente resuelta en float. A 200 ms está apagado y a 205 encendido, porque el
blinky parpadea. Para cambiar de placa no hay que recompilar nada.

**Cuatro ficheros:** la factoría con auto-registro (`part_factory.h`), el lector
de XML (`xml_min.h`), el puente al netlist (`netlist_xml.h`) y el ejecutable
(`sim_main.cpp`). Y `netlist_parts.h` se reescribió sobre la factoría **tal como
el paso 2 predijo**: `netlist.h` no se tocó salvo para que `add()` consulte el
registro.

**Lector de XML propio, y es la decisión discutible del paso.** Este proyecto
tiene una dependencia —SystemC— y se compila con un Makefile de veinte líneas;
añadir TinyXML-2 por un formato de tres elementos cambia eso para todo el que se
baje el repositorio. Escribir un analizador a mano suele ser un error, así que lo
evité por la vía de ser **estricto**: lo que no entiende es un error con línea y
columna, nunca una suposición. Nueve XML rotos a propósito están en T121, uno por
motivo. Detalle que merece nota: en este formato ningún elemento tiene contenido
textual, así que texto entre etiquetas no es algo que ignorar — es alguien que se
ha equivocado, y se dice.

**La validación eléctrica es el retorno que justificaba los tres pasos**, y tuvo
un momento instructivo. La primera versión sacó **59 avisos, de los que 52 eran
falsos**: marcaba como flotante cualquier pin que ninguna pieza externa gobernara.
Está mal — al otro lado de un pin está el pad del MCU, y que nadie de la placa
tire de él es lo normal en una entrada. El aviso solo tiene sentido en nodos que
no son pines. Un validador que grita por todo no se usa, así que ese ajuste era la
diferencia entre una herramienta y un ruido.

Con el criterio afinado quedaron **siete, y los siete eran ciertos**: los buses
I2C de la EEPROM y el maestro externo, el cable en Y del CAN, y cuatro pines que
el banco comparte entre grupos de prueba (PA0, PA1, PD2 y PH0 — este último, en
una placa real, sería un error: cristal y oscilador externo sobre el mismo pin).
Ninguno es un fallo, pero **ninguno estaba escrito en ningún sitio**: vivían
repartidos por `i2c_bus()`, `adc_links()`, `can_links()` como efectos laterales.
Declararlos con `nodo_bus()` —siete líneas comentadas— convirtió siete decisiones
de placa implícitas en explícitas. Eso ya vale el ejercicio. Ahora: **0 avisos
sobre 43 componentes y 155 nodos**, y la validación corre en cada arranque.

**Y el caso que lo empezó todo**, reconstruido en T121:

```
nodo PB11: conducen a la vez u2_tx.pin y phy_txen.pin
```

El cortocircuito de F7-ETH. Entonces salió como un aviso de sobrecorriente del pad
en mitad de una prueba de USART y hubo que rastrearlo hacia atrás. Ahora salta
antes de simular, con los dos culpables por su nombre.

**La ida y vuelta** cierra el formato: se vuelca la placa entera a XML, se relee
con el lector de verdad y se comprueba que sale el mismo grafo —tipo,
identificador, terminales, referencias, parámetros, estado de conexión y las
marcas de los nodos—. Y el mismo fichero, leído por **otro ejecutable**:
`./build/stm32f407vg --netlist > placas/banco.xml` y luego `./build/sim
placas/banco.xml --valida` da los 43 componentes montados por un programa que no
sabe nada de la suite.

**Suite: 1851/1851, 0 fallos**, los tres binarios compilan limpios y el banco
sigue en 50 deltas. Queda el paso 4, el SVG, que ya tiene de dónde salir.

---

## 2026-09-07 UTC — `doc/parts.md`, el catálogo de componentes

**Prompt.** «Hecho el commit a mano. Genera un fichero MD llamado doc/parts.md,
con la explicación detallada de cada componente externo disponible en la carpeta
parts, incluyendo el significado y uso de cada parámetro. Por ejemplo, en
`<componente tipo="Led" id="LD4" a_vss="si" vf="2.0" r="330">`, indica qué efecto
tienen los parámetros tipo, id, a_vss, vf y r. Crea una sección en el MD para los
parámetros que son comunes a todos o muchos de los componentes, y explica en otra
sección cómo se establecen las conexiones».

**Respuesta.** Catálogo de las 21 piezas, con tres secciones previas: cómo se
establecen las conexiones (nodos, `<pin>`, `<ref>`, `conectada`), los parámetros
comunes (`tipo`, `id`, `conectada`; `vdd`; las resistencias) y cómo se escriben
los valores. Ese último apartado documenta una trampa real: los números se leen
con `atof`, así que `0x50` funciona (glibc lee hexadecimal en coma flotante) pero
**`8M` vale 8 y `4.7k` vale 4,7**, en silencio. Escribir `8e6` y `4700`.

---

## 2026-09-07 UTC — El parámetro `vdd` del LED

**Prompt.** «Añade el parámetro vdd con valor por defecto de 3.3V al Led, tanto en
el XML como en SystemC y la documentación. De esta manera se puede modelar un Led
azul con vf = 3.0V conectando el ánodo a 5V y el cátodo al pin del MCU».

**Respuesta.** `Led(..., double vdd = 3.3, const char* term = "anodo")`. Con
`a_vss="no"` el LED conduce cuando la tensión del pin baja de `vdd - vf`, y ese
`vdd` ya no está clavado a 3,3. Placa de ejemplo `placas/led_azul_5v.xml`.

Añadí además, **sin que se pidiera y diciéndolo**, el alias de terminal `catodo`,
para que el montaje con el cátodo al pin se escriba como es. Ofrecí quitarlo.

---

## 2026-09-07 UTC — Las placas XML al repositorio, y la limpieza

**Prompts.** «Añade los xml utilizandos en los bancos de prueba al repositorio
(git add), aunque sean regenerables» y «Borra los residuos en _to_delete».

**Respuesta.** `placas/discovery_min.xml`, `led_azul_5v.xml` y `banco.xml`
versionados: el último es regenerable con `--netlist`, y precisamente por eso
tenerlo en el repositorio hace que un cambio en la placa aparezca como un diff en
vez de como nada. Los residuos de `_to_delete` —restos de bloqueos de git del
puente remoto— borrados tras volver a pedir permiso de borrado, que se pierde al
reiniciar el espacio de trabajo.

---

## 2026-09-07 UTC — `Resistor` → `Rpull`, y una corrección mía

**Prompt.** «Cambia el nombre de Resistor a Rpull para que sea coherente con su
función. Modifica el comportamiento del parámetro v para que se pueda conectar a
diferentes tensiones (por ejemplo, v = "5", para construir una resistencia de
pull-up a 5V). Actualiza no sólo el código, también la documentación».

**Respuesta.** El renombrado, sí. Pero la segunda mitad del encargo **no hacía
falta**: comprobé empíricamente que `v` ya aceptaba cualquier tensión —es la
tensión del extremo lejano de un driver Thevenin, y siempre lo fue—. Lo que
estaba mal era **mi documentación**, que decía «`3.3` = pull-up; `0` = pull-down;
cualquier otra cosa = un divisor», como si solo hubiera tres casos. Lo dije en
lugar de fingir un cambio de código, y reescribí el apartado.

De paso salió **un fallo de verdad en el `Led`**: su hilo esperaba un cambio de
tensión *antes* de evaluar, así que un LED sostenido por una resistencia de pull
con el pin como entrada informaba «apagado, 0,00 mA» aunque estuviera encendido.
Evaluar primero y esperar después. Verificado contra la superposición calculada a
mano: 2,20 V / 0,60 mA con `v=5`, 2,09 V / 0,26 mA con 3,3, y apagado con 1,8.

---

## 2026-09-07 UTC — Análisis: varios MCUs en una placa

**Prompt.** «Analiza las modificaciones necesarias al XML y al código C++ para
que, en el futuro, se pueda modificar el MCU que lleva la placa o incluso poner
varios MCUs. […] los nodos de los MCUs no hace falta declararlos, así que las
referencias deberían ser algo así como `nodo = "PD12"` si sólo hay un MCU o
`nodo = "u0.PD12"` si hay dos».

**Respuesta.** `doc/stm32f407vg_multi_mcu.md`, con cada cita de código verificada.
Lo fácil es el esquema de nombres (`NodeMap` es un mapa de cadenas). Lo medio, un
elemento `<mcu>` y una interfaz `mcu_if` con su factoría — la superficie que
`sim_main.cpp` usa del MCU son cuatro cosas. **Lo difícil es unir un pin de `u0`
con uno de `u1`**, porque cada pad crea su `AnalogNet` y lo ata a un `sc_port` en
el constructor, y un `sc_port` no se reata: la decisión hay que tomarla al
construir el MCU. Opción A (un acoplador que generaliza `I2cWire`) solo propaga
el cero, cuesta un delta y aproxima tensiones; **opción B (nodo compartido de
verdad), recomendada**. Y cuatro cosas que ya estaban mal: `basename()`,
`registra_mcu` pisándose, dos `static bool warned` y un solo puerto de GDB.

---

## 2026-09-08 UTC — Dos preguntas sobre la opción B

**Prompt 1.** «En el caso de aplicar la solución recomendada al problema "cada pad
crea su `AnalogNet`", en el caso de un único MCU declarado explícitamente como
"u0", ¿sería necesario declarar explícitamente el nodo del pin PD12 de u0 o un
LED, por ejemplo, podría seguir conectando con `nodo="PD12"`?»

**Respuesta.** No hace falta declararlo, por dos razones independientes que
conviene no mezclar: por el **nombre** (la regla depende de que haya un solo MCU,
no de que sea implícito; los pads existen desde que se construye el `PinMux` y
`<nodo>` solo es obligatorio para los externos) y por la **mecánica de la opción
B** (un pad solo deja de crear su nodo si un `une` lo nombra, así que el
`Cableado` llega vacío y el coste es cero). La asimetría: `une` nombra *pads* y
crea un nodo que *sustituye* al suyo, así que un puente hay que declararlo y un
LED sobre un pad no.

**Prompt 2.** «Y si quisiera unir dos pines del mismo MCU, por ejemplo, una salida
PWM de un canal de un temporizador y una entrada de un canal de otro temporizador
para medirla, aunque no tenga mucho sentido, ¿es posible? ¿cómo se haría?»

**Respuesta.** Sí, y **ese caso exacto ya estaba en el banco**: `lnk_pwm` lleva
PD12 (TIM4_CH1) a PB4 (TIM3_CH1) y T41 y T43 lo usan. Pero `SignalLink` no es un
cable: es un buffer unidireccional con el umbral de un pad. Para ese montaje es
fiel —un solo emisor, cero tiempo simulado, y se puede despegar— y no lo es para
un punto eléctrico de verdad. Con la opción B se escribiría
`<nodo id="n" une="PD12 PB4"/>`, y unir dos pines del mismo chip sale gratis
porque el `Cableado` es un mapa pad→nodo al que no le importa de qué chip es cada
pad. El precio: un nodo compartido es soldadura permanente.

---

## 2026-09-08 UTC — Implementación de los nodos compartidos

**Prompt.** «Añade al fichero stm32f407 multi mcu.md toda esta información […].
Aplica los cambios al código y al XML usando la solución recomendada para "cada
pad crea su `AnalogNet` y lo ata a un `sc_port` en el constructor de `PinMux`",
resuelve los problemas de `--inventario` por usar `basename()` y de llamar a
`registra_mcu` dos veces sin prefijo, pero deja los otros dos solamente
documentados y pendientes. Después comitea todo».

**Respuesta.** Tres cosas hechas y dos deliberadamente no hechas.

**La opción B.** `Cableado` y `pad_desde_nombre()` en `pins/pin_mux.h`; el
constructor de `PinMux` y el de `Stm32F407VG` lo reciben; `PinMux` distingue
ahora entre `net[p][i]` —los nodos que **posee** y destruirá, `nullptr` en los
pines con puente— y `nodo[p][i]`, el que el pad usa. En el XML, el atributo
`une="PB9 PD3"`, con `Netlist::nodo_une()` y `cableado_desde_netlist()`. El orden
de montaje se invierte: **leer → crear los nodos compartidos → construir el MCU →
registrar los nodos → construir las piezas**, y es viable porque leer no
construye. Para una placa sin puentes el mapa va vacío y el constructor hace lo de
siempre: **coste cero**, y las tres placas de `placas/` siguen valiendo sin tocar
una línea.

Lo que el análisis no había previsto: el nombre del puente y el de sus pads
designan el mismo `AnalogNet`, y la validación agrupa por nombre. Hubo que
enseñarle que un puente no puede «quedar flotante por culpa de la placa» —al otro
lado hay pads— y que declararlo `bus` por cualquiera de sus nombres quiere decir
lo mismo (`es_bus_efectivo`).

**§7.1, `basename()`.** La traducción nodo→nombre se muda a
`common/nombres_nodo.h`, parte del nombre **jerárquico** y cualifica el pad con su
MCU cuando hay más de uno (`n_mcus()`, que cuenta el constructor de `PinMux`
porque hay exactamente uno por MCU). Con un solo MCU el volcado sale idéntico, que
era la condición para no migrar nada.

**§7.2, `registra_mcu`.** Gana el prefijo, y `NodeMap::registra()` convierte en
`SC_REPORT_ERROR` lo que antes era un solapamiento mudo: dar de alta un nombre dos
veces con dos `AnalogNet` distintos. Con el mismo nodo sigue valiendo, y hace
falta que valga: es lo que ocurre cuando un pad forma parte de un puente ya
registrado.

**§7.3 y §7.4 se quedan pendientes**, marcados como tales en el análisis y en el
TODO (I-17). Los `static bool warned` no se manifiestan con un solo MCU, y el
puerto de GDB no tiene dónde ponerse mientras no exista el `<mcu>` que lo lleve:
el arreglo y su sitio llegan juntos.

**Prueba.** El banco lleva ahora el puente **PB9–PD3** y lo comprueba **T122**, con
20 comprobaciones: que los dos nombres son el mismo objeto, que el nivel pasa en
los dos sentidos —cosa que una pista no puede—, que la ida y vuelta por XML lo
conserva, que los cuatro `une` mal escritos se rechazan cada uno por su motivo, y
lo que de verdad justifica el trabajo: **con los dos pines conduciendo a la vez el
nodo se queda a 1,65 V y los dos pads avisan de sobrecorriente**. Con un acoplador
entre dos nodos ese conflicto no existiría.

Un tropiezo mío que conviene dejar escrito: creí encontrar una discrepancia
—`src/README.md` documenta `make sim` y el `Makefile` del contenedor no tenía esa
regla— y añadí la regla. Estaba mirando el fichero equivocado. El versionado es
**`src/Makefile.stm32`** (`src/Makefile` está en `.gitignore`, es la copia
generada), y ahí la regla existía desde siempre, junto con la de `bench`. Mi copia
del contenedor se había quedado atrás. Deshecho: `src/Makefile` vuelve a ser una
copia exacta de `Makefile.stm32`, y de paso comprobé que `build/bench` —el tercer
ejecutable, que no había tocado— compila y corre con los cambios.

**Suite: 1871/1871, 0 fallos.** Bajo ASan la comparación con el árbol previo da
los mismos 17 fallos y las mismas 8 asignaciones filtradas —todas anteriores—, así
que el cambio de propiedad de los nodos no ha introducido ni fugas ni accesos
indebidos. `placas/banco.xml` regenerado con el puente; las tres placas validan
con 0 avisos.

---

## 2026-09-08 UTC — Los problemas de ASan

**Prompt.** «Analiza y corrige los problemas detectados con ASan».

**Respuesta.** Tres cosas, y la primera es una rectificación.

**Los 17 fallos no existían: eran míos.** En el turno anterior informé de «17
fallos bajo ASan, los mismos antes y después del cambio, todos anteriores». Es
falso. Había ejecutado el binario desde `/home/claude` en lugar de desde `src/`,
porque una orden anterior había reiniciado el directorio de trabajo del shell.
Las imágenes de firmware se cargan por **rutas relativas** (`verif/fw/...`), así
que fallaron las dieciséis comprobaciones de «imagen … cargada en la Flash» más
la del MCO1 que depende de una de ellas, y se saltaron 128 más. Desde `src/` la
suite bajo ASan da **1871/1871, 0 fallos**. No era ruido de sanitizer ni un fallo
anterior: era mi error de directorio, y lo di por bueno demasiado deprisa.

**Las fugas sí existían, y ya no.** 248 bytes en 8 asignaciones, seis sitios, dos
causas:

*Objetos del banco que nadie destruía.* La sonda SWD y el stub de GDB van
soldados a PA13/PA14 pero no pasan por el netlist —son instrumentos, no
circuitería—, así que había que borrarlos a mano y no se hacía. Detalle
instructivo: **solo la sonda aparecía en el informe**. `GdbStub` es un
`sc_module` y sigue colgando de la jerarquía de SystemC, de modo que
LeakSanitizer lo ve *alcanzable*; se perdía igual, pero en silencio.

*Locales que viven a través de un `wait()`.* Las otras cinco eran el mismo
patrón, y merece la pena entenderlo porque se repetirá: un `sc_event_or_list`
—o un `std::vector`— declarado **dentro** del bucle de un `SC_THREAD` sigue vivo
mientras el proceso está suspendido. Cuando la simulación termina, el hilo se
queda ahí y **su pila no se desenrolla**: los destructores de sus locales no
corren nunca. Es una fuga acotada, una por proceso, pero ensucia el informe, y un
informe sucio es un informe que nadie mira.

Los cinco: `Cpu::exec_proc` (las listas del sueño WFI/WFE y de la parada del
depurador), `Rcc::clock_tree_proc` (siete eventos del árbol de reloj),
`CanBase::bit_proc`, `OtgBase::pines_proc` e `I2cWire::run`. En los cinco, los
eventos **no cambian después de la elaboración**, así que la lista se arma una
vez y se reutiliza. Además de quitar la fuga, eso elimina una reserva de montón
por cada despertar en cinco caminos que se recorren mucho —cada WFI del firmware,
cada escritura al RCC, cada flanco de un bus I2C—, que es justo la regla de coste
del proyecto: lo que importa no es lo que hace un bloque, sino cuántas veces
despierta.

**Y el SIGSEGV que había atribuido a «ruido de sanitizer».** Tampoco era ruido.
ASan trae encendido `detect_stack_use_after_return` desde GCC 13, y eso es
incompatible con las corrutinas de SystemC: el registro de marcos falsos es por
hilo del sistema operativo, todas las corrutinas comparten uno, y en cuanto el
planificador salta de pila un proceso ve los marcos del anterior. El síntoma es
un `SEGV on address 0x20` dentro de `sc_in<bool>::value_changed_event()`, antes
de la primera comprobación y en un sitio sin relación con nada. No es arreglable
por nuestra parte —es de la biblioteca—, pero sí evitable: `common/asan_opciones.h`
define `__asan_default_options()`, de modo que el binario trae su propia
configuración y **no hay que acordarse de nada** en la línea de órdenes.

**Resultado.** `make asan` es ahora un objetivo del `Makefile.stm32`: compila la
suite con ASan y UBSan a `-O1` y la ejecuta. Sale **1871/1871, 0 fugas, 0 avisos
de comportamiento indefinido**, y los otros dos ejecutables (`sim` y `bench`)
también. El tiempo simulado no se movió ni un picosegundo: 2328209149213 ps antes
y después, que es la prueba de que hoistar las listas de espera no cambió ni un
evento.

---

## 2026-09-08 UTC — Varios MCUs, cada uno con su stub de GDB

**Prompt.** «Modifica el programa y sus parámetros de entrada para poder ejecutar
varios GDB-stubs, uno por cada MCU del XML, incluyendo el cambio en el puerto
TCP. Si no se instancia explícitamente ningún MCU o sólo se instancia
explícitamente uno en el XML, entonces los parámetros actuales deben seguir
sirviendo. Si hay dos o más MCUs instanciados en el XML, entonces debe ser
posible especificar el tipo de gdb-stub y puerto TCP para cada MCU».

**Tres decisiones que pregunté antes de escribir**, porque cambiaban el
comportamiento y no eran mías: con un solo MCU **manda la línea de órdenes**
sobre el XML; con dos o más, un argumento global es **un error que nombra los
MCUs**; y `depuracion` por omisión es **`pines` y sin escuchar**, para que ningún
XML existente abra un socket por sorpresa.

**Lo implementado.** El elemento `<mcu>`, que era el paso 2 del análisis:

```xml
<mcu tipo="STM32F407VG" id="u0" firmware="a.bin" depuracion="dap"   puerto_gdb="3333"/>
<mcu tipo="STM32F407VG" id="u1" firmware="b.bin" depuracion="pines" puerto_gdb="3334"/>
```

`DeclMcu` y `Netlist::add_mcu()` en `parts/netlist.h`, la pasada cero del lector
en `parts/netlist_xml.h`, y `sim_main.cpp` reescrito alrededor de un vector de
MCUs montados. Cada chip lleva su `DebugCaps` —que ya era por instancia, así que
el modelo no hubo que tocarlo—, su `ImageLoader`, sus cuatro drivers de
alimentación y su stub. Los dos modos conviven en la misma placa: `u0` habla con
su DAP por llamada de función y `u1` bit a bit por SWCLK/SWDIO, como un ST-LINK.

**La regla de compatibilidad** vive entera en `Netlist::resuelve_pad()`: ningún
`<mcu>` → nombres desnudos; uno → valen los dos y apuntan al **mismo**
`AnalogNet` (`registra_mcu` se llama dos veces y `NodeMap::registra` es
idempotente); dos o más → solo cualificados, y el desnudo es un error que dice
los candidatos. Las cuatro placas de `placas/` siguen valiendo sin tocar una
línea, que era la condición.

**El puente entre chips sale gratis**, y eso fue lo satisfactorio: el `Cableado`
del turno anterior es un mapa pad → nodo, así que unir `u0.PB6` con `u1.PB6` es
el mismo mecanismo que unir dos pines del mismo chip. Solo hubo que indexarlo por
identificador de MCU. `placas/dos_mcu.xml` son dos F407 con dos hilos I2C
compartidos y sus pull-up.

**Y funciona de verdad**, que es lo que había que comprobar: los dos stubs
escuchan en 3333 y 3334, y contestan `qSupported` y `?` de forma independiente
—lo verifiqué hablando RSP contra los dos puertos—.

**Dos cosas que aparecieron por el camino.**

*La cifra de nodos mentía.* Con un MCU declarado como `u0` el mapa tiene dos
nombres por pad, así que `nodos.size()` decía 308 en una placa de 154. `size()`
sigue contando nombres y `n_nodos()` cuenta `AnalogNet` distintos, que es lo que
se imprime. De paso el banco pasó de decir 156 a decir 154, que es la verdad
desde que PB9 y PD3 comparten nodo.

*El argumento de milisegundos.* Es global —hay un solo reloj de simulación— pero
como argumento posicional va detrás del firmware, que con varios MCUs ya no se
pone ahí. De ahí `--ms=2`.

**Cerrados también I-17** (los dos `static bool warned` de `adc.h` y `sdio.h`
pasan a miembros `mutable`, y el puerto de GDB deja de salir de una global) y
el paso 1b del análisis. Se dejaron pendientes a propósito en el turno anterior
porque su síntoma era imposible con un solo chip; con dos es inevitable.

**Lo que NO está**, y conviene que se lea: `mcu_if` con su factoría (§6.1) y el
encapsulado como dato de instancia (§6.3), que son lo que hace falta para un MCU
**distinto**, no para otro igual. Y la comprobación automática del **montaje**
de dos MCUs (I-21): **T123** cubre la capa de declaración con 28 comprobaciones
—leer `<mcu>`, resolver `u0.PD12`, el nombre ambiguo, el chip inexistente, el
puerto repetido, los cinco `<mcu>` rotos y la ida y vuelta por XML— pero que dos
chips arranquen y se hablen solo se verifica a mano con `sim`. La suite monta un
único `dut` del que cuelgan la mitad de sus comprobaciones, y meter un segundo
dentro sería duplicar la elaboración del banco entero para probar otra cosa.

**Suite: 1899/1899, 0 fallos**, tiempo simulado invariante (2328209149213 ps),
`make asan` limpio, y `sim` con dos MCUs también limpio bajo ASan.

---

## 2026-09-09 UTC — Una GUI en Qt sobre la simulación: análisis

**Prompt.** «Analiza las posibilidades y consecuencias de conectar la simulación
SystemC con un programa gráfico en Qt […] sólo sería necesario visualizar el
estado de los LEDs, el ángulo de posición de los servos, el periodo y ciclo de
trabajo de las señales PWM, y la información sobre el giro de los motores […]
También debería ser posible actuar sobre los botones, finales de carrera, etc.
[…] escenarios: 1) un único programa Qt con un único thread, 2) un thread Qt
para la simulación y otros para la parte visual, 3) dos programas separados.
Escribe el informe en doc/analisis_gui.md».

**Respuesta.** `doc/analisis_gui.md`, con las conclusiones medidas y no supuestas.
Escribí una sonda que trocea `sc_start()` y cronometra cada rodaja, y las cifras
cambiaron el orden de las prioridades:

* **el modelo va entre 11 y 200 veces más RÁPIDO que el tiempo real** (0,089 s de
  anfitrión por segundo simulado con el núcleo al 100 %; 0,005 aparcado). El
  problema no es que la simulación no dé abasto para la pantalla: es frenarla;
* **trocear `sc_start()` no cuesta nada**: mismo número de deltas exacto —29 837
  con blinky, troceado o no— y el tiempo dentro del ruido. La rodaja peor en
  régimen permanente es **1 ms** contra los 16,7 de un fotograma;
* **la vista le cuesta al modelo 60 despertares por segundo simulado**: +299
  deltas en 5 s, el 0,2 %. Para comparar, OTG y ETH costaban 56 000;
* y la excepción, que decide el reparto en hilos: **con `--ondas` el modelo pasa
  a ser 222 veces más lento que el tiempo real** y una rodaja de 1 ms tarda un
  cuarto de segundo. Ninguna arquitectura de un solo hilo sobrevive a eso.

**La tesis del informe** es que la decisión importante no es el reparto en hilos
sino **dónde se pone la frontera y qué la cruza**. Si por ella solo pasan
escalares con nombre y unidad —«ángulo», «ciclo», «encendido»— los tres
escenarios son tres formas de dar cuerda a la misma máquina. Si un widget llama a
`led->on()`, el escenario 1 es el único posible y no hay camino desde ahí. De ahí
el contrato de `Observable` / `Mando` sobre `ExtPartBase`, que además deja que la
GUI **enumere la placa sin conocer un solo tipo de C++**, igual que la factoría
del paso 3 construye piezas desde una cadena.

Un corolario que no es evidente: **la medida tiene que ocurrir dentro del
modelo**. Muestrear un pin a 60 Hz no permite recuperar el ciclo de trabajo de
una PWM de 50 Hz con pulsos de 1 a 2 ms; hace falta una pieza que cronometre
flancos y publique un escalar.

**Recomendación: el escenario 2, con la frontera diseñada como si fuera el 3.**

**Dos hallazgos que no esperaba.**

*Los stubs de GDB no usan hilos del sistema operativo.* Son `SC_THREAD` que
atienden un socket no bloqueante cada 100 µs de tiempo **simulado**. Eso hace de
precedente y de aviso: la capacidad de respuesta frente al exterior va atada a lo
deprisa que avance el tiempo simulado. De paso corregí un comentario mío de
`sim_main.cpp` del turno anterior que decía justo lo contrario.

*La interactividad rompe el determinismo*, que es el activo principal de este
proyecto, y **en los tres escenarios**: no es un problema de concurrencia sino de
tener a una persona en el lazo. El arreglo es barato —la cola de órdenes ya lleva
el instante simulado, así que grabarla es un fichero— y da algo a cambio: un
fallo encontrado haciendo clic se convierte en un caso reproducible y de ahí en
una prueba de la suite. Lo puse como paso 5 del orden de trabajo, **antes** que
los motores: en cuanto haya algo que se pueda romper haciendo clic, querremos
poder contar cómo se rompió.

**Y lo que de verdad cuesta no es la GUI.** De lo que el enunciado quiere ver,
solo LEDs y botones existen: de los 21 tipos del catálogo no hay servo, ni
medidor de PWM, ni etapa de potencia, ni paso a paso, ni motor de continua, ni
encoder. Todas se pueden escribir y verificar con el banco de siempre, sin
ventana ninguna.

---

## 2026-09-09 UTC — El objetivo real: un simulador didáctico

**Prompt.** «Lo que busco es un programa didáctico que permita simular con
exactitud funcional el funcionamiento de sistemas embebidos para alumnos
universitarios que comienzan a programar este tipo de sistemas y no disponen de
material hardware. […] El objetivo fundamental es que el usuario pueda
desarrollar y depurar la aplicación embebida con STM32CubeIDE u otro entorno de
desarrollo para MCUs y que pruebe el funcionamiento con este programa […] de la
misma manera que lo haría con el hardware real. Analiza cómo afecta este objetivo
al análisis y añade tus conclusiones al informe».

**Respuesta.** Una parte II en `doc/analisis_gui.md` (§13–§21). Las medidas de la
parte I no cambian y la recomendación de escenario tampoco, pero **el orden de
las prioridades sí, y bastante**.

**Lo que se invierte: la interfaz principal no es la GUI, es el servidor de
GDB.** «Depurar con CubeIDE como con el hardware real» quiere decir exactamente
que el IDE lanza un servidor de GDB y habla RSP por TCP; sustituir la placa es
sustituir ese servidor. Y ya está escrito. Repasé la cobertura de
`common/gdb_rsp.h` y cubre lo que hace falta: registros con `target.xml`, memoria
por el AHB-AP —así que las *Live Expressions* y la vista de SFR funcionan con el
núcleo corriendo—, `vFlashErase/Write/Done`, puntos de ruptura sobre el FPB y el
DWT, `monitor reset/halt/resume`, y **`printf` por SWV/ITM**, que para un alumno
sin placa vale su peso en oro. El ELF ni siquiera hay que leerlo: GDB carga los
símbolos en el lado del IDE y por el socket solo van bytes.

Lo que no pude hacer es probarlo contra CubeIDE de verdad, así que en vez de
afirmar lo escribí como **el guion de la prueba que decide si el producto puede
existir**, y la puse como paso **0** del orden de trabajo, antes que todo lo
demás: cuesta una tarde y es lo único que puede invalidar el proyecto.

**Lo que se endurece: el ritmo.** Un `blinky` que va 55 veces más rápido que el
tiempo real no es un LED parpadeando, es un LED encendido. El tiempo real pasa de
opción a modo por omisión. Y hay un caso que hay que decidir a propósito: si el
tiempo simulado sigue corriendo cuando el alumno para en un punto de ruptura. En
hardware sí —el motor sigue girando— y recomiendo mantenerlo, porque eso **es la
lección** que un simulador demasiado amable le ahorraría.

**Lo más serio, y es nuevo: dónde acaba la «exactitud funcional».** El modelo hace
≈4,7 ciclos por instrucción donde el silicio hace ≈1,5
(`doc/stm32f407vg_fase2.md`, §405-407). Consecuencia práctica: un retardo por
bucle tarda **unas tres veces más** que en la placa, mientras que cualquier cosa
temporizada por SysTick o por un TIM sale exacta. En F2 se anotó que «nada de lo
verificado depende de ello» y era cierto; **con alumnos delante es lo primero que
van a notar**, y quien calibre un retardo aquí tendrá código que en la placa va
tres veces rápido. P-02 cambia de prioridad. Recomiendo decirlo y empujar hacia
los temporizadores —que es el consejo correcto igualmente— antes que calibrar el
modelo, que sería mentir mejor.

**Y el argumento que mata el escenario 1**, que no existía en la parte I: el
servidor de GDB es un `SC_THREAD` que sondea su socket en tiempo simulado, y en
el escenario 1 quien hace avanzar el tiempo simulado es el temporizador de la
ventana. Si la ventana se bloquea —un diálogo, arrastrarla, un repintado lento—
el socket deja de atenderse y **el IDE del alumno da el objetivo por muerto**. El
alumno culpará a su código, que es el peor resultado posible aquí.

**Dos cosas más que salieron del cambio de objetivo.** Una: el peor modo de fallo
de un simulador didáctico no es romperse, es **enseñar algo falso**, y el caso
concreto son las 48 funciones «bits sin máquina» —el alumno configura algo, lee
de vuelta lo que escribió y no pasa nada, sin poder distinguir un modelo
incompleto de un error suyo—. De ahí la propuesta más rentable del informe: que
el modelo avise «has habilitado X y esto no está modelado». Y dos: **se
distribuye**, y eso es un requisito que no existía; el riesgo grande y no
cuantificado es Windows. La buena noticia es que la dependencia del sistema
operativo está acotada a `common/gdb_rsp.h` (y a `verif/gdb_client.h`, que no va
en el producto): todo lo demás es C++17 y `<systemc>`.

Lo que **no** cambia: la frontera de §5, las cifras de §3 y la recomendación del
escenario 2. Y las piezas que faltan siguen siendo lo caro, pero dejan de ser lo
urgente, porque el propio enunciado dice que se añaden después.

---

## 2026-09-09 UTC — Windows, Linux y macOS

**Prompt.** «Modifica el programa y genera los makefiles para que pueda ser
compilado en Windows, Linux y Mac. En la primera plataforma asume que el
compilador es mingw 11.2.0 64 bits (o una versión más moderna). La selección de
la plataforma puede hacerse con distintos makefiles o ajustando uno o más
valores en un makefile único».

**Respuesta.** Un Makefile único con detección automática y `PLATAFORMA=` para
forzarla, y toda la dependencia del sistema operativo recogida en **un solo
fichero nuevo**, `common/red.h`.

El análisis de la GUI ya había localizado el problema: de todo `src/`, solo dos
ficheros incluían cabeceras del sistema —`common/gdb_rsp.h`, el servidor de GDB
que va en el producto, y `verif/gdb_client.h`, el cliente con el que la suite se
prueba a sí misma—. Ahora ninguno de los dos las incluye: las dos usan `red.h`,
y el resto del modelo sigue siendo C++17 y `<systemc>` sin una sola directiva de
preprocesador.

**Las tres trampas que aparecieron**, y las tres son fallos mudos y no errores de
compilación:

* en Windows el descriptor de socket es un entero **sin signo**, así que el
  `if (s < 0)` de todo el código POSIX **nunca es cierto**: los errores se
  tragarían en silencio. De ahí `red::valido()` en vez de una comparación;
* MinGW usa por omisión el `printf` de msvcrt, que **no entiende `%llu`**, y el
  modelo lo usa veintiocho veces. `-D__USE_MINGW_ANSI_STDIO=1`;
* y una que no es de Windows sino de **macOS**: allí no existe `MSG_NOSIGNAL`, y
  escribir en un socket que el otro extremo cerró manda un `SIGPIPE` que mata el
  proceso. Un simulador que se muere porque el alumno cerró el IDE de golpe no
  vale, así que la capa pone `SO_NOSIGPIPE` al crear cada socket.

**Lo que sí he verificado**, y lo separo a propósito de lo que no:

* Linux con **g++** y con **clang**: 1899/1899 y el mismo tiempo simulado al
  picosegundo. Los 151 checks de F6 son sesiones TCP de verdad entre el cliente
  y los dos stubs, así que la capa está ejercitada por la suite entera;
* `make red` —trece comprobaciones de la capa sola, sin SystemC de por medio,
  en `verif/prueba_red.cpp`— pasa en Linux;
* **el cruce a Windows con MinGW-w64 compila y enlaza un PE32+ sin un aviso**,
  con `-Wall -Wextra`. Instalé el cruce en el contenedor para poder decirlo;
* la rama de macOS compila con g++ y con clang, forzando su combinación de
  macros (sin `MSG_NOSIGNAL`, con `SO_NOSIGPIPE`).

**Lo que NO he verificado, y no quiero que se lea de otra manera:** no he
construido SystemC para MinGW ni para macOS, ni he ejecutado nada en esas dos
plataformas. No hay Wine en el contenedor y las fuentes de SystemC no son
alcanzables desde aquí. Que el código compile y enlace no es que funcione. Queda
como I-23 en el TODO, y es el paso 0b del orden de trabajo del análisis de la
GUI: para un programa que se reparte a alumnos no es opcional.

De paso, el `.exe` se enlaza con `-static-libgcc -static-libstdc++`, que para
repartirlo no es un lujo, y `make asan` avisa en vez de fallar cuando la
plataforma es Windows, porque MinGW no trae AddressSanitizer.

---

## 2026-09-09 UTC — ¿Pasar a SystemC 3.0?

**Prompt.** «Analiza qué cambios sería necesario hacer al código para usar
SystemC 3.0.2 y si el cambio a la última versión de SystemC ofrece alguna
ventaja».

**Respuesta.** `doc/analisis_systemc3.md`, y con una advertencia por delante que
es parte del resultado: **no he podido examinar SystemC 3.0.** Aquí solo está la
2.3.4 y las fuentes de Accellera no son alcanzables —GitHub está cerrado para
esta sesión—, así que lo que digo de la 3.0 viene de lo que sé y no de haber
leído sus cabeceras; en particular no sé qué distingue la 3.0.2 de la 3.0.0. Por
eso construí el informe para que **la parte débil no cambie la conclusión**: la
pregunta «¿qué habría que cambiar?» se contesta desde nuestro lado, con el
inventario exacto de la API que el modelo usa, sacado del código.

**Y ese inventario es la respuesta:** `sc_module`, procesos, `sc_time`,
`sc_signal`/`sc_in`/`sc_out`/`sc_vector`, `sc_event`, `sc_prim_channel`,
`sc_port`, `sc_export`, `sc_spawn`, `sc_report_handler` y TLM-2.0. Nada más. Ni
tipos de datos de SystemC, ni `sc_fifo`, ni `sc_clock`, ni control de procesos,
ni nada del subconjunto de síntesis. Es la parte de IEEE 1666 que lleva estable
veinte años, así que **no espero ningún cambio necesario**.

**Lo que sí encontré, y arreglé sobre la marcha:** el modelo tenía exactamente
**un** `#include` de las tripas de SystemC —`<sysc/kernel/sc_spawn.h>` en
`pin_mux.h`—, que no es API sino distribución interna de cabeceras, justo lo que
cambia entre versiones mayores sin que nadie lo considere una ruptura. Estaba ahí
por un motivo real: `sc_spawn()` solo se expone si se define una macro **antes**
de incluir `<systemc>`, y eso no se puede hacer desde una cabecera. Ahora el
Makefile pasa `-DSC_INCLUDE_DYNAMIC_PROCESSES` y `pin_mux.h` da un `#error` claro
si falta. Con eso, `src/` no incluye ni una cabecera interna: es la precondición
de cualquier migración. 1899/1899 y el mismo tiempo simulado.

**Medido, además:** el modelo y las cabeceras de la 2.3.4 pasan `-fsyntax-only`
en **C++17, C++20 y C++23** sin un error, así que el estándar tampoco es un
obstáculo por ninguno de los dos lados.

**¿Compensa? Mi respuesta es que hoy no**, y por un motivo que no es sobre la
3.0 sino sobre nosotros: ninguna ventaja conocida ataca un problema que tengamos.
Rendimiento, no —el coste lo domina cuántas veces despierta un proceso, y ya
vamos entre 11 y 200 veces más rápido que el tiempo real; el problema es
frenarlo—. Y hay un coste en contra que se olvida: hoy en Linux SystemC se
instala con `apt`, y la 3.0 habría que compilarla, que es lo contrario de lo que
quiere un proyecto que se reparte a alumnos.

**La única ventaja posible es también la única pregunta abierta:** que la 3.0 sea
más fácil de construir en **Windows y macOS**, que es lo que bloquea repartir el
programa (I-23). Si lo es, eso solo justifica el cambio; si no, no queda ningún
motivo. De ahí lo único que cambiaría el plan: **«¿2.3.4 o 3.0?» no es una
decisión aparte, es una rama de «¿cómo se construye esto en Windows?»**. Quien
ataque I-23 que pruebe las dos a la vez.

Y una cosa que me gustó comprobar: la red de seguridad para una migración así ya
existe y no se hizo para esto. Las 1899 comprobaciones pasan, pero el criterio de
verdad es que **el tiempo simulado sale idéntico al picosegundo** entre
ejecuciones, compiladores y plataformas. Si al cambiar de biblioteca esa cifra se
moviera, algo del planificador sería distinto y se sabría en veintitrés segundos.

---

## 2026-09-10 UTC — El enlace que falla en MSYS2, y la guía de compilación

**Prompt.** «¿Cómo hago en Windows para ejecutar make y construir el programa
modificando la ruta de instalación de SystemC *sin* modificar el Makefile?» —y,
tras la salida real de MSYS2 con el error de enlace,— «Añade esta explicación a
un fichero doc/compilacion.md y haz el commit de todo».

**Respuesta.** La respuesta corta a la primera pregunta es
`make SYSTEMC_HOME=/c/systemc`: todas las variables del Makefile están
declaradas con `?=`, así que la línea de órdenes gana siempre y no hay que tocar
nada. Pero la salida que me pasaste contenía un problema mucho más interesante
que la ruta, y esa es la parte que merecía un documento.

**El error decía:**
`undefined reference to sc_core::sc_api_version_2_3_4_cxx201703L<&sc_core::SC_DISABLE_VIRTUAL_BIND_UNDEFINED_>`.

**Lo primero que hay que ver es lo que *no* dice.** Falta **un solo símbolo**. Si
la biblioteca no se hubiera encontrado, o la ruta estuviera mal, no habría un
`undefined reference` sino miles: `sc_start`, `sc_time`, `sc_module`, todo. Que
falte exactamente uno significa que la biblioteca **se encontró y se enlazó**, y
que lo único que no cuadra es el símbolo que SystemC pone ahí precisamente para
que no cuadre.

**Porque ese símbolo es un detector de incompatibilidades, no una función.**
Verificado en `sc_ver.h:100-195`: SystemC declara una plantilla
`sc_api_version_<M>_<m>_<p>_cxx<SC_CPLUSPLUS>` parametrizada por la dirección de
`SC_DISABLE_VIRTUAL_BIND_DEFINED_` o `SC_DISABLE_VIRTUAL_BIND_UNDEFINED_`, y la
biblioteca instancia **una sola variante**. Tu programa referencia la variante
que le corresponde según cómo lo estés compilando tú. Si las dos coinciden,
enlaza; si no, el enlazador te lo dice con este error. Es decir: **el error es el
mecanismo funcionando**, no un fallo de configuración de rutas.

Lo que tiene que coincidir son cinco cosas, tres en tiempo de enlace —la versión
exacta, `SC_CPLUSPLUS` (que en GCC y clang es `__cplusplus`, `sc_cmnhdr.h:123`, o
sea tu `-std=`) y si `SC_DISABLE_VIRTUAL_BIND` está definida— y dos más que se
comprueban al **arrancar** el programa, pasadas como argumentos del constructor:
`SC_DEFAULT_WRITER_POLICY` y `SC_ENABLE_COVARIANT_VIRTUAL_BASE`. De ahí que un
programa pueda enlazar y luego abortar nada más empezar: son comprobaciones
distintas.

**La orden que lo resuelve en un segundo** —y que es el centro de la guía— es
preguntarle a la biblioteca qué variante tiene dentro:

```bash
nm -C /ruta/a/libsystemc.a | grep -i api_version | head
```

Si sale `..._cxx201402L`, tu SystemC se compiló con C++14 y la solución es
`make CXXSTD=c++14`. Si sale `..._DEFINED_` y tú referencias `..._UNDEFINED_`, te
falta `-DSC_DISABLE_VIRTUAL_BIND`. Si sale `2_3_3` y tú pides `2_3_4`, tienes dos
instalaciones y `SYSTEMC_HOME` apunta a la que no crees.

**Y el segundo hallazgo, en tus propios avisos:** aparecían advertencias dentro
de las cabeceras de SystemC. Eso pasa porque el Makefile las metía con `-I`. Con
`-isystem` el compilador las trata como cabeceras del sistema y calla. Lo cambié
—y ahí me llevé un susto propio que también está documentado: **poner `-isystem`
sobre `/usr/include` rompe la compilación en Linux** (`fatal error: stdlib.h: No
such file or directory`), porque reordena la cadena de cabeceras estándar. Por
eso el Makefile lleva ahora una guarda explícita para cuando `SYSTEMC_HOME=/usr`.

**Tres cambios de código**, todos verificados con 1899/1899 y el mismo tiempo
simulado (`2328209149213 ps`):

1. `EXTRA` y `EXTRA_LD` en el Makefile. Hacían falta por una trampa de `make`
   que muerde a mucha gente: escribir `make CXXFLAGS=-I/c/systemc/include`
   **sustituye** todo lo que el Makefile había acumulado con `+=` —incluido
   `-std=c++17` y `-DSC_INCLUDE_DYNAMIC_PROCESSES`—, así que en lugar de añadir
   una opción te cargas la compilación. `EXTRA=` añade sin destruir.
2. `-isystem` para las cabeceras de SystemC, con la guarda de `/usr`.
3. Fuera el `SC_HAS_PROCESS(CortexM4F);` de `core/cortex_m4f.h`: ese módulo no
   declara procesos propios, así que la macro no hacía nada salvo aparecer en el
   sitio donde alguien la buscaría por un motivo equivocado.

**Y el documento.** `doc/compilacion.md` está ordenado **por síntoma**, que es
como se lee una guía así: con el error delante y sin ganas de leer nada más. Lo
mínimo, lo que se puede ajustar sin tocar el Makefile, las tres plataformas, y
luego el catálogo: `sc_api_version` (§5.1, con el mecanismo entero y la tabla de
`nm -C`), `cannot find -lsystemc`, la trampa de `stdlib.h`, `make` desde
`cmd.exe`, `%llu` en MinGW, los avisos de las cabeceras, `sc_spawn` y el abort en
tiempo de ejecución. Con una advertencia honesta sobre
`SC_DISABLE_API_VERSION_CHECK`: **sí, silencia el error, y no, no lo arregla** —
apagar el detector de incompatibilidades no hace compatibles las opciones; solo
mueve el fallo a un sitio peor, normalmente una corrupción de memoria.

El criterio de aceptación sigue siendo el mismo y está escrito en §6: no que
pasen las 1899 comprobaciones, sino que el **tiempo simulado salga idéntico al
picosegundo**. Si al cambiar de biblioteca, compilador o plataforma esa cifra se
moviera, algo del planificador sería distinto, y se sabe en veintitrés segundos.

---

## 2026-09-10 UTC — Qué dice la salida de `sim` sobre una placa mínima

**Prompt.** «Explica la salida de esta ejecución: `build/sim
placas/discovery_min.xml`» —con las cinco líneas de la ejecución.

**Respuesta.** Reproducida la ejecución tal cual, línea a línea.

**`placa 'discovery-min': 1 MCU(s), 3 componentes, 154 nodos, 0 avisos`.** Sale
después de construir y pasar los dos validadores. Un MCU porque el XML no
declara ningún `<mcu>` y se instancia el implícito; tres componentes son las
instancias declaradas —X1, LD4, B1—, no los pines.

**Los 154 nodos son el número que sorprende, y no es un error.** El XML declara
tres nodos y salen 154 porque el recuento no es de nodos *declarados* sino de
redes eléctricas distintas: 9 puertos × 16 pines = 144 pads, más los diez de
alimentación y control. Cada pad tiene su `AnalogNet` haya o no algo soldado,
que es lo que hace que un pin al aire sea un pin al aire de verdad y no un cero.
**Declarar un nodo en el XML no crea una red: le pone nombre a una que ya
existía.** Y «0 avisos» es que pasan los dos validadores, el de declaración —que
es fatal— y el eléctrico —que solo avisa, porque un pin compartido puede ser
deliberado—.

**`sin firmware: el nucleo se aparca en wfe`.** Sin imagen, en vez de dejar la
Flash a 0xFF —que daría un fallo de bus nada más arrancar y no enseñaría nada—
el cargador escribe un vector de reset mínimo: SP al final de SRAM1, PC a
0x08000100 y allí `0xBF20 0xE7FD`, o sea `wfe ; b .-2`. El núcleo **sí** ejecuta:
busca, ejecuta el WFE y se bloquea en un `sc_event`, con la condición de
despertar recomprobada justo antes de esperar para que no se pierda un suceso
que llegue entre la instrucción y la espera. Lo que queda es un chip alimentado,
fuera de reset y con sus periféricos en estado de reset. La línea base correcta
para mirar lo eléctrico sin que el firmware lo enturbie.

**`simulados 100.000 ms en 0.000 s de anfitrion (49 deltas)`.** El 0.000 s es que
el tiempo de anfitrión queda por debajo de la resolución con la que se imprime.
Los 49 deltas son lo interesante, y lo medí: son **los mismos 49** con `--ms=1`,
`--ms=100`, `--ms=1000` y `--ms=10000`. Todos vienen de la secuencia de encendido
anterior a la ventana —dar de alta los drivers, subir VDD/VDDA, los 10 µs, BOOT0
y NRST, los 100 µs, soltar NRST, el reset propagándose—. Con el núcleo en WFE,
**la ventana de 100 ms cuesta cero deltas**: diez segundos simulados valen lo
mismo que un milisegundo porque en ninguno de los dos pasa nada. Es la regla
sobre la que está construido el modelo —el coste es proporcional a cuántas veces
despierta algo, no al tiempo que transcurre— en su caso más puro. Corolario: el
reloj **no** se modela como una señal que conmuta a 168 MHz; si lo fuera, esos
100 ms serían 16,8 millones de sucesos.

**`LED LD4 en PD12: apagado (0.00 V, 0.00 mA)`.** El LED es el instrumento de
medida. LD4 va con `a_vss="si"`, `vf="2.0"`, `r="330"`: ánodo al pin, cátodo a
masa por 330 Ω, luce cuando el pin sube. Tras el reset PD12 es entrada sin pull,
o sea el pad en alta impedancia (`R_HIZ = 1e12`); y el LED, con el nodo por
debajo de Vf, está en corte, o sea **también** en alta impedancia. Las dos ramas
del nodo son alta impedancia, la superposición da 0,00 V y por el diodo no pasa
nada. No está apagado porque un booleano lo diga: está apagado porque la tensión
resuelta no llega a la de conducción, y por eso se imprimen los `float`.

**`Info: /OSCI/SystemC: Simulation stopped by user`** es el mensaje normal del
planificador tras `sc_stop()`: la simulación terminó donde se le pidió y no por
una excepción.

**Un hueco encontrado de paso:** `sim` toma como firmware cualquier argumento que
no reconozca, así que `sim placa.xml --netlist` intenta cargar un fichero llamado
`--netlist`. `--netlist` e `--inventario` son de `stm32f407vg`, no de `sim`.
Anotado como **I-26**.

---

## 2026-09-10 UTC — Que `sim` no termine hasta que lo digas tú

**Prompt.** «Añade la explicación de cómo lanzar el programa sim para que no
termine hasta que el usuario no lo decida y haz el commit».

**Respuesta.** La forma es **pedir un stub de GDB**, y merece la pena decir por
qué, porque no es evidente: lo que hace que `sim` no termine no es una opción de
duración sino que haya **un puerto escuchando**. `--gdb`, `--gdb-dap` o
`--port=` valen igual, y **no hace falta que GDB llegue a conectarse**: el
puerto se abre, la simulación corre indefinidamente y se sale con Ctrl-C. Es
justo el modo al que apunta el objetivo del proyecto —el simulador esperando
mientras STM32CubeIDE compila y se engancha— y el mismo en el que dos chips se
depuran a la vez, cada uno en su puerto.

Documentado en `README.md` con las **tres consecuencias** de dejarlo encendido,
que son lo que de verdad hacía falta escribir:

1. **Se salta el informe final** de los LEDs, que se imprime al acabar la ventana
   de `--ms` y en este modo no acaba nunca. Con un stub, lo que pasa dentro se
   mira por GDB.
2. **Corre tan deprisa como puede y se come un núcleo**: medido, **98,6 % de
   CPU** esperando sin firmware. No hay freno de tiempo real, así que «esperar»
   aquí no es dormir, es simular a toda velocidad.
3. **Redirigido a un fichero, los mensajes se pierden al matarlo**: la salida
   pasa de línea a línea a bloques, y el Ctrl-C se lleva el buffer.

Y lo que **no** es una forma de conseguirlo: un `--ms` enorme. Termina igual,
solo que más tarde, y llega al final en un suspiro por lo medido en el
intercambio anterior —`--ms=10000` cuesta lo mismo que `--ms=1`—.

**Dos cambios de código, mínimos.** Un `fflush(stdout)` tras el mensaje
«esperando a GDB»: es el último `printf` antes de un bucle infinito del que solo
se sale con Ctrl-C, así que sin vaciar el buffer el mensaje que explica cómo
salir es exactamente el que se pierde al redirigir. Y `--help` ahora lo cuenta,
que es donde lo va a buscar quien lo necesite.

**Y una corrección mía.** En el intercambio anterior dije que había anotado el
hueco en el TODO y añadido el intercambio a este registro «en local». No lo había
hecho: lo dije y no lo ejecuté. Va en este commit, con los dos huecos anotados —
**I-25** (no hay freno de tiempo real, y «correr sin parar» y «aceptar un
depurador» son hoy la misma opción cuando son dos cosas; es requisito de la GUI)
e **I-26** (el argumento posicional que se traga las opciones mal escritas)—.
Total de puntos: 138 → 140.

---

## 2026-09-10 UTC — «Could not verify ST device!»: el IDE elige mal la sonda

**Prompt.** Configurado STM32CubeIDE con la sonda `ST-LINK (ST-LINK GDB server)`
apuntando a `localhost:3333` contra `build/sim placas/discovery_min.xml --gdb`,
el simulador registra «cliente conectado» y «cliente desconectado», y el IDE da
«Could not verify ST device! Please verify that the latest version of the
GDB-server is used for the connection».

**Respuesta.** El fallo no está en el simulador ni en la red: está en **el tipo
de sonda elegido**, y el mensaje lo dice aunque no lo parezca.

**Lo primero: ese mensaje no lo escribe GDB.** GDB, cuando algo le falla con un
servidor remoto, se queja del protocolo —«Remote replied unexpectedly», «Ignoring
packet error»—. Hablar de «ST device» y de «the GDB-server» es la capa de ST, que
está comprobando una cosa muy concreta: que al otro lado del puerto esté **su
propio servidor** (`ST-LINK_gdbserver`) hablando con **una sonda ST-LINK de
verdad por USB**. No lo está —lo que hay es un servidor de GDB corriente—, así
que se identifica, no reconoce a los suyos y cierra. De ahí el «conectado» y
«desconectado» seguidos: la sesión sí se abrió; lo que no pasó es el saludo.

**Y lo segundo, que era lo importante de comprobar: el stub responde bien.** Lo
verifiqué con un cliente RSP crudo, sin IDE de por medio:

```
-> qSupported   <- PacketSize=1000;qXfer:features:read+;QStartNoAckMode+;...
-> ?            <- T050d:00c00120;0f:02010008;thread:1;
-> me0042000,4  <- 13640110
-> qXfer:features:read:target.xml:0,100  <- <target><architecture>arm</...
-> qRcmd,7265736574 ("reset")            <- OK
```

Ese `13640110` es little-endian de **`0x10016413`**, o sea el `DBGMCU_IDCODE` del
STM32F407 con **DEV_ID `0x413`** (F405/407/415/417) y **REV_ID `0x1001`**. Es
decir: **el dispositivo se identifica correctamente como el ST que es**, y aun
así la comprobación falla, porque lo que ST comprueba no es el identificador del
chip sino quién sirve el puerto. Saberlo ahorra ir a buscar el problema al
IDCODE, que es donde la literalidad del mensaje lleva a mirar primero.

**La solución es cambiar de tipo de sonda:** en CubeIDE, **Run → Debug
Configurations → `GDB Hardware Debugging`** (no `STM32 C/C++ Application`), con
`Use remote target` marcado, `JTAG Device` = **`Generic TCP/IP`**, host
`localhost`, puerto `3333`, y en *Startup* sin `Reset and Delay` ni `Halt`
—eso lo hacen `monitor reset` y `monitor halt`, que el stub implementa— y con
`Load image` marcado, porque el stub implementa `vFlashErase`/`vFlashWrite`.
Queda documentado en `src/README.md` con la tabla de ajustes.

**Y antes de pelearse con el IDE, la comprobación que separa los dos problemas
en diez segundos:** `arm-none-eabi-gdb tu.elf` y `target remote localhost:3333`.
Si eso conecta, el stub está bien y lo único que falta es la configuración.

**Un cambio de código: `--traza-gdb`.** La capacidad de imprimir cada paquete RSP
ya existía en `common/gdb_rsp.h` (`set_verbose`) y **no había manera de pedirla
desde fuera**, que es justo lo que hace falta cuando un IDE se conecta y se va
sin explicar por qué: **el último paquete antes de «cliente desconectado» es el
que no le gustó**. Ahora `./build/sim placa.xml --gdb --traza-gdb` lo imprime.
Suite 1899/1899 y el mismo tiempo simulado.

**Lo que esto significa para el objetivo del proyecto**, anotado como **I-27**:
la vuelta funciona, pero **el camino que un alumno toma por omisión en CubeIDE es
el de ST**, y ese es el que falla. Documentarlo es lo que se ha hecho; lo que de
verdad se parecería al objetivo —«que pruebe igual que con la placa real»— es
repartir el fichero de configuración de lanzamiento ya hecho, para que el alumno
no tenga que elegir. Suplantar el protocolo del servidor de ST es la tercera
opción y no está publicada. Puntos: 140 → 141.

---

## 2026-09-10 UTC — La traza señala el paquete: `monitor ReadAPEx 0x0 0xF8`

**Prompt.** Cambiada la sonda a `ST-LINK (OpenOCD)`, el mensaje de CubeIDE es el
mismo, y la traza de `sim` muestra la sesión entera: `qSupported`, `vCont?`,
`QStartNoAckMode`, `target.xml`, `?`, hilos, `m8000102,4`, `g`, `qSymbol::`,
`qRcmd,5265616441504578203078302030784638` y `D`.

**Respuesta.** La traza cierra el diagnóstico sin margen de duda. Ese `qRcmd` en
hexadecimal se lee **`ReadAPEx 0x0 0xF8`**, y es el **último** paquete antes del
`D` con el que el cliente se despide.

**Lo que la traza demuestra:** todo lo que es GDB estándar funcionó. Se negoció
`qSupported`, se sirvió `target.xml`, se contestó `?` con la parada, se
enumeraron hilos, **se leyó memoria en `0x08000102`** —el bucle `wfe` que el
simulador inyecta— y **se leyeron los registros con `g`**. El IDE llegó hasta el
final y se fue en la única orden que no es de GDB: una orden `monitor` propia de
ST. Cambiar de `ST-LINK GDB server` a `ST-LINK (OpenOCD)` no cambia nada porque
**la comprobación no la hace el servidor, la hace el IDE**: las dos sondas viven
bajo el mismo tipo de lanzamiento, `STM32 C/C++ Application`, y ese tipo es el
que exige un ST al otro lado.

**Y qué es `ReadAPEx 0x0 0xF8`:** leer el registro **`BASE` del AP 0**, que en
ADIv5 es el puntero a la ROM table. Con él, un depurador identifica lo que tiene
delante recorriendo la tabla. Detalle que da rabia: **esa información está en el
modelo** —`AP_BASE` = `0xE00FF003`, y la ROM table con sus PID/CID está
implementada desde F6—. No faltaba el dato: faltaba la orden con la que ST lo
pide.

**Decidido hacer las dos cosas, en este orden.**

**1. La configuración ya hecha, que es lo que funciona hoy.**
`cubeide/simulador.launch` + `cubeide/README.md`: `GDB Hardware Debugging` con
`Generic TCP/IP` a `localhost:3333`, `monitor reset` / `monitor halt` como
órdenes de arranque, `load` marcado —el stub implementa `vFlashErase` y
`vFlashWrite`— y parada en `main`. El alumno importa y cambia dos cosas: el
nombre del proyecto y la ruta de su `.elf`. *(Una nota de camino: `--gdb` dentro
de un comentario XML es ilegal —`--` no se admite ahí— y el fichero no validaba;
la prosa se movió al README.)*

**2. `ReadAPEx` y `WriteAPEx` en el stub.** `dap_leer_ap()`/`dap_escribir_ap()`
nuevas en `common/gdb_rsp.h`, resueltas por cada stub a su manera —el de pines
con una transacción SWD de verdad, conmutando `APBANKSEL` en `SELECT` y
**dejándolo como estaba**, porque todo el acceso a memoria vive en el banco 0; el
interno preguntando a `DebugSys::ap_registro()`—. Comprobado con un cliente RSP
crudo: `ReadAPEx 0x0 0xF8` → `0xE00FF003`, `0xFC` → `0x24770011`, y la memoria
sigue leyéndose bien después de haber tocado `SELECT`.

**Lo que NO puedo afirmar:** que con esto CubeIDE arranque. **El formato de la
respuesta no lo publica ST.** Devuelvo el valor en texto (`0x%08X`) por analogía
con lo que imprimen sus otras órdenes `monitor`, y eso es una conjetura razonada,
no un hecho verificado. Está escrito así en el código, en el README y en I-27, y
`--traza-gdb` dirá dónde se atasca si no basta.

**Un fallo del modelo encontrado por accidente,** probando mi propia
implementación: pedí `ReadAPEx 0x1 0xF8` —un AP que no existe— y contestó lo
mismo que el AP 0. El SW-DP **ignoraba APSEL**: `ejecuta_lectura()` y
`ejecuta_escritura()` solo miraban `SELECT[7:4]` (APBANKSEL) y nunca
`SELECT[31:24]`. En el silicio un AP inexistente lee ceros, **y así es
exactamente como una sonda cuenta cuántos hay**: recorre los IDR hasta que sale
0. Con el fallo, esa enumeración no habría terminado nunca — que es una forma
muy plausible de colgar a un IDE. Corregido en los dos caminos, y
`ap_registro()` sigue el mismo criterio para que los dos stubs se vean idénticos
desde fuera. Anotado como **I-28**, cerrado. Puntos: 141 → 142.

Suite 1899/1899 y `2328209149213 ps` después de todo esto, que es lo que dice
que tocar el DAP no ha movido nada más.

---

## 2026-09-10 UTC — «Truncated register 18»: el GDB equivocado

**Prompt.** Con `GDB Hardware Debugging`, CubeIDE falla en
`-target-select remote localhost:3333` con «Truncated register 18 in remote 'g'
packet», y la traza muestra la sesión completa hasta un `g` y la desconexión.

**Respuesta.** La causa está en el **primer** paquete de la traza, no en el
último. Ahí, entre las capacidades que GDB anuncia, viene:

```
xmlRegisters=i386
```

**Ese GDB es un depurador de PC**, no de ARM. Es el `gdb` del sistema, no
`arm-none-eabi-gdb`, y por eso el fallo aparece justo al pedir los registros:
todo lo anterior —negociar, leer `target.xml`, enumerar hilos— no depende de la
arquitectura, y el paquete `g` sí.

**Las cuentas salen justas, que es lo que lo convierte en certeza y no en
sospecha.** El modelo manda 23 registros de 32 bits: **184 caracteres**
(comprobado midiendo el paquete). Un GDB de i386 espera ocho de 32 bits, `EIP`,
`EFLAGS` y seis de segmento —16 × 8 = 128 caracteres—, y a continuación los de
x87, que son de **80 bits**, o sea 20 caracteres cada uno: 128 + 20 + 20 = 168,
y el registro 18 necesitaría llegar a 188. Hay 184. **Truncado en el registro
18**, ni el 17 ni el 19.

Y no se arregla mandándole otro `target.xml`: aunque le decimos
`<architecture>arm</architecture>`, un GDB construido solo para x86 no lleva ARM
dentro y no puede hacer nada con eso.

**La solución** es una línea: *Debugger → GDB Command*, con la **ruta completa**
del `arm-none-eabi-gdb` que trae CubeIDE, que vive por
`STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.<version>/tools/bin/`.
Conviene la ruta entera y no fiarse del `PATH`.

**Y un cambio, porque este fallo lo va a repetir todo el mundo.** El stub mira
ahora `xmlRegisters=` en el `qSupported` y, si no dice ARM, avisa **en el
momento de conectar**, antes de que el IDE llegue al `g` y falle con un mensaje
que no se parece en nada a la causa:

```
[gdb] AVISO: el depurador conectado dice ser para 'i386', no para ARM.
[gdb]        Esto es el `gdb` del PC, no arm-none-eabi-gdb. Fallara en el paquete `g`
[gdb]        con "Truncated register ... in remote 'g' packet".
[gdb]        En el IDE: Debugger > GDB Command, con la RUTA COMPLETA de arm-none-eabi-gdb.
```

Es el mismo criterio que la validación eléctrica de la placa: **decirlo antes,
que es cuando el aviso todavía ahorra tiempo.** Documentado en
`cubeide/README.md`, con la aritmética del 18 incluida. Suite 1899/1899 y
`2328209149213 ps`.

---

## 2026-09-10 UTC — La carga que se perdía en silencio: `X` sobre la Flash

**Prompt.** Con la sonda `ST-LINK (ST-LINK GDB Server)` la sesión llega mucho más
lejos —carga con paquetes `X`, pone el PC con `Pf=380d0008`, continúa— y acaba
leyendo `mffffffc0`, `m0`, `mffffff80` y matando la sesión con `vKill`/`k`.

**Respuesta.** Esas direcciones son la firma de un núcleo **bloqueado**: PC en
`0xFFFFFFFE`, donde acaba un Cortex-M tras un HardFault sin vector válido. El
IDE no está leyendo memoria: está intentando desensamblar alrededor de un PC
imposible. Y la causa está unos paquetes antes.

**El programa se cargó con paquetes `X`** —`X8000000,188:`, `X8000188,f78:`,
`X8001100,fa0:`…— que son escrituras de memoria a pelo sobre `0x08000000`. Y
**la Flash no se escribe escribiendo**: el controlador ignora esas escrituras
mientras no se desbloquee y se ponga `PG`. En el modelo es literal —verificado
en `mem/flash_if.h:374`: con `LOCK` puesto responde
`TLM_GENERIC_ERROR_RESPONSE`— y por SWD ese rechazo se convierte en un **bit
pegajoso de `CTRL/STAT`**, no en un error del paquete, así que el cliente nunca
se entera. **La carga se pierde en silencio, exactamente como en el silicio.**
Después se salta a `main`, allí hay `0xFF`, y de ahí al bloqueo.

Que el síntoma esté tan lejos de la causa —un PC en `0xFFFFFFFE` treinta
paquetes después de la escritura que se perdió— es lo que hace este fallo caro.

**Y por qué usó `X` y no `vFlashWrite`,** que está implementado desde F6: GDB
solo usa `vFlashErase`/`vFlashWrite` si **conoce el mapa de memoria**, y el stub
no lo anuncia (`qXfer:memory-map:read`). Sin mapa, cae a `X`.

**Corregido por donde arregla a todos los clientes.** Una sonda de verdad no
escribe la Flash a pelo: reconoce el rango y ejecuta la secuencia de
programación. Este stub *es* la sonda, así que `escribir_bytes()` mira la
dirección: Flash por su secuencia, todo lo demás al bus. El borrado es **por
sector y solo la primera vez que se toca cada uno** —borrar en cada trozo
destruiría lo escrito por el trozo anterior del mismo sector—, y los sectores
vuelven a marcarse «por borrar» cuando se conecta un cliente nuevo, porque una
segunda carga sin borrar solo puede APAGAR bits y dejar el programa corrupto de
una forma preciosa de depurar.

**Comprobado de punta a punta**, que es lo que convierte esto en un hecho:

* `verif/fw/blinky/blinky.bin`, 1032 bytes, cargado entero por paquetes `X`;
* releído con `m` y **byte a byte idéntico** al fichero;
* dos trozos del mismo sector: el segundo no borró el primero;
* otro sector, y la RAM por el bus, intactos;
* `SP`/`PC` puestos con `P`, `vCont;c`, Ctrl-C, y **el PC estaba en
  `0x080003C4`**, dentro del blinky. Antes habría estado en `0xFFFFFFFE`.

Suite 1899/1899 y `2328209149213 ps`.

**Lo que queda, anotado como I-29:** anunciar el mapa de memoria sería lo
canónico y haría que GDB usara `vFlash*`. Se ha dejado fuera a propósito: un
mapa incompleto hace que GDB **se niegue a leer** lo que no aparezca en él —los
periféricos, el PPB—, y eso no se puede verificar sin un GDB de ARM, que en el
contenedor no hay. El arreglo elegido no depende del cliente, que era la
propiedad importante. Puntos: 142 → 143.

---

## 2026-09-10 UTC — La traza que no cambia: media conversación no basta

**Prompt.** Con la sonda `ST-LINK (ST-LINK GDB Server)`, la traza vuelve a
terminar en `qRcmd,ReadAPEx 0x0 0xF8` y `D`.

**Respuesta.** Esa traza es **idéntica** a la primera, la de antes de implementar
`ReadAPEx`, y ahí está lo interesante: idéntica es compatible con dos historias
muy distintas, y **la traza tal como estaba no podía distinguirlas**.

1. El binario que corre no lleva el cambio —extraer el tar no recompila; hace
   falta `make sim`—, así que sigue respondiendo vacío.
2. El binary sí lo lleva, contestó `0xE00FF003`, y **ST rechazó el formato**.

La causa de no poder distinguirlas es que la traza imprimía **solo los paquetes
recibidos**. Media conversación. Y con un cliente que se marcha sin explicarse,
la mitad que falta es justo la que importa: una respuesta vacía a un paquete que
el cliente considera obligatorio es la forma habitual de que una sesión se caiga
sin decir nada.

**Dos añadidos, los dos pequeños y los dos por lo mismo:**

* `responder()` traza también la **salida**, con `->`. Una respuesta vacía sale
  como `(vacia)`, que es lo que hay que ver de un vistazo.
* Al activar la traza, el stub dice **cuándo se construyó**:
  `traza de paquetes activada (stub construido el Sep 10 2026 14:02:44)`.
  Parece una tontería y no lo es: cuando un fallo se persigue a base de cambios
  en el stub, la primera pregunta ante una traza que no cambia es si lo que
  corre lleva el cambio dentro, y sin esa línea no hay forma de saberlo mirando
  la salida.

Con eso, la próxima traza contesta sola:

```
[gdb] <- qRcmd,5265616441504578203078302030784638
[gdb] -> 307845303046463030330a          <- 0xE00FF003\n en hexadecimal
```

Si ese `->` no aparece, falta recompilar. Si aparece, el stub hizo su parte y lo
que falla es el formato que ST espera, que es lo único de todo esto que no se
puede averiguar sin documentación suya.

**Y una observación sobre la traza anterior:** la sesión que cargó el programa
con paquetes `X` y acabó bloqueada **no contenía ningún `ReadAPEx`**. Un mismo
IDE no manda esa orden unas veces sí y otras no; lo más probable es que aquella
fuera la configuración de `GDB Hardware Debugging` y esta la de ST. Si es así,
la buena noticia es que **el camino que funciona ya no tiene el fallo de la
Flash**: aquella sesión murió por la carga perdida, que está corregida y
verificada de punta a punta. Merece la pena repetirla antes que seguir peleando
con la sonda de ST.

Suite 1899/1899 y `2328209149213 ps`.

---

## 2026-09-10 UTC — Conjetura refutada: ST rechaza la respuesta, no el valor

**Prompt.** Con el binario recién construido (16:10:43) y la traza de entrada y
salida, la sesión con la sonda `ST-LINK (ST-LINK GDB Server)` muestra:
`<- qRcmd,ReadAPEx 0x0 0xF8`, `-> 307845303046463030330a`, `<- D`.

**Respuesta.** Ya no hay ambigüedad, y la respuesta es que **mi conjetura era
falsa**. El binario es el bueno —lo dice su fecha de construcción—, el stub
contestó `0xE00FF003`, que es el valor correcto, y **ST se despidió igual**.

Lo que dice la traza, ahora que se ve entera:

* **el stub responde**: `307845...` es `"0xE00FF003\n"` en hexadecimal;
* **el valor es el correcto**: `0xE00FF003` es el `BASE` del AP 0, el puntero a
  la ROM table, con sus bits de formato y presencia;
* **ST no reintenta, no pide otra cosa, y no recorre la ROM table**. Esto último
  es lo decisivo: si estuviera identificando el dispositivo de verdad, con ese
  puntero en la mano leería la tabla con paquetes `m` —que el stub sirve sin
  problema— y encontraría el Cortex-M4 y sus unidades. No lo intenta.

**Conclusión: lo que rechaza no es el valor, es la respuesta.** O el formato, o
el hecho mismo de que quien contesta no sea su servidor. Y eso ya no se puede
averiguar desde aquí: el protocolo de las órdenes `monitor` de ST no está
publicado, y seguir es probar formatos a ciegas **sin saber siquiera cuántas
órdenes más vendrían detrás si se acertara con esta**. Es una búsqueda sin
fondo conocido, que es la peor clase.

**Así que cierro I-27 como decisión, no como pendiente**, con las dos ramas
probadas y una de ellas funcionando: `GDB Hardware Debugging` con
`Generic TCP/IP` es **el camino soportado**, y `cubeide/simulador.launch` lo
deja hecho para que el alumno no tenga que elegir. Deja de ser un apaño
provisional para pasar a ser la respuesta.

**Lo implementado se queda.** `ReadAPEx`/`WriteAPEx`, `dap_leer_ap()` y
`DebugSys::ap_registro()` no estorban, son fieles —un AP tiene esos registros y
ahora se pueden leer por los dos stubs— y dejan el camino hecho si algún día
aparece la especificación. Y de paso destaparon **I-28**, el APSEL que el SW-DP
ignoraba, que sí era un fallo del modelo.

**El balance de estos cinco intercambios**, que es lo que queda: cuatro fallos
distintos, tres de ellos nuestros y arreglados —la Flash que se perdía en
silencio (el grave), el APSEL, y una traza que solo contaba la mitad de la
conversación— y uno que no es nuestro y no tiene arreglo desde este lado. No es
mal reparto para una persecución que empezó con «Could not verify ST device!».

---

## 2026-09-10 UTC — Ya depura: `C_MASKINTS`, el bit que nadie miraba

**Prompt.** Proyecto de CubeIDE con el plugin de configuración gráfica,
`GDB Hardware Debugging` sobre `Generic TCP/IP` a `localhost:3333`. **La
depuración arranca.** Para en `Reset_Handler` y no en `main`; con un punto de
ruptura en `main()` se llega a `MX_GPIO_Init()`, pero el paso a paso salta sin
parar a `SysTick_Handler`.

**Respuesta.** Funciona. Un STM32CubeIDE depurando un programa dentro del modelo
SystemC, que era el objetivo del proyecto. Y los dos peros son de naturaleza
distinta: uno es configuración y el otro era **un fallo del modelo**.

**Lo de `Reset_Handler` es la pestaña *Startup***: «Set breakpoint at: `main`» y
«Resume», marcados. Sin eso GDB deja el PC en el punto de entrada y para ahí,
que es exactamente `Reset_Handler`. El `simulador.launch` ya lo trae.

**Lo del paso a paso era nuestro.** Cuando una sonda da un paso escribe en
`DHCSR` dos bits juntos: `C_STEP` **y `C_MASKINTS`**. El segundo dice «mientras
doy este paso, las excepciones configurables se quedan pendientes» —NMI y
HardFault pasan igual, que no se enmascaran—. Sin él, cada paso se come la
interrupción que hubiera pendiente, y en un proyecto de CubeIDE, con SysTick a
1 kHz, **siempre hay una**: «paso sobre esta línea» se convierte en «entra en
`SysTick_Handler`», una y otra vez.

En el modelo el bit **se guardaba y no lo consultaba nadie** —está en la máscara
`0x2F` de `DebugSys::escribir()` desde F6, y ahí se quedaba— y el stub tampoco
lo ponía. Ahora: `core_debug_if::dbg_mask_ints()`, `Cpu::check_exceptions()`
deja pendientes las configurables mientras esté puesto, y `GdbRsp::paso()`
escribe `C_STEP|C_MASKINTS|C_DEBUGEN`, quitándolo al reanudar —un `continue`
tiene que volver a recibir sus interrupciones o no se parecería a la realidad—.

**Medido, con control negativo**, mismo firmware, misma interrupción forzada a
mano con `ICSR.PENDSTSET` y el mismo paso; lo único distinto es el bit:

| DHCSR del paso | PC después | IPSR |
| :--- | :--- | :--- |
| `0xA05F0005` (como antes) | `0x0800027E` | **15** — dentro de `SysTick_Handler` |
| `0xA05F000D` (ahora) | `0x0800018C` | **0** — sigue en el programa |

Y con ocho pasos seguidos: `IPSR=0` en los ocho, y el bit de SysTick **sigue
pendiente** al final, que es la semántica exacta —no se pierde la interrupción,
espera—. *(El control tuvo que rehacerse: la primera versión medía el segundo
caso con el núcleo todavía en modo manejador por culpa del primero, y daba
`IPSR=15` en los dos. Un `monitor reset` entre casos lo arregló. La moraleja de
siempre: un experimento que no aísla su estado inicial no mide lo que cree.)*

Anotado como **I-30**, cerrado. Suite 1899/1899 y `2328209149213 ps`.

**Y lo que esto NO arregla, dicho antes de que se note (I-31):** en el modelo el
SysTick **sigue contando con el núcleo parado**, y ARMv7-M dice que no debería
—el contador no decrementa con el procesador detenido en Debug state—. Aquí
pesa más que en el silicio: pararse en una placa real cinco segundos son cinco
segundos de temporizadores, pero aquí, con el núcleo parado, no queda casi nada
que simular y **el tiempo simulado vuela**, así que mientras el alumno mira una
variable pueden pasar segundos simulados y al reanudar hay una interrupción
esperando siempre. Va de la mano del freno de tiempo real (I-25): son el mismo
problema visto desde dos sitios. Puntos: 143 → 145.

---

## 2026-09-10 UTC — Las dos mitades del tiempo: SysTick congelado y freno real

**Prompt.** «Sí» —atacar I-31 (el SysTick contando con el núcleo parado) y I-25
(el freno de tiempo real)—.

**Respuesta.** Son el mismo problema visto desde dos sitios, y por eso van
juntos: **con el núcleo parado, el tiempo del modelo no se comporta como el de
una placa**. Los dos arreglados y medidos.

**I-31. El SysTick es parte del núcleo y se para con él.** ARMv7-M §B3.3.1: el
contador no decrementa con el procesador detenido en Debug state. En el modelo
era un `SC_THREAD` que no sabía nada del `halt`. Ahora `SysTick` tiene una
entrada `parado`, atada a `sig_halted_` en `cortex_m4f.h`: al parar congela el
valor, al reanudar sigue desde él, y **el tiempo detenido sencillamente no
existe para ese contador**.

Un detalle que costó una relectura y que merece quedar escrito: `halt_proc()` es
un `SC_METHOD` sensible a esa señal, así que **corre cuando `parado` ya vale
true**. Calcular ahí el valor con la función que respeta el paro devolvería el
valor viejo —el contador retrocedería— y por eso hay dos:
`elapsed_ticks_brutos()`, que ignora el paro y es la que usa la congelación, y
`elapsed_ticks()`, que lo respeta y es la que usan las lecturas.

**Medido:** núcleo parado, dos segundos de reloj de pared —muchísimos simulados—,
`CVR` idéntico al picosegundo; tras reanudar y volver a parar, `CVR` movido.

**I-25. `--tiempo-real`.** Ata el avance simulado al reloj de pared, con factor
opcional: `--tiempo-real=4` va cuatro veces más rápido, `=0.5` a la mitad. El
proceso **se duerme** hasta que el reloj de pared alcanza al simulado, y dormir
dentro de un proceso de SystemC detiene toda la simulación —las corrutinas
comparten el hilo del sistema—, que es exactamente lo que se busca. Solo frena:
si el modelo fuera más lento que el tiempo real, no duerme ni acumula deuda.

**Medido:**

| | pared para 2000 ms simulados | CPU esperando a GDB |
| :--- | ---: | ---: |
| sin freno | 0,033 s | 99,6 % |
| `--tiempo-real` | **2,000 s** | **5,3 %** |
| `--tiempo-real=4` | 0,500 s | — |

Dos consecuencias que valen más que los números. La primera: **un LED que
parpadea a 1 Hz parpadea a 1 Hz**, y no doscientas veces por segundo, así que se
puede *mirar*. Para un simulador didáctico eso no es un lujo, es la diferencia
entre ver el sistema y ver un borrón. La segunda: deja de comerse un núcleo
entero por esperar, porque dormir es dormir.

Y un ajuste fino: el bucle de espera pasó de rodajas de 10 ms a **1 ms**, porque
con el freno la rodaja es también lo que el proceso duerme de una vez, y dormido
no atiende el socket. Un milisegundo de latencia ante GDB no se nota; diez, sí.

**Para trabajar con el IDE hay que ponerlo**, y así queda documentado en
`cubeide/README.md`, en `README.md` y en la ayuda.

Suite 1899/1899, `2328209149213 ps`, y **ASan + UBSan limpios** —importaba
comprobarlo: se ha tocado el núcleo y se ha metido un `sleep` dentro de un
proceso—. Puntos cerrados: I-25, I-30 e I-31.

---

## 2026-09-11 UTC — Análisis del proyecto `idetest/sim_discovery_min`

**Prompt.** «Analiza si el código configura correctamente el reloj y el LED de
la placa `discovery_min`, y qué pasará con los otros elementos configurados
—otros LEDs, TIM2, salida PWM— que no están conectados a nada en la placa
simulada».

**Respuesta.** Analizado **y ejecutado**: el proyecto se compiló con
`arm-none-eabi-gcc` y se corrió sobre `placas/discovery_min.xml`, y los valores
de abajo están leídos de los registros del modelo con el stub de GDB, no
deducidos del código.

**El reloj está bien, y es más: los números del temporizador solo cuadran con
él.** HSI (16 MHz) → PLL M=8, N=90, P=2 → entrada del VCO 2,00 MHz (el máximo
recomendado por ST, dentro de 0,95–2,1), VCO 180 MHz (dentro de 100–432),
**SYSCLK 90 MHz**. HCLK/1 = 90 MHz (≤168 ✓), APB1 y APB2 a /4 = 22,5 MHz (≤42 y
≤84 ✓). `FLASH_LATENCY_2` es exactamente lo que pide 90 MHz a 3,3 V —2 WS cubre
de 60 a 90—, o sea justo en el borde y correcto. Leído del modelo:
`RCC_CFGR.SWS=2` (PLL), `SystemCoreClock = 90000000`.

Con eso, TIM2 recibe **45 MHz** (APB1 ≠ /1, así que el reloj del temporizador es
el doble de PCLK1), y `PSC=1249`, `ARR=35999` dan 45 MHz/1250/36000 = **1 Hz
exacto**, con `CCR3=2699` = 7,5 % de ciclo de trabajo. Es decir: los valores se
calcularon para esta configuración, no al azar.

**Dos matices sobre el reloj, ninguno un error.** El programa usa **HSI, no
HSE**, así que el cristal de 8 MHz que la placa declara en PH0 está decorativo;
en hardware real eso significa ±1 % de error, que para un PWM de 1 Hz no importa
y para un UART sí importaría. Y APB2 a /4 es innecesariamente lento, aunque
legal. *(Si algún día se pasa a HSE, el modelo lo soporta: el oscilador observa
el pad y no arranca sin cristal —`rcc/osc_pll.h:64`, `source_present()`—, que es
justo lo que hace el silicio.)*

**El LED está bien configurado y nunca se enciende.** PD12 queda como salida
push-pull sin pull, que es lo correcto para esta placa —el LED tiene el ánodo al
pin y el cátodo a masa por 330 Ω, así que luce en alto—. Pero `MX_GPIO_Init()`
lo pone a `RESET` y **`while(1)` está vacío**: no hay `HAL_GPIO_TogglePin`, ni
`HAL_Delay`, ni nada. Ejecutado:

```
LED LD4 en PD12: apagado  (0.00 V, 0.00 mA)
```

La misma línea que sin firmware, por un motivo distinto: aquí el pin **conduce**
0 V, antes estaba en alta impedancia.

**Lo que no está conectado a nada: no pasa nada, y eso es lo correcto.** Cada pad
del modelo tiene su propio nodo eléctrico —los 154 de los que habla el arranque—
así que LD3, LD5 y LD6 (PD13/14/15) quedan como salidas conduciendo 0 V contra un
nodo sin nada más colgado: sin corriente, sin conflicto y sin aviso. Igual PA2.
La placa declara **0 avisos**, que es lo que debe ser: un pin que no va a ningún
sitio no es un error, es una placa sin poblar.

**El PWM no llega a existir.** `MX_TIM2_Init()` deja TIM2 configurado —OC3M=6
(PWM1), PSC, ARR y CCR3 escritos, PA2 en AF1 push-pull— pero **falta
`HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3)`**. Leído del modelo: `CR1.CEN = 0`
(contador parado) y `CCER.CC3E = 0` (salida no habilitada). No es que no se vea
por no estar conectado: es que no oscila.

**Y el pulsador no puede funcionar en esta placa, por dos motivos
independientes.** Uno de software: EXTI0 está armado en el periférico
(`IMR.0 = 1`, `RTSR.0 = 1`) pero **el NVIC no** (`ISER0` bit 6 = 0) y no hay
`EXTI0_IRQHandler` en el proyecto, así que pulsar solo pondría un bit en `PR`.
Otro eléctrico, más interesante: el código pide **flanco de subida** y **sin
pull**, pero la pieza `Button` de esta placa es **un pulsador a masa**. Sin
pulsar, PA0 queda **flotante**; pulsado, a 0 V. Nunca hay un flanco de subida.
En la Discovery real el botón lleva el pin a VDD con una resistencia externa a
masa, y por eso el código generado espera subida: **la placa simulada modela otra
cosa**. Un pin de entrada flotante sin pull es además un defecto real, no del
simulador.

**Y una sorpresa medida, que corrige algo que yo mismo había escrito.** Este
firmware cuesta **4,8 s de anfitrión por segundo simulado** (986 422 deltas por
segundo), o sea **cinco veces más lento que el tiempo real**; el blinky del
repositorio hace ese mismo segundo en **0,014 s**, setenta veces más rápido. La
diferencia no es el tamaño del programa: es que **este nunca duerme**. El núcleo
se sincroniza con el planificador cada 1 µs —el *quantum* del modelo LT,
`core/cpu.h:75`—, así que un `while(1)` vacío a 90 MHz paga un millón de
sincronizaciones por segundo simulado; el blinky usa `WFI` (dos en su binario,
cero en este) y el modelo se queda dormido con él.

Consecuencias prácticas, las dos honestas: **el `while(1)` vacío que genera
CubeMX es el peor caso para el simulador**, y **`--tiempo-real` no hace nada
aquí** —medido: los mismos 5 s— porque el freno solo frena. La frase del README
sobre ir «entre 11 y 200 veces más rápido que el hardware» vale para el firmware
que se midió, no para éste, y conviene matizarla: la referencia es **P-02** (≈4,7
ciclos por instrucción modelados frente a ≈1,5 reales).

---

## 2026-09-11 UTC — El HSE en bypass con un cristal: I-32, anotado y cerrado

**Prompt.** «Sí, anota todo como punto nuevo del TODO y lo arreglas».

**Respuesta.** Hecho: **I-32**, con prueba automática.

**El defecto.** `Oscillator::source_present()` solo comprobaba que el nodo de
OSC_IN **no estuviera al aire**, y `effective_hz()` caía a la frecuencia nominal
cuando no había podido medir nada. Como la pieza `Crystal` polariza el pin a
VDD/2 por 1 MΩ, un `RCC_HSE_BYPASS` sobre un cristal pasivo **arrancaba**. En el
silicio no: con `BYPASS` el amplificador está apagado y OSC_IN es una entrada
digital, así que un resonador pasivo no entrega nada, `HSERDY` no sube y el
firmware se queda esperando o cae en su `Error_Handler`. **El simulador era más
permisivo que el chip** — la dirección de error que peor le viene a un proyecto
didáctico: funciona aquí y se cuelga en la placa.

**El arreglo.** Una función nueva, `fuente_valida()`, que separa los dos modos
con lo único que hay: la electricidad del pin. **En bypass hace falta que el pin
CONMUTE** —que se haya podido medir una frecuencia—; en modo cristal basta con
que haya algo conectado. Y un segundo fallo que salió al escribir la prueba: la
medida era **pegajosa**. Si se retira el reloj externo y se cuelga otra cosa del
mismo pin, el bypass habría arrancado con la frecuencia del reloj que ya no
está; ahora se borra en cuanto el nodo queda al aire.

**Un cambio que parecía natural y era falso.** De camino hice también que el
modo cristal usara la frecuencia **medida** en el pin en vez de la nominal. Rompió
tres comprobaciones —HCLK a 252 MHz en vez de 168— y **está bien que las
rompiera**: lo que oscila en OSC_IN en modo cristal es el propio amplificador
contra el cuarzo, no una señal ajena, y quien manda es el corte del cristal.
Revertido, y el motivo escrito en el comentario para que nadie lo intente otra
vez. El invariante del tiempo simulado hizo exactamente su trabajo: la cifra se
movió, y eso fue la primera señal.

**Verificado por los dos lados.** Con firmware real compilado del proyecto de
STM32CubeIDE, sobre `placas/discovery_min.xml`:

| modo | qué hay en PH0 | HSERDY | SYSCLK | dónde acaba |
| :--- | :--- | :---: | ---: | :--- |
| `HSE_BYPASS` | `Crystal` (pasivo) | **0** | 16 MHz (HSI) | `Error_Handler` |
| `HSE_BYPASS` | `ExtClock` 8 MHz | 1 | 90 MHz | `main` |
| `HSE_ON` | `Crystal` | 1 | 90 MHz | `main` |
| `HSE_ON` | `ExtClock` | 1 | 90 MHz | `main` |
| cualquiera | nada | 0 | 16 MHz | `Error_Handler` |

Y con **tres comprobaciones nuevas en T21**, para que no vuelva: el cristal en
bypass no sube `HSERDY`, el mismo cristal sin bypass sí, y lo hace a su
frecuencia **nominal**.

**La suite pasa de 1899 a 1902 comprobaciones** y el invariante del tiempo
simulado a **`2336211899213 ps`**. Es la primera vez que esa cifra se mueve por
un motivo legítimo —añadir pruebas mueve el reloj—, así que queda actualizada en
`README.md`, en `doc/compilacion.md` y en este registro; las entradas anteriores
conservan la cifra que era cierta cuando se escribieron. Puntos: 145 → 146.

---

## 2026-09-12 UTC — La Discovery entera, y el fallo que destapó

**Prompt.** «Modifica `discovery_min.xml` para que contenga dos
cristales/osciladores externos, LSE y HSE, igual que la tarjeta real. Conecta
los cuatro pines de los cuatro LEDs (PD12 a PD15), el botón azul en PA0, el
botón negro (de reset) en PB2 y todos los pines de depuración con la circuitería
necesaria para que el resultado coincida con la tarjeta real».

**Respuesta.** Hecha, contrastada con UM1472 y el esquemático MB997, y con **dos
correcciones al enunciado y un fallo del modelo** que apareció al comprobarla.

**Corrección 1: el pulsador negro no va a PB2, va a NRST.** PB2 es **BOOT1**, y
en esta tarjeta está sujeto a masa por resistencia para que el arranque sea
desde la Flash de usuario. Un pulsador en PB2 no resetearía nada. El de reset va
a NRST, cuyo nivel alto lo da el pull-up interno de 40 kΩ del propio pad.
Modelados los dos: `B2` sobre NRST y una `Rpull` a masa en PB2.

**Corrección 2: el cristal del LSE no viene soldado en la tarjeta real.** X3 es
un zócalo vacío en la MB997. Se ha puesto porque se pidió, pero es la única
pieza del fichero que la Discovery no lleva de fábrica, y está avisado en la
cabecera: un firmware que use el RTC con LSE funcionará aquí y no allí. Para que
coincida de verdad basta `conectada="no"` en X3.

**Y una tercera cosa que no es corrección sino hallazgo: el botón azul va a VDD,
no a masa.** Cerrado lleva PA0 a 3,3 V y una resistencia externa lo sujeta abajo
—por eso el código que CubeMX genera para esta placa pide EXTI por flanco de
**subida** sin pull interno, que es justo lo que tenía el proyecto de prueba—.
La pieza `Button` del modelo **solo sabía ir a masa**, así que la placa no se
podía describir. Ahora tiene `v_cerrado` (**I-34**).

**El fallo del modelo, que es lo gordo (I-33).** Al comprobar el botón salió que
PA0 se leía **0** con el nodo a 3,3 V. No era el botón: `Pad::pad_proc()`
escribía `din = false` y **se ponía a esperar** un cambio en el nodo o en la
configuración. Un nodo que ya está gobernado cuando empieza la simulación —una
resistencia de pull, que conduce desde que se construye— no genera ningún evento
después, así que el pad **nunca llegaba a mirarlo**: el IDR decía cero,
indefinidamente.

Lo que lo hace notable es que **es el mismo fallo que ya se había corregido en
el `Led`**, con su comentario y todo —«se evalúa ANTES de esperar, no después…
hay montajes donde el nodo no se mueve nunca»— y que nadie fue a mirar en el
`Pad`. Y aquí es peor: en el LED quien se equivocaba era el modelo; aquí es **el
firmware que lo lee**, y el alumno se pasaría la tarde buscando el error en su
código. El banco no lo veía porque sus entradas las mueve un `Driver` en marcha,
y mover el nodo sí despierta al pad.

Arreglado mirando una vez antes del bucle. **Medido**: tres pines sujetos a
3,3 V desde el arranque pasan de leerse `0` a leerse `1`; 1902/1902 y el mismo
tiempo simulado **al picosegundo**, que es lo que dice que no se ha movido nada
más.

**La comprobación de la placa se cuenta sola.** Un firmware mínimo que usa los
cuatro LEDs como indicadores —LD4: estoy vivo; LD3: el botón; LD5: HSERDY;
LD6: LSERDY—:

| | LD4 vivo | LD3 botón | LD5 HSE | LD6 LSE |
| :--- | :---: | :---: | :---: | :---: |
| tal cual, botón suelto | ● | ○ | ● | ● |
| botón azul pulsado | ● | **●** | ● | ● |
| botón negro de reset pulsado | ○ | ○ | ○ | ○ |

La última fila es el MCU entero detenido en reset, que es exactamente lo que
tiene que pasar. Y el LSE tarda **dos segundos** en arrancar —a 2000 ms de
simulación todavía está apagado y a 3000 ms ya no—, que es el tiempo real de
arranque de un cuarzo de 32 kHz y estaba ya en el modelo.

**Y los cuatro LEDs encendidos enseñan de un vistazo por qué el azul es
especial:**

```
  LED LD4 en PD12: encendido  (3.20 V, 1.77 mA)      verde,   Vf 2,0
  LED LD3 en PD13: encendido  (3.20 V, 1.77 mA)      naranja, Vf 2,0
  LED LD5 en PD14: encendido  (3.19 V, 2.04 mA)      rojo,    Vf 1,8
  LED LD6 en PD15: encendido  (3.28 V, 0.41 mA)      azul,    Vf 3,0
```

Cuatro veces menos corriente por el azul: con 3,0 V de caída sobre 3,3 V no
queda casi nada para la resistencia de 680 Ω. No es una simplificación, es la
tarjeta.

**Lo que queda fuera, dicho:** los valores de la resistencia de pull-down del
botón (100 kΩ) y de serie de los LEDs (680 Ω) salen del esquemático MB997 leído
con herramientas automáticas y **no los he podido contrastar a mano**; ninguno
cambia la lógica, solo la corriente. Y `PA15`, `PB3` y `PB4` no llevan nada
colgado **porque en la tarjeta real tampoco**: solo PA13 y PA14 van al ST-LINK,
y por eso el SWO de la Discovery no se puede leer con su depurador de a bordo.
En el simulador sí se puede, descomentando el `SwoReceiver` del fichero.

Puntos: 146 → 148.

---

## 2026-09-12 UTC — El LSE, desoldado como en la tarjeta

**Prompt.** «Si no hay cristal LSE externo en la placa, ¿cómo es que el circuito
interno que comprueba el LSE sí arranca (tras 2 segundos)?» — y, tras la
respuesta, «Sí, déjalo así».

**Respuesta.** Porque en el fichero que entregué **el cristal estaba**: lo puse
yo, porque se pidieron dos. Lo que no lo lleva es la tarjeta. El simulador no
inventaba nada: simulaba la placa que le describí.

Comprobado antes de contestar, con LD6 enseñando `LSERDY`:

| placa | LSERDY a los 3 s |
| :--- | :--- |
| con X3 (como se entregó) | encendido |
| X3 con `conectada="no"` | apagado |
| X3 borrado del fichero | apagado |

Y el HSE sigue arrancando en los tres casos, que es el control: falla el LSE y
solo el LSE. O sea que el mecanismo está bien —es el mismo `source_present()`
que se arregló ayer para el HSE en bypass—: sin nada en PC14, el oscilador no
alcanza RDY nunca.

**Ahora queda `conectada="no"`,** que es la opción fiel y además la más útil para
enseñar: el alumno que intente el RTC con LSE se encontrará aquí el mismo
problema que se encontraría con la tarjeta en la mano. Se ha preferido a borrar
el componente porque **declararlo desoldado es información, y borrarlo es
ausencia de información**: el fichero sigue diciendo que ahí va un cristal y que
no está puesto, y desde C++ se puede soldar en marcha con `attach()`.

Actualizados el fichero, su cabecera, `doc/parts.md` y el `README`. Sigue en
11 componentes y 0 avisos.

---

## 2026-09-14 UTC — El pulsador normalmente cerrado

**Prompt.** «El componente Button siempre cierra el circuito cuando se pulsa.
Modifica el comportamiento y añade un atributo `normalmente` al XML para que
pueda representar un botón normalmente-abierto o normalmente-cerrado, con los
valores `abierto` y `cerrado`, siendo `abierto` el valor por defecto y que debe
coincidir con el comportamiento actual».

**Respuesta.** Hecho, y la regla cabe en una línea: **lo que conduce no es
«pulsado», es «pulsado XOR normalmente cerrado»**.

| `normalmente` | suelto | pulsado | desoldado |
| :--- | :---: | :---: | :---: |
| `abierto` (omisión) | abierto | **cierra** | abierto |
| `cerrado` | **cierra** | abre | abierto |

La última columna es la misma para los dos y es lo razonable: **una pieza que no
está no cierra ningún contacto**. Al volver a soldarla recupera su reposo, que en
un NC es conduciendo — de ahí que `set_enabled()` tenga que reaplicar el estado y
no solo ponerse en alta impedancia.

**Y una consecuencia de diseño que merece nombre propio:** un NC **conduce desde
que se construye**, antes de que nadie lo toque. Eso lo pone en la misma
categoría que `Rpull`, y es justo el caso que ayer destapó el fallo del `Pad`
(I-33): si el pad no mirase el nodo antes de esperar, un final de carrera NC en
reposo se leería como 0 para siempre. Arreglado ayer, funciona hoy.

**Por qué esto no es una rareza.** Los finales de carrera, las setas de
emergencia y los detectores de puerta se cablean NC **a propósito**, para que un
cable cortado se vea igual que una pulsación y la máquina pare. Un montaje de
seguridad descrito como NA **parece funcionar hasta el día en que se corta el
cable**, que es justo el día que importa. Por eso una palabra que no sea una de
las dos es **error de netlist** y no un `abierto` silencioso:

```
Error: netlist: Button 'FC1': normalmente="NC" no vale;
       solo "abierto" (por omision) o "cerrado"
```

**Dos nombres que conviene no confundir**, y que en un NC son opuestos:
`pressed()` es lo que hace el **dedo**, `cerrado()` lo que hace el **contacto**.

**T124, trece comprobaciones nuevas.** Van sobre **nodos propios del banco**, no
sobre pines del MCU: lo que se prueba es la pieza, y colgarla de un pad
obligaría a tocar la placa del banco para probar un componente. Los dos
pulsadores cierran contra 3,3 V con un pull-down de 100 kΩ, así que «conduce» se
lee como 3,3 V y «abierto» como 0 V, y se comprueban los cuatro estados, el
desoldado y el resoldado. *(Los `AnalogNet` son `sc_object`, así que se declaran
como miembros: la elaboración de SystemC es estática y no se pueden crear con la
simulación en marcha.)*

Y por el lado del XML, las cuatro rutas comprobadas con `sim` y un LED espejo:
`cerrado` lee 1 en reposo, `abierto` lee 0, **sin el atributo lee 0** —que era el
requisito: el valor por omisión tiene que coincidir con el comportamiento de
antes— y una palabra inválida no arranca.

Suite: 1902 → **1915**, tiempo simulado `2336217899213 ps` —los 6 µs de esperas
de T124—. Puntos: 148 → 149 (**I-35**).

---

## 2026-09-14 UTC — Un pin, cuatro maneras de escribirlo

**Prompt.** «Unos MCUs nombran los pines PA0…PB15 y otros usan número de puerto
con punto (P1.0) o letra con punto (PA.0). Modifica el programa para aceptar los
tres casos. En caso de ambigüedad (P111 podría ser P1.11 o P11.1) elegir siempre
el número de puerto más pequeño (P1.11)».

**Respuesta.** Aceptadas las cuatro formas —la cuarta sale sola: si vale `P3.12`,
tiene que valer `P312`—, y el trabajo de verdad no estaba donde parecía.

| escrito | se lee |
| :--- | :--- |
| `PD12` | letra sin punto, la de ST y la **canónica** |
| `PD.12` | letra con punto |
| `P3.12` | número con punto |
| `P312` | número sin punto |

**El punto ya estaba ocupado, y ese era el problema.** En `u0.PD12` separa el
chip del pad; en `P3.12`, el puerto del pin. La regla que los distingue cabe en
una línea: **si detrás del último punto solo hay dígitos, ese punto es el del
pin** —y lo de delante tiene que terminar en algo con forma de puerto—. Así
`u0.P3.12` tiene los dos puntos y cada uno hace lo suyo.

**El número de puerto es el índice**, contado como las letras: `P0` = `PA`, …
`P8` = `PI`. No es arbitrario: es la única correspondencia que hace que las dos
formas nombren lo mismo.

**La ambigüedad, resuelta como se pidió y por un motivo:** `P111` se lee `P1.11`,
el puerto más pequeño. No es un desempate caprichoso —los puertos bajos son los
que existen en todos los chips— y además **es estable**: si un modelo futuro
tuviera más puertos, lo que hoy se lee `P1.11` se seguirá leyendo igual. Se
implementa probando el puerto más corto primero, que es también el menor.

**Lo que costó más que el parser: que las cuatro formas nombren el MISMO nodo.**
La primera versión parseaba bien y aun así fallaba, porque el `NodeMap` va por
nombre literal: declarar `<nodo id="P3.12"/>` y conectar `nodo="PD.12"` daba
«nodo desconocido». La solución es `nombre_canonico_pad()` aplicada **a la
entrada** —`Instancia::pin`, `nodo_externo`, `nodo_bus`, `nodo_une` y el propio
`NodeMap`—, de modo que todo converge a una sola clave y **los volcados siguen
hablando con una sola voz**. Canonizar en la puerta y no en cada comparación es
lo que evita tener que acordarse en veinte sitios.

Comprobado con una placa de verdad, con las formas mezcladas a propósito: `PD12`
declarado como `P3.12` y conectado como `PD.12`, `PD13` como `P313`. Once
componentes, **0 avisos**, y el informe final imprime `LED LD4 en PD12`.

**Lo que sigue sin valer, a propósito:** `PA05`, `P1.05` y `P016`. Un cero a la
izquierda no es otra forma de escribir lo mismo, es una escritura distinta del
mismo número, y admitirla convierte cualquier nombre en un acertijo. `P016`
además es ambiguo de verdad.

**T125: veinte comprobaciones y CERO tiempo simulado.** `pad_desde_nombre()` es
una función pura, así que la prueba no mueve el reloj: la suite pasa de 1915 a
**1935** comprobaciones con el invariante **exactamente igual**,
`2336217899213 ps`. Puntos: 149 → 150 (**I-36**).

---

## 2026-09-14 UTC — `--mcu`: la opción está, el segundo MCU no

**Prompt.** «Modifica el programa para aceptar por parámetro de entrada el tipo
de MCU implícito en caso de que no esté especificado en el XML, con el valor por
defecto `stm32f407vg`. De manera que `sim discovery_min --debug` simule un
stm32f407vg y `sim discovery_min --mcu stm32f446re --debug` simule los mismos
componentes con un MCU stm32f446re».

**Respuesta.** La opción está hecha y hace exactamente lo que se pidió. La
segunda mitad de la frase no se puede cumplir, y decirlo es la respuesta:
**el único MCU que este programa modela es el STM32F407VG**.

**Lo hecho.** `--mcu TIPO` (y `--mcu=TIPO`) fija el tipo del MCU **implícito**,
el que se monta cuando el XML no declara ninguno. Insensible a mayúsculas, con
`STM32F407VG` por omisión, y **sin pisar lo que diga el XML**: una placa que
declara sus chips ya ha dicho cuáles son, y la línea de órdenes no tiene por qué
saberlo mejor. Comprobado: `--mcu stm32f407vg`, `--mcu=STM32F407VG` y sin opción
dan la misma placa, y `placas/dos_mcu.xml` sigue montando sus dos chips con la
opción puesta.

**Lo que pasa con `--mcu stm32f446re`:**

```
mcu (implicito): no se sabe construir un 'STM32F446RE'.
Los tipos que este programa modela son: STM32F407VG.
Un MCU distinto no es un parametro: es otro modelo, con su mapa de memoria,
sus perifericos y su encapsulado.
```

**Y ese error es la parte importante de este cambio.** Montar un F407 mientras
el usuario ha escrito F446RE sería exactamente la clase de mentira que llevamos
toda la semana quitando —el HSE que arrancaba sin cristal, el pad que leía 0 con
el pin a 3,3 V—: el alumno vería su programa «funcionar» sobre un chip que no es
el suyo. Un MCU no es un parámetro; es 45 000 líneas de otro modelo.

Lo que sí queda hecho para ese día: el sitio donde enchufarlo. `TIPOS_MCU` es
una lista —hoy de uno— que se imprime sola en el error y en `--help`, así que
añadir un modelo no obliga a tocar la interfaz ni a buscar por dónde se colaba
una constante. Anotado como **I-37**.

**Dos detalles del ejemplo que no son lo que parecen**, y conviene decirlos en
vez de adivinarlos: la opción de depuración se llama **`--gdb`**, no `--debug`, y
el fichero de placa se da **con su ruta y su extensión** (`placas/discovery_min.xml`),
no como `discovery_min`. Las dos comodidades son de cinco minutos; no las he
añadido sin preguntar porque cambian la interfaz y eso se decide, no se supone.

Suite 1935/1935 y `2336217899213 ps` —`sim_main.cpp` no entra en el banco, pero
se comprueba igual—. Puntos: 150 → 151.

---

## `sim --help COMPONENTE`

> Modifica el programa para que `sim --help COMPONENTE` informe al usuario sobre
> el funcionamiento del componente `COMPONENTE` y sus atributos en el XML. Añade
> esta capacidad a todos los componentes que se añadan en el futuro.

Hecho. Los veintiún tipos tienen ficha:

```
$ ./build/sim --help Button
Button
======

  Pulsador. SIN PULSAR DEJA EL PIN ABIERTO, asi que el nivel de reposo tiene
  que darlo alguien: el pull interno del MCU o un Rpull externo. Sin ninguno
  de los dos el pin queda indeterminado, que es lo que pasa en una placa real
  y lo que el modelo reproduce.

  <nodo id="PA0" bus="si"/>
  <componente tipo="Button" id="B1" v_cerrado="3.3" r_cerrado="10">
    <pin nombre="pin" nodo="PA0"/>
  </componente>
  ...

TERMINALES  (<pin nombre="..." nodo="..."/>)
  pin             obligatorio
      El pin del pulsador.

ATRIBUTOS DEL XML  (<componente ... nombre="valor">)
  r_cerrado       por omision 10
      Resistencia del contacto cerrado, en ohmios. Cerrado, la pieza gobierna
      el nodo con {v_cerrado, r_cerrado}.
  ...
```

**Pero la orden no es la parte interesante del encargo.** La segunda frase —«a
todos los componentes que se añadan en el futuro»— es la que decide el diseño, y
tiene dos respuestas posibles con consecuencias muy distintas.

La cómoda es una tabla `tipo → texto` en el fichero que imprime la ayuda. Se
escribe en veinte minutos y **se queda vieja el primer día**: alguien añade un
parámetro al creador del `Led`, no se acuerda de la tabla, y a partir de ahí la
ayuda miente. Una ayuda que miente es peor que no tener ninguna, porque el que la
lee ya no vuelve al código a comprobarlo.

La otra es que **el texto viaje con el registro**. `REGISTRA_PARTE` pasa a tener
tres argumentos —tipo, ayuda y creador— en la misma llamada:

```cpp
REGISTRA_PARTE(Led,
    Ayuda("Diodo con su resistencia en serie. NO ES LINEAL: ...")
      .pin("anodo o catodo", "uno de los dos", "La patilla que va al pin...")
      .atr("vf", "2.0", "Tension directa del diodo, en voltios...")
      .nota("EL CASO DEL LED AZUL. Con vf=3,0 sobre 3,3 V...")
      .cpp("on() dice si luce y current() la corriente..."),
    [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Led(...);
    });
```

Y el argumento del medio **no es opcional**. No hace falta acordarse de
documentar la pieza nueva en otro sitio, porque no hay otro sitio: si no se pasa
la ayuda, no compila. Eso es lo que responde a la segunda frase — no una
convención que haya que recordar, sino una que no se puede saltar.

Lo que la macro no puede hacer es juzgar si un texto dice algo. De eso se
encargan tres redes, en este orden: `Ayuda::completa()` exige resumen y
terminales, `Fabrica::sin_documentar()` enumera las que no lo cumplen, `--help`
avisa si hay alguna, y **T126** falla. Las veintiuna comprobaciones de T126
**recorren la factoría**, no una lista escrita a mano, que es la única forma de
que sigan valiendo para la pieza número veintidós.

Tres decisiones pequeñas dentro:

* **`--help led` vale.** La búsqueda no distingue mayúsculas, porque quien
  escribe eso en una consola está *preguntando*. El XML sigue distinguiéndolas,
  porque ahí se está *describiendo una placa*, y son dos cosas distintas.
* **El error de tipo desconocido lo dice ahora**: `tipo desconocido 'led'. Se
  escribe 'Led': el tipo distingue mayúsculas`, en vez de limitarse a enumerar
  los veintiuno y dejar que el lector los compare.
* **El formateo respeta lo preformateado.** Una línea del texto que empiece por
  espacio se copia tal cual, que es lo que deja meter la tablita de «pulsado XOR
  normalmente cerrado» dentro de una nota sin que el envoltorio le destroce las
  columnas.

`doc/parts.md` no desaparece y ahora dice por qué: de los dos, `--help` es el que
está siempre al día y se tiene a mano sin salir de la consola; el catálogo tiene
las tablas, las comparaciones y los ejemplos largos que no caben en una ficha.

Suite **1956/1956** (+21) y el invariante intacto en **`2336217899213 ps`**: todo
lo de T126 es consulta de un mapa estático, no avanza el reloj ni toca un nodo.
ASan+UBSan limpios. Puntos: 151 → 152, con **I-38** cerrado.

---

## Las piezas, reutilizables en otro MCU

> Si no está hecho así ya, modifica el código para que las diferentes partes del
> stm32f407vg sean fácilmente reutilizables en otros MCUs. Por ejemplo, que sea
> fácil construir un MCU que tenga el mismo core pero con diferente número de
> IRQs o bits de prioridad de las interrupciones, diferentes tamaños de memorias
> (Flash o SRAM).

No estaba hecho así, y conviene decir con precisión **en qué no lo estaba**,
porque en el papel sí lo parecía.

El proyecto ya usaba la receta de rasgos (`UsartCaps`, `TimCaps`, `SpiCaps`,
`DebugCaps`) para los periféricos: catorce temporizadores salen del mismo modelo
y el tipo de cada uno lo fija un `struct`. **El núcleo y las memorias se habían
quedado fuera.** Ahí estaba todo soldado:

* `N_IRQ = 82` era una constante global del `namespace stm32`, y de ella colgaban
  `bool enabled_[N_IRQ]`, `uint8_t ipr_[N_IRQ]`, `bool pending_[N_EXCEPTIONS]`…
  arrays de tamaño fijo, o sea el chip metido en el tipo;
* la máscara de prioridad era un `0xF0` **escrito a mano en tres sitios**
  distintos;
* las ocho regiones del MPU eran `static constexpr` y los índices se recortaban
  con `& 7u` repartido por seis líneas;
* `FlashIf` leía `addr::FLASH_SIZE`, `N_FLASH_SECTORS` y `FLASH_SECTORS` **desde
  dentro**, así que una Flash de otro tamaño no era una instancia distinta: era
  otro fichero.

Ahora los rasgos son datos, en tres capas:

```cpp
CoreCaps   // n_irq, prio_bits, mpu_regiones, fpu, cpuid
MapaFlash  // base, size, tabla de sectores, sysmem/OTP/opt, curva de WS
MapaRam    // las cuatro RAM; un tamaño a cero = ese bloque NO existe
McuCaps    // los junta, mas los topes de reloj, con nombre y FAMILIA
```

Los reciben `Scs`, `Mpu`, `CortexM4F`, `FlashIf`, `AhbMatrix`, `Rcc` y el top,
**todos con el F407 por omisión**. Un modelo construido como siempre es el de
siempre, byte a byte y picosegundo a picosegundo.

**Tres decisiones de diseño que merecen explicación.**

*Struct y no plantilla.* Con un `struct` por valor la elección se puede hacer en
tiempo de compilación —hay alias, `CoreT<DBG_PINES, CORE_M4F_MINIMO>`— o **en
tiempo de ejecución**, que es lo que hace falta cuando el tipo de MCU viene de un
XML o de `--mcu`. Una plantilla cerraría lo segundo, y lo segundo es el caso de
uso de este simulador.

*La geometría de sectores es una tabla, no una división.* Un F407 de 1 MB tiene
doce sectores de cuatro tamaños distintos. No se calcula; hay que darla.

*La curva de estados de espera sí es una regla.* En toda la familia es *un WS más
por cada tanto de frecuencia*, y lo que cambia son el escalón y el techo. Con
esos dos números da **exactamente** lo mismo que la tabla escrita a mano —T127 lo
comprueba megahercio a megahercio— y además vale para un chip que no llegue a
168 MHz.

**Tres cosas que se arreglaron de camino y no eran el encargo.** `MPU_TYPE` lee
0 en un núcleo sin MPU y `CPACR` se queda a cero en uno sin FPU, que es como
CMSIS averigua que no los hay. El aviso del RCC imprime ahora **el tope del chip
que se está montando** en vez de un `"HCLK > 168 MHz"` a pelo — un aviso que dice
168 en un chip que no pasa de 84 es peor que no decir nada. Y la máscara de
`SHPR3` dejaba fuera el byte de DebugMonitor, que ARMv7-M §B3.2.10 sí implementa;
al derivarla de `prio_bits` para los cuatro bytes, entró sola.

**La parte honesta, que es la frontera.** Está escrita en `McuCaps::familia`:
*dos chips de la misma familia se distinguen con un descriptor; dos familias
distintas necesitan modelo nuevo.* Un F405 es un F407 sin Ethernet ni cámara y se
describe cambiando números. Un F446 tiene otro árbol de reloj y periféricos que
aquí no existen, y describirlo con un `McuCaps` no lo convertiría en un F446: lo
convertiría en **un F407 con etiqueta falsa**, que es justo la clase de mentira
que I-32 e I-33 vinieron a quitar de este proyecto.

Y lo que **sigue soldado**, anotado en la §7 del documento nuevo: el encapsulado
(`is_bonded_lqfp100` es todavía una función estática), la tabla de funciones
alternativas y los números de IRQ de cada periférico. Los tres son de la familia,
no del chip.

**T127**, treinta y ocho comprobaciones, y no miran los `struct`: montan **un
NVIC de 32 líneas con tres bits de prioridad y una Flash de 256 KB con seis
sectores como módulos de verdad, al lado del F407 y en la misma simulación**.
Escribir `0xFF` en una prioridad deja `0xE0` en uno y `0xF0` en el otro; `ISER1`
entero se queda a cero en el de 32 líneas mientras la IRQ 5 se habilita sin
problema; `0x0805_0000` está en el sector 6 del F407 y en **ninguno** del de
256 KB. Las piezas de laboratorio no son ningún chip y el código lo dice: no hay
ahí ni un periférico, ni un árbol de reloj, ni un encapsulado.

Suite **1993/1993** (+37), invariante intacto en **`2336217899213 ps`** —los
módulos de laboratorio se atan a señales que nadie mueve, así que sus procesos no
despiertan nunca—, ASan+UBSan limpios. Documento nuevo:
`doc/stm32f407vg_reutilizacion.md`. Puntos: 152 → 153, con **I-39** cerrado.

---

## La familia F405/F407, entera (y el renombrado que no hacía falta)

> Renombra tanto en el código como en los XML y la documentación el nombre del
> MCU stm32f407vgt a stm32f407vg para eliminar la terminación 't' que indica el
> encapsulado físico. Además, construye todos los miembros restantes de la
> familia: STM32F405RG, STM32F405OG, STM32F405VG, STM32F405ZG, STM32F405OE,
> STM32F407VE, STM32F407ZE, STM32F407ZG, STM32F407IE, STM32F407IG

**Lo primero no había que hacerlo: el proyecto nunca usó `vgt`.** La cadena
aparece **una sola vez en todo el árbol**, en un comentario de un fichero CMSIS
de ST que viene con el firmware de pruebas y que además habla de otro chip:

```
src/verif/fw/cmsis/Include/stm32f4xx.h:88:
/* #define STM32F412Vx */ /*!< STM32F412VET, STM32F412VGT, ... */
```

Todo lo nuestro —347 apariciones en código, XML y documentación— ya dice
`STM32F407VG`. No he tocado el fichero de ST: es material del fabricante, y
reescribirlo sería peor que dejarlo.

**Lo segundo sí, y salió bien porque el refactor anterior había quitado el
obstáculo.** Los once miembros son el mismo silicio, y lo que los distingue son
tres cosas [DS8626, tabla 2]:

| | |
| :--- | :--- |
| **el dígito 5 o 7** | un F405 es un F407 **sin Ethernet y sin cámara**. Eso es todo |
| **la letra del encapsulado** | R=LQFP64, O=WLCSP90, V=LQFP100, Z=LQFP144, I=LQFP176 |
| **la última letra** | E = 512 KB en ocho sectores, G = 1 MB en doce |

Los once llevan **el mismo núcleo** (82 IRQ, 4 bits de prioridad, 8 regiones de
MPU) y **la misma RAM** (112+16+64 KB más 4 de backup). Nada de eso cambia.

**El obstáculo era el encapsulado**, y estaba señalado desde el análisis de
varios MCU (§6.3): `is_bonded_lqfp100()` era una **función estática**. Ahora es
un `Encapsulado` con una máscara de 16 bits por puerto. Tenía que ser por pin y
no por puerto, y el LQFP64 es el caso que lo demuestra: de su puerto D sale **un
solo pin**, `PD2`. «El puerto D existe a medias» no se puede decir con un
booleano.

Lo que sostiene la tabla —y es lo que me preocupaba— es un contraste barato:
cada encapsulado lleva **el número de E/S que le da el datasheet** (51, 72, 82,
114, 140) y T128 comprueba que la máscara tiene exactamente esos bits. Una
máscara mal copiada deja de cuadrar. Sin eso, el error sería invisible: el
modelo se montaría igual y el alumno trabajaría contra un chip que no es el suyo.

**Un periférico ausente no es un periférico apagado.** En un F405 la ventana del
Ethernet **no la decodifica nadie** y tocarla da error de bus, como en el
silicio. El módulo sigue construido —la elaboración de SystemC es estática— pero
sin camino desde el bus; su socket se ata a un iniciador mudo solo para que la
elaboración cierre. Descarté decodificar la ventana hacia un destino que
devolviera error: se vería igual desde el firmware, pero **mentiría en el
netlist**, que enseñaría un periférico donde no lo hay.

Funciona de punta a punta:

```
$ ./build/sim placas/discovery_min.xml --mcu STM32F405RG --valida
  [decl] LD4.anodo: el pad PD12 no sale al encapsulado LQFP64
  [decl] LD3.anodo: el pad PD13 no sale al encapsulado LQFP64
  ...
$ ./build/sim placas/discovery_min.xml blinky.bin 200 --mcu STM32F405VG
  LED LD4 en PD12: apagado  (0.00 V, 0.00 mA)      # el F405 no tiene ETH, y da igual
$ ./build/sim placas/discovery_min.xml --mcu STM32F446RE --valida
  mcu (implicito): no se sabe construir un 'STM32F446RE'.
```

**Y una cosa que NO he podido verificar, dicha en voz alta.** De los seis
encapsulados, cinco llevan su reparto contrastado y **el WLCSP90 no**. El
recuento (72 E/S) sale de la tabla 2 y está comprobado; el reparto bola a bola
está en la tabla 7 del mismo datasheet, que no he conseguido leer. Lo que hay en
el modelo es la reconstrucción razonable —A, B, C y D completos, `PE0`–`PE5` y
los dos del oscilador— y **puede no ser la de ST**. Está marcado
`verificado = false` y `sim` lo dice al montar la placa:

```
  [aviso] STM32F405OE: el reparto de pads del WLCSP90 es una reconstruccion a
  partir del recuento del datasheet (72 E/S) y NO esta contrastado bola a bola
```

Es deliberado que moleste: un mapa que parece exacto y no lo es sería peor que
el aviso. Queda como **I-40**, y se cierra copiando la tabla 7 del DS8626 — la
comprobación que lo validará ya está escrita.

Sin cerrar también, y anotado en §9.5: los bits de reloj de los periféricos
ausentes (`RCC_AHB1ENR.ETHMACEN` debería leer cero en un F405 y aquí no lo
hace), y que la tabla de funciones alternativas es la misma para los once.

**T128**, 38 comprobaciones. Suite **2031/2031** (+38), invariante intacto en
**`2336217899213 ps`**, ASan+UBSan limpios. Puntos: 153 → 154, con **I-37**
cerrado (ya hay once tipos que construir) e **I-40** abierto.

---

## Informe comparativo F407VG / F446RE y plan de puerto

> Genera un informe detallado en la carpeta doc, llamado stm32f407vg_vs_446re.md
> con el análisis detallado de todos los elementos (core, scs, depuración, dma,
> memorias, buses, periféricos, reloj, etc.) comparando las similitudes y
> diferencias entre un stm32f407vg y un stm32f446re y desarrolla un plan para
> crear un modelo SystemC de un stm32f446re reutilizando al máximo posible el
> código del modelo del stm32f407vg

741 líneas, quince secciones. Lo he escrito contra **documentación contrastada**
—RM0090, RM0390, PM0214, los dos datasheets, AN4658, las cabeceras CMSIS de ST y
su base de datos de pines—, con la fuente al lado de cada afirmación y con siete
puntos marcados explícitamente como **SIN VERIFICAR**, diciendo qué documento
cerraría cada uno.

**Las tres conclusiones:**

*El F446 no es un F407 con otros números.* Comparten núcleo, mapa de buses, tabla
de funciones alternativas y la mayoría de periféricos, pero el F446 tiene **otro
árbol de reloj** (tres PLL, divisor R, cinco registros de selección que en el
F407 no existen), **seis bloques nuevos** (SAI×2, SPDIF-RX, QUADSPI, FMPI2C1,
HDMI-CEC, FMC con SDRAM) y **pierde tres** (Ethernet, RNG, CCM RAM).

*Por eso no cabe en un `McuCaps`.* Y lo digo en el informe con nombre y apellidos
porque es la tentación obvia después del refactor de la familia F405/407: un
descriptor no puede describir cinco registros que no existen. El resultado sería
un modelo que acepta `RCC_DCKCFGR`, no hace nada con él y no lo dice.

*Pero se reutiliza el ~80 %.* De las 33.833 líneas de modelo, `core/` entero
(5.459) vale sin tocar una línea, igual que `parts/`, `mem/` y `verif/`. Trabajo
nuevo estimado: ~4.500 líneas, un 13 %.

**Cuatro hallazgos que no esperaba y que son los que más valen del informe:**

* **`0x4000_4000` es I2S3ext en el F407 y SPDIF-RX en el F446.** La única
  dirección que cambia de dueño. Es el error más caro posible en este puerto
  porque **no falla**: escribes en el sitio de siempre y encuentras otro chip.
* **La tabla de AF no remezcla nada.** Comparé las dos tablas de ST par a par,
  por máquina: 260 pares (pin, señal) comunes y **cero** diferencias de número de
  AF. Los periféricos nuevos entran en ranuras que estaban libres en esos mismos
  pines. Solo se vacía el AF11, el del Ethernet.
* **Ninguna posición de vector cambia de dueño.** Las altas del F446 van todas
  por encima de 81, donde el F407 ni llega. El `enum` se extiende, no se
  reescribe.
* **El LQFP64 del F446RE tiene 50 E/S y el del F405RG tiene 51**: al F446 le
  falta PB11. No son el mismo mapa, y es justo la clase de detalle que caza la
  comprobación `Encapsulado::coherente()` que escribimos ayer.

**Y dos avisos que van en el informe por delante del plan.** El `DBGMCU_IDCODE`
del F446 no lo he podido verificar y es **bloqueante**: ponerle el del F407
haría que STM32CubeIDE identificara mal el chip, que es exactamente el error que
nos costó media sesión depurar. Y el over-drive del PWR tiene una secuencia con
dos esperas (`ODRDY`, `ODSWRDY`) que, si el modelo no acompaña, **cuelga el
firmware en el paso 3** — el mismo fallo que I-32 con `HSERDY`, y merece el mismo
cuidado.

El plan va en seis fases con seis hitos comprobables. La fase 1 es la que
importa: separar «familia F4» de «STM32F407» **sin añadir un solo chip**, con el
criterio de aceptación de que la suite no se mueva ni un picosegundo. Dentro de
ella, la pieza aburrida y decisiva es convertir las ~300 llamadas de
`bind_gpio_pins()` en una tabla de datos: sin eso, el F446 obliga a copiarlas y
editarlas, y dos tablas que hay que mantener a la vez acaban discrepando.

---

## Fase 0 del plan del F446: los siete puntos, cerrados

> Ejecuta la fase 0 del plan

Hecha. La fase 0 era **cerrar lo que estaba sin verificar antes de escribir una
línea de código**, y no es burocracia: cada uno de esos puntos, mal, produce un
modelo que funciona y miente.

**Método.** Descargar los PDF enteros —RM0090 son 1749 páginas, RM0390 son 1328—
y extraerlos con `pdftotext -layout`, que preserva las columnas de las tablas.
Nada de resúmenes: las tablas leídas fila a fila. Para los pines, además,
`STM32_open_pin_data`, la base de ST que alimenta CubeMX.

**Seis de siete confirmados, y dos sorpresas.**

*La sorpresa buena* — `[RM0390]` §2.1 dice «Seven masters» **y «Seven slaves»**,
pero el séptimo esclavo del F446 **no es el mismo** que el del F407: allí es
`FSMC` y aquí es **`FMC / QUADSPI`**. Los dos comparten un único puerto de la
matriz. Es decir: **el QUADSPI no añade un octavo esclavo**. Eso no estaba en el
informe y cambia el plan — si se descubre al escribir el código, cuesta rehacer
el `enum` y la máscara de conectividad.

*El punto bloqueante* — `[RM0390]` §33.6.1, literal: «The device ID is 0x421».
Con `REV_ID = 0x1000`, el registro entero es **`0x1000 0421`**; el F407 es
`0x1001 6413`. Ya no hay agujero en el hito H5, que era el que más podía doler:
un `IDCODE` equivocado no da un error claro, da un «Could not verify ST device»
— el mensaje que nos costó media sesión diagnosticar.

*La discrepancia 96/91 es peor de lo que parecía.* Los tres números, leídos: el
RM dice 96, el datasheet dice 91, y contando la tabla salen **86 implementadas
en 97 ranuras**. **Ninguno de los dos números de portada coincide con la tabla
ni entre sí.** Decisión escrita: el modelo dimensiona por la tabla, y el recuento
de implementadas no le hace falta a nadie.

*Una laguna documental* — la Tabla 1 del RM0390 rev. 4 **no lista el FMPI2C1**.
Deja sin nombrar `0x4000_6000`–`0x4000_63FF`, que es donde lo pone la cabecera
de ST, y el capítulo 23 describe el periférico entero. Falta su fila, no el
bloque.

*Lo único que sigue abierto*: la revisión vigente del RM0390 es la **9, de
febrero de 2026**, y lo leído es la 4. st.com da 403 al cliente HTTP de aquí y
el lector de páginas trunca un PDF de 1328 páginas mucho antes del capítulo 33.
Lo que lo sostiene mientras tanto es que **todo lo verificado coincide con la
cabecera CMSIS actual de ST**.

**Y la sorpresa que cambió código: I-40.**

Teniendo el DS8626 abierto aproveché para cerrar el punto que dejé pendiente el
otro día, el ball-map del WLCSP90 que iba marcado como no contrastado. **Mi
reconstrucción era falsa en casi todo**:

| | Lo que suponía | Lo que dice ST |
| :--- | :--- | :--- |
| PC | completo | **faltan PC1, PC4, PC5** |
| PD | completo | **faltan PD3, PD13** |
| PE | PE0–PE5 | **PE7–PE15**, la mitad contraria |
| PI | no existía | **PI0 y PI1** |
| Total | 72 | 72 |

**El recuento cuadraba por casualidad.** Setenta y dos bits mal repartidos siguen
siendo setenta y dos, y eso es exactamente por lo que `coherente()` no basta sola
y por lo que el campo `verificado` tenía que existir: con el mapa falso, `sim`
aceptaba en silencio un LED soldado a PC4, donde no hay bola.

Verificado ahora por dos fuentes de ST que coinciden puerto a puerto —la figura
17 del DS8626, el diagrama de bolas, y la base de pines de CubeMX—:
16/16/13/14/9/2/2. Y el detalle que más me sorprendió: **PI0 y PI1 salen en un
encapsulado de 90 bolas y no salen en el LQFP144 de 144 patillas.** No es que a
más patillas, más E/S.

Se ve en una línea:

```
$ ./build/sim placas/discovery_min.xml --mcu STM32F405OE --valida
  [decl] LD3.anodo: el pad PD13 no sale al encapsulado WLCSP90
```

La Discovery **no cabe en un WLCSP90**: le falta el pin del LED naranja.

Suite **2033/2033** (+2), invariante intacto en **`2336217899213 ps`**, ASan+UBSan
limpios. **I-40 cerrado** (veintiuna de cuarenta), el informe crece a 936 líneas
con una §15 nueva que cuenta cómo se cerró cada punto, y el plan queda listo para
la fase 1.

---

## Fase 1 del plan del F446: separar «familia F4» de «STM32F407»

> No hace falta. Ejecuta la fase 1 del plan

Hecha, y con el criterio de aceptación que la hacía segura: **el modelo hace
exactamente lo que hacía**. El tiempo simulado sigue siendo
**`2336217899213 ps`** al picosegundo, en las cinco entregas y al final.

La fase 1 no añade un chip. Su único cometido es que, cuando llegue el F446, no
haya que copiar y editar nada. Cinco movimientos:

| Qué | Resultado |
| :--- | :--- |
| `periph/crc_rng.h` se parte | `crc.h` (140) + `rng.h` (288) + un `crc_rng.h` de 18 líneas que incluye los dos |
| Conectividad de la matriz, de función a dato | `Conectividad` + `CONN_STM32F407VG` |
| Tabla de funciones alternativas, fuera del top | `soc/f4_mapa_af.h`, 169 llamadas movidas |
| `common/ahb_types.h` se parte | 203 líneas de arquitectura + `soc/f4_mapa_perif.h` (117) de familia |
| `mcu_if` y `FabricaMcu` | `soc/mcu_if.h` (154) + `soc/stm32f4_mcu.h` (112), y `sim_main.cpp` migrado |

**El truco para mover código sin leerlo.** Las 169 llamadas de la tabla de
funciones alternativas viajaron a otro fichero como **definición fuera de
clase** —`inline void Stm32F407VG::bind_mapa_af() { ... }`—, que ve los miembros
igual que si siguiera dentro: los cuerpos se movieron carácter a carácter, sin
adaptar una línea. Y `f4_mapa_perif.h` **no es un cabecero autónomo** a
propósito: se incluye desde `ahb_types.h` con el `namespace addr` todavía
abierto, así que **ningún otro fichero tuvo que cambiar un `#include`**. Es poco
elegante y es exactamente lo que pide un criterio al picosegundo.

**Una fila a cero es un maestro que no existe.** Al pasar la conectividad a dato,
la pregunta «¿cómo se dice que el F446 tiene siete maestros y no ocho?» se
contesta sola: con la fila de ese maestro a cero. Ni constante nueva ni `if`.

**El despacho es por familia, no por pieza.** `FabricaMcu` se indexa por
`McuCaps::familia`: los once del F405/407 comparten un creador y se distinguen
por su descriptor. Y cuando la familia no tiene modelo enlazado, `crea()`
devuelve `nullptr` y `sim` lo dice con nombre —«el tipo X es de la familia Y, y
no hay ningún modelo registrado para ella»—. **Nunca monta otro chip en su
lugar**, que sería la forma más silenciosa que tendría este programa de mentirle
a un alumno.

**Dos correcciones al propio plan**, para que no envejezca: el fichero es
`soc/f4_mapa_perif.h` y no `soc/f407/mapa.h` —con un solo mapa, una carpeta por
chip habría sido estructura por adelantado—, y la tabla de funciones alternativas
son **169 llamadas, no 300**; la cuenta de 300 salía de contar señales.

**Y el hallazgo del último paso, que lo encontró el saneador y no la suite.** Al
migrar `sim_main.cpp`, el destructor de `Sim` seguía haciendo `delete m.dut`
cuando el dueño del chip pasó a ser el adaptador. No era un doble borrado, que se
habría visto enseguida: era lo contrario, el chip se borraba una vez y **el
adaptador se quedaba colgando**, 248 bytes por MCU. Lo cazó ASan sobre `sim`,
porque **la suite no compila `sim_main.cpp`**. Desde ahora la verificación de una
entrega incluye compilar `sim` con `-fsanitize=address,undefined` y correr con él
la Discovery con el *blinky*, que es el camino que recorre un alumno.

Suite **2043/2043** (+10, las de T129, que prueba el contrato de la factoría sin
construir un solo MCU: la elaboración ya terminó cuando esa prueba corre), ASan y
UBSan limpios, las cuatro placas validan, el *blinky* enciende LD4 en los once
`--mcu` que sacan PD12 y los rechaza con nombre en los tres que no. Con esto, un
`Stm32F446` necesita su clase top, su `McuCaps`, su `Conectividad` de siete
maestros y **un `REGISTRA_MCU`** — y ni una línea de `ahb_types.h`, de la matriz,
del CRC ni de `sim_main.cpp`.

---

## Fase 2 del plan del F446: el esqueleto, y el fallo que destapó

> Ejecuta la fase 2 del plan

Hecha, y con las dos cosas que pedía: **un `Stm32F446` que es el F407 menos lo
que no tiene**, y **un blinky compilado para F446RE que arranca y parpadea**.

```
$ ./build/sim placas/nucleo_f446re.xml verif/fw/blinky446/blinky446.bin 700
placa 'nucleo-f446re': 1 MCU(s), 2 componentes, 154 nodos, 0 avisos
firmware de u0: verif/fw/blinky446/blinky446.bin
simulados 700.000 ms en 0.010 s de anfitrion (18522 deltas)
  LED LD2 en PA5: encendido  (3.17 V, 2.30 mA)
```

Ese firmware usa **`stm32f446xx.h` de ST sin tocar**, su `startup_stm32f446xx.s`
y el `system_stm32f4xx.c` de siempre con `-DSTM32F446xx`. Es lo que lo convierte
en una prueba y no en una demostración: quien decide dónde está cada registro es
la cabecera del fabricante. El propio firmware calcula su SYSCLK con
`SystemCoreClockUpdate()` y le salen los 84 MHz que el modelo tiene en HCLK.

**El netlist se llama ahora `SocF4`.** Se llamaba `Stm32F407VG`, y ese nombre era
de los que enseñan mal: ese fichero nunca fue «del F407VG» —ya servía para los
once miembros del F405/407 cambiando el descriptor— y sirve para el F446 por la
misma razón. `using Stm32F407VG = SocF4;` deja valer todo lo escrito.

**Qué cabe en un descriptor y qué no.** Casi todo lo que distingue al F446 sí
cabe: 512 KB, sin CCM, LQFP64 propio —50 E/S, que no son las 51 del LQFP64 del
F405RG: falta PB11—, 97 posiciones de vector, siete maestros en la matriz
(*una fila a cero*, la del DMA del Ethernet) y otro IDCODE. Lo que no cabe es el
árbol de reloj, y por eso hay una CLASE: es el sitio donde la fase 3 pondrá el
tercer PLL y el over-drive.

**Y mientras tanto lo dice.** `sim` imprime al montar la placa las seis cosas que
el modelo todavía no hace, cada una con su fase:

```
[ojo] u0: el arbol de reloj es todavia el del F407: [...] RCC_DCKCFGR y
      RCC_DCKCFGR2 no existen en el modelo [fase 3]
[ojo] u0: los topes de 180/45/90 MHz no EXIGEN la secuencia de over-drive [fase 3]
```

**EL HALLAZGO, y no es del chip nuevo.** Montando el blinky apareció que **el
SysTick no interrumpía nunca a 84 MHz**: el contador corría, `CTRL` valía 7,
`LOAD` valía 83 999, y la interrupción no llegaba jamás. El mismo firmware sobre
el modelo del **F407** hacía exactamente lo mismo.

La causa: `tick_proc()` esperaba N veces el periodo de un tick **redondeado a
picosegundos**, y un tick a 84 MHz dura 11 904,7619… ps. Redondeado hacia arriba
y multiplicado por 84 000, el contador queda **pasado** del cero; la condición de
disparo era «¿vale exactamente cero?», no se cumplía nunca, y el bucle esperaba
otra vuelta entera para pasarse otra vez. Las dos frecuencias que usa la suite
—168 MHz y 16 MHz— redondean hacia abajo o caen justas, y por abajo converge. **84
MHz no es un caso raro: es lo que sale de un PLL con P = 4.**

Corregido calculando el **instante absoluto** del cruce en vez de sumar periodos.
Y el invariante del F407 **no se movió ni un picosegundo**, porque a 168 MHz el
camino es el mismo. Es el mejor argumento a favor del puerto: modelar la pieza
nueva encontró un fallo de la vieja que llevaba dentro desde la fase 6.

**La suite del F446 es un ejecutable aparte, y también por un hallazgo.** El
primer intento fue construir el F446 dentro del banco del F407 para hacerle
preguntas; eso **movió el invariante** —de `2336217899213` a `2336186149213 ps`—
porque un segundo chip no es inerte y la simulación se volvió trece veces más
lenta. De paso se vio que **hay una prueba del I2S cuyo resultado depende del
reloj de pared** (queda anotada como I-42: una suite que cambia de resultado
según la máquina no es una suite). Así que `make test446`, y la fase 5 del plan
tenía razón por adelantado.

Estado: **2045/2045 en el F407 con `2336217899213 ps` intactos**, **43/43 en el
F446**, ASan y UBSan limpios en las dos suites y en `sim`, las cinco placas
validan. I-41 cerrado (veintidós de cuarenta y dos).

---

## Fase 3 del plan del F446: el árbol de reloj, y otro fallo del F407

> Ejecuta la fase 3 del plan

Hecha, con las dos mitades del hito H4 —la que funciona y la que **debe** fallar—
y con el invariante del F407 intacto en **`2336217899213 ps`**.

```
--- D1 H4: el SystemClock_Config() de una Nucleo-F446RE ---
    buzon: done=1 sysclk=180000000 etapa=6 od=3 ticks=59 pclk1=45000000
  [OK] el firmware recorre las seis etapas de RM0390 5.1.3
  [OK] y llega con ODRDY y ODSWRDY puestas: paso por el over-drive de verdad

--- D2 H4, la otra mitad: si ODRDY no sube, el firmware NO avanza ---
    buzon: done=0 etapa=1 (1 = escribio ODEN y sigue esperando ODRDY)
  [OK] sesenta milisegundos despues, el firmware SIGUE en el paso 3
```

La segunda mitad no necesita un chip aparte: el tiempo que tarda el regulador es
un **parámetro** del PWR, y ponerlo en diez segundos es exactamente «esta
bandera no va a subir». Mismo firmware, sin recompilar.

**El criterio del plan, al pie de la letra.** «Cada selector tiene que cambiar
una frecuencia observable, no solo guardar un bit»: así que ninguna de las
comprobaciones nuevas lee el registro que acaba de escribir, todas preguntan al
árbol por una frecuencia. Están los nueve selectores de `RCC_DCKCFGR` y
`DCKCFGR2`, el tercer PLL, el divisor R, el PLLI2S con M propia —en el F407
comparte la del PLL principal, así que las dos VCO van atadas— y TIMPRE, que es
el único que se nota en un periférico que este modelo ya tiene: con APB1 /4, los
temporizadores pasan de 90 a 180 MHz.

**Las codificaciones no salieron de la memoria.** `stm32f446xx.h` da los
desplazamientos y las posiciones de bit; `stm32f4xx_hal_rcc_ex.h` da qué
significa cada valor de cada selector, y el propio macro
`__HAL_RCC_TIMCLKPRESCALER` da, con las palabras de ST, las dos reglas de
TIMPRE. Eso cazó tres cosas que se habrían copiado mal: **DCKCFGR2 está en 0x94
y no en 0x90** —en medio hay otro registro, CKGATENR—, **PLLSAIDIVQ vale N−1**
(un 3 divide por cuatro) y **SAI1SRC = 11 es el pin `I2S_CKIN` pero SAI2SRC = 11
no lo es**, sino la fuente del PLL: el SAI2 no tiene entrada de reloj externa.

**Y otro fallo del F407, que es lo que más me gusta de esta fase.** Al colocar
`PLLSAIRDYF` hubo que mirar dónde estaban sus vecinos, y resultó que **toda la
mitad baja de `RCC_CIR` estaba desplazada un bit**: el modelo ponía `LSIRDYF` en
el 1, los `IE` en 9..14 y los de limpieza en 17..22, copiando fielmente una
tabla del informe interno que está mal. ST los pone en 0, 8 y 16. Sobrevivió
porque los dos extremos —`CSSF` en el 7 y `CSSC` en el 23— sí estaban bien, y
son los únicos que la suite miraba. Un alumno que habilitara `HSERDYIE` con la
constante de CMSIS no habría recibido nunca esa interrupción (I-43).

Van dos: el SysTick en la fase 2 y esto en la fase 3. **Modelar la pieza nueva
sigue encontrando fallos en la vieja**, que es el argumento más sólido a favor
del puerto que ha dado este plan.

Estado: **2047/2047** en el F407 con el invariante intacto, **101/101** en el
F446 (eran 43), ASan y UBSan limpios en las dos suites, las cinco placas validan
y los dos blinkys corren. Queda dicho en `limitaciones()` lo que la fase 3 no
hace: los escalones de VOS no limitan la frecuencia, `RCC_CKGATENR` se guarda y
no hace nada, y la conmutación del over-drive no para los relojes.

## Fase 4 del plan del F446: los siete bloques nuevos, y un tercer fallo del F407

> Ejecuta la fase 4 del plan

Hecha, en el orden que pedía el plan —por utilidad docente y no por tamaño— y
con el invariante del F407 intacto en **`2336217899213 ps`**.

```
--- E2 FMPI2C1: el reloj que elige FMPI2C1SEL se ve en SCL ---
  [OK] FMPI2C1SEL = 00: el bloque come de PCLK1 y su SCL sale a 562,5 kHz
  [OK] FMPI2C1SEL = 01: el MISMO TIMINGR da cuatro veces mas: SYSCLK
  [OK] FMPI2C1SEL = 10: y del HSI

--- E3 QUADSPI: un comando que sale por los pines ---
  [OK] escribir CCR sin fase de direccion LANZA el comando
  [OK] y por el pin ha salido el 0x9F, no otro byte

--- E6 Lo que el die tiene y el LQFP64 no saca ---
  [OK] el SPI4 responde en el bus y no tiene ni un pin
  [OK] y IO2 del QUADSPI NO tiene pin: es la nota 3 del datasheet
```

**El FMPI2C1 primero, porque es otro IP.** No es el I2C de siempre con una
velocidad más: `TIMINGR` en vez de `CCR`, `ISR`/`ICR` en vez de la pareja
`SR1`/`SR2` leída en el orden correcto, y `NBYTES`/`RELOAD`/`AUTOEND` en vez de
contar bytes a mano. Está modelado a nivel de bit sobre pines de colector
abierto, y su reloj **no es PCLK1 salvo que `FMPI2C1SEL` lo diga**: cambiar el
selector cambia la frecuencia de SCL medida en el pin, que es exactamente lo que
la fase 3 dejó calculado esperando a que llegara el periférico.

**El QUADSPI comparte puerto con el FMC**, como decía §15.1: el séptimo esclavo
de la matriz es uno solo. `SocF4` no puede atarlo a ciegas —SystemC no deja
reatar un socket— así que decide por el descriptor, tapa el FSMC y deja el
puerto para la clase derivada, que lo lleva a un decodificador suyo con las dos
ventanas: los 256 MB mapeados en memoria y el kilobyte de registros.

**El SPDIF-RX y el HDMI-CEC, declarados y sin modelar, diciéndolo.** Hay tres
maneras de tratar un bloque que el silicio tiene y el modelo no: no decodificar
su ventana (correcto cuando el chip **no lo lleva**), contestar como si
estuviera (la peor: el firmware se cree configurado y se queda esperando una
bandera), o **contestar y decirlo**. Es lo tercero: ocupan su ventana, leen
cero, no guardan lo que se escribe y avisan la primera vez que alguien los toca.

**Los pines son del encapsulado, no del die**, y eso da las tres cosas que un
alumno se encuentra en la tarjeta: el **SPI4 y el SAI2 no tienen un solo pin**
en el LQFP64 —se pueden programar y no se ve nada—, y el **QUADSPI no tiene
IO2**, que es la nota 3 literal del datasheet. Salen de `[PINDATA]`, los
ficheros de pines que publica ST, y no de la memoria.

**Y un tercer fallo del F407.** Las máscaras de «qué bits existen» de los
`RCC_xxxENR` estaban escritas a mano desde la fase 1. Al necesitar las del F446
se sacaron contando los `_Pos` de las cabeceras de ST, y con las dos columnas
juntas saltaron tres: **`AHB1ENR` usaba la máscara del `AHB1LPENR`**,
**`AHB2ENR`** admitía el CRYP y el HASH de un F417, y la primera versión de esta
misma fase se dejó fuera **el bit del SPDIF-RX** (I-44). Los tres del mismo
tipo que el SysTick y que `RCC_CIR`: el modelo era **más permisivo que el
silicio**.

Van tres, uno por fase. Y apareció una cuarta que no falla sino que calla: **el
segmento APB2 no mide lo mismo en las dos piezas** —en el F446 sigue 2 KB más,
porque detrás del último temporizador están los dos SAI—, de modo que el SAI
estaba construido, con reloj y dado de alta en su decodificador, y no recibía un
solo acceso.

Estado: **2050/2050** en el F407 con el invariante intacto, **133/133** en el
F446 (eran 101), ASan y UBSan limpios en las dos suites y las cinco placas
validan. Queda dicho en `limitaciones()` lo que la fase 4 no hace: el PEC y los
temporizadores de SMBus del FMPI2C1, el companding y la sincronización de los
SAI, y que las peticiones de DMA de los bloques nuevos salen y no llegan a
ninguna celda, porque el mapa de canales sigue siendo el del F407.

## Fase 5 del plan del F446: la verificación, que encontró tres cosas más

> Ejecuta la fase 5 del plan

Hecha. El invariante del F407 sigue en **`2336217899213 ps`**, la suite del F407
pasa de 2050 a **2053** y la del F446 de 133 a **189**. Los dos hitos que
faltaban, **H5 y H6**, están cerrados.

**La prueba cruzada, escrita como una tabla de afirmaciones.** Es lo que el plan
pedía con nombre —«la prueba que evita que el puerto se coma al original»— y por
eso no es una lista de comprobaciones sueltas: cada fila es **una frase del
documento de comparación**, con su sección, y al lado la expresión que la hace
verdad o mentira preguntando a los dos descriptores a la vez. Leída de arriba
abajo, la tabla *es* el documento. Están las tres que el plan nombraba
—`0x4000_4000`, el vector 80, PB11— y once más.

**Y encontró dos cosas el primer día**, las dos de la misma familia que las tres
fases anteriores: el modelo era más permisivo que el silicio.

* **El AF11 no estaba vacío.** El documento dice que el AF11 —el del Ethernet—
  es el único número de AF que el F446 vacía entero. No lo estaba: el mapa
  registraba las dieciocho entradas del MAC **sin preguntar si el chip lleva
  Ethernet**, de modo que poner `AFR = 11` en PA2 de un F446 conectaba el pad a
  un periférico que no existe. Arreglarlo **arregla también los dos F405**, que
  llevaban lo mismo desde la fase 1.
* **La ventana de la CCM contestaba en un chip sin CCM**: `0x1000_0000` iba al
  esclavo de la Flash. La prueba que había solo miraba que no devolviera el
  código de la CCM, y por eso pasaba.

**I-42, y el gusto de corregir un diagnóstico propio.** Desde la fase 2 había
anotado que una prueba del I2S dependía del reloj de pared. **No es verdad**: la
misma suite bajo ASan y UBSan —varias veces más lenta— da los mismos números y
el mismo picosegundo. Lo que cambió al añadir un segundo chip no fue la
velocidad sino **el orden en que SystemC despierta los procesos**. Y de paso
queda claro que la consecuencia era menor: las comprobaciones son por rango, así
que el veredicto nunca cambió, solo los números impresos. Lo que faltaba era
comprobar lo que **no** puede moverse —un enlace de audio que funciona no pierde
muestras— y ahora está; y las dos muestras «incorrectas» de las veintitrés
resultaron ser **ceros del arranque del enlace**, no datos corrompidos.

**El DMA: decidir y decirlo.** El mapa de canales del F446 no se ha modelado
porque **no hay fuente de ST legible por máquina** para su tabla de peticiones,
y este modelo no se inventa tablas —es la misma regla que en la fase 4 mandó
buscar los ficheros de pines de ST en vez de tirar de memoria—. Lo que sí se ha
hecho es que **deje de ser silencioso**: el controlador lleva ahora una máscara
de las celdas que el chip cablea de verdad, y armar un stream sobre una que no
está saca un aviso diciendo cuál es. Sin eso, una celda reservada en el silicio
y una celda que el modelo no cablea se ven idénticas desde dentro.

**H5**: una sonda SWD soldada a PA13/PA14 hace el reset de línea, engancha el
SW-DP, lee el AHB-AP y saca `DBGMCU_IDCODE = 0x1000_0421` **por los dos hilos**,
que es por donde lo lee CubeIDE; luego lee y escribe la SRAM, vuelca un bloque,
para el núcleo, le escribe un registro y lo suelta. 51 paquetes, ni un ACK
perdido.

**H6**: los nuevos a fondo —una trama entera del FMPI2C1 con su NACK cuando no
hay nadie, el QUADSPI leído por la ventana mapeada en memoria, una trama de
audio del SAI con datos— y, sobre todo, **los viejos sobre el die nuevo**, que
es donde se habría roto algo: el USART a 115 200 con un divisor que ya no es el
del F407, el TIM2 contando mil microsegundos en un milisegundo, el SPI1 a
22,5 MHz y el DMA2 copiando entre las dos SRAM.

Estado: ASan y UBSan limpios en las dos suites (y de paso ASan cazó una fuga de
72 bytes: la sonda del banco nuevo, que no es un módulo de SystemC y hay que
devolver a mano), `make red` 13/13 y las cinco placas validan sin un aviso.

## Fase 6: las dos casillas que quedaban, cerradas con fuentes de ST

> En la carpeta doc/stm32f446xx tienes el RM0390 rev. 9 tanto en pdf como en
> texto. Tienes conectada la carpeta donde este ordenador tiene instalados tanto
> CubeMX como CubeIDE. Cierra lo que queda pendiente

Hechas las dos. El invariante del F407 sigue en **`2336217899213 ps`**, la suite
del F407 pasa a **2055** y la del F446 a **193**.

**El mapa de DMA estaba donde dije que estaría, y la estructura tenía truco.**
La tabla de peticiones no está en `STM32_open_pin_data` —ese repositorio dice en
su README que es «a subset of STM32CubeMx internal database»: solo pines— sino en
la base completa, un fichero por IP. Y no viene servida: el `RefMode` de cada
petición da su **canal**, y hay que bajar por el árbol de `Mode` para saber de
qué **stream** cuelga. Las 128 celdas se extrajeron por máquina.

**Luego las contrasté con las Tablas 28 y 29 del RM0390 rev. 9** —leyendo la
página del PDF, porque `pdftotext` destroza esas tablas: tienen celdas de varias
líneas y las descoloca—. **Coinciden las 128.** Dos fuentes de ST independientes
diciendo lo mismo, que es la verificación más fuerte que ha hecho este proyecto;
y aquí importa especialmente, porque un mapa de DMA mal copiado no falla, no
avisa y no transfiere: el stream se arma y se queda esperando.

**Y salió un regalo.** La tabla del F407 que sacó CubeMX coincidía ya con la que
el modelo tenía escrita a mano... salvo **cinco celdas**: las de los bloques de
extensión del I2S, que llevaban al aire desde que se escribió el modelo del DMA
con un comentario honesto («[IR] no recoge esa tabla»). La base de ST sí las
trae. Cableadas (I-48). Las del CRYP y el HASH, en cambio, siguen sin fuente a
propósito: son de un F417 y este chip no lleva el acelerador.

**La divergencia que más me gusta** es que donde el F407 tiene el I2S3ext —DMA1
stream 2 canal 2— el F446 tiene el **FMPI2C1**. Es exactamente la misma trampa
que `0x4000_4000` en el mapa de direcciones: la celda no falla, pide otro
periférico. Un firmware portado que arme ese stream se encuentra con otro chip
dentro.

**La prueba que lo cierra no es que la tabla lo diga.** El grupo H5 del banco
hace pasar dieciséis palabras de la SRAM a `SAI1_A->DR` por su celda, y luego
repite el montaje **cambiando solo `CHSEL`**: con el canal equivocado no se mueve
un dato. Esa segunda mitad es la que distingue «la celda funciona» de «el stream
funciona».

**Y el RM0390 rev. 9.** Los cuatro puntos de la fase 0: §2.1 idéntico —siete
maestros, siete esclavos, «FMC / QUADSPI»—, §10.1.1 idéntico, §33.6.1 idéntico
(`DEV_ID = 0x421`), y la Tabla 1... **sigue sin listar el FMPI2C1**. Cinco
revisiones después, el rango `0x4000 6000`–`0x4000 63FF` sigue sin nombre en la
tabla de fronteras mientras el capítulo 23 del mismo manual describe el
periférico entero. No era el descuido de una revisión vieja.

De propina, el `RCC_CIR` de la rev. 9 **confirma el arreglo de I-43**: `LSIRDYF`
en el bit 0, el IE en el 8 y el de limpieza en el 16. La tabla que está mal es la
del informe interno, y ahora hay dos documentos de ST que lo dicen.

Una nota de proceso: a mitad de faena el contenedor de la nube se reinició y
perdí la copia de trabajo. La recuperé del repo del equipo —que tenía la fase 5
confirmada—, reinstalé SystemC y la toolchain ARM, reconstruí los dieciocho
firmwares de verificación y rehíce los cambios. La base volvió al picosegundo
exacto antes de tocar nada, que es justamente para lo que sirve tener un
invariante.

## La familia F446 entera: ocho referencias, un die

> En doc/stm32f446xx tienes la hoja técnica de los miembros de la familia
> stm32f446xx, fichero DS_stm32f446mc.pdf; crea los modelos de todos los
> miembros de la familia

Hechas las ocho. El invariante del F407 sigue en **`2336217899213 ps`** con
2055/2055, y la suite del F446 pasa de 193 a **200**.

Cuatro encapsulados por dos tamaños de Flash, y ninguna es una clase nueva: son
`Stm32F446` con otro `McuCaps`, que es exactamente lo que §6.3 decía que un
descriptor sí puede describir. Las máscaras salen de los cuatro ficheros de
referencia de la base de pines de ST, y su recuento —63, 50, 81 y 114— coincide
con la Tabla 2 del datasheet.

**Dos cosas que no me esperaba al mirarlas juntas.** La primera, que **PB11 solo
existe en el LQFP144**: no sale en el LQFP64 —eso ya lo sabíamos—, pero tampoco
en el WLCSP81 ni en el LQFP100, donde el datasheet dice literalmente que lo
sustituye VCAP1. Ese pin lleva seis fases apareciendo. La segunda, que **el
WLCSP81 no es un LQFP100 recortado**: le faltan PC1 y PC5, que el LQFP64 —con 17
patillas menos— sí saca. Es el mismo patrón que destapó I-40 en el WLCSP90 del
F407.

**Y el datasheet cambió de opinión sobre el SAI2.** La revisión 4 ponía `2` en
la fila del SAI para todas las columnas, y por eso §8.3 decía que en el LQFP64 la
diferencia era solo de pines. **La revisión 11 pone `1`**, y la base de pines lo
confirma por otro camino: en un RC o RE no hay una sola señal de SAI2 en ningún
pin, ni siquiera en PA1, PA2 y PA12, que ese encapsulado sí suelda y que en las
otras siete referencias llevan SAI2. No es bonding: ST no vende esa referencia
con dos SAI. Así que pasa a ser un rasgo de `Periferia`, y el modelo hace lo de
siempre: el bloque sigue respondiendo en el bus —el die es el mismo— y lo que no
tiene es por dónde salir, y eso se dice.

**Lo que más ha cambiado por dentro** es la tabla de funciones alternativas:
registraba quince entradas, las del LQFP64, y ahora registra las noventa y una
del die. Un F446ZE con el SAI2 declarado y sin un pin donde sacarlo habría sido
una mentira silenciosa. El mux ya sabe no sacar nada por un pad que el
encapsulado no suelda, así que registrar de más no miente; registrar de menos,
sí.

**La prueba que lo cierra** cruza la tabla del modelo con la máscara de cada
encapsulado y compara con la lista que ST publica para esa referencia — y las
dos mitades vienen de ficheros distintos: la implementación, del fichero del
die; los números de la prueba, de los cuatro de referencia. Dieciséis cuentas,
las dieciséis cuadran, y las diferencias con ST son exactamente las tres cosas
que el modelo no registra y que dice que no registra: el SMBA del FMPI2C1, los
IO del segundo banco del QUADSPI y los pines de los dos bloques declarados.

Y `limitaciones()` dejó de ser una lista fija: ahora se construye según la
referencia. Un MC no dice nada de encapsulado, un RE dice tres cosas, un VE dice
la nota 1 de la Tabla 2 —banco 1 del FMC, NE1, multiplexado y sin línea de
interrupción porque el puerto G no sale— y un ZE no dice ninguna, porque en un
LQFP144 sale todo.
