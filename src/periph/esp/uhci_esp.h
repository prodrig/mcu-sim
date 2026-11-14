// =============================================================================
// uhci_esp.h — El UHCI de los ESP32-C3: la UART con la GDMA
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §26.4.10, §26.4.12 y §26.6-7;
// uhci_reg.h, uhci_struct.h, uhci_ll.h y uhci.c (el driver «UART DMA») de
// ESP-IDF; los comentarios de uhci_reg.h del ESP32; el transporte «Three-wire
// UART» (H5) de Bluetooth, Core v5.x Vol 4 Part D, para el formato de la
// cabecera y del CRC, que el TRM no da]. Une una de las dos UART
// (CONF0.UART0_CE o UART1_CE) a la GDMA (periférico 2).
//
// TRANSMISIÓN: los bytes de la GDMA van a la línea de la UART cuando su FIFO
// de transmisión está vacía. Con SEPER_EN, cada trama (hasta un descriptor con
// suc_eof) sale entre dos separadores (ESC_CONF0.SEPER_CHAR, 0xC0) y, según
// ESCAPE_CONF, los bytes iguales al separador, a 0xDB, a 0x11 o a 0x13 salen
// como su pareja de ESC_CONF0-3 (0xC0 -> DB DC, 0xDB -> DB DD...): la
// codificación SLIP. RX_START cuando sale un separador.
//
// LA CABECERA DE HCI (HEAD_EN), la de H5: cuatro bytes, {seq[2:0], ack[5:3],
// DIC[6] (lleva CRC), fiable[7]}, {tipo[3:0], largo[3:0]}, {largo[11:4]} y la
// suma (la de los cuatro, módulo 256, es 0xFF). Al mandar, son los cuatro
// primeros bytes de la trama de la GDMA; con TX_ACK_NUM_RE el hardware pone
// en ack el suyo (ACK_NUM) y con TX_CHECK_SUM_RE, la suma. Con DIC y
// ENCODE_CRC_EN, tras los datos va el CRC de H5: CRC-CCITT reflejado (0x8408,
// desde 0xFFFF) de cabecera y datos, dado la vuelta y el byte alto primero. Si
// la trama de la GDMA no mide 4 + largo, OUTLINK_EOF_ERR.
// WAIT_SW_START: antes de cada trama de la GDMA el codificador espera
// (ENCODE_STATE = 7, ST_SW_WAIT) a que el programa escriba SW_START.
// EL ENVÍO RÁPIDO (QUICK_SENT): REG_Qn_WORD0/1 son 8 bytes (cabecera y hasta
// 4 de datos con HEAD_EN). SINGLE_SEND_EN manda el Q[SINGLE_SEND_NUM] una vez
// y se borra solo (SEND_S_REG_Q al acabar); ALWAYS_SEND_EN manda el
// Q[ALWAYS_SEND_NUM] cada vez que la línea queda libre (SEND_A_REG_Q con cada
// uno) hasta que se borra. El rápido va antes que la GDMA.
//
// RECEPCIÓN: los bytes que recibe la UART van a la GDMA y no a su FIFO. La
// trama acaba (IN_SUC_EOF de la GDMA) con LEN_EOF_EN al llegar a PKT_THRS
// bytes (o, con HEAD_EN, al largo de la cabecera), con UART_IDLE_EOF_EN tras
// RX_IDLE_THRHD tiempos de bit de la UART sin recibir, con UART_RX_BRK_EOF_EN
// con un break, o, con SEPER_EN, en el separador del final (se quitan los
// separadores y se deshacen los escapes; TX_START con cada separador que
// llega; el separador del final vale de principio para la siguiente).
// Con HEAD_EN, la cabecera va a RX_HEAD y, con SAVE_HEAD, también a la
// memoria. Los errores, en STATE0.RX_ERR_CAUSE, cierran la trama con err_eof
// e IN_ERR_EOF de la GDMA y tiran el resto hasta el separador: 1 la suma
// (CHECK_SUM_EN), 2 el número de secuencia de una trama fiable (CHECK_SEQ_EN;
// se espera el ACK_NUM del UHCI, que sube con cada trama fiable buena), 3 DIC
// sin CRC_REC_EN, 4 un separador antes del final, 5 sin separador al final, 6
// el CRC (salvo con CRC_DISABLE, que solo lo quita).
//
// HUNG: TX_HUNG cuando, a mitad de una trama, la GDMA no da bytes durante
// TXFIFO_TIMEOUT << TXFIFO_TIMEOUT_SHIFT ciclos del APB (con su ENA); RX_HUNG
// cuando hay bytes recibidos que la GDMA no puede tomar (sin canal de
// entrada) durante RXFIFO_TIMEOUT << SHIFT. Esos bytes esperan, hasta 128
// (la FIFO de la UART), a que se arranque un canal de entrada.
//
// La interrupción, fuente 15 (INT_RAW 0x04: RX_START 0, TX_START 1, RX_HUNG
// 2, TX_HUNG 3, SEND_S_REG_Q 4, SEND_A_REG_Q 5, OUTLINK_EOF_ERR 6, APP_CTRL0
// 7 y APP_CTRL1 8, estos dos los pone el software). DATE 0x2007170.
//
// LO QUE ES DEL MODELO (el TRM no lo dice): los valores de DECODE_STATE (0
// libre, 1 cabecera, 2 datos, 3 CRC, 4 esperando el separador) y de
// ENCODE_STATE (0 libre, 1 mandando, 7 ST_SW_WAIT), la unidad de HUNG y lo de
// DIC sin CRC_REC_EN (error 3). CRC_DISABLE se toma como dice su nombre y el
// uhci_reg.h del ESP32 («disable crc calculation»): el TRM del C3 dice lo
// contrario.
// =============================================================================
#ifndef STM32_PERIPH_ESP_UHCI_ESP_H
#define STM32_PERIPH_ESP_UHCI_ESP_H

