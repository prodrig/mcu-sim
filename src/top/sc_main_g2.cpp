// =============================================================================
// sc_main_g2.cpp — LA SUITE DEL MSP430G2553 (fase M3 de doc/analisis_msp430.md)
//
//   G1  el SoC contra su descriptor generado: los vectores que usa son los de
//       la cabecera de TI; las constantes de fábrica del DCO dan 1, 8, 12 y
//       16 MHz (sin tiempo)
//   G2  g2_basico.c (msp430-elf-gcc -mmcu=msp430g2553): el DCO a 1 MHz con
//       CALBC1/CALDCO, la UART a 9600 por P1.2 (el tiempo de bit en el pin),
//       la Flash (borrar y escribir INFO D), el sensor de temperatura del
//       ADC10, Timer0_A3 en modo ascendente cambiando P1.0 cada 1 ms, LPM0, el
//       botón S2 (P1.3 con REN e interrupción) y el eco de RX
//   G3  g2_wdt.c: el perro guardián sin refrescar da un PUC a los 8,192 ms y
//       el programa lo ve en WDTIFG
//   G4  la sonda de bancada por Spy-Bi-Wire en TEST/RST: el JTAG ID y la Flash
//   G5  el Replicator430 de TI (verif/m430/replicator), compilado para el
//       anfitrión sobre los pines del chip, como oráculo: por SBW, GetDevice,
//       el borrado masivo con su comprobación por PSA, WriteFLASH de
//       g2_basico.elf con TCLKstrobes y su VerifyMem por PSA, la memoria
//       rápida (DATA_QUICK), HaltCPU/ReleaseCPU con la misma secuencia que la
//       sonda (MSPDS), ReleaseDevice y el programa en marcha; por JTAG de 4
//       hilos en P1.4-P1.7, el segmento D; y el fusible (BlowFuse con VPP en
//       TEST): después, GetDevice da STATUS_FUSEBLOWN
// =============================================================================
#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/g2553/soc_g2553.h"
#include "m430/sonda/sonda_m430.h"
#include "verif/m430/replicator/replicador430.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/g2553/elf/";

