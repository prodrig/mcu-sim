// =============================================================================
// sonda_m430.h — La sonda de bancada de MSP430 (M2 de doc/analisis_msp430.md, §7.3)
//
// Port a C++ de las macros y funciones HIL de la MSP Debug Stack 3.15.1.1,
// para la familia 1xx/2xx/4xx (CPU de 16 bits):
//
//   Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the conditions of the BSD
//   3-Clause license are met (MSPDebugStack_OS_Package, Bios/src/hil/edt.h,
//   Bios/src/hal/macros/*.c, Bios/src/hil/hil_2w.c y hil_4w.c).
//
// Lo de arriba son las secuencias de edt.h tal cual (halt_cpu, release_cpu,
// ReadMemWord, WriteMemWord, WriteMemByte, ReadCpuReg, WriteCpuReg, SetPc,
// instrLoad) y las de las macros que usa GDB (GetJtagId, una
// SyncJtag_AssertPor_SaveContext sin la Flash, ReadMemWords, WriteMemWords,
// EemDataExchange, RestoreContext_ReleaseJtag sin el EEM de estado). Lo de
// abajo, el HIL de 4 hilos y el de SBW sobre los `AnalogNet` de los pines,
// con las franjas de hil_2w.c.
//
// Cada transacción (IR, DR, TCLK) se apunta en `traza` si se pide: es lo que
// se compara con el Replicator430 (verif/m430/replicator).
// =============================================================================
#ifndef STM32_M430_SONDA_SONDA_M430_H
#define STM32_M430_SONDA_SONDA_M430_H

#include <systemc>
#include <cstdint>
#include <string>
#include <vector>
#include "../../common/analog_net.h"
#include "../debug/tap_m430.h"

namespace stm32 {

class SondaM430 {
public:
    enum class Modo { Jtag4, Sbw };
    struct Pines {
        analog_net_if* tck = nullptr; analog_net_if* tms = nullptr; analog_net_if* tdi = nullptr;
        analog_net_if* tdo = nullptr; analog_net_if* test = nullptr; analog_net_if* rst = nullptr;
    };
    // La traza de transacciones: "I13>89", "D16 2401>2401", "T0", "T1"
    std::vector<std::string>* traza = nullptr;
    sc_core::sc_time medio_periodo{250, sc_core::SC_NS};      // TCK a 2 MHz

    SondaM430(const Pines& p, Modo m, double vdd = 3.3) : p_(p), modo_(m), vdd_(vdd) {
        if (m == Modo::Jtag4) {
            id_tck_ = p_.tck->register_driver("sonda430_tck");
            id_tms_ = p_.tms->register_driver("sonda430_tms");
            id_tdi_ = p_.tdi->register_driver("sonda430_tdi");
        }
        if (p_.test) id_test_ = p_.test->register_driver("sonda430_test");
        if (p_.rst) id_rst_ = p_.rst->register_driver("sonda430_rst");
        suelta_pines();
    }
    Modo modo() const { return modo_; }
    uint64_t flancos() const { return n_flancos_; }

    // ==== Lo de arriba ======================================================
    // StartJtag + ResetTAP + GetJtagId (StartJtag.c, GetJtagId.c; las entradas
    // de StartJtag.c y JTAGfunc430.c: RST y TEST arriba, o el pulso de SBW)
    uint8_t conecta() {
        if (modo_ == Modo::Sbw) {
            pon(p_.test, id_test_, false); espera_us(4000);
            pon(p_.rst, id_rst_, true);
            pon(p_.test, id_test_, true); espera_us(20000);
            pon(p_.test, id_test_, false); espera_us(1);
            pon(p_.test, id_test_, true); espera_us(60);
            tclk_ = true;
        } else {
            if (p_.rst) pon(p_.rst, id_rst_, true);
            if (p_.test) pon(p_.test, id_test_, true);
            pon(p_.tck, id_tck_, true); pon(p_.tms, id_tms_, true); pon(p_.tdi, id_tdi_, true);
            espera_us(10);
            tclk_ = true;
        }
        reset_tap();
        return uint8_t(ir_shift(IR430_BYPASS));
    }
    // Suelta la CPU (IR_CNTRL_SIG_RELEASE) y los pines
    void desconecta() {
        ir_shift(IR430_CNTRL_SIG_RELEASE);
        if (modo_ == Modo::Sbw) { pon(p_.test, id_test_, false); espera_us(20); }
        suelta_pines();
    }

