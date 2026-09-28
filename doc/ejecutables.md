# Ejecutar mcu-sim sin compilarlo

*Para quien solo quiere usar el simulador: descargar un fichero, ejecutarlo y
ponerse a probar su firmware. Compilar el modelo está en `doc/compilacion.md`, y
aquí no hace falta.*

---

## 1. Qué descargar

Los ejecutables se publican en la página de **Releases** del repositorio, y los
construye la integración continua a partir de un commit concreto: no salen del
portátil de nadie. Cada uno se publica **solo si las tres suites pasaron y los
tres invariantes coincidieron al picosegundo** en su plataforma.

| Tu máquina | Fichero | ¿Hay que instalar algo? |
| :--- | :--- | :--- |
| Windows 10 u 11, 64 bits | `mcu-sim-windows-x86_64.zip` | **No** |
| Linux x86-64, Ubuntu 22.04 o posterior | `mcu-sim-linux-x86_64.tar.gz` | **No** |
| Mac con Apple Silicon (M1…M4) | `mcu-sim-macos-arm64.tar.gz` | **Sí**: `brew install systemc` |
| Mac con Intel | `mcu-sim-macos-x86_64.tar.gz` | **Sí**: `brew install systemc` |

Si no sabes qué Mac tienes, `uname -m` responde `arm64` o `x86_64`.

**Comprobar que el fichero es el que se publicó**, si quieres: al lado va
`SHA256SUMS.txt`, y

```bash
shasum -a 256 -c SHA256SUMS.txt      # macOS
sha256sum -c SHA256SUMS.txt          # Linux
```

---

## 2. Windows

```
Descomprime el .zip donde quieras (botón derecho → Extraer todo)
Abre una consola en esa carpeta
mcu-sim.exe --help
```

No hay que instalar nada: el ejecutable lleva dentro SystemC, `libstdc++`,
`libgcc` y el runtime de MinGW, y la integración continua comprueba en cada
cambio que no dependa de ninguna DLL de MinGW.

**La primera vez, Windows avisa.** SmartScreen dice «Windows protegió tu PC»
porque el ejecutable no está firmado. Es un aviso, no un bloqueo: *Más
información* → *Ejecutar de todas formas*.

---

## 3. Linux

```bash
tar xzf mcu-sim-linux-x86_64.tar.gz
./mcu-sim --help
```

Tampoco hay que instalar nada, y **tampoco hace falta `libsystemc-dev`**: va
enlazada dentro. Lo único que el ejecutable espera de tu sistema es **glibc 2.35
o posterior**, que es la de Ubuntu 22.04.

> **Por qué hay un suelo y no puede no haberlo.** Un binario de Linux funciona
> hacia adelante pero no hacia atrás: el compilado en Ubuntu 24.04 pide
> `GLIBC_2.38` y en 22.04 no arranca. Así que se construye a propósito en la
> distribución más antigua que queremos soportar. Si tu distribución es anterior
> a 22.04, toca compilar: `doc/compilacion.md`.

Si ves `Permission denied`, es que el bit de ejecución se perdió por el camino
—descomprimir con una herramienta gráfica lo hace a veces—:

```bash
chmod +x mcu-sim
```

---

## 4. macOS

### 4.1 Primero, SystemC

```bash
brew install systemc
```

Un comando, no una compilación. En macOS el ejecutable **no** lleva SystemC
dentro, a propósito: es la plataforma donde instalarla es trivial, y así el
paquete pesa poco y la biblioteca te llega de Accellera con su licencia, no
reempaquetada por nosotros.

Si no tienes Homebrew, está en https://brew.sh.

### 4.2 Descomprimir

```bash
cd ~/Downloads
tar xzf mcu-sim-macos-arm64.tar.gz      # o -x86_64 si tu Mac es Intel
```

### 4.3 Quitar la cuarentena — el paso que hay que dar sí o sí

