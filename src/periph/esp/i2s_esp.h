// =============================================================================
// i2s_esp.h — El I2S de los ESP32-C3: TDM (Philips, MSB y PCM) y PDM, maestro
//             y esclavo
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §29; i2s_struct.h, i2s_reg.h,
// i2s_ll.h e i2s_hal.c de ESP-IDF; ITU-T G.711 para A-law y mu-law]. Un
// transmisor y un receptor, con la GDMA (periférico 3) y la matriz de GPIO:
// I2S_MCLK 12, I2SO_BCK 13, I2SO_WS 14, I2SO_SD 15 (salida) / I2SI_SD 15
// (entrada), I2SI_BCK 16, I2SI_WS 17 e I2SO_SD1 59; las de reloj, de salida
// en maestro y de entrada en esclavo.
//
// EL RELOJ (§29.6): TX/RX_CLKM_CONF elige la fuente (0 XTAL de 40 MHz, 1 PLL
// de 240, 2 PLL de 160, 3 I2S_MCLK_in, cuya frecuencia se mide flanco a
// flanco) y el divisor N (CLKM_DIV_NUM: 0 es 256, 1 es 2); el fraccionario b/a
// sale de X, Y, Z e YN1 (CLKM_DIV_CONF); BCK = I2S_CLK / (BCK_DIV_NUM + 1).
// CLK_ACTIVE apagado: no hay reloj. I2S_MCLK_out es el I2S_CLK del TX o, con
// MCLK_SEL, del RX; solo se genera si la matriz lo saca por algún pin.
//
// LA TRAMA TDM (§29.9, §29.10): TDM_TOT_CHAN_NUM + 1 canales de
// TDM_CHAN_BITS + 1 bits; la trama dura 2 (HALF_SAMPLE_BITS + 1) ciclos de
// BCK. WS vale WS_IDLE_POL los primeros TDM_WS_WIDTH + 1 ciclos y lo
// contrario el resto. Con MSB_SHIFT (Philips y PCM), SD va un ciclo detrás de
// WS. Los datos cambian en el flanco de bajada de BCK y se leen en el de
// subida. Cada canal lleva un dato válido de BITS_MOD + 1 bits (8, 16, 24 o
// 32; con 24 y 24_FILL_EN, 32 en memoria): de la GDMA en el orden de
// BIG_ENDIAN; alineado según LEFT_ALIGN si el canal es más ancho, o sus bits
// bajos si es más estrecho; y BIT_ORDER le da la vuelta. Un canal apagado
// (TDM_CHANn_EN a 0) manda el anterior (CHAN_EQUAL) o CONF_SIGLE_DATA, y en
// recepción no se guarda; con TX_TDM_SKIP_MSK_EN, la memoria trae también su
// dato, que se lee y se tira.
// A-LAW Y MU-LAW (PCM_BYPASS a 0; PCM_CONF 0 expande A-law, 1 comprime A-law,
// 2 expande mu-law, 3 comprime mu-law): G.711 sobre el dato válido, al salir
// de la memoria en TX y al ir a ella en RX. Comprimir toma el dato como una
// muestra de 16 bits con signo (si es más ancho, sus 16 bits altos; si es más
// estrecho, desplazado) y da el código de 8 bits; expandir toma el código de
// los 8 bits bajos y da la muestra de 16 bits; lo que sobra, ceros.
//
// EL TRANSMISOR. Maestro: con TX_START saca BCK, WS y SD. Al quedarse sin
// datos de la GDMA, TX_DONE y, con TX_STOP_EN, se para tras el último bit
// (STATE.TX_IDLE a 1); sin él repite la última trama. Esclavo (TX_SLAVE_MOD):
// toma BCK y WS de I2SO_BCK_in/WS_in, se sincroniza con el flanco de WS que
// empieza la trama (WS pasa a WS_IDLE_POL) y cambia SD en las bajadas de BCK;
// sin datos y con TX_STOP_EN, manda ceros.
// EL RECEPTOR. Maestro (sus BCK y WS por I2SI_BCK/WS_out), esclavo
// (RX_SLAVE_MOD: de I2SI_BCK/WS_in) o, con SIG_LOOPBACK, con el BCK y el WS
// del transmisor, sea maestro o esclavo. Lee I2SI_SD en la subida y da los
// bytes a la GDMA; cada RXEOF_NUM bytes, el final de la trama de la GDMA
// (IN_SUC_EOF) y RX_DONE. RX_STOP_MODE: 1 para tras un EOF y 2 cuando la
// GDMA no toma más (hasta volver a escribir RX_START).
//
// PDM (TX/RX_PDM_EN, §29.9.2.2 y §29.10.1.2): WS cambia con cada ciclo de BCK
// (va a la mitad); en la mitad con WS = WS_IDLE_POL va el canal izquierdo y
// en la otra el derecho, un bit por mitad. PDM en crudo: los bits salen de
// las palabras de la memoria, del bit alto al bajo; en estéreo, una palabra
// para cada canal; con TX_MONO, una para los dos; TX_CHAN_MOD y WS_IDLE_POL
// eligen qué va en cada mitad (tabla 29.9-4, «single» es CONF_SIGLE_DATA).
// PCM a PDM (PCM2PDM_CONV_EN): las muestras PCM con signo (en estéreo, una
// palabra de 32 bits: el izquierdo en los 16 bajos), una cada
// TX_PDM_SINC_OSR2 x 32 periodos de WS, pasan por un modulador sigma-delta
// de segundo orden; con TX_PDM_DAC_MODE_EN, un canal por periodo de WS (y,
// con DAC_2OUT_EN, el derecho por I2SO_SD1). En recepción, los bits de cada
// canal se juntan en palabras de BITS_MOD + 1 (izquierda y luego derecha,
// según TDM_PDM_CHAN0/1_EN).
//
// LOS RETRASOS (TX/RX_TIMING): cada señal, sin retraso (0), con un periodo
// de I2S_CLK (1, «por el flanco de subida») o con medio (2, «por el de
// bajada»). En las entradas, lo que se lee es el valor de entonces.
// HUNG (LC_HUNG_CONF, con LC_FIFO_TIMEOUT_ENA): LC_FIFO_TIMEOUT <<
// LC_FIFO_TIMEOUT_SHIFT periodos de I2S_CLK sin BCK en esclavo (TX_HUNG,
// RX_HUNG) o, en recepción, con la GDMA sin tomar los datos (RX_HUNG).
// La interrupción, fuente 20 (RX_DONE 0, TX_DONE 1, RX_HUNG 2, TX_HUNG 3).
// TX_UPDATE y RX_UPDATE se bajan solos. DATE 0x2003230.
//
// LO QUE ES DEL MODELO: el filtro del PCM a PDM (el paso alto, el sinc, las
// escalas, PRESCALE, FS/FP y el tramado) no está; el modulador es un
// sigma-delta de segundo orden con ganancia 1, de modo que la densidad de
// unos es (1 + x) / 2 con x la muestra entre -1 y 1. La unidad de HUNG y lo
// que valen 1 y 2 en los retrasos, también.
// =============================================================================
#ifndef STM32_PERIPH_ESP_I2S_ESP_H
#define STM32_PERIPH_ESP_I2S_ESP_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include "../../common/periph_base.h"
#include "gdma_esp.h"

