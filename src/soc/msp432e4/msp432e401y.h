// =============================================================================
// msp432e401y.h — EL MSP432E401Y (fase T6)
//
// El segundo Cortex-M4F de Texas Instruments, y otra vez el MISMO núcleo:
// `CortexM4F` con
//
//   CoreCaps           111 IRQ, 3 bits de prioridad, MPU, FPU      [e4_desc.h]
//   MapaRam            256 KB de SRAM, sin CCM
//   IntegracionNucleo  sin alias del fabricante en 0x0 (la Flash está ahí) y
//                      sin ventana en el PPB: todo lo de TI está en el bus de
//                      periféricos
//
// y alrededor, un Tiva [doc/analisis_msp432.md, §2 y §8 T6]:
//
//   memoria     Flash de 1 MB en 0x0 con su controlador, SRAM de 256 KB y la
//               ROM en 0x0100_0000 (con su alias en 0x0 al arrancar en ella)
//   reloj       SYSCTL: PIOSC, MOSC (OSC0/OSC1), LFIOSC, el PLL y MEMTIM0
//   periféricos con su RELOJ Y SU «LISTO» por clase (RCGC/PR): sin ellos, un
//               acceso es un error de bus, y el modelo dice cuál y por qué
//   reset       POR, el pin RST, SYSRESETREQ y los dos perros, con su causa en
//               RESC
//   pines       quince puertos sobre pads eléctricos; JTAG/SWD en PC0-PC3, que
//               son pines de puerto y no patillas dedicadas como en el P4
//
// Lo que todavía no está, DECLARADO: contesta, no guarda y avisa una vez.
// `limitaciones()` lo enumera.
// =============================================================================
#ifndef STM32_SOC_MSP432E4_MSP432E401Y_H
#define STM32_SOC_MSP432E4_MSP432E401Y_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <string>
#include <vector>
#include "e4_desc.h"
#include "rom_e4.h"
#include "../../core/cortex_m4f.h"
#include "../../common/clock_gen.h"
#include "../../bus/ahb_decoder.h"
#include "../../mem/sram.h"
#include "../../periph/bloque_declarado.h"
#include "../../periph/msp432e4/sysctl.h"
#include "../../periph/msp432e4/gpio.h"
#include "../../periph/msp432e4/flash.h"
#include "../../periph/msp432e4/watchdog.h"

namespace stm32 {

inline constexpr IntegracionNucleo INTEGRACION_MSP432E4{ false, 0, 0 };
inline constexpr MapaRam RAM_MSP432E4{ e4::SRAM_B, 0x00040000u, 0, 0, 0, 0, 0, 0 };

class SinDestinoE4 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<SinDestinoE4> tsk{"tsk"};
    explicit SinDestinoE4(sc_core::sc_module_name nm) : sc_core::sc_module(nm) {
        tsk.register_b_transport(this, &SinDestinoE4::bt);
    }
private:
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

class Msp432E4 : public sc_core::sc_module {
public:
    const McuCapsE4 mcu;

    CortexM4F core;

    // --- Memoria ---------------------------------------------------------------
    FlashE4      flash;
    FlashCtrlE4  flashc;
    Sram         sram;
    RomE4        rom{"rom"};
    SinDestinoE4 sin_ccm{"sin_ccm"};

    // --- Sistema ---------------------------------------------------------------
    SysctlE4   sysctl;
    GpioE4     gpio;
    WatchdogE4 wdt0{"wdt0", e4::WDT0_B, false}, wdt1{"wdt1", e4::WDT1_B, true};

    ClockGen g_sys{"g_sys"};
    AhbDecoder perif{"perif"};
    std::vector<BloqueDeclarado*> declarados;

    // --- Las patillas que no son de puerto -------------------------------------
    AnalogNet *vdd, *gnd, *vdda, *gnda, *vbat, *rst, *osc0, *osc1, *xosc0, *xosc1;

    tlm_utils::simple_target_socket_optional<Msp432E4> from_tb{"from_tb"};

