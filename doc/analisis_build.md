# Análisis de `mcu-sim-build`: placas y sistemas dibujados en vez de escritos

> Rama `build` de `mcu-sim` y de `mcu-sim-gui`. Este documento es el mismo en
> las dos (`doc/analisis_build.md`). **Estado: análisis, con las
> decisiones tomadas** (24-11-2026). No hay código todavía. D1 a D4 (§6) las
> eligió el autor: **D1-A**, **D2-B**, **D3-C** y **D4-A**, con B14 más
> adelante. Lo que de ellas cambia el plan está ya en §3.5, §5.3, §7, §8 y §9.

---

## 0. En una página

**Qué se pide.** Un programa nuevo, `mcu-sim-build`, hecho con el mismo Qt 6
que `mcu-sim-gui` y en su mismo repositorio. Sirve para **dibujar** una placa:
se arrastran piezas de una biblioteca a un lienzo y se unen sus patillas con
el ratón. También sirve para dibujar un sistema: se arrastran placas y se unen
los pines de sus conectores. Al terminar, el programa escribe el XML de la
placa y su SVG, o el XML del sistema, y `mcu-sim` y `mcu-sim-gui` los usan
tal cual. Para que la biblioteca exista, `mcu-sim` tiene que **exportar** lo
que sabe construir: un SVG y un XML de patillas por cada pieza, por cada MCU y
por cada conector con nombre (morpho, Arduino, BoosterPack).

**Lo que lo hace posible.** Casi todo lo que hace falta para el formato ya
existe, y es estable:

* el XML de placa y de sistema (`doc/parts.md` §2);
* el perfil de autor del SVG (`mcu-sim-gui`, `doc/analisis-uso-ilustraciones.md` §11);
* los ids por pieza y por pin de conector;
* las variantes y los rótulos;
* la disposición `x`, `y`, `giro`, `escala`, `lienzo` y `<ruta>`.

Además, `mcu-sim-gui` ya sabe pintar las líneas en curva y en tramos rectos,
editarlas y guardarlas en el XML (plan §37 y §38). Y `mcu-sim --valida`
comprueba una placa sin simularla.

**Lo que no es trivial**, y es donde se va el trabajo (§3):

1. **Una placa de `mcu-sim` no es un esquema: es una lista de nodos.** No hay
   «hilo del pin 3 del LED al pin 21 del chip». Hay un `<pin nombre="anodo"
   nodo="PA5"/>`. El editor dibuja conexiones, y al exportar hay que
   convertirlas en nodos. El camino inverso, para editar una placa que ya
   existe, no tiene dibujo del que partir.
2. **Los SVG que hay son de una pieza.** Están dibujados a mano o generados
   por `placas/dibuja_*.py`, sin el SVG de cada componente por separado. Esas
   62 placas no se pueden «descomponer» automáticamente.
3. **Los MCU no tienen su encapsulado como dato.** `pins/encapsulado.h` guarda
   qué pads salen, pero no por qué patilla ni en qué orden. En los STM32 las
   funciones alternativas se registran con punteros a señales, sin nombre.
   Hace falta una fuente externa: STM32 Open Pin Data para ST y los
   datasheets para TI y Espressif. Hay que contrastarla con el modelo.
4. **Los comentarios de los XML son documentación.** Las placas de `mcu-sim`
   explican en comentarios por qué cada pieza está como está. Una
   herramienta que regenere el XML los borraría.

**Tamaño.** Es el proyecto más grande del lado de la ventana hasta ahora: unas
7 000 a 9 000 líneas de C++ nuevas en `mcu-sim-gui`, que hoy tiene unas 8 400.
A eso se suman los datos generados de unos sesenta MCU y los dibujos de 76
tipos de pieza en `mcu-sim`. El plan de la §9 lo parte en **14 fases, más una para después**, y
cada una deja algo usable. La primera que el usuario ve funcionar (G10) ya
crea una placa nueva y la valida con `mcu-sim`.

**Las decisiones** (§6), en una línea cada una:

* **D1-A**: los pines de los MCU salen de Open Pin Data y de los datasheets,
  y se contrastan con el modelo.
* **D2-B**: lo que solo usa `mcu-sim-build` va en un `<build>` dentro del
  propio XML, que `mcu-sim` acepta, comprueba lo justo y no manda nunca a la
  ventana.
* **D3-C**: en esta versión, `mcu-sim-build` crea placas nuevas y reedita
  **las suyas**. Las 62 placas que ya existen se usan como tarjetas en los
  sistemas, pero no se editan.
* **D4-A**: los perfiles de conector viven en la biblioteca. Enseñárselos a
  `mcu-sim` (B14) queda para después.

La documentación está toda (§10): Open Pin Data para ST, los datasheets de los MSP430, los MSP432 y los ESP32 en `doc/refs/`, y la matriz de ESP-IDF.

---

## 1. Análisis de necesidades

Lo pedido, numerado para poder citarlo:

| | Necesidad | De dónde |
| :--- | :--- | :--- |
| **N1** | Rama `build` en `mcu-sim` y en `mcu-sim-gui` | Enunciado |
| **N2** | `mcu-sim` exporta un SVG por **cada** componente (LED, botón, servo… y también conectores y MCU) y un XML con el **nombre y el uso** de cada pin | §2 del enunciado |
| **N3** | Los pines de un MCU tienen **varios usos**, incluido el mapeo por **PMAP** del MSP432 | §2 |
| **N4** | Conectores **específicos**, con nombre y uso de cada pin, y **combinaciones** de conectores: morpho + Arduino de las Nucleo, BoosterPack de las LaunchPad. El SVG y el XML de un morpho dicen el uso habitual y el pin del MCU además del número | §3 |
| **N5** | Ejecutable nuevo `mcu-sim-build`, con el mismo Qt que `mcu-sim-gui` | §4 |
| **N6** | Se le indican una o más carpetas y construye dos bibliotecas virtuales: **piezas** y **tarjetas** | §4 |
| **N7** | Valorar si la biblioteca **persiste** entre ejecuciones, siempre con la posibilidad de refrescarla | §5 |
| **N8** | Placa nueva: arrastrar piezas al lienzo y **pedir su id** al soltarlas | §6 |
| **N9** | Trasladar, borrar, **girar y escalar** piezas; definir el **marco** (bounding box) de la placa | §6 |
| **N10** | Conexiones entre pines de la misma pieza o de piezas distintas. Ctrl+clic para elegir pines, y un botón para unirlos **en curva** y otro **en tramos rectos**. Cada conexión se puede **ocultar**, volver a mostrar y cambiar de un trazado al otro | §6 |
| **N11** | Los componentes complejos (MCU) tienen **dos SVG**: el detallado, con los pines, para construir, y el simplificado, que va por omisión al SVG de la placa. Se elige uno u otro **por componente** | §7 |
| **N12** | Las conexiones dentro de una placa **nunca** se ven en `mcu-sim-gui`; solo en `mcu-sim-build` | §7 |
| **N13** | Al terminar se escriben el SVG y el XML de la placa, en cualquier carpeta, y `mcu-sim` y `mcu-sim-gui` los usan **tal cual** | §8 |
| **N14** | Sistema nuevo: lo mismo con tarjetas en vez de piezas. Se pide el id de cada tarjeta y se conectan pines de conectores con las mismas herramientas | §9 |
| **N15** | Al terminar se escribe el XML del sistema, utilizable tal cual | §10 |
| **N16** | **Editar** tarjetas y sistemas que ya existen, no solo crearlos | §1 del enunciado: «o edite alguna tarjeta o sistema existente». **Acotada por D3-C**: se editan los sistemas existentes y las placas hechas con `mcu-sim-build`, no las 62 placas de antes |

Y lo que el enunciado no dice, pero sin lo cual lo anterior no funciona:

| | Necesidad implícita | Por qué |
| :--- | :--- | :--- |
| **N17** | Editar los **atributos** de cada pieza (`vf` y `r` de un LED, `tipo` de un MCU, `filas` y `columnas` de un conector…) | Una placa sin parámetros no simula lo que se quiere: el LED azul de 3,3 V no luce con `vf` de omisión |
| **N18** | **Validar** lo que se va a escribir | Un XML que `mcu-sim` rechaza no se usa «tal cual». Lo más fiable es preguntárselo a `mcu-sim --valida` |
| **N19** | **Deshacer y rehacer** | Es un editor gráfico |
| **N20** | Lo que una placa lleva y **no se conecta por patillas**: `<ref>` (el `CanWire` de un `CanTransceiver`), `<transmision>`, `<vista3d>`, la tabla de `<ilustracion>`, `visible="no"`, `conectada="no"` y `contenido=` | Si se pierden al editar, una placa como `fresadora_3020.xml` deja de funcionar |
| **N21** | **Conservar los comentarios** y el orden del XML al editar una placa existente | Son la documentación de cada placa (ver §3.5) |
| **N22** | En un sistema, los **acoples** (`<acopla>`) además de los hilos, y lo que el sistema decide de cada placa (`<mcu ref firmware>`, `<jumper ref>`, `<memoria ref>`) | Un shield sobre una Nucleo es un acople, no 32 hilos |

---

## 2. Lo que ya hay, y en qué se apoya `mcu-sim-build`

### 2.1 En `mcu-sim`

* **`src/placas/`**: 62 placas, 29 sistemas y 47 SVG. Los SVG no están hechos
  con un editor. Unos están escritos a mano y otros salen de cinco guiones:
  `dibuja_st.py`, `dibuja_ti.py`, `dibuja_esp.py`, `dibuja_motores.py` y
  `dibuja_cnc.py`. Los guiones llevan cada uno su propia clase `Svg`, casi
  idéntica en todos.
