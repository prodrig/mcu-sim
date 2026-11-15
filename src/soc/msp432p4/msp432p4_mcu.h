// =============================================================================
// msp432p4_mcu.h — El ADAPTADOR del MSP432P4 a `mcu_if`, y su alta en la factoría
//
// Lo mismo que `soc/stm32f4_mcu.h` para los STM32, con lo que cambia de un
// chip a otro dicho aquí y no en `sim`:
//
//   * los PINES se llaman `P1.0`…`PJ.5` y la alimentación DVCC/AVCC/DVSS/AVSS,
//     con RSTN en vez de NRST y sin BOOT0;
//   * SWCLK y SWDIO son las patillas DEDICADAS SWCLKTCK y SWDIOTMS;
//   * el FIRMWARE va a la Flash en 0x0, y se lee en cualquiera de los cuatro
//     formatos: binario, Intel HEX, ELF (el `.out` de CCS) y TI-TXT.
//
// La familia que despacha la factoría es "MSP432P4". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_MSP432P4_MCU_H
#define STM32_SOC_MSP432P4_MCU_H

#include <cstdio>
#include "../mcu_if.h"
#include "msp432p401r.h"
#include "../../parts/netlist.h"
#include "../../common/imagen_fw.h"
#include "../../common/memoria_nv.h"
#include "../herramientas.h"

namespace stm32 {

class McuP4 : public mcu_if, public memoria_nv_if {
public:
    McuP4(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d)
        : chip_(new Msp432P4(nm, dbg, cab, *static_cast<const McuCapsP4*>(d.propio))),
          desc_(d) {
        d_dvcc_ = chip_->dvcc->register_driver("sim_dvcc");
        d_dvss_ = chip_->dvss->register_driver("sim_dvss");
        d_avcc_ = chip_->avcc->register_driver("sim_avcc");
        d_avss_ = chip_->avss->register_driver("sim_avss");
        d_rstn_ = chip_->rstn->register_driver("sim_rstn");
    }
    ~McuP4() override { delete chip_; }

    Msp432P4& chip() { return *chip_; }

    // La Flash principal y la de información entera, para `contenido`
    // (common/memoria_nv.h):
    //   * el sector 0 de la información (0x0020_0000): el buzón del arranque y
    //     los datos del programa, sin lo que esté a 0xFF;
    //   * el TLV (0x0020_1000), que el firmware no puede cambiar: sale solo si
    //     difiere del que había antes de cargar -es decir, si vino del
    //     fichero-, entero. Uno que viene en el fichero se valida y se adopta,
    //     salvo que tlv= diga uno: entonces manda tlv=;
    //   * el BSL (0x0020_2000), que el firmware sí puede cambiar: sale solo si
    //     difiere del que puso bsl=, entero. Uno que viene en el fichero manda
    //     sobre bsl=.
    memoria_nv_if* memoria_nv() override { return this; }
    std::vector<RegionNv> regiones_nv() const override {
        const auto& c = *static_cast<const McuCapsP4*>(desc_.propio);
        return {RegionNv::borrada("Flash", p4::FLASH_B, c.flash_size),
                RegionNv::borrada("Flash de informacion", p4::INFO_B, 0x1000u),
                RegionNv::con_referencia("TLV", p4::TLV_B,
                    ref_tlv_.empty() ? chip_->flash.copia(p4::TLV_B, 0x1000u) : ref_tlv_, true),
                RegionNv::con_referencia("BSL", p4::BSL_B,
                    ref_bsl_.empty() ? chip_->flash.copia(p4::BSL_B, p4::BSL_SIZE) : ref_bsl_, true)};
    }
    bool en_nv(uint32_t a) const {
        const auto& c = *static_cast<const McuCapsP4*>(desc_.propio);
        return (a >= p4::FLASH_B && a - p4::FLASH_B < c.flash_size) ||
               (a >= p4::INFO_B && a - p4::INFO_B < p4::INFO_SIZE);
    }
    bool lee_nv(uint32_t a, uint8_t& v) override {
        if (!en_nv(a)) return false;
        v = uint8_t(chip_->flash.peek32(a));
        return true;
    }
    bool escribe_nv(uint32_t a, uint8_t v) override {
        return en_nv(a) && chip_->flash.poke_byte(a, v);
    }
    // Antes de cargar, con tlv= y bsl= ya puestos: las referencias
    void antes_de_cargar_nv() override {
        ref_tlv_ = chip_->flash.copia(p4::TLV_B, 0x1000u);
        ref_bsl_ = chip_->flash.copia(p4::BSL_B, p4::BSL_SIZE);
    }
    std::vector<std::string> tras_cargar_nv(std::string& error) override {
        std::vector<std::string> a;
        if (chip_->flash.copia(p4::TLV_B, 0x1000u) != ref_tlv_) {
            if (!chip_->tlv().empty()) {
                for (uint32_t i = 0; i < 0x1000u; ++i) chip_->flash.poke_byte(p4::TLV_B + i, ref_tlv_[i]);
                a.push_back("el TLV del contenido no se usa: manda el de tlv= (" + chip_->tlv() + ")");
            } else {
                const std::string e = chip_->adopta_tlv_de_contenido();
                if (!e.empty()) { error = "el TLV del fichero (0x0020_1000) " + e; return a; }
                a.push_back("TLV del contenido: el de esa pieza, con sus calibraciones");
            }
        }
        if (chip_->flash.copia(p4::BSL_B, p4::BSL_SIZE) != ref_bsl_) {
            chip_->bsl_de_contenido();
            a.push_back("BSL del contenido, distinto del de bsl=: manda el del contenido");
        }
        return a;
    }
    const McuDesc& desc() const override { return desc_; }
    std::vector<congela::Nucleo> nucleos() override {
        return {chip_->core.nucleo_congelable(std::string(chip_->name()) + ".core")};
    }
    std::vector<std::string> limitaciones() const override { return chip_->limitaciones(); }

