// =============================================================================
// esp32c6_mcu.h — El ADAPTADOR del ESP32-C6 a `mcu_if`, y su alta en la factoría
//
// E9.5 de doc/analisis_esp32_riscv.md. Como el del C3 (esp32c3_mcu.h), con lo
// que cambia:
//
//   * los pines: GPIO0…GPIO30 (y IOn, MTMS…MTDO, U0TXD/U0RXD en GPIO16/17,
//     USB_D-/USB_D+ en GPIO12/13, los del SDIO y los de la Flash, SPICS0 en
//     GPIO24…); la alimentación, VDDA3P3, VDDPST1, VDDPST2, VDD_SPI, VDDA y
//     GND; el reset, CHIP_PU (activo a nivel bajo: «reset_pin» lo baja);
//   * el FIRMWARE, igual que el C3: una imagen .bin de la Flash (entera desde
//     0, o `fichero.bin@0x10000`), el `flash_args` de ESP-IDF, o un ELF
//     bare-metal en la HP SRAM que arranca SIN la ROM; sin firmware, la ROM
//     arranca y lee la Flash que haya;
//   * la Flash: la del encapsulado (C6FH4, C6FH8) o una pieza SpiFlash de la
//     placa con su CS en SPICS0 (GPIO24);
//   * `efuse=fichero` en el <mcu>; `usb=DESTINO` (o --usb): un PC en D+
//     (GPIO13) y D- (GPIO12) que enumera el USB-Serial-JTAG y lleva su
//     CDC-ACM al destino serie;
//   * la depuración (E9.6): el stub de GDB con el DM en directo
//     (depuracion="dap", G1), el JTAG en MTMS-MTDO para OpenOCD y CMSIS-DAP
//     (G2) si los eFuse lo eligen (`jtag=pines`: DIS_USB_JTAG quemado), y el
//     del USB-Serial-JTAG (de fábrica) a remote_bitbang con `usb=...,jtag=P`
//     (G3); lo que hace openocd-esp32 al parar y al reiniciar,
//     HerramientaEsp32C6 (soc/herramientas.h).
//
// La familia que despacha la factoría es "ESP32C6". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_ESP32C6_ESP32C6_MCU_H
#define STM32_SOC_ESP32C6_ESP32C6_MCU_H

#include <cstdio>
#include "../mcu_if.h"
#include "esp32c6.h"
#include "../../parts/netlist.h"
#include "../../common/nombres_nodo.h"
#include "../../parts/spi_flash.h"
#include "../../parts/puente_usb.h"
#include "../esp/programador_esp.h"
#include "../../rv/gdb/gdb_stub_rv.h"
#include "../herramientas.h"

namespace stm32 {

class McuC6 : public mcu_if {
public:
    McuC6(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d) : desc_(d) {
        const McuCapsC6& m = *static_cast<const McuCapsC6*>(d.propio);
        chip_ = new Esp32C6(nm, m, cab);
        auto net = [&](const char* n) -> analog_net_if* {
            if (analog_net_if* x = cab.busca_alim(n)) return x;
            propios_.push_back(new AnalogNet((std::string(nm) + "_" + n).c_str()));
            return propios_.back();
        };
        vdd_ = net("VDDA3P3"); vdd_pst1_ = net("VDDPST1"); vdd_pst2_ = net("VDDPST2");
        vdd_spi_ = net("VDD_SPI"); vdda_ = net("VDDA"); gnd_ = net("GND");
        d_vdd_ = vdd_->register_driver("sim_vdd");
        d_gnd_ = gnd_->register_driver("sim_gnd");
        d_pu_ = chip_->chip_pu.register_driver("sim_chip_pu");
        chip_->chip_pu.set_hiz(d_pu_);
        // El `load` de GDB: por la ventana de la MMU
        prog_.traduce = [this](uint32_t a, uint32_t& off) { return chip_->flash() && chip_->cache.traduce(a, off); };
        prog_.tam_flash = [this] { return chip_->flash() ? chip_->flash()->tam() : 0u; };
        prog_.borra = [this](uint32_t off, uint32_t n) {
            if (SpiFlashChip* f = chip_->flash())
                for (uint32_t i = 0; i < n && off + i < f->tam(); ++i) f->datos()[off + i] = 0xFF;
        };
        prog_.escribe = [this](uint32_t off, const uint8_t* p, unsigned n) {
            if (SpiFlashChip* f = chip_->flash())
                for (unsigned i = 0; i < n && off + i < f->tam(); ++i) f->datos()[off + i] &= p[i];
        };
        prog_.invalida = [this] { chip_->cache.invalida_lineas(); };
        // G1: el stub de GDB con el DM en directo (depuracion="dap")
        if (dbg.attach == DebugAttach::Interno && dbg.puerto) {
            gdb_.reset(new GdbStubRv((std::string(nm) + "_gdb").c_str(), *chip_->dm, dbg.puerto, "gdb-c6"));
            gdb_->pon_programador(&prog_);
            gdb_->set_enabled(false);
        }
    }
    ~McuC6() override {
        puente_usb_.reset();
        gdb_.reset();
        delete chip_;
        for (AnalogNet* n : propios_) delete n;
    }
    Esp32C6& chip() { return *chip_; }

