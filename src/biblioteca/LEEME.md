# `biblioteca/`: lo que `mcu-sim` sabe construir, como dato

Es la biblioteca de **`mcu-sim-build`**, el programa de `mcu-sim-gui` que dibuja
placas y sistemas en lugar de escribirlos (`doc/analisis_build.md`). Aquí no
hay nada que `mcu-sim` necesite para simular: es lo que `mcu-sim` **exporta**
para que otro programa sepa qué piezas hay y cómo se conectan.

| Carpeta | Qué hay | Fase | Cómo se hace |
| :--- | :--- | :--- | :--- |
| `piezas/` | `TIPO.pieza.xml`: la ficha de cada tipo de componente —la misma que imprime `mcu-sim --help TIPO`— con sus terminales, sus atributos y su omisión; `TIPO.svg`, su dibujo, y `TIPO.forma.xml`, dónde está cada terminal en él | B1, B5 | **Generado**: `make biblioteca` |
| `mcus/` | `TIPO.mcu.xml` (el patillaje y lo que hace cada patilla, contrastado con lo que simula el modelo), `TIPO.detalle.svg` y `TIPO.simple.svg` | B3 (los 44 STM32), B4 (los 13 de TI y los 8 ESP32) | **Generado**: `make biblioteca-mcus` |
| `conectores/` | Los perfiles de conector (Arduino, ST morpho, BoosterPack y las tiras propias de algunas placas), sus conjuntos y el cableado de cada placa del repositorio | B6 | **Generado**: `make biblioteca-conectores` |

## `mcus/`

**No se edita a mano.** `biblioteca/herramientas/genera_mcus.py` hace tres cosas:
* toma lo que el modelo simula (`mcu-sim --exporta-mcu`, de
  `make exporta-mcu-comprueba`);
* busca la fuente de cada MCU:
  * ST: Open Pin Data, en `fuentes/st/`;
  * TI: la transcripción de su datasheet, en `fuentes/ti/`;
  * Espressif: la de su datasheet y la matriz de ESP-IDF, en `fuentes/esp/`;
* las **contrasta** y escribe los tres ficheros.

Si el modelo simula una función que la fuente no tiene en ese pad con ese
número, el contraste falla y ese MCU no se publica. En TI y Espressif el
contraste también exige el mismo NOMBRE, y comprueba el PMAP y la matriz
GPIO. Así salieron errores del modelo en B3 y en B4b; están en
`doc/analisis_build.md`.

En cada patilla, `simulada="si"` dice que el modelo la simula. `por="periferico"`
dice que eso se decide por el periférico, porque no se elige con un número
(una entrada del ADC, un oscilador). El `<mapeo>` final lista lo que se
asigna a cualquier pin: los mnemónicos del PMAP (F5529, P401R) con su valor,
o las señales de la matriz de un ESP32 con su índice y, si se sabe, su
`sentido`.

Los dos SVG están a escala, en unidades de 0,1 mm. Cada patilla es
`id="$.N"`, con N su posición (`21`, o `A1` en una rejilla de bolas).

## `piezas/`

**No se edita a mano.** La fuente es la ficha de cada pieza, que viaja con su
registro en la factoría (`parts/part_help.h`), y `mcu-sim --exporta-piezas DIR`
la vuelca tal cual (`parts/exporta_piezas.h`, donde está el formato). Lo
generado se versiona por dos razones:

* `mcu-sim-build` funciona con un `mcu-sim` sin compilar;
* un cambio en una ficha se ve en el `diff`.

```
make biblioteca             # regenera piezas/
make biblioteca-comprueba   # ¿está al día? y ¿es XML bien formado?
```

Si `biblioteca-comprueba` dice que no está al día, es que alguien cambió una
ficha y no regeneró: `make biblioteca` y se versiona el resultado. Si un tipo
desaparece de la factoría, su fichero se queda: `biblioteca-comprueba` lo
señala y se borra a mano.

