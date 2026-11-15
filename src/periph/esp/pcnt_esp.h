// =============================================================================
// pcnt_esp.h — El contador de pulsos (PCNT) del ESP32-C6
//
// E11 de doc/analisis_esp32_riscv.md [TRM-C6 §37; pcnt_reg.h de ESP-IDF]:
//
//   * CUATRO UNIDADES, cada una con un contador de 16 bits con signo
//     (Ux_CNT, 0x30 + 4·u) y DOS CANALES; cada canal, dos entradas de la
//     matriz GPIO: la de pulsos (SIG) y la de control (CTRL) -las señales
//     101-116, en el orden de gpio_sig_map.h-;
//   * en cada flanco de SIG, el canal hace lo que digan CHn_POS_MODE (al
//     subir) o CHn_NEG_MODE (al bajar): 1 suma, 2 resta, 0 y 3 nada; y lo
//     que diga el nivel de CTRL lo cambia: CHn_HCTRL_MODE con CTRL alto,
//     CHn_LCTRL_MODE con CTRL bajo: 0 lo deja, 1 lo invierte, 2 y 3 no
//     cuentan. Es la decodificación en cuadratura de un encoder: dos canales
//     con SIG y CTRL cruzados cuentan los cuatro flancos de un ciclo;
//   * el FILTRO (CONF0: FILTER_EN, FILTER_THRES en ciclos del APB): una
//     entrada solo cambia si se queda en su nivel nuevo FILTER_THRES ciclos;
//     un pulso más corto no existe. Vale para SIG y para CTRL. Sale del reset
//     activo con 16 ciclos;
//   * los LÍMITES (CONF2: H_LIM y L_LIM): al llegar a uno, el contador vuelve
//     a 0. Los SUCESOS (CONF0: THR_*_EN): el límite alto, el bajo, el cero y
//     los dos umbrales (CONF1: THRES0 y THRES1). Cada suceso habilitado pone
//     el bit de la unidad en INT_RAW (0x40; fuente 62 de la matriz) y su bit
//     en Ux_STATUS (0x50 + 4·u): ZERO_MODE [1:0] (0 de positivo a 0, 1 de
//     negativo a 0, 2 negativo, 3 positivo), THRES1 2, THRES0 3, L_LIM 4,
//     H_LIM 5, ZERO 6. Los bits de suceso se borran al borrar la
//     interrupción de la unidad;
//   * CTRL (0x60): PULSE_CNT_RST_Ux (2·u; sale a 1: el contador a 0 y
//     quieto) y CNT_PAUSE_Ux (2·u+1).
//
// Las tareas del ETM del PCNT (PCNT_TASK_*) y sus sucesos no están en el C6
// de ESP-IDF (SOC_PCNT_SUPPORT_ETM no está): no se modelan.
// =============================================================================
#ifndef STM32_PERIPH_ESP_PCNT_ESP_H
#define STM32_PERIPH_ESP_PCNT_ESP_H

#include <systemc>
#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class PcntEsp : public BusSlave {
public:
    static constexpr unsigned N_UNIDADES = 4;
    std::function<void(bool)> irq;
    // Las entradas: (unidad, canal, ctrl) -> la señal de la matriz
    std::function<const sc_core::sc_signal_in_if<bool>&(unsigned u, unsigned c, bool ctrl)> entrada;
    double hz_apb = 80e6;
    uint32_t date = 0x19072601u;

    explicit PcntEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        reset_regs();
    }
    // Los procesos, cuando el SoC ya ha puesto `entrada`
    void end_of_elaboration() override {
        if (!entrada) return;
        for (unsigned u = 0; u < N_UNIDADES; ++u)
            for (unsigned c = 0; c < 2; ++c)
                for (unsigned k = 0; k < 2; ++k)
                    sc_core::sc_spawn([this, u, c, k] { filtro_proc(u, c, k != 0); });
    }
    void reset() { reset_regs(); publica(); }

    int16_t cuenta(unsigned u) const { return u < N_UNIDADES ? un_[u].cnt : 0; }
    uint64_t sucesos() const { return n_sucesos_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x30) {
            const Unidad& x = un_[off / 0xC];
            return x.conf[(off % 0xC) / 4];
        }
        if (off >= 0x30 && off < 0x40) return uint16_t(un_[(off - 0x30) / 4].cnt);
        if (off >= 0x50 && off < 0x60) return un_[(off - 0x50) / 4].status;
        switch (off) {
        case 0x40: return raw_;
        case 0x44: return raw_ & ena_;
        case 0x48: return ena_;
        case 0x60: return ctrl_;
        case 0xFC: return date;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        if (off < 0x30) {
            uint32_t& r = un_[off / 0xC].conf[(off % 0xC) / 4];
            r = (r & ~m) | (v & m);
            return;
        }
        switch (off) {
        case 0x48: ena_ = ((ena_ & ~m) | (v & m)) & 0xFu; publica(); return;
        case 0x4C:
            for (unsigned u = 0; u < N_UNIDADES; ++u)
                if ((v & m) >> u & 1u) un_[u].status &= 3u;         // los sucesos, con la interrupción
            raw_ &= ~(v & m); publica(); return;
        case 0x60:
            ctrl_ = (ctrl_ & ~m) | (v & m);
            for (unsigned u = 0; u < N_UNIDADES; ++u) if ((ctrl_ >> (2 * u)) & 1u) un_[u].cnt = 0;
            return;
        case 0xFC: date = v; return;
        default: return;
        }
    }

