// =============================================================================
// tap_m430.h — El TAP JTAG de un MSP430 y su control de la CPU (M2 de
// doc/analisis_msp430.md, §7.1)
//
// [JTAG] es SLAU320AJ; [MSPDS], JTAG_defs.h y edt.h de la MSP Debug Stack.
//
//   * La máquina de 16 estados de IEEE 1149.1 con una IR DE 8 BITS: al
//     desplazar la IR sale el JTAG ID (0x89 en 1xx/2xx/4xx, 0x91 en 5xx/6xx,
//     0x98 en FR2xx/FR4xx, 0x99 en FR5xx/FR6xx) [JTAG §2.2.2].
//   * TCLK es TDI mientras el TAP está en Run-Test/Idle [JTAG §2.2.3]: cada
//     flanco es un ciclo de la CPU cuando está bajo JTAG (TCE1).
//   * El registro de control de 1xx/2xx/4xx [JTAG tabla 2-6]: R/W (0),
//     HALT_JTAG (3), BYTE (4), INSTR_LOAD (7, solo lectura), TCE (9, solo
//     lectura), TCE1 (10), POR (11), RELEASE_LBYTE (12), TAGFUNCSAT (13).
//   * Los registros de datos de §2.2.4: ADDR_16BIT (el MAB de JTAG; 20 bits
//     en la CPUX), ADDR_CAPTURE, DATA_TO_ADDR (el MDB de JTAG; con
//     HALT_JTAG, un flanco de subida de TCLK hace el acceso en el MAB de
//     JTAG), DATA_16BIT (el MDB de JTAG para lo que la CPU lee; el MAB es el
//     de la CPU), DATA_CAPTURE (lee el MDB sin tocarlo), CNTRL_SIG_16BIT, su
//     CAPTURE y su RELEASE, BYPASS, EMEX_DATA_EXCHANGE (el EEM: primero la
//     dirección con el bit 0 de lectura, y luego el dato [MSPDS
//     EemDataExchange.c]), PREPARE_BLOW y EX_BLOW (el fusible).
//
// LO QUE NO ESTÁ [declarado]: DATA_QUICK y DATA_PSA/SHIFT_OUT_PSA contestan
// el MDB y avisan una vez; los registros de 5xx/FR (CNTRL de Xv2, JMB,
// JSTATE, DEVICE_ID, COREIP_ID) son de M4 y M6. Las instrucciones se
// comprueban contra las de JTAG_defs.h con sus bits invertidos (test430 N7).
// =============================================================================
#ifndef STM32_M430_DEBUG_TAP_M430_H
#define STM32_M430_DEBUG_TAP_M430_H

#include <cstdint>
#include <cstdio>
#include "eem.h"

