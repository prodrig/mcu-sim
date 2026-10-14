// =============================================================================
// wdt_a.h — El perro guardián del MSP432P4 (WDT_A)
//
// Es el del MSP430, y trae su costumbre más famosa: ARRANCA ENCENDIDO. Al salir
// del reset cuenta 2^15 ciclos de SMCLK -con el DCO a 3 MHz, unos 10,9 ms- y,
// si nadie lo para o lo refresca, resetea el chip. Es el primer escollo de
// cualquier alumno que viene de un STM32, y por eso el modelo lo hace igual.
// [H: WDT_A_CTL_IS_4 = 2^15; 0x6904 de reset, 1F: TRM; P-16 i]
//
// Un único registro de 16 bits, WDTCTL, en el desplazamiento 0x0C [H]:
//
//   PW    [15:8]  contraseña: se escribe 0x5A, se lee 0x69. Escribir otra
//                 cosa -o escribir solo el byte bajo- es una VIOLACIÓN y
//                 resetea el chip.
//   HOLD  [7]     parado
//   SSEL  [6:5]   SMCLK, ACLK, VLOCLK o BCLK
//   TMSEL [4]     0: perro guardián (resetea); 1: temporizador (interrumpe)
//   CNTCL [3]     escribir 1 pone el contador a cero; se lee 0
//   IS    [2:0]   2^31, 2^27, 2^23, 2^19, 2^15, 2^13, 2^9, 2^6 ciclos
//
// Si el reset es duro o blando lo decide SYSCTL_WDTRESET_CTL, que el SoC
// consulta al pedirlo. Se congela con el núcleo parado en el depurador si
// SYSCTL_PERIHALT_CTL.HALT_WDT lo pide.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_WDT_A_H
#define STM32_PERIPH_MSP432P4_WDT_A_H

#include <functional>
#include "../../common/periph_base.h"
#include "../../common/guarda_tick.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class WdtAP4 : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};             // WDT_A_IRQn, en modo intervalo

    static constexpr uint16_t PW_ESCRITA = 0x5A, PW_LEIDA = 0x69;
    static constexpr uint32_t R_CTL = 0x0C;       // [H: WDT_A_Type]

    // La frecuencia de la fuente SSEL (0 SMCLK, 1 ACLK, 2 VLO, 3 BCLK).
    std::function<double(unsigned)> f_fuente;
    // Pide un reset: `por_violacion` distingue las dos causas.
    std::function<void(bool por_violacion)> pide_reset;
    // ¿Congelado? (núcleo parado y HALT_WDT)
    std::function<bool()> congelado;

    explicit WdtAP4(sc_core::sc_module_name nm) : BusSlave(nm, p4::WDT_A_B, 0x400) {
        SC_HAS_PROCESS(WdtAP4);
        SC_THREAD(cuenta_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }

    uint16_t ctl() const { return ctl_; }
    bool     parado() const { return (ctl_ & 0x80u) != 0; }
    unsigned vencimientos() const { return n_venc_; }
    unsigned violaciones() const { return n_viol_; }
    // Lo que el SoC llama cuando cambia un reloj o la congelación.
    void recalcula() { ancla(); ev_.notify(sc_core::SC_ZERO_TIME); }

protected:
    uint16_t ctl_ = 0x0004;                       // IS = 4, SMCLK, perro, en marcha
    double   cuenta0_ = 0.0;                      // ciclos contados en t_ref_
    sc_core::sc_time t_ref_{sc_core::SC_ZERO_TIME};
    double   f_ref_ = 0.0;                        // frecuencia desde t_ref_
    bool     congelado_ref_ = false;
    bool     o_irq_ = false;
    unsigned n_venc_ = 0, n_viol_ = 0;
    sc_core::sc_event ev_, pub_ev_;

    static double tope(unsigned is) {
        static const unsigned n[8] = {31, 27, 23, 19, 15, 13, 9, 6};
        return double(1ull << n[is & 7u]);
    }
    bool corre() const { return !parado() && !congelado_ref_ && f_ref_ > 0.0; }
    double cuenta_ahora() const {
        if (!corre()) return cuenta0_;
        return cuenta0_ + (sc_core::sc_time_stamp() - t_ref_).to_seconds() * f_ref_;
    }
    // Fija el punto de partida con la configuración y el reloj de AHORA.
    void ancla() {
        cuenta0_ = cuenta_ahora();
        t_ref_ = sc_core::sc_time_stamp();
        f_ref_ = f_fuente ? f_fuente((ctl_ >> 5) & 3u) : 0.0;
        congelado_ref_ = congelado ? congelado() : false;
    }

    void reset_proc() {
        if (rst_n.read()) return;
        ctl_ = 0x0004;
        cuenta0_ = 0.0;
        o_irq_ = false;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        recalcula();
    }
    void pub_proc() { irq.write(o_irq_); }

    void cuenta_proc() {
        for (;;) {
            if (!corre()) { wait(ev_); continue; }
            const double falta = tope(ctl_ & 7u) - cuenta_ahora();
            if (falta > GUARDA_TICK) {
                wait(sc_core::sc_time(falta / f_ref_, sc_core::SC_SEC), ev_);
                continue;
            }
            // Vence
            ++n_venc_;
            cuenta0_ = 0.0;
            t_ref_ = sc_core::sc_time_stamp();
            if (ctl_ & 0x10u) {                       // intervalo: un pulso
                o_irq_ = true;  pub_ev_.notify(sc_core::SC_ZERO_TIME);
                wait(sc_core::SC_ZERO_TIME); wait(sc_core::SC_ZERO_TIME);
                o_irq_ = false; pub_ev_.notify(sc_core::SC_ZERO_TIME);
            } else if (pide_reset) {
                // Un reset duro le llegará también a él y lo dejará como
                // sale de fábrica; uno blando no, y entonces sigue contando
                // desde cero con la configuración que tenía.
                pide_reset(false);
            }
        }
    }

    uint32_t reg_read(uint32_t off) override {
        if (off == R_CTL) return uint32_t((PW_LEIDA << 8) | (ctl_ & 0xF7u));
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (off != R_CTL) return;
        // Una escritura a WDTCTL que no lleve el byte de la contraseña es una
        // violación, igual que una con la contraseña equivocada.
        const bool lleva_pw = (be & 0x2u) != 0;
        if (!lleva_pw || ((v >> 8) & 0xFFu) != PW_ESCRITA) {
            ++n_viol_;
            if (pide_reset) pide_reset(true);
            return;
        }
        if (!(be & 0x1u)) return;                     // solo la contraseña
        ancla();
        if (v & 0x08u) cuenta0_ = 0.0;                // CNTCL
        ctl_ = uint16_t(v & 0xF7u);
        ancla();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_WDT_A_H
