// =============================================================================
// gdma_esp.h — La GDMA de los ESP32-C3: los canales de salida (memoria ->
//              periférico) y de entrada (periférico -> memoria), con sus
//              listas de descriptores
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §2; gdma_reg.h de ESP-IDF]. Tres
// canales; cada uno, con OUT_PERI_SEL, sirve a un periférico (0 SPI2, 3 AES,
// 4 SHA, 5 ADC...). El descriptor: {DW0: size[11:0], length[23:12],
// suc_eof[30], owner[31]; el búfer; el siguiente}. OUT_LINK.ADDR (20 bits,
// sobre 0x3FC0_0000) y OUTLINK_START lo arrancan. El periférico pide bytes
// (`saca`) y la GDMA los lee de la memoria, descriptor a descriptor; al
// acabar uno con suc_eof, OUT_EOF y OUT_TOTAL_EOF; con cada uno, OUT_DONE
// (bits 3, 4 y 8 de INT_RAW_CHn, a las fuentes 44-46). Deja el owner a 0 si
// OUT_AUTO_WRBACK (CONF0 bit 2).
// LA ENTRADA (E8, para el I2S y el UHCI): IN_PERI_SEL (0xA0), IN_LINK (0x80:
// ADDR, STOP en el bit 21, START en el 22) e IN_CONF1.IN_CHECK_OWNER (bit
// 12). El periférico da bytes (`mete`) y la GDMA los escribe en el búfer del
// descriptor; al llenarlo, o al acabar la trama el periférico (EOF), le pone
// length, suc_eof si es el final y el owner a 0, IN_DONE (bit 0) y, con EOF,
// IN_SUC_EOF (bit 1) e IN_SUC_EOF_DES_ADDR (0x88). Sin descriptor siguiente
// (o, con IN_CHECK_OWNER, con el owner a 0), IN_DSCR_EMPTY (bit 7) y para.
// Los periféricos: 0 SPI2, 2 UHCI0, 3 I2S, 6 AES, 7 SHA, 8 ADC
// [SOC_GDMA_TRIG_PERIPH_* de ESP-IDF].
// LO QUE NO SE MODELA: las prioridades y el ancho de banda (lo que tarda lo
// pone el periférico), la memoria externa, OUT_PUSH e IN_POP (las FIFO a
// mano) e IN_ERR_EOF.
// =============================================================================
#ifndef STM32_PERIPH_ESP_GDMA_ESP_H
#define STM32_PERIPH_ESP_GDMA_ESP_H

#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class GdmaEsp : public BusSlave {
public:
    static constexpr unsigned N = 3;
    std::function<void(unsigned, bool)> irq;                  // canal, nivel
    std::function<bool(uint32_t, uint8_t&)> lee;              // la memoria
    std::function<void(uint32_t, uint32_t)> escribe32;
    std::function<bool(uint32_t, uint8_t)> escribe8;          // la memoria, para la entrada

    explicit GdmaEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() {
        for (auto& r : r_) r = 0;
        for (unsigned c = 0; c < N; ++c) {
            r_[(0x100 + 0xC0 * c) / 4] = 0x3F; r_[(0xA0 + 0xC0 * c) / 4] = 0x3F;
            act_[c] = ent_[c] = false; irq_[c] = false;
        }
        evalua();
    }
    // Lo que pide un periférico: hasta `n` bytes de su canal. Devuelve cuántos.
    unsigned saca(unsigned peri, uint8_t* p, unsigned n) {
        for (unsigned c = 0; c < N; ++c)
            if ((r_[(0x100 + 0xC0 * c) / 4] & 0x3Fu) == peri && act_[c]) return saca_canal(c, p, n);
        return 0;
    }
    // Lo que da un periférico a su canal de entrada: `n` bytes y, si `eof`,
    // el final de la trama tras ellos. Devuelve cuántos ha podido escribir.
    unsigned mete(unsigned peri, const uint8_t* p, unsigned n, bool eof) {
        for (unsigned c = 0; c < N; ++c)
            if ((r_[(0xA0 + 0xC0 * c) / 4] & 0x3Fu) == peri && ent_[c]) return mete_canal(c, p, n, eof);
        return 0;
    }
    // ¿Acabó la última `saca` de `peri` un descriptor con suc_eof? (se borra al leerlo)
    bool eof_salida(unsigned peri) {
        for (unsigned c = 0; c < N; ++c)
            if ((r_[(0x100 + 0xC0 * c) / 4] & 0x3Fu) == peri && eof_sal_[c]) { eof_sal_[c] = false; return true; }
        return false;
    }
    bool salida_activa(unsigned peri) const {
        for (unsigned c = 0; c < N; ++c)
            if ((r_[(0x100 + 0xC0 * c) / 4] & 0x3Fu) == peri && act_[c]) return true;
        return false;
    }
    // Para despertar al periférico cuando se arranca un canal de salida
    std::function<void(unsigned peri)> al_arrancar;
    bool entrada_activa(unsigned peri) const {
        for (unsigned c = 0; c < N; ++c)
            if ((r_[(0xA0 + 0xC0 * c) / 4] & 0x3Fu) == peri && ent_[c]) return true;
        return false;
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x30) {
            const unsigned c = off / 0x10, k = off % 0x10;
            const uint32_t raw = r_[(c * 0x10) / 4], ena = r_[(c * 0x10 + 8) / 4];
            return k == 0 ? raw : k == 4 ? (raw & ena) : k == 8 ? ena : 0;
        }
        if (off == 0x6C) return 0x2008250u;
        return off < 0x400 ? r_[off / 4] : 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off < 0x30) {
            const unsigned c = off / 0x10, k = off % 0x10;
            if (k == 8) r_[(c * 0x10 + 8) / 4] = v;
            if (k == 0xC) r_[(c * 0x10) / 4] &= ~v;
            evalua();
            return;
        }
        if (off < 0x400) r_[off / 4] = v;
        for (unsigned c = 0; c < N; ++c)
            if (off == 0xE0 + 0xC0 * c) {
                if (v & (1u << 21)) {
                    act_[c] = true; dscr_[c] = 0x3FC00000u | (v & 0xFFFFFu); pos_[c] = 0; eof_sal_[c] = false;
                    if (al_arrancar) al_arrancar(r_[(0x100 + 0xC0 * c) / 4] & 0x3Fu);
                }
                if (v & (1u << 20)) act_[c] = false;
            } else if (off == 0x80 + 0xC0 * c) {
                if (v & (1u << 22)) { ent_[c] = true; dent_[c] = 0x3FC00000u | (v & 0xFFFFFu); pent_[c] = 0; }
                if (v & (1u << 21)) ent_[c] = false;
            }
    }

