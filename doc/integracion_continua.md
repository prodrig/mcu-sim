# Integración continua: las tres suites en cuatro plataformas

`.github/workflows/suites.yml` ejecuta, en cada empujón a `main` y en cada
petición de fusión, las tres suites sobre **Linux, macOS Apple Silicon, macOS
Intel y Windows/MSYS2**.

---

## 1. Qué se comprueba, que no es lo obvio

Que las suites pasen lo comprueba cualquiera. Lo que se comprueba aquí es que
**el tiempo simulado no se mueva**, al picosegundo:

```
test407   2118 comprobaciones   2336217899213 ps
test446    204                  1033367277932 ps
test417    165                   718988288 ps
```

Un cambio puede dejar las 2 118 comprobaciones en verde y haber cambiado el
comportamiento del modelo. Lo que lo delata es el reloj. Por eso esas tres
cifras están **una sola vez** en el repositorio, en `src/verif/invariantes.txt`,
y `ci/comprueba_invariante.sh` las contrasta contra la salida real. Si no
coinciden, el trabajo falla. No avisa: falla.

**Esto no habría sido posible hace una semana.** Mientras los `.bin` de
`verif/fw` no se versionaban, cada máquina compilaba su propio firmware y
ejecutaba, por tanto, un programa distinto; el tiempo simulado no era
comparable entre máquinas y un CI multiplataforma habría dado rojo
permanentemente por la razón equivocada. Está en **T-22** de `doc/todo.md`.
`verif/huellas.txt` garantiza la mitad de la propiedad —el mismo binario en
todas partes— y `verif/invariantes.txt` la otra —el mismo resultado—.

---

## 2. Los cuatro trabajos

| Trabajo | Máquina | SystemC | Qué añade |
| :--- | :--- | :--- | :--- |
| `rapidas` | Ubuntu | no hace falta | `red`, `macros-win`, `hash`, `cryp`, `vectores`, `serie`, `rfc2217` y `gui-proto`. Menos de un minuto; evita encender cuatro máquinas por una errata. Es el único que clona **también `mcu-sim-gui`**, para comparar las cabeceras que los dos comparten por copia (`protocolo.h` y `proto_io.h`): se compara con su rama principal, así que un cambio del protocolo se sube **primero allí** |
| `linux` | `ubuntu-latest` | `apt install libsystemc-dev` | Verifica **la vía que documenta `compilacion.md` §4**. Si el paquete desaparece de Ubuntu, quiero enterarme aquí y no en el portátil de un alumno |
| `macos` | `macos-26` y `macos-26-intel` | compilada, en caché | **La plataforma que nadie ha ejecutado nunca.** Las dos arquitecturas, porque el `Makefile` tiene una rama para cada una y una rama que nadie ejecuta no está verificada |
| `windows` | `windows-latest` + MSYS2 | compilada, en caché | Las suites **y** que `mcu-sim.exe` no arrastre ninguna DLL de MinGW, que es lo de §5.6 |

`macos` y `windows` pasan además `make red gui-proto`: la capa de red es lo
único del modelo que sabe en qué sistema corre, y en Linux ya se prueba en
`rapidas`.

`linux`, `macos` y `windows` pasan también `verif/gui/saludo.py` y
`verif/gui/marcha.py` (`make gui-saludo gui-marcha`), detrás de la
interoperabilidad del puente UART y por el mismo motivo: necesitan el `mcu-sim`
de verdad. Fuera de Linux se instala `psutil` para poder medir la CPU de
`mcu-sim` mientras espera; en Linux basta `/proc`. `marcha.py` tarda unos
segundos: dos de sus grupos van con `--tiempo-real`, para tener tiempo de pared
con que hablar con la simulación en marcha.

`fail-fast: false` en la matriz de macOS: que una arquitectura falle no debe
ocultar lo que hace la otra.

La biblioteca se cachea por plataforma. La primera ejecución de cada una tarda
unos minutos más; las siguientes, no.

### Por qué `ENABLE_PTHREADS` en dos de ellas