* **El formato de placa** (`doc/parts.md` §2): `<mcu>`, `<nodo>` (con
  `externo`, `bus` y `une`), `<componente>` con `<pin>` y `<ref>`, `conectada`
  y `visible`. Lo lee `parts/netlist_xml.h`, que **rechaza atributos y
  elementos desconocidos**. Eso pesa en D2.
* **El formato de sistema** (§2.5): `<placa id fichero|nombre>`, `<acopla>`,
  `<hilo>`, `<mcu ref>`, `<memoria ref>`, `<jumper ref>`, y la disposición:
  `x`, `y`, `giro`, `escala`, `lienzo` y `<ruta>`.
* **El dibujo** (§2.6) se busca por nombre y viaja con la placa. Lleva un id
  por pieza viva y un id por pin de conector (`CN7.5`). Las variantes
  `PIEZA@valor` y los rótulos `PIEZA#campo` los resuelve
  `common/svg_variantes.h`, que además gira el SVG (`gira_svg`).
* **La factoría de piezas**: 76 tipos registrados con `REGISTRA_PARTE`. La
  cabecera de `doc/parts.md` todavía dice 69 y está por actualizar. **Cada
  registro lleva su ficha** (`parts/part_help.h`), con resumen, terminales
  (`pin`, con cuándo hacen falta), atributos (con su omisión) y referencias.
  Es la fuente única que `--help TIPO` ya imprime. **Exportarla a XML no
  puede desincronizarse**, aunque cuesta algo más de lo que parece (§4.2).
* **Los MCU**: los 46 STM32 y MSP432 de la tabla de §2.0, más los MSP430 (diez
  dies con placa) y los ESP32-C3/C6. En total, unas sesenta referencias. Lo
  que se sabe de cada pin está repartido:
  * `pins/encapsulado.h`: qué pads salen, como máscara por puerto, sin número
    de patilla;
  * `soc/f4_mapa_af.h` y sus hermanos: `connect_af(puerto, pin, af,
    señales)`, sin nombre de la función. Los MSP432 sí lo llevan: cada
    `FuncionP4` tiene su `nombre`, y las mapeables pasan por
    `conecta_mapeable(PmapP4::PM_…)`. El MSP430F5529 también tiene PMAP en su
    puerto 4, y el ESP32 tiene su **matriz GPIO**, que es el mismo problema en
    grande: casi cualquier señal por casi cualquier pin;
  * los nombres de los pads, en `pins/nomenclatura.h`.
* **`--valida`**, **`--mcus`** y **`--memorias`**: salidas para máquinas que
  `mcu-sim-gui` ya usa. `mcu-sim-build` puede llamar a `--valida` igual.

### 2.2 En `mcu-sim-gui`

* **Qt 6.3 o posterior** (`find_package(Qt6 6.3 … Core Widgets Network Svg
  SvgWidgets)`), con C++17, CMake 3.19 y tres bibliotecas estáticas:
  `transporte`, `sesion` y `pantalla`. Un ejecutable más es otro
  `qt_add_executable` que enlaza lo que necesite.
* **`ilustracion.cpp`** (2 916 líneas): la `QGraphicsScene` en coordenadas de
  `viewBox`. Pinta cada pieza viva como `QGraphicsSvgItem` sobre el renderer
  del SVG entero, y localiza cada pin de conector por su id. Las **líneas**:

  * en curva: `cubicTo`, saliendo por el lado del pin;
  * en tramos rectos, con las esquinas redondeadas (`redondeada`). Doble
    clic, o el menú «En tramos rectos» / «Otra vez en curva», cambia de una a
    otra, y los tramos se arrastran;
  * a un pin **acodado** llegan al borde, y a uno derecho, al centro.

  Es justo lo que pide N10, ya hecho y probado. **Pero está dentro de
  `ilustracion.cpp`**, enredado con la sesión y los observables (D7).
* **`disposicion_xml.cpp`**: escribe en el XML **como texto**. Busca las
  etiquetas que importan y cambia solo sus atributos, así que comentarios,
  espacios y orden quedan como estaban. Es el patrón que pide N21.
* **`generado.cpp`**: el dibujo de una placa sin SVG. Genera conectores con su
  forma de verdad (filas, columnas, numeración, `CN5.1`…) y un glifo por
  pieza. Sirve de punto de partida para los conectores paramétricos.
* **`configuracion.cpp`**: un JSON en `QStandardPaths::AppConfigLocation`,
  con la ruta de `mcu-sim`. La biblioteca persistente de N7 encaja ahí.

---

## 3. Lo que falta, y por qué no es trivial

### 3.1 De las conexiones a los nodos

En `mcu-sim-build` el usuario une **patillas**. En el XML, cada patilla va a
un **nodo**. El paso de una cosa a la otra es un *union-find*: cada conexión
dibujada une dos patillas, y cada clase resultante es un nodo. La regla para
**nombrar** cada nodo es la que haría una persona escribiendo el XML:

| La red contiene | Nodo que se escribe |
| :--- | :--- |
| Un pad de un MCU (`PA5`) | El pad: `nodo="PA5"`, sin `<nodo>`. Con dos o más MCU en la placa, cualificado: `u0.PA5` |
| Dos o más pads del mismo chip | `<nodo id="…" une="PB9 PD3"/>`. Es un **puente de verdad**: el modelo lo trata como un cortocircuito (`doc/parts.md` §2.1), y el programa **lo advierte** antes de escribirlo |
| Una alimentación o un control (`VDD`, `VSS`, `NRST`, `BOOT0`…) | Su nombre |
| Ningún pad, pero el pin de un conector | El pin: `nodo="CN9.8"`, sin nodo nuevo (§4.1 de `parts.md`: un pin sin soldar es su propio nodo) |
| Nada de lo anterior | `<nodo id="n_…" externo="si"/>`. El nombre lo propone el programa a partir de las patillas (`n_LD2_anodo`) y se puede cambiar |
| Varias piezas que conducen | `bus="si"`, si el usuario lo confirma. Si no, `--valida` lo avisa, como hoy |

Una pieza con una patilla **suelta** no escribe ese `<pin>`. Si la patilla es
obligatoria, el programa lo marca en rojo antes de exportar, con la ficha de la
pieza («obligatorio», «al menos uno», «uno de los dos»). Un bus indexado con
huecos (`d0`, `d1`, `d3`) se señala igual: es «el error fácil de este
formato».

El camino inverso, de una placa existente al lienzo, da redes y no hilos. Se
dibujan como un **ratsnest**: una línea de la patilla de cada pieza al pad o al
conector de su nodo, en estrella.

### 3.2 Los SVG de las placas son monolíticos

Un `nucleo_f446re.svg` tiene ids para `LD2`, `B1`, `B2`, `USB`, `VCP` y cada
pin de los seis conectores. **No los tiene** para el ánodo de `LD2` ni para las
patillas del chip, porque el chip es decorado. Lo que se ve de cada pieza es su
caja, no sus patillas. Por eso una placa existente **no se puede convertir**
en «piezas de biblioteca + conexiones» sin redibujarla. D3 decide qué se hace
con ellas.

### 3.3 Los MCU necesitan datos que el modelo no tiene

Para el SVG detallado de un MCU (N11) hace falta, por cada **patilla física**:
su número, su lado del encapsulado, su nombre, su tipo (E/S, alimentación,
reset, arranque) y **todas** sus funciones. Del modelo solo sale lo segundo y
a medias:

| Dato | ¿Lo tiene el modelo? | De dónde sale |
| :--- | :--- | :--- |
| Qué pads salen en cada encapsulado | Sí (`Encapsulado::puerto`) | — |
| Número de patilla de cada pad, y las de alimentación | **No** | STM32: **STM32 Open Pin Data** (`mcu/*.xml`: `<Pin Name="PA5" Position="21" Type="I/O">`), BSD-3-Clause. TI y Espressif: los datasheets |
| Funciones de cada pin, por nombre | Solo en TI (`FuncionP4::nombre`) | STM32: el mismo Open Pin Data, con el número de AF en `mcu/IP/GPIO-…_Modes.xml`. TI: datasheet (tablas de funciones de pines). ESP32: IO MUX y matriz del TRM |
| Qué funciones **simula** el modelo | Sí, de forma implícita (`PinMux::tiene_af`, `conecta_funcion`, `conecta_mapeable`) | Hay que exponerlo: `mcu-sim --exporta-mcu TIPO` |

La distinción entre **función que existe** y **función simulada** es la que
más valor tiene para un alumno. `SPI4_SCK` existe en el F446 y el modelo la
tiene, pero el F446RE no la saca. Un periférico que el chip tiene y el modelo
todavía no simula debe verse en gris, no como si funcionara.

**Lo que dio B3**: el contraste con Open Pin Data encontró **cuatro errores
del modelo**, ya corregidos:
* El **F302** registraba en sus pines las funciones del TIM8, el TIM7 y los
  COMP/OPAMP que no tiene. Ahora `conecta_pines_s3` se salta las de
  `mcu.sin`.
* El **F405/F415** registraba la DCMI, que no tiene. Ahora solo se registra
  con `mcu.perif.dcmi`.
* El **F446** registraba el I2S2ext y el I2S3ext, que no tiene. Ahora solo
  con `mcu.perif.i2sext`.
* **Todos los F4 con I2Sext** ponían el I2S3ext_SD de PC11 en el AF7 en vez
  del AF5. Como la tabla AF es un mapa, **pisaba el USART3_RX de PC11**: en
  el modelo, el USART3 no recibía por ese pin.

Ninguna de las cuatro mueve las suites (`test407`, `test417`, `test446` y
`testf3`, al picosegundo). Lo que la familia tiene y un encapsulado no saca
(el FSMC en un LQFP64) queda como aviso, no como error.

