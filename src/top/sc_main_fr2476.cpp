// =============================================================================
// sc_main_fr2476.cpp — LA SUITE DEL MSP430FR2476 (fase M7 de doc/analisis_msp430.md)
//
//   K1  el SoC contra su descriptor generado (vectores por nombre, la FRAM
//       hasta 0x17FFF, MPY32) y la TLV de fábrica (0x832A)
//   K2  fr247_basico.c (-mlarge): el FLL a 16 MHz y la UCA0 en P1.4; código y
//       tabla por encima de 0x10000, y PFWP también allí; el ADC de 12 bits
//       y REFVSEL = 2,5 V; TA2.1 en P3.3 y, con TA2RMP, en P5.7; TB0.6 (el
//       Timer0_B7) en P4.4; el eCOMP0 contra la de 1,2 V; la interrupción de
//       P6
//   K3  la demo de fábrica de TI (su binario TI-TXT) con la red de la
//       LaunchPad: el TMP235 en A1 con la referencia de 1,5 V, el PWM por
//       programa del LED RGB (verde a la temperatura de la calibración, rojo
//       por encima), S2 manda la temperatura por la UART y S1 recalibra
//   K4  el BSL B1 (M10) por I2C, con la configuración del usuario de
//       fr247_bsl_i2c.c (0xFF88: solo I2C, sin borrar la RAM, en la 0x60): el
//       arranque de unos 300 ms, la UART que no contesta, la 0x48 tampoco, la
//       contraseña, la versión 00.09.36.B5, la RAM que sigue, escribir por
//       encima de 0x10000 con su CRC, y Load PC
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/fr2476/soc_fr2476.h"
#include "parts/ext_parts.h"
#include "parts/tmp235.h"
#include "top/banco_bsl_fram.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/fr2476/elf/";