    Msp432E4(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
             const Cableado& cab = Cableado(),
             const McuCapsE4& caps = MCU_MSP432E401Y)
        : sc_core::sc_module(nm), mcu(caps),
          core("core", dbg, caps.nucleo, RAM_MSP432E4, INTEGRACION_MSP432E4),
          flash("flash", caps.flash_size),
          flashc("flash_ctrl", caps.flash_size, caps.sram_size),
          sram("sram", e4::SRAM_B, caps.sram_size),
          sysctl("sysctl", caps.did0, caps.did1),
          gpio("gpio", caps.enc, cab),
          s_irq_("s_irq", e4::N_IRQ) {
        SC_HAS_PROCESS(Msp432E4);
        crea_patillas(cab);
        conecta_nucleo();
        conecta_bus();
        conecta_sistema();
        SC_THREAD(reset_proc);
        SC_METHOD(sysresetreq_proc); sensitive << s_sysresetreq_; dont_initialize();
        SC_METHOD(rst_perif_proc); sensitive << s_rst_perif_ << rst_perif_ev_;
        SC_METHOD(halt_proc); sensitive << core.debug.halted; dont_initialize();
    }
    ~Msp432E4() override {
        for (BloqueDeclarado* b : declarados) delete b;
        for (AnalogNet* n : propias_) delete n;
        for (auto& p : perifs_) delete p.rst;
    }

