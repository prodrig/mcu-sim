// =============================================================================
// msp432e4_mcu.h — El ADAPTADOR del MSP432E4 a `mcu_if`, y su alta en la factoría
//
// Lo mismo que `soc/msp432p4/msp432p4_mcu.h`, con lo que cambia en un Tiva:
//
//   * los PINES se llaman `PA0`…`PQ7`, sin I ni O; la alimentación VDD/GND y
//     VDDA/GNDA, y RST es la patilla de reset;
//   * SWCLK y SWDIO son PC0 y PC1, pines de PUERTO en su función 1, como PA14
//     y PA13 en un STM32: la sonda se cuelga de sus nodos;
//   * el FIRMWARE va a la Flash en 0x0, en cualquiera de los cuatro formatos.
//
// La familia que despacha la factoría es "MSP432E4". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_MSP432E4_MCU_H
#define STM32_SOC_MSP432E4_MCU_H

#include <cstdio>
#include "../mcu_if.h"
#include "msp432e401y.h"
#include "../../parts/netlist.h"
#include "../../common/imagen_fw.h"

namespace stm32 {

class McuE4 : public mcu_if {
public:
    McuE4(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d)
        : chip_(new Msp432E4(nm, dbg, cab, *static_cast<const McuCapsE4*>(d.propio))),
          desc_(d) {
        d_vdd_ = chip_->vdd->register_driver("sim_vdd");
        d_gnd_ = chip_->gnd->register_driver("sim_gnd");
        d_vdda_ = chip_->vdda->register_driver("sim_vdda");
        d_gnda_ = chip_->gnda->register_driver("sim_gnda");
        d_rst_ = chip_->rst->register_driver("sim_rst");
    }
    ~McuE4() override { delete chip_; }

    Msp432E4& chip() { return *chip_; }
    const McuDesc& desc() const override { return desc_; }
    std::vector<std::string> limitaciones() const override { return chip_->limitaciones(); }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_MSP432E4);
        else                 c.fija(prefijo, &NOM_MSP432E4);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = chip_->mcu.enc;
        for (unsigned p = 0; p < e4::N_PUERTOS; ++p)
            for (unsigned i = 0; i < e4::N_PINES; ++i) {
                const std::string canon = e4::canonico_e4(p, i);
                if (AnalogNet* a = chip_->gpio.propio[p][i]) a->nombre_esq = pre + canon;
                n.registra(pre + canon, chip_->gpio.nodo_de(p, i), true,
                           e.bonded(p, i), e.nombre);
            }
        n.registra(pre + "VDD", *chip_->vdd);     n.registra(pre + "GND", *chip_->gnd);
        n.registra(pre + "VDDA", *chip_->vdda);   n.registra(pre + "GNDA", *chip_->gnda);
        n.registra(pre + "VBAT", *chip_->vbat);   n.registra(pre + "RST", *chip_->rst);
        n.registra(pre + "OSC0", *chip_->osc0);   n.registra(pre + "OSC1", *chip_->osc1);
        n.registra(pre + "XOSC0", *chip_->xosc0); n.registra(pre + "XOSC1", *chip_->xosc1);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        return chip_->gpio.nodo_de(puerto, pin);
    }
    analog_net_if* nodo_swclk() override { return &chip_->gpio.nodo_de(e4::PC, 0); }
    analog_net_if* nodo_swdio() override { return &chip_->gpio.nodo_de(e4::PC, 1); }

    void alimenta(bool on) override {
        if (on) {
            chip_->vdd->set_drive(d_vdd_, 3.3f, 0.1f);
            chip_->vdda->set_drive(d_vdda_, 3.3f, 0.1f);
            chip_->gnd->set_drive(d_gnd_, 0.0f, 0.1f);
            chip_->gnda->set_drive(d_gnda_, 0.0f, 0.1f);
        } else {
            chip_->vdd->set_drive(d_vdd_, 0.0f, 1.0f);
            chip_->vdda->set_drive(d_vdda_, 0.0f, 1.0f);
            reset_pin(true);
        }
    }
    void reset_pin(bool activo) override {
        if (activo) chip_->rst->set_drive(d_rst_, 0.0f, 100.0f);
        else        chip_->rst->set_hiz(d_rst_);
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
                TrozoFw tr; tr.dir = e4::FLASH_B;
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
    void aparca_en_wfe() override {
        auto w32 = [&](uint32_t a, uint32_t v) {
            for (unsigned i = 0; i < 4; ++i) chip_->poke8(a + i, uint8_t(v >> (8 * i)));
        };
        w32(e4::FLASH_B + 0, e4::SRAM_B + chip_->mcu.sram_size);
        w32(e4::FLASH_B + 4, (e4::FLASH_B + 0x100u) | 1u);
        w32(e4::FLASH_B + 0x100u, 0xE7FDBF20u);
    }
    void set_ondas_reloj(bool on) override { chip_->set_ondas(on); }
    programador_nv_if* programador() override { return &ProgramadorE4::el(); }
    bool admite_opcion(const std::string& clave) const override { return clave == "rom"; }
    std::string opcion(const std::string& clave, const std::string& valor) override {
        if (clave == "rom") {
            if (valor == "r1" || valor == "R1") {
                if (!chip_->rom.hay_r1()) return "este simulador no lleva la ROM R1 del E4";
                chip_->rom.pon_r1(); return "";
            }
            if (valor == "ninguna" || valor == "r3" || valor == "R3") {
                chip_->rom.pon_ninguna(); return "";
            }
            return chip_->rom.pon_volcado(valor);
        }
        return mcu_if::opcion(clave, valor);
    }
    std::string describe_opcion(const std::string& clave) const override {
        if (clave != "rom") return "";
        char h[16];
        std::snprintf(h, sizeof h, "0x%08X", unsigned(chip_->rom.huella()));
        std::string d = chip_->rom.nombre_origen();
        if (!chip_->rom.ruta().empty()) d += ", " + chip_->rom.ruta();
        if (chip_->rom.presente()) d += std::string(", huella ") + h;
        return d;
    }
    bool tiene_gdb_interno() const override { return chip_->core.gdb != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!chip_->core.gdb) return;
        chip_->core.gdb->set_verbose(traza);
        chip_->core.gdb->set_enabled(activo);
    }

private:
    Msp432E4* chip_;
    McuDesc   desc_;
    int d_vdd_ = -1, d_gnd_ = -1, d_vdda_ = -1, d_gnda_ = -1, d_rst_ = -1;
};

REGISTRA_MCU(MSP432E4, [](const char* nm, DebugCaps d, const Cableado& c,
                          const McuDesc& desc) -> mcu_if* {
    return new McuE4(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_MSP432E4_MCU_H
