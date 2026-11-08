// =============================================================================
// jtag_pines.h — El TAP de un RISC-V en sus pines: TCK, TMS, TDI, TDO y nTRST
//
// E2 de doc/analisis_esp32_riscv.md, §3.6. El lado del OBJETIVO del cable
// JTAG, sobre los nodos analógicos que el chip le dé (en el laboratorio, unos
// nodos sueltos; en el C3, los de MTCK/MTMS/MTDI/MTDO). Es el equivalente
// RISC-V del frente SWD que el Cortex-M tiene en PA13/PA14:
//
//   * en el flanco de subida de TCK muestrea TMS y TDI y mueve el TAP;
//   * en el de bajada saca TDO (y lo suelta fuera de Shift-IR/DR);
//   * nTRST a cero lleva el TAP a Test-Logic-Reset;
//   * TMS, TDI y nTRST tienen sus pull-ups, como pide IEEE 1149.1: sin sonda,
//     el TAP se queda en Test-Logic-Reset.
//
// `activo(false)` lo desconecta (los pines son del chip para otra cosa):
// deja de escuchar y suelta TDO y los pull-ups (en el C3 son GPIO4-7).
// =============================================================================
#ifndef STM32_RV_DEBUG_JTAG_PINES_H
#define STM32_RV_DEBUG_JTAG_PINES_H

#include <systemc>
#include "../../common/analog_net.h"
#include "rv_dtm_jtag.h"

namespace stm32 {

SC_MODULE(JtagPines) {
    struct Pines {
        analog_net_if* tck; analog_net_if* tms; analog_net_if* tdi;
        analog_net_if* tdo; analog_net_if* ntrst;      // ntrst puede faltar
    };

    JtagPines(sc_core::sc_module_name nm, RvDtmJtag& tap, const Pines& p, double vdd = 3.3)
        : sc_core::sc_module(nm), tap_(tap), p_(p), vdd_(vdd) {
        id_tdo_ = p_.tdo->register_driver("jtag_tdo");
        p_.tdo->set_hiz(id_tdo_);
        id_pu_tms_ = p_.tms->register_driver("jtag_tms_pu");
        p_.tms->set_drive(id_pu_tms_, float(vdd_), 50e3f);
        id_pu_tdi_ = p_.tdi->register_driver("jtag_tdi_pu");
        p_.tdi->set_drive(id_pu_tdi_, float(vdd_), 50e3f);
        if (p_.ntrst) {
            id_pu_trst_ = p_.ntrst->register_driver("jtag_ntrst_pu");
            p_.ntrst->set_drive(id_pu_trst_, float(vdd_), 50e3f);
        }
        SC_HAS_PROCESS(JtagPines);
        SC_THREAD(run);
        if (p_.ntrst) { SC_METHOD(trst); sensitive << p_.ntrst->value_changed_event();
                        dont_initialize(); }
    }

    void activo(bool v) {
        activo_ = v;
        if (!v) {
            p_.tdo->set_hiz(id_tdo_);
            p_.tms->set_hiz(id_pu_tms_);
            p_.tdi->set_hiz(id_pu_tdi_);
            if (p_.ntrst) p_.ntrst->set_hiz(id_pu_trst_);
        } else {
            p_.tms->set_drive(id_pu_tms_, float(vdd_), 50e3f);
            p_.tdi->set_drive(id_pu_tdi_, float(vdd_), 50e3f);
            if (p_.ntrst) p_.ntrst->set_drive(id_pu_trst_, float(vdd_), 50e3f);
        }
    }
    bool activo() const { return activo_; }
    uint64_t flancos() const { return n_; }

private:
    RvDtmJtag& tap_;
    Pines p_;
    double vdd_;
    int id_tdo_ = -1, id_pu_tms_ = -1, id_pu_tdi_ = -1, id_pu_trst_ = -1;
    bool activo_ = true;
    uint64_t n_ = 0;

    bool alto(analog_net_if* n) const { return n->voltage() > 0.5 * vdd_; }

    void trst() { if (activo_ && !alto(p_.ntrst)) tap_.trst(); }

    void run() {
        bool prev = alto(p_.tck);
        for (;;) {
            wait(p_.tck->value_changed_event());
            const bool c = alto(p_.tck);
            if (c == prev) continue;
            prev = c;
            if (!activo_) continue;
            if (p_.ntrst && !alto(p_.ntrst)) { tap_.trst(); continue; }
            if (c) {
                ++n_;
                tap_.subida(alto(p_.tms), alto(p_.tdi));
            } else if (tap_.desplazando()) {
                p_.tdo->set_drive(id_tdo_, tap_.tdo() ? float(vdd_) : 0.0f, 30.0f);
            } else {
                p_.tdo->set_hiz(id_tdo_);
            }
        }
    }
};

} // namespace stm32
#endif // STM32_RV_DEBUG_JTAG_PINES_H
