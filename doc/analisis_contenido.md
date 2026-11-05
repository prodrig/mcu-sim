# El `contenido` de las memorias no volátiles: lo que queda fuera

**Análisis y plan** · 04-11-2026

> **Estado:** análisis, y su implementación en cinco parches, uno por fila de
> la tabla de §6. El atributo `contenido` está descrito en `doc/parts.md` §2.7 y
> en `common/memoria_nv.h`.

`contenido="fichero.hex"` guarda, de una simulación a la siguiente, **lo que
cada memoria declara como sus regiones** (`memoria_nv_if::regiones_nv`). En la
primera versión, cada MCU declaraba esto:

| MCU | Regiones | Lo que quedaba fuera |
| :--- | :--- | :--- |
| STM32F4 (F405/407/415/417/446) | La Flash principal | La OTP y los bytes de opción |
| STM32F3 (F303/F358) | La Flash principal | Los bytes de opción. El F303 **no tiene OTP**: la memoria de sistema y los bytes de opción son todo lo que hay detrás de la Flash [RM0316] |
| MSP432P401R | La Flash principal y el sector 0 de la de información (el buzón) | El TLV (0x0020_1000) y el BSL (0x0020_2000) |
| MSP432E401Y | La Flash y la EEPROM (en 0x400A_F000) | Los registros no volátiles: BOOTCFG, USER_REG0-3 y FMPRE/FMPPE |

Este documento analiza qué costaría añadir cada una, qué cambia cuando se
añade, y en qué orden conviene hacerlo.

---

## 1. Antes de nada: la referencia de una región

La primera versión tenía un único matiz por región, `omite_borrado`: al
guardar, la Flash de un MCU no escribía los registros que eran todo 0xFF. Es lo
que hace que un MB de Flash con un programa de 20 KB sean 20 KB de fichero.

Las regiones nuevas necesitan lo mismo, pero con otro valor:

* los bytes de opción de un STM32 **no salen de fábrica a 0xFF**: RDP es 0xAA;
* el BSL del P4 sale de lo que ponga `bsl=`, y el TLV de lo que ponga `tlv=`
  (o del sintético);
* los registros del E4 salen de fábrica a 0xFFFFFFFF, salvo BOOTCFG
  (0xFFFFFFFE).

Se generaliza: cada región tiene una **referencia**, lo que vale si nadie la ha
tocado. Puede ser ninguna (se guarda entera), un relleno (0xFF para la Flash) o
unos bytes concretos. **Al guardar, lo que es igual a la referencia no se
escribe.** Tres consecuencias:

1. **Los ficheros solo crecen cuando el firmware cambia algo.** Un F407 que no
   toca sus bytes de opción no los lleva en el fichero.
2. **La ida y vuelta es exacta.** Lo que el fichero no dice se queda como
   estaba al construir la memoria, que es justo la referencia.
3. **Es más seguro fuera del simulador.** Un `contenido` es un Intel HEX
   corriente. Si alguien lo graba en una placa de verdad con una herramienta
   que acepte esas direcciones, lo que haya de OTP —irreversible— o de bytes de
   opción —RDP— se programaría de verdad. Que solo salga cuando no es de
   fábrica reduce el riesgo. `doc/parts.md` §2.7 lo avisa.

Hay regiones que no deben guardarse a trozos: un BSL del que solo se guardaran
los 16 bytes cambiados, cargado con otro BSL de origen, sería una mezcla de
dos. Esas regiones son **«todo o nada»**: si algo difiere, se guarda entera.

Y dos momentos nuevos en la interfaz. **Antes de cargar**, el chip fija sus
referencias: el BSL y el TLV que hay en ese instante, ya con `bsl=` y `tlv=`
aplicados. **Después de cargar**, hace lo que el chip de verdad haría con lo
cargado. El fichero se carga con NRST abajo, después del reset de
alimentación, así que el reset que recargaría los bytes de opción ya ha pasado.

---

## 2. STM32F3: los bytes de opción

