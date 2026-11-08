# ESP32-C3 y ESP32-C6 en `mcu-sim`

**Análisis y plan por fases** · rama `esp32rv` · 07-11-2026

Segunda versión del análisis del 14-10-2026. Entre una y otra han entrado en
`main` el MSP432 (P-16, P-17) y el STM32F303 (P-18), con la generalización T0,
la sonda `remote_bitbang`, la sonda CMSIS-DAP por TCP y el puente USB de clase.
Además, ya están en `doc/refs/esp32-c3/` y `doc/refs/esp32-c6/` los manuales de
referencia, las hojas de datos, las erratas y las guías de diseño de hardware.
Este documento sustituye al anterior: rehace el inventario con lo que ya existe,
baja los dos chips al detalle de sus manuales y convierte el plan en fases con
sus entregas y su criterio de cierre.

Hay una condición nueva que ordena todo lo demás: **el núcleo RISC-V tiene que
quedar separado del chip y reutilizable**, porque detrás del C3 y el C6 vendrán
otros MCUs RISC-V. Ejemplos: el ESP32-C5, el C61, el H2 o el P4 del mismo
fabricante, un GD32VF103, un CH32V o el Hazard3 del RP2350. Por eso la §3 está
dedicada a esa separación, y las fases E1 y E2 construyen y validan el núcleo y
su depuración **sin ningún chip de Espressif**.

Referencias de este documento, todas en `doc/refs/` y sin versionar:

| Clave | Documento |
| :--- | :--- |
| [TRM-C3] | ESP32-C3 Technical Reference Manual, v1.4 |
| [DS-C3] | ESP32-C3 Series Datasheet, v2.4 |
| [ER-C3] | ESP32-C3 Series SoC Errata, v1.3 |
| [AR-C3] | Compatibility Advisory AR2024-009 (revisión v1.1), v1.2 |
| [TRM-C6] | ESP32-C6 Technical Reference Manual, v1.2 |
| [DS-C6] | ESP32-C6 Series Datasheet, v1.5 |
| [ER-C6] | ESP32-C6 Series SoC Errata, v1.3 |
| [HW-C3], [HW-C6] | Hardware Design Guidelines de cada chip |
| [DBG] | RISC-V External Debug Support, v0.13.2 |
| [PRIV] | RISC-V Privileged Architecture (v1.10 es la que citan los dos TRM) |

---

## 0. Resumen

* **Ya está hecha buena parte de la antigua fase E0.** La T0 del MSP432 la
  hizo para una familia ARM de otro fabricante:
  * `mcu_if` ya no exige un `McuCaps` (`soc/mcu_desc.h`);
  * los nombres de los pines los interpreta el chip (`pins/nomenclatura.h`);
  * el `load` de GDB pide la secuencia de Flash al chip (`programador_nv_if`);
  * el cargador lee ELF (`common/imagen_fw.h`);
  * la memoria no volátil persiste entre simulaciones (`memoria_nv.h`);
  * y hay opciones por chip (`--rom`, `admite_opcion`).

  **Lo que queda es lo que separa a ARM de RISC-V** (§5):
  * `DebugCaps`, que sigue en `cortex_m4f.h`;
  * el motor de GDB, que sigue siendo de ARMv7-M;
  * las sondas, que solo hablan SWD;
  * los 16 pines por puerto de `Encapsulado`;
  * y los nodos de depuración, que hoy son solo SWCLK y SWDIO.
* **Lo que entonces había que escribir ahora existe, y se reutiliza tal cual**:
  * la sonda `remote_bitbang` para OpenOCD (`verif/sonda_openocd.h`), a la
    que solo le falta la mitad JTAG del protocolo;
  * la prueba con OpenOCD de verdad (`verif/openocd/programa.py`);
  * la sonda CMSIS-DAP por TCP;
  * y, sobre todo, **el puente USB de clase CDC-ACM → RFC 2217**
    (`parts/puente_usb.h`). El USB-Serial-JTAG del C3 y del C6 es un
    dispositivo USB de función fija, y si se modela detrás de `usb_dev_if`,
    el puente lo enumera como enumera el OTG del F303 o el MUSB del E4.
    Por ese camino, `esptool` y `idf.py monitor` funcionan sobre el USB nativo
    sin escribir un puente nuevo.
* **La ROM deja de ser un problema de licencia.** Los ELF de las ROM que
  publica Espressif (`espressif/esp-rom-elfs`) están bajo **Apache-2.0**, como
  CMSIS: se pueden descargar en la integración continua e incluso versionar.
  El código de la ROM no es abierto, pero el binario sí se puede redistribuir
  (§8).
* **Los dos chips no tienen el mismo núcleo**, aunque los dos se llamen
  «ESP-RISC-V» y tengan 4 etapas. Los TRM lo dejan claro, y es la mejor prueba
  de que el núcleo tiene que ser un componente parametrizable (§2.2):

  | | C3 | C6 HP | C6 LP |
  | :--- | :--- | :--- | :--- |
  | ISA | RV32IMC | RV32IMAC | RV32IMAC |
  | `mie`/`mip` | no tiene | sí | sí |
  | Modos | U, sin delegación | U con delegación (`mideleg`, `ustatus`, `utvec`, `uret`) | M |
  | Triggers | 8 | 4 | 2 |
  | Contadores | propios | propios | `mcycle`/`minstret` estándar |
  | Interrupciones | 31 líneas | 28 externas + 4 del CLINT | una sola entrada |

* **La arquitectura propuesta tiene tres anillos** (§3):
  1. **`src/rv/`**: el núcleo RISC-V genérico. Contiene el hart, los CSR, los
     traps, PMP y PMA, los tiempos, el Debug Module, los *triggers*, el TAP con
     su DTM y el *backend* RISC-V del motor de GDB, más un **SoC de
     laboratorio** con su banco `testrv`. Ahí se valida la ISA contra
     `riscv-tests`, `riscv-arch-test` y Spike, sin ningún chip.
  2. **`src/soc/esp/` y `src/periph/esp/`**: lo común de Espressif: formato de
     imagen, ROM, eFuse, caché con MMU, MSPI y los periféricos que comparten el
     C3 y el C6.
  3. **`src/soc/esp32c3/` y `src/soc/esp32c6/`**: lo propio de cada chip.

  Un MCU RISC-V futuro solo escribe el tercer anillo, y el segundo si es de
  otro fabricante. El núcleo, la depuración y GDB le llegan hechos (§3.9).
* **Descarga y depuración**, sin cambios de fondo respecto a la primera versión
  y ahora con nombres de fase:
  * D2 es `esptool` por la UART0 con auto-reset;
  * D3 es `esptool` por el USB-Serial-JTAG;
  * G1 es el stub interno pegado al Debug Module;
  * G2 es `openocd-esp32` por `remote_bitbang` en JTAG;
  * G3 es OpenOCD por el JTAG del USB-Serial-JTAG.

  Todas las vías salen del núcleo genérico, así que valen para cualquier
  RISC-V con un DM 0.13.
* **El plan tiene catorce fases (E0-E13, §10).** La radio sigue declarada y no
  modelada, salvo la vía paravirtual de `doc/analisis_wifi_bl.md` (R8), que
  queda como fase opcional. El orden es este:
  * E0 termina la generalización;
  * E1 y E2 son el núcleo y su depuración en el laboratorio;
  * de E3 a E8, el C3 entero: arranque directo, ROM y ESP-IDF, esptool, GDB y
    OpenOCD, USB-Serial-JTAG, periféricos;
  * de E9 a E11, el C6: el núcleo HP, el núcleo LP como segundo hart, y sus
    periféricos propios;
  * E12, la radio paravirtual (opcional);
  * E13, la GUI.

---

## 1. Qué ha cambiado desde la primera versión

| Punto del análisis de octubre | Estado hoy (`esp32rv` = `main` + este documento) |
| :--- | :--- |
| `mcu_if` exige `McuCaps` | **Resuelto en T0**: `McuDesc` con `propio` opaco y `CatalogoMcu` común |
| Los nombres de pin son los de ST | **Resuelto en T0**: `Nomenclatura` por tipo de chip; falta registrar la de Espressif (`GPIOn`, `IOn`) |
| El `load` de GDB lleva dentro la Flash del F407 | **Resuelto en T0**: `programador_nv_if`. Para el ESP32 hará falta uno que programe la Flash SPI a través de SPI1 (§7.1) |
| Falta ELF en el cargador | **Resuelto en T0** (`imagen_fw.h`, carga por LMA). Falta el formato de imagen de Espressif |
| `mcu_if` incluye `cortex_m4f.h` | **Sigue igual**: solo por `DebugCaps`, que es neutral y hay que moverlo (E0) |
| `GdbRsp` mezcla protocolo y ARMv7-M | **Sigue igual** (DHCSR, FPB, DWT y los 23 registros dentro del motor). Es el trabajo grande de E0 |
| Hay que escribir una sonda `remote_bitbang` | **Existe** (T5 del MSP432), solo para SWD: hoy `R` contesta `0` y `0`-`7` se ignoran. Hay que completar JTAG (E0) |
| — | **Existe** una sonda CMSIS-DAP por TCP (X2 del XDS110). Con `DAP_JTAG_Sequence` sería una sonda JTAG rápida |
| Hay que escribir el puente CDC → RFC 2217 | **Existe** (`puente_usb.h`, T7 del MSP432). Falta que admita un dispositivo compuesto y una segunda clase, la del JTAG de Espressif (E7) |
| La licencia de la ROM es la primera incógnita | **Resuelta**: Apache-2.0 [esp-rom-elfs, README] |
| `Encapsulado`: 16 pines por puerto | **Sigue igual**. Hoy son 18 puertos × 16 bits; el C6 tiene GPIO0-30 sin GPIO14 en un solo banco |
| PowerPads con NRST y BOOT0 | La `Nomenclatura` ya trae sus pines de alimentación (`alim`). Falta CHIP_PU como reset |
| La radio | Hay análisis propio (`doc/analisis_wifi_bl.md`, §3.3 y R8): **paravirtual** con `VirtWifi`, opcional |

**Suites cuyo invariante no se puede mover** (`verif/invariantes.txt`):
`test407`, `test446`, `test417`, `testserie`, `testgui`, `testp4`, `teste4` y
`testf3`.

---

## 2. Los dos chips, según sus manuales

### 2.1 El mapa del sistema

| | ESP32-C3 [TRM-C3 §3] | ESP32-C6 [TRM-C6 §5] |
| :--- | :--- | :--- |
| ROM | 384 KB: ROM0 de 256 KB en `0x4000_0000` (solo bus I) y ROM1 de 128 KB en `0x4004_0000`, **con alias** en el bus D en `0x3FF0_0000` | 320 KB en `0x4000_0000`, bus I y D en la misma dirección |
| SRAM | 400 KB: SRAM0 de 16 KB en `0x4037_C000` (solo bus I; **puede ser la caché**) y SRAM1 de 384 KB en `0x4038_0000` (bus I) **= `0x3FC8_0000`** (bus D) | HP SRAM de 512 KB en `0x4080_0000`, **sin alias** |
| Memoria que sobrevive al Deep-sleep | RTC FAST, 8 KB en `0x5000_0000` | **LP SRAM**, 16 KB en `0x5000_0000`, con dos modos de acceso (rápido para el HP o para el LP, `LP_AON_FAST_MEM_MUX_SEL`) |
| Flash externa | Hasta 16 MB. Caché de 16 KB, 8 vías y líneas de 32 B. **Dos ventanas** de 8 MB: `0x4200_0000` (I) y `0x3C00_0000` (D, solo lectura), en páginas de 64 KB | Hasta 16 MB. Caché de 32 KB, 4 vías y líneas de 32 B. **Una ventana** de 16 MB en `0x4200_0000` para I y D |
| Periféricos | `0x6000_0000`-`0x600D_0FFF`, 35 módulos | `0x6000_0000`-`0x600C_FFFF`, 51 módulos. Los LP van en `0x600B_xxxx` en ventanas de 1 KB |
| Región de la CPU | `0x2000_0000`-`0x27FF_FFFF` («DM») | `0x2000_0000`-`0x2FFF_FFFF` («CPU sub-system»: el CLINT en `+0x1800`/`+0x1C00`) |
| Lo que no está en el bus | Lo que no cae en IRAM, DRAM ni DM va por AHB [TRM-C3 tabla 1.3-1] | Igual [TRM-C6 tabla 1.4-1] |
| GDMA | 3 TX + 3 RX, 7 periféricos | 3 TX + 3 RX, 8 periféricos |

Para el modelo, la diferencia de fondo está en los alias: **el C3 ve la misma
SRAM1 y la misma ROM1 por dos direcciones**, y el C6 no. Se resuelve en el
decodificador de bus de cada chip, no en el núcleo (§3.1).

### 2.2 Los tres núcleos