namespace stm32 {

// G.711, como el g711.c de referencia (Sun, dominio público)
namespace g711 {
inline uint8_t lin2alaw(int16_t pcm) {
    static const int16_t seg[8] = {0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF};
    int v = pcm >> 3;
    uint8_t mask;
    if (v >= 0) mask = 0xD5;
    else { mask = 0x55; v = -v - 1; }
    int s = 0;
    while (s < 8 && v > seg[s]) ++s;
    if (s >= 8) return uint8_t(0x7F ^ mask);
    uint8_t a = uint8_t(s << 4);
    a |= (s < 2) ? ((v >> 1) & 0xF) : ((v >> s) & 0xF);
    return uint8_t(a ^ mask);
}
inline int16_t alaw2lin(uint8_t a) {
    a ^= 0x55;
    int t = (a & 0xF) << 4;
    const int s = (a & 0x70) >> 4;
    if (s == 0) t += 8;
    else if (s == 1) t += 0x108;
    else { t += 0x108; t <<= s - 1; }
    return int16_t((a & 0x80) ? t : -t);
}
inline uint8_t lin2ulaw(int16_t pcm) {
    static const int16_t seg[8] = {0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF};
    int v = pcm >> 2;
    uint8_t mask;
    if (v < 0) { v = -v; mask = 0x7F; }
    else mask = 0xFF;
    if (v > 8159) v = 8159;
    v += 0x84 >> 2;
    int s = 0;
    while (s < 8 && v > seg[s]) ++s;
    if (s >= 8) return uint8_t(0x7F ^ mask);
    return uint8_t(((s << 4) | ((v >> (s + 1)) & 0xF)) ^ mask);
}
inline int16_t ulaw2lin(uint8_t u) {
    u = uint8_t(~u);
    int t = ((u & 0xF) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return int16_t((u & 0x80) ? (0x84 - t) : (t - 0x84));
}
} // namespace g711

class I2sEsp : public BusSlave {
public:
    enum : unsigned { S_MCLK = 12, S_O_BCK = 13, S_O_WS = 14, S_SD = 15, S_I_BCK = 16, S_I_WS = 17, S_SD1 = 59, PERI_GDMA = 3 };
    std::function<void(bool)> irq;
    std::function<void(unsigned, bool)> pon;       // una señal de salida de la matriz
    std::function<bool(unsigned)> lee;             // una de entrada
    std::function<const sc_core::sc_event&(unsigned)> ev_in;   // su cambio
    std::function<bool(unsigned)> usada;           // ¿sale la señal por algún pin?
    std::function<const sc_core::sc_event&()> ev_matriz;       // cambia la matriz
    std::function<void(unsigned, bool)> oe;        // la habilitación de salida de una señal
    GdmaEsp* gdma = nullptr;
    double hz_xtal = 40e6;

