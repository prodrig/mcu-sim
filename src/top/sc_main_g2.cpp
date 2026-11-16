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
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/g2553/soc_g2553.h"
#include "m430/sonda/sonda_m430.h"
#include "verif/m430/replicator/replicador430.h"
#include "parts/ext_parts.h"
#include "parts/boosterpack.h"

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
    // G6: un OPT3001 en su propio par de líneas, unido a P1.6/P1.7 por dos
    // hilos de bus con sus pull-up que solo se conectan en G6 (antes, P1.6 y
    // P1.7 son también TDI y TDO del JTAG de 4 hilos)
    AnalogNet scl_n{"opt_scl"}, sda_n{"opt_sda"};
    Opt3001 opt{"opt3001", scl_n, sda_n, nullptr, 0x44, 300.0};
    I2cWire h_scl{"h_scl", {&g2.p(1, 6), &scl_n}}, h_sda{"h_sda", {&g2.p(1, 7), &sda_n}};
    int id_p10_ = -1, id_p13_ = -1, id_p20_ = -1, id_pu13_ = -1, id_rxd_ = -1;
    bool onda_ = false;
    // G7: lo que sale por P1.2 a 9600 8N1, leído en el PIN (la UART del
    // Timer_A no pasa por la USCI), como lo vería el eZ-FET
    std::string pin_tx_;
    bool lee_pin_ = false;
    void lector_proc() {
        const double tb = 1e6 / 9600.0;
        for (;;) {
            wait(g2.p(1, 2).value_changed_event());
            if (!lee_pin_ || g2.p(1, 2).voltage() > 1.6f) continue;
            wait(sc_time(tb * 1.5, SC_US));
            unsigned c = 0;
            for (unsigned k = 0; k < 8; ++k) {
                if (g2.p(1, 2).voltage() > 1.6f) c |= 1u << k;
                wait(sc_time(tb, SC_US));
            }
            pin_tx_.push_back(char(c));
        }
    }
    // Un carácter a 9600 8N1 por P1.1 (RXD), como el eZ-FET
    void manda_pin(uint8_t c) {
        const sc_time tb(1e6 / 9600.0, SC_US);
        auto pon = [&](bool v) { g2.p(1, 1).set_drive(id_rxd_, v ? 3.3f : 0.0f, 50.0f); wait(tb); };
        pon(false);
        for (unsigned k = 0; k < 8; ++k) pon((c >> k) & 1u);
        pon(true);
        g2.p(1, 1).set_drive(id_rxd_, 3.3f, 50.0f);
    }
    SC_CTOR(TbG2) {
        SC_THREAD(run);
        SC_THREAD(onda_proc);
        SC_THREAD(lector_proc);
        id_boton_ = g2.p(1, 3).register_driver("boton_s2"); g2.p(1, 3).set_hiz(id_boton_);
        id_p10_ = g2.p(1, 0).register_driver("tb_ca0"); g2.p(1, 0).set_hiz(id_p10_);
        id_p13_ = g2.p(1, 3).register_driver("tb_a3"); g2.p(1, 3).set_hiz(id_p13_);
        id_p20_ = g2.p(2, 0).register_driver("tb_1khz"); g2.p(2, 0).set_hiz(id_p20_);
        h_scl.set_enabled(false); h_sda.set_enabled(false);
        id_pu13_ = g2.p(1, 3).register_driver("tb_r6"); g2.p(1, 3).set_hiz(id_pu13_);
        id_rxd_ = g2.p(1, 1).register_driver("tb_rxd"); g2.p(1, 1).set_hiz(id_rxd_);
    }
    // 1 kHz en P2.0 mientras onda_ (G6)
    void onda_proc() {
        bool v = false;
        for (;;) {
            wait(500, SC_US);
            if (!onda_) { g2.p(2, 0).set_hiz(id_p20_); continue; }
            v = !v;
            g2.p(2, 0).set_drive(id_p20_, v ? 3.3f : 0.0f, 50.0f);
        }
    }

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

    void g6() {
        grupo("G6 g2_perif.c: USCI_B0 (SPI e I2C), Comp_A+, la DTC del ADC10, captura y PWM");
        h_scl.set_enabled(true); h_sda.set_enabled(true);
        g2.p(1, 0).set_drive(id_p10_, 1.0f, 10.0f);              // CA0 por debajo de Vcc/2
        g2.p(1, 3).set_drive(id_p13_, 2.0f, 10.0f);              // A3
        onda_ = true;
        corre("g2_perif.elf");
        // CAOUT a 0; luego CA0 sube y el flanco pide la interrupción
        for (int i = 0; i < 200 && g2.uart_tx.find("ca 0\n") == std::string::npos; ++i) wait(1, SC_MS);
        g2.p(1, 0).set_drive(id_p10_, 2.5f, 10.0f);
        for (int i = 0; i < 300 && g2.uart_tx.find("cap ") == std::string::npos; ++i) wait(1, SC_MS);
        wait(10, SC_MS);
        const std::string s = g2.uart_tx;
        auto linea = [&](const char* c) {
            const size_t i = s.find(c);
            return i == std::string::npos ? std::string() : s.substr(i, s.find('\n', i) - i);
        };
        check(linea("spi") == "spi A5 3C 81", "SPI maestro con UCLISTEN: A5 3C 81 vuelven (" + linea("spi") + ")");
        check(linea("i2c") == "i2c 5449 3001 nack", "I2C maestro: el OPT3001 da 0x5449 y 0x3001; 0x45 da NACK (" + linea("i2c") + ")");
        check(opt.lecturas() == 2, "el OPT3001 ha servido dos registros");
        check(linea("ca ") == "ca 0" && linea("ca irq") == "ca irq 1",
              "Comp_A+: CA0 a 1 V contra Vcc/2 da 0; a 2,5 V, el flanco pide su interrupción y CAOUT = 1");
        unsigned v[4] = {0, 0, 0, 0}; char mas = 0;
        const std::string l = linea("adc");
        std::sscanf(l.c_str(), "adc %u %u %u %u%c", &v[0], &v[1], &v[2], &v[3], &mas);
        check(std::abs(int(v[0]) - 620) <= 2 && std::abs(int(v[3]) - 775) <= 2 && mas == '+',
              "ADC10 + DTC: A3..A0 a la RAM en un bloque (A3 = 2 V: 620; A0 = 2,5 V: 775) y ADC10IFG (" + l + ")");
        unsigned cap = 0;
        std::sscanf(linea("cap").c_str(), "cap %u", &cap);
        char t[160];
        std::snprintf(t, sizeof t, "Timer1_A3 captura en CCI0A (P2.0) un 1 kHz: %u cuentas de SMCLK (1 MHz)", cap);
        check(cap >= 995 && cap <= 1005, t);
        const Mide m = mide(g2.p(1, 6), sc_time(4, SC_MS));
        double alto = 0, per = 0;
        for (size_t i = 0; i + 2 < m.t.size(); ++i)
            if (m.v[i]) { alto = (m.t[i + 1] - m.t[i]).to_seconds() * 1e6; per = (m.t[i + 2] - m.t[i]).to_seconds() * 1e6; break; }
        std::snprintf(t, sizeof t, "TA0.1 en P1.6 (OUTMOD_7, CCR1 = 250 de 1000): %.1f us de %.1f us", alto, per);
        check(std::fabs(per - 1001.0) < 5.0 && std::fabs(alto / per - 0.25) < 0.01, t);
        onda_ = false;
        h_scl.set_enabled(false); h_sda.set_enabled(false);
        g2.p(1, 0).set_hiz(id_p10_); g2.p(1, 3).set_hiz(id_p13_);
    }

    // La fracción del tiempo que un pin está arriba
    double alto(analog_net_if& n, sc_time cuanto) {
        const sc_time fin = sc_time_stamp() + cuanto;
        sc_time t0 = sc_time_stamp(), a = SC_ZERO_TIME;
        bool v = n.voltage() > 1.6f;
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), n.value_changed_event());
            if (v) a += sc_time_stamp() - t0;
            t0 = sc_time_stamp();
            v = n.voltage() > 1.6f;
        }
        return a.to_seconds() / cuanto.to_seconds();
    }
    void g7() {
        grupo("G7 los ejemplos de la G2ET y la demo de fábrica (verif/g2553/ejemplos)");
        // Blink
        corre("ej_blink.elf");
        const Mide b = mide(g2.p(1, 0), sc_time(300, SC_MS));
        // el periodo, de los dos últimos semiperiodos (el primer flanco puede
        // ser el del arranque)
        const size_t nb = b.t.size();
        double per = nb >= 4 ? (b.t[nb - 1] - b.t[nb - 3]).to_seconds() * 1e3 : 0.0;
        char t[200];
        std::snprintf(t, sizeof t, "ej_blink: P1.0 cambia en un bucle de 10000 vueltas: periodo %.1f ms con el DCO de reset (MCLK %.2f MHz)", per, g2.f_mclk() / 1e6);
        check(nb >= 4 && per > 120.0 && per < 135.0, t);
        // USCI: eco
        corre("ej_uart_usci.elf");
        wait(2, SC_MS);
        for (char c : std::string("eco")) { g2.uart_rx(uint8_t(c)); wait(2, SC_MS); }
        wait(3, SC_MS);
        check(g2.uart_tx == "eco", "ej_uart_usci: USCI_A0 a 9600, eco con LPM0 y la interrupción de RX («" + g2.uart_tx + "»)");
        // Timer_A UART
        g2.p(1, 1).set_drive(id_rxd_, 3.3f, 50.0f);
        lee_pin_ = true; pin_tx_.clear();
        corre("ej_uart_ta.elf");
        wait(15, SC_MS);
        const std::string saludo = pin_tx_;
        manda_pin('k'); wait(3, SC_MS); manda_pin('9'); wait(3, SC_MS);
        check(saludo == "TA UART\r\n" && pin_tx_ == "TA UART\r\nk9",
              "ej_uart_ta: la UART del Timer_A (TXD = TA0.1 en P1.2, RXD = CCI0A en P1.1): el saludo y el eco, leídos en los pines");
        lee_pin_ = false;
        g2.p(1, 1).set_hiz(id_rxd_);
        // ADC10 y temperatura
        g2.temperatura = 25.0;
        corre("ej_adc_temp.elf");
        wait(130, SC_MS);
        g2.temperatura = 40.0;
        wait(250, SC_MS);
        const std::string at = g2.uart_tx;
        g2.temperatura = 25.0;
        check(at.rfind("T 24\r\n", 0) == 0 && at.find("T 39\r\n") != std::string::npos,
              "ej_adc_temp: el sensor por el ADC10 con 1,5 V: 25 °C da «T 24» y 40 °C, «T 39» (la fórmula de TI, que trunca)");
        // La demo de fábrica
        g2.p(1, 3).set_drive(id_pu13_, 3.3f, 47000.0f);            // R6
        lee_pin_ = true; pin_tx_.clear();
        corre("oobe.elf");
        const Mide l1 = mide(g2.p(1, 0), sc_time(800, SC_MS));
        const double r0 = alto(g2.p(1, 6), sc_time(1, SC_MS));
        check(l1.t.size() >= 4 && pin_tx_.empty(), "la demo: al arrancar, los dos LED se turnan cada 250 ms y la UART calla");
        (void)r0;
        g2.p(1, 3).set_drive(id_boton_, 0.0f, 10.0f);              // S1
        wait(5, SC_MS);
        g2.p(1, 3).set_hiz(id_boton_);
        wait(700, SC_MS);
        check(pin_tx_.find("24\r\n24\r\n") != std::string::npos,
              "la demo: S1 pasa a «Live Temperature» y manda la temperatura por la UART del Timer_A (P1.2): «" +
              pin_tx_.substr(0, 12) + "»");
        g2.temperatura = 30.0;
        wait(600, SC_MS);
        const double v_alto = alto(g2.p(1, 0), sc_time(20, SC_MS)), r_alto = alto(g2.p(1, 6), sc_time(20, SC_MS));
        std::snprintf(t, sizeof t, "la demo: a 30 °C (referencia 24) luce el verde (P1.0 %.0f %%) y no el rojo (%.0f %%)",
                      v_alto * 100, r_alto * 100);
        check(v_alto > 0.9 && r_alto < 0.01, t);
        g2.temperatura = 20.0;
        wait(600, SC_MS);
        const double v2 = alto(g2.p(1, 0), sc_time(20, SC_MS)), r2 = alto(g2.p(1, 6), sc_time(20, SC_MS));
        std::snprintf(t, sizeof t, "la demo: a 20 °C luce el rojo (P1.6 %.0f %%) y no el verde (%.0f %%)", r2 * 100, v2 * 100);
        check(r2 > 0.9 && v2 < 0.01, t);
        g2.temperatura = 25.0;
        lee_pin_ = false;
        g2.p(1, 3).set_hiz(id_pu13_);
    }

    void run() {
        // TESTG2_SOLO=67 corre solo G6 y G7 (para depurar; el invariante es sin él)
        const char* solo = std::getenv("TESTG2_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) g1();
        if (va('2')) g2_();
        if (va('3')) g3();
        if (va('4')) g4();
        if (va('5')) g5();
        if (va('6')) g6();
        if (va('7')) g7();
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
