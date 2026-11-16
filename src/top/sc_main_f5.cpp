// =============================================================================
// sc_main_f5.cpp — LA SUITE DEL MSP430F5529 (fase M4 de doc/analisis_msp430.md)
//
//   F1  el SoC contra su descriptor generado (vectores, mapa, JTAG ID, EEM) y
//       la TLV de fábrica (el ID 0x5529, la CRC, las calibraciones del ADC)
//   F2  f5_basico.c, con la DriverLib de TI: VCORE 3 y el FLL a 25 MHz, la
//       UCA1 a 115200 (el backchannel, leído en P4.4), la Flash de 5xx, el
//       sensor del ADC12_A con el REF, el DMA, el MPY32, el CRC16, una
//       función por encima de 0x10000 y Timer0_A5
//   F3  el Replicator430 de TI compilado para el F5529 (verif/f5529/replicator)
//       programa un G2553 por Spy-Bi-Wire: P6.2 -> TEST, P6.5 -> RST; borra,
//       escribe el programa de ejemplo de TI, lo verifica con la PSA y lo
//       suelta: «REP OK» por el backchannel y P1.0 del G2553 parpadea
//   F4  lo mismo por JTAG de 4 hilos (P6.0-P6.5 -> TEST, RST, P1.4-P1.7)
//   F5  f5_rtc_cb.c, con la DriverLib: el RTC_A en calendario (el cambio de
//       año, el evento de minuto y la alarma, en LPM3) y el Comp_B (CB0 contra
//       1,5 V, con P6.0 movido por el banco)
//   F6  el Replicator430Xv2 de TI, compilado para el F5529, programa OTRO
//       F5529 por Spy-Bi-Wire: el JTAG de Xv2 (CPUSUSP, la MOVA por TCLK, el
//       buzón JMB con las rutinas de Flash en la RAM del objetivo, la PSA)
//   F7  lo mismo por el JTAG de 4 hilos del objetivo (PJ.0-PJ.3)
//   F8  el patrón mágico y la firma de JTAG (los bloques de TI, activados)
//   F9  el USB (M5): el simpleUsbBackchannel de TI, su binario tal cual,
//       enumera por el puente CDC-ACM y lleva datos entre el USB y el
//       backchannel (UCA1) en los dos sentidos
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/f5529/soc_f5529.h"
#include "soc/msp430/g2553/soc_g2553.h"
#include "parts/puente_usb.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/f5529/elf/";

// Los hilos entre la F5529 del Replicator y su G2553: el G2553 usa los nodos
// de los pines del F5529 (un Cableado, como un <hilo> de un sistema)
static Cableado cab_sbw(SocF5529& f) {
    Cableado c;
    c.une_alim("TEST", f.p(6, 2));                 // SBWTCK
    c.une_alim("RST", f.p(6, 5));                  // SBWTDIO
    return c;
}
static Cableado cab_jtag(SocF5529& f) {
    Cableado c;
    c.une_alim("TEST", f.p(6, 0));
    c.une_alim("RST", f.p(6, 1));
    c.une(0, 4, f.p(6, 2));                        // TCK
    c.une(0, 5, f.p(6, 3));                        // TMS
    c.une(0, 6, f.p(6, 5));                        // TDI
    c.une(0, 7, f.p(6, 4));                        // TDO
    return c;
}

// Los del Replicator430Xv2: el objetivo es otro F5529, con SBW en TEST/RST y
// el JTAG de 4 hilos en PJ.0-PJ.3 (TDO, TDI, TMS, TCK)
static Cableado cab_xsbw(SocF5529& f) {
    Cableado c;
    c.une_alim("TEST", f.p(6, 2));
    c.une_alim("RST", f.p(6, 5));
    return c;
}
static Cableado cab_xjtag(SocF5529& f) {
    Cableado c;
    c.une_alim("TEST", f.p(6, 0));
    c.une_alim("RST", f.p(6, 1));
    c.une(10, 3, f.p(6, 2));                       // TCK
    c.une(10, 2, f.p(6, 3));                       // TMS
    c.une(10, 1, f.p(6, 5));                       // TDI
    c.une(10, 0, f.p(6, 4));                       // TDO
    return c;
}

