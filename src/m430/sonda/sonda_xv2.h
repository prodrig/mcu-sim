// =============================================================================
// sonda_xv2.h — La sonda de bancada de MSP430 para la CPUXv2 (5xx/6xx)
// (M4 de doc/analisis_msp430.md: --gdb por los pines del F5529)
//
// Va sobre el HIL de SondaM430 (los pines por SBW o por JTAG de 4 hilos).
// Las secuencias son las del Replicator430Xv2 de TI (SLAU320, BSD-3, en
// verif/f5529/replicator_xv2: SyncJtag_AssertPor, ExecutePOR_430Xv2,
// SetPC_430Xv2, ReadMem_430Xv2, WriteMem_430Xv2, ReleaseDevice_430Xv2,
// WriteFLASH_430Xv2 y EraseFLASH_430Xv2 con sus rutinas en la RAM del
// objetivo y el JMB) y las de la MSP Debug Stack (BSD-3: ReadCpuRegXv2 y
// WriteCpuRegXv2 de edt.h, EemDataExchangeXv2.c).
//
// [de modelo] El núcleo no tiene la cola de búsqueda de la CPUXv2: suspendida
// (CPUSUSP), la CPU espera en la búsqueda del PC, y ADDR_CAPTURE da el PC tal
// cual (la MSPDS le resta 4). Por eso, tras leer o escribir un registro
// metiendo una MOVA por JTAG, la sonda devuelve el PC con SetPC en vez de con
// el JMP $-4 de la MSPDS.
// =============================================================================
#ifndef STM32_M430_SONDA_SONDA_XV2_H
#define STM32_M430_SONDA_SONDA_XV2_H

#include <systemc>
#include <cstdint>
#include <vector>
#include "sonda_m430.h"

namespace stm32 {

class SondaXv2 {
public:
    // `ram`: donde se cargan las rutinas de la Flash (RAM_START_ADDRESS)
    SondaXv2(SondaM430& h, uint32_t ram = 0x2400) : h_(h), ram_(ram) {}

    uint8_t conecta() { return h_.conecta(); }
    void desconecta() { h_.desconecta(); }

    // --- El control ------------------------------------------------------------
    void cntrl(uint16_t v) { h_.ir_shift(IR430_CNTRL_SIG_16BIT); h_.dr_shift(16, v); }
    uint16_t captura() { h_.ir_shift(IR430_CNTRL_SIG_CAPTURE); return uint16_t(h_.dr_shift(16, 0)); }
    // SyncJtag (sin POR): la CPU bajo JTAG y suspendida en una búsqueda
    bool sincroniza() {
        cntrl(0x1501);
        for (int i = 0; i < 50; ++i) {
            if (captura() & 0x0200) return true;                    // TCE0
            sc_core::wait(10, sc_core::SC_US);
        }
        return false;
    }
    // SyncJtag_AssertPor y ExecutePOR_430Xv2: la CPU, tras un POR, en la
    // búsqueda del vector de reset (y el perro parado)
    bool sincroniza_por() {
        if (!sincroniza()) return false;
        captura();
        h_.tclk(false); h_.tclk(true);
        cntrl(0x0C01);
        h_.dr_shift(16, 0x0401);
        for (int i = 0; i < 5; ++i) { h_.tclk(false); h_.tclk(true); }
        cntrl(0x0501);
        h_.tclk(false); h_.tclk(true);
        escribe(0x015C, 0x5A80);                                    // WDTCTL
        return (captura() & 0x0301) != 0;
    }
    uint32_t lee_pc() { h_.ir_shift(IR430_ADDR_CAPTURE); return h_.dr_shift(20, 0) & 0xFFFFFu; }

    // --- La memoria -------------------------------------------------------------
    uint16_t lee(uint32_t a, bool byte = false) {
        if (!(captura() & 0x0301)) return 0;
        h_.tclk(false);
        cntrl(byte ? 0x0511 : 0x0501);
        h_.ir_shift(IR430_ADDR_16BIT); h_.dr_shift(20, a);
        h_.ir_shift(IR430_DATA_TO_ADDR);
        h_.tclk(true); h_.tclk(false);
        const uint16_t v = uint16_t(h_.dr_shift(16, 0));
        h_.tclk(true); h_.tclk(false); h_.tclk(true);
        return v;
    }
    void escribe(uint32_t a, uint16_t v, bool byte = false) {
        if (!(captura() & 0x0301)) return;
        h_.tclk(false);
        cntrl(byte ? 0x0510 : 0x0500);
        h_.ir_shift(IR430_ADDR_16BIT); h_.dr_shift(20, a);
        h_.tclk(true);
        h_.ir_shift(IR430_DATA_TO_ADDR); h_.dr_shift(16, v);
        h_.tclk(false);
        cntrl(0x0501);
        h_.tclk(true); h_.tclk(false); h_.tclk(true);
    }

