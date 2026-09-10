// =============================================================================
// sim_main.cpp — El modelo, con la placa en un fichero
//
//   ./build/sim placa.xml [firmware.bin] [ms_simulados]
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso3.md.)
//
// Es el modo de uso que justifica los tres pasos. `sc_main.cpp` es la suite de
// verificación: 1871 comprobaciones sobre una placa fija escrita en C++. Esto
// es lo otro — los MCUs y la placa que diga el XML, con el firmware que se les
// pase—, y para cambiar de placa no hace falta recompilar nada.
//
// VARIOS MCUs. Una placa puede declarar cero, uno o varios `<mcu>`, cada uno
// con su firmware, su modo de depuración y su puerto de GDB
// [doc/stm32f407vg_multi_mcu.md, §5]:
//
//   <mcu tipo="STM32F407VG" id="u0" firmware="maestro.bin"
//        depuracion="dap"   puerto_gdb="3333"/>
//   <mcu tipo="STM32F407VG" id="u1" firmware="esclavo.bin"
//        depuracion="pines" puerto_gdb="3334"/>
//
// Y la regla que hace que nada de esto rompa lo anterior:
//
//   ningún <mcu>   un STM32F407VG implícito y nodos con nombre desnudo
//                  (`PD12`). Es el comportamiento de siempre;
//   un <mcu>       valen los dos nombres, `PD12` y `u0.PD12`, y los argumentos
//                  de la línea de órdenes siguen sirviendo: manda la línea de
//                  órdenes sobre lo que diga el XML;
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
#include <map>
#include <string>
#include <vector>
#include "../common/asan_opciones.h"
#include "stm32f407vg.h"
#include "../verif/image_loader.h"
#include "../verif/gdb_stub.h"
#include "../parts/netlist_parts.h"
#include "../parts/netlist_xml.h"

using namespace sc_core;
using namespace stm32;

// El único tipo de MCU que se sabe construir. Cuando haya un segundo, esto pasa
// a ser una factoría con auto-registro, igual que la de piezas del paso 3
// [doc/stm32f407vg_multi_mcu.md, §6.1]; mientras solo haya uno, una factoría
// sería andamio sin obra.
static const char* TIPO_MCU = "STM32F407VG";

static std::string g_placa, g_img, g_nombre = "placa";
static double      g_ms      = 100.0;
static bool        g_solo_valida = false;
static bool        g_ondas = false;
static bool        g_traza_gdb = false;
// Factor de tiempo real: 0 = a toda velocidad (lo de siempre);
// 1 = un segundo simulado por segundo de reloj de pared; 0,5 = a la
// mitad, para poder mirar lo que pasa.
static double      g_tiempo_real = 0.0;
// Depuración pedida por la línea de órdenes. `g_gdb_modo` vacío = no se pidió.
static std::string g_gdb_modo;          // "pines" o "dap"
static unsigned    g_gdb_puerto = 0;
static bool        g_puerto_dado = false;

// ---------------------------------------------------------------------------
// Un MCU montado: lo que la placa declaró, el chip, su stub de pines si lo
// lleva, y los drivers con los que se le da corriente.
// ---------------------------------------------------------------------------
struct McuMontado {
    DeclMcu       decl;
    Stm32F407VG*  dut  = nullptr;
    GdbStub*      stub = nullptr;       // solo en modo "pines" y con puerto
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1;
};

static void muere(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    std::exit(2);
}

