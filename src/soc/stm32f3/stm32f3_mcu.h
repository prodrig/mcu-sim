// =============================================================================
// stm32f3_mcu.h — El ADAPTADOR del STM32F303 a `mcu_if`, y su alta en la
// factoría (fase S1)
//
// Lo que cambia frente al de los F4 es solo lo que el chip es: los nodos son
// los de siempre de un STM32 (`PinMux` y `PowerPads`: PA0..PF10, VDD, NRST,
// BOOT0...), y la sonda va a PA14/PA13 como en el F4; el firmware va a la
// Flash en 0x0800_0000, en los formatos que el cargador sabe leer (ELF, Intel
// HEX y binario, éste a la base de la Flash).
//
// La familia que despacha la factoría es "STM32F3". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_STM32F3_STM32F3_MCU_H
#define STM32_SOC_STM32F3_STM32F3_MCU_H

#include <cstdio>
#include "../mcu_if.h"
#include "stm32f303.h"
#include "../../parts/netlist.h"
#include "../../common/imagen_fw.h"

namespace stm32 {

class McuF3 : public mcu_if {
public:
    McuF3(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d)
        : chip_(new SocF3(nm, dbg, cab, *static_cast<const F3Caps*>(d.propio))), desc_(d) {
        d_vdd_  = chip_->pwr_pads.vdd.register_driver("sim_vdd");
        d_vss_  = chip_->pwr_pads.vss.register_driver("sim_vss");
        d_vdda_ = chip_->pwr_pads.vdda.register_driver("sim_vdda");
        d_nrst_ = chip_->pwr_pads.nrst.register_driver("sim_nrst");
        d_bt0_  = chip_->pwr_pads.boot0.register_driver("sim_boot0");
    }
    ~McuF3() override { delete chip_; }

    SocF3& chip() { return *chip_; }
    const McuDesc& desc() const override { return desc_; }
    std::vector<std::string> limitaciones() const override { return chip_->limitaciones(); }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        n.registra_mcu(prefijo, chip_->pinmux, chip_->pwr_pads);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        return chip_->pinmux.analog(puerto, pin);
    }

    void alimentacion_externa(bool si) override { externa_ = si; }
    void alimenta(bool on) override {
        if (externa_) chip_->pwr_pads.vdd.set_hiz(d_vdd_);
        if (on) {
            if (!externa_) chip_->pwr_pads.vdd.set_drive(d_vdd_, 3.3f, 0.1f);
            chip_->pwr_pads.vdda.set_drive(d_vdda_, 3.3f, 0.1f);
            chip_->pwr_pads.vss.set_drive(d_vss_, 0.0f, 0.1f);
        } else {
            if (!externa_) chip_->pwr_pads.vdd.set_drive(d_vdd_, 0.0f, 1.0f);
            chip_->pwr_pads.vdda.set_drive(d_vdda_, 0.0f, 1.0f);
            // BOOT0 abajo por resistencia: arranque desde la Flash. Una placa
            // que lo quiera alto (la Discovery con SB19 abierto) lo pone ella.
            chip_->pwr_pads.boot0.set_drive(d_bt0_, 0.0f, 10e3f);
            reset_pin(true);
        }
    }
    void reset_pin(bool activo) override {
        if (activo) chip_->pwr_pads.nrst.set_drive(d_nrst_, 0.0f, 100.0f);
        else        chip_->pwr_pads.nrst.set_hiz(d_nrst_);
    }
    // BOOT0 desde el banco: alto con poca resistencia, o suelto (vuelve a la
    // resistencia a masa del adaptador)
    void boot0(bool alto) {
        if (alto) chip_->pwr_pads.boot0.set_drive(d_bt0_, 3.3f, 100.0f);
        else      chip_->pwr_pads.boot0.set_drive(d_bt0_, 0.0f, 10e3f);
    }

    bool carga_firmware(const std::string& ruta) override {
        std::vector<TrozoFw> t;
        std::string e;
        switch (formato_fw(ruta)) {
            case FormatoFw::Elf:      e = lee_elf(ruta, t);  break;
            case FormatoFw::IntelHex: e = lee_ihex(ruta, t); break;
            case FormatoFw::TiTxt:    e = "el formato TI-TXT es de los MSP432"; break;
            case FormatoFw::Bin: {
                std::FILE* f = std::fopen(ruta.c_str(), "rb");
                if (!f) { e = "no se puede leer " + ruta; break; }
                TrozoFw tr; tr.dir = f3::FLASH_B;
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
                    std::fprintf(stderr, "  [fw] %s: 0x%08X no es ni Flash, ni SRAM ni CCM del "
                                 "%s\n", ruta.c_str(), unsigned(x.dir + i), desc_.nombre);
                    return false;
                }
        return true;
    }
    // Sin firmware: un vector de reset que apunta a un `wfe` en bucle
    void aparca_en_wfe() override {
        auto w32 = [&](uint32_t a, uint32_t v) {
            for (unsigned i = 0; i < 4; ++i) chip_->poke8(a + i, uint8_t(v >> (8 * i)));
        };
        w32(f3::FLASH_B + 0, f3::SRAM_B + chip_->mcu.sram_size);
        w32(f3::FLASH_B + 4, (f3::FLASH_B + 0x200u) | 1u);
        w32(f3::FLASH_B + 0x200u, 0xE7FDBF20u);
    }
    void set_ondas_reloj(bool on) override { chip_->set_ondas(on); }
    // La secuencia del FPEC para el `load` de GDB por los pines (S2)
    programador_nv_if* programador() override { return &ProgramadorF3::el(); }
    bool tiene_gdb_interno() const override { return chip_->core.gdb != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!chip_->core.gdb) return;
        chip_->core.gdb->set_verbose(traza);
        chip_->core.gdb->set_enabled(activo);
    }

private:
    SocF3*  chip_;
    McuDesc desc_;
    int d_vdd_ = -1, d_vss_ = -1, d_vdda_ = -1, d_nrst_ = -1, d_bt0_ = -1;
    bool externa_ = false;
};

REGISTRA_MCU(STM32F3, [](const char* nm, DebugCaps d, const Cableado& c,
                         const McuDesc& desc) -> mcu_if* {
    return new McuF3(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_STM32F3_STM32F3_MCU_H