| | C3 [TRM-C3 §1] | C6 HP [TRM-C6 §1] | C6 LP [TRM-C6 §3] |
| :--- | :--- | :--- | :--- |
| ISA | RV32IMC | RV32IMAC | RV32IMAC |
| Segmentación y reloj | 4 etapas en orden, hasta 160 MHz | 4 etapas, **BTB con predicción estática**, hasta 160 MHz | **2 etapas**, hasta 20 MHz |
| Modos | M y U (`misa.U = 1`, `MPP` solo 0 o 3, `mstatus.TW`) | M y U **con delegación** de interrupciones: `mideleg`, `ustatus`, `uie`, `utvec`, `uscratch`, `uepc`, `ucause`, `uip` y `uret`, es decir, la antigua extensión N | Solo M |
| `mie`/`mip` | **No existen**: el enmascarado es del controlador, por registros | Sí | Sí, pero solo se usa el bit 30 |
| `mtvec` | Solo vectorizado, base alineada a 256 | Igual | Igual. Tras el reset vale `0x5000_0000` y el PC arranca en `0x5000_0080` |
| Interrupciones | INTC de 31 IDs (1-31), prioridad 1-15, umbral y tipo nivel o flanco; salto a `mtvec + 4·ID`; **latencia de 4 ciclos**; ID 0 reservado para excepciones | INTPRI: 28 externas (1-2, 5-6, 8-31) con prioridad 0-15 y umbral, más **4 del CLINT** (0 USIP, 3 MSIP, 4 UTIP, 7 MTIP) con prioridad fija y sin umbral; las externas ganan siempre | **Una sola entrada** (`mtvec + 4·30`); la fuente se lee en `LPPERI_INTERRUPT_SOURCE_REG` |
| Temporizador del núcleo | — (el SYSTIMER es un periférico) | CLINT: `MTIME` de 64 bits, `MTIMECMP` y `UTIMECMP`, con desbordamiento (`MTOF`) | — |
| Contadores | **Propios**: `mpcer`, `mpcmr` y `mpccr` (`0x7E0`-`0x7E2`); la lista de CSR del TRM no trae `mcycle` ni `minstret` | Propios, como el C3 | **Estándar**: `mcycle`, `minstret`, `mhpmcounter3`-`12` y `mcountinhibit` |
| CSR propios | `cpu_gpio_oen/in/out` (`0x803`-`0x805`, GPIO dedicada) | Los mismos, más **PMA**: `pma_cfg0`-`15` (`0xBC0`) y `pma_addr0`-`15` (`0xBD0`) | — |
| Protección | PMP de 16 regiones | PMP de 16 regiones + PMA de 16 | — |
| Desalineados | Los hace el hardware, partidos en dos transacciones. Con la errata **CPU-863** (en todas las revisiones): una carga desalineada seguida de un almacenamiento puede dar fallo de acceso si los dos están en regiones PMP o PMA distintas [ER-C3 §3.1] | Igual, con CPU-863 en v0.0-v0.2 | Excepción 6, «misaligned atomic» |
| Identificación | `mvendorid = 0x612`, `marchid = 0x8000_0001`, `mimpid = 1` | (por leer del TRM en E9) | Solo `mhartid` (= 1 para el DM) |
| Depuración | DM 0.13, *program buffer* de 16 palabras, SBA de palabra alineada, **8 triggers** y `tcontrol` | DM 0.13 **con varios harts** (hart 0 = HP, hart 1 = LP), *program buffer* de 16 palabras, **4 triggers**, disparo cruzado HP↔LP y codificador de traza | Comparte el DM del HP. **2 triggers**. Sale «unavail» del reset hasta que se escribe `LPPERI_CPU_REG` |

**Consecuencia para el diseño**: si se escribiera el núcleo «del C3» y luego se
adaptara al C6, quedarían tres copias con `if` por todas partes. Lo correcto es
un núcleo cuyos rasgos y cuyo controlador de interrupciones vengan de fuera
(§3.2 y §3.5).

### 2.3 Pines, arranque y JTAG

| | ESP32-C3 | ESP32-C6 |
| :--- | :--- | :--- |
| GPIO | 22 (`GPIO0`-`21`); **16** en el C3FH4AZ y el C3FH4X, que no sacan los pines de la Flash [DS-C3 tabla 1-1] | **30** en QFN40 (`GPIO0`-`13` y `GPIO15`-`30`: **no hay GPIO14**); 22 en QFN32 (C6FH4, C6FH8, con Flash dentro) [DS-C6] |
| Pines de arranque | GPIO2, GPIO8 y GPIO9 (más GPIO3 en el *SPI Download Boot*); GPIO9 con *pull-up* interno [TRM-C3 §7] | MTMS, MTDI, GPIO8, GPIO9 y GPIO15; GPIO9 con *pull-up* [TRM-C6 §9] |
| Modos | GPIO9 = 1: SPI Boot. GPIO9 = 0 y GPIO8 = 1: *Joint Download* (UART0 o USB). `0 0 0 1`: *SPI Download* | GPIO9 = 1: SPI Boot. GPIO9 = 0 y GPIO8 = 1: Download |
| Variantes del SPI Boot | *Normal Flash Boot* (la ROM copia el bootloader a la SRAM) y **Direct Boot**: si las dos primeras palabras de la Flash son `0xaedb041d`, la ROM monta la caché y salta a la Flash sin bootloader | Las mismas |
| Mensajes de la ROM | UART0 y USB-Serial-JTAG; los gobiernan `EFUSE_UART_PRINT_CONTROL` y GPIO8 | Igual |
| UART0 | U0RXD = GPIO20, U0TXD = GPIO21 | U0TXD = GPIO16, U0RXD = GPIO17 |
| USB-Serial-JTAG | D- = GPIO18, D+ = GPIO19 | D- = GPIO12, D+ = GPIO13 |
| JTAG en pines | MTMS/MTDI/MTCK/MTDO = GPIO4/5/6/7 | Los mismos GPIO4-7 |
| Origen del JTAG | eFuses `DIS_USB_JTAG`, `DIS_PAD_JTAG` y `JTAG_SEL_ENABLE`. **El TRM del C3 no dice qué pin de arranque elige**; hay que cerrarlo con la guía de JTAG de ESP-IDF | Tabla completa con eFuses y **GPIO15** [TRM-C6 tabla 9.2-4 y §1.10.4] |
| Revisiones | v0.4 y **v1.1, con otra ROM**. Un binario para v0.4 **no arranca** en v1.1 sin recompilar con un ESP-IDF reciente [AR-C3]. La revisión está en eFuse [ER-C3 §1.2] | v0.0, v0.1 y v0.2 |

### 2.4 Los periféricos

| Bloque | C3 | C6 | En el modelo |
| :--- | :--- | :--- | :--- |
| UART | 2 + UHCI | 2 + **LP_UART** + UHCI | Una IP común; el LP_UART es otra instancia con otro reloj |
| SPI | SPI0/1 (Flash) + GP-SPI2 | Igual | Comunes |
| I2C | 1 | 1 + **LP_I2C** | Común |
| I2S | 1 | 1 | Común (declarado al principio) |
| TWAI (CAN) | 1 | **2** | Común. Las piezas del bus CAN ya existen |
| LEDC (PWM) | 6 canales | 6 canales | Común |
| RMT | Sí | Sí (errata RMT-176) | Común |
| SAR ADC y sensor de temperatura | Sí | Sí | Común |
| SYSTIMER, TIMG×2 y WDT | Sí | Sí | Comunes |
| GDMA | 3+3 | 3+3 | Común |
| USB-Serial-JTAG | Sí | Sí | Común |
| SHA, AES, RSA, HMAC, DS, XTS_AES y RNG | Sí | Sí, más **ECC** | SHA, AES y RNG modelados; el resto, declarados |
| Reloj y reset | SYSTEM/SYSCON + RTC_CNTL | **PCR** + LP_CLKRST + **PMU** + LP_AON | **Distintos**: uno por chip |
| Interrupciones | Matriz + INTC de la CPU | INTMTX + **INTPRI** + CLINT | Matriz por chip y controlador en `src/rv/irq` |
| Protección | **PMS** + World Controller | **APM** + TEE + PMP/PMA | Distintos |
| ASSIST_DEBUG | Sí | Sí | Común: ESP-IDF lo usa como **guarda de pila** |
| Solo del C6 | — | MCPWM, PCNT, **ETM**, PARL_IO, esclavo SDIO, monitor de memoria, codificador de traza, `I2C_ANA_MST` | Propios del C6 (E11) |

---

## 3. El núcleo RISC-V reutilizable

### 3.1 Qué es del núcleo y qué es del chip

La línea es la misma que T0 trazó en ARM con `IntegracionNucleo`: **lo que dice
la especificación RISC-V, y lo que todo fabricante parametriza, es del núcleo.
Lo que el fabricante pone alrededor es del chip.**

| Del núcleo (`src/rv/`) | Del integrador (el SoC) |
| :--- | :--- |
| Decodificación y ejecución de I, M, A, C (y F en el futuro) | Qué extensiones tiene: un `RvCaps` |
| CSR estándar de modo máquina, de usuario y de depuración; los que no existen dan instrucción ilegal | Qué CSR existen y sus valores de identificación |
| Traps, `mret`/`uret`, `wfi`, delegación | Si hay delegación (C6 HP) o no (C3) |
| PMP; PMA como mecanismo | Cuántas regiones; los CSR de PMA del C6 están en el espacio propio del fabricante |
| La **interfaz** con el controlador de interrupciones | **Qué** controlador: el INTC del C3, el INTPRI con CLINT del C6, el LP del C6, un CLIC, un PLIC |
| Debug Module 0.13, *triggers*, modo depuración, DTM y TAP JTAG | Cuántos harts, *triggers* y palabras de *program buffer*; el IDCODE del TAP |
| *Backend* RISC-V de GDB | El programador de su memoria no volátil |
| Modelo de tiempos, con sus tablas | Los valores de las tablas de su microarquitectura |
| — | El mapa de memoria, los alias, la caché, las ventanas de periféricos |
| — | Los CSR y las instrucciones propias del fabricante, por las interfaces de §3.4 |

Igual que `CortexM4F`, el núcleo se integra como **un `SC_MODULE` fino**
(`NucleoRv`) con sus *sockets* TLM de instrucción y de datos, su reloj, su
reset, la interfaz de interrupciones y la de depuración. El SoC le dice qué
ventanas de direcciones son suyas con un `IntegracionRv`: la región «de la CPU»
de `0x2000_0000`, donde el C6 tiene el CLINT, y la vía rápida de la SRAM
local, que en el C3 y el C6 es de espera cero.

### 3.2 Los rasgos: `RvCaps`

Un `struct` `constexpr` por núcleo, con la misma receta que `CoreCaps`,
`UsartCaps` o `TimCaps`:

```cpp
struct RvCaps {
    const char* nombre;          // "ESP32-C3", "ESP32-C6 HP", "ESP32-C6 LP"
    unsigned    xlen;            // 32 (64 queda previsto, no hecho)
    uint32_t    ext;             // bits de misa: I M A C (F D en el futuro)
    uint32_t    zext;            // Zicsr Zifencei Zicntr Zihpm Zba Zbb Zbs Zcb Zcmp Zicond...
    bool        modo_u;          // ¿hay modo usuario?
    bool        deleg_n;         // ¿delegación de interrupciones a U (mideleg + CSR de usuario)?
    bool        mie_mip;         // false en el C3: el enmascarado es del controlador
    unsigned    mtvec_modos;     // directo / vectorizado / CLIC
    uint32_t    mtvec_alin;      // 256 en los tres núcleos de Espressif
    uint32_t    pc_reset, mtvec_reset;
    unsigned    pmp_regiones, pmp_grano;
    bool        contadores_std;  // mcycle/minstret (C6 LP sí; C3 y C6 HP no)
    Desalineado desalineado;     // trap | hardware | hardware_cpu863
    uint32_t    mvendorid, marchid, mimpid;
    RvDebugCaps dbg;             // triggers, progbuf, datacount, SBA, impebreak
    const RvTiempos* tiempos;    // §3.3
};
```

Las tres instancias de este plan son `RV_ESP32C3`, `RV_ESP32C6_HP` y
`RV_ESP32C6_LP`. Las suites del núcleo tendrán además dos de laboratorio:
`RV_LAB_IMC` y `RV_LAB_IMAC`, con `mie`/`mip`, modo directo y contadores
estándar, para pasar las pruebas de conformidad tal como vienen.

### 3.3 El hart

* **Estado** en un `struct` plano (`RvState`): `x[32]`, `pc`, los CSR y, cuando
  haga falta, `f[32]`. Es el mismo criterio que `cpu_state.h`: lo inspecciona el
  depurador y lo copian las pruebas.
* **Decodificación por tabla**, con las instrucciones comprimidas **expandidas a
  su forma de 32 bits** antes de ejecutar. Así C no duplica la semántica, y
  `Zcb`/`Zcmp` (Hazard3, C5) entran después como más expansiones.
* **Los vectores de decodificación** salen de un generador en Python con dos
  fuentes, como `gen_decoder_vectors.py`: el *opcodes* de `riscv-opcodes` y la
  especificación.
* **Traps** genéricos: causa, `mepc`, `mtval`, `MPIE`/`MPP` y el cálculo del
  vector, todo según `RvCaps`. Con `deleg_n`, la versión de usuario
  (`ucause`, `uepc`, `utvec`, `uret`).
* **Atómicas** (A): reserva de LR/SC por hart. Las AMO viajan como **una
  transacción TLM marcada atómica**: una extensión del *payload* que el bus
  respeta. Importa en el C6, donde el HP y el LP comparten la LP SRAM.
* **PMP y PMA**: el comprobador es del núcleo y la tabla de PMA la rellena el
  integrador. En el C6, por sus CSR `0xBC0`/`0xBD0`, que se dan de alta como
  CSR propios (§3.4).
* **Desalineados**: tres comportamientos según `RvCaps`. El tercero reproduce la
  errata CPU-863 y **se puede apagar**, como el resto de erratas del proyecto:
  activa por omisión, porque es lo que hace el silicio.
* **Tiempos** (`RvTiempos`): ciclos por clase de instrucción, penalización de
  salto tomado y de fallo del BTB, latencia de carga a uso, ciclos de `mul` y
  `div`, latencia del controlador de interrupciones (4 ciclos en C3 y C6 según
  sus TRM) y entrada a un trap.
  * Es LT con quantum, como el Cortex. Las cifras que los TRM no dan van como
    **parámetros públicos sin fuente**, apuntados en la sección D de `todo.md`
    y marcados `[de modelo]`.
  * Hay una fuente para calibrarlas: el CoreMark/MHz que publica Espressif.
* **`wfi`** con el reloj parado: el `mpccr` del C3 no cuenta durante `WFI`
  [TRM-C3, registro `mpcer`].

### 3.4 Lo que añade el fabricante: CSR e instrucciones propios

Dos interfaces, para que **ningún fichero de `src/rv/` sepa de Espressif**:

