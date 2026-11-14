// =============================================================================
// i2s_esp.h — El I2S de los ESP32-C3, en modo TDM (Philips, MSB y PCM)
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §29; i2s_struct.h, i2s_reg.h e
// i2s_ll.h de ESP-IDF]. Un transmisor y un receptor, con la GDMA (periférico
// 3) y la matriz de GPIO: I2SO_BCK 13, I2SO_WS 14, I2SO_SD 15 (salida) /
// I2SI_SD 15 (entrada), I2SI_BCK 16 e I2SI_WS 17.
//
// EL RELOJ (§29.6): TX/RX_CLKM_CONF elige la fuente (0 XTAL de 40 MHz, 1 PLL
// de 240, 2 PLL de 160) y el divisor N (CLKM_DIV_NUM: 0 es 256, 1 es 2); el
// fraccionario b/a sale de X, Y, Z e YN1 (CLKM_DIV_CONF); BCK = I2S_CLK /
// (BCK_DIV_NUM + 1). CLK_ACTIVE apagado: no hay reloj.
//
// LA TRAMA (§29.9, §29.10): TDM_TOT_CHAN_NUM + 1 canales de TDM_CHAN_BITS + 1
// bits; la trama dura 2 (HALF_SAMPLE_BITS + 1) ciclos de BCK. WS vale
// WS_IDLE_POL los primeros TDM_WS_WIDTH + 1 ciclos y lo contrario el resto.
// Con MSB_SHIFT (Philips y PCM), SD va un ciclo detrás de WS. Los datos
// cambian en el flanco de bajada de BCK y se leen en el de subida.
// Cada canal lleva un dato válido de BITS_MOD + 1 bits (8, 16, 24 o 32; con
// 24 y 24_FILL_EN, 32 en memoria): de la GDMA en el orden de BIG_ENDIAN;
// alineado según LEFT_ALIGN si el canal es más ancho, o sus bits bajos si es
// más estrecho; y BIT_ORDER le da la vuelta. Un canal apagado
// (TDM_CHANn_EN a 0) manda el anterior (CHAN_EQUAL) o CONF_SIGLE_DATA, y en
// recepción no se guarda.
// EL TRANSMISOR, maestro: con TX_START saca BCK, WS y SD. Al quedarse sin
// datos de la GDMA, TX_DONE y, con TX_STOP_EN, se para tras el último bit
// (STATE.TX_IDLE a 1); sin él repite la última trama.
// EL RECEPTOR, maestro o, con SIG_LOOPBACK (TX_CONF bit 27), con el BCK y el
// WS del transmisor: lee SD de la matriz y da los bytes a la GDMA; cada
// RXEOF_NUM bytes, el final de la trama de la GDMA (IN_SUC_EOF) y RX_DONE.
// TX_UPDATE y RX_UPDATE se bajan solos. La interrupción, fuente 20.
//
// LO QUE NO SE MODELA (avisa una vez): el modo esclavo sin SIG_LOOPBACK, PDM
// (y PCM a PDM), la compresión A-law y mu-law, MCLK, los retrasos de
// TX/RX_TIMING, TX_TDM_SKIP_MSK_EN y las interrupciones HUNG.
// =============================================================================
#ifndef STM32_PERIPH_ESP_I2S_ESP_H
#define STM32_PERIPH_ESP_I2S_ESP_H

#include <cstdint>
#include <cstring>
#include <functional>
#include "../../common/periph_base.h"
#include "gdma_esp.h"

namespace stm32 {

class I2sEsp : public BusSlave {
public:
    enum : unsigned { S_O_BCK = 13, S_O_WS = 14, S_SD = 15, S_I_BCK = 16, S_I_WS = 17, PERI_GDMA = 3 };
    std::function<void(bool)> irq;
    std::function<void(unsigned, bool)> pon;       // una señal de salida de la matriz
    std::function<bool(unsigned)> lee;             // una de entrada
    GdmaEsp* gdma = nullptr;
    double hz_xtal = 40e6;

