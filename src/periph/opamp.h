// =============================================================================
// opamp.h — Los cuatro amplificadores operacionales del STM32F303xB/C (fase S5)
//
// [RM0316 §18; DS9118 tabla 76; PINDATA (los pines)]
//
// Como los comparadores, sin ventana propia: OPAMP1_CSR..OPAMP4_CSR están en la
// del SYSCFG (0x38-0x44) y el SYSCFG los delega.
//
// LAS ENTRADAS: VP_SEL elige la no inversora entre cuatro pines y VM_SEL la
// inversora entre dos, o el modo: 10 PGA (la realimentación interna, con la
// ganancia de PGA_GAIN: 2, 4, 8 o 16) y 11 seguidor [RM0316 §18.3.3-18.3.5].
// La salida VOUT es un pin fijo, que es a la vez una entrada de un ADC: PA2
// (ADC1_IN3), PA6 (ADC2_IN3), PB1 (ADC3_IN1) y PB12 (ADC4_IN3) [figura 172].
//
// EL MODELO DEL AMPLIFICADOR: un driver Thevenin de 10 ohm en el nodo de VOUT,
// de raíl a raíl (0..VDDA):
//   * seguidor y PGA: la salida es VP o G*VP, recortada, al instante [de modelo:
//     el DS da un tiempo de establecimiento de unos microsegundos];
//   * autónomo (VM_SEL = 00/01), con la red que ponga el banco fuera: la salida
//     integra el error con el producto ganancia-ancho de banda (8,2 MHz) y se
//     limita a 4,7 V/us de pendiente [DS9118 tabla 76], en pasos de 20 ns
//     mientras no esté quieta; así cierra cualquier lazo de resistencias.
//
// LA CALIBRACIÓN: con CALON las dos entradas van a VREFOPAMP (CALSEL: 3,3, 10,
// 50 o 90 % de VDDA) y OUTCAL dice hacia dónde está el error. El modelo pone el
// cero de cada par diferencial en 0x10 [de modelo]: OUTCAL = 1 mientras el
// recorte del par que se calibra (TRIMOFFSETN con CALSEL 00/01, TRIMOFFSETP con
// 10/11) está por debajo, que es lo que espera el algoritmo de ST (sube el
// recorte con OUTCAL = 1). Sin USER_TRIM, los de fábrica (0x10): desplazamiento
// cero. Con USER_TRIM, 0,3 mV por paso fuera de 0x10, sumados [de modelo].
//
// VREFOPAMP (con TSTREF = 0) va a un canal interno del ADC: IN15 del ADC1, IN17
// del ADC2/3/4. FORCE_VP la lleva a la entrada no inversora.
//
// LO QUE NO SE MODELA: el modo de multiplexor por temporizador (TCM_EN: lo
// conmuta el OC6 del TIM1/TIM8/TIM20) se guarda y avisa; la realimentación
// interna hacia VM0/VM1 de PGA_GAIN 1x se toma como la PGA sin el pin; el
// ruido, la corriente de salida máxima y el tiempo de arranque.
// =============================================================================
#ifndef STM32_PERIPH_OPAMP_H
#define STM32_PERIPH_OPAMP_H

#include <cmath>
#include <functional>
#include <string>
#include <systemc>
#include "../common/analog_net.h"

namespace stm32 {

class OpampF3 : public sc_core::sc_module {
public:
    static constexpr unsigned N = 4;
    std::function<double()> vdda = [] { return 3.3; };

    enum : uint32_t {
        O_EN = 1u << 0, O_FORCE_VP = 1u << 1, O_TCM_EN = 1u << 7, O_CALON = 1u << 11,
        O_USER_TRIM = 1u << 18, O_TSTREF = 1u << 29, O_OUTCAL = 1u << 30, O_LOCK = 1u << 31
    };
    static constexpr uint32_t TRIM_FABRICA = 0x10;
    // El reset deja los recortes de fábrica a la vista [RM0316: 0xXXXX 0000]
    static constexpr uint32_t CSR_RESET = (TRIM_FABRICA << 24) | (TRIM_FABRICA << 19);
    static constexpr double GBW_HZ = 8.2e6, SLEW_V_S = 4.7e6, R_OUT = 10.0, PASO_S = 20e-9;

