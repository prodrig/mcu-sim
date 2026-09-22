# Compilar el modelo — guía y catálogo de errores

*Cómo construir el modelo en Windows, Linux y macOS, qué se puede ajustar sin
tocar el `Makefile`, y qué hacer cuando falla. La segunda mitad está ordenada por
**síntoma**, que es como se lee un documento así: con el error delante.*

---

## 1. Lo mínimo

> **Lo primero, y una sola vez por copia de trabajo.** Lo que se versiona es
> `src/Makefile.mcu-sim`; el `src/Makefile` a secas está en el `.gitignore` y es
> local de cada máquina —las herramientas con las que se edita este árbol en
> remoto se niegan a escribir un fichero llamado `Makefile`, que es un nombre
> protegido—. **No lo copies: enlázalo.**
>
> ```bash
> cd src
> make -f Makefile.mcu-sim enlaza     # deja un Makefile de una línea
> ```
>
> Una copia **envejece en silencio**: un `git pull` que traiga objetivos nuevos
> no los pone en tu copia, y `make` contesta «No rule to make target», que no se
> parece en nada a su causa. El enlace no puede quedarse atrás. Y mientras
> tanto, `make -f Makefile.mcu-sim <objetivo>` funciona siempre.

```bash
cd src
make                    # el simulador: build/mcu-sim
make test407            # construye y ejecuta la suite del F407: 2118 comprobaciones
make test446            # y la del F446: 204 comprobaciones
make test417            # el acelerador criptografico del F415/F417: 165

make test407fw          # REGENERA los firmwares (estan versionados) y LUEGO la suite
make test446fw          # lo mismo para la del F446
make test417fw          # y para la del F415/F417
make fw                 # solo los firmwares de las tres, sin simular
make cleanfw            # borrarlos todos (deja el arbol como recien clonado)
make hash               # los vectores de MD5 y SHA-1, que no necesitan SystemC
make cryp               # los de AES, DES y TDES, en las dos direcciones
```

Hace falta **SystemC ≥ 2.3.3** de Accellera y un compilador con **C++17**. Nada
más: el modelo son 45 000 líneas de C++17 y `<systemc>`, y la única dependencia
del sistema operativo está en `common/red.h`.

**Ejecuta siempre desde `src/`**, que es lo que hace `make`. Las imágenes de
firmware se cargan por rutas relativas (`verif/fw/...`) y desde otro directorio
fallan diecisiete comprobaciones por un motivo que no tiene nada que ver con lo
que estuvieras mirando.

---

## 2. Lo que decides tú, sin tocar el Makefile

Todas las variables están declaradas con `?=`, así que se sobrescriben desde la
línea de órdenes —que gana siempre— o desde el entorno.

| Variable | Para qué | Ejemplo |
| :--- | :--- | :--- |
| `SYSTEMC_HOME` | Dónde está SystemC (`include/` y la biblioteca) | `make SYSTEMC_HOME=/opt/systemc` |
| `PLATAFORMA` | `linux`, `macos` o `windows`. Se detecta sola; esto la fuerza | `make PLATAFORMA=windows` |
| `CXX` | El compilador | `make CXX=clang++` |
| `CXXSTD` | El estándar de C++. **Tiene que ser el mismo con el que se compiló SystemC** (§5.1) | `make CXXSTD=c++14` |
| `EXTRA` | Opciones **adicionales** al compilar | `make EXTRA=-DSC_WIN_DLL` |
| `EXTRA_LD` | Opciones **adicionales** al enlazar | `make EXTRA_LD=-Wl,-t` |

```bash
make SYSTEMC_HOME=/opt/systemc              # una vez
export SYSTEMC_HOME=/opt/systemc; make      # para toda la sesión
```

**`EXTRA` y `EXTRA_LD` existen por un motivo concreto**: el Makefile construye
`CXXFLAGS` con `+=`, y una asignación en la línea de órdenes **sustituye** el
valor entero en vez de añadirse. Es decir, `make CXXFLAGS=-DFOO` borraría
`-std=c++17`, los `-I` y todo lo demás. Con `EXTRA` se añade sin romper nada.

### Lo primero cuando algo no cuadra

```bash
make plataforma
```

