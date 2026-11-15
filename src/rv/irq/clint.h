// =============================================================================
// clint.h — El CLINT (temporizador e interrupción software) y el controlador estándar
//
// E1 de doc/analisis_esp32_riscv.md, §3.5. Dos piezas, ninguna de Espressif:
//
//   Clint          el bloque de SiFive que casi todos los RISC-V copian: un
//                  `mtime` de 64 bits que avanza a `hz` (de un reloj de tiempo
//                  real, no del núcleo), un `mtimecmp` y un `msip` por hart, en
//                  el mapa de siempre: msip en +0x0000 + 4·hart, mtimecmp en
//                  +0x4000 + 8·hart y mtime en +0xBFF8. Es el que tiene Spike
//                  en 0x0200_0000. El del ESP32-C6 es una variante (con
//                  MTIMECTL, UTIMECMP y USIP) que monta su chip.
//   IrqEstandar    el rv_irq_if de un hart con mip/mie "de libro": MSIP (3) y
//                  MTIP (7) del CLINT, MEIP (11) de una línea externa, y el
//                  orden estándar de prioridad.
//
// `mtime` se calcula a partir del tiempo simulado, sin un proceso que lo
// incremente: cuesta cero mientras nadie lo mira. Cuando un hart duerme en wfi
// esperando a su `mtimecmp`, el CLINT le avisa en el instante exacto.
// =============================================================================
#ifndef STM32_RV_IRQ_CLINT_H
#define STM32_RV_IRQ_CLINT_H

#include <systemc>
#include <tlm>
#include <cstdint>
#include <vector>
#include "../hart/nucleo_rv.h"

namespace stm32 {

class Clint : public sc_core::sc_module {
public:
    Clint(sc_core::sc_module_name nm, unsigned harts, double hz = 10e6)
        : sc_core::sc_module(nm), hz_(hz), msip_(harts, 0), mtimecmp_(harts, ~0ull),
          ev_(harts), nucleos_(harts, nullptr) {
        SC_HAS_PROCESS(Clint);
        for (unsigned h = 0; h < harts; ++h) {
            sc_core::sc_spawn_options o;
            o.spawn_method();
            o.set_sensitivity(&ev_[h]);
            o.dont_initialize();
            sc_core::sc_spawn([this, h]() { if (nucleos_[h]) nucleos_[h]->nota_irq(); },
                              sc_core::sc_gen_unique_name("vence"), &o);
        }
    }
    void engancha(unsigned hart, NucleoRv* n) { nucleos_.at(hart) = n; }
    unsigned harts() const { return unsigned(msip_.size()); }

    uint64_t mtime() const {
        const double s = (sc_core::sc_time_stamp() - t0_).to_seconds();
        return base_ + uint64_t(s * hz_);
    }
    bool msip(unsigned h) const { return h < msip_.size() && msip_[h]; }
    bool mtip(unsigned h) const { return h < mtimecmp_.size() && mtime() >= mtimecmp_[h]; }
    void reset() {
        for (auto& m : msip_) m = 0;
        for (auto& c : mtimecmp_) c = ~0ull;
        base_ = 0; t0_ = sc_core::sc_time_stamp();
    }

    // El acceso desde el bus, con la dirección ya relativa al CLINT
    void acceso(tlm::tlm_generic_payload& gp, uint32_t off) {
        const bool escr = gp.is_write();
        const unsigned n = gp.get_data_length();
        unsigned char* p = gp.get_data_ptr();
        uint32_t v = 0;
        if (escr) for (unsigned i = 0; i < n && i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
        uint32_t r = 0;
        const unsigned w = off & ~3u, sh = (off & 3u) * 8u;
        if (w < 0x4000u) {
            const unsigned h = w / 4u;
            if (h < msip_.size()) {
                if (escr) { msip_[h] = v & 1u; avisa(h); }
                r = msip_[h];
            }
        } else if (w >= 0x4000u && w < 0xBFF8u) {
            const unsigned h = (w - 0x4000u) / 8u;
            if (h < mtimecmp_.size()) {
                uint64_t& c = mtimecmp_[h];
                const bool alto = ((w - 0x4000u) & 4u) != 0;
                if (escr) {
                    if (alto) c = (c & 0xFFFFFFFFull) | (uint64_t(v) << 32);
                    else      c = (c & ~0xFFFFFFFFull) | v;
                    programa(h);
                }
                r = alto ? uint32_t(c >> 32) : uint32_t(c);
            }
        } else if (w == 0xBFF8u || w == 0xBFFCu) {
            uint64_t t = mtime();
            if (escr) {
                if (w == 0xBFF8u) t = (t & ~0xFFFFFFFFull) | v;
                else              t = (t & 0xFFFFFFFFull) | (uint64_t(v) << 32);
                base_ = t; t0_ = sc_core::sc_time_stamp();
                for (unsigned h = 0; h < mtimecmp_.size(); ++h) programa(h);
            }
            r = (w == 0xBFF8u) ? uint32_t(t) : uint32_t(t >> 32);
        }
        if (!escr) for (unsigned i = 0; i < n; ++i) p[i] = uint8_t((r >> sh) >> (8 * i));
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

private:
    double hz_;
    std::vector<uint32_t> msip_;
    std::vector<uint64_t> mtimecmp_;
    std::vector<sc_core::sc_event> ev_;
    std::vector<NucleoRv*> nucleos_;
    uint64_t base_ = 0;
    sc_core::sc_time t0_{sc_core::SC_ZERO_TIME};

    void avisa(unsigned h) { ev_[h].notify(sc_core::SC_ZERO_TIME); }
    // El aviso de vencimiento, para un hart que duerme en wfi
    void programa(unsigned h) {
        ev_[h].cancel();
        const uint64_t ahora = mtime(), c = mtimecmp_[h];
        if (c <= ahora) { avisa(h); return; }
        const double s = double(c - ahora) / hz_;
        if (s < 1e6) ev_[h].notify(sc_core::sc_time(s, sc_core::SC_SEC));
    }
};

// El controlador estándar de un hart: MSIP y MTIP del CLINT, y MEIP de fuera
class IrqEstandar : public rv_irq_if {
public:
    IrqEstandar(Clint& c, unsigned hart) : clint_(c), hart_(hart) {}
    void pon_meip(bool v) { meip_ = v; }
    uint32_t pendientes() override {
        uint32_t p = 0;
        if (clint_.msip(hart_)) p |= 1u << 3;
        if (clint_.mtip(hart_)) p |= 1u << 7;
        if (meip_) p |= 1u << 11;
        return p;
    }
    bool elige(uint32_t c, unsigned& id) override { return rv_elige_estandar(c, id); }

private:
    Clint& clint_;
    unsigned hart_;
    bool meip_ = false;
};

} // namespace stm32
#endif // STM32_RV_IRQ_CLINT_H
