// =============================================================================
// rv_hart.h — Un hart RISC-V de 32 bits: I, M, A, C, Zicsr, Zifencei, modos M y U
//
// E1 de doc/analisis_esp32_riscv.md, §3.3. Es C++ puro: ejecuta una
// instrucción (o toma una interrupción) en cada `paso()`, y todo lo de fuera
// -memoria, interrupciones, CSR e instrucciones propias- le llega por las
// interfaces de rv_ifaces.h. El `SC_MODULE` que lo mete en una simulación
// (rv/hart/nucleo_rv.h) es una capa fina que pone el tiempo y el bus.
//
// QUÉ HAY:
//   * RV32I, M, A y C [Unprivileged ISA 20191213]; la C se expande a su forma
//     de 32 bits (rv_decod.h) y el ejecutor solo conoce esas;
//   * Zicsr, Zifencei, Zicntr y, como contadores a cero, Zihpm;
//   * modo M y, si `RvCaps` lo dice, U, con MPRV y TW [Privileged 20211203];
//   * la delegación de interrupciones a U de la extensión N (ustatus, uie,
//     utvec, uepc, ucause, uip, uret), que es la que tiene el ESP32-C6 HP;
//   * traps con mtvec directo o vectorizado, o solo vectorizado y alineado a
//     256 (los núcleos de Espressif);
//   * interrupciones por un controlador que no es del núcleo (rv_irq_if),
//     con o sin mie/mip;
//   * PMP de 0 a 16 regiones (TOR, NA4 y NAPOT, con bloqueo);
//   * desalineados con trap, por hardware o por hardware con la errata
//     CPU-863 de Espressif;
//   * el lado del hart de la depuración [Debug Spec 0.13.2, cap. 4 y 5]: modo
//     depuración, dcsr/dpc/dscratch, dret, ebreakm/ebreaku, paso a paso, la
//     ejecución del program buffer y los triggers mcontrol (tipo 2).
//
// LO QUE NO HAY, dicho: F y D, S y la memoria virtual, RV64, las extensiones
// B y Zc*, y `time` sin una fuente que dé el integrador.
// =============================================================================
#ifndef STM32_RV_HART_RV_HART_H
#define STM32_RV_HART_RV_HART_H

#include <cstdint>
#include <cstring>
#include <functional>
#include "rv_caps.h"
#include "rv_decod.h"
#include "rv_ifaces.h"

namespace stm32 {

// Una instrucción retirada, para trazas y para comparar con Spike
struct RvCommit {
    uint32_t pc;
    uint32_t insn;        // tal como estaba en memoria (16 o 32 bits)
    unsigned largo;       // 2 o 4
    int      rd;          // registro escrito, o -1 si ninguno (x0 no cuenta)
    uint32_t valor;
    RvModo   modo;        // con el que se ejecutó
};

// Causas de excepción [Privileged, tabla 3.6]
enum : uint32_t {
    RV_EXC_INST_DESALINEADA = 0, RV_EXC_INST_ACCESO = 1, RV_EXC_ILEGAL = 2,
    RV_EXC_BREAKPOINT = 3, RV_EXC_LOAD_DESALINEADA = 4, RV_EXC_LOAD_ACCESO = 5,
    RV_EXC_STORE_DESALINEADA = 6, RV_EXC_STORE_ACCESO = 7,
    RV_EXC_ECALL_U = 8, RV_EXC_ECALL_M = 11
};
// Causas de entrada en modo depuración [Debug Spec, tabla 4.2]
enum : uint32_t {
    RV_DBG_EBREAK = 1, RV_DBG_TRIGGER = 2, RV_DBG_HALTREQ = 3, RV_DBG_STEP = 4,
    RV_DBG_RESETHALTREQ = 5
};

class RvHart {
public:
    RvHart(const RvCaps& caps, unsigned hartid = 0) : caps_(caps), hartid_(hartid) { reset(); }

    // --- Lo de fuera --------------------------------------------------------
    void pon_memoria(rv_mem_if* m)       { mem_ = m; }
    void pon_irq(rv_irq_if* i)           { irq_ = i; }
    void pon_csr_ext(rv_csr_ext_if* e)   { csr_ext_ = e; }
    void pon_ext(rv_ext_if* e)           { ext_ = e; }
    // La fuente de `time`/`timeh`. Sin ella, leerlos es instrucción ilegal.
    std::function<uint64_t()> fuente_tiempo;
    // Lo que cuenta como "permiso" de una dirección además del PMP (la PMA de
    // un C6, el PMS de un C3). Solo lo usa la errata CPU-863.
    std::function<uint32_t(uint32_t)> permisos_extra;
    // ¿Hay algo en esta dirección? (la PMA del SoC). Si está, un sc.w que
    // falla por no tener reserva da igualmente fallo de acceso donde no hay
    // nada, como Sail; si no, falla sin tocar el bus.
    std::function<bool(uint32_t, unsigned)> existe_dir;
    // Cada instrucción retirada
    std::function<void(const RvCommit&)> traza;

    const RvCaps& caps() const { return caps_; }
    // Dónde arranca tras el reset si no es el vector del núcleo: un SoC sin
    // ROM que salta al punto de entrada del firmware (el C3 de E3). 0 = el de
    // RvCaps.
    void pon_pc_arranque(uint32_t pc) { pc_arranque_ = pc; }
    uint32_t pc_arranque() const { return pc_arranque_ ? pc_arranque_ : caps_.pc_reset; }
    unsigned hartid() const { return hartid_; }

    // --- Estado arquitectónico -----------------------------------------------
    uint32_t x(unsigned i) const     { return i ? x_[i & 31u] : 0u; }
    void     pon_x(unsigned i, uint32_t v) { if (i & 31u) x_[i & 31u] = v; }
    uint32_t pc() const              { return pc_; }
    void     pon_pc(uint32_t v)      { pc_ = v; fetch_vacio(); }
    RvModo   modo() const            { return modo_; }
    uint64_t ciclos() const          { return mcycle_; }
    uint64_t instret() const         { return minstret_; }
    bool     en_wfi() const          { return wfi_; }
    uint32_t mstatus() const         { return mstatus_; }
    uint32_t mcause() const          { return mcause_; }
    uint32_t mepc() const            { return mepc_; }
    uint32_t mtval() const           { return mtval_; }
    uint64_t traps() const           { return n_traps_; }

    // --- Reset ---------------------------------------------------------------
    void reset() {
        std::memset(x_, 0, sizeof x_);
        pc_ = pc_arranque_ ? pc_arranque_ : caps_.pc_reset;
        modo_ = RvModo::M;
        mstatus_ = legaliza_mpp(0);
        mtvec_ = legaliza_tvec(caps_.mtvec_reset);
        utvec_ = legaliza_tvec(0);
        mepc_ = mcause_ = mtval_ = mscratch_ = 0;
        uepc_ = ucause_ = uscratch_ = 0;
        mie_ = 0; mideleg_ = caps_.deleg_n ? caps_.mideleg_reset : 0;
        mcounteren_ = 0; mcountinhibit_ = 0;
        mcycle_ = minstret_ = 0;
        std::memset(pmpcfg_, 0, sizeof pmpcfg_);
        std::memset(pmpaddr_, 0, sizeof pmpaddr_);
        tselect_ = 0;
        for (unsigned i = 0; i < 8; ++i) { tdata1_[i] = vacio_tdata1(); tdata2_[i] = 0; }
        tcontrol_ = 0;
        dcsr_ = (4u << 28) | 3u;             // xdebugver = 4, prv = M
        dpc_ = dscratch0_ = dscratch1_ = 0;
        debug_ = false; paso_pendiente_ = false;
        resv_ = false; wfi_ = false;
        cpu863_ = false;
        fetch_vacio();
        if (resethaltreq_) entra_debug(RV_DBG_RESETHALTREQ);
    }

    // =========================================================================
    // EL PASO
    // =========================================================================
    struct Paso {
        unsigned ciclos = 0;
        bool     retirada = false;    // una instrucción terminó (o trap dentro)
        bool     trap = false;        // se entró en un trap
        bool     dormido = false;     // en wfi, sin nada que lo despierte
        bool     parado = false;      // en modo depuración
    };

    Paso paso() {
        Paso r;
        if (debug_) { r.parado = true; return r; }
        if (haltreq_) { entra_debug(RV_DBG_HALTREQ); r.parado = true; return r; }
        const RvTiempos& t = *caps_.tiempos;
        // Interrupciones: entre instrucciones, salvo pasando sin stepie
        const bool irq_ok = !(paso_pendiente_ && !(dcsr_ & DCSR_STEPIE));
        unsigned id = 0; bool a_u = false;
        if (irq_ok && interrupcion(id, a_u)) {
            wfi_ = false;
            toma_trap(true, id, 0, pc_, a_u);
            if (irq_) irq_->reclamada(id);
            r.trap = true; r.retirada = true;
            r.ciclos = t.trap + t.lat_irq;
            cuenta(r.ciclos, false);
            fin_de_paso();
            return r;
        }
        if (wfi_) {
            if (!despierta()) { r.dormido = true; return r; }
            wfi_ = false;
        }
        r = ejecuta_una();
        if (r.parado) return r;
        cuenta(r.ciclos, r.retirada && !r.trap);
        fin_de_paso();
        return r;
    }

