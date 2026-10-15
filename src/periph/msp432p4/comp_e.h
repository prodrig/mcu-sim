// =============================================================================
// comp_e.h — Los comparadores del MSP432P4, COMP_E0 y COMP_E1 (fase T3)
//
// El Comp_E del MSP430 [H: COMP_E_Type; TRM SLAU356 cap. 23; segunda fuente:
// DriverLib comp_e.c, que dice cómo se usan REF0 y REF1]:
//
//   CTL0   IPSEL/IPEN y IMSEL/IMEN: qué canal va a cada entrada
//   CTL1   OUT, OUTPOL, F (filtro) con FDLY, IES, SHORT, EX, PWRMD, ON, MRVL/MRVS
//   CTL2   REF0, REF1 (las tomas de la escalera, 1..32 de 32), RSEL (a qué
//          entrada va la referencia), RS (sin referencia, VCC por la escalera,
//          la compartida por la escalera, o la compartida directa), REFL
//          (1,2, 2,0 o 2,5 V), REFACC
//   CTL3   los PD: buffers digitales apagados (se guardan)
//   INT    IFG, IIFG, RDYIFG y sus IE; IV: 2 IFG, 4 IIFG, 0x0A RDY [1F]
//
// LA HISTÉRESIS es la de DriverLib (COMP_E_setReferenceVoltage): REF1 es el
// umbral de subida, que se usa mientras la salida está a 0, y REF0 el de
// bajada, mientras está a 1; con MRVS, MRVL elige a mano. La toma es
// Vbase·(REFx + 1)/32.
//
// LAS ENTRADAS son los canales 0-7, en sus pines [1F: datasheet, tablas de
// pines]: C0.0-C0.7 en P8.1, P8.0, P7.7-P7.4, P10.5, P10.4; y C1.0-C1.7 en
// P6.7-P6.2, P5.7, P5.6. La salida CxOUT sale por el PMAP (el mismo valor que
// TAxCLK, con PxDIR = 1).
//
// LO QUE NO ESTÁ: los canales internos 8-15, SHORT, los modos de consumo
// (todos responden igual), y la salida hacia las capturas de los Timer_A.
// El retardo es el del filtro con F = 1 (450-3600 ns) o ninguno. [P-16 w]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_COMP_E_H
#define STM32_PERIPH_MSP432P4_COMP_E_H

#include <functional>
#include "bus16.h"
#include "ref_a.h"

namespace stm32 {

class CompEP4 : public Bus16P4 {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_signal<bool> salida{"salida"};       // CxOUT, para el pin

    std::function<double(unsigned)> v_canal;         // canales 0-7
    std::function<double()> v_cc;
    RefAP4* ref = nullptr;
    unsigned bit_ref = 2;                            // su bit de petición en REF_A

    enum : uint16_t {
        IPEN = 0x0080, IMEN = 0x8000,
        OUT = 0x0001, OUTPOL = 0x0002, F = 0x0004, IES = 0x0008, SHORT = 0x0010,
        EX = 0x0020, ON = 0x0400, MRVL = 0x0800, MRVS = 0x1000,
        RSEL = 0x0020,
        IFG = 0x0001, IIFG = 0x0002, RDYIFG = 0x0010, IE = 0x0100, IIE = 0x0200, RDYIE = 0x1000
    };