**Lo que dio B6**: el contraste de los conectores con la norma BoosterPack
encontró que la **J2 de la MSP-EXP430FR5969 estaba numerada al revés**. J2.1
era GND y J2.10, P1.3. Las demás LaunchPad, su SVG y la norma (J2.1 es el 11,
abajo) dicen lo contrario: es la figura 15 de SLAU535B. Su comentario lo
contaba bien, pero ponía P3.0 en el 13, cuando es el 18. No afectaba a la
simulación de la placa sola. Pero un `<acopla>` con un BoosterPack de 20
pines habría unido GND con P1.3. Ya está corregida, y también el hilo de
masa de `msp_exp430fr5969_bsl.xml`, que usaba J2.1.

**Lo que dio B4b**: el contraste con los datasheets de TI y Espressif
encontró **dos errores del modelo** que cambian lo que se simula, y varios
nombres mal puestos en su inventario, ya corregidos:
* El **P401R** conectaba TA2CLK en P4.2 con el modo 1, que es el de ACLK.
  El datasheet (SLAS826E, tabla 6-70) dice que es la secundaria: P4SEL1 =
  1, P4SEL0 = 0. Ahora va en el 2.
* La **ESP32-C3FH8X** tenía el encapsulado de 16 GPIO, como la C3FH4X. Pero
  la «X» del nombre es la revisión v1.1, no el encapsulado: la tabla 1-1 del
  datasheet v2.4 le da 22, y solo la C3FH4X (y la C3FH4AZ) deja sin conectar
  las patillas 19-24.
* Del inventario, sin cambio en la simulación: en las FR5/FR6, los bits del
  SR se nombraban por índice siendo máscara (salían «SR»), y las salidas del
  ESI salían «ESI5» en vez de ESIC1OUT. El disparo del Timer_B es TBxOUTH en
  sus datasheets. En el P401R, «A», «C», «VREF» y «cristal» pasan a ser A0-A23,
  C0.0-C1.7, VREF+/VeREF+/C1.7 y LFXIN/LFXOUT/HFXOUT/HFXIN. En el E401Y, «AIN»
  pasa a AIN0-AIN19. Y los ESP32 declaran los canales del SAR ADC que leen.

Las transcripciones las escribe `extrae_ti.py` (con pdfplumber) y
`extrae_esp.py`. Lo que el PDF no deja leer bien se corrige ahí, citando la
tabla: filas perdidas al final de página (F5529), erratas entre las dos
tablas de un datasheet (COMP0_0 en el FR2355, DMA0E/DMAE0 en el FR6989) y
descripciones en vez de nombres («Comparator output», «Mapped secondary
digital function»). Una X en un bit de PxSEL es «cualquiera»: el TB0.0 de
P2.1 del FR5969 se elige con 01 y con 11, y el modelo ya lo hacía así.

**Lo que dio B2** (`--exporta-mcu`, `soc/inventario_pines.h`). Los 65 MCU
se exportan, con tantos pads como dice su encapsulado. Lo que el modelo sabe
decir de sus funciones no es igual en todas las familias, y eso cambia B3 y
B4:

| Familia | Funciones que el modelo inventaría | Consecuencia |
| :--- | :--- | :--- |
| STM32F4, F446 | La tabla AF del `PinMux`: (pad, AF) → señal de SystemC (`usart2.tx_out`), **sin el nombre del manual** | B3 cruza con Open Pin Data **por el número de AF** |
| STM32F3 | La misma, **con el nombre** (`USART2_TX`), porque su tabla ya sale de Open Pin Data | B3 cruza por AF y por nombre |
| MSP432P4 | Las fijas del DIO por PxSEL y las **29 mapeables del PMAP** (`P2 P3 P7`), con su nombre corto (`simo`) y su señal | **Hecho en B4b**: con el datasheet, por PxSEL y nombre; el PMAP, por valor y mnemónico. Los nombres de lo analógico (A0-A23, C0.0-C1.7, los cristales) pasan al modelo |
| MSP432E4 | Las del GPIO por PCTL y las analógicas | **Hecho en B4b**: con la tabla de PCTL del datasheet |
| ESP32-C3/C6 | Solo la función 0 directa del IO_MUX (U0TXD, U0RXD). **La matriz no**: sus señales no están en una tabla del modelo | **Hecho en B4a**: `c3::SENALES` y `c6::SENALES`, con los nombres de ESP-IDF v5.3.1. El modelo habilita con ellas las salidas de la matriz |
| MSP430 (los diez) | **Ninguna**: cada SoC decide la función de un pin en su código (PxSEL, PxSEL2) | **Hecho en B4a**. Los ocho FR2/FR4/FR5/FR6 ya decidían con una tabla (`ChipFr2::fn`), y solo faltaba volcarla. El G2553 (`g2553::FN`) y el F5529 (`f5529::FN` y `f5529::PMAP`) deciden ahora con la suya |

Lo analógico de los STM32 (entradas del ADC, salidas del DAC) tampoco está en
el inventario: va por `bind_analog`, fuera de la tabla AF. B3 lo marca como
simulado si el periférico está en el chip.

**El PMAP (N3)** es una tercera clase: el pin no tiene una lista fija de
funciones, sino **una lista de las mapeables**, que el firmware elige al
arrancar. El XML del MCU lo dice como lista aparte por puerto mapeable
(§4.3), y el editor lo enseña como «P2.0: cualquiera de 24 (PMAP)» con la
lista desplegable. La matriz GPIO de los ESP32 se describe igual, y el F5529
también.

### 3.4 Piezas paramétricas

Un `Conector` de 1×3 y uno de 2×19 son el mismo tipo con dibujos distintos.
Lo mismo pasa con un `Jumper` con otras posiciones de puente o con un `RGBLed`
con otro color. Un SVG fijo por tipo no basta. Estas piezas se dibujan **por
generador**, a partir de sus atributos: es lo que ya hace `generado.cpp` en la
ventana con los conectores. D5 decide si el generador vive en `mcu-sim`
(Python, en guiones `dibuja_*`) o en `mcu-sim-build` (C++).

### 3.5 Los comentarios

`nucleo_f446re.xml` tiene 252 líneas, y más de la mitad son comentarios que
citan el esquema del fabricante, las resistencias de la placa y los puentes
de soldadura. Eso es parte del proyecto, no ruido. Regenerar el XML desde el
modelo del editor (QDomDocument o `QXmlStreamWriter`) lo destruye. Hay dos
casos:

* **Placa nueva**: se genera, con un comentario de cabecera que dice que la
  hizo `mcu-sim-build` y con huecos para documentarla.
* **Placa de `mcu-sim-build`, reabierta** (D3-C deja fuera las de antes): se
  **regenera**, pero conservando dos cosas que la persona haya escrito a
  mano después:
  * el comentario de cabecera;
  * el comentario que haya **justo encima** de cada `<componente>`, `<nodo>`
    y `<mcu>`, que viaja con el elemento por su id.

  Es una regla sencilla y comprobable, y cubre la forma en que están
  documentadas las placas de `src/placas/`. Un comentario en otro sitio (entre
  dos `<pin>`) se pierde, y el programa **lo avisa** antes de guardar, con la
  lista de los que se van a perder.
* **Sistema existente**: se edita **como texto**, como hace
  `disposicion_xml.cpp`, porque los 29 sistemas sí se editan (N16). Se tocan
  solo los `<placa id>`, `<hilo>`, `<acopla>`, `<ruta>` y `<mcu ref>` que
  hayan cambiado, más el `<build>`. Lo demás, intacto.

### 3.6 Lo que no son patillas

`<ref>`, `<transmision>`, `<vista3d>`, `<ilustracion>`, `conectada`,
`visible` y `contenido` no se dibujan. En una placa nueva, las referencias
se editan en el panel de propiedades, con una lista de las piezas
compatibles, y el resto en un apartado «Avanzado». En una placa existente,
**lo que el editor no entiende se conserva tal cual**: es la consecuencia
natural de editar como texto.

---

## 4. Lo que exporta `mcu-sim` (rama `build`)

### 4.1 La carpeta

```
src/biblioteca/
  LEEME.md
  piezas/      <Tipo>.pieza.xml (generado, B1)  <Tipo>.forma.xml  <Tipo>.svg   (76 tipos)
  mcus/        <TIPO>.mcu.xml    <TIPO>.detalle.svg  <TIPO>.simple.svg
  conectores/  <perfil>.conector.xml  <perfil>.svg
               <conjunto>.conjunto.xml                     (§4.4)
```

**Se versiona lo generado**, igual que los SVG de `placas/`. Así
`mcu-sim-build` funciona con un `mcu-sim` sin compilar, y un cambio en una
pieza se ve en el `diff`. Una prueba de la suite comprueba que lo versionado
coincide con lo que se regenera, como ya hace `gui-proto` con las cabeceras
compartidas.

### 4.2 Una pieza: `Led.pieza.xml`

Sale de la ficha de `part_help.h` con `mcu-sim --exporta-piezas DIR`, así que
no puede decir otra cosa que `--help Led`. **Hecho en B1** (el formato real
está en `parts/exporta_piezas.h`; el ejemplo de abajo es el de partida). Lo
único que añade la mano es la `<forma>`: dónde está cada patilla en el dibujo
y hacia dónde sale. Va **en un fichero aparte**, `TIPO.forma.xml`, porque el
`.pieza.xml` se regenera y se compara byte a byte con lo versionado.

**Un matiz que encarece B1.** Las fichas nombran los terminales **para una
persona**, no para una máquina. De sus 196 filas `.pin(...)`, unas cuarenta
agrupan varios terminales en una:

