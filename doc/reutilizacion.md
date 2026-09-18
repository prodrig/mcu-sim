# Reutilizar las piezas en otro MCU

Qué parte de este modelo es «del STM32F407VG» y qué parte es del Cortex-M4F, de
la familia F4 o de la arquitectura ARMv7-M; y cómo se construye una pieza con
otros rasgos sin tocar su código.

---

## 1. El problema, en una frase

El proyecto modela un STM32F407VG, pero **casi nada de lo que hay dentro es del
F407**. El núcleo es un Cortex-M4F con licencia: lo que ST decide al integrarlo
son cuatro números. El controlador de Flash es el mismo en toda la familia F4;
lo que cambia es el tamaño y la tabla de sectores. La SRAM es una SRAM.

Hasta ahora eso era cierto **en el papel y falso en el código**: `N_IRQ = 82`
era una constante global, `bool enabled_[N_IRQ]` un array de tamaño fijo, la
máscara de prioridad un `0xF0` escrito a mano en tres sitios y el tamaño de la
Flash un `addr::FLASH_SIZE` leído desde dentro de `FlashIf`. Cada una de esas
líneas era una pieza soldada al chip.

Ahora los rasgos son **datos**, y las piezas los reciben.

---

## 2. Las tres capas, y dónde está cada cosa

| | Qué es | Dónde vive | ¿Se parametriza? |
| :--- | :--- | :--- | :---: |
| **Arquitectura** | El juego de instrucciones, el modelo de excepciones, el mapa del PPB, el bit-banding, las bases `0x0800_0000` y `0x2000_0000` | `common/ahb_types.h`, `core/cpu*.h` | **No** |
| **Integración** | Cuántas IRQ, cuántos bits de prioridad, cuántas regiones de MPU, qué FPU, cuánta Flash y cuánta RAM, qué topes de reloj | `core/core_caps.h`, `mem/mem_caps.h`, `top/mcu_caps.h` | **Sí** |
| **Familia** | Qué periféricos hay y en qué dirección, el árbol de reloj, el encapsulado y su tabla de funciones alternativas | `periph/`, `rcc/`, `pins/` | **No: modelo nuevo** |

La frontera de la segunda fila es la que importa, y está escrita en el campo
`McuCaps::familia`: **dos chips de la misma familia se distinguen con un
descriptor; dos familias distintas necesitan modelo.** Un F405 es un F407 sin
Ethernet ni cámara y se describe cambiando números; un F446 tiene otro árbol de
reloj y periféricos que aquí no existen, y describirlo con un `McuCaps` no lo
convertiría en un F446 — lo convertiría en un F407 con etiqueta falsa.

---

## 3. Los rasgos del núcleo: `CoreCaps`

```cpp
struct CoreCaps {
    unsigned n_irq;         // 82 en el F407; hasta 240 en ARMv7-M
    unsigned prio_bits;     // 4 en toda la familia F4
    unsigned mpu_regiones;  // 0 = no hay MPU
    FpuKind  fpu;
    uint32_t cpuid;         // lo que el firmware lee en SCB->CPUID
};
```

De ahí salen, sin repetir la cuenta en ningún sitio:

* el tamaño del vector de entradas del NVIC (`irq_in`) y de sus arrays de
  estado;
* `n_excepciones() = 16 + n_irq`;
* **`prio_mask()`**, que es la parte que más desconcierta al portar firmware:
  con cuatro bits, escribir `0xFF` en una prioridad deja `0xF0`; con tres,
  `0xE0`. `NVIC_SetPriority(irq, 5)` no significa lo mismo en dos chips con
  distinto número de bits, y ahora el modelo lo reproduce en vez de dar siempre
  la respuesta del F407;
* `MPU_TYPE.DREGION`, y que un núcleo **sin** MPU lea `MPU_TYPE = 0`, que es
  como CMSIS averigua que no lo hay;
* que `CPACR` se quede a cero en un núcleo sin FPU, que es como el arranque de
  CMSIS averigua que no debe habilitarla.

Quien construye: `Scs`, `Mpu` y `CortexM4F`, todos con **el F407 por omisión**.

```cpp
Scs        scs{"scs"};                          // los 82 de siempre
Scs        otro{"otro", CORE_M4F_MINIMO};       // 32 lineas, 3 bits
CortexM4F  nucleo{"nucleo", DBG_PINES, CORE_M3_SIN_FPU};
```

---

## 4. Los rasgos de las memorias: `MapaFlash` y `MapaRam`