* **`rv_csr_ext_if`**: el integrador da de alta un rango de CSR con su lectura,
  su escritura y su modo mínimo. Los casos de este plan son:
  * los contadores `mpcer`, `mpcmr` y `mpccr` (`0x7E0`-`0x7E2`), con sus
    sucesos;
  * la GPIO dedicada `cpu_gpio_*` (`0x803`-`0x805`), que en el C3 y el C6
    gobierna pines a través de la matriz GPIO;
  * la PMA del C6 (`0xBC0`-`0xBDF`).
* **`rv_ext_if`**: los cuatro huecos `custom-0..3` del espacio de códigos de
  operación. No hace falta para el C3 ni el C6. Existe para el futuro: el
  Xesppie del P4, las extensiones XW de los CH32V o `Xh3*` del Hazard3.

### 3.5 Interrupciones: la interfaz y los controladores

El núcleo no sabe qué controlador tiene. Le pregunta por **`rv_irq_if`**:

```cpp
class rv_irq_if {
  virtual bool pendiente(RvModo m, unsigned& id, unsigned& nivel) = 0; // la mejor que puede entrar ya
  virtual void reclamada(unsigned id) = 0;                             // entró: actualiza estado
  virtual uint32_t mip_externo() const { return 0; }                   // lo que se ve en mip
  virtual void tras_mret(RvModo) {}                                    // controladores con nivel (CLIC)
};
```

| Controlador | Para | Dónde |
| :--- | :--- | :--- |
| `IrqEstandar` | `mip`/`mie` puros con MEIP, MTIP y MSIP. Modo directo y vectorizado | `src/rv/irq/`; el laboratorio y cualquier RISC-V «de libro» |
| `Clint` | `MTIME`, `MTIMECMP` y `MSIP`. Variante Espressif con `UTIMECMP`, `USIP` y `MTIMECTL` | `src/rv/irq/` (el bloque) + el C6 lo monta en `0x2000_1800` |
| `IntcEspV1` | 31 IDs, prioridad 1-15, umbral, nivel o flanco, borrado por pulso, latencia de 4 ciclos, sin `mie`/`mip` | `src/soc/esp/` (es de Espressif): el C3 |
| `IntcEspV2` | INTPRI: 28 externas, prioridad 0-15, umbral, delegación a U y, combinado con el CLINT, las externas por delante | `src/soc/esp/`: el C6 HP |
| `IrqLpEsp` | Una línea en el ID 30 | `src/soc/esp32c6/` |
| `Clic`, `Plic` | Previstos, no hechos | `src/rv/irq/`, cuando llegue un chip que los use (C5, C61, P4 o GD32V) |

La **matriz de interrupciones** (fuentes de periférico → líneas de CPU) es del
chip: 62 fuentes en el C3 y otro número en el C6, que hay que leer de [TRM-C6 §10].

### 3.6 La depuración

Todo en `src/rv/debug/` y sin nada de Espressif:

* **`RvDebugModule`**, según [DBG] 0.13.2:
  * registros `dmcontrol`, `dmstatus`, `hartinfo`, `haltsum0`, `abstractcs`,
    `command`, `data0`-`n`, `progbuf0`-`n`, `sbcs`, `sbaddress0` y `sbdata0`;
  * comandos abstractos de acceso a registros (GPR, CSR y, si existe, FPR);
  * *program buffer* con `impebreak`;
  * **SBA** que entra al bus como un maestro más;
  * `ndmreset` y `hartreset`;
  * **varios harts** con `hartsel`, incluido el estado «unavail» del LP del C6;
  * y `resethaltreq`, para el «halt desde la primera instrucción» que los dos
    TRM prometen.
* **`RvTriggers`** (Sdtrig): `mcontrol` (tipo 2) de ejecución, carga y
  almacenamiento, con `tselect`/`tdata1`/`tdata2` y el **`tcontrol`** que el
  C3 y el C6 tienen. El número sale de `RvCaps`: 8, 4 o 2. El hart decide en
  cada búsqueda y en cada acceso, por el mismo contrato que `debug_if.h`.
* **Modo depuración del hart**: `dcsr` (con `ebreakm`, `ebreaku`, `step`,
  `cause` y `prv`), `dpc` y `dscratch0/1`, inaccesibles fuera de depuración, y
  la entrada y salida (`dret`).
* **`RvDtmJtag`**: la máquina de 16 estados del TAP, IR de 5 bits, `IDCODE`,
  `BYPASS`, **`dtmcs`** y **`dmi`** (con `abits` según el DM) y el `dmistat`
  de ocupado.
* **`JtagPines`**: TCK, TMS, TDI y TDO (y TRST, si el chip lo saca) sobre los
  `AnalogNet` de unos pads que el chip elige. Es el equivalente RISC-V del
  `SwdPort` que hoy cuelga de PA13/PA14.
* **Disparo cruzado** (`dcsr` y los registros de [TRM-C6 §1.13]) entre harts del
  mismo DM.

Hay **una interfaz, `dmi_if`** (`leer(dir)`, `escribir(dir, v)`), y tres maneras
de llegar a ella. Las tres terminan en el mismo DM:

| Camino | Coste |
| :--- | :--- |
| Llamada directa (stub interno) | Una llamada de función por acceso |
| DTM por los pines (sonda `remote_bitbang`, CMSIS-DAP o el stub de pines propio) | Decenas de flancos de TCK por acceso |
| DTM por el USB-Serial-JTAG (E7) | Paquetes USB con órdenes de reloj comprimidas |

### 3.7 GDB: el *backend* RISC-V

En E0 el motor `GdbRsp` se parte en dos (§5): **protocolo** y **arquitectura**.
`gdb_arq_riscv.h` es la arquitectura RISC-V y habla con el DM por `dmi_if`:

* **Registros**: x0-x31 y `pc`, más los F si hay F. El `target.xml` se genera de
  `RvCaps` (`org.gnu.gdb.riscv.cpu`, `.fpu` y `.csr`, con los CSR que se quieran
  enseñar).
* **Parar, seguir y paso**: `haltreq`/`resumereq` y `dcsr.step`, con la causa
  de parada traducida a la señal de GDB.
* **Puntos de ruptura**:
  * por *trigger* cuando la memoria no se puede escribir (la Flash por la
    caché, la ROM);
  * por `ebreak`/`c.ebreak` en RAM, según el tamaño de la instrucción.

  Los *watchpoints*, por *trigger*. El número de *triggers* se **descubre**
  recorriendo `tselect`, como hace OpenOCD, en lugar de leerlo de la tabla.
  Así el *backend* vale para un chip que no conoce.
* **Memoria**: por SBA cuando existe, y por *program buffer* si no.
* **Varios harts**: cada hart es un hilo de GDB (`qfThreadInfo`, `Hg`, `vCont`).
  Es lo que pide el C6 con su LP y, más adelante, el P4.
* **`monitor reset halt`**, `monitor reset` y la secuencia de ESP-IDF al
  conectar (`mon reset halt`, `maintenance flush register-cache`, `thb
  app_main`, `c`).
* **El `load`** pasa por el `programador_nv_if` del chip, como en ARM.

### 3.8 El SoC de laboratorio y la verificación del núcleo

`src/rv/lab/SocLabRv` es un SoC mínimo y sin fabricante: RAM, un `Clint`, una
UART de pega y la interfaz **HTIF** (`tohost`/`fromhost`), por la que terminan
las pruebas de conformidad. Lleva su propio banco, **`testrv`**, con su
invariante. Sirve para tres cosas:

1. **Conformidad de la ISA**:
   * `riscv-tests` (BSD-3), `rv32ui`, `rv32um`, `rv32ua`, `rv32uc` y `rv32mi`,
     entorno `-p`;
   * y las partes RV32I, M, A, C, Zicsr, Zifencei y *privilege* de
     **`riscv-arch-test`** (Apache-2.0/BSD-3), con sus firmas comparadas con
     las de referencia.
2. **Traza contra Spike** (BSD-3): el mismo ELF en Spike con `--log-commits` y
   en `testrv`, instrucción a instrucción. Es la segunda fuente de la ISA y
   ocupa el lugar que tienen los 254 vectores de `valida_instrucciones.py` en
   el Cortex.
3. **La depuración sin chip**: GDB y OpenOCD (`target create riscv`, genérico,
   con `remote_bitbang` en JTAG) contra el DM del laboratorio (E2).

El laboratorio **no es una curiosidad**. Es lo que permite decir que un fallo
en el C3 es del C3 y no del núcleo, y es lo primero que monta un MCU RISC-V
futuro antes de escribir su SoC.

### 3.9 Cómo entra un MCU RISC-V futuro

| Paso | Qué escribe | Qué recibe hecho |
| :--- | :--- | :--- |
| 1 | Su `RvCaps` y su `RvTiempos` | Ejecución, traps, PMP, conformidad con `testrv` |
| 2 | Su controlador, si no es uno de §3.5 (el ECLIC del GD32VF103 o el PFIC de los CH32V) | El resto de las interrupciones |
| 3 | Su SoC: mapa, periféricos, `mcu_if`, `Nomenclatura` y `Encapsulado` | Netlist, piezas, GUI, puente serie y CI |
| 4 | Su IDCODE y sus pines JTAG (o su transporte, como el SDI de un hilo de los CH32V) | G1, G2 y OpenOCD genérico |
| 5 | Su `programador_nv_if` | El `load` de GDB |

Cuatro de los cinco pasos son datos. El quinto, el programador, es el mismo
trabajo que hizo falta para cada familia ARM.

---

## 4. La capa de Espressif

Entre el núcleo genérico y cada chip hay una capa que es de Espressif pero no
de un chip concreto. Separarla es lo que hace que el C6 cueste la mitad que el
C3, y que el C5, el C61 o el H2 cuesten todavía menos.

| Fichero (propuesto) | Qué | C3 / C6 |
| :--- | :--- | :--- |
| `soc/esp/esp_imagen.h` | El formato de imagen (cabecera `0xE9`, segmentos, suma, SHA-256 opcional), las imágenes completas de Flash, `flash_args` y `flasher_args.json`, todo sobre `TrozoFw` | Común |
| `soc/esp/esp_rom.h` | Carga del ELF de la ROM por chip y revisión; vías R1/R3 (§8); avisos si la revisión de eFuse y la de la ROM no casan | Común; el mapa lo da cada chip |
| `soc/esp/esp_efuse.h` | Bloques de eFuse desde un fichero compatible con `qemu_efuse.bin`; lectura por registros; MAC, revisión, selección de JTAG, `UART_PRINT_CONTROL`, `DIS_DOWNLOAD_MODE`, `DIS_DIRECT_BOOT` | El controlador cambia de registros entre chips: los parámetros van en el descriptor |
| `soc/esp/esp_cache_mmu.h` | Caché de solo lectura (vías, tamaño, línea), MMU de páginas de 64 KB, coste de fallo como lectura SPI, invalidar, precargar, bloquear | C3 (16 KB, 8 vías, dos ventanas) y C6 (32 KB, 4 vías, una ventana) |
| `soc/esp/intc_esp.h` | `IntcEspV1` e `IntcEspV2` (§3.5) | Uno por chip |
| `soc/esp/matriz_int.h` | Matriz de interrupciones parametrizada por la tabla de fuentes | Tabla por chip |
| `soc/esp/arranque.h` | Los pines de arranque en el flanco de CHIP_PU, `GPIO_STRAPPING`, el *force download* y el *Direct Boot* (`0xaedb041d`) | Tabla por chip |
| `periph/esp/spi_mem.h` | SPI0/SPI1 (MSPI): lectura por la caché, órdenes de usuario (las del *stub* de esptool y de OpenOCD), suspensión automática | Común |
| `periph/esp/uart.h` | UART con FIFO de 128, detección automática de baudios (la ROM la usa para sincronizar con esptool), RS485 e IrDA declarados | Común; el LP_UART es otra instancia |
| `periph/esp/gpio_matriz.h`, `io_mux.h` | Matriz GPIO (cualquier señal a cualquier pin), IO_MUX con funciones directas, **GPIO dedicada** por los CSR, filtros, sobre el `Pad` eléctrico de siempre (`FUN_DRV` en cuatro niveles) | Común; tablas de señales por chip |
| `periph/esp/systimer.h`, `timg.h`, `wdt.h` | SYSTIMER de 52 bits (2 contadores y 3 comparadores), TIMG con MWDT, RWDT y SWD (super watchdog) | Común |
| `periph/esp/assist_debug.h` | Monitor de SP (guarda de pila de ESP-IDF), monitor de regiones y registro de PC | Común |
| `periph/esp/usb_serial_jtag.h` | Dispositivo compuesto CDC-ACM + JTAG, detrás de `usb_dev_if`; reset y modo descarga por DTR/RTS (§6) | Común |
| `periph/esp/ledc.h`, `rmt.h`, `gpspi.h`, `i2c.h`, `twai.h`, `gdma.h`, `adc.h`, `sha.h`, `aes.h`, `rng.h` | Los periféricos de aula | Comunes, con `Caps` por chip |

Las piezas de placa nuevas (`SpiFlash`, `AutoResetEsp` y `Ws2812`) son de
`parts/` y no de Espressif. La `SpiFlash` (W25Q y GD25Q: JEDEC ID, SFDP,
lectura simple/dual/quad, programación de página, borrado de sector y de bloque,
estado y protección, suspensión) **le sirve también al QUADSPI del F446**.
Además, por `memoria_nv`, guarda su contenido entre simulaciones con
`contenido=`.

---

## 5. Lo que queda de la generalización (E0)