private:
    uint32_t r_[256] = {};
    bool act_[N] = {}, irq_[N] = {}, ent_[N] = {}, eof_sal_[N] = {};
    uint32_t dscr_[N] = {}, pos_[N] = {}, dent_[N] = {}, pent_[N] = {};

    uint32_t lee32(uint32_t a) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) { uint8_t b = 0; if (lee) lee(a + i, b); v |= uint32_t(b) << (8 * i); }
        return v;
    }
    void evalua() {
        for (unsigned c = 0; c < N; ++c) {
            const bool n = (r_[(c * 0x10) / 4] & r_[(c * 0x10 + 8) / 4]) != 0;
            if (n != irq_[c]) { irq_[c] = n; if (irq) irq(c, n); }
        }
    }
    unsigned saca_canal(unsigned c, uint8_t* p, unsigned n) {
        unsigned k = 0;
        while (k < n && act_[c]) {
            const uint32_t d = dscr_[c];
            const uint32_t w0 = lee32(d), buf = lee32(d + 4), sig = lee32(d + 8);
            const uint32_t largo = (w0 >> 12) & 0xFFFu;
            while (k < n && pos_[c] < largo) { uint8_t b = 0; if (lee) lee(buf + pos_[c], b); p[k++] = b; ++pos_[c]; }
            if (pos_[c] < largo) break;
            // Acabado este descriptor
            if ((r_[(0xD0 + 0xC0 * c) / 4] >> 2) & 1u) { if (escribe32) escribe32(d, w0 & ~(1u << 31)); }
            r_[(0xE8 + 0xC0 * c) / 4] = d;                      // OUT_EOF_DES_ADDR (el último)
            r_[(c * 0x10) / 4] |= 1u << 3;                      // OUT_DONE
            pos_[c] = 0;
            if ((w0 >> 30) & 1u) {
                r_[(c * 0x10) / 4] |= (1u << 4) | (1u << 8);    // OUT_EOF, OUT_TOTAL_EOF
                act_[c] = false;
                eof_sal_[c] = true;
            } else if (!sig) {
                act_[c] = false;
            } else {
                dscr_[c] = sig;
            }
            evalua();
        }
        return k;
    }
    unsigned mete_canal(unsigned c, const uint8_t* p, unsigned n, bool eof) {
        unsigned k = 0;
        for (;;) {
            if (!ent_[c]) return k;
            const uint32_t d = dent_[c];
            const uint32_t w0 = lee32(d), buf = lee32(d + 4), sig = lee32(d + 8);
            if (pent_[c] == 0 && ((r_[(0x74 + 0xC0 * c) / 4] >> 12) & 1u) && !(w0 >> 31)) {
                r_[(c * 0x10) / 4] |= 1u << 7;                  // IN_DSCR_EMPTY: no es suyo
                ent_[c] = false;
                evalua();
                return k;
            }
            const uint32_t tam = w0 & 0xFFFu;
            while (k < n && pent_[c] < tam) { if (escribe8) escribe8(buf + pent_[c], p[k]); ++k; ++pent_[c]; }
            const bool fin = eof && k == n;
            if (pent_[c] < tam && !fin) return k;               // cabe más
            // Cerrado: length, suc_eof y el owner a 0
            if (escribe32)
                escribe32(d, tam | (pent_[c] << 12) | (fin ? (1u << 30) : 0u));
            r_[(c * 0x10) / 4] |= 1u << 0;                      // IN_DONE
            if (fin) {
                r_[(c * 0x10) / 4] |= 1u << 1;                  // IN_SUC_EOF
                r_[(0x88 + 0xC0 * c) / 4] = d;
            }
            pent_[c] = 0;
            if (!sig) {
                r_[(c * 0x10) / 4] |= 1u << 7;                  // IN_DSCR_EMPTY
                ent_[c] = false;
            } else {
                dent_[c] = sig;
            }
            evalua();
            if (fin || k == n) return k;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_GDMA_ESP_H
