// =============================================================================
// stm32f303.h — EL STM32F303VC (fase S1 del plan del F303)
//
// Un SoC PROPIO y no una derivada de `SocF4`, por lo que dice §3.4 del
// análisis: cambian el RCC, la Flash, el DMA, los buses (GPIO en AHB2, ADC en
// AHB3), la USART, el ADC y la mitad del SYSCFG, y heredar obligaría a tapar
// más de lo que se aprovecha. Lo que SÍ se aprovecha, tal cual:
//
//   el núcleo     `CortexM4F` con 82 IRQ y 4 bits [f3_desc.h], la CCM
//                 EJECUTABLE (`MapaRam::ccm_ejecutable`, rasgo de S0) y sin el
//                 alias de arranque del núcleo: el de 0x0 lo resuelve este SoC
//                 con su propio MEM_MODE
//   los pines     `PinMux` y `PowerPads` de siempre, con el LQFP100 del F303
//                 (generado) y sus funciones alternativas (generadas)
//   el GPIO       `GpioPort` con el rasgo `GPIO_F3`: en AHB2 y con BRR
//   el EXTI       `Exti` con `EXTI_F303`: 36 líneas en dos tandas
//   DBGMCU        el de `DebugSys`, con DEV_ID 0x422 y la tabla de congelación
//                 del F3 como dato
//
// y lo nuevo: `RccF3` (rcc/rcc_f3.h), `FlashF1` (mem/flash_f1.h), el SYSCFG y
// el PWR (syscfg_f3.h) y la memoria de sistema (aquí abajo).
//
// LA MATRIZ: decodificadores directos, como los MSP432, y no `AhbMatrix`. Con
// la CPU como único maestro los dos dan los mismos tiempos; la contención con
// los DMA, que es lo que la matriz mide, llega con los DMA en S3, y es allí
// donde se decide (§9, riesgo 6).
//
// LO DEMÁS, DECLARADO: ocupa su ventana, avisa una vez con la fase que le toca
// y lee cero (`periph/bloque_declarado.h`). `limitaciones()` lo enumera.
// =============================================================================
#ifndef STM32_SOC_STM32F3_STM32F303_H
#define STM32_SOC_STM32F3_STM32F303_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>
#include <string>
#include <vector>
#include "f3_desc.h"
#include "syscfg_f3.h"
#include "../../core/cortex_m4f.h"
#include "../../bus/ahb_decoder.h"
#include "../../mem/sram.h"
#include "../../mem/flash_f1.h"
#include "f3_programador.h"
#include "../../rcc/rcc_f3.h"
#include "../../pins/pin_mux.h"
#include "../../pins/power_pads.h"
#include "../../periph/gpio_port.h"
#include "../../periph/exti.h"
#include "../../periph/bloque_declarado.h"

namespace stm32 {

// La CCM ejecutable, a 0x1000_0000, 8 KB. Sin SRAM2 ni de respaldo.
inline constexpr MapaRam ram_f303(const F3Caps& c) {
    MapaRam r{ f3::SRAM_B, c.sram_size, 0, 0, f3::CCM_B, c.ccm_size, 0, 0 };
    r.ccm_ejecutable = true;
    return r;
}
// Sin el alias del núcleo: lo hace el SoC (MEM_MODE y el tamaño de cada
// memoria son del F3, no del F4). Sin ventana de fabricante en el PPB: el
// DBGMCU está donde en el F4 y lo atiende `DebugSys`.
inline constexpr IntegracionNucleo INTEGRACION_F303{ false, 0, 0 };

// Las líneas de congelación, en el orden en que este SoC las usa, y de qué bit
// de DBGMCU_APB1_FZ/APB2_FZ sale cada una [CMSIS: DBGMCU_APBx_FZ_*]
enum FzF3 : unsigned {
    FZ3_TIM2, FZ3_TIM3, FZ3_TIM4, FZ3_TIM6, FZ3_TIM7, FZ3_RTC, FZ3_WWDG, FZ3_IWDG,
    FZ3_I2C1, FZ3_I2C2, FZ3_CAN,
    FZ3_TIM1 = 16, FZ3_TIM8, FZ3_TIM15, FZ3_TIM16, FZ3_TIM17
};
inline constexpr int FZ_APB1_F303[16] = {0, 1, 2, 4, 5, 10, 11, 12, 21, 22, 25, -1, -1, -1, -1, -1};
inline constexpr int FZ_APB2_F303[5]  = {0, 1, 2, 3, 4};

// -----------------------------------------------------------------------------
// LA MEMORIA DE SISTEMA: 8 KB en 0x1FFF_D800, de solo lectura
//
// El cargador de ST no se puede distribuir; hasta S7 aquí hay un arranque que
// deja la marca 'MSYS' y se aparca (sysmem_s1.s). Al final de la ventana, los
// datos de fábrica: el UID, la calibración del sensor de temperatura y de
// VREFINT, y F_SIZE [CMSIS; HAL] [1F: valores típicos, f3_desc.h].
// -----------------------------------------------------------------------------
class SysMemF3 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<SysMemF3> tsk{"tsk"};
    static constexpr uint32_t SIZE = f3::SYSMEM_FIN - f3::SYSMEM_B;

