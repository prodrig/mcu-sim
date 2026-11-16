// =============================================================================
// g2553_mcu.h — El ADAPTADOR del MSP430G2553 a `mcu_if`, y su alta en la
// factoría (P-20: el G2553 en el binario `mcu-sim` y en las placas)
//
// Como los de los MSP432, con lo que cambia en un MSP430 dicho aquí:
//
//   * los PINES se llaman P1.0…P3.7 (P3 solo sale en el TSSOP-28), y la
//     alimentación y el control, DVCC, DVSS, RST (RST/NMI/SBWTDIO) y TEST
//     (TEST/SBWTCK);
//   * no hay SWD: la depuración por los pines es Spy-Bi-Wire en TEST/RST, y
//     el stub de pines lo hace el propio chip (`stub_pines`): la sonda de
//     bancada de m430/sonda, el `DbgSondaM430` y el programador de la Flash
//     de 2xx; el de --gdb-dap, `DbgDirectoM430` sobre el núcleo;
//   * el FIRMWARE va a la Flash, en ELF o TI-TXT; sin firmware, un programa
//     de tres instrucciones que para el perro y duerme en LPM4.
//
// La familia que despacha la factoría es "MSP430G2". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_MSP430_G2553_G2553_MCU_H
#define STM32_SOC_MSP430_G2553_G2553_MCU_H

#include <cstdio>
#include <memory>
#include "../../mcu_if.h"
#include "soc_g2553.h"
#include "../../../pins/nomenclatura.h"
#include "../../../parts/netlist.h"
#include "../../../common/nombres_nodo.h"
#include "../../../m430/gdb/gdb_m430.h"
#include "../../../m430/sonda/sonda_m430.h"

namespace stm32 {

// --- Encapsulados [DS SLAS735J, tabla 4-1] --------------------------------------
// PDIP-20 (el de la G2ET, MSP430G2553IN20) y TSSOP-20: P1 y P2; TSSOP-28 y
// QFN-32: también P3.
inline constexpr Encapsulado ENC_G2553_N20{"PDIP20", 20, 16, {0x00FFu, 0x00FFu, 0x0000u}, true};
inline constexpr Encapsulado ENC_G2553_PW28{"TSSOP28", 28, 24, {0x00FFu, 0x00FFu, 0x00FFu}, true};

namespace g2 {
inline bool pad_g2(const std::string& t, unsigned& puerto, unsigned& pin) {
    std::string s = t;
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (s.size() == 4 && s[0] == 'P' && s[1] >= '1' && s[1] <= '3' && s[2] == '.' && s[3] >= '0' && s[3] <= '7') {
        puerto = unsigned(s[1] - '1'); pin = unsigned(s[3] - '0'); return true;
    }
    if (s == "XIN") { puerto = 1; pin = 6; return true; }
    if (s == "XOUT") { puerto = 1; pin = 7; return true; }
    return false;
}
inline std::string canonico_g2(unsigned puerto, unsigned pin) {
    return "P" + std::to_string(puerto + 1) + "." + std::to_string(pin);
}
inline const char* const ALIM_G2[] = {"DVCC", "DVSS", "RST", "TEST", nullptr};
} // namespace g2

inline const Nomenclatura NOM_MSP430G2{"MSP430G2", &g2::pad_g2, &g2::canonico_g2, g2::ALIM_G2};
struct RegistroNomG2 {
    RegistroNomG2() { ContextoNomenclatura::g().registra_tipo("MSP430G2", &NOM_MSP430G2); }
};
inline const RegistroNomG2 g_registro_nom_g2{};

struct McuCapsG2 { const char* nombre; Encapsulado enc; };
inline constexpr McuCapsG2 MCU_MSP430G2553{"MSP430G2553", ENC_G2553_N20};
inline constexpr McuCapsG2 MCU_MSP430G2553PW28{"MSP430G2553IPW28", ENC_G2553_PW28};
struct RegistroCatalogoG2 {
    RegistroCatalogoG2() {
        for (const McuCapsG2* m : {&MCU_MSP430G2553, &MCU_MSP430G2553PW28})
            CatalogoMcu::registra(McuDesc{m->nombre, "MSP430G2", &m->enc, m});
    }
};
inline const RegistroCatalogoG2 g_registro_catalogo_g2{};

class McuG2553 : public mcu_if {
public:
    McuG2553(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d) : desc_(d) {
        chip_ = new SocG2553(nm, &cab);
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
            SocG2553* c = chip_;
            directo_->al_reset = [c] { c->reset(true); sc_core::wait(1, sc_core::SC_US); c->reset(false); };
            gdb_.reset(new GdbStubM430((std::string(nm) + "_gdb").c_str(), *directo_, dbg.puerto, "gdb-430"));
            gdb_->pon_flash_2xx(0xC000, 0x1000, 0x10C0);
            gdb_->set_enabled(false);
        }
    }
    ~McuG2553() override {
        gdb_.reset(); gdb_pines_.reset(); dbg_sonda_.reset(); sonda_.reset(); directo_.reset();
        delete chip_;
        for (AnalogNet* n : propios_) delete n;
    }
    SocG2553& chip() { return *chip_; }
    const McuDesc& desc() const override { return desc_; }

