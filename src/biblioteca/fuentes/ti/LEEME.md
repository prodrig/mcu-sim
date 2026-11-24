# `fuentes/ti/`: el patillaje de los MSP430 y los MSP432, de sus datasheets

Un `TIPO.pines` por MCU de TI (fase B4b de `doc/analisis_build.md`), con la
**transcripción** de dos tablas de su datasheet:

* la de patillas («Pin Attributes», «Terminal Functions» o «Signal
  Descriptions»): la posición de cada patilla en el encapsulado que usa
  mcu-sim, y su nombre;
* las de funciones («Port Px Pin Functions»): por pad, cada función y el
  **número que la elige**, el mismo que usa el modelo:

  | Familia | Número |
  | :--- | :--- |
  | MSP430G2 | PxSEL + 2·PxSEL2 |
  | MSP430F5 | PxSEL (y el PMAP de P4) |
  | MSP430FR2/FR4/FR5/FR6, MSP432P4 | 2·PxSEL1 + PxSEL0 |
  | MSP432E4 | PCTL, de «GPIO Pins and Alternate Functions» |

  Una X en un bit de PxSEL es «cualquiera de los dos», así que una función
  puede elegirse con dos números. Lo que no se elige con PxSEL (lo analógico
  del G2, el JTAG, el BSL) va con `-`.
* y la del PMAP («Port Mapping Mnemonics and Functions», F5529 y P401R): el
  valor de cada mnemónico.

| Fichero | Datasheet | Encapsulado |
| :--- | :--- | :--- |
| `MSP430G2553.pines` | SLAS735J | PDIP20 (N) |
| `MSP430G2553IPW28.pines` | SLAS735J | TSSOP28 (PW) |
| `MSP430F5529.pines` | SLAS590P | LQFP80 (PN) |
| `MSP430FR2311.pines` | SLASE58E | TSSOP20 (PW) |
| `MSP430FR2355.pines` | SLASEC4D | LQFP48 (PT) |
| `MSP430FR2433.pines` | SLASE59F | VQFN24 (RGE) |
| `MSP430FR2476.pines` | SLASEO7C | LQFP48 (PT) |
| `MSP430FR4133.pines` | SLAS865F | LQFP64 (PM) |
| `MSP430FR5969.pines` | SLAS704G | VQFN48 (RGZ) |
| `MSP430FR5994.pines` | SLASE54D | LQFP80 (PN) |
| `MSP430FR6989.pines` | SLAS789D | LQFP100 (PZ) |
| `MSP432P401R.pines` | SLAS826E | LQFP100 (PZ) |
| `MSP432E401Y.pines` | SLASEN5 | TQFP128 (PDT) |

**No se editan a mano.** Los escribe `herramientas/extrae_ti.py` a partir de
los PDF, que están en `doc/refs/` y no se versionan:

    make biblioteca-fuentes REFS=../doc/refs

Lo que el PDF no deja leer bien (una fila perdida al final de una página, una
errata entre las dos tablas, una descripción en vez de un nombre) se corrige
en `extrae_ti.py` (`PATILLAS_PERDIDAS`, `ERRATAS`, `DESCRIPCIONES`), citando
la tabla. Nunca en el `.pines`.

¿SysConfig? Su `mode` es un índice interno suyo, no el PxSEL del chip, y
mezcla funciones de software. Las transcripciones salen del datasheet, que
describe el hardware tal cual.