SC_MODULE(TbF5) {
    SocF5529 f5{"f5529"};
    SocF5529 rs{"rep_sbw"}, rj{"rep_jtag"};
    Cableado c_sbw = cab_sbw(rs), c_jtag = cab_jtag(rj);
    SocG2553 gs{"g2_sbw", &c_sbw}, gj{"g2_jtag", &c_jtag};
    SocF5529 xs{"repx_sbw"}, xj{"repx_jtag"};
    Cableado c_xsbw = cab_xsbw(xs), c_xjtag = cab_xjtag(xj);
    SocF5529 ts{"f5_sbw", &c_xsbw}, tj{"f5_jtag", &c_xjtag};
    int id_pu_s_ = -1, id_pu_j_ = -1, id_pu_xs_ = -1, id_pu_xj_ = -1;
    SC_CTOR(TbF5) {
        SC_THREAD(run);
        // R10 de la G2ET: 47 kohm de RST a 3V3
        id_pu_s_ = gs.rst.register_driver("r10"); gs.rst.set_drive(id_pu_s_, 3.3f, 47000.0f);
        id_pu_j_ = gj.rst.register_driver("r10"); gj.rst.set_drive(id_pu_j_, 3.3f, 47000.0f);
        // Y la de la F5529LP (R10, 47 kohm)
        id_pu_xs_ = ts.rst.register_driver("r10"); ts.rst.set_drive(id_pu_xs_, 3.3f, 47000.0f);
        id_pu_xj_ = tj.rst.register_driver("r10"); tj.rst.set_drive(id_pu_xj_, 3.3f, 47000.0f);
        for (SocF5529* x : {&xs, &xj, &ts, &tj}) x->monitor_uart = x == &xs || x == &xj;
    }
    // El Replicator430Xv2 en un F5529 programa otro F5529
    void repx(const char* nombre, SocF5529& f, SocF5529& t, const char* elf, const char* como) {
        // El objetivo con f5_basico, y algo en la información B y A, que borrar o no
        t.reset(true);
        t.carga(std::string(DIR) + "f5_basico.elf");
        t.poke(0x1800, 0x34); t.poke(0x1801, 0x12);
        t.poke(0x1980, 0x78); t.poke(0x1981, 0x56);
        t.reset(false);
        f.reset(true);
        wait(1, SC_US);
        f.carga(std::string(DIR) + elf);
        f.uart_tx.clear();
        f.reset(false);
        for (int i = 0; i < 60000 && f.uart_tx.find("REP OK") == std::string::npos &&
                        f.uart_tx.find("REP ERROR") == std::string::npos; ++i) wait(1, SC_MS);
        wait(10, SC_MS);
        static const uint16_t EPROM[14] = {0x4031, 0x0280, 0x40B2, 0x5A80, 0x015C, 0xD3D2, 0x0204, 0xE3D2,
                                           0x0202, 0x403F, 0xC350, 0x831F, 0x23FE, 0x3FF9};
        unsigned mal = 0;
        for (unsigned i = 0; i < 14; ++i) if (t.mem16(0xFC00 + 2 * i) != EPROM[i]) ++mal;
        std::string s = f.uart_tx;
        for (char& c : s) if (c == '\n') c = '|';
        check(f.uart_tx.find("REP ACTIVO 00\nREP OK 00\n") != std::string::npos,
              std::string(nombre) + ": el Replicator430Xv2 en el F5529 (" + como + "): «" + s + "»");
        unsigned llenas = 0;
        for (uint32_t a = 0x4400; a < 0x24400; a += 2) if (a < 0xFC00 || a >= 0x10000) if (t.mem16(a) != 0xFFFF) ++llenas;
        check(!mal && t.mem16(0xFFFE) == 0xFC00 && !llenas,
              std::string(nombre) + ": la principal borrada entera (f5_basico ya no está) y el programa de TI en 0xFC00");
        check(t.mem16(0x1800) == 0xFFFF && t.mem16(0x1900) == 0xFFFF && t.mem16(0x1980) == 0x5678,
              std::string(nombre) + ": la información D, C y B borrada; la A, con su LOCKA, intacta");
        // Un cambio cada 50000 vueltas de 3 ciclos (DEC y JNZ en la CPUXv2) con
        // MCLK de 1 MHz: unos 143 ms
        unsigned flancos = 0;
        bool ant = t.p(1, 0).voltage() > 1.6f;
        const sc_time fin = sc_time_stamp() + sc_time(1000, SC_MS);
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), t.p(1, 0).value_changed_event());
            const bool v = t.p(1, 0).voltage() > 1.6f;
            if (v != ant) { ++flancos; ant = v; }
        }
        char x[140];
        std::snprintf(x, sizeof x, "%s: el F5529 objetivo, suelto, ejecuta el programa: P1.0 cambia (%u flancos en 1 s)", nombre, flancos);
        check(flancos >= 6, x);
    }
    void rep(const char* nombre, SocF5529& f, SocG2553& g, const char* elf, const char* como) {
        // El G2553 con otro programa (g2_basico) que el Replicator tiene que borrar
        g.reset(true);
        g.carga("verif/g2553/elf/g2_basico.elf");
        g.reset(false);
        f.reset(true);
        wait(1, SC_US);
        f.carga(std::string(DIR) + elf);
        f.uart_tx.clear();
        f.reset(false);
        for (int i = 0; i < 6000 && f.uart_tx.find("REP OK") == std::string::npos &&
                        f.uart_tx.find("REP ERROR") == std::string::npos; ++i) wait(1, SC_MS);
        wait(10, SC_MS);                               // el resto de la línea (9600)
        static const uint16_t EPROM[18] = {0x4031, 0x0300, 0x40B2, 0x5A80, 0x0120, 0xD3D2, 0x0022, 0xD3E2,
                                           0x0032, 0x403F, 0x9C40, 0x831F, 0x23FE, 0xE3D2, 0x0021, 0xE3E2,
                                           0x0031, 0x3FF7};
        unsigned mal = 0;
        for (unsigned i = 0; i < 18; ++i) if (g.mem16(0xFF00 + 2 * i) != EPROM[i]) ++mal;
        std::string s = f.uart_tx;
        for (char& c : s) if (c == '\n') c = '|';
        check(f.uart_tx.find("REP ACTIVO 00\nREP OK 00\n") != std::string::npos,
              std::string(nombre) + ": el Replicator430 en el F5529 (" + como + "): «" + s + "»");
        check(!mal && g.mem16(0xFFFE) == 0xFF00 && g.mem16(0xC000) == 0xFFFF,
              std::string(nombre) + ": el G2553 borrado (g2_basico ya no está) y con el programa de TI en 0xFF00");
        // Suelto, el programa del objetivo hace parpadear P1.0
        unsigned flancos = 0;
        bool ant = g.p(1, 0).voltage() > 1.6f;
        const sc_time fin = sc_time_stamp() + sc_time(500, SC_MS);
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), g.p(1, 0).value_changed_event());
            const bool v = g.p(1, 0).voltage() > 1.6f;
            if (v != ant) { ++flancos; ant = v; }
        }
        char t[120];
        std::snprintf(t, sizeof t, "%s: el G2553, suelto, ejecuta el programa: P1.0 cambia (%u flancos en 500 ms)", nombre, flancos);
        check(flancos >= 4, t);
    }
    void f3() { grupo("F3 el Replicator430 de TI en el F5529 programa un G2553 por Spy-Bi-Wire");
                rep("SBW", rs, gs, "rep_sbw.elf", "SBW en P6.2/P6.5"); }
    void f4() { grupo("F4 el Replicator430 de TI en el F5529 programa un G2553 por JTAG de 4 hilos");
                rep("JTAG", rj, gj, "rep_jtag.elf", "JTAG en P6.0-P6.5"); }

    void corre(const char* elf) {
        f5.reset(true);
        wait(1, SC_US);
        const std::string e = f5.carga(std::string(DIR) + elf);
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        f5.uart_tx.clear();
        f5.reset(false);
    }
    std::string linea(const std::string& s, const char* c) {
        const size_t i = s.find(c);
        return i == std::string::npos ? std::string() : s.substr(i, s.find('\n', i) - i);
    }

    void f1() {
        grupo("F1 el SoC contra el descriptor generado y la TLV, sin tiempo");
        const m430::Desc430& d = SocF5529::DESC;
        check(d.jtag_id == 0x91 && d.eem == m430::TamEem::L && d.ram.ini == 0x2400 && d.ram_usb.ini == 0x1C00 &&
              d.principal2.fin == 0x243FF && d.info.ini == 0x1800,
              "el descriptor: JTAG ID 0x91, EEM L, RAM 0x2400, RAM de USB 0x1C00, Flash hasta 0x243FF");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"RTC", 0xFFD2}, {"PORT2", 0xFFD4}, {"TIMER2_A1", 0xFFD6}, {"TIMER2_A0", 0xFFD8},
                       {"USCI_B1", 0xFFDA}, {"USCI_A1", 0xFFDC}, {"PORT1", 0xFFDE}, {"TIMER1_A1", 0xFFE0},
                       {"TIMER1_A0", 0xFFE2}, {"DMA", 0xFFE4}, {"TIMER0_A1", 0xFFE8}, {"TIMER0_A0", 0xFFEA},
                       {"ADC12", 0xFFEC}, {"USCI_B0", 0xFFEE}, {"USCI_A0", 0xFFF0}, {"WDT", 0xFFF2},
                       {"TIMER0_B1", 0xFFF4}, {"TIMER0_B0", 0xFFF6}, {"COMP_B", 0xFFF8}, {"UNMI", 0xFFFA}, {"SYSNMI", 0xFFFC}, {"RESET", 0xFFFE}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 22 vectores que usa el SoC, como en msp430f5529.h");
        check(f5.mem16(0x1A04) == 0x2955 && d.id_dispositivo == 0x2955, "la TLV: el ID del chip en 0x1A04 (0x2955, el de devicedb)");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a < 0x1B00; ++a) {
            const uint8_t b = f5.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(f5.mem16(0x1A02) == c, "la TLV: su CRC-CCITT en 0x1A02");
        check(f5.mem8(0x1A14) == 0x11 && f5.mem16(0x1A1A) < f5.mem16(0x1A1C),
              "la TLV: TLV_ADC12CAL en 0x1A14, CAL_ADC_15T30 < CAL_ADC_15T85");
    }
    void f2() {
        grupo("F2 f5_basico.c con la DriverLib: VCORE, FLL, UART, Flash, ADC12_A, DMA, MPY32, CRC16, Timer_A");
        corre("f5_basico.elf");
        for (int i = 0; i < 2000 && f5.uart_tx.find("ta0 10 ms") == std::string::npos; ++i) {
            wait(1, SC_MS);
        }
        const std::string s = f5.uart_tx;
        char t[200];
        // 25 MHz con REFO: D = 2 y N = 763/2 - 1 = 380 -> 2·381·32768 / 2 = 24,969 MHz
        std::snprintf(t, sizeof t, "PMM_setVCore(3) y UCS_initFLLSettle(25000, 763): MCLK %.3f MHz, VCORE %u («%s»)",
                      f5.f_mclk() / 1e6, f5.vcore(), linea(s, "hola").c_str());
        check(std::fabs(f5.f_mclk() - 24.969216e6) < 1e3 && f5.vcore() == 3 && !f5.avisado_vcore() &&
              linea(s, "hola") == "hola F5529 24969 kHz", t);
        check(linea(s, "flash") == "flash 5529 BEEF ok", "FlashCtl: el segmento D borrado, escrito y la comprobación de borrado (" + linea(s, "flash") + ")");
        long dc = 0;
        std::sscanf(linea(s, "temp").c_str(), "temp %ld", &dc);
        std::snprintf(t, sizeof t, "ADC12_A: el sensor con el REF a 1,5 V y la TLV: %.1f °C (25)", dc / 10.0);
        check(std::labs(dc - 250) <= 10, t);
        check(linea(s, "dma") == "dma 16 ifg", "DMA: un bloque de 16 palabras por DMAREQ, y DMAIFG (" + linea(s, "dma") + ")");
        check(linea(s, "mpy") == "mpy 0000001CBE991A08 -233331", "MPY32 por libgcc: 123456789·1000 en 64 bits y -77777·3 (" + linea(s, "mpy") + ")");
        check(linea(s, "crc") == "crc 29B1", "CRC16: CRC-CCITT de «123456789» = 0x29B1 (" + linea(s, "crc") + ")");
        check(linea(s, "arriba") == "arriba 10000 124", "una función en 0x10000 (-mlarge, CALLA): 41·3+1 (" + linea(s, "arriba") + ")");
        check(s.find("ta0 10 ms") != std::string::npos, "Timer0_A5 a 1 ms con su interrupción");
    }

    void f6() { grupo("F6 el Replicator430Xv2 de TI en el F5529 programa otro F5529 por Spy-Bi-Wire");
                repx("SBW", xs, ts, "repx_sbw.elf", "SBW en P6.2/P6.5"); }
    void f7() { grupo("F7 el Replicator430Xv2 de TI en el F5529 programa otro F5529 por JTAG de 4 hilos");
                repx("JTAG", xj, tj, "repx_jtag.elf", "JTAG en P6.0-P6.5 a PJ.0-PJ.3"); }

    // Corre un Replicator en `f` hasta su REP OK o REP ERROR; devuelve lo que dijo
    std::string corre_rep(SocF5529& f, const char* elf) {
        f.reset(true);
        wait(1, SC_US);
        f.carga(std::string(DIR) + elf);
        f.uart_tx.clear();
        f.reset(false);
        for (int i = 0; i < 60000 && f.uart_tx.find("REP OK") == std::string::npos &&
                        f.uart_tx.find("REP ERROR") == std::string::npos; ++i) wait(1, SC_MS);
        wait(10, SC_MS);
        std::string s = f.uart_tx;
        for (char& c : s) if (c == '\n') c = '|';
        return s;
    }
    void f8() {
        grupo("F8 el patrón mágico y la firma de JTAG de 5xx, con los bloques de TI del Replicator430Xv2");
        // El objetivo de F6, con el programa de TI corriendo
        SocF5529& t = ts;
        t.reset(true);
        t.carga(std::string(DIR) + "f5_basico.elf");
        t.reset(false);
        wait(20, SC_MS);
        const unsigned antes = t.veces_patron();
        const std::string s = corre_rep(xs, "repx_firma.elf");
        check(s.find("REP ACTIVO 00|REP OK 00|") != std::string::npos,
              "magicPattern() da el JTAG ID 0x91 y ProgramLockKey() acaba con el chip bloqueado: «" + s + "»");
        check(t.veces_patron() == antes + 1,
              "el patrón mágico: 0xA55A en JMBI0 durante la entrada (TEST arriba) y el arranque deja la CPU parada hasta que JTAG la toma");
        check(t.mem16(0x17FC) == 0xDEAD && t.mem16(0x17FE) == 0xBABE && t.bloqueado_por_firma(),
              "la firma 0xDEADBABE en 0x17FC (con SYSBSLPE abierto por SYSBSLC = 3) y, tras el BOR de TEST_REG, el JTAG bloqueado");
        // Bloqueado, el Replicator normal no entra: GetDevice da STATUS_FUSEBLOWN
        const std::string s2 = corre_rep(xs, "repx_sbw.elf");
        check(s2.find("REP ERROR 01") != std::string::npos && t.mem16(0xFC00) == 0x4031,
              "con la firma, el Replicator430Xv2 se para en GetDevice (registros de 1 bit, como el fusible) y no toca la Flash: «" + s2 + "»");
    }

    // M5: el ejemplo USB de TI (su binario, tal cual) con el puente CDC-ACM
    void f9() {
        grupo("F9 el USB: simpleUsbBackchannel de TI (su binario) enumera como CDC-ACM y lleva el backchannel");
        f5.reset(true);
        wait(1, SC_US);
        const std::string e = f5.carga("verif/fw/f5529/SimpleUsbBackchannel.txt");
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        f5.uart_tx.clear();
        f5.usb.vbus(true);                             // el cable: VBUS
        f5.reset(false);
        PuenteUsbCdc pu(f5.pu0, f5.pu1, &f5.usb, serie::parsea("memoria"));
        pu.controlador().pausa_control_us = 125.0;
        pu.ev_in = &f5.usb.ev_in();                    // el PC pide IN sin parar
        for (int i = 0; i < 3000 && !pu.listo(); ++i) wait(1, SC_MS);
        char t[200];
        std::snprintf(t, sizeof t, "enumerado %04X:%04X «%s» en %s (VID de TI, PID del ejemplo CDC)",
                      pu.vid(), pu.pid(), pu.producto().c_str(), sc_time_stamp().to_string().c_str());
        check(pu.listo() && pu.vid() == 0x2047 && pu.pid() == 0x0403, t);
        CanalMemoria& m = *pu.memoria();
        m.empuja("hola por USB\n");
        espera_uart("hola por USB", 500);
        check(linea(f5.uart_tx, "hola") == "hola por USB",
              "del PC al chip: lo del CDC sale por la UART del backchannel (UCA1, P4.4): «" + linea(f5.uart_tx, "hola") + "»");
        for (char c : std::string("desde la UART\n")) f5.uart_rx(uint8_t(c));
        for (int i = 0; i < 500 && m.recibido().find('\n') == std::string::npos; ++i) wait(1, SC_MS);
        check(m.recibido() == "desde la UART\n",
              "del backchannel al PC: lo que entra por P4.5 sale por el IN de bulk del CDC: «" +
              m.recibido().substr(0, m.recibido().find('\n')) + "»");
        std::snprintf(t, sizeof t, "la PLL de USB enganchada desde XT2 (4 MHz) y %llu paquetes contestados",
                      (unsigned long long)f5.usb.paquetes());
        check(f5.usb.pll_enganchada() && f5.usb.paquetes() > 20, t);
        pu.enchufa(false);
        f5.usb.vbus(false);
        wait(5, SC_MS);
    }

    // Espera a que salga `c` por la UART, como mucho `ms`
    // (y el resto de su línea)
    bool espera_uart(const char* c, int ms) {
        auto entera = [&] { const size_t i = f5.uart_tx.find(c); return i != std::string::npos && f5.uart_tx.find('\n', i) != std::string::npos; };
        for (int i = 0; i < ms && !entera(); ++i) wait(1, SC_MS);
        return entera();
    }
    void f5_() {
        grupo("F5 f5_rtc_cb.c con la DriverLib: el RTC_A en calendario y el Comp_B");
        corre("f5_rtc_cb.elf");
        AnalogNet* cb0 = dynamic_cast<AnalogNet*>(&f5.p(6, 0));
        const int d = cb0->register_driver("banco_cb0");
        cb0->set_drive(d, 1.0f, 10.0f);
        const bool listo = espera_uart("listo cb", 200);
        check(listo && linea(f5.uart_tx, "listo") == "listo cb 0", "Comp_B: CB0 a 1,0 V contra 1,5 V, CBOUT = 0 (" + linea(f5.uart_tx, "listo") + ")");
        cb0->set_drive(d, 2.0f, 10.0f);
        const bool sube = espera_uart("cb sube", 50);
        cb0->set_drive(d, 1.0f, 10.0f);
        const bool baja = espera_uart("cb baja", 50);
        check(sube && baja && linea(f5.uart_tx, "cb sube") == "cb sube 1" && linea(f5.uart_tx, "cb baja") == "cb baja 0",
              "Comp_B: P6.0 a 2,0 V y a 1,0 V: CBIFG y CBIIFG por CBIV, con CBOUT a 1 y a 0");
        cb0->set_hiz(d);
        const bool rtc = espera_uart("rtc ", 5000);
        check(rtc && linea(f5.uart_tx, "rtc") == "rtc 2026-01-01 00:00:00 4 tev+alarma",
              "RTC_A: de 31-12-2025 23:59:57 al cambio de año en LPM3; RTCIV da el evento y luego la alarma (" +
              linea(f5.uart_tx, "rtc") + ")");
        char t[120];
        std::snprintf(t, sizeof t, "RTC_A: tres segundos de ACLK (REFO): %u segundos contados", unsigned(f5.rtc.segundos_contados()));
        check(f5.rtc.segundos_contados() == 3, t);
    }

    void run() {
        const char* solo = std::getenv("TESTF5_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) f1();
        if (va('2')) f2();
        if (va('3')) f3();
        if (va('4')) f4();
        if (va('5')) f5_();
        if (va('6')) f6();
        if (va('7')) f7();
        if (va('8')) f8();
        if (va('9')) f9();
        std::printf("\nTOTAL F5: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, SC_DO_NOTHING);
    TbF5 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n",
                (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12)));
    return g_fallos ? 1 : 0;
}