#include <algorithm>
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
    double hz_apb = 80e6;

    explicit UhciEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(UhciEsp);
        SC_THREAD(reposo_proc);
        SC_THREAD(hung_proc);
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        r_[0x00 / 4] = 0x6E0u; r_[0x14 / 4] = 0x33u; r_[0x20 / 4] = 0x33u; r_[0x24 / 4] = 0x810810u;
        r_[0x6C / 4] = 0xDCDBC0u; r_[0x70 / 4] = 0xDDDBDBu; r_[0x74 / 4] = 0xDEDB11u; r_[0x78 / 4] = 0xDFDB13u;
        r_[0x7C / 4] = 0x80u; r_[0x80 / 4] = 0x2007170u;
        raw_ = 0;
        reset_tx();
        reset_rx();
        ack_ = 0;
        rx_pend_.clear();
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
        const uint16_t e = pend_.front();
        pend_.pop_front();
        b = uint8_t(e);
        if ((e & F_SEP) && seper()) raw_ |= 1u;                          // RX_START: sale un separador
        if (e & F_SEND_S) { raw_ |= 1u << 4; r_[0x30 / 4] &= ~(1u << 3); }
        if (e & F_SEND_A) raw_ |= 1u << 5;
        if (pend_.empty() && tx_ == TX_RAPIDO) tx_ = TX_LIBRE;
        evalua();
        return true;
    }
    bool da_rx(unsigned n, uint8_t b) {
        if (unida() != int(n)) return false;
        decodifica(b);
        ev_rx_.notify(sc_core::SC_ZERO_TIME);
        return true;
    }
    void brk_rx(unsigned n) {
        if (unida() == int(n) && ((r_[0] >> 12) & 1u)) fin_paquete();
    }
    void despierta() { const int u = unida(); if (u >= 0 && uart[u]) uart[u]->despierta_tx(); }
    // Se ha arrancado un canal de entrada: lo que esperaba, a la GDMA
    void vacia_rx() {
        while (!rx_pend_.empty() && gdma && gdma->entrada_activa(PERI_GDMA)) {
            const uint16_t e = rx_pend_.front();
            const bool eof = e & P_EOF, err = e & P_ERR, dato = !(e & P_SOLO_EOF);
            const uint8_t b = uint8_t(e);
            if (gdma->mete(PERI_GDMA, dato ? &b : nullptr, dato ? 1u : 0u, eof, err) != (dato ? 1u : 0u)) break;
            rx_pend_.pop_front();
        }
        if (!rx_pend_.empty()) { t_pend_rx_ = sc_core::sc_time_stamp(); hung_rx_dado_ = false; }
        ev_hung_.notify(sc_core::SC_ZERO_TIME);
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x04: return raw_;
        case 0x08: return raw_ & r_[0x0C / 4];
        case 0x18: return (causa_ & 7u) | (uint32_t(fase_rx()) << 3);
        case 0x1C: return tx_ == TX_ESPERA_SW ? 7u : tx_ == TX_LIBRE ? 0u : 1u;
        case 0x28: return ack_ & 7u;
        case 0x2C: return rx_head_;
        default: return off < 0x100 ? r_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x04: raw_ = (raw_ & ~(3u << 7)) | (v & (3u << 7)); evalua(); return;   // APP_CTRL0/1
        case 0x08: case 0x18: case 0x1C: case 0x2C: return;
        case 0x0C: r_[off / 4] = v; evalua(); return;
        case 0x10: raw_ &= ~v; evalua(); return;
        case 0x00:
            r_[0] = v & ~3u;
            if (v & 1u) reset_tx();                                    // TX_RST
            if (v & 2u) reset_rx();                                    // RX_RST
            despierta();
            return;
        case 0x14:                                                    // CONF1: SW_START
            r_[off / 4] = v;
            if ((v >> 8) & 1u) {
                r_[off / 4] &= ~(1u << 8);
                sw_go_ = true;
                if (tx_ == TX_ESPERA_SW) tx_ = TX_LIBRE;
                despierta();
            }
            return;
        case 0x28:                                                    // ACK_NUM_LOAD
            if ((v >> 3) & 1u) ack_ = v & 7u;
            r_[off / 4] = v & 7u;
            return;
        case 0x30:                                                    // QUICK_SENT
            r_[off / 4] = v & 0xFFu;
            despierta();
            return;
        default:
            if (off < 0x100) r_[off / 4] = v;
            if (off == 0x24) ev_hung_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
    }

