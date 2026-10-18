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
#include "../../periph/msp432e4/eeprom.h"
#include "../../periph/msp432e4/uart.h"
#include "../../periph/msp432e4/gptm.h"
#include "../../periph/msp432e4/i2c.h"
#include "../../periph/msp432e4/ssi.h"
#include "../../periph/msp432e4/adc.h"
#include "../../periph/msp432e4/udma.h"
#include "../../periph/msp432e4/hib.h"
#include "../../periph/msp432e4/ccm.h"
#include "../../periph/msp432e4/ccan.h"
#include "e4_programador.h"

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
    EepromE4   eeprom;

    // --- Comunicaciones, temporizadores y analógicos (entrega 3) ---------------
    UartE4  uart0{"uart0", e4::UART0_B + 0x0000u}, uart1{"uart1", e4::UART0_B + 0x1000u},
            uart2{"uart2", e4::UART0_B + 0x2000u}, uart3{"uart3", e4::UART0_B + 0x3000u},
            uart4{"uart4", e4::UART0_B + 0x4000u}, uart5{"uart5", e4::UART0_B + 0x5000u},
            uart6{"uart6", e4::UART0_B + 0x6000u}, uart7{"uart7", e4::UART0_B + 0x7000u};
    SsiE4   ssi0{"ssi0", e4::SSI0_B + 0x0000u}, ssi1{"ssi1", e4::SSI0_B + 0x1000u},
            ssi2{"ssi2", e4::SSI0_B + 0x2000u}, ssi3{"ssi3", e4::SSI0_B + 0x3000u};
    I2cE4   i2c0{"i2c0", e4::I2C0_B + 0x0000u}, i2c1{"i2c1", e4::I2C0_B + 0x1000u},
            i2c2{"i2c2", e4::I2C0_B + 0x2000u}, i2c3{"i2c3", e4::I2C0_B + 0x3000u},
            i2c4{"i2c4", e4::I2C4_B + 0x0000u}, i2c5{"i2c5", e4::I2C4_B + 0x1000u},
            i2c6{"i2c6", e4::I2C4_B + 0x2000u}, i2c7{"i2c7", e4::I2C4_B + 0x3000u},
            i2c8{"i2c8", e4::I2C8_B}, i2c9{"i2c9", e4::I2C9_B};
    GptmE4  timer0{"timer0", e4::TIMER0_B + 0x0000u}, timer1{"timer1", e4::TIMER0_B + 0x1000u},
            timer2{"timer2", e4::TIMER0_B + 0x2000u}, timer3{"timer3", e4::TIMER0_B + 0x3000u},
            timer4{"timer4", e4::TIMER0_B + 0x4000u}, timer5{"timer5", e4::TIMER0_B + 0x5000u},
            timer6{"timer6", e4::TIMER6_B}, timer7{"timer7", e4::TIMER7_B};
    AdcE4   adc0{"adc0", e4::ADC0_B}, adc1{"adc1", e4::ADC1_B};
    UdmaE4  udma{"udma"};
    HibE4   hib{"hib"};
    CcmE4   ccm{"ccm"};
    CCanE4  can0{"can0", e4::CAN0_B}, can1{"can1", e4::CAN1_B};
    CCanE4* can(unsigned n) { return n ? &can1 : &can0; }
    UartE4* uart(unsigned n) { UartE4* u[8] = {&uart0, &uart1, &uart2, &uart3, &uart4, &uart5, &uart6, &uart7}; return u[n & 7u]; }
    SsiE4*  ssi(unsigned n) { SsiE4* u[4] = {&ssi0, &ssi1, &ssi2, &ssi3}; return u[n & 3u]; }
    I2cE4*  i2c(unsigned n) { I2cE4* u[10] = {&i2c0, &i2c1, &i2c2, &i2c3, &i2c4, &i2c5, &i2c6, &i2c7, &i2c8, &i2c9}; return u[n < 10 ? n : 0]; }
    GptmE4* timer(unsigned n) { GptmE4* u[8] = {&timer0, &timer1, &timer2, &timer3, &timer4, &timer5, &timer6, &timer7}; return u[n & 7u]; }

    ClockGen g_sys{"g_sys"};
    AhbDecoder perif{"perif"};
    std::vector<BloqueDeclarado*> declarados;

    // --- Las patillas que no son de puerto -------------------------------------
    AnalogNet *vdd, *gnd, *vdda, *gnda, *vbat, *rst, *osc0, *osc1, *xosc0, *xosc1, *wake, *hibn;

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
          eeprom("eeprom", caps.eeprom_size),
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
    unsigned hibernaciones() const { return n_hib_; }
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
            "USB, EMAC, PWM, QEI, EPI y comparadores estan DECLARADOS: son de T7",
            "los C_CAN copian entre las interfaces y los objetos en el acto (BUSY no "
            "se ve), salen de bus-off al quitar INIT sin esperar 128 x 11 bits, y no "
            "tienen modo basico (CANTST.BASIC) ni control directo de CANTX",
            "el CCM calcula cada bloque en el acto; sin uDMA ni AES en CCM, CBC-MAC, "
            "XTS, f8 ni f9",
            "las UART sin IrDA, tarjeta, 9 bits ni lineas de modem; los SSI y los I2C, "
            "solo como maestros (SPI de Freescale); los GPTM sin RTC, sin las "
            "interrupciones del PWM ni TnWOT/TnSNAPS/TCACT; el ADC ideal, sin "
            "comparadores digitales, promediado ni DMA; la uDMA sin scatter-gather; "
            "la hibernacion sin calendario ni deteccion de manipulacion",
            "los modos de sueno no paran relojes: WFI duerme el nucleo y nada mas "
            "(SCGC/DCGC y DSCLKCFG se guardan pero no cuentan)",
            "el SysTick con CLKSOURCE = 0 cuenta SYSCLK/8, como en los STM32; en el "
            "E4 es PIOSC/4",
            "SRGPIO no resetea un puerto suelto; los SRx de las demas clases si",
            "los perros con INTTYPE = 1 (NMI) no la piden: dan la interrupcion normal",
            "la Flash y la EEPROM se programan con tiempos de modelo (30 us por palabra, "
            "15 ms por sector, 100 us por palabra de EEPROM), sin errores de "
            "verificacion (PROGRIS, ERRIS) ni reintentos (EESUPP); la EEPROM, sin "
            "contrasenas (EEPASS) ni bloques ocultos (EEHIDE) que cuenten",
        };
        switch (rom.origen()) {
            case RomE4::Origen::Volcado:
                l.push_back("la ROM es la de TI (R2), de " + rom.ruta());
                break;
            case RomE4::Origen::Ninguna:
                l.push_back("sin ROM (R3): ni cargador serie ni DriverLib en ROM; con la "
                            "Flash en blanco el chip no arranca nada");
                break;
            default:
                l.push_back("la ROM es la R1 (sustituta): DriverLib compilada en las casillas "
                            "de rom.h, salvo SMBus, SPI Flash y Update{I2C,SSI,USB,EMAC}, que "
                            "se paran en un BKPT; su cargador solo escucha por la UART0 "
                            "(PA0/PA1), sin SSI0, I2C0, USB ni Ethernet, y no comprueba CRC");
                break;
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

    tlm_utils::simple_target_socket<Msp432E4> t_i_{"t_i"}, t_d_{"t_d"}, t_s_{"t_s"},
                                              t_dma_{"t_dma"};
    sc_core::sc_signal<bool> s_rst_hib_{"s_rst_hib"};
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
            if (p.rst) p.rst->write(s_rst_perif_.read() && !sysctl.en_reset_sw(p.clase, p.bit));
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
    int d_rst_pull_ = -1, d_wake_pull_ = -1;
    bool hibernar_ = false;
    unsigned n_hib_ = 0;
    void crea_patillas(const Cableado& cab) {
        vdd = patilla(cab, "VDD");   gnd = patilla(cab, "GND");
        vdda = patilla(cab, "VDDA"); gnda = patilla(cab, "GNDA");
        vbat = patilla(cab, "VBAT"); rst = patilla(cab, "RST");
        osc0 = patilla(cab, "OSC0"); osc1 = patilla(cab, "OSC1");
        xosc0 = patilla(cab, "XOSC0"); xosc1 = patilla(cab, "XOSC1");
        wake = patilla(cab, "WAKE");   hibn = patilla(cab, "HIB");
        // WAKE con su pull-up dentro, como RST [1F: P-17 h]
        d_wake_pull_ = wake->register_driver("pullup_wake");
        wake->set_drive(d_wake_pull_, 3.3f, 50e3f);
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
        // El `load` de GDB programa esta Flash con su controlador
        ProgramadorE4::el().flash_size = mcu.flash_size;
        if (core.gdb) core.gdb->pon_programador(&ProgramadorE4::el());
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
        engancha(eeprom, SysctlE4::C_EEPROM, 0);
        using S = SysctlE4;
        const char* const T7 = "Llega en la fase T7 del plan de doc/analisis_msp432.md.";
        for (unsigned n = 0; n < 8; ++n) engancha(*uart(n), S::C_UART, n);
        for (unsigned n = 0; n < 4; ++n) engancha(*ssi(n), S::C_SSI, n);
        for (unsigned n = 0; n < 10; ++n) engancha(*i2c(n), S::C_I2C, n);
        for (unsigned n = 0; n < 8; ++n) engancha(*timer(n), S::C_TIMER, n);
        engancha(adc0, S::C_ADC, 0);
        engancha(adc1, S::C_ADC, 1);
        engancha(udma, S::C_DMA, 0);
        engancha(ccm, S::C_CCM, 0);
        engancha(can0, S::C_CAN, 0);
        engancha(can1, S::C_CAN, 1);
        // La hibernación: su reloj es RCGCHIB, pero su reset es el de VBAT
        perif.add_slave("s_hib", hib.base(), hib.size())->bind(hib.tsk);
        hib.clk(s_sys_); hib.clk_hz(s_sys_hz_); hib.clk_en(s_uno_); hib.rst_n(s_rst_hib_);
        perifs_.push_back({"hib", hib.base(), hib.size(), S::C_HIB, 0, nullptr, false});
        declara("pwm0", e4::PWM0_B, 0x1000, S::C_PWM, 0, T7);
        declara("qei0", e4::QEI0_B, 0x1000, S::C_QEI, 0, T7);
        declara("comp", e4::COMP_B, 0x1000, S::C_ACMP, 0, T7);
        declara("usb0", e4::USB0_B, 0x1000, S::C_USB, 0, T7);
        declara("epi0", e4::EPI0_B, 0x1000, S::C_EPI, 0, T7);
        declara("emac0", e4::EMAC0_B, 0x1000, S::C_EMAC, 0, T7);
        // La uDMA es otro maestro del bus
        t_dma_.register_b_transport(this, &Msp432E4::bt);
        t_dma_.register_transport_dbg(this, &Msp432E4::dbg);
        udma.mst.bind(t_dma_);
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
        eeprom.al_terminar = [this] { flashc.marca(FlashCtrlE4::RIS_ERIS); };
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
        conecta_perifericos();
    }

    // --- Los periféricos de la entrega 3 --------------------------------------
    struct PinFn { uint8_t p, i, pctl; const char* n; };
    static const PinFn* tabla_pines(size_t& n) {
        static const PinFn t[] = {
#include "e4_pines.inc"
        };
        n = sizeof t / sizeof t[0];
        return t;
    }
    // Los canales del ADC [TI-Drivers, Board_init.c de la LaunchPad]
    static constexpr uint8_t AIN[20][2] = {
        {e4::PE, 3}, {e4::PE, 2}, {e4::PE, 1}, {e4::PE, 0}, {e4::PD, 7}, {e4::PD, 6},
        {e4::PD, 5}, {e4::PD, 4}, {e4::PE, 5}, {e4::PE, 4}, {e4::PB, 4}, {e4::PB, 5},
        {e4::PD, 3}, {e4::PD, 2}, {e4::PD, 1}, {e4::PD, 0}, {e4::PK, 0}, {e4::PK, 1},
        {e4::PK, 2}, {e4::PK, 3}};
    void conecta_perifericos() {
        auto reloj_cs = [this](unsigned cs) { return cs == 5 ? sysctl.f_alt() : sysctl.f_sys(); };
        static const unsigned IRQ_UART[8] = {e4::IRQ_UART0, e4::IRQ_UART1, e4::IRQ_UART2, e4::IRQ_UART3,
                                             e4::IRQ_UART4, e4::IRQ_UART5, e4::IRQ_UART6, e4::IRQ_UART7};
        static const unsigned IRQ_SSI[4] = {e4::IRQ_SSI0, e4::IRQ_SSI1, e4::IRQ_SSI2, e4::IRQ_SSI3};
        static const unsigned IRQ_I2C[10] = {e4::IRQ_I2C0, e4::IRQ_I2C1, e4::IRQ_I2C2, e4::IRQ_I2C3,
                                             e4::IRQ_I2C4, e4::IRQ_I2C5, e4::IRQ_I2C6, e4::IRQ_I2C7,
                                             e4::IRQ_I2C8, e4::IRQ_I2C9};
        static const unsigned IRQ_TA[8] = {e4::IRQ_TIMER0A, e4::IRQ_TIMER1A, e4::IRQ_TIMER2A, e4::IRQ_TIMER3A,
                                           e4::IRQ_TIMER4A, e4::IRQ_TIMER5A, e4::IRQ_TIMER6A, e4::IRQ_TIMER7A};
        for (unsigned n = 0; n < 8; ++n) {
            uart(n)->irq(s_irq_[IRQ_UART[n]]);
            uart(n)->f_reloj = reloj_cs;
            timer(n)->irq_a(s_irq_[IRQ_TA[n]]);
            timer(n)->irq_b(s_irq_[IRQ_TA[n] + 1]);
            timer(n)->f_reloj = [this](bool alt) { return alt ? sysctl.f_alt() : sysctl.f_sys(); };
            timer(n)->dispara_adc = [this] { adc0.disparo_temporizador(); adc1.disparo_temporizador(); };
        }
        for (unsigned n = 0; n < 4; ++n) { ssi(n)->irq(s_irq_[IRQ_SSI[n]]); ssi(n)->f_reloj = reloj_cs; }
        for (unsigned n = 0; n < 10; ++n) i2c(n)->irq(s_irq_[IRQ_I2C[n]]);
        // Los pines, de la tabla de pin_map.h
        size_t np = 0;
        const PinFn* t = tabla_pines(np);
        for (size_t k = 0; k < np; ++k) {
            const std::string f = t[k].n;
            unsigned u = 0;
            FuncionE4 fn;
            if (std::sscanf(f.c_str(), "U%uRX", &u) == 1 && f.size() == 4 && f[2] == 'R') fn = uart(u)->fn_rx();
            else if (std::sscanf(f.c_str(), "U%uTX", &u) == 1 && f.size() == 4 && f[2] == 'T') fn = uart(u)->fn_tx();
            else if (f.rfind("I2C", 0) == 0) {
                u = unsigned(f[3] - '0');
                fn = f.find("SCL") != std::string::npos ? i2c(u)->fn_scl() : i2c(u)->fn_sda();
            } else if (f.rfind("SSI", 0) == 0) {
                u = unsigned(f[3] - '0');
                const std::string q = f.substr(4);
                fn = q == "CLK" ? ssi(u)->fn_clk() : q == "FSS" ? ssi(u)->fn_fss()
                   : q == "XDAT0" ? ssi(u)->fn_dat0() : ssi(u)->fn_dat1();
            } else if (f.rfind("CAN", 0) == 0) {
                u = unsigned(f[3] - '0');
                fn.nombre = t[k].n;
                if (f.substr(4) == "TX") { fn.out = &can(u)->tx_out; fn.oe = &can(u)->tx_oe; }
                else { fn.in = &can(u)->rx_in; fn.idle_in = true; }
            } else if (f[0] == 'T' && f.find("CCP") == 2) {
                u = unsigned(f[1] - '0');
                fn = timer(u)->fn_ccp(unsigned(f[5] - '0'));
            } else continue;
            gpio.conecta_funcion(t[k].p, t[k].i, t[k].pctl, fn);
        }
        // El ADC: los canales son los nodos de los pines, en modo analógico
        for (unsigned c = 0; c < 20; ++c) gpio.conecta_analogica(AIN[c][0], AIN[c][1], "AIN");
        for (AdcE4* a : {&adc0, &adc1}) {
            a->v_canal = [this](unsigned c) {
                return c < 20 ? double(gpio.nodo_de(AIN[c][0], AIN[c][1]).voltage()) - double(gnda->voltage()) : 0.0;
            };
            a->es_analogico = [this](unsigned c) { return c < 20 && gpio.es_analogico(AIN[c][0], AIN[c][1]); };
            a->v_ref = [this] { return double(vdda->voltage()) - double(gnda->voltage()); };
            a->f_reloj = [this](unsigned cs) {
                if (cs == 1) return sysctl.f_alt();
                return sysctl.pll_encendido() && sysctl.pll_enganchado() ? sysctl.f_vco() : sysctl.f_piosc();
            };
        }
        for (unsigned s = 0; s < 4; ++s) {
            adc0.irq[s](s_irq_[e4::IRQ_ADC0SS0 + s]);
            adc1.irq[s](s_irq_[e4::IRQ_ADC1SS0 + s]);
        }
        // La uDMA: las fuentes de la codificación 0 de CHMAP [DL: UDMA_CHn_*]
        udma.irq(s_irq_[e4::IRQ_UDMA]);
        udma.irq_err(s_irq_[e4::IRQ_UDMAERR]);
        udma.conecta_fuente(8, 0, &uart0.dma_rx, [this] { uart0.dma_hecho(false); });
        udma.conecta_fuente(9, 0, &uart0.dma_tx, [this] { uart0.dma_hecho(true); });
        udma.conecta_fuente(8, 1, &uart1.dma_rx, [this] { uart1.dma_hecho(false); });
        udma.conecta_fuente(9, 1, &uart1.dma_tx, [this] { uart1.dma_hecho(true); });
        udma.conecta_fuente(0, 1, &uart2.dma_rx, [this] { uart2.dma_hecho(false); });
        udma.conecta_fuente(1, 1, &uart2.dma_tx, [this] { uart2.dma_hecho(true); });
        udma.conecta_fuente(10, 0, &ssi0.dma_rx, [this] { ssi0.dma_hecho(false); });
        udma.conecta_fuente(11, 0, &ssi0.dma_tx, [this] { ssi0.dma_hecho(true); });
        udma.conecta_fuente(10, 1, &ssi1.dma_rx, [this] { ssi1.dma_hecho(false); });
        udma.conecta_fuente(11, 1, &ssi1.dma_tx, [this] { ssi1.dma_hecho(true); });
        // Los dos C_CAN; sin congelacion por el depurador
        can0.irq(s_irq_[e4::IRQ_CAN0]); can1.irq(s_irq_[e4::IRQ_CAN1]);
        can0.freeze(s_cero_); can1.freeze(s_cero_);
        // La hibernación
        hib.irq(s_irq_[e4::IRQ_HIB]);
        hib.osc.xtal_in = xosc0;
        hib.pide_hibernar = [this] { hibernar_ = true; ev_reset_.notify(sc_core::SC_ZERO_TIME); };
        hib.al_cambiar = [this] { if (((sysctl.rsclkcfg() >> 20) & 0xFu) == 4u) relojes(); };
        sysctl.f_rtc = [this] { return hib.f_rtc(); };
    }
    sc_core::sc_signal<bool> s_wdt_irq_[2];
    sc_core::sc_signal<bool> s_cero_{"s_cero"};
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
        if ((a >= e4::PERIPH_B && a < e4::PERIPH_FIN) || (a >= e4::CCM_B && a < e4::CCM_FIN)) {
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
        if ((a >= e4::PERIPH_B && a < e4::PERIPH_FIN) || (a >= e4::CCM_B && a < e4::CCM_FIN))
            return a_perif_->transport_dbg(gp);
        return 0;
    }

    // --- Relojes y topes --------------------------------------------------------
    void relojes() {
        g_sys.set_freq(sysctl.f_sys());
        wdt0.recalcula(); wdt1.recalcula();
        for (unsigned n = 0; n < 8; ++n) { uart(n)->recalcula(); timer(n)->recalcula(); }
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
                if (por) s_rst_hib_.write(false);      // el primer encendido: también VBAT
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
            if (hibernar_) { hiberna(); continue; }
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
    // La hibernación: todo apagado salvo el módulo HIB, hasta la alarma del
    // RTC o hasta que WAKE baje; la SRAM se pierde, y se arranca como de un
    // encendido con la causa en RESC.HIB [1F: P-17 i]
    void hiberna() {
        hibernar_ = false;
        ++n_hib_;
        s_rst_core_.write(false); s_rst_perif_.write(false);
        for (;;) {
            sc_core::sc_event_or_list l;
            l |= vdd->value_changed_event(); l |= rst->value_changed_event();
            l |= hib.alarma(); l |= wake->value_changed_event();
            if (!alimentado() || pin_en_reset()) break;
            if (hib.despierta_pin() && wake->voltage() < 0.3f * 3.3f) { hib.marca_externa(); break; }
            wait(l);
            if (hib.despierta_rtc() && hib.alarma().triggered()) break;
        }
        std::vector<uint8_t> ceros(sram.size(), 0);
        sram.load(ceros.data(), unsigned(ceros.size()), 0);
        sysctl.anota_reset(SysctlE4::RESC_HIB);
        ++n_resets_;
        wait(sc_core::sc_time(20, sc_core::SC_US));
        if (alimentado() && !pin_en_reset()) suelta();
    }
    void suelta() {
        s_rst_hib_.write(true);
        s_rst_perif_.write(true);
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
        flashc.rmctl_ba = arranque_en_rom();
        s_rst_core_.write(true);
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP432E4_MSP432E401Y_H