    explicit I2sEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(I2sEsp);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        SC_THREAD(tx_esclavo_proc);
        SC_THREAD(rx_esclavo_proc);
        SC_THREAD(mclk_proc);
        SC_THREAD(mclk_in_proc);
        SC_METHOD(hist_proc);
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
        rx_parado_ = false;
        atasco_ = false;
        std::memset(tx_ultima_, 0, sizeof tx_ultima_);
        pdm_ = PdmTx{};
        rxp_ = PdmRx{};
        if (pon) for (unsigned s : {S_MCLK, S_O_BCK, S_O_WS, S_SD, S_I_BCK, S_I_WS, S_SD1}) pon(s, false);
        pon_oe();
        evalua();
        ev_tx_.notify(sc_core::SC_ZERO_TIME);
        ev_rx_.notify(sc_core::SC_ZERO_TIME);
    }
    unsigned muestras_tx() const { return n_tx_; }
    unsigned muestras_rx() const { return n_rx_; }
    // El I2S_CLK del transmisor o del receptor [Hz] (0: sin reloj)
    double hz_clk(bool rx) const {
        const uint32_t clkm = r_[(rx ? 0x30 : 0x34) / 4], div = r_[(rx ? 0x38 : 0x3C) / 4];
        if (!((clkm >> 26) & 1u)) return 0;
        const unsigned sel = (clkm >> 27) & 3u;
        const double src = sel == 0 ? hz_xtal : sel == 1 ? 240e6 : sel == 2 ? 160e6 : hz_mclk_in_;
        const unsigned dn = clkm & 0xFFu;
        const double n = dn == 0 ? 256 : dn == 1 ? 2 : dn;
        const unsigned z = div & 0x1FFu, y = (div >> 9) & 0x1FFu, x = (div >> 18) & 0x1FFu, yn1 = (div >> 27) & 1u;
        double frac = 0;
        if (z) {
            const double a = double(x + 1) * z + y;
            frac = yn1 ? (a - z) / a : z / a;
        }
        return src / (n + frac);
    }
    // El BCK del transmisor o del receptor [Hz] (0: sin reloj)
    double hz_bck(bool rx) const {
        const unsigned bck = (r_[(rx ? 0x28 : 0x2C) / 4] >> 7) & 0x3Fu;
        return hz_clk(rx) / double(bck + 1);
    }
    double hz_mclk_entrada() const { return hz_mclk_in_; }

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
            if (v & 3u) { rx_bytes_ = 0; rxp_ = PdmRx{}; lb_ = Rx{}; }   // RX_RESET, RX_FIFO_RESET
            if (((v >> 2) & 1u) && !((r_[off / 4] >> 2) & 1u)) { rx_parado_ = false; atasco_ = false; lb_ = Rx{}; }
            r_[off / 4] = v & ~(3u | (1u << 8));                // los reset y UPDATE se bajan
            pon_oe();
            ev_rx_.notify(sc_core::SC_ZERO_TIME);
            ev_cfg_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        case 0x24: {
            if (v & 3u) pdm_ = PdmTx{};
            r_[off / 4] = v & ~(3u | (1u << 8));
            pon_oe();
            ev_tx_.notify(sc_core::SC_ZERO_TIME);
            ev_rx_.notify(sc_core::SC_ZERO_TIME);
            ev_cfg_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        default:
            if (off < 0x100) r_[off / 4] = v;
            if (off == 0x34) pon_oe();
            ev_tx_.notify(sc_core::SC_ZERO_TIME);
            ev_rx_.notify(sc_core::SC_ZERO_TIME);
            ev_cfg_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
    }

private:
    uint32_t r_[64] = {};
    uint32_t raw_ = 0;
    bool irq_ = false, tx_idle_ = true;
    uint32_t tx_ultima_[16];
    unsigned rx_bytes_ = 0, n_tx_ = 0, n_rx_ = 0;
    bool rx_parado_ = false, atasco_ = false;
    sc_core::sc_time t_atasco_;
    double hz_mclk_in_ = 0;
    sc_core::sc_event ev_tx_, ev_rx_, ev_cfg_, ev_nunca_;
    // El receptor TDM: lo que ve en cada flanco de subida de BCK
    struct Rx { bool on = false, previa = false; uint32_t acum = 0; } lb_;
    // Las entradas, con su valor anterior, para los retrasos
    struct Hist { bool cur = false, prev = false; sc_core::sc_time t; };
    Hist hist_[3];                                           // I2SO_WS, I2SI_SD, I2SI_WS
    // PDM en transmisión: los dos flujos y el modulador
    struct PdmTx {
        uint32_t pal[2] = {}; unsigned quedan[2] = {};
        double x[2] = {}, i1[2] = {}, i2[2] = {};
        unsigned periodo = 0;
        bool hay = false;
    } pdm_;
    // PDM en recepción: las dos palabras a medias
    struct PdmRx { uint32_t acum[2] = {}; unsigned n[2] = {}; } rxp_;