| # | Qué | Por qué | Riesgo para los invariantes |
| :--- | :--- | :--- | :--- |
| E0.1 | `DebugCaps`/`DebugAttach` a `common/depuracion.h` | Que `mcu_if.h` no incluya `cortex_m4f.h` | Nulo: es mover un `struct` |
| E0.2 | `GdbRsp` → `GdbRsp` (protocolo: socket, tramado, paquetes, hilos, `monitor`, `qXfer`, traza) + **`gdb_arq_if`** (registros y `target.xml`, parar/seguir/paso, puntos y vigilancias, reset, memoria) + `gdb_arq_armv7m.h`, que **emite exactamente la misma secuencia de accesos al DAP** | El *backend* RISC-V entra por ahí | **El más alto**. T96, T97 y P21 y las pruebas de OpenOCD O1-O5 tienen que dar lo mismo **paquete a paquete** con la traza puesta |
| E0.3 | Hilos en el motor (`qfThreadInfo`, `Hg`/`Hc`, `T`, `vCont` por hilo) | Varios harts. Hoy, en ARM, los hilos de RTOS los da OpenOCD | Bajo: con un solo hilo, no cambia nada |
| E0.4 | `mcu_if`: `nodos_depuracion()` con nombres (`swclk`, `swdio`, `tck`, `tms`, `tdi`, `tdo`, `trst`, `srst`), con los dos virtuales actuales como casos particulares | La sonda JTAG necesita cuatro nodos | Nulo con la implementación por omisión |
| E0.5 | `Encapsulado`: máscaras de 32 bits por puerto (`bonded`, `cuenta_gpio` hasta 32) | El C6 tiene GPIO0-30 en un banco y le falta el 14 | Nulo: los inicializadores siguen valiendo |
| E0.6 | Sonda `remote_bitbang` con **JTAG**: `0`-`7` (TCK/TMS/TDI), `R` (TDO), `r`-`u` con TRST y SRST, y el mismo avance de tiempo por reloj de pared que ya tiene en SWD | G2 en RISC-V | Nulo si el modo SWD no cambia: O1-O5 iguales |
| E0.7 | Sonda CMSIS-DAP con `DAP_JTAG_Sequence`, `DAP_JTAG_Configure` y `DAP_JTAG_IDCODE` | Una sonda JTAG rápida (por paquetes) para OpenOCD | Nulo con SWD intacto |
| E0.8 | `depuracion=` neutral: `dap` pasa a significar **interno** (en RISC-V, el DM) y se admite `dm` como sinónimo; `pines`, `openocd` y `cmsis-dap` siguen igual; se añade `usb` (G3) | Que el XML no diga «DAP» a un RISC-V | Nulo |
| E0.9 | `PowerPads`/`Nomenclatura`: CHIP_PU como pin de reset de la familia, VDD3P3, VDD3P3_RTC, VDD_SPI, VDDA y XTAL | `reset_pin()` ya es neutral; falta el nombre | Nulo |
| E0.10 | `aparca_en_wfe()`: cada chip pone su bucle (`wfi` en RISC-V) | Ya es virtual; solo hay que saberlo | — |

**Cierre de E0:**
* las ocho suites con las mismas comprobaciones y el mismo tiempo simulado al
  picosegundo, en las cuatro plataformas;
* T96, T97, P21, O1-O5 y las pruebas de `cmsis_dap.py` **idénticos paquete a
  paquete** con la traza;
* y una pieza de laboratorio, `TapLab` (un TAP con IDCODE y BYPASS sobre
  cuatro nodos), contra la que `openocd -c "adapter driver remote_bitbang" -c
  "transport select jtag"` lee el IDCODE por `remote_bitbang` y por CMSIS-DAP.

---

## 6. Descarga del firmware

Sin cambios de fondo respecto a la primera versión. Lo que cambia es que ahora
se sabe qué existe y en qué fase entra cada vía.

| Vía | Herramienta del alumno | Cómo, en el modelo | Fase (C3 / C6) |
| :--- | :--- | :--- | :--- |
| **D1** Carga directa | `mcu-sim placa.xml fw` | ELF *bare-metal* en IRAM sin ROM (arranque directo del modelo); imagen completa de Flash (`merge-bin`, `qemu_flash.bin`); `flash_args`; `--efuse`; `contenido=` de la `SpiFlash` | E3 (ELF) y E4 (imagen) / E9 |
| **D2** esptool por la UART0 | `idf.py -p rfc2217://localhost:3355 flash monitor`, `esptool`, PlatformIO; Arduino IDE con los redirectores de `doc/puente_serie.md` §4 | `PuenteSerie` en U0TXD/U0RXD + **`AutoResetEsp`**: dos transistores cruzados y un RC en CHIP_PU; DTR → GPIO9, RTS → CHIP_PU. La ROM de verdad contesta a SLIP, carga el *stub* de esptool en RAM, y el *stub* programa con SPI1 | E5 / E9 |
| **D3** esptool por el USB-Serial-JTAG | Las mismas, con el puerto del CDC y **`--before usb-reset`**: por un puerto remoto esptool no ve el VID/PID y no puede elegir la secuencia solo [esptool, «Remote Serial Ports»] | `usb_serial_jtag.h` detrás de `usb_dev_if` + el `PuenteUsb` que ya existe. El periférico interpreta DTR/RTS como dice [TRM-C3 tabla 30.3-2]: `RTS=0, DTR=1` pone la bandera de descarga y `RTS=1, DTR=0` resetea el chip | E7 / E9 |
| **D4** OpenOCD `program_esp` | `idf.py openocd` + `program_esp fw.bin 0x10000 verify` | OpenOCD carga en RAM su algoritmo de Flash, que usa la ROM, por G2 o G3 | E6 / E10 |

**El `load` de GDB** contra el stub interno vale para:
* programas en RAM, cargados por su LMA;
* y, mediante un `programador_nv_if` de Espressif, para **segmentos cuya LMA
  sea un desplazamiento de Flash**. Ese programador escribe la `SpiFlash` con
  las órdenes de SPI1, como el *stub* de esptool.

Un ELF de ESP-IDF tiene sus segmentos en direcciones de la caché
(`0x4200_0000`), no desplazamientos de Flash. Escribir ahí sería más permisivo
que el silicio, así que el programador lo rechaza diciendo cómo se descarga
(D2 o D4).

**El tiempo de esptool** (riesgo 3 de la versión anterior) se resuelve en E5
con las dos medidas ya propuestas:
* la receta recomienda `--tiempo-real`;
* y el puente garantiza un **mínimo de tiempo simulado** entre dos cambios de
  DTR/RTS consecutivos. Así GPIO9 sigue abajo cuando sube CHIP_PU, que es
  cuando se muestrean los pines de arranque.

---

## 7. Depuración del firmware

| Vía | `depuracion=` | Camino | Herramienta | Fase (C3 / C6) |
| :--- | :--- | :--- | :--- | :--- |
| **G1** Stub interno | `dap` o `dm` | `GdbRsp` + `gdb_arq_riscv` → `dmi_if` → DM | `riscv32-esp-elf-gdb`, `gdb-multiarch`, `idf.py gdb`, VS Code (extensión de ESP-IDF o `cppdbg`), Espressif-IDE | E6 / E10 |
| **G2** OpenOCD por los pines | `openocd` o `cmsis-dap` | Sonda → GPIO4-7 → TAP → DTM → DM | `openocd-esp32` sin modificar con `target/esp32c3.cfg` o `esp32c6.cfg`: `program_esp`, puntos de ruptura en Flash, hilos de FreeRTOS, *core dump*, `apptrace`. Y el OpenOCD genérico, con `target create riscv` | E6 / E10 |
| **G2'** Stub de pines propio | `pines` | `GdbRsp` + `gdb_arq_riscv` → cliente DTM propio → `JtagPines` | El mismo GDB que G1, pero por los pines | E6 |
| **G3** JTAG por USB | `usb` | OpenOCD `remote_bitbang` → `PuenteUsb` (clase JTAG de Espressif) → endpoints del USB-Serial-JTAG → TAP → DTM → DM | `openocd-esp32` | E7 / E9 |

Que la vía de los pines esté disponible depende de los eFuses y, en el C6, de
GPIO15 (§2.3). Se modela así: el chip sale **con el JTAG por USB**, como sale
de fábrica. Una placa que quiera el JTAG en los pines lo dice con su fichero
de eFuse o con un atributo del `<mcu>`. Si alguien engancha una sonda a
GPIO4-7 con el JTAG en el USB, el modelo **avisa**, como avisa hoy el STM32
cuando se usan los pines SWD con la función cambiada.

---

## 8. La ROM, las fuentes y las licencias

### 8.1 La ROM

| Vía | Qué es | Veredicto |
| :--- | :--- | :--- |
| **R1. La ROM de verdad** | El ELF de `espressif/esp-rom-elfs` del chip y la revisión, con el nombre `<chip>_rev<REV>_rom.elf` [README de esp-rom-elfs]. Apache-2.0 | **La de por omisión** |
| R2. Una ROM sustituta | Como la R1 del MSP432, que es DriverLib compilada desde el SDK | **No es posible**: el código de la ROM de Espressif no es abierto. Tampoco hace falta, porque R1 se puede distribuir |
| **R3. Sin ROM** | `--rom ninguna`: cualquier acceso a la ventana es un error de bus, avisado una vez | Para el arranque directo de E3 y las suites *bare-metal* |

**Qué hacer con la licencia.** Apache-2.0 es compatible con la AGPLv3 del
proyecto en esta dirección, como CMSIS. Hay dos opciones:
1. **versionar** los segmentos cargables de las ROM que usan las suites (C3
   rev. 0.4 y rev. 1.1, C6 rev. 0.x) como `.inc` generados, con su entrada
   en `TERCEROS.md`, como hoy `rom_r1.inc` del MSP432;
2. o **descargarlos** en el CI de una etiqueta fija de `esp-rom-elfs` con su
   suma SHA-256.

Este análisis recomienda **la primera** para lo que usan las suites, porque
hace el CI independiente de la red. El resto se busca en la instalación de
ESP-IDF del alumno (`~/.espressif/tools/esp-rom-elfs/<versión>/`) o se da con
`--rom`. La última etiqueta del repositorio al escribir esto es `20260528`.

**La revisión manda.** El C3 v1.1 trae **otra ROM, con funciones en otras
direcciones**, y un binario hecho para v0.4 no arranca en v1.1 [AR-C3]. El
descriptor fija la revisión, y con ella los eFuses de revisión
(`EFUSE_RD_MAC_SPI_SYS_5_REG[25:23]` y `SYS_3[20:18]`, [ER-C3 §1.2]) y la ROM
por omisión. Si se pide una ROM de otra revisión, `sim` lo dice al montar.

### 8.2 Dos fuentes de cada dato

| Papel | Fuente 1 (no se versiona) | Fuente 2 (se versiona o se cita) |
| :--- | :--- | :--- |
| Mapa, registros y valores de reset | [TRM-C3], [TRM-C6] | Cabeceras `soc/*_reg.h` y `*_struct.h` de ESP-IDF (Apache-2.0); las SVD de Espressif |
| Pines, encapsulados y variantes | [DS-C3], [DS-C6] | `soc/io_mux_reg.h`, `gpio_sig_map.h` de ESP-IDF; un `gen_pines.py` como los de F3 y E4 |
| Erratas | [ER-C3], [ER-C6] | — (se modelan y se pueden apagar) |
| Placas | Esquemáticos de las DevKit (Espressif) | [HW-C3], [HW-C6] |
| ISA y depuración | [PRIV], la ISA no privilegiada, [DBG] | Spike, `riscv-tests`, `riscv-arch-test` |
| Comportamiento del arranque | La ROM misma (R1) | El QEMU de Espressif (C3 y C6): **solo como referencia de comportamiento, sin copiar código**, por ser GPL-2.0 |

**Registros sin documentar.** El arranque de ESP-IDF toca registros analógicos
que no están en los TRM (calibración del PLL por `regi2c`, el PHY). En el C6,
`I2C_ANA_MST` sí está documentado como periférico; los esclavos analógicos que
hay detrás de él no. La regla de siempre: o se modelan con dos fuentes, o se
declaran (contestan, no guardan y avisan una vez), y en ningún caso se deja
seguir al firmware sin decirlo.

---

## 9. Placas

| Placa | Chip | Lo que lleva | Fase |
| :--- | :--- | :--- | :--- |
| `esp32c3_devkitm1.xml` | ESP32-C3 (Flash externa de 4 MB) | Puente USB-UART con auto-reset en UART0, **LED RGB direccionable (WS2812) en GPIO8**, pulsadores BOOT (GPIO9) y RST (CHIP_PU), cristal de 40 MHz | E3-E5 |
| `esp32c3_supermini.xml` | ESP32-C3FH4 | **Solo USB nativo** (USB-Serial-JTAG), LED en GPIO8, BOOT y RST | E7 |
| `esp32c3_y_f407.xml` | C3 + F407 | Dos familias por UART o I2C en la misma placa: la prueba de que la frontera aguanta | E3 |
| `esp32c6_devkitc1.xml` | ESP32-C6 (QFN40, Flash externa) | **Dos USB**: un puente USB-UART en UART0 y el USB-Serial-JTAG; LED RGB en GPIO8; BOOT y RST | E9 |

Las placas se contrastan con su esquemático antes de cerrar la fase, como se hizo
con la MB1035: GPIO de cada LED, resistencias, condensador de CHIP_PU. Lo que no
salga del esquemático, `[de modelo]`.

---

## 10. Plan por fases

Es el método de siempre: fases pequeñas, cada una con sus entregas y su criterio
de cierre, un banco propio con su invariante (`testrv`, `testc3` y `testc6`, con
su `asan*`) y, como condición de todas, **sin mover los invariantes de las ocho
suites**. Los firmwares de prueba se versionan con su huella (T-22); compilarlos
pide `riscv32-esp-elf-gcc` (el de ESP-IDF), pero ejecutar las suites no.

### Vista de conjunto

