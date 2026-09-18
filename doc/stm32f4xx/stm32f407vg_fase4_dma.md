# Fase F4 (parte DMA) — Controladores DMA1 y DMA2

Informe de implementación de la **parte de DMA** de la fase F4 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a `doc/stm32f4xx/smt32f407vg_fase0.md`,
`doc/stm32f4xx/stm32f407vg_fase1.md` (infraestructura y matriz AHB),
`doc/stm32f4xx/stm32f407vg_fase2.md` (núcleo Cortex-M4F) y `doc/stm32f4xx/stm32f407vg_fase3.md`
(pines, GPIO y RCC eléctrico). Fuentes: `doc/stm32f4xx/informe_revisado.md` [IR] y
`doc/stm32f4xx/informe_instrucciones.md` [II].

**Alcance de la fase según el plan:** *"DMA1/2, USART, TIM avanzados,
EXTI/SYSCFG"*, con criterio de salida *"firmware con drivers HAL básicos"*.
**Alcance de este entregable:** únicamente **DMA1 y DMA2** [IR, §11]. Los
demás bloques de F4 (USART, temporizadores avanzados, EXTI/SYSCFG) quedan para
entregables posteriores de la misma fase.

**Resultado:** los dos controladores están completos y verificados. El modelo
compila sin avisos con `-Wall -Wextra -O2` y la suite pasa **287 de 287
comprobaciones** (124 de F1 + 12 de F2 + 80 de F3 + **71 nuevas de DMA**,
código de salida 0, ~1,3 s de CPU del anfitrión). La parte que corresponde al
criterio de salida se cumple con firmware real: un programa compilado con el
**CMSIS oficial de ARM y de ST**, sin adaptaciones, que programa DMA2 siguiendo
la secuencia del manual, copia 256 bytes de memoria a memoria, atiende la
interrupción `DMA2_Stream0_IRQHandler` y hace **conmutar el LED de PD12
escribiendo GPIOD_BSRR por DMA, sin que la CPU llegue a tocar el puerto** — el
parpadeo se observa como tensión en el pin.

---

## 1. Resumen ejecutivo

| Bloque | Estado |
| :--- | :--- |
| Banco de registros | Completo: LISR/HISR/LIFCR/HIFCR y, por stream, SxCR/SxNDTR/SxPAR/SxM0AR/SxM1AR/SxFCR con sus máscaras de escritura y valores de reset |
| Topología de puertos | DMA1: puerto de memoria en la matriz, puerto de periféricos directo a APB1. DMA2: los dos puertos son maestros de la matriz |
| Árbitro | Prioridad de software (PL) con desempate por número de stream |
| Modo directo | Un elemento por petición, sin FIFO ni ráfagas; ancho de memoria forzado al del periférico |
| Modo FIFO | FIFO de 4 palabras por stream, umbrales FTH, empaquetado y desempaquetado entre anchos distintos, campo FS |
| Ráfagas | INCR4/INCR8/INCR16 independientes en cada puerto (MBURST/PBURST) |
| Direccionamiento | PINC/MINC, dirección fija, PINCOS |
| Modos | Circular y doble buffer con conmutación automática de CT |
| Multiplexado | Los 64 canales (8 streams × 8 canales) de las tablas [IR, §11.4], ya cableados desde F0 |
| Banderas e IRQ | TCIF/HTIF/TEIF/DMEIF/FEIF, borrado por LIFCR/HIFCR, una IRQ por stream hacia el NVIC |
| Errores | Error de transferencia, de modo directo y de FIFO, con deshabilitación automática del stream |
| Control de flujo por periférico | Parcial (PFCTRL aceptado, sin fin de transferencia por el periférico); se cierra con el SDIO en F5 |

Código nuevo o reescrito: **≈550 líneas** de modelo (`periph/dma.h`), **≈390
líneas** de verificación y un firmware de demostración de 136 líneas.

---

## 2. Ficheros de la fase

### 2.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `src/verif/fw/dma_demo/main.c`, `Makefile` | Firmware de demostración compilado con el CMSIS de ARM/ST vendorizado en F3 |

### 2.2 Reescritos o ampliados