    // Las señales de reloj salen en maestro y entran en esclavo
    void pon_oe() {
        if (!oe) return;
        oe(S_O_BCK, !tx_esclavo()); oe(S_O_WS, !tx_esclavo());
        oe(S_I_BCK, !rx_esclavo()); oe(S_I_WS, !rx_esclavo());
        oe(S_MCLK, ((r_[0x34 / 4] >> 27) & 3u) != 3u);
        oe(S_SD, true); oe(S_SD1, true);
    }
    void evalua() {
        const bool n = (raw_ & r_[0x14 / 4]) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    bool loopback() const { return (r_[0x24 / 4] >> 27) & 1u; }
    bool tx_esclavo() const { return (r_[0x24 / 4] >> 3) & 1u; }
    bool rx_esclavo() const { return (r_[0x20 / 4] >> 3) & 1u; }
    bool tx_pdm() const { return (r_[0x24 / 4] >> 20) & 1u; }
    bool rx_pdm() const { return (r_[0x20 / 4] >> 20) & 1u; }
    void out(unsigned s, bool v) { if (pon) pon(s, v); }
    const sc_core::sc_event& entrada(unsigned s) { return ev_in ? ev_in(s) : ev_nunca_; }

    // --- Los retrasos (TIMING) ---------------------------------------------
    sc_core::sc_time retraso(bool rx, unsigned bit) const {
        const unsigned dm = (r_[(rx ? 0x58 : 0x5C) / 4] >> bit) & 3u;
        const double hz = hz_clk(rx);
        if (!dm || dm == 3 || hz <= 0) return sc_core::SC_ZERO_TIME;
        return sc_core::sc_time((dm == 1 ? 1.0 : 0.5) / hz, sc_core::SC_SEC);
    }
    struct Esc { unsigned s; bool v; sc_core::sc_time d; };
    // Escribe las señales en el orden de su retraso; devuelve lo que ha esperado
    sc_core::sc_time escribe(Esc* e, unsigned n) {
        std::stable_sort(e, e + n, [](const Esc& a, const Esc& b) { return a.d < b.d; });
        sc_core::sc_time hecho = sc_core::SC_ZERO_TIME;
        for (unsigned i = 0; i < n; ++i) {
            if (e[i].d > hecho) { sc_core::wait(e[i].d - hecho); hecho = e[i].d; }
            out(e[i].s, e[i].v);
        }
        return hecho;
    }
    void espera_resto(const sc_core::sc_time& m, const sc_core::sc_time& hecho) {
        if (m > hecho) sc_core::wait(m - hecho);
    }
    unsigned idx_hist(unsigned s) const { return s == S_O_WS ? 0u : s == S_SD ? 1u : 2u; }
    // El valor de una entrada hace `d`
    bool lee_retrasada(unsigned s, const sc_core::sc_time& d) {
        const bool v = lee ? lee(s) : false;
        if (d == sc_core::SC_ZERO_TIME || !ev_in) return v;
        const Hist& h = hist_[idx_hist(s)];
        return (sc_core::sc_time_stamp() - h.t < d) ? h.prev : v;
    }
    void hist_proc() {
        if (!ev_in || !lee) { next_trigger(ev_cfg_); return; }
        for (unsigned s : {S_O_WS, S_SD, S_I_WS}) {
            Hist& h = hist_[idx_hist(s)];
            const bool v = lee(s);
            if (v != h.cur) { h.prev = h.cur; h.cur = v; h.t = sc_core::sc_time_stamp(); }
        }
        next_trigger(ev_in(S_O_WS) | ev_in(S_SD) | ev_in(S_I_WS));
    }

    // --- La trama ----------------------------------------------------------
    struct Trama {
        unsigned canales, bits_canal, ciclos, ws_ancho, bits_dato, bytes_dato;
        bool ws_pol, msb_shift, big, izq, orden, igual, salta;
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
        t.salta = !rx && ((td >> 20) & 1u);
        return t;
    }
    static uint32_t invierte(uint32_t v, unsigned n) {
        uint32_t r = 0;
        for (unsigned i = 0; i < n; ++i) if ((v >> i) & 1u) r |= 1u << (n - 1 - i);
        return r;
    }
    static uint32_t mascara(unsigned n) { return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u); }
    // A-law y mu-law sobre un dato válido de `bits` (PCM_CONF)
    static uint32_t g711_aplica(uint32_t v, unsigned bits, unsigned conf) {
        if (conf == 1 || conf == 3) {                       // comprimir
            int32_t s = int32_t(v << (32 - bits)) >> (32 - bits);
            const int32_t m = bits >= 16 ? (s >> (bits - 16)) : (s << (16 - bits));
            return conf == 1 ? g711::lin2alaw(int16_t(m)) : g711::lin2ulaw(int16_t(m));
        }
        const int16_t m = conf == 0 ? g711::alaw2lin(uint8_t(v)) : g711::ulaw2lin(uint8_t(v));
        return uint16_t(m) & mascara(bits);
    }
    // Un dato de la GDMA (false si no hay)
    bool dato_tx(const Trama& t, uint32_t& v) {
        uint8_t b[4] = {};
        if (!gdma || gdma->saca(PERI_GDMA, b, t.bytes_dato) != t.bytes_dato) return false;
        v = 0;
        for (unsigned i = 0; i < t.bytes_dato; ++i)
            v |= uint32_t(b[t.big ? t.bytes_dato - 1 - i : i]) << (8 * i);
        v &= mascara(t.bits_dato);
        const uint32_t c = r_[0x24 / 4];
        if (!((c >> 12) & 1u)) v = g711_aplica(v, t.bits_dato, (c >> 10) & 3u) & mascara(t.bits_dato);
        return true;
    }
    // Los bits que salen por el canal (MSB primero), de bits_canal
    uint32_t a_canal(const Trama& t, uint32_t v) const {
        uint32_t w;
        if (t.bits_canal >= t.bits_dato) w = t.izq ? v << (t.bits_canal - t.bits_dato) : v;
        else w = v;                                        // los bits bajos
        w &= mascara(t.bits_canal);
        return t.orden ? invierte(w, t.bits_canal) : w;
    }

    // --- El transmisor TDM -------------------------------------------------
    uint32_t canal_[16] = {};
    bool prev_sd_ = false;
    // Prepara la trama siguiente; `sin_datos` si la GDMA se ha quedado sin
    bool prepara_tx(const Trama& t, bool& sin_datos) {
        sin_datos = false;
        for (unsigned k = 0; k < t.canales; ++k) {
            uint32_t v = 0;
            if ((t.chan_en >> k) & 1u) {
                if (dato_tx(t, v)) { tx_ultima_[k] = v; ++n_tx_; }
                else { sin_datos = true; v = tx_ultima_[k]; }
            } else {
                if (t.salta) { uint32_t x; (void)dato_tx(t, x); }
                v = (t.igual && k) ? tx_ultima_[k - 1] : r_[0x68 / 4];
                v &= mascara(t.bits_dato);
                tx_ultima_[k] = v;
            }
            canal_[k] = a_canal(t, v);
        }
        return true;
    }
    // El bit de SD en el ciclo `cy` de la trama preparada
    bool bit_tx(const Trama& t, unsigned cy) const {
        const int n = int(cy) - (t.msb_shift ? 1 : 0);
        if (n < 0) return prev_sd_;
        const unsigned k = unsigned(n) / t.bits_canal, i = unsigned(n) % t.bits_canal;
        return k < t.canales ? ((canal_[k] >> (t.bits_canal - 1 - i)) & 1u) : false;
    }
    // Con MSB_SHIFT, el último bit de la trama va al principio de la siguiente
    void cierra_trama_tx(const Trama& t) {
        if (t.msb_shift) prev_sd_ = bit_tx(t, t.ciclos);
    }

