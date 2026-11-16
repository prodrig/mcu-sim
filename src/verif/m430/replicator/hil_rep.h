// =============================================================================
// hil_rep.h — Los pines del REP430F simulados, para el Replicator430 de TI
// compilado para el anfitrión (P-20 / M2 de doc/analisis_msp430.md, §7.3)
//
// El Replicator430 (SLAU320, paquete 1.22) es el programador de referencia de
// TI: un MSP430F5437 que mueve los pines de JTAG o de Spy-Bi-Wire de otro
// MSP430 por su puerto 5 (TCK P5.4, TMS P5.5, TDO P5.6, TDI P5.7, TEST P5.2,
// RST P5.3) y unos traductores de nivel. Aquí, ese puerto es esto: cada
// escritura del registro de salida mueve los `AnalogNet` del chip simulado, y
// los `_NOP()` y retardos son tiempo simulado. Lo de TI (JTAGfunc430.c,
// Devices430.c) se compila sin tocar una línea: verif/m430/replicator/430/.
//
// En SBW, como en el REP430F: SBWTCK sale por el bit de TCK (al pin TEST del
// chip), SBWTDIO por el de TDI (a RST), y se lee por el de TDO con el
// traductor de TDOI dado la vuelta. VPP (6,5 V) va a TEST.
// =============================================================================
#ifndef STM32_VERIF_M430_REPLICATOR_HIL_REP_H
#define STM32_VERIF_M430_REPLICATOR_HIL_REP_H

#include <systemc>
#include <functional>
#include <string>
#include <vector>
#include "../../../common/analog_net.h"

namespace stm32 {

class HilRep {
public:
    enum : unsigned char { B_TEST = 0x04, B_RST = 0x08, B_TCK = 0x10, B_TMS = 0x20, B_TDO = 0x40, B_TDI = 0x80 };
    struct Pines {
        analog_net_if* tck = nullptr; analog_net_if* tms = nullptr; analog_net_if* tdi = nullptr;
        analog_net_if* tdo = nullptr; analog_net_if* test = nullptr; analog_net_if* rst = nullptr;
    };
    // Lo que hace la fuente de la placa del REP430F con el chip (SetTargetVcc)
    std::function<void(bool)> alimenta;
    // La traza de las transacciones de JTAG: "I13>89", "D16 2401>2401", "T0", "T1"
    std::vector<std::string>* traza = nullptr;
    double vdd = 3.3, vpp = 6.5;
    sc_core::sc_time t_nop{55556, sc_core::SC_PS};            // un ciclo de MCLK a 18 MHz

    HilRep(const Pines& p, bool sbw) : p_(p), sbw_(sbw) {
        if (!sbw_) {
            id_tck_ = p_.tck->register_driver("rep_tck"); id_tms_ = p_.tms->register_driver("rep_tms");
            id_tdi_ = p_.tdi->register_driver("rep_tdi");
        }
        id_test_ = p_.test->register_driver("rep_test");
        id_rst_ = p_.rst->register_driver("rep_rst");
        tres_estados();
    }
    bool sbw() const { return sbw_; }

    // --- El puerto del REP430F ---------------------------------------------------
    void salida(unsigned char v) { out_ = v; aplica(); }
    unsigned char salida() const { return out_; }
    // El bit de TDO (0x40), como se lee en P5IN
    unsigned char entrada() {
        espera_pendiente();
        const bool b = sbw_ ? lee(p_.rst) : lee(p_.tdo);
        return b ? B_TDO : 0;
    }
    // El traductor de TDOI: arriba, SBWTDIO es del chip
    void tdoi_entrada(bool v) { tdoi_ent_ = v; aplica(); }
    void vpp_en_test(bool v) { vpp_test_ = v; aplica(); }
    void conduce(bool v) { conduce_ = v; aplica(); }
    void nop(unsigned n = 1) { pendiente_ += t_nop * double(n); }
    void espera_us(double us) { nop(0); pendiente_ += sc_core::sc_time(us, sc_core::SC_US); espera_pendiente(); }
    void tres_estados() { conduce_ = false; vpp_test_ = false; aplica(); espera_pendiente(); }

    // La traza: la escribe la capa de LowLevelFunc430.h (la Shift y TCLK)
    void apunta(const std::string& s) { if (traza) traza->push_back(s); }
    void completa(const std::string& s) { if (traza && !traza->empty()) traza->back() += s; }

private:
    Pines p_;
    bool sbw_;
    int id_tck_ = -1, id_tms_ = -1, id_tdi_ = -1, id_test_ = -1, id_rst_ = -1;
    unsigned char out_ = 0;
    bool tdoi_ent_ = false, vpp_test_ = false, conduce_ = false;
    sc_core::sc_time pendiente_{sc_core::SC_ZERO_TIME};

    bool lee(analog_net_if* n) const { return n && !n->floating() && n->voltage() > 0.5f * float(vdd); }
    void pon(analog_net_if* n, int id, bool v) { n->set_drive(id, v ? float(vdd) : 0.0f, 30.0f); }
    void espera_pendiente() {
        if (pendiente_ > sc_core::SC_ZERO_TIME) { sc_core::wait(pendiente_); pendiente_ = sc_core::SC_ZERO_TIME; }
    }
    // Cada escritura del puerto es una instrucción del F5437 (BIS.B/BIC.B
    // #x,&P5OUT: 4 ciclos [SLAU208 «Instruction Cycles and Lengths»]): el flanco siguiente no puede ser
    // en el mismo instante
    void aplica() {
        espera_pendiente();
        pendiente_ = t_nop * 4.0;
        aplica_ya();
    }
    void aplica_ya() {
        if (!conduce_) {
            if (!sbw_) { p_.tck->set_hiz(id_tck_); p_.tms->set_hiz(id_tms_); p_.tdi->set_hiz(id_tdi_); }
            if (vpp_test_) p_.test->set_drive(id_test_, float(vpp), 30.0f); else p_.test->set_hiz(id_test_);
            p_.rst->set_hiz(id_rst_);
            return;
        }
        if (sbw_) {
            if (vpp_test_) p_.test->set_drive(id_test_, float(vpp), 30.0f);
            else pon(p_.test, id_test_, out_ & B_TCK);
            if (tdoi_ent_) p_.rst->set_hiz(id_rst_); else pon(p_.rst, id_rst_, out_ & B_TDI);
            return;
        }
        pon(p_.tck, id_tck_, out_ & B_TCK); pon(p_.tms, id_tms_, out_ & B_TMS); pon(p_.tdi, id_tdi_, out_ & B_TDI);
        if (vpp_test_) p_.test->set_drive(id_test_, float(vpp), 30.0f);
        else pon(p_.test, id_test_, out_ & B_TEST);
        pon(p_.rst, id_rst_, out_ & B_RST);
    }
};

} // namespace stm32
#endif // STM32_VERIF_M430_REPLICATOR_HIL_REP_H
