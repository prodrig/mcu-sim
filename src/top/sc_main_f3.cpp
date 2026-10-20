// =============================================================================
// sc_main_f3.cpp — LA SUITE DEL STM32F303VC (fases S0-S3 del plan del F303)
//
// Un banco aparte, como el de cada chip: un chip más dentro de otra simulación
// movería su invariante. [doc/analisis_stm32f303.md, §7.1]
//
//   R1-R8   (S0) los rasgos nuevos, sobre piezas sueltas: el GPIO con BRR, el
//           EXTI de 36 líneas, el TIM de seis canales con su PWM combinado y
//           su segundo freno, el CRC de 16 bits, el IWDG con ventana, el SPI
//           con FIFO y empaquetado, el RTC sin CALIBR y con 16 respaldos, y la
//           CCM ejecutable y el DBGMCU como dato
//   F0      (S1) las imagenes de firmware son las versionadas
//   F1-F2   sin tiempo simulado: el catalogo, el encapsulado y sus funciones
//           alternativas (generados de la base de pines), la integracion
//   F3-F7   el arranque: la Flash con el HSI, la memoria de sistema (BOOT0),
//           la SRAM (BOOT0 y nBOOT1), MEM_MODE por firmware y la CCM
//           ejecutable con su proteccion (f3_demo)
//   F8-F10  el reloj: 72 MHz por el HSE en modo cristal y en bypass, y la
//           secuencia mala con sus avisos
//   F11-F12 el GPIO con BRR, el MCO en PA8, y el EXTI (B1 y la linea 33)
//   F13     CoreMark
//   F14-F15 GPIO_IOToggle y GPIO_EXTI de STM32CubeF3, sin modificar
//   F16-F17 (S2) el FPEC desde el firmware: borrar, programar medias
//           palabras, PGERR, LOCK, el error de bus de una palabra; los bytes
//           de opcion con OBL_LAUNCH (f3_demo, modos 8 y 9)
//   F18     FLASH_EraseProgram de STM32CubeF3, sin modificar
//   F19     GDB con `load` por la sonda de los pines (G2, PA13/PA14) y por el
//           DAP (G1), con el programador del FPEC (D2)
//   F20-F21 el FPEC, los bytes de opcion, RDP, el RCC y el SYSCFG por el DAP,
//           contra lo que dice RM0316
//
//   (S3) los ejemplos de STM32CubeF3 de §7.2, sin modificar, de
//   verif/fw/stm32f3/ejemplos_s3:
//   F22-F31 dos Discovery cableadas (TbDos): UART_TwoBoards por la USART2
//           (PD5/PD6, sondeo, IT y DMA), SPI_FullDuplex por la SPI3 (PC10-12)
//           e I2C_TwoBoards por el I2C1 (PB6/PB7, 10 bits) con sus pull-ups
//   F32-F39 los TIM: PWMOutput, DMA, TimeBase, Asymetric, InputCapture,
//           Combined, ComplementarySignals y PWMInput
//   F40-F42 IWDG_Reset, IWDG_WindowMode y WWDG_Example
//   F43-F46 RTC_Calendar y las bases de tiempos de la HAL sin SysTick (RTC
//           alarma, RTC despertador y TIM6)
//   F47-F53 PWR_STOP, PWR_STANDBY, PWR_PVD y PWR_CurrentConsumption en sus
//           cuatro modos (Sleep, Stop+RTC, Standby, Standby+RTC)
//
//   TESTF3_SOLO=s2 / s3 corre solo esos grupos, TESTF3_GRUPO=F44 solo los que
//   empiecen asi, TESTF3_TRAZA=1 imprime trazas de depuracion: con cualquiera
//   de ellos el tiempo simulado no es el del invariante.
//
// EL TIEMPO SIMULADO DE F19 NO ES REPRODUCIBLE entre maquinas, como el de E18
// en teste4: el stub atiende un socket de verdad. El banco imprime «el resto»,
// el total sin el, y eso es lo que contrasta `verif/invariantes.txt`.
//
//   make testf3   (o: make -f Makefile.mcu-sim testf3-build && ./build/testf3)
// =============================================================================
#include <systemc>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include "../common/asan_opciones.h"
#include "../common/huella_fw.h"
#include "../soc/stm32f3/stm32f3_mcu.h"
#include "../periph/timers.h"
#include "../periph/crc.h"
#include "../periph/watchdog.h"
#include "../periph/spi.h"
#include "../periph/rtc.h"
#include "../periph/dac.h"
#include "../periph/can.h"
#include "../bus/ahb_decoder.h"
#include "../verif/bus_test_master.h"
#include "../parts/ext_parts.h"
#include "../parts/netlist_xml.h"
#include "../verif/gdb_stub.h"
#include "../verif/gdb_client.h"
#include "../core/gdb_stub_dap.h"
#include <chrono>
#include <fstream>
#include <vector>

using namespace sc_core;
using namespace stm32;

static unsigned g_ok = 0, g_fallos = 0;
static void grupo(const char* t) { std::printf("\n--- %s ---\n", t); }
static bool check(bool c, const char* q) {
    if (c) { ++g_ok;  std::printf("  [OK  ] %s\n", q); }
    else   { ++g_fallos; std::printf("  [FALLO] %s\n", q); }
    return c;
}
static bool check_eq(uint64_t got, uint64_t exp, const char* q) {
    const bool c = (got == exp);
    if (c) std::printf("  [OK  ] %s\n", q);
    else   std::printf("  [FALLO] %s (obtenido 0x%llX, esperado 0x%llX)\n", q,
                       (unsigned long long)got, (unsigned long long)exp);
    if (c) ++g_ok; else ++g_fallos;
    return c;
}

