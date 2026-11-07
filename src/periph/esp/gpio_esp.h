// =============================================================================
// gpio_esp.h — GPIO, la matriz GPIO y el IO_MUX de Espressif [TRM-C3 §5]
//
// E3 de doc/analisis_esp32_riscv.md, §4. Dos bloques de registros sobre un
// mismo estado: `GpioEsp` (GPIO y la matriz, 0x6000_4000 en el C3) e
// `IoMuxEsp` (0x6000_9000). Cada pin es un `Pad`, el eléctrico de siempre
// (pins/pad.h), con su nodo analógico: un LED de la placa, un pulsador o la
// UART de otro chip se cuelgan de él igual que de un STM32.
//
// LA SALIDA de un pin la decide el IO_MUX (MCU_SEL):
//   * función 1 (GPIO) -y la 0 en los pines cuya función 0 es GPIOn-: la
//     matriz. GPIO_FUNCn_OUT_SEL_CFG elige la señal (OUT_SEL; 128 es
//     GPIO_OUT), la invierte (INV_SEL) y dice quién manda en la habilitación:
//     GPIO_ENABLE (OEN_SEL = 1) o el periférico (OEN_SEL = 0);
//   * función 0 en un pin con función DIRECTA (U0TXD en GPIO21, U0RXD en
//     GPIO20): la señal del periférico, sin la matriz;
//   * otra (FSPI*, la Flash, el JTAG): no modelada todavía; el pin en alta
//     impedancia (E4, E6, E8).
//   Pull-up y pull-down (FUN_WPU/WPD), fuerza (FUN_DRV, 0-3) y drenador
//   abierto (GPIO_PINn_PAD_DRIVER).
//
// LA ENTRADA: GPIO_IN es el nivel del pad si FUN_IE; una señal de periférico
// (GPIO_FUNCy_IN_SEL_CFG) viene de un pin (IN_SEL 0-21), de un 1 (0x1E) o de
// un 0 (0x1F), invertida o no, con SIG_IN_SEL = 1; con 0, de su pin directo
// si su IO_MUX está en la función 0, y si no, 0.
//
// LAS INTERRUPCIONES de pin (GPIO_PINn: INT_TYPE 1 subida, 2 bajada, 3 los
// dos, 4 nivel bajo, 5 nivel alto; INT_ENA bit 0, la CPU) van a GPIO_STATUS
// y, juntas, a la fuente GPIO_PROCPU_INTR de la matriz.
//
// LA GPIO DEDICADA (cpu_gpio_*: señales 28-35) la mueven los CSR del núcleo
// (soc/esp32c3: cpu_gpio_oen, cpu_gpio_in, cpu_gpio_out).
//
// LOS PINES DE ARRANQUE: `latch_arranque()` guarda en GPIO_STRAP el nivel de
// GPIO9, GPIO8, GPIO3 y GPIO2 (bits 3-0) [TRM-C3 §7; los bits 1-0, de
// modelo]: lo llama el SoC al soltar el reset de chip.
// =============================================================================
#ifndef STM32_PERIPH_ESP_GPIO_ESP_H
#define STM32_PERIPH_ESP_GPIO_ESP_H

#include <systemc>
#include <functional>
#include <string>
#include <vector>
#include "../../common/periph_base.h"
#include "../../common/analog_net.h"
#include "../../pins/pad.h"
#include "../../pins/pin_mux.h"

namespace stm32 {

// La función 0 de un pin y su configuración tras el reset [TRM-C3 tabla 5.12-1]
struct PinEsp {
    enum Tipo : uint8_t { Gpio, EntradaDirecta, SalidaDirecta, NoModelada };
    const char* fn0;        // "GPIO2", "U0TXD", "MTMS"...
    Tipo tipo;
    int sig;                // la señal de la función directa
    bool ie, wpu, wpd;      // tras el reset
    uint8_t drv;            // FUN_DRV tras el reset
};

class GpioEsp : public BusSlave {
public:
    static constexpr unsigned N_SIG = 128;
    static constexpr unsigned SIG_GPIO_OUT = 128;

    // Las señales de los periféricos: salida, habilitación de salida y entrada
    // (varios escritores: un periférico desde su proceso y el núcleo al tocar
    // un registro o un CSR)
    using Senal = sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS>;
    sc_core::sc_vector<Senal> sig_out{"sig_out", N_SIG};
    sc_core::sc_vector<Senal> sig_oe{"sig_oe", N_SIG};
    sc_core::sc_vector<Senal> sig_in{"sig_in", N_SIG};

    std::function<void(bool)> irq;          // GPIO_PROCPU_INTR

    std::vector<Pad*> pad;
    std::vector<analog_net_if*> nodo;
    std::vector<AnalogNet*> propio;

