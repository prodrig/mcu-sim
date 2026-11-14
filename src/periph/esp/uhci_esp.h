// =============================================================================
// uhci_esp.h — El UHCI de los ESP32-C3: la UART con la GDMA
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §26.4.10 y §26.6; uhci_reg.h,
// uhci_ll.h y uhci.c (el driver «UART DMA») de ESP-IDF]. Une una de las dos
// UART (CONF0.UART0_CE o UART1_CE) a la GDMA (periférico 2):
//
// TRANSMISIÓN: los bytes de la GDMA van a la línea de la UART cuando su FIFO
// de transmisión está vacía. Con SEPER_EN, cada trama (hasta un descriptor con
// suc_eof) sale entre dos separadores (ESC_CONF0.SEPER_CHAR, 0xC0) y, según
// ESCAPE_CONF, los bytes iguales al separador, a 0xDB, a 0x11 o a 0x13 salen
// como su pareja de ESC_CONF0-3 (0xC0 -> DB DC, 0xDB -> DB DD...): la
// codificación SLIP de HCI. RX_START cuando sale un separador.
//
// RECEPCIÓN: los bytes que recibe la UART van a la GDMA y no a su FIFO. La
// trama acaba (IN_SUC_EOF de la GDMA) con LEN_EOF_EN al llegar a PKT_THRS
// bytes, con UART_IDLE_EOF_EN tras RX_IDLE_THRHD tiempos de bit de la UART
// sin recibir, con UART_RX_BRK_EOF_EN con un break, o, con SEPER_EN, en el
// separador del final (se quitan los separadores y se deshacen los escapes;
// TX_START con cada separador que llega).
//
// La interrupción, fuente 15 (INT_RAW 0x04: RX_START bit 0, TX_START bit 1,
// APP_CTRL0/1 bits 7 y 8, que pone el software). DATE 0x2007170.
//
// LO QUE NO SE MODELA (avisa una vez): la cabecera de HCI (HEAD_EN), el CRC y
// la suma de comprobación, los ACK, el envío rápido (QUICK_SENT) y HUNG.
// =============================================================================
#ifndef STM32_PERIPH_ESP_UHCI_ESP_H
#define STM32_PERIPH_ESP_UHCI_ESP_H

#include <cstdint>
#include <deque>
#include <functional>
#include "../../common/periph_base.h"
#include "gdma_esp.h"
#include "uart_esp.h"

namespace stm32 {

class UhciEsp : public BusSlave {
public:
    enum : unsigned { PERI_GDMA = 2 };
    std::function<void(bool)> irq;
    GdmaEsp* gdma = nullptr;
    UartEsp* uart[2] = {nullptr, nullptr};

    explicit UhciEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(UhciEsp);
        SC_THREAD(reposo_proc);
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        r_[0x00 / 4] = 0x6E0u; r_[0x14 / 4] = 0x33u; r_[0x20 / 4] = 0x33u; r_[0x24 / 4] = 0x810810u;
        r_[0x6C / 4] = 0xDCDBC0u; r_[0x70 / 4] = 0xDDDBDBu; r_[0x74 / 4] = 0xDEDB11u; r_[0x78 / 4] = 0xDFDB13u;
        r_[0x7C / 4] = 0x80u; r_[0x80 / 4] = 0x2007170u;
        raw_ = 0;
        pend_.clear();
        en_trama_ = esc_ = dentro_ = false;
        n_rx_ = 0;
        evalua();
    }
    // La UART unida (-1: ninguna)
    int unida() const {
        const uint32_t c = r_[0];
        return (c & 4u) ? 0 : (c & 8u) ? 1 : -1;
    }
    unsigned tramas_rx() const { return n_tramas_; }

    // Los ganchos de la UART n (los pone el SoC)
    bool toma_tx(unsigned n, uint8_t& b) {
        if (unida() != int(n)) return false;
        if (pend_.empty()) codifica();
        if (pend_.empty()) return false;
        b = pend_.front();
        pend_.pop_front();
        if (b == sep() && seper()) { raw_ |= 1u; evalua(); }       // RX_START: sale un separador
        return true;
    }
    bool da_rx(unsigned n, uint8_t b) {
        if (unida() != int(n)) return false;
        decodifica(b);
        ev_rx_.notify(sc_core::SC_ZERO_TIME);
        return true;
    }
    void brk_rx(unsigned n) {
        if (unida() == int(n) && ((r_[0] >> 12) & 1u)) cierra();
    }
    void despierta() { if (unida() >= 0 && uart[unida()]) uart[unida()]->despierta_tx(); }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x04: return raw_;
        case 0x08: return raw_ & r_[0x0C / 4];
        default: return off < 0x100 ? r_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x04: raw_ = (raw_ & ~(3u << 7)) | (v & (3u << 7)); evalua(); return;   // APP_CTRL0/1
        case 0x08: return;
        case 0x0C: r_[off / 4] = v; evalua(); return;
        case 0x10: raw_ &= ~v; evalua(); return;
        case 0x00:
            r_[0] = v & ~3u;
            if (v & 1u) { pend_.clear(); en_trama_ = false; }         // TX_RST
            if (v & 2u) { n_rx_ = 0; esc_ = dentro_ = false; }         // RX_RST
            if ((v >> 6) & 1u) avisa("la cabecera de HCI (HEAD_EN)");
            if (((v >> 7) & 1u) || ((v >> 10) & 1u)) avisa("el CRC (CRC_REC_EN, ENCODE_CRC_EN)");
            despierta();
            return;
        default:
            if (off < 0x100) r_[off / 4] = v;
            return;
        }
    }

