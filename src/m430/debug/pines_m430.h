// =============================================================================
// pines_m430.h — Los pines de depuración de un MSP430: JTAG de 4 hilos y
// Spy-Bi-Wire (M2 de doc/analisis_msp430.md, §7.1 y §7.2)
//
// JTAG DE 4 HILOS (TCK, TMS, TDI, TDO): en la subida de TCK el TAP muestrea
// TMS y TDI; TDO cambia en la bajada (lo que se lee tras la subida es el bit
// que se acaba de desplazar). Con el TAP en Run-Test/Idle, TDI es TCLK.
//
// SPY-BI-WIRE (SBWTCK en TEST, SBWTDIO en RST) [JTAG §1.5 y §2.2.3.1; el
// LowLevelFunc430.c de SLAU320 y hil_2w.c de la MSP Debug Stack]:
//   * se entra con un pulso corto de SBWTCK (bajo menos de 7 µs) tras tenerlo
//     alto: ese pulso no es una franja;
//   * cada ciclo de TCK de JTAG son tres franjas de SBWTCK (el dato se toma
//     en su bajada): TMS, TDI y TDO. En la de TDO el chip gobierna SBWTDIO
//     mientras SBWTCK está bajo. Con el TAP en Run-Test/Idle, el valor de la
//     franja de TDI es TCLK;
//   * SBWTCK bajo más de 7 µs saca al chip de Spy-Bi-Wire: el TAP suelta la
//     CPU y el pin RST vuelve a ser RST.
// =============================================================================
#ifndef STM32_M430_DEBUG_PINES_M430_H
#define STM32_M430_DEBUG_PINES_M430_H

#include <systemc>
#include "../../common/analog_net.h"
#include "tap_m430.h"

namespace stm32 {

SC_MODULE(PinesM430) {
    struct Pines {
        analog_net_if* tck = nullptr; analog_net_if* tms = nullptr; analog_net_if* tdi = nullptr;
        analog_net_if* tdo = nullptr;
        analog_net_if* test = nullptr; analog_net_if* rst = nullptr;      // SBWTCK, SBWTDIO
    };
    static constexpr double T_SBW_US = 7.0;

    PinesM430(sc_core::sc_module_name nm, TapM430& tap, const Pines& p, double vdd = 3.3)
        : sc_core::sc_module(nm), tap_(tap), p_(p), vdd_(vdd) {
        SC_HAS_PROCESS(PinesM430);
        if (p_.tck && p_.tms && p_.tdi && p_.tdo) {
            id_tdo_ = p_.tdo->register_driver("m430_tdo");
            p_.tdo->set_hiz(id_tdo_);
            SC_METHOD(tck_proc); sensitive << p_.tck->value_changed_event(); dont_initialize();
            SC_METHOD(tdi_proc); sensitive << p_.tdi->value_changed_event(); dont_initialize();
        }
        if (p_.test && p_.rst) {
            id_sbw_ = p_.rst->register_driver("m430_sbwtdio");
            p_.rst->set_hiz(id_sbw_);
            SC_METHOD(sbwtck_proc); sensitive << p_.test->value_changed_event(); dont_initialize();
            SC_METHOD(sbw_tope_proc); sensitive << ev_tope_; dont_initialize();
        }
    }
    bool en_sbw() const { return sbw_; }
    // Para el SoC: con SBW activo, RST no resetea
    bool rst_es_sbw() const { return sbw_; }
    uint64_t franjas() const { return n_franjas_; }

private:
    TapM430& tap_;
    Pines p_;
    double vdd_;
    int id_tdo_ = -1, id_sbw_ = -1;
    bool tck_ = false, tdi_ = true, sbw_ = false, test_ = false, tms_s_ = false, bit_tdo_ = false;
    unsigned franja_ = 0;
    uint64_t n_franjas_ = 0;
    sc_core::sc_time t_baja_{sc_core::SC_ZERO_TIME}, t_sube_{sc_core::SC_ZERO_TIME};
    sc_core::sc_event ev_tope_;

    bool alto(analog_net_if* n) const { return !n->floating() && n->voltage() > 0.5f * float(vdd_); }
    void pon_tdo(bool b) { p_.tdo->set_drive(id_tdo_, b ? float(vdd_) : 0.0f, 50.0f); }

    // --- 4 hilos ---------------------------------------------------------------
    void tck_proc() {
        const bool v = alto(p_.tck);
        if (v == tck_) return;
        tck_ = v;
        if (sbw_) return;
        if (v) { const bool b = tap_.tdo(); tap_.reloj(alto(p_.tms), alto(p_.tdi)); pon_tdo(b); }
        else pon_tdo(tap_.tdo());
    }
    void tdi_proc() {
        const bool v = alto(p_.tdi);
        if (v == tdi_) return;
        tdi_ = v;
        if (!sbw_) tap_.tdi_en_rti(v);
    }

    // --- Spy-Bi-Wire -----------------------------------------------------------
    void sbwtck_proc() {
        const bool v = alto(p_.test);
        if (v == test_) return;
        test_ = v;
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        if (!v) {                                         // bajada
            t_baja_ = ahora;
            ev_tope_.notify(sc_core::sc_time(T_SBW_US, sc_core::SC_US));
            if (!sbw_) return;
            const bool d = alto(p_.rst);
            ++n_franjas_;
            switch (franja_) {
            case 0: tms_s_ = d; break;
            case 1:
                if (tap_.estado() == TapM430::RTI) tap_.tdi_en_rti(d);
                bit_tdo_ = tap_.tdo();
                tap_.reloj(tms_s_, d);
                break;
            default:
                p_.rst->set_drive(id_sbw_, bit_tdo_ ? float(vdd_) : 0.0f, 50.0f);   // el chip gobierna
                break;
            }
            return;
        }
        // subida
        ev_tope_.cancel();
        if (sbw_) {
            if (franja_ == 2) p_.rst->set_hiz(id_sbw_);
            franja_ = (franja_ + 1) % 3;
            return;
        }
        // Entrada: un pulso bajo corto tras un rato alto
        const double bajo = (ahora - t_baja_).to_seconds() * 1e6, alto_ = (t_baja_ - t_sube_).to_seconds() * 1e6;
        if (bajo < T_SBW_US && alto_ > 20.0) { sbw_ = true; franja_ = 0; tap_.reset(); }
        t_sube_ = ahora;
    }
    void sbw_tope_proc() {
        if (!test_ && sbw_) {
            sbw_ = false;
            p_.rst->set_hiz(id_sbw_);
            tap_.suelta();
        }
    }
};

} // namespace stm32
#endif // STM32_M430_DEBUG_PINES_M430_H
