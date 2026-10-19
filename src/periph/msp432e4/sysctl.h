// =============================================================================
// sysctl.h — El SYSCTL del MSP432E4: relojes, MEMTIM0, RCGC/PR y resets (T6)
//
// Es el bloque que más se aparta del P4 y el que más tropiezos da en clase. En
// 0x400F_E000 [H, hw_sysctl.h] están:
//
//   DID0/DID1     la identidad: 0x180C0002 / 0x102DC06E en el E401Y [OO]
//   RIS/IMC/MISC  las interrupciones del bloque: MOSC arrancado, PLL enganchado
//   RESC          por qué se arrancó; se acumula y se borra escribiendo
//   MOSCCTL       el oscilador principal (el cristal de 25 MHz de la LaunchPad,
//                 en OSC0/OSC1): PWRDN y NOXTAL suben en el reset
//   RSCLKCFG      de dónde sale el reloj del sistema: OSCSRC y OSYSDIV sin PLL,
//                 USEPLL, PLLSRC y PSYSDIV con él; y MEMTIMU y NEWFREQ, los dos
//                 bits que hacen efectivos MEMTIM0 y PLLFREQn
//   MEMTIM0       estados de espera de la Flash (FWS) y de la EEPROM (EWS)
//   PLLFREQ0/1    MINT, MFRAC, N y Q, y PLLPWR
//   PLLSTAT       LOCK
//   PPx, SRx, RCGCx, SCGCx, DCGCx, PCx, PRx   por clase de periférico: presente,
//                 en reset, con reloj (en marcha, dormido, sueño profundo),
//                 encendido y LISTO
//
// LA FRECUENCIA [DL: _SysCtlFrequencyGet, y la fórmula del datasheet]:
//
//   fVCO = fIN · (MINT + MFRAC/1024) / (N+1)                fIN = PIOSC o MOSC
//   SYSCLK = fVCO / (Q+1) / (PSYSDIV+1)   con USEPLL
//   SYSCLK = fOSC / (OSYSDIV+1)   sin él                  fOSC = OSCSRC
//
// DriverLib pone el divisor del sistema en Q y deja PSYSDIV = 1: con 25 MHz,
// MINT = 96, N = 4 y Q = 1 da 480 / 2 / 2 = 120 MHz. Las dos lecturas de la
// fórmula dan lo mismo, porque es un producto.
//
// LO QUE EL SILICIO NO DICE Y EL MODELO SÍ, con nombre y apellidos, como el RCC
// de los STM32 y el CS del P4:
//
//   * SYSCLK por encima de 120 MHz;
//   * menos estados de espera en la Flash de los que pide la frecuencia (la
//     tabla de g_sXTALtoMEMTIM de DriverLib) -en la placa da lecturas de Flash
//     equivocadas, sin error-; MEMTIM0 NO cuenta hasta que se escribe
//     RSCLKCFG con MEMTIMU, que es el olvido de quien lo hace a mano;
//   * USEPLL con el PLL sin enganchar.
//
// «PERIFÉRICO LISTO». Un bit de RCGC da reloj al periférico, pero no al
// instante: hay que esperar a que su bit de PR suba, que DriverLib sondea en
// SysCtlPeripheralReady(). El modelo lo hace esperar 3 ciclos de SYSCLK
// [1F: la nota «There must be a delay of 3 system clocks...» de los
// datasheets de Tiva; P-17 d]. Tocar un periférico sin reloj o antes de que
// esté listo es un error de bus, y el SoC lo dice una vez por periférico.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_SYSCTL_H
#define STM32_PERIPH_MSP432E4_SYSCTL_H