SystemC implementa sus procesos con **QuickThreads**, que lleva ensamblador
específico por arquitectura. En dos sitios se le pide que use *pthreads* en su
lugar:

* **macOS Apple Silicon**, por precaución. Si la primera ejecución demuestra que
  QuickThreads funciona en arm64, se quita.
* **MinGW-w64**, por un problema de enlazado conocido (incidencia 3 del
  repositorio de Accellera). El GCC de MSYS2 usa el modelo de hilos POSIX, así
  que winpthreads está ahí de todas formas.

Tiene un efecto secundario que interesa: si el invariante sale **idéntico con
dos paquetes de corrutinas distintos**, queda demostrado que el tiempo lo lleva
el planificador y nada más. Es una predicción, no un resultado: lo dirá la
primera ejecución.

---

## 3. Subir el repositorio a GitHub sin perder el historial

`git push` de un repositorio que ya existe **conserva los 102 commits con sus
fechas, sus autores y sus mensajes**. No hay que clonar nada ni copiar ficheros.

> **Lo que NO hay que hacer:** crear el repositorio en GitHub y arrastrar los
> ficheros a la interfaz web, o usar «Add file → Upload files». Eso crea **un
> commit nuevo** con todo el contenido y tira el historial a la basura.

Con `gh` (viene con Git para Windows o se instala con `winget install
GitHub.cli`), desde la carpeta del repositorio:

```bash
gh auth login
gh repo create mcu-sim --public --source=. --remote=origin --push
```

Sin `gh`: crear el repositorio **vacío** en github.com —sin README, sin
`.gitignore`, sin licencia, porque cualquiera de las tres crea un commit que
luego estorba— y después:

```bash
git remote add origin https://github.com/<usuario>/mcu-sim.git
git push -u origin main
git push origin --tags        # hoy no hay etiquetas; el día que las haya
```

Y comprobarlo, que es el paso que se salta todo el mundo:

```bash
cd /tmp && git clone https://github.com/<usuario>/mcu-sim.git comprobacion
cd comprobacion && git rev-list --count HEAD     # tienen que ser 102
cd src && make -f Makefile.mcu-sim test407       # y 2118 / 2336217899213 ps
```

Ese último paso es el que de verdad verifica la migración: un clon limpio, sin
compilador cruzado, tiene que dar el invariante. Es exactamente lo que va a
hacer el CI.

### Tres decisiones que hay que tomar ANTES del primer empujón

**1. Público o privado.** Los minutos de macOS y Windows son gratis e
ilimitados en repositorios **públicos**; en privados salen del cupo mensual y
los de macOS son los más caros del catálogo. Para este proyecto —un simulador
didáctico que se va a repartir a alumnos— público es lo natural, pero es una
decisión, no un trámite.

**2. El correo del historial.** Los 102 commits llevan
`p.rodriguez.ballester@gmail.com` en el campo de autor. Al hacer público el
repositorio, ese correo queda **públicamente indexable**. Si eso no interesa,
hay que decidirlo ahora: cambiarlo después obliga a reescribir los 102 commits
con `git filter-repo`, lo que cambia **todos** los identificadores y rompe
cualquier enlace que ya se hubiera compartido. GitHub ofrece una dirección
`@users.noreply.github.com` y la opción de rechazar empujones que expongan el
correo real.

**3. Una licencia.** El repositorio no tiene ninguna. Publicar sin licencia
significa «todos los derechos reservados»: nadie puede legalmente usarlo ni
modificarlo, lo cual choca de frente con repartirlo a alumnos. El material de
terceros ya trae la suya —`verif/fw/cmsis/LICENSE-*.md` y
`verif/fw/coremark/LICENSE.md`— pero el código del modelo, no.

**Lo que ya está bien y conviene no tocar:** `doc/refs/` está en el
`.gitignore`. Ahí viven los manuales de ST, que son material con derechos de
autor y **no se pueden redistribuir**. Al pasar a público eso deja de ser una
cuestión de tamaño y pasa a ser una cuestión legal. `doc/fuentes.md` —el
índice, con revisión y URL de cada documento— sí se versiona, que es la forma
correcta de citarlos.

---

## 4. Qué puede fallar la primera vez

