// =============================================================================
// msp432p401r.h — EL MSP432P401R (fase T1)
//
// Un Cortex-M4F de Texas Instruments: el MISMO núcleo que el STM32F407 -la
// misma clase, `CortexM4F`, sin tocar una línea- con lo que TI puso alrededor.
// Es la prueba de que T0 sirvió: el núcleo se construye con
//
//   CoreCaps           41 IRQ, 3 bits de prioridad, MPU, FPU      [p4_desc.h]
//   MapaRam            sin CCM
//   IntegracionNucleo  SIN alias en 0x0 (la Flash está ahí) y con la ventana
//                      0xE004_2000-0xE004_5000 del PPB atendida por el SoC:
//                      RSTCTL, SYSCTL y SYSCTL_Boot
//
// y el resto es de TI [doc/analisis_msp432.md, §2 y §8 T1]:
//
//   memoria     Flash principal (256 KB, 0x0) e información (16 KB, 0x20_0000,
//               con el TLV), SRAM de 64 KB en 0x2000_0000 y su alias de
//               código en 0x0100_0000; la ROM de DriverLib, AUSENTE en T1 (R3)
//   reloj       CS: DCO, REFO, VLO, MODOSC, SYSOSC y los dos cristales
//   energía     PCM: los seis modos activos y sus transiciones
//   Flash       FLCTL: estados de espera y búferes (programar: T2)
//   reset       POR, reboot, duro y blando; RSTn; SYSRESETREQ; el perro
//   perro       WDT_A, que arranca encendido
//   pines       P1..P10 y PJ sobre pads eléctricos, con interrupciones en
//               P1..P6, y el PMAP
//   depuración  SWCLKTCK y SWDIOTMS son patillas DEDICADAS: la sonda SWD de
//               los STM32 se cuelga de ellas sin cambios
//
// Y los que todavía no están, DECLARADOS: contestan, no guardan y avisan una
// vez (Timer_A, eUSCI, ADC14, DMA, Timer32...). `limitaciones()` los enumera.
//
// EL VIGILANTE DE TOPES. Lo que el silicio no comprueba y el modelo dice en voz
// alta: MCLK por encima de lo que permite el VCORE, o con menos estados de
// espera de los que pide la Flash a esa frecuencia [SA, tabla 2]. En la placa
// eso no da un error: da lecturas de Flash equivocadas. Aquí da un aviso con
// nombre y apellidos, igual que el RCC de los STM32 con sus topes.
// =============================================================================
#ifndef STM32_SOC_MSP432P4_MSP432P401R_H
#define STM32_SOC_MSP432P4_MSP432P401R_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <string>
#include <vector>
#include "p4_desc.h"
#include "../../core/cortex_m4f.h"
#include "../../common/clock_gen.h"
#include "../../bus/ahb_decoder.h"
#include "../../mem/sram.h"
#include "../../periph/bloque_declarado.h"
#include "../../periph/msp432p4/flctl.h"
#include "../../periph/msp432p4/cs.h"
#include "../../periph/msp432p4/pcm.h"
#include "../../periph/msp432p4/wdt_a.h"
#include "../../periph/msp432p4/sysctl.h"
#include "../../periph/msp432p4/dio.h"
#include "../../common/gdb_rsp.h"