    // Los nodos, con los nombres del P4. Se asegura además de que el contexto
    // de nomenclatura sabe de este chip con este prefijo: si la placa no se
    // leyó de un XML -un banco de pruebas- nadie lo habría fijado.
    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_MSP432P4);
        else                 c.fija(prefijo, &NOM_MSP432P4);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = chip_->mcu.enc;
        for (unsigned p = 0; p < p4::N_PUERTOS; ++p)
            for (unsigned i = 0; i < p4::N_PINES; ++i) {
                const std::string canon = p4::canonico_p4(p, i);
                if (AnalogNet* a = chip_->dio.propio[p][i]) a->nombre_esq = pre + canon;
                n.registra(pre + canon, chip_->dio.nodo_de(p, i), true,
                           e.bonded(p, i), e.nombre);
            }
        n.registra(pre + "DVCC", *chip_->dvcc);   n.registra(pre + "DVSS", *chip_->dvss);
        n.registra(pre + "AVCC", *chip_->avcc);   n.registra(pre + "AVSS", *chip_->avss);
        n.registra(pre + "VCORE", *chip_->vcore_pin);
        n.registra(pre + "RSTN", *chip_->rstn);
        // Las dos de SWD son pads del chip, como los de los puertos: lo que no
        // las gobierne ninguna pieza no queda «colgado»
        n.registra(pre + "SWCLKTCK", *chip_->swclk_net, true);
        n.registra(pre + "SWDIOTMS", *chip_->swdio_net, true);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        return chip_->dio.nodo_de(puerto, pin);
    }
    analog_net_if* nodo_swclk() override { return chip_->swclk_net; }
    analog_net_if* nodo_swdio() override { return chip_->swdio_net; }

    void alimentacion_externa(bool si) override { externa_ = si; }
    void alimenta(bool on) override {
        if (externa_) chip_->dvcc->set_hiz(d_dvcc_);   // la pone la placa
        if (on) {
            if (!externa_) chip_->dvcc->set_drive(d_dvcc_, 3.3f, 0.1f);
            chip_->avcc->set_drive(d_avcc_, 3.3f, 0.1f);
            chip_->dvss->set_drive(d_dvss_, 0.0f, 0.1f);
            chip_->avss->set_drive(d_avss_, 0.0f, 0.1f);
        } else {
            if (!externa_) chip_->dvcc->set_drive(d_dvcc_, 0.0f, 1.0f);
            chip_->avcc->set_drive(d_avcc_, 0.0f, 1.0f);
            reset_pin(true);
        }
    }
    void reset_pin(bool activo) override {
        if (activo) chip_->rstn->set_drive(d_rstn_, 0.0f, 100.0f);
        else        chip_->rstn->set_hiz(d_rstn_);
    }

    bool carga_firmware(const std::string& ruta) override {
        std::vector<TrozoFw> t;
        std::string e;
        switch (formato_fw(ruta)) {
            case FormatoFw::Elf:      e = lee_elf(ruta, t);   break;
            case FormatoFw::TiTxt:    e = lee_titxt(ruta, t); break;
            case FormatoFw::IntelHex: e = lee_ihex(ruta, t);  break;
            case FormatoFw::Bin: {
                std::FILE* f = std::fopen(ruta.c_str(), "rb");
                if (!f) { e = "no se puede leer " + ruta; break; }
                TrozoFw tr; tr.dir = p4::FLASH_B;
                int c;
                while ((c = std::fgetc(f)) != EOF) tr.datos.push_back(uint8_t(c));
                std::fclose(f);
                t.push_back(std::move(tr));
                break;
            }
        }
        if (!e.empty()) { std::fprintf(stderr, "  [fw] %s\n", e.c_str()); return false; }
        for (const TrozoFw& x : t)
            for (size_t i = 0; i < x.datos.size(); ++i)
                if (!chip_->poke8(uint32_t(x.dir + i), x.datos[i])) {
                    std::fprintf(stderr, "  [fw] %s: 0x%08X no es ni Flash ni SRAM del "
                                 "%s\n", ruta.c_str(), unsigned(x.dir + i), desc_.nombre);
                    return false;
                }
        return true;
    }
    // Sin firmware: SP al final de la SRAM y un `wfe` en bucle en 0x100.
    void aparca_en_wfe() override {
        auto w32 = [&](uint32_t a, uint32_t v) {
            for (unsigned i = 0; i < 4; ++i) chip_->poke8(a + i, uint8_t(v >> (8 * i)));
        };
        w32(p4::FLASH_B + 0, p4::SRAM_B + chip_->mcu.sram_size);
        w32(p4::FLASH_B + 4, (p4::FLASH_B + 0x100u) | 1u);
        w32(p4::FLASH_B + 0x100u, 0xE7FDBF20u);
    }
    // Sin firmware y con BSL, la Flash se queda en blanco: el código de
    // arranque entra en el BSL, como en un chip recién salido de fábrica, y se
    // le puede programar por el backchannel (BSL Scripter, verif/serie/bsl_p4.py)
    std::string sin_firmware() override {
        if (chip_->origen_bsl() == Msp432P4::OrigenBsl::Ninguno) {
            aparca_en_wfe(); return "el nucleo se aparca en wfe";
        }
        return "la Flash queda en blanco, como en un chip nuevo: el codigo de arranque "
               "entra en el BSL (UART en P1.2/P1.3, 9600 8E1)";
    }
    void set_ondas_reloj(bool on) override { chip_->set_ondas(on); }
    programador_nv_if* programador() override { return &ProgramadorP4::el(); }
    // La ROM de DriverLib y el TLV (T4)
    bool admite_opcion(const std::string& clave) const override {
        return clave == "rom" || clave == "tlv" || clave == "bsl";
    }
    std::string opcion(const std::string& clave, const std::string& valor) override {
        if (clave == "rom") {
            if (valor == "r1" || valor == "R1") { chip_->rom.pon_r1(); return ""; }
            if (valor == "ninguna" || valor == "r3" || valor == "R3") {
                chip_->rom.pon_ninguna(); return "";
            }
            return chip_->rom.pon_volcado(valor);
        }
        if (clave == "bsl") {
            if (valor == "b1" || valor == "B1") {
                if (!chip_->hay_bsl_b1()) return "este simulador no lleva el BSL B1";
                chip_->pon_bsl_b1(); return "";
            }
            if (valor == "ninguno" || valor == "b3" || valor == "B3") { chip_->pon_bsl_ninguno(); return ""; }
            return chip_->pon_bsl_fichero(valor);
        }
        if (clave == "tlv") {
            if (valor == "sintetico") { chip_->pon_tlv_sintetico(); return ""; }
            return chip_->carga_tlv(valor);
        }
        return mcu_if::opcion(clave, valor);
    }
    std::string describe_opcion(const std::string& clave) const override {
        char h[16];
        if (clave == "rom") {
            std::snprintf(h, sizeof h, "0x%08X", unsigned(chip_->rom.huella()));
            std::string d = chip_->rom.nombre_origen();
            if (!chip_->rom.ruta().empty()) d += ", " + chip_->rom.ruta();
            if (chip_->rom.origen() != RomP4::Origen::Ninguna) d += std::string(", huella ") + h;
            return d;
        }
        if (clave == "tlv") return chip_->tlv().empty() ? "sintetico" : chip_->tlv();
        if (clave == "bsl") {
            std::string d = chip_->nombre_bsl();
            if (!chip_->ruta_bsl().empty()) d += ", " + chip_->ruta_bsl();
            if (chip_->origen_bsl() != Msp432P4::OrigenBsl::Ninguno) {
                std::snprintf(h, sizeof h, "0x%08X", unsigned(chip_->huella_bsl()));
                d += std::string(", huella ") + h;
            }
            return d;
        }
        return "";
    }
    herramienta_if* herramienta() override { return nullptr; }
    void herramienta_interno(herramienta_if* h) override { if (chip_->core.gdb) chip_->core.gdb->pon_herramienta(h); }
    bool tiene_gdb_interno() const override { return chip_->core.gdb != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!chip_->core.gdb) return;
        chip_->core.gdb->set_verbose(traza);
        chip_->core.gdb->set_enabled(activo);
    }

private:
    std::vector<uint8_t> ref_tlv_, ref_bsl_;   // las referencias, antes de cargar
    Msp432P4* chip_;
    McuDesc   desc_;
    int d_dvcc_ = -1, d_dvss_ = -1, d_avcc_ = -1, d_avss_ = -1, d_rstn_ = -1;
    bool externa_ = false;               // DVCC la pone la placa (X1)
};

REGISTRA_MCU(MSP432P4, [](const char* nm, DebugCaps d, const Cableado& c,
                          const McuDesc& desc) -> mcu_if* {
    return new McuP4(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_MSP432P4_MCU_H