    // EL TRANSMISOR MAESTRO
    void tx_proc() {
        for (;;) {
            sc_core::wait(ev_tx_);
            const bool start = (r_[0x24 / 4] >> 2) & 1u;
            const double hz = hz_bck(false);
            if (!start || hz <= 0 || tx_esclavo()) continue;
            tx_idle_ = false;
            const sc_core::sc_time m(0.5 / hz, sc_core::SC_SEC);
            prev_sd_ = false;
            if (tx_pdm()) { tx_pdm_maestro(m); out(S_O_BCK, false); tx_idle_ = true; continue; }
            while ((r_[0x24 / 4] >> 2) & 1u) {
                const Trama t = trama(false);
                bool sin_datos;
                prepara_tx(t, sin_datos);
                if (sin_datos) {
                    raw_ |= 2u;                                     // TX_DONE
                    evalua();
                    if ((r_[0x24 / 4] >> 13) & 1u) {               // TX_STOP_EN
                        // Con MSB_SHIFT, el último bit va en un ciclo más
                        if (t.msb_shift) ciclo_tx(t, 0, m, true);
                        break;
                    }
                }
                for (unsigned cy = 0; cy < t.ciclos; ++cy) ciclo_tx(t, cy, m, false);
                cierra_trama_tx(t);
            }
            out(S_O_BCK, false);
            tx_idle_ = true;
        }
    }
    // Un ciclo de BCK del maestro: la bajada con WS y SD, y la subida
    void ciclo_tx(const Trama& t, unsigned cy, const sc_core::sc_time& m, bool cola) {
        const bool ws = cola ? t.ws_pol : (cy < t.ws_ancho ? t.ws_pol : !t.ws_pol);
        const bool sd = cola ? prev_sd_ : bit_tx(t, cy);
        Esc e[3] = {{S_O_BCK, false, retraso(false, 20)}, {S_O_WS, ws, retraso(false, 16)}, {S_SD, sd, retraso(false, 0)}};
        espera_resto(m, escribe(e, 3));
        Esc s[1] = {{S_O_BCK, true, retraso(false, 20)}};
        const sc_core::sc_time h = escribe(s, 1);
        if (loopback()) rx_flanco(cy);
        espera_resto(m, h);
    }

    // EL TRANSMISOR ESCLAVO: BCK y WS de I2SO_BCK_in/WS_in
    void tx_esclavo_proc() {
        for (;;) {
            sc_core::wait(ev_tx_);
            if (!((r_[0x24 / 4] >> 2) & 1u) || !tx_esclavo() || !ev_in) continue;
            tx_idle_ = false;
            int cy = -1;
            bool ws_prev = false, lista = false, parado = false;
            prev_sd_ = false;
            bool bck_prev = lee(S_O_BCK);
            t_ult_bck_tx_ = sc_core::sc_time_stamp();
            while (((r_[0x24 / 4] >> 2) & 1u) && tx_esclavo()) {
                const sc_core::sc_time lim = limite_hung(false);
                if (lim > sc_core::SC_ZERO_TIME) {
                    sc_core::wait(lim, entrada(S_O_BCK) | ev_tx_);
                    if (sc_core::sc_time_stamp() >= t_ult_bck_tx_ + lim && hung_on()) { raw_ |= 8u; evalua(); }
                } else {
                    sc_core::wait(entrada(S_O_BCK) | ev_tx_);
                }
                const bool bck = lee(S_O_BCK);
                if (bck == bck_prev) continue;
                bck_prev = bck;
                t_ult_bck_tx_ = sc_core::sc_time_stamp();
                const sc_core::sc_time dbck = retraso(false, 28);
                if (dbck > sc_core::SC_ZERO_TIME) sc_core::wait(dbck);
                if (tx_pdm()) { pdm_esclavo_flanco(bck); continue; }
                const Trama t = trama(false);
                if (bck) {                                         // subida: ¿dónde estamos?
                    const bool ws = lee_retrasada(S_O_WS, retraso(false, 24));
                    if (ws == t.ws_pol && ws_prev != t.ws_pol) {
                        if (!lista) { bool sd; prepara_tx(t, sd); lista = true; if (sd) aviso_sin_datos(parado); }
                        cy = 0;
                    } else if (cy >= 0) cy = int((unsigned(cy) + 1u) % t.ciclos);
                    ws_prev = ws;
                    if (loopback() && cy >= 0) rx_flanco(unsigned(cy));
                } else {                                           // bajada: el bit siguiente
                    if (cy < 0) { escribe_sd(false); continue; }
                    const unsigned sig = (unsigned(cy) + 1u) % t.ciclos;
                    const bool era = parado;                       // la trama que acaba
                    if (sig == 0) {
                        cierra_trama_tx(t);
                        bool sin;
                        prepara_tx(t, sin);
                        if (sin) aviso_sin_datos(parado);
                    }
                    // Con MSB_SHIFT, el ciclo 0 lleva el último bit de la anterior
                    if (sig == 0 && t.msb_shift) escribe_sd(era ? false : bit_tx(t, 0));
                    else escribe_sd(parado ? false : bit_tx(t, sig));
                }
            }
            tx_idle_ = true;
        }
    }
    sc_core::sc_time t_ult_bck_tx_, t_ult_bck_rx_;
    void escribe_sd(bool v) {
        Esc e[1] = {{S_SD, v, retraso(false, 0)}};
        escribe(e, 1);
    }
    // Sin datos en esclavo: TX_DONE y, con TX_STOP_EN, ceros
    void aviso_sin_datos(bool& parado) {
        raw_ |= 2u;
        evalua();
        if ((r_[0x24 / 4] >> 13) & 1u) parado = true;
    }

