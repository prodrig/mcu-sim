# Trabajo pendiente de mcu-sim — revisión 1

Inventario consolidado de **todo lo que las siete fases han ido dejando fuera**,
extraído de los veintidós informes de `doc/` y contrastado contra el código de
`src/`. Fecha de corte original: fase F7 cerrada, **1811/1811 comprobaciones,
0 fallos**.

**Puesto al día con los dos planes de familia posteriores** —el del F446 y el
del F415/F417—, que cerraron puntos y abrieron otros. Estado de hoy: **tres
bancos**, `test407` (2118), `test446` (204) y `test417` (165), **0 fallos**, y
los tres invariantes intactos —`2336217899213`, `1033367277932` y
`718988288 ps`— **en Linux y en Windows, al picosegundo**.

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
  protocolo especificado; hechas las fases 0 a 7 de las 0 a 9 del plan.
- **Y un puente UART hacia el ordenador** (**P-14**): una USART del MCU hasta
  un puerto TCP con RFC 2217, y de ahí al puerto serie que ponga una
  herramienta externa. Plan de nueve fases en `doc/analisis_puente_serie.md`,
  **las nueve hechas y P-14 cerrado** el 30-09-2026, con la matriz de pruebas
  manuales a medias, aceptada así.
- **Y un fabricante nuevo** (**P-16**): los MSP432 de Texas Instruments, con
  el mismo núcleo Cortex-M4F. Plan de nueve fases en
  `doc/analisis_msp432.md`; **hechas T0 a T4**: la generalización que quita
  lo que era de ST dentro de lo que es de ARM, sin mover un picosegundo; el
  MSP432P401R con su banco, `make testp4`; la descarga y la depuración; los
  periféricos de aula con la BOOSTXL-EDUMKII; la ROM de DriverLib, con la
  que los ejemplos del SDK que usan `MAP_` corren sin tocarlos; y **T5**:
  OpenOCD de verdad programa y depura el modelo por sus pines, el MSP432 y
  el F407; y el BSL del P4, con uno propio y probado contra el de TI y
  contra BSL Scripter. **Y T6 (P-17)**: la segunda familia, el
  MSP432E401Y, con su placa, su ROM y su cargador serie, `make teste4`;
  y **T7**: su conectividad y su criptografía —CCM, dos C_CAN, EMAC con
  PHY interno y lwIP por el puente Ethernet, el cargador por Ethernet de la
  ROM y el USB como CDC-ACM por el puente USB de clase—.
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
repositorio creado, FASES 0 A 7 EJECUTADAS** — `--gui host:puerto` se
reconoce, con T130 detrás (43 comprobaciones puras); la frontera dentro del
modelo existe: observables y mandos en las piezas, el catálogo, el
muestreador y el aplicador, probados en un banco propio, `testgui`; el
transporte: el marco del protocolo en `common/proto_io.h` —el mismo fichero en
los dos repositorios— y `conecta`/`escucha` con host en `common/red.h`, con
`make gui-proto` sin SystemC; **`sim --gui` se conecta de verdad**: manda la
placa y el catálogo y no simula hasta que la ventana diga «arranca»; y **con
la simulación en marcha la ventana ve el modelo**: instantáneas, avisos y los
dos relojes; y **la ventana toca el modelo**: órdenes a los mandos de las
piezas, antes de arrancar o en marcha, con un eco por orden en el instante
real; y **la ventana lleva la simulación**: el ritmo, la pausa —el modelo
quieto y oyendo—, los pasos a demanda y parar en marcha; y **la ventana lanza
el modelo ella misma**, con un diálogo que construye a partir de lo que dice
`sim --argumentos`. El invariante del F407, intacto las ocho veces; desde la
segunda, también su recuento.

`doc/analisis_gui.md` comparaba tres escenarios y recomendaba el **2** —un solo
ejecutable Qt con la simulación en su propio hilo—. **La decisión tomada es la
de dos procesos**, el escenario 3, y el motivo por el que el análisis la
descartaba —la distribución: «un alumno tiene que instalar *una* cosa y pulsar
*un* icono»— se convierte en diseño en vez de en excusa: **la GUI escucha
primero y lanza `mcu-sim` como proceso hijo**, con lo que no hay carrera de
arranque, no hay puerto ocupado y no hay cortafuegos.

El proyecto vive en un repositorio aparte, `mcu-sim-gui`, a propósito: `mcu-sim`
tiene que seguir clonándose y compilándose **sin Qt** en cualquier máquina, que
es lo que hace que sus suites valgan en todas partes. Allí están
el plan por fases (`doc/plan_dos_procesos.md`, nueve fases) y la especificación
del protocolo (`doc/protocolo.md`).

**Lo que este repositorio tiene que crecer**, y es lo que cuenta como pendiente
aquí:

