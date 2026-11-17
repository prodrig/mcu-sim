// =============================================================================
// sc_main_fr2.cpp — LA SUITE DEL MSP430FR2433 (fase M6 de doc/analisis_msp430.md)
//
//   R1  el SoC contra su descriptor generado (vectores, mapa, JTAG ID, EEM) y
//       la TLV de fábrica (el ID 0x8240, la CRC, la calibración del ADC)
//   R2  fr2_basico.c con la DriverLib de TI: el FLL a 16 MHz con el recorte
//       de DCOFTRIM por programa, la UCA0 a 115200 leída en P1.4, el sensor
//       con la referencia de 1,5 V del PMM, MPY32, CRC16, la protección de la
//       FRAM con SYSCFG0 y Timer0_A3
//   R3  LPM3.5 con el RTC: el despertar es un BOR (SYSRSTIV 8, PMMLPM5IFG,
//       RTCIFG), la BAKMEM sigue y la RAM no, y los pines se quedan quietos
//       (LOCKLPM5) hasta que el programa los suelta
//   R4  LPM4.5 (sin RTC) y el despertar por P2.3: P2IFG.3; la BAKMEM, perdida
//   R5  el JSTATE por SBW (la sonda de bancada): LPMx.5, y LPM0 con CPUOFF
//   R6  la demo de fábrica de TI (su binario TI-TXT): registra en la FRAM
//       despertando de LPM3.5 con el RTC (la CTPL); S1 manda el registro en
//       JSON; S1 y S2 a la vez, el modo en vivo
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/fr2433/soc_fr2433.h"
#include "m430/sonda/sonda_xv2.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/fr2433/elf/";