Esto no se ha ejecutado nunca. Lo honesto es enumerar dónde está el riesgo, en
vez de presentarlo como terminado:

| Sospecha | Cómo se verá | Qué hacer |
| :--- | :--- | :--- |
| La etiqueta `2.3.4` del repositorio de Accellera no se llama así | `git clone --branch 2.3.4` falla diciéndolo | Corregir `SYSTEMC_VER` en el workflow |
| SystemC 2.3.4 no compila con clang de macOS 26 | Error de compilación en el paso de SystemC | Probar sin `ENABLE_PTHREADS`, o subir a SystemC 3.0 (**I-24**) |
| `libsystemc-dev` ya no está en Ubuntu | `apt-get install` falla | Usar `ci/construye_systemc.sh` también en Linux, **y corregir `compilacion.md` §4**, porque entonces la documentación estaría mintiendo |
| El `make` de macOS (3.81) no entiende algo del `Makefile` | Error de sintaxis de make | Instalar `gmake` con Homebrew en el trabajo |
| El invariante **no coincide** en macOS | `[FALLO] tiempo simulado ...` | **Éste es el interesante.** No tocar `invariantes.txt`: averiguar por qué |

Ese último caso es el motivo de todo esto. Si el tiempo simulado sale distinto
en macOS con el mismo firmware, hay una diferencia real de comportamiento entre
plataformas y hay que entenderla antes de dar macOS por buena. Cambiar la cifra
esperada para que el CI se ponga verde sería exactamente la clase de arreglo
que este proyecto no hace.

### 4.1 Lo que falló de verdad, que no estaba en la lista

Dos ejecuciones, dos fallos, **y ninguno de los dos aparecía arriba**. La lista
se queda como estaba, sin retocar a posteriori: una previsión que se corrige
después de ver el resultado no es una previsión.

**Primera: el trabajo rápido, el que no necesita SystemC.** `make vectores`
llama a `comprueba_vectores.py`, que devuelve 1 si algún caso queda confirmado
por **un solo motor**. En una máquina recién hecha no está `pycryptodome`, así
que los 32 casos de AES, DES y TDES se quedaron solo con `openssl`. El script
tiene razón —un vector que solo confirma openssl es openssl comparándose
consigo mismo— y el que estaba mal era el workflow, al que le faltaba la
dependencia. Corregido instalándola, no relajando el criterio.

**Segunda: los tres trabajos que construyen SystemC, idénticamente.**

```
CMake Error at CMakeLists.txt:209 (cmake_minimum_required):
  Compatibility with CMake < 3.5 has been removed from CMake.
```

