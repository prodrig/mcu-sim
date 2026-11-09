// =============================================================================
// i2c_esp.h — El controlador I2C de los ESP32-C3 (maestro, con su lista de
//             órdenes)
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §28; i2c_reg.h y hal/i2c_ll.h de
// ESP-IDF]. El maestro no se maneja byte a byte: se le da una lista de hasta
// 8 órdenes (COMD0-7: OPCODE[13:11] = 6 RSTART, 1 WRITE, 3 READ, 2 STOP,
// 4 END; ACK_VALUE[10], ACK_EXP[9], ACK_CHECK_EN[8], BYTE_NUM[7:0]) y los
// datos en la FIFO de 32 bytes (I2C_DATA), y CTR.TRANS_START la ejecuta.
// Cada orden hecha pone COMMAND_DONE (bit 31). Al acabar: STOP da
// TRANS_COMPLETE (bit 7 de INT_RAW), END da END_DETECT (bit 3) y deja la
// lista en pausa; un ACK que no es el esperado da NACK (bit 10) y para. A la
// fuente 29.
//
// Va bit a bit por las líneas, como en el silicio: SCL y SDA son las señales
// 53 y 54 de la matriz (con drenador abierto en el pad: 0 tira, 1 suelta), y
// lo que se lee (el ACK, los datos, si un esclavo estira el reloj) viene de su
// entrada. Los tiempos: SCL_LOW_PERIOD y SCL_HIGH_PERIOD (+1) en ciclos de
// SCLK (CLK_CONF: XTAL o RC_FAST, entre SCLK_DIV_NUM + 1); la parada se
// espera con SCL_STOP_SETUP/HOLD, con un mínimo de medio periodo.
// LO QUE NO SE MODELA: el modo esclavo, el arbitraje entre maestros, los
// filtros, los plazos de SCL (TIME_OUT) y el acceso directo a la RAM de la
// FIFO (TXFIFO/RXFIFO_START_ADDR: se lee y se escribe por I2C_DATA).
// =============================================================================
#ifndef STM32_PERIPH_ESP_I2C_ESP_H
#define STM32_PERIPH_ESP_I2C_ESP_H

#include <systemc>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class I2cEsp : public BusSlave {
public:
    std::function<void(bool)> irq;
    std::function<void(bool scl, bool sda)> lineas;     // lo que el maestro suelta (1) o tira (0)
    std::function<bool()> lee_scl, lee_sda;              // lo que hay en el bus
    double hz_xtal = 40e6, hz_rc_fast = 17.5e6;

    explicit I2cEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(I2cEsp);
        SC_THREAD(maestro);
        // Las dos líneas las escribe un solo proceso: reset() (desde el
        // control del chip) y el maestro no chocan en el mismo delta (E115)
        SC_METHOD(pub_lineas); sensitive << ev_lineas_; dont_initialize();
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        r_[0x18 / 4] = 0x0000408Bu;        // FIFO_CONF de reset
        tx_.clear(); rx_.clear();
        raw_ = ena_ = 0;
        scl_ = sda_ = true;
        if (sc_core::sc_is_running()) ev_lineas_.notify();
        else ev_lineas_.notify(sc_core::SC_ZERO_TIME);
        evalua();
    }
    uint64_t bytes() const { return n_bytes_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x08: return (uint32_t(std::min<size_t>(rx_.size(), 63)) << 8) | (ocupado_ ? 0x10u : 0u) |
                          (uint32_t(std::min<size_t>(tx_.size(), 63)) << 18);
        case 0x1C: {
            if (rx_.empty()) return 0;
            const uint8_t b = rx_.front();
            rx_.pop_front();
            return b;
        }
        case 0x20: return raw_;
        case 0x28: return ena_;
        case 0x2C: return raw_ & ena_;
        case 0xF8: return 0x20070201u;
        default: return off < 0x200 ? r_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x04:
            r_[1] = v & ~(1u << 5);
            if (v & (1u << 5)) { pedido_ = true; ev_.notify(sc_core::SC_ZERO_TIME); }
            return;
        case 0x18:
            r_[6] = v;
            if (v & (1u << 12)) rx_.clear();
            if (v & (1u << 13)) tx_.clear();
            return;
        case 0x1C: if (tx_.size() < 32) tx_.push_back(uint8_t(v)); return;
        case 0x24: raw_ &= ~v; evalua(); return;
        case 0x28: ena_ = v; evalua(); return;
        default:
            if (off >= 0x58 && off < 0x78) { r_[off / 4] = v & 0x7FFFu; return; }   // COMD: DONE a 0
            if (off < 0x200) r_[off / 4] = v;
            return;
        }
    }

