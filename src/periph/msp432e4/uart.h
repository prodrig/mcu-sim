// =============================================================================
// uart.h — Las UART del MSP432E4 (T6, entrega 3)
//
// Ocho, UART0..7, de 4 KB a partir de 0x4000_C000. Es la UART de Tiva, que es
// una PL011 de ARM con añadidos de TI [H; hw_uart.h; segunda fuente: uart.c
// de DriverLib, que es la que dice en qué orden se tocan]:
//
//   DR      el dato; al leerlo, con los errores de ESE carácter en 11:8 (OE,
//           BE, PE, FE)
//   RSR/ECR los errores del último leído; escribir borra
//   FR      TXFE, RXFF, TXFF, RXFE, BUSY
//   IBRD/FBRD  BRD = reloj / (16 · baudios) (/8 con HSE): parte entera y
//           fraccionaria en 64avos. Se leen al escribir LCRH, como en la PL011
//   LCRH    BRK, PEN, EPS, STP2, FEN (las FIFO de 16), WLEN (5-8 bits), SPS
//   CTL     UARTEN, TXE, RXE, LBE (bucle interno), HSE, EOT
//   IFLS    los niveles de las FIFO que dan interrupción: 1/8 a 7/8
//   IM/RIS/MIS/ICR   RX, TX, RT (el receptor lleva 32 bits sin recibir con algo
//           en la FIFO), FE, PE, BE, OE, EOT, y DMARX/DMATX
//   DMACTL  RXDMAE, TXDMAE, DMAERR
//   CC      el reloj: SYSCLK (0) o ALTCLK (5, PIOSC)
//
// LA LÍNEA va bit a bit por los pines, como la eUSCI del P4: el emisor
// conduce UnTx con los tiempos de BRD y el receptor muestrea UnRx en el centro
// de cada bit desde el flanco del arranque.
//
// LA INTERRUPCIÓN DE TX es de cruce, como en la PL011: sube cuando la FIFO
// BAJA hasta el nivel de IFLS, no mientras esté por debajo; sin FIFO, cuando
// el registro queda vacío; con EOT, cuando acaba el último bit. Se baja con ICR
// o escribiendo hasta pasar el nivel.
//
// LO QUE NO ESTÁ: el modo IrDA (SIR), el de tarjeta (SMART), el de 9 bits, y
// el control de flujo y las líneas de módem (RTS/CTS, DTR/DSR/DCD/RI): sus
// registros se guardan y no hacen nada. [P-17 i]
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_UART_H
#define STM32_PERIPH_MSP432E4_UART_H

#include <deque>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"
#include "gpio.h"

namespace stm32 {

class UartE4 : public BusSlave {
public:
    enum : uint32_t {
        R_DR = 0x000, R_RSR = 0x004, R_FR = 0x018, R_ILPR = 0x020, R_IBRD = 0x024,
        R_FBRD = 0x028, R_LCRH = 0x02C, R_CTL = 0x030, R_IFLS = 0x034, R_IM = 0x038,
        R_RIS = 0x03C, R_MIS = 0x040, R_ICR = 0x044, R_DMACTL = 0x048,
        R_9BITADDR = 0x0A4, R_9BITAMASK = 0x0A8, R_PP = 0xFC0, R_CC = 0xFC8
    };
    static constexpr uint32_t I_RX = 1u << 4, I_TX = 1u << 5, I_RT = 1u << 6,
        I_FE = 1u << 7, I_PE = 1u << 8, I_BE = 1u << 9, I_OE = 1u << 10,
        I_EOT = 1u << 11, I_DMARX = 1u << 16, I_DMATX = 1u << 17;
    static constexpr uint32_t CTL_UARTEN = 1u << 0, CTL_HSE = 1u << 5, CTL_EOT = 1u << 4,
        CTL_LBE = 1u << 7, CTL_TXE = 1u << 8, CTL_RXE = 1u << 9;
    static constexpr unsigned FIFO = 16;

