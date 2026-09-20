// =============================================================================
// protocolo.h — El contrato entre mcu-sim y mcu-sim-gui
//
// ESTE FICHERO VIVE EN DOS REPOSITORIOS Y TIENE QUE SER EL MISMO EN LOS DOS.
// La copia de referencia es la de `mcu-sim-gui/src/protocolo.h`; la de
// `mcu-sim/src/common/protocolo.h` es una copia vendida, y la comprobación T-GUI
// de la suite de mcu-sim falla si dejan de coincidir byte a byte. No es
// burocracia: un protocolo cuyos dos extremos discrepan en un `uint16_t` no
// falla al compilar, falla en marcha y sin decir por qué.
//
// QUÉ ES. Un marco binario sobre UNA conexión TCP. Cabecera de tamaño fijo con
// la longitud del cuerpo, de modo que un extremo puede SALTARSE un mensaje que
// no entiende en vez de morirse: esa es la propiedad que hace que las dos
// partes se puedan versionar por separado, y es toda la razón de que la
// longitud esté en la cabecera y no implícita en el tipo.
//
// QUÉ NO ES. No es un formato de fichero ni un formato de red: es little-endian
// a secas porque los dos extremos son x86 o ARM en modo little-endian y se
// declara aquí en vez de fingir que se ha pensado. Si algún día hay un extremo
// big-endian, esto hay que arreglarlo, y arreglarlo son los seis `htole*` que
// hoy no están.
//
// Lo que NO cruza este socket, dicho aquí para que no haya dudas luego, y es lo
// mismo que dice `doc/analisis_gui.md` §2.3:
//
//   * punteros a objetos del modelo. Ni uno;
//   * tensiones y corrientes de pin, salvo como observable DECLARADO de una
//     pieza a la que tenga sentido mirárselo (la corriente de un LED, sí; la de
//     PA7, no);
//   * registros del MCU. Para eso están los dos servidores de GDB, que ya
//     existen y hablan un protocolo que los IDE entienden.
// =============================================================================
#ifndef MCU_SIM_PROTOCOLO_H
#define MCU_SIM_PROTOCOLO_H

#include <cstdint>
#include <cstddef>

namespace mcusim {
namespace proto {

// ---------------------------------------------------------------------------
// Identidad y versión
// ---------------------------------------------------------------------------

// 'M','S','G','1' leídos en little-endian. Sirve para dos cosas: cerrar de
// inmediato una conexión que no es de esto (alguien apuntó su navegador al
// puerto) y resincronizar una traza a mano.
inline constexpr uint32_t MAGIA = 0x3147534Du;

// La versión del PROTOCOLO, no la del programa. Sube cuando cambia el
// significado o la disposición de algo que ya existía; NO sube por añadir un
// tipo de mensaje nuevo, porque para eso está el salto por longitud.
inline constexpr uint16_t VERSION = 1;

// El puerto por omisión. Vecino del 3333 de los dos servidores de GDB, para que
// los puertos del proyecto se recuerden juntos, y fuera del rango bien
// conocido. Se cambia con `--gui host:puerto`.
inline constexpr uint16_t PUERTO_OMISION = 3344;

// Tope de cuerpo que un extremo acepta sin cerrar la conexión. El mensaje más
// grande de verdad es el XML de la placa (`banco.xml` son ~40 kB) y el
// catálogo; ocho megas es holgura de sobra y a la vez un techo que impide que
// una longitud corrupta pida un `malloc` de dos gigas.
inline constexpr uint32_t CUERPO_MAX = 8u * 1024u * 1024u;

// ---------------------------------------------------------------------------
// La cabecera: 16 bytes, sin relleno, todos los campos alineados naturalmente
// ---------------------------------------------------------------------------
struct Cabecera {
    uint32_t magia;        // MAGIA
    uint16_t version;      // la versión EN USO en esta conexión
    uint16_t tipo;         // Tipo, abajo
    uint32_t longitud;     // bytes del cuerpo que siguen a esta cabecera
    uint32_t secuencia;    // contador propio de cada sentido, desde 0
};
static_assert(sizeof(Cabecera) == 16, "la cabecera son 16 bytes exactos");

// ---------------------------------------------------------------------------
// Los tipos de mensaje
//
// El rango dice el sentido y eso es deliberado: un extremo que recibe un tipo
// de su propio rango sabe que algo está mal conectado sin tener que mirar el
// contenido.
//
//   0x0000-0x7FFF   del MODELO a la PANTALLA   (mcu-sim  -> mcu-sim-gui)
//   0x8000-0xFFFF   de la PANTALLA al MODELO   (mcu-sim-gui -> mcu-sim)
// ---------------------------------------------------------------------------
enum Tipo : uint16_t {
    // --- Saludo, antes de que la simulación exista -------------------------
    T_HOLA        = 0x0001,  // quién soy y qué versión hablo
    T_PLACA       = 0x0002,  // el XML de `--netlist`, tal cual, UTF-8
    T_CATALOGO    = 0x0003,  // observables y mandos de cada pieza
    T_LISTO       = 0x0004,  // elaborado y ESPERANDO. `sc_start()` no se ha llamado

