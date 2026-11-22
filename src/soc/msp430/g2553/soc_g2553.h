// =============================================================================
// soc_g2553.h — El MSP430G2553 (M3 de doc/analisis_msp430.md, §4 y §6)
//
// El primer chip de la familia, el de la MSP-EXP430G2ET. El mapa, los
// vectores y las direcciones salen del descriptor generado
// (soc/msp430/desc/msp430g2553.h); lo de cada módulo, de [UG2] (SLAU144K) y
// [DS] (SLAS735J):
//
//   0x0000-0x000F SFR (IE1, IE2, IFG1, IFG2)          [UG2 §1.3]
//   0x0010-0x00FF módulos de 8 bits: P1-P3, BCS+, USCI_A0/B0, Comp_A+, ADC10
//                 (AE0, DTC)                          -> Bus8
//   0x0100-0x01FF módulos de 16 bits: WDT+, FLASH2, Timer0_A3, Timer1_A3,
//                 ADC10                               -> Palabra2xx
//   0x0200-0x03FF RAM (512 B)
//   0x0C00-0x0FFF BSL en ROM [declarado: sin el código de TI, se lee 0x3FFF]
//   0x1000-0x10FF información (A-D, 64 B cada uno; A con la TLV de
//                 calibración, protegido por LOCKA)
//   0xC000-0xFFFF Flash principal (vectores en 0xFFE0-0xFFFF)
//
// LO QUE HAY:
//   * BCS+ [UG2 cap. 5]: DCO con RSEL/DCO/MOD y sus constantes de calibrado
//     de 1, 8, 12 y 16 MHz en INFOA, VLO de 12 kHz, LFXT1 de 32768 Hz si se
//     monta el cristal (si no, OFIFG); MCLK, SMCLK y ACLK con sus divisores;
//     CPUOFF/SCG1/OSCOFF del SR los paran;
//   * WDT+ [UG2 cap. 10]: contraseña (0x5A; mal, PUC), los 4 intervalos,
//     modo perro (PUC y WDTIFG) y de intervalo (WDTIFG, vector 0xFFF4); tras
//     el PUC, en marcha con SMCLK/32768;
//   * FLASH2 [UG2 cap. 7]: FCTL1-3 con su contraseña (0xA5), escritura de
//     byte/palabra (solo baja bits a 0), borrado de segmento y masivo, el
//     generador de tiempos (fFTG entre 257 y 476 kHz: fuera, ACCVIFG [de
//     modelo]), BUSY, LOCK, LOCKA y ACCVIFG; la CPU espera mientras;
//   * Timer0_A3 y Timer1_A3: el Timer_A de periph/ti con 3 CCR, TAIV con
//     TAIFG en 0x0A y sin TAxEX0; los IV en 0x012E y 0x011E;
//   * P1-P3 [UG2 cap. 8]: IN/OUT/DIR/REN/SEL/SEL2 y, P1 y P2, IFG/IES/IE;
//     los pines son `AnalogNet`; las funciones de las tablas de [DS]: TA0.1
//     en P1.2/P1.6 (SEL), UCA0RXD/TXD en P1.1/P1.2 (SEL+SEL2);
//   * USCI_A0 en UART [UG2 cap. 15]: UCBRx/UCBRSx/UCBRFx/UCOS16, la trama
//     en el pin TXD bit a bit, y la recepción del pin RXD;
//   * ADC10 [UG2 cap. 22]: una conversión, canales A0-A7 de los pines, 10
//     (sensor de temperatura) y 11 ((VCC-VSS)/2); referencias VCC, 1,5 V y
//     2,5 V; el tiempo de muestreo y las 13 cuentas de ADC10CLK.
//   * NMI: NMIIFG (por RST/NMI no se modela), OFIFG y ACCVIFG con sus IE.
//
// DECLARADO (contesta y avisa): USCI_B0, Comp_A+, el DTC del ADC10, el modo
// SPI/IrDA del USCI_A0 y el BSL de ROM.
// =============================================================================
#ifndef STM32_SOC_MSP430_G2553_SOC_G2553_H
#define STM32_SOC_MSP430_G2553_SOC_G2553_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "../../../m430/cpu/nucleo_m430.h"
#include "../../../m430/debug/pines_m430.h"
#include "../../../periph/ti/timer_a.h"
#include "../../../periph/ti/usci.h"
#include "../../../common/analog_net.h"
#include "../../../pins/pin_mux.h"
#include "../../../common/imagen_fw.h"
#include "../desc/msp430g2553.h"