    // ¿Hay alguna interrupción pedida y habilitada en mie, aunque MIE = 0? Es
    // lo que saca a un hart de wfi [Privileged, §3.3.3].
    bool despierta() {
        const uint32_t hw = irq_ ? irq_->pendientes() : 0;
        return (caps_.mie_mip ? (hw & mie_) : hw) != 0;
    }

    // =========================================================================
    // LA DEPURACIÓN, LADO DEL HART [Debug Spec 0.13.2]
    // =========================================================================
    void pide_parada(bool v)        { haltreq_ = v; }
    void pide_parada_al_reset(bool v) { resethaltreq_ = v; }
    bool parado() const             { return debug_; }
    uint32_t dcsr() const           { return dcsr_ | (dcsr_causa_ << 6); }
    uint32_t dpc() const            { return dpc_; }
    // resumereq: vuelve del modo depuración como un dret
    void pide_seguir() {
        if (!debug_) return;
        sale_debug();
    }
    // Acceso de un comando abstracto a un registro [Debug Spec, tabla 3.3]:
    // 0x0000-0x0FFF CSR, 0x1000-0x101F GPR. `false` = no existe.
    bool dm_lee_reg(uint16_t regno, uint32_t& v) {
        if (regno >= 0x1000 && regno <= 0x101F) { v = x(regno - 0x1000u); return true; }
        if (regno < 0x1000) {
            if (regno == CSR_DPC) { v = dpc_; return true; }
            const bool d = debug_; debug_ = true;
            const bool ok = csr_lee(regno, v);
            debug_ = d;
            return ok;
        }
        return false;
    }
    bool dm_escribe_reg(uint16_t regno, uint32_t v) {
        if (regno >= 0x1000 && regno <= 0x101F) { pon_x(regno - 0x1000u, v); return true; }
        if (regno < 0x1000) {
            const bool d = debug_; debug_ = true;
            const bool ok = csr_escribe(regno, v);
            debug_ = d;
            return ok;
        }
        return false;
    }
    // El program buffer: se ejecuta en modo depuración hasta un ebreak (o el
    // final, con impebreak). `false` si una instrucción dio excepción: el DM
    // lo dice con cmderr = 3, y el estado del hart no cambia por el trap.
    // Con la región del DM en el mapa (RvDebugCaps::dm_tam), el program buffer
    // y los datos (`datos`, `nd`) están en sus direcciones: el hart los busca
    // y los lee por ahí, con la PMP.
    bool dm_progbuf(const uint32_t* pb, unsigned n, unsigned& ciclos,
                    uint32_t* datos = nullptr, unsigned nd = 0) {
        if (!debug_) return false;
        progbuf_ = pb; progbuf_n_ = n;
        dm_datos_ = datos; dm_nd_ = nd;
        const uint32_t pc_guardado = pc_;
        const uint32_t b = pb_base();
        pc_ = b;
        bool ok = true;
        unsigned pasos = 0;
        for (;;) {
            if (pc_ < b || pc_ >= b + 4u * n || ++pasos > 1000) break;
            excepcion_pb_ = false; fin_pb_ = false;
            Paso p = ejecuta_una();
            ciclos += p.ciclos;
            if (excepcion_pb_) { ok = false; break; }
            if (fin_pb_) break;
        }
        progbuf_ = nullptr; dm_datos_ = nullptr; dm_nd_ = 0;
        pc_ = pc_guardado;
        fetch_vacio();
        return ok;
    }
    // Los triggers, para que los vea quien quiera (pruebas)
    uint32_t tdata1(unsigned i) const { return i < 8 ? tdata1_[i] : 0; }
    uint32_t tdata2(unsigned i) const { return i < 8 ? tdata2_[i] : 0; }