Imprime qué ha decidido: `PLATAFORMA`, `CXX`, `CXXSTD`, `SYSTEMC_HOME`, `EXE`,
`LDLIBS` y `LIBDIRS`. Antes de investigar nada, mira ahí.

### Los otros objetivos

| | |
| :--- | :--- |
| `make test407` | La suite entera |
| `make red` | 13 comprobaciones de la capa de sockets, **sin necesitar SystemC**. Sirve para validar una plataforma nueva antes de pelearse con la biblioteca |
| `make mcu-sim` | El modelo con la placa en un fichero XML |
| `make bench` | El banco de medida del coste de simulación |
| `make asan407` | La suite con ASan + UBSan (Linux y macOS; MinGW no los trae) |
| `make clean` | |

---

## 3. Windows con MSYS2

Es la plataforma con más aristas, así que va entera.

1. Abre la terminal **MSYS2 MINGW64** (no la MSYS ni la UCRT, salvo que hayas
   construido SystemC para esa). Instala `mingw-w64-x86_64-gcc` y
   `mingw-w64-x86_64-make` (o usa el `make` de MSYS2).
2. Construye o instala SystemC. Si lo dejas, por ejemplo, en `/opt/systemc`:

```bash
cd src
make plataforma SYSTEMC_HOME=/opt/systemc      # comprueba antes
make SYSTEMC_HOME=/opt/systemc
```

Cinco detalles que muerden:

* **Barras hacia delante**: `C:/systemc`, nunca `C:\systemc`. Make trata la barra
  invertida como escape.
* **Sin espacios en la ruta.** `C:/Program Files/...` rompe make de formas poco
  divertidas. Instálalo en `/opt/systemc` o `C:/systemc-2.3.4`.
* **Desde el shell MinGW64, no desde `cmd.exe`.** Las recetas usan `mkdir -p` y
  `rm -rf`, que necesitan un shell POSIX. Git Bash también vale.
* **La plataforma debería detectarse sola** porque MSYS2 hereda `OS=Windows_NT`
  de Windows. Si no, `make PLATAFORMA=windows`.
* Si tienes MinGW suelto sin MSYS2, el ejecutable se llama `mingw32-make`.

Lo que el Makefile hace por ti en Windows y conviene saber que está ahí:
`-lws2_32` (los sockets son Winsock), `-D__USE_MINGW_ANSI_STDIO=1` —sin ella el
`printf` de msvcrt no entiende `%llu`, y el modelo lo usa 28 veces— y
`-static-libgcc -static-libstdc++`, para que el `.exe` no necesite las DLL de
MinGW instaladas.

---

## 4. Linux y macOS

**Linux.** SystemC viene empaquetada: `apt install libsystemc-dev` en Ubuntu
24.04 pone la 2.3.4 en `/usr`, que es el valor por omisión. `make` y ya está.

**macOS.** `SYSTEMC_HOME` se detecta entre `/opt/homebrew` (Apple Silicon) y
`/usr/local` (Intel). El compilador es clang, y el modelo compila con él sin
avisos propios.

---

## 5. Cuando falla: catálogo por síntoma

### 5.1 `undefined reference to sc_core::sc_api_version_2_3_4_cxx201703L<...>`

Es el error que más tiempo hace perder, así que va con detalle.

```
undefined reference to `sc_core::sc_api_version_2_3_4_cxx201703L
    <&sc_core::SC_DISABLE_VIRTUAL_BIND_UNDEFINED_>
    ::sc_api_version_2_3_4_cxx201703L(sc_core::sc_writer_policy, bool)'
```

**Lo primero, y es lo que orienta todo: falta UN solo símbolo.** Si la biblioteca
no se hubiera encontrado, o fuera de otra arquitectura, no tendrías un
`undefined reference` sino miles —`sc_module`, `sc_signal`, `sc_time`, todo—. Así
que `libsystemc` **se ha enlazado bien y tiene todo lo real**. Lo que falla es
otra cosa.

**Qué es ese símbolo.** No es un fallo: es una comprobación que SystemC pone a
propósito para que la aplicación y la biblioteca no se compilen con opciones
distintas. Está en `sysc/kernel/sc_ver.h`, y el comentario del propio fichero lo
dice:

> *«Some preprocessor switches need to be consistent between the application and
> the library (e.g. if sizes of classes are affected or other parts of the ABI
> are affected). (Some of) these are checked here at link-time as well, by
> setting template parameters to sc_api_version_XXX, while only one variant is
> defined in sc_ver.cpp.»*

El mecanismo es una plantilla cuyo **nombre y parámetros llevan dentro la
configuración**, y de la que la biblioteca instancia **una sola variante**:

```cpp
// sc_ver.h — el nombre se construye con la version y el estandar de C++
#define SC_API_VERSION_STRING  sc_api_version_<MAJOR>_<MINOR>_<PATCH>_cxx<SC_CPLUSPLUS>