| Fase | Qué | Banco | Depende de | Tamaño |
| :--- | :--- | :--- | :--- | :--- |
| **E0** | Lo que queda de la generalización (§5) | Las ocho | — | Medio |
| **E1** | Núcleo RV32IMAC genérico + SoC de laboratorio | `testrv` | E0.1 | Grande |
| **E2** | Debug Module, *triggers*, DTM, JTAG en pines y GDB, en el laboratorio | `testrv` | E0, E1 | Grande |
| **E3** | ESP32-C3: el SoC con arranque directo, sin ROM | `testc3` | E1 | Grande |
| **E4** | C3: Flash SPI, MSPI, caché/MMU, ROM real y ESP-IDF | `testc3` | E3 | Grande, **el de más riesgo** · **hecha** |
| **E5** | C3: descarga con esptool por la UART0 (D2) | `testc3` + Python | E4 | Pequeño |
| **E6** | C3: depuración G1, G2 y G2', y D4 | `testc3` + Python | E2, E4 | Medio |
| **E7** | C3: USB-Serial-JTAG, D3 y G3 | `testc3` + Python | E5, E6 | Medio |
| **E8** | C3: periféricos de aula | `testc3` | E4 | Grande, divisible |
| **E9** | ESP32-C6: núcleo HP, SoC, ROM, ESP-IDF, D1-D3 y G1-G3 | `testc6` | E4-E7 | Grande |
| **E10** | C6: el núcleo LP como segundo hart, LP SRAM y periféricos LP | `testc6` | E9 | Medio |
| **E11** | C6: sus periféricos propios (MCPWM, PCNT, ETM, PARL_IO, TWAI1, ECC…) | `testc6` | E9 | Grande, divisible |
| **E12** | Radio paravirtual (`VirtWifi`), opcional | `testradio` | E9 y R1-R3 de `analisis_wifi_bl.md` | Grande |
| **E13** | `mcu-sim-gui`: placas y dibujos | — | E3, E9 | Pequeño |

```
E0 ──► E1 ──► E2 ─────────────────────┐
        │                              ▼
        └──► E3 ──► E4 ──┬──► E5 ──► E7 ──► E9 ──► E10
                         ├──► E6 ──┘         │
                         └──► E8             ├──► E11
                                             └──► E12 (opcional)
E13 en paralelo desde E3
```

### E0 · Lo que queda de la generalización

**Objetivo:** que el resto del plan no tenga que tocar nada de ARM.

**Entregas:** E0.1 a E0.10 de §5, cada una en su commit y con las suites
pasadas antes de la siguiente.

**Cierre:** el de §5.

**Tamaño:** medio. Lo delicado es E0.2. Conviene hacerlo en dos commits:
primero mover el código sin cambiarlo y, después, cortar la interfaz.

**Hecho (2026-11-07, commits `a47352b` y `dda29ce`):**
* E0.1: `common/depuracion.h`; `mcu_if.h` ya no incluye `cortex_m4f.h`.
* E0.2 y E0.3: `common/gdb_rsp.h` partido en `GdbRspBase` (el protocolo, con
  los hilos) y `GdbRsp` (ARMv7-M, el código de antes, sin cambiar una línea de
  lo que emite). La interfaz no es un `gdb_arq_if` aparte sino los virtuales
  `arq_*` y `mem_*` de la base: el mismo corte, con un objeto menos. Con un
  hilo, `H` contesta siempre OK y `T` vacío, como antes.
* E0.4: `mcu_if::nodo_depuracion(nombre)`.
* E0.5: puertos de hasta 32 pines (`N_PINES_PUERTO_MAX`).
* E0.6 y E0.7: las dos sondas con JTAG (`PinesJtag`), además de SWD.
* E0.8: `depuracion="dm"` es sinónimo de `dap`.
* E0.9 queda para E3, donde el C3 da nombre a sus pines de alimentación; E0.10
  no pide código.
* `lee_elf` acepta la máquina esperada (`ELF_ARM` por omisión, `ELF_RISCV`).

Cierre: **las ocho suites con las mismas comprobaciones y el mismo tiempo
simulado al picosegundo**, y las pruebas de OpenOCD de ARM sin un fallo con el
OpenOCD de git: `make openocd` (19, O1-O5) y `make cmsis-dap` (32). El `TapLab` del cierre no hizo falta como pieza
aparte: el TAP de E2 (`rv_dtm_jtag.h` en `jtag_pines.h`) es el que OpenOCD lee
por `remote_bitbang` y por CMSIS-DAP en JTAG (`make openocd-rv`, L1-L2).

### E1 · El núcleo RV32 genérico y el laboratorio

**Objetivo:** un hart RISC-V correcto, validado contra dos fuentes
independientes y sin una línea de Espressif.

**Entregas:**
1. `src/rv/hart/`: `rv_caps.h`, `rv_state.h`, `rv_decod.h` (con la expansión
   de C), `rv_exec.h` (I, M, A, C), `rv_csr.h` (registro de CSR con permisos y
   modo), `rv_trap.h`, `rv_pmp.h` y `rv_tiempos.h`.
2. `NucleoRv` (`SC_MODULE`): *sockets* I y D, quantum LT, transacciones atómicas
   marcadas, `wfi` con el reloj parado e `IntegracionRv`.
3. `src/rv/irq/`: `rv_irq_if`, `IrqEstandar` y `Clint`.
4. `src/rv/lab/`: `SocLabRv` (RAM, CLINT, UART de pega, HTIF) y
   `top/sc_main_rv.cpp` → `make testrv`, `asanrv`.
5. `verif/rv/`:
   * el *port* de `riscv-tests` (entorno `-p`, enlazado para el laboratorio),
     con sus binarios versionados;
   * el de `riscv-arch-test` con sus firmas de referencia;
   * el guion que compara la traza con la de Spike;
   * y `gen_rv_vectores.py`, los vectores de decodificación de dos fuentes.
6. CoreMark compilado para RV32IMC y para RV32IMAC con otro `core_portme`. Los
   fuentes ya están en `verif/fw/coremark`.
7. Las extensiones del integrador, `rv_csr_ext_if` y `rv_ext_if`, con una
   prueba de laboratorio de cada una.

**Cierre:**
* `riscv-tests` `rv32ui`, `rv32um`, `rv32ua`, `rv32uc` y `rv32mi`, todas en
  verde, con `RV_LAB_IMAC`;
* `riscv-arch-test` RV32 I, M, A, C, Zicsr, Zifencei y *privilege* (modos M
  y U), con las firmas iguales a las de referencia (hecho: ACT4, 220 pruebas);
* **traza idéntica a la de Spike** en un programa propio con traps,
  interrupciones del CLINT y PMP, y en CoreMark;
* CoreMark termina con su CRC correcto en las dos variantes;
* las tres variantes de desalineado probadas, incluida la errata CPU-863;
* `testrv` en el CI con su invariante, en las cuatro plataformas.

**Tamaño:** grande, pero bastante menos que el Thumb-2 de la fase F2: la ISA
base son unas 40 instrucciones, la C se expande y no hay modo Thumb ni ITSTATE.

**Hecho (2026-11-07):**
* **Los ficheros** no son los ocho de la lista sino cinco: `rv_caps.h`,
  `rv_decod.h`, `rv_ifaces.h` (las interfaces de memoria, interrupciones y
  extensiones), `rv_hart.h` (estado, ejecución, CSR, traps, PMP, triggers y
  modo depuración: C++ puro, sin SystemC) y `nucleo_rv.h`. Los tiempos van en
  `RvCaps::tiempos`. Separarlos más habría sido partir una clase por la mitad.
* **`testrv`, 234 comprobaciones** (con las de E2; 236 con `irq_spike`): V1 las 247 comprimidas
  válidas y las 22 reservadas de RV32C, contra GNU as (`gen_vectores_c.py`,
  con `.option rvc`/`norvc`); V4-V5 los 77 riscv-tests en RV32IMAC y, menos
  `ua`, en RV32IMC (`ua` tiene que fallar ahí); V6 `ma_data` con
  `Hardware` y `HardwareCpu863`; V7 ocho pruebas propias (`verif/rv/propias`:
  excepciones, modo U y PMP, mtvec, triggers, CLINT con `wfi`, la errata
  CPU-863 y las extensiones del integrador, con y sin ellas); V8 **la traza
  idéntica a la de Spike** (instrucciones retiradas: PC, instrucción,
  registro y valor) en cuatro programas propios y en CoreMark, 352 381
  instrucciones; V9 CoreMark con sus CRC.
* **Spike** (`riscv-isa-sim` de git, 1.1.1-dev) con `--isa=rv32imac_zicsr_
  zifencei_zicntr --priv=MU`. Para que las trazas coincidan donde se lee el
  reloj, `rt/inicio.S` pone a cero `mcycle`, `minstret`, `a0` y `a1` antes de
  `main`: la ROM de arranque de Spike deja cinco instrucciones en los
  contadores y el DTB en `a1`. Y `trig.S` escribe un `mcontrol` antes de leer
  el tipo: tras el reset, la 0.13.2 da 2 y Spike, que sigue la 1.0, 15.
* **Los binarios se versionan** (`verif/rv/elf`); riscv-tests, comprimido
  (`riscv-tests.tar.xz`, 21 KB, BSD-3; `TERCEROS.md`). `make testrv` lo extrae
  en `build/rvt`. El banco no necesita la cadena de RISC-V ni Spike.
* `asanrv`, limpio. El invariante: `testrv 234 8799500000 resto` (236 y
  8811500000 desde `irq_spike.S`).
* **La traza de Spike con interrupciones del CLINT** (`irq_spike.S`, hecho
  después, con E3): Spike sube `mtime` en tandas de 5000 instrucciones y solo
  mira las interrupciones al empezar una tanda o tras escribir un CSR; el
  programa deja cada interrupción pendiente y enmascarada, la desenmascara con
  una escritura de CSR, usa `mtimecmp` solo en el pasado o en el futuro
  lejano y no lee `mtime`. Así las dos trazas son iguales (244 instrucciones)
  sin deformar el núcleo. `testrv` pasa a 236 comprobaciones.
* **`riscv-arch-test` (ACT4, hecho después, con E3).** La rama `act4`
  (commit `bad8b12`) ya no usa riscof: genera las pruebas, calcula los
  resultados con **Sail 0.16** al compilarlas y los mete en el propio ELF, que
  se comprueba solo y dice `RVCP-SUMMARY: TEST PASSED`. Lo nuestro:
  * **La configuración del DUT**, `verif/rv/act/mcusim-lab-rv32imac/`
    (derivada de la de Hazard3, el más parecido: RV32IMAC con U y 16 PMP):
    `test_config.yaml`, la de UDB (`mcusim-lab-rv32imac.yaml`), `sail.json`,
    `link.ld` (4 MB de RAM) y `rvmodel_macros.h` (HTIF, la UART de pega, el
    CLINT y la interrupción externa). Declara I, M, Zmmul, A, Zaamo, Zalrsc,
    C, Zca, Zicsr, Zifencei, Zicntr, Zihpm (contadores a cero), Zihintpause,
    U y Sm 1.12; no Sdtrig (los triggers son `mcontrol` de la 0.13). Salen
    **220 pruebas**: I 39, M 8, Zmmul 4, Zaamo 9, Zalrsc 2, Zca 26, Zicsr 6,
    Zicntr 2, Zihpm 2, Zifencei 1, Zihintpause 1, Sm 28, U 9, PMP 60,
    Exceptions 8 e InterruptsSm 15.
  * **El laboratorio**, dos cosas nuevas que pide la configuración: el CSR
    `time` lee el `mtime` del CLINT (`RvHart::fuente_tiempo`), y en
    `0x0C00_0000` está el «simple interrupt generator» de Sail (escribir en +4
    `(1<<31)|(1<<11)` levanta MEIP y `(1<<11)` lo baja), así que la externa
    se pide igual en Sail y aquí.
  * **Tres fallos del núcleo** que no habían visto ni riscv-tests ni Spike:
    faltaba `menvcfg`/`menvcfgh` (la 1.12 los pide con modo U; ahora existen,
    a cero, con `RV_Z_MSTATUSH`); una región TOR vacía (tope por debajo de la
    base) se tomaba por coincidencia parcial y daba fallo de acceso; y un
    `sc.w` sin reserva a una dirección donde no hay nada fallaba sin más, y
    Sail da fallo de acceso: ahora el SoC dice qué existe
    (`RvHart::existe_dir`) y se comprueba antes de la reserva.
  * **Los binarios se versionan** (`verif/rv/elf/act.tar.xz`, 1,2 MB,
    BSD-3-Clause; `TERCEROS.md`) y `make testrv` los extrae en `build/act`.
    `testrv` los pasa en **V10**, una comprobación cada uno (457 en total;
    `testrv 457 28577500000 resto`: V10 añade 19 766 us y lo demás dura lo
    mismo). Uno suelto: `testrv --elf x.elf` (con `RV_TRAZA_ELF=fichero`,
    la traza).
  * **Regenerarlos**: `make -C verif/rv act ACT=<riscv-arch-test>`, con
    `sail_riscv_sim` 0.16 y **GCC 15** (`riscv-none-elf-gcc` de xPack 15.2.0-1;
    ACT4 rechaza el 13) en el `PATH`. ACT4 usa las gemas de Ruby de UDB por
    `bundle`; donde no se llega a rubygems.org, `SIN_RUBY=1` pone delante
    `verif/rv/act/bin/bundle`, que hace lo mismo para una configuración
    «fully configured» con `udb_sin_ruby.py` (sin validar el esquema).

### E2 · La depuración genérica, en el laboratorio

**Objetivo:** que cualquier RISC-V con DM 0.13 se depure con G1, G2 y OpenOCD
desde el primer día. Hacerlo **antes** del C3 tiene una ventaja más: cuando se
ponga en marcha el C3 ya habrá depurador.

**Entregas:**
1. `src/rv/debug/`: `rv_dm.h`, `rv_trigger.h`, el modo depuración del hart,
   `rv_dtm_jtag.h` y `jtag_pines.h`.
2. `common/gdb_arq_riscv.h` sobre el motor de E0.2, con hilos por hart y
   `target.xml` generado.
3. G1 (stub interno), G2' (stub de pines propio con su cliente DTM) y G2
   (OpenOCD por `remote_bitbang` y por CMSIS-DAP en JTAG), contra el
   laboratorio.