private:
    struct Unidad {
        uint32_t conf[3] = {0x3C10u, 0, 0};
        int16_t cnt = 0;
        uint32_t status = 0;
        bool fil[2][2] = {};           // [canal][ctrl]: el nivel filtrado
    };
    Unidad un_[N_UNIDADES];
    uint32_t raw_ = 0, ena_ = 0, ctrl_ = 0x55u, irq_ = 0;
    uint64_t n_sucesos_ = 0;

    void reset_regs() {
        for (Unidad& u : un_) u = Unidad();
        raw_ = ena_ = 0; ctrl_ = 0x55u;
        if (entrada)
            for (unsigned u = 0; u < N_UNIDADES; ++u)
                for (unsigned c = 0; c < 2; ++c)
                    for (unsigned k = 0; k < 2; ++k) un_[u].fil[c][k] = entrada(u, c, k != 0).read();
    }
    void publica() {
        const bool n = (raw_ & ena_) != 0;
        if (n != bool(irq_)) { irq_ = n; if (irq) irq(n); }
    }

    // Una entrada, por el filtro: cambia si se queda FILTER_THRES ciclos
    void filtro_proc(unsigned u, unsigned c, bool ctrl) {
        const sc_core::sc_signal_in_if<bool>& s = entrada(u, c, ctrl);
        un_[u].fil[c][ctrl] = s.read();
        for (;;) {
            wait(s.value_changed_event());
            for (;;) {
                const uint32_t c0 = un_[u].conf[0];
                if (!((c0 >> 10) & 1u) || (c0 & 0x3FFu) == 0) break;
                const sc_core::sc_time d(double(c0 & 0x3FFu) / hz_apb, sc_core::SC_SEC);
                const sc_core::sc_time t0 = sc_core::sc_time_stamp();
                wait(d, s.value_changed_event());
                if (sc_core::sc_time_stamp() - t0 >= d) break;
            }
            const bool v = s.read();
            if (v == un_[u].fil[c][ctrl]) continue;
            un_[u].fil[c][ctrl] = v;
            if (!ctrl) flanco(u, c, v);
        }
    }

    void flanco(unsigned u, unsigned c, bool sube) {
        Unidad& x = un_[u];
        if ((ctrl_ >> (2 * u)) & 3u) return;                     // en reset o en pausa
        const uint32_t c0 = x.conf[0];
        const unsigned sh = 16 + 8 * c;
        unsigned modo = (c0 >> (sube ? sh + 2 : sh)) & 3u;      // 1 suma, 2 resta
        if (modo != 1 && modo != 2) return;
        const unsigned cm = (c0 >> (x.fil[c][1] ? sh + 4 : sh + 6)) & 3u;
        if (cm >= 2) return;                                     // no cuenta
        if (cm == 1) modo = modo == 1 ? 2 : 1;                   // al revés
        const int16_t antes = x.cnt;
        x.cnt = int16_t(x.cnt + (modo == 1 ? 1 : -1));
        const int16_t hl = int16_t(x.conf[2] & 0xFFFFu), ll = int16_t(x.conf[2] >> 16);
        const int16_t t0 = int16_t(x.conf[1] & 0xFFFFu), t1 = int16_t(x.conf[1] >> 16);
        uint32_t ev = 0;
        if (x.cnt == t1 && ((c0 >> 15) & 1u)) ev |= 1u << 2;
        if (x.cnt == t0 && ((c0 >> 14) & 1u)) ev |= 1u << 3;
        bool limite = false;
        if (hl != 0 && x.cnt == hl) { limite = true; if ((c0 >> 12) & 1u) ev |= 1u << 5; }
        if (ll != 0 && x.cnt == ll) { limite = true; if ((c0 >> 13) & 1u) ev |= 1u << 4; }
        if (x.cnt == 0 && ((c0 >> 11) & 1u)) ev |= 1u << 6;
        if (limite) x.cnt = 0;
        const uint32_t zm = x.cnt == 0 ? (antes > 0 ? 0u : 1u) : x.cnt < 0 ? 2u : 3u;
        x.status = (x.status & ~3u) | zm;
        if (ev) {
            x.status |= ev;
            raw_ |= 1u << u;
            ++n_sucesos_;
            publica();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_PCNT_ESP_H
