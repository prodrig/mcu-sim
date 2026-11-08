// =============================================================================
// esp_cache_mmu.h — La caché de la Flash y su MMU (EXTMEM), de los ESP32
//
// E4 de doc/analisis_esp32_riscv.md, §4. El C3 [TRM-C3 §3.3.3, extmem_reg.h]:
//
//   las ventanas  0x4200_0000 (bus I) y 0x3C00_0000 (bus D), de 8 MB cada
//                 una, pasan por la MISMA MMU: 128 entradas de 64 KB en
//                 0x600C_5000 (DR_REG_MMU_TABLE). Cada entrada: bits 7-0, la
//                 página de la Flash; bit 8, inválida. La caché es de solo
//                 lectura: escribir en las ventanas es un error.
//   la caché      16 KB, 8 vías, líneas de 32 bytes (64 conjuntos), LRU. Un
//                 acierto no cuesta nada más que la instrucción; un fallo
//                 cuesta lo que tarda el SPI0 en leer la línea
//                 (SpiMemEsp::t_lectura), con su modo y su reloj.
//   los registros EXTMEM: ICACHE_CTRL.ENABLE; ICACHE_CTRL1, SHUT_IBUS/DBUS
//                 (a 1 tras el reset: el bus cortado); las operaciones que
//                 la ROM y ESP-IDF lanzan y sondean -invalidar (SYNC_CTRL),
//                 precargar (PRELOAD_CTRL), autocargar (AUTOLOAD_CTRL), fijar
//                 y soltar líneas (LOCK_CTRL), congelar (FREEZE)- acaban en el
//                 acto y dejan su bit DONE; CACHE_STATE dice «en reposo». Los
//                 contadores de aciertos y fallos (IBUS/DBUS_ACS_*) cuentan.
//                 El resto guarda su valor de reset (tabla de ESP-IDF).
//
// Lo que NO se modela: el bloqueo de líneas (se guarda y no fija nada), la
// precarga (no trae líneas: acaba sin más), el cifrado de la Flash, la
// protección por PMS de las ventanas (IBUS/DBUS_PMS_TBL) y las
// interrupciones de acceso ilegal. Un acceso con el bus cortado, con la caché
// apagada o a una página inválida es un error de bus y lo avisa una vez.
// =============================================================================
#ifndef STM32_SOC_ESP_ESP_CACHE_MMU_H
#define STM32_SOC_ESP_ESP_CACHE_MMU_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "regs_esp.h"
#include "../../periph/esp/spi_mem.h"

namespace stm32 {

struct CacheMmuCaps {
    uint32_t base_i, base_d, ventana;   // 0x4200_0000, 0x3C00_0000, 8 MB
    unsigned entradas;                  // 128
    uint32_t pagina;                    // 64 KB
    unsigned vias, linea, tam;          // 8, 32, 16 KB
};
inline constexpr CacheMmuCaps CACHE_C3{0x42000000u, 0x3C000000u, 8u << 20, 128, 64u << 10, 8, 32, 16u << 10};

class CacheMmuEsp : public BloqueEsp {
public:
    // EXTMEM en `base` y la tabla de la MMU en base + 0x1000
    enum : uint32_t {
        ICACHE_CTRL = 0x000, ICACHE_CTRL1 = 0x004, LOCK_CTRL = 0x01C, SYNC_CTRL = 0x028,
        PRELOAD_CTRL = 0x034, AUTOLOAD_CTRL = 0x040, ACS_CNT_CLR = 0x064,
        IBUS_MISS = 0x068, IBUS_CNT = 0x06C, DBUS_MISS = 0x070, DBUS_CNT = 0x074,
        CACHE_STATE = 0x0B0, FREEZE = 0x0CC, MMU = 0x1000,
    };
    template <size_t N>
    CacheMmuEsp(sc_core::sc_module_name nm, uint32_t base, const RegEsp (&t)[N],
                const CacheMmuCaps& caps, SpiMemEsp& spi0)
        : BloqueEsp(nm, base, 0x2000, t), c_(caps), spi0_(spi0),
          conjuntos_(caps.tam / caps.linea / caps.vias), tag_(conjuntos_ * caps.vias),
          edad_(conjuntos_ * caps.vias), mmu_(caps.entradas) {
        reset();
    }

    void reset() {
        reset_regs();
        for (uint32_t& e : mmu_) e = 0x100u;              // inválidas
        invalida();
        aciertos_ = fallos_ = 0;
    }
    bool es_ventana(uint32_t a) const {
        return (a >= c_.base_i && a - c_.base_i < c_.ventana) || (a >= c_.base_d && a - c_.base_d < c_.ventana);
    }
    uint32_t entrada(unsigned i) const { return i < mmu_.size() ? mmu_[i] : 0x100u; }
    void pon_entrada(unsigned i, uint32_t v) { if (i < mmu_.size()) mmu_[i] = v & 0x1FFu; }
    uint64_t aciertos() const { return aciertos_; }
    // Vaciar la caché (la Flash ha cambiado por detrás: el `load` de GDB)
    void invalida_lineas() { invalida(); }
    // Una dirección de una ventana, a la Flash, por la MMU (false: página
    // inválida o fuera de las ventanas)
    bool traduce(uint32_t a, uint32_t& off) const {
        if (!es_ventana(a)) return false;
        const uint32_t v = a - ((a >= c_.base_i && a - c_.base_i < c_.ventana) ? c_.base_i : c_.base_d);
        const uint32_t e = mmu_[(v / c_.pagina) % mmu_.size()];
        if (e & 0x100u) return false;
        off = (e & 0xFFu) * c_.pagina + v % c_.pagina;
        return true;
    }
    uint64_t fallos() const { return fallos_; }

