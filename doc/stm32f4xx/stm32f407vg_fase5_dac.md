# Fase F5 (parte DAC) — Convertidor digital-analógico

Informe de implementación de la **parte de DAC** de la fase F5 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la fase
F4 y a las partes de SPI/I2S, I2C y ADC de esta misma fase
(`doc/stm32f4xx/stm32f407vg_fase5_spi.md`, `_i2c.md`, `_adc.md`).
Fuentes: `doc/refs/stm32f407xx/informe_revisado.md` [IR] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** el bloque DAC del STM32F407VG con sus **dos
canales** [IR, §12.14], con el requisito explícito de **analizar sus
similitudes y diferencias** y de que el tipo se seleccione por parámetros de
plantilla o del constructor (número de bits, número de canales, etc.).

**Resultado:** el bloque está completo y verificado. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **893 de 893 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S + 75 de I2C +
95 de ADC + **65 nuevas de DAC**, código de salida 0). La verificación no se
queda en los registros: la salida es un **driver eléctrico sobre el pin**, y con
una carga de 1 kΩ colgada de PA4 se ve por qué existe el amplificador de salida
—sin él la tensión se hunde de 3,3 V a 0,2 V—; un firmware compilado con el
**CMSIS oficial de ARM y de ST** genera una rampa que se mide en el pin.

---

## 1. Similitudes y diferencias de los DAC

Aquí el encargo tiene un matiz que conviene aclarar antes de nada: **el F407 no
tiene varios DAC**. Tiene **un bloque con dos canales** [IR, §12.14.1], así que
la comparación que pide el análisis es entre **los dos canales**.

### 1.1 Los dos canales son simétricos hasta el bit

Y la respuesta es todavía más tajante que la de los tres I2C:

* **`DAC_CR` repite para el canal 2 exactamente los mismos campos del canal 1,
  desplazados 16 bits.** `EN`, `BOFF`, `TEN`, `TSEL[2:0]`, `WAVE[1:0]`,
  `MAMP[3:0]`, `DMAEN` y `DMAUDRIE` están en los bits 0-13 para el canal 1 y en
  los 16-29 para el canal 2, en el mismo orden.
* **`DAC_SR` usa el mismo desplazamiento:** `DMAUDR1` en el bit 13 y `DMAUDR2`
  en el 29.
* **Cada registro de datos existe por duplicado** y en el mismo orden:
  `DHR12R1`/`DHR12L1`/`DHR8R1` y `DHR12R2`/`DHR12L2`/`DHR8R2`.
* **La tabla de disparos es la misma para los dos.** Los ocho valores de `TSEL`
  —TIM6, TIM8, TIM7, TIM5, TIM2, TIM4, EXTI9 y software— valen igual para
  ambos.
* Los dos tienen los mismos 12 bits, el mismo amplificador de salida y los
  mismos dos generadores de onda.

Esa simetría es tan literal que el modelo la aprovecha: hay **un solo juego de
accesores** a los campos de `CR`, con el desplazamiento de 16 bits como
parámetro.

```cpp
static unsigned sh(unsigned c) { return 16u * c; }
bool en(unsigned c)   const { return (cr_ >> (sh(c) + 0)) & 1u; }
bool boff(unsigned c) const { return (cr_ >> (sh(c) + 1)) & 1u; }
...
```

### 1.2 En qué se diferencian: solo en la integración

| | Canal 1 | Canal 2 |
| :--- | :---: | :---: |
| Salida [IR, §12.14.1] | **PA4** (DAC_OUT1) | **PA5** (DAC_OUT2) |
| Celda de DMA | **DMA1 S5C7** | **DMA1 S6C7** |
| Bit de bandera en `DAC_SR` | 13 | 29 |
| Base, reloj y habilitación | 0x4000 7400, APB1, `APB1ENR.29` | ← el mismo |
| Interrupción | **54**, compartida | ← la misma |

Y la interrupción merece un párrafo aparte porque es una rareza doble: los dos
canales **comparten** el vector 54 y, además, **ese vector no es suyo**: es el
del TIM6 (`TIM6_DAC`). Un manejador de desbordamiento de DAC tiene que convivir
con el de actualización del TIM6, que es justo el temporizador que se usa para
disparar el DAC. En el modelo eso está cableado como una puerta OR explícita en
el top (`or_irq54`), y T70 comprueba que la bandera del DAC levanta el vector.

