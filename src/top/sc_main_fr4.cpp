// =============================================================================
// sc_main_fr4.cpp — LA SUITE DEL MSP430FR4133 (fase M7 de doc/analisis_msp430.md)
//
//   F1  el SoC contra su descriptor generado (vectores, mapa, JTAG ID) y la
//       TLV de fábrica (el ID 0x81F0, la CRC hasta 0x1A77); el LCD_E al reset
//   F2  fr4_basico.c con la DriverLib de TI y el vidrio FH-1138P colgado de
//       los pines: XT1 y el FLL a 8 MHz, la UCA0 a 115200 en P1.0, el LCD_E
//       en 4-mux como hal_LCD.c («HOLA43» y el corazón), la memoria de
//       parpadeo con LCDDISP («FR4133»), las tramas (LCDFRMIFG), el
//       parpadeo de todo, VLCD de la bomba de carga, el sensor, Timer0_A3 y
//       el PWM de TA1.1 en P4.0
//   F3  LPM3.5 con el LCD encendido: el vidrio sigue viendo «LPM35» sin
//       núcleo, el RTC desde XT1 lo despierta, y el LCD sigue sin volver a
//       configurarlo
//   F4  la demo de fábrica de TI (su binario TI-TXT): los textos que pasan por
//       el vidrio, S1+S2 al cronómetro (cuenta en LPM3, parado en LPM3.5) y
//       otra vez S1+S2 al termómetro (°C, y °F con S2)
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "common/asan_opciones.h"
#include "soc/msp430/fr4133/soc_fr4133.h"
#include "parts/lcd_fh1138p.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/fr4133/elf/";

// Las patillas del FH-1138P en la LaunchPad: el Lx de cada una [SLAU595B tabla 4]
static std::vector<analog_net_if*> vidrio_de(SocFR4133& f) {
    static const int L[38] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 4, 5, 6, 7,
                              3, 2, 1, 0, -1, -1, -1, -1, -1, -1, -1, 24, 25, 26, 36, 37, 38, 39};
    std::vector<analog_net_if*> v(38, nullptr);
    for (unsigned k = 0; k < 38; ++k) if (L[k] >= 0) v[k] = f.pin[fr4133::LCD_L[L[k]]];
    return v;
}

