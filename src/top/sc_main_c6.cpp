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
// E9.3: por la ROM de verdad (rev0) y la Flash, por SPI0/SPI1, la caché y la
// MMU del C6.
//   D6   Direct Boot (0xAEDB041D dos veces): la ROM proyecta la Flash en la
//        ventana única y salta a 0x4200_0008; el firmware corre y lee sus
//        constantes por la caché
//   D7   ESP-IDF v5.5.6 hello_world en un C6 v0.2 con la Flash en la placa:
//        la ROM, el bootloader, la tabla de particiones, la app con FreeRTOS,
//        y esp_restart() a los diez segundos (rst:0xc, otra vez la ROM)
//   D8   el modo descarga (GPIO9 a masa, GPIO8 arriba): la ROM dice
//        «DOWNLOAD(USB/UART0/SDIO_FEI_FEO)» y espera; y el C6FH4, con la
//        Flash dentro, arranca el mismo hello_world
// E9.4: los periféricos comunes con el C3, y el segundo TWAI.
//   D9   verif/fw/esp32c6/idf/perifericos (nuestra, con los controladores de
//        ESP-IDF v5.5.6) con su banco: LEDC con el fundido de la RAM gamma, RMT,
//        TWAI0 <-> TWAI1 por un bus CAN, GP-SPI2 y I2S con la GDMA, I2C, el SAR
//        ADC y el sensor de temperatura, AES y SHA en modo DMA, RSA, y el DATE
//        de cada bloque en su sitio
// Los binarios de ESP-IDF: verif/fw/esp32c6/idf/idf-v5.5.tar.xz, que `make
// testc6` extrae en build/idf-c6.
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
#include <memory>
#include <vector>

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

// E9.4: el banco de verif/fw/esp32c6/idf/perifericos: los cables entre pines,
// el bus CAN (una Y cableada, como dos transceptores), el divisor que pone
// 1,2 V en GPIO0, los pull-ups del I2C, y la medida de GPIO2 (LEDC) y GPIO3
// (RMT)
SC_MODULE(BancoPerifC6) {
    Esp32C6& c6;
    // LEDC: los flancos de subida y bajada de GPIO2
    std::vector<double> sube2, baja2;
    // RMT: los anchos (s) de cada nivel en GPIO3
    std::vector<std::pair<bool, double>> rmt;
    // Conectado o no (en testc6, solo durante D9: si no, sus cables y su
    // divisor cambiarían los pines de arranque de los otros grupos)
    bool activo = true;
    sc_event ev_activo;
    void conecta(bool si) { activo = si; ev_activo.notify(SC_ZERO_TIME); }
    BancoPerifC6(sc_module_name nm, Esp32C6& c, bool act = true) : sc_module(nm), c6(c), activo(act) {
        SC_HAS_PROCESS(BancoPerifC6);
        SC_THREAD(cables);
        SC_METHOD(mide2);
        sensitive << c6.gpio.nodo_de(2).value_changed_event();
        dont_initialize();
        SC_METHOD(mide3);
        sensitive << c6.gpio.nodo_de(3).value_changed_event();
        dont_initialize();
    }
    static bool alto(analog_net_if& n) { return n.voltage() > 1.65f; }
    void mide2() { (alto(c6.gpio.nodo_de(2)) ? sube2 : baja2).push_back(sc_time_stamp().to_seconds()); }
    double t3_ = 0;
    bool n3_ = false;
    void mide3() {
        const bool a = alto(c6.gpio.nodo_de(3));
        if (a == n3_) return;
        const double t = sc_time_stamp().to_seconds();
        if (t3_ > 0) rmt.push_back({n3_, t - t3_});
        t3_ = t; n3_ = a;
    }
    void cables() {
        auto& g = c6.gpio;
        const int d21 = g.nodo_de(21).register_driver("banco_miso"), d11 = g.nodo_de(11).register_driver("banco_din");
        const int d5 = g.nodo_de(5).register_driver("can_rx0"), d7 = g.nodo_de(7).register_driver("can_rx1");
        const int d0 = g.nodo_de(0).register_driver("divisor_1v2");
        const int p1 = g.nodo_de(1).register_driver("pullup_sda"), p15 = g.nodo_de(15).register_driver("pullup_scl");
        for (;;) {
            if (!activo) {
                for (auto x : {std::make_pair(21, d21), std::make_pair(11, d11), std::make_pair(5, d5), std::make_pair(7, d7),
                               std::make_pair(0, d0), std::make_pair(1, p1), std::make_pair(15, p15)})
                    g.nodo_de(unsigned(x.first)).set_hiz(x.second);
                wait(ev_activo);
                continue;
            }
            g.nodo_de(0).set_drive(d0, 1.2f, 100.0f);
            g.nodo_de(1).set_drive(p1, 3.3f, 4.7e3f);
            g.nodo_de(15).set_drive(p15, 3.3f, 4.7e3f);
            g.nodo_de(21).set_drive(d21, alto(g.nodo_de(20)) ? 3.3f : 0.0f, 50.0f);
            g.nodo_de(11).set_drive(d11, alto(g.nodo_de(10)) ? 3.3f : 0.0f, 50.0f);
            // El bus: dominante (0) si alguno de los dos TX está a 0
            const bool rec = !(g.nodo_de(4).voltage() < 1.0f && !g.nodo_de(4).floating()) &&
                             !(g.nodo_de(6).voltage() < 1.0f && !g.nodo_de(6).floating());
            g.nodo_de(5).set_drive(d5, rec ? 3.3f : 0.0f, 50.0f);
            g.nodo_de(7).set_drive(d7, rec ? 3.3f : 0.0f, 50.0f);
            wait(g.nodo_de(20).value_changed_event() | g.nodo_de(10).value_changed_event() |
                 g.nodo_de(4).value_changed_event() | g.nodo_de(6).value_changed_event() | ev_activo);
        }
    }
};