| | Qué | Fase del plan |
| :--- | :--- | :--- |
| a | ~~`--gui host:puerto`, con `localhost:3344` por omisión~~ **HECHO en la fase 0**, con las seis formas, sus siete errores y el aviso cuando el host no es la propia máquina. **Sin el argumento, nada cambia**: 2117/203/164 y `2336217899213 ps` | 0 |
| b | ~~`Observable` / `Mando` en `ExtPartBase`, y las tres primeras piezas que los declaran (`Led`, `Button`, `Crystal`)~~ **HECHO en la fase 1**: seis métodos virtuales con valores por omisión, así que las otras diecinueve piezas compilan sin tocarlas. `Led` declara `encendido` y `corriente` (mA); `Button`, `pulsado` y el mando `pulsar`; y `Crystal`, **`presente` y no `frecuencia`**, que es lo que pedía el plan: la pieza no lleva frecuencia a propósito (lo dice su ficha), y publicar una sería inventarla. El catálogo (`parts/frontera_gui.h`) numera los observables con un `id_obs` plano y escribe el XML de `T_CATALOGO`. Las comprobaciones que no gastan tiempo **no** van en `test407`, aunque el plan lo decía: por **D-12** del puente UART, lo que no necesita ese banco no mueve sus cifras. Están en el grupo G0 de `testgui` | 1 |
| c | ~~La instantánea y la cola de órdenes: los dos `SC_THREAD` de la frontera~~ **HECHO en la fase 1**: `FronteraGui`, construida siempre —en `sim` y en el banco del F407— y **sin activar**, con sus dos procesos esperando un evento que nadie notifica. La prueba es que `sim` da la misma salida y los mismos deltas con y sin ella, y que la línea del F407 en `invariantes.txt` **no ha cambiado**: 2118 y `resto` en `2240553274213 ps`. Lo que hacen en el tiempo —muestras en la rejilla del periodo, órdenes con deltas, `RES_TARDE`, el atasco que tira y cuenta— está en **`testgui`**, un quinto banco (87 comprobaciones, `26500000000 ps`), porque probarlo en `test407` movería su invariante. Una decisión que el protocolo no decía y ahora dice: **las órdenes de un instante van antes que la muestra de ese instante**, con un delta de espera en el muestreador; sin él, la muestra se adelantaba a la orden (comprobado quitándolo) | 1 |
| d | ~~`conecta(host, puerto)` y `escucha(host, puerto)` en `common/red.h`, **al lado** de las de bucle local y sin sustituirlas~~ **HECHO en la fase 2**, con IPv4 e IPv6 (los corchetes de `--gui [::1]` se quitan allí), un plazo de 5 s por dirección en `conecta` y `puerto_local()` para escuchar en el puerto 0. Las de GDB no se han tocado. Con ellas, **`common/proto_io.h`**: el emisor y el lector del marco, que comprueban magia, versión, longitud y sentido y saltan lo desconocido; es **el mismo fichero** que lee la GUI, y `make gui-proto` exige que las dos copias —y las de `protocolo.h`— sean idénticas byte a byte (**R-6** del plan, cerrado). `make gui-proto`: 40 comprobaciones sin SystemC, con dos hilos y un socket de verdad; en el trabajo rápido del CI, y en macOS y Windows junto a `make red` | 2 |
| e | ~~El saludo antes de `sc_start()`, y `--valida --gui`~~ **HECHO en la fase 3**: `common/gui_cliente.h` (`ClienteGui`, sin SystemC) y `saluda_gui()` en `sim_main.cpp`, entre construir la placa y `sc_start()`. `T_PLACA` es `Netlist::volcar_xml`, la placa declarada; `T_CATALOGO`, el `Catalogo` de la fase 1, que activa la frontera con los mismos índices. La espera de `T_ARRANCA` no tiene plazo ni gasta CPU; contesta `T_PING`, acepta `T_PARA` (termina sin simular) e ignora `T_SUSCRIBE` y `T_ORDENES` hasta las fases 4 y 5. **`--valida --gui`**: placa, catálogo y `T_FIN`, sin `T_LISTO` y sin esperar. **Errores** —nadie escuchando, ninguna versión común, sin respuesta a `T_HOLA` en 10 s, la GUI cerrando antes de arrancar—: código 2 y dicho. Al acabar la ventana, `T_FIN` con el instante real; `muere()` manda `T_FIN` con `M_ERROR`. **Y un fallo de la fase 1 destapado aquí**: las piezas que no son `sc_module` salían en el catálogo como `Crystal_1` o `Button_3` y en la placa como `X2` o `B1`, así que la ventana no podía casarlas; ahora `Netlist::construye` les pone el id del XML (`ExtPartBase::pon_id`). Pruebas: `make gui-proto` P4 (18 comprobaciones, el saludo contra una GUI falsa) y **`make gui-saludo`**, el `sim` de verdad con `discovery_min.xml` contra una ventana en Python con su propia implementación del marco (22): que espera **sin CPU y sin avanzar un picosegundo**, y que con `--gui` simula exactamente lo mismo, deltas incluidos | 3 |
| f | ~~Los avisos de `SC_REPORT` desviados al socket~~ **HECHO en la fase 4**, y con ellos todo el sentido modelo → pantalla: **`parts/enlace_gui.h`**, un `SC_THREAD` que atiende la conexión cada 100 µs simulados (como el GDB) y no habla con un socket sino con un **canal** (`common/gui_mensajes.h`): en `sim` es el socket, en `testgui` un búfer en memoria que se atasca a voluntad. Lee `T_SUSCRIBE` y `T_PING`; manda avisos, instantáneas con un `T_ESTADO` detrás de cada tanda, y un `T_ESTADO` suelto cada 250 ms de pared si no ha salido ninguno. **Contrapresión**: la salida no pasa de 256 kB, las instantáneas que no caben se tiran y se cuentan en `perdidas`, y los avisos no se tiran nunca: con 1000 sin mandar, `T_FIN` con `M_ERROR`, la conexión se cierra **y la simulación sigue**. **Los avisos** son lo que la consola enseña —lo silenciado sigue silenciado— menos el «Simulation stopped by user» del núcleo; los **de placa** (`[elec]`, `[serie]`) salen en el saludo, entre `T_CATALOGO` y `T_LISTO`. La **suscripción previa a `T_ARRANCA`** se aplica antes de `sc_start()`, y la secuencia de instantáneas se repite al picosegundo. Sin `--gui` no cambia nada: `test407` lleva un `EnlaceGui` construido y sin activar, y su línea de `invariantes.txt` no se ha movido. Con `--gui` hay más deltas —el sondeo es un proceso que despierta— y el mismo modelo: mismos LEDs, mismo instante final. Pruebas: `testgui` G9–G12 (87 → **114**, `70550000000 ps`; seis mutaciones del enlace, las seis caen) y **`make gui-marcha`** (24), el `sim` de verdad con el blinky: **seis flancos separados 100 ms** en las instantáneas, la misma secuencia dos veces, los avisos de placa, la conversación en marcha y que si la ventana se va, `sim` sigue. **Y un fallo de `--tiempo-real` destapado aquí**: con una ventana finita se simulaba entera de golpe y luego se dormía; ahora va en rodajas de 1 ms, y el freno se mide contra un ancla, sin acumular el retraso de cada `sleep` (un 10 % antes) | 4 |
| g | ~~Las órdenes: `T_ORDENES` y `T_ORDEN_HECHA`~~ **HECHO en la fase 5**: el enlace lee `T_ORDENES` y lo encola en la frontera —relativo al instante en que lo lee—, y vacía hacia la ventana un **`T_ORDEN_HECHA` por orden**, con el instante real y el resultado; **nunca se tira uno**: van como los avisos, con el mismo máximo, porque son lo único que dice cuándo se aplicó de verdad una orden. Los `T_ORDENES` que llegan **antes de `T_ARRANCA`** los guarda `ClienteGui`, todos y en orden, y `sim` los encola antes de `sc_start()`: instantes absolutos, reproducibles al nanosegundo. Validación sin silencios: pieza o mando que no existen, su eco con `RES_PIEZA`/`RES_MANDO`; fuera de rango, se recorta, se aplica y **detrás del eco un `T_AVISO`** que dice qué pieza y mando, de qué lado se salió, qué se aplicó y el rango; un `T_ORDENES` que no es un múltiplo de 16 bytes, un `T_AVISO` y ninguna aplicada. Pruebas: `testgui` G13 (114 → **126**, `75300000000 ps`; cinco mutaciones del enlace, las cinco caen), `gui-proto` P4 (61 → 64) y **`make gui-ordenes`** (23): **el ejemplo del enunciado** sobre B1 de `discovery_min.xml`, con los cuatro ecos en **exactamente 1,00 / 1,50 / 4,00 / 4,22 s**, `pulsado` en las instantáneas justo en esos intervalos, la misma secuencia dos veces, la validación y las órdenes en marcha | 5 |
| h | ~~El control: `T_PAUSA`, `T_SIGUE`, `T_PASO`, `T_PARA` y el ritmo de `T_ARRANCA`~~ **HECHO en la fase 6**. **La pausa es un bucle de reloj de pared DENTRO del proceso del enlace**: como todos los procesos de SystemC comparten un hilo, mientras no llame a `wait()` no corre ningún otro y el tiempo simulado no se mueve —ni un delta—, y el enlace sigue leyendo: duerme en el canal hasta que llega algo, contesta `T_PING`, acepta suscripciones y órdenes y manda su latido con `F_PAUSADA`. Es la salida literal a la trampa de que el socket solo se atiende si el tiempo avanza. `T_PASO` (solo con `RIT_DEMANDA`, que arranca en pausa) para en **t + ns exacto**: el enlace acorta su última espera. `T_PARA` en marcha es `sc_stop()`; `sim` lo ve, da su resumen y manda `T_FIN` con `M_PARA`. **El ritmo lo dice `T_ARRANCA`** (`RIT_REAL` con su factor es el freno de `--tiempo-real`, que con `--gui` deja de mandar) y **la ventana de tiempo**: `ventana_ns` > 0 manda; 0 es la de la línea de órdenes, o **sin fin** si no se dio —ahora se puede parar desde la ventana—, y entonces, si la ventana se va, `sim` se para solo. Pruebas: `testgui` G14 (126 → **136**, `76450000000 ps`) con un guion que hace de ventana durante la pausa, y **`make gui-control`** (29): pausa con el tiempo quieto durante 1,2 s, tres `T_PONG` y **0,000 s de CPU**; seguir, a tiempo real otra vez; parar con su `T_FIN`; a demanda, pasos de 100/250/0/50+50/150 ms que paran **exactamente** donde dicen, y **lo simulado a pasos idéntico a lo simulado de un tirón** | 6 |
| i | ~~`--argumentos`: que el programa vuelque su lista de opciones, para que el diálogo de lanzamiento de la GUI no envejezca~~ **HECHO en la fase 7**: un `<argumentos>` con los dos posicionales y las quince opciones —forma, tipo, omisión, unidad, grupo (`--gdb` y `--gdb-dap` se excluyen), repetible, ayuda, y `con_gui="no"` para lo que con la ventana no tiene sentido—, y los 29 MCUs del catálogo como valores de `--mcu`, sacados del catálogo y no escritos. Dentro de `sim_main.cpp` la lista sigue estando dos veces —la tabla y el bucle de `argv`—, y lo vigila **`make gui-argumentos`** (11): toda opción de `--help` está en el volcado y viceversa, y cada una, con su omisión o su ejemplo, la acepta `sim` de verdad. Al escribirlo apareció **I-26**: una opción desconocida se tomaba por el firmware; ahora es un error. Y `--ms` no declara omisión a propósito: sin él son 100 ms, pero con `--gui` es sin fin | 7 |
| j | `--sesion fichero.xml`: reproducir una sesión grabada **sin GUI**, que es lo que devuelve el determinismo que la interactividad quita | 8 |
| k | **Depurar con un botón pulsado** —pedido por el uso, no por el plan; plan de `mcu-sim-gui` §16—, en tres pasos y sin tocar el protocolo: (1) ~~el «switch» al lado de cada botón y *Siempre encima*~~ **HECHO**, solo en la ventana; (2) ~~**los rebotes del pulsador**~~ **HECHO**: `rebote` (2 ms por omisión; `no` es un contacto ideal), `rebotes` (5) y `semilla` en `Button`, con un generador propio y ns enteros para que el patrón sea el mismo al picosegundo en todas partes; un pulsador sin rebote no crea ningún proceso, así que **sin T132 el invariante del F407 no se mueve** (2128 y `2245527784555 ps`, comprobado); los bancos montan sus pulsadores con `pulsador()`, que pone `rebote="no"`, y B2 —el RESET de la Discovery, cuyo condensador se come los rebotes— también; **T132** (18) lo vigila sobre los dos pulsadores sueltos de T124; (3) ~~el rebote como mando en marcha, y el valor inicial de los mandos en el catálogo~~ **HECHO**: `Button` tiene un segundo mando, `rebote_ms` (continuo, de 0 a 20 ms o a lo que diga la placa si es más), que es `pon_rebote_ms()` y vale desde el siguiente movimiento del dedo; y `valor_mando()`, una consulta nueva en `ExtPartBase` —el mínimo por omisión—, sale en el catálogo como `valor="…"` en cada `<mando>`, para que la pantalla nazca donde está el modelo. Es un atributo más del XML: **el protocolo no cambia**, y una ventana anterior lo ignora. `testgui` 136 → **142**, sin mover su tiempo; `gui-saludo` 22 → **23** (B1 dice `valor="2"`, B2 `valor="0"`); (4) ~~**cuántos rebotes, también en marcha**~~ **HECHO**: un tipo de mando nuevo, **`discreto`** —un entero del rango, que la ventana pinta como desplegable—, y el tercer mando del pulsador, `rebotes`, de 1 a 9 (o lo que diga la placa); y `rebotes` pasa a ser el número **exacto** y no un máximo, porque lo que sirve para enseñar es «con 3 rebotes, la EXTI ve 4 flancos». El invariante del F407 no se mueve —T132 cambia sus comprobaciones a números exactos, con el mismo tiempo—; `testgui` 142 → **145**, `gui-saludo` 23 → **24**. Una ventana anterior pintaría el tipo nuevo como un botón: hay que actualizar las dos | — |
| l | ~~**El MCU no se supone, y una placa puede no llevar ninguno**~~ **HECHO**, pedido por el uso —una placa sin chip, solo electricidad, para mirarla en la ventana—. (1) **Sin STM32F407VG por omisión**: el chip se declara con `<mcu>` o con `--mcu`, y las placas de `placas/` lo declaran ya; **si están los dos, gana `--mcu`**, que sustituye el tipo del `<mcu>` y lo dice (con dos o más `<mcu>` es un error). `--mcu` deja de tener omisión en `--argumentos` —lleva un `ejemplo`— y `banco.xml`, que se genera y no declara chip, se monta con `--mcu STM32F407VG`. (2) **Sin `<mcu>` ni `--mcu`, la placa va sin MCU**: no hay pads; cada `<nodo>` es externo sin decirlo; un nodo con nombre de pin —`PD12`, `VDD`, `u0.PA0`— se rechaza con la pista de declarar el chip; y un firmware, `--gdb`, `--gdb-dap`, `--port`, `--traza-gdb` y `--ondas` se rechazan diciendo por qué. `T_HOLA` lleva `mcu=` vacío. (3) **Dos piezas nuevas, `Fuente` y `Gnd`**: una tensión fija (3,3 V y 0 V) con `limite_ma` opcional —«no» por omisión—, que se comporta como una fuente de laboratorio: si la carga pide más, entrega el límite justo, deja caer la tensión, pone `sobrecorriente` a 1 y lo avisa una vez (`T_AVISO`). Observables `corriente` (mA) y `sobrecorriente`, marcado en el catálogo con un atributo nuevo, **`alarma="si"`**, para que la ventana lo pinte en rojo; una ventana anterior lo ignora y **el protocolo no cambia**. Un nodo con una fuente y cargas es un raíl y no un conflicto; dos fuentes en un nodo, sí. `placas/fuente_y_masa.xml` es el ejemplo, y **`make gui-sin-mcu`** (36) lo vigila contra el `mcu-sim` de verdad. Ningún invariante se mueve | — |
| m | ~~**El dibujo de cada placa**, para las ilustraciones de `mcu-sim-gui` (su `doc/analisis-uso-ilustraciones.md`, fase 4)~~ **HECHO**: `ilustracion="x.svg"` en la `<placa>` —o, sin decirlo, el SVG que se llama como ella—, la **tabla de enlaces** `<ilustracion><enlace pieza elemento [efecto]/>`, el del montaje en `<placa id ilustracion>` de un `<sistema>`; `mcu-sim` lo busca y lo manda en el saludo, **un `T_ILUSTRACION` por fichero** con las placas que lo usan (tipo nuevo: la versión no sube). Un dibujo que no está, que no es SVG o que pasa de 2 MiB es un aviso; una tabla con una pieza que no hay, un error. La raíz `<placa>` ya no acepta atributos desconocidos. `placas/nucleo_f446re.svg`, el primero. `make gui-sistema` C10 (61 → **78**). Sin `--gui` no cambia nada, salvo una línea de consola por dibujo | — |
| n | ~~**Una barra de 8 LEDs con el común en el conector**~~ **HECHO**, pedido por el uso —los módulos DM41A08—. (1) **`Led` con las dos patillas a la vista**: con `anodo` y `catodo` declarados a la vez —antes, un error—, ningún extremo va por dentro a masa ni a VDD; la rama entre los dos nodos se resuelve exacta, con el Thevenin de cada lado sin el LED, y `a_vss` o `vdd` ahí son un error. Con un extremo al aire no luce, y `sim` lo dice. El montaje de una patilla no cambia. (2) **`placas/barra8_{anodo,catodo}_comun_{rojo,azul}.xml`**: ocho LEDs con 2 kΩ, un J1 de 9 pines con el común en J1.9, y su dibujo (`barra8_rojo.svg`, `barra8_azul.svg`). (3) **`placas/barra8_en_nucleo.xml`** y `verif/fw/barra8_demo` (`make fwplacas`): la barra cableada a la Nucleo con el común en D10, y una luz que corre con el común a 1 y luego a 0, que vale para las dos polaridades. `make gui-sistema` C11 (78 → **90**). Ningún invariante se mueve | — |
| o | ~~**Pines de conector con nombre, y los morpho de la Nucleo**~~ **HECHO**, pedido por el uso. (1) **`Conector` con `nombres=`**: uno por pin, `-` para el que se queda con su número; un pin con nombre se llama solo por él (`P1.COM`; `P1.1` sigue siendo PB1), el número sigue mandando en la forma y en los acoples, y `T_PLACA` lleva los nombres. Errores: los que no son uno por pin, los repetidos, los que no empiezan por letra y los que harían de `ID.nombre` un pad o una alimentación. (2) **Las barras de 8 LEDs con `P1`**: `COM` a la izquierda y `D1`..`D8`, rotulados en el dibujo. (3) **CN7 y CN10 en `nucleo_f446re.xml`**, cada pin a su pad como en la tarjeta [UM1724; Zephyr `st_morpho_connector.dtsi`], y el dibujo de la Nucleo rehecho a escala, con el rótulo de cada pin de los seis conectores. (4) **`placas/nucleo_f446re_barra8ac_azul.xml`** y `verif/fw/barra8_morpho`. (5) La validación eléctrica reconoce un bus por su hilo: el común de la barra, bus en su placa, llevado a VDD por un hilo, ya no avisa. `make gui-sistema` C12 (90 → **107**). Ningún invariante se mueve | — |
| p | ~~**Un adaptador USB-serie FT232RL con su jumper de tensión**~~ **HECHO**, pedido por el uso. (1) **`Jumper`**: un conector con un `puente=` obligatorio entre dos pines vecinos —o `no`—, que los une en un nodo antes de construir; errores si falta, si no son dos pines, si no existen, si son el mismo o si no son vecinos. Los nombres de pin admiten ya `.` y `+` y empezar por cifra, con al menos una letra (`5V`, `3.3V`). (2) **`PuenteSerie` con `vccio`**: el nivel de sus salidas y el umbral de sus entradas siguen a ese nodo, y `rx` y `cts` llevan el pull-up de 200 kΩ del FT232R. (3) **Variantes en el dibujo**: un elemento `PIEZA@valor` se manda solo si es la variante de la pieza —el puente de un jumper—, y `mcu-sim` quita las demás del SVG (`common/svg_variantes.h`). (4) **`placas/ftdi_ft232rl.xml`** y su dibujo. `make gui-sistema` C13 (107 → **119**). Ningún invariante se mueve | — |
| q | ~~**El VCP del ST-LINK en la Nucleo, y el FT232RL en el sistema de la barra**~~ **HECHO**, pedido por el uso. (1) **`nucleo_f446re.xml` lleva el VCP**: un `PuenteSerie` en la USART2 por RFC 2217 en el 3355, y D0 y D1 al aire (SB62 y SB63 abiertos, de fábrica). (2) **Con los ms dichos, una simulación con puentes por red acaba a su hora**; sin ellos, espera como siempre. (3) **Dos puentes con el mismo puerto en sus ficheros** —dos Nucleo— no chocan: el segundo se corre al siguiente libre y se dice; con `--serie`, sigue siendo un error. `--serie` admite `N/VCP`. (4) **Rótulos en el dibujo**: un texto `PIEZA#campo` se rellena al mandarlo; el de la Nucleo dice junto al ST-LINK a dónde va el VCP. (5) **`nucleo_f446re_barra8ac_azul.xml`** con el FT232RL en la USART1, y `barra8_morpho` hablando por los dos puertos. `make gui-sistema` C14 (119 → **128**), `make serie` (138 → **140**). Ningún invariante se mueve | — |
| r | ~~**Un módulo KY-040: encoder rotativo con pulsador**~~ **HECHO**, pedido por el uso. (1) **`Encoder`**: `pasos` clics por vuelta y dos contactos, A-C y B-C, abiertos en las posiciones pares y cerrados en las impares; cada clic los cambia los dos, primero A a la derecha y primero B a la izquierda, con `desfase_ms` entre uno y otro: con pull-ups, código Gray. Mando `girar` (discreto, la cuenta de clics); observables `posicion`, `cuenta`, `contacto_a` y `contacto_b`. Era una de las piezas de `doc/analisis_gui.md` §8. (2) **`Resistencia`** entre dos nodos, para un pull-up que cuelga de la VCC de un conector; un extremo al aire se queda a la tensión del otro. Las dos sobre `RamaDosNodos`, la rama exacta del `Led` de dos patillas. (3) **El efecto `giro`** en la tabla de enlaces. (4) **`placas/ky040.xml`** y su dibujo, con el mando en el centro: el anillo gira con el encoder y la tapa es el pulsador. `make gui-ordenes` O6 (29 → **40**). Ningún invariante se mueve | — |
| s | ~~**Una pantalla TFT de 1,8" y 128x160 con su ST7735S**~~ **HECHO**, pedido por el uso. (1) **`Tft128x160`** (`parts/tft_st7735.h`): el ST7735S por SPI de cuatro hilos, con todas las órdenes del sistema —también las lecturas, que contestan por SDA— y las del panel aceptadas; la memoria de 128x160 (o 132x162), MADCTL, 12/16/18 bits con su tabla de color, desplazamiento, modo parcial, ocho colores, inversión, sueño y reset; el regulador y la retroiluminación como cargas de verdad. Lo que se ve es lo que enseñaría la de verdad: blanca dormida, ruido al encenderla, negra sin luz. Avisa de las órdenes en los 5 ms tras el reset (que se pierden), de las esperas de 120 ms, de un reloj demasiado rápido y de 5 V en sus entradas. (2) **Las IMÁGENES**: una pieza puede enseñar una imagen entera (`Imagen` en `parts/part_base.h`); el catálogo la declara como `<imagen>` con un `id_obs` detrás de los observables, se pide en la suscripción y sale en un mensaje nuevo, **`T_IMAGEN`**, solo cuando cambia, con la luz media aparte (`protocolo.h`, sin subir la versión). (3) **`placas/tft_128x160.xml`** y su dibujo, y **`placas/nucleo_f446re_tft.xml`** con `verif/fw/tft_demo`; `verif/fw/tft_prueba` para la prueba. (4) De paso: `sim` sujeta **VSS a 0 V** como sujeta VDD a 3,3 —una pieza que se alimenta entre dos pines de la Nucleo no tenía masa—; la validación eléctrica no avisa de una entrada que sale por un conector de una placa suelta, ni de dos patillas de una misma pieza en el mismo hilo. `make gui-marcha` M6 y M7 (24 → **52**). Ningún invariante se mueve | — |
| t | ~~**El dibujo de una placa, girado**~~ **HECHO**, pedido por el uso —la pantalla TFT de pie, o con el conector a la izquierda—. En vez de un SVG por postura, **`giro="90"`** (o 180, o 270, en el sentido de las agujas del reloj) en la `<placa>` o en su `<placa id>` de un sistema, que manda: `mcu-sim` cambia el viewBox y el tamaño de la raíz y mete el dibujo en un `<g transform>` antes de mandarlo (`gira_svg`, `common/svg_variantes.h`). Los ids no cambian, y lo que va encima de una pieza gira con ella. Un giro que no vale es un error; un SVG sin viewBox se avisa y va como está. En `mcu-sim-gui`, un cuarto o media vuelta ya no avisa de «girado» (su plan §31). `make gui-sistema` C15 (128 → **135**). Ningún invariante se mueve | — |
| u | ~~**Un servo SG90, en una placa y en un sistema con encoder y pantalla**~~ **HECHO**, pedido por el uso. (1) **`Servo`** (`parts/servo.h`): la anchura del pulso es el ángulo —`pulso_min_ms`/`pulso_max_ms` a `angulo_min`/`angulo_max`, en línea recta, o `continuo="si"`, una velocidad—; va hacia él a la `velocidad` de su tensión (`s@V`, en línea recta entre puntos, o proporcional con uno), con banda muerta; un pulso fuera de rango lleva al tope y uno que no lo es se ignora, avisando; sin señal, `suelta` o `mantiene`; es una carga de verdad en VCC (`i_reposo_ma`, `i_marcha_ma`, `i_bloqueo_ma`), y por debajo de `v_min` no se mueve; empieza en un ángulo al azar (`posicion_inicial`, `semilla`, el mismo en todas partes). Observables `angulo` —el que sugiere—, `pulso`, `corriente` y `rpm`; mando `bloquear`. El eje es una recta entre eventos, sin pasos. (2) **El efecto `angulo`** y **varias variantes por pieza** (`SERVO.cuerpo@azul`, `SERVO.aspa@cuatro`). (3) **El +5V de la Nucleo**: una `Fuente` `USB` de 5 V y 500 mA en CN6.5, CN7.18 y CN10.8, el conector USB de su dibujo. (4) **`placas/servo_sg90.xml`** y su dibujo, y **`placas/nucleo_f446re_servo.xml`** con el KY-040 y la TFT de pie, y `verif/fw/servo_demo` y `servo_prueba`. `make gui-marcha` M8 (52 → **68**), `make gui-ordenes` O7 (40 → **52**); `make gui-sistema` cuenta un componente y dos nodos menos en la Nucleo. Ningún invariante se mueve | — |
| v | ~~**La disposición de la ventana, en el XML**~~ **HECHO**, pedido por el uso —colocar las placas de un sistema en la ilustración una vez y no cada vez; fase 4 de `doc/analisis_disposicion_ilustracion.md` de `mcu-sim-gui`—. **`x=` e `y=`** (mm, juntos) y **`escala=`** (0.25 a 4) en cada `<placa id>` de un sistema, también en una placa escrita dentro; **`lienzo="x y ancho alto"`** en el `<sistema>`; `escala` y `lienzo` en la raíz de una placa suelta; y **`<ruta linea="hilo A/J.2 B/J.1" eje="h\|v" codos="..."/>`**, una línea en tramos rectos, comprobada contra las líneas que hay (`Netlist::claves_lineas`). Para la simulación no son nada: solo se leen, se comprueban, se dicen en una línea de consola (`ventana: ...`) y van en `T_PLACA`, **añadidos a la versión 2 sin subirla**. Lo que no vale es un error que dice por qué. `placas/nucleo_f446re_servo.xml` coloca sus cuatro placas, escrito por el «Guardar en el XML» de la ventana (su plan §38). `make gui-sistema` C16 (135 → **140**). Ningún invariante se mueve | — |
| w | ~~**Piezas que no interesa ver en la ventana: `visible="no"`**~~ **HECHO**, pedido por el uso —la bandeja «sin dibujar» del FT232RL, con `REG_3V3` y `MASA`—. En un `<componente>`, solo `si` o `no` (otra cosa es un error, como `conectada`); va en `T_PLACA` con la pieza, también aplanada en un sistema, y la simulación no lo mira. `mcu-sim-gui` no pone esa pieza en la bandeja ni en el dibujo generado y la cuenta como «oculta» (su plan §41); sin el atributo, todo como siempre. `make gui-sistema` C17 (140 → **144**). Ningún invariante se mueve | — |
| x | ~~**El firmware de cada MCU desde la línea de órdenes, o ninguno**~~ **HECHO**, pedido por el uso —lanzar desde la ventana un sistema sin el binario que dice su XML, o con otro por chip—. **`--firmware ID=FICHERO`** (repetible, con o sin igual) da a ese chip el suyo y manda sobre su `firmware=`, también con varios MCUs; **`--sin-firmware=ID`** lo deja sin ninguno —el núcleo se aparca en `wfe`— y **`--sin-firmware`** a secas, a todos, con un `--firmware` por id que manda sobre eso. Errores: un id que no está (con los que hay), el mismo chip dos veces, sin fichero, junto al firmware posicional, y en una placa sin MCU. **`mcu-sim placa.xml --mcus`** lista los chips en XML —id, tipo y firmware del XML, con `--mcu` encima—: con eso el diálogo de lanzamiento de `mcu-sim-gui` pone una fila de firmware por chip con su casilla «sin firmware» (su plan §42). Las tres en `--argumentos` y en `--help`. `make gui-argumentos` A4 (11 → **17**). Ningún invariante se mueve | — |
| y | ~~**«Suspend operation timeout» en STM32CubeIDE, y un paso a paso muy lento**~~ **HECHO**, pedido por el uso —un proyecto de prácticas cuyo `main` es un bucle sobre `HAL_GetTick()`—. (1) **El stub no borraba `DFSR`** —sus bits se quedan hasta que se escribe un 1—, así que el BKPT del punto de ruptura de `main` que pone el IDE se quedaba para toda la sesión y cada parada salía como `swbreak`; y **al Ctrl-C contestaba `T05`**, no `T02`. GDB tomaba la parada por un punto de ruptura ya quitado y reanudaba sin avisar al IDE. Ahora `GdbRsp` borra `DFSR` al reanudar y al dar un paso (`borrar_causa`) y contesta `T02` al Ctrl-C —o `T05` si el objetivo se había parado solo en ese momento—. Medido con GDB por MI: la pausa, de nunca a 0,05 s. (2) **En Windows, `timeBeginPeriod(1)`** con `--tiempo-real` o con la ventana: sin él, cada rodaja de 1 ms dormía hasta 15,6 ms y cada paquete de GDB los esperaba (`-lwinmm`). (3) Un *step over* en `cfEvaluate();` dentro de `while (1)` no acaba nunca —la llamada y el salto de vuelta son de la misma línea, y GDB sigue hasta salir de ella—: no es el simulador, y pasa igual con una placa. `cubeide/README.md`, «El quinto». `make test407` T96 (2146 → **2149**); el `resto` no se mueve | — |
| z | ~~**Conectores acodados y derechos, para los hilos de la ventana**~~ **HECHO**, pedido por el uso. **`acodado="si"`** en un `Conector` (o un `Jumper`): los pines salen de lado, por el borde de la placa; por omisión, `no`, hacia arriba. Se comprueba (`si` o `no`), va en el `<conector>` de `T_PLACA` y la simulación no lo mira. `mcu-sim-gui` lleva un hilo al borde de un pin acodado y al centro de uno derecho, con un círculo (su plan §44). Acodados: el `P1` del KY-040, la pantalla TFT, el servo y el FT232RL. `make gui-sistema` C18 (144 → **147**). Ningún invariante se mueve | — |
| aa | ~~**Un LED RGB: tres colores en un encapsulado, que se mezclan**~~ **HECHO**, pedido por el uso. (1) **`RGBLed`** (`parts/ext_parts.h`): tres `Led` hijos —rojo, verde, azul—, cada uno con su `vf_*` y su `r_*`, con la patilla común por dentro (`comun="catodo"` a masa, `"anodo"` a `vdd`) o a la vista (`<pin nombre="comun">`); `ExtPartBase::adopta` le pasa los terminales de los hijos y `retira` los saca del inventario, que la pieza es una. (2) **`color="#rrggbb"` en el catálogo** (`Observable::color`): un 0/1 y una corriente del mismo color son un canal; la ventana (mcu-sim-gui) suma los encendidos con su brillo y los lleva al tope —los tres, blanco—, pinta de su color el ● del panel y da al piloto generado un encapsulado claro. Sin cambiar la versión del protocolo: una ventana anterior pinta `encendido`, el primero. (3) **Las placas**: revisadas todas; solo dos llevan un LED RGB de verdad, la LED2 de la MSP-EXP432P401R y el D1 de la BOOSTXL-EDUMKII (este, con los transistores de su montaje como carga de cada canal), y su dibujo es ahora un único elemento con la cúpula clara. Los LED de colores sueltos (Discovery, Nucleo, MSP-EXP432E401Y) siguen siendo `Led`. (4) El informe del final dice «LED RGB LED2: cian». Ningún invariante se mueve | — |
| ab | ~~**La sonda XDS110 de las LaunchPad MSP432, y una sonda CMSIS-DAP por TCP**~~ **HECHO**, X1 y X2 de `doc/analisis_xds110.md`. (1) **`Xds110`** (`parts/xds110.h`), el lado de la sonda de J101 en las dos LaunchPad: su LDO alimenta el chip por el puente 3V3 (`mcu_if::alimentacion_externa`: el adaptador deja DVCC/VDD a la placa; sin el puente, el chip no arranca), `--openocd` y `--cmsis-dap` van por sus pines TMS/TCK/RST, y la traza SWO -PJ.5 en el P401R, que gana TDO/SWO como función primaria; PC3 en el E401Y- sale con `swo=`, `--swo` y `--swo-bitrate` a memoria, TCP o RFC 2217, como texto del ITM o en crudo; `--espera-terminal` también espera a su cliente. La corriente del LDO se mide sin el umbral de 0,1 mV del nodo (`AnalogNet::corriente_exacta`). (2) **`SondaCmsisDap`** (`verif/sonda_cmsis_dap.h`, `--cmsis-dap` o `depuracion="cmsis-dap"`, puerto 4441), para el `cmsis-dap tcp` de OpenOCD en cualquier chip: programa el P401R en 1,0 s (4-6 s con `remote_bitbang`) y el F407 en 1,6 s (16 s); los WAIT se reintentan con espera, o el borrado de un sector del F407 no cabe en los 64 reintentos de OpenOCD. (3) La validación eléctrica reconoce un pad por su nodo, no solo por su nombre. (4) `make xds110` (15) y `make cmsis-dap` (25), en el CI de Linux; firmwares `p4_swo` y `e4_swo`; `.cfg` de CMSIS-DAP y configuraciones de CCS 20 en `ide/msp432/`. Ningún invariante se mueve | — |

