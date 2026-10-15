// =============================================================================
// rom_p4.h — La ROM de DriverLib del MSP432P401R (fase T4)
//
// En el P4 la ROM es DriverLib: 32 KB en 0x0200_0000 con una tabla de tablas
// en 0x0200_0800 (ROM_APITABLE). Un firmware que llama a MAP_xxx() salta por
// esa tabla a la funcion de la ROM [doc/analisis_msp432.md, §6.1]. La ventana
// tiene tres contenidos posibles, que elige quien monta el chip:
//
//   R1  la ROM SUSTITUTA, por omision. DriverLib del SDK (BSD-3) compilada con
//       arm-none-eabi-gcc y colocada con las tablas de rom.h, en su orden
//       (verif/fw/msp432p4/rom_r1). Va DENTRO del simulador -rom_r1.inc-, asi
//       que no depende de ninguna ruta. Mismas funciones en las mismas
//       casillas que la de TI, otras instrucciones: los firmwares que usan
//       MAP_ funcionan sin cambios, y tardan lo que tarda ESTE codigo.
//   R2  la ROM DE TI (`--rom FICHERO`), que no se versiona: sirve para medir
//       cuanto se aparta la R1 del silicio. Vale un volcado en binario de los
//       32 KB de 0x0200_0000 (OpenOCD: `dump_image rom.bin 0x02000000
//       0x8000`) o un ELF con segmentos en la ventana, como la imagen que CCS
//       instala para depurar dentro de la ROM:
//       ccs_base/emulation/gel/images/msp432p401r/revC/
//       msp432_driverlib_rom_image.out.
//   R3  SIN ROM (`--rom ninguna`): cualquier acceso es un error de bus, y la
//       primera vez se dice por que. Era el comportamiento de T1-T3.
//
// Se lee sin estados de espera, como la SRAM [sintetico: SLAS826 no da los de
// la ROM]. Escribir en ella es un error de bus, avisado una vez. El depurador
// la lee y no la escribe.
// =============================================================================
#ifndef STM32_SOC_MSP432P4_ROM_P4_H
#define STM32_SOC_MSP432P4_ROM_P4_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "p4_desc.h"
#include "../../common/ahb_types.h"
#include "../../common/imagen_fw.h"

namespace stm32 {

namespace rom_r1 {
// La imagen de la ROM R1, palabra a palabra. La genera el Makefile de
// verif/fw/msp432p4/rom_r1 a partir de rom_r1.bin; su huella esta en
// verif/fw/huellas.txt y la comprueba el primer grupo de la suite.
inline const uint32_t IMAGEN[] = {
#include "rom_r1.inc"
};
static_assert(sizeof(IMAGEN) == p4::ROM_SIZE, "la ROM R1 tiene que llenar la ventana");
inline constexpr uint32_t FNV = ROM_R1_FNV;
} // namespace rom_r1

inline uint32_t fnv1a(const uint8_t* d, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ d[i]) * 16777619u;
    return h;
}

class RomP4 : public sc_core::sc_module {
public:
    enum class Origen { R1, Volcado, Ninguna };

    tlm_utils::simple_target_socket<RomP4> tsk{"tsk"};

    explicit RomP4(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm), img_(p4::ROM_SIZE) {
        tsk.register_b_transport(this, &RomP4::bt);
        tsk.register_transport_dbg(this, &RomP4::dbg);
        pon_r1();
    }