SystemC 2.3.4 es de 2022 y pide un CMake anterior al 3.5; **CMake 4 ha retirado
esa compatibilidad**. Linux no lo sufre porque allí la biblioteca viene de
`apt`. Se añade `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, que es la salida que el
propio CMake propone, y queda anotado en **I-24** como el primer argumento
concreto para subir a la línea 3.0: hasta ese día no había ninguno.

De paso se cambió el clon superficial por uno completo con `checkout` de la
etiqueta. El aviso `refs/tags/2.3.4 ... is not a commit!` que salía en los tres
logs era benigno —la etiqueta sí se sacaba— pero dejaba en el aire si lo
construido era la etiqueta o la rama por omisión, que hoy es la 3.0. Ahora el
script imprime qué fuente ha usado.

**Tercera ejecución: macOS Apple Silicon pasa.** Las tres suites, los tres
invariantes. Es la tercera plataforma que da las mismas cifras, y la primera
que las da con un **paquete de corrutinas distinto** —pthreads en vez de
QuickThreads—, lo que confirma lo que se esperaba pero no se había medido: el
tiempo simulado lo lleva el planificador de SystemC y nada más.

Y dos fallos más, los dos míos y de signo contrario:

* **Windows**: le pedí `ENABLE_PTHREADS=ON` y el `CMakeLists` de SystemC lo
  **prohíbe explícitamente** (`Pthreads is not supported on Windows`). Allí el
  paquete por omisión es Fiber, de la API de Win32, y funciona. Lo puse por una
  incidencia de MinGW que leí por encima.
* **macOS Intel**: con QuickThreads, el enlazado muere con `Undefined symbols`.
  `sc_cor_qt.cpp` declara dos símbolos de ASan como **referencias débiles**, un
  modismo de ELF que el enlazador de Mach-O no acepta. O sea que **QuickThreads
  está roto en macOS con SystemC 2.3.4**, en las dos arquitecturas; en
  Apple Silicon no se vio porque allí ya llevaba pthreads por otro motivo. Esa
  precaución acertó por una razón que no era la suya, y sigue sin saberse si
  QuickThreads funciona en arm64: nunca se le ha dejado intentarlo.

### 4.2 Y el que no estaba en el entorno

Los cuatro fallos anteriores estaban fuera del modelo. El quinto, no.

Al pasar macOS a la SystemC de Homebrew —que es la 3.0.2— **la suite dejó de
terminar**. El primer diagnóstico fue *«se cuelga en T24»*, y **era falso**:
T24 era sencillamente lo último que se veía, porque el aviso de `SC_REPORT`
sale por `stderr`, sin búfer, y las comprobaciones salen por `stdout`, con
búfer de bloque. Con `stdbuf -oL` el cuelgue apareció donde estaba: en **T25**,
dentro del `delay_ms` del blinky, con el proceso al 100 % de CPU dentro de
`SysTick::tick_proc`.

**La causa era del modelo.** Los contadores que se interpolan desde el tiempo
simulado —SysTick, IWDG, WWDG, subsegundos del RTC— calculan los ticks con un
`double` que vale decenas de millones, y el SysTick le sumaba una guarda de
redondeo de `0.5e-9` ticks **cuando el error del propio `double` a esa escala
es de unos 3e-9**. De qué lado cae el truncamiento depende del último bit de
`to_seconds()`, y ese bit cambió entre versiones:

| | `to_seconds()` de 69 000 000 000 ps | `× 168 MHz` |
| :--- | :--- | :--- |
| SystemC 2.3.4 | `0,069000000000000005773` | `11592000,000000002` → **11592000** |
| SystemC 3.0.2 | `0,068999999999999991895` | `11591999,999999998` → **11591999** |

Un ULP, en direcciones opuestas. Con la 3.0.2 el contador se quedaba un tick
corto, el cruce calculado caía en el mismo picosegundo que el instante actual
—luego no se esperaba— y el `continue` giraba en ciclos delta para siempre.

Corregido en `common/guarda_tick.h`: guarda de `1e-6` de tick, cinco órdenes
por encima del error del `double` y seis por debajo de un tick, o sea por
debajo de la resolución del simulador. Y un seguro en `tick_proc` para que un
camino que no espera no pueda existir.

**El resultado es mejor que el arreglo.** Las tres suites dan ahora lo mismo
con las DOS versiones de SystemC, al picosegundo:

| | 2.3.4 | 3.0.2 |
| :--- | ---: | ---: |
| `test407` | `2336217899213 ps` | `2336217899213 ps` |
| `test446` | `1033367277932 ps` | `1033367277932 ps` |
| `test417` | `718988288 ps` | `718988288 ps` |

Es decir: **el invariante ha dejado de depender de la versión de SystemC**, y
el CI puede usar en cada plataforma lo que esa plataforma empaqueta —`apt` en
Linux, Homebrew en macOS— sin renunciar a comparar. De paso quedó destapado
**T-23**: el IWDG resetea un tick antes de su plazo, y la comprobación que lo
vigila tiene diez veces más tolerancia que el error.

### 4.3 El sexto, y lo encontró un instrumento que se añadió para otra cosa

Con el redondeo corregido, macOS Intel pasó a ejecutar las tres suites en **159
segundos** —de no terminar en 45 minutos— con las 2 118 comprobaciones en
verde. Falló una sola cosa: el tiempo simulado, `2336617899213 ps` frente a
`2336217899213`. **0,4 ms de más.**

Comparadas las dos salidas línea a línea, **de 2 548 líneas la única distinta
era esa**. Ni una comprobación con otro valor. Y el desglose que la suite
imprime desde la investigación de T-22 lo situó en una línea:

| | `T96+T97` (socket de GDB) | el resto |
| :--- | ---: | ---: |
| Linux, Windows, macOS arm64 | 96 665 875 ns | `2239552024213 ps` |
| macOS Intel | **97 065 875 ns** | **`2239552024213 ps`** |

Los 400 µs están **enteros** dentro de los dos grupos que hablan con un `gdb`
de verdad por un socket de verdad: cuatro sondeos más de los 100 µs simulados
que el stub tarda en atender el socket. Es **T-16**, documentado desde la fase
F6 como «no es un problema de corrección sino de ritmo» — y resulta que también
consume tiempo simulado que depende del anfitrión, cosa que no estaba dicha.

**La parte que da gusto**: ese desglose se añadió para comprobar una hipótesis
que resultó **falsa** —se creía que el socket explicaba los 2 ms de diferencia
del F407 entre máquinas, y era el binario del firmware—. Se dejó puesto con el
argumento de que «separa una cifra que sí podría haber variado». Hoy varió, y
lo localizó en una línea.

**La corrección** es del criterio, no del modelo: `verif/invariantes.txt` tiene
ahora una cuarta columna y la suite del F407 contrasta `resto` en vez de
`total`. No afloja nada —quita exactamente la parte que el proyecto ya tenía
documentada como dependiente del anfitrión, y deja dentro todo lo que decide el
modelo— y el total se sigue imprimiendo para el registro.

**Lo que esto dice del CI**, y es la conclusión que aguanta seis casos: ha
encontrado en cinco ejecuciones seis cosas que tres máquinas de desarrollo no
habían visto en meses. Cuatro estaban en el entorno. **La quinta estaba en el
modelo, llevaba ahí desde siempre, y ninguna de las 2 118 comprobaciones la
veía. La sexta estaba en el propio criterio**: el número que el proyecto
llamaba invariante no era del todo suyo.

---

## 5. El estado al cierre, 23 de septiembre de 2026

Séptima ejecución, la primera sobre el repositorio republicado y con la
historia reescrita. **Los cinco trabajos en verde**, y estos son los tiempos
de pared:

| Trabajo | SystemC | Tiempo |
| :--- | :--- | ---: |
| Sin SystemC (red, macros de Windows, vectores) | — | **10 s** |
| macOS Apple Silicon | 3.0.2, Homebrew | **1 min 53 s** |
| Linux (g++, `libsystemc-dev`) | 2.3.4, apt | **2 min 40 s** |
| macOS Intel | 3.0.2, Homebrew | **2 min 53 s** |
| Windows (MSYS2 / MinGW-w64) | 2.3.4, compilada | **6 min 12 s** |

Seis minutos de Windows contra dos de los demás: es el único que **compila
SystemC**, porque es la única plataforma sin paquete. Y macOS Intel, que en la
ejecución de los pthreads no había terminado en 44 minutos, tarda ahora menos
de tres.

**Lo que esta ejecución valida, y no es el traslado de los informes.** Es la
primera que contrasta **el invariante movido por T-23** —`2240553274213 ps` en
la columna `resto`— fuera de Linux. La corrección del perro se había medido
aquí con SystemC 2.3.4 y 3.0.2, pero las dos son la misma máquina y el mismo
compilador. Ahora la misma cifra sale en **cuatro plataformas, dos versiones de
SystemC, dos compiladores y tres sistemas operativos**, al picosegundo.

Dicho de otra forma: el cambio de T-23 movió el invariante **exactamente lo que
tenía que moverlo**, y eso ya no es una afirmación de una máquina.

### Dos avisos con fecha que el propio CI imprime

**`ubuntu-latest` pasa a Ubuntu 26 el 19 de octubre de 2026.** El trabajo de
Linux se apoya en `libsystemc-dev` de Ubuntu, así que ese día puede cambiarle
la versión de SystemC debajo sin que nadie toque nada. **No es alarmante y por
una razón medida**: desde I-24 sabemos que los tres invariantes valen lo mismo
con la 2.3.4 y con la 3.0.2. Pero si ese día el trabajo de Linux se pone rojo,
lo primero que hay que mirar es qué versión trae el paquete nuevo — y **no se
toca `verif/invariantes.txt`** hasta saberlo.

**Node 20 está obsoleto** y `actions/checkout@v4`, `cache` y `upload-artifact@v4`
se ejecutan ya forzados sobre Node 24. Funciona, pero es prestado: subirlas a
la v5 sigue siendo el punto **b** de P-13.

---

## 6. Desde el 26 de septiembre de 2026, el CI también construye lo que se reparte

Cada plataforma empaqueta su ejecutable y una etiqueta `v*` lo publica como
*Release*. El `needs` del trabajo que publica incluye a todos los demás, así que
**un binario no se publica si su plataforma no pasó las suites y los tres
invariantes**. La guía para el alumno es `doc/ejecutables.md`.

Y el trabajo de Linux pasa a ser una **matriz de dos distribuciones**,
`ubuntu-22.04` y `ubuntu-latest`. La primera es el **suelo** que el ejecutable de
Linux declara soportar, y una plataforma soportada que nadie ejecuta no está
verificada: es exactamente la lección de I-23 con macOS.

### Lo que salió de hacerlo, que era el motivo de hacerlo

**Una tercera versión de SystemC, gratis.** Ubuntu 22.04 empaqueta la **2.3.3**,
no la 2.3.4 de 24.04. Medido aquí antes de escribir el `workflow` —2.3.3
construida aparte, modelo compilado con `g++-11`, que es el de 22.04, sin un solo
aviso—, las tres suites dan **las mismas cifras al picosegundo**:

| | 2.3.3 + GCC 11 | 2.3.4 | 3.0.2 |
| :--- | ---: | ---: | ---: |
| `test407`, resto | 2240553274213 ps | 2240553274213 ps | 2240553274213 ps |
| `test446` | 1033367277932 ps | 1033367277932 ps | 1033367277932 ps |
| `test417` | 718988288 ps | 718988288 ps | 718988288 ps |

**Y un defecto de diseño del reparto, que este trabajo destapó antes de que lo
hiciera un alumno.** El paquete de 22.04 instala `libsystemc-2.3.3.so` y el de
24.04 `libsystemc-2.3.4.so`: **SONAME distintos**. Un ejecutable de Linux
enlazado dinámicamente contra uno no encuentra el otro, así que repartirlo así
**no habría funcionado ni entre dos Ubuntu**. De ahí que el de Linux lleve
SystemC estática dentro, más `-static-libgcc -static-libstdc++`, y que lo único
que quede fuera sea glibc —con una comprobación de `objdump -T` que falla si el
suelo pasa de 2.35—.

En macOS, en cambio, SystemC **no** va dentro: el alumno la instala con
`brew install systemc`. Eso mantiene el paquete pequeño y tiene una consecuencia
que no estaba buscada —la biblioteca no se redistribuye, así que la sección 4 de
la Apache-2.0 no entra en juego en ese paquete—. Donde sí entra, el paquete lleva
su `LICENSE` y su `NOTICE`; el inventario completo está en `TERCEROS.md`.

---

## 7. La primera Release, y las cuatro cosas que salieron mal

`v0.1.0` se publicó el 28 de septiembre de 2026. Merece quedar escrito lo que
costó, porque **ninguno de los cuatro tropiezos estaba en el modelo** y tres de
ellos son de la clase que se repite si no se anota.

### 7.1 La etiqueta empujada desde el repositorio equivocado

El `git tag` y el `git push` se hicieron en la copia **vieja** —la que conserva
la historia sin filtrar— en vez de en la publicada. Y empujar una etiqueta
empuja también todos los objetos que necesita, así que **los tres informes
volvieron a GitHub**, dentro de los 120 commits que la etiqueta arrastraba.

Se detectó en minutos, se borró la etiqueta y se volvió a borrar y recrear el
repositorio, que es lo único que garantiza que no queden objetos inalcanzables
servidos por SHA.

**La causa de fondo no fue el despiste**, sino tener dos carpetas casi
homónimas —`mcu-sim` y `mcu-sim-publico`—, una con la historia limpia y otra con
la sucia, **y las dos con `origin` apuntando al mismo sitio**. Quitarle el
remoto al archivo es la mitad del arreglo; que no se llame parecido es la otra
mitad.

### 7.2 `git commit -a` no añade ficheros nuevos

El commit de los ejecutables llegó **sin `TERCEROS.md`, sin `doc/ejecutables.md`
y sin `doc/notas_release.md`**: existían en disco, pero sin seguimiento. `-a`
registra modificaciones y bajas de lo que ya estaba versionado, y nada más.

No se notó al empujar porque el CI de `main` pasó igual —esos tres ficheros solo
los usa el empaquetado—, y habría reventado al publicar, en el `cp` y en el
`--notes-file`.

### 7.3 Finales de línea

Los siete ficheros modificados acabaron en CRLF en la copia de trabajo mientras
el repositorio los guarda en LF. Se vio porque `git diff` marcaba 8 082 líneas
cambiadas y **`git diff --ignore-cr-at-eol` salía vacío**: ni un carácter de
contenido distinto.

Si eso se comete, `ci/*.sh` entra con CRLF y los trabajos de Linux y Windows
mueren con un `bad interpreter`. Se deshizo con `git checkout -- .` tras poner
`core.autocrlf false` en esa copia, que es coherente con lo que el
`.gitattributes` ya decidió: **no** normalizar en masa.

### 7.4 El fallo de macOS Intel que no sabemos explicar

El mismo commit, el mismo workflow y el mismo tipo de runner: **tres minutos en
la ejecución de `main` y más de treinta en la de la etiqueta**, agotando el
límite del trabajo. Al relanzar solo ese trabajo, sin tocar una línea, pasó.

**No es reproducible y la causa sigue abierta.** Las dos hipótesis son la
contención —los dos trabajos gemelos corrían a la vez sobre el mismo commit— y
una diferencia en lo que sirvió Homebrew. Ninguna está demostrada.

**Y lo peor no fue el fallo, sino que nos quedamos sin el cuerpo.** Cuando
`timeout-minutes` mata un trabajo, GitHub no archiva su log —`gh run view
--log` contesta `log not found`— y se lleva por delante los pasos que faltaban,
incluido el `upload-artifact` que tiene `if: always()` justamente para esto. El
CI no podía investigar su propio fallo más caro.

### 7.5 Lo que se ha cambiado, que es lo único que evita el siguiente

**`ci/pasa_suites.sh`**, que ejecuta las tres suites con dos propiedades nuevas:

- **Presupuesto propio por suite**, más corto que el límite del trabajo. Si se
  agota, falla *la etapa* en vez de morir *el trabajo*, el `upload-artifact` sí
  se ejecuta y quedan los tres `.log` con el desglose por grupos —que es lo que
  distingue «uniformemente lento» de «parado en T25»—. Y **se detiene en la
  primera suite que agota su presupuesto**, para no comerse el límite del
  trabajo con las otras dos.
- **Sin búfer de bloque** (`stdbuf -oL`). Ya evitó un diagnóstico falso una vez:
  el cuelgue que parecía estar en T24 estaba en T25, y T24 solo era lo último
  que se *veía*.

En macOS hace falta `brew install coreutils`, porque BSD no trae ni `timeout` ni
`stdbuf`; llegan como `gtimeout` y `gstdbuf` y el script busca los dos nombres.
Si no encuentra ninguno **lo dice en voz alta y sigue sin presupuesto**: una
protección que se desactiva en silencio es peor que no tenerla.

**Y el grupo de concurrencia pasa a ser por `github.sha`** en vez de por rama,
así que la ejecución de una etiqueta sustituye a la de la rama sobre el mismo
commit en vez de duplicarla. No demuestra que la contención fuera la causa de
7.4, pero elimina la hipótesis del mapa y deja de pagar dos veces por lo mismo.

Lo que **no** se ha hecho es subir el `timeout-minutes`. Un límite que se
afloja cada vez que molesta ya no comprueba nada.