SC_MODULE(Sim) {
    std::vector<McuMontado> mcus;
    NodeMap      nodos;
    Netlist      placa;
    unsigned     n_avisos = 0;
    bool         hay_stub = false;

    SC_CTOR(Sim) {
        // --- 1. Leer. No construye nada: devuelve datos ---------------------
        const std::string e = netlist_desde_fichero(placa, g_placa, &g_nombre);
        if (!e.empty()) muere("error de netlist: " + e);

        // --- 2. La lista de MCUs, y la línea de órdenes encima ---------------
        std::vector<DeclMcu> decls = placa.mcus();
        if (decls.empty()) {                 // ninguno declarado: uno implícito
            DeclMcu m;
            m.tipo = TIPO_MCU;               // id vacío -> nodos con nombre desnudo
            decls.push_back(m);
        }
        aplica_linea_de_ordenes(decls);
        for (const DeclMcu& m : decls)
            if (m.tipo != TIPO_MCU)
                muere("mcu " + m.id + ": tipo desconocido '" + m.tipo +
                      "'. El unico que se sabe construir es " + TIPO_MCU);

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
            m.dut = new Stm32F407VG(nm.c_str(), caps, cab);
            // Los nodos, con el prefijo del chip. Y además con el nombre
            // desnudo cuando solo hay uno: es lo que hace que las placas
            // escritas hasta hoy sigan valiendo sin migrarlas.
            nodos.registra_mcu(d.id, m.dut->pinmux, m.dut->pwr_pads);
            if (decls.size() == 1 && !d.id.empty())
                nodos.registra_mcu(std::string(), m.dut->pinmux, m.dut->pwr_pads);
            // El stub de PINES se cuelga por fuera de PA14/PA13, como un
            // ST-LINK. El de DAP lo ha creado ya el propio núcleo.
            if (d.depuracion == "pines" && d.puerto_gdb) {
                const std::string snm = "gdb_" + (d.id.empty() ? "dut" : d.id);
                m.stub = new GdbStub(snm.c_str(),
                                     m.dut->pinmux.analog(0, 14),   // PA14 SWCLK
                                     m.dut->pinmux.analog(0, 13),   // PA13 SWDIO
                                     d.puerto_gdb, 2e6);
                m.stub->set_enabled(false);      // se abre al arrancar, no aquí
            }
            if (d.puerto_gdb) hay_stub = true;
            m.d_vdd  = m.dut->pwr_pads.vdd.register_driver("sim_vdd");
            m.d_vdda = m.dut->pwr_pads.vdda.register_driver("sim_vdda");
            m.d_nrst = m.dut->pwr_pads.nrst.register_driver("sim_nrst");
            m.d_bt0  = m.dut->pwr_pads.boot0.register_driver("sim_boot0");
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

        // --- 6 y 7. Construir las piezas y validar lo eléctrico -------------
        placa.construye(nodos);
        for (const std::string& q : placa.valida_electrica(nodos)) {
            std::fprintf(stderr, "  [elec] %s\n", q.c_str());
            ++n_avisos;
        }
        // Un conflicto eléctrico NO detiene la simulación: puede ser una
        // decisión deliberada -un pin compartido entre dos montajes- y el que
        // manda es quien escribe la placa. Pero se dice, y se dice antes.
        std::printf("placa '%s': %u MCU(s), %u componentes, %u nodos, %u avisos\n",
                    g_nombre.c_str(), unsigned(mcus.size()),
                    unsigned(placa.instancias().size()), nodos.n_nodos(), n_avisos);
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
        for (McuMontado& m : mcus) { delete m.stub; delete m.dut; }
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
    void aplica_linea_de_ordenes(std::vector<DeclMcu>& decls) {
        const bool global = !g_img.empty() || !g_gdb_modo.empty() || g_puerto_dado;
        if (decls.size() > 1) {
            if (!global) return;
            std::string ids;
            for (const DeclMcu& m : decls) { if (!ids.empty()) ids += ", "; ids += m.id; }
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
    void espera(const sc_time& d) {
        if (g_tiempo_real <= 0.0) { wait(d); return; }
        const auto t0 = std::chrono::steady_clock::now();
        const double sim0 = sc_time_stamp().to_seconds();
        wait(d);
        const double avance = sc_time_stamp().to_seconds() - sim0;
        const double debe = avance / g_tiempo_real;      // segundos de pared
        const double lleva =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (debe > lleva)
            std::this_thread::sleep_for(std::chrono::duration<double>(debe - lleva));
    }

    void run() {
        if (g_solo_valida) { sc_stop(); return; }
        // La onda cuadrada de los relojes internos se apaga por omision. No es
        // una simplificacion del modelo: es un interruptor que ya existia. Con
        // ella encendida, los flancos de HCLK son el noventa y tantos por
        // ciento de los eventos de la simulacion, y solo hacen falta cuando lo
        // que se mira es el propio arbol de reloj. `--ondas` la devuelve.
        for (McuMontado& m : mcus) {
            m.dut->rcc.set_internal_waveforms(g_ondas);
            // Arranque eléctrico: el mismo de siempre, porque el MCU no sabe
            // que su placa viene de un fichero.
            m.dut->pwr_pads.vdd.set_drive(m.d_vdd, 0.0f, 1.0f);
            m.dut->pwr_pads.vdda.set_drive(m.d_vdda, 0.0f, 1.0f);
            m.dut->pwr_pads.boot0.set_drive(m.d_bt0, 0.0f, 10e3f);
            m.dut->pwr_pads.nrst.set_drive(m.d_nrst, 0.0f, 100.0f);
        }
        wait(10, SC_US);

        for (McuMontado& m : mcus) {
            const char* quien = m.decl.id.empty() ? "" : m.decl.id.c_str();
            ImageLoader ld(*m.dut);
            if (!m.decl.firmware.empty()) {
                const long n = ld.load_file(m.decl.firmware.c_str(), addr::FLASH_BASE);
                if (n <= 0) muere("no se puede cargar " + m.decl.firmware);
                std::printf("firmware%s%s: %ld bytes de %s\n",
                            *quien ? " de " : "", quien, n, m.decl.firmware.c_str());
            } else {
                ld.write_reset_vector(addr::SRAM1_BASE + addr::SRAM1_SIZE, 0x08000100u);
                ld.poke32(addr::FLASH_BASE + 0x100, 0xE7FDBF20u);   // wfe ; b .-2
                std::printf("sin firmware%s%s: el nucleo se aparca en wfe\n",
                            *quien ? " en " : "", quien);
            }
        }

        for (McuMontado& m : mcus) {
            m.dut->pwr_pads.vdd.set_drive(m.d_vdd, 3.3f, 0.1f);
            m.dut->pwr_pads.vdda.set_drive(m.d_vdda, 3.3f, 0.1f);
        }
        wait(100, SC_US);
        for (McuMontado& m : mcus) m.dut->pwr_pads.nrst.set_hiz(m.d_nrst);

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
                } else if (m.dut->core.gdb) {                   // modo dap
                    m.dut->core.gdb->set_verbose(g_traza_gdb);
                    m.dut->core.gdb->set_enabled(true);
                }
            }
            std::printf("esperando a GDB; la simulacion no se detiene sola "
                        "(Ctrl-C para salir)\n");
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

        const auto h0 = std::chrono::steady_clock::now();
        espera(sc_time(g_ms, SC_MS));
        const double seg =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - h0).count();
        std::printf("simulados %.3f ms en %.3f s de anfitrion (%llu deltas)\n",
                    g_ms, seg, (unsigned long long)sc_delta_count());
        // Y lo que se ve desde fuera. Un LED es el instrumento de medida mas
        // antiguo de este oficio, asi que se dice como acabo cada uno: su
        // tension de pin y la corriente que le pasa, en float, como el modelo
        // las resuelve de verdad.
        for (const Instancia& i : placa.instancias()) {
            if (i.tipo != "Led") continue;
            const Led* l = placa.como<Led>(i.id);
            if (!l) continue;
            // La patilla del pin se llama `anodo` o `catodo` segun el montaje.
            const std::string& nd = i.nodo_de("anodo").empty()
                                  ? i.nodo_de("catodo") : i.nodo_de("anodo");
            std::printf("  LED %s en %s: %s  (%.2f V, %.2f mA)\n", i.id.c_str(),
                        nd.c_str(), l->on() ? "encendido" : "apagado",
                        double(l->pin_voltage()), l->current() * 1e3);
        }
        sc_stop();
    }
};

