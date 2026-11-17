// =============================================================================
// fr2_mcu.h — El ADAPTADOR de las FR2xx/FR4xx a `mcu_if`, y su alta en la
// factoría (M6: el FR2433; M7: el FR4133...)
//
// Como el del F5529 (f5529_mcu.h), con lo que cambia:
//   * los PINES son los del encapsulado de cada chip (P1-P8 como mucho);
//   * la memoria es FRAM: el `load` de GDB la escribe abriendo SYSCFG0
//     (ProgramadorFram), por los dos caminos: --gdb-dap contra el núcleo y
//     --gdb por los pines (SBW en TEST/RST, la sonda de bancada con las
//     secuencias de Xv2).
//
// Un chip es su tabla (`ChipFr2`), su encapsulado y si su LaunchPad lleva
// cristal. La familia que despacha la factoría es "MSP430FR2". [soc/mcu_if.h]
// =============================================================================
#ifndef STM32_SOC_MSP430_FR2_FR2_MCU_H
#define STM32_SOC_MSP430_FR2_FR2_MCU_H

#include <cstdio>
#include <memory>
#include "../../mcu_if.h"
#include "../fr2433/soc_fr2433.h"
#include "../fr4133/soc_fr4133.h"
#include "../../../pins/nomenclatura.h"
#include "../../../parts/netlist.h"
#include "../../../common/nombres_nodo.h"
#include "../../../m430/gdb/gdb_m430.h"

namespace stm32 {

// --- Encapsulados [DS, «Pin Diagrams»] -------------------------------------------
// FR2433 VQFN-24 (el de la LaunchPad, MSP430FR2433IRGE): P1 y P2 enteros,
// P3.0-P3.2 [SLASE59F]
inline constexpr Encapsulado ENC_FR2433_RGE24{"VQFN24", 24, 19, {0xFFu, 0xFFu, 0x07u}, true};
// FR4133 LQFP-64 (el de la LaunchPad, MSP430FR4133IPM): P1-P7 enteros,
// P8.0-P8.3 [SLAS865F figura 7-1]
inline constexpr Encapsulado ENC_FR4133_PM64{"LQFP64", 64, 60, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x0Fu}, true};

namespace fr2 {
inline bool pad_fr2(const std::string& t, unsigned& puerto, unsigned& pin) {
    std::string s = t;
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (s.size() == 4 && s[0] == 'P' && s[2] == '.' && s[3] >= '0' && s[3] <= '7' && s[1] >= '1' && s[1] <= '8') {
        puerto = unsigned(s[1] - '1'); pin = unsigned(s[3] - '0');
        return true;                                // lo que existe lo dice el encapsulado
    }
    return false;
}
inline std::string canonico_fr2(unsigned puerto, unsigned pin) {
    return "P" + std::to_string(puerto + 1) + "." + std::to_string(pin);
}
inline const char* const ALIM_FR2[] = {"DVCC", "DVSS", "RST", "TEST", nullptr};
} // namespace fr2

inline const Nomenclatura NOM_MSP430FR2{"MSP430FR2", &fr2::pad_fr2, &fr2::canonico_fr2, fr2::ALIM_FR2};
struct RegistroNomFr2 {
    RegistroNomFr2() {
        ContextoNomenclatura::g().registra_tipo("MSP430FR2", &NOM_MSP430FR2);
        ContextoNomenclatura::g().registra_tipo("MSP430FR4", &NOM_MSP430FR2);   // el FR4133: la misma
    }
};
inline const RegistroNomFr2 g_registro_nom_fr2{};

// `cristal`: la LaunchPad lleva uno de 32 768 Hz en XIN/XOUT (y conectado)
struct McuCapsFr2 { const char* nombre; Encapsulado enc; const ChipFr2* chip; bool cristal; };
inline constexpr McuCapsFr2 MCU_MSP430FR2433{"MSP430FR2433", ENC_FR2433_RGE24, &CHIP_FR2433, false};
inline constexpr McuCapsFr2 MCU_MSP430FR4133{"MSP430FR4133", ENC_FR4133_PM64, &CHIP_FR4133, true};
struct RegistroCatalogoFr2 {
    RegistroCatalogoFr2() {
        for (const McuCapsFr2* m : {&MCU_MSP430FR2433, &MCU_MSP430FR4133})
            CatalogoMcu::registra(McuDesc{m->nombre, "MSP430FR2", &m->enc, m});
    }
};
inline const RegistroCatalogoFr2 g_registro_catalogo_fr2{};

