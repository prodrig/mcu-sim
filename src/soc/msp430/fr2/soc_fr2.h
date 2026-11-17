// =============================================================================
// soc_fr2.h — LA FAMILIA FR2xx/FR4xx (M6 y M7 de doc/analisis_msp430.md)
//
// La CPUXv2 de src/m430 con lo que una FR2xx/FR4xx le pone alrededor [UG FR2
// SLAU445I], para un chip descrito por un `ChipFr2` (fr2433/chip_fr2433.h,
// fr4133/chip_fr4133.h...): el SoC es uno, lo que cambia de un chip a otro es
// una tabla.
//
//   0x00000-0x00FFF  periféricos
//   0x01000-...      BSL1 (ROM; aquí vacía: 0xFF, M10), del descriptor
//   0x01800-0x019FF  información (FRAM, 512 B; DFWP la protege)
//   0x01A00-0x01AFF  TLV (de fábrica)
//   0x02000-...      RAM (se pierde en LPMx.5), del descriptor
//   ...-0x0FFFF      FRAM principal (PFWP la protege), vectores y firmas en
//                    0xFF80-0xFFFF; y la de arriba de 0x10000 si la hay
//
// LO QUE ES DE TODOS (del capítulo de cada módulo en el UG):
//   SFR; SYS (SYSRSTIV, SYSSNIV, SYSUNIV, el JMB, SYSCFG0 con la protección
//   de la FRAM y su contraseña FRWPPW, SYSCFG1, SYSCFG2 con ADCPCTL y
//   LCDPCTL, SYSCFG3); PMM (PMMCTL0 con PMMREGOFF y los reset por programa,
//   PMMCTL2 con la referencia de 1,5 V y el sensor de temperatura, PMMIFG,
//   PM5CTL0 con LOCKLPM5); el CS (periph/ti/cs_fr.h); el controlador de la
//   FRAM (FRCTL0 con NWAITS y contraseña, GCCTL0/1); CRC16; WDT_A; la
//   memoria de respaldo (BAKMEM, 32 B en 0x660); el ADC (periph/ti/adc_fr.h)
//   y el RTC contador (periph/ti/rtc_fr.h).
// LO QUE DICE LA TABLA: los puertos (cuántos, qué bits, cuáles con
//   interrupción y despertar de LPMx.5), los Timer_A (base, CCR, vectores),
//   las eUSCI, el MPY32 si lo hay, la base del RTC, la función de cada pin con
//   cada PxSEL, las conexiones internas de los Timer y los disparos del ADC,
//   los canales del ADC, el sensor de temperatura, el cristal, el JTAG de 4
//   hilos, el ID de la TLV y el LCD_E (periph/ti/lcd_e.h) si lo hay.
//
// Los vectores salen del descriptor generado por su NOMBRE: la tabla dice
// «TIMER0_A0», el descriptor dice dónde está.
//
// LPMx.5 [UG FR2 §1.4.3]: con PMMREGOFF, LPM3 o LPM4 apagan el núcleo. Si el
// RTC está en marcha con XT1 o VLO, o el LCD_E (FR4xx), es LPM3.5 (siguen, y
// la BAKMEM); si no, LPM4.5. Se pierden la RAM y los registros; los pines se
// quedan como estaban (LOCKLPM5), salvo los del LCD, que siguen con sus
// ondas. Despiertan el RTC (en LPM3.5), un flanco en un puerto con
// interrupción y su IE, o RST: un BOR con SYSRSTIV = 0x08 y PMMLPM5IFG, y la
// bandera del despertar puesta (PxIFG o RTCIFG). Hasta borrar LOCKLPM5 los
// pines siguen quietos.
//
// La depuración: el EEM S (3 disparadores), el TAP de Xv2 de las FRAM (JTAG
// ID 0x98) con el JSTATE (LPMx.5, LPM, la parada del EEM), SBW en TEST/RST y
// JTAG de 4 hilos con TEST arriba. La firma de JTAG de 0xFF80 (0x5555 0x5555:
// bloqueado) [UG FR2 §1.11].
// =============================================================================
#ifndef STM32_SOC_MSP430_FR2_SOC_FR2_H
#define STM32_SOC_MSP430_FR2_SOC_FR2_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <algorithm>
#include <array>
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
#include "../../../periph/ti/eusci_fr.h"
#include "../../../periph/ti/adc_fr.h"
#include "../../../periph/ti/rtc_fr.h"
#include "../../../periph/ti/cs_fr.h"
#include "../../../periph/ti/mpy32.h"
#include "../../../periph/ti/lcd_e.h"
#include "../../../common/analog_net.h"
#include "../../../pins/pin_mux.h"
#include "../../../common/imagen_fw.h"
#include "../desc/desc430.h"

