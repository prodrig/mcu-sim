// =============================================================================
// sc_main_c3.cpp — LA SUITE DEL ESP32-C3 (fase E3 de doc/analisis_esp32_riscv.md)
//
// El C3 sin ROM: el firmware de verif/fw/esp32c3 se carga en la SRAM y el
// núcleo arranca en su entrada. Cada grupo arranca el chip con CHIP_PU, como
// en la placa.
//
//   C1   sin tiempo: los encapsulados, la nomenclatura, el núcleo (misa, los
//        CSR de identificación, sin mie/mip) y las huellas de los ELF
//   C2   el arranque: CHIP_PU, los pines de arranque (GPIO9 con su pull-up),
//        la causa de reset y blinky: GPIO8 cambia diez veces cada 500 us con
//        la CPU a 160 MHz
//   C3   la UART0 por los pines: el saludo sale por GPIO21 a 115200 8N1 (lo
//        lee una UART de bancada en el nodo), y lo que entra por GPIO20 vuelve
//        en mayúsculas por la interrupción de RX
//   C4   el INTC [TRM-C3 §1.5.2]: prioridad 0 enmascarada, el umbral, empate
//        por ID, la prioridad, flanco enganchado y su pulso de borrado, nivel
//        que no se borra con CLEAR; SYSTIMER en modo periodo, la alarma del T0
//        de TIMG0 y mpccr
//   C5   la matriz GPIO: la UART1 por GPIO4/5 unidos por fuera, la GPIO
//        dedicada por los CSR, la interrupción de pin, las entradas
//        constantes y los pull del IO_MUX
//   C6   los resets: por software del sistema (RTC_CNTL, causa 3, STORE
//        conservado), CHIP_PU (causa 1) y GPIO9 a masa en el arranque
//   C7   CoreMark a 160 MHz, validado por su CRC
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/esp32c3/esp32c3.h"
#include "common/huella_fw.h"

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) {
    std::printf("\n--- %s --- [t=%s]\n", t, sc_time_stamp().to_string().c_str());
}
static bool check(bool c, const std::string& q) {
    if (c) { ++g_ok;  std::printf("  [OK  ] %s\n", q.c_str()); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q.c_str()); }
    return c;
}
static bool check_eq(uint64_t got, uint64_t exp, const std::string& q) {
    const bool c = got == exp;
    if (c) std::printf("  [OK  ] %s\n", q.c_str());
    else   std::printf("  [FALLO] %s (obtenido 0x%llX, esperado 0x%llX)\n", q.c_str(),
                       (unsigned long long)got, (unsigned long long)exp);
    if (c) ++g_ok; else ++g_fallos;
    return c;
}

static const char* const FW = "verif/fw/esp32c3/";

// Los cables de la placa de pruebas: GPIO4-GPIO5 y GPIO6-GPIO7
struct Cables {
    AnalogNet g45{"cable_g4_g5"}, g67{"cable_g6_g7"};
    Cableado cab;
    Cables() { cab.une(0, 4, g45); cab.une(0, 5, g45); cab.une(0, 6, g67); cab.une(0, 7, g67); }
};