    SysMemF3(sc_core::sc_module_name nm, uint16_t flash_kb)
        : sc_core::sc_module(nm), mem_(SIZE, 0xFF) {
        tsk.register_b_transport(this, &SysMemF3::bt);
        tsk.register_transport_dbg(this, &SysMemF3::dbg);
        static const uint8_t stub[] = {
#include "sysmem_s1.inc"
        };
        std::memcpy(mem_.data(), stub, sizeof stub);
        for (unsigned i = 0; i < 3; ++i) pon32(f3::UID_B + 4 * i, f3::UID[i]);
        pon16(f3::TS_CAL1_A, f3::TS_CAL1);
        pon16(f3::VREFINT_A, f3::VREFINT_CAL);
        pon16(f3::TS_CAL2_A, f3::TS_CAL2);
        pon16(f3::F_SIZE_A, flash_kb);
    }
    uint32_t peek32(uint32_t a) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) v |= uint32_t(mem_[a - f3::SYSMEM_B + i]) << (8 * i);
        return v;
    }
    unsigned escrituras() const { return n_esc_; }

private:
    std::vector<uint8_t> mem_;
    unsigned n_esc_ = 0;
    void pon16(uint32_t a, uint16_t v) { mem_[a - f3::SYSMEM_B] = uint8_t(v); mem_[a - f3::SYSMEM_B + 1] = uint8_t(v >> 8); }
    void pon32(uint32_t a, uint32_t v) { pon16(a, uint16_t(v)); pon16(a + 2, uint16_t(v >> 16)); }
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (a < f3::SYSMEM_B || a + n > f3::SYSMEM_FIN) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (gp.is_write()) { ++n_esc_; gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        std::memcpy(gp.get_data_ptr(), &mem_[a - f3::SYSMEM_B], n);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (a < f3::SYSMEM_B || a + n > f3::SYSMEM_FIN || !gp.is_read()) return 0;
        std::memcpy(gp.get_data_ptr(), &mem_[a - f3::SYSMEM_B], n);
        return n;
    }
};

class SocF3 : public sc_core::sc_module {
public:
    const F3Caps mcu;

    PinMux    pinmux;
    PowerPads pwr_pads;
    RccF3     rcc;
    CortexM4F core;

    // --- Memoria -----------------------------------------------------------------
    FlashF1  flash;
    Sram     sram;
    Sram     ccm;
    SysMemF3 sysmem;

    // --- Buses ---------------------------------------------------------------------
    AhbDecoder ahb1{"ahb1"}, ahb2{"ahb2"}, ahb3{"ahb3"}, apb1{"apb1"}, apb2{"apb2"};
    AhbApbBridge br_apb1{"br_apb1"}, br_apb2{"br_apb2"};

    // --- Sistema -------------------------------------------------------------------
    sc_core::sc_vector<GpioPort> gpio;     // A..F
    Exti      exti;
    SyscfgF3  syscfg{"syscfg"};
    PwrF3     pwr{"pwr"};
    std::vector<BloqueDeclarado*> declarados;
    static constexpr unsigned N_PUERTOS = 6;

    tlm_utils::simple_target_socket_optional<SocF3> from_tb{"from_tb"};

    SocF3(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
          const Cableado& cab = Cableado(), const F3Caps& caps = MCU_STM32F303VC)
        : sc_core::sc_module(nm), mcu(caps),
          pinmux("pinmux", cab, caps.enc),
          pwr_pads("pwr_pads", &cab),
          rcc("rcc"),
          core("core", dbg, caps.nucleo, ram_f303(caps), INTEGRACION_F303),
          flash("flash", f3::FLASH_R, f3::FLASH_B, caps.flash_size, f3::PAGINA, f3::OB_B),
          sram("sram", f3::SRAM_B, caps.sram_size),
          ccm("ccm", f3::CCM_B, caps.ccm_size),
          sysmem("sysmem", uint16_t(caps.flash_size / 1024u)),
          gpio("gpio", N_PUERTOS, [](const char* n, size_t i) {
                   return new GpioPort(n, unsigned(i), GPIO_F3); }),
          exti("exti", EXTI_F303),
          s_irq_("s_irq", caps.nucleo.n_irq),
          s_exti_gpio_("s_exti_gpio", N_GPIO_PORTS * N_PORT_PINS),
          s_exticr_("s_exticr", 16),
          s_exti_in_("s_exti_in", EXTI_F303.n_lineas - 16),
          s_exti_lin_("s_exti_lin", EXTI_F303.n_lineas - 16),
          s_freeze_("s_freeze", FZ_COUNT) {
        SC_HAS_PROCESS(SocF3);
        conecta_relojes();
        conecta_nucleo();
        conecta_bus();
        conecta_pines();
        conecta_irq();
        SC_METHOD(boot_proc);
        sensitive << s_boot0_ << s_sysrst_n_ << boot_ev_;
        SC_METHOD(irq_exti_proc);
        for (unsigned i = 0; i < s_exti_lin_.size(); ++i) sensitive << s_exti_lin_[i];
        dont_initialize();
        SC_METHOD(mco_proc); sensitive << mco_ev_;
        // Sin las ondas de los relojes internos, como el E4: lo que cuenta
        // ciclos trabaja con la frecuencia (common/clock_gen.h)
        // El `load` de GDB programa esta Flash con su FPEC (S2)
        ProgramadorF3::el().tam = mcu.flash_size;
        if (core.gdb) core.gdb->pon_programador(&ProgramadorF3::el());
        set_ondas(false);
    }
    ~SocF3() override { for (BloqueDeclarado* b : declarados) delete b; }

