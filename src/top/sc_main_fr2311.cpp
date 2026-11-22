// =============================================================================
// sc_main_fr2311.cpp — LA SUITE DEL MSP430FR2311 (fase M7 de doc/analisis_msp430.md)
//
//   H1  el SoC contra su descriptor generado (vectores por nombre, mapa sin
//       memoria de información, JTAG ID) y la TLV de fábrica (0x82F0)
//   H2  fr23_basico.c con la DriverLib de TI y una red en el banco: el FLL a
//       8 MHz y la UCA0 en P1.7; el Timer_B (CNTL de 8 bits, TB1.1 en P2.0 con
//       el latch de CLLD = 1); el eCOMP0 contra su DAC (los dos búferes) y su
//       interrupción; el SAC0 en modo GP con ganancia 3 por resistencias del
//       banco; y el TIA0 con Rf y una corriente del banco
//   H3  la demo de fábrica de TI (su binario TI-TXT) con el fotodiodo D1 y R3
//       como en la LaunchPad: la luz que ve el SAC0 en transimpedancia, el ADC
//       y el PWM de LED2 (TB1.1 en P2.0)
//   H4  el BSL B1 (M10): el chip en blanco entra solo; la UART de P1.6/P1.7,
//       la contraseña mala, y la versión 00.08.35.B3
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/fr2311/soc_fr2311.h"
#include "parts/ext_parts.h"
#include "parts/fotodiodo.h"
#include "top/banco_bsl_fram.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/fr2311/elf/";