```cpp
struct MapaFlash {
    uint32_t base, size;
    const FlashSector* sectores;   // la tabla, que NO es proporcional
    unsigned n_sectores;
    uint32_t sysmem_base, sysmem_size;  // + OTP y bytes de opción
    double   hz_por_estado_espera;      // la curva de LATENCY
    unsigned max_estados_espera;
};
```

Tres cosas que este struct resuelve y que antes no tenían dónde ir:

**La geometría de sectores no es una división.** Un F407 de 1 MB tiene doce
sectores de cuatro tamaños distintos —cuatro de 16 KB, uno de 64, siete de
128—, y uno de 256 KB tiene seis. No se puede calcular; hay que darla. Por eso
es una tabla y no un `size / n`.

**La curva de estados de espera es una regla, no una tabla.** En toda la
familia es *un estado de espera más por cada tanto de frecuencia*, y lo que
cambia entre chips y entre rangos de VDD son **el escalón y el techo**. Con esos
dos números, `latencia_minima()` da exactamente lo mismo que la tabla escrita a
mano del F407 —comprobado megahercio a megahercio en T127— y además sirve para
un chip que no llegue a 168 MHz.

**Un bloque que no existe es un cero.** Hay F4 sin CCM y sin SRAM2. Describirlos
es poner `0` en `MapaRam`, no borrar líneas del modelo: la matriz AHB deja de
decodificar ese rango, el cargador de imágenes deja de aceptarlo y el resto
sigue igual.

---

## 5. El descriptor: `McuCaps`

```cpp
struct McuCaps {
    const char*  nombre;     // "STM32F407VG", el que se escribe en el XML
    const char*  familia;    // "STM32F4": la frontera de lo recombinable
    CoreCaps     nucleo;
    MemCaps      memoria;
    LimitesReloj reloj;      // los topes por dominio
    Encapsulado  enc;        // qué pads salen al plástico (§9.2)
    Periferia    perif;      // ETH, cámara y bus externo (§9.3)
};
```

`Stm32F407VG` lo recibe por el constructor, con `MCU_STM32F407VG` por omisión, y
de él salen el núcleo, las cuatro RAM, la Flash, la decodificación de la matriz
y los avisos de frecuencia del RCC. El aviso del RCC, de paso, dejó de decir
`"HCLK > 168 MHz"` a pelo: ahora imprime el tope **del chip que se está
montando**, porque un aviso que dice 168 en un chip que no pasa de 84 es peor
que no decir nada.

---

## 6. Por qué un struct y no una plantilla

Es la receta que el resto del modelo lleva usando desde F4 (`UsartCaps`,
`TimCaps`, `SpiCaps`, `DebugCaps`): un `struct` `constexpr`, unas instancias con
nombre, y las clases lo toman **por valor en el constructor**.

Con eso la elección se puede hacer **en tiempo de compilación** —hay un alias de
plantilla, `CoreT<DBG_PINES, CORE_M4F_MINIMO>`— o **en tiempo de ejecución**, que
es lo que hace falta cuando el tipo de MCU viene de un fichero XML o de `--mcu`.
Una plantilla obligaría a lo primero y cerraría lo segundo, y lo segundo es
justo el caso de uso de este simulador.

**El coste, dicho sin adornos:** los arrays de tamaño fijo del NVIC pasan a ser
`std::vector`, y eso es una indirección más por acceso. Se ha medido —suite
completa, **mismo tiempo simulado al picosegundo**— y no se nota: el NVIC se
consulta una vez por instrucción en el peor caso y el vector está siempre en
caché. Lo que sí se ha cuidado es que **no haya ninguna reserva de memoria en
marcha**: todo se dimensiona en el constructor, antes de `sc_start`.

---

## 7. Lo que sigue soldado

Conviene decirlo, porque es el límite real de esto y no se arregla con más
`struct`s:

* ~~**El encapsulado.**~~ **Cerrado** — véase §9.2. Era una función estática y
  ahora es un `Encapsulado` con máscara por pin, que el `PinMux` y el netlist
  reciben por instancia. Era el obstáculo que señalaba multi-MCU §6.3, y hasta
  que no cayó no se podían describir los otros diez miembros de la familia.
* **La tabla de funciones alternativas** (`pins/af_types.h`) y **el juego de
  periféricos** son de la familia, y ahí no hay parametrización posible: un
  chip con otro juego es otro modelo.
* **Los números de IRQ de cada periférico** están escritos en `bind_irqs()`.
  Son de la familia igual que los periféricos que los generan, así que van
  juntos.
* **No hay una factoría de MCU.** `--mcu` sigue comprobando el nombre contra una
  lista de uno. El día que haya un segundo chip, lo que hace falta es la
  `FabricaMcu` de multi-MCU §6.1 — y ahora, a diferencia de antes, lo que esa
  factoría tendría que devolver ya existe: un `McuCaps`.