    // --- En marcha ---------------------------------------------------------
    T_INSTANTANEA = 0x0010,  // t_sim_ns + n muestras de los observables suscritos
    T_AVISO       = 0x0011,  // texto para la persona: info, aviso, error
    T_ESTADO      = 0x0012,  // corriendo/pausado/terminado + los dos relojes
    T_ORDEN_HECHA = 0x0013,  // eco de una orden con el instante REAL en que se aplicó
    T_PONG        = 0x0014,
    T_FIN         = 0x001F,  // se acabó: motivo y código de salida

    // --- De la pantalla al modelo ------------------------------------------
    T_VERSION     = 0x8000,  // la versión que la GUI elige hablar. SIEMPRE el primero
    T_SUSCRIBE    = 0x8001,  // qué observables quiere ver, y cada cuánto
    T_ARRANCA     = 0x8002,  // ¡ahora! política de ritmo y ventana de tiempo
    T_PAUSA       = 0x8003,
    T_SIGUE       = 0x8004,
    T_PASO        = 0x8005,  // avanza N ns simulados y vuelve a pausa
    T_ORDENES     = 0x8006,  // n x Orden, con la semántica de deltas de abajo
    T_PARA        = 0x8007,  // final ordenado: sc_stop()
    T_PING        = 0x8008
};

inline bool es_del_modelo(uint16_t t)  { return (t & 0x8000u) == 0; }
inline bool es_de_pantalla(uint16_t t) { return (t & 0x8000u) != 0; }

// ---------------------------------------------------------------------------
// Los cuerpos que son POD. Los demás son texto UTF-8 sin terminador: la
// longitud de la cabecera es la que manda, y NO se supone un '\0' al final.
// ---------------------------------------------------------------------------

// Lo que el modelo publica: un identificador plano y un valor. El
// identificador lo asigna T_CATALOGO y vale para toda la ejecución, porque la
// elaboración de SystemC es estática y el inventario de piezas no cambia
// después de ella. Por eso no hace falta renegociarlo nunca.
struct Muestra {
    uint16_t id;           // id_obs global, el que dio el catálogo
    uint16_t relleno;      // a cero. Está para que la struct mida 8 y no 6
    float    valor;
};
static_assert(sizeof(Muestra) == 8, "Muestra son 8 bytes");

// Cabecera de T_INSTANTANEA; detrás van `n` Muestra seguidas.
struct CabInstantanea {
    uint64_t t_sim_ns;
    uint32_t n;
    uint32_t perdidas;     // instantáneas descartadas desde la anterior (véase abajo)
};
static_assert(sizeof(CabInstantanea) == 16, "CabInstantanea son 16 bytes");

// Lo que la pantalla manda: tocar un mando de una pieza.
//
// LA SEMÁNTICA DEL TIEMPO, que es lo único no obvio de todo el protocolo:
// dentro de UN mensaje T_ORDENES con n órdenes,
//
//   * la PRIMERA lleva un tiempo ABSOLUTO desde el inicio de la simulación si
//     el mensaje llega ANTES de T_ARRANCA; y un tiempo RELATIVO al instante
//     simulado en que el modelo lo saca de la cola, si la simulación ya está
//     en marcha;
//   * las SIGUIENTES llevan el tiempo TRANSCURRIDO DESDE LA ANTERIOR. Deltas,
//     no instantes. Un delta de 0 es legal y quiere decir «en el mismo
//     instante, y en el orden en que vienen»; un delta no cabe negativo porque
//     el campo no tiene signo, y eso es lo que garantiza la monotonía.
//
// La diferencia entre los dos casos no es cosmética: una secuencia enviada
// ANTES de arrancar es reproducible al picosegundo, y una enviada en marcha
// aterriza donde el reloj de pared decida. Por eso el modelo devuelve
// T_ORDEN_HECHA con el instante real: con eso la sesión se puede grabar y
// volver a ejecutar sin GUI (`doc/analisis_gui.md` §9).
struct Orden {
    uint64_t t_sim_ns;     // absoluto (la 1.ª) o delta desde la anterior
    uint16_t pieza;        // índice de pieza que dio el catálogo
    uint16_t mando;        // índice de mando dentro de esa pieza
    float    valor;
};
static_assert(sizeof(Orden) == 16, "Orden son 16 bytes");

// Eco de T_ORDEN_HECHA: la orden tal y como se aplicó, ya con instante absoluto.
struct OrdenHecha {
    uint64_t t_sim_ns;     // el instante simulado REAL, absoluto
    uint16_t pieza, mando;
    float    valor;
    uint32_t resultado;    // 0 = aplicada; véase Resultado
    uint32_t relleno;
};
static_assert(sizeof(OrdenHecha) == 24, "OrdenHecha son 24 bytes");

enum Resultado : uint32_t {
    R_OK          = 0,
    R_PIEZA       = 1,   // no hay tal pieza
    R_MANDO       = 2,   // la pieza no tiene ese mando
    R_RANGO       = 3,   // valor fuera de [min, max] del mando: se recorta y se avisa
    R_TARDE       = 4    // el instante pedido ya había pasado: se aplicó al recibirla
};

// T_ARRANCA
enum Ritmo : uint32_t {
    RIT_REAL   = 0,   // 16,7 ms simulados por fotograma, esperando si sobra tiempo
    RIT_LIBRE  = 1,   // todo lo que se pueda
    RIT_DEMANDA= 2    // no avanza sin T_PASO
};
struct Arranca {
    uint32_t ritmo;        // Ritmo
    float    factor;       // solo con RIT_REAL: 1.0 = tiempo real, 0.5 = la mitad
    uint64_t ventana_ns;   // 0 = indefinida (como hoy con --gdb)
};
static_assert(sizeof(Arranca) == 16, "Arranca son 16 bytes");

// T_SUSCRIBE: cabecera y detrás `n` uint16_t con los id_obs que se quieren.
// n = 0 quiere decir «ninguno»: es legal y apaga las instantáneas.
struct CabSuscribe {
    uint32_t periodo_ns_lo, periodo_ns_hi;   // periodo de muestreo en ns simulados
    uint32_t n;
    uint32_t relleno;
};
static_assert(sizeof(CabSuscribe) == 16, "CabSuscribe son 16 bytes");

// T_PASO
struct Paso { uint64_t ns; };

// T_ESTADO
enum Fase : uint32_t { F_ESPERANDO = 0, F_CORRIENDO = 1, F_PAUSADA = 2, F_TERMINADA = 3 };
struct Estado {
    uint32_t fase;         // Fase
    uint32_t relleno;
    uint64_t t_sim_ns;
    double   t_pared_s;    // segundos de reloj de pared desde T_ARRANCA
    uint64_t deltas;       // sc_delta_count()
};
static_assert(sizeof(Estado) == 32, "Estado son 32 bytes");

// T_AVISO: cabecera y detrás el texto UTF-8, `longitud - sizeof(CabAviso)` bytes.
enum Nivel : uint32_t { N_INFO = 0, N_AVISO = 1, N_ERROR = 2, N_FATAL = 3 };
struct CabAviso {
    uint32_t nivel;        // Nivel
    uint32_t origen_len;   // bytes del `id` de SC_REPORT, que van primero
    uint64_t t_sim_ns;
};
static_assert(sizeof(CabAviso) == 16, "CabAviso son 16 bytes");

// T_FIN
enum Motivo : uint32_t {
    M_VENTANA  = 0,   // se agotó la ventana de tiempo pedida
    M_PARA     = 1,   // la GUI lo pidió
    M_ERROR    = 2,   // el modelo se rindió
    M_SC_STOP  = 3    // alguien llamó a sc_stop() desde dentro del modelo
};
struct Fin {
    uint32_t motivo;       // Motivo
    int32_t  codigo;       // el que devolverá el proceso
    uint64_t t_sim_ns;
};
static_assert(sizeof(Fin) == 16, "Fin son 16 bytes");

// T_HOLA y T_VERSION llevan texto: véase `doc/protocolo.md` §3. Son los dos
// únicos mensajes cuyo cuerpo es texto estructurado y no POD, a propósito: son
// los que tienen que poder crecer sin romper a nadie.

} // namespace proto
} // namespace mcusim

#endif // MCU_SIM_PROTOCOLO_H