    sc_core::sc_out<bool> irq{"irq"};
    // Los pines: TX lo conduce la UART; RX lo escribe el GPIO
    sc_core::sc_signal<bool> tx{"tx"}, rx{"rx"}, uno{"uno"};
    // Las peticiones a la uDMA (niveles) y el fin de un ciclo de DMA
    sc_core::sc_signal<bool> dma_rx{"dma_rx"}, dma_tx{"dma_tx"};
    void dma_hecho(bool es_tx) { ris_ |= es_tx ? I_DMATX : I_DMARX; actualiza(); }
    // El reloj: 0 SYSCLK, 5 ALTCLK
    std::function<double(unsigned cs)> f_reloj;

    UartE4(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(UartE4);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        SC_THREAD(rt_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        tx.write(true); uno.write(true);
    }
    // TX lo escribe SOLO pub_proc: lo cambian el emisor, LCRH.BRK y el reset
    FuncionE4 fn_tx() { FuncionE4 f; f.out = &tx; f.oe = &uno; f.nombre = "UnTx"; return f; }
    FuncionE4 fn_rx() { FuncionE4 f; f.in = &rx; f.idle_in = true; f.nombre = "UnRx"; return f; }

    void recalcula() { ev_.notify(sc_core::SC_ZERO_TIME); }
    unsigned enviados() const { return n_tx_; }
    unsigned recibidos() const { return n_rx_; }
    unsigned errores() const { return n_err_; }
    double baudios() const {
        const double f = reloj();
        const double brd = double(ibrd_l_) + double(fbrd_l_) / 64.0;
        return (f > 0 && brd > 0) ? f / ((ctl_ & CTL_HSE ? 8.0 : 16.0) * brd) : 0.0;
    }

protected:
    uint32_t ibrd_ = 0, fbrd_ = 0, ibrd_l_ = 0, fbrd_l_ = 0, lcrh_ = 0, ctl_ = 0x300,
             ifls_ = 0x12, im_ = 0, ris_ = 0, dmactl_ = 0, rsr_ = 0, cc_ = 0, ilpr_ = 0,
             addr9_ = 0, amask9_ = 0xFF;
    std::deque<uint8_t>  txf_;
    std::deque<uint16_t> rxf_;              // dato + errores en 11:8
    bool tx_ocupado_ = false, o_tx_ = true;
    void pon_tx(bool v) { o_tx_ = v; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    sc_core::sc_time ult_rx_;
    unsigned n_tx_ = 0, n_rx_ = 0, n_err_ = 0;
    sc_core::sc_event ev_, pub_ev_, rx_ev_;

    double reloj() const { return f_reloj ? f_reloj(cc_ & 0xFu) : 0.0; }
    bool fifo() const { return (lcrh_ & (1u << 4)) != 0; }
    unsigned prof() const { return fifo() ? FIFO : 1u; }
    unsigned bits() const { return 5u + ((lcrh_ >> 5) & 3u); }
    bool habilitada() const { return (ctl_ & CTL_UARTEN) != 0; }
    sc_core::sc_time t_bit() const {
        const double b = baudios();
        return b > 0 ? sc_core::sc_time(1.0 / b, sc_core::SC_SEC) : sc_core::SC_ZERO_TIME;
    }
    static unsigned nivel(unsigned sel) {       // en caracteres, de 16
        static const unsigned n[8] = {2, 4, 8, 12, 14, 8, 8, 8};
        return n[sel & 7u];
    }
    bool linea_rx() const { return (ctl_ & CTL_LBE) ? o_tx_ : rx.read(); }
    const sc_core::sc_event& flanco_rx() const {
        return (ctl_ & CTL_LBE) ? tx.value_changed_event() : rx.value_changed_event();
    }

    void valores_de_reset() {
        ibrd_ = fbrd_ = ibrd_l_ = fbrd_l_ = lcrh_ = 0;
        ctl_ = 0x300; ifls_ = 0x12; im_ = ris_ = dmactl_ = rsr_ = cc_ = ilpr_ = 0;
        addr9_ = 0; amask9_ = 0xFF;
        txf_.clear(); rxf_.clear();
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        pon_tx(true);
        actualiza();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void actualiza() {
        // RX: por nivel; sin FIFO, con un carácter
        if (fifo() ? rxf_.size() >= nivel((ifls_ >> 3) & 7u) : !rxf_.empty()) ris_ |= I_RX;
        else ris_ &= ~I_RX;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        if (tx.read() != o_tx_) tx.write(o_tx_);
        irq.write((ris_ & im_) != 0);
        dma_tx.write((dmactl_ & 2u) && habilitada() && txf_.size() < prof());
        dma_rx.write((dmactl_ & 1u) && habilitada() && !rxf_.empty());
    }
    // La TX baja hasta el nivel (o se vacía, sin FIFO): sube TXRIS
    void tx_vacia_hasta(size_t antes) {
        if (ctl_ & CTL_EOT) return;
        if (fifo()) {
            const size_t n = nivel(ifls_ & 7u);
            if (antes > n && txf_.size() <= n) ris_ |= I_TX;
        } else if (txf_.empty()) {
            ris_ |= I_TX;
        }
    }

    void tx_proc() {
        for (;;) {
            if (!habilitada() || !(ctl_ & CTL_TXE) || txf_.empty() || baudios() <= 0) {
                wait(ev_);
                continue;
            }
            const uint8_t d = txf_.front();
            const size_t antes = txf_.size();
            txf_.pop_front();
            tx_vacia_hasta(antes);
            tx_ocupado_ = true;
            actualiza();
            const sc_core::sc_time tb = t_bit();
            std::vector<bool> b;
            const unsigned n = bits();
            unsigned unos = 0;
            for (unsigned i = 0; i < n; ++i) { const bool v = (d >> i) & 1u; b.push_back(v); unos += v; }
            if (lcrh_ & 2u) {
                const bool par = (lcrh_ & 4u) != 0;               // EPS: par
                bool p = par ? (unos & 1u) : !(unos & 1u);
                if (lcrh_ & 0x80u) p = !par;                       // SPS: fija
                b.push_back(p);
            }
            pon_tx(false); wait(tb);                              // arranque
            for (bool v : b) { pon_tx(v); wait(tb); }
            pon_tx(true);
            wait(tb * ((lcrh_ & 8u) ? 2.0 : 1.0));                // parada
            tx_ocupado_ = false;
            ++n_tx_;
            if ((ctl_ & CTL_EOT) && txf_.empty()) ris_ |= I_EOT | I_TX;
            actualiza();
        }
    }

    void rx_proc() {
        for (;;) {
            if (!habilitada() || !(ctl_ & CTL_RXE) || baudios() <= 0) { wait(ev_ | flanco_rx()); continue; }
            if (!linea_rx()) { wait(flanco_rx() | ev_); continue; }
            wait(flanco_rx() | ev_);
            if (linea_rx() || !habilitada()) continue;
            const sc_core::sc_time tb = t_bit();
            wait(tb / 2.0);
            if (linea_rx()) continue;                            // falso arranque
            const unsigned n = bits();
            const bool pen = (lcrh_ & 2u) != 0;
            uint16_t d = 0;
            unsigned unos = 0;
            bool todo_cero = true;
            for (unsigned i = 0; i < n; ++i) {
                wait(tb);
                const bool v = linea_rx();
                if (v) { d = uint16_t(d | (1u << i)); ++unos; todo_cero = false; }
            }
            bool err_p = false;
            if (pen) {
                wait(tb);
                const bool p = linea_rx();
                if (p) todo_cero = false;
                const bool par = (lcrh_ & 4u) != 0;
                bool esperado = par ? (unos & 1u) : !(unos & 1u);
                if (lcrh_ & 0x80u) esperado = !par;
                err_p = p != esperado;
            }
            wait(tb);
            const bool parada = linea_rx();
            uint16_t e = 0;
            if (!parada) e |= 0x100;                              // FE
            if (err_p) e |= 0x200;                                // PE
            if (todo_cero && !parada) { e |= 0x400; d = 0; }      // BE
            ++n_rx_;
            if (e) ++n_err_;
            if (rxf_.size() >= prof()) {
                ris_ |= I_OE;
                rsr_ |= 8u;
                if (!rxf_.empty()) rxf_.back() |= 0x800;          // OE en el último
            } else {
                rxf_.push_back(uint16_t(d | e));
            }
            if (e & 0x100) ris_ |= I_FE;
            if (e & 0x200) ris_ |= I_PE;
            if (e & 0x400) ris_ |= I_BE;
            ult_rx_ = sc_core::sc_time_stamp();
            rx_ev_.notify(sc_core::SC_ZERO_TIME);
            actualiza();
            // Medio bit de parada más, para no ver el siguiente arranque tarde
            if (!parada) { while (!linea_rx()) wait(flanco_rx() | ev_); }
        }
    }
    // El de tiempo de espera: 32 bits sin nada nuevo y algo en la FIFO
    void rt_proc() {
        for (;;) {
            if (rxf_.empty() || baudios() <= 0) { wait(rx_ev_ | ev_); continue; }
            const sc_core::sc_time lim = ult_rx_ + t_bit() * 32.0;
            if (sc_core::sc_time_stamp() < lim) { wait(lim - sc_core::sc_time_stamp(), rx_ev_ | ev_); continue; }
            if (!(ris_ & I_RT)) { ris_ |= I_RT; actualiza(); }
            wait(rx_ev_ | ev_);
        }
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_DR: {
                if (rxf_.empty()) return 0;
                const uint16_t v = rxf_.front();
                rxf_.pop_front();
                rsr_ = (v >> 8) & 0xFu;
                ult_rx_ = sc_core::sc_time_stamp();
                if (rxf_.empty()) ris_ &= ~I_RT;
                actualiza();
                ev_.notify(sc_core::SC_ZERO_TIME);
                return v;
            }
            case R_RSR: return rsr_;
            case R_FR: {
                uint32_t f = 0;
                if (txf_.empty()) f |= 1u << 7;
                if (rxf_.size() >= prof()) f |= 1u << 6;
                if (txf_.size() >= prof()) f |= 1u << 5;
                if (rxf_.empty()) f |= 1u << 4;
                if (tx_ocupado_ || !txf_.empty()) f |= 1u << 3;
                return f;
            }
            case R_ILPR: return ilpr_;
            case R_IBRD: return ibrd_;
            case R_FBRD: return fbrd_;
            case R_LCRH: return lcrh_;
            case R_CTL:  return ctl_;
            case R_IFLS: return ifls_;
            case R_IM:   return im_;
            case R_RIS:  return ris_;
            case R_MIS:  return ris_ & im_;
            case R_DMACTL: return dmactl_;
            case R_9BITADDR: return addr9_;
            case R_9BITAMASK: return amask9_;
            case R_PP: return 0x0000000Fu;          // SC, NB, MS, MSE [1F]
            case R_CC: return cc_;
            // PrimeCell [H: UART_PeriphID/PCellID]
            case 0xFE0: return 0x60; case 0xFE4: return 0x00; case 0xFE8: return 0x18;
            case 0xFEC: return 0x01; case 0xFF0: return 0x0D; case 0xFF4: return 0xF0;
            case 0xFF8: return 0x05; case 0xFFC: return 0xB1;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
            case R_DR:
                if (txf_.size() < prof()) txf_.push_back(uint8_t(v));
                if (fifo() && txf_.size() > nivel(ifls_ & 7u)) ris_ &= ~I_TX;
                if (!fifo()) ris_ &= ~I_TX;
                ris_ &= ~I_EOT;
                break;
            case R_RSR: rsr_ = 0; break;
            case R_ILPR: ilpr_ = v & 0xFFu; break;
            case R_IBRD: ibrd_ = v & 0xFFFFu; break;
            case R_FBRD: fbrd_ = v & 0x3Fu; break;
            case R_LCRH:
                lcrh_ = v & 0xFFu;
                ibrd_l_ = ibrd_; fbrd_l_ = fbrd_;     // la PL011 los toma al escribir LCRH
                if (!fifo()) { while (txf_.size() > 1) txf_.pop_back(); while (rxf_.size() > 1) rxf_.pop_back(); }
                if (lcrh_ & 1u) pon_tx(false);        // BRK
                else if (!tx_ocupado_) pon_tx(true);
                break;
            case R_CTL: ctl_ = v & 0xFFBFu; break;
            case R_IFLS: ifls_ = v & 0x3Fu; break;
            case R_IM: im_ = v & 0x31FF7u; break;
            case R_ICR: ris_ &= ~v; break;
            case R_DMACTL: dmactl_ = v & 7u; break;
            case R_9BITADDR: addr9_ = v & 0x80FFu; break;
            case R_9BITAMASK: amask9_ = v & 0xFFu; break;
            case R_CC: cc_ = v & 0xFu; break;
            default: return;
        }
        actualiza();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_UART_H