namespace stm32 { namespace bsl_g2 {
// El BSL B1 del G2553 (verif/fw/g2553/bsl_b1, AGPLv3): su 1 KB de 0x0C00
inline const uint8_t IMAGEN[] = {
#include "bsl_g2.inc"
};
static_assert(sizeof(IMAGEN) == BSL_G2_BYTES && BSL_G2_TAM == 0x400, "bsl_g2.inc mal generado");
} }
namespace stm32 {

inline constexpr M430Caps M430_G2553 {"msp430g2553", M430Arq::CPU, m430::msp430g2553::DESC.errata, false};

SC_MODULE(SocG2553), public m430_irq_if {
    static constexpr const m430::Desc430& DESC = m430::msp430g2553::DESC;

    tlm_utils::simple_target_socket<SocG2553> bus{"bus"};
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_mclk{"hz_mclk"}, hz_smclk{"hz_smclk"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_n{"rst_n"};
    sc_core::sc_signal<bool> clk_dummy{"clk_dummy"}, uno{"uno"};

    NucleoM430 nucleo;
    EemM430 eem{2, 1};
    TapM430 tap{0x89, false, &eem};

    // Los pines: P1.0-P1.7, P2.0-P2.7, P3.0-P3.7 y RST/TEST
    // Los de la placa, si el pin está en un nodo compartido (un <hilo> de un
    // <sistema>, un nodo de bus): `Cableado`; si no, los suyos
    std::vector<std::unique_ptr<AnalogNet>> propios_;
    std::vector<analog_net_if*> pin;
    analog_net_if& test;
    analog_net_if& rst;
    // SBW en TEST/RST y JTAG de 4 hilos en P1.4-P1.7 con TEST arriba
    // (TCK, TMS, TDI/TCLK, TDO) [DS «JTAG pins»]
    std::unique_ptr<PinesM430> pines_dbg;
    double vcc = 3.3;
    double temperatura = 25.0;           // °C, para el sensor del ADC10
    bool cristal_32k = false;            // la G2ET lo trae sin montar
    // El BSL de ROM (M9): B1 el propio, B2 un fichero (`--bsl FICHERO`, el
    // volcado de la ROM de un chip), B3 ninguno (0x3FFF, con el ID en 0x0FF0)
    uint8_t rom_bsl[0x400];
    bool bsl_hay = true;
    std::string bsl_origen = "B1";
    unsigned bsl_entradas = 0;
    void pon_bsl_b1() {
        std::fill(rom_bsl, rom_bsl + 0x400, 0xFF);
        std::copy(bsl_g2::IMAGEN, bsl_g2::IMAGEN + sizeof bsl_g2::IMAGEN, rom_bsl);
        bsl_hay = true; bsl_origen = "B1";
    }
    std::string carga_bsl(const std::string& ruta) {
        for (unsigned i = 0; i < 0x400; ++i) rom_bsl[i] = (i & 1) ? 0x3F : 0xFF;
        rom_bsl[0x3F0] = 0x25; rom_bsl[0x3F1] = 0x53;
        if (ruta == "ninguno") { bsl_hay = false; bsl_origen = "ninguno"; return std::string(); }
        std::vector<TrozoFw> t;
        std::string e;
        switch (formato_fw(ruta)) {
        case FormatoFw::Elf: e = lee_elf(ruta, t, ELF_MSP430); break;
        case FormatoFw::TiTxt: e = lee_titxt(ruta, t); break;
        case FormatoFw::IntelHex: e = lee_ihex(ruta, t); break;
        default: return ruta + ": el BSL va en TI-TXT, Intel HEX o ELF";
        }
        if (!e.empty()) return e;
        size_t n = 0;
        for (const TrozoFw& tr : t)
            for (size_t i = 0; i < tr.datos.size(); ++i)
                if (tr.dir + i >= 0x0C00 && tr.dir + i < 0x1000) { rom_bsl[tr.dir + i - 0x0C00] = tr.datos[i]; ++n; }
        if (!n) return ruta + ": no trae nada en 0x0C00-0x0FFF";
        bsl_hay = true; bsl_origen = ruta;
        return std::string();
    }

    // La ventana llega a +0x2E, donde el modelo (el del MSP432) tiene TAxIV: en
    // el 2xx, TA0IV y TA1IV están en 0x012E y 0x011E y el SoC los reenvía ahí
    TimerAP4 ta0{"timer0_a3", 0x0160, TimerACaps{3, 0x0A, false}, 0x30};
    TimerAP4 ta1{"timer1_a3", 0x0180, TimerACaps{3, 0x0A, false}, 0x30};
    tlm_utils::simple_initiator_socket<SocG2553> a_ta0{"a_ta0"}, a_ta1{"a_ta1"};
    sc_core::sc_signal<bool> irq_ta0_0, irq_ta0_n, irq_ta1_0, irq_ta1_n;
    // USCI_B0: SPI e I2C (la A0 es la UART de abajo) [UG2 cap. 16-17]
    UsciTi ucb0{"usci_b0", true};

    // Lo que sale por la UART (la G2ET lo lleva al eZ-FET: el «backchannel»)
    std::string uart_tx;
    std::function<void(uint8_t)> al_tx_uart;

    SocG2553(sc_core::sc_module_name nm, const Cableado* cab = nullptr)
        : sc_core::sc_module(nm), nucleo("nucleo", M430_G2553),
          test(nodo_alim(cab, "TEST")), rst(nodo_alim(cab, "RST")), mem_(0x10000, 0xFF) {
        for (unsigned i = 0; i < 24; ++i) {
            char n[8]; std::snprintf(n, sizeof n, "P%u_%u", 1 + i / 8, i % 8);
            analog_net_if* x = cab ? cab->busca(i / 8, i % 8) : nullptr;
            if (!x) {
                propios_.emplace_back(new AnalogNet(n));
                n[2] = '.';
                propios_.back()->nombre_esq = n;
                x = propios_.back().get();
            }
            n[2] = '.';
            pin.push_back(x);
            id_pin_[i] = x->register_driver(n);
            x->set_hiz(id_pin_[i]);
        }
        PinesM430::Pines pj{pin[4], pin[5], pin[6], pin[7], &test, &rst};
        pj.jtag4_con_test = true;
        pines_dbg.reset(new PinesM430("pines_dbg", tap, pj));
        tap.tension_test = [this] { return test.floating() ? 0.0 : double(test.voltage()); };   // VPP
        bus.register_b_transport(this, &SocG2553::b_transport);
        bus.register_transport_dbg(this, &SocG2553::transport_dbg);
        nucleo.bus.bind(bus);
        nucleo.clk_hz(hz_mclk);
        nucleo.rst_n(rst_n);
        nucleo.cpu.pon_irq(this);
        nucleo.tap = &tap;
        nucleo.eem = &eem;
        nucleo.perif_hi = 0x200;
        nucleo.cpu.conserva_registros = true;              // [UG2 §2.1.2]: solo PC y SR tienen reset
        // CPU4 está en SLAZ440K y no en devices.csv (CPU_Bugs = 0 en la fila del G2553)
        nucleo.cpu.errata.activas |= m430::ERR_CPU4;
        nucleo.al_parada_dbg = [this](bool) { relojes(); };
        tap.pon_cpu(&nucleo);
        nucleo.al_por = [this](bool v) { if (v) puc(true); else { reset_regs(); rst_n.write(true); } };
        nucleo.cpu.al_cambiar_sr = [this](uint16_t, uint16_t) { relojes(); };
        for (TimerAP4* t : {&ta0, &ta1}) {
            t->clk(clk_dummy); t->rst_n(rst_n); t->clk_en(uno); t->clk_hz(hz_smclk);
            t->modo_byte(Bus16::ModoByte::Palabra2xx);
            t->f_fuente = [this](unsigned s) { return s == 1 ? f_aclk_ : s == 2 ? f_smclk_ : 0.0; };
        }
        a_ta0.bind(ta0.tsk); a_ta1.bind(ta1.tsk);
        ucb0.clk(clk_dummy); ucb0.rst_n(rst_n); ucb0.clk_en(uno); ucb0.clk_hz(hz_smclk);
        ucb0.f_brclk = [this](unsigned s) { return s == 1 ? f_aclk_ : s >= 2 ? f_smclk_ : 0.0; };
        ta0.irq0(irq_ta0_0); ta0.irqn(irq_ta0_n); ta1.irq0(irq_ta1_0); ta1.irqn(irq_ta1_n);
        uno.write(true);
        rst_n.write(false);
        SC_HAS_PROCESS(SocG2553);
        SC_METHOD(irq_timers); sensitive << irq_ta0_0 << irq_ta0_n << irq_ta1_0 << irq_ta1_n; dont_initialize();
        SC_METHOD(salidas_ta); sensitive << ta0.out[0] << ta0.out[1] << ta0.out[2] << ta1.out[0] << ta1.out[1]
                                       << ta1.out[2]; dont_initialize();
        SC_METHOD(usci_irq_proc); sensitive << ucb0.cambio(); dont_initialize();
        SC_METHOD(salidas_usci); sensitive << ucb0.simo_out << ucb0.simo_oe << ucb0.somi_out << ucb0.somi_oe
                                         << ucb0.clk_out << ucb0.clk_oe << ucb0.ste_out << ucb0.ste_oe;
        dont_initialize();
        SC_THREAD(wdt_proc);
        SC_METHOD(puc_fin_proc); sensitive << ev_puc_fin_; dont_initialize();
        SC_THREAD(uart_tx_proc);
        SC_THREAD(uart_rx_proc);
        SC_THREAD(flash_proc);
        SC_THREAD(adc_proc);
        SC_METHOD(test_proc); sensitive << test.value_changed_event(); dont_initialize();   // JTAG de 4 hilos
        SC_METHOD(rst_pin_proc); sensitive << rst.value_changed_event() << ev_rst_pin_; dont_initialize();
        SC_METHOD(bsl_proc); sensitive << ev_bsl_; dont_initialize();
        pon_bsl_b1();
        SC_METHOD(entradas_proc);
        for (auto& p : pin) sensitive << p->value_changed_event();
        sensitive << ev_ca_;
        dont_initialize();
        fabrica();
        reset_regs();
    }

    M430Cpu& cpu() { return nucleo.cpu; }
    static double frecuencia_dco(unsigned rsel, unsigned dco, unsigned mod) { return f_dco_de(rsel, dco, mod); }
    analog_net_if& p(unsigned puerto, unsigned bit) { return *pin[(puerto - 1) * 8 + bit]; }

    // Carga un ELF o un TI-TXT en la Flash y la información (con lo de
    // fábrica en INFOA si el programa no la toca); deja el chip en reset
    std::string carga(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::string e = lee_elf(ruta, t, ELF_MSP430);
        if (!e.empty() && formato_fw(ruta) == FormatoFw::TiTxt) e = lee_titxt(ruta, t);
        if (!e.empty()) return e;
        std::fill(mem_.begin() + 0xC000, mem_.end(), 0xFF);
        std::fill(mem_.begin() + 0x1000, mem_.begin() + 0x10C0, 0xFF);
        for (const TrozoFw& tr : t)
            for (size_t i = 0; i < tr.datos.size(); ++i) {
                const uint32_t a = tr.dir + uint32_t(i);
                if (DESC.principal.contiene(a) || DESC.vectores.contiene(a) || DESC.info.contiene(a)) mem_[a] = tr.datos[i];
                else if (DESC.ram.contiene(a)) mem_[a] = tr.datos[i];
                // fuera: lo que el enlazador mete del propio ELF delante del primer
                // segmento (la cabecera, alineada a página): no hay memoria ahí
            }
        return std::string();
    }
    // El POR (el de la placa): borra también las banderas de reset
    void reset(bool activo) { if (activo) { rst_n.write(false); } else { ifg1_ = 0; reset_regs(); rst_n.write(true); } }
    uint8_t mem8(uint32_t a) const { return a < 0x10000 ? mem_[a] : 0; }
    void poke(uint32_t a, uint8_t v) { if (a < 0x10000) mem_[a] = v; }
    uint16_t mem16(uint32_t a) const { return uint16_t(mem8(a) | (mem8(a + 1) << 8)); }
    double f_mclk() const { return f_mclk_; }
    double f_smclk() const { return f_smclk_; }
    double f_aclk() const { return f_aclk_; }
    double f_dco() const { return f_dco_; }
    uint8_t ifg1() const { return ifg1_; }
    unsigned pucs() const { return n_puc_; }
    // Mete un byte por RXD (a la velocidad que tenga la UART), como el eZ-FET
    void uart_rx(uint8_t b) { rx_cola_.push_back(b); ev_rx_.notify(sc_core::SC_ZERO_TIME); }

    // --- m430_irq_if -------------------------------------------------------------
    uint16_t pendiente(bool gie) override {
        // NMI: OFIFG, ACCVIFG y NMIIFG con su IE, aunque GIE esté a 0 [UG2 §2.2.1.2]
        if (((ifg1_ & 0x02) && (ie1_ & 0x02)) || (acciv_ && (ie1_ & 0x20)) || ((ifg1_ & 0x10) && (ie1_ & 0x10)))
            return 0xFFFC;
        if (!gie) return 0;
        const uint16_t f = fuentes();
        if (!f) return 0;
        unsigned i = 15;
        while (!((f >> i) & 1u)) --i;
        return uint16_t(0xFFE0u + 2 * i);
    }
    void acepta(uint16_t v) override {
        if (v == 0xFFFC) { ie1_ = uint8_t(ie1_ & ~0x32); return; }       // NMIIE, OFIE, ACCVIE a 0
        if (v == 0xFFF4 && (wdtctl_ & 0x10)) ifg1_ = uint8_t(ifg1_ & ~1u);  // WDTIFG, de un origen
        if (v == 0xFFF2) ta0.reconoce_ccr0();
        if (v == 0xFFFA) ta1.reconoce_ccr0();
        if (v == 0xFFEA) adc_ifg_ = false;
    }

private:
    analog_net_if& nodo_alim(const Cableado* cab, const char* n) {
        if (cab) if (analog_net_if* x = cab->busca_alim(n)) return *x;
        propios_.emplace_back(new AnalogNet(n));
        return *propios_.back();
    }
    std::vector<uint8_t> mem_;
    int id_pin_[24];
    uint8_t ie1_ = 0, ifg1_ = 0, ie2_ = 0, ifg2_ = 0;
    // BCS+
    uint8_t dcoctl_ = 0x60, bcsctl1_ = 0x87, bcsctl2_ = 0, bcsctl3_ = 0x05;
    double f_dco_ = 0, f_mclk_ = 0, f_smclk_ = 0, f_aclk_ = 0;
    // WDT+
    uint16_t wdtctl_ = 0x6900;
    sc_core::sc_event ev_wdt_;
    sc_core::sc_time wdt_t0_{sc_core::SC_ZERO_TIME};
    unsigned n_puc_ = 0;
    bool wdt_pausa_ = false;
    sc_core::sc_time wdt_t_pausa_;
    // Puertos (0, 1, 2 = P1, P2, P3)
    struct Puerto { uint8_t in = 0, out = 0, dir = 0, ifg = 0, ies = 0, ie = 0, sel = 0, sel2 = 0, ren = 0; };
    Puerto pt_[3];
    // FLASH2
    uint16_t fctl1_ = 0, fctl2_ = 0x42, fctl3_ = 0x58;   // FCTL3: LOCKA, LOCK y WAIT [UG2 §7.4]
    bool acciv_ = false;
    sc_core::sc_event ev_flash_, ev_flash_corta_;
    struct OpFlash { int tipo = 0; uint32_t a = 0; uint16_t v = 0; bool byte = false; } op_;
    // USCI_A0
    uint8_t uca_[16] = {};            // 0x5D..0x67 por desplazamiento desde 0x5D
    uint8_t txbuf_ = 0, rxbuf_ = 0, ucastat_ = 0;
    bool tx_lleno_ = false;
    sc_core::sc_event ev_tx_, ev_rx_;
    std::vector<uint8_t> rx_cola_;
    // ADC10
    uint16_t adc_ctl0_ = 0, adc_ctl1_ = 0, adc_mem_ = 0;
    uint8_t adc_ae0_ = 0;
    bool adc_ifg_ = false;
    sc_core::sc_event ev_adc_;
    // ADC10: la DTC [UG2 §22.2.7]
    uint8_t adc_dtc0_ = 0, adc_dtc1_ = 0;
    uint16_t adc_sa_ = 0x200;
    unsigned adc_dtc_n_ = 0;            // transferencias hechas en el bloque
    // Comp_A+ [UG2 cap. 21]
    uint8_t cactl1_ = 0, cactl2_ = 0, capd_ = 0;
    bool ca_out_ = false, ca_ifg_ = false;
    sc_core::sc_event ev_ca_;
    // Lo que queda sin máquina (BSL, P3 de 28 pines...)
    uint8_t declarado_[0x100] = {};
    bool avisado_ = false;
    unsigned ultimo_cal_ = 0;

    // --- Fábrica: la TLV de INFOA con las constantes del DCO y del ADC10 -----------
    // RSEL 0..15 y DCO 0..7: f = f0 · S_RSEL^RSEL · S_DCO^DCO [DS: S_RSEL 1,35, S_DCO 1,08;
    // f(RSEL=7, DCO=3) ≈ 0,8-1,5 MHz]. Las calibradas son los (RSEL, DCO, MOD) más cerca.
    static double f_dco_de(unsigned rsel, unsigned dco, unsigned mod) {
        auto f = [](unsigned r, unsigned d) { return 1.4e6 * std::pow(1.35, double(r) - 7.0) * std::pow(1.08, double(d) - 3.0); };
        if (dco >= 7) return f(rsel, 7);
        return (f(rsel, dco) * (32 - mod) + f(rsel, dco + 1) * mod) / 32.0;
    }
    void fabrica() {
        struct C { double f; uint32_t a; };
        const C cal[] = {{16e6, 0x10F8}, {12e6, 0x10FA}, {8e6, 0x10FC}, {1e6, 0x10FE}};
        for (const C& c : cal) {
            double mejor = 1e30; unsigned br = 0, bd = 0, bm = 0;
            for (unsigned r = 0; r < 16; ++r) for (unsigned d = 0; d < 8; ++d) for (unsigned m = 0; m < 32; ++m) {
                const double e = std::fabs(f_dco_de(r, d, m) - c.f);
                if (e < mejor) { mejor = e; br = r; bd = d; bm = m; }
            }
            mem_[c.a] = uint8_t((bd << 5) | bm);              // CALDCO_xMHZ
            mem_[c.a + 1] = uint8_t(0x80 | br);                // CALBC1_xMHZ (XT2OFF)
        }
        // La cabecera de la TLV y las del ADC10 [DS tabla «TLV»]: 1,5 V a 30 y 85 °C
        mem_[0x10DA] = 0x10; mem_[0x10DB] = 0x08;   // TAG_ADC10_1
        mem_[0x10E8] = 0x5E; mem_[0x10E9] = 0x01;   // CAL_ADC_15T30 = 0x015E (aprox., de modelo)
        mem_[0x10EA] = 0xA0; mem_[0x10EB] = 0x01;   // CAL_ADC_15T85
    }

    // --- Reset ---------------------------------------------------------------------
    void reset_regs() {
        ie1_ = ie2_ = ifg2_ = 0;
        ifg1_ = uint8_t((ifg1_ & 0x0D) | 0x04);      // PORIFG (y RSTIFG/WDTIFG si venían)
        dcoctl_ = 0x60; bcsctl1_ = 0x87; bcsctl2_ = 0; bcsctl3_ = 0x05;
        wdtctl_ = 0x6900; wdt_t0_ = sc_core::sc_time_stamp(); wdt_pausa_ = false;
        for (Puerto& x : pt_) { x.out = x.dir = x.ifg = x.ies = x.ie = x.sel = x.sel2 = x.ren = 0; }
        fctl1_ = 0; fctl2_ = 0x42; fctl3_ = 0x58; acciv_ = false;
        for (uint8_t& u : uca_) u = 0;
        uca_[0x61 - 0x5D] = 0x01;                     // UCSWRST
        tx_lleno_ = false; ucastat_ = 0;
        adc_ctl0_ = adc_ctl1_ = adc_mem_ = 0; adc_ae0_ = 0; adc_ifg_ = false;
        adc_dtc0_ = adc_dtc1_ = 0; adc_sa_ = 0x200; adc_dtc_n_ = 0;
        cactl1_ = cactl2_ = capd_ = 0; ca_out_ = false; ca_ifg_ = false;
        if (!cristal_32k) ifg1_ |= 0x02;              // OFIFG: sin cristal
        relojes();
        pines_salida();
        ev_wdt_.notify(sc_core::SC_ZERO_TIME);
    }
    // El PUC: el reset del chip sin el POR (WDT, contraseñas, flash) [UG2 §2.1]
    void puc(bool mantener) {
        ++n_puc_;
        rst_n.write(false);
        if (!mantener) ev_puc_fin_.notify(sc_core::sc_time(2, sc_core::SC_US));
    }
    sc_core::sc_event ev_puc_fin_;
    void puc_fin_proc() { reset_regs(); rst_n.write(true); }
    // RST/NMI: abajo más de 2 µs (t(RST) [DS]) es un reset; sin conducir, la
    // placa lo tiene arriba (47 kΩ en la G2ET). Con SBW, es SBWTDIO y no
    // resetea [JTAG §2.3.1]. NMI (WDTNMI) no se modela.
    sc_core::sc_event ev_rst_pin_;
    bool rst_pin_bajo_ = false;
    // La entrada del BSL [BSL SLAU319 §2.3]: con RST bajo, dos subidas de
    // TEST; RST sube con TEST alto y la CPU sale en el BSL (0x0C00) [de
    // modelo: el arranque de la ROM, de alto nivel]
    unsigned flancos_test_ = 0;
    bool test_alto_ = false;
    sc_core::sc_event ev_bsl_;
    void bsl_proc() {
        nucleo.cpu.pon_reg(0, 0x0C00);
        nucleo.cpu.pon_reg(2, 0);
        ++bsl_entradas;
        nucleo.espera(false);
    }
    void rst_pin_proc() {
        const bool bajo = !rst.floating() && rst.voltage() < 0.5f * float(vcc) && !pines_dbg->rst_es_sbw();
        if (bajo && !rst_pin_bajo_) {
            if (!(ev_rst_pin_.triggered())) { ev_rst_pin_.notify(sc_core::sc_time(2, sc_core::SC_US)); flancos_test_ = 0; return; }
            rst_pin_bajo_ = true;
            rst_n.write(false);
        } else if (!bajo && rst_pin_bajo_) {
            rst_pin_bajo_ = false;
            ifg1_ |= 0x08;                                         // RSTIFG
            reset_regs();
            const bool bsl = bsl_hay && flancos_test_ >= 2 && !test.floating() && test.voltage() > 0.5f * float(vcc);
            flancos_test_ = 0;
            if (bsl) { nucleo.espera(true); ev_bsl_.notify(sc_core::sc_time(1, sc_core::SC_US)); }
            rst_n.write(true);
        }
    }

    // --- Relojes ------------------------------------------------------------------------
    void relojes() {
        const uint16_t sr = nucleo.cpu.sr();
        const unsigned rsel = bcsctl1_ & 0x0F, dco = dcoctl_ >> 5, mod = dcoctl_ & 0x1F;
        f_dco_ = f_dco_de(rsel, dco, mod);
        const unsigned lfx = (bcsctl3_ >> 4) & 3;                         // LFXT1Sx
        double f_lf = lfx == 2 ? 12000.0 : (lfx == 0 && cristal_32k) ? 32768.0 : 0.0;
        if (sr & SR_OSCOFF) f_lf = (lfx == 2) ? f_lf : 0.0;
        const double divm = double(1u << ((bcsctl2_ >> 4) & 3)), divs = double(1u << ((bcsctl2_ >> 1) & 3)),
                     diva = double(1u << ((bcsctl1_ >> 4) & 3));
        const unsigned selm = (bcsctl2_ >> 6) & 3;
        double fm = selm >= 2 ? f_lf : f_dco_;
        if (selm >= 2 && f_lf == 0.0) fm = f_dco_;                       // fallo de LFXT1: el DCO
        f_mclk_ = fm / divm;
        f_smclk_ = (sr & SR_SCG1) ? 0.0 : ((bcsctl2_ & 0x08) ? f_lf : f_dco_) / divs;
        f_aclk_ = f_lf / diva;
        // El control de relojes del EEM: con la CPU en manos de la depuración,
        // los que GENCLKCTRL dice se paran (la herramienta pone 0x26, ACLK y
        // SMCLK: el perro y los Timer_A no corren parados) [UG2 §28.2.4;
        // MSPDS, DefaultClkCntrl]. Sin herramienta que lo escriba, nada
        if (nucleo.parada_dbg()) {
            if (eem.genclkctrl() & EemM430::STOP_SMCLK) f_smclk_ = 0.0;
            if (eem.genclkctrl() & EemM430::STOP_ACLK) f_aclk_ = 0.0;
        }
        // El perro, congelado mientras su reloj no corre (LPM o depuración):
        // su cuenta sigue donde estaba
        {
            const double fw = (wdtctl_ & 0x04) ? f_aclk_ : f_smclk_;
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (fw <= 0.0 && !wdt_pausa_) { wdt_pausa_ = true; wdt_t_pausa_ = ahora; }
            else if (fw > 0.0 && wdt_pausa_) { wdt_pausa_ = false; wdt_t0_ += ahora - wdt_t_pausa_; }
        }
        hz_mclk.write(f_mclk_);
        hz_smclk.write(f_smclk_);
        ta0.recalcula(); ta1.recalcula(); ucb0.recalcula();
        ev_wdt_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Interrupciones -----------------------------------------------------------------
    uint16_t fuentes() const {
        uint16_t f = 0;
        auto pon = [&](uint16_t v) { f = uint16_t(f | (1u << ((v - 0xFFE0u) / 2))); };
        if (pt_[0].ifg & pt_[0].ie) pon(0xFFE4);
        if (pt_[1].ifg & pt_[1].ie) pon(0xFFE6);
        if (adc_ifg_ && (adc_ctl0_ & 0x08)) pon(0xFFEA);
        if ((ifg2_ & 0x02) && (ie2_ & 0x02)) pon(0xFFEC);                 // UCA0TXIFG
        if ((ifg2_ & 0x01) && (ie2_ & 0x01)) pon(0xFFEE);                 // UCA0RXIFG
        // USCI_B0 [UG2 §17.3.7]: en SPI, RX al vector RX y TX al TX; en I2C,
        // RX y TX los dos al vector TX, y las de estado (STAT) al RX
        if (ucb0.ifg_tx() && (ie2_ & 0x08)) pon(0xFFEC);
        if (ucb0.ifg_rx() && (ie2_ & 0x04)) pon(ucb0.en_i2c() ? 0xFFEC : 0xFFEE);
        if (ucb0.i2c_estado_pide()) pon(0xFFEE);
        if (ca_ifg_ && (cactl1_ & 0x02)) pon(0xFFF6);                     // COMPARATORA
        if (irq_ta0_n.read()) pon(0xFFF0);
        if (irq_ta0_0.read()) pon(0xFFF2);
        if ((ifg1_ & 0x01) && (ie1_ & 0x01) && (wdtctl_ & 0x10)) pon(0xFFF4);
        if (irq_ta1_n.read()) pon(0xFFF8);
        if (irq_ta1_0.read()) pon(0xFFFA);
        return f;
    }
    void irq_timers() { nucleo.nota_irq(); }
    void usci_irq_proc() { nucleo.nota_irq(); }
    void salidas_usci() { pines_salida(); }
    void avisa_irq() { nucleo.nota_irq(); }

    // --- Pines --------------------------------------------------------------------------
    // Las funciones de los pines [DS SLAS735J, tablas «Port P1/P2 Pin
    // Functions»]: con PxSEL = 1 y PxSEL2 = 0, el Timer_A (salida con PxDIR = 1,
    // captura con PxDIR = 0), SMCLK en P1.4 y CAOUT en P1.7; con los dos a 1,
    // la USCI (A0: RXD/TXD en P1.1/P1.2; B0: STE, CLK, SOMI/SCL y SIMO/SDA en
    // P1.4-P1.7). CAPD desconecta el buffer de entrada (el pin es analógico).
    // Devuelve: 0 GPIO; 1 salida de función (nivel en `v`); 2 función que no
    // conduce (entrada o drenador abierto suelto); 3 conduce a 0 (I2C).
    int funcion(unsigned i, bool& v) const {
        const Puerto& x = pt_[i / 8];
        const unsigned b = i % 8;
        const bool sel = (x.sel >> b) & 1u, sel2 = (x.sel2 >> b) & 1u, dir = (x.dir >> b) & 1u;
        if (!sel && !sel2) return 0;
        if (i < 8 && sel && sel2) {                                   // la USCI
            switch (b) {
            case 1: return 2;                                         // UCA0RXD
            case 2: v = tx_nivel_; return 1;                          // UCA0TXD
            case 4: if (ucb0.ste_oe.read()) { v = ucb0.ste_out.read(); return 1; } return 2;
            case 5: if (ucb0.clk_oe.read()) { v = ucb0.clk_out.read(); return 1; } return 2;
            case 6: case 7: {
                const bool simo = b == 7;
                const bool oe = simo ? ucb0.simo_oe.read() : ucb0.somi_oe.read();
                const bool o = simo ? ucb0.simo_out.read() : ucb0.somi_out.read();
                if (ucb0.en_i2c()) return oe ? 3 : 2;                 // drenador abierto
                if (oe) { v = o; return 1; }
                return 2;
            }
            default: return 2;
            }
        }
        if (sel && !sel2 && dir) {
            if (i < 8) switch (b) {
                case 1: case 5: v = ta0.out[0].read(); return 1;     // TA0.0
                case 2: case 6: v = ta0.out[1].read(); return 1;     // TA0.1
                case 4: v = false; return 2;                          // SMCLK (no se saca)
                case 7: v = ca_out_; return 1;                        // CAOUT
                default: return 2;
            }
            if (i < 16) switch (b) {
                case 0: case 3: v = ta1.out[0].read(); return 1;     // TA1.0
                case 1: case 2: v = ta1.out[1].read(); return 1;     // TA1.1
                case 4: case 5: v = ta1.out[2].read(); return 1;     // TA1.2
                default: return 2;                                    // XIN/XOUT
            }
        }
        return 2;
    }
    void pines_salida() {
        const bool jtag4 = pines_dbg && pines_dbg->jtag4_activo();
        for (unsigned i = 0; i < 24; ++i) {
            const Puerto& x = pt_[i / 8];
            const unsigned b = i % 8;
            if (jtag4 && i >= 4 && i <= 7) { pin[i]->set_hiz(id_pin_[i]); continue; }   // son del JTAG
            const bool dir = (x.dir >> b) & 1u;
            bool v = false;
            const int fn = funcion(i, v);
            if (fn == 1) pin[i]->set_drive(id_pin_[i], v ? float(vcc) : 0.0f, 50.0f);
            else if (fn == 3) pin[i]->set_drive(id_pin_[i], 0.0f, 50.0f);
            else if (fn == 2) pin[i]->set_hiz(id_pin_[i]);
            else if (dir) pin[i]->set_drive(id_pin_[i], ((x.out >> b) & 1u) ? float(vcc) : 0.0f, 50.0f);
            else if ((x.ren >> b) & 1u)
                pin[i]->set_drive(id_pin_[i], ((x.out >> b) & 1u) ? float(vcc) : 0.0f, 35000.0f);  // 35 kΩ [DS]
            else pin[i]->set_hiz(id_pin_[i]);
        }
    }
    bool tx_nivel_ = true;
    void salidas_ta() { pines_salida(); }
    void test_proc() {
        const bool alto = !test.floating() && test.voltage() > 0.5f * float(vcc);
        if (alto && !test_alto_ && !rst.floating() && rst.voltage() < 0.5f * float(vcc)) ++flancos_test_;
        test_alto_ = alto;
        pines_salida();
    }
    bool nivel(unsigned i) const { return !pin[i]->floating() && pin[i]->voltage() > 0.5f * float(vcc); }
    void entradas_proc() {
        for (unsigned k = 0; k < 3; ++k) {
            Puerto& x = pt_[k];
            uint8_t n = 0;
            for (unsigned b = 0; b < 8; ++b) if (nivel(k * 8 + b)) n = uint8_t(n | (1u << b));
            const uint8_t cambio = uint8_t(n ^ x.in);
            if (k < 2) {
                // IFG: flanco de subida (IES = 0) o de bajada (IES = 1) [UG2 §8.2.6]
                const uint8_t sube = uint8_t(cambio & n), baja = uint8_t(cambio & ~n);
                x.ifg = uint8_t(x.ifg | (sube & ~x.ies) | (baja & x.ies));
                if (x.ifg & x.ie) avisa_irq();
            }
            x.in = n;
        }
        // A los módulos: las entradas de captura, la USCI_B0 y el comparador
        const uint8_t p1 = pt_[0].in, p2 = pt_[1].in;
        auto b = [](uint8_t v, unsigned k) { return ((v >> k) & 1u) != 0; };
        ta0.cci_a[0].write(b(p1, 1)); ta0.cci_a[1].write(b(p1, 2));
        ta0.cci_b[1].write(ca_out_);                                   // CCI1B: CAOUT
        ta1.cci_a[0].write(b(p2, 0)); ta1.cci_b[0].write(b(p2, 3));
        ta1.cci_a[1].write(b(p2, 1)); ta1.cci_b[1].write(b(p2, 2));
        ta1.cci_a[2].write(b(p2, 4)); ta1.cci_b[2].write(b(p2, 5));
        ucb0.ste_in.write(b(p1, 4)); ucb0.clk_in.write(b(p1, 5));
        ucb0.somi_in.write(b(p1, 6)); ucb0.simo_in.write(b(p1, 7));
        comparador();
    }

    // --- Comp_A+ [UG2 cap. 21] -----------------------------------------------------------
    // + : P2CA4/P2CA0 (CA0-CA2 o nada); - : P2CA3..1 (CA1-CA7 o nada); CAREF
    // (0,25·Vcc, 0,5·Vcc o el diodo, ~0,55 V) a la entrada que dice CARSEL;
    // CAEX cambia las entradas e invierte la salida; CAF no filtra (el filtro
    // RC quita rebotes que aquí no hay). CAIFG en el flanco que dice CAIES.
    double ca_pin(unsigned k) const { return pin[k]->floating() ? 0.0 : double(pin[k]->voltage()); }
    void comparador() {
        if (!(cactl1_ & 0x08)) { if (ca_out_) { ca_out_ = false; pines_salida(); } return; }   // CAON
        const unsigned mas = ((cactl2_ >> 2) & 1u) | (((cactl2_ >> 6) & 1u) << 1);           // P2CA4:P2CA0
        const unsigned menos = (cactl2_ >> 3) & 7u;                                          // P2CA3..1
        double vp = mas ? ca_pin(mas - 1) : -1.0, vm = menos ? ca_pin(menos) : -1.0;
        const unsigned ref = (cactl1_ >> 4) & 3u;
        const double vref = ref == 1 ? 0.25 * vcc : ref == 2 ? 0.5 * vcc : ref == 3 ? 0.55 : -1.0;
        const bool rsel = (cactl1_ & 0x40) != 0, ex = (cactl1_ & 0x80) != 0;
        if (vref >= 0.0) { if (rsel != ex) vm = vref; else vp = vref; }
        if (ex) std::swap(vp, vm);
        bool o = vp > vm;
        if (ex) o = !o;
        if (o != ca_out_) {
            const bool sube = o;
            ca_out_ = o;
            if (sube != ((cactl1_ & 0x04) != 0)) { ca_ifg_ = true; cactl1_ |= 0x01; avisa_irq(); }
            ev_ca_.notify(sc_core::SC_ZERO_TIME);          // CCI1B de TA0, desde entradas_proc
            pines_salida();
        }
    }

    // --- WDT+ ---------------------------------------------------------------------------
    sc_core::sc_time wdt_periodo() const {
        static const double div[4] = {32768, 8192, 512, 64};
        const double f = (wdtctl_ & 0x04) ? f_aclk_ : f_smclk_;
        if (f <= 0.0) return sc_core::SC_ZERO_TIME;
        return sc_core::sc_time(div[wdtctl_ & 3] / f, sc_core::SC_SEC);
    }
    void wdt_proc() {
        for (;;) {
            const sc_core::sc_time p = wdt_periodo();
            if ((wdtctl_ & 0x80) || p == sc_core::SC_ZERO_TIME || !rst_n.read()) { wait(ev_wdt_); continue; }
            const sc_core::sc_time fin = wdt_t0_ + p;
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (fin > ahora) { wait(fin - ahora, ev_wdt_); continue; }
            wdt_t0_ = ahora;
            ifg1_ |= 0x01;                                    // WDTIFG
            if (wdtctl_ & 0x10) avisa_irq();                  // intervalo
            else { puc(false); wait(ev_wdt_); }               // perro: PUC
        }
    }

    // --- FLASH2 -------------------------------------------------------------------------
    double f_ftg() const {
        const unsigned s = (fctl2_ >> 6) & 3;
        const double f = s == 0 ? f_aclk_ : s == 1 ? f_mclk_ : f_smclk_;
        return f / double((fctl2_ & 0x3F) + 1);
    }
    bool es_flash(uint32_t a) const { return a >= 0xC000 || (a >= 0x1000 && a < 0x1100); }
    void escribe_flash(uint32_t a, uint16_t v, bool byte) {
        if ((fctl3_ & 0x10) || (fctl3_ & 0x01)) { acceso_mal(); return; }   // LOCK o BUSY
        if (a >= 0x10C0 && a < 0x1100 && (fctl3_ & 0x40)) { acceso_mal(); return; }   // LOCKA
        const double f = f_ftg();
        // Con FSSEL = MCLK bajo JTAG, MCLK es TCLK: no hay frecuencia que mirar
        if (!ftg_de_tclk() && (f < 257e3 || f > 476e3)) { acceso_mal(); return; }
        // ERASE: el segmento; MERAS: la principal; los dos: también la información [UG2 tabla 7-1]
        if (fctl1_ & 0x06) op_ = OpFlash{(fctl1_ & 0x06) == 0x02 ? 1 : (fctl1_ & 0x06) == 0x04 ? 2 : 4, a, v, byte};
        else if (fctl1_ & 0x40) op_ = OpFlash{3, a, v, byte};
        else { acceso_mal(); return; }
        fctl3_ |= 0x01;                                       // BUSY
        ev_flash_.notify(sc_core::SC_ZERO_TIME);
    }
    void acceso_mal() { fctl3_ |= 0x04; acciv_ = true; avisa_irq(); }      // ACCVIFG
    // El generador de tiempos cuenta TCLK: FSSEL = MCLK y la CPU bajo JTAG
    bool ftg_de_tclk() const { return ((fctl2_ >> 6) & 3) == 1 && nucleo.en_tclk(); }
    void flash_proc() {
        for (;;) {
            wait(ev_flash_);
            // [DS «Flash Memory»]: palabra o byte 30 tFTG, segmento 4819, masivo 10593
            const unsigned n = op_.tipo == 3 ? 30 : op_.tipo == 1 ? 4819 : 10593;
            const unsigned div = (fctl2_ & 0x3F) + 1;
            nucleo.espera(true);                             // la CPU espera (se ejecuta de la Flash)
            if (ftg_de_tclk()) {
                // Bajo JTAG: n ciclos del generador = n·div bajadas de TCLK (las
                // TCLKstrobes de [JTAG §2.4.7]); sin ellas, la operación no acaba
                const uint64_t fin = nucleo.ciclos_mclk_jtag() + uint64_t(n) * div;
                while (nucleo.ciclos_mclk_jtag() < fin && nucleo.en_tclk()) wait(nucleo.ev_mclk_jtag() | ev_flash_corta_);
            } else wait(sc_core::sc_time(double(n) / f_ftg(), sc_core::SC_SEC));
            if (op_.tipo == 3) {
                mem_[op_.a] &= uint8_t(op_.v);
                if (!op_.byte) mem_[op_.a + 1] &= uint8_t(op_.v >> 8);
            } else if (op_.tipo == 1) {
                const uint32_t seg = op_.a < 0x1100 ? 64u : 512u;
                const uint32_t b = op_.a & ~(seg - 1);
                if (!(op_.a >= 0x10C0 && op_.a < 0x1100 && (fctl3_ & 0x40)))
                    std::fill(mem_.begin() + b, mem_.begin() + b + seg, 0xFF);
                fctl1_ &= uint16_t(~0x06);
            } else {
                std::fill(mem_.begin() + 0xC000, mem_.end(), 0xFF);
                if (op_.tipo == 4) {                                  // masivo: también la información
                    std::fill(mem_.begin() + 0x1000, mem_.begin() + 0x10C0, 0xFF);
                    if (!(fctl3_ & 0x40)) std::fill(mem_.begin() + 0x10C0, mem_.begin() + 0x1100, 0xFF);
                }
                fctl1_ &= uint16_t(~0x06);
            }
            fctl3_ = uint16_t(fctl3_ & ~0x01) | 0x08;          // BUSY a 0, WAIT a 1
            nucleo.espera(false);
        }
    }

    // --- USCI_A0 (UART) -----------------------------------------------------------------
    uint8_t& ureg(uint32_t a) { return uca_[a - 0x5D]; }
    sc_core::sc_time t_bit() const {
        const uint8_t ctl1 = uca_[0x61 - 0x5D];
        const unsigned ssel = (ctl1 >> 6) & 3;
        const double brclk = ssel == 1 ? f_aclk_ : ssel >= 2 ? f_smclk_ : 0.0;
        const unsigned br = uca_[0x62 - 0x5D] | (uca_[0x63 - 0x5D] << 8);
        const uint8_t mctl = uca_[0x64 - 0x5D];
        if (brclk <= 0.0 || br == 0) return sc_core::SC_ZERO_TIME;
        double div;
        if (mctl & 1) div = 16.0 * (br + ((mctl >> 4) & 0xF) / 16.0);           // UCOS16
        else div = br + ((mctl >> 1) & 7) / 8.0;                                  // UCBRSx
        return sc_core::sc_time(div / brclk, sc_core::SC_SEC);
    }
    bool uart_activa() const { return !(uca_[0x61 - 0x5D] & 0x01); }
    void uart_tx_proc() {
        for (;;) {
            while (!tx_lleno_ || !uart_activa()) wait(ev_tx_);
            const sc_core::sc_time tb = t_bit();
            if (tb == sc_core::SC_ZERO_TIME) { wait(ev_tx_); continue; }
            const uint8_t b = txbuf_;
            tx_lleno_ = false;
            ifg2_ |= 0x02;                                   // UCA0TXIFG: TXBUF vacío
            avisa_irq();
            ucastat_ |= 0x01;                                // UCBUSY
            const bool lsb_primero = !(uca_[0x60 - 0x5D] & 0x20);
            tx_nivel_ = false; pines_salida(); wait(tb);
            for (unsigned i = 0; i < 8; ++i) {
                const unsigned k = lsb_primero ? i : 7 - i;
                tx_nivel_ = (b >> k) & 1u; pines_salida(); wait(tb);
            }
            tx_nivel_ = true; pines_salida(); wait(tb);
            ucastat_ = uint8_t(ucastat_ & ~0x01);
            uart_tx.push_back(char(b));
            if (al_tx_uart) al_tx_uart(b);
        }
    }
    void uart_rx_proc() {
        for (;;) {
            while (rx_cola_.empty() || !uart_activa()) wait(ev_rx_);
            const sc_core::sc_time tb = t_bit();
            if (tb == sc_core::SC_ZERO_TIME) { wait(ev_rx_); continue; }
            const uint8_t b = rx_cola_.front();
            rx_cola_.erase(rx_cola_.begin());
            wait(tb * 10);
            if (ifg2_ & 0x01) ucastat_ |= 0x20;              // UCOE
            rxbuf_ = b;
            ifg2_ |= 0x01;                                   // UCA0RXIFG
            avisa_irq();
        }
    }

    // --- ADC10 --------------------------------------------------------------------------
    double adc_entrada(unsigned ch) {
        if (ch == 10) return 0.00355 * temperatura + 0.986;                     // [DS: VSENSOR]
        if (ch == 11) return vcc / 2.0;
        if (ch < 8) return pin[ch]->floating() ? 0.0 : pin[ch]->voltage();
        return 0.0;
    }
    // Las secuencias (CONSEQx: canal único, secuencia de INCHx a A0, y las dos
    // repetidas; con MSC, las repetidas siguen solas), y la DTC: cada
    // resultado a ADC10SA + 2·n; ADC10IFG al acabar el bloque (en un bloque,
    // ADC10B1 = 0) o cada bloque (dos bloques, ADC10TB, con ADC10B1 diciendo
    // cuál se acaba de llenar); sin ADC10CT, la DTC para tras el primero
    // [UG2 §22.2.6-22.2.7]. El disparo, ADC10SC (SHSx = 0); los del Timer_A se
    // avisan una vez.
    bool adc_busy_ = false, adc_dtc_parada_ = false, adc_avisado_shs_ = false;
    void adc_resultado(uint16_t r) {
        adc_mem_ = r;
        if (adc_dtc1_ && !adc_dtc_parada_) {
            const unsigned bloques = (adc_dtc0_ & 0x08) ? 2u : 1u;
            const uint32_t a = uint32_t(adc_sa_ + 2u * adc_dtc_n_) & 0xFFFFu;
            if (DESC.ram.contiene(a)) { mem_[a] = uint8_t(r); mem_[a + 1] = uint8_t(r >> 8); }
            ++adc_dtc_n_;
            if (adc_dtc_n_ % adc_dtc1_ == 0) {                         // un bloque lleno
                if (bloques == 2) adc_dtc0_ = uint8_t(adc_dtc0_ ^ 0x04);  // ADC10B1
                adc_ifg_ = true;
                if (adc_ctl0_ & 0x08) avisa_irq();
                if (adc_dtc_n_ >= bloques * adc_dtc1_) {
                    adc_dtc_n_ = 0;
                    if (!(adc_dtc0_ & 0x02)) adc_dtc_parada_ = true;     // sin ADC10CT, para
                }
            }
            return;
        }
        adc_ifg_ = true;
        if (adc_ctl0_ & 0x08) avisa_irq();
    }
    void adc_proc() {
        for (;;) {
            wait(ev_adc_);
            if (!(adc_ctl0_ & 0x10) || !(adc_ctl0_ & 0x02)) continue;           // ADC10ON y ENC
            if (((adc_ctl1_ >> 10) & 3) && !adc_avisado_shs_) {
                adc_avisado_shs_ = true;
                std::fprintf(stderr, "[g2553] ADC10: el disparo por el Timer_A (SHSx) no se modela; vale ADC10SC\n");
            }
            adc_busy_ = true;
            adc_dtc_parada_ = false;
            const unsigned conseq = (adc_ctl1_ >> 1) & 3, inch = adc_ctl1_ >> 12;
            unsigned ch = inch;
            for (;;) {
                const unsigned ssel = (adc_ctl1_ >> 3) & 3, div = ((adc_ctl1_ >> 5) & 7) + 1;
                const double fclk = (ssel == 0 ? 5e6 : ssel == 1 ? f_aclk_ : ssel == 2 ? f_mclk_ : f_smclk_) / div;
                static const unsigned sht[4] = {4, 8, 16, 64};
                const unsigned n = sht[(adc_ctl0_ >> 11) & 3] + 13;
                if (fclk > 0) wait(sc_core::sc_time(n / fclk, sc_core::SC_SEC));
                const unsigned sref = (adc_ctl0_ >> 13) & 7;
                const double vref = (sref & 1) ? ((adc_ctl0_ & 0x40) ? 2.5 : 1.5) : vcc;  // REF2_5V
                const double v = adc_entrada(ch);
                long x = std::lround(v / vref * 1023.0);
                if (x < 0) x = 0;
                if (x > 1023) x = 1023;
                adc_ctl0_ = uint16_t(adc_ctl0_ & ~0x0001);                   // ADC10SC se borra solo
                adc_resultado((adc_ctl1_ & 0x0200) ? uint16_t(x << 6) : uint16_t(x));   // ADC10DF
                const bool enc = (adc_ctl0_ & 0x02) && (adc_ctl0_ & 0x10);
                if (conseq == 0) break;
                if (conseq == 1 || conseq == 3) {
                    if (ch == 0) { if (conseq == 1) break; ch = inch; }
                    else --ch;
                }
                if (!enc && conseq != 1) break;
                // Sin MSC, cada conversión espera su disparo (ADC10SC otra vez)
                if (!(adc_ctl0_ & 0x80)) {
                    wait(ev_adc_);
                    if (!(adc_ctl0_ & 0x10)) break;
                }
            }
            adc_busy_ = false;
        }
    }

    // --- El bus ---------------------------------------------------------------------------
    uint8_t lee8(uint32_t a) {
        switch (a) {
        case 0x00: return ie1_;
        case 0x01: return ie2_;
        case 0x02: return uint8_t(ifg1_ | (acciv_ ? 0 : 0));
        case 0x03: return uint8_t((ifg2_ & 0x03) | (ucb0.ifg_rx() ? 0x04 : 0) | (ucb0.ifg_tx() ? 0x08 : 0));
        case 0x48: return adc_dtc0_;
        case 0x49: return adc_dtc1_;
        case 0x59: return cactl1_;
        case 0x5A: return uint8_t((cactl2_ & 0xFE) | (ca_out_ ? 1 : 0));
        case 0x5B: return capd_;
        case 0x68: return ucb0.lee8_2xx(UsciTi::CTL0);
        case 0x69: return ucb0.lee8_2xx(UsciTi::CTL1);
        case 0x6A: return ucb0.lee8_2xx(UsciTi::BR0);
        case 0x6B: return ucb0.lee8_2xx(UsciTi::BR1);
        case 0x6C: return ucb0.lee8_2xx(UsciTi::I2CIE);
        case 0x6D: return ucb0.lee8_2xx(UsciTi::STAT);
        case 0x6E: return ucb0.lee8_2xx(UsciTi::RXBUF);
        case 0x6F: return ucb0.lee8_2xx(UsciTi::TXBUF);
        case 0x53: return bcsctl3_;
        case 0x56: return dcoctl_;
        case 0x57: return bcsctl1_;
        case 0x58: return bcsctl2_;
        case 0x4A: return adc_ae0_;
        case 0x66: { const uint8_t r = rxbuf_; ifg2_ = uint8_t(ifg2_ & ~0x01); ucastat_ = uint8_t(ucastat_ & ~0x20); return r; }
        case 0x67: return txbuf_;
        case 0x65: return ucastat_;
        default: break;
        }
        if (a >= 0x5D && a <= 0x64) return uca_[a - 0x5D];
        if (a >= 0x18 && a <= 0x2F) return lee_puerto(a);
        if (a >= 0x10 && a <= 0x1B) return lee_puerto(a);
        if (a >= 0x41 && a <= 0x43) return pt_[a - 0x41].sel2;
        return declarado(a);
    }
    void escribe8(uint32_t a, uint8_t v) {
        switch (a) {
        case 0x00: ie1_ = v; avisa_irq(); return;
        case 0x01: ie2_ = v; avisa_irq(); return;
        case 0x02: ifg1_ = v; return;
        case 0x03: ifg2_ = uint8_t(v & 0x03); ucb0.pon_ifg(v & 0x04, v & 0x08); avisa_irq(); return;
        case 0x48: adc_dtc0_ = uint8_t(v & 0x0F); return;
        case 0x49: adc_dtc1_ = v; adc_dtc_n_ = 0; return;
        case 0x59: cactl1_ = v; ca_ifg_ = (v & 0x01) != 0; comparador(); avisa_irq(); return;
        case 0x5A: cactl2_ = uint8_t(v & 0xFE); comparador(); return;
        case 0x5B: capd_ = v; pines_salida(); return;
        case 0x68: ucb0.escribe8_2xx(UsciTi::CTL0, v); return;
        case 0x69: ucb0.escribe8_2xx(UsciTi::CTL1, v); return;
        case 0x6A: ucb0.escribe8_2xx(UsciTi::BR0, v); return;
        case 0x6B: ucb0.escribe8_2xx(UsciTi::BR1, v); return;
        case 0x6C: ucb0.escribe8_2xx(UsciTi::I2CIE, v); avisa_irq(); return;
        case 0x6D: ucb0.escribe8_2xx(UsciTi::STAT, v); avisa_irq(); return;
        case 0x6F: ucb0.escribe8_2xx(UsciTi::TXBUF, v); return;
        case 0x53: bcsctl3_ = v; if (!cristal_32k && ((v >> 4) & 3) == 0) ifg1_ |= 0x02; relojes(); return;
        case 0x56: dcoctl_ = v; relojes(); return;
        case 0x57: bcsctl1_ = v; relojes(); return;
        case 0x58: bcsctl2_ = v; relojes(); return;
        case 0x4A: adc_ae0_ = v; return;
        case 0x67:
            txbuf_ = v; tx_lleno_ = true; ifg2_ = uint8_t(ifg2_ & ~0x02); ev_tx_.notify(sc_core::SC_ZERO_TIME); return;
        case 0x61:
            uca_[4] = v;
            if (!(v & 0x01)) { ifg2_ |= 0x02; avisa_irq(); ev_tx_.notify(sc_core::SC_ZERO_TIME); ev_rx_.notify(sc_core::SC_ZERO_TIME); }
            else ifg2_ = uint8_t(ifg2_ & ~0x03);
            return;
        default: break;
        }
        if (a >= 0x5D && a <= 0x64) { uca_[a - 0x5D] = v; return; }
        if ((a >= 0x18 && a <= 0x2F) || (a >= 0x10 && a <= 0x1B)) { escribe_puerto(a, v); return; }
        if (a >= 0x41 && a <= 0x43) { pt_[a - 0x41].sel2 = v; pines_salida(); return; }
        declarado(a) = v;
        aviso(a);
    }
    uint8_t& declarado(uint32_t a) { return declarado_[a & 0xFF]; }
    void aviso(uint32_t a) {
        if (avisado_) return;
        avisado_ = true;
        std::fprintf(stderr, "[g2553] 0x%04X: registro sin módulo (aviso una vez)\n", unsigned(a));
    }
    // P1 0x20-0x27, P2 0x28-0x2F, P3 0x18-0x1B y P3REN 0x10
    Puerto* puerto_de(uint32_t a, unsigned& r) {
        if (a >= 0x20 && a <= 0x2F) { r = (a - 0x20) % 8; return &pt_[(a - 0x20) / 8]; }
        if (a >= 0x18 && a <= 0x1B) { r = a - 0x18; if (r == 3) r = 6; return &pt_[2]; }
        if (a == 0x10) { r = 7; return &pt_[2]; }
        return nullptr;
    }
    uint8_t lee_puerto(uint32_t a) {
        unsigned r = 0;
        Puerto* x = puerto_de(a, r);
        if (!x) return declarado(a);
        switch (r) { case 0: return x->in; case 1: return x->out; case 2: return x->dir; case 3: return x->ifg;
                     case 4: return x->ies; case 5: return x->ie; case 6: return x->sel; default: return x->ren; }
    }
    void escribe_puerto(uint32_t a, uint8_t v) {
        unsigned r = 0;
        Puerto* x = puerto_de(a, r);
        if (!x) { declarado(a) = v; return; }
        switch (r) { case 0: return; case 1: x->out = v; break; case 2: x->dir = v; break;
                     case 3: x->ifg = v; avisa_irq(); return; case 4: x->ies = v; return; case 5: x->ie = v; avisa_irq(); return;
                     case 6: x->sel = v; break; default: x->ren = v; break; }
        pines_salida();
    }
    uint16_t lee16(uint32_t a) {
        switch (a) {
        case 0x0120: return uint16_t(0x6900 | (wdtctl_ & 0xFF));
        case 0x0128: return uint16_t(0x9600 | (fctl1_ & 0xFF));
        case 0x012A: return uint16_t(0x9600 | (fctl2_ & 0xFF));
        case 0x012C: return uint16_t(0x9600 | (fctl3_ & 0xFF));
        case 0x01B0: return adc_ctl0_ | (adc_ifg_ ? 0x0004 : 0);
        case 0x01B2: return uint16_t(adc_ctl1_ | ((adc_busy_ || (adc_ctl0_ & 0x01)) ? 1 : 0));
        case 0x01B4: return adc_mem_;
        case 0x01BC: return adc_sa_;
        case 0x0118: return ucb0.i2coa();
        case 0x011A: return ucb0.i2csa();
        default: return 0;
        }
    }
    void escribe16(uint32_t a, uint16_t v) {
        switch (a) {
        case 0x0120:
            if ((v >> 8) != 0x5A) { ifg1_ |= 0x01; puc(false); return; }   // contraseña mal: PUC y WDTIFG
            wdtctl_ = uint16_t(v & 0xFF);
            if (v & 0x08) wdt_t0_ = sc_core::sc_time_stamp();             // WDTCNTCL
            ev_wdt_.notify(sc_core::SC_ZERO_TIME);
            return;
        case 0x0128: case 0x012A: case 0x012C:
            if ((v >> 8) != 0xA5) { puc(false); return; }                 // [UG2 §7.3.1]
            if (a == 0x0128) fctl1_ = uint16_t(v & 0xC6);
            else if (a == 0x012A) fctl2_ = uint16_t(v & 0xFF);
            else {
                if (v & 0x40) fctl3_ ^= 0x40;                              // LOCKA: escribir 1 lo cambia
                fctl3_ = uint16_t((fctl3_ & 0x41) | (v & 0xBE & ~0x40));
                if (!(fctl3_ & 0x04)) acciv_ = false;
            }
            return;
        case 0x01B0: {
            adc_ctl0_ = uint16_t(v & ~0x0004);
            if (!(v & 0x0004)) adc_ifg_ = false;
            if ((v & 0x03) == 0x03) ev_adc_.notify(sc_core::SC_ZERO_TIME); // ENC | ADC10SC
            if (!(v & 0x02)) adc_dtc_n_ = 0;
            return;
        }
        case 0x01B2: adc_ctl1_ = uint16_t(v & ~1u); return;
        case 0x01BC: adc_sa_ = uint16_t(v & ~1u); adc_dtc_n_ = 0; return;
        case 0x0118: ucb0.pon_i2coa(v); return;
        case 0x011A: ucb0.pon_i2csa(v); return;
        default: return;
        }
    }

    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
        if (a >= 0x0160 && a < 0x01A0) {                       // los Timer_A
            sc_core::sc_time t = sc_core::SC_ZERO_TIME;
            if (dbg) (a < 0x0180 ? a_ta0 : a_ta1)->transport_dbg(gp);
            else (a < 0x0180 ? a_ta0 : a_ta1)->b_transport(gp, t);
            return;
        }
        if (a == 0x012E || a == 0x011E) {                      // TA0IV, TA1IV
            tlm::tlm_generic_payload g2;
            g2.deep_copy_from(gp);
            g2.set_address(a == 0x012E ? 0x018E : 0x01AE);          // base + 0x2E
            g2.set_data_ptr(d);
            sc_core::sc_time t = sc_core::SC_ZERO_TIME;
            if (dbg) (a == 0x012E ? a_ta0 : a_ta1)->transport_dbg(g2);
            else (a == 0x012E ? a_ta0 : a_ta1)->b_transport(g2, t);
            return;
        }
        if (a < 0x0100) {                                      // 8 bits
            if (gp.is_read()) { d[0] = dbg ? peek8(a) : lee8(a); if (n == 2) d[1] = 0; }
            else escribe8(a, d[0]);
            return;
        }
        if (a < 0x0200) {                                      // 16 bits
            const uint32_t w = a & ~1u;
            if (gp.is_read()) {
                const uint16_t v = lee16(w);
                d[0] = (a & 1) ? 0 : uint8_t(v);
                if (n == 2) d[1] = uint8_t(v >> 8);
            } else if (!(a & 1)) escribe16(w, uint16_t(d[0] | (n == 2 ? d[1] << 8 : 0)));
            return;
        }
        if (a >= 0x0C00 && a < 0x1000) {                       // BSL de ROM (M9)
            // El identificador del chip en 0x0FF0 (0x25, 0x53: la «version»
            // 0x5325 de devicedb, que GetDevice del Replicator lee) [DS tabla
            // «Device ID»]: lo lleva la imagen
            if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = rom_bsl[(a + i - 0x0C00) & 0x3FF];
            return;
        }
        if (es_flash(a) && gp.is_write() && !dbg) { escribe_flash(a, uint16_t(d[0] | (n == 2 ? d[1] << 8 : 0)), n == 1); return; }
        if (DESC.ram.contiene(a) || es_flash(a)) {
            if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = mem_[(a + i) & 0xFFFF];
            else for (unsigned i = 0; i < n; ++i) mem_[(a + i) & 0xFFFF] = d[i];
            return;
        }
        if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = 0x3F;   // vacío [de modelo]
    }
    uint8_t peek8(uint32_t a) {
        if (a == 0x66) return rxbuf_;
        if (a == 0x6E) return ucb0.lee8_2xx(UsciTi::RXBUF, true);
        return lee8(a);
    }
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        M430Ext* x = nullptr;
        gp.get_extension(x);
        acceso(gp, x && x->depurador);               // la depuración escribe la Flash sin el controlador
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) { acceso(gp, true); return gp.get_data_length(); }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_G2553_SOC_G2553_H
