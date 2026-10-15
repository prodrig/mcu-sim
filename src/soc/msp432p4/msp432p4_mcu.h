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

namespace stm32 {

class McuP4 : public mcu_if {
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
    const McuDesc& desc() const override { return desc_; }
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
        n.registra(pre + "SWCLKTCK", *chip_->swclk_net);
        n.registra(pre + "SWDIOTMS", *chip_->swdio_net);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        return chip_->dio.nodo_de(puerto, pin);
    }
    analog_net_if* nodo_swclk() override { return chip_->swclk_net; }
    analog_net_if* nodo_swdio() override { return chip_->swdio_net; }

    void alimenta(bool on) override {
        if (on) {
            chip_->dvcc->set_drive(d_dvcc_, 3.3f, 0.1f);
            chip_->avcc->set_drive(d_avcc_, 3.3f, 0.1f);
            chip_->dvss->set_drive(d_dvss_, 0.0f, 0.1f);
            chip_->avss->set_drive(d_avss_, 0.0f, 0.1f);
        } else {
            chip_->dvcc->set_drive(d_dvcc_, 0.0f, 1.0f);
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
    void set_ondas_reloj(bool on) override { chip_->set_ondas(on); }
    programador_nv_if* programador() override { return &ProgramadorP4::el(); }
    bool tiene_gdb_interno() const override { return chip_->core.gdb != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!chip_->core.gdb) return;
        chip_->core.gdb->set_verbose(traza);
        chip_->core.gdb->set_enabled(activo);
    }

private:
    Msp432P4* chip_;
    McuDesc   desc_;
    int d_dvcc_ = -1, d_dvss_ = -1, d_avcc_ = -1, d_avss_ = -1, d_rstn_ = -1;
};

REGISTRA_MCU(MSP432P4, [](const char* nm, DebugCaps d, const Cableado& c,
                          const McuDesc& desc) -> mcu_if* {
    return new McuP4(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_MSP432P4_MCU_H