    // --- PDM en transmisión ------------------------------------------------
    // El origen de cada mitad (0 izquierdo, 1 derecho, 2 «single»), tabla 29.9-4
    void origen_pdm(unsigned& izq, unsigned& der) const {
        const uint32_t c = r_[0x24 / 4];
        const unsigned mod = (c >> 24) & 7u;
        const bool pol = (c >> 17) & 1u;
        izq = 0; der = 1;
        switch (mod) {
        case 1: izq = der = pol ? 1u : 0u; break;
        case 2: izq = der = pol ? 0u : 1u; break;
        case 3: if (pol) { izq = 0; der = 2; } else { izq = 2; der = 1; } break;
        case 4: if (pol) { izq = 2; der = 1; } else { izq = 0; der = 2; } break;
        default: break;
        }
    }
    bool pcm2pdm() const { return (r_[0x40 / 4] >> 25) & 1u; }
    // Un bit de PDM del canal `c` (0, 1) para el periodo de WS en curso
    bool bit_pdm(unsigned c) {
        if (pcm2pdm()) {
            // Modulador sigma-delta de segundo orden, con los integradores acotados
            const double x = pdm_.x[c];
            const double y = pdm_.i2[c] >= 0 ? 1.0 : -1.0;
            pdm_.i1[c] = std::clamp(pdm_.i1[c] + x - y, -4.0, 4.0);
            pdm_.i2[c] = std::clamp(pdm_.i2[c] + pdm_.i1[c] - y, -8.0, 8.0);
            return y > 0;
        }
        const bool mono = (r_[0x24 / 4] >> 5) & 1u;
        const unsigned f = mono ? 0u : c;
        if (pdm_.quedan[f] == 0) return false;
        return (pdm_.pal[f] >> (pdm_.quedan[f] - 1)) & 1u;
    }
    // Al empezar un periodo de WS: recarga lo que se ha gastado
    void periodo_pdm() {
        const Trama t = trama(false);
        const bool mono = (r_[0x24 / 4] >> 5) & 1u;
        const unsigned p = pdm_.periodo++;
        if (pcm2pdm()) {
            const unsigned osr2 = (r_[0x40 / 4] >> 1) & 0xFu;
            const unsigned cada = std::max(1u, osr2 * 32u);
            if (p % cada == 0) {
                uint32_t v;
                if (t.bits_dato == 32 && !mono) {
                    if (dato_tx(t, v)) {
                        pdm_.x[0] = double(int16_t(v & 0xFFFFu)) / 32768.0;
                        pdm_.x[1] = double(int16_t(v >> 16)) / 32768.0;
                        ++n_tx_;
                    } else sin_pdm();
                } else {
                    for (unsigned c = 0; c < (mono ? 1u : 2u); ++c) {
                        if (dato_tx(t, v)) {
                            const int32_t s = int32_t(v << (32 - t.bits_dato)) >> (32 - t.bits_dato);
                            pdm_.x[c] = double(s) / double(1u << (t.bits_dato - 1));
                            ++n_tx_;
                        } else sin_pdm();
                    }
                    if (mono) pdm_.x[1] = pdm_.x[0];
                }
            }
            return;
        }
        for (unsigned c = 0; c < (mono ? 1u : 2u); ++c) {
            if (pdm_.quedan[c] > 1) { --pdm_.quedan[c]; continue; }
            uint32_t v;
            if (dato_tx(t, v)) { pdm_.pal[c] = v; ++n_tx_; }
            else sin_pdm();
            pdm_.quedan[c] = t.bits_dato;
        }
    }
    bool pdm_sin_ = false;
    void sin_pdm() { pdm_sin_ = true; raw_ |= 2u; evalua(); }
    // Lo que sale en la mitad `h` (0 la de WS_IDLE_POL) del periodo
    void sd_pdm(unsigned h, bool& sd, bool& sd1, bool& toca1) {
        const uint32_t p = r_[0x40 / 4];
        toca1 = false;
        if (pcm2pdm() && ((p >> 24) & 1u)) {                 // DAC: un canal por periodo
            sd = pdm_.hay ? bit_ch_[0] : false;
            if ((p >> 23) & 1u) { sd1 = bit_ch_[1]; toca1 = true; }
            return;
        }
        unsigned iz, de;
        origen_pdm(iz, de);
        const unsigned o = h == 0 ? iz : de;
        if (o == 2) sd = (r_[0x68 / 4] >> (31u - ((pdm_.periodo - 1u) & 31u))) & 1u;   // «single», del bit 31 al 0
        else sd = bit_ch_[o];
    }
    bool bit_ch_[2] = {};
    void tx_pdm_maestro(const sc_core::sc_time& m) {
        pdm_ = PdmTx{};
        pdm_sin_ = false;
        const bool pol = (r_[0x24 / 4] >> 17) & 1u;
        while ((r_[0x24 / 4] >> 2) & 1u && tx_pdm()) {
            periodo_pdm();
            if (pdm_sin_ && ((r_[0x24 / 4] >> 13) & 1u)) break;
            pdm_sin_ = false;
            pdm_.hay = true;
            bit_ch_[0] = bit_pdm(0);
            bit_ch_[1] = bit_pdm(1);
            for (unsigned h = 0; h < 2; ++h) {
                bool sd = false, sd1 = false, t1 = false;
                sd_pdm(h, sd, sd1, t1);
                Esc e[4] = {{S_O_BCK, false, retraso(false, 20)}, {S_O_WS, h == 0 ? pol : !pol, retraso(false, 16)},
                            {S_SD, sd, retraso(false, 0)}, {S_SD1, sd1, retraso(false, 4)}};
                espera_resto(m, escribe(e, t1 ? 4u : 3u));
                Esc s[1] = {{S_O_BCK, true, retraso(false, 20)}};
                const sc_core::sc_time hh = escribe(s, 1);
                if (loopback()) rx_pdm_bit(h, lee_retrasada(S_SD, retraso(true, 0)));
                espera_resto(m, hh);
            }
        }
    }
    // PDM en esclavo: la mitad la dice WS en la subida anterior
    bool pdm_ws_ = false;
    void pdm_esclavo_flanco(bool bck) {
        const bool pol = (r_[0x24 / 4] >> 17) & 1u;
        if (bck) {
            pdm_ws_ = lee_retrasada(S_O_WS, retraso(false, 24));
            if (loopback()) rx_pdm_bit(pdm_ws_ == pol ? 0u : 1u, lee_retrasada(S_SD, retraso(true, 0)));
            return;
        }
        const unsigned h = (pdm_ws_ == pol) ? 1u : 0u;            // la siguiente mitad
        if (h == 0) {
            periodo_pdm();
            pdm_.hay = true;
            bit_ch_[0] = bit_pdm(0);
            bit_ch_[1] = bit_pdm(1);
        }
        bool sd = false, sd1 = false, t1 = false;
        sd_pdm(h, sd, sd1, t1);
        if (pdm_sin_ && ((r_[0x24 / 4] >> 13) & 1u)) sd = sd1 = false;
        Esc e[2] = {{S_SD, sd, retraso(false, 0)}, {S_SD1, sd1, retraso(false, 4)}};
        escribe(e, t1 ? 2u : 1u);
    }

