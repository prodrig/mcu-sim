// =============================================================================
// esi.h — El ESI (Extended Scan Interface) del MSP430FR6989 (M8 de
// doc/analisis_msp430.md) [UG FR5 SLAU367P cap. 37; DS FR6989 SLAS789D
// §5.3.23 y tablas 6-15 y 6-82]
//
// Registros en 0x0D00 (0x00-0x9F) y la RAM de la PSM en 0x0E00 (128 bytes):
//   +00..08 ESIDEBUG1-5: la última dirección de la PSM; el índice de la TSM y
//           Q7-Q0; el ESITSMx en curso; el ESIDAC1Rx y el ESIDAC2Rx elegidos
//   +10..16 ESICNT0-3   +1A ESIIV   +1C ESIINT1 (IE, IFGSET1/2)
//   +1E ESIINT2 (IFG, IS0, IS2)  +20 ESIAFE  +22 ESIPPU (ESIOUT0-7, TCHOUT0-1)
//   +24 ESITSM  +26 ESIPSM  +28 ESIOSC  +2A ESICTL  +2C/2E ESITHR1/2
//   +40..4E ESIDAC1R0-7  +50..5E ESIDAC2R0-7  +60..9E ESITSM0-31
//
// LA TSM (§37.2.2): una secuencia empieza en ESITSM0 y acaba en el estado con
// ESISTOP (que dura un ciclo de ESIHFCLK). Cada estado dura ESIREPEAT + 1
// ciclos de ACLK (ESICLK = 1) o de ESIHFCLK (SMCLK o el oscilador del ESI,
// entre ESIDIV1) [de modelo: sin la sincronización de la tabla 37-5]. La
// arranca ESISTART (ESITSMTRG 10/11; se borra al acabar), el ACLK dividido
// (01/11: ESIDIV2 · ((2·DIV3A + 1) · (2·DIV3B + 1) · 2) ciclos; ESIIFG2) o,
// con ESITSMRP, el final de la anterior (al siguiente ciclo de ACLK).
// ESIIFG1 en el flanco de subida de ESISTOP. Con ESITESTD, tras la secuencia
// normal va la de prueba (TESTDX: el canal de ESITCH0/1 según ESITESTS1, los
// ESIDAC1R6/R7, la salida en ESITCHOUT0/1; no dispara la PSM) y ESITESTD se
// borra. Mientras corre, los ESITSMx se leen 0 y no se escriben.
//
// LOS AFE (§37.2.1): el comparador compara la entrada con el DAC
// (AVCC · código/4096) si el estado lleva ESICA y ESIDAC (el AFE2, además,
// con ESICA2EN y ESIDAC2EN); si no, da 0. La entrada del AFE1: ESICIx
// (ESICA1X; o ESICI con ESICISEL, o ESICI3 con ESICACI3), la muestra de
// ESICHx (ESISH: se toma, de todos a la vez, en los estados con ESIEX) o el
// pin ESICHx. La del AFE2: ESICIx (ESICA2X) o ESICHx. El DAC de cada canal,
// ESIDACyR(2·canal + ESIOUT anterior): la histéresis de la tabla 37-3.
// ESICAyINV invierte. Con ESIRSON, un 1 del comparador pone el cerrojo del
// canal; en el flanco de ESISTOP los cerrojos de los canales medidos pasan a
// su ESIOUTx [de modelo: el cerrojo se borra al empezar la secuencia].
// Con ESITEN, ESIEX y ESILCEN, el ESICHx del estado se tira a ESIDVSS (la
// excitación); ESICOM da AVCC/2 con ESIVMIDEN y ESISH = 0 [de modelo: sin
// los 6 ms de subida].
//
// LA PSM (§37.2.5): en el flanco de ESISTOP de una secuencia normal, la
// dirección V = PPUS1 | PPUS2 << 1 | (ESIV2SEL ? Q0 : PPUS3) << 2 | Q3-Q5 << 3
// | (ESIQ6EN ? Q6 : 0) << 6, y Q = RAM[V]. Q1 cuenta ESICNT0 (+1) y ESICNT1
// (+1); Q2, ESICNT2 (-1) y ESICNT1 (-1); con los dos, ESICNT1 no cambia. Q6
// da ESIIFG5; Q7, ESIIFG6 (y con ESIQ7TRG, otra transición enseguida [de
// modelo: hasta 8 seguidas]). ESIIFG7/ESIIFG4 por ESIIS0/ESIIS2 (cada cuenta,
// módulo 4, módulo 256 o el paso por cero); ESIIFG3 cuando ESICNT1 vale
// ESITHR1 o ESITHR2. La RAM solo la ve la CPU con ESIEN = 0 (si no, se lee 0
// y no se escribe). Al encender (ESIEN), Q = RAM[0].
//
// ESIIFG0/ESIIFG8: al escribirse el ESIOUTx que eligen ESIIFGSET1/2, si queda
// como piden (a 1 o a 0). ESIIV: 2 IFG1, 4 IFG0, 6 IFG8, 8 IFG3, 0A IFG6,
// 0C IFG5, 0E IFG4, 10 IFG7, 12 IFG2; leerlo borra la más alta; escribirlo,
// todas.
//
// EL OSCILADOR: 4,8 MHz con ESICLKFQ = 0x20 y un 3 % por paso, entre 2,3 y
// 7,9 MHz [DS tabla 5-41; de modelo: exponencial]. Con ESIHFSEL y ESICLKGON,
// ESICNT3 cuenta sus ciclos en un periodo de ACLK (empieza en el siguiente
// flanco); mientras cuenta se lee 1.
//
// AL TIMER (tabla 6-15): ESIO0 = ESIEX(tsm) a TA3 CCI2B; ESIO1 (TA3 CCI3B) y
// ESIO2 (CCI4B) = ESIC1OUT o, con ESICS, PPUS1 y PPUS2.
// =============================================================================
#ifndef STM32_PERIPH_TI_ESI_H
#define STM32_PERIPH_TI_ESI_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace stm32 {

