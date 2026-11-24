# `biblioteca/`: lo que `mcu-sim` sabe construir, como dato

Es la biblioteca de **`mcu-sim-build`**, el programa de `mcu-sim-gui` que dibuja
placas y sistemas en lugar de escribirlos (`doc/analisis_build.md`). Aquí no
hay nada que `mcu-sim` necesite para simular: es lo que `mcu-sim` **exporta**
para que otro programa sepa qué piezas hay y cómo se conectan.

| Carpeta | Qué hay | Fase | Cómo se hace |
| :--- | :--- | :--- | :--- |
| `piezas/` | `TIPO.pieza.xml`: la ficha de cada tipo de componente —la misma que imprime `mcu-sim --help TIPO`— con sus terminales, sus atributos y su omisión | B1 | **Generado**: `make biblioteca` |
| `mcus/` | El patillaje de cada MCU y sus dos dibujos | B3, B4 | *(pendiente)* |
| `conectores/` | Los perfiles de conector (morpho, Arduino, BoosterPack) y sus conjuntos | B6 | *(pendiente)* |

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

**Lo que no está aquí**: dónde va cada patilla en el dibujo de la pieza. Eso
lo añade la fase B5, en ficheros aparte (`TIPO.forma.xml` y `TIPO.svg`) que
**no** se generan, para que regenerar las fichas no los pise.
