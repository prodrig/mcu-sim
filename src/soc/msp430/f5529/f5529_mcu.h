// =============================================================================
// f5529_mcu.h — El ADAPTADOR del MSP430F5529 a `mcu_if`, y su alta en la
// factoría (M4: el F5529 en el binario `mcu-sim` y en las placas)
//
// Como el del G2553 (g2553_mcu.h), con lo que cambia:
//   * los PINES son P1.0…P8.7 y PJ.0…PJ.3 (el JTAG de 4 hilos);
//   * --gdb-dap: el stub contra el núcleo, con el programador de la Flash de
//     5xx (512 B en la principal, hasta 0x243FF; 128 B en la información);
//   * --gdb por los pines (el JTAG de Xv2) queda para cuando la sonda de
//     bancada hable Xv2: hasta entonces, --gdb-dap.
//
// La familia que despacha la factoría es "MSP430F5". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_MSP430_F5529_F5529_MCU_H
#define STM32_SOC_MSP430_F5529_F5529_MCU_H

#include <cstdio>
#include <memory>
#include "../../mcu_if.h"
#include "soc_f5529.h"
#include "../../../pins/nomenclatura.h"
#include "../../../parts/netlist.h"
#include "../../../common/nombres_nodo.h"
#include "../../../m430/gdb/gdb_m430.h"

namespace stm32 {

// --- Encapsulados [DS SLAS590P, «Pin Designations»] -------------------------------
// LQFP-80 (el de la F5529LP, MSP430F5529IPN): P1-P7 enteros, P8.0-P8.2 y PJ.0-PJ.3
inline constexpr Encapsulado ENC_F5529_PN80{"LQFP80", 80, 63,
    {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x07u, 0, 0, 0x0Fu}, true};

namespace f5 {
inline bool pad_f5(const std::string& t, unsigned& puerto, unsigned& pin) {
    std::string s = t;
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (s.size() == 4 && s[0] == 'P' && s[2] == '.' && s[3] >= '0' && s[3] <= '7') {
        if (s[1] >= '1' && s[1] <= '8') { puerto = unsigned(s[1] - '1'); pin = unsigned(s[3] - '0'); return true; }
        if (s[1] == 'J' && s[3] <= '3') { puerto = 10; pin = unsigned(s[3] - '0'); return true; }
    }
    return false;
}
inline std::string canonico_f5(unsigned puerto, unsigned pin) {
    if (puerto == 10) return "PJ." + std::to_string(pin);
    return "P" + std::to_string(puerto + 1) + "." + std::to_string(pin);
}
inline const char* const ALIM_F5[] = {"DVCC", "DVSS", "AVCC", "AVSS", "VUSB", "V18", "VBUS", "RST", "TEST", nullptr};
} // namespace f5

inline const Nomenclatura NOM_MSP430F5{"MSP430F5", &f5::pad_f5, &f5::canonico_f5, f5::ALIM_F5};
struct RegistroNomF5 {
    RegistroNomF5() { ContextoNomenclatura::g().registra_tipo("MSP430F5", &NOM_MSP430F5); }
};
inline const RegistroNomF5 g_registro_nom_f5{};

struct McuCapsF5 { const char* nombre; Encapsulado enc; };
inline constexpr McuCapsF5 MCU_MSP430F5529{"MSP430F5529", ENC_F5529_PN80};
struct RegistroCatalogoF5 {
    RegistroCatalogoF5() { CatalogoMcu::registra(McuDesc{MCU_MSP430F5529.nombre, "MSP430F5", &MCU_MSP430F5529.enc, &MCU_MSP430F5529}); }
};
inline const RegistroCatalogoF5 g_registro_catalogo_f5{};

class McuF5529 : public mcu_if {
public:
    McuF5529(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d) : desc_(d) {
        chip_ = new SocF5529(nm, &cab);
        chip_->monitor_uart = false;                 // el backchannel es el PuenteSerie de la placa
        auto net = [&](const char* n) -> analog_net_if* {
            if (analog_net_if* x = cab.busca_alim(n)) return x;
            propios_.push_back(new AnalogNet((std::string(nm) + "_" + n).c_str()));
            return propios_.back();
        };
        dvcc_ = net("DVCC"); dvss_ = net("DVSS");
        d_dvcc_ = dvcc_->register_driver("sim_dvcc");
        d_dvss_ = dvss_->register_driver("sim_dvss");
        d_rst_ = chip_->rst.register_driver("sim_rst");
        chip_->rst.set_hiz(d_rst_);
        if (dbg.attach == DebugAttach::Interno && dbg.puerto) {
            directo_.reset(new DbgDirectoM430(chip_->nucleo, chip_->eem));
            SocF5529* c = chip_;
            directo_->al_reset = [c] { c->reset(true); sc_core::wait(1, sc_core::SC_US); c->reset(false); };
            directo_->tam_de = [](uint32_t a) { return (a >= 0x1800 && a < 0x1A00) ? 128u : 512u; };
            gdb_.reset(new GdbStubM430((std::string(nm) + "_gdb").c_str(), *directo_, dbg.puerto, "gdb-430"));
            gdb_->pon_flash_5xx(0x4400, 0x243FF, 0x1800, 0x19FF);
            gdb_->set_enabled(false);
        }
    }
    ~McuF5529() override {
        gdb_.reset(); directo_.reset();
        delete chip_;
        for (AnalogNet* n : propios_) delete n;
    }
    SocF5529& chip() { return *chip_; }
    const McuDesc& desc() const override { return desc_; }

