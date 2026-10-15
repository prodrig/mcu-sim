// =============================================================================
// ref_a.h — La referencia de tensión del MSP432P4, REF_A (fase T3)
//
// Un solo registro de 16 bits, REFCTL0 [H: REF_A_Type; TRM SLAU356, cap. 21;
// segunda fuente: DriverLib ref_a.c]:
//
//   ON      enciende el generador         VSEL    1,2 / 1,45 / -- / 2,5 V
//   OUT     saca VREF por VREF+ (P5.6)    TCOFF   apaga el sensor de temperatura
//   GENOT, BGOT   disparos de una vez (no se modelan: avisan)
//   GENACT, BGACT, GENBUSY, BGMODE, GENRDY, BGRDY   de solo lectura
//
// LA REFERENCIA SE PIDE. Además de con ON, la encienden el ADC14 (cuando va a
// convertir con VRSEL = 1 o a leer el sensor de temperatura) y los COMP_E: es
// el «reference request» del TRM. Activa por la causa que sea, GENACT y BGACT
// suben en el acto y GENRDY/BGRDY tras el tiempo de asentamiento. Mientras un
// módulo la está usando, GENBUSY = 1 y escribir VSEL u ON no hace nada, como
// en el chip. [TRM 21.2.3]
//
// LO QUE NO ESTÁ: el tiempo de asentamiento es de modelo (40 µs, el orden de
// magnitud del datasheet, sin contrastar), las tensiones son las nominales, y
// el sensor de temperatura es lineal: 685 mV a 0 °C y 1,9 mV/°C [1F, de
// memoria del datasheet SLAS826: P-16 s]. El TLV sintético lleva las
// calibraciones que corresponden a ESE sensor, así que la cuenta de los
// ejemplos de TI da la temperatura que se ha puesto.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_REF_A_H
#define STM32_PERIPH_MSP432P4_REF_A_H

#include <functional>
#include "bus16.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class RefAP4 : public Bus16P4 {
public:
    enum : uint16_t {
        ON = 0x0001, OUT = 0x0002, TCOFF = 0x0008, VSEL = 0x0030, GENOT = 0x0040,
        BGOT = 0x0080, GENACT = 0x0100, BGACT = 0x0200, GENBUSY = 0x0400,
        BGMODE = 0x0800, GENRDY = 0x1000, BGRDY = 0x2000
    };
    // Cambia algo que se ve fuera: VREF+ en el pin, o la tensión
    std::function<void()> al_cambiar;

    explicit RefAP4(sc_core::sc_module_name nm) : Bus16P4(nm, p4::REF_A_B, 0x400) {
        SC_HAS_PROCESS(RefAP4);
        SC_METHOD(listo_proc); sensitive << listo_ev_; dont_initialize();
        SC_METHOD(reset_proc); sensitive << rst_n;
    }

    // --- Lo que piden y miran los demás --------------------------------------
    // Un módulo pide (o deja de pedir) la referencia. `quien`: un bit por módulo
    void solicita(unsigned quien, bool si) {
        const unsigned antes = pedidos_;
        pedidos_ = si ? (pedidos_ | quien) : (pedidos_ & ~quien);
        if (pedidos_ != antes) actualiza();
    }
    // Un módulo la está usando ahora mismo (GENBUSY)
    void ocupa(unsigned quien, bool si) {
        ocupada_ = si ? (ocupada_ | quien) : (ocupada_ & ~quien);
    }
    bool activa() const { return (ctl0_ & ON) || pedidos_; }
    bool lista()  const { return activa() && listo_; }
    // La tensión que da: 0 si no está activa
    double vref() const {
        if (!activa()) return 0.0;
        switch ((ctl0_ >> 4) & 3u) {
            case 0:  return 1.2;
            case 1:  return 1.45;
            default: return 2.5;              // 2 está reservado: el silicio da 2,5
        }
    }
    bool sensor_on() const { return activa() && !(ctl0_ & TCOFF); }
    bool sale_por_pin() const { return lista() && (ctl0_ & OUT); }
    uint16_t ctl0() { return lee16(0, true); }

    static sc_core::sc_time t_asentar() { return sc_core::sc_time(40, sc_core::SC_US); }

protected:
    uint16_t ctl0_ = TCOFF;
    unsigned pedidos_ = 0, ocupada_ = 0;
    bool listo_ = false, avisado_ot_ = false;
    sc_core::sc_event listo_ev_;

    void reset_proc() {
        if (rst_n.read()) return;
        ctl0_ = TCOFF; listo_ = false; listo_ev_.cancel();
        if (al_cambiar) al_cambiar();
    }
    void actualiza() {
        if (activa() && !listo_) listo_ev_.notify(t_asentar());
        if (!activa()) { listo_ = false; listo_ev_.cancel(); }
        if (al_cambiar) al_cambiar();
    }
    void listo_proc() {
        if (!activa()) return;
        listo_ = true;
        if (al_cambiar) al_cambiar();
    }

    uint16_t lee16(uint32_t off, bool) override {
        if (off != 0) return 0;
        uint16_t v = uint16_t(ctl0_ & (ON | OUT | TCOFF | VSEL));
        if (activa()) v |= GENACT | BGACT;
        if (ocupada_) v |= GENBUSY;
        if (lista()) v |= GENRDY | BGRDY;
        return v;
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        if (off != 0) return;
        uint16_t w = uint16_t((ctl0_ & ~m) | (v & m));
        if (w & (GENOT | BGOT)) {
            if (!avisado_ot_) {
                avisado_ot_ = true;
                SC_REPORT_WARNING("msp432p4/ref_a", "REFGENOT/REFBGOT (disparo de una "
                    "vez): no se modela; se usa la referencia encendida con REFON");
            }
        }
        // Con la referencia en uso, VSEL y ON no cambian [TRM 21.2.3]
        if (ocupada_) w = uint16_t((w & ~(ON | VSEL)) | (ctl0_ & (ON | VSEL)));
        const bool cambia_v = ((w ^ ctl0_) & VSEL) != 0;
        ctl0_ = uint16_t(w & (ON | OUT | TCOFF | VSEL));
        if (cambia_v && activa()) { listo_ = false; listo_ev_.cancel(); }
        actualiza();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_REF_A_H
