// =============================================================================
// etm_esp.h — La matriz de sucesos y tareas (SOC_ETM) del ESP32-C6
//
// E11 de doc/analisis_esp32_riscv.md [TRM-C6 §11; soc_etm_reg.h y
// soc_etm_source.h de ESP-IDF]. Un periférico da un SUCESO (un número de
// soc_etm_source.h: GPIO_EVT_CH0_RISE_EDGE es el 1) y otro hace una TAREA
// (GPIO_TASK_CH0_SET es la 1), sin la CPU:
//
//   * 50 CANALES; cada uno, su suceso (CHn_EVT_ID, 0x18 + 8·n) y su tarea
//     (CHn_TASK_ID, 0x1C + 8·n). Se habilitan en CH_ENA_AD0/AD1 (0x00/0x0C),
//     con sus registros de poner (+4) y quitar (+8);
//   * cuando un periférico da un suceso, cada canal habilitado con ese suceso
//     manda su tarea. En el chip tarda unos ciclos del reloj del ETM; aquí, en
//     el acto [de modelo].
//
// Quién da sucesos y hace tareas lo decide el SoC (`tarea`): en el C6 de
// mcu-sim, el GPIO (los 8 canales de GPIO_EXT), los dos grupos de
// temporizadores, el SYSTIMER (sus sucesos) y el MCPWM. Los demás números
// se aceptan y no pasa nada [declarado].
// =============================================================================
#ifndef STM32_PERIPH_ESP_ETM_ESP_H
#define STM32_PERIPH_ESP_ETM_ESP_H

#include <systemc>
#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class EtmEsp : public BusSlave {
public:
    static constexpr unsigned N_CANALES = 50;
    // Hacer una tarea (su número)
    std::function<void(unsigned id)> tarea;
    uint32_t date = 0x2203092u;

    explicit EtmEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() {
        ena_ = 0; clk_en_ = 0;
        for (unsigned c = 0; c < N_CANALES; ++c) evt_[c] = task_[c] = 0;
    }
    // Un periférico da un suceso
    void suceso(unsigned id) {
        if (!id || prof_ > 4) return;
        ++prof_;
        ++n_sucesos_;
        for (unsigned c = 0; c < N_CANALES; ++c)
            if (((ena_ >> c) & 1u) && evt_[c] == id && task_[c]) { ++n_tareas_; if (tarea) tarea(task_[c]); }
        --prof_;
    }
    uint64_t sucesos() const { return n_sucesos_; }
    uint64_t tareas() const { return n_tareas_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return uint32_t(ena_);
        case 0x0C: return uint32_t(ena_ >> 32);
        case 0x1A8: return clk_en_;
        case 0x1AC: return date;
        default: break;
        }
        if (off >= 0x18 && off < 0x18 + 8 * N_CANALES) {
            const unsigned c = (off - 0x18) / 8;
            return (off & 4u) ? task_[c] : evt_[c];
        }
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        const uint64_t mh = (uint64_t(1) << (N_CANALES - 32)) - 1;
        switch (off) {
        case 0x00: ena_ = (ena_ & ~0xFFFFFFFFull) | v; return;
        case 0x04: ena_ |= v; return;
        case 0x08: ena_ &= ~uint64_t(v); return;
        case 0x0C: ena_ = (ena_ & 0xFFFFFFFFull) | (uint64_t(v & mh) << 32); return;
        case 0x10: ena_ |= uint64_t(v & mh) << 32; return;
        case 0x14: ena_ &= ~(uint64_t(v & mh) << 32); return;
        case 0x1A8: clk_en_ = v & 1u; return;
        case 0x1AC: date = v & 0xFFFFFFFu; return;
        default: break;
        }
        if (off >= 0x18 && off < 0x18 + 8 * N_CANALES) {
            const unsigned c = (off - 0x18) / 8;
            ((off & 4u) ? task_[c] : evt_[c]) = v & 0xFFu;
        }
    }

private:
    uint64_t ena_ = 0;
    uint32_t clk_en_ = 0, evt_[N_CANALES] = {}, task_[N_CANALES] = {};
    unsigned prof_ = 0;
    uint64_t n_sucesos_ = 0, n_tareas_ = 0;
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_ETM_ESP_H