// =============================================================================
// EL BANCO DE RASGOS (S0): las piezas con los rasgos del F303, sueltas, en un
// decodificador propio y con sus relojes de banco. Ninguna toca el SoC.
// =============================================================================
SC_MODULE(BancoRasgos) {
    BusTestMaster tm{"tm"};
    AhbDecoder    dec{"dec"};
    sc_signal<bool>   clk{"clk"}, uno{"uno"}, cero{"cero"};
    sc_signal<double> hz{"hz"}, lsi_hz{"lsi_hz"}, rtc_hz{"rtc_hz"}, i2s_hz{"i2s_hz"};
    // GPIO A del F3, con BRR
    GpioPort gpio{"gpio", 0, GPIO_F3};
    sc_vector<sc_signal<PadDrive>> g_drv{"g_drv", 16};
    sc_vector<sc_signal<bool>> g_din{"g_din", 16}, g_exti{"g_exti", 16};
    // EXTI de 36 líneas
    Exti exti{"exti", EXTI_F303};
    sc_vector<sc_signal<bool>> e_gpio{"e_gpio", N_GPIO_PORTS * N_PORT_PINS};
    sc_vector<sc_signal<uint8_t>> e_sel{"e_sel", 16};
    sc_vector<sc_signal<bool>> e_in{"e_in", 20}, e_lin{"e_lin", 20};
    sc_vector<sc_signal<bool>> e_irq{"e_irq", 7}, e_int{"e_int", 7};
    sc_signal<bool> e_evt{"e_evt"}, e_wk{"e_wk"};
    // TIM1 del F3: seis canales, OCxM de 4 bits, segundo freno, MMS2
    TimerBase tim{"tim", 0x40012C00u, CAPS_F3_TIM_ADV};
    sc_vector<sc_signal<bool>> t_itr{"t_itr", 4}, t_dcc{"t_dcc", 4};
    sc_signal<bool> t_g{"t_g"}, t_up{"t_up"}, t_cc{"t_cc"}, t_tc{"t_tc"}, t_brk{"t_brk"},
                    t_trgo{"t_trgo"}, t_dup{"t_dup"}, t_dtr{"t_dtr"}, t_dcom{"t_dcom"};
    // CRC programable, IWDG con ventana, SPI v3, RTC del F3
    CrcUnit crc{"crc", CRC_F3};
    Iwdg iwdg{"iwdg", IWDG_F3};
    sc_signal<bool> w_rst{"w_rst"}, w_lsi{"w_lsi"};
    SpiBase spi{"spi", 0x40013000u, CAPS_F3_SPI1};
    sc_signal<bool> s_irq{"s_irq"}, s_drx{"s_drx"}, s_dtx{"s_dtx"};
    Rtc rtc{"rtc", RTC_F3};
    sc_signal<bool> r_a{"r_a"}, r_t{"r_t"}, r_w{"r_w"};

    SC_CTOR(BancoRasgos) {
        tm.isk.bind(dec.tsk);
        uno.write(true); cero.write(false);
        hz.write(72e6); lsi_hz.write(40e3); rtc_hz.write(32768.0); i2s_hz.write(0);
        auto ata = [&](BusSlave& b) {
            dec.add_slave((std::string("a_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
            b.clk(clk); b.clk_hz(hz); b.rst_n(uno); b.clk_en(uno);
        };
        ata(gpio); ata(exti); ata(tim); ata(crc); ata(iwdg); ata(spi); ata(rtc);
        for (unsigned i = 0; i < 16; ++i) {
            gpio.pad_drive[i](g_drv[i]); gpio.pad_din[i](g_din[i]); gpio.exti_line[i](g_exti[i]);
        }
        for (unsigned k = 0; k < e_gpio.size(); ++k) exti.gpio_line[k](e_gpio[k]);
        for (unsigned l = 0; l < 16; ++l) exti.exticr_sel[l](e_sel[l]);
        for (unsigned i = 0; i < 20; ++i) { exti.linea_in[i](e_in[i]); exti.linea_irq[i](e_lin[i]); }
        exti.irq_exti0(e_irq[0]); exti.irq_exti1(e_irq[1]); exti.irq_exti2(e_irq[2]);
        exti.irq_exti3(e_irq[3]); exti.irq_exti4(e_irq[4]); exti.irq_exti9_5(e_irq[5]);
        exti.irq_exti15_10(e_irq[6]);
        exti.event_out(e_evt); exti.wakeup(e_wk);
        tim.timclk(clk); tim.timclk_hz(hz); tim.freeze(cero);
        for (unsigned i = 0; i < 4; ++i) { tim.itr[i](t_itr[i]); tim.dma_cc[i](t_dcc[i]); }
        tim.irq_global(t_g); tim.irq_up(t_up); tim.irq_cc(t_cc); tim.irq_trg_com(t_tc);
        tim.irq_brk(t_brk); tim.trgo(t_trgo); tim.dma_up(t_dup); tim.dma_trig(t_dtr);
        tim.dma_com(t_dcom);
        iwdg.lsi_clk(clk); iwdg.lsi_hz(lsi_hz); iwdg.rst_req(w_rst); iwdg.lsi_on_req(w_lsi);
        iwdg.freeze(cero); iwdg.hw_start(cero);
        spi.irq(s_irq); spi.dma_req_rx(s_drx); spi.dma_req_tx(s_dtx);
        spi.i2s_ext_clk(cero); spi.i2s_clk_hz(i2s_hz); spi.ext_ck(cero); spi.ext_ws(cero);
        rtc.rtcclk(clk); rtc.rtcclk_hz(rtc_hz); rtc.bkp_rst_n(uno); rtc.dbp(uno);
        rtc.exti17_alarm(r_a); rtc.exti21_tamp_ts(r_t); rtc.exti22_wakeup(r_w);
        SC_METHOD(lazo_spi); sensitive << spi.mosi_out;
    }
    // MOSI unido a MISO: lo que sale vuelve a entrar
    void lazo_spi() { spi.miso_in.write(spi.mosi_out.read()); }

    uint32_t rd(uint32_t a) { uint32_t v = 0; tm.read32(a, v); return v; }
    void wr(uint32_t a, uint32_t v) { tm.write32(a, v); }
};



// Un generador de onda rectangular con su ciclo de trabajo, para las entradas
// de los temporizadores (TIM_InputCapture, TIM_PWMInput). Con 0 Hz suelta el
// pin: montado en el banco desde el principio no molesta a nadie
SC_MODULE(GenPwm), public ExtPart {
    GenPwm(sc_module_name nm, analog_net_if& n)
        : sc_module(nm), ExtPart(n, "GenPwm", "gen", "out", nm) {
        SC_HAS_PROCESS(GenPwm);
        SC_THREAD(run);
    }
    void pon(double hz, double duty) { hz_ = hz; duty_ = duty; ev_.notify(SC_ZERO_TIME); }
private:
    double hz_ = 0, duty_ = 0.5;
    sc_event ev_;
    void run() {
        for (;;) {
            if (hz_ <= 0) { hiz(); wait(ev_); continue; }
            const double per = 1e12 / hz_;
            drive(3.3f, 50.0f);
            wait(sc_time(per * duty_, SC_PS), ev_);
            if (ev_.triggered()) continue;
            drive(0.0f, 50.0f);
            wait(sc_time(per * (1 - duty_), SC_PS), ev_);
        }
    }
};

// =============================================================================
// S3: DOS DISCOVERY, para los ejemplos *TwoBoards* de ST [doc/analisis_stm32f303.md
// §5.3, §7.2]. Los hilos son los de sus readme, cada uno un nodo que es un pad
// de cada chip:
//
//   UART   PD5 (USART2_TX) de cada una al PD6 (USART2_RX) de la otra
//   SPI3   PC10 (SCK), PC11 (MISO) y PC12 (MOSI), de una a otra
//   I2C1   PB6 (SCL) y PB7 (SDA), con sus pull-up de 4,7 kohm
//
// Las dos con los 8 MHz del ST-LINK en PF0 y B1 en PA0. La «a» es la que
// manda (TRANSMITTER_BOARD, MASTER_BOARD); la «b», la otra. Estan apagadas
// hasta que un grupo las enciende.
// =============================================================================
static const char* const DIR_S3 = "verif/fw/stm32f3/ejemplos_s3/";
static std::string fw_s3(const char* n) { return std::string(DIR_S3) + n + ".bin"; }

struct HilosDos {
    AnalogNet uart_ab{"h_uart_ab"}, uart_ba{"h_uart_ba"};
    AnalogNet sck{"h_sck"}, miso{"h_miso"}, mosi{"h_mosi"}, scl{"h_scl"}, sda{"h_sda"};
    Cableado ca, cb;
    HilosDos() {
        ca.une(3, 5, uart_ab); cb.une(3, 6, uart_ab);
        cb.une(3, 5, uart_ba); ca.une(3, 6, uart_ba);
        for (Cableado* c : {&ca, &cb}) {
            c->une(2, 10, sck); c->une(2, 11, miso); c->une(2, 12, mosi);
            c->une(1, 6, scl);  c->une(1, 7, sda);
        }
    }
};

SC_MODULE(TbDos) {
    HilosDos h;
    McuF3 a, b;
    ExtClock *mco_a = nullptr, *mco_b = nullptr;
    Button *b1_a = nullptr, *b1_b = nullptr;
    Rpull *rb_a = nullptr, *rb_b = nullptr, *pu_scl = nullptr, *pu_sda = nullptr;

    SC_CTOR(TbDos)
        : a("dos_a", DBG_PINES, h.ca, *CatalogoMcu::busca("STM32F303VC")),
          b("dos_b", DBG_PINES, h.cb, *CatalogoMcu::busca("STM32F303VC")) {
        mco_a = new ExtClock("mco_a", a.chip().pinmux.analog(5, 0), 8e6, 3.3, 50.0, false);
        mco_b = new ExtClock("mco_b", b.chip().pinmux.analog(5, 0), 8e6, 3.3, 50.0, false);
        b1_a = new Button(a.chip().pinmux.analog(0, 0), 430.0, 3.3); b1_a->release();
        b1_b = new Button(b.chip().pinmux.analog(0, 0), 430.0, 3.3); b1_b->release();
        rb_a = new Rpull(a.chip().pinmux.analog(0, 0), 0.0, 220e3);
        rb_b = new Rpull(b.chip().pinmux.analog(0, 0), 0.0, 220e3);
        pu_scl = new Rpull(h.scl, 3.3, 4700.0);
        pu_sda = new Rpull(h.sda, 3.3, 4700.0);
    }
    ~TbDos() override {
        delete mco_a; delete mco_b; delete b1_a; delete b1_b; delete rb_a; delete rb_b;
        delete pu_scl; delete pu_sda;
    }
    bool alto(McuF3& m, unsigned p, unsigned i) { return m.chip().pinmux.analog(p, i).voltage() > 1.65f; }
    // Las dos con su firmware; la «b» arranca primero, que es lo que piden los
    // readme («reset the Slave board first»)
    void arranca(const std::string& fa, const std::string& fb) {
        a.alimenta(true); b.alimenta(true);
        a.reset_pin(true); b.reset_pin(true);
        wait(20, SC_US);
        a.carga_firmware(fa); b.carga_firmware(fb);
        b.reset_pin(false);
        wait(1, SC_MS);
        a.reset_pin(false);
    }
    void pulsa(Button* x, double ms = 50) { x->press(); wait(ms, SC_MS); x->release(); }
    void apaga() { a.alimenta(false); b.alimenta(false); wait(1, SC_MS); }
    // Espera a que un pin se quede como se pide
    bool espera(McuF3& m, unsigned p, unsigned i, bool v, double ms) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (sc_time_stamp() < lim) {
            if (alto(m, p, i) == v) return true;
            wait(lim - sc_time_stamp(), m.chip().pinmux.analog(p, i).value_changed_event());
        }
        return alto(m, p, i) == v;
    }
};

static const char* const FW_DEMO   = "verif/fw/stm32f3/f3_demo/f3_demo.bin";
static const char* const FW_CM     = "verif/fw/stm32f3/coremark/coremark_f3.bin";
static const char* const FW_TOGGLE = "verif/fw/stm32f3/gpio_iotoggle/GPIO_IOToggle.bin";
static const char* const FW_EXTI   = "verif/fw/stm32f3/gpio_exti/GPIO_EXTI.bin";
static const char* const FW_FLASH  = "verif/fw/stm32f3/flash_eraseprogram/FLASH_EraseProgram.bin";
static sc_time g_t_gdb = SC_ZERO_TIME;

// El buzon de f3_demo [verif/fw/stm32f3/f3_demo/main.c, buzon_t]
enum : uint32_t {
    B_MODO = 0x00, B_ARRANQUES = 0x04, B_ESTADO = 0x08, B_IDCODE = 0x0C, B_FSIZE = 0x10,
    B_UID = 0x14, B_TSCAL = 0x20, B_VREFINT = 0x24, B_R0 = 0x28, B_R1 = 0x2C, B_R2 = 0x30,
    B_R3 = 0x34, B_PULSACIONES = 0x38, B_IRQ_COMP = 0x3C, B_CFSR = 0x40, B_BFAR = 0x44,
    B_FAULTS = 0x48, B_CFGR = 0x4C, B_CCM = 0x50, B_TAM = 0x60
};

SC_MODULE(TbF3) {
    BancoRasgos* r = nullptr;
    TbDos*  dos = nullptr;
    GenPwm* gen_e11 = nullptr;   // PE11: TIM1_CH2 (TIM_InputCapture)
    GenPwm* gen_b7  = nullptr;   // PB7: TIM4_CH2 (TIM_PWMInput)
    McuF3   mcu;
    SocF3&  dut;
    // La placa, lo justo: los 8 MHz del ST-LINK en PF0 (sin onda), B1 en PA0 a
    // VDD con su resistencia a masa
    ExtClock* mco = nullptr;
    Button*   b1  = nullptr;
    Rpull*    r_b1 = nullptr;
    // GDB: el stub de los pines (G2, en PA14/PA13) y el del DAP (G1)
    GdbStub*    gdb_p = nullptr;
    GdbStubDap* gdb_d = nullptr;
    GdbClient   cli;
    static constexpr unsigned PUERTO_PINES = 3451, PUERTO_DAP = 3452;

    SC_CTOR(TbF3)
        : mcu("dut", DBG_PINES, Cableado(), *CatalogoMcu::busca("STM32F303VC")),
          dut(mcu.chip()) {
        r = new BancoRasgos("rasgos");
        dos = new TbDos("dos");
        gen_e11 = new GenPwm("gen_e11", dut.pinmux.analog(4, 11));
        gen_b7  = new GenPwm("gen_b7", dut.pinmux.analog(1, 7));
        mco = new ExtClock("mco", dut.pinmux.analog(5, 0), 8e6, 3.3, 50.0, false);
        b1 = new Button(dut.pinmux.analog(0, 0), 330.0, 3.3);
        b1->release();
        r_b1 = new Rpull(dut.pinmux.analog(0, 0), 0.0, 220e3);
        gdb_p = new GdbStub("gdb_p", *mcu.nodo_swclk(), *mcu.nodo_swdio(), PUERTO_PINES, 2e6);
        gdb_p->set_enabled(false);
        gdb_p->pon_programador(mcu.programador());
        gdb_d = new GdbStubDap("gdb_d", dut.core.debug, PUERTO_DAP);
        gdb_d->set_enabled(false);
        gdb_d->pon_programador(mcu.programador());
        SC_THREAD(run);
    }
    ~TbF3() override { delete r; delete dos; delete gen_e11; delete gen_b7; delete mco; delete b1; delete r_b1; delete gdb_p; delete gdb_d; }

    // --- Utilidades -----------------------------------------------------------
    uint32_t buzon(uint32_t off) { return dut.sram.peek32(off); }
    analog_net_if& pin(unsigned p, unsigned i) { return dut.pinmux.analog(p, i); }
    bool alto(unsigned p, unsigned i) { return pin(p, i).voltage() > 1.65f; }
    void arranca(uint32_t modo, const char* fw = FW_DEMO) {
        mcu.reset_pin(true);
        wait(20, SC_US);
        if (fw) mcu.carga_firmware(fw);
        uint8_t ceros[B_TAM] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        dut.sram.poke32(B_MODO, modo);
        mcu.reset_pin(false);
    }
    // Los ejemplos de ST de S3, cada uno desde un ENCENDIDO: VBAT va con VDD
    // en la Discovery, y sin cortarla el dominio de respaldo -el RTC con su
    // despertador o su alarma, las banderas de PWR_CSR y de RCC_CSR- llega
    // del ejemplo anterior. PWR_STANDBY, detras de PWR_STOP, despertaba al
    // instante con el WUTF que este habia dejado puesto
    void arranca_s3(const std::string& fw) {
        mcu.alimenta(false);
        wait(1, SC_MS);
        mcu.alimenta(true);
        wait(50, SC_US);
        arranca(0, fw.c_str());
    }
    bool espera_estado(uint32_t e, double ms) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (sc_time_stamp() < lim) {
            if (buzon(B_ESTADO) == e) return true;
            wait(50, SC_US);
        }
        return buzon(B_ESTADO) == e;
    }
    double periodo(unsigned p, unsigned i, unsigned n = 4, double max_ms = 100) {
        const sc_time t_lim = sc_time_stamp() + sc_time(max_ms, SC_MS);
        sc_time primero = SC_ZERO_TIME, ultimo = SC_ZERO_TIME;
        unsigned flancos = 0;
        bool ant = alto(p, i);
        while (sc_time_stamp() < t_lim && flancos <= n) {
            wait(t_lim - sc_time_stamp(), pin(p, i).value_changed_event());
            const bool v = alto(p, i);
            if (v && !ant) {
                if (flancos == 0) primero = sc_time_stamp();
                ultimo = sc_time_stamp();
                ++flancos;
            }
            ant = v;
        }
        if (flancos < 2) return 0.0;
        return (ultimo - primero).to_seconds() / double(flancos - 1);
    }
    // Frecuencia de un pin contando flancos de subida durante `us`
    double frecuencia(unsigned p, unsigned i, double us) {
        const sc_time fin = sc_time_stamp() + sc_time(us, SC_US);
        unsigned n = 0;
        bool ant = alto(p, i);
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), pin(p, i).value_changed_event());
            const bool v = alto(p, i);
            if (v && !ant) ++n;
            ant = v;
        }
        return double(n) / (us * 1e-6);
    }
    static bool cerca(double v, double ref, double tol) { return std::fabs(v - ref) <= tol * ref; }

    // =======================================================================
    // R: los rasgos de S0
    // =======================================================================
    void r_gpio() {
        grupo("R1 GPIO del F3: en AHB2 y con BRR (0x28)");
        const uint32_t B = 0x48000000u;
        check(GPIO_F3.base == 0x48000000u && GPIO_F3.brr && !GPIO_F4.brr,
              "el rasgo: GPIOA en 0x4800_0000 y BRR; el F4 sin BRR");
        r->wr(B + 0x14, 0x0000FF0Fu);                  // ODR
        r->wr(B + 0x28, 0x00000F03u);                  // BRR: borra 0-1 y 8-11
        check_eq(r->rd(B + 0x14), 0x0000F00Cu, "BRR pone a cero los pines con un uno, y nada mas");
        check_eq(r->rd(B + 0x28), 0, "BRR es de solo escritura: lee cero");
        r->wr(B + 0x18, 0x00010002u);                  // BSRR: pone 1 y borra 0
        check_eq(r->rd(B + 0x14), 0x0000F00Eu, "BSRR sigue como siempre");
    }

    void r_exti() {
        grupo("R2 EXTI de 36 lineas, en dos tandas (IMR2..PR2)");
        const uint32_t E = 0x40010400u;
        check(EXTI_F303.n_lineas == 36 && EXTI_F4.n_lineas == 23, "36 lineas en el F303, 23 en el F4");
        check_eq(r->rd(E + 0x00), 0x1F800000u,
                 "IMR1 de reset: las directas (23-28) desenmascaradas, como en el silicio");
        check_eq(r->rd(E + 0x20), 0xFFFFFFFCu, "IMR2 de reset: 34 y 35 (UART4/5), y lo que no existe a uno");
        // La linea 33 (COMP7) por flanco de subida, en la segunda tanda
        r->wr(E + 0x28, 1u << 1);                       // RTSR2: linea 33
        r->wr(E + 0x20, 0xFFFFFFFEu | (1u << 1));       // IMR2: 33
        r->e_in[33 - 16].write(true);
        wait(1, SC_NS);
        check_eq(r->rd(E + 0x34) & 2u, 2u, "un flanco de subida en la linea 33 levanta PR2[1]");
        check(r->e_lin[33 - 16].read(), "y su salida hacia el NVIC (COMP7_IRQn)");
        r->wr(E + 0x34, 2u);
        wait(1, SC_NS);
        check(!(r->rd(E + 0x34) & 2u) && !r->e_lin[33 - 16].read(), "PR2 se borra escribiendo un uno");
        // Una directa (25, USART1) no tiene flanco ni PR, solo despierta
        r->wr(E + 0x08, 1u << 25);                      // RTSR1: no se deja
        check_eq(r->rd(E + 0x08) & (1u << 25), 0, "una linea directa no tiene detector de flanco");
        r->e_in[25 - 16].write(true);
        wait(1, SC_NS);
        check(!(r->rd(E + 0x14) & (1u << 25)) && r->e_wk.read(),
              "y levantada no deja nada en PR, pero despierta (IMR1[25])");
        r->e_in[25 - 16].write(false);
        r->e_in[33 - 16].write(false);
        // SWIER2 en la 32 (COMP6)
        r->wr(E + 0x20, 0xFFFFFFFDu);                   // IMR2: 32 y no 33
        r->wr(E + 0x30, 1u);                            // SWIER2: 32
        wait(1, SC_NS);
        check(r->rd(E + 0x34) & 1u, "SWIER2 en la 32 levanta PR2[0]");
        r->wr(E + 0x34, 1u);
        check_eq(r->rd(E + 0x30) & 1u, 0, "y borrar PR2 borra SWIER2");
        check_eq(r->rd(E + 0x38), 0, "detras de PR2 no hay nada");
    }

    void r_tim() {
        grupo("R3 TIM1 del F3: seis canales, OCxM de 4 bits, BK2, MMS2, UIFREMAP y SMS[3]");
        const uint32_t T = 0x40012C00u;
        check(CAPS_F3_TIM_ADV.channels == 6 && CAPS_F3_TIM_ADV.ocm_4bits &&
              CAPS_F3_TIM_ADV.break2 && !CAPS_TIM_ADV.ocm_4bits && CAPS_TIM_ADV.channels == 4,
              "el rasgo: 6 canales y lo de v3 en el F303; el F4, 4 canales y nada de v3");
        check(CAPS_F3_TIM15.channels == 2 && CAPS_F3_TIM15.comp_channels == 1 &&
              CAPS_F3_TIM16.channels == 1 && CAPS_F3_TIM16.bdtr && !CAPS_F3_TIM16.trgo,
              "TIM15: 2 canales, uno complementario; TIM16/17: 1 canal con freno y sin TRGO");
        r->wr(T + 0x58, 0xE0001234u);                   // CCR5 con GC5C1..3
        check_eq(r->rd(T + 0x58), 0xE0001234u, "CCR5 guarda el valor y GC5C1-3 (31:29)");
        r->wr(T + 0x5C, 0x5678u);
        check_eq(r->rd(T + 0x5C), 0x5678u, "CCR6");
        r->wr(T + 0x58, 0); r->wr(T + 0x5C, 0);
        r->wr(T + 0x54, 0xFFFFFFFFu);
        check_eq(r->rd(T + 0x54), 0x0101FCFCu, "CCMR3: OC5/OC6 FE, PE, M (con M[3]) y CE, sin CCxS");
        r->wr(T + 0x54, 0);
        r->wr(T + 0x18, 0x00010000u);                   // CCMR1: OC1M[3]
        check_eq(r->rd(T + 0x18), 0x00010000u, "CCMR1 deja escribir OC1M[3] (bit 16)");
        r->wr(T + 0x44, 0xFFFFFFFFu);
        check_eq(r->rd(T + 0x44), 0x03FFFFFFu, "BDTR: BKF, BK2F, BK2E y BK2P");
        r->wr(T + 0x44, 0);
        r->wr(T + 0x04, 0xFFFFFFFFu);
        check_eq(r->rd(T + 0x04) & 0x00F00000u, 0x00F00000u, "CR2.MMS2 (23:20)");
        r->wr(T + 0x04, 0);
        r->wr(T + 0x08, 0x00010000u);
        check_eq(r->rd(T + 0x08), 0x00010000u, "SMCR.SMS[3]");
        r->wr(T + 0x08, 0);
        // El PWM combinado: CH1 en modo 12 (OC1REF OR OC2REF)
        r->wr(T + 0x2C, 99);                            // ARR
        r->wr(T + 0x34, 20);                            // CCR1
        r->wr(T + 0x38, 60);                            // CCR2
        r->wr(T + 0x18, (0x4u << 4) | 0x00010000u | (0x6u << 12));   // OC1M = 1100, OC2M = PWM1
        r->wr(T + 0x20, 1u);                            // CC1E
        r->wr(T + 0x44, 1u << 15);                      // MOE
        r->wr(T + 0x00, 1u);                            // CEN
        wait(sc_time(0.5, SC_US));                      // CNT ~ 36
        const bool alto = r->tim.ch_out[0].read();
        wait(sc_time(0.5, SC_US));                      // CNT ~ 72
        const bool bajo = !r->tim.ch_out[0].read();

        check(alto && bajo, "PWM combinado 1: CH1 alto mientras OC1REF O OC2REF (CNT < 60), "
              "bajo despues");
        // GC5C1: CH1 = OC1REFC AND OC5REF. Con OC5 en PWM 1 y CCR5 = 30, en CNT ~ 36 cae
        r->wr(T + 0x54, 0x6u << 4);                     // OC5M = PWM 1
        r->wr(T + 0x58, (1u << 29) | 30u);              // GC5C1, CCR5 = 30
        r->wr(T + 0x00, 0);
        r->wr(T + 0x24, 0);                             // CNT = 0
        r->wr(T + 0x00, 1u);
        wait(sc_time(0.1, SC_US));
        const bool g_alto = r->tim.ch_out[0].read();      // CNT ~ 7: OC1REFC y OC5REF
        wait(sc_time(0.4, SC_US));                      // CNT ~ 36: OC5REF ya no
        const bool g_bajo = !r->tim.ch_out[0].read();
        check(g_alto && g_bajo, "GC5C1: el canal 1 sale AND con OC5REF (el canal sin pin)");
        r->wr(T + 0x58, 0); r->wr(T + 0x54, 0);
        r->wr(T + 0x00, 0);
        // UIFREMAP: UIF en CNT[31]
        r->wr(T + 0x00, 1u << 11);
        r->wr(T + 0x14, 1u);                            // UG -> UIF
        check(r->rd(T + 0x24) & 0x80000000u, "con UIFREMAP, CNT[31] es la copia de UIF");
        r->wr(T + 0x10, 0);
        check_eq(r->rd(T + 0x24) & 0x80000000u, 0, "y se va al borrar UIF");
        r->wr(T + 0x00, 0);
        // El segundo freno
        r->wr(T + 0x44, (1u << 15) | (1u << 24) | (1u << 25));   // MOE, BK2E, BK2P = 1
        r->tim.bkin2_in.write(true);
        wait(1, SC_NS);
        check((r->rd(T + 0x10) & (1u << 8)) && !(r->rd(T + 0x44) & (1u << 15)),
              "BKIN2 activo: B2IF y MOE a cero");
        r->tim.bkin2_in.write(false);
        r->wr(T + 0x10, 0);
        r->wr(T + 0x44, 0);
        r->wr(T + 0x18, 0); r->wr(T + 0x20, 0);
    }

    void r_crc() {
        grupo("R4 CRC programable: CRC-16/ARC (poly 0x8005, entrada y salida reflejadas)");
        const uint32_t C = 0x40023000u;
        check(CRC_F3.polinomio_programable && !CRC_F4.polinomio_programable,
              "el rasgo: programable en el F303, fijo en el F4");
        r->wr(C + 0x14, 0x8005u);                       // POL
        r->wr(C + 0x10, 0);                             // INIT
        r->wr(C + 0x08, (1u << 3) | (1u << 5) | (1u << 7) | 1u);   // 16 bits, REV_IN byte, REV_OUT, RESET
        const char* s = "123456789";
        for (const char* p = s; *p; ++p) r->tm.write8(C, uint8_t(*p));
        check_eq(r->rd(C) & 0xFFFFu, 0xBB3Du, "\"123456789\" byte a byte: 0xBB3D, el valor de control");
        check_eq(r->rd(C + 0x08), (1u << 3) | (1u << 5) | (1u << 7), "CR se lee (sin RESET)");
        // El de siempre, CRC-32/MPEG-2, palabra a palabra con los valores de reset
        r->wr(C + 0x14, 0x04C11DB7u); r->wr(C + 0x10, 0xFFFFFFFFu);
        r->wr(C + 0x08, 1u);
        r->wr(C, 0x12345678u);
        check_eq(r->rd(C), crc32_word(0xFFFFFFFFu, 0x12345678u),
                 "con POLYSIZE 32 y sin reflejar es el CRC del F4, palabra a palabra");
        r->tm.write16(C, 0xABCDu);
        check_eq(r->rd(C), crc_bits(crc32_word(0xFFFFFFFFu, 0x12345678u), 0xABCDu, 16, 0x04C11DB7u, 32),
                 "y una escritura de 16 bits es un dato de 16 bits");
        r->wr(C + 0x10, 0x13579BDFu);
        check_eq(r->rd(C), 0x13579BDFu, "escribir INIT carga ya DR, sin RESET [RM0316 §6.3.3]");
    }

    void r_iwdg() {
        grupo("R5 IWDG con ventana: recargar antes de tiempo es un reset");
        const uint32_t W = 0x40003000u;
        r->wr(W, 0xCCCCu);                              // arranca
        r->wr(W, 0x5555u);
        r->wr(W + 0x04, 0);                             // /4: 0,1 ms por cuenta a 40 kHz
        r->wr(W + 0x08, 1000);
        r->wr(W + 0x10, 500);                           // WINR
        check_eq(r->rd(W + 0x10), 500, "WINR se escribe con la llave");
        check(r->rd(W + 0x0C) & 4u, "y WVU anuncia el cambio");
        wait(1, SC_MS);
        check_eq(r->rd(W + 0x0C), 0, "WVU baja sola");
        wait(10, SC_MS);                                // ~ 890: por encima de la ventana
        r->wr(W, 0xAAAAu);
        wait(1, SC_NS);
        check(r->w_rst.read() && r->iwdg.fuera_de_ventana() == 1,
              "una recarga con el contador por encima de WINR pide reset");
        wait(60, SC_MS);                                // ~ 290: dentro de la ventana
        const unsigned antes = r->iwdg.fuera_de_ventana();
        r->wr(W, 0xAAAAu);
        wait(1, SC_NS);
        check(r->iwdg.fuera_de_ventana() == antes && r->iwdg.counter() > 900,
              "y dentro de la ventana recarga como siempre");
    }

    void r_spi() {
        grupo("R6 SPI v3: DS en CR2, FIFO de 4 bytes y empaquetado por anchura de acceso");
        const uint32_t S = 0x40013000u;
        check(CAPS_F3_SPI1.fifo_v3 && !CAPS_SPI_APB2.fifo_v3, "el rasgo: FIFO en el F303, no en el F4");
        check_eq(r->rd(S + 0x04), 0x0700u, "CR2 de reset: DS = 0111, 8 bits");
        check_eq(r->rd(S + 0x08) & 0x1802u, 0x0002u, "SR de reset: TXE y la FIFO vacia");
        // Maestro, 8 bits, SSM/SSI, BR = /2, FRXTH = 1 (RXNE con un byte)
        r->wr(S + 0x04, 0x0700u | (1u << 12));
        r->wr(S + 0x00, (1u << 2) | (1u << 8) | (1u << 9) | (1u << 6));   // MSTR, SSI, SSM, SPE
        r->tm.write16(S + 0x0C, 0xB2A1u);               // DOS datos de 8 bits
        check_eq((r->rd(S + 0x08) >> 11) & 3u, 1u, "una escritura de 16 bits con DS = 8: FTLVL = 1/4... "
                 "ya ha salido uno al desplazador");
        wait(5, SC_US);
        check_eq((r->rd(S + 0x08) >> 9) & 3u, 2u, "y en lazo vuelven los dos: FRLVL = 1/2 (dos bytes)");
        uint8_t b0 = 0, b1 = 0;
        r->tm.read8(S + 0x0C, b0);
        check(r->rd(S + 0x08) & 1u, "leidos 8 bits, queda uno: RXNE sigue a uno");
        r->tm.read8(S + 0x0C, b1);
        check(b0 == 0xA1 && b1 == 0xB2, "cada lectura de 8 bits saca un dato, en orden");
        check_eq(r->rd(S + 0x08) & 1u, 0, "y la FIFO queda vacia");
        // DS = 12 bits: un dato son dos bytes en la FIFO
        r->wr(S + 0x00, 0);
        r->wr(S + 0x04, 0x0B00u);                       // DS = 1011
        r->wr(S + 0x00, (1u << 2) | (1u << 8) | (1u << 9) | (1u << 6));
        r->tm.write16(S + 0x0C, 0x0ABCu);
        wait(5, SC_US);
        uint16_t h = 0;
        r->tm.read16(S + 0x0C, h);
        check_eq(h, 0x0ABCu, "con DS = 12, un dato de 12 bits ida y vuelta");
        r->wr(S + 0x00, 0);
        r->wr(S + 0x04, 0x0100u);                       // DS = 0001, «no usado»
        check_eq((r->rd(S + 0x04) >> 8) & 0xFu, 7u, "un DS no usado se queda en 0111 [RM0316 §30.9.2]");
    }

    void r_rtc_ccm() {
        grupo("R7 RTC sin CALIBR y con 16 respaldos; DAC, CAN y DBGMCU como dato");
        const uint32_t R = 0x40002800u;
        r->wr(R + 0x24, 0xCA); r->wr(R + 0x24, 0x53);
        r->wr(R + 0x18, 0x81u);
        check_eq(r->rd(R + 0x18), 0, "CALIBR (0x18) no existe en el F303: lee cero");
        r->wr(R + 0x50 + 4 * 15, 0xCAFEu);
        r->wr(R + 0x50 + 4 * 16, 0xBEEFu);
        check(r->rd(R + 0x50 + 4 * 15) == 0xCAFEu && r->rd(R + 0x50 + 4 * 16) == 0,
              "BKP15R es el ultimo: BKP16R no existe");
        check(std::string(CAPS_DAC_F303.tsel[3]) == "TIM15_TRGO" &&
              std::string(CAPS_DAC_F407.tsel[3]) == "TIM5_TRGO",
              "la tabla de disparos del DAC es dato: TSEL = 011 es TIM15 en el F303 y TIM5 en el F4");
        check(CAPS_CAN_F303.filter_banks == 14 && !CAPS_CAN_F303.shared_filters && CAPS_CAN_F303.ttcm,
              "el bxCAN del F303: un solo controlador con sus 14 bancos");
        check(MCU_STM32F303VC.idcode() == 0x10030422u && FZ_APB2_F303[2] == 2 &&
              FZ_APB1_F303[FZ3_CAN] == 25,
              "DBGMCU: IDCODE 0x1003_0422 y la tabla de congelacion del F303 (TIM15 en APB2_FZ[2])");
        const MapaRam m = ram_f303(MCU_STM32F303VC);
        check(m.ccm_ejecutable && m.ccm_base == 0x10000000u && m.ccm_size == 0x2000u &&
              !RAM_STM32F407VG.ccm_ejecutable,
              "la CCM del F303 es ejecutable (8 KB en 0x1000_0000); la del F4, no");
    }

    // =======================================================================
    // F: el STM32F303VC (S1)
    // =======================================================================
    void firmwares() {
        grupo("F0 Imagenes de firmware [verif/fw/huellas.txt]");
        const huella_fw::Veredicto v =
            huella_fw::verifica("verif/fw/huellas.txt", "verif/fw", "f3");
        std::printf("         %d imagenes: %s\n", v.comprobadas, v.detalle.c_str());
        check(v.ok(), "las imagenes de la suite del STM32F303 son las versionadas");
    }

    void catalogo() {
        grupo("F1 El catalogo, el encapsulado y las funciones alternativas, generados");
        const McuDesc* d = CatalogoMcu::busca("stm32f303vc");
        check(d && std::string(d->familia) == "STM32F3" && FabricaMcu::conoce("STM32F3"),
              "STM32F303VC esta en el catalogo, de la familia STM32F3, y la factoria la conoce");
        const Encapsulado& e = ENC_LQFP100_F303;
        check(e.n_gpio == 87 && e.coherente() && N_PATILLAS_F303 == 100,
              "LQFP100: 100 patillas y 87 E/S, y la mascara cuadra");
        check(e.bonded(5, 0) && e.bonded(5, 1) && e.bonded(5, 10) && !e.bonded(5, 3) &&
              !e.bonded(7, 0) && !e.bonded(6, 0),
              "PF0, PF1 y PF10 salen; PF3, PH0 y el puerto G no");
        // Las ocho posiciones que no son las del F407 [doc/analisis_stm32f303.md §0]
        const unsigned pos[8] = {10, 11, 12, 13, 19, 27, 49, 73};
        const char* nom[8] = {"PF9", "PF10", "PF0-OSC_IN", "PF1-OSC_OUT", "PF2", "PF4", "VSS", "PF6"};
        bool ok = true;
        for (unsigned k = 0; k < 8; ++k)
            ok = ok && PATILLAS_F303[pos[k] - 1].pos == pos[k] &&
                 std::string(PATILLAS_F303[pos[k] - 1].nombre) == nom[k];
        check(ok, "las ocho posiciones distintas del F407: 10, 11, 12, 13, 19, 27, 49 y 73");
        const auto& t = SocF3::tabla_af();
        auto tiene = [&](unsigned p, unsigned i, unsigned af, const char* sn) {
            for (const SocF3::PinAf& x : t)
                if (x.puerto == p && x.pin == i && x.af == af && std::string(x.senal) == sn) return true;
            return false;
        };
        unsigned eventout = 0;
        for (const SocF3::PinAf& x : t) if (std::string(x.senal) == "EVENTOUT") ++eventout;
        check(t.size() == 399, "399 funciones alternativas en los pines que salen");
        check(tiene(0, 8, 6, "TIM1_CH1") && tiene(4, 9, 2, "TIM1_CH1") && !tiene(0, 8, 1, "TIM1_CH1"),
              "TIM1_CH1 es AF6 en PA8 y AF2 en PE9; NO el AF1 del F4");
        check(tiene(2, 4, 7, "USART1_TX") && tiene(0, 5, 5, "SPI1_SCK") && tiene(1, 6, 4, "I2C1_SCL"),
              "el VCP (USART1_TX en PC4, AF7), SPI1_SCK en PA5 (AF5), I2C1_SCL en PB6 (AF4)");
        check(tiene(0, 13, 0, "SYS_JTMS-SWDIO") && tiene(0, 14, 0, "SYS_JTCK-SWCLK") &&
              tiene(0, 8, 0, "RCC_MCO"), "SWD y MCO en AF0");
        check(eventout == 82, "y los 82 EVENTOUT (AF15) de la base");
        check(dut.pinmux.tiene_af(0, 13, 0) && dut.pinmux.tiene_af(0, 8, 0) &&
              dut.pinmux.tiene_af(1, 9, 15),
              "el SoC conecta los de AF0 y EVENTOUT");
    }

    void integracion() {
        grupo("F2 La integracion del nucleo");
        check(dut.core.nucleo.n_irq == 82 && dut.core.nucleo.prio_bits == 4 &&
              dut.core.nucleo.hay_mpu() && dut.core.nucleo.hay_fpu(),
              "82 lineas, 4 bits de prioridad, MPU y FPU");
        check(!INTEGRACION_F303.alias_arranque,
              "el alias de 0x0 no es el del nucleo: lo resuelve el SoC con MEM_MODE");
        check(dut.core.debug.idcode() == 0x10030422u, "DBGMCU_IDCODE para el depurador: 0x1003_0422");
        check(f3::latencia_minima(24e6) == 0 && f3::latencia_minima(48e6) == 1 &&
              f3::latencia_minima(72e6) == 2, "la tabla de LATENCY: 0 hasta 24 MHz, 2 a 72");
    }

    void arranque_flash() {
        grupo("F3 Arranca de la Flash con el HSI a 8 MHz (f3_demo, modo 0)");
        arranca(0);
        check(espera_estado(100, 20), "f3_demo llega a su bucle");
        check(buzon(B_ARRANQUES) == 1 && dut.mem_mode() == 0, "un arranque, con MEM_MODE = 00 (la Flash)");
        check_eq(buzon(B_IDCODE), 0x10030422u, "DBGMCU_IDCODE desde el firmware: DEV_ID 0x422, REV_ID 0x1003");
        check_eq(buzon(B_FSIZE), 256, "F_SIZE: 256 KB");
        check(buzon(B_UID) == f3::UID[0] && buzon(B_UID + 4) == f3::UID[1] && buzon(B_UID + 8) == f3::UID[2],
              "el UID en 0x1FFF_F7AC");
        check(buzon(B_TSCAL) == (f3::TS_CAL1 | (uint32_t(f3::TS_CAL2) << 16)) &&
              buzon(B_VREFINT) == f3::VREFINT_CAL,
              "TS_CAL1, TS_CAL2 y VREFINT_CAL donde los busca la HAL");
        check(cerca(dut.f_sysclk(), 8e6, 1e-9), "SYSCLK = 8 MHz: el HSI");
        const double t = periodo(4, 9);
        std::printf("    periodo de LD3 (PE9): %.3f ms\n", t * 1e3);
        check(cerca(t, 20e-3, 0.01), "LD3 parpadea cada 20 ms con el SysTick a 8 MHz");
    }

    void arranque_sistema() {
        grupo("F4 Con BOOT0 alto arranca la memoria de sistema");
        mcu.reset_pin(true);
        mcu.boot0(true);
        wait(20, SC_US);
        dut.sram.poke32(0, 0);
        mcu.reset_pin(false);
        wait(200, SC_US);
        check(dut.mem_mode() == 1, "MEM_MODE = 01 al salir de reset");
        check_eq(dut.sram.peek32(0), 0x5359534Du, "el arranque de la memoria de sistema deja 'MSYS'");
        check(dut.sysmem.peek32(f3::SYSMEM_B) == 0x20001000u, "con su pila en 0x2000_1000");
        mcu.reset_pin(true);
        mcu.boot0(false);
        wait(20, SC_US);
    }

    void arranque_sram() {
        grupo("F5 Con BOOT0 alto y nBOOT1 a cero arranca la SRAM");
        dut.flash.pon_ob(1, 0xEF);                     // USER sin nBOOT1 (bit 4)
        mcu.alimenta(false);
        wait(100, SC_US);
        mcu.boot0(true);
        mcu.alimenta(true);
        // El programa, en la SRAM: pila, vector y 'MSRA' en 0x2000_0400
        const uint32_t prog[7] = {0x20002000u, 0x20000009u, 0x49034802u, 0xBF206001u,
                                  0x0000E7FDu, 0x20000400u, 0x4152534Du};
        for (unsigned k = 0; k < 7; ++k) dut.sram.poke32(4 * k, prog[k]);
        dut.sram.poke32(0x400, 0);
        wait(50, SC_US);
        mcu.reset_pin(false);
        wait(2, SC_MS);
        check(dut.sram.peek32(0x400) == 0,
              "a los 2 ms del encendido el chip sigue en reset: t_RSTTEMPO, 2,5 ms [DS9118, tabla 26]");
        wait(1, SC_MS);
        check(!dut.flash.nboot1(), "tras el encendido, FLASH_OBR dice nBOOT1 = 0");
        check(dut.mem_mode() == 3, "MEM_MODE = 11: la SRAM en 0x0");
        check_eq(dut.sram.peek32(0x400), 0x4152534Du, "y el programa de la SRAM corre: 'MSRA'");
        mcu.reset_pin(true);
        mcu.boot0(false);
        dut.flash.pon_ob(1, 0xFF);
        mcu.alimenta(false);
        wait(100, SC_US);
        mcu.alimenta(true);
        wait(50, SC_US);
    }

    void mem_mode() {
        grupo("F6 MEM_MODE desde el firmware: la Flash, la memoria de sistema y la SRAM en 0x0 (modo 7)");
        arranca(7);
        check(espera_estado(100, 20), "f3_demo termina");
        check_eq(buzon(B_R0), 0x2000A000u, "con MEM_MODE = 00, en 0x0 esta la Flash: su _estack");
        check_eq(buzon(B_R1), 0x20001000u, "con 01, la memoria de sistema");
        check_eq(buzon(B_R2), 1u, "con 11, la SRAM: en 0x4 se lee buzon.arranques");
        check_eq(buzon(B_R3) & 3u, 0u, "y vuelve a la Flash");
    }

    void ccm() {
        grupo("F7 La CCM es ejecutable, y SYSCFG_RCR la protege (modo 6)");
        arranca(6);
        check(espera_estado(200, 20), "f3_demo acaba en el BusFault que se esperaba");
        uint32_t a = 0;
        for (uint32_t i = 0; i < 10; ++i) a = a * 3u + i;
        check_eq(buzon(B_CCM), a, "una rutina copiada a la CCM devuelve lo que tiene que devolver");
        check(buzon(B_R1) >= 0x10000000u && buzon(B_R1) < 0x10002000u,
              "y su PC estaba en la CCM: se ejecuto desde alli");
        check_eq(buzon(B_R0), 0x10000001u, "en_ccm esta en 0x1000_0000 (Thumb)");
        check_eq(buzon(B_R2), 0x80u, "SYSCFG_RCR: la pagina 7 protegida");
        check_eq(buzon(B_R3), 0x5A5A5A5Au, "la pagina 6 se escribe");
        check(buzon(B_FAULTS) == 1 && dut.escrituras_ccm_protegidas() == 1,
              "la pagina 7 no: error de bus, y el modelo lo cuenta");
    }

    void hse(unsigned modo, const char* g, const char* que) {
        grupo(g);
        const unsigned av0 = dut.avisos();
        arranca(modo);
        check(espera_estado(100, 50), "f3_demo llega a 72 MHz");
        check(cerca(dut.f_sysclk(), 72e6, 1e-9) && cerca(dut.rcc.f_pclk1(), 36e6, 1e-9) &&
              cerca(dut.rcc.f_pclk2(), 72e6, 1e-9),
              "SYSCLK 72 MHz, PCLK1 36 MHz y PCLK2 72 MHz");
        check((buzon(B_CFGR) & 0xCu) == 0x8u && (buzon(B_R1) & (1u << 17)), que);
        check_eq(buzon(B_R2) & 7u, 2u, "FLASH_ACR.LATENCY = 2");
        const double t = periodo(4, 8);
        std::printf("    periodo de LD4 (PE8): %.3f ms\n", t * 1e3);
        check(cerca(t, 20e-3, 0.01), "LD4 cada 20 ms con el SysTick a 72 MHz");
        check(dut.avisos() == av0, "y sin un aviso");
    }

    void mala() {
        grupo("F10 La secuencia MALA (modo 3): 72 MHz con LATENCY 0 y APB1 sin dividir");
        const unsigned a0 = dut.rcc.avisos(), f0 = dut.flash.avisos();
        arranca(3);
        check(espera_estado(100, 50), "el firmware sigue (el modelo no corrompe nada: avisa)");
        check(dut.flash.avisos() > f0, "aviso de la Flash: 72 MHz con LATENCY = 0");
        check(dut.rcc.avisos() > a0, "aviso del RCC: PCLK1 a 72 MHz, por encima de 36");
    }

    void gpio_mco() {
        grupo("F11 GPIO con BRR, y el MCO en PA8 (modo 4)");
        arranca(4);
        check(espera_estado(101, 20), "f3_demo saca el HSI por el MCO");
        check_eq(buzon(B_R0) & 0xFF00u, 0xFF00u, "BSRR pone PE8-PE15");
        check_eq(buzon(B_R1) & 0xFF00u, 0xAA00u, "BRR quita PE8, PE10, PE12 y PE14");
        check_eq(buzon(B_R2), 0u, "BRR lee cero");
        check(alto(4, 9) && !alto(4, 8), "y los pines lo siguen");
        const double f1 = frecuencia(0, 8, 10.0);
        std::printf("    MCO = %.3f MHz\n", f1 / 1e6);
        check(cerca(f1, 8e6, 0.02), "PA8 da los 8 MHz del HSI");
        check(espera_estado(102, 20), "y luego PLL/2");
        const double f2 = frecuencia(0, 8, 5.0);
        std::printf("    MCO = %.3f MHz\n", f2 / 1e6);
        check(cerca(f2, 36e6, 0.02), "PA8 da 36 MHz: PLL (72) / 2");
        arranca(99);
        wait(100, SC_US);
    }

    void exti() {
        grupo("F12 EXTI: B1 (PA0) por flanco de subida, y la linea 33 por SWIER2 (modo 5)");
        arranca(5);
        check(espera_estado(100, 20), "f3_demo espera");
        check_eq(buzon(B_IRQ_COMP), 1u, "SWIER2 en la linea 33 llega al NVIC como COMP7_IRQn (66)");
        check_eq(buzon(B_R0), 0xFFFFFFFEu, "IMR2 con la 33 (y lo reservado a uno)");
        for (unsigned k = 0; k < 3; ++k) {
            b1->press();  wait(2, SC_MS);
            b1->release(); wait(2, SC_MS);
        }
        check_eq(buzon(B_PULSACIONES), 3u, "tres pulsaciones de B1, tres EXTI0");
        check(alto(4, 9), "y LD3 conmutado tres veces: encendido");
    }

    void coremark() {
        grupo("F13 CoreMark sobre el STM32F303VC");
        mcu.reset_pin(true);
        wait(20, SC_US);
        check(mcu.carga_firmware(FW_CM), "coremark_f3.bin cargado en 0x0800_0000");
        uint8_t ceros[16] = {};
        dut.sram.load(ceros, sizeof ceros, 0);
        const uint64_t i0 = dut.core.cpu.inst_count;
        mcu.reset_pin(false);
        const sc_time t0 = sc_time_stamp();
        bool fin = false;
        while (sc_time_stamp() - t0 < sc_time(8000, SC_MS)) {
            wait(2, SC_MS);
            if (dut.sram.peek32(0) == 1u) { fin = true; break; }
        }
        check(fin, "CoreMark termina");
        const uint32_t magic = dut.sram.peek32(4), len = dut.sram.peek32(8);
        std::string s;
        if (magic == 0x434F4E53u && len < 4096)
            for (uint32_t k = 0; k < len; ++k) s += char(dut.sram.peek32(12 + (k & ~3u)) >> (8 * (k & 3u)));
        const bool crc_ok =
            s.find("seedcrc          : 0xe9f5") != std::string::npos &&
            s.find("[0]crclist       : 0xe714") != std::string::npos &&
            s.find("[0]crcmatrix     : 0x1fd7") != std::string::npos &&
            s.find("[0]crcstate      : 0x8e3a") != std::string::npos;
        check(crc_ok, "y valida su resultado: los CRC son los canonicos del 2K run");
        std::printf("    %llu instrucciones en %.3f s simulados\n",
                    (unsigned long long)(dut.core.cpu.inst_count - i0),
                    (sc_time_stamp() - t0).to_seconds());
    }

    void st_iotoggle() {
        grupo("F14 GPIO_IOToggle de STM32CubeF3, sin modificar: la rosa, de 100 en 100 ms");
        arranca(0, FW_TOGGLE);
        // El orden en que se encienden: PE15, PE14 ... PE8, cada 100 ms
        sc_time t_ultimo = SC_ZERO_TIME;
        bool orden = true, separacion = true;
        for (unsigned k = 0; k < 8; ++k) {
            const unsigned p = 15 - k;
            const sc_time lim = sc_time_stamp() + sc_time(300, SC_MS);
            while (!alto(4, p) && sc_time_stamp() < lim) wait(lim - sc_time_stamp(), pin(4, p).value_changed_event());
            if (!alto(4, p)) { orden = false; break; }
            if (k > 0) {
                const double dt = (sc_time_stamp() - t_ultimo).to_seconds();
                if (!cerca(dt, 0.101, 0.002)) { separacion = false; std::printf("    PE%u tras %.3f ms\n", p, dt * 1e3); }
            }
            t_ultimo = sc_time_stamp();
        }
        check(cerca(dut.f_sysclk(), 72e6, 1e-9), "SystemClock_Config de ST: 72 MHz desde el HSE (HSE_ON)");
        check(orden, "los ocho LED se encienden en el orden del ejemplo: LD6 (PE15) ... LD4 (PE8)");
        check(separacion, "uno cada 101 ms: HAL_Delay(100) espera un tick de mas, como en el silicio");
    }

    void st_exti() {
        grupo("F15 GPIO_EXTI de STM32CubeF3, sin modificar: B1 conmuta LD3, LD6, LD7 y LD10");
        arranca(0, FW_EXTI);
        wait(20, SC_MS);
        const bool antes = alto(4, 9) || alto(4, 15) || alto(4, 11) || alto(4, 13);
        b1->press(); wait(5, SC_MS); b1->release();
        wait(400, SC_MS);                              // el retardo de 0x7FFFF vueltas
        const bool una = alto(4, 9) && alto(4, 15) && alto(4, 11) && alto(4, 13);
        b1->press(); wait(5, SC_MS); b1->release();
        wait(400, SC_MS);                              // el retardo de 0x7FFFF vueltas
        const bool dos = !alto(4, 9) && !alto(4, 15) && !alto(4, 11) && !alto(4, 13);
        check(!antes, "sin pulsar, los cuatro apagados");
        check(una, "una pulsacion: LD3, LD6, LD7 y LD10 encendidos");
        check(dos, "otra: apagados");
    }

    // =======================================================================
    // S2: el FPEC, los bytes de opcion, FLASH_EraseProgram y GDB
    // =======================================================================
    void fpec() {
        grupo("F16 El FPEC desde el firmware (f3_demo, modo 8)");
        const unsigned p0 = dut.flash.programadas(), r0 = dut.flash.escrituras_rechazadas();
        arranca(8);
        check(espera_estado(200, 100), "f3_demo acaba en el BusFault que se esperaba");
        check_eq(buzon(B_R0) & 0x80u, 0u, "KEYR: las dos llaves quitan LOCK");
        check((buzon(0x54) & 0x21u) == 0x20u, "borrar la pagina 126: BSY mientras, EOP al acabar");
        check_eq(buzon(B_R1), 0xFFFFFFFFu, "y se lee borrada");
        check_eq(buzon(B_R2), 0xABCD1234u, "PG: dos medias palabras programadas");
        check((buzon(0x58) & 0x04u) != 0, "otra vez sobre lo programado: PGERR");
        check_eq(dut.flash.peek32(0x0803F000u), 0xABCD0000u,
                 "pero 0x0000 se puede escribir encima; visto desde fuera");
        check_eq(buzon(B_R3), 0x80u, "LOCK vuelve a cerrar CR");
        check(dut.flash.escrituras_rechazadas() == r0 + 1 && buzon(B_FAULTS) == 1,
              "una PALABRA con PG es error de bus: el FPEC solo programa medias palabras");
        check(dut.flash.programadas() == p0 + 3, "tres medias palabras programadas en total");
        check(dut.flash.peek32(0x0803F004u) == 0xFFFFFFFFu, "y la palabra no ha tocado la Flash");
    }

    void bytes_opcion() {
        grupo("F17 Los bytes de opcion: borrar, programar DATA0 y OBL_LAUNCH (modo 9)");
        arranca(9);
        check(espera_estado(110, 200), "f3_demo da tres arranques y acaba");
        check_eq(buzon(B_ARRANQUES), 3u, "el segundo y el tercero, por OBL_LAUNCH");
        check_eq(buzon(B_R2) & 0xFFFFu, 0xA55Au, "OPTPG: DATA0 = 0x5A, y el FPEC pone su complemento");
        check_eq((buzon(B_R0) >> 16) & 0xFFu, 0x5Au, "tras el reset, FLASH_OBR.DATA0 = 0x5A");
        check_eq(buzon(B_R0) & 7u, 0u, "RDP = 0xAA: nivel 0, y sin OPTERR");
        check((buzon(B_R1) >> 25) & 1u, "RCC_CSR.OBLRSTF: el reset fue el de OBL_LAUNCH");
        check_eq((buzon(B_R3) >> 16) & 0xFFu, 0xFFu, "y al final, como de fabrica: DATA0 borrado");
        check(dut.flash.ob_half(0) == 0x55AAu && dut.flash.ob_half(2) == 0xFFFFu,
              "visto desde fuera: RDP 0x55AA, DATA0 borrado");
        check(dut.flash.nboot1(), "USER borrado: nBOOT1 = 1");
    }

    void st_flash() {
        grupo("F18 FLASH_EraseProgram de STM32CubeF3, sin modificar");
        const unsigned b0 = dut.flash.borradas(), p0 = dut.flash.programadas();
        arranca(0, FW_FLASH);
        wait(5, SC_MS);                     // los LED, de salida y apagados
        const sc_time lim = sc_time_stamp() + sc_time(6000, SC_MS);
        while (sc_time_stamp() < lim && !alto(4, 9) && !alto(4, 8) && !alto(4, 10))
            wait(5, SC_MS);
        std::printf("    %u paginas borradas, %u medias palabras programadas\n",
                    dut.flash.borradas() - b0, dut.flash.programadas() - p0);
        std::printf("    %.0f ms; LD3 %d, LD4 %d, LD5 %d\n",
                    (sc_time_stamp() - (lim - sc_time(6000, SC_MS))).to_seconds() * 1e3,
                    alto(4, 9), alto(4, 8), alto(4, 10));
        check(alto(4, 9) && !alto(4, 8) && !alto(4, 10),
              "LD3 encendido: las paginas 16-63 se borran, se programan y se leen bien");
        bool ok = true;
        for (uint32_t a = 0x08008000u; a < 0x08020000u; a += 4)
            if (dut.flash.peek32(a) != 0x12345678u) { ok = false; break; }
        check(ok && dut.flash.peek32(0x08020000u) == 0xFFFFFFFFu,
              "y en la Flash esta 0x12345678 de 0x0800_8000 a 0x0801_FFFF, y nada mas");
    }

    // --- GDB --------------------------------------------------------------
    static std::string hex(const std::string& s) {
        std::string h; char b[3];
        for (unsigned char c : s) { std::snprintf(b, sizeof b, "%02x", c); h += b; }
        return h;
    }
    static uint32_t de_hex_le(const std::string& s) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4 && 2 * i + 1 < s.size(); ++i)
            v |= uint32_t(std::stoul(s.substr(2 * i, 2), nullptr, 16)) << (8 * i);
        return v;
    }
    uint32_t pc() { return de_hex_le(cli.pedir("p0f")); }
    std::string espera_parada(double ms = 100) { return cli.recibir(sc_time(ms, SC_MS)); }

    struct ResGdb {
        bool conecta = false, carga = false, lectura = false, wp = false, bp = false,
             paso = false, corre = false;
        uint32_t pc_reset = 0, pc_bp = 0, pc_paso = 0;
    };

    ResGdb sesion_gdb(unsigned puerto, const std::vector<uint8_t>& img, uint32_t parpadea) {
        ResGdb r;
        // En la Flash hay OTRA cosa: CoreMark. El `load` tiene que borrar.
        arranca(0, FW_CM);
        wait(2, SC_MS);
        if (!cli.conectar(puerto)) return r;
        wait(2, SC_MS);
        cli.pedir("qSupported:swbreak+;hwbreak+;xmlRegisters=arm");
        r.conecta = cli.pedir("QStartNoAckMode") == "OK" &&
                    cli.pedir("?").rfind("T05", 0) == 0;
        bool ok = !img.empty();
        char cab[64];
        for (size_t o = 0; ok && o < img.size(); o += 256) {
            const size_t n = std::min<size_t>(256, img.size() - o);
            std::snprintf(cab, sizeof cab, "M%zx,%zx:", size_t(f3::FLASH_B) + o, n);
            std::string pq = cab;
            for (size_t i = 0; i < n; ++i) { char b[3]; std::snprintf(b, 3, "%02x", img[o + i]); pq += b; }
            ok = cli.pedir(pq, sc_time(400, SC_MS)) == "OK";
        }
        r.carga = ok;
        std::string leido;
        for (size_t o = 0; o < img.size(); o += 256) {
            std::snprintf(cab, sizeof cab, "m%zx,%zx", size_t(f3::FLASH_B) + o,
                          std::min<size_t>(256, img.size() - o));
            leido += cli.pedir(cab, sc_time(100, SC_MS));
        }
        std::string esperado;
        for (uint8_t c : img) { char b[3]; std::snprintf(b, 3, "%02x", c); esperado += b; }
        r.lectura = leido == esperado;
        cli.pedir("M20000000,8:0000000000000000");          // el buzon: modo 0
        cli.pedir("qRcmd," + hex("reset halt"), sc_time(50, SC_MS));
        r.pc_reset = pc();
        char z[64];
        std::snprintf(z, sizeof z, "Z2,%x,4", unsigned(f3::SRAM_B + B_ARRANQUES));
        const bool zw = cli.pedir(z) == "OK";
        cli.enviar("vCont;c");
        const std::string pw = espera_parada();
        r.wp = zw && pw.find("watch") != std::string::npos && buzon(B_ARRANQUES) == 1;
        std::snprintf(z, sizeof z, "z2,%x,4", unsigned(f3::SRAM_B + B_ARRANQUES));
        cli.pedir(z);
        std::snprintf(z, sizeof z, "Z0,%x,2", parpadea);
        const bool zb = cli.pedir(z) == "OK";
        cli.enviar("vCont;c");
        const std::string pb = espera_parada();
        r.pc_bp = pc();
        r.bp = zb && pb.find("swbreak") != std::string::npos && r.pc_bp == parpadea;
        std::snprintf(z, sizeof z, "z0,%x,2", parpadea);
        cli.pedir(z);
        cli.enviar("vCont;s");
        const std::string ps = espera_parada();
        r.pc_paso = pc();
        r.paso = ps.rfind("T05", 0) == 0 && r.pc_paso > parpadea && r.pc_paso < parpadea + 8;
        // Y que siga: se espera, con un plazo de reloj, a que el nucleo deje
        // de estar parado (el `vCont;c` lo lee el stub en tiempo de pared)
        cli.enviar("vCont;c");
        {
            const auto t_lim = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (dut.core.debug.is_halted() && std::chrono::steady_clock::now() < t_lim)
                wait(100, SC_US);
        }
        wait(1, SC_MS);
        const bool parado = dut.core.debug.is_halted();
        const uint32_t estado = buzon(B_ESTADO);
        const double t_led = periodo(4, 9);
        r.corre = !parado && estado == 100 && cerca(t_led, 20e-3, 0.01);
        if (!r.corre)
            std::printf("    al soltarlo: nucleo %s, estado %u, periodo de PE9 %.3g s\n",
                        parado ? "PARADO" : "en marcha", unsigned(estado), t_led);
        cli.crudo("\x03");
        espera_parada();
        cli.pedir("D");
        cli.desconectar();
        wait(1, SC_MS);
        return r;
    }

    void informa(const char* via, const ResGdb& r, uint32_t rh) {
        auto c = [&](bool v, const std::string& t) { check(v, (std::string(via) + ": " + t).c_str()); };
        c(r.conecta, "GDB se conecta, negocia y encuentra el nucleo parado");
        c(r.carga, "`load` de f3_demo.bin con paquetes M sobre CoreMark: el stub borra "
                   "las paginas y programa medias palabras por el FPEC (D2)");
        c(r.lectura, "y lo leido de vuelta es el .bin, byte a byte");
        c(r.pc_reset == rh, "`monitor reset halt` para en la primera instruccion, Reset_Handler");
        c(r.wp, "un watchpoint en buzon.arranques para el nucleo en la primera escritura");
        c(r.bp, "un punto de ruptura en SysTick_Handler para el nucleo alli");
        c(r.paso, "un paso avanza una instruccion");
        c(r.corre, "y al soltarlo, el firmware cargado hace parpadear LD3 cada 20 ms");
    }

    void gdb() {
        grupo("F19 GDB por la sonda de los pines (G2, PA14/PA13) y por el DAP (G1), con `load` (D2)");
        std::vector<uint8_t> img;
        {
            std::ifstream f(FW_DEMO, std::ios::binary);
            img.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        auto vec = [&](unsigned i) -> uint32_t {
            if (img.size() < 4 * i + 4) return 0;
            return uint32_t(img[4 * i]) | (uint32_t(img[4 * i + 1]) << 8) |
                   (uint32_t(img[4 * i + 2]) << 16) | (uint32_t(img[4 * i + 3]) << 24);
        };
        // Sin simbolos: el ELF no se versiona. Reset_Handler y SysTick_Handler
        // salen de la tabla de vectores del .bin
        const uint32_t rh = vec(1) & ~1u, st = vec(15) & ~1u;
        const unsigned f0 = ProgramadorF3::el().fallos();
        const sc_time t0 = sc_time_stamp();
        gdb_p->set_enabled(true);
        wait(1, SC_MS);
        const ResGdb rp = sesion_gdb(PUERTO_PINES, img, st);
        gdb_p->set_enabled(false);
        gdb_p->soltar_pines();
        informa("G2 (pines)", rp, rh);
        gdb_d->set_enabled(true);
        wait(1, SC_MS);
        const ResGdb rd_ = sesion_gdb(PUERTO_DAP, img, st);
        gdb_d->set_enabled(false);
        informa("G1 (DAP)", rd_, rh);
        check(rp.pc_reset == rd_.pc_reset && rp.pc_bp == rd_.pc_bp && rp.pc_paso == rd_.pc_paso,
              "los dos caminos dan lo mismo: el mismo depurador, otro cable");
        check(ProgramadorF3::el().fallos() == f0, "y el programador no ha fallado ni una vez");
        g_t_gdb += sc_time_stamp() - t0;
    }

    // =======================================================================
    // F20-F21: contra RM0316, por el DAP (como lo veria un depurador)
    // =======================================================================
    tlm::tlm_response_status w32(uint32_t a, uint32_t v) { return dut.core.debug.ap_write32(a, v); }
    tlm::tlm_response_status w16(uint32_t a, uint16_t v) { return dut.core.debug.ap_write16(a, v); }
    uint32_t r32(uint32_t a) { uint32_t v = 0; dut.core.debug.ap_read32(a, v); return v; }
    void reinicia() { mcu.reset_pin(true); wait(30, SC_US); mcu.reset_pin(false); wait(100, SC_US); }

    void fpec_rm() {
        grupo("F20 El FPEC y los bytes de opcion, por el DAP, contra RM0316 §4-5");
        enum : uint32_t { F = 0x40022000u, KEYR = F + 4, OPTKEYR = F + 8, SR = F + 0xC, CR = F + 0x10,
                          AR = F + 0x14, OBR = F + 0x1C, OB = 0x1FFFF800u };
        arranca(99);
        wait(200, SC_US);
        const bool err = w32(KEYR, 0x12345678u) != tlm::TLM_OK_RESPONSE;
        w32(KEYR, FlashF1::KEY1); w32(KEYR, FlashF1::KEY2);
        check(err && (r32(CR) & 0x80u) && dut.flash.bloqueo_duro(),
              "una llave mal es un error de bus, y el FPEC queda cerrado hasta el reset (§4.2.3)");
        reinicia();
        w32(KEYR, FlashF1::KEY1); w32(KEYR, FlashF1::KEY2);
        check_eq(r32(CR), 0u, "tras el reset, las dos llaves lo abren");
        w32(CR, 0x80u);
        w32(CR, 0x1u);
        check_eq(r32(CR), 0x80u, "cerrado, CR no cambia (§4.2.3)");
        w32(KEYR, FlashF1::KEY1); w32(KEYR, FlashF1::KEY2);
        // Borrar la pagina 124: BSY y STRT a uno mientras dura, y CR no se escribe
        w32(CR, 0x2u); w32(AR, 0x0803E000u); w32(CR, 0x42u);
        const uint32_t sr_b = r32(SR), cr_b = r32(CR);
        w32(CR, 0);
        const uint32_t cr_c = r32(CR);
        check((sr_b & 1u) && (cr_b & 0x40u) && cr_c == 0x42u,
              "borrando: BSY y STRT a uno, y CR no admite escrituras (§4.2.3, §4.5.5)");
        wait(25, SC_MS);
        check((r32(SR) & 0x21u) == 0x20u && !(r32(CR) & 0x40u), "al acabar, EOP; BSY y STRT a cero");
        w32(SR, 0x34u);
        w32(CR, 0x1u);
        w16(0x0803E004u, 0x1234u);
        wait(100, SC_US);
        check_eq(r32(AR), 0x0803E004u, "AR guarda la ultima direccion programada (§4.5.6)");
        w32(CR, 0);
        // Los bytes de opcion: borrados, DATA1 = 0x5A y, encima, un cero: PGERR
        w32(OPTKEYR, FlashF1::KEY1); w32(OPTKEYR, FlashF1::KEY2);
        w32(CR, 0x220u); w32(CR, 0x260u);
        wait(25, SC_MS);
        w32(CR, 0x210u);
        w16(OB, 0x00AAu); wait(100, SC_US);
        w16(OB + 6, 0x005Au); wait(100, SC_US);
        w32(SR, 0x34u);
        w16(OB + 6, 0x0000u); wait(100, SC_US);
        check((r32(SR) & 4u) && dut.flash.ob_half(3) == 0xA55Au,
              "un byte de opcion programado no admite ni un cero: PGERR (§4.2.3)");
        w32(CR, 0x80u);
        // Se cargan en cualquier reset del sistema, no solo con OBL_LAUNCH (§5)
        reinicia();
        check_eq((r32(OBR) >> 24) & 0xFFu, 0x5Au, "un reset del pin carga DATA1 = 0x5A en FLASH_OBR (§5)");
        check_eq(r32(OBR) & 1u, 0u, "y lo borrado (USER, DATA0, WRP) no da OPTERR: el comparador no mira 0xFF/0xFF");
        // El nivel de RDP sale del byte Y de su complemento (tabla 8)
        dut.flash.pon_ob_crudo(0, 0x00AAu);
        reinicia();
        check((r32(OBR) & 7u) == 3u, "RDP = 0xAA con un complemento que no cuadra: nivel 1 y OPTERR (§4.3.1)");
        dut.flash.pon_ob(0, 0xCCu);
        reinicia();
        w32(KEYR, FlashF1::KEY1); w32(KEYR, FlashF1::KEY2);
        w32(OPTKEYR, FlashF1::KEY1); w32(OPTKEYR, FlashF1::KEY2);
        w32(CR, 0x220u); w32(CR, 0x260u);
        wait(1, SC_MS);
        const bool no_borra = (r32(SR) & 0x10u) && dut.flash.ob_half(0) == 0x33CCu;
        w32(SR, 0x34u); w32(CR, 0x210u);
        w16(OB, 0x00AAu); wait(100, SC_US);
        check(((r32(OBR) >> 1) & 3u) == 3u && no_borra && (r32(SR) & 0x10u) && dut.flash.ob_half(0) == 0x33CCu,
              "en el nivel 2 ni se borran los bytes de opcion ni se programa RDP: WRPRTERR (§4.3.1)");
        w32(CR, 0x80u);
        // Del nivel 1 al 0: programar 0xAA borra antes la Flash entera
        dut.flash.pon_ob(0, 0xBBu);
        reinicia();
        const bool n1 = ((r32(OBR) >> 1) & 3u) == 1u && dut.flash.peek32(0x08000000u) != 0xFFFFFFFFu;
        w32(KEYR, FlashF1::KEY1); w32(KEYR, FlashF1::KEY2);
        w32(OPTKEYR, FlashF1::KEY1); w32(OPTKEYR, FlashF1::KEY2);
        w32(CR, 0x220u); w32(CR, 0x260u);
        wait(25, SC_MS);
        w32(CR, 0x210u);
        w16(OB, 0x00AAu);
        wait(50, SC_MS);
        bool vacia = true;
        for (uint32_t a = 0x08000000u; a < 0x08040000u; a += 4)
            if (dut.flash.peek32(a) != 0xFFFFFFFFu) { vacia = false; break; }
        check(n1 && vacia && dut.flash.ob_half(0) == 0x55AAu,
              "del nivel 1 al 0: programar RDP = 0xAA borra antes la Flash entera (§4.3.1)");
        w32(CR, 0x80u);
        // Como de fabrica, para lo que sigue
        for (unsigned i = 0; i < 8; ++i) dut.flash.pon_ob(i, i == 0 ? 0xAAu : 0xFFu);
        reinicia();
    }

    void rcc_rm() {
        grupo("F21 El RCC y el SYSCFG por el DAP, contra RM0316 §9 y §12");
        enum : uint32_t { R = 0x40021000u, RCR = R, CFGR = R + 4, APB2ENR = R + 0x18, APB1ENR = R + 0x1C,
                          BDCR = R + 0x20, PWR_CR = 0x40007000u, SYSCFG = 0x40010000u };
        arranca(1, FW_DEMO);                            // 72 MHz desde el HSE, por el PLL
        check(espera_estado(100, 50), "f3_demo a 72 MHz, con el PLL comiendo del HSE");
        w32(RCR, r32(RCR) & ~((1u << 16) | 1u));
        check((r32(RCR) & (1u << 16)) && cerca(dut.f_sysclk(), 72e6, 1e-9),
              "HSEON no se quita mientras el HSE es SYSCLK a traves del PLL (§9.4.1)");
        check(!(r32(RCR) & 1u), "y HSION si: el PLL no come del HSI");
        w32(RCR, r32(RCR) | 1u);
        w32(CFGR, (r32(CFGR) & ~(7u << 24)) | (5u << 24));     // MCO = HSI
        wait(1, SC_US);
        check(r32(CFGR) & (1u << 28), "MCOF a uno cuando el MCO ya ha cambiado (§9.4.2)");
        w32(APB1ENR, r32(APB1ENR) | (1u << 28));
        w32(PWR_CR, r32(PWR_CR) | (1u << 8));           // DBP
        w32(BDCR, 1u);                                  // LSEON (X3 no esta: no arranca)
        w32(BDCR, 1u | (1u << 2));
        check_eq(r32(BDCR) & 5u, 1u, "LSEBYP no cambia con el LSE encendido (§9.4.9)");
        w32(BDCR, 0);
        w32(APB2ENR, r32(APB2ENR) | 1u);
        w32(SYSCFG, r32(SYSCFG) | (1u << 14));
        check(r32(SYSCFG) & (1u << 14), "SYSCFG_CFGR1.TIM7_DAC1_CH2_DMA_RMP se escribe en el F303xC (§12.1.1)");
        arranca(99);
        wait(100, SC_US);
    }

    // =======================================================================
    // S3: los ejemplos de dos placas de STM32CubeF3, sin modificar
    // =======================================================================
    // Los LED de la rosa, por su nombre en el BSP
    enum : unsigned { LED3 = 9, LED4 = 8, LED5 = 10, LED6 = 15, LED7 = 11 };
    void st_uart(const char* n, const char* g, unsigned ok_tx, unsigned ok_rx, unsigned err) {
        if (!toca(g)) return;
        grupo(g);
        TbDos& d = *dos;
        d.arranca(fw_s3((std::string(n) + "_tx").c_str()), fw_s3((std::string(n) + "_rx").c_str()));
        wait(300, SC_MS);
        const bool espera = !d.alto(d.a, 4, ok_tx) && !d.alto(d.b, 4, ok_rx);
        d.pulsa(d.b1_a);
        const bool a_ok = d.espera(d.a, 4, ok_tx, true, 500) && d.espera(d.a, 4, ok_rx, true, 500);
        const bool b_ok = d.espera(d.b, 4, ok_tx, true, 500) && d.espera(d.b, 4, ok_rx, true, 500);
        wait(20, SC_MS);
        check(espera, "antes de pulsar B1 nadie ha mandado nada");
        check(a_ok, "la placa 1 (TRANSMITTER_BOARD) manda y recibe: sus dos LED de exito");
        check(b_ok, "la placa 2 recibe y devuelve: los mismos dos LED");
        check(!d.alto(d.a, 4, err) && !d.alto(d.b, 4, err), "y ninguna enciende el LED de error");
        traza(d.a, "usart2", d.a.chip().usart2, {0x00, 0x04, 0x08, 0x0C, 0x1C});
        traza(d.b, "usart2", d.b.chip().usart2, {0x00, 0x04, 0x08, 0x0C, 0x1C});
        d.apaga();
    }
    void st_spi(const char* n, const char* g) {
        if (!toca(g)) return;
        grupo(g);
        TbDos& d = *dos;
        d.arranca(fw_s3((std::string(n) + "_maestro").c_str()), fw_s3((std::string(n) + "_esclavo").c_str()));
        wait(200, SC_MS);
        d.pulsa(d.b1_a);
        const bool m = d.espera(d.a, 4, LED4, true, 6000) && d.alto(d.a, 4, LED3);
        const bool e = d.espera(d.b, 4, LED4, true, 100) && d.alto(d.b, 4, LED3);
        wait(50, SC_MS);
        check(m, "el maestro termina: LED3 (transmitido) y LED4 (recibido)");
        check(e, "el esclavo tambien");
        check(!d.alto(d.a, 4, LED5) && !d.alto(d.b, 4, LED5), "y nadie enciende LED5 (error o plazo)");
        traza(d.a, "spi3", d.a.chip().spi3, {0x00, 0x04, 0x08});
        traza(d.b, "spi3", d.b.chip().spi3, {0x00, 0x04, 0x08});
        d.apaga();
    }
    void st_i2c(const char* n, const char* g, bool dos_pasos) {
        if (!toca(g)) return;
        grupo(g);
        TbDos& d = *dos;
        d.arranca(fw_s3((std::string(n) + "_maestro").c_str()), fw_s3((std::string(n) + "_esclavo").c_str()));
        wait(200, SC_MS);
        d.pulsa(d.b1_a);
        if (!dos_pasos) {
            // AdvComIT: tras sincronizar, va y vuelve sin parar; LED7 conmuta
            unsigned n_cambios = 0; bool ant = d.alto(d.a, 4, LED7);
            const sc_time fin = sc_time_stamp() + sc_time(500, SC_MS);
            while (sc_time_stamp() < fin) {
                wait(fin - sc_time_stamp(), d.a.chip().pinmux.analog(4, LED7).value_changed_event());
                if (d.alto(d.a, 4, LED7) != ant) { ++n_cambios; ant = !ant; }
            }
            check(n_cambios >= 4, "el maestro conmuta LED7 en cada intercambio bueno");
        } else {
            const bool tx = d.espera(d.a, 4, LED7, true, 2000);
            check(tx, "primer paso: el maestro manda y LED7 se enciende");
            wait(100, SC_MS);
            d.pulsa(d.b1_a);
            const bool rx = d.espera(d.a, 4, LED7, false, 2000);
            check(rx, "segundo paso: el maestro recibe y LED7 se apaga");
            wait(100, SC_MS);
            check(!d.alto(d.b, 4, LED7), "el esclavo tambien acaba con LED7 apagado");
        }
        check(!d.alto(d.a, 4, LED3) && !d.alto(d.b, 4, LED3), "y ninguno enciende LED3 (error)");
        traza(d.a, "i2c1", d.a.chip().i2c1, {0x00, 0x04, 0x08, 0x10, 0x18});
        traza(d.b, "i2c1", d.b.chip().i2c1, {0x00, 0x04, 0x08, 0x10, 0x18});
        traza(d.b, "dma1", d.b.chip().dma1, {0x00, 0x6C, 0x70, 0x74, 0x78, 0x80, 0x84, 0x88, 0x8C});
        d.apaga();
    }

    // =======================================================================
    // S3: los ejemplos de una placa de STM32CubeF3, sin modificar
    // =======================================================================
    struct Medida { double hz = 0, duty = 0; unsigned flancos = 0; };
    // Frecuencia y ciclo de trabajo de un pin en una ventana
    Medida mide(unsigned p, unsigned i, double us, unsigned por_periodo = 1) {
        // El ciclo de trabajo, entre el primer flanco de subida y el ultimo:
        // periodos enteros
        Medida m;
        const sc_time fin = sc_time_stamp() + sc_time(us, SC_US);
        bool ant = alto(p, i);
        sc_time primero = SC_ZERO_TIME, ultimo = SC_ZERO_TIME, sube = SC_ZERO_TIME;
        sc_time alto_t = SC_ZERO_TIME, alto_hasta_ultimo = SC_ZERO_TIME;
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), pin(p, i).value_changed_event());
            const bool v = alto(p, i);
            if (v == ant) continue;
            if (v) {
                if (m.flancos == 0) primero = sc_time_stamp();
                // Solo se cierra la cuenta en un flanco que complete periodos
                if (m.flancos % por_periodo == 0) { ultimo = sc_time_stamp(); alto_hasta_ultimo = alto_t; }
                sube = sc_time_stamp();
                ++m.flancos;
            } else if (m.flancos > 0) {
                alto_t += sc_time_stamp() - sube;
            }
            ant = v;
        }
        if (m.flancos >= 2) {
            m.hz = double((m.flancos - 1) / por_periodo * por_periodo) / (ultimo - primero).to_seconds();
            m.duty = alto_hasta_ultimo.to_seconds() / (ultimo - primero).to_seconds();
        }
        return m;
    }
    bool espera_pin(unsigned p, unsigned i, bool v, double ms) {
        const sc_time lim = sc_time_stamp() + sc_time(ms, SC_MS);
        while (alto(p, i) != v && sc_time_stamp() < lim)
            wait(lim - sc_time_stamp(), pin(p, i).value_changed_event());
        return alto(p, i) == v;
    }
    unsigned cambios(unsigned p, unsigned i, double ms) {
        const sc_time fin = sc_time_stamp() + sc_time(ms, SC_MS);
        unsigned n = 0; bool ant = alto(p, i);
        while (sc_time_stamp() < fin) {
            wait(fin - sc_time_stamp(), pin(p, i).value_changed_event());
            if (alto(p, i) != ant) {
                ++n; ant = !ant;
                if (std::getenv("TESTF3_TRAZA")) std::printf("      P%c%u = %d a los %s\n", 'A' + p, i, ant, sc_time_stamp().to_string().c_str());
            }
        }
        return n;
    }
    // Una variable del ejemplo, sin su ELF: la palabra de la SRAM que valga eso
    bool en_sram(uint32_t v, uint32_t* dir = nullptr, uint32_t tol = 0) {
        for (uint32_t a = 0; a < 0x2000u; a += 4) {
            const uint32_t w = dut.sram.peek32(a);
            if (w + tol >= v && w <= v + tol) { if (dir) *dir = 0x20000000u + a; return true; }
        }
        return false;
    }
    uint32_t csr() { return dut.rcc.peek(0x24); }
    enum : unsigned { L3 = 9, L4 = 8, L5 = 10, L6 = 15 };

    void st_tim() {
        if (toca("F32")) {
            grupo("F32 TIM_TimeBase: TIM3 a 10 kHz con ARR = 9999, una interrupcion por segundo");
            arranca_s3(fw_s3("TIM_TimeBase"));
            wait(100, SC_MS);
            const bool a0 = alto(4, L4);
            espera_pin(4, L4, !a0, 1500);
            const sc_time t1 = sc_time_stamp();
            espera_pin(4, L4, a0, 1500);
            const double dt = (sc_time_stamp() - t1).to_seconds();
            if (!cerca(dt, 1.0, 1e-4)) std::printf("    dt = %.6f s; TIM3 PSC %u ARR %u, timclk %.0f\n", dt,
                dut.tim3.dbg_read(0x28), dut.tim3.dbg_read(0x2C), dut.tim3.tick_hz());
            check(cerca(dt, 1.0, 1e-4), "LD4 (PE8) cambia cada segundo");
        }
        if (toca("F33")) {
            grupo("F33 TIM_PWMOutput: TIM3 a 24 MHz, ARR = 664, cuatro PWM en PC6-PC9");
            arranca_s3(fw_s3("TIM_PWMOutput"));
            wait(20, SC_MS);
            const double esp[4] = {332.0 / 665, 249.0 / 665, 166.0 / 665, 83.0 / 665};
            bool f = true, dd = true;
            for (unsigned c = 0; c < 4; ++c) {
                const Medida m = mide(2, 6 + c, 500);
                if (!cerca(m.hz, 24e6 / 665, 1e-3)) { f = false; std::printf("    CH%u %.1f Hz\n", c + 1, m.hz); }
                if (std::fabs(m.duty - esp[c]) > 0.005) { dd = false; std::printf("    CH%u %.4f\n", c + 1, m.duty); }
            }
            check(f, "los cuatro a 36,09 kHz (24 MHz / 665)");
            check(dd, "con el 50, 37,5, 25 y 12,5 %");
        }
        if (toca("F34")) {
            grupo("F34 TIM_Combined: PWM combinado de TIM1 con el canal 5 (GC5C1-3), centrado");
            arranca_s3(fw_s3("TIM_Combined"));
            wait(20, SC_MS);
            const double esp[3] = {(2047.0 - 254) / 4096, (1535.0 - 254) / 4096, (1023.0 - 254) / 4096};
            bool f = true, dd = true;
            for (unsigned c = 0; c < 3; ++c) {
                const Medida m = mide(0, 8 + c, 2000, 2);
                // Dos pulsos por periodo: en centrado, CH1 (PWM1) esta alto en
                // el valle y CH5 (PWM2) lo parte por la mitad
                if (m.flancos < 34 || m.flancos > 36) { f = false; std::printf("    CH%u %u flancos en 2 ms\n", c + 1, m.flancos); }
                if (std::fabs(m.duty - esp[c]) > 0.005) { dd = false; std::printf("    CH%u %.4f (%.4f)\n", c + 1, m.duty, esp[c]); }
            }
            check(f, "PA8-PA10: dos pulsos por periodo de 8,79 kHz (72 MHz / 2 / 4096)");
            check(dd, "cada uno, su canal Y el 5 en PWM2: 43,8, 31,3 y 18,8 %");
        }
        if (toca("F35")) {
            grupo("F35 TIM_Asymetric: TIM1 de referencia; TIM8 no arranca (el ejemplo comparte el handle)");
            arranca_s3(fw_s3("TIM_Asymetric"));
            wait(20, SC_MS);
            const Medida r = mide(0, 8, 5000);
            check(cerca(r.hz, 72e6 / 44001, 1e-3) && std::fabs(r.duty - 0.5) < 0.005,
                  "TIM1_CH1 (PA8), la referencia: 1,636 kHz al 50 %");
            // El ejemplo usa EL MISMO TIM_HandleTypeDef para TIM1 y TIM8, y la
            // HAL v1.5.9 guarda en el handle el estado de cada canal: el canal
            // 1 ya esta BUSY (de TIM1), HAL_TIM_PWM_Start de TIM8 devuelve
            // HAL_ERROR y el ejemplo va a Error_Handler. En la placa, igual
            check(alto(4, L3) && !(dut.tim8.dbg_read(0) & 1u),
                  "TIM8 sin arrancar y LD3 encendido: HAL_TIM_PWM_Start(TIM8) da HAL_ERROR, como en la placa");
            check(dut.tim8.dbg_read(0x18) == 0x01007878u && dut.tim8.dbg_read(8) == 4u,
                  "pero TIM8 queda configurado: CH2 en PWM2 asimetrico (OC2M = 1111), reset por ITR0");
        }
        if (toca("F36")) {
            grupo("F36 TIM_ComplementarySignals: TIM1 a 10 kHz con tiempo muerto, freno y LOCK");
            arranca_s3(fw_s3("TIM_ComplementarySignals"));
            wait(20, SC_MS);
            const Medida c1 = mide(0, 8, 1000), c1n = mide(1, 13, 1000);
            const Medida c2 = mide(4, 11, 1000), c2n = mide(1, 14, 1000);
            check(cerca(c1.hz, 10e3, 1e-3) && cerca(c1n.hz, 10e3, 1e-3), "CH1 (PA8) y CH1N (PB13) a 10 kHz");
            // Con 100 tics de tiempo muerto (1,39 us) cada flanco de subida se retrasa
            const double dt = 100 / 72e6 * 10e3;
            check(std::fabs(c1.duty - (899.0 / 1800 - dt)) < 0.004 && std::fabs(c1n.duty - (901.0 / 1800 - dt)) < 0.004,
                  "50 % menos el tiempo muerto en los dos");
            check(std::fabs(c2.duty - (449.0 / 1800 - dt)) < 0.004 && std::fabs(c2n.duty - (1351.0 / 1800 - dt)) < 0.004,
                  "CH2 (PE11) al 25 % y CH2N (PB14) al 75 %, menos el tiempo muerto");
            bool solape = false;
            for (unsigned k = 0; k < 200; ++k) { if (alto(0, 8) && alto(1, 13)) solape = true; wait(337, SC_NS); }
            check(!solape, "CH1 y CH1N nunca a la vez");
        }
        if (toca("F37")) {
            grupo("F37 TIM_DMA: el DMA1 (canal 5, TIM1_UP) cambia CCR3 cada cuatro periodos (RCR = 3)");
            arranca_s3(fw_s3("TIM_DMA"));
            wait(20, SC_MS);
            // Los anchos de los pulsos de CH3 (PA10), uno detras de otro
            std::vector<double> d;
            espera_pin(0, 10, false, 1);
            for (unsigned k = 0; k < 30; ++k) {
                espera_pin(0, 10, true, 1); const sc_time a = sc_time_stamp();
                espera_pin(0, 10, false, 1);
                d.push_back((sc_time_stamp() - a).to_seconds() * 17570.0);
            }
            unsigned n75 = 0, n50 = 0, n25 = 0;
            for (double x : d) { if (std::fabs(x - 0.75) < 0.01) ++n75; else if (std::fabs(x - 0.5) < 0.01) ++n50; else if (std::fabs(x - 0.25) < 0.01) ++n25; }
            check(n75 + n50 + n25 == 30 && n75 >= 8 && n50 >= 8 && n25 >= 8,
                  "los tres anchos del buffer, 75, 50 y 25 %, de cuatro en cuatro");
            check(dut.dma1.transferencias() > 10, "y los ha movido el DMA");
        }
        if (toca("F38")) {
            grupo("F38 TIM_InputCapture: TIM1_CH2 (PE11) mide una senal de 10 kHz");
            gen_e11->pon(10e3, 0.5);
            arranca_s3(fw_s3("TIM_InputCapture"));
            wait(50, SC_MS);
            if (!en_sram(10000, nullptr, 2)) std::printf("    capturas TIM1 %llu, CCR2 %u; [48..58] %u %u %u %u %u\n", (unsigned long long)dut.tim1.captures(), dut.tim1.dbg_read(0x38),
                dut.sram.peek32(0x48), dut.sram.peek32(0x4C), dut.sram.peek32(0x50), dut.sram.peek32(0x54), dut.sram.peek32(0x58));
            // El borde de la senal no cae en un tic del contador: 7199 o 7200
            check(en_sram(10000, nullptr, 2), "uwFrequency = PCLK2 / diferencia de capturas = 10000 (+-2)");
            check(!alto(4, L3), "sin error");
            gen_e11->pon(0, 0.5);
        }
        if (toca("F39")) {
            grupo("F39 TIM_PWMInput: TIM4 en modo de entrada PWM (PB7), 5 kHz al 30 %");
            gen_b7->pon(5e3, 0.3);
            arranca_s3(fw_s3("TIM_PWMInput"));
            wait(50, SC_MS);
            uint32_t a = 0;
            const bool f = en_sram(5000, &a, 2);
            if (!f) std::printf("    capturas TIM4 %llu, CCR1 %u CCR2 %u\n", (unsigned long long)dut.tim4.captures(), dut.tim4.dbg_read(0x34), dut.tim4.dbg_read(0x38));
            check(f, "uwFrequency = HCLK / CCR2 = 5000 (+-2)");
            // uwDutyCycle es la palabra de al lado (.bss, en el orden que deja el enlazador)
            auto dc = [&](uint32_t x) { const uint32_t w = dut.sram.peek32(x - 0x20000000u); return w == 29 || w == 30; };
            check(f && (dc(a + 4) || dc(a - 4)), "uwDutyCycle = CCR1 * 100 / CCR2 = 30 (o 29: el flanco no cae en un tic)");
            gen_b7->pon(0, 0.5);
        }
    }

    void st_perros() {
        if (toca("F40")) {
            grupo("F40 IWDG_Reset: el TIM16 mide el LSI por el MCO; sin recargas, reset a 1 s");
            arranca_s3(fw_s3("IWDG_Reset"));
            wait(100, SC_MS);                 // lo que tarda en arrancar (y los pulsos de BSP_LED_Init)
            const unsigned n = cambios(4, L4, 2500);
            if (!(n >= 2 && n <= 3)) std::printf("    %u cambios; uwLsiFreq %u, IWDG PR %u RLR %u\n", n, dut.sram.peek32(0x50),
                                                  dut.iwdg.dbg_read(4), dut.iwdg.dbg_read(8));
            check(n >= 2 && n <= 3, "LD4 cambia cada 990 ms: el perro se recarga");
            check(!alto(4, L3), "y no hay reset");
            check(dut.tim16.captures() >= 2, "el TIM16 ha capturado el LSI (TI1 desde el MCO, OR.TI1_RMP)");
            b1->press(); wait(20, SC_MS); b1->release();
            const sc_time t0 = sc_time_stamp();
            const bool r = espera_pin(4, L3, true, 1500);
            check(r && (csr() & (1u << 29)), "B1: fallo, sin recargas, y el IWDG reinicia (IWDGRSTF): LD3");
            std::printf("    LD3 a los %.0f ms\n", (sc_time_stamp() - t0).to_seconds() * 1e3);
        }
        if (toca("F41")) {
            grupo("F41 IWDG_WindowMode: recargar dentro de la ventana; fuera, reset");
            arranca_s3(fw_s3("IWDG_WindowMode"));
            wait(100, SC_MS);                 // lo que tarda en arrancar (y los pulsos de BSP_LED_Init)
            const unsigned n = cambios(4, L4, 2000);
            if (!(n >= 4 && n <= 5)) std::printf("    %u cambios; IWDG PR %u RLR %u WINR %u\n", n, dut.iwdg.dbg_read(4), dut.iwdg.dbg_read(8), dut.iwdg.dbg_read(0x10));
            check(n >= 4 && n <= 5, "LD4 cambia cada 450 ms, recargando dentro de la ventana (400 ms)");
            const unsigned f0 = dut.iwdg.fuera_de_ventana();
            b1->press(); wait(20, SC_MS); b1->release();
            wait(800, SC_MS);
            check(dut.iwdg.fuera_de_ventana() == f0 + 1 && (csr() & (1u << 29)),
                  "B1: recarga cada 200 ms, antes de la ventana, y reset (IWDGRSTF)");
            check(alto(4, L4) && cambios(4, L4, 1000) == 0, "tras el reset LD4 se queda encendido (los 4 s)");
            check(!alto(4, L3), "sin error");
        }
        if (toca("F42")) {
            grupo("F42 WWDG_Example: refresco cada 44 ms en la ventana; sin el, reset");
            arranca_s3(fw_s3("WWDG_Example"));
            wait(100, SC_MS);                 // lo que tarda en arrancar (y los pulsos de BSP_LED_Init)
            const unsigned n = cambios(4, L4, 500);
            if (!(n >= 10 && n <= 12)) std::printf("    %u cambios; WWDG CR %X CFR %X\n", n, dut.wwdg.dbg_read(0), dut.wwdg.dbg_read(4));
            check(n >= 10 && n <= 12, "LD4 cambia cada 44 ms");
            b1->press(); wait(5, SC_MS); b1->release();
            const bool r = espera_pin(4, L3, true, 200);
            check(r && (csr() & (1u << 30)), "B1: el WWDG reinicia (WWDGRSTF): LD3 encendido");
            check(!alto(4, L5), "sin error");
        }
    }

    void st_rtc() {
        if (toca("F43")) {
            grupo("F43 RTC_Calendar: el RTC al LSI, la fecha y la hora, y la marca en BKP1");
            arranca_s3(fw_s3("RTC_Calendar"));
            const bool ok = espera_pin(4, L6, true, 500);
            const uint32_t t1 = dut.rtc.dbg_read(0x00);
            wait(2100, SC_MS);
            const uint32_t t2 = dut.rtc.dbg_read(0x00);
            check(ok, "LD6: el RTC se ha configurado");
            std::printf("    TR %06X -> %06X, DR %06X\n", t1, t2, dut.rtc.dbg_read(0x04));
            check(t2 != t1 && ((t2 & 0x7Fu) - (t1 & 0x7Fu)) >= 2, "y cuenta: dos segundos mas en RTC_TR");
            check(dut.rtc.dbg_read(0x54) == 0x32F2u, "RTC_BKP1R = 0x32F2, la marca del ejemplo (RTC_BKP_DR1)");
        }
        auto tb = [&](const char* g, const char* fw, double ms) {
            if (!toca(g)) return;
            grupo((std::string(g) + " " + fw + ": la base de tiempos de la HAL sin SysTick").c_str());
            arranca_s3(fw_s3(fw));
            wait(100, SC_MS);
            const unsigned n = cambios(4, L3, 4 * ms + ms / 2);
            check(n >= 4 && n <= 5, "LD3 cambia con HAL_Delay, a su ritmo");
            b1->press(); wait(20, SC_MS); b1->release();
            const unsigned m = cambios(4, L3, 3 * ms);
            check(m == 0, "B1 suspende el tick: LD3 se para");
            b1->press(); wait(20, SC_MS); b1->release();
            check(cambios(4, L3, 3 * ms) >= 2, "otra vez B1: el tick vuelve");
        };
        tb("F44", "HAL_TimeBase_RTC_ALARM", 500);
        tb("F45", "HAL_TimeBase_RTC_WKUP", 500);
        tb("F46", "HAL_TimeBase_TIM", 1000);
    }

    void st_pwr() {
        if (toca("F47")) {
            grupo("F47 PWR_STOP: Stop con el regulador en bajo consumo; despierta el RTC");
            arranca_s3(fw_s3("PWR_STOP"));
            const unsigned n = cambios(4, L4, 4900);
            check(n > 40, "en marcha, LD4 parpadea");
            wait(300, SC_MS);
            check(dut.pwr.modo() == LP_STOP && dut.f_sysclk() == 0.0, "a los 5 s, Stop: los relojes parados");
            const unsigned e = dut.pwr.entradas_stop();
            const bool d = espera_pin(4, L3, true, 4000);
            check(d && dut.pwr.despertares() >= 1, "el despertador del RTC (0x1FFF / (LSI / 16) = 3,3 s) lo saca: LD3");
            wait(50, SC_MS);
            check(cerca(dut.f_sysclk(), 72e6, 1e-9), "y SYSCLKConfig_STOP vuelve a poner 72 MHz");
            check(e == 1, "una entrada en Stop");
        }
        if (toca("F48")) {
            grupo("F48 PWR_STANDBY: Standby con WKUP1 y la alarma del RTC");
            arranca_s3(fw_s3("PWR_STANDBY"));
            wait(300, SC_MS);
            check(alto(4, L3), "en marcha, LD3 encendido");
            b1->press(); wait(50, SC_MS); b1->release();
            wait(100, SC_MS);
            check(dut.pwr.modo() == LP_STANDBY && dut.en_reset(), "al soltar B1, Standby: el dominio de 1,8 V apagado");
            check(!alto(4, L3), "y los pines sueltos");
            const bool d = espera_pin(4, L3, true, 6000);
            check(d, "la alarma (+5 s) lo despierta: arranca de nuevo y LD3 se enciende");
            check((dut.pwr.dbg_read(0x04) & 2u) && (csr() & RccF3::CSR_V18PWRRSTF), "con SBF en PWR_CSR y V18PWRRSTF en RCC_CSR");
        }
        if (toca("F49")) {
            grupo("F49 PWR_PVD: el PVD en el nivel 3 (2,38 V) por la linea 16 del EXTI");
            arranca_s3(fw_s3("PWR_PVD"));
            wait(300, SC_MS);
            check(cambios(4, L3, 1000) >= 4, "con 3,3 V, LD3 parpadea");
            mcu.tension(2.3);
            wait(50, SC_MS);
            check(cambios(4, L3, 1000) == 0, "con 2,3 V, el PVD salta y LD3 se para");
            mcu.tension(3.3);
            wait(50, SC_MS);
            check(cambios(4, L3, 1000) >= 4, "con 3,3 V otra vez, vuelve a parpadear");
        }
        // PWR_CurrentConsumption, sus cuatro modos: el de por defecto (Sleep)
        // y los otros tres con -D<MODO>. En todos, B1 deja de parpadear LD3 y
        // entra en el modo; cada uno sale por donde dice su readme
        auto cc = [&](const char* g, const char* modo, const char* fw, LpMode lp) {
            if (!toca(g)) return false;
            grupo((std::string(g) + " PWR_CurrentConsumption (" + modo + ")").c_str());
            arranca_s3(fw_s3(fw));
            wait(300, SC_MS);
            check(cambios(4, L3, 500) >= 3, "en marcha, LD3 parpadea");
            b1->press(); wait(50, SC_MS); b1->release();
            wait(450, SC_MS);          // STANDBY_MODE espera 200 ms antes de entrar
            check(dut.pwr.modo() == lp, (std::string("B1: ") + modo).c_str());
            return true;
        };
        if (cc("F50", "SLEEP_MODE", "PWR_CurrentConsumption", LP_SLEEP)) {
            check(cambios(4, L3, 500) == 0, "con el tick suspendido no despierta solo");
            b1->press(); wait(50, SC_MS); b1->release();
            wait(100, SC_MS);
            check(dut.pwr.modo() == LP_RUN && cambios(4, L3, 500) >= 3, "B1 lo despierta: LD3 parpadea otra vez");
        }
        if (cc("F51", "STOP_RTC_MODE", "PWR_CurrentConsumption_STOP_RTC_MODE", LP_STOP)) {
            check(dut.f_sysclk() <= 0.0, "en Stop, los relojes parados");
            const bool d = espera_pin(4, L6, true, 23000);
            check(d && dut.pwr.modo() == LP_RUN, "el despertador del RTC (0xA017 / (LSI / 16)) lo saca: LD6");
        }
        if (cc("F52", "STANDBY_MODE", "PWR_CurrentConsumption_STANDBY_MODE", LP_STANDBY)) {
            b1->press(); wait(50, SC_MS);
            const bool d = espera_pin(4, L5, true, 500);
            b1->release();
            check(d && (csr() & RccF3::CSR_V18PWRRSTF), "WKUP1 (B1, PA0) lo despierta: arranca con SBF y LD5");
        }
        if (cc("F53", "STANDBY_RTC_MODE", "PWR_CurrentConsumption_STANDBY_RTC_MODE", LP_STANDBY)) {
            const bool d = espera_pin(4, L5, true, 23000);
            check(d && (csr() & RccF3::CSR_V18PWRRSTF), "el despertador del RTC lo saca del Standby: LD5");
        }
    }

    void s3_una() {
        st_tim();
        st_perros();
        st_rtc();
        st_pwr();
    }

    // Solo un grupo de S3 (TESTF3_GRUPO=F22), para depurarlo
    static bool toca(const char* g) {
        const char* e = std::getenv("TESTF3_GRUPO");
        return !e || std::strncmp(g, e, std::strlen(e)) == 0;
    }
    static void traza(McuF3& m, const char* n, BusSlave& b, std::initializer_list<uint32_t> offs) {
        if (!std::getenv("TESTF3_TRAZA")) return;
        std::printf("    [%s] pc=%08X sysclk=%.0f lr=%08X\n", m.chip().name(), m.chip().core.cpu.pc(),
                    m.chip().f_sysclk(), m.chip().core.cpu.reg.r[14]);
        {
            const uint32_t sp = m.chip().core.cpu.reg.r[13];
            if (sp >= 0x20000000u && sp < 0x2000A000u - 32)
                std::printf("    [%s] sp=%08X pila: pc=%08X lr=%08X\n", m.chip().name(), sp,
                            m.chip().sram.peek32(sp - 0x20000000u + 24), m.chip().sram.peek32(sp - 0x20000000u + 20));
            if (sp >= 0x20000000u && sp < 0x2000A000u - 64) {
                std::printf("    [%s] pila:", m.chip().name());
                for (unsigned k = 0; k < 12; ++k) std::printf(" %08X", m.chip().sram.peek32(sp - 0x20000000u + 4 * k));
                std::printf("\n");
            }
        }
        if (const char* v = std::getenv("TESTF3_VOLCADO")) {
            const uint32_t a0 = uint32_t(std::strtoul(v, nullptr, 16));
            std::printf("    [%s] %08X:", m.chip().name(), a0);
            for (unsigned i = 0; i < 32; ++i) {
                const uint32_t w = m.chip().sram.peek32(((a0 - 0x20000000u) & ~3u) + (i & ~3u));
                const char c = char(w >> (8 * (i & 3)));
                std::printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            std::printf("\n");
        }
        std::printf("    [%s %s]", m.chip().name(), n);
        for (uint32_t o : offs) std::printf(" %02X=%08X", unsigned(o), unsigned(b.dbg_read(o)));
        std::printf("\n");
    }
    void s3_dos() {
        st_uart("UART_TwoBoards_ComPolling", "F22 UART_TwoBoards_ComPolling: dos Discovery por la USART2 (PD5/PD6)",
                LED6, LED5, LED3);
        st_uart("UART_TwoBoards_ComIT", "F23 UART_TwoBoards_ComIT", LED3, LED5, LED6);
        st_uart("UART_TwoBoards_ComDMA", "F24 UART_TwoBoards_ComDMA: con el DMA1 (canales 6 y 7)", LED3, LED5, LED6);
        st_spi("SPI_FullDuplex_ComPolling", "F25 SPI_FullDuplex_ComPolling: SPI3 maestro y esclavo (PC10-12)");
        st_spi("SPI_FullDuplex_ComIT", "F26 SPI_FullDuplex_ComIT");
        st_spi("SPI_FullDuplex_ComDMA", "F27 SPI_FullDuplex_ComDMA: con el DMA2 (canales 1 y 2)");
        st_i2c("I2C_TwoBoards_ComPolling", "F28 I2C_TwoBoards_ComPolling: I2C1 con direccion de 10 bits (PB6/PB7)", true);
        st_i2c("I2C_TwoBoards_ComIT", "F29 I2C_TwoBoards_ComIT", true);
        st_i2c("I2C_TwoBoards_ComDMA", "F30 I2C_TwoBoards_ComDMA: con el DMA1 (canales 6 y 7)", true);
        st_i2c("I2C_TwoBoards_AdvComIT", "F31 I2C_TwoBoards_AdvComIT: orden, respuesta y vuelta a empezar", false);
    }

    void run() {
        firmwares();
        catalogo();
        integracion();
        mcu.alimenta(true);
        wait(50, SC_US);
        // TESTF3_SOLO=s2: solo los grupos de S2, para depurarlos (el tiempo
        // simulado, entonces, no es el del invariante)
        const char* solo = std::getenv("TESTF3_SOLO");
        if (solo && std::strcmp(solo, "s3") == 0) {
            s3_dos();
            s3_una();
            std::printf("\nTOTAL F3: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
            sc_stop();
            return;
        }
        if (!solo || std::strcmp(solo, "s2") != 0) {
            arranque_flash();
            arranque_sistema();
            arranque_sram();
            mem_mode();
            ccm();
            hse(1, "F8 72 MHz por el HSE en modo CRISTAL (HSE_ON), con los 8 MHz del ST-LINK en PF0 (modo 1)",
                "SWS = PLL y HSERDY");
            hse(2, "F9 72 MHz por el HSE en modo BYPASS (modo 2)", "SWS = PLL y HSERDY, en bypass");
            mala();
            gpio_mco();
            exti();
            coremark();
            st_iotoggle();
            st_exti();
        }
        fpec();
        bytes_opcion();
        st_flash();
        gdb();
        fpec_rm();
        rcc_rm();
        r_gpio();
        r_exti();
        r_tim();
        r_crc();
        r_iwdg();
        r_spi();
        r_rtc_ccm();
        if (!solo) {
            s3_una();
            s3_dos();
        }
        std::printf("\nTOTAL F3: %u comprobaciones OK, %u fallos\n", g_ok, g_fallos);
        sc_stop();
    }
};

int sc_main(int, char*[]) {
    sc_report_handler::set_actions(SC_WARNING,
        std::getenv("TESTF3_AVISOS") ? SC_DISPLAY : SC_DO_NOTHING);
    TbF3 tb("tb");
    sc_start();
    std::printf("\nTiempo simulado: %s\n", sc_time_stamp().to_string().c_str());
    if (g_t_gdb != SC_ZERO_TIME)
        std::printf("  de los cuales F19 (socket de GDB)    : %s\n"
                    "  el resto                             : %s\n",
                    g_t_gdb.to_string().c_str(),
                    (sc_time_stamp() - g_t_gdb).to_string().c_str());
    return g_fallos ? 1 : 0;
}
