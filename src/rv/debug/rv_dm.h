// =============================================================================
// rv_dm.h — El Debug Module de RISC-V [Debug Spec 0.13.2, capítulo 3]
//
// E2 de doc/analisis_esp32_riscv.md, §3.6. Genérico: no sabe de qué chip es.
// Lo que cambia de uno a otro (cuántos datos, cuánto program buffer, si hay
// SBA y de qué anchos, impebreak) sale de los RvDebugCaps del primer hart.
//
//   * dmcontrol, dmstatus, hartinfo, haltsum0/1, abstractcs, command,
//     abstractauto, data0-n y progbuf0-n;
//   * comandos abstractos de ACCESO A REGISTRO (GPR y CSR; los FPR no existen
//     en estos núcleos), con aarpostincrement y postexec;
//   * el program buffer, que el hart ejecuta en modo depuración;
//   * SBA (sbcs, sbaddress0, sbdata0) con sbreadonaddr, sbreadondata y
//     sbautoincrement: un maestro más del bus (`sba`), con RvExt::depurador;
//   * ndmreset (con un gancho: qué resetea es cosa del SoC) y hartreset;
//   * varios harts con hartsel (sin hart array), incluido «unavail»: un hart
//     que existe pero no se puede depurar ahora (el LP del C6 dormido);
//   * resethaltreq: parar en la primera instrucción después de un reset;
//   * y el DISPARO CRUZADO (opcional, `pon_disparo_cruzado`): cuando un hart
//     se para, el DM pide parar a los demás. La 0.13.2 no lo define (la 1.0
//     tiene los grupos de parada); el C6 lo tiene en registros propios
//     [TRM-C6 §1.13], que lo encenderán por aquí.
//
// Las operaciones son instantáneas: abstractcs.busy y sbcs.sbbusy siempre se
// leen 0. Un comando abstracto con el hart corriendo da cmderr = 4.
// =============================================================================
#ifndef STM32_RV_DEBUG_RV_DM_H
#define STM32_RV_DEBUG_RV_DM_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <functional>
#include <vector>
#include "dmi_if.h"
#include "../hart/nucleo_rv.h"

