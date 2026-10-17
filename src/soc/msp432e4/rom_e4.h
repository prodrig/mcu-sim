// =============================================================================
// rom_e4.h — La ROM del MSP432E4 (T6)
//
// En 0x0100_0000 [DS; DL: rom.h]: el cargador de arranque y DriverLib, con la
// tabla de tablas en 0x0100_0010 (ROM_APITABLE), detrás de los cuatro
// primeros vectores del cargador. La ventana tiene, como en el P4, tres
// contenidos posibles:
//
//   R1  la ROM SUSTITUTA (T6, entrega 4): DriverLib del E4 compilada en las
//       casillas de rom.h y un cargador serie propio
//   R2  la de TI, de un fichero (`--rom FICHERO`): un volcado de la ventana o
//       un ELF con segmentos en ella
//   R3  sin ROM (`--rom ninguna`): cualquier acceso es un error de bus
//
// EL ALIAS DE ARRANQUE. Con la Flash en blanco (la palabra de 0x4 a
// 0xFFFFFFFF) o con BOOTCFG pidiendo el cargador por un pin, el chip arranca
// en la ROM. El modelo lo hace como los Tiva: la ROM se ve en 0x0 mientras
// RMCTL.BA = 1, y el cargador lo borra antes de saltar a la aplicación
// [flash.h; 1F: P-17 b].
// =============================================================================
#ifndef STM32_SOC_MSP432E4_ROM_E4_H
#define STM32_SOC_MSP432E4_ROM_E4_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "e4_desc.h"
#include "../../common/ahb_types.h"
#include "../../common/imagen_fw.h"
#include "../msp432p4/rom_p4.h"     // fnv1a

namespace stm32 {

class RomE4 : public sc_core::sc_module {
public:
    enum class Origen { R1, Volcado, Ninguna };

    tlm_utils::simple_target_socket<RomE4> tsk{"tsk"};

    explicit RomE4(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm), img_(e4::ROM_SIZE, 0xFF) {
        tsk.register_b_transport(this, &RomE4::bt);
        tsk.register_transport_dbg(this, &RomE4::dbg);
        pon_r1();
    }

    // R1: la imagen que lleva dentro el simulador (si la lleva)
    void pon_r1();
    bool hay_r1() const;
    void pon_ninguna() { origen_ = Origen::Ninguna; ruta_.clear(); }
    std::string pon_volcado(const std::string& ruta) {
        if (formato_fw(ruta) == FormatoFw::Elf) return pon_elf(ruta);
        std::FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) return "no se puede leer " + ruta;
        std::vector<uint8_t> d;
        int c;
        while ((c = std::fgetc(f)) != EOF && d.size() <= e4::ROM_SIZE) d.push_back(uint8_t(c));
        std::fclose(f);
        if (d.size() > e4::ROM_SIZE)
            return ruta + ": mas de 256 KB; la ventana de la ROM es 0x0100_0000-0x0103_FFFF";
        if (d.size() < 0x10u + 4u * 47u)
            return ruta + ": no llega a ROM_APITABLE (0x0100_0010); no es un volcado de la ROM";
        std::fill(img_.begin(), img_.end(), uint8_t(0xFF));
        std::memcpy(img_.data(), d.data(), d.size());
        origen_ = Origen::Volcado; ruta_ = ruta;
        return "";
    }
    std::string pon_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        const std::string e = lee_elf(ruta, t);
        if (!e.empty()) return e;
        std::vector<uint8_t> nueva(e4::ROM_SIZE, uint8_t(0xFF));
        bool tabla = false;
        for (const TrozoFw& tr : t) {
            if (tr.dir < e4::ROM_B || uint64_t(tr.dir) + tr.datos.size() >
                                          uint64_t(e4::ROM_B) + e4::ROM_SIZE)
                return ruta + ": un segmento cae fuera de la ROM del E4 "
                              "(0x0100_0000-0x0103_FFFF); no es una imagen de la ROM";
            std::memcpy(nueva.data() + (tr.dir - e4::ROM_B), tr.datos.data(), tr.datos.size());
            if (tr.dir <= e4::ROM_APITABLE && tr.dir + tr.datos.size() >= e4::ROM_APITABLE + 4u)
                tabla = true;
        }
        if (!tabla) return ruta + ": ningun segmento lleva ROM_APITABLE (0x0100_0010)";
        img_.swap(nueva);
        origen_ = Origen::Volcado; ruta_ = ruta;
        return "";
    }

    Origen origen() const { return origen_; }
    bool presente() const { return origen_ != Origen::Ninguna; }
    const char* nombre_origen() const {
        switch (origen_) {
            case Origen::R1:      return "R1 (sustituta: DriverLib compilada y un cargador propio)";
            case Origen::Volcado: return "R2 (la de TI, de fichero)";
            default:              return "R3 (sin ROM)";
        }
    }
    const std::string& ruta() const { return ruta_; }
    uint32_t huella() const { return fnv1a(img_.data(), img_.size()); }
    uint32_t peek32(uint32_t a) const {
        const uint32_t o = a - e4::ROM_B;
        if (uint64_t(o) + 4 > img_.size()) return 0xFFFFFFFFu;
        uint32_t v; std::memcpy(&v, img_.data() + o, 4); return v;
    }
    unsigned accesos() const { return n_; }
    unsigned fallos() const { return n_mal_; }
    uint64_t instrucciones() const { return n_instr_; }

private:
    std::vector<uint8_t> img_;
    Origen origen_ = Origen::Ninguna;
    std::string ruta_;
    unsigned n_ = 0, n_mal_ = 0;
    uint64_t n_instr_ = 0;
    bool avisado_ninguna_ = false, avisado_escritura_ = false;

    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        ++n_;
        const uint32_t o = uint32_t(gp.get_address()) - e4::ROM_B;
        const unsigned len = gp.get_data_length();
        if (origen_ == Origen::Ninguna) {
            ++n_mal_;
            if (!avisado_ninguna_) {
                avisado_ninguna_ = true;
                SC_REPORT_WARNING("msp432e4/rom",
                    "acceso a la ROM (0x0100_0000) sin ROM (--rom ninguna): es un "
                    "error de bus. Un firmware con MAP_/ROM_ salta aqui");
            }
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.is_write()) {
            ++n_mal_;
            if (!avisado_escritura_) {
                avisado_escritura_ = true;
                SC_REPORT_WARNING("msp432e4/rom",
                    "escritura en la ROM (0x0100_0000): es de solo lectura");
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
        const uint32_t o = uint32_t(gp.get_address()) - e4::ROM_B;
        unsigned len = gp.get_data_length();
        if (o >= img_.size()) return 0;
        if (o + len > img_.size()) len = unsigned(img_.size() - o);
        std::memcpy(gp.get_data_ptr(), img_.data() + o, len);
        return len;
    }
};

// La R1 llega en la entrega 4 de T6. Hasta entonces el chip sale sin ROM.
inline bool RomE4::hay_r1() const { return false; }
inline void RomE4::pon_r1() { pon_ninguna(); }

} // namespace stm32
#endif // STM32_SOC_MSP432E4_ROM_E4_H
