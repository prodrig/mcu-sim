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
// E10: el núcleo LP, con los ejemplos de examples/system/ulp/lp_core de
// ESP-IDF v5.5.6 sin tocar (verif/fw/esp32c6/idf, con el prefijo lp_):
//   D12  interrupt, lp_timer_interrupt, inter_cpu_critical_section y
//        build_system: el arranque del LP, la interrupción del PMU, la del LP
//        timer, el cerrojo entre los dos núcleos en la LP SRAM
//   D13  gpio, gpio_wakeup y gpio_intr_pulse_counter: el sueño profundo del
//        HP, el LP IO (nivel, despertar e interrupción) y el despertar del HP
//   D14  lp_uart_print y lp_i2c (con un BH1750): la LP UART y el LP I2C
//   D15  el DM con dos harts: el LP «unavail» hasta que ESP-IDF lo deja,
//        parar y seguir el LP sin tocar el HP, y el disparo cruzado
// E11: el PCNT, el MCPWM y el ETM, con los ejemplos de examples/peripherals
// de ESP-IDF v5.5.6 sin tocar (con el prefijo e11_) y motor (nuestra):
//   D16  pcnt/rotary_encoder con un KY-040; mcpwm_servo_control con un SG90;
//        mcpwm_capture_hc_sr04 y gptimer_capture_hc_sr04 (el ETM) con un
//        HC-SR04; mcpwm_sync (tres temporizadores, sincronía por GPIO); y
//        motor: tiempo muerto, freno por fallo, PCNT y ETM entre dos GPIO
// E9.6: la depuración.
//   D10  GDB contra hello_world por G1 (el DM en directo): reset halt,
//        trigger en la Flash, ebreak en la HP SRAM, watchpoint, paso y Ctrl-C
//   D11  lo mismo por G2' (el TAP en MTMS-MTDO con jtag=pines, la sonda de
//        bancada) y el IDCODE 0x0000DC25. Van por un socket de verdad: su
//        tiempo no cuenta en el invariante (la columna `resto`).
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
#include "rv/gdb/gdb_stub_rv.h"
#include "verif/sonda_jtag_rv.h"
#include "verif/gdb_client.h"
#include "parts/bh1750.h"
#include "parts/hc_sr04.h"
#include "parts/servo.h"
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
// E10: el banco del dominio LP. Unos pines que se ponen a 0 o a 1 (o se
// sueltan), un receptor de UART a 115200 en GPIO5 (la TX de la LP UART) y un
// BH1750 en el LP I2C (SDA GPIO6, SCL GPIO7) con sus pull-ups, que se
// conecta y desconecta
SC_MODULE(BancoLpC6) {
    Esp32C6& c6;
    std::string lp_uart;                 // lo que sale por GPIO5
    Bh1750 luz;
    BancoLpC6(sc_module_name nm, Esp32C6& c)
        : sc_module(nm), c6(c), luz("bh1750", c.gpio.nodo_de(7), c.gpio.nodo_de(6)) {
        for (unsigned p = 0; p < 8; ++p) d_[p] = c6.gpio.nodo_de(p).register_driver("banco_lp");
        SC_HAS_PROCESS(BancoLpC6);
        SC_THREAD(rx5);
    }
    // nivel: 0, 1, o -1 suelto; con r, la resistencia (un pull-up es 3.3 V con 4,7 k)
    void pin(unsigned p, int nivel, float r = 50.0f) {
        if (nivel < 0) c6.gpio.nodo_de(p).set_hiz(d_[p]);
        else c6.gpio.nodo_de(p).set_drive(d_[p], nivel ? 3.3f : 0.0f, r);
    }
    void suelta() { for (unsigned p = 0; p < 8; ++p) pin(p, -1); }
private:
    int d_[8];
    static bool alto(analog_net_if& n) { return n.voltage() > 1.65f; }
    void rx5() {
        analog_net_if& n = c6.gpio.nodo_de(5);
        const sc_time bit(1e6 / 115200.0, SC_US);
        for (;;) {
            wait(n.value_changed_event());
            if (alto(n) || n.floating()) continue;
            wait(bit / 2);
            if (alto(n)) continue;
            uint8_t b = 0;
            for (unsigned i = 0; i < 8; ++i) { wait(bit); if (alto(n)) b |= uint8_t(1u << i); }
            wait(bit);
            lp_uart.push_back(char(b));
        }
    }
};