**El riesgo que hay que vigilar, y tiene su mitigación escrita:** los dos
`SC_THREAD` de la frontera se construyen siempre —la elaboración de SystemC es
estática— y un proceso que despierta **mueve el invariante**. Sin `--gui` tienen
que esperar sobre un evento que nadie notifica nunca. Que eso funciona está
probado: la fase 4 del plan del F415/F417 metió el CRYP y el HASH enteros en la
elaboración del F407 sin que `2336217899213 ps` se moviera un picosegundo.

### P-13 — Integración continua: las tres suites en cuatro plataformas

**Fase:** posterior a F7. **En marcha desde 2026-09-22**, en
`.github/workflows/suites.yml`, documentada en `doc/integracion_continua.md`.

Cuatro trabajos en cada empujón a `main`: uno rápido sin SystemC, Linux con
`libsystemc-dev` —que verifica **la vía que documenta `compilacion.md` §4**—,
macOS en **las dos arquitecturas** y Windows con MSYS2.

**Lo que comprueba no es que las suites pasen.** Eso lo comprueba cualquiera.
Comprueba que el **tiempo simulado no se mueva**, al picosegundo, contra
`src/verif/invariantes.txt`, que es donde viven esas tres cifras una sola vez.
Un cambio puede dejar las 2 118 comprobaciones en verde y haber cambiado el
comportamiento del modelo; lo que lo delata es el reloj. Por eso es un fallo y
no un aviso.

**Esto no habría sido posible antes de T-22.** Mientras los `.bin` no se
versionaban, cada máquina ejecutaba un programa distinto y un CI
multiplataforma habría dado rojo permanentemente por la razón equivocada.

**EN PIE Y EN VERDE desde el 2026-09-23.** Séptima ejecución, la primera sobre
el repositorio republicado: los cinco trabajos pasan. `10 s` el rápido,
`1:53` macOS arm64, `2:40` Linux, `2:53` macOS Intel y `6:12` Windows —el único
que compila SystemC, porque es el único sin paquete—.

**Y desde el 2026-09-26 hace una segunda cosa: CONSTRUYE LO QUE SE REPARTE.**
Cada plataforma empaqueta su ejecutable y una etiqueta `v*` lo publica como
*Release*, con `needs` de todos los trabajos: **un binario no se publica si su
plataforma no pasó las suites y los invariantes**. Eso convierte la promesa de
`doc/ejecutables.md` en algo comprobado en vez de dicho.

Tres decisiones dentro de eso, y las tres tienen su razón escrita en el
`workflow`:

| | Decisión | Por qué |
| :--- | :--- | :--- |
| El ejecutable de Linux se construye en **Ubuntu 22.04** | Un binario de Linux corre hacia adelante y no hacia atrás: el de 24.04 pide `GLIBC_2.38` y en 22.04 no arranca. **La glibc del que construye es el suelo de lo que se puede repartir**, así que se elige la distribución más vieja que se quiere soportar, no la más nueva que hay. **Y de paso aparece una TERCERA versión de SystemC**: 22.04 empaqueta la 2.3.3. Medido antes de escribirlo —2.3.3 construida aparte, modelo compilado con `g++-11` sin un solo aviso— las tres suites dan **las mismas cifras al picosegundo** que con 2.3.4 y 3.0.2 |
| En Linux y Windows, SystemC va **estática**; en macOS, **no** | 22.04 empaqueta `libsystemc-2.3.3.so` y 24.04 `libsystemc-2.3.4.so`: **SONAME distintos**, así que un binario de Linux enlazado dinámicamente no funcionaría ni entre dos Ubuntu. En macOS el alumno la instala con un `brew install`, y así SystemC **no se redistribuye** y la Apache-2.0 §4 no entra en juego —donde sí entra, el paquete lleva su `LICENSE` y su `NOTICE`; véase `TERCEROS.md`— |
| Se empaqueta con `tar`, no con el zip de `upload-artifact` | El zip de esa acción **pierde el bit de ejecución**, y el alumno se encuentra un `Permission denied` que no tiene nada que ver con su máquina |

**Lo que queda:**