int sc_main(int argc, char** argv) {
    sc_report_handler::set_actions("rcc",   SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("flash", SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("pll",   SC_WARNING, SC_DO_NOTHING);

    std::vector<std::string> libres;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--valida") g_solo_valida = true;
        else if (a == "--ondas") g_ondas = true;
        else if (a == "--traza-gdb") g_traza_gdb = true;
        else if (a == "--tiempo-real") g_tiempo_real = 1.0;
        else if (a.rfind("--tiempo-real=", 0) == 0)
            g_tiempo_real = std::atof(a.c_str() + 14);
        // El tiempo simulado SÍ es global: hay un solo reloj de simulación por
        // muchos chips que haya. Como argumento posicional va detrás del
        // firmware, y con varios MCUs el firmware ya no se pone ahí; de ahí
        // esta forma con nombre, que es la única utilizable entonces.
        else if (a.rfind("--ms=", 0) == 0) g_ms = std::atof(a.c_str() + 5);
        else if (a == "--gdb")     g_gdb_modo = "pines";
        else if (a == "--gdb-dap") g_gdb_modo = "dap";
        else if (a.rfind("--port=", 0) == 0) {
            g_gdb_puerto = unsigned(std::atoi(a.c_str() + 7));
            g_puerto_dado = true;
        } else if (a == "-h" || a == "--help") {
            std::printf(
                "uso: sim placa.xml [firmware.bin] [ms]\n"
                "     sim placa.xml --valida     solo comprueba la placa\n"
                "     sim placa.xml --ondas      con la onda cuadrada de los relojes\n"
                "     sim placa.xml --gdb        stub de GDB por los pines SWD\n"
                "     sim placa.xml --gdb-dap    stub de GDB contra el DAP\n"
                "     sim placa.xml --port=3333  puerto TCP del stub\n"
                "     sim placa.xml --traza-gdb  imprime cada paquete RSP recibido\n"
                "     sim placa.xml --tiempo-real  frena la simulacion al reloj de\n"
                "                                pared (=0.5 a mitad de velocidad)\n"
                "     sim placa.xml --ms=2       tiempo simulado (global: hay un\n"
                "                                solo reloj por muchos chips)\n"
                "\n"
                "Por omision simula 100 ms y para. Para que NO termine hasta que lo\n"
                "digas tu, pide un stub: con un puerto escuchando la simulacion corre\n"
                "indefinidamente y se sale con Ctrl-C. No hace falta que GDB llegue a\n"
                "conectarse. A cambio no se imprime el informe final de los LEDs, que\n"
                "sale al acabar la ventana de --ms.\n"
                "\n"
                "La placa se describe en XML: MCUs, nodos, componentes y conexiones.\n"
                "\n"
                "Con un solo MCU -declarado o implicito- estos argumentos valen y\n"
                "mandan sobre lo que diga el XML. Con dos o mas, cada chip lleva lo\n"
                "suyo en su <mcu ... firmware= depuracion= puerto_gdb=> y un\n"
                "argumento global se rechaza, porque ya no dice a cual.\n"
                "\n"
                "Los tipos de componente que se saben construir son:\n  %s\n",
                Fabrica::tipos_como_texto().c_str());
            return 0;
        } else libres.push_back(a);
    }
    if (libres.empty()) {
        std::fprintf(stderr, "uso: sim placa.xml [firmware.bin] [ms]\n");
        return 1;
    }
    g_placa = libres[0];
    if (libres.size() > 1) g_img = libres[1];
    if (libres.size() > 2) g_ms  = std::atof(libres[2].c_str());

    Sim s("sim");
    sc_start();
    return 0;
}
