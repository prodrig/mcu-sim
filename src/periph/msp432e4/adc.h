// =============================================================================
// adc.h — Los dos ADC de 12 bits del MSP432E4 (T6, entrega 3)
//
// ADC0 y ADC1, con cuatro SECUENCIADORES cada uno: SS0 de 8 pasos, SS1 y SS2
// de 4, SS3 de 1, cada uno con su FIFO de ese tamaño [H; hw_adc.h; segunda
// fuente: adc.c de DriverLib y ADCMSP432E4 de TI-Drivers, que da los pines]:
//
//   ACTSS   qué secuenciadores están activos (y BUSY)
//   EMUX    qué los dispara: el procesador (PSSI), un temporizador (5), o
//           «siempre» (0xF)
//   PSSI    dispararlos a mano
//   SSMUXn, SSEMUXn   el canal de cada paso (AIN0..19: el bit de SSEMUX suma 16)
//   SSCTLn  por paso: D (diferencial), END, IE (interrumpe al acabar ese
//           paso) y TS (el sensor de temperatura)
//   SSFIFOn, SSFSTATn   los resultados y cuántos hay
//   RIS/IM/ISC   una interrupción por secuenciador (INRn)
//   OSTAT/USTAT  desbordamiento y vaciado de las FIFO
//   CC      el reloj: el VCO del PLL dividido (0) o ALTCLK (1); sin PLL, PIOSC
//
// LOS CANALES son los nodos analógicos de los pines [TI-Drivers, Board_init.c
// de la LaunchPad: ADCMSP432E4_PE_3_A0 ...]: AIN0-3 en PE3-PE0, AIN4-7 en
// PD7-PD4, AIN8-9 en PE5-PE4, AIN10-11 en PB4-PB5, AIN12-15 en PD3-PD0 y
// AIN16-19 en PK0-PK3. El pin tiene que estar en modo analógico (AMSEL): si
// no, el canal lee 0 y el modelo avisa una vez.
//
// EL CONVERTIDOR ES IDEAL: VREFP = VDDA, VREFN = GNDA, sin ruido ni error.
// Cada muestra cuesta el tiempo de muestreo de SSTSH más 16 ciclos del reloj
// del ADC [1F: P-17 i]. El sensor de temperatura da (147,5 - T) / 75 V, la
// fórmula del datasheet al revés [1F].
//
// LO QUE NO ESTÁ: los comparadores digitales (DC*), el promediado (SAC), la
// fase de muestreo (SPC), la DMA del ADC, los disparos de GPIO, PWM y
// comparador, y la sincronización entre los dos ADC (GSYNC) [P-17 i].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_ADC_H
#define STM32_PERIPH_MSP432E4_ADC_H