macOS marca **todo** lo que llega de internet con un atributo extendido,
`com.apple.quarantine`. Si el programa no está firmado y notarizado —y éste no
lo está: notarizar exige una cuenta de pago de Apple Developer—, Gatekeeper lo
bloquea en cuanto lo ejecutas, con un mensaje del estilo *«no se puede abrir
porque Apple no puede comprobar si contiene software malicioso»*.

**Paso a paso, en la Terminal:**

```bash
# 1. Ver si el atributo está ahí (si no imprime nada, no hay nada que quitar)
xattr -l mcu-sim

# 2. Quitarlo
xattr -d com.apple.quarantine mcu-sim

# 3. Asegurar el bit de ejecución, que a veces se pierde al descomprimir
chmod +x mcu-sim

# 4. Comprobar que arranca
./mcu-sim --help
```

Tres detalles que ahorran una tarde:

- **Si el paso 2 dice `No such xattr`**, perfecto: significa que no había
  cuarentena y puedes seguir.
- **Si descomprimiste en una carpeta y quieres limpiarla entera**, el atributo
  se quita recursivamente: `xattr -dr com.apple.quarantine <carpeta>`.
- **No hace falta `sudo`** para nada de esto. Si algo te pide la contraseña de
  administrador, párate y mira qué estás ejecutando.

### 4.4 Si prefieres no usar la Terminal

El camino por la interfaz existe, y en las versiones recientes de macOS es
éste —el truco antiguo de *botón derecho → Abrir* ya no basta siempre—:

1. Ejecuta el programa y deja que macOS lo bloquee.
2. Abre **Ajustes del Sistema → Privacidad y seguridad**.
3. Baja hasta el aviso «*mcu-sim* se ha bloqueado porque no es de un
   desarrollador identificado».
4. Pulsa **Abrir de todos modos** y autentícate.
5. Vuelve a ejecutarlo. Esta vez sale un diálogo con un botón **Abrir**.

Es más largo y hay que repetirlo con cada descarga nueva, que es justo por lo
que el camino de la Terminal está primero.

---

## 5. Cuando no arranca: por síntoma

| Lo que ves | Qué es | Qué hacer |
| :--- | :--- | :--- |
| `Permission denied` | El bit de ejecución se perdió al descomprimir | `chmod +x mcu-sim` |
| macOS: «Apple no puede comprobar si contiene software malicioso» | La cuarentena | §4.3 |
| macOS: `Library not loaded: libsystemc…` | Falta SystemC, o Homebrew ha subido de versión mayor desde que se publicó el binario | `brew install systemc`; si ya estaba, `brew upgrade systemc` y, si sigue, compila (`doc/compilacion.md`) |
| Linux: `version 'GLIBC_2.35' not found` | Tu distribución es anterior a Ubuntu 22.04 | Compilar; el suelo no se puede bajar desde el binario |
| Windows: «Windows protegió tu PC» | SmartScreen, por no estar firmado | *Más información* → *Ejecutar de todas formas* |
| Arranca y dice que no encuentra el firmware | Es normal: el simulador necesita que le des un `.bin` | `./mcu-sim --help` explica las opciones |

---

## 6. Lo que estos paquetes **no** traen

- **Las suites de verificación** (`test407`, `test446`, `test417`). Se construyen
  desde el código; el CI las ejecuta en cada cambio y su resultado es público.
- **Los firmwares de ejemplo.** El uso previsto es que compiles el tuyo en
  STM32CubeIDE y lo cargues aquí, que es para lo que existe el proyecto.
- **La interfaz gráfica**, que vive en el repositorio `mcu-sim-gui` y se conecta
  a este programa por un socket.
- **Los manuales de ST.** No son nuestros; `doc/fuentes.md` dice cuáles son, en
  qué revisión y de dónde se bajan.

Lo que sí traen, además del ejecutable: el `README.md`, el `TERCEROS.md` con las
licencias del software ajeno, y —en Windows y Linux, donde SystemC va dentro del
binario— la licencia y el aviso de atribución de SystemC.