**Lo que hay.** El modelo es fiel (`mem/flash_f1.h`): los 16 bytes de
0x1FFF_F800 están como en el silicio, ocho medias palabras con su byte y su
complemento (RDP, USER, DATA0, DATA1, WRP0-3). Se cargan en `OBR` y `WRPR` en
cada reset de alimentación y con `OBL_LAUNCH`. `ob_half()` y `pon_ob_crudo()`
ya existen.

**Lo que cambia al guardarlos: mucho, porque el modelo ya los usa todos:**

| Byte | Qué persiste |
| :--- | :--- |
| DATA0, DATA1 | Los dos bytes de usuario: el uso más típico en clase |
| USER.WDG_SW | El IWDG que arranca solo al salir de reset |
| USER.nBOOT1 | Con BOOT0 = 1, memoria de sistema o SRAM |
| USER.nRST_STOP / nRST_STDBY | El reset al entrar en Stop o Standby |
| WRP0-3 | Las páginas protegidas siguen protegidas |
| RDP | El cargador de sistema contesta NACK en el nivel 1; volver al 0 borra la Flash, y eso queda en el fichero |

**El riesgo:** el **nivel 2 de RDP es irreversible**, y el modelo lo respeta:
no deja borrar los bytes de opción. Un `contenido` con nivel 2 deja ese chip
simulado así para siempre. Por eso, **`--restore-rdp[=ID]`** devuelve RDP a su
valor de fábrica (0xAA, nivel 0) justo después de cargar, aunque esté en el
nivel 2, y sin borrar la Flash. **No es lo que hace el chip**: es la salida de
emergencia del simulador, y se dice por la consola. Al cargar un nivel 1 o 2,
la consola lo avisa y recuerda la opción.

**Dificultad:** muy baja. Una región «Bytes de opción» con su referencia de
fábrica y recargar `OBR`/`WRPR` después de cargar.

---

## 3. STM32F4: los bytes de opción y la OTP

### 3.1 Los bytes de opción

**Lo que había:** `OPTCR` copiado tal cual en los cuatro primeros bytes del
bloque de 0x1FFF_C000, con un aviso en el código: «distribución byte a byte…
no disponible en las fuentes». **No era la del chip.** Según RM0090 (la tabla de los bytes de opción del F405/07/15/17), la
disposición es:

* **0x1FFF_C000**: USER en los bits [7:0] (nRST_STDBY en el 7, nRST_STOP en el
  6, WDG_SW en el 5, BOR_LEV en el 3:2; los bits 4, 1 y 0 sin uso) y RDP en
  los [15:8];
* **0x1FFF_C008**: nWRP en los bits [11:0]; los 15:12 sin uso.

Con la copia de `OPTCR`, nWRP caía en 0x1FFF_C002. Un fichero guardado así no
sería el que vuelca STM32CubeProgrammer, y el depurador leía otra cosa. **Se
corrige primero**, con los bits sin uso y los bytes reservados a 1, como la
Flash borrada. Así el bloque de fábrica es todo 0xFF salvo RDP = 0xAA, y
`OPTCR` de fábrica sigue siendo 0x0FFF_AAED. Los bits sin uso **hay que
contrastarlos con un volcado** de una placa real: RM0090 no dice cómo se leen.

**Lo que cambia al guardarlos:**

| Campo | ¿Lo usa el modelo? |
| :--- | :--- |
| BOR_LEV | Sí: el umbral del BOR (T24 de `test407`) |
| nWRP | Sí: WRPERR al programar o borrar un sector |
| WDG_SW | **No lo usaba**: `iwdg.hw_start` iba a `s_false`, con un TODO. Se conecta, como en el F3: con WDG_SW = 0, el IWDG arranca solo al salir de cada reset. Persistir un bit que no hace nada no tendría sentido |
| nRST_STOP / nRST_STDBY | No están modelados en el F4 |
| RDP | Declarado y no aplicado: un nivel 1 o 2 guardado no cierra nada, y la consola lo dice al cargarlo |

`--restore-rdp` vale igual que en el F3. El F446 tiene además SPRMOD (PCROP),
que no está modelado; no estorba para guardar el resto.

**Dificultad:** media-baja. La disposición, unas 20 líneas en `reset_regs` y
`OPTSTRT`; la región; WDG_SW, una señal y un método en `SocF4`. Las suites leen
`OPTCR` por registros, así que no se mueve ningún invariante.

