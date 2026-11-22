// =============================================================================
// estado_cpu.h — El estado de la CPU de un MSP430 para la ventana (M12 de
// doc/analisis_msp430.md)
//
// Una CPU en LPMx NO es una CPU parada por el depurador, aunque en los dos
// casos no ejecute: en LPM espera una interrupción y los periféricos que su
// modo deja vivos siguen; parada, la tiene JTAG. La pieza lo enseña con tres
// observables, que la ventana pinta sin saber qué es (los dos primeros, de 0 a
// 1, como ● / ○):
//
//   lpm        durmiendo en un modo de bajo consumo (CPUOFF, o LPMx.5)
//   depurador  parada por JTAG/SBW (HALT_JTAG, TCLK de JTAG, o la parada de GDB)
//   modo       0 activa, 1-5 LPM0-LPM4, 6 LPMx.5, 7 parada, 8 en reset
//
// La crea el MCU (g2553_mcu.h, f5529_mcu.h, fr2_mcu.h) con su id y «.CPU»: no
// va en el XML de la placa.
// =============================================================================
#ifndef STM32_PARTS_ESTADO_CPU_H
#define STM32_PARTS_ESTADO_CPU_H

#include <functional>
#include <string>
#include "part_base.h"

namespace stm32 {

class EstadoCpu : public ExtPartBase {
public:
    enum Modo { ACTIVA = 0, LPM0 = 1, LPM1, LPM2, LPM3, LPM4, LPMX5 = 6, PARADA = 7, RESET = 8 };
    // `modo`: lo que está haciendo ahora
    EstadoCpu(const std::string& nombre, std::function<int()> modo)
        : ExtPartBase("EstadoCpu", nombre.c_str()), nombre_(nombre), modo_(std::move(modo)) {}

    // El modo de un MSP430 por su SR [UG2 §2.3; UG5 §1.4]: CPUOFF, OSCOFF, SCG0, SCG1
    static int por_sr(uint16_t sr) {
        if (!(sr & 0x0010)) return ACTIVA;
        const bool scg0 = sr & 0x0040, scg1 = sr & 0x0080, oscoff = sr & 0x0020;
        if (oscoff && scg0 && scg1) return LPM4;
        if (scg0 && scg1) return LPM3;
        if (scg1) return LPM2;
        if (scg0) return LPM1;
        return LPM0;
    }

    int modo() const { return modo_ ? modo_() : ACTIVA; }
    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"lpm", "", 0.f, 1.f, true};
        if (i == 1) return {"depurador", "", 0.f, 1.f, true};
        return {"modo", "", 0.f, 8.f, true};
    }
    float valor_observable(unsigned i) const override {
        const int m = modo();
        if (i == 0) return (m >= LPM0 && m <= LPMX5) ? 1.f : 0.f;
        if (i == 1) return m == PARADA ? 1.f : 0.f;
        return float(m);
    }

private:
    std::string nombre_;
    std::function<int()> modo_;
};

} // namespace stm32
#endif