SC_MODULE(TbFr2) {
    SocFR2433 fr{"fr2433"};
    int d_s1_ = -1, d_s2_ = -1;
    SC_CTOR(TbFr2) {
        SC_THREAD(run);
        SC_METHOD(led1_proc); sensitive << fr.p(1, 0).value_changed_event(); dont_initialize();
        // R1 de la LaunchPad: 47 kohm de RST a 3V3
        const int d = fr.rst.register_driver("r1"); fr.rst.set_drive(d, 3.3f, 47000.0f);
        AnalogNet* s1 = dynamic_cast<AnalogNet*>(&fr.p(2, 3));
        AnalogNet* s2 = dynamic_cast<AnalogNet*>(&fr.p(2, 7));
        d_s1_ = s1->register_driver("S1"); s1->set_hiz(d_s1_);
        d_s2_ = s2->register_driver("S2"); s2->set_hiz(d_s2_);
    }
    // Los destellos de LED1 (P1.0): los flancos de subida
    unsigned led1_ = 0;
    bool led1_ant_ = false;
    void led1_proc() { const bool a = alto(1, 0); if (a && !led1_ant_) ++led1_; led1_ant_ = a; }
    void boton(unsigned n, bool pulsado) {
        AnalogNet* s = dynamic_cast<AnalogNet*>(&fr.p(2, n == 1 ? 3 : 7));
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
    bool alto(unsigned p, unsigned b) { return !fr.p(p, b).floating() && fr.p(p, b).voltage() > 2.0f; }

    void r1() {
        grupo("R1 el SoC contra el descriptor generado y la TLV, sin tiempo");
        const m430::Desc430& d = SocFR2433::DESC;
        check(d.jtag_id == 0x98 && d.eem == m430::TamEem::S && d.ram.ini == 0x2000 && d.ram.fin == 0x2FFF &&
              d.principal.ini == 0xC400 && d.info.ini == 0x1800 && d.info.fin == 0x19FF && fr.eem.disparadores() == 3,
              "el descriptor: JTAG ID 0x98, EEM S (3 disparadores), RAM 0x2000-0x2FFF, FRAM desde 0xC400, información 0x1800-0x19FF");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"PORT2", 0xFFDA}, {"PORT1", 0xFFDC}, {"ADC", 0xFFDE}, {"USCI_B0", 0xFFE0}, {"USCI_A1", 0xFFE2},
                       {"USCI_A0", 0xFFE4}, {"WDT", 0xFFE6}, {"RTC", 0xFFE8}, {"TIMER3_A1", 0xFFEA}, {"TIMER3_A0", 0xFFEC},
                       {"TIMER2_A1", 0xFFEE}, {"TIMER2_A0", 0xFFF0}, {"TIMER1_A1", 0xFFF2}, {"TIMER1_A0", 0xFFF4},
                       {"TIMER0_A1", 0xFFF6}, {"TIMER0_A0", 0xFFF8}, {"UNMI", 0xFFFA}, {"SYSNMI", 0xFFFC}, {"RESET", 0xFFFE}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 19 vectores que usa el SoC, como en msp430fr2433.h (TIMER0_A0 en 0xFFF8: el generador corrige el comentario de TI)");
        check(fr.mem16(0x1A04) == 0x8240 && d.id_dispositivo == 0x8240, "la TLV: el ID del chip en 0x1A04 (0x8240, el de devicedb)");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a <= 0x1AF5; ++a) {
            const uint8_t b = fr.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(fr.mem16(0x1A02) == c, "la TLV: su CRC-CCITT de 0x1A04 a 0x1AF5 en 0x1A02");
        check(fr.mem8(0x1A14) == 0x11 && fr.mem16(0x1A1A) < fr.mem16(0x1A1C) && fr.mem8(0x1A1E) == 0x12,
              "la TLV: la calibración del ADC en 0x1A14 (CAL_ADC_15V_30C < 85C) y la de la referencia y el DCO en 0x1A1E");
    }

    void r2() {
        grupo("R2 fr2_basico.c con la DriverLib: FLL a 16 MHz con DCOFTRIM, UART, sensor, MPY32, CRC16, FRAM, Timer_A");
        corre(std::string(DIR) + "fr2_basico.elf");
        const bool ok = espera_uart("ta0 10 ms", 3000);
        const std::string s = fr.uart_tx;
        char t[220];
        std::snprintf(t, sizeof t, "CS_initFLLCalculateTrim(16000, 488): MCLK %.3f MHz («%s»)", fr.f_mclk() / 1e6, linea(s, "hola").c_str());
        check(ok && std::fabs(fr.f_mclk() - 15.990784e6) < 2e4 && linea(s, "hola").find("hola FR2433 159") == 0, t);
        check(linea(s, "fll") == "fll dco 256 trim 3 id 8240",
              "el recorte por programa converge: el grifo 256 con DCOFTRIM = 3; el ID de la TLV (" + linea(s, "fll") + ")");
        long dc = 0, adc = 0;
        std::sscanf(linea(s, "temp").c_str(), "temp %ld dC adc %ld", &dc, &adc);
        std::snprintf(t, sizeof t, "el sensor con la referencia de 1,5 V del PMM: %ld décimas de °C (ADC %ld; el banco, a 25 °C)", dc, adc);
        check(dc >= 240 && dc <= 260, t);
        check(linea(s, "mpy") == "mpy 0000001CBE991A08 -233331", "MPY32 (libgcc): " + linea(s, "mpy"));
        check(linea(s, "crc") == "crc 29B1", "CRC16: el CRC-CCITT de «123456789» (" + linea(s, "crc") + ")");
        check(linea(s, "fram") == "fram 1111 1111 3333 5678",
              "SYSCFG0: con PFWP la FRAM no se escribe, sin FRWPPW no se abre, abierta sí; DFWP guarda la información (" + linea(s, "fram") + ")");
        check(ok && !fr.avisado_nwaits(), "Timer0_A3 a 1 ms; con NWAITS = 1 no hay aviso de estados de espera");
    }

    void r3() {
        grupo("R3 LPM3.5 con el RTC: BOR al despertar, BAKMEM, RAM perdida y LOCKLPM5");
        const bool entra = espera_uart("lpm35 entra", 100);
        for (int i = 0; i < 100 && !fr.en_lpm5(); ++i) wait(100, SC_US);
        check(entra && fr.en_lpm5() && fr.lpm35() && fr.veces_lpm5() == 1 && alto(1, 1),
              "PMMREGOFF y LPM3 con el RTC en marcha: LPM3.5, y P1.1 sigue arriba");
        const sc_time t0 = sc_time_stamp();
        for (int i = 0; i < 2000 && fr.en_lpm5(); ++i) wait(50, SC_US);
        const double ms = (sc_time_stamp() - t0).to_seconds() * 1e3;
        wait(1, SC_MS);                                     // despierto, LOCKLPM5 aún puesto
        const bool quieto = alto(1, 1) && fr.pines_bloqueados();
        char t[200];
        std::snprintf(t, sizeof t, "el RTC (VLO, 500 cuentas) despierta a los %.1f ms; P1.1 sigue arriba con LOCKLPM5 tras el BOR", ms);
        check(!fr.en_lpm5() && ms > 40 && ms < 60 && quieto, t);
        const bool l = espera_uart("lpm35 rstiv", 200);
        check(l && linea(fr.uart_tx, "lpm35 rstiv") == "lpm35 rstiv 0010 ifg 8000 rtc 1 bak BEEF ram perdida",
              "SYSRSTIV = 0x08 (despertar de LPMx.5), PMMLPM5IFG, RTCIFG, la BAKMEM sigue, la RAM no (" +
              linea(fr.uart_tx, "lpm35 rstiv") + ")");
        check(!fr.pines_bloqueados() && !alto(1, 1), "borrado LOCKLPM5, P1.1 toma lo de sus registros (tras el BOR: entrada)");
    }

    void r4() {
        grupo("R4 LPM4.5 (sin RTC) y el despertar por P2.3");
        const bool entra = espera_uart("lpm45 entra", 100);
        for (int i = 0; i < 100 && !fr.en_lpm5(); ++i) wait(100, SC_US);
        check(entra && fr.en_lpm5() && !fr.lpm35() && fr.veces_lpm5() == 2, "el RTC parado: LPM4.5");
        wait(20, SC_MS);
        const bool sigue = fr.en_lpm5();
        boton(1, true);
        wait(5, SC_MS);
        boton(1, false);
        const bool l = espera_uart("lpm45 rstiv", 200);
        check(sigue && l && linea(fr.uart_tx, "lpm45 rstiv") == "lpm45 rstiv 0010 ifg 8000 p2ifg 08 bak 0000",
              "S1 (P2.3) lo despierta: SYSRSTIV = 0x08, P2IFG.3 puesto; la BAKMEM no sigue en LPM4.5 (" +
              linea(fr.uart_tx, "lpm45 rstiv") + ")");
        check(espera_uart("fin", 50), "el programa acaba en LPM0");
    }

    void r5() {
        grupo("R5 el JSTATE por SBW: LPMx.5 y LPM0");
        // En LPM0 (el final de fr2_basico)
        SondaM430::Pines p; p.test = &fr.test; p.rst = &fr.rst;
        uint64_t j0 = 0, j1 = 0;
        {
            SondaM430 h(p, SondaM430::Modo::Sbw);
            SondaXv2 s(h);
            const uint8_t id = s.conecta();
            j0 = s.lee_jstate();
            h.desconecta();
            char t[160];
            std::snprintf(t, sizeof t, "conecta (JTAG ID 0x%02X); en LPM0 el JSTATE da 0x%016llX (CPUOFF: 0x80 arriba, sin LPMx.5)",
                          id, (unsigned long long)j0);
            check(id == 0x98 && (j0 >> 56) == 0x80, t);
        }
        // En LPM3.5: otra vuelta de fr2_basico hasta su primer LPM3.5
        corre(std::string(DIR) + "fr2_basico.elf");
        espera_uart("lpm35 entra", 3000);
        for (int i = 0; i < 100 && !fr.en_lpm5(); ++i) wait(100, SC_US);
        {
            SondaM430 h(p, SondaM430::Modo::Sbw);
            SondaXv2 s(h);
            s.conecta();
            j1 = s.lee_jstate();
            h.desconecta();
        }
        char t[160];
        std::snprintf(t, sizeof t, "en LPM3.5 el JSTATE da 0x%016llX (LPMX5_MASK_J, 0x40 arriba): la herramienta lo sigue",
                      (unsigned long long)j1);
        check(fr.en_lpm5() && (j1 >> 56) == 0x40, t);
        const bool l = espera_uart("lpm35 rstiv", 300);
        check(l, "suelta la sonda, el RTC lo despierta igual");
    }

    void r6() {
        grupo("R6 la demo de fábrica de TI (su binario): registro en la FRAM con LPM3.5, S1, y S1+S2 en vivo");
        corre("verif/fw/fr2433/OutOfBox_MSP430FR2433.txt");
        // P1.1 se enciende ~1 s al empezar; luego LPM3.5 y el RTC cada ~5 s
        wait(500, SC_MS);
        const bool led2 = alto(1, 1);
        const unsigned v0 = fr.veces_lpm5(), f0 = led1_;
        wait(12, SC_SEC);
        const unsigned flancos = led1_ - f0;
        const unsigned v1 = fr.veces_lpm5();
        char t[200];
        std::snprintf(t, sizeof t, "LED2 al empezar; en 12 s, %u entradas en LPM3.5 y %u destellos de LED1 (el RTC con el VLO, cada ~5,2 s)",
                      v1 - v0, flancos);
        check(led2 && v1 - v0 >= 2 && flancos >= 2 && flancos <= 3, t);
        check(fr.avisado_nwaits(), "la demo pone el FLL a 16 MHz sin estado de espera de la FRAM (NWAITS = 0): el modelo lo avisa [DS SLASE59F: > 8 MHz pide NWAITS = 1]");
        // S1: el registro por el backchannel, en JSON
        fr.uart_tx.clear();
        boton(1, true); wait(200, SC_MS); boton(1, false);
        const bool j = espera_uart("framTempData", 2000);
        const std::string l = linea(fr.uart_tx, "{\"framTempData\"");
        unsigned n = 0;
        for (char c : l) if (c == '.') ++n;
        check(j && l.find("{\"framTempData\":[") == 0 && l.find("]}") != std::string::npos && n >= 2 && l.find("24.") != std::string::npos,
              "S1 (despierta de LPM3.5 al soltarlo): «" + l.substr(0, 60) + "…»");
        // S1 y S2 a la vez: el modo en vivo
        fr.uart_tx.clear();
        boton(1, true); boton(2, true); wait(200, SC_MS); boton(1, false); boton(2, false);
        const bool v = espera_uart("liveTempData", 3000);
        const std::string lv = linea(fr.uart_tx, "{\"liveTempData\"");
        check(v && lv.find("{\"liveTempData\":24.") == 0 && lv.find("\"tempThreshold\":25.00}") != std::string::npos,
              "S1 y S2: el modo en vivo con el umbral («" + lv + "»)");
    }

    void run() {
        const char* solo = std::getenv("TESTFR2_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) r1();
        if (va('2') || va('3') || va('4') || va('5')) { r2(); r3(); r4(); r5(); }
        if (va('6')) r6();
        std::printf("\nTOTAL FR2: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbFr2 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