**Los terminales** son lo único que no es copiar. La ficha los escribe para una
persona —`in1 .. in4`, `anodo o catodo`, `d0 ... dN`— y el volcado los
convierte en `<terminal>` y `<serie>`. Toda fila de toda pieza tiene que
entenderse, y lo comprueba **T133** en `make test407`. Una ficha nueva con
una forma que no se entiende hace fallar la suite.

**El dibujo y la forma** (B5) van en ficheros aparte, `TIPO.svg` y
`TIPO.forma.xml`, para que regenerar las fichas no los toque. Los escribe
`herramientas/dibuja_piezas.py` (`make biblioteca-piezas`) a partir de un
puñado de plantillas: un módulo con su tira de pines, un integrado, un
componente de patas, un motor, un TO-220... Cada tipo dice en `PIEZAS` cuál
usa. Son dibujos propios, a escala, en unidades de 0,1 mm. La pieza es
`<g id="$">` y cada terminal `$.terminal`; al componer una placa,
`mcu-sim-build` cambia `$` por el id de la pieza (`LD2`, `LD2.anodo`).

`TIPO.forma.xml` dice, por cada terminal, su elemento en el SVG, su
posición y hacia dónde sale el hilo (`<ancla terminal elemento lado x y>`).
Cuatro piezas son **paramétricas**, porque su dibujo depende de sus
atributos: `Conector` y `Jumper` (generador `conector`), `I2cWire` (`bus`) y
`CameraSensor` (`modulo`). No tienen SVG, y las dibuja `mcu-sim-build`. Las
piezas sin terminales (`Postura`, `Cotas`...) tienen dibujo, pero no anclas.

`herramientas/comprueba_piezas.py`, en `make biblioteca-comprueba`, exige
tres cosas. Las anclas son exactamente los terminales de la ficha. Cada una
está en su SVG. Y el SVG cumple el perfil de autor de `mcu-sim-gui`: SVG 1.2
Tiny, `viewBox` y tamaño en mm, sin filtros, máscaras, scripts, estilos ni
imágenes.

## `conectores/`

**No se edita a mano.** `herramientas/genera_conectores.py`
(`make biblioteca-conectores`) escribe tres clases de ficheros:

* `PERFIL.conector.xml` y `PERFIL.svg`: una tira con nombre y el **uso** de
  cada pin. Las de norma salen de UM1724 (Arduino UNO R3 y ST morpho de las
  Nucleo-64, tablas 19 y 29) y de SLAA542 (BoosterPack de 20 y 40 pines).
  En las BoosterPack, `estandar` es el número de la norma: cada tira se
  numera de 1 a 10 en las placas, y J2.1 es el 11. Las tiras propias de una
  placa (las P1/P2 de la STM32F3DISCOVERY, las de las DevKit de los ESP32)
  no tienen norma, y su uso es el nodo que les da el XML de la placa;
* `CONJUNTO.conjunto.xml`: varias tiras con su posición relativa. La
  geometría sale del SVG de una placa de referencia: el pin 1 de cada tira,
  el paso a la columna siguiente y a la otra fila. Lleva también un
  `<cableado>` por cada placa del repositorio que tiene esas tiras, sacado
  de su XML: qué nodo va a cada pin;
* `CONJUNTO.svg` y `CONJUNTO.PLACA.svg`: el conjunto despiezado, con el uso
  de cada pin (y su nodo en esa placa) al lado.

Antes de escribir nada, **contrasta**. Cada tira de cada placa tiene la
forma de su perfil. Un pin de masa del perfil va a una masa, uno de
alimentación a una alimentación y uno de reset a un reset. Uno NC no se
usa. Y una E/S de una placa con MCU va a un pad de ese MCU (de `mcus/`). La
geometría reconstruida cae en cada pin del SVG de referencia, a menos de
0,2 mm. Si algo no cuadra, acaba mal y no escribe nada. Así salió que la J2
de la MSP-EXP430FR5969 estaba numerada al revés (`doc/analisis_build.md`,
B6).