    // =========================================================================
    // LOS CSR, públicos para el DM y para las extensiones del integrador
    // =========================================================================
    enum : uint16_t {
        CSR_USTATUS = 0x000, CSR_UIE = 0x004, CSR_UTVEC = 0x005, CSR_USCRATCH = 0x040,
        CSR_UEPC = 0x041, CSR_UCAUSE = 0x042, CSR_UIP = 0x044,
        CSR_CYCLE = 0xC00, CSR_TIME = 0xC01, CSR_INSTRET = 0xC02,
        CSR_CYCLEH = 0xC80, CSR_TIMEH = 0xC81, CSR_INSTRETH = 0xC82,
        CSR_MVENDORID = 0xF11, CSR_MARCHID = 0xF12, CSR_MIMPID = 0xF13, CSR_MHARTID = 0xF14,
        CSR_MCONFIGPTR = 0xF15,
        CSR_MSTATUS = 0x300, CSR_MISA = 0x301, CSR_MIDELEG = 0x303, CSR_MIE = 0x304,
        CSR_MTVEC = 0x305, CSR_MCOUNTEREN = 0x306, CSR_MSTATUSH = 0x310,
        CSR_MENVCFG = 0x30A, CSR_MENVCFGH = 0x31A,
        CSR_MCOUNTINHIBIT = 0x320, CSR_MSCRATCH = 0x340, CSR_MEPC = 0x341,
        CSR_MCAUSE = 0x342, CSR_MTVAL = 0x343, CSR_MIP = 0x344,
        CSR_PMPCFG0 = 0x3A0, CSR_PMPADDR0 = 0x3B0,
        CSR_MCYCLE = 0xB00, CSR_MINSTRET = 0xB02, CSR_MCYCLEH = 0xB80, CSR_MINSTRETH = 0xB82,
        CSR_TSELECT = 0x7A0, CSR_TDATA1 = 0x7A1, CSR_TDATA2 = 0x7A2, CSR_TDATA3 = 0x7A3,
        CSR_TINFO = 0x7A4, CSR_TCONTROL = 0x7A5,
        CSR_DCSR = 0x7B0, CSR_DPC = 0x7B1, CSR_DSCRATCH0 = 0x7B2, CSR_DSCRATCH1 = 0x7B3
    };
    // Leer o escribir un CSR SIN las comprobaciones de privilegio de la
    // instrucción (que hace `instr_csr`). `false` = no existe.
    bool csr_lee(uint16_t a, uint32_t& v) {
        if (csr_ext_ && csr_ext_->atiende(a)) return csr_ext_->lee(*this, a, v);
        const bool u = caps_.modo_u(), n = caps_.deleg_n;
        switch (a) {
        case CSR_MVENDORID: v = caps_.mvendorid; return true;
        case CSR_MARCHID:   v = caps_.marchid; return true;
        case CSR_MIMPID:    v = caps_.mimpid; return true;
        case CSR_MHARTID:   v = hartid_; return true;
        case CSR_MCONFIGPTR:
            if (!caps_.tiene_z(RV_Z_MSTATUSH)) return false;
            v = 0; return true;
        case CSR_MSTATUS:   v = mstatus_; return true;
        case CSR_MSTATUSH:
            if (!caps_.tiene_z(RV_Z_MSTATUSH)) return false;
            v = 0; return true;
        // menvcfg (1.12, con modo U): sin S ni Zicbo*, solo FIOM, y fijo a 0
        case CSR_MENVCFG: case CSR_MENVCFGH:
            if (!caps_.tiene_z(RV_Z_MSTATUSH) || !u) return false;
            v = 0; return true;
        case CSR_MISA:      v = caps_.misa(); return true;
        case CSR_MIDELEG:   if (!n) return false; v = mideleg_; return true;
        case CSR_MIE:       if (!caps_.mie_mip) return false; v = mie_; return true;
        case CSR_MIP:       if (!caps_.mie_mip) return false; v = mip(); return true;
        case CSR_MTVEC:     v = mtvec_; return true;
        case CSR_MCOUNTEREN:
            if (!u || !caps_.contadores_std) return false;
            v = mcounteren_; return true;
        case CSR_MCOUNTINHIBIT:
            if (!caps_.contadores_std) return false;
            v = mcountinhibit_; return true;
        case CSR_MSCRATCH:  v = mscratch_; return true;
        case CSR_MEPC:      v = mepc_; return true;
        case CSR_MCAUSE:    v = mcause_; return true;
        case CSR_MTVAL:     v = mtval_; return true;
        case CSR_USTATUS:   if (!n) return false; v = mstatus_ & (MS_UIE | MS_UPIE); return true;
        case CSR_UIE:       if (!n) return false; v = mie_ & mideleg_; return true;
        case CSR_UIP:       if (!n) return false; v = mip() & mideleg_; return true;
        case CSR_UTVEC:     if (!n) return false; v = utvec_; return true;
        case CSR_USCRATCH:  if (!n) return false; v = uscratch_; return true;
        case CSR_UEPC:      if (!n) return false; v = uepc_; return true;
        case CSR_UCAUSE:    if (!n) return false; v = ucause_; return true;
        case CSR_MCYCLE: case CSR_CYCLE:
            if (!caps_.contadores_std) return false;
            v = uint32_t(mcycle_); return true;
        case CSR_MCYCLEH: case CSR_CYCLEH:
            if (!caps_.contadores_std) return false;
            v = uint32_t(mcycle_ >> 32); return true;
        case CSR_MINSTRET: case CSR_INSTRET:
            if (!caps_.contadores_std) return false;
            v = uint32_t(minstret_); return true;
        case CSR_MINSTRETH: case CSR_INSTRETH:
            if (!caps_.contadores_std) return false;
            v = uint32_t(minstret_ >> 32); return true;
        case CSR_TIME:  if (!fuente_tiempo) return false; v = uint32_t(fuente_tiempo()); return true;
        case CSR_TIMEH: if (!fuente_tiempo) return false; v = uint32_t(fuente_tiempo() >> 32); return true;
        case CSR_TSELECT:
            if (!caps_.dbg.triggers) return false;
            v = tselect_; return true;
        case CSR_TDATA1:
            if (!caps_.dbg.triggers) return false;
            v = tdata1_[tselect_]; return true;
        case CSR_TDATA2:
            if (!caps_.dbg.triggers) return false;
            v = tdata2_[tselect_]; return true;
        case CSR_TDATA3:
            if (!caps_.dbg.triggers) return false;
            v = 0; return true;
        case CSR_TINFO:
            if (!caps_.dbg.triggers) return false;
            v = 1u << 2; return true;            // solo mcontrol
        case CSR_TCONTROL:
            if (!caps_.dbg.tcontrol) return false;
            v = tcontrol_; return true;
        case CSR_DCSR:      if (!debug_) return false; v = dcsr(); return true;
        case CSR_DPC:       if (!debug_) return false; v = dpc_; return true;
        case CSR_DSCRATCH0: if (!debug_) return false; v = dscratch0_; return true;
        case CSR_DSCRATCH1: if (!debug_) return false; v = dscratch1_; return true;
        default: break;
        }
        if (a >= CSR_PMPCFG0 && a < CSR_PMPCFG0 + 4) {
            if (!caps_.pmp_regiones) return false;
            v = 0;
            for (unsigned k = 0; k < 4; ++k) v |= uint32_t(pmpcfg_[(a - CSR_PMPCFG0) * 4 + k]) << (8 * k);
            return true;
        }
        if (a >= CSR_PMPADDR0 && a < CSR_PMPADDR0 + 16) {
            if (!caps_.pmp_regiones) return false;
            v = pmpaddr_[a - CSR_PMPADDR0];
            return true;
        }
        // Zihpm: los contadores y sus sucesos existen y valen cero
        if (caps_.tiene_z(RV_Z_IHPM) && caps_.contadores_std &&
            ((a >= 0xB03 && a <= 0xB1F) || (a >= 0xB83 && a <= 0xB9F) ||
             (a >= 0xC03 && a <= 0xC1F) || (a >= 0xC83 && a <= 0xC9F) ||
             (a >= 0x323 && a <= 0x33F))) { v = 0; return true; }
        return false;
    }
    bool csr_escribe(uint16_t a, uint32_t v) {
        if (csr_ext_ && csr_ext_->atiende(a)) return csr_ext_->escribe(*this, a, v);
        const bool u = caps_.modo_u(), n = caps_.deleg_n;
        switch (a) {
        case CSR_MSTATUS: {
            uint32_t m = MS_MIE | MS_MPIE | MS_MPP;
            if (u) m |= MS_MPRV | MS_TW;
            if (n) m |= MS_UIE | MS_UPIE;
            uint32_t nv = (mstatus_ & ~m) | (v & m);
            nv = legaliza_mpp(nv);
            mstatus_ = nv;
            return true;
        }
        case CSR_MSTATUSH: return caps_.tiene_z(RV_Z_MSTATUSH);
        case CSR_MENVCFG: case CSR_MENVCFGH: return caps_.tiene_z(RV_Z_MSTATUSH) && u;
        case CSR_MISA:     return true;                      // WARL, fija
        case CSR_MIDELEG:  if (!n) return false; mideleg_ = v & caps_.mie_bits; return true;
        case CSR_MIE:      if (!caps_.mie_mip) return false; mie_ = v & caps_.mie_bits; return true;
        case CSR_MIP:      return caps_.mie_mip;                // nada escribible por software
        case CSR_MTVEC:    mtvec_ = legaliza_tvec(v); return true;
        case CSR_MCOUNTEREN:
            if (!u || !caps_.contadores_std) return false;
            mcounteren_ = v; return true;
        case CSR_MCOUNTINHIBIT:
            if (!caps_.contadores_std) return false;
            mcountinhibit_ = v & 0x5u; return true;
        case CSR_MSCRATCH: mscratch_ = v; return true;
        case CSR_MEPC:     mepc_ = v & (caps_.tiene(RV_EXT_C) ? ~1u : ~3u); return true;
        case CSR_MCAUSE:   mcause_ = v; return true;
        case CSR_MTVAL:    mtval_ = v; return true;
        case CSR_USTATUS:
            if (!n) return false;
            mstatus_ = (mstatus_ & ~(MS_UIE | MS_UPIE)) | (v & (MS_UIE | MS_UPIE));
            return true;
        case CSR_UIE:
            if (!n) return false;
            mie_ = (mie_ & ~mideleg_) | (v & mideleg_ & caps_.mie_bits);
            return true;
        case CSR_UIP:      return n;
        case CSR_UTVEC:    if (!n) return false; utvec_ = legaliza_tvec(v); return true;
        case CSR_USCRATCH: if (!n) return false; uscratch_ = v; return true;
        case CSR_UEPC:     if (!n) return false; uepc_ = v & (caps_.tiene(RV_EXT_C) ? ~1u : ~3u); return true;
        case CSR_UCAUSE:   if (!n) return false; ucause_ = v; return true;
        case CSR_MCYCLE:
            if (!caps_.contadores_std) return false;
            mcycle_ = (mcycle_ & 0xFFFFFFFF00000000ull) | v; escrito_cy_ = true; return true;
        case CSR_MCYCLEH:
            if (!caps_.contadores_std) return false;
            mcycle_ = (mcycle_ & 0xFFFFFFFFull) | (uint64_t(v) << 32); escrito_cy_ = true; return true;
        case CSR_MINSTRET:
            if (!caps_.contadores_std) return false;
            minstret_ = (minstret_ & 0xFFFFFFFF00000000ull) | v; escrito_ir_ = true; return true;
        case CSR_MINSTRETH:
            if (!caps_.contadores_std) return false;
            minstret_ = (minstret_ & 0xFFFFFFFFull) | (uint64_t(v) << 32); escrito_ir_ = true; return true;
        case CSR_TSELECT:
            if (!caps_.dbg.triggers) return false;
            if (v < caps_.dbg.triggers) tselect_ = v;
            return true;
        case CSR_TDATA1:
            if (!caps_.dbg.triggers) return false;
            escribe_tdata1(v);
            return true;
        case CSR_TDATA2:
            if (!caps_.dbg.triggers) return false;
            if ((tdata1_[tselect_] & MC_DMODE) && !debug_) return true;
            tdata2_[tselect_] = v; return true;
        case CSR_TDATA3: return caps_.dbg.triggers != 0;
        case CSR_TINFO:  return caps_.dbg.triggers != 0;
        case CSR_TCONTROL:
            if (!caps_.dbg.tcontrol) return false;
            tcontrol_ = v & (TC_MTE | TC_MPTE); return true;
        case CSR_DCSR:
            if (!debug_) return false;
            escribe_dcsr(v);
            return true;
        case CSR_DPC:
            if (!debug_) return false;
            dpc_ = v & (caps_.tiene(RV_EXT_C) ? ~1u : ~3u); return true;
        case CSR_DSCRATCH0: if (!debug_) return false; dscratch0_ = v; return true;
        case CSR_DSCRATCH1: if (!debug_) return false; dscratch1_ = v; return true;
        default: break;
        }
        if (a >= CSR_PMPCFG0 && a < CSR_PMPCFG0 + 4) {
            if (!caps_.pmp_regiones) return false;
            for (unsigned k = 0; k < 4; ++k) {
                const unsigned i = (a - CSR_PMPCFG0) * 4 + k;
                if (i >= caps_.pmp_regiones || (pmpcfg_[i] & PMP_L)) continue;
                uint8_t c = uint8_t(v >> (8 * k)) & uint8_t(PMP_L | PMP_A | PMP_X | PMP_W | PMP_R);
                if ((c & PMP_W) && !(c & PMP_R)) c &= uint8_t(~(PMP_W | PMP_R)) ; // W sin R: reservado
                pmpcfg_[i] = c;
            }
            return true;
        }
        if (a >= CSR_PMPADDR0 && a < CSR_PMPADDR0 + 16) {
            if (!caps_.pmp_regiones) return false;
            const unsigned i = a - CSR_PMPADDR0;
            if (i >= caps_.pmp_regiones) return true;
            if (pmpcfg_[i] & PMP_L) return true;
            if (i + 1 < caps_.pmp_regiones && (pmpcfg_[i + 1] & PMP_L) &&
                ((pmpcfg_[i + 1] & PMP_A) >> 3) == 1) return true;   // TOR bloqueado
            pmpaddr_[i] = v;
            return true;
        }
        if (caps_.tiene_z(RV_Z_IHPM) && caps_.contadores_std &&
            ((a >= 0xB03 && a <= 0xB1F) || (a >= 0xB83 && a <= 0xB9F) ||
             (a >= 0x323 && a <= 0x33F))) return true;
        return false;
    }

