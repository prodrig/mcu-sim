// =============================================================================
// esp32c3_mcu.h — El ADAPTADOR del ESP32-C3 a `mcu_if`, y su alta en la factoría
//
// E3 de doc/analisis_esp32_riscv.md. Lo que cambia respecto a los ARM, dicho
// aquí y no en `sim`:
//
//   * los pines se llaman GPIO0…GPIO21 (y IOn, MTMS…MTDO, U0RXD/U0TXD); la
//     alimentación, VDD3P3, VDD3P3_RTC, VDD_SPI, VDDA y GND, y el reset es
//     CHIP_PU (activo a nivel bajo: «reset_pin» lo baja);
//   * no hay SWD: ni swclk ni swdio. El JTAG (MTMS-MTDO) y el DM: E6;
//   * el FIRMWARE es un ELF de RISC-V que se carga en la SRAM y arranca en su
//     entrada (sin ROM, sin Flash: E4 traerá la imagen de esptool y la Flash);
//   * `efuse=fichero` en el <mcu>: los registros de lectura del eFuse.
//
// La familia que despacha la factoría es "ESP32C3". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_ESP32C3_ESP32C3_MCU_H
#define STM32_SOC_ESP32C3_ESP32C3_MCU_H

#include <cstdio>
#include "../mcu_if.h"
#include "esp32c3.h"
#include "../../parts/netlist.h"
#include "../../common/nombres_nodo.h"

namespace stm32 {

class McuC3 : public mcu_if {
public:
    McuC3(const char* nm, DebugCaps, const Cableado& cab, const McuDesc& d)
        : desc_(d) {
        const McuCapsC3& m = *static_cast<const McuCapsC3*>(d.propio);
        chip_ = new Esp32C3(nm, m, cab);
        auto net = [&](const char* n) -> analog_net_if* {
            if (analog_net_if* x = cab.busca_alim(n)) return x;
            propios_.push_back(new AnalogNet((std::string(nm) + "_" + n).c_str()));
            return propios_.back();
        };
        vdd_ = net("VDD3P3"); vdd_rtc_ = net("VDD3P3_RTC"); vdd_spi_ = net("VDD_SPI");
        vdda_ = net("VDDA"); gnd_ = net("GND");
        d_vdd_ = vdd_->register_driver("sim_vdd");
        d_gnd_ = gnd_->register_driver("sim_gnd");
        d_pu_ = chip_->chip_pu.register_driver("sim_chip_pu");
        chip_->chip_pu.set_hiz(d_pu_);
    }
    ~McuC3() override {
        delete chip_;
        for (AnalogNet* n : propios_) delete n;
    }
    Esp32C3& chip() { return *chip_; }

    const McuDesc& desc() const override { return desc_; }
    std::vector<std::string> limitaciones() const override { return chip_->limitaciones(); }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_ESP32C3);
        else                 c.fija(prefijo, &NOM_ESP32C3);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = *desc_.enc;
        for (unsigned i = 0; i < c3::N_GPIO; ++i) {
            const std::string canon = c3::canonico_c3(0, i);
            if (AnalogNet* a = chip_->gpio.propio[i]) a->nombre_esq = pre + canon;
            n.registra(pre + canon, chip_->gpio.nodo_de(i), true, e.bonded(0, i), e.nombre);
        }
        n.registra(pre + "VDD3P3", *vdd_);   n.registra(pre + "VDD3P3_RTC", *vdd_rtc_);
        n.registra(pre + "VDD_SPI", *vdd_spi_); n.registra(pre + "VDDA", *vdda_);
        n.registra(pre + "GND", *gnd_);
        n.registra(pre + "CHIP_PU", chip_->chip_pu);
    }
    analog_net_if& nodo_analogico(unsigned, unsigned pin) override {
        return chip_->gpio.nodo_de(pin < c3::N_GPIO ? pin : 0);
    }
    analog_net_if* nodo_swclk() override { return nullptr; }
    analog_net_if* nodo_swdio() override { return nullptr; }
    analog_net_if* nodo_depuracion(const std::string&) override { return nullptr; }

    void alimentacion_externa(bool si) override { externa_ = si; }
    void alimenta(bool on) override {
        if (externa_) vdd_->set_hiz(d_vdd_);
        else vdd_->set_drive(d_vdd_, on ? 3.3f : 0.0f, on ? 0.1f : 1.0f);
        gnd_->set_drive(d_gnd_, 0.0f, 0.1f);
        chip_->alimenta(on);
        if (!on) reset_pin(true);
    }
    void reset_pin(bool activo) override {
        if (activo) chip_->chip_pu.set_drive(d_pu_, 0.0f, 100.0f);
        else        chip_->chip_pu.set_hiz(d_pu_);
    }

    bool carga_firmware(const std::string& ruta) override {
        if (formato_fw(ruta) != FormatoFw::Elf) {
            std::fprintf(stderr, "  [fw] %s: el ESP32-C3 de esta fase solo carga ELF en la SRAM "
                         "(sin ROM ni Flash: las imagenes de esptool llegan en E4)\n", ruta.c_str());
            return false;
        }
        const std::string e = chip_->carga_elf(ruta);
        if (!e.empty()) { std::fprintf(stderr, "  [fw] %s\n", e.c_str()); return false; }
        return true;
    }
    void aparca_en_wfe() override { chip_->aparca_en_wfi(); }
    std::string sin_firmware() override {
        chip_->aparca_en_wfi();
        return "el nucleo se aparca en wfi (sin ROM: no hay modo descarga todavia)";
    }
    void set_ondas_reloj(bool) override {}
    bool tiene_gdb_interno() const override { return false; }
    void gdb_interno(bool, bool) override {}

    bool admite_opcion(const std::string& clave) const override { return clave == "efuse"; }
    std::string opcion(const std::string& clave, const std::string& valor) override {
        if (clave == "efuse") return chip_->efuse.carga(valor);
        return mcu_if::opcion(clave, valor);
    }
    std::string describe_opcion(const std::string& clave) const override {
        if (clave == "efuse") return chip_->efuse.ruta().empty() ? "MAC de omision" : chip_->efuse.ruta();
        return "";
    }

private:
    Esp32C3* chip_;
    McuDesc desc_;
    std::vector<AnalogNet*> propios_;
    analog_net_if *vdd_, *vdd_rtc_, *vdd_spi_, *vdda_, *gnd_;
    int d_vdd_ = -1, d_gnd_ = -1, d_pu_ = -1;
    bool externa_ = false;
};

REGISTRA_MCU(ESP32C3, [](const char* nm, DebugCaps d, const Cableado& c,
                         const McuDesc& desc) -> mcu_if* {
    return new McuC3(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_ESP32C3_ESP32C3_MCU_H
