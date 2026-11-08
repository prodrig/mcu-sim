// =============================================================================
// uart_esp.h — La UART de Espressif (la del C3, y la del C6 con sus cambios)
//
// E3 de doc/analisis_esp32_riscv.md, §4. [TRM-C3 §26]. Lo que se modela:
//
//   * FIFO de 128 bytes en cada sentido, por UART_FIFO_REG (0x00): leer saca
//     de la de recepción; escribir mete en la de transmisión;
//   * el reloj: UART_CLK_CONF elige SCLK (1 APB, 2 RC_FAST, 3 XTAL) y lo
//     divide por SCLK_DIV_NUM + 1 (+ A/B, que no se modela); la velocidad es
//     SCLK / (CLKDIV + FRAG/16). Tras el reset, XTAL/2 y 0x2B6: lo que pone
//     la ROM no está aquí (E4);
//   * la trama de CONF0: 5-8 bits, paridad par o impar, 1, 1,5 o 2 de
//     parada; las inversiones de TXD y RXD; el lazo (LOOPBACK); el break
//     (TXD_BRK); y el reset de las FIFO;
//   * los umbrales de CONF1 y la espera de RX (RX_TOUT_EN, RX_TOUT_THRHD en
//     MEM_CONF, en tiempos de bit);
//   * las interrupciones RXFIFO_FULL, TXFIFO_EMPTY, PARITY_ERR, FRM_ERR,
//     RXFIFO_OVF, BRK_DET, RXFIFO_TOUT y TX_DONE, con INT_RAW, INT_ST,
//     INT_ENA e INT_CLR, a una fuente de la matriz;
//   * UART_ID: REG_UPDATE, que el silicio baja al sincronizar los registros
//     con el núcleo de la UART: aquí se baja en el acto.
//
//   * la detección de baudios (E5): con CONF0.AUTOBAUD_EN, cada flanco de
//     RXD cuenta en RXD_CNT y cada pulso mide, en ciclos de SCLK, el mínimo
//     de los bajos (LOWPULSE), de los altos (HIGHPULSE) y de flanco a flanco
//     de subida y de bajada (POSPULSE, NEGPULSE). Al poner AUTOBAUD_EN se
//     vuelven a 0 y a 0xFFF. Es lo que usa la ROM para esptool: espera 128
//     flancos del SYNC (0x55 0x55...) y saca el divisor de
//     (LOWPULSE + HIGHPULSE + 2) / 2 [TRM-C3 §26.4.5; uart_baudrate_detect
//     de la ROM]. El valor medido es el del pulso entero menos 1, como en el
//     silicio; el filtro de glitches (RX_FILT) no se modela.
//
// Declarado (contesta y guarda, no hace): el control de flujo por hardware y
// por software, RS485, IrDA, AT_CMD y el modo de bajo consumo.
//
// La línea va bit a bit por las señales de la matriz GPIO (U0TXD_out,
// U0RXD_in...), así que llega a un pad y a lo que haya en la placa.
// =============================================================================
#ifndef STM32_PERIPH_ESP_UART_ESP_H
#define STM32_PERIPH_ESP_UART_ESP_H

#include <systemc>
#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include "../../common/periph_base.h"

namespace stm32 {

class UartEsp : public BusSlave {
public:
    // La interrupción, hacia la matriz (nivel)
    std::function<void(bool)> irq;
    // Las frecuencias de las fuentes de SCLK [Hz] (las pone el SoC)
    double hz_apb = 80e6, hz_xtal = 40e6, hz_rc_fast = 17.5e6;
    // Todo lo transmitido, para el banco
    std::string salida;
    bool eco = false;

    UartEsp(sc_core::sc_module_name nm, uint32_t base, sc_core::sc_signal_inout_if<bool>& txd,
            const sc_core::sc_signal_in_if<bool>& rxd)
        : BusSlave(nm, base, 0x1000), txd_(txd), rxd_(rxd) {
        SC_HAS_PROCESS(UartEsp);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        SC_THREAD(tout_proc);
        SC_METHOD(autobaud_proc);
        sensitive << rxd_.value_changed_event();
        dont_initialize();
        reset_regs();
    }

