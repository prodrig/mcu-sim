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
#include "../../periph/msp432p4/eusci.h"
#include "../../periph/msp432p4/timer_a.h"
#include "../../periph/msp432p4/timer32.h"
#include "../../periph/msp432p4/ref_a.h"
#include "../../periph/msp432p4/adc14.h"
#include "p4_programador.h"

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

    // --- Comunicaciones (T3) ---------------------------------------------------
    EusciP4  uca0{"eusci_a0", p4::EUSCI_A0_B, false}, uca1{"eusci_a1", p4::EUSCI_A1_B, false},
             uca2{"eusci_a2", p4::EUSCI_A2_B, false}, uca3{"eusci_a3", p4::EUSCI_A3_B, false};
    EusciP4  ucb0{"eusci_b0", p4::EUSCI_B0_B, true},  ucb1{"eusci_b1", p4::EUSCI_B1_B, true},
             ucb2{"eusci_b2", p4::EUSCI_B2_B, true},  ucb3{"eusci_b3", p4::EUSCI_B3_B, true};
    EusciP4* uca(unsigned n) { EusciP4* a[4] = {&uca0, &uca1, &uca2, &uca3}; return a[n & 3u]; }
    EusciP4* ucb(unsigned n) { EusciP4* b[4] = {&ucb0, &ucb1, &ucb2, &ucb3}; return b[n & 3u]; }

    // --- Temporizadores (T3) ---------------------------------------------------
    TimerAP4  ta0{"timer_a0", p4::TIMER_A0_B}, ta1{"timer_a1", p4::TIMER_A1_B},
              ta2{"timer_a2", p4::TIMER_A2_B}, ta3{"timer_a3", p4::TIMER_A3_B};
    Timer32P4 t32{"timer32"};
    TimerAP4* ta(unsigned n) { TimerAP4* a[4] = {&ta0, &ta1, &ta2, &ta3}; return a[n & 3u]; }

    // --- Analógicos (T3) ----------------------------------------------------------
    RefAP4   ref{"ref_a"};
    Adc14P4  adc{"adc14"};

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
        // Los disparos del ADC14: las salidas OUT1/OUT2 de los Timer_A
        SC_METHOD(disparos_adc_proc);
        sensitive << ta0.out[1] << ta0.out[2] << ta1.out[1] << ta1.out[2]
                  << ta2.out[1] << ta2.out[2] << ta3.out[1];
        dont_initialize();
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
            "la Flash se programa y se borra por el FLCTL y con `load` de GDB (T2), "
            "pero con tiempos de modelo, sin verificacion previa (VER_PRE, AUTO_PRE) "
            "y sin modos de lectura de margen",
            "la ROM de DriverLib (0x0200_0000) no esta: un firmware con MAP_/ROM_ "
            "da error de bus al llamarla (T4)",
            "el TLV es SINTETICO: identidad P401R rev. C y calibraciones tipicas, "
            "no las de una pieza concreta",
            "COMP_E, RTC_C, AES256, CRC32, DMA y CAPTIO estan DECLARADOS y no "
            "modelados (T3)",
            "el ADC14 es un convertidor ideal (sin ruido, sin error de ganancia ni "
            "de offset, sin carga del muestreo sobre el nodo); el sensor de "
            "temperatura es lineal (685 mV a 0 C, 1,9 mV/C) y el TLV lleva SUS "
            "calibraciones",
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
        // Y el `load` de GDB programa esta Flash con su FLCTL (T2).
        if (core.gdb) core.gdb->pon_programador(&ProgramadorP4::el());
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
        flctl.mem = &flash;
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
        for (unsigned n = 0; n < 4; ++n) { engancha(*uca(n)); engancha(*ucb(n)); engancha(*ta(n)); }
        engancha(t32);
        engancha(ref); engancha(adc);
        struct D { const char* n; uint32_t b, s; const char* q; };
        static const D lista[] = {
            {"comp_e0",  p4::COMP_E0_B,  0x400, "COMP_E0"},
            {"comp_e1",  p4::COMP_E1_B,  0x400, "COMP_E1"},
            {"aes256",   p4::AES256_B,   0x400, "AES256"},
            {"crc32",    p4::CRC32_B,    0x400, "CRC32"},
            {"rtc_c",    p4::RTC_C_B,    0x400, "RTC_C"},
            {"captio0",  p4::CAPTIO0_B,  0x400, "CAPTIO0"},
            {"captio1",  p4::CAPTIO1_B,  0x400, "CAPTIO1"},
            {"dma",      p4::DMA_B,      0x1000, "uDMA"},
            {"pss",      p4::PSS_B,      0x400, "PSS"},
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
        dio.pmap = &pmap;
        pmap.al_cambiar = [this] { dio.mapa_cambiado(); };
        for (unsigned i = 0; i < DioP4::N_INT; ++i)
            dio.irq[i](s_irq_[p4::IRQ_PORT1 + i]);
        wdt.irq(s_irq_[p4::IRQ_WDT_A]);
        flctl.irq(s_irq_[p4::IRQ_FLCTL]);
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
        sysctl.al_cambiar_perihalt = [this] { wdt.recalcula(); };
        conecta_comunicaciones();
        conecta_temporizadores();
        conecta_analogicos();
    }

    // Los canales del ADC14 [2F: ADCMSP432.h, ADCMSP432_Px_y_An; función
    // terciaria] y las referencias externas VeREF+ (P5.6) y VeREF- (P5.7)
    static constexpr uint8_t CANAL_ADC[24][2] = {
        {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0},                 // A0-A5:   P5.5-P5.0
        {3, 7}, {3, 6}, {3, 5}, {3, 4}, {3, 3}, {3, 2}, {3, 1}, {3, 0}, // A6-A13:  P4.7-P4.0
        {5, 1}, {5, 0},                                                 // A14-A15: P6.1, P6.0
        {8, 1}, {8, 0},                                                 // A16-A17: P9.1, P9.0
        {7, 7}, {7, 6}, {7, 5}, {7, 4}, {7, 3}, {7, 2},                 // A18-A23: P8.7-P8.2
    };
    void conecta_analogicos() {
        for (unsigned c = 0; c < 24; ++c) {
            FuncionP4 f; f.analogica = true; f.nombre = "A";
            dio.conecta_funcion(CANAL_ADC[c][0], CANAL_ADC[c][1], 3, f);
        }
        for (unsigned i = 6; i < 8; ++i) {
            FuncionP4 f; f.analogica = true; f.nombre = "VREF";
            dio.conecta_funcion(4, i, 3, f);
        }
        adc.irq(s_irq_[p4::IRQ_ADC14]);
        adc.ref = &ref;
        adc.v_avcc = [this] { return double(avcc->voltage()); };
        adc.v_avss = [this] { return double(avss->voltage()); };
        adc.v_canal = [this](unsigned c) {
            return c < 24 ? double(dio.nodo_de(CANAL_ADC[c][0], CANAL_ADC[c][1]).voltage()) : 0.0;
        };
        adc.es_analogico = [this](unsigned c) {
            return c >= 24 || dio.funcion(CANAL_ADC[c][0], CANAL_ADC[c][1]) == 3;
        };
        adc.v_eref_mas   = [this] { return double(dio.nodo_de(4, 6).voltage()); };
        adc.v_eref_menos = [this] { return double(dio.nodo_de(4, 7).voltage()); };
        adc.f_reloj = [this](unsigned s) {
            switch (s) {
                case 0:  return p4::HZ_MODOSC;
                case 1:  return p4::HZ_SYSOSC;
                case 2:  return cs.f_aclk();
                case 3:  return cs.f_mclk();
                case 4:  return cs.f_smclk();
                case 5:  return cs.f_hsmclk();
                default: return 0.0;
            }
        };
        // VREF+ por el pin, con REFOUT = 1 y P5.6 en función analógica
        d_vref_ = dio.nodo_de(4, 6).register_driver("vref_out");
        dio.nodo_de(4, 6).set_hiz(d_vref_);
        ref.al_cambiar = [this] {
            if (ref.sale_por_pin() && dio.funcion(4, 6) == 3)
                dio.nodo_de(4, 6).set_drive(d_vref_, float(ref.vref()), 1.0f);
            else dio.nodo_de(4, 6).set_hiz(d_vref_);
        };
    }
    int d_vref_ = -1;
    void disparos_adc_proc() {
        adc.disparo(1, ta0.out[1].read()); adc.disparo(2, ta0.out[2].read());
        adc.disparo(3, ta1.out[1].read()); adc.disparo(4, ta1.out[2].read());
        adc.disparo(5, ta2.out[1].read()); adc.disparo(6, ta2.out[2].read());
        adc.disparo(7, ta3.out[1].read());
    }
    static float flota(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

    // Los Timer_A y el Timer32 [1F: datasheet, tablas de pines; segunda fuente,
    // PWMTimerMSP432.h y CaptureMSP432.h de TI-Drivers; P-16 p]
    void conecta_temporizadores() {
        auto fuente = [this](unsigned s) {
            return s == 1 ? cs.f_aclk() : s == 2 ? cs.f_smclk() : 0.0;
        };
        for (unsigned n = 0; n < 4; ++n) {
            ta(n)->f_fuente = fuente;
            ta(n)->irq0(s_irq_[p4::IRQ_TA0_0 + 2 * n]);
            ta(n)->irqn(s_irq_[p4::IRQ_TA0_N + 2 * n]);
        }
        t32.f_mclk = [this] { return cs.f_mclk(); };
        t32.irq1(s_irq_[p4::IRQ_T32_INT1]);
        t32.irq2(s_irq_[p4::IRQ_T32_INT2]);
        t32.irqc(s_irq_[p4::IRQ_T32_INTC]);
        // Mapeables: TA0.0-TA0.4 y TA1.1-TA1.4; TA0CLK y TA1CLK con DIR = 0
        for (unsigned k = 0; k < 5; ++k)
            dio.conecta_mapeable(PmapP4::PM_TA0CCR0A + k, ta0.fn_ta(k));
        for (unsigned k = 1; k < 5; ++k)
            dio.conecta_mapeable(PmapP4::PM_TA1CCR1A + k - 1, ta1.fn_ta(k));
        dio.conecta_mapeable(PmapP4::PM_TA0CLK, ta0.fn_taclk());
        dio.conecta_mapeable(PmapP4::PM_TA1CLK, ta1.fn_taclk());
        // Fijos: TA1.0 (P8.0, secundaria), TA2 en P5.6/P5.7/P6.6/P6.7 y TA2.0
        // en P8.1 (secundaria), TA2CLK en P4.2 (con DIR = 0), TA3 en P10.4,
        // P10.5, P8.2, P9.2, P9.3 y TA3CLK en P8.3
        dio.conecta_funcion(7, 0, 2, ta1.fn_ta(0));
        dio.conecta_funcion(4, 6, 1, ta2.fn_ta(1));
        dio.conecta_funcion(4, 7, 1, ta2.fn_ta(2));
        dio.conecta_funcion(5, 6, 1, ta2.fn_ta(3));
        dio.conecta_funcion(5, 7, 1, ta2.fn_ta(4));
        dio.conecta_funcion(7, 1, 2, ta2.fn_ta(0));
        dio.conecta_funcion(3, 2, 1, ta2.fn_taclk());
        dio.conecta_funcion(9, 4, 1, ta3.fn_ta(0));
        dio.conecta_funcion(9, 5, 1, ta3.fn_ta(1));
        dio.conecta_funcion(7, 2, 1, ta3.fn_ta(2));
        dio.conecta_funcion(8, 2, 1, ta3.fn_ta(3));
        dio.conecta_funcion(8, 3, 1, ta3.fn_ta(4));
        dio.conecta_funcion(7, 3, 1, ta3.fn_taclk());
    }

    // Las eUSCI: su reloj, su interrupción y sus patillas [1F: datasheet,
    // tablas de pines; segunda fuente, las definiciones de TI-Drivers
    // (UARTMSP432.h, SPIMSP432DMA.h, I2CMSP432.h); P-16 p]
    void conecta_comunicaciones() {
        auto brclk = [this](unsigned ssel) {
            return ssel == 0 ? 0.0 : ssel == 1 ? cs.f_aclk() : cs.f_smclk();
        };
        for (unsigned n = 0; n < 4; ++n) {
            uca(n)->f_brclk = brclk; ucb(n)->f_brclk = brclk;
            uca(n)->irq(s_irq_[p4::IRQ_EUSCIA0 + n]);
            ucb(n)->irq(s_irq_[p4::IRQ_EUSCIB0 + n]);
        }
        // (puerto 0 = P1, pin, función, eUSCI, patilla)
        auto fija = [this](unsigned p, unsigned i, unsigned fn, EusciP4& u, char q) {
            dio.conecta_funcion(p, i, fn, q == 's' ? u.fn_simo() : q == 'o' ? u.fn_somi()
                                        : q == 'c' ? u.fn_clk() : u.fn_ste());
        };
        // UCA0 en P1.0-P1.3, UCB0 en P1.4-P1.7: el backchannel de la
        // LaunchPad y el SPI/I2C de los BoosterPack
        fija(0, 0, 1, uca0, 'e'); fija(0, 1, 1, uca0, 'c');
        fija(0, 2, 1, uca0, 'o'); fija(0, 3, 1, uca0, 's');
        fija(0, 4, 1, ucb0, 'e'); fija(0, 5, 1, ucb0, 'c');
        fija(0, 6, 1, ucb0, 's'); fija(0, 7, 1, ucb0, 'o');
        // UCB1 en P6.2-P6.5 (el I2C de la BOOSTXL-EDUMKII, P6.4/P6.5)
        fija(5, 2, 1, ucb1, 'e'); fija(5, 3, 1, ucb1, 'c');
        fija(5, 4, 1, ucb1, 's'); fija(5, 5, 1, ucb1, 'o');
        // UCA3 en P9.4-P9.7
        fija(8, 4, 1, uca3, 'e'); fija(8, 5, 1, uca3, 'c');
        fija(8, 6, 1, uca3, 'o'); fija(8, 7, 1, uca3, 's');
        // UCB3 en P10.0-P10.3 y P8.0/P8.1 (primarias), y en P6.6/P6.7 (secundaria)
        fija(9, 0, 1, ucb3, 'e'); fija(9, 1, 1, ucb3, 'c');
        fija(9, 2, 1, ucb3, 's'); fija(9, 3, 1, ucb3, 'o');
        fija(7, 0, 1, ucb3, 'e'); fija(7, 1, 1, ucb3, 'c');
        fija(5, 6, 2, ucb3, 's'); fija(5, 7, 2, ucb3, 'o');
        // Las mapeables [H: PMAP_*]
        dio.conecta_mapeable(PmapP4::PM_UCA0CLK, uca0.fn_clk());
        dio.conecta_mapeable(PmapP4::PM_UCA0RXD, uca0.fn_somi());
        dio.conecta_mapeable(PmapP4::PM_UCA0TXD, uca0.fn_simo());
        dio.conecta_mapeable(PmapP4::PM_UCB0CLK, ucb0.fn_clk());
        dio.conecta_mapeable(PmapP4::PM_UCB0SDA, ucb0.fn_simo());
        dio.conecta_mapeable(PmapP4::PM_UCB0SCL, ucb0.fn_somi());
        dio.conecta_mapeable(PmapP4::PM_UCA1STE, uca1.fn_ste());
        dio.conecta_mapeable(PmapP4::PM_UCA1CLK, uca1.fn_clk());
        dio.conecta_mapeable(PmapP4::PM_UCA1RXD, uca1.fn_somi());
        dio.conecta_mapeable(PmapP4::PM_UCA1TXD, uca1.fn_simo());
        dio.conecta_mapeable(PmapP4::PM_UCA2STE, uca2.fn_ste());
        dio.conecta_mapeable(PmapP4::PM_UCA2CLK, uca2.fn_clk());
        dio.conecta_mapeable(PmapP4::PM_UCA2RXD, uca2.fn_somi());
        dio.conecta_mapeable(PmapP4::PM_UCA2TXD, uca2.fn_simo());
        dio.conecta_mapeable(PmapP4::PM_UCB2STE, ucb2.fn_ste());
        dio.conecta_mapeable(PmapP4::PM_UCB2CLK, ucb2.fn_clk());
        dio.conecta_mapeable(PmapP4::PM_UCB2SDA, ucb2.fn_simo());
        dio.conecta_mapeable(PmapP4::PM_UCB2SCL, ucb2.fn_somi());
    }

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
        for (unsigned n = 0; n < 4; ++n) {
            uca(n)->recalcula(); ucb(n)->recalcula(); ta(n)->recalcula();
        }
        t32.recalcula();
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