namespace stm32 {

// La integración del núcleo en el MSP432P4 [T0]
inline constexpr IntegracionNucleo INTEGRACION_MSP432P4{
    false, p4::RSTCTL_B, p4::PPB_FAB_FIN
};
// Sin CCM: un mapa de RAM con la CCM a cero. La SRAM la gestiona el SoC.
inline constexpr MapaRam RAM_MSP432P4{
    p4::SRAM_B, 0x00010000u, 0, 0, 0, 0, 0, 0
};

// -----------------------------------------------------------------------------
// La ventana de la ROM, AUSENTE en T1 (opción R3 de §6.1): cualquier acceso es
// un error de bus, y la primera vez se dice por qué. La ROM sustituta
// construida con DriverLib llega en T4.
// -----------------------------------------------------------------------------
class RomAusenteP4 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<RomAusenteP4> tsk{"tsk"};
    explicit RomAusenteP4(sc_core::sc_module_name nm) : sc_core::sc_module(nm) {
        tsk.register_b_transport(this, &RomAusenteP4::bt);
    }
    unsigned accesos() const { return n_; }
private:
    unsigned n_ = 0;
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        if (n_++ == 0)
            SC_REPORT_WARNING("msp432p4/rom",
                "acceso a la ROM de DriverLib (0x0200_0000), que este modelo NO "
                "lleva todavia (T4). Un firmware que usa MAP_/ROM_ salta aqui: "
                "compilalo sin ROM o espera a la ROM sustituta");
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// Un destino que no debería recibir nunca nada: la CCM que el MSP432 no tiene.
class SinDestinoP4 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<SinDestinoP4> tsk{"tsk"};
    explicit SinDestinoP4(sc_core::sc_module_name nm) : sc_core::sc_module(nm) {
        tsk.register_b_transport(this, &SinDestinoP4::bt);
    }
private:
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// El programador de la Flash para el `load` de GDB, que llega en T2: de
// momento reconoce la Flash y se niega, diciéndolo.
class ProgramadorP4Pendiente : public programador_nv_if {
public:
    static ProgramadorP4Pendiente& el() { static ProgramadorP4Pendiente p; return p; }
    const char* nombre() const override { return "MSP432P4 (pendiente: T2)"; }
    int sector_de(uint32_t a) const override {
        return (a < 0x00040000u) ? int(a / 0x1000u) : -1;
    }
    uint32_t tam_sector(int) const override { return 0x1000u; }
    unsigned n_sectores() const override { return 64; }
    bool borrar(acceso_nv_if&, uint32_t, uint32_t) override { return avisa(); }
    bool programar(acceso_nv_if&, uint32_t, const uint8_t*, unsigned,
                   unsigned&) override { return avisa(); }
    void terminar(acceso_nv_if&) override {}
private:
    bool avisado_ = false;
    bool avisa() {
        if (!avisado_) {
            avisado_ = true;
            SC_REPORT_WARNING("msp432p4/gdb",
                "el stub de GDB todavia no sabe programar la Flash del MSP432P4 "
                "(T2): carga el firmware al arrancar `mcu-sim placa.xml fw.out`");
        }
        return false;
    }
};

class Msp432P4 : public sc_core::sc_module, public ppb_fabricante_if {
public:
    const McuCapsP4 mcu;

    // --- El núcleo -----------------------------------------------------------
    CortexM4F core;

    // --- Memoria -------------------------------------------------------------
    FlctlP4      flctl{"flctl"};
    FlashP4      flash;
    Sram         sram;
    RomAusenteP4 rom{"rom"};
    SinDestinoP4 sin_ccm{"sin_ccm"};

    // --- Sistema ---------------------------------------------------------------
    CsP4     cs{"cs"};
    PcmP4    pcm{"pcm"};
    WdtAP4   wdt{"wdt"};
    SysctlP4 sysctl;
    DioP4    dio;
    PmapP4   pmap{"pmap"};

    // --- Los relojes que reparte el CS ----------------------------------------
    ClockGen g_mclk{"g_mclk"}, g_hsmclk{"g_hsmclk"}, g_smclk{"g_smclk"},
             g_aclk{"g_aclk"};

    // --- El bus de periféricos y lo declarado -----------------------------------
    AhbDecoder perif{"perif"};
    std::vector<BloqueDeclarado*> declarados;

    // --- Las patillas que no son de puerto -------------------------------------
    AnalogNet* dvcc; AnalogNet* dvss; AnalogNet* avcc; AnalogNet* avss;
    AnalogNet* vcore_pin; AnalogNet* rstn;
    AnalogNet* swclk_net; AnalogNet* swdio_net;
    Pad pad_swclk{"pad_swclk"}, pad_swdio{"pad_swdio"};

    // Un maestro de pruebas puede entrar al bus como el S-bus. Lo usa el banco.
    tlm_utils::simple_target_socket_optional<Msp432P4> from_tb{"from_tb"};