    // --- Lo que mira el adaptador y el banco ------------------------------------------
    double f_sysclk() const { return rcc.f_sysclk(); }
    double f_hclk()   const { return rcc.f_hclk(); }
    bool   en_reset() const { return rcc.en_reset(); }
    uint8_t mem_mode() const { return syscfg.mem_mode(); }
    unsigned avisos() const { return rcc.avisos() + flash.avisos(); }
    void set_ondas(bool on) { rcc.set_internal_waveforms(on); }
    // Escribir un byte de firmware donde vaya: Flash, SRAM o CCM
    bool poke8(uint32_t a, uint8_t v) {
        if (flash.poke8(a, v)) return true;
        if (a >= f3::SRAM_B && a < f3::SRAM_B + sram.size()) { uint8_t b = v; return sram.load(&b, 1, a - f3::SRAM_B); }
        if (a >= f3::CCM_B && a < f3::CCM_B + ccm.size()) { uint8_t b = v; return ccm.load(&b, 1, a - f3::CCM_B); }
        return false;
    }
    unsigned escrituras_ccm_protegidas() const { return n_ccm_prot_; }

    std::vector<std::string> limitaciones() const {
        return {
            "fases S1 y S2 del plan del F303: nucleo, reloj, Flash (lectura y FPEC: "
            "PG, PER, MER, bytes de opcion, OBL_LAUNCH), SRAM, CCM ejecutable, memoria "
            "de sistema, GPIO, EXTI y SYSCFG. El resto esta DECLARADO: contesta, lee "
            "cero y avisa una vez",
            "la proteccion de lectura (RDP) esta DECLARADA: se carga en FLASH_OBR, pero "
            "no bloquea al depurador ni borra la Flash al quitarla; la Flash se programa "
            "con tiempos tipicos del modelo (40 us, 20 ms por pagina)",
            "DMA1/2, USART1-3, UART4/5, SPI1-3 e I2S, I2C1/2, TIM1-17, IWDG, WWDG, RTC "
            "y CRC: fase S3",
            "ADC1-4, DAC1, COMP1-7, OPAMP1-4 y TSC: fase S5; USB y CAN: fase S6",
            "la memoria de sistema no lleva el cargador de ST (S7, opcional): arranca "
            "un stub que deja 'MSYS' en 0x2000_0000 y se aparca en wfe",
            "la calibracion de fabrica (TS_CAL1/2, VREFINT_CAL) y el UID son valores "
            "tipicos del modelo, no de un chip",
            "sin bajo consumo (Sleep, Stop, Standby) ni modelo de corriente; WFI solo "
            "duerme el nucleo",
            "la linea 81 de la FPU (FPU_IE) no llega al NVIC, como en el F4",
        };
    }

private:
    // --- Señales -------------------------------------------------------------------
    sc_core::sc_signal<bool>   s_hclk_{"s_hclk"}, s_pclk1_{"s_pclk1"}, s_pclk2_{"s_pclk2"};
    sc_core::sc_signal<bool>   s_tim1_{"timclk1"}, s_tim2_{"timclk2"}, s_stk_{"stkclk"};
    sc_core::sc_signal<bool>   s_rtc_{"rtcclk"}, s_lsi_{"lsiclk"};
    sc_core::sc_signal<double> s_hclk_hz_{"s_hclk_hz"}, s_pclk1_hz_{"s_pclk1_hz"},
                               s_pclk2_hz_{"s_pclk2_hz"}, s_tim1_hz_{"timclk1_hz"},
                               s_tim2_hz_{"timclk2_hz"}, s_rtc_hz_{"rtcclk_hz"},
                               s_lsi_hz_{"s_lsi_hz"};
    sc_core::sc_signal<bool>   s_sysrst_n_{"s_sysrst_n"}, s_bkprst_n_{"s_bkprst_n"},
                               s_nrst_drv_{"s_nrst_drv"}, s_por_ok_{"s_por_ok"},
                               s_nrst_n_{"s_nrst_n"}, s_boot0_{"s_boot0"},
                               s_bor_trip_{"s_bor_trip"}, s_dbp_{"s_dbp"};
    sc_core::sc_signal<double> s_vdd_{"s_vdd"}, s_vdda_{"s_vdda"}, s_vbat_{"s_vbat"};
    sc_core::sc_signal<double> s_idd_{"s_idd"}, s_ibat_{"s_ibat"};
    sc_core::sc_signal<uint8_t> s_bor_lev_{"s_bor_lev"}, s_boot_{"s_boot"},
                                s_boot_core_{"s_boot_core"}, s_dbg_lp_{"s_dbg_lp"};
    sc_core::sc_signal<bool>   s_true_{"s_true"}, s_false_{"s_false"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_irq_;
    sc_core::sc_signal<bool>   s_nmi_{"s_nmi"}, s_sysresetreq_{"s_sysresetreq"},
                               s_sleeping_{"s_sleeping"}, s_sleepdeep_{"s_sleepdeep"},
                               s_evt_in_{"s_evt_in"}, s_evt_out_{"s_evt_out"},
                               s_exti_wakeup_{"s_exti_wakeup"};
    sc_core::sc_signal<bool>   s_iwdg_rr_{"s_iwdg_rr"}, s_wwdg_rr_{"s_wwdg_rr"};
    sc_core::sc_vector<sc_core::sc_signal<bool>>    s_exti_gpio_;
    sc_core::sc_vector<sc_core::sc_signal<uint8_t>> s_exticr_;
    sc_core::sc_vector<sc_core::sc_signal<bool>>    s_exti_in_, s_exti_lin_;
    sc_core::sc_vector<sc_core::sc_signal<bool>>    s_freeze_;
    sc_core::sc_signal<bool> s_swdio_o_{"s_swdio_o"}, s_swdio_oe_{"s_swdio_oe"},
                             s_jtdo_{"s_jtdo"}, s_dbg_swclk_{"s_dbg_swclk"},
                             s_dbg_swdio_i_{"s_dbg_swdio_i"}, s_dbg_jtdi_{"s_dbg_jtdi"},
                             s_dbg_njtrst_{"s_dbg_njtrst"}, s_mco_oe_{"s_mco_oe"};
    sc_core::sc_event mco_ev_, boot_ev_;
    unsigned n_ccm_prot_ = 0;
    bool avisado_ccm_ = false;

    tlm_utils::simple_target_socket<SocF3> t_i_{"t_i"}, t_d_{"t_d"}, t_s_{"t_s"}, t_ccm_{"t_ccm"};
    tlm_utils::simple_initiator_socket<SocF3> a_flash_{"a_flash"}, a_ob_{"a_ob"},
        a_sram_{"a_sram"}, a_ccm_{"a_ccm"}, a_sysmem_{"a_sysmem"}, a_apb1_{"a_apb1"},
        a_apb2_{"a_apb2"}, a_ahb1_{"a_ahb1"}, a_ahb2_{"a_ahb2"}, a_ahb3_{"a_ahb3"};

    // --- Construcción --------------------------------------------------------------------
    void conecta_relojes() {
        rcc.hclk(s_hclk_);       rcc.hclk_hz(s_hclk_hz_);
        rcc.pclk1(s_pclk1_);     rcc.pclk1_hz(s_pclk1_hz_);
        rcc.pclk2(s_pclk2_);     rcc.pclk2_hz(s_pclk2_hz_);
        rcc.timclk1(s_tim1_);    rcc.timclk1_hz(s_tim1_hz_);
        rcc.timclk2(s_tim2_);    rcc.timclk2_hz(s_tim2_hz_);
        rcc.systick_ext(s_stk_);
        rcc.rtcclk(s_rtc_);      rcc.rtcclk_hz(s_rtc_hz_);
        rcc.lsi_clk(s_lsi_);     rcc.lsi_hz(s_lsi_hz_);
        rcc.sys_rst_n(s_sysrst_n_); rcc.bkp_rst_n(s_bkprst_n_);
        rcc.drive_nrst_low(s_nrst_drv_);
        rcc.por_ok(s_por_ok_);   rcc.nrst_in_n(s_nrst_n_);
        rcc.sysresetreq(s_sysresetreq_);
        rcc.iwdg_rst_req(s_iwdg_rr_); rcc.wwdg_rst_req(s_wwdg_rr_);
        rcc.dbp(s_dbp_);
        rcc.nmi_css(s_nmi_);
        rcc.irq(s_irq_[f3::IRQ_RCC]);
        rcc.clk(s_hclk_); rcc.clk_hz(s_hclk_hz_); rcc.rst_n(s_true_); rcc.clk_en(s_true_);
        // La alimentación y el pin de reset
        pwr_pads.por_ok(s_por_ok_);
        pwr_pads.bor_trip(s_bor_trip_);
        pwr_pads.bor_lev(s_bor_lev_);         // el F303 no tiene BOR programable: 11 (apagado)
        pwr_pads.nrst_in_n(s_nrst_n_);
        pwr_pads.boot0_lvl(s_boot0_);
        pwr_pads.vdd_lvl(s_vdd_);
        pwr_pads.vdda_lvl(s_vdda_);
        pwr_pads.vbat_lvl(s_vbat_);
        pwr_pads.drive_nrst_low(s_nrst_drv_);
        pwr_pads.idd_req(s_idd_);
        pwr_pads.ibat_req(s_ibat_);
        pinmux.standby(s_false_);
        pinmux.wkup_en(s_false_);
        pinmux.dbg_pins(s_false_);
        // El HSE por PF0-OSC_IN, el LSE por PC14-OSC32_IN [PINDATA]
        rcc.hse.xtal_in = &pinmux.analog(5, 0);
        rcc.hse.ext_in  = &pinmux.pad_din[5 * N_PORT_PINS + 0];
        rcc.lse.xtal_in = &pinmux.analog(2, 14);
        rcc.lse.ext_in  = &pinmux.pad_din[2 * N_PORT_PINS + 14];
        rcc.al_cambiar = [this] { flash.revisa(); mco_ev_.notify(sc_core::SC_ZERO_TIME); };
        rcc.al_cargar_opciones = [this] { flash.recarga_opciones(); boot_ev_.notify(sc_core::SC_ZERO_TIME); };
        s_bor_lev_.write(3);
        s_true_.write(true);
    }

    void conecta_nucleo() {
        core.icode.bind(t_i_); core.dcode.bind(t_d_); core.sbus.bind(t_s_);
        core.ccm.bind(t_ccm_);
        core.fclk(s_hclk_); core.fclk_hz(s_hclk_hz_);
        core.systick_ext(s_stk_); core.rst_n(s_sysrst_n_);
        for (unsigned i = 0; i < mcu.nucleo.n_irq; ++i) core.irq_in[i](s_irq_[i]);
        core.nmi_in(s_nmi_);
        core.sysresetreq(s_sysresetreq_);
        core.sleeping(s_sleeping_); core.sleepdeep(s_sleepdeep_);
        core.event_in(s_evt_in_); core.event_out(s_evt_out_);
        core.boot_mode(s_boot_core_);
        core.debug.set_idcode(mcu.idcode());
        core.debug.set_mapa_congelacion(FZ_APB1_F303, FZ_APB2_F303);
        core.debug.swclk_tck(s_dbg_swclk_);
        core.debug.swdio_in(s_dbg_swdio_i_);
        core.debug.swdio_out(s_swdio_o_);
        core.debug.swdio_oe(s_swdio_oe_);
        core.debug.jtdi(s_dbg_jtdi_);
        core.debug.jtdo_swo(s_jtdo_);
        core.debug.njtrst(s_dbg_njtrst_);
        core.debug.dbg_lp(s_dbg_lp_);
        for (unsigned i = 0; i < FZ_COUNT; ++i) core.debug.freeze[i](s_freeze_[i]);
    }

    template <class B>
    void engancha(B& b, AhbDecoder& d, sc_core::sc_signal<bool>& clk,
                  sc_core::sc_signal<double>& hz, unsigned id) {
        d.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
        b.clk(clk); b.clk_hz(hz);
        b.rst_n(rcc.s_rst_n[id]); b.clk_en(rcc.s_en[id]);
        b.clk_en_live = rcc.clk_en_ptr(id);
    }
    template <class B>
    void engancha_sin_reloj(B& b, AhbDecoder& d, sc_core::sc_signal<bool>& clk,
                            sc_core::sc_signal<double>& hz) {
        d.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
        b.clk(clk); b.clk_hz(hz); b.rst_n(s_sysrst_n_); b.clk_en(s_true_);
    }
    void declara(const char* n, uint32_t base, uint32_t size, AhbDecoder& d,
                 sc_core::sc_signal<bool>& clk, sc_core::sc_signal<double>& hz,
                 int id, const char* cuando) {
        BloqueDeclarado* b = new BloqueDeclarado(n, base, size, n, cuando);
        if (id >= 0) engancha(*b, d, clk, hz, unsigned(id));
        else engancha_sin_reloj(*b, d, clk, hz);
        declarados.push_back(b);
    }

    void conecta_bus() {
        for (auto* s : {&t_i_, &t_d_, &t_s_}) {
            s->register_b_transport(this, &SocF3::bt);
            s->register_transport_dbg(this, &SocF3::dbg);
        }
        t_ccm_.register_b_transport(this, &SocF3::bt_ccm);
        t_ccm_.register_transport_dbg(this, &SocF3::dbg_ccm);
        from_tb.register_b_transport(this, &SocF3::bt);
        from_tb.register_transport_dbg(this, &SocF3::dbg);
        a_flash_.bind(flash.mem);  a_ob_.bind(flash.ob);
        a_sram_.bind(sram.tsk);    a_ccm_.bind(ccm.tsk);
        a_sysmem_.bind(sysmem.tsk);
        a_apb1_.bind(br_apb1.ahb); a_apb2_.bind(br_apb2.ahb);
        a_ahb1_.bind(ahb1.tsk);    a_ahb2_.bind(ahb2.tsk);   a_ahb3_.bind(ahb3.tsk);
        br_apb1.pclk(s_pclk1_); br_apb1.pclk_hz(s_pclk1_hz_); br_apb1.apb.bind(apb1.tsk);
        br_apb2.pclk(s_pclk2_); br_apb2.pclk_hz(s_pclk2_hz_); br_apb2.apb.bind(apb2.tsk);
        sram.hclk(s_hclk_); sram.rst_n(s_sysrst_n_);
        ccm.hclk(s_hclk_);  ccm.rst_n(s_sysrst_n_);
        flash.irq(s_irq_[f3::IRQ_FLASH]);
        flash.al_obl = [this] { rcc.pide_reset_obl(); };
        flash.latencia_minima = [](double f) { return f3::latencia_minima(f); };

        const char* S3 = "Es de la fase S3 del plan (doc/analisis_stm32f303.md, S3).";
        const char* S5 = "Es de la fase S5 del plan (doc/analisis_stm32f303.md, S5).";
        const char* S6 = "Es de la fase S6 del plan (doc/analisis_stm32f303.md, S6).";
        auto& H = s_hclk_;  auto& Hz = s_hclk_hz_;
        auto& P1 = s_pclk1_; auto& P1z = s_pclk1_hz_;
        auto& P2 = s_pclk2_; auto& P2z = s_pclk2_hz_;
        // --- AHB1 --------------------------------------------------------------------
        declara("dma1", f3::DMA1_B, 0x400, ahb1, H, Hz, F3P_DMA1, S3);
        declara("dma2", f3::DMA2_B, 0x400, ahb1, H, Hz, F3P_DMA2, S3);
        ahb1.add_slave("s_rcc", rcc.base(), rcc.size())->bind(rcc.tsk);   // sus relojes, arriba
        engancha(flash, ahb1, H, Hz, F3P_FLITF);
        declara("crc", f3::CRC_B, 0x400, ahb1, H, Hz, F3P_CRC, S3);
        declara("tsc", f3::TSC_B, 0x400, ahb1, H, Hz, F3P_TSC, S5);
        // --- AHB2: los GPIO ------------------------------------------------------------
        for (unsigned p = 0; p < N_PUERTOS; ++p) engancha(gpio[p], ahb2, H, Hz, F3P_GPIOA + p);
        // --- AHB3: los ADC -------------------------------------------------------------
        declara("adc12", f3::ADC12_B, 0x400, ahb3, H, Hz, F3P_ADC12, S5);
        declara("adc34", f3::ADC34_B, 0x400, ahb3, H, Hz, F3P_ADC34, S5);
        // --- APB1 ----------------------------------------------------------------------
        declara("tim2", f3::TIM2_B, 0x400, apb1, P1, P1z, F3P_TIM2, S3);
        declara("tim3", f3::TIM3_B, 0x400, apb1, P1, P1z, F3P_TIM3, S3);
        declara("tim4", f3::TIM4_B, 0x400, apb1, P1, P1z, F3P_TIM4, S3);
        declara("tim6", f3::TIM6_B, 0x400, apb1, P1, P1z, F3P_TIM6, S3);
        declara("tim7", f3::TIM7_B, 0x400, apb1, P1, P1z, F3P_TIM7, S3);
        declara("rtc", f3::RTC_B, 0x400, apb1, P1, P1z, -1, S3);
        declara("wwdg", f3::WWDG_B, 0x400, apb1, P1, P1z, F3P_WWDG, S3);
        declara("iwdg", f3::IWDG_B, 0x400, apb1, P1, P1z, -1, S3);
        declara("i2s2ext", f3::I2S2EXT_B, 0x400, apb1, P1, P1z, F3P_SPI2, S3);
        declara("spi2", f3::SPI2_B, 0x400, apb1, P1, P1z, F3P_SPI2, S3);
        declara("spi3", f3::SPI3_B, 0x400, apb1, P1, P1z, F3P_SPI3, S3);
        declara("i2s3ext", f3::I2S3EXT_B, 0x400, apb1, P1, P1z, F3P_SPI3, S3);
        declara("usart2", f3::USART2_B, 0x400, apb1, P1, P1z, F3P_USART2, S3);
        declara("usart3", f3::USART3_B, 0x400, apb1, P1, P1z, F3P_USART3, S3);
        declara("uart4", f3::UART4_B, 0x400, apb1, P1, P1z, F3P_UART4, S3);
        declara("uart5", f3::UART5_B, 0x400, apb1, P1, P1z, F3P_UART5, S3);
        declara("i2c1", f3::I2C1_B, 0x400, apb1, P1, P1z, F3P_I2C1, S3);
        declara("i2c2", f3::I2C2_B, 0x400, apb1, P1, P1z, F3P_I2C2, S3);
        declara("usb", f3::USB_B, 0x400, apb1, P1, P1z, F3P_USB, S6);
        declara("usb_pma", f3::USB_PMA_B, 0x400, apb1, P1, P1z, F3P_USB, S6);
        declara("can", f3::CAN_B, 0x400, apb1, P1, P1z, F3P_CAN, S6);
        engancha(pwr, apb1, P1, P1z, F3P_PWR);
        declara("dac1", f3::DAC1_B, 0x400, apb1, P1, P1z, F3P_DAC1, S5);
        // --- APB2 ----------------------------------------------------------------------
        engancha(syscfg, apb2, P2, P2z, F3P_SYSCFG);
        engancha_sin_reloj(exti, apb2, P2, P2z);
        declara("tim1", f3::TIM1_B, 0x400, apb2, P2, P2z, F3P_TIM1, S3);
        declara("spi1", f3::SPI1_B, 0x400, apb2, P2, P2z, F3P_SPI1, S3);
        declara("tim8", f3::TIM8_B, 0x400, apb2, P2, P2z, F3P_TIM8, S3);
        declara("usart1", f3::USART1_B, 0x400, apb2, P2, P2z, F3P_USART1, S3);
        declara("tim15", f3::TIM15_B, 0x400, apb2, P2, P2z, F3P_TIM15, S3);
        declara("tim16", f3::TIM16_B, 0x400, apb2, P2, P2z, F3P_TIM16, S3);
        declara("tim17", f3::TIM17_B, 0x400, apb2, P2, P2z, F3P_TIM17, S3);
        // El PWR abre el dominio de respaldo
        pwr.dbp(s_dbp_);
        // El SYSCFG: EXTICR hacia el EXTI, y los pines de arranque
        syscfg.boot(s_boot_);
        for (unsigned l = 0; l < 16; ++l) {
            syscfg.exticr_sel[l](s_exticr_[l]);
            exti.exticr_sel[l](s_exticr_[l]);
        }
    }

    void conecta_pines() {
        for (unsigned p = 0; p < N_PUERTOS; ++p) {
            gpio[p].mux = &pinmux;
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                const unsigned k = p * N_PORT_PINS + i;
                gpio[p].pad_drive[i](pinmux.gpio_drive[k]);
                gpio[p].pad_din[i](pinmux.pad_din[k]);
                gpio[p].exti_line[i](s_exti_gpio_[k]);
            }
        }
        for (unsigned k = 0; k < N_GPIO_PORTS * N_PORT_PINS; ++k) exti.gpio_line[k](s_exti_gpio_[k]);
        auto af = [](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                     sc_core::sc_signal<bool>* i, bool idle = true) {
            AfEndpoint ep; ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = idle; return ep;
        };
        // AF0, el sistema [f3_pines.inc: SYS_*, RCC_MCO]
        pinmux.connect_af(0, 13, 0, af(&s_swdio_o_, &s_swdio_oe_, &s_dbg_swdio_i_, false));
        pinmux.connect_af(0, 14, 0, af(nullptr, nullptr, &s_dbg_swclk_, false));
        pinmux.connect_af(0, 15, 0, af(nullptr, nullptr, &s_dbg_jtdi_, false));
        pinmux.connect_af(1,  3, 0, af(&s_jtdo_, &s_true_, nullptr));
        pinmux.connect_af(1,  4, 0, af(nullptr, nullptr, &s_dbg_njtrst_, true));
        pinmux.connect_af(0,  8, 0, af(&rcc.mco_sig, &s_true_, nullptr));
        // EVENTOUT (AF15), en los pines en que la base de pines la pone
        for (const PinAf& x : tabla_af())
            if (x.af == 15 && std::strcmp(x.senal, "EVENTOUT") == 0)
                pinmux.connect_af(x.puerto, x.pin, 15, af(&s_evt_out_, &s_true_, nullptr));
    }

public:
    // La tabla de funciones alternativas del encapsulado, generada [f3_pines.inc]
    struct PinAf { uint8_t puerto, pin, af; const char* senal; };
    static const std::vector<PinAf>& tabla_af() {
        static const std::vector<PinAf> t = {
#include "f3_pines.inc"
        };
        return t;
    }

private:
    void conecta_irq() {
        exti.irq_exti0(s_irq_[f3::IRQ_EXTI0]);
        exti.irq_exti1(s_irq_[f3::IRQ_EXTI1]);
        exti.irq_exti2(s_irq_[f3::IRQ_EXTI2_TSC]);
        exti.irq_exti3(s_irq_[f3::IRQ_EXTI3]);
        exti.irq_exti4(s_irq_[f3::IRQ_EXTI4]);
        exti.irq_exti9_5(s_irq_[f3::IRQ_EXTI9_5]);
        exti.irq_exti15_10(s_irq_[f3::IRQ_EXTI15_10]);
        for (unsigned i = 0; i < s_exti_lin_.size(); ++i) {
            exti.linea_in[i](s_exti_in_[i]);
            exti.linea_irq[i](s_exti_lin_[i]);
        }
        exti.event_out(s_evt_in_);
        exti.wakeup(s_exti_wakeup_);
    }
    // Las líneas 16-35 del EXTI hacia el NVIC, con las que comparten vector
    // [CMSIS: comentarios de IRQn_Type]. Las directas no tienen salida.
    void irq_exti_proc() {
        auto l = [&](unsigned n) { return n >= 16 && n - 16 < s_exti_lin_.size() && s_exti_lin_[n - 16].read(); };
        s_irq_[f3::IRQ_PVD].write(l(16));
        s_irq_[f3::IRQ_RTC_ALARM].write(l(17));
        s_irq_[f3::IRQ_USB_WAKEUP].write(l(18));
        s_irq_[f3::IRQ_TAMP_STAMP].write(l(19));
        s_irq_[f3::IRQ_RTC_WKUP].write(l(20));
        s_irq_[f3::IRQ_COMP1_2_3].write(l(21) || l(22) || l(29));
        s_irq_[f3::IRQ_COMP4_5_6].write(l(30) || l(31) || l(32));
        s_irq_[f3::IRQ_COMP7].write(l(33));
    }