    GpioEsp(sc_core::sc_module_name nm, uint32_t base, const std::vector<PinEsp>& pines,
            const std::vector<bool>& bonded, const Cableado& cab, unsigned puerto_cab = 0)
        : BusSlave(nm, base, 0x1000), pines_(pines), n_(unsigned(pines.size())),
          s_drive_("s_drive", n_), s_din_("s_din", n_), s_dok_("s_dok", n_), s_oor_("s_oor", n_) {
        char b[48];
        pin_cfg_.assign(n_, 0); out_cfg_.assign(n_, SIG_GPIO_OUT); mux_.assign(n_, 0);
        nivel_.assign(n_, false);
        for (unsigned p = 0; p < n_; ++p) {
            analog_net_if* n = cab.busca(puerto_cab, p);
            AnalogNet* pr = nullptr;
            if (!n) {
                std::snprintf(b, sizeof b, "net_GPIO%u", p);
                pr = new AnalogNet(b);
                pr->nombre_esq = std::string("GPIO") + std::to_string(p);
                n = pr;
            }
            propio.push_back(pr);
            nodo.push_back(n);
            std::snprintf(b, sizeof b, "pad_GPIO%u", p);
            Pad* q = new Pad(b);
            q->vdd = 3.3;
            q->ft_5v = false;
            q->r_pull = 45e3;                       // [DS-C3 tabla 5-4: R_PU/R_PD ~45 k]
            // FUN_DRV: ~5, 10, 20 y 40 mA -> resistencia de salida de modelo
            q->r_on_speed[0] = 160.0; q->r_on_speed[1] = 80.0;
            q->r_on_speed[2] = 40.0;  q->r_on_speed[3] = 20.0;
            for (double& t : q->t_pd_speed) t = 2e-9;
            q->bonded = p < bonded.size() ? bonded[p] : true;
            q->net(*n);
            q->drive(s_drive_[p]);
            q->din(s_din_[p]);
            q->din_valid(s_dok_[p]);
            q->out_of_range(s_oor_[p]);
            pad.push_back(q);
        }
        for (unsigned s = 0; s < N_SIG; ++s) in_cfg_[s] = 0;
        reset_regs();
        SC_HAS_PROCESS(GpioEsp);
        SC_METHOD(salida_proc);
        sensitive << ev_cfg_;
        for (unsigned s = 0; s < N_SIG; ++s) sensitive << sig_out[s] << sig_oe[s];
        dont_initialize();
        SC_METHOD(entrada_proc);
        sensitive << ev_cfg_;
        for (unsigned p = 0; p < n_; ++p) sensitive << s_din_[p];
    }
    ~GpioEsp() override {
        for (Pad* q : pad) delete q;
        for (AnalogNet* n : propio) delete n;
    }

    // El reset del bloque (lo llama el SoC)
    void reset() { reset_regs(); }
    // El primer gobierno de los pads, un poco después de arrancar: los Pad
    // miran su entrada en el delta 0 y esperan un delta, así que un cambio en
    // ese delta se les escaparía (y GPIO9 se quedaría sin su pull-up)
    void start_of_simulation() override {
        ev_cfg_.cancel();
        ev_cfg_.notify(sc_core::sc_time(1, sc_core::SC_NS));
    }

    unsigned n_pines() const { return n_; }
    analog_net_if& nodo_de(unsigned p) { return *nodo.at(p); }
    bool nivel(unsigned p) const { return p < n_ && s_din_[p].read(); }
    uint32_t out() const { return out_; }
    uint32_t enable() const { return enable_; }
    uint32_t strap() const { return strap_; }

    // --- El IO_MUX (lo escribe IoMuxEsp) -----------------------------------------
    uint32_t mux(unsigned p) const { return p < n_ ? mux_[p] : 0; }
    void pon_mux(unsigned p, uint32_t v) { if (p < n_) { mux_[p] = v; cambia(); } }
    uint32_t pin_ctrl = 0x7FFu;
    void reset_mux() {
        for (unsigned p = 0; p < n_; ++p) {
            const PinEsp& x = pines_[p];
            mux_[p] = (x.ie ? 1u << 9 : 0u) | (x.wpu ? 1u << 8 : 0u) | (x.wpd ? 1u << 7 : 0u) |
                      (uint32_t(x.drv & 3u) << 10);
        }
        cambia();
    }

    // --- Los pines de arranque -------------------------------------------------
    void latch_arranque() {
        auto n = [&](unsigned p) { return p < n_ && s_din_[p].read() ? 1u : 0u; };
        strap_ = (n(9) << 3) | (n(8) << 2) | (n(3) << 1) | n(2);
    }
    // RTC_CNTL_FORCE_DOWNLOAD_BOOT: «1x» pasa a «01» [TRM-C3 §7.2]
    void fuerza_descarga() { if (strap_ & 8u) strap_ = (strap_ & 3u) | 4u; }