namespace stm32 {

// Los códigos de [JTAG] (los de JTAG_defs.h, con los bits al revés)
enum : uint8_t {
    IR430_ADDR_16BIT = 0x83, IR430_ADDR_CAPTURE = 0x84, IR430_DATA_TO_ADDR = 0x85,
    IR430_CAPTURE_CPU_REG = 0x86, IR430_DEVICE_ID = 0x87,
    IR430_DATA_16BIT = 0x41, IR430_DATA_CAPTURE = 0x42, IR430_DATA_QUICK = 0x43, IR430_DATA_PSA = 0x44,
    IR430_SHIFT_OUT_PSA = 0x46,
    IR430_EMEX_DATA_EXCHANGE = 0x09, IR430_EMEX_READ_TRIGGER = 0x0A, IR430_EMEX_READ_CONTROL = 0x0B,
    IR430_EMEX_WRITE_CONTROL = 0x0C,
    IR430_CNTRL_SIG_HIGH_BYTE = 0x11, IR430_CNTRL_SIG_LOW_BYTE = 0x12, IR430_CNTRL_SIG_16BIT = 0x13, IR430_CNTRL_SIG_CAPTURE = 0x14, IR430_CNTRL_SIG_RELEASE = 0x15,
    IR430_COREIP_ID = 0x17, IR430_PREPARE_BLOW = 0x22, IR430_EX_BLOW = 0x24,
    IR430_JMB_EXCHANGE = 0x61, IR430_JSTATE_ID = 0x62, IR430_TDO_EVENT = 0x64, IR430_TDO_EVENT_CTL = 0x65,
    IR430_BYPASS = 0xFF,
};

// El registro de control de 1xx/2xx/4xx [JTAG tabla 2-6]
enum : uint16_t {
    CS430_RW = 0x0001, CS430_HALT_JTAG = 0x0008, CS430_BYTE = 0x0010, CS430_INSTR_LOAD = 0x0080,
    CS430_TCE = 0x0200, CS430_TCE1 = 0x0400, CS430_POR = 0x0800, CS430_RELEASE_LBYTE = 0x1000,
    CS430_TAGFUNCSAT = 0x2000, CS430_SWITCH = 0x4000,
};

// Lo que el TAP pide al núcleo (NucleoM430)
struct m430_jtag_cpu_if {
    virtual ~m430_jtag_cpu_if() = default;
    virtual void jtag_control(bool tce1) = 0;          // la CPU bajo JTAG (TCLK) o libre
    virtual void jtag_halt(bool v) = 0;                // HALT_JTAG
    virtual void jtag_tclk(bool nivel) = 0;            // un flanco de TCLK
    virtual bool jtag_sincronizado() const = 0;        // TCE
    virtual bool jtag_instr_load() const = 0;          // INSTR_LOAD
    virtual void jtag_por(bool v) = 0;                 // el POR de JTAG
    virtual uint32_t jtag_mab() const = 0;             // el MAB de la CPU
    // Un acceso con el bus en manos de JTAG (HALT_JTAG)
    virtual uint16_t jtag_lee(uint32_t a, bool byte) = 0;
    virtual void jtag_escribe(uint32_t a, uint16_t v, bool byte) = 0;
};

class TapM430 {
public:
    enum Estado : uint8_t { TLR, RTI, SEL_DR, CAP_DR, SH_DR, EX1_DR, PAU_DR, EX2_DR, UPD_DR,
                            SEL_IR, CAP_IR, SH_IR, EX1_IR, PAU_IR, EX2_IR, UPD_IR };

    TapM430(uint8_t jtag_id, bool cpux, EemM430* eem) : id_(jtag_id), cpux_(cpux), eem_(eem) { reset(); }
    void pon_cpu(m430_jtag_cpu_if* c) { cpu_ = c; }
    uint8_t jtag_id() const { return id_; }

    void reset() {
        st_ = TLR;
        ir_ = IR430_BYPASS;
        cntrl_ = CS430_TAGFUNCSAT;
        sr_ = 0; n_ = 0; tclk_ = true; emex_fase_ = 0;
    }
    Estado estado() const { return st_; }
    uint8_t ir() const { return ir_; }
    uint16_t cntrl() const { return cntrl_; }
    bool tclk() const { return tclk_; }
    // El MDB de JTAG da el dato de lo que lea la CPU (IR_DATA_16BIT/QUICK)
    bool da_mdb() const { return ir_ == IR430_DATA_16BIT || ir_ == IR430_DATA_QUICK; }
    uint16_t mdb_jtag() const { return mdb_jtag_; }
    // Lo último que ha pasado por el MDB (lo que captura el TAP)
    void pon_mdb(uint16_t v) { mdb_ = v; }
    // El fusible: fundido, el TAP no deja entrar (solo BYPASS) [JTAG §2.3.6]
    bool fusible() const { return fusible_; }
    void restaura_fusible() { fusible_ = false; }
    uint64_t ciclos_tck() const { return n_tck_; }

    // TDI en Run-Test/Idle: TCLK
    void tdi_en_rti(bool nivel) {
        if (st_ != RTI || nivel == tclk_) return;
        tclk_ = nivel;
        if (!cpu_) return;
        // Con HALT_JTAG, la subida hace el acceso de JTAG en el MAB de JTAG
        if (nivel && (cntrl_ & CS430_HALT_JTAG) && (cntrl_ & CS430_TCE1) && ir_ == IR430_DATA_TO_ADDR) {
            const bool byte = (cntrl_ & CS430_BYTE) != 0;
            if (cntrl_ & CS430_RW) mdb_ = cpu_->jtag_lee(mab_jtag_, byte);
            else { cpu_->jtag_escribe(mab_jtag_, mdb_jtag_, byte); mdb_ = mdb_jtag_; }
        }
        cpu_->jtag_tclk(nivel);
    }

