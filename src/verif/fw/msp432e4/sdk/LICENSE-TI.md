# Licencia de la copia de DriverLib y de las cabeceras del SDK del MSP432E4

Lo que hay debajo de `ti/devices/msp432e4/` es de Texas Instruments, tal cual
se distribuye con su SDK (SimpleLink MSP432E4 SDK, la copia que lleva el
núcleo `msp432e-core` de Energia, https://github.com/energia/msp432e-core,
revisión be5c1c2): **DriverLib entera** (`driverlib/`, sus fuentes y sus
`hw_*.h`), las cabeceras de dispositivo (`inc/`), el arranque y el
`system_msp432e401y.c` para GCC (`startup_system_files/`) y el guion de
enlazado (`linker_files/`). Ningún fichero se ha modificado. La usan los
firmwares de `verif/fw/msp432e4/`. Todos los ficheros llevan en su cabecera
esta licencia (BSD de tres cláusulas), también `rom.h` y `rom_map.h`, al
contrario que en la copia del P4:

```
Copyright (c) 2017, Texas Instruments Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

*  Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

*  Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

*  Neither the name of Texas Instruments Incorporated nor the names of
   its contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Lo que NO se ha copiado

El cargador de arranque de **TivaWare** (`boot_loader/bl_*.c`) y sus ejemplos
son del mismo silicio, pero llevan otra licencia: «You may not combine this
software with "viral" open-source software in order to form a larger
program». mcu-sim es AGPLv3, así que ni se copian ni se compilan aquí. El
cargador de la ROM sustituta del E4 es propio (T6, entrega 4).
