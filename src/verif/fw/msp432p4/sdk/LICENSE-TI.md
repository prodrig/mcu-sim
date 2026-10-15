# Licencia de la copia de DriverLib y de las cabeceras del SDK de TI

Lo que hay debajo de `ti/devices/msp432p4xx/` es de Texas Instruments, tal
cual se distribuye con su SDK (SimpleLink MSP432P4 SDK, la copia que lleva el
núcleo `msp432r-core` de Energia, https://github.com/energia/msp432r-core,
revisión 0faf18b): **DriverLib entera** desde T4 —todos sus módulos, con sus
cabeceras— y las cabeceras de dispositivo que piden (`inc/`). Ningún fichero
se ha modificado. La usan `verif/fw/msp432p4/driverlib_t3`, los ejemplos de
`verif/fw/msp432p4/sdk_ejemplos`, la demo de FreeRTOS y la ROM sustituta,
`verif/fw/msp432p4/rom_r1`, que el simulador lleva dentro. Se usan con esta
licencia (BSD de tres cláusulas), que es la que llevan en su cabecera:

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

## Una excepción que conviene corregir: `rom.h` y `rom_map.h`

En la copia de Energia, estas dos cabeceras —las que dicen qué función de la
ROM ocupa cada casilla de `ROM_APITABLE`, y las macros `MAP_`— no llevan la
cabecera BSD, sino la nota antigua de las primeras versiones de DriverLib:

```
// Copyright (c) 2013 Texas Instruments Incorporated.  All rights reserved.
// TI Information - Selective Disclosure
```

TI publica **el mismo fichero con la licencia BSD de arriba** en el SimpleLink
MSP432P4 SDK 3.40.00.05 (`source/ti/devices/msp432p4xx/driverlib/rom.h`; su
fuente, en la documentación del SDK:
https://software-dl.ti.com/simplelink/esd/simplelink_msp432_sdk/3.40.00.05/docs/driverlib/msp432p4xx/html/rom_8h_source.html).
Lo que se ha podido comprobar desde aquí: los índices de las muestras
consultadas coinciden (`ROM_GPIOTABLE` = 9, `ROM_WDTTABLE` = 25,
`ROM_CS_setDCOFrequency` = `ROM_CSTABLE[12]`, `ROM_GPIO_toggleOutputOnPin` =
`ROM_GPIOTABLE[3]`), y las 424 casillas de esta copia llevan a la función del
mismo nombre en la ROM de TI (`rom_r1/contrasta_ti.py`).

**Lo correcto es sustituir estas dos por las del SDK 3.40**, que no está en un
repositorio público y hay que bajarlo de ti.com. Es un cambio sin más: el
Makefile de `rom_r1` vuelve a generar las tablas desde `rom.h` y a
verificarlas, y la huella de `rom_r1.bin` dirá si algo ha cambiado.