### 3.2 La OTP

**Lo que hay:** el array de 528 bytes en 0x1FFF_7800 (512 de datos en 16
bloques de 32, y 16 bytes de bloqueo en 0x1FFF_7A00). Se programa por la vía
normal de PG y no se borra nunca. **Lo que faltaba:** el bloqueo. Según RM0090, un
bloque no se puede programar una vez que su byte de bloqueo está a
0x00. Es un error de programación que el modelo no daba. Ahora da WRPERR: el
manual no dice cuál, y es el de un sector protegido.

**Lo que cambia al guardarla:** lo que el firmware graba ahí —un número de
serie, una calibración— sobrevive, y los bloques bloqueados siguen bloqueados.
Nada más del modelo lee la OTP.

**Dificultad:** baja. Región con relleno 0xFF y el bloqueo en `program_write`.

**La salida de emergencia:** como RDP, una OTP gastada en un `contenido` no
tiene vuelta en el chip. **`--restore-otp[=ID]`** la devuelve a la de fábrica
—todo a 0xFF, también los bytes de bloqueo— justo después de cargar el
`contenido` de ese F4, y la consola dice cuántos bytes y bloqueos había. Sin
`contenido` es un error: la OTP ya arranca de fábrica.

---

## 4. MSP432E401Y: los registros no volátiles

El E4 no tiene bytes de opción ni OTP ni TLV, y su cargador está en ROM, no en
la Flash: no hay BSL que guardar. **Lo que tiene son registros no volátiles**,
en el espacio del SYSCTL, que el firmware escribe y después **fija** con la
orden COMT del controlador de la Flash. Lo fijado es lo que vale en cada reset
(`periph/msp432e4/flash.h`):

| Registro | Dirección | Qué es | ¿Lo usa el modelo? |
| :--- | :--- | :--- | :--- |
| BOOTCFG | 0x400F_E1D0 | El pin y el nivel que piden el cargador de la ROM, la llave de la Flash (KEY) y si está fijado (NW) | Sí: `arranque_en_rom()` y la llave de FMC |
| USER_REG0-3 | 0x400F_E1E0 | Cuatro palabras del usuario. En las LaunchPad de TI, USER_REG0/1 llevan la MAC de fábrica, y los ejemplos de Ethernet la leen de ahí | No: son del firmware |
| FMPRE0-15 | 0x400F_E200 | Protección de lectura, un bit por bloque | Sí: leer como dato falla, ejecutar no |
| FMPPE0-15 | 0x400F_E400 | Protección de programación | Sí: ARIS |

Hasta ahora, «para siempre» era la vida del modelo: lo decía su propio
comentario, «no hay fichero detrás». **Con `contenido`, lo fijado se guarda**
en sus direcciones del mapa, como la EEPROM en la ventana de sus registros. La
referencia es la de fábrica, así que un E4 que no ha fijado nada no los lleva
en el fichero. Al cargar, lo fijado vuelve a valer, como tras un reset.

**Lo que no se hace:** el desbloqueo por el puerto de depuración, que en el
chip devuelve todo esto a fábrica con un borrado total. Es el equivalente del
`--restore-rdp` de los STM32, y queda anotado. La MAC de fábrica en
USER_REG0/1 tampoco: el modelo sale de fábrica con ellos a 0xFFFFFFFF, como un
chip suelto.

**Dificultad:** baja. Cuatro regiones pequeñas y acceso a lo fijado.

---

## 5. MSP432P401R: el TLV y el BSL

Aquí la dificultad no es técnica: el FLCTL ya maneja la información entera.
**Es de diseño: quién manda sobre esos bytes**, porque ya los fijan `tlv=` y
`bsl=`.

### 5.1 El TLV (0x0020_1000, 4 KB)

**Ni el silicio ni el modelo dejan que el firmware lo cambie** (`es_tlv()`
rechaza borrarlo y programarlo). Guardarlo no conserva nada que la simulación
haya hecho. Lo que sí importa es **un volcado real**: quien use de `contenido`
la información volcada de una placa con UniFlash recibía «0x00201000 no es de
la memoria», y el fichero no se tocaba.

