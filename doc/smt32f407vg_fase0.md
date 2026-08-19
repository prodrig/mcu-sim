He escrito el plan completo (te lo acabo de enviar; lo guardaré en doc/ junto al esqueleto cuando apruebes). Resumen de las propuestas de cambio sobre tu jerarquía:

P1 — Errores de bus a corregir: GPIOA-E están en AHB1, no en APB1; ADC es APB2; OTG_FS, RNG y DCMI son AHB2; OTG_HS y CRC son AHB1; USART2 aparecía duplicado en APB2; eth_dma/usb_dma se absorben como puertos maestros de eth_mac/otg_hs; bxCAN se separa en CAN1+CAN2.

P2 — Faltan bloques: SYSCFG (imprescindible: boot remap y ruteo EXTI), EXTI como periférico APB2 (no dentro del SCB), RTC como periférico APB1 (no dentro de RCC — RCC solo conserva el mux RTCCLK), SDIO, FSMC, ETH_MAC, flash_if (registros FLASH) y GPIOF-I.

P3 — Núcleo: ART se mueve al controlador de Flash (así es en el silicio, lo gobierna FLASH_ACR); los registros Rx/xPSR/PRIMASK/... pasan de submódulos a struct RegFile dentro de la CPU (no son bloques con interfaz de bus); AHB_AP se integra dentro del DAP; CCM se conecta directa al D-bus, sin pasar por la matriz.

P4 — RCC: PH0/PH1 (y PC14/PC15 del LSE, que faltaban) no son hijos de RCC sino pads conectados a HSE/LSE vía pin_mux; se añaden clock_tree, reset_ctrl y CSS→NMI.

P5-P8: modelo eléctrico de pines con canal AnalogNet (resolución Thevenin de V/I en float, Schmitt con histéresis, detección de fuera de rango, NRST open-drain, BOOT0 muestreado); relojes con ClockGen reprogramable (sc_clock no permite cambiar frecuencia); matriz TLM 8×7 con la tabla de conectividad del cap. 6; vectores irq[82], líneas EXTI y pares req/ack de DMA según los informes.

Se crearon 44 archivos, 12 archivos editados, ejecutó 9 comandos · 5 notas

Hecho. Con las propuestas P1–P8 aprobadas, tienes en tu repo:

doc/smt32f407vg_diseño.md — el plan completo: análisis de tu jerarquía con las correcciones, jerarquía revisada, matriz de interconexiones (TLM, relojes, resets, IRQ, DMA, pines), decisiones (bus TLM-2.0 LT→AT, la opción recomendada) y las 8 fases de implementación.

src/ — el esqueleto SystemC completo: 43 ficheros, 62 módulos instanciados, compilado y verificado con SystemC 2.3.4 (make -f Makefile.stm32 smoke elabora el netlist entero y simula el power-up — renombra Makefile.stm32 a Makefile si prefieres; el puente remoto no permite escribir ficheros con ese nombre exacto). Los puntos clave del esqueleto:

Frontera eléctrica como pediste: cada pin es un canal AnalogNet donde cada conectado (pad del MCU, testbench, DAC, cristales) publica un equivalente Thevenin {V, Rout} en float; el canal resuelve tensión y corriente del nodo, y el Pad digitaliza con Schmitt (VIH/VIL con histéresis), señala nivel indeterminado (X) y fuera de rango, y modela push-pull/open-drain/pull-up/down/analógico según MODER/OTYPER/PUPDR. El testbench de humo ya alimenta VDD y suelta NRST por esta vía.
Relojes digitales con ClockGen reprogramable en caliente (los osciladores y salidas de PLL/prescaler son instancias; sc_clock no vale porque su periodo es fijo).
El top es el contrato de integración: matriz 8×7 con la máscara de conectividad real, CCM colgada del D-bus (no de la matriz), decodificadores AHB1/AHB2 + 2 puentes APB con todos los mapas de direcciones, las 82 IRQs numeradas una a una (con puertas OR para las compartidas TIM1/TIM9-11, TIM8/TIM12-14 y TIM6/DAC), las 128 celdas de petición DMA según las tablas corregidas del informe, líneas EXTI, freeze de DBGMCU, y rutas analógicas de ADC/DAC/USB-FS/cristales ya registradas.
Cada módulo dummy lleva sus TODO(fase N) con la referencia exacta del informe que hay que implementar.

Dos avisos de la compilación que te afectan en local: la biblioteca SystemC empaquetada en Ubuntu está construida con C++17 (el estándar debe coincidir o el enlazado falla con el símbolo sc_api_version...), y el segundo puerto del decodificador APB1 (para el puerto de periféricos de DMA1, que en el silicio no pasa por la matriz) usa simple_target_socket_optional, disponible desde SystemC 2.3.3.