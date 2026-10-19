// =============================================================================
// hib.h — El módulo de hibernación del MSP432E4 y su RTC (T6, entrega 3)
//
// En 0x400F_C000 [H; hw_hibernate.h; segunda fuente: hibernate.c de
// DriverLib]. Vive en su propio dominio, el de VBAT: un reset del sistema no
// lo toca, y por eso su reset en el modelo es solo el primer encendido.
//
//   CTL     RTCEN, HIBREQ (hibernar), RTCWEN y PINWEN (con qué se despierta),
//           CLK32EN (el oscilador de 32 768 Hz, con el cristal en XOSC0/XOSC1),
//           WRC (escritura completa: aquí siempre lista)
//   RTCC    los segundos; RTCLD los carga (y pone a cero los subsegundos)
//   RTCSS   RTCSSC, los subsegundos (1/32768), y RTCSSM, los de la alarma
//   RTCM0   la alarma, en segundos: RTCALT0 cuando RTCC = RTCM0 y RTCSSC = RTCSSM
//   IM/RIS/MIS/IC   RTCALT0, EXTW (el pin WAKE), WC
//   DATA    16 palabras que se conservan en hibernación
//
// HIBERNAR: con HIBREQ el chip se apaga -el núcleo y los periféricos en
// reset- hasta la alarma (con RTCWEN) o hasta que WAKE baje (con PINWEN), y
// entonces arranca como desde un encendido, pero con el RTC y DATA intactos y
// la causa en RIS. Es el LPM4.5 del P4.
//
// LO QUE NO ESTÁ: el modo calendario (CALCTL, CALn), la detección de
// manipulación (TP*), la retención de los GPIO (RETCLR), el aviso de batería
// baja y el ajuste fino del RTC (RTCT); y WRC no espera los ~92 µs del
// silicio [P-17 i].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_HIB_H
#define STM32_PERIPH_MSP432E4_HIB_H

