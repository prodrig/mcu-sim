// =============================================================================
// sonda_jtag_rv.h — Una sonda JTAG de bancada para un RISC-V, y el DMI por ella
//
// E2 de doc/analisis_esp32_riscv.md (G2'). El otro extremo del cable de
// rv/debug/jtag_pines.h, como swd_port.h lo es del frente SWD del Cortex-M:
//
//   * `JtagSonda` gobierna TCK, TMS, TDI (y nTRST) y lee TDO, bit a bit y a
//     su ritmo: pone TMS y TDI con TCK bajo, lee TDO, sube TCK, espera, lo
//     baja y espera [IEEE 1149.1; es lo que hace el bitbang de OpenOCD];
//   * `DmiPorJtag` es un dmi_if hecho con ella: IR = dmi, un barrido para la
//     operación y otro (nop) para recoger el resultado, con dmireset y
//     reintento si el DTM dice «ocupado».
//
// Vive en el banco: no ve nada del modelo que no pase por los pines.
// =============================================================================
#ifndef STM32_VERIF_SONDA_JTAG_RV_H
#define STM32_VERIF_SONDA_JTAG_RV_H

#include <systemc>
#include <cstdint>
#include "../common/analog_net.h"
#include "../rv/debug/dmi_if.h"

namespace stm32 {

class JtagSonda {
public:
    struct Pines {
        analog_net_if* tck; analog_net_if* tms; analog_net_if* tdi;
        analog_net_if* tdo; analog_net_if* ntrst;
    };
    JtagSonda(const Pines& p, double hz = 10e6, double vdd = 3.3)
        : p_(p), semi_(0.5 / hz, sc_core::SC_SEC), vdd_(vdd) {
        id_tck_ = p_.tck->register_driver("sonda_tck");
        id_tms_ = p_.tms->register_driver("sonda_tms");
        id_tdi_ = p_.tdi->register_driver("sonda_tdi");
        if (p_.ntrst) id_trst_ = p_.ntrst->register_driver("sonda_ntrst");
        suelta();
    }
    void conectar() {
        conectada_ = true;
        pon(p_.tck, id_tck_, false);
        pon(p_.tms, id_tms_, true);
        pon(p_.tdi, id_tdi_, false);
        if (p_.ntrst) p_.ntrst->set_hiz(id_trst_);
    }
    void suelta() {
        conectada_ = false;
        p_.tck->set_hiz(id_tck_);
        p_.tms->set_hiz(id_tms_);
        p_.tdi->set_hiz(id_tdi_);
        if (p_.ntrst) p_.ntrst->set_hiz(id_trst_);
    }
    bool conectada() const { return conectada_; }
    void pon_hz(double hz) { semi_ = sc_core::sc_time(0.5 / hz, sc_core::SC_SEC); }

    // Un ciclo de TCK. Devuelve TDO, leído antes del flanco de subida
    bool ciclo(bool tms, bool tdi) {
        pon(p_.tms, id_tms_, tms);
        pon(p_.tdi, id_tdi_, tdi);
        sc_core::wait(sc_core::SC_ZERO_TIME);
        const bool o = p_.tdo->voltage() > 0.5 * vdd_;
        pon(p_.tck, id_tck_, true);
        sc_core::wait(semi_);
        pon(p_.tck, id_tck_, false);
        sc_core::wait(semi_);
        ++n_;
        return o;
    }
    // A Test-Logic-Reset (cinco unos) y a Run-Test/Idle
    void reset_tap() { for (int i = 0; i < 5; ++i) ciclo(true, false); ciclo(false, false); }
    void trst() {
        if (!p_.ntrst) return reset_tap();
        p_.ntrst->set_drive(id_trst_, 0.0f, 30.0f);
        sc_core::wait(semi_ * 2);
        p_.ntrst->set_hiz(id_trst_);
        sc_core::wait(semi_ * 2);
        ciclo(false, false);
    }
    void reposo(unsigned n) { for (unsigned i = 0; i < n; ++i) ciclo(false, false); }