    // --- La GPIO dedicada --------------------------------------------------------
    void cpu_gpio_out(uint8_t v) {
        cpu_out_ = v;
        for (unsigned i = 0; i < 8; ++i) sig_out[28 + i].write((v >> i) & 1u);
    }
    void cpu_gpio_oen(uint8_t v) {
        cpu_oen_ = v;
        for (unsigned i = 0; i < 8; ++i) sig_oe[28 + i].write((v >> i) & 1u);
    }
    uint8_t cpu_gpio_in() const {
        uint8_t v = 0;
        for (unsigned i = 0; i < 8; ++i) v |= uint8_t(valor_entrada(28 + i)) << i;
        return v;
    }
    uint8_t cpu_out() const { return cpu_out_; }
    uint8_t cpu_oen() const { return cpu_oen_; }

    // El valor que ve ahora una señal de entrada (sin esperar al delta)
    bool valor_entrada(unsigned s) const {
        const uint32_t c = in_cfg_[s];
        bool v;
        if (c & (1u << 6)) {
            const unsigned src = c & 0x1Fu;
            v = src == 0x1E ? true : src == 0x1F ? false : (src < n_ && ie(src) && s_din_[src].read());
        } else {
            v = false;
            for (unsigned p = 0; p < n_; ++p)
                if (pines_[p].tipo == PinEsp::EntradaDirecta && unsigned(pines_[p].sig) == s &&
                    mcu_sel(p) == 0)
                    v = ie(p) && s_din_[p].read();
        }
        return (c & (1u << 5)) ? !v : v;
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return bt_select_;
        case 0x04: return out_;
        case 0x20: return enable_;
        case 0x38: return strap_;
        case 0x3C: return in_;
        case 0x44: return status_;
        case 0x5C: return status_ & cpu_ena();
        case 0x14C: return status_next_;
        case 0x62C: return clock_gate_;
        case 0x6FC: return 0x2006130u;
        default: break;
        }
        if (off >= 0x74 && off < 0x74 + 4 * n_) return pin_cfg_[(off - 0x74) / 4];
        if (off >= 0x154 && off < 0x154 + 4 * N_SIG) return in_cfg_[(off - 0x154) / 4];
        if (off >= 0x554 && off < 0x554 + 4 * n_) return out_cfg_[(off - 0x554) / 4];
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        const uint32_t pm = (n_ >= 32 ? 0xFFFFFFFFu : (1u << n_) - 1u);
        v &= m;
        switch (off) {
        case 0x00: bt_select_ = (bt_select_ & ~m) | v; return;
        case 0x04: out_ = ((out_ & ~m) | v) & pm; cambia(); return;
        case 0x08: out_ |= v & pm; cambia(); return;
        case 0x0C: out_ &= ~v; cambia(); return;
        case 0x20: enable_ = ((enable_ & ~m) | v) & pm; cambia(); return;
        case 0x24: enable_ |= v & pm; cambia(); return;
        case 0x28: enable_ &= ~v; cambia(); return;
        case 0x44: status_ = ((status_ & ~m) | v) & pm; publica(); cambia(); return;
        case 0x48: status_ |= v & pm; publica(); cambia(); return;
        case 0x4C: status_ &= ~v; publica(); cambia(); return;
        case 0x62C: clock_gate_ = (clock_gate_ & ~m) | v; return;
        default: break;
        }
        if (off >= 0x74 && off < 0x74 + 4 * n_) {
            uint32_t& r = pin_cfg_[(off - 0x74) / 4]; r = (r & ~m) | v; cambia(); return;
        }
        if (off >= 0x154 && off < 0x154 + 4 * N_SIG) {
            uint32_t& r = in_cfg_[(off - 0x154) / 4]; r = (r & ~m) | v; cambia(); return;
        }
        if (off >= 0x554 && off < 0x554 + 4 * n_) {
            uint32_t& r = out_cfg_[(off - 0x554) / 4]; r = (r & ~m) | v; cambia(); return;
        }
    }

private:
    std::vector<PinEsp> pines_;
    unsigned n_;
    sc_core::sc_vector<sc_core::sc_signal<PadDrive>> s_drive_;
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_din_, s_dok_, s_oor_;
    std::vector<uint32_t> pin_cfg_, out_cfg_, mux_;
    uint32_t in_cfg_[N_SIG];
    std::vector<bool> nivel_;
    uint32_t bt_select_ = 0, out_ = 0, enable_ = 0, strap_ = 0, in_ = 0, status_ = 0,
             status_next_ = 0, clock_gate_ = 1;
    uint8_t cpu_out_ = 0, cpu_oen_ = 0;
    bool irq_ = false;
    sc_core::sc_event ev_cfg_;

