// =============================================================================
// cs_fr.h — El sistema de reloj (CS) de las FR2xx/FR4xx (M6 de
// doc/analisis_msp430.md) [UG FR2 SLAU445I cap. 3]
//
// Nueve registros en CS_BASE (0x180): CSCTL0-CSCTL8.
//
//   FLL      fDCOCLK = D·(N+1)·fFLLREF/n; DCOCLKDIV = fDCOCLK/D. FLLREF es XT1
//            o REFO (SELREF; sin XT1, REFO). SCG0 lo para: el DCO se queda en
//            su grifo (CSCTL0.DCO), que también se puede escribir.
//   DCO      ocho márgenes (DCORSEL: 1, 2, 4, 8, 12, 16, 20, 24 MHz) y 512
//            grifos. [de modelo] El grifo 256 da la frecuencia nominal del
//            margen con el recorte de fábrica (DCOFTRIM = 3); cada grifo
//            multiplica por 2^(1/256), y cada paso de DCOFTRIM por 1,06. Así
//            el FLL engancha en el grifo que toque, y el recorte por programa
//            de DriverLib (privateCSComputeDCOFTrim: busca el DCOFTRIM con el
//            grifo más cerca de 256) converge igual que en el chip. Con el
//            grifo en 0 o en 511, DCOFFG y FLLUNLOCK = 11.
//   FLLUNLOCK  tras un cambio con el FLL en marcha, 01 (lento) o 10 (rápido)
//            durante 32 periodos de la referencia [de modelo: el tiempo de
//            enganche; la frecuencia salta a la final al momento]; con su
//            historia en FLLUNLOCKHIS.
//   MCLK     DCOCLKDIV, REFO, XT1 o VLO (SELMS) / DIVM; SMCLK = MCLK / DIVS
//            (sale de MCLK) [UG FR2 §3.2.5]; ACLK: XT1, REFO o VLO (SELA).
//   Faltas   XT1OFFG si XT1 está pedido y no oscila (ACLK, MCLK, FLLREF, o
//            XT1AUTOOFF = 0); OFIFG con cualquier falta. Sin XT1, lo que lo
//            pide recibe REFO [UG FR2 §3.2.9].
//   LPM      CPUOFF para MCLK; SCG1, SMCLK (salvo una petición con
//            SMCLKREQEN); OSCOFF, ACLK (salvo petición con ACLKREQEN, que es
//            lo que hace el RTC o el perro con el VLO o XT1).
//
// CON `fr5` (M8): el CS de las FR5xx/FR6xx [UG FR5 SLAU367P cap. 3], siete
// registros en CS_BASE (0x160): CSCTL0-CSCTL6.
//   CSCTL0   CSKEY: 0xA5 en el byte alto abre los demás (se lee 0x96); otro
//            valor los cierra [de modelo: sin PUC, porque DriverLib cierra
//            escribiendo 0 en CSCTL0_H]. Cerrado, escribir CSCTL1-6 no hace
//            nada.
//   CSCTL1   DCORSEL y DCOFSEL: el DCO de frecuencias fijas recortadas de
//            fábrica [DS FR5969 tabla 5-6]: 1, 2,667, 3,5, 4, 5,333, 7, 8 MHz
//            con DCORSEL = 0; 1, 5,333, 7, 8, 16, 21, 24 MHz con DCORSEL = 1
//            (DCOFSEL = 7 como el 6).
//   CSCTL2   SELA, SELS, SELM: 0 LFXTCLK, 1 VLO, 2 LFMODCLK (MODOSC/128), 3
//            DCO, 4 MODCLK, 5 HFXTCLK (ACLK, solo las tres primeras).
//   CSCTL3   DIVA, DIVS, DIVM: /1../32. Tras el PUC, MCLK = SMCLK = DCO/8.
//   CSCTL4   LFXTOFF, SMCLKOFF, VLOOFF, HFXTOFF, LFXTBYPASS, HFXTBYPASS,
//            HFFREQ y los DRIVE.
//   CSCTL5   LFXTOFFG (1 tras el POR), HFXTOFFG (0 tras el POR) [tabla 3-9]:
//            un oscilador encendido que no oscila. El reloj que lo usa pasa a
//            LFMODCLK (LFXT) o a MODCLK (HFXT) [§3.2.8]. ENSTFCNT1/2, los
//            contadores de arranque.
//   CSCTL6   las peticiones condicionales (ACLKREQEN, MCLKREQEN, SMCLKREQEN).
//
// El HFXT [§3.2.3; DS FR5969 tabla 5-5]. Está encendido si su HFXIN tiene
// PSEL y (HFXTOFF = 0, o MCLK lo usa con CPUOFF = 0, o SMCLK con SMCLKOFF = 0
// y sin SCG1 o con su petición). Con cristal (HFXTBYPASS = 0) oscila si la
// placa lo tiene y su frecuencia cae en el margen de HFFREQ (1: 4-8 MHz, 2:
// 8,01-16, 3: 16,01-24; HFFREQ = 0 no vale con cristal, nota 2); en bypass,
// con un reloj en HFXIN (el que declara el ExtClock del nodo) de al menos
// fFault,HFXT = 800 kHz. Hasta que arranca, HFXTOFFG y MODCLK: el arranque
// es la subida de la oscilación más el contador de 1024 ciclos (ENSTFCNT2;
// en bypass, solo el contador). tSTART,HFXT de la tabla 5-5: 1,6 ms a 4 MHz
// con HFXTDRIVE = 0 y 0,6 ms a 24 MHz con HFXTDRIVE = 3; [de modelo] la
// subida, en línea recta entre esos dos puntos (1,344 ms y 0,557 ms sin el
// contador), sea cual sea HFXTDRIVE. Apagado, se pierde: al encenderlo otra
// vez, otro arranque (la nota de §3.2.3 sobre los LPM). Fuera de margen en
// bypass: avisa y sigue [de modelo].
// El LFXT: con LFXTBYPASS = 1 pide un reloj declarado en LFXIN (10,5-50 kHz;
// por debajo de 3,5 kHz, falta); el cristal arranca al momento [de modelo:
// tSTART,LFXT = 0,8-1 s no se modela].
// =============================================================================
#ifndef STM32_PERIPH_TI_CS_FR_H
#define STM32_PERIPH_TI_CS_FR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <systemc>
#include "../../common/verbosidad.h"