namespace stm32 {

SC_MODULE(RvDm), public dmi_if {
    // El acceso al bus del sistema (SBA)
    tlm_utils::simple_initiator_socket<RvDm> sba{"sba"};

    // Qué hace ndmreset en este SoC: true al ponerlo, false al quitarlo
    std::function<void(bool)> ndmreset;
    // ¿Se puede depurar ahora el hart h? (false = «unavail»)
    std::function<bool(unsigned)> disponible;

    RvDm(sc_core::sc_module_name nm, const std::vector<NucleoRv*>& harts)
        : sc_core::sc_module(nm), harts_(harts),
          dc_(harts.at(0)->hart.caps().dbg), h_(harts.size()) {
        unsigned b = 0;
        while ((1u << b) < harts_.size()) ++b;
        hartsellen_ = b ? b : 1;
        SC_HAS_PROCESS(RvDm);
        SC_METHOD(al_parar);
        for (NucleoRv* n : harts_) sensitive << n->ev_parado();
        dont_initialize();
        reset_dm();
    }

    void pon_disparo_cruzado(bool v) { cruzado_ = v; }
    bool disparo_cruzado() const     { return cruzado_; }
    unsigned n_harts() const         { return unsigned(harts_.size()); }
    uint64_t accesos() const         { return n_acc_; }
    uint64_t accesos_sba() const     { return n_sba_; }

    // =========================================================================
    // dmi_if
    // =========================================================================
    bool dmi_leer(uint32_t a, uint32_t& v) override {
        ++n_acc_;
        v = lee(a);
        return true;
    }
    bool dmi_escribir(uint32_t a, uint32_t v) override {
        ++n_acc_;
        escribe(a, v);
        return true;
    }

private:
    struct EstadoHart {
        bool haltreq = false, resumeack = false, havereset = true, resethaltreq = false;
    };
    std::vector<NucleoRv*> harts_;
    const RvDebugCaps& dc_;
    std::vector<EstadoHart> h_;
    unsigned hartsellen_ = 1;
    bool cruzado_ = false;
    uint64_t n_acc_ = 0, n_sba_ = 0;

    bool dmactive_ = false, ndmreset_ = false;
    uint32_t hartsel_ = 0;
    uint32_t data_[12] = {}, progbuf_[16] = {};
    uint32_t command_ = 0, abstractauto_ = 0, cmderr_ = 0;
    // SBA
    uint32_t sbaccess_ = 2, sberror_ = 0, sbaddress_ = 0, sbdata_ = 0;
    bool sbreadonaddr_ = false, sbreadondata_ = false, sbautoinc_ = false, sbbusyerror_ = false;

    void reset_dm() {
        dmactive_ = false;
        ndmreset_ = false;
        hartsel_ = 0;
        for (uint32_t& d : data_) d = 0;
        for (uint32_t& p : progbuf_) p = 0;
        command_ = abstractauto_ = cmderr_ = 0;
        sbaccess_ = 2; sberror_ = 0; sbaddress_ = sbdata_ = 0;
        sbreadonaddr_ = sbreadondata_ = sbautoinc_ = sbbusyerror_ = false;
        for (unsigned i = 0; i < h_.size(); ++i) {
            if (h_[i].haltreq) harts_[i]->pide_parada(false);
            if (h_[i].resethaltreq) harts_[i]->hart.pide_parada_al_reset(false);
            const bool hr = h_[i].havereset;
            h_[i] = EstadoHart{};
            h_[i].havereset = hr;
        }
    }

    bool existe(uint32_t h) const { return h < harts_.size(); }
    bool dispo(uint32_t h) const {
        return existe(h) && (!disponible || disponible(h)) && !harts_[h]->en_reset();
    }
    bool parado(uint32_t h) const { return dispo(h) && harts_[h]->parado(); }

    // =========================================================================
    // Lectura
    // =========================================================================
    uint32_t lee(uint32_t a) {
        using namespace dm;
        if (a == DMCONTROL) {
            return (hartsel_ & 0x3FFu) << 16 | ((hartsel_ >> 10) & 0x3FFu) << 6 |
                   (ndmreset_ ? NDMRESET : 0u) | (dmactive_ ? DMACTIVE : 0u);
        }
        if (a == DMSTATUS) return dmstatus();
        if (!dmactive_) return 0;
        const unsigned nd = dc_.datacount < 12u ? dc_.datacount : 12u;
        const unsigned np = dc_.progbuf < 16u ? dc_.progbuf : 16u;
        if (a >= DATA0 && a < DATA0 + nd) {
            const uint32_t v = data_[a - DATA0];
            autoexec((abstractauto_ >> (a - DATA0)) & 1u);
            return v;
        }
        if (a >= PROGBUF0 && a < PROGBUF0 + np) {
            const uint32_t v = progbuf_[a - PROGBUF0];
            autoexec((abstractauto_ >> (16 + a - PROGBUF0)) & 1u);
            return v;
        }
        switch (a) {
        case HARTINFO: return 0;              // sin datos en memoria ni dscratch visibles
        case ABSTRACTCS:
            return (np << 24) | (cmderr_ << 8) | nd;
        case COMMAND: return 0;               // solo de escritura
        case ABSTRACTAUTO: return abstractauto_;
        case HALTSUM0: {
            uint32_t s = 0;
            const uint32_t base = hartsel_ & ~0x1Fu;
            for (unsigned i = 0; i < 32; ++i) if (parado(base + i)) s |= 1u << i;
            return s;
        }
        case HALTSUM1: {
            uint32_t s = 0;
            for (unsigned i = 0; i < harts_.size(); ++i) if (parado(i)) s |= 1u << (i / 32);
            return s & 1u;
        }
        case SBCS:
            if (!dc_.sba) return 0;
            return (1u << 29) | (sbbusyerror_ ? SB_BUSYERROR : 0u) |
                   (sbreadonaddr_ ? SB_READONADDR : 0u) | (sbaccess_ << 17) |
                   (sbautoinc_ ? SB_AUTOINC : 0u) | (sbreadondata_ ? SB_READONDATA : 0u) |
                   (sberror_ << 12) | (32u << 5) | (dc_.sba_anchos & 0x1Fu);
        case SBADDRESS0: return dc_.sba ? sbaddress_ : 0;
        case SBDATA0: {
            if (!dc_.sba) return 0;
            const uint32_t v = sbdata_;
            if (sbreadondata_ && !sberror_ && !sbbusyerror_) sb_lee();
            return v;
        }
        default: return 0;
        }
    }

    uint32_t dmstatus() const {
        using namespace dm;
        uint32_t s = (dc_.impebreak ? IMPEBREAK : 0u) | AUTHENTICATED | HASRESETHALTREQ | 2u;
        if (!dmactive_) return s;
        const uint32_t h = hartsel_;
        if (!existe(h)) return s | ALLNONEXISTENT | ANYNONEXISTENT;
        if (h_[h].havereset) s |= ALLHAVERESET | ANYHAVERESET;
        if (h_[h].resumeack) s |= ALLRESUMEACK | ANYRESUMEACK;
        if (!dispo(h)) return s | ALLUNAVAIL | ANYUNAVAIL;
        if (harts_[h]->parado()) s |= ALLHALTED | ANYHALTED;
        else                     s |= ALLRUNNING | ANYRUNNING;
        return s;
    }

    // =========================================================================
    // Escritura
    // =========================================================================
    void escribe(uint32_t a, uint32_t v) {
        using namespace dm;
        if (a == DMCONTROL) return escribe_dmcontrol(v);
        if (!dmactive_) return;
        const unsigned nd = dc_.datacount < 12u ? dc_.datacount : 12u;
        const unsigned np = dc_.progbuf < 16u ? dc_.progbuf : 16u;
        if (a >= DATA0 && a < DATA0 + nd) {
            data_[a - DATA0] = v;
            autoexec((abstractauto_ >> (a - DATA0)) & 1u);
            return;
        }
        if (a >= PROGBUF0 && a < PROGBUF0 + np) {
            progbuf_[a - PROGBUF0] = v;
            autoexec((abstractauto_ >> (16 + a - PROGBUF0)) & 1u);
            return;
        }
        switch (a) {
        case ABSTRACTCS:
            cmderr_ &= ~((v >> 8) & 7u);                       // W1C
            return;
        case COMMAND:
            if (cmderr_) return;
            command_ = v;
            ejecuta();
            return;
        case ABSTRACTAUTO:
            abstractauto_ = v & ((((1u << np) - 1u) << 16) | ((1u << nd) - 1u));
            return;
        case SBCS:
            if (!dc_.sba) return;
            if (v & SB_BUSYERROR) sbbusyerror_ = false;
            sberror_ &= ~((v >> 12) & 7u);
            sbreadonaddr_ = (v & SB_READONADDR) != 0;
            sbaccess_ = (v >> 17) & 7u;
            sbautoinc_ = (v & SB_AUTOINC) != 0;
            sbreadondata_ = (v & SB_READONDATA) != 0;
            return;
        case SBADDRESS0:
            if (!dc_.sba) return;
            sbaddress_ = v;
            if (sbreadonaddr_ && !sberror_ && !sbbusyerror_) sb_lee();
            return;
        case SBDATA0:
            if (!dc_.sba) return;
            sbdata_ = v;
            if (!sberror_ && !sbbusyerror_) sb_escribe();
            return;
        default: return;
        }
    }

    void escribe_dmcontrol(uint32_t v) {
        using namespace dm;
        if (!(v & DMACTIVE)) {                  // el DM entero vuelve a su reset
            if (ndmreset_ && ndmreset) ndmreset(false);
            reset_dm();
            return;
        }
        dmactive_ = true;
        const uint32_t sel = ((v >> 16) & 0x3FFu) | (((v >> 6) & 0x3FFu) << 10);
        hartsel_ = sel & ((1u << hartsellen_) - 1u);
        const uint32_t h = hartsel_;
        if (existe(h)) {
            NucleoRv& n = *harts_[h];
            EstadoHart& e = h_[h];
            if (v & ACKHAVERESET) e.havereset = false;
            if (v & SETRESETHALTREQ) { e.resethaltreq = true;  n.hart.pide_parada_al_reset(true); }
            if (v & CLRRESETHALTREQ) { e.resethaltreq = false; n.hart.pide_parada_al_reset(false); }
            const bool hq = (v & HALTREQ) != 0;
            if (hq != e.haltreq) { e.haltreq = hq; n.pide_parada(hq); }
            if ((v & RESUMEREQ) && !hq && dispo(h)) {
                e.resumeack = false;
                if (n.parado()) { n.pide_parada(false); n.pide_seguir(); }
                e.resumeack = true;
            }
            if (v & HARTRESET) { n.pide_reset_hart(); e.havereset = true; }
        }
        const bool nr = (v & NDMRESET) != 0;
        if (nr != ndmreset_) {
            ndmreset_ = nr;
            if (nr) for (EstadoHart& e : h_) e.havereset = true;
            if (ndmreset) ndmreset(nr);
        }
    }

    // =========================================================================
    // Comandos abstractos
    // =========================================================================
    void autoexec(bool si) { if (si && !cmderr_) ejecuta(); }

    void ejecuta() {
        using namespace dm;
        const unsigned tipo = command_ >> 24;
        if (tipo != 0) { cmderr_ = CMDERR_NO_SOPORTADO; return; }   // solo acceso a registro
        const uint32_t h = hartsel_;
        if (!existe(h) || !parado(h)) { cmderr_ = CMDERR_PARADA; return; }
        RvHart& hart = harts_[h]->hart;
        const unsigned size = (command_ >> 20) & 7u;
        const uint16_t regno = uint16_t(command_ & 0xFFFFu);
        if (command_ & AR_TRANSFER) {
            if (size != 2) { cmderr_ = CMDERR_NO_SOPORTADO; return; }
            const bool ok = (command_ & AR_WRITE) ? hart.dm_escribe_reg(regno, data_[0])
                                                  : hart.dm_lee_reg(regno, data_[0]);
            if (!ok) { cmderr_ = CMDERR_EXCEPCION; return; }
        }
        if (command_ & AR_POSTINC)
            command_ = (command_ & ~0xFFFFu) | uint16_t(regno + 1u);
        if (command_ & AR_POSTEXEC) {
            unsigned ciclos = 0;
            if (!hart.dm_progbuf(progbuf_, dc_.progbuf < 16u ? dc_.progbuf : 16u, ciclos))
                cmderr_ = CMDERR_EXCEPCION;
        }
    }

    // =========================================================================
    // SBA
    // =========================================================================
    unsigned sb_bytes() const { return sbaccess_ <= 4 ? (1u << sbaccess_) : 0u; }
    bool sb_comprueba() {
        const unsigned n = sb_bytes();
        if (!n || n > 4 || !((dc_.sba_anchos >> sbaccess_) & 1u)) { sberror_ = 4; return false; }
        if (sbaddress_ & (n - 1u)) { sberror_ = 3; return false; }
        return true;
    }
    void sb_lee() {
        if (!sb_comprueba()) return;
        uint32_t v = 0;
        if (!sb_bus(false, v)) { sberror_ = 2; return; }
        sbdata_ = v;
        if (sbautoinc_) sbaddress_ += sb_bytes();
    }
    void sb_escribe() {
        if (!sb_comprueba()) return;
        uint32_t v = sbdata_;
        if (!sb_bus(true, v)) { sberror_ = 2; return; }
        if (sbautoinc_) sbaddress_ += sb_bytes();
    }
    bool sb_bus(bool escr, uint32_t& v) {
        ++n_sba_;
        const unsigned n = sb_bytes();
        unsigned char buf[4] = {};
        if (escr) for (unsigned i = 0; i < n; ++i) buf[i] = uint8_t(v >> (8 * i));
        tlm::tlm_generic_payload gp;
        RvExt ext;
        ext.depurador = true;
        ext.acceso = escr ? RvAcceso::Store : RvAcceso::Load;
        gp.set_command(escr ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        gp.set_address(sbaddress_);
        gp.set_data_ptr(buf);
        gp.set_data_length(n);
        gp.set_streaming_width(n);
        gp.set_byte_enable_ptr(nullptr);
        gp.set_dmi_allowed(false);
        gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        gp.set_extension(&ext);
        sc_core::sc_time d = sc_core::SC_ZERO_TIME;
        sba->b_transport(gp, d);
        gp.clear_extension(&ext);
        if (gp.get_response_status() != tlm::TLM_OK_RESPONSE) return false;
        if (!escr) { v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(buf[i]) << (8 * i); }
        return true;
    }

    // =========================================================================
    // El disparo cruzado: un hart se ha parado
    // =========================================================================
    void al_parar() {
        if (!cruzado_ || !dmactive_) return;
        bool alguno = false;
        for (NucleoRv* n : harts_) if (n->parado()) alguno = true;
        if (!alguno) return;
        for (unsigned i = 0; i < harts_.size(); ++i)
            if (dispo(i) && !harts_[i]->parado()) harts_[i]->pide_parada(true);
    }
};

} // namespace stm32
#endif // STM32_RV_DEBUG_RV_DM_H