    // Una lectura por una ventana. false: error de bus (y `por_que`).
    bool lee_ventana(uint32_t a, uint8_t* p, unsigned n, bool fetch, sc_core::sc_time& d,
                     std::string& por_que) {
        const bool ibus = a >= c_.base_i && a - c_.base_i < c_.ventana;
        const uint32_t v = a - (ibus ? c_.base_i : c_.base_d);
        if (!(r(ICACHE_CTRL) & 1u)) { por_que = "la cache esta apagada (EXTMEM_ICACHE_CTRL)"; return false; }
        if (r(ICACHE_CTRL1) & (ibus ? 1u : 2u)) {
            por_que = ibus ? "el bus I de la cache esta cortado (SHUT_IBUS)" : "el bus D de la cache esta cortado (SHUT_DBUS)";
            return false;
        }
        (void)fetch;
        for (unsigned k = 0; k < n;) {
            const uint32_t va = v + k;
            const uint32_t e = mmu_[(va / c_.pagina) % mmu_.size()];
            if (e & 0x100u) { por_que = "pagina de la MMU invalida"; return false; }
            const uint32_t fis = (e & 0xFFu) * c_.pagina + va % c_.pagina;
            // La línea entera, por la caché
            const uint32_t lin = fis / c_.linea;
            if (!busca(lin)) {
                d += spi0_.t_lectura(c_.linea);
                ++fallos_;
                pon(ibus ? IBUS_MISS : DBUS_MISS, r(ibus ? IBUS_MISS : DBUS_MISS) + 1);
            } else {
                ++aciertos_;
            }
            pon(ibus ? IBUS_CNT : DBUS_CNT, r(ibus ? IBUS_CNT : DBUS_CNT) + 1);
            const unsigned resto = std::min<unsigned>(n - k, c_.linea - fis % c_.linea);
            if (spi0_.flash) spi0_.flash->lee(fis % spi0_.flash->tam(), p + k, resto,
                                              sc_core::sc_time_stamp().to_seconds());
            else std::memset(p + k, 0xFF, resto);
            k += resto;
        }
        return true;
    }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off >= MMU) {
            const unsigned i = (off - MMU) / 4;
            v = i < mmu_.size() ? mmu_[i] : 0u;
            return true;
        }
        if (off == CACHE_STATE) { v = 1u; return true; }          // en reposo
        return false;
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off >= MMU) {
            const unsigned i = (off - MMU) / 4;
            if (i < mmu_.size()) mmu_[i] = ((mmu_[i] & ~m) | (v & m)) & 0x1FFu;
            return true;
        }
        switch (off) {
        case SYNC_CTRL:                                          // bit 0 invalidar, bit 1 hecho
            guarda(off, v, m);
            if (r(off) & 1u) { invalida(); pon(off, (r(off) & ~1u) | 2u); }
            return true;
        case PRELOAD_CTRL:                                       // bit 0 pedir, bit 1 hecho
            guarda(off, v, m);
            if (r(off) & 1u) pon(off, (r(off) & ~1u) | 2u);
            return true;
        case AUTOLOAD_CTRL:                                      // bit 2 pedir, bit 3 hecho
            guarda(off, v, m);
            if (r(off) & 4u) pon(off, (r(off) & ~4u) | 8u);
            return true;
        case LOCK_CTRL:                                          // bits 0/1 fijar/soltar, 2 hecho
            guarda(off, v, m);
            if (r(off) & 3u) pon(off, (r(off) & ~3u) | 4u);
            return true;
        case FREEZE:                                             // bit 0 congelar, bit 2 hecho
            guarda(off, v, m);
            pon(off, (r(off) & 3u) | ((r(off) & 1u) ? 4u : 0u));
            return true;
        case ACS_CNT_CLR:
            if (v & m & 1u) { pon(IBUS_MISS, 0); pon(IBUS_CNT, 0); }
            if (v & m & 2u) { pon(DBUS_MISS, 0); pon(DBUS_CNT, 0); }
            return true;
        default: return false;
        }
    }

private:
    CacheMmuCaps c_;
    SpiMemEsp& spi0_;
    unsigned conjuntos_;
    std::vector<uint32_t> tag_;          // línea + 1 (0: vacía)
    std::vector<uint64_t> edad_;
    std::vector<uint32_t> mmu_;
    uint64_t reloj_ = 0, aciertos_ = 0, fallos_ = 0;

    void invalida() { std::fill(tag_.begin(), tag_.end(), 0u); std::fill(edad_.begin(), edad_.end(), 0u); }
    // ¿Está la línea? Si no, la trae (LRU) y devuelve false
    bool busca(uint32_t lin) {
        const unsigned s = lin % conjuntos_;
        const unsigned b = s * c_.vias;
        ++reloj_;
        unsigned victima = b;
        for (unsigned w = b; w < b + c_.vias; ++w) {
            if (tag_[w] == lin + 1) { edad_[w] = reloj_; return true; }
            if (edad_[w] < edad_[victima]) victima = w;
        }
        tag_[victima] = lin + 1;
        edad_[victima] = reloj_;
        return false;
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP_ESP_CACHE_MMU_H