#include <array>
#include <cstdio>
#include <functional>
#include <map>
#include "../../common/periph_base.h"
#include "../../rcc/osc_pll.h"
#include "flash.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class SysctlE4 : public BusSlave {
public:
    // Los desplazamientos [H, hw_sysctl.h]
    enum : uint32_t {
        R_DID0 = 0x000, R_DID1 = 0x004, R_RIS = 0x050, R_IMC = 0x054,
        R_MISC = 0x058, R_RESC = 0x05C, R_MOSCCTL = 0x07C, R_RSCLKCFG = 0x0B0,
        R_MEMTIM0 = 0x0C0, R_ALTCLKCFG = 0x138, R_DSCLKCFG = 0x144,
        R_SYSPROP = 0x14C, R_PLLFREQ0 = 0x160, R_PLLFREQ1 = 0x164,
        R_PLLSTAT = 0x168, R_RESBEHAVCTL = 0x1D8,
        R_PP = 0x300, R_SR = 0x500, R_RCGC = 0x600, R_SCGC = 0x700,
        R_DCGC = 0x800, R_PC = 0x900, R_PR = 0xA00, R_UNIQUEID0 = 0xF20
    };
    // Las clases de periférico: el índice de su registro en cada fila
    enum Clase : unsigned {
        C_WD = 0, C_TIMER = 1, C_GPIO = 2, C_DMA = 3, C_EPI = 4, C_HIB = 5,
        C_UART = 6, C_SSI = 7, C_I2C = 8, C_USB = 10, C_EPHY = 12, C_CAN = 13,
        C_ADC = 14, C_ACMP = 15, C_PWM = 16, C_QEI = 17, C_EEPROM = 22,
        C_CCM = 29, C_LCD = 36, C_OWIRE = 38, C_EMAC = 39, N_CLASES = 40
    };
    // Los bits de RESC [H]
    static constexpr uint32_t RESC_EXT = 1u << 0, RESC_POR = 1u << 1,
        RESC_BOR = 1u << 2, RESC_WDT0 = 1u << 3, RESC_SW = 1u << 4,
        RESC_WDT1 = 1u << 5, RESC_HIB = 1u << 6 /* [DL: SYSCTL_CAUSE_HIB] */, RESC_HSSR = 1u << 12,
        RESC_MOSCFAIL = 1u << 16;
    static constexpr uint32_t RIS_PLLL = 1u << 6, RIS_MOSCPUP = 1u << 8;
    // El enganche del PLL [modelo: P-17 e]
    static constexpr double T_LOCK_S = 100e-6;
    static constexpr unsigned CICLOS_LISTO = 3;

    // El cristal principal (OSC0/OSC1): el SoC le da su nodo
    Oscillator mosc{"mosc"};
    sc_core::sc_out<bool> irq{"irq"};

    // El SoC: la frecuencia ha cambiado; un periférico cambia de reloj o de
    // reset por software; el reloj del RTC (OSCSRC = RTC).
    std::function<void()> al_cambiar;
    std::function<void()> al_cambiar_periferico;
    std::function<double()> f_rtc;
    // Los registros de la Flash que viven aquí (RMCTL, BOOTCFG, USER_REG,
    // FMPRE/FMPPE): los atiende el controlador de la Flash
    FlashCtrlE4* flash = nullptr;

    SysctlE4(sc_core::sc_module_name nm, uint32_t did0, uint32_t did1)
        : BusSlave(nm, e4::SYSCTL_B, 0x1000), did0_(did0), did1_(did1) {
        SC_HAS_PROCESS(SysctlE4);
        mosc.ready(s_mosc_rdy_); mosc.clk(s_mosc_clk_); mosc.freq_hz(s_mosc_hz_);
        mosc.needs_source = true;
        mosc.nominal_hz = 25e6;            // el de la LaunchPad [EN]
        mosc.t_startup_s = 1e-3;           // [modelo: P-17 e]
        mosc.set_waveform(false);
        SC_METHOD(mosc_proc); sensitive << mosc.state_event(); dont_initialize();
        SC_THREAD(pll_proc);
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
        resc_ = RESC_POR;
    }

    // --- Las frecuencias -------------------------------------------------------
    double f_piosc() const { return e4::HZ_PIOSC; }
    double f_osc() const {
        switch ((rsclkcfg_ >> 20) & 0xFu) {
            case 0:  return f_piosc();
            case 2:  return e4::HZ_LFIOSC;
            case 3:  return mosc.out_hz();
            case 4:  return f_rtc ? f_rtc() : 0.0;
            default: return 0.0;
        }
    }
    double f_pll_in() const {
        return ((rsclkcfg_ >> 24) & 0xFu) == 3 ? mosc.out_hz() : f_piosc();
    }
    // El VCO (480 MHz con el cristal de 25 y M = 96, N = 4), y lo que sale del
    // PLL tras Q, que es lo que divide PSYSDIV
    double f_vco() const {
        const double m = double(pllfreq0_ & 0x3FFu) + double((pllfreq0_ >> 10) & 0x3FFu) / 1024.0;
        const double n = double((pllfreq1_ & 0x1Fu) + 1u);
        return f_pll_in() * m / n;
    }
    double f_pll() const { return f_vco() / double(((pllfreq1_ >> 8) & 0x1Fu) + 1u); }
    bool pll_encendido() const { return (pllfreq0_ >> 23) & 1u; }
    bool pll_enganchado() const { return lock_; }
    double f_sys() const {
        if ((rsclkcfg_ & (1u << 28)) && lock_)
            return f_pll() / double((rsclkcfg_ & 0x3FFu) + 1u);
        return f_osc() / double(((rsclkcfg_ >> 10) & 0x3FFu) + 1u);
    }
    // ALTCLK: la otra fuente de la UART, el SSI y el ADC (PIOSC por omisión)
    double f_alt() const {
        switch (altclkcfg_ & 0xFu) {
            case 0:  return f_piosc();
            case 3:  return f_rtc ? f_rtc() : 0.0;
            case 4:  return e4::HZ_LFIOSC;
            default: return 0.0;
        }
    }
    // Los tiempos de memoria que valen AHORA (los que se cargaron con MEMTIMU)
    unsigned fws() const { return memtim_activo_ & 0xFu; }
    unsigned ews() const { return (memtim_activo_ >> 16) & 0xFu; }
    uint32_t memtim0_escrito() const { return memtim0_; }
    uint32_t rsclkcfg() const { return rsclkcfg_; }

    // --- Los periféricos ----------------------------------------------------
    bool con_reloj(unsigned c, unsigned bit) const {
        return c < N_CLASES && ((rcgc_[c] >> bit) & 1u);
    }
    bool en_reset_sw(unsigned c, unsigned bit) const {
        return c < N_CLASES && ((sr_[c] >> bit) & 1u);
    }
    // ¿Está listo en el instante `t`? Con reloj, fuera de reset y pasados los
    // ciclos desde que se le dio el reloj
    bool listo(unsigned c, unsigned bit, const sc_core::sc_time& t) const {
        if (!con_reloj(c, bit) || en_reset_sw(c, bit)) return false;
        return t >= desde_[c][bit & 31u];
    }
    static bool presente(unsigned c, unsigned bit) {
        return c < N_CLASES && ((pp_valor(c) >> bit) & 1u);
    }

    // --- Los resets ------------------------------------------------------------
    uint32_t resc() const { return resc_; }
    // El SoC, al ejecutar uno: los registros vuelven a su valor de reset (lo hace
    // rst_n) y RESC acumula la causa; un POR la deja sola.
    void anota_reset(uint32_t bits) { if (bits & RESC_POR) resc_ = bits; else resc_ |= bits; }

    // --- Avisos ----------------------------------------------------------------
    unsigned avisos_pll() const { return n_av_pll_; }