    // Un ciclo de TCK (en la subida): TMS y TDI; devuelve el TDO de después
    bool reloj(bool tms, bool tdi) {
        ++n_tck_;
        // Lo que sale por TDO es el bit 0 del registro de desplazamiento
        switch (st_) {
        case SH_DR: desplaza(tdi); break;
        case SH_IR: desplaza(tdi); break;
        default: break;
        }
        const Estado ant = st_;
        st_ = siguiente(st_, tms);
        if (st_ == CAP_DR) captura_dr();
        else if (st_ == CAP_IR) { n_ = 8; sr_ = invierte(fusible_ ? 0xFFu : id_, 8); }
        else if (st_ == UPD_DR) actualiza_dr();
        else if (st_ == UPD_IR) actualiza_ir();
        else if (st_ == TLR && ant != TLR) { ir_ = IR430_BYPASS; emex_fase_ = 0; }
        return tdo();
    }
    bool tdo() const { return (st_ == SH_DR || st_ == SH_IR) ? (sr_ & 1u) != 0 : false; }

    // El JTAG pierde la CPU (TEST bajo: se sale de JTAG)
    void suelta() {
        if (cpu_ && (cntrl_ & CS430_TCE1)) { cpu_->jtag_halt(false); cpu_->jtag_control(false); }
        reset();
    }

private:
    uint8_t id_;
    bool cpux_;
    EemM430* eem_;
    m430_jtag_cpu_if* cpu_ = nullptr;
    Estado st_ = TLR;
    uint8_t ir_ = IR430_BYPASS;
    uint16_t cntrl_ = CS430_TAGFUNCSAT, mdb_jtag_ = 0, mdb_ = 0;
    uint32_t mab_jtag_ = 0;
    uint64_t sr_ = 0, n_tck_ = 0;
    unsigned n_ = 0, emex_fase_ = 0;
    uint8_t emex_dir_ = 0;
    bool tclk_ = true, fusible_ = false, preparado_ = false, avisado_ = false;

    static Estado siguiente(Estado s, bool tms) {
        static const Estado T[16][2] = {
            {RTI, TLR}, {RTI, SEL_DR}, {CAP_DR, SEL_IR}, {SH_DR, EX1_DR}, {SH_DR, EX1_DR}, {PAU_DR, UPD_DR},
            {PAU_DR, EX2_DR}, {SH_DR, UPD_DR}, {RTI, SEL_DR}, {CAP_IR, TLR}, {SH_IR, EX1_IR}, {SH_IR, EX1_IR},
            {PAU_IR, UPD_IR}, {PAU_IR, EX2_IR}, {SH_IR, UPD_IR}, {RTI, SEL_DR}};
        return T[s][tms ? 1 : 0];
    }
    void desplaza(bool tdi) {
        // Los registros de MSP430 se desplazan con el MSB primero [JTAG §2.2.2]:
        // el registro se guarda invertido, y sale por su bit 0
        sr_ = (sr_ >> 1) | (uint64_t(tdi) << (n_ - 1));
    }
    // El registro de desplazamiento, con el MSB del valor en el bit 0
    static uint64_t invierte(uint64_t v, unsigned n) {
        uint64_t r = 0;
        for (unsigned i = 0; i < n; ++i) if ((v >> i) & 1u) r |= uint64_t(1) << (n - 1 - i);
        return r;
    }
    uint32_t valor_dr() const { return uint32_t(invierte(sr_, n_)); }

