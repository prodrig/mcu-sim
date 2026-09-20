// =============================================================================
// prueba_macros_win.cpp — El campo de minas del preprocesador de Windows
//
// NO ES UNA PRUEBA DE EJECUCIÓN: es una prueba de COMPILACIÓN, y por eso no
// tiene `main` ni imprime nada. Si compila, pasa.
//
// QUÉ PROTEGE. Las cabeceras que este proyecto comparte con `mcu-sim-gui`
// —`common/protocolo.h` y `common/gui_destino.h`— tienen que compilar en
// Windows, y allí el preprocesador llega antes que el compilador con una
// colección de macros de nombre corto y genérico. **Un `namespace` no protege
// de una macro**: el preprocesador no sabe qué es un espacio de nombres.
//
// Pasó de verdad, y por eso existe este fichero. Un enumerador llamado `R_OK`
// compilaba en Linux sin un aviso y reventaba en MSYS2, porque `<systemc>`
// arrastra `<string>` → `<cwchar>` → `<wchar.h>` → `<sys/stat.h>` → `<io.h>`,
// donde `R_OK` vale 4. El error —«expected identifier before numeric
// constant»— no menciona la macro por ningún lado, y hay cinco cabeceras entre
// la causa y el síntoma.
//
// CÓMO PROTEGE. Definiendo AQUÍ, a mano, las macros que Windows define allí, e
// incluyendo después las cabeceras compartidas. Así el fallo sale **en Linux**,
// en la máquina de quien escribe el código, y no tres semanas después en la de
// un alumno. No cubre todas las macros de Windows —eso es imposible— pero sí
// las de nombre corto, que son las que muerden.
//
// SI ESTE FICHERO DEJA DE COMPILAR: no toques este fichero. Renombra el
// identificador que colisiona en la cabecera compartida, con un prefijo largo,
// y acuérdate de que hay DOS copias de `protocolo.h` que tienen que seguir
// siendo idénticas byte a byte.
// =============================================================================

// --- <io.h> / <unistd.h>: los modos de access() -----------------------------
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

// --- <windows.h> y compañía: las de nombre corto que más daño hacen ---------
#define IN
#define OUT
#define OPTIONAL
#define far
#define near
#define small char
#define interface struct
#define TRUE  1
#define FALSE 0
#define ERROR 0
#define NO_ERROR 0L
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

// --- Las que ponen los sistemas de construcción -----------------------------
#define VERSION "0.0.0"
#define PACKAGE "algo"
#define DEBUG 1

// Y ahora las cabeceras compartidas, que tienen que sobrevivir a todo eso.
#include "../common/protocolo.h"
#include "../common/gui_destino.h"

// Una comprobación de que lo de arriba no es decorativo: si `VERSION` se
// hubiera quedado como nombre de constante, esta línea no compilaría.
static_assert(mcusim::proto::VERSION_PROTO == 1, "la version del protocolo es 1");
static_assert(sizeof(mcusim::proto::Orden) == 16, "una Orden son 16 bytes");
static_assert(mcusim::proto::RES_OK == 0, "RES_OK sigue siendo cero");