SC_MODULE(TbC3) {
    Cables cables;
    Esp32C3 c3;
    // Una UART de bancada en GPIO21 (lee) y GPIO20 (escribe)
    int d_rx_ = -1, d_g9_ = -1;
    std::string terminal;
    unsigned flancos_g8 = 0;

    TbC3(sc_module_name nm) : sc_module(nm), c3("c3", MCU_ESP32C3, cables.cab) {
        SC_HAS_PROCESS(TbC3);
        SC_THREAD(run);
        SC_THREAD(terminal_proc);
        SC_METHOD(cuenta_g8);
        sensitive << c3.gpio.nodo_de(8).value_changed_event();
        dont_initialize();
    }
    void end_of_elaboration() override {
        d_rx_ = c3.gpio.nodo_de(20).register_driver("banco_tx");
        c3.gpio.nodo_de(20).set_drive(d_rx_, 3.3f, 100.0f);     // línea en reposo
        d_g9_ = c3.gpio.nodo_de(9).register_driver("banco_g9");
        c3.gpio.nodo_de(9).set_hiz(d_g9_);
    }

    bool g8_alto() { return c3.gpio.nodo_de(8).voltage() > 1.65f; }
    bool prev_g8_ = false;
    void cuenta_g8() {
        const bool a = g8_alto();
        if (a != prev_g8_) { ++flancos_g8; prev_g8_ = a; }
    }

    // La UART de bancada: 115200 8N1 en el nodo de GPIO21
    static constexpr double BAUD = 115200.0;
    void terminal_proc() {
        const sc_time tb(1.0 / BAUD, SC_SEC);
        analog_net_if& n = c3.gpio.nodo_de(21);
        for (;;) {
            wait(n.value_changed_event());
            if (n.voltage() > 1.65f) continue;
            wait(tb * 1.5);
            unsigned d = 0;
            for (unsigned i = 0; i < 8; ++i) { if (n.voltage() > 1.65f) d |= 1u << i; wait(tb); }
            if (n.voltage() > 1.65f) terminal.push_back(char(d));
        }
    }
    void envia(const std::string& s) {
        const sc_time tb(1.0 / BAUD, SC_SEC);
        analog_net_if& n = c3.gpio.nodo_de(20);
        for (char ch : s) {
            n.set_drive(d_rx_, 0.0f, 100.0f); wait(tb);
            for (unsigned i = 0; i < 8; ++i) { n.set_drive(d_rx_, ((ch >> i) & 1) ? 3.3f : 0.0f, 100.0f); wait(tb); }
            n.set_drive(d_rx_, 3.3f, 100.0f); wait(tb * 2.0);
        }
    }

    // --- Arrancar un firmware ------------------------------------------------
    uint32_t buzon_ = 0;
    std::string fw_;
    bool arranca(const std::string& elf) {
        c3.alimenta(false);
        wait(10, SC_US);
        fw_ = std::string(FW) + elf;
        const std::string e = c3.carga_elf(fw_);
        if (!e.empty()) { std::printf("  (%s)\n", e.c_str()); return false; }
        buzon_ = 0;
        simbolo_elf(fw_, "buzon", buzon_);
        c3.poke32(buzon_, 0); c3.poke32(buzon_ + 4, 0);
        c3.alimenta(true);
        return true;
    }
    uint32_t bz(unsigned i) { return c3.peek32(buzon_ + 8 + 4 * i); }
    bool espera_fin(sc_time lim) {
        const sc_time t0 = sc_time_stamp();
        while (sc_time_stamp() - t0 < lim) {
            if (c3.peek32(buzon_) == 0xC3C3B00Cu && c3.peek32(buzon_ + 4) == 1) return true;
            wait(20, SC_US);
        }
        return false;
    }

    // =========================================================================
    void c1() {
        grupo("C1 los encapsulados, la nomenclatura y el nucleo");
        check(ENC_C3_QFN32.coherente() && ENC_C3_QFN32_X.coherente(),
              "QFN32: 22 GPIO; las X (C3FH4X), 16 (sin GPIO12-17)");
        unsigned p = 9, i = 9;
        check(c3::pad_c3("GPIO8", p, i) && p == 0 && i == 8 && c3::pad_c3("io21", p, i) && i == 21 &&
              c3::pad_c3("MTDO", p, i) && i == 7 && !c3::pad_c3("GPIO22", p, i) &&
              !c3::pad_c3("GPIO08", p, i),
              "GPIO8, IO21 y MTDO se entienden; GPIO22 y GPIO08, no");
        check_eq(RV_ESP32C3.misa(), 0x40101104u, "misa: RV32 I M C U (sin A)");
        uint32_t v = 0;
        RvHart& h = c3.core.hart;
        check(h.csr_lee(0xF11, v) && v == 0x612u && h.csr_lee(0xF12, v) && v == 0x80000001u &&
              h.csr_lee(0xF13, v) && v == 1u, "mvendorid 0x612, marchid 0x8000_0001, mimpid 1");
        check(!h.csr_lee(0x304, v) && !h.csr_lee(0x344, v) && !h.csr_lee(0xB00, v),
              "sin mie, mip ni mcycle: el C3 los tiene en el INTC y en mpccr");
        check(c3.limitaciones().size() >= 5, "limitaciones(): sin ROM, Flash, cache ni perifericos de E8");
        const huella_fw::Veredicto hv = huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "c3");
        std::printf("    %d imagenes: %s\n", hv.comprobadas, hv.detalle.c_str());
        check(hv.ok() && hv.comprobadas == 5, "los cinco ELF de la suite son los versionados (verif/fw/huellas.txt)");
    }

    void c2() {
        grupo("C2 el arranque: CHIP_PU, pines de arranque y blinky");
        check(arranca("blinky.elf"), "blinky.elf cargado en la SRAM");
        wait(20, SC_US);
        check(c3.encendido(), "CHIP_PU sube con la alimentacion y el chip sale del reset");
        check_eq(c3.gpio.strap() & 0x8u, 0x8u, "GPIO_STRAP[3] = 1: GPIO9 por su pull-up (SPI Boot)");
        check_eq(c3.rtc.causa(), 1u, "RESET_STATE: 1, encendido");
        const unsigned f0 = flancos_g8;
        const sc_time t0 = sc_time_stamp();
        const bool fin = espera_fin(sc_time(20, SC_MS));
        check(fin, "blinky acaba sus diez cambios");
        check_eq(bz(0), 10u, "y el buzon lo dice");
        check(flancos_g8 - f0 >= 10, "GPIO8 cambia en el nodo diez veces (" + std::to_string(flancos_g8 - f0) + ")");
        const double ms = (sc_time_stamp() - t0).to_seconds() * 1e3;
        check(ms > 4.9 && ms < 5.5, "cada 500 us por el SYSTIMER: " + std::to_string(ms) + " ms");
        check(c3.system.hz_cpu() == 160e6 && c3.system.hz_apb() == 80e6, "la CPU a 160 MHz y el APB a 80 (PLL)");
    }

    void c3_uart() {
        grupo("C3 la UART0 por los pines");
        terminal.clear();
        check(arranca("uart.elf"), "uart.elf");
        wait(4, SC_MS);
        check(terminal == "Hola desde el ESP32-C3\r\n", "el saludo llega por GPIO21 a 115200 8N1: \"" +
              terminal.substr(0, terminal.size() > 2 ? terminal.size() - 2 : 0) + "\"");
        check(c3.uart0.salida == terminal, "lo mismo que dice la UART");
        terminal.clear();
        envia("abc");
        wait(1, SC_MS);
        check(terminal == "ABC", "lo que entra por GPIO20 vuelve en mayusculas, por la interrupcion de RX");
        check_eq(bz(2), 3u, "tres caracteres en la ISR");
        check(c3.intmtx.reclamaciones() >= 1, "por el INTC, linea 3");
    }

    void c4() {
        grupo("C4 el INTC, el SYSTIMER y TIMG0");
        check(arranca("intc.elf") && espera_fin(sc_time(20, SC_MS)), "intc.elf acaba");
        check_eq(bz(0), 0u, "prioridad 0: enmascarada, y fuera de EIP_STATUS");
        check_eq(bz(1), 0u, "prioridad 2 con umbral 3: no entra");
        check_eq(bz(2), 1u, "con umbral 2: entra");
        check_eq(bz(3), 4u | (6u << 8) | (2u << 16), "empate de prioridad: la 4 antes que la 6");
        check_eq(bz(4), 9u | (7u << 8) | (2u << 16), "la de mas prioridad primero: la 9 (7) antes que la 7 (6)");
        check_eq(bz(5), 1u, "flanco: enganchada aunque la fuente ya haya bajado");
        check_eq(bz(6), 2u, "sin el pulso de CLEAR vuelve a entrar; con el, no");
        check_eq(bz(7), 2u, "una de flanco sin reclamar y deshabilitada se vacia con el pulso");
        check_eq(bz(8), 1u, "una de nivel no se borra con CLEAR");
        check_eq(bz(9), 1u, "y entra hasta que se borra en la fuente");
        check_eq(bz(10), 5u, "SYSTIMER en modo periodo: cinco alarmas");
        check(bz(11) >= 4 * 1600 - 40 && bz(11) <= 4 * 1600 + 80,
              "cada 1600 cuentas (100 us): " + std::to_string(bz(11)) + " en cuatro periodos");
        check(bz(12) >= 31900 && bz(12) <= 32400,
              "TIMG0 T0: la alarma a los 2 ms (" + std::to_string(bz(12)) + " cuentas de 16 MHz)");
        check_eq(bz(13), 0u, "y ALARM_EN se baja sola");
        check(bz(14) > 100 && bz(14) < 3000, "mpccr cuenta ciclos (" + std::to_string(bz(14)) + ")");
    }

    void c5() {
        grupo("C5 la matriz GPIO, la GPIO dedicada y la interrupcion de pin");
        check(arranca("gpio_matriz.elf") && espera_fin(sc_time(20, SC_MS)), "gpio_matriz.elf acaba");
        check_eq(bz(0), 6u, "la UART1 por GPIO4 -> GPIO5: vuelven los seis caracteres");
        check_eq(bz(1), uint32_t('m') | uint32_t('a') << 8 | uint32_t('t') << 16 | uint32_t('z') << 24,
                 "\"matriz\"");
        check_eq(bz(2), 0x5u, "cpu_gpio_out0 por GPIO6 vuelve por cpu_gpio_in0 desde GPIO7 (y GPIO_IN)");
        check_eq(bz(3), 3u, "tres subidas en GPIO7, tres interrupciones de pin (linea 8)");
        check_eq(bz(4), 0x5u, "IN_SEL 0x1E da 1, 0x1F da 0, e INV lo invierte");
        check_eq(bz(5), 0x1u, "GPIO10 suelto: 1 con FUN_WPU, 0 con FUN_WPD");
    }

    void c6() {
        grupo("C6 los resets");
        terminal.clear();
        check(arranca("uart.elf"), "uart.elf otra vez");
        wait(3, SC_MS);
        check_eq(bz(0), 1u, "causa 1 tras encender");
        c3.rtc.dbg_write(0x50, 0xCAFE0001u);                    // RTC_CNTL_STORE0
        c3.rtc.dbg_write(0x00, 1u << 31);                       // SW_SYS_RST
        wait(3, SC_MS);
        check_eq(bz(0), 3u, "reset del sistema por software: causa 3 (RTC_CNTL_SW_SYS_RST)");
        check_eq(c3.rtc.store(0), 0xCAFE0001u, "y STORE0 sobrevive");
        check(terminal.find("Hola") != std::string::npos, "el firmware vuelve a saludar");
        c3.rtc.dbg_write(0x00, 1u << 5);                         // SW_PROCPU_RST
        wait(3, SC_MS);
        check_eq(bz(0), 12u, "reset de la CPU por software: causa 12");
        // GPIO9 a masa al arrancar: Joint Download Boot
        c3.gpio.nodo_de(9).set_drive(d_g9_, 0.0f, 100.0f);
        c3.alimenta(false); wait(20, SC_US); c3.alimenta(true);
        wait(50, SC_US);
        c3.gpio.nodo_de(9).set_hiz(d_g9_);
        check_eq(c3.gpio.strap() & 0x8u, 0x0u, "con GPIO9 a masa, GPIO_STRAP[3] = 0 (descarga; sin ROM, sin efecto)");
        check_eq(c3.rtc.store(0), 0u, "y un reset de chip borra los STORE");
        wait(3, SC_MS);
        check_eq(bz(0), 1u, "causa 1 de nuevo");
    }

    void c7() {
        grupo("C7 CoreMark a 160 MHz");
        const std::string elf = std::string(FW) + "coremark_c3.elf";
        const sc_time t0 = sc_time_stamp();
        check(arranca("coremark_c3.elf") && espera_fin(sc_time(200, SC_MS)), "coremark_c3.elf acaba");
        const sc_time dt = sc_time_stamp() - t0;
        uint32_t con = 0;
        simbolo_elf(elf, "cm_console", con);
        std::string sal;
        const uint32_t len = c3.peek32(con + 4);
        for (uint32_t i = 0; i < len && i < 4096; ++i) { uint8_t b = 0; c3.peek8(con + 8 + i, b); sal.push_back(char(b)); }
        check(sal.find("seedcrc          : 0xe9f5") != std::string::npos &&
              sal.find("[0]crclist       : 0xe714") != std::string::npos &&
              sal.find("[0]crcmatrix     : 0x1fd7") != std::string::npos &&
              sal.find("[0]crcstate      : 0x8e3a") != std::string::npos,
              "seedcrc, crclist, crcmatrix y crcstate, las de referencia");
        check(sal.find("[0]crcfinal      : 0xe714") != std::string::npos, "crcfinal = 0xe714");
        std::printf("    %llu instrucciones, %llu ciclos, %s simulados\n",
                    (unsigned long long)c3.core.hart.instret(), (unsigned long long)c3.core.hart.ciclos(),
                    dt.to_string().c_str());
        if (std::getenv("C3_COREMARK")) std::printf("%s\n", sal.c_str());
    }

    void run() {
        c1();
        c2();
        c3_uart();
        c4();
        c5();
        c6();
        c7();
        std::printf("\nTOTAL C3: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, std::getenv("TESTC3_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    TbC3 tb("tb");
    sc_start();
    auto ps = [](const sc_time& t) {
        return (unsigned long long)(t.value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    };
    std::printf("\nTiempo simulado: %llu ps\n", ps(sc_time_stamp()));
    return g_fallos ? 1 : 0;
}