    void reset() { reset_regs(); evalua(); }
    double baudios() const {
        const unsigned sel = (clk_conf_ >> 20) & 3u;
        const double src = sel == 1 ? hz_apb : sel == 2 ? hz_rc_fast : sel == 3 ? hz_xtal : 0.0;
        const double sclk = src / double(((clk_conf_ >> 12) & 0xFFu) + 1u);
        const double div = double(clkdiv_ & 0xFFFu) + double((clkdiv_ >> 20) & 0xFu) / 16.0;
        return div > 0.0 ? sclk / div : 0.0;
    }
    unsigned rx_cnt() const { return unsigned(rx_.size()); }
    unsigned tx_cnt() const { return unsigned(tx_.size()); }
    uint64_t n_tx() const { return n_tx_; }
    uint64_t n_rx() const { return n_rx_; }

    // Bits de interrupción
    enum : uint32_t {
        RXFIFO_FULL = 1u << 0, TXFIFO_EMPTY = 1u << 1, PARITY_ERR = 1u << 2, FRM_ERR = 1u << 3,
        RXFIFO_OVF = 1u << 4, BRK_DET = 1u << 7, RXFIFO_TOUT = 1u << 8, TX_DONE = 1u << 14,
    };

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: {
            if (rx_.empty()) return 0;
            const uint8_t b = rx_.front();
            rx_.pop_front();
            evalua();
            return b;
        }
        case 0x04: return raw_;
        case 0x08: return raw_ & ena_;
        case 0x0C: return ena_;
        case 0x14: return clkdiv_;
        case 0x18: return rx_filt_;
        case 0x1C: return status();
        case 0x20: return conf0_;
        case 0x24: return conf1_;
        case 0x28: return ab_low_;
        case 0x2C: return ab_high_;
        case 0x30: return ab_cnt_;
        case 0x70: return ab_pos_;
        case 0x74: return ab_neg_;
        case 0x60: return mem_conf_;
        case 0x78: return clk_conf_;
        case 0x7C: return 0x2008270u;
        case 0x80: return id_;
        default: return otros_[(off & 0xFFu) / 4];
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        auto w = [&](uint32_t& r) { r = (r & ~m) | (v & m); };
        switch (off) {
        case 0x00:
            if (tx_.size() < 128) tx_.push_back(uint8_t(v));
            tx_done_pend_ = true;
            ev_tx_.notify(sc_core::SC_ZERO_TIME);
            evalua();
            return;
        case 0x0C: w(ena_); evalua(); return;
        case 0x10: raw_ &= ~(v & m); evalua(); return;
        case 0x14: w(clkdiv_); return;
        case 0x18: w(rx_filt_); return;
        case 0x20: {
            const bool ab_antes = conf0_ & AUTOBAUD_EN;
            w(conf0_);
            if ((conf0_ & AUTOBAUD_EN) && !ab_antes) reinicia_autobaud();
            if (conf0_ & (1u << 17)) rx_.clear();          // RXFIFO_RST
            if (conf0_ & (1u << 18)) tx_.clear();          // TXFIFO_RST
            if (conf0_ & (1u << 8)) ev_tx_.notify(sc_core::SC_ZERO_TIME);   // TXD_BRK
            publica_tx(linea_tx_);
            evalua();
            return;
        }
        case 0x24: w(conf1_); evalua(); return;
        case 0x60: w(mem_conf_); return;
        case 0x78: w(clk_conf_); return;
        case 0x80: w(id_); id_ &= ~(1u << 31); return;    // REG_UPDATE: hecho en el acto
        default: { uint32_t& r = otros_[(off & 0xFFu) / 4]; w(r); return; }
        }
    }

private:
    sc_core::sc_signal_inout_if<bool>& txd_;
    const sc_core::sc_signal_in_if<bool>& rxd_;
    std::deque<uint8_t> tx_, rx_;
    uint32_t raw_ = 0, ena_ = 0, clkdiv_ = 0, rx_filt_ = 0, conf0_ = 0, conf1_ = 0,
             mem_conf_ = 0, clk_conf_ = 0, id_ = 0;
    uint32_t otros_[64] = {};
    static constexpr uint32_t AUTOBAUD_EN = 1u << 27;
    uint32_t ab_low_ = 0xFFFu, ab_high_ = 0xFFFu, ab_pos_ = 0xFFFu, ab_neg_ = 0xFFFu, ab_cnt_ = 0;
    sc_core::sc_time ab_t_ = sc_core::SC_ZERO_TIME, ab_sub_ = sc_core::SC_ZERO_TIME,
                     ab_baj_ = sc_core::SC_ZERO_TIME;
    bool ab_hay_ = false, ab_hay_sub_ = false, ab_hay_baj_ = false;
    bool linea_tx_ = true, tx_ocupado_ = false, rx_ocupado_ = false, tx_done_pend_ = false;
    sc_core::sc_event ev_tx_, ev_rx_byte_, ev_ltx_;
    sc_core::sc_time ult_rx_ = sc_core::SC_ZERO_TIME;
    uint64_t n_tx_ = 0, n_rx_ = 0;
    bool irq_ = false;