4. Un laboratorio con **dos harts** (`SocLabRv` con `n_harts = 2`) para probar
   `hartsel`, `haltsum`, «unavail» y el disparo cruzado antes de que haga falta
   en el C6.

**Cierre:**
* con `verif/gdb_client.h` y por G1, G2' y G2, **con los mismos resultados**:
  parar, leer y escribir los 32 registros, el `pc` y CSR, un punto de ruptura
  software en RAM, uno por *trigger*, un *watchpoint* de escritura, un paso,
  `monitor reset halt`, y `load` de un programa a RAM;
* OpenOCD genérico (`target create riscv`) por `remote_bitbang` y por
  CMSIS-DAP: `examine`, `halt`, `reg`, `mdw`, `load_image`, `resume`, y GDB por
  el servidor de OpenOCD;
* dos harts: GDB ve dos hilos, y parar uno no para el otro salvo con el disparo
  cruzado activado;
* la razón de velocidades G1/G2', medida y anotada como T97.

**Tamaño:** grande. El DM es la mitad; la otra mitad es el TAP y los tres
caminos.

**Hecho (2026-11-07):**
* `src/rv/debug`: `dmi_if.h`, `rv_dm.h` (DM 0.13.2: comandos abstractos de
  acceso a registro con `aarpostincrement` y `postexec`, program buffer,
  `abstractauto`, SBA de 8, 16 y 32 bits con `sbreadonaddr`,
  `sbreadondata` y autoincremento, `ndmreset` y `hartreset`, `resethaltreq`,
  varios harts con `hartsel`, «unavail» y disparo cruzado), `rv_dtm_jtag.h`
  (el TAP con IDCODE, `dtmcs`, `dmi` y BYPASS), `jtag_pines.h` (el TAP en
  cinco nodos, con sus pull-ups) y `rv_dm_cliente.h` (lo que hace OpenOCD con
  un DM, una vez). Los triggers y el modo depuración están en el hart (E1):
  `rv_trigger.h` no hizo falta.
* `src/rv/gdb`: `gdb_rsp_riscv.h` (el piso RISC-V de `GdbRspBase`: 33
  registros, `ebreak`/`c.ebreak` según el tamaño, Z1-Z4 por trigger con los
  triggers descubiertos, `watch:` en el aviso de parada, SBA o program
  buffer, un hilo por hart en «todo parado», `monitor reset halt`) y
  `gdb_stub_rv.h`, el mismo stub para G1 y G2'.
* `verif/sonda_jtag_rv.h`: la sonda de bancada (G2') y el DMI por ella.
* El laboratorio lleva el DM, el DTM y el TAP, con IDCODE `0x10AB1001`.
* `testrv`: W1 el DTM por los pines (93 ciclos de TCK por acceso al DM; T97,
  la razón de velocidades, se imprime: unas 20 veces en reloj de pared),
  W2 el DM y sus errores, W3 dos harts, W4-W5 **la misma sesión de GDB por G1
  y por G2' con los mismos resultados**, W6 dos hilos.
* **OpenOCD genérico** (`make openocd-rv`, `verif/openocd/riscv_lab.py`, 28
  comprobaciones): `target create riscv` por remote_bitbang y por CMSIS-DAP en
  JTAG (`examine`, `halt`, `reg`, `mdw`, `load_image`, `resume`, `reset
  halt`); dos harts con `target smp`; GDB (`gdb-multiarch`) por el servidor de
  OpenOCD y contra G1, con el mismo resultado. `testrv --servir=bitbang|
  cmsis-dap|gdb` deja el laboratorio en marcha con la sonda que se pida.

### E3 · ESP32-C3: el SoC, con arranque directo y sin ROM

**Objetivo:** el C3 que ejecuta firmware *bare-metal*, con pines eléctricos, en
una placa.

**Entregas:**
1. `soc/esp32c3/`:
   * `c3_desc.h`: descriptores ESP32-C3, C3FH4, C3FH4X y C3FH8X, con su
     encapsulado, su Flash interna o externa y su revisión;
   * `c3_mapa.h`: el mapa de [TRM-C3 tabla 3.3-3] y las fuentes de
     interrupción;
   * `esp32c3.h`: el SoC;
   * `esp32c3_mcu.h`: `mcu_if` y `REGISTRA_MCU(ESP32C3, …)`.
2. El decodificador de bus del C3, con los alias IRAM/DRAM de la SRAM1 y la ROM1.
3. `IntcEspV1` y la matriz de interrupciones del C3.
4. SYSTEM/SYSCON (reloj: cristal de 40 MHz, PLL, CPU a 80/160 MHz, APB), RTC_CNTL
   mínimo (causa de reset, perros guardianes, *force download*), SYSTIMER,
   TIMG×2, UART0/1, GPIO con la matriz, IO_MUX y la GPIO dedicada por los CSR,
   eFuse desde un fichero, y los pines de arranque en el flanco de CHIP_PU.
5. `Nomenclatura` `ESP32C3` (`GPIOn`, `IOn`, `MTMS`… y los de alimentación).
6. `RV_ESP32C3` con los CSR propios (`mpc*`, `cpu_gpio_*`), **sin `mie`/`mip`**.
7. `placas/esp32c3_devkitm1.xml` con su `.svg` y `placas/esp32c3_y_f407.xml`.
8. `verif/fw/esp32c3/`: `startup.S`, `c3_iram.ld`, y *blinky*, *uart*,
   *systimer*, *intc* (prioridades, umbral y flanco/nivel), *gpio_matriz* y
   CoreMark, con su huella.
9. `top/sc_main_c3.cpp` → `make testc3`, `asanc3`.

**Cierre:**
* `mcu-sim placas/esp32c3_devkitm1.xml verif/fw/esp32c3/blinky.elf` hace
  parpadear un LED de la placa, y el `uart.elf` saluda por el `PuenteSerie` en
  GPIO21/20;
* la prueba del INTC reproduce las reglas de [TRM-C3 §1.5.2]: prioridad 0
  enmascarada, empate por ID, borrado de flanco por pulso y latencia de 4
  ciclos;
* `esp32c3_y_f407.xml`: un C3 y un F407 se hablan por UART;
* `limitaciones()` dice que no hay ROM, Flash, caché, radio, ni periféricos de E8;
* `testc3` y `asanc3` en el CI con su invariante.

**Tamaño:** grande. Es el equivalente de la T1 del MSP432.

**Hecho (2026-11-07):**
* `soc/esp32c3/c3_desc.h`: el mapa (`c3::`, que hace de `c3_mapa.h`), las
  fuentes de interrupción y las señales de la matriz que se usan,
  `RV_ESP32C3` (RV32IMC con U, sin mie/mip, mtvec vectorizado y alineado a
  256, 16 PMP, sin contadores estándar, `HardwareCpu863`, 8 triggers con
  tcontrol), los pines con su función 0 y su configuración tras el reset, los
  encapsulados (QFN32 de 22 GPIO y el de las «X», de 16), la nomenclatura
  (`GPIOn`, `IOn`, `MTMS`…, `U0RXD`/`U0TXD`) y los descriptores ESP32-C3,
  C3FH4, C3FH4X y C3FH8X.
* `soc/esp32c3/esp32c3.h`: el SoC, con el decodificador de bus (la SRAM1 por
  las dos vistas, datos por el bus I solo en palabras alineadas, sin ejecutar
  por el D), RTC FAST, la ROM vacía con aviso, los CSR propios (`mpc*` y
  `cpu_gpio_*`), CHIP_PU, los pines de arranque y los resets de chip, de
  sistema y de CPU con su causa. `esp32c3_mcu.h`: `McuC3` y
  `REGISTRA_MCU(ESP32C3, …)`; `mcu-sim` lo monta.
* `soc/esp/intc_esp.h`: la matriz y `IntcEspV1`. `periph/esp/`: `uart_esp.h`,
  `systimer_esp.h`, `timg_esp.h` (T0 y MWDT) y `gpio_esp.h` (GPIO, matriz,
  IO_MUX sobre pads eléctricos, GPIO dedicada, interrupciones de pin).
  `soc/esp32c3/c3_sistema.h`: SYSTEM (relojes), RTC_CNTL (causa, reset por
  software, STORE, force download), eFuse de lectura desde un fichero, y un
  `BloqueGuardado` para SYSCON, PMS, ASSIST_DEBUG y el World Controller.
* `verif/fw/esp32c3/`: `startup.S`, `c3_iram.ld`, `blinky`, `uart`, `intc`
  (que incluye el SYSTIMER y TIMG0), `gpio_matriz` y CoreMark, con sus
  huellas en `verif/fw/huellas.txt`.
* `make testc3`: 57 comprobaciones; invariante `testc3 57 28556458348`.
  Igual en dos pasadas y con `asanc3`.
* Placas: `esp32c3_devkitm1.xml` (puente USB-UART en la UART0, BOOT y RESET),
  `esp32c3_supermini.xml` (con un LED de verdad en GPIO8, para ver blinky) y
  `esp32c3_y_f407.xml` (los dos chips por UART).
* **Lo que se ha quedado fuera de E3:**
  * el `.svg` de la DevKitM-1, que va con E13;
  * el LED WS2812 de la DevKitM-1, que necesita la pieza `Ws2812` y el RMT (E8);
  * el corte de reloj por `PERIP_CLK_EN`;
  * el RWDT y el SWD;
  * y la latencia de 4 ciclos del INTC como retardo: se cobra como coste al
    entrar (`lat_irq`), no se mide.
  * Los tiempos por instrucción de RV_ESP32C3 son de modelo: CoreMark sale a
    unos 2,3 CoreMark/MHz.

### E4 · C3: Flash SPI, caché, ROM y ESP-IDF

**Objetivo:** que arranque un `hello_world` de ESP-IDF sin tocar.

**Entregas:**
1. La pieza `SpiFlash`, con `contenido=`, dentro del encapsulado (C3FH4) o en
   la placa (C3).
2. `spi_mem.h` (SPI0 y SPI1) y `esp_cache_mmu.h` con los parámetros del C3.
3. `esp_rom.h` (R1, R3), `esp_imagen.h`, D1 completo (imagen, `flash_args`,
   `--efuse`) y las ROM versionadas o descargadas según se decida en §8.1.
4. Lo que ESP-IDF toca al arrancar, cada cosa modelada o declarada:
   * PMS y World Controller (la protección de memoria por omisión de ESP-IDF);
   * ASSIST_DEBUG (la guarda de pila);
   * RWDT, MWDT y SWD;
   * el BBPLL por `regi2c`;
   * el *brownout*;
   * y el XTAL32K WDT.
5. SHA-256 en `hash_algo.h` (ya prevé el SHA-2) y el acelerador SHA, para que la
   ROM y el bootloader puedan comprobar imágenes.
6. `verif/fw/esp32c3/idf/`: `hello_world`, `blink` (con el `led_strip` por RMT
   declarado, o un LED simple) y `uart_echo`. Se compilan con las dos ramas de
   ESP-IDF con soporte en ese momento (hoy, v5.5 y v6.0) y para **v0.4 y
   v1.1**, y se versionan (Apache-2.0).
7. Un firmware *bare-metal* en **Direct Boot** (`0xaedb041d`): la ROM, la caché
   y la MMU ejecutando desde la Flash, sin ESP-IDF. Es el escalón intermedio
   para localizar fallos.

**Cierre:**
* **Direct Boot**: el firmware *bare-metal* corre desde la Flash por la caché;
* **ESP-IDF**: la consola enseña los mensajes de la ROM (causa de reset y
  `SPI_FAST_FLASH_BOOT`), el bootloader de segunda etapa, la tabla de
  particiones y `Hello world!`; `esp_restart()` vuelve a empezar. Con las dos
  ramas de ESP-IDF y en las dos revisiones del chip;
* con la ROM de v1.1 y un binario de v0.4, el chip falla como en la placa, y
  `sim` lo avisa al montar;
* con GPIO9 a masa al subir CHIP_PU, la ROM anuncia el modo descarga y espera;
* un `blink` de Arduino-ESP32 3.x arranca;
* cada registro sin documentar que se haya tocado está en `todo.md`, con su
  fuente o con «declarado».

**Tamaño:** grande, y **el de más riesgo**. Mitigación: Direct Boot antes que
ESP-IDF, y ESP-IDF sin Wi-Fi ni Bluetooth.

**Hecho (2026-11-07).** `testc3` 107/107 (C1-C7 de E3, iguales al
picosegundo, y C8-C13 nuevos).
* **La Flash.** `parts/spi_flash.h` (`SpiFlash`: W25Q y GD25Q, por
  transacciones; programar y borrar con sus tiempos y WIP) dentro del
  encapsulado (C3FH4, C3FN4, C3FH4X...) o en la placa, con el CS en SPICS0;
  `contenido=` la guarda. Placas: la SuperMini y la DevKitM-1 (C3FN4) con la
  interna, y la nueva **DevKitC-02** (WROOM-02) con una `SpiFlash` aparte.
* **SPI0/SPI1** (`periph/esp/spi_mem.h`): las órdenes fijas y las de usuario,
  ocupadas mientras dura la transacción en la línea; SPI0 da el coste de un
  fallo de caché. **Caché y MMU** (`soc/esp/esp_cache_mmu.h`): 16 KB, 8 vías,
  líneas de 32 B, LRU; las 128 entradas de 64 KB; las dos ventanas.
* **La ROM (R1)**: `gen_rom_c3.py` reconstruye además lo que el ELF de
  esp-rom-elfs no trae en sus segmentos: la imagen de los datos iniciales que
  `_init` copia a la DRAM (sin ella, `rom_spiflash_legacy_data` era NULL) y
  las tablas `.rodata.interface*` (`ets_rom_layout_p`). La rev. 3 queda igual
  que la `esp32c3-rom.bin` del QEMU de Espressif salvo 8 bytes sin uso.
* **Lo que toca el arranque** (`regs_c3.inc`: los valores de reset de las
  cabeceras de ESP-IDF, con `gen_regs_esp.py`): eFuse con la revisión del
  chip y la orden de leer; SHA (la ROM comprueba el SHA-256 del bootloader);
  REGI2C; el RNG de SYSCON; la calibración del reloj lento en TIMG; el RWDT y
  el flashboot de MWDT0/RWDT; PMS/WCL/ASSIST_DEBUG que vuelven a cero con la
  CPU; la radio (FE, NRX, BB) que guarda. Y dos arreglos que no eran del C3:
  `NucleoRv` no veía un pulso de reset más corto que su adelanto LT (el de
  `esp_restart()`), y el SYSTIMER cogía PERIOD_MODE solo al hacer
  COMPx_LOAD (ESP-IDF lo pone después: sin el tic de FreeRTOS, saltaba el
  perro de las interrupciones). Cada registro sin documentar, en `todo.md`
  (P-19/E4) con su fuente o «declarado».
* **D1 completo**: `mcu-sim placa.xml flash_args` (cada .bin en su sitio),
  `fichero.bin` (desde 0: merge-bin o Direct Boot) o `fichero.bin@0x10000`;
  `rom=ninguna|omision|rev3|rev101|<ELF>` y `efuse=` en el `<mcu>`. Un ELF
  de ESP-IDF se rechaza diciendo que se descarga con sus .bin; un ELF
  *bare-metal* sigue yendo a la SRAM (E3).
* **Los binarios**: `verif/fw/esp32c3/idf/idf-v5.5.tar.xz` y
  `idf-v6.0.tar.xz` (210 KB): `hello_world`, `blink` (GPIO8, sin el
  `led_strip` del registro) y `uart_echo`, de **v5.5.6** y **v6.0.3**, con
  `construye.sh`. Y `direct_boot.bin` (`0xAEDB041D`, c3_flash.ld).
* **El cierre**, punto por punto: Direct Boot corre desde la Flash (C8);
  ESP-IDF enseña la ROM, el bootloader, la tabla de particiones y
  `Hello world!`, y `esp_restart()` vuelve a empezar (rst:0xc), con las dos
  ramas y en v0.4 (C9) y v1.1 (C10); blink y uart_echo (C11); GPIO9 a masa:
  «waiting for download» sin que nadie resetee (C12); y con la Flash vacía,
  «invalid header» y los perros del flashboot (rst:0x7, rst:0x10).
  **Dos matices.** Un binario de ESP-IDF 5.5/6.0 con la revisión mínima de
  omisión (v0.3) **arranca en v0.4 y en v1.1**, también con la ROM
  cambiada: llama a la ROM por la tabla de su revisión. Lo que se avisa al
  montar es la pareja ROM-eFuse que no existe en el silicio (C13). Y el
  `blink` de Arduino-ESP32 3.x **queda pendiente**: su núcleo trae
  bibliotecas precompiladas que no se pueden bajar desde aquí.

### E5 · C3: esptool por la UART0 (D2)

**Entregas:**
1. La pieza `AutoResetEsp`, eléctrica, con el RC de CHIP_PU.
2. El mínimo de tiempo simulado entre cambios de control en el `PuenteSerie`.
3. La detección automática de baudios de la UART del C3, si no entró en E3.
4. Las recetas en `doc/puente_serie.md`.

**Cierre:**
* `verif/esp/esptool.py`, al estilo de `programa.py`: `chip-id`, `flash-id`,
  `write-flash` + `verify-flash`, **con el *stub* y con `--no-stub`**, a 115 200
  y a 921 600 baudios; y el firmware nuevo arranca. Si no hay `esptool`, la
  prueba se omite diciéndolo, salvo con la variable de obligatoriedad que usa
  el CI;
* `idf.py -p rfc2217://localhost:3355 flash monitor`, probado a mano en las
  tres plataformas y anotado en la matriz de `doc/puente_serie.md`;
* el invariante de `testserie` no se mueve.

**Tamaño:** pequeño.

**Hecho (2026-11-08).** esptool 5.6 de verdad programa la DevKitM-1 simulada
por RFC 2217, con el *stub* y sin él, a 115 200 y a 921 600, y el firmware
nuevo arranca.
* **`AutoResetEsp`** (`parts/auto_reset_esp.h`): los dos NPN cruzados entre DTR
  y RTS y el RC de CHIP_PU (10 kΩ, 1 µF: EN sube con tau = 10 ms); un pulsador
  de RESET en el mismo nodo descarga el condensador. En las dos DevKit, con el
  `PuenteSerie` en `rfc2217` y sus `dtr` y `rts`, y GPIO8 con su pull-up (si
  flota, la ROM lee boot:0x0 y no entra en descarga).
* **El tiempo entre cambios de control** no se resuelve con un mínimo: por RFC
  2217 pySerial espera la respuesta de cada SET-CONTROL sondeando cada 50 ms de
  reloj, y entre eso y el ritmo de la simulación el reinicio de esptool
  (DTR=0 RTS=1 · 100 ms · DTR=1 RTS=0 · 50 ms · DTR=0) llega deformado:
  CHIP_PU subía con GPIO9 todavía alto. Se hace lo mismo que el servidor
  `esp_rfc2217_server` de Espressif con una placa de verdad: `reinicio="esp"`
  en el puente reconoce el **principio** de la secuencia y la hace él en tiempo
  simulado (UnixTightReset y HardReset de esptool); lo demás de la secuencia se
  contesta y no se aplica. DTR y RTS salen inactivos.
* **La detección de baudios de la UART** (LOWPULSE, HIGHPULSE, POSPULSE,
  NEGPULSE, RXD_CNT con AUTOBAUD_EN): la ROM espera 128 flancos del SYNC y saca
  el divisor de (LOW + HIGH + 2) / 2. Sin ella no contesta nunca.
* **La latencia de las interrupciones**: una petición nueva no entra en la
  frontera de instrucción que sigue sino en la otra (dormido en `wfi`, en el
  acto). Sin eso la ROM fallaba (Load access fault en `uart_rx_intr_handler`):
  `uart_buff_switch` habilita la interrupción de la UART0 y una instrucción
  después dice que la UART es la 0; en medio entraba la interrupción con la
  UART del USB (3) todavía puesta. C1-C13 no se mueven.
* **`verif/esp/esptool_c3.py`** (`make esptool-c3`, en el CI con
  `ESPTOOL_OBLIGATORIO=1`; sin esptool se omite): `chip-id` con el *stub* a
  115 200, `flash-id --no-stub`, `write-flash` de blink a 921 600 con el *stub*
  (MD5 de las tres imágenes), `verify-flash --no-stub` (el MD5 de la ROM),
  `write-flash --no-stub` a 921 600 y blink arrancando por el mismo puerto. No se
  llama `esptool.py`: se importaría a sí mismo.
* **A mano**, en Linux: `idf.py -p rfc2217://localhost:3356 flash` y
  `monitor` (esp-idf-monitor reinicia al abrir, como con una placa). La matriz,
  en `doc/puente_serie.md` §4.5. El puerto de la DevKitM-1 es el 3356 (el 3355
  que decía este plan es el de las placas de ST); la DevKitC-02, el 3357.
* `testc3` C14: la ROM detecta los baudios del primer SYNC, contesta al segundo
  y `READ_REG` de CHIP_DETECT_MAGIC da 0x1B31506F; 115 comprobaciones.
  `testserie` no se mueve.

### E6 · C3: depuración (G1, G2, G2') y D4

**Entregas:**
1. El DM de E2 montado en el C3 con `RV_ESP32C3.dbg` (8 *triggers*, *program
   buffer* de 16 y SBA de palabra), el IDCODE del TAP del C3 y su región `DM`.
2. La selección del origen del JTAG por eFuses (y por el pin de arranque, una
   vez cerrado el dato de §2.3).
3. El `programador_nv_if` de Espressif para el `load`.
4. `ide/esp32c3/`: Espressif-IDE (`.launch`) y VS Code (`launch.json` para la
   extensión de ESP-IDF y para `cppdbg`).

**Cierre:**
* el guion de E2, por G1 y G2', contra el `hello_world` de ESP-IDF: punto de
  ruptura en `app_main` **en la Flash** (por *trigger*), uno en IRAM (por
  `ebreak`), *watchpoint* y paso;
* `idf.py gdb` contra G1 llega a `app_main`;
* **`openocd-esp32` sin modificar**, con `target/esp32c3.cfg` por
  `remote_bitbang`: lee el IDCODE, hace `program_esp` + `verify` (D4),
  `reset run` y arranca el firmware nuevo; en GDB, `info threads` enseña las
  tareas de FreeRTOS;
* el mismo OpenOCD por CMSIS-DAP en JTAG.

**Tamaño:** medio. Lo grande se hizo en E2.

**Hecho (2026-11-08), salvo lo que se dice al final.**
* **En el C3** (`soc/esp32c3/esp32c3.h`): el DM de E2 con `RV_ESP32C3.dbg`, su
  SBA en el bus, el DTM con el IDCODE 0x00005C25 y el TAP en MTMS (GPIO4), MTDI
  (GPIO5), MTCK (GPIO6) y MTDO (GPIO7). **La fuente del JTAG por eFuse** al
  soltar el reset de chip: DIS_PAD_JTAG, SOFT_DIS_JTAG, JTAG_SEL_ENABLE con el
  pin GPIO10 y DIS_USB_JTAG [TRM-C3 §7.3]; de fábrica va al USB-Serial-JTAG
  (E7), y `--jtag=pines` / `jtag="pines"` en el `<mcu>` es como quemar
  DIS_USB_JTAG. Con los pines sin JTAG, sus pull-ups se sueltan (son GPIO).
  **ndmreset** es un reset de la CPU (como `esp_restart()`), con la CPU en reset
  mientras está puesto: openocd-esp32 cuenta con eso (desactiva los perros y
  vuelve a poner ndmreset); si reseteara también los periféricos, el MWDT0 en
  flashboot volvía a armarse y saltaba en medio del flasher.
* **G1, G2' y G2 en `sim`**: `--gdb-dap` es el stub con el DM en directo;
  `--gdb` en un chip con JTAG y sin SWD, G2' (la sonda de bancada y el DTM por
  los pines); `--openocd` y `--cmsis-dap`, las sondas en JTAG.
