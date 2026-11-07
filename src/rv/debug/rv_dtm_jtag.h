// =============================================================================
// rv_dtm_jtag.h — El Debug Transport Module por JTAG [Debug Spec 0.13.2, §6]
//
// E2 de doc/analisis_esp32_riscv.md, §3.6. C++ puro: la máquina del TAP
// (IEEE 1149.1, los 16 estados), un IR de 5 bits y los cuatro registros de
// datos que pide la especificación:
//
//   0x01 IDCODE   el del chip (lo pone quien lo integra)
//   0x10 dtmcs    version = 1 (0.13), abits, idle, dmistat; dmireset y
//                 dmihardreset
//   0x11 dmi      abits + 34 bits: dirección, dato y op; la operación se hace
//                 en Update-DR y su resultado se recoge en el Capture-DR
//                 siguiente
//   0x1F BYPASS   (y cualquier otro valor del IR)
//
// Lo mueve quien tenga los pines (`jtag_pines.h`): `subida(tms, tdi)` en
// cada flanco de subida de TCK, y `tdo()` es lo que hay que sacar por TDO a
// partir del flanco de bajada siguiente. Como el DM contesta en el acto, el
// DMI no está nunca ocupado (op = 3): basta `idle` = 1 [§6.1.4].
// =============================================================================
#ifndef STM32_RV_DEBUG_RV_DTM_JTAG_H
#define STM32_RV_DEBUG_RV_DTM_JTAG_H

#include <cstdint>
#include "dmi_if.h"

namespace stm32 {

class RvDtmJtag {
public:
    enum Estado : uint8_t {
        TLR, RTI, SEL_DR, CAP_DR, SH_DR, EX1_DR, PAU_DR, EX2_DR, UPD_DR,
        SEL_IR, CAP_IR, SH_IR, EX1_IR, PAU_IR, EX2_IR, UPD_IR,
    };
    enum : uint8_t { IR_IDCODE = 0x01, IR_DTMCS = 0x10, IR_DMI = 0x11, IR_BYPASS = 0x1F };

    RvDtmJtag(dmi_if& dm, uint32_t idcode, unsigned abits = 7)
        : dm_(dm), idcode_(idcode | 1u), abits_(abits) {}

    // TRST, o cinco unos en TMS: Test-Logic-Reset
    void trst() { st_ = TLR; ir_ = IR_IDCODE; }

    // Un flanco de subida de TCK
    void subida(bool tms, bool tdi) {
        ++flancos_;
        // Lo que se hace en el estado en el que se está
        switch (st_) {
        case CAP_DR: captura(); break;
        case SH_DR: {
            const unsigned n = largo_dr();
            sr_ >>= 1;
            if (tdi) sr_ |= uint64_t(1) << (n - 1);
            break;
        }
        case CAP_IR: sr_ = 0x01; break;                // 0b00001, como pide 1149.1
        case SH_IR:
            sr_ >>= 1;
            if (tdi) sr_ |= uint64_t(1) << 4;
            break;
        default: break;
        }
        // Y la transición
        const Estado a = st_;
        st_ = siguiente(st_, tms);
        if (st_ == TLR) ir_ = IR_IDCODE;
        // La actualización va al ENTRAR en Update (en el silicio, en el flanco
        // de bajada que sigue: para quien mira desde fuera es lo mismo)
        if (st_ == UPD_DR && a != UPD_DR) actualiza_dr();
        if (st_ == UPD_IR && a != UPD_IR) ir_ = uint8_t(sr_ & 0x1Fu);
    }

    // Lo que sale por TDO (vale desde el flanco de bajada)
    bool tdo() const { return (st_ == SH_DR || st_ == SH_IR) && (sr_ & 1u); }
    bool desplazando() const { return st_ == SH_DR || st_ == SH_IR; }

    Estado estado() const { return st_; }
    uint8_t ir() const { return ir_; }
    uint64_t flancos() const { return flancos_; }
    uint64_t operaciones_dmi() const { return n_dmi_; }
    unsigned abits() const { return abits_; }

    static Estado siguiente(Estado s, bool tms) {
        static const Estado t[16][2] = {
            {RTI, TLR},       {RTI, SEL_DR},    {CAP_DR, SEL_IR}, {SH_DR, EX1_DR},
            {SH_DR, EX1_DR},  {PAU_DR, UPD_DR}, {PAU_DR, EX2_DR}, {SH_DR, UPD_DR},
            {RTI, SEL_DR},    {CAP_IR, TLR},    {SH_IR, EX1_IR},  {SH_IR, EX1_IR},
            {PAU_IR, UPD_IR}, {PAU_IR, EX2_IR}, {SH_IR, UPD_IR},  {RTI, SEL_DR},
        };
        return t[s][tms ? 1 : 0];
    }

private:
    dmi_if& dm_;
    uint32_t idcode_;
    unsigned abits_;
    Estado st_ = TLR;
    uint8_t ir_ = IR_IDCODE;
    uint64_t sr_ = 0;
    uint64_t flancos_ = 0, n_dmi_ = 0;
    // El resultado de la última operación del DMI
    uint32_t dmi_dir_ = 0, dmi_dato_ = 0;
    unsigned dmi_op_ = 0;            // 0 bien, 2 fallo (sticky hasta dmireset)

    unsigned largo_dr() const {
        switch (ir_) {
        case IR_IDCODE: case IR_DTMCS: return 32;
        case IR_DMI: return abits_ + 34;
        default: return 1;
        }
    }
    uint32_t dtmcs() const {
        return (dmi_op_ << 10) | (1u << 12) | (abits_ << 4) | 1u;   // idle = 1, version = 1
    }
    void captura() {
        switch (ir_) {
        case IR_IDCODE: sr_ = idcode_; break;
        case IR_DTMCS:  sr_ = dtmcs(); break;
        case IR_DMI:
            sr_ = (uint64_t(dmi_dir_) << 34) | (uint64_t(dmi_dato_) << 2) | dmi_op_;
            break;
        default: sr_ = 0; break;
        }
    }
    void actualiza_dr() {
        if (ir_ == IR_DTMCS) {
            if (sr_ & ((1u << 16) | (1u << 17))) dmi_op_ = 0;     // dmireset / dmihardreset
            return;
        }
        if (ir_ != IR_DMI) return;
        const unsigned op = unsigned(sr_ & 3u);
        const uint32_t dato = uint32_t(sr_ >> 2);
        const uint32_t dir = uint32_t(sr_ >> 34) & ((1u << abits_) - 1u);
        if (op == 0 || dmi_op_ != 0) return;          // nop, o un fallo pegajoso
        ++n_dmi_;
        dmi_dir_ = dir;
        bool ok;
        if (op == 1) ok = dm_.dmi_leer(dir, dmi_dato_);
        else if (op == 2) { ok = dm_.dmi_escribir(dir, dato); dmi_dato_ = dato; }
        else ok = false;
        if (!ok) dmi_op_ = 2;
    }
};

} // namespace stm32
#endif // STM32_RV_DEBUG_RV_DTM_JTAG_H
