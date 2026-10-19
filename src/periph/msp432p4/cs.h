// =============================================================================
// cs.h — El Clock System del MSP432P4
//
// Siete fuentes y cinco relojes [H, CS_Type; SYS]:
//
//   fuentes  DCO (1,5 MHz << DCORSEL, ajustado con DCOTUNE y la calibración del
//            TLV), LFXT y HFXT (cristales de la placa), VLO (9,4 kHz),
//            REFO (32 768 Hz o 128 kHz), MODOSC (25 MHz) y SYSOSC (5 MHz)
//   relojes  MCLK (la CPU), HSMCLK y SMCLK (periféricos), ACLK y BCLK (baja
//            frecuencia)
//
// LO QUE HACE QUE ESTO NO SEA UNA TABLA:
//
//   * LA LLAVE. Todo registro del CS salvo KEY ignora las escrituras si en KEY
//     no se ha escrito 0x695A. KEY se lee siempre 0xA596. [H: CS_KEY_VAL]
//   * LOS CRISTALES SON DE LA PLACA. LFXT arranca solo si hay un cristal en
//     LFXIN (PJ.0) y el pin está en su función (PJSEL0.0 = 1); HFXT, igual con
//     HFXIN (PJ.3). Son el `Oscillator` de los STM32 -el mismo código- mirando
//     el nodo del pin. Sin cristal no hay reloj y el flag de fallo no se baja.
//   * EL FALLO CAMBIA LA FUENTE. Si un reloj usa LFXT y LFXT falla, sale de
//     REFO; si usa HFXT y HFXT falla, sale del DCO [H: comentarios de
//     CS_CTL1_SELM__LFXTCLK y __HFXTCLK]. Lo que se pide no es lo que hay.
//   * EL FLAG DE FALLO es pegajoso: CLRIFG lo baja solo si el oscilador ya
//     funciona; si no, vuelve a subir.
//
// Lo que NO hace: el contador de faltas (CTL3), la resistencia externa del DCO
// (DCORES) -se acepta y se usa la calibración ER del TLV, pero no hay pin
// DCOR que mirar-, los tiempos de conmutación entre fuentes, y el gating de
// CLKEN. `limitaciones()` del SoC lo enumera. [P-16 g]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_CS_H
#define STM32_PERIPH_MSP432P4_CS_H

