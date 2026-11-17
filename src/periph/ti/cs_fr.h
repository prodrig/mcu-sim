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
// =============================================================================
#ifndef STM32_PERIPH_TI_CS_FR_H
#define STM32_PERIPH_TI_CS_FR_H

#include <cmath>
#include <cstdint>
#include <systemc>

namespace stm32 {

class CsFr {
public:
    static constexpr double F_REFO = 32768.0, F_VLO = 10000.0, F_MODOSC = 5e6;

    struct Entradas {
        bool scg0 = false, scg1 = false, oscoff = false, cpuoff = false;
        bool xt1 = false;                    // el cristal oscila (y sus pines están en XIN/XOUT)
        double f_xt1 = 32768.0;
        bool pide_smclk = false, pide_aclk = false;
    };
    // Lo que sale
    double f_dco = 0, f_dcodiv = 0, f_mclk = 0, f_smclk = 0, f_aclk = 0, f_fllref = 0;
    double f_mclk_fuente = 0;                // MCLK sin CPUOFF (lo que ve el perro, el ADC...)
    bool xt1_pedido = false;

    void reset() {
        r_[0] = 0; r_[1] = 0x0033; r_[2] = 0x101F; r_[3] = 0; r_[4] = 0x0100; r_[5] = 0x1000;
        r_[6] = 0x00C1; r_[7] = 0x0440; r_[8] = 0x0007;
        tap_ = 256; f_dco = 0; desbloqueo_ = sc_core::SC_ZERO_TIME; unlock_ = 0;
    }
    uint16_t lee(unsigned k) const {
        if (k == 0) return uint16_t((r_[0] & 0x3E00u) | tap_);
        if (k == 7) return uint16_t((r_[7] & ~0x0300u) | (uint16_t(unlock_) << 8) | 0x0004);   // REFOREADY
        return k < 9 ? r_[k] : 0;
    }
    // Devuelve true si cambian los relojes
    bool escribe(unsigned k, uint16_t v, uint16_t m, const Entradas& e) {
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
    bool falta() const { return (r_[7] & 0x0003) != 0; }
    // El fin del desenganche (para recalcular entonces)
    sc_core::sc_time fin_desenganche() const { return desbloqueo_; }
    unsigned grifo() const { return tap_; }
    unsigned unlock() const { return unlock_; }
    bool fllulpuc() const { return (r_[7] & 0x1010) == 0x1010; }

private:
    uint16_t r_[9] = {};
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