    Msp432P4(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
             const Cableado& cab = Cableado(),
             const McuCapsP4& caps = MCU_MSP432P401R)
        : sc_core::sc_module(nm), mcu(caps),
          core("core", dbg, caps.nucleo, RAM_MSP432P4, INTEGRACION_MSP432P4),
          flash("flash", caps.flash_size, caps.device_id, caps.hwrev),
          sram("sram", p4::SRAM_B, caps.sram_size),
          sysctl(caps.sram_size, caps.flash_size),
          dio("dio", caps.enc, cab),
          s_irq_("s_irq", p4::N_IRQ) {
        SC_HAS_PROCESS(Msp432P4);
        crea_patillas(cab);
        conecta_nucleo();
        conecta_bus();
        conecta_sistema();
        SC_THREAD(reset_proc);
        SC_METHOD(sysresetreq_proc); sensitive << s_sysresetreq_; dont_initialize();
        SC_METHOD(swdio_proc); sensitive << s_swdio_o_ << s_swdio_oe_;
        SC_METHOD(halt_proc); sensitive << core.debug.halted; dont_initialize();
    }
    ~Msp432P4() override {
        for (BloqueDeclarado* b : declarados) delete b;
        for (AnalogNet* n : propias_) delete n;
    }

    // --- ppb_fabricante_if: RSTCTL, SYSCTL y SYSCTL_Boot -----------------------
    void b_transport_ppb(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        sysctl.b_transport(gp, t);
    }