    // --- El PC y los registros ----------------------------------------------------
    // SetPC_430Xv2: MOVA #pc,PC por IR_DATA_16BIT; luego, suspendida
    void pon_pc(uint32_t pc) {
        mova_imm(0, pc);
    }
    // WriteCpuRegXv2: MOVA #v,Rn (n de 1 a 15); el PC, como estaba
    void pon_reg(unsigned r, uint32_t v) {
        if (r == 0) { pon_pc(v); return; }
        const uint32_t pc = lee_pc();
        mova_imm(r, v);
        pon_pc(pc);
    }
    // ReadCpuRegXv2: MOVA Rn,&0x018C (el JMB, como la MSPDS) y las dos
    // escrituras de 16 bits capturadas en el MDB; el PC, como estaba
    uint32_t lee_reg(unsigned r) {
        const uint32_t pc = lee_pc();
        if (!(captura() & 0x0301)) return 0;
        h_.tclk(false);
        h_.ir_shift(IR430_DATA_16BIT);
        h_.tclk(true);
        h_.dr_shift(16, 0x0060u | ((r & 15u) << 8));               // MOVA Rn,&abs20
        h_.tclk(false);
        cntrl(0x1401);
        h_.ir_shift(IR430_DATA_16BIT);
        h_.tclk(false); h_.tclk(true);                              // la MOVA
        h_.dr_shift(16, 0x018C);
        h_.tclk(false); h_.tclk(true);                              // su dirección
        h_.tclk(false);
        h_.ir_shift(IR430_DATA_CAPTURE);
        h_.tclk(true);                                              // la escritura baja
        const uint32_t lo = h_.dr_shift(16, 0);
        h_.tclk(false); h_.tclk(true);                              // la alta
        const uint32_t hi = h_.dr_shift(16, 0);
        h_.tclk(false);
        cntrl(0x0501);
        h_.tclk(true);
        pon_pc(pc);
        return (lo & 0xFFFFu) | ((hi & 0xFu) << 16);
    }
    // Una instrucción (SingleStepXv2, sin el EEM): hasta la búsqueda siguiente
    bool un_paso() {
        cntrl(0x0401);
        bool ok = false;
        for (int i = 0; i < 64 && !ok; ++i) {
            h_.tclk(false); h_.tclk(true);
            ok = (captura() & CS430_INSTR_LOAD) != 0;
        }
        cntrl(0x0501);
        h_.tclk(false); h_.tclk(true);
        return ok;
    }
    // ReleaseDevice_430Xv2: la CPU, libre (desde el PC que tenga)
    void suelta() {
        cntrl(0x0401);
        h_.ir_shift(IR430_ADDR_CAPTURE);
        h_.ir_shift(IR430_CNTRL_SIG_RELEASE);
    }
    void suelta_en(uint32_t pc) {
        pon_pc(pc);
        h_.tclk(true);
        suelta();
    }

    // --- El EEM (EemDataExchangeXv2: 32 bits) ---------------------------------------
    uint32_t lee_eem(uint8_t dir) {
        h_.ir_shift(IR430_EMEX_DATA_EXCHANGE32); h_.dr_shift(32, uint32_t(dir) | 1u);
        return h_.dr_shift(32, 0);
    }
    void escribe_eem(uint8_t dir, uint32_t v) {
        h_.ir_shift(IR430_EMEX_DATA_EXCHANGE32); h_.dr_shift(32, dir); h_.dr_shift(32, v);
    }

    // El BOR por JTAG (TEST_REG = 0x0200: ReleaseDevice_430Xv2(V_BOR))
    void bor() { h_.ir_shift(IR430_TEST_REG); h_.dr_shift(16, 0x0200); sc_core::wait(1, sc_core::SC_MS); }

    // --- El JSTATE de las FRAM (64 bits) [MSPDS PollJStateReg.c] ---------------------
    uint64_t lee_jstate() { h_.ir_shift(IR430_JSTATE_ID); return h_.dr_shift64(0); }