* **D4**: `ProgramadorEsp` (`soc/esp/programador_esp.h`), el `load` de GDB
  sobre la Flash por las ventanas de la MMU (las páginas mapeadas); y
  `--flash-rapida` (tiempos de programar y borrar al 1 %), porque el flasher
  de openocd-esp32 espera con plazos de reloj de pared.
* **Un arreglo del cliente del DM**: el `c.ebreak` en la IRAM (el C3
  solo tiene SBA de palabra y la IRAM no admite medias palabras por el bus de
  datos: el cliente del DM lee, cambia y escribe la palabra, como
  openocd-esp32).
* `testc3` C15-C16: con `hello_world` de v5.5 (los símbolos, en
  `verif/fw/esp32c3/idf/simbolos-v5.5.txt`), por G1 y por G2': `monitor reset
  halt` (pc 0x4000_0000), `app_main` en la Flash por *trigger*,
  `xTaskIncrementTick` en la IRAM por `ebreak`, un *watchpoint* en
  `xTickCount`, un paso y Ctrl-C. 131 comprobaciones; el invariante pasa a
  «resto» (el socket) y el resto es el total de E5.
* **`verif/esp/openocd_c3.py`** (`make openocd-c3`; en el CI, si se puede bajar
  openocd-esp32): **openocd-esp32 v0.12.0-esp32-20260831 sin modificar**, con
  `target/esp32c3.cfg`, por remote_bitbang: IDCODE, examine (la revisión v0.4
  de los eFuse), halt, y **`program_esp` de blink con `verify` y `reset`** (el
  flasher de Espressif corre en el C3 simulado: borra 35 sectores, escribe
  140 KB y calcula el SHA) y blink arranca; lo mismo por **CMSIS-DAP en JTAG**;
  y **GDB contra G1 como `idf.py gdb`**: `monitor reset halt`, `thb app_main`,
  `continue` y `bt` hasta `vPortTaskWrapper`.
* `ide/esp32c3/`: los dos `.cfg` de OpenOCD, `launch.json` (la extensión de
  ESP-IDF con G1 y con OpenOCD, y `cppdbg`) y el `.launch` de Espressif-IDE.

**Pendiente:** GDB *a través de* openocd-esp32: al conectarse, OpenOCD ve la
protección de memoria activa y hace su `reset halt`; después, el punto de
ruptura no le llega a GDB (el chip arranca y el *trigger* no para), así que
`info threads` con las tareas de FreeRTOS por OpenOCD no está comprobado. La
región del DM en el mapa de memoria tampoco: el *program buffer* lo ejecuta el
hart por dentro.

### E7 · C3: USB-Serial-JTAG (D3 y G3)

**Entregas:**
1. `periph/esp/usb_serial_jtag.h`:
   * dispositivo compuesto detrás de `usb_dev_if`;
   * el CDC-ACM con sus FIFO y sus registros;
   * el reset y la bandera de descarga por DTR/RTS [TRM-C3 tabla 30.3-2];
   * y el **procesador de órdenes JTAG** (`CMD_CLK`, `CMD_RST`, `CMD_FLUSH`,
     `CMD_REP`) conectado al TAP.
2. `PuenteUsb`:
   * dispositivos compuestos (IAD) y elección de la interfaz CDC;
   * **una segunda clase, la del JTAG de Espressif**, sacada como servidor
     `remote_bitbang`, que traduce cada orden de OpenOCD a órdenes del
     endpoint;
   * con el `ControladorUsb` y el canal RFC 2217 de siempre.
3. `placas/esp32c3_supermini.xml`.

**Cierre:**
* con la SuperMini, `esptool --port rfc2217://localhost:3356 --before usb-reset
  write-flash` e `idf.py monitor` por el CDC: el reset a modo descarga lo hace
  el periférico, sin transistores, con la secuencia de [TRM-C3 tabla 30.4-1].
  Cómo se le pide lo mismo a `idf.py flash` se comprueba aquí;
* `openocd-esp32` por G3 (`remote_bitbang` hacia el puente) hace lo mismo que en
  E6;
* la consola de ESP-IDF sale por el USB (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`);
* el invariante de las suites que usan el puente USB (`teste4` y `testf3`) no
  se mueve.