    const McuDesc& desc() const override { return desc_; }
    std::vector<congela::Nucleo> nucleos() override {
        Esp32C6* c = chip_;
        return {{std::string(c->name()) + ".core", [c] { return c->core.parado() && !c->core.en_reset(); },
                 [c](sc_core::sc_event_or_list& l) {
                     l |= c->core.ev_parado();
                     l |= c->core.rst_n.value_changed_event();
                 }}};
    }
    std::vector<std::string> limitaciones() const override { return chip_->limitaciones(); }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_ESP32C6);
        else                 c.fija(prefijo, &NOM_ESP32C6);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = *desc_.enc;
        for (unsigned i = 0; i < c6::N_GPIO; ++i) {
            const std::string canon = c6::canonico_c6(0, i);
            if (AnalogNet* a = chip_->gpio.propio[i]) a->nombre_esq = pre + canon;
            n.registra(pre + canon, chip_->gpio.nodo_de(i), true, e.bonded(0, i), e.nombre);
        }
        n.registra(pre + "VDDA3P3", *vdd_);   n.registra(pre + "VDDPST1", *vdd_pst1_);
        n.registra(pre + "VDDPST2", *vdd_pst2_); n.registra(pre + "VDD_SPI", *vdd_spi_);
        n.registra(pre + "VDDA", *vdda_);     n.registra(pre + "GND", *gnd_);
        n.registra(pre + "CHIP_PU", chip_->chip_pu);
    }
    analog_net_if& nodo_analogico(unsigned, unsigned pin) override {
        return chip_->gpio.nodo_de(pin < c6::N_GPIO ? pin : 0);
    }
    analog_net_if* nodo_swclk() override { return nullptr; }
    analog_net_if* nodo_swdio() override { return nullptr; }
    // El JTAG en sus pines: MTCK (GPIO6), MTMS (GPIO4), MTDI (GPIO5), MTDO
    // (GPIO7); el reset que tira la sonda, CHIP_PU
    analog_net_if* nodo_depuracion(const std::string& h) override {
        if (h == "tck") return &chip_->gpio.nodo_de(6);
        if (h == "tms") return &chip_->gpio.nodo_de(4);
        if (h == "tdi") return &chip_->gpio.nodo_de(5);
        if (h == "tdo") return &chip_->gpio.nodo_de(7);
        if (h == "srst") return &chip_->chip_pu;
        return nullptr;
    }

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
        engancha_flash();
        std::string e;
        std::string f = ruta;
        uint32_t off = 0;
        const size_t at = ruta.rfind('@');
        if (at != std::string::npos && at + 1 < ruta.size()) {
            f = ruta.substr(0, at);
            off = uint32_t(std::strtoul(ruta.c_str() + at + 1, nullptr, 0));
        }
        const std::string base = f.substr(f.find_last_of("/\\") + 1);
        if (base == "flash_args") e = carga_flash_args(f);
        else if (formato_fw(f) == FormatoFw::Elf) {
            if (elf_en_flash(f))
                e = f + ": es un ELF de ESP-IDF (con segmentos en la Flash): se descarga con sus .bin, "
                    "como en la placa: el flash_args de la compilacion o la imagen de esptool merge-bin";
            else e = chip_->carga_elf(f);
        } else if (formato_fw(f) == FormatoFw::Bin) e = chip_->carga_imagen(f, off);
        else e = f + ": el ESP32-C6 carga imagenes .bin de la Flash, el flash_args de ESP-IDF o un ELF bare-metal";
        if (!e.empty()) { std::fprintf(stderr, "  [fw] %s\n", e.c_str()); return false; }
        return true;
    }
    void aparca_en_wfe() override { chip_->aparca_en_wfi(); }
    std::string sin_firmware() override {
        engancha_flash();
        chip_->arranca_por_rom();
        return chip_->flash() ? "arranca la ROM y lee lo que haya en la Flash"
                              : "arranca la ROM; no hay Flash (ni en el encapsulado ni en SPICS0)";
    }
    memoria_nv_if* memoria_nv() override { return chip_->flash_interna(); }
    void set_ondas_reloj(bool) override {}
    herramienta_if* herramienta() override { return &HerramientaEsp32C6::el(); }
    void herramienta_interno(herramienta_if* h) override { if (gdb_) gdb_->pon_herramienta(h); }
    bool tiene_gdb_interno() const override { return gdb_ != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!gdb_) return;
        gdb_->set_verbose(traza);
        gdb_->set_enabled(activo);
    }
    programador_nv_if* programador() override { return &prog_; }

    bool admite_opcion(const std::string& clave) const override {
        return clave == "efuse" || clave == "flash_rapida" || clave == "usb" || clave == "jtag";
    }
    std::string opcion(const std::string& clave, const std::string& valor) override {
        if (clave == "efuse") return chip_->efuse.carga(valor);
        // El USB-Serial-JTAG: un PC en D+ (GPIO13) y D- (GPIO12) que lo
        // enumera; su CDC-ACM va al destino serie y, con ",jtag=PUERTO", su
        // JTAG a un servidor remote_bitbang para OpenOCD (G3)
        if (clave == "usb") {
            std::string v = valor;
            unsigned pj = 0;
            const size_t c = v.find(",jtag=");
            if (c != std::string::npos) { pj = unsigned(std::atoi(v.c_str() + c + 6)); v = v.substr(0, c); }
            const serie::Destino d = serie::parsea(v);
            if (!d.valido) return d.error;
            if (puente_usb_) return "el USB ya tiene su puente";
            puente_usb_.reset(new PuenteUsbCdc(chip_->gpio.nodo_de(13), chip_->gpio.nodo_de(12), &chip_->usj, d));
            puente_usb_->ev_in = &chip_->usj.ev_in_listo();
            if (!puente_usb_->error().empty()) return puente_usb_->error();
            if (pj) { const std::string e = puente_usb_->pon_jtag(pj); if (!e.empty()) return e; jtag_usb_ = pj; }
            return "";
        }
        if (clave == "jtag") {
            if (valor != "pines" && valor != "usb")
                return "jtag=" + valor + ": pines (EFUSE_DIS_USB_JTAG quemado) o usb (de fabrica)";
            jtag_ = valor;
            chip_->jtag_en_pines(valor == "pines");
            return "";
        }
        if (clave == "flash_rapida") {
            engancha_flash();
            if (!chip_->flash()) return "no hay Flash";
            chip_->flash()->pon_escala_tiempos(valor == "no" ? 1.0 : 0.01);
            return "";
        }
        return mcu_if::opcion(clave, valor);
    }
    std::string describe_opcion(const std::string& clave) const override {
        if (clave == "efuse") return chip_->efuse.ruta().empty() ? "MAC de omision" : chip_->efuse.ruta();
        if (clave == "usb")
            return puente_usb_ ? puente_usb_->describir() + (jtag_usb_ ? ", y su JTAG por remote_bitbang en localhost:" +
                                                                          std::to_string(jtag_usb_) : std::string()) : "";
        if (clave == "jtag") return jtag_ == "pines" ? "en los pines MTMS-MTDO (EFUSE_DIS_USB_JTAG quemado)"
                                                     : "en el USB-Serial-JTAG (de fabrica)";
        if (clave == "flash_rapida") return "programar y borrar la Flash, al 1 % de lo de la hoja de datos";
        return "";
    }
    PuenteUsbCdc* puente_usb() { return puente_usb_.get(); }