Se trata como una región **de solo lectura y todo o nada**, con referencia en
lo que hay antes de cargar:

* **al cargar**, si el fichero trae TLV, se valida igual que `tlv=` (DEVINFO
  detrás de la suma y la cadena hasta la etiqueta de fin) y **se adopta**: es
  el TLV de esa pieza, y la calibración del DCO se vuelve a leer;
* **pero si se dio `tlv=`**, manda el `tlv=`: el del fichero no se adopta y la
  consola lo dice;
* **al guardar**, solo se escribe si difiere de lo que había antes de cargar.
  Es decir, si vino del fichero: así se conserva la ida y vuelta de un volcado.

### 5.2 El BSL (0x0020_2000, 8 KB)

**A diferencia del TLV, el firmware sí puede cambiarlo**, si quita su
`BANK1_INFO_WEPROT`: una actualización del BSL, o un borrado total con la
información desprotegida. Sin guardarlo, ese cambio se perdía.

No se guarda siempre por dos razones:

1. Cada fichero llevaría 8 KB del B1 de esa versión de `mcu-sim`, y con un B1
   nuevo los ficheros viejos seguirían cargando el antiguo sin que se notara.
2. Cambiar `bsl=` en el XML no tendría efecto mientras el fichero trajera un
   BSL.

Se trata como una región **todo o nada con referencia en el BSL de origen**:
se guarda solo si el firmware lo ha cambiado. Al cargar, un BSL que viene en
el fichero **gana** a `bsl=`, porque es un estado posterior: el chip ya lo
tenía. La consola lo dice, y el origen pasa a ser «el del contenido» en lo que
cuenta el chip de sí mismo (`--memorias`, el aviso de arranque). Para volver
al de `bsl=`, se quita del fichero o se usa otro `contenido`.

**Dificultad:** media. Las referencias se fijan antes de cargar; el TLV se
valida y se aplica después; el BSL lleva un cuarto origen.

---

## 6. Plan, en el orden en que se hace

| Parche | Qué | Toca | Invariantes |
| :--- | :--- | :--- | :--- |
| 1 | **La referencia**: ninguna, relleno o bytes, y todo o nada; los dos momentos, antes y después de cargar | `common/memoria_nv.h`, las regiones que ya había, `top/sim_main.cpp` | No se mueven: las regiones de antes guardan lo mismo |
| 2 | **Los bytes de opción del F3 y del F4**, con la disposición real del F4, su WDG_SW, y **`--restore-rdp[=ID]`** | `mem/flash_f1.h`, `mem/flash_if.h`, `top/soc_f4.h`, los dos adaptadores, `top/sim_main.cpp` | No: de fábrica, WDG_SW = 1 y nada arranca solo |
| 3 | **La OTP del F4**, con su bloqueo | `mem/flash_if.h`, `soc/stm32f4_mcu.h` | No |
| 4 | **Los registros no volátiles del E4** | `periph/msp432e4/flash.h`, `soc/msp432e4/msp432e4_mcu.h` | No |
| 5 | **El TLV y el BSL del P4** | `periph/msp432p4/flctl.h`, `soc/msp432p4/*`, `top/sim_main.cpp` | No |

Cada parche amplía `verif/gui/contenido.py` con sus comprobaciones, contra el
`mcu-sim` de verdad, y pone al día `doc/parts.md` §2.7 y lo que cuenta
`--memorias` de cada chip.

---

## 7. Lo que queda abierto

1. **Los bits sin uso de los bytes de opción del F4**, a contrastar con un
   volcado de una placa (§3.1).
2. **RDP no se aplica en el F4**: ni el nivel 1 cierra la Flash al depurador
   ni el 2 lo apaga. En el F3 solo se aplica en el cargador. Guardarlo es
   correcto, pero no tiene más efecto que ese.
3. **El desbloqueo del E4** por el puerto de depuración (§4).
4. **SPRMOD/PCROP del F446**, sin modelar.
5. **La memoria de sistema** de los STM32 (el cargador de ST) y la ROM del E4
   son ROM: no se guardan, y no deben.
