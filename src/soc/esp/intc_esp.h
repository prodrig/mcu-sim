// =============================================================================
// intc_esp.h — La matriz de interrupciones de Espressif y el INTC del C3
//
// E3 de doc/analisis_esp32_riscv.md, §3.5. Un solo bloque de registros
// (INTERRUPT_CORE0, en 0x600C_2000 en el C3) con las dos cosas:
//
//   LA MATRIZ    Cada fuente de periférico (62 en el C3) tiene un registro de
//                MAPEO (`+4·n`) que dice a qué línea de la CPU va (1-31; 0 la
//                deja suelta). Varias fuentes pueden ir a la misma línea: se
//                suman con un OR. INTR_STATUS_0/1 dan el nivel crudo de cada
//                fuente. [TRM-C3 §8.3]
//   EL INTC      `IntcEspV1`: 31 líneas con habilitación, tipo (nivel o
//                flanco), prioridad 1-15 y un umbral global. Una línea entra
//                si está habilitada, pendiente, con prioridad distinta de 0 y
//                >= el umbral; entre varias, la de MÁS prioridad y, a igual
//                prioridad, la de ID más bajo. Las de flanco se enganchan en
//                la subida y se borran «haciendo pulso» en CPU_INT_CLEAR (poner
//                y quitar el bit), solo si ya se reclamaron o si la línea está
//                deshabilitada; las de nivel se borran en la fuente.
//                Sin mie/mip: el núcleo pregunta por `rv_irq_if`. [TRM-C3 §1.5]
//
// La LATENCIA de 4 ciclos del INTC [TRM-C3 §1.5.3.1] va en
// RvTiempos::lat_irq del núcleo: se factura al entrar, y una petición nueva no
// entra en la frontera de instrucción que sigue sino en la otra
// (RvHart::visibles).
//
// La disposición de los registros es la del C3; la del C6 (INTMTX + INTPRI)
// es otra clase, IntcEspV2 (E9).
// =============================================================================
#ifndef STM32_SOC_ESP_INTC_ESP_H
#define STM32_SOC_ESP_INTC_ESP_H

#include <systemc>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../../rv/hart/rv_ifaces.h"

namespace stm32 {

class MatrizIntEspV1 : public BusSlave, public rv_irq_if {
public:
    // A quién avisar cuando cambia algo (el núcleo: NucleoRv::nota_irq)
    std::function<void()> aviso;

    MatrizIntEspV1(sc_core::sc_module_name nm, uint32_t base, unsigned n_fuentes)
        : BusSlave(nm, base, 0x800), n_(n_fuentes), mapa_(n_fuentes, 0u),
          fuente_(n_fuentes, false) {
        reset_regs();
    }

    void reset() { reset_regs(); recalcula(); }
    // --- Lo que llaman los periféricos -----------------------------------------
    void fuente(unsigned n, bool nivel) {
        if (n >= n_ || fuente_[n] == nivel) return;
        fuente_[n] = nivel;
        recalcula();
    }
    bool nivel_fuente(unsigned n) const { return n < n_ && fuente_[n]; }

    // --- rv_irq_if ---------------------------------------------------------------
    uint32_t pendientes() override { return eip(); }
    bool elige(uint32_t c, unsigned& id) override {
        unsigned mejor = 0, pmax = 0;
        for (unsigned i = 1; i < 32; ++i)
            if ((c >> i) & 1u)
                if (prio_[i] > pmax) { pmax = prio_[i]; mejor = i; }
        if (!mejor) return false;
        id = mejor;
        return true;
    }
    void reclamada(unsigned id) override {
        if (id < 32) reclamada_ |= 1u << id;
        ++n_recl_;
    }

    // --- Para el banco ---------------------------------------------------------
    uint32_t eip() const {
        uint32_t r = 0;
        for (unsigned i = 1; i < 32; ++i)
            if (((pend_ & ena_) >> i & 1u) && prio_[i] && prio_[i] >= umbral_) r |= 1u << i;
        return r;
    }
    unsigned linea_de(unsigned fuente) const { return fuente < n_ ? mapa_[fuente] : 0u; }
    uint64_t reclamaciones() const { return n_recl_; }