#include <cstring>
#include <functional>
#include "../../common/periph_base.h"
#include "../../rcc/osc_pll.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class CsP4 : public BusSlave {
public:
    enum : uint32_t {
        R_KEY = 0x00, R_CTL0 = 0x04, R_CTL1 = 0x08, R_CTL2 = 0x0C, R_CTL3 = 0x10,
        R_CLKEN = 0x30, R_STAT = 0x34, R_IE = 0x40, R_IFG = 0x48,
        R_CLRIFG = 0x50, R_SETIFG = 0x58, R_DCOERCAL0 = 0x60, R_DCOERCAL1 = 0x64
    };
    static constexpr uint32_t KEY_VAL = 0x695Au, KEY_LEIDA = 0xA596u;
    static constexpr uint32_t IFG_LFXT = 1u << 0, IFG_HFXT = 1u << 1;
    enum Fuente : unsigned { LFXT = 0, VLO = 1, REFO = 2, DCO = 3, MODOSC = 4,
                             HFXT = 5 };

    // Los dos cristales. El SoC les da su nodo (PJ.0 y PJ.3) y su frecuencia
    // nominal, que es un dato de la PLACA: 32 768 Hz y 48 MHz en la LaunchPad.
    Oscillator lfxt{"lfxt"};
    Oscillator hfxt{"hfxt"};
    // Si el pin está en la función del cristal (PJSEL0.0 / PJSEL0.2-3). Lo
    // pone el DIO; sin él, el oscilador no tiene a qué engancharse.
    bool pin_lfxt = false, pin_hfxt = false;

    // Calibración del DCO, del TLV. La pone el SoC al construir.
    uint32_t fcal_ir04 = 400, fcal_ir5 = 400, fcal_er04 = 400, fcal_er5 = 400;
    float    k_ir04 = 1.0f / 632.0f, k_ir5 = 1.0f / 632.0f,
             k_er04 = 1.0f / 632.0f, k_er5 = 1.0f / 632.0f;

    // Avisa al SoC de que algo ha cambiado: frecuencias o fuentes.
    std::function<void()> al_cambiar;

    explicit CsP4(sc_core::sc_module_name nm) : BusSlave(nm, p4::CS_B, 0x400) {
        SC_HAS_PROCESS(CsP4);
        lfxt.ready(s_lf_rdy_); lfxt.clk(s_lf_clk_); lfxt.freq_hz(s_lf_hz_);
        hfxt.ready(s_hf_rdy_); hfxt.clk(s_hf_clk_); hfxt.freq_hz(s_hf_hz_);
        lfxt.needs_source = true; lfxt.nominal_hz = p4::HZ_REFO_L;
        hfxt.needs_source = true; hfxt.nominal_hz = 48e6;
        // Arranques [modelo, sin contrastar: P-16 g]. El de 32 kHz es lento en
        // el silicio -cientos de ms-; aquí se acorta para que una prueba que
        // lo espere no cueste segundos de simulación.
        lfxt.t_startup_s = 5e-3;
        hfxt.t_startup_s = 0.5e-3;
        SC_METHOD(osc_proc);
        sensitive << lfxt.state_event() << hfxt.state_event();
        dont_initialize();
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }

    // --- Lo que el SoC lee ----------------------------------------------------
    // Con el chip en LPM3/LPM4 (`dormido_ = 1`) MCLK, HSMCLK y SMCLK están
    // parados, y ACLK y BCLK solo les llegan al RTC_C y al WDT_A, que son los
    // únicos módulos que funcionan en LPM3 en el MSP432 (no como en el MSP430:
    // un Timer_A en ACLK se para) [TRM 7.4; 1F]; en LPM3.5 (`2`), lo mismo; en
    // LPM4.5 (`3`), nada (T3). `f_mclk_nucleo()` es la del reloj del núcleo,
    // que el núcleo dormido no usa y que sigue puesta para que despierte;
    // `f_aclk_rtc_wdt()`, la de ACLK para esos dos.
    double f_mclk()   const { return dormido_ ? 0.0 : f_mclk_nucleo(); }
    double f_mclk_nucleo() const { return div(f_fuente(sel(0)),  (ctl1_ >> 16) & 7u); }
    double f_hsmclk() const { return dormido_ ? 0.0 : div(f_fuente(sel(4)),  (ctl1_ >> 20) & 7u); }
    double f_smclk()  const { return dormido_ ? 0.0 : div(f_fuente(sel(4)),  (ctl1_ >> 28) & 7u); }
    double f_aclk()   const { return dormido_ ? 0.0 : f_aclk_rtc_wdt(); }
    double f_aclk_rtc_wdt() const {
        return dormido_ >= 3 ? 0.0 : div(f_fuente_lf(sel(8)), (ctl1_ >> 24) & 7u);
    }
    double f_bclk()   const {
        if (dormido_ >= 3) return 0.0;
        return ((ctl1_ >> 12) & 1u) ? f_refo() : f_fuente_lf(LFXT);
    }
    bool osc_modulo_on() const { return dormido_ == 0; }      // MODOSC y SYSOSC
    void duerme(unsigned nivel) {
        if (nivel == dormido_) return;
        dormido_ = nivel;
        avisa();
    }
    unsigned dormido() const { return dormido_; }
    double f_vlo()    const { return p4::HZ_VLO; }
    double f_dco()    const {
        const unsigned rsel = (ctl0_ >> 16) & 7u;
        const double centro = p4::HZ_DCO_BASE * double(1u << (rsel > 5 ? 5 : rsel));
        int tune = int(ctl0_ & 0x3FFu);
        if (tune & 0x200) tune -= 0x400;                     // 10 bits con signo
        if (tune == 0) return centro;
        const bool er = (ctl0_ >> 22) & 1u;
        const bool r5 = rsel >= 5;
        const float  k = er ? (r5 ? k_er5 : k_er04) : (r5 ? k_ir5 : k_ir04);
        const uint32_t fc = er ? (r5 ? fcal_er5 : fcal_er04) : (r5 ? fcal_ir5 : fcal_ir04);
        // La fórmula de TI [SYS: calculate_DCO_clock]
        const double den = 1.0 / double(k) + 768.0 - double(fc);
        return centro / (1.0 - double(tune) / den);
    }
    uint32_t ctl0() const { return ctl0_; }
    uint32_t ctl1() const { return ctl1_; }
    uint32_t ifg()  const { return ifg_; }
    bool bloqueado() const { return key_ != KEY_VAL; }