    // --- El receptor -------------------------------------------------------
    // Da `n` bytes a la GDMA, con el EOF cada RXEOF_NUM
    void entrega(const uint8_t* b, unsigned n) {
        if (rx_parado_) return;
        const unsigned eofn = r_[0x64 / 4] & 0xFFFu;
        const unsigned modo = (r_[0x20 / 4] >> 13) & 3u;
        unsigned dado = 0;
        while (dado < n) {
            const unsigned falta = eofn ? eofn - rx_bytes_ : n;
            const unsigned q = std::min(n - dado, falta ? falta : n - dado);
            const bool eof = eofn && rx_bytes_ + q >= eofn;
            const unsigned ok = gdma ? gdma->mete(PERI_GDMA, b + dado, q, eof) : 0u;
            if (ok < q) {                                         // la GDMA no lo toma
                const sc_core::sc_time ahora = sc_core::sc_time_stamp();
                if (!atasco_) { atasco_ = true; t_atasco_ = ahora; }
                else if (hung_on() && ahora - t_atasco_ >= limite_hung(true)) { raw_ |= 4u; evalua(); }
                if (modo == 2) rx_parado_ = true;
                return;
            }
            atasco_ = false;
            dado += q;
            rx_bytes_ += q;
            if (eof) {
                rx_bytes_ = 0;
                raw_ |= 1u;                                         // RX_DONE
                evalua();
                if (modo == 1) { rx_parado_ = true; return; }
            }
        }
    }
    // Un dato de canal ya recibido (bits_canal) -> la memoria
    void guarda_rx(const Trama& t, uint32_t w) {
        uint32_t v;
        if (t.bits_canal > t.bits_dato) v = t.izq ? (w >> (t.bits_canal - t.bits_dato)) : w;
        else v = w;
        v &= mascara(t.bits_dato);
        const uint32_t c = r_[0x20 / 4];
        if (!((c >> 12) & 1u)) v = g711_aplica(v, t.bits_dato, (c >> 10) & 3u) & mascara(t.bits_dato);
        uint8_t b[4];
        for (unsigned j = 0; j < t.bytes_dato; ++j) {
            const uint8_t x = uint8_t(v >> (8 * j));
            b[t.big ? t.bytes_dato - 1 - j : j] = x;
        }
        ++n_rx_;
        entrega(b, t.bytes_dato);
    }
    // TDM: un flanco de subida de BCK, en el ciclo `cy` de la trama (lo da
    // quien sabe dónde está: el transmisor en SIG_LOOPBACK, el receptor
    // maestro o el esclavo). Empieza con una trama entera.
    void rx_flanco(unsigned cy) {
        if (!((r_[0x20 / 4] >> 2) & 1u)) { lb_.on = false; return; }
        const Trama t = trama(true);
        const bool sd = lee_retrasada(S_SD, retraso(true, 0));
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
        w &= mascara(t.bits_canal);
        lb_.acum = 0;
        if (!((t.chan_en >> k) & 1u)) return;
        guarda_rx(t, w);
    }
    // PDM: el bit de la mitad `h` (0 izquierda)
    void rx_pdm_bit(unsigned h, bool sd) {
        if (!((r_[0x20 / 4] >> 2) & 1u)) return;
        const Trama t = trama(true);
        rxp_.acum[h] = (rxp_.acum[h] << 1) | (sd ? 1u : 0u);
        if (++rxp_.n[h] < t.bits_dato || h == 0) return;
        if (rxp_.n[0] < t.bits_dato) { rxp_.n[1] = 0; rxp_.acum[1] = 0; return; }
        // Las dos palabras: izquierda y derecha
        for (unsigned c = 0; c < 2; ++c) {
            uint32_t w = t.orden ? invierte(rxp_.acum[c], t.bits_dato) : rxp_.acum[c];
            w &= mascara(t.bits_dato);
            if ((t.chan_en >> c) & 1u) guarda_rx(t, w);
            rxp_.acum[c] = 0; rxp_.n[c] = 0;
        }
    }