SC_MODULE(TbC6) {
    Esp32C6 c6;
    SpiFlashChip flash{*busca_modelo_flash("GD25Q32")};
    Esp32C6 c6b{"c6b", MCU_ESP32C6FH4};
    BancoPerifC6 banco{"banco", c6, false};
    int d_rx_ = -1, d_g9_ = -1, d_g8_ = -1;
    std::string terminal;
    unsigned flancos_g8 = 0;
    bool prev_g8_ = false;
    static constexpr double BAUD = 115200.0;

    TbC6(sc_module_name nm) : sc_module(nm), c6("c6", MCU_ESP32C6) {
        SC_HAS_PROCESS(TbC6);
        c6.pon_flash(&flash);
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

    bool espera_texto(const std::string& t, const std::string& que, sc_time lim) {
        const sc_time t0 = sc_time_stamp();
        while (t.find(que) == std::string::npos) {
            if (sc_time_stamp() - t0 >= lim) return false;
            wait(1, SC_MS);
        }
        return true;
    }
    static bool tiene(const std::string& t, const std::string& q) { return t.find(q) != std::string::npos; }
    static unsigned cuenta(const std::string& t, const std::string& q) {
        unsigned n = 0;
        for (size_t p = t.find(q); p != std::string::npos; p = t.find(q, p + 1)) ++n;
        return n;
    }
    // Un flash_args de ESP-IDF en la Flash de `chip`
    static bool carga_flash_args(Esp32C6& chip, const std::string& dir) {
        std::FILE* f = std::fopen((dir + "/flash_args").c_str(), "r");
        if (!f) { std::printf("  (no hay %s/flash_args: make testc6 extrae build/idf-c6)\n", dir.c_str()); return false; }
        char l[512], nom[400];
        unsigned long off;
        bool ok = true;
        unsigned n = 0;
        while (std::fgets(l, sizeof l, f))
            if (std::sscanf(l, "0x%lx %399s", &off, nom) == 2) {
                const std::string e = chip.carga_imagen(dir + "/" + nom, uint32_t(off));
                if (!e.empty()) { std::printf("  (%s)\n", e.c_str()); ok = false; }
                ++n;
            }
        std::fclose(f);
        return ok && n == 3;
    }
    // Apaga c6, vacía su Flash y la carga; GPIO9 y GPIO8 como se pida
    bool arranca_flash(const std::string& fa_dir, const std::string& bin = "", bool descarga = false) {
        c6.alimenta(false);
        wait(10, SC_US);
        flash.borra_todo();
        bool ok = true;
        if (!fa_dir.empty()) ok = carga_flash_args(c6, fa_dir);
        else if (!bin.empty()) { const std::string e = c6.carga_imagen(bin, 0); ok = e.empty(); if (!ok) std::printf("  (%s)\n", e.c_str()); }
        else c6.arranca_por_rom();
        if (descarga) {
            c6.gpio.nodo_de(9).set_drive(d_g9_, 0.0f, 100.0f);
            c6.gpio.nodo_de(8).set_drive(d_g8_, 3.3f, 10e3f);
        } else {
            c6.gpio.nodo_de(9).set_hiz(d_g9_);
            c6.gpio.nodo_de(8).set_hiz(d_g8_);
        }
        terminal.clear();
        c6.uart0.salida.clear();
        c6.alimenta(true);
        return ok;
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
        check(hv.ok() && hv.comprobadas == 5, "los cinco ficheros de la suite son los versionados (verif/fw/huellas.txt)");
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

    // =========================================================================
    // E9.3: por la ROM y la Flash
    // =========================================================================
    void d6() {
        grupo("D6 Direct Boot: el firmware corre desde la Flash, por la cache");
        const std::string elf = std::string(FW) + "direct_boot.elf";
        check(arranca_flash("", std::string(FW) + "direct_boot.bin"), "direct_boot.bin en la Flash, desde 0");
        buzon_ = 0;
        simbolo_elf(elf, "buzon", buzon_);
        c6.poke32(buzon_ + 4, 0);
        const uint64_t f0 = c6.cache.fallos(), a0 = c6.cache.aciertos();
        check(espera_texto(terminal, "Direct Boot: desde la Flash, por la cache\r\n", sc_time(60, SC_MS)),
              "el saludo llega por GPIO16: \"Direct Boot: desde la Flash, por la cache\"");
        check(tiene(terminal, "ESP-ROM:esp32c6-20220919") && tiene(terminal, "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)"),
              "antes, la ROM (esp32c6-20220919): encendido y arranque desde la Flash");
        check(espera_fin(sc_time(10, SC_MS)), "y el buzon dice que ha acabado");
        check(bz(0) >= 0x42000000u && bz(0) < 0x43000000u, "main() ejecuta en la ventana de la Flash (0x" +
              [&] { char b[16]; std::snprintf(b, sizeof b, "%08X", bz(0)); return std::string(b); }() + ")");
        check(bz(1) >= 0x42000000u && bz(1) < 0x43000000u, "y sus constantes estan en la misma ventana");
        uint32_t x = 0;
        for (uint32_t i = 0; i < 64; ++i) x ^= (0x9E3779B9u * i + 0x7F4A7C15u) + i;
        check_eq(bz(2), x, "la tabla leida por la cache es la de la imagen");
        check_eq(bz(3), 1u, "LP_CLKRST_RESET_CAUSE: 1");
        check(c6.cache.fallos() > f0 && c6.cache.aciertos() - a0 > 10 * (c6.cache.fallos() - f0),
              "la cache: " + std::to_string(c6.cache.aciertos() - a0) + " aciertos y " +
              std::to_string(c6.cache.fallos() - f0) + " fallos");
    }

    void d7() {
        grupo("D7 ESP-IDF v5.5 hello_world en un C6 v0.2 (la Flash en la placa)");
        const uint64_t fb0 = c6.fallos_bus();
        check(arranca_flash("build/idf-c6/v5.5/hello_world"), "su flash_args: bootloader, particiones y app");
        check(espera_texto(terminal, "Restarting in 10 seconds...", sc_time(1, SC_SEC)), "llega a \"Restarting in 10 seconds...\"");
        check(tiene(terminal, "ESP-ROM:esp32c6-20220919") && tiene(terminal, "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)"),
              "la ROM: encendido y arranque desde la Flash");
        check(tiene(terminal, "load:0x40875730") && tiene(terminal, "entry 0x4086b91a"),
              "la ROM carga el bootloader de la Flash (comprobando su SHA-256 con el SHA)");
        check(tiene(terminal, "ESP-IDF v5.5.6 2nd stage bootloader") && tiene(terminal, "boot: chip revision: v0.2"),
              "el bootloader v5.5.6 lee la revision v0.2 del eFuse");
        check(tiene(terminal, "factory app      00 00 00010000 00100000"), "la tabla de particiones");
        check(tiene(terminal, "cpu_start: cpu freq: 160000000 Hz"), "la app pone la CPU a 160 MHz (el PLL del PCR)");
        check(tiene(terminal, "spi_flash: detected chip: gd"), "y reconoce la GD25Q32 por su JEDEC ID");
        check(tiene(terminal, "Hello world!"), "Hello world!");
        check(tiene(terminal, "This is esp32c6 chip with 1 CPU core(s), WiFi/BLE, 802.15.4 (Zigbee/Thread), silicon revision v0.2, 2MB external flash"),
              "\"This is esp32c6 chip ... silicon revision v0.2, 2MB external flash\"");
        check(c6.uart0.salida == terminal, "lo que lee la UART de bancada en GPIO16 es lo que manda la UART0");
        check(c6.intpri.reclamaciones() > 10, "FreeRTOS: el tick por el SYSTIMER, la matriz y el PLIC (" +
              std::to_string(c6.intpri.reclamaciones()) + " interrupciones)");
        check(espera_texto(terminal, "rst:0xc (SW_CPU)", sc_time(15, SC_SEC)),
              "esp_restart() a los diez segundos: la ROM otra vez, rst:0xc (SW_CPU)");
        const sc_time t1 = sc_time_stamp();
        while (cuenta(terminal, "Hello world!") < 2 && sc_time_stamp() - t1 < sc_time(1, SC_SEC)) wait(1, SC_MS);
        check(cuenta(terminal, "Hello world!") >= 2, "y vuelve a saludar");
        check_eq(c6.lp_clkrst.causa(), 12u, "LP_CLKRST_RESET_CAUSE: 12");
        check_eq(c6.fallos_bus() - fb0, 0u, "sin un solo error de bus (los dos de D4 son los de la PMA)");
    }

    void d8() {
        grupo("D8 el modo descarga y el C6FH4 (la Flash dentro)");
        check(arranca_flash("", "", true), "GPIO9 a masa y GPIO8 arriba al encender");
        check(espera_texto(terminal, "waiting for download", sc_time(50, SC_MS)), "\"waiting for download\"");
        check(tiene(terminal, "boot:0x4 (DOWNLOAD(USB/UART0/SDIO_FEI_FEO))"),
              "boot:0x4 (DOWNLOAD(USB/UART0/SDIO_FEI_FEO)): GPIO_STRAPPING con MTMS y MTDI sueltos");
        const std::string antes = terminal;
        wait(20, SC_MS);
        check(terminal == antes, "y espera: nadie la resetea");
        c6.alimenta(false);
        c6.gpio.nodo_de(9).set_hiz(d_g9_);
        c6.gpio.nodo_de(8).set_hiz(d_g8_);
        check(c6b.flash_interna() && c6b.flash_interna()->tam() == (4u << 20), "el C6FH4 trae 4 MB dentro");
        check(carga_flash_args(c6b, "build/idf-c6/v5.5/hello_world"), "hello_world en su Flash interna");
        c6b.uart0.salida.clear();
        c6b.alimenta(true);
        check(espera_texto(c6b.uart0.salida, "Restarting in 10 seconds...", sc_time(1, SC_SEC)), "llega a \"Restarting in 10 seconds...\"");
        check(tiene(c6b.uart0.salida, "silicon revision v0.2"), "\"silicon revision v0.2\"");
        check_eq((c6b.efuse.rd(0x50) >> 24) & 7u, 1u, "PKG_VERSION 1 (QFN32) y FLASH_CAP 1 (4 MB) en el eFuse");
        c6b.alimenta(false);
    }

    void d9() {
        grupo("D9 los perifericos comunes con el C3, y TWAI1, con ESP-IDF (perifericos)");
        banco.conecta(true);
        const uint64_t a0 = c6.aes.bloques_dma(), s0 = c6.sha.bloques_dma(), t0 = c6.spi2.transacciones();
        banco.sube2.clear(); banco.baja2.clear(); banco.rmt.clear();
        check(arranca_flash("build/idf-c6/v5.5/perifericos"), "perifericos: su flash_args");
        check(espera_texto(terminal, "perifericos: FIN", sc_time(3, SC_SEC)), "llega a \"perifericos: FIN\"");
        // LEDC: 5 kHz y el 25 % antes del fundido
        bool ledc_ok = banco.sube2.size() > 20 && banco.baja2.size() > 20;
        double per = 0, alto = 0;
        if (ledc_ok) {
            per = banco.sube2[11] - banco.sube2[10];
            for (double b : banco.baja2) if (b > banco.sube2[10]) { alto = b - banco.sube2[10]; break; }
        }
        check(ledc_ok && std::fabs(per - 200e-6) < 0.5e-6, "LEDC en GPIO2: 5000 Hz (" + std::to_string(per * 1e6) + " us)");
        check(ledc_ok && std::fabs(alto / per - 0.25) < 0.005, "y el 25 % (" + std::to_string(alto / per) + ")");
        check(tiene(terminal, "P2 LEDC fundido hasta 6144"), "el fundido de ESP-IDF por la RAM gamma llega a 6144");
        // RMT: 10/20, 30/40, 50/60, 70 us
        static const double RMT_US[7] = {10, 20, 30, 40, 50, 60, 70};
        bool rmt_ok = banco.rmt.size() >= 7;
        for (unsigned i = 0; rmt_ok && i < 7; ++i)
            rmt_ok = banco.rmt[i].first == (i % 2 == 0) && std::fabs(banco.rmt[i].second * 1e6 - RMT_US[i]) < 0.1;
        check(rmt_ok, "RMT en GPIO3: los cuatro pulsos de 10/20, 30/40, 50/60 y 70 us");
        check(tiene(terminal, "P4 TWAI0 -> TWAI1: OK id 0x123 dlc 8 10..17") &&
              tiene(terminal, "P4 TWAI1 -> TWAI0: OK id 0x456 dlc 8 20..27"),
              "TWAI0 y TWAI1 a 500 kbit/s por un bus CAN: una trama en cada sentido");
        check(tiene(terminal, "P5 SPI OK 64 bytes por la GDMA") && c6.spi2.transacciones() > t0,
              "GP-SPI2 a 1 MHz con la GDMA en los dos sentidos (MOSI -> MISO)");
        check(tiene(terminal, "P6 I2S OK"), "I2S Philips de 16 bits con la GDMA: lo que sale por GPIO10 vuelve por GPIO11");
        check(tiene(terminal, "P7 I2C 0x50: ESP_ERR_NOT_FOUND"), "I2C: nadie en 0x50, NACK");
        check(tiene(terminal, "P8 ADC1 canal 0: 1966"), "SAR ADC: 1,2 V en GPIO0 a 12 dB: 1966");
        check(tiene(terminal, "P9 temperatura 25 C"), "el sensor de temperatura: 25 C");
        check(tiene(terminal, "P10 AES (ECB, CBC, CTR; 128 y 256) OK") && c6.aes.bloques_dma() > a0,
              "AES en modo DMA (los vectores de SP 800-38A): " + std::to_string(c6.aes.bloques_dma() - a0) + " bloques");
        check(tiene(terminal, "P11 SHA-1 OK") && tiene(terminal, "P12 SHA-256 OK") && c6.sha.bloques_dma() > s0,
              "SHA-1 y SHA-256 en modo DMA: " + std::to_string(c6.sha.bloques_dma() - s0) + " bloques");
        check(tiene(terminal, "P13 RSA (exp_mod) OK"), "RSA: la exponenciacion modular de mbedTLS");
        struct F { const char* n; uint32_t v; };
        static const F FECHAS[] = {{"RMT", 0x2108213}, {"I2C", 0x2201172}, {"UHCI", 0x2201100}, {"LEDC", 0x2111150},
                                   {"I2S", 0x2201070}, {"SARADC", 0x2206240}, {"USJ", 0x2109220}, {"GDMA", 0x2202250},
                                   {"SPI2", 0x2201300}, {"AES", 0x20191210}, {"SHA", 0x20201229}, {"RSA", 0x20200618},
                                   {"DS", 0x20200618}, {"HMAC", 0x20200618}, {"XTS", 0x20200623}};
        bool fechas = true;
        for (const F& f : FECHAS) {
            char l[48];
            std::snprintf(l, sizeof l, "P14 %s 0x%08X", f.n, f.v);
            if (!tiene(terminal, l)) { fechas = false; std::printf("    falta \"%s\"\n", l); }
        }
        check(fechas, "el DATE de los quince bloques, en su sitio y con su valor (el XTS_AES, dentro del SPI1)");
        c6.alimenta(false);
        banco.conecta(false);
    }

    void run() {
        wait(SC_ZERO_TIME);
        d1();
        d2();
        d3();
        d4();
        d5();
        d6();
        d7();
        d8();
        d9();
        std::printf("\nTOTAL C6: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

// --- Arrancar unas imagenes de la Flash, fuera de la suite ---------------------
//   testc6 --flash [--fh4] [--ms N] [--descarga] 0x0 bootloader.bin 0x8000 ...
SC_MODULE(CorreFlash) {
    std::unique_ptr<SpiFlashChip> fl;
    std::unique_ptr<Esp32C6> c6;
    std::vector<std::pair<std::string, uint32_t>> imgs;
    double ms = 100;
    bool descarga = false;
    int res = 0;
    std::unique_ptr<BancoPerifC6> banco;
    CorreFlash(sc_module_name nm, const McuCapsC6& m, bool con_banco) : sc_module(nm) {
        c6.reset(new Esp32C6("c6", m));
        if (con_banco) banco.reset(new BancoPerifC6("banco", *c6));
        fl.reset(new SpiFlashChip(*busca_modelo_flash("GD25Q32")));
        if (!c6->flash()) c6->pon_flash(fl.get());
        SC_HAS_PROCESS(CorreFlash);
        SC_THREAD(run);
    }
    void run() {
        for (auto& i : imgs) {
            const std::string e = c6->carga_imagen(i.first, i.second);
            if (!e.empty()) { std::printf("%s\n", e.c_str()); res = 2; sc_stop(); return; }
        }
        if (imgs.empty()) c6->arranca_por_rom();
        uint64_t fallos = 0;
        c6->al_fallar = [&](uint32_t a, unsigned n, bool w) {
            if (fallos++ < 20) std::fprintf(stderr, "[bus] error %s 0x%08X (%u) pc=0x%08X\n", w ? "W" : "R", a, n,
                                            c6->core.hart.pc());
        };
        if (descarga) {
            const int d = c6->gpio.nodo_de(9).register_driver("boton_boot");
            c6->gpio.nodo_de(9).set_drive(d, 0.0f, 100.0f);
            const int d8 = c6->gpio.nodo_de(8).register_driver("pullup_g8");
            c6->gpio.nodo_de(8).set_drive(d8, 3.3f, 10e3f);
        }
        c6->alimenta(true);
        size_t visto = 0;
        const sc_time fin = sc_time_stamp() + sc_time(ms, SC_MS);
        int traza = std::getenv("C6_TRAZA") ? std::atoi(std::getenv("C6_TRAZA")) : 0;
        if (traza) c6->core.hart.traza = [traza](const RvCommit& c) mutable {
            if (traza-- > 0) std::fprintf(stderr, "[traza] %08x %08x\n", c.pc, c.insn);
        };
        while (sc_time_stamp() < fin) {
            wait(100, SC_US);
            const std::string& o = c6->uart0.salida;
            if (o.size() > visto) { std::fwrite(o.data() + visto, 1, o.size() - visto, stdout); std::fflush(stdout); visto = o.size(); }
        }
        if (std::getenv("C6_DBG")) {
            uint32_t pc = 0;
            c6->core.hart.csr_lee(0x7E2, pc);
            std::printf("\n[dbg] ciclos %llu mpccr 0x%X s0 0x%X s1 0x%X", (unsigned long long)c6->core.hart.ciclos(), pc,
                        c6->core.hart.x(8), c6->core.hart.x(9));
        }
        if (std::getenv("C6_DBG"))
            std::printf("\n[dbg] spi2 %llu trans, aes %u bloques dma, sha %u bloques dma, intmtx 0x%08X",
                        (unsigned long long)c6->spi2.transacciones(), c6->aes.bloques_dma(), c6->sha.bloques_dma(),
                        c6->intpri.eip(0));
        if (std::getenv("C6_DBG"))
            std::printf("\n[dbg] resets %llu causa %u uart0 %.0f baudios cpu %.0f apb %.0f strap 0x%X reclamaciones %llu",
                        (unsigned long long)c6->resets(), c6->lp_clkrst.causa(), c6->uart0.baudios(),
                        c6->pcr.hz_cpu(), c6->pcr.hz_apb(), c6->gpio.strap(),
                        (unsigned long long)c6->intpri.reclamaciones());
        std::printf("\n[fin] %.1f ms, pc=0x%08X, %llu instrucciones, %llu errores de bus, cache %llu/%llu\n", ms,
                    c6->core.hart.pc(), (unsigned long long)c6->core.hart.instret(), (unsigned long long)fallos,
                    (unsigned long long)c6->cache.aciertos(), (unsigned long long)c6->cache.fallos());
        sc_stop();
    }
};
static int corre_flash(int argc, char* argv[]) {
    const McuCapsC6* m = &MCU_ESP32C6;
    double ms = 100;
    bool descarga = false, con_banco = false;
    std::vector<std::pair<std::string, uint32_t>> imgs;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fh4") m = &MCU_ESP32C6FH4;
        else if (a == "--fh8") m = &MCU_ESP32C6FH8;
        else if (a == "--ms" && i + 1 < argc) ms = std::atof(argv[++i]);
        else if (a == "--descarga") descarga = true;
        else if (a == "--banco") con_banco = true;
        else if (i + 1 < argc) { imgs.push_back({argv[i + 1], uint32_t(std::strtoul(argv[i], nullptr, 0))}); ++i; }
    }
    CorreFlash c("corre", *m, con_banco);
    c.imgs = imgs;
    c.ms = ms;
    c.descarga = descarga;
    sc_start();
    return c.res;
}

int sc_main(int argc, char* argv[]) {
    sc_report_handler::set_actions(SC_WARNING, std::getenv("TESTC6_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    if (argc > 1 && std::strcmp(argv[1], "--flash") == 0) return corre_flash(argc, argv);
    TbC6 tb("tb");
    sc_start();
    const unsigned long long ps =
        (unsigned long long)(sc_time_stamp().value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    std::printf("\nTiempo simulado: %llu ps\n", ps);
    return g_fallos ? 1 : 0;
}
