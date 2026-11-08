// =============================================================================
// rv_dm_cliente.h — El lado del DEPURADOR del Debug Module
//
// E2 de doc/analisis_esp32_riscv.md, §3.7. Lo que hace OpenOCD con un DM
// 0.13, escrito una vez y usado por el backend de GDB (rv/gdb) y por el banco:
// activar el DM, descubrir los harts, parar, seguir, dar un paso, leer y
// escribir registros por comandos abstractos, la memoria por SBA (o por el
// program buffer si no hay SBA), descubrir los triggers recorriendo tselect y
// resetear con ndmreset y resethaltreq.
//
// Habla solo por `dmi_if`: le da igual que al otro lado esté el DM por una
// llamada (G1) o un DTM por los pines (G2'). Cuando tiene que esperar a que el
// hart haga algo (pararse, dar el paso) cede la simulación con
// sc_core::wait(), así que se usa desde un SC_THREAD.
// =============================================================================
#ifndef STM32_RV_DEBUG_RV_DM_CLIENTE_H
#define STM32_RV_DEBUG_RV_DM_CLIENTE_H

#include <systemc>
#include <cstdint>
#include <vector>
#include "dmi_if.h"

namespace stm32 {

class RvDmCliente {
public:
    explicit RvDmCliente(dmi_if& d) : d_(&d) {}
    void pon_dmi(dmi_if& d) { d_ = &d; activo_ = false; }

    // Cuánto tiempo simulado se cede entre dos sondeos del estado, y cuántos
    // sondeos se hacen antes de rendirse
    sc_core::sc_time sondeo{1, sc_core::SC_US};
    unsigned intentos = 2000;

    static constexpr uint16_t CSR_DCSR = 0x7B0, CSR_DPC = 0x7B1, CSR_TSELECT = 0x7A0,
                              CSR_TDATA1 = 0x7A1, CSR_TDATA2 = 0x7A2;
    static constexpr uint16_t GPR(unsigned i) { return uint16_t(0x1000u + i); }

    // --- Arranque -----------------------------------------------------------
    // Activa el DM y cuenta los harts. false si no hay DM 0.13 al otro lado.
    bool activar() {
        using namespace dm;
        uint32_t v = 0;
        if (!w(DMCONTROL, 0) || !w(DMCONTROL, DMACTIVE)) return false;
        for (unsigned i = 0; i < 100; ++i) {
            if (!r(DMCONTROL, v)) return false;
            if (v & DMACTIVE) break;
        }
        if (!r(DMSTATUS, v) || (v & 0xFu) != 2u) return false;
        // HARTSELLEN: se escriben unos y se lee lo que queda
        w(DMCONTROL, DMACTIVE | (0x3FFu << 16) | (0x3FFu << 6));
        r(DMCONTROL, v);
        const uint32_t max = ((v >> 16) & 0x3FFu) | (((v >> 6) & 0x3FFu) << 10);
        n_harts_ = 0;
        for (uint32_t h = 0; h <= max && h < 1024; ++h) {
            selecciona(h);
            r(DMSTATUS, v);
            if (v & ANYNONEXISTENT) break;
            ++n_harts_;
        }
        uint32_t ac = 0;
        r(ABSTRACTCS, ac);
        progbuf_ = (ac >> 24) & 0x1Fu;
        datacount_ = ac & 0xFu;
        uint32_t sb = 0;
        r(SBCS, sb);
        sba32_ = (sb >> 29) != 0 && (sb & 0x4u);
        sba16_ = (sb >> 29) != 0 && (sb & 0x2u);
        selecciona(0);
        activo_ = n_harts_ > 0;
        return activo_;
    }
    bool activo() const { return activo_; }
    unsigned n_harts() const { return n_harts_; }
    bool tiene_sba() const { return sba32_; }
    unsigned progbuf() const { return progbuf_; }

    // Lo que se hace por SBA, o por el program buffer
    bool usar_sba = true;

    void selecciona(uint32_t h) {
        sel_ = h;
        w(dm::DMCONTROL, ctl());
    }
    uint32_t hart() const { return sel_; }

    // --- Estado --------------------------------------------------------------
    uint32_t dmstatus() { uint32_t v = 0; r(dm::DMSTATUS, v); return v; }
    bool parado()      { return (dmstatus() & dm::ALLHALTED) != 0; }
    bool disponible()  { return !(dmstatus() & (dm::ANYUNAVAIL | dm::ANYNONEXISTENT)); }

