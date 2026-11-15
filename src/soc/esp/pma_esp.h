// =============================================================================
// pma_esp.h — El comprobador de atributos de memoria (PMAC) de los núcleos de
//             Espressif con CSR propios: el ESP32-C6 HP
//
// E9 de doc/analisis_esp32_riscv.md [TRM-C6 §1.9]. Dieciséis entradas,
// pma_cfg0-15 (0xBC0-0xBCF) y pma_addr0-15 (0xBD0-0xBDF), solo desde M:
//   pma_cfgX: A [31:30] (como el A de pmpcfg: OFF, TOR, NA4, NAPOT), LOCK
//   [29], ATTRIBUTE [27:24] (para la caché: aquí solo se guarda), READ [4],
//   WRITE [3], EXECUTE [2] y TYPE [1:0] (0 no válida: RWX a 0; 1 válida).
//   De reset, A = 2 (NA4) y lo demás 0: la palabra 0 no es válida (un puntero
//   nulo falla). pma_addrX, como pmpaddr (la dirección >> 2).
// LOCK impide escribir la entrada hasta el reset de la CPU.
// LA COMPROBACIÓN, en todos los modos: la entrada de número más bajo que
// abarca el acceso decide; si ninguna lo abarca, se deja pasar. Una entrada
// no válida (TYPE 0) o sin el permiso da el fallo de acceso de la clase
// (1 instrucción, 5 carga, 7 almacenamiento), como el PMP. Se engancha en la
// memoria del hart (`permite`): un `false` es un error de bus para él.
// =============================================================================
#ifndef STM32_SOC_ESP_PMA_ESP_H
#define STM32_SOC_ESP_PMA_ESP_H

#include <cstdint>
#include "../../rv/hart/rv_ifaces.h"

namespace stm32 {

class PmaEsp {
public:
    static constexpr unsigned N = 16;
    PmaEsp() { reset(); }
    void reset() {
        for (unsigned i = 0; i < N; ++i) { cfg_[i] = 2u << 30; addr_[i] = 0; }
        recalcula();
    }
    static bool es_suyo(uint16_t csr) { return csr >= 0xBC0 && csr <= 0xBDF; }
    bool lee(uint16_t csr, uint32_t& v) const {
        v = csr < 0xBD0 ? cfg_[csr - 0xBC0] : addr_[csr - 0xBD0];
        return true;
    }
    void escribe(uint16_t csr, uint32_t v) {
        const unsigned i = (csr - 0xBC0u) & 15u;
        if ((cfg_[i] >> 29) & 1u) return;                  // LOCK
        if (csr < 0xBD0) cfg_[i] = v & 0xEF00001Fu;
        else addr_[i] = v;
        recalcula();
    }
    uint32_t cfg(unsigned i) const { return cfg_[i]; }
    // Los permisos que da a `dir` (para la errata CPU-863): la entrada que
    // decide y su TYPE y RWX, o 0 si ninguna la abarca
    uint32_t firma(uint32_t dir) const {
        for (unsigned i = 0; i < N; ++i)
            if (ok_[i] && dir >= lo_[i] && dir < hi_[i]) return 0x100u | (i << 5) | (cfg_[i] & 0x1Fu);
        return 0;
    }
    // ¿Deja el acceso de `bytes` en `dir`?
    bool permite(uint32_t dir, unsigned bytes, RvAcceso a) const {
        const uint64_t ini = dir, fin = uint64_t(dir) + bytes;
        for (unsigned i = 0; i < N; ++i) {
            if (!ok_[i]) continue;
            if (fin <= lo_[i] || ini >= hi_[i]) continue;      // no lo toca
            const uint32_t c = cfg_[i];
            if ((c & 3u) != 1u) return false;                  // no válida
            if (a == RvAcceso::Fetch) return (c >> 2) & 1u;
            if (a == RvAcceso::Load) return (c >> 4) & 1u;
            if (a == RvAcceso::Store) return (c >> 3) & 1u;
            return ((c >> 4) & 1u) && ((c >> 3) & 1u);         // AMO: lee y escribe
        }
        return true;
    }

private:
    uint32_t cfg_[N], addr_[N];
    // Los rangos, calculados al escribir y no en cada acceso: el núcleo
    // pregunta en cada instrucción
    uint64_t lo_[N], hi_[N];
    bool ok_[N];
    void recalcula() { for (unsigned i = 0; i < N; ++i) ok_[i] = rango(i, lo_[i], hi_[i]); }
    bool rango(unsigned i, uint64_t& lo, uint64_t& hi) const {
        const unsigned a = (cfg_[i] >> 30) & 3u;
        const uint64_t ad = uint64_t(addr_[i]) << 2;
        switch (a) {
        case 1: lo = i ? uint64_t(addr_[i - 1]) << 2 : 0; hi = ad; return lo < hi;   // TOR
        case 2: lo = ad; hi = ad + 4; return true;                                     // NA4
        case 3: {                                                                      // NAPOT
            unsigned k = 0;
            while (k < 32 && ((addr_[i] >> k) & 1u)) ++k;
            const uint64_t tam = uint64_t(8) << k;
            lo = (uint64_t(addr_[i]) << 2) & ~(tam - 1);
            hi = lo + tam;
            return true;
        }
        default: return false;
        }
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP_PMA_ESP_H
