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
| `rapidas` | Ubuntu | no hace falta | `red`, `macros-win`, `hash`, `cryp`, `vectores`. Menos de un minuto; evita encender cuatro máquinas por una errata |
| `linux` | `ubuntu-latest` | `apt install libsystemc-dev` | Verifica **la vía que documenta `compilacion.md` §4**. Si el paquete desaparece de Ubuntu, quiero enterarme aquí y no en el portátil de un alumno |
| `macos` | `macos-26` y `macos-26-intel` | compilada, en caché | **La plataforma que nadie ha ejecutado nunca.** Las dos arquitecturas, porque el `Makefile` tiene una rama para cada una y una rama que nadie ejecuta no está verificada |
| `windows` | `windows-latest` + MSYS2 | compilada, en caché | Las suites **y** que `mcu-sim.exe` no arrastre ninguna DLL de MinGW, que es lo de §5.6 |

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

**Lo que ya está bien y conviene no tocar:** `doc/pdf/` está en el
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