SC_MODULE(TbFr2311) {
    SocFR2311 fr{"fr2311"};
    banco_bsl::Uart bsl_{"bsl_uart", fr.p(1, 7), fr.p(1, 6)};
    AnalogNet gnd{"gnd"};
    // La red del banco: 20k de OA0O (P1.3) a OA0- (P1.2) y 10k de OA0- a masa
    // (ganancia 3); 1 Mohm de TRI0O (P1.5) a TRI0- (P1.6) y la corriente; y la
    // de la LaunchPad: R3 (2,37 Mohm) de OA0O a OA0- y el fotodiodo D1
    Resistencia r20k{"r20k", fr.p(1, 3), fr.p(1, 2), 20000.0};
    Resistencia r10k{"r10k", fr.p(1, 2), gnd, 10000.0};
    Resistencia rf{"rf", fr.p(1, 5), fr.p(1, 6), 1e6};
    Fotodiodo itia{"itia", gnd, fr.p(1, 6), 1.0, 1000.0};       // 1 uA
    Resistencia r3{"r3", fr.p(1, 3), fr.p(1, 2), 2.37e6};
    Fotodiodo d1{"d1", gnd, fr.p(1, 2), 5.0, 0.0};
    int d_gnd_ = -1, d_c0_ = -1, d_mas_ = -1, d_tri_ = -1, d_s1_ = -1;

    SC_CTOR(TbFr2311) {
        SC_THREAD(run);
        SC_METHOD(pwm_proc); sensitive << fr.p(2, 0).value_changed_event(); dont_initialize();
        SC_METHOD(led1_proc); sensitive << fr.p(1, 0).value_changed_event(); dont_initialize();
        d_gnd_ = gnd.register_driver("gnd"); gnd.set_drive(d_gnd_, 0.0f, 0.01f);
        const int d = fr.rst.register_driver("r1"); fr.rst.set_drive(d, 3.3f, 47000.0f);
        d_c0_ = fr.p(1, 0).register_driver("c0");
        d_mas_ = fr.p(1, 4).register_driver("oa0mas");
        d_tri_ = fr.p(1, 7).register_driver("tri0mas");
        d_s1_ = fr.p(1, 1).register_driver("S1"); fr.p(1, 1).set_hiz(d_s1_);
        fr.p(1, 0).set_hiz(d_c0_); fr.p(1, 4).set_hiz(d_mas_); fr.p(1, 7).set_hiz(d_tri_);
        // Al empezar, la red del banco; ni la de la LaunchPad ni la del TIA
        rf.set_enabled(false); itia.set_enabled(false); r3.set_enabled(false); d1.set_enabled(false);
    }
    // El PWM de P2.0: el tiempo en alto entre dos marcas
    sc_time t_alto_{SC_ZERO_TIME}, t_sube_{SC_ZERO_TIME};
    bool alto_ = false;
    unsigned subidas_ = 0;
    void pwm_proc() {
        const bool a = alto(2, 0);
        if (a && !alto_) { t_sube_ = sc_time_stamp(); ++subidas_; }
        if (!a && alto_) t_alto_ += sc_time_stamp() - t_sube_;
        alto_ = a;
    }
    double ciclo(const sc_time& dur) {
        t_alto_ = SC_ZERO_TIME; t_sube_ = sc_time_stamp();
        const sc_time t0 = sc_time_stamp();
        wait(dur);
        sc_time h = t_alto_;
        if (alto_) h += sc_time_stamp() - t_sube_;
        return h.to_seconds() / (sc_time_stamp() - t0).to_seconds();
    }
    unsigned led1_ = 0;
    bool led1_ant_ = false;
    void led1_proc() { const bool a = alto(1, 0); if (a && !led1_ant_) ++led1_; led1_ant_ = a; }

    void corre(const std::string& ruta) {
        fr.reset(true);
        wait(1, SC_US);
        const std::string e = fr.carga(ruta);
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        fr.uart_tx.clear();
        fr.reset(false);
    }
    std::string linea(const std::string& s, const char* c) {
        const size_t i = s.find(c);
        return i == std::string::npos ? std::string() : s.substr(i, s.find('\n', i) - i);
    }
    bool espera_uart(const char* c, int ms) {
        auto entera = [&] { const size_t i = fr.uart_tx.find(c); return i != std::string::npos && fr.uart_tx.find('\n', i) != std::string::npos; };
        for (int i = 0; i < ms * 10 && !entera(); ++i) wait(100, SC_US);
        return entera();
    }
    bool alto(unsigned p, unsigned b) { return !fr.p(p, b).floating() && fr.p(p, b).voltage() > 1.6f; }
    void escribe16(uint32_t a, uint16_t v) {
        tlm::tlm_generic_payload gp; uint8_t d[2] = {uint8_t(v), uint8_t(v >> 8)};
        gp.set_command(tlm::TLM_WRITE_COMMAND); gp.set_address(a); gp.set_data_ptr(d); gp.set_data_length(2);
        fr.acceso(gp, true);
    }
    long num(const std::string& l, const char* tras) {
        const size_t i = l.find(tras);
        return i == std::string::npos ? -1 : std::strtol(l.c_str() + i + std::strlen(tras), nullptr, 10);
    }

    void h1() {
        grupo("H1 el SoC contra el descriptor generado y la TLV, sin tiempo");
        const m430::Desc430& d = SocFR2311::DESC;
        check(d.jtag_id == 0x98 && d.ram.ini == 0x2000 && d.ram.fin == 0x23FF && d.principal.ini == 0xF100 &&
              !d.info.existe() && d.mpy == m430::Mpy::Ninguno,
              "el descriptor: JTAG ID 0x98, RAM 0x2000-0x23FF, FRAM desde 0xF100, sin memoria de información y sin MPY");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"ECOMP0", 0xFFE2}, {"PORT2", 0xFFE4}, {"PORT1", 0xFFE6}, {"ADC", 0xFFE8}, {"EUSCI_B0", 0xFFEA},
                       {"EUSCI_A0", 0xFFEC}, {"WDT", 0xFFEE}, {"RTC", 0xFFF0}, {"TIMER1_B1", 0xFFF2}, {"TIMER1_B0", 0xFFF4},
                       {"TIMER0_B1", 0xFFF6}, {"TIMER0_B0", 0xFFF8}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 12 vectores que usa el SoC, por su nombre en msp430fr2311.h (el eCOMP0 en 0xFFE2)");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a <= 0x1A77; ++a) {
            const uint8_t b = fr.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(fr.mem16(0x1A04) == 0x82F0 && d.id_dispositivo == 0x82F0 && fr.mem16(0x1A02) == c,
              "la TLV: el ID 0x82F0 y su CRC-CCITT de 0x1A04 a 0x1A77 [DS SLASE58E tablas 6-45 y 6-46]");
    }

    void h2() {
        grupo("H2 fr23_basico.c: FLL, UART, Timer_B, eCOMP0, SAC0 y TIA0 con la red del banco");
        // La red: C0 a 1,2 V, OA0+ a 0,5 V
        fr.p(1, 0).set_drive(d_c0_, 1.2f, 10.0f);
        fr.p(1, 4).set_drive(d_mas_, 0.5f, 10.0f);
        corre(std::string(DIR) + "fr23_basico.elf");
        const bool ok = espera_uart("hola FR2311", 1000);
        check(ok && linea(fr.uart_tx, "hola") == "hola FR2311 7995 kHz id 82F0", "el FLL a 8 MHz, la UCA0 en P1.7 (" + linea(fr.uart_tx, "hola") + ")");
        const bool t0 = espera_uart("tb0 cntl8", 100);
        const long n = num(linea(fr.uart_tx, "tb0"), "cntl8 ");
        check(t0 && n >= 38 && n <= 40, "Timer_B con CNTL = 8 bits a 1 MHz: " + std::to_string(n) + " desbordamientos (TBIFG) en 10 ms (uno cada 256 us)");
        espera_uart("tb1 25", 100);
        const double c25 = ciclo(sc_time(3, SC_MS));
        espera_uart("tb1 ccr1", 100);
        const double c75 = ciclo(sc_time(3, SC_MS));
        char t[200];
        std::snprintf(t, sizeof t, "TB1.1 en P2.0 (reset/set, CLLD = 1): %.1f %% y, con TB1CCR1 = 750, %.1f %% (%s)",
                      100 * c25, 100 * c75, linea(fr.uart_tx, "tb1 ccr1").c_str());
        check(std::fabs(c25 - 0.25) < 0.02 && std::fabs(c75 - 0.75) < 0.02 && linea(fr.uart_tx, "tb1 ccr1") == "tb1 ccr1 750", t);
        const bool e = espera_uart("ecomp buf1", 100);
        check(e && linea(fr.uart_tx, "ecomp buf1") == "ecomp buf1 1 buf2 0",
              "el eCOMP0: C0 = 1,2 V contra el DAC de VDD con CPDACBUF1 = 20 (1,03 V) y CPDACBUF2 = 30 (1,55 V): " + linea(fr.uart_tx, "ecomp buf1"));
        espera_uart("ecomp espera", 100);
        wait(1, SC_MS);
        fr.p(1, 0).set_drive(d_c0_, 2.0f, 10.0f);
        const bool ei = espera_uart("ecomp irq", 50);
        check(ei && linea(fr.uart_tx, "ecomp irq") == "ecomp irq 1 out 1", "el banco sube C0 a 2 V: CPIFG por CPxIV (" + linea(fr.uart_tx, "ecomp irq") + ")");
        const bool s = espera_uart("sac adc", 100);
        const long a = num(linea(fr.uart_tx, "sac adc"), "sac adc ");
        std::snprintf(t, sizeof t, "el SAC0 en GP, OA0+ a 0,5 V con 20k/10k (ganancia 3): A3 da %ld (1,5 V de 3,3: 465)", a);
        check(s && std::labs(a - 465) <= 3, t);
        // El TIA: suelta la UART, pone la red, y la quita
        espera_uart("tia", 100);
        wait(10, SC_MS);
        fr.suelta_rx(true);
        rf.set_enabled(true); itia.set_enabled(true);
        fr.p(1, 7).set_drive(d_tri_, 0.3f, 10.0f);
        wait(20, SC_MS);
        fr.p(1, 7).set_hiz(d_tri_);
        rf.set_enabled(false); itia.set_enabled(false);
        fr.suelta_rx(false);
        const bool ti = espera_uart("tia adc", 100);
        const long b = num(linea(fr.uart_tx, "tia adc"), "tia adc ");
        std::snprintf(t, sizeof t, "el TIA0: TRI0+ a 0,3 V, 1 uA en TRI0- y Rf = 1 Mohm: A5 da %ld (1,3 V: 403)", b);
        check(ti && std::labs(b - 403) <= 4, t);
        check(espera_uart("fin", 50), "el programa acaba en LPM0");
        // TB25 [SLAZ679R]: en ascendente, CLLD = 01 carga el latch en el acto;
        // en continuo, cuando TB1R pasa por 0 (cada 8,2 ms a 8 MHz)
        escribe16(0x3C0, 0x0004);                                 // TB1CLR
        escribe16(0x3D2, 60000);                                  // TB1CCR0
        escribe16(0x3C4, 0x0200);                                 // TB1CCTL1: CLLD = 01
        escribe16(0x3C0, 0x0210);                                 // SMCLK, ascendente
        wait(100, SC_US);
        escribe16(0x3D4, 100);
        const unsigned l1 = fr.timer(1).ccr(1);
        escribe16(0x3C0, 0x0220);                                 // continuo
        wait(100, SC_US);
        escribe16(0x3D4, 200);
        const unsigned l2 = fr.timer(1).ccr(1);
        wait(10, SC_MS);
        const unsigned l3 = fr.timer(1).ccr(1);
        escribe16(0x3C0, 0);
        std::snprintf(t, sizeof t, "TB25: en ascendente el latch de CCR1 toma 100 en el acto (%u); en continuo, 200 espera al paso por 0 (%u, luego %u)", l1, l2, l3);
        check(l1 == 100 && l2 == 100 && l3 == 200, t);
        fr.p(1, 0).set_hiz(d_c0_);
        fr.p(1, 4).set_hiz(d_mas_);
    }

    void h3() {
        grupo("H3 la demo de fábrica de TI (su binario): la luz en el fotodiodo, el SAC0, el ADC y el PWM de LED2");
        // La LaunchPad: OA0+ a masa (J9), R3 y D1 en OA0- (J7, J8)
        r20k.set_enabled(false); r10k.set_enabled(false);
        r3.set_enabled(true); d1.set_enabled(true);
        fr.p(1, 4).set_drive(d_mas_, 0.0f, 0.1f);
        d1.pon_luz(0.0);
        const unsigned l0 = led1_;
        corre("verif/fw/fr2311/OutOfBox_MSP430FR2311.txt");
        wait(300, SC_MS);
        const double c0 = ciclo(sc_time(100, SC_MS));
        check(led1_ - l0 == 1 && c0 < 0.01, "LED1 da un destello al empezar; a oscuras, LED2 apagado (" + std::to_string(int(100 * c0)) + " %)");
        d1.pon_luz(50.0);
        wait(300, SC_MS);
        const double c50 = ciclo(sc_time(100, SC_MS));
        // 50 lx: 0,25 uA por 2,37 Mohm = 0,593 V; el ADC (AVCC) da 184, la demo lo parte por 2: 92 de 256
        char t[200];
        std::snprintf(t, sizeof t, "50 lx (0,25 uA en R3): LED2 al %.1f %% (0,593 V, ADC 184 / 2 = 92 de 256: 35,9 %%)", 100 * c50);
        check(std::fabs(c50 - 92.0 / 256.0) < 0.02, t);
        d1.pon_luz(300.0);
        wait(300, SC_MS);
        const double c300 = ciclo(sc_time(100, SC_MS));
        std::snprintf(t, sizeof t, "300 lx: el OA llega al raíl, el ADC satura y LED2 se queda en 255 de 256: %.1f %%", 100 * c300);
        check(c300 > 0.98, t);
    }


    void h4() {
        grupo("H4 el BSL B1 (M10) del FR2311: el chip en blanco entra solo; la UART de P1.6/P1.7 y la versión");
        corre(std::string(DIR) + "fr23_basico.elf");
        wait(5, SC_MS);
        const int drst = fr.rst.register_driver("bsl_rst"), dtest = fr.test.register_driver("bsl_test");
        fr.test.set_drive(dtest, 0.0f, 10.0f);
        bsl_.conecta(true);
        // El chip en blanco: el vector de reset a 0xFFFF y un reset sin la secuencia [BSLFRAM §3.3.3]
        fr.poke(0xFFFE, 0xFF); fr.poke(0xFFFF, 0xFF);
        const unsigned v0 = fr.bsl_vacios;
        fr.rst.set_drive(drst, 0.0f, 10.0f); wait(1, SC_MS); fr.rst.set_drive(drst, 3.3f, 10.0f);
        std::vector<uint8_t> v;
        wait(30, SC_MS);
        const uint16_t antes = fr.mem16(0xF100);
        int a = bsl_.orden(banco_bsl::contrasena(0x00), &v);
        const bool mala = a == 0 && v == std::vector<uint8_t>({0x3B, 0x05}) && antes != 0xFFFF && fr.mem16(0xF100) == 0xFFFF;
        a = bsl_.orden(banco_bsl::contrasena(0xFF), &v);
        const int b = bsl_.orden({0x19}, &v);
        check(fr.bsl_vacios == v0 + 1 && mala && a == 0 && b == 0 && v == std::vector<uint8_t>({0x3A, 0x00, 0x08, 0x35, 0xB3}),
              "el vector en blanco lo arranca; la contraseña mala borra desde 0xF100; la versión 00.08.35.B3 [tabla 7-1]");
        bsl_.conecta(false);
        fr.rst.set_hiz(drst); fr.test.set_hiz(dtest);
    }

    void run() {
        const char* solo = std::getenv("TESTFR2311_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) h1();
        if (va('2')) h2();
        if (va('3')) h3();
        if (va('4')) h4();
        std::printf("\nTOTAL FR2311: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbFr2311 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