* `anodo o catodo`, en la del `Led`;
* `in1 .. in4`;
* `d0 ... d15`;
* `cs, sck, mosi, miso`;
* `1..N o su nombre`, que depende de `filas` y `columnas`.

Hay dos formas de obtener la lista de terminales:

* **Un analizador de esas cuatro formas**: lista con comas, «o», rango con
  `..` o `...` y rango que depende de un atributo. Va con una prueba que
  **falla** si una fila no se deja analizar, para que una ficha nueva no
  pueda escribirse de otra manera.
* Un campo estructurado nuevo en `Ayuda` (`.terminales("in", 1, 4)`), que
  obliga a tocar unas cuarenta fichas.

Se propone el analizador: la ficha sigue siendo la fuente única y no cambia
lo que imprime `--help`. Los rangos que dependen de un atributo se exportan
como tales (`<terminales prefijo="d" desde="0" hasta="atributo:bits"/>`), y
esa pieza es paramétrica (D5).

```xml
<pieza tipo="Led" version="1">
  <resumen>Diodo con su resistencia en serie...</resumen>
  <terminal nombre="anodo"  cuando="uno de los dos, o los dos">...</terminal>
  <terminal nombre="catodo" cuando="uno de los dos, o los dos">...</terminal>
  <atributo nombre="a_vss" omision="si">...</atributo>
  <atributo nombre="vdd"   omision="3.3">...</atributo>
  <atributo nombre="vf"    omision="2.0">Tension directa del diodo, en voltios...</atributo>
  <atributo nombre="r"     omision="330">...</atributo>
  <forma svg="Led.svg">
    <ancla terminal="anodo"  elemento="$.anodo"  lado="arriba"/>
    <ancla terminal="catodo" elemento="$.catodo" lado="abajo"/>
  </forma>
</pieza>
```

**Como quedó en B5.** La `<forma>` va en `TIPO.forma.xml` y no dentro del
`.pieza.xml`, para que regenerar las fichas no la toque. Se genera junto al
SVG desde `herramientas/dibuja_piezas.py`, así que no es «de mano». Cada
`<ancla>` lleva además su `x`, `y` (0,1 mm) en el dibujo. Las paramétricas
son `Conector`, `Jumper` (`generador="conector"`), `I2cWire` (`bus`) y
`CameraSensor` (`modulo`, por su serie `d0..d(bits-1)`).

**El convenio de ids del SVG de pieza.** La raíz de la pieza es `id="$"`, cada
patilla es `$.<terminal>` y las variantes son `$.cuerpo@azul`. Al componer la
placa, `$` pasa a ser el id que dé el usuario (`LD2`). Así se obtienen,
**sin regla nueva**, los ids que ya esperan `mcu-sim` y la ventana: `LD2`,
`SERVO.cuerpo@azul` y `CN5.6`. Las patillas que no son de conector (`LD2.anodo`)
quedan en el SVG de la placa. La ventana no las mira, porque solo busca
piezas y pines de conector, y a `mcu-sim-build` le sirven para reabrirla.

### 4.3 Un MCU: `STM32F446RE.mcu.xml`

```xml
<mcu tipo="STM32F446RE" encapsulado="LQFP64" familia="STM32F4" die="0x421"
     fuente="STM32_open_pin_data@&lt;commit&gt;" contrastado="si">
  <patilla n="1"  nombre="VBAT"  nodo="VBAT" clase="alimentacion"/>
  <patilla n="21" nombre="PA5"   nodo="PA5"  clase="es" ft="si">
    <funcion nombre="GPIO"        simulada="si"/>
    <funcion nombre="TIM2_CH1"    af="1"  simulada="si"/>
    <funcion nombre="TIM8_CH1N"   af="3"  simulada="si"/>
    <funcion nombre="SPI1_SCK"    af="5"  simulada="si"/>
    <funcion nombre="USB_OTG_HS_ULPI_CK" af="10" simulada="no"/>
    <funcion nombre="ADC12_IN5"   analogica="si" simulada="si"/>
    <funcion nombre="DAC_OUT2"    analogica="si" simulada="si"/>
  </patilla>
  <patilla n="19" nombre="VDD" nodo="VDD" clase="alimentacion"/>
  <patilla n="32" nombre="VDD" nodo="VDD" clase="alimentacion"/>
  ...
</mcu>
```

* El atributo `nodo` es el nombre del XML de placa. Por él se une una
  patilla, y varias patillas pueden tener el mismo (las cuatro `VDD`).
* `simulada` sale de `mcu-sim --exporta-mcu TIPO`, que recorre el `PinMux`
  ya elaborado. **Esta salida es la que contrasta el modelo con el
  datasheet**. Una función que el modelo registra y Open Pin Data no tiene,
  o con otro número de AF, es un error de uno de los dos, y la suite lo
  dice. Es el mismo método que la comparación de tablas AF del plan del
  F415/F417.
* Para el MSP432P401R, las patillas mapeables llevan `pmap="si"` y la lista
  sale una vez, aparte:

  ```xml
  <mapeables puertos="P2 P3 P7">
    <funcion nombre="UCA0TXD" valor="PM_UCA0TXD" simulada="si"/>
    <funcion nombre="TA0.1"   valor="PM_TA0CCR1A" simulada="si"/>
    ...
  </mapeables>
  ```

  Ese mismo esquema sirve para el puerto 4 del MSP430F5529 y para la matriz
  de los ESP32-C3/C6, con `puertos="GPIO0 … GPIO21"`.

**Los dos SVG.** `detalle` es el encapsulado visto desde arriba, con cada
patilla en su sitio, su número y su nombre. **Su id es su posición**: `$.21`,
o `$.A1` en una rejilla de bolas. Es único aunque haya cuatro `VDD`, y no
choca con las variantes (`@`) ni con los rótulos (`#`) del SVG de placa.
El `.mcu.xml` dice qué nodo es cada posición. Este cambio respecto a la
primera versión (`$.PA5`, `$.VDD#19`) se hizo en B3. Se genera **entero** desde el
`.mcu.xml`: QFP y QFN cuadrados con la patilla 1 marcada, y TSSOP de dos
lados. Los BGA (el UFBGA176 de los F407IG) se dibujan como rejilla con la
bola como número (`A1`). `simple` es el cuadrado oscuro con la referencia,
que es lo que ya llevan hoy todas las placas como decorado.

### 4.4 Conectores con nombre y conjuntos (N4)

Hoy un morpho es un `Conector` de 2×19 genérico. Lo que **cada pin significa**
lo dice un comentario del XML de la placa. Un **perfil** lo convierte en dato:

```xml
<!-- conectores/arduino_uno_r3.conector.xml -->
<conector perfil="arduino_uno_r3.cn5" filas="1" columnas="10"
          titulo="Arduino UNO R3: D8..D15">
  <pin n="1"  uso="D8"/>
  <pin n="6"  uso="D13" otros="SPI_SCK"/>
  <pin n="7"  uso="GND" clase="masa"/>
  <pin n="8"  uso="AREF" clase="referencia"/>
  <pin n="9"  uso="D14" otros="I2C_SDA"/>
  <pin n="10" uso="D15" otros="I2C_SCL"/>
</conector>
```

Un **conjunto** es varios conectores con su posición relativa (los cuatro
Arduino y los dos morpho de una Nucleo-64) y **la tabla de cada placa
concreta**: qué pad del MCU va a cada pin. Esa tabla no es del conector:
`CN10.18` es PB11 en otras Nucleo-64 y queda al aire en la F446RE. Por eso
va por placa:

```xml
<!-- conectores/nucleo64.conjunto.xml -->
<conjunto nombre="nucleo64" titulo="Nucleo-64: Arduino UNO R3 + ST morpho">
  <miembro id="CN7"  perfil="st_morpho.izq"  x="0"    y="0"/>
  <miembro id="CN10" perfil="st_morpho.dcha" x="63.5" y="0"/>
  <miembro id="CN5"  perfil="arduino_uno_r3.cn5"  x="..." y="..."/>
  ...
  <cableado mcu="STM32F446RE" fuente="UM1724; placas/nucleo_f446re.xml">
    <pin p="CN10.11" nodo="PA5"/>   <pin p="CN5.6" nodo="PA5"/>
    <pin p="CN6.4"   nodo="VDD"/>   ...
  </cableado>
</conjunto>
```

Al soltar el conjunto `nucleo64` junto a un STM32F446RE, el editor **propone**
las 80 conexiones de su `<cableado>`, y el usuario las acepta o no. El SVG del
perfil lleva al lado de cada pin el rótulo con su uso, en los colores de los
pinouts de ST que ya usa `nucleo_f446re.svg` (azul el pin del chip, rojo la
alimentación, morado el nombre Arduino…). Con un `<cableado>`, el rótulo
lleva también el pad.

La primera tanda, **hecha en B6**: la que ya usan las placas del repositorio.

| Conjunto | Miembros (perfil) | Cableados |
| :--- | :--- | :--- |
| `nucleo64` | CN5, CN6, CN8 y CN9 (los cuatro `arduino_uno_r3.*`); CN7 y CN10 (`st_morpho.cn7`, `.cn10`) | NUCLEO-F446RE; el lado morpho de la X-NUCLEO-IHM07M1 |
| `arduino_uno_r3` | Los cuatro Arduino, sin morpho | CNC Shield V3, `shield_leds`, la parte Arduino de la NUCLEO-F446RE |
| `boosterpack40` | J1 a J4 (`boosterpack.j1` a `.j4`) | MSP-EXP432P401R, MSP-EXP432E401Y, MSP-EXP430F5529LP, FR2355, LP-MSP430FR2476, BOOSTXL-EDUMKII |
| `boosterpack20` | J1 y J2 | MSP-EXP430G2ET, FR2311, FR2433, FR4133, FR5969 |
| `discovery_f3` | P1 y P2 (`EXP1`, `EXP2` en el XML) | STM32F3DISCOVERY |
| `esp32c3_devkitm1`, `esp32c6_devkitc1` | J1 y J3 | Las dos DevKit |

