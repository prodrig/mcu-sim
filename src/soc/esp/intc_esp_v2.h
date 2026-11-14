// =============================================================================
// intc_esp_v2.h — Las interrupciones del ESP32-C6: INTMTX, INTPRI y el CLINT
//                 de Espressif
//
// E9 de doc/analisis_esp32_riscv.md [TRM-C6 §1.6, §1.7 y §10; intpri_reg.h e
// interrupt_matrix_reg.h de ESP-IDF]. Tres bloques de registros y un solo
// rv_irq_if para el núcleo HP, que SÍ tiene mie/mip (y mideleg):
//
//   LA MATRIZ (INTMTX, 0x6001_0000)  `MatrizIntEspV2`: cada una de las 77
//        fuentes tiene su registro de mapeo en +4·n (la línea de la CPU,
//        1, 2, 5, 6 u 8-31; 0 la suelta), INT_STATUS_0-2 (0x134-0x13C) dan su
//        nivel crudo y DATE (0x7FC) es de lectura y escritura.
//   EL INTPRI (0x600C_5000) Y EL PLIC (0x2000_1000)  `IntPriEsp` (más abajo,
//        los dos bancos, M y U, y sus dos vistas): ENABLE (0x00), TYPE (0x04, 1 =
//        flanco), EIP_STATUS (0x08, solo lectura), PRI_n (0x0C + 4·n, 0-15),
//        THRESH (0x8C), CPU_INTR_FROM_CPU_0-3 (0x90-0x9C: las fuentes 22-25
//        de la matriz, que pone el software), DATE (0xA0) y CLEAR (0xA8). Una
//        línea externa está pendiente si está habilitada, tiene nivel (o el
//        flanco enganchado) y su prioridad es >= el umbral [TRM-C6 §1.6.2];
//        las de flanco se borran haciendo pulso en CLEAR si ya se reclamaron o
//        si la línea está deshabilitada. Las líneas 0, 3, 4 y 7 son del CLINT:
//        sus bits de ENABLE, TYPE y PRI están reservados.
//   EL CLINT (0x2000_1800, en la región de la CPU)  `ClintEsp` [TRM-C6
//        §1.7]: MSIP (0x000), MTIMECTL (0x004: MTCE 0, MTIE 1, MTIP 2 solo
//        lectura, MTOF 3), MTIME (0x008, 64 bits, R/W), MTIMECMP (0x010); y
//        las de U en +0x400: USIP, UTIMECTL (UTIE 1, UTIP 2, UTOF 3), UTIME
//        (la misma cuenta, solo lectura) y UTIMECMP. El contador avanza con
//        MTCE a `hz()` [de modelo: el TRM no dice de qué reloj; se toma el de
//        la CPU] y se calcula del tiempo simulado. MTIP = MTIE y MTIME >=
//        MTIMECMP (igual en U). Dan los ID 0 (USI), 3 (MSI), 4 (UTI) y 7 (MTI).
//
// LA PRIORIDAD (`elige`): una externa siempre antes que las del CLINT; entre
// externas, la de más prioridad y, a igual, la de ID más bajo; entre las del
// CLINT, la prioridad fija MSI 3, MTI 2, USI 1, UTI 0. La latencia de 4
// ciclos va en RvTiempos::lat_irq.
// =============================================================================
#ifndef STM32_SOC_ESP_INTC_ESP_V2_H
#define STM32_SOC_ESP_INTC_ESP_V2_H

#include <systemc>
#include <tlm>
#include <algorithm>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../../rv/hart/rv_ifaces.h"

namespace stm32 {

constexpr uint32_t CLINT_ESP_IDS = (1u << 0) | (1u << 3) | (1u << 4) | (1u << 7);

// --- El CLINT de Espressif -----------------------------------------------------------
class ClintEsp : public sc_core::sc_module {
public:
    std::function<double()> hz = [] { return 20e6; };
    std::function<void()> aviso;                    // cambia lo pendiente