class Esi : public sc_core::sc_module {
public:
    // Del SoC
    std::function<double()> f_aclk, f_smclk, v_avcc;
    std::function<double(unsigned)> v_ch, v_ci;   // ESICH0-3 y ESICI0-3 (4: el pin ESICI)
    std::function<void()> al_cambiar;            // una bandera o una salida: el SoC mira
    // Las salidas al Timer (ESIO0-2) y a los pines
    bool esio[3] = {};

    SC_HAS_PROCESS(Esi);
    explicit Esi(sc_core::sc_module_name nm) : sc_core::sc_module(nm) {
        SC_THREAD(tsm_proc);
        SC_THREAD(disparo_proc);
        reset();
    }
    void reset() {
        int1_ = int2_ = afe_ = tsm_ = osc_ = ctl_ = thr1_ = 0;
        psm_ = 0x0080; osc_ = 0x2000; thr2_ = 0xFFFF;
        cnt_[0] = cnt_[1] = cnt_[2] = 0; cnt3_ = 0;
        ppu_ = 0; q_ = 0; ult_dir_ = 0; idx_ = 0; activo_ = false; testdx_ = false;
        c1_ = c2_ = false; dac1_sel_ = dac2_sel_ = 0;
        for (auto& x : esio) x = false;
        ev_cfg_.notify(sc_core::SC_ZERO_TIME);
    }
    bool pide() const { return (int2_ & int1_ & 0x01FF) != 0; }
    bool en() const { return (ctl_ & 1u) != 0; }
    unsigned secuencias() const { return n_sec_; }

    // Las salidas a los pines: 0-3 ESITEST0-3, 4 ESITEST4, 5 ESIC1OUT, 6 ESIC2OUT
    bool salida(unsigned n) const {
        switch (n) {
        case 0: return activo_ && ca_on(1);                         // [de modelo: el comparador del AFE1 encendido]
        case 1: return c1_;
        case 2: return activo_ && ca_on(2);
        case 3: return c2_;
        case 4: switch ((psm_ >> 8) & 3u) { case 0: return (q_ >> 2) & 1u; case 1: return (q_ >> 1) & 1u; case 2: return activo_; default: return c1_; }
        case 5: return c1_;
        case 6: return c2_;
        default: return false;
        }
    }
    // La excitación: el ESICHx que tira a ESIDVSS (o -1)
    int excita() const {
        if (!activo_ || !(afe_ & 1u) || (afe_ & 2u)) return -1;
        const uint16_t t = estado_;
        return ((t & 0x08) && (t & 0x04)) ? int(t & 3u) : -1;
    }
    double v_esicom() const {
        if ((afe_ & 0x04) && !(afe_ & 0x02)) return (v_avcc ? v_avcc() : 3.0) / 2.0;
        return -1.0;
    }