    // Desde Run-Test/Idle, un barrido de IR o de DR, y de vuelta a RTI
    uint64_t barre_ir(uint64_t v, unsigned n) { ciclo(true, false); return barre(v, n, true); }
    uint64_t barre_dr(uint64_t v, unsigned n) { return barre(v, n, false); }

    uint64_t ciclos() const { return n_; }

private:
    Pines p_;
    sc_core::sc_time semi_;
    double vdd_;
    int id_tck_ = -1, id_tms_ = -1, id_tdi_ = -1, id_trst_ = -1;
    bool conectada_ = false;
    uint64_t n_ = 0;

    void pon(analog_net_if* n, int id, bool v) { n->set_drive(id, v ? float(vdd_) : 0.0f, 30.0f); }

    uint64_t barre(uint64_t v, unsigned n, bool ir) {
        (void)ir;
        ciclo(true, false);                 // Select-DR (o -IR, si ya se ha pasado por DR)
        ciclo(false, false);                // Capture
        ciclo(false, false);                // Shift
        uint64_t o = 0;
        for (unsigned i = 0; i < n; ++i) {
            const bool ult = i + 1 == n;
            if (ciclo(ult, (v >> i) & 1u)) o |= uint64_t(1) << i;   // el último, a Exit1
        }
        ciclo(true, false);                 // Update
        ciclo(false, false);                // Run-Test/Idle
        return o;
    }
};

// -----------------------------------------------------------------------------
// El DMI por la sonda
// -----------------------------------------------------------------------------
class DmiPorJtag : public dmi_if {
public:
    explicit DmiPorJtag(JtagSonda& s) : s_(s) {}

    // Conecta, lee el IDCODE y dtmcs, y deja el IR en dmi. false si no hay un
    // DTM 0.13 al otro lado.
    bool enganchar() {
        s_.conectar();
        s_.trst();
        s_.reset_tap();
        idcode_ = uint32_t(s_.barre_dr(0, 32));            // tras el reset, IR = IDCODE
        s_.barre_ir(0x10, 5);
        const uint32_t cs = uint32_t(s_.barre_dr(0, 32));
        if ((cs & 0xFu) != 1u) return false;
        abits_ = (cs >> 4) & 0x3Fu;
        idle_ = (cs >> 12) & 7u;
        s_.barre_ir(0x11, 5);
        listo_ = true;
        return true;
    }
    uint32_t idcode() const { return idcode_; }
    unsigned abits() const { return abits_; }
    uint64_t reintentos() const { return n_reint_; }

    bool dmi_leer(uint32_t a, uint32_t& v) override {
        uint64_t r = 0;
        if (!op(a, 0, 1, r)) return false;
        v = uint32_t(r >> 2);
        return true;
    }
    bool dmi_escribir(uint32_t a, uint32_t v) override {
        uint64_t r = 0;
        return op(a, v, 2, r);
    }

private:
    JtagSonda& s_;
    uint32_t idcode_ = 0;
    unsigned abits_ = 7, idle_ = 1;
    bool listo_ = false;
    uint64_t n_reint_ = 0;

    bool op(uint32_t a, uint32_t d, unsigned o, uint64_t& r) {
        if (!listo_ && !enganchar()) return false;
        const unsigned n = abits_ + 34;
        for (unsigned k = 0; k < 8; ++k) {
            s_.barre_dr((uint64_t(a) << 34) | (uint64_t(d) << 2) | o, n);
            s_.reposo(idle_);
            r = s_.barre_dr(0, n);                         // nop: recoge el resultado
            const unsigned st = unsigned(r & 3u);
            if (st == 0) return true;
            ++n_reint_;
            s_.barre_ir(0x10, 5);                          // dmireset
            s_.barre_dr(1u << 16, 32);
            s_.barre_ir(0x11, 5);
            if (st == 2) return false;
            s_.reposo(4u << k);
        }
        return false;
    }
};

} // namespace stm32
#endif // STM32_VERIF_SONDA_JTAG_RV_H