### 1.3 Lo que no es de ningún canal: los registros duales

Hay una tercera categoría que no encaja en «lo común» ni en «lo distinto»: los
registros **duales** `DHR12RD`, `DHR12LD` y `DHR8RD`. No pertenecen a un canal
sino **a la pareja**: una sola escritura carga los dos datos, y es lo que
permite actualizar las dos salidas **en el mismo instante** [IR, §12.14.2-C].
Es la única funcionalidad del bloque que desaparece por completo si se quita un
canal, y por eso en el modelo el rasgo `dual` lleva un `static_assert` que exige
`n_channels == 2`.

### 1.4 Entonces, ¿por qué parametrizar?

Por las mismas dos razones que en el I2C, y el resultado es igual de útil:

1. **El bloque sí varía entre familias de STM32.** Hay derivados con un solo
   canal, sin generador de ondas o sin amplificador de salida.
2. **Convierte una suposición en una comprobación.** «Los dos canales son
   iguales» es una afirmación sobre el silicio; con los rasgos explícitos T67
   **lo comprueba desde el bus**: escribe unos a `DAC_CR`, lee, y compara la
   mitad alta con la baja.

```
DAC_CR = 0x3FFF3FFF -> canal 1 = 0x3FFF, canal 2 = 0x3FFF
```

Para `DAC_SR` ese truco **no sirve**, y el motivo es interesante: sus banderas
son `w1c`, así que escribir unos las *borra* en vez de ponerlas. La simetría se
demuestra entonces provocando el mismo desbordamiento en cada canal y viendo
dónde aparece la bandera:

```
DAC_SR tras desbordar los dos canales = 0x20002000
```

---

## 2. Selección del tipo de DAC

`src/periph/dac.h` define un `struct` de rasgos, tres instancias constantes y
las dos vías de selección.

```cpp
struct DacCaps {
    unsigned n_channels  = 2;      // canales de salida
    unsigned bits        = 12;     // resolución del convertidor
    bool     fmt_left    = true;   // registros DHR12Lx
    bool     fmt_8bit    = true;   // registros DHR8Rx
    bool     dual        = true;   // DHR12RD/DHR12LD/DHR8RD
    bool     buffer      = true;   // CR.BOFFx y el amplificador de salida
    bool     noise       = true;   // WAVE = 01, LFSR
    bool     triangle    = true;   // WAVE = 1x
    bool     trigger     = true;   // CR.TENx/TSELx y SWTRIGR
    bool     dma         = true;   // CR.DMAENx, SR.DMAUDRx
    double   t_settle_us = 3.0;    // latencia de estabilización
    double   r_buffered  = 15.0;   // impedancia de salida con buffer [ohm]
    double   r_open      = 15.0e3; // impedancia de salida con BOFF [ohm]
    double   v_margin    = 0.2;    // el buffer no llega a los raíles [V]
    const char* kind     = "DAC";
};
```

### 2.1 En tiempo de compilación (parámetro de plantilla)

```cpp
template <const DacCaps& C> class DacT : public DacBase { ... };

using Dac      = DacT<CAPS_DAC_F407>;    // el F407: 2 canales, 12 bits
using Dac1Ch   = DacT<CAPS_DAC_1CH>;     // un solo canal
using DacBasic = DacT<CAPS_DAC_BASIC>;   // 8 bits, sin ondas ni DMA
```

Las combinaciones imposibles no compilan, y el número de canales y de bits se
consulta como constante de compilación:

```cpp
static_assert(C.n_channels >= 1 && C.n_channels <= 2, "uno o dos canales");
static_assert(C.bits >= 8 && C.bits <= 12, "el dato cabe en DHR de 12 bits");
static_assert(!C.dual || C.n_channels == 2,
              "los registros duales exigen los dos canales");
static constexpr unsigned channels() { return C.n_channels; }
static constexpr unsigned bits()     { return C.bits; }
```

### 2.2 En tiempo de ejecución (parámetro del constructor)

```cpp
DacBase d{"d", CAPS_DAC_BASIC};                    // o unos DacCaps a medida
```

T67 construye esa variante y comprueba desde el bus lo que ha perdido:

