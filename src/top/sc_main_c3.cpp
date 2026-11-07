// =============================================================================
// sc_main_c3.cpp — LA SUITE DEL ESP32-C3 (fases E3 y E4 de
//                  doc/analisis_esp32_riscv.md)
//
// C1-C7 (E3): el firmware de verif/fw/esp32c3 se carga en la SRAM y el
// núcleo arranca en su entrada, sin pasar por la ROM. C8-C13 (E4): el chip
// arranca por la ROM de verdad y lee la Flash, como en la placa. Cada grupo
// arranca el chip con CHIP_PU.
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
//   C8   Direct Boot (0xAEDB041D): la ROM proyecta la Flash y salta a
//        0x4200_0008; el firmware corre por la caché, lee sus constantes por
//        el bus D y saluda por GPIO21
//   C9   ESP-IDF v5.5, hello_world, en un C3 v0.4 con la Flash en la placa:
//        la ROM (api1), el bootloader, la tabla de particiones, la app, y
//        esp_restart() a los diez segundos (rst:0xc, otra vez la ROM)
//   C10  ESP-IDF v6.0, hello_world, en un C3FH4X v1.1 (la Flash dentro):
//        la ROM eco7 y «silicon revision v1.1»
//   C11  ESP-IDF: blink (v6.0, GPIO8 cada 200 ms en el nodo) y uart_echo
//        (v5.5, la UART1 por GPIO4/GPIO5 con una UART de bancada)
//   C12  el modo descarga (GPIO9 a masa, GPIO8 arriba): la ROM espera y
//        nadie la resetea; con la Flash vacía, «invalid header» una y otra
//        vez, y los perros del flashboot: el MWDT0 resetea el sistema
//        (rst:0x7) y el RWDT, el RTC (rst:0x10)
//   C13  la ROM de otra revisión: el aviso al montar
// Los binarios de ESP-IDF: verif/fw/esp32c3/idf/idf-v*.tar.xz, que `make
// testc3` extrae en build/idf.
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
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
    // E4: la Flash de la placa de c3, y un C3FH4X (v1.1, la Flash dentro)
    SpiFlashChip flash{*busca_modelo_flash("GD25Q32")};
    Esp32C3 c3b{"c3b", MCU_ESP32C3FH4X};
    std::string term_b;          // lo que sale por GPIO4 de c3b (UART1 de uart_echo)
    int d_g8_ = -1, d_b5_ = -1;
    // Una UART de bancada en GPIO21 (lee) y GPIO20 (escribe)
    int d_rx_ = -1, d_g9_ = -1;
    std::string terminal;
    unsigned flancos_g8 = 0;

    TbC3(sc_module_name nm) : sc_module(nm), c3("c3", MCU_ESP32C3, cables.cab) {
        SC_HAS_PROCESS(TbC3);
        c3.pon_flash(&flash);
        SC_THREAD(run);
        SC_THREAD(terminal_proc);
        sc_spawn(sc_bind(&TbC3::terminal_nodo, this, &c3b.gpio.nodo_de(4), &term_b));
        SC_METHOD(cuenta_g8);
        sensitive << c3.gpio.nodo_de(8).value_changed_event();
        dont_initialize();
    }
    void end_of_elaboration() override {
        d_rx_ = c3.gpio.nodo_de(20).register_driver("banco_tx");
        c3.gpio.nodo_de(20).set_drive(d_rx_, 3.3f, 100.0f);     // línea en reposo
        d_g9_ = c3.gpio.nodo_de(9).register_driver("banco_g9");
        c3.gpio.nodo_de(9).set_hiz(d_g9_);
        d_g8_ = c3.gpio.nodo_de(8).register_driver("banco_g8");
        c3.gpio.nodo_de(8).set_hiz(d_g8_);
        d_b5_ = c3b.gpio.nodo_de(5).register_driver("banco_b_tx");
        c3b.gpio.nodo_de(5).set_drive(d_b5_, 3.3f, 100.0f);
    }
    // Una UART de bancada (115200 8N1) leyendo un nodo cualquiera
    void terminal_nodo(analog_net_if* n, std::string* t) {
        const sc_time tb(1.0 / BAUD, SC_SEC);
        for (;;) {
            wait(n->value_changed_event());
            if (n->voltage() > 1.65f) continue;
            wait(tb * 1.5);
            unsigned d = 0;
            for (unsigned i = 0; i < 8; ++i) { if (n->voltage() > 1.65f) d |= 1u << i; wait(tb); }
            if (n->voltage() > 1.65f) t->push_back(char(d));
        }
    }
    void envia_nodo(analog_net_if& n, int drv, const std::string& s) {
        const sc_time tb(1.0 / BAUD, SC_SEC);
        for (char ch : s) {
            n.set_drive(drv, 0.0f, 100.0f); wait(tb);
            for (unsigned i = 0; i < 8; ++i) { n.set_drive(drv, ((ch >> i) & 1) ? 3.3f : 0.0f, 100.0f); wait(tb); }
            n.set_drive(drv, 3.3f, 100.0f); wait(tb * 2.0);
        }
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
        check(c3.limitaciones().size() >= 5, "limitaciones(): lo que no esta (perifericos de E7-E8, SWD, brownout...)");
        const huella_fw::Veredicto hv = huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "c3");
        std::printf("    %d imagenes: %s\n", hv.comprobadas, hv.detalle.c_str());
        check(hv.ok() && hv.comprobadas == 7, "los siete ficheros de la suite son los versionados (verif/fw/huellas.txt)");
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


    // =========================================================================
    // E4: por la ROM y la Flash
    // =========================================================================
    // Un flash_args de ESP-IDF en la Flash de `chip`
    static bool carga_flash_args(Esp32C3& chip, const std::string& dir) {
        std::FILE* f = std::fopen((dir + "/flash_args").c_str(), "r");
        if (!f) { std::printf("  (no hay %s/flash_args: make testc3 extrae build/idf)\n", dir.c_str()); return false; }
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
    // Apaga c3, vacía su Flash y la carga; GPIO9 y GPIO8 como se pida
    bool arranca_flash(const std::string& fa_dir, const std::string& bin = "", bool descarga = false) {
        c3.alimenta(false);
        wait(10, SC_US);
        flash.borra_todo();
        bool ok = true;
        if (!fa_dir.empty()) ok = carga_flash_args(c3, fa_dir);
        else if (!bin.empty()) { const std::string e = c3.carga_imagen(bin, 0); ok = e.empty(); if (!ok) std::printf("  (%s)\n", e.c_str()); }
        else c3.arranca_por_rom();
        if (descarga) {
            c3.gpio.nodo_de(9).set_drive(d_g9_, 0.0f, 100.0f);
            c3.gpio.nodo_de(8).set_drive(d_g8_, 3.3f, 10e3f);
        } else {
            c3.gpio.nodo_de(9).set_hiz(d_g9_);
            c3.gpio.nodo_de(8).set_hiz(d_g8_);
        }
        terminal.clear();
        c3.uart0.salida.clear();
        c3.alimenta(true);
        return ok;
    }
    // Espera hasta que `t` contenga `que` (o se acabe el tiempo)
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

    void c8() {
        grupo("C8 Direct Boot: el firmware corre desde la Flash, por la cache");
        const std::string elf = std::string(FW) + "direct_boot.elf";
        check(arranca_flash("", std::string(FW) + "direct_boot.bin"), "direct_boot.bin en la Flash, desde 0");
        buzon_ = 0;
        simbolo_elf(elf, "buzon", buzon_);
        c3.poke32(buzon_ + 4, 0);
        const uint64_t f0 = c3.cache.fallos(), a0 = c3.cache.aciertos();
        check(espera_texto(terminal, "Direct Boot: desde la Flash, por la cache\r\n", sc_time(60, SC_MS)),
              "el saludo llega por GPIO21: \"Direct Boot: desde la Flash, por la cache\"");
        check(tiene(terminal, "ESP-ROM:esp32c3-api1-20210207") && tiene(terminal, "(SPI_FAST_FLASH_BOOT)"),
              "antes, la ROM de la rev. 3 (api1) y el arranque desde la Flash");
        check(espera_fin(sc_time(10, SC_MS)), "y el buzon dice que ha acabado");
        check(bz(0) >= 0x42000000u && bz(0) < 0x42800000u, "main() ejecuta en la ventana I de la Flash (0x" +
              [&] { char b[16]; std::snprintf(b, sizeof b, "%08X", bz(0)); return std::string(b); }() + ")");
        check(bz(1) >= 0x3C000000u && bz(1) < 0x3C800000u, "y sus constantes estan en la ventana D");
        uint32_t x = 0;
        for (uint32_t i = 0; i < 64; ++i) x ^= (0x9E3779B9u * i + 0x7F4A7C15u) + i;
        check_eq(bz(2), x, "la tabla leida por el bus D es la de la imagen");
        check_eq(bz(3), 1u, "RESET_STATE: 1");
        check(c3.cache.fallos() > f0 && c3.cache.aciertos() - a0 > 10 * (c3.cache.fallos() - f0),
              "la cache: " + std::to_string(c3.cache.aciertos() - a0) + " aciertos y " +
              std::to_string(c3.cache.fallos() - f0) + " fallos");
    }

    void c9() {
        grupo("C9 ESP-IDF v5.5 hello_world en un C3 v0.4 (la Flash en la placa)");
        check(arranca_flash("build/idf/v5.5/hello_world"), "su flash_args: bootloader, particiones y app");
        check(espera_texto(terminal, "Restarting in 10 seconds...", sc_time(1, SC_SEC)), "llega a \"Restarting in 10 seconds...\"");
        check(tiene(terminal, "ESP-ROM:esp32c3-api1-20210207") && tiene(terminal, "rst:0x1 (POWERON),boot:0x") &&
              tiene(terminal, "(SPI_FAST_FLASH_BOOT)"),
              "la ROM (api1): encendido y arranque desde la Flash");
        check(tiene(terminal, "load:0x3fcd5820") && tiene(terminal, "entry 0x403cbf1a"),
              "la ROM carga el bootloader de la Flash (comprobando su SHA-256)");
        check(tiene(terminal, "ESP-IDF v5.5.6 2nd stage bootloader") && tiene(terminal, "boot: chip revision: v0.4"),
              "el bootloader v5.5.6 lee la revision v0.4 del eFuse");
        check(tiene(terminal, "factory app      00 00 00010000 00100000"), "la tabla de particiones");
        check(tiene(terminal, "spi_flash: detected chip: gd"), "la app reconoce la GD25Q32 por su JEDEC ID");
        check(tiene(terminal, "Hello world!\r\n") || tiene(terminal, "Hello world!\n"), "Hello world!");
        check(tiene(terminal, "silicon revision v0.4, 2MB external flash"), "\"silicon revision v0.4, 2MB external flash\"");
        check(c3.uart0.salida == terminal, "lo que lee la UART de bancada en GPIO21 es lo que manda la UART0");
        check(espera_texto(terminal, "rst:0xc (RTC_SW_CPU_RST)", sc_time(11, SC_SEC)),
              "esp_restart() a los diez segundos: la ROM otra vez, rst:0xc (RTC_SW_CPU_RST)");
        const sc_time t1 = sc_time_stamp();
        while (cuenta(terminal, "Hello world!") < 2 && sc_time_stamp() - t1 < sc_time(1, SC_SEC)) wait(1, SC_MS);
        check(cuenta(terminal, "Hello world!") >= 2, "y vuelve a saludar");
        check_eq(c3.rtc.causa(), 12u, "RESET_STATE: 12");
    }

    void c10() {
        grupo("C10 ESP-IDF v6.0 hello_world en un C3FH4X v1.1 (la Flash dentro)");
        check(c3b.aviso_rom().empty() && c3b.rom.revision() == 101, "la ROM por omision del v1.1 es la rev. 101 (eco7)");
        c3b.alimenta(false);
        wait(10, SC_US);
        c3b.flash_interna()->borra_todo();
        check(carga_flash_args(c3b, "build/idf/v6.0/hello_world"), "su flash_args en la Flash interna (4 MB)");
        c3b.uart0.salida.clear();
        c3b.alimenta(true);
        check(espera_texto(c3b.uart0.salida, "Restarting in 10 seconds...", sc_time(1, SC_SEC)), "llega a \"Restarting in 10 seconds...\"");
        const std::string& o = c3b.uart0.salida;
        check(tiene(o, "ESP-ROM:esp32c3-eco7-20230720"), "la ROM eco7 (20230720)");
        check(tiene(o, "ESP-IDF v6.0.3 2nd stage bootloader") && tiene(o, "boot: chip revision: v1.1"),
              "el bootloader v6.0.3 y la revision v1.1");
        check(tiene(o, "silicon revision v1.1"), "\"silicon revision v1.1\"");
        c3b.alimenta(false);
    }

    void c11() {
        grupo("C11 ESP-IDF: blink (v6.0) y uart_echo (v5.5)");
        check(arranca_flash("build/idf/v6.0/blink"), "blink de v6.0 en c3");
        check(espera_texto(terminal, "Turning the LED ON!", sc_time(1, SC_SEC)), "\"Turning the LED ON!\"");
        const unsigned f0 = flancos_g8;
        const sc_time t0 = sc_time_stamp();
        wait(1, SC_SEC);
        const unsigned n = flancos_g8 - f0;
        check(n >= 4 && n <= 6, "GPIO8 cambia en el nodo cada 200 ms: " + std::to_string(n) + " cambios en " +
              std::to_string((sc_time_stamp() - t0).to_seconds()) + " s");
        check(cuenta(terminal, "Turning the LED OFF!") >= 2, "y lo dice por la UART0");
        c3.alimenta(false);
        // uart_echo en c3b: la UART1 por GPIO4 (TX) y GPIO5 (RX)
        c3b.alimenta(false);
        wait(10, SC_US);
        c3b.flash_interna()->borra_todo();
        check(carga_flash_args(c3b, "build/idf/v5.5/uart_echo"), "uart_echo de v5.5 en c3b");
        c3b.uart0.salida.clear();
        term_b.clear();
        c3b.alimenta(true);
        check(espera_texto(c3b.uart0.salida, "main_task: Returned from app_main()", sc_time(1, SC_SEC)) ||
              espera_texto(c3b.uart0.salida, "Calling app_main()", sc_time(1, SC_SEC)), "arranca");
        wait(50, SC_MS);
        envia_nodo(c3b.gpio.nodo_de(5), d_b5_, "Hola, E4");
        wait(20, SC_MS);
        check(term_b == "Hola, E4", "lo que entra por GPIO5 vuelve por GPIO4: \"" + term_b + "\"");
        c3b.alimenta(false);
    }

    void c12() {
        grupo("C12 el modo descarga, la Flash vacia y el RWDT");
        check(arranca_flash("build/idf/v5.5/hello_world", "", true), "GPIO9 a masa y GPIO8 arriba al subir CHIP_PU");
        check(espera_texto(terminal, "waiting for download", sc_time(100, SC_MS)), "\"waiting for download\"");
        check(tiene(terminal, "boot:0x4 (DOWNLOAD(USB/UART0/1))"), "boot:0x4 (DOWNLOAD(USB/UART0/1))");
        check_eq(c3.gpio.strap() & 0xCu, 0x4u, "GPIO_STRAP: GPIO9 a 0, GPIO8 a 1");
        const uint64_t r0 = c3.resets();
        wait(2, SC_SEC);
        check(c3.resets() == r0 && !tiene(terminal, "2nd stage"),
              "dos segundos esperando sin reset: en descarga, el flashboot de los WDT no cuenta");
        check(arranca_flash(""), "la Flash vacia (0xFF) y GPIO9 suelto");
        check(espera_texto(terminal, "invalid header: 0xffffffff", sc_time(100, SC_MS)), "\"invalid header: 0xffffffff\"");
        const bool rw = espera_texto(terminal, "rst:0x10 (RTCWDT_RTC_RST)", sc_time(2, SC_SEC));
        check(tiene(terminal, "rst:0x7 (TG0WDT_SYS_RST)"),
              "antes, el MWDT0 en flashboot resetea el sistema (rst:0x7) a los 26e6 ciclos del APB");
        check(rw,
              "y el RWDT en flashboot, que no para con el reset del sistema, resetea el RTC (rst:0x10)");
        check(c3.rtc.disparos_rwdt() >= 1 && c3.rtc.causa() == 16, "RESET_STATE: 16");
        c3.alimenta(false);
    }

    void c13() {
        grupo("C13 la ROM de otra revision");
        check(c3.aviso_rom().empty(), "C3 v0.4 con la ROM rev. 3: sin aviso");
        check(c3.pon_rom("rev101").empty() && !c3.aviso_rom().empty(), "con la rev. 101: \"" + c3.aviso_rom().substr(0, 50) + "...\"");
        check(c3.pon_rom("ninguna").empty() && !c3.rom.hay(), "rom=ninguna (R3): sin ROM");
        check(!c3.carga_imagen(std::string(FW) + "direct_boot.bin", 0).empty(), "y sin ROM no se arranca desde la Flash");
        check(c3.pon_rom("omision").empty() && c3.rom.revision() == 3, "rom=omision: la de la revision del chip");
    }

    void run() {
        c1();
        c2();
        c3_uart();
        c4();
        c5();
        c6();
        c7();
        c8();
        c9();
        c10();
        c11();
        c12();
        c13();
        std::printf("\nTOTAL C3: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

// testc3 --flash img.bin[@0x0] [...] [--v1.1] [--ms N] [--descarga]: no es la
// suite (--descarga: GPIO9 a masa al arrancar). El C3
// con la ROM y la Flash cargada, durante N ms simulados (100 por omisión), con
// la UART0 a la salida estándar y los errores de bus contados. Para
// desarrollar y para ver arrancar una imagen de ESP-IDF.
SC_MODULE(CorreFlash) {
    std::unique_ptr<SpiFlashChip> fl;
    std::unique_ptr<Esp32C3> c3;
    std::vector<std::pair<std::string, uint32_t>> imgs;
    double ms = 100;
    bool descarga = false;
    int res = 0;
    CorreFlash(sc_module_name nm, const McuCapsC3& m) : sc_module(nm) {
        c3.reset(new Esp32C3("c3", m));
        fl.reset(new SpiFlashChip(*busca_modelo_flash("GD25Q32")));
        if (!c3->flash()) c3->pon_flash(fl.get());
        SC_HAS_PROCESS(CorreFlash);
        SC_THREAD(run);
    }
    void run() {
        for (auto& i : imgs) {
            const std::string e = c3->carga_imagen(i.first, i.second);
            if (!e.empty()) { std::printf("%s\n", e.c_str()); res = 2; sc_stop(); return; }
        }
        uint64_t fallos = 0;
        c3->al_fallar = [&](uint32_t a, unsigned n, bool w) {
            if (fallos++ < 20) std::fprintf(stderr, "[bus] error %s 0x%08X (%u) pc=0x%08X\n", w ? "W" : "R", a, n,
                                            c3->core.hart.pc());
        };
        if (descarga) {
            // GPIO9 a masa (el botón BOOT) y GPIO8 arriba, como en las placas
            const int d = c3->gpio.nodo_de(9).register_driver("boton_boot");
            c3->gpio.nodo_de(9).set_drive(d, 0.0f, 100.0f);
            const int d8 = c3->gpio.nodo_de(8).register_driver("pullup_g8");
            c3->gpio.nodo_de(8).set_drive(d8, 3.3f, 10e3f);
        }
        c3->alimenta(true);
        size_t visto = 0;
        const sc_time fin = sc_time_stamp() + sc_time(ms, SC_MS);
        int traza = std::getenv("C3_TRAZA") ? std::atoi(std::getenv("C3_TRAZA")) : 0;
        bool puesta = false;
        while (sc_time_stamp() < fin) {
            wait(100, SC_US);
            if (traza && !puesta && c3->resets() >= 2) {
                puesta = true;
                std::fprintf(stderr, "[traza] reset en %s\n", sc_time_stamp().to_string().c_str());
                c3->core.hart.traza = [traza](const RvCommit& c) mutable {
                    if (traza-- > 0) std::fprintf(stderr, "[traza] %08x %08x\n", c.pc, c.insn);
                };
            }
            const std::string& o = c3->uart0.salida;
            if (o.size() > visto && std::getenv("C3_DBG_T")) std::fprintf(stderr, "[t] %s %zu\n", sc_time_stamp().to_string().c_str(), o.size());
            if (o.size() > visto) { std::fwrite(o.data() + visto, 1, o.size() - visto, stdout); std::fflush(stdout); visto = o.size(); }
        }
        if (std::getenv("C3_DBG"))
            std::printf("\n[dbg] resets %llu causa %u uart0 %.0f baudios (clkdiv 0x%X clk_conf 0x%X) cpu %.0f apb %.0f",
                        (unsigned long long)c3->resets(), c3->rtc.causa(), c3->uart0.baudios(),
                        c3->uart0.dbg_read(0x14), c3->uart0.dbg_read(0x78), c3->system.hz_cpu(), c3->system.hz_apb());
            std::printf("\n[dbg] systimer alarmas %llu %llu %llu raw 0x%X conf 0x%X; intmtx reclamaciones %llu\n",
                        (unsigned long long)c3->systimer.alarmas(0), (unsigned long long)c3->systimer.alarmas(1),
                        (unsigned long long)c3->systimer.alarmas(2), c3->systimer.int_raw(),
                        c3->systimer.dbg_read(0), (unsigned long long)c3->intmtx.reclamaciones());
        std::printf("\n[fin] %.1f ms, pc=0x%08X, %llu instrucciones, %llu errores de bus, cache %llu/%llu\n", ms,
                    c3->core.hart.pc(), (unsigned long long)c3->core.hart.instret(), (unsigned long long)fallos,
                    (unsigned long long)c3->cache.aciertos(), (unsigned long long)c3->cache.fallos());
        sc_stop();
    }
};

static int corre_flash(int argc, char* argv[]) {
    const McuCapsC3* m = &MCU_ESP32C3;
    std::vector<std::pair<std::string, uint32_t>> imgs;
    double ms = 100;
    bool descarga = false;
    std::string rom;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--flash" && i + 1 < argc) {
            std::string f = argv[++i];
            uint32_t off = 0;
            const size_t at = f.rfind('@');
            if (at != std::string::npos) { off = uint32_t(std::stoul(f.substr(at + 1), nullptr, 0)); f = f.substr(0, at); }
            imgs.emplace_back(f, off);
        } else if (a == "--v1.1") m = &MCU_ESP32C3FH4X;
        else if (a == "--ms" && i + 1 < argc) ms = std::atof(argv[++i]);
        else if (a == "--descarga") descarga = true;
        else if (a == "--rom" && i + 1 < argc) rom = argv[++i];
    }
    CorreFlash c("corre", *m);
    c.imgs = imgs;
    c.ms = ms;
    c.descarga = descarga;
    if (!rom.empty()) {
        const std::string e = c.c3->pon_rom(rom);
        if (!e.empty()) { std::printf("%s\n", e.c_str()); return 2; }
        if (!c.c3->aviso_rom().empty()) std::printf("[aviso] %s\n", c.c3->aviso_rom().c_str());
    }
    sc_start();
    return c.res;
}

int sc_main(int argc, char* argv[]) {
    sc_report_handler::set_actions(SC_WARNING, std::getenv("TESTC3_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    if (argc > 1 && std::strcmp(argv[1], "--flash") == 0) return corre_flash(argc, argv);
    TbC3 tb("tb");
    sc_start();
    auto ps = [](const sc_time& t) {
        return (unsigned long long)(t.value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    };
    std::printf("\nTiempo simulado: %llu ps\n", ps(sc_time_stamp()));
    return g_fallos ? 1 : 0;
}
