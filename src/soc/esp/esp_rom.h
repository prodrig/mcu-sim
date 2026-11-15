// =============================================================================
// esp_rom.h — La ROM de un chip de Espressif: la de verdad (R1) o ninguna (R3)
//
// E4 de doc/analisis_esp32_riscv.md, §8.1.
//
//   R1  el contenido de la ROM del chip y la revisión: los segmentos
//       cargables del ELF de espressif/esp-rom-elfs (Apache-2.0). Las que usan
//       las suites van DENTRO del simulador como .inc generados
//       (soc/esp32c3/rom/gen_rom_c3.py), así que ni el CI ni el alumno
//       necesitan la red. Otra cualquiera se da con `rom=fichero.elf` en el
//       <mcu> (por ejemplo, la de ~/.espressif/tools/esp-rom-elfs).
//   R3  `rom=ninguna`: no hay ROM. Leer en su ventana devuelve cero y avisa
//       una vez; el firmware se carga en la SRAM y arranca en su entrada (el
//       arranque directo de E3 y las suites bare-metal).
//
// LA REVISIÓN MANDA. El C3 v1.1 trae otra ROM con funciones en otras
// direcciones. El descriptor fija la revisión y con ella la ROM por omisión;
// si se pide la ROM de otra revisión, el chip (Esp32C3::aviso_rom) lo dice al
// montar: esa pareja no existe en el silicio. ESP-IDF 5.x y 6.x, con la
// revisión mínima de omisión (v0.3), arrancan con las dos ROM: llaman a la ROM
// por la tabla de su revisión (esp_rom_*) y eligen los parches por los eFuse.
// =============================================================================
#ifndef STM32_SOC_ESP_ESP_ROM_H
#define STM32_SOC_ESP_ESP_ROM_H

#include <cstdint>
#include <string>
#include <vector>
#include "../../common/imagen_fw.h"

namespace stm32 {

class RomEsp {
public:
    // base y tam: la ventana de la ROM por el bus de instrucciones
    RomEsp(uint32_t base, uint32_t tam) : base_(base), tam_(tam) {}

    // R1 desde palabras empotradas (un .inc)
    void pon_empotrada(const uint32_t* w, size_t n, unsigned rev, const std::string& nombre) {
        img_.assign(tam_, 0);
        for (size_t i = 0; i < n && 4 * i + 3 < tam_; ++i)
            for (unsigned k = 0; k < 4; ++k) img_[4 * i + k] = uint8_t(w[i] >> (8 * k));
        rev_ = rev;
        nombre_ = nombre;
    }
    // R1 desde un ELF de esp-rom-elfs: los PT_LOAD cuya dirección física cae
    // en la ventana. "" si bien. La revisión sale del nombre (…_rev<N>_rom.elf).
    std::string carga_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        const std::string e = lee_elf(ruta, t, ELF_RISCV);
        if (!e.empty()) return e;
        std::vector<uint8_t> img(tam_, 0);
        bool algo = false;
        for (const TrozoFw& x : t) {
            if (x.dir < base_ || x.dir >= base_ + tam_) continue;
            if (uint64_t(x.dir) + x.datos.size() > uint64_t(base_) + tam_)
                return ruta + ": un segmento se sale de la ventana de la ROM";
            std::copy(x.datos.begin(), x.datos.end(), img.begin() + (x.dir - base_));
            algo = true;
        }
        if (!algo) return ruta + ": ningún segmento cae en la ROM: ¿es el ELF de una ROM?";
        img_.swap(img);
        nombre_ = ruta;
        rev_ = REV_DESCONOCIDA;
        const size_t p = ruta.rfind("_rev");
        if (p != std::string::npos) {
            unsigned r = 0;
            size_t i = p + 4;
            bool num = false;
            while (i < ruta.size() && ruta[i] >= '0' && ruta[i] <= '9') { r = r * 10 + unsigned(ruta[i] - '0'); ++i; num = true; }
            if (num) rev_ = r;
        }
        return "";
    }
    // R3
    void quita() { img_.clear(); nombre_ = "ninguna"; rev_ = REV_DESCONOCIDA; }

    bool hay() const { return !img_.empty(); }
    uint32_t base() const { return base_; }
    uint32_t tam() const { return tam_; }
    // La revisión de la ROM, en el número de esp-rom-elfs (3 = v0.3/v0.4,
    // 101 = v1.1)
    unsigned revision() const { return rev_; }
    const std::string& nombre() const { return nombre_; }
    const uint8_t* datos() const { return img_.data(); }
    uint8_t* byte(uint32_t a) {
        if (img_.empty() || a < base_ || a >= base_ + tam_) return nullptr;
        return &img_[a - base_];
    }

    static constexpr unsigned REV_DESCONOCIDA = ~0u;

private:
    uint32_t base_, tam_;
    std::vector<uint8_t> img_;
    unsigned rev_ = REV_DESCONOCIDA;
    std::string nombre_ = "ninguna";
};

} // namespace stm32
#endif // STM32_SOC_ESP_ESP_ROM_H
