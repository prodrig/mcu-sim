// =============================================================================
// bitband.h — Traducción de las regiones alias de bit-banding [IR, §5.4]
//
// ARMv7-M define dos ventanas de alias que permiten acceso atómico a bits:
//   SRAM        : base 0x2000 0000-0x200F FFFF (1 MB) -> alias 0x2200 0000 (32 MB)
//   Periféricos : base 0x4000 0000-0x400F FFFF (1 MB) -> alias 0x4200 0000 (32 MB)
//
//   bit_word_addr = alias_base + (byte_offset * 32) + (bit_number * 4)
//
// Una LECTURA del alias devuelve 0 o 1 (bit extendido a la palabra); una
// ESCRITURA usa solo el bit 0 del dato y se traduce en una secuencia atómica
// lectura-modificación-escritura sobre el byte de la región base.
//
// Ubicación en el modelo: el bit-banding es una función del interfaz de bus del
// núcleo Cortex-M4 (regiones del mapa ARMv7-M, no del interconector de ST), por
// lo que la capa se aplica en el router del núcleo (core/cortex_m4f.h) antes de
// emitir la transacción a la matriz. Los maestros DMA no ven los alias: sus
// accesos a 0x2200 0000/0x4200 0000 caen en rango reservado de la matriz y
// obtienen ERROR, igual que en el silicio.
// =============================================================================
#ifndef STM32_BUS_BITBAND_H
#define STM32_BUS_BITBAND_H

#include <tlm>
#include "../common/ahb_types.h"

namespace stm32 {
namespace bitband {

inline bool is_sram_alias(uint64_t a) {
    return a >= addr::BB_SRAM_ALIAS &&
           a <  uint64_t(addr::BB_SRAM_ALIAS) + addr::BB_SRAM_ALEN;
}
inline bool is_periph_alias(uint64_t a) {
    return a >= addr::BB_PERIPH_ALIAS &&
           a <  uint64_t(addr::BB_PERIPH_ALIAS) + addr::BB_PERIPH_ALEN;
}
inline bool is_alias(uint64_t a) { return is_sram_alias(a) || is_periph_alias(a); }

// Descompone una dirección alias en (dirección del byte base, nº de bit 0..7).
struct Target { uint32_t byte_addr; unsigned bit; };

inline Target decode(uint64_t alias_addr) {
    const bool sram    = is_sram_alias(alias_addr);
    const uint32_t ab  = sram ? addr::BB_SRAM_ALIAS : addr::BB_PERIPH_ALIAS;
    const uint32_t rb  = sram ? addr::BB_SRAM_BASE  : addr::BB_PERIPH_BASE;
    const uint32_t off = uint32_t(alias_addr - ab);
    Target t;
    t.byte_addr = rb + (off / 32u);
    t.bit       = (off % 32u) / 4u;   // 0..7 (bit dentro del byte)
    return t;
}

// Ejecuta el acceso bit-band emitiendo transacciones de 1 byte a través de
// 'send' (functor: void(tlm_generic_payload&, sc_time&)). Devuelve la respuesta
// en el propio payload del llamante.
template <typename SendFn>
void access(tlm::tlm_generic_payload& gp, sc_core::sc_time& t, SendFn send) {
    const Target tg = decode(gp.get_address());
    unsigned char byte = 0;

    tlm::tlm_generic_payload sub;
    AhbExt* src = gp.get_extension<AhbExt>();
    AhbExt  ext;
    if (src) ext = *src;

    // 1) Lectura del byte base
    gp_setup(sub, false, tg.byte_addr, &byte, 1);
    sub.set_extension(&ext);
    send(sub, t);
    if (sub.get_response_status() != tlm::TLM_OK_RESPONSE) {
        sub.clear_extension(&ext);
        gp.set_response_status(sub.get_response_status());
        return;
    }
    sub.clear_extension(&ext);

    if (gp.is_read()) {
        const uint32_t v = (byte >> tg.bit) & 1u;
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < gp.get_data_length(); ++i)
            d[i] = uint8_t(i == 0 ? v : 0);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }

    // 2) Modificación + escritura (atómica: b_transport no cede el control)
    const bool set = (gp.get_data_ptr()[0] & 1u) != 0;
    byte = uint8_t(set ? (byte | (1u << tg.bit)) : (byte & ~(1u << tg.bit)));
    gp_setup(sub, true, tg.byte_addr, &byte, 1);
    sub.set_extension(&ext);
    send(sub, t);
    sub.clear_extension(&ext);
    gp.set_response_status(sub.get_response_status());
}

} // namespace bitband
} // namespace stm32
#endif // STM32_BUS_BITBAND_H