template< const int* DisableVirtualBind >
struct SC_API_VERSION_STRING {
    SC_API_VERSION_STRING(sc_writer_policy default_writer_policy,
                          bool has_covariant_virtual_base);
};

static SC_API_VERSION_STRING< &SC_DISABLE_VIRTUAL_BIND_CHECK_ >
    api_version_check(SC_DEFAULT_WRITER_POLICY, SC_HAS_COVARIANT_VIRTUAL_BASE_);
```

Y `SC_CPLUSPLUS` es `__cplusplus` con GCC y clang (`sc_cmnhdr.h:123`), así que
`-std=c++17` produce `cxx201703L`.

De ahí, **lo que tiene que coincidir**:

| Qué | Dónde se comprueba |
| :--- | :--- |
| Versión de SystemC (`2_3_4`) | En el nombre → **enlazado** |
| Estándar de C++ (`cxx201703L`) | En el nombre → **enlazado** |
| `SC_DISABLE_VIRTUAL_BIND` definida o no | En el parámetro de plantilla → **enlazado** |
| `SC_DEFAULT_WRITER_POLICY` | En un argumento del constructor → **ejecución** |
| `SC_ENABLE_COVARIANT_VIRTUAL_BASE` | En el otro argumento → **ejecución** |

Los dos últimos no dan error de enlazado: dan un error al arrancar el programa.

**Y un detalle importante: el símbolo del mensaje es el que TU compilación pide,
no el que la biblioteca tiene.** Por eso el error no dice cuál es el desajuste.

#### La orden que lo resuelve

```bash
nm -C /opt/systemc/lib/libsystemc.a | grep -i api_version | head
```

(prueba también `lib64/`, `lib-mingw64/` y, si es una DLL, `libsystemc.dll.a`).
Imprime el símbolo que la biblioteca **sí** exporta. Compara las dos cadenas:

| Lo que diga `nm` | Causa | Arreglo |
| :--- | :--- | :--- |
| `..._cxx201402L...` u otro `cxx` | La biblioteca se compiló con otro estándar | `make CXXSTD=c++14` (el que diga) |
| `..._SC_DISABLE_VIRTUAL_BIND_DEFINED_...` | Se construyó con esa macro | `make EXTRA=-DSC_DISABLE_VIRTUAL_BIND` |
| Otra versión que no sea la de las cabeceras | Cabeceras y biblioteca no son del mismo sitio | Corregir `SYSTEMC_HOME` |
| El fichero no existe o no sale nada | Se enlazó **otra** biblioteca | Véase abajo |

#### Si se enlazó otra biblioteca

En MSYS2 es muy posible: si `/opt/systemc/lib*` no contiene un `libsystemc.a`, el
enlazador sigue buscando en los directorios por omisión y puede coger el paquete
`mingw-w64-x86_64-systemc` de `/mingw64/lib`, construido con otras opciones.

```bash
ls -l /opt/systemc/lib*
make SYSTEMC_HOME=/opt/systemc EXTRA_LD=-Wl,-t 2>&1 | grep -i systemc
```

`-Wl,-t` hace que el enlazador imprima **cada fichero que abre**, así que verás
la ruta exacta de la que salió `-lsystemc`.

#### Si SystemC se construyó como DLL

Sospéchalo si en `lib/` hay un `libsystemc.dll.a` (biblioteca de importación) y
un `.dll` suelto: es un build compartido, que es lo que CMake hace por omisión en
algunas configuraciones. Entonces las cabeceras tienen que saberlo, porque
`SC_API` solo se convierte en `__declspec(dllimport)` si `SC_WIN_DLL` está
definida (`sc_cmnhdr.h:174-185`):

```bash
make SYSTEMC_HOME=/opt/systemc EXTRA=-DSC_WIN_DLL
```

Y el `.dll` tiene que estar en el `PATH` al ejecutar, o junto al `.exe`.

#### Saltarse la comprobación: cuándo sí y cuándo no

```bash
make EXTRA=-DSC_DISABLE_API_VERSION_CHECK
```

Existe (`sc_ver.h:182`) y **sirve para diagnosticar**: si con eso enlaza todo lo
demás, confirmas que el único problema era la comprobación y no la biblioteca.

**No sirve como arreglo.** La comprobación está ahí porque un desajuste de
verdad —otro estándar de C++, otra política de escritura— cambia tamaños de
clase y comportamiento; desactivarla convierte un error de enlazado, que es
molesto pero honrado, en un fallo en ejecución imposible de perseguir.

### 5.2 `cannot find -lsystemc`

`SYSTEMC_HOME` apunta a otro sitio, o la biblioteca está en un directorio con un
nombre que el Makefile no busca. Mira qué busca con `make plataforma` (línea
`LIBDIRS`) y añade el tuyo sin tocar nada:

```bash
make SYSTEMC_HOME=/opt/systemc EXTRA_LD=-L/opt/systemc/lib-mingw64-static
```

### 5.3 `fatal error: stdlib.h: No such file or directory`

El Makefile usa `-isystem` para las cabeceras de SystemC, de modo que sus avisos
no se mezclen con los nuestros. Pero marcar `/usr/include` como `-isystem` lo
**saca de su sitio** en la cadena estándar de cabeceras y deja de encontrarse
`<stdlib.h>`. El Makefile detecta el caso `SYSTEMC_HOME=/usr` y usa `-I` normal;
si te aparece con otro prefijo del sistema, el arreglo es el mismo.

### 5.4 `mkdir: -p: command not found`, o `rm` no reconocido

Estás en `cmd.exe` o en PowerShell. Usa la terminal MSYS2 MINGW64 o Git Bash.

### 5.5 Los `%llu` imprimen basura en Windows

Falta `-D__USE_MINGW_ANSI_STDIO=1`, que el Makefile pone solo cuando
`PLATAFORMA=windows`. Comprueba con `make plataforma` que la detección ha
acertado.

### 5.6 «No se encuentra el punto de entrada `clock_gettime64`»

*Arreglado en el Makefile; queda aquí porque el mensaje no se parece a su causa
y porque es EXACTAMENTE lo que le pasará a un alumno.*

El síntoma, medido en una máquina de verdad:

| Dónde | Qué hace `mcu-sim.exe --help` |
| :--- | :--- |
| Terminal **MINGW64** de MSYS2 | funciona |
| **PowerShell** | no dice nada y no hace nada |
| **`cmd.exe`** | *«No se encuentra el punto de entrada `clock_gettime64` en la biblioteca de vínculos dinámicos…»* |

Lee bien el mensaje, porque ahí está todo: **no dice que falte la DLL**. Dice
que la DLL que ha encontrado **no exporta ese símbolo**. Es decir, la encontró —
y es la equivocada.

**De dónde sale.** El GCC de MSYS2 está construido con el modelo de hilos
**POSIX**, así que `std::chrono`, `std::thread` y SystemC entran por
**winpthreads**, y eso deja en el ejecutable una importación de
`libwinpthread-1.dll`. Desde el shell MINGW64 el `PATH` lleva delante la DLL
buena, la de `/mingw64/bin`, y todo va. Desde `cmd` gana la primera que
aparezca en el `PATH` del sistema — de otro MinGW, de Qt, de un IDE— y si es
anterior al cambio a `time_t` de 64 bits, no exporta `clock_gettime64`.

Para ver cuál gana en tu máquina:

```
where libwinpthread-1.dll
```

**El arreglo, y por qué no es «pon la DLL al lado».** El Makefile enlaza con
**`-static`** en Windows. `-static-libgcc -static-libstdc++` —lo que había—
cubre la biblioteca estándar de C++ y la de GCC, pero **no** winpthreads: esa se
colaba igual. Con `-static` no hay ninguna DLL de MinGW que buscar, que es lo
que el comentario del Makefile llevaba prometiendo.

Repartir la DLL junto al `.exe` también «funciona», y es peor: basta con que el
alumno lo ejecute desde otra carpeta, o que tenga otra copia antes en su `PATH`,
para volver al mismo sitio. **Un fallo que depende del orden del `PATH` de cada
máquina no se depura por correo.**

### 5.7 Decenas de avisos dentro de las cabeceras de SystemC

Con compiladores muy nuevos sobre SystemC 2.3.4 salen avisos de
`-Woverloaded-virtual` en los sockets de TLM y similares. No son nuestros y no
los podemos arreglar: el Makefile los silencia con `-isystem`. Si aparecen de
todas formas, es que `SYSTEMC_HOME=/usr` y estamos en el caso de §5.3, donde hay
que usar `-I`.

### 5.8 `undefined reference to sc_core::sc_spawn(...)`

Falta `-DSC_INCLUDE_DYNAMIC_PROCESSES`, que el Makefile pone siempre.
`pins/pin_mux.h` usa procesos dinámicos y `<systemc>` solo los expone si esa
macro está definida **antes** de incluirlo. Si compilas a mano, ponla.

### 5.9 El enlazado va bien pero el programa aborta al arrancar

Mira los dos últimos de la tabla de §5.1: `SC_DEFAULT_WRITER_POLICY` y
`SC_ENABLE_COVARIANT_VIRTUAL_BASE` se comprueban en tiempo de **ejecución**, no
de enlazado, y el mensaje de SystemC dice cuál es.

---

## 6. Los firmwares de las suites, y el compilador de ARM

Veinte grupos de las tres suites cargan un `.bin` **de verdad** en la Flash del
modelo y lo ejecutan con el Cortex-M4.

**Esos diecinueve binarios se versionan**, y un árbol recién clonado los trae.
Son 37 KB. Para ejecutar las suites **no hace falta ningún compilador cruzado**:
`make test407` basta.

No fue así siempre, y el motivo del cambio es el único interesante de esta
sección. Mientras no se versionaban, cada máquina compilaba los suyos con el
compilador cruzado que allí hubiera, y por tanto **cada máquina ejecutaba un
programa distinto**: el `arm-none-eabi-gcc` 13.2 de Debian y el 13.3.1 de
STM32CubeIDE producen un CoreMark de 14528 y de 14856 bytes. Un binario
distinto ejecuta un número distinto de instrucciones, `t17_coremark()` sondea
el final cada 2 ms, y la diferencia salía cuantizada a 2 ms pareciendo un
problema de plataforma. Costó dos diagnósticos, el primero equivocado. Está
en **T-22**.

Cuál es la imagen buena lo dice **`src/verif/fw/huellas.txt`** —ruta, tamaño y
FNV-1a de cada una— y el **primer grupo de cada suite** (`T00` en el F407, `A0`
en las otras dos) lo comprueba antes de simular nada, a coste cero de tiempo
simulado. Ese fichero es la única fuente de verdad: ningún `.cpp` lleva una
huella escrita a mano.

Los objetivos de abajo ya no hacen falta para ejecutar las suites. Sirven para
**regenerar** los firmwares, que es otra cosa: ensucian el árbol de trabajo, y
si la cadena cruzada de esta máquina no reproduce las imágenes buenas, `T00`
lo dice en la primera línea. Todos avisan antes de escribir.

| Orden | Qué hace |
| :--- | :--- |
| `make test407fw` | **Regenera** los **16** firmwares de la suite del F407 y la ejecuta |
| `make test446fw` | Los **2** del F446 y su suite |
| `make test417fw` | El del F415/F417 y su banco |
| `make fw` | Los tres juegos, sin simular |
| `make fw407` / `fw446` / `fw417` | Solo el juego de una suite |
| `make cleanfw` | **Borra** los de **todos** los directorios (19). Están versionados: `git checkout -- verif/fw` los devuelve |

Si el cambio es querido —otra versión de CoreMark, otra opción de compilación—
hay que actualizar también `huellas.txt` y los invariantes de tiempo simulado
de §7. Es una decisión, no un trámite.

### 6.1 Dónde está el compilador: la variable `CROSS`

Una sola variable, y es el **prefijo** del nombre de las herramientas. Cada
Makefile de firmware la respeta desde siempre (`CROSS ?= arm-none-eabi-`) y de
ahí salen `$(CROSS)gcc`, `$(CROSS)objcopy` y `$(CROSS)objdump`.

**Si `arm-none-eabi-gcc` está en el `PATH`, no hay que hacer nada.** Si no:

```bash
make test407fw CROSS=/opt/gcc-arm/bin/arm-none-eabi-
```

**Ojo al guion del final.** Es un prefijo, no un ejecutable: sin él, el Makefile
intentará ejecutar algo llamado `.../bingcc`. Y si se va a usar siempre la
misma, se exporta una vez por sesión:

```bash
export CROSS=/opt/gcc-arm/bin/arm-none-eabi-
```

De dónde sacarlo:

| Sistema | Cómo |
| :--- | :--- |
| Debian / Ubuntu | `apt install gcc-arm-none-eabi` |
| MSYS2 | `pacman -S mingw-w64-x86_64-arm-none-eabi-toolchain` |
| macOS | `brew install --cask gcc-arm-embedded` |
| **Cualquiera, sin instalar nada** | El que ya trae **STM32CubeIDE** |

**En MSYS2, el grupo `-toolchain` y no el paquete `-gcc` a secas**, porque los
Makefiles de firmware necesitan además `objcopy`, que viene en `binutils`. Y si
el nombre no existe —MSYS2 los renombra de vez en cuando y está migrando de
`mingw64` a `ucrt64`—, la forma de averiguarlo no es adivinar:

```bash
pacman -Ss arm-none-eabi
```

Sirve cualquiera de los entornos: es un compilador **cruzado**, produce código
ARM, y da igual contra qué biblioteca de C de Windows se construyó él. Si acaba
en `/ucrt64/bin` estando tú en MINGW64, se usa igual con `CROSS`.

### 6.2 El atajo: el compilador que ya tienes

**Quien usa este simulador tiene STM32CubeIDE**, y CubeIDE trae su propio
`arm-none-eabi-gcc` con su `objcopy` y su `objdump`. No hay que instalar nada:
solo hay que encontrarlo.

```bash
ls -d /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin
```

Y con eso:

```bash
export CROSS=/c/ST/STM32CubeIDE_1.17.0/STM32CubeIDE/plugins/\
com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-
${CROSS}gcc --version        # comprobar antes de lanzar nada
make test407fw
```

*(La ruta es de una instalación concreta; la tuya llevará otra versión y otro
sello de fecha. Por eso el `ls` de arriba, que la encuentra sola.)*

La versión no importa mucho: el firmware es C bare-metal corriente y no usa nada
exótico. Aquí se compila con `arm-none-eabi-gcc 13.2`, y el de CubeIDE 1.17 es
un 13.3.1.

Si el prefijo apunta a donde no hay nada, el Makefile **lo dice antes de
empezar** en vez de soltar veinte «command not found»:

```
  ojo: no encuentro '/no/existe/arm-none-eabi-gcc' en el PATH.
  Dale la ruta con CROSS, que es un PREFIJO y acaba en guion:
      make fw417 CROSS=/ruta/a/bin/arm-none-eabi-
