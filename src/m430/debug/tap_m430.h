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
//     HALT_JTAG, la bajada de TCLK hace el acceso en el MAB de JTAG),
//     DATA_16BIT (el MDB de JTAG para lo que la CPU lee; el MAB es el
//     de la CPU), DATA_CAPTURE (lee el MDB sin tocarlo), CNTRL_SIG_16BIT, su
//     CAPTURE y su RELEASE, BYPASS, EMEX_DATA_EXCHANGE (el EEM: primero la
//     dirección con el bit 0 de lectura, y luego el dato [MSPDS
//     EemDataExchange.c]), PREPARE_BLOW y EX_BLOW (el fusible).
//
// Lo que añade P-20 (las secuencias de [JTAG] §2.4 y del Replicator430):
//
//   * DATA_QUICK [JTAG §2.4.3.2]: con HALT_JTAG, cada subida de TCLK hace el
//     acceso (lectura o escritura según R/W) en la dirección rápida, y cada
//     bajada la incrementa en 2. La dirección empieza en el MAB de la CPU más
//     4: con HaltCPU, el MAB es el PC, Start−4 tras SetPC(Start−4) [JTAG
//     §2.4.3.2; de modelo: el desfase, el de WriteMemQuick/ReadMemQuick del
//     Replicator].
//   * DATA_PSA y SHIFT_OUT_PSA [JTAG §2.4.5]: al entrar, el registro PSA y su
//     dirección valen el MDB de JTAG si la CPU está parada (EnhVerify) o la
//     de la última búsqueda si no (SetPC(Start−2)); cada subida de TCLK avanza la dirección en 2, lee la palabra
//     y la acumula con el polinomio 0x0805; SHIFT_OUT_PSA lo da. Con PSA, la
//     CPU no ve los flancos.
//   * El fusible [JTAG §2.3.6]: PREPARE_BLOW y luego EX_BLOW con VPP (más de
//     6 V) en TEST: por la actualización de la IR (4 hilos) o por una subida
//     de TCLK con EX_BLOW cargada (SBW, IR_Ex_Blow_SBW_Shift). Fundido, la IR
//     sigue dando el JTAG ID, pero todos los registros de datos son de 1 bit
//     (DR_Shift16(0xAAAA) da 0x5555: IsFuseBlown).
//
// Lo que añade M4, la CPUXv2 (JTAG ID 0x91; las secuencias de [JTAG] §2.3 y
// del Replicator430Xv2):
//
//   * el registro de control de Xv2 [JTAG tabla 2-7]: READ (0), HALT_JTAG
//     (3), BYTE (4), INSTR_LOAD (7), CPUSUSP (8), TCE0 (9), TCE1 (10), POR
//     (11), RELEASE_LBYTE (12-13). Es el de 1xx con CPUSUSP donde estaba
//     HALT_JTAG: con él la CPU queda suspendida en la búsqueda de la
//     instrucción y el bus es de JTAG (DATA_TO_ADDR, DATA_QUICK, la PSA);
//   * TCLK: en Xv2, la CPU avanza en las SUBIDAS (SetPC_430Xv2 mete la MOVA
//     en IR_DATA_16BIT con TCLK abajo y la CPU la toma al subir); los
//     accesos de JTAG con CPUSUSP siguen en las bajadas, como en 1xx;
//   * DATA_QUICK empieza en el MAB (el PC de la búsqueda suspendida), sin
//     el desfase de 1xx (ReadMemQuick_430Xv2 hace SetPC(Start));
//   * JMB_EXCHANGE, el buzón de JTAG [JTAG §2.3.?; UG5 §1.14]: la primera
//     palabra es el control (INREQ, OUTREQ, JMB32B) y su captura da el
//     estado (IN0RDY, IN1RDY, OUT0RDY, OUT1RDY); detrás, una o dos palabras
//     de entrada o de salida. El buzón es del SoC (`jmb_*`);
//   * COREIP_ID (16 bits) y DEVICE_ID (20 bits: el puntero a la TLV);
//   * EMEX_DATA_EXCHANGE32 (0x0D): el EEM con valores de 32 bits (los de los
//     disparadores son de 20), y EMEX_READ_CONTROL/EMEX_WRITE_CONTROL, el
//     GENCTRL directo [MSPDS EemDataExchangeXv2.c];
//   * TEST_REG con 0x0200 hace un BOR (ReleaseDevice_430Xv2(V_BOR)) y
//     TEST_3V_REG se acepta;
//   * la protección por firma de 5xx [UG5 §1.11]: la pone el SoC al
//     arrancar (`pon_bloqueo`) y se ve como el fusible: registros de 1 bit.
//
// El JSTATE es de FRAM (M6). Las instrucciones se comprueban contra las de
// JTAG_defs.h con sus bits invertidos (test430 N7).
// =============================================================================
#ifndef STM32_M430_DEBUG_TAP_M430_H
#define STM32_M430_DEBUG_TAP_M430_H