    // ResetTAP de JTAGfunc430.c / hil: 6 TCK con TMS a 1 y Run-Test/Idle
    void reset_tap() {
        for (int i = 0; i < 6; ++i) tck_ciclo(true, true);
        tck_ciclo(false, true);
    }

    // --- Las funciones de edt.h (2xx) ---------------------------------------
    uint16_t cntrl_capture() { ir_shift(IR430_CNTRL_SIG_CAPTURE); return uint16_t(dr_shift(16, 0)); }
    void halt_cpu() {
        ir_shift(IR430_DATA_16BIT); dr_shift(16, 0x3FFF);
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2409);
        tclk(true);
    }
    void release_cpu() {
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2401);
        ir_shift(IR430_ADDR_CAPTURE);
        tclk(true);
    }
    bool instr_load() {
        ir_shift(IR430_CNTRL_SIG_LOW_BYTE); dr_shift(8, CS430_RW);
        tclk(true);
        for (int i = 0; i < 10; ++i) {
            ir_shift(IR430_CNTRL_SIG_CAPTURE);
            if ((dr_shift(16, 0) & (CS430_INSTR_LOAD | CS430_RW)) == (CS430_INSTR_LOAD | CS430_RW)) return true;
            tclk(false); tclk(true);
        }
        return false;
    }
    uint16_t lee_palabra(uint16_t a) {
        halt_cpu();
        tclk(false);
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, a);
        ir_shift(IR430_DATA_TO_ADDR);
        tclk(true); tclk(false);
        const uint16_t v = uint16_t(dr_shift(16, 0));
        release_cpu();
        instr_load();                       // como ReadMemWords.c: la CPU, otra vez en una búsqueda
        return v;
    }
    void escribe_palabra(uint16_t a, uint16_t v) {
        halt_cpu();
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_LOW_BYTE); dr_shift(8, 0x08);
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, a);
        ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, v);
        tclk(true);
        release_cpu();
        instr_load();
    }
    void escribe_byte(uint16_t a, uint8_t v) {
        halt_cpu();
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_LOW_BYTE); dr_shift(8, 0x18);   // escritura de byte (en WriteMemBytes.c)
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, a);
        ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, v);
        tclk(true); tclk(false);
        release_cpu();
        instr_load();
    }
    uint16_t lee_reg(unsigned r) {
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x3401);
        ir_shift(IR430_DATA_16BIT); dr_shift(16, ((r << 8) & 0x0F00u) | 0x4082u);   // MOV Rn,&0x00FE
        tclk(false);
        ir_shift(IR430_DATA_CAPTURE); tclk(true);
        ir_shift(IR430_DATA_16BIT); dr_shift(16, 0x00FE);
        tclk(false);
        ir_shift(IR430_DATA_CAPTURE); tclk(true);
        tclk(false); tclk(true);
        const uint16_t v = uint16_t(dr_shift(16, 0));
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2401);
        tclk(true);
        return v;
    }
    void escribe_reg(unsigned r, uint16_t v) {
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x3401);
        ir_shift(IR430_DATA_16BIT); dr_shift(16, 0x4030u | r);                     // MOV #v,Rn
        tclk(false);
        ir_shift(IR430_DATA_CAPTURE); tclk(true);
        ir_shift(IR430_DATA_16BIT); dr_shift(16, v);
        tclk(false);
        ir_shift(IR430_DATA_CAPTURE); tclk(true);
        ir_shift(IR430_DATA_16BIT); dr_shift(16, 0x3FFD);                          // JMP $-4: el PC, como estaba
        tclk(false);
        ir_shift(IR430_DATA_CAPTURE); tclk(true);
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2401);
        tclk(true);
    }
    void pon_pc(uint16_t pc) {
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x3401);
        ir_shift(IR430_DATA_16BIT); dr_shift(16, 0x4030);                          // MOV #pc,PC
        tclk(false); tclk(true);
        dr_shift(16, pc);
        tclk(false); tclk(true);
        ir_shift(IR430_ADDR_CAPTURE);
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2401);
        tclk(true);
    }
    uint16_t lee_eem(uint8_t dir) {
        ir_shift(IR430_EMEX_DATA_EXCHANGE); dr_shift(16, uint32_t(dir) | 1u);
        return uint16_t(dr_shift(16, 0));
    }
    void escribe_eem(uint8_t dir, uint16_t v) {
        ir_shift(IR430_EMEX_DATA_EXCHANGE); dr_shift(16, dir); dr_shift(16, v);
    }

    // --- Lo que usa GDB --------------------------------------------------------
    // SyncJtag_Conditional/AssertPor_SaveContext (2xx), lo esencial: la CPU bajo
    // JTAG (TCE1), sincronizada y en el límite de una instrucción; con `por`,
    // además, un POR por el registro de control (y la CPU parada en su vector).
    // Devuelve false si no se sincroniza.
    bool sincroniza(bool por) {
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2401);
        bool ok = false;
        for (int i = 0; i < 50 && !ok; ++i) {
            ir_shift(IR430_CNTRL_SIG_CAPTURE);
            ok = (dr_shift(16, 0) & CS430_TCE) != 0;
            if (!ok) espera_us(10);
        }
        if (!ok) return false;
        if (por) {
            ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2C01);                 // POR
            dr_shift(16, 0x2401);
            tclk(false); tclk(true); tclk(false); tclk(true); tclk(false);
            ir_shift(IR430_ADDR_CAPTURE);
            tclk(true);
        }
        return instr_load();
    }
    // El PC: en el límite de una instrucción, el MAB [ADDR_CAPTURE]
    uint16_t lee_pc() { ir_shift(IR430_ADDR_CAPTURE); return uint16_t(dr_shift(16, 0)); }
    // Una instrucción bajo TCLK (SingleStep.c sin el EEM): hasta la búsqueda siguiente
    bool un_paso() {
        tclk(false); tclk(true);
        return instr_load();
    }
    // RestoreContext_ReleaseJtag: la CPU, libre
    void suelta_cpu() {
        ir_shift(IR430_CNTRL_SIG_RELEASE);
        ir_shift(IR430_ADDR_CAPTURE);
    }

    // --- La Flash de 1xx/2xx/4xx por JTAG (WriteFLASH y EraseFLASH de
    // JTAGfunc430.c del Replicator430 1.22, BSD-3 [JTAG §2.4.7 y §2.4.8]) -----
    // El generador de tiempos con MCLK (que bajo JTAG es TCLK) y FN = 0: cada
    // palabra, 35 flancos de TCLK; el borrado, 4820 (segmento) o 10600
    // (masivo, Flash rápida). `fctl3` es 0xA500, o 0xA540 para cambiar LOCKA.
    static constexpr uint16_t BORRA_SEGMENTO = 0xA502, BORRA_PRINCIPAL = 0xA504, BORRA_MASIVO = 0xA506;
    void escribe_flash(uint16_t dir, const uint16_t* w, unsigned n, uint16_t fctl3 = 0xA500) {
        halt_cpu();
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2408);
        escribe_reg_flash(0x0128, 0xA540);                    // FCTL1: WRT
        escribe_reg_flash(0x012A, 0xA540);                    // FCTL2: MCLK, /1
        escribe_reg_flash(0x012C, fctl3);                     // FCTL3: sin LOCK
        ir_shift(IR430_CNTRL_SIG_16BIT);
        for (unsigned i = 0; i < n; ++i) {
            dr_shift(16, 0x2408);
            ir_shift(IR430_ADDR_16BIT); dr_shift(16, uint16_t(dir + 2 * i));
            ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, w[i]);
            tclk(true); tclk(false);
            ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2409);
            tclk_strobes(35);
        }
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2408);
        escribe_reg_flash(0x0128, 0xA500);
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, 0x012C);
        ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, uint16_t(fctl3 | 0x0010));   // LOCK otra vez
        tclk(true);
        release_cpu();
        instr_load();
    }
    void borra_flash(uint16_t modo, uint16_t dir, uint16_t fctl3 = 0xA500) {
        const unsigned n = modo == BORRA_SEGMENTO ? 4820 : 10600;
        halt_cpu();
        tclk(false);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2408);
        escribe_reg_flash(0x0128, modo);
        escribe_reg_flash(0x012A, 0xA540);
        escribe_reg_flash(0x012C, fctl3);
        escribe_reg_flash(dir, 0x55AA);                       // la escritura que borra
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2409);
        tclk_strobes(n);
        ir_shift(IR430_CNTRL_SIG_16BIT); dr_shift(16, 0x2408);
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, 0x0128);
        ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, 0xA500);
        tclk(true);
        tclk(false);
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, 0x012C);
        ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, uint16_t(fctl3 | 0x0010));
        tclk(true);
        release_cpu();
        instr_load();
    }
    // TCLKstrobes: n bajadas de TCLK sin pasar por la máquina de JTAG. Por
    // SBW, dentro de la franja de TDI, con SBWTDIO y SBWTCK arriba (como
    // LowLevelFunc430.c); por 4 hilos, en TDI. Acaba con TCLK arriba.
    void tclk_strobes(unsigned n) {
        if (traza) traza->push_back(hex("S", 0, n));
        const sc_core::sc_time alto(1500, sc_core::SC_NS), bajo(1000, sc_core::SC_NS);   // ~400 kHz
        if (modo_ == Modo::Jtag4) {
            for (unsigned i = 0; i < n; ++i) { pon(p_.tdi, id_tdi_, false); sc_core::wait(bajo); pon(p_.tdi, id_tdi_, true); sc_core::wait(alto); }
            tclk_ = true;
            return;
        }
        franja(false, tclk_);                           // la de TMS (TMSLDH si TCLK estaba arriba)
        for (unsigned i = 0; i < n; ++i) { pon(p_.rst, id_rst_, false); sc_core::wait(bajo); pon(p_.rst, id_rst_, true); sc_core::wait(alto); }
        franja(true, false);                            // TDIH
        franja_tdo();
        tclk_ = true;
    }

    // --- El fusible (BlowFuse de JTAGfunc430.c del Replicator430, BSD-3) -------
    // VPP en TEST: por 4 hilos, PREPARE_BLOW, VPP y EX_BLOW; por SBW, la IR
    // con EX_BLOW acabando con SBWTDIO abajo, la franja de TMS, VPP en TEST y
    // SBWTDIO arriba (IR_Ex_Blow_SBW_Shift). Pide el pin TEST.
    void funde_fusible(double vpp = 6.5) {
        ir_shift(IR430_PREPARE_BLOW);
        espera_us(1000);
        if (modo_ == Modo::Jtag4) {
            if (p_.test) p_.test->set_drive(id_test_, float(vpp), 30.0f);
            espera_us(5000);
            ir_shift(IR430_EX_BLOW);
            espera_us(1000);
            if (p_.test) pon(p_.test, id_test_, true);
            return;
        }
        if (traza) traza->push_back("I24>blow");
        tck_ciclo(true, tclk_); tck_ciclo(true, true); tck_ciclo(false, true); tck_ciclo(false, true);
        uint32_t d = 0x24;                              // IR_EX_BLOW de JTAGfunc430.h (0x24 al revés es 0x24)
        for (unsigned i = 8; i > 0; --i) { tck_ciclo(i == 1, (d >> 7) & 1u); d <<= 1; }
        tck_ciclo(true, false);                         // Update-IR, SBWTDIO abajo
        tck_ciclo(false, false);                        // Run-Test/Idle
        tclk_ = false;
        franja(false, false);                           // la franja de TMS: ahora, la de TDI
        espera_us(1000);
        p_.test->set_drive(id_test_, float(vpp), 30.0f);
        espera_us(3000);
        pon(p_.rst, id_rst_, true);                     // SBWTDIO arriba: se funde
        espera_us(1000);
        pon(p_.test, id_test_, true);
        franja(true, false);                            // se acaba la franja de TDI (TDIH)
        franja_tdo();
        tclk_ = true;
    }
    // IsFuseBlown: DR_Shift16(0xAAAA) en CNTRL_SIG_CAPTURE da 0x5555 si está fundido
    bool fusible_fundido() {
        for (int i = 0; i < 3; ++i) {
            ir_shift(IR430_CNTRL_SIG_CAPTURE);
            if (dr_shift(16, 0xAAAA) == 0x5555) return true;
        }
        return false;
    }

    // ==== El HIL ============================================================
    uint32_t ir_shift(uint8_t ir) {
        if (traza) traza->push_back(hex("I", 2, ir));
        // RTI -> Select-DR -> Select-IR -> Capture-IR -> Shift-IR
        tck_ciclo(true, tclk_); tck_ciclo(true, tclk_); tck_ciclo(false, tclk_); tck_ciclo(false, tclk_);
        // El código, con el LSB primero (como JTAG_defs.h: los bits al revés)
        uint32_t rev = 0;
        for (unsigned i = 0; i < 8; ++i) if ((ir >> i) & 1u) rev |= 0x80u >> i;
        const uint32_t r = desplaza(8, rev);
        if (traza) traza->back() += hex(">", 2, r);
        return r;
    }
    uint32_t dr_shift(unsigned n, uint32_t v) {
        if (traza) traza->push_back(hex("D", 0, n) + hex(" ", (n + 3) / 4, v));
        // RTI -> Select-DR -> Capture-DR -> Shift-DR
        tck_ciclo(true, tclk_); tck_ciclo(false, tclk_); tck_ciclo(false, tclk_);
        const uint32_t r = desplaza(n, v);
        if (traza) traza->back() += hex(">", (n + 3) / 4, r);
        return r;
    }
    void tclk(bool nivel) {
        if (traza) traza->push_back(nivel ? "T1" : "T0");
        if (modo_ == Modo::Jtag4) { pon(p_.tdi, id_tdi_, nivel); espera(); tclk_ = nivel; return; }
        // SBW: ClrTCLK_sbw / SetTCLK_sbw de LowLevelFunc430.c (la franja de TMS a 0;
        // la de TDI lleva el nuevo TCLK; la de TDO, sin leer)
        franja(false, tclk_ ? true : false);
        franja(nivel, false, true);
        franja_tdo();
        tclk_ = nivel;
    }

