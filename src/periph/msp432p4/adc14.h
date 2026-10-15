// =============================================================================
// adc14.h — El ADC14 del MSP432P4 (fase T3)
//
// El SAR de 14 bits con 32 memorias de conversión [H: ADC14_Type; TRM
// SLAU356, cap. 22; segunda fuente: DriverLib adc14.c y TI-Drivers
// ADCMSP432/ADCBufMSP432]:
//
//   CTL0   SC, ENC, ON, MSC, SHT0/SHT1 (4..192 ciclos de muestreo), BUSY,
//          CONSEQ (uno, secuencia, uno repetido, secuencia repetida), SSEL
//          (MODCLK, SYSCLK, ACLK, MCLK, SMCLK, HSMCLK), DIV, ISSH, SHP, SHS,
//          PDIV
//   CTL1   PWRMD, REFBURST, DF (binario o complemento a 2 alineado a la
//          izquierda), RES (8, 10, 12, 14 bits: 9, 11, 14, 16 ciclos de
//          conversión), CSTARTADD, BATMAP, TCMAP, CH0..3MAP
//   MCTLn  INCH, EOS, VRSEL (AVCC, VREF, VeREF+), DIF, WINC, WINCTH
//   MEMn   el resultado; LEERLO baja IFGn
//   LO/HI  los dos pares del comparador de ventana
//   IER/IFGR/CLRIFGR, IV   IFG0..31 y, en IFGR1, IN, LO, HI, OV, TOV, RDY
//
// LOS CANALES: A0-A23 en los pines (A0 = P5.5 ... A23 = P8.2, función
// terciaria, la tabla de ADCMSP432.h) y, en vez de A22 y A23, el sensor de
// temperatura (TCMAP) y AVCC/2 (BATMAP): los canales internos son MAX-1 y
// MAX, con MAX = 23 en el P401R. Los CH0..3MAP se guardan y avisan.
//
// LOS DISPAROS (SHS): 0 el bit SC, 1 TA0.1, 2 TA0.2, 3 TA1.1, 4 TA1.2,
// 5 TA2.1, 6 TA2.2, 7 TA3.1 -la salida OUTn de cada temporizador, aunque no
// vaya a ningún pin-. Es la tabla adcTriggerTable de ADCBufMSP432.c [2F] y la
// del datasheet [1F]. Con SHP = 1 el flanco de subida (de bajada con ISSH)
// arranca el muestreo de SHT ciclos; con SHP = 0 se muestrea MIENTRAS la señal
// está alta (el bit SC, que borra el firmware, o la salida del temporizador).
//
// EL RESULTADO es el de un convertidor ideal: N = 2^n·(Vin - VR-)/(VR+ - VR-),
// truncado y recortado a [0, 2^n - 1]; en diferencial, 2^(n-1)·(1 + ΔV/VR).
// La tensión se lee del nodo al acabar el muestreo. Sin ruido, sin error de
// ganancia ni de offset: la calibración del TLV es 1,0 y 0. [P-16 s]
//
// LO QUE NO ESTÁ: el muestreo no carga el nodo (la resistencia y el
// condensador de muestreo de entrada no se modelan), PWRMD y REFBURST no
// cambian nada, RDYIFG no se levanta nunca, y los CHxMAP no llevan a ningún
// canal interno. [P-16 s]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_ADC14_H
#define STM32_PERIPH_MSP432P4_ADC14_H

#include <cmath>
#include <functional>
#include "../../common/periph_base.h"
#include "ref_a.h"

namespace stm32 {

class Adc14P4 : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_signal<bool> dma_req{"dma_req"};   // a la uDMA: hay un IFG puesto

    // Lo que da el SoC
    std::function<double(unsigned)> v_canal;       // A0-A23: la tensión del pin (o NaN)
    std::function<double()> v_avcc, v_avss;
    std::function<double()> v_eref_mas, v_eref_menos;   // VeREF+ (P5.6), VeREF- (P5.7)
    std::function<double(unsigned)> f_reloj;       // SSEL -> Hz
    std::function<bool(unsigned)> es_analogico;    // ¿el pin del canal está en función analógica?
    RefAP4* ref = nullptr;
    double temperatura_c = 25.0;                   // la del chip; la pone el banco

    static constexpr unsigned PIDE_REF = 1u;       // el bit de REF_A de este módulo