private:
    uint32_t r_[128] = {};
    std::deque<uint8_t> tx_, rx_;
    uint32_t raw_ = 0, ena_ = 0;
    bool irq_ = false, pedido_ = false, ocupado_ = false, scl_ = true, sda_ = true;
    uint64_t n_bytes_ = 0;
    unsigned sig_ = 0;                  // la orden por la que va (END: se sigue desde aquí)
    sc_core::sc_event ev_, ev_lineas_;

    void evalua() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    double fs() const {
        const uint32_t c = r_[0x54 / 4];
        const double f = ((c >> 20) & 1u) ? hz_rc_fast : hz_xtal;
        return f / double((c & 0xFFu) + 1u);
    }
    sc_core::sc_time ciclos(uint32_t n) const { return sc_core::sc_time(double(n) / fs(), sc_core::SC_SEC); }
    sc_core::sc_time t_bajo() const { return ciclos(std::max(1u, (r_[0] & 0x1FFu) + 1u)); }
    sc_core::sc_time t_alto() const { return ciclos(std::max(1u, (r_[0x38 / 4] & 0x1FFu) + 1u)); }
    void pon(bool scl, bool sda) { scl_ = scl; sda_ = sda; ev_lineas_.notify(); }
    void pub_lineas() { if (lineas) lineas(scl_, sda_); }
    // Soltar SCL y esperar a que suba de verdad (un esclavo puede estirarlo)
    void sube_scl() {
        pon(true, sda_);
        for (int i = 0; i < 100000 && lee_scl && !lee_scl(); ++i) sc_core::wait(ciclos(4));
    }
    bool bit(bool b) {                         // manda b (o lee, con b = 1) y devuelve SDA
        pon(false, b);
        sc_core::wait(t_bajo());
        sube_scl();
        const bool v = lee_sda ? lee_sda() : b;
        sc_core::wait(t_alto());
        pon(false, b);
        return v;
    }
    void maestro() {
        pon(true, true);
        for (;;) {
            while (!pedido_) sc_core::wait(ev_);
            pedido_ = false;
            ocupado_ = true;
            raw_ |= 1u << 9;                  // TRANS_START
            evalua();
            bool sigue = true;
            for (unsigned i = sig_; i < 8 && sigue; ++i) {
                uint32_t& c = r_[0x58 / 4 + i];
                const unsigned op = (c >> 11) & 7u, n = c & 0xFFu;
                switch (op) {
                case 6:                                         // RSTART
                    pon(scl_, true);
                    sc_core::wait(t_bajo());
                    sube_scl();
                    sc_core::wait(t_alto());
                    pon(true, false);                           // SDA baja con SCL alto
                    sc_core::wait(t_alto());
                    pon(false, false);
                    break;
                case 1:                                         // WRITE
                    for (unsigned k = 0; k < n && sigue; ++k) {
                        const uint8_t b = tx_.empty() ? 0xFF : tx_.front();
                        if (!tx_.empty()) tx_.pop_front();
                        for (int j = 7; j >= 0; --j) bit((b >> j) & 1u);
                        const bool ack = bit(true);             // 0: ACK
                        ++n_bytes_;
                        if (((c >> 8) & 1u) && ack != bool((c >> 9) & 1u)) {
                            raw_ |= 1u << 10;                   // NACK: para
                            sigue = false;
                        }
                    }
                    break;
                case 3:                                         // READ
                    for (unsigned k = 0; k < n; ++k) {
                        uint8_t b = 0;
                        for (int j = 7; j >= 0; --j) b = uint8_t((b << 1) | (bit(true) ? 1u : 0u));
                        if (rx_.size() < 32) rx_.push_back(b);
                        bit((c >> 10) & 1u);                    // ACK_VALUE
                        ++n_bytes_;
                    }
                    break;
                case 2:                                         // STOP
                    pon(false, false);
                    sc_core::wait(t_bajo());
                    sube_scl();
                    sc_core::wait(t_alto());
                    pon(true, true);                            // SDA sube con SCL alto
                    sc_core::wait(t_alto());
                    raw_ |= 1u << 7;                            // TRANS_COMPLETE
                    sigue = false;
                    break;
                case 4:                                         // END: pausa
                    raw_ |= 1u << 3;
                    c |= 1u << 31;
                    sig_ = i + 1;
                    sigue = false;
                    continue;
                default: break;
                }
                if (sigue || op == 2) c |= 1u << 31;
                if (!sigue && op != 4) sig_ = 0;
                if (i == 7) sig_ = 0;
            }
            if (sig_ >= 8) sig_ = 0;
            ocupado_ = sig_ != 0;
            evalua();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_I2C_ESP_H
