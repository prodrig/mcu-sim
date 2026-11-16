// =============================================================================
// replicador430.h — El Replicator430 de TI, compilado para el anfitrión, como
// oráculo (P-20 / M2 de doc/analisis_msp430.md, §7.3)
//
// JTAGfunc430.c y Devices430.c del Replicator430 1.22 (SLAU320, BSD-3, en
// verif/m430/replicator/430/ sin cambiar una línea) se incluyen dos veces,
// cada una en su espacio de nombres: `sbw` (INTERFACE = SPYBIWIRE_IF) y
// `jtag` (SPYBIWIREJTAG_IF: JTAG de 4 hilos en un chip con SBW). Debajo,
// nuestro LowLevelFunc430.h y el puerto simulado de hil_rep.h.
//
// `Replicador430` es la fachada: las funciones de TI con sus nombres, sobre
// los pines de un chip simulado, desde un SC_THREAD.
// =============================================================================
#ifndef STM32_VERIF_M430_REPLICATOR_REPLICADOR430_H
#define STM32_VERIF_M430_REPLICATOR_REPLICADOR430_H

#include <cstdio>
#include "hil_rep.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define bool rep_bool

namespace stm32 { namespace rep430 {

namespace sbw {
#define REP_INTERFACE 3
#include "430/JTAGfunc430.c"
#include "430/Devices430.c"
}

#undef REP_INTERFACE
#undef INTERFACE
#undef SPYBIWIRE_MODE
#undef _DEVICES_H_
#undef __BYTEWORD__
#undef __DATAFORMATS__
#undef ClrTCLK
#undef SetTCLK

namespace jtag {
#define REP_INTERFACE 2
#include "430/JTAGfunc430.c"
#include "430/Devices430.c"
}

}} // namespace stm32::rep430

#undef bool
#pragma GCC diagnostic pop

namespace stm32 {

// Las funciones de TI que usan los bancos, por la interfaz que toque
class Replicador430 {
public:
    Replicador430(const HilRep::Pines& p, bool sbw) : hil_(p, sbw), sbw_(sbw) {}
    HilRep& hil() { return hil_; }

#define REP430_LLAMA(f, ...) (sbw_ ? (rep430::sbw::hil = &hil_, rep430::sbw::f(__VA_ARGS__)) \
                                   : (rep430::jtag::hil = &hil_, rep430::jtag::f(__VA_ARGS__)))
    // STATUS_OK (1), STATUS_ERROR (0) o STATUS_FUSEBLOWN (2)
    unsigned get_device() { return REP430_LLAMA(GetDevice); }
    void init_target() { REP430_LLAMA(InitTarget); }
    void release_target() { REP430_LLAMA(ReleaseTarget); }
    void release_device(uint16_t a) { REP430_LLAMA(ReleaseDevice, a); }
    void erase_flash(uint16_t modo, uint16_t a) { REP430_LLAMA(EraseFLASH, modo, a); }
    unsigned erase_check(uint16_t a, uint16_t n) { return REP430_LLAMA(EraseCheck, a, n); }
    void write_flash(uint16_t a, uint16_t n, uint16_t* d) { REP430_LLAMA(WriteFLASH, a, n, d); }
    unsigned verify_mem(uint16_t a, uint16_t n, uint16_t* d) { return REP430_LLAMA(VerifyMem, a, n, d); }
    uint16_t read_mem(bool palabra, uint16_t a) { return REP430_LLAMA(ReadMem, palabra ? 16 : 8, a); }
    void write_mem(bool palabra, uint16_t a, uint16_t v) { REP430_LLAMA(WriteMem, palabra ? 16 : 8, a, v); }
    void write_mem_quick(uint16_t a, uint16_t n, uint16_t* d) { REP430_LLAMA(WriteMemQuick, a, n, d); }
    void read_mem_quick(uint16_t a, uint16_t n, uint16_t* d) { REP430_LLAMA(ReadMemQuick, a, n, d); }
    unsigned blow_fuse() { return REP430_LLAMA(BlowFuse); }
    unsigned is_fuse_blown() { return REP430_LLAMA(IsFuseBlown); }
    void unlock_info_a() { REP430_LLAMA(UnlockInfoA); }
    void halt_cpu() { REP430_LLAMA(HaltCPU); }
    void release_cpu() { REP430_LLAMA(ReleaseCPU); }
    unsigned execute_por() { return REP430_LLAMA(ExecutePOR); }
    // Los modos de borrado de Devices430.h (ERASE_MASS, ERASE_MAIN, ERASE_SGMT)
    static constexpr uint16_t BORRA_MASIVO = 0xA506, BORRA_PRINCIPAL = 0xA504, BORRA_SEGMENTO = 0xA502;
#undef REP430_LLAMA

private:
    HilRep hil_;
    bool sbw_;
};

} // namespace stm32
#endif // STM32_VERIF_M430_REPLICATOR_REPLICADOR430_H
