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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "common/asan_opciones.h"
#include "soc/esp32c3/esp32c3.h"
#include "common/huella_fw.h"
#include "rv/gdb/gdb_stub_rv.h"
#include "verif/sonda_jtag_rv.h"
#include "verif/gdb_client.h"
#include "parts/puente_usb.h"
#include "parts/ws2812.h"
#include "parts/ext_parts.h"
#include "parts/tft_st7735.h"
#include "verif/fw/esp32c3/hmac_vec.h"   // E8: las claves y los resultados del HMAC (C25)
#include "verif/fw/esp32c3/ds_vec.h"     // E8: la clave cifrada del DS (C26)
#include "verif/fw/esp32c3/xts_vec.h"    // E8: la clave y el texto cifrado del XTS_AES (C27)

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
    // E6: GDB contra c3b (sin los cables del banco en GPIO4-7, que son el
    // JTAG): G1 con el DM en directo y G2' por los pines con la sonda de bancada
    GdbStubRv g1{"g1", *c3b.dm, puerto_gdb(0), "gdb-g1"};
    JtagSonda sonda{JtagSonda::Pines{&c3b.gpio.nodo_de(6), &c3b.gpio.nodo_de(4), &c3b.gpio.nodo_de(5),
                                     &c3b.gpio.nodo_de(7), nullptr}, 10e6};
    DmiPorJtag dmi_pines{sonda};
    GdbStubRv g2p{"g2p", dmi_pines, puerto_gdb(1), "gdb-g2p"};
    GdbClient cli;
    // E8: dos WS2812 en GPIO8 de c3 (el LED de la DevKitM-1, y uno más)
    Ws2812 tira{"tira", c3.gpio.nodo_de(8), 2, 3.3};
    int d_g0_ = -1;
    // E8: una 24Cxx (0x50) en SDA = GPIO1 y SCL = GPIO10 de c3, con 4,7 kohm
    // E8: un ST7735 en c3b (SCLK GPIO6, MOSI GPIO7, CS GPIO10, D/C GPIO3)
    AnalogNet tft_vcc{"tft_vcc"}, tft_gnd{"tft_gnd"}, tft_rst{"tft_rst"}, tft_luz{"tft_luz"};
    Tft128x160 tft{"tft", tft_vcc, tft_gnd, c3b.gpio.nodo_de(10), tft_rst, c3b.gpio.nodo_de(3),
                   c3b.gpio.nodo_de(7), c3b.gpio.nodo_de(6), tft_luz};
    // E8: un transceptor CAN en c3b (TX GPIO0, RX GPIO1) y otro nodo en el hilo
    CanWire can_bus{3.3, 1000.0, "can_bus"};
    CanTransceiver xcvr{"xcvr", c3b.gpio.nodo_de(0), c3b.gpio.nodo_de(1), can_bus};
    CanNode nodo_can{"nodo_can", can_bus, 500e3};
    I2cEeprom eeprom{"eeprom", c3.gpio.nodo_de(10), c3.gpio.nodo_de(1), 0x50, 3.3};
    int d_pu_scl_ = c3.gpio.nodo_de(10).register_driver("tb_pu_scl"), d_pu_sda_ = c3.gpio.nodo_de(1).register_driver("tb_pu_sda");
    sc_time t_gdb = SC_ZERO_TIME;
    static unsigned puerto_gdb(unsigned i) {
        const char* e = std::getenv("TESTC3_PUERTO_GDB");
        return (e ? unsigned(std::atoi(e)) : 3471u) + i;
    }
    int d_g8_ = -1, d_b5_ = -1;
    // Una UART de bancada en GPIO21 (lee) y GPIO20 (escribe)
    int d_rx_ = -1, d_g9_ = -1;
    std::string terminal;
    unsigned flancos_g8 = 0;

    TbC3(sc_module_name nm) : sc_module(nm), c3("c3", MCU_ESP32C3, cables.cab) {
        SC_HAS_PROCESS(TbC3);
        c3.pon_flash(&flash);
        for (GdbStubRv* g : {&g1, &g2p}) { g->set_halt_on_attach(true); g->set_enabled(false); }
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
        check(hv.ok() && hv.comprobadas == 17, "los diecisiete ficheros de la suite son los versionados (verif/fw/huellas.txt)");
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
        check(espera_texto(terminal, "rst:0xc (RTC_SW_CPU_RST)", sc_time(15, SC_SEC)),
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
        // (Sin PC en el USB, la consola secundaria de ESP-IDF espera su plazo
        // en cada línea: E7)
        wait(300, SC_MS);
        envia_nodo(c3b.gpio.nodo_de(5), d_b5_, "Hola, E4");
        for (int i = 0; i < 50 && term_b != "Hola, E4"; ++i) wait(10, SC_MS);
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

    // --- E5: el protocolo de descarga de la ROM (SLIP), por la UART0 ---------
    static std::string slip(const std::string& d) {
        std::string o = "\xC0";
        for (char ch : d) {
            if (ch == '\xC0') o += "\xDB\xDC";
            else if (ch == '\xDB') o += "\xDB\xDD";
            else o += ch;
        }
        return o + "\xC0";
    }
    static std::string orden(uint8_t op, const std::string& datos, uint32_t chk = 0) {
        std::string o;
        o += char(0); o += char(op);
        o += char(datos.size() & 0xFF); o += char(datos.size() >> 8);
        for (unsigned i = 0; i < 4; ++i) o += char((chk >> (8 * i)) & 0xFF);
        return o + datos;
    }
    static std::string sync_esptool() {
        std::string d = "\x07\x07\x12\x20";
        d += std::string(32, '\x55');
        return slip(orden(0x08, d));
    }
    // Las respuestas SLIP que hay en `t` desde `desde`: cada una, sin escapar
    static std::vector<std::string> respuestas(const std::string& t, size_t desde = 0) {
        std::vector<std::string> r;
        bool dentro = false, esc = false;
        std::string cur;
        for (size_t i = desde; i < t.size(); ++i) {
            const uint8_t b = uint8_t(t[i]);
            if (b == 0xC0) {
                if (dentro && !cur.empty()) r.push_back(cur);
                cur.clear();
                dentro = true;
                continue;
            }
            if (!dentro) continue;
            if (esc) { cur += char(b == 0xDC ? 0xC0 : b == 0xDD ? 0xDB : b); esc = false; }
            else if (b == 0xDB) esc = true;
            else cur += char(b);
        }
        return r;
    }

    void c14() {
        grupo("C14 esptool por la UART0: la deteccion de baudios y el SYNC de la ROM (E5)");
        check(arranca_flash("build/idf/v5.5/hello_world", "", true), "en descarga");
        check(espera_texto(terminal, "waiting for download", sc_time(100, SC_MS)), "\"waiting for download\"");
        const size_t t0 = terminal.size();
        // Como esptool: el primero se lo come la detección de baudios
        unsigned n = 0;
        for (; n < 4 && respuestas(terminal, t0).empty(); ++n) {
            envia(sync_esptool());
            wait(10, SC_MS);
        }
        const std::vector<std::string> r = respuestas(terminal, t0);
        if (std::getenv("C3_DBG")) std::printf("  [c14] %zu bytes, %zu respuestas tras %u SYNC\n", terminal.size() - t0, r.size(), n);
        const uint32_t lo = c3.uart0.dbg_read(0x28) & 0xFFFu, hi = c3.uart0.dbg_read(0x2C) & 0xFFFu;
        const double b = 40e6 / ((lo + hi + 2) / 2.0);
        check(c3.uart0.dbg_read(0x30) > 127, "RXD_CNT: " + std::to_string(c3.uart0.dbg_read(0x30)) + " flancos (> 127)");
        check(b > 114000 && b < 116500, "LOWPULSE " + std::to_string(lo) + " y HIGHPULSE " + std::to_string(hi) +
                                        ": 40 MHz / " + std::to_string((lo + hi + 2) / 2) + " = " + std::to_string(int(b)) + " baudios");
        check(std::abs(c3.uart0.baudios() - 115200.0) < 1000.0, "y la ROM pone el divisor que sale: " + std::to_string(int(c3.uart0.baudios())));
        check(n == 2, "el primer SYNC se lo come la deteccion; el segundo contesta");
        check(!r.empty() && r[0].size() >= 10 && uint8_t(r[0][0]) == 1 && uint8_t(r[0][1]) == 0x08,
              "SYNC (0x08): respuesta 1, orden 0x08");
        // READ_REG (0x0A) de CHIP_DETECT_MAGIC (0x4000_1000): lo primero que
        // lee esptool para saber qué chip es
        const size_t t1 = terminal.size();
        envia(slip(orden(0x0A, std::string("\x00\x10\x00\x40", 4))));
        for (int i = 0; i < 50; ++i) {
            bool hay = false;
            for (const std::string& x : respuestas(terminal, t1)) hay = hay || (x.size() >= 2 && uint8_t(x[1]) == 0x0A);
            if (hay) break;
            wait(1, SC_MS);
        }
        const std::vector<std::string> r2 = respuestas(terminal, t1);
        // (La ROM contesta ocho veces a cada SYNC: lo que llegue antes son restos)
        uint32_t magia = 0;
        bool hay = false;
        for (const std::string& x : r2)
            if (x.size() >= 8 && uint8_t(x[0]) == 1 && uint8_t(x[1]) == 0x0A) {
                hay = true;
                for (unsigned i = 0; i < 4; ++i) magia |= uint32_t(uint8_t(x[4 + i])) << (8 * i);
            }
        char t[64];
        std::snprintf(t, sizeof t, "READ_REG 0x40001000: 0x%08X (ESP32-C3 ECO3)", magia);
        check(hay && magia == 0x1B31506Fu, t);
        c3.alimenta(false);
    }

    // --- E6: GDB contra ESP-IDF, por G1 y por G2' ------------------------------
    static std::string hexs(const std::string& t) {
        std::string r; char b[3];
        for (unsigned char ch : t) { std::snprintf(b, 3, "%02x", ch); r += b; }
        return r;
    }
    static uint32_t de_hex_le(const std::string& t) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4 && 2 * i + 2 <= t.size(); ++i)
            v |= uint32_t(std::stoul(t.substr(2 * i, 2), nullptr, 16)) << (8 * i);
        return v;
    }
    uint32_t reg(unsigned n) { char b[8]; std::snprintf(b, sizeof b, "p%x", n); return de_hex_le(cli.pedir(b)); }
    std::string z(char c, unsigned t, uint32_t a, unsigned n) {
        char b[48]; std::snprintf(b, sizeof b, "%c%u,%x,%u", c, t, unsigned(a), n);
        return cli.pedir(b);
    }
    static uint32_t simbolo(const std::string& nom) {
        std::FILE* f = std::fopen((std::string(FW) + "idf/simbolos-v5.5.txt").c_str(), "r");
        if (!f) return 0;
        char l[256], app[64], n[128];
        unsigned long v = 0, r = 0;
        while (std::fgets(l, sizeof l, f))
            if (l[0] != '#' && std::sscanf(l, "%63s %127s %lx", app, n, &v) == 3 && nom == n) r = v;
        std::fclose(f);
        return uint32_t(r);
    }
    struct ResC3 {
        bool conecta = false, reset = false, flash = false, iram = false, wp = false, paso = false, corre = false;
        uint32_t pc_reset = 0, pc_flash = 0, pc_iram = 0, pc_paso = 0;
    };
    ResC3 sesion_c3(GdbStubRv& st, unsigned puerto) {
        ResC3 r;
        const uint32_t app_main = simbolo("app_main"), tick = simbolo("xTaskIncrementTick"),
                       cuenta = simbolo("xTickCount");
        c3b.alimenta(false);
        wait(10, SC_US);
        if (c3b.flash()) c3b.flash()->borra_todo();
        if (!carga_flash_args(c3b, "build/idf/v5.5/hello_world") || !app_main || !tick || !cuenta) return r;
        c3b.jtag_en_pines(true);
        c3b.alimenta(true);
        wait(2, SC_MS);                       // la ROM, en marcha
        st.set_enabled(true);
        wait(1, SC_MS);
        if (!cli.conectar(puerto)) { st.set_enabled(false); return r; }
        wait(1, SC_MS);
        const std::string sup = cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=riscv");
        r.conecta = sup.find("PacketSize") != std::string::npos && cli.pedir("QStartNoAckMode") == "OK" &&
                    cli.pedir("?").rfind("T0", 0) == 0;
        // monitor reset halt: el reset del sistema por ndmreset, parado en la
        // primera instrucción de la ROM
        r.reset = cli.pedir("qRcmd," + hexs("reset halt"), sc_time(50, SC_MS)) == "OK";
        r.pc_reset = reg(32);
        r.reset = r.reset && r.pc_reset == 0x40000000u;
        // app_main, en la Flash: por trigger
        const bool zh = z('Z', 1, app_main, 2) == "OK";
        cli.enviar("c");
        const std::string ph = cli.recibir(sc_time(1500, SC_MS));
        r.pc_flash = reg(32);
        r.flash = zh && ph.find("hwbreak") != std::string::npos && r.pc_flash == app_main;
        z('z', 1, app_main, 2);
        // xTaskIncrementTick, en la IRAM: por ebreak (lo escribe el stub)
        const bool zb = z('Z', 0, tick, 2) == "OK";
        cli.enviar("c");
        const std::string pb = cli.recibir(sc_time(100, SC_MS));
        r.pc_iram = reg(32);
        r.iram = zb && pb.find("swbreak") != std::string::npos && r.pc_iram == tick;
        if (std::getenv("C3_DBG")) std::printf("  [c15] Z0 %d, parada \"%s\", pc 0x%08X\n", int(zb), pb.c_str(), r.pc_iram);
        z('z', 0, tick, 2);
        // un watchpoint de escritura en xTickCount: para en el sw, antes
        char m[48];
        std::snprintf(m, sizeof m, "m%x,4", unsigned(cuenta));
        const uint32_t antes = de_hex_le(cli.pedir(m));
        const bool zw = z('Z', 2, cuenta, 4) == "OK";
        cli.enviar("c");
        const std::string pw = cli.recibir(sc_time(100, SC_MS));
        char w[32];
        std::snprintf(w, sizeof w, "watch:%08x", unsigned(cuenta));
        const uint32_t pc_wp = reg(32);
        r.wp = zw && pw.find(w) != std::string::npos && de_hex_le(cli.pedir(m)) == antes;
        z('z', 2, cuenta, 4);
        // un paso: el sw se hace
        cli.enviar("s");
        const std::string ps = cli.recibir(sc_time(50, SC_MS));
        r.pc_paso = reg(32);
        r.paso = ps.rfind("T05", 0) == 0 && r.pc_paso != pc_wp && de_hex_le(cli.pedir(m)) == antes + 1;
        // y sigue: hello_world cuenta hacia el reinicio
        cli.enviar("c");
        wait(20, SC_MS);
        cli.crudo("\x03");
        r.corre = cli.recibir(sc_time(50, SC_MS)).rfind("T02", 0) == 0;
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        st.set_enabled(false);
        c3b.alimenta(false);
        c3b.jtag_en_pines(false);
        return r;
    }
    void informa_c3(const char* q, const ResC3& r) {
        const std::string p = std::string(q) + ": ";
        check(r.conecta, p + "GDB se conecta y encuentra el nucleo parado");
        char b[96];
        std::snprintf(b, sizeof b, "monitor reset halt: el reset del sistema, parado en la ROM (pc 0x%08X)", r.pc_reset);
        check(r.reset, p + b);
        std::snprintf(b, sizeof b, "app_main, en la Flash, por trigger (Z1): hwbreak en 0x%08X", r.pc_flash);
        check(r.flash, p + b);
        std::snprintf(b, sizeof b, "xTaskIncrementTick, en la IRAM, por ebreak (Z0): swbreak en 0x%08X", r.pc_iram);
        check(r.iram, p + b);
        check(r.wp, p + "watchpoint de escritura en xTickCount (Z2): para antes del sw");
        check(r.paso, p + "un paso: el sw se hace (xTickCount + 1)");
        check(r.corre, p + "y sigue corriendo hasta Ctrl-C (T02)");
    }
    void c15() {
        const sc_time t0 = sc_time_stamp();
        grupo("C15 GDB contra hello_world de ESP-IDF: G1 (el DM en directo) (E6)");
        check(c3b.fuente_jtag() == Esp32C3::FuenteJtag::Usb, "de fabrica, el JTAG va al USB-Serial-JTAG (DIS_USB_JTAG = 0)");
        informa_c3("G1", sesion_c3(g1, puerto_gdb(0)));
        grupo("C16 lo mismo por G2' (el TAP en MTMS-MTDO, con la sonda de bancada) (E6)");
        const ResC3 r = sesion_c3(g2p, puerto_gdb(1));
        char b[64];
        std::snprintf(b, sizeof b, "con DIS_USB_JTAG (jtag=pines), el IDCODE por los pines: 0x%08X", dmi_pines.idcode());
        check(dmi_pines.idcode() == Esp32C3::IDCODE, b);
        informa_c3("G2'", r);
        t_gdb += sc_time_stamp() - t0;
    }

    // --- E7: el USB-Serial-JTAG, con el puente USB en memoria ----------------
    // Las órdenes del JTAG por el EP2 y lo que vuelve por el EP3
    uint32_t jtag_usb(PuenteUsbCdc& pu, const std::vector<uint8_t>& nib, unsigned nbits) {
        std::vector<uint8_t> s, p;
        std::vector<uint8_t> n = nib;
        if (n.size() & 1u) n.push_back(0xA);
        for (size_t i = 0; i < n.size(); i += 2) s.push_back(uint8_t((n[i] << 4) | n[i + 1]));
        const uint8_t dir = c3b.usj.direccion();
        pu.controlador().paquete(PID_OUT, dir, 2, s, p);
        uint32_t v = 0;
        unsigned k = 0;
        for (int intento = 0; intento < 20 && k < nbits; ++intento) {
            if (pu.controlador().paquete(PID_IN, dir, 3, {}, p) != PID_ACK) continue;
            for (uint8_t b : p) for (unsigned j = 0; j < 8 && k < nbits; ++j, ++k) v |= uint32_t((b >> j) & 1u) << k;
        }
        return v;
    }
    uint32_t idcode_usb(PuenteUsbCdc& pu) {
        std::vector<uint8_t> n;
        for (int i = 0; i < 5; ++i) n.push_back(0x1);                 // TMS = 1: Test-Logic-Reset
        n.push_back(0x0); n.push_back(0x1); n.push_back(0x0); n.push_back(0x0);   // -> Shift-DR (IR = IDCODE)
        for (int i = 0; i < 32; ++i) n.push_back(uint8_t(0x4 | (i == 31 ? 1 : 0)));  // 32 bits, capturando
        n.push_back(0xA);                                              // CMD_FLUSH
        return jtag_usb(pu, n, 32);
    }
    void c17() {
        grupo("C17 el USB-Serial-JTAG: CDC-ACM, consola de ESP-IDF, descarga por USB y JTAG (E7)");
        c3b.alimenta(false);
        wait(10, SC_US);
        if (c3b.flash()) c3b.flash()->borra_todo();
        check(carga_flash_args(c3b, "build/idf/v5.5/hello_world"), "hello_world (v5.5) en c3b, con la consola secundaria en el USB");
        serie::Destino d = serie::parsea("memoria");
        PuenteUsbCdc pu(c3b.gpio.nodo_de(19), c3b.gpio.nodo_de(18), &c3b.usj, d);
        pu.ev_in = &c3b.usj.ev_in_listo();
        c3b.alimenta(true);
        const sc_time t0 = sc_time_stamp();
        while (!pu.listo() && sc_time_stamp() - t0 < sc_time(500, SC_MS)) wait(1, SC_MS);
        char b[96];
        std::snprintf(b, sizeof b, "el PC lo enumera: %04X:%04X \"%s\"", pu.vid(), pu.pid(), pu.producto().c_str());
        check(pu.listo() && pu.vid() == 0x303A && pu.pid() == 0x1001, b);
        check(pu.hay_jtag(), "y encuentra la interfaz JTAG de Espressif (FF/FF/01) con sus dos bulk");
        // La consola: la secundaria de ESP-IDF en el CDC
        const sc_time t1 = sc_time_stamp();
        while (pu.memoria()->recibido().find("Hello world!") == std::string::npos && sc_time_stamp() - t1 < sc_time(1500, SC_MS))
            wait(5, SC_MS);
        check(pu.memoria()->recibido().find("Hello world!") != std::string::npos,
              "la consola de ESP-IDF sale por el USB (CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG): \"Hello world!\"");
        // El JTAG del chip, por el USB (los eFuse de fábrica lo llevan aquí)
        std::snprintf(b, sizeof b, "el IDCODE por el USB (CMD_CLK con captura y CMD_FLUSH): 0x%08X", idcode_usb(pu));
        check(idcode_usb(pu) == Esp32C3::IDCODE, b);
        // La descarga por USB: la secuencia «usb-reset» de esptool, en SET-CONTROL de RFC 2217
        terminal.clear();
        c3b.uart0.salida.clear();
        for (uint8_t v : {12, 9, 8, 12, 11, 9, 11, 9, 12}) { pu.pide_control(v); wait(5, SC_MS); }
        const sc_time t3 = sc_time_stamp();
        while (c3b.uart0.salida.find("waiting for download") == std::string::npos && sc_time_stamp() - t3 < sc_time(200, SC_MS))
            wait(1, SC_MS);
        const std::string& o = c3b.uart0.salida;
        if (std::getenv("C3_DBG")) std::printf("  [c17] uart0: %s\n", o.substr(o.size() > 400 ? o.size() - 400 : 0).c_str());
        check(o.find("rst:0x15 (USB_UART_CHIP_RESET),boot:0x4 (DOWNLOAD(USB/UART0") != std::string::npos,
              "DTR y RTS (la secuencia usb-reset de esptool): rst:0x15, boot:0x4, modo descarga");
        // El SYNC por el CDC: la ROM contesta por el USB
        const size_t r0 = pu.memoria()->recibido().size();
        bool sync = false;
        for (int i = 0; i < 6 && !sync; ++i) {
            pu.memoria()->empuja(sync_esptool());
            wait(20, SC_MS);
            for (const std::string& x : respuestas(pu.memoria()->recibido(), r0))
                if (x.size() >= 2 && uint8_t(x[0]) == 1 && uint8_t(x[1]) == 0x08) sync = true;
        }
        check(sync, "el SYNC de esptool por el CDC: la ROM contesta por el USB (sin detectar baudios)");
        // Con DIS_USB_JTAG (jtag=pines), el JTAG ya no llega por el USB
        c3b.jtag_en_pines(true);
        c3b.alimenta(false); wait(10, SC_US); c3b.alimenta(true);
        const sc_time t2 = sc_time_stamp();
        while (!pu.listo() && sc_time_stamp() - t2 < sc_time(500, SC_MS)) wait(1, SC_MS);
        check(idcode_usb(pu) == 0u, "con DIS_USB_JTAG quemado, el TAP no contesta por el USB (lee 0)");
        c3b.jtag_en_pines(false);
        c3b.alimenta(false);
        pu.enchufa(false);
        wait(1, SC_MS);
    }

    // --- E8: el LEDC --------------------------------------------------------------
    // El periodo y el ciclo de un PWM en un nodo: dos subidas seguidas y la
    // bajada de en medio
    bool mide_pwm(analog_net_if& n, double& periodo_us, double& ciclo) {
        auto alto = [&] { return n.voltage() > 1.65f; };
        const sc_time lim = sc_time_stamp() + sc_time(5, SC_MS);
        // Una subida
        while (alto()) { wait(sc_time(5, SC_MS), n.value_changed_event()); if (sc_time_stamp() >= lim) return false; }
        while (!alto()) { wait(sc_time(5, SC_MS), n.value_changed_event()); if (sc_time_stamp() >= lim) return false; }
        const sc_time t0 = sc_time_stamp();
        while (alto()) { wait(sc_time(5, SC_MS), n.value_changed_event()); if (sc_time_stamp() >= lim) return false; }
        const sc_time t1 = sc_time_stamp();
        while (!alto()) { wait(sc_time(5, SC_MS), n.value_changed_event()); if (sc_time_stamp() >= lim) return false; }
        const sc_time t2 = sc_time_stamp();
        periodo_us = (t2 - t0).to_seconds() * 1e6;
        ciclo = (t1 - t0).to_seconds() / (t2 - t0).to_seconds();
        return true;
    }
    void c18() {
        grupo("C18 el LEDC: 4 kHz con 13 bits en GPIO8, el ciclo y el fundido (E8)");
        check(arranca("ledc.elf"), "ledc.elf");
        wait(500, SC_US);
        double p = 0, d = 0;
        char b[96];
        bool ok = mide_pwm(c3.gpio.nodo_de(8), p, d);
        std::snprintf(b, sizeof b, "50 %%: periodo %.2f us (250), ciclo %.3f", p, d);
        check(ok && std::fabs(p - 250.0) < 0.5 && std::fabs(d - 0.5) < 0.002, b);
        while (c3.peek32(buzon_ + 8) < 2) wait(100, SC_US);
        wait(600, SC_US);
        ok = mide_pwm(c3.gpio.nodo_de(8), p, d);
        std::snprintf(b, sizeof b, "25 %% (DUTY_START): periodo %.2f us, ciclo %.3f", p, d);
        check(ok && std::fabs(p - 250.0) < 0.5 && std::fabs(d - 0.25) < 0.002, b);
        check(espera_fin(sc_time(20, SC_MS)), "el fundido acaba con DUTY_CHNG_END");
        check_eq(bz(1), 4096u, "DUTY_R tras el fundido: 2048 + 4 x 512");
        ok = mide_pwm(c3.gpio.nodo_de(8), p, d);
        std::snprintf(b, sizeof b, "y el pin, al 50 %% otra vez: ciclo %.3f", d);
        check(ok && std::fabs(d - 0.5) < 0.002, b);
        c3.alimenta(false);
    }

    void c19() {
        grupo("C19 el RMT con dos WS2812, el SAR ADC y su sensor de temperatura, y el AES (E8)");
        if (d_g0_ < 0) d_g0_ = c3.gpio.nodo_de(0).register_driver("tb_adc");
        c3.gpio.nodo_de(0).set_drive(d_g0_, 0.5f, 10.0f);          // 0,5 V en GPIO0
        check(arranca("e8.elf"), "e8.elf");
        check(espera_fin(sc_time(20, SC_MS)), "acaba");
        c3.gpio.nodo_de(0).set_hiz(d_g0_);
        char b[96];
        check(bz(0) == 1 && c3.rmt.envios(0) == 1, "el RMT manda la trama del canal 0 y da TX_END");
        std::snprintf(b, sizeof b, "los WS2812 se encienden: 0x%06X y 0x%06X (RGB)", tira.color(0), tira.color(1));
        check(tira.tramas() >= 1 && tira.color(0) == 0x102030u && tira.color(1) == 0xFF0080u, b);
        std::snprintf(b, sizeof b, "ADC1, GPIO0 a 0,5 V: %u a 0 dB (2730) y %u a 11 dB (819)", bz(1), bz(2));
        check(std::abs(int(bz(1)) - 2730) <= 3 && std::abs(int(bz(2)) - 819) <= 3, b);
        std::snprintf(b, sizeof b, "el sensor de temperatura a 25 C: TSENS_OUT = %u (104)", bz(3));
        check(bz(3) == 104u, b);
        check(bz(4) == 0xD8E0C469u && bz(5) == 0x30047B6Au && bz(6) == 0x80B7CDD8u && bz(7) == 0x5AC5B470u,
              "AES-128, FIPS-197 C.1: 69c4e0d8 6a7b0430 d8cdb780 70b4c55a");
        check(bz(8) == 0xCAB7A28Eu && bz(9) == 0xBF456751u && bz(10) == 0x9049FCEAu && bz(11) == 0x8960494Bu,
              "AES-256, FIPS-197 C.3: 8ea2b7ca 516745bf eafc4990 4b496089");
        check(bz(12) == 0x33221100u && bz(13) == 0x77665544u && bz(14) == 0xBBAA9988u && bz(15) == 0xFFEEDDCCu,
              "AES-256 descifrado: el texto en claro otra vez");
        c3.alimenta(false);
    }

    void c20() {
        grupo("C20 el I2C: una 24Cxx por la lista de ordenes, con RSTART y NACK (E8)");
        c3.gpio.nodo_de(10).set_drive(d_pu_scl_, 3.3f, 4700.0f);
        c3.gpio.nodo_de(1).set_drive(d_pu_sda_, 3.3f, 4700.0f);
        check(arranca("i2c.elf"), "i2c.elf");
        check(espera_fin(sc_time(20, SC_MS)), "acaba");
        char b[96];
        std::snprintf(b, sizeof b, "escribir: TRANS_COMPLETE sin NACK (INT_RAW 0x%X)", bz(0));
        check((bz(0) & 0x80u) && !(bz(0) & 0x400u), b);
        check(eeprom.peek(0x10) == 0xC0 && eeprom.peek(0x11) == 0xFF && eeprom.peek(0x12) == 0xEE && eeprom.peek(0x13) == 0x42,
              "la EEPROM tiene C0 FF EE 42 en 0x10");
        std::snprintf(b, sizeof b, "leer con RSTART: %02X %02X %02X %02X", bz(1), bz(2), bz(3), bz(4));
        check(bz(1) == 0xC0 && bz(2) == 0xFF && bz(3) == 0xEE && bz(4) == 0x42 && (bz(5) & 0x80u), b);
        std::snprintf(b, sizeof b, "la 0x51 no contesta: NACK (INT_RAW 0x%X)", bz(6));
        check((bz(6) & 0x400u) != 0, b);
        c3.gpio.nodo_de(10).set_hiz(d_pu_scl_);
        c3.gpio.nodo_de(1).set_hiz(d_pu_sda_);
        c3.alimenta(false);
    }

    void c21() {
        grupo("C21 el GP-SPI2 con la GDMA: un ST7735 (Tft128x160) en c3b (E8)");
        sonda.suelta();          // la sonda de C16 tiene TCK (GPIO6, el SCLK aqui) a nivel bajo
        const int dv = tft_vcc.register_driver("tb"), dg = tft_gnd.register_driver("tb"),
                  dr = tft_rst.register_driver("tb"), dl = tft_luz.register_driver("tb");
        tft_vcc.set_drive(dv, 3.3f, 0.1f); tft_gnd.set_drive(dg, 0.f, 0.1f);
        tft_rst.set_drive(dr, 3.3f, 10.f); tft_luz.set_drive(dl, 3.3f, 10.f);
        c3b.alimenta(false);
        wait(10, SC_US);
        const std::string e = c3b.carga_elf(std::string(FW) + "spi_tft.elf");
        uint32_t bz_b = 0;
        simbolo_elf(std::string(FW) + "spi_tft.elf", "buzon", bz_b);
        c3b.poke32(bz_b, 0); c3b.poke32(bz_b + 4, 0);
        c3b.alimenta(true);
        check(e.empty(), "spi_tft.elf en c3b");
        const sc_time t0 = sc_time_stamp();
        while (c3b.peek32(bz_b + 4) != 1 && sc_time_stamp() - t0 < sc_time(500, SC_MS)) wait(1, SC_MS);
        check(c3b.peek32(bz_b + 4) == 1, "acaba");
        char b[112];
        std::snprintf(b, sizeof b, "%u transacciones con TRANS_DONE; el ST7735, despierto y encendido",
                      c3b.peek32(bz_b + 8));
        if (std::getenv("C3_DBG"))
            std::printf("  [c21] alim %d reset %d dormida %d encendida %d bpp %u ordenes %llu pixeles %llu perdidas %llu\n",
                        tft.alimentada(), tft.en_reset(), tft.dormida(), tft.encendida(), tft.bits_por_pixel(),
                        (unsigned long long)tft.ordenes(), (unsigned long long)tft.pixeles(),
                        (unsigned long long)tft.ordenes_perdidas());
        check(c3b.peek32(bz_b + 8) == 16u && tft.mostrando() && tft.bits_por_pixel() == 16, b);
        std::snprintf(b, sizeof b, "la GDMA: OUT_DONE, OUT_EOF y OUT_TOTAL_EOF (0x%X), y el owner a 0 (0x%08X)",
                      c3b.peek32(bz_b + 12), c3b.peek32(bz_b + 16));
        check((c3b.peek32(bz_b + 12) & 0x118u) == 0x118u && !(c3b.peek32(bz_b + 16) >> 31), b);
        std::snprintf(b, sizeof b, "100 pixeles por DMA, rojos: (0,0) 0x%06X, (9,9) 0x%06X, (10,10) 0x%06X",
                      tft.color_en(0, 0), tft.color_en(9, 9), tft.color_en(10, 10));
        const uint32_t r = tft.color_en(0, 0);
        check(tft.pixeles() >= 100 && ((r >> 16) & 0xFF) >= 0xF0 && (r & 0xFFFF) == 0 && tft.color_en(9, 9) == r &&
              tft.color_en(10, 10) != r, b);
        c3b.alimenta(false);
        for (AnalogNet* n : {&tft_vcc, &tft_gnd, &tft_rst, &tft_luz}) (void)n;
        tft_vcc.set_hiz(dv);
    }

    void c22() {
        grupo("C22 el TWAI a 500 kbit/s con un nodo CAN, el filtro y el error pasivo (E8)");
        xcvr.set_attached(true);
        nodo_can.set_enabled(true);
        nodo_can.set_ack(true);
        nodo_can.flush();
        c3b.alimenta(false);
        wait(10, SC_US);
        const std::string e = c3b.carga_elf(std::string(FW) + "twai.elf");
        uint32_t bz = 0;
        simbolo_elf(std::string(FW) + "twai.elf", "buzon", bz);
        for (unsigned i = 0; i < 32; ++i) c3b.poke32(bz + 4 * i, 0);
        CanFrame ext{};
        ext.id = 0x1ABCDE0; ext.ide = true; ext.dlc = 3; ext.data[0] = 0x11; ext.data[1] = 0x22; ext.data[2] = 0x33;
        nodo_can.send(ext);
        c3b.alimenta(true);
        check(e.empty(), "twai.elf en c3b");
        auto r = [&](unsigned i) { return c3b.peek32(bz + 8 + 4 * i); };
        auto espera = [&](unsigned i, uint32_t v, double ms) {
            const sc_time t0 = sc_time_stamp();
            while (r(i) != v && sc_time_stamp() - t0 < sc_time(ms, SC_MS)) wait(10, SC_US);
            return r(i) == v;
        };
        check(espera(15, 1, 100), "manda y recibe; pone el filtro");
        char b[128];
        const CanFrame& u = nodo_can.last();
        std::snprintf(b, sizeof b, "el nodo recibe 0x%X [%02X %02X %02X %02X] (dlc %u); STATUS 0x%X, INT 0x%X",
                      u.id, u.data[0], u.data[1], u.data[2], u.data[3], u.dlc, r(0), r(1));
        check(nodo_can.received() >= 1 && u.id == 0x123 && !u.ide && u.dlc == 4 && u.data[0] == 0xDE &&
              u.data[3] == 0xEF && (r(0) & 0xCu) == 0xCu && (r(1) & 2u), b);
        std::snprintf(b, sizeof b, "el C3 recibe el extendido: %02X %02X %02X %02X %02X | %02X %02X %02X; FIFO %u -> %u",
                      r(2), r(3), r(4), r(5), r(6), r(7), r(8), r(9), r(10), r(11));
        check(r(2) == 0x83 && r(3) == 0x0D && r(4) == 0x5E && r(5) == 0x6F && r(6) == 0x00 &&
              r(7) == 0x11 && r(8) == 0x22 && r(9) == 0x33 && r(10) == 1 && r(11) == 0, b);
        CanFrame a{}; a.id = 0x100; a.dlc = 1; a.data[0] = 0x66;
        CanFrame c{}; c.id = 0x555; c.dlc = 1; c.data[0] = 0x77;
        nodo_can.send(a); nodo_can.send(c);
        check(espera(15, 2, 100), "el filtro");
        std::snprintf(b, sizeof b, "el filtro deja pasar 0x555 y no 0x100: %u en la FIFO, 0x%04X [%02X]", r(12), r(13), r(14));
        check(r(12) == 1 && r(13) == 0xAAA0u && r(14) == 0x77, b);
        nodo_can.set_ack(false);
        c3b.poke32(bz + 8 + 4 * 20, 1);
        const sc_time t0 = sc_time_stamp();
        while (c3b.peek32(bz + 4) != 1 && sc_time_stamp() - t0 < sc_time(100, SC_MS)) wait(10, SC_US);
        check(c3b.peek32(bz + 4) == 1, "acaba");
        std::snprintf(b, sizeof b, "sin asentimiento: TEC %u (128, error pasivo), INT 0x%X (EW, EP, BE), STATUS 0x%X",
                      r(16), r(17), r(18));
        check(r(16) == 128 && (r(17) & 0xA4u) == 0xA4u && (r(18) & 0x40u) && !(r(18) & 0x80u), b);
        std::snprintf(b, sizeof b, "ABORT_TX: el buffer de transmision libre (STATUS 0x%X)", r(19));
        check((r(19) & 0x4u) && !(r(19) & 0x20u), b);
        c3b.alimenta(false);
        nodo_can.set_ack(true);
        nodo_can.flush();
        xcvr.set_attached(false);
    }

    // La región del DM en el mapa de la CPU [TRM-C3 tabla 1.3-1]: el program
    // buffer y los datos del DM, en 0x2000_0000-0x27FF_FFFF
    void c23() {
        grupo("C23 la region del DM: el program buffer en 0x2000_0340, sus datos y la PMP (E6)");
        c3b.alimenta(false);
        wait(10, SC_US);
        const std::string e = c3b.carga_elf(std::string(FW) + "blinky.elf");
        c3b.alimenta(true);
        wait(1, SC_MS);
        RvDmCliente dmc(*c3b.dm);
        dmi_if& d = *c3b.dm;
        check(e.empty() && dmc.activar() && dmc.parar(), "blinky en c3b, parado por el DM");
        uint32_t ac = 0;
        d.dmi_leer(dm::ABSTRACTCS, ac);
        char b[160];
        std::snprintf(b, sizeof b, "abstractcs: progbufsize 16 y datacount 2, como un C3 (0x%08X)", ac);
        check((ac >> 24 & 0x1Fu) == 16 && (ac & 0xFu) == 2, b);
        auto ejecuta = [&](std::initializer_list<uint32_t> pb) {
            unsigned i = 0;
            for (uint32_t w : pb) d.dmi_escribir(dm::PROGBUF0 + i++, w);
            d.dmi_escribir(dm::COMMAND, dm::AR_SIZE32 | dm::AR_POSTEXEC);
            uint32_t a = 0;
            d.dmi_leer(dm::ABSTRACTCS, a);
            d.dmi_escribir(dm::ABSTRACTCS, 7u << 8);
            return (a >> 8) & 7u;
        };
        // auipc a0, 0: la dirección del program buffer
        uint32_t a0 = 0;
        const unsigned e1 = ejecuta({0x00000517u, 0x00100073u});
        dmc.lee_reg(RvDmCliente::GPR(10), a0);
        std::snprintf(b, sizeof b, "auipc en el program buffer: a0 = 0x%08X (0x20000340), cmderr %u", a0, e1);
        check(e1 == 0 && a0 == 0x20000340u, b);
        // lw/sw relativos al pc: data0 en 0x2000_0380 y data1 en 0x2000_0384
        d.dmi_escribir(dm::DATA0, 0x12345678u);
        d.dmi_escribir(dm::DATA0 + 1, 0);
        const unsigned e2 = ejecuta({0x00000297u, 0x0402A583u, 0x00158593u, 0x04B2A223u, 0x00100073u});
        uint32_t d1 = 0;
        d.dmi_leer(dm::DATA0 + 1, d1);
        std::snprintf(b, sizeof b, "lw de data0 y sw a data1 por sus direcciones: data1 = 0x%08X, cmderr %u", d1, e2);
        check(e2 == 0 && d1 == 0x12345679u, b);
        // Fuera del modo depuración la región no se ejecuta: un paso con
        // dpc = 0x2000_0000 da el error de acceso a instrucción
        uint32_t mtvec = 0, mc = 0, mepc = 0;
        dmc.lee_reg(0x305, mtvec);
        dmc.escribe_reg(RvDmCliente::CSR_DPC, 0x20000000u);
        const bool p = dmc.paso();
        dmc.lee_reg(0x342, mc);
        dmc.lee_reg(0x341, mepc);
        std::snprintf(b, sizeof b, "fuera del modo depuracion: saltar a 0x20000000 da mcause %u, mepc 0x%08X", mc, mepc);
        check(p && mc == 1 && mepc == 0x20000000u, b);
        // La PMP: una regla sin L no cuenta en modo M; con L y sin X, el
        // program buffer ya no se busca (por eso ESP-IDF le da una regla RWX)
        dmc.escribe_reg(0x3B0, 0x08FFFFFFu);          // NAPOT 0x2000_0000, 128 MB
        dmc.escribe_reg(0x3A0, 0x18u);                // NAPOT, sin permisos, sin L
        const unsigned e3 = ejecuta({0x00000517u, 0x00100073u});
        dmc.escribe_reg(0x3A0, 0x98u);                // con L
        const unsigned e4 = ejecuta({0x00000517u, 0x00100073u});
        std::snprintf(b, sizeof b, "PMP sobre la region: sin L, cmderr %u (0); con L y sin X, cmderr %u (3: excepcion)", e3, e4);
        check(e3 == 0 && e4 == 3, b);
        c3b.alimenta(false);
    }


    // E8: el RSA con una clave RSA-1024 de gen_rsa.py (los resultados, de
    // Python): firma, comprobación con SEARCH y sin tiempo constante,
    // MODMULT, MULT, la interrupción y los tiempos de modelo
    void c24() {
        grupo("C24 el RSA: firma y comprobacion RSA-1024, MODMULT, MULT y la interrupcion (E8)");
        check(arranca("rsa.elf"), "rsa.elf");
        check(espera_fin(sc_time(100, SC_MS)), "acaba");
        char b[160];
        const uint32_t ok = bz(0);
        std::snprintf(b, sizeof b, "RSA_DATE 0x%08X y RSA_CLEAN %u", bz(4), bz(5));
        check(bz(4) == 0x20200618u && bz(5) == 1u, b);
        std::snprintf(b, sizeof b, "la firma S = X^d mod M sale como la de Python (palabra baja 0x%08X)", bz(6));
        check(ok & 1u, b);
        check(ok & 2u, "S^e mod M = X con SEARCH (bit 16) y CONSTANT_TIME a 0");
        check(ok & 4u, "MODMULT: A*B mod M");
        check(ok & 8u, "MULT: 512 x 512 bits = 1024");
        check(ok & 16u, "tras MODEXP, M, Y, M' y MODE no cambian");
        check(ok & 32u, "IDLE vale 0 justo despues de arrancar");
        check((ok & 64u) && (ok & 128u), "la fuente 47 sube al acabar, baja con CLEAR_INTERRUPT y no sube sin INTERRUPT_ENA");
        // Los tiempos [de modelo]: n(n+6) ciclos del APB (80 MHz) por producto
        // de Montgomery; la CPU, a 160 MHz. La firma: 2 + 2*1024 + 1
        // productos; la comprobación: 22; MODMULT: 2. En tics del SYSTIMER
        // (16 MHz): 498803, 5350 y 486, más lo que tarda leerlo
        std::snprintf(b, sizeof b, "tiempos: firma %u, comprobacion %u y MODMULT %u tics de 16 MHz (498803, 5350 y 486)",
                      bz(1), bz(2), bz(3));
        check(bz(1) >= 498803u && bz(1) < 498803u + 20 && bz(2) >= 5350u && bz(2) < 5350u + 20 &&
              bz(3) >= 486u && bz(3) < 486u + 20, b);
        check(c3.rsa.operaciones() == 4, "cuatro operaciones");
        c3.alimenta(false);
    }

    // E8: el HMAC con la secuencia de esp_hmac_calculate y las claves
    // quemadas en los eFuse por el banco (los resultados, de Python:
    // vec/gen_hmac.py), y la reapertura del JTAG que cierra SOFT_DIS_JTAG
    void quema_clave(unsigned n, const uint8_t k[32], unsigned proposito) {
        for (unsigned i = 0; i < 8; ++i)
            c3.efuse.pon_palabra(0x9C + 0x20 * n + 4 * i,
                                 uint32_t(k[4 * i]) | uint32_t(k[4 * i + 1]) << 8 | uint32_t(k[4 * i + 2]) << 16 | uint32_t(k[4 * i + 3]) << 24);
        const uint32_t off = n < 2 ? 0x34u : 0x38u;
        const unsigned s = n < 2 ? 24 + 4 * n : 4 * (n - 2);
        for (unsigned b = 0; b < 4; ++b) c3.efuse.pon_bit(off, s + b, (proposito >> b) & 1u);
    }
    void c25() {
        grupo("C25 el HMAC: subida con las cuatro formas de acabar, el proposito, RD_DIS y el JTAG (E8)");
        static const uint8_t cero[32] = {};
        quema_clave(0, HMAC_K0, 8);
        quema_clave(1, HMAC_K1, 6);
        quema_clave(2, HMAC_K2, 7);
        c3.efuse.pon_bit(0x30, 0, true);                 // RD_DIS de KEY0
        c3.efuse.pon_bit(0x30, 9, true);                 // DIS_USB_JTAG: el JTAG, a los pines
        c3.efuse.pon_bit(0x30, 16, true);                // SOFT_DIS_JTAG = 1: cerrado
        check(arranca("hmac.elf"), "hmac.elf");
        c3.poke32(buzon_ + 8 + 12, 0);                   // res[3]: lo que dejo el anterior
        wait(5, SC_US);
        check(c3.fuente_jtag() == Esp32C3::FuenteJtag::Ninguna, "con SOFT_DIS_JTAG impar, el JTAG de los pines esta cerrado");
        bool paso1 = false, paso2 = false;
        const sc_time t0 = sc_time_stamp();
        while (sc_time_stamp() - t0 < sc_time(20, SC_MS) && bz(3) < 3) {
            if (bz(3) == 1 && !paso1) { paso1 = true; check(c3.fuente_jtag() == Esp32C3::FuenteJtag::Ninguna, "un WR_JTAG equivocado no lo abre"); }
            if (bz(3) == 2 && !paso2) { paso2 = true; check(c3.fuente_jtag() == Esp32C3::FuenteJtag::Pines, "el HMAC de 32 bytes 0x00 con KEY1 (proposito 6) lo abre"); }
            wait(2, SC_US);
        }
        check(paso1 && paso2, "el firmware pasa por los dos intentos");
        check(espera_fin(sc_time(20, SC_MS)), "acaba");
        check(c3.fuente_jtag() == Esp32C3::FuenteJtag::Ninguna, "SET_INVALIDATE_JTAG lo vuelve a cerrar");
        check(c3.hmac.aperturas() == 1, "se abre una sola vez");
        char b[160];
        const uint32_t ok = bz(0);
        std::snprintf(b, sizeof b, "DATE 0x%08X", bz(2));
        check(bz(2) == 0x20200618u, b);
        std::snprintf(b, sizeof b, "32 bytes con ONE_BLOCK, como Python (primera palabra 0x%08X)", bz(1));
        check(ok & 1u, b);
        check(ok & 2u, "64 bytes con el relleno del hardware (SET_MESSAGE_END)");
        check(ok & 4u, "100 bytes: dos bloques, el ultimo con SET_MESSAGE_PAD");
        check(ok & 8u, "200 bytes: tres bloques con SET_MESSAGE_ING y PAD");
        check(ok & 16u, "KEY1 (proposito 6) en subida: QUERY_ERROR = 1");
        check(ok & 32u, "la CPU lee ceros en RD_KEY0 (RD_DIS) y el HMAC usa la clave");
        check(ok & 64u, "QUERY_BUSY = 1 justo despues de SET_MESSAGE_ONE");
        check(ok & 128u, "SET_RESULT_FINISH borra RD_RESULT");
        uint8_t ds[32];
        check(!c3.hmac.clave_ds(ds), "sin bajada de DS no hay clave del DS");
        char c[96];
        std::snprintf(c, sizeof c, "%u calculos (el de proposito equivocado no cuenta)", c3.hmac.calculos());
        check(c3.hmac.calculos() == 5, c);
        c3.alimenta(false);
        // Los eFuse, como estaban
        for (unsigned n = 0; n < 3; ++n) quema_clave(n, cero, 0);
        c3.efuse.pon_bit(0x30, 0, false);
        c3.efuse.pon_bit(0x30, 9, false);
        c3.efuse.pon_bit(0x30, 16, false);
    }

    // E8: el DS con una clave RSA-1024 cifrada como lo hace ESP-IDF
    // (vec/gen_ds.py): la clave del HMAC, las dos comprobaciones y el tiempo
    void c26() {
        grupo("C26 el DS: firma RSA-1024 con la clave del HMAC, el MD y el relleno (E8)");
        static const uint8_t cero[32] = {};
        quema_clave(3, DS_HK, 7);
        check(arranca("ds.elf"), "ds.elf");
        check(espera_fin(sc_time(200, SC_MS)), "acaba");
        char b[160];
        const uint32_t ok = bz(0);
        check(ok & 128u, "DATE 0x20200618 (en 0xE20, donde lo pone ESP-IDF)");
        check(ok & 1u, "sin la bajada del HMAC, QUERY_BUSY sigue a 1 pasado 1,1 ms y KEY_WRONG = 0");
        check(ok & 2u, "con la bajada de KEY3 (proposito 7), la clave llega");
        std::snprintf(b, sizeof b, "la firma: QUERY_CHECK = 0 y Z como Python (palabra baja 0x%08X)", bz(2));
        check(ok & 4u, b);
        check(ok & 8u, "SET_FINISH borra Z_MEM");
        check(ok & 16u, "un byte cambiado en C: QUERY_CHECK = 1 (el MD) y Z a cero");
        check(ok & 32u, "el relleno mal: QUERY_CHECK = 2, y la firma sale igual");
        check(ok & 64u, "tras SET_INVALIDATE_DS del HMAC, la clave ya no llega");
        // El tiempo [de modelo]: 2051 productos de Montgomery de n(n+6) = 1216
        // ciclos del APB, mas 75 bloques de AES (16) y 19 de SHA-256 (64):
        // 2496432 ciclos a 80 MHz = 31,2 ms, 499286 tics de 16 MHz
        std::snprintf(b, sizeof b, "la firma tarda %u tics de 16 MHz (499286)", bz(1));
        check(bz(1) >= 499286u && bz(1) < 499286u + 20, b);
        check(c3.ds.firmas() == 2, "dos firmas (la del MD mal no calcula)");
        c3.alimenta(false);
        quema_clave(3, cero, 0);
    }

    // E8: el XTS_AES: cuatro lineas cifradas a mano y escritas por el SPI1,
    // y leidas en claro por la cache (vec/gen_xts.py: el texto cifrado, de
    // espsecure)
    void c27() {
        grupo("C27 el XTS_AES: el cifrado manual por el SPI1 y el descifrado de la cache (E8)");
        static const uint8_t cero[32] = {};
        quema_clave(5, XTS_EFUSE, 4);
        c3.efuse.pon_bit(0x34, 18, true);                // SPI_BOOT_CRYPT_CNT = 1
        check(arranca("xts.elf"), "xts.elf");
        // Aqui el C3 sigue en el modo de descarga que dejaron grupos
        // anteriores (GPIO_STRAP solo se toma en un reset de chip): el
        // firmware pone los dos permisos del cifrado manual, como
        // spi_flash_encrypt_ll_enable, y el del descifrado de la descarga
        // (G0CB); por separado (C3_SOLO=c27), arranca por SPI y descifra por
        // SPI_BOOT_CRYPT_CNT
        check(espera_fin(sc_time(500, SC_MS)), "acaba");
        char b[160];
        const uint32_t ok = bz(0);
        std::snprintf(b, sizeof b, "DATE 0x%08X", bz(2));
        check(bz(2) == 0x20200111u, b);
        check(ok & 1u, "sin los permisos de SYSTEM_EXTERNAL_DEVICE_ENCRYPT_DECRYPT_CONTROL, TRIGGER no cifra");
        check(ok & 2u, "STATE: 1 al cifrar, 2 hecho, 3 soltado y 0 tras DESTROY");
        std::snprintf(b, sizeof b, "el SPI1 lee el texto cifrado de espsecure (0x%08X)", bz(1));
        check(ok & 4u, b);
        bool igual = c3.flash() != nullptr;
        for (unsigned i = 0; igual && i < 112; ++i) {
            const uint32_t a = i < 96 ? 0x10000u + i : 0x10070u + (i - 96);
            igual = c3.flash()->datos()[a] == uint8_t(XTS_CIFRADO[i / 4] >> (8 * (i % 4)));
        }
        check(igual, "en la Flash queda el texto cifrado, no el claro de W0-W7");
        check(ok & 8u, "la cache lee en claro las tres lineas de 32 bytes");
        check(ok & 16u, "y la de 16 bytes (LINESIZE = 0)");
        check(ok & 32u, "lo no escrito sigue borrado en la Flash");
        check(c3.xts.cifrados() == 4, "cuatro cifrados");
        c3.alimenta(false);
        quema_clave(5, cero, 0);
        c3.efuse.pon_bit(0x34, 18, false);
    }

    // E8: el I2S en Philips de 16 bits y estereo con SIG_LOOPBACK. Ademas de
    // lo que comprueba el firmware, el banco decodifica la trama de los
    // pines por su cuenta (WS cambia un ciclo antes del MSB del canal; 0 es
    // el izquierdo) y mide BCK y WS
    void c28() {
        grupo("C28 el I2S: Philips, 16 bits y estereo, en duplex con SIG_LOOPBACK y la GDMA (E8)");
        check(arranca("i2s.elf"), "i2s.elf");
        const sc_time t0 = sc_time_stamp();
        while (bz(3) != 1 && sc_time_stamp() - t0 < sc_time(5, SC_MS)) wait(1, SC_US);
        // El analizador: 24 tramas en los pines
        auto& bck = c3.gpio.nodo_de(0);
        auto& ws = c3.gpio.nodo_de(2);
        auto& sd = c3.gpio.nodo_de(3);
        std::vector<std::pair<uint32_t, uint32_t>> tramas;
        bool ws_ant = true, prim = true;
        int canal = -1, n = 0;
        uint32_t acum = 0, izq = 0;
        bool hay_izq = false;                            // la primera trama, entera
        bool b_ant = bck.voltage() > 1.65f;
        const sc_time lim = sc_time_stamp() + sc_time(2, SC_MS);
        while (tramas.size() < 24 && sc_time_stamp() < lim) {
            wait(sc_time(100, SC_US), bck.value_changed_event());
            const bool b = bck.voltage() > 1.65f;
            if (b && !b_ant) {
                const bool w = ws.voltage() > 1.65f, d = sd.voltage() > 1.65f;
                if (canal >= 0) {
                    acum = (acum << 1) | (d ? 1u : 0u);
                    if (++n == 16) {
                        if (canal == 0) { izq = acum; hay_izq = true; }
                        else if (hay_izq) { tramas.push_back({izq, acum}); hay_izq = false; }
                        canal = -1;
                    }
                }
                if (!prim && w != ws_ant) { canal = w ? 1 : 0; n = 0; acum = 0; }
                ws_ant = w;
                prim = false;
            }
            b_ant = b;
        }
        // Las tramas decodificadas, seguidas, dentro de las que manda
        bool sigue = tramas.size() >= 8;
        unsigned desde = 0;
        if (sigue) {
            while (desde < 32 && tramas[0].first != 0x1000u + desde * 0x111u) ++desde;
            for (unsigned i = 0; sigue && i < tramas.size() && desde + i < 32; ++i) {
                const unsigned k = desde + i;
                sigue = tramas[i].first == ((0x1000u + k * 0x111u) & 0xFFFFu) &&
                        tramas[i].second == ((0xF000u ^ (k * 0x0F0Fu)) & 0xFFFFu);
            }
        }
        char b[160];
        std::snprintf(b, sizeof b, "el analizador decodifica %zu tramas Philips en los pines, desde la %u, como las del firmware",
                      tramas.size(), desde);
        check(sigue && desde < 32, b);
        double p = 0, dc = 0;
        bool ok = mide_pwm(bck, p, dc);
        std::snprintf(b, sizeof b, "BCK: %.4f us (160 MHz / (13 + 1/48) / 8 = 0,6510) y ciclo %.3f", p, dc);
        check(ok && std::fabs(p - 0.65104) < 0.0005 && std::fabs(dc - 0.5) < 0.01, b);
        ok = mide_pwm(ws, p, dc);
        std::snprintf(b, sizeof b, "WS: %.3f us (32 BCK, 48 kHz: 20,833) y ciclo %.3f", p, dc);
        check(ok && std::fabs(p - 20.833) < 0.01 && std::fabs(dc - 0.5) < 0.01, b);
        check(espera_fin(sc_time(10, SC_MS)), "acaba");
        const uint32_t okf = bz(0);
        std::snprintf(b, sizeof b, "DATE 0x%08X", bz(2));
        check(bz(2) == 0x2003230u, b);
        std::snprintf(b, sizeof b, "lo recibido por la GDMA es lo mandado (0x%08X)", bz(1));
        check(okf & 1u, b);
        check(okf & 2u, "los descriptores de entrada: length 64, suc_eof y owner 0 (RXEOF_NUM = 64)");
        check(okf & 4u, "la GDMA: IN_SUC_EOF e IN_DONE en el canal 1 y OUT_EOF en el 0");
        check(okf & 8u, "el I2S: TX_DONE, RX_DONE y TX_IDLE (TX_STOP_EN)");
        check(okf & 16u, "TX_UPDATE y RX_UPDATE se bajan solos");
        std::snprintf(b, sizeof b, "64 muestras mandadas y recibidas (%u, %u)", c3.i2s.muestras_tx(), c3.i2s.muestras_rx());
        check(c3.i2s.muestras_tx() == 64 && c3.i2s.muestras_rx() == 64, b);
        c3.alimenta(false);
    }

    void run() {
        // C3_SOLO=c4 (o c1...c14): un grupo suelto, para desarrollar
        if (const char* solo = std::getenv("C3_SOLO")) {
            const std::string q = solo;
            void (TbC3::*g[])() = {&TbC3::c1, &TbC3::c2, &TbC3::c3_uart, &TbC3::c4, &TbC3::c5, &TbC3::c6, &TbC3::c7,
                                   &TbC3::c8, &TbC3::c9, &TbC3::c10, &TbC3::c11, &TbC3::c12, &TbC3::c13, &TbC3::c14,
                                   &TbC3::c15, &TbC3::c15, &TbC3::c17, &TbC3::c18, &TbC3::c19, &TbC3::c20, &TbC3::c21, &TbC3::c22, &TbC3::c23, &TbC3::c24, &TbC3::c25, &TbC3::c26, &TbC3::c27, &TbC3::c28};
            const int k = q.size() > 1 ? std::atoi(q.c_str() + 1) : 0;
            if (k >= 1 && k <= int(sizeof g / sizeof g[0])) (this->*g[k - 1])();
            std::printf("\nTOTAL C3: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
            sc_stop();
            return;
        }
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
        c14();
        c15();
        c17();
        c18();
        c19();
        c20();
        c21();
        c22();
        c23();
        c24();
        c25();
        c26();
        c27();
        c28();
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
    std::printf("  de los cuales C15-C16 (socket de GDB): %llu ps\n", ps(tb.t_gdb));
    std::printf("  el resto                             : %llu ps\n", ps(sc_time_stamp() - tb.t_gdb));
    return g_fallos ? 1 : 0;
}
