// =============================================================================
// sim_main.cpp — El modelo, con la placa en un fichero
//
//   ./build/mcu-sim placa.xml [firmware.bin] [ms_simulados]
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f4xx/stm32f407vg_parts_paso3.md.)
//
// Es el modo de uso que justifica los tres pasos. `sc_main.cpp` es la suite de
// verificación: 1871 comprobaciones sobre una placa fija escrita en C++. Esto
// es lo otro — los MCUs y la placa que diga el XML, con el firmware que se les
// pase—, y para cambiar de placa no hace falta recompilar nada.
//
// VARIOS MCUs. Una placa puede declarar cero, uno o varios `<mcu>`, cada uno
// con su firmware, su modo de depuración y su puerto de GDB
// [doc/multi_mcu.md, §5]:
//
//   <mcu tipo="STM32F407VG" id="u0" firmware="maestro.bin"
//        depuracion="dap"   puerto_gdb="3333"/>
//   <mcu tipo="STM32F407VG" id="u1" firmware="esclavo.bin"
//        depuracion="pines" puerto_gdb="3334"/>
//
// Y la regla que hace que nada de esto rompa lo anterior:
//
//   ningún <mcu>   con `--mcu TIPO`, un MCU de ese tipo y nodos con nombre
//                  desnudo (`PD12`). SIN `--mcu`, LA PLACA NO LLEVA MCU: solo
//                  sus piezas, todo nodo es de la placa, y un nodo con nombre
//                  de pin (`PD12`, `NRST`, `VDD`...) es un error que dice qué
//                  falta, porque casi siempre es un <mcu> olvidado. Hasta el
//                  2026-10-05 sin decir nada se montaba un STM32F407VG; ahora
//                  el MCU se declara siempre;
//   un <mcu>       valen los dos nombres, `PD12` y `u0.PD12`, y los argumentos
//                  de la línea de órdenes siguen sirviendo: manda la línea de
//                  órdenes sobre lo que diga el XML, también `--mcu` sobre su
//                  tipo;
//   dos o más      solo con prefijo, y cada MCU lleva LO SUYO en el XML. Un
//                  firmware o un puerto sueltos en la línea de órdenes ya no
//                  designan a nadie, así que se rechazan nombrando los MCUs:
//                  un firmware cargado en el chip equivocado es de los fallos
//                  más caros de diagnosticar.
//
// El orden importa y es el único posible:
//
//   1. leer el fichero -que NO construye nada, solo declara-;
//   2. resolver la lista de MCUs y aplicarle la línea de órdenes;
//   3. crear los nodos COMPARTIDOS que la placa declare con `une`, porque un
//      pad que va a un nodo compartido no puede crear el suyo y `Pad::net` es
//      un `sc_port` que se ata en el constructor: la decisión hay que tomarla
//      antes de que el MCU exista;
//   4. construir los MCUs con ese cableado y dar de alta sus nodos (154 por
//      chip: los 144 pads con su nombre de esquemático y los diez de
//      alimentación);
//   5. validar la declaración: nodo inexistente, pad que este encapsulado no
//      saca, identificador repetido, referencia hacia delante, tipo que la
//      factoría no conoce;
//   6. construir las piezas;
//   7. validar lo ELÉCTRICO, que necesita las piezas montadas para saber qué
//      terminal conduce y cuál solo escucha;
//   8. y solo entonces `sc_start()`.
//
// Que leer vaya ANTES que construir los MCUs no era así al principio, y es lo
// que permite los pasos 2 y 3. Fue un acierto del paso 3 que aquí se cobra
// solo: leer devuelve datos, y con datos todavía se puede decidir.
//
// Todo lo que va del 1 al 7 ocurre en la elaboración, porque la de SystemC es
// estática: no se puede añadir una pieza con la simulación en marcha. Esa es
// también la razón por la que los pasos 5 y 7 sirven para algo — avisan antes
// de simular, que es cuando el aviso todavía ahorra tiempo.
// =============================================================================
#include <systemc>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../common/asan_opciones.h"
#include "../common/gui_destino.h"
#include "../common/gui_cliente.h"
#include "../common/serie_destino.h"
#include "../common/svg_variantes.h"
#include "soc_f4.h"
#include "../verif/image_loader.h"

// ---------------------------------------------------------------------------
// EL TITULAR DEL COPYRIGHT, EN UN SOLO SITIO.
//
// La AGPL se hace valer a traves del copyright, asi que este nombre no es
// decorativo: es lo que convierte «hay que publicar las mejoras» en algo
// exigible. Va aqui y no repartido por cien ficheros para que cambiarlo sea
// una linea. Lo imprime `--licencia`, y el fichero LICENSE de la raiz dice lo
// mismo.
//
// ESTA A PROPOSITO SIN RELLENAR EN EL PARCHE QUE LO INTRODUJO: ponerle un
// nombre a la titularidad de otro no es cosa de quien escribe el codigo.
// ---------------------------------------------------------------------------
// La version que `mcu-sim` dice de si mismo en el saludo con la GUI (T_HOLA,
// clave `mcu_sim`). Es informativa: lo que decide si los dos se entienden es
// la version del PROTOCOLO, no esta. Quien empaqueta puede fijarla al
// compilar (-DVERSION_MCU_SIM=\"0.2.0\").
#ifndef VERSION_MCU_SIM
#define VERSION_MCU_SIM "desarrollo"
#endif

#ifndef TITULAR_COPYRIGHT
#define TITULAR_COPYRIGHT "Francisco Rodríguez Ballester (prodrig@disca.upv.es)"
#endif
#include "../verif/gdb_stub.h"
#include "../parts/netlist_parts.h"
#include "../parts/netlist_xml.h"
#include "../parts/frontera_gui.h"
#include "../parts/enlace_gui.h"
#include "../soc/stm32f4_mcu.h"
#include "../soc/stm32f446.h"

using namespace sc_core;
using namespace stm32;

// Los tipos de MCU que este ejecutable sabe CONSTRUIR: los once miembros de la
// familia F405/F407, que salen del catálogo de `top/mcu_caps.h` y no de una
// lista escrita aquí. Un miembro nuevo aparece en `--mcu`, en `--help` y en el
// mensaje de error sin tocar este fichero.
static std::string mayus(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}
static std::string tipos_como_texto() { return mcus_como_texto(); }

// Y las FAMILIAS que este ejecutable sabe construir, que es otra cosa: el
// catálogo dice qué chips existen, y la factoría dice de cuáles hay modelo.
// Mientras solo esté registrada `STM32F4` las dos listas se corresponden; el
// día que el catálogo tenga un F446 y su modelo no esté enlazado, este texto
// es el que se lo explica al usuario en vez de montarle otro chip a escondidas.
static std::string familias_como_texto() {
    std::string s;
    for (const std::string& f : FabricaMcu::familias()) {
        if (!s.empty()) s += ", ";
        s += f;
    }
    return s.empty() ? std::string("(ninguna)") : s;
}

static std::string g_placa, g_img, g_nombre = "placa";
static double      g_ms      = 100.0;
// Si la ventana de tiempo se dio en la linea de ordenes. Con `--gui` importa:
// si no se dio, y la ventana tampoco la pone en T_ARRANCA, la simulacion no
// tiene fin y se para desde la ventana (fase 6).
static bool        g_ms_dado = false;
static bool        g_sin_fin = false;
static bool        g_solo_valida = false;
// `--espera-terminal` (P-14, fase D7): no arrancar el MCU hasta que haya un
// terminal en cada puente serie por red. Sin esto, lo primero que imprime un
// firmware -el saludo- sale cuando todavia no hay nadie y se descarta (D-6).
static bool        g_espera_terminal = false;
static bool        g_ondas = false;
static bool        g_traza_gdb = false;
// Factor de tiempo real: 0 = a toda velocidad (lo de siempre);
// 1 = un segundo simulado por segundo de reloj de pared; 0,5 = a la
// mitad, para poder mirar lo que pasa.
static double      g_tiempo_real = 0.0;
// `--mcu TIPO`. Vacio, no se ha dicho. NO HAY MCU POR OMISION: sin <mcu> en el
// XML y sin esto, la placa va sin MCU. Con un <mcu> en el XML, MANDA SOBRE SU
// TIPO -como el firmware o el puerto de la linea de ordenes mandan sobre los
// suyos-; con varios, no dice a cual y es un error.
static std::string g_tipo_mcu;
// Depuración pedida por la línea de órdenes. `g_gdb_modo` vacío = no se pidió.
static std::string g_gdb_modo;          // "pines" o "dap"
static unsigned    g_gdb_puerto = 0;
static bool        g_puerto_dado = false;

// --- La ventana (`mcu-sim-gui`) ---------------------------------------------
// `--gui host:puerto` dice donde esta la ventana. Desde la fase 3 del plan,
// `sim` se conecta a ella despues de montar la placa, le manda la placa y el
// catalogo, y NO EMPIEZA a simular hasta que la ventana diga «arranca»
// (`saluda_gui`, mas abajo). Sin el argumento, nada de esto existe.
static bool          g_gui_pedida = false;
static stm32::gui::Destino g_gui;

// --- Los puentes serie (`PuenteSerie`) --------------------------------------
// `--serie ID=DESTINO` se aplica sobre lo que dice la placa y se comprueba que
// no choca con nada (fase D0). La pieza existe desde la D2 con el destino
// `memoria`; el socket en crudo llegó en la D3 y RFC 2217 en la D5
// [doc/analisis_puente_serie.md §10].
static std::vector<stm32::serie::Asignacion> g_serie;

// ---------------------------------------------------------------------------
// Un MCU montado: lo que la placa declaró, el chip, su stub de pines si lo
// lleva, y los drivers con los que se le da corriente.
// ---------------------------------------------------------------------------
struct McuMontado {
    DeclMcu       decl;
    // El chip, por la interfaz: es lo que la factoria devuelve, y lo que
    // permite que `tipo=` despache de verdad en vez de comprobarse.
    mcu_if*       mcu  = nullptr;
    GdbStub*      stub = nullptr;       // solo en modo "pines" y con puerto
};

// La conexion con la ventana, si hay `--gui` y el saludo ha empezado. Es
// global por `muere()`: un modelo que se rinde con la GUI escuchando se lo
// dice con un T_FIN antes de irse, en vez de dejarla mirando un socket que se
// cierra sin explicacion.
static stm32::gui::ClienteGui* g_cliente = nullptr;
// Y con la simulacion en marcha, el enlace que atiende esa conexion (fase 4).
static stm32::gui::EnlaceGui*  g_enlace  = nullptr;

static uint64_t ahora_ns() {
    return uint64_t(sc_time_stamp().value() / sc_time(1, SC_NS).value());
}

static void muere(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    if (g_enlace)       g_enlace->termina(mcusim::proto::M_ERROR, 2);
    else if (g_cliente) g_cliente->fin(mcusim::proto::M_ERROR, 2, ahora_ns());
    std::exit(2);
}