SC_MODULE(TbG2) {
    SocG2553 g2{"g2553"};
    int id_boton_ = -1;
    SC_CTOR(TbG2) { SC_THREAD(run); id_boton_ = g2.p(1, 3).register_driver("boton_s2"); g2.p(1, 3).set_hiz(id_boton_); }

    // Los flancos de un pin
    struct Mide { std::vector<sc_time> t; std::vector<bool> v; };
    Mide mide(analog_net_if& n, sc_time cuanto) {
        Mide m;
        const sc_time fin = sc_time_stamp() + cuanto;
        bool ant = n.voltage() > 1.6f;
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), n.value_changed_event());
            const bool v = n.voltage() > 1.6f;
            if (v != ant) { m.t.push_back(sc_time_stamp()); m.v.push_back(v); ant = v; }
        }
        return m;
    }
    void corre(const char* elf) {
        g2.reset(true);
        wait(1, SC_US);
        const std::string e = g2.carga(std::string(DIR) + elf);
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        g2.uart_tx.clear();
        g2.reset(false);
    }

    void g1() {
        grupo("G1 el SoC contra el descriptor generado, sin tiempo");
        const m430::Desc430& d = SocG2553::DESC;
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"PORT1", 0xFFE4}, {"PORT2", 0xFFE6}, {"ADC10", 0xFFEA}, {"USCIAB0TX", 0xFFEC},
                       {"USCIAB0RX", 0xFFEE}, {"TIMER0_A1", 0xFFF0}, {"TIMER0_A0", 0xFFF2}, {"WDT", 0xFFF4},
                       {"TIMER1_A1", 0xFFF8}, {"TIMER1_A0", 0xFFFA}, {"NMI", 0xFFFC}, {"RESET", 0xFFFE}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 12 vectores que usa el SoC, como en msp430g2553.h");
        check(d.ram.ini == 0x200 && d.ram.fin == 0x3FF && d.principal.ini == 0xC000 && d.info.ini == 0x1000 &&
              d.bsl.ini == 0x0C00 && d.jtag_id == 0x89 && d.eem == m430::TamEem::XS,
              "el mapa (RAM, Flash, información, BSL), el JTAG ID y el EEM del descriptor");
        struct C { double f; uint32_t a; };
        const C cal[] = {{1e6, 0x10FE}, {8e6, 0x10FC}, {12e6, 0x10FA}, {16e6, 0x10F8}};
        for (const C& c : cal) {
            const uint8_t dco = g2.mem8(c.a), bc1 = g2.mem8(c.a + 1);
            const double f = SocG2553::frecuencia_dco(bc1 & 15, dco >> 5, dco & 31);
            char t[96]; std::snprintf(t, sizeof t, "CALBC1/CALDCO de %2.0f MHz: %.3f MHz (±1 %%)", c.f / 1e6, f / 1e6);
            check(std::fabs(f - c.f) < 0.01 * c.f, t);
        }
    }
    void g2_() {
        grupo("G2 g2_basico.c: DCO, UART, Flash, ADC10, Timer_A, LPM0, S2 y RX");
        corre("g2_basico.elf");
        // El bit de arranque de la primera letra, en P1.2 (TXD)
        const Mide m = mide(g2.p(1, 2), sc_time(3, SC_MS));
        // el bit: el intervalo más corto entre flancos de la primera letra ('h' = 0x68)
        double tb = 0.0;
        for (size_t i = 1; i + 1 < m.t.size() && i < 10; ++i) {
            const double x = (m.t[i + 1] - m.t[i]).to_seconds() * 1e6;
            if (tb == 0.0 || x < tb) tb = x;
        }
        char t[128];
        std::snprintf(t, sizeof t, "MCLK %.4f MHz; el primer bit de TXD en P1.2: %.1f µs (9600 baudios: 104,2)", g2.f_mclk() / 1e6, tb);
        check(std::fabs(g2.f_mclk() - 1e6) < 1e4 && std::fabs(tb - 104.2) < 2.0, t);
        wait(80, SC_MS);
        const std::string s = g2.uart_tx;
        check(s.rfind("hola G2553\nflash ok\nadc ", 0) == 0, "la UART: «hola G2553», «flash ok»");
        unsigned adc = 0;
        if (s.size() > 25) adc = unsigned(std::atoi(s.c_str() + 24));
        std::snprintf(t, sizeof t, "el sensor a 25 °C con 1,5 V: %u (0,00355·T + 0,986 V: 733)", adc);
        check(adc >= 725 && adc <= 740, t);
        check(g2.mem16(0x1000) == 0x1234 && g2.mem16(0x1002) == 0xFFFF && g2.mem8(0x10FE) != 0xFF,
              "INFO D borrado y escrito por el controlador; INFOA intacto");
        check(s.find("20 ms\n") != std::string::npos, "LPM0 y Timer0_A3: la rutina despierta a main a los 20 ms");
        const Mide p = mide(g2.p(1, 0), sc_time(10, SC_MS));
        const double per = p.t.size() >= 3 ? (p.t[2] - p.t[1]).to_seconds() * 1e3 : 0.0;
        std::snprintf(t, sizeof t, "P1.0 cambia cada %.3f ms (CCR0 = 999 con SMCLK = 1 MHz)", per);
        check(std::fabs(per - 1.0) < 0.02, t);
        const bool led0 = g2.p(1, 6).voltage() > 1.6f;
        g2.p(1, 3).set_drive(id_boton_, 0.0f, 10.0f);           // S2 pulsado
        wait(1, SC_MS);
        g2.p(1, 3).set_hiz(id_boton_);
        wait(1, SC_MS);
        check((g2.p(1, 6).voltage() > 1.6f) != led0, "S2 (P1.3, REN y flanco de bajada): la rutina cambia P1.6");
        const size_t n0 = g2.uart_tx.size();
        g2.uart_rx('A');
        wait(3, SC_MS);
        check(g2.uart_tx.size() == n0 + 1 && g2.uart_tx.back() == 'B', "RX: 'A' entra por la UART y sale 'B'");
    }

    void g3() {
        grupo("G3 g2_wdt.c: el perro guardián");
        const unsigned p0 = g2.pucs();
        corre("g2_wdt.elf");
        wait(60, SC_MS);
        check(g2.uart_tx == "perro en marcha\nreinicio por WDT\n" && g2.pucs() == p0 + 1,
              "sin refrescar: un PUC (" + std::to_string(g2.pucs() - p0) + "), y tras él WDTIFG");
    }

    void g4() {
        grupo("G4 la sonda de bancada por Spy-Bi-Wire (TEST y RST)");
        corre("g2_basico.elf");
        wait(5, SC_MS);
        SondaM430::Pines p; p.test = &g2.test; p.rst = &g2.rst;
        SondaM430 s(p, SondaM430::Modo::Sbw);
        const uint8_t id = s.conecta();
        check(id == 0x89, "el JTAG ID: 0x89");
        const bool sinc = s.sincroniza(false);
        check(sinc && s.lee_palabra(0xFFFE) == g2.mem16(0xFFFE) && s.lee_palabra(0x10FE) == g2.mem16(0x10FE),
              "sincronizada; el vector de reset y CALDCO_1MHZ por ReadMemWord");
        s.desconecta();
        wait(100, SC_US);
    }

    // La imagen de un ELF en palabras, por trozos
    struct Trozo { uint16_t dir; std::vector<uint16_t> w; };
    static std::vector<Trozo> imagen(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::vector<Trozo> r;
        if (!lee_elf(ruta, t, ELF_MSP430).empty()) return r;
        for (const TrozoFw& x : t) {
            // Solo la Flash principal (lo de delante es la cabecera del ELF)
            const uint32_t ini = std::max<uint32_t>(x.dir, 0xC000), fin = std::min<uint32_t>(x.dir + uint32_t(x.datos.size()), 0x10000);
            if (ini >= fin) continue;
            Trozo z; z.dir = uint16_t(ini & ~1u);
            for (uint32_t a = z.dir; a < fin; a += 2) {
                auto b = [&](uint32_t y) -> uint16_t { return (y >= x.dir && y < x.dir + x.datos.size()) ? x.datos[y - x.dir] : 0xFF; };
                z.w.push_back(uint16_t(b(a) | (b(a + 1) << 8)));
            }
            r.push_back(z);
        }
        return r;
    }
    // Lo que entra por TDI, sin lo que sale (para comparar secuencias)
    static std::vector<std::string> entradas(const std::vector<std::string>& t, size_t desde = 0) {
        std::vector<std::string> r;
        for (size_t i = desde; i < t.size(); ++i) r.push_back(t[i].substr(0, t[i].find('>')));
        return r;
    }
    void g5() {
        grupo("G5 el Replicator430 de TI como oráculo (SBW, JTAG de 4 hilos y el fusible)");
        g2.reset(true);
        wait(1, SC_US);
        g2.reset(false);
        wait(1, SC_MS);
        std::vector<std::string> tr;
        HilRep::Pines hp; hp.test = &g2.test; hp.rst = &g2.rst;
        {
            Replicador430 r(hp, true);
            r.hil().alimenta = [this](bool on) { g2.reset(!on); };
            r.init_target();
            check(r.get_device() == 1, "SBW: GetDevice (JTAG ID 0x89, el chip 0x2553 en 0x0FF0, ExecutePOR)");
            r.erase_flash(Replicador430::BORRA_MASIVO, 0xFE00);
            check(r.erase_check(0xC000, 0x2000) == 1 && g2.mem16(0xC000) == 0xFFFF && g2.mem16(0xFFFE) == 0xFFFF &&
                  g2.mem8(0x10FE) != 0xFF,
                  "EraseFLASH(ERASE_MASS) con TCLKstrobes(10600) y EraseCheck por PSA: la principal en blanco, INFOA no (LOCKA)");
            std::vector<Trozo> img = imagen(std::string(DIR) + "g2_basico.elf");
            unsigned palabras = 0;
            for (Trozo& z : img) { r.write_flash(z.dir, uint16_t(z.w.size()), z.w.data()); palabras += unsigned(z.w.size()); }
            bool bien = !img.empty();
            for (Trozo& z : img) bien = bien && r.verify_mem(z.dir, uint16_t(z.w.size()), z.w.data()) == 1;
            std::vector<uint16_t> mal = img.empty() ? std::vector<uint16_t>() : img[0].w;
            if (!mal.empty()) mal[0] ^= 1;
            const bool caza = !mal.empty() && r.verify_mem(img[0].dir, uint16_t(mal.size()), mal.data()) == 0;
            check(bien && caza, "WriteFLASH de g2_basico.elf (" + std::to_string(palabras) +
                                " palabras, TCLKstrobes(35)) y VerifyMem por PSA; un bit cambiado, la PSA no casa");
            uint16_t q[16], v[16];
            for (unsigned i = 0; i < 16; ++i) q[i] = uint16_t(0x1111 * (i + 1) ^ 0x0F0F);
            r.write_mem_quick(0x0300, 16, q);
            r.read_mem_quick(0x0300, 16, v);
            bool rapida = r.read_mem(true, 0x0300) == q[0];
            for (unsigned i = 0; i < 16; ++i) rapida = rapida && v[i] == q[i] && g2.mem16(0x0300 + 2 * i) == q[i];
            check(rapida, "WriteMemQuick y ReadMemQuick (DATA_QUICK): 16 palabras en la RAM, y ReadMem");
            // HaltCPU y ReleaseCPU: lo mismo que halt_cpu/release_cpu de la sonda
            r.hil().traza = &tr;
            r.halt_cpu(); r.release_cpu();
            r.hil().traza = nullptr;
            size_t k = 0;
            while (k < tr.size() && tr[k].rfind("I41", 0) != 0) ++k;     // sin SetInstrFetch
            const std::vector<std::string> rep = entradas(tr, k);
            r.release_device(0xFFFE);
            r.hil().tres_estados();
            g2.uart_tx.clear();
            wait(40, SC_MS);
            check(g2.uart_tx.rfind("hola G2553\n", 0) == 0,
                  "ReleaseDevice(V_RESET): el programa escrito por JTAG arranca («hola G2553» por la UART)");
            // La sonda (MSPDS), la misma operación
            SondaM430::Pines sp; sp.test = &g2.test; sp.rst = &g2.rst;
            SondaM430 s(sp, SondaM430::Modo::Sbw);
            s.conecta();
            s.sincroniza(false);
            std::vector<std::string> ts;
            s.traza = &ts;
            s.halt_cpu(); s.release_cpu();
            s.traza = nullptr;
            s.desconecta();
            check(!rep.empty() && rep == entradas(ts),
                  "HaltCPU/ReleaseCPU del Replicator y halt_cpu/release_cpu de la sonda: la misma secuencia (" +
                  std::to_string(rep.size()) + " transacciones)");
            wait(1, SC_MS);
        }
        {
            Replicador430 r(HilRep::Pines{&g2.p(1, 4), &g2.p(1, 5), &g2.p(1, 6), &g2.p(1, 7), &g2.test, &g2.rst}, false);
            r.hil().alimenta = [this](bool on) { g2.reset(!on); };
            r.init_target();
            const unsigned st = r.get_device();
            r.erase_flash(Replicador430::BORRA_SEGMENTO, 0x1000);
            uint16_t d[4] = {0xCAFE, 0x0430, 0x2553, 0x1234};
            r.write_flash(0x1000, 4, d);
            check(st == 1 && r.verify_mem(0x1000, 4, d) == 1 && g2.mem16(0x1000) == 0xCAFE && g2.mem16(0x1006) == 0x1234 &&
                  g2.mem16(0x1008) == 0xFFFF,
                  "JTAG de 4 hilos (P1.4-P1.7, entrada con RST abajo): GetDevice, el segmento D borrado, escrito y por PSA");
            r.release_device(0xFFFE);
            r.hil().tres_estados();
            wait(1, SC_MS);
        }
        {
            Replicador430 r(hp, true);
            r.hil().alimenta = [this](bool on) { g2.reset(!on); };
            r.init_target();
            const unsigned antes = r.get_device();
            const unsigned fundido = r.blow_fuse();
            const unsigned despues = r.get_device();
            check(antes == 1 && fundido == 1 && despues == 2 && g2.tap.fusible(),
                  "BlowFuse por SBW (VPP en TEST): GetDevice da STATUS_FUSEBLOWN (IsFuseBlown: 0xAAAA -> 0x5555)");
            r.release_target();
            g2.tap.restaura_fusible();
            g2.reset(false);
            wait(1, SC_MS);
        }
    }

    void run() {
        g1();
        g2_();
        g3();
        g4();
        g5();
        std::printf("\nTOTAL G2: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbG2 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
