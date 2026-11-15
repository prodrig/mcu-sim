# riscv-arch-test (ACT4) para el laboratorio RISC-V

La configuración del DUT con la que
[riscv-arch-test](https://github.com/riscv/riscv-arch-test) (rama `act4`)
genera las pruebas del núcleo de `src/rv` en el SoC de laboratorio
(`SocLabRv` con `RV_LAB_IMAC`). Lo que sale son ELF que se comprueban solos:
Sail 0.16 calcula los resultados al compilarlos. Están versionados en
`../elf/act.tar.xz` y los pasa `testrv` (V10).

| Fichero | Qué es |
| :--- | :--- |
| `mcusim-lab-rv32imac/test_config.yaml` | La entrada de ACT4: compilador, Sail, UDB, enlazador |
| `mcusim-lab-rv32imac/mcusim-lab-rv32imac.yaml` | Extensiones y parámetros, en el formato de UDB |
| `mcusim-lab-rv32imac/sail.json` | Lo mismo para Sail. Tiene que coincidir con lo anterior y con `rv_hart.h` |
| `mcusim-lab-rv32imac/rvmodel_macros.h` | HTIF, la UART de pega, el CLINT y la interrupción externa |
| `mcusim-lab-rv32imac/link.ld` | 4 MB de RAM en `0x8000_0000` |
| `udb_sin_ruby.py`, `bin/bundle` | Para generar sin las gemas de Ruby de UDB (sin rubygems.org) |

Regenerar (desde `src/verif/rv`), con `sail_riscv_sim` 0.16 y
`riscv-none-elf-gcc` 15 en el `PATH`, y `uv sync` hecho en riscv-arch-test:

```
make act ACT=~/riscv-arch-test             # con Ruby y rubygems.org
make act ACT=~/riscv-arch-test SIN_RUBY=1  # sin ellos
```

Un ELF suelto, con su salida y, si se pide, la traza:

```
RV_TRAZA_ELF=traza.txt build/testrv --elf build/act/rv32i/I/I-add-00.elf
```

La configuración sale de la de Hazard3 de riscv-arch-test (Apache-2.0, ver
`LICENSE-APACHE-2.0.txt`); `rvmodel_macros.h`, de la de Spike (BSD-3-Clause).