protected:
    const uint32_t did0_, did1_;
    uint32_t ris_ = 0, imc_ = 0, resc_ = 0, mosccctl_ = 0, rsclkcfg_ = 0,
             memtim0_ = 0, memtim_activo_ = 0, altclkcfg_ = 0, dsclkcfg_ = 0,
             pllfreq0_ = 0, pllfreq1_ = 0, resbehav_ = 0;
    std::array<uint32_t, N_CLASES> sr_{}, rcgc_{}, scgc_{}, dcgc_{}, pc_{};
    sc_core::sc_time desde_[N_CLASES][32];
    std::map<uint32_t, uint32_t> otros_;
    bool lock_ = false;
    unsigned n_av_pll_ = 0;
    sc_core::sc_time t_acc_;
    sc_core::sc_event pll_ev_, irq_ev_;
    sc_core::sc_signal<bool>   s_mosc_rdy_{"s_mosc_rdy"}, s_mosc_clk_{"s_mosc_clk"};
    sc_core::sc_signal<double> s_mosc_hz_{"s_mosc_hz"};

    // Lo que hay en el E401Y, por clase [H: los bits de cada PPx; DS]
    static uint32_t pp_valor(unsigned c) {
        switch (c) {
            case C_WD: return 0x3;         case C_TIMER: return 0xFF;
            case C_GPIO: return 0x7FFF;    case C_DMA: return 0x1;
            case C_EPI: return 0x1;        case C_HIB: return 0x1;
            case C_UART: return 0xFF;      case C_SSI: return 0xF;
            case C_I2C: return 0x3FF;      case C_USB: return 0x1;
            case C_EPHY: return 0x1;       case C_CAN: return 0x3;
            case C_ADC: return 0x3;        case C_ACMP: return 0x1;
            case C_PWM: return 0x1;        case C_QEI: return 0x1;
            case C_EEPROM: return 0x1;     case C_CCM: return 0x1;
            case C_EMAC: return 0x1;
            default: return 0;
        }
    }

    void valores_de_reset() {
        ris_ = imc_ = 0;
        mosccctl_ = 0x0000000Cu;             // NOXTAL | PWRDN [1F: P-17 e]
        rsclkcfg_ = 0;                       // PIOSC, sin PLL
        memtim0_ = memtim_activo_ = 0x00300030u;   // 0 esperas, FBCE/EBCE, MB1
        altclkcfg_ = 0; dsclkcfg_ = 0;
        pllfreq0_ = pllfreq1_ = 0;
        resbehav_ = 0;
        lock_ = false;
        sr_.fill(0); rcgc_.fill(0); scgc_.fill(0); dcgc_.fill(0);
        for (unsigned c = 0; c < N_CLASES; ++c) pc_[c] = pp_valor(c);
        rcgc_[C_HIB] = 1;                    // la hibernación, con reloj [1F]
        for (auto& f : desde_) for (auto& t : f) t = sc_core::SC_ZERO_TIME;
        otros_.clear();
        mosc.enable(false);
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        pll_ev_.notify(sc_core::SC_ZERO_TIME);
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        avisa();
        if (al_cambiar_periferico) al_cambiar_periferico();
    }
    void avisa() { if (al_cambiar) al_cambiar(); }

    // El MOSC ha arrancado (o ha caído)
    void mosc_proc() {
        if (mosc.is_ready()) { ris_ |= RIS_MOSCPUP; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
        pll_ev_.notify(sc_core::SC_ZERO_TIME);
        avisa();
    }
    // El PLL: engancha T_LOCK después de encenderse o de un NEWFREQ, si su
    // fuente está en marcha
    void pll_proc() {
        for (;;) {
            wait(pll_ev_);
            const bool pwr = (pllfreq0_ >> 23) & 1u;
            const bool fuente = f_pll_in() > 0.0;
            if (lock_) { lock_ = false; avisa(); }
            if (!pwr || !fuente) continue;
            wait(sc_core::sc_time(T_LOCK_S, sc_core::SC_SEC), pll_ev_);
            if (pll_ev_.triggered()) { pll_ev_.notify(sc_core::SC_ZERO_TIME); continue; }
            lock_ = true;
            ris_ |= RIS_PLLL;
            irq_ev_.notify(sc_core::SC_ZERO_TIME);
            avisa();
        }
    }
    void irq_proc() { irq.write((ris_ & imc_) != 0); }

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        t_acc_ = sc_core::sc_time_stamp() + t;
        BusSlave::b_transport(gp, t);
    }

    uint32_t reg_read(uint32_t off) override {
        if (flash && FlashCtrlE4::es_suyo(off)) return flash->lee_sysctl(off);
        switch (off) {
            case R_DID0: return did0_;
            case R_DID1: return did1_;
            case R_RIS:  return ris_;
            case R_IMC:  return imc_;
            case R_MISC: return ris_ & imc_;
            case R_RESC: return resc_;
            case R_MOSCCTL: return mosccctl_;
            case R_RSCLKCFG: return rsclkcfg_ & ~((1u << 31) | (1u << 30));
            case R_MEMTIM0: return memtim0_;
            case R_ALTCLKCFG: return altclkcfg_;
            case R_DSCLKCFG: return dsclkcfg_;
            case R_SYSPROP: return 0x00000001u;     // FPU [H: SYSCTL_SYSPROP_FPU]
            case R_PLLFREQ0: return pllfreq0_;
            case R_PLLFREQ1: return pllfreq1_;
            case R_PLLSTAT: return lock_ ? 1u : 0u;
            case R_RESBEHAVCTL: return resbehav_;
            default: break;
        }
        if (off >= R_PP && off < R_PP + 4 * N_CLASES)   return pp_valor((off - R_PP) / 4);
        if (off >= R_SR && off < R_SR + 4 * N_CLASES)   return sr_[(off - R_SR) / 4];
        if (off >= R_RCGC && off < R_RCGC + 4 * N_CLASES) return rcgc_[(off - R_RCGC) / 4];
        if (off >= R_SCGC && off < R_SCGC + 4 * N_CLASES) return scgc_[(off - R_SCGC) / 4];
        if (off >= R_DCGC && off < R_DCGC + 4 * N_CLASES) return dcgc_[(off - R_DCGC) / 4];
        if (off >= R_PC && off < R_PC + 4 * N_CLASES)   return pc_[(off - R_PC) / 4];
        if (off >= R_PR && off < R_PR + 4 * N_CLASES) {
            const unsigned c = (off - R_PR) / 4;
            uint32_t v = 0;
            for (unsigned b = 0; b < 32; ++b)
                if (listo(c, b, t_acc_)) v |= 1u << b;
            return v;
        }
        if (off >= R_UNIQUEID0 && off < R_UNIQUEID0 + 16)    // sintéticos
            return 0x4D435553u ^ (off * 0x9E3779B9u);
        const auto it = otros_.find(off);
        return it == otros_.end() ? 0u : it->second;
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t ben) override {
        uint32_t m = 0;
        for (unsigned i = 0; i < 4; ++i) if (ben & (1u << i)) m |= 0xFFu << (8 * i);
        auto pon = [&](uint32_t& r) { r = (r & ~m) | (v & m); };
        if (flash && FlashCtrlE4::es_suyo(off)) { flash->escribe_sysctl(off, v); return; }
        switch (off) {
            case R_DID0: case R_DID1: case R_RIS: case R_PLLSTAT: case R_SYSPROP: return;
            case R_IMC:  pon(imc_); irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_MISC: ris_ &= ~(v & m); irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_RESC: resc_ &= (v | ~m); return;     // escribir 0 borra el bit
            case R_MOSCCTL: {
                pon(mosccctl_);
                // MOSC encendido si no está apagado (PWRDN) ni dice que no hay
                // cristal (NOXTAL)
                const bool on = !(mosccctl_ & 0xCu);
                if (!on && mosc.is_ready()) ris_ &= ~RIS_MOSCPUP;
                mosc.enable(on);
                avisa();
                return;
            }
            case R_RSCLKCFG: {
                const uint32_t nuevo = (rsclkcfg_ & ~m) | (v & m);
                if (nuevo & (1u << 31)) memtim_activo_ = memtim0_;      // MEMTIMU
                const bool newfreq = (nuevo >> 30) & 1u;
                rsclkcfg_ = nuevo & ~((1u << 31) | (1u << 30));
                if (newfreq) pll_ev_.notify(sc_core::SC_ZERO_TIME);
                if ((rsclkcfg_ & (1u << 28)) && !lock_) {
                    ++n_av_pll_;
                    SC_REPORT_WARNING("msp432e4/sysctl",
                        "RSCLKCFG.USEPLL con el PLL sin enganchar (PLLSTAT.LOCK = 0): el "
                        "reloj sigue en OSCSRC hasta que enganche. Espera a LOCK antes "
                        "de pasarte al PLL");
                }
                avisa();
                return;
            }
            case R_MEMTIM0: pon(memtim0_); return;    // cuenta con MEMTIMU
            case R_ALTCLKCFG: pon(altclkcfg_); avisa(); return;
            case R_DSCLKCFG: pon(dsclkcfg_); return;
            case R_PLLFREQ0: {
                const uint32_t a = pllfreq0_;
                pon(pllfreq0_);
                // Encender el PLL lo hace engancharse; cambiar M sin NEWFREQ,
                // no (sigue con lo que tenía)
                if (((a ^ pllfreq0_) >> 23) & 1u) pll_ev_.notify(sc_core::SC_ZERO_TIME);
                avisa();
                return;
            }
            case R_PLLFREQ1: pon(pllfreq1_); avisa(); return;
            case R_RESBEHAVCTL: pon(resbehav_); return;
            default: break;
        }
        if (off >= R_SR && off < R_SR + 4 * N_CLASES) {
            pon(sr_[(off - R_SR) / 4]);
            if (al_cambiar_periferico) al_cambiar_periferico();
            return;
        }
        if (off >= R_RCGC && off < R_RCGC + 4 * N_CLASES) {
            const unsigned c = (off - R_RCGC) / 4;
            const uint32_t a = rcgc_[c];
            pon(rcgc_[c]);
            const uint32_t sube = rcgc_[c] & ~a;
            const double f = f_sys();
            const sc_core::sc_time ciclos = f > 0 ? sc_core::sc_time(CICLOS_LISTO / f, sc_core::SC_SEC)
                                                  : sc_core::SC_ZERO_TIME;
            for (unsigned b = 0; b < 32; ++b)
                if (sube & (1u << b)) desde_[c][b] = t_acc_ + ciclos;
            if (rcgc_[c] != a && al_cambiar_periferico) al_cambiar_periferico();
            return;
        }
        if (off >= R_SCGC && off < R_SCGC + 4 * N_CLASES) { pon(scgc_[(off - R_SCGC) / 4]); return; }
        if (off >= R_DCGC && off < R_DCGC + 4 * N_CLASES) { pon(dcgc_[(off - R_DCGC) / 4]); return; }
        if (off >= R_PC && off < R_PC + 4 * N_CLASES)     { pon(pc_[(off - R_PC) / 4]); return; }
        if ((off >= R_PP && off < R_PP + 4 * N_CLASES) || (off >= R_PR && off < R_PR + 4 * N_CLASES))
            return;
        pon(otros_[off]);
    }
    bool responds_without_clock() const override { return true; }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_SYSCTL_H