**Tamaño:** medio.

**Hecho (2026-11-08), salvo un `write-flash` grande por el USB.**
* **`periph/esp/usb_serial_jtag.h`**: el dispositivo de función fija detrás de
  `usb_dev_if`, con sus descriptores (303A:1001, compuesto con IAD: el CDC-ACM
  en las interfaces 0 y 1 y el JTAG de Espressif, FF/FF/01, en la 2); el CDC
  con sus dos búferes de 64 bytes (EP1, WR_DONE, SERIAL_IN_EP_DATA_FREE,
  SERIAL_OUT_EP_DATA_AVAIL) y sus interrupciones (fuente 26); el pull-up de
  1,5 kΩ en D+ (USB_PAD_ENABLE, DP_PULLUP); **el reset por DTR/RTS**: (RTS,
  DTR) = (1, 0) es el reset (rst:0x15, USB_UART_CHIP_RESET, un reset de chip
  que vuelve a leer los pines de arranque), a modo descarga si se llega desde
  (1, 1); y **el procesador de órdenes JTAG** (CMD_CLK, CMD_RST, CMD_FLUSH,
  CMD_REP) sobre el TAP del chip cuando los eFuse lo eligen (de fábrica, sí).
  La disposición de los nibbles y de REP es de modelo.
* **`PuenteUsbCdc`**: encuentra el CDC en un dispositivo compuesto y la
  interfaz JTAG de Espressif, y la saca como **servidor remote_bitbang**
  (`--usb rfc2217:P,jtag=P2`): cada flanco de TCK es una CMD_CLK y cada 'R' de
  OpenOCD adelanta la CMD_CLK con captura y un FLUSH. `sondeo_in_us`: el C3
  pide los IN de bulk cada 50 µs dentro de la trama (la ROM y el stub esperan
  pocos milisegundos a que se vacíe el búfer); el E4 y el F303 siguen igual.
* La SuperMini lleva `usb="rfc2217:3356,jtag=3337"` en su `<mcu>`.
* **Comprobado** (`verif/esp/usb_c3.py`, `make usb-c3`, en el CI): la consola
  de ESP-IDF por el USB (la secundaria de `hello_world`, que viene puesta);
  openocd-esp32 por G3 (IDCODE, examine, halt); `esptool --before usb-reset`
  con el *stub* (`chip-id`) y `write-flash` de la tabla de particiones con su
  MD5, y `hello_world` vuelve a arrancar. Y `idf.py monitor` abre el puerto
  activando DTR y RTS y luego quita DTR: pasa por (1, 0) y reinicia en
  descarga, como el silicio (es el aviso conocido de los C3 con Arduino).
* `testc3` C17: la enumeración, la consola por el CDC, el IDCODE por el USB, la
  secuencia usb-reset hasta «waiting for download», el SYNC contestado por el
  USB, y que con DIS_USB_JTAG el TAP no contesta por el USB. Sin PC en el
  USB, la ROM y la consola secundaria esperan su plazo al llenarse el búfer:
  eso mueve C8-C14 (139 comprobaciones; el resto, 17846755875579 ps).

**Pendiente:** `write-flash` del bootloader entero (13 KB comprimidos) por el
USB se queda sin respuesta al FLASH_DEFL_END: el stub sigue en marcha y el
dispositivo no tiene nada pendiente; sin diagnosticar.

### E8 · C3: periféricos de aula

Por orden de uso en prácticas. Cada periférico es una entrega con su firmware
de prueba, su grupo de comprobaciones en `testc3` y, si hay uno que encaje, un
ejemplo de ESP-IDF sin tocar contra una pieza que ya existe (los nombres de la
tabla son los de `examples/peripherals`, a confirmar con la versión que se use):

| # | Periférico | Pieza o ejemplo |
| :--- | :--- | :--- |
| 1 | LEDC | LED con brillo, el servo SG90; `ledc_basic` |
| 2 | RMT + la pieza `Ws2812` | El LED RGB de la DevKitM-1; `led_strip` |
| 3 | GP-SPI2 + GDMA | TFT ST7735; `spi_master` |
| 4 | I2C | EEPROM 24Cxx; `i2c_eeprom` |
| 5 | SAR ADC y sensor de temperatura | Potenciómetro sobre los nodos analógicos; `oneshot_read`, `temp_sensor` |
| 6 | TWAI | El bus CAN del proyecto (T-24); `twai_network` |
| 7 | SHA, AES y RNG | Vectores de `verif/vectores/` |
| 8 | Declarados con aviso | I2S, RSA, HMAC, DS, XTS_AES, UHCI |

**Cierre:** el de cada entrega. El invariante de `testc3` se fija en cada una,
diciendo por qué se ha movido.

### E9 · ESP32-C6: el núcleo HP y el SoC

**Objetivo:** el segundo chip, que reutiliza el núcleo, la capa de Espressif y
todos los caminos de descarga y depuración. Esta fase es la que demuestra que
la separación de §3 funciona.

**Entregas:**
1. `RV_ESP32C6_HP`:
   * A;
   * U con delegación (`mideleg` y los CSR de usuario: la parte «N» del hart,
     que ya existe desde E1 tras `deleg_n`);
   * `mie`/`mip`;
   * PMA por CSR propios;
   * 4 *triggers*;
   * y la tabla de tiempos con el BTB.
2. `Clint` en la variante Espressif (`0x2000_1800`) e `IntcEspV2` (INTPRI) con
   su combinación con el CLINT [TRM-C6 §1.6 y §1.7]; la matriz de
   interrupciones del C6.
3. `soc/esp32c6/`: descriptores (C6 en QFN40 y C6FH4/FH8 en QFN32), mapa
   [TRM-C6 tabla 5.3-2], bus sin alias, PCR, LP_AON, LP_CLKRST, la parte de la
   PMU que el arranque necesita, `I2C_ANA_MST`, APM y TEE (modelados o
   declarados), caché de 32 KB con su ventana única, y los cinco pines de
   arranque con GPIO15 para el origen del JTAG.
4. Los periféricos comunes de E3-E8 montados con los `Caps` del C6, y TWAI1.
5. `placas/esp32c6_devkitc1.xml` con sus dos USB.
6. `verif/fw/esp32c6/` (*bare-metal* e IDF), `top/sc_main_c6.cpp` →
   `make testc6`, `asanc6`.

**Cierre:**
* `riscv-arch-test` en el laboratorio con `RV_ESP32C6_HP` (U incluido);
* *bare-metal*: el *blinky*, la UART, el CLINT y el INTPRI (umbral, prioridad
  0, delegación de una interrupción a U y `uret`);
* ESP-IDF: `hello_world` en Direct Boot y en arranque normal;
* D2 y D3 con esptool;
* G1, G2 (`target/esp32c6.cfg`) y G3, como en E6 y E7;
* `testc6` y `asanc6` en el CI con su invariante.

**Tamaño:** grande, pero claramente menor que E3 + E4.

### E10 · C6: el núcleo LP

**Entregas:**
1. `RV_ESP32C6_LP`:
   * 2 etapas a 20 MHz;
   * solo M;
   * una interrupción en el ID 30;
   * contadores estándar;
   * 2 *triggers*;
   * y reset en `0x5000_0080`.

   Como **segunda instancia de `NucleoRv`** en el mismo SoC.
2. LP SRAM con sus dos modos de acceso y la latencia cruzada; LPPERI
   (`LPPERI_CPU_REG`, `LPPERI_INTERRUPT_SOURCE_REG`), RTC_TIMER, LP_UART, LP_I2C
   y LP IO MUX.
3. El DM con **dos harts**: el LP «unavail» hasta que se escribe
   `LPPERI_CPU_REG`, y el disparo cruzado HP↔LP.
4. Las atómicas entre los dos núcleos sobre la LP SRAM y, si se reproduce, la
   errata CPU-206 de v0.0-v0.1, que se puede apagar.

**Cierre:**
* los ejemplos de LP Core de ESP-IDF (los de `examples/system/ulp/lp_core`)
  sin tocar: el LP cuenta, despierta al HP y le pasa datos por la LP SRAM;
* GDB por G1 enseña **dos hilos**, uno por hart; `openocd-esp32` depura el LP;
* parar el HP con el disparo cruzado activado para el LP, y desactivado no lo
  para.

**Tamaño:** medio.

### E11 · C6: periféricos propios

| # | Periférico | Para qué |
| :--- | :--- | :--- |
| 1 | MCPWM | Los motores de CC y BLDC del catálogo (rama `cnc`) |
| 2 | PCNT | El encoder KY-040 |
| 3 | ETM | Sucesos entre periféricos sin CPU |
| 4 | PARL_IO | Declarado al principio |
| 5 | ECC | Declarado |
| 6 | Esclavo SDIO | Declarado: la pieza que haría de anfitrión no existe |
| 7 | Codificador de traza | Declarado |
| 8 | Monitor de memoria | Declarado |

### E12 · Radio paravirtual (opcional)

Es la R8 de `doc/analisis_wifi_bl.md` §3.3. Consiste en un periférico `VirtWifi`
inventado y documentado por nosotros, en una dirección libre del mapa, más un
componente de ESP-IDF que lo usa por debajo de `esp_wifi`. Depende del
`MedioRadio` y del `PuntoAcceso` (R1-R3) y del puente Ethernet. **No se empieza
sin que R1-R3 estén hechas.** Mientras tanto, la radio sigue declarada.

### E13 · `mcu-sim-gui`

Los dibujos de las placas de §9. El protocolo no cambia: nombra los nodos con
cadenas, y `GPIO9` vale tanto como `PA5`. Hay que comprobar que la ventana no da
por hechos nombres de la forma `Pxn`.

---

## 11. Riesgos y preguntas abiertas

1. **E0.2, el corte del motor de GDB.** Es lo único del plan que toca código
   que ya funciona en las ocho suites. Mitigación: dos commits (mover y luego
   cortar) y comparar la traza de T96, T97, P21 y O1-O5 byte a byte.
2. **Los registros sin documentar del arranque (E4).** Son el gran desconocido.
   Mitigación: Direct Boot primero, ESP-IDF sin radio y cada registro anotado.
   El QEMU de Espressif ayuda a saber qué se toca, no a saber qué valor tiene
   que tener.
3. **La microarquitectura no está publicada.** Los TRM dicen «4 etapas», «BTB
   con predicción estática» y «latencia de 4 ciclos» en el controlador, y poco
   más. Los ciclos por instrucción irán como parámetros sin fuente
   (sección D de `todo.md`), calibrados contra el CoreMark/MHz que publica
   Espressif.
4. **Dos revisiones de ROM en el C3.** Si no se fija bien la pareja revisión-ROM,
   los fallos parecerán del modelo. El descriptor fija las dos, y `sim` avisa si
   no casan.
5. **El origen del JTAG en el C3.** Su TRM no da el pin de arranque. Es un dato
   para la sección D hasta contrastarlo con la guía de JTAG de ESP-IDF.
6. **Que OpenOCD de Espressif traiga `remote_bitbang`.** El controlador está en
   OpenOCD desde hace años, pero hay que comprobar que los binarios de
   `openocd-esp32` lo traen compilado. Si no, hay que compilarlo, como ya se
   hizo con la versión de git que pide el SWD (T5).
7. **El tiempo de esptool y de OpenOCD contra el reloj de pared.** La sonda
   `remote_bitbang` ya resuelve el de OpenOCD. El de esptool se resuelve en E5
   (§6).
8. **Coste de simulación.** Dos núcleos en el C6, una caché con fallos y un USB
   con SOF cada milisegundo. Hay que medirlo en E9 y E10, como
   `doc/coste_simulacion.md` hizo con el F407.
9. **Lo que no está y hay que decir desde el primer día:**
   * la radio (salvo E12);
   * el cifrado de Flash y el arranque seguro (eFuses de seguridad
     declarados);
   * el Deep-sleep con su dominio de potencia, que se hace solo en lo que pida
     el LP;
   * y el SDIO esclavo.

---

## 12. Fuentes

* Repositorio `mcu-sim`, rama `esp32rv` (`54a5792`): `soc/mcu_if.h`,
  `soc/mcu_desc.h`, `pins/nomenclatura.h`, `pins/encapsulado.h`,
  `common/gdb_rsp.h`, `common/imagen_fw.h`, `common/memoria_nv.h`,
  `core/cortex_m4f.h`, `core/debug_if.h`, `verif/sonda_openocd.h`,
  `verif/sonda_cmsis_dap.h`, `verif/openocd/programa.py`,
  `parts/puente_usb.h`, `soc/msp432p4/rom_p4.h`, `top/sim_main.cpp`,
  `parts/netlist.h`, `verif/invariantes.txt`, `doc/todo.md`,
  `doc/analisis_msp432.md` y `doc/analisis_wifi_bl.md`.
* `doc/refs/esp32-c3/` y `doc/refs/esp32-c6/` (§0, tabla de referencias).
* Espressif, `esp-rom-elfs`, README: licencia Apache-2.0 y nombres de fichero.
  Consultado el 07-11-2026.
* Espressif, documentación de esptool: «Remote Serial Ports» y «Boot Mode
  Selection». Consultada el 14-10-2026.
* Espressif, ESP-IDF Programming Guide: «JTAG Debugging — Tips and Quirks» y
  «QEMU Emulator». Consultadas el 14-10-2026.
* CNX Software, «ESP-IDF v6.0 framework adds support for ESP32-C5 and ESP32-C61»
  (24-03-2026).
* Pendientes de incorporar a `doc/refs/`: RISC-V ISA Manual (no privilegiado y
  privilegiado), RISC-V External Debug Support 0.13.2, y los esquemáticos de la
  ESP32-C3-DevKitM-1 y la ESP32-C6-DevKitC-1.
