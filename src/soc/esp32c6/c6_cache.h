// =============================================================================
// c6_cache.h — La caché de la Flash del ESP32-C6 y su MMU
//
// E9 de doc/analisis_esp32_riscv.md [TRM-C6 §5.3.3, §6; extmem_reg.h,
// spi_mem_reg.h, ext_mem_defs.h y mmu_ll.h de ESP-IDF]. Lo que cambia del C3
// (soc/esp/esp_cache_mmu.h):
//
//   la ventana   UNA, 0x4200_0000-0x42FF_FFFF, para los buses I y D: el C6 no
//                tiene bus D aparte. 256 entradas de 64 KB (16 MB; la página
//                puede ser de 32 o 16 KB con SPI_MEM_MMU_PAGE_SIZE, bits 4:3
//                de MMU_POWER_CTRL del SPI0).
//   la MMU       no está en EXTMEM: está detrás del SPI0, en
//                SPI_MEM_MMU_ITEM_INDEX (0x380) y SPI_MEM_MMU_ITEM_CONTENT
//                (0x37C): se escribe el índice y luego se lee o escribe la
//                entrada. Cada entrada: bits 8-0, la página de la Flash; bit
//                9, válida (SOC_MMU_VALID, al revés que el C3); bit 10,
//                «sensible» (la página va cifrada). Tras el reset, todas
//                inválidas.
//   la caché     32 KB, 4 vías, líneas de 32 bytes [TRM-C6 §6.3]; un fallo
//                cuesta lo que tarda el SPI0 en leer la línea, como en el C3.
//   los registros EXTMEM (0x600C_8000), con la L1 de ESP-IDF: L1_CACHE_CTRL
//                (SHUT_IBUS 0, SHUT_DBUS 1: el bus cortado), y las
//                operaciones que acaban en el acto y dejan su DONE:
//                SYNC_CTRL (INVALIDATE/CLEAN/WRITEBACK, DONE 4; de reset,
//                INVALIDATE_ENA a 1 y la caché vacía), PRELOAD_CTRL (ENA 0,
//                DONE 1), AUTOLOAD_CTRL (DONE 1), LOCK_CTRL (LOCK/UNLOCK,
//                DONE 2) y FREEZE_CTRL (EN 16, DONE 18). Los contadores de
//                aciertos y fallos del bus I y del D (0x1BC-0x1D0) cuentan con
//                ACS_CNT_CTRL.
//
// Lo que no se modela, como en el C3: el bloqueo de líneas, la precarga (no
// trae nada), la autocarga y las interrupciones de acceso ilegal. El cifrado
// (las páginas sensibles) lo descifra el SoC con `descifra`.
// =============================================================================
#ifndef STM32_SOC_ESP32C6_C6_CACHE_H
#define STM32_SOC_ESP32C6_C6_CACHE_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "c6_sistema.h"
#include "../../periph/esp/spi_mem.h"

namespace stm32 {

// El SPI0 del C6: el SPI_MEM de siempre con la MMU dentro
class Spi0C6 : public SpiMemEsp {
public:
    static constexpr unsigned N_MMU = 256;
    enum : uint32_t { MMU_CONTENT = 0x37C, MMU_INDEX = 0x380, MMU_POWER = 0x384 };
    std::function<void()> mmu_cambia;       // a la caché: las líneas ya no valen

    Spi0C6(sc_core::sc_module_name nm, uint32_t base) : SpiMemEsp(nm, base, c6::REGS_SPI_MEM), mmu_(N_MMU, 0u) {}
    void reset() { SpiMemEsp::reset(); std::fill(mmu_.begin(), mmu_.end(), 0u); idx_ = 0; }
    uint32_t entrada(unsigned i) const { return i < N_MMU ? mmu_[i] : 0u; }
    void pon_entrada(unsigned i, uint32_t v) { if (i < N_MMU) mmu_[i] = v & 0x7FFu; }
    uint32_t pagina() const {
        const unsigned c = (r(MMU_POWER) >> 3) & 3u;
        return c == 1 ? (32u << 10) : c == 2 ? (16u << 10) : (64u << 10);
    }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off == MMU_INDEX) { v = idx_; return true; }
        if (off == MMU_CONTENT) { v = idx_ < N_MMU ? mmu_[idx_] : 0u; return true; }
        return SpiMemEsp::lee(off, v);
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off == MMU_INDEX) { idx_ = ((idx_ & ~m) | (v & m)) & 0x3FFu; return true; }
        if (off == MMU_CONTENT) {
            if (idx_ < N_MMU) mmu_[idx_] = ((mmu_[idx_] & ~m) | (v & m)) & 0x7FFu;
            if (mmu_cambia) mmu_cambia();
            return true;
        }
        return SpiMemEsp::escribe(off, v, m);
    }

private:
    std::vector<uint32_t> mmu_;
    uint32_t idx_ = 0;
};

class CacheC6 : public BloqueEsp {
public:
    static constexpr uint32_t BASE = 0x42000000u, VENTANA = 16u << 20;
    static constexpr unsigned VIAS = 4, LINEA = 32, TAM = 32u << 10;
    enum : uint32_t {
        CTRL = 0x004, FREEZE = 0x02C, LOCK_CTRL = 0x088, SYNC_CTRL = 0x098, PRELOAD_CTRL = 0x0D8,
        AUTOLOAD_CTRL = 0x134, ACS_CNT_CTRL = 0x178, IBUS_HIT = 0x1BC, IBUS_MISS = 0x1C0,
        DBUS_HIT = 0x1CC, DBUS_MISS = 0x1D0,
    };
    // Lo que se lee de la Flash física `fis` (`n` bytes en `p`), en claro si
    // la página es sensible y el SoC tiene el descifrado encendido
    std::function<void(uint32_t fis, uint8_t* p, unsigned n)> descifra;