    // --- Parar, seguir, paso --------------------------------------------------
    bool parar() {
        w(dm::DMCONTROL, ctl() | dm::HALTREQ);
        const bool ok = espera([this] { return parado(); });
        w(dm::DMCONTROL, ctl());
        return ok;
    }
    bool seguir() {
        w(dm::DMCONTROL, ctl() | dm::RESUMEREQ);
        const bool ok = espera([this] { return (dmstatus() & dm::ALLRESUMEACK) != 0; });
        w(dm::DMCONTROL, ctl());
        return ok;
    }
    bool paso() {
        uint32_t dcsr = 0;
        if (!lee_reg(CSR_DCSR, dcsr)) return false;
        escribe_reg(CSR_DCSR, dcsr | 4u);
        w(dm::DMCONTROL, ctl() | dm::RESUMEREQ);
        const bool ok = espera([this] { return parado(); });
        w(dm::DMCONTROL, ctl());
        escribe_reg(CSR_DCSR, dcsr & ~4u);
        return ok;
    }
    // El reset del sistema, con todos los harts parados en su primera
    // instrucción (`monitor reset halt`); o corriendo, con `halt = false`
    bool reset(bool halt = true) {
        using namespace dm;
        for (uint32_t h = 0; h < n_harts_; ++h) {
            sel_ = h;
            w(DMCONTROL, ctl() | (halt ? SETRESETHALTREQ : CLRRESETHALTREQ));
        }
        sel_ = 0;
        w(DMCONTROL, ctl() | NDMRESET);
        sc_core::wait(sondeo * 10);           // el pulso tiene que durar algo
        w(DMCONTROL, ctl());
        bool ok = true;
        for (uint32_t h = 0; h < n_harts_; ++h) {
            sel_ = h;
            w(DMCONTROL, ctl());
            ok = espera([this, halt] {
                const uint32_t s = dmstatus();
                return (s & ANYHAVERESET) && !(s & ANYUNAVAIL) &&
                       (halt ? (s & ALLHALTED) != 0 : (s & ALLRUNNING) != 0);
            }) && ok;
            w(DMCONTROL, ctl() | ACKHAVERESET | CLRRESETHALTREQ);
        }
        selecciona(0);
        return ok;
    }

    // --- Registros (el hart tiene que estar parado) -----------------------------
    bool lee_reg(uint16_t regno, uint32_t& v) {
        if (!comando(dm::AR_SIZE32 | dm::AR_TRANSFER | regno)) return false;
        return r(dm::DATA0, v);
    }
    bool escribe_reg(uint16_t regno, uint32_t v) {
        w(dm::DATA0, v);
        return comando(dm::AR_SIZE32 | dm::AR_TRANSFER | dm::AR_WRITE | regno);
    }
    uint32_t cmderr() { uint32_t v = 0; r(dm::ABSTRACTCS, v); return (v >> 8) & 7u; }

    // --- Memoria ---------------------------------------------------------------
    bool lee32(uint32_t a, uint32_t& v) {
        if (sba32_ && usar_sba) return sb_lee(a, 2, v);
        return pb_acceso(a, v, false, 2);
    }
    bool escribe32(uint32_t a, uint32_t v) {
        if (sba32_ && usar_sba) return sb_escribe(a, 2, v);
        return pb_acceso(a, v, true, 2);
    }
    bool escribe16(uint32_t a, uint16_t v) {
        uint32_t x = v;
        if (sba16_ && usar_sba) return sb_escribe(a, 1, x);
        // Sin SBA de 16 bits (el C3 solo la tiene de palabra), leer, cambiar y
        // escribir la palabra entera: la IRAM del C3 no admite medias palabras
        // por el bus de datos, y ahí es donde van los c.ebreak (E6). Es lo que
        // hace openocd-esp32.
        const uint32_t al = a & ~3u;
        uint32_t w32 = 0;
        if (!lee32(al, w32)) return false;
        const unsigned sh = (a & 2u) * 8u;
        w32 = (w32 & ~(0xFFFFu << sh)) | (uint32_t(v) << sh);
        return escribe32(al, w32);
    }
    // Un bloque de palabras seguidas: por SBA con autoincremento, que es lo
    // que hace que un `load` no tarde una eternidad
    unsigned lee_bloque(uint32_t a, uint32_t* p, unsigned n) {
        if (!(sba32_ && usar_sba)) {
            for (unsigned i = 0; i < n; ++i) if (!lee32(a + 4 * i, p[i])) return i;
            return n;
        }
        using namespace dm;
        w(SBCS, SB_READONADDR | SB_READONDATA | SB_AUTOINC | (2u << 17) | (7u << 12));
        w(SBADDRESS0, a);
        for (unsigned i = 0; i < n; ++i) r(SBDATA0, p[i]);
        uint32_t s = 0;
        r(SBCS, s);
        w(SBCS, 7u << 12);
        return ((s >> 12) & 7u) ? 0 : n;
    }
    unsigned escribe_bloque(uint32_t a, const uint32_t* p, unsigned n) {
        if (!(sba32_ && usar_sba)) {
            for (unsigned i = 0; i < n; ++i) if (!escribe32(a + 4 * i, p[i])) return i;
            return n;
        }
        using namespace dm;
        w(SBCS, SB_AUTOINC | (2u << 17) | (7u << 12));
        w(SBADDRESS0, a);
        for (unsigned i = 0; i < n; ++i) w(SBDATA0, p[i]);
        uint32_t s = 0;
        r(SBCS, s);
        w(SBCS, 7u << 12);
        return ((s >> 12) & 7u) ? 0 : n;
    }

