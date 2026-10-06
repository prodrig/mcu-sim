# Ilustraciones SVG de las placas: lo que cambia en `mcu-sim`

> **Estado (2026-10-06): HECHO** lo de este repositorio, con lo decidido: se
> busca el SVG por el nombre del XML; tamaño máximo de 2 MiB; **B2 y LD3 no
> se han añadido** a la Nucleo —su botón RESET y su LED PWR quedan como
> decorado del dibujo—. Lo que hay está en `doc/parts.md` §2.6, y lo comprueba
> el grupo C10 de `make gui-sistema`.

*Análisis del 2026-10-06. El análisis completo —cómo se pinta, cómo se
localizan las piezas en el dibujo, los efectos, los mandos, varias placas— está
en `mcu-sim-gui`, `doc/analisis-uso-ilustraciones.md`. Aquí solo lo que toca a
este repositorio.*

## 1. Lo que se quiere, en una línea

Que `mcu-sim-gui` enseñe cada placa con un dibujo SVG aproximado —una
NUCLEO-F446RE dibujada a mano, por ejemplo— y que sobre él se vean los
observables (el LD2 que se enciende) y se accionen los mandos (pulsar B1 con el
ratón sobre el dibujo).

## 2. Por qué `mcu-sim` se ve afectado

**El modelo, no.** La ilustración es pantalla: no cambia ni una pieza ni un
proceso, y **ningún invariante se mueve**. `mcu-sim` sigue sin saber leer SVG y
sin depender de Qt, que es lo que permite compilarlo en cualquier máquina.

**Pero el dibujo es de la placa, y la placa vive aquí.** Tres cosas solo las
puede hacer `mcu-sim`:

1. **decir qué dibujo es el de cada placa**, porque es quien lee su XML;
2. **encontrar el fichero**, porque las rutas son relativas a la placa y la
   ventana no sabe dónde está la placa en el disco de `mcu-sim`;
3. **mandárselo a la ventana**, que puede estar en otra máquina
   (`--gui host:puerto`) y no ver estos ficheros.

## 3. Cómo se declara el dibujo

**En la placa**, un atributo:

```xml
<placa nombre="nucleo-f446re" ilustracion="nucleo_f446re.svg">
```

con la ruta relativa al fichero de la placa. **Sin él**, se busca un SVG con el
mismo nombre que el XML (`nucleo_f446re.xml` → `nucleo_f446re.svg`), en la
misma carpeta: así una placa con su dibujo al lado no tiene que decir nada.

**En un `<sistema>`**, el montaje puede cambiarlo para una placa concreta:

```xml
<placa id="N" fichero="nucleo_f446re.xml" ilustracion="nucleo_vista_trasera.svg"/>
```

**Y, opcionalmente, la tabla de enlaces** para dibujos que no se quieren tocar
(qué elemento del SVG es cada pieza; el porqué, en el análisis de la ventana,
§5):

```xml
<placa nombre="nucleo-f446re" ilustracion="nucleo_f446re.svg">
  <ilustracion>
    <enlace pieza="LD2" elemento="led-verde" efecto="brillo"/>
    <enlace pieza="B1"  elemento="btn-user-tapa"/>
  </ilustracion>
```

**Lo que hay que cambiar en el lector** (`parts/netlist_xml.h`):

* hoy el lector **no comprueba los atributos de la raíz `<placa>`**: un
  `ilustracion=` se aceptaría y se ignoraría en silencio. Hay que leerlo, y de
  paso empezar a rechazar los atributos desconocidos de la raíz, como ya hace
  con todo lo demás;
* `<ilustracion>` dentro de `<placa>` hoy es «elemento desconocido»: hay que
  aceptarlo, con sus `<enlace pieza elemento [efecto] [observable] [mando]>`,
  y comprobar que cada `pieza` existe;
* en el `<sistema>`, `<placa id fichero>` hoy rechaza cualquier otro
  atributo: hay que admitir `ilustracion`, con la ruta relativa a la carpeta del
  sistema.

El `Netlist` guarda, por placa, la ruta resuelta y la tabla de enlaces. No
interpreta el SVG.

## 4. Lo que se comprueba

Sin leer el SVG —eso es de la ventana—, solo lo que se puede saber desde aquí:

* **el fichero no existe**: un **aviso**, no un error. Una placa sin dibujo
  sigue funcionando, y la ventana dibuja una genérica;
* **no empieza por `<svg` o `<?xml`**, o **pasa de un tamaño razonable** (2 MB,
  muy por debajo de los 8 MiB de `CUERPO_MAX`): un aviso, y no se manda;
* **una `pieza` de la tabla de enlaces que no existe**: un error de la placa,
  como cualquier otra referencia rota.

`--valida` dice qué dibujo usa cada placa, igual que dice qué firmware lleva
cada chip:

```
  placa N: dibujo nucleo_f446re.svg (14 kB)
  placa S: sin dibujo (no hay shield_leds.svg)
```

## 5. Cómo llega a la ventana

**Un mensaje nuevo, `T_ILUSTRACION`**, del modelo a la pantalla, uno por
dibujo distinto, **durante el saludo**: después de `T_CATALOGO` y antes de
`T_LISTO`. Su cuerpo, en el estilo de `T_HOLA`:

```
placas=L1 L2
fichero=pc104_leds.svg

<svg xmlns="http://www.w3.org/2000/svg" ...>...</svg>
```

Las cabeceras hasta la línea en blanco, y detrás el SVG tal cual. Un mismo
dibujo usado por varias placas —el mismo módulo dos veces en una pila— se manda
una vez.

**No sube la versión del protocolo.** La regla de `protocolo.h` es que un tipo
de mensaje nuevo no la sube, porque una ventana que no lo conoce lo salta por
su longitud. El tipo va en `protocolo.h`, que es el mismo fichero en los dos
repositorios: se sube primero a `mcu-sim-gui` y lo vigila `make gui-proto`.

**`T_PLACA` dice el dibujo de cada placa** en su descripción —
`<placa id="N" ... ilustracion="nucleo_f446re.svg">`—, y la tabla de enlaces,
si la hay, dentro de ella. Es un atributo más, como los de la §14 de
`doc/analisis_placas_conectadas.md`.

## 6. Los ficheros

* **Los dibujos viven junto a las placas**, en `src/placas/`:
  `nucleo_f446re.svg` al lado de `nucleo_f446re.xml`. Un solo sitio, y viajan
  con la placa.
* **Dibujos propios**, no fotos ni ilustraciones de los fabricantes, que tienen
  derechos. La de ejemplo es así: una aproximación dibujada a mano.
* **Siguen el perfil de autor** del análisis de la ventana (§11): SVG 1.2
  Tiny, geometría como atributo, sin scripts ni imágenes externas, tamaño en
  milímetros, y los elementos vivos con el id de su pieza.

## 7. Las placas, para que casen con su dibujo

Al poner el dibujo de ejemplo al lado de `placas/nucleo_f446re.xml` se ve lo
que falta en el XML para que el dibujo tenga algo que enseñar:

| En el dibujo | En el XML hoy | Propuesta |
| :--- | :--- | :--- |
| LED `LD2` | `Led` `LD2` | nada: casa por id |
| botón USER | `Button` `B1` | nada; en el dibujo, `id="B1"` |
| botón RESET | no hay pieza | **un `Button` `B2` en NRST**, a masa, sin rebotes (el condensador de la placa se los come), como el de la Discovery. Desde P-15, NRST es un nodo como cualquier otro |
| LED `PWR` | no hay pieza | un `Led` `LD3` en VDD: luce mientras hay alimentación |
| LED `COM` | no hay pieza | decorativo: es del ST-LINK, que no se modela |
| conectores Arduino | `CN5`, `CN6`, `CN8`, `CN9` | nada; en el dibujo, sus ids |
| morpho | no se declaran | decorativos mientras la placa no los declare |

Los ids de las piezas son ya los de la serigrafía (`LD2`, `B1`), que es lo que
hace que el enlace por id funcione sin tabla. Conviene mantenerlo como regla
para las placas nuevas.

Añadir B2 y LD3 a la Nucleo cambia lo que sale por consola al simularla (dos
piezas más en el resumen, un LED más en el informe final) y lo que comprueban
`make gui-sistema` y `prueba_cruzada` de la ventana; no mueve ningún invariante,
porque las suites no usan este fichero.

## 8. Lo que cuesta aquí

| | Qué | Tamaño |
| :--- | :--- | :--- |
| Lector | `ilustracion=` en `<placa>` y en `<placa id>` del sistema; `<ilustracion>` con sus `<enlace>`; la raíz deja de aceptar atributos desconocidos | pequeño |
| `Netlist` | Por placa: la ruta resuelta y la tabla de enlaces; volcarlas en `T_PLACA` | pequeño |
| `sim_main.cpp` | Resolver las rutas, las comprobaciones de §4, el resumen de `--valida` y mandar los `T_ILUSTRACION` en el saludo | pequeño |
| `protocolo.h` | El tipo `T_ILUSTRACION` (primero en la ventana) | una línea |
| Placas | `nucleo_f446re.svg`, y B2 y LD3 en `nucleo_f446re.xml` | el dibujo |
| Pruebas | Un grupo en `verif/gui/sistema.py`: llega un `T_ILUSTRACION` por dibujo, con sus placas; el de un dibujo compartido llega una vez; un fichero que falta es un aviso y no se manda; una pieza de la tabla que no existe es un error | pequeño |
| Documentación | `doc/parts.md` §2 (el atributo y la tabla), `src/README.md` (las placas con dibujo), P-12 en `doc/todo.md` | pequeño |

Nada de eso depende de Qt ni de que la ventana sepa pintar SVG: se puede hacer
antes, a la vez o después de la parte de la ventana, y una ventana que todavía
no lo pinta simplemente salta el mensaje.

## 9. Lo que hay que decidir

* **Si se busca el SVG por el nombre del XML** cuando la placa no lo declara
  (§3), o solo cuando lo declara. Recomiendo buscarlo: es lo cómodo y no
  sorprende a nadie que tenga un `.svg` al lado.
* **Si se añaden B2 y LD3 a la Nucleo** (§7), que es lo que da sentido a su
  dibujo.
* **El tamaño máximo** de un dibujo (propuesto: 2 MB).
