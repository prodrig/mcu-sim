// =============================================================================
// sim_main.cpp — El modelo, con la placa en un fichero
//
//   ./build/sim placa.xml [firmware.bin] [ms_simulados]
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso3.md.)
//
// Es el modo de uso que justifica los tres pasos. `sc_main.cpp` es la suite de
// verificación: 1851 comprobaciones sobre una placa fija escrita en C++. Esto
// es lo otro — un MCU, la placa que diga el XML y el firmware que se le pase—,
// y para cambiar de placa no hace falta recompilar nada.
//
// El orden importa y es el único posible:
//
//   1. leer el fichero -que NO construye nada, solo declara-;
//   2. crear los nodos COMPARTIDOS que la placa declare con `une`, porque un
//      pad que va a un nodo compartido no puede crear el suyo y `Pad::net` es
//      un `sc_port` que se ata en el constructor: la decisión hay que tomarla
//      antes de que el MCU exista;
//   3. construir el MCU con ese cableado y dar de alta sus nodos (los 144 pads
//      con su nombre de esquemático y los diez de alimentación);
//   4. validar la declaración: nodo inexistente, pad que este encapsulado no
//      saca, identificador repetido, referencia hacia delante, tipo que la
//      factoría no conoce;
//   5. construir las piezas;
//   6. validar lo ELÉCTRICO, que necesita las piezas montadas para saber qué
//      terminal conduce y cuál solo escucha;
//   7. y solo entonces `sc_start()`.
//
// Que leer vaya ANTES que construir el MCU no era así al principio, y es lo que
// permite el paso 2. Fue un acierto del paso 3 que aquí se cobra solo: leer
// devuelve datos, y con datos todavía se puede decidir.
//
// Todo lo que va del 1 al 6 ocurre en la elaboración, porque la de SystemC es
// estática: no se puede añadir una pieza con la simulación en marcha. Esa es
// también la razón por la que los pasos 4 y 6 sirven para algo — avisan antes
// de simular, que es cuando el aviso todavía ahorra tiempo.
// =============================================================================
#include <systemc>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "stm32f407vg.h"
#include "../verif/image_loader.h"
#include "../parts/netlist_parts.h"
#include "../parts/netlist_xml.h"

using namespace sc_core;
using namespace stm32;

static std::string g_placa, g_img, g_nombre = "placa";
static double      g_ms      = 100.0;
static bool        g_solo_valida = false;
static bool        g_ondas = false;

SC_MODULE(Sim) {
    Stm32F407VG* dut = nullptr;
    NodeMap      nodos;
    Netlist      placa;
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1;
    unsigned     n_avisos = 0;

    SC_CTOR(Sim) {
        const std::string e = netlist_desde_fichero(placa, g_placa, &g_nombre);
        if (!e.empty()) {
            std::fprintf(stderr, "error de netlist: %s\n", e.c_str());
            std::exit(2);
        }
        // Los nodos COMPARTIDOS, antes del MCU. Si la placa no declara ninguno
        // -que es lo normal- el cableado sale vacío y el MCU se construye
        // exactamente igual que siempre.
        Cableado cab;
        const std::string ec = cableado_desde_netlist(placa, nodos, cab);
        if (!ec.empty()) {
            std::fprintf(stderr, "  [decl] %s\n", ec.c_str());
            std::exit(2);
        }
        dut = new Stm32F407VG("dut", DBG_PINES, cab);
        nodos.registra_mcu(dut->pinmux, dut->pwr_pads);

        for (const std::string& q : placa.valida(nodos)) {
            std::fprintf(stderr, "  [decl] %s\n", q.c_str());
            ++n_avisos;
        }
        if (n_avisos) {
            std::fprintf(stderr, "%s: %u problemas de declaracion; no se monta\n",
                         g_placa.c_str(), n_avisos);
            std::exit(2);
        }
        placa.construye(nodos);
        for (const std::string& q : placa.valida_electrica(nodos)) {
            std::fprintf(stderr, "  [elec] %s\n", q.c_str());
            ++n_avisos;
        }
        // Un conflicto eléctrico NO detiene la simulación: puede ser una
        // decisión deliberada -un pin compartido entre dos montajes- y el que
        // manda es quien escribe la placa. Pero se dice, y se dice antes.
        std::printf("placa '%s': %u componentes, %u nodos, %u avisos\n",
                    g_nombre.c_str(), unsigned(placa.instancias().size()),
                    nodos.size(), n_avisos);

        d_vdd  = dut->pwr_pads.vdd.register_driver("sim_vdd");
        d_vdda = dut->pwr_pads.vdda.register_driver("sim_vdda");
        d_nrst = dut->pwr_pads.nrst.register_driver("sim_nrst");
        d_bt0  = dut->pwr_pads.boot0.register_driver("sim_boot0");
        SC_THREAD(run);
    }
    ~Sim() { placa.libera(); delete dut; }

    void run() {
        if (g_solo_valida) { sc_stop(); return; }
        // La onda cuadrada de los relojes internos se apaga por omision. No es
        // una simplificacion del modelo: es un interruptor que ya existia. Con
        // ella encendida, los flancos de HCLK son el noventa y tantos por
        // ciento de los eventos de la simulacion, y solo hacen falta cuando lo
        // que se mira es el propio arbol de reloj. `--ondas` la devuelve.
        dut->rcc.set_internal_waveforms(g_ondas);
        // Arranque eléctrico: el mismo de siempre, porque el MCU no sabe que
        // su placa viene de un fichero.
        dut->pwr_pads.vdd.set_drive(d_vdd, 0.0f, 1.0f);
        dut->pwr_pads.vdda.set_drive(d_vdda, 0.0f, 1.0f);
        dut->pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(10, SC_US);

        ImageLoader ld(*dut);
        if (!g_img.empty()) {
            const long n = ld.load_file(g_img.c_str(), addr::FLASH_BASE);
            if (n <= 0) {
                std::fprintf(stderr, "no se puede cargar %s\n", g_img.c_str());
                sc_stop();
                return;
            }
            std::printf("firmware: %ld bytes de %s\n", n, g_img.c_str());
        } else {
            ld.write_reset_vector(addr::SRAM1_BASE + addr::SRAM1_SIZE, 0x08000100u);
            ld.poke32(addr::FLASH_BASE + 0x100, 0xE7FDBF20u);   // wfe ; b .-2
            std::printf("sin firmware: el nucleo se aparca en wfe\n");
        }

        dut->pwr_pads.vdd.set_drive(d_vdd, 3.3f, 0.1f);
        dut->pwr_pads.vdda.set_drive(d_vdda, 3.3f, 0.1f);
        wait(100, SC_US);
        dut->pwr_pads.nrst.set_hiz(d_nrst);

        const auto h0 = std::chrono::steady_clock::now();
        wait(g_ms, SC_MS);
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
        else if (a == "-h" || a == "--help") {
            std::printf(
                "uso: sim placa.xml [firmware.bin] [ms]\n"
                "     sim placa.xml --valida     solo comprueba la placa\n"
                "     sim placa.xml --ondas      con la onda cuadrada de los relojes\n"
                "\n"
                "La placa se describe en XML: nodos, componentes y conexiones.\n"
                "Los tipos que se saben construir son:\n  %s\n",
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