Lo que cambió respecto a lo previsto:
* Los perfiles se nombran por su **función en la norma**
  (`arduino_uno_r3.digital_alto`), no por la referencia de una placa
  (`.cn5`). Un shield los llama J5, J6, J8 y J9, y la Nucleo, CN5 a CN9.
* `discovery_f4` no está: ninguna placa del repositorio tiene las P1/P2 de la
  STM32F4DISCOVERY. `pc104` tampoco: la pila del repositorio es un ejemplo
  sin norma detrás.
* Las tiras propias de una placa (las de la STM32F3DISCOVERY y las DevKit) no
  tienen norma, y su uso es el nodo que les da su XML. El contraste no les
  añade nada, pero así están en la biblioteca para montar sistemas.
* El contraste no es con el `.ioc`, sino con la norma y con `biblioteca/mcus`.
  Un pin de masa del perfil va a una masa, uno de alimentación a una
  alimentación, uno de reset a un reset y uno NC no se usa. Una E/S de una
  placa con MCU va a un pad de ese MCU.
* La geometría de cada conjunto sale del SVG de una placa de referencia, y
  se comprueba que la reconstruida cae en cada pin del dibujo. Los SVG del
  conjunto van **despiezados**: a escala real, los rótulos de dos tiras a
  2,54 mm no caben.

**¿Lo sabe el simulador?** Con D4-A, no: el XML que se escribe lleva un
`Conector` normal, y el perfil solo existe en `mcu-sim-build`. Con D4-B, el
`Conector` gana `perfil=`. `mcu-sim` comprobaría entonces que un `<acopla>`
une dos perfiles compatibles, y `CN5.D13` sería un alias válido de `CN5.6`.

---

## 5. Cómo es `mcu-sim-build`

### 5.1 Las piezas del programa

```
mcu-sim-gui/
  src/trazado.{h,cpp}       las líneas, compartidas con la ilustración (G8, D7)
  src/construccion/         (no `src/build/`: .gitignore ignora todo build/)
    ficha.{h,cpp}           leer .pieza.xml, .forma.xml, .mcu.xml y perfiles;
                            el generador de las paramétricas (sin pantalla)
    biblioteca.{h,cpp}      importar carpetas, el índice, la caché (sin pantalla)
    catalogo.{h,cpp}        fichas y dibujos de cada instancia (sin pantalla)
    documento.{h,cpp}       el modelo: instancias, conexiones, marco, bus
    redes.{h,cpp}           union-find, nombres de nodo (§3.1) (sin pantalla)
    exporta_placa.{h,cpp}   XML y SVG de una placa nueva (sin pantalla)
    abre.{h,cpp}            reabrir una placa con <build> (sin pantalla)
    importa.{h,cpp}         leer una placa cualquiera y reconstruirla en
                            rejilla: la prueba de ida y vuelta (sin pantalla)
    valida.{h,cpp}          QProcess: mcu-sim X.xml --valida, y su informe
    lienzo.{h,cpp}          QGraphicsScene/View; items de pieza, terminal,
                            conexión y marco
    panel_biblioteca.{h,cpp}, panel_propiedades.{h,cpp}
    ventana_build.{h,cpp}, main_build.cpp
  tests/
    prueba_biblioteca.cpp, prueba_redes.cpp, prueba_exporta.cpp (ida y
    vuelta), prueba_build.cpp (la ventana, sin pantalla)
```

Igual que en `mcu-sim-gui`, **lo que no pinta no enlaza `Widgets`**: la
biblioteca `construccion` (Core, Gui y Svg) se prueba sin pantalla, con
`ctest`; la interfaz va en `construccion_ui`.

### 5.2 La ventana

* **A la izquierda**, la biblioteca: dos pestañas, *Piezas* y *Tarjetas*.
  Las piezas se agrupan por clase (pasivas, motores, buses, MCU, conectores),
  y hay un buscador. Cada entrada tiene su miniatura (el SVG) y, al pasar el
  ratón, el resumen de su ficha.
* **En el centro**, el lienzo, en mm, con la rejilla de 2,54 mm que se puede
  ocultar. Al **soltar** una pieza se pide su id, que se comprueba al
  momento:
  * que sea único;
  * que no haga de `ID.k` un pad: la regla de `P1` de `parts.md`;
  * que no lleve `/`.
* **A la derecha**, las propiedades de lo seleccionado. Los atributos salen
  de la ficha, con su omisión en gris; las referencias, con una lista de
  piezas compatibles. En los MCU se elige el **SVG detallado o el
  simplificado** (N11) y se ve la lista de funciones de la patilla bajo el
  ratón, con las simuladas en negro y el resto en gris.
* **Barra de herramientas**:

  | Botón | Qué hace |
  | :--- | :--- |
  | Girar | 90° a la derecha y a la izquierda |
  | Escalar | De 0,25 a 4, el rango de `escala=` |
  | Borrar | Borra la selección |
  | Marco | Define el borde de la placa arrastrando sus esquinas. Por omisión, el que abarca todo más un margen |
  | Unir en curva | Une los pines elegidos en curva |
  | Unir en tramos | Une los pines elegidos en tramos rectos |
  | Ocultar / Mostrar | Oculta o muestra las conexiones seleccionadas o todas |
  | Curva ↔ tramos | Cambia el trazado de las conexiones seleccionadas |
  | Validar | Llama a `mcu-sim --valida` |
  | Exportar | Escribe los ficheros |
  | Deshacer / Rehacer | Con `QUndoStack` |

* **Ctrl+clic en un pin** lo añade a la selección de pines. Los pines
  seleccionados se marcan y llevan un número de orden. **Con dos pines**,
  «Unir» los une. **Con más de dos**, los une **en estrella** desde el
  primero: son el mismo nodo de todas formas, y cada línea se puede mover
  luego por separado. Un pin con el ratón encima dice su nombre y, en un MCU,
  sus funciones.

### 5.3 Qué se escribe

**Placa nueva** (N13):

* `placa.xml` con:
  * `<mcu>`;
  * los `<nodo>` necesarios;
  * un `<componente>` por instancia, con sus atributos **distintos de la
    omisión** y sus `<pin>`.
* `placa.svg`, que se compone así:
  * el marco como rectángulo de la placa;
  * cada instancia como `<g id="LD2" transform="translate(..) rotate(..)
    scale(..)">` con el contenido de su SVG y los ids `$` sustituidos;
  * los MCU con su SVG simplificado o el detallado, según se haya elegido;
  * **sin las conexiones internas** (N12);
  * la cabecera de comentario del perfil de autor, como las de
    `placas/*.svg`.

  Todo en unidades de 0,1 mm y en SVG 1.2 Tiny, sin nada de Qt 6.7.
* El **`<build>`**, como último hijo de la `<placa>` (D2-B, §7): dónde está
  cada pieza, las conexiones, su trazado y su visibilidad, y la elección de
  SVG detallado o simplificado.
* Y antes de escribir, `--valida` sobre una copia temporal. Si falla, se
  enseña el mensaje y **no se escribe** salvo que se insista.

**Sistema nuevo** (N15):

* las `<placa id fichero x y giro escala>`;
* un `<hilo>` por conexión entre pines;
* un **`<acopla>`** cuando el usuario une dos conectores enteros (botón
  «Acoplar», con «espejo» si hace falta), en vez de N hilos;
* `<ruta>` para las líneas en tramos rectos, exactamente como hoy las escribe
  la ventana;
* `<mcu ref firmware>` si se ha dicho.

La **visibilidad** de las líneas de un sistema es cosa de la ventana, que la
guarda en su configuración. `mcu-sim-build` la guarda en el `<build>` del
sistema, que tampoco llega a la ventana. El trazado no va en el `<build>`:
ya lo dicen las `<ruta>`, y así lo que se dibuja en `mcu-sim-build` se ve
igual en `mcu-sim-gui`.

**Rutas de los ficheros.** El `fichero=` de un sistema es relativo a la
carpeta del sistema. Si el sistema se guarda en otra carpeta que sus placas,
se escribe la ruta relativa que haga falta (`../placas/nucleo_f446re.xml`).
Si es otra unidad de Windows y no hay ruta relativa posible, se ofrece
**copiar** la placa y su SVG junto al sistema.

---

## 6. Decisiones de diseño

Las cuatro primeras cambian lo que se construye y cuánto cuesta, y las elige el
autor. Las demás llevan una propuesta que se aplica si no se dice otra cosa.

### D1 — De dónde salen los pines de los MCU · **elegida: A**

| | Opción | A favor | En contra |
| :--- | :--- | :--- | :--- |
| **A** | **Fuentes externas generadas por guion y contrastadas con el modelo**: STM32 Open Pin Data para ST, tablas de los datasheets para TI y Espressif. `mcu-sim --exporta-mcu` marca qué funciones simula, y la suite compara las dos | Datos completos (también lo que no se simula). Para ST es automático. El contraste destapa errores en los dos lados | Los datasheets de TI y Espressif no son legibles por máquina: transcribir sus tablas lleva días por familia |
| B | Solo el modelo: dar nombre a cada `connect_af` de los STM32 y añadir a mano la tabla de patillas físicas de cada encapsulado | Una sola fuente | Tocar decenas de ficheros de `soc/` para un dato que no simula nada. La tabla física hay que transcribirla igual. Las funciones no simuladas no se verían |
| C | Solo fuentes externas, sin contraste | Lo más rápido | El editor diría «esta patilla es SPI1_SCK» aunque el modelo no lo simule |

