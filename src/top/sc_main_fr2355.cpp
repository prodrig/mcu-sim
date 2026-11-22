// =============================================================================
// sc_main_fr2355.cpp — LA SUITE DEL MSP430FR2355 (fase M7 de doc/analisis_msp430.md)
//
//   J1  el SoC contra su descriptor generado (vectores por nombre, MPY32, la
//       ROM de bibliotecas declarada), la TLV de fábrica (0x830C) y el ICC
//       tras el reset (ICCSC 0x0023, ICCMVS 0x00FF, ICCILSRx 0xFFFF)
//   J2  fr235_basico.c: XT1 y el FLL a 24 MHz, la UCA1 en P4.3; el ADC de 12
//       bits y la referencia compartida a 2,0 V; el ICC (el anidamiento por
//       niveles, ICCSC e ICCMVS); el DAC del SAC3 cargado por TB2.1, el SAC1
//       en PGA inversor en cascada y su salida al ADC por dentro, DACIFG, y
//       el eCOMP1 contra la de 1,2 V; TB3.6 (siete CCR) en P6.5; el MPY32
//   J3  la demo de fábrica de TI (su binario TI-TXT) con la red de la
//       LaunchPad: en el modo sensor de luz, el fotodiodo D1 y R3 en el SAC2,
//       el SAC0 de seguidor, el ADC y el PWM por programa de LED1 y LED2; S1
//       recalibra; S2 pasa al generador de funciones (el seno del DAC del
//       SAC3 por el SAC1 invertido, el ADC y el JSON por la UART)
//   J4  fr235_extra.c (-mlarge): la ROM de bibliotecas (la DriverLib por las
//       tablas de TI en 0xFAC00), la modulación IR de SYSCFG1 en UCA0TXD,
//       TB0OUTH (por el eCOMP0 y por TB0TRG) y el MFM (un paquete de 32
//       bytes en Manchester por P5.1, que arranca TB2.0, y otro que el banco
//       manda por P5.0; la UCB1 de esclava; el fin en TB2 CCI0B)
//   J5  el BSL B1 (M10): el chip en blanco entra solo; la configuración de
//       la TLV (la etiqueta 0xB5: la UART en P1.6/P1.7), los ~300 ms de
//       arranque, la contraseña y la versión 00.09.36.B4
// =============================================================================
#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "common/asan_opciones.h"
#include "soc/msp430/fr2355/soc_fr2355.h"
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
static const char* const DIR = "verif/fr2355/elf/";

