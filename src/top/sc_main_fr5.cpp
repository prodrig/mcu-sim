// =============================================================================
// sc_main_fr5.cpp — LA SUITE DE LAS FR5xx/FR6xx (fase M8 de
// doc/analisis_msp430.md)
//
//   L1  el FR5969 contra su descriptor generado (vectores por nombre, la FRAM
//       hasta 0x13FFF, MPY32) y la TLV de fábrica (0x8169, la calibración del
//       ADC12_B y de REF)
//   L2  fr59_basico.c (-mlarge): el DCO a 8 MHz y la UCA0 en P2.0; el LFXT;
//       el ADC12_B con el sensor y AVCC/2; el Comp_E contra 1,2 V; AES-128;
//       el DMA por DMAREQ; el RTC_B; TB0.3; la IPE (fuera, 0x3FFF; dentro, el
//       dato; el depurador tampoco la lee); la MPU con su NMI y con su PUC; y
//       LPM3.5 con el RTC_B
//   L3  la demo de fábrica de TI (su binario TI-TXT): el parpadeo de LED1
//       (P4.6) y LED2 (P1.0); el ping ('5'); la temperatura en vivo ('1': la
//       calibración de la TLV y ADC12MEM0 cada 1/8 s); el registro en FRAM
//       ('2': LPM3.5 con el RTC_B y el LFXT) y S2 (P1.1), que lo despierta
//   L4  el FR5994: fr5994_basico.c (la RAM del LEA, la FRAM hasta 0x43FFF,
//       CRC32 y CRC16 de «123456789», el RTC_C con RTCKEY) y la microSD de la
//       LaunchPad (la pieza SdSpi) por la UCB0: el arranque SPI, un bloque
//       escrito y leído
//   L5  el FR6989: la demo de fábrica de TI (su binario) con el vidrio
//       FH-1138P en el LCD_C, cableado como en la MSP-EXP430FR6989: el texto
//       que se desplaza y, con S1 y S2 a la vez, el cronómetro
//   L6  el Replicator430FR de TI en un F5529 (M4) programa un FR5969 por
//       Spy-Bi-Wire: apaga la MPU, borra por el buzón (0xA55A, 0x1A1A),
//       carga su programa y lo suelta; y con la contraseña de JTAG en
//       0xFF80 (0xAAAA, dos palabras: 0x1111 0x2222), que da por el buzón
//       antes (0xA55A, 0x1E1E y la contraseña)
//   L7  el FR5994 (fr5994_reloj.c): el HFXT con un cristal de 16 MHz (su
//       arranque, HFFREQ fuera de margen, apagado en LPM3) y en bypass con
//       un ExtClock de 12 MHz; el RAMCTL (la clave, RCRS2OFF = 1 y 2)
//   L8  las funciones de los pines (por los registros, con un programa que
//       duerme en LPM0): PxSEL sin función a DVSS, los bits del SR, los
//       relojes, TB0OUTH, la captura B, DMAE0, RTCCLK, COUT, CEPDx, el LFXT
//       y su bypass; en el FR6989, TA0.1 y MCLK con 11 y A4 en P8.7; en el
//       FR5994, MCLK en P5.7 y P8.0-P8.3
//   L9  las erratas que se modelan: REF9, COMP10, ADC41, ADC71, ADC42, AES1
//       y WDG5 en el FR5969; TA22 y RTC12 en el FR6989
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/msp430/fr5969/soc_fr5969.h"
#include "soc/msp430/fr5994/soc_fr5994.h"
#include "parts/sd_spi.h"
#include "soc/msp430/fr6989/soc_fr6989.h"
#include "parts/lcd_fh1138p.h"
#include "soc/msp430/f5529/soc_f5529.h"
#include "parts/ext_parts.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str()); }
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok; std::printf("  [OK  ] %s\n", q.c_str()); } else { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static const char* const DIR5969 = "verif/fr5969/elf/";

SC_MODULE(TbFr5) {
    SocFR5969 fr{"fr5969"};
    SocFR5994 fr94{"fr5994"};
    SdSpi sd{"sd", fr94.p(4, 0), fr94.p(2, 2), fr94.p(1, 6), fr94.p(1, 7), 1024};
    // L7: un reloj de 12 MHz en HFXIN (PJ.6), sin onda: lo declara en el nodo
    ExtClock hfxin{"hfxin", fr94.p(11, 6), 0.0, 3.3, 50.0, false};
    int d_cd_ = -1;
    SocFR6989 fr69{"fr6989"};
    // El vidrio, como en la LaunchPad [SLAU627A tabla 4]: la patilla k+1
    std::vector<analog_net_if*> vidrio() {
        const struct { unsigned p, b; } P[38] = {
            {8, 3}, {8, 2}, {8, 1}, {8, 0}, {5, 6}, {5, 5}, {5, 4}, {7, 4}, {4, 6}, {4, 5}, {4, 4}, {5, 7}, {5, 2}, {5, 1},
            {5, 0}, {10, 2}, {10, 1}, {7, 6}, {7, 5}, {6, 7}, {6, 6}, {6, 5}, {6, 4}, {6, 3}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
            {0, 0}, {0, 0}, {0, 0}, {10, 0}, {7, 7}, {5, 3}, {7, 3}, {7, 2}, {7, 1}, {7, 0}};
        std::vector<analog_net_if*> v(38, nullptr);
        for (unsigned k = 0; k < 38; ++k) if (P[k].p) v[k] = &fr69.p(P[k].p, P[k].b);
        return v;
    }
    LcdFh1138p lcd1{"lcd1", vidrio()};
    int d69s1_ = -1, d69s2_ = -1;
    // El Replicator430FR en un F5529 y su FR5969, por SBW (P6.2 TEST, P6.5 RST)
    SocF5529 rf{"rep_fr"};
    Cableado c_rf = [this] { Cableado c; c.une_alim("TEST", rf.p(6, 2)); c.une_alim("RST", rf.p(6, 5)); return c; }();
    SocFR5969 tf{"fr5969_obj", &c_rf};
    int d_p12_ = -1, d_s2_ = -1;
    unsigned subidas_[2] = {0, 0};
    bool led_[2] = {false, false};
    void leds_proc() {
        const bool a[2] = {alto(1, 0), alto(4, 6)};
        for (unsigned k = 0; k < 2; ++k) { if (a[k] && !led_[k]) ++subidas_[k]; led_[k] = a[k]; }
    }

    SC_CTOR(TbFr5) {
        SC_THREAD(run);
        SC_METHOD(pwm_proc); sensitive << fr.p(3, 4).value_changed_event(); dont_initialize();
        const int d = fr.rst.register_driver("r"); fr.rst.set_drive(d, 3.3f, 47000.0f);
        d_p12_ = fr.p(1, 2).register_driver("banco"); fr.p(1, 2).set_hiz(d_p12_);
        d_s2_ = fr.p(1, 1).register_driver("S2"); fr.p(1, 1).set_hiz(d_s2_);
        const int d94 = fr94.rst.register_driver("r"); fr94.rst.set_drive(d94, 3.3f, 47000.0f);
        const int d69 = fr69.rst.register_driver("r"); fr69.rst.set_drive(d69, 3.3f, 47000.0f);
        d69s1_ = fr69.p(1, 1).register_driver("S1"); fr69.p(1, 1).set_hiz(d69s1_);
        d69s2_ = fr69.p(1, 2).register_driver("S2"); fr69.p(1, 2).set_hiz(d69s2_);
        { const int d = tf.rst.register_driver("r"); tf.rst.set_drive(d, 3.3f, 47000.0f); }
        rf.monitor_uart = true;
        d_cd_ = fr94.p(7, 2).register_driver("cd"); fr94.p(7, 2).set_drive(d_cd_, 0.0f, 10.0f);   // la tarjeta, dentro
        SC_METHOD(leds_proc); sensitive << fr.p(1, 0).value_changed_event() << fr.p(4, 6).value_changed_event(); dont_initialize();
    }
    // El tiempo en alto de P3.4
    sc_time alto_{SC_ZERO_TIME}, sube_{SC_ZERO_TIME};
    bool a_ = false;
    void pwm_proc() {
        const bool a = alto(3, 4);
        if (a && !a_) sube_ = sc_time_stamp();
        if (!a && a_) alto_ += sc_time_stamp() - sube_;
        a_ = a;
    }
    double ciclo(const sc_time& dur) {
        alto_ = SC_ZERO_TIME; sube_ = sc_time_stamp();
        const sc_time t0 = sc_time_stamp();
        wait(dur);
        sc_time h = alto_;
        if (a_) h += sc_time_stamp() - sube_;
        return h.to_seconds() / (sc_time_stamp() - t0).to_seconds();
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
    uint16_t lee_dbg(uint32_t a) {
        tlm::tlm_generic_payload gp;
        uint8_t d[2] = {0, 0};
        gp.set_command(tlm::TLM_READ_COMMAND); gp.set_address(a); gp.set_data_ptr(d); gp.set_data_length(2);
        fr.acceso(gp, true);
        return uint16_t(d[0] | (d[1] << 8));
    }

    void l1() {
        grupo("L1 el FR5969 contra el descriptor generado y la TLV, sin tiempo");
        const m430::Desc430& d = SocFR5969::DESC;
        check(d.jtag_id == 0x99 && d.ram.ini == 0x1C00 && d.ram.fin == 0x23FF && d.principal.ini == 0x4400 &&
              d.principal2.ini == 0x10000 && d.principal2.fin == 0x13FFF && d.mpy == m430::Mpy::Mpy32,
              "el descriptor: JTAG ID 0x99, RAM 0x1C00-0x23FF, FRAM 0x4400-0xFF7F y 0x10000-0x13FFF, MPY32");
        struct V { const char* n; uint16_t usa; };
        const V v[] = {{"AES256", 0xFFCC}, {"RTC", 0xFFCE}, {"PORT4", 0xFFD0}, {"PORT3", 0xFFD2}, {"TIMER3_A1", 0xFFD4},
                       {"TIMER3_A0", 0xFFD6}, {"PORT2", 0xFFD8}, {"TIMER2_A1", 0xFFDA}, {"TIMER2_A0", 0xFFDC},
                       {"PORT1", 0xFFDE}, {"TIMER1_A1", 0xFFE0}, {"TIMER1_A0", 0xFFE2}, {"DMA", 0xFFE4},
                       {"USCI_A1", 0xFFE6}, {"TIMER0_A1", 0xFFE8}, {"TIMER0_A0", 0xFFEA}, {"ADC12", 0xFFEC},
                       {"USCI_B0", 0xFFEE}, {"USCI_A0", 0xFFF0}, {"WDT", 0xFFF2}, {"TIMER0_B1", 0xFFF4},
                       {"TIMER0_B0", 0xFFF6}, {"COMP_E", 0xFFF8}};
        unsigned mal = 0;
        for (const V& x : v) { const m430::Vector* p = d.vector(x.n); if (!p || p->dir != x.usa) ++mal; }
        check(!mal, "los 23 vectores que usa el SoC, por su nombre en msp430fr5969.h [DS SLAS704G tabla 6-4]");
        uint16_t c = 0xFFFF;
        for (unsigned a = 0x1A04; a <= 0x1AFF; ++a) {
            const uint8_t b = fr.mem8(a);
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        check(fr.mem16(0x1A04) == 0x8169 && d.id_dispositivo == 0x8169 && fr.mem16(0x1A02) == c && fr.mem8(0x1A14) == 0x11 &&
              fr.mem8(0x1A26) == 0x12 && fr.mem8(0x1A2E) == 0x15 && fr.mem8(0x1A40) == 0x1C,
              "la TLV: el ID 0x8169, su CRC-CCITT, y las etiquetas del ADC12 (0x11), de REF (0x12), del aleatorio (0x15) y del BSL (0x1C) [tabla 6-64]");
    }

    void l2() {
        grupo("L2 fr59_basico.c: DCO, LFXT, ADC12_B, Comp_E, AES, DMA, RTC_B, TB0, IPE, MPU y LPM3.5");
        corre(std::string(DIR5969) + "fr59_basico.elf");
        {
            const bool ok = espera_uart("lfxt ok", 200);
            check(ok, "arranca: «" + linea(fr.uart_tx, "hola") + "»; el LFXT de PJ.4/PJ.5 sin falta");
        }
        check(linea(fr.uart_tx, "hola").find("id 8169 rst 02") != std::string::npos, "el ID de la TLV y SYSRSTIV = 0x02 (el BOR del encendido)");
        espera_uart("adc", 100);
        {
            const std::string l = linea(fr.uart_tx, "adc");
            const long t = num(l, "temp "), b = num(l, "bat ");
            check(t >= 240 && t <= 260 && b >= 3370 && b <= 3385,
                  "«" + l + "»: el sensor (A30, TCMAP) con 2,0 V y la calibración de la TLV da 25 °C; AVCC/2 (A31, BATMAP) = 1,65 V");
        }
        check(espera_uart("ce 0 espera", 50), "el Comp_E: C2 (P1.2, en alta impedancia) por debajo de 1,2 V: CEOUT = 0");
        fr.p(1, 2).set_drive(d_p12_, 2.0f, 10.0f);
        check(espera_uart("ce sube", 50), "con P1.2 a 2,0 V, CEOUT sube");
        fr.p(1, 2).set_hiz(d_p12_);
        espera_uart("aes", 50);
        check(linea(fr.uart_tx, "aes") == "aes 69C4E0D86A7B0430D8CDB78070B4C55A", "AES-128 de FIPS-197 C.1: «" + linea(fr.uart_tx, "aes") + "»");
        espera_uart("dma", 50);
        check(linea(fr.uart_tx, "dma") == "dma suma 54576 ifg 1", "el DMA: 16 palabras por DMAREQ en bloque, y DMA0IFG: «" + linea(fr.uart_tx, "dma") + "»");
        espera_uart("rtc ", 3000);
        check(linea(fr.uart_tx, "rtc ") == "rtc 13:00:00", "el RTC_B en BCD con el LFXT: de 12:59:58 a «" + linea(fr.uart_tx, "rtc ") + "»");
        espera_uart("tb0", 50);
        wait(1, SC_MS);
        const double cy = ciclo(sc_time(5, SC_MS));
        check(std::fabs(cy - 0.25) < 0.02, "TB0.3 en P3.4 al 25 %: " + std::to_string(cy));
        check(espera_uart("ipe", 100) && linea(fr.uart_tx, "ipe") == "ipe fuera 3FFF dentro BEEF nmi 1 sniv 08",
              "la IPE: leída desde fuera da 0x3FFF (JMP $) y la NMI con SYSSNIV = 0x08; desde dentro, el dato: «" + linea(fr.uart_tx, "ipe") + "»");
        check(lee_dbg(0x10000) == 0x3FFF && lee_dbg(0x10002) == 0x3FFF && fr.mem16(0x10000) == 0xCAFE,
              "el depurador tampoco la lee (0x3FFF), aunque la FRAM tiene el dato [UG FR5 tabla 9-2]");
        check(espera_uart("mpu nmi", 50) && linea(fr.uart_tx, "mpu nmi") == "mpu nmi 1 sniv 0E antes FFFF ahora FFFF",
              "la MPU: escribir en el segmento 2 de solo lectura no escribe y da la NMI (SYSSNIV = 0x0E): «" + linea(fr.uart_tx, "mpu nmi") + "»");
        check(espera_uart("mpu puc", 100) && linea(fr.uart_tx, "mpu puc") == "mpu puc rstiv 2C" && fr.uart_tx.find("mpu sin puc") == std::string::npos,
              "con MPUSEG2VS, la violación es un PUC: SYSRSTIV = 0x2C [DS tabla 6-11]");
        check(espera_uart("lpm35 despierta", 3000) && fr.mem16(0x9000) == 0xFFFF,
              "LPM3.5 con el RTC_B (RTCRDYIE) y el LFXT: «" + linea(fr.uart_tx, "lpm35 despierta") + "»");
        check(linea(fr.uart_tx, "lpm35 despierta").find("rstiv 08 rtc 1") != std::string::npos,
              "despierta con SYSRSTIV = 0x08 y RTCRDYIFG puesta");
        check(fr.violaciones_mpu() == 3, "tres violaciones (la IPE, la NMI y el PUC): " + std::to_string(fr.violaciones_mpu()));
    }

    void l3() {
        grupo("L3 la demo de fábrica de TI con la red de la MSP-EXP430FR5969");
        subidas_[0] = subidas_[1] = 0;
        corre("verif/fw/fr5969/OutOfBox_MSP430FR5969.txt");
        wait(2500, SC_MS);
        check(subidas_[0] == 6 && subidas_[1] == 5, "al arrancar, LED2 (P1.0) se enciende y los dos cambian diez veces: " +
              std::to_string(subidas_[0]) + " y " + std::to_string(subidas_[1]) + " subidas");
        fr.uart_tx.clear();
        fr.uart_rx('5');
        wait(20, SC_MS);
        check(fr.uart_tx == std::string("\xFF\xFF"), "el ping ('5' a 9600): responde 0xFF 0xFF (" + std::to_string(fr.uart_tx.size()) + " bytes)");
        fr.uart_tx.clear();
        fr.uart_rx('1');
        wait(1200, SC_MS);
        {
            const std::string& u = fr.uart_tx;
            auto b = [&](size_t i) { return unsigned(uint8_t(u[i])); };
            const bool cal = u.size() >= 4 && (b(0) << 8 | b(1)) == fr.mem16(0x1A1E) && (b(2) << 8 | b(3)) == fr.mem16(0x1A20);
            check(cal, "'1': manda primero la calibración del sensor con 2,0 V de la TLV (0x1A1E y 0x1A20, el byte alto primero)");
            const size_t n = u.size() >= 4 ? (u.size() - 4) / 2 : 0;
            const unsigned t = n ? (b(4) << 8 | b(5)) : 0;
            check(n >= 6 && n <= 10 && t >= 1550 && t <= 1572, "y luego ADC12MEM0 cada 1/8 s (TA0 con ACLK, el LFXT): " + std::to_string(n) +
                  " muestras en 1,2 s; la primera, " + std::to_string(t) + " (25 °C con 2,0 V)");
        }
        // El modo en vivo acaba al cambiar `mode`, y el bucle principal se
        // duerme en LPM3 hasta la orden siguiente: la segunda '2' lo arranca
        fr.uart_rx('2');
        wait(300, SC_MS);
        fr.uart_rx('2');
        // y storeTimeStamp() espera la hora del PC: seis bytes
        wait(20, SC_MS);
        for (uint8_t b : {0x18, 0x11, 0x26, 0x08, 0x30, 0x00}) fr.uart_rx(b);
        bool dentro = false;
        for (int i = 0; i < 400 && !dentro; ++i) {
            wait(10, SC_MS); dentro = fr.lpm35();
        }
        check(dentro, "'2' (otra vez, ya fuera del modo en vivo) y la hora del PC (6 bytes): tres destellos de LED1 y LPM3.5 (el RTC_B con el LFXT sigue)");
        fr.p(1, 1).set_drive(d_s2_, 0.0f, 10.0f);
        wait(50, SC_MS);
        fr.p(1, 1).set_hiz(d_s2_);
        wait(100, SC_MS);
        check(!fr.lpm35() && alto(4, 6), "S2 (P1.1, con su pull-up interno) lo despierta: SYSRSTIV = LPM5WU y P1IFG.1, y LED1 se enciende");
    }

    void l4() {
        grupo("L4 el FR5994: el LEA, la FRAM alta, CRC32, RTC_C y la microSD por SPI");
        fr94.reset(true);
        wait(1, SC_US);
        const std::string e = fr94.carga(std::string("verif/fr5994/elf/") + "fr5994_basico.elf");
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        fr94.uart_tx.clear();
        fr94.reset(false);
        auto lin = [&](const char* c) { return linea(fr94.uart_tx, c); };
        auto espera = [&](const char* c, int ms) {
            auto entera = [&] { const size_t i = fr94.uart_tx.find(c); return i != std::string::npos && fr94.uart_tx.find('\n', i) != std::string::npos; };
            for (int i = 0; i < ms * 10 && !entera(); ++i) wait(100, SC_US);
            return entera();
        };
        {
            const bool ok = espera("hola", 100);
            check(ok && lin("hola") == "hola FR5994 id 82A1", "arranca: «" + lin("hola") + "» (la TLV del FR5994)");
        }
        espera("mem", 50);
        check(lin("mem") == "mem lea 5994 fram F00D", "la RAM del LEA (0x3000) y la FRAM de 0x43F00: «" + lin("mem") + "»");
        espera("crc32", 50);
        check(lin("crc32") == "crc32 CBF43926 crc16 29B1", "CRC32 de Ethernet y CRC-CCITT de «123456789» con el mapa de las FR5994: «" + lin("crc32") + "»");
        {
            const bool ok = espera("rtc", 3000);
            check(ok && lin("rtc") == "rtc 00:00:01", "el RTC_C con RTCKEY y el LFXT, en BCD: de 23:59:59 a «" + lin("rtc") + "»");
        }
        espera("sd escribe", 500);
        check(lin("sd detecta") == "sd detecta 1", "la detección de la tarjeta (P7.2 a masa, con el pull-up interno)");
        check(lin("sd r0") == "sd r0 01 r8 01 eco AA acmd41 2 ocr C0",
              "el arranque SPI: CMD0 e inactiva, CMD8 con su eco, ACMD41 a la segunda, CMD58 con CCS: «" + lin("sd r0") + "»");
        check(lin("sd escribe") == "sd escribe 00 resp 05 lee 00 mal 0" && sd.bloques_escritos() == 1 && sd.byte(5 * 512 + 1) == 10,
              "el bloque 5, escrito (0x05, aceptado) y leído igual: «" + lin("sd escribe") + "»");
    }

    void l5() {
        grupo("L5 el FR6989: la demo de fábrica con el LCD_C y el vidrio FH-1138P");
        fr69.reset(true);
        wait(1, SC_US);
        const std::string e = fr69.carga("verif/fw/fr6989/OutOfBox_MSP430FR6989.txt");
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        fr69.reset(false);
        std::string visto, ultimo;
        for (int i = 0; i < 600; ++i) {
            wait(20, SC_MS);
            const std::string t = lcd1.texto();
            if (t != ultimo) { visto += t + "|"; ultimo = t; }
        }
        check(visto.find("WELCOM") != std::string::npos && visto.find("FR6989") != std::string::npos,
              "el texto se desplaza en el vidrio: «WELCOME TO THE FR6989 LAUNCHPAD» (" + std::to_string(lcd1.tramas()) + " tramas)");
        fr69.p(1, 1).set_drive(d69s1_, 0.0f, 10.0f);
        fr69.p(1, 2).set_drive(d69s2_, 0.0f, 10.0f);
        wait(2000, SC_MS);                          // 40 tics de TA0 con los dos abajo
        fr69.p(1, 1).set_hiz(d69s1_);
        fr69.p(1, 2).set_hiz(d69s2_);
        visto.clear();
        for (int i = 0; i < 250; ++i) {
            wait(20, SC_MS);
            const std::string t = lcd1.texto();
            if (t != ultimo) { visto += t + "|"; ultimo = t; }
        }
        check(visto.find("PWATCH") != std::string::npos && visto.find("MODE") != std::string::npos && ultimo == "000000",
              "S1 y S2 a la vez, 2 s: «STOPWATCH MODE» se desplaza y queda «000000» (la S se ve como un 5)");
        fr69.p(1, 1).set_drive(d69s1_, 0.0f, 10.0f);
        wait(100, SC_MS);
        fr69.p(1, 1).set_hiz(d69s1_);
        wait(1500, SC_MS);
        const std::string t = lcd1.texto();
        // mmsscc: de la pulsación (100 ms) a la lectura (1,5 s), 1,60 s; la
        // pantalla se refresca cada ms (RT0PS /32) [StopWatchMode.c]
        const int cs = t.size() == 6 && t.find_first_not_of("0123456789") == std::string::npos
                           ? std::stoi(t.substr(0, 2)) * 6000 + std::stoi(t.substr(2, 2)) * 100 + std::stoi(t.substr(4, 2)) : -1;
        check(cs >= 155 && cs <= 161,
              "S1 arranca el cronómetro (el RTC_C, RTCPS de 16 bits y los preescaladores a cero al escribir la hora): «" + t +
                  "» al cabo de 1,6 s");
    }

    void rep(const char* elf, bool clave) {
        tf.reset(true);
        tf.carga(std::string(DIR5969) + "fr59_basico.elf");
        if (clave) {
            // La contraseña: la firma 0xAAAA, dos palabras, en 0xFF88
            const uint16_t w[] = {0xAAAA, 0x0002, 0xFFFF, 0xFFFF, 0x1111, 0x2222};
            for (unsigned i = 0; i < 6; ++i) { tf.poke(0xFF80 + 2 * i, uint8_t(w[i])); tf.poke(0xFF81 + 2 * i, uint8_t(w[i] >> 8)); }
        }
        tf.reset(false);
        wait(5, SC_MS);
        if (clave) check(tf.jtag_bloqueado(), "la contraseña en 0xFF80: JTAG bloqueado tras el arranque (solo BYPASS y el buzón)");
        rf.reset(true);
        wait(1, SC_US);
        rf.carga(std::string("verif/f5529/elf/") + elf);
        rf.uart_tx.clear();
        rf.reset(false);
        for (int i = 0; i < 60000 && rf.uart_tx.find("REP OK") == std::string::npos &&
                        rf.uart_tx.find("REP ERROR") == std::string::npos; ++i) wait(1, SC_MS);
        wait(10, SC_MS);
        std::string s = rf.uart_tx;
        for (char& c : s) if (c == '\n') c = '|';
        check(rf.uart_tx.find("REP OK") != std::string::npos,
              std::string("el Replicator430FR (") + elf + "): «" + s + "»");
        if (clave) check(tf.desbloqueos_jtag() == 1, "la contraseña entra por el buzón y abre JTAG (una vez)");
        unsigned llenas = 0;
        for (uint32_t a = 0x4400; a < 0x14000; a += 2) if ((a < 0xC200 || a >= 0xC260) && a != 0xFFFE && tf.mem16(a) != 0xFFFF) ++llenas;
        check(tf.borrados_buzon() >= 1 && !llenas && tf.mem16(0xC200) == 0x4031 && tf.mem16(0xFFFE) == 0xC200,
              "borrada por el buzón (0x1A1A) y con el programa de TI en 0xC200 (" + std::to_string(llenas) + " palabras de más)");
        unsigned flancos = 0;
        bool ant = tf.p(1, 0).voltage() > 1.6f;
        const sc_time fin = sc_time_stamp() + sc_time(1000, SC_MS);
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), tf.p(1, 0).value_changed_event());
            const bool v = tf.p(1, 0).voltage() > 1.6f;
            if (v != ant) { ++flancos; ant = v; }
        }
        check(flancos >= 4 && !tf.jtag_bloqueado(), "el FR5969, suelto, ejecuta el programa: P1.0 cambia (" + std::to_string(flancos) + " flancos en 1 s)");
    }
    void l6() {
        grupo("L6 el Replicator430FR de TI en un F5529 programa un FR5969 por Spy-Bi-Wire");
        rep("repfr_sbw.elf", false);
        rep("repfr_clave.elf", true);
    }

    void l7() {
        grupo("L7 el FR5994: el HFXT (cristal, HFFREQ, LPM3 y bypass) y el RAMCTL");
        fr94.reset(true);
        wait(1, SC_US);
        fr94.cristal_hfxt = true; fr94.f_hfxt = 16e6;               // un cristal de 16 MHz en PJ.6/PJ.7
        const std::string e = fr94.carga("verif/fr5994/elf/fr5994_reloj.elf");
        if (!e.empty()) std::printf("  %s\n", e.c_str());
        fr94.uart_tx.clear();
        fr94.reset(false);
        auto lin = [&](const char* c) { return linea(fr94.uart_tx, c); };
        auto espera = [&](const char* c, int ms) {
            auto entera = [&] { const size_t i = fr94.uart_tx.find(c); return i != std::string::npos && fr94.uart_tx.find('\n', i) != std::string::npos; };
            for (int i = 0; i < ms * 10 && !entera(); ++i) wait(100, SC_US);
            return entera();
        };
        espera("hfxt", 200);
        {
            const std::string l = lin("hfxt");
            const long t = num(l, "tics "), k = num(l, "smclk ");
            // tSTART,HFXT a 16 MHz: 0,872 ms de subida [de modelo, entre los dos puntos de la tabla 5-5] y
            // 1024 ciclos (64 µs): 0,936 ms, 30,7 tics de 32 768 Hz
            check(l.find("falta 1") != std::string::npos && t >= 29 && t <= 32 && l.find("ok 1") != std::string::npos &&
                      k >= 7900 && k <= 8100,
                  "el cristal de 16 MHz con HFFREQ = 2: HFXTOFFG hasta que arranca (tSTART,HFXT, " + std::to_string(t) +
                      " tics de ACLK) y SMCLK = HFXT/2: «" + l + "»");
        }
        espera("hffreq", 200);
        {
            const std::string l = lin("hffreq");
            const long t = num(l, "tics "), k = num(l, "smclk ");
            check(l.find("falta 1") != std::string::npos && t > 200 && k >= 2400 && k <= 2600,
                  "HFFREQ = 3 con el cristal de 16 MHz: no oscila, la falta sigue y SMCLK cae a MODCLK/2: «" + l + "»");
        }
        espera("lpm", 200);
        {
            const std::string l = lin("lpm ");
            const long t = num(l, "tics ");
            check(l.find("antes 0 despues 1") != std::string::npos && t >= 29 && t <= 32 && l.find("desp 1") != std::string::npos,
                  "HFXTOFF = 1 y solo SMCLK lo usa: LPM3 lo apaga y al despertar arranca otra vez: «" + l + "»");
        }
        espera("bypass listo", 200);
        fr94.cristal_hfxt = false;
        hfxin.set_freq(12e6);
        espera("bypass tics", 300);
        {
            const std::string l = lin("bypass tics");
            const long t = num(l, "tics "), k = num(l, "smclk ");
            check(t >= 0 && t <= 4 && l.find("ok 1") != std::string::npos && k >= 11900 && k <= 12100,
                  "HFXTBYPASS con 12 MHz en HFXIN (un ExtClock que lo declara en el nodo): el contador de 1024 ciclos y SMCLK = 12 MHz: «" +
                      l + "»");
        }
        hfxin.set_freq(0.0);
        espera("ram", 200);
        check(lin("ram") == "ram clave 6900 6900 6900 6910 off1 CDCD 5A5A 1234 off2 3FFF 3FFF 4321",
              "RCCTL0: 0x69 al leer, sin escribir de byte ni con otra clave; RCRS2OFF = 1, el sector 2 se pierde en LPM3 y el 0 no; "
              "= 2, sigue apagado (0x3FFF, sin escribir) hasta volver a 0: «" + lin("ram") + "»");
        check(fr94.ram_perdida() == 2 && !fr94.sector_apagado(2), "los dos apagados del sector 2, y vuelto a encender");
    }

    // --- L8: las funciones de los pines, por los registros (con el depurador) --
    static void esc(SocFR2& c, uint32_t a, uint16_t v, unsigned n) {
        tlm::tlm_generic_payload gp;
        uint8_t d[2] = {uint8_t(v), uint8_t(v >> 8)};
        gp.set_command(tlm::TLM_WRITE_COMMAND); gp.set_address(a); gp.set_data_ptr(d); gp.set_data_length(n);
        c.acceso(gp, true);
    }
    static uint16_t lee(SocFR2& c, uint32_t a) {
        tlm::tlm_generic_payload gp;
        uint8_t d[2] = {0, 0};
        gp.set_command(tlm::TLM_READ_COMMAND); gp.set_address(a); gp.set_data_ptr(d); gp.set_data_length(2);
        c.acceso(gp, true);
        return uint16_t(d[0] | (d[1] << 8));
    }
    // Un programa de cuatro instrucciones (verif/fr5969/dormilon.txt, TI-TXT):
    // para el perro, suelta LOCKLPM5 y se queda en LPM0
    void dormilon(SocFR2& c) {
        c.reset(true);
        wait(1, SC_US);
        const std::string e = c.carga("verif/fr5969/dormilon.txt");
        if (!e.empty()) std::printf("  %s\n", e.c_str());
    }
    // El estado de un pin: 'H', 'L' o 'Z'
    static char estado(analog_net_if& n) { return n.floating() ? 'Z' : n.voltage() > 1.6f ? 'H' : 'L'; }
    void l8() {
        grupo("L8 las funciones de los pines de las FR5xx/FR6xx (las tablas de las hojas de datos)");
        dormilon(fr); dormilon(fr69); dormilon(fr94);
        fr.reset(false); fr69.reset(false); fr94.reset(false);
        wait(1, SC_MS);
        // FR5969: PxSEL sin función, de salida, a DVSS
        esc(fr, 0x203, 0x80, 1); esc(fr, 0x205, 0x80, 1); esc(fr, 0x20B, 0x80, 1);      // P2.7: OUT, DIR, SEL0
        esc(fr, 0x223, 0x20, 1); esc(fr, 0x225, 0x20, 1); esc(fr, 0x22D, 0x20, 1);      // P4.5: OUT, DIR, SEL1
        wait(10, SC_US);
        check(estado(fr.p(2, 7)) == 'L' && estado(fr.p(4, 5)) == 'L',
              "un PxSEL sin función y de salida va a DVSS: P2.7 (01) y P4.5 (10) con PxOUT = 1, a 0 [DS FR5969 figuras 6-8 y 6-13]");
        // Los bits del SR en PJ.0-PJ.3 con PJSEL = 10: la CPU en LPM0
        esc(fr, 0x324, 0x0F, 1); esc(fr, 0x32C, 0x0F, 1);
        wait(10, SC_US);
        const std::string sr = {estado(fr.p(11, 0)), estado(fr.p(11, 1)), estado(fr.p(11, 2)), estado(fr.p(11, 3))};
        check(sr == "LLLH", "la depuración de bajo consumo en PJ.0-PJ.3 (SCG1, SCG0, OSCOFF, CPUOFF) en LPM0: «" + sr + "» [tabla 6-62]");
        // MCLK (PJ.1, 01), SMCLK (P3.4, 1X), ACLK (P2.0, 11, de salida)
        esc(fr, 0x32C, 0x00, 1); esc(fr, 0x32A, 0x02, 1);
        esc(fr, 0x224, 0x10, 1); esc(fr, 0x22C, 0x10, 1); esc(fr, 0x22A, 0x10, 1);
        esc(fr, 0x205, 0x81, 1); esc(fr, 0x20B, 0x81, 1); esc(fr, 0x20D, 0x01, 1);
        wait(10, SC_US);
        check(estado(fr.p(11, 1)) == 'L' && estado(fr.p(3, 4)) == 'H' && estado(fr.p(2, 0)) == 'H',
              "MCLK en PJ.1 (parado en LPM0), SMCLK en P3.4 (11) y ACLK en P2.0 (11, de salida) [tablas 6-52, 6-57, 6-62]");
        esc(fr, 0x205, 0x80, 1);                                    // P2.0 de entrada: TB0CLK
        wait(10, SC_US);
        check(estado(fr.p(2, 0)) == 'Z', "P2.0 con 11 y de entrada es TB0CLK: sin salida");
        // TB0OUTH en PJ.0 (01, de entrada): TB0.3 en P3.4 (01) a alta impedancia
        esc(fr, 0x22C, 0x00, 1);                                    // P3.4: 01 (TB0.3)
        esc(fr, 0x3C8, 0x0004, 2);                                  // TB0CCTL3: OUTMOD 0, OUT = 1
        esc(fr, 0x324, 0x0E, 1); esc(fr, 0x32A, 0x01, 1);           // PJ.0: 01, de entrada
        const int dpj = fr.p(11, 0).register_driver("outh");
        fr.p(11, 0).set_drive(dpj, 0.0f, 10.0f);
        wait(10, SC_US);
        const char antes = estado(fr.p(3, 4));
        fr.p(11, 0).set_drive(dpj, 3.3f, 10.0f);
        wait(10, SC_US);
        const char con = estado(fr.p(3, 4));
        fr.p(11, 0).set_hiz(dpj);
        check(antes == 'H' && con == 'Z', std::string("TB0OUTH en PJ.0 a 1 pone TB0.3 (P3.4) en alta impedancia: ") + antes + " → " + con +
                                              " [UG FR5 §26.2.5.2]");
        // La captura B: TB0.CCI3B en P1.6 (01, de entrada)
        esc(fr, 0x3C0, 0x0220, 2);                                  // TB0CTL: SMCLK, continuo
        esc(fr, 0x3C8, 0x5100, 2);                                  // TB0CCTL3: CM subida, CCIS = CCIB, CAP
        esc(fr, 0x20A, 0x40, 1);
        const int d16 = fr.p(1, 6).register_driver("cci3b");
        fr.p(1, 6).set_drive(d16, 0.0f, 10.0f); wait(20, SC_US);
        fr.p(1, 6).set_drive(d16, 3.3f, 10.0f); wait(20, SC_US);
        fr.p(1, 6).set_hiz(d16);
        check(lee(fr, 0x3C8) & 0x0001, "TB0.CCI3B en P1.6 (01): un flanco de subida, captura (CCIFG) [tabla 6-51]");
        // DMAE0 en P1.0 (10, de entrada): el disparo 31
        fr.poke(0x1C00, 0x34); fr.poke(0x1C01, 0x12);
        esc(fr, 0x512, 0x1C00, 2); esc(fr, 0x514, 0, 2); esc(fr, 0x516, 0x1C10, 2); esc(fr, 0x518, 0, 2);
        esc(fr, 0x51A, 1, 2); esc(fr, 0x500, 31, 2); esc(fr, 0x510, 0x0010, 2);
        esc(fr, 0x20C, 0x01, 1);
        const int d10 = fr.p(1, 0).register_driver("dmae0");
        fr.p(1, 0).set_drive(d10, 0.0f, 10.0f); wait(20, SC_US);
        fr.p(1, 0).set_drive(d10, 3.3f, 10.0f); wait(50, SC_US);
        fr.p(1, 0).set_hiz(d10);
        check(fr.mem16(0x1C10) == 0x1234, "DMAE0 en P1.0 (10): el flanco dispara el canal 0 con DMA0TSEL = 31 [tabla 6-12]");
        // RTCCLK en P1.0 (10, de salida): el RTC_B en marcha con RTCCALF = 1
        esc(fr, 0x204, 0x01, 1);
        esc(fr, 0x4A0, 0x2000, 2);                                  // RTCMODE, sin RTCHOLD
        wait(10, SC_US);
        const char sin_cal = estado(fr.p(1, 0));
        esc(fr, 0x4A2, 0x0100, 2);                                  // RTCCALF = 1: 512 Hz
        wait(10, SC_US);
        check(sin_cal == 'L' && estado(fr.p(1, 0)) == 'H', "RTCCLK en P1.0 (10, de salida): nada con RTCCALF = 0; 512 Hz con 1 [de modelo: nivel alto]");
        // COUT en P3.5 (1X): de salida, el Comp_E apagado da 0; de entrada, nada
        esc(fr, 0x225, 0x00, 1);
        esc(fr, 0x223, 0x00, 1);
        esc(fr, 0x224, 0x20, 1); esc(fr, 0x22C, 0x30, 1);
        wait(10, SC_US);
        const char cout = estado(fr.p(3, 5));
        esc(fr, 0x224, 0x00, 1);
        wait(10, SC_US);
        check(cout == 'L' && estado(fr.p(3, 5)) == 'Z', std::string("COUT en P3.5 (10): de salida, ") + cout + "; de entrada, nada [tabla 6-57]");
        // Comp_E: C0 (P1.0) con CEPD0 pierde la salida digital
        esc(fr, 0x20C, 0x00, 1); esc(fr, 0x202, 0x01, 1);           // P1.0 GPIO, de salida, a 1
        wait(10, SC_US);
        const char gpio = estado(fr.p(1, 0));
        esc(fr, 0x8C6, 0x0001, 2);                                  // CECTL3: CEPD0
        wait(10, SC_US);
        check(gpio == 'H' && estado(fr.p(1, 0)) == 'Z', "C0 en P1.0: con CEPD0, sin salida digital [tabla 6-49, nota 5]");
        // El LFXT: PJ.4 con 01 hace de PJ.5 el cristal; con LFXTBYPASS, PJ.5 es un pin normal
        esc(fr, 0x322, 0x20, 1); esc(fr, 0x324, 0x20, 1); esc(fr, 0x32A, 0x10, 1);
        wait(10, SC_US);
        const char xt = estado(fr.p(11, 5));
        esc(fr, 0x161, 0xA5, 1); esc(fr, 0x168, 0xCDD9, 2); esc(fr, 0x161, 0x00, 1);
        wait(10, SC_US);
        check(xt == 'Z' && estado(fr.p(11, 5)) == 'H',
              std::string("PJ.4 con 01: PJ.5 es LFXOUT (") + xt + "); con LFXTBYPASS, PJ.5 vuelve a ser un pin (PJOUT = 1) [tabla 6-60]");
        // FR6989: TA0.1 en P1.6 con 11; MCLK en P4.0 con 11; A4 en P8.7
        esc(fr69, 0x342 + 2, 0x0004, 2);                            // TA0CCTL1: OUT = 1
        esc(fr69, 0x204, 0x40, 1); esc(fr69, 0x20A, 0x40, 1); esc(fr69, 0x20C, 0x40, 1);
        esc(fr69, 0x225, 0x01, 1); esc(fr69, 0x22B, 0x01, 1); esc(fr69, 0x22D, 0x01, 1);
        wait(10, SC_US);
        check(estado(fr69.p(1, 6)) == 'H' && estado(fr69.p(4, 0)) == 'L',
              "FR6989: TA0.1 en P1.6 con 11 (OUT = 1) y MCLK en P4.0 con 11 (parado en LPM0) [DS SLAS789D tablas 6-21 y 6-26]");
        const int d87 = fr69.p(8, 7).register_driver("a4");
        fr69.p(8, 7).set_drive(d87, 1.5f, 10.0f);
        esc(fr69, 0x26B, 0x80, 1); esc(fr69, 0x26D, 0x80, 1);       // P8.7: 11
        esc(fr69, 0x800, 0x0210, 2); esc(fr69, 0x802, 0x0200, 2); esc(fr69, 0x804, 0x0020, 2);
        esc(fr69, 0x820, 0x0004, 2); esc(fr69, 0x80C, 0, 2); esc(fr69, 0x800, 0x0213, 2);
        wait(1, SC_MS);
        const unsigned a4 = lee(fr69, 0x860);
        fr69.p(8, 7).set_hiz(d87);
        check(a4 >= 1850 && a4 <= 1872, "FR6989: A4 es P8.7 (1,5 V con AVCC de referencia): " + std::to_string(a4) + " [tabla 6-36]");
        // FR5994: MCLK en P5.7 con 11; P8 solo tiene P8.0-P8.3
        esc(fr94, 0x244, 0x80, 1); esc(fr94, 0x24A, 0x80, 1); esc(fr94, 0x24C, 0x80, 1);
        wait(10, SC_US);
        check(estado(fr94.p(5, 7)) == 'L' && !fr94.existe(fr2_pin(8, 4)) && fr94.existe(fr2_pin(8, 3)),
              "FR5994: MCLK en P5.7 con 11 (parado en LPM0); el PN80 tiene P8.0-P8.3 [DS SLASE54D tablas 9-31 y 9-35]");
    }

    void l9() {
        grupo("L9 las erratas de las FR5xx/FR6xx que se modelan (fr59_errata.c, fr69_errata.c)");
        fr.p(1, 2).set_drive(d_p12_, 0.6f, 10.0f);                 // C2 a 0,6 V
        corre(std::string(DIR5969) + "fr59_errata.elf");
        espera_uart("ref9", 200);
        check(linea(fr.uart_tx, "ref9") == "ref9 con refon 1 sin 0",
              "REF9: con REFON = 1 el Comp_E no tiene la de 1,2 V (0,6 V por encima de 0); pedida por CEREFL, sí: «" +
                  linea(fr.uart_tx, "ref9") + "»");
        fr.p(1, 2).set_hiz(d_p12_);
        espera_uart("comp10 activo", 100);
        wait(5, SC_MS);
        const char am = alto(3, 5) ? 'H' : 'L';
        wait(30, SC_MS);
        const char lpm = alto(3, 5) ? 'H' : 'L';
        check(am != lpm, std::string("COMP10: CEON = 0 y CEOUTPOL = 1, COUT en P3.5 cambia en LPM3: ") + am + " → " + lpm);
        espera_uart("adc41", 300);
        {
            const std::string l = linea(fr.uart_tx, "adc41");
            check(num(l, "tb0 ") == 0 && num(l, "ta0 ") >= 15,
                  "ADC41: en LPM3 con ADC12OSC y sin REFON, TB0.0 (SHS = 2) no dispara el ADC12_B y TA0.1 (SHS = 1) sí: «" + l + "»");
        }
        espera_uart("adc71", 100);
        check(linea(fr.uart_tx, "adc71") == "adc71 busy 1 sc 1 luego 0",
              "ADC71: ENC y SC a la vez tras el disparo de TA0: BUSY y SC se quedan a 1 (de ellos sale con ON = 0): «" +
                  linea(fr.uart_tx, "adc71") + "»");
        espera_uart("adc42", 100);
        check(linea(fr.uart_tx, "adc42") == "adc42 busy 1 ifg 0 tov 0 otra 1",
              "ADC42: un SC con la conversión en curso (repetición de un canal) para el ADC: BUSY a 1, sin resultado ni TOV; "
              "con ENC = 0 y ON = 0 vuelve: «" + linea(fr.uart_tx, "adc42") + "»");
        espera_uart("aes1", 100);
        check(linea(fr.uart_tx, "aes1") == "aes1 busy 1 errfg 0", "AES1: escribir AESAXIN con AESBUSY no pone AESERRFG: «" + linea(fr.uart_tx, "aes1") + "»");
        espera_uart("wdg5 entra", 100);
        wait(2, SC_MS);
        check(fr.en_lpm5() && !fr.lpm35(), "WDG5: con el perro en marcha, LPM4.5 entra igual");
        fr.p(1, 1).set_drive(d_s2_, 0.0f, 10.0f);
        espera_uart("wdg5 despierta", 100);
        fr.p(1, 1).set_hiz(d_s2_);
        check(linea(fr.uart_tx, "wdg5 despierta") == "wdg5 despierta rstiv 08", "y despierta por P1.1 con SYSRSTIV = 0x08: «" + linea(fr.uart_tx, "wdg5 despierta") + "»");
        // El FR6989
        fr69.reset(true);
        wait(1, SC_US);
        fr69.carga("verif/fr6989/elf/fr69_errata.elf");
        fr69.uart_tx.clear();
        fr69.reset(false);
        auto lin = [&](const char* c) { return linea(fr69.uart_tx, c); };
        auto espera = [&](const char* c, int ms) {
            auto entera = [&] { const size_t i = fr69.uart_tx.find(c); return i != std::string::npos && fr69.uart_tx.find('\n', i) != std::string::npos; };
            for (int i = 0; i < ms * 10 && !entera(); ++i) wait(100, SC_US);
            return entera();
        };
        espera("ta22 activo", 200);
        wait(5, SC_MS);
        const bool a = fr69.p(1, 6).voltage() > 1.6f;
        wait(30, SC_MS);
        const bool b = fr69.p(1, 6).voltage() > 1.6f;
        check(a && !b, "TA22 (FR6989): TA0.1 en P1.6, a 1 con el Timer parado, pasa a 0 al entrar en LPM3");
        espera("rtc12 tcmp", 200);
        const bool ok_tc = lin("rtc12 tcmp") == "rtc12 tcmp E005";      // RTCTCMPS, RTCTCRDY, RTCTCOK y 5
        espera("rtc12 despierta", 2000);
        check(ok_tc && lin("rtc12 despierta") == "rtc12 despierta rstiv 08 tcmp C005",
              "RTC12 (FR6989): RTCTCOK a 1 tras escribir RTCTCMP («" + lin("rtc12 tcmp") + "») y a 0 tras LPM3.5: «" +
                  lin("rtc12 despierta") + "»");
    }

    void run() {
        wait(10, SC_US);
        const char* solo = std::getenv("FR5_SOLO");
        auto toca = [&](char g) { return !solo || std::strchr(solo, g); };
        if (toca('1')) l1();
        if (toca('2')) l2();
        if (toca('3')) l3();
        if (toca('4')) l4();
        if (toca('5')) l5();
        if (toca('6')) l6();
        if (toca('7')) l7();
        if (toca('8')) l8();
        if (toca('9')) l9();
        std::printf("\nTOTAL FR5 : %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    TbFr5 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %llu ps\n", (unsigned long long)sc_time_stamp().value());
    return g_fallos ? 1 : 0;
}