    CacheC6(sc_core::sc_module_name nm, uint32_t base, Spi0C6& spi0)
        : BloqueEsp(nm, base, 0x1000, c6::REGS_EXTMEM), spi0_(spi0), conjuntos_(TAM / LINEA / VIAS),
          tag_(conjuntos_ * VIAS), edad_(conjuntos_ * VIAS) {
        spi0_.mmu_cambia = [this] { invalida(); };
        reset();
    }
    void reset() { reset_regs(); invalida(); aciertos_ = fallos_ = 0; }
    static bool es_ventana(uint32_t a) { return a >= BASE && a - BASE < VENTANA; }
    uint64_t aciertos() const { return aciertos_; }
    uint64_t fallos() const { return fallos_; }
    void invalida_lineas() { invalida(); }
    // Una dirección de la ventana, a la Flash, por la MMU (false: inválida)
    bool traduce(uint32_t a, uint32_t& off, bool* sensible = nullptr) const {
        if (!es_ventana(a)) return false;
        const uint32_t pg = spi0_.pagina(), v = a - BASE;
        const uint32_t e = spi0_.entrada((v / pg) % Spi0C6::N_MMU);
        if (!(e & (1u << 9))) return false;
        off = (e & 0x1FFu) * pg + v % pg;
        if (sensible) *sensible = (e >> 10) & 1u;
        return true;
    }

    // Una lectura por la ventana. false: error de bus (y `por_que`).
    bool lee_ventana(uint32_t a, uint8_t* p, unsigned n, bool fetch, sc_core::sc_time& d, std::string& por_que) {
        if (r(CTRL) & (fetch ? 1u : 2u)) {
            por_que = fetch ? "el bus I de la cache esta cortado (L1_CACHE_SHUT_IBUS)"
                            : "el bus D de la cache esta cortado (L1_CACHE_SHUT_DBUS)";
            return false;
        }
        for (unsigned k = 0; k < n;) {
            uint32_t fis = 0;
            bool sens = false;
            if (!traduce(a + k, fis, &sens)) { por_que = "pagina de la MMU invalida"; return false; }
            const uint32_t lin = fis / LINEA;
            const bool cuenta = (r(ACS_CNT_CTRL) >> (fetch ? 4 : 5)) & 1u;
            if (!busca(lin)) {
                d += spi0_.t_lectura(LINEA);
                ++fallos_;
                if (cuenta) pon(fetch ? IBUS_MISS : DBUS_MISS, r(fetch ? IBUS_MISS : DBUS_MISS) + 1);
            } else {
                ++aciertos_;
                if (cuenta) pon(fetch ? IBUS_HIT : DBUS_HIT, r(fetch ? IBUS_HIT : DBUS_HIT) + 1);
            }
            const unsigned resto = std::min<unsigned>(n - k, LINEA - fis % LINEA);
            if (spi0_.flash) spi0_.flash->lee(fis % spi0_.flash->tam(), p + k, resto,
                                              sc_core::sc_time_stamp().to_seconds());
            else std::memset(p + k, 0xFF, resto);
            if (sens && descifra) descifra(fis, p + k, resto);
            k += resto;
        }
        return true;
    }

protected:
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        switch (off) {
        case SYNC_CTRL:                                    // 0-3 pedir, 4 hecho
            guarda(off, v, m);
            if (r(off) & 1u) invalida();
            pon(off, (r(off) & ~0xFu) | 0x10u);
            return true;
        case PRELOAD_CTRL:                                 // 0 pedir, 1 hecho
            guarda(off, v, m);
            pon(off, (r(off) & ~1u) | 2u);
            return true;
        case LOCK_CTRL:                                    // 0/1 fijar/soltar, 2 hecho
            guarda(off, v, m);
            pon(off, (r(off) & ~3u) | 4u);
            return true;
        case FREEZE:                                       // 16 congelar, 18 hecho
            guarda(off, v, m);
            pon(off, (r(off) & ~(1u << 18)) | ((r(off) >> 16 & 1u) << 18));
            return true;
        case ACS_CNT_CTRL:
            guarda(off, v, m);
            if (v & m & (1u << 20)) { pon(IBUS_HIT, 0); pon(IBUS_MISS, 0); }
            if (v & m & (1u << 21)) { pon(DBUS_HIT, 0); pon(DBUS_MISS, 0); }
            return true;
        default: return false;
        }
    }
    bool lee(uint32_t off, uint32_t& v) override {
        // Tras el reset, la invalidación de arranque ya ha acabado
        if (off == SYNC_CTRL) { v = (r(off) & ~1u) | 0x10u; return true; }
        return false;
    }

private:
    Spi0C6& spi0_;
    unsigned conjuntos_;
    std::vector<uint32_t> tag_;
    std::vector<uint64_t> edad_;
    uint64_t reloj_ = 0, aciertos_ = 0, fallos_ = 0;

    void invalida() { std::fill(tag_.begin(), tag_.end(), 0u); std::fill(edad_.begin(), edad_.end(), 0u); }
    bool busca(uint32_t lin) {
        const unsigned b = (lin % conjuntos_) * VIAS;
        ++reloj_;
        unsigned victima = b;
        for (unsigned w = b; w < b + VIAS; ++w) {
            if (tag_[w] == lin + 1) { edad_[w] = reloj_; return true; }
            if (edad_[w] < edad_[victima]) victima = w;
        }
        tag_[victima] = lin + 1;
        edad_[victima] = reloj_;
        return false;
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_C6_CACHE_H