namespace stm32 {

class CsFr {
public:
    static constexpr double F_REFO = 32768.0, F_VLO = 10000.0, F_MODOSC = 5e6;

    struct Entradas {
        bool scg0 = false, scg1 = false, oscoff = false, cpuoff = false;
        bool xt1 = false;                    // el cristal oscila (y sus pines están en XIN/XOUT)
        double f_xt1 = 32768.0;
        bool pide_smclk = false, pide_aclk = false;
        bool hfxt = false;                   // FR5: el HFXIN tiene PSEL y la placa, un cristal
        double f_hfxt = 0.0;                 // su frecuencia
        double f_hfxt_ext = 0.0;             // FR5: el reloj declarado en HFXIN (bypass)
        double f_lfxt_ext = 0.0;             // FR5: el de LFXIN (bypass del LFXT)
        bool lfxt_psel = false;              // FR5: el LFXIN tiene PSEL (para el bypass)
    };
    // Las FR5xx/FR6xx (lo pone el SoC antes del primer reset)
    bool fr5 = false;
    static constexpr double F_LFMOD = F_MODOSC / 128.0, F_VLO5 = 9400.0;
    // Lo que sale
    double f_dco = 0, f_dcodiv = 0, f_mclk = 0, f_smclk = 0, f_aclk = 0, f_fllref = 0;
    double f_mclk_fuente = 0;
    double f_smclk_peticion = 0;             // FR5: SMCLK a petición (SMCLKREQEN), aunque esté parado: lo que ve la eUSCI que la pide                // MCLK sin CPUOFF (lo que ve el perro, el ADC...)
    bool xt1_pedido = false;

