// =============================================================================
// analog_fr.h — Lo analógico de las FR2xx con «Smart Analog Combo» (M7 de
// doc/analisis_msp430.md): el eCOMP con su DAC de 6 bits, el SAC (el OA, y en
// el SAC-L3 la PGA y el DAC de 12 bits) y el TIA [UG FR2 SLAU445I, caps. 18,
// 19 y 20]
//
// EL OPERACIONAL (OpAmpFr), el de los tres: sin lazo dentro (el modo GP del
// SAC, el TIA) su salida busca que V- = V+ por fuera, por las resistencias de
// la placa; aquí cada paso (0,1 µs en alta velocidad, 0,5 µs en bajo
// consumo) la salida se mueve la mitad del error, entre 0 y VCC, hasta que
// el error baja de 0,2 mV o la salida toca un raíl. Con el lazo dentro (el
// seguidor y la PGA del SAC-L3) la salida es la cuenta, en el acto
// [de modelo: ni el ancho de banda ni el ritmo de subida del DS].
//
// EL eCOMP: compara V+ con V- (las entradas que dice el chip para cada
// CPPSEL/CPNSEL, con CPPEN/CPNEN; 110 es su DAC), con la histéresis de
// CPHSEL (la salida cambia cuando la diferencia pasa de la mitad, a cada
// lado) y CPINV; CPIFG y CPIIFG en los flancos según CPIES; CPxIV. El DAC:
// 64 pasos de VDD o de la referencia de 1,5 V; con CPDACBUFS = 0 elige el
// búfer la salida [de modelo: CPOUT = 1 da CPDACBUF2, como CPDACSW = 1]. El
// filtro (CPFLT) no retrasa [de modelo].
//
// EL SAC: SACxOA (SACEN, OAEN, OAPM, PSEL/PMUXEN, NSEL/NMUXEN), SACxPGA (MSEL
// y GAIN, tabla 20-2), SACxDAC (DACEN, DACIE, DACLSEL, DACSREF), SACxDAT,
// SACxDACSTS y SACxIV. El DAC: Vref × DACDAT / 4096 con DACLSEL = 00; las
// cargas por disparo (10 y 11) las da el SoC con `dispara_dac`.
//
// EL TIA: TRICTL (TRIEN, TRIPM, TRIPSEL); TRI- es la entrada que va al
// negativo, y la resistencia Rf, de la placa.
//
// El SoC les da las tensiones (v_fuente) y recoge sus salidas; cualquier
// cambio en un pin les da un toque (`toca`).
// =============================================================================
#ifndef STM32_PERIPH_TI_ANALOG_FR_H
#define STM32_PERIPH_TI_ANALOG_FR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <systemc>