| Fichero | Cambios |
| :--- | :--- |
| `src/periph/dma.h` | Motor completo (era un esqueleto de 50 líneas) |
| `src/bus/ahb_matrix.h` | Camino DMA2 → Flash por el bus DCode (§9) |
| `src/top/sc_main.cpp` | Grupos de verificación T26-T31 |

---

## 3. Arquitectura del modelo

### 3.1 Doble puerto y topología

Cada controlador es a la vez **esclavo** (banco de registros en AHB1) y
**maestro doble**: un puerto de memoria y un puerto de periféricos
[IR, §11-Implicaciones]. La topología reproduce la del silicio:

* **DMA1** — el puerto de memoria entra en la matriz como maestro *DMA1-M*; el
  puerto de periféricos ataca el decodificador de APB1 **sin pasar por la
  matriz**. La consecuencia es observable y se comprueba en T30: un `SxPAR`
  apuntando a un periférico de AHB1 no se decodifica y produce error de
  transferencia. DMA1 no tiene ruta memoria-a-memoria.
* **DMA2** — los dos puertos son maestros de la matriz (*DMA2-M* y *DMA2-P*),
  admite memoria-a-memoria y alcanza la Flash.

Como el puerto de periféricos de DMA1 no cruza la matriz, no necesita un
identificador de maestro propio en `AhbExt`; sus transacciones se etiquetan como
DMA1 a efectos de traza. Esto evita añadir una fila inútil a la tabla de
conectividad y una novena entrada al vector de maestros de la matriz.

En un modelo *loosely-timed* los dos puertos no pueden operar literalmente «a la
vez» dentro de una misma llamada bloqueante; lo observable —el reparto de ancho
de banda y el orden de los accesos— se reproduce anotando la latencia que cada
esclavo devuelve, que es la que la matriz calcula con su propio arbitraje.

### 3.2 Motor y árbitro

Un único `SC_THREAD` por controlador ejecuta el ciclo *arbitrar → servir*:

1. si no hay reloj (gating del RCC o árbol parado) o el bloque está en reset, el
   motor no mueve datos — igual que el silicio, y además evita que el hilo gire
   sin que avance el tiempo simulado;
2. `arbitrate()` elige entre los streams **listos** el de mayor `PL[1:0]` y, en
   caso de empate, el de número más bajo [IR, §11.3.2];
3. `service()` ejecuta una concesión completa (una ráfaga, o un elemento en modo
   directo), consume el tiempo acumulado de los accesos y actualiza banderas.

Un stream está *listo* si está habilitado y, salvo en memoria-a-memoria, su
canal seleccionado por `CHSEL` tiene la petición activa. Durante el servicio se
activa la línea de reconocimiento (`ack_out`) del canal.

Cuando ningún stream está listo el motor se bloquea en un evento que notifican
tanto las 64 líneas de petición como cualquier escritura al banco de registros:
el coste de simulación de un DMA inactivo es nulo.

### 3.3 FIFO, anchos y ráfagas

La FIFO se modela como **16 bytes** por stream con un nivel en bytes, que es lo
que permite representar el empaquetado sin casos especiales:

* **modo directo** (`DMDIS = 0`): un elemento por petición; el ancho de memoria
  lo fuerza el hardware al del periférico, no se admiten ráfagas y la FIFO no
  interviene [IR, §11.3.3];
* **modo FIFO** (`DMDIS = 1`): fase de llenado desde el origen (una ráfaga del
  puerto de origen, limitada por el hueco disponible y por lo que queda por
  leer) y fase de vaciado al destino cuando el nivel alcanza el umbral `FTH`, o
  cuando ya no queda nada por leer y hay que vaciar la cola.

El empaquetado sale gratis: si el origen es de 8 bits y el destino de 32, la
fase de llenado deposita bytes y la de vaciado saca palabras. T28 lo comprueba
en los dos sentidos contando los *beats* reales de cada puerto: 64 lecturas de
byte contra 16 escrituras de palabra, y al revés.

### 3.4 El contador NDTR