    using Nodo = std::function<analog_net_if&(unsigned puerto, unsigned pin)>;

    OpampF3(sc_core::sc_module_name nm, Nodo nodo) : sc_core::sc_module(nm) {
        SC_HAS_PROCESS(OpampF3);
        for (unsigned i = 0; i < N; ++i) {
            const P& p = PINES[i];
            for (unsigned k = 0; k < 4; ++k) vp_[i][k] = &nodo(p.vp[k] >> 4, p.vp[k] & 15u);
            for (unsigned k = 0; k < 2; ++k) vm_[i][k] = &nodo(p.vm[k] >> 4, p.vm[k] & 15u);
            out_[i] = &nodo(p.vout >> 4, p.vout & 15u);
            static const char* nom[N] = {"opamp1", "opamp2", "opamp3", "opamp4"};
            id_[i] = out_[i]->register_driver(nom[i]);
            out_[i]->set_hiz(id_[i]);
            csr_[i] = CSR_RESET;
        }
        SC_THREAD(motor0); SC_THREAD(motor1); SC_THREAD(motor2); SC_THREAD(motor3);
    }

    // ---- Lo que el SYSCFG delega --------------------------------------------------
    uint32_t lee(unsigned i) const {
        if (i >= N) return 0;
        return (csr_[i] & ~O_OUTCAL) | (outcal(i) ? O_OUTCAL : 0u);
    }
    void escribe(unsigned i, uint32_t v) {
        if (i >= N || (csr_[i] & O_LOCK)) return;
        csr_[i] = v & 0xBFFFFFEFu;                   // OUTCAL es de lectura; el bit 4, reservado
        if ((v & O_TCM_EN) && !av_tcm_) {
            av_tcm_ = true;
            const std::string m = std::string(name()) + ": TCM_EN (el multiplexor por "
                                  "temporizador) se guarda, pero las entradas no conmutan";
            SC_REPORT_WARNING("opamp_f3", m.c_str());
        }
        ev_[i].notify(sc_core::SC_ZERO_TIME);
    }
    void reinicia() {
        for (unsigned i = 0; i < N; ++i) { csr_[i] = CSR_RESET; ev_[i].notify(sc_core::SC_ZERO_TIME); }
    }

    // ---- Para el SoC y el banco -------------------------------------------------------
    // VREFOPAMPx hacia el ADC (0 si TSTREF = 1 o el amplificador está apagado)
    double vrefopamp(unsigned i) const {
        if (i >= N || !(csr_[i] & O_EN) || (csr_[i] & O_TSTREF)) return 0.0;
        return vref_cal(i);
    }
    double v_out(unsigned i) const { return i < N ? vo_[i] : 0.0; }
    bool activo(unsigned i) const { return i < N && (csr_[i] & O_EN); }
    unsigned pasos(unsigned i) const { return i < N ? n_pasos_[i] : 0u; }

private:
    struct P { unsigned vp[4]; unsigned vm[2]; unsigned vout; };
    static constexpr unsigned A = 0x00, B = 0x10, C = 0x20, D = 0x30;
    // vp[] indexado por VP_SEL, vm[] por VM_SEL [RM0316 §18.4]
    static constexpr P PINES[N] = {
        {{A + 7, A + 5, A + 3, A + 1},       {C + 5, A + 3},   A + 2},     // OPAMP1
        {{D + 14, B + 14, B + 0, A + 7},     {C + 5, A + 5},   A + 6},     // OPAMP2
        {{B + 13, A + 5, A + 1, B + 0},      {B + 10, B + 2},  B + 1},     // OPAMP3
        {{D + 11, B + 11, A + 4, B + 13},    {B + 10, D + 8},  B + 12},    // OPAMP4
    };
    analog_net_if* vp_[N][4] = {};
    analog_net_if* vm_[N][2] = {};
    analog_net_if* out_[N] = {};
    int id_[N] = {};
    uint32_t csr_[N] = {};
    double vo_[N] = {};
    bool conduce_[N] = {};
    unsigned n_pasos_[N] = {};
    bool av_tcm_ = false;
    sc_core::sc_event ev_[N];

