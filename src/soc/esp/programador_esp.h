// =============================================================================
// programador_esp.h — El `load` de GDB sobre la Flash de un ESP (E6, D4)
//
// En un ESP la Flash no está en el mapa de memoria: se ve por las ventanas de
// la caché (0x4200_0000 instrucciones y 0x3C00_0000 datos en el C3), página a
// página según la MMU. Un ELF de ESP-IDF lleva sus segmentos de la Flash con
// la dirección VIRTUAL (la de la ventana), no con el desplazamiento en la
// Flash: por eso Espressif no programa con `load` sino con `program_esp` de
// OpenOCD o con esptool, a partir de los .bin.
//
// Este programador hace lo que tiene sentido con un `load`: cada escritura en
// una ventana va al sitio de la Flash al que la MMU tiene mapeada esa página
// AHORA (la que puso el bootloader al cargar la aplicación). Así, con la
// aplicación arrancada y parada (`monitor reset halt` y `thb app_main`, o
// parada en cualquier sitio), `load` del mismo proyecto recompilado reescribe
// su código en la Flash. Lo que va a la IRAM o a la DRAM va por el bus como
// cualquier escritura. Una página sin mapear no es Flash para el programador:
// se escribe en la ventana, y eso da error (la caché es de solo lectura).
//
// Lo hace por la puerta de atrás (la memoria de la pieza SpiFlash, borrando
// sectores de 4 KB y programando), con la caché invalidada al acabar: no
// ejecuta el algoritmo de Flash en el chip como hace `program_esp`. Eso, en
// E6, lo hace OpenOCD por su cuenta.
// =============================================================================
#ifndef STM32_SOC_ESP_PROGRAMADOR_ESP_H
#define STM32_SOC_ESP_PROGRAMADOR_ESP_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include "../../common/gdb_rsp.h"

namespace stm32 {

class ProgramadorEsp : public programador_nv_if {
public:
    static constexpr uint32_t SECTOR = 4096;
    // De dirección virtual a desplazamiento en la Flash (false: no es Flash
    // mapeada)
    std::function<bool(uint32_t, uint32_t&)> traduce;
    std::function<uint32_t()> tam_flash;
    std::function<void(uint32_t, uint32_t)> borra;                     // desplazamiento, bytes
    std::function<void(uint32_t, const uint8_t*, unsigned)> escribe;   // desplazamiento
    std::function<void()> invalida;                                    // la caché, al acabar

    const char* nombre() const override { return "ESP (la Flash por las ventanas de la MMU)"; }
    int sector_de(uint32_t a) const override {
        uint32_t off = 0;
        return traduce && traduce(a, off) ? int(off / SECTOR) : -1;
    }
    uint32_t tam_sector(int) const override { return SECTOR; }
    unsigned n_sectores() const override { return tam_flash ? tam_flash() / SECTOR : 0u; }
    bool borrar(acceso_nv_if&, uint32_t addr, uint32_t len) override {
        for (uint32_t a = addr & ~(SECTOR - 1); a < addr + len; a += SECTOR) {
            uint32_t off = 0;
            if (!traduce(a, off)) return false;
            borra(off & ~(SECTOR - 1), SECTOR);
        }
        return true;
    }
    bool programar(acceso_nv_if&, uint32_t addr, const uint8_t* dat, unsigned n,
                   unsigned& palabras) override {
        for (unsigned k = 0; k < n;) {
            uint32_t off = 0;
            if (!traduce(addr + k, off)) return false;
            // Hasta el final de la página de 4 KB (la MMU mapea 64 KB: cabe)
            const unsigned resto = std::min<unsigned>(n - k, SECTOR - ((addr + k) & (SECTOR - 1)));
            escribe(off, dat + k, resto);
            k += resto;
            palabras += (resto + 3) / 4;
        }
        return true;
    }
    void terminar(acceso_nv_if&) override { if (invalida) invalida(); }
};

} // namespace stm32
#endif // STM32_SOC_ESP_PROGRAMADOR_ESP_H