```
variante en ejecucion (a medida): CR = 0x00000001, SR = 0x00000000,
    canal 2 = 0x0000, dual = 0x00000000, DHR8R1 = 0xA00
```

Sin buffer, sin ondas, sin disparo y sin DMA no queda en `CR` más que `EN1`; el
canal 2 y los registros duales no existen; y con 8 bits el dato se coloca en la
parte alta de `DHR`, que es exactamente lo que hace un DAC de 8 bits.

### 2.3 Cómo actúan los rasgos: máscaras de escritura

El mecanismo es el del resto del proyecto: los rasgos definen la **máscara de
escritura** de cada registro, y un bit que la instancia no implementa lee cero
igual que un bit reservado del silicio. En `CR` la simetría permite construir la
máscara **una vez** y duplicarla:

```cpp
uint32_t cr_mask() const {
    uint32_t one = 1u;                                   // ENx siempre
    if (caps_.buffer)  one |= 1u << 1;                   // BOFFx
    if (caps_.trigger) one |= (1u << 2) | (7u << 3);     // TENx, TSELx
    if (caps_.noise || caps_.triangle) one |= (3u << 6) | (0xFu << 8);
    if (caps_.dma)     one |= (1u << 12) | (1u << 13);
    uint32_t m = one;
    if (caps_.n_channels > 1) m |= one << 16;            // el canal 2 es igual
    return m;
}
```

---

## 3. El modelo

`src/periph/dac.h` (≈470 líneas), sustituyendo el esqueleto que había desde F1.

### 3.1 La salida es eléctrica, y por eso el buffer se nota

La salida **no es un número**: es un driver Thevenin sobre el `AnalogNet` del
pin, con `V = DOR/(2^N - 1) · VREF+` y una **impedancia de salida que depende de
`BOFF`**. De ahí salen dos comportamientos opuestos que el banco mide en el pin:

| | Con buffer (`BOFF = 0`) | Sin buffer (`BOFF = 1`) |
| :--- | :---: | :---: |
| Recorrido en vacío | 0,200 V a 3,100 V | **0,000 V a 3,300 V** |
| Con 1 kΩ a masa | **3,054 V** | 0,206 V |

El amplificador **no llega a los raíles** —se queda a 0,2 V de cada extremo—
pero puede con una carga; sin él la salida recorre toda la escala pero su
impedancia de ~15 kΩ hace que cualquier carga la hunda. Es exactamente el
compromiso que obliga a decidir `BOFF` en una placa real, y el modelo no lo
decide: lo resuelve el divisor del nodo analógico.

Un detalle que costó un fallo: **cambiar `BOFF` cambia la etapa de salida, no el
dato**. La primera versión solo volvía a aplicar la salida cuando `DOR` se
movía, así que activar el buffer con el dato quieto dejaba el pin con la
impedancia anterior. Ahora una escritura de `CR` que toque `BOFF` fuerza la
reaplicación.

### 3.2 La latencia de estabilización

`t_SETTLING` se modela como **retardo de transporte**: el pin conserva la
tensión anterior hasta que vence el plazo y entonces toma la nueva. No es una
rampa RC —el plan pide modelar la latencia, no la forma del transitorio— y el
banco lo mide:

```
escalon 0 -> 4095: a 1 us el pin esta a 0.000 V, a 21 us a 3.300 V
```

### 3.3 El camino del dato y la tubería del disparo

Con `TEN = 0` el dato pasa de `DHR` a `DOR` en cuanto se escribe. Con `TEN = 1`
se queda esperando, y aquí está el detalle que más despista en el silicio y que
el modelo reproduce: **cada disparo saca a `DOR` el dato que ya estaba en `DHR`
y pide el siguiente**. Cuando una transferencia de DMA termina, la última
muestra sigue en `DHR` esperando un disparo más:

```
al terminar el DMA, DOR1 = 3510; tras un disparo mas, DOR1 = 4095
```

Es la causa clásica de que «la última muestra de la tabla no salga», y está
comprobada explícitamente en T70.

### 3.4 Generadores de onda

* **Triángulo**: un contador que sube hasta `2^(MAMP+1) - 1` y vuelve a bajar,
  sumado al dato de `DHR`.
