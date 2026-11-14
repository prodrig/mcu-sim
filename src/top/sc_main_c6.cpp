// =============================================================================
// sc_main_c6.cpp — LA SUITE DEL ESP32-C6 (fase E9 de doc/analisis_esp32_riscv.md)
//
// E9.2: el firmware de verif/fw/esp32c6 se carga en la HP SRAM y el núcleo
// arranca en su entrada, sin pasar por la ROM («arranque directo», como C1-C7
// del C3). Cada grupo arranca el chip con CHIP_PU.
//
//   D1   sin tiempo: los encapsulados (QFN40, y QFN32 del C6FH4/FH8), el
//        núcleo (misa con A, mie/mip, mideleg 0x111), la ROM en su sitio y las
//        huellas de los ELF
//   D2   el arranque: CHIP_PU, los pines de arranque (GPIO9 con su pull-up),
//        la causa de reset y blinky: GPIO8 cambia diez veces cada 500 us con
//        la CPU a 160 MHz por el PLL del PCR
//   D3   la UART0 por los pines: el saludo sale por GPIO16 a 115200 8N1 con el
//        SCLK del PCR, y lo que entra por GPIO17 vuelve en mayúsculas por la
//        interrupción de RX (la matriz, el INTPRI y mie)
//   D4   el INTPRI y el CLINT [TRM-C6 §1.6, §1.7]: prioridad 0 enmascarada, el
//        umbral, empate por ID, la prioridad, mie, flanco y CLEAR, nivel; el
//        MTI del CLINT con MTIMECMP y el MSI, que el umbral no enmascara; una
//        interrupción delegada a U (utvec, ucause, uret); mpccr
//   D5   los resets: del sistema por LP_AON (causa 3, STORE conservado), de
//        la CPU (causa 12), CHIP_PU (causa 1, STORE borrado) y GPIO9 a masa:
//        GPIO_STRAPPING = 0b0100 con MTMS y MTDI sueltos (la descarga
//        «SDIO_FEI_FEO» de la ROM)
// =============================================================================
#include <systemc>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "common/asan_opciones.h"
#include "soc/esp32c6/esp32c6.h"
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

static const char* const FW = "verif/fw/esp32c6/";

