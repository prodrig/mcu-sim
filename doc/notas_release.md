**mcu-sim** — simulador SystemC de microcontroladores STM32 (F407 / F417 / F446).

Estos ejecutables los ha construido la integración continua a partir del commit
etiquetado, y **cada uno se publica solo si en su plataforma pasaron las tres
suites y los tres invariantes de tiempo simulado coincidieron al picosegundo**.
No salen del portátil de nadie.

| Tu máquina | Fichero | ¿Instalar algo? |
| :--- | :--- | :--- |
| Windows 10/11 x64 | `mcu-sim-windows-x86_64.zip` | **No** |
| Linux x86-64, Ubuntu 22.04 o posterior | `mcu-sim-linux-x86_64.tar.gz` | **No** |
| Mac Apple Silicon | `mcu-sim-macos-arm64.tar.gz` | **Sí**: `brew install systemc` |
| Mac Intel | `mcu-sim-macos-x86_64.tar.gz` | **Sí**: `brew install systemc` |

**Antes de nada, dos avisos que te van a pasar:**

- **macOS bloquea el programa** porque no está firmado. Se arregla en una línea:
  `xattr -d com.apple.quarantine mcu-sim`. El paso a paso, y la alternativa sin
  Terminal, están en **`doc/ejecutables.md` §4.3**.
- **Windows muestra el aviso de SmartScreen**, por lo mismo. *Más información* →
  *Ejecutar de todas formas*.

**Linux tiene un suelo, y es Ubuntu 22.04** (glibc 2.35). Un binario de Linux
funciona hacia adelante pero no hacia atrás, así que se construye a propósito en
la distribución más antigua que soportamos. Si la tuya es anterior, toca
compilar: `doc/compilacion.md`.

`SHA256SUMS.txt` va al lado si quieres comprobar la descarga.

La guía completa —qué hacer en cada plataforma y qué mirar cuando no arranca—
está en **`doc/ejecutables.md`**. Las licencias del software ajeno que llevan
estos paquetes, en **`TERCEROS.md`**.