    uint16_t lee(uint32_t off, bool dbg) {
        off &= 0xFE;
        if (off >= 0x40 && off < 0x50) return dac1_[(off - 0x40) / 2];
        if (off >= 0x50 && off < 0x60) return dac2_[(off - 0x50) / 2];
        if (off >= 0x60 && off < 0xA0) return activo_ ? 0 : tsmr_[(off - 0x60) / 2];
        switch (off) {
        case 0x00: return ult_dir_;
        case 0x02: return uint16_t((idx_ & 0x1Fu) << 8 | q_);
        case 0x04: return activo_ ? estado_ : 0;
        case 0x06: return uint16_t((dac1_sel_ & 7u) << 12 | (dac1_[dac1_sel_ & 7u] & 0x0FFF));
        case 0x08: return uint16_t((dac2_sel_ & 7u) << 12 | (dac2_[dac2_sel_ & 7u] & 0x0FFF));
        case 0x10: return cnt_[0];
        case 0x12: return cnt_[1];
        case 0x14: return cnt_[2];
        case 0x16: return lee_cnt3();
        case 0x1A: {
            static const struct { uint16_t b, v; } P[] = {{1u << 1, 2}, {1u << 0, 4}, {1u << 8, 6}, {1u << 3, 8}, {1u << 6, 0xA},
                                                          {1u << 5, 0xC}, {1u << 4, 0xE}, {1u << 7, 0x10}, {1u << 2, 0x12}};
            for (const auto& p : P)
                if (int2_ & int1_ & p.b) { if (!dbg) { int2_ &= uint16_t(~p.b); cambia(); } return p.v; }
            return 0;
        }
        case 0x1C: return int1_;
        case 0x1E: return int2_;
        case 0x20: return afe_;
        case 0x22: return ppu_;
        case 0x24: return tsm_;
        case 0x26: return psm_;
        case 0x28: return osc_;
        case 0x2A: return ctl_;
        case 0x2C: return thr1_;
        case 0x2E: return thr2_;
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        off &= 0xFE;
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        if (off >= 0x40 && off < 0x50) { uint16_t& r = dac1_[(off - 0x40) / 2]; r = uint16_t(mz(r) & 0x0FFF); return; }
        if (off >= 0x50 && off < 0x60) { uint16_t& r = dac2_[(off - 0x50) / 2]; r = uint16_t(mz(r) & 0x0FFF); return; }
        if (off >= 0x60 && off < 0xA0) { if (!activo_) { uint16_t& r = tsmr_[(off - 0x60) / 2]; r = mz(r); } return; }
        switch (off) {
        case 0x1A: int2_ &= uint16_t(~0x01FFu); cambia(); return;   // escribir ESIIV las borra todas
        case 0x1C: int1_ = uint16_t(mz(int1_) & 0xFDFF); cambia(); return;
        case 0x1E: int2_ = uint16_t(mz(int2_) & 0x6DFF); cambia(); return;
        case 0x20: afe_ = uint16_t(mz(afe_) & 0x0FFF); cambia(); return;
        case 0x24: {
            const uint16_t ant = tsm_;
            tsm_ = uint16_t(mz(tsm_) & 0x7FFF);
            if ((tsm_ & 0x0800) && !(ant & 0x0800)) ev_start_.notify(sc_core::SC_ZERO_TIME);
            ev_cfg_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        case 0x26: {
            uint16_t x = mz(psm_);
            if (x & 0x2000) { cnt_[0] = 0; }
            if (x & 0x4000) { cnt_[1] = 0; }
            if (x & 0x8000) { cnt_[2] = 0; }
            psm_ = uint16_t(x & 0x03BD);
            return;
        }
        case 0x28: {
            const uint16_t ant = osc_;
            osc_ = uint16_t(mz(osc_) & 0x3F03);
            if ((osc_ & 3u) == 3u && (ant & 3u) != 3u) calibra();
            return;
        }
        case 0x2A: {
            const bool antes = en();
            ctl_ = mz(ctl_);
            if (en() && !antes) { q_ = ram[0]; ult_dir_ = 0; }
            if (!en() && antes) { cnt_[0] = cnt_[1] = cnt_[2] = 0; q_ = 0; activo_ = false; }
            ev_cfg_.notify(sc_core::SC_ZERO_TIME);
            cambia();
            return;
        }
        case 0x2C: thr1_ = mz(thr1_); return;
        case 0x2E: thr2_ = mz(thr2_); return;
        default: return;
        }
    }
    // La RAM de la PSM: la CPU solo con ESIEN = 0
    uint8_t ram[128] = {};
    uint8_t lee_ram(unsigned i) const { return en() ? 0 : ram[i & 127u]; }
    void escribe_ram(unsigned i, uint8_t v) { if (!en()) ram[i & 127u] = v; }

    double f_osc() const {
        const int fq = int((osc_ >> 8) & 0x3Fu) - 32;
        return std::max(2.3e6, std::min(7.9e6, 4.8e6 * std::pow(1.03, fq)));
    }

private:
    uint16_t int1_, int2_, afe_, tsm_, psm_, osc_, ctl_, thr1_, thr2_;
    uint16_t cnt_[3], cnt3_;
    uint16_t dac1_[8] = {}, dac2_[8] = {}, tsmr_[32] = {};
    uint16_t ppu_;
    uint8_t q_, ult_dir_, idx_;
    uint16_t estado_ = 0;
    bool activo_, testdx_, c1_, c2_;
    unsigned dac1_sel_, dac2_sel_;
    unsigned n_sec_ = 0;
    double sh_[4] = {};
    bool cerrojo_[4] = {}, cerrojo2_[4] = {}, medido_[4] = {}, medido2_[4] = {}, tcerrojo_[2] = {}, tmedido_[2] = {};
    sc_core::sc_time cnt3_fin_;
    sc_core::sc_event ev_start_, ev_cfg_;
    bool pend_start_ = false;

    void cambia() { if (al_cambiar) al_cambiar(); }
    bool ca_on(unsigned afe) const {
        const uint16_t t = estado_;
        if (!(t & 0x10) || !(t & 0x100)) return false;
        return afe == 1 || ((afe_ & 0x0400) && (afe_ & 0x0800));
    }
    double aclk() const { const double f = f_aclk ? f_aclk() : 32768.0; return f > 0 ? f : 32768.0; }
    double hfclk() const {
        const double f = (osc_ & 1u) ? f_osc() : (f_smclk ? f_smclk() : 0.0);
        return (f > 0 ? f : 1e6) / double(1u << (tsm_ & 3u));
    }
    void calibra() {
        // empieza en el siguiente flanco de ACLK y dura un periodo
        const double pa = 1.0 / aclk();
        const double t = sc_core::sc_time_stamp().to_seconds();
        const double sig = (std::floor(t / pa) + 1.0) * pa;
        cnt3_fin_ = sc_core::sc_time(sig + pa, sc_core::SC_SEC);
        cnt3_ = uint16_t(std::lround(f_osc() / aclk()));
    }
    uint16_t lee_cnt3() const {
        if ((osc_ & 3u) == 3u && sc_core::sc_time_stamp() < cnt3_fin_) return 1;
        return cnt3_;
    }
    unsigned div_disparo() const {
        const unsigned d2 = 1u << ((tsm_ >> 2) & 3u), a = (tsm_ >> 4) & 7u, b = (tsm_ >> 7) & 7u;
        return d2 * ((a + 1) * 2 - 1) * ((b + 1) * 2 - 1) * 2;
    }
    // El disparo por el ACLK dividido (ESITSMTRG 01/11, sin ESITSMRP)
    void disparo_proc() {
        for (;;) {
            const unsigned trg = (tsm_ >> 12) & 3u;
            if (!en() || !(trg & 1u) || (tsm_ & 0x0400)) { wait(ev_cfg_); continue; }
            // [de modelo: un cambio de la configuración vuelve a empezar el divisor]
            const sc_core::sc_time p(double(div_disparo()) / aclk(), sc_core::SC_SEC);
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            wait(p, ev_cfg_);
            if (sc_core::sc_time_stamp() - t0 >= p && en() && ((tsm_ >> 12) & 1u) && !(tsm_ & 0x0400) && !activo_) {
                pend_start_ = true; ev_start_.notify(sc_core::SC_ZERO_TIME);
            }
        }
    }
    void tsm_proc() {
        for (;;) {
            // esperar un disparo: ESISTART, el ACLK dividido o el modo repetido
            while (!(en() && (pend_start_ || ((tsm_ & 0x0800) && ((tsm_ >> 12) & 2u)) ||
                              (((tsm_ >> 12) & 3u) && (tsm_ & 0x0400)))))
                wait(ev_start_ | ev_cfg_);
            const bool por_aclk = pend_start_;
            pend_start_ = false;
            if ((tsm_ & 0x0800) && ((tsm_ >> 12) & 3u) == 2u) wait(sc_core::sc_time(2.0 / aclk(), sc_core::SC_SEC));   // la sincronización con ACLK
            if (!en()) continue;
            secuencia(false, por_aclk);
            if (ctl_ & 0x0002) { secuencia(true, false); ctl_ &= uint16_t(~0x0002u); }    // la de prueba
            tsm_ &= uint16_t(~0x0800u);                                                    // ESISTART
            if (tsm_ & 0x0400) wait(sc_core::sc_time(1.0 / aclk(), sc_core::SC_SEC));    // ESITSMRP: al siguiente ACLK
        }
    }
    double dac_v(uint16_t c) const { return (v_avcc ? v_avcc() : 3.0) * double(c & 0x0FFF) / 4096.0; }
    // El comparador de cada AFE en el estado `t` (y su DAC)
    void compara(uint16_t t) {
        const unsigned ch = t & 3u;
        const bool tests1 = (t & 0x80) != 0;
        const unsigned tch = testdx_ ? ((ctl_ >> (tests1 ? 5 : 3)) & 3u) : ch;
        // AFE1
        bool o1 = false;
        if (ca_on(1)) {
            double vin;
            if (afe_ & 0x40) vin = (afe_ & 0x20) ? (v_ci ? v_ci(4) : 0.0) : (v_ci ? v_ci((afe_ & 0x10) ? 3u : tch) : 0.0);
            else if (afe_ & 0x02) vin = sh_[tch];
            else vin = v_ch ? v_ch(tch) : 0.0;
            unsigned sel;
            if (testdx_) sel = tests1 ? 7u : 6u;
            else sel = 2u * ch + ((ppu_ >> ch) & 1u);
            dac1_sel_ = sel;
            o1 = vin > dac_v(dac1_[sel]);
            if (afe_ & 0x0100) o1 = !o1;
        }
        // AFE2
        bool o2 = false;
        if (ca_on(2)) {
            const double vin = (afe_ & 0x80) ? (v_ci ? v_ci(ch) : 0.0) : (v_ch ? v_ch(ch) : 0.0);
            const unsigned sel = 2u * ch + ((ppu_ >> (4 + ch)) & 1u);
            dac2_sel_ = sel;
            o2 = vin > dac_v(dac2_[sel]);
            if (afe_ & 0x0200) o2 = !o2;
        }
        c1_ = o1; c2_ = o2;
        if (t & 0x40) {                                             // ESIRSON: los cerrojos
            if (testdx_) { const unsigned k = tests1 ? 1u : 0u; tmedido_[k] = true; if (o1) tcerrojo_[k] = true; }
            else {
                medido_[ch] = true; if (o1) cerrojo_[ch] = true;
                if ((afe_ & 0x0400) && (afe_ & 0x0800)) { medido2_[ch] = true; if (o2) cerrojo2_[ch] = true; }
            }
        }
    }
    void timer_sal() {
        const unsigned p1 = (ctl_ >> 7) & 7u, p2 = (ctl_ >> 10) & 7u;
        esio[0] = activo_ && (estado_ & 0x08);
        esio[1] = (ctl_ & 4u) ? ((ppu_ >> p1) & 1u) : c1_;
        esio[2] = (ctl_ & 4u) ? ((ppu_ >> p2) & 1u) : c1_;
        cambia();
    }
    void secuencia(bool prueba, bool por_aclk) {
        activo_ = true; testdx_ = prueba; ++n_sec_;
        if (por_aclk && !prueba) { int2_ |= 0x04; }                // ESIIFG2
        for (unsigned k = 0; k < 4; ++k) { cerrojo_[k] = cerrojo2_[k] = medido_[k] = medido2_[k] = false; }
        tcerrojo_[0] = tcerrojo_[1] = tmedido_[0] = tmedido_[1] = false;
        for (unsigned i = 0; i < 32; ++i) {
            const uint16_t t = tsmr_[i];
            idx_ = uint8_t(i); estado_ = t;
            if ((t & 0x08) && (afe_ & 0x02))                         // ESIEX con S/H: se muestrean todos
                for (unsigned k = 0; k < 4; ++k) sh_[k] = v_ch ? v_ch(k) : 0.0;
            compara(t);
            timer_sal();
            if (t & 0x0200) { fin_secuencia(prueba); break; }      // ESISTOP
            const double f = (t & 0x0400) ? aclk() : hfclk();
            wait(sc_core::sc_time(double(((t >> 11) & 0x1Fu) + 1u) / f, sc_core::SC_SEC));
            compara(t);                                             // al final del estado, otra vez
        }
        wait(sc_core::sc_time(1.0 / hfclk(), sc_core::SC_SEC));    // el estado de parada: un ciclo
        activo_ = false; testdx_ = false; estado_ = tsmr_[0]; idx_ = 0;
        c1_ = c2_ = false;
        timer_sal();
    }
    void pon_out(unsigned bit, bool v) {
        ppu_ = uint16_t(v ? (ppu_ | (1u << bit)) : (ppu_ & ~(1u << bit)));
        // ESIIFG0 / ESIIFG8
        const unsigned s1 = (int1_ >> 10) & 7u, s2 = (int1_ >> 13) & 7u;
        if (bit < 4 && (s1 >> 1) == bit && v == !(s1 & 1u)) int2_ |= 0x0001;
        if (bit >= 4 && (s2 >> 1) == bit - 4 && v == !(s2 & 1u)) int2_ |= 0x0100;
    }
    void fin_secuencia(bool prueba) {
        if (prueba) {
            for (unsigned k = 0; k < 2; ++k)
                if (tmedido_[k]) ppu_ = uint16_t(tcerrojo_[k] ? (ppu_ | (0x100u << k)) : (ppu_ & ~(0x100u << k)));
        } else {
            for (unsigned k = 0; k < 4; ++k) {
                if (medido_[k]) pon_out(k, cerrojo_[k]);
                if (medido2_[k]) pon_out(4 + k, cerrojo2_[k]);
            }
        }
        int2_ |= 0x0002;                                            // ESIIFG1
        if (!prueba) psm();
        cambia();
    }
    void cuenta() {
        const bool q1 = (q_ >> 1) & 1u, q2 = (q_ >> 2) & 1u;
        const unsigned is0 = (int2_ >> 10) & 3u, is2 = (int2_ >> 13) & 3u;
        if (q1 && (psm_ & 0x08)) {
            const uint16_t a = cnt_[0]++;
            if (is0 == 0 || (is0 == 1 && (cnt_[0] & 3u) == 0) || (is0 == 2 && (cnt_[0] & 0xFFu) == 0) || (is0 == 3 && a == 0xFFFF)) int2_ |= 0x80;
        }
        if (q2 && (psm_ & 0x20)) {
            const uint16_t a = cnt_[2]--;
            if (is2 == 0 || (is2 == 1 && (cnt_[2] & 3u) == 0) || (is2 == 2 && (cnt_[2] & 0xFFu) == 0) || (is2 == 3 && a == 1)) int2_ |= 0x10;
        }
        if ((psm_ & 0x10) && (q1 != q2)) {
            cnt_[1] = uint16_t(cnt_[1] + (q1 ? 1 : -1));
            if (cnt_[1] == thr1_ || cnt_[1] == thr2_) int2_ |= 0x08;
        }
        if (q_ & 0x40) int2_ |= 0x20;                                // ESIIFG5
        if (q_ & 0x80) int2_ |= 0x40;                                // ESIIFG6
    }
    void psm() {
        for (unsigned n = 0; n < 8; ++n) {
            const unsigned p1 = (ctl_ >> 7) & 7u, p2 = (ctl_ >> 10) & 7u, p3 = (ctl_ >> 13) & 7u;
            const unsigned s1 = (ppu_ >> p1) & 1u, s2 = (ppu_ >> p2) & 1u, s3 = (ppu_ >> p3) & 1u;
            const unsigned v2 = (psm_ & 0x80) ? (q_ & 1u) : s3;
            const unsigned dir = s1 | s2 << 1 | v2 << 2 | (q_ & 0x38u) | ((psm_ & 1u) ? (q_ & 0x40u) : 0u);
            ult_dir_ = uint8_t(dir);
            q_ = ram[dir & 127u];
            cuenta();
            if (!((psm_ & 0x04) && (q_ & 0x80))) break;              // ESIQ7TRG: otra enseguida
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_ESI_H
