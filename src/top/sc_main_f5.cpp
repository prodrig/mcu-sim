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
//   F10 (TESTF5_SOLO=A) el RTC_A en modo contador: el evento de 8 y de 32
//       bits, RT0PS y RT1PS encadenados y RTCHOLD
//   F11 (TESTF5_SOLO=B) CBOUT del Comp_B en P1.6 y en la captura del TA1
//   F12 (C) lo del USB que no usa la pila de TI: Port U, el temporizador y la
//       marca de tiempo, la suspensión con RWUP y la sobrecarga del LDO
//   F13 (D) la sonda de bancada con las secuencias de Xv2 (SondaXv2 y
//       DbgSondaXv2, lo que hay detrás de --gdb por los pines): SBW en
//       TEST/RST de un F5529 con f5_basico; la memoria, los registros de 20
//       bits, el paso, una ruptura del EEM en 0x10000, la Flash con las rutinas
//       del Replicator430Xv2 y el POR por JTAG
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
#include "m430/gdb/gdb_m430.h"

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
    SocF5529 sx{"f5_sonda"};
    int id_pu_s_ = -1, id_pu_j_ = -1, id_pu_xs_ = -1, id_pu_xj_ = -1;
    SC_CTOR(TbF5) {
        SC_THREAD(run);
        // R10 de la G2ET: 47 kohm de RST a 3V3
        id_pu_s_ = gs.rst.register_driver("r10"); gs.rst.set_drive(id_pu_s_, 3.3f, 47000.0f);
        id_pu_j_ = gj.rst.register_driver("r10"); gj.rst.set_drive(id_pu_j_, 3.3f, 47000.0f);
        // Y la de la F5529LP (R10, 47 kohm)
        id_pu_xs_ = ts.rst.register_driver("r10"); ts.rst.set_drive(id_pu_xs_, 3.3f, 47000.0f);
        id_pu_xj_ = tj.rst.register_driver("r10"); tj.rst.set_drive(id_pu_xj_, 3.3f, 47000.0f);
        { const int d = sx.rst.register_driver("r10"); sx.rst.set_drive(d, 3.3f, 47000.0f); }
        for (SocF5529* x : {&xs, &xj, &ts, &tj, &sx}) x->monitor_uart = x == &xs || x == &xj || x == &sx;
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

    // F10: el RTC_A en modo contador
    void f10() {
        grupo("F10 f5_rtc_cnt.c con la DriverLib: el RTC_A en modo contador, con sus preescaladores");
        corre("f5_rtc_cnt.elf");
        // (los tiempos llevan lo que tarda la línea en salir a 9600: ~1 ms por carácter)
        const bool a0 = espera_uart("A\n", 200);
        const bool a1 = espera_uart("tev8 ", 200);
        char t[200];
        const std::string la = linea(f5.uart_tx, "tev8");
        std::snprintf(t, sizeof t, "RTCNT de ACLK con el evento de 8 bits: el cuarto RTCTEVIFG deja «%s» (0x400 y lo que tarda en leerlo)", la.c_str());
        check(a0 && a1 && la.size() == 13 && la.compare(5, 6, "000004") == 0, t);
        const bool b0 = espera_uart("B\n", 50);
        const sc_time tb = sc_time_stamp();
        const bool b1 = espera_uart("ps1 ", 1500);
        const double d1 = (sc_time_stamp() - tb).to_seconds();
        const bool b2 = espera_uart("tev32 ", 1500);
        const double d2 = (sc_time_stamp() - tb).to_seconds();
        std::snprintf(t, sizeof t, "la cadena ACLK/256/128 a 1 Hz: RT1PSIFG a %.3f s con «%s», y el desborde de 32 bits a %.3f s con «%s»",
                      d1, linea(f5.uart_tx, "ps1").c_str(), d2, linea(f5.uart_tx, "tev32").c_str());
        check(b0 && b1 && b2 && linea(f5.uart_tx, "ps1") == "ps1 FFFFFFFF" && linea(f5.uart_tx, "tev32") == "tev32 00000000" &&
              std::fabs(d1 - 1.0) < 0.02 && std::fabs(d2 - 2.0) < 0.02, t);
        const bool c = espera_uart("hold ", 3000);
        check(c && linea(f5.uart_tx, "hold") == "hold ok", "RTCHOLD para el contador (" + linea(f5.uart_tx, "hold") + ")");
    }

    // F11: CBOUT en P1.6 y en la captura del Timer1_A3
    void f11() {
        grupo("F11 f5_cbout.c: CBOUT del Comp_B en P1.6 y en la captura CCI1B del Timer1_A3");
        corre("f5_cbout.elf");
        AnalogNet* cb0 = dynamic_cast<AnalogNet*>(&f5.p(6, 0));
        const int d = cb0->register_driver("banco_cbout");
        cb0->set_drive(d, 1.0f, 10.0f);
        const bool listo = espera_uart("listo", 200);
        const bool bajo = f5.p(1, 6).voltage() < 0.3f;
        cb0->set_drive(d, 2.0f, 10.0f);
        wait(5, SC_US);
        const bool alto = f5.p(1, 6).voltage() > 3.0f;
        wait(995, SC_US);                                  // 1 ms arriba
        cb0->set_drive(d, 1.0f, 10.0f);
        wait(5, SC_US);
        const bool bajo2 = f5.p(1, 6).voltage() < 0.3f;
        check(listo && bajo && alto && bajo2, "P1.6 (SEL y DIR) sigue a CBOUT: abajo, arriba con CB0 a 2,0 V y abajo otra vez");
        const bool cap = espera_uart("cap ", 100);
        const std::string l = linea(f5.uart_tx, "cap");
        const long us = cap ? std::strtol(l.c_str() + 4, nullptr, 10) : -1;
        char t[160];
        std::snprintf(t, sizeof t, "TA1 captura los dos flancos de CBOUT por CCI1B: %ld ciclos de SMCLK entre ellos (1 ms a 1,05 MHz)", us);
        check(cap && us > 1000 && us < 1100, t);
        cb0->set_hiz(d);
    }

    // F12: lo del módulo USB que no usa la pila de TI
    void f12() {
        grupo("F12 f5_usb_hw.c: Port U, el temporizador y la marca de tiempo de USB, RWUP y la sobrecarga del LDO");
        f5.usb.vbus(true);
        corre("f5_usb_hw.elf");
        char t[200];
        const bool s0 = espera_uart("pu sale", 200);
        const float v0 = f5.pu0.voltage(), v1 = f5.pu1.voltage();
        std::snprintf(t, sizeof t, "Port U como salidas (PUOPE): PU.0 a %.2f V y PU.1 a %.2f V (PUOUT0 = 1, PUOUT1 = 0)", v0, v1);
        check(s0 && v0 > 3.0f && v1 < 0.3f, t);
        const bool s1 = espera_uart("pu entra", 200);
        AnalogNet* p0 = dynamic_cast<AnalogNet*>(&f5.pu0);
        AnalogNet* p1 = dynamic_cast<AnalogNet*>(&f5.pu1);
        const int d0 = p0->register_driver("banco_pu0"), d1 = p1->register_driver("banco_pu1");
        p0->set_drive(d0, 0.0f, 50.0f); p1->set_drive(d1, 3.3f, 50.0f);
        const bool s2 = espera_uart("puin", 100);
        check(s1 && s2 && linea(f5.uart_tx, "puin") == "puin 08", "Port U como entradas (PUIPE): PUIN0 = 0 y PUIN1 = 1 (" + linea(f5.uart_tx, "puin") + ")");
        p0->set_hiz(d0); p1->set_hiz(d1);
        espera_uart("pll", 100);
        const bool s3 = espera_uart("ut ", 100);
        const long ut = std::strtol(linea(f5.uart_tx, "ut ").c_str() + 3, nullptr, 10);
        const bool s4 = espera_uart("ts ", 100);
        const long ts = std::strtol(linea(f5.uart_tx, "ts ").c_str() + 3, nullptr, 10);
        const bool s5 = espera_uart("tsdma ", 100);
        const long td = std::strtol(linea(f5.uart_tx, "tsdma").c_str() + 6, nullptr, 10);
        std::snprintf(t, sizeof t, "el temporizador de USB: %ld UTIFG en ~2 ms con UTSEL 5 (128 us), PLL «%s»", ut, linea(f5.uart_tx, "pll").c_str());
        // (la RSI alarga el bucle de espera: entre 15 y 30 son de 2 a 4 ms)
        check(s3 && ut >= 15 && ut <= 30 && linea(f5.uart_tx, "pll") == "pll 00", t);
        std::snprintf(t, sizeof t, "la marca de tiempo a 16 MHz: %ld cuentas entre dos TSE3 (~100 us) y %ld hasta el disparo del DMA0 (~210 us)", ts, td);
        check(s4 && s5 && ts > 1500 && ts < 2000 && td > 3000 && td < 4000, t);
        const bool s6 = espera_uart("susp listo", 200);
        // Un anfitrión que deja de mandar SOF y vuelve con la reanudación remota
        bool despierto = false;
        f5.usb.al_rwup = [&despierto] { despierto = true; };
        for (unsigned k = 0; k < 4; ++k) { f5.usb.sof(uint16_t(k)); wait(1, SC_MS); }
        auto linea_entera = [this](const char* c) {
            const size_t i = f5.uart_tx.find(c);
            return i != std::string::npos && f5.uart_tx.find('\n', i) != std::string::npos;
        };
        for (unsigned k = 4; k < 80 && !linea_entera("susr"); ++k) {
            if (despierto) f5.usb.sof(uint16_t(k));
            wait(1, SC_MS);
        }
        const unsigned rwup = f5.usb.reanudaciones_remotas();
        f5.usb.al_rwup = nullptr;
        const bool s7 = linea_entera("susr");
        std::snprintf(t, sizeof t, "3 ms sin SOF: SUSRIFG; el programa pide RWUP y el anfitrión ve %u reanudación remota: RESRIFG («%s»)",
                      rwup, linea(f5.uart_tx, "susr").c_str());
        check(s6 && s7 && linea(f5.uart_tx, "susr") == "susr 1 resr 1" && rwup == 1, t);
        const bool s8 = espera_uart("ovl listo", 100);
        f5.usb.sobrecarga(true);
        const bool s9 = espera_uart("ovl vusben", 50);
        check(s8 && s9 && linea(f5.uart_tx, "ovl vusben") == "ovl vusben 0",
              "un corto en VUSB: VUOVLIFG por USBVECINT y, con OVLAOFF, VUSBEN a 0 (" + linea(f5.uart_tx, "ovl vusben") + ")");
        f5.usb.vbus(false);
    }

    // F13: la sonda de Xv2 por SBW, sin GDB por medio
    void f13() {
        grupo("F13 la sonda de bancada con las secuencias de Xv2 (SondaXv2, DbgSondaXv2) por SBW");
        sx.reset(true);
        sx.carga(std::string(DIR) + "f5_basico.elf");
        sx.reset(false);
        // f5_basico hasta su bucle en LPM0 (con la RSI del TA0 cada ms)
        for (int i = 0; i < 2000 && sx.uart_tx.find("ta0 10 ms") == std::string::npos; ++i) wait(1, SC_MS);
        wait(2, SC_MS);
        SondaM430::Pines p; p.test = &sx.test; p.rst = &sx.rst;
        SondaM430 h(p, SondaM430::Modo::Sbw);
        SondaXv2 s(h, 0x2400);
        DbgSondaXv2 d(s, sx.eem.disparadores());
        char t[200];
        const bool eng = d.enganchar();
        const uint32_t pc = d.reg(0);
        const uint32_t sr = d.reg(2);
        std::snprintf(t, sizeof t, "conecta (JTAG ID 0x91) y toma la CPU dormida en LPM0: PC = 0x%05X, SR = 0x%04X (CPUOFF y GIE)",
                      unsigned(pc), unsigned(sr));
        check(eng && sx.nucleo.en_tclk() && pc >= 0x4400 && pc < 0x24400 && (sr & 0x0018) == 0x0018, t);
        d.escribe16(0x2400, 0xBEEF);
        const uint16_t r = d.lee16(0x2400);
        std::snprintf(t, sizeof t, "la RAM por JTAG: 0xBEEF escrito y leído (0x%04X); la TLV: 0x%04X en 0x1A04 (el ID 0x5529)", r, d.lee16(0x1A04));
        check(r == 0xBEEF && d.lee16(0x1A04) == 0x2955 && sx.mem16(0x2400) == 0xBEEF, t);
        d.pon_reg(12, 0xABCDE);
        d.pon_reg(5, 0x01234);
        const uint32_t r12 = d.reg(12), r5 = d.reg(5), pc2 = d.reg(0);
        std::snprintf(t, sizeof t, "los registros de 20 bits: R12 = 0x%05X, R5 = 0x%05X (MOVA por JTAG), y el PC sigue (0x%05X)",
                      unsigned(r12), unsigned(r5), unsigned(pc2));
        check(r12 == 0xABCDE && r5 == 0x01234 && sx.nucleo.cpu.reg(12) == 0xABCDE && pc2 == pc, t);
        // El paso: un MOV #imm,R4 escrito en la RAM
        d.escribe16(0x2500, 0x4034); d.escribe16(0x2502, 0x1357); d.escribe16(0x2504, 0x3FFF);
        d.pon_reg(0, 0x2500);
        d.paso();
        std::snprintf(t, sizeof t, "un paso: MOV #0x1357,R4 en 0x2500; PC = 0x%05X, R4 = 0x%04X",
                      unsigned(d.reg(0)), unsigned(d.reg(4)));
        check(d.reg(0) == 0x2504 && d.reg(4) == 0x1357, t);
        // Una ruptura en arriba() (0x10000) con el disparador 0 del EEM, como el Z1 de GDB
        d.reset(true);
        const uint32_t v = d.reg(0);
        std::snprintf(t, sizeof t, "el POR por JTAG (SyncJtag_AssertPor): el PC en el vector de reset, 0x%05X", unsigned(v));
        check(v == sx.mem16(0xFFFE), t);
        d.eem_escribe(0x00, 0x10000);                      // MBTRIGx_VAL
        d.eem_escribe(0x02, 0x0000);                       // CTL: búsqueda, MAB, ==
        d.eem_escribe(0x04, 0x0000);                       // MSK
        d.eem_escribe(0x06, 0x0001);                       // CMB
        d.eem_escribe(0x80, 0x0001);                       // BREAKREACT
        d.eem_escribe(0x82, 0x0001 | 0x0002);              // EEM_EN | CLEAR_STOP
        d.seguir();
        bool par = false;
        for (int i = 0; i < 400 && !(par = d.parado()); ++i) wait(1, SC_MS);
        std::snprintf(t, sizeof t, "la ruptura del EEM en arriba() (0x10000): parada con E_STOPPED, PC = 0x%05X, R12 = %u",
                      unsigned(d.reg(0)), unsigned(d.reg(12)));
        check(par && d.reg(0) == 0x10000 && d.reg(12) == 41, t);
        d.eem_escribe(0x80, 0);
        // La Flash: un segmento de la principal y la información B con las rutinas de TI
        const uint16_t w[4] = {0x1111, 0x2222, 0x3333, 0x4444};
        const bool b1 = d.flash_borra(0x1880, false);
        const bool e1 = d.flash_escribe(0x1880, w, 4);
        const uint16_t a = d.lee16(0x1880), b = d.lee16(0x1886);
        std::snprintf(t, sizeof t, "la Flash (información B) con FlashErase_o/FlashWrite_o en la RAM del objetivo y el JMB: 0x%04X ... 0x%04X", a, b);
        check(b1 && e1 && a == 0x1111 && b == 0x4444 && sx.mem16(0x1882) == 0x2222, t);
        const uint16_t w2[2] = {0x5A5A, 0xA5A5};
        const bool b2 = d.flash_borra(0x20000, false);
        const bool e2 = d.flash_escribe(0x20000, w2, 2);
        std::snprintf(t, sizeof t, "y por encima de 0x10000: 0x20000 = 0x%04X, 0x20002 = 0x%04X, 0x201FE borrado (0x%04X)",
                      d.lee16(0x20000), d.lee16(0x20002), d.lee16(0x201FE));
        check(b2 && e2 && d.lee16(0x20000) == 0x5A5A && d.lee16(0x20002) == 0xA5A5 && d.lee16(0x201FE) == 0xFFFF, t);
        // Y suelta: f5_basico desde el reset, por el backchannel
        sx.uart_tx.clear();
        d.reset(false);
        h.desconecta();
        for (int i = 0; i < 2000 && sx.uart_tx.find("arriba") == std::string::npos; ++i) wait(1, SC_MS);
        check(sx.uart_tx.find("arriba") != std::string::npos && !sx.nucleo.parado(),
              "monitor reset (sin parar) y suelta: f5_basico vuelve a arrancar y escribe por la UCA1");
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

    // F14 (TESTF5_SOLO=E): el BSL USB de Flash (B1: el de TI, BSD-3, portado a
    // gcc) con un anfitrión HID en el banco: el chip en blanco con VBUS entra
    // en el BSL, enumera como 2047:0200 y contesta la batería; con
    // BSL_TI=<BSL_00.08.88.39.txt>, la misma batería con la imagen de TI (B2)
    // tiene que dar lo mismo
    struct HostHid {
        TbF5& tb;
        ControladorUsb hc;
        uint8_t ep_in = 0, ep_out = 0;
        uint16_t vid = 0, pid = 0;
        int pd_ = -1, pd2_ = -1;
        // Las de 15 kOhm a masa del anfitrión en D+ y D- [USB 2.0 §7.1.5]
        explicit HostHid(TbF5& t) : tb(t), hc(&t.f5.usb) {
            hc.pausa_control_us = 125.0;
            pd_ = t.f5.pu0.register_driver("hid_pd"); pd2_ = t.f5.pu1.register_driver("hid_pd");
            t.f5.pu0.set_drive(pd_, 0.0f, 15.0e3f); t.f5.pu1.set_drive(pd2_, 0.0f, 15.0e3f);
        }
        ~HostHid() { tb.f5.pu0.set_hiz(pd_); tb.f5.pu1.set_hiz(pd2_); }
        void tramas(int ms) { for (int i = 0; i < ms; ++i) { hc.sof(); sc_core::wait(1, SC_MS); } }
        bool reset_bus() {
            const int a = tb.f5.pu0.register_driver("hid_rst"), b = tb.f5.pu1.register_driver("hid_rst");
            tb.f5.pu0.set_drive(a, 0.0f, 45.0f); tb.f5.pu1.set_drive(b, 0.0f, 45.0f);
            sc_core::wait(10, SC_MS);
            tb.f5.pu0.set_hiz(a); tb.f5.pu1.set_hiz(b);
            sc_core::wait(10, SC_US);
            tramas(10);
            if (std::getenv("BSL_VER")) std::printf("reset_bus: D+ %.2f V pc %05X t=%s entradas %u pucs %u\n", tb.f5.pu0.voltage(), unsigned(tb.f5.nucleo.cpu.pc()), sc_time_stamp().to_string().c_str(), tb.f5.bsl_entradas, tb.f5.pucs());
            return tb.f5.pu0.voltage() > 2.0f;
        }
        static bool dbg(int k) { if (std::getenv("BSL_VER")) std::printf("enumera falla en %d\n", k); return false; }
        bool enumera() {
            for (int i = 0; i < 6000 && tb.f5.pu0.voltage() < 2.0f; ++i) sc_core::wait(1, SC_MS);       // el 1,5 kOhm de PUR_EN
            if (std::getenv("BSL_VER")) std::printf("pur %.2f V t=%s pc %05X\n", tb.f5.pu0.voltage(), sc_time_stamp().to_string().c_str(), unsigned(tb.f5.nucleo.cpu.pc()));
            for (int estable = 0, i = 0; estable < 100 && i < 6000; ++i) {          // el rebote de USB 2.0 §7.1.7.3: 100 ms seguidos
                tramas(1);
                estable = tb.f5.pu0.voltage() > 2.0f ? estable + 1 : 0;
            }
            if (!reset_bus()) return dbg(1);
            std::vector<uint8_t> d;
            if (hc.control_in(0, ControladorUsb::setup(0x80, 6, 0x0100, 0, 8), d, 8) != PID_ACK || d.size() < 8) return dbg(2);
            const unsigned mps0 = d[7] ? d[7] : 8;
            if (!reset_bus()) return dbg(3);
            if (hc.control_out(0, ControladorUsb::setup(0x00, 5, 1, 0, 0)) != PID_ACK) return dbg(4);
            tramas(2);
            if (hc.control_in(1, ControladorUsb::setup(0x80, 6, 0x0100, 0, 18), d, mps0) != PID_ACK || d.size() < 18) return dbg(5);
            vid = uint16_t(d[8] | (d[9] << 8)); pid = uint16_t(d[10] | (d[11] << 8));
            if (hc.control_in(1, ControladorUsb::setup(0x80, 6, 0x0200, 0, 9), d, mps0) != PID_ACK || d.size() < 9) return dbg(6);
            const uint16_t total = uint16_t(d[2] | (d[3] << 8));
            if (hc.control_in(1, ControladorUsb::setup(0x80, 6, 0x0200, 0, total), d, mps0) != PID_ACK || d.size() < total) return dbg(7);
            if (std::getenv("BSL_VER")) { std::printf("conf %zu:", d.size()); for (uint8_t x : d) std::printf(" %02X", x); std::printf("\n"); }
            for (size_t i = 0; i + 6 < d.size(); i += d[i] ? d[i] : 1)
                if (d[i + 1] == 5) { if (d[i + 2] & 0x80) ep_in = d[i + 2] & 0x0F; else ep_out = d[i + 2] & 0x0F; }
            if (hc.control_out(1, ControladorUsb::setup(0x00, 9, d[5], 0, 0)) != PID_ACK) return dbg(8);
            tramas(5);
            return ep_in && ep_out;
        }
        // Un informe: 0x3F, la longitud y el núcleo; devuelve el núcleo de la respuesta (o nada)
        bool orden(const std::vector<uint8_t>& nucleo, std::vector<uint8_t>* resp) {
            std::vector<uint8_t> r{0x3F, uint8_t(nucleo.size())};
            r.insert(r.end(), nucleo.begin(), nucleo.end());
            r.resize(64, 0xAC);
            std::vector<uint8_t> e;
            const uint8_t po = hc.insiste(PID_OUT, 1, ep_out, r, e);
            if (std::getenv("BSL_VER") && nucleo[0] == 0x19) std::printf("orden 19: OUT %u enum %02X susp %02X conf %02X\n", po, tb.f5.mem8(0x2412), tb.f5.mem8(0x2411), tb.f5.mem8(0x2400));
            if (po != PID_ACK) return false;
            if (!resp) { tramas(2); return true; }
            for (int i = 0; i < 500; ++i) {
                if (hc.paquete(PID_IN, 1, ep_in, {}, e) == PID_ACK && e.size() >= 2 && e[0] == 0x3F) {
                    resp->assign(e.begin() + 2, e.begin() + 2 + std::min<size_t>(e[1], e.size() - 2));
                    return true;
                }
                hc.sof(); sc_core::wait(1, SC_MS);
            }
            return false;
        }
    };
    // La batería: lo que contesta el BSL de Flash, en texto
    std::string bateria(HostHid& h) {
        std::string r;
        auto hx = [&](const std::vector<uint8_t>& v) { char b[4]; for (uint8_t c : v) { std::snprintf(b, sizeof b, "%02X", c); r += b; } r += ' '; };
        std::vector<uint8_t> v;
        std::vector<uint8_t> pw{0x11};
        for (int i = 0; i < 32; ++i) pw.push_back(0xFF);
        r += h.orden(pw, &v) ? "" : "!"; hx(v);                                   // la contraseña (el chip en blanco)
        r += h.orden({0x19}, &v) ? "" : "!"; hx(v);                               // la versión: no la tiene el de Flash
        r += h.orden({0x10, 0x00, 0x25, 0x00, 0x55}, &v) ? "" : "!"; hx(v);       // RX Data Block (lento): tampoco
        // RX Data Block Fast: un programa en RAM (0x2500) que enciende P1.0 (LED1) y se para
        const std::vector<uint8_t> prog{0xD2, 0xD3, 0x04, 0x02,  0xD2, 0xD3, 0x02, 0x02,  0xFF, 0x3F};   // bis.b #1,&P1DIR; bis.b #1,&P1OUT; jmp $
        std::vector<uint8_t> c{0x1B, 0x00, 0x25, 0x00};
        c.insert(c.end(), prog.begin(), prog.end());
        r += h.orden(c, nullptr) ? "fast " : "!fast ";
        r += h.orden({0x17, 0x00, 0x25, 0x00}, nullptr) ? "pc " : "!pc ";        // Load PC
        h.tramas(20);
        r += f5.p(1, 0).voltage() > 1.6f ? "led" : "sinled";
        return r;
    }
    bool entra_hid(HostHid& h, unsigned& entradas) {
        f5.usb.vbus(false);                                          // el cable fuera y dentro
        sc_core::wait(10, SC_MS);
        f5.reset(true);
        sc_core::wait(1, SC_US);
        f5.en_blanco();
        const unsigned n0 = f5.bsl_entradas;
        f5.usb.vbus(true);
        f5.reset(false);
        sc_core::wait(5, SC_MS);
        entradas = f5.bsl_entradas - n0;
        return h.enumera();
    }
    std::string corre_bsl(HostHid& h, unsigned& entradas) {
        if (!entra_hid(h, entradas)) return "sin enumerar";
        return bateria(h);
    }
    void f14() {
        grupo("F14 el BSL USB de Flash (B1, el de TI portado a gcc): entra con el chip en blanco y VBUS, enumera y contesta");
        HostHid h(*this);
        unsigned n = 0;
        if (const char* ti = std::getenv("BSL_SOLO_TI")) f5.carga_bsl(ti);
        const std::string b1 = corre_bsl(h, n);
        char t[200];
        std::snprintf(t, sizeof t, "el chip en blanco con VBUS entra en el BSL (%u) y enumera como %04X:%04X (HID, EP%u IN y EP%u OUT)",
                      n, h.vid, h.pid, h.ep_in, h.ep_out);
        check(n == 1 && h.vid == 0x2047 && h.pid == 0x0200, t);
        check(b1 == "3B00 3B07 3B07 fast pc led",
              "la batería: la contraseña (3B 00), las órdenes que no tiene el de Flash (3B 07), RX Data Block Fast a RAM y "
              "Load PC (LED1 se enciende): «" + b1 + "»");
        f5.usb.vbus(false);
        if (const char* ti = std::getenv("BSL_TI")) {
            const std::string e = f5.carga_bsl(ti);
            HostHid h2(*this);
            unsigned n2 = 0;
            const std::string b2 = e.empty() ? corre_bsl(h2, n2) : e;
            check(b2 == b1 && h2.pid == h.pid, "B2 (la imagen de TI, " + f5.bsl_origen + ") da lo mismo: «" + b2 + "»");
            f5.usb.vbus(false);
            f5.pon_bsl_b1();
        }
        // Con RAM_BSL=<RAM_BSL.00.06.05.34.txt> (el que carga python-msp430-tools
        // con bsl5.hid; de TI, no se versiona): lo que hace la herramienta. El BSL
        // de RAM se carga con RX Data Block Fast, Load PC 0x2504, vuelve a
        // enumerar, da su versión y programa, verifica y arranca blink
        if (const char* rb = std::getenv("RAM_BSL")) {
            std::vector<TrozoFw> t;
            std::string e = lee_titxt(rb, t);
            HostHid h3(*this);
            unsigned n3 = 0;
            bool ok = e.empty() && entra_hid(h3, n3);
            if (std::getenv("BSL_VER")) std::printf("e='%s' entra %d n3 %u trozos %zu\n", e.c_str(), ok, n3, t.size());
            std::vector<uint8_t> pw{0x11};
            for (int i = 0; i < 32; ++i) pw.push_back(0xFF);
            std::vector<uint8_t> v;
            ok = ok && h3.orden(pw, &v);
            for (const TrozoFw& tr : t)
                for (size_t o = 0; ok && o < tr.datos.size(); o += 48) {
                    const uint32_t ad = tr.dir + uint32_t(o);
                    std::vector<uint8_t> c{0x1B, uint8_t(ad), uint8_t(ad >> 8), uint8_t(ad >> 16)};
                    c.insert(c.end(), tr.datos.begin() + long(o), tr.datos.begin() + long(std::min(tr.datos.size(), o + 48)));
                    ok = h3.orden(c, nullptr);
                }
            const bool ok1 = ok;
            ok = ok && h3.orden({0x17, 0x04, 0x25, 0x00}, nullptr);
            for (int i = 0; i < 3000; ++i) { h3.hc.sof(); sc_core::wait(1, SC_MS); }   // la herramienta espera 3 s
            // Si el BSL de RAM sigue conectado, el anfitrión no vuelve a enumerar
            // (la dirección y la configuración siguen); si se desconectó, sí
            HostHid& h4 = h3;
            bool sigue = ok && h4.orden({0x19}, &v);
            if (!sigue && ok) ok = h4.enumera() && h4.orden({0x19}, &v);
            const bool ok2 = ok;
            const std::vector<uint8_t> ver = v;
            // blink (P1.0): RX Data Block (con respuesta), TX Data Block y Load PC
            std::vector<TrozoFw> b;
            lee_elf("verif/f5529/elf/f5_blink.elf", b, ELF_MSP430);
            uint32_t entrada = 0;
            bool prog = !b.empty(), verif = true;
            for (const TrozoFw& tr : b) {
                if (tr.dir < 0x4400) continue;
                for (size_t o = 0; prog && o < tr.datos.size(); o += 48) {
                    const uint32_t ad = tr.dir + uint32_t(o);
                    const size_t n = std::min<size_t>(48, tr.datos.size() - o);
                    std::vector<uint8_t> c{0x10, uint8_t(ad), uint8_t(ad >> 8), uint8_t(ad >> 16)};
                    c.insert(c.end(), tr.datos.begin() + long(o), tr.datos.begin() + long(o + n));
                    prog = h4.orden(c, &v) && v == std::vector<uint8_t>({0x3B, 0x00});
                    std::vector<uint8_t> l;
                    verif = verif && h4.orden({0x18, uint8_t(ad), uint8_t(ad >> 8), uint8_t(ad >> 16), uint8_t(n), 0}, &l) &&
                            l.size() == n + 1 && std::equal(l.begin() + 1, l.end(), tr.datos.begin() + long(o));
                }
                if (tr.dir <= 0xFFFE && tr.dir + tr.datos.size() >= 0x10000) entrada = tr.datos[0xFFFE - tr.dir] | (tr.datos[0xFFFF - tr.dir] << 8);
            }
            h4.orden({0x17, uint8_t(entrada), uint8_t(entrada >> 8), 0}, nullptr);
            unsigned cambios = 0;
            bool ant = f5.p(1, 0).voltage() > 1.6f;
            for (int i = 0; i < 600; ++i) { sc_core::wait(1, SC_MS); const bool x = f5.p(1, 0).voltage() > 1.6f; if (x != ant) ++cambios; ant = x; }
            char t2[240];
            std::snprintf(t2, sizeof t2, "el BSL de RAM de python-msp430-tools: carga, vuelve a enumerar, versión %02X.%02X.%02X.%02X; "
                          "blink programado %d, verificado %d y arrancado (P1.0 cambia %u veces)",
                          ver.size() > 4 ? ver[1] : 0, ver.size() > 4 ? ver[2] : 0, ver.size() > 4 ? ver[3] : 0, ver.size() > 4 ? ver[4] : 0,
                          prog, verif, cambios);
            if (std::getenv("BSL_VER")) std::printf("ok1 %d ok2 %d ok %d pc %05X\n", ok1, ok2, ok, unsigned(f5.nucleo.cpu.pc()));
            check(ok && ver == std::vector<uint8_t>({0x3A, 0x00, 0x06, 0x05, 0x34}) && prog && verif && cambios >= 4, t2);
            f5.usb.vbus(false);
        }
        // Con un programa, el vector no está en blanco: arranca el programa aunque haya VBUS
        f5.reset(true);
        wait(1, SC_US);
        f5.carga("verif/fw/f5529/SimpleUsbBackchannel.txt");
        const unsigned n1 = f5.bsl_entradas;
        f5.usb.vbus(true);
        f5.reset(false);
        wait(20, SC_MS);
        check(f5.bsl_entradas == n1, "con un programa (el vector puesto) y VBUS, arranca el programa");
        f5.usb.vbus(false);
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
        if (va('A')) f10();
        if (va('B')) f11();
        if (va('C')) f12();
        if (va('D')) f13();
        if (va('E')) f14();
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