    // --- El JMB ------------------------------------------------------------------
    uint32_t lee_jmb() {
        h_.ir_shift(IR430_JMB_EXCHANGE);
        uint32_t r = h_.dr_shift(16, 0);
        if (r & 0x0008) {                                           // OUT1RDY
            h_.dr_shift(16, 0x0014);                                // JMB32B + OUTREQ
            const uint32_t o0 = h_.dr_shift(16, 0), o1 = h_.dr_shift(16, 0);
            r = (o1 << 16) | (o0 & 0xFFFFu);
        }
        return r;
    }
    bool escribe_jmb16(uint16_t v) {
        h_.ir_shift(IR430_JMB_EXCHANGE);
        int i = 0;
        while (!(h_.dr_shift(16, 0) & 0x0001) && ++i < 3000) {}    // IN0RDY
        if (i >= 3000) return false;
        h_.dr_shift(16, 0x0001);                                    // INREQ
        h_.dr_shift(16, v);
        return true;
    }

    // --- La Flash de 5xx: las rutinas de TI en la RAM del objetivo ----------------
    // EraseFLASH_430Xv2 (modo: 0xA502 segmento, 0xA504 banco, 0xA506 todo)
    bool borra_flash(uint16_t modo, uint32_t dir, uint16_t fctl3 = 0xA548) {
        std::vector<uint16_t> f(FLASH_BORRA, FLASH_BORRA + N_BORRA);
        f[2] = uint16_t(dir); f[3] = uint16_t(dir >> 16); f[4] = modo; f[5] = fctl3;
        carga(f);
        suelta_en(ram_ + f[0]);
        bool ok = espera_jmb(0xCAFEBABEu);
        if (!sincroniza_por()) ok = false;
        limpia(f.size());
        return ok;
    }
    // WriteFLASH_430Xv2: la rutina, y las palabras por el JMB
    bool escribe_flash(uint32_t dir, const uint16_t* w, unsigned n, uint16_t fctl3 = 0xA548) {
        std::vector<uint16_t> f(FLASH_ESCRIBE, FLASH_ESCRIBE + N_ESCRIBE);
        f[2] = uint16_t(dir); f[3] = uint16_t(dir >> 16); f[4] = uint16_t(n); f[5] = 0; f[6] = fctl3;
        carga(f);
        suelta_en(ram_ + f[0]);
        bool ok = espera_jmb(0xABADBABEu);
        if (ok) for (unsigned i = 0; i < n && ok; ++i) ok = escribe_jmb16(w[i]);
        if (ok) ok = espera_jmb(0xCAFEBABEu);
        if (!sincroniza_por()) ok = false;
        limpia(f.size());
        return ok;
    }

private:
    SondaM430& h_;
    uint32_t ram_;

    // MOVA #imm20,Rn por JTAG (SetPC_430Xv2 con Rn en lugar del PC); acaba
    // suspendida en la búsqueda siguiente
    void mova_imm(unsigned r, uint32_t v) {
        if (!(captura() & 0x0301)) return;
        h_.tclk(false);
        h_.ir_shift(IR430_DATA_16BIT);
        h_.tclk(true);
        h_.dr_shift(16, 0x0080u | ((v >> 8) & 0x0F00u) | (r & 15u));
        h_.tclk(false);
        cntrl(0x1400);
        h_.ir_shift(IR430_DATA_16BIT);
        h_.tclk(false); h_.tclk(true);                              // la MOVA
        h_.dr_shift(16, v & 0xFFFFu);
        h_.tclk(false); h_.tclk(true);                              // el inmediato
        h_.dr_shift(16, 0x4303);                                    // NOP, como SetPC_430Xv2
        h_.tclk(false);
        cntrl(0x0501);
        h_.tclk(true);
    }
    void carga(const std::vector<uint16_t>& f) {
        for (size_t i = 0; i < f.size(); ++i) escribe(ram_ + 2 * uint32_t(i), f[i]);
    }
    void limpia(size_t n) {
        for (size_t i = 0; i < n; ++i) escribe(ram_ + 2 * uint32_t(i), 0x3FFF);
    }
    bool espera_jmb(uint32_t que) {
        for (int i = 0; i < 3000; ++i) {
            if (lee_jmb() == que) return true;
            sc_core::wait(10, sc_core::SC_US);
        }
        return false;
    }

    // Las rutinas de TI (Replicator430Xv2: FlashErase.c y FlashWrite.c,
    // Copyright (C) 2016 Texas Instruments Incorporated, BSD-3)
    static constexpr uint16_t FLASH_BORRA[] = {
#include "funclet_borra_xv2.inc"
    };
    static constexpr uint16_t FLASH_ESCRIBE[] = {
#include "funclet_escribe_xv2.inc"
    };
    static constexpr unsigned N_BORRA = sizeof(FLASH_BORRA) / sizeof(FLASH_BORRA[0]);
    static constexpr unsigned N_ESCRIBE = sizeof(FLASH_ESCRIBE) / sizeof(FLASH_ESCRIBE[0]);
};

} // namespace stm32
#endif // STM32_M430_SONDA_SONDA_XV2_H