    std::vector<congela::Nucleo> nucleos() override {
        SocF5529* c = chip_;
        return {{std::string(c->name()) + ".nucleo", [c] { return c->nucleo.parado() && !c->nucleo.en_reset(); },
                 [c](sc_core::sc_event_or_list& l) {
                     l |= c->nucleo.ev_parado();
                     l |= c->rst_n.value_changed_event();
                 }}};
    }
    std::vector<std::string> limitaciones() const override {
        return {"Comp_B, RTC_A y el USB (M5) estan declarados: contestan y avisan",
                "el BSL de Flash no esta (M9); con el vector de reset borrado, el chip no arranca nada",
                "--gdb por los pines (JTAG de Xv2) aun no: use --gdb-dap"};
    }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_MSP430F5);
        else                 c.fija(prefijo, &NOM_MSP430F5);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = *desc_.enc;
        for (unsigned p = 0; p < 8; ++p)
            for (unsigned i = 0; i < 8; ++i) {
                const std::string canon = f5::canonico_f5(p, i);
                analog_net_if& a = chip_->p(p + 1, i);
                if (AnalogNet* x = dynamic_cast<AnalogNet*>(&a)) x->nombre_esq = pre + canon;
                n.registra(pre + canon, a, true, e.bonded(p, i), e.nombre);
            }
        for (unsigned i = 0; i < 4; ++i) {
            const std::string canon = f5::canonico_f5(10, i);
            analog_net_if& a = chip_->pj(i);
            if (AnalogNet* x = dynamic_cast<AnalogNet*>(&a)) x->nombre_esq = pre + canon;
            n.registra(pre + canon, a, true, true, e.nombre);
        }
        n.registra(pre + "DVCC", *dvcc_);
        n.registra(pre + "DVSS", *dvss_);
        n.registra(pre + "RST", chip_->rst, true);
        n.registra(pre + "TEST", chip_->test, true);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        if (puerto == 10) return chip_->pj(pin & 3u);
        return chip_->p((puerto < 8 ? puerto : 0) + 1, pin & 7u);
    }
    analog_net_if* nodo_swclk() override { return nullptr; }
    analog_net_if* nodo_swdio() override { return nullptr; }
    analog_net_if* nodo_depuracion(const std::string& h) override {
        if (h == "sbwtck" || h == "test") return &chip_->test;
        if (h == "sbwtdio" || h == "rst" || h == "srst") return &chip_->rst;
        return nullptr;
    }

    void alimentacion_externa(bool si) override { externa_ = si; }
    void alimenta(bool on) override {
        if (externa_) dvcc_->set_hiz(d_dvcc_);
        else dvcc_->set_drive(d_dvcc_, on ? 3.3f : 0.0f, on ? 0.1f : 1.0f);
        dvss_->set_drive(d_dvss_, 0.0f, 0.1f);
        chip_->reset(!on);
    }
    void reset_pin(bool activo) override {
        if (activo) chip_->rst.set_drive(d_rst_, 0.0f, 100.0f);
        else        chip_->rst.set_hiz(d_rst_);
    }

    bool carga_firmware(const std::string& ruta) override {
        const std::string e = chip_->carga(ruta);
        if (!e.empty()) { std::fprintf(stderr, "  [fw] %s\n", e.c_str()); return false; }
        return true;
    }
    // Sin firmware: el perro parado y LPM4
    void aparca_en_wfe() override {
        static const uint16_t p[] = {0x40B2, 0x5A80, 0x015C,     // mov #WDTPW|WDTHOLD,&WDTCTL
                                     0xD032, 0x00F0,             // bis #LPM4,SR
                                     0x3FFD};                    // jmp $-4
        for (unsigned i = 0; i < 6; ++i) poke16(0x4400u + 2 * i, p[i]);
        poke16(0xFFFE, 0x4400);
    }
    std::string sin_firmware() override { aparca_en_wfe(); return "el nucleo para el perro y duerme en LPM4"; }
    void set_ondas_reloj(bool) override {}
    bool tiene_gdb_interno() const override { return gdb_ != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!gdb_) return;
        gdb_->set_verbose(traza);
        gdb_->set_enabled(activo);
    }

private:
    SocF5529* chip_;
    McuDesc desc_;
    std::vector<AnalogNet*> propios_;
    analog_net_if* dvcc_ = nullptr;
    analog_net_if* dvss_ = nullptr;
    int d_dvcc_ = -1, d_dvss_ = -1, d_rst_ = -1;
    bool externa_ = false;
    std::unique_ptr<DbgDirectoM430> directo_;
    std::unique_ptr<GdbStubM430> gdb_;

    void poke16(uint32_t a, uint16_t v) { chip_->poke(a, uint8_t(v)); chip_->poke(a + 1, uint8_t(v >> 8)); }
};

REGISTRA_MCU(MSP430F5, [](const char* nm, DebugCaps d, const Cableado& c, const McuDesc& desc) -> mcu_if* {
    return new McuF5529(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_MSP430_F5529_F5529_MCU_H