private:
    enum : uint16_t { F_SEP = 0x100, F_SEND_S = 0x200, F_SEND_A = 0x400 };
    enum : uint16_t { P_EOF = 0x100, P_ERR = 0x200, P_SOLO_EOF = 0x400 };
    enum Tx { TX_LIBRE, TX_ESPERA_SW, TX_GDMA, TX_RAPIDO };
    enum Rx { RX_CAB = 1, RX_DATOS = 2, RX_CRC = 3, RX_FIN = 4 };

    uint32_t r_[64] = {};
    uint32_t raw_ = 0;
    bool irq_ = false;
    // Transmisión
    Tx tx_ = TX_LIBRE;
    bool sw_go_ = false;
    std::deque<uint16_t> pend_;
    uint8_t cab_tx_[4] = {};
    unsigned n_tx_ = 0;
    uint16_t crc_tx_ = 0xFFFF;
    bool hambre_tx_ = false, hung_tx_dado_ = false;
    sc_core::sc_time t_hambre_tx_;
    // Recepción
    bool esc_ = false, dentro_ = false, tirando_ = false;
    unsigned n_rx_ = 0, n_tramas_ = 0;
    Rx fase_ = RX_CAB;
    uint8_t cab_rx_[4] = {};
    unsigned n_cab_ = 0, largo_ = 0, n_dat_ = 0, n_crc_ = 0;
    uint8_t crc_rx_[2] = {};
    std::deque<uint8_t> retraso_;
    uint16_t crc_calc_ = 0xFFFF;
    uint32_t rx_head_ = 0, causa_ = 0;
    unsigned ack_ = 0;
    std::deque<uint16_t> rx_pend_;
    bool hung_rx_dado_ = false;
    sc_core::sc_time t_pend_rx_;
    sc_core::sc_event ev_rx_, ev_hung_;

    void evalua() {
        const bool n = (raw_ & r_[0x0C / 4]) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    bool seper() const { return (r_[0] >> 5) & 1u; }
    bool cabecera() const { return (r_[0] >> 6) & 1u; }
    uint8_t sep() const { return uint8_t(r_[0x6C / 4]); }
    unsigned fase_rx() const { return (cabecera() && (dentro_ || !seper()) && !tirando_ && (n_cab_ || fase_ != RX_CAB)) ? unsigned(fase_) : 0u; }
    // Las cuatro sustituciones: (carácter, sustituto 0, sustituto 1)
    struct Esc { uint8_t c, s0, s1; };
    Esc esc(unsigned i) const {
        const uint32_t w = r_[(0x6C + 4 * i) / 4];
        return {uint8_t(w), uint8_t(w >> 8), uint8_t(w >> 16)};
    }
    static uint16_t crc_paso(uint16_t c, uint8_t b) {
        c ^= b;
        for (unsigned i = 0; i < 8; ++i) c = (c & 1u) ? uint16_t((c >> 1) ^ 0x8408u) : uint16_t(c >> 1);
        return c;
    }
    static uint16_t vuelta16(uint16_t v) {
        uint16_t r = 0;
        for (unsigned i = 0; i < 16; ++i) if (v & (1u << i)) r |= uint16_t(1u << (15 - i));
        return r;
    }
    static unsigned largo_de(const uint8_t* h) { return (h[1] >> 4) | (unsigned(h[2]) << 4); }

    void reset_tx() {
        pend_.clear();
        tx_ = TX_LIBRE;
        sw_go_ = false;
        n_tx_ = 0;
        hambre_tx_ = hung_tx_dado_ = false;
    }
    void reset_rx() {
        esc_ = dentro_ = tirando_ = false;
        n_rx_ = 0;
        empieza_rx();
        causa_ = 0;
    }
    void empieza_rx() {
        fase_ = RX_CAB;
        n_cab_ = largo_ = n_dat_ = n_crc_ = 0;
        retraso_.clear();
        crc_calc_ = 0xFFFF;
    }

    // --- Transmisión --------------------------------------------------------
    // Un byte de la trama, con sus escapes (con SEPER_EN)
    void emite(uint8_t b, uint16_t fl = 0) {
        if (seper())
            for (unsigned i = 0; i < 4; ++i) {
                const Esc e = esc(i);
                if (b == e.c && ((r_[0x20 / 4] >> i) & 1u)) {
                    pend_.push_back(e.s0);
                    pend_.push_back(uint16_t(e.s1 | fl));
                    return;
                }
            }
        pend_.push_back(uint16_t(b | fl));
    }
    void arregla_cab(uint8_t* h) const {
        const uint32_t c1 = r_[0x14 / 4];
        if ((c1 >> 5) & 1u) h[0] = uint8_t((h[0] & ~0x38u) | (ack_ << 3));     // TX_ACK_NUM_RE
        if ((c1 >> 4) & 1u) h[3] = uint8_t(~(h[0] + h[1] + h[2]));             // TX_CHECK_SUM_RE
    }
    bool con_crc_tx(const uint8_t* h) const { return ((h[0] >> 6) & 1u) && ((r_[0] >> 10) & 1u); }
    void emite_crc(uint16_t c, uint16_t fl = 0) {
        const uint16_t r = vuelta16(c);
        emite(uint8_t(r >> 8));
        emite(uint8_t(r), fl);
    }
    // Un paquete rápido entero: Q[n] (WORD0 y WORD1)
    void rapido(unsigned q, uint16_t fl) {
        uint8_t b[8];
        for (unsigned i = 0; i < 8; ++i) b[i] = uint8_t(r_[(0x34 + 8 * q + 4 * (i / 4)) / 4] >> (8 * (i % 4)));
        std::deque<uint8_t> trama;
        uint16_t crc = 0xFFFF;
        unsigned n = 8;
        if (seper()) pend_.push_back(uint16_t(sep() | F_SEP));
        if (cabecera()) {
            arregla_cab(b);
            n = 4 + std::min(largo_de(b), 4u);
        }
        const bool crc_si = cabecera() && con_crc_tx(b);
        for (unsigned i = 0; i < n; ++i) {
            crc = crc_paso(crc, b[i]);
            emite(b[i], (!seper() && !crc_si && i + 1 == n) ? fl : 0);
        }
        if (crc_si) emite_crc(crc, seper() ? 0 : fl);
        if (seper()) pend_.push_back(uint16_t(sep() | F_SEP | fl));
        tx_ = TX_RAPIDO;
    }
    // Lo siguiente que sale, a pend_ (la cabecera se junta antes de salir)
    void codifica() {
        while (pend_.empty() && paso()) {}
    }
    // Un paso del codificador; false si no ha podido avanzar
    bool paso() {
        const uint32_t qs = r_[0x30 / 4];
        if (tx_ == TX_LIBRE && ((qs >> 3) & 1u)) { rapido(qs & 7u, F_SEND_S); return true; }
        if (tx_ == TX_LIBRE && gdma && gdma->salida_activa(PERI_GDMA)) {
            if (((r_[0x14 / 4] >> 7) & 1u) && !sw_go_) { tx_ = TX_ESPERA_SW; return false; }
            sw_go_ = false;
        }
        if (tx_ == TX_ESPERA_SW || tx_ == TX_RAPIDO || !gdma) return false;
        uint8_t b;
        if (gdma->saca(PERI_GDMA, &b, 1) != 1) {
            if (tx_ == TX_GDMA && !hambre_tx_) {
                hambre_tx_ = true; hung_tx_dado_ = false;
                t_hambre_tx_ = sc_core::sc_time_stamp();
                ev_hung_.notify(sc_core::SC_ZERO_TIME);
            }
            if (tx_ == TX_LIBRE && ((qs >> 7) & 1u)) { rapido((qs >> 4) & 7u, F_SEND_A); return true; }
            return false;
        }
        hambre_tx_ = false;
        const bool fin = gdma->eof_salida(PERI_GDMA);
        if (tx_ == TX_LIBRE) {
            tx_ = TX_GDMA;
            n_tx_ = 0;
            crc_tx_ = 0xFFFF;
            if (seper()) pend_.push_back(uint16_t(sep() | F_SEP));
        }
        if (cabecera() && n_tx_ < 4) {
            cab_tx_[n_tx_++] = b;
            if (n_tx_ == 4) {
                arregla_cab(cab_tx_);
                for (unsigned i = 0; i < 4; ++i) { crc_tx_ = crc_paso(crc_tx_, cab_tx_[i]); emite(cab_tx_[i]); }
            }
        } else {
            ++n_tx_;
            crc_tx_ = crc_paso(crc_tx_, b);
            emite(b);
        }
        if (fin) {
            if (cabecera()) {
                if (n_tx_ < 4) for (unsigned i = 0; i < n_tx_; ++i) emite(cab_tx_[i]);
                if (n_tx_ < 4 || n_tx_ != 4 + largo_de(cab_tx_)) raw_ |= 1u << 6;     // OUTLINK_EOF_ERR
                else if (con_crc_tx(cab_tx_)) emite_crc(crc_tx_);
                evalua();
            }
            if (seper()) pend_.push_back(uint16_t(sep() | F_SEP));
            tx_ = TX_LIBRE;
        }
        return true;
    }

    // --- Recepción ----------------------------------------------------------
    // A la GDMA (o a esperar, si no puede tomarlo)
    void a_gdma(const uint8_t* b, bool eof, bool err) {
        const unsigned n = b ? 1u : 0u;
        if (rx_pend_.empty() && gdma && gdma->entrada_activa(PERI_GDMA) && gdma->mete(PERI_GDMA, b, n, eof, err) == n) return;
        if (rx_pend_.size() >= 128) return;                           // la FIFO de la UART, llena
        if (rx_pend_.empty()) { t_pend_rx_ = sc_core::sc_time_stamp(); hung_rx_dado_ = false; ev_hung_.notify(sc_core::SC_ZERO_TIME); }
        rx_pend_.push_back(uint16_t((b ? unsigned(*b) : 0u) | (eof ? unsigned(P_EOF) : 0u) | (err ? unsigned(P_ERR) : 0u) | (b ? 0u : unsigned(P_SOLO_EOF))));
    }
    void mete(uint8_t b) {
        a_gdma(&b, false, false);
        if (!cabecera() && ((r_[0] >> 9) & 1u) && n_rx_ >= (r_[0x7C / 4] & 0x1FFFu)) cierra(false);   // LEN_EOF
    }
    void cierra(bool err) {
        a_gdma(nullptr, true, err);
        n_rx_ = 0;
        ++n_tramas_;
        empieza_rx();
    }
    void error(unsigned c) {
        causa_ = c;
        cierra(true);
        tirando_ = seper();                                           // hasta el separador
    }
    // Un byte ya sin escapes
    void recibe(uint8_t d) {
        if (tirando_) return;
        ++n_rx_;
        if (!cabecera()) { mete(d); return; }
        const uint32_t c0 = r_[0], c1 = r_[0x14 / 4];
        const bool len_eof = (c0 >> 9) & 1u, crc_rec = (c0 >> 7) & 1u;
        switch (fase_) {
        case RX_CAB: {
            cab_rx_[n_cab_++] = d;
            if (n_cab_ < 4) return;
            const uint8_t* h = cab_rx_;
            rx_head_ = h[0] | (uint32_t(h[1]) << 8) | (uint32_t(h[2]) << 16) | (uint32_t(h[3]) << 24);
            if ((c1 >> 3) & 1u) for (unsigned i = 0; i < 4; ++i) a_gdma(&h[i], false, false);   // SAVE_HEAD
            for (unsigned i = 0; i < 4; ++i) crc_calc_ = crc_paso(crc_calc_, h[i]);
            if ((c1 & 1u) && uint8_t(h[0] + h[1] + h[2] + h[3]) != 0xFFu) { error(1); return; }
            if (((c1 >> 1) & 1u) && (h[0] >> 7) && (h[0] & 7u) != ack_) { error(2); return; }
            if (((h[0] >> 6) & 1u) && !crc_rec) { error(3); return; }
            largo_ = largo_de(h);
            fase_ = (len_eof && largo_ == 0) ? (((h[0] >> 6) & 1u) ? RX_CRC : RX_FIN) : RX_DATOS;
            if (fase_ == RX_FIN && !seper()) fin_paquete();
            return;
        }
        case RX_DATOS: {
            const bool dic = (cab_rx_[0] >> 6) & 1u;
            if (len_eof) {
                crc_calc_ = crc_paso(crc_calc_, d);
                a_gdma(&d, false, false);
                if (++n_dat_ == largo_) {
                    fase_ = dic ? RX_CRC : RX_FIN;
                    if (fase_ == RX_FIN && !seper()) fin_paquete();
                }
            } else if (dic) {                                         // el CRC son los dos últimos
                retraso_.push_back(d);
                if (retraso_.size() > 2) {
                    const uint8_t x = retraso_.front();
                    retraso_.pop_front();
                    crc_calc_ = crc_paso(crc_calc_, x);
                    a_gdma(&x, false, false);
                    ++n_dat_;
                }
            } else {
                crc_calc_ = crc_paso(crc_calc_, d);
                a_gdma(&d, false, false);
                ++n_dat_;
            }
            return;
        }
        case RX_CRC:
            crc_rx_[n_crc_++] = d;
            if (n_crc_ == 2) { fase_ = RX_FIN; if (!seper()) fin_paquete(); }
            return;
        case RX_FIN:
            error(5);                                                 // falta el separador
            return;
        }
    }
    // El final del paquete: el separador, el reposo o el break
    void fin_paquete() {
        if (!cabecera()) { if (n_rx_) cierra(false); return; }
        if (tirando_ || (fase_ == RX_CAB && n_cab_ == 0)) return;
        const uint32_t c0 = r_[0], c1 = r_[0x14 / 4];
        const bool len_eof = (c0 >> 9) & 1u;
        const bool dic = (cab_rx_[0] >> 6) & 1u;
        if (fase_ == RX_CAB || (len_eof && fase_ != RX_FIN)) { error(4); return; }
        if (!len_eof && dic) {
            if (retraso_.size() < 2) { error(4); return; }
            crc_rx_[0] = retraso_[0]; crc_rx_[1] = retraso_[1];
        }
        if (dic && !((c1 >> 2) & 1u)) {
            const uint16_t r = vuelta16(crc_calc_);
            if (crc_rx_[0] != uint8_t(r >> 8) || crc_rx_[1] != uint8_t(r)) { error(6); return; }
        }
        if (cab_rx_[0] >> 7) ack_ = ((cab_rx_[0] & 7u) + 1u) & 7u;   // una fiable buena
        cierra(false);
    }
    void decodifica(uint8_t b) {
        if (!seper()) { recibe(b); return; }
        if (b == sep()) {
            raw_ |= 2u;                                               // TX_START: llega un separador
            evalua();
            if (tirando_) { tirando_ = false; n_rx_ = 0; empieza_rx(); }
            else if (dentro_ && n_rx_) fin_paquete();
            dentro_ = true;
            esc_ = false;
            return;
        }
        if (!dentro_) return;                                         // fuera de una trama: se tira
        if (esc_) {
            esc_ = false;
            for (unsigned i = 0; i < 4; ++i) {
                const Esc e = esc(i);
                if (b == e.s1 && ((r_[0x20 / 4] >> (4 + i)) & 1u)) { recibe(e.c); return; }
            }
            recibe(esc(1).s0);                                        // no era un escape
            recibe(b);
            return;
        }
        for (unsigned i = 0; i < 4; ++i) {
            const Esc e = esc(i);
            if (b == e.s0 && ((r_[0x20 / 4] >> (4 + i)) & 1u)) { esc_ = true; return; }
        }
        recibe(b);
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
                fin_paquete();
                break;
            }
        }
    }
    // TX_HUNG y RX_HUNG
    sc_core::sc_time limite(unsigned base) const {
        const uint32_t h = r_[0x24 / 4] >> base;
        return sc_core::sc_time(double((h & 0xFFu) << ((h >> 8) & 7u)) / hz_apb, sc_core::SC_SEC);
    }
    void hung_proc() {
        for (;;) {
            sc_core::sc_time prox = sc_core::SC_ZERO_TIME;
            bool hay = false;
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            const uint32_t h = r_[0x24 / 4];
            if (hambre_tx_ && tx_ == TX_GDMA && !hung_tx_dado_ && ((h >> 11) & 1u)) {
                const sc_core::sc_time t = t_hambre_tx_ + limite(0);
                if (ahora >= t) { hung_tx_dado_ = true; raw_ |= 1u << 3; evalua(); }
                else { prox = t - ahora; hay = true; }
            }
            if (!rx_pend_.empty() && !hung_rx_dado_ && ((h >> 23) & 1u)) {
                const sc_core::sc_time t = t_pend_rx_ + limite(12);
                if (ahora >= t) { hung_rx_dado_ = true; raw_ |= 1u << 2; evalua(); }
                else if (!hay || t - ahora < prox) { prox = t - ahora; hay = true; }
            }
            if (hay) sc_core::wait(prox, ev_hung_);
            else sc_core::wait(ev_hung_);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_UHCI_ESP_H