    // EL RECEPTOR MAESTRO (sin SIG_LOOPBACK): su propio BCK y WS
    void rx_proc() {
        for (;;) {
            sc_core::wait(ev_rx_);
            const uint32_t c = r_[0x20 / 4];
            const double hz = hz_bck(true);
            if (!((c >> 2) & 1u) || rx_esclavo() || loopback() || hz <= 0) continue;
            const sc_core::sc_time m(0.5 / hz, sc_core::SC_SEC);
            while (((r_[0x20 / 4] >> 2) & 1u) && !loopback() && !rx_esclavo()) {
                const Trama t = trama(true);
                const bool pdm = rx_pdm();
                const unsigned ciclos = pdm ? 2u : t.ciclos;
                for (unsigned cy = 0; cy < ciclos && ((r_[0x20 / 4] >> 2) & 1u); ++cy) {
                    const bool ws = pdm ? (cy == 0 ? t.ws_pol : !t.ws_pol) : (cy < t.ws_ancho ? t.ws_pol : !t.ws_pol);
                    Esc e[2] = {{S_I_BCK, false, retraso(true, 20)}, {S_I_WS, ws, retraso(true, 16)}};
                    espera_resto(m, escribe(e, 2));
                    Esc s[1] = {{S_I_BCK, true, retraso(true, 20)}};
                    const sc_core::sc_time h = escribe(s, 1);
                    if (pdm) rx_pdm_bit(cy, lee_retrasada(S_SD, retraso(true, 0)));
                    else rx_flanco(cy);
                    espera_resto(m, h);
                }
            }
            out(S_I_BCK, false);
            lb_.on = false;
        }
    }
    // EL RECEPTOR ESCLAVO: BCK y WS de I2SI_BCK_in/WS_in
    void rx_esclavo_proc() {
        for (;;) {
            sc_core::wait(ev_rx_);
            if (!((r_[0x20 / 4] >> 2) & 1u) || !rx_esclavo() || loopback() || !ev_in) continue;
            int cy = -1;
            bool ws_prev = false;
            bool bck_prev = lee(S_I_BCK);
            lb_.on = false;
            t_ult_bck_rx_ = sc_core::sc_time_stamp();
            while (((r_[0x20 / 4] >> 2) & 1u) && rx_esclavo() && !loopback()) {
                const sc_core::sc_time lim = limite_hung(true);
                if (lim > sc_core::SC_ZERO_TIME) {
                    sc_core::wait(lim, entrada(S_I_BCK) | ev_rx_);
                    if (sc_core::sc_time_stamp() >= t_ult_bck_rx_ + lim && hung_on()) { raw_ |= 4u; evalua(); }
                } else {
                    sc_core::wait(entrada(S_I_BCK) | ev_rx_);
                }
                const bool bck = lee(S_I_BCK);
                if (bck == bck_prev) continue;
                bck_prev = bck;
                t_ult_bck_rx_ = sc_core::sc_time_stamp();
                if (!bck) continue;
                const sc_core::sc_time dbck = retraso(true, 28);
                if (dbck > sc_core::SC_ZERO_TIME) sc_core::wait(dbck);
                const Trama t = trama(true);
                const bool ws = lee_retrasada(S_I_WS, retraso(true, 24));
                if (rx_pdm()) { rx_pdm_bit(ws == t.ws_pol ? 0u : 1u, lee_retrasada(S_SD, retraso(true, 0))); continue; }
                if (ws == t.ws_pol && ws_prev != t.ws_pol) cy = 0;
                else if (cy >= 0) cy = int((unsigned(cy) + 1u) % t.ciclos);
                ws_prev = ws;
                if (cy >= 0) rx_flanco(unsigned(cy));
            }
            lb_.on = false;
        }
    }

    // --- HUNG --------------------------------------------------------------
    bool hung_on() const { return (r_[0x60 / 4] >> 11) & 1u; }
    sc_core::sc_time limite_hung(bool rx) const {
        if (!hung_on()) return sc_core::SC_ZERO_TIME;
        const uint32_t h = r_[0x60 / 4];
        double hz = hz_clk(rx);
        if (hz <= 0) hz = hz_xtal;
        const double n = double((h & 0xFFu) << ((h >> 8) & 7u));
        return sc_core::sc_time(std::max(n, 1.0) / hz, sc_core::SC_SEC);
    }

    // --- MCLK --------------------------------------------------------------
    void mclk_proc() {
        bool nivel = false;
        for (;;) {
            const bool del_rx = (r_[0x30 / 4] >> 29) & 1u;
            const double hz = hz_clk(del_rx);
            if (hz <= 0 || !usada || !usada(S_MCLK)) {
                if (nivel) { out(S_MCLK, false); nivel = false; }
                if (ev_matriz) sc_core::wait(ev_cfg_ | ev_matriz());
                else sc_core::wait(ev_cfg_);
                continue;
            }
            nivel = !nivel;
            out(S_MCLK, nivel);
            sc_core::wait(sc_core::sc_time(0.5 / hz, sc_core::SC_SEC));
        }
    }
    // I2S_MCLK_in como fuente (CLK_SEL = 3): su frecuencia, de subida a subida
    void mclk_in_proc() {
        sc_core::sc_time t0;
        bool hay = false, prev = false;
        for (;;) {
            const bool sel = ((r_[0x30 / 4] >> 27) & 3u) == 3u || ((r_[0x34 / 4] >> 27) & 3u) == 3u;
            if (!sel || !ev_in || !lee) { hay = false; sc_core::wait(ev_cfg_); continue; }
            sc_core::wait(ev_in(S_MCLK) | ev_cfg_);
            const bool v = lee(S_MCLK);
            if (v && !prev) {
                const sc_core::sc_time ahora = sc_core::sc_time_stamp();
                if (hay && ahora > t0) hz_mclk_in_ = 1.0 / (ahora - t0).to_seconds();
                t0 = ahora;
                hay = true;
            }
            prev = v;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_I2S_ESP_H
