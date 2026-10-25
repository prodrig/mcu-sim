// =============================================================================
// comp.h — Los siete comparadores del STM32F303xB/C (fase S5)
//
// [RM0316 §17; DS9118 tabla 75 (histéresis); PINDATA (los pines)]
//
// No tienen ventana propia: sus registros, COMP1_CSR..COMP7_CSR, están en la del
// SYSCFG (0x1C-0x34), y el SYSCFG se los delega (`SyscfgF3::analogico`). Este
// bloque es por eso un `sc_module` sin bus: lee y escribe por `lee()` y
// `escribe()`, y calcula.
//
// QUÉ COMPARA. La entrada no inversora es un pin (INPSEL elige entre dos; en
// COMP1, solo PA1), o la del comparador anterior en modo VENTANA (WINMODE de
// COMP2, COMP4 y COMP6: comparten la de COMP1, COMP3 y COMP5). La inversora,
// INMSEL: 1/4, 1/2, 3/4 o toda VREFINT, PA4 o PA5 (las salidas del DAC, que el
// DAC ya pone en su nodo), o uno o dos pines propios [RM0316 §17.3.2, figura 168].
// Las tensiones son las de los NODOS de los pines (`AnalogNet`): lo que ponga
// el banco, un OPAMP o el DAC.
//
// LA SALIDA: con histéresis (baja, media, alta: 8, 15 y 31 mV [DS9118 tabla 75:
// valores entre el típico y el máximo, de modelo]) y POL. Va a cuatro sitios:
//   * el bit OUT (30) del registro;
//   * su línea del EXTI (21, 22, 29, 30, 31, 32 y 33) [RM0316 tabla 33];
//   * los pines COMPx_OUT, por la función alternativa;
//   * los frenos de TIM1 y TIM8 (OUTSEL 0001-0101), que el SoC cablea.
// Las otras salidas (captura de TIM1/2/3, OCREF_CLR) y el BLANKING se guardan y
// avisan una vez [de modelo].
//
// EL TIEMPO: la salida cambia en el mismo instante que la entrada (un delta) [de
// modelo: el DS da de 50 ns en alta velocidad a 2 us en ultrabajo consumo].
// MODE se guarda.
// =============================================================================
#ifndef STM32_PERIPH_COMP_H
#define STM32_PERIPH_COMP_H

#include <cstdio>
#include <functional>
#include <string>
#include <systemc>
#include "../common/analog_net.h"

namespace stm32 {

class CompF3 : public sc_core::sc_module {
public:
    static constexpr unsigned N = 7;
    // La salida de cada comparador, después de POL
    sc_core::sc_signal<bool> out[N];
    // Cambia la configuración (para quien mire OUTSEL)
    sc_core::sc_event cfg_ev;
    // VREFINT y VDDA, del SoC
    std::function<double()> vrefint = [] { return 1.23; };
    std::function<double()> vdda = [] { return 3.3; };

    enum : uint32_t {
        C_EN = 1u << 0, C_INP_DAC = 1u << 1, C_WINMODE = 1u << 9, C_INPSEL = 1u << 7,
        C_POL = 1u << 15, C_OUT = 1u << 30, C_LOCK = 1u << 31
    };
    // Las histéresis [de modelo, DS9118 tabla 75]
    static constexpr double HYST_V[4] = {0.0, 0.008, 0.015, 0.031};

    using Nodo = std::function<analog_net_if&(unsigned puerto, unsigned pin)>;

    CompF3(sc_core::sc_module_name nm, Nodo nodo) : sc_core::sc_module(nm) {
        SC_HAS_PROCESS(CompF3);
        for (unsigned i = 0; i < N; ++i) {
            const P& p = PINES[i];
            inp_[i][0] = p.inp[0] >= 0 ? &nodo(unsigned(p.inp[0]) >> 4, unsigned(p.inp[0]) & 15u) : nullptr;
            inp_[i][1] = p.inp[1] >= 0 ? &nodo(unsigned(p.inp[1]) >> 4, unsigned(p.inp[1]) & 15u) : nullptr;
            inm_[i][0] = p.inm[0] >= 0 ? &nodo(unsigned(p.inm[0]) >> 4, unsigned(p.inm[0]) & 15u) : nullptr;
            inm_[i][1] = p.inm[1] >= 0 ? &nodo(unsigned(p.inm[1]) >> 4, unsigned(p.inm[1]) & 15u) : nullptr;
        }
        pa4_ = &nodo(0, 4); pa5_ = &nodo(0, 5);
        SC_METHOD(evalua);
        sensitive << ev_;
        for (unsigned i = 0; i < N; ++i)
            for (unsigned k = 0; k < 2; ++k) {
                if (inp_[i][k]) sensitive << inp_[i][k]->value_changed_event();
                if (inm_[i][k]) sensitive << inm_[i][k]->value_changed_event();
            }
        sensitive << pa4_->value_changed_event() << pa5_->value_changed_event();
    }