#include <cstdint>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <systemc>
#include "eem.h"

namespace stm32 {

// Los códigos de [JTAG] (los de JTAG_defs.h, con los bits al revés)
enum : uint8_t {
    IR430_ADDR_16BIT = 0x83, IR430_ADDR_CAPTURE = 0x84, IR430_DATA_TO_ADDR = 0x85,
    IR430_CAPTURE_CPU_REG = 0x86, IR430_DEVICE_ID = 0x87,
    IR430_DATA_16BIT = 0x41, IR430_DATA_CAPTURE = 0x42, IR430_DATA_QUICK = 0x43, IR430_DATA_PSA = 0x44,
    IR430_SHIFT_OUT_PSA = 0x46,
    IR430_EMEX_DATA_EXCHANGE = 0x09, IR430_EMEX_READ_TRIGGER = 0x0A, IR430_EMEX_READ_CONTROL = 0x0B,
    IR430_EMEX_WRITE_CONTROL = 0x0C, IR430_EMEX_DATA_EXCHANGE32 = 0x0D,
    IR430_CNTRL_SIG_HIGH_BYTE = 0x11, IR430_CNTRL_SIG_LOW_BYTE = 0x12, IR430_CNTRL_SIG_16BIT = 0x13, IR430_CNTRL_SIG_CAPTURE = 0x14, IR430_CNTRL_SIG_RELEASE = 0x15,
    IR430_COREIP_ID = 0x17, IR430_PREPARE_BLOW = 0x22, IR430_EX_BLOW = 0x24,
    IR430_TEST_REG = 0x2A, IR430_TEST_3V_REG = 0x2F,
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
    // La dirección de la última búsqueda de instrucción (el MAB del ciclo de
    // búsqueda, que es donde arrancan DATA_QUICK y PSA)
    virtual uint32_t jtag_ultima_busqueda() const { return jtag_mab(); }
    // Un acceso con el bus en manos de JTAG (HALT_JTAG)
    virtual uint16_t jtag_lee(uint32_t a, bool byte) = 0;
    virtual void jtag_escribe(uint32_t a, uint16_t v, bool byte) = 0;
};

class TapM430 {
public:
    enum Estado : uint8_t { TLR, RTI, SEL_DR, CAP_DR, SH_DR, EX1_DR, PAU_DR, EX2_DR, UPD_DR,
                            SEL_IR, CAP_IR, SH_IR, EX1_IR, PAU_IR, EX2_IR, UPD_IR };

    TapM430(uint8_t jtag_id, bool cpux, EemM430* eem) : id_(jtag_id), cpux_(cpux), xv2_(jtag_id == 0x91), eem_(eem) { reset(); }
    bool xv2() const { return xv2_; }
    // Lo que el TAP de Xv2 pide al SoC: el buzón (JMB) y el BOR
    std::function<uint16_t()> jmb_estado;                  // IN0RDY 1, IN1RDY 2, OUT0RDY 4, OUT1RDY 8
    std::function<void(unsigned, uint16_t)> jmb_entra;     // JMBIn0/1 desde JTAG
    std::function<uint16_t(unsigned)> jmb_sale;            // JMBOut0/1 hacia JTAG (las vacía)
    std::function<void()> al_bor;
    uint16_t coreip_id = 0;
    uint32_t device_id_ptr = 0;
    // La firma de 5xx: bloqueado, como el fusible fundido
    void pon_bloqueo(bool b) { bloqueo_ = b; }
    void pon_cpu(m430_jtag_cpu_if* c) { cpu_ = c; }
    uint8_t jtag_id() const { return id_; }

