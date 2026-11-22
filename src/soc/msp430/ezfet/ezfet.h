// =============================================================================
// ezfet.h — La eZ-FET lite de las LaunchPad, de bajo nivel (M11b de
// doc/analisis_msp430.md §11)
//
// Es lo que es en la placa: un MSP430F5528 (aquí el modelo del F5529, de la
// misma familia F552x: la misma Flash de 128 KB hasta 0x243FF, el mismo USB y
// los mismos periféricos que usa el firmware) que ejecuta las imágenes de TI
// de la eZ-FET -núcleo, HAL, HIL, canal de comunicación y la capa del DC-DC-,
// leídas de la copia de la MSP Debug Stack del usuario (common/imagen_ezfet.h;
// no se versionan). Lo que haga es lo que hace la sonda: el protocolo V3, las
// macros HAL de verdad, la comprobación de versiones con libmsp430 y, si hace
// falta, la actualización de su propio firmware.
//
// El cableado, el de [MSPDS] Bios/src/hil/ez_fet (arch.h, hil.c):
//
//   P4.2  RST / SBWTDIO  ->  RST del objetivo       (_Jtag_Target.RST)
//   P4.3  TST / SBWTCK   ->  TEST del objetivo      (_Jtag_Target.TST)
//   P6.0  A0  VBUS·150/400                          (_hil_GetVBus)
//   P6.1  A1  VCC del objetivo / 2                  (_hil_GetVcc)
//   P6.2  A2  VCC externo / 2                       (_hil_GetVccExt)
//   PU.0/PU.1 el USB, con su puente CDC hacia un pty
//
// [de modelo] La tensión del objetivo es la de la placa, fija (3,3 V): el
// interruptor de VCC de la eZ-FET (P2.0/P2.1) no la cambia; las medidas del
// ADC son las de 3,3 V y 5 V de VBUS.
// [de modelo] El identificador de herramienta de 0x100E (dentro del BSL del
// F5528, donde lo lee el núcleo: Bios/src/fw/fet/bios.c) es el de la eZ-FET
// lite sin DC-DC, 0xAAAB (eZ_FET_NO_DCDC); el resto del BSL queda en blanco.
// El backchannel UART (el segundo CDC) no se conecta todavía.
// =============================================================================
#ifndef STM32_SOC_MSP430_EZFET_H
#define STM32_SOC_MSP430_EZFET_H

#include <systemc>
#include <memory>
#include <string>
#include <vector>
#include "../f5529/soc_f5529.h"
#include "../../../parts/puente_usb.h"
#include "../../../parts/canal_pty.h"
#include "../../../common/imagen_ezfet.h"

namespace stm32 {

class EzFet : public sc_core::sc_module {
public:
    static constexpr uint16_t ID_EZFET_LITE = 0xAAAB;   // eZ_FET_NO_DCDC [MSPDS Bios/include/eZ_FetDcdc*]

    // `test` y `rst`: los nodos del objetivo. `pty`: el enlace del pty (vacío:
    // el puente USB en memoria, para los bancos)
    EzFet(sc_core::sc_module_name nm, analog_net_if& test, analog_net_if& rst, const std::string& imagenes,
          const std::string& pty = std::string())
        : sc_core::sc_module(nm), cab_(cableado(test, rst)), f5528("f5528", &cab_) {
        std::vector<TrozoFw> t;
        error_ = ezfet::lee_todas(imagenes, t);
        if (!error_.empty()) return;
        f5528.en_blanco();
        f5528.carga_bsl("ninguno");
        for (const TrozoFw& x : t)
            for (size_t i = 0; i < x.datos.size(); ++i) f5528.poke(x.dir + uint32_t(i), x.datos[i]);
        f5528.poke(0x100E, uint8_t(ID_EZFET_LITE));
        f5528.poke(0x100F, uint8_t(ID_EZFET_LITE >> 8));
        f5528.monitor_uart = false;
        // Las medidas del ADC: 5 V de VBUS y 3,3 V del objetivo, con sus divisores
        const struct { unsigned bit; float v; } an[] = {{0, 5.0f * 150.0f / 400.0f}, {1, 1.65f}, {2, 1.65f}};
        for (const auto& a : an) {
            analog_net_if& n = f5528.p(6, a.bit);
            n.set_drive(n.register_driver("ezfet_div"), a.v, 1000.0f);
        }
        if (!pty.empty()) {
            std::unique_ptr<CanalPty> c(new CanalPty(pty));
            if (!c->abierto()) { error_ = "el pty de la eZ-FET: " + c->error(); return; }
            pty_ = c.get();
            pu_.reset(new PuenteUsbCdc(f5528.pu0, f5528.pu1, &f5528.usb, std::move(c)));
        } else {
            pu_.reset(new PuenteUsbCdc(f5528.pu0, f5528.pu1, &f5528.usb, serie::parsea("memoria")));
        }
        pu_->controlador().pausa_control_us = 125.0;
        pu_->ev_in = &f5528.usb.ev_in();
        listo_ = true;
        SC_THREAD(arranque);
    }
    SC_HAS_PROCESS(EzFet);

    const std::string& error() const { return error_; }
    PuenteUsbCdc& usb() { return *pu_; }
    const CanalPty* pty() const { return pty_; }
    std::string describir() const {
        return "eZ-FET lite (F5528 con el firmware de TI) por " + (pu_ ? pu_->describir() : std::string("?"));
    }

private:
    Cableado cab_;                                       // antes que el F5528: se construye con él
public:
    SocF5529 f5528;

private:
    static Cableado cableado(analog_net_if& test, analog_net_if& rst) {
        Cableado c;
        c.une(3, 2, rst);                                   // P4.2: el puerto 4 es el índice 3
        c.une(3, 3, test);                                  // P4.3
        return c;
    }
    // Enchufar el cable: VBUS y fuera del reset
    void arranque() {
        f5528.reset(true);
        sc_core::wait(1, sc_core::SC_US);
        f5528.usb.vbus(true);
        f5528.reset(false);
    }
    std::unique_ptr<PuenteUsbCdc> pu_;
    CanalPty* pty_ = nullptr;
    std::string error_;
    bool listo_ = false;
};

} // namespace stm32
#endif
