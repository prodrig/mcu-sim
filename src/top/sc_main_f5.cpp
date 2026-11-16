// =============================================================================
// sc_main_f5.cpp — LA SUITE DEL MSP430F5529 (fase M4 de doc/analisis_msp430.md)
//
//   F1  el SoC contra su descriptor generado (vectores, mapa, JTAG ID, EEM) y
//       la TLV de fábrica (el ID 0x5529, la CRC, las calibraciones del ADC)
//   F2  f5_basico.c, con la DriverLib de TI: VCORE 3 y el FLL a 25 MHz, la
//       UCA1 a 115200 (el backchannel, leído en P4.4), la Flash de 5xx, el
//       sensor del ADC12_A con el REF, el DMA, el MPY32, el CRC16, una
//       función por encima de 0x10000 y Timer0_A5
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/f5529/soc_f5529.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR = "verif/f5529/elf/";

SC_MODULE(TbF5) {
    SocF5529 f5{"f5529"};
    SC_CTOR(TbF5) { SC_THREAD(run); }

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
                       {"TIMER0_B1", 0xFFF4}, {"TIMER0_B0", 0xFFF6}, {"UNMI", 0xFFFA}, {"SYSNMI", 0xFFFC}, {"RESET", 0xFFFE}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 21 vectores que usa el SoC, como en msp430f5529.h");
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

    void run() {
        const char* solo = std::getenv("TESTF5_SOLO");
        auto va = [&](char g) { return !solo || std::strchr(solo, g); };
        if (va('1')) f1();
        if (va('2')) f2();
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