**Recomendada: A.** Es la que ya usa el proyecto con las tablas AF del
F415/F417 contra CubeMX.

### D2 — Dónde se guarda lo que solo ve `mcu-sim-build` · **elegida: B**

Es la posición de cada pieza, las conexiones dibujadas, su trazado y si se
ven, la elección de SVG simplificado o detallado y la visibilidad de las
líneas de un sistema.

| | Opción | A favor | En contra |
| :--- | :--- | :--- | :--- |
| **A** | **Un fichero gemelo**: `placa.build.xml` junto a `placa.xml`, y `sistema.build.xml` | `mcu-sim` no cambia. Se borra el gemelo y la placa sigue igual. Se puede no versionar | Son dos ficheros que pueden separarse. Si se edita la placa a mano, el gemelo queda viejo (se detecta: lleva el hash del XML que describe) |
| B | Un elemento `<build>` dentro del XML, que `mcu-sim` acepta e ignora | Un solo fichero | Cambia el parser de `mcu-sim` (que hoy rechaza lo que no conoce, a propósito) y **ensucia** las placas con geometría que no simula |
| C | Una capa oculta dentro del SVG, que `mcu-sim` quita antes de mandarlo, como las variantes | Viaja con el dibujo, que es lo que describe | El SVG pasa de dibujo a base de datos, y un editor de SVG (Inkscape) la estropea sin avisar |

Se recomendaba A. **Se eligió B**: un solo fichero, que no puede separarse de
su placa ni quedar viejo. Lo que eso cuesta y cómo se garantiza N12 está en
§7 y en la fase **B7**:

* `parts/netlist_xml.h` admite **un** `<build>` como hijo de la raíz `<placa>`
  o `<sistema>`. No lo admite en una `<placa>` escrita dentro de un sistema:
  esa la edita el sistema.
* Valida lo justo para que una errata no pase en silencio. Su `version`, y
  que cada `<instancia id>` y cada extremo de `<conexion>` nombre algo que
  la placa tiene. Una referencia rota es un **aviso**, no un error: la
  placa simula igual, y quien la arregla es `mcu-sim-build`.
* **No va en `T_PLACA` ni en el SVG**: N12 depende de esto, y una prueba de
  `mcu-sim` lo comprueba con una placa que lo lleva.
* `disposicion_xml.cpp` («Guardar en el XML» de la ventana) **no lo toca**.
  Ya edita como texto solo lo suyo; una prueba nueva de `mcu-sim-gui` lo
  asegura.

### D3 — Qué se hace con las placas que ya existen · **elegida: C**

Son 62, con SVG de una pieza (§3.2). N16 pide editarlas.

| | Opción | Qué se puede hacer con una existente | Coste |
| :--- | :--- | :--- | :--- |
| **A** | **Abrirlas «sobre fondo»**: su SVG queda fijo, como un fondo con sus piezas vivas localizadas por id (como la ventana). Encima se añaden piezas de la biblioteca y conexiones. Las conexiones que ya tiene salen del XML como ratsnest (§3.1), y el XML se edita como texto (§3.5) | Añadir, quitar y reconectar piezas y cambiar atributos. No mover lo que está dibujado en el fondo | Medio |
| B | **Redibujarlas** con piezas de biblioteca: migración, placa a placa, de las 62, con su `<build>` cada una | Todo, como una nueva | Alto. Los guiones `dibuja_*.py` tienen detalle (serigrafía, rótulos, zonas) que la biblioteca no reproduce y habría que perder o rehacer |
| C | Solo placas nuevas en la primera versión. Las existentes solo se usan como tarjetas en sistemas | Nada, hasta otra versión | Ninguno ahora |

Se recomendaba A. **Se eligió C**: la primera versión no abre para editar
ninguna de las 62 placas existentes. Las consecuencias:

* **Sale del plan** la edición «sobre fondo» y la edición como texto de
  placas, que era la parte de más riesgo (§8). La fase que era la edición
  de placas existentes pasa a ser **reabrir las placas propias** (G11, §9).
* Las 62 placas **sí** están en la biblioteca de *Tarjetas* para montar
  sistemas: un sistema solo necesita su dibujo, sus conectores y sus ids, y
  eso ya lo tienen.
* Abrir una placa sin `<build>` como placa, para editarla, da un mensaje que
  dice por qué no se puede y ofrece abrirla **como sistema de una tarjeta**.
* A y B siguen siendo posibles más adelante sin cambiar ningún formato.

### D4 — Los conectores con nombre: solo en la biblioteca, o también en el simulador · **elegida: A, con B después**

| | Opción | A favor | En contra |
| :--- | :--- | :--- | :--- |
| **A** | **Solo en `mcu-sim-build`**: el perfil propone conexiones y dibuja rótulos, y el XML lleva un `Conector` genérico | `mcu-sim` no cambia. Las placas existentes son válidas sin tocarlas | El simulador no puede comprobar que un shield se acopla a un conector compatible |
| B | `perfil=` en `Conector` y en `Jumper`, que `mcu-sim` valida | `<acopla>` comprueba la compatibilidad, `CN5.D13` es un alias válido y `--help` lista los perfiles | Una fase más en `mcu-sim`. Hay que decidir qué pasa con las placas existentes (¿se les pone `perfil=`?) |

**Elegida: A ahora, con B como fase B14, después.** A es un subconjunto de
B: los perfiles son los mismos ficheros, y B solo los enseña también a
`mcu-sim`. Para que B14 no obligue a rehacer nada, el `<build>` de una placa
ya guarda el perfil de cada instancia de conector (`fuente=` en §7).

### D5 — Quién dibuja las piezas paramétricas · *propuesta*

`Conector`, `Jumper`, las barras de LED y el detalle de los MCU dependen de sus
atributos. **Propuesta: los genera `mcu-sim-build`, en C++.** Ya lo hace
`generado.cpp` con los conectores, y con los atributos cambiando en el panel
el dibujo tiene que rehacerse al momento, sin llamar a Python. `mcu-sim`
exporta para esas piezas un `.pieza.xml` con `<forma generador="conector"/>`
en vez de un SVG. Para que un generador no difiera del otro, los SVG de
`detalle` de los MCU sí se generan en `mcu-sim`, con un guion: son estáticos
por referencia.

### D6 — La biblioteca persistente (N7) · *propuesta*

**Sí, persistente**, porque importar una carpeta supone leer ~200 XML y ~150
SVG y montar sus miniaturas. Con la propuesta:

* las carpetas se guardan en la configuración (el mismo JSON de
  `AppConfigLocation`);
* el **índice** va en `AppDataLocation/biblioteca.json`: tipo, ruta, mtime,
  tamaño y hash de cada fichero, más el resumen;
* las miniaturas van a una caché de PNG.

Al arrancar se comprueban los mtime, en milisegundos, y solo se relee lo que
cambió. «Refrescar» fuerza la relectura completa. «Carpetas…» añade o quita
carpetas. Una entrada cuyo fichero ha desaparecido se marca y no se borra
hasta refrescar. Si dos carpetas tienen un fichero con el mismo nombre de
tarjeta, aparecen las dos, con su carpeta.

### D7 — Las líneas: compartir el código con `mcu-sim-gui` · *propuesta*

N10 pide «al estilo de `mcu-sim-gui`». **Propuesta: sacar de
`ilustracion.cpp` a una biblioteca estática `trazado`** lo que es geometría:

* `redondeada`;
* el cálculo de tramos y codos del plan §37;
* el `cubicTo` con salida por el lado del pin;
* el anclaje a pin acodado o derecho;
* el arrastre de tramos.

La enlazan los dos ejecutables. Es una refactorización de `mcu-sim-gui` sin
cambio de comportamiento, y la vigilan sus pruebas actuales
(`prueba_ilustracion`, `prueba_ventana`), que deben seguir en verde sin
tocarlas. Copiar el código sería más rápido hoy, y daría dos copias que
divergen mañana.

### D8 — Girar y escalar piezas · *propuesta*

En una placa, **giros de 90°**. Las tiras de pines van a 2,54 mm, y un giro
arbitrario deja patillas fuera de la rejilla. Además, `mcu-sim` solo admite
90, 180 y 270 en el `giro=` de una placa, y conviene que las dos cosas se
parezcan. La escala de cada pieza va **de 0,25 a 4**, como `escala=`, aunque
en una placa lo normal es 1: una pieza tiene su tamaño de verdad, en mm. Si se
quiere giro libre, es una casilla y no cambia formatos.

### D9 — Dónde vive `mcu-sim-build` · *propuesta*

Un segundo `qt_add_executable` en el **mismo** `CMakeLists.txt` de
`mcu-sim-gui`, con la opción `MCU_SIM_BUILD` (ON por omisión), y en los
**mismos paquetes** de las tres plataformas. No se crea un repositorio nuevo:
el enunciado lo pone en la rama `build` de `mcu-sim-gui`, y comparte Qt,
empaquetado e integración continua.

---

## 7. El elemento `<build>` (D2-B)

En una placa, como **último hijo** de la raíz:

```xml
<placa nombre="mi-placa">
  <mcu tipo="STM32F446RE" id="u0"/>
  <componente tipo="Led" id="LD2" vf="3.0">
    <pin nombre="anodo" nodo="PA5"/>
  </componente>
  <componente tipo="Conector" id="CN5" filas="1" columnas="10">
    <pin nombre="6" nodo="PA5"/>
  </componente>

  <!-- Lo que sigue solo lo usa mcu-sim-build: mcu-sim lo comprueba y no lo
       manda nunca a la ventana -->
  <build version="1" programa="mcu-sim-build 0.1.0">
    <marco x="0" y="0" ancho="70" alto="45"/>                      <!-- mm -->
    <instancia id="u0"  fuente="mcus/STM32F446RE" x="20" y="10" dibujo="simple"/>
    <instancia id="LD2" fuente="piezas/Led" x="5" y="30" giro="90"/>
    <instancia id="CN5" fuente="conectores/arduino_uno_r3.digital_alto" x="40" y="2"/>
    <conexion a="LD2.anodo" b="u0.PA5" trazado="curva"/>
    <conexion a="CN5.6" b="u0.PA5" trazado="tramos" eje="h" codos="31 12" visible="no"/>
  </build>
</placa>
```

* `x`, `y`, `giro` y `escala` de una `<instancia>` son los de la pieza **en la
  placa**, en mm. No se confunden con los `x`, `y`, `giro` y `escala` de una
  `<placa id>` en un sistema, que dicen dónde va la placa entera.
* `fuente` es la entrada de la biblioteca de la que salió, **relativa a la
  carpeta de la biblioteca** y sin extensión. Una fuente que ya no está en la
  biblioteca no impide abrir la placa: la pieza se dibuja con el glifo
  genérico y se avisa.
* `trazado="tramos"`, con `eje` y `codos`, es **la misma semántica que la
  `<ruta>`** de los sistemas. Así el código de D7 sirve para las dos.
* `visible` falta cuando es `si`.

En un sistema, el `<build>` solo lleva lo que la `<ruta>` y las `<placa id>`
no dicen ya, que es la **visibilidad** de cada línea:

```xml
<build version="1">
  <linea linea="hilo S/P1.PWM N/CN9.6" visible="no"/>
</build>
```

**Una placa hecha con `mcu-sim-build` y editada a mano después.** Un
componente sin `<instancia>` se coloca en una **bandeja** al abrirla, y sus
nodos se dibujan como ratsnest (§3.1). Una `<instancia>` sin componente se
descarta con un aviso. Nada de esto impide abrirla.

---

## 8. Complejidad

| Bloque | Tamaño | Riesgo | Por qué |
| :--- | :--- | :--- | :--- |
| Exportar las fichas (`--exporta-piezas`) | S–M | Bajo | Recorrer la factoría y escribir XML, más el analizador de las ~40 filas de terminales agrupados (§4.2) |
| `--exporta-mcu` (lo simulado) | M | Medio | Hay que recorrer el `PinMux` de cada familia: STM32 por AF, TI por `conecta_funcion` y PMAP, ESP32 por matriz. Son tres implementaciones |
| Datos de MCU de ST | M | Bajo | Guion sobre Open Pin Data, ya usado por el proyecto |
| Datos de MCU de TI y Espressif | L | **Alto** | Transcripción de datasheets. Son diez MSP430, dos MSP432 y dos ESP32. Es la parte más lenta y más propensa a erratas |
| SVG de 76 tipos de pieza | L | Medio | Mucho dibujo. Unos 30 se pueden sacar de los SVG de placa que ya los tienen (servo, TFT, KY-040, LED, pulsador…). El resto, nuevos. Los paramétricos no necesitan SVG (D5) |
| Perfiles y conjuntos de conectores | M | Bajo | Pocos, y los `<cableado>` salen de las placas existentes |
| `trazado` (D7) | M | Medio | Refactorizar 2 900 líneas sin cambiar nada |
| Biblioteca persistente | M | Bajo | — |
| Lienzo, piezas y conexiones | L | Medio | El núcleo interactivo, con deshacer |
| Redes → nodos y exportar una placa nueva | M | Medio | Reglas de §3.1. Se pueden probar sin pantalla contra las 62 placas |
| `<build>` en `mcu-sim` (D2-B) | S | Bajo | El parser lo acepta, lo comprueba y no lo manda |
| Reabrir placas propias y conservar comentarios (§3.5) | M | Medio | La regla del «comentario de encima» es sencilla. El riesgo es lo que alguien escriba a mano fuera de ella, y por eso se avisa |
| Editar sistemas existentes como texto (N21) | M | Medio | Se reutiliza el patrón de `disposicion_xml.cpp`. Con D3-C ya no hay que hacerlo con placas, que era lo difícil |
| Sistemas | M | Bajo | La ventana ya hace la mitad (disposición y rutas) |
| Empaquetado, CI y guía | M | Bajo | Ya existe para `mcu-sim-gui` |

S: días. M: una o dos semanas. L: tres o más.

---

## 9. Plan por fases

Cada fase termina en un commit (o una serie) de la rama `build`, con sus
pruebas en verde, y deja algo que se puede usar o comprobar. **B** son las de
`mcu-sim` y **G** las de `mcu-sim-gui`. La numeración es única para que el
orden sea claro.

| Fase | Repo | Qué | Entregable | Cómo se comprueba |
| :--- | :--- | :--- | :--- | :--- |
| **B0** | los dos | Este análisis y las ramas `build` | `doc/analisis_build.md` en las dos | — |
| **B1** ✔ | mcu-sim | `--exporta-piezas DIR`: un `.pieza.xml` por tipo, desde la ficha, con el analizador de terminales agrupados | `src/biblioteca/piezas/*.pieza.xml` (sin `<forma>`) | Prueba nueva: los 76 tipos están, **toda** fila de terminales se analiza, y cada terminal y atributo de la ficha está. Lo versionado coincide con lo regenerado |
| **B2** ✔ | mcu-sim | `--exporta-mcu TIPO`: patillas soldadas y funciones **simuladas** | La salida, en XML | Pruebas por familia: el recuento de pads coincide con `Encapsulado::n_gpio` (lo mismo que T128) |
| **B3** ✔ | mcu-sim | Datos de ST: `herramientas/exporta_mcus_st.py` sobre Open Pin Data (commit fijado) + B2 | `mcus/STM32*.mcu.xml` y `*.detalle.svg`/`*.simple.svg` de los 46 | **Contraste**: toda función simulada existe en Open Pin Data con el mismo AF, y todo pad soldado tiene patilla |
| **B4a** ✔ | mcu-sim | La tabla de funciones de cada modelo MSP430 y la de la matriz de los ESP32, **usadas por el modelo** (decisión del autor, 24-11-2026) | Las tablas, y `--exporta-mcu` sin ninguna familia «sin inventario» | Las suites del G2553, el F5529, los C3/C6, `test430` y `testfr2`, al picosegundo |
| **B4b** ✔ | mcu-sim | Las fuentes de TI y Espressif: el **datasheet** de cada chip (tablas de patillas y «Pin Functions», con el número que elige cada función), el PMAP y la matriz de ESP-IDF. Contraste **estricto**: pad, número y nombre (decisión del autor, 24-11-2026: «tan estricto como el propio hardware»; SysConfig no es obligatorio en un programa y su `mode` no es el PxSEL) | `fuentes/ti/*.pines`, `fuentes/esp/*.pines` y `gpio_sig_map.h`; los `.mcu.xml` y SVG de los 21 | `genera_mcus.py`: los 65 MCU cuadran. Las suites de las familias tocadas, al picosegundo |
| **B5** ✔ | mcu-sim | SVG de las piezas no paramétricas y `<forma>` de todas, de plantillas (`herramientas/dibuja_piezas.py`) | `piezas/*.svg` (72) y `piezas/*.forma.xml` (76: cuatro son paramétricas, con `generador=`) | `comprueba_piezas.py`: las anclas son exactamente los terminales de la ficha, cada una existe en su SVG, y el SVG cumple el perfil de autor (sin filtros ni máscaras, con viewBox y mm). Lo versionado es lo regenerado |
| **B6** ✔ | mcu-sim | Perfiles y conjuntos de conectores, con los `<cableado>` extraídos de las placas (`herramientas/genera_conectores.py`) | `conectores/*`: 16 perfiles, 7 conjuntos, 21 cableados | El contraste con la norma y con `biblioteca/mcus` (§4.4), la geometría contra el SVG de la placa y lo versionado contra lo regenerado |
| **B7** ✔ | mcu-sim | **`<build>` en placas y sistemas** (D2-B, §7): se admite, se comprueba y no sale en `T_PLACA` ni en el SVG. `doc/parts.md` §2 lo documenta | El parser, con su prueba (T134) y la de `mcu-sim-gui` | Una placa con `<build>` valida y simula igual que sin él. `T_PLACA` no lo lleva. Un `<build>` repetido, mal colocado o con `version` desconocida es un error. Una `<instancia>` que no está es un aviso. «Guardar en el XML» de la ventana lo deja intacto |
| **G8** ✔ | gui | Biblioteca `trazado` (D7), sin cambio de comportamiento | `src/trazado.{h,cpp}`: enganches, curva, tramos, codos, esquinas redondeadas; la ilustración la usa | Todas las pruebas actuales de `mcu-sim-gui`, sin tocar: `prueba_ilustracion` 353 y `prueba_ventana` 83, las mismas |
| **G9** ✔ | gui | `mcu-sim-build`: esqueleto y biblioteca persistente (D6) | El ejecutable abre, importa carpetas (la primera vez, las de mcu-sim si hay `MCU_SIM_SRC` o se conoce el ejecutable) y enseña piezas y tarjetas con su miniatura -cacheada en PNG- y su resumen | `prueba_biblioteca` (22): importar, el índice que se reutiliza sin releer, un fichero cambiado que se relee solo, uno borrado que se marca y el refresco completo que lo quita, las miniaturas de la caché |
| **G10** ✔ | gui | **Placa nueva**: lienzo, colocar, id, mover, girar, escalar, borrar, marco, conexiones (Ctrl+clic, curva, tramos, ocultar, alternar), propiedades, deshacer. Redes → nodos, exportar XML+SVG con su `<build>`, validar | **Primera versión usable**: una placa nueva de principio a fin, y se reabre (el `<build>`; sin los comentarios, que son de G11) | `prueba_redes` (22), `prueba_build` (39: la ventana sin pantalla, con Ctrl+clic de verdad) y la **prueba de ida y vuelta** (`prueba_exporta`): **49 placas** del repositorio se reconstruyen desde la biblioteca, se exportan, dan el mismo netlist que la original -comparado sobre el XML: las mismas redes con los mismos terminales, sin `--netlist`-, `mcu-sim --valida` las acepta y reabiertas dan el mismo XML. Las 17 que no se reconstruyen llevan lo que aún no se dibuja (`<ref>`, `<transmision>`, `<ilustracion>`, varios MCU) y se dicen. `ky040` es de ellas (lleva `<ilustracion>`); van `pololu_a4988` y `nucleo_f446re` en su lugar |
| **G11** | gui | **Reabrir placas propias** (D3-C): leer el `<build>` (hecho en G10, `abre.cpp`), regenerar conservando los comentarios (§3.5), la bandeja de los componentes añadidos a mano | Editar una placa hecha con `mcu-sim-build` | `prueba_reabre`: abrir y guardar sin cambios da los mismos bytes. Un comentario de encima de un componente viaja con él. Uno fuera de la regla se anuncia. Un componente añadido a mano aparece en la bandeja |
| **G12** | gui | **Sistemas**: nuevos y existentes, hilos, acoples, rutas, `<mcu ref>` y rutas relativas | Sistemas de principio a fin | Los 29 sistemas: abrir y guardar sin cambios da los mismos bytes. Rehacer `nucleo_f446re_servo.xml` en el lienzo da el mismo netlist. «Guardar en el XML» de `mcu-sim-gui` deja intacto el `<build>` |
| **G13** | gui | Empaquetado (Linux, Windows y macOS), CI, `doc/guia de usuario.md` y notas de versión | `mcu-sim-build` en los paquetes de la Release | CI de las tres plataformas, con y sin la vista 3D |
| **B14** | mcu-sim | *(Después, D4-B)* `perfil=` en `Conector` y `Jumper`, acoples compatibles y alias por uso | — | Suite de `mcu-sim` |