private:
    Pines p_;
    Modo modo_;
    double vdd_;
    int id_tck_ = -1, id_tms_ = -1, id_tdi_ = -1, id_test_ = -1, id_rst_ = -1;
    bool tclk_ = true;
    uint64_t n_flancos_ = 0;

    static std::string hex(const char* pre, unsigned dig, uint32_t v) {
        char b[24];
        if (!dig) std::snprintf(b, sizeof b, "%s%u", pre, unsigned(v));
        else std::snprintf(b, sizeof b, "%s%0*X", pre, int(dig), unsigned(v));
        return b;
    }
    void escribe_reg_flash(uint16_t a, uint16_t v) {
        ir_shift(IR430_ADDR_16BIT); dr_shift(16, a);
        ir_shift(IR430_DATA_TO_ADDR); dr_shift(16, v);
        tclk(true); tclk(false);
    }
    void pon(analog_net_if* n, int id, bool v) { if (n) n->set_drive(id, v ? float(vdd_) : 0.0f, 30.0f); }
    bool lee(analog_net_if* n) const { return n && !n->floating() && n->voltage() > 0.5f * float(vdd_); }
    void espera() { sc_core::wait(medio_periodo); }
    void espera_us(double us) { sc_core::wait(sc_core::sc_time(us, sc_core::SC_US)); }
    void suelta_pines() {
        if (modo_ == Modo::Jtag4) { p_.tck->set_hiz(id_tck_); p_.tms->set_hiz(id_tms_); p_.tdi->set_hiz(id_tdi_); }
        if (p_.test) p_.test->set_hiz(id_test_);
        if (p_.rst) p_.rst->set_hiz(id_rst_);
    }

    // Un ciclo de TCK; devuelve TDO (leído tras la subida)
    bool tck_ciclo(bool tms, bool tdi) {
        ++n_flancos_;
        if (modo_ == Modo::Jtag4) {
            pon(p_.tms, id_tms_, tms); pon(p_.tdi, id_tdi_, tdi);
            pon(p_.tck, id_tck_, false); espera();
            pon(p_.tck, id_tck_, true); espera();
            return lee(p_.tdo);
        }
        franja(tms, false);
        franja(tdi, false);
        return franja_tdo();
    }
    // Una franja de SBW: el dato, SBWTCK abajo, arriba (con `dh`, el dato sube
    // en la fase baja: el TMSLDH de LowLevelFunc430.h)
    void franja(bool d, bool dh, bool = false) {
        pon(p_.rst, id_rst_, d); espera();
        pon(p_.test, id_test_, false); espera();
        if (dh) pon(p_.rst, id_rst_, true);
        pon(p_.test, id_test_, true); espera();
    }
    bool franja_tdo() {
        p_.rst->set_hiz(id_rst_); espera();
        pon(p_.test, id_test_, false); espera();
        const bool b = lee(p_.rst);
        pon(p_.test, id_test_, true); espera();
        return b;
    }
    // n bits, el MSB primero; TMS a 1 en el último (Shift de JTAGfunc430.c).
    // Termina en Run-Test/Idle con TDI = TCLK.
    uint32_t desplaza(unsigned n, uint32_t v) {
        uint32_t r = 0;
        for (unsigned i = n; i > 0; --i) {
            const bool b = (v >> (i - 1)) & 1u;
            const bool t = tck_ciclo(i == 1, b);
            r = (r << 1) | (t ? 1u : 0u);
        }
        tck_ciclo(true, tclk_);                 // Exit1 -> Update
        tck_ciclo(false, tclk_);                // Update -> Run-Test/Idle
        return r;
    }
};

} // namespace stm32
#endif // STM32_M430_SONDA_SONDA_M430_H
