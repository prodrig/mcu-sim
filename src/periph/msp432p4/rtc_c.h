// =============================================================================
// rtc_c.h — El reloj de tiempo real del MSP432P4, RTC_C (fase T3)
//
// El RTC_C del MSP430, solo en modo calendario [H: RTC_C_Type; TRM SLAU356
// cap. 20; segunda fuente: DriverLib rtc_c.c]:
//
//   CTL0     RTCKEY (0xA5 para escribir CTL0, CTL13, OCAL y TCMP; se lee 0x96;
//            cualquier otro valor cierra), y los IE/IFG de OF, TEV, A y RDY
//   CTL13    CALF, BCD, HOLD (a 1 tras el reset), MODE (siempre 1), RDY, TEV
//   PS0CTL, PS1CTL, PS   los dos preescaladores: RT0PS cuenta BCLK, RT1PS cuenta
//            los desbordes de RT0PS, y RT1PS desbordando es el segundo. RTxIP
//            elige cada cuánto levantan su IFG (/2 .. /256 y /2 .. /128)
//   TIM0, TIM1, DATE, YEAR   segundos y minutos, horas y día de la semana, día y
//            mes, año; en binario o en BCD según CTL13.BCD
//   AMINHR, ADOWDAY   la alarma: minuto, hora, día de la semana y del mes, cada
//            uno con su bit AE; salta cuando coinciden todos los que lo tienen
//   IV       OF, RDY, TEV, A, RT0PS, RT1PS, en ese orden [1F]
//   BIN2BCD, BCD2BIN   conversores: se escribe un número y se lee el otro
//
// EL TIEMPO: BCLK (LFXT o REFO, lo que diga el CS) a 32 768 Hz da un segundo;
// a otra frecuencia, el segundo dura lo que toque. El modelo no cuenta cada
// tic: despierta en cada segundo, y en los intervalos de los preescaladores
// solo si su IE está puesto; PS y los IFG de los preescaladores se calculan al
// leerlos. Al pasar el segundo: RDYIFG; al pasar el minuto, la hora, la
// medianoche o el mediodía, TEVIFG según RTCTEV; y la alarma.
//
// LO QUE NO ESTÁ: RTCRDY no baja nunca (en el chip baja un momento antes de
// cada actualización), la calibración (OCAL, TCMP, CALF y la salida RTCCLK)
// se guarda y no hace nada, y RTCOFIFG no se levanta. [P-16 v]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_RTC_C_H
#define STM32_PERIPH_MSP432P4_RTC_C_H

#include <cmath>
#include <functional>
#include "bus16.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class RtcCP4 : public Bus16P4 {
public:
    sc_core::sc_out<bool> irq{"irq"};
    std::function<double()> f_bclk;        // la da el SoC

    enum : uint16_t {
        RDYIFG = 0x01, AIFG = 0x02, TEVIFG = 0x04, OFIFG = 0x08,
        RDYIE = 0x10, AIE = 0x20, TEVIE = 0x40, OFIE = 0x80,
        BCD = 0x0080, HOLD = 0x0040, MODE = 0x0020, RDY = 0x0010,
        PSIFG = 0x01, PSIE = 0x02
    };

    explicit RtcCP4(sc_core::sc_module_name nm) : Bus16P4(nm, p4::RTC_C_B, 0x400) {
        SC_HAS_PROCESS(RtcCP4);
        SC_THREAD(cuenta_proc);
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }
    void recalcula() { al_dia(); ev_.notify(sc_core::SC_ZERO_TIME); }

    // --- Lo que mira el banco ----------------------------------------------------
    bool en_marcha() const { return !(ctl13_ & HOLD); }
    unsigned segundo() const { return seg_; }
    unsigned minuto() const { return min_; }
    unsigned hora() const { return hora_; }
    uint64_t segundos_contados() const { return n_seg_; }