    // Lo que se ve en mip: lo que pone el hardware, filtrado por los bits que
    // existen
    uint32_t mip() { return (irq_ ? irq_->pendientes() : 0u) & caps_.mie_bits; }

private:
    // --- Bits de mstatus, dcsr, mcontrol, pmpcfg ------------------------------
    static constexpr uint32_t MS_UIE = 1u << 0, MS_UPIE = 1u << 4, MS_MIE = 1u << 3,
                              MS_MPIE = 1u << 7, MS_MPP = 3u << 11, MS_MPRV = 1u << 17,
                              MS_TW = 1u << 21;
    static constexpr uint32_t DCSR_EBREAKM = 1u << 15, DCSR_EBREAKU = 1u << 12,
                              DCSR_STEPIE = 1u << 11, DCSR_STOPCOUNT = 1u << 10,
                              DCSR_STOPTIME = 1u << 9, DCSR_STEP = 1u << 2;
    static constexpr uint32_t MC_DMODE = 1u << 27, MC_HIT = 1u << 20, MC_ACTION = 0xFu << 12,
                              MC_MATCH = 0xFu << 7, MC_M = 1u << 6, MC_U = 1u << 3,
                              MC_EXEC = 1u << 2, MC_STORE = 1u << 1, MC_LOAD = 1u << 0;
    static constexpr uint32_t TC_MTE = 1u << 3, TC_MPTE = 1u << 7;
    static constexpr uint8_t PMP_R = 1, PMP_W = 2, PMP_X = 4, PMP_A = 3u << 3, PMP_L = 0x80;
    static constexpr uint32_t PB_BASE = 0xFFFFF800u;    // dirección ficticia del progbuf
    static constexpr uint32_t DM_DATOS = 0x380u;        // los datos, en la región del DM
    // Dónde empieza el program buffer: en la región del DM, justo debajo de los
    // datos (16 palabras: base + 0x340); sin ella, la dirección ficticia
    uint32_t pb_base() const {
        const RvDebugCaps& d = caps_.dbg;
        return d.dm_tam ? d.dm_base + DM_DATOS - 4u * (d.progbuf < 16u ? d.progbuf : 16u) : PB_BASE;
    }
    bool en_dm(uint32_t a) const {
        const RvDebugCaps& d = caps_.dbg;
        return d.dm_tam && a - d.dm_base < d.dm_tam;
    }

    const RvCaps& caps_;
    uint32_t pc_arranque_ = 0;
    unsigned hartid_;
    rv_mem_if*     mem_ = nullptr;
    rv_irq_if*     irq_ = nullptr;
    rv_csr_ext_if* csr_ext_ = nullptr;
    rv_ext_if*     ext_ = nullptr;

    uint32_t x_[32];
    uint32_t pc_ = 0;
    RvModo   modo_ = RvModo::M;
    uint32_t mstatus_ = 0, mtvec_ = 0, mepc_ = 0, mcause_ = 0, mtval_ = 0, mscratch_ = 0;
    uint32_t mie_ = 0, mideleg_ = 0, mcounteren_ = 0, mcountinhibit_ = 0;
    uint32_t utvec_ = 0, uepc_ = 0, ucause_ = 0, uscratch_ = 0;
    uint64_t mcycle_ = 0, minstret_ = 0;
    // Para la latencia de las interrupciones (`visibles`): lo pedido en la
    // frontera anterior
    uint32_t hw_ant_ = 0;
    bool     escrito_cy_ = false, escrito_ir_ = false;
    uint8_t  pmpcfg_[16];
    uint32_t pmpaddr_[16];
    uint32_t tselect_ = 0, tdata1_[8], tdata2_[8], tcontrol_ = 0;
    uint32_t dcsr_ = 0, dcsr_causa_ = 0, dpc_ = 0, dscratch0_ = 0, dscratch1_ = 0;
    bool     debug_ = false, haltreq_ = false, resethaltreq_ = false, paso_pendiente_ = false;
    bool     resv_ = false;
    uint32_t resv_dir_ = 0;
    bool     wfi_ = false;
    uint64_t n_traps_ = 0;
    // El program buffer en ejecución
    const uint32_t* progbuf_ = nullptr;
    unsigned progbuf_n_ = 0;
    uint32_t* dm_datos_ = nullptr;
    unsigned dm_nd_ = 0;
    bool     excepcion_pb_ = false, fin_pb_ = false;
    // La errata CPU-863: no se modela el cauce; se mira la instrucción
    // siguiente cuando hay una carga desalineada que cruza palabra
    bool     cpu863_ = false;
    // Búfer de búsqueda: una palabra alineada
    uint32_t fb_dir_ = 1, fb_dat_ = 0;

    // --- La instrucción en curso ----------------------------------------------
    uint32_t next_pc_ = 0;
    bool     trap_ = false;          // la instrucción en curso provocó un trap
    int      rd_esc_ = -1;
    uint32_t rd_val_ = 0;
    uint32_t crudo_ = 0;

    void fetch_vacio() { fb_dir_ = 1; }

    // =========================================================================
    // Contadores
    // =========================================================================
    void cuenta(unsigned ciclos, bool retirada) {
        const bool parar = debug_ && (dcsr_ & DCSR_STOPCOUNT);
        if (!parar) {
            if (!(mcountinhibit_ & 1u) && !escrito_cy_) mcycle_ += ciclos;
            if (retirada && !(mcountinhibit_ & 4u) && !escrito_ir_) ++minstret_;
        }
        escrito_cy_ = escrito_ir_ = false;
    }
    void fin_de_paso() {
        if (paso_pendiente_ && !debug_) {
            paso_pendiente_ = false;
            entra_debug(RV_DBG_STEP);
        }
    }

    // =========================================================================
    // Legalizaciones
    // =========================================================================
    uint32_t legaliza_tvec(uint32_t v) const {
        uint32_t m = v & 3u;
        if (m >= 2) m &= 1u;
        if (m == 0 && !(caps_.mtvec_modos & RV_MTVEC_DIRECTO)) m = 1;
        if (m == 1 && !(caps_.mtvec_modos & RV_MTVEC_VECTORIZADO)) m = 0;
        const uint32_t base = v & ~3u & ~(caps_.mtvec_alin - 1u);
        return base | m;
    }
    uint32_t legaliza_mpp(uint32_t ms) const {
        const uint32_t mpp = (ms & MS_MPP) >> 11;
        uint32_t nuevo = mpp;
        if (mpp == 1 || mpp == 2) nuevo = caps_.modo_u() ? 0u : 3u;
        if (mpp == 0 && !caps_.modo_u()) nuevo = 3u;
        return (ms & ~MS_MPP) | (nuevo << 11);
    }
    void escribe_dcsr(uint32_t v) {
        uint32_t m = DCSR_EBREAKM | DCSR_STEPIE | DCSR_STOPCOUNT | DCSR_STOPTIME | DCSR_STEP | 3u;
        if (caps_.modo_u()) m |= DCSR_EBREAKU;
        uint32_t nv = (dcsr_ & ~m) | (v & m);
        const uint32_t prv = nv & 3u;
        if (prv != 3u && !(prv == 0 && caps_.modo_u())) nv = (nv & ~3u) | 3u;
        dcsr_ = nv;
    }
    static uint32_t vacio_tdata1() { return 2u << 28; }   // mcontrol, apagado
    void escribe_tdata1(uint32_t v) {
        uint32_t& t = tdata1_[tselect_];
        if ((t & MC_DMODE) && !debug_) return;           // de la depuración
        const unsigned tipo = v >> 28;
        if (tipo == 0) { t = vacio_tdata1(); tdata2_[tselect_] = 0; return; }
        uint32_t m = MC_HIT | MC_ACTION | MC_MATCH | MC_M | MC_EXEC | MC_STORE | MC_LOAD;
        if (caps_.modo_u()) m |= MC_U;
        if (debug_) m |= MC_DMODE;
        uint32_t nv = (2u << 28) | (v & m);
        // maskmax = 0 (sin NAPOT); acción: 0 (excepción) o 1 (depuración, solo con dmode)
        const unsigned accion = (nv & MC_ACTION) >> 12;
        if (accion > 1 || (accion == 1 && !(nv & MC_DMODE))) nv &= ~MC_ACTION;
        const unsigned match = (nv & MC_MATCH) >> 7;
        if (match != 0 && match != 2 && match != 3) nv &= ~MC_MATCH;
        t = nv;
    }