#include <array>
#include <cmath>
#include <functional>
#include "../../common/periph_base.h"
#include "../../rcc/osc_pll.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class HibE4 : public BusSlave {
public:
    enum : uint32_t {
        R_RTCC = 0x000, R_RTCM0 = 0x004, R_RTCLD = 0x00C, R_CTL = 0x010, R_IM = 0x014,
        R_RIS = 0x018, R_MIS = 0x01C, R_IC = 0x020, R_RTCT = 0x024, R_RTCSS = 0x028,
        R_IO = 0x02C, R_DATA = 0x030, R_CALCTL = 0x300, R_LOCK = 0x360,
        R_PP = 0xFC0, R_CC = 0xFC8
    };
    static constexpr uint32_t CTL_RTCEN = 1u << 0, CTL_HIBREQ = 1u << 1,
        CTL_RTCWEN = 1u << 3, CTL_PINWEN = 1u << 4, CTL_CLK32EN = 1u << 6,
        CTL_WRC = 1u << 31;
    static constexpr uint32_t RIS_RTCALT0 = 1u << 0, RIS_EXTW = 1u << 3, RIS_WC = 1u << 4;

    sc_core::sc_out<bool> irq{"irq"};
    Oscillator osc{"osc32"};
    // El SoC: hibernar ya; y la frecuencia, para OSCSRC = RTC
    std::function<void()> pide_hibernar;

    explicit HibE4(sc_core::sc_module_name nm) : BusSlave(nm, e4::HIB_B, 0x1000) {
        SC_HAS_PROCESS(HibE4);
        osc.ready(s_rdy_); osc.clk(s_clk_); osc.freq_hz(s_hz_);
        osc.needs_source = true;
        osc.nominal_hz = e4::HZ_RTC;
        osc.t_startup_s = 1e-3;          // [modelo: el silicio tarda cientos de ms]
        osc.set_waveform(false);
        SC_THREAD(rtc_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(osc_proc); sensitive << osc.state_event(); dont_initialize();
    }
    double f_rtc() const { return (ctl_ & CTL_CLK32EN) ? osc.out_hz() : 0.0; }
    bool rtc_en_marcha() const { return (ctl_ & CTL_RTCEN) && f_rtc() > 0; }
    uint32_t segundos() const { return uint32_t(ticks() / 32768ull); }
    // La hibernación: lo que el SoC mira para despertar
    bool despierta_rtc() const { return ctl_ & CTL_RTCWEN; }
    bool despierta_pin() const { return ctl_ & CTL_PINWEN; }
    void marca_externa() { ris_ |= RIS_EXTW; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    const sc_core::sc_event& alarma() const { return alarma_ev_; }
    std::function<void()> al_cambiar;

protected:
    uint32_t ctl_ = 0, im_ = 0, ris_ = 0, rtcm0_ = 0xFFFFFFFFu, ssm_ = 0, rtct_ = 0x7FFF,
             io_ = 0, cc_ = 0, calctl_ = 0;
    std::array<uint32_t, 16> data_{};
    uint64_t ticks0_ = 0;                 // en 1/32768 s, en t_ref_
    sc_core::sc_time t_ref_;
    double f_ref_ = 0;
    sc_core::sc_event ev_, pub_ev_, alarma_ev_;
    sc_core::sc_signal<bool> s_rdy_{"s_rdy"}, s_clk_{"s_clk"};
    sc_core::sc_signal<double> s_hz_{"s_hz"};

    uint64_t ticks() const {
        if (f_ref_ <= 0) return ticks0_;
        return ticks0_ + uint64_t(std::floor((sc_core::sc_time_stamp() - t_ref_).to_seconds() * f_ref_ + 1e-9));
    }
    void ancla() {
        ticks0_ = ticks();
        t_ref_ = sc_core::sc_time_stamp();
        f_ref_ = (ctl_ & CTL_RTCEN) ? f_rtc() : 0.0;
        ev_.notify(sc_core::SC_ZERO_TIME);
        if (al_cambiar) al_cambiar();
    }
    void osc_proc() { ancla(); }
    void reset_proc() {
        if (rst_n.read()) return;
        ctl_ = im_ = ris_ = io_ = cc_ = calctl_ = 0;
        rtcm0_ = 0xFFFFFFFFu; ssm_ = 0; rtct_ = 0x7FFF;
        data_.fill(0);
        ticks0_ = 0; f_ref_ = 0;
        osc.enable(false);
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() { irq.write((ris_ & im_) != 0); }

    void rtc_proc() {
        for (;;) {
            if (f_ref_ <= 0) { wait(ev_); continue; }
            const uint64_t ahora = ticks();
            const uint64_t meta = (uint64_t(rtcm0_) << 15) | (ssm_ & 0x7FFFu);
            if (meta < ahora) { wait(ev_); continue; }    // ya pasó: hasta que se reprograme
            const double s = double(meta - ahora) / f_ref_;
            if (s > 0) {
                wait(sc_core::sc_time(s, sc_core::SC_SEC), ev_);
                if (ev_.triggered()) continue;
            }
            ris_ |= RIS_RTCALT0;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
            alarma_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(sc_core::sc_time(1.5 / f_ref_, sc_core::SC_SEC), ev_);
        }
    }

    uint32_t reg_read(uint32_t off) override {
        if (off >= R_DATA && off < R_DATA + 64) return data_[(off - R_DATA) / 4];
        switch (off) {
            case R_RTCC: return segundos();
            case R_RTCM0: return rtcm0_;
            case R_CTL: return ctl_ | CTL_WRC;
            case R_IM: return im_;
            case R_RIS: return ris_ | RIS_WC;
            case R_MIS: return (ris_ | RIS_WC) & im_;
            case R_RTCT: return rtct_;
            case R_RTCSS: return (ssm_ << 16) | uint32_t(ticks() & 0x7FFFu);
            case R_IO: return io_ | (1u << 31);       // IOWRC: siempre lista
            case R_CALCTL: return calctl_;
            case R_PP: return 0x1u;                  // WAKENC [1F]
            case R_CC: return cc_;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off >= R_DATA && off < R_DATA + 64) { data_[(off - R_DATA) / 4] = v; return; }
        switch (off) {
            case R_RTCM0: rtcm0_ = v; ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_RTCLD: ticks0_ = uint64_t(v) << 15; t_ref_ = sc_core::sc_time_stamp(); ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_CTL: {
                ancla();
                const bool hib = (v & CTL_HIBREQ) && !(ctl_ & CTL_HIBREQ);
                ctl_ = v & 0x400F67DFu & ~CTL_HIBREQ;
                osc.enable((ctl_ & CTL_CLK32EN) != 0);
                ancla();
                if (hib && pide_hibernar) pide_hibernar();
                break;
            }
            case R_IM: im_ = v & 0xFDu; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_IC: ris_ &= ~v; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_RTCT: rtct_ = v & 0xFFFFu; break;
            case R_RTCSS: ssm_ = (v >> 16) & 0x7FFFu; ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_IO: io_ = v; break;
            case R_CALCTL: calctl_ = v; break;
            case R_CC: cc_ = v & 1u; break;
            default: break;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_HIB_H
