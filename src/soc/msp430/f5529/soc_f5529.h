// =============================================================================
// soc_f5529.h — EL MSP430F5529 (M4 de doc/analisis_msp430.md)
//
// La CPUXV2 de src/m430 con lo que el F5529 le pone alrededor [DS SLAS590P;
// UG5 SLAU208Q]:
//
//   0x00000-0x00FFF  periféricos (ventanas de 16 bits; los de byte, también)
//   0x01000-0x017FF  BSL (Flash; aquí vacía: 0xFF, M9)
//   0x01800-0x019FF  información A-D (4 x 128 B)
//   0x01A00-0x01AFF  TLV (de fábrica: el ID 0x5529 y las calibraciones)
//   0x01C00-0x023FF  RAM de USB (2 KB; RAM normal con el USB parado)
//   0x02400-0x043FF  RAM (8 KB)
//   0x04400-0x243FF  Flash principal (128 KB en cuatro bancos de 32 KB),
//                    vectores en 0xFF80-0xFFFF
//
// LO QUE HAY, y de dónde sale:
//   SFR, SYS (SYSRSTIV, SYSSNIV, SYSUNIV, el JMB), PMM (VCORE 0-3 con sus
//   banderas de retardo, como las espera PMM_setVCore de DriverLib), UCS (el
//   FLL con FLLN/FLLD/FLLREFDIV, DCORSEL con los márgenes de la hoja de datos
//   y DCOFFG si no llega, REFO, VLO, XT1 a 32 768 Hz y XT2 a 4 MHz con sus
//   banderas de fallo y el cambio a REFO/DCOCLKDIV cuando fallan), la Flash
//   de 5xx (segmento, banco y masivo; palabra, palabra larga; LOCK, LOCKA,
//   LOCKINFO, KEYV y ACCVIFG; la CPU espera si ejecuta de la Flash), CRC16,
//   WDT_A, REF (REFVSEL para el ADC), los puertos P1-P8 y PJ con todas sus
//   funciones y el PMAP de P4, TA0 (5 CCR), TA1 y TA2 (3), TB0 (7, como un
//   Timer_A de 7: sin los registros de comparación agrupados), las cuatro
//   USCI (periph/ti/usci.h), el ADC12_A (periph/ti/adc12_a.h), el MPY32
//   (periph/ti/mpy32.h) y el DMA de tres canales (periph/ti/dma5xx.h).
//   El Comp_B (periph/ti/comp_b.h) y el RTC_A, en calendario y en contador
//   (periph/ti/rtc_a.h). El USB (M5: periph/ti/usb_ti.h), con D+ y D- en los
//   nodos PU.0 y PU.1 y la PLL desde XT2.
//
// La depuración: el EEM L (8 disparadores) y el TAP de Xv2 (JTAG ID 0x91).
// =============================================================================
#ifndef STM32_SOC_MSP430_F5529_SOC_F5529_H
#define STM32_SOC_MSP430_F5529_SOC_F5529_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../../../m430/cpu/nucleo_m430.h"
#include "../../../m430/debug/eem.h"
#include "../../../m430/debug/tap_m430.h"
#include "../../../m430/debug/pines_m430.h"
#include "../../../periph/ti/timer_a.h"
#include "../../../periph/ti/usci.h"
#include "../../../periph/ti/adc12_a.h"
#include "../../../periph/ti/mpy32.h"
#include "../../../periph/ti/dma5xx.h"
#include "../../../periph/ti/comp_b.h"
#include "../../../periph/ti/rtc_a.h"
#include "../../../periph/ti/usb_ti.h"
#include "../../../common/analog_net.h"
#include "../../../pins/pin_mux.h"
#include "../../../common/imagen_fw.h"
#include "../desc/msp430f5529.h"