protected:
    uint16_t ctl0_ = 0, ctl13_ = HOLD | MODE | RDY, ocal_ = 0, tcmp_ = 0x4000;
    uint16_t ps0ctl_ = 0, ps1ctl_ = 0;
    uint16_t amin_ = 0, ahora_ = 0, adow_ = 0, aday_ = 0;
    uint16_t bin2bcd_ = 0, bcd2bin_ = 0;
    bool abierto_ = false;
    // El calendario, en binario
    unsigned seg_ = 0, min_ = 0, hora_ = 0, dow_ = 0, dia_ = 1, mes_ = 1, anio_ = 0;
    // El tiempo: los tics de BCLK (1/32768 de segundo de calendario) contados
    // hasta `t_ref_`, y desde ahí corriendo a `f_` si no está parado
    double tics_ = 0.0, f_ = 32768.0;
    sc_core::sc_time t_ref_;
    uint64_t ps_ifg_visto_[2] = {0, 0};          // el último intervalo ya marcado
    uint64_t n_seg_ = 0;
    sc_core::sc_event ev_, irq_ev_;

    void valores_de_reset() {
        ctl0_ = 0; ctl13_ = HOLD | MODE | RDY; ocal_ = 0; tcmp_ = 0x4000;
        ps0ctl_ = ps1ctl_ = 0; amin_ = ahora_ = adow_ = aday_ = 0;
        abierto_ = false;
        seg_ = min_ = hora_ = dow_ = 0; dia_ = 1; mes_ = 1; anio_ = 0;
        tics_ = 0.0; t_ref_ = sc_core::sc_time_stamp();
        ps_ifg_visto_[0] = ps_ifg_visto_[1] = 0;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // El NVIC del modelo latchea por flanco: si al leer IV queda otro flag
    // pendiente, la línea baja un delta y vuelve a subir (como el DIO y la eUSCI)
    bool repulso_ = false;
    void irq_proc() {
        const bool c0 = (ctl0_ & (ctl0_ >> 4) & 0x0Fu) != 0;
        const bool p0 = (ps0ctl_ & PSIFG) && (ps0ctl_ & PSIE);
        const bool p1 = (ps1ctl_ & PSIFG) && (ps1ctl_ & PSIE);
        const bool q = c0 || p0 || p1;
        if (repulso_ && q) { repulso_ = false; irq.write(false); irq_ev_.notify(sc_core::SC_ZERO_TIME); return; }
        repulso_ = false;
        irq.write(q);
    }
    bool corre() const { return !(ctl13_ & HOLD) && f_ > 0.0; }

    // Lleva los tics hasta ahora, y lo que haya pasado mientras: los
    // segundos los avanza `cuenta_proc` justo en su instante; aquí solo los
    // preescaladores, que se miran al leer
    void al_dia() {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        if (corre()) tics_ += (ahora - t_ref_).to_seconds() * f_;
        t_ref_ = ahora;
        f_ = f_bclk ? f_bclk() : 32768.0;
        marca_ps();
    }
    uint64_t tics_enteros() const { return uint64_t(std::floor(tics_ + 1e-6)); }
    // Los intervalos de los preescaladores: RT0 cada 2^(IP+1) tics de BCLK, RT1
    // cada 2^(IP+1) desbordes de RT0 (256 tics)
    uint64_t periodo_ps(unsigned k) const {
        const unsigned ip = ((k ? ps1ctl_ : ps0ctl_) >> 2) & 7u;
        return (k ? 256ull : 1ull) << (ip + 1);
    }
    void marca_ps() {
        const uint64_t t = tics_enteros();
        for (unsigned k = 0; k < 2; ++k) {
            const uint64_t n = t / periodo_ps(k);
            if (n > ps_ifg_visto_[k]) {
                ps_ifg_visto_[k] = n;
                (k ? ps1ctl_ : ps0ctl_) |= PSIFG;
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
            }
        }
    }
    // Lo siguiente que hay que hacer, en tics: el próximo segundo, y el próximo
    // intervalo de un preescalador con IE
    double proximo() const {
        const uint64_t t = tics_enteros();
        double p = double((t / 32768u + 1u) * 32768u);
        for (unsigned k = 0; k < 2; ++k)
            if ((k ? ps1ctl_ : ps0ctl_) & PSIE) {
                const uint64_t per = periodo_ps(k);
                p = std::min(p, double((t / per + 1u) * per));
            }
        return p;
    }
    void cuenta_proc() {
        for (;;) {
            al_dia();
            if (!corre()) { wait(ev_); continue; }
            const double falta = proximo() - tics_;
            const sc_core::sc_time dt(std::max(falta, 0.0) / f_, sc_core::SC_SEC);
            const uint64_t seg_antes = tics_enteros() / 32768u;
            wait(dt + sc_core::sc_time(1, sc_core::SC_PS), ev_);
            al_dia();
            const uint64_t seg_ahora = tics_enteros() / 32768u;
            for (uint64_t s = seg_antes; s < seg_ahora; ++s) un_segundo();
        }
    }

    static unsigned dias_mes(unsigned m, unsigned a) {
        static const unsigned d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m == 2 && ((a % 4 == 0 && a % 100 != 0) || a % 400 == 0)) return 29;
        return (m >= 1 && m <= 12) ? d[m - 1] : 31;
    }
    void un_segundo() {
        ++n_seg_;
        ctl0_ |= RDYIFG;
        bool minuto = false, hora = false;
        if (++seg_ >= 60) {
            seg_ = 0; minuto = true;
            if (++min_ >= 60) {
                min_ = 0; hora = true;
                if (++hora_ >= 24) {
                    hora_ = 0;
                    dow_ = (dow_ + 1) % 7;
                    if (++dia_ > dias_mes(mes_, anio_)) {
                        dia_ = 1;
                        if (++mes_ > 12) { mes_ = 1; ++anio_; }
                    }
                }
            }
        }
        // El evento de RTCTEV
        if (minuto) {
            bool ev = false;
            switch (ctl13_ & 3u) {
                case 0: ev = true; break;                         // cada minuto
                case 1: ev = hora; break;                         // cada hora
                case 2: ev = hora && hora_ == 0; break;           // medianoche
                case 3: ev = hora && hora_ == 12; break;          // mediodía
            }
            if (ev) ctl0_ |= TEVIFG;
            // La alarma: coinciden todos los campos con AE
            const bool alguno = (amin_ & 0x80) || (ahora_ & 0x80) || (adow_ & 0x80) || (aday_ & 0x80);
            auto ok = [this](uint16_t a, unsigned v) {
                return !(a & 0x80) || de_reg(uint16_t(a & 0x7F)) == v;
            };
            if (alguno && ok(amin_, min_) && ok(ahora_, hora_) && ok(adow_, dow_) && ok(aday_, dia_))
                ctl0_ |= AIFG;
        }
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Binario o BCD ------------------------------------------------------------
    bool bcd() const { return (ctl13_ & BCD) != 0; }
    static uint16_t a_bcd(unsigned v) {
        return uint16_t(((v / 1000) % 10) << 12 | ((v / 100) % 10) << 8 | ((v / 10) % 10) << 4 | (v % 10));
    }
    static unsigned de_bcd(uint16_t v) {
        return ((v >> 12) & 0xF) * 1000 + ((v >> 8) & 0xF) * 100 + ((v >> 4) & 0xF) * 10 + (v & 0xF);
    }
    uint16_t a_reg(unsigned v) const { return bcd() ? a_bcd(v) : uint16_t(v); }
    unsigned de_reg(uint16_t v) const { return bcd() ? de_bcd(v) : v; }

    uint16_t lee16(uint32_t off, bool dbg) override {
        if (!dbg) al_dia();
        switch (off) {
            case 0x00: return uint16_t((abierto_ ? 0xA500u : 0x9600u) | (ctl0_ & 0xFFu));
            case 0x02: return ctl13_;
            case 0x04: return ocal_;
            case 0x06: return tcmp_;
            case 0x08: return ps0ctl_;
            case 0x0A: return ps1ctl_;
            case 0x0C: {                                          // PS
                const uint64_t t = tics_enteros();
                return uint16_t((t & 0xFFu) | (((t >> 8) & 0x7Fu) << 8));
            }
            case 0x0E: return dbg ? 0 : iv();
            case 0x10: return uint16_t(a_reg(seg_) | (a_reg(min_) << 8));
            case 0x12: return uint16_t(a_reg(hora_) | (dow_ << 8));
            case 0x14: return uint16_t(a_reg(dia_) | (a_reg(mes_) << 8));
            case 0x16: return a_reg(anio_);
            case 0x18: return uint16_t(amin_ | (ahora_ << 8));
            case 0x1A: return uint16_t(adow_ | (aday_ << 8));
            case 0x1C: return bin2bcd_;
            case 0x1E: return bcd2bin_;
            default:   return 0;
        }
    }
    uint16_t iv() {
        struct { uint16_t* r; uint16_t f, e, v; } t[6] = {
            {&ctl0_, OFIFG, OFIE, 0x02}, {&ctl0_, RDYIFG, RDYIE, 0x04},
            {&ctl0_, TEVIFG, TEVIE, 0x06}, {&ctl0_, AIFG, AIE, 0x08},
            {&ps0ctl_, PSIFG, PSIE, 0x0A}, {&ps1ctl_, PSIFG, PSIE, 0x0C}};
        for (auto& x : t)
            if ((*x.r & x.f) && (*x.r & x.e)) {
                *x.r &= uint16_t(~x.f);
                repulso_ = true;
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return x.v;
            }
        return 0;
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        al_dia();
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        switch (off) {
            case 0x00: {
                if (m & 0xFF00u) abierto_ = ((v >> 8) & 0xFFu) == 0xA5u;
                if ((m & 0x00FFu) && abierto_) ctl0_ = uint16_t(mz(ctl0_) & 0xFFu);
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x02:
                if (!abierto_) return;
                ctl13_ = uint16_t((mz(ctl13_) & 0x03CFu) | MODE | RDY);
                ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            case 0x04: if (abierto_) ocal_ = mz(ocal_); return;
            case 0x06: if (abierto_) tcmp_ = uint16_t((mz(tcmp_) & 0x83FFu) | 0x6000u); return;
            case 0x08: ps0ctl_ = uint16_t(mz(ps0ctl_) & 0x1Fu);
                       irq_ev_.notify(sc_core::SC_ZERO_TIME); ev_.notify(sc_core::SC_ZERO_TIME); return;
            case 0x0A: ps1ctl_ = uint16_t(mz(ps1ctl_) & 0x1Fu);
                       irq_ev_.notify(sc_core::SC_ZERO_TIME); ev_.notify(sc_core::SC_ZERO_TIME); return;
            case 0x0C: {                                          // PS: fijar los contadores
                const uint64_t t = tics_enteros();
                const uint16_t cur = uint16_t((t & 0xFFu) | (((t >> 8) & 0x7Fu) << 8));
                const uint16_t w = mz(cur);
                const uint64_t base = t - (t % 32768u);
                tics_ = double(base + (w & 0xFFu) + (uint64_t((w >> 8) & 0x7Fu) << 8));
                ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x10: {
                if (m & 0x00FFu) seg_ = de_reg(uint16_t(v & 0x7Fu));
                if (m & 0xFF00u) min_ = de_reg(uint16_t((v >> 8) & 0x7Fu));
                return;
            }
            case 0x12: {
                if (m & 0x00FFu) hora_ = de_reg(uint16_t(v & 0x3Fu));
                if (m & 0xFF00u) dow_ = (v >> 8) & 7u;
                return;
            }
            case 0x14: {
                if (m & 0x00FFu) dia_ = de_reg(uint16_t(v & 0x3Fu));
                if (m & 0xFF00u) mes_ = de_reg(uint16_t((v >> 8) & 0x1Fu));
                return;
            }
            case 0x16: anio_ = de_reg(mz(a_reg(anio_))); return;
            case 0x18: {
                const uint16_t w = mz(uint16_t(amin_ | (ahora_ << 8)));
                amin_ = uint16_t(w & 0xFFu); ahora_ = uint16_t(w >> 8);
                return;
            }
            case 0x1A: {
                const uint16_t w = mz(uint16_t(adow_ | (aday_ << 8)));
                adow_ = uint16_t(w & 0xFFu); aday_ = uint16_t(w >> 8);
                return;
            }
            case 0x1C: { const uint16_t b = mz(0); bin2bcd_ = a_bcd(b & 0xFFFu); return; }
            case 0x1E: { const uint16_t b = mz(0); bcd2bin_ = uint16_t(de_bcd(b)); return; }
            default: return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_RTC_C_H