    // =========================================================================
    // Traps
    // =========================================================================
    void toma_trap(bool interr, uint32_t causa, uint32_t tval, uint32_t epc, bool a_u = false) {
        ++n_traps_;
        resv_ = false;
        if (a_u) {
            uepc_ = epc;
            ucause_ = (interr ? 0x80000000u : 0u) | causa;
            if (mstatus_ & MS_UIE) mstatus_ |= MS_UPIE; else mstatus_ &= ~MS_UPIE;
            mstatus_ &= ~MS_UIE;
            const uint32_t base = utvec_ & ~3u;
            pc_ = (interr && (utvec_ & 3u) == 1u) ? base + 4u * causa : base;
            fetch_vacio();
            return;
        }
        mepc_ = epc;
        mcause_ = (interr ? 0x80000000u : 0u) | causa;
        mtval_ = tval;
        if (mstatus_ & MS_MIE) mstatus_ |= MS_MPIE; else mstatus_ &= ~MS_MPIE;
        mstatus_ &= ~MS_MIE;
        mstatus_ = (mstatus_ & ~MS_MPP) | (uint32_t(modo_) << 11);
        if (caps_.dbg.tcontrol) {
            tcontrol_ = (tcontrol_ & ~TC_MPTE) | ((tcontrol_ & TC_MTE) ? TC_MPTE : 0u);
            tcontrol_ &= ~TC_MTE;
        }
        modo_ = RvModo::M;
        const uint32_t base = mtvec_ & ~3u;
        pc_ = (interr && (mtvec_ & 3u) == 1u) ? base + 4u * causa : base;
        fetch_vacio();
    }
    // Una excepción de la instrucción en curso
    void excepcion(uint32_t causa, uint32_t tval) {
        trap_ = true;
        if (progbuf_) { excepcion_pb_ = true; return; }  // en el program buffer: al DM
        toma_trap(false, causa, tval, pc_);
    }
    // mtval de una ilegal: la codificación tal como estaba en memoria (16 bits
    // si era comprimida), que es lo que pone Spike
    bool ilegal(uint32_t /*insn*/) { excepcion(RV_EXC_ILEGAL, crudo_); return false; }

    // La LATENCIA (RvTiempos::lat_irq, de 4 ciclos en el C3 [TRM-C3
    // §1.5.3.1]): una petición que aparece durante una instrucción no puede
    // entrar en la frontera que sigue, sino en la siguiente; se factura al
    // entrar. El software cuenta con ello: la ROM del C3 habilita la
    // interrupción de la UART y, en la instrucción siguiente, dice qué UART es
    // la suya (uart_buff_switch); si entrase en el acto, la rutina leería la
    // que no es. Las que ya estaban pedidas compiten todas a la vez, por
    // prioridad. Con lat_irq = 0 (los de laboratorio), nada.
    uint32_t visibles(uint32_t hw) {
        if (!caps_.tiempos->lat_irq) return hw;
        // Dormido en wfi, lo que lo despierta entra en el acto: el despertar
        // tarda más que la latencia, y el silicio no ejecuta nada antes del
        // trap (si no, un `lw` tras el wfi leería el dato de antes de la ISR)
        const uint32_t v = wfi_ ? hw : (hw & hw_ant_);
        hw_ant_ = hw;
        return v;
    }
    bool interrupcion(unsigned& id, bool& a_u) {
        const uint32_t hw = visibles(irq_ ? irq_->pendientes() : 0u);
        const uint32_t pend = caps_.mie_mip ? (hw & mie_ & caps_.mie_bits) : hw;
        if (!pend) return false;
        const uint32_t deleg = caps_.deleg_n ? mideleg_ : 0u;
        const bool m_ok = modo_ != RvModo::M || (mstatus_ & MS_MIE);
        const uint32_t cm = m_ok ? (pend & ~deleg) : 0u;
        const bool u_ok = caps_.deleg_n && modo_ == RvModo::U && (mstatus_ & MS_UIE);
        const uint32_t cu = u_ok ? (pend & deleg) : 0u;
        auto elige = [&](uint32_t c, unsigned& i) {
            return irq_ ? irq_->elige(c, i) : rv_elige_estandar(c, i);
        };
        if (cm && elige(cm, id)) { a_u = false; return true; }
        if (cu && elige(cu, id)) { a_u = true; return true; }
        return false;
    }

    // =========================================================================
    // Modo depuración
    // =========================================================================
    void entra_debug(uint32_t causa) {
        dpc_ = pc_;
        dcsr_causa_ = causa;
        dcsr_ = (dcsr_ & ~3u) | uint32_t(modo_);
        debug_ = true;
        wfi_ = false;
        paso_pendiente_ = false;
    }
    void sale_debug() {
        modo_ = RvModo(dcsr_ & 3u);
        pc_ = dpc_;
        fetch_vacio();
        debug_ = false;
        if (dcsr_ & DCSR_STEP) paso_pendiente_ = true;
    }

    // =========================================================================
    // Triggers (mcontrol, timing = antes)
    // =========================================================================
    // ¿Salta algún trigger para este acceso? `tipo`: MC_EXEC, MC_LOAD o MC_STORE.
    // Devuelve el índice o -1.
    int trigger(uint32_t tipo, uint32_t dir) {
        if (debug_ || !caps_.dbg.triggers) return -1;
        const uint32_t bit_modo = modo_ == RvModo::M ? MC_M : MC_U;
        for (unsigned i = 0; i < caps_.dbg.triggers; ++i) {
            const uint32_t t = tdata1_[i];
            if ((t >> 28) != 2u || !(t & tipo) || !(t & bit_modo)) continue;
            const unsigned accion = (t & MC_ACTION) >> 12;
            // Con tcontrol, un trigger de excepción no salta en M hasta mte
            if (accion == 0 && modo_ == RvModo::M && caps_.dbg.tcontrol && !(tcontrol_ & TC_MTE))
                continue;
            const unsigned match = (t & MC_MATCH) >> 7;
            const uint32_t d = tdata2_[i];
            const bool ok = match == 0 ? dir == d : match == 2 ? dir >= d : dir < d;
            if (!ok) continue;
            tdata1_[i] |= MC_HIT;
            return int(i);
        }
        return -1;
    }
    // Lo que hace un trigger que ha saltado. `false`: la instrucción no sigue.
    // `tval` es la dirección que coincidió: el PC en uno de ejecución, la del
    // dato en uno de carga o almacenamiento [Privileged, §3.1.16].
    bool dispara(int i, uint32_t tval) {
        const unsigned accion = (tdata1_[unsigned(i)] & MC_ACTION) >> 12;
        trap_ = true;
        if (accion == 1) { entra_debug(RV_DBG_TRIGGER); return false; }
        excepcion(RV_EXC_BREAKPOINT, tval);
        return false;
    }