[IR, §11.3.1] dice que `SxNDTR` «decrementa tras cada fase de escritura
exitosa». Tomado al pie de la letra, eso es inconsistente con el
empaquetado: con PSIZE de 8 bits y MSIZE de 32, cuatro lecturas producen una
sola escritura, y descontar uno por escritura movería cuatro veces más datos de
los programados.

El modelo lleva la cuenta en **bytes** y expone `NDTR` como los elementos del
**ancho de origen** que aún no han quedado escritos en el destino. Así:

* se respeta literalmente el enunciado del informe —el contador baja en la fase
  de escritura, cuando el dato está realmente comprometido—;
* y es consistente con el empaquetado: una escritura de 32 bits que consume
  cuatro bytes de origen descuenta cuatro elementos.

Con `PSIZE = MSIZE`, que es el caso de todos los ejemplos del informe y de casi
todo el software real, las dos lecturas coinciden.

### 3.5 Circular y doble buffer

Al completar la transferencia se activa `TCIF` y:

* con **doble buffer** (`DBM`) se conmuta `CT`, el puntero de memoria pasa al
  otro buffer y el contador se recarga;
* con **circular** (`CIRC`) se recargan punteros y contador;
* en otro caso **el hardware borra `EN`**, que es lo que el firmware observa
  para saber que el stream terminó.

`HTIF` se activa al cruzar la mitad de los datos, medida también en bytes.

---

## 4. Banderas, interrupciones y errores

Las banderas viven en `LISR` (streams 0-3) y `HISR` (streams 4-7), con el
reparto de bits de [IR, §11.5]: dentro del grupo de seis bits de cada stream,
`FEIF` en +0, `DMEIF` en +2, `TEIF` en +3, `HTIF` en +4 y `TCIF` en +5, y los
grupos en los desplazamientos 0, 6, 16 y 22. `LIFCR`/`HIFCR` las borran
escribiendo un uno y leen cero.

La interrupción de cada stream es el OR de sus banderas con su habilitación
(`TCIE`, `HTIE`, `TEIE`, `DMEIE` en `SxCR` y `FEIE` en `SxFCR`). El vector de
salidas se publica desde un único proceso, siguiendo la convención del modelo:
el estado lo cambian tanto el motor como el `b_transport` del banco de
registros, y SystemC no admite dos escritores sobre una misma señal.

Errores implementados [IR, §11.8]:

| Bandera | Condición modelada |
| :--- | :--- |
| **TEIF** | Respuesta de error del bus en una fase de lectura o de escritura. El stream se deshabilita automáticamente |
| **DMEIF** | Ráfaga configurada (MBURST o PBURST ≠ single) con el modo directo activo, que el silicio no admite |
| **FEIF** | El umbral de la FIFO no es múltiplo exacto de lo que consume una ráfaga del puerto de memoria; o se pide memoria-a-memoria en modo directo |
| **TEIF** (configuración) | Memoria-a-memoria solicitada en DMA1, que no tiene esa ruta |

En los tres casos de configuración inválida el stream **no llega a arrancar** y
`EN` vuelve a cero, que es lo que hace el hardware.

---

## 5. Verificación

Seis grupos nuevos, **71 comprobaciones**.

| Grupo | Contenido |
| :--- | :--- |
| **T26** | Banco de registros: valores de reset de los 8 streams, independencia de los bancos, LISR/HISR de solo lectura, LIFCR de solo escritura, `SxNDTR` escribible solo con `EN = 0`, campo `FS`, borrado automático de `EN` al completar |
| **T27** | Memoria a memoria: SRAM→SRAM de 256 bytes con verificación byte a byte, `HTIF` a mitad de camino, recuento exacto de *beats*, Flash→SRAM (el caso de uso clásico de DMA2) y rechazo de memoria-a-memoria en DMA1 |
| **T28** | Empaquetado 8→32 y desempaquetado 32→8 con verificación byte a byte y recuento de *beats* en cada puerto; ráfaga INCR4 en el puerto de memoria |
| **T29** | Periférico↔memoria con petición: muestreo de `GPIOE_IDR` a un buffer, volcado de un buffer sobre `GPIOD_BSRR` con el LED como testigo; arbitraje por prioridad (instantánea de los dos `NDTR` a mitad de camino); modo circular; doble buffer con conmutación de `CT` |
| **T30** | Errores: `TEIF` por la restricción topológica del puerto de periféricos de DMA1, `DMEIF` por ráfaga en modo directo, `FEIF` por umbral incompatible, y la IRQ 56 (DMA2 stream 0) subiendo y bajando con la bandera |
| **T31** | Firmware real con CMSIS (abajo) |