    // BOOT0 y nBOOT1 (bit 4 del byte USER, cargado en FLASH_OBR) deciden
    // MEM_MODE mientras dura el reset [1F: RM0316 §3.5]:
    //   BOOT0 = 0                 la Flash
    //   BOOT0 = 1, nBOOT1 = 1     la memoria de sistema
    //   BOOT0 = 1, nBOOT1 = 0     la SRAM
    void boot_proc() {
        if (s_sysrst_n_.read()) return;
        uint8_t m = SyscfgF3::MEM_FLASH;
        if (s_boot0_.read()) m = flash.nboot1() ? SyscfgF3::MEM_SISTEMA : SyscfgF3::MEM_SRAM;
        s_boot_.write(m);
    }
    // La onda del MCO solo cuando PA8 la saca de verdad: es cara
    void mco_proc() { rcc.mco_onda(rcc.f_mco() > 0.0); }

    // --- El bus -------------------------------------------------------------------------
    // El alias de 0x0: la memoria que dice MEM_MODE, hasta su tamaño
    bool alias(uint64_t& a) const {
        if (a >= 0x08000000ull) return true;
        switch (syscfg.mem_mode()) {
            case SyscfgF3::MEM_SISTEMA:
                if (a >= SysMemF3::SIZE) return false;
                a += f3::SYSMEM_B; return true;
            case SyscfgF3::MEM_SRAM:
                if (a >= sram.size()) return false;
                a += f3::SRAM_B; return true;
            default:
                if (a >= flash.size()) return false;
                a += f3::FLASH_B; return true;
        }
    }
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a0 = gp.get_address();
        uint64_t a = a0;
        if (!alias(a)) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        gp.set_address(a);
        enruta(gp, t);
        gp.set_address(a0);
    }
    void enruta(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (flash.es_flash(uint32_t(a)) && a < 0x10000000ull) { a_flash_->b_transport(gp, t); return; }
        if (a >= f3::CCM_B && a < uint64_t(f3::CCM_B) + ccm.size()) { bt_ccm(gp, t); return; }
        if (a >= f3::SYSMEM_B && a < f3::SYSMEM_FIN) { a_sysmem_->b_transport(gp, t); return; }
        if (a >= f3::OB_B && a < f3::OB_B + f3::OB_SIZE) { a_ob_->b_transport(gp, t); return; }
        if (a >= f3::SRAM_B && a < uint64_t(f3::SRAM_B) + sram.size()) { a_sram_->b_transport(gp, t); return; }
        if (a >= f3::APB1_B && a < f3::APB1_FIN) { a_apb1_->b_transport(gp, t); return; }
        if (a >= f3::APB2_B && a < f3::APB2_FIN) { a_apb2_->b_transport(gp, t); return; }
        if (a >= f3::AHB1_B && a < f3::AHB1_FIN) { a_ahb1_->b_transport(gp, t); return; }
        if (a >= f3::AHB2_B && a < f3::AHB2_FIN) { a_ahb2_->b_transport(gp, t); return; }
        if (a >= f3::AHB3_B && a < f3::AHB3_FIN) { a_ahb3_->b_transport(gp, t); return; }
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint64_t a0 = gp.get_address();
        uint64_t a = a0;
        if (!alias(a)) return 0;
        gp.set_address(a);
        unsigned n = 0;
        if (flash.es_flash(uint32_t(a)) && a < 0x10000000ull) n = a_flash_->transport_dbg(gp);
        else if (a >= f3::CCM_B && a < uint64_t(f3::CCM_B) + ccm.size()) n = a_ccm_->transport_dbg(gp);
        else if (a >= f3::SYSMEM_B && a < f3::SYSMEM_FIN) n = a_sysmem_->transport_dbg(gp);
        else if (a >= f3::OB_B && a < f3::OB_B + f3::OB_SIZE) n = a_ob_->transport_dbg(gp);
        else if (a >= f3::SRAM_B && a < uint64_t(f3::SRAM_B) + sram.size()) n = a_sram_->transport_dbg(gp);
        else if (a >= f3::APB1_B && a < f3::APB1_FIN) n = a_apb1_->transport_dbg(gp);
        else if (a >= f3::APB2_B && a < f3::APB2_FIN) n = a_apb2_->transport_dbg(gp);
        else if (a >= f3::AHB1_B && a < f3::AHB1_FIN) n = a_ahb1_->transport_dbg(gp);
        else if (a >= f3::AHB2_B && a < f3::AHB2_FIN) n = a_ahb2_->transport_dbg(gp);
        else if (a >= f3::AHB3_B && a < f3::AHB3_FIN) n = a_ahb3_->transport_dbg(gp);
        gp.set_address(a0);
        return n;
    }
    // La CCM: lo que protege SYSCFG_RCR no se escribe, y el intento es un error
    // de bus [1F: RM0316 §12.1.2]
    void bt_ccm(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t off = uint32_t(gp.get_address() - f3::CCM_B);
        if (gp.is_write() && syscfg.ccm_protegida(off)) {
            ++n_ccm_prot_;
            if (!avisado_ccm_) {
                avisado_ccm_ = true;
                SC_REPORT_WARNING("stm32f303", "escritura en una pagina de la CCM protegida "
                                  "por SYSCFG_RCR: error de bus (la proteccion solo se quita "
                                  "con un reset)");
            }
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        a_ccm_->b_transport(gp, t);
    }
    unsigned dbg_ccm(tlm::tlm_generic_payload& gp) { return a_ccm_->transport_dbg(gp); }
};

} // namespace stm32
#endif // STM32_SOC_STM32F3_STM32F303_H