#include <cmath>
#include <deque>
#include <functional>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class AdcE4 : public BusSlave {
public:
    enum : uint32_t {
        R_ACTSS = 0x000, R_RIS = 0x004, R_IM = 0x008, R_ISC = 0x00C, R_OSTAT = 0x010,
        R_EMUX = 0x014, R_USTAT = 0x018, R_TSSEL = 0x01C, R_SSPRI = 0x020, R_SPC = 0x024,
        R_PSSI = 0x028, R_SAC = 0x030, R_DCISC = 0x034, R_CTL = 0x038,
        R_SS0 = 0x040, R_PP = 0xFC0, R_PC = 0xFC4, R_CC = 0xFC8
    };
    static constexpr unsigned PROF[4] = {8, 4, 4, 1};

    sc_core::sc_out<bool> irq[4];
    // La tensión de un canal (0..19), la de las referencias, y si su pin está
    // en modo analógico: los pone el SoC
    std::function<double(unsigned)> v_canal;
    std::function<bool(unsigned)> es_analogico;
    std::function<double()> v_ref;
    std::function<double(unsigned cs)> f_reloj;
    double temperatura = 25.0;

    AdcE4(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(AdcE4);
        SC_THREAD(motor);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    // Un vencimiento de temporizador con ADCEV
    void disparo_temporizador() {
        for (unsigned s = 0; s < 4; ++s)
            if (((emux_ >> (4 * s)) & 0xFu) == 5u) pend_ |= 1u << s;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    unsigned muestras() const { return n_; }
    unsigned avisos_amsel() const { return n_amsel_; }

protected:
    struct Seq { uint32_t mux = 0, ctl = 0, emux = 0, tsh = 0, op = 0, dc = 0; std::deque<uint16_t> fifo; };
    uint32_t actss_ = 0, ris_ = 0, im_ = 0, ostat_ = 0, emux_ = 0, ustat_ = 0, tssel_ = 0,
             sspri_ = 0x3210, spc_ = 0, sac_ = 0, ctl_ = 0, cc_ = 0;
    Seq ss_[4];
    uint32_t pend_ = 0;
    bool ocupado_ = false, avisado_amsel_ = false;
    unsigned n_ = 0, n_amsel_ = 0;
    sc_core::sc_event ev_, pub_ev_;

    void pub_proc() { for (unsigned s = 0; s < 4; ++s) irq[s].write(((ris_ & im_) >> s) & 1u); }
    void reset_proc() {
        if (rst_n.read()) return;
        actss_ = ris_ = im_ = ostat_ = emux_ = ustat_ = tssel_ = spc_ = sac_ = ctl_ = cc_ = 0;
        sspri_ = 0x3210;
        for (Seq& s : ss_) { s.mux = s.ctl = s.emux = s.tsh = s.op = s.dc = 0; s.fifo.clear(); }
        pend_ = 0;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    double f_adc() const {
        const double f = f_reloj ? f_reloj(cc_ & 0xFu) : 0.0;
        if ((cc_ & 0xFu) == 0) return f / double(((cc_ >> 4) & 0x3Fu) + 1u);
        return f;
    }
    double t_muestra(unsigned s, unsigned paso) const {
        static const double tsh[16] = {4, 4, 8, 8, 16, 16, 32, 32, 64, 64, 128, 128, 256, 256, 256, 256};
        const double f = f_adc();
        return f > 0 ? (tsh[(ss_[s].tsh >> (4 * paso)) & 0xFu] + 16.0) / f : 1e-6;
    }
    uint16_t convierte(unsigned s, unsigned paso) {
        const uint32_t c = (ss_[s].ctl >> (4 * paso)) & 0xFu;
        const unsigned canal = ((ss_[s].mux >> (4 * paso)) & 0xFu) + (((ss_[s].emux >> (4 * paso)) & 1u) ? 16u : 0u);
        const double vref = v_ref ? v_ref() : 3.3;
        auto lee = [&](unsigned ch) -> double {
            if (ch >= 20 || !v_canal) return 0.0;
            if (es_analogico && !es_analogico(ch)) {
                ++n_amsel_;
                if (!avisado_amsel_) {
                    avisado_amsel_ = true;
                    char m[160];
                    std::snprintf(m, sizeof m, "ADC: AIN%u se convierte con su pin fuera del modo "
                                  "analogico (GPIOAMSEL, y AFSEL sin DEN): lee 0", ch);
                    SC_REPORT_WARNING("msp432e4/adc", m);
                }
                return 0.0;
            }
            return v_canal(ch);
        };
        double v;
        if (c & 8u) v = (147.5 - temperatura) / 75.0;                 // TS
        else if (c & 1u) {                                            // diferencial: el par
            const unsigned par = (canal / 2u) * 2u;
            v = (lee(par) - lee(par + 1u)) / 2.0 + vref / 2.0;
        } else v = lee(canal);
        double k = std::floor(v / vref * 4096.0);
        if (k < 0) k = 0;
        if (k > 4095) k = 4095;
        return uint16_t(k);
    }

    void motor() {
        for (;;) {
            // El de más prioridad (SSPRI, menor valor) que esté pendiente y activo
            for (unsigned s = 0; s < 4; ++s)
                if (((emux_ >> (4 * s)) & 0xFu) == 0xFu && (actss_ & (1u << s))) pend_ |= 1u << s;
            int elegido = -1;
            unsigned mejor = 99;
            for (unsigned s = 0; s < 4; ++s)
                if ((pend_ & (1u << s)) && (actss_ & (1u << s))) {
                    const unsigned p = (sspri_ >> (4 * s)) & 3u;
                    if (p < mejor) { mejor = p; elegido = int(s); }
                }
            if (elegido < 0) { ocupado_ = false; wait(ev_); continue; }
            const unsigned s = unsigned(elegido);
            pend_ &= ~(1u << s);
            ocupado_ = true;
            for (unsigned paso = 0; paso < PROF[s]; ++paso) {
                wait(sc_core::sc_time(t_muestra(s, paso), sc_core::SC_SEC));
                const uint16_t r = convierte(s, paso);
                ++n_;
                if (ss_[s].fifo.size() < PROF[s]) ss_[s].fifo.push_back(r);
                else ostat_ |= 1u << s;
                const uint32_t c = (ss_[s].ctl >> (4 * paso)) & 0xFu;
                if (c & 4u) { ris_ |= 1u << s; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
                if (c & 2u) break;
            }
            ocupado_ = false;
        }
    }

    uint32_t reg_read(uint32_t off) override {
        if (off >= R_SS0 && off < R_SS0 + 0x80) {
            const unsigned s = (off - R_SS0) / 0x20u, r = (off - R_SS0) % 0x20u;
            Seq& q = ss_[s];
            switch (r) {
                case 0x00: return q.mux;
                case 0x04: return q.ctl;
                case 0x08: {
                    if (q.fifo.empty()) { ustat_ |= 1u << s; return 0; }
                    const uint16_t v = q.fifo.front();
                    q.fifo.pop_front();
                    return v;
                }
                case 0x0C: {
                    const unsigned n = unsigned(q.fifo.size());
                    return (n >= PROF[s] ? (1u << 12) : 0u) | (n == 0 ? (1u << 8) : 0u) | ((n & 0xFu) << 4);
                }
                case 0x10: return q.op;
                case 0x14: return q.dc;
                case 0x18: return q.emux;
                case 0x1C: return q.tsh;
                default: return 0;
            }
        }
        switch (off) {
            case R_ACTSS: return actss_ | (ocupado_ ? (1u << 16) : 0u);
            case R_RIS: return ris_;
            case R_IM: return im_;
            case R_ISC: return ris_ & im_;
            case R_OSTAT: return ostat_;
            case R_EMUX: return emux_;
            case R_USTAT: return ustat_;
            case R_TSSEL: return tssel_;
            case R_SSPRI: return sspri_;
            case R_SPC: return spc_;
            case R_SAC: return sac_;
            case R_CTL: return ctl_;
            case R_PP: return 0x00B02087u;   // 2 Msps, 24 canales, TS, 12 bits [1F]
            case R_PC: return 0x7u;
            case R_CC: return cc_;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off >= R_SS0 && off < R_SS0 + 0x80) {
            const unsigned s = (off - R_SS0) / 0x20u, r = (off - R_SS0) % 0x20u;
            Seq& q = ss_[s];
            const uint32_t m = PROF[s] == 8 ? 0xFFFFFFFFu : PROF[s] == 4 ? 0xFFFFu : 0xFu;
            switch (r) {
                case 0x00: q.mux = v & m; break;
                case 0x04: q.ctl = v & m; break;
                case 0x10: q.op = v & m; break;
                case 0x14: q.dc = v & m; break;
                case 0x18: q.emux = v & (PROF[s] == 8 ? 0x11111111u : PROF[s] == 4 ? 0x1111u : 1u); break;
                case 0x1C: q.tsh = v & m; break;
                default: break;
            }
            return;
        }
        switch (off) {
            case R_ACTSS: actss_ = v & 0xFu; ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_IM: im_ = v & 0xF0F0Fu; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_ISC: ris_ &= ~(v & 0xFu); pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_OSTAT: ostat_ &= ~v; break;
            case R_EMUX: emux_ = v & 0xFFFFu; ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_USTAT: ustat_ &= ~v; break;
            case R_TSSEL: tssel_ = v; break;
            case R_SSPRI: sspri_ = v & 0x3333u; break;
            case R_SPC: spc_ = v & 0xFu; break;
            case R_PSSI: pend_ |= v & 0xFu; ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_SAC: sac_ = v & 7u; break;
            case R_CTL: ctl_ = v; break;
            case R_CC: cc_ = v & 0x3FFu; break;
            default: break;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_ADC_H