### 5.1 Cómo se generan las peticiones sin periféricos

Las 64 líneas de petición están cableadas desde F0 según las tablas
[IR, §11.4], pero los periféricos que las activan (USART, SPI, I2C, ADC,
temporizadores) son todavía esqueletos. Para poder ejercitar los caminos
periférico→memoria y memoria→periférico antes de que existan, el modelo expone
`DmaCtrl::tb_set_request(stream, on)`: fuerza la petición del canal
seleccionado por ese stream. **No corresponde a ningún registro del silicio** y
está marcado como instrumentación de verificación; desaparecerá de las pruebas
en cuanto cada periférico active su propia línea.

Los «periféricos» de las pruebas son registros reales del modelo: `GPIOE_IDR`
como fuente (con un patrón forzado desde fuera del encapsulado con los drivers
externos de F3) y `GPIOD_BSRR` como destino, con el LED de PD12 como testigo.

### 5.2 T31 — Firmware con CMSIS

`verif/fw/dma_demo/main.c` se compila con la cabecera de dispositivo de ST y el
CMSIS-Core de ARM vendorizados en F3, con el `startup_stm32f407xx.s` y el
`system_stm32f4xx.c` oficiales. Sigue la secuencia de configuración del manual
[IR, §11.7] paso por paso y hace tres cosas:

1. copia 256 bytes de memoria a memoria con DMA2 stream 0 y **verifica el
   resultado él mismo**;
2. atiende `DMA2_Stream0_IRQHandler` a través del NVIC, leyendo `DMA2->LISR`
   dentro del manejador y limpiándolo con `DMA2->LIFCR`;
3. vuelca una tabla de valores sobre `GPIOD->BSRR` por DMA, de modo que el LED
   conmuta sin que la CPU escriba el puerto.

Resultado:

```
copia=1 NDTR_final=0 IRQ=1 BSRR=1 LISR=0x00000030 | 727839 instrucciones, 6 flancos del LED
```

`LISR = 0x30` son `TCIF0` y `HTIF0` tal como los ve el manejador antes de
borrarlos. El banco de pruebas comprueba además que los flancos del LED se
observan **en el pin**, no en el registro.

### 5.3 Resultado de la suite

```
Resumen F1:       124 comprobaciones OK, 0 fallos
Resumen F2:        12 comprobaciones OK, 0 fallos
Resumen F3:        80 comprobaciones OK, 0 fallos
Resumen F4 (DMA):  71 comprobaciones OK, 0 fallos
TOTAL          :  287 comprobaciones OK, 0 fallos
```

---

## 6. Fallos del modelo que ha destapado esta fase

| # | Síntoma | Causa y corrección |
| :--- | :--- | :--- |
| 1 | Con origen de 32 bits y destino de 8, el stream se paraba tras la primera concesión y bloqueaba además a todos los de igual prioridad | La fase de vaciado escribía **una sola ráfaga por concesión**: con la FIFO llena por encima del umbral pero sin hueco para otro elemento de origen, no se podía ni leer ni escribir. Ahora, una vez alcanzado el umbral, se vacía hasta que no quede una ráfaga completa, y se fuerza el vaciado si la FIFO no admite otro elemento de origen |
| 2 | Un stream que no progresa monopoliza el árbitro | Consecuencia del anterior: el árbitro elegía siempre el mismo stream bloqueado. Resuelto en origen; el modelo garantiza además que cada concesión consume tiempo simulado |

---

## 7. Decisiones de diseño de esta fase

