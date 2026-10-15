# Licencia de la copia de DriverLib y de las cabeceras del SDK de TI

Lo que hay debajo de `ti/devices/msp432p4xx/` es de Texas Instruments, tal
cual se distribuye con su SDK (SimpleLink MSP432P4 SDK, la copia que lleva el
núcleo `msp432r-core` de Energia, https://github.com/energia/msp432r-core,
revisión 0faf18b): solo los módulos de DriverLib que usa
`verif/fw/msp432p4/driverlib_t3` (adc14, cpu, cs, dma, flash, gpio,
interrupt, pcm, sysctl, timer_a, uart, wdt_a, con sus cabeceras y las que
estas incluyen) y las cabeceras de dispositivo que piden (`inc/`). Ningún
fichero se ha modificado. Se usan con esta licencia (BSD de tres cláusulas),
que es la que llevan en su cabecera:

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