    // --- Triggers ----------------------------------------------------------------
    // Cuántos hay, recorriendo tselect hasta que no se queda con lo escrito
    // (lo que hace OpenOCD: así vale para un chip que no se conoce)
    unsigned cuenta_triggers() {
        unsigned n = 0;
        uint32_t t1 = 0, v = 0;
        for (uint32_t i = 0; i < 32; ++i) {
            if (!escribe_reg(CSR_TSELECT, i) || !lee_reg(CSR_TSELECT, v) || v != i) break;
            if (!lee_reg(CSR_TDATA1, t1) || (t1 >> 28) == 0) break;
            ++n;
        }
        escribe_reg(CSR_TSELECT, 0);
        return n;
    }

    uint64_t accesos() const { return n_; }

private:
    dmi_if* d_;
    bool activo_ = false, sba32_ = false, sba16_ = false;
    unsigned n_harts_ = 0, progbuf_ = 0, datacount_ = 0;
    uint32_t sel_ = 0;
    uint64_t n_ = 0;

    bool r(uint32_t a, uint32_t& v) { ++n_; return d_->dmi_leer(a, v); }
    bool w(uint32_t a, uint32_t v)  { ++n_; return d_->dmi_escribir(a, v); }
    uint32_t ctl() const {
        return dm::DMACTIVE | ((sel_ & 0x3FFu) << 16) | (((sel_ >> 10) & 0x3FFu) << 6);
    }
    template <class F> bool espera(F listo) {
        for (unsigned i = 0; i < intentos; ++i) {
            if (listo()) return true;
            sc_core::wait(sondeo);
        }
        return listo();
    }
    bool comando(uint32_t c) {
        w(dm::COMMAND, c);
        const uint32_t e = cmderr();
        if (e) { w(dm::ABSTRACTCS, 7u << 8); return false; }
        return true;
    }

    bool sb_lee(uint32_t a, unsigned acc, uint32_t& v) {
        using namespace dm;
        w(SBCS, SB_READONADDR | (acc << 17) | (7u << 12));
        w(SBADDRESS0, a);
        r(SBDATA0, v);
        uint32_t s = 0;
        r(SBCS, s);
        if ((s >> 12) & 7u) { w(SBCS, 7u << 12); return false; }
        return true;
    }
    bool sb_escribe(uint32_t a, unsigned acc, uint32_t v) {
        using namespace dm;
        w(SBCS, (acc << 17) | (7u << 12));
        w(SBADDRESS0, a);
        w(SBDATA0, v);
        uint32_t s = 0;
        r(SBCS, s);
        if ((s >> 12) & 7u) { w(SBCS, 7u << 12); return false; }
        return true;
    }
    // Por el program buffer (hart parado): s0 = dirección, s1 = dato;
    // `lw s1, 0(s0)` o `sw/sh s1, 0(s0)`, y ebreak. s0 y s1 se guardan y se
    // devuelven, como hace OpenOCD.
    bool pb_acceso(uint32_t a, uint32_t& v, bool escr, unsigned acc) {
        if (progbuf_ < 2) return false;
        uint32_t s0 = 0, s1 = 0;
        if (!lee_reg(GPR(8), s0) || !lee_reg(GPR(9), s1)) return false;
        const uint32_t ins = escr ? (acc == 2 ? 0x00942023u : 0x00941023u)   // sw/sh s1, 0(s0)
                                  : 0x00042483u;                              // lw s1, 0(s0)
        using namespace dm;
        w(PROGBUF0, ins);
        w(PROGBUF0 + 1, 0x00100073u);                                          // ebreak
        bool ok = escribe_reg(GPR(8), a);
        if (escr) ok = ok && escribe_reg(GPR(9), v);
        ok = ok && comando(AR_SIZE32 | AR_POSTEXEC);
        if (ok && !escr) ok = lee_reg(GPR(9), v);
        escribe_reg(GPR(8), s0);
        escribe_reg(GPR(9), s1);
        return ok;
    }
};

} // namespace stm32
#endif // STM32_RV_DEBUG_RV_DM_CLIENTE_H