    void reinicia_autobaud() {
        ab_low_ = ab_high_ = ab_pos_ = ab_neg_ = 0xFFFu;
        ab_cnt_ = 0;
        ab_hay_ = ab_hay_sub_ = ab_hay_baj_ = false;
    }
    double hz_sclk() const {
        const unsigned sel = (clk_conf_ >> 20) & 3u;
        const double src = sel == 1 ? hz_apb : sel == 2 ? hz_rc_fast : sel == 3 ? hz_xtal : 0.0;
        return src / double(((clk_conf_ >> 12) & 0xFFu) + 1u);
    }
    // Un pulso de `t`, en ciclos de SCLK menos 1 (como lo cuenta el silicio)
    uint32_t ciclos(const sc_core::sc_time& t) const {
        const double c = t.to_seconds() * hz_sclk();
        return c < 1.0 ? 0u : std::min<uint32_t>(0xFFFu, uint32_t(c + 0.5) - 1u);
    }
    void autobaud_proc() {
        if (!(conf0_ & AUTOBAUD_EN)) return;
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        bool l = rxd_.read();
        if (rxd_inv()) l = !l;
        if (ab_hay_) {
            // Acaba un pulso del nivel contrario al de ahora
            const uint32_t c = ciclos(ahora - ab_t_);
            uint32_t& r = l ? ab_low_ : ab_high_;
            r = std::min(r, c);
        }
        if (l) { if (ab_hay_sub_) ab_pos_ = std::min(ab_pos_, ciclos(ahora - ab_sub_)); ab_sub_ = ahora; ab_hay_sub_ = true; }
        else   { if (ab_hay_baj_) ab_neg_ = std::min(ab_neg_, ciclos(ahora - ab_baj_)); ab_baj_ = ahora; ab_hay_baj_ = true; }
        ab_t_ = ahora;
        ab_hay_ = true;
        ab_cnt_ = (ab_cnt_ + 1u) & 0x3FFu;
    }

    void reset_regs() {
        reinicia_autobaud();
        tx_.clear(); rx_.clear();
        raw_ = 0; ena_ = 0;
        clkdiv_ = 0x2B6u;
        rx_filt_ = 0x08u;
        conf0_ = 0x1000001Cu;
        conf1_ = 0x60u | (0x60u << 9);
        mem_conf_ = 0x000A0012u;
        clk_conf_ = 0x03701000u;
        id_ = 0x40000500u;
    }
    unsigned bits_datos() const { return 5u + ((conf0_ >> 2) & 3u); }
    bool loopback() const { return (conf0_ >> 14) & 1u; }
    bool txd_inv() const { return (conf0_ >> 22) & 1u; }
    bool rxd_inv() const { return (conf0_ >> 19) & 1u; }
    sc_core::sc_time t_bit() const {
        const double b = baudios();
        return b > 0.0 ? sc_core::sc_time(1.0 / b, sc_core::SC_SEC) : sc_core::sc_time(1, sc_core::SC_MS);
    }
    uint32_t status() const {
        return unsigned(rx_.size()) | (unsigned(tx_.size()) << 16) |
               (uint32_t(linea_rx()) << 15) | (uint32_t(linea_tx_) << 31);
    }
    bool linea_rx() const {
        const bool l = loopback() ? linea_tx_ : rxd_.read();
        return rxd_inv() ? !l : l;
    }
    void publica_tx(bool l) {
        if (l != linea_tx_) ev_ltx_.notify(sc_core::SC_ZERO_TIME);
        linea_tx_ = l;
        txd_.write(txd_inv() ? !l : l);
    }
    // Los niveles que se recalculan (TXFIFO_EMPTY y RXFIFO_FULL son de nivel
    // en el silicio: vuelven en cuanto se borran si la condición sigue)
    void evalua() {
        const unsigned rx_thr = conf1_ & 0x1FFu, tx_thr = (conf1_ >> 9) & 0x1FFu;
        if (tx_.size() < tx_thr) raw_ |= TXFIFO_EMPTY;
        if (rx_.size() >= rx_thr && rx_thr) raw_ |= RXFIFO_FULL;
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }

    void tx_proc() {
        publica_tx(true);
        for (;;) {
            if (conf0_ & (1u << 8)) {                  // break: la línea a 0
                publica_tx(false);
                wait(t_bit() * double(bits_datos() + 2u));
                continue;
            }
            if (tx_.empty()) {
                if (tx_done_pend_ && !tx_ocupado_) {
                    tx_done_pend_ = false;
                    raw_ |= TX_DONE;
                    evalua();
                }
                wait(ev_tx_);
                continue;
            }
            const uint8_t d = tx_.front();
            tx_.pop_front();
            tx_ocupado_ = true;
            evalua();
            const sc_core::sc_time tb = t_bit();
            const unsigned n = bits_datos();
            publica_tx(false);
            wait(tb);
            unsigned unos = 0;
            for (unsigned i = 0; i < n; ++i) {
                const bool b = (d >> i) & 1u;
                unos += b;
                publica_tx(b);
                wait(tb);
            }
            if (conf0_ & 2u) {                          // PARITY_EN
                const bool impar = conf0_ & 1u;
                publica_tx(impar ? !(unos & 1u) : (unos & 1u));
                wait(tb);
            }
            publica_tx(true);
            const unsigned sp = (conf0_ >> 4) & 3u;     // 1: 1, 2: 1,5, 3: 2
            wait(sp == 2 ? tb * 1.5 : sp == 3 ? tb * 2.0 : tb);
            ++n_tx_;
            salida.push_back(char(d));
            if (eco) { std::putchar(d); std::fflush(stdout); }
            tx_ocupado_ = false;
        }
    }

    void rx_proc() {
        for (;;) {
            // Esperar el flanco de bajada del arranque
            if (!linea_rx()) { wait(rxd_.value_changed_event() | ev_ltx_); continue; }
            wait(rxd_.value_changed_event() | ev_ltx_);
            if (linea_rx()) continue;
            const sc_core::sc_time tb = t_bit();
            rx_ocupado_ = true;
            wait(tb / 2.0);
            if (linea_rx()) { rx_ocupado_ = false; continue; }     // falso arranque
            const unsigned n = bits_datos();
            unsigned d = 0, unos = 0;
            for (unsigned i = 0; i < n; ++i) {
                wait(tb);
                const bool b = linea_rx();
                d |= unsigned(b) << i;
                unos += b;
            }
            bool err_par = false;
            if (conf0_ & 2u) {
                wait(tb);
                const bool impar = conf0_ & 1u;
                const bool esperado = impar ? !(unos & 1u) : (unos & 1u);
                err_par = linea_rx() != esperado;
            }
            wait(tb);
            const bool parada = linea_rx();
            rx_ocupado_ = false;
            if (!parada && d == 0) { raw_ |= BRK_DET | FRM_ERR; evalua(); continue; }
            if (!parada) raw_ |= FRM_ERR;
            if (err_par) raw_ |= PARITY_ERR;
            if (rx_.size() >= 128) raw_ |= RXFIFO_OVF;
            else rx_.push_back(uint8_t(d));
            ++n_rx_;
            ult_rx_ = sc_core::sc_time_stamp();
            ev_rx_byte_.notify(sc_core::SC_ZERO_TIME);
            evalua();
        }
    }

    // RXFIFO_TOUT: con RX_TOUT_EN, datos en la FIFO y la línea callada
    // RX_TOUT_THRHD tiempos de bit desde el último byte
    void tout_proc() {
        for (;;) {
            wait(ev_rx_byte_);
            for (;;) {
                if (!(conf1_ & (1u << 21))) break;
                const unsigned thr = (mem_conf_ >> 16) & 0x3FFu;
                const sc_core::sc_time lim = ult_rx_ + t_bit() * double(thr ? thr : 1u);
                if (sc_core::sc_time_stamp() < lim) { wait(lim - sc_core::sc_time_stamp(), ev_rx_byte_); continue; }
                if (!rx_.empty()) { raw_ |= RXFIFO_TOUT; evalua(); }
                break;
            }
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_UART_ESP_H