    void reset_regs() {
        for (uint32_t& c : pin_cfg_) c = 0;
        for (uint32_t& c : out_cfg_) c = SIG_GPIO_OUT;
        for (uint32_t& c : in_cfg_) c = 0;
        out_ = enable_ = status_ = 0;
        reset_mux();
    }
    void cambia() { ev_cfg_.notify(sc_core::SC_ZERO_TIME); }
    unsigned mcu_sel(unsigned p) const { return (mux_[p] >> 12) & 7u; }
    bool ie(unsigned p) const { return (mux_[p] >> 9) & 1u; }
    uint32_t cpu_ena() const {
        uint32_t r = 0;
        for (unsigned p = 0; p < n_; ++p) if ((pin_cfg_[p] >> 13) & 1u) r |= 1u << p;
        return r;
    }
    void publica() {
        const bool n = (status_ & cpu_ena()) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }

    void salida_proc() {
        for (unsigned p = 0; p < n_; ++p) {
            const PinEsp& x = pines_[p];
            const unsigned sel = mcu_sel(p);
            PadDrive d;
            const uint32_t mx = mux_[p];
            d.pupd = (mx & (1u << 8)) ? 1 : (mx & (1u << 7)) ? 2 : 0;
            d.speed = uint8_t((mx >> 10) & 3u);
            d.od = (pin_cfg_[p] >> 2) & 1u;
            const bool matriz = sel == 1 || (sel == 0 && x.tipo == PinEsp::Gpio);
            if (matriz) {
                const uint32_t c = out_cfg_[p];
                const unsigned os = c & 0xFFu;
                if (os >= SIG_GPIO_OUT) {
                    d.out = (out_ >> p) & 1u;
                    d.oe = (enable_ >> p) & 1u;
                } else {
                    d.out = sig_out[os].read() != bool((c >> 8) & 1u);
                    const bool oe = ((c >> 9) & 1u) ? bool((enable_ >> p) & 1u) : sig_oe[os].read();
                    d.oe = oe != bool((c >> 10) & 1u);
                }
            } else if (sel == 0 && x.tipo == PinEsp::SalidaDirecta) {
                d.out = sig_out[unsigned(x.sig)].read();
                d.oe = sig_oe[unsigned(x.sig)].read();
            }
            if (!(s_drive_[p].read() == d)) s_drive_[p].write(d);
        }
    }

    void entrada_proc() {
        uint32_t in = 0;
        for (unsigned p = 0; p < n_; ++p) if (ie(p) && s_din_[p].read()) in |= 1u << p;
        // Interrupciones de pin
        const uint32_t sube = in & ~in_, baja = ~in & in_;
        for (unsigned p = 0; p < n_; ++p) {
            const unsigned t = (pin_cfg_[p] >> 7) & 7u;
            const bool b = (in >> p) & 1u;
            bool salta = false;
            switch (t) {
            case 1: salta = (sube >> p) & 1u; break;
            case 2: salta = (baja >> p) & 1u; break;
            case 3: salta = ((sube | baja) >> p) & 1u; break;
            case 4: salta = !b; break;
            case 5: salta = b; break;
            default: break;
            }
            if (salta) status_ |= 1u << p;
        }
        in_ = in;
        for (unsigned s = 0; s < N_SIG; ++s) {
            const bool v = valor_entrada(s);
            if (sig_in[s].read() != v) sig_in[s].write(v);
        }
        publica();
    }
};

// El IO_MUX: un registro por pin (IO_MUX_GPIOn_REG en 0x04 + 4·n) y PIN_CTRL
class IoMuxEsp : public BusSlave {
public:
    IoMuxEsp(sc_core::sc_module_name nm, uint32_t base, GpioEsp& g)
        : BusSlave(nm, base, 0x1000), g_(g) {}
protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == 0) return g_.pin_ctrl;
        if (off >= 4 && off < 4 + 4 * g_.n_pines()) return g_.mux((off - 4) / 4);
        if (off == 0xFC) return 0x2006050u;
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        if (off == 0) { g_.pin_ctrl = (g_.pin_ctrl & ~m) | (v & m); return; }
        if (off >= 4 && off < 4 + 4 * g_.n_pines()) {
            const unsigned p = (off - 4) / 4;
            g_.pon_mux(p, ((g_.mux(p) & ~m) | (v & m)) & 0xFFFFu);
        }
    }
private:
    GpioEsp& g_;
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_GPIO_ESP_H