namespace stm32 {

inline constexpr M430Caps M430_F5529 {"msp430f5529", M430Arq::CPUXV2, m430::msp430f5529::DESC.errata, true};

SC_MODULE(SocF5529), public m430_irq_if {
    static constexpr const m430::Desc430& DESC = m430::msp430f5529::DESC;
    static constexpr unsigned NPIN = 68;                 // P1-P8 (64) y PJ.0-PJ.3

    tlm_utils::simple_target_socket<SocF5529> bus{"bus"};
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_mclk{"hz_mclk"}, hz_smclk{"hz_smclk"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_n{"rst_n"};
    sc_core::sc_signal<bool> clk_dummy{"clk_dummy"}, uno{"uno"};

    NucleoM430 nucleo;
    EemM430 eem{8, 4};
    TapM430 tap{0x91, true, &eem};

    std::vector<std::unique_ptr<AnalogNet>> propios_;
    std::vector<analog_net_if*> pin;          // P1.0..P8.7 (0-63), PJ.0-PJ.3 (64-67)
    analog_net_if& test;
    analog_net_if& rst;
    // El USB (M5): D+ (PU.0) y D- (PU.1); la placa pone 1,5 kOhm de PUR a D+
    analog_net_if& pu0;
    analog_net_if& pu1;
    UsbTi usb;
    std::unique_ptr<PinesM430> pines_dbg;
    double vcc = 3.3;
    double temperatura = 25.0;
    bool cristal_xt1 = true;                  // la F5529LP: Y2, 32 768 Hz en P5.4/P5.5
    bool cristal_xt2 = true;                  // y Y1, 4 MHz en P5.2/P5.3
    double f_xt2_cristal = 4e6;

    // Los Timer: la ventana de 0x40 llega a TAxIV (+0x2E) y TAxEX0 (+0x20)
    TimerAP4 ta0{"timer0_a5", 0x0340, TimerACaps{5, 0x0E, true}, 0x40};
    TimerAP4 ta1{"timer1_a3", 0x0380, TimerACaps{3, 0x0E, true}, 0x40};
    TimerAP4 tb0{"timer0_b7", 0x03C0, TimerACaps{7, 0x0E, true}, 0x40};
    TimerAP4 ta2{"timer2_a3", 0x0400, TimerACaps{3, 0x0E, true}, 0x40};
    tlm_utils::simple_initiator_socket<SocF5529> a_ta0{"a_ta0"}, a_ta1{"a_ta1"}, a_tb0{"a_tb0"}, a_ta2{"a_ta2"};
    sc_core::sc_signal<bool> irq_t[8];        // ta0 0/n, ta1 0/n, tb0 0/n, ta2 0/n
    UsciTi ua0{"usci_a0", false}, ub0{"usci_b0", true}, ua1{"usci_a1", false}, ub1{"usci_b1", true};
    Adc12A adc{"adc12_a"};
    Dma5xx dma{"dma"};
    Mpy32 mpy;
    RtcA rtc{"rtc_a", 0x04A0};
    CompB cb{"comp_b", 0x08C0};
    tlm_utils::simple_initiator_socket<SocF5529> a_rtc{"a_rtc"}, a_cb{"a_cb"};
    sc_core::sc_signal<bool> irq_rtc{"irq_rtc"}, irq_cb{"irq_cb"};

    // Lo que sale por la UCA1 (la F5529LP la lleva al eZ-FET: el backchannel),
    // leído en P4.4 por un monitor propio que también mete lo de `uart_rx`
    // por P4.5. En una placa lo hace su PuenteSerie: `monitor_uart = false`
    std::string uart_tx;
    bool monitor_uart = true;

    SocF5529(sc_core::sc_module_name nm, const Cableado* cab = nullptr)
        : sc_core::sc_module(nm), nucleo("nucleo", M430_F5529),
          test(nodo_alim(cab, "TEST")), rst(nodo_alim(cab, "RST")),
          pu0(nodo_alim(cab, "PU.0")), pu1(nodo_alim(cab, "PU.1")), usb("usb", pu0, pu1), mem_(0x24400, 0xFF) {
        for (unsigned i = 0; i < NPIN; ++i) {
            char n[8];
            if (i < 64) std::snprintf(n, sizeof n, "P%u_%u", 1 + i / 8, i % 8);
            else std::snprintf(n, sizeof n, "PJ_%u", i - 64);
            analog_net_if* x = cab ? cab->busca(i < 64 ? i / 8 : 10, i < 64 ? i % 8 : i - 64) : nullptr;
            n[2] = '.';
            if (!x) {
                propios_.emplace_back(new AnalogNet(n));
                propios_.back()->nombre_esq = n;
                x = propios_.back().get();
            }
            pin.push_back(x);
            id_pin_[i] = x->register_driver(n);
            x->set_hiz(id_pin_[i]);
        }
        // JTAG de 4 hilos en PJ.0-PJ.3 (TDO, TDI, TMS, TCK) con TEST arriba; SBW en TEST/RST
        PinesM430::Pines pj{pin[67], pin[66], pin[65], pin[64], &test, &rst};
        pj.jtag4_con_test = true;
        pines_dbg.reset(new PinesM430("pines_dbg", tap, pj));
        tap.tension_test = [this] { return test.floating() ? 0.0 : double(test.voltage()); };
        bus.register_b_transport(this, &SocF5529::b_transport);
        bus.register_transport_dbg(this, &SocF5529::transport_dbg);
        nucleo.bus.bind(bus);
        nucleo.clk_hz(hz_mclk);
        nucleo.rst_n(rst_n);
        nucleo.cpu.pon_irq(this);
        nucleo.tap = &tap;
        nucleo.eem = &eem;
        nucleo.perif_hi = 0x1000;
        nucleo.cpu.conserva_registros = true;
        // 5xx: con el vector borrado arranca el BSL [UG5 §1.2.1]; el BSL es de
        // M9, y hasta entonces el chip se duerme (LPM4), como un 2xx
        nucleo.vector_vacio_apaga = true;
        nucleo.xv2 = true;
        usb.ram = [this](uint32_t a) -> uint8_t& { return mem_[a]; };
        usb.f_ref = [this](unsigned s) { return s == 0 ? f_xt2_ : f_xt1_; };
        usb.al_cambiar = [this] { avisa_irq(); };
        usb.al_bus = [this] { avisa_irq(); };
        // El TAP de Xv2: el buzón, el BOR, los ID y la firma [UG5 §1.11, §1.14]
        tap.coreip_id = 0x4020;                // [de modelo: TI no lo publica; distinto de 0]
        tap.device_id_ptr = 0x1A00;            // la TLV: el ID del chip en +4
        tap.jmb_estado = [this] {
            return uint16_t(((sysjmbc_ & 0x01) ? 0 : 0x01) | ((sysjmbc_ & 0x02) ? 0 : 0x02) |
                            ((sysjmbc_ & 0x04) ? 0 : 0x04) | ((sysjmbc_ & 0x08) ? 0 : 0x08));
        };
        tap.jmb_entra = [this](unsigned i, uint16_t v) {
            jmbi_[i & 1] = v;
            sysjmbc_ = uint16_t(sysjmbc_ | (1u << (i & 1)));
            sfrifg1_ |= 0x40;                                          // JMBINIFG
            nucleo.nota_irq();
            if (arranque_espera_ && i == 0) mira_patron();
        };
        tap.jmb_sale = [this](unsigned i) {
            const uint16_t v = jmbo_[i & 1];
            sysjmbc_ = uint16_t(sysjmbc_ | (4u << (i & 1)));
            if ((sysjmbc_ & 0x0C) == 0x0C) { sfrifg1_ |= 0x80; nucleo.nota_irq(); }   // JMBOUTIFG
            return v;
        };
        tap.al_bor = [this] { bor(); };
        // El patrón mágico: lo que lo para es JTAG tomando la CPU, o el BOR
        nucleo.al_tce1 = [this](bool) { if (magico_ || arranque_espera_) suelta_arranque(); };
        nucleo.al_parada_dbg = [this](bool) { relojes(); };
        tap.pon_cpu(&nucleo);
        nucleo.al_por = [this](bool v) { if (v) puc(true, 0x00); else { reset_regs(true); rst_n.write(true); } };
        nucleo.cpu.al_cambiar_sr = [this](uint16_t, uint16_t) { relojes(); };
        TimerAP4* tms[4] = {&ta0, &ta1, &tb0, &ta2};
        tlm_utils::simple_initiator_socket<SocF5529>* sks[4] = {&a_ta0, &a_ta1, &a_tb0, &a_ta2};
        for (unsigned k = 0; k < 4; ++k) {
            TimerAP4* t = tms[k];
            t->clk(clk_dummy); t->rst_n(rst_n); t->clk_en(uno); t->clk_hz(hz_smclk);
            t->f_fuente = [this](unsigned s) { return s == 1 ? f_aclk_ : s == 2 ? f_smclk_ : 0.0; };
            sks[k]->bind(t->tsk);
            t->irq0(irq_t[2 * k]); t->irqn(irq_t[2 * k + 1]);
        }
        for (UsciTi* u : {&ua0, &ub0, &ua1, &ub1}) {
            u->clk(clk_dummy); u->rst_n(rst_n); u->clk_en(uno); u->clk_hz(hz_smclk);
            u->f_brclk = [this](unsigned s) { return s == 1 ? f_aclk_ : s >= 2 ? f_smclk_ : 0.0; };
        }
        adc.v_canal = [this](unsigned ch) { return adc_entrada(ch); };
        adc.v_ref = [this](unsigned sref) { return adc_vref(sref); };
        adc.f_reloj = [this](unsigned s) { return s == 0 ? 5e6 : s == 1 ? f_aclk_ : s == 2 ? f_mclk_ : f_smclk_; };
        adc.al_cambiar = [this] { avisa_irq(); };
        adc.al_convertir = [this](unsigned) { dma.dispara(24); };
        dma.lee = [this](uint32_t a, bool byte) { return lee_bus(a, byte); };
        dma.escribe = [this](uint32_t a, uint16_t v, bool byte) { escribe_bus(a, v, byte); };
        dma.f_mclk = [this] { return f_mclk_; };
        dma.al_cambiar = [this] { avisa_irq(); };
        dma.al_disparo = [this](unsigned n) { usb.t_acceso = sc_core::sc_time_stamp() + nucleo.adelanto(); usb.evento_dma(n); };
        dma.ocupado = [this](bool v) { nucleo.espera(v || flash_espera_); dma_ocupado_ = v; };
        rtc.clk(clk_dummy); rtc.rst_n(rst_n); rtc.clk_en(uno); rtc.clk_hz(hz_smclk);
        rtc.f_bclk = [this] { return f_aclk_; };
        rtc.f_smclk = [this] { return f_smclk_; };
        a_rtc.bind(rtc.tsk); rtc.irq(irq_rtc);
        cb.clk(clk_dummy); cb.rst_n(rst_n); cb.clk_en(uno); cb.clk_hz(hz_smclk);
        cb.v_canal = [this](unsigned c) { return cb_entrada(c); };
        cb.v_cc = [this] { return vcc; };
        a_cb.bind(cb.tsk); cb.irq(irq_cb);
        uno.write(true);
        rst_n.write(false);
        SC_HAS_PROCESS(SocF5529);
        SC_METHOD(irq_timers);
        for (auto& s : irq_t) sensitive << s;
        sensitive << irq_rtc << irq_cb;
        dont_initialize();
        SC_METHOD(salidas_proc);
        for (TimerAP4* t : tms) for (unsigned n = 0; n < 7; ++n) sensitive << t->out[n];
        for (UsciTi* u : {&ua0, &ub0, &ua1, &ub1})
            sensitive << u->simo_out << u->simo_oe << u->somi_out << u->somi_oe << u->clk_out << u->clk_oe
                      << u->ste_out << u->ste_oe;
        sensitive << cb.salida;
        dont_initialize();
        // CBOUT, por dentro, a la captura CCI1B del Timer1_A3 [DS SLAS590P, «TA1 Signal Connections»]
        SC_METHOD(cbout_proc); sensitive << cb.salida; dont_initialize();
        SC_METHOD(usci_proc);
        for (UsciTi* u : {&ua0, &ub0, &ua1, &ub1}) sensitive << u->cambio();
        dont_initialize();
        SC_METHOD(entradas_proc);
        for (auto& p : pin) sensitive << p->value_changed_event();
        dont_initialize();
        SC_THREAD(wdt_proc);
        SC_THREAD(flash_proc);
        SC_THREAD(osc_proc);
        SC_METHOD(pmm_proc); sensitive << ev_pmm_; dont_initialize();
        SC_METHOD(dly_proc); sensitive << ev_dly_; dont_initialize();
        SC_METHOD(puc_fin_proc); sensitive << ev_puc_fin_; dont_initialize();
        SC_METHOD(bor_fin_proc); sensitive << ev_bor_fin_; dont_initialize();
        SC_METHOD(test_bajo_proc); sensitive << ev_test_bajo_; dont_initialize();
        SC_METHOD(test_proc); sensitive << test.value_changed_event(); dont_initialize();
        SC_METHOD(rst_pin_proc); sensitive << rst.value_changed_event() << ev_rst_pin_; dont_initialize();
        SC_THREAD(uart_mon_proc);
        fabrica();
        reset_regs(true);
    }

    M430Cpu& cpu() { return nucleo.cpu; }
    analog_net_if& p(unsigned puerto, unsigned bit) { return *pin[(puerto - 1) * 8 + bit]; }
    analog_net_if& pj(unsigned bit) { return *pin[64 + bit]; }

    // Carga un ELF o un TI-TXT en la Flash; deja el chip en reset
    std::string carga(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::string e = lee_elf(ruta, t, ELF_MSP430);
        if (!e.empty() && formato_fw(ruta) == FormatoFw::TiTxt) e = lee_titxt(ruta, t);
        if (!e.empty()) return e;
        std::fill(mem_.begin() + 0x4400, mem_.end(), 0xFF);
        std::fill(mem_.begin() + 0x1800, mem_.begin() + 0x1A00, 0xFF);
        for (const TrozoFw& tr : t)
            for (size_t i = 0; i < tr.datos.size(); ++i) {
                const uint32_t a = tr.dir + uint32_t(i);
                if (es_flash(a) || es_ram(a)) mem_[a] = tr.datos[i];
            }
        return std::string();
    }
    void reset(bool activo) {
        if (activo) rst_n.write(false);
        else { rstiv_.clear(); pon_rstiv(0x02); sysjmbc_ = 0x000C; reset_regs(true); arranque(); rst_n.write(true); }
    }
    bool bloqueado_por_firma() const { return tap.fusible(); }
    bool parado_por_patron() const { return magico_; }
    unsigned veces_patron() const { return n_magico_; }
    uint8_t mem8(uint32_t a) const { return a < mem_.size() ? mem_[a] : 0; }
    uint16_t mem16(uint32_t a) const { return uint16_t(mem8(a) | (mem8(a + 1) << 8)); }
    void poke(uint32_t a, uint8_t v) { if (a < mem_.size()) mem_[a] = v; }
    double f_mclk() const { return f_mclk_; }
    double f_smclk() const { return f_smclk_; }
    double f_aclk() const { return f_aclk_; }
    double f_dco() const { return f_dco_; }
    unsigned vcore() const { return pmmctl0_ & 3u; }
    unsigned pucs() const { return n_puc_; }
    uint16_t ucsctl7() const { return ucsctl7_; }
    // Mete un byte por la UCA1 (RXD, P4.5), como el eZ-FET: a la velocidad que tenga
    void uart_rx(uint8_t b) { rx_cola_.push_back(b); ev_rx_.notify(sc_core::SC_ZERO_TIME); }
    bool avisado_vcore() const { return avisado_vcore_; }

    // --- m430_irq_if -------------------------------------------------------------
    uint16_t pendiente(bool gie) override {
        // SYSNMI (0xFFFC): VMA, JMB; UNMI (0xFFFA): NMIIFG, OFIFG y ACCVIFG con su IE
        if (((sfrifg1_ & 0x08) && (sfrie1_ & 0x08)) || ((sfrifg1_ & 0x40) && (sfrie1_ & 0x40)) ||
            ((sfrifg1_ & 0x80) && (sfrie1_ & 0x80)))
            return 0xFFFC;
        if (((sfrifg1_ & 0x10) && (sfrie1_ & 0x10)) || ((sfrifg1_ & 0x02) && (sfrie1_ & 0x02)) ||
            ((fctl3_ & 0x04) && (sfrie1_ & 0x20)))
            return 0xFFFA;
        if (!gie) return 0;
        const uint64_t f = fuentes();
        if (!f) return 0;
        unsigned i = 63;
        while (!((f >> i) & 1u)) --i;
        return uint16_t(0xFF80u + 2 * i);
    }
    void acepta(uint16_t v) override {
        if (v == 0xFFFA || v == 0xFFFC) return;                       // las borra el programa (SYSUNIV/SYSSNIV)
        if (v == 0xFFF2 && (wdtctl_ & 0x10)) sfrifg1_ = uint16_t(sfrifg1_ & ~1u);   // WDTIFG, de un origen
        if (v == 0xFFEA) ta0.reconoce_ccr0();
        if (v == 0xFFE2) ta1.reconoce_ccr0();
        if (v == 0xFFF6) tb0.reconoce_ccr0();
        if (v == 0xFFD8) ta2.reconoce_ccr0();
    }

private:
    analog_net_if& nodo_alim(const Cableado* cab, const char* n) {
        if (cab) if (analog_net_if* x = cab->busca_alim(n)) return *x;
        propios_.emplace_back(new AnalogNet(n));
        return *propios_.back();
    }
    std::vector<uint8_t> mem_;
    int id_pin_[NPIN];
    unsigned n_puc_ = 0;

    bool es_ram(uint32_t a) const { return (a >= 0x1C00 && a < 0x4400); }
    bool es_flash(uint32_t a) const { return (a >= 0x4400 && a < 0x24400) || (a >= 0x1800 && a < 0x1A00) || (a >= 0x1000 && a < 0x1800); }

    // --- SFR y SYS ------------------------------------------------------------------
    uint16_t sfrie1_ = 0, sfrifg1_ = 0, sfrrpcr_ = 0x1C;
    uint16_t sysctl_ = 0, sysbslc_ = 0x8003, sysjmbc_ = 0x000C, jmbi_[2] = {}, jmbo_[2] = {};
    std::vector<uint16_t> rstiv_;             // las causas de reset pendientes
    void pon_rstiv(uint16_t v) { if (v && std::find(rstiv_.begin(), rstiv_.end(), v) == rstiv_.end()) rstiv_.push_back(v); }
    uint16_t lee_rstiv(bool dbg) {
        if (rstiv_.empty()) return 0;
        auto it = std::min_element(rstiv_.begin(), rstiv_.end());
        const uint16_t v = *it;
        if (!dbg) rstiv_.erase(it);
        return v;
    }
    // SYSUNIV: 2 NMIIFG, 4 OFIFG, 6 ACCVIFG [UG5 tabla 1-?]
    uint16_t lee_univ(bool dbg) {
        if (sfrifg1_ & 0x10) { if (!dbg) sfrifg1_ = uint16_t(sfrifg1_ & ~0x10); return 2; }
        if (sfrifg1_ & 0x02) return 4;                                  // OFIFG: la borra el programa
        if (fctl3_ & 0x04) { if (!dbg) fctl3_ = uint16_t(fctl3_ & ~0x04); return 6; }
        return 0;
    }

    // --- PMM ---------------------------------------------------------------------
    uint16_t pmmctl0_ = 0x9600, pmmctl1_ = 0, svsmhctl_ = 0x4400, svsmlctl_ = 0x4400, svsmio_ = 0x0020,
             pmmifg_ = 0, pmmrie_ = 0x0130, pm5ctl0_ = 0;
    bool pmm_abierto_ = false, avisado_vcore_ = false;
    unsigned vcore_obj_ = 0;
    sc_core::sc_event ev_pmm_, ev_svsmh_, ev_svsml_;
    // Los retardos del SVS/SVM (modo normal: ~20 µs) y de subir VCORE [DS t(SVSM)]
    void pmm_proc() {
        // un cambio de nivel pendiente ya ha llegado
        pmmctl0_ = uint16_t((pmmctl0_ & ~3u) | vcore_obj_);
        pmmifg_ |= 0x0004;                                            // SVMLVLRIFG
        comprueba_vcore();
    }
    void comprueba_vcore() {
        static const double MAX[4] = {8e6, 12e6, 20e6, 25e6};
        if (f_mclk_ > MAX[vcore()] * 1.02 && !avisado_vcore_) {
            avisado_vcore_ = true;
            std::fprintf(stderr, "[f5529] MCLK %.2f MHz con PMMCOREV = %u (máximo %.0f MHz): fuera de especificación [DS]\n",
                         f_mclk_ / 1e6, vcore(), MAX[vcore()] / 1e6);
        }
    }

    // --- UCS -----------------------------------------------------------------------
    uint16_t ucsctl_[9] = {};
    uint16_t ucsctl7_ = 0x0703;                                       // XT1LFOFFG, DCOFFG... al reset
    double f_dco_ = 0, f_dcodiv_ = 0, f_mclk_ = 0, f_smclk_ = 0, f_aclk_ = 0, f_xt1_ = 0, f_xt2_ = 0;
    bool xt1_ok_ = false, xt2_ok_ = false;
    sc_core::sc_time xt1_desde_, xt2_desde_;
    bool xt1_pedido_ = false, xt2_pedido_ = false;
    sc_core::sc_event ev_osc_;
    static constexpr double F_REFO = 32768.0, F_VLO = 9400.0, F_MODOSC = 4.8e6;
    // [DS «DCO Frequency»]: los extremos de cada DCORSEL (DCO = 0 y DCO = 31, MOD = 0)
    static double dco_min(unsigned r) { static const double m[8] = {0.07e6, 0.15e6, 0.32e6, 0.64e6, 1.3e6, 2.5e6, 4.6e6, 8.5e6}; return m[r & 7]; }
    static double dco_max(unsigned r) { static const double m[8] = {1.7e6, 3.45e6, 7.38e6, 14.0e6, 28.2e6, 54.1e6, 88.0e6, 135e6}; return m[r & 7]; }
    double f_fuente(unsigned s) const {
        switch (s) {
        case 0: return xt1_ok_ ? f_xt1_ : F_REFO;                    // XT1CLK (si falla, REFO)
        case 1: return F_VLO;
        case 2: return F_REFO;
        case 3: return f_dco_;
        case 4: return f_dcodiv_;
        default: return xt2_ok_ ? f_xt2_ : f_dcodiv_;                 // XT2CLK (si falla, DCOCLKDIV)
        }
    }
    void relojes() {
        const uint16_t sr = nucleo.cpu.sr();
        // Los cristales: XT1 con XT1OFF = 0 y P5.4/P5.5 en función; XT2 con
        // XT2OFF = 0 y P5.2/P5.3 (o pedidos por un reloj que los usa) [UG5 §5.2.4]
        const bool p_xt1 = (pt_[4].sel & 0x30) == 0x30, p_xt2 = (pt_[4].sel & 0x0C) == 0x0C;
        const bool xt1_on = cristal_xt1 && p_xt1 && !(ucsctl_[6] & 0x0001);
        const bool xt2_on = cristal_xt2 && p_xt2 && !(ucsctl_[6] & 0x0100);
        if (xt1_on != xt1_pedido_) { xt1_pedido_ = xt1_on; xt1_desde_ = sc_core::sc_time_stamp(); if (!xt1_on) xt1_ok_ = false; ev_osc_.notify(sc_core::SC_ZERO_TIME); }
        if (xt2_on != xt2_pedido_) { xt2_pedido_ = xt2_on; xt2_desde_ = sc_core::sc_time_stamp(); if (!xt2_on) xt2_ok_ = false; ev_osc_.notify(sc_core::SC_ZERO_TIME); }
        f_xt1_ = xt1_ok_ ? 32768.0 : 0.0;
        f_xt2_ = xt2_ok_ ? f_xt2_cristal : 0.0;
        // El FLL: DCOCLK = D·(N+1)·fFLLREF/n, salvo que SCG0 lo pare (el DCO
        // se queda donde estaba) o que no quepa en DCORSEL (DCOFFG)
        const unsigned rsel = (ucsctl_[1] >> 4) & 7u, n = ucsctl_[2] & 0x3FFu, d = 1u << ((ucsctl_[2] >> 12) & 7u);
        const unsigned selref = (ucsctl_[3] >> 4) & 7u, rdiv_c = ucsctl_[3] & 7u;
        static const unsigned RDIV[8] = {1, 2, 4, 8, 12, 16, 16, 16};
        double fref = selref == 2 ? F_REFO : selref == 5 ? (xt2_ok_ ? f_xt2_ : F_REFO) : (xt1_ok_ ? f_xt1_ : F_REFO);
        fref /= RDIV[rdiv_c];
        bool dcoffg = false;
        if (!(sr & 0x0040) || f_dco_ == 0.0) {                        // SCG0 = 0: el FLL manda
            double obj = double(d) * double(n + 1) * fref;
            if (obj < dco_min(rsel)) { obj = dco_min(rsel); dcoffg = true; }
            if (obj > dco_max(rsel)) { obj = dco_max(rsel); dcoffg = true; }
            f_dco_ = obj;
        }
        dco_fallo_ = dcoffg;
        f_dcodiv_ = f_dco_ / double(d);
        const unsigned sela = (ucsctl_[4] >> 8) & 7u, sels = (ucsctl_[4] >> 4) & 7u, selm = ucsctl_[4] & 7u;
        auto div = [&](unsigned sh) { return double(1u << ((ucsctl_[5] >> sh) & 7u)); };
        f_mclk_ = f_fuente(selm) / div(0);
        // SCG1 o SMCLKOFF; con SCG1, una petición de un módulo (SMCLKREQEN) lo
        // mantiene [UG5 §5.2.10]: aquí, la de una USCI que está enviando
        smclk_pedido_ = pide_smclk();
        f_smclk_ = ((sr & 0x0080) && !((ucsctl_[8] & 0x0004) && smclk_pedido_)) || (ucsctl_[6] & 0x0002)
                   ? 0.0 : f_fuente(sels) / div(4);
        f_aclk_ = f_fuente(sela) / div(8);
        if (sr & 0x0020) f_aclk_ = f_aclk_;                           // OSCOFF: XT1 en LPM4 (no se modela el corte)
        // El control de relojes del EEM con la CPU en manos del depurador
        if (nucleo.parada_dbg()) {
            if (eem.genclkctrl() & EemM430::STOP_SMCLK) f_smclk_ = 0.0;
            if (eem.genclkctrl() & EemM430::STOP_ACLK) f_aclk_ = 0.0;
        }
        actualiza_fallos();
        // El perro, congelado mientras su reloj no corre
        {
            const double fw = wdt_f();
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (fw <= 0.0 && !wdt_pausa_) { wdt_pausa_ = true; wdt_t_pausa_ = ahora; }
            else if (fw > 0.0 && wdt_pausa_) { wdt_pausa_ = false; wdt_t0_ += ahora - wdt_t_pausa_; }
        }
        hz_mclk.write(f_mclk_);
        hz_smclk.write(f_smclk_);
        ta0.recalcula(); ta1.recalcula(); tb0.recalcula(); ta2.recalcula();
        for (UsciTi* u : {&ua0, &ub0, &ua1, &ub1}) u->recalcula();
        rtc.recalcula_a();
        usb.relojes();
        ev_wdt_.notify(sc_core::SC_ZERO_TIME);
        comprueba_vcore();
    }
    bool dco_fallo_ = false;
    // UCSCTL7: los fallos se ponen solos mientras duran; borrarlos con el
    // oscilador bien los deja borrados [UG5 §5.2.12]
    void actualiza_fallos() {
        const uint16_t antes = ucsctl7_;
        // Un cristal solo «falla» si está pedido: XT1OFF/XT2OFF a 0, o un
        // reloj (ACLK, SMCLK, MCLK o la referencia del FLL) que lo usa; apagado
        // y sin nadie que lo use, la bandera se borra y no vuelve [UG5 §5.2.12]
        const unsigned sela = (ucsctl_[4] >> 8) & 7u, sels = (ucsctl_[4] >> 4) & 7u, selm = ucsctl_[4] & 7u;
        const unsigned selref = (ucsctl_[3] >> 4) & 7u;
        const bool usa_xt1 = !(ucsctl_[6] & 0x0001) || sela == 0 || sels == 0 || selm == 0 || selref < 2;
        const bool usa_xt2 = !(ucsctl_[6] & 0x0100) || sela >= 5 || sels >= 5 || selm >= 5 || selref == 5;
        if (!xt1_ok_ && usa_xt1) ucsctl7_ |= 0x0002;                  // XT1LFOFFG
        if (!xt2_ok_ && usa_xt2) ucsctl7_ |= 0x0008;                  // XT2OFFG
        if (dco_fallo_) ucsctl7_ |= 0x0001;                           // DCOFFG
        if (ucsctl7_ & 0x000F) sfrifg1_ |= 0x02;                      // OFIFG
        if (ucsctl7_ != antes) avisa_irq();
    }
    void osc_proc() {
        // El arranque de los cristales [DS t(START)]: aquí 2 ms el de 32 kHz y 1 ms el de 4 MHz
        for (;;) {
            wait(ev_osc_);
            for (;;) {
                const sc_core::sc_time ahora = sc_core::sc_time_stamp();
                sc_core::sc_time falta = sc_core::SC_ZERO_TIME;
                bool cambia = false;
                if (xt1_pedido_ && !xt1_ok_) {
                    const sc_core::sc_time fin = xt1_desde_ + sc_core::sc_time(2, sc_core::SC_MS);
                    if (ahora >= fin) { xt1_ok_ = true; cambia = true; } else falta = fin - ahora;
                }
                if (xt2_pedido_ && !xt2_ok_) {
                    const sc_core::sc_time fin = xt2_desde_ + sc_core::sc_time(1, sc_core::SC_MS);
                    if (ahora >= fin) { xt2_ok_ = true; cambia = true; }
                    else if (falta == sc_core::SC_ZERO_TIME || fin - ahora < falta) falta = fin - ahora;
                }
                if (cambia) relojes();
                if (falta == sc_core::SC_ZERO_TIME) break;
                wait(falta, ev_osc_);
            }
        }
    }

    // --- WDT_A ---------------------------------------------------------------------
    uint16_t wdtctl_ = 0x0004;
    sc_core::sc_event ev_wdt_;
    sc_core::sc_time wdt_t0_{sc_core::SC_ZERO_TIME}, wdt_t_pausa_;
    bool wdt_pausa_ = false;
    double wdt_f() const {
        const unsigned s = (wdtctl_ >> 5) & 3u;
        return s == 0 ? f_smclk_ : s == 1 ? f_aclk_ : F_VLO;
    }
    sc_core::sc_time wdt_periodo() const {
        static const double DIV[8] = {2147483648.0, 134217728.0, 8388608.0, 524288.0, 32768.0, 8192.0, 512.0, 64.0};
        const double f = wdt_f();
        if (f <= 0.0) return sc_core::SC_ZERO_TIME;
        return sc_core::sc_time(DIV[wdtctl_ & 7] / f, sc_core::SC_SEC);
    }
    void wdt_proc() {
        for (;;) {
            const sc_core::sc_time p = wdt_periodo();
            if ((wdtctl_ & 0x80) || p == sc_core::SC_ZERO_TIME || !rst_n.read()) { wait(ev_wdt_); continue; }
            const sc_core::sc_time fin = wdt_t0_ + p;
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (fin > ahora) { wait(fin - ahora, ev_wdt_); continue; }
            wdt_t0_ = ahora;
            sfrifg1_ |= 0x01;                                         // WDTIFG
            if (wdtctl_ & 0x10) avisa_irq();
            else { puc(false, 0x16); wait(ev_wdt_); }                 // SYSRSTIV: WDT timeout
        }
    }

    // --- Flash de 5xx --------------------------------------------------------------
    uint16_t fctl1_ = 0, fctl3_ = 0x58, fctl4_ = 0;
    sc_core::sc_event ev_flash_;
    struct OpFlash { int tipo = 0; uint32_t a = 0; uint16_t v = 0; bool byte = false; } op_;
    bool flash_espera_ = false, dma_ocupado_ = false;
    uint32_t largo_a_ = 0; uint16_t largo_v_ = 0; bool largo_medio_ = false;
    bool info_bloqueada(uint32_t a) const {
        if (a >= 0x1800 && a < 0x1A00 && (fctl4_ & 0x80)) return true;            // LOCKINFO
        if (a >= 0x1980 && a < 0x1A00 && (fctl3_ & 0x40)) return true;            // LOCKA
        return false;
    }
    void acceso_mal() { fctl3_ |= 0x04; avisa_irq(); }                             // ACCVIFG
    void escribe_flash(uint32_t a, uint16_t v, bool byte) {
        if ((fctl3_ & 0x10) || (fctl3_ & 0x01)) { acceso_mal(); return; }         // LOCK o BUSY
        // El BSL, protegido mientras SYSBSLPE [UG5 §1.15.3]: ProgramLockKey lo
        // abre (SYSBSLC = 3) para escribir la firma de 0x17FC
        if (info_bloqueada(a) || (a >= 0x1000 && a < 0x1800 && (sysbslc_ & 0x8000))) { acceso_mal(); return; }
        const uint16_t m = fctl1_ & 0xE6;
        if (m & 0x06) op_ = OpFlash{(m & 0x06) == 0x02 ? 1 : (m & 0x06) == 0x04 ? 2 : 4, a, v, byte};
        else if (m & 0x40) {
            if (m & 0x80) {                                                         // palabra larga: dos palabras
                if (!largo_medio_) { largo_a_ = a; largo_v_ = v; largo_medio_ = true; return; }
                largo_medio_ = false;
                op_ = OpFlash{5, largo_a_ & ~3u, largo_v_, false};
                largo_v2_ = v;
            } else op_ = OpFlash{3, a, v, byte};
        } else { acceso_mal(); return; }
        fctl3_ |= 0x01;                                                             // BUSY
        // La CPU espera si está ejecutando de la Flash [UG5 §7.3.2]
        const uint32_t pc = nucleo.cpu.pc();
        flash_espera_ = !es_ram(pc);
        if (flash_espera_) nucleo.espera(true);
        ev_flash_.notify(sc_core::SC_ZERO_TIME);
    }
    uint16_t largo_v2_ = 0;
    void borra(uint32_t ini, uint32_t fin) { std::fill(mem_.begin() + ini, mem_.begin() + fin, 0xFF); }
    void flash_proc() {
        for (;;) {
            wait(ev_flash_);
            // [DS «Flash Memory»]: palabra 64-85 µs, palabra larga 145 µs, borrado 23-32 ms
            const double t = op_.tipo == 3 ? 75e-6 : op_.tipo == 5 ? 145e-6 : 25e-3;
            wait(sc_core::sc_time(t, sc_core::SC_SEC));
            const uint32_t a = op_.a;
            if (op_.tipo == 3) {
                mem_[a] &= uint8_t(op_.v);
                if (!op_.byte) mem_[a + 1] &= uint8_t(op_.v >> 8);
            } else if (op_.tipo == 5) {
                mem_[a] &= uint8_t(op_.v); mem_[a + 1] &= uint8_t(op_.v >> 8);
                mem_[a + 2] &= uint8_t(largo_v2_); mem_[a + 3] &= uint8_t(largo_v2_ >> 8);
            } else if (op_.tipo == 1) {                                             // segmento
                if (a >= 0x1800 && a < 0x1A00) borra(a & ~0x7Fu, (a & ~0x7Fu) + 0x80);
                else borra(a & ~0x1FFu, (a & ~0x1FFu) + 0x200);
                fctl1_ = uint16_t(fctl1_ & ~0x06);
            } else if (op_.tipo == 2) {                                             // banco de 32 KB
                if (a >= 0x4400) {
                    const uint32_t b = 0x4400 + ((a - 0x4400) / 0x8000) * 0x8000;
                    borra(b, b + 0x8000);
                }
                fctl1_ = uint16_t(fctl1_ & ~0x06);
            } else {                                                                // masivo: la principal
                borra(0x4400, 0x24400);
                fctl1_ = uint16_t(fctl1_ & ~0x06);
            }
            fctl3_ = uint16_t((fctl3_ & ~0x01) | 0x08);                            // BUSY a 0, WAIT a 1
            if (flash_espera_) { flash_espera_ = false; nucleo.espera(dma_ocupado_); }
        }
    }

    // --- CRC16 ----------------------------------------------------------------------
    uint16_t crc_ = 0xFFFF;
    static uint8_t al_reves8(uint8_t b) {
        b = uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
        b = uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
        return uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
    }
    void crc_byte(uint8_t b) {
        for (unsigned i = 0; i < 8; ++i) {
            const bool fb = ((crc_ >> 15) & 1u) ^ ((b >> i) & 1u);
            crc_ = uint16_t(crc_ << 1);
            if (fb) crc_ ^= 0x1021;
        }
    }

    // --- REF ------------------------------------------------------------------------
    uint16_t refctl0_ = 0x0080;                                       // REFMSTR
    double vref_interna() const {
        // Con REFMSTR, la manda el REF (REFVSEL); sin él, el ADC12_A (ADC12REF2_5V)
        if (refctl0_ & 0x80) {
            const unsigned s = (refctl0_ >> 4) & 3u;
            return (refctl0_ & 0x01) ? (s == 0 ? 1.5 : s == 1 ? 2.0 : 2.5) : 0.0;
        }
        return adc.refon_interno() ? (adc.ref2_5() ? 2.5 : 1.5) : 0.0;
    }
    double adc_vref(unsigned sref) const {
        switch (sref & 7u) {
        case 1: case 5: return vref_interna();                       // VREF+
        case 2: case 3: case 6: case 7: return pin[32]->floating() ? 0.0 : double(pin[32]->voltage());   // VeREF+ (P5.0)
        default: return vcc;                                          // AVCC
        }
    }
    double adc_entrada(unsigned ch) const {
        if (ch < 8) return pin[40 + ch]->floating() ? 0.0 : double(pin[40 + ch]->voltage());   // P6.0-P6.7
        if (ch == 8 || ch == 9) return pin[32 + (ch - 8)]->floating() ? 0.0 : double(pin[32 + (ch - 8)]->voltage());
        if (ch == 10) return 0.00255 * temperatura + 0.680;           // [DS: TCSENSOR, VSENSOR]
        if (ch == 11) return vcc / 2.0;
        return pin[48 + (ch - 12)]->floating() ? 0.0 : double(pin[48 + (ch - 12)]->voltage());  // P7.0-P7.3
    }

    // Comp_B: CB0-CB7 en P6.0-P6.7, CB8-CB11 en P7.0-P7.3 [DS SLAS590P]
    double cb_entrada(unsigned c) const {
        const unsigned i = c < 8 ? 40 + c : c < 12 ? 48 + (c - 8) : 0;
        if (c >= 12 || pin[i]->floating()) return 0.0;
        return double(pin[i]->voltage());
    }

    // --- TLV de fábrica -----------------------------------------------------------
    void fabrica() {
        uint8_t* t = &mem_[0x1A00];
        std::fill(t, t + 0x100, 0xFF);
        t[0x00] = 0x06; t[0x01] = 0x06;                               // longitudes
        t[0x04] = 0x55; t[0x05] = 0x29;                               // el ID: 0x5529
        t[0x06] = 0x12; t[0x07] = 0x18;                               // revisiones de HW y FW [de modelo]
        t[0x08] = 0x08; t[0x09] = 0x0A;                               // TLV_DIERECORD
        for (unsigned i = 0; i < 10; ++i) t[0x0A + i] = uint8_t(0x30 + i);
        t[0x14] = 0x11; t[0x15] = 0x10;                               // TLV_ADC12CAL
        auto w = [&](unsigned o, uint16_t v) { t[o] = uint8_t(v); t[o + 1] = uint8_t(v >> 8); };
        w(0x16, 0x8000); w(0x18, 0x0000);                             // ganancia 1, sin desplazamiento
        // Las del sensor, desde el modelo: 30 y 85 °C con 1,5, 2,0 y 2,5 V
        const double vr[3] = {1.5, 2.0, 2.5};
        for (unsigned k = 0; k < 3; ++k) {
            w(0x1A + 4 * k, uint16_t(std::lround((0.00255 * 30 + 0.680) / vr[k] * 4095.0)));
            w(0x1C + 4 * k, uint16_t(std::lround((0.00255 * 85 + 0.680) / vr[k] * 4095.0)));
        }
        t[0x26] = 0x12; t[0x27] = 0x06;                               // TLV_REFCAL: factores de 1
        w(0x28, 0x8000); w(0x2A, 0x8000); w(0x2C, 0x8000);
        // CRC-CCITT de 0x1A04 a 0x1AFF [UG5 §1.13.2]
        uint16_t c = 0xFFFF;
        for (unsigned i = 0x04; i < 0x100; ++i) {
            uint8_t b = t[i];
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        w(0x02, c);
    }

    // --- Reset ---------------------------------------------------------------------
    // `bor`: el de encendido (y el POR por JTAG): todo; si no, el PUC
    void reset_regs(bool bor) {
        sfrie1_ = 0;
        sfrifg1_ = uint16_t((sfrifg1_ & 0x11) | (bor ? 0 : 0));
        if (bor) { sysbslc_ = 0x8003; pmmctl0_ = 0x9600; svsmhctl_ = svsmlctl_ = 0x4400; pmmifg_ = 0; avisado_vcore_ = false; vcore_obj_ = 0; }
        pmm_abierto_ = false;
        sysctl_ = 0;
        usb.reset();
        // El JMB no se toca con el PUC ni con el pin RST: el patrón mágico se
        // escribe con RST abajo y lo lee el arranque [de modelo; UG5 §1.14]
        if (bor) sysjmbc_ = uint16_t(sysjmbc_ & 0x0F);
        wdtctl_ = 0x0004; wdt_t0_ = sc_core::sc_time_stamp(); wdt_pausa_ = false;
        ucsctl_[0] = 0x0000; ucsctl_[1] = 0x0020; ucsctl_[2] = 0x101F; ucsctl_[3] = 0x0000;
        ucsctl_[4] = 0x0044; ucsctl_[5] = 0x0000; ucsctl_[6] = 0xC1CD; ucsctl_[8] = 0x0707;
        ucsctl7_ = 0x0703;
        f_dco_ = 0.0;
        for (Puerto& x : pt_) { x.out = x.dir = x.ifg = x.ies = x.ie = x.sel = x.ren = x.ds = 0; }
        pmap_[0] = 13; pmap_[1] = 15; pmap_[2] = 14; pmap_[3] = 16; pmap_[4] = 12; pmap_[5] = 11; pmap_[6] = 0; pmap_[7] = 0;
        pmap_llave_ = false; pmapctl_ = 0x0001;
        fctl1_ = 0; fctl3_ = 0x58; fctl4_ = 0; largo_medio_ = false;
        refctl0_ = 0x0080;
        crc_ = 0xFFFF;
        mpy.reset();
        adc.reset();
        dma.reset();
        xt1_ok_ = xt2_ok_ = false; xt1_pedido_ = xt2_pedido_ = false;
        relojes();
        pines_salida();
        ev_wdt_.notify(sc_core::SC_ZERO_TIME);
    }
    // El PUC (con su causa en SYSRSTIV)
    sc_core::sc_event ev_puc_fin_;
    void puc(bool mantener, uint16_t causa) {
        ++n_puc_;
        pon_rstiv(causa);
        rst_n.write(false);
        if (!mantener) ev_puc_fin_.notify(sc_core::sc_time(2, sc_core::SC_US));
    }
    void puc_fin_proc() { reset_regs(false); rst_n.write(true); }
    // El BOR por JTAG (TEST_REG): como el pin RST
    void bor() {
        rst_n.write(false);
        pon_rstiv(0x02);
        reset_regs(true);
        ev_bor_fin_.notify(sc_core::sc_time(2, sc_core::SC_US));
    }
    sc_core::sc_event ev_bor_fin_;
    void bor_fin_proc() { arranque(); rst_n.write(true); }
    // Lo que hace el código de arranque de 5xx al salir del BOR [UG5 §1.11,
    // §1.14]: mira la firma de JTAG (0x17FC-0x17FF: ni 0 ni todo unos,
    // bloqueado) y el patrón mágico (0xA55A en JMBI0: la CPU se queda
    // parada en el arranque hasta que JTAG la tome)
    // [de modelo] Con TEST arriba (la lógica de prueba activa: una herramienta
    // está entrando), el arranque espera: así ve el patrón que llega por el
    // JMB con RST ya suelto (magicPattern() de TI suelta RST antes de
    // escribirlo); si TEST baja del todo (más que el tope de SBW) sin patrón,
    // arranca
    bool magico_ = false, arranque_espera_ = false;
    unsigned n_magico_ = 0;
    void arranque() {
        const uint32_t f = uint32_t(mem16(0x17FC)) | (uint32_t(mem16(0x17FE)) << 16);
        tap.pon_bloqueo(f != 0 && f != 0xFFFFFFFFu);
        if (tap.fusible()) return;                                     // bloqueado: ni JMB ni espera
        mira_patron();
        if (!magico_ && !test.floating() && test.voltage() > 0.5f * float(vcc)) {
            arranque_espera_ = true;
            nucleo.espera(true);
        }
    }
    void mira_patron() {
        if ((sysjmbc_ & 0x01) && jmbi_[0] == 0xA55A) {
            sysjmbc_ = uint16_t(sysjmbc_ & ~0x01);
            magico_ = true;
            arranque_espera_ = false;
            ++n_magico_;
            nucleo.espera(true);
        }
    }
    void suelta_arranque() {
        magico_ = arranque_espera_ = false;
        nucleo.espera(flash_espera_ || dma_ocupado_);
    }
    sc_core::sc_event ev_test_bajo_;
    void test_bajo_proc() {
        if (arranque_espera_ && !magico_ && (test.floating() || test.voltage() < 0.5f * float(vcc))) suelta_arranque();
    }
    sc_core::sc_event ev_rst_pin_;
    bool rst_pin_bajo_ = false;
    void rst_pin_proc() {
        const bool bajo = !rst.floating() && rst.voltage() < 0.5f * float(vcc) && !pines_dbg->rst_es_sbw();
        if (bajo && !rst_pin_bajo_) {
            if (!(ev_rst_pin_.triggered())) { ev_rst_pin_.notify(sc_core::sc_time(2, sc_core::SC_US)); return; }
            rst_pin_bajo_ = true;
            rst_n.write(false);
        } else if (!bajo && rst_pin_bajo_) {
            rst_pin_bajo_ = false;
            pon_rstiv(0x04);                                          // RST/NMI
            reset_regs(true);
            arranque();
            rst_n.write(true);
        }
    }
    void test_proc() {
        pines_salida();
        if (arranque_espera_ && (test.floating() || test.voltage() < 0.5f * float(vcc)))
            ev_test_bajo_.notify(sc_core::sc_time(150, sc_core::SC_US));   // más que el tope de SBW
    }

    // --- Interrupciones -------------------------------------------------------------
    // Una por vector: bit (v - 0xFF80)/2
    uint64_t fuentes() {
        uint64_t f = 0;
        auto pon = [&](uint16_t v) { f |= 1ull << ((v - 0xFF80u) / 2); };
        if (sfrifg1_ & sfrie1_ & 0x01 && (wdtctl_ & 0x10)) pon(0xFFF2);
        if (irq_t[0].read()) pon(0xFFEA);
        if (irq_t[1].read()) pon(0xFFE8);
        if (irq_t[2].read()) pon(0xFFE2);
        if (irq_t[3].read()) pon(0xFFE0);
        if (irq_t[4].read()) pon(0xFFF6);
        if (irq_t[5].read()) pon(0xFFF4);
        if (irq_t[6].read()) pon(0xFFD8);
        if (irq_t[7].read()) pon(0xFFD6);
        if (ua0.irq5()) pon(0xFFF0);
        if (ub0.irq5()) pon(0xFFEE);
        if (ua1.irq5()) pon(0xFFDC);
        if (ub1.irq5()) pon(0xFFDA);
        if (adc.pide()) pon(0xFFEC);
        if (dma.pide_irq()) pon(0xFFE4);
        if (pt_[0].ifg & pt_[0].ie) pon(0xFFDE);
        if (pt_[1].ifg & pt_[1].ie) pon(0xFFD4);
        if (irq_rtc.read()) pon(0xFFD2);
        if (irq_cb.read()) pon(0xFFF8);
        if (usb.pide()) pon(0xFFE6);                                    // USB_UBM
        return f;
    }
    void irq_timers() { nucleo.nota_irq(); dispara_dma_timers(); }
    void avisa_irq() { nucleo.nota_irq(); }
    // DMA: los flancos de CCIFG de CCR0/CCR2 de los Timer [DS «DMA Trigger
    // Assignments»: 1 TA0CCR0, 2 TA0CCR2, 3 TA1CCR0, 4 TA1CCR2, 5 TA2CCR0,
    // 6 TA2CCR2, 7 TB0CCR0, 8 TB0CCR2] (con las IE puestas: la línea del
    // Timer; sin ellas el modelo no lo ve)
    bool t_ant_[8] = {};
    void dispara_dma_timers() {
        static const unsigned TSEL[8] = {1, 0, 3, 0, 7, 0, 5, 0};
        for (unsigned k = 0; k < 8; k += 2) {
            const bool v = irq_t[k].read();
            if (v && !t_ant_[k] && TSEL[k]) dma.dispara(TSEL[k]);
            t_ant_[k] = v;
        }
    }
    bool smclk_pedido_ = false;
    bool pide_smclk() const {
        for (const UsciTi* u : {&ua0, &ub0, &ua1, &ub1}) if (u->fuente_brclk() >= 2 && u->pide_reloj()) return true;
        return false;
    }
    // Las USCI: interrupciones y disparos del DMA (16-23)
    bool u_ant_[8] = {};
    void usci_proc() {
        if (pide_smclk() != smclk_pedido_) relojes();
        UsciTi* u[4] = {&ua0, &ub0, &ua1, &ub1};
        for (unsigned k = 0; k < 4; ++k) {
            const bool rx = u[k]->dma_rx_pide(), tx = u[k]->dma_tx_pide();
            if (rx && !u_ant_[2 * k]) dma.dispara(16 + 2 * k);
            if (tx && !u_ant_[2 * k + 1]) dma.dispara(17 + 2 * k);
            u_ant_[2 * k] = rx; u_ant_[2 * k + 1] = tx;
        }
        avisa_irq();
    }

    // --- Puertos --------------------------------------------------------------------
    struct Puerto { uint8_t in = 0, out = 0, dir = 0, ren = 0, ds = 0, sel = 0, ies = 0, ie = 0, ifg = 0; };
    Puerto pt_[9];                                            // P1-P8 y PJ (8)
    uint8_t pmap_[8] = {};
    bool pmap_llave_ = false;
    uint16_t pmapctl_ = 0x0001;
    // Una línea de una USCI: 0 SIMO/TXD/SDA, 1 SOMI/RXD/SCL, 2 CLK, 3 STE
    struct LineaU { UsciTi* u = nullptr; int l = -1; };
    // La función de periférico del pin i con PxSEL = 1. Devuelve el tipo:
    // 0 ninguna (GPIO), 1 Timer (con su salida y su entrada), 2 USCI, 3
    // analógica, 4 reloj (salida fija)
    // tipo: 1 Timer, 2 USCI, 3 analógico, 4 reloj de salida, 5 CBOUT; `clk`,
    // el TACLK que el pin da como entrada [DS SLAS590P, «Terminal Functions»]
    struct Fn { int tipo = 0; TimerAP4* t = nullptr; unsigned n = 0; LineaU u[2]; TimerAP4* clk = nullptr; };
    void cbout_proc() { ta1.cci_b[1].write(cb.salida.read()); }
    Fn funcion_de(unsigned i) const {
        Fn f;
        const unsigned p = i / 8, b = i % 8;
        auto T = [&](const TimerAP4* t, unsigned n) { f.tipo = 1; f.t = const_cast<TimerAP4*>(t); f.n = n; };
        auto U = [&](const UsciTi* a, int la, const UsciTi* c = nullptr, int lc = -1) {
            f.tipo = 2; f.u[0] = LineaU{const_cast<UsciTi*>(a), la}; if (c) f.u[1] = LineaU{const_cast<UsciTi*>(c), lc};
        };
        switch (p) {
        case 0:                                                       // P1
            if (b >= 1 && b <= 5) T(&ta0, b - 1);
            else if (b == 7) T(&ta1, 0);
            else if (b == 0) { f.tipo = 4; f.clk = const_cast<TimerAP4*>(&ta0); }   // ACLK / TA0CLK
            else if (b == 6) { f.tipo = 5; f.clk = const_cast<TimerAP4*>(&ta1); }   // CBOUT / TA1CLK
            break;
        case 1:                                                       // P2
            if (b == 0) T(&ta1, 1);
            else if (b == 1) T(&ta1, 2);
            else if (b >= 3 && b <= 5) T(&ta2, b - 3);
            else if (b == 2) { f.tipo = 4; f.clk = const_cast<TimerAP4*>(&ta2); }   // SMCLK / TA2CLK
            else if (b == 7) U(&ub0, 3, &ua0, 2);
            break;
        case 2:                                                       // P3
            if (b == 0) U(&ub0, 0);
            else if (b == 1) U(&ub0, 1);
            else if (b == 2) U(&ub0, 2, &ua0, 3);
            else if (b == 3) U(&ua0, 0);
            else if (b == 4) U(&ua0, 1);
            else if (b == 5) T(&tb0, 5);
            else if (b == 6) T(&tb0, 6);
            break;
        case 3: {                                                     // P4: el PMAP
            const unsigned m = pmap_[b];
            if (m >= 4 && m <= 10) T(&tb0, m - 4);
            else if (m == 11) U(&ua1, 1);
            else if (m == 12) U(&ua1, 0);
            else if (m == 13) U(&ua1, 2, &ub1, 3);
            else if (m == 14) U(&ub1, 1);
            else if (m == 15) U(&ub1, 0);
            else if (m == 16) U(&ub1, 2, &ua1, 3);
            else if (m == 31) f.tipo = 3;
            else if (m == 3 || m == 18) f.tipo = 4;
            break;
        }
        case 4:                                                       // P5
            if (b <= 5) f.tipo = 3;                                   // VREF, XT2, XT1
            else T(&tb0, b - 6);
            break;
        case 5: f.tipo = 3; break;                                    // P6: A0-A7
        case 6:                                                       // P7
            if (b <= 3) f.tipo = 3;
            else if (b <= 6) T(&tb0, b - 2);
            else f.tipo = 4;
            break;
        default: break;
        }
        return f;
    }
    static void linea_u(const LineaU& x, bool& oe, bool& o) {
        switch (x.l) {
        case 0: oe = x.u->simo_oe.read(); o = x.u->simo_out.read(); break;
        case 1: oe = x.u->somi_oe.read(); o = x.u->somi_out.read(); break;
        case 2: oe = x.u->clk_oe.read(); o = x.u->clk_out.read(); break;
        case 3: oe = x.u->ste_oe.read(); o = x.u->ste_out.read(); break;
        default: oe = false; o = false; break;
        }
    }
    void pines_salida() {
        const bool jtag4 = pines_dbg && pines_dbg->jtag4_activo();
        for (unsigned i = 0; i < NPIN; ++i) {
            if (i >= 64) {                                            // PJ: GPIO o JTAG
                if (jtag4) { pin[i]->set_hiz(id_pin_[i]); continue; }
            }
            const Puerto& x = pt_[i < 64 ? i / 8 : 8];
            const unsigned b = i < 64 ? i % 8 : i - 64;
            const bool sel = i < 64 && ((x.sel >> b) & 1u), dir = (x.dir >> b) & 1u;
            const float alto = float(vcc);
            if (sel) {
                const Fn f = funcion_de(i);
                if (f.tipo == 1 && dir) { pin[i]->set_drive(id_pin_[i], f.t->out[f.n].read() ? alto : 0.0f, 50.0f); continue; }
                if (f.tipo == 2) {
                    bool oe = false, o = false;
                    for (const LineaU& l : f.u) if (l.u) { bool e2, o2; linea_u(l, e2, o2); if (e2) { oe = true; o = o2; } }
                    const bool i2c = (f.u[0].u && f.u[0].u->en_i2c());
                    if (i2c) { if (oe) pin[i]->set_drive(id_pin_[i], 0.0f, 50.0f); else pin[i]->set_hiz(id_pin_[i]); }
                    else if (oe) pin[i]->set_drive(id_pin_[i], o ? alto : 0.0f, 50.0f);
                    else pin[i]->set_hiz(id_pin_[i]);
                    continue;
                }
                if (f.tipo == 5 && dir) { pin[i]->set_drive(id_pin_[i], cb.salida.read() ? alto : 0.0f, 50.0f); continue; }
                if (f.tipo == 3 || f.tipo == 4 || f.tipo == 5 || !dir) { pin[i]->set_hiz(id_pin_[i]); continue; }
            }
            if (dir) pin[i]->set_drive(id_pin_[i], ((x.out >> b) & 1u) ? alto : 0.0f, 50.0f);
            else if ((x.ren >> b) & 1u) pin[i]->set_drive(id_pin_[i], ((x.out >> b) & 1u) ? alto : 0.0f, 35000.0f);
            else pin[i]->set_hiz(id_pin_[i]);
        }
    }
    void salidas_proc() { pines_salida(); }
    bool nivel(unsigned i) const { return !pin[i]->floating() && pin[i]->voltage() > 0.5f * float(vcc); }
    void entradas_proc() {
        for (unsigned k = 0; k < 9; ++k) {
            Puerto& x = pt_[k];
            uint8_t n = 0;
            const unsigned nb = k < 8 ? 8 : 4;
            for (unsigned b = 0; b < nb; ++b) if (nivel(k < 8 ? k * 8 + b : 64 + b)) n = uint8_t(n | (1u << b));
            const uint8_t cambio = uint8_t(n ^ x.in);
            if (k < 2) {
                const uint8_t sube = uint8_t(cambio & n), baja = uint8_t(cambio & ~n);
                x.ifg = uint8_t(x.ifg | (sube & ~x.ies) | (baja & x.ies));
                if (x.ifg & x.ie) avisa_irq();
            }
            x.in = n;
        }
        // A los módulos: las entradas de las USCI y las de captura de los Timer
        for (unsigned i = 0; i < 64; ++i) {
            const Fn f = funcion_de(i);
            const bool v = nivel(i);
            if (f.tipo == 2) {
                for (const LineaU& l : f.u) {
                    if (!l.u) continue;
                    switch (l.l) {
                    case 0: l.u->simo_in.write(v); break;
                    case 1: l.u->somi_in.write(v); break;
                    case 2: l.u->clk_in.write(v); break;
                    case 3: l.u->ste_in.write(v); break;
                    default: break;
                    }
                }
            } else if (f.tipo == 1 && ((pt_[i / 8].sel >> (i % 8)) & 1u) && !((pt_[i / 8].dir >> (i % 8)) & 1u)) {
                f.t->cci_a[f.n].write(v);
            } else if (f.clk && ((pt_[i / 8].sel >> (i % 8)) & 1u) && !((pt_[i / 8].dir >> (i % 8)) & 1u)) {
                f.clk->taclk.write(v);                                  // TAxCLK
            }
        }
        cb.recalcula();                                              // las tensiones de CB0-CB11
    }
    uint16_t piv(unsigned k, bool dbg) {
        Puerto& x = pt_[k];
        const uint8_t pend = uint8_t(x.ifg & x.ie);
        for (unsigned b = 0; b < 8; ++b)
            if (pend & (1u << b)) { if (!dbg) x.ifg = uint8_t(x.ifg & ~(1u << b)); return uint16_t(2 * (b + 1)); }
        return 0;
    }
    // Un registro de puerto: 0x200 + 0x20·par + desplazamiento (P1/P2 en el par 0; PJ en 0x320)
    bool reg_puerto(uint32_t a, unsigned& k, unsigned& r) const {
        if (a >= 0x200 && a < 0x280) {
            const unsigned par = (a - 0x200) / 0x20, off = (a - 0x200) % 0x20;
            k = 2 * par + (off & 1u); r = off & ~1u;
            return true;
        }
        if (a >= 0x320 && a < 0x340) { k = 8; r = (a - 0x320) & ~1u; return (a & 1u) == 0; }
        return false;
    }
    uint8_t lee_puerto(uint32_t a, bool dbg) {
        unsigned k, r;
        if (!reg_puerto(a, k, r)) return 0;
        const Puerto& x = pt_[k];
        switch (r) {
        case 0x00: return x.in;
        case 0x02: return x.out;
        case 0x04: return x.dir;
        case 0x06: return x.ren;
        case 0x08: return x.ds;
        case 0x0A: return x.sel;
        case 0x0E: case 0x1E: return 0;                               // el IV va por palabras
        case 0x18: return x.ies;
        case 0x1A: return x.ie;
        case 0x1C: return x.ifg;
        default: (void)dbg; return 0;
        }
    }
    void escribe_puerto(uint32_t a, uint8_t v) {
        unsigned k, r;
        if (!reg_puerto(a, k, r)) return;
        Puerto& x = pt_[k];
        switch (r) {
        case 0x02: x.out = v; break;
        case 0x04: x.dir = v; break;
        case 0x06: x.ren = v; break;
        case 0x08: x.ds = v; break;
        case 0x0A: x.sel = v; if (k == 4) relojes(); break;          // P5: los cristales
        case 0x18: x.ies = v; break;
        case 0x1A: x.ie = v; avisa_irq(); break;
        case 0x1C: x.ifg = v; avisa_irq(); break;
        default: break;
        }
        pines_salida();
    }

    // --- El monitor del backchannel (la UCA1 en P4.4/P4.5) ------------------------------
    std::vector<uint8_t> rx_cola_;
    sc_core::sc_event ev_rx_;
    int id_rx_ = -1;
    void uart_mon_proc() {
        if (!monitor_uart) return;
        // Lo que sale por P4.4 (UCA1TXD con el PMAP de reset), a los baudios de
        // la UCA1; y lo que entra por P4.5 (UCA1RXD), del eZ-FET
        id_rx_ = pin[29]->register_driver("ezfet_txd");
        pin[29]->set_drive(id_rx_, float(vcc), 1000.0f);             // el eZ-FET tiene la línea arriba
        sc_core::sc_spawn([this] { mon_tx(); });
        for (;;) {
            while (rx_cola_.empty()) wait(ev_rx_);
            const uint8_t c = rx_cola_.front();
            rx_cola_.erase(rx_cola_.begin());
            const double tb = t_bit_uca1();
            if (tb <= 0) continue;
            auto pon = [&](bool v) { pin[29]->set_drive(id_rx_, v ? float(vcc) : 0.0f, 50.0f); wait(sc_core::sc_time(tb, sc_core::SC_SEC)); };
            pon(false);
            for (unsigned k = 0; k < 8; ++k) pon((c >> k) & 1u);
            pon(true);
            pin[29]->set_drive(id_rx_, float(vcc), 1000.0f);
        }
    }
    double t_bit_uca1() {
        const double b = ua1.baudios();
        return b > 0 ? 1.0 / b : 0.0;
    }
    void mon_tx() {
        for (;;) {
            wait(pin[28]->value_changed_event());
            if (pin[28]->voltage() > 0.5f * float(vcc)) continue;
            const double tb = t_bit_uca1();
            if (tb <= 0) continue;
            wait(sc_core::sc_time(tb * 1.5, sc_core::SC_SEC));
            unsigned c = 0;
            for (unsigned k = 0; k < 8; ++k) {
                if (pin[28]->voltage() > 0.5f * float(vcc)) c |= 1u << k;
                wait(sc_core::sc_time(tb, sc_core::SC_SEC));
            }
            uart_tx.push_back(char(c));
        }
    }

    // --- El bus ---------------------------------------------------------------------
    uint8_t declarado_[0x1000] = {};
    bool avisado_ = false;
    void aviso(uint32_t a) {
        if (avisado_ || a < 0x900) return;
        avisado_ = true;
        std::fprintf(stderr, "[f5529] 0x%04X: registro sin modulo (aviso una vez)\n", unsigned(a));
    }
    // Los registros de 16 bits (las palabras de los módulos de palabra)
    bool lee16(uint32_t a, uint16_t& v, bool dbg) {
        if (a >= 0x340 && a < 0x440) return false;                    // los Timer: aparte
        if ((a >= 0x4A0 && a < 0x4C0) || (a >= 0x8C0 && a < 0x8D0)) return false;
        if (a >= 0x4C0 && a < 0x4F0) { v = mpy.lee16(a - 0x4C0); return true; }
        if (a >= 0x500 && a < 0x540) { v = dma.lee16(a - 0x500, dbg); return true; }
        if (a >= 0x5C0 && a < 0x640) { v = usci_de(a)->lee16_5xx(a & 0x1F, dbg); return true; }
        if (a >= 0x700 && a < 0x740) { v = adc.lee16(a - 0x700, dbg); return true; }
        switch (a) {
        case 0x100: v = sfrie1_; return true;
        case 0x102: v = sfrifg1_; return true;
        case 0x104: v = sfrrpcr_; return true;
        case 0x120: v = uint16_t(0x9600 | (pmmctl0_ & 0xFF)); return true;
        case 0x122: v = pmmctl1_; return true;
        case 0x124: v = svsmhctl_; return true;
        case 0x126: v = svsmlctl_; return true;
        case 0x128: v = svsmio_; return true;
        case 0x12C: v = pmmifg_; return true;
        case 0x12E: v = pmmrie_; return true;
        case 0x130: v = pm5ctl0_; return true;
        case 0x140: v = uint16_t(0x9600 | (fctl1_ & 0xFF)); return true;
        case 0x144: v = uint16_t(0x9600 | (fctl3_ & 0xFF)); return true;
        case 0x146: v = uint16_t(0x9600 | (fctl4_ & 0xFF)); return true;
        case 0x150: v = 0; return true;                               // CRCDI (escritura)
        case 0x152: v = 0; return true;
        case 0x154: v = crc_; return true;                            // CRCINIRES
        case 0x156: v = uint16_t((al_reves8(uint8_t(crc_)) << 8) | al_reves8(uint8_t(crc_ >> 8))); return true;   // CRCRESR
        case 0x158: v = 0x6900; return true;                          // RCCTL0
        case 0x15C: v = uint16_t(0x6900 | (wdtctl_ & 0xFF)); return true;
        case 0x180: v = sysctl_; return true;
        case 0x182: v = sysbslc_; return true;
        case 0x186: v = sysjmbc_; return true;
        // JMBCLR0OFF/JMBCLR1OFF: la lectura no borra la bandera
        case 0x188: v = jmbi_[0]; if (!dbg && !(sysjmbc_ & 0x40)) { sysjmbc_ = uint16_t(sysjmbc_ & ~0x01); } if (!dbg) sfrifg1_ &= uint16_t(~0x40); return true;
        case 0x18A: v = jmbi_[1]; if (!dbg && !(sysjmbc_ & 0x80)) { sysjmbc_ = uint16_t(sysjmbc_ & ~0x02); } return true;
        case 0x18C: v = jmbo_[0]; return true;
        case 0x18E: v = jmbo_[1]; return true;
        case 0x198: v = 0; return true;                               // SYSBERRIV
        case 0x19A: v = lee_univ(dbg); return true;
        case 0x19C: v = 0; return true;                               // SYSSNIV
        case 0x19E: v = lee_rstiv(dbg); return true;
        case 0x1B0: v = uint16_t(refctl0_ | ((refctl0_ & 1) ? 0x0300 : 0)); return true;   // REFGENACT/REFBGACT
        case 0x1C0: v = 0x96A5; return true;                          // PMAPKEYID
        case 0x1C2: v = pmapctl_; return true;
        case 0x20E: v = piv(0, dbg); return true;
        case 0x21E: v = piv(1, dbg); return true;
        default: break;
        }
        if (a >= 0x160 && a < 0x172) {
            const unsigned k = (a - 0x160) / 2;
            v = k == 7 ? ucsctl7_ : k == 0 ? uint16_t(dco_tap() << 8) : ucsctl_[k];
            return true;
        }
        if (a >= 0x1E0 && a < 0x1E8) { v = uint16_t(pmap_[a - 0x1E0] | (pmap_[a - 0x1E0 + 1] << 8)); return true; }
        return false;
    }
    // El grifo del DCO que el FLL ha elegido (UCSCTL0.DCO) [de modelo: lineal en DCORSEL]
    unsigned dco_tap() const {
        const unsigned r = (ucsctl_[1] >> 4) & 7u;
        const double lo = dco_min(r), hi = dco_max(r);
        if (f_dco_ <= lo) return 0;
        if (f_dco_ >= hi) return 31;
        return unsigned(31.0 * std::log(f_dco_ / lo) / std::log(hi / lo));
    }
    void escribe16(uint32_t a, uint16_t v, uint16_t m) {
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        if (a >= 0x4C0 && a < 0x4F0) { mpy.escribe(a - 0x4C0, v, m != 0xFFFF); return; }
        if (a >= 0x500 && a < 0x540) { dma.escribe16(a - 0x500, mz(dma.lee16(a - 0x500, true))); return; }
        if (a >= 0x5C0 && a < 0x640) { usci_de(a)->escribe16_5xx(a & 0x1F, v, m); return; }
        if (a >= 0x700 && a < 0x740) {
            if (m == 0xFFFF) adc.escribe16(a - 0x700, v);
            else adc.escribe8((a - 0x700) | (m == 0xFF00 ? 1u : 0u), uint8_t(m == 0xFF00 ? v >> 8 : v));
            return;
        }
        switch (a) {
        case 0x100: sfrie1_ = mz(sfrie1_); avisa_irq(); return;
        case 0x102: sfrifg1_ = mz(sfrifg1_); actualiza_fallos(); avisa_irq(); return;
        case 0x104: sfrrpcr_ = mz(sfrrpcr_); return;
        case 0x120:
            if (m & 0xFF00) {
                // PMMPW: 0xA5 abre; otra cosa cierra. Con el registro cerrado, nada cambia
                const uint8_t pw = uint8_t(v >> 8);
                pmm_abierto_ = pw == 0xA5;
                if (!pmm_abierto_ && pw != 0x00 && pw != 0x96) { puc(false, 0x20); return; }
            }
            if ((m & 0x00FF) && pmm_abierto_) {
                if (v & 0x04) { puc(false, 0x06); return; }           // PMMSWBOR
                if (v & 0x08) { puc(false, 0x14); return; }           // PMMSWPOR
                const unsigned nivel = v & 3u;
                pmmctl0_ = uint16_t((pmmctl0_ & ~0x10) | (v & 0x10));
                if (nivel != vcore_obj_) {
                    vcore_obj_ = nivel;
                    pmmifg_ = uint16_t(pmmifg_ & ~0x0004);
                    if (nivel < vcore()) { pmmctl0_ = uint16_t((pmmctl0_ & ~3u) | nivel); pmmifg_ |= 0x0004; }
                    else ev_pmm_.notify(sc_core::sc_time(30, sc_core::SC_US));   // subir lleva su rampa
                }
            }
            return;
        case 0x124: if (pmm_abierto_) { svsmhctl_ = mz(svsmhctl_); pmmifg_ = uint16_t(pmmifg_ & ~0x0010); dly_h_ = true; ev_dly_.notify(sc_core::sc_time(20, sc_core::SC_US)); } return;
        case 0x126: if (pmm_abierto_) { svsmlctl_ = mz(svsmlctl_); pmmifg_ = uint16_t(pmmifg_ & ~0x0001); dly_l_ = true; ev_dly_.notify(sc_core::sc_time(20, sc_core::SC_US)); } return;
        case 0x128: if (pmm_abierto_) svsmio_ = mz(svsmio_); return;
        case 0x12C: if (pmm_abierto_ || true) pmmifg_ = mz(pmmifg_); return;
        case 0x12E: if (pmm_abierto_) pmmrie_ = mz(pmmrie_); return;
        case 0x130: pm5ctl0_ = mz(pm5ctl0_); return;
        case 0x140: case 0x144: case 0x146:
            if (m == 0xFFFF && (v >> 8) != 0xA5) { fctl3_ |= 0x02; puc(false, 0x1A); return; }   // KEYV
            if (a == 0x140) fctl1_ = uint16_t(v & 0xE6);
            else if (a == 0x144) {
                if (v & 0x40) fctl3_ ^= 0x40;                         // LOCKA: escribir 1 lo cambia
                fctl3_ = uint16_t((fctl3_ & 0x41) | (v & 0xB6 & ~0x40));
            } else fctl4_ = uint16_t(v & 0xB1);
            avisa_irq();
            return;
        case 0x150: for (unsigned k = 0; k < 2; ++k) if (m & (0xFFu << (8 * k))) crc_byte(uint8_t(v >> (8 * k))); return;
        case 0x152: for (unsigned k = 0; k < 2; ++k) if (m & (0xFFu << (8 * k))) crc_byte(al_reves8(uint8_t(v >> (8 * k)))); return;
        case 0x154: crc_ = mz(crc_); return;
        case 0x158: return;
        case 0x15C:
            if (m == 0xFFFF && (v >> 8) != 0x5A) { sfrifg1_ |= 0x01; puc(false, 0x18); return; }   // WDT: contraseña
            wdtctl_ = uint16_t(v & 0xF7);
            if (v & 0x08) wdt_t0_ = sc_core::sc_time_stamp();
            relojes();
            return;
        case 0x180: sysctl_ = mz(sysctl_); return;
        case 0x182: sysbslc_ = mz(sysbslc_); return;
        // JMBIN0FG/JMBIN1FG se borran escribiendo 0 (con JMBCLRxOFF); las de
        // salida son de solo lectura [UG5 §1.14.4]
        case 0x186: { const uint16_t n = mz(sysjmbc_);
                      sysjmbc_ = uint16_t((sysjmbc_ & 0x0C) | (sysjmbc_ & n & 0x03) | (n & 0xD0)); return; }
        case 0x18C: jmbo_[0] = mz(jmbo_[0]); sysjmbc_ = uint16_t(sysjmbc_ & ~0x04); sfrifg1_ &= uint16_t(~0x80); return;
        case 0x18E: jmbo_[1] = mz(jmbo_[1]); sysjmbc_ = uint16_t(sysjmbc_ & ~0x08); sfrifg1_ &= uint16_t(~0x80); return;
        case 0x1B0: refctl0_ = uint16_t(mz(refctl0_) & 0x00BB); return;
        case 0x1C0: if ((v & m) == (0x2D52 & m)) pmap_llave_ = true; else pmap_llave_ = false; return;
        case 0x1C2: if (pmap_llave_) pmapctl_ = uint16_t((mz(pmapctl_) & 0x0002) | 0x0001); return;
        default: break;
        }
        if (a >= 0x160 && a < 0x172) {
            const unsigned k = (a - 0x160) / 2;
            if (k == 7) { ucsctl7_ = mz(ucsctl7_); actualiza_fallos(); return; }
            ucsctl_[k] = mz(ucsctl_[k]);
            relojes();
            return;
        }
        if (a >= 0x1E0 && a < 0x1E8) {
            // PMAP: con la llave puesta (o PMAPRECFG) [UG5 §13.2]
            if (!pmap_llave_) return;
            const unsigned k = a - 0x1E0;
            if (m & 0x00FF) pmap_[k] = uint8_t(v);
            if (m & 0xFF00) pmap_[k + 1] = uint8_t(v >> 8);
            pines_salida();
            return;
        }
    }
    sc_core::sc_event ev_dly_;
    bool dly_h_ = false, dly_l_ = false;
    // SVSMHDLYIFG y SVSMLDLYIFG: el SVS/SVM se ha asentado tras cambiarlo
    void dly_proc() {
        if (dly_h_) { dly_h_ = false; pmmifg_ |= 0x0010; }
        if (dly_l_) { dly_l_ = false; pmmifg_ |= 0x0001; }
    }
    UsciTi* usci_de(uint32_t a) { return a < 0x5E0 ? &ua0 : a < 0x600 ? &ub0 : a < 0x620 ? &ua1 : &ub1; }

    // Los accesos de byte a los registros de palabra: el byte que toca (5xx: «Libre»)
    uint16_t lee_bus(uint32_t a, bool byte) {
        tlm::tlm_generic_payload gp;
        uint8_t d[2] = {0, 0};
        gp.set_command(tlm::TLM_READ_COMMAND);
        gp.set_address(a);
        gp.set_data_ptr(d);
        gp.set_data_length(byte ? 1 : 2);
        acceso(gp, false);
        return uint16_t(d[0] | (byte ? 0 : d[1] << 8));
    }
    void escribe_bus(uint32_t a, uint16_t v, bool byte) {
        tlm::tlm_generic_payload gp;
        uint8_t d[2] = {uint8_t(v), uint8_t(v >> 8)};
        gp.set_command(tlm::TLM_WRITE_COMMAND);
        gp.set_address(a);
        gp.set_data_ptr(d);
        gp.set_data_length(byte ? 1 : 2);
        acceso(gp, false);
    }

public:
    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint32_t a = uint32_t(gp.get_address()) & 0xFFFFFu;
        const unsigned n = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
        if (a >= 0x900 && a < 0x940) {                                // el USB, por bytes
            usb.t_acceso = sc_core::sc_time_stamp() + nucleo.adelanto();
            for (unsigned i = 0; i < n; ++i) {
                if (gp.is_read()) d[i] = usb.lee8(a + i, dbg);
                else usb.escribe8(a + i, d[i]);
            }
            return;
        }
        const bool rtc_o_cb = (a >= 0x4A0 && a < 0x4C0) || (a >= 0x8C0 && a < 0x8D0);
        if ((a >= 0x340 && a < 0x440) || rtc_o_cb) {                  // los Timer, el RTC_A y el Comp_B
            tlm_utils::simple_initiator_socket<SocF5529>& s = a >= 0x8C0 ? a_cb : a >= 0x4A0 ? a_rtc :
                a < 0x380 ? a_ta0 : a < 0x3C0 ? a_ta1 : a < 0x400 ? a_tb0 : a_ta2;
            sc_core::sc_time t = sc_core::SC_ZERO_TIME;
            if (dbg) s->transport_dbg(gp); else s->b_transport(gp, t);
            return;
        }
        if (a < 0x1000) {
            // 4 bytes (.A): dos palabras
            if (n == 4) {
                for (unsigned k = 0; k < 2; ++k) {
                    tlm::tlm_generic_payload g2;
                    g2.set_command(gp.get_command());
                    g2.set_address(a + 2 * k);
                    g2.set_data_ptr(d + 2 * k);
                    g2.set_data_length(2);
                    acceso(g2, dbg);
                }
                return;
            }
            const uint32_t w = a & ~1u;
            if (gp.is_read()) {
                uint16_t v = 0;
                if (reg_puerto_byte(a, d, n, dbg)) return;
                if (lee16(w, v, dbg)) {
                    if (n == 1) d[0] = uint8_t((a & 1) ? v >> 8 : v);
                    else { d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); }
                    return;
                }
                for (unsigned i = 0; i < n; ++i) d[i] = declarado_[(a + i) & 0xFFF];
                return;
            }
            // escritura
            if (escribe_puerto_byte(a, d, n)) return;
            uint16_t v; uint16_t m;
            if (n == 1) { v = (a & 1) ? uint16_t(d[0] << 8) : d[0]; m = (a & 1) ? 0xFF00 : 0x00FF; }
            else { v = uint16_t(d[0] | (d[1] << 8)); m = 0xFFFF; }
            uint16_t x;
            if (lee16(w, x, true) || (w >= 0x4C0 && w < 0x4F0)) { escribe16(w, v, m); return; }
            for (unsigned i = 0; i < n; ++i) declarado_[(a + i) & 0xFFF] = d[i];
            if (a >= 0x4A0) aviso(a);
            return;
        }
        if (a >= 0x1A00 && a < 0x1B00) {                              // TLV: solo lectura
            if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = mem_[a + i];
            return;
        }
        if (es_flash(a) && gp.is_write() && !dbg) { escribe_flash(a, uint16_t(d[0] | (n >= 2 ? d[1] << 8 : 0)), n == 1);
            if (n == 4) escribe_flash(a + 2, uint16_t(d[2] | (d[3] << 8)), false);
            return; }
        if (a + n <= mem_.size() && (es_ram(a) || es_flash(a))) {
            if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = mem_[a + i];
            else {
                for (unsigned i = 0; i < n; ++i) mem_[a + i] = d[i];
                if (a >= 0x23C8 && a < 0x2400) usb.nota_in();             // un IN de bulk listo
            }
            return;
        }
        if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = 0xFF;   // vacío [de modelo]
    }
private:
    bool reg_puerto_byte(uint32_t a, unsigned char* d, unsigned n, bool dbg) {
        unsigned k, r;
        if (!reg_puerto(a, k, r) || r == 0x0E || r == 0x1E) return false;
        d[0] = lee_puerto(a, dbg);
        if (n == 2) d[1] = lee_puerto(a + 1, dbg);
        return true;
    }
    bool escribe_puerto_byte(uint32_t a, const unsigned char* d, unsigned n) {
        unsigned k, r;
        if (!reg_puerto(a, k, r) || r == 0x0E || r == 0x1E) return false;
        escribe_puerto(a, d[0]);
        if (n == 2) escribe_puerto(a + 1, d[1]);
        return true;
    }
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        M430Ext* x = nullptr;
        gp.get_extension(x);
        acceso(gp, x && x->depurador);
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) { acceso(gp, true); return gp.get_data_length(); }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_F5529_SOC_F5529_H