    void reset() {
        st_ = TLR;
        ir_ = IR430_BYPASS;
        cntrl_ = CS430_TAGFUNCSAT;
        sr_ = 0; n_ = 0; tclk_ = true; emex_fase_ = 0; jmb_fase_ = 0;
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
    bool fusible() const { return fusible_ || bloqueo_; }
    void restaura_fusible() { fusible_ = false; }
    uint64_t ciclos_tck() const { return n_tck_; }
    // La tensión de TEST (para VPP), la pone quien tenga el pin
    std::function<double()> tension_test;
    uint16_t psa() const { return psa_; }

    // TDI en Run-Test/Idle: TCLK
    void tdi_en_rti(bool nivel) {
        if (st_ != RTI || nivel == tclk_) return;
        tclk_ = nivel;
        if (!cpu_) return;
        // El fusible por SBW: la subida de TCLK con EX_BLOW cargada y VPP
        if (nivel && ir_ == IR430_EX_BLOW && armado_ && hay_vpp()) { fusible_ = true; armado_ = false; }
        // DATA_QUICK: la subida hace el acceso; la bajada incrementa
        if (ir_ == IR430_DATA_QUICK && (cntrl_ & bit_halt()) && (cntrl_ & CS430_TCE1)) {
            if (nivel) {
                const bool byte = (cntrl_ & CS430_BYTE) != 0;
                if (cntrl_ & CS430_RW) mdb_ = cpu_->jtag_lee(quick_, byte);
                else { cpu_->jtag_escribe(quick_, mdb_jtag_, byte); mdb_ = mdb_jtag_; }
            } else quick_ = (quick_ + 2) & (cpux_ ? 0xFFFFFu : 0xFFFFu);
            cpu_->jtag_tclk(nivel);
            return;
        }
        // PSA: cada subida, una palabra más
        if (ir_ == IR430_DATA_PSA) {
            if (nivel && (cntrl_ & CS430_TCE1)) {
                psa_dir_ = (psa_dir_ + 2) & (cpux_ ? 0xFFFFFu : 0xFFFFu);
                const uint16_t d = cpu_->jtag_lee(psa_dir_, false);
                psa_ = uint16_t((psa_ & 0x8000u) ? (((psa_ ^ 0x0805u) << 1) | 1u) : (psa_ << 1));
                psa_ ^= d;
            }
            return;
        }
        // Con HALT_JTAG, la BAJADA hace el acceso de JTAG en el MAB de JTAG: el
        // WriteFLASH de SBW del Replicator llega a SetTCLK con TCLK ya arriba
        // (lo deja así TCLKstrobes) y la escritura solo puede ser al bajar
        // [JTAG §2.4.2: «SetTCLK; ClrTCLK» tras DATA_TO_ADDR]
        if (!nivel && (cntrl_ & bit_halt()) && (cntrl_ & CS430_TCE1) && ir_ == IR430_DATA_TO_ADDR) {
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
        else if (st_ == CAP_IR) { n_ = 8; sr_ = invierte(id_, 8); }
        else if (st_ == UPD_DR) actualiza_dr();
        else if (st_ == UPD_IR) actualiza_ir();
        else if (st_ == TLR && ant != TLR) { ir_ = IR430_BYPASS; emex_fase_ = 0; }
        // Al volver a Run-Test/Idle, TDI vuelve a ser TCLK (las secuencias lo
        // dejan como estaba; IR_Ex_Blow_SBW_Shift lo deja abajo a propósito)
        if (st_ == RTI && ant != RTI) tdi_en_rti(tdi);
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
    bool cpux_, xv2_;
    bool bloqueo_ = false;
    unsigned jmb_fase_ = 0, jmb_n_ = 0;       // 0: el control; 1-2: entrada; 3-4: salida
    // CPUSUSP en Xv2, HALT_JTAG en 1xx
    uint16_t bit_halt() const { return xv2_ ? 0x0100 : CS430_HALT_JTAG; }
    EemM430* eem_;
    m430_jtag_cpu_if* cpu_ = nullptr;
    Estado st_ = TLR;
    uint8_t ir_ = IR430_BYPASS;
    uint16_t cntrl_ = CS430_TAGFUNCSAT, mdb_jtag_ = 0, mdb_ = 0;
    uint32_t mab_jtag_ = 0;
    uint64_t sr_ = 0, n_tck_ = 0;
    unsigned n_ = 0, emex_fase_ = 0;
    uint8_t emex_dir_ = 0;
    bool tclk_ = true, fusible_ = false, preparado_ = false, armado_ = false;
    uint32_t quick_ = 0, psa_dir_ = 0;
    uint16_t psa_ = 0;
    bool hay_vpp() const { return tension_test && tension_test() > 6.0; }

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
        case IR430_DEVICE_ID: return xv2_ ? 20 : 16;
        case IR430_EMEX_DATA_EXCHANGE32: return 32;
        default: return 16;
        }
    }
    void captura_dr() {
        n_ = largo_dr();
        uint32_t v = 0;
        if (fusible()) { n_ = 1; sr_ = 0; return; }  // fundido o con firma: todo es BYPASS
        switch (ir_) {
        case IR430_CNTRL_SIG_16BIT: case IR430_CNTRL_SIG_CAPTURE: v = estado_cntrl(); break;
        case IR430_CNTRL_SIG_HIGH_BYTE: v = estado_cntrl() >> 8; break;
        case IR430_CNTRL_SIG_LOW_BYTE: v = estado_cntrl() & 0xFFu; break;
        case IR430_ADDR_16BIT: case IR430_ADDR_CAPTURE: v = cpu_ ? cpu_->jtag_mab() : 0; break;
        case IR430_DATA_TO_ADDR: case IR430_DATA_16BIT: case IR430_DATA_CAPTURE: case IR430_DATA_QUICK:
            v = mdb_; break;
        case IR430_SHIFT_OUT_PSA: v = psa_; break;
        case IR430_DATA_PSA: v = mdb_; break;
        case IR430_EMEX_DATA_EXCHANGE: case IR430_EMEX_DATA_EXCHANGE32:
            v = (emex_fase_ == 1 && (emex_dir_ & 1u) && eem_) ? eem_->lee(uint8_t(emex_dir_ & ~1u)) : 0; break;
        case IR430_EMEX_READ_CONTROL: case IR430_EMEX_WRITE_CONTROL: v = eem_ ? eem_->lee(0x82) : 0; break;
        case IR430_COREIP_ID: v = xv2_ ? coreip_id : 0; break;
        case IR430_DEVICE_ID: v = xv2_ ? device_id_ptr : 0; break;
        case IR430_JMB_EXCHANGE:
            if (!xv2_) v = 0;
            else if (jmb_fase_ >= 3) v = jmb_sale ? jmb_sale(jmb_fase_ - 3) : 0;
            else v = jmb_estado ? jmb_estado() : 0;
            break;
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
        if (fusible()) { ir_ = IR430_BYPASS; return; }
        if (cpu_ && ir_ == IR430_DATA_QUICK) quick_ = (cpu_->jtag_mab() + (xv2_ ? 0 : 4)) & (cpux_ ? 0xFFFFFu : 0xFFFFu);
        if (ir_ == IR430_JMB_EXCHANGE) jmb_fase_ = 0;
        if (cpu_ && ir_ == IR430_DATA_PSA) {
            psa_dir_ = (cntrl_ & bit_halt()) ? mdb_jtag_ : cpu_->jtag_ultima_busqueda();
            psa_ = uint16_t(psa_dir_);
        }
        if (ir_ == IR430_EMEX_DATA_EXCHANGE || ir_ == IR430_EMEX_DATA_EXCHANGE32) emex_fase_ = 0;
        if (ir_ == IR430_CNTRL_SIG_RELEASE || ir_ == IR430_BYPASS) {
            // RELEASE (y BYPASS, que lo hace a la vez [JTAG §2.2.4.2.4]): la CPU, libre
            if (cpu_ && (cntrl_ & CS430_TCE1)) { cpu_->jtag_halt(false); cpu_->jtag_control(false); }
            cntrl_ = uint16_t(cntrl_ & ~(CS430_TCE1 | bit_halt()));
        }
        if (ir_ == IR430_PREPARE_BLOW) { preparado_ = true; armado_ = false; }
        else if (ir_ == IR430_EX_BLOW) {
            if (preparado_ && hay_vpp()) fusible_ = true;           // 4 hilos: VPP ya puesta
            else armado_ = preparado_;                              // SBW: la VPP y TCLK vienen después
            preparado_ = false;
        } else { preparado_ = false; armado_ = false; }
    }
    // La IR entra con el LSB primero [JTAG §2.2.2]: por eso las herramientas
    // (JTAG_defs.h, el Replicator430) desplazan los códigos al revés con el
    // MSB primero (IR_CNTRL_SIG_16BIT = 0xC8 por 0x13). El registro, tal cual
    uint32_t valor_ir() const { return uint32_t(sr_ & 0xFFu); }
    void actualiza_dr() {
        if (fusible()) return;
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
        // El de 32 bits (Xv2: los valores de 20 bits de los disparadores) [EemDataExchangeXv2.c]
        case IR430_EMEX_DATA_EXCHANGE32:
            if (emex_fase_ == 0) { emex_dir_ = uint8_t(v); emex_fase_ = 1; }
            else { if (!(emex_dir_ & 1u) && eem_) eem_->escribe(emex_dir_, v); emex_fase_ = 0; }
            break;
        case IR430_EMEX_WRITE_CONTROL: if (eem_) eem_->escribe(0x82, uint16_t(v)); break;
        case IR430_JMB_EXCHANGE: if (xv2_) jmb(uint16_t(v)); break;
        case IR430_TEST_REG:
            // 0x0200: el BOR [Replicator430Xv2, ReleaseDevice_430Xv2(V_BOR)]; suelta la CPU
            if (xv2_ && (v & 0x0200)) {
                if (cpu_ && (cntrl_ & CS430_TCE1)) { cpu_->jtag_halt(false); cpu_->jtag_control(false); }
                cntrl_ = CS430_TAGFUNCSAT;
                if (al_bor) al_bor();
            }
            break;
        default: break;
        }
    }
    // El buzón: el control y luego sus palabras [Replicator430Xv2: i_ReadJmbOut,
    // i_WriteJmbIn16/32]
    void jmb(uint16_t v) {
        enum : uint16_t { INREQ = 0x0001, OUTREQ = 0x0004, B32 = 0x0010 };
        if (jmb_fase_ == 0) {
            const unsigned n = (v & B32) ? 2u : 1u;
            if (v & INREQ) { jmb_fase_ = 1; jmb_n_ = n; }
            else if (v & OUTREQ) { jmb_fase_ = 3; jmb_n_ = n; }
            return;
        }
        if (jmb_fase_ <= 2 && jmb_entra) jmb_entra(jmb_fase_ - 1, v);
        const unsigned k = jmb_fase_ <= 2 ? jmb_fase_ : jmb_fase_ - 2;   // la palabra que va
        jmb_fase_ = k >= jmb_n_ ? 0 : jmb_fase_ + 1;
    }
    void pon_cntrl(uint16_t v) {
        const uint16_t ant = cntrl_;
        cntrl_ = uint16_t(v & ~(CS430_INSTR_LOAD | CS430_TCE));
        if (!cpu_) return;
        if ((cntrl_ ^ ant) & CS430_TCE1) cpu_->jtag_control((cntrl_ & CS430_TCE1) != 0);
        if ((cntrl_ ^ ant) & bit_halt()) cpu_->jtag_halt((cntrl_ & bit_halt()) != 0);
        if ((cntrl_ ^ ant) & CS430_POR) cpu_->jtag_por((cntrl_ & CS430_POR) != 0);
    }
};

} // namespace stm32
#endif // STM32_M430_DEBUG_TAP_M430_H