// ---------------------------------------------------------------------------
// `sim --help COMPONENTE`
//
// La ficha de una pieza: qué hace, qué terminales tiene y qué atributos admite
// en el XML. El texto NO ESTÁ AQUÍ: lo lleva cada entrada de la factoría, en la
// misma llamada que da de alta el creador [parts/part_help.h]. Esto solo lo
// busca y lo imprime, y por eso una pieza nueva sale en `--help` sin tocar este
// fichero — que es media respuesta a «añade esta capacidad a todos los
// componentes que se añadan en el futuro». La otra media es que la macro de
// registro exige la ayuda, así que no se puede añadir una pieza sin ella.
// ---------------------------------------------------------------------------
static int ayuda_de_componente(const std::string& que) {
    const std::string tipo = Fabrica::busca_laxo(que);
    if (tipo.empty()) {
        std::fprintf(stderr,
            "no se que es un componente de tipo '%s'.\n\n"
            "Los tipos que se saben construir son:\n  %s\n\n"
            "Se escriben con la mayuscula inicial en el XML -`Led`, no `LED`-,\n"
            "aunque aqui, para preguntar, da igual como se escriban.\n",
            que.c_str(), Fabrica::tipos_como_texto().c_str());
        return 1;
    }
    const Ayuda* a = Fabrica::ayuda(tipo);
    // No puede pasar -la macro de registro exige la ayuda-, pero si alguien
    // consigue registrar una pieza sin ella, que se vea, y que se vea dónde se
    // arregla. Una ayuda vacía en silencio sería lo único peor que no tenerla.
    if (!a || !a->completa()) {
        std::fprintf(stderr,
            "el componente '%s' existe pero no se ha explicado: su entrada en\n"
            "src/parts/netlist_parts.h se registro con una ayuda vacia. Eso es\n"
            "un fallo del modelo, no del fichero de placa.\n", tipo.c_str());
        return 1;
    }
    std::fputs(a->texto(tipo).c_str(), stdout);
    return 0;
}