    // --- Lo que mira el banco y el adaptador -----------------------------------
    double f_mclk() const   { return cs.f_mclk(); }
    unsigned avisos_vcore() const { return n_av_vcore_; }
    unsigned avisos_flash() const { return n_av_flash_; }
    unsigned resets() const { return n_resets_; }
    bool en_reset() const { return !s_rst_core_.read(); }
    void set_ondas(bool on) {
        g_mclk.set_waveform(on); g_hsmclk.set_waveform(on);
        g_smclk.set_waveform(on); g_aclk.set_waveform(on);
        cs.lfxt.set_waveform(on); cs.hfxt.set_waveform(on);
    }
    // El firmware: a la Flash principal o de información, o a la SRAM.
    bool poke8(uint32_t a, uint8_t v) {
        if (flash.poke_byte(a, v)) return true;
        if (a >= p4::SRAM_B && a < p4::SRAM_B + sram.size()) {
            uint8_t b = v; return sram.load(&b, 1, a - p4::SRAM_B);
        }
        return false;
    }
    std::vector<std::string> limitaciones() const {
        return {
            "MSP432P4 (fase T1): la Flash se lee, pero programarla y borrarla -por "
            "el FLCTL o con `load` de GDB- llega en T2",
            "la ROM de DriverLib (0x0200_0000) no esta: un firmware con MAP_/ROM_ "
            "da error de bus al llamarla (T4)",
            "el TLV es SINTETICO: identidad P401R rev. C y calibraciones tipicas, "
            "no las de una pieza concreta",
            "Timer_A, eUSCI, ADC14, COMP_E, REF_A, RTC_C, AES256, CRC32, DMA, "
            "Timer32 y CAPTIO estan DECLARADOS y no modelados (T3)",
            "los modos de bajo consumo LPM3/LPM4/LPMx.5 no se modelan: un WFI "
            "duerme el nucleo con los relojes en marcha (LPM0)",
            "el SysTick con CLKSOURCE = 0 cuenta MCLK/8, como en los STM32; la "
            "referencia externa del P4 no esta contrastada",
            "el codigo de arranque de TI no se ejecuta: el nucleo sale del reset "
            "directamente al vector de 0x0 (sin buzon, sin BSL, sin bloqueo JTAG)",
        };
    }

private:
    sc_core::sc_signal<bool>    s_mclk_{"s_mclk"}, s_hsmclk_{"s_hsmclk"},
                                s_smclk_{"s_smclk"}, s_aclk_{"s_aclk"};
    sc_core::sc_signal<double>  s_mclk_hz_{"s_mclk_hz"}, s_hsmclk_hz_{"s_hsmclk_hz"},
                                s_smclk_hz_{"s_smclk_hz"}, s_aclk_hz_{"s_aclk_hz"};
    sc_core::sc_signal<bool>    s_rst_core_{"s_rst_core"}, s_rst_perif_{"s_rst_perif"};
    sc_core::sc_signal<bool>    s_uno_{"s_uno"}, s_cero_{"s_cero"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_irq_;
    sc_core::sc_signal<bool>    s_nmi_{"s_nmi"}, s_sysresetreq_{"s_sysresetreq"},
                                s_sleeping_{"s_sleeping"}, s_sleepdeep_{"s_sleepdeep"},
                                s_evt_in_{"s_evt_in"}, s_evt_out_{"s_evt_out"},
                                s_stk_ext_{"s_stk_ext"};
    sc_core::sc_signal<uint8_t> s_boot_{"s_boot"}, s_dbg_lp_{"s_dbg_lp"};
    sc_core::sc_signal<bool>    s_swclk_{"s_swclk"}, s_swdio_i_{"s_swdio_i"},
                                s_swdio_o_{"s_swdio_o"}, s_swdio_oe_{"s_swdio_oe"},
                                s_jtdi_{"s_jtdi"}, s_jtdo_{"s_jtdo"}, s_njtrst_{"s_njtrst"};
    sc_core::sc_signal<bool>    s_freeze_[FZ_COUNT];
    sc_core::sc_signal<PadDrive> s_drv_swclk_{"s_drv_swclk"}, s_drv_swdio_{"s_drv_swdio"};
    sc_core::sc_signal<bool>    s_swclk_ok_{"s_swclk_ok"}, s_swclk_oor_{"s_swclk_oor"},
                                s_swdio_ok_{"s_swdio_ok"}, s_swdio_oor_{"s_swdio_oor"};

    tlm_utils::simple_target_socket<Msp432P4> t_i_{"t_i"}, t_d_{"t_d"}, t_s_{"t_s"};
    tlm_utils::simple_initiator_socket<Msp432P4> a_flash_{"a_flash"}, a_sram_{"a_sram"},
                                                 a_rom_{"a_rom"}, a_perif_{"a_perif"};
    std::vector<AnalogNet*> propias_;
    int d_rstn_pull_ = -1;
    sc_core::sc_event ev_reset_;
    ResetP4 pedido_ = ResetP4::Duro;
    int      src_pedido_ = -1;
    bool     hay_pedido_ = false;
    unsigned n_resets_ = 0, n_av_vcore_ = 0, n_av_flash_ = 0;
    bool     mal_vcore_ = false, mal_flash_ = false;

    // --- Construcción -------------------------------------------------------
    AnalogNet* patilla(const Cableado& cab, const char* nombre, const char* sc) {
        if (analog_net_if* n = cab.busca_alim(nombre))
            if (AnalogNet* a = dynamic_cast<AnalogNet*>(n)) return a;
        AnalogNet* a = new AnalogNet(sc);
        a->nombre_esq = nombre;
        propias_.push_back(a);
        return a;
    }
    void crea_patillas(const Cableado& cab) {
        dvcc = patilla(cab, "DVCC", "net_DVCC");
        dvss = patilla(cab, "DVSS", "net_DVSS");
        avcc = patilla(cab, "AVCC", "net_AVCC");
        avss = patilla(cab, "AVSS", "net_AVSS");
        vcore_pin = patilla(cab, "VCORE", "net_VCORE");
        rstn = patilla(cab, "RSTN", "net_RSTN");
        swclk_net = patilla(cab, "SWCLKTCK", "net_SWCLKTCK");
        swdio_net = patilla(cab, "SWDIOTMS", "net_SWDIOTMS");
        // RSTn lleva su pull-up dentro: sin nada fuera, el chip no está en
        // reset. [1F: datasheet, RSTn/NMI; P-16 l]
        d_rstn_pull_ = rstn->register_driver("pullup_rstn");
        rstn->set_drive(d_rstn_pull_, 3.3f, 47e3f);
        // Las dos patillas de SWD, como pads de entrada (y SWDIO también de
        // salida, cuando el DAP contesta).
        for (Pad* q : {&pad_swclk, &pad_swdio}) { q->vdd = 3.3; q->ft_5v = false; }
        pad_swclk.net(*swclk_net);   pad_swdio.net(*swdio_net);
        pad_swclk.drive(s_drv_swclk_); pad_swdio.drive(s_drv_swdio_);
        pad_swclk.din(s_swclk_);     pad_swdio.din(s_swdio_i_);
        pad_swclk.din_valid(s_swclk_ok_); pad_swdio.din_valid(s_swdio_ok_);
        pad_swclk.out_of_range(s_swclk_oor_); pad_swdio.out_of_range(s_swdio_oor_);
    }

    void conecta_nucleo() {
        core.icode.bind(t_i_); core.dcode.bind(t_d_); core.sbus.bind(t_s_);
        core.ccm.bind(sin_ccm.tsk);
        core.fclk(s_mclk_); core.fclk_hz(s_mclk_hz_);
        core.systick_ext(s_stk_ext_); core.rst_n(s_rst_core_);
        for (unsigned i = 0; i < p4::N_IRQ; ++i) core.irq_in[i](s_irq_[i]);
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
        core.ppb_fab = this;
        s_njtrst_.write(true);
        // El `load` de GDB todavía no sabe programar esta Flash (T2): que lo
        // diga, en vez de escribir las llaves del F407 donde no hay nada.
        if (core.gdb) core.gdb->pon_programador(&ProgramadorP4Pendiente::el());
    }

    void conecta_bus() {
        t_i_.register_b_transport(this, &Msp432P4::bt_i);
        t_d_.register_b_transport(this, &Msp432P4::bt_d);
        t_s_.register_b_transport(this, &Msp432P4::bt_d);
        from_tb.register_b_transport(this, &Msp432P4::bt_d);
        for (auto* s : {&t_i_, &t_d_, &t_s_})
            s->register_transport_dbg(this, &Msp432P4::dbg);
        from_tb.register_transport_dbg(this, &Msp432P4::dbg);
        a_flash_.bind(flash.tsk);
        a_sram_.bind(sram.tsk);
        a_rom_.bind(rom.tsk);
        a_perif_.bind(perif.tsk);
        flash.flctl = &flctl;
        flash.mclk_hz(s_mclk_hz_);
        sram.hclk(s_mclk_); sram.rst_n(s_rst_perif_);
        // Los periféricos
        auto engancha = [&](BusSlave& b) {
            perif.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())
                ->bind(b.tsk);
            b.clk(s_mclk_); b.clk_hz(s_mclk_hz_); b.rst_n(s_rst_perif_);
            b.clk_en(s_uno_);
        };
        engancha(flctl); engancha(cs); engancha(pcm); engancha(wdt);
        engancha(dio);   engancha(pmap);
        struct D { const char* n; uint32_t b, s; const char* q; };
        static const D lista[] = {
            {"timer_a0", p4::TIMER_A0_B, 0x400, "Timer_A0"},
            {"timer_a1", p4::TIMER_A1_B, 0x400, "Timer_A1"},
            {"timer_a2", p4::TIMER_A2_B, 0x400, "Timer_A2"},
            {"timer_a3", p4::TIMER_A3_B, 0x400, "Timer_A3"},
            {"eusci_a0", p4::EUSCI_A0_B, 0x400, "eUSCI_A0"},
            {"eusci_a1", p4::EUSCI_A1_B, 0x400, "eUSCI_A1"},
            {"eusci_a2", p4::EUSCI_A2_B, 0x400, "eUSCI_A2"},
            {"eusci_a3", p4::EUSCI_A3_B, 0x400, "eUSCI_A3"},
            {"eusci_b0", p4::EUSCI_B0_B, 0x400, "eUSCI_B0"},
            {"eusci_b1", p4::EUSCI_B1_B, 0x400, "eUSCI_B1"},
            {"eusci_b2", p4::EUSCI_B2_B, 0x400, "eUSCI_B2"},
            {"eusci_b3", p4::EUSCI_B3_B, 0x400, "eUSCI_B3"},
            {"ref_a",    p4::REF_A_B,    0x400, "REF_A"},
            {"comp_e0",  p4::COMP_E0_B,  0x400, "COMP_E0"},
            {"comp_e1",  p4::COMP_E1_B,  0x400, "COMP_E1"},
            {"aes256",   p4::AES256_B,   0x400, "AES256"},
            {"crc32",    p4::CRC32_B,    0x400, "CRC32"},
            {"rtc_c",    p4::RTC_C_B,    0x400, "RTC_C"},
            {"captio0",  p4::CAPTIO0_B,  0x400, "CAPTIO0"},
            {"captio1",  p4::CAPTIO1_B,  0x400, "CAPTIO1"},
            {"timer32",  p4::TIMER32_B,  0x400, "Timer32"},
            {"dma",      p4::DMA_B,      0x1000, "uDMA"},
            {"pss",      p4::PSS_B,      0x400, "PSS"},
            {"adc14",    p4::ADC14_B,    0x400, "ADC14"},
        };
        for (const D& d : lista) {
            BloqueDeclarado* b = new BloqueDeclarado(d.n, d.b, d.s, d.q,
                "Llega en la fase T3 del plan de doc/analisis_msp432.md.");
            engancha(*b);
            declarados.push_back(b);
        }
        // Los relojes
        g_mclk.clk(s_mclk_);     g_mclk.freq_hz(s_mclk_hz_);
        g_hsmclk.clk(s_hsmclk_); g_hsmclk.freq_hz(s_hsmclk_hz_);
        g_smclk.clk(s_smclk_);   g_smclk.freq_hz(s_smclk_hz_);
        g_aclk.clk(s_aclk_);     g_aclk.freq_hz(s_aclk_hz_);
        set_ondas(false);
        s_uno_.write(true);
    }