    // =========================================================================
    // PMP [Privileged, §3.7]
    // =========================================================================
    // ¿Puede `modo` hacer `perm` (R, W o X) en [dir, dir+n)?
    bool pmp_ok(uint32_t dir, unsigned n, uint8_t perm, RvModo modo) const {
        if (debug_) return true;
        return pmp_ok_m_o(dir, n, perm, modo);
    }
    // La PMP de modo M aunque el hart esté en depuración (la región del DM)
    bool pmp_ok_m(uint32_t dir, unsigned n, uint8_t perm) const { return pmp_ok_m_o(dir, n, perm, RvModo::M); }
    bool pmp_ok_m_o(uint32_t dir, unsigned n, uint8_t perm, RvModo modo) const {
        if (!caps_.pmp_regiones) return true;
        const uint64_t lo = dir, hi = uint64_t(dir) + n;
        for (unsigned i = 0; i < caps_.pmp_regiones; ++i) {
            const uint8_t c = pmpcfg_[i];
            const unsigned a = (c & PMP_A) >> 3;
            if (a == 0) continue;
            uint64_t b0, b1;
            if (a == 1) {                                   // TOR
                b0 = i ? (uint64_t(pmpaddr_[i - 1]) << 2) : 0;
                b1 = uint64_t(pmpaddr_[i]) << 2;
            } else if (a == 2) {                            // NA4
                b0 = uint64_t(pmpaddr_[i]) << 2; b1 = b0 + 4;
            } else {                                        // NAPOT
                const uint32_t pa = pmpaddr_[i];
                unsigned unos = 0;
                while (unos < 32 && ((pa >> unos) & 1u)) ++unos;
                const uint64_t tam = uint64_t(8) << unos;
                b0 = (uint64_t(pa) << 2) & ~(tam - 1); b1 = b0 + tam;
            }
            if (b0 >= b1) continue;                         // TOR vacío: no coincide nada
            const bool dentro_lo = lo >= b0 && lo < b1, dentro_hi = hi > b0 && hi <= b1;
            if (!dentro_lo && !dentro_hi && !(lo < b0 && hi > b1)) continue;
            if (!(dentro_lo && dentro_hi)) return false;    // coincidencia parcial
            if (modo == RvModo::M && !(c & PMP_L)) return true;
            return (c & perm) != 0;
        }
        return modo == RvModo::M;
    }
    // El índice de la regla PMP que manda en una dirección (o -1), y sus
    // permisos: lo que compara la errata CPU-863
    uint32_t pmp_firma(uint32_t dir) const {
        for (unsigned i = 0; i < caps_.pmp_regiones; ++i) {
            const uint8_t c = pmpcfg_[i];
            if (!(c & PMP_A)) continue;
            if (pmp_ok_regla(i, dir)) return (i << 8) | (c & 7u);
        }
        return 0xFFFFu;
    }
    bool pmp_ok_regla(unsigned i, uint32_t dir) const {
        const uint8_t c = pmpcfg_[i];
        const unsigned a = (c & PMP_A) >> 3;
        uint64_t b0, b1;
        if (a == 1) { b0 = i ? (uint64_t(pmpaddr_[i - 1]) << 2) : 0; b1 = uint64_t(pmpaddr_[i]) << 2; }
        else if (a == 2) { b0 = uint64_t(pmpaddr_[i]) << 2; b1 = b0 + 4; }
        else {
            const uint32_t pa = pmpaddr_[i];
            unsigned unos = 0;
            while (unos < 32 && ((pa >> unos) & 1u)) ++unos;
            const uint64_t tam = uint64_t(8) << unos;
            b0 = (uint64_t(pa) << 2) & ~(tam - 1); b1 = b0 + tam;
        }
        return dir >= b0 && dir < b1;
    }
    RvModo modo_datos() const {
        if (modo_ == RvModo::M && (mstatus_ & MS_MPRV)) return RvModo((mstatus_ & MS_MPP) >> 11);
        return modo_;
    }

    // =========================================================================
    // La memoria, con lo que la rodea
    // =========================================================================
    bool lee_bus(uint32_t a, unsigned n, uint32_t& v, RvAcceso t, RvModo m) {
        if (en_dm(a)) return dm_mem(a, n, v, false);
        return mem_ && mem_->lee(a, n, v, t, m);
    }
    bool escribe_bus(uint32_t a, unsigned n, uint32_t v, RvAcceso t, RvModo m) {
        if (en_dm(a)) return dm_mem(a, n, v, true);
        if (fb_dir_ == (a & ~3u) || fb_dir_ == ((a + n - 1) & ~3u)) fetch_vacio();
        return mem_ && mem_->escribe(a, n, v, t, m);
    }
    // La región del DM vista por las cargas y los almacenamientos: no va al
    // bus. Desde el program buffer se leen el program buffer y los datos, y
    // los datos se escriben (como `dataaccess`, aunque hartinfo no lo diga:
    // dataaddr no llega a 0x2000_0000); fuera del modo depuración, error de
    // acceso [de modelo: el TRM solo da la región]
    bool dm_mem(uint32_t a, unsigned n, uint32_t& v, bool escribe) {
        if (!progbuf_) return false;
        const uint32_t pd = caps_.dbg.dm_base + DM_DATOS, pb = pb_base();
        const uint32_t w = a & ~3u, sh = (a & 3u) * 8u;
        const uint32_t m = n >= 4 ? 0xFFFFFFFFu : ((1u << (8 * n)) - 1u);
        if ((a & 3u) + n > 4) return false;
        if (w >= pd && w < pd + 4u * dm_nd_ && dm_datos_) {
            uint32_t& d = dm_datos_[(w - pd) / 4u];
            if (escribe) d = (d & ~(m << sh)) | ((v & m) << sh);
            else v = (d >> sh) & m;
            return true;
        }
        if (escribe) return true;                // el resto, de solo lectura
        if (w >= pb && w < pb + 4u * progbuf_n_) v = (progbuf_[(w - pb) / 4u] >> sh) & m;
        else v = 0;
        return true;
    }
    bool carga(uint32_t a, unsigned n, uint32_t& v) {
        const int tr = trigger(MC_LOAD, a);
        if (tr >= 0) return dispara(tr, a);
        const RvModo md = modo_datos();
        const bool desal = (a & (n - 1)) != 0;
        if (desal && caps_.desalineado == RvDesalineado::Trap) {
            excepcion(RV_EXC_LOAD_DESALINEADA, a); return false;
        }
        if (!pmp_ok(a, n, PMP_R, md)) { excepcion(RV_EXC_LOAD_ACCESO, a); return false; }
        if (!desal) {
            if (!lee_bus(a, n, v, RvAcceso::Load, md)) { excepcion(RV_EXC_LOAD_ACCESO, a); return false; }
            return true;
        }
        uint32_t r = 0;
        for (unsigned k = 0; k < n; ++k) {
            uint32_t b = 0;
            if (!lee_bus(a + k, 1, b, RvAcceso::Load, md)) { excepcion(RV_EXC_LOAD_ACCESO, a); return false; }
            r |= (b & 0xFFu) << (8 * k);
        }
        v = r;
        return true;
    }
    bool almacena(uint32_t a, unsigned n, uint32_t v) {
        const int tr = trigger(MC_STORE, a);
        if (tr >= 0) return dispara(tr, a);
        const RvModo md = modo_datos();
        const bool desal = (a & (n - 1)) != 0;
        if (desal && caps_.desalineado == RvDesalineado::Trap) {
            excepcion(RV_EXC_STORE_DESALINEADA, a); return false;
        }
        if (!pmp_ok(a, n, PMP_W, md)) { excepcion(RV_EXC_STORE_ACCESO, a); return false; }
        if (!desal) {
            if (!escribe_bus(a, n, v, RvAcceso::Store, md)) { excepcion(RV_EXC_STORE_ACCESO, a); return false; }
            return true;
        }
        for (unsigned k = 0; k < n; ++k)
            if (!escribe_bus(a + k, 1, (v >> (8 * k)) & 0xFFu, RvAcceso::Store, md)) {
                excepcion(RV_EXC_STORE_ACCESO, a); return false;
            }
        return true;
    }

    // --- La búsqueda --------------------------------------------------------
    bool lee_parcela(uint32_t a, uint32_t& p) {
        if (progbuf_) {
            const uint32_t b = pb_base();
            if (caps_.dbg.dm_tam) {
                // En la región del DM la búsqueda pasa por la PMP como en modo
                // M (las reglas con L): por eso ESP-IDF le da una regla RWX
                if (!en_dm(a) || !pmp_ok_m(a, 2, PMP_X)) return false;
            }
            const unsigned i = (a - b) / 4u;
            if (a < b || i >= progbuf_n_) { p = 0x00100073u >> ((a & 2u) * 8u); return true; }
            p = (progbuf_[i] >> ((a & 2u) * 8u)) & 0xFFFFu;
            return true;
        }
        if (en_dm(a)) return false;             // fuera del modo depuración, nada
        const uint32_t w = a & ~3u;
        if (fb_dir_ != w) {
            if (!pmp_ok(w, 4, PMP_X, modo_) && !pmp_ok(a, 2, PMP_X, modo_)) return false;
            uint32_t d = 0;
            if (!lee_bus(w, 4, d, RvAcceso::Fetch, modo_)) return false;
            fb_dir_ = w; fb_dat_ = d;
        }
        if (!pmp_ok(a, 2, PMP_X, modo_)) return false;
        p = (fb_dat_ >> ((a & 2u) * 8u)) & 0xFFFFu;
        return true;
    }

    // =========================================================================
    // UNA INSTRUCCIÓN
    // =========================================================================
    Paso ejecuta_una() {
        Paso r;
        const RvTiempos& t = *caps_.tiempos;
        trap_ = false; rd_esc_ = -1;
        // Trigger de ejecución, antes de buscar
        if (!progbuf_) {
            const int tr = trigger(MC_EXEC, pc_);
            if (tr >= 0) {
                dispara(tr, pc_);
                if (debug_) { r.parado = true; return r; }
                r.trap = true; r.retirada = true; r.ciclos = t.trap;
                return r;
            }
        }
        uint32_t p0 = 0;
        if (!lee_parcela(pc_, p0)) {
            excepcion(RV_EXC_INST_ACCESO, pc_);
            r.trap = true; r.retirada = true; r.ciclos = t.trap; return r;
        }
        uint32_t insn, crudo;
        unsigned largo;
        if (rvd::es_comprimida(p0)) {
            largo = 2; crudo = p0; crudo_ = p0;
            if (!caps_.tiene(RV_EXT_C) ||
                !rvd::expande_c(uint16_t(p0), insn, caps_.tiene(RV_EXT_F), caps_.tiene(rv_letra('D')))) {
                ilegal(p0);
                r.trap = true; r.retirada = true; r.ciclos = t.trap; return r;
            }
        } else {
            uint32_t p1 = 0;
            if (!lee_parcela(pc_ + 2, p1)) {
                excepcion(RV_EXC_INST_ACCESO, pc_ + 2);
                r.trap = true; r.retirada = true; r.ciclos = t.trap; return r;
            }
            largo = 4; insn = crudo = crudo_ = p0 | (p1 << 16);
        }
        next_pc_ = pc_ + largo;
        const uint32_t pc_insn = pc_;
        const RvModo modo_insn = modo_;
        unsigned ciclos = 0;
        ejecuta(insn, largo, ciclos);
        if (debug_ && !progbuf_) { r.parado = true; return r; }   // ebreak a depuración
        if (trap_) {
            r.trap = true; r.retirada = true; r.ciclos = t.trap; return r;
        }
        if (progbuf_) { pc_ = next_pc_; r.ciclos = ciclos; r.retirada = true; return r; }
        pc_ = next_pc_;
        if (traza) traza(RvCommit{pc_insn, crudo, largo, rd_esc_, rd_val_, modo_insn});
        r.retirada = true;
        r.ciclos = ciclos;
        return r;
    }