    // ---- Lo que el SYSCFG delega --------------------------------------------------
    uint32_t lee(unsigned i) const { return i < N ? (csr_[i] & ~C_OUT) | (out[i].read() ? C_OUT : 0u) : 0u; }
    void escribe(unsigned i, uint32_t v) {
        if (i >= N || (csr_[i] & C_LOCK)) return;
        csr_[i] = v & mascara(i);
        avisa(i);
        ev_.notify(sc_core::SC_ZERO_TIME);
        cfg_ev.notify(sc_core::SC_ZERO_TIME);
    }
    void reinicia() {
        for (unsigned i = 0; i < N; ++i) { csr_[i] = 0; crudo_[i] = false; }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // ---- Para el SoC y el banco -------------------------------------------------------
    unsigned outsel(unsigned i) const { return i < N ? (csr_[i] >> 10) & 0xFu : 0u; }
    bool activo(unsigned i) const { return i < N && (csr_[i] & C_EN); }
    unsigned conmutaciones(unsigned i) const { return i < N ? n_conm_[i] : 0u; }
    // Las tensiones que está comparando ahora mismo
    double v_inp(unsigned i) const { return i < N ? tension_inp(i) : 0.0; }
    double v_inm(unsigned i) const { return i < N ? tension_inm(i) : 0.0; }

private:
    // Los pines de cada comparador: puerto * 16 + pin, o -1 [PINDATA; RM0316 §17.5]
    struct P { int inp[2]; int inm[2]; };
    static constexpr int A = 0x00, B = 0x10, C = 0x20, D = 0x30, E = 0x40;
    static constexpr P PINES[N] = {
        {{A + 1, -1},      {A + 0, -1}},          // COMP1: PA1;       110 PA0
        {{A + 7, A + 3},   {A + 2, -1}},          // COMP2: PA7/PA3;   110 PA2
        {{B + 14, D + 14}, {D + 15, B + 12}},     // COMP3: PB14/PD14; PD15, PB12
        {{B + 0, E + 7},   {E + 8, B + 2}},       // COMP4: PB0/PE7;   PE8, PB2
        {{D + 12, B + 13}, {D + 13, B + 10}},     // COMP5: PD12/PB13; PD13, PB10
        {{D + 11, B + 11}, {D + 10, B + 15}},     // COMP6: PD11/PB11; PD10, PB15
        {{A + 0, C + 1},   {C + 0, -1}},          // COMP7: PA0/PC1;   PC0
    };
    analog_net_if* inp_[N][2] = {};
    analog_net_if* inm_[N][2] = {};
    analog_net_if* pa4_ = nullptr;
    analog_net_if* pa5_ = nullptr;
    uint32_t csr_[N] = {};
    bool crudo_[N] = {};              // la salida antes de POL (con su histéresis)
    unsigned n_conm_[N] = {};
    bool av_outsel_ = false, av_blank_ = false, av_inm_ = false, av_dac_ = false;
    sc_core::sc_event ev_;

    static uint32_t mascara(unsigned i) {
        // EN, MODE, INMSEL, INPSEL (no en COMP1), OUTSEL, POL, HYST, BLANKING, LOCK;
        // COMP1_INP_DAC solo en COMP1, WINMODE solo en COMP2/4/6
        uint32_t m = C_EN | (3u << 2) | (7u << 4) | (0xFu << 10) | C_POL | (3u << 16) |
                     (7u << 18) | C_LOCK;
        if (i == 0) m |= C_INP_DAC; else m |= C_INPSEL;
        if (i == 1 || i == 3 || i == 5) m |= C_WINMODE;
        return m;
    }
    static double v(const analog_net_if* n) { return n ? double(n->voltage()) : 0.0; }
    double tension_inp(unsigned i) const {
        const uint32_t c = csr_[i];
        if ((i == 1 || i == 3 || i == 5) && (c & C_WINMODE)) return tension_inp(i - 1);
        const unsigned s = (i != 0 && (c & C_INPSEL)) ? 1u : 0u;
        return v(inp_[i][s]);
    }
    double tension_inm(unsigned i) const {
        const unsigned s = (csr_[i] >> 4) & 7u;
        switch (s) {
            case 0: return vrefint() * 0.25;
            case 1: return vrefint() * 0.5;
            case 2: return vrefint() * 0.75;
            case 3: return vrefint();
            case 4: return v(pa4_);
            case 5: return v(pa5_);
            case 6: return v(inm_[i][0]);
            default: return v(inm_[i][1]);
        }
    }
    void evalua() {
        for (unsigned i = 0; i < N; ++i) {
            bool o = false;
            if (csr_[i] & C_EN) {
                const double h = HYST_V[(csr_[i] >> 16) & 3u];
                const double d = tension_inp(i) - tension_inm(i);
                // Con histéresis: sube por encima de +h/2, baja por debajo de -h/2
                if (crudo_[i]) crudo_[i] = d > -h / 2;
                else           crudo_[i] = d > h / 2;
                o = crudo_[i] != bool(csr_[i] & C_POL);
            } else {
                crudo_[i] = false;
            }
            if (o != out[i].read()) { out[i].write(o); ++n_conm_[i]; }
        }
    }
    void avisa(unsigned i) {
        const uint32_t c = csr_[i];
        const unsigned os = (c >> 10) & 0xFu;
        if (os >= 6 && !av_outsel_) {
            av_outsel_ = true;
            aviso("COMPx_OUTSEL hacia una captura o un OCREF_CLR de un temporizador: se guarda, "
                  "pero no llega (solo los frenos de TIM1/TIM8, 0001-0101)");
        }
        if (((c >> 18) & 7u) && !av_blank_) {
            av_blank_ = true;
            aviso("COMPx_BLANKING: se guarda, pero la salida no se enmascara");
        }
        if (i == 6 || i == 0 || i == 1) {
            if (((c >> 4) & 7u) == 7 && !av_inm_) {
                av_inm_ = true;
                aviso("COMPxINMSEL = 111 es reservado en COMP1, COMP2 y COMP7: la inversora queda "
                      "a cero [de modelo]");
            }
        }
        if (i == 0 && (c & C_INP_DAC) && !av_dac_) {
            av_dac_ = true;
            aviso("COMP1_INP_DAC (el interruptor de PA0 a PA4): se guarda, sin efecto en el nodo");
        }
    }
    void aviso(const char* q) {
        const std::string m = std::string(name()) + ": " + q;
        SC_REPORT_WARNING("comp_f3", m.c_str());
    }
};

} // namespace stm32
#endif // STM32_PERIPH_COMP_H
