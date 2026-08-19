// =============================================================================
// image_loader.h — Cargador de imágenes de firmware (fase F1)
//
// Carga un binario crudo (.bin) o un Intel HEX (.hex, salida habitual de las
// toolchains ARM) en la memoria del modelo antes de arrancar la simulación,
// respetando el mapa de memoria: la Flash principal y las áreas de System
// Memory / OTP / option bytes las resuelve FlashIf; SRAM1, SRAM2, CCM y BKPSRAM
// se cargan directamente en su array.
//
// También genera la tabla de vectores mínima (MSP inicial + vector de reset)
// que la CPU leerá en la fase F2 [IR, §7.2.2].
// =============================================================================
#ifndef STM32_VERIF_IMAGE_LOADER_H
#define STM32_VERIF_IMAGE_LOADER_H

#include <cstdio>
#include <cstring>
#include <string>
#include "../top/stm32f407vg.h"

namespace stm32 {

class ImageLoader {
public:
    explicit ImageLoader(Stm32F407VG& dut) : dut_(dut) {}

    // Escribe un byte en la dirección indicada del mapa de memoria.
    bool poke8(uint32_t a, uint8_t v) {
        if (a >= addr::SRAM1_BASE && a < addr::SRAM1_BASE + addr::SRAM1_SIZE) {
            uint8_t b = v; return dut_.sram1.load(&b, 1, a - addr::SRAM1_BASE);
        }
        if (a >= addr::SRAM2_BASE && a < addr::SRAM2_BASE + addr::SRAM2_SIZE) {
            uint8_t b = v; return dut_.sram2.load(&b, 1, a - addr::SRAM2_BASE);
        }
        if (a >= addr::CCM_BASE && a < addr::CCM_BASE + addr::CCM_SIZE) {
            uint8_t b = v; return dut_.ccm.load(&b, 1, a - addr::CCM_BASE);
        }
        if (a >= addr::BKPSRAM_BASE && a < addr::BKPSRAM_BASE + addr::BKPSRAM_SIZE) {
            uint8_t b = v; return dut_.bkpsram.load(&b, 1, a - addr::BKPSRAM_BASE);
        }
        return dut_.flash.poke_byte(a, v);          // Flash, sysmem, OTP, opt
    }
    bool poke32(uint32_t a, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i)
            if (!poke8(a + i, uint8_t(v >> (8 * i)))) return false;
        return true;
    }

    // Carga un bloque de memoria del proceso anfitrión.
    bool load_bytes(uint32_t base, const uint8_t* d, size_t n) {
        for (size_t i = 0; i < n; ++i)
            if (!poke8(uint32_t(base + i), d[i])) return false;
        return true;
    }

    // Carga un fichero. Si termina en .hex o .ihex se interpreta como Intel HEX
    // (las direcciones vienen en el propio fichero y 'base' se ignora); en otro
    // caso se trata como binario crudo cargado a partir de 'base'.
    // Devuelve el nº de bytes cargados, o -1 en caso de error.
    long load_file(const char* path, uint32_t base = addr::FLASH_BASE) {
        const std::string p(path);
        const bool hex = p.size() > 4 &&
                         (p.compare(p.size() - 4, 4, ".hex") == 0 ||
                          p.compare(p.size() - 5, 5, ".ihex") == 0);
        if (hex) return dut_.flash.load_ihex_file(path);

        std::FILE* f = std::fopen(path, "rb");
        if (!f) return -1;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); return -1; }
        std::string buf(size_t(n), '\0');
        const size_t rd = std::fread(&buf[0], 1, size_t(n), f);
        std::fclose(f);
        return load_bytes(base, reinterpret_cast<const uint8_t*>(buf.data()), rd)
                   ? long(rd) : -1;
    }

    // Tabla de vectores mínima en el inicio de la Flash [IR, §7.2.2]:
    //   [0x0000] = valor inicial del MSP
    //   [0x0004] = dirección del manejador de reset (bit 0 = 1, estado Thumb)
    bool write_reset_vector(uint32_t initial_msp, uint32_t reset_handler,
                            uint32_t base = addr::FLASH_BASE) {
        return poke32(base + 0, initial_msp) &&
               poke32(base + 4, reset_handler | 1u);
    }

private:
    Stm32F407VG& dut_;
};

} // namespace stm32
#endif // STM32_VERIF_IMAGE_LOADER_H