```

---

## 7. Comprobar que ha salido bien

### 7.1 Si faltan los firmwares, no te creas los fallos que salen después

*Corregido; queda escrito porque el síntoma apuntaba al sitio equivocado.*

*Ya no puede pasar por descuido: los `.bin` se versionan (§6) y el primer grupo
de cada suite comprueba que están y que son los buenos. Pero `make cleanfw` los
borra, así que el síntoma sigue siendo alcanzable, y merece quedar escrito
porque apuntaba al sitio equivocado.*

Cuando los `.bin` no estaban, quince grupos de la suite fallaban con «imagen …
cargada en la Flash». Eso era lo esperable. **Ahora eso sale como un solo
fallo, el primero de todos**, que nombra los ficheros que faltan:

```
--- T00 Imagenes de firmware [verif/fw/huellas.txt] ---
         16 imagenes:
         falta coremark/coremark.bin
  [FALLO] las 16 imagenes de la suite del F407 son las versionadas
```

Lo que **no** era normal es lo que venía detrás. Los grupos que ejecutan
firmware **apagan la onda cuadrada de los relojes internos** porque generarla a
168 MHz domina el tiempo de simulación. Cuando el `.bin` no estaba, esos grupos
se iban por un `return` temprano **sin volver a encenderla**, y quedaba apagada
para **todo el resto de la suite**. Consecuencia:

* **T23** medía `0 Hz` en el pin de MCO1 — no había flancos que contar;
* **T53** perdía dos muestras de I2S.

Tres fallos en grupos que no tenían nada que ver con el firmware que faltaba.
En una máquina de desarrollo no se ve nunca, porque allí los firmwares están
siempre compilados; apareció la primera vez que la suite corrió en Windows.

Arreglado con un guarda RAII (`GuardaOndas`) que restaura el estado anterior por
cualquier camino de salida. **Comprobado en las dos direcciones**: escondiendo
`coremark.bin` a propósito, antes caían cuatro comprobaciones y ahora cae solo
la que debe caer — más las dos de T53, que resultaron tener otra causa y están
anotadas como **V-11**: esa comprobación depende del instante simulado en que
arranca, y eso no lo arregla el guarda.



```bash
make red        # 13 comprobaciones de la capa de red, sin SystemC
make test407    # 2118 comprobaciones
make test446    # 204
make test417    # 165
```

Y el criterio que de verdad vale, más allá de que pasen: al final de `make test407`,

```
TOTAL     : 2118 comprobaciones OK, 0 fallos
Tiempo simulado: 2336217899213 ps
```

Los otros dos bancos tienen su propio invariante —`1033367277932 ps` el del
F446 y `718988288 ps` el del F417— y valen para lo mismo.

Y debajo, desde que la suite corrió en una segunda máquina, **tres líneas más**:

```
  de los cuales T96+T97 (socket de GDB): 96665875 ns
  el resto                             : 2239552024213 ps
  huella de coremark.bin               : 0x644FCE21
  (la que debe ser esta en verif/fw/huellas.txt, y T00 lo comprueba: T-22)