| | Qué | Estado |
| :--- | :--- | :--- |
| a | Que macOS pase por primera vez | **HECHO**; es I-23, cerrado el 2026-09-23 con las dos arquitecturas |
| b | Subir `actions/checkout`, `cache` y `upload-artifact` a la v5 | **PENDIENTE.** Node 20 está obsoleto y las tres se ejecutan ya forzadas sobre Node 24: funciona, pero es prestado |
| c | Decidir si `ENABLE_PTHREADS` en arm64 hace falta de verdad | **RESUELTO, y la respuesta fue que no hacía falta nada de eso**: macOS ya no compila SystemC, usa la de Homebrew. La precaución costó una ejecución de 44 minutos sin terminar en Intel |
| d | **19 de octubre de 2026: `ubuntu-latest` pasa a Ubuntu 26** | **CON FECHA.** El trabajo de Linux se apoya en `libsystemc-dev` del sistema, así que ese día puede cambiarle la versión de SystemC debajo sin que nadie toque nada. Desde I-24 sabemos que la 2.3.4 y la 3.0.2 dan las mismas cifras, así que no es alarmante; pero si se pone rojo, lo primero es mirar qué versión trae el paquete, y **no se toca `verif/invariantes.txt`** hasta saberlo. **Mitigado en parte el 2026-09-26**: el trabajo de Linux corre ahora en `ubuntu-22.04` **y** en `ubuntu-latest`, así que la migración ya no puede llevarse la única cobertura de Linux por delante |
| e | **17 de abril de 2027: se retira la imagen `ubuntu-22.04`** | **CON FECHA, y ésta duele más.** Es la máquina que fija el **suelo de glibc del ejecutable de Linux** (2.35). Empezó su retirada el 17 de septiembre de 2026 y queda sin soporte el 17 de abril de 2027 [actions/runner-images#14254]. Ese día el suelo sube a 24.04 —glibc 2.39— quiera o no, y hay que **decirlo en `doc/ejecutables.md` antes de que un alumno con 22.04 se encuentre un `GLIBC_2.39 not found`**. La alternativa, si hiciera falta bajar más el suelo, es construir en un contenedor viejo en vez de en la imagen del runner |
| f | Firmar los ejecutables | **PENDIENTE, y decidido por ahora que no.** macOS exige cuenta de Apple Developer de pago para notarizar, y Windows un certificado de firma de código. Mientras no se firmen, el alumno ve el aviso de SmartScreen y tiene que quitar la cuarentena a mano en macOS: **`doc/ejecutables.md` §4.3** lo explica paso a paso, que es el sustituto honesto de firmar |
| g | **Un trabajo muerto por `timeout-minutes` se lleva sus logs** | ~~Sin red~~ **ARREGLADO el 2026-09-28 con `ci/pasa_suites.sh`.** Lo destapó la primera etiqueta: macOS Intel agotó los 30 minutos, GitHub lo mató, y con él se fueron los pasos que faltaban —incluido el `upload-artifact` que tiene `if: always()` para esto— de modo que `gh run view --log` contestó `log not found`. **El CI no podía investigar su fallo más caro.** Ahora cada suite tiene presupuesto propio, más corto que el del trabajo: si se agota falla la etapa, no el trabajo, y los tres `.log` se suben. Se para en la primera que lo agote, para no comerse el límite con las otras dos. En macOS exige `brew install coreutils`, porque BSD no trae `timeout` ni `stdbuf` |
| h | **macOS Intel tardó >30 min en un commit que tardaba 3** | **ABIERTO, y sin causa.** Mismo commit `a2dd59b`, mismo workflow, mismo tipo de runner: la ejecución de `main` pasó en 3 min y la de la etiqueta agotó los 30. Relanzado solo ese trabajo, sin tocar nada, **pasó**. Dos hipótesis, ninguna demostrada: la **contención** —los dos trabajos gemelos corrían a la vez sobre el mismo commit, y los runners de Intel son el recurso más escaso que usamos— o una **diferencia en lo que sirvió Homebrew**. El cambio del grupo de concurrencia a `github.sha` elimina la primera del mapa; si vuelve a pasar sin trabajo gemelo, es la segunda. Y esta vez habrá log (**punto g**) |
| i | **Dos carpetas homónimas con el mismo `origin`** | **ARREGLADO a mano, sin red que lo impida.** La etiqueta `v0.1.0` se empujó desde la copia VIEJA —la de la historia sin filtrar— y **devolvió los tres informes a GitHub** dentro de los 120 commits que arrastraba. Se borró la etiqueta y se volvió a borrar y recrear el repositorio. La causa de fondo no fue el despiste sino tener `mcu-sim` y `mcu-sim-publico` con `origin` las dos; el archivo se ha quedado sin remoto y renombrado. **No hay nada en el repositorio que impida repetirlo**, y merece pensarse si un `pre-push` lo valdría |

**Lo que ha enseñado, en siete ejecuciones:** seis fallos que tres máquinas de
desarrollo no habían visto en meses. Cuatro en el entorno —el segundo motor de
`comprueba_vectores.py`, **SystemC 2.3.4 contra CMake 4**, el `#if` que no se
evalúa del ensamblador de QuickThreads, pthreads cien veces más lento—; **uno
en el modelo**, la guarda de redondeo, que llevaba ahí desde siempre sin que
ninguna de las 2 118 comprobaciones lo viera (I-24, y de ahí salió T-23); y
**uno en el propio criterio**, el tiempo que el stub de GDB gasta esperando un
socket (T-16).

**Y una propiedad que no estaba en el plan:** los tres invariantes salen
idénticos al picosegundo en **cuatro plataformas, con dos versiones de SystemC,
dos compiladores y tres sistemas operativos**. Dejaron de ser una propiedad de
una máquina. La ejecución del 2026-09-23 es además la primera que lo comprueba
con **el invariante movido por T-23**, que hasta entonces solo se había medido
en Linux.

### P-14 — El puente UART: una USART del MCU hasta un puerto serie del ordenador

**Fase:** posterior a F7. **Analizado y planificado en
`doc/analisis_puente_serie.md`** (§10, arquitectura D con RFC 2217).
**CERRADO el 30-09-2026**: las nueve fases (D0 a D8) ejecutadas en la rama
`puente-uart`, con el CI en verde en las cuatro plataformas, e integrado en
`main`. **Se acepta con la matriz manual del §10.6 a medias** (véase abajo lo
que queda abierto, y por qué no bloquea).

Lo que hace un ST-LINK/V2-1 en una Nucleo: una pieza de placa, `PuenteSerie`,
colgada de los pines de una USART, que lleva sus bytes a un puerto TCP de esta
máquina. El puerto serie del sistema **no lo abre `mcu-sim`**: lo pone una
herramienta externa y gratuita (socat, gensio, com0com + hub4com, HW VSP3...),
y así `common/red.h` sigue siendo el único fichero que sabe en qué sistema
operativo corre. Con RFC 2217, los baudios que el alumno elige en su terminal
llegan a la línea simulada, y un desajuste da basura en la USART del modelo,
como en la placa.

| | Qué | Fase del plan |
| :--- | :--- | :--- |
| a | ~~`--serie ID=DESTINO` y el parseo de `host=`~~ **HECHO**: `common/serie_destino.h`, `make serie` con 72 comprobaciones puras, fuera del banco. Puerto por omisión **3355** (no el 5000 del AirPlay de macOS). **Sin el argumento, nada cambia**: 2118/204/165 y los tres invariantes intactos | D0 |
| b | ~~`MotorUart` extraído de `SwoReceiver`~~ **HECHO**: `parts/motor_uart.h` (`ReceptorUart` y `EmisorUart`, de 5 a 9 bits, paridad N/E/O/M/S, 1, 1,5 y 2 de parada, break) y `common/formato_uart.h`. **`test407` idéntico al picosegundo**: 2118 y `resto` 2240553274213 ps, con la misma salida línea a línea. Estrena `make testserie` (43, `51783680816 ps`) | D1 |
| c | ~~La pieza `PuenteSerie`~~ **HECHO**: `parts/puente_serie.h` en los pines, `parts/canal_host.h` con el canal `memoria`, su ficha de `--help`, sus atributos validados antes de montar, el firmware `vcp_demo` y `placas/vcp_memoria.xml`. `testserie` pasa a 79 y `149861131044 ps`; las otras tres suites, intactas | D2 |
| d | ~~TCP en crudo sobre `red.h`~~ **HECHO**: `CanalTcp` en `localhost`, un cliente (el nuevo sustituye al viejo), reconexión, «no termina sola», aviso sin `--tiempo-real` y `placas/vcp_tcp.xml`. `testserie` pasa a 101 y `236861131044 ps`, **determinista aunque use sockets de verdad**. En reposo, con `--tiempo-real`, un 9 % de un núcleo (el stub de GDB, un 11 % en la misma máquina) | D3 |
| e | ~~El códec Telnet + RFC 2217~~ **HECHO**: `common/telnet2217.h` (decodificador, codificador y la política del servidor), `make rfc2217` con 64 comprobaciones, contra las RFC y contra **dos sesiones de verdad grabadas**: pySerial contra pySerial, y pySerial contra este códec, que se reproduce byte a byte. Compila para Windows con MinGW sin avisos | D4 |
| f | ~~RFC 2217 en la pieza~~ **HECHO**: `CanalRfc2217` sobre `CanalTcp` y el códec de la D4; la pieza es la `LineaSerie` que decide lo que pide el terminal. `baudios="host"` entrega al terminal baudios, formato y flujo (**D-14**); break sostenido → `LBD`; DTR y RTS en los pines; LINESTATE y MODEMSTATE con máscaras; PURGE; SUSPEND/RESUME; `placas/vcp_rfc2217.xml`. `testserie` pasa a 189 y `400677589564 ps`, las otras tres intactas. Y `miniterm` de pySerial contra `mcu-sim`, a mano: eco, 9600, vuelta, CTS/DSR/CD, DTR y break | D5 |
| g | ~~Interoperabilidad automática~~ **HECHA** (en verde en Linux; el CI dirá macOS y Windows): `verif/serie/interop.py` y `make interop`, el `mcu-sim` de verdad contra pySerial (`rfc2217://`, `socket://`) y un pty de socat, 30 comprobaciones, en el CI de las cuatro plataformas (socat solo en Linux y macOS). **Pendiente a mano: la matriz manual de Windows y escritorio** (§10.6 del análisis; el guion, prueba a prueba, M1 a M8, detrás del registro de la D6). Y un aviso para la D7: con la línea fija y `flujo="rtscts"`, pySerial no abre sin `rtscts=True` | D6 |
| h | ~~Recetas y placas~~ **HECHO**: `doc/puente_serie.md` (la receta del alumno, por sistema y por síntoma), §6 de `doc/ejecutables.md`, `placas/nucleo_f446re_vcp.xml`, `--espera-terminal` para que el saludo del firmware no se pierda (D-15) y `ejemplos/` en los paquetes (D-16). Comprobado en Linux y con pySerial y socat en el CI (`make interop`, 36); las recetas de Windows con `COM`, pendientes de la matriz manual | D7 |
| i | ~~Modo cliente~~ **HECHO**: `tcp-cliente:HOST:PUERTO` y `rfc2217-cliente:HOST:PUERTO`, con conexión sin bloquear y reintento cada segundo; el cliente RFC 2217 configura el puerto remoto con la línea del puente (D-17). `make interop` 47 (I12, I13 contra el `PortManager` de pySerial sobre un pty de socat), `make serie` 138, `make rfc2217` 74. `rfc2217_server.py` tal cual **no** sirve sobre un pty: su `PortManager` lee CTS/DSR y se cae | D8 |

**Lo que la D2 deja sin probar, dicho:** ~~`set_dtr()` y el terminal `dtr` no
los ejercita ningún banco~~ (la D5 los prueba, P26); de una trama de 9 bits se entregan al anfitrión los 8 de abajo, porque RFC 2217 no
admite más; y el canal en memoria no tiene límite hacia el anfitrión, solo hacia
el MCU.

**Lo que queda abierto al cerrar, y se acepta así.** Lo que el alumno usará
casi siempre está comprobado: pySerial (`rfc2217://`, `socket://`), socat y el
modo cliente, en el CI de las cuatro plataformas; la receta de Linux a mano; y
**CoolTerm en Windows 10 Pro 22H2** (M1 entera, 30-09-2026). Lo que falta de la
matriz necesita Windows con drivers o un escritorio, y se hará cuando haya
máquina y ocasión, anotando cada fila con versión y fecha en el §10.6; el guion,
prueba a prueba, está en el registro de la D6:

* **M1** CoolTerm en macOS y Linux · **M2** PuTTY *Raw* · **M3** socat +
  picocom en macOS · **M4** ttynvt · **M5** HW VSP3 · **M6** com0com +
  `com2tcp-rfc2217` (y, con ella, si el cliente de hub4com se entiende con un
  servidor pasivo, D-13) · **M7** HHD, solo si su licencia lo permite · **M8**
  la captura de un redirector, opcional.
* Y lo no hecho que el registro de cada fase ya dice: LINESTATE sin el bit de
  desbordamiento; ningún banco provoca un break desde el MCU; la firma sin
  versión; `escucha="red"` (D-7); y el cliente sin DTR/RTS (D-17), sin
  reenviar las notificaciones del servidor y con la resolución de nombres
  bloqueante.

Las recetas de Windows con `COM` (`doc/puente_serie.md` §4.2) siguen diciendo
«sin comprobar» hasta que M5 o M6 digan otra cosa. **D-16** (`ejemplos/` en los
paquetes) queda **confirmada**.

**Lo que no resuelve, dicho desde el principio:** en Windows, un `COMn` que no
es físico exige un driver, y ninguna herramienta gratuita lo evita (§7.1 del
análisis). Para ver lo que imprime el firmware no hace falta: basta un terminal
que hable TCP, como CoolTerm o PuTTY en modo *Raw*.


### P-15 — Varias placas enchufadas: `Conector` y `<sistema>`

**Analizado y decidido en `doc/analisis_placas_conectadas.md`. HECHO el
2026-10-05**, la primera parte:

* **La pieza `Conector`**: filas × columnas pines, `numeracion` zigzag o por
  filas; no conduce, y sus pines al aire no se avisan como flotantes. Un pin
  soldado a un nodo ES ese nodo (`Netlist::resuelve_alias`, unión-búsqueda
  antes de construir, como `une`).
* **El fichero `<sistema>`**: placas de su fichero o escritas dentro,
  `<acopla>` (con `espejo`), `<hilo>` y `<mcu ref>`. Todo cualificado con `/`
  (`A/LD2`, `A/u0.PA5`); leer aplana, y del lector en adelante nadie sabe que
  hay placas.
* **`T_PLACA` en la versión 2 del protocolo**: un `<sistema>` aplanado; a una
  ventana de la 1, lo mismo con la raíz `<placa>`. `mcu-sim-gui` agrupa por
  placa (su plan §19).
* La NUCLEO-F446RE lleva sus conectores Arduino, y `placas/nucleo_y_shield.xml`
  la enchufa a un shield sin MCU. **`make gui-sistema`** lo vigila.
* **Pilas** (§13 del análisis): `<acopla conectores="A/J1 B/J1 C/J1"/>`, un
  acople de varios conectores, como en PC/104; `placas/pila_pc104.xml`.
* **Cada placa, descrita para poder dibujarla** (§14): en `T_PLACA`, sus
  piezas, chips y conectores con su forma, y en cada acople e hilo, qué placas
  une. Añadido a la versión 2, sin subirla. `make gui-sistema` llega a 61.

* **VDD, masa, NRST y BOOT0** (§15): los diez pads de alimentación y arranque
  de un chip se unen como los de puerto, entre chips y entre placas.
  `PowerPads` recibe el `Cableado` (`une_alim`). La Nucleo suelda a su chip el
  RESET, el +3V3, el IOREF y las masas de sus conectores Arduino.

**Lo que queda**, cada cosa con su motivo en el §12 del análisis: alimentación
de verdad entre placas; un sistema dentro de otro; cambiar parámetros de piezas
desde el montaje; y dibujar el sistema en la ventana, para lo que harán falta
además el tamaño de cada placa y dónde va cada conector.
### P-16 — Los MSP432 de Texas Instruments: P4 y E4

**Analizado en `doc/analisis_msp432.md`, con un plan de nueve fases (T0-T8).
HECHAS T0 y T1 el 2026-10-14**, en la rama `msp432`:

* **T0, la generalización neutral.** Ningún invariante se movió ni un
  picosegundo. El núcleo sabe quién lo integró (`IntegracionNucleo`: el alias
  de 0x0 y la ventana del PPB del fabricante); los nombres de pin los
  interpreta el chip (`pins/nomenclatura.h`: `P1.0` es PB0 en un STM32 y el
  primer pin de un MSP432, y T125 no se toca); `mcu_if` ya no exige un
  `McuCaps` (`soc/mcu_desc.h`, catálogo común); el `load` de GDB pide la
  secuencia de Flash al chip (`programador_nv_if`); y el cargador lee ELF
  (el `.out` de CCS) y TI-TXT.
* **T1, el MSP432P401R.** El mismo `CortexM4F` con 41 IRQ y 3 bits de
  prioridad, sin alias y con RSTCTL/SYSCTL en el PPB; Flash principal y de
  información con un TLV sintético; SRAM con su alias de código; CS (DCO,
  REFO, VLO, MODOSC, SYSOSC y los dos cristales de la placa), PCM, FLCTL de
  lectura con estados de espera, WDT_A encendido de fábrica, los cuatro tipos
  de reset, DIO con P1..P10/PJ sobre pads eléctricos e interrupciones en
  P1..P6, PMAP, las patillas SWD dedicadas, y un vigilante que avisa si MCLK
  pasa del tope del VCORE o de los estados de espera. `placas/msp_exp432p401r.xml`.
  El banco es `make testp4`: **96 comprobaciones, `254192855661 ps`**, con
  `asanp4` limpio. Una placa con un P4 y un F407 se monta y corre.

**HECHA T2 el 2026-10-15**, en la misma rama:

* **El FLCTL entero.** Protección por sector (`BANKx_MAIN/INFO_WEPROT`, todo
  protegido al salir del reset y el TLV imposible de desproteger);
  programación inmediata, de palabra completa (la línea de 128 bits) y en
  ráfaga de hasta cuatro líneas, con `BURST_STATUS` que pasa por 2..5 y acaba
  en 7; borrado de sector y masivo; `ADDR_ERR`, `PRG_ERR`, `AVPST` con
  `VER_PST`/`AUTO_PST`; la IRQ al NVIC; y el **read-while-write**: leer el
  banco que se está programando o borrando espera, leer el otro no.
* **`ProgramadorP4`**, el `programador_nv_if` del P4: lo que hace el algoritmo
  de Flash de TI para OpenOCD, por el DAP. El `load` de GDB borra y programa;
  un `load` que pise el TLV falla diciéndolo.
* **`SYSCTL_PERIHALT_CTL`**: con `HALT_WDT` (el valor de reset) el perro se para
  con el núcleo; sin él, resetea aunque el depurador lo tenga parado.
* **Depuración por los dos caminos** (`depuracion="dap"` y `"pines"`): la misma
  sesión de GDB —`load` sobre CoreMark, lectura de vuelta, `monitor reset
  halt`, watchpoint, punto de ruptura y paso— da los mismos resultados.
* **De paso, un arreglo del stub común**: `monitor reset halt` soltaba el
  núcleo tras el *vector catch* (`enganchar()` escribía `DHCSR` sin `C_HALT`)
  y el PC aparecía unas instrucciones más allá; en el F407 paraba en
  `0x080001DE` en vez de en `Reset_Handler`. Ninguna suite usaba
  `monitor reset`: ningún invariante se mueve por esto.
* **Configuraciones de IDE** en `ide/msp432/`: CCS (`.launch` de CDT) y VS Code
  (Cortex-Debug y `cppdbg`). Probadas con `gdb-multiarch` por consola y por
  MI, **no dentro de los IDE**.
* El banco: `make testp4` sube a **148 comprobaciones** (P18 el FLCTL desde el
  bus, P19 un firmware que reprograma su Flash —modo 9 de `p4_demo`—, P20 los
  símbolos del ELF, P21 GDB por G2 y G1), y su invariante pasa a la columna
  `resto`: **`331699188968 ps`**. `asanp4` limpio.

**HECHA T3 el 2026-10-15**, en nueve entregas, cada una con su commit y su
grupo de comprobaciones en `make testp4` (P22-P61: 148 -> 356, resto
`4941243771818 ps`):

1. **Las funciones de periférico en los pines** (SEL1:SEL0, las que gobierna
   el módulo, las de PxDIR y las analógicas; el PMAP con su tabla de reset)
   y **la eUSCI_A en UART** a nivel de bit, con el backchannel de la
   LaunchPad como `PuenteSerie`.
2. **Timer_A** (los tres modos, los ocho de salida, captura, TAxCLK, ID/IDEX)
   y **Timer32**; un servo SG90 en TA0.2.
3. **La eUSCI_B en I2C** (maestro y esclavo, con estiramiento, START
   repetido, TBCNT) **y SPI** (A y B, los cuatro modos), contra una 24Cxx y la
   ST7735.
4. **ADC14 y REF_A**: resoluciones, formato con signo, diferencial,
   secuencias, MSC, disparo por Timer_A, ventana; el sensor de temperatura
   con la calibración del TLV.
5. **La uDMA** (PL230 con el multiplexor de TI): básico, automático y
   ping-pong, con la eUSCI y el ADC14.
6. **CRC32, AES256, RTC_C y COMP_E**.
7. **Los modos de bajo consumo** (LPM0, LPM3, LPM4, LPM3.5, LPM4.5, con su
   salida por POR y LOCKLPM5) y **el consumo por DVCC**.
8. **La BOOSTXL-EDUMKII**: cuatro piezas nuevas (`FuenteAjustable`,
   `Zumbador`, `Opt3001`, `Tmp006`), los conectores BoosterPack de la
   LaunchPad, la placa y el sistema `placas/msp_exp432p401r_edumkii.xml` con
   su firmware; `make p4-edumkii`, en el CI.
9. **Los ejemplos de DriverLib** (uart_pc_echo, timer_a_pwm,
   adc14_single_conversion, dma) con DriverLib de TI compilada de sus
   fuentes (`verif/fw/msp432p4/sdk`).

Por el camino, siete fallos del modelo que solo se ven usándolo: escribir
CTL0 con la eUSCI en marcha soltaba SDA a medio bit; la esclava I2C soltaba
SCL a la vez que cambiaba SDA (el maestro veía un STOP); la esclava no
contaba el STOP tras un NACK; el ADC14, el RTC_C y el COMP_E no volvían a
subir la interrupción con otro flag pendiente; el Timer_A giraba sin avanzar
el tiempo con el tic redondeado a ps (12 MHz); el Timer32 truncaba a 16 bits
un LOAD escrito parado; y la uDMA no entendía el fin de búfer que escribe
DriverLib (el último byte). El último lo encontró DriverLib: es para lo que
está.

**HECHA T4 el 2026-10-15**, en la misma rama (P62-P67: 356 -> 380, resto
`18893170139423 ps`):

* **La ROM sustituta R1** (`verif/fw/msp432p4/rom_r1`): DriverLib entera,
  compilada con gcc, con `ROM_APITABLE` en 0x0200_0800 y sus 28 tablas
  **generadas** de `rom.h` (`gen_tablas.py`) y **verificadas** por otro camino
  (`verifica_tablas.py`: el preprocesador expande cada `ROM_xxx` y la casilla
  tiene que llevar a la función de ese nombre). Va dentro del simulador
  (`soc/msp432p4/rom_r1.inc`), con su huella.
* **`--rom` y `--tlv`** en `sim` y como atributos `rom=`/`tlv=` del `<mcu>`:
  `r1` (por omisión), `ninguna` (R3, lo de T1-T3), o la ROM de TI de un
  fichero (R2: un volcado de 32 KB o el ELF de CCS); y el TLV de una placa
  (4 KB de 0x0020_1000, recorrido por etiquetas como `SysCtl_getTLVInfo`), o
  `sintetico`. El DCO del modelo sigue la calibración del TLV cargado.
* **El cierre**: cuatro ejemplos del SDK **sin modificar** que usan `MAP_`
  (`verif/fw/msp432p4/sdk_ejemplos`: SysTick, `CS_setDCOFrequency`, Timer32
  con S1 y el eco UART a 9600), la demo blinky de FreeRTOS para la LaunchPad
  con *tickless idle* (`verif/fw/msp432p4/freertos_blinky`, sin modificar,
  núcleo V11.3.1), y la comparación con la ROM de TI fuera de la suite, porque
  esa imagen no se versiona: `make p4-rom-ti ROM_TI=...` (las cifras, en
  `doc/analisis_msp432.md`).
* **Un fallo del modelo**, de la eUSCI en UART: si el emisor va exactamente a
  la misma velocidad (9600 con BRF = 2 y BRS = 0 contra el `PuenteSerie`), el
  arranque del carácter siguiente baja en el mismo instante en que el
  receptor acaba la parada, y no lo veía: se enganchaba al primer flanco de
  bajada del dato. Ahora, si la parada fue buena y la línea ya está baja,
  ese es el arranque, contado desde su flanco. Ningún grupo anterior lo
  pisaba: P0-P61 no se mueven.

**HECHA T5 el 2026-10-17**, en la misma rama; el BSL (D4), el 2026-10-18:

* **La sonda de OpenOCD** (`verif/sonda_openocd.h`): el protocolo
  `remote_bitbang` en SWCLK, SWDIO y NRST, como un adaptador que OpenOCD
  maneja bit a bit. En `sim`, `--openocd` (puerto 3335, porque OpenOCD abre
  el 3333 para su GDB) o `depuracion="openocd"` en el `<mcu>`. Sirve igual al
  STM32.
* **El cierre**, en `make openocd` (`verif/openocd/programa.py`, 12
  comprobaciones, en el CI): `program ... verify reset` con
  `target/ti/msp432.cfg` y con `target/stm32f4x.cfg` **sin tocarlos**, y el
  firmware nuevo arranca; GDB por OpenOCD ve las tareas de FreeRTOS (G3); y
  la sonda dice que es SWD si se le habla en JTAG. Hace falta un OpenOCD de
  git (`ci/construye_openocd.sh`): sin él la prueba se omite diciéndolo.
* **Seis fallos del modelo**, todos del núcleo y el DAP compartidos, que solo
  se ven con un depurador que no es nuestro:
  1. la secuencia JTAG→SWD (`0xE79E`) se reconocía en cualquier punto del
     flujo de bits: un dato que la llevara (`0xFF3CF7FF`) reiniciaba el SWD a
     media escritura y la palabra se perdía con ACK OK. Ahora solo vale justo
     detrás de un reset de línea, como dice ADIv5;
  2. el DAP exigía reposo entre dos paquetes; OpenOCD no lo deja;
  3. el AHB-AP solo hacía palabras: faltaban los accesos de byte y media
     palabra (CSW.Size) y BD0-BD3;
  4. TAR dejaba de avanzar en la frontera de 1 KiB; en el AHB-AP del Cortex-M
     da la vuelta en la de 4 KiB, que es lo que supone OpenOCD;
  5. un reset local no sacaba al núcleo de la parada: `program ... reset`
     dejaba el firmware nuevo parado en `Reset_Handler`, porque OpenOCD suelta
     el núcleo sobre el BKPT del último algoritmo y resetea. Ahora el reset
     borra C_HALT y solo VC_CORERESET vuelve a parar (es por lo que los
     depuradores necesitan la captura de vector);
  6. el núcleo comprobaba la parada antes de `sync()` y esperaba después: si
     en ese avance se levantaba -tras un SYSRESETREQ, en el F407-, no se
     enteraba nunca.
* **Dos cosas que faltaban**: la lectura en ráfaga del FLCTL (RDBRST), con la
  que DriverLib verifica cada borrado y cada programación; y la firma
  electrónica del F407 (UID y F_SIZE), que OpenOCD lee para contar sectores.
* Ningún invariante se mueve: los grupos de las suites no pisaban nada de
  esto. P21 de `testp4` cambia una comprobación de sitio (el reset del perro
  con el núcleo parado ahora lo pone en marcha), sin cambiar la cuenta.

**HECHO EL BSL (D4) el 2026-10-18**, con el paquete MSP432BSL de TI
(`MSP432BSL_1_01_00_00`: el código del BSL a medida y su imagen) y BSL
Scripter 3.4 (código y ejecutables) en `doc/refs/`, sin versionar:

* **El código de arranque de TI, de alto nivel** (`soc/msp432p4/bsl_p4.h`):
  con la palabra de 0x4 en blanco, el núcleo sale del reset en la función de
  entrada del BSL (la palabra de 0x0020_2000) con r0 = 0xFC48FFFF, por un
  gancho nuevo del núcleo, `Cpu::arranque_fab` (vacío en los STM32). El TLV
  sintético lleva ya la configuración del BSL (etiqueta 15: UART en la
  eUSCI_A0 por P1.2/P1.3, SPI en la B0, I2C en la B1).
* **Tres BSL, como las ROM**: `--bsl b1` (por omisión), `ninguno` o un
  fichero. **B1** es propio (`verif/fw/msp432p4/bsl_b1`, AGPLv3, escrito
  desde la descripción del protocolo): UART a 9600-115200 8E1 con los
  baudios medidos de la sincronía, contraseña, borrado masivo y de sector,
  escritura en la Flash y la SRAM, CRC, lectura, versión, LOAD_PC,
  REBOOT_RESET y CHANGE_BAUD_RATE; duerme en WFI entre byte y byte. **B2**
  es el de TI (`BSL432_MSP432P401.txt`), que no se versiona: su «TI Text
  File License» solo permite usarlo con dispositivos de TI.
* **Probado contra lo de TI**: el BSL de TI corre en el modelo
  (`TESTP4_SOLO=bsl_ti P4_BSL_TI=...`, 15 comprobaciones: la misma sesión que
  P68); y BSL Scripter de TI, sin modificar, programa el B1 por un pty de
  socat con su guion de ejemplo (`script_P4xx_uart.txt`: sincronía,
  contraseña, borrado, Blink_LED, versión, lectura de 16 KB y reboot), y lo
  leído es lo escrito (`BSL_SCRIPTER=... make bsl-p4`). Su modo E4xx UART
  programa también el E4 por el cargador de la R1 (P-17 l).
* **Dos fallos del modelo que solo destapó el BSL de TI**: (1) en el núcleo,
  un `LDMIA.W SP!, {..., PC}` -o `LDR PC, [SP], #4`- que cargaba un
  EXC_RETURN escribía la base DESPUÉS del retorno de excepción y pisaba el
  desapilado: la pila del hilo quedaba 32 bytes más abajo en cada
  interrupción, y de ahí un HardFault. Ninguna suite lo pisaba (ningún
  invariante se mueve); (2) la eUSCI_B en I2C esclavo salía de UCSWRST con
  TXIFG0, y el BSL de TI, que configura las tres interfaces y se queda con
  la primera que pide algo, elegía el I2C.
* `testp4` suma P68 y P69: **406 comprobaciones, `resto`
  `22456139139423 ps`**; P0-P67 no cambian. Y `make bsl-p4`, en el CI.

**Lo que queda**, con su fase:
T8 (el resto de la familia; T6 y T7, el MSP432E401Y y su conectividad, están
hechas y lo suyo va en **P-17**). CAPTIO y PSS siguen declarados. Y lo que T1-T5 dejan dicho, con la
letra con la que lo cita el código:

| | Qué | Por qué |
| :--- | :--- | :--- |
| **a** | ~~Programar y borrar la Flash por el FLCTL; el `load` de GDB~~ | **Resuelto en T2.** Sin `PRG_CTLSTAT.ENABLE` una escritura sigue ignorándose y avisando una vez, como en el chip |
| **b** | ~~La ROM de DriverLib en 0x0200_0000~~ | **Resuelto en T4**: la R1 por omisión. Lo que queda de ella, en **y** |
| **c** | Estados de espera de la Flash por VCORE | **Discrepancia resuelta**: el `system_msp432p401r.c` del port de mbed dice «BANK0 VCORE1 max is 16 MHz» por estado; SLAA668A (2017, posterior) da 24/48 MHz con 0/1 en VCORE1. Se sigue a SLAA668A |
| **d** | HWREV = 0x43 ('C') en el TLV | [1F] Sin segunda fuente a mano |
| **e** | Valores de reset del FLCTL (POWER_STAT, WEPROT) y los búferes de lectura | [1F] TRM. Los búferes se modelan como una línea de 128 bits por banco y tipo, sin el *prefetch* del silicio |
| **f** | El TLV es SINTÉTICO | Estructura de `TLV_Type` [H] y etiquetas de `sysctl.h` [SDK 3.40]; calibraciones elegidas (FCAL = 400, K = 1/632) y suma de comprobación a 0. `limitaciones()` lo dice. Desde T4, `--tlv` carga el de una placa; el modelo lee las calibraciones del DCO en las posiciones fijas de `TLV_Type`, no buscando la etiqueta, y no comprueba la suma |
| **g** | CS: valores de reset, arranque de los cristales, conmutación | [1F] TRM para los valores de reset. LFXT arranca en 5 ms y HFXT en 0,5 ms (modelo: el de 32 kHz tarda cientos de ms en el silicio). Sin contador de faltas (CTL3), sin pin DCOR (DCORES usa la calibración ER del TLV), sin tiempos de conmutación, sin efecto de CLKEN y sin la IRQ del CS |
| **h** | PCM: reglas de transición, tiempos, bajo consumo | Las reglas salen del ejemplo de SLAA668A y de que TI pide ir por pasos; la lista completa del TRM no está contrastada. 20 µs (tensión) y 10 µs (regulador) de PMR_BUSY, de modelo. LPM3/LPM4/LPMx.5 no se modelan; sin la IRQ del PCM |
| **i** | WDT_A: 0x6904 de reset; qué reinicia un reset blando | [1F] TRM. El modelo no reinicia el WDT_A en un reset blando |
| **j** | Qué fuente es qué bit de HARDRESET/SOFTRESET_STAT | [1F] tabla de SLAS826; DriverLib no las nombra. LOCKUP no está conectado |
| **k** | DIO: RPULL 30 kΩ, 50/12 Ω de salida, alta corriente en P2.0-P2.3; PMAP | [1F] datasheet. Escribir IES no levanta IFG (sí en el MSP430). Los valores por omisión del PMAP no están: llegan con los periféricos (T3). Un pin en función de periférico se queda en alta impedancia y avisa |
| **l** | Reset: pull-up de RSTn (47 kΩ), pulso de 20 µs, código de arranque | [1F] el pull-up. El pulso es el mínimo del RCC de los STM32 y por la misma razón (el cuanto de 1 µs de la CPU; con 1 µs se perdían los resets que pide la propia CPU). El código de arranque de TI no se ejecuta: ni buzón, ni BSL, ni bloqueo de JTAG |
| **m** | SRAM_BANKEN | Se guarda (habilitar el banco n habilita los de debajo) pero no apaga ningún banco |
| **n** | Tiempos del FLCTL | De modelo: 30 µs por palabra o por línea de ráfaga, 5 ms por sector, 40 ms el masivo. El orden de magnitud es el del datasheet; los valores exactos dependen de los pulsos y no están contrastados. Un sector protegido «se borra» en el acto: STATUS 3 sin tocar nada |
| **p** | Las tablas de pines | [1F] datasheet (SLAS826 §6.12) para las funciones fijas y la tabla «Default Mapping» del PMAP; segunda fuente, las definiciones de TI-Drivers (`*MSP432.h`) y Energía. Los conectores BoosterPack de la LaunchPad, de SLAU597 y de los A0-A15 de Energía |
| **q** | eUSCI | El orden en que se aplica el patrón UCBRSx a los bits de un carácter es [1F]; los valores, de la tabla de la guía y de Tock. No están: detección automática de baudios, IrDA, el filtro de glitches, I2C de 10 bits, UCCLTO y UCSWACK (se guardan y avisan) |
| **r** | Timer_A y Timer32 | CCIFG de CCR0 no se borra solo al atender la interrupción (en el MSP430 sí) [1F]. Las entradas CCInB internas e INCLK leen 0. El Timer32 es el SP804 de ARM |
| **s** | ADC14, REF_A y el sensor de temperatura | Convertidor ideal: sin ruido, sin error de ganancia ni de offset, sin carga del muestreo sobre el nodo. PWRMD y REFBURST no cambian nada; RDYIFG no sube; los CHxMAP no llevan a ningún canal. El sensor es lineal, 685 mV a 0 °C y 1,9 mV/°C (de memoria del datasheet, sin segunda fuente), y el TLV sintético lleva SUS calibraciones. REF_A asienta en 40 µs (modelo); GENOT/BGOT avisan |
| **t** | uDMA | Sin scatter-gather (cycle_ctrl 4-7: error y aviso), sin distinguir peticiones simples de ráfaga, sin USEBURST ni prot. Los Timer_A, el AES y el pin externo no piden transferencias |
| **u** | AES256 | Bloque a bloque: los modos de cifrado con DMA (CMEN, CBC/OFB/CFB, BLKCNT) avisan. Los ciclos de 192 y 256 bits son [1F] |
| **v** | RTC_C | RTCRDY siempre a 1; la calibración (OCAL, TCMP, CALF, RTCCLK) se guarda y no hace nada; RTCOFIFG no sube. IV en el orden OF, RDY, TEV, A, RT0PS, RT1PS [1F] |
| **w** | COMP_E | Sin los canales internos 8-15, sin SHORT, todos los modos de consumo responden igual, y la salida no llega a las capturas de los Timer_A. Retardo, el del filtro (450-3600 ns) o ninguno |
| **x** | El consumo | Cifras de modelo con el orden de magnitud del datasheet: 40 µA + 100/115 µA/MHz (LDO) u 80/95 (DC-DC) en activo, el 40 % en LPM0, 0,66 µA en LPM3, 0,5 en LPM4, 0,63 en LPM3.5 y 25 nA en LPM4.5. Ni los periféricos ni las transiciones gastan. La SRAM se conserva entera en LPM3.5 (en el chip, el banco 0) |
| **y** | La ROM R1 | Mismas funciones en las mismas casillas que la de TI; **otras instrucciones** (gcc -Os frente a IAR): unos microsegundos de diferencia en los ejemplos medidos. `ROM_VERSION` vale 1, no el de TI. Se lee sin estados de espera [sintético: SLAS826 no da los de la ROM]. Las 12 casillas que la ROM de TI llena y `rom.h` no declara para el P401R (cinco de I2C, cuatro de RTC_C, `SPI_initMaster`, `SysCtl_getNMISourceStatus`, `Timer_A_setOutputForOutputModeOutBitValue`) son en R1 un `BKPT`: `MAP_` no llega a ellas. **`rom.h` y `rom_map.h` son las de Energia (`msp432r-core` 0faf18b), con la nota antigua «TI Information - Selective Disclosure» en la cabecera**; TI publica el mismo fichero con licencia BSD-3 en el SDK 3.40 (comparado por muestras y, entero, por sus 424 casillas contra la ROM). Conviene sustituirlas por las del SDK 3.40: el Makefile de `rom_r1` vuelve a generar y verificar las tablas |
| **z** | PxIES tras el reset | El TRM lo da por indefinido; el modelo lo deja a 0 (flanco de subida). `timer32_periodic_mode_led_toggle`, que no lo escribe, enciende el LED al **soltar** S1; en una placa puede ser al pulsar |
| **bb** | El BSL del P4 (D4) | **Hecho** (ver arriba). Lo que no está: del código de arranque, el buzón de la Flash (0x0020_0000: FACTORY_RESET, la configuración del BSL), la invocación por un pin, el bloqueo de JTAG y las zonas IP; los parámetros que pasa al BSL (0xFC48FFFF) son los de los ejemplos de TI para entrar desde una aplicación [1F]. Del B1: SPI e I2C, el tiempo límite que lo apaga en LPM4.5, FACTORY_RESET y escribir en la Flash de información |
| **ff** | El BSL de TI y BSL Scripter | El BSL de TI se queda esperando en un bucle con el núcleo a 24 MHz: el simulador lo corre a ~0,3 veces el tiempo real, y BSL Scripter espera cada respuesta solo 1 s. Contra el B2 BSL Scripter no llega a tiempo; contra el B1 (que duerme) sí. `bsl_p4.py` espera lo que haga falta con los dos |
| **gg** | La configuración del BSL en el TLV | Los pines de cada interfaz (etiqueta 15) son los del datasheet, de memoria; los bits reservados van a 0 [1F] |
| **cc** | El tiempo con OpenOCD | La sonda deja correr el tiempo simulado lo que pasa en el reloj de pared entre dos órdenes de OpenOCD (como una placa mientras la sonda habla despacio). Por eso una sesión de OpenOCD **no es reproducible al picosegundo** y `make openocd` no tiene invariante: comprueba resultados, no tiempos |
| **dd** | `adapter speed` con `remote_bitbang` | `target/stm32f4x.cfg` cambia la velocidad del adaptador en sus eventos de reset, y `remote_bitbang` no tiene velocidades: OpenOCD dice «Translation from khz to adapter speed not implemented» y sigue. Es de OpenOCD, no del modelo, y no se puede evitar sin tocar el `.cfg` |
| **ee** | La firma electrónica del F407 | El UID de 96 bits es SINTÉTICO, el mismo en todas las piezas; F_SIZE sale del tamaño de la Flash del modelo |
| **aa** | El ejemplo con RTOS | El cierre pedía uno «del SDK con FreeRTOS o TI-RTOS». Los del SDK van con TI-Drivers y SysConfig; se usa la demo de FreeRTOS para la MSP432 LaunchPad (repositorio de FreeRTOS, MIT), que es la que usa DriverLib con `MAP_` |
| **o** | Verificación previa y modos de lectura | `VER_PRE`/`AUTO_PRE` se guardan y no hacen nada; los modos de lectura de margen (`RDCTL.RD_MODE`), `RDBRST` y los marcadores de borrado (`BMRK`) no se modelan. Tampoco el límite de pulsos del TLV (`FLASHCTL`): se programa siempre a la primera |

Y una que no es del P4 sino del núcleo compartido: el SysTick con
`CLKSOURCE = 0` cuenta FCLK/8, que es la integración de ST; la referencia
externa del P4 no está contrastada.

### P-17 — El MSP432E401Y (T6 de `doc/analisis_msp432.md`)

**HECHA T6 el 2026-10-17**, en la rama `msp432`, en cuatro entregas con su
commit. Es la segunda familia de TI: el mismo `CortexM4F` (111 IRQ, 3 bits
de prioridad) con todo lo demás de Tiva. **Y T7 el 2026-10-18**, en seis
entregas (abajo, «La T7»).

* **El chip** (`soc/msp432e4/`, `periph/msp432e4/`): 1 MB de Flash en 0x0 con
  los estados de espera de `MEMTIM0` y la prebúsqueda, 256 KB de SRAM, 6 KB
  de EEPROM, la ROM en 0x0100_0000; SYSCTL con PIOSC, MOSC (el cristal de
  25 MHz de la placa), PLL, `RSCLKCFG` y `MEMTIM0` (que solo cuenta con
  `MEMTIMU`), `RCGC`/`SR`/`PR` con el retardo de «periférico listo» y el
  error de bus de tocar uno sin reloj, dicho una vez; RESC con sus causas;
  GPIO A-Q (sin I ni O) con el dato enmascarado, `DEN` (avisa si se escribe
  una salida sin él), `LOCK`/`CR`, interrupciones por pin en P y Q y las
  funciones de `pin_map.h`; los dos perros; el controlador de Flash con su
  `programador_nv_if` (el `load` de GDB) y COMT; la EEPROM; ocho UART, cuatro
  SSI, diez I²C, ocho GPTM, dos ADC, la uDMA de 32 canales y la hibernación
  con su RTC (el chip se apaga y despierta con la alarma o con WAKE).
* **La ROM R1** (`verif/fw/msp432e4/rom_r1`): DriverLib del E4 compilada en
  las 858 casillas de `rom.h` (85 al hueco, las que no tienen fuente BSD) y un
  **cargador serie propio** con el protocolo de las herramientas de TI. Con la
  Flash en blanco o con `BOOTCFG` pidiéndolo por un pin, el chip arranca en
  él; `ROM_UpdateUART` entra sin medir baudios. `mcu-sim` sin firmware deja
  la Flash del E4 en blanco, y `verif/serie/sflash_e4.py` (o el `sflash` de
  TivaWare por un redirector COM–RFC 2217) lo programa por el *backchannel*.
* **La placa** `placas/msp_exp432e401y.xml`: los cuatro LED, los dos
  pulsadores, RESET, los dos cristales y el *backchannel* en el 3357.
* **El banco**, `make teste4`: **203 comprobaciones, `resto`
  `3683870847978 ps`**, con `asane4` limpio. *Blinky* en los cuatro LED,
  120 MHz por las dos fuentes del PLL con el aviso de `MEMTIM0` y del PLL,
  CoreMark, la Flash y la EEPROM con DriverLib, GDB por los pines (G2) y por
  el DAP (G1) con `load` (D2), los periféricos con DriverLib (`e4_t3`), la
  ROM y su cargador (`e4_rom` con `MAP_`). Y `make sflash-e4`, de punta a
  punta contra el `mcu-sim` de verdad, en el CI.

Lo que queda dicho, con la letra con la que lo cita el código:

| | Qué | Por qué |
| :--- | :--- | :--- |
| **a** | GPIO y pines | **El §2 del análisis se equivocaba**: `msp432e401y.h` y DriverLib del E4 solo nombran la apertura AHB de los puertos, no la APB de otros Tiva, `0x4005_8000 + 0x1000·n`, y el modelo solo tiene esa. Son quince puertos, A-Q sin I ni O (R, S y T son del E411Y), y los protegidos por `LOCK`/`CR` son PC0-PC3 y PD7 (PE7 no sale en el E401Y). El reparto de pines por puerto es el del TM4C1294NCPDT, el mismo silicio; PA6 y PQ4 [1F]. *Pull-up* y *pull-down* de 20 kΩ y sin tolerancia a 5 V [1F]; las fuerzas de 2/4/8/12 mA, de modelo |
| **b** | La ROM y el arranque | La ventana de 256 KB y el alias por `RMCTL.BA` (la ROM en 0x0 mientras se arranca en ella, y el cargador lo borra antes de saltar) son [1F], de los Tiva. La R1: mismas funciones en las mismas casillas, **otras instrucciones**; `ROM_VERSION` vale 1; se lee sin estados de espera; las 85 casillas de SMBus, SPI Flash y `Update{I2C,SSI,USB,EMAC}` son un `BKPT`. Su cargador solo escucha por la UART0 (PA0/PA1) —no por SSI0, I2C0, USB ni Ethernet—, no comprueba el CRC de la imagen, y mide los baudios sondeando PA0: con PIOSC, hasta 115 200 está probado y por encima de ~460 800 el bucle puede perder flancos. Escrito desde la descripción del protocolo (SPMU301), sin el `boot_loader` de TivaWare, cuya licencia no casa con la AGPLv3. No hay volcado de la ROM de un E4 de verdad con el que comparar (la R2 está, sin probar contra una) |
| **c** | LFIOSC | 33 kHz típicos [1F, de memoria del datasheet]; DriverLib cuenta 30 kHz nominales |
| **d** | «Periférico listo» | 3 ciclos de SYSCLK entre `RCGC` y `PR` [1F: la nota de los datasheets de Tiva] |
| **e** | MOSC y PLL | Arranque del cristal en 1 ms y enganche del PLL en 100 µs: de modelo. `MOSCCTL` sale del reset con `NOXTAL` y `PWRDN` [1F] |
| **f** | La prebúsqueda | «Dos juegos de búferes de prebúsqueda de instrucciones, de 2 x 256 bits cada uno, combinables como uno de 4 x 256» [DS]: cuatro líneas de instrucciones con reemplazo LRU, y la siguiente se trae adelantada y sin coste; los datos, una línea propia. Se paga `FWS` al tocar una línea que no está. Cuándo se combinan los dos juegos y el coste real de la línea adelantada son de modelo [1F]. `FPFOFF` la apaga. `make e4-retardo` lo vigila |
| **g** | Tiempos y protección de la Flash y la EEPROM | De modelo: 30 µs por palabra, 15 ms por sector y por el borrado total, 100 µs por palabra de EEPROM. Sin errores de verificación (`PROGRIS`, `ERRIS`) ni reintentos (`EESUPP`); `EEPASS` y `EEHIDE` no cuentan. Protección de 2 KB por bit [1F]. **`FlashProtectSet` de DriverLib del E4 elige el registro con `(bloque / 32) % 4`**, que es de los Tiva de 256 KB: por encima de 256 KB protege el bloque equivocado. Es de DriverLib, no del modelo, y `e4_nv` lo esquiva |
| **h** | RST y WAKE | Los dos con *pull-up* interno [1F]; 50 kΩ de modelo |
| **i** | Periféricos | UART sin IrDA, tarjeta, 9 bits ni líneas de módem; SSI e I²C solo maestros (SPI de Freescale; sin *quick command* ni `MCLKOCNT`); GPTM sin modo RTC, sin las interrupciones del PWM ni `TnWOT`/`TnSNAPS`/`TCACT`; ADC ideal, sin comparadores digitales, promediado ni DMA, `GSYNC` no sincroniza; la uDMA sin *scatter-gather* y, cuando acaba un canal de software, un **pulso** en la línea UDMA (la cabecera del E4 no tiene `CHIS`) [1F]; la hibernación sin calendario, manipulación, retención de GPIO ni la espera de `WRC`, y la causa de despertar en `RESC.HIB` [1F]. Los modos de sueño no paran relojes (`SCGC`/`DCGC`/`DSCLKCFG` se guardan); el SysTick con `CLKSOURCE = 0` cuenta SYSCLK/8, como en los STM32, y en el E4 es PIOSC/4; `SRGPIO` no resetea un puerto suelto; un perro con `INTTYPE = 1` da la interrupción normal, no el NMI |
| **j** | Las tablas de AES de la ROM | `ROM_pvAESTable` es la dirección de la casilla 7 de `ROM_SOFTWARETABLE`, donde la ROM de TI guarda datos; en la R1 esa casilla es el hueco |
| **k** | ~~Lo de T7~~ | **Resuelto en T7**: el CCM, los C_CAN, el EMAC con su PHY y el USB están modelados (**o**, **p**, **q**, **s**). Lo que sigue declarado, en **t** |
| **l** | `sflash` de TI | El protocolo se ha probado con el banco, con `sflash_e4.py` (pySerial, RFC 2217) y con **BSL Scripter 3.4 de TI** en su modo `E4xx UART` (por un pty de socat, con su guion de ejemplo: el cargador de la R1 le contesta y programa lo que manda). `sflash` de TivaWare, LM Flash Programmer y UniFlash no se han probado: no se alcanzan desde aquí |
| **m** | `SysCtlDelay` (ROM, Flash o SRAM) | TI cuenta 3 ciclos por vuelta; el núcleo del modelo cobra el salto tomado con 2 de rellenado (P = 2 de 1..3 [ARM DDI 0439]) y da 4, un 33 % de más. Es del núcleo compartido (T-tiempos), no del E4: cambiarlo mueve los tiempos de todos los chips. `e4_retardo` lo mide |
| **n** | OpenOCD contra el E4 | No se ha probado (D3/G3 del E4 no eran de T6). El E4 se depura con el stub interno por los pines o por el DAP |

#### La T7: conectividad y criptografía

* **Entrega 1, el CCM** (`periph/msp432e4/ccm.h`): CRC (cuatro polinomios y
  la suma de TCP), SHA/MD5 con SHA-224 y SHA-256 (`hash_algo.h` los gana),
  HMAC, AES (ECB, CBC, CFB, CTR, ICM y GCM; 128/192/256) y DES/TDES.
  `e4_t7` es un servidor de vectores: **todos** los de `verif/vectores`
  (`cryp2.vec`, `sha2.vec`, `crc.vec`, nuevos, comprobados cada uno con dos
  motores por `comprueba_vectores.py`) pasan por DriverLib y por el chip.
* **Entrega 2, los C_CAN** (`ccan.h`, sobre `periph/can_motor.h`, el motor
  que ahora comparten con el bxCAN): CAN0 y CAN1 en el mismo hilo con
  filtros, FIFOs, extendidos y remotas, y **dos E4 en la misma placa**
  hablando por el bus CAN que ya había. El NVIC pasa a ser de nivel (**r**).
* **Entrega 3, el EMAC** (`emac.h`, el `eth_mac.h` del F407 con mapa propio
  y una frontera de tramas) con el **PHY interno** (`EphyE4`), y el **puente
  Ethernet** (`parts/puente_ethernet.h`, `mcu-sim --eth` o `eth=`): memoria,
  `qemu:PUERTO` o `qemu-cliente:HOST:PUERTO` -el protocolo de `-netdev
  stream` de QEMU, gvproxy y passt- y `,pcap=`. `e4_eth` es lwIP 2.2.1 (BSD-3,
  en TERCEROS.md) con eco TCP y UDP en el 7; `make eth-e4` lo prueba contra
  el `mcu-sim` de verdad con `verif/eth/eco_e4.py`, que es otra pila.
* **Entrega 4, el cargador por Ethernet** de la R1 (`ROM_UpdateEMAC`, BOOTP y
  TFTP a la Flash, y el reset): 84 casillas al hueco, una menos. `e4_bootp`
  lo llama; en el banco y por el puente (`make eth-e4`) graba `e4_eth.bin` y
  arranca lo grabado.
* **Entrega 5, el USB0** (`usb.h`, el MUSB de Mentor como dispositivo) con
  `e4_usb`, un CDC-ACM sobre `usb.c` de DriverLib; y **el puente USB de
  clase** (U2 de `doc/analisis_puente_usb.md`, `parts/puente_usb.h`):
  `mcu-sim --usb` o `usb=` pone un PC en D+/D- que enumera el CDC y lo lleva
  a un canal del puente serie (RFC 2217, TCP, sus clientes o memoria). Los
  baudios, el formato y DTR/RTS del terminal llegan al firmware como
  `SET_LINE_CODING` y `SET_CONTROL_LINE_STATE`. `make usb-e4`, con pySerial.
* **Entrega 6**: lo que queda declarado (**t**) y los documentos.
* `teste4`: **296 comprobaciones, `resto` `4507254220852 ps`** (E30-E36);
  E0-E29 no se mueven en ninguna entrega. Las otras seis suites, tampoco.
  Tras la prebúsqueda de cuatro líneas (**f**, 2026-10-19): `resto`
  `4505148675235 ps`, 2,1 ms menos.

| | Qué | Por qué |
| :--- | :--- | :--- |
| **o** | El CCM | Cada bloque está listo en el acto [modelo]. El orden de los bytes de los registros de resumen es el del controlador omap-sham de Linux, la misma IP [1F]. Sin uDMA (`SYSCONFIG`/`DMAIM` se guardan) ni interrupciones (el E401Y no las tiene en el NVIC); los modos de AES CCM, CBC-MAC, XTS, f8 y f9 se guardan y avisan |
| **p** | Los C_CAN | Copian entre las interfaces y los objetos en el acto (`BUSY` no se ve); salen de bus-off al quitar `INIT` sin los 128 × 11 bits [modelo, como F-29]; el modo básico y el control directo de CANTX (`CANTST.BASIC`/`TX`) se guardan y avisan [Bosch C_CAN User's Manual 1.2] |
| **q** | El EMAC, su PHY y el cargador | El MAC es el del F407 (sin PAUSE, VLAN, semidúplex ni la suma de ENTRADA). El PHY negocia en 1 ms (en el chip, unos 2 s), sin pines MII ni LED, y el MDIO solo llega a la dirección 0; ID1/ID2 = 0x2000/0xA221 [1F, de memoria del datasheet de la TM4C129]. El puente Ethernet es de capa 2: la red la pone el otro extremo (gvproxy, passt, QEMU u otro mcu-sim). El cargador de la R1: BOOTP sin DHCP, TFTP sin opciones (`blksize`), sin comprobar la imagen, y con la Flash en blanco el arranque solo escucha por la UART0 |
| **r** | El NVIC, de nivel | Una línea que sigue alta al salir de su manejador vuelve a quedar pendiente [ARMv7-M B3.4.1]; se mira 1 ps después de la salida, cuando el periférico ya ha publicado lo que le escribió el manejador [modelo]. Ningún invariante de los STM32 ni del P4 se mueve |
| **s** | El USB0 y el puente USB | Solo dispositivo de velocidad completa: sin modo anfitrión ni OTG (SRP/HNP), alta velocidad, ULPI, uDMA del USB, isócronos, LPM, suspensión ni detección de VBUS e ID. Los paquetes cruzan como paquetes, sin NRZI, relleno ni CRC, y sin el conmutador DATA0/DATA1 [T-17]. El puente: solo la clase CDC-ACM (HID y MSC de U2 no), una configuración, sin hubs ni suspensión; las notificaciones del endpoint de interrupción (SERIAL_STATE) no se leen, y las líneas de módem que ve el terminal son fijas (CTS, DSR y DCD con el dispositivo enumerado). U1 (USB/IP) y U3 siguen sin hacer |
| **t** | Lo que sigue declarado | El EPI (como pide el plan), PWM, QEI, los comparadores y SYSEXC: el registro contesta, no guarda y avisa una vez. El LCD no es del E401Y, sino del E411Y (T8) |
| **u** | Las placas y su dibujo | `msp_exp432p401r.xml`, `msp_exp432e401y.xml` y `boostxl_edumkii.xml` están revisadas contra SLAU597F, SLAU748B y SLAU599B (guía y esquemáticos) y llevan su dibujo; los sistemas de las dos LaunchPad con la EDUMKII, `make p4-edumkii` y `make e4-edumkii`. **De modelo**: el RGB y el zumbador de la EDUMKII van por transistores desde los 5 V, y aquí son la carga de su pin (un LED con la resistencia de su cátodo; 2,9 kΩ); la luz de la pantalla, igual; su vidrio es de 128x128 y el `Tft128x160` enseña 160 filas; el TPS2052B de la E401Y (el interruptor de los 5 V) y el XDS110 de las dos no están, y su lado de J101 es de adorno salvo el backchannel y los 5 V; el pad VREF+ del E4 (J10.34) no existe en el modelo. Las medidas de la P401R y de la EDUMKII en los dibujos son aproximadas (la E401Y, 63,5 x 174 mm, de su guía). ~~Un `SysCtlDelay` largo desde la Flash a 120 MHz tarda en el modelo bastante más que 3 ciclos por vuelta~~ **Resuelto (2026-10-19)**: el modelo tenía una sola línea de prebúsqueda de instrucciones y no adelantaba la siguiente, así que un bucle que tocaba dos líneas de 256 bits -por sí mismo o por la búsqueda adelantada del núcleo, que lee la palabra que sigue al salto- pagaba los cinco `FWS` dos veces por vuelta: 14 ciclos en vez de 4. Ahora son cuatro líneas y la siguiente adelantada (**f**), y `make e4-retardo` mide las seis variantes a 4 ciclos. Queda el ciclo de más de **m**. `e4_edumkii` sigue esperando con el TIMER1, que es exacto a cualquier reloj |

### P-18 — El STM32F303 y la STM32F3DISCOVERY

**Analizado en `doc/analisis_stm32f303.md` (2026-10-19), con un plan de diez
fases (S0-S9). HECHAS S0, S1, S2 y S3 el 2026-10-20 y S4, S7, S8 y S9 el 2026-10-21** en la rama `stm32f303`
(«Estado» del análisis): los rasgos neutrales, el STM32F303VC con su RCC, su
Flash con el FPEC, la CCM ejecutable, la memoria de sistema y el arranque por
BOOT0/nBOOT1/`MEM_MODE`, la STM32F3DISCOVERY con su dibujo, `testf3` (R1-R7,
F0-F19), `make f3-discovery`, O5 y C6 con OpenOCD, y G8 en `mcu-sim-gui`; y en S3
los periféricos de segunda generación (USART v2, DMA por canales, SPI v3, I²C,
TIM v3, perros, RTC, CRC, bajo consumo), el VCP, el sistema de dos Discovery,
los 29 ejemplos de ST de S3 sin modificar (F22-F53) y `make f3-vcp`; y en S4
los sensores de la placa (`parts/mems_st.h`: la `Postura` y los cuatro MEMS de
las dos revisiones, con `revision=` en la placa), el ejemplo `BSP` de ST en las
dos revisiones, `f3_mems` contra la postura (F54-F57) y la demo de ST con
`make f3-demo` (F58); y en S7 el cargador de sistema propio con el protocolo de
AN3155 (`soc/stm32f3/cargador/`), que `stm32flash` usa sin modificar
(`make f3-stm32flash`, F59-F60); y en S8, en la ventana, el mando de
inclinación y la aguja del rumbo (`observable=` en la tabla de enlaces); y en S9 los
quince del die 0x422 (F303xB/C, F302xB/C y F358xC, F61-F62). **Queda S5 (lo
analógico) y S6 (USB y CAN)**, y los otros dos dies, con su plan en
`doc/analisis_f303xD-E_f303x6_8_F334.md`. Es la primera familia del proyecto con la
segunda generación de IP de ST: el I²C es el `FMPI2C1` del F446 (`i2c2_v1_1`),
y bxCAN, DAC, RTC, perros, CRC, EXTI, GPIO y temporizadores se reaprovechan por
rasgos; RCC, controlador de Flash (el del F1), DMA por canales, USART v2, ADC,
USB FSDEV, COMP y OPAMP son nuevos, y el SPI tiene la misma estructura con
otro comportamiento (FIFO, `CR2.DS`, `CRCL` en el bit 11). El sistema de
pruebas es la STM32F3DISCOVERY, con sus dos revisiones de sensores
(L3GD20 + LSM303DLHC hasta la D; I3G4250D + LSM303AGR desde la E), su XML y su
SVG. Lo primero, S0: tener RM0316, DS9118 y UM1570 en `doc/refs/`, que desde
el entorno de trabajo no se alcanzan.

Lo que queda de una sola fuente, o de modelo, tras S9:

| | Qué | Estado |
| :--- | :--- | :--- |
| **a** | Las tiras P1 y P2 de la Discovery (`EXP1`/`EXP2` en el XML: «P1.5» sería el pad PB5) | ~~Emparejamiento deducido~~ **Resuelto (2026-10-20)**: la tabla 7 de UM1570 Rev 7 y la hoja 1 del esquemático MB1035 coinciden con el XML. Con los puentes de fábrica, P2.5/P2.6 (SB17/18) y P2.41/P2.42 (SB21/22) quedan sin conectar |
| **b** | Los tiempos del FPEC | **Contrastado con DS9118, tabla 47 (2026-10-20)**: 53,5 us por media palabra (el típico); 20 ms por página, por borrado masivo y por los bytes de opción, el mínimo de 20-40 ms, porque la tabla no da típico [de modelo] |
| **c** | La protección de escritura | Un bit de `WRPR` por cada dos páginas y el 31 para el resto, como en el F303xC [1F: RM0316]. `MER` con alguna página protegida da `WRPRTERR` y no borra [de modelo] |
| **d** | RDP | DECLARADO: se carga en `FLASH_OBR.RDPRT`, pero no bloquea al depurador ni borra la Flash al quitarlo |
| **e** | Los bytes de opción borrados | Un byte a `0xFFFF` (borrado) se carga como `0xFF` sin `OPTERR`; uno cuyo complemento no cuadra, como `0xFF` con `OPTERR` [1F] |
| **f** | El reset durante un borrado | En el modelo el borrado acaba igual; en el silicio el resultado queda indefinido |
| **g** | La memoria de sistema | ~~Un stub que deja `'MSYS'` y se aparca~~ **S7 (2026-10-21)**: un cargador PROPIO con el protocolo de AN3155 por USART1 y USART2, a 8E1 y con la velocidad del `0x7F`; `stm32flash` 0.7 lo usa sin modificar. De modelo: sin I²C, SPI, CAN ni DFU; WRITE PROTECT y READOUT PROTECT contestan NACK; la velocidad se mide con el SysTick (el de ST usa un temporizador), y sale un 2 % corta a 115 200 por el quantum. AN2606 y AN3155 no están en `doc/refs/`: lo citado sale de ellos de memoria y de `stm32flash` (su `dev_table`: la SRAM libre desde `0x2000_1400`) [1F] |
| **h** | La calibración de fábrica y el UID | Valores típicos de DS9118 (tablas 28 y 78: `TS_CAL1` 1748, `TS_CAL2` 1321, `VREFINT_CAL` 1526), no de una pieza; el UID, del modelo |
| **i** | Los bloques de S4-S6 | DECLARADOS: ocupan su ventana, leen cero y avisan una vez con la fase que les toca (`limitaciones()` del chip los lista). Los de S3, hechos; de ellos queda declarado el I2S en dúplex (`I2S2ext`/`I2S3ext`) |
| **j** | La matriz | ~~Se decide con los DMA~~ **Decidido en S3**: decodificadores directos; cada DMA, un iniciador con su socket, un ciclo de HCLK por fase y sin contención con la CPU [de modelo] |
| **k** | Lo que se cita de RM0316 | **Contrastado con RM0316 Rev 10 (2026-10-20)**, que ya está en `doc/refs/stm32f303/`: confirma lo citado y obliga a doce cambios (la tabla del «Estado» del análisis), probados en F20, F21, R4 y R6 de `testf3` |
| **m** | Lo que RM0316 no dice | De modelo: una llave con `CR` abierto lo cierra; escribir en una página protegida de la CCM es error de bus; `REV_OUT` refleja los `POLYSIZE` bits; el aviso de `HLFCYA` a partir de 8 MHz; BOOT0 se muestrea durante el reset (el silicio, en el cuarto flanco de SYSCLK); `HSICAL` y el USER de fábrica |
| **n** | RDP, a medias | El nivel se carga (byte y complemento), volver al 0 borra la Flash y el 2 bloquea los bytes de opción; pero el nivel 1 no cierra la Flash al depurador ni el 2 apaga la depuración (RM0316 §4.3.1, tabla 9) |
| **o** | El NMI del CSS | Un pulso por fallo; en el silicio se repite hasta que `CSSC` borra `CSSF` (RM0316 §9.2.7). El fallo tampoco llega al freno de los TIM (S3) |
| **p** | El IWDG y el reset del sistema | En el F3 se para con el reset del sistema (`IwdgCaps::para_con_reset`; sin ello `IWDG_Reset` de ST seguia reiniciando). El F4 no se ha tocado, para no mover sus invariantes: **queda por mirar en RM0090 si el suyo debería pararse también** |
| **q** | El núcleo, arreglado en S3 para todos | La vuelta de excepción desapila IT[1:0] y GE y no borra el ITSTATE; el MSP se guarda antes de leer la trama de vuelta; WFI solo despierta con una excepción que podría entrar (ARMv7-M B1.5.19). Las siete suites de antes, al picosegundo |
| **r** | Lo que S3 no modela | USART: Smartcard, IrDA, auto-baud y `UESM`; I²C: `WUPEN` y PEC; el I2S en dúplex; el reloj de `I2S_CKIN`; el ITR del TIM15 desde el TIM16/17 es la salida OC1 y no OC1REF; la corriente en los modos de bajo consumo; el IWDG se para en Standby (el silicio sigue contando); el fallo del HSE, el PVD y LOCKUP no llegan al freno de los TIM |
| **s** | `testf3` y su tiempo | Con los 32 grupos de S3 y los cuatro de S4 tarda unos 9-10 min de reloj en la máquina de trabajo (dos de ellos los ponen las diez parejas de dos Discovery): el CI le da 1200, 1500 y 1800 s de presupuesto (`SUITES_PRESUPUESTO`, Linux, macOS y Windows) y 15-25 min mas a cada trabajo |
| **t** | Los ejes de los sensores (S4) | El giróscopo, de dos fuentes que coinciden (el ejemplo BSP y la demo de ST). El acelerómetro, de una: el ejemplo BSP (x al norte, y al oeste); **la demo de ST usa otra tabla**, que con z hacia fuera no es una rotación de la del BSP, y en el modelo enciende LD6 con el norte abajo (F58 lo comprueba así). El magnetómetro del AGR, del libro «Discovery» de Rust (x al sur, y al este); el del DLHC, como su acelerómetro [de modelo]. El I3G4250D, con los ejes del L3GD20. **Con una placa en la mano se resuelve en un minuto**: inclinarla con la demo y con el ejemplo BSP |
| **u** | Los sensores, de modelo (S4) | Las hojas de datos de los cuatro MEMS no están en `doc/refs/` [1F]. De modelo: las temperaturas (0 cuentas a 25 °C), el paso alto de primer orden (el del giróscopo con la tabla de HPCF a 95 Hz escalada; el del acelerómetro, una fracción de ODR), los umbrales de INT (alto y bajo como |valor| frente al umbral, sin la duración), el DRDY del magnetómetro (se baja al leer el último byte), las sensibilidades del DLHC fuera de HR. Sin FIFO, click, 6D, SPI de tres hilos ni ruido |
| **v** | El I2C del STM32 (S4, también el F446) | `TC` se borra con `START`/`STOP` y `TCR` al escribir `NBYTES` [RM0316 §28.7.7]; SCL y SDA de drenador abierto por el periférico, sea cual sea `OTYPER` [de modelo: el BSP de la Discovery pone `AF_PP` y funciona] |
| **w** | La sincronización de las escrituras del F303 (S4) | `Cpu::sync_escritura_perif`: el núcleo del F303 sincroniza con el planificador antes de cada escritura a un periférico, para que un pulso de GPIO más corto que el quantum (el CS del giróscopo, 0,2 µs) no desaparezca. **Los F4 y los MSP432 no**: tienen el mismo problema latente, y ponerlo movería sus invariantes |
| **x** | El `IDR` tras un reset del GPIO (S7, todos los chips) | Se ponía a cero y no se volvía a muestrear hasta que un pad cambiase; ahora se muestrea al salir del reset. Las siete suites, al picosegundo |
| **y** | El arranque en el cargador, en la placa | `placas/stm32f3discovery_boot0.xml`: un puente de P2.19 (BOOT0) a P2.20 (VDD), como se hace con una Discovery de verdad, en vez de abrir SB19; el FT232RL en la USART1. `make f3-stm32flash` va con `-m 8n1` por el pty (Linux no guarda la paridad de un pty); la paridad de la línea la pone el puente RFC 2217 |
| **z** | La aguja y la inclinación (S8) | La aguja de `MOV` es un indicador de rumbo (marca hacia dónde mira el borde norte de la placa), no una brújula que señale el norte: es lo que el efecto `angulo` sabe hacer, a la derecha los positivos. El mando de inclinación pone el punto en el lado que BAJA, como el LED del ejemplo BSP; el borde son 90 grados, aunque el alabeo llegue a 180 |
| **aa** | La familia del die 0x422 (S9) | Lo que falta en el F302 y el F358 sale de dos fuentes (la base de pines y CMSIS); la CCM que falta en el F302, solo de CMSIS. Lo que **no** se modela: el F358 se alimenta de 1,65 a 1,95 V sin regulador y con NPOR, y el modelo lo alimenta y arranca como al F303; los bloques analógicos que faltan en el F302 (ADC3/4, COMP3/5/7, OPAMP3/4) se notarán con S5. Las hojas de datos del F302 (DS9896) y del F358 no están en `doc/refs/` |
| **bb** | El CI de S4 (2026-10-21) | Falló en las cinco plataformas por T126 de `test407`: la `Postura` no tiene patillas y la regla de `sim --help` exige terminales. Ahora `Ayuda::sin_terminales("por qué")` cuenta como tales, y la ficha dice «TERMINALES ninguno» con el motivo. Las invariantes no se habían movido: lo que fallaba era una comprobación |
| **l** | La placa contra su esquemático | Contrastada con MB1035 rev. D.1 (2026-10-20): resistencias de la rosa (510 Ω en los verdes), B1 (100 + 330 Ω a VDD, 220 kΩ a masa), LD1, BOOT0, R39 y X3 sin montar. La vf de cada LED es de modelo; VDD se modela a 3,3 V, y en la placa son los 3,3 V del LD3985 menos el diodo D3 [de modelo] |

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
| **F-10** | **Filtro digital de entrada de los temporizadores** (`ICxF`, `ETF`) | F4-TIM §7 | Se aplican polaridad y `ICxPSC`, pero no se filtra por número de muestras. "Afecta solo a entradas con rebotes". **Desde que el pulsador rebota (P-12 k), hay entradas con rebotes**: un pulsador en un canal de captura es ahora el caso de prueba natural de este filtro |
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
| **T-16** | **La velocidad del stub GDB depende del ritmo de simulación** (atiende el socket cada 100 µs simulados) | F6-gdb §8 | "No es un problema de corrección sino de ritmo". **2026-09-23: y además consume TIEMPO SIMULADO que depende del anfitrión, cosa que hasta ahora no estaba dicha.** Mientras el stub espera la respuesta de un `gdb` de verdad por un socket de verdad, el reloj simulado avanza a saltos de 100 µs, y cuántos saltos hagan falta lo decide la máquina. Medido con el mismo commit y los mismos firmwares: `T96+T97` valen **96 665 875 ns** en Linux, Windows y macOS arm64, y **97 065 875 ns** en macOS Intel —cuatro sondeos más, 400 µs—, mientras **«el resto» vale `2239552024213 ps` en las cuatro, al picosegundo**. Por eso `verif/invariantes.txt` contrasta ahora `resto` y no `total` en la suite del F407: el total llevaba dentro una cifra que no es del modelo. **Arreglarlo de verdad** —que el stub no gaste tiempo simulado esperando a un socket— devolvería el criterio al total. **2026-10-21: y el plazo del cliente también era solo simulado.** En el CI de macOS (Apple Silicon) `testp4` se cayó con `stoul: no conversion`: la respuesta a una orden llegó por el socket después de que venciera su plazo simulado, y la siguiente petición la tomó por suya. `verif/gdb_client.h` ya no se rinde hasta que han pasado el plazo simulado **y** medio segundo de reloj, y los bancos leen un registro sin lanzar excepciones (una respuesta rara hace fallar la comprobación, no la suite). Solo cambia algo cuando una respuesta llega tarde, y eso cae en la parte que «el resto» no cuenta |
| **T-17** | **Capa física de paquetes, no de bits, en el USB**: sin NRZI, sin relleno de bits, sin CRC5/CRC16 ni reintentos por CRC | F7-OTG §3.1 y §6 | **Frontera deliberada**, escrita en la cabecera del fichero: "simular el bitstream no aporta nada a un modelo de MCU" |
| **T-18** | **Capa eléctrica del par trenzado y autonegociación fuera del Ethernet** | F7-ETH §3.1 | Es cosa del PHY, al otro lado de los pines; la autonegociación se resuelve por MDIO como en el silicio |
| **T-22** | ~~**El tiempo simulado total depende de los FIRMWARES, que no se versionan**~~ **RESUELTO: los `.bin` se versionan.** El síntoma: `2336217899213 ps` se mantuvo al picosegundo doce fases, y en Windows salía `2334217899213 ps`, **2 ms menos**. La causa no era el anfitrión: los `.bin` se compilaban en cada máquina con el compilador cruzado que allí hubiera —`arm-none-eabi-gcc` 13.2 de Debian aquí, 13.3.1 de STM32CubeIDE allí—, un binario distinto ejecuta un número distinto de instrucciones, y **T17 sondea el final de CoreMark cada 2 ms**, así que la diferencia salía **cuantizada a 2 ms**. **Medido en una sola máquina**: recompilando CoreMark de `-O2` a `-O1`, el total se mueve **24 ms exactos**, doce pasos del bucle. **Confirmado entre las dos**: 14528 B / `0x644FCE21` frente a 14856 B / `0x5157F2C7`. No se comparaban dos ejecuciones del mismo programa, sino dos programas distintos. **La corrección** (2026-09-21) quita la causa en vez de documentarla: los **19 `.bin` (37 KB) se versionan**, con excepción explícita en el `.gitignore` y `*.bin binary` en un `.gitattributes` nuevo para que Windows no los toque; **`src/verif/fw/huellas.txt`** es la única fuente de verdad —ruta, tamaño y FNV-1a de cada imagen, y la cadena cruzada que las produjo—; y el **primer grupo de cada suite** (`T00` en el F407, `A0` en las otras dos) lo contrasta antes de simular nada, a coste cero de tiempo simulado. Comprobado que falla en los tres casos en que debe: imagen ausente, imagen alterada un byte, manifiesto ausente. Las suites pasan de 2117/203/164 a **2118/204/165**, con los tres invariantes **intactos**. *(La primera hipótesis —que la culpa era del sondeo del socket de GDB— quedó **refutada** por la propia medida que se añadió para comprobarla: T96+T97 consumen `96665875 ns` **idénticos** en las dos plataformas.)* **CONFIRMADO EN WINDOWS**, con la predicción escrita antes: las tres suites dan allí **2118 / 204 / 165** y **`2336217899213` / `1033367277932` / `718988288 ps`**, el mismo picosegundo que en Linux, y la huella `0x644FCE21` del manifiesto. Los 2 ms eran el binario y nada más. **Lo que sigue abierto**: `make fw407` y compañía regeneran los binarios y solo **avisan**; sigue sin verificarse que esta cadena cruzada los reproduzca byte a byte en otra máquina —no lo hace la de CubeIDE, y eso es justamente el motivo de versionarlos—; El invariante **ya no es de una máquina**, es de dos sistemas operativos y dos compiladores de C++. | F6-gdb2 §8 |

### Tres divergencias de comportamiento, no solo de precisión

| Id | Divergencia | Fase |
| :--- | :--- | :--- |
| **T-19** | **LOCKUP**: el modelo lo señala con `halted_on_lockup` y detiene la ejecución; **en hardware el núcleo queda con `PC = 0xFFFFFFFE`** | F2 §5.1 |
| **T-20** | **`VOS` afecta al consumo pero no a la frecuencia máxima**: el modelo **permite correr a 168 MHz en escala 3**, cosa que el silicio no admite. Único caso catalogado de comportamiento **más permisivo** que el hardware | F7-lowpower §7 |
| **T-21** | **Un evento del EXTI despierta también de un `WFI`**. En ARM el registro de evento no debe despertar de un `WFI`. La distinción está hecha **a medias**: el núcleo ya no despierta con el registro de evento, pero sí con el nivel de la entrada | F7-lowpower §7 |
| **T-23** | ~~**El IWDG resetea UN TICK ANTES de su plazo**~~ **CORREGIDO el 2026-09-23.** El modelo reseteaba al LLEGAR el contador a cero, o sea a las RLR cuentas; [RM0090, §21.3] da el plazo como `t_LSI x 4 x 2^PR x (RLR+1)`, que es lo que `timeout_s()` decía aquí desde siempre. Con un plazo de 201,0 ms el perro ladraba a los 200,0; ahora lo hace a los 201,1 —el primer sondeo del banco después del plazo—. **Lo destapó una diferencia entre versiones de SystemC**, no una comprobación: con la 3.0.2 el redondeo caía del otro lado y compensaba el tick por accidente. Y **la comprobación que debía cazarlo, `check_near(dti, timeout_s, 0.05)`, tenía un 5 % de tolerancia para un error del 0,5 %: diez veces más floja que el defecto.** Ahora es del **0,2 %** —±0,4 ms, cuatro veces el paso del sondeo y menos de medio tick—, o sea que puede fallar, que es lo mínimo que se le pide. **Mueve el invariante, a propósito y por primera vez desde la fase 7**: la columna `resto` del F407 pasa de `2239552024213` a `2240553274213 ps` (1,00125 ms más) y el total a `2337219149213`. Las otras dos suites no se mueven. Comprobado con SystemC 2.3.4 y 3.0.2: mismas cifras | F5-RTC/WDG, 2026-09-23 |

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
| **V-11** | **T53 (I2S) depende del INSTANTE SIMULADO en que arranca.** La comprobación «no se pierde ni una muestra» compara cuántas entraron en el esclavo con cuántas salieron del maestro, y ese número cambia según la fase con la que el enlace de audio engancha — que a su vez depende de **todo lo que se haya ejecutado antes en la suite**. Se descubrió ejecutando la suite en Windows con los firmwares sin compilar: al no correr CoreMark, T53 empieza en otro instante y pierde una o dos muestras. **No es un fallo del modelo de I2S**; es una comprobación que mide algo que no es estable. Arreglarla es sincronizar antes de contar, o contar solo las muestras que llevan el patrón | F5-SPI §7 |
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
| **I-23** | ~~**Sin verificación en Windows ni en macOS**~~ **CERRADO el 2026-09-23: las CUATRO plataformas pasan en la integración continua.** | analisis_gui §19.3 | `Linux (apt, SystemC 2.3.4)`, `Windows (MSYS2, 2.3.4 compilada)`, `macOS Apple Silicon (Homebrew, 3.0.2)` y `macOS Intel (Homebrew, 3.0.2)`: **2118 / 204 / 165 y los tres invariantes**, y no hay que creérselo porque el trabajo falla si no coinciden (**P-13**). **Con una propiedad que no estaba en el plan**: las mismas cifras salen con **dos versiones de SystemC**, dos compiladores y tres sistemas operativos. El invariante dejó de ser una propiedad de una máquina y pasó a ser una del modelo. **Lo que costó, que es lo que merece quedar escrito**: seis fallos en cinco ejecuciones. Cuatro estaban en el entorno —el segundo motor de los vectores, CMake 4, el `#if` que no se evalúa del ensamblador de QuickThreads, pthreads cien veces más lento—; **uno estaba en el modelo** y llevaba ahí desde siempre sin que ninguna de las 2 118 comprobaciones lo viera (la guarda de redondeo, I-24); y **el sexto estaba en el criterio**, que incluía una cifra que no era del modelo (T-16). **Lo que NO cubre**: `make asan407` y compañía no se ejecutan en el CI, y macOS sigue sin probarse a mano en un Mac de alguien —lo que hay es hardware Apple alquilado por minutos, que para el caso vale, pero no es lo mismo que un alumno con su portátil— |
| **I-24** | ~~**Se sigue en SystemC 2.3.4 (IEEE 1666-2011) pudiendo estar en la línea 3.0 (IEEE 1666-2023)**~~ **DESBLOQUEADO: la 3.0.2 da EXACTAMENTE los mismos tres invariantes que la 2.3.4.** | analisis_systemc3 | **La historia entera, porque el camino importa más que el final.** Durante meses el punto decía que no se migraba porque «ninguna ventaja conocida de la 3.0 ataca un problema que tengamos». Luego la integración continua trajo dos argumentos —la 2.3.4 no configura con CMake 4, y la SystemC que empaqueta Homebrew para macOS es la 3.0.2—, y al probarla **la suite no terminaba**. **El diagnóstico, y la primera versión era falsa.** Se dijo «se cuelga en T24», y no: T24 era lo último que se veía porque el aviso de `SC_REPORT` va por `stderr`, sin búfer, y las comprobaciones van por `stdout`, con búfer de bloque. Con la salida sin bufferizar, el cuelgue de verdad estaba en **T25**, dentro del `delay_ms` del blinky, y el proceso gastaba el 100 % de CPU en `SysTick::tick_proc`. **La causa.** `elapsed_ticks_brutos()` interpola los ticks desde el tiempo simulado con un `double` que vale decenas de millones, y le sumaba una guarda de redondeo de `0.5e-9` ticks **cuando el error del propio `double` a esa escala es de unos 3e-9**. De qué lado cae el truncamiento depende del último bit de `to_seconds()`, y ese bit cambió: 69 000 000 000 ps son `0,069000000000000005773` en la 2.3.4 y `0,068999999999999991895` en la 3.0.2. Con la 3.0.2 el contador se quedaba **un tick corto**, el cruce calculado caía en el mismo picosegundo que el instante actual —luego `tick_proc` no esperaba— y el `continue` giraba en ciclos delta para siempre. **La corrección** está en `common/guarda_tick.h` y en los cuatro sitios que interpolan contadores (SysTick, IWDG, WWDG y los subsegundos del RTC): guarda de `1e-6` de tick, cinco órdenes por encima del error del `double` y seis por debajo de un tick, más un seguro en `tick_proc` para que un bucle que no espera no pueda existir. **El resultado, medido en las tres suites y en las dos versiones:** `2118 / 2336217899213 ps`, `204 / 1033367277932 ps` y `165 / 718988288 ps`, idénticos. **El invariante ha dejado de depender de la versión de SystemC**, que es más de lo que se pedía: ya no hace falta elegir una. **Lo que queda**: migrar del todo sigue siendo una decisión, no una urgencia, y su coste conocido es que en Linux habría que dejar el paquete de Ubuntu —la vía documentada— y compilar. De paso quedó destapado **T-23** |
| **I-25** | ~~**`sim` no tenía freno de tiempo real**~~ | analisis_gui §16 | **HECHO.** `--tiempo-real` ata el avance simulado al reloj de pared, y `--tiempo-real=<factor>` lo escala (`=4`, cuatro veces más rápido; `=0.5`, a la mitad para poder mirar). El proceso **se duerme** hasta que el reloj de pared alcanza al simulado, lo que dentro de un proceso de SystemC detiene toda la simulación —las corrutinas comparten hilo—, que es justo lo que se busca. Solo frena: si el modelo va más lento que el tiempo real no duerme ni acumula deuda. **Medido**: 2000 ms simulados salen en 0,033 s sin freno, **2,000 s** con `--tiempo-real` y 0,500 s con `=4`; y esperando a GDB la CPU baja de **99,6 % a 5,3 %**. Con esto un LED que parpadea a 1 Hz parpadea a 1 Hz y se puede mirar, que para un simulador didáctico no es un lujo. **Sigue abierto lo otro que decía este punto**: «correr sin parar» y «aceptar un depurador» siguen siendo la misma opción (`--gdb`), cuando son dos cosas distintas |
| **I-26** | ~~**`sim` acepta como firmware cualquier argumento que no reconozca**, incluida una opción mal escrita: `sim placa.xml --netlist` intenta cargar un fichero llamado `--netlist` y muere con «no se puede cargar --netlist»~~ | — | **ARREGLADO en la fase 7 de mcu-sim-gui (P-12)**, que es donde dejó de ser barata de ignorar: con un diálogo donde se escriben argumentos a mano, y con una placa que declara su propio firmware, la errata se tragaba sin ningún aviso. Ahora `opcion desconocida: '--netlist' (sim --help las enumera)` y código 1; lo vigila `make gui-argumentos`. Lo que se decía entonces: **Errata de usabilidad, barata.** `--netlist`, `--inventario` y `--valida` son opciones de `stm32f407vg` y solo la última existe en `sim`, así que confundirlas es lo esperable. Bastaría rechazar todo argumento posicional que empiece por `-` remitiendo a `--help` |
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
| **I-50** | ~~**Los informes de `doc/` pueden ser obra derivada de los manuales de ST y de ARM, y el repositorio es público**~~ | licencia, 2026-09-22 | **CERRADO el 2026-09-23, en dos pasos: salen del árbol y salen del pasado.** **Uno.** `informe.md`, `informe_revisado.md` e `informe_instrucciones.md` se han movido a `doc/refs/stm32f407xx/` —al lado de los manuales de los que salieron— y `.gitignore` excluye esa carpeta. **El criterio, que es lo que había que decidir y no se había decidido**: se va lo que se escribió para SUSTITUIR al manual, se queda lo que se escribió para RAZONAR sobre él. No es una corazonada: `informe_reglas.md` ordena por escrito *«prohibido remitir a otro documento… reproduce el dato en el propio informe»* y *«no resumas»*, y `informe_general.md` dice que el equipo destinatario *«NO tendrá acceso a los manuales originales»*. Eso es reproducción, no comentario. Por la misma regla **se quedan** los planes de fase, las dos comparativas entre piezas —que citan por sección en vez de reproducir— y los cuatro ficheros de prompts, que son el método. **Dos.** Sacarlos de la punta no los sacaba de los 116 commits anteriores, así que la historia pública se reescribió con `git filter-repo --invert-paths` nombrando **las seis rutas** —los tres ficheros en sus **dos** ubicaciones históricas, porque hasta `97aabd1` vivían en `doc/*.md` y no en `doc/stm32f4xx/`—. Un glob `doc/informe*` habría sido más cómodo y se habría llevado por delante los cuatro prompts, que se quedan. El repositorio de GitHub se borró y se volvió a crear con el mismo nombre en vez de forzar el empujón, para que **no queden objetos inalcanzables** sirviéndose por SHA: eso solo lo recoge Soporte de GitHub, que en plan gratuito no se puede pedir. **Lo que cuesta, y queda anotado donde se va a notar**: todos los commits cambiaron de hash, así que los que cita esta misma documentación no resuelven; el `README` y la §0 de `doc/fuentes.md` lo explican. **Y lo que sigue sin hacerse**: leer las dos comparativas con esta pregunta en la mano. Se han quedado por criterio, no por lectura |
| **I-51** | ~~**El repositorio es público y no tiene licencia**~~ | licencia, 2026-09-28 | **CERRADO: AGPLv3.** Estuvo bloqueado por I-50 —*solo se puede licenciar lo que es propio*— y al salir los informes se desbloqueó. **El criterio lo puso el encargo**: que un profesor de otra universidad pueda añadir cores, periféricos o placas **con la obligación de que el resultado quede al alcance de todo el mundo**. Eso es copyleft, y descarta MIT y Apache-2.0. **La GPLv2 estaba descartada de antes por compatibilidad**: SystemC es Apache-2.0, que es compatible con la v3 y no con la v2. **Y entre GPLv3 y AGPLv3 decide un caso que aquí no es hipotético**: un departamento que monte el simulador en un servidor para sus alumnos no *distribuye* nada, así que con GPLv3 no tendría que publicar sus cambios. La §13 de la AGPL cierra ese hueco, y el proyecto ya habla por socket con el depurador y con la interfaz. **Lo que trae consigo**: `LICENSE` en la raíz, la sección del `README`, `mcu-sim --licencia` —que cumple a la vez la §4 de la Apache-2.0 y la §13 de la AGPL, porque el aviso viaja con el ejecutable aunque el fichero se quede atrás—, y una comprobación en el CI que falla si falta `LICENSE` o si el titular sigue sin rellenar |
| **I-52** | **Buena parte de este código y de esta documentación la ha escrito un modelo de lenguaje, y eso toca al copyright** | licencia, 2026-09-28 | **ABIERTO, y no lo resuelve elegir licencia.** El copyleft se hace valer **a través del copyright**: si una parte del material no fuera obra protegible —en la UE la protección exige autoría humana y originalidad, y el estatus de lo generado por máquina no está asentado—, la obligación de publicar sería más débil sobre esa parte. **No es un problema práctico hoy** y no cambia nada de lo hecho, pero es una pregunta real que merece plantearse a quien sepa de esto antes de apoyarse en la licencia para exigir algo. Se anota aquí porque callarlo sería justo lo contrario de lo que hace el resto de este documento |
| **I-53** | ~~**El SysTick dejaba de interrumpir PARA SIEMPRE al reanudar una parada del depurador**, más o menos una vez de cada dos~~ | 2026-10-02, depurando desde CubeIDE | **HECHO.** El síntoma, en una sesión de CubeIDE contra `discovery_min.xml`: punto de ruptura en `HAL_GPIO_TogglePin`, *Resume*, y el programa se quedaba dentro de `HAL_Delay(100)` sin volver nunca al punto de ruptura —`uwTick` congelado—; si se suspendía y se reanudaba, a veces seguía. Visto desde la ventana: el LED cambiaba en un *Resume* que no terminaba, y el *Resume* siguiente llegaba al punto de ruptura sin cambiar ninguno. **La causa**, en `tick_proc()`: al reanudar, el contador sigue desde el valor `c` que tenía al parar, y el cruce por cero se calcula como `t_base_ + c/f` redondeado al picosegundo. A 168 MHz un tick son 5952,38 ps, y para más o menos la mitad de los `c` el redondeo cae hasta medio picosegundo ANTES del tick: falta uno, se espera 1 ps, y en la vuelta siguiente el contador ya vale cero, lo que el cálculo toma por «ya recargado» —espera otro periodo entero **sin disparar**—. Cada periodo son RVR+1 ticks exactos, así que el redondeo cae igual en todos: no vuelve a interrumpir. **Es la misma familia que I-41 e I-24**, por otro camino. No se había visto porque el contador solo se rebasaba con `c` en 0 o en RVR+1 —al programarlo—, y esos redondean bien; lo destapa **parar**, y también **reescribir CTRL a medio contar**, que es lo que hacen `HAL_SuspendTick()`/`HAL_ResumeTick()`, sin depurador de por medio. **La corrección** son tres líneas: tras el picosegundo de espera se decide ahí mismo si el tick ha llegado, en vez de volver a empezar la cuenta. **No mueve ningún invariante**: con el arreglo solo, 2118 y `resto` en `2240553274213 ps` al picosegundo, y las otras suites igual. **Lo vigila T131** (5 comprobaciones, sobre el SCS suelto del banco de laboratorio): 48 paradas con 48 valores de `c` distintos, el instante exacto del cruce tras reanudar —medio tick antes no, medio después sí— y 48 parejas suspender/reanudar a medio contar. **Sin el arreglo fallan tres de las cinco**, con 20 de 48 paradas mudas y 20 de 48 reanudaciones mudas. T131 sí mueve la línea del F407: 2128 y `resto` en `2245527784555 ps`, los 4,97 ms de la propia prueba |
| **I-54** | ~~**El aviso de «gdb del PC» saltaba también con `gdb-multiarch`**, que funciona perfectamente~~ | 2026-10-02 | **HECHO.** El aviso que se puso tras el «Truncated register 18» miraba `xmlRegisters=` en el `qSupported` y avisaba si no decía `arm`. Pero GDB no pone ahí la arquitectura para la que está construido, sino las que registran ese soporte, y solo lo hace la de x86: `arm-none-eabi-gdb` no manda nada, el `gdb` del PC manda `i386`, y `gdb-multiarch` —que lleva las dos— **también** `i386`. Comprobado con `gdb` y `gdb-multiarch` 15.1 de Ubuntu: el `qSupported` es idéntico. Lo que los distingue es el paquete `g`: el del PC no lo puede leer y se va —es él quien cierra la conexión, en el acto—; `gdb-multiarch` lo lee y sigue. Ahora el `qSupported` solo toma nota, y el aviso sale al desconectar **si lo último que se pidió fue la `g`**. Lo vigilan cinco comprobaciones nuevas en **T96**, con dos sesiones que imitan a cada uno; como van por el socket, mueven el total del F407 pero no su `resto` |
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
| **P** — Pendientes de plan | 17 *(una cerrada: P-14, con la matriz manual a medias; P-15, hecha su primera parte)* |
| **F** — Funciones no modeladas | 51 |
| **T** — Temporización y física | 23 |
| **D** — Datos sin fuente | 14 |
| **X** — Discrepancias, silencios de [IR] y erratas de ST | 14 |
| **V** — Huecos de verificación | 11 |
| **I** — Deuda de instrumentación y proyecto | 50 *(treinta cerradas: I-11, I-12, I-15, I-16, I-17, I-20, I-22, I-25, I-28, I-30, I-31, I-32, I-33, I-34, I-35, I-36, I-37, I-38, I-39, I-40, I-41, I-42, I-43, I-44, I-45, I-46, I-47, I-48, I-53 e I-54)* |
| **Total** | **180** |

De los 180, **uno solo** (P-01) es un pendiente de plan de primer orden; **once**
son trabajo acotado y barato (bloque 1 y 2 de la sección 10); y **la gran
mayoría** son decisiones conscientes de alcance, cada una con su motivo escrito
en el informe que la originó.

> **Dos correcciones del recuento, dichas en vez de calladas.** Al revisar esta
> tabla para dar de alta lo del F415/F417 se contaron las filas una a una y
> salieron dos errores propios: (1) el **total no sumaba** —la columna daba 162
> y la casilla decía 159—, y (2) la fila de la **I** contaba hasta el
> identificador más alto, no las filas que hay: **I-18 e I-19 no existen**, así
> que son 47 puntos y no 49, y la lista de cerradas nombraba un I-18 inexistente.
> Ahora el total **es la suma de la columna**: 12 + 51 + 22 + 14 + 14 + 11 + 47.
>
> **Y una tercera (2026-10-17, al dar de alta P-17).** P-16 se añadió sin
> contarla: la fila de la **P** decía 15 con dieciséis puntos escritos. Ahora
> son 17 (P-01 a P-17) y el total, 180.
