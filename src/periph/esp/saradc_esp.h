// =============================================================================
// saradc_esp.h — El SAR ADC de los ESP32-C3 (APB_SARADC) y su sensor de
//                temperatura
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §34-35; apb_saradc_reg.h]. Lo que
// usan `adc_oneshot` y `temperature_sensor` de ESP-IDF:
//
//   * la lectura suelta (ONETIME_SAMPLE): SARADC1_ONETIME_SAMPLE (ADC1, canales
//     0-4 = GPIO0-4) o SARADC2 (ADC2, canal 0 = GPIO5), ONETIME_CHANNEL,
//     ONETIME_ATTEN; al subir ONETIME_START convierte (unos 10 us) y deja 12
//     bits en 1_DATA_STATUS / 2_DATA_STATUS y ADC1/ADC2_DONE en INT_RAW
//     (bits 31 y 30, a la fuente 43). El fondo de escala por atenuación, el de
//     la hoja de datos: 0 dB 750 mV, 2,5 dB 1050, 6 dB 1300, 11 dB 2500;
//   * el sensor de temperatura: con TSENS_PU, TSENS_OUT (8 bits) da
//     (T + 20,52) / 0,4386, la fórmula de ESP-IDF con el rango -10..80 °C
//     (desplazamiento 0) y sin la calibración del eFuse. `temperatura_c`.
//
// LO QUE NO SE MODELA: el modo continuo con la DMA (patrones, filtros,
// umbrales: guardan), la calibración y el ruido. Un valor medido es exacto.
// =============================================================================
#ifndef STM32_PERIPH_ESP_SARADC_ESP_H
#define STM32_PERIPH_ESP_SARADC_ESP_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"
#include "../../common/analog_net.h"

namespace stm32 {

class SarAdcEsp : public BusSlave {
public:
    std::function<void(bool)> irq;
    std::function<analog_net_if*(unsigned)> pad;      // GPIOn
    double temperatura_c = 25.0;

    explicit SarAdcEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(SarAdcEsp);
        SC_THREAD(conv_proc);
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        d1_ = d2_ = 0; raw_ = ena_ = 0; tsens_ = 0; uno_ = false;
        evalua();
    }
    uint64_t conversiones() const { return n_conv_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x2C: return d1_;
        case 0x30: return d2_;
        case 0x40: return ena_;
        case 0x44: return raw_;
        case 0x48: return raw_ & ena_;
        case 0x58: return (tsens_ & ~0xFFu) | ((tsens_ >> 22) & 1u ? sale_tsens() : 0u);
        case 0x3FC: return 0x2007171u;
        default: return off < 0x400 ? r_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x20: {
            const bool antes = (r_[8] >> 29) & 1u;
            r_[8] = v;
            if (((v >> 29) & 1u) && !antes) { uno_ = true; ev_.notify(sc_core::SC_ZERO_TIME); }
            return;
        }
        case 0x40: ena_ = v; evalua(); return;
        case 0x4C: raw_ &= ~v; evalua(); return;
        case 0x58: tsens_ = v; return;
        default: if (off < 0x400) r_[off / 4] = v; return;
        }
    }

private:
    uint32_t r_[256] = {};
    uint32_t d1_ = 0, d2_ = 0, raw_ = 0, ena_ = 0, tsens_ = 0;
    bool uno_ = false, irq_ = false;
    uint64_t n_conv_ = 0;
    sc_core::sc_event ev_;

    void evalua() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    uint32_t sale_tsens() const {
        const double o = (temperatura_c + 20.52) / 0.4386;
        return uint32_t(std::clamp(std::lround(o), 0L, 255L));
    }
    static double fondo(unsigned at) { return at == 0 ? 0.75 : at == 1 ? 1.05 : at == 2 ? 1.30 : 2.50; }
    void conv_proc() {
        for (;;) {
            while (!uno_) sc_core::wait(ev_);
            uno_ = false;
            sc_core::wait(10, sc_core::SC_US);
            const uint32_t c = r_[8];
            const unsigned ch = (c >> 25) & 0xFu, at = (c >> 23) & 3u;
            const bool adc1 = (c >> 31) & 1u, adc2 = (c >> 30) & 1u;
            const unsigned gpio = adc1 ? ch : 5u + ch;
            analog_net_if* n = pad ? pad(gpio) : nullptr;
            const double v = n ? double(n->voltage()) : 0.0;
            const uint32_t d = uint32_t(std::clamp(std::lround(v / fondo(at) * 4095.0), 0L, 4095L));
            ++n_conv_;
            if (adc1) { d1_ = d; raw_ |= 1u << 31; }
            else if (adc2) { d2_ = d; raw_ |= 1u << 30; }
            evalua();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_SARADC_ESP_H