    unsigned largo_dr() const {
        switch (ir_) {
        case IR430_ADDR_16BIT: case IR430_ADDR_CAPTURE: return cpux_ ? 20 : 16;
        case IR430_BYPASS: return 1;
        case IR430_CNTRL_SIG_HIGH_BYTE: case IR430_CNTRL_SIG_LOW_BYTE: return 8;
        default: return 16;
        }
    }
    void captura_dr() {
        n_ = largo_dr();
        uint32_t v = 0;
        if (fusible_) { sr_ = 0; return; }
        switch (ir_) {
        case IR430_CNTRL_SIG_16BIT: case IR430_CNTRL_SIG_CAPTURE: v = estado_cntrl(); break;
        case IR430_CNTRL_SIG_HIGH_BYTE: v = estado_cntrl() >> 8; break;
        case IR430_CNTRL_SIG_LOW_BYTE: v = estado_cntrl() & 0xFFu; break;
        case IR430_ADDR_16BIT: case IR430_ADDR_CAPTURE: v = cpu_ ? cpu_->jtag_mab() : 0; break;
        case IR430_DATA_TO_ADDR: case IR430_DATA_16BIT: case IR430_DATA_CAPTURE: case IR430_DATA_QUICK:
            v = mdb_; break;
        case IR430_DATA_PSA: case IR430_SHIFT_OUT_PSA:
            if (!avisado_) { avisado_ = true; std::fprintf(stderr, "[tap430] PSA declarado (aviso una vez)\n"); }
            v = mdb_; break;
        case IR430_EMEX_DATA_EXCHANGE:
            v = (emex_fase_ == 1 && (emex_dir_ & 1u) && eem_) ? eem_->lee(uint8_t(emex_dir_ & ~1u)) : 0; break;
        default: v = 0; break;
        }
        sr_ = invierte(v, n_);
    }
    uint16_t estado_cntrl() const {
        uint16_t v = uint16_t(cntrl_ & ~(CS430_INSTR_LOAD | CS430_TCE));
        if (cpu_ && (cntrl_ & CS430_TCE1)) {
            if (cpu_->jtag_sincronizado()) v |= CS430_TCE;
            if (cpu_->jtag_instr_load()) v |= CS430_INSTR_LOAD;
        }
        return v;
    }
    void actualiza_ir() {
        ir_ = uint8_t(valor_ir());
        if (fusible_) { ir_ = IR430_BYPASS; return; }
        if (ir_ == IR430_EMEX_DATA_EXCHANGE) emex_fase_ = 0;
        if (ir_ == IR430_CNTRL_SIG_RELEASE || ir_ == IR430_BYPASS) {
            // RELEASE (y BYPASS, que lo hace a la vez [JTAG §2.2.4.2.4]): la CPU, libre
            if (cpu_ && (cntrl_ & CS430_TCE1)) { cpu_->jtag_halt(false); cpu_->jtag_control(false); }
            cntrl_ = uint16_t(cntrl_ & ~(CS430_TCE1 | CS430_HALT_JTAG));
        }
        if (ir_ == IR430_PREPARE_BLOW) preparado_ = true;
        else if (ir_ == IR430_EX_BLOW) { if (preparado_) fusible_ = true; preparado_ = false; }
        else preparado_ = false;
    }
    uint32_t valor_ir() const { return uint32_t(invierte(sr_, 8)); }
    void actualiza_dr() {
        if (fusible_) return;
        const uint32_t v = valor_dr();
        switch (ir_) {
        case IR430_CNTRL_SIG_16BIT: pon_cntrl(uint16_t(v)); break;
        case IR430_CNTRL_SIG_HIGH_BYTE: pon_cntrl(uint16_t((cntrl_ & 0x00FFu) | ((v & 0xFFu) << 8))); break;
        case IR430_CNTRL_SIG_LOW_BYTE: pon_cntrl(uint16_t((cntrl_ & 0xFF00u) | (v & 0xFFu))); break;
        case IR430_ADDR_16BIT: mab_jtag_ = v & (cpux_ ? 0xFFFFFu : 0xFFFFu); break;
        case IR430_DATA_TO_ADDR: case IR430_DATA_16BIT: case IR430_DATA_QUICK: mdb_jtag_ = uint16_t(v); break;
        case IR430_EMEX_DATA_EXCHANGE:
            if (emex_fase_ == 0) { emex_dir_ = uint8_t(v); emex_fase_ = 1; }
            else { if (!(emex_dir_ & 1u) && eem_) eem_->escribe(emex_dir_, uint16_t(v)); emex_fase_ = 0; }
            break;
        default: break;
        }
    }
    void pon_cntrl(uint16_t v) {
        const uint16_t ant = cntrl_;
        cntrl_ = uint16_t(v & ~(CS430_INSTR_LOAD | CS430_TCE));
        if (!cpu_) return;
        if ((cntrl_ ^ ant) & CS430_TCE1) cpu_->jtag_control((cntrl_ & CS430_TCE1) != 0);
        if ((cntrl_ ^ ant) & CS430_HALT_JTAG) cpu_->jtag_halt((cntrl_ & CS430_HALT_JTAG) != 0);
        if ((cntrl_ ^ ant) & CS430_POR) cpu_->jtag_por((cntrl_ & CS430_POR) != 0);
    }
};

} // namespace stm32
#endif // STM32_M430_DEBUG_TAP_M430_H
