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
#include "../top/soc_f4.h"

namespace stm32 {

class ImageLoader {
public:
    explicit ImageLoader(SocF4& dut) : dut_(dut) {}

    // Escribe un byte en la dirección indicada del mapa de memoria.
    //
    // Cada bloque dice DE SÍ MISMO dónde empieza y cuánto mide (`Sram::base()`
    // y `Sram::size()`), en vez de consultar las constantes globales del F407.
    // No es cosmética: era el último sitio del camino de carga que daba por
    // sabido el tamaño de las RAM, y con él un chip con otra SRAM cargaba mal
    // el firmware sin quejarse.
    bool poke8(uint32_t a, uint8_t v) {
        if (en(dut_.sram1, a))   return uno(dut_.sram1, a, v);
        if (en(dut_.sram2, a))   return uno(dut_.sram2, a, v);
        if (en(dut_.ccm, a))     return uno(dut_.ccm, a, v);
        if (en(dut_.bkpsram, a)) return uno(dut_.bkpsram, a, v);
        return dut_.flash.poke_byte(a, v);          // Flash, sysmem, OTP, opt
    }
    static bool en(const Sram& m, uint32_t a) {
        return m.size() != 0 && a >= m.base() && a < m.base() + m.size();
    }
    static bool uno(Sram& m, uint32_t a, uint8_t v) {
        uint8_t b = v; return m.load(&b, 1, a - m.base());
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
    SocF4& dut_;
};

} // namespace stm32
#endif // STM32_VERIF_IMAGE_LOADER_H