// E11: las piezas de los ejemplos de periféricos, desconectadas hasta D16
//   el encoder KY-040 (A GPIO0, B GPIO2, C a masa, con sus pull-ups de 10 k),
//   el servo (la señal en GPIO0, a 5 V; 0,5-2,5 ms son -90..90 grados, como
//   en mcpwm_servo_control), dos HC-SR04 (TRIG GPIO0; ECHO GPIO2 el del
//   MCPWM y GPIO1 el del GPTimer) y lo que pasa por GPIO0-5 (los flancos)
SC_MODULE(BancoE11C6) {
    Esp32C6& c6;
    AnalogNet v5{"v5"}, gnd{"gnd"};
    Encoder enc;
    Servo servo;
    HcSr04 sonar_mcpwm, sonar_gptimer;
    std::unique_ptr<Rpull> pu_a, pu_b;
    // Los flancos de GPIO0-5: {t, nivel}
    std::vector<std::pair<double, bool>> flancos[6];
    bool graba = false;
    static ConfigServo cfg_servo() {
        ConfigServo c;
        c.pulso_min_ms = 0.5; c.pulso_max_ms = 2.5; c.inicial_aleatoria = false; c.semilla = 1;
        return c;
    }
    BancoE11C6(sc_module_name nm, Esp32C6& c)
        : sc_module(nm), c6(c),
          enc("ky040", c.gpio.nodo_de(0), c.gpio.nodo_de(2), gnd),
          servo("sg90", v5, gnd, c.gpio.nodo_de(0), cfg_servo()),
          sonar_mcpwm("hc_sr04_mcpwm", c.gpio.nodo_de(0), c.gpio.nodo_de(2)),
          sonar_gptimer("hc_sr04_gptimer", c.gpio.nodo_de(0), c.gpio.nodo_de(1)) {
        v5.set_drive(v5.register_driver("fuente_5v"), 5.0f, 0.1f);
        gnd.set_drive(gnd.register_driver("masa"), 0.0f, 0.01f);
        enc.set_enabled(false);
        servo.set_enabled(false);
        sonar_mcpwm.set_enabled(false);
        sonar_gptimer.set_enabled(false);
        for (unsigned p = 0; p < 6; ++p) {
            sc_spawn([this, p] {
                analog_net_if& n = c6.gpio.nodo_de(p);
                bool v = alto(n);
                for (;;) {
                    wait(n.value_changed_event());
                    const bool a = alto(n);
                    if (a == v) continue;
                    v = a;
                    if (graba) flancos[p].push_back({sc_time_stamp().to_seconds(), a});
                }
            });
        }
    }
    static bool alto(analog_net_if& n) { return !n.floating() && n.voltage() > 1.65f; }
    void encoder(bool si) {
        enc.set_enabled(si);
        if (si) { pu_a.reset(new Rpull(c6.gpio.nodo_de(0), 3.3, 10e3)); pu_b.reset(new Rpull(c6.gpio.nodo_de(2), 3.3, 10e3)); }
        else { pu_a.reset(); pu_b.reset(); }
    }
    void empieza() { for (auto& f : flancos) f.clear(); graba = true; }
    void para() { graba = false; }
    // Los instantes de las subidas (o bajadas) de un pin
    std::vector<double> sube(unsigned p, bool nivel = true) const {
        std::vector<double> r;
        for (auto& f : flancos[p]) if (f.second == nivel) r.push_back(f.first);
        return r;
    }
};

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
    BancoLpC6 banco_lp{"banco_lp", c6};
    BancoE11C6 banco_e11{"banco_e11", c6};
    // E9.6: GDB contra c6b: G1 con el DM en directo y G2' por los pines
    // MTMS-MTDO con la sonda de bancada
    GdbStubRv g1{"g1", *c6b.dm, puerto_gdb(0), "gdb-g1"};
    JtagSonda sonda{JtagSonda::Pines{&c6b.gpio.nodo_de(6), &c6b.gpio.nodo_de(4), &c6b.gpio.nodo_de(5),
                                     &c6b.gpio.nodo_de(7), nullptr}, 10e6};
    DmiPorJtag dmi_pines{sonda};
    GdbStubRv g2p{"g2p", dmi_pines, puerto_gdb(1), "gdb-g2p"};
    GdbClient cli;
    sc_time t_gdb = SC_ZERO_TIME;
    static unsigned puerto_gdb(unsigned i) {
        const char* e = std::getenv("TESTC6_PUERTO_GDB");
        return (e ? unsigned(std::atoi(e)) : 3481u) + i;
    }
    int d_rx_ = -1, d_g9_ = -1, d_g8_ = -1;
    std::string terminal;
    unsigned flancos_g8 = 0;
    bool prev_g8_ = false;
    static constexpr double BAUD = 115200.0;

    TbC6(sc_module_name nm) : sc_module(nm), c6("c6", MCU_ESP32C6) {
        SC_HAS_PROCESS(TbC6);
        c6.pon_flash(&flash);
        for (GdbStubRv* g : {&g1, &g2p}) { g->set_halt_on_attach(true); g->set_enabled(false); }
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

    // --- E9.6: GDB contra ESP-IDF, por G1 y por G2' (como C15-C16 del C3) -----
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
    struct ResGdb {
        bool conecta = false, reset = false, flash = false, sram = false, wp = false, paso = false, corre = false;
        uint32_t pc_reset = 0, pc_flash = 0, pc_sram = 0, pc_paso = 0;
    };
    ResGdb sesion(GdbStubRv& st, unsigned puerto, bool pines) {
        ResGdb r;
        const uint32_t app_main = simbolo("app_main"), tick = simbolo("xTaskIncrementTick"),
                       cuenta = simbolo("xTickCount");
        c6b.alimenta(false);
        wait(10, SC_US);
        if (c6b.flash()) c6b.flash()->borra_todo();
        if (!carga_flash_args(c6b, "build/idf-c6/v5.5/hello_world") || !app_main || !tick || !cuenta) return r;
        c6b.jtag_en_pines(pines);
        c6b.alimenta(true);
        wait(2, SC_MS);                       // la ROM, en marcha
        st.set_enabled(true);
        wait(1, SC_MS);
        if (!cli.conectar(puerto)) { st.set_enabled(false); return r; }
        wait(1, SC_MS);
        const std::string sup = cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=riscv");
        r.conecta = sup.find("PacketSize") != std::string::npos && cli.pedir("QStartNoAckMode") == "OK" &&
                    cli.pedir("?").rfind("T0", 0) == 0;
        // monitor reset halt: ndmreset, parado en la primera instrucción de la ROM
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
        // xTaskIncrementTick, en la HP SRAM: por ebreak (lo escribe el stub)
        const bool zb = z('Z', 0, tick, 2) == "OK";
        cli.enviar("c");
        const std::string pb = cli.recibir(sc_time(100, SC_MS));
        r.pc_sram = reg(32);
        r.sram = zb && pb.find("swbreak") != std::string::npos && r.pc_sram == tick;
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
        // y sigue
        cli.enviar("c");
        wait(20, SC_MS);
        cli.crudo("\x03");
        r.corre = cli.recibir(sc_time(50, SC_MS)).rfind("T02", 0) == 0;
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        st.set_enabled(false);
        c6b.alimenta(false);
        c6b.jtag_en_pines(false);
        return r;
    }
    void informa(const char* q, const ResGdb& r) {
        const std::string p = std::string(q) + ": ";
        check(r.conecta, p + "GDB se conecta y encuentra el nucleo parado");
        char b[96];
        std::snprintf(b, sizeof b, "monitor reset halt: ndmreset, parado en la ROM (pc 0x%08X)", r.pc_reset);
        check(r.reset, p + b);
        std::snprintf(b, sizeof b, "app_main, en la Flash, por trigger (Z1): hwbreak en 0x%08X", r.pc_flash);
        check(r.flash, p + b);
        std::snprintf(b, sizeof b, "xTaskIncrementTick, en la HP SRAM, por ebreak (Z0): swbreak en 0x%08X", r.pc_sram);
        check(r.sram, p + b);
        check(r.wp, p + "watchpoint de escritura en xTickCount (Z2): para antes del sw");
        check(r.paso, p + "un paso: el sw se hace (xTickCount + 1)");
        check(r.corre, p + "y sigue corriendo hasta Ctrl-C (T02)");
    }
    void d10() {
        const sc_time t0 = sc_time_stamp();
        grupo("D10 GDB contra hello_world de ESP-IDF: G1 (el DM en directo) (E9.6)");
        check(c6b.fuente_jtag() == Esp32C6::FuenteJtag::Usb, "de fabrica, el JTAG va al USB-Serial-JTAG (DIS_USB_JTAG = 0)");
        informa("G1", sesion(g1, puerto_gdb(0), false));
        grupo("D11 lo mismo por G2' (el TAP en MTMS-MTDO, con la sonda de bancada) (E9.6)");
        const ResGdb r = sesion(g2p, puerto_gdb(1), true);
        char b[80];
        std::snprintf(b, sizeof b, "con DIS_USB_JTAG (jtag=pines), el IDCODE por los pines: 0x%08X", dmi_pines.idcode());
        check(dmi_pines.idcode() == Esp32C6::IDCODE, b);
        informa("G2'", r);
        t_gdb += sc_time_stamp() - t0;
    }

    // --- E10: el núcleo LP -------------------------------------------------------
    // Arranca un ejemplo de examples/system/ulp/lp_core (lp_<nombre>)
    bool arranca_lp(const char* ej) {
        banco_lp.suelta();
        return arranca_flash(std::string("build/idf-c6/v5.5/") + ej);
    }
    // Lo que salió por la consola y por la LP UART, cuando algo de E10 falla
    void muestra_lp() {
        auto cola = [](const std::string& t) { return t.substr(t.size() > 500 ? t.size() - 500 : 0); };
        std::printf("    [consola] %s\n    [lp_uart %.0f baudios, %zu bytes] %s\n", cola(terminal).c_str(),
                    c6.lp_uart.baudios(), banco_lp.lp_uart.size(), cola(banco_lp.lp_uart).c_str());
        std::printf("    [pmu] hp_dormido %d lp_dormido %d, pc HP 0x%08X, pc LP 0x%08X, LP %llu instrucciones\n",
                    int(c6.pmu.hp_dormido()), int(c6.pmu.lp_dormido()), c6.core.hart.pc(), c6.core_lp.hart.pc(),
                    (unsigned long long)c6.core_lp.instrucciones());
    }
    static long ultimo_numero(const std::string& t, const std::string& tras) {
        const size_t i = t.rfind(tras);
        return i == std::string::npos ? -1 : std::atol(t.c_str() + i + tras.size());
    }
    void d12() {
        grupo("D12 el nucleo LP: el arranque, sus interrupciones, el LP timer y las atomicas (E10)");
        check(c6.dm->n_harts() == 2, "el DM tiene dos harts: el HP (0) y el LP (1)");
        const uint64_t i0 = c6.core_lp.instrucciones();
        check(arranca_lp("lp_interrupt"), "lp_core/interrupt: su flash_args");
        check(espera_texto(terminal, "Triggered 10 interrupts on the LP-Core, LP-Core received 10 interrupts",
                           sc_time(3, SC_SEC)),
              "el HP dispara HP_TRIGGER_LP diez veces y el LP las cuenta en su interrupcion (el PMU, la 30)");
        check(c6.pmu.despertares_lp() >= 1 && c6.core_lp.instrucciones() > i0,
              "el LP sale del reset en 0x5000_0080 cuando el HP lo despierta (" +
              std::to_string(c6.core_lp.instrucciones() - i0) + " instrucciones)");
        check(arranca_lp("lp_lp_timer_interrupt"), "lp_core/lp_timer_interrupt");
        check(espera_texto(terminal, "Interrupt count: 3", sc_time(3, SC_SEC)),
              "la alarma del LP timer (TAR1, cada 500 ms) interrumpe al LP: tres veces");
        check(arranca_lp("lp_inter_cpu_critical_section"), "lp_core/inter_cpu_critical_section");
        const bool fin = espera_texto(terminal, "LP CPU's increment ends", sc_time(5, SC_SEC)) &&
                         espera_texto(terminal, "HP CPU's increment ends", sc_time(5, SC_SEC));
        wait(50, SC_MS);                                   // el resto de las dos líneas
        const long a = ultimo_numero(terminal, "LP CPU's increment ends, shared counter = "),
                   b = ultimo_numero(terminal, "HP CPU's increment ends, shared counter = ");
        if (!check(fin && std::max(a, b) == 200000,
                   "100 000 incrementos de cada nucleo bajo el cerrojo de la LP SRAM (Peterson): " +
                   std::to_string(std::max(a, b))))
            std::printf("%s\n", terminal.substr(terminal.size() > 600 ? terminal.size() - 600 : 0).c_str());
        check(arranca_lp("lp_build_system"), "lp_core/build_system");
        check(espera_texto(terminal, "Sum calculated by ULP using external library func: 11", sc_time(3, SC_SEC)),
              "el LP calcula con una biblioteca suya y el HP lo lee de la LP SRAM: 11");
        c6.alimenta(false);
    }
    void d13() {
        grupo("D13 el sueno profundo, el LP IO y el despertar del HP (E10)");
        check(arranca_lp("lp_gpio"), "lp_core/gpio: el LP lee GPIO0 cada 10 ms (el LP timer)");
        banco_lp.pin(0, 0);
        check(espera_texto(terminal, "Entering in deep sleep", sc_time(3, SC_SEC)), "el HP se duerme (profundo)");
        wait(50, SC_MS);
        check(c6.pmu.hp_dormido(), "y sigue dormido mientras GPIO0 no cambia");
        banco_lp.pin(0, 1);
        check(espera_texto(terminal, "ULP woke up the main CPU!", sc_time(3, SC_SEC)),
              "GPIO0 a 1: el LP despierta al HP (LP_TRIGGER_HP), que arranca otra vez");
        check(tiene(terminal, "rst:0x5 ") && c6.lp_clkrst.causa() == 5, "con la causa 5 (CORE_DEEP_SLEEP)");
        wait(50, SC_MS);                                   // el resto de la línea
        if (!check(tiene(terminal, "ULP read changes in GPIO_0 current is: High"),
              "y lee de la LP SRAM lo que vio el LP: High")) muestra_lp();
        check(arranca_lp("lp_gpio_wakeup"), "lp_core/gpio_wakeup: GPIO2 con pull-up, despertar por flanco de bajada");
        // Se duerme 100 ms después de decirlo: el botón, ya dormido
        bool dormido = espera_texto(terminal, "Entering deep sleep", sc_time(3, SC_SEC));
        const sc_time t0 = sc_time_stamp();
        while (dormido && !c6.pmu.hp_dormido() && sc_time_stamp() - t0 < sc_time(1, SC_SEC)) wait(1, SC_MS);
        check(dormido && c6.pmu.hp_dormido(), "el HP se duerme");
        wait(10, SC_MS);
        banco_lp.pin(2, 0);
        if (!check(espera_texto(terminal, "ULP woke up the main CPU!", sc_time(3, SC_SEC)),
              "GPIO2 a 0: el LP IO despierta al LP (WAKEUP_ENABLE) y este al HP")) muestra_lp();
        banco_lp.pin(2, -1);
        check(arranca_lp("lp_gpio_intr_pulse_counter"), "lp_core/gpio_intr_pulse_counter: GPIO6");
        banco_lp.pin(6, 0);
        check(espera_texto(terminal, "Entering in deep sleep", sc_time(3, SC_SEC)), "el HP se duerme");
        for (unsigned i = 0; i < 10; ++i) { wait(2, SC_MS); banco_lp.pin(6, 1); wait(2, SC_MS); banco_lp.pin(6, 0); }
        check(espera_texto(terminal, "Pulse count: 10", sc_time(3, SC_SEC)),
              "diez flancos de subida en GPIO6: la interrupcion del LP IO los cuenta y despierta al HP");
        if (!check(tiene(banco_lp.lp_uart, "LP Core pulse counter started") &&
              tiene(banco_lp.lp_uart, "Pulse count: 10, wake-up main CPU"),
              "y el LP lo cuenta por la LP UART (GPIO5)")) muestra_lp();
        c6.alimenta(false);
    }
    void d14() {
        grupo("D14 la LP UART y el LP I2C (E10)");
        banco_lp.lp_uart.clear();
        check(arranca_lp("lp_lp_uart_print"), "lp_core/lp_uart/lp_uart_print");
        check(espera_texto(terminal, "Entering deep sleep...", sc_time(3, SC_SEC)), "el HP inicia la LP UART y se duerme");
        const sc_time t0 = sc_time_stamp();
        while (!tiene(banco_lp.lp_uart, "This program has run 2 times") && sc_time_stamp() - t0 < sc_time(3, SC_SEC))
            wait(10, SC_MS);
        if (!check(tiene(banco_lp.lp_uart, "Hello from the LP core!!\r\nThis program has run 1 times\r\n"),
              "el LP escribe por la LP UART (GPIO5, 115200)")) muestra_lp();
        check(tiene(banco_lp.lp_uart, "This program has run 2 times"),
              "y otra vez al segundo: el LP timer lo despierta, desde el reset, con la LP SRAM como estaba");
        banco_lp.luz.lux = 300;
        banco_lp.pin(6, 1, 4.7e3f);
        banco_lp.pin(7, 1, 4.7e3f);
        check(arranca_lp("lp_lp_i2c"), "lp_core/lp_i2c, con un BH1750 en el LP I2C (SDA GPIO6, SCL GPIO7)");
        banco_lp.pin(6, 1, 4.7e3f);
        banco_lp.pin(7, 1, 4.7e3f);
        check(espera_texto(terminal, "Entering deep sleep", sc_time(3, SC_SEC)) || tiene(terminal, "deep sleep"),
              "el HP inicia el LP I2C y se duerme");
        wait(100, SC_MS);
        check(banco_lp.luz.encendido() && banco_lp.luz.lecturas() > 0,
              "el LP enciende el BH1750 y lo lee (" + std::to_string(banco_lp.luz.lecturas()) + " lecturas)");
        check(c6.pmu.hp_dormido(), "300 lux: dentro de los umbrales, el HP sigue dormido");
        banco_lp.luz.lux = 2000;
        check(espera_texto(terminal, "LP core woke up the main CPU", sc_time(2, SC_SEC)),
              "2000 lux: el LP despierta al HP");
        check(espera_texto(terminal, "Lux = 2000", sc_time(1, SC_SEC)), "que lee la medida del LP: Lux = 2000");
        c6.alimenta(false);
        banco_lp.suelta();
    }

    // --- E11: el PCNT, el MCPWM y el ETM -----------------------------------------------
    static double ultimo_real(const std::string& t, const std::string& tras) {
        const size_t i = t.rfind(tras);
        return i == std::string::npos ? -1e9 : std::atof(t.c_str() + i + tras.size());
    }
    static bool cerca(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
    void d16() {
        grupo("D16 el PCNT, el MCPWM y el ETM con los ejemplos de ESP-IDF (E11)");
        // 1. El encoder KY-040 en el PCNT, con los dos canales en cuadratura
        banco_e11.encoder(true);
        check(arranca_flash("build/idf-c6/v5.5/e11_rotary_encoder"), "pcnt/rotary_encoder, con un KY-040 en GPIO0/GPIO2");
        check(espera_texto(terminal, "start pcnt unit", sc_time(2, SC_SEC)), "el PCNT en marcha: filtro de 1 us, cuatro limites y umbrales");
        wait(50, SC_MS);
        banco_e11.enc.gira(30);                  // 30 clics: 60 flancos
        const bool w50 = espera_texto(terminal, "Watch point event, count: 50", sc_time(1, SC_SEC)) ||
                         tiene(terminal, "Watch point event, count: -50");
        check(w50, "30 clics: el umbral de 50 (THRES1) interrumpe");
        wait(1200, SC_MS);
        const long n60 = ultimo_numero(terminal, "Pulse count: ");
        check(std::labs(n60) == 60, "y la cuenta es 60: los cuatro flancos de cada ciclo (" + std::to_string(n60) + ")");
        banco_e11.enc.gira(-80);                 // 160 flancos hacia atrás: hasta el límite
        const bool lim = espera_texto(terminal, n60 > 0 ? "Watch point event, count: -100" : "Watch point event, count: 100",
                                      sc_time(2, SC_SEC));
        check(lim, "80 clics atras: el limite (L_LIM) interrumpe en -100 y el contador vuelve a 0");
        wait(1200, SC_MS);
        check(ultimo_numero(terminal, "Pulse count: ") == 0, "Pulse count: 0");
        c6.alimenta(false);
        banco_e11.encoder(false);
        // 2. El servo
        banco_e11.servo.set_enabled(true);
        check(arranca_flash("build/idf-c6/v5.5/e11_mcpwm_servo_control"), "mcpwm/mcpwm_servo_control, con un SG90 en GPIO0");
        check(espera_texto(terminal, "Angle of rotation: ", sc_time(2, SC_SEC)), "el MCPWM a 50 Hz");
        espera_texto(terminal, "Angle of rotation: -40", sc_time(15, SC_SEC));
        wait(100, SC_MS);
        banco_e11.empieza();
        wait(45, SC_MS);
        banco_e11.para();
        const auto s0 = banco_e11.sube(0), b0 = banco_e11.sube(0, false);
        const long ang = ultimo_numero(terminal, "Angle of rotation: ");
        double ancho = 0, per = 0;
        if (s0.size() >= 2 && !b0.empty()) {
            per = s0[1] - s0[0];
            for (double b : b0) if (b > s0[0]) { ancho = b - s0[0]; break; }
        }
        const double esperado = 500.0 + (double(ang) + 90.0) * 2000.0 / 180.0;
        check(cerca(per * 1e6, 20000.0, 1.0) && cerca(ancho * 1e6, esperado, 1.0),
              "el pulso: " + std::to_string(int(std::lround(ancho * 1e6))) + " us cada " +
              std::to_string(int(std::lround(per * 1e6))) + " us para " + std::to_string(ang) + " grados");
        check(cerca(banco_e11.servo.objetivo(), double(ang), 1.0), "y el SG90 va a " + std::to_string(ang) + " grados");
        c6.alimenta(false);
        banco_e11.servo.set_enabled(false);
        // 3. La captura del MCPWM, con un HC-SR04
        banco_e11.sonar_mcpwm.distancia_cm = 30.0;
        banco_e11.sonar_mcpwm.set_enabled(true);
        check(arranca_flash("build/idf-c6/v5.5/e11_mcpwm_capture_hc_sr04"), "mcpwm/mcpwm_capture_hc_sr04 (TRIG GPIO0, ECHO GPIO2)");
        check(espera_texto(terminal, "Measured distance: ", sc_time(3, SC_SEC)), "la captura mide el eco por los dos flancos");
        wait(10, SC_MS);
        const double d30 = ultimo_real(terminal, "Measured distance: ");
        // El ejemplo pasa los tics a us con esp_clk_apb_freq() (40 MHz en el
        // C6), pero la captura cuenta con el reloj del grupo (80 MHz,
        // mcpwm_capture_timer_get_resolution): en el chip, y aquí, sale el doble
        check(cerca(d30, 2 * 30.16, 0.1), "30 cm: " + std::to_string(d30) +
              " cm (1749 us; el ejemplo divide los tics de 80 MHz entre 40 MHz: el doble, como en el chip)");
        banco_e11.sonar_mcpwm.distancia_cm = 100.0;
        wait(1200, SC_MS);
        const double d100 = ultimo_real(terminal, "Measured distance: ");
        check(cerca(d100, 2 * 100.53, 0.1), "100 cm: " + std::to_string(d100) + " cm (el doble)");
        c6.alimenta(false);
        banco_e11.sonar_mcpwm.set_enabled(false);
        // 4. El ETM: el flanco de GPIO1 captura el GPTimer, sin la CPU
        banco_e11.sonar_gptimer.distancia_cm = 50.0;
        banco_e11.sonar_gptimer.set_enabled(true);
        const uint64_t t0 = c6.etm.tareas();
        check(arranca_flash("build/idf-c6/v5.5/e11_gptimer_capture_hc_sr04"), "timer_group/gptimer_capture_hc_sr04 (ECHO GPIO1)");
        check(espera_texto(terminal, "Measured distance: ", sc_time(3, SC_SEC)), "el ETM: GPIO_EVT_CH0_ANY_EDGE -> TIMER0_TASK_CNT_CAP");
        wait(10, SC_MS);
        const double d50 = ultimo_real(terminal, "Measured distance: ");
        check(cerca(d50, 50.26, 0.05) && c6.etm.tareas() > t0,
              "50 cm: " + std::to_string(d50) + " cm, con " + std::to_string(c6.etm.tareas() - t0) + " tareas del ETM");
        c6.alimenta(false);
        banco_e11.sonar_gptimer.set_enabled(false);
        // 5. La sincronía de tres temporizadores por GPIO5
        check(arranca_flash("build/idf-c6/v5.5/e11_mcpwm_sync"), "mcpwm/mcpwm_sync (GPIO0, 2 y 4; la sincronia por GPIO5)");
        check(espera_texto(terminal, "Force the output level to low", sc_time(3, SC_SEC)), "tres temporizadores, arrancados uno a uno");
        espera_texto(terminal, "Now the output PWMs should in sync", sc_time(1, SC_SEC));
        wait(5, SC_MS);
        banco_e11.empieza();
        wait(3, SC_MS);
        banco_e11.para();
        const auto a = banco_e11.sube(0), b = banco_e11.sube(2), c = banco_e11.sube(4);
        bool sinc = a.size() >= 2 && b.size() >= 2 && c.size() >= 2;
        for (size_t i = 0; sinc && i < std::min({a.size(), b.size(), c.size()}); ++i)
            sinc = cerca(a[i], b[i], 1e-9) && cerca(a[i], c[i], 1e-9);
        const auto ba = banco_e11.sube(0, false);
        const double alto0 = (!a.empty() && !ba.empty()) ? (ba.back() > a.front() ? ba[0] - a[0] : 0) : 0;
        check(sinc && cerca(a[1] - a[0], 1e-3, 1e-9) && cerca(alto0, 200e-6, 1e-8),
              "despues del pulso en GPIO5, los tres a la vez: 1 kHz, 200 us altos");
        c6.alimenta(false);
        // 6. motor (nuestra): tiempo muerto, freno por fallo, PCNT y ETM entre GPIO
        check(arranca_flash("build/idf-c6/v5.5/motor"), "motor: el MCPWM con tiempo muerto y freno, el PCNT y el ETM");
        check(espera_texto(terminal, "M2 pcnt ", sc_time(3, SC_SEC)), "M2");
        wait(5, SC_MS);
        const long np = ultimo_numero(terminal, "M2 pcnt ");
        // vTaskDelay(100 ms) son 10 tics de FreeRTOS: entre 90 y 100 ms
        check(np >= 900 && np <= 1001, "el PCNT cuenta las subidas de GPIO0 en los 10 tics de vTaskDelay(100 ms): " +
              std::to_string(np));
        check(espera_texto(terminal, "M3 etm en marcha", sc_time(1, SC_SEC)), "M3");
        wait(1, SC_MS);
        banco_e11.empieza();
        wait(2, SC_MS);
        banco_e11.para();
        {
            const auto sa = banco_e11.sube(0), ba2 = banco_e11.sube(0, false), sb = banco_e11.sube(2), bb = banco_e11.sube(2, false);
            // A sube 5 us después de que B baje; B, 5 us después de que A baje
            bool dt = sa.size() >= 5 && sb.size() >= 5;
            double ancho_a = 0, ancho_b = 0, muerto1 = 1, muerto2 = 1;
            if (dt) {
                for (double t : ba2) if (t > sa[1]) { ancho_a = t - sa[1]; break; }
                for (double t : bb) if (t > sb[1]) { ancho_b = t - sb[1]; break; }
                for (double t : bb) if (t < sa[2]) muerto1 = sa[2] - t;
                for (double t : ba2) if (t < sb[2]) muerto2 = sb[2] - t;
            }
            if (!check(dt && cerca(ancho_a, 25e-6, 1e-8) && cerca(ancho_b, 65e-6, 1e-8) && cerca(muerto1, 5e-6, 1e-8) &&
                       cerca(muerto2, 5e-6, 1e-8),
                       "el tiempo muerto: A 25 us y B (complementaria) 65 us altos, 5 us entre las dos"))
                std::printf("    A %.3f us, B %.3f us, B->A %.3f us, A->B %.3f us (%zu/%zu subidas)\n", ancho_a * 1e6,
                            ancho_b * 1e6, muerto1 * 1e6, muerto2 * 1e6, sa.size(), sb.size());
            const auto s5 = banco_e11.sube(5);
            check(s5.size() >= 2 && cerca(s5[1] - s5[0], 200e-6, 1e-8),
                  "el ETM: cada subida de GPIO0 invierte GPIO5 (GPIO_EVT -> GPIO_TASK_TOG): 2,5 kHz");
        }
        const int df = c6.gpio.nodo_de(4).register_driver("fallo_e11");
        c6.gpio.nodo_de(4).set_drive(df, 3.3f, 50.0f);
        check(espera_texto(terminal, "M4 freno", sc_time(1, SC_SEC)), "GPIO4 alto: el fallo 0 frena el operador (OST) y avisa");
        banco_e11.empieza();
        wait(1, SC_MS);
        banco_e11.para();
        check(banco_e11.flancos[0].empty() && banco_e11.flancos[2].empty() &&
              !BancoE11C6::alto(c6.gpio.nodo_de(0)) && !BancoE11C6::alto(c6.gpio.nodo_de(2)),
              "y las dos salidas se quedan a 0");
        c6.gpio.nodo_de(4).set_drive(df, 0.0f, 50.0f);
        if (!check(espera_texto(terminal, "M5 vuelve", sc_time(1, SC_SEC)), "GPIO4 bajo: CLR_OST, y el PWM vuelve"))
            std::printf("%s\n", terminal.substr(terminal.size() > 800 ? terminal.size() - 800 : 0).c_str());
        wait(1, SC_MS);
        banco_e11.empieza();
        wait(1, SC_MS);
        banco_e11.para();
        check(banco_e11.sube(0).size() >= 9, "otra vez a 10 kHz");
        c6.gpio.nodo_de(4).set_hiz(df);
        c6.alimenta(false);
    }

    void d15() {
        grupo("D15 la depuracion de los dos harts: unavail, parar el LP y el disparo cruzado (E10)");
        RvDmCliente cli_dm(*c6.dm);
        check(arranca_lp("lp_lp_timer_interrupt"), "lp_core/lp_timer_interrupt: el LP en wfi, con su alarma cada 500 ms");
        wait(5, SC_MS);
        check(cli_dm.activar() && cli_dm.n_harts() == 2, "el DM, por el DMI: dos harts");
        cli_dm.selecciona(1);
        check(!cli_dm.disponible(), "antes de que ESP-IDF lo arranque, el LP esta «unavail» (LPPERI_CPU, el reset en 1)");
        espera_texto(terminal, "LP core loaded with firmware and running successfully", sc_time(3, SC_SEC));
        cli_dm.selecciona(1);
        check(cli_dm.disponible(), "ulp_lp_core_run() lo deja depurable (LPCORE_DBGM_UNAVALIABLE = 0)");
        check(cli_dm.parar() && cli_dm.parado(), "haltreq al hart 1: el LP se para");
        uint32_t dpc = 0;
        cli_dm.lee_reg(RvDmCliente::CSR_DPC, dpc);
        check(dpc >= c6::LP_SRAM && dpc < c6::LP_SRAM + c6::LP_SRAM_TAM, "parado en la LP SRAM (dpc " + hex(dpc) + ")");
        cli_dm.selecciona(0);
        check(!cli_dm.parado(), "y el HP sigue corriendo");
        cli_dm.selecciona(1);
        check(cli_dm.seguir() && !cli_dm.parado(), "resumereq: el LP sigue");
        c6.dm->pon_disparo_cruzado(true);
        cli_dm.selecciona(1);
        cli_dm.parar();
        wait(1, SC_US);
        cli_dm.selecciona(0);
        check(cli_dm.parado(), "con el disparo cruzado, parar el LP para tambien el HP");
        cli_dm.seguir();
        cli_dm.selecciona(1);
        cli_dm.seguir();
        c6.dm->pon_disparo_cruzado(false);
        cli_dm.parar();
        wait(1, SC_US);
        cli_dm.selecciona(0);
        check(!cli_dm.parado(), "y sin el, no");
        cli_dm.selecciona(1);
        cli_dm.seguir();
        const std::string antes = terminal;
        wait(1200, SC_MS);
        check(ultimo_numero(terminal, "Interrupt count: ") >= 2, "los dos siguen: las interrupciones del LP timer cuentan (" +
              std::to_string(ultimo_numero(terminal, "Interrupt count: ")) + ")");
        c6.alimenta(false);
    }
    static std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "0x%08X", v); return b; }

    // TESTC6_SOLO="12,13": solo esos grupos (para depurar; la invariante es
    // la de la suite entera)
    static bool toca(unsigned g) {
        const char* e = std::getenv("TESTC6_SOLO");
        if (!e) return true;
        const std::string s = std::string(",") + e + ",";
        return s.find("," + std::to_string(g) + ",") != std::string::npos;
    }
    void run() {
        wait(SC_ZERO_TIME);
        if (toca(1)) d1();
        if (toca(2)) d2();
        if (toca(3)) d3();
        if (toca(4)) d4();
        if (toca(5)) d5();
        if (toca(6)) d6();
        if (toca(7)) d7();
        if (toca(8)) d8();
        if (toca(9)) d9();
        if (toca(12)) d12();
        if (toca(13)) d13();
        if (toca(14)) d14();
        if (toca(15)) d15();
        if (toca(16)) d16();
        if (toca(10)) d10();
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
        if (std::getenv("C6_DBG"))
            std::printf("\n[dbg] hp_dormido %d lp_dormido %d; lp_uart %.0f baudios (clkdiv 0x%X, sclk %.0f), %zu bytes enviados; LP %llu instrucciones", int(c6->pmu.hp_dormido()), int(c6->pmu.lp_dormido()),
                        c6->lp_uart.baudios(), c6->lp_uart.clkdiv(), c6->lp_uart.hz_fuente(), c6->lp_uart.salida.size(),
                        (unsigned long long)c6->core_lp.instrucciones());
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
    const unsigned long long pg =
        (unsigned long long)(tb.t_gdb.value() * (sc_get_time_resolution().to_seconds() / 1e-12));
    std::printf("  de los cuales D10-D11 (socket de GDB): %llu ps\n", pg);
    std::printf("  el resto                              : %llu ps\n", ps - pg);
    return g_fallos ? 1 : 0;
}