SC_MODULE(TbFr4) {
    SocFR4133 fr{"fr4133"};
    LcdFh1138p lcd{"LCD1", vidrio_de(fr), true};
    int d_s1_ = -1, d_s2_ = -1;
    SC_CTOR(TbFr4) {
        SC_THREAD(run);
        SC_METHOD(led2_proc); sensitive << fr.p(4, 0).value_changed_event(); dont_initialize();
        SC_METHOD(textos_proc); sensitive << ev_texto_; dont_initialize();
        const int d = fr.rst.register_driver("r1"); fr.rst.set_drive(d, 3.3f, 47000.0f);
        AnalogNet* s1 = dynamic_cast<AnalogNet*>(&fr.p(1, 2));
        AnalogNet* s2 = dynamic_cast<AnalogNet*>(&fr.p(2, 6));
        d_s1_ = s1->register_driver("S1"); s1->set_hiz(d_s1_);
        d_s2_ = s2->register_driver("S2"); s2->set_hiz(d_s2_);
    }
    unsigned led2_ = 0;
    bool led2_ant_ = false;
    void led2_proc() { const bool a = alto(4, 0); if (a && !led2_ant_) ++led2_; led2_ant_ = a; }
    // Los textos que pasan por el vidrio (cada 5 ms se mira)
    std::vector<std::string> vistos_;
    sc_event ev_texto_;
    bool grabando_ = false;
    void textos_proc() {
        if (!grabando_) return;
        const std::string t = lcd.texto();
        if (vistos_.empty() || vistos_.back() != t) vistos_.push_back(t);
        next_trigger(5, SC_MS);
    }
    void graba(bool si) { grabando_ = si; if (si) { vistos_.clear(); ev_texto_.notify(SC_ZERO_TIME); } }
    bool visto(const std::string& t) const { for (const auto& x : vistos_) if (x == t) return true; return false; }
    // ¿Aparecen en este orden?
    bool en_orden(const std::vector<std::string>& ts) const {
        size_t i = 0;
        for (const auto& x : vistos_) if (i < ts.size() && x == ts[i]) ++i;
        return i == ts.size();
    }
    void boton(unsigned n, bool pulsado) {
        AnalogNet* s = dynamic_cast<AnalogNet*>(n == 1 ? &fr.p(1, 2) : &fr.p(2, 6));
        const int d = n == 1 ? d_s1_ : d_s2_;
        if (pulsado) s->set_drive(d, 0.0f, 10.0f); else s->set_hiz(d);
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
        for (int i = 0; i < ms && !entera(); ++i) wait(1, SC_MS);
        return entera();
    }
    bool espera_lcd(const std::string& t, int ms) {
        for (int i = 0; i < ms && lcd.texto() != t; ++i) wait(1, SC_MS);
        return lcd.texto() == t;
    }
    bool alto(unsigned p, unsigned b) { return !fr.p(p, b).floating() && fr.p(p, b).voltage() > 2.0f; }
    uint16_t lee16(uint32_t a) {
        tlm::tlm_generic_payload gp; uint8_t d[2] = {};
        gp.set_command(tlm::TLM_READ_COMMAND); gp.set_address(a); gp.set_data_ptr(d); gp.set_data_length(2);
        fr.acceso(gp, true);
        return uint16_t(d[0] | (d[1] << 8));
    }

    void f1() {
        grupo("F1 el SoC contra el descriptor generado, la TLV y el LCD_E al reset, sin tiempo");
        const m430::Desc430& d = SocFR4133::DESC;
        check(d.jtag_id == 0x98 && d.ram.ini == 0x2000 && d.ram.fin == 0x27FF && d.principal.ini == 0xC400 &&
              d.mpy == m430::Mpy::Ninguno && fr.eem.disparadores() == 3,
              "el descriptor: JTAG ID 0x98, RAM 0x2000-0x27FF, FRAM desde 0xC400, sin MPY, EEM S (3 disparadores)");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"LCD_E", 0xFFE2}, {"PORT2", 0xFFE4}, {"PORT1", 0xFFE6}, {"ADC", 0xFFE8}, {"USCI_B0", 0xFFEA},
                       {"USCI_A0", 0xFFEC}, {"WDT", 0xFFEE}, {"RTC", 0xFFF0}, {"TIMER1_A1", 0xFFF2}, {"TIMER1_A0", 0xFFF4},
                       {"TIMER0_A1", 0xFFF6}, {"TIMER0_A0", 0xFFF8}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 12 vectores que usa el SoC, por su nombre en msp430fr4133.h (el LCD_E en 0xFFE2)");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a <= 0x1A77; ++a) {
            const uint8_t b = fr.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(fr.mem16(0x1A04) == 0x81F0 && d.id_dispositivo == 0x81F0 && fr.mem16(0x1A02) == c,
              "la TLV: el ID 0x81F0 (el de devicedb) y su CRC-CCITT de 0x1A04 a 0x1A77 [DS SLAS865F tabla 9-27]");
        char t[160];
        std::snprintf(t, sizeof t, "el sensor en la TLV: CAL_ADC_15V_30C %u y 85C %u (1,013 V a 0 °C y 3,35 mV/°C)",
                      fr.mem16(0x1A1A), fr.mem16(0x1A1C));
        check(fr.mem16(0x1A1A) == 759 && fr.mem16(0x1A1C) == 885, t);
        check(lee16(0x600) == 0x3800 && lee16(0x606) == 0 && lee16(0x61E) == 0,
              "el LCD_E al reset: LCDCTL0 0x3800 (LCDDIV = 7), LCDMEMCTL y LCDIV a 0");
    }

    void f2() {
        grupo("F2 fr4_basico.c: XT1, FLL, UART, el LCD_E en 4-mux con el vidrio, la memoria de parpadeo, tramas, sensor, Timer_A");
        corre(std::string(DIR) + "fr4_basico.elf");
        const bool ok = espera_uart("hola FR4133", 2000);
        char t[220];
        std::snprintf(t, sizeof t, "XT1 y el FLL a 244 x 32768: MCLK %.3f MHz («%s»)", fr.f_mclk() / 1e6, linea(fr.uart_tx, "hola").c_str());
        check(ok && std::fabs(fr.f_mclk() - 7.995392e6) < 2e4 && linea(fr.uart_tx, "hola").find(" id 81F0") != std::string::npos, t);
        const bool h = espera_uart("lcd hola", 500);
        check(h && lcd.texto() == "HOLA43" && lcd.simbolo("HRT") && !lcd.simbolo("TMR"),
              "el vidrio ve «" + lcd.texto() + "» y el corazón: L0-L3 de COM, 4-mux, LCDDIV = 2 (como hal_LCD.c)");
        // VLCD: el mayor nivel de un COM (L0, P7.0) en una trama
        double vmax = 0;
        for (int i = 0; i < 60; ++i) { vmax = std::max(vmax, double(fr.p(7, 0).voltage())); wait(250, SC_US); }
        std::snprintf(t, sizeof t, "VLCD de la bomba de carga con la referencia interna: %.2f V (VLCD = 0110: 2,96 V)", vmax);
        check(std::fabs(vmax - 2.96) < 0.02, t);
        const bool bm = espera_uart("lcd bm", 500);
        check(bm && lcd.texto() == "FR4133", "LCDDISP = 1: la memoria de parpadeo se muestra («" + lcd.texto() + "»)");
        const bool tr = espera_uart("lcd tramas", 500);
        long n = 0;
        std::sscanf(linea(fr.uart_tx, "lcd tramas").c_str(), "lcd tramas %ld", &n);
        std::snprintf(t, sizeof t, "LCDFRMIFG por LCDIV: %ld tramas en 100 ms (fFRAME = 32768 / (3 x 16) / 8 = 85,3 Hz)", n);
        check(tr && n >= 8 && n <= 9, t);
        // El parpadeo de todo: el vidrio pasa de «HOLA43» a blanco y vuelve
        espera_uart("lcd parpadea", 300);
        graba(true);
        espera_uart("lcd quieto", 2500);
        graba(false);
        unsigned blancos = 0;
        for (const auto& x : vistos_) if (x == "      ") ++blancos;
        std::snprintf(t, sizeof t, "LCDBLKMOD = 10 con LCDBLKPRE = /128: %u apagados en 2 s (fBLINK = 683 / 512 = 1,33 Hz)", blancos);
        check(blancos >= 2 && blancos <= 3 && en_orden({"HOLA43", "      ", "HOLA43"}), t);
        check(lcd.texto() == "HOLA43", "sin parpadeo, «HOLA43» otra vez");
        const bool te = espera_uart("temp", 500);
        long dc = 0, adc = 0;
        std::sscanf(linea(fr.uart_tx, "temp").c_str(), "temp %ld dC adc %ld", &dc, &adc);
        std::snprintf(t, sizeof t, "el sensor con la referencia de 1,5 V y la TLV: %ld décimas de °C (ADC %ld; el banco, a 25 °C)", dc, adc);
        check(te && dc >= 240 && dc <= 260, t);
        const unsigned l0 = led2_;
        const bool ta = espera_uart("ta0 10 ms", 300);
        wait(3, SC_MS);
        std::snprintf(t, sizeof t, "Timer0_A3 a 1 ms; TA1.1 en P4.0 (LED2) con PWM de 1 kHz: %u flancos", led2_ - l0);
        check(ta && led2_ - l0 >= 8, t);
    }

    void f3() {
        grupo("F3 LPM3.5 con el LCD encendido y el RTC desde XT1");
        const bool entra = espera_uart("lpm35 entra", 300);
        for (int i = 0; i < 100 && !fr.en_lpm5(); ++i) wait(100, SC_US);
        const uint64_t tr0 = lcd.tramas();
        wait(50, SC_MS);
        char t[200];
        std::snprintf(t, sizeof t, "LPM3.5 (el LCD_E desde XT1 cuenta como el RTC): sin núcleo, el vidrio sigue con «%s» (%llu tramas en 50 ms)",
                      lcd.texto().c_str(), (unsigned long long)(lcd.tramas() - tr0));
        check(entra && fr.en_lpm5() && fr.lpm35() && lcd.texto() == "LPM35 " && lcd.tramas() - tr0 >= 3, t);
        const bool l = espera_uart("lpm35 rstiv", 300);
        check(l && linea(fr.uart_tx, "lpm35 rstiv") == "lpm35 rstiv 0010 bak 4133 lcdctl0 101D",
              "el RTC (XT1, 3277 cuentas) lo despierta: SYSRSTIV = 0x08, la BAKMEM y el LCD_E siguen (" +
              linea(fr.uart_tx, "lpm35 rstiv") + ")");
        const bool s = espera_uart("lcd sigue", 200);
        check(s && lcd.texto() == "LPM35 ", "tras el BOR, sin volver a configurarlo, el vidrio sigue con «LPM35»");
        const bool f = espera_uart("fin", 200);
        check(f && lcd.texto() == "FIN   ", "y el programa escribe «FIN» en la memoria que se quedó");
        // SYS23: un BOR por RST no conserva 0x20FE-0x20FF [SLAZ550AA]
        fr.poke(0x20FE, 0xAA); fr.poke(0x20FF, 0x55); fr.poke(0x2100, 0x77);
        const int d = fr.rst.register_driver("s3");
        fr.rst.set_drive(d, 0.0f, 10.0f); wait(1, SC_MS); fr.rst.set_hiz(d); wait(1, SC_MS);
        check(fr.mem8(0x20FE) == 0 && fr.mem8(0x20FF) == 0 && fr.mem8(0x2100) == 0x77,
              "SYS23 (SLAZ550AA): tras el BOR por RST, 0x20FE-0x20FF no se conservan; 0x2100, sí");
    }

    void f4() {
        grupo("F4 la demo de fábrica de TI (su binario): el vidrio, el cronómetro y el termómetro");
        graba(true);
        corre("verif/fw/fr4133/OutOfBox_MSP430FR4133.txt");
        wait(17, SC_SEC);
        check(en_orden({"WELCOM", "ELCOME", "TO THE", "FR4133", "LAUNCH", "UNCHPA"}),
              "pasa «WELCOME TO THE FR4133 LAUNCHPAD» por los seis caracteres (" + std::to_string(vistos_.size()) + " textos)");
        // [hal_LCD.c] La S y el 5 son el mismo glifo ({0xB7, 0x00}): el vidrio no los distingue
        check(en_orden({"UNCHPA", "??????", "HOLD 5", "52 TO ", "5WITCH", " MODE5"}),
              "recorre los segmentos y pasa «HOLD S1 AND S2 TO SWITCH MODES» (la S, con el glifo del 5)");
        // S1 y S2 a la vez (40 vueltas de 15 ms del Timer_A): el cronómetro
        graba(true);
        boton(1, true); boton(2, true); wait(1500, SC_MS); boton(1, false); boton(2, false);
        wait(4, SC_SEC);
        check(en_orden({"5TOPWA", "WATCH ", " MODE "}) && lcd.texto() == "000000" && lcd.texto_con_signos() == "00:00:00",
              "S1 y S2: «STOPWATCH MODE» y el cronómetro a cero: «" + lcd.texto_con_signos() + "»");
        for (int i = 0; i < 100 && !fr.en_lpm5(); ++i) wait(10, SC_MS);
        check(fr.en_lpm5() && fr.lpm35() && lcd.simbolo("A2COL") && lcd.simbolo("A4COL"),
              "parado, en LPM3.5 con el LCD encendido (y los dos puntos)");
        // S1: cuenta (LPM3 con el RTC cada 10 ms)
        boton(1, true); wait(50, SC_MS); boton(1, false);
        wait(2, SC_SEC);
        boton(1, true); wait(50, SC_MS); boton(1, false);
        wait(300, SC_MS);
        const std::string cr = lcd.texto();
        const int cs = std::atoi(cr.substr(2, 4).c_str());
        check(cr.substr(0, 2) == "00" && cs >= 195 && cs <= 215,
              "S1 arranca, S1 para a los 2 s: «" + lcd.texto_con_signos() + "» (MM:SS:CC)");
        for (int i = 0; i < 100 && !fr.en_lpm5(); ++i) wait(10, SC_MS);
        check(fr.en_lpm5() && lcd.texto() == cr, "parado otra vez, LPM3.5, y el vidrio sigue con la cuenta");
        // S1 y S2: el termómetro
        graba(true);
        boton(1, true); boton(2, true); wait(1500, SC_MS); boton(1, false); boton(2, false);
        wait(5, SC_SEC);
        const std::string tc = lcd.texto();
        check(en_orden({"TEMP5E", "5EN5OR", " MODE "}) && tc.size() == 6 && tc[5] == 'C' && tc.substr(2, 3) >= "240" && tc.substr(2, 3) <= "260" &&
              lcd.simbolo("DEG") && lcd.simbolo("A4DP"),
              "S1 y S2: «TEMPSENSOR MODE» y la temperatura: «" + lcd.texto_con_signos() + "» (el ADC disparado por TA1.1)");
        boton(2, true); wait(50, SC_MS); boton(2, false);
        wait(600, SC_MS);
        const std::string tf = lcd.texto();
        check(tf[5] == 'F' && tf.substr(2, 3) >= "760" && tf.substr(2, 3) <= "780", "S2: en °F, «" + lcd.texto_con_signos() + "»");
        graba(false);
    }

    void run() {
        const char* solo = std::getenv("TESTFR4_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) f1();
        if (va('2') || va('3')) { f2(); f3(); }
        if (va('4')) f4();
        std::printf("\nTOTAL FR4: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbFr4 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