SC_MODULE(TbFr2355) {
    SocFR2355 fr{"fr2355"};
    banco_bsl::Uart bsl_{"bsl_uart", fr.p(1, 7), fr.p(1, 6)};
    AnalogNet gnd{"gnd"};
    // La red de la LaunchPad: R3 (2,37 Mohm) de OA2O (P3.1) a OA2- (P3.2),
    // el fotodiodo D1 con el ánodo a masa, y OA2+ (P3.3) a masa por J9
    Resistencia r3{"r3", fr.p(3, 1), fr.p(3, 2), 2.37e6};
    Fotodiodo d1{"d1", gnd, fr.p(3, 2), 5.0, 0.0};
    int d_gnd_ = -1, d_oa2m_ = -1, d_s1_ = -1, d_s2_ = -1, d_p10_ = -1, d_p12_ = -1, d_p50_ = -1;
    // Los flancos de P1.7 (IR), P1.6 (TB0OUTH) y P5.1 (MFM.TX)
    struct Flancos { unsigned p, b; std::vector<std::pair<sc_time, bool>> v; bool ant = false; };
    Flancos fl_[3] = {{1, 7, {}}, {1, 6, {}}, {5, 1, {}}};
    void flancos_proc() {
        for (Flancos& x : fl_) {
            const bool a = alto(x.p, x.b);
            if (a != x.ant) { x.v.emplace_back(sc_time_stamp(), a); x.ant = a; }
        }
    }
    unsigned subidas(unsigned k, const sc_time& desde) const {
        unsigned n = 0;
        for (const auto& e : fl_[k].v) if (e.first >= desde && e.second) ++n;
        return n;
    }

    SC_CTOR(TbFr2355) {
        SC_THREAD(run);
        SC_METHOD(pwm_proc); sensitive << fr.p(6, 5).value_changed_event() << fr.p(1, 0).value_changed_event()
                                       << fr.p(6, 6).value_changed_event(); dont_initialize();
        d_gnd_ = gnd.register_driver("gnd"); gnd.set_drive(d_gnd_, 0.0f, 0.01f);
        const int d = fr.rst.register_driver("r1"); fr.rst.set_drive(d, 3.3f, 47000.0f);
        d_oa2m_ = fr.p(3, 3).register_driver("j9"); fr.p(3, 3).set_drive(d_oa2m_, 0.0f, 0.1f);
        d_s1_ = fr.p(4, 1).register_driver("S1"); fr.p(4, 1).set_hiz(d_s1_);
        d_s2_ = fr.p(2, 3).register_driver("S2"); fr.p(2, 3).set_hiz(d_s2_);
        d_p10_ = fr.p(1, 0).register_driver("banco"); fr.p(1, 0).set_hiz(d_p10_);
        d_p12_ = fr.p(1, 2).register_driver("banco"); fr.p(1, 2).set_hiz(d_p12_);
        d_p50_ = fr.p(5, 0).register_driver("banco"); fr.p(5, 0).set_hiz(d_p50_);
        SC_METHOD(flancos_proc);
        for (const Flancos& x : fl_) sensitive << fr.p(x.p, x.b).value_changed_event();
        dont_initialize();
        r3.set_enabled(false); d1.set_enabled(false);
    }
    // El tiempo en alto de P6.5 (TB3.6), P1.0 (LED1) y P6.6 (LED2)
    struct Pwm { unsigned p, b; sc_time alto{SC_ZERO_TIME}, sube{SC_ZERO_TIME}; bool a = false; };
    Pwm pw_[3] = {{6, 5}, {1, 0}, {6, 6}};
    void pwm_proc() {
        for (Pwm& x : pw_) {
            const bool a = alto(x.p, x.b);
            if (a && !x.a) x.sube = sc_time_stamp();
            if (!a && x.a) x.alto += sc_time_stamp() - x.sube;
            x.a = a;
        }
    }
    // Los ciclos de trabajo de los tres en `dur`
    void ciclos(const sc_time& dur, double c[3]) {
        for (Pwm& x : pw_) { x.alto = SC_ZERO_TIME; x.sube = sc_time_stamp(); }
        const sc_time t0 = sc_time_stamp();
        wait(dur);
        for (unsigned k = 0; k < 3; ++k) {
            sc_time h = pw_[k].alto;
            if (pw_[k].a) h += sc_time_stamp() - pw_[k].sube;
            c[k] = h.to_seconds() / (sc_time_stamp() - t0).to_seconds();
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
    uint16_t lee16(uint32_t a) {
        tlm::tlm_generic_payload gp; uint8_t d[2] = {0, 0};
        gp.set_command(tlm::TLM_READ_COMMAND); gp.set_address(a); gp.set_data_ptr(d); gp.set_data_length(2);
        fr.acceso(gp, true);
        return uint16_t(d[0] | (d[1] << 8));
    }
    long num(const std::string& l, const char* tras) {
        const size_t i = l.find(tras);
        return i == std::string::npos ? -1 : std::strtol(l.c_str() + i + std::strlen(tras), nullptr, 10);
    }
    void pulsa(int d, unsigned p, unsigned b, int ms) {
        fr.p(p, b).set_drive(d, 0.0f, 10.0f);
        wait(ms, SC_MS);
        fr.p(p, b).set_hiz(d);
    }

    void j1() {
        grupo("J1 el SoC contra el descriptor generado, la TLV y el ICC tras el reset, sin tiempo");
        const m430::Desc430& d = SocFR2355::DESC;
        check(d.jtag_id == 0x98 && d.ram.ini == 0x2000 && d.ram.fin == 0x2FFF && d.principal.ini == 0x8000 &&
              d.info.ini == 0x1800 && d.mpy == m430::Mpy::Mpy32,
              "el descriptor: JTAG ID 0x98, RAM 0x2000-0x2FFF, FRAM desde 0x8000, información en 0x1800 y MPY32");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"PORT4", 0xFFCE}, {"PORT3", 0xFFD0}, {"PORT2", 0xFFD2}, {"PORT1", 0xFFD4}, {"SAC1_SAC3", 0xFFD6},
                       {"SAC0_SAC2", 0xFFD8}, {"ECOMP0_ECOMP1", 0xFFDA}, {"ADC", 0xFFDC}, {"EUSCI_B1", 0xFFDE},
                       {"EUSCI_B0", 0xFFE0}, {"EUSCI_A1", 0xFFE2}, {"EUSCI_A0", 0xFFE4}, {"WDT", 0xFFE6}, {"RTC", 0xFFE8},
                       {"TIMER3_B1", 0xFFEA}, {"TIMER3_B0", 0xFFEC}, {"TIMER2_B1", 0xFFEE}, {"TIMER2_B0", 0xFFF0},
                       {"TIMER1_B1", 0xFFF2}, {"TIMER1_B0", 0xFFF4}, {"TIMER0_B1", 0xFFF6}, {"TIMER0_B0", 0xFFF8}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 22 vectores que usa el SoC, por su nombre en msp430fr2355.h (SAC0_SAC2 en 0xFFD8, ECOMP0_ECOMP1 en 0xFFDA)");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a <= 0x1AF7; ++a) {
            const uint8_t b = fr.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(fr.mem16(0x1A04) == 0x830C && d.id_dispositivo == 0x830C && fr.mem16(0x1A02) == c,
              "la TLV: el ID 0x830C y su CRC-CCITT de 0x1A04 a 0x1AF7 [DS SLASEC4D tablas 6-69 y 6-70]");
        unsigned ilsr = 0;
        for (unsigned k = 0; k < 8; ++k) if (lee16(0x6C4 + 2 * k) == 0xFFFF) ++ilsr;
        char t[200];
        std::snprintf(t, sizeof t, "el ICC tras el reset: ICCSC 0x%04X, ICCMVS 0x%04X y los ocho ICCILSRx a 0xFFFF (%u) [UG FR2 tabla 5-1]",
                      lee16(0x6C0), lee16(0x6C2), ilsr);
        check(lee16(0x6C0) == 0x0023 && lee16(0x6C2) == 0x00FF && ilsr == 8, t);
        check(d.rom.ini == 0xFAC00 && d.rom.fin == 0xFFBFF,
              "la ROM de DriverLib y FFT, 0xFAC00-0xFFBFF, declarada en el descriptor [DS tabla 6-4]");
    }

    void j2() {
        grupo("J2 fr235_basico.c: FLL a 24 MHz, ADC de 12 bits, ICC, SAC por parejas con DAC disparado, eCOMP1, TB3 y MPY32");
        corre(std::string(DIR) + "fr235_basico.elf");
        const bool ok = espera_uart("hola FR2355", 1000);
        check(ok && linea(fr.uart_tx, "hola") == "hola FR2355 id 830C" && std::fabs(fr.f_mclk() - 24e6) < 0.1e6,
              "XT1 y el FLL a 24 MHz (NWAITS = 2), la UCA1 en P4.3 a 115200 (" + linea(fr.uart_tx, "hola") + ")");
        espera_uart("adc ref", 100);
        check(linea(fr.uart_tx, "adc vcc") == "adc vcc 4095", "el ADC de 12 bits (ADCRES = 10): DVCC en el canal 15 da 4095 (" + linea(fr.uart_tx, "adc vcc") + ")");
        check(linea(fr.uart_tx, "adc ref") == "adc ref 2482 8b 155",
              "la referencia compartida con REFVSEL = 01 (2,0 V) en el canal 13: 2482 de 4095 y 155 de 255 a 8 bits (" + linea(fr.uart_tx, "adc ref") + ")");
        espera_uart("icc c", 200);
        check(linea(fr.uart_tx, "icc sc") == "icc sc 00A3", "ICCEN: ICCSC = 0x00A3 (la pila vacía, ICMC = 3) (" + linea(fr.uart_tx, "icc sc") + ")");
        check(linea(fr.uart_tx, "icc a") == "icc a TPpt sc 0083 mvs 02F3",
              "el P1 (nivel 0) anida dentro de TB0 CCR0 (nivel 3); dentro, ICCSC 0x0083 e ICCMVS 0x02F3 (dos en la pila: 3 y 0) (" + linea(fr.uart_tx, "icc a") + ")");
        check(linea(fr.uart_tx, "icc b") == "icc b PpTt sc 00A3",
              "TB0 (nivel 3) espera a la RETI del P1 (nivel 0) aunque haya GIE, y la pila vuelve a vaciarse (" + linea(fr.uart_tx, "icc b") + ")");
        check(linea(fr.uart_tx, "icc c") == "icc c PTtp", "sin ICCEN, TB0 anida dentro del P1 como en cualquier MSP430 (" + linea(fr.uart_tx, "icc c") + ")");
        espera_uart("sac tras", 200);
        const long a0 = num(linea(fr.uart_tx, "sac antes"), "sac antes "), a1 = num(linea(fr.uart_tx, "sac tras"), "sac tras ");
        char t[260];
        std::snprintf(t, sizeof t, "el SAC1 en PGA inversor en cascada sobre su DAC (1,65 V) y el SAC3 de seguidor: A5 (por dentro) da %ld con el "
                      "latch del DAC3 a 0 (4093) y %ld cuando TB2.1 lo carga con 0xC00 (1022); %s", a0, a1, linea(fr.uart_tx, "sac tras").c_str());
        check(std::labs(a0 - 4093) <= 3 && std::labs(a1 - 1022) <= 3, t);
        check(num(linea(fr.uart_tx, "sac antes"), "cp1 ") == 1 && num(linea(fr.uart_tx, "sac tras"), "cp1 ") == 0,
              "el eCOMP1: OA1O (CPPSEL = 101) contra la de 1,2 V (CPNSEL = 010): 1 con 3,3 V y 0 con 0,82 V");
        check(num(linea(fr.uart_tx, "sac tras"), "dacifg ") >= 2, "DACIFG del SAC3 en cada carga por TB2.1, por SAC1_SAC3 y SAC3IV = 4 (" +
              std::to_string(num(linea(fr.uart_tx, "sac tras"), "dacifg ")) + ")");
        espera_uart("tb3", 100);
        wait(2, SC_MS);
        double c[3];
        ciclos(sc_time(5, SC_MS), c);
        espera_uart("tb3 ccr6", 100);
        std::snprintf(t, sizeof t, "TB3.6 en P6.5 (Timer3_B7): %.1f %% con CCR6 = 7200 de 24000, y CCR6 por TB3IV = 0x0C (%s)",
                      100 * c[0], linea(fr.uart_tx, "tb3 ccr6").c_str());
        check(std::fabs(c[0] - 0.30) < 0.01 && linea(fr.uart_tx, "tb3 ccr6") == "tb3 ccr6 1", t);
        espera_uart("mpy", 100);
        check(linea(fr.uart_tx, "mpy") == "mpy -300000000", "el MPY32: -100000 x 3000 con signo (" + linea(fr.uart_tx, "mpy") + ")");
        check(espera_uart("fin", 50), "el programa acaba en LPM0");
    }

    void j3() {
        grupo("J3 la demo de fábrica de TI (su binario): el sensor de luz y el generador de funciones");
        r3.set_enabled(true); d1.set_enabled(true);
        d1.pon_luz(0.0);
        corre("verif/fw/fr2355/OutOfBox_MSP-EXP430FR2355.txt");
        wait(1500, SC_MS);
        double c[3];
        ciclos(sc_time(200, SC_MS), c);
        char t[260];
        std::snprintf(t, sizeof t, "a oscuras (el ADC cerca de 0, lejos del umbral de 500): LED1 al %.1f %% y LED2 al %.1f %%", 100 * c[1], 100 * c[2]);
        check(c[1] > 0.97 && c[2] < 0.01, t);
        // 40 lx: 0,2 uA por 2,37 Mohm = 0,474 V; 12 bits de AVCC: 588
        d1.pon_luz(40.0);
        wait(1500, SC_MS);
        ciclos(sc_time(200, SC_MS), c);
        std::snprintf(t, sizeof t, "40 lx (0,474 V tras SAC2 y SAC0, ADC 588): LED1 al %.1f %% y LED2 al %.1f %% (la media entera de la demo se para 9 por debajo, en 579: (579 - 500) / 4 - 5 = 14 de 101, 13,9 %%)",
                      100 * c[1], 100 * c[2]);
        check(c[1] < 0.01 && std::fabs(c[2] - 14.0 / 101.0) < 0.01, t);
        pulsa(d_s1_, 4, 1, 50);
        wait(500, SC_MS);
        ciclos(sc_time(200, SC_MS), c);
        std::snprintf(t, sizeof t, "S1 (P4.1) toma la luz de ahora como umbral: los dos LED apagados (%.1f %% y %.1f %%)", 100 * c[1], 100 * c[2]);
        check(c[1] < 0.01 && c[2] < 0.01, t);
        fr.uart_tx.clear();
        pulsa(d_s2_, 2, 3, 50);
        // La demo calcula antes las 8192 muestras del seno con sinf (coma
        // flotante por programa): tarda; luego, 1,2 s de muestras
        const sc_time t0 = sc_time_stamp();
        for (int k = 0; k < 400 && fr.uart_tx.find("{\"a\":") == std::string::npos; ++k) wait(10, SC_MS);
        const double t_seno = (sc_time_stamp() - t0).to_seconds();
        const std::string cfg = fr.uart_tx;
        fr.uart_tx.clear();
        wait(1200, SC_MS);
        const std::string u = fr.uart_tx;
        long mx = -1, mn = 1 << 20;
        unsigned n = 0;
        for (size_t i = u.find("{\"a\":"); i != std::string::npos; i = u.find("{\"a\":", i + 1)) {
            const long v = std::strtol(u.c_str() + i + 5, nullptr, 10);
            mx = std::max(mx, v); mn = std::min(mn, v); ++n;
        }
        std::snprintf(t, sizeof t, "S2 (P2.3): el generador de funciones manda su configuración en JSON por la UCA1 (signalFreq 1, adcFreq 512) "
                      "y empieza a muestrear a los %.2f s (la tabla del seno)", t_seno);
        check(cfg.find("{\"clear\":1}") != std::string::npos && cfg.find("{\"signalFreq\":1}") != std::string::npos &&
              cfg.find("{\"adcFreq\":512}") != std::string::npos && t_seno < 3.9, t);
        std::snprintf(t, sizeof t, "el seno de 1 Hz del DAC del SAC3 (TB2.1 a 8192 Hz) invertido por el SAC1: %u muestras del ADC en 1,2 s (TB1.1, 512 Hz) entre %ld y %ld (1023-3070)",
                      n, mn, mx);
        check(n > 590 && n < 640 && mn < 1150 && mn > 950 && mx > 2950 && mx < 3150, t);
    }

    void j4() {
        grupo("J4 fr235_extra.c: la ROM de bibliotecas, la modulación IR, TB0OUTH y el MFM");
        r3.set_enabled(false); d1.set_enabled(false);
        fr.p(1, 0).set_drive(d_p10_, 1.5f, 10.0f);
        fr.p(5, 0).set_drive(d_p50_, 0.0f, 10.0f);
        corre(std::string(DIR) + "fr235_extra.elf");
        check(!fr.rom_cargada.empty(), "la imagen de la ROM, cargada al construir el SoC (" + fr.rom_cargada + ")");
        const bool r = espera_uart("rom v", 1000);
        check(r && linea(fr.uart_tx, "rom v") == "rom v 025A0107 p3 1 crc 1 mpy 7006652",
              "la DriverLib de la ROM por sus tablas: la versión 2.90.1.7, GPIO, CRC (igual que a mano) y MPY32 (" + linea(fr.uart_tx, "rom v") + ")");
        // La modulación IR: en cada tramo, 1 ms de P1.7
        struct Ir { const char* m; bool portadora; bool nivel; const char* q; };
        const Ir ir[] = {{"ir ask1", true, false, "ASK con el dato a 1: la portadora (TB0.2, 38 kHz)"},
                         {"ir ask0", false, false, "ASK con el dato a 0: nada (0)"},
                         {"ir inv", false, true, "IRPSEL: invertida (1)"},
                         {"ir fsk0", true, false, "FSK con el dato a 0: la primera PWM, la portadora"},
                         {"ir fsk1", false, false, "FSK con el dato a 1: la segunda, TB1.2 a 0"},
                         {"ir fin", false, true, "sin IREN: el pin vuelve a ser UCA0TXD (en reposo, 1)"}};
        for (const Ir& x : ir) {
            espera_uart(x.m, 100);
            wait(500, SC_US);
            const sc_time t0 = sc_time_stamp();
            wait(1, SC_MS);
            const unsigned n = subidas(0, t0);
            const bool ok = x.portadora ? (n >= 37 && n <= 39) : (n == 0 && alto(1, 7) == x.nivel);
            check(ok, std::string("IR, ") + x.q + ": " + std::to_string(n) + " flancos en 1 ms");
        }
        // TB0OUTH: con el eCOMP0 a 1 (P1.0 a 1,5 V), P1.6 suelto; con 0,5 V, el PWM
        espera_uart("outh ecomp", 100);
        wait(500, SC_US);
        const bool suelto1 = fr.p(1, 6).floating();
        fr.p(1, 0).set_drive(d_p10_, 0.5f, 10.0f);
        wait(500, SC_US);
        sc_time t0 = sc_time_stamp();
        wait(1, SC_MS);
        const unsigned n1 = subidas(1, t0);
        check(suelto1 && n1 >= 37 && n1 <= 39,
              "TB0OUTH por el eCOMP0: TB0.1 (P1.6) en alta impedancia con CPOUT = 1; con CPOUT = 0, su PWM (" + std::to_string(n1) + " flancos en 1 ms)");
        fr.p(1, 2).set_drive(d_p12_, 3.3f, 10.0f);
        espera_uart("outh pin", 100);
        wait(500, SC_US);
        const bool suelto2 = fr.p(1, 6).floating();
        fr.p(1, 2).set_drive(d_p12_, 0.0f, 10.0f);
        wait(500, SC_US);
        t0 = sc_time_stamp();
        wait(1, SC_MS);
        const unsigned n2 = subidas(1, t0);
        check(suelto2 && n2 >= 37 && n2 <= 39,
              "TB0OUTH por TB0TRG (P1.2, TB0TRGSEL = 1): alta impedancia con el pin a 1, el PWM con él a 0 (" + std::to_string(n2) + " flancos)");
        fr.p(1, 2).set_hiz(d_p12_); fr.p(1, 0).set_hiz(d_p10_);
        // El MFM: lo que sale por P5.1, decodificado aquí
        const size_t i0 = fl_[2].v.size();             // P5.1 no se ha movido antes
        espera_uart("mfm", 100);
        // la recepción: cuando P3.1 sube, el paquete por P5.0
        for (int k = 0; k < 2000 && !alto(3, 1); ++k) wait(10, SC_US);
        const sc_time T(8.0 / 3e6, SC_SEC), M = T / 2;
        uint8_t mando[32];
        for (unsigned j = 0; j < 32; ++j) mando[j] = uint8_t(0xA5 ^ (j * 13));
        const bool listo = alto(3, 1);
        if (listo) {
            wait(20, SC_US);
            for (unsigned j = 0; j < 32; ++j)
                for (int b = 7; b >= 0; --b) {
                    const bool v = (mando[j] >> b) & 1u;
                    fr.p(5, 0).set_drive(d_p50_, v ? 3.3f : 0.0f, 10.0f); wait(M);
                    fr.p(5, 0).set_drive(d_p50_, v ? 0.0f : 3.3f, 10.0f); wait(M);
                }
            fr.p(5, 0).set_drive(d_p50_, 0.0f, 10.0f);
        }
        const bool fin = espera_uart("mfm tx", 200);
        // lo que salió por P5.1: desde la primera subida, cada bit en su cuarto
        std::vector<std::pair<sc_time, bool>> e(fl_[2].v.begin() + long(i0), fl_[2].v.end());
        auto nivel_en = [&](const sc_time& t) {
            bool v = false;
            for (const auto& x : e) { if (x.first > t) break; v = x.second; }
            return v;
        };
        unsigned bien = 0;
        if (!e.empty()) {
            const sc_time s0 = e.front().first;
            for (unsigned j = 0; j < 32; ++j) {
                uint8_t b = 0;
                for (unsigned k = 0; k < 8; ++k) {
                    const sc_time t = s0 + T * double(8 * j + k) + T / 4;
                    const bool h1 = nivel_en(t), h2 = nivel_en(t + M);
                    if (h1 == h2) break;
                    b = uint8_t((b << 1) | (h1 ? 1u : 0u));
                }
                if (b == uint8_t(0x80 | (j * 7))) ++bien;
            }
        }
        char t[300];
        std::snprintf(t, sizeof t, "MFM.TX (P5.1): TB2.0 arranca el paquete; %u de 32 bytes en Manchester a 375 kbps (SMCLK 3 MHz / 8), "
                      "los de TXBUF de la UCB1 esclava", bien);
        check(bien == 32 && fr.mfm_paquetes_tx() == 1, t);
        uint16_t sm = 0;
        for (unsigned j = 0; j < 32; ++j) sm = uint16_t(sm * 31 + mando[j]);
        char esp[80];
        std::snprintf(esp, sizeof esp, "mfm tx 32 rx 32 suma %04X r0 A5 r31 %02X", sm, mando[31]);
        check(listo && fin && linea(fr.uart_tx, "mfm tx") == esp && fr.mfm_paquetes_rx() == 1,
              std::string("MFM.RX (P5.0): el paquete del banco, byte a byte en RXBUF de la UCB1; los dos fines en TB2 CCI0B (") +
              linea(fr.uart_tx, "mfm tx") + ")");
        check(espera_uart("fin", 50), "el programa acaba en LPM0");
        fr.p(5, 0).set_hiz(d_p50_);
    }


    void j5() {
        grupo("J5 el BSL B1 (M10) del FR2355: el chip en blanco, la configuración de la TLV (0xB5), los ~300 ms, y la UART de P1.6/P1.7");
        corre(std::string(DIR) + "fr235_basico.elf");
        wait(5, SC_MS);
        const int drst = fr.rst.register_driver("bsl_rst"), dtest = fr.test.register_driver("bsl_test");
        fr.test.set_drive(dtest, 0.0f, 10.0f);
        bsl_.conecta(true);
        // El chip en blanco: el vector de reset a 0xFFFF y un reset sin la secuencia [BSLFRAM §3.3.3]
        fr.poke(0xFFFE, 0xFF); fr.poke(0xFFFF, 0xFF);
        const unsigned v0 = fr.bsl_vacios;
        fr.rst.set_drive(drst, 0.0f, 10.0f); wait(1, SC_MS); fr.rst.set_drive(drst, 3.3f, 10.0f);
        std::vector<uint8_t> v;
        wait(100, SC_MS);
        const int pronto = bsl_.orden({0x19}, &v, 50);
        wait(300, SC_MS);
        const uint16_t antes = fr.mem16(0x8000);
        int a = bsl_.orden(banco_bsl::contrasena(0x00), &v);
        const bool mala = a == 0 && v == std::vector<uint8_t>({0x3B, 0x05}) && antes != 0xFFFF && fr.mem16(0x8000) == 0xFFFF;
        a = bsl_.orden(banco_bsl::contrasena(0xFF), &v);
        const int b = bsl_.orden({0x19}, &v);
        check(fr.bsl_vacios == v0 + 1 && pronto == -1 && mala && a == 0 && b == 0 &&
              v == std::vector<uint8_t>({0x3A, 0x00, 0x09, 0x36, 0xB4}),
              "el vector en blanco lo arranca; a los 100 ms aún no contesta (la TLV, §3.6); por la UART de la TLV "
              "(P1.6/P1.7), la contraseña (mala: borra; luego la de 0xFF) y la versión 00.09.36.B4 [tabla 7-3]");
        bsl_.conecta(false);
        fr.rst.set_hiz(drst); fr.test.set_hiz(dtest);
    }

    void run() {
        const char* solo = std::getenv("TESTFR2355_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) j1();
        if (va('2')) j2();
        if (va('3')) j3();
        if (va('4')) j4();
        if (va('5')) j5();
        std::printf("\nTOTAL FR2355: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbFr2355 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