---

## 8. Cómo se comprueba

**T127**, treinta y ocho comprobaciones. No mira los `struct`: monta **un NVIC de
32 líneas con tres bits de prioridad y una Flash de 256 KB con seis sectores**
como módulos de verdad, al lado del F407 y en la misma simulación, y comprueba
que cada uno se comporta como el chip que describe:

* escribir `0xFF` en una prioridad deja `0xE0` en uno y `0xF0` en el otro;
* `ISER1` entero se queda a cero en el de 32 líneas —el registro existe, los
  bits no— mientras la IRQ 5 se habilita sin problema;
* la dirección `0x0805_0000` está en el sector 6 del F407 y **en ninguno** del
  de 256 KB;
* y la regla genérica de estados de espera da lo mismo que la tabla original en
  todo el rango, megahercio a megahercio.

Las piezas de laboratorio (`CORE_M4F_MINIMO`, `FLASH_LAB_256K`, `RAM_LAB_64K`)
**no son ningún chip** y el código lo dice en su comentario: no hay ahí ni un
periférico, ni un árbol de reloj, ni un encapsulado. Son las piezas montadas de
otra manera, que es exactamente lo que se afirma que se puede hacer.

El coste en simulación es cero: los puertos de esos módulos se atan a señales
que nadie mueve, así que sus procesos no despiertan nunca. El invariante del
banco sigue en **2 336 217 899 213 ps**.

---

## 9. La familia F405/F407, entera

Once referencias, y el refactor de las secciones anteriores es lo que permite
describirlas sin duplicar nada: **son el mismo modelo con distintos rasgos**.

| Referencia | Encapsulado | E/S | Flash | Sectores | ETH | Cámara | Bus ext. |
| :--- | :--- | ---: | ---: | ---: | :---: | :---: | :---: |
| STM32F405RG | LQFP64 | 51 | 1 MB | 12 | — | — | — |
| STM32F405OG | WLCSP90 | 72 | 1 MB | 12 | — | — | sí |
| STM32F405VG | LQFP100 | 82 | 1 MB | 12 | — | — | sí |
| STM32F405ZG | LQFP144 | 114 | 1 MB | 12 | — | — | sí |
| STM32F405OE | WLCSP90 | 72 | 512 KB | 8 | — | — | sí |
| STM32F407VE | LQFP100 | 82 | 512 KB | 8 | sí | sí | sí |
| **STM32F407VG** | LQFP100 | 82 | 1 MB | 12 | sí | sí | sí |
| STM32F407ZE | LQFP144 | 114 | 512 KB | 8 | sí | sí | sí |
| STM32F407ZG | LQFP144 | 114 | 1 MB | 12 | sí | sí | sí |
| STM32F407IE | LQFP176 | 140 | 512 KB | 8 | sí | sí | sí |
| STM32F407IG | LQFP176 | 140 | 1 MB | 12 | sí | sí | sí |

*Fuente de los recuentos: DS8626 (STM32F405xx/407xx), tabla 2.*
**Los once llevan el mismo núcleo** —82 líneas de IRQ, 4 bits de prioridad, 8
regiones de MPU, FPv4-SP— **y la misma RAM**: 112 + 16 + 64 KB más 4 KB de
backup. Ni el encapsulado ni el tamaño de Flash los cambian.

### 9.1 Las tres cosas que los distinguen, y ninguna más

**El dígito 5 o 7.** Un F405 es un F407 **sin Ethernet y sin cámara**. Eso es
todo: mismo núcleo, misma memoria, mismos temporizadores, mismos puertos serie,
mismos ADC, mismo bxCAN, mismos OTG. (El F415/F417 son los mismos con el
acelerador criptográfico; **no están declarados** porque ese bloque no está
modelado, y un descriptor no lo haría aparecer.)

**La letra del encapsulado.** `R` = LQFP64, `O` = WLCSP90, `V` = LQFP100,
`Z` = LQFP144, `I` = LQFP176/UFBGA176.

**La última letra.** `E` = 512 KB de Flash en ocho sectores; `G` = 1 MB en doce.

### 9.2 El encapsulado, que era el obstáculo

`is_bonded_lqfp100()` era una **función estática** —señalada como obstáculo en
multi-MCU §6.3— y ahora es un `Encapsulado`: una máscara de 16 bits por puerto,
que dice **pin a pin** qué sale al plástico.

Tenía que ser por pin y no por puerto, y el LQFP64 es el caso que lo demuestra:
de su puerto D sale **un** pin, `PD2` (SDIO_CMD y UART5_RX). «El puerto D existe
a medias» no se puede decir con un booleano.

