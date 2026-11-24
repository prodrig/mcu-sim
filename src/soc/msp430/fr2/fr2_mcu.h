// =============================================================================
// fr2_mcu.h — El ADAPTADOR de las FR2xx/FR4xx a `mcu_if`, y su alta en la
// factoría (M6: el FR2433; M7: el FR4133, el FR2311...)
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

#include "../../../parts/estado_cpu.h"
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include "../../mcu_if.h"
#include "../fr2433/soc_fr2433.h"
#include "../fr4133/soc_fr4133.h"
#include "../fr2311/soc_fr2311.h"
#include "../fr2355/soc_fr2355.h"
#include "../fr2476/soc_fr2476.h"
#include "../fr5969/soc_fr5969.h"
#include "../fr5994/soc_fr5994.h"
#include "../fr6989/soc_fr6989.h"
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
// FR2311 TSSOP-20 (el de la LaunchPad, MSP430FR2311IPW20): P1 y P2 enteros [SLASE58E tabla 4-1]
inline constexpr Encapsulado ENC_FR2311_PW20{"TSSOP20", 20, 16, {0xFFu, 0xFFu}, true};
// FR2355 LQFP-48 (el de la LaunchPad, MSP430FR2355TPT): P1-P4 enteros,
// P5.0-P5.4 y P6.0-P6.6 [SLASEC4D figura 4-1]
inline constexpr Encapsulado ENC_FR2355_PT48{"LQFP48", 48, 44, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x1Fu, 0x7Fu}, true};
// FR2476 LQFP-48 (el de la LaunchPad, MSP430FR2476TPT): P1-P5 enteros y
// P6.0-P6.2 [SLASEO7C figura 7-1]
inline constexpr Encapsulado ENC_FR2476_PT48{"LQFP48", 48, 43, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x07u}, true};
// FR5969 VQFN-48 (el de la LaunchPad, MSP430FR5969IRGZ): P1-P4 y PJ enteros
// [SLAS704G figura 4-1] (PJ es el puerto de índice FR5_PJ)
inline constexpr Encapsulado ENC_FR5969_RGZ48{"VQFN48", 48, 40, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0, 0, 0, 0, 0, 0, 0xFFu}, true};
// FR5994 LQFP-80 (el de la LaunchPad, MSP430FR5994IPN): P1-P7 y PJ enteros,
// P8.0-P8.3 [SLASE54D figura 4-1, tabla 9-35]
inline constexpr Encapsulado ENC_FR5994_PN80{"LQFP80", 80, 68, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x0Fu, 0, 0, 0xFFu}, true};
// FR6989 LQFP-100 (el de la LaunchPad, MSP430FR6989IPZ): P1-P9, P10.0-P10.2
// y PJ [SLAS789D figura 4-1]
inline constexpr Encapsulado ENC_FR6989_PZ100{"LQFP100", 100, 83, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x07u, 0xFFu}, true};
inline constexpr Encapsulado ENC_FR4133_PM64{"LQFP64", 64, 60, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x0Fu}, true};

namespace fr2 {
inline bool pad_fr2(const std::string& t, unsigned& puerto, unsigned& pin) {
    std::string s = t;
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (s.size() == 4 && s[0] == 'P' && s[2] == '.' && s[3] >= '0' && s[3] <= '7' && s[1] >= '1' && s[1] <= '9') {
        puerto = unsigned(s[1] - '1'); pin = unsigned(s[3] - '0');
        return true;                                // lo que existe lo dice el encapsulado
    }
    // P10.x y PJ.x (FR5xx/FR6xx: PJ es el índice FR5_PJ)
    if (s.size() == 5 && s.compare(0, 4, "P10.") == 0 && s[4] >= '0' && s[4] <= '7') { puerto = 9; pin = unsigned(s[4] - '0'); return true; }
    if (s.size() == 4 && s.compare(0, 3, "PJ.") == 0 && s[3] >= '0' && s[3] <= '7') { puerto = FR5_PJ; pin = unsigned(s[3] - '0'); return true; }
    return false;
}
inline std::string canonico_fr2(unsigned puerto, unsigned pin) {
    if (puerto == FR5_PJ) return "PJ." + std::to_string(pin);
    return "P" + std::to_string(puerto + 1) + "." + std::to_string(pin);
}
inline const char* const ALIM_FR2[] = {"DVCC", "DVSS", "RST", "TEST", nullptr};
} // namespace fr2