    void wr(unsigned rd, uint32_t v) {
        if (!rd) return;
        x_[rd] = v;
        rd_esc_ = int(rd); rd_val_ = v;
    }
    bool salta(uint32_t destino) {
        if (!caps_.tiene(RV_EXT_C) && (destino & 3u)) {
            excepcion(RV_EXC_INST_DESALINEADA, destino);
            return false;
        }
        next_pc_ = destino;
        return true;
    }

    void ejecuta(uint32_t i, unsigned largo, unsigned& ciclos) {
        using namespace rvd;
        const RvTiempos& t = *caps_.tiempos;
        const unsigned rd_ = rd(i), r1 = rs1(i), r2 = rs2(i), f3 = funct3(i), f7 = funct7(i);
        const uint32_t a = x(r1), b = x(r2);
        switch (opcode(i)) {
        case OP_LUI:   ciclos = t.alu; wr(rd_, imm_u(i)); return;
        case OP_AUIPC: ciclos = t.alu; wr(rd_, pc_ + imm_u(i)); return;
        case OP_JAL: {
            ciclos = t.jal;
            const uint32_t ret = pc_ + largo;
            if (salta(pc_ + imm_j(i))) wr(rd_, ret);
            return;
        }
        case OP_JALR: {
            if (f3 != 0) { ilegal(i); return; }
            ciclos = t.jalr;
            const uint32_t ret = pc_ + largo;
            if (salta((a + imm_i(i)) & ~1u)) wr(rd_, ret);
            return;
        }
        case OP_BRANCH: {
            bool c;
            switch (f3) {
            case 0: c = a == b; break;
            case 1: c = a != b; break;
            case 4: c = int32_t(a) < int32_t(b); break;
            case 5: c = int32_t(a) >= int32_t(b); break;
            case 6: c = a < b; break;
            case 7: c = a >= b; break;
            default: ilegal(i); return;
            }
            ciclos = c ? t.salto_si : t.salto_no;
            if (c) salta(pc_ + imm_b(i));
            return;
        }
        case OP_LOAD: {
            const uint32_t dir = a + imm_i(i);
            unsigned n; bool sig;
            switch (f3) {
            case 0: n = 1; sig = true; break;
            case 1: n = 2; sig = true; break;
            case 2: n = 4; sig = false; break;
            case 4: n = 1; sig = false; break;
            case 5: n = 2; sig = false; break;
            default: ilegal(i); return;
            }
            ciclos = t.carga;
            if (caps_.desalineado == RvDesalineado::HardwareCpu863 && (dir & (n - 1)) &&
                ((dir & ~3u) != ((dir + n - 1) & ~3u)) && cpu863_choca(dir, n, i, largo)) {
                excepcion(RV_EXC_LOAD_ACCESO, dir);
                return;
            }
            uint32_t v = 0;
            if (!carga(dir, n, v)) return;
            if (n < 4) v = sig ? sext(v, 8 * n) : (v & ((1u << (8 * n)) - 1u));
            wr(rd_, v);
            return;
        }
        case OP_STORE: {
            const uint32_t dir = a + imm_s(i);
            unsigned n;
            switch (f3) { case 0: n = 1; break; case 1: n = 2; break; case 2: n = 4; break;
                          default: ilegal(i); return; }
            ciclos = t.almacen;
            almacena(dir, n, n == 4 ? b : (b & ((1u << (8 * n)) - 1u)));
            return;
        }
        case OP_IMM: {
            const uint32_t im = imm_i(i);
            const unsigned sh = (i >> 20) & 0x1Fu;
            ciclos = t.alu;
            switch (f3) {
            case 0: wr(rd_, a + im); return;
            case 2: wr(rd_, int32_t(a) < int32_t(im) ? 1u : 0u); return;
            case 3: wr(rd_, a < im ? 1u : 0u); return;
            case 4: wr(rd_, a ^ im); return;
            case 6: wr(rd_, a | im); return;
            case 7: wr(rd_, a & im); return;
            case 1:
                if (f7 != 0) { ilegal(i); return; }
                wr(rd_, a << sh); return;
            case 5:
                if (f7 == 0)    { wr(rd_, a >> sh); return; }
                if (f7 == 0x20) { wr(rd_, uint32_t(int32_t(a) >> sh)); return; }
                ilegal(i); return;
            }
            return;
        }
        case OP_OP: {
            if (f7 == 1) {
                if (!caps_.tiene(RV_EXT_M)) { ilegal(i); return; }
                ejecuta_m(f3, rd_, a, b, ciclos);
                return;
            }
            ciclos = t.alu;
            if (f7 == 0) {
                switch (f3) {
                case 0: wr(rd_, a + b); return;
                case 1: wr(rd_, a << (b & 31u)); return;
                case 2: wr(rd_, int32_t(a) < int32_t(b) ? 1u : 0u); return;
                case 3: wr(rd_, a < b ? 1u : 0u); return;
                case 4: wr(rd_, a ^ b); return;
                case 5: wr(rd_, a >> (b & 31u)); return;
                case 6: wr(rd_, a | b); return;
                case 7: wr(rd_, a & b); return;
                }
            }
            if (f7 == 0x20) {
                if (f3 == 0) { wr(rd_, a - b); return; }
                if (f3 == 5) { wr(rd_, uint32_t(int32_t(a) >> (b & 31u))); return; }
            }
            ilegal(i); return;
        }
        case OP_MISC_MEM:
            ciclos = t.alu;
            if (f3 == 0) return;                          // fence (y pause)
            if (f3 == 1 && caps_.tiene_z(RV_Z_IFENCEI)) { fetch_vacio(); return; }
            ilegal(i); return;
        case OP_AMO:
            if (!caps_.tiene(RV_EXT_A) || f3 != 2) { ilegal(i); return; }
            ciclos = t.amo;
            ejecuta_a(i, rd_, a, b);
            return;
        case OP_SYSTEM:
            ejecuta_system(i, rd_, r1, f3, a, ciclos);
            return;
        case OP_CUSTOM0: case OP_CUSTOM1: case OP_CUSTOM2: case OP_CUSTOM3:
            if (ext_ && ext_->ejecuta(*this, i, ciclos)) return;
            ilegal(i); return;
        default:
            ilegal(i); return;
        }
    }

    void ejecuta_m(unsigned f3, unsigned rd_, uint32_t a, uint32_t b, unsigned& ciclos) {
        const RvTiempos& t = *caps_.tiempos;
        const int32_t sa = int32_t(a), sb = int32_t(b);
        ciclos = f3 < 4 ? t.mul : t.div;
        switch (f3) {
        case 0: wr(rd_, a * b); return;
        case 1: wr(rd_, uint32_t((int64_t(sa) * int64_t(sb)) >> 32)); return;
        case 2: wr(rd_, uint32_t((int64_t(sa) * int64_t(uint64_t(b))) >> 32)); return;
        case 3: wr(rd_, uint32_t((uint64_t(a) * uint64_t(b)) >> 32)); return;
        case 4:
            if (b == 0) { wr(rd_, 0xFFFFFFFFu); return; }
            if (a == 0x80000000u && sb == -1) { wr(rd_, a); return; }
            wr(rd_, uint32_t(sa / sb)); return;
        case 5: wr(rd_, b == 0 ? 0xFFFFFFFFu : a / b); return;
        case 6:
            if (b == 0) { wr(rd_, a); return; }
            if (a == 0x80000000u && sb == -1) { wr(rd_, 0); return; }
            wr(rd_, uint32_t(sa % sb)); return;
        case 7: wr(rd_, b == 0 ? a : a % b); return;
        }
    }