    static constexpr uint32_t R_STATUS0 = 0x0F8, R_STATUS1 = 0x0FC, R_CLOCK_GATE = 0x100,
                              R_ENABLE = 0x104, R_TYPE = 0x108, R_CLEAR = 0x10C,
                              R_EIP = 0x110, R_PRI0 = 0x114, R_THRESH = 0x194,
                              R_DATE = 0x7FC;

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 4u * n_) return mapa_[off / 4];
        switch (off) {
        case R_STATUS0: { uint32_t v = 0; for (unsigned i = 0; i < 32 && i < n_; ++i) v |= uint32_t(fuente_[i]) << i; return v; }
        case R_STATUS1: { uint32_t v = 0; for (unsigned i = 32; i < 64 && i < n_; ++i) v |= uint32_t(fuente_[i]) << (i - 32); return v; }
        case R_CLOCK_GATE: return clock_gate_;
        case R_ENABLE: return ena_;
        case R_TYPE:   return tipo_;
        case R_CLEAR:  return clear_;
        case R_EIP:    return eip();
        case R_THRESH: return umbral_;
        case R_DATE:   return 0x2007210u;
        default: break;
        }
        if (off >= R_PRI0 && off < R_PRI0 + 4 * 32) return prio_[(off - R_PRI0) / 4];
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        if (off < 4u * n_) { mapa_[off / 4] = (mapa_[off / 4] & ~m) | (v & m & 0x1Fu); recalcula(); return; }
        switch (off) {
        case R_CLOCK_GATE: clock_gate_ = (clock_gate_ & ~m) | (v & m & 1u); return;
        case R_ENABLE: ena_ = ((ena_ & ~m) | (v & m)) & ~1u; recalcula(); return;
        case R_TYPE:   tipo_ = ((tipo_ & ~m) | (v & m)) & ~1u; recalcula(); return;
        case R_CLEAR: {
            const uint32_t nuevo = (clear_ & ~m) | (v & m);
            // El pulso: el bit que pasa a 1 borra el enganche de una de flanco
            // ya reclamada, o de una deshabilitada
            const uint32_t sube = nuevo & ~clear_;
            const uint32_t borra = sube & tipo_ & (reclamada_ | ~ena_);
            pend_ &= ~borra;
            reclamada_ &= ~borra;
            clear_ = nuevo;
            recalcula();
            return;
        }
        case R_THRESH: umbral_ = (umbral_ & ~m) | (v & m & 0xFu); recalcula(); return;
        default: break;
        }
        if (off >= R_PRI0 && off < R_PRI0 + 4 * 32) {
            const unsigned i = (off - R_PRI0) / 4;
            if (i) prio_[i] = (prio_[i] & ~m) | (v & m & 0xFu);
            recalcula();
        }
    }

private:
    unsigned n_;
    std::vector<uint32_t> mapa_;
    std::vector<bool> fuente_;
    uint32_t ena_ = 0, tipo_ = 0, clear_ = 0, umbral_ = 0, clock_gate_ = 1;
    uint32_t prio_[32] = {};
    uint32_t linea_ = 0;      // nivel de cada línea (OR de sus fuentes)
    uint32_t pend_ = 0;       // nivel, o enganche de flanco
    uint32_t reclamada_ = 0;
    uint64_t n_recl_ = 0;

    void reset_regs() {
        for (uint32_t& x : mapa_) x = 0;
        ena_ = tipo_ = clear_ = umbral_ = 0;
        clock_gate_ = 1;
        for (uint32_t& p : prio_) p = 0;
        linea_ = pend_ = reclamada_ = 0;
    }
    void recalcula() {
        uint32_t l = 0;
        for (unsigned i = 0; i < n_; ++i)
            if (fuente_[i] && mapa_[i]) l |= 1u << mapa_[i];
        const uint32_t sube = l & ~linea_;
        linea_ = l;
        // Flanco: se engancha en la subida (si está habilitada); nivel: sigue
        pend_ = (pend_ & tipo_) | (sube & tipo_ & ena_) | (l & ~tipo_);
        if (aviso) aviso();
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP_INTC_ESP_H