namespace stm32 {

// --- La tabla de un chip -----------------------------------------------------------
// Los pines se numeran puerto × 8 + bit (P1.0 = 0, P2.0 = 8...)
inline constexpr uint8_t fr2_pin(unsigned puerto, unsigned bit) { return uint8_t((puerto - 1) * 8 + bit); }

struct Fr2Timer { const char* nombre; uint32_t base; unsigned ncc; const char* v0; const char* vn; };
struct Fr2Usci { const char* nombre; uint32_t base; bool es_b; const char* vec; };
// Lo que hace un pin con un PxSEL (1, 2 o 3). tipo: 1 el CCR `n` del Timer
// `mod` (salida con DIR, captura sin él), 2 la línea `n` (0 SIMO/TXD/SDA, 1
// SOMI/RXD/SCL, 2 CLK, 3 STE) de la eUSCI `mod`, 3 analógica o cristal, 4 el
// reloj `n` de salida (0 MCLK, 1 SMCLK, 2 ACLK), 5 el TAxCLK del Timer `mod`
struct Fr2Fn { uint8_t pin, sel, tipo, mod, n; };
// Las salidas 0..n-1 del Timer `de` van a las capturas B del Timer `a`
struct Fr2Enlace { uint8_t de, a, n; };
// La salida `ccr` del Timer `timer` dispara el ADC con ADCSHS = `shs`
struct Fr2Disparo { uint8_t timer, ccr, shs; };

struct ChipFr2 {
    const m430::Desc430* desc;
    M430Caps caps;
    const char* etiqueta;                // "fr2433": la de los avisos
    unsigned npuertos;                   // P1..Pn
    uint8_t bits[8];                     // los bits que existen de cada puerto
    unsigned npuertos_irq;               // P1..Pk con interrupciones (y despertar de LPMx.5)
    const Fr2Timer* timers; unsigned ntimers;
    const Fr2Usci* uscis; unsigned nuscis;
    bool mpy32;
    uint32_t rtc_base;
    const Fr2Fn* fn; unsigned nfn;
    const Fr2Enlace* enlaces; unsigned nenlaces;
    const Fr2Disparo* disparos; unsigned ndisparos;
    int8_t adc_pin[16];                  // el pin de cada canal (-1: interno o nada)
    int veref_pin;                       // VeREF+ (-1: no hay)
    double vsensor, t_vsensor, tcsensor; // el sensor: vsensor a t_vsensor °C, y la pendiente
    uint8_t xt1_puerto, xt1_bits;        // XIN/XOUT (PxSEL0 = 1)
    uint8_t jtag[4];                     // TCK, TMS, TDI, TDO
    uint8_t bcl_usci, bcl_tx, bcl_rx;    // el backchannel de la LaunchPad: la eUSCI y sus pines
    uint16_t tlv_id;                     // 0x1A04
    uint8_t tlv_crc_fin;                 // la CRC de la TLV cubre 0x1A04..0x1A00+fin
    uint32_t lcd_base;                   // 0: no hay LCD_E
    const uint8_t* lcd_l; unsigned nlcd; // el pin de cada Lx
    const uint8_t* lcd_pot; unsigned nlcd_pot;   // LCDCAP0/1, R13, R23, R33 (con LCDPCTL de SYSCFG2)
    // SYSCFG0 sin contraseña (FRWPPW «no tiene efecto» en las FR413x y FR203x)
    // [UG FR2 figura 1-35, nota 1]
    bool syscfg0_sin_clave = false;
    // SYS23: la RAM en 0x20FE-0x20FF no se conserva tras un BOR por RST, por
    // PMMSWBOR, por la seguridad o por SVSH [SLAZ550AA]
    bool errata_sys23 = false;
};

SC_MODULE(SocFR2), public m430_irq_if {
    static constexpr unsigned NPIN = 64;

    const ChipFr2& chip;
    const m430::Desc430& desc;

    tlm_utils::simple_target_socket<SocFR2> bus{"bus"};
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_mclk{"hz_mclk"}, hz_smclk{"hz_smclk"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_n{"rst_n"};
    sc_core::sc_signal<bool> clk_dummy{"clk_dummy"}, uno{"uno"};

    NucleoM430 nucleo;
    EemM430 eem;
    TapM430 tap;

    std::vector<std::unique_ptr<AnalogNet>> propios_;
    std::vector<analog_net_if*> pin;          // puerto × 8 + bit (nullptr: no existe)
    analog_net_if& test;
    analog_net_if& rst;
    std::unique_ptr<PinesM430> pines_dbg;
    double vcc = 3.3;
    double temperatura = 25.0;
    bool cristal_xt1 = false;                 // lo pone la placa: si hay un cristal de 32 768 Hz en XIN/XOUT

    std::vector<std::unique_ptr<TimerAP4>> ta;
    std::vector<std::unique_ptr<EusciFr>> usci;
    AdcFr adc{"adc"};
    RtcFr rtc{"rtc"};
    Mpy32 mpy;
    CsFr cs;
    std::unique_ptr<LcdE> lcd;

    // Lo que sale por la eUSCI del backchannel, leído en su TXD por un
    // monitor propio que también mete lo de `uart_rx` por su RXD. En una
    // placa lo hace su PuenteSerie
    std::string uart_tx;
    bool monitor_uart = true;

    SocFR2(sc_core::sc_module_name nm, const ChipFr2& c, const Cableado* cab = nullptr)
        : sc_core::sc_module(nm), chip(c), desc(*c.desc), nucleo("nucleo", c.caps),
          eem(c.desc->disparadores, 4), tap(c.desc->jtag_id, true, &eem),
          test(nodo_alim(cab, "TEST")), rst(nodo_alim(cab, "RST")),
          mem_(std::max<size_t>(0x10000, c.desc->principal2.existe() ? c.desc->principal2.fin + 1 : 0), 0xFF) {
        pin.assign(NPIN, nullptr);
        for (unsigned i = 0; i < NPIN; ++i) {
            if (!existe(i)) continue;
            char n[8];
            std::snprintf(n, sizeof n, "P%u_%u", 1 + i / 8, i % 8);
            analog_net_if* x = cab ? cab->busca(i / 8, i % 8) : nullptr;
            if (!x) {
                propios_.emplace_back(new AnalogNet(n));          // P1_0 (SystemC no admite el punto)
                n[2] = '.';
                propios_.back()->nombre_esq = n;
                x = propios_.back().get();
            }
            n[2] = '.';
            pin[i] = x;
            id_pin_[i] = x->register_driver(n);
            x->set_hiz(id_pin_[i]);
        }
        for (unsigned i = 0; i < NPIN; ++i) { adc_ch_[i] = -1; lcd_l_[i] = -1; pot_[i] = false; }
        for (unsigned ch = 0; ch < 16; ++ch) if (c.adc_pin[ch] >= 0) adc_ch_[unsigned(c.adc_pin[ch])] = int8_t(ch);
        for (unsigned l = 0; l < c.nlcd; ++l) lcd_l_[c.lcd_l[l]] = int8_t(l);
        for (unsigned k = 0; k < c.nlcd_pot; ++k) pot_[c.lcd_pot[k]] = true;
        for (unsigned k = 0; k < c.nfn; ++k) fn_[c.fn[k].pin][c.fn[k].sel & 3u] = c.fn[k];
        // JTAG de 4 hilos (TCK, TMS, TDI, TDO) con TEST arriba; SBW en TEST/RST
        PinesM430::Pines pj{pin[c.jtag[0]], pin[c.jtag[1]], pin[c.jtag[2]], pin[c.jtag[3]], &test, &rst};
        pj.jtag4_con_test = true;
        pines_dbg.reset(new PinesM430("pines_dbg", tap, pj));
        tap.tension_test = [this] { return test.floating() ? 0.0 : double(test.voltage()); };
        bus.register_b_transport(this, &SocFR2::b_transport);
        bus.register_transport_dbg(this, &SocFR2::transport_dbg);
        nucleo.bus.bind(bus);
        nucleo.clk_hz(hz_mclk);
        nucleo.rst_n(rst_n);
        nucleo.cpu.pon_irq(this);
        nucleo.tap = &tap;
        nucleo.eem = &eem;
        nucleo.perif_hi = 0x1000;
        nucleo.cpu.conserva_registros = true;
        // Con el vector borrado arrancaría el BSL [UG FR2 §1.9]; es de M10: el chip se duerme (LPM4)
        nucleo.vector_vacio_apaga = true;
        nucleo.xv2 = true;
        // El TAP de Xv2: el buzón, el BOR, los ID, la firma y el JSTATE
        tap.coreip_id = 0x4020;                // [de modelo: TI no lo publica; distinto de 0]
        tap.device_id_ptr = 0x1A00;
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
        tap.al_bor = [this] { bor(0x02); };
        tap.jstate = [this] { return jstate(); };
        nucleo.al_tce1 = [this](bool) { if (magico_ || arranque_espera_) suelta_arranque(); };
        nucleo.al_parada_dbg = [this](bool) { relojes(); };
        tap.pon_cpu(&nucleo);
        nucleo.al_por = [this](bool v) { if (v) puc(true, 0x00); else { reset_regs(true, false); rst_n.write(true); } };
        nucleo.cpu.al_cambiar_sr = [this](uint16_t, uint16_t) { relojes(); };
        // Los Timer_A
        for (unsigned k = 0; k < c.ntimers; ++k) {
            const Fr2Timer& d = c.timers[k];
            ta.emplace_back(new TimerAP4(d.nombre, d.base, TimerACaps{d.ncc, 0x0E, true}, 0x40));
            a_ta_.emplace_back(new tlm_utils::simple_initiator_socket<SocFR2>(("a_" + std::string(d.nombre)).c_str()));
            irq_t_.emplace_back(new sc_core::sc_signal<bool>(("irq0_" + std::string(d.nombre)).c_str()));
            irq_t_.emplace_back(new sc_core::sc_signal<bool>(("irqn_" + std::string(d.nombre)).c_str()));
            TimerAP4* t = ta.back().get();
            t->clk(clk_dummy); t->rst_n(rst_n); t->clk_en(uno); t->clk_hz(hz_smclk);
            t->f_fuente = [this](unsigned s) { return s == 1 ? f_aclk_ : s == 2 ? f_smclk_ : 0.0; };
            a_ta_.back()->bind(t->tsk);
            t->irq0(*irq_t_[2 * k]); t->irqn(*irq_t_[2 * k + 1]);
        }
        for (unsigned k = 0; k < c.nuscis; ++k) {
            usci.emplace_back(new EusciFr(c.uscis[k].nombre, c.uscis[k].es_b));
            EusciFr* u = usci.back().get();
            u->clk(clk_dummy); u->rst_n(rst_n); u->clk_en(uno); u->clk_hz(hz_smclk);
            u->f_brclk = [this](unsigned s) { return s == 1 ? CsFr::F_MODOSC : s >= 2 ? f_smclk_ : 0.0; };
        }
        adc.v_canal = [this](unsigned ch) { return adc_entrada(ch); };
        adc.v_ref = [this](unsigned sref) { return adc_vref(sref); };
        adc.f_reloj = [this](unsigned s) { return s == 0 ? CsFr::F_MODOSC : s == 1 ? f_aclk_ : f_smclk_; };
        adc.al_cambiar = [this] { avisa_irq(); };
        rtc.f_fuente = [this](unsigned s) { return s == 1 ? f_smclk_ : s == 2 ? (xt1_en_marcha() ? 32768.0 : 0.0) : CsFr::F_VLO; };
        rtc.al_cambiar = [this] { avisa_irq(); };
        rtc.al_desbordar = [this] { adc.dispara(1); if (lpm5_) despierta_lpm5(-1); };
        if (c.lcd_base) {
            lcd.reset(new LcdE("lcd_e"));
            lcd->npines = c.nlcd;
            lcd->f_fuente = [this](unsigned s) { return s == 0 ? (xt1_en_marcha() ? 32768.0 : 0.0) : s == 1 ? f_aclk_ : s == 2 ? CsFr::F_VLO : 0.0; };
            lcd->vcc = [this] { return vcc; };
            lcd->al_cambiar = [this] { avisa_irq(); };
            lcd->al_fase = [this] { pines_lcd(); };
        }
        construye_fuentes();
        uno.write(true);
        rst_n.write(false);
        SC_HAS_PROCESS(SocFR2);
        SC_METHOD(irq_timers);
        for (auto& s : irq_t_) sensitive << *s;
        dont_initialize();
        SC_METHOD(salidas_proc);
        for (unsigned k = 0; k < c.ntimers; ++k)
            for (unsigned n = 0; n < std::max(3u, c.timers[k].ncc) && n < TimerAP4::NCC; ++n) sensitive << ta[k]->out[n];
        for (auto& u : usci)
            sensitive << u->simo_out << u->simo_oe << u->somi_out << u->somi_oe << u->clk_out << u->clk_oe
                      << u->ste_out << u->ste_oe;
        dont_initialize();
        // Las conexiones internas de los Timer y los disparos del ADC
        if (c.nenlaces || c.ndisparos) {
            SC_METHOD(timers_proc);
            for (unsigned k = 0; k < c.nenlaces; ++k)
                for (unsigned n = 0; n < c.enlaces[k].n; ++n) sensitive << ta[c.enlaces[k].de]->out[n];
            for (unsigned k = 0; k < c.ndisparos; ++k) sensitive << ta[c.disparos[k].timer]->out[c.disparos[k].ccr];
            dont_initialize();
        }
        SC_METHOD(usci_proc);
        for (auto& u : usci) sensitive << u->cambio();
        dont_initialize();
        SC_METHOD(entradas_proc);
        for (analog_net_if* p : pin) if (p) sensitive << p->value_changed_event();
        sensitive << ev_entradas_;
        dont_initialize();
        SC_THREAD(wdt_proc);
        SC_METHOD(fll_proc); sensitive << ev_fll_; dont_initialize();
        SC_METHOD(puc_fin_proc); sensitive << ev_puc_fin_; dont_initialize();
        SC_METHOD(bor_fin_proc); sensitive << ev_bor_fin_; dont_initialize();
        SC_METHOD(test_bajo_proc); sensitive << ev_test_bajo_; dont_initialize();
        SC_METHOD(test_proc); sensitive << test.value_changed_event(); dont_initialize();
        SC_METHOD(rst_pin_proc); sensitive << rst.value_changed_event() << ev_rst_pin_; dont_initialize();
        SC_METHOD(lpm5_proc); sensitive << ev_lpm5_; dont_initialize();
        SC_THREAD(uart_mon_proc);
        fabrica();
        reset_regs(true, true);
    }

    M430Cpu& cpu() { return nucleo.cpu; }
    bool existe(unsigned i) const { return i < NPIN && i / 8 < chip.npuertos && ((chip.bits[i / 8] >> (i % 8)) & 1u); }
    analog_net_if& p(unsigned puerto, unsigned bit) { return *pin[(puerto - 1) * 8 + bit]; }
    TimerAP4& timer(unsigned k) { return *ta[k]; }
    EusciFr& eusci(unsigned k) { return *usci[k]; }

    // Carga un ELF o un TI-TXT en la FRAM; deja el chip en reset
    std::string carga(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::string e = lee_elf(ruta, t, ELF_MSP430);
        if (!e.empty() && formato_fw(ruta) == FormatoFw::TiTxt) e = lee_titxt(ruta, t);
        if (!e.empty()) return e;
        std::fill(mem_.begin() + desc.principal.ini, mem_.begin() + 0x10000, 0xFF);
        if (desc.principal2.existe()) std::fill(mem_.begin() + desc.principal2.ini, mem_.begin() + desc.principal2.fin + 1, 0xFF);
        std::fill(mem_.begin() + desc.info.ini, mem_.begin() + desc.info.fin + 1, 0xFF);
        for (const TrozoFw& tr : t)
            for (size_t i = 0; i < tr.datos.size(); ++i) {
                const uint32_t a = tr.dir + uint32_t(i);
                if (es_fram(a) || es_ram(a)) mem_[a] = tr.datos[i];
            }
        return std::string();
    }
    // El reset de la placa (el BOR de encendido)
    void reset(bool activo) {
        if (activo) { rst_n.write(false); lpm5_ = false; }
        else {
            rstiv_.clear(); pon_rstiv(0x02); sysjmbc_ = 0x000C;
            reset_regs(true, true);
            arranque();
            rst_n.write(true);
        }
    }
    bool bloqueado_por_firma() const { return tap.fusible(); }
    uint8_t mem8(uint32_t a) const { return a < mem_.size() ? mem_[a] : 0xFF; }
    uint16_t mem16(uint32_t a) const { return uint16_t(mem8(a) | (mem8(a + 1) << 8)); }
    void poke(uint32_t a, uint8_t v) { if (a < mem_.size()) mem_[a] = v; }
    double f_mclk() const { return cs.f_mclk; }
    double f_smclk() const { return cs.f_smclk; }
    double f_aclk() const { return cs.f_aclk; }
    double f_dco() const { return cs.f_dco; }
    unsigned pucs() const { return n_puc_; }
    // LPMx.5: dentro (y cuál), y cuántas veces se ha entrado
    bool en_lpm5() const { return lpm5_; }
    bool lpm35() const { return lpm5_ && lpm5_rtc_; }
    unsigned veces_lpm5() const { return n_lpm5_; }
    bool pines_bloqueados() const { return pm5ctl0_ & 1u; }
    // La herramienta para la CPU en LPMx.5: la despierta (un BOR) [de modelo:
    // la MSPDS lo hace por JTAG; aquí, como un despertar más]
    void despierta_por_depurador() { if (lpm5_) despierta_lpm5(0); }
    bool avisado_nwaits() const { return avisado_nwaits_; }
    void uart_rx(uint8_t b) { rx_cola_.push_back(b); ev_rx_.notify(sc_core::SC_ZERO_TIME); }
    // La FRAM principal (para el `load` de GDB)
    uint32_t fram_ini() const { return desc.principal.ini; }

    // --- m430_irq_if -------------------------------------------------------------
    uint16_t pendiente(bool gie) override {
        // SYSNMI (0xFFFC): VMA, JMB; UNMI (0xFFFA): NMIIFG y OFIFG con su IE
        if (((sfrifg1_ & 0x08) && (sfrie1_ & 0x08)) || ((sfrifg1_ & 0x40) && (sfrie1_ & 0x40)) ||
            ((sfrifg1_ & 0x80) && (sfrie1_ & 0x80)))
            return 0xFFFC;
        if (((sfrifg1_ & 0x10) && (sfrie1_ & 0x10)) || ((sfrifg1_ & 0x02) && (sfrie1_ & 0x02)))
            return 0xFFFA;
        if (!gie) return 0;
        const uint64_t f = fuentes();
        if (!f) return 0;
        unsigned i = 63;
        while (!((f >> i) & 1u)) --i;
        return uint16_t(0xFF80u + 2 * i);
    }
    void acepta(uint16_t v) override {
        if (v == 0xFFFA || v == 0xFFFC) return;
        for (const Fuente& f : fuentes_) {
            if (f.vec != v) continue;
            if (f.clase == Fuente::WDT && (wdtctl_ & 0x10)) sfrifg1_ = uint16_t(sfrifg1_ & ~1u);   // WDTIFG, de un origen
            if (f.clase == Fuente::TIMER0) ta[f.i]->reconoce_ccr0();
        }
    }

private:
    analog_net_if& nodo_alim(const Cableado* cab, const char* n) {
        if (cab) if (analog_net_if* x = cab->busca_alim(n)) return *x;
        propios_.emplace_back(new AnalogNet(n));
        return *propios_.back();
    }
    std::vector<uint8_t> mem_;
    uint8_t bkmem_[32] = {};
    int id_pin_[NPIN] = {};
    int8_t adc_ch_[NPIN], lcd_l_[NPIN];
    bool pot_[NPIN];
    Fr2Fn fn_[NPIN][4] = {};
    unsigned n_puc_ = 0;
    std::vector<std::unique_ptr<tlm_utils::simple_initiator_socket<SocFR2>>> a_ta_;
    std::vector<std::unique_ptr<sc_core::sc_signal<bool>>> irq_t_;   // 0 y n de cada Timer

    bool es_ram(uint32_t a) const { return desc.ram.contiene(a); }
    bool es_fram_principal(uint32_t a) const { return (a >= desc.principal.ini && a < 0x10000) || desc.principal2.contiene(a); }
    bool es_info(uint32_t a) const { return desc.info.contiene(a); }
    bool es_fram(uint32_t a) const { return es_fram_principal(a) || es_info(a); }
    bool es_bsl(uint32_t a) const { return desc.bsl.contiene(a); }

    // --- SFR y SYS ------------------------------------------------------------------
    uint16_t sfrie1_ = 0, sfrifg1_ = 0, sfrrpcr_ = 0x1C;
    uint16_t sysctl_ = 0, sysbslc_ = 0, sysjmbc_ = 0x000C, jmbi_[2] = {}, jmbo_[2] = {};
    uint16_t syscfg0_ = 0x0003, syscfg1_ = 0, syscfg2_ = 0, syscfg3_ = 0;
    std::vector<uint16_t> rstiv_;
    void pon_rstiv(uint16_t v) { if (v && std::find(rstiv_.begin(), rstiv_.end(), v) == rstiv_.end()) rstiv_.push_back(v); }
    uint16_t lee_rstiv(bool dbg) {
        if (rstiv_.empty()) return 0;
        auto it = std::min_element(rstiv_.begin(), rstiv_.end());
        const uint16_t v = *it;
        if (!dbg) {
            rstiv_.erase(it);
            // y la bandera del PMM que le toca [UG FR2 tabla 2-6]
            if (v == 0x04) pmmifg_ = uint16_t(pmmifg_ & ~0x0200);
            if (v == 0x06) pmmifg_ = uint16_t(pmmifg_ & ~0x0100);
            if (v == 0x08) pmmifg_ = uint16_t(pmmifg_ & ~0x8000);
            if (v == 0x0E) pmmifg_ = uint16_t(pmmifg_ & ~0x2000);
            if (v == 0x14) pmmifg_ = uint16_t(pmmifg_ & ~0x0400);
        }
        return v;
    }
    // SYSUNIV: 2 NMIIFG, 4 OFIFG; SYSSNIV: 0x12 VMA, 0x14 JMBIN, 0x16 JMBOUT [DS: SYS]
    uint16_t lee_univ(bool dbg) {
        if (sfrifg1_ & 0x10) { if (!dbg) sfrifg1_ = uint16_t(sfrifg1_ & ~0x10); return 2; }
        if (sfrifg1_ & 0x02) return 4;                                  // OFIFG: la borra el programa
        return 0;
    }
    uint16_t lee_sniv(bool dbg) {
        if (sfrifg1_ & 0x08) { if (!dbg) sfrifg1_ = uint16_t(sfrifg1_ & ~0x08); return 0x12; }
        if (sfrifg1_ & 0x40) { if (!dbg) sfrifg1_ = uint16_t(sfrifg1_ & ~0x40); return 0x14; }
        if (sfrifg1_ & 0x80) { if (!dbg) sfrifg1_ = uint16_t(sfrifg1_ & ~0x80); return 0x16; }
        return 0;
    }
    // La protección de la FRAM [UG FR2 §1.16.2.1]
    bool fram_protegida(uint32_t a) const {
        if (es_fram_principal(a)) return syscfg0_ & 0x01;           // PFWP
        if (es_info(a)) return syscfg0_ & 0x02;                     // DFWP
        return true;
    }

    // --- PMM ---------------------------------------------------------------------
    uint16_t pmmctl0_ = 0x0040, pmmctl2_ = 0, pmmifg_ = 0, pm5ctl0_ = 0x0011;
    bool pmm_abierto_ = false;
    // La referencia de 1,5 V: con INTREFEN (o el ADC que la pide con ADCSREF = 1)
    bool vref_on() const { return pmmctl2_ & 0x0001; }

    // --- CS ------------------------------------------------------------------------
    sc_core::sc_event ev_fll_;
    bool xt1_sel() const { return (pt_[chip.xt1_puerto].sel0 & chip.xt1_bits) == chip.xt1_bits; }
    bool xt1_pines() const { return xt1_sel() || !(cs.lee(6) & 0x0001); }
    // El cristal oscila: con sus pines (o sin XT1AUTOOFF), y también en
    // LPMx.5 y con LOCKLPM5 tras su despertar si oscilaba al entrar [UG FR2
    // §1.4.3: XT1 sigue para el RTC y el LCD en LPM3.5; de modelo: la
    // configuración de los pines se conserva hasta LOCKLPM5]
    bool xt1_retenido_ = false;
    bool xt1_en_marcha() const { return cristal_xt1 && (xt1_pines() || xt1_retenido_); }
    CsFr::Entradas entradas_cs() const {
        CsFr::Entradas e;
        const uint16_t sr = nucleo.cpu.sr();
        e.cpuoff = sr & 0x0010; e.oscoff = sr & 0x0020; e.scg0 = sr & 0x0040; e.scg1 = sr & 0x0080;
        e.xt1 = cristal_xt1 && (xt1_sel() || xt1_retenido_);
        e.pide_smclk = smclk_pedido_;
        e.pide_aclk = aclk_pedido_;
        return e;
    }
    void relojes() {
        aclk_pedido_ = pide_aclk();
        cs.calcula(entradas_cs());
        if (cs.fin_desenganche() > sc_core::sc_time_stamp())
            ev_fll_.notify(cs.fin_desenganche() - sc_core::sc_time_stamp());
        if (cs.fllulpuc()) { puc(false, 0x24); return; }               // FLL unlock (PUC)
        // El control de relojes del EEM con la CPU en manos del depurador
        // (GENCLKCTRL: el perro, los Timer, la eUSCI y el ADC, parados con ella)
        f_smclk_ = cs.f_smclk; f_aclk_ = cs.f_aclk;
        if (nucleo.parada_dbg()) {
            if (eem.genclkctrl() & EemM430::STOP_SMCLK) f_smclk_ = 0.0;
            if (eem.genclkctrl() & EemM430::STOP_ACLK) f_aclk_ = 0.0;
        }
        const double f_smclk = f_smclk_;
        if (cs.falta()) sfrifg1_ |= 0x02;                             // OFIFG
        if (cs.f_mclk > 8.1e6 && (frctl0_ & 0x70) == 0 && !avisado_nwaits_) {
            avisado_nwaits_ = true;                                   // una vez por simulación
            std::fprintf(stderr, "[%s] MCLK %.2f MHz sin estados de espera de la FRAM (NWAITS = 0; > 8 MHz pide 1) [DS]\n",
                         chip.etiqueta, cs.f_mclk / 1e6);
        }
        {
            const double fw = wdt_f();
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (fw <= 0.0 && !wdt_pausa_) { wdt_pausa_ = true; wdt_t_pausa_ = ahora; }
            else if (fw > 0.0 && wdt_pausa_) { wdt_pausa_ = false; wdt_t0_ += ahora - wdt_t_pausa_; }
        }
        hz_mclk.write(cs.f_mclk);
        hz_smclk.write(f_smclk);
        for (auto& t : ta) t->recalcula();
        for (auto& u : usci) u->recalcula();
        rtc.recalcula();
        if (lcd) lcd->recalcula();
        ev_wdt_.notify(sc_core::SC_ZERO_TIME);
        mira_lpm5();
    }
    void fll_proc() { relojes(); avisa_irq(); }
    double f_smclk_ = 0, f_aclk_ = 0;          // SMCLK y ACLK para los módulos (con GENCLKCTRL)

    // --- FRCTL ---------------------------------------------------------------------
    uint16_t frctl0_ = 0, gcctl0_ = 0x0006, gcctl1_ = 0;
    bool frctl_abierto_ = false, avisado_nwaits_ = false;

    // --- WDT_A ---------------------------------------------------------------------
    uint16_t wdtctl_ = 0x0004;
    sc_core::sc_event ev_wdt_;
    sc_core::sc_time wdt_t0_{sc_core::SC_ZERO_TIME}, wdt_t_pausa_;
    bool wdt_pausa_ = false;
    double wdt_f() const {
        const unsigned s = (wdtctl_ >> 5) & 3u;
        return s == 0 ? f_smclk_ : s == 1 ? f_aclk_ : s == 2 ? CsFr::F_VLO : 0.0;
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

    // --- El ADC: canales y referencias [DS: ADC Channel Connections] ---------------
    double v_sensor(double t) const { return chip.vsensor + chip.tcsensor * (t - chip.t_vsensor); }
    double adc_vref(unsigned sref) const {
        switch (sref & 7u) {
        case 1: case 5: return vref_on() ? 1.5 : 0.0;                // VREF (1,5 V, con INTREFEN)
        case 2: case 3: case 6: case 7: {                            // VeREF+
            if (chip.veref_pin < 0) return vcc;
            const analog_net_if* x = pin[unsigned(chip.veref_pin)];
            return x->floating() ? 0.0 : double(x->voltage());
        }
        default: return vcc;
        }
    }
    double adc_entrada(unsigned ch) const {
        if (ch < 16 && chip.adc_pin[ch] >= 0) {
            const analog_net_if* x = pin[unsigned(chip.adc_pin[ch])];
            return ((syscfg2_ >> ch) & 1u) && !x->floating() ? double(x->voltage()) : 0.0;
        }
        if (ch == 12) return (pmmctl2_ & 0x0008) ? v_sensor(temperatura) : 0.0;   // [DS: VSENSOR, TCSENSOR]
        if (ch == 13) return vref_on() ? 1.5 : 0.0;
        if (ch == 15) return vcc;
        return 0.0;
    }

    // --- TLV de fábrica [DS: Device Descriptors] ---------------------------------------
    void fabrica() {
        uint8_t* t = &mem_[0x1A00];
        std::fill(t, t + 0x100, 0xFF);
        auto w = [&](unsigned o, uint16_t v) { t[o] = uint8_t(v); t[o + 1] = uint8_t(v >> 8); };
        t[0x00] = 0x06; t[0x01] = 0x06;                               // longitudes
        w(0x04, chip.tlv_id);                                         // el ID
        t[0x06] = 0x10; t[0x07] = 0x10;                               // revisiones de HW y FW [de modelo]
        t[0x08] = 0x08; t[0x09] = 0x0A;                               // el registro del dado
        for (unsigned i = 0; i < 10; ++i) t[0x0A + i] = uint8_t(0x30 + i);
        t[0x14] = 0x11; t[0x15] = 0x08;                               // la calibración del ADC
        w(0x16, 0x8000); w(0x18, 0x0000);                             // ganancia 1, sin desplazamiento
        // El sensor, del modelo: 30 y 85 °C con la referencia de 1,5 V, 10 bits
        w(0x1A, uint16_t(std::lround(v_sensor(30.0) / 1.5 * 1023.0)));
        w(0x1C, uint16_t(std::lround(v_sensor(85.0) / 1.5 * 1023.0)));
        t[0x1E] = 0x12; t[0x1F] = 0x04;                               // REF y DCO
        w(0x20, 0x8000);                                              // factor de la referencia: 1
        w(0x22, 256);                                                 // el grifo de 16 MHz a 30 °C [de modelo]
        // CRC-CCITT de 0x1A04 al final que dice la hoja de datos
        uint16_t c = 0xFFFF;
        for (unsigned i = 0x04; i <= chip.tlv_crc_fin; ++i) {
            const uint8_t b = t[i];
            for (unsigned k = 0; k < 8; ++k) {
                const bool fb = ((c >> 15) & 1u) ^ ((b >> (7 - k)) & 1u);
                c = uint16_t(c << 1);
                if (fb) c ^= 0x1021;
            }
        }
        w(0x02, c);
    }

    // --- Reset ---------------------------------------------------------------------
    // `bor`: un BOR (todo); `rtc`: también el dominio de LPM3.5 (RTC, LCD_E y
    // BAKMEM), que solo pierde un BOR de verdad, no el despertar de LPMx.5
    void reset_regs(bool bor, bool rtc_tambien) {
        sfrie1_ = 0;
        sfrifg1_ = uint16_t(sfrifg1_ & 0x11);
        if (bor) {
            sysbslc_ = 0; pmmctl0_ = 0x0040; pmmctl2_ = 0; syscfg0_ = 0x0003; syscfg1_ = syscfg2_ = syscfg3_ = 0;
            pm5ctl0_ = 0x0011;
            frctl0_ = 0; gcctl0_ = 0x0006; gcctl1_ = 0;
            if (rtc_tambien) { rtc.reset(); if (lcd) lcd->reset(); std::fill(bkmem_, bkmem_ + 32, 0); xt1_retenido_ = false; }
        }
        pmm_abierto_ = false; frctl_abierto_ = false;
        sysctl_ = 0;
        if (bor) sysjmbc_ = uint16_t(sysjmbc_ & 0x0F);
        wdtctl_ = 0x0004; wdt_t0_ = sc_core::sc_time_stamp(); wdt_pausa_ = false;
        cs.reset();
        for (Puerto& x : pt_) { x.out = x.dir = x.ifg = x.ies = x.ie = x.sel0 = x.sel1 = x.ren = 0; }
        crc_ = 0xFFFF;
        mpy.reset();
        adc.reset();
        relojes();
        pines_salida();
        ev_wdt_.notify(sc_core::SC_ZERO_TIME);
    }
    sc_core::sc_event ev_puc_fin_;
    void puc(bool mantener, uint16_t causa) {
        ++n_puc_;
        pon_rstiv(causa);
        rst_n.write(false);
        if (!mantener) ev_puc_fin_.notify(sc_core::sc_time(2, sc_core::SC_US));
    }
    void puc_fin_proc() { reset_regs(false, false); rst_n.write(true); }
    // Un BOR (por JTAG, por programa, el pin RST): con su causa en SYSRSTIV
    sc_core::sc_event ev_bor_fin_;
    void bor(uint16_t causa) {
        rst_n.write(false);
        pon_rstiv(causa);
        if (causa == 0x06) sys23();
        const bool de_lpm5 = lpm5_;
        if (lpm5_) sal_lpm5();
        reset_regs(true, !de_lpm5);
        ev_bor_fin_.notify(sc_core::sc_time(2, sc_core::SC_US));
    }
    void bor_fin_proc() { arranque(); rst_n.write(true); }
    // [SLAZ550AA SYS23] Lo que queda en 0x20FE-0x20FF tras ese BOR [de
    // modelo: TI no dice qué; aquí, 0x00]
    unsigned n_sys23_ = 0;
    void sys23() {
        if (!chip.errata_sys23) return;
        mem_[0x20FE] = mem_[0x20FF] = 0x00;
        if (!n_sys23_++) std::fprintf(stderr, "[%s] SYS23: el BOR no conserva la RAM en 0x20FE-0x20FF [SLAZ550AA]\n", chip.etiqueta);
    }

    // El arranque: la firma de JTAG [UG FR2 §1.11: 0xFF80 = 0x5555 y 0xFF82 =
    // 0x5555 la bloquea] y el patrón mágico por el JMB (como en 5xx)
    bool magico_ = false, arranque_espera_ = false;
    void arranque() {
        tap.pon_bloqueo(mem16(0xFF80) == 0x5555 && mem16(0xFF82) == 0x5555);
        if (tap.fusible()) return;
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
            nucleo.espera(true);
        }
    }
    void suelta_arranque() { magico_ = arranque_espera_ = false; nucleo.espera(false); }
    sc_core::sc_event ev_test_bajo_;
    void test_bajo_proc() {
        if (arranque_espera_ && !magico_ && (test.floating() || test.voltage() < 0.5f * float(vcc))) suelta_arranque();
    }
    void test_proc() {
        pines_salida();
        if (arranque_espera_ && (test.floating() || test.voltage() < 0.5f * float(vcc)))
            ev_test_bajo_.notify(sc_core::sc_time(150, sc_core::SC_US));
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
            pon_rstiv(0x04);                                          // RST/NMI (BOR)
            sys23();
            const bool de_lpm5 = lpm5_;
            if (lpm5_) { pon_rstiv(0x08); sal_lpm5(); }
            reset_regs(true, !de_lpm5);
            pmmifg_ |= 0x0200;                                        // PMMRSTIFG
            if (de_lpm5) pmmifg_ |= 0x8000;
            arranque();
            rst_n.write(true);
        }
    }

    // --- LPMx.5 [UG FR2 §1.4.3] ------------------------------------------------------
    bool lpm5_ = false, lpm5_rtc_ = false;
    unsigned n_lpm5_ = 0;
    sc_core::sc_event ev_lpm5_;
    struct Retenido { bool hiz = true; bool alto = false; double r = 50.0; double v = -1.0; };
    Retenido retenido_[NPIN];
    // Lo que pide LPMx.5: PMMREGOFF y LPM3/LPM4 (CPUOFF, SCG0, SCG1), sin el
    // perro en modo perro; la CPU suelta y fuera de un PUC
    void mira_lpm5() {
        if (lpm5_ || !rst_n.read() || nucleo.en_tclk()) return;
        const uint16_t sr = nucleo.cpu.sr();
        if (!(pmmctl0_ & 0x0010) || (sr & 0x00D0) != 0x00D0) return;
        if (!(wdtctl_ & 0x80) && !(wdtctl_ & 0x10)) return;          // el perro en marcha: no entra
        ev_lpm5_.notify(sc_core::SC_ZERO_TIME);
    }
    bool lcd_lento() const { return lcd && lcd->on() && lcd->fuente() != 1; }
    void lpm5_proc() {
        if (lpm5_ || !rst_n.read()) return;
        const uint16_t sr = nucleo.cpu.sr();
        if (!(pmmctl0_ & 0x0010) || (sr & 0x00D0) != 0x00D0) return;
        // Los pines se quedan como están
        for (unsigned i = 0; i < NPIN; ++i) retenido_[i] = conducido_[i];
        lpm5_ = true;
        // LPM3.5: el RTC con XT1 o VLO, o el LCD_E [DS: modos de bajo consumo]
        lpm5_rtc_ = (rtc.activo() && rtc.fuente() >= 2) || lcd_lento();
        xt1_retenido_ = lpm5_rtc_ && xt1_en_marcha();
        // LPM4.5: el dominio del RTC, apagado, y la BAKMEM con él
        if (!lpm5_rtc_) { rtc.reset(); if (lcd) lcd->reset(); std::fill(bkmem_, bkmem_ + 32, 0); }
        ++n_lpm5_;
        for (unsigned k = 0; k < chip.npuertos_irq; ++k) ifg5_[k] = uint8_t(pt_[k].ifg & pt_[k].ie);
        pm5ctl0_ |= 0x0001;                                           // LOCKLPM5 (al despertar)
        rst_n.write(false);                                           // sin núcleo
        // La RAM se pierde [de modelo: queda con 0xCD]
        std::fill(mem_.begin() + desc.ram.ini, mem_.begin() + desc.ram.fin + 1, 0xCD);
        pines_salida();
        // Con una bandera ya puesta (un pin o el RTC con su IE), despierta enseguida
        bool pend = false;
        for (unsigned k = 0; k < chip.npuertos_irq; ++k) pend = pend || ifg5_[k];
        if (pend) despierta_lpm5(0);
        else if (lpm5_rtc_ && rtc.pide()) despierta_lpm5(-1);
    }
    uint8_t ifg5_[8] = {};
    // `pin`: el pin que despierta (-1: el RTC)
    void despierta_lpm5(int pin_d) {
        if (!lpm5_) return;
        if (pin_d < 0 && !(rtc.pide())) return;                       // el RTC despierta con RTCIE
        sal_lpm5();
        pon_rstiv(0x08);
        reset_regs(true, false);
        pmmifg_ |= 0x8000;                                            // PMMLPM5IFG
        // Las banderas de los pines que lo han despertado (o que estaban
        // pendientes al entrar) siguen puestas tras el BOR [UG FR2 §1.4.3.2]
        (void)pin_d;
        for (unsigned k = 0; k < chip.npuertos_irq; ++k) pt_[k].ifg = ifg5_[k];
        // 350 µs hasta que el núcleo arranca [DS: tWAKE-UP LPM3.5]
        ev_bor_fin_.notify(sc_core::sc_time(350, sc_core::SC_US));
    }
    void sal_lpm5() { lpm5_ = false; }
    // El JSTATE [MSPDS PollJStateReg.c: LPMX5_MASK_J 0x40..., BP_HIT_MASK_J
    // 0x04..., LPM4 0x80.../0x83...]. [de modelo] El byte alto: 0x40 en LPMx.5;
    // 0x80 con CPUOFF, más SCG1 (0x02) y SCG0 (0x01); 0x04 parada por el EEM.
    // Abajo, el PC.
    uint64_t jstate() const {
        uint64_t alto = 0;
        if (lpm5_) alto = 0x40;
        else {
            const uint16_t sr = nucleo.cpu.sr();
            if (sr & 0x0010) alto |= 0x80 | ((sr & 0x0080) ? 0x02 : 0) | ((sr & 0x0040) ? 0x01 : 0);
            if (eem.lee(0x82) & EemM430::E_STOPPED) alto |= 0x04;
        }
        return (alto << 56) | (lpm5_ ? 0 : uint64_t(nucleo.cpu.pc() & 0xFFFFFu));
    }

    // --- Interrupciones -------------------------------------------------------------
    struct Fuente {
        enum Clase { WDT, TIMER0, TIMERN, RTC, USCI, ADC, PUERTO, LCD } clase;
        unsigned i;
        uint16_t vec;
    };
    std::vector<Fuente> fuentes_;
    uint16_t vector_de(const char* n) const {
        const m430::Vector* v = desc.vector(n);
        if (!v) { std::fprintf(stderr, "[%s] el vector %s no está en el descriptor\n", chip.etiqueta, n); return 0; }
        return v->dir;
    }
    void construye_fuentes() {
        auto pon = [&](Fuente::Clase c, unsigned i, const char* n) { if (const uint16_t v = vector_de(n)) fuentes_.push_back({c, i, v}); };
        pon(Fuente::WDT, 0, "WDT");
        for (unsigned k = 0; k < chip.ntimers; ++k) { pon(Fuente::TIMER0, k, chip.timers[k].v0); pon(Fuente::TIMERN, k, chip.timers[k].vn); }
        pon(Fuente::RTC, 0, "RTC");
        for (unsigned k = 0; k < chip.nuscis; ++k) pon(Fuente::USCI, k, chip.uscis[k].vec);
        pon(Fuente::ADC, 0, "ADC");
        for (unsigned k = 0; k < chip.npuertos_irq; ++k) {
            const std::string n = "PORT" + std::to_string(k + 1);
            pon(Fuente::PUERTO, k, n.c_str());
        }
        if (lcd) pon(Fuente::LCD, 0, "LCD_E");
    }
    uint64_t fuentes() {
        uint64_t f = 0;
        for (const Fuente& x : fuentes_) {
            bool p = false;
            switch (x.clase) {
            case Fuente::WDT: p = (sfrifg1_ & sfrie1_ & 0x01) && (wdtctl_ & 0x10); break;
            case Fuente::TIMER0: p = irq_t_[2 * x.i]->read(); break;
            case Fuente::TIMERN: p = irq_t_[2 * x.i + 1]->read(); break;
            case Fuente::RTC: p = rtc.pide(); break;
            case Fuente::USCI: p = usci[x.i]->pide(); break;
            case Fuente::ADC: p = adc.pide(); break;
            case Fuente::PUERTO: p = pt_[x.i].ifg & pt_[x.i].ie; break;
            case Fuente::LCD: p = lcd->pide(); break;
            }
            if (p) f |= 1ull << ((x.vec - 0xFF80u) / 2);
        }
        return f;
    }
    void irq_timers() { nucleo.nota_irq(); }
    void avisa_irq() { nucleo.nota_irq(); }
    // Las peticiones de reloj de los módulos [UG FR2 §3.2.12]
    bool smclk_pedido_ = false, aclk_pedido_ = false;
    bool pide_smclk() const {
        for (const auto& u : usci) if (u->fuente_brclk() >= 2 && u->pide_reloj()) return true;
        for (const auto& t : ta) if (t->pide_reloj(2)) return true;
        return adc.convirtiendo() && adc.fuente_reloj() >= 2;
    }
    bool pide_aclk() const {
        if (rtc.activo() || (!(wdtctl_ & 0x80) && ((wdtctl_ >> 5) & 3u) == 1)) return true;
        if (lcd && lcd->on() && lcd->fuente() == 1) return true;
        for (const auto& t : ta) if (t->pide_reloj(1)) return true;
        return false;
    }
    void mira_pedidos() {
        const bool s = pide_smclk(), a = pide_aclk();
        if (s != smclk_pedido_ || a != aclk_pedido_) { smclk_pedido_ = s; aclk_pedido_ = a; relojes(); }
    }
    void usci_proc() {
        mira_pedidos();
        avisa_irq();
    }
    bool disparo_ant_[8] = {};
    void timers_proc() {
        for (unsigned k = 0; k < chip.nenlaces; ++k) {
            const Fr2Enlace& e = chip.enlaces[k];
            for (unsigned n = 0; n < e.n; ++n) ta[e.a]->cci_b[n].write(ta[e.de]->out[n].read());
        }
        for (unsigned k = 0; k < chip.ndisparos && k < 8; ++k) {
            const Fr2Disparo& d = chip.disparos[k];
            const bool v = ta[d.timer]->out[d.ccr].read();
            if (v && !disparo_ant_[k]) adc.dispara(d.shs);
            disparo_ant_[k] = v;
        }
    }

    // --- Puertos --------------------------------------------------------------------
    struct Puerto { uint8_t in = 0, out = 0, dir = 0, ren = 0, sel0 = 0, sel1 = 0, ies = 0, ie = 0, ifg = 0; };
    Puerto pt_[8];
    struct LineaU { EusciFr* u = nullptr; int l = -1; };
    // tipo: 1 Timer (salida con DIR, captura sin él), 2 eUSCI, 3 analógica o
    // cristal, 4 reloj de salida (MCLK, SMCLK, ACLK), 5 TAxCLK (entrada), 6 LCD
    struct Fn { int tipo = 0; TimerAP4* t = nullptr; unsigned n = 0; LineaU u; unsigned reloj = 0; };
    Fn funcion_de(unsigned i) const {
        Fn f;
        // El LCD manda en sus pines (LCDSx), sin mirar PxSEL [UG FR2 §17.2.2]
        if (lcd && lcd_l_[i] >= 0 && lcd->pin_lcd(unsigned(lcd_l_[i]))) { f.tipo = 6; f.n = unsigned(lcd_l_[i]); return f; }
        if (pot_[i] && (syscfg2_ & 0x1000)) { f.tipo = 3; return f; }    // LCDPCTL: los pines de potencia del LCD
        const unsigned p = i / 8, b = i % 8;
        const unsigned s = (((pt_[p].sel1 >> b) & 1u) << 1) | ((pt_[p].sel0 >> b) & 1u);
        if (!s) return f;
        const Fr2Fn& d = fn_[i][s];
        switch (d.tipo) {
        case 1: f.tipo = 1; f.t = ta[d.mod].get(); f.n = d.n; break;
        case 2: f.tipo = 2; f.u = LineaU{usci[d.mod].get(), int(d.n)}; break;
        case 3: f.tipo = 3; break;
        case 4: f.tipo = 4; f.reloj = d.n; break;
        case 5: f.tipo = 5; f.t = ta[d.mod].get(); break;
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
    Retenido conducido_[NPIN];
    void conduce(unsigned i, const Retenido& r) {
        if (r.hiz) pin[i]->set_hiz(id_pin_[i]);
        else if (r.v >= 0.0) pin[i]->set_drive(id_pin_[i], float(r.v), float(r.r));
        else pin[i]->set_drive(id_pin_[i], r.alto ? float(vcc) : 0.0f, float(r.r));
    }
    bool es_jtag4(unsigned i) const { for (uint8_t j : chip.jtag) if (j == i) return true; return false; }
    void pines_salida() {
        const bool jtag4 = pines_dbg && pines_dbg->jtag4_activo();
        const bool bloqueados = (pm5ctl0_ & 1u) || lpm5_;
        for (unsigned i = 0; i < NPIN; ++i) {
            if (!pin[i]) continue;
            if (jtag4 && es_jtag4(i)) { pin[i]->set_hiz(id_pin_[i]); continue; }
            // El LCD sigue con sus ondas en LPM3.5 y con LOCKLPM5
            if (lcd && lcd_l_[i] >= 0 && lcd->pin_lcd(unsigned(lcd_l_[i]))) { pin_lcd(i); continue; }
            // LOCKLPM5: el pin sigue como estaba al entrar en LPMx.5 (o en
            // alta impedancia tras un BOR) hasta que se borra [UG FR2 §8.3.1]
            if (bloqueados) { conduce(i, retenido_[i]); continue; }
            Retenido r = salida_de(i);
            conducido_[i] = r;
            conduce(i, r);
        }
    }
    // Un pin del LCD: la tensión de su onda en esta fase (o alta impedancia)
    double v_lcd_[NPIN] = {};
    void pin_lcd(unsigned i) {
        const double v = lcd->v_pin(unsigned(lcd_l_[i]));
        Retenido r;
        if (v >= 0.0) { r.hiz = false; r.v = v; r.r = 2000.0; }      // [de modelo: la salida del LCD, ~2 kohm]
        conducido_[i] = r;
        if (v == v_lcd_[i] && v >= 0.0) return;
        v_lcd_[i] = v;
        conduce(i, r);
    }
    void pines_lcd() {
        if (!lcd) return;
        for (unsigned l = 0; l < chip.nlcd; ++l) {
            const unsigned i = chip.lcd_l[l];
            if (!pin[i]) continue;
            if (lcd->pin_lcd(l)) pin_lcd(i);
            else if (v_lcd_[i] != -2.0) { v_lcd_[i] = -2.0; pines_salida(); return; }   // vuelve a ser un pin normal
        }
    }
    Retenido salida_de(unsigned i) const {
        Retenido r;
        const Puerto& x = pt_[i / 8];
        const unsigned b = i % 8;
        const bool sel = ((x.sel0 | x.sel1) >> b) & 1u, dir = (x.dir >> b) & 1u;
        if (adc_ch_[i] >= 0 && ((syscfg2_ >> unsigned(adc_ch_[i])) & 1u)) return r;   // ADCPCTLx: analógico
        if (pot_[i] && (syscfg2_ & 0x1000)) return r;                 // LCDPCTL
        if (sel) {
            const Fn f = funcion_de(i);
            if (f.tipo == 1 && dir) { r.hiz = false; r.alto = f.t->out[f.n].read(); return r; }
            if (f.tipo == 2) {
                bool oe = false, o = false;
                linea_u(f.u, oe, o);
                if (f.u.u->en_i2c()) { if (oe) { r.hiz = false; r.alto = false; } return r; }
                if (oe) { r.hiz = false; r.alto = o; }
                return r;
            }
            if (f.tipo == 4 && dir) {
                // los relojes de salida: se ven como nivel alto si corren [de modelo]
                const double fr = f.reloj == 0 ? cs.f_mclk : f.reloj == 1 ? cs.f_smclk : cs.f_aclk;
                r.hiz = false; r.alto = fr > 0.0;
                return r;
            }
            if (f.tipo != 0 || !dir) return r;
        }
        if (dir) { r.hiz = false; r.alto = (x.out >> b) & 1u; }
        else if ((x.ren >> b) & 1u) { r.hiz = false; r.alto = (x.out >> b) & 1u; r.r = 35000.0; }
        return r;
    }
    void salidas_proc() { pines_salida(); }
    bool nivel(unsigned i) const { return !pin[i]->floating() && pin[i]->voltage() > 0.5f * float(vcc); }
    void entradas_proc() {
        for (unsigned k = 0; k < chip.npuertos; ++k) {
            Puerto& x = pt_[k];
            uint8_t n = 0;
            for (unsigned b = 0; b < 8; ++b)
                if (pin[k * 8 + b] && nivel(k * 8 + b)) n = uint8_t(n | (1u << b));
            const uint8_t cambio = uint8_t(n ^ x.in);
            if (k < chip.npuertos_irq && cambio) {
                const uint8_t sube = uint8_t(cambio & n), baja = uint8_t(cambio & ~n);
                const uint8_t flanco = uint8_t((sube & ~x.ies) | (baja & x.ies));
                if (lpm5_) {
                    // En LPMx.5 los registros no están: el despertar lo deciden
                    // los IES/IE que había al entrar [de modelo: los guardados]
                    const uint8_t f5 = uint8_t(((sube & ~ies5_[k]) | (baja & ies5_[k])) & ie5_[k]);
                    ifg5_[k] = uint8_t(ifg5_[k] | f5);
                    if (f5) despierta_lpm5(int(k * 8));
                } else if (!(pm5ctl0_ & 1u)) {
                    x.ifg = uint8_t(x.ifg | flanco);
                    if (x.ifg & x.ie) avisa_irq();
                }
            }
            x.in = n;
        }
        for (unsigned i = 0; i < NPIN; ++i) {
            if (!pin[i]) continue;
            const Fn f = funcion_de(i);
            if (f.tipo != 1 && f.tipo != 2 && f.tipo != 5) continue;
            const bool v = nivel(i);
            const bool dir = (pt_[i / 8].dir >> (i % 8)) & 1u;
            if (f.tipo == 2) {
                switch (f.u.l) {
                case 0: f.u.u->simo_in.write(v); break;
                case 1: f.u.u->somi_in.write(v); break;
                case 2: f.u.u->clk_in.write(v); break;
                case 3: f.u.u->ste_in.write(v); break;
                default: break;
                }
            } else if (f.tipo == 1 && !dir) f.t->cci_a[f.n].write(v);
            else if (f.tipo == 5 && !dir) f.t->taclk.write(v);
        }
    }
    uint8_t ies5_[8] = {}, ie5_[8] = {};
    sc_core::sc_event ev_entradas_;
    uint16_t piv(unsigned k, bool dbg) {
        Puerto& x = pt_[k];
        const uint8_t pend = uint8_t(x.ifg & x.ie);
        for (unsigned b = 0; b < 8; ++b)
            if (pend & (1u << b)) { if (!dbg) x.ifg = uint8_t(x.ifg & ~(1u << b)); return uint16_t(2 * (b + 1)); }
        return 0;
    }
    // Un registro de puerto: los pares de puertos en bloques de 0x20 desde
    // 0x200 (el impar en el byte alto), con el PxIV del par en +0x0E y +0x1E
    bool reg_puerto(uint32_t a, unsigned& k, unsigned& r) const {
        if (a < 0x200 || a >= 0x200 + 0x20 * ((chip.npuertos + 1) / 2)) return false;
        const unsigned bloque = (a - 0x200) / 0x20, off = (a - 0x200) % 0x20;
        if ((off & ~1u) == 0x0E || (off & ~1u) == 0x1E) return false;   // los PxIV (en lee16)
        k = 2 * bloque + (off & 1u);
        r = off & ~1u;
        return k < chip.npuertos;
    }
    // ¿Es el PxIV de un puerto con interrupción? (k: el puerto)
    bool reg_piv(uint32_t w, unsigned& k) const {
        if (w < 0x200 || w >= 0x200 + 0x20 * ((chip.npuertos + 1) / 2)) return false;
        const unsigned bloque = (w - 0x200) / 0x20, off = (w - 0x200) % 0x20;
        if (off == 0x0E) k = 2 * bloque; else if (off == 0x1E) k = 2 * bloque + 1; else return false;
        return k < chip.npuertos_irq;
    }
    uint8_t lee_puerto(uint32_t a) const {
        unsigned k, r;
        if (!reg_puerto(a, k, r)) return 0;
        const Puerto& x = pt_[k];
        const bool irq = k < chip.npuertos_irq;
        switch (r) {
        case 0x00: return x.in;
        case 0x02: return x.out;
        case 0x04: return x.dir;
        case 0x06: return x.ren;
        case 0x0A: return x.sel0;
        case 0x0C: return x.sel1;
        case 0x18: return irq ? x.ies : 0;
        case 0x1A: return irq ? x.ie : 0;
        case 0x1C: return irq ? x.ifg : 0;
        default: return 0;
        }
    }
    void escribe_puerto(uint32_t a, uint8_t v) {
        unsigned k, r;
        if (!reg_puerto(a, k, r)) return;
        Puerto& x = pt_[k];
        const bool irq = k < chip.npuertos_irq;
        switch (r) {
        case 0x02: x.out = v; break;
        case 0x04: x.dir = v; break;
        case 0x06: x.ren = v; break;
        case 0x0A: x.sel0 = v; if (k == chip.xt1_puerto) relojes(); break;     // XIN/XOUT: el cristal
        case 0x0C: x.sel1 = v; break;
        case 0x18: if (irq) { x.ies = v; ies5_[k] = v; } break;
        case 0x1A: if (irq) { x.ie = v; ie5_[k] = v; avisa_irq(); } break;
        // Con LOCKLPM5 las PxIFG no se borran [UG FR2 §8.3.4: «the PxIFG flag
        // cannot be cleared until the LOCKLPM5 bit has been cleared»]
        case 0x1C: if (irq) { x.ifg = (pm5ctl0_ & 1u) ? uint8_t(x.ifg | v) : v; avisa_irq(); } break;
        default: break;
        }
        pines_salida();
    }

    // --- El monitor del backchannel ------------------------------------------------
    std::vector<uint8_t> rx_cola_;
    sc_core::sc_event ev_rx_;
    int id_rx_ = -1;
    analog_net_if* bcl_rx() const { return pin[chip.bcl_rx]; }
    analog_net_if* bcl_tx() const { return pin[chip.bcl_tx]; }
    void uart_mon_proc() {
        if (!monitor_uart) return;
        id_rx_ = bcl_rx()->register_driver("ezfet_txd");
        bcl_rx()->set_drive(id_rx_, float(vcc), 1000.0f);
        sc_core::sc_spawn([this] { mon_tx(); });
        for (;;) {
            while (rx_cola_.empty()) wait(ev_rx_);
            const uint8_t c = rx_cola_.front();
            rx_cola_.erase(rx_cola_.begin());
            const double tb = t_bit();
            if (tb <= 0) continue;
            auto pon = [&](bool v) { bcl_rx()->set_drive(id_rx_, v ? float(vcc) : 0.0f, 50.0f); wait(sc_core::sc_time(tb, sc_core::SC_SEC)); };
            pon(false);
            for (unsigned k = 0; k < 8; ++k) pon((c >> k) & 1u);
            pon(true);
            bcl_rx()->set_drive(id_rx_, float(vcc), 1000.0f);
        }
    }
    double baudios_mon_ = 0;
    double t_bit() {
        const double b = usci[chip.bcl_usci]->baudios();
        if (b > 0) baudios_mon_ = b;
        return baudios_mon_ > 0 ? 1.0 / baudios_mon_ : 0.0;
    }
    void mon_tx() {
        for (;;) {
            wait(bcl_tx()->value_changed_event());
            if (bcl_tx()->voltage() > 0.5f * float(vcc)) continue;
            const double tb = t_bit();
            if (tb <= 0) continue;
            wait(sc_core::sc_time(tb * 1.5, sc_core::SC_SEC));
            unsigned c = 0;
            for (unsigned k = 0; k < 8; ++k) {
                if (bcl_tx()->voltage() > 0.5f * float(vcc)) c |= 1u << k;
                wait(sc_core::sc_time(tb, sc_core::SC_SEC));
            }
            uart_tx.push_back(char(c));
        }
    }

    // --- El bus ---------------------------------------------------------------------
    uint8_t declarado_[0x1000] = {};
    bool avisado_ = false;
    void aviso(uint32_t a) {
        if (avisado_) return;
        avisado_ = true;
        std::fprintf(stderr, "[%s] 0x%04X: registro sin modulo (aviso una vez)\n", chip.etiqueta, unsigned(a));
    }
    // La eUSCI de esa dirección (y su desplazamiento)
    EusciFr* usci_de(uint32_t a, uint32_t& off) const {
        for (unsigned k = 0; k < chip.nuscis; ++k) {
            const uint32_t b = chip.uscis[k].base, t = chip.uscis[k].es_b ? 0x30 : 0x20;
            if (a >= b && a < b + t) { off = a - b; return usci[k].get(); }
        }
        return nullptr;
    }
    bool es_mpy(uint32_t a) const { return chip.mpy32 && a >= 0x4C0 && a < 0x4F0; }
    bool es_lcd(uint32_t a) const { return lcd && a >= chip.lcd_base && a < chip.lcd_base + 0x60; }
    bool lee16(uint32_t a, uint16_t& v, bool dbg) {
        uint32_t off = 0;
        if (es_mpy(a)) { v = mpy.lee16(a - 0x4C0); return true; }
        if (EusciFr* u = usci_de(a, off)) { v = u->lee(off, dbg); return true; }
        if (es_lcd(a)) { v = lcd->lee(a - chip.lcd_base, dbg); return true; }
        if (a >= 0x700 && a < 0x720) { v = adc.lee(a - 0x700, dbg); return true; }
        if (a >= chip.rtc_base && a < chip.rtc_base + 0x10) { v = rtc.lee(a - chip.rtc_base, dbg); return true; }
        if (a >= 0x660 && a < 0x680) { v = uint16_t(bkmem_[a - 0x660] | (bkmem_[a - 0x660 + 1] << 8)); return true; }
        if (a >= 0x180 && a < 0x192) { v = cs.lee((a - 0x180) / 2); return true; }
        unsigned k = 0;
        if (reg_piv(a, k)) { v = piv(k, dbg); return true; }
        switch (a) {
        case 0x100: v = sfrie1_; return true;
        case 0x102: v = sfrifg1_; return true;
        case 0x104: v = sfrrpcr_; return true;
        case 0x120: v = uint16_t(0x9600 | (pmmctl0_ & 0xFF)); return true;
        case 0x122: v = 0x9600; return true;
        case 0x124: v = uint16_t((pmmctl2_ & 0x003B) | 0x3200 | ((pmmctl2_ & 0x0003) ? 0x0100 : 0)); return true;
        case 0x12A: v = pmmifg_; return true;
        case 0x12E: v = 0; return true;
        case 0x130: v = pm5ctl0_; return true;
        case 0x140: v = sysctl_; return true;
        case 0x142: v = sysbslc_; return true;
        case 0x146: v = sysjmbc_; return true;
        case 0x148: v = jmbi_[0]; if (!dbg && !(sysjmbc_ & 0x40)) { sysjmbc_ = uint16_t(sysjmbc_ & ~0x01); } if (!dbg) sfrifg1_ &= uint16_t(~0x40); return true;
        case 0x14A: v = jmbi_[1]; if (!dbg && !(sysjmbc_ & 0x80)) { sysjmbc_ = uint16_t(sysjmbc_ & ~0x02); } return true;
        case 0x14C: v = jmbo_[0]; return true;
        case 0x14E: v = jmbo_[1]; return true;
        case 0x158: v = 0; return true;                               // SYSBERRIV
        case 0x15A: v = lee_univ(dbg); return true;
        case 0x15C: v = lee_sniv(dbg); return true;
        case 0x15E: v = lee_rstiv(dbg); return true;
        case 0x160: v = uint16_t(0x9600 | (syscfg0_ & 0xFF)); return true;
        case 0x162: v = syscfg1_; return true;
        case 0x164: v = syscfg2_; return true;
        case 0x166: v = syscfg3_; return true;
        case 0x1A0: v = uint16_t(0x9600 | (frctl0_ & 0xFF)); return true;
        case 0x1A4: v = gcctl0_; return true;
        case 0x1A6: v = gcctl1_; return true;
        case 0x1C0: case 0x1C2: v = 0; return true;                   // CRCDI, CRCDIRB
        case 0x1C4: v = crc_; return true;
        case 0x1C6: v = uint16_t((al_reves8(uint8_t(crc_)) << 8) | al_reves8(uint8_t(crc_ >> 8))); return true;
        case 0x1CC: v = uint16_t(0x6900 | (wdtctl_ & 0xFF)); return true;
        default: break;
        }
        return false;
    }
    void escribe16(uint32_t a, uint16_t v, uint16_t m) {
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        uint32_t off = 0;
        if (es_mpy(a)) { mpy.escribe(a - 0x4C0, v, m != 0xFFFF); return; }
        if (EusciFr* u = usci_de(a, off)) { u->escribe(off, v, m); return; }
        if (es_lcd(a)) { lcd->escribe(a - chip.lcd_base, v, m); relojes(); pines_salida(); return; }
        if (a >= 0x700 && a < 0x720) { adc.escribe(a - 0x700, v, m); return; }
        if (a >= chip.rtc_base && a < chip.rtc_base + 0x10) { rtc.escribe(a - chip.rtc_base, v, m); relojes(); return; }
        if (a >= 0x660 && a < 0x680) {
            if (m & 0x00FF) bkmem_[a - 0x660] = uint8_t(v);
            if (m & 0xFF00) bkmem_[a - 0x660 + 1] = uint8_t(v >> 8);
            return;
        }
        if (a >= 0x180 && a < 0x192) { cs.escribe((a - 0x180) / 2, v, m, entradas_cs()); relojes(); avisa_irq(); return; }
        unsigned k = 0;
        if (reg_piv(a, k)) return;
        switch (a) {
        case 0x100: sfrie1_ = mz(sfrie1_); avisa_irq(); return;
        case 0x102: sfrifg1_ = mz(sfrifg1_); if (cs.falta()) sfrifg1_ |= 0x02; avisa_irq(); return;
        case 0x104: sfrrpcr_ = mz(sfrrpcr_); return;
        case 0x120:
            // PMMPW: 0xA5 abre; otra cosa por palabra, PUC; por byte, cierra [UG FR2 §2.3]
            if (m & 0xFF00) {
                const uint8_t pw = uint8_t(v >> 8);
                if (pw != 0xA5 && m == 0xFFFF) { puc(false, 0x20); return; }
                pmm_abierto_ = pw == 0xA5;
            }
            if ((m & 0x00FF) && pmm_abierto_) {
                if (v & 0x04) { pmmifg_ |= 0x0100; bor(0x06); return; }            // PMMSWBOR
                if (v & 0x08) { pmmifg_ |= 0x0400; puc_por(); return; }           // PMMSWPOR
                pmmctl0_ = uint16_t((pmmctl0_ & ~0x0050u) | (v & 0x0050u));
                mira_lpm5();
            }
            return;
        case 0x124: if (pmm_abierto_) pmmctl2_ = uint16_t(mz(pmmctl2_) & 0x003Bu); else puc(false, 0x20); return;
        case 0x12A: pmmifg_ = mz(pmmifg_); return;
        case 0x12E: return;
        case 0x130: {
            // PM5CTL0: sin contraseña; LOCKLPM5 solo se borra
            const uint16_t n = mz(pm5ctl0_);
            pm5ctl0_ = uint16_t((n & 0x0030u) | (pm5ctl0_ & n & 0x0001u));
            if (!(pm5ctl0_ & 1u) && xt1_retenido_) { xt1_retenido_ = false; relojes(); }
            pines_salida();
            ev_entradas_.notify(sc_core::SC_ZERO_TIME);
            avisa_irq();
            return;
        }
        case 0x140: sysctl_ = mz(sysctl_); return;
        case 0x142: sysbslc_ = mz(sysbslc_); return;
        case 0x146: { const uint16_t n = mz(sysjmbc_);
                      sysjmbc_ = uint16_t((sysjmbc_ & 0x0C) | (sysjmbc_ & n & 0x03) | (n & 0xD0)); return; }
        case 0x14C: jmbo_[0] = mz(jmbo_[0]); sysjmbc_ = uint16_t(sysjmbc_ & ~0x04); sfrifg1_ &= uint16_t(~0x80); return;
        case 0x14E: jmbo_[1] = mz(jmbo_[1]); sysjmbc_ = uint16_t(sysjmbc_ & ~0x08); sfrifg1_ &= uint16_t(~0x80); return;
        case 0x15A: case 0x15C: return;
        case 0x15E: rstiv_.clear(); return;                           // escribir lo borra todo
        case 0x160:
            // SYSCFG0: la contraseña y los bits, en una palabra [UG FR2 tabla 1-29, nota 2]
            if (chip.syscfg0_sin_clave) { if (m & 0x00FF) syscfg0_ = uint16_t(v & 0x0003u); }
            else if (m == 0xFFFF && (v >> 8) == 0xA5) syscfg0_ = uint16_t(v & 0x00FFu);
            return;
        case 0x162: syscfg1_ = mz(syscfg1_); return;
        case 0x164: syscfg2_ = mz(syscfg2_); pines_salida(); return;
        case 0x166: syscfg3_ = mz(syscfg3_); return;
        case 0x1A0:
            if (m & 0xFF00) {
                const uint8_t pw = uint8_t(v >> 8);
                if (pw != 0xA5 && m == 0xFFFF) { puc(false, 0x1A); return; }      // FRCTLPW
                frctl_abierto_ = pw == 0xA5;
            }
            if ((m & 0x00FF) && frctl_abierto_) { frctl0_ = uint16_t(v & 0x0070u); relojes(); }
            return;
        case 0x1A4: if (frctl_abierto_) gcctl0_ = uint16_t((mz(gcctl0_) & 0x00E4u) | 0x0002u); return;
        case 0x1A6: gcctl1_ = uint16_t(gcctl1_ & (v | ~m) & 0x000Eu); return;      // se borran con 0
        case 0x1C0: for (unsigned b = 0; b < 2; ++b) if (m & (0xFFu << (8 * b))) crc_byte(uint8_t(v >> (8 * b))); return;
        case 0x1C2: for (unsigned b = 0; b < 2; ++b) if (m & (0xFFu << (8 * b))) crc_byte(al_reves8(uint8_t(v >> (8 * b)))); return;
        case 0x1C4: crc_ = mz(crc_); return;
        case 0x1CC:
            if (m == 0xFFFF && (v >> 8) != 0x5A) { sfrifg1_ |= 0x01; puc(false, 0x18); return; }
            if (m != 0xFFFF) { puc(false, 0x18); return; }                       // el byte bajo solo: violación
            wdtctl_ = uint16_t(v & 0xF7);
            if (v & 0x08) wdt_t0_ = sc_core::sc_time_stamp();
            relojes();
            return;
        default: break;
        }
    }
    // El POR por programa: como el PUC, con su causa (0x14)
    void puc_por() { puc(false, 0x14); }

public:
    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint32_t a = uint32_t(gp.get_address()) & 0xFFFFFu;
        const unsigned n = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
        for (unsigned k = 0; k < chip.ntimers; ++k) {                 // los Timer
            if (a < chip.timers[k].base || a >= chip.timers[k].base + 0x40) continue;
            tlm_utils::simple_initiator_socket<SocFR2>& s = *a_ta_[k];
            sc_core::sc_time t = sc_core::SC_ZERO_TIME;
            if (dbg) s->transport_dbg(gp); else s->b_transport(gp, t);
            if (gp.is_write()) mira_pedidos();
            return;
        }
        if (a < 0x1000) {
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
            unsigned k, r;
            const bool puerto = reg_puerto(a, k, r);
            if (gp.is_read()) {
                if (puerto) { d[0] = lee_puerto(a); if (n == 2) d[1] = lee_puerto(a + 1); return; }
                uint16_t v = 0;
                if (lee16(w, v, dbg)) {
                    if (n == 1) d[0] = uint8_t((a & 1) ? v >> 8 : v);
                    else { d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); }
                    return;
                }
                for (unsigned i = 0; i < n; ++i) d[i] = declarado_[(a + i) & 0xFFF];
                return;
            }
            if (puerto) { escribe_puerto(a, d[0]); if (n == 2) escribe_puerto(a + 1, d[1]); return; }
            uint16_t v; uint16_t m;
            if (n == 1) { v = (a & 1) ? uint16_t(d[0] << 8) : d[0]; m = (a & 1) ? 0xFF00 : 0x00FF; }
            else { v = uint16_t(d[0] | (d[1] << 8)); m = 0xFFFF; }
            uint16_t x;
            if (lee16(w, x, true) || es_mpy(w)) { escribe16(w, v, m); return; }
            for (unsigned i = 0; i < n; ++i) declarado_[(a + i) & 0xFFF] = d[i];
            aviso(a);
            return;
        }
        if (a >= 0x1A00 && a < 0x1B00) {                              // TLV: solo lectura
            if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = mem_[a + i];
            return;
        }
        if (a + n <= mem_.size() && (es_ram(a) || es_fram(a) || es_bsl(a))) {
            if (gp.is_read()) { for (unsigned i = 0; i < n; ++i) d[i] = mem_[a + i]; return; }
            if (es_bsl(a) && !dbg) return;                            // ROM
            // La FRAM protegida no se escribe (el depurador, sí: la herramienta
            // la abre con SYSCFG0 [MSPDS]; aquí escribe por debajo)
            if (es_fram(a) && !dbg && fram_protegida(a)) return;
            for (unsigned i = 0; i < n; ++i) mem_[a + i] = d[i];
            return;
        }
        if (gp.is_read()) for (unsigned i = 0; i < n; ++i) d[i] = 0xFF;   // vacío [de modelo]
    }
    // Lo que hay en la memoria del LCD_E (para el banco de pruebas)
    const LcdE* lcd_e() const { return lcd.get(); }
private:
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        M430Ext* x = nullptr;
        gp.get_extension(x);
        acceso(gp, x && x->depurador);
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) { acceso(gp, true); return gp.get_data_length(); }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR2_SOC_FR2_H