```

**La precondición para comparar dos máquinas no era la que parecía.** Mientras
los `.bin` no se versionaron, se compilaban en cada sitio con el compilador
cruzado que allí hubiera. Un binario distinto ejecuta un número distinto de
instrucciones, y **T17 sondea el final de CoreMark cada 2 ms**, así que esa
diferencia salía **cuantizada a 2 ms**. Medido en una sola máquina:
recompilando CoreMark de `-O2` a `-O1`, el total se mueve **24 ms exactos**,
doce pasos del bucle.

Y comprobado **entre las dos máquinas**, que es lo que cerró el asunto. Las dos
imágenes de CoreMark no eran la misma:

| Máquina | Compilador cruzado | `coremark.bin` | Huella | Total F407 |
| :--- | :--- | ---: | :--- | ---: |
| Linux (contenedor) | `arm-none-eabi-gcc` 13.2, Debian | 14528 B | `0x644FCE21` | `2336217899213 ps` |
| Windows (MSYS2) | `arm-none-eabi-gcc` 13.3.1, STM32CubeIDE | 14856 B | `0x5157F2C7` | `2334217899213 ps` |

**No se estaba comparando la misma simulación**, sino dos programas distintos
corriendo sobre el mismo modelo. La diferencia de 2 ms no era un síntoma; era
la única forma que tenía el reloj de expresar «aquí sobra o falta una vuelta
del sondeo de T17».

**La corrección fue quitar la causa, no documentarla mejor**: los diecinueve
`.bin` se versionan (§6), y el primer grupo de cada suite los contrasta con
`verif/fw/huellas.txt` antes de simular nada. La línea de la huella se queda
porque es la cifra que explicaría una diferencia si alguna vez vuelve a
aparecer: **si la huella coincide y el total no**, entonces sí hay algo del
planificador o de la resolución del tiempo que es distinto en esa plataforma, y
hay que entenderlo antes de darla por buena.

*(El desglose del socket de GDB se quedó de la primera hipótesis, que era otra y
resultó falsa: se creía que el sondeo del socket era lo que dependía del
anfitrión. La propia medida que se añadió para comprobarlo la refutó —T96+T97
consumen exactamente lo mismo en las dos plataformas— y de paso dejó localizada
la diferencia en el resto. Se mantiene porque separa una cifra que sí podría
haber variado.)*

---

## 8. Qué está verificado y qué no

| Plataforma | Estado |
| :--- | :--- |
| Linux, g++ 13 | **Verificado**: 2118/2118, 204/204, 165/165, `make red` 13/13, ASan limpio en los tres |
| Linux, clang | **Verificado** con el codigo anterior a versionar los firmwares: 2117/2117, mismo tiempo simulado al picosegundo. Falta repetirlo; no se espera nada distinto, pero no se ha hecho |
| **Windows, MSYS2 / MinGW-w64** | **VERIFICADO POR COMPLETO, y los TRES invariantes coinciden con los de Linux al picosegundo.** Con los firmwares versionados: **2118/2118** en `2336217899213 ps` (huella `0x644FCE21`), **204/204** en `1033367277932 ps` y **165/165** en `718988288 ps`. Antes, con los firmwares de cada sitio, el del F407 salia `2334217899213 ps`: los 2 ms eran el binario y nada mas (**T-22**) |
| Windows, cruzado desde Linux | **Compila y enlaza** (`make red PLATAFORMA=windows CXX=x86_64-w64-mingw32-g++`, PE32+ sin avisos). Ojo: el cruzado de Debian usa hilos **win32** y el de MSYS2 **posix**, así que no reproduce el caso de §5.6 |
| macOS, clang | **La rama específica compila**. **Falta** probarlo en un Mac |

**Windows está verificado, y ahora también el invariante.** SystemC 2.3.4 se
construye para MinGW, el modelo se ejecuta y las tres suites pasaron enteras
—2117, 203 y 164 comprobaciones, 0 fallos— ya antes de versionar los
firmwares. Lo que faltaba era el tiempo: el F446 y el F417 salían **idénticos
al picosegundo**, y el del F407 se quedaba **2 ms corto**.

**Ese hueco ya no está.** Con los `.bin` versionados, las tres suites dan en
Windows exactamente lo mismo que en Linux:

| Suite | Comprobaciones | Tiempo simulado | Linux |
| :--- | ---: | ---: | :--- |
| `test407` | 2118 | `2336217899213 ps` | idéntico |
| `test446` | 204 | `1033367277932 ps` | idéntico |
| `test417` | 165 | `718988288 ps` | idéntico |

y el F407 imprime `huella de coremark.bin : 0x644FCE21`, la del manifiesto.

**El mismo picosegundo en los tres.** Era el binario, y nada más: ni el
planificador de SystemC, ni la resolución del tiempo, ni el socket de GDB —ésa
fue la primera hipótesis, y era falsa—. La predicción estaba escrita en §8 y en
**T-22** *antes* de ejecutarla, que es la única forma de que una confirmación
signifique algo.

El F446 y el F417 se ejecutaron **dos veces**, con salida idéntica: el tiempo
simulado no depende de la carga de la máquina, como debe ser en un modelo
donde el tiempo lo lleva el planificador y no el reloj de pared.

En **macOS** sigue faltando la verificación, pero ya no es un hueco sin plan:
entra por la **integración continua**, en las dos arquitecturas y sobre
hardware Apple real —que es la única forma legal de probarlo desde un PC con
Windows, porque la licencia de macOS solo permite virtualizarla sobre un
ordenador Apple—. Está contado en **`doc/integracion_continua.md`**, y sigue
siendo el punto **I-23** del trabajo pendiente hasta que salgan las 2 118
comprobaciones y el invariante.

## 8.1 Y lo mismo, en cada cambio

`.github/workflows/suites.yml` ejecuta las tres suites en las cuatro
plataformas en cada empujón a `main`, y **contrasta el tiempo simulado contra
`src/verif/invariantes.txt`**, que es donde viven esas tres cifras una sola vez.
Si no coinciden, el trabajo falla; no avisa, falla. El porqué de ese criterio
—y no el de «las suites pasan»— está en §7, y el detalle de los cuatro trabajos
en `doc/integracion_continua.md`.

---

## 9. Y si la biblioteca da guerra

Si construir SystemC 2.3.4 en Windows o en macOS resulta ser un problema, la
alternativa está analizada en `doc/analisis_systemc3.md`: el modelo usa
exclusivamente la API central de IEEE 1666, no incluye ninguna cabecera interna
de SystemC, y compila en C++17, C++20 y C++23. **Probar la línea 3.0 no debería
costar más que cambiar `SYSTEMC_HOME`**, y esa decisión es precisamente una rama
de esta pregunta, no una aparte.