    // --- Lo que mira el banco y el adaptador -----------------------------------
    double f_sys() const { return sysctl.f_sys(); }
    unsigned avisos_flash() const { return n_av_flash_; }
    unsigned avisos_reloj() const { return n_av_reloj_; }
    unsigned avisos_sin_reloj() const { return n_sin_reloj_; }
    unsigned resets() const { return n_resets_; }
    bool en_reset() const { return !s_rst_core_.read(); }
    bool arranco_en_rom() const { return flashc.rmctl_ba; }
    void set_ondas(bool on) { g_sys.set_waveform(on); }
    bool poke8(uint32_t a, uint8_t v) {
        if (flash.poke_byte(a, v)) return true;
        if (a >= e4::SRAM_B && a < e4::SRAM_B + sram.size()) {
            uint8_t b = v; return sram.load(&b, 1, a - e4::SRAM_B);
        }
        return false;
    }
    std::vector<std::string> limitaciones() const {
        std::vector<std::string> l = {
            "UART, SSI, I2C, GPTM, ADC, uDMA, EEPROM e Hibernation estan DECLARADOS "
            "(T6, entregas 2 y 3); CAN, USB, EMAC, PWM, QEI, EPI, comparadores y el "
            "CCM (CRC, AES, DES, SHA/MD5), para T7",
            "los modos de sueno no paran relojes: WFI duerme el nucleo y nada mas "
            "(SCGC/DCGC y DSCLKCFG se guardan pero no cuentan)",
            "el SysTick con CLKSOURCE = 0 cuenta SYSCLK/8, como en los STM32; en el "
            "E4 es PIOSC/4",
            "SRGPIO no resetea un puerto suelto; los SRx de las demas clases si",
            "los perros con INTTYPE = 1 (NMI) no la piden: dan la interrupcion normal",
        };
        switch (rom.origen()) {
            case RomE4::Origen::Volcado:
                l.push_back("la ROM es la de TI (R2), de " + rom.ruta());
                break;
            case RomE4::Origen::Ninguna:
                l.push_back("sin ROM (R3): ni cargador serie ni DriverLib en ROM; con la "
                            "Flash en blanco el chip no arranca nada");
                break;
            default: break;
        }
        return l;
    }

private:
    sc_core::sc_signal<bool>   s_sys_{"s_sys"};
    sc_core::sc_signal<double> s_sys_hz_{"s_sys_hz"};
    sc_core::sc_signal<bool>   s_rst_core_{"s_rst_core"}, s_rst_perif_{"s_rst_perif"};
    sc_core::sc_signal<bool>   s_uno_{"s_uno"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_irq_;
    sc_core::sc_signal<bool>   s_nmi_{"s_nmi"}, s_sysresetreq_{"s_sysresetreq"},
                               s_sleeping_{"s_sleeping"}, s_sleepdeep_{"s_sleepdeep"},
                               s_evt_in_{"s_evt_in"}, s_evt_out_{"s_evt_out"},
                               s_stk_ext_{"s_stk_ext"};
    sc_core::sc_signal<uint8_t> s_boot_{"s_boot"}, s_dbg_lp_{"s_dbg_lp"};
    sc_core::sc_signal<bool>   s_swclk_{"s_swclk"}, s_swdio_i_{"s_swdio_i"},
                               s_swdio_o_{"s_swdio_o"}, s_swdio_oe_{"s_swdio_oe"},
                               s_jtdi_{"s_jtdi"}, s_jtdo_{"s_jtdo"}, s_njtrst_{"s_njtrst"},
                               s_uno_oe_{"s_uno_oe"};
    sc_core::sc_signal<bool>   s_freeze_[FZ_COUNT];
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_gpio_irq_{"s_gpio_irq", GpioE4::N_IRQ};

    tlm_utils::simple_target_socket<Msp432E4> t_i_{"t_i"}, t_d_{"t_d"}, t_s_{"t_s"};
    tlm_utils::simple_initiator_socket<Msp432E4> a_flash_{"a_flash"}, a_sram_{"a_sram"},
                                                 a_rom_{"a_rom"}, a_perif_{"a_perif"};
    std::vector<AnalogNet*> propias_;
    sc_core::sc_event ev_reset_, rst_perif_ev_;
    uint32_t causa_pedida_ = 0;
    unsigned n_resets_ = 0, n_av_flash_ = 0, n_av_reloj_ = 0, n_sin_reloj_ = 0;
    bool mal_flash_ = false, mal_reloj_ = false;

    // --- Los periféricos con reloj por clase -----------------------------------
    struct Perif {
        const char* nombre; uint32_t base, size; unsigned clase, bit;
        sc_core::sc_signal<bool>* rst; bool avisado;
    };
    std::vector<Perif> perifs_;
    const Perif* perif_de(uint64_t a) const {
        for (const Perif& p : perifs_) if (a >= p.base && a < uint64_t(p.base) + p.size) return &p;
        return nullptr;
    }
    // Registra un periférico: su ventana en el decodificador, su reloj, su
    // reset propio (el del sistema y su bit de SRx) y su sitio en la tabla
    void engancha(BusSlave& b, unsigned clase, unsigned bit) {
        perif.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
        b.clk(s_sys_); b.clk_hz(s_sys_hz_); b.clk_en(s_uno_);
        auto* r = new sc_core::sc_signal<bool>((std::string("rst_") + b.basename()).c_str());
        b.rst_n(*r);
        perifs_.push_back({b.basename(), b.base(), b.size(), clase, bit, r, false});
    }
    void engancha_sin_reloj(BusSlave& b) {
        perif.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
        b.clk(s_sys_); b.clk_hz(s_sys_hz_); b.clk_en(s_uno_); b.rst_n(s_rst_perif_);
    }
    void declara(const char* n, uint32_t base, uint32_t size, unsigned clase, unsigned bit,
                 const char* cuando) {
        BloqueDeclarado* b = new BloqueDeclarado(n, base, size, n, cuando);
        engancha(*b, clase, bit);
        declarados.push_back(b);
    }
    void rst_perif_proc() {
        for (Perif& p : perifs_)
            p.rst->write(s_rst_perif_.read() && !sysctl.en_reset_sw(p.clase, p.bit));
    }

    // --- Construcción -------------------------------------------------------
    AnalogNet* patilla(const Cableado& cab, const char* nombre) {
        if (analog_net_if* n = cab.busca_alim(nombre))
            if (AnalogNet* a = dynamic_cast<AnalogNet*>(n)) return a;
        AnalogNet* a = new AnalogNet((std::string("net_") + nombre).c_str());
        a->nombre_esq = nombre;
        propias_.push_back(a);
        return a;
    }
    int d_rst_pull_ = -1;
    void crea_patillas(const Cableado& cab) {
        vdd = patilla(cab, "VDD");   gnd = patilla(cab, "GND");
        vdda = patilla(cab, "VDDA"); gnda = patilla(cab, "GNDA");
        vbat = patilla(cab, "VBAT"); rst = patilla(cab, "RST");
        osc0 = patilla(cab, "OSC0"); osc1 = patilla(cab, "OSC1");
        xosc0 = patilla(cab, "XOSC0"); xosc1 = patilla(cab, "XOSC1");
        // RST con su pull-up dentro [1F: P-17 h]
        d_rst_pull_ = rst->register_driver("pullup_rst");
        rst->set_drive(d_rst_pull_, 3.3f, 50e3f);
    }

    void conecta_nucleo() {
        core.icode.bind(t_i_); core.dcode.bind(t_d_); core.sbus.bind(t_s_);
        core.ccm.bind(sin_ccm.tsk);
        core.fclk(s_sys_); core.fclk_hz(s_sys_hz_);
        core.systick_ext(s_stk_ext_); core.rst_n(s_rst_core_);
        for (unsigned i = 0; i < e4::N_IRQ; ++i) core.irq_in[i](s_irq_[i]);
        core.nmi_in(s_nmi_);
        core.sysresetreq(s_sysresetreq_);
        core.sleeping(s_sleeping_); core.sleepdeep(s_sleepdeep_);
        core.event_in(s_evt_in_); core.event_out(s_evt_out_);
        core.boot_mode(s_boot_);
        core.debug.swclk_tck(s_swclk_);
        core.debug.swdio_in(s_swdio_i_);
        core.debug.swdio_out(s_swdio_o_);
        core.debug.swdio_oe(s_swdio_oe_);
        core.debug.jtdi(s_jtdi_);
        core.debug.jtdo_swo(s_jtdo_);
        core.debug.njtrst(s_njtrst_);
        core.debug.dbg_lp(s_dbg_lp_);
        for (unsigned i = 0; i < FZ_COUNT; ++i) core.debug.freeze[i](s_freeze_[i]);
        s_njtrst_.write(true);
        // SWD/JTAG por PC0-PC3, función 1, que es como salen del reset
        FuncionE4 clk; clk.in = &s_swclk_; clk.idle_in = false; clk.nombre = "SWCLK";
        FuncionE4 dio; dio.in = &s_swdio_i_; dio.out = &s_swdio_o_; dio.oe = &s_swdio_oe_;
        dio.idle_in = true; dio.nombre = "SWDIO";
        FuncionE4 tdi; tdi.in = &s_jtdi_; tdi.nombre = "TDI";
        FuncionE4 tdo; tdo.out = &s_jtdo_; tdo.oe = &s_uno_oe_; tdo.nombre = "TDO/SWO";
        gpio.conecta_funcion(e4::PC, 0, 1, clk);
        gpio.conecta_funcion(e4::PC, 1, 1, dio);
        gpio.conecta_funcion(e4::PC, 2, 1, tdi);
        gpio.conecta_funcion(e4::PC, 3, 1, tdo);
        s_uno_oe_.write(true);
    }

    void conecta_bus() {
        t_i_.register_b_transport(this, &Msp432E4::bt);
        t_d_.register_b_transport(this, &Msp432E4::bt);
        t_s_.register_b_transport(this, &Msp432E4::bt);
        from_tb.register_b_transport(this, &Msp432E4::bt);
        for (auto* s : {&t_i_, &t_d_, &t_s_})
            s->register_transport_dbg(this, &Msp432E4::dbg);
        from_tb.register_transport_dbg(this, &Msp432E4::dbg);
        a_flash_.bind(flash.tsk);
        a_sram_.bind(sram.tsk);
        a_rom_.bind(rom.tsk);
        a_perif_.bind(perif.tsk);
        flash.ctrl = &flashc;
        flashc.mem = &flash;
        flash.sys_hz(s_sys_hz_);
        flash.fws = [this] { return sysctl.fws(); };
        sysctl.flash = &flashc;
        sram.hclk(s_sys_); sram.rst_n(s_rst_perif_);
        // Sin reloj por clase: el SYSCTL, el controlador de la Flash
        engancha_sin_reloj(sysctl);
        engancha_sin_reloj(flashc);
        engancha(gpio, SysctlE4::C_GPIO, 0);      // el reloj de cada puerto: perif_de
        engancha(wdt0, SysctlE4::C_WD, 0);
        engancha(wdt1, SysctlE4::C_WD, 1);
        using S = SysctlE4;
        const char* const E2 = "Llega en la entrega 2 de T6 (doc/analisis_msp432.md).";
        const char* const E3 = "Llega en la entrega 3 de T6 (doc/analisis_msp432.md).";
        const char* const T7 = "Llega en la fase T7 del plan de doc/analisis_msp432.md.";
        static const char* const NU[8] = {"uart0", "uart1", "uart2", "uart3", "uart4", "uart5", "uart6", "uart7"};
        static const char* const NS[4] = {"ssi0", "ssi1", "ssi2", "ssi3"};
        static const char* const NT[8] = {"timer0", "timer1", "timer2", "timer3", "timer4", "timer5", "timer6", "timer7"};
        static const char* const NI[10] = {"i2c0", "i2c1", "i2c2", "i2c3", "i2c4", "i2c5", "i2c6", "i2c7", "i2c8", "i2c9"};
        for (unsigned n = 0; n < 8; ++n) declara(NU[n], e4::UART0_B + 0x1000u * n, 0x1000, S::C_UART, n, E3);
        for (unsigned n = 0; n < 4; ++n) declara(NS[n], e4::SSI0_B + 0x1000u * n, 0x1000, S::C_SSI, n, E3);
        for (unsigned n = 0; n < 6; ++n) declara(NT[n], e4::TIMER0_B + 0x1000u * n, 0x1000, S::C_TIMER, n, E3);
        declara(NT[6], e4::TIMER6_B, 0x1000, S::C_TIMER, 6, E3);
        declara(NT[7], e4::TIMER7_B, 0x1000, S::C_TIMER, 7, E3);
        for (unsigned n = 0; n < 4; ++n) declara(NI[n], e4::I2C0_B + 0x1000u * n, 0x1000, S::C_I2C, n, E3);
        for (unsigned n = 4; n < 8; ++n) declara(NI[n], e4::I2C4_B + 0x1000u * (n - 4), 0x1000, S::C_I2C, n, E3);
        declara(NI[8], e4::I2C8_B, 0x1000, S::C_I2C, 8, E3);
        declara(NI[9], e4::I2C9_B, 0x1000, S::C_I2C, 9, E3);
        declara("adc0", e4::ADC0_B, 0x1000, S::C_ADC, 0, E3);
        declara("adc1", e4::ADC1_B, 0x1000, S::C_ADC, 1, E3);
        declara("udma", e4::UDMA_B, 0x1000, S::C_DMA, 0, E3);
        declara("hib", e4::HIB_B, 0x1000, S::C_HIB, 0, E3);
        declara("eeprom", e4::EEPROM_B, 0x1000, S::C_EEPROM, 0, E2);
        declara("pwm0", e4::PWM0_B, 0x1000, S::C_PWM, 0, T7);
        declara("qei0", e4::QEI0_B, 0x1000, S::C_QEI, 0, T7);
        declara("comp", e4::COMP_B, 0x1000, S::C_ACMP, 0, T7);
        declara("can0", e4::CAN0_B, 0x1000, S::C_CAN, 0, T7);
        declara("can1", e4::CAN1_B, 0x1000, S::C_CAN, 1, T7);
        declara("usb0", e4::USB0_B, 0x1000, S::C_USB, 0, T7);
        declara("epi0", e4::EPI0_B, 0x1000, S::C_EPI, 0, T7);
        declara("emac0", e4::EMAC0_B, 0x1000, S::C_EMAC, 0, T7);
        BloqueDeclarado* sx = new BloqueDeclarado("sysexc", e4::SYSEXC_B, 0x1000, "SYSEXC", T7);
        engancha_sin_reloj(*sx);
        declarados.push_back(sx);
        g_sys.clk(s_sys_); g_sys.freq_hz(s_sys_hz_);
        set_ondas(false);
        s_uno_.write(true);
    }

    void conecta_sistema() {
        sysctl.mosc.xtal_in = osc0;
        sysctl.al_cambiar = [this] { relojes(); };
        sysctl.al_cambiar_periferico = [this] { rst_perif_ev_.notify(sc_core::SC_ZERO_TIME); };
        sysctl.irq(s_irq_[e4::IRQ_SYSCTL]);
        flashc.irq(s_irq_[e4::IRQ_FLASH]);
        for (unsigned k = 0; k < GpioE4::N_IRQ; ++k) {
            const unsigned linea = k < e4::N_PUERTOS ? e4::irq_gpio(k)
                                 : (k < e4::N_PUERTOS + 7 ? e4::IRQ_GPIOP0 + 1 + (k - e4::N_PUERTOS)
                                                          : e4::IRQ_GPIOQ0 + 1 + (k - e4::N_PUERTOS - 7));
            gpio.irq[k](s_irq_[linea]);
        }
        // Los dos perros comparten línea
        wdt0.irq(s_wdt_irq_[0]); wdt1.irq(s_wdt_irq_[1]);
        for (WatchdogE4* w : {&wdt0, &wdt1}) {
            w->nucleo_parado = [this] { return core.debug.is_halted(); };
        }
        wdt0.f_reloj = [this] { return sysctl.f_sys(); };
        wdt1.f_reloj = [this] { return sysctl.f_piosc(); };
        wdt0.pide_reset = [this] { pide(SysctlE4::RESC_WDT0); };
        wdt1.pide_reset = [this] { pide(SysctlE4::RESC_WDT1); };
    }
    sc_core::sc_signal<bool> s_wdt_irq_[2];
    void wdt_irq_proc() { s_irq_[e4::IRQ_WATCHDOG].write(s_wdt_irq_[0].read() || s_wdt_irq_[1].read()); }
    void end_of_elaboration() override {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.set_sensitivity(&s_wdt_irq_[0].value_changed_event());
        o.set_sensitivity(&s_wdt_irq_[1].value_changed_event());
        sc_core::sc_spawn([this] { wdt_irq_proc(); }, "wdt_irq", &o);
    }

    // --- El bus ---------------------------------------------------------------
    void enruta(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        uint64_t a = gp.get_address();
        if (a < flash.size()) {
            // El alias de la ROM en 0x0 mientras se arranca en ella
            if (flashc.rmctl_ba && rom.presente() && a < e4::ROM_SIZE) {
                gp.set_address(a + e4::ROM_B);
                a_rom_->b_transport(gp, t);
                gp.set_address(a);
                return;
            }
            a_flash_->b_transport(gp, t); return;
        }
        if (a >= e4::ROM_B && a < e4::ROM_B + e4::ROM_SIZE) { a_rom_->b_transport(gp, t); return; }
        if (a >= e4::SRAM_B && a < e4::SRAM_B + sram.size()) { a_sram_->b_transport(gp, t); return; }
        if (a >= e4::PERIPH_B && a < e4::PERIPH_FIN) {
            if (!puede(a, t)) { gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
            a_perif_->b_transport(gp, t); return;
        }
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    // ¿Tiene reloj y está listo? Si no, error de bus, dicho una vez por periférico
    bool puede(uint64_t a, const sc_core::sc_time& t) {
        const Perif* p = perif_de(a);
        if (!p) return true;
        unsigned bit = p->bit;
        if (p->clase == SysctlE4::C_GPIO) bit = unsigned((a - e4::GPIO_B) >> 12);
        if (sysctl.listo(p->clase, bit, sc_core::sc_time_stamp() + t)) return true;
        ++n_sin_reloj_;
        Perif* q = const_cast<Perif*>(p);
        if (!q->avisado || p->clase == SysctlE4::C_GPIO) {
            static bool avisado_gpio[e4::N_PUERTOS] = {};
            if (p->clase == SysctlE4::C_GPIO) {
                if (bit < e4::N_PUERTOS && avisado_gpio[bit]) return false;
                if (bit < e4::N_PUERTOS) avisado_gpio[bit] = true;
            }
            q->avisado = true;
            static const char* const FILA[] = {"WD", "TIMER", "GPIO", "DMA", "EPI", "HIB",
                "UART", "SSI", "I2C", "?", "USB", "?", "EPHY", "CAN", "ADC", "ACMP", "PWM", "QEI"};
            const char* fila = p->clase < 18 ? FILA[p->clase] : (p->clase == SysctlE4::C_EEPROM ? "EEPROM"
                             : p->clase == SysctlE4::C_EMAC ? "EMAC" : p->clase == SysctlE4::C_CCM ? "CCM" : "?");
            char m[300];
            const bool reloj = sysctl.con_reloj(p->clase, bit);
            if (p->clase == SysctlE4::C_GPIO)
                std::snprintf(m, sizeof m, "acceso al GPIO%c (0x%08X) %s: error de bus. "
                              "%s", e4::LETRAS[bit], unsigned(a),
                              reloj ? "antes de que este listo" : "sin reloj",
                              reloj ? "Espera a su bit de PRGPIO (SysCtlPeripheralReady)"
                                    : "Pon su bit de RCGCGPIO (SysCtlPeripheralEnable)");
            else
                std::snprintf(m, sizeof m, "acceso a %s (0x%08X) %s: error de bus. %s%s, bit %u",
                              p->nombre, unsigned(a), reloj ? "antes de que este listo" : "sin reloj",
                              reloj ? "Espera a su bit de PR" : "Pon su bit de RCGC", fila, bit);
            SC_REPORT_WARNING("msp432e4", m);
        }
        return false;
    }
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) { enruta(gp, t); }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint64_t a = gp.get_address();
        if (a < flash.size()) {
            if (flashc.rmctl_ba && rom.presente() && a < e4::ROM_SIZE) {
                gp.set_address(a + e4::ROM_B);
                const unsigned n = a_rom_->transport_dbg(gp);
                gp.set_address(a);
                return n;
            }
            return a_flash_->transport_dbg(gp);
        }
        if (a >= e4::ROM_B && a < e4::ROM_B + e4::ROM_SIZE) return a_rom_->transport_dbg(gp);
        if (a >= e4::SRAM_B && a < e4::SRAM_B + sram.size()) return a_sram_->transport_dbg(gp);
        if (a >= e4::PERIPH_B && a < e4::PERIPH_FIN) return a_perif_->transport_dbg(gp);
        return 0;
    }

    // --- Relojes y topes --------------------------------------------------------
    void relojes() {
        g_sys.set_freq(sysctl.f_sys());
        wdt0.recalcula(); wdt1.recalcula();
        revisa_topes();
    }
    void revisa_topes() {
        const double f = sysctl.f_sys();
        const unsigned w = sysctl.fws(), wmin = e4::fws_minimo(f);
        const bool mal_r = f > e4::HZ_MAX * (1.0 + 1e-9);
        const bool mal_f = w < wmin;
        char m[260];
        if (mal_r && !mal_reloj_) {
            ++n_av_reloj_;
            std::snprintf(m, sizeof m, "SYSCLK = %.2f MHz: el maximo del MSP432E401Y es "
                          "120 MHz [DS]", f / 1e6);
            SC_REPORT_WARNING("msp432e4", m);
        }
        if (mal_f && !mal_flash_) {
            ++n_av_flash_;
            std::snprintf(m, sizeof m, "SYSCLK = %.2f MHz con MEMTIM0.FWS = %u en vigor: "
                          "hacen falta %u estados de espera, y en el silicio se leen datos "
                          "de la Flash equivocados. Escribe MEMTIM0 y luego RSCLKCFG con "
                          "MEMTIMU [DriverLib, g_sXTALtoMEMTIM]", f / 1e6, w, wmin);
            SC_REPORT_WARNING("msp432e4", m);
        }
        mal_reloj_ = mal_r;
        mal_flash_ = mal_f;
    }

    // --- Los resets -------------------------------------------------------------
    void pide(uint32_t causa) {
        causa_pedida_ |= causa;
        ev_reset_.notify(sc_core::SC_ZERO_TIME);
    }
    void sysresetreq_proc() { if (s_sysresetreq_.read()) pide(SysctlE4::RESC_SW); }
    void halt_proc() { wdt0.recalcula(); wdt1.recalcula(); }
    bool alimentado() const { return vdd->voltage() - gnd->voltage() > 2.7f; }
    bool pin_en_reset() const { return rst->voltage() - gnd->voltage() < 0.3f * 3.3f; }

    // ¿Arranca en la ROM? Con ROM, si la Flash está en blanco o si BOOTCFG,
    // fijado (NW = 0) y con EN, pide el cargador por un pin que está al nivel
    // de POL [1F: P-17 b]
    bool arranque_en_rom() {
        if (!rom.presente()) return false;
        if (flash.peek32(4) == 0xFFFFFFFFu) return true;
        const uint32_t b = flashc.bootcfg();
        if ((b & (1u << 31)) || !(b & (1u << 8))) return false;
        const unsigned port = (b >> 13) & 7u, pin = (b >> 10) & 7u;
        const bool nivel = gpio.nodo_de(port, pin).voltage() > 1.65f;
        return nivel == bool(b & (1u << 9));
    }

    // Todos los resets del E4 son de sistema: núcleo y periféricos
    void reset_proc() {
        const sc_core::sc_time PULSO(20, sc_core::SC_US);    // como en el P4
        s_rst_core_.write(false); s_rst_perif_.write(false);
        bool estaba = false, por = true;
        for (;;) {
            const bool ok = alimentado() && !pin_en_reset();
            if (!ok) {
                if (estaba) { s_rst_core_.write(false); s_rst_perif_.write(false); estaba = false; }
                if (!alimentado()) por = true;
                wait(vdd->value_changed_event() | rst->value_changed_event() |
                     gnd->value_changed_event());
                continue;
            }
            if (!estaba) {
                sysctl.anota_reset(por ? SysctlE4::RESC_POR : SysctlE4::RESC_EXT);
                por = false;
                causa_pedida_ = 0;
                ++n_resets_;
                wait(PULSO);
                if (!(alimentado() && !pin_en_reset())) continue;
                suelta();
                estaba = true;
                continue;
            }
            wait(vdd->value_changed_event() | rst->value_changed_event() |
                 gnd->value_changed_event() | ev_reset_);
            if (!causa_pedida_) continue;
            const uint32_t c = causa_pedida_;
            causa_pedida_ = 0;
            ++n_resets_;
            sysctl.anota_reset(c);
            s_rst_core_.write(false); s_rst_perif_.write(false);
            wait(PULSO);
            suelta();
        }
    }
    // Sale del reset: los periféricos primero (vuelven a su valor de reset),
    // luego la decisión de arranque y, un delta después, el núcleo
    void suelta() {
        s_rst_perif_.write(true);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        flashc.rmctl_ba = arranque_en_rom();
        s_rst_core_.write(true);
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP432E4_MSP432E401Y_H