namespace stm32 {

// --- El operacional ----------------------------------------------------------------
SC_MODULE(OpAmpFr) {
    std::function<double()> vcc;
    std::function<bool()> encendido;
    std::function<bool()> lento;                // bajo consumo (OAPM, TRIPM)
    // Sin lazo dentro: V+ y V- (las del SoC); con él: la salida, ya calculada
    std::function<bool()> lazo_dentro;
    std::function<double()> v_mas, v_menos, salida_directa;
    std::function<void()> al_cambiar;           // la salida ha cambiado

    SC_CTOR(OpAmpFr) { SC_THREAD(hilo); }
    void toca() { ev_.notify(sc_core::SC_ZERO_TIME); }
    // < 0: alta impedancia (apagado)
    double salida() const { return vout_; }

private:
    sc_core::sc_event ev_;
    double vout_ = -1.0;
    void pon(double v) {
        if (v == vout_) return;
        vout_ = v;
        if (al_cambiar) al_cambiar();
    }
    void hilo() {
        using namespace sc_core;
        for (;;) {
            if (!encendido || !encendido()) { pon(-1.0); wait(ev_); continue; }
            const double vc = vcc ? vcc() : 3.3;
            if (lazo_dentro && lazo_dentro()) {
                pon(std::min(vc, std::max(0.0, salida_directa())));
                wait(ev_);
                continue;
            }
            const double e = v_mas() - v_menos();
            double v = vout_ < 0.0 ? std::min(vc, std::max(0.0, v_mas())) : vout_;
            const double n = std::min(vc, std::max(0.0, v + 0.5 * e));
            const bool en_rail = (n == vc && e > 0) || (n == 0.0 && e < 0);
            if (vout_ >= 0.0 && (std::fabs(e) < 0.2e-3 || (en_rail && v == n))) { pon(v); wait(ev_); continue; }
            pon(n);
            wait(sc_time(lento && lento() ? 0.5 : 0.1, SC_US), ev_);
        }
    }
};

// --- El eCOMP ------------------------------------------------------------------------
SC_MODULE(EcompFr) {
    // La tensión de la entrada `sel` (0-7) del terminal + (mas) o - ; < 0: nada
    std::function<double(bool mas, unsigned sel)> v_entrada;
    std::function<double()> vcc, vref;          // VDD y la referencia de 1,5 V
    std::function<void()> al_cambiar;           // IFG o IE
    std::function<void(bool)> al_salida;        // CPOUT, hacia el pin y los demás módulos

    SC_CTOR(EcompFr) { SC_METHOD(evalua); sensitive << ev_; dont_initialize(); reset(); }
    void reset() {
        ctl0_ = 0x0100; ctl1_ = 0; int_ = 0; dacctl_ = 0x0002; dacdata_ = 0x0100; out_ = false;
        if (al_salida) al_salida(false);
        toca();
    }
    void toca() { ev_.notify(sc_core::SC_ZERO_TIME); }
    bool salida() const { return out_; }
    bool pide() const { return ((int_ & 1u) && (ctl1_ & 0x4000)) || ((int_ & 2u) && (ctl1_ & 0x8000)); }
    // La tensión del DAC de 6 bits
    double v_dac() const {
        if (!(dacctl_ & 0x80)) return 0.0;
        const double ref = (dacctl_ & 0x04) ? (vref ? vref() : 1.5) : (vcc ? vcc() : 3.3);
        const bool b2 = (dacctl_ & 0x02) ? (dacctl_ & 0x01) : out_;
        const unsigned d = b2 ? (dacdata_ >> 8) & 0x3Fu : dacdata_ & 0x3Fu;
        return ref * d / 64.0;
    }

    uint16_t lee(uint32_t off, bool dbg) {
        switch (off & 0x1E) {
        case 0x00: return ctl0_;
        case 0x02: return uint16_t((ctl1_ & ~1u) | (out_ ? 1u : 0u));
        case 0x06: return int_;
        case 0x08: {
            uint16_t v = 0;
            if ((int_ & 1u) && (ctl1_ & 0x4000)) { v = 2; if (!dbg) int_ = uint16_t(int_ & ~1u); }
            else if ((int_ & 2u) && (ctl1_ & 0x8000)) { v = 4; if (!dbg) int_ = uint16_t(int_ & ~2u); }
            if (v && !dbg && al_cambiar) al_cambiar();
            return v;
        }
        case 0x10: return dacctl_;
        case 0x12: return dacdata_;
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        auto mz = [&](uint16_t c) { return uint16_t((c & ~m) | (v & m)); };
        switch (off & 0x1E) {
        case 0x00: ctl0_ = uint16_t(mz(ctl0_) & 0x1717u); break;
        case 0x02: ctl1_ = uint16_t(mz(ctl1_) & 0xCFFEu); break;
        case 0x06: int_ = uint16_t(mz(int_) & 0x0003u); if (al_cambiar) al_cambiar(); break;
        case 0x10: dacctl_ = uint16_t(mz(dacctl_) & 0x0087u); break;
        case 0x12: dacdata_ = uint16_t(mz(dacdata_) & 0x3F3Fu); break;
        default: return;
        }
        toca();
    }

private:
    uint16_t ctl0_ = 0x0100, ctl1_ = 0, int_ = 0, dacctl_ = 0x0002, dacdata_ = 0x0100;
    bool out_ = false, crudo_ = false;
    sc_core::sc_event ev_;
    void evalua() {
        if (!(ctl1_ & 0x0200)) { pon(false); return; }              // CPEN
        const unsigned ps = ctl0_ & 7u, ns = (ctl0_ >> 8) & 7u;
        const double vp = (ctl0_ & 0x0010) ? (ps == 6 ? v_dac() : v_entrada(true, ps)) : -1.0;
        const double vn = (ctl0_ & 0x1000) ? (ns == 6 ? v_dac() : v_entrada(false, ns)) : -1.0;
        if (vp < 0 || vn < 0) { pon(bool(ctl1_ & 0x0002)); return; }   // un terminal suelto [de modelo: 0]
        static const double HIST[4] = {0.0, 0.010, 0.020, 0.030};
        const double h = HIST[(ctl1_ >> 10) & 3u] / 2.0;
        const double d = vp - vn;
        if (d > h) crudo_ = true;
        else if (d < -h) crudo_ = false;
        pon(crudo_ != bool(ctl1_ & 0x0002));
    }
    void pon(bool o) {
        if (o == out_) return;
        out_ = o;
        // CPIES = 0: CPIFG en la subida y CPIIFG en la bajada; 1, al revés
        const bool sube = o, ies = ctl1_ & 0x0010;
        int_ = uint16_t(int_ | ((sube != ies) ? 1u : 2u));
        if (al_cambiar) al_cambiar();
        if (al_salida) al_salida(o);
        toca();                                                       // el DAC con CPDACBUFS = 0 cambia de búfer
    }
};

// --- El SAC ----------------------------------------------------------------------------
SC_MODULE(SacFr) {
    bool l3 = false;                            // con PGA y DAC (SAC-L3)
    // Las tensiones que da el SoC: 0 el pin OAx+, 1 el pin OAx-, 2 la salida
    // del OA pareja, 3 la fuente «10» del positivo, 4 la del negativo
    std::function<double(unsigned)> v_fuente;
    std::function<double(bool secundaria)> v_ref_dac;
    std::function<double()> vcc;
    std::function<void()> al_cambiar;           // DACIFG o DACIE
    std::function<void()> al_salida;            // la salida del OA
    OpAmpFr oa;

    SacFr(sc_core::sc_module_name nm) : sc_core::sc_module(nm), oa("oa") {
        oa.vcc = [this] { return vcc ? vcc() : 3.3; };
        oa.encendido = [this] { return (oa_ & 0x0400) && (oa_ & 0x0100); };
        oa.lento = [this] { return oa_ & 0x0200; };
        oa.lazo_dentro = [this] { return l3 && (oa_ & 0x80) && ((oa_ >> 4) & 3u) == 1; };
        oa.v_mas = [this] { return mas(); };
        oa.v_menos = [this] { return (oa_ & 0x80) ? v_fuente(((oa_ >> 4) & 3u) == 2 ? 4 : 1) : mas(); };
        oa.salida_directa = [this] { return pga(); };
        oa.al_cambiar = [this] { if (al_salida) al_salida(); };
        reset();
    }
    void reset() { oa_ = 0; pga_ = 0x0001; dac_ = 0; dat_ = 0; lat_ = 0; sts_ = 0; oa.toca(); }
    void toca() { oa.toca(); }
    double salida() const { return oa.salida(); }
    bool pide() const { return (sts_ & 1u) && (dac_ & 0x0002); }
    // El DAC de 12 bits (SAC-L3)
    double v_dac() const {
        if (!l3 || !(dac_ & 1u)) return 0.0;
        return (v_ref_dac ? v_ref_dac(dac_ & 0x1000) : 3.3) * double(lat_ & 0x0FFFu) / 4096.0;
    }
    // La carga del latch por el disparo del chip (DACLSEL = 10 u 11)
    void dispara_dac(unsigned lsel) {
        if (!l3 || !(dac_ & 1u) || ((dac_ >> 8) & 3u) != lsel) return;
        lat_ = dat_; sts_ |= 1u;
        if (al_cambiar) al_cambiar();
        oa.toca();
    }

    uint16_t lee(uint32_t off, bool dbg) {
        switch (off & 0x0E) {
        case 0x00: return oa_;
        case 0x02: return l3 ? pga_ : 0;
        case 0x04: return l3 ? dac_ : 0;
        case 0x06: return l3 ? dat_ : 0;
        case 0x08: return l3 ? sts_ : 0;
        case 0x0A:
            if (l3 && pide()) { if (!dbg) { sts_ = 0; if (al_cambiar) al_cambiar(); } return 4; }
            return 0;
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        auto mz = [&](uint16_t c) { return uint16_t((c & ~m) | (v & m)); };
        switch (off & 0x0E) {
        case 0x00: oa_ = uint16_t(mz(oa_) & 0x07BBu); break;
        case 0x02: if (l3) pga_ = uint16_t(mz(pga_) & 0x0073u); break;
        case 0x04: if (l3) dac_ = uint16_t(mz(dac_) & 0x1307u); break;
        case 0x06:
            if (l3) { dat_ = uint16_t(mz(dat_) & 0x0FFFu); if (((dac_ >> 8) & 3u) == 0) lat_ = dat_; }
            break;
        case 0x08: if (l3 && (v & m & 1u)) { sts_ = 0; if (al_cambiar) al_cambiar(); } break;
        default: return;
        }
        oa.toca();
    }

private:
    uint16_t oa_ = 0, pga_ = 0x0001, dac_ = 0, dat_ = 0, lat_ = 0, sts_ = 0;
    // El positivo: PSEL (00 el pin, 01 el DAC, 10 lo del chip)
    double mas() const {
        if (!(oa_ & 0x08)) return 0.0;                                // PMUXEN = 0: suelto [de modelo: 0 V]
        switch (oa_ & 3u) {
        case 0: return v_fuente(0);
        case 1: return v_dac();
        case 2: return v_fuente(3);
        default: return 0.0;
        }
    }
    // La PGA (NSEL = 01) [UG FR2 tabla 20-2]
    double pga() const {
        const double vp = mas();
        const unsigned g = (pga_ >> 4) & 7u;
        switch (pga_ & 3u) {
        case 1: return vp;                                            // seguidor
        case 2: { static const double G[8] = {1, 2, 3, 5, 9, 17, 26, 33}; return vp * G[g]; }
        default: {                                                    // inversor: OAx- (00) o la pareja (11)
            static const double G[8] = {0, 1, 2, 4, 8, 16, 25, 32};
            const double vin = (pga_ & 3u) == 0 ? v_fuente(1) : v_fuente(2);
            return vp - G[g] * (vin - vp);
        }
        }
    }
};

// --- El TIA ---------------------------------------------------------------------------
SC_MODULE(TiaFr) {
    // 0 el pin TRI+, 1 el pin TRI-, 2 y 3 las fuentes «01» y «10» del positivo
    std::function<double(unsigned)> v_fuente;
    std::function<double()> vcc;
    std::function<void()> al_salida;
    OpAmpFr oa;

    TiaFr(sc_core::sc_module_name nm) : sc_core::sc_module(nm), oa("oa") {
        oa.vcc = [this] { return vcc ? vcc() : 3.3; };
        oa.encendido = [this] { return ctl_ & 1u; };
        oa.lento = [this] { return ctl_ & 2u; };
        oa.lazo_dentro = [] { return false; };
        oa.v_mas = [this] {
            const unsigned s = (ctl_ >> 6) & 3u;
            return s == 0 ? v_fuente(0) : s < 3 ? v_fuente(s + 1) : 0.0;
        };
        oa.v_menos = [this] { return v_fuente(1); };
        oa.al_cambiar = [this] { if (al_salida) al_salida(); };
    }
    void reset() { ctl_ = 0; oa.toca(); }
    void toca() { oa.toca(); }
    double salida() const { return oa.salida(); }
    uint16_t lee(uint32_t, bool) { return ctl_; }
    void escribe(uint32_t, uint16_t v, uint16_t m) { ctl_ = uint16_t(((ctl_ & ~m) | (v & m)) & 0x00C3u); oa.toca(); }

private:
    uint16_t ctl_ = 0;
};

} // namespace stm32
#endif // STM32_PERIPH_TI_ANALOG_FR_H