    std::vector<congela::Nucleo> nucleos() override {
        SocG2553* c = chip_;
        return {{std::string(c->name()) + ".nucleo", [c] { return c->nucleo.parado() && !c->nucleo.en_reset(); },
                 [c](sc_core::sc_event_or_list& l) {
                     l |= c->nucleo.ev_parado();
                     l |= c->rst_n.value_changed_event();
                 }}};
    }
    std::vector<std::string> limitaciones() const override {
        return {"USCI_B0 (SPI e I2C), Comp_A+ y el DTC del ADC10 estan declarados: contestan y avisan",
                "el BSL de ROM no esta (sin el codigo de TI se lee 0x3FFF); NMI por RST/NMI tampoco",
                "la sonda de la placa (eZ-FET) es de M11: --gdb va por SBW en TEST/RST con la sonda de bancada"};
    }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_MSP430G2);
        else                 c.fija(prefijo, &NOM_MSP430G2);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = *desc_.enc;
        for (unsigned p = 0; p < 3; ++p)
            for (unsigned i = 0; i < 8; ++i) {
                const std::string canon = g2::canonico_g2(p, i);
                analog_net_if& a = chip_->p(p + 1, i);
                if (AnalogNet* x = dynamic_cast<AnalogNet*>(&a)) x->nombre_esq = pre + canon;
                n.registra(pre + canon, a, true, e.bonded(p, i), e.nombre);
            }
        n.registra(pre + "DVCC", *dvcc_);
        n.registra(pre + "DVSS", *dvss_);
        n.registra(pre + "RST", chip_->rst, true);
        n.registra(pre + "TEST", chip_->test, true);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        return chip_->p((puerto < 3 ? puerto : 0) + 1, pin & 7u);
    }
    analog_net_if* nodo_swclk() override { return nullptr; }
    analog_net_if* nodo_swdio() override { return nullptr; }
    // Spy-Bi-Wire: "sbwtck" (TEST) y "sbwtdio" (RST); el JTAG de 4 hilos en
    // P1.4-P1.7 con TEST arriba
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
    // Sin firmware: el perro parado y LPM4 [de modelo, como el wfe de los ARM]
    void aparca_en_wfe() override {
        static const uint16_t p[] = {0x40B2, 0x5A80, 0x0120,     // mov #WDTPW|WDTHOLD,&WDTCTL
                                     0xD032, 0x00F0,             // bis #LPM4,SR
                                     0x3FFD};                    // jmp $-4
        for (unsigned i = 0; i < 6; ++i) poke16(0xC000u + 2 * i, p[i]);
        poke16(0xFFFE, 0xC000);
    }
    std::string sin_firmware() override { aparca_en_wfe(); return "el nucleo para el perro y duerme en LPM4"; }
    void set_ondas_reloj(bool) override {}
    bool tiene_gdb_interno() const override { return gdb_ != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!gdb_) return;
        gdb_->set_verbose(traza);
        gdb_->set_enabled(activo);
    }
    // --gdb: el stub por los pines, sobre la sonda de bancada por SBW
    GdbRspBase* stub_pines(const std::string& nombre, unsigned puerto) override {
        SondaM430::Pines p; p.test = &chip_->test; p.rst = &chip_->rst;
        sonda_.reset(new SondaM430(p, SondaM430::Modo::Sbw));
        dbg_sonda_.reset(new DbgSondaM430(*sonda_, chip_->eem.disparadores()));
        gdb_pines_.reset(new GdbStubM430(nombre.c_str(), *dbg_sonda_, puerto, "gdb-pines"));
        gdb_pines_->pon_flash_2xx(0xC000, 0x1000, 0x10C0);
        gdb_pines_->set_enabled(false);
        return gdb_pines_.get();
    }

private:
    SocG2553* chip_;
    McuDesc desc_;
    std::vector<AnalogNet*> propios_;
    analog_net_if* dvcc_ = nullptr;
    analog_net_if* dvss_ = nullptr;
    int d_dvcc_ = -1, d_dvss_ = -1, d_rst_ = -1;
    bool externa_ = false;
    std::unique_ptr<DbgDirectoM430> directo_;
    std::unique_ptr<GdbStubM430> gdb_, gdb_pines_;
    std::unique_ptr<SondaM430> sonda_;
    std::unique_ptr<DbgSondaM430> dbg_sonda_;

    void poke16(uint32_t a, uint16_t v) { chip_->poke(a, uint8_t(v)); chip_->poke(a + 1, uint8_t(v >> 8)); }
};

REGISTRA_MCU(MSP430G2, [](const char* nm, DebugCaps d, const Cableado& c, const McuDesc& desc) -> mcu_if* {
    return new McuG2553(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_MSP430_G2553_G2553_MCU_H