    CompEP4(sc_core::sc_module_name nm, uint32_t base) : Bus16P4(nm, base, 0x400) {
        SC_HAS_PROCESS(CompEP4);
        SC_METHOD(evalua); sensitive << ev_; dont_initialize();
        SC_METHOD(aplica); sensitive << aplica_ev_; dont_initialize();
        SC_METHOD(listo_proc); sensitive << listo_ev_; dont_initialize();
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    // El SoC lo llama cuando cambia la tensión de un canal o la referencia
    void recalcula() { ev_.notify(sc_core::SC_ZERO_TIME); }
    bool out() const { return (ctl1_ & OUT) != 0; }
    double umbral() const { return umbral_; }

protected:
    uint16_t ctl0_ = 0, ctl1_ = 0, ctl2_ = 0, ctl3_ = 0, int_ = 0;
    bool pendiente_ = false, nuevo_ = false;
    double umbral_ = 0.0;
    sc_core::sc_event ev_, aplica_ev_, listo_ev_, irq_ev_;

    void reset_proc() {
        if (rst_n.read()) return;
        ctl0_ = ctl1_ = ctl2_ = ctl3_ = int_ = 0;
        pide_ref();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        aplica_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool repulso_ = false;           // véase RtcCP4::irq_proc
    void irq_proc() {
        const bool q = (int_ & (int_ >> 8) & (IFG | IIFG | RDYIFG)) != 0;
        if (repulso_ && q) { repulso_ = false; irq.write(false); irq_ev_.notify(sc_core::SC_ZERO_TIME); }
        else { repulso_ = false; irq.write(q); }
        salida.write((ctl1_ & OUT) != 0);
    }
    unsigned rs() const { return (ctl2_ >> 6) & 3u; }
    void pide_ref() {
        if (ref) ref->solicita(bit_ref, (ctl1_ & ON) && rs() >= 2 && ((ctl2_ >> 13) & 3u));
    }
    double v_compartida() const {
        switch ((ctl2_ >> 13) & 3u) {
            case 1: return 1.2;
            case 2: return 2.0;
            case 3: return 2.5;
            default: return 0.0;
        }
    }
    // La referencia, con la toma que toca según la salida
    double v_cref() {
        const bool usa1 = (ctl1_ & MRVS) ? (ctl1_ & MRVL) != 0 : !(ctl1_ & OUT);
        const unsigned tap = usa1 ? ((ctl2_ >> 8) & 0x1Fu) : (ctl2_ & 0x1Fu);
        switch (rs()) {
            case 1: return (v_cc ? v_cc() : 3.3) * double(tap + 1u) / 32.0;
            case 2: return (ref && ref->lista() ? v_compartida() : 0.0) * double(tap + 1u) / 32.0;
            case 3: return ref && ref->lista() ? v_compartida() : 0.0;
            default: return 0.0;
        }
    }
    double canal(unsigned c) { return (c < 8 && v_canal) ? v_canal(c) : 0.0; }
    void evalua() {
        if (!(ctl1_ & ON)) return;
        const double cref = v_cref();
        umbral_ = cref;
        double vp = (ctl0_ & IPEN) ? canal(ctl0_ & 0xFu) : (!(ctl2_ & RSEL) ? cref : 0.0);
        double vm = (ctl0_ & IMEN) ? canal((ctl0_ >> 8) & 0xFu) : ((ctl2_ & RSEL) ? cref : 0.0);
        bool r = vp > vm;
        if (ctl1_ & EX) r = !(vm > vp);      // entradas cambiadas y la salida invertida
        if (ctl1_ & OUTPOL) r = !r;
        if (r == out() && !pendiente_) return;
        nuevo_ = r;
        pendiente_ = true;
        static const unsigned ns[4] = {450, 900, 1800, 3600};
        if (ctl1_ & F) aplica_ev_.notify(sc_core::sc_time(ns[(ctl1_ >> 6) & 3u], sc_core::SC_NS));
        else           aplica_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void aplica() {
        if (!pendiente_) { irq_ev_.notify(sc_core::SC_ZERO_TIME); return; }
        pendiente_ = false;
        const bool antes = out();
        if (nuevo_ == antes) return;
        if (nuevo_) ctl1_ |= OUT; else ctl1_ &= ~OUT;
        // IES = 0: IFG en el flanco de subida, IIFG en el de bajada; IES = 1, al revés
        const bool sube = nuevo_;
        int_ |= ((sube != ((ctl1_ & IES) != 0)) ? IFG : IIFG);
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);     // la histéresis cambia de toma
    }
    void listo_proc() {
        if (!(ctl1_ & ON)) return;
        int_ |= RDYIFG;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    uint16_t lee16(uint32_t off, bool dbg) override {
        switch (off) {
            case 0x00: return ctl0_;
            case 0x02: return ctl1_;
            case 0x04: return ctl2_;
            case 0x06: return ctl3_;
            case 0x0C: return int_;
            case 0x0E: {
                static const struct { uint16_t f, e, v; } t[3] = {
                    {IFG, IE, 0x02}, {IIFG, IIE, 0x04}, {RDYIFG, RDYIE, 0x0A}};
                for (const auto& x : t)
                    if ((int_ & x.f) && (int_ & x.e)) {
                        if (!dbg) { int_ &= uint16_t(~x.f); repulso_ = true; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
                        return x.v;
                    }
                return 0;
            }
            default: return 0;
        }
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        switch (off) {
            case 0x00: ctl0_ = uint16_t(mz(ctl0_) & 0x8F8Fu); break;
            case 0x02: {
                const bool estaba = (ctl1_ & ON) != 0;
                // OUT es de solo lectura
                ctl1_ = uint16_t((mz(ctl1_) & 0x1FFEu) | (ctl1_ & OUT));
                if (!(ctl1_ & ON)) ctl1_ &= ~OUT;
                if (!estaba && (ctl1_ & ON)) listo_ev_.notify(sc_core::sc_time(1, sc_core::SC_US));
                pide_ref();
                break;
            }
            case 0x04: ctl2_ = mz(ctl2_); pide_ref(); break;
            case 0x06: ctl3_ = mz(ctl3_); break;
            case 0x0C: int_ = uint16_t(mz(int_) & 0x1313u); irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            default: return;
        }
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_COMP_E_H