    explicit ClintEsp(sc_core::sc_module_name nm) : sc_core::sc_module(nm) {
        SC_HAS_PROCESS(ClintEsp);
        SC_METHOD(vence);
        sensitive << ev_;
        dont_initialize();
        reset();
    }
    void reset() {
        msip_ = usip_ = 0;
        mctl_ = uctl_ = 0;
        mcmp_ = ucmp_ = 0;
        base_ = 0; t0_ = sc_core::sc_time_stamp();
        ev_.cancel();
        if (aviso) aviso();
    }
    uint64_t mtime() const {
        if (!(mctl_ & 1u)) return base_;
        return base_ + uint64_t((sc_core::sc_time_stamp() - t0_).to_seconds() * hz());
    }
    // Los ID que pide: 0 USI, 3 MSI, 4 UTI, 7 MTI
    uint32_t pendientes() const {
        uint32_t p = 0;
        const uint64_t t = mtime();
        if (usip_) p |= 1u << 0;
        if (msip_) p |= 1u << 3;
        if ((uctl_ & 2u) && t >= ucmp_) p |= 1u << 4;
        if ((mctl_ & 2u) && t >= mcmp_) p |= 1u << 7;
        return p;
    }
    // El acceso desde el bus, con la dirección relativa a 0x2000_1800
    void acceso(tlm::tlm_generic_payload& gp, uint32_t off) {
        const bool escr = gp.is_write();
        const unsigned n = gp.get_data_length();
        unsigned char* p = gp.get_data_ptr();
        uint32_t v = 0;
        if (escr) for (unsigned i = 0; i < n && i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
        const unsigned w = off & ~3u, sh = (off & 3u) * 8u;
        const bool u = w >= 0x400;
        const unsigned r0 = w & 0x3FFu;
        uint32_t r = 0;
        uint64_t& cmp = u ? ucmp_ : mcmp_;
        uint32_t& ctl = u ? uctl_ : mctl_;
        switch (r0) {
        case 0x000: {
            uint32_t& sip = u ? usip_ : msip_;
            if (escr) { sip = v & 1u; cambia(); }
            r = sip;
            break;
        }
        case 0x004:
            if (escr) {
                const uint64_t t = mtime();
                if (u) uctl_ = v & 0xAu;
                else {
                    mctl_ = v & 0xBu;
                    base_ = t; t0_ = sc_core::sc_time_stamp();
                }
                cambia();
            }
            r = ctl | (((ctl & 2u) && mtime() >= cmp) ? 4u : 0u);
            break;
        case 0x008: case 0x00C: {
            uint64_t t = mtime();
            if (escr && !u) {
                if (r0 == 0x008) t = (t & ~0xFFFFFFFFull) | v;
                else t = (t & 0xFFFFFFFFull) | (uint64_t(v) << 32);
                base_ = t; t0_ = sc_core::sc_time_stamp();
                cambia();
            }
            r = r0 == 0x008 ? uint32_t(t) : uint32_t(t >> 32);
            break;
        }
        case 0x010: case 0x014:
            if (escr) {
                if (r0 == 0x010) cmp = (cmp & ~0xFFFFFFFFull) | v;
                else cmp = (cmp & 0xFFFFFFFFull) | (uint64_t(v) << 32);
                cambia();
            }
            r = r0 == 0x010 ? uint32_t(cmp) : uint32_t(cmp >> 32);
            break;
        default: break;
        }
        if (!escr) for (unsigned i = 0; i < n; ++i) p[i] = uint8_t((r >> sh) >> (8 * i));
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    // Los cambios de reloj de la CPU: la cuenta sigue desde donde iba
    void reloj_cambia() { base_ = mtime(); t0_ = sc_core::sc_time_stamp(); cambia(); }

private:
    uint32_t msip_ = 0, usip_ = 0, mctl_ = 0, uctl_ = 0;
    uint64_t mcmp_ = 0, ucmp_ = 0, base_ = 0;
    sc_core::sc_time t0_{sc_core::SC_ZERO_TIME};
    sc_core::sc_event ev_;

    void cambia() {
        if (aviso) aviso();
        programa();
    }
    // El próximo vencimiento, para despertar a un núcleo en wfi
    void programa() {
        ev_.cancel();
        if (!(mctl_ & 1u)) return;
        const uint64_t t = mtime();
        uint64_t prox = ~0ull;
        if ((mctl_ & 2u) && mcmp_ > t) prox = std::min(prox, mcmp_);
        if ((uctl_ & 2u) && ucmp_ > t) prox = std::min(prox, ucmp_);
        if (prox == ~0ull) return;
        const double s = double(prox - t) / hz();
        if (s < 1e6) ev_.notify(sc_core::sc_time(s, sc_core::SC_SEC));
    }
    void vence() { if (aviso) aviso(); programa(); }
};

// --- La matriz del C6 -----------------------------------------------------------------
class MatrizIntEspV2 : public BusSlave {
public:
    std::function<void(uint32_t lineas)> lineas_cambian;      // al INTPRI

    MatrizIntEspV2(sc_core::sc_module_name nm, uint32_t base, unsigned n_fuentes)
        : BusSlave(nm, base, 0x800), n_(n_fuentes), mapa_(n_fuentes, 0u), fuente_(n_fuentes, false) {
        reset();
    }
    void reset() {
        for (uint32_t& m : mapa_) m = 0;
        date_ = 0x2203110u;
        recalcula();
    }
    void fuente(unsigned n, bool nivel) {
        if (n >= n_ || fuente_[n] == nivel) return;
        fuente_[n] = nivel;
        recalcula();
    }
    bool nivel_fuente(unsigned n) const { return n < n_ && fuente_[n]; }
    unsigned linea_de(unsigned f) const { return f < n_ ? mapa_[f] : 0u; }
    uint32_t lineas() const { return lineas_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 4u * n_) return mapa_[off / 4];
        if (off >= 0x134 && off <= 0x13C) {
            const unsigned b = (off - 0x134) / 4 * 32;
            uint32_t v = 0;
            for (unsigned i = 0; i < 32 && b + i < n_; ++i) v |= uint32_t(fuente_[b + i]) << i;
            return v;
        }
        if (off == 0x7FC) return date_;
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off < 4u * n_) { mapa_[off / 4] = v & 0x1Fu; recalcula(); return; }
        if (off == 0x7FC) date_ = v & 0x0FFFFFFFu;
    }

private:
    unsigned n_;
    std::vector<uint32_t> mapa_;
    std::vector<bool> fuente_;
    uint32_t lineas_ = 0, date_ = 0;
    void recalcula() {
        uint32_t l = 0;
        for (unsigned i = 0; i < n_; ++i)
            if (fuente_[i] && mapa_[i]) l |= 1u << mapa_[i];
        l &= ~CLINT_ESP_IDS;
        lineas_ = l;
        if (lineas_cambian) lineas_cambian(l);
    }
};

// --- El INTPRI y el PLIC, y con ellos el rv_irq_if del núcleo HP -----------------------
// Dos bancos con los mismos campos: el de M y el de U. El de M se ve en dos
// sitios, con el mismo contenido: el INTPRI del TRM (0x600C_5000: ENABLE 0x00,
// TYPE 0x04, EIP_STATUS 0x08, PRI_n 0x0C + 4n, THRESH 0x8C, CLEAR 0xA8) y el
// PLIC MX de ESP-IDF (0x2000_1000: ENABLE 0x00, TYPE 0x04, CLEAR 0x08,
// EIP 0x0C, PRI_n 0x10 + 4n, THRESH 0x90, CLAIM 0x94, y CONF en 0x3FC), que es
// el que usa ESP-IDF (SOC_INT_PLIC_SUPPORTED, riscv/interrupt_plic.c). El de U,
// solo en el PLIC UX (0x2000_1400, la misma disposición, CONF en 0x7FC)
// [plic_reg.h de ESP-IDF; que el INTPRI y el PLIC MX sean el mismo banco es
// de modelo: el TRM solo documenta el INTPRI y ESP-IDF solo usa el PLIC].
// Una línea está pendiente en un banco si está habilitada en él, tiene nivel
// (o el flanco enganchado) y su prioridad es >= el umbral del banco; el
// núcleo ve la unión, la filtra con mie y la lleva a M o a U con mideleg.
class IntPriEsp : public BusSlave, public rv_irq_if {
public:
    std::function<void()> aviso;                          // al núcleo
    std::function<void(unsigned, bool)> desde_cpu;        // las fuentes 22-25
    ClintEsp* clint = nullptr;

    IntPriEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() {
        for (Banco& b : b_) b = Banco();
        for (unsigned i = 0; i < 4; ++i) { cpu_[i] = 0; if (desde_cpu) desde_cpu(i, false); }
        date_ = 0x2201090u;
        conf_[0] = conf_[1] = 0;
        recalcula();
    }
    // Las líneas de la matriz
    void lineas(uint32_t l) {
        const uint32_t sube = l & ~linea_;
        linea_ = l;
        for (Banco& b : b_) b.pend = (b.pend & b.tipo) | (sube & b.tipo & b.ena) | (l & ~b.tipo);
        recalcula();
    }
    uint32_t eip(unsigned k = 0) const {
        const Banco& b = b_[k];
        uint32_t r = 0;
        for (unsigned i = 1; i < 32; ++i)
            if (!((CLINT_ESP_IDS >> i) & 1u) && (((b.pend & b.ena) >> i) & 1u) && b.prio[i] >= b.umbral) r |= 1u << i;
        return r;
    }
    uint64_t reclamaciones() const { return n_recl_; }

    // El PLIC en la región de la CPU: `off` desde 0x2000_1000 (0-0x7FF)
    void acceso_plic(tlm::tlm_generic_payload& gp, uint32_t off) {
        const unsigned n = gp.get_data_length();
        unsigned char* p = gp.get_data_ptr();
        const unsigned k = off >= 0x400 ? 1u : 0u, r0 = (off & 0x3FFu) & ~3u, sh = (off & 3u) * 8u;
        if (gp.is_write()) {
            uint32_t v = 0;
            for (unsigned i = 0; i < n && i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
            v <<= sh;
            switch (r0) {
            case 0x00: escribe_ena(k, v); break;
            case 0x04: escribe_tipo(k, v); break;
            case 0x08: escribe_clear(k, v); break;
            case 0x90: b_[k].umbral = v & 0xFFu; recalcula(); break;
            case 0x94: b_[k].claim = v; break;
            case 0x3FC: conf_[k] = v; break;
            default:
                if (r0 >= 0x10 && r0 < 0x90) escribe_prio(k, (r0 - 0x10) / 4, v);
                break;
            }
        } else {
            uint32_t r = 0;
            switch (r0) {
            case 0x00: r = b_[k].ena; break;
            case 0x04: r = b_[k].tipo; break;
            case 0x08: r = b_[k].clear; break;
            case 0x0C: r = eip(k); break;
            case 0x90: r = b_[k].umbral; break;
            case 0x94: r = b_[k].claim; break;
            case 0x3FC: r = conf_[k]; break;
            default:
                if (r0 >= 0x10 && r0 < 0x90) r = b_[k].prio[(r0 - 0x10) / 4];
                break;
            }
            for (unsigned i = 0; i < n; ++i) p[i] = uint8_t((r >> sh) >> (8 * i));
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // --- rv_irq_if ---------------------------------------------------------------
    uint32_t pendientes() override { return eip(0) | eip(1) | (clint ? clint->pendientes() : 0u); }
    bool elige(uint32_t c, unsigned& id) override {
        const uint32_t ext = c & ~CLINT_ESP_IDS;
        if (ext) {
            unsigned mejor = 32, pmax = 0;
            for (unsigned i = 1; i < 32; ++i) {
                if (!((ext >> i) & 1u)) continue;
                const unsigned pr = ((b_[0].ena >> i) & 1u) ? b_[0].prio[i] : b_[1].prio[i];
                if (mejor == 32 || pr > pmax) { pmax = pr; mejor = i; }
            }
            id = mejor;
            return true;
        }
        for (unsigned i : {3u, 7u, 0u, 4u}) if ((c >> i) & 1u) { id = i; return true; }
        return false;
    }
    void reclamada(unsigned id) override {
        if (id < 32) for (Banco& b : b_) b.reclamada |= 1u << id;
        ++n_recl_;
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return b_[0].ena;
        case 0x04: return b_[0].tipo;
        case 0x08: return eip(0);
        case 0x8C: return b_[0].umbral;
        case 0x90: case 0x94: case 0x98: case 0x9C: return cpu_[(off - 0x90) / 4];
        case 0xA0: return date_;
        case 0xA8: return b_[0].clear;
        default: break;
        }
        if (off >= 0x0C && off < 0x0C + 4 * 32) return b_[0].prio[(off - 0x0C) / 4];
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x00: escribe_ena(0, v); return;
        case 0x04: escribe_tipo(0, v); return;
        case 0x8C: b_[0].umbral = v & 0xFFu; recalcula(); return;
        case 0x90: case 0x94: case 0x98: case 0x9C: {
            const unsigned i = (off - 0x90) / 4;
            cpu_[i] = v & 1u;
            if (desde_cpu) desde_cpu(i, cpu_[i] != 0);
            return;
        }
        case 0xA0: date_ = v & 0x0FFFFFFFu; return;
        case 0xA8: escribe_clear(0, v); return;
        default: break;
        }
        if (off >= 0x0C && off < 0x0C + 4 * 32) escribe_prio(0, (off - 0x0C) / 4, v);
    }

private:
    struct Banco {
        uint32_t ena = 0, tipo = 0, clear = 0, umbral = 0, claim = 0, pend = 0, reclamada = 0;
        uint32_t prio[32] = {};
    };
    static constexpr uint32_t RES = ~CLINT_ESP_IDS & ~1u;
    Banco b_[2];
    uint32_t cpu_[4] = {};
    uint32_t linea_ = 0, date_ = 0, conf_[2] = {};
    uint64_t n_recl_ = 0;
    void recalcula() { if (aviso) aviso(); }
    void escribe_ena(unsigned k, uint32_t v) { b_[k].ena = v & RES; recalcula(); }
    void escribe_tipo(unsigned k, uint32_t v) { b_[k].tipo = v & RES; recalcula(); }
    void escribe_prio(unsigned k, unsigned i, uint32_t v) {
        if ((RES >> i) & 1u) b_[k].prio[i] = v & 0xFu;
        recalcula();
    }
    void escribe_clear(unsigned k, uint32_t v) {
        Banco& b = b_[k];
        const uint32_t sube = v & ~b.clear;
        const uint32_t borra = sube & b.tipo & (b.reclamada | ~b.ena);
        b.pend &= ~borra;
        b.reclamada &= ~borra;
        b.clear = v;
        recalcula();
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP_INTC_ESP_V2_H
