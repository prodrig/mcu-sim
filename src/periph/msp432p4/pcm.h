// =============================================================================
// pcm.h — El Power Control Manager del MSP432P4
//
// Decide con qué regulador (LDO, DC-DC o el de baja frecuencia) y con qué
// tensión de núcleo (VCORE0, 1,2 V; VCORE1, 1,4 V) trabaja el chip. Importa a
// cualquiera que quiera pasar de 24 MHz: a VCORE0 el tope es 24 MHz, y para
// llegar a 48 hay que subir a VCORE1 ANTES de subir el reloj [SA, tabla 2].
//
// El modelo hace las transiciones de modo activo -las seis de [H,
// PCM_CTL0_AMR__*]- con su tiempo de PMR_BUSY, y rechaza las que el silicio
// rechaza, levantando AM_INVALID_TR_IFG [H]:
//
//   * cambiar a la vez de regulador Y de tensión (hay que ir por pasos: SA da
//     el ejemplo LDO_VCORE0 -> LDO_VCORE1 -> DCDC_VCORE1);
//   * cambiar de tensión fuera del LDO;
//   * pasar entre DC-DC y baja frecuencia sin pasar por el LDO;
//   * bajar a VCORE0 con MCLK por encima de 24 MHz, o entrar en baja
//     frecuencia con MCLK por encima de 128 kHz.
//
// Las reglas de las tres primeras salen del ejemplo de SA y del aviso de TI de
// que las transiciones se hacen por pasos; la lista completa del TRM no se ha
// contrastado todavía [P-16 h]. Los modos de bajo consumo (LPM0/3/4 y los x.5)
// se guardan pero no se modelan: un WFI duerme el núcleo con los relojes en
// marcha, que es LPM0. `limitaciones()` lo dice.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_PCM_H
#define STM32_PERIPH_MSP432P4_PCM_H

#include <functional>
#include "../../common/periph_base.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class PcmP4 : public BusSlave {
public:
    enum : uint32_t { R_CTL0 = 0x00, R_CTL1 = 0x04, R_IE = 0x08, R_IFG = 0x0C,
                      R_CLRIFG = 0x10 };
    static constexpr uint32_t KEY = 0x695Au, KEY_LEIDA = 0xA596u;
    static constexpr uint32_t IFG_AM_INVALID_TR = 1u << 2;
    static constexpr uint32_t CTL1_PMR_BUSY = 1u << 8;

    // La frecuencia de MCLK, para validar las transiciones. La pone el SoC.
    std::function<double()> f_mclk;
    // Cambió el modo en curso (y con él la tensión).
    std::function<void()>   al_cambiar;

    // Cuánto dura una transición [modelo, P-16 h]: el silicio da decenas de
    // microsegundos para el cambio de tensión.
    double t_cambio_vcore_s = 20e-6;
    double t_cambio_reg_s   = 10e-6;

    explicit PcmP4(sc_core::sc_module_name nm) : BusSlave(nm, p4::PCM_B, 0x400) {
        SC_HAS_PROCESS(PcmP4);
        SC_THREAD(transicion_proc);
        SC_METHOD(reset_proc); sensitive << rst_n;
    }

    unsigned cpm()   const { return cpm_; }
    unsigned vcore() const { return cpm_ & 1u; }
    bool     ocupado() const { return busy_; }
    unsigned rechazadas() const { return n_rechazos_; }
    unsigned sin_llave() const { return n_sin_llave_; }

protected:
    unsigned amr_ = 0, lpmr_ = 0, cpm_ = 0, ctl1_ = 0, ie_ = 0, ifg_ = 0;
    bool     busy_ = false;
    unsigned destino_ = 0;
    unsigned n_rechazos_ = 0, n_sin_llave_ = 0;
    sc_core::sc_event ev_;

    static bool am_valido(unsigned m) {
        return m == 0 || m == 1 || m == 4 || m == 5 || m == 8 || m == 9;
    }
    // ¿Puede ir de `de` a `a` de una vez?
    bool transicion_valida(unsigned de, unsigned a) const {
        if (!am_valido(a)) return false;
        const unsigned v_de = de & 1u, v_a = a & 1u;
        const unsigned r_de = de >> 2, r_a = a >> 2;          // 0 LDO, 1 DCDC, 2 LF
        if (v_de != v_a && r_de != r_a) return false;          // las dos a la vez
        if (v_de != v_a && r_de != 0) return false;            // tensión fuera del LDO
        if (r_de != r_a && r_de != 0 && r_a != 0) return false; // DCDC <-> LF
        const double f = f_mclk ? f_mclk() : 0.0;
        if (v_a == 0 && f > p4::tope_mclk(0)) return false;
        if (r_a == 2 && f > 128e3) return false;
        return true;
    }

    void reset_proc() {
        if (rst_n.read()) return;
        amr_ = lpmr_ = cpm_ = ctl1_ = ie_ = ifg_ = 0;
        busy_ = false;
        if (al_cambiar) al_cambiar();
    }

    void transicion_proc() {
        for (;;) {
            wait(ev_);
            const bool cambia_v = (destino_ & 1u) != (cpm_ & 1u);
            wait(sc_core::sc_time(cambia_v ? t_cambio_vcore_s : t_cambio_reg_s,
                                  sc_core::SC_SEC));
            cpm_ = destino_;
            busy_ = false;
            if (al_cambiar) al_cambiar();
        }
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CTL0: return (KEY_LEIDA << 16) | (cpm_ << 8) | (lpmr_ << 4) | amr_;
            case R_CTL1: return (KEY_LEIDA << 16) | (busy_ ? CTL1_PMR_BUSY : 0u) |
                                (ctl1_ & 0x7u);
            case R_IE:   return ie_;
            case R_IFG:  return ifg_;
            default:     return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        switch (off) {
            case R_CTL0:
            case R_CTL1: {
                // Sin la llave en la mitad alta, la escritura no hace nada
                // [H: PCM_CTL0_KEY_VAL = 0x695A0000]. Escribir solo la mitad
                // baja tampoco: la llave no va.
                if ((be & 0xCu) != 0xCu || (v >> 16) != KEY) { ++n_sin_llave_; return; }
                if (off == R_CTL1) { ctl1_ = v & 0x7u; return; }
                if (busy_) return;                          // en plena transición
                lpmr_ = (v >> 4) & 0xFu;
                const unsigned a = v & 0xFu;
                amr_ = a;
                if (a == cpm_) return;
                if (!transicion_valida(cpm_, a)) {
                    ifg_ |= IFG_AM_INVALID_TR;
                    amr_ = cpm_;
                    ++n_rechazos_;
                    return;
                }
                destino_ = a;
                busy_ = true;
                ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case R_IE:     ie_ = v & 0x47u; return;
            case R_CLRIFG: ifg_ &= ~v;      return;
            default:       return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_PCM_H
