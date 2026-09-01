# Fase F5 (parte RTC y perros guardianes) — Periféricos de sistema

Informe de implementación de la **parte de RTC, IWDG y WWDG** de la fase F5 del
plan `doc/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la
fase F4 y a las partes de SPI/I2S, I2C, ADC y DAC de esta misma fase.
Fuentes: `doc/informe_revisado.md` [IR] y `doc/informe_instrucciones.md` [II].

**Alcance de este entregable:** el **reloj de tiempo real** con su dominio de
backup [IR, §12.9] y los **dos perros guardianes**, el de ventana (WWDG)
[IR, §12.10] y el independiente (IWDG) [IR, §12.11].

**Resultado:** los tres bloques están completos y verificados. El modelo compila
sin avisos con `-Wall -Wextra -O2` y la suite pasa **987 de 987 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S + 75 de I2C +
95 de ADC + 65 de DAC + **94 nuevas de RTC y perros guardianes**, código de
salida 0, ~5 s de CPU del anfitrión). La verificación no se queda en los
registros: el calendario **da la vuelta al año y al mes bisiesto**, el perro
independiente **resetea el dispositivo con la onda de reloj del sistema
apagada**, y el de ventana demuestra que refrescar demasiado pronto es tan grave
como no refrescar.

---

## 1. Qué tienen en común estos tres bloques

No son una familia —no comparten registros ni estructura— pero sí comparten la
propiedad que los hace distintos de todo lo modelado hasta ahora: **los tres
siguen funcionando cuando el resto del dispositivo no lo hace**.

* El **RTC** vive en el dominio de backup, alimentado por VBAT y por su propio
  oscilador. Sigue contando con VDD ausente, y un reset de sistema no lo toca.
* El **IWDG** cuenta con el LSI, un oscilador que no depende del árbol de reloj.
  Sigue vigilando aunque el firmware estropee el PLL o pare el SYSCLK.
* El **WWDG** sí depende de PCLK1 —y esa es justamente su limitación— pero
  vigila algo que el otro no puede ver: que el refresco llegue **en su momento**.

Eso obliga a una decisión de modelado común: **ninguno de los tres puede
depender de los flancos de reloj**. Los tres están modelados evento a evento,
como los temporizadores de la fase F4: calculan *cuándo* ocurrirá lo siguiente y
programan una sola cita; los contadores se interpolan al leerlos. Además de ser
barato, es lo que permite apagar la onda cuadrada de los relojes internos
durante las esperas largas sin alterar el resultado — y eso convirtió estos
cuatro grupos de prueba de **37 segundos a 5** de tiempo de anfitrión.

---

## 2. El RTC

`src/periph/rtc.h` (523 líneas), sustituyendo el esqueleto de la fase F1.

### 2.1 El dominio de backup es lo que define el bloque

El RTC es el único periférico que vive fuera del dominio de alimentación
principal, y de ahí salen sus tres rarezas, que no son caprichos sino
consecuencias de estar al otro lado de una frontera de dominio:

1. **Protección por llave.** Los registros están bloqueados; hay que escribir
   `0xCA` y después `0x53` en `RTC_WPR` [IR, §12.9.2]. Cualquier otro valor, en
   cualquier punto de la secuencia, vuelve a cerrar. Y por delante hay un
   segundo cerrojo: `PWR_CR.DBP`, que abre el dominio entero. **Dos cerrojos en
   serie**, porque el dominio sobrevive a los resets y un programa desbocado
   podría corromper la hora de forma permanente.
2. **Modo de inicialización.** El calendario no se puede escribir en marcha: hay
   que pedir `ISR.INIT`, esperar a `INITF` —que es el acuse de la otra orilla—
   y solo entonces cargar `TR` y `DR`.
3. **Todo en BCD.** La hora y la fecha se guardan en decimal codificado en
   binario, para que el firmware las muestre sin dividir.

El banco lo comprueba de la forma más directa posible: escribe los veinte
registros de backup y la fecha, **resetea el MCU**, y mira qué queda.

```
antes del reset de sistema:   25/12/2023 12:34:56 (dia 1)
despues del reset de sistema: 25/12/2023 12:35:07 (dia 1)
```

El calendario ni se enteró — y además siguió contando durante el reset. Con
`BDRST` en `RCC_BDCR`, en cambio, todo vuelve a la fecha de reset (0x2101: 1 de
enero de 2000, lunes).

### 2.2 El calendario, y cómo verificar un año en milisegundos

`ck_spre = RTCCLK / ((PREDIV_A+1) · (PREDIV_S+1))`. Con los valores de reset
(128 × 256) sobre un LSE de 32768 Hz sale **1 Hz exacto**, y así se comprueba.

Pero un calendario a 1 Hz es inverificable en una simulación que dura menos de
un segundo. La solución no es tocar el modelo, sino **usar el hardware como
permite el manual**: bajando los prescaladores a 2 × 2, el «segundo» del
calendario dura 122 µs y se pueden recorrer vueltas de año enteras en
milisegundos. Es una configuración legítima del bloque, no un atajo del modelo.

Con eso se verifica lo que de verdad cuesta acertar en un calendario:

```
arranca en:            31/12/2023 23:59:55 (dia 7)
seis segundos despues: 01/01/2024 00:00:01 (dia 1)
28/02/2024 + 3 s:      29/02/2024 00:00:01   <- 2024 es bisiesto
28/02/2023 + 3 s:      01/03/2023 00:00:01   <- 2023 no lo es
30/04/2024 + 3 s:      01/05/2024 00:00:01   <- abril tiene 30 dias
```

La vuelta de año arrastra el día de la semana (de domingo a lunes), el año en
BCD y la hora. También se verifica el **formato de 12 horas**: las 13:30 se leen
como `01:30` con `PM = 1`, mientras el calendario sigue contando en 24 horas por
dentro.

Los **subsegundos** (`SSR`) son una cuenta descendente desde `PREDIV_S` dentro de
cada segundo, y se interpolan al leerlos en vez de contarse.

### 2.3 Alarmas, despertar, marca de tiempo y manipulación

Las **alarmas A y B** tienen cuatro máscaras cada una. Con `MSK4..MSK1` a uno no
se compara nada y saltan cada segundo; quitándolas se afina hasta un instante
concreto del mes. El banco recorre las dos situaciones y comprueba que la alarma
salta **exactamente en el segundo programado**, y que las dos comparten la línea
17 del EXTI.

El **temporizador de despertar** se mide contra su fórmula:

```
el temporizador de despertar salto a los 48.88 ms (teorico 48.83 ms)
```

La **marca de tiempo** captura el calendario en el flanco del pin RTC_TS, y un
segundo suceso sin atender el primero marca `TSOVF`. La **detección de
manipulación** comparte pin y línea de EXTI, y `TAFCR` es de los pocos registros
que se dejan escribir con el RTC cerrado con llave — cosa que el banco también
comprueba, porque es fácil modelarlo mal.

---

## 3. Los dos perros guardianes

`src/periph/watchdog.h` (387 líneas). No son dos instancias de un bloque: son
**dos bloques distintos que resuelven el mismo problema de formas opuestas**, y
esa es la razón de que el dispositivo lleve los dos.

| | IWDG | WWDG |
| :--- | :--- | :--- |
| Reloj | **LSI (~32 kHz), independiente** | PCLK1 / 4096 / 2^WDGTB |
| Contador | descendente de 12 bits | descendente de 7 bits |
| Se maneja con | **llaves** en `KR` (0xAAAA, 0x5555, 0xCCCC) | bits en `CR`/`CFR` |
| Detecta | que el software se haya parado | que refresque **fuera de tiempo** |
| Avisa antes | no | **sí: EWI, IRQ 0** |
| Sobrevive a un fallo de reloj | **sí** | no |
| Lo para un reset de sistema | **no** | sí |

### 3.1 WWDG: la ventana es lo que aporta

Refrescar el WWDG **demasiado pronto** —con el contador todavía por encima de la
ventana `W`— provoca reset igual que no refrescarlo. Eso detecta un programa que
se ha ido por una rama equivocada y da vueltas de más, que es un fallo que el
perro independiente no puede ver. El banco prueba las dos caras:

* refrescar con `T > W` → reset, visible en `RCC_CSR.WWDGRSTF`;
* esperar a que `T` baje de `W` y refrescar entonces → no pasa nada, y el
  contador vuelve a lo alto.

También se verifica el **aviso temprano**: al llegar a 0x40 salta `EWIF` y con
él la IRQ 0, y a la cuenta siguiente llega el reset. Y que el **bit de
congelación del depurador** para la cuenta, sin el cual detenerse en un punto de
interrupción reiniciaría el dispositivo.

### 3.2 IWDG: la prueba que importa

Las llaves se comprueban una a una: sin escribir antes `0x5555` en `KR`, los
registros `PR` y `RLR` no se dejan tocar; cualquier otro valor vuelve a cerrar
el acceso; y `PVU`/`RVU` avisan de que el cambio está cruzando al dominio del
LSI y **se bajan solas** unas cuentas después.

Pero la prueba que justifica la existencia del bloque es esta: se deja de
refrescar **con la onda de reloj del sistema apagada**, simulando un fallo del
árbol de reloj, y el perro resetea igual.

```
sin refrescar y CON LA ONDA DE RELOJ APAGADA, el IWDG reseto a los 200.0 ms
(plazo 201.0 ms)
```

Dos detalles del silicio que el modelo reproduce y que son fáciles de pasar por
alto:

* **Arrancar el IWDG enciende el LSI por hardware.** Si no fuera así bastaría
  con apagar el oscilador desde `RCC_CSR.LSION` para dejar al dispositivo sin
  vigilancia. El banco lo comprueba: con el perro en marcha, borrar `LSION` no
  apaga el LSI.
* **Un reset de sistema no para al perro independiente.** Una vez arrancado con
  `0xCCCC` solo lo detiene un reset de alimentación, y eso es justo su gracia:
  si el firmware se reinicia en bucle, el IWDG sigue contando. El banco
  comprueba que sigue en marcha después del reset que él mismo provocó.

El plazo se mide contra la fórmula del manual, `t_IWDG = t_LSI · 4 · 2^PR ·
(RL+1)`, incluido el máximo: con `PR = 110` y `RLR = 0xFFF` salen los **32,76 s**
que cita [IR, §12.11].

---

## 4. Tres fallos que estos bloques destaparon

Modelar periféricos que sobreviven a los resets obligó a mirar cosas que hasta
ahora nadie había mirado, y aparecieron tres errores en código ya existente.

### 4.1 El dominio de backup se borraba con cada reset de sistema

`rcc/rcc.h` publicaba `bkp_rst_n = o_bkp_rst_n_ && o_sys_rst_n_`, y el
secuenciador de reset ponía `o_bkp_rst_n_ = false` en **toda** entrada en reset.
Es decir: un NRST, un perro guardián o un `SYSRESETREQ` borraban el RTC, los
veinte registros de backup y la BKPSRAM.

Es lo contrario de lo que dice el manual —y de lo que decía el comentario que
había tres líneas más arriba, «CSR y BDCR sobreviven»—. El dominio de backup
solo lo borran `BDRST` o la pérdida de alimentación [IR, §4.1.3]. Sin esto, el
RTC y la BKPSRAM no tendrían ningún sentido: su única razón de ser es
precisamente sobrevivir. Corregido: el reset del dominio se asocia únicamente a
un reset de alimentación (`por_ok` bajo) o a `BDRST`.

### 4.2 El LSI no se encendía al arrancar el IWDG

El modelo del RCC encendía el LSI solo con `RCC_CSR.LSION`. En el silicio,
arrancar el perro independiente lo enciende por hardware, y esa es una propiedad
de seguridad, no una comodidad. Se ha añadido una petición explícita del IWDG al
RCC (`iwdg_lsi_req`) que se suma a `LSION`.

De paso, esto puso de manifiesto una trampa que afecta a cualquier firmware:
`RCC_CSR` lleva en el mismo registro **las banderas de causa de reset y el bit
`LSION`**. Borrar las banderas con una escritura directa apaga el LSI. El banco
lo hace como debe hacerlo un driver, con lectura-modificación-escritura, y el
comentario lo deja dicho.

### 4.3 `PWR` no tenía banco de registros

`PWR_CR.DBP` es la llave del dominio de backup entero, y el bloque `Pwr` era
todavía un esqueleto de la fase F7. Se ha implementado su **banco de registros**
(`CR` y `CSR`, con `DBP`, `PVDE`/`PLS`, `VOS` y las órdenes de borrado `CWUF` y
`CSBF`), que es lo que el RTC necesita. La máquina de estados de Stop y Standby
sigue siendo trabajo de la fase F7, y así queda anotado en el código.

---

## 5. Verificación

94 comprobaciones nuevas, en cuatro grupos.

| Grupo | Contenido |
| :--- | :--- |
| **T71** | Dominio de backup, los dos cerrojos (DBP y la llave WPR), modo de inicialización, y la supervivencia del calendario y de los veinte registros de backup a un reset de sistema |
| **T72** | `ck_spre` y los prescaladores, calendario BCD con vuelta de año, meses de 30 y 31 días, febrero bisiesto y no bisiesto, formato de 12 horas y subsegundos |
| **T73** | Alarmas A y B con sus máscaras, temporizador de despertar contra su fórmula, marca de tiempo con desbordamiento, manipulación, y las tres líneas de EXTI (17, 21, 22) |
| **T74** | WWDG: fórmula del periodo, cuenta descendente, aviso temprano, reset, **la ventana** y la congelación del depurador. IWDG: las tres llaves, sincronización de dominio, fórmula del plazo, el LSI forzado, **el reset con la onda de reloj apagada** y la supervivencia al reset |

---

## 6. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/rtc.h` | **reescrito** | El RTC completo (523 líneas; antes era el esqueleto de F1) |
| `src/periph/watchdog.h` | **reescrito** | IWDG y WWDG completos (387 líneas) |
| `src/periph/pwr.h` | ampliado | Banco de registros `CR`/`CSR` (§4.3) |
| `src/rcc/rcc.h` | corregido | El dominio de backup sobrevive al reset de sistema (§4.1); salida `lsi_hz`; entrada `iwdg_lsi_req` (§4.2) |
| `src/top/stm32f407vg.h`, `_bind2.h` | ampliados | Señales `s_lsi_hz` y `s_iwdg_lsi` |
| `src/top/sc_main.cpp` | ampliado | Grupos T71-T74 (94 comprobaciones) |

---

## 7. Trabajo pendiente

Del RTC quedan fuera tres cosas, todas anotadas en el código:

* la **calibración**, gruesa (`CALIBR`) y suave (`CALR`): los registros existen y
  guardan lo que se les escribe, pero no alteran la marcha del calendario;
* el registro de **desplazamiento fino** (`SHIFTR`, con `ADD1S`/`SUBFS`) y la
  bandera `SHPF`;
* los **subsegundos de las alarmas** (`ALRMASSR`/`ALRMBSSR`): se guardan y se
  enmascaran, pero la comparación se hace solo sobre el segundo entero.

Ninguna de las tres cambia el comportamiento de un firmware que use el RTC como
calendario y como despertador, que es su uso normal.

De los perros guardianes queda una sola cosa: el **arranque automático del IWDG
por el bit de opción `WDG_SW = 0`** (el «watchdog de hardware») está modelado —la
entrada `hw_start` lo arranca— pero el top la tiene atada a cero porque los bits
de opción de la Flash son trabajo pendiente desde la fase F2.

De la fase F5 quedan por modelar: **bxCAN**, **SDIO** y **CRC/RNG**.