    void ejecuta_a(uint32_t i, unsigned rd_, uint32_t dir, uint32_t b) {
        const unsigned f5 = i >> 27;
        if (dir & 3u) {
            // Desalineada: siempre excepción (no hay atómicas partidas)
            excepcion(f5 == 2 ? RV_EXC_LOAD_DESALINEADA : RV_EXC_STORE_DESALINEADA, dir);
            return;
        }
        const RvModo md = modo_datos();
        if (f5 == 2) {                                    // LR.W
            if (rvd::rs2(i) != 0) { ilegal(i); return; }
            const int tr = trigger(MC_LOAD, dir);
            if (tr >= 0) { dispara(tr, dir); return; }
            uint32_t v = 0;
            if (!pmp_ok(dir, 4, PMP_R, md) || !lee_bus(dir, 4, v, RvAcceso::Amo, md)) {
                excepcion(RV_EXC_LOAD_ACCESO, dir); return;
            }
            resv_ = true; resv_dir_ = dir;
            wr(rd_, v);
            return;
        }
        if (f5 == 3) {                                    // SC.W
            const int tr = trigger(MC_STORE, dir);
            if (tr >= 0) { dispara(tr, dir); return; }
            if (!pmp_ok(dir, 4, PMP_W, md) || (existe_dir && !existe_dir(dir, 4))) {
                excepcion(RV_EXC_STORE_ACCESO, dir); return;
            }
            if (resv_ && resv_dir_ == dir) {
                if (!escribe_bus(dir, 4, b, RvAcceso::Amo, md)) { excepcion(RV_EXC_STORE_ACCESO, dir); return; }
                resv_ = false;
                wr(rd_, 0);
            } else {
                resv_ = false;
                wr(rd_, 1);
            }
            return;
        }
        int tr = trigger(MC_STORE, dir);
        if (tr < 0) tr = trigger(MC_LOAD, dir);
        if (tr >= 0) { dispara(tr, dir); return; }
        if (!pmp_ok(dir, 4, PMP_R, md) || !pmp_ok(dir, 4, PMP_W, md)) {
            excepcion(RV_EXC_STORE_ACCESO, dir); return;
        }
        uint32_t v = 0;
        if (!lee_bus(dir, 4, v, RvAcceso::Amo, md)) { excepcion(RV_EXC_STORE_ACCESO, dir); return; }
        uint32_t n;
        switch (f5) {
        case 0x01: n = b; break;                                          // AMOSWAP
        case 0x00: n = v + b; break;                                      // AMOADD
        case 0x04: n = v ^ b; break;                                      // AMOXOR
        case 0x0C: n = v & b; break;                                      // AMOAND
        case 0x08: n = v | b; break;                                      // AMOOR
        case 0x10: n = int32_t(v) < int32_t(b) ? v : b; break;            // AMOMIN
        case 0x14: n = int32_t(v) > int32_t(b) ? v : b; break;            // AMOMAX
        case 0x18: n = v < b ? v : b; break;                              // AMOMINU
        case 0x1C: n = v > b ? v : b; break;                              // AMOMAXU
        default: ilegal(i); return;
        }
        if (!escribe_bus(dir, 4, n, RvAcceso::Amo, md)) { excepcion(RV_EXC_STORE_ACCESO, dir); return; }
        wr(rd_, v);
    }

    void ejecuta_system(uint32_t i, unsigned rd_, unsigned r1, unsigned f3, uint32_t a,
                        unsigned& ciclos) {
        const RvTiempos& t = *caps_.tiempos;
        if (f3 == 0) {
            if (rd_ != 0) { ilegal(i); return; }
            switch (i >> 20) {
            case 0x000:                                   // ecall
                if (r1) { ilegal(i); return; }
                excepcion(modo_ == RvModo::M ? RV_EXC_ECALL_M : RV_EXC_ECALL_U, 0);
                return;
            case 0x001:                                   // ebreak
                if (r1) { ilegal(i); return; }
                if (progbuf_) { fin_pb_ = true; return; }
                if ((modo_ == RvModo::M && (dcsr_ & DCSR_EBREAKM)) ||
                    (modo_ == RvModo::U && (dcsr_ & DCSR_EBREAKU))) {
                    entra_debug(RV_DBG_EBREAK);
                    return;
                }
                excepcion(RV_EXC_BREAKPOINT, pc_);
                return;
            case 0x002:                                   // uret
                if (!caps_.deleg_n || r1) { ilegal(i); return; }
                ciclos = t.retorno;
                if (mstatus_ & MS_UPIE) mstatus_ |= MS_UIE; else mstatus_ &= ~MS_UIE;
                mstatus_ |= MS_UPIE;
                next_pc_ = uepc_;
                fetch_vacio();
                return;
            case 0x302: {                                 // mret
                if (modo_ != RvModo::M || r1) { ilegal(i); return; }
                ciclos = t.retorno;
                const RvModo nuevo = RvModo((mstatus_ & MS_MPP) >> 11);
                if (mstatus_ & MS_MPIE) mstatus_ |= MS_MIE; else mstatus_ &= ~MS_MIE;
                mstatus_ |= MS_MPIE;
                mstatus_ = (mstatus_ & ~MS_MPP) | (caps_.modo_u() ? 0u : (3u << 11));
                if (nuevo != RvModo::M) mstatus_ &= ~MS_MPRV;
                if (caps_.dbg.tcontrol)
                    tcontrol_ = (tcontrol_ & ~TC_MTE) | ((tcontrol_ & TC_MPTE) ? TC_MTE : 0u);
                modo_ = nuevo;
                next_pc_ = mepc_;
                fetch_vacio();
                return;
            }
            case 0x7B2:                                   // dret
                if (!debug_ || r1) { ilegal(i); return; }
                ciclos = t.retorno;
                if (progbuf_) { fin_pb_ = true; return; }
                sale_debug();
                next_pc_ = pc_;
                return;
            case 0x105:                                   // wfi
                if (r1) { ilegal(i); return; }
                if (modo_ == RvModo::U && (mstatus_ & MS_TW)) { ilegal(i); return; }
                ciclos = t.alu;
                if (!progbuf_ && !debug_) wfi_ = true;
                return;
            default:
                ilegal(i); return;
            }
        }
        if (f3 == 4) { ilegal(i); return; }
        // CSRRW/S/C y sus inmediatos
        ciclos = t.csr;
        const uint16_t csr = uint16_t(i >> 20);
        const bool inmediato = f3 & 4u;
        const uint32_t src = inmediato ? r1 : a;
        const unsigned op = f3 & 3u;
        const bool escribe = (op == 1) || (r1 != 0);
        const bool lee = (op != 1) || (rd_ != 0);
        // Privilegio y solo-lectura [Privileged, §2.1]
        const unsigned priv_min = (csr >> 8) & 3u;
        if (!debug_ && unsigned(modo_) < priv_min) { ilegal(i); return; }
        if (escribe && (csr >> 10) == 3u) { ilegal(i); return; }
        // Los contadores de usuario, con mcounteren
        if (!debug_ && modo_ == RvModo::U && ((csr >= 0xC00 && csr <= 0xC1F) ||
                                               (csr >= 0xC80 && csr <= 0xC9F))) {
            if (!((mcounteren_ >> (csr & 0x1Fu)) & 1u)) { ilegal(i); return; }
        }
        uint32_t viejo = 0;
        if (!csr_lee(csr, viejo)) { ilegal(i); return; }
        if (escribe) {
            uint32_t nuevo = op == 1 ? src : op == 2 ? (viejo | src) : (viejo & ~src);
            if (!csr_escribe(csr, nuevo)) { ilegal(i); return; }
        }
        if (lee) wr(rd_, viejo);
    }

    // ¿Choca esta carga desalineada con el almacenamiento que la sigue, según
    // la errata CPU-863? Se mira la instrucción siguiente sin ejecutarla.
    bool cpu863_choca(uint32_t dir, unsigned n, uint32_t insn, unsigned largo) {
        uint32_t p0 = 0, p1 = 0, sig;
        const uint32_t pcs = pc_ + largo;
        if (!lee_bus(pcs & ~1u, 2, p0, RvAcceso::Fetch, modo_)) return false;
        if (rvd::es_comprimida(p0)) {
            if (!rvd::expande_c(uint16_t(p0), sig)) return false;
        } else {
            if (!lee_bus(pcs + 2, 2, p1, RvAcceso::Fetch, modo_)) return false;
            sig = (p0 & 0xFFFFu) | (p1 << 16);
        }
        if (rvd::opcode(sig) != rvd::OP_STORE) return false;
        // La base del almacenamiento, ya con lo que escribe la carga
        const unsigned r1 = rvd::rs1(sig);
        uint32_t base = x(r1);
        if (r1 && r1 == rvd::rd(insn)) return false;   // depende de la carga: no se adelanta
        const uint32_t st = base + rvd::imm_s(sig);
        const uint32_t segunda = (dir + n - 1) & ~3u;
        uint32_t fa = pmp_firma(segunda), fb = pmp_firma(st);
        if (permisos_extra) { fa = (fa << 16) ^ permisos_extra(segunda); fb = (fb << 16) ^ permisos_extra(st); }
        return fa != fb;
    }
};

} // namespace stm32
#endif // STM32_RV_HART_RV_HART_H