private:
    uint32_t r_[64] = {};
    uint32_t raw_ = 0;
    bool irq_ = false, en_trama_ = false, esc_ = false, dentro_ = false;
    std::deque<uint8_t> pend_;
    unsigned n_rx_ = 0, n_tramas_ = 0;
    bool avisado_ = false;
    sc_core::sc_event ev_rx_;

    void avisa(const char* que) {
        if (avisado_) return;
        avisado_ = true;
        SC_REPORT_WARNING("uhci", (std::string(que) + ": no se modela").c_str());
    }
    void evalua() {
        const bool n = (raw_ & r_[0x0C / 4]) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    bool seper() const { return (r_[0] >> 5) & 1u; }
    uint8_t sep() const { return uint8_t(r_[0x6C / 4]); }
    // Las cuatro sustituciones: (carácter, sustituto 0, sustituto 1, bit de
    // ESCAPE_CONF en transmisión)
    struct Esc { uint8_t c, s0, s1; unsigned bit; };
    Esc esc(unsigned i) const {
        const uint32_t w = r_[(0x6C + 4 * i) / 4];
        return {uint8_t(w), uint8_t(w >> 8), uint8_t(w >> 16), i};
    }

    // Toma de la GDMA el siguiente byte y lo codifica en pend_
    void codifica() {
        if (!gdma) return;
        uint8_t b;
        if (gdma->saca(PERI_GDMA, &b, 1) != 1) return;
        const bool fin = gdma->eof_salida(PERI_GDMA);
        if (!seper()) { pend_.push_back(b); return; }
        if (!en_trama_) { pend_.push_back(sep()); en_trama_ = true; }
        bool sustituido = false;
        for (unsigned i = 0; i < 4 && !sustituido; ++i) {
            const Esc e = esc(i);
            if (b == e.c && ((r_[0x20 / 4] >> i) & 1u)) {
                pend_.push_back(e.s0);
                pend_.push_back(e.s1);
                sustituido = true;
            }
        }
        if (!sustituido) pend_.push_back(b);
        if (fin) { pend_.push_back(sep()); en_trama_ = false; }
    }

    void mete(uint8_t b) {
        if (gdma) gdma->mete(PERI_GDMA, &b, 1, false);
        ++n_rx_;
        if (((r_[0] >> 9) & 1u) && n_rx_ >= (r_[0x7C / 4] & 0x1FFFu)) cierra();   // LEN_EOF
    }
    void cierra() {
        if (gdma) gdma->mete(PERI_GDMA, nullptr, 0, true);
        n_rx_ = 0;
        ++n_tramas_;
    }
    void decodifica(uint8_t b) {
        if (!seper()) { mete(b); return; }
        if (b == sep()) {
            raw_ |= 2u;                                       // TX_START: llega un separador
            evalua();
            if (dentro_ && n_rx_) { cierra(); dentro_ = false; }
            else dentro_ = true;
            esc_ = false;
            return;
        }
        if (!dentro_) return;                                 // fuera de una trama: se tira
        if (esc_) {
            esc_ = false;
            for (unsigned i = 0; i < 4; ++i) {
                const Esc e = esc(i);
                if (b == e.s1 && ((r_[0x20 / 4] >> (4 + i)) & 1u)) { mete(e.c); return; }
            }
            mete(esc(1).s0);                                  // no era un escape
            mete(b);
            return;
        }
        for (unsigned i = 0; i < 4; ++i) {
            const Esc e = esc(i);
            if (b == e.s0 && ((r_[0x20 / 4] >> (4 + i)) & 1u)) { esc_ = true; return; }
        }
        mete(b);
    }

    // UART_IDLE_EOF: RX_IDLE_THRHD tiempos de bit sin recibir
    void reposo_proc() {
        for (;;) {
            sc_core::wait(ev_rx_);
            for (;;) {
                const int u = unida();
                if (u < 0 || !uart[u] || !((r_[0] >> 8) & 1u) || n_rx_ == 0) break;
                const sc_core::sc_time lim = uart[u]->ultimo_rx() + uart[u]->tiempo_bit() * double(uart[u]->umbral_reposo());
                if (sc_core::sc_time_stamp() < lim) { sc_core::wait(lim - sc_core::sc_time_stamp(), ev_rx_); continue; }
                cierra();
                break;
            }
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_UHCI_ESP_H