SC_MODULE(TbFr2476) {
    SocFR2476 fr{"fr2476"};
    AnalogNet gnd{"gnd"}, v33{"v33"};
    // El TMP235 (U1) con VDD a 3V3 por J9 y la salida en A1 (P1.1)
    Tmp235 u1{"u1", v33, fr.p(1, 1), 25.0};
    banco_bsl::Uart bsl_u_{"bsl_uart", fr.p(1, 4), fr.p(1, 5)};
    banco_bsl::I2c bsl_i_{"bsl_i2c", fr.p(1, 3), fr.p(1, 2)};
    int d_gnd_ = -1, d_v33_ = -1, d_p11_ = -1, d_p61_ = -1, d_s1_ = -1, d_s2_ = -1;

    SC_CTOR(TbFr2476) {
        SC_THREAD(run);
        SC_METHOD(pwm_proc);
        for (const Pwm& x : pw_) sensitive << fr.p(x.p, x.b).value_changed_event();
        dont_initialize();
        d_gnd_ = gnd.register_driver("gnd"); gnd.set_drive(d_gnd_, 0.0f, 0.01f);
        d_v33_ = v33.register_driver("3v3"); v33.set_drive(d_v33_, 3.3f, 0.01f);
        const int d = fr.rst.register_driver("r11"); fr.rst.set_drive(d, 3.3f, 47000.0f);
        d_p11_ = fr.p(1, 1).register_driver("banco"); fr.p(1, 1).set_hiz(d_p11_);
        d_p61_ = fr.p(6, 1).register_driver("banco"); fr.p(6, 1).set_hiz(d_p61_);
        d_s1_ = fr.p(4, 0).register_driver("S1"); fr.p(4, 0).set_hiz(d_s1_);
        d_s2_ = fr.p(2, 3).register_driver("S2"); fr.p(2, 3).set_hiz(d_s2_);
        u1.set_enabled(false);
    }
    // El tiempo en alto de P3.3, P5.7, P4.4 y de los tres del LED RGB: P5.1
    // (rojo), P5.0 (verde) y P4.7 (azul)
    struct Pwm { unsigned p, b; sc_time alto{SC_ZERO_TIME}, sube{SC_ZERO_TIME}; bool a = false; unsigned flancos = 0; };
    Pwm pw_[6] = {{3, 3}, {5, 7}, {4, 4}, {5, 1}, {5, 0}, {4, 7}};
    void pwm_proc() {
        for (Pwm& x : pw_) {
            const bool a = alto(x.p, x.b);
            if (a && !x.a) { x.sube = sc_time_stamp(); ++x.flancos; }
            if (!a && x.a) x.alto += sc_time_stamp() - x.sube;
            x.a = a;
        }
    }
    void ciclos(const sc_time& dur, double c[6], unsigned f[6] = nullptr) {
        for (Pwm& x : pw_) { x.alto = SC_ZERO_TIME; x.sube = sc_time_stamp(); x.flancos = 0; }
        const sc_time t0 = sc_time_stamp();
        wait(dur);
        for (unsigned k = 0; k < 6; ++k) {
            sc_time h = pw_[k].alto;
            if (pw_[k].a) h += sc_time_stamp() - pw_[k].sube;
            c[k] = h.to_seconds() / (sc_time_stamp() - t0).to_seconds();
            if (f) f[k] = pw_[k].flancos;
        }
    }

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
    long num(const std::string& l, const char* tras) {
        const size_t i = l.find(tras);
        return i == std::string::npos ? -1 : std::strtol(l.c_str() + i + std::strlen(tras), nullptr, 10);
    }
    void pulsa(int d, unsigned p, unsigned b, int ms) {
        fr.p(p, b).set_drive(d, 0.0f, 10.0f);
        wait(ms, SC_MS);
        fr.p(p, b).set_hiz(d);
    }

    void k1() {
        grupo("K1 el SoC contra el descriptor generado y la TLV, sin tiempo");
        const m430::Desc430& d = SocFR2476::DESC;
        check(d.jtag_id == 0x98 && d.ram.ini == 0x2000 && d.ram.fin == 0x3FFF && d.principal.ini == 0x8000 &&
              d.principal2.ini == 0x10000 && d.principal2.fin == 0x17FFF && d.mpy == m430::Mpy::Mpy32,
              "el descriptor: JTAG ID 0x98, RAM 0x2000-0x3FFF, FRAM 0x8000-0xFFFF y 0x10000-0x17FFF, MPY32");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"ECOMP0", 0xFFCA}, {"PORT6", 0xFFCC}, {"PORT5", 0xFFCE}, {"PORT4", 0xFFD0}, {"PORT3", 0xFFD2},
                       {"PORT2", 0xFFD4}, {"PORT1", 0xFFD6}, {"ADC", 0xFFD8}, {"EUSCI_B1", 0xFFDA}, {"EUSCI_B0", 0xFFDC},
                       {"EUSCI_A1", 0xFFDE}, {"EUSCI_A0", 0xFFE0}, {"WDT", 0xFFE2}, {"RTC", 0xFFE4},
                       {"TIMER0_B1", 0xFFE6}, {"TIMER0_B0", 0xFFE8}, {"TIMER3_A1", 0xFFEA}, {"TIMER3_A0", 0xFFEC},
                       {"TIMER2_A1", 0xFFEE}, {"TIMER2_A0", 0xFFF0}, {"TIMER1_A1", 0xFFF2}, {"TIMER1_A0", 0xFFF4},
                       {"TIMER0_A1", 0xFFF6}, {"TIMER0_A0", 0xFFF8}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 24 vectores que usa el SoC, por su nombre en msp430fr2476.h (PORT1-PORT6, todos)");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a <= 0x1AF7; ++a) {
            const uint8_t b = fr.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(fr.mem16(0x1A04) == 0x832A && d.id_dispositivo == 0x832A && fr.mem16(0x1A02) == c,
              "la TLV: el ID 0x832A y su CRC-CCITT de 0x1A04 a 0x1AF7 [DS SLASEO7C tablas 9-29 y 9-30]");
    }

    void k2() {
        grupo("K2 fr247_basico.c: FLL a 16 MHz, FRAM alta, ADC de 12 bits, TA2 reasignado, TB0.6, eCOMP0 y P6");
        fr.p(1, 1).set_drive(d_p11_, 1.0f, 10.0f);
        corre(std::string(DIR) + "fr247_basico.elf");
        const bool ok = espera_uart("hola FR2476", 1000);
        check(ok && linea(fr.uart_tx, "hola") == "hola FR2476 id 832A" && std::fabs(fr.f_mclk() - 16e6) < 0.1e6,
              "el FLL a 16 MHz desde REFO (NWAITS = 1), la UCA0 en P1.4 a 115200 (" + linea(fr.uart_tx, "hola") + ")");
        espera_uart("alta pfwp", 100);
        check(linea(fr.uart_tx, "alta f") == "alta f 1000A t 10000 suma E258",
              "código en 0x1000A y una tabla en 0x10000, por encima de 0x10000: CALLA y lecturas de 20 bits (" + linea(fr.uart_tx, "alta f") + ")");
        check(linea(fr.uart_tx, "alta pfwp") == "alta pfwp FFFF sin 2476",
              "0x17F00: con PFWP no se escribe; sin él, sí (" + linea(fr.uart_tx, "alta pfwp") + ")");
        espera_uart("adc vcc", 100);
        check(linea(fr.uart_tx, "adc vcc") == "adc vcc 4095 ref 3102",
              "el ADC de 12 bits: DVCC 4095 y la referencia con REFVSEL = 10 (2,5 V) 3102 (" + linea(fr.uart_tx, "adc vcc") + ")");
        espera_uart("ta2 def", 100);
        wait(2, SC_MS);
        double c[6];
        unsigned f[6];
        ciclos(sc_time(5, SC_MS), c, f);
        char t[260];
        std::snprintf(t, sizeof t, "TA2.1 en P3.3 (P3SEL = 01): %.1f %%; P5.7 quieto (%u flancos)", 100 * c[0], f[1]);
        check(std::fabs(c[0] - 0.25) < 0.01 && f[1] == 0, t);
        espera_uart("ta2 rmp", 100);
        wait(2, SC_MS);
        ciclos(sc_time(5, SC_MS), c, f);
        std::snprintf(t, sizeof t, "con TA2RMP (SYSCFG3 bit 2), TA2.1 pasa a P5.7: %.1f %%; P3.3 quieto (%u flancos)", 100 * c[1], f[0]);
        check(std::fabs(c[1] - 0.25) < 0.01 && f[0] == 0, t);
        espera_uart("tb0", 100);
        wait(2, SC_MS);
        ciclos(sc_time(5, SC_MS), c);
        std::snprintf(t, sizeof t, "TB0.6 en P4.4 (P4SEL = 10, el Timer0_B7): %.1f %%", 100 * c[2]);
        check(std::fabs(c[2] - 0.10) < 0.01, t);
        espera_uart("cp ", 100);
        check(linea(fr.uart_tx, "cp ") == "cp 0 espera", "el eCOMP0: P1.1 a 1,0 V contra la de 1,2 V da 0");
        wait(1, SC_MS);
        fr.p(1, 1).set_drive(d_p11_, 1.5f, 10.0f);
        check(espera_uart("cp sube", 20), "el banco sube P1.1 a 1,5 V: CPOUT a 1");
        fr.p(1, 1).set_hiz(d_p11_);
        espera_uart("p6 espera", 100);
        wait(1, SC_MS);
        pulsa(d_p61_, 6, 1, 1);
        const bool p6 = espera_uart("p6 irq", 20);
        check(p6 && linea(fr.uart_tx, "p6 irq") == "p6 irq 1 iv 4",
              "P6.1 a masa: PORT6 y P6IV = 4 (" + linea(fr.uart_tx, "p6 irq") + ")");
        check(espera_uart("fin", 50), "el programa acaba en LPM0");
    }

    void k3() {
        grupo("K3 la demo de fábrica de TI (su binario): el TMP235, el LED RGB, S1 y S2");
        u1.set_enabled(true);
        u1.pon_temperatura(25.0);
        corre("verif/fw/fr2476/OutOfBox_LP-MSP430FR2476.txt");
        wait(1000, SC_MS);
        double c[6];
        ciclos(sc_time(500, SC_MS), c);
        char t[260];
        std::snprintf(t, sizeof t, "a 25 C (0,75 V, la calibración del arranque): verde al %.1f %%, rojo al %.1f %%, azul al %.1f %%",
                      100 * c[4], 100 * c[3], 100 * c[5]);
        check(c[4] > 0.98 && c[3] < 0.01 && c[5] < 0.01, t);
        // 25,5 C: 0,755 V, 2061 de 4095 con 1,5 V; la media entera de la demo
        // sube hasta 9 por debajo (2052): (2052 - 2048) x 4 = 16 de 256 rojo
        u1.pon_temperatura(25.5);
        wait(1500, SC_MS);
        ciclos(sc_time(500, SC_MS), c);
        std::snprintf(t, sizeof t, "a 25,5 C (ADC 2061, la media de la demo en 2052): rojo al %.1f %% y verde al %.1f %% (16 y 239 de 256)", 100 * c[3], 100 * c[4]);
        check(std::fabs(c[3] - 16.0 / 256) < 0.01 && std::fabs(c[4] - 239.0 / 256) < 0.01 && c[5] < 0.01, t);
        fr.uart_tx.clear();
        pulsa(d_s2_, 2, 3, 30);
        const bool a = espera_uart("Ambient Temperature", 200);
        check(a && linea(fr.uart_tx, "Ambient") == "Ambient Temperature: 25.48 C",
              "S2 (P2.3): «" + linea(fr.uart_tx, "Ambient") + "» por la UCA0 a 9600 baudios (ADC 2061 de 1,5 V: 754,8 mV)");
        fr.uart_tx.clear();
        pulsa(d_s1_, 4, 0, 30);
        const bool b = espera_uart("Calibrated Temperature", 200);
        wait(500, SC_MS);
        ciclos(sc_time(500, SC_MS), c);
        std::snprintf(t, sizeof t, "S1 (P4.0) recalibra («%s») y el LED vuelve a verde (%.1f %%)", linea(fr.uart_tx, "Calibrated").c_str(), 100 * c[4]);
        check(b && linea(fr.uart_tx, "Calibrated").find("Calibrated Temperature: 25.") == 0 && c[4] > 0.98 && c[3] < 0.01, t);
        u1.pon_temperatura(24.5);
        wait(1500, SC_MS);
        ciclos(sc_time(500, SC_MS), c);
        std::snprintf(t, sizeof t, "a 24,5 C (ADC 2034), por debajo de la calibración (2052): azul al %.1f %% y verde al %.1f %% (72 y 183 de 256)", 100 * c[5], 100 * c[4]);
        check(std::fabs(c[5] - 72.0 / 256) < 0.01 && std::fabs(c[4] - 183.0 / 256) < 0.01 && c[3] < 0.01, t);
    }


    void k4() {
        grupo("K4 el BSL B1 (M10) por I2C con la configuración del usuario: solo I2C, la 0x60, sin borrar la RAM");
        const int drst = fr.rst.register_driver("bsl_rst"), dtest = fr.test.register_driver("bsl_test");
        corre(std::string(DIR) + "fr247_bsl_i2c.elf");
        wait(10, SC_MS);
        fr.poke(0x21F0, 0xA5);                                   // en la RAM que el BSL borraría
        bsl_u_.conecta(true); bsl_i_.conecta(true);
        const unsigned n0 = fr.bsl_entradas;
        banco_bsl::entra(fr.rst, fr.test, drst, dtest);           // y 20 ms
        std::vector<uint8_t> v;
        wait(60, SC_MS);
        const int pronto = bsl_i_.orden(0x60, {0x19}, &v);
        wait(320, SC_MS);
        const int u = bsl_u_.orden({0x19}, &v, 100);
        const int d48 = bsl_i_.orden(0x48, {0x19}, &v);
        check(fr.bsl_entradas == n0 + 1 && pronto == -1 && u == -1 && d48 == -1,
              "la secuencia lo arranca; a los 80 ms aún lee la TLV (unos 300 ms, §3.6), y luego ni la UART ni la 0x48 contestan");
        std::vector<uint8_t> pw{0x11};
        for (uint32_t a = 0xFFE0; a <= 0xFFFF; ++a) pw.push_back(fr.mem8(a));
        int a = bsl_i_.orden(0x60, pw, &v);
        check(a == 0 && v == std::vector<uint8_t>({0x3B, 0x00}), "por I2C en la 0x60 de 0xFFA0: la contraseña del programa, 0x3B 0x00");
        a = bsl_i_.orden(0x60, {0x19}, &v);
        check(a == 0 && v == std::vector<uint8_t>({0x3A, 0x00, 0x09, 0x36, 0xB5}) && fr.mem8(0x21F0) == 0xA5,
              "TX BSL Version: 00.09.36.B5 [tabla 7-4]; la RAM no se ha borrado (0xFF8A = 0x5A06)");
        a = bsl_i_.orden(0x60, {0x18, 0x88, 0xFF, 0x00, 0x04, 0x00}, &v);
        check(a == 0 && v == std::vector<uint8_t>({0x3A, 0x5A, 0x69, 0x06, 0x5A}), "TX Data Block de 0xFF88: la configuración, 5A 69 06 5A");
        const std::vector<uint8_t> d{0xDE, 0xC0, 0xAD, 0x0B};
        std::vector<uint8_t> w{0x10, 0x00, 0x00, 0x01};
        w.insert(w.end(), d.begin(), d.end());
        a = bsl_i_.orden(0x60, w, &v);
        const uint16_t k = banco_bsl::crc_ccitt(d);
        std::vector<uint8_t> c;
        const int b = bsl_i_.orden(0x60, {0x16, 0x00, 0x00, 0x01, 0x04, 0x00}, &c);
        check(a == 0 && b == 0 && fr.mem16(0x10000) == 0xC0DE && c == std::vector<uint8_t>({0x3A, uint8_t(k), uint8_t(k >> 8)}),
              "RX Data Block en 0x10000 (la FRAM de arriba) y su CRC Check");
        const uint16_t e = fr.mem16(0xFFFE);
        a = bsl_i_.orden(0x60, {0x17, uint8_t(e), uint8_t(e >> 8), 0x00}, nullptr);
        unsigned cambios = 0;
        bool ant = alto(1, 0);
        for (int i = 0; i < 3000; ++i) { wait(100, SC_US); const bool x = alto(1, 0); if (x != ant) ++cambios; ant = x; }
        char t[160];
        std::snprintf(t, sizeof t, "Load PC 0x%04X (con su ACK por I2C) arranca el programa: P1.0 cambia %u veces en 300 ms", unsigned(e), cambios);
        check(a == 0 && cambios >= 4, t);
        bsl_u_.conecta(false); bsl_i_.conecta(false);
        fr.rst.set_hiz(drst); fr.test.set_hiz(dtest);
    }

    void run() {
        const char* solo = std::getenv("TESTFR2476_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) k1();
        if (va('2')) k2();
        if (va('3')) k3();
        if (va('4')) k4();
        std::printf("\nTOTAL FR2476: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbFr2476 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
