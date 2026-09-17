// =============================================================================
// stm32f4_mcu.h — El ADAPTADOR de la familia F405/407 a `mcu_if`
//
// Una clase fina que envuelve al `Stm32F407VG` que ya existía y le pone la cara
// que `mcu_if` pide. No modela nada: traduce.
//
// POR QUÉ UN ADAPTADOR Y NO HACER QUE `Stm32F407VG` HEREDE DE `mcu_if`.
// Porque `Stm32F407VG` es un `sc_module` con ciento y pico submódulos, y
// meterle una herencia virtual más para que `sim` pueda llamarle por una
// interfaz sería pagar en el sitio equivocado. El adaptador se construye una
// vez por chip durante la elaboración, vive lo que vive la simulación y no
// interviene en ninguna transacción: su coste es exactamente cero.
//
// Y hay una segunda razón, que es la que de verdad manda: **el adaptador es el
// sitio donde se guarda lo que `sim` necesita y el modelo no tiene por qué
// saber**. Los cuatro índices de driver de alimentación (`d_vdd`, `d_vdda`,
// `d_nrst`, `d_bt0`) eran cuatro campos sueltos en una `struct` de `sim_main`;
// aquí son estado del adaptador, y `alimenta(true)` los usa sin que quien llama
// tenga que acordarse de ellos ni del orden.
//
// EL ORDEN DEL ARRANQUE, que es lo que esos cuatro índices esconden y conviene
// dejar escrito: primero todo a cero con NRST abajo, luego VDD y VDDA a 3,3 V,
// y **solo después** se suelta el reset. Es el orden de una placa de verdad, y
// hacerlo al revés deja al chip arrancando con la alimentación a medias.
// =============================================================================
#ifndef STM32_SOC_STM32F4_MCU_H
#define STM32_SOC_STM32F4_MCU_H

#include "mcu_if.h"
#include "../top/stm32f407vg.h"
#include "../parts/netlist.h"
#include "../verif/image_loader.h"

namespace stm32 {

class Stm32F4Mcu : public mcu_if {
public:
    Stm32F4Mcu(const char* nm, DebugCaps dbg, const Cableado& cab,
               const McuCaps& c)
        : dut_(new Stm32F407VG(nm, dbg, cab, c)), caps_(c) {
        // Los cuatro drivers con los que `sim` hace de fuente de alimentación.
        // Se registran aquí, en la elaboración, porque un `AnalogNet` no admite
        // altas con la simulación en marcha.
        d_vdd_  = dut_->pwr_pads.vdd.register_driver("sim_vdd");
        d_vdda_ = dut_->pwr_pads.vdda.register_driver("sim_vdda");
        d_nrst_ = dut_->pwr_pads.nrst.register_driver("sim_nrst");
        d_bt0_  = dut_->pwr_pads.boot0.register_driver("sim_boot0");
    }
    ~Stm32F4Mcu() override { delete dut_; }

    // El objeto concreto, para lo que la interfaz no cubre a propósito: el
    // informe final de los LEDs, las pruebas del banco, lo que sea que necesite
    // saber que esto es un F4 y no otra cosa.
    Stm32F407VG& chip() { return *dut_; }

    const McuCaps& caps() const override { return caps_; }

    void registra_nodos(const std::string& prefijo, NodeMap& n) override {
        n.registra_mcu(prefijo, dut_->pinmux, dut_->pwr_pads);
    }
    analog_net_if& nodo_analogico(unsigned puerto, unsigned pin) override {
        return dut_->pinmux.analog(puerto, pin);
    }

    // El apagado deja NRST a masa: un chip sin alimentación no está «en reset»,
    // está sin alimentación, y el pin lo refleja.
    void alimenta(bool encendido) override {
        if (encendido) {
            dut_->pwr_pads.vdd.set_drive(d_vdd_,  3.3f, 0.1f);
            dut_->pwr_pads.vdda.set_drive(d_vdda_, 3.3f, 0.1f);
        } else {
            dut_->pwr_pads.vdd.set_drive(d_vdd_,  0.0f, 1.0f);
            dut_->pwr_pads.vdda.set_drive(d_vdda_, 0.0f, 1.0f);
            // BOOT0 abajo por resistencia: arranque desde la Flash de usuario.
            dut_->pwr_pads.boot0.set_drive(d_bt0_, 0.0f, 10e3f);
            reset_pin(true);
        }
    }
    void reset_pin(bool activo) override {
        if (activo) dut_->pwr_pads.nrst.set_drive(d_nrst_, 0.0f, 100.0f);
        else        dut_->pwr_pads.nrst.set_hiz(d_nrst_);
    }

    bool carga_firmware(const std::string& ruta) override {
        ImageLoader ld(*dut_);
        return ld.load_file(ruta.c_str(), caps_.memoria.flash.base) > 0;
    }

    void set_ondas_reloj(bool on) override { dut_->rcc.set_internal_waveforms(on); }

    bool tiene_gdb_interno() const override { return dut_->core.gdb != nullptr; }
    void gdb_interno(bool activo, bool traza) override {
        if (!dut_->core.gdb) return;          // en modo `pines` no hay: no-op
        dut_->core.gdb->set_verbose(traza);
        dut_->core.gdb->set_enabled(activo);
    }

private:
    Stm32F407VG* dut_;
    McuCaps      caps_;
    int d_vdd_ = -1, d_vdda_ = -1, d_nrst_ = -1, d_bt0_ = -1;
};

// La familia entera, con un solo creador: los once miembros son la MISMA clase
// con distintos descriptores. Un F446 sería otro `REGISTRA_MCU` con otra clase.
REGISTRA_MCU(STM32F4, [](const char* nm, DebugCaps d, const Cableado& c,
                         const McuCaps& caps) -> mcu_if* {
    return new Stm32F4Mcu(nm, d, c, caps);
});

} // namespace stm32
#endif // STM32_SOC_STM32F4_MCU_H