    void reset() {
        if (fr5) {
            r_[0] = 0; r_[1] = 0x000C; r_[2] = 0x0033; r_[3] = 0x0033; r_[4] = 0xCDC9;
            r_[5] = uint16_t(0x00C0 | (r_[5] & 0x0003) | (por_ ? 0x0001 : 0)); r_[6] = 0x0007;
            por_ = false; abierto_ = false;
            hf_on_ = false; hf_ok_ = false; desbloqueo_ = sc_core::SC_ZERO_TIME;
            return;
        }
        r_[0] = 0; r_[1] = 0x0033; r_[2] = 0x101F; r_[3] = 0; r_[4] = 0x0100; r_[5] = 0x1000;
        r_[6] = 0x00C1; r_[7] = 0x0440; r_[8] = 0x0007;
        tap_ = 256; f_dco = 0; desbloqueo_ = sc_core::SC_ZERO_TIME; unlock_ = 0;
    }
    // El POR (las faltas de los cristales vuelven a 1) [UG FR5 §3.2.8]
    void por() { por_ = true; }
    uint16_t lee(unsigned k) const {
        if (fr5) return k == 0 ? 0x9600 : k < 7 ? r_[k] : 0;
        if (k == 0) return uint16_t((r_[0] & 0x3E00u) | tap_);
        if (k == 7) return uint16_t((r_[7] & ~0x0300u) | (uint16_t(unlock_) << 8) | 0x0004);   // REFOREADY
        return k < 9 ? r_[k] : 0;
    }
    // Devuelve true si cambian los relojes
    bool escribe(unsigned k, uint16_t v, uint16_t m, const Entradas& e) {
        if (fr5) return escribe5(k, v, m, e);
        if (k > 8) return false;
        const uint16_t n = uint16_t((r_[k] & ~m) | (v & m));
        switch (k) {
        case 0: r_[0] = uint16_t(n & 0x3E00u); tap_ = n & 0x1FFu; tap_escrito_ = true; break;
        case 1: r_[1] = uint16_t(n & 0x00FFu); break;
        case 2: r_[2] = uint16_t(n & 0x73FFu); if (!(r_[2] & 0x3FFu)) r_[2] |= 1u; break;
        case 3: r_[3] = uint16_t(n & 0x00B7u); break;
        case 4: r_[4] = uint16_t(n & 0x0307u); break;
        case 5: r_[5] = uint16_t(n & 0x1137u); break;
        case 6: r_[6] = uint16_t(n & 0x2FFFu); break;
        case 7: {
            // FLLUNLOCK es de solo lectura; las faltas y la historia, se borran con 0
            const uint16_t borra = uint16_t(m & ~v);
            r_[7] = uint16_t((r_[7] & ~(borra & 0x0C13u)) | (n & 0x3040u));
            break;
        }
        case 8: r_[8] = uint16_t(n & 0x000Fu); break;
        default: break;
        }
        calcula(e, true);
        return true;
    }
    // Las frecuencias con el estado de ahora; `cambio`: algo del FLL ha
    // cambiado (arranca la ventana de desenganche)
    void calcula(const Entradas& e, bool cambio = false) {
        if (fr5) { calcula5(e); return; }
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        const unsigned selref = (r_[3] >> 4) & 3u, sela = (r_[4] >> 8) & 3u, selms = r_[4] & 7u;
        const bool auto_off = (r_[6] & 0x0001) != 0;
        xt1_pedido = !auto_off || sela == 0 || selms == 2 || (selref == 0 && !e.scg0);
        const double xt1 = e.xt1 ? e.f_xt1 : F_REFO;               // sin XT1, REFO
        static const unsigned RDIV[8] = {1, 32, 64, 128, 256, 512, 640, 768};
        f_fllref = (selref == 0 ? xt1 : F_REFO) / (e.xt1 && selref == 0 ? RDIV[r_[3] & 7u] : 1u);
        const unsigned d = 1u << ((r_[2] >> 12) & 7u), nn = (r_[2] & 0x3FFu) + 1u;
        const double base = nominal();
        if (!e.scg0) {                                              // el FLL manda
            const double obj = double(d) * double(nn) * f_fllref;
            const double t = 256.0 + 256.0 * std::log2(obj / base);
            int nt = int(std::lround(t));
            const unsigned antes = tap_;
            if (nt < 0) nt = 0;
            if (nt > 511) nt = 511;
            if (cambio || tap_escrito_ || unsigned(nt) != antes) {
                if (unsigned(nt) != antes || tap_escrito_) {
                    unlock_dir_ = unsigned(nt) > antes ? 1u : 2u;   // 01 lento: sube; 10 rápido: baja
                    desbloqueo_ = ahora + sc_core::sc_time(32.0 / f_fllref, sc_core::SC_SEC);
                    r_[7] |= uint16_t(unlock_dir_ << 10);           // FLLUNLOCKHIS
                }
            }
            tap_ = unsigned(nt);
            tap_escrito_ = false;
        }
        f_dco = base * std::exp2((double(tap_) - 256.0) / 256.0);
        const bool fuera = tap_ == 0 || tap_ == 511;
        if (fuera) r_[7] |= 0x0001;                                 // DCOFFG
        unlock_ = fuera ? 3u : (!e.scg0 && ahora < desbloqueo_) ? unlock_dir_ : 0u;
        if (unlock_ == 2u) r_[7] |= 0x0010;                         // FLLULIFG (demasiado rápido)
        f_dcodiv = f_dco / double(d);
        // XT1OFFG: pedido y sin cristal
        if (xt1_pedido && !e.xt1) r_[7] |= 0x0002;
        const double fm = selms == 0 ? f_dcodiv : selms == 1 ? F_REFO : selms == 2 ? xt1 : F_VLO;
        f_mclk_fuente = fm / double(1u << (r_[5] & 7u));
        f_mclk = e.cpuoff ? 0.0 : f_mclk_fuente;
        const bool smclk_on = !(r_[5] & 0x0100) && (!e.scg1 || (e.pide_smclk && (r_[8] & 0x04)));
        f_smclk = smclk_on ? f_mclk_fuente / double(1u << ((r_[5] >> 4) & 3u)) : 0.0;
        const double fa = sela == 0 ? xt1 : sela == 1 ? F_REFO : F_VLO;
        f_aclk = (!e.oscoff || (e.pide_aclk && (r_[8] & 0x01))) ? fa : 0.0;
    }
    bool falta() const { return fr5 ? (r_[5] & 0x0003) != 0 : (r_[7] & 0x0003) != 0; }
    bool abierto() const { return abierto_; }
    // El fin del desenganche (para recalcular entonces); en las FR5, el del
    // arranque del HFXT
    sc_core::sc_time fin_desenganche() const { return desbloqueo_; }
    // FR5: ¿da el HFXT su reloj? (para las pruebas)
    bool hfxt_listo() const { return hf_ok_; }
    unsigned grifo() const { return tap_; }
    unsigned unlock() const { return unlock_; }
    bool fllulpuc() const { return !fr5 && (r_[7] & 0x1010) == 0x1010; }
    // FR5: ¿pide alguien el LFXT? (para los pines y el despertar de LPM3.5)
    bool lfxt_pedido() const { return xt1_pedido; }

private:
    uint16_t r_[9] = {};
    bool por_ = true, abierto_ = false;
    bool escribe5(unsigned k, uint16_t v, uint16_t m, const Entradas& e) {
        if (k == 0) {
            if (m & 0xFF00) abierto_ = ((v >> 8) & 0xFFu) == 0xA5u;
            return false;
        }
        if (k > 6 || !abierto_) return false;
        const uint16_t n = uint16_t((r_[k] & ~m) | (v & m));
        switch (k) {
        case 1: r_[1] = uint16_t(n & 0x004Eu); break;
        case 2: r_[2] = uint16_t(n & 0x0777u); break;
        case 3: r_[3] = uint16_t(n & 0x0777u); break;
        case 4: r_[4] = uint16_t(n & 0xDDDBu); break;   // 13, 9, 5 y 2: reservados
        case 5: {
            // las faltas se borran con 0 (y vuelven si siguen)
            const uint16_t borra = uint16_t(m & ~v & 0x0003u);
            r_[5] = uint16_t(((r_[5] & ~borra) & 0x0003u) | (n & 0x00C0u));
            break;
        }
        case 6: r_[6] = uint16_t(n & 0x000Fu); break;
        default: break;
        }
        calcula5(e);
        return true;
    }
    // HFFREQ: ¿cae f en su margen? [DS FR5969 tabla 5-5]
    static bool en_margen_hf(unsigned hffreq, double f, bool bypass) {
        switch (hffreq) {
        case 0: return bypass && f >= 0.9e6 && f <= 4e6;
        case 1: return f > 4e6 - 1 && f <= 8e6;
        case 2: return f > 8e6 && f <= 16e6;
        default: return f > 16e6 && f <= 24e6;
        }
    }
    // El arranque del HFXT [tabla 5-5, de modelo en línea recta]
    static double t_arranque_hf(double f, bool bypass, bool contador) {
        const double cuenta = contador ? 1024.0 / f : 0.0;
        if (bypass) return cuenta;
        const double fm = std::min(24e6, std::max(4e6, f));
        return 1.344e-3 - (fm - 4e6) / 20e6 * (1.344e-3 - 0.557e-3) + cuenta;
    }
    void calcula5(const Entradas& e) {
        static const double D0[8] = {1e6, 2.667e6, 3.5e6, 4e6, 5.333e6, 7e6, 8e6, 8e6};
        static const double D1[8] = {1e6, 5.333e6, 7e6, 8e6, 16e6, 21e6, 24e6, 24e6};
        const unsigned fsel = (r_[1] >> 1) & 7u;
        f_dco = (r_[1] & 0x40) ? D1[fsel] : D0[fsel];
        const unsigned sela = (r_[2] >> 8) & 7u, sels = (r_[2] >> 4) & 7u, selm = r_[2] & 7u;
        const bool lf_pide = !(r_[4] & 0x0001) || sela == 0 || sels == 0 || selm == 0;
        const bool smclk_en = !(r_[4] & 0x0002) && (!e.scg1 || (e.pide_smclk && (r_[6] & 0x04)));
        const bool hf_pide = !(r_[4] & 0x0100) || (selm == 5 && !e.cpuoff) || (sels == 5 && smclk_en);
        xt1_pedido = lf_pide;
        // El LFXT: con bypass, el reloj de LFXIN
        const bool lf_bypass = (r_[4] & 0x0010) != 0;
        const bool lf_ok = lf_bypass ? (e.lfxt_psel && e.f_lfxt_ext >= 3500.0) : e.xt1;
        if (lf_pide && !lf_ok) r_[5] |= 0x0001;                     // LFXTOFFG
        // El HFXT
        const bool hf_bypass = (r_[4] & 0x1000) != 0;
        const unsigned hffreq = (r_[4] >> 10) & 3u;
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        const bool hf_psel = e.hfxt || (hf_bypass && e.f_hfxt_ext > 0.0);
        const bool hf_enc = hf_pide && hf_psel;
        double fhf = 0.0;
        bool puede = false;
        if (hf_enc) {
            fhf = hf_bypass ? e.f_hfxt_ext : e.f_hfxt;
            if (hf_bypass) {
                puede = fhf >= 800e3;
                if (puede && !en_margen_hf(hffreq, fhf, true) && !avisado_hffreq_) {
                    avisado_hffreq_ = true;
                    ::stm32::avisa_consola("[CS] HFXT en bypass a %.3f MHz con HFFREQ = %u: fuera de su margen [DS tabla 5-5]\n", fhf / 1e6, hffreq);
                }
            } else {
                puede = fhf > 0.0 && en_margen_hf(hffreq, fhf, false);
                if (!puede && fhf > 0.0 && !avisado_hffreq_) {
                    avisado_hffreq_ = true;
                    ::stm32::avisa_consola("[CS] cristal del HFXT de %.3f MHz con HFFREQ = %u: no oscila [de modelo; DS tabla 5-5]\n", fhf / 1e6, hffreq);
                }
            }
        }
        if (!hf_enc || !puede) { hf_on_ = false; hf_ok_ = false; }
        else if (!hf_on_ || fhf != hf_f_) {
            hf_on_ = true; hf_ok_ = false; hf_f_ = fhf;
            hf_listo_ = ahora + sc_core::sc_time(t_arranque_hf(fhf, hf_bypass, (r_[5] & 0x80) != 0), sc_core::SC_SEC);
        }
        if (hf_on_ && !hf_ok_ && ahora >= hf_listo_) hf_ok_ = true;
        desbloqueo_ = (hf_on_ && !hf_ok_) ? hf_listo_ : sc_core::SC_ZERO_TIME;
        if (hf_pide && !hf_ok_) r_[5] |= 0x0002;                    // HFXTOFFG
        const double lf = lf_ok ? (lf_bypass ? e.f_lfxt_ext : e.f_xt1) : F_LFMOD, hf = hf_ok_ ? hf_f_ : F_MODOSC;
        auto fuente = [&](unsigned s) {
            switch (s) {
            case 0: return lf;
            case 1: return F_VLO5;
            case 2: return F_LFMOD;
            case 4: return F_MODOSC;
            case 5: return hf;
            default: return f_dco;
            }
        };
        static const unsigned DIV[8] = {1, 2, 4, 8, 16, 32, 32, 32};
        f_dcodiv = f_dco;
        f_fllref = 0;
        f_mclk_fuente = fuente(selm) / DIV[r_[3] & 7u];
        f_mclk = e.cpuoff ? 0.0 : f_mclk_fuente;
        f_smclk = smclk_en ? fuente(sels) / DIV[(r_[3] >> 4) & 7u] : 0.0;
        f_smclk_peticion = (!(r_[4] & 0x0002) && (r_[6] & 0x04)) ? fuente(sels) / DIV[(r_[3] >> 4) & 7u] : 0.0;
        const double fa = fuente(sela > 2 ? 0 : sela) / DIV[(r_[3] >> 8) & 7u];
        f_aclk = (!e.oscoff || (e.pide_aclk && (r_[6] & 0x01))) ? fa : 0.0;
        unlock_ = 0;
    }
    bool hf_on_ = false, hf_ok_ = false, avisado_hffreq_ = false;
    double hf_f_ = 0.0;
    sc_core::sc_time hf_listo_{sc_core::SC_ZERO_TIME};
    unsigned tap_ = 256, unlock_ = 0, unlock_dir_ = 1;
    bool tap_escrito_ = false;
    sc_core::sc_time desbloqueo_{sc_core::SC_ZERO_TIME};
    double nominal() const {
        static const double F[8] = {1e6, 2e6, 4e6, 8e6, 12e6, 16e6, 20e6, 24e6};
        const unsigned rsel = (r_[1] >> 1) & 7u;
        const unsigned trim = (r_[1] & 0x80) ? (r_[1] >> 4) & 7u : 3u;
        return F[rsel] * (1.0 + 0.06 * (double(trim) - 3.0));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_CS_FR_H