* **Ruido**: un LFSR de 12 bits con valor de arranque 0xAAA, enmascarado por
  `MAMP`. El juego exacto de tomas no consta en las fuentes; se usa el
  polinomio documentado `x^12 + x^6 + x^4 + x + 1` y así queda anotado en el
  código. Por eso la verificación comprueba **propiedades** —que cambia en cada
  disparo y que se queda dentro de `DHR + máscara`— y no una secuencia concreta,
  que sería inventarse un dato que no está en el informe.

### 3.5 DMA y desbordamiento

Petición por canal, de nivel, atendida cuando alguien escribe `DHR`. Si llega
un disparo con la petición anterior aún sin servir, se marca `DMAUDRx` y el
canal **deja de pedir**, como el silicio. La bandera es **`w1c`**: se borra
escribiendo **uno**, al revés que casi todas las del dispositivo, que son
`rc_w0`. T70 lo comprueba en los dos sentidos.

---

## 4. Verificación

65 comprobaciones nuevas, en cuatro grupos.

### 4.1 T67 — Los dos canales y las variantes

La simetría de `CR` y de `SR` medida desde el bus (§1.4), la existencia de los
tres formatos en los dos canales, y la variante reducida construida en tiempo
de ejecución.

### 4.2 T68 — Registros, formatos y tensión en el pin

Error de bus sin `DACEN`, valores de reset, `SWTRIGR` que se lee como cero por
ser de solo escritura, los tres formatos de dato cargando el mismo valor, el
registro dual cargando los dos canales de una vez, la rampa de tensiones medida
en PA4 y la latencia de estabilización.

```
codigo   V(PA4)   esperado
     0   0.000 V   0.000 V
  1024   0.825 V   0.825 V
  2048   1.650 V   1.650 V
  3072   2.476 V   2.476 V
  4095   3.300 V   3.300 V
```

### 4.3 T69 — Buffer, carga, disparos y ondas

El compromiso del amplificador con y sin carga (§3.1); el disparo por software
y por `TIM6_TRGO`; el triángulo con su amplitud de `MAMP`; y el ruido con sus
propiedades estadísticas.

### 4.4 T70 — DMA, desbordamiento y firmware con CMSIS

Una forma de onda de ocho muestras entregada por **DMA1 stream 5 canal 7** con
disparo del TIM6, incluida la tubería del disparo (§3.3); el desbordamiento
`DMAUDR1` con su interrupción por el vector 54 y su borrado `w1c`; y un firmware
real en `verif/fw/dac_demo/`:

```
PCLK1 = 42000000 Hz | DOR1 final = 4095 | pasos = 16 | 3300 mV | dual = 1
el pin PA4 recorrio de 0.000 V a 3.300 V durante la rampa
```

El firmware sube el reloj a 168 MHz, pone PA4 y PA5 en modo analógico, genera
una rampa de 16 pasos, comprueba el registro dual y pasa `DOR` a milivoltios.
Mientras tanto el banco **vigila el pin desde fuera** y comprueba que la rampa
recorre de verdad la escala.

---

## 5. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/dac.h` | **reescrito** | El bloque DAC completo y parametrizado (≈470 líneas; antes era el esqueleto de F1) |
| `src/top/sc_main.cpp` | ampliado | Grupos T67-T70 (65 comprobaciones) |
| `verif/fw/dac_demo/` | **nuevo** | Firmware con CMSIS (`main.c`, `Makefile`) |

El netlist del top no ha necesitado cambios: las salidas a PA4/PA5, el vector 54
por su puerta OR con el TIM6, las celdas de DMA y el vector de disparos
indexado por `TSEL` ya estaban cableados desde la fase F1.

---

## 6. Trabajo pendiente

Del propio DAC queda fuera una sola cosa, anotada en el código: el **disparo por
EXTI9** (`TSEL = 110`), que en el top está a cero porque el modelo del EXTI no
exporta esa línea como señal de disparo. Los seis disparos por `TRGO` de
temporizador y el de software están conectados y verificados.

De la fase F5 quedan por modelar, en el orden previsto por el plan: **RTC**,
**bxCAN**, **SDIO**, **CRC/RNG** y los **perros guardianes** (IWDG y WWDG). Con
el DAC queda cerrada la parte analógica del dispositivo.