class McuFR2 : public mcu_if {
public:
    McuFR2(const char* nm, DebugCaps dbg, const Cableado& cab, const McuDesc& d)
        : desc_(d), caps_(*static_cast<const McuCapsFr2*>(d.propio)) {
        chip_ = new SocFR2(nm, *caps_.chip, &cab);
        chip_->cristal_xt1 = caps_.cristal;
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
            SocFR2* c = chip_;
            directo_->al_reset = [c] { c->reset(true); sc_core::wait(1, sc_core::SC_US); c->reset(false); };
            directo_->despierta = [c] { c->despierta_por_depurador(); };
            gdb_.reset(new GdbStubM430((std::string(nm) + "_gdb").c_str(), *directo_, dbg.puerto, "gdb-430"));
            gdb_->pon_fram(chip_->fram_ini(), 0xFFFF, 0x1800, 0x19FF);
            gdb_->set_enabled(false);
        }
    }
    ~McuFR2() override {
        gdb_.reset(); gdb_pines_.reset(); dbg_sonda_.reset(); sonda_xv2_.reset(); sonda_.reset();
        directo_.reset();
        delete chip_;
        for (AnalogNet* n : propios_) delete n;
    }
    SocFR2& chip() { return *chip_; }
    const McuDesc& desc() const override { return desc_; }

    std::vector<congela::Nucleo> nucleos() override {
        SocFR2* c = chip_;
        return {{std::string(c->name()) + ".nucleo", [c] { return c->nucleo.parado() && !c->nucleo.en_reset(); },
                 [c](sc_core::sc_event_or_list& l) {
                     l |= c->nucleo.ev_parado();
                     l |= c->rst_n.value_changed_event();
                 }}};
    }
    std::vector<std::string> limitaciones() const override {
        return {"el BSL de ROM no esta (M10); con el vector de reset borrado, el chip no arranca nada",
                "la FRAM no tiene estados de espera ni cache en el tiempo: NWAITS solo avisa"};
    }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        ContextoNomenclatura& c = ContextoNomenclatura::g();
        if (prefijo.empty()) c.fija_desnudo(&NOM_MSP430FR2);
        else                 c.fija(prefijo, &NOM_MSP430FR2);
        apunta_mcu_en_placa(chip_);
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        const Encapsulado& e = *desc_.enc;
        for (unsigned p = 0; p < caps_.chip->npuertos; ++p)
            for (unsigned i = 0; i < 8; ++i) {
                if (!chip_->existe(p * 8 + i)) continue;
                const std::string canon = fr2::canonico_fr2(p, i);
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
        if (!chip_->existe(puerto * 8 + (pin & 7u))) return chip_->p(1, 0);
        return chip_->p(puerto + 1, pin & 7u);
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
        static const uint16_t p[] = {0x40B2, 0x5A80, 0x01CC,     // mov #WDTPW|WDTHOLD,&WDTCTL
                                     0xD032, 0x00F0,             // bis #LPM4,SR
                                     0x3FFD};                    // jmp $-4
        const uint32_t a = chip_->fram_ini();
        for (unsigned i = 0; i < 6; ++i) poke16(a + 2 * i, p[i]);
        poke16(0xFFFE, uint16_t(a));
    }
    std::string sin_firmware() override { aparca_en_wfe(); return "el nucleo para el perro y duerme en LPM4"; }
    void set_ondas_reloj(bool) override {}
    bool tiene_gdb_interno() const override { return gdb_ != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!gdb_) return;
        gdb_->set_verbose(traza);
        gdb_->set_enabled(activo);
    }
    // --gdb: el stub por los pines, la sonda de bancada por SBW en TEST/RST
    GdbRspBase* stub_pines(const std::string& nombre, unsigned puerto) override {
        SondaM430::Pines p; p.test = &chip_->test; p.rst = &chip_->rst;
        sonda_.reset(new SondaM430(p, SondaM430::Modo::Sbw));
        sonda_xv2_.reset(new SondaXv2(*sonda_, 0x2000));
        dbg_sonda_.reset(new DbgSondaXv2(*sonda_xv2_, chip_->eem.disparadores()));
        gdb_pines_.reset(new GdbStubM430(nombre.c_str(), *dbg_sonda_, puerto, "gdb-pines"));
        gdb_pines_->pon_fram(chip_->fram_ini(), 0xFFFF, 0x1800, 0x19FF);
        gdb_pines_->set_enabled(false);
        return gdb_pines_.get();
    }

private:
    SocFR2* chip_;
    McuDesc desc_;
    McuCapsFr2 caps_;
    std::vector<AnalogNet*> propios_;
    analog_net_if* dvcc_ = nullptr;
    analog_net_if* dvss_ = nullptr;
    int d_dvcc_ = -1, d_dvss_ = -1, d_rst_ = -1;
    bool externa_ = false;
    std::unique_ptr<DbgDirectoM430> directo_;
    std::unique_ptr<GdbStubM430> gdb_, gdb_pines_;
    std::unique_ptr<SondaM430> sonda_;
    std::unique_ptr<SondaXv2> sonda_xv2_;
    std::unique_ptr<DbgSondaXv2> dbg_sonda_;

    void poke16(uint32_t a, uint16_t v) { chip_->poke(a, uint8_t(v)); chip_->poke(a + 1, uint8_t(v >> 8)); }
};

REGISTRA_MCU(MSP430FR2, [](const char* nm, DebugCaps d, const Cableado& c, const McuDesc& desc) -> mcu_if* {
    return new McuFR2(nm, d, c, desc);
});

} // namespace stm32
#endif // STM32_SOC_MSP430_FR2_FR2_MCU_H