    void conecta_sistema() {
        cs.lfxt.xtal_in = dio.nodo[p4::PUERTO_J][0];       // LFXIN  = PJ.0
        cs.hfxt.xtal_in = dio.nodo[p4::PUERTO_J][3];       // HFXIN  = PJ.3
        // La calibración del DCO, del TLV que lleva la Flash
        cs.fcal_ir04 = flash.peek32(p4::TLV_B + 19 * 4);
        cs.fcal_ir5  = flash.peek32(p4::TLV_B + 20 * 4);
        cs.k_ir04 = flota(flash.peek32(p4::TLV_B + 25 * 4));
        cs.k_ir5  = flota(flash.peek32(p4::TLV_B + 26 * 4));
        cs.fcal_er04 = flash.peek32(p4::TLV_B + 27 * 4);
        cs.fcal_er5  = flash.peek32(p4::TLV_B + 28 * 4);
        cs.k_er04 = flota(flash.peek32(p4::TLV_B + 33 * 4));
        cs.k_er5  = flota(flash.peek32(p4::TLV_B + 34 * 4));
        cs.al_cambiar = [this] { relojes(); };
        pcm.f_mclk = [this] { return cs.f_mclk(); };
        pcm.al_cambiar = [this] { revisa_topes(); };
        flctl.al_cambiar = [this] { revisa_topes(); };
        dio.al_cambiar_cristales = [this](bool lf, bool hf) { cs.pines_cristal(lf, hf); };
        for (unsigned i = 0; i < DioP4::N_INT; ++i)
            dio.irq[i](s_irq_[p4::IRQ_PORT1 + i]);
        wdt.irq(s_irq_[p4::IRQ_WDT_A]);
        wdt.f_fuente = [this](unsigned s) {
            switch (s) {
                case 0:  return cs.f_smclk();
                case 1:  return cs.f_aclk();
                case 2:  return cs.f_vlo();
                default: return cs.f_bclk();
            }
        };
        wdt.congelado = [this] { return sysctl.halt_wdt() && core.debug.is_halted(); };
        wdt.pide_reset = [this](bool viol) {
            const bool duro = sysctl.wdt_duro(viol);
            pide(duro ? ResetP4::Duro : ResetP4::Blando,
                 int(viol ? SysctlP4::SRC_WDT_VIOLA : SysctlP4::SRC_WDT_VENCE));
        };
        sysctl.pide_reset = [this](ResetP4 r) { pide(r, -1); };
    }
    static float flota(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

    // --- El bus ---------------------------------------------------------------
    void enruta(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (a < flash.main_size() || flash.es_info(uint32_t(a))) {
            a_flash_->b_transport(gp, t); return;
        }
        if (a >= p4::SRAM_CODE_B && a < p4::SRAM_CODE_B + sram.size()) {
            // El alias de la SRAM en la región de código: la misma memoria
            gp.set_address(a - p4::SRAM_CODE_B + p4::SRAM_B);
            a_sram_->b_transport(gp, t);
            gp.set_address(a);
            return;
        }
        if (a >= p4::ROM_B && a < p4::ROM_B + p4::ROM_SIZE) { a_rom_->b_transport(gp, t); return; }
        if (a >= p4::SRAM_B && a < p4::SRAM_B + sram.size()) { a_sram_->b_transport(gp, t); return; }
        if (a >= p4::PERIPH_B && a < p4::PERIPH_B + p4::PERIPH_SIZE) {
            a_perif_->b_transport(gp, t); return;
        }
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    void bt_i(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) { enruta(gp, t); }
    void bt_d(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) { enruta(gp, t); }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint64_t a = gp.get_address();
        if (a < flash.main_size() || flash.es_info(uint32_t(a))) return a_flash_->transport_dbg(gp);
        if (a >= p4::SRAM_CODE_B && a < p4::SRAM_CODE_B + sram.size()) {
            gp.set_address(a - p4::SRAM_CODE_B + p4::SRAM_B);
            const unsigned n = a_sram_->transport_dbg(gp);
            gp.set_address(a);
            return n;
        }
        if (a >= p4::SRAM_B && a < p4::SRAM_B + sram.size()) return a_sram_->transport_dbg(gp);
        if (a >= p4::PERIPH_B && a < p4::PERIPH_B + p4::PERIPH_SIZE) return a_perif_->transport_dbg(gp);
        if (a >= p4::RSTCTL_B && a < p4::PPB_FAB_FIN) return sysctl.transport_dbg(gp);
        return 0;
    }

    // --- Relojes y topes --------------------------------------------------------
    void relojes() {
        g_mclk.set_freq(cs.f_mclk());
        g_hsmclk.set_freq(cs.f_hsmclk());
        g_smclk.set_freq(cs.f_smclk());
        g_aclk.set_freq(cs.f_aclk());
        wdt.recalcula();
        revisa_topes();
    }
    void revisa_topes() {
        const double f = cs.f_mclk();
        const unsigned v = pcm.vcore();
        const bool mal_v = f > p4::tope_mclk(v) * (1.0 + 1e-9);
        const bool mal_f = f > p4::tope_flash(v, flctl.wait_banco(0)) * (1.0 + 1e-9) ||
                           f > p4::tope_flash(v, flctl.wait_banco(1)) * (1.0 + 1e-9);
        char m[200];
        if (mal_v && !mal_vcore_) {
            ++n_av_vcore_;
            std::snprintf(m, sizeof m, "MCLK = %.2f MHz con VCORE%u: el maximo es "
                          "%.0f MHz. Sube el VCORE (PCM) ANTES que el reloj "
                          "[SLAA668A, tabla 2]", f / 1e6, v, p4::tope_mclk(v) / 1e6);
            SC_REPORT_WARNING("msp432p4", m);
        }
        if (mal_f && !mal_flash_) {
            ++n_av_flash_;
            std::snprintf(m, sizeof m, "MCLK = %.2f MHz con VCORE%u y %u/%u estados "
                          "de espera en la Flash: no bastan, y en el silicio se leen "
                          "datos equivocados [SLAA668A, tabla 2]", f / 1e6, v,
                          flctl.wait_banco(0), flctl.wait_banco(1));
            SC_REPORT_WARNING("msp432p4", m);
        }
        mal_vcore_ = mal_v;
        mal_flash_ = mal_f;
    }

    // --- Los resets -------------------------------------------------------------
    void pide(ResetP4 r, int src) {
        if (hay_pedido_ && int(r) >= int(pedido_)) return;   // ya hay uno más hondo
        pedido_ = r; src_pedido_ = src; hay_pedido_ = true;
        ev_reset_.notify(sc_core::SC_ZERO_TIME);
    }
    void sysresetreq_proc() {
        if (s_sysresetreq_.read()) pide(ResetP4::Duro, int(SysctlP4::SRC_SYSRESETREQ));
    }
    void halt_proc() { wdt.recalcula(); }
    void swdio_proc() {
        PadDrive d;
        d.oe = s_swdio_oe_.read();
        d.out = s_swdio_o_.read();
        d.pupd = 1;                  // SWDIO con pull-up interno, como en ARM
        s_drv_swdio_.write(d);
        PadDrive c;
        c.pupd = 2;                  // SWCLK con pull-down
        s_drv_swclk_.write(c);
    }
    bool alimentado() const { return dvcc->voltage() - dvss->voltage() > 1.62f; }
    bool pin_en_reset() const { return rstn->voltage() - dvss->voltage() < 0.3f * 3.3f; }

    // El controlador de reset. Mientras no haya alimentación o RSTn esté abajo,
    // todo en reset; al soltar, un POR (o un reset de pin). Las peticiones de
    // software, del núcleo o del perro dan un pulso del tipo que toque.
    void reset_proc() {
        // EL PULSO DURA 20 us, y no es un número cualquiera: es el mínimo del
        // RCC de los STM32 y por la misma razón. La CPU avanza por cuantos de
        // 1 us con su tiempo adelantado; un reset que pide ella misma -por
        // RSTCTL o por una violación del perro- empezaría y acabaría dentro de
        // su cuanto sin que lo viera. Con 1 us pasaba exactamente eso.
        // [modelo; la duración del silicio no está contrastada: P-16 l]
        const sc_core::sc_time PULSO(20, sc_core::SC_US);
        s_rst_core_.write(false); s_rst_perif_.write(false);
        bool estaba = false;                 // ¿estaba en marcha?
        bool por = true;
        for (;;) {
            const bool ok = alimentado() && !pin_en_reset();
            if (!ok) {
                if (estaba) {
                    s_rst_core_.write(false); s_rst_perif_.write(false);
                    estaba = false;
                }
                if (!alimentado()) por = true;
                wait(dvcc->value_changed_event() | rstn->value_changed_event() |
                     dvss->value_changed_event());
                continue;
            }
            if (!estaba) {
                // Sale del reset: por alimentación o por la patilla
                if (por) { sysctl.por(); por = false; }
                else     { sysctl.reboot(); sysctl.anota_pin(); }
                hay_pedido_ = false;
                ++n_resets_;
                wait(PULSO);
                if (!(alimentado() && !pin_en_reset())) continue;
                s_rst_perif_.write(true); s_rst_core_.write(true);
                estaba = true;
                continue;
            }
            wait(dvcc->value_changed_event() | rstn->value_changed_event() |
                 dvss->value_changed_event() | ev_reset_);
            if (!hay_pedido_) continue;
            const ResetP4 r = pedido_;
            const int src = src_pedido_;
            hay_pedido_ = false;
            ++n_resets_;
            switch (r) {
                case ResetP4::Por:
                case ResetP4::Reboot:
                    sysctl.reboot(); sysctl.anota_reboot();
                    s_rst_core_.write(false); s_rst_perif_.write(false);
                    break;
                case ResetP4::Duro:
                    sysctl.anota_duro(src);
                    s_rst_core_.write(false); s_rst_perif_.write(false);
                    break;
                case ResetP4::Blando:
                    sysctl.anota_blando(src);
                    s_rst_core_.write(false);
                    break;
            }
            wait(PULSO);
            s_rst_perif_.write(true); s_rst_core_.write(true);
        }
    }

};

} // namespace stm32
#endif // STM32_SOC_MSP432P4_MSP432P401R_H
