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
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/g2553/soc_g2553.h"
#include "m430/sonda/sonda_m430.h"

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
    Mide mide(AnalogNet& n, sc_time cuanto) {
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

    void run() {
        g1();
        g2_();
        g3();
        g4();
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