    explicit I2sEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(I2sEsp);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        r_[0x20 / 4] = 0x9600u; r_[0x24 / 4] = 0xB200u;
        r_[0x28 / 4] = r_[0x2C / 4] = 0x2F3DE300u;
        r_[0x30 / 4] = r_[0x34 / 4] = 0x2u;
        r_[0x38 / 4] = r_[0x3C / 4] = 0x200u;
        r_[0x40 / 4] = 0x4AA004u; r_[0x44 / 4] = 0x3F783C0u;
        r_[0x50 / 4] = r_[0x54 / 4] = 0xFFFFu;
        r_[0x60 / 4] = 0x810u; r_[0x64 / 4] = 0x40u; r_[0x80 / 4] = 0x2003230u;
        raw_ = 0;
        tx_idle_ = true;
        rx_bytes_ = 0;
        std::memset(tx_ultima_, 0, sizeof tx_ultima_);
        if (pon) for (unsigned s : {S_O_BCK, S_O_WS, S_SD, S_I_BCK, S_I_WS}) pon(s, false);
        evalua();
        ev_tx_.notify(sc_core::SC_ZERO_TIME);
        ev_rx_.notify(sc_core::SC_ZERO_TIME);
    }
    unsigned muestras_tx() const { return n_tx_; }
    unsigned muestras_rx() const { return n_rx_; }
    // El BCK del transmisor o del receptor [Hz] (0: sin reloj)
    double hz_bck(bool rx) const {
        const uint32_t clkm = r_[(rx ? 0x30 : 0x34) / 4], div = r_[(rx ? 0x38 : 0x3C) / 4];
        if (!((clkm >> 26) & 1u)) return 0;
        const unsigned sel = (clkm >> 27) & 3u;
        const double src = sel == 0 ? hz_xtal : sel == 1 ? 240e6 : sel == 2 ? 160e6 : 0;
        const unsigned dn = clkm & 0xFFu;
        const double n = dn == 0 ? 256 : dn == 1 ? 2 : dn;
        const unsigned z = div & 0x1FFu, y = (div >> 9) & 0x1FFu, x = (div >> 18) & 0x1FFu, yn1 = (div >> 27) & 1u;
        double frac = 0;
        if (z) {
            const double a = double(x + 1) * z + y;
            frac = yn1 ? (a - z) / a : z / a;
        }
        const unsigned bck = (r_[(rx ? 0x28 : 0x2C) / 4] >> 7) & 0x3Fu;
        return src / (n + frac) / double(bck + 1);
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x0C: return raw_;
        case 0x10: return raw_ & r_[0x14 / 4];
        case 0x6C: return tx_idle_ ? 1u : 0u;
        default: return off < 0x100 ? r_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x0C: case 0x10: case 0x6C: return;
        case 0x18: raw_ &= ~v; evalua(); return;
        case 0x14: r_[off / 4] = v; evalua(); return;
        case 0x20: {
            if (v & 3u) { rx_bytes_ = 0; }                     // RX_RESET, RX_FIFO_RESET
            r_[off / 4] = v & ~(3u | (1u << 8));                // los reset y UPDATE se bajan
            if (((v >> 2) & 1u) && ((v >> 3) & 1u) && !loopback()) avisa("el receptor esclavo sin SIG_LOOPBACK");
            if ((v >> 20) & 1u) avisa("PDM en recepcion");
            if (!((v >> 12) & 1u)) avisa("la compresion A-law/mu-law en recepcion");
            ev_rx_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        case 0x24: {
            r_[off / 4] = v & ~(3u | (1u << 8));
            if ((v >> 3) & 1u) avisa("el transmisor esclavo");
            if ((v >> 20) & 1u) avisa("PDM en transmision");
            if (!((v >> 12) & 1u)) avisa("la compresion A-law/mu-law en transmision");
            ev_tx_.notify(sc_core::SC_ZERO_TIME);
            ev_rx_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        default:
            if (off < 0x100) r_[off / 4] = v;
            ev_tx_.notify(sc_core::SC_ZERO_TIME);
            ev_rx_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
    }

private:
    uint32_t r_[64] = {};
    uint32_t raw_ = 0;
    bool irq_ = false, tx_idle_ = true;
    uint32_t tx_ultima_[16];
    unsigned rx_bytes_ = 0, n_tx_ = 0, n_rx_ = 0;
    unsigned avisos_ = 0;
    sc_core::sc_event ev_tx_, ev_rx_;
    // El receptor en SIG_LOOPBACK: lo que ve en cada flanco de subida del BCK
    // del transmisor
    struct Rx { bool on = false, previa = false; uint32_t acum = 0; } lb_;

    void avisa(const char* que) {
        SC_REPORT_WARNING("i2s", (std::string(que) + ": no se modela").c_str());
        ++avisos_;
    }
    void evalua() {
        const bool n = (raw_ & r_[0x14 / 4]) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    bool loopback() const { return (r_[0x24 / 4] >> 27) & 1u; }
    void out(unsigned s, bool v) { if (pon) pon(s, v); }

    // Lo común de la trama: (conf, conf1, tdm_ctrl)
    struct Trama {
        unsigned canales, bits_canal, ciclos, ws_ancho, bits_dato, bytes_dato;
        bool ws_pol, msb_shift, big, izq, orden, igual;
        uint32_t chan_en;
    };
    Trama trama(bool rx) const {
        const uint32_t c = r_[(rx ? 0x20 : 0x24) / 4], c1 = r_[(rx ? 0x28 : 0x2C) / 4], td = r_[(rx ? 0x50 : 0x54) / 4];
        Trama t;
        t.canales = ((td >> 16) & 0xFu) + 1u;
        t.chan_en = td & 0xFFFFu;
        t.bits_canal = ((c1 >> 24) & 0x1Fu) + 1u;
        t.ciclos = 2u * (((c1 >> 18) & 0x3Fu) + 1u);
        t.ws_ancho = (c1 & 0x7Fu) + 1u;
        t.bits_dato = ((c1 >> 13) & 0x1Fu) + 1u;
        const bool fill = (c >> 16) & 1u;
        t.bytes_dato = t.bits_dato <= 8 ? 1 : t.bits_dato <= 16 ? 2 : (t.bits_dato <= 24 && !fill) ? 3 : 4;
        t.ws_pol = (c >> 17) & 1u;
        t.msb_shift = (c1 >> 29) & 1u;
        t.big = (c >> 7) & 1u;
        t.izq = (c >> 15) & 1u;
        t.orden = (c >> 18) & 1u;
        t.igual = !rx && ((c >> 6) & 1u);
        return t;
    }
    static uint32_t invierte(uint32_t v, unsigned n) {
        uint32_t r = 0;
        for (unsigned i = 0; i < n; ++i) if ((v >> i) & 1u) r |= 1u << (n - 1 - i);
        return r;
    }
    // Un dato de la GDMA (false si no hay)
    bool dato_tx(const Trama& t, uint32_t& v) {
        uint8_t b[4] = {};
        if (!gdma || gdma->saca(PERI_GDMA, b, t.bytes_dato) != t.bytes_dato) return false;
        v = 0;
        for (unsigned i = 0; i < t.bytes_dato; ++i)
            v |= uint32_t(b[t.big ? t.bytes_dato - 1 - i : i]) << (8 * i);
        if (t.bits_dato < 32) v &= (1u << t.bits_dato) - 1u;
        return true;
    }
    // Los bits que salen por el canal (MSB primero), de bits_canal
    uint32_t a_canal(const Trama& t, uint32_t v) const {
        uint32_t w;
        if (t.bits_canal >= t.bits_dato) w = t.izq ? v << (t.bits_canal - t.bits_dato) : v;
        else w = v;                                        // los bits bajos
        if (t.bits_canal < 32) w &= (1u << t.bits_canal) - 1u;
        return t.orden ? invierte(w, t.bits_canal) : w;
    }

    // EL TRANSMISOR
    void tx_proc() {
        for (;;) {
            sc_core::wait(ev_tx_);
            const bool start = (r_[0x24 / 4] >> 2) & 1u;
            const double hz = hz_bck(false);
            if (!start || hz <= 0 || ((r_[0x24 / 4] >> 3) & 1u)) continue;
            tx_idle_ = false;
            const sc_core::sc_time m(0.5 / hz, sc_core::SC_SEC);
            bool prev_sd = false, sin_datos = false;
            while ((r_[0x24 / 4] >> 2) & 1u) {
                const Trama t = trama(false);
                // La trama: los canales en bits, MSB primero
                uint32_t canal[16];
                for (unsigned k = 0; k < t.canales; ++k) {
                    uint32_t v = 0;
                    if ((t.chan_en >> k) & 1u) {
                        if (dato_tx(t, v)) { tx_ultima_[k] = v; ++n_tx_; }
                        else { sin_datos = true; v = tx_ultima_[k]; }
                    } else {
                        v = (t.igual && k) ? tx_ultima_[k - 1] : r_[0x68 / 4];
                        if (t.bits_dato < 32) v &= (1u << t.bits_dato) - 1u;
                        tx_ultima_[k] = v;
                    }
                    canal[k] = a_canal(t, v);
                }
                if (sin_datos) {
                    raw_ |= 2u;                                     // TX_DONE
                    evalua();
                    if ((r_[0x24 / 4] >> 13) & 1u) {               // TX_STOP_EN
                        // Con MSB_SHIFT, el último bit va en un ciclo más
                        if (t.msb_shift) {
                            out(S_O_BCK, false);
                            out(S_O_WS, t.ws_pol);
                            out(S_SD, prev_sd);
                            sc_core::wait(m);
                            out(S_O_BCK, true);
                            if (loopback()) rx_flanco(0);
                            sc_core::wait(m);
                        }
                        break;
                    }
                    sin_datos = false;
                }
                for (unsigned cy = 0; cy < t.ciclos; ++cy) {
                    // El bit del flujo que toca: con MSB_SHIFT, uno detrás
                    const int n = int(cy) - (t.msb_shift ? 1 : 0);
                    bool sd = prev_sd;
                    if (n >= 0) {
                        const unsigned k = unsigned(n) / t.bits_canal, i = unsigned(n) % t.bits_canal;
                        sd = k < t.canales ? ((canal[k] >> (t.bits_canal - 1 - i)) & 1u) : false;
                    }
                    const bool ws = cy < t.ws_ancho ? t.ws_pol : !t.ws_pol;
                    out(S_O_BCK, false);
                    out(S_O_WS, ws);
                    out(S_SD, sd);
                    prev_sd = sd;
                    sc_core::wait(m);
                    out(S_O_BCK, true);
                    if (loopback()) rx_flanco(cy);
                    sc_core::wait(m);
                }
                // El último bit de la trama, con MSB_SHIFT, va al principio de
                // la siguiente
                if (t.msb_shift) {
                    const unsigned n = t.ciclos - 1, k = n / t.bits_canal, i = n % t.bits_canal;
                    prev_sd = k < t.canales ? ((canal[k] >> (t.bits_canal - 1 - i)) & 1u) : false;
                }
            }
            out(S_O_BCK, false);
            tx_idle_ = true;
        }
    }

    // EL RECEPTOR: un flanco de subida de BCK, en el ciclo `cy` de la trama
    // (lo da quien genera BCK y WS: el transmisor en SIG_LOOPBACK o el propio
    // receptor maestro). Empieza con una trama entera.
    void rx_flanco(unsigned cy) {
        if (!((r_[0x20 / 4] >> 2) & 1u)) { lb_.on = false; return; }
        const Trama t = trama(true);
        const bool sd = lee ? lee(S_SD) : false;
        if (!lb_.on) {
            if (cy != 0) return;
            lb_ = Rx{}; lb_.on = true;
        }
        // Con MSB_SHIFT, el ciclo 0 lleva el último bit de la trama anterior
        int n = int(cy) - (t.msb_shift ? 1 : 0);
        if (n < 0) {
            if (!lb_.previa) return;
            n = int(t.ciclos) - 1;
        }
        if (cy + 1 == t.ciclos) lb_.previa = true;
        const unsigned k = unsigned(n) / t.bits_canal, i = unsigned(n) % t.bits_canal;
        if (k >= t.canales) return;
        lb_.acum = (lb_.acum << 1) | (sd ? 1u : 0u);
        if (i + 1 < t.bits_canal) return;
        // Un canal entero
        uint32_t w = t.orden ? invierte(lb_.acum, t.bits_canal) : lb_.acum;
        if (t.bits_canal < 32) w &= (1u << t.bits_canal) - 1u;
        lb_.acum = 0;
        if (!((t.chan_en >> k) & 1u)) return;
        uint32_t v;
        if (t.bits_canal > t.bits_dato) v = t.izq ? (w >> (t.bits_canal - t.bits_dato)) : w;
        else v = w;
        if (t.bits_dato < 32) v &= (1u << t.bits_dato) - 1u;
        uint8_t b[4];
        for (unsigned j = 0; j < t.bytes_dato; ++j) {
            const uint8_t x = uint8_t(v >> (8 * j));
            b[t.big ? t.bytes_dato - 1 - j : j] = x;
        }
        ++n_rx_;
        const unsigned eofn = r_[0x64 / 4] & 0xFFFu;
        unsigned dado = 0;
        while (dado < t.bytes_dato) {
            const unsigned falta = eofn ? eofn - rx_bytes_ : t.bytes_dato;
            const unsigned q = std::min(t.bytes_dato - dado, falta ? falta : t.bytes_dato - dado);
            const bool eof = eofn && rx_bytes_ + q >= eofn;
            if (gdma) gdma->mete(PERI_GDMA, b + dado, q, eof);
            dado += q;
            rx_bytes_ += q;
            if (eof) {
                rx_bytes_ = 0;
                raw_ |= 1u;                                         // RX_DONE
                evalua();
            }
        }
    }

    // EL RECEPTOR MAESTRO (sin SIG_LOOPBACK): su propio BCK y WS
    void rx_proc() {
        for (;;) {
            sc_core::wait(ev_rx_);
            const uint32_t c = r_[0x20 / 4];
            const double hz = hz_bck(true);
            if (!((c >> 2) & 1u) || ((c >> 3) & 1u) || loopback() || hz <= 0) continue;
            const sc_core::sc_time m(0.5 / hz, sc_core::SC_SEC);
            while (((r_[0x20 / 4] >> 2) & 1u) && !loopback() && !((r_[0x20 / 4] >> 3) & 1u)) {
                const Trama t = trama(true);
                for (unsigned cy = 0; cy < t.ciclos && ((r_[0x20 / 4] >> 2) & 1u); ++cy) {
                    const bool ws = cy < t.ws_ancho ? t.ws_pol : !t.ws_pol;
                    out(S_I_BCK, false);
                    out(S_I_WS, ws);
                    sc_core::wait(m);
                    out(S_I_BCK, true);
                    rx_flanco(cy);
                    sc_core::wait(m);
                }
            }
            out(S_I_BCK, false);
            lb_.on = false;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_I2S_ESP_H