protected:
    unsigned dormido_ = 0;
    uint32_t key_ = 0, ctl0_ = 0, ctl1_ = 0, ctl2_ = 0, ctl3_ = 0, clken_ = 0,
             ie_ = 0, ifg_ = 0, ercal0_ = 0, ercal1_ = 0;
    sc_core::sc_signal<bool>   s_lf_rdy_{"s_lf_rdy"}, s_lf_clk_{"s_lf_clk"},
                               s_hf_rdy_{"s_hf_rdy"}, s_hf_clk_{"s_hf_clk"};
    sc_core::sc_signal<double> s_lf_hz_{"s_lf_hz"}, s_hf_hz_{"s_hf_hz"};

    // [1F: valores de reset del TRM, SLAU356 §6.3; P-16 g] DCORSEL = 1 (3 MHz);
    // MCLK, HSMCLK y SMCLK del DCO; ACLK y BCLK de LFXT, que al no estar
    // arrancado cae a REFO; los cuatro relojes habilitados.
    void valores_de_reset() {
        key_ = 0;
        ctl0_ = 0x00010000u;
        ctl1_ = 0x00000033u;
        ctl2_ = 0x00010003u;
        ctl3_ = 0x00000BBBu;
        clken_ = 0x0000000Fu;
        ie_ = 0;
        ifg_ = IFG_LFXT;                 // LFXT no está en marcha: en fallo
        ercal0_ = ercal1_ = 0;
        aplica_osciladores();
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        avisa();
    }

    unsigned sel(unsigned sh) const { return (ctl1_ >> sh) & 7u; }
    static double div(double f, unsigned d) { return f / double(1u << d); }
    double f_refo() const {
        return (clken_ & (1u << 15)) ? p4::HZ_REFO_H : p4::HZ_REFO_L;
    }
    // Las de alta frecuencia (MCLK, HSMCLK, SMCLK)
    double f_fuente(unsigned s) const {
        switch (s) {
            case LFXT:   return f_fuente_lf(LFXT);
            case VLO:    return p4::HZ_VLO;
            case REFO:   return f_refo();
            case DCO:    return f_dco();
            case MODOSC: return p4::HZ_MODOSC;
            case HFXT:   return hfxt.is_ready() ? hfxt.out_hz() : f_dco();
            default:     return f_dco();               // HFXT2 y reservado
        }
    }
    // Las de baja frecuencia (ACLK, BCLK): LFXT, VLO o REFO
    double f_fuente_lf(unsigned s) const {
        switch (s) {
            case LFXT: return lfxt.is_ready() ? lfxt.out_hz() : f_refo();
            case VLO:  return p4::HZ_VLO;
            default:   return f_refo();
        }
    }

    void aplica_osciladores() {
        lfxt.enable(((ctl2_ >> 8) & 1u) && pin_lfxt);
        hfxt.enable(((ctl2_ >> 24) & 1u) && pin_hfxt);
        lfxt.bypass = (ctl2_ >> 9) & 1u;
        hfxt.bypass = (ctl2_ >> 25) & 1u;
    }
    // Un oscilador ha cambiado de estado: el flag de fallo lo refleja.
    void osc_proc() {
        if (!lfxt.is_ready()) ifg_ |= IFG_LFXT;
        if (!hfxt.is_ready() && ((ctl2_ >> 24) & 1u)) ifg_ |= IFG_HFXT;
        avisa();
    }
    void avisa() { if (al_cambiar) al_cambiar(); }