SC_MODULE(TbC6) {
    Esp32C6 c6;
    int d_rx_ = -1, d_g9_ = -1, d_g8_ = -1;
    std::string terminal;
    unsigned flancos_g8 = 0;
    bool prev_g8_ = false;
    static constexpr double BAUD = 115200.0;

    TbC6(sc_module_name nm) : sc_module(nm), c6("c6", MCU_ESP32C6) {
        SC_HAS_PROCESS(TbC6);
        SC_THREAD(run);
        SC_THREAD(terminal_proc);
        SC_METHOD(cuenta_g8);
        sensitive << c6.gpio.nodo_de(8).value_changed_event();
        dont_initialize();
    }
    void end_of_elaboration() override {
        d_rx_ = c6.gpio.nodo_de(17).register_driver("banco_tx");
        c6.gpio.nodo_de(17).set_drive(d_rx_, 3.3f, 100.0f);     // línea en reposo
        d_g9_ = c6.gpio.nodo_de(9).register_driver("banco_g9");
        c6.gpio.nodo_de(9).set_hiz(d_g9_);
        d_g8_ = c6.gpio.nodo_de(8).register_driver("banco_g8");
        c6.gpio.nodo_de(8).set_hiz(d_g8_);
    }
    void cuenta_g8() {
        const bool a = c6.gpio.nodo_de(8).voltage() > 1.65f;
        if (a != prev_g8_) { ++flancos_g8; prev_g8_ = a; }
    }
    // La UART de bancada: 115200 8N1 en el nodo de GPIO16 (U0TXD)
    void terminal_proc() {
        const sc_time tb(1.0 / BAUD, SC_SEC);
        analog_net_if& n = c6.gpio.nodo_de(16);
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
        analog_net_if& n = c6.gpio.nodo_de(17);
        for (char ch : s) {
            n.set_drive(d_rx_, 0.0f, 100.0f); wait(tb);
            for (unsigned i = 0; i < 8; ++i) { n.set_drive(d_rx_, ((ch >> i) & 1) ? 3.3f : 0.0f, 100.0f); wait(tb); }
            n.set_drive(d_rx_, 3.3f, 100.0f); wait(tb * 2.0);
        }
    }

    // --- Arrancar un firmware ------------------------------------------------
    uint32_t buzon_ = 0;
    bool arranca(const std::string& elf) {
        c6.alimenta(false);
        wait(10, SC_US);
        const std::string fw = std::string(FW) + elf;
        const std::string e = c6.carga_elf(fw);
        if (!e.empty()) { std::printf("  (%s)\n", e.c_str()); return false; }
        buzon_ = 0;
        simbolo_elf(fw, "buzon", buzon_);
        c6.poke32(buzon_, 0); c6.poke32(buzon_ + 4, 0);
        c6.gpio.nodo_de(9).set_hiz(d_g9_);
        c6.gpio.nodo_de(8).set_hiz(d_g8_);
        c6.alimenta(true);
        return true;
    }
    uint32_t bz(unsigned i) { return c6.peek32(buzon_ + 8 + 4 * i); }
    bool espera_fin(sc_time lim) {
        const sc_time t0 = sc_time_stamp();
        while (sc_time_stamp() - t0 < lim) {
            if (c6.peek32(buzon_) == 0xC6C6B00Cu && c6.peek32(buzon_ + 4) == 1) return true;
            wait(20, SC_US);
        }
        return false;
    }

    // =========================================================================
    void d1() {
        grupo("D1 los encapsulados, el nucleo, la ROM y las huellas");
        check(ENC_C6_QFN40.coherente() && ENC_C6_QFN32.coherente(),
              "QFN40: 30 GPIO (sin GPIO14); QFN32 (C6FH4/FH8): 22");
        check(!ENC_C6_QFN40.bonded(0, 14) && ENC_C6_QFN40.bonded(0, 15) && !ENC_C6_QFN32.bonded(0, 10) &&
              !ENC_C6_QFN32.bonded(0, 24), "GPIO14 no sale en el QFN40; GPIO10 y la Flash (24-30), no en el QFN32");
        check(MCU_ESP32C6FH4.flash_interna == (4u << 20) && MCU_ESP32C6FH8.flash_interna == (8u << 20) &&
              MCU_ESP32C6.flash_interna == 0, "C6FH4 y C6FH8 con la Flash dentro; el C6, en la placa");
        check_eq(RV_ESP32C6_HP.misa(), 0x40101105u, "misa: RV32 I M A C U");
        uint32_t v = 0;
        RvHart& h = c6.core.hart;
        check(h.csr_lee(0xF11, v) && v == 0x612u && h.csr_lee(0xF12, v) && v == 0x80000002u &&
              h.csr_lee(0xF13, v) && v == 2u, "mvendorid 0x612, marchid 0x8000_0002, mimpid 2");
        check(h.csr_lee(0x304, v) && h.csr_lee(0x344, v) && h.csr_lee(0x303, v) && v == 0x111u,
              "con mie y mip, y mideleg 0x111 de reset");
        check(!h.csr_lee(0xB00, v), "sin mcycle: los contadores son mpccr");
        check(c6.rom.hay() && c6.peek32(c6::ROM_B) == c6::ROM_REV0[0],
              "la ROM rev0 (esp32c6_rev0_rom.elf) en 0x4000_0000");
        std::string fecha;
        for (uint32_t a = 0x4004A000u; a < c6::ROM_FIN && fecha.empty(); ++a) {
            uint8_t b = 0;
            if (c6.peek8(a, b) && b == 'e') {
                char s[17] = {};
                for (unsigned i = 0; i < 16; ++i) { uint8_t x = 0; c6.peek8(a + i, x); s[i] = char(x); }
                if (std::strncmp(s, "esp32c6-20220919", 16) == 0) fecha = s;
            }
        }
        check(!fecha.empty(), "y dentro, su version: \"esp32c6-20220919\"");
        check(c6.limitaciones().size() >= 5, "limitaciones(): lo que no esta todavia");
        const huella_fw::Veredicto hv = huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "c6");
        std::printf("    %d imagenes: %s\n", hv.comprobadas, hv.detalle.c_str());
        check(hv.ok() && hv.comprobadas == 3, "los tres ficheros de la suite son los versionados (verif/fw/huellas.txt)");
    }

    void d2() {
        grupo("D2 el arranque: CHIP_PU, pines de arranque y blinky");
        check(arranca("blinky.elf"), "blinky.elf cargado en la HP SRAM");
        wait(20, SC_US);
        check(c6.encendido(), "CHIP_PU sube con la alimentacion y el chip sale del reset");
        check_eq(c6.gpio.strap() & 0xCu, 0x8u, "GPIO_STRAPPING[3:2] = 0b10: GPIO9 por su pull-up, GPIO8 suelto (SPI Boot)");
        check_eq(c6.lp_clkrst.causa(), 1u, "LP_CLKRST_RESET_CAUSE: 1, encendido");
        const unsigned f0 = flancos_g8;
        const sc_time t0 = sc_time_stamp();
        const bool fin = espera_fin(sc_time(20, SC_MS));
        check(fin, "blinky acaba sus diez cambios");
        check_eq(bz(0), 10u, "y el buzon lo dice");
        check(flancos_g8 - f0 >= 10, "GPIO8 cambia en el nodo diez veces (" + std::to_string(flancos_g8 - f0) + ")");
        const double ms = (sc_time_stamp() - t0).to_seconds() * 1e3;
        check(ms > 4.9 && ms < 5.5, "cada 500 us por el SYSTIMER: " + std::to_string(ms) + " ms");
        check(c6.pcr.hz_cpu() == 160e6 && c6.pcr.hz_apb() == 40e6,
              "el PCR: la CPU a 160 MHz por el PLL, el AHB y el APB a 40");
    }

    void d3() {
        grupo("D3 la UART0 por los pines");
        terminal.clear();
        check(arranca("uart.elf"), "uart.elf");
        wait(4, SC_MS);
        check(terminal == "Hola desde el ESP32-C6\r\n", "el saludo llega por GPIO16 a 115200 8N1: \"" +
              terminal.substr(0, terminal.size() > 2 ? terminal.size() - 2 : 0) + "\"");
        check(c6.uart0.salida == terminal, "lo mismo que dice la UART");
        check_eq(bz(3), 0x02201260u, "UART_DATE del C6 (0x8C)");
        terminal.clear();
        envia("abc");
        wait(1, SC_MS);
        check(terminal == "ABC", "lo que entra por GPIO17 vuelve en mayusculas, por la interrupcion de RX");
        check_eq(bz(2), 3u, "tres caracteres en la ISR");
        check(c6.intpri.reclamaciones() >= 1, "por la matriz y el INTPRI, linea 5");
    }

    void d4() {
        grupo("D4 el INTPRI, el CLINT y la delegacion a U");
        check(arranca("intc.elf") && espera_fin(sc_time(20, SC_MS)), "intc.elf acaba");
        check_eq(bz(20), 0x111u, "mideleg de reset: 0x111");
        check_eq(bz(0), 0u, "prioridad 0 con umbral 1: enmascarada, y fuera de EIP_STATUS");
        check_eq(bz(1), 0u, "prioridad 2 con umbral 3: no entra");
        check_eq(bz(2), 1u, "con umbral 2: entra (prioridad >= umbral)");
        check_eq(bz(3), 9u | (10u << 8) | (2u << 16), "empate de prioridad: la 9 antes que la 10");
        check_eq(bz(4), 12u | (11u << 8) | (2u << 16), "la de mas prioridad primero: la 12 (7) antes que la 11 (6)");
        check_eq(bz(5), 0u | (1u << 8), "con su bit de mie a 0 no entra, y mip la muestra pendiente");
        check_eq(bz(6), 1u, "con mie, entra");
        check_eq(bz(7), 1u, "flanco: enganchada aunque la fuente ya haya bajado");
        check_eq(bz(8), 2u, "sin el pulso de CLEAR vuelve a entrar; con el, no");
        check_eq(bz(9), 1u, "una de nivel no se borra con CLEAR");
        check_eq(bz(10), 1u, "y entra hasta que se borra en la fuente");
        check(bz(11) >= 1000 && bz(11) <= 1100, "el MTI del CLINT: MTIMECMP a 1000 cuentas (" + std::to_string(bz(11)) + ")");
        check_eq(bz(12), 1u, "el MSI entra con umbral 15: el umbral no enmascara el CLINT");
        check_eq(bz(13), 1u << 8, "delegada a U (mideleg.14): en M no entra, y uip la muestra");
        check_eq(bz(14), 1u, "en U entra por utvec + 4*14 y uret vuelve");
        check_eq(bz(15), 0x8000000Eu, "ucause = 0x8000_000E");
        check_eq(bz(16), 0u, "y M no la ve");
        check(bz(17) > 100 && bz(17) < 3000, "mpccr cuenta ciclos (" + std::to_string(bz(17)) + ")");
        check_eq(bz(18), 0x80000000u, "pma_cfg0 de reset: NA4 en la direccion 0, TYPE 0 (no valida)");
        check_eq(bz(19), 1u | (5u << 8), "leer un puntero nulo: el fallo de carga (mcause 5)");
        check_eq(bz(21), 1u | (7u << 8), "una entrada NAPOT de solo lectura: escribir da mcause 7, leer no (y no se escribio)");
        check_eq(bz(22), 0x5678u, "con la entrada quitada (TYPE 0, A = OFF), se escribe");
        check_eq(c6.negadas_pma(), 2u, "dos accesos negados por la PMA");
    }

    void d5() {
        grupo("D5 los resets y los pines de arranque");
        terminal.clear();
        check(arranca("uart.elf"), "uart.elf otra vez");
        wait(3, SC_MS);
        check_eq(bz(0), 1u, "causa 1 tras encender");
        c6.lp_aon.dbg_write(0x00, 0xCAFE0001u);                   // LP_AON_STORE0
        c6.lp_aon.dbg_write(0x34, 1u << 31);                      // HPSYS_SW_RESET
        wait(3, SC_MS);
        check_eq(bz(0), 3u, "reset del sistema por software: causa 3 (LP_AON_HPSYS_SW_RESET)");
        check_eq(c6.lp_aon.store(0), 0xCAFE0001u, "y STORE0 sobrevive");
        check(terminal.find("Hola") != std::string::npos, "el firmware vuelve a saludar");
        c6.lp_aon.dbg_write(0x38, 1u << 28);                      // CPU_CORE0_SW_RESET
        wait(3, SC_MS);
        check_eq(bz(0), 12u, "reset de la CPU por software: causa 12 (LP_AON_CPU_CORE0_SW_RESET)");
        // GPIO9 a masa y GPIO8 arriba al arrancar: el modo descarga
        c6.gpio.nodo_de(9).set_drive(d_g9_, 0.0f, 100.0f);
        c6.gpio.nodo_de(8).set_drive(d_g8_, 3.3f, 10e3f);
        c6.alimenta(false); wait(20, SC_US); c6.alimenta(true);
        wait(50, SC_US);
        c6.gpio.nodo_de(9).set_hiz(d_g9_);
        c6.gpio.nodo_de(8).set_hiz(d_g8_);
        check_eq(c6.gpio.strap(), 0x4u, "GPIO_STRAPPING = 0b0100: GPIO9 a 0, GPIO8 a 1, MTMS y MTDI sueltos");
        check_eq(c6.lp_aon.store(0), 0u, "y un reset de chip borra los STORE");
        wait(3, SC_MS);
        check_eq(bz(0), 1u, "causa 1 de nuevo");
        check_eq(bz(1), 0x4u, "y el firmware lee lo mismo en GPIO_STRAP");
    }

    void run() {
        wait(SC_ZERO_TIME);
        d1();
        d2();
        d3();
        d4();
        d5();
        std::printf("\nTOTAL C6: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING, std::getenv("TESTC6_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    TbC6 tb("tb");
    sc_start();
    const unsigned long long ps =
        (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    std::printf("\nTiempo simulado: %llu ps\n", ps);
    return g_fallos ? 1 : 0;
}