inline const Nomenclatura NOM_MSP430FR2{"MSP430FR2", &fr2::pad_fr2, &fr2::canonico_fr2, fr2::ALIM_FR2};
struct RegistroNomFr2 {
    RegistroNomFr2() {
        ContextoNomenclatura::g().registra_tipo("MSP430FR2", &NOM_MSP430FR2);
        ContextoNomenclatura::g().registra_tipo("MSP430FR4", &NOM_MSP430FR2);   // el FR4133: la misma
        ContextoNomenclatura::g().registra_tipo("MSP430FR5", &NOM_MSP430FR2);   // las FR5xx/FR6xx, con PJ
        ContextoNomenclatura::g().registra_tipo("MSP430FR6", &NOM_MSP430FR2);
    }
};
inline const RegistroNomFr2 g_registro_nom_fr2{};

// `cristal`: la LaunchPad lleva uno de 32 768 Hz en XIN/XOUT (y conectado)
struct McuCapsFr2 { const char* nombre; Encapsulado enc; const ChipFr2* chip; bool cristal; };
inline constexpr McuCapsFr2 MCU_MSP430FR2433{"MSP430FR2433", ENC_FR2433_RGE24, &CHIP_FR2433, false};
inline constexpr McuCapsFr2 MCU_MSP430FR4133{"MSP430FR4133", ENC_FR4133_PM64, &CHIP_FR4133, true};
inline constexpr McuCapsFr2 MCU_MSP430FR2311{"MSP430FR2311", ENC_FR2311_PW20, &CHIP_FR2311, false};
inline constexpr McuCapsFr2 MCU_MSP430FR2355{"MSP430FR2355", ENC_FR2355_PT48, &CHIP_FR2355, true};
inline constexpr McuCapsFr2 MCU_MSP430FR2476{"MSP430FR2476", ENC_FR2476_PT48, &CHIP_FR2476, false};
inline constexpr McuCapsFr2 MCU_MSP430FR5969{"MSP430FR5969", ENC_FR5969_RGZ48, &CHIP_FR5969, true};
inline constexpr McuCapsFr2 MCU_MSP430FR5994{"MSP430FR5994", ENC_FR5994_PN80, &CHIP_FR5994, true};
inline constexpr McuCapsFr2 MCU_MSP430FR6989{"MSP430FR6989", ENC_FR6989_PZ100, &CHIP_FR6989, true};
struct RegistroCatalogoFr2 {
    RegistroCatalogoFr2() {
        for (const McuCapsFr2* m : {&MCU_MSP430FR2433, &MCU_MSP430FR4133, &MCU_MSP430FR2311, &MCU_MSP430FR2355, &MCU_MSP430FR2476,
                                  &MCU_MSP430FR5969, &MCU_MSP430FR5994, &MCU_MSP430FR6989})
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
            gdb_->pon_fram(chip_->fram_ini(), chip_->fram_fin(), 0x1800, 0x19FF, caps_.chip->mpu_base);
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
    // `hfxt="<Hz>"` en el <mcu>: un cristal en HFXIN/HFXOUT (las FR5xx/FR6xx;
    // las LaunchPad no lo montan) [UG FR5 §3.2.3]. `--bsl` (M9 y M10, todas):
    // b1 el propio (por omisión), un fichero con el volcado de la ROM del BSL
    // (B2: el BSL1 y, en las FR23xx-FR26xx, el BSL2) o ninguno (B3)
    bool admite_opcion(const std::string& clave) const override {
        return (clave == "hfxt" && caps_.chip->hfxt_bits) || (clave == "bsl" && caps_.chip->desc->bsl.existe());
    }
    std::string opcion(const std::string& clave, const std::string& valor) override {
        if (!admite_opcion(clave)) return mcu_if::opcion(clave, valor);
        if (clave == "bsl") {
            if (valor == "b1" || valor == "B1") { chip_->pon_bsl_b1(); return ""; }
            if (valor == "ninguno" || valor == "b3" || valor == "B3") { chip_->quita_bsl(); return ""; }
            return chip_->carga_bsl(valor);
        }
        char* fin = nullptr;
        const double f = std::strtod(valor.c_str(), &fin);
        if (!fin || *fin || f < 4e6 || f > 24e6) return "hfxt: una frecuencia en Hz de 4e6 a 24e6 (DS tabla 5-5), no '" + valor + "'";
        chip_->cristal_hfxt = true;
        chip_->f_hfxt = f;
        return "";
    }
    std::string describe_opcion(const std::string& clave) const override {
        if (clave == "bsl") return chip_->bsl_origen;
        if (clave != "hfxt" || !chip_->cristal_hfxt) return "";
        char b[80];
        std::snprintf(b, sizeof b, "HFXT: un cristal de %.3f MHz en HFXIN/HFXOUT", chip_->f_hfxt / 1e6);
        return b;
    }

    std::vector<congela::Nucleo> nucleos() override {
        SocFR2* c = chip_;
        return {{std::string(c->name()) + ".nucleo", [c] { return c->nucleo.parado() && !c->nucleo.en_reset(); },
                 [c](sc_core::sc_event_or_list& l) {
                     l |= c->nucleo.ev_parado();
                     l |= c->rst_n.value_changed_event();
                 }}};
    }
    // B4: las funciones de los pines, de la tabla del chip (ChipFr2::fn), que
    // es la que usa el modelo; `modo` es PxSEL1:PxSEL0 (1, 2 o 3). Y lo
    // analógico que se elige con otro registro: los canales del ADC (con
    // ADCPCTL; con PxSEL = 11 en los «Smart Analog Combo»), los SAC, el TIA y
    // los pines del LCD
    InventarioPines inventario_pines() override {
        InventarioPines inv;
        inv.hay = true;
        const ChipFr2& c = *caps_.chip;
        auto timer = [&](unsigned m) {
            // "timer0_a3" -> "TA0", "timer1_b7" -> "TB1"
            const std::string n = m < c.ntimers ? c.timers[m].nombre : "";
            const size_t u = n.find('_');
            if (n.size() < 7 || u == std::string::npos) return std::string("T?");
            return std::string("T") + char(std::toupper(static_cast<unsigned char>(n[u + 1]))) +
                   n.substr(5, u - 5);
        };
        auto usci = [&](unsigned m, unsigned l) {
            const std::string n = m < c.nuscis ? c.uscis[m].nombre : "";
            const bool b = m < c.nuscis && c.uscis[m].es_b;
            const std::string u = std::string("UC") + (b ? "B" : "A") +
                                  (n.empty() ? std::string("?") : n.substr(n.size() - 1));
            switch (l) {
            case 0: return b ? u + "SIMO/" + u + "SDA" : u + "TXD/" + u + "SIMO";
            case 1: return b ? u + "SOMI/" + u + "SCL" : u + "RXD/" + u + "SOMI";
            case 2: return u + "CLK";
            default: return u + "STE";
            }
        };
        auto pon = [&](unsigned pin, int modo, const std::string& nombre, bool analogica,
                       const std::string& senal) {
            FuncionDePin f;
            f.puerto = pin / 8; f.pin = pin % 8; f.modo = modo;
            f.nombre = nombre; f.analogica = analogica; f.senal = senal;
            inv.fijas.push_back(f);
        };
        static const char* const RELOJ[] = {"MCLK", "SMCLK", "ACLK"};
        // El bit del SR de la función 14 va como su MÁSCARA (0x80 SCG1, 0x40
        // SCG0, 0x20 OSCOFF, 0x10 CPUOFF), no como índice
        auto sr = [](unsigned m) -> std::string {
            switch (m) {
            case 0x80: return "SRSCG1";
            case 0x40: return "SRSCG0";
            case 0x20: return "SROSCOFF";
            case 0x10: return "SRCPUOFF";
            default: return "SR";
            }
        };
        // Las salidas del ESI (función 16): 0-4 ESITEST0-4, 5 ESIC1OUT, 6 ESIC2OUT
        auto esi = [](unsigned k) -> std::string {
            if (k < 5) return "ESITEST" + std::to_string(k);
            return k == 5 ? "ESIC1OUT" : k == 6 ? "ESIC2OUT" : "ESI" + std::to_string(k);
        };
        for (unsigned k = 0; k < c.nfn; ++k) {
            const Fr2Fn& f = c.fn[k];
            const int modo = f.sel & 0x03;
            if (f.tipo == 0 || modo == 0) continue;      // un hueco de la tabla (FR5994)
            std::string n;
            switch (f.tipo) {
            case 1: n = timer(f.mod) + (f.dir == 0 ? ".CCI" + std::to_string(f.n) + "A"
                                                    : "." + std::to_string(f.n)); break;
            case 2: n = usci(f.mod, f.n); break;
            case 3: n = ""; break;                       // analógica o cristal
            case 4: n = f.n < 3 ? RELOJ[f.n] : "CLK"; break;
            case 5: n = timer(f.mod) + "CLK"; break;
            case 7: n = "COMP" + std::to_string(f.mod) + ".O"; break;
            case 8: n = f.n ? "MFM.TX" : "MFM.RX"; break;
            // La entrada que pone en alta impedancia las salidas del Timer_B:
            // TBxTRG en los datasheets de las FR2/FR4, TBxOUTH en los de las
            // FR5/FR6
            case 9: n = timer(f.mod) + (c.fr5 ? "OUTH" : "TRG"); break;
            case 10: n = "COUT"; break;
            case 11: n = timer(f.mod) + ".CCI" + std::to_string(f.n) + "B"; break;
            case 12: n = "DMAE0"; break;
            case 13: n = "RTCCLK"; break;
            case 14: n = sr(f.n); break;
            case 16: n = esi(f.n); break;
            default: n = "tipo" + std::to_string(f.tipo); break;
            }
            pon(f.pin, modo, n, f.tipo == 3, "fr2:" + std::to_string(f.tipo));
        }
        for (unsigned ch = 0; ch < 16; ++ch)
            if (c.adc_pin[ch] >= 0)
                pon(unsigned(c.adc_pin[ch]), c.adc_por_sel ? 3 : -1, "A" + std::to_string(ch), true,
                    "adc");
        // Las FR5xx/FR6xx: el ADC12_B (A0-A31), el Comp_E (C0-C15), VeREF± y
        // las entradas del ESI; todos con PxSEL = 11
        for (unsigned ch = 0; ch < 32; ++ch)
            if (c.adc5_pin[ch] >= 0)
                pon(unsigned(c.adc5_pin[ch]), 3, "A" + std::to_string(ch), true, "adc12_b");
        for (unsigned ch = 0; ch < 16; ++ch)
            if (c.ce_pin[ch] >= 0) pon(unsigned(c.ce_pin[ch]), 3, "C" + std::to_string(ch), true, "comp_e");
        if (c.veref_mas >= 0) pon(unsigned(c.veref_mas), 3, "VeREF+", true, "ref");
        if (c.veref_menos >= 0) pon(unsigned(c.veref_menos), 3, "VeREF-", true, "ref");
        for (unsigned k = 0; k < 4; ++k) {
            if (c.esi_ch_pin[k] >= 0) pon(unsigned(c.esi_ch_pin[k]), 3, "ESICH" + std::to_string(k), true, "esi");
            if (c.esi_ci_pin[k] >= 0) pon(unsigned(c.esi_ci_pin[k]), 3, "ESICI" + std::to_string(k), true, "esi");
        }
        for (unsigned k = 0; k < c.nsacs; ++k) {
            const std::string o = "OA" + std::string(c.sacs[k].nombre).substr(std::string(c.sacs[k].nombre).size() - 1);
            pon(c.sacs[k].pin_mas, 3, o + "+", true, "sac");
            pon(c.sacs[k].pin_menos, 3, o + "-", true, "sac");
            pon(c.sacs[k].pin_sal, 3, o + "O", true, "sac");
        }
        if (c.tia) {
            pon(c.tia->pin_mas, 3, "TRI0+", true, "tia");
            pon(c.tia->pin_menos, 3, "TRI0-", true, "tia");
            pon(c.tia->pin_sal, 3, "TRI0O", true, "tia");
        }
        for (unsigned k = 0; k < c.nlcd; ++k)
            if (c.lcd_l[k] != 0xFF)                      // 0xFF: esa Lx no tiene pin
                pon(c.lcd_l[k], -1, "L" + std::to_string(k), true, "lcd");
        return inv;
    }
    std::vector<std::string> limitaciones() const override {
        if (caps_.chip->fr5)
            return {"el BSL de ROM es el B1 de mcu-sim (--bsl FICHERO para el volcado de un chip): con el vector de reset borrado no se arranca solo",
                    "la FRAM no tiene estados de espera ni cache en el tiempo: NWAITS solo avisa"};
        if (caps_.chip->desc->bsl2.existe())
            return {"el BSL de ROM es el B1 de mcu-sim (--bsl FICHERO para el volcado de un chip): con el vector de reset borrado arranca en el",
                    "la FRAM no tiene estados de espera ni cache en el tiempo: NWAITS solo avisa"};
        return {"el BSL de ROM es el B1 de mcu-sim, solo UART a 9600 (--bsl FICHERO para el volcado de un chip); con el vector de reset borrado, el chip no arranca nada",
                "la FRAM no tiene estados de espera ni cache en el tiempo: NWAITS solo avisa"};
    }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        // El estado de la CPU para la ventana: LPMx no es parada (M12)
        if (!estado_cpu_)
            estado_cpu_.reset(new EstadoCpu(prefijo.empty() ? std::string("CPU") : prefijo + ".CPU", [this] {
                if (chip_->nucleo.en_reset()) return int(EstadoCpu::RESET);
                if (chip_->nucleo.parada_dbg()) return int(EstadoCpu::PARADA);
                if (chip_->en_lpm5()) return int(EstadoCpu::LPMX5);
                return EstadoCpu::por_sr(chip_->nucleo.cpu.sr());
            }));
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
        const uint16_t p[] = {0x40B2, 0x5A80, uint16_t(caps_.chip->fr5 ? 0x015C : 0x01CC),   // mov #WDTPW|WDTHOLD,&WDTCTL
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
        gdb_pines_->pon_fram(chip_->fram_ini(), chip_->fram_fin(), 0x1800, 0x19FF, caps_.chip->mpu_base);
        gdb_pines_->set_enabled(false);
        return gdb_pines_.get();
    }

private:
    std::unique_ptr<EstadoCpu> estado_cpu_;
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