**Lo que encontró la ida y vuelta de G10.** (1) `cripto_f417` unía un
`osc_out` del `Crystal`, que no tiene ese terminal: `mcu-sim` pasa por alto
un `<pin>` con un nombre que la pieza no tiene, **sin avisar** (también con
`--valida`). Se quitó de la placa; que el parser lo diga es una mejora
pendiente de `mcu-sim`. (2) En los ESP32, `mcu-sim` acepta el nombre de la
función como alias del nodo (`SPICS0` por `GPIO14`, `c3_desc.h`); el
`.mcu.xml` no lo dice aparte, pero está en el nombre de la patilla
(`SPICS0/GPIO14`), y de ahí lo lee `mcu-sim-build`.

**Orden y paralelismo.** B1 a B7, G8 y G9 no dependen entre sí y se
pueden hacer en cualquier orden. G10 necesita B1, B5, **B7** y G8, pero **no** los
MCU (B3 y B4): sus pruebas de ida y vuelta empiezan con placas sin MCU. Un
MCU sin `.mcu.xml` se coloca con un glifo genérico y sus pads se escriben a
mano en el pin. B3 entra en cuanto esté y B4 al final, porque es la que más
tarda. Así **la primera versión usable no espera a la transcripción de los
datasheets.**

---

## 10. Documentación técnica

Revisada el 24-11-2026, con acceso a `doc/refs/` de `mcu-sim` (sin versionar) y
a `C:\ti`. **No falta nada que bloquee:**

| Para | Fuente | Dónde | Cómo se usa |
| :--- | :--- | :--- | :--- |
| B3 (ST) | **STM32 Open Pin Data**, BSD-3-Clause: `mcu/*.xml` (patillas con `Position`, `Type` y `Signal`), `mcu/IP/GPIO-*_Modes.xml` (número de AF) y `boards/*.ioc` | GitHub, con un commit fijado | Fuente principal, por guion |
| B3 (ST) | Descriptores de CubeMX | `doc/refs/cubemx/` | Contraste: deben coincidir con Open Pin Data en las F4 |
| B4 (MSP432P401R) | SysConfig 1.8 | `C:\ti\sysconfig_1_8_0` | **No se usa**: su `mode` es un índice de SysConfig, no el PxSEL del chip, y mezcla funciones de software (el chip select de SDSPI en todos los pines). Lo vio B4 |
| B4 (MSP432P401R) | **SLAS826E**, el datasheet: tabla 4-1 (*Pin Attributes*), 6-44 (PMAP) y 6-62 a 6-84 (*Pin Functions*) | `doc/refs/msp432/slas826e.pdf` | **Transcripción** (`extrae_ti.py`) |
| B4 (MSP432E401Y) | **SLASEN5**: tabla 4-2 (*Pin Attributes*) y 4-4 (*GPIO Pins and Alternate Functions*, con el número de PCTL) | `doc/refs/msp432/slasen5.pdf` | **Transcripción** |
| B4 (MSP430) | Los diez datasheets: SLAS735J (G2553), SLAS590P (F5529, con su PMAP del P4), SLASE58E (FR2311), SLASEC4D (FR2355), SLASE59F (FR2433), `msp430fr2476.pdf` (FR2476, SLASEO7C), SLAS865F (FR4133), `msp430fr5969.pdf` (FR5969, SLAS704G), SLASE54D (FR5994) y SLAS789D (FR6989) | `doc/refs/msp430/<familia>/` | **Transcripción** de las tablas de patillas y de las *Pin Functions* de cada puerto. Ni SysConfig 1.8 ni `targetdb` de CCS 11.2 traen el patillaje de los MSP430 |
| B4 (ESP32) | Datasheets del ESP32-C3 (v2.4) y del ESP32-C6 (v1.1): la tabla *Pin Overview* del apéndice, con las funciones del IO MUX y su número | `doc/refs/esp32-c3/`, `doc/refs/esp32-c6/` | **Transcripción** (`extrae_esp.py`) |
| B4 (ESP32) | ESP-IDF v5.3.1, Apache-2.0: `components/soc/<chip>/include/soc/gpio_sig_map.h` | `biblioteca/fuentes/esp/` | **La matriz, legible por máquina**: el índice de cada señal |
| B6 | UM1724 (Nucleo-64); SLAU597F, SLAU748B y SLAU599B (LaunchPad MSP432 y EDUMKII); las guías de las diez LaunchPad MSP430 | `doc/refs/nucleo64/`, `doc/refs/msp432/`, `doc/refs/msp430/launchpads/` | Los `<cableado>` salen de los XML de las placas. Estas guías son el contraste |
| B6 | El estándar de patillaje BoosterPack de TI | ti.com | Solo para los **usos habituales** de J1 a J4. Las guías de las LaunchPad ya traen la tabla |

---

## 11. Riesgos y lo que queda fuera

* **Lo que queda fuera de la primera versión**:
  * editar el **dibujo** de una pieza: los SVG de biblioteca se hacen con
    los guiones o a mano, como hoy;
  * `<vista3d>`: se conserva, pero no se edita;
  * las piezas sin patillas (`Postura`, `Cotas`): se colocan y se refieren,
    pero no se dibujan conexiones con ellas;
  * fusionar dos placas en una;
  * **editar las 62 placas que ya existen** (D3-C). Se usan como tarjetas
    en los sistemas. Abrirlas «sobre fondo» (D3-A) o redibujarlas (D3-B)
    queda como ampliación, sin cambiar formatos.
* **Las variantes del SVG**: el color de un servo o la posición de un jumper.
  En el lienzo se enseña la del atributo actual, con el mismo algoritmo que
  `svg_variantes.h`, reescrito en C++/Qt o compartido si se separa en una
  cabecera sin SystemC.
* **Qt 6.4 en Ubuntu 24.04**: el perfil de autor ya lo tiene en cuenta (SVG
  Tiny, sin filtros). El arrastrar y soltar, `QUndoStack` y
  `QGraphicsSvgItem` existen desde mucho antes.
* **Un `.mcu.xml` con erratas** engaña al alumno más que no tenerlo. El
  contraste con el modelo (B2–B4) y la marca `contrastado="si"`, solo cuando
  ha pasado, son la defensa.
* **Una placa propia editada a mano** (D2-B): el `<build>` puede quedar
  corto o sobrar. §7 dice qué se hace con cada caso. Nunca impide abrirla.
* **Un `<build>` que se cuela en la ventana** rompería N12. Lo impiden B7,
  con su prueba sobre `T_PLACA`, y que las conexiones internas nunca se
  escriban en el SVG.

---

## 12. Resumen de las decisiones

| | Decisión | Elegida |
| :--- | :--- | :--- |
| **D1** | Pines de los MCU | **A**: externas por guion y contrastadas con el modelo |
| **D2** | Dónde va lo de `mcu-sim-build` | **B**: un `<build>` dentro del XML, que `mcu-sim` comprueba y no manda (fase B7) |
| **D3** | Placas existentes | **C**: en esta versión no se editan; sirven como tarjetas en sistemas |
| **D4** | Conectores con nombre | **A** ahora, **B** después (B14) |
| D5–D9 | Generadores en C++, biblioteca persistente, biblioteca `trazado`, giros de 90°, mismo repositorio | Las propuestas, mientras no se diga otra cosa |
