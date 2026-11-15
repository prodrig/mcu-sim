# riscv-arch-test — licencia

`act.tar.xz` lleva 220 programas de prueba **compilados** de
[riscv-arch-test](https://github.com/riscv/riscv-arch-test), rama `act4`
(commit `bad8b12`, del 07-11-2026), para la configuración
`verif/rv/act/mcusim-lab-rv32imac`: las pruebas generadas por su `testgen`,
con los resultados de referencia de Sail 0.16 dentro, compiladas con
riscv-none-elf-gcc 15.2.0 (xPack 15.2.0-1). Se regeneran con
`make -C verif/rv act ACT=<riscv-arch-test>`.

El código de riscv-arch-test va con la licencia BSD de tres cláusulas
(SPDX `BSD-3-Clause`) o con la Apache 2.0 (SPDX `Apache-2.0`); el entorno de
las pruebas (`tests/env`) y las pruebas generadas, con la BSD. Su texto, tal
como está en `COPYING.BSD`:

```
Copyright (c) <year> <owner>. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