    double vref_cal(unsigned i) const {
        static const double f[4] = {0.033, 0.10, 0.50, 0.90};
        return f[(csr_[i] >> 12) & 3u] * vdda();
    }
    unsigned trim_p(unsigned i) const { return (csr_[i] & O_USER_TRIM) ? (csr_[i] >> 19) & 0x1Fu : TRIM_FABRICA; }
    unsigned trim_n(unsigned i) const { return (csr_[i] & O_USER_TRIM) ? (csr_[i] >> 24) & 0x1Fu : TRIM_FABRICA; }
    bool outcal(unsigned i) const {
        if (!(csr_[i] & O_CALON)) return false;
        const unsigned cs = (csr_[i] >> 12) & 3u;
        return (cs < 2 ? trim_n(i) : trim_p(i)) < TRIM_FABRICA;
    }
    double desplazamiento(unsigned i) const {
        return 0.3e-3 * ((double(trim_p(i)) - TRIM_FABRICA) + (double(trim_n(i)) - TRIM_FABRICA));
    }
    double vp(unsigned i) const {
        if (csr_[i] & (O_FORCE_VP | O_CALON)) return vref_cal(i);
        return double(vp_[i][(csr_[i] >> 2) & 3u]->voltage());
    }
    void conduce(unsigned i, double v) {
        const double va = vdda();
        if (v < 0) v = 0;
        if (v > va) v = va;
        vo_[i] = v;
        out_[i]->set_drive(id_[i], float(v), float(R_OUT));
        conduce_[i] = true;
    }

    void motor0() { motor(0); }
    void motor1() { motor(1); }
    void motor2() { motor(2); }
    void motor3() { motor(3); }
    void motor(unsigned i) {
        for (;;) {
            const uint32_t c = csr_[i];
            const unsigned vms = (c >> 5) & 3u;
            sc_core::sc_event_or_list espera;
            espera |= ev_[i];
            if (!(c & O_EN)) {
                if (conduce_[i]) { out_[i]->set_hiz(id_[i]); conduce_[i] = false; vo_[i] = 0; }
                wait(espera);
                continue;
            }
            for (unsigned k = 0; k < 4; ++k) espera |= vp_[i][k]->value_changed_event();
            if (vms == 3 || vms == 2) {
                // Seguidor y PGA: al instante
                const double g = vms == 3 ? 1.0 : double(2u << ((c >> 14) & 3u));
                conduce(i, (vp(i) + desplazamiento(i)) * g);
                wait(espera);
                continue;
            }
            // Autónomo: el lazo lo cierra lo de fuera
            const double vm = double(vm_[i][vms]->voltage());
            const double e = vp(i) + desplazamiento(i) - vm;
            double dv = 2.0 * M_PI * GBW_HZ * PASO_S * e;
            const double lim = SLEW_V_S * PASO_S;
            if (dv > lim) dv = lim;
            if (dv < -lim) dv = -lim;
            const double nuevo = vo_[i] + dv;
            const bool satura = (nuevo <= 0 && vo_[i] <= 0) || (nuevo >= vdda() && vo_[i] >= vdda());
            if (std::fabs(dv) < 1e-7 || satura) {
                if (!conduce_[i]) conduce(i, vo_[i]);
                espera |= vm_[i][0]->value_changed_event();
                espera |= vm_[i][1]->value_changed_event();
                wait(espera);
                continue;
            }
            conduce(i, nuevo);
            ++n_pasos_[i];
            wait(sc_core::sc_time(PASO_S, sc_core::SC_SEC), ev_[i]);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_OPAMP_H