SC_MODULE(Sim) {
    std::vector<McuMontado> mcus;
    NodeMap      nodos;
    Netlist      placa;
    unsigned     n_avisos = 0;
    bool         hay_stub = false;
    bool         hay_puente_red = false;   // un PuenteSerie por TCP (D3)

    // La frontera con `mcu-sim-gui` (fase 1 de su plan): el muestreador y el
    // aplicador. Se construye SIEMPRE, porque la elaboracion de SystemC es
    // estatica y no se puede decidir despues; y mientras nadie la active sus
    // dos procesos esperan sobre un evento que nadie notifica, asi que sin
    // `--gui` este programa simula exactamente lo mismo que antes. La activa
    // el saludo (`saluda_gui`), con el catalogo de las piezas ya montadas.
    stm32::gui::FronteraGui frontera{"frontera"};
    // Y el enlace que atiende la conexion con la simulacion en marcha (fase
    // 4): lo mismo, construido siempre y quieto hasta que el saludo lo active.
    stm32::gui::EnlaceGui   enlace{"enlace", frontera};
    // Los avisos de la placa -lo electrico y los puentes serie-, para la
    // ventana: los ve antes de pulsar «arranca», que es cuando ahorran tiempo.
    std::vector<std::string> avisos_placa;
    // Los dibujos de las placas, leídos, para mandárselos a la ventana: uno
    // por FICHERO, con las placas que lo usan (doc/analisis-uso-ilustraciones.md)
    struct DibujoLeido {
        std::vector<std::string> placas;   // ids en el sistema; "" en una placa suelta
        std::string ruta, svg;
        std::map<std::string, std::string> variantes;   // las de sus piezas
        std::map<std::string, std::string> rotulos;     // y lo que dicen sus textos
        int giro = 0;                                   // y cuánto se gira
    };
    std::vector<DibujoLeido> dibujos;
    // Un dibujo de placa es un dibujo, no una foto en alta resolucion: mucho
    // menos que los 8 MiB que deja pasar el protocolo
    static constexpr size_t DIBUJO_MAX = 2u * 1024u * 1024u;

    SC_CTOR(Sim) {
        // --- 1. Leer. No construye nada: devuelve datos ---------------------
        const std::string e = placa_o_sistema_desde_fichero(placa, g_placa, &g_nombre);
        if (!e.empty()) muere("error de netlist: " + e);

        // --- 2. La lista de MCUs, y la línea de órdenes encima ---------------
        std::vector<DeclMcu> decls = placa.mcus();
        if (!g_tipo_mcu.empty()) {
            if (decls.empty() && placa.es_sistema()) {
                // En un sistema no hay a qué placa ponérselo: el chip se
                // declara en la placa que lo lleva.
                muere("en un <sistema>, --mcu no pone un MCU: ninguna placa declara "
                      "uno, y no se sabe en cual iria. Declaralo en la placa que lo "
                      "lleva, con <mcu tipo=\"" + g_tipo_mcu + "\" id=\"u0\"/>");
            } else if (decls.empty()) {      // ninguno declarado: el de --mcu
                DeclMcu m;
                m.tipo = g_tipo_mcu;         // id vacío -> nodos con nombre desnudo
                decls.push_back(m);
            } else if (decls.size() == 1) {  // --mcu manda sobre su tipo
                // Se vacia la salida: si la placa no cabe en el chip nuevo, el
                // error sale por stderr y tiene que leerse DESPUES de esto.
                if (mayus(decls[0].tipo) != g_tipo_mcu) {
                    std::printf("  [mcu] %s: --mcu %s sustituye al %s que declara "
                                "la placa\n", decls[0].id.c_str(), g_tipo_mcu.c_str(),
                                decls[0].tipo.c_str());
                    std::fflush(stdout);
                }
                decls[0].tipo = g_tipo_mcu;
                placa.cambia_tipo_mcu(decls[0].id, g_tipo_mcu);
            } else {
                std::string ids;
                for (const DeclMcu& m : decls) { if (!ids.empty()) ids += ", "; ids += m.id; }
                muere("la placa lleva " + std::to_string(decls.size()) + " MCUs (" + ids +
                      "), asi que --mcu no dice a cual cambiar de tipo.\n"
                      "Cambialo en su <mcu tipo=\"...\">");
            }
        }
        if (decls.empty()) {
            placa.pon_sin_mcu();
            comprueba_sin_mcu();
        }
        aplica_linea_de_ordenes(decls);
        // Cada chip se resuelve contra el catálogo, y de ahí sale su descriptor
        // completo: núcleo, memorias, encapsulado y qué periféricos lleva. Un
        // tipo que no está NO se sustituye por otro — se rechaza diciendo
        // cuáles hay, porque montar un F407 donde el usuario escribió otra cosa
        // sería la clase de mentira que este modelo no se permite.
        std::vector<const McuCaps*> caps_de(decls.size(), nullptr);
        for (size_t k = 0; k < decls.size(); ++k) {
            DeclMcu& m = decls[k];
            m.tipo = mayus(m.tipo);
            caps_de[k] = mcu_por_nombre(m.tipo);
            if (!caps_de[k])
                muere("mcu " + (m.id.empty() ? std::string("(el de --mcu)") : m.id) +
                      ": no se sabe construir un '" + m.tipo + "'.\n"
                      "Los tipos que este programa modela son:\n  " +
                      tipos_como_texto() + "\n"
                      "Un MCU de OTRA FAMILIA no es un parametro: es otro modelo, "
                      "con su arbol de reloj y sus perifericos.");
            m.enc = &caps_de[k]->enc;
            // Y el encapsulado tambien al netlist, que es quien valida los pads
            // ANTES de que los MCU existan.
            if (m.id.empty()) placa.fija_encapsulado_implicito(m.enc);
            else              placa.fija_encapsulado(m.id, m.enc);
            // El WLCSP90 lleva su reparto de bolas sin contrastar, y eso se
            // dice en voz alta en vez de esconderse en un comentario.
            if (!caps_de[k]->enc.verificado)
                std::printf("  [aviso] %s: el reparto de pads del %s es una "
                            "reconstruccion a partir del recuento del datasheet "
                            "(%u E/S) y NO esta contrastado bola a bola\n",
                            caps_de[k]->nombre, caps_de[k]->enc.nombre,
                            caps_de[k]->enc.n_gpio);
        }

        // --- 2 bis. Conectores, acoples e hilos: qué nodos son el mismo ------
        // Antes de los MCUs por lo mismo que `une`: un pin de conector unido a
        // otro pad de otro chip hace que esos dos pads compartan nodo, y eso
        // se decide antes de atar un solo `sc_port`. Sin conectores ni hilos
        // no hace nada.
        {
            const std::vector<std::string> ea = placa.resuelve_alias();
            for (const std::string& q : ea) std::fprintf(stderr, "  [decl] %s\n", q.c_str());
            if (!ea.empty())
                muere(g_placa + ": " + std::to_string(ea.size()) +
                      " problemas con conectores e hilos; no se monta");
        }

        // --- 3. Los nodos COMPARTIDOS, antes de los MCUs --------------------
        // Si la placa no declara ninguno -que es lo normal- los cableados salen
        // vacíos y cada chip se construye exactamente igual que siempre.
        std::map<std::string, Cableado> cabs;
        const std::string ec = cableado_desde_netlist(placa, nodos, cabs);
        if (!ec.empty()) muere("  [decl] " + ec);

        // --- 4. Los MCUs, y sus nodos ---------------------------------------
        const Cableado sin_puentes;          // para el chip que no tiene ninguno
        for (const DeclMcu& d : decls) {
            McuMontado m;
            m.decl = d;
            const auto it = cabs.find(d.id);
            const Cableado& cab = (it == cabs.end()) ? sin_puentes : it->second;
            // Los rasgos de depuración son POR INSTANCIA, que es lo que permite
            // que un chip lleve el stub interno y el de al lado el de pines.
            DebugCaps caps = DBG_PINES;
            if (d.depuracion == "dap") {
                caps.attach = DebugAttach::Interno;
                caps.puerto = d.puerto_gdb;      // 0: reserva los pines, no escucha
            }
            const std::string nm = d.id.empty() ? std::string("dut") : d.id;
            const McuCaps* mc = caps_de[size_t(&d - &decls[0])];
            // AQUI es donde una cadena se convierte en un objeto. La factoria
            // despacha por FAMILIA -los once miembros del F405/407 son la misma
            // clase con distintos descriptores- y devuelve nullptr si esa
            // familia no esta registrada, sin montar otra cosa en su lugar.
            m.mcu = FabricaMcu::crea(*mc, nm.c_str(), caps, cab);
            if (!m.mcu)
                muere("mcu " + nm + ": el tipo '" + mc->nombre + "' es de la "
                      "familia '" + mc->familia + "', y no hay ningun modelo "
                      "registrado para ella.\n"
                      "Las familias que este programa sabe construir son: " +
                      familias_como_texto() + ".");
            // Y lo que el modelo sabe de si mismo y todavia no hace. Se dice
            // al montar, no al final: quien lee esto tiene que saberlo ANTES
            // de fiarse de lo que vea.
            for (const std::string& q : m.mcu->limitaciones())
                std::fprintf(stderr, "  [ojo] %s: %s\n", nm.c_str(), q.c_str());
            // Los nodos, con el prefijo del chip. Y además con el nombre
            // desnudo cuando solo hay uno: es lo que hace que las placas
            // escritas hasta hoy sigan valiendo sin migrarlas.
            m.mcu->registra_nodos(d.id, nodos);
            // En un <sistema> no: allí no hay nombres desnudos, todo es
            // `A/u0.PA5`, y el desnudo solo podría confundir.
            if (decls.size() == 1 && !d.id.empty() && !placa.es_sistema())
                m.mcu->registra_nodos(std::string(), nodos);
            // El stub de PINES se cuelga por fuera de PA14/PA13, como un
            // ST-LINK. El de DAP lo ha creado ya el propio núcleo.
            if (d.depuracion == "pines" && d.puerto_gdb) {
                const std::string snm = "gdb_" + (d.id.empty() ? "dut" : d.id);
                m.stub = new GdbStub(snm.c_str(),
                                     m.mcu->nodo_analogico(0, 14),  // PA14 SWCLK
                                     m.mcu->nodo_analogico(0, 13),  // PA13 SWDIO
                                     d.puerto_gdb, 2e6);
                m.stub->set_enabled(false);      // se abre al arrancar, no aquí
            }
            if (d.puerto_gdb) hay_stub = true;
            // Los cuatro drivers de alimentacion los registra el adaptador en
            // su constructor: son suyos, no de aqui.
            mcus.push_back(m);
        }

        // --- 5. Validar la declaración --------------------------------------
        for (const std::string& q : placa.valida(nodos)) {
            std::fprintf(stderr, "  [decl] %s\n", q.c_str());
            ++n_avisos;
        }
        if (n_avisos)
            muere(g_placa + ": " + std::to_string(n_avisos) +
                  " problemas de declaracion; no se monta");

        // --- 5 bis. Los puentes serie: a donde da cada uno -------------------
        // Tambien es declaracion, y va aqui por lo mismo que el resto: avisa
        // antes de construir nada. Un puerto repetido o cogido por un GDB no
        // falla al montar sino al abrir, en marcha, y ese mensaje sale donde
        // nadie mira.
        resuelve_puentes_serie();

        // --- 6 y 7. Construir las piezas y validar lo eléctrico -------------
        placa.construye(nodos);
        for (const std::string& q : placa.valida_electrica(nodos)) {
            std::fprintf(stderr, "  [elec] %s\n", q.c_str());
            avisos_placa.push_back(q);
            ++n_avisos;
        }
        // Un conflicto eléctrico NO detiene la simulación: puede ser una
        // decisión deliberada -un pin compartido entre dos montajes- y el que
        // manda es quien escribe la placa. Pero se dice, y se dice antes.
        if (placa.es_sistema()) {
            std::string l;
            for (const PlacaDeSistema& p : placa.placas())
                l += (l.empty() ? "" : ", ") + p.id + " (" + p.nombre + ")";
            std::printf("sistema '%s': %u placas: %s\n", g_nombre.c_str(),
                        unsigned(placa.placas().size()), l.c_str());
            for (const Netlist::Acople& a : placa.acoples())
                std::printf("  [acopla] %s%s\n", a.texto().c_str(),
                            a.espejo ? ", en espejo"
                                     : (a.conectores.size() > 2 ? ", en pila" : ""));
        }
        // En un sistema, el nombre ya se ha dicho arriba: aquí van los totales
        const std::string cab = placa.es_sistema() ? std::string("  en total")
                                                   : "placa '" + g_nombre + "'";
        if (mcus.empty())
            std::printf("%s: SIN MCU, %u componentes, %u nodos, %u avisos\n",
                        cab.c_str(), unsigned(placa.instancias().size()),
                        nodos.n_nodos(), n_avisos);
        else
            std::printf("%s: %u MCU(s), %u componentes, %u nodos, %u avisos\n",
                        cab.c_str(), unsigned(mcus.size()),
                        unsigned(placa.instancias().size()), nodos.n_nodos(), n_avisos);
        lee_dibujos();
        // Sin MCU, sin ventana y sin --valida, simular es ver pasar el tiempo
        // sin que nada lo mueva: se dice, por si era un <mcu> olvidado que el
        // nombre de los nodos no ha delatado.
        if (mcus.empty() && !g_gui_pedida && !g_solo_valida)
            std::printf("  [aviso] sin MCU y sin --gui, nadie toca las piezas: esto "
                        "solo tiene sentido con --gui o con --valida\n");
        for (const serie::Pieza& p : puentes) {
            const PuenteSerie* ps = placa.como<PuenteSerie>(p.id);
            if (!ps) continue;
            // Un puente que no escucha no es un puente: el alumno abriria su
            // terminal contra un puerto que es de otro. Se para aqui.
            if (!ps->canal_ok()) muere("  [serie] " + p.id + ": " + ps->error_canal());
            if (ps->por_red()) hay_puente_red = true;
            std::printf("  serie %s: %s\n", p.id.c_str(), ps->describir().c_str());
        }
        // Con los ms dichos la simulacion acaba igual (abajo), y el ritmo del
        // terminal da lo mismo: el aviso es para quien se queda mirando
        if (hay_puente_red && g_tiempo_real <= 0.0 && !g_solo_valida && !g_ms_dado) {
            avisos_placa.push_back(
                "hay un puente serie por TCP y no se ha pedido --tiempo-real: el "
                "terminal vera el ritmo de la simulacion, no el de la placa");
            std::fprintf(stderr,
                "  [serie] AVISO: hay un puente serie por TCP y no se ha pedido\n"
                "          --tiempo-real. El terminal vera el ritmo de la\n"
                "          simulacion, no el de la placa: un printf por segundo\n"
                "          puede llegar cien veces por segundo, y el procesador\n"
                "          va a tope. Con --tiempo-real va como en la placa.\n");
        }
        for (const McuMontado& m : mcus) {
            if (!m.decl.puerto_gdb && m.decl.firmware.empty() && m.decl.id.empty())
                continue;                    // el caso de siempre: no dice nada
            std::string linea = "  mcu ";
            linea += m.decl.id.empty() ? "(unico)" : m.decl.id;
            linea += ": ";
            linea += m.decl.firmware.empty() ? "sin firmware" : m.decl.firmware;
            if (m.decl.puerto_gdb) {
                linea += ", gdb por " + m.decl.depuracion + " en el puerto " +
                         std::to_string(m.decl.puerto_gdb);
            }
            std::printf("%s\n", linea.c_str());
        }
        SC_THREAD(run);
    }

    ~Sim() {
        placa.libera();                      // las piezas, antes que los nodos
        // El chip lo borra su ADAPTADOR, que es quien lo creo. Se borra por la
        // interfaz, y el destructor virtual hace el resto.
        for (McuMontado& m : mcus) { delete m.stub; delete m.mcu; }
    }

    // -----------------------------------------------------------------------
    // Los puentes serie de la placa, con `--serie` encima.
    //
    // Los puertos que ya tienen dueño son los de los stubs de GDB -uno por MCU
    // que lo pida- y el de la GUI si es esta maquina: `mcu-sim` es CLIENTE de
    // la GUI, pero la GUI escucha en ese puerto aqui mismo, y un puente que se
    // pusiera encima no podria abrirlo.
    // -----------------------------------------------------------------------
    std::vector<serie::Pieza> puentes;

    // EL DIBUJO DE CADA PLACA. Aquí no se interpreta: se busca, se mira por
    // encima -que exista, que parezca un SVG, que no sea enorme- y se guarda
    // para mandarlo. Lo que falla es un AVISO: una placa sin dibujo funciona
    // igual, y la ventana dibuja una genérica. Que no esté el que se llama
    // como la placa no es ni un aviso: es lo normal.
    void lee_dibujos() {
        const bool sis = placa.es_sistema();
        auto aviso = [&](const std::string& t) {
            std::fprintf(stderr, "  [dibujo] %s\n", t.c_str());
            avisos_placa.push_back(t);
        };
        auto base = [](const std::string& r) {
            const size_t b = r.find_last_of("/\\");
            return b == std::string::npos ? r : r.substr(b + 1);
        };
        // Las variantes de las piezas de una placa -el puente de un Jumper, el
        // color y el aspa de un Servo-,
        // por su id en ella: lo que su dibujo tiene que enseñar
        auto variantes = [&](const std::string& id) {
            std::map<std::string, std::string> v;
            for (const Instancia& i : placa.instancias()) {
                for (const auto& x : Netlist::variantes(i)) {
                    if (!sis) v[i.id + x.first] = x.second;
                    else if (placa.placa_de(i.id) == id)
                        v[i.id.substr(id.size() + 1) + x.first] = x.second;
                }
            }
            return v;
        };
        // Y sus rótulos: lo que dice el dibujo de cada puente serie, con el
        // destino que ha quedado -el del XML, o el de --serie-
        auto rotulos = [&](const std::string& id) {
            std::map<std::string, std::string> r;
            for (const Instancia& i : placa.instancias()) {
                if (i.tipo != "PuenteSerie") continue;
                std::string local = i.id;
                if (sis) {
                    if (placa.placa_de(i.id) != id) continue;
                    local = i.id.substr(id.size() + 1);
                }
                const auto h = i.params.find("host");
                const serie::Destino d = h == i.params.end() ? serie::por_omision()
                                                             : serie::parsea(h->second);
                r[local + "#destino"] = serie::describe(d);
                r[local + "#host"] = serie::como_texto(d);
            }
            return r;
        };
        auto mira = [&](const std::string& id, const Ilustracion& il) {
            const std::string quien = sis ? "dibujo " + id : std::string("dibujo");
            if (il.ruta.empty()) {
                if (sis) std::printf("  %s: ninguno\n", quien.c_str());
                return;
            }
            // El mismo fichero con las mismas variantes es el mismo dibujo; con
            // otras -dos adaptadores con el jumper distinto- es otro
            const std::map<std::string, std::string> var = variantes(id), rot = rotulos(id);
            for (DibujoLeido& d : dibujos)
                if (d.ruta == il.ruta && d.variantes == var && d.rotulos == rot &&
                    d.giro == il.giro) {
                    std::printf("  %s: %s, el mismo que %s\n", quien.c_str(),
                                base(il.ruta).c_str(), d.placas[0].c_str());
                    d.placas.push_back(id);
                    return;
                }
            std::ifstream f(il.ruta, std::ios::binary);
            if (!f) {
                if (!il.declarada.empty())
                    aviso((sis ? "placa " + id + ": " : std::string()) +
                          "no se encuentra su dibujo " + il.ruta);
                else if (sis)
                    std::printf("  %s: ninguno\n", quien.c_str());
                return;
            }
            std::string svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (svg.size() > DIBUJO_MAX) {
                aviso((sis ? "placa " + id + ": " : std::string()) + "el dibujo " + il.ruta +
                      " pesa " + std::to_string(svg.size() / 1024) + " kB, y el maximo es " +
                      std::to_string(DIBUJO_MAX / 1024) + ": no se manda");
                return;
            }
            size_t k = 0;
            if (svg.compare(0, 3, "\xEF\xBB\xBF") == 0) k = 3;      // la marca UTF-8
            while (k < svg.size() && (svg[k] == ' ' || svg[k] == '\t' || svg[k] == '\r' ||
                                      svg[k] == '\n'))
                ++k;
            const std::string ini = svg.substr(k, 9);
            if (ini.compare(0, 4, "<svg") != 0 && ini.compare(0, 5, "<?xml") != 0 &&
                ini.compare(0, 4, "<!--") != 0 && ini.compare(0, 9, "<!DOCTYPE") != 0) {
                aviso((sis ? "placa " + id + ": " : std::string()) + il.ruta +
                      " no parece un SVG (no empieza por <svg ni por <?xml): no se manda");
                return;
            }
            std::string con;
            for (const auto& kv : var) con += ", " + kv.first + " " + kv.second;
            if (il.giro) con += ", girado " + std::to_string(il.giro);
            std::printf("  %s: %s (%u kB%s)\n", quien.c_str(), base(il.ruta).c_str(),
                        unsigned((svg.size() + 1023) / 1024), con.c_str());
            svg = pon_rotulos(quita_variantes(svg, var), rot);
            if (il.giro) {
                std::string e;
                svg = gira_svg(svg, il.giro, &e);
                if (!e.empty())
                    aviso((sis ? "placa " + id + ": " : std::string()) + "el dibujo " +
                          il.ruta + " no se puede girar (" + e + "): se manda como esta");
            }
            dibujos.push_back({{id}, il.ruta, std::move(svg), var, rot, il.giro});
        };
        if (sis)
            for (const PlacaDeSistema& p : placa.placas()) mira(p.id, p.ilustracion);
        else
            mira(std::string(), placa.ilustracion());
        // La disposición para la ventana (plan §38 de mcu-sim-gui), si la hay:
        // aquí no se usa, pero se dice que se ha leído
        {
            auto num = [](double v) {
                char b[32];
                std::snprintf(b, sizeof b, "%g", v);
                return std::string(b);
            };
            std::vector<std::string> l;
            if (placa.lienzo().fijo)
                l.push_back("lienzo " + num(placa.lienzo().ancho) + " x " +
                            num(placa.lienzo().alto) + " mm");
            auto una = [&](const std::string& id, const Ilustracion& il) {
                std::string t;
                if (il.colocada) t += " en (" + num(il.x_mm) + ", " + num(il.y_mm) + ") mm";
                if (il.escala != 1.0) t += " al " + num(il.escala * 100) + " %";
                if (!t.empty()) l.push_back((id.empty() ? std::string("la placa") : id) + t);
            };
            if (sis)
                for (const PlacaDeSistema& p : placa.placas()) una(p.id, p.ilustracion);
            else
                una(std::string(), placa.ilustracion());
            if (!placa.rutas().empty())
                l.push_back(std::to_string(placa.rutas().size()) + " linea(s) en tramos rectos");
            if (!l.empty()) {
                std::string t;
                for (const std::string& x : l) t += (t.empty() ? "" : "; ") + x;
                std::printf("  ventana: %s\n", t.c_str());
            }
        }
    }

    void resuelve_puentes_serie() {
        std::vector<serie::Pieza> decl;
        for (const Instancia& i : placa.instancias()) {
            if (i.tipo != "PuenteSerie") continue;
            const auto h = i.params.find("host");
            decl.push_back({ i.id, h == i.params.end() ? serie::por_omision()
                                                       : serie::parsea(h->second) });
        }
        std::vector<serie::Ocupado> ocupados;
        for (const McuMontado& m : mcus)
            if (m.decl.puerto_gdb)
                ocupados.push_back({ m.decl.puerto_gdb,
                                     m.decl.id.empty() ? std::string("el GDB")
                                                       : "el GDB de " + m.decl.id });
        if (g_gui_pedida && gui::es_bucle_local(g_gui.host))
            ocupados.push_back({ g_gui.puerto, "mcu-sim-gui" });

        const serie::Resultado r = serie::resuelve(decl, g_serie, ocupados);
        std::vector<std::string> err = r.errores;
        // Lo que manda la linea de ordenes se ESCRIBE en la instancia, para que
        // la validacion y el creador vean lo mismo que se ha decidido aqui.
        if (err.empty())
            for (const serie::Pieza& p : r.piezas)
                if (Instancia* i = placa.busca(p.id))
                    i->params["host"] = serie::como_texto(p.destino);
        // Y el resto de atributos de cada puente: baudios, formato, flujo,
        // guion y los terminales. Tambien antes de construir.
        for (const Instancia& i : placa.instancias())
            if (i.tipo == "PuenteSerie")
                for (const std::string& q : valida_puente_serie(i)) err.push_back(q);
        for (const std::string& q : err)
            std::fprintf(stderr, "  [serie] %s\n", q.c_str());
        if (!err.empty())
            muere(g_placa + ": " + std::to_string(err.size()) +
                  " problemas con los puentes serie; no se monta");
        for (const std::string& q : r.notas) std::printf("  [serie] %s\n", q.c_str());
        puentes = r.piezas;
    }

    // -----------------------------------------------------------------------
    // La línea de órdenes sobre la lista declarada.
    //
    //   con 0 o 1 MCU   los argumentos globales siguen sirviendo y MANDAN sobre
    //                   el XML: el fichero es la configuración y el argumento
    //                   es la intención inmediata. Así se puede depurar la
    //                   placa de otro sin editarla;
    //   con 2 o más     un argumento global ya no designa a nadie. Se rechaza
    //                   nombrando los MCUs, en vez de elegir uno por su cuenta.
    // -----------------------------------------------------------------------
    // -----------------------------------------------------------------------
    // UNA PLACA SIN MCU. Es legitima -un pulsador, una fuente y un LED; validar
    // una placa antes de tener firmware; probar la ventana sin chip-, pero
    // tambien es lo que sale de olvidarse el <mcu>, y entonces `<nodo id=PD12>`
    // seria un cable suelto en silencio. Lo que lo distingue es el nombre: sin
    // MCU no hay pines, asi que un nodo que se llama como un pin es un error
    // que dice que falta. Y lo que solo tiene sentido con un MCU -un firmware,
    // un stub de GDB, sus relojes- tambien.
    // -----------------------------------------------------------------------
    static bool parece_pin_de_mcu(const std::string& nodo) {
        std::string pref;
        unsigned p = 0, i = 0;
        if (pad_desde_nombre(nodo, pref, p, i)) return true;
        // Con mayúsculas exactas, como los registra el MCU: `vdd` es un hilo
        // de la placa y no la patilla del chip.
        std::string a;
        return alim_desde_nombre(nodo, pref, a);
    }
    void comprueba_sin_mcu() {
        std::vector<std::string> pins;
        auto mira = [&](const std::string& nodo) {
            if (!parece_pin_de_mcu(nodo)) return;
            for (const std::string& q : pins) if (q == nodo) return;
            pins.push_back(nodo);
        };
        for (const std::string& n : placa.externos()) mira(n);
        for (const Instancia& i : placa.instancias())
            for (const Conexion& c : i.pines) mira(c.nodo);
        if (!pins.empty()) {
            std::string l;
            for (const std::string& q : pins) { if (!l.empty()) l += ", "; l += q; }
            muere("la placa no lleva MCU -ni <mcu> en el XML ni --mcu-, y usa "
                  "nodos con nombre de pin de MCU: " + l + ".\n"
                  "Si la placa lleva un MCU, declaralo:\n"
                  "  <mcu tipo=\"STM32F407VG\" id=\"u0\"/>     (en el XML)\n"
                  "  --mcu STM32F407VG                     (en la linea de ordenes)\n"
                  "Si de verdad no lleva ninguno, esos nodos son cables de la "
                  "placa: dales otro nombre.");
        }
        auto no = [](const std::string& que) {
            muere("la placa no lleva MCU -ni <mcu> en el XML ni --mcu-, asi que " + que +
                  " no tiene sentido. Si lleva uno, declaralo: <mcu tipo=\"...\" "
                  "id=\"u0\"/> en el XML, o --mcu TIPO");
        };
        if (!g_img.empty())        no("un firmware (" + g_img + ") no tiene donde cargarse y");
        if (!g_gdb_modo.empty())   no("un stub de GDB (--gdb, --gdb-dap)");
        if (g_puerto_dado)         no("--port");
        if (g_traza_gdb)           no("--traza-gdb");
        if (g_ondas)               no("--ondas, que son los relojes del MCU,");
    }

    void aplica_linea_de_ordenes(std::vector<DeclMcu>& decls) {
        if (decls.empty()) return;           // sin MCU: ya lo ha mirado comprueba_sin_mcu
        const bool global = !g_img.empty() || !g_gdb_modo.empty() || g_puerto_dado;
        if (decls.size() > 1) {
            if (!global) return;
            std::string ids;
            for (const DeclMcu& m : decls) { if (!ids.empty()) ids += ", "; ids += m.id; }
            if (placa.es_sistema())
                muere("el sistema lleva " + std::to_string(decls.size()) + " MCUs (" +
                      ids + "), asi que un firmware o un puerto sueltos en la linea "
                      "de ordenes no dicen a cual.\nDilo en el <sistema>, uno por "
                      "chip: <mcu ref=\"" + decls.front().id + "\" firmware=\"...\" "
                      "depuracion=\"pines|dap\" puerto_gdb=\"...\"/>");
            muere("la placa lleva " + std::to_string(decls.size()) + " MCUs (" +
                  ids + "), asi que un firmware o un puerto sueltos en la linea "
                  "de ordenes no dicen a cual.\nPonlo en cada <mcu>: "
                  "firmware=\"...\" depuracion=\"pines|dap\" puerto_gdb=\"...\"");
        }
        DeclMcu& m = decls.front();
        if (!g_img.empty()) m.firmware = g_img;
        if (!g_gdb_modo.empty()) {
            m.depuracion = g_gdb_modo;
            if (!m.puerto_gdb) m.puerto_gdb = 3333;      // el de siempre
        }
        if (g_puerto_dado) {
            m.puerto_gdb = g_gdb_puerto;
            if (m.depuracion.empty()) m.depuracion = "pines";
        }
    }

    // -----------------------------------------------------------------------
    // ESPERAR, con freno opcional de tiempo real.
    //
    // Sin `--tiempo-real` esto es un `wait()` y ya está: el modelo corre todo lo
    // deprisa que puede, que es lo que quiere una prueba. Con él, el proceso se
    // DUERME hasta que el reloj de pared alcanza al simulado, y eso arregla dos
    // cosas de golpe:
    //
    //   * un LED que parpadea a 1 Hz parpadea a 1 Hz, y no doscientas veces por
    //     segundo, así que se puede MIRAR. Para un simulador didáctico eso no es
    //     un lujo: es la diferencia entre ver el sistema y ver un borrón;
    //   * deja de comerse un núcleo entero esperando —medido: 98,6 % de CPU—,
    //     porque dormir es dormir.
    //
    // Dormir dentro de un proceso de SystemC detiene TODA la simulación, que es
    // justo lo que aquí se quiere: las corrutinas comparten el hilo del sistema.
    // El freno solo frena; si el modelo va MÁS LENTO que el tiempo real, no hay
    // nada que hacer y se sigue sin dormir, sin acumular deuda.
    // -----------------------------------------------------------------------
    //
    // El freno se mide contra un ANCLA -un instante de pared y uno simulado-
    // y no rodaja a rodaja. Medido rodaja a rodaja, lo que cada `sleep_for` se
    // pasaba se iba sumando: un 10 % de retraso a los pocos segundos con
    // rodajas de 1 ms. Con el ancla no se suma nada. Y si el modelo va por
    // DETRAS mas de 50 ms, el ancla se mueve al presente: no se acumula una
    // deuda que luego se pagaria corriendo sin freno.
    sc_time  ancla_sim_ = SC_ZERO_TIME;
    std::chrono::steady_clock::time_point ancla_pared_;
    bool     anclado_ = false;

    void espera(const sc_time& d) {
        if (g_tiempo_real <= 0.0) { wait(d); return; }
        if (!anclado_) {
            ancla_pared_ = std::chrono::steady_clock::now();
            ancla_sim_   = sc_time_stamp();
            anclado_     = true;
        }
        wait(d);
        const double debe = (sc_time_stamp() - ancla_sim_).to_seconds() / g_tiempo_real;
        const auto   ahora = std::chrono::steady_clock::now();
        const double lleva = std::chrono::duration<double>(ahora - ancla_pared_).count();
        if (debe > lleva)
            std::this_thread::sleep_for(std::chrono::duration<double>(debe - lleva));
        else if (lleva - debe > 0.05) {
            ancla_pared_ = ahora;
            ancla_sim_   = sc_time_stamp();
        }
    }

    // -----------------------------------------------------------------------
    // `--espera-terminal`: con el tiempo simulado en CERO, antes de dar
    // corriente al MCU, espera en tiempo de pared a que cada puente serie por
    // red tenga su terminal. Es lo que en una Nucleo se hace abriendo el
    // terminal y pulsando RESET: sin ello, lo que el firmware imprime al
    // arrancar sale cuando todavia no hay nadie, y se descarta (D-6).
    //
    // Y no basta con que se conecte: un terminal, al abrir, CONFIGURA -
    // negocia Telnet, manda baudios, formato, lineas, y el de pySerial acaba
    // PURGANDO lo recibido-. Lo que el MCU mandase en medio se perderia igual,
    // o saldria antes de BINARY con la regla del NVT. Asi que se espera a que
    // el cliente lleve 300 ms callado (con RFC 2217, ademas, con la
    // negociacion terminada si la empezo, y como mucho dos segundos). Mientras
    // tanto no corre ningun proceso de SystemC -comparten un hilo-, asi que
    // el sondeo del canal, que en marcha hace la pieza, es cosa de aqui.
    // -----------------------------------------------------------------------
    void espera_terminales() {
        std::vector<PuenteSerie*> red;
        for (const serie::Pieza& p : puentes)
            if (PuenteSerie* ps = placa.como<PuenteSerie>(p.id))
                if (ps->por_red()) red.push_back(ps);
        if (red.empty()) {
            std::fprintf(stderr, "  [serie] --espera-terminal: esta placa no tiene "
                         "puentes serie por red; no hay nada que esperar\n");
            return;
        }
        for (PuenteSerie* ps : red)
            std::printf("esperando a un terminal en %s (Ctrl-C para salir)\n",
                        ps->canal().describir().c_str());
        std::fflush(stdout);
        using reloj = std::chrono::steady_clock;
        const auto segundos = [](reloj::time_point t) {
            return std::chrono::duration<double>(reloj::now() - t).count();
        };
        const std::size_t n = red.size();
        std::vector<reloj::time_point> desde(n), ultimo(n);
        std::vector<uint64_t> bytes(n, 0);
        std::vector<bool> tenia(n, false);
        for (;;) {
            bool todos = true;
            for (std::size_t i = 0; i < n; ++i) {
                CanalTcp* c = red[i]->tcp();
                c->sondear();
                const bool hay = c->conectado();
                if (hay && !tenia[i]) { desde[i] = ultimo[i] = reloj::now(); bytes[i] = 0; }
                tenia[i] = hay;
                if (!hay) { todos = false; continue; }
                if (c->recibidos_conexion() != bytes[i]) {
                    bytes[i] = c->recibidos_conexion();
                    ultimo[i] = reloj::now();
                }
                bool listo = segundos(ultimo[i]) > 0.3;
                if (CanalRfc2217* r = red[i]->rfc2217()) {
                    const auto& ng = r->negociador();
                    if (ng.despierto() && !(ng.com_port() && ng.binario_salida()))
                        listo = false;
                }
                todos = todos && (listo || segundos(desde[i]) > 2.0);
            }
            if (todos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("terminal conectado: arranca el MCU\n");
        std::fflush(stdout);
    }

    void run() {
        if (g_solo_valida) { sc_stop(); return; }
        if (g_espera_terminal) espera_terminales();
        // La onda cuadrada de los relojes internos se apaga por omision. No es
        // una simplificacion del modelo: es un interruptor que ya existia. Con
        // ella encendida, los flancos de HCLK son el noventa y tantos por
        // ciento de los eventos de la simulacion, y solo hacen falta cuando lo
        // que se mira es el propio arbol de reloj. `--ondas` la devuelve.
        for (McuMontado& m : mcus) {
            m.mcu->set_ondas_reloj(g_ondas);
            // Arranque eléctrico: el mismo de siempre, porque el MCU no sabe
            // que su placa viene de un fichero.
            m.mcu->alimenta(false);      // todo a cero, con NRST abajo
        }
        wait(10, SC_US);

        for (McuMontado& m : mcus) {
            const char* quien = m.decl.id.empty() ? "" : m.decl.id.c_str();
            // Todo por la interfaz: DONDE va el firmware y donde esta la SRAM
            // lo sabe el chip, y con dos familias montadas eso ya no es un
            // detalle -son dos mapas de memoria distintos-.
            if (!m.decl.firmware.empty()) {
                if (!m.mcu->carga_firmware(m.decl.firmware))
                    muere("no se puede cargar " + m.decl.firmware);
                std::printf("firmware%s%s: %s\n",
                            *quien ? " de " : "", quien, m.decl.firmware.c_str());
            } else {
                m.mcu->aparca_en_wfe();
                std::printf("sin firmware%s%s: el nucleo se aparca en wfe\n",
                            *quien ? " en " : "", quien);
            }
        }

        for (McuMontado& m : mcus) {
            m.mcu->alimenta(true);
        }
        wait(100, SC_US);
        for (McuMontado& m : mcus) m.mcu->reset_pin(false);   // se suelta NRST

        // --- Los stubs de GDB, uno por MCU ----------------------------------
        // Se abren DESPUÉS del reset, como en el banco: un depurador que se
        // engancha antes de que el chip arranque ve un DAP que todavía no
        // responde.
        //
        // Un stub NO es un hilo del sistema operativo: es un SC_THREAD que
        // atiende un socket no bloqueante cada 100 us de tiempo SIMULADO
        // (common/gdb_rsp.h). La consecuencia practica es que su capacidad de
        // respuesta va atada a lo deprisa que avance el tiempo simulado: con el
        // modelo yendo mas rapido que el tiempo real es instantaneo, y con
        // --ondas se nota. Cada uno tiene su propio puerto TCP, eso si.
        if (hay_stub) {
            for (McuMontado& m : mcus) {
                if (!m.decl.puerto_gdb) continue;
                // La traza imprime CADA paquete RSP que llega. Es lo unico que
                // sirve cuando un IDE se conecta y se va sin decir por que: el
                // ultimo paquete antes del "cliente desconectado" es el que no
                // le gusto. La capacidad ya existia en el stub (set_verbose);
                // lo que faltaba era poder pedirla desde fuera.
                if (m.stub) {                                   // modo pines
                    m.stub->set_verbose(g_traza_gdb);
                    m.stub->set_enabled(true);
                } else if (m.mcu->tiene_gdb_interno()) {       // modo dap
                    m.mcu->gdb_interno(true, g_traza_gdb);
                }
            }
        }
        // Con un stub de GDB o con un puente serie por TCP hay alguien al otro
        // lado de un puerto, y la simulacion no puede terminar por su cuenta:
        // se sale con Ctrl-C. SALVO un puente serie con los ms DICHOS -`--ms=`
        // o el tercer argumento-: quien los da quiere que acabe, y la placa
        // que lleva un VCP de serie, como la Nucleo, se sigue pudiendo
        // simular un rato y mirar como acaba. El puente escucha mientras tanto.
        if (hay_puente_red && !hay_stub && g_ms_dado) {
            std::printf("  [serie] los puentes por red escuchan; la simulacion "
                        "acaba a los %g ms que se han pedido\n", g_ms);
        } else if (hay_stub || hay_puente_red) {
            std::printf("esperando a %s; la simulacion no se detiene sola "
                        "(Ctrl-C para salir)\n",
                        hay_stub && hay_puente_red ? "GDB y a los puentes serie"
                        : hay_stub ? "GDB" : "los puentes serie");
            // Y se vacia el buffer, que aqui no es manía. Este printf es el
            // ULTIMO antes de un bucle infinito: redirigida la salida a un
            // fichero deja de ser linea a linea y pasa a bloques, y como de
            // este modo solo se sale con Ctrl-C, sin este fflush el mensaje que
            // explica como salir es justo el que se pierde.
            std::fflush(stdout);
            // Rodajas de 1 ms: con el freno puesto, la rodaja es tambien lo
            // que el proceso duerme de una vez, y dormido no atiende el socket.
            // Un milisegundo de latencia ante GDB no se nota; diez, si.
            for (;;) espera(sc_time(1, SC_MS));
        }

        h0_ = std::chrono::steady_clock::now();
        t0_ = sc_time_stamp();
        // Sin ventana de tiempo (con --gui, cuando nadie la dio): hasta que la
        // ventana diga T_PARA, en rodajas de 1 ms como el bucle de GDB.
        if (g_sin_fin) for (;;) espera(sc_time(1, SC_MS));
        // CON --tiempo-real, EN RODAJAS DE 1 ms, como el bucle de arriba. Una
        // sola espera de toda la ventana simulaba los segundos de golpe, en
        // centesimas, y DESPUES dormia lo que faltaba: el total cuadraba con el
        // reloj de pared, pero el LED parpadeaba a toda velocidad y luego nada.
        // En la consola no se notaba -solo se ve como acaba-; con una ventana
        // mirando (mcu-sim-gui, fase 4) es lo primero que se ve.
        if (g_tiempo_real > 0.0) {
            const sc_time rodaja(1, SC_MS);
            sc_time quedan(g_ms, SC_MS);
            while (quedan > SC_ZERO_TIME) {
                const sc_time d = quedan < rodaja ? quedan : rodaja;
                espera(d);
                quedan -= d;
            }
        } else {
            espera(sc_time(g_ms, SC_MS));
        }
        informe();
        sc_stop();
    }

    // Lo que se dice al acabar: cuanto se ha simulado y como acaba lo que se ve
    // desde fuera. Lo llama `run()` al agotar la ventana y, desde la fase 6 de
    // mcu-sim-gui, `sc_main` cuando la ventana dice T_PARA en marcha.
    std::chrono::steady_clock::time_point h0_ = std::chrono::steady_clock::now();
    sc_time t0_ = SC_ZERO_TIME;

    void informe() {
        const double seg =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - h0_).count();
        std::printf("simulados %.3f ms en %.3f s de anfitrion (%llu deltas)\n",
                    (sc_time_stamp() - t0_).to_seconds() * 1e3, seg,
                    (unsigned long long)sc_delta_count());
        // Y lo que se ve desde fuera. Un LED es el instrumento de medida mas
        // antiguo de este oficio, asi que se dice como acabo cada uno: su
        // tension de pin y la corriente que le pasa, en float, como el modelo
        // las resuelve de verdad.
        for (const Instancia& i : placa.instancias()) {
            if (i.tipo != "Led") continue;
            const Led* l = placa.como<Led>(i.id);
            if (!l) continue;
            // Con las dos patillas, entre que nodos esta y la tension entre
            // ellos, que es la que lo hace lucir; o que un extremo esta al aire
            if (l->dos_patillas()) {
                char medida[64];
                if (l->al_aire())
                    std::snprintf(medida, sizeof medida, "un extremo al aire");
                else {
                    const double v = std::fabs(l->tension()) < 0.005 ? 0.0 : l->tension();
                    std::snprintf(medida, sizeof medida, "%.2f V, %.2f mA", v,
                                  l->current() * 1e3);
                }
                std::printf("  LED %s entre %s y %s: %s  (%s)\n", i.id.c_str(),
                            i.nodo_de("anodo").c_str(), i.nodo_de("catodo").c_str(),
                            l->on() ? "encendido" : "apagado", medida);
                continue;
            }
            // La patilla del pin se llama `anodo` o `catodo` segun el montaje.
            const std::string& nd = i.nodo_de("anodo").empty()
                                  ? i.nodo_de("catodo") : i.nodo_de("anodo");
            std::printf("  LED %s en %s: %s  (%.2f V, %.2f mA)\n", i.id.c_str(),
                        nd.c_str(), l->on() ? "encendido" : "apagado",
                        double(l->pin_voltage()), l->current() * 1e3);
        }
        // Las fuentes y las masas: cuanto entregan -o reciben- y si han tenido
        // que limitar. Es lo que se mira en una placa sin MCU.
        for (const Instancia& i : placa.instancias()) {
            if (i.tipo != "Fuente" && i.tipo != "Gnd") continue;
            const FuenteTension* f = placa.como<FuenteTension>(i.id);
            if (!f) continue;
            char lim[48] = "sin limite";
            if (f->limite_ma() > 0.0)
                std::snprintf(lim, sizeof lim, "limite %.3g mA", f->limite_ma());
            std::printf("  %s %s en %s: %.2f mA (%s)%s", i.tipo.c_str(), i.id.c_str(),
                        i.nodo_de("pin").c_str(), f->corriente() * 1e3, lim,
                        f->sobrecorriente() ? "  SOBRECORRIENTE" : "");
            if (f->episodios())
                std::printf(", %u episodio(s) de sobrecorriente", f->episodios());
            std::printf("\n");
        }
        // Las pantallas: que ensenan y como lo tienen puesto. Lo que se ve de
        // verdad lo pinta la ventana; aqui, lo que hay que saber si no se ve
        // nada -dormida, apagada, sin luz-.
        for (const Instancia& i : placa.instancias()) {
            if (i.tipo != "Tft128x160") continue;
            const Tft128x160* t = placa.como<Tft128x160>(i.id);
            if (!t) continue;
            const char* estado = !t->alimentada() ? "sin tension"
                               : t->en_reset()    ? "en reset (blanca)"
                               : t->dormida()     ? "dormida (blanca)"
                               : !t->encendida()  ? "apagada con DISPOFF (blanca)"
                                                  : "ensena su memoria";
            std::printf("  TFT %s: %s; %u bits por pixel, MADCTL 0x%02X; %llu ordenes, "
                        "%llu pixeles; luz %.1f mA%s\n", i.id.c_str(), estado,
                        t->bits_por_pixel(), unsigned(t->madctl()),
                        (unsigned long long)t->ordenes(), (unsigned long long)t->pixeles(),
                        t->luz_ma(), t->luz_ma() < 0.05 ? " (a oscuras: negra)" : "");
            if (t->ordenes_perdidas())
                std::printf("  TFT %s: %llu ordenes perdidas por llegar antes de 5 ms tras "
                            "el reset\n", i.id.c_str(),
                            (unsigned long long)t->ordenes_perdidas());
        }
        // Los servos: donde ha quedado el eje, hacia donde iba, la senal que le
        // llega y donde empezo -que, aleatorio, cambia de una vez a otra-
        for (const Instancia& i : placa.instancias()) {
            if (i.tipo != "Servo") continue;
            const Servo* s = placa.como<Servo>(i.id);
            if (!s) continue;
            const char* estado = s->bloqueado()  ? "bloqueado"
                               : s->moviendose() ? "moviendose"
                               : "quieto";
            char senal[96];
            if (!s->pulsos())
                std::snprintf(senal, sizeof senal, "sin un pulso");
            else
                std::snprintf(senal, sizeof senal, "pulso %.0f us cada %.1f ms%s", s->pulso_us(),
                              s->periodo_ms(), s->con_senal() ? "" : " (ya sin senal)");
            std::printf("  SERVO %s: %+.1f grados (objetivo %+.1f), %s; %s; %.2f V, %.0f mA; "
                        "empezo en %+.1f grados\n", i.id.c_str(), s->angulo(), s->objetivo(),
                        senal, estado, s->tension(), s->corriente_ma(), s->inicial());
        }
        // Y lo que los puentes serie tengan a medias, que tambien se ve desde
        // fuera: es la basura de unos baudios equivocados.
        for (const serie::Pieza& p : puentes)
            if (PuenteSerie* ps = placa.como<PuenteSerie>(p.id)) {
                ps->vacia_muestra();
                std::printf("  serie %s: %llu bytes del MCU (%llu con error de "
                            "trama, %llu de paridad), %llu hacia el MCU\n",
                            p.id.c_str(), (unsigned long long)ps->bytes_desde_mcu(),
                            (unsigned long long)ps->errores_trama(),
                            (unsigned long long)ps->errores_paridad(),
                            (unsigned long long)ps->bytes_hacia_mcu());
            }
    }

    // El freno de tiempo real se echa el ancla de nuevo: lo usa el enlace al
    // salir de una pausa, para que el freno no crea que va con retraso.
    void reancla() { anclado_ = false; }
};

// ---------------------------------------------------------------------------
// EL SALUDO CON LA VENTANA (fase 3 del plan de mcu-sim-gui)
//
// Va DESPUES de construir la placa -T_PLACA y T_CATALOGO hablan de piezas que
// tienen que existir- y ANTES de `sc_start()`, que es lo que hace verdadero el
// «la simulacion no empieza hasta que la GUI lo diga»: no es que empiece y se
// quede quieta, es que el nucleo de SystemC todavia no ha dado un paso. La
// espera de T_ARRANCA es un `select` que duerme, asi que no gasta CPU, y no
// tiene plazo.
//
// Devuelve -1 si hay que simular, o el codigo de salida si no. Con `--valida`
// tambien devuelve -1: se manda la placa, se termina con T_FIN y se deja que
// `run()` haga lo de siempre con `--valida`, que es parar sin simular.
//
// El contenido de T_ARRANCA -ritmo, factor, ventana- se aplica desde la fase 6
// (`aplica_arranca`, mas abajo).
// Lo que llegue antes de T_ARRANCA -la ultima T_SUSCRIBE y todos los
// T_ORDENES- se aplica antes de `sc_start()` (fases 4 y 5).
// ---------------------------------------------------------------------------
// EL CONTENIDO DE T_ARRANCA (fase 6): el ritmo y la ventana de tiempo.
//
//   * RIT_REAL con su factor es `--tiempo-real=F`; RIT_LIBRE, sin freno; y
//     RIT_DEMANDA, sin freno y EN PAUSA desde t = 0: solo avanza con T_PASO.
//     Con `--gui` el ritmo lo dice la ventana, que es quien lo esta mirando;
//     el `--tiempo-real` de la linea de ordenes deja de mandar, y se dice;
//   * `ventana_ns` > 0 es la ventana de tiempo; 0 es la de mcu-sim: la de su
//     linea de ordenes o, si no se dio ninguna, SIN FIN, hasta que la ventana
//     diga T_PARA. Antes de la fase 6 no habia T_PARA en marcha y por eso
//     habia que tener siempre un final.
static const char* nombre_ritmo(uint32_t r) {
    switch (r) {
        case mcusim::proto::RIT_REAL:    return "tiempo real";
        case mcusim::proto::RIT_LIBRE:   return "libre";
        case mcusim::proto::RIT_DEMANDA: return "a demanda";
    }
    return "?";
}

static void aplica_arranca(Sim& s, const mcusim::proto::Arranca& a) {
    using namespace mcusim::proto;
    const double antes = g_tiempo_real;
    uint32_t ritmo = a.ritmo;
    if (ritmo > RIT_DEMANDA) {
        std::printf("gui: ritmo %u desconocido; se usa tiempo real\n", unsigned(ritmo));
        ritmo = RIT_REAL;
    }
    if (ritmo == RIT_REAL) {
        // Un factor que no es un numero positivo no frena nada con sentido
        g_tiempo_real = (a.factor > 0.0f && a.factor < 1e6f) ? double(a.factor) : 1.0;
    } else {
        g_tiempo_real = 0.0;
    }
    s.enlace.ritmo(ritmo);
    s.enlace.al_seguir([&s] { s.reancla(); });
    if (a.ventana_ns > 0) {
        g_ms = double(a.ventana_ns) / 1e6;
    } else if (!g_ms_dado) {
        g_sin_fin = true;
        s.enlace.para_al_perderse(true);
    }
    std::printf("gui: ritmo %s", nombre_ritmo(ritmo));
    if (ritmo == RIT_REAL) std::printf(" (x%g)", g_tiempo_real);
    if (g_sin_fin) std::printf(", sin fin: hasta que la ventana diga parar\n");
    else           std::printf(", ventana de %.3f ms\n", g_ms);
    if (antes > 0.0 && g_tiempo_real != antes)
        std::printf("gui: el ritmo lo dice la ventana; --tiempo-real no se aplica\n");
    std::fflush(stdout);
}

static std::string texto_hola(const Sim& s, int argc, char** argv) {
    std::string mcus, fws, args;
    for (const McuMontado& m : s.mcus) {
        if (!mcus.empty()) mcus += ",";
        mcus += m.decl.tipo;
        if (!m.decl.firmware.empty()) {
            if (!fws.empty()) fws += ",";
            fws += m.decl.firmware;
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (i > 1) args += " ";
        args += argv[i];
    }
#if defined(_WIN32)
    const unsigned long pid = (unsigned long)::GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)::getpid();
#endif
    return "protocolo_max=" + std::to_string(mcusim::proto::VERSION_PROTO) + "\n" +
           "mcu_sim=" + VERSION_MCU_SIM + "\n" +
           "pid=" + std::to_string(pid) + "\n" +
           "placa=" + g_placa + "\n" +
           "mcu=" + mcus + "\n" +
           "firmware=" + fws + "\n" +
           "argumentos=" + args + "\n" +
           "modo=" + (g_solo_valida ? "valida" : "simula") + "\n";
}

static int saluda_gui(Sim& s, stm32::gui::ClienteGui& cli,
                      std::unique_ptr<stm32::gui::CanalGui>& canal,
                      int argc, char** argv) {
    using stm32::gui::ClienteGui;
    std::printf("gui: conectando con mcu-sim-gui en %s (protocolo v%u)\n",
                stm32::gui::como_texto(g_gui).c_str(),
                unsigned(mcusim::proto::VERSION_PROTO));
    std::fflush(stdout);
    if (!cli.conecta(g_gui)) {
        std::fprintf(stderr, "gui: %s\n", cli.error().c_str());
        return 2;
    }
    g_cliente = &cli;

    // El catalogo, sobre las piezas ya montadas. La frontera se activa con el
    // MISMO, para que los indices que la GUI usa en las suscripciones y en las ordenes
    // sean los suyos. Activarla no cuesta nada mientras no haya suscripciones
    // ni ordenes: sus dos procesos siguen esperando un evento.
    const stm32::gui::Catalogo cat(ExtPartBase::inventario());
    s.frontera.activa(cat);
    std::ostringstream placa, placa_v1;
    s.placa.volcar_xml(placa, g_nombre.c_str());
    // Un <sistema>, a una ventana de la versión 1 del protocolo: lo mismo con
    // la raíz <placa>, que es lo único que sabe leer (doc/protocolo.md §3).
    if (s.placa.es_sistema()) {
        s.placa.volcar_xml(placa_v1, g_nombre.c_str(), false);
        cli.placa_para_v1(placa_v1.str());
    }

    if (!g_solo_valida) {
        std::printf("gui: conectado; la simulacion espera a que la ventana diga "
                    "'arranca'\n");
        std::fflush(stdout);
    }
    mcusim::proto::Arranca arr{};
    cli.avisos_de_placa(s.avisos_placa);
    // Los dibujos: las cabeceras hasta la línea en blanco, y el SVG tal cual
    std::vector<std::string> ilus;
    for (const Sim::DibujoLeido& d : s.dibujos) {
        std::string pl;
        for (const std::string& p : d.placas) pl += (pl.empty() ? "" : " ") + p;
        const size_t b = d.ruta.find_last_of("/\\");
        ilus.push_back("placas=" + pl + "\nfichero=" +
                       (b == std::string::npos ? d.ruta : d.ruta.substr(b + 1)) + "\n\n" +
                       d.svg);
    }
    cli.ilustraciones(std::move(ilus));
    switch (cli.saluda(texto_hola(s, argc, argv), placa.str(), cat.xml(),
                       g_solo_valida, arr)) {
        case ClienteGui::Desenlace::Arranca:
            std::printf("gui: la ventana dice 'arranca'\n");
            std::fflush(stdout);
            // La conexion pasa al enlace, que la atiende en marcha. La
            // suscripcion que llego antes de arrancar se aplica AHORA, antes
            // de sc_start(): por eso la secuencia de instantaneas se repite al
            // picosegundo de una ejecucion a otra.
            canal = cli.entrega();
            s.enlace.activa(canal.get(), cli.emisor(), cli.lector());
            aplica_arranca(s, arr);
            if (cli.hay_suscripcion()) s.enlace.suscripcion_inicial(cli.suscripcion());
            // Y lo mismo las ordenes que llegaron antes de arrancar: su primera
            // orden es un instante ABSOLUTO, y encoladas antes de sc_start()
            // se aplican al picosegundo en todas las ejecuciones.
            for (const std::string& o : cli.ordenes_previas())
                s.enlace.ordenes(o, false);
            g_enlace  = &s.enlace;
            g_cliente = nullptr;
            return -1;
        case ClienteGui::Desenlace::Valida:
            cli.fin(mcusim::proto::M_VENTANA, 0, 0);
            g_cliente = nullptr;
            return -1;
        case ClienteGui::Desenlace::Para:
            std::printf("gui: la ventana pidio parar antes de arrancar; no se "
                        "simula nada\n");
            cli.fin(mcusim::proto::M_PARA, 0, 0);
            g_cliente = nullptr;
            return 0;
        case ClienteGui::Desenlace::Error:
        default:
            std::fprintf(stderr, "gui: %s\n", cli.error().c_str());
            g_cliente = nullptr;
            return 2;
    }
}

// ---------------------------------------------------------------------------
// `--argumentos` (fase 7 del plan de mcu-sim-gui): la lista de opciones de este
// programa, en XML, para que la ventana construya con ella su dialogo de
// lanzamiento. Es la tercera vez que el proyecto hace que un programa se
// describa a si mismo -el netlist, `--help COMPONENTE`, y ahora esto- y por la
// misma razon: si la ventana llevase su propia lista de opciones habria DOS
// sitios que la saben, y el segundo envejeceria. Asi, una opcion nueva aparece
// sola en el dialogo.
//
// Dentro de este fichero la lista sigue estando dos veces -aqui y en el bucle
// que lee `argv`-, y eso no se arregla sin reescribir el bucle. Lo que lo
// vigila es `make gui-argumentos` (`verif/gui/argumentos.py`): toda opcion que
// cite `--help` tiene que estar aqui, toda la de aqui en `--help`, y cada una,
// con su valor por omision, la tiene que aceptar este programa.
//
// El formato:
//
//   <argumentos programa="mcu-sim" version="...">
//     <posicional nombre="placa" tipo="fichero" filtro="*.xml" obligatorio="si" ayuda="..."/>
//     <opcion nombre="--ms" forma="valor" tipo="numero" unidad="ms" omision="100" ayuda="..."/>
//     <opcion nombre="--mcu" forma="valor" tipo="eleccion" ayuda="...">
//       <valor>STM32F405RG</valor> ...
//     </opcion>
//   </argumentos>
//
// `forma`: `bandera` (--x), `valor` (--x=V), `valor_opcional` (--x o --x=V)
// o `accion` (--help: hace otra cosa y sale; el dialogo no la ofrece). `tipo`
// de lo que va detras del igual: numero, entero, texto, eleccion o fichero.
// `grupo`: las del mismo grupo se excluyen. `repetible="si"`: puede ir varias
// veces. `con_gui="no"`: con `--gui` no tiene sentido -la pone la ventana, o la
// decide ella- y el dialogo no la ofrece.
// ---------------------------------------------------------------------------
struct OpcionCli {
    const char* nombre;
    const char* forma;
    const char* tipo;        // "" para banderas y acciones
    const char* omision;
    const char* unidad;
    const char* grupo;
    bool        repetible;
    bool        con_gui;
    const char* ejemplo;
    const char* ayuda;
};

static const OpcionCli OPCIONES[] = {
    // Sin omision a proposito: sin --ms son 100 ms, pero con --gui es SIN FIN,
    // y un dialogo que enseñase «100» mentiria justo en su caso.
    {"--ms", "valor", "numero", "", "ms", "", false, true, "2000",
     "tiempo simulado. Es lo mismo que el tercer argumento posicional. Sin el son "
     "100 ms; con --gui, no hay fin: se para desde la ventana"},
    {"--mcu", "valor", "eleccion", "", "", "", false, true, "STM32F407VG",
     "el MCU de la placa: si el XML no declara ninguno, lo pone; si declara uno, "
     "manda sobre su tipo. Sin <mcu> ni --mcu, la placa va sin MCU"},
    {"--valida", "bandera", "", "", "", "", false, true, "",
     "solo comprueba la placa, sin simular"},
    {"--ondas", "bandera", "", "", "", "", false, true, "",
     "con la onda cuadrada de los relojes internos"},
    {"--gdb", "bandera", "", "", "", "gdb", false, true, "",
     "stub de GDB por los pines SWD"},
    {"--gdb-dap", "bandera", "", "", "", "gdb", false, true, "",
     "stub de GDB contra el DAP"},
    {"--port", "valor", "entero", "3333", "", "", false, true, "3333",
     "puerto TCP del stub de GDB"},
    {"--traza-gdb", "bandera", "", "", "", "", false, true, "",
     "imprime cada paquete RSP que llega al stub"},
    {"--serie", "valor", "texto", "", "", "", true, true, "VCP=rfc2217:4000",
     "a donde da el PuenteSerie ID: ID=memoria, tcp:PUERTO, rfc2217:PUERTO, "
     "tcp-cliente:HOST:PUERTO o rfc2217-cliente:HOST:PUERTO"},
    {"--espera-terminal", "bandera", "", "", "", "", false, true, "",
     "no arranca el MCU hasta que haya un terminal en cada puente serie por red"},
    {"--tiempo-real", "valor_opcional", "numero", "1", "", "", false, false, "0.5",
     "frena la simulacion al reloj de pared. Con --gui el ritmo lo dice la ventana"},
    {"--gui", "valor_opcional", "texto", "localhost:3344", "", "", false, false,
     "localhost:3344", "habla con mcu-sim-gui. Con la ventana, la pone ella"},
    {"--argumentos", "accion", "", "", "", "", false, false, "",
     "esta lista de opciones, en XML, para mcu-sim-gui"},
    {"--help", "accion", "", "", "", "", false, false, "",
     "la ayuda; con COMPONENTE, la de ese componente"},
    {"--licencia", "accion", "", "", "", "", false, false, "",
     "licencia, donde esta el fuente y el software ajeno que lleva"},
};

static int vuelca_argumentos() {
    using stm32::xml_escapa;
    std::string x = "<argumentos programa=\"mcu-sim\" version=\"" +
                    xml_escapa(VERSION_MCU_SIM) + "\">\n";
    x += "  <posicional nombre=\"placa\" tipo=\"fichero\" filtro=\"*.xml\" "
         "obligatorio=\"si\" ayuda=\"la placa -MCUs, nodos y componentes- o el sistema "
         "de placas enchufadas, en XML\"/>\n";
    x += "  <posicional nombre=\"firmware\" tipo=\"fichero\" filtro=\"*.bin\" "
         "obligatorio=\"no\" ayuda=\"la imagen binaria que se carga en la Flash; "
         "sin ella el nucleo se aparca en wfe\"/>\n";
    for (const OpcionCli& o : OPCIONES) {
        const bool es_mcu = std::string(o.nombre) == "--mcu";
        x += std::string("  <opcion nombre=\"") + o.nombre + "\" forma=\"" + o.forma + "\"";
        if (*o.tipo)     x += std::string(" tipo=\"") + o.tipo + "\"";
        const std::string om = o.omision;
        if (!om.empty()) x += " omision=\"" + xml_escapa(om) + "\"";
        if (*o.unidad)   x += std::string(" unidad=\"") + o.unidad + "\"";
        if (*o.grupo)    x += std::string(" grupo=\"") + o.grupo + "\"";
        if (o.repetible) x += " repetible=\"si\"";
        if (!o.con_gui)  x += " con_gui=\"no\"";
        if (*o.ejemplo)  x += " ejemplo=\"" + xml_escapa(o.ejemplo) + "\"";
        x += " ayuda=\"" + xml_escapa(o.ayuda) + "\"";
        if (!es_mcu) { x += "/>\n"; continue; }
        x += ">\n";
        for (const McuCaps* m : CATALOGO_MCU)
            x += std::string("    <valor>") + xml_escapa(m->nombre) + "</valor>\n";
        x += "  </opcion>\n";
    }
    x += "</argumentos>\n";
    std::fputs(x.c_str(), stdout);
    return 0;
}

int sc_main(int argc, char** argv) {
    sc_report_handler::set_actions("rcc",   SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("flash", SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("pll",   SC_WARNING, SC_DO_NOTHING);

    std::vector<std::string> libres;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--valida") g_solo_valida = true;
        else if (a == "--espera-terminal") g_espera_terminal = true;
        else if (a == "--ondas") g_ondas = true;
        else if (a == "--traza-gdb") g_traza_gdb = true;
        else if (a == "--argumentos") return vuelca_argumentos();
        else if (a == "--mcu" && i + 1 < argc) g_tipo_mcu = mayus(argv[++i]);
        else if (a.rfind("--mcu=", 0) == 0)    g_tipo_mcu = mayus(a.substr(6));
        else if (a == "--tiempo-real") g_tiempo_real = 1.0;
        else if (a.rfind("--tiempo-real=", 0) == 0)
            g_tiempo_real = std::atof(a.c_str() + 14);
        // El tiempo simulado SÍ es global: hay un solo reloj de simulación por
        // muchos chips que haya. Como argumento posicional va detrás del
        // firmware, y con varios MCUs el firmware ya no se pone ahí; de ahí
        // esta forma con nombre, que es la única utilizable entonces.
        else if (a.rfind("--ms=", 0) == 0) { g_ms = std::atof(a.c_str() + 5); g_ms_dado = true; }
        else if (a == "--gdb")     g_gdb_modo = "pines";
        else if (a == "--gdb-dap") g_gdb_modo = "dap";
        else if (a.rfind("--port=", 0) == 0) {
            g_gdb_puerto = unsigned(std::atoi(a.c_str() + 7));
            g_puerto_dado = true;
        }
        // `--gui`, `--gui host:puerto` y `--gui=host:puerto`. Lo que sigue se
        // toma como destino solo si NO empieza por guion: asi `--gui --ondas`
        // sigue siendo la ventana por omision y las ondas, y no un host
        // llamado `--ondas`. Es la misma regla que ya usa `--help`.
        else if (a == "--gui" || a.rfind("--gui=", 0) == 0) {
            std::string dest;
            if (a.rfind("--gui=", 0) == 0) dest = a.substr(6);
            else if (i + 1 < argc && argv[i + 1][0] != '-') dest = argv[++i];
            g_gui = stm32::gui::parsea(dest);
            if (!g_gui.valido) {
                std::fprintf(stderr, "--gui: %s\n", g_gui.error.c_str());
                return 1;
            }
            g_gui_pedida = true;
        }
        // `--serie ID=DESTINO` y `--serie=ID=DESTINO`, tantas veces como
        // puentes. La SINTAXIS se comprueba aqui, en cuanto se lee, como la de
        // `--gui`; que el puente exista y que su puerto este libre se
        // comprueba al leer la placa, que es cuando se sabe.
        else if (a == "--serie" || a.rfind("--serie=", 0) == 0) {
            std::string v;
            if (a.rfind("--serie=", 0) == 0) v = a.substr(8);
            else if (i + 1 < argc && argv[i + 1][0] != '-') v = argv[++i];
            if (v.empty()) {
                std::fprintf(stderr, "--serie: falta ID=DESTINO, como en "
                             "--serie VCP=rfc2217:%u\n",
                             stm32::serie::PUERTO_OMISION);
                return 1;
            }
            const stm32::serie::Asignacion as = stm32::serie::parsea_asignacion(v);
            if (!as.valido) {
                std::fprintf(stderr, "--serie: %s\n", as.error.c_str());
                return 1;
            }
            g_serie.push_back(as);
        } else if (a == "--licencia" || a == "--licencias") {
            // POR QUE ESTO ES UNA OPCION Y NO SOLO UN FICHERO SUELTO.
            //
            // Dos obligaciones distintas se cumplen con el mismo parrafo. La
            // seccion 4 de la Apache-2.0 -SystemC va enlazada ESTATICAMENTE en
            // los paquetes de Linux y de Windows- admite trasladar las
            // atribuciones «within a display generated by the Derivative
            // Works». Y la seccion 13 de la AGPL pide que quien use el programa
            // POR UNA RED pueda llegar al codigo fuente; un aviso que el propio
            // programa imprime viaja con el aunque el fichero de licencia se
            // quede por el camino, que es justo lo que pasa cuando alguien
            // copia solo el ejecutable.
            std::printf(
                "mcu-sim - modelo SystemC de microcontroladores STM32\n"
                "Copyright (C) 2026 %s\n"
                "\n"
                "Este programa es software libre: puedes redistribuirlo y/o\n"
                "modificarlo bajo los terminos de la GNU Affero General Public\n"
                "License, version 3, publicada por la Free Software Foundation.\n"
                "\n"
                "Se distribuye con la esperanza de que sea util, pero SIN NINGUNA\n"
                "GARANTIA; ni siquiera la garantia implicita de COMERCIABILIDAD o\n"
                "IDONEIDAD PARA UN PROPOSITO PARTICULAR. La licencia completa esta\n"
                "en el fichero LICENSE y en https://www.gnu.org/licenses/\n"
                "\n"
                "EL CODIGO FUENTE de esta copia esta en:\n"
                "  https://github.com/prodrig/mcu-sim\n"
                "\n"
                "Si ofreces este programa A TRAVES DE UNA RED, la seccion 13 de la\n"
                "AGPL te obliga a dar acceso al fuente de TU version a quien lo use.\n"
                "\n"
                "SOFTWARE DE TERCEROS\n"
                "  SystemC            Apache-2.0, Accellera Systems Initiative.\n"
                "                     Atribuciones completas en su NOTICE, que\n"
                "                     viaja en los paquetes que la llevan dentro.\n"
                "  CMSIS y cabeceras de dispositivo de ST     Apache-2.0 / BSD-3.\n"
                "  CoreMark           EEMBC, Apache-2.0.\n"
                "  libstdc++, libgcc  GPLv3 CON la GCC Runtime Library Exception,\n"
                "                     que es la que permite enlazarlas de forma\n"
                "                     estatica sin arrastrar sus condiciones.\n"
                "\n"
                "El inventario completo, y que parte viaja en cada paquete, esta en\n"
                "TERCEROS.md.\n",
                TITULAR_COPYRIGHT);
            return 0;
        } else if (a == "-h" || a == "--help" || a.rfind("--help=", 0) == 0) {
            // `--help COMPONENTE` y `--help=COMPONENTE`. Lo que sigue se toma
            // como nombre de pieza solo si no empieza por guion: así
            // `sim --help --valida` sigue siendo la ayuda general y no un
            // componente llamado `--valida`.
            std::string que;
            if (a.rfind("--help=", 0) == 0) que = a.substr(7);
            else if (i + 1 < argc && argv[i + 1][0] != '-') que = argv[++i];
            if (!que.empty()) return ayuda_de_componente(que);
            std::printf(
                "uso: sim placa.xml [firmware.bin] [ms]\n"
                "     sim placa.xml --valida     solo comprueba la placa\n"
                "     sim placa.xml --ondas      con la onda cuadrada de los relojes\n"
                "     sim placa.xml --gdb        stub de GDB por los pines SWD\n"
                "     sim placa.xml --gdb-dap    stub de GDB contra el DAP\n"
                "     sim placa.xml --port=3333  puerto TCP del stub\n"
                "     sim placa.xml --traza-gdb  imprime cada paquete RSP recibido\n"
                "     sim placa.xml --gui[=host:puerto]  habla con mcu-sim-gui\n"
                "                                (por omision localhost:%u): le\n"
                "                                manda la placa y no simula hasta\n"
                "                                que la ventana diga 'arranca'.\n"
                "                                El ritmo, la pausa y parar los\n"
                "                                lleva la ventana; sin [ms] en la\n"
                "                                linea de ordenes no hay fin.\n"
                "                                Con --valida, solo le manda la\n"
                "                                placa. Sin la GUI escuchando, sale\n"
                "                                con codigo 2\n"
                "     sim placa.xml --tiempo-real  frena la simulacion al reloj de\n"
                "                                pared (=0.5 a mitad de velocidad)\n"
                "     sim placa.xml --serie ID=DESTINO  a donde da el PuenteSerie ID:\n"
                "                                memoria, tcp:PUERTO, rfc2217:PUERTO,\n"
                "                                tcp-cliente:HOST:PUERTO o\n"
                "                                rfc2217-cliente:HOST:PUERTO\n"
                "                                (manda sobre su host= del XML)\n"
                "     sim placa.xml --espera-terminal  no arranca el MCU hasta que\n"
                "                                haya un terminal en cada puente\n"
                "                                serie por red: asi se ve el saludo\n"
                "                                (doc/puente_serie.md)\n"
                "     sim placa.xml --mcu TIPO   el MCU de la placa: lo pone si el XML\n"
                "                                no declara ninguno y manda sobre el\n"
                "                                tipo si declara uno. Sin <mcu> ni\n"
                "                                --mcu la placa va sin MCU\n"
                "     sim placa.xml --ms=2       tiempo simulado (global: hay un\n"
                "                                solo reloj por muchos chips)\n"
                "     sim --argumentos           estas opciones en XML, para que\n"
                "                                mcu-sim-gui construya su dialogo\n"
                "     sim --help COMPONENTE      que hace ese componente y que\n"
                "                                atributos admite en el XML\n"
                "     sim --licencia             licencia de mcu-sim (AGPLv3), donde\n"
                "                                esta el fuente y el software ajeno\n"
                "                                que lleva dentro\n"
                "\n"
                "Por omision simula 100 ms y para. Para que NO termine hasta que lo\n"
                "digas tu, pide un stub: con un puerto escuchando la simulacion corre\n"
                "indefinidamente y se sale con Ctrl-C. No hace falta que GDB llegue a\n"
                "conectarse. A cambio no se imprime el informe final de los LEDs, que\n"
                "sale al acabar la ventana de --ms.\n"
                "\n"
                "La placa se describe en XML: MCUs, nodos, componentes y conexiones.\n"
                "Varias placas enchufadas entre si son un <sistema>: cada <placa>\n"
                "con su id -de su fichero o escrita dentro-, y lo que las une con\n"
                "<acopla a=\"A/CN9\" b=\"B/J9\"/> (dos Conector, pin a pin, o en\n"
                "espejo) y <hilo a=\"A/CN9.2\" b=\"B/J9.1\"/>. Todo lo de la placa\n"
                "A se nombra A/...: A/LD2, A/u0.PA5. Vease doc/parts.md, 2.5.\n"
                "\n"
                "Con un solo MCU (de <mcu> o de --mcu) estos argumentos valen y\n"
                "mandan sobre lo que diga el XML. Con dos o mas, cada chip lleva lo\n"
                "suyo en su <mcu ... firmware= depuracion= puerto_gdb=> y un\n"
                "argumento global se rechaza, porque ya no dice a cual.\n"
                "\n"
                "Los MCUs que se saben construir son:\n  %s\n"
                "\n"
                "Los tipos de componente que se saben construir son:\n  %s\n"
                "\n"
                "Cada uno se explica solo: `sim --help Led` cuenta lo que hace un\n"
                "LED y que atributos admite. El catalogo completo, con tablas y\n"
                "ejemplos, esta en doc/parts.md.\n",
                unsigned(mcusim::proto::PUERTO_OMISION),
                tipos_como_texto().c_str(),
                Fabrica::tipos_como_texto().c_str());
            // Si alguna pieza se ha registrado sin explicarse, que se sepa
            // aquí y no el día que alguien la busque. No debería pasar -la
            // macro exige la ayuda- y la suite lo comprueba (T126).
            const std::vector<std::string> mudas = Fabrica::sin_documentar();
            if (!mudas.empty()) {
                std::string s;
                for (const std::string& t : mudas) {
                    if (!s.empty()) s += ", ";
                    s += t;
                }
                std::fprintf(stderr,
                    "\nAVISO: estos componentes se registraron sin ayuda y "
                    "`--help` no sabra\nque decir de ellos: %s\n"
                    "Se arregla en su entrada de src/parts/netlist_parts.h.\n",
                    s.c_str());
            }
            return 0;
        } else if (a.size() > 1 && a[0] == '-') {
            // Una opcion que no existe NO es un posicional. Antes de la fase 7
            // de mcu-sim-gui `--no-existe` se tomaba en silencio por el nombre
            // del firmware -y con la placa declarando el suyo, ni se notaba-;
            // con un dialogo donde se escriben argumentos a mano, una errata
            // tiene que decirse. Lo encontro `make gui-argumentos`.
            std::fprintf(stderr, "opcion desconocida: '%s' (sim --help las "
                                 "enumera)\n", a.c_str());
            return 1;
        } else libres.push_back(a);
    }
    if (libres.empty()) {
        std::fprintf(stderr, "uso: sim placa.xml [firmware.bin] [ms]\n");
        return 1;
    }
    g_placa = libres[0];
    if (libres.size() > 1) g_img = libres[1];
    if (libres.size() > 2) { g_ms = std::atof(libres[2].c_str()); g_ms_dado = true; }

    // --- La ventana ----------------------------------------------------------
    // Se avisa ANTES de conectarse si no es la propia maquina: el enlace no
    // esta autenticado. Se permite -hace falta para una GUI en otra maquina-,
    // pero no en silencio (doc/protocolo.md §1).
    if (g_gui_pedida) {
        if (!stm32::gui::es_bucle_local(g_gui.host))
            std::fprintf(stderr,
                "AVISO: '%s' no es la propia maquina. Este enlace NO esta\n"
                "       autenticado: quien lo alcance podra ver el estado de la\n"
                "       simulacion y accionar sus mandos. Los servidores de GDB\n"
                "       de este programa solo escuchan en bucle local por esto\n"
                "       mismo.\n", g_gui.host.c_str());
    }

    Sim s("sim");
    stm32::gui::ClienteGui cliente;
    std::unique_ptr<stm32::gui::CanalGui> canal;
    if (g_gui_pedida) {
        const int r = saluda_gui(s, cliente, canal, argc, argv);
        if (r >= 0) return r;
    }
    sc_start();
    // Se acabo la ventana: se le dice a la GUI, con lo que quedara pendiente
    // -avisos, instantaneas, un T_ESTADO final- y el instante en que se acabo.
    // Si la GUI ya no esta, no pasa nada. Desde la fase 6, tambien puede ser
    // que la ventana dijera T_PARA en marcha: entonces el resumen no lo ha
    // dicho `run()`, y el motivo es M_PARA.
    if (g_enlace && g_enlace->parada()) {
        std::printf("gui: la ventana pidio parar en t = %.3f ms\n",
                    sc_time_stamp().to_seconds() * 1e3);
        s.informe();
        g_enlace->termina(mcusim::proto::M_PARA, 0);
        g_enlace = nullptr;
    } else if (g_enlace && g_sin_fin) {
        // Sin fin solo se acaba asi: la ventana se fue y el enlace paro
        s.informe();
        g_enlace->termina(mcusim::proto::M_VENTANA, 0);
        g_enlace = nullptr;
    } else if (g_enlace) {
        g_enlace->termina(mcusim::proto::M_VENTANA, 0);
        g_enlace = nullptr;
    } else if (g_cliente) {
        g_cliente->fin(mcusim::proto::M_VENTANA, 0, ahora_ns());
        g_cliente = nullptr;
    }
    return 0;
}