    // CTL0 [H]
    enum : uint32_t {
        SC = 1u << 0, ENC = 1u << 1, ON = 1u << 4, MSC = 1u << 7, BUSY = 1u << 16,
        ISSH = 1u << 25, SHP = 1u << 26
    };
    // CTL1
    enum : uint32_t { DF = 1u << 3, BATMAP = 1u << 22, TCMAP = 1u << 23 };
    // MCTL
    enum : uint32_t { EOS = 1u << 7, DIF = 1u << 13, WINC = 1u << 14, WINCTH = 1u << 15 };
    // IFGR1
    enum : uint32_t { I_IN = 1u << 1, I_LO = 1u << 2, I_HI = 1u << 3, I_OV = 1u << 4,
                      I_TOV = 1u << 5, I_RDY = 1u << 6 };

    explicit Adc14P4(sc_core::sc_module_name nm) : BusSlave(nm, p4::ADC14_B, 0x400) {
        SC_HAS_PROCESS(Adc14P4);
        SC_THREAD(conv_proc);
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }

    // Un disparo de temporizador ha cambiado (lo llama el SoC: `n` = SHS 1..7)
    void disparo(unsigned n, bool v) {
        if (n == 0 || n > 7) return;
        // Un flanco que llega con una conversión en marcha: se pierde (TOV)
        if (n == shs() && v && !shi_[n] && convirtiendo_ && vivo() && !(ctl0_ & MSC))
            { ifg1_ |= I_TOV; cambia_irq(); }
        shi_[n] = v;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Lo que mira el banco ---------------------------------------------------
    uint64_t conversiones() const { return n_conv_; }
    uint16_t mem(unsigned n) const { return mem_[n & 31u]; }
    unsigned avisos() const { return n_avisos_; }
    // La tensión del sensor de temperatura a `t` grados
    static double v_sensor(double t) { return p4::v_sensor_temp(t); }

protected:
    uint32_t ctl0_ = 0, ctl1_ = 0x30, lo_[2] = {0, 0}, hi_[2] = {0x3FFF, 0x3FFF};
    uint32_t mctl_[32] = {}, mem_[32] = {};
    uint32_t ier0_ = 0, ier1_ = 0, ifg0_ = 0, ifg1_ = 0;
    bool shi_[8] = {};
    bool convirtiendo_ = false;
    uint64_t n_conv_ = 0;
    unsigned n_avisos_ = 0;
    bool av_chmap_ = false, av_canal_ = false, av_ref_ = false, av_tcoff_ = false,
         av_clk_ = false;
    sc_core::sc_event ev_, irq_ev_;

    void valores_de_reset() {
        ctl0_ = 0; ctl1_ = 0x30;
        lo_[0] = lo_[1] = 0; hi_[0] = hi_[1] = 0x3FFF;
        for (uint32_t& m : mctl_) m = 0;
        for (uint32_t& m : mem_) m = 0;
        ier0_ = ier1_ = ifg0_ = ifg1_ = 0;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        if (ref) { ref->solicita(PIDE_REF, false); ref->ocupa(PIDE_REF, false); }
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool repulso_ = false;           // véase RtcCP4::irq_proc
    void irq_proc() {
        const bool q = (ifg0_ & ier0_) || (ifg1_ & ier1_ & 0x7Eu);
        if (repulso_ && q) { repulso_ = false; irq.write(false); irq_ev_.notify(sc_core::SC_ZERO_TIME); }
        else { repulso_ = false; irq.write(q); }
        dma_req.write(ifg0_ != 0);
    }
    void cambia_irq() { irq_ev_.notify(sc_core::SC_ZERO_TIME); }
    void aviso(bool& ya, const char* m) {
        if (ya) return;
        ya = true; ++n_avisos_;
        SC_REPORT_WARNING("msp432p4/adc14", m);
    }

    // --- Campos -------------------------------------------------------------------
    bool on()  const { return (ctl0_ & ON) != 0; }
    bool enc() const { return (ctl0_ & ENC) != 0; }
    unsigned conseq() const { return (ctl0_ >> 17) & 3u; }
    unsigned shs() const { return (ctl0_ >> 27) & 7u; }
    unsigned res_bits() const { static const unsigned b[4] = {8, 10, 12, 14};
                                return b[(ctl1_ >> 4) & 3u]; }
    unsigned ciclos_conv() const { static const unsigned c[4] = {9, 11, 14, 16};
                                   return c[(ctl1_ >> 4) & 3u]; }
    unsigned cstart() const { return (ctl1_ >> 16) & 31u; }
    unsigned ciclos_muestreo(unsigned n) const {
        static const unsigned s[8] = {4, 8, 16, 32, 64, 96, 128, 192};
        const bool sht1 = (n >= 8 && n <= 23);
        return s[(ctl0_ >> (sht1 ? 12 : 8)) & 7u];
    }
    double f_adcclk() const {
        const unsigned ssel = (ctl0_ >> 19) & 7u;
        double f = f_reloj ? f_reloj(ssel) : 0.0;
        static const unsigned pd[4] = {1, 4, 32, 64};
        f /= double(pd[(ctl0_ >> 30) & 3u]);
        f /= double(((ctl0_ >> 22) & 7u) + 1u);
        return f;
    }
    // ¿Usa la secuencia la referencia interna (VRSEL = 1 o el sensor)?
    bool usa_ref() const {
        unsigned n = cstart();
        for (unsigned k = 0; k < 32; ++k) {
            const uint32_t m = mctl_[n];
            if (((m >> 8) & 0xFu) == 1u) return true;
            if ((ctl1_ & TCMAP) && (m & 0x1Fu) == 22u) return true;
            if (conseq() == 0 || conseq() == 2 || (m & EOS)) break;
            n = (n + 1u) & 31u;
        }
        return false;
    }
    void pide_ref() {
        if (ref) ref->solicita(PIDE_REF, on() && enc() && usa_ref());
    }
    // El disparo, ya invertido por ISSH
    bool shi() const {
        const bool v = shs() == 0 ? (ctl0_ & SC) != 0 : shi_[shs()];
        return (ctl0_ & ISSH) ? !v : v;
    }

    // --- La tensión de un canal ------------------------------------------------------
    double tension(unsigned ch) {
        if (ch == 22 && (ctl1_ & TCMAP)) {
            if (!ref || !ref->sensor_on()) {
                aviso(av_tcoff_, "el sensor de temperatura (TCMAP) esta apagado: hace "
                      "falta la referencia encendida y REFTCOFF = 0; se lee 0 V");
                return 0.0;
            }
            return v_sensor(temperatura_c);
        }
        if (ch == 23 && (ctl1_ & BATMAP))
            return 0.5 * ((v_avcc ? v_avcc() : 3.3) - (v_avss ? v_avss() : 0.0));
        if (ch >= 18 && ch <= 21 && (ctl1_ & (1u << (24 + (21 - ch)))))
            aviso(av_chmap_, "ADC14CHxMAP: los canales internos 0-3 no se modelan; "
                  "se lee el pin");
        if (ch > 23) return 0.0;
        if (es_analogico && !es_analogico(ch))
            aviso(av_canal_, "el ADC14 convierte un canal cuyo pin no esta en funcion "
                  "analogica (PxSEL0 = PxSEL1 = 1): en el chip el buffer digital "
                  "sigue conectado; aqui se lee la tension igual");
        const double v = v_canal ? v_canal(ch) : 0.0;
        return std::isnan(v) ? 0.0 : v;
    }
    void referencias(uint32_t m, double& vp, double& vm) {
        const double avss = v_avss ? v_avss() : 0.0;
        switch ((m >> 8) & 0xFu) {
            case 1:
                if (!ref || !ref->lista())
                    aviso(av_ref_, "VRSEL = 1 con la referencia sin asentar (REFGENRDY = 0): "
                          "el resultado no vale");
                vp = ref ? ref->vref() : 0.0; vm = avss; return;
            case 14: case 15:
                vp = v_eref_mas ? v_eref_mas() : 0.0;
                vm = v_eref_menos ? v_eref_menos() : 0.0; return;
            default:
                vp = v_avcc ? v_avcc() : 3.3; vm = avss; return;
        }
    }
    // El código, ya en el formato de MEMn
    uint32_t convierte(unsigned n) {
        const uint32_t m = mctl_[n];
        const unsigned bits = res_bits();
        const double full = double(1u << bits);
        double vp = 0, vm = 0;
        referencias(m, vp, vm);
        const double span = vp - vm;
        const unsigned ch = m & 0x1Fu;
        long code;
        if (m & DIF) {
            const unsigned par = ch & ~1u;
            const double dv = tension(par) - tension(par + 1u);
            code = span > 0 ? long(std::floor(full / 2.0 * (1.0 + dv / span))) : 0;
        } else {
            const double v = tension(ch) - vm;
            code = span > 0 ? long(std::floor(full * v / span)) : long(full - 1);
        }
        if (code < 0) code = 0;
        if (code > long(full) - 1) code = long(full) - 1;
        if (ctl1_ & DF) {                       // con signo, alineado a la izquierda
            const long s = code - long(full / 2);
            return uint32_t(uint16_t(int16_t(s << (16 - bits))));
        }
        return uint32_t(code);
    }
    void guarda(unsigned n, uint32_t v) {
        if (ifg0_ & (1u << n)) ifg1_ |= I_OV;         // la anterior no se leyó
        mem_[n] = v;
        ifg0_ |= 1u << n;
        ++n_conv_;
        const uint32_t m = mctl_[n];
        if (m & WINC) {
            const unsigned w = (m & WINCTH) ? 1u : 0u;
            // En formato con signo los umbrales también lo son
            const bool df = (ctl1_ & DF) != 0;
            auto val = [df](uint32_t x) { return df ? long(int16_t(uint16_t(x))) : long(x & 0xFFFFu); };
            const long r = val(v), l = val(lo_[w]), h = val(hi_[w]);
            if (r > h) ifg1_ |= I_HI;
            else if (r < l) ifg1_ |= I_LO;
            else ifg1_ |= I_IN;
        }
        cambia_irq();
    }

    // --- La máquina: muestreo y conversión ---------------------------------------
    bool vivo() const { return on() && enc(); }
    void conv_proc() {
        for (;;) {
            // Esperar el disparo: flanco de subida de SHI (o SHI alto, la
            // primera vez con SC)
            while (!(vivo() && shi())) wait(ev_);
            unsigned n = (conseq() == 0 || conseq() == 2) ? cstart() : cstart();
            bool primero = true;
            for (;;) {
                if (!primero && !(ctl0_ & MSC && (ctl0_ & SHP))) {
                    // Sin MSC: cada conversión espera su disparo
                    if (shs() == 0) ctl0_ &= ~SC;
                    while (vivo() && shi()) wait(ev_);      // que baje
                    while (vivo() && !shi()) wait(ev_);     // y vuelva a subir
                    if (!vivo()) break;
                }
                primero = false;
                if (!muestrea_y_convierte(n)) break;
                const bool fin_seq = (conseq() == 0 || conseq() == 2) || (mctl_[n] & EOS);
                if (conseq() == 0) break;                    // una y ya
                if (fin_seq) {
                    if (conseq() == 1) break;                // secuencia, una vez
                    if (!enc()) break;                       // repetidas: ENC = 0 para
                    n = cstart();
                } else n = (n + 1u) & 31u;
                if (!on()) break;
            }
            if (shs() == 0) ctl0_ &= ~SC;
            // Que SHI baje antes de aceptar otro flanco
            while (vivo() && shi() && shs() != 0) wait(ev_);
            if (!vivo()) wait(ev_);
        }
    }
    bool muestrea_y_convierte(unsigned n) {
        const double f = f_adcclk();
        if (f <= 0.0) {
            aviso(av_clk_, "el ADC14 no tiene reloj (SSEL elige un reloj parado): no convierte");
            wait(ev_);
            return false;
        }
        const sc_core::sc_time ciclo(1e12 / f, sc_core::SC_PS);
        convirtiendo_ = true;
        if (ref && usa_ref()) ref->ocupa(PIDE_REF, true);
        if (ctl0_ & SHP) {
            wait(ciclo * double(ciclos_muestreo(n)));
        } else {
            // Muestreo extendido: mientras SHI esté alto
            while (vivo() && shi()) wait(ev_);
        }
        if (!on()) { convirtiendo_ = false; if (ref) ref->ocupa(PIDE_REF, false); return false; }
        const uint32_t v = convierte(n);        // la tensión, al acabar el muestreo
        if ((ctl0_ & SHP) && shs() == 0) ctl0_ &= ~SC;   // SC se borra solo con SHP = 1
        wait(ciclo * double(ciclos_conv()));
        convirtiendo_ = false;
        if (ref) ref->ocupa(PIDE_REF, false);
        if (!on()) return false;
        guarda(n, v);
        return true;
    }

    // --- Los registros ----------------------------------------------------------------
    bool dbg_ = false;
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) override {
        dbg_ = true;
        const unsigned r = BusSlave::transport_dbg(gp);
        dbg_ = false;
        return r;
    }
    uint32_t reg_read(uint32_t off) override {
        if (off == 0x00) return ctl0_ | (convirtiendo_ ? BUSY : 0u);
        if (off == 0x04) return ctl1_;
        if (off >= 0x08 && off < 0x18) {
            const unsigned k = (off - 0x08) / 8u;
            return ((off - 0x08) & 4u) ? hi_[k] : lo_[k];
        }
        if (off >= 0x18 && off < 0x98) return mctl_[(off - 0x18) / 4u];
        if (off >= 0x98 && off < 0x118) {
            const unsigned n = (off - 0x98) / 4u;
            if (!dbg_) { ifg0_ &= ~(1u << n); cambia_irq(); }
            return mem_[n];
        }
        switch (off) {
            case 0x13C: return ier0_;
            case 0x140: return ier1_;
            case 0x144: return ifg0_;
            case 0x148: return ifg1_;
            case 0x154: return iv();
            default: return 0;
        }
    }
    // OV, TOV, HI, LO, IN, IFG0..31, RDY [1F: TRM, ADC14IV]
    uint32_t iv() {
        static const uint32_t r1[5] = {I_OV, I_TOV, I_HI, I_LO, I_IN};
        for (unsigned k = 0; k < 5; ++k)
            if (ifg1_ & ier1_ & r1[k]) {
                if (!dbg_) { ifg1_ &= ~r1[k]; repulso_ = true; cambia_irq(); }
                return 2u * (k + 1u);
            }
        for (unsigned n = 0; n < 32; ++n)
            if (ifg0_ & ier0_ & (1u << n)) {
                if (!dbg_) { ifg0_ &= ~(1u << n); repulso_ = true; cambia_irq(); }
                return 0x0Cu + 2u * n;
            }
        if (ifg1_ & ier1_ & I_RDY) {
            if (!dbg_) { ifg1_ &= ~I_RDY; repulso_ = true; cambia_irq(); }
            return 0x4Cu;
        }
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        auto mz = [&](uint32_t cur) { return (cur & ~m) | (v & m); };
        if (off == 0x00) {
            uint32_t w = mz(ctl0_) & ~BUSY;
            // Con ENC = 1 solo cambian ENC y SC (y ON) [TRM 22.3.1]
            if (enc()) w = (ctl0_ & ~(ENC | SC | ON)) | (w & (ENC | SC | ON));
            // SC con una conversión en marcha, y sin MSC: desbordamiento de tiempo
            if (convirtiendo_ && (w & SC) && !(ctl0_ & SC) && shs() == 0) ifg1_ |= I_TOV;
            ctl0_ = w;
            pide_ref();
            cambia_irq();
            ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        if (off == 0x04) {
            if (enc()) return;
            ctl1_ = mz(ctl1_) & 0x0FDF003Fu;
            pide_ref();
            return;
        }
        if (off >= 0x08 && off < 0x18) {
            const unsigned k = (off - 0x08) / 8u;
            uint32_t& r = ((off - 0x08) & 4u) ? hi_[k] : lo_[k];
            r = mz(r) & 0xFFFFu;
            return;
        }
        if (off >= 0x18 && off < 0x98) {
            if (enc()) return;
            uint32_t& r = mctl_[(off - 0x18) / 4u];
            r = mz(r) & 0x0000EF9Fu;
            pide_ref();
            return;
        }
        if (off >= 0x98 && off < 0x118) return;          // MEMn: solo lectura
        switch (off) {
            case 0x13C: ier0_ = mz(ier0_); cambia_irq(); return;
            case 0x140: ier1_ = mz(ier1_) & 0x7Eu; cambia_irq(); return;
            case 0x14C: ifg0_ &= ~(v & m); cambia_irq(); return;
            case 0x150: ifg1_ &= ~(v & m & 0x7Eu); cambia_irq(); return;
            default: return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_ADC14_H