public:
    // El DIO cambia la función de PJ: el cristal gana o pierde su pin.
    void pines_cristal(bool lf, bool hf) {
        if (lf == pin_lfxt && hf == pin_hfxt) return;
        pin_lfxt = lf; pin_hfxt = hf;
        aplica_osciladores();
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_KEY:   return KEY_LEIDA;
            case R_CTL0:  return ctl0_;
            case R_CTL1:  return ctl1_;
            case R_CTL2:  return ctl2_;
            case R_CTL3:  return ctl3_;
            case R_CLKEN: return clken_;
            case R_STAT:  return stat();
            case R_IE:    return ie_;
            case R_IFG:   return ifg_;
            case R_DCOERCAL0: return ercal0_;
            case R_DCOERCAL1: return ercal1_;
            default:      return 0;
        }
    }
    // Quién está encendido y qué relojes están listos. Todo listo: el modelo
    // conmuta al instante (P-16 g).
    uint32_t stat() const {
        uint32_t s = 0;
        s |= 1u << 0 | 1u << 1;                          // DCO_ON, DCOBIAS_ON
        if (hfxt.is_ready()) s |= 1u << 2;
        s |= 1u << 4 | 1u << 5;                          // MODOSC_ON, VLO_ON
        if (lfxt.is_ready()) s |= 1u << 6;
        s |= 1u << 7;                                    // REFO_ON
        s |= 0x1Fu << 16;                                // ACLK..MODCLK_ON
        s |= 0x1Fu << 24;                                // *_READY
        return s;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        if (off == R_KEY) { key_ = (key_ & ~m & 0xFFFFu) | (v & m & 0xFFFFu); return; }
        if (bloqueado()) { ++n_bloqueadas_; return; }   // sin llave no se escribe
        auto mezcla = [&](uint32_t cur) { return (cur & ~m) | (v & m); };
        switch (off) {
            case R_CTL0:  ctl0_  = mezcla(ctl0_)  & 0x00C703FFu; break;
            case R_CTL1:  ctl1_  = mezcla(ctl1_)  & 0x77771777u; break;
            case R_CTL2:  ctl2_  = mezcla(ctl2_)  & 0x03710303u;
                          aplica_osciladores();                  break;
            case R_CTL3:  ctl3_  = mezcla(ctl3_)  & 0x0000FFFFu; break;
            case R_CLKEN: clken_ = mezcla(clken_) & 0x0000870Fu; break;
            case R_IE:    ie_    = mezcla(ie_);                  break;
            case R_CLRIFG: {
                const uint32_t c = v & m;
                ifg_ &= ~c;
                // El fallo es pegajoso: vuelve a subir si el oscilador no va.
                if ((c & IFG_LFXT) && !lfxt.is_ready()) ifg_ |= IFG_LFXT;
                if ((c & IFG_HFXT) && !hfxt.is_ready() && ((ctl2_ >> 24) & 1u))
                    ifg_ |= IFG_HFXT;
                break;
            }
            case R_SETIFG: ifg_ |= (v & m);                      break;
            case R_DCOERCAL0: ercal0_ = mezcla(ercal0_);         break;
            case R_DCOERCAL1: ercal1_ = mezcla(ercal1_);         break;
            default: return;
        }
        avisa();
    }

public:
    // Escrituras rechazadas por no haber puesto la llave: lo mira el banco.
    unsigned escrituras_sin_llave() const { return n_bloqueadas_; }
private:
    unsigned n_bloqueadas_ = 0;
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_CS_H