    // --- Lo que elige quien monta el chip ----------------------------------
    void pon_r1() {
        std::memcpy(img_.data(), rom_r1::IMAGEN, p4::ROM_SIZE);
        origen_ = Origen::R1; ruta_.clear();
    }
    void pon_ninguna() { origen_ = Origen::Ninguna; ruta_.clear(); }
    // Un volcado: los 32 KB de la ventana, en binario. Uno mas corto se
    // rellena con 0xFF (lo que no se volco no se inventa); uno mas largo no
    // es la ROM de este chip. Devuelve el error, o "" si va bien.
    std::string pon_volcado(const std::string& ruta) {
        if (formato_fw(ruta) == FormatoFw::Elf) return pon_elf(ruta);
        std::FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) return "no se puede leer " + ruta;
        std::vector<uint8_t> d;
        int c;
        while ((c = std::fgetc(f)) != EOF && d.size() <= p4::ROM_SIZE) d.push_back(uint8_t(c));
        std::fclose(f);
        if (d.size() > p4::ROM_SIZE)
            return ruta + ": mas de 32 KB; la ROM del P401R es 0x0200_0000-0x0200_7FFF";
        if (d.size() < 0x800u + 4u * 29u)
            return ruta + ": no llega a ROM_APITABLE (0x0200_0800); no es un volcado "
                          "de la ROM";
        std::fill(img_.begin(), img_.end(), uint8_t(0xFF));
        std::memcpy(img_.data(), d.data(), d.size());
        origen_ = Origen::Volcado; ruta_ = ruta;
        return "";
    }

    // Un ELF: sus segmentos PT_LOAD, que tienen que caer en la ventana y
    // llegar a ROM_APITABLE. Lo que no cubren queda a 0xFF.
    std::string pon_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        const std::string e = lee_elf(ruta, t);
        if (!e.empty()) return e;
        std::vector<uint8_t> nueva(p4::ROM_SIZE, uint8_t(0xFF));
        bool tabla = false;
        for (const TrozoFw& tr : t) {
            if (tr.dir < p4::ROM_B || uint64_t(tr.dir) + tr.datos.size() >
                                          uint64_t(p4::ROM_B) + p4::ROM_SIZE)
                return ruta + ": un segmento cae fuera de la ROM del P401R "
                              "(0x0200_0000-0x0200_7FFF); no es una imagen de la ROM";
            std::memcpy(nueva.data() + (tr.dir - p4::ROM_B), tr.datos.data(), tr.datos.size());
            if (tr.dir <= p4::ROM_B + 0x800u && tr.dir + tr.datos.size() >= p4::ROM_B + 0x800u + 4u * 29u)
                tabla = true;
        }
        if (!tabla) return ruta + ": ningun segmento lleva ROM_APITABLE (0x0200_0800)";
        img_.swap(nueva);
        origen_ = Origen::Volcado; ruta_ = ruta;
        return "";
    }

    // --- Lo que mira el banco y el adaptador -----------------------------------
    Origen origen() const { return origen_; }
    const char* nombre_origen() const {
        switch (origen_) {
            case Origen::R1:      return "R1 (sustituta, DriverLib compilada)";
            case Origen::Volcado: return "R2 (la de TI, de fichero)";
            default:              return "R3 (sin ROM)";
        }
    }
    const std::string& ruta() const { return ruta_; }
    uint32_t huella() const { return fnv1a(img_.data(), img_.size()); }
    uint32_t peek32(uint32_t a) const {
        const uint32_t o = a - p4::ROM_B;
        if (o + 4 > img_.size()) return 0xFFFFFFFFu;
        uint32_t v; std::memcpy(&v, img_.data() + o, 4); return v;
    }
    // Accesos del bus: todos, los que fallaron y las instrucciones leidas
    unsigned accesos() const { return n_; }
    unsigned fallos() const { return n_mal_; }
    uint64_t instrucciones() const { return n_instr_; }

private:
    std::vector<uint8_t> img_;
    Origen origen_ = Origen::R1;
    std::string ruta_;
    unsigned n_ = 0, n_mal_ = 0;
    uint64_t n_instr_ = 0;
    bool avisado_ninguna_ = false, avisado_escritura_ = false;

    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        ++n_;
        const uint32_t o = uint32_t(gp.get_address()) - p4::ROM_B;
        const unsigned len = gp.get_data_length();
        if (origen_ == Origen::Ninguna) {
            ++n_mal_;
            if (!avisado_ninguna_) {
                avisado_ninguna_ = true;
                SC_REPORT_WARNING("msp432p4/rom",
                    "acceso a la ROM de DriverLib (0x0200_0000) con --rom ninguna: "
                    "no hay ROM y es un error de bus. Un firmware que usa MAP_/ROM_ "
                    "salta aqui; quita --rom ninguna para tener la ROM R1");
            }
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.is_write()) {
            ++n_mal_;
            if (!avisado_escritura_) {
                avisado_escritura_ = true;
                SC_REPORT_WARNING("msp432p4/rom",
                    "escritura en la ROM de DriverLib (0x0200_0000): es de solo "
                    "lectura, y el bus contesta con error");
            }
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (uint64_t(o) + len > img_.size()) {
            ++n_mal_;
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (const AhbExt* e = gp.get_extension<AhbExt>()) if (e->instr) ++n_instr_;
        std::memcpy(gp.get_data_ptr(), img_.data() + o, len);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        if (origen_ == Origen::Ninguna || !gp.is_read()) return 0;
        const uint32_t o = uint32_t(gp.get_address()) - p4::ROM_B;
        unsigned len = gp.get_data_length();
        if (o >= img_.size()) return 0;
        if (o + len > img_.size()) len = unsigned(img_.size() - o);
        std::memcpy(gp.get_data_ptr(), img_.data() + o, len);
        return len;
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP432P4_ROM_P4_H