private:
    // La Flash de la placa: la pieza SpiFlash cuyo CS va a SPICS0 (GPIO24)
    void engancha_flash() {
        if (chip_->flash()) return;
        if (SpiFlashChip* f = RegistroFlashSpi::busca(&chip_->gpio.nodo_de(24))) chip_->pon_flash(f);
    }
    // ¿Tiene el ELF segmentos en la ventana de la caché?
    static bool elf_en_flash(const std::string& ruta) {
        std::vector<TrozoFw> t;
        if (!lee_elf(ruta, t, ELF_RISCV).empty()) return false;
        for (const TrozoFw& x : t)
            if (x.dir >= c6::FLASH_B && x.dir < c6::FLASH_B + c6::FLASH_TAM) return true;
        return false;
    }
    std::string carga_flash_args(const std::string& ruta) {
        std::FILE* f = std::fopen(ruta.c_str(), "r");
        if (!f) return "no se puede leer " + ruta;
        const size_t b = ruta.find_last_of("/\\");
        const std::string dir = b == std::string::npos ? std::string() : ruta.substr(0, b + 1);
        char l[1024];
        unsigned n = 0;
        std::string e;
        while (e.empty() && std::fgets(l, sizeof l, f)) {
            char nom[900];
            unsigned long off = 0;
            if (std::sscanf(l, "0x%lx %899s", &off, nom) == 2) {
                e = chip_->carga_imagen(dir + nom, uint32_t(off));
                ++n;
            }
        }
        std::fclose(f);
        if (e.empty() && !n) e = ruta + ": ninguna imagen (se esperan lineas «0x0 fichero.bin»)";
        return e;
    }
    Esp32C6* chip_;
    McuDesc desc_;
    ProgramadorEsp prog_;
    std::unique_ptr<GdbStubRv> gdb_;
    std::string jtag_ = "usb";
    unsigned jtag_usb_ = 0;
    std::unique_ptr<PuenteUsbCdc> puente_usb_;
    std::vector<AnalogNet*> propios_;
    analog_net_if *vdd_, *vdd_pst1_, *vdd_pst2_, *vdd_spi_, *vdda_, *gnd_;
    int d_vdd_ = -1, d_gnd_ = -1, d_pu_ = -1;
    bool externa_ = false;
};

REGISTRA_MCU(ESP32C6, [](const char* nm, DebugCaps d, const Cableado& c,
                         const McuDesc& desc) -> mcu_if* {
    return new McuC6(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_ESP32C6_ESP32C6_MCU_H
