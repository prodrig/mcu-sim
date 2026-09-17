// =============================================================================
// bench_main.cpp — Banco de MEDIDA del coste de simulación
//
// No verifica nada: levanta el MCU, carga un firmware, lo deja correr un tiempo
// simulado fijo y mide el tiempo de CPU del anfitrión. Sirve para responder a
// una pregunta muy concreta: ¿cuánto cuesta llevar dentro un periférico que el
// programa no usa?
//
//   ./build/bench [imagen.bin] [ms_simulados]
//
// Imprime el tiempo de anfitrión, el número de eventos de SystemC (deltas) y el
// número de despertares de proceso, que es la métrica que de verdad explica el
// coste de un modelo de eventos discretos.
// =============================================================================
#include <systemc>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../common/asan_opciones.h"
#include "soc_f4.h"
#include "../verif/image_loader.h"
#include "../parts/ext_parts.h"

using namespace sc_core;
using namespace stm32;

static std::string g_img;
static double      g_ms = 50.0;

SC_MODULE(Bench) {
    SocF4* dut = nullptr;
    Crystal*     xtal = nullptr;
    int d_vdd = -1, d_vdda = -1, d_nrst = -1, d_bt0 = -1, d_pb2 = -1;

    SC_CTOR(Bench) {
        dut  = new SocF4("dut");
        xtal = new Crystal(dut->pinmux.analog(7, 0));       // PH0-OSC_IN
        d_vdd  = dut->pwr_pads.vdd.register_driver("tb_vdd");
        d_vdda = dut->pwr_pads.vdda.register_driver("tb_vdda");
        d_nrst = dut->pwr_pads.nrst.register_driver("tb_nrst");
        d_bt0  = dut->pwr_pads.boot0.register_driver("tb_boot0");
        d_pb2  = dut->pinmux.analog(1, 2).register_driver("tb_boot1");
        SC_THREAD(run);
    }
    ~Bench() { delete xtal; delete dut; }

    void run() {
        // La onda cuadrada de los relojes internos se apaga: es lo que hace el
        // modo de ejecución normal del modelo, y sin ello los flancos de HCLK
        // ahogarían cualquier otra medida.
        dut->rcc.set_internal_waveforms(false);

        // Arranque eléctrico
        dut->pwr_pads.vdd.set_drive(d_vdd, 0.0f, 1.0f);
        dut->pwr_pads.vdda.set_drive(d_vdda, 0.0f, 1.0f);
        dut->pwr_pads.boot0.set_drive(d_bt0, 0.0f, 10e3f);
        dut->pinmux.analog(1, 2).set_drive(d_pb2, 0.0f, 10e3f);
        dut->pwr_pads.nrst.set_drive(d_nrst, 0.0f, 100.0f);
        wait(10, SC_US);

        ImageLoader ld(*dut);
        if (!g_img.empty()) {
            const long n = ld.load_file(g_img.c_str(), addr::FLASH_BASE);
            std::printf("  imagen: %ld bytes de %s\n", n, g_img.c_str());
        } else {
            // Sin imagen: el núcleo se aparca en un bucle de bajo consumo.
            ld.write_reset_vector(addr::SRAM1_BASE + addr::SRAM1_SIZE, 0x08000100u);
            ld.poke32(addr::FLASH_BASE + 0x100, 0xE7FDBF20u);   // wfe ; b .-2
            std::printf("  sin imagen: nucleo aparcado en wfe\n");
        }

        dut->pwr_pads.vdd.set_drive(d_vdd, 3.3f, 0.1f);
        dut->pwr_pads.vdda.set_drive(d_vdda, 3.3f, 0.1f);
        wait(100, SC_US);
        dut->pwr_pads.nrst.set_hiz(d_nrst);
        wait(200, SC_US);

        // --- Ventana de medida ------------------------------------------------
        const sc_time t0 = sc_time_stamp();
        const auto    h0 = std::chrono::steady_clock::now();
        wait(g_ms, SC_MS);
        const auto    h1 = std::chrono::steady_clock::now();
        const double  seg = std::chrono::duration<double>(h1 - h0).count();

        std::printf("  ventana simulada : %s\n", (sc_time_stamp() - t0).to_string().c_str());
        std::printf("  tiempo anfitrion : %.3f s\n", seg);
        std::printf("  deltas           : %llu\n",
                    (unsigned long long)sc_delta_count());
        // Trafico real por la matriz: es el denominador honesto de cualquier
        // discusion sobre el coste del decodificador.
        unsigned long long tot = 0;
        for (unsigned m = 0; m < 8; ++m)
            for (unsigned sl = 0; sl < 7; ++sl) tot += dut->matrix.n_xfer[m][sl];
        std::printf("  transacciones AHB: %llu  (nucleo I=%llu D=%llu S=%llu)\n", tot,
            (unsigned long long)(dut->matrix.n_xfer[0][0] + dut->matrix.n_xfer[0][2] + dut->matrix.n_xfer[0][4]),
            (unsigned long long)(dut->matrix.n_xfer[1][1] + dut->matrix.n_xfer[1][2] + dut->matrix.n_xfer[1][4]),
            (unsigned long long)(dut->matrix.n_xfer[2][2] + dut->matrix.n_xfer[2][4] + dut->matrix.n_xfer[2][5]));
        std::printf("  nucleo parado    : %d\n", int(dut->core.debug.is_halted()));
        std::printf("RESULTADO %.4f %llu\n", seg,
                    (unsigned long long)sc_delta_count());
        sc_stop();
    }
};

int sc_main(int argc, char** argv) {
    sc_report_handler::set_actions("rcc",   SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("flash", SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("pll",   SC_WARNING, SC_DO_NOTHING);
    sc_report_handler::set_actions("pad",   SC_WARNING, SC_DO_NOTHING);
    if (argc > 1) g_img = argv[1];
    if (argc > 2) g_ms  = std::atof(argv[2]);
    Bench b("bench");
    sc_start();
    return 0;
}