Lo que sostiene la tabla es una comprobación sencilla y eficaz: cada encapsulado
lleva además **el número de E/S que le da el datasheet**, y T128 comprueba que
la máscara tiene exactamente esos bits. Una máscara mal copiada deja de cuadrar
y la prueba falla; sin ese contraste, el error sería invisible — el modelo se
montaría igual y el alumno desarrollaría contra un chip que no es el suyo.

### 9.3 Un periférico ausente es espacio reservado

Que el F405 no lleve Ethernet no se modela apagándolo. Su ventana **no la
decodifica nadie**, y tocarla da error de bus, que es lo que pasa en el silicio.
El módulo sigue construido —la elaboración de SystemC es estática y no hay otra—
pero sin camino desde el bus; para que la elaboración cierre, su socket se ata a
un iniciador que no manda nada nunca.

La alternativa descartada, y por qué: decodificar la ventana hacia un destino
que devuelva error se vería igual desde el firmware, pero **mentiría en el
netlist** — el volcado enseñaría un periférico donde no lo hay.

El FSMC es el mismo caso con otro motivo: falta en el LQFP64 no por decisión de
catálogo sino **porque no hay pines donde sacar el bus**. Y el «FSMC restringido»
que el datasheet anota para LQFP100 y WLCSP90 no necesita ningún campo: la
restricción es que no salen todas las líneas de dirección y datos, y eso ya lo
dice el encapsulado. Un booleano más sería describir dos veces lo mismo y
arriesgarse a que las dos descripciones no coincidan.

### 9.4 El WLCSP90: el único irregular *(cerrado — antes estaba sin contrastar)*

De los seis encapsulados, **los seis llevan ya su reparto verificado**. El
WLCSP90 fue el último, y el que más justifica que esto sea una máscara por pin y
no una regla: los otros cinco se describen con «estos puertos enteros más el
oscilador», y este no.

| Puerto | Bolas | Detalle |
| :--- | ---: | :--- |
| PA, PB | 16 + 16 | completos |
| PC | 13 | **faltan PC1, PC4 y PC5** — ADC123_IN11, ADC12_IN14 y ADC12_IN15 |
| PD | 14 | **faltan PD3 y PD13** |
| PE | 9 | **solo PE7–PE15**, la mitad alta |
| PH | 2 | PH0, PH1 |
| PI | 2 | **PI0 y PI1**, que no salen ni en el LQFP100 ni en el LQFP144 |

Verificado por dos fuentes de ST que coinciden puerto a puerto: la **figura 17
del DS8626** (el diagrama de bolas) y **`STM32_open_pin_data`**, la base que
alimenta CubeMX, que declara `<IONb>72</IONb>`.

**Lo que había antes era falso, y conviene contarlo.** La reconstrucción
provisional decía «A, B, C y D completos más PE0–PE5», que **suma 72 y no es el
reparto de ST en casi nada**: ni C ni D están completos, el rango de E es el
contrario y PI no se contemplaba. El recuento cuadraba **por casualidad**, y esa
es justo la razón por la que `Encapsulado::coherente()` no basta por sí sola y
por la que el campo `verificado` tenía que existir: con el mapa falso, `sim`
aceptaba sin decir nada un componente soldado a PC4, donde no hay bola.

El efecto de tenerlo bien se ve en una línea:

```
$ ./build/mcu-sim placas/discovery_min.xml --mcu STM32F405OE --valida
  [decl] LD3.anodo: el pad PD13 no sale al encapsulado WLCSP90
```

La Discovery **no cabe en un WLCSP90**: le falta el pin del LED naranja.

### 9.5 Lo que sigue sin distinguirse entre miembros

Honestidad sobre el alcance, que es lo que hace que lo anterior valga:

* **La tabla de funciones alternativas es la misma para los once.** En el
  silicio lo es; lo que cambia es qué pines salen, y de eso ya se encarga el
  encapsulado. Un AF registrado sobre un pad no soldado no hace daño —ese pad no
  se puede conectar a nada, y el netlist lo rechaza—, pero conviene saber que el
  modelo no distingue AF por referencia.
* **Los bits de reloj de los periféricos ausentes siguen existiendo.** En un
  F405, `RCC_AHB1ENR.ETHMACEN` debería leer cero; aquí se escribe y se lee como
  en el F407. El periférico no está —su ventana da error de bus—, pero el bit
  que lo enciende sí. Es una diferencia observable y está sin cerrar.
* **`UFBGA176` y `LQFP176` comparten reparto** y solo se distinguen por el
  nombre, que es lo correcto: son el mismo die con otro plástico.
