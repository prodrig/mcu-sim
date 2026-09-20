# Compilar el modelo — guía y catálogo de errores

*Cómo construir el modelo en Windows, Linux y macOS, qué se puede ajustar sin
tocar el `Makefile`, y qué hacer cuando falla. La segunda mitad está ordenada por
**síntoma**, que es como se lee un documento así: con el error delante.*

---

## 1. Lo mínimo

```bash
cd src
make                    # el simulador: build/mcu-sim
make test407            # construye y ejecuta la suite del F407: 2074 comprobaciones
make test446            # y la del F446: 203 comprobaciones
make test417            # el acelerador criptografico del F415/F417: 164
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

### 5.6 Decenas de avisos dentro de las cabeceras de SystemC

Con compiladores muy nuevos sobre SystemC 2.3.4 salen avisos de
`-Woverloaded-virtual` en los sockets de TLM y similares. No son nuestros y no
los podemos arreglar: el Makefile los silencia con `-isystem`. Si aparecen de
todas formas, es que `SYSTEMC_HOME=/usr` y estamos en el caso de §5.3, donde hay
que usar `-I`.

### 5.7 `undefined reference to sc_core::sc_spawn(...)`

Falta `-DSC_INCLUDE_DYNAMIC_PROCESSES`, que el Makefile pone siempre.
`pins/pin_mux.h` usa procesos dinámicos y `<systemc>` solo los expone si esa
macro está definida **antes** de incluirlo. Si compilas a mano, ponla.

### 5.8 El enlazado va bien pero el programa aborta al arrancar

Mira los dos últimos de la tabla de §5.1: `SC_DEFAULT_WRITER_POLICY` y
`SC_ENABLE_COVARIANT_VIRTUAL_BASE` se comprueban en tiempo de **ejecución**, no
de enlazado, y el mensaje de SystemC dice cuál es.

---

## 6. Comprobar que ha salido bien

```bash
make red        # 13 comprobaciones de la capa de red, sin SystemC
make test407    # 2074 comprobaciones
make test446    # 203
make test417    # 164
```

Y el criterio que de verdad vale, más allá de que pasen: al final de `make test407`,

```
TOTAL     : 2074 comprobaciones OK, 0 fallos
Tiempo simulado: 2336217899213 ps
```

Los otros dos bancos tienen su propio invariante —`1033367277932 ps` el del
F446 y `718988288 ps` el del F417— y valen para lo mismo.

**Ese picosegundo es el mismo en Linux con g++, en Linux con clang y en cualquier
sitio donde el modelo se comporte igual.** Si las comprobaciones pasan pero la
cifra cambia, algo del planificador o de la resolución del tiempo es distinto, y
hay que entender qué antes de dar la plataforma por buena.

---

## 7. Qué está verificado y qué no

| Plataforma | Estado |
| :--- | :--- |
| Linux, g++ 13 | **Verificado**: 2074/2074, 203/203, 164/164, `make red` 13/13, ASan limpio en los tres |
| Linux, clang | **Verificado**: 2074/2074, mismo tiempo simulado al picosegundo |
| Windows, MinGW-w64 | **Compila y enlaza** cruzado desde Linux (PE32+ sin avisos). **Falta** construir SystemC allí y ejecutarlo |
| macOS, clang | **La rama específica compila**. **Falta** probarlo en un Mac |

Lo que falta en Windows y macOS es lo mismo en los dos casos: **la biblioteca de
SystemC**. Que el código compile y enlace no es que funcione. Es el punto **I-23**
del trabajo pendiente, y para un programa que se reparte a alumnos no es
opcional.

---

## 8. Y si la biblioteca da guerra

Si construir SystemC 2.3.4 en Windows o en macOS resulta ser un problema, la
alternativa está analizada en `doc/analisis_systemc3.md`: el modelo usa
exclusivamente la API central de IEEE 1666, no incluye ninguna cabecera interna
de SystemC, y compila en C++17, C++20 y C++23. **Probar la línea 3.0 no debería
costar más que cambiar `SYSTEMC_HOME`**, y esa decisión es precisamente una rama
de esta pregunta, no una aparte.