| # | Decisión | Motivo |
| :--- | :--- | :--- |
| **F4D-1** | La FIFO se lleva en bytes, no en palabras | El empaquetado entre anchos distintos deja de ser un caso especial: llenar y vaciar son la misma operación con granularidades distintas |
| **F4D-2** | `NDTR` cuenta elementos del ancho de **origen** y baja cuando el dato queda escrito en el destino | Reconcilia el enunciado de [IR, §11.3.1] con el empaquetado (§3.4) |
| **F4D-3** | Un solo hilo por controlador, con una concesión completa por iteración | El paralelismo real de los dos puertos no es observable en un modelo LT; un hilo por stream multiplicaría por ocho el coste sin añadir información |
| **F4D-4** | Las configuraciones inválidas ponen su bandera de error y **no arrancan** el stream | Es lo que hace el silicio, y convierte un error de programación del firmware en un síntoma diagnosticable en lugar de en una transferencia silenciosamente incorrecta |
| **F4D-5** | El puerto de periféricos de DMA1 no recibe identificador de maestro propio | No cruza la matriz, así que no participa en el arbitraje ni en la máscara de conectividad; añadirlo obligaría a ampliar la matriz con una fila que nunca se usa |
| **F4D-6** | `tb_set_request()` como instrumentación explícita, no como registro | Permite verificar los caminos P↔M antes de que existan los periféricos, sin inventar un mecanismo que el silicio no tiene |
| **F4D-7** | Se resuelve a favor de [IR, §11.1.1] la contradicción sobre el acceso de DMA2 a la Flash | Véase §9 |

---

## 8. Trabajo pendiente

**Del propio DMA:**

* **Control de flujo por periférico (`PFCTRL`)**: el bit se acepta y desactiva
  el contador, pero no hay un periférico capaz de señalar el fin de la
  transferencia. El único del F407 que lo usa es el SDIO, de la fase F5; se
  cerrará con él.
* **Reenvío del ACK a los periféricos**: la línea `ack_out` se genera
  correctamente, pero todavía no la consume nadie. Se conectará al implementar
  cada periférico.
* **Errores de FIFO por temporización** (*overrun*/*underrun* reales): en el
  modelo la FIFO se sirve de forma secuencial dentro de cada concesión, así que
  no puede desbordarse por falta de ancho de banda. Solo se detecta la
  incompatibilidad de configuración. Modelarlo exigiría el modo AT de la
  matriz, previsto para F7.
* **`PINCOS`** está implementado como incremento forzado de 4 bytes; falta
  contrastarlo cuando existan periféricos de 32 bits con acceso empaquetado.

**Del resto de la fase F4:** USART, temporizadores avanzados y EXTI/SYSCFG,
junto con el registro de sus funciones alternativas en el multiplexor de pines
(la infraestructura del mux, de F3, no cambia) y el uso efectivo de las líneas
de petición de DMA que este entregable deja ya operativas.

---

## 9. Una contradicción en las fuentes: ¿alcanza DMA2 la Flash?

El informe se contradice:

* **[IR, §6.2]**, tabla de conectividad maestro-esclavo, pone **«No»** en la
  celda *DMA2-M × Flash D*;
* **[IR, §11.1.1]** dice, del DMA2: *«Soporta transferencias memoria-a-memoria
  y acceso a la memoria Flash»*.

Las dos afirmaciones no pueden ser ciertas a la vez, y la diferencia es
observable: de ella depende el caso de uso más habitual del DMA2, copiar una
tabla constante de la Flash a la SRAM.

**Decisión:** se resuelve a favor de §11.1.1 y se añade el camino
DMA2-M/DMA2-P → Flash-D a la máscara de la matriz, con el motivo documentado en
el propio código (`bus/ahb_matrix.h`). Razones: es la afirmación más específica
de las dos, coincide con la arquitectura del silicio y con el uso real del
periférico, y la tabla de §6.2 es un resumen que ya presenta otras
simplificaciones (por ejemplo, la columna de CCM RAM, que el propio informe
aclara en nota). La prueba T27 comprueba explícitamente la copia Flash→SRAM.

La alternativa —respetar §6.2 literalmente— dejaría el modelo incapaz de
ejecutar un patrón de software que sí funciona en la placa, que es exactamente
el tipo de divergencia que un modelo no debe introducir. Si el criterio se
quisiera invertir, basta con quitar `S::FLASH_DCODE` de esas dos filas: el resto
del modelo no depende de ello y la prueba T27 lo detectaría de inmediato.
