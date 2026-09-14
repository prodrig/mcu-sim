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

* **El encapsulado.** `is_bonded_lqfp100()` sigue siendo una **función
  estática** en `pins/pin_mux.h`. Mientras haya un solo encapsulado da igual;
  con dos MCU de encapsulados distintos en la misma placa, no. Tiene que pasar
  a ser un dato de instancia —un predicado o una máscara por puerto— y hay que
  hacerlo **antes** de que exista un segundo encapsulado, no después.
  [multi-MCU §6.3]
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
