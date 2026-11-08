// =============================================================================
// usb_serial_jtag.h — El USB-Serial-JTAG de los ESP32 (C3, S3, C6...)
//
// E7 de doc/analisis_esp32_riscv.md (D3 y G3) [TRM-C3 §30;
// usb_serial_jtag_reg.h de ESP-IDF]. Un dispositivo USB de función FIJA: lo
// enumera el hardware, sin firmware, y tiene dos partes:
//
//   * un CDC-ACM (interfaces 0 y 1): lo que el PC manda por el bulk OUT EP1
//     llega a un búfer de 64 bytes que la CPU lee por USB_SERIAL_JTAG_EP1_REG
//     (SERIAL_OUT_EP_DATA_AVAIL en EP1_CONF); lo que la CPU escribe en EP1 va
//     a otro de 64 que sale por el bulk IN EP1 cuando escribe WR_DONE (o se
//     llena). Las interrupciones SERIAL_OUT_RECV_PKT y SERIAL_IN_EMPTY;
//   * el JTAG (interfaz 2, de fabricante, FF/FF/01): el bulk OUT EP2 trae
//     órdenes de 4 bits, dos por byte, la primera en el nibble alto, que se
//     hacen en el TAP del chip:
//         CMD_CLK  0 c d s   un ciclo de TCK con TDI = d y TMS = s; con c,
//                            guarda TDO (leído antes del flanco de subida)
//         CMD_RST  1 0 0 r   SRST (r = 1: el chip en reset)
//         CMD_FLUSH 1 0 1 0  manda ya los bits de TDO guardados
//         CMD_REP  1 1 n n   repite la última CMD_CLK; varias seguidas
//                            forman la cuenta en base 4, la primera abajo
//     y los bits de TDO salen por el bulk IN EP3, ocho por byte, el primero
//     en el bit 0 [el driver esp_usb_jtag de openocd-esp32; la disposición
//     de los nibbles y de REP, de modelo: el TRM no la da].
//
// EL RESET POR DTR Y RTS (SET_CONTROL_LINE_STATE) [TRM-C3 §30.3.2]: con RTS
// activo y DTR no, el chip está en reset; si se llega ahí desde DTR y RTS
// activos a la vez, al salir arranca en modo descarga. Es lo que hace la
// secuencia «usb-reset» de esptool, y abrir y cerrar un terminal (los dos a la
// vez) no hace nada. El periférico no se resetea con el chip: el PC lo sigue
// viendo enumerado.
//
// LO QUE NO SE MODELA: el modo de bajo consumo, cambiar los pines (EXCHG_PINS)
// o usar un PHY de fuera, las estadísticas de errores del bus (PID/CRC/STUFF),
// y los registros de estado de los endpoints (IN_EPx_ST, OUT_EPx_ST), que
// leen cero salvo los contadores básicos.
// =============================================================================
#ifndef STM32_PERIPH_ESP_USB_SERIAL_JTAG_H
#define STM32_PERIPH_ESP_USB_SERIAL_JTAG_H

#include <systemc>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "../../common/periph_base.h"
#include "../../common/analog_net.h"
#include "../otg.h"
#include "../../rv/debug/rv_dtm_jtag.h"

namespace stm32 {

class UsbSerialJtagEsp : public BusSlave, public usb_dev_if {
public:
    // La interrupción, hacia la matriz
    std::function<void(bool)> irq;
    // El reset del chip por DTR/RTS (activo, a modo descarga) y por CMD_RST
    std::function<void(bool, bool)> reset_chip;
    // ¿Va el JTAG del chip a este periférico? (lo deciden los eFuse)
    std::function<bool()> jtag_activo;
    RvDtmJtag* tap = nullptr;
    // Lo que dice el descriptor de serie (la MAC)
    std::string serie = "7C:DF:A1:00:C3:01";

    enum : uint32_t {
        EP1 = 0x00, EP1_CONF = 0x04, INT_RAW = 0x08, INT_ST = 0x0C, INT_ENA = 0x10, INT_CLR = 0x14,
        CONF0 = 0x18, TEST = 0x1C, JFIFO_ST = 0x20, FRAM_NUM = 0x24, MISC_CONF = 0x44, MEM_CONF = 0x48,
        DATE = 0x80,
    };
    enum : uint32_t { I_SOF = 1u << 1, I_OUT_RECV = 1u << 2, I_IN_EMPTY = 1u << 3, I_BUS_RESET = 1u << 9 };

    UsbSerialJtagEsp(sc_core::sc_module_name nm, uint32_t base, analog_net_if& dp, analog_net_if& dm)
        : BusSlave(nm, base, 0x1000), dp_(dp), dm_(dm) {
        id_pu_ = dp_.register_driver("usj_dp_pullup");
        reset_todo();
    }

    // Encendido: todo a su valor de reset (y el pull-up de D+)
    void reset_todo() {
        out_.clear(); in_.clear(); in_listo_ = false;
        raw_ = I_IN_EMPTY; ena_ = 0; conf0_ = (1u << 9) | (1u << 14); misc_ = 0; mem_ = 0x2;
        dir_ = 0; dir_pend_ = -1; config_ = 0; trama_ = 0;
        dtr_ = rts_ = false;
        bits_ = 0; nbits_ = 0; tdo_.clear(); ultima_clk_ = -1; rep_ = 0; nrep_ = 0;
        ctl_ = Ctl();
        pullup();
        evalua();
    }
    void alimenta(bool on) {
        alimentado_ = on;
        if (!on) reset_todo();
        pullup();
    }
    bool dtr() const { return dtr_; }
    bool rts() const { return rts_; }
    uint8_t direccion() const { return dir_; }
    uint8_t configuracion() const { return config_; }
    uint64_t ordenes_jtag() const { return n_jtag_; }
    uint64_t bytes_out() const { return n_out_; }
    uint64_t bytes_in() const { return n_in_; }
    const std::vector<uint8_t>& line_coding() const { return lc_; }
    // Hay un IN listo (WR_DONE, o el búfer lleno): para el anfitrión
    const sc_core::sc_event& ev_in_listo() const { return ev_in_; }

    // --- usb_dev_if ------------------------------------------------------------
    uint8_t transaccion(uint8_t pid, uint8_t addr, uint8_t ep, const std::vector<uint8_t>& s,
                        std::vector<uint8_t>& e) override {
        e.clear();
        if (!alimentado_ || !conectado() || addr != dir_) return PID_NADIE;
        if (ep == 0) return ep0(pid, s, e);
        if (!config_) return PID_STALL;
        if (pid == PID_OUT && ep == 1) {                   // CDC: del PC al chip
            if (!out_.empty()) return PID_NAK;
            out_.assign(s.begin(), s.end());
            n_out_ += s.size();
            raw_ |= I_OUT_RECV;
            evalua();
            return PID_ACK;
        }
        if (pid == PID_IN && ep == 1) {                    // CDC: del chip al PC
            if (!in_listo_ && in_.size() >= 64) in_listo_ = true;
            if (!in_listo_) return PID_NAK;
            e.assign(in_.begin(), in_.end());
            n_in_ += in_.size();
            in_.clear();
            in_listo_ = false;
            raw_ |= I_IN_EMPTY;
            evalua();
            return PID_ACK;
        }
        if (pid == PID_IN && ep == 2) return PID_NAK;      // la notificación del CDC
        if (pid == PID_OUT && ep == 2) {                   // JTAG: las órdenes
            for (uint8_t b : s) { orden((b >> 4) & 0xFu); orden(b & 0xFu); }
            return PID_ACK;
        }
        if (pid == PID_IN && ep == 3) {                    // JTAG: TDO
            if (tdo_.empty()) return PID_NAK;
            const size_t n = std::min<size_t>(tdo_.size(), 64);
            e.assign(tdo_.begin(), tdo_.begin() + long(n));
            tdo_.erase(tdo_.begin(), tdo_.begin() + long(n));
            return PID_ACK;
        }
        return PID_STALL;
    }
    void sof(uint16_t t) override { trama_ = t; raw_ |= I_SOF; evalua(); }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case EP1: {
            if (out_.empty()) return 0;
            const uint8_t b = out_.front();
            out_.pop_front();
            return b;
        }
        case EP1_CONF:
            return (in_listo_ || in_.size() >= 64 ? 0u : 2u) | (out_.empty() ? 0u : 4u);
        case INT_RAW: return raw_;
        case INT_ST: return raw_ & ena_;
        case INT_ENA: return ena_;
        case CONF0: return conf0_;
        case FRAM_NUM: return trama_ & 0x7FFu;
        case MISC_CONF: return misc_;
        case MEM_CONF: return mem_;
        case DATE: return 0x2007300u;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case EP1:
            if (!in_listo_ && in_.size() < 64) in_.push_back(uint8_t(v));
            if (in_.size() >= 64) ev_in_.notify(sc_core::SC_ZERO_TIME);
            return;
        case EP1_CONF:
            if ((v & 1u) && !in_.empty()) { in_listo_ = true; ev_in_.notify(sc_core::SC_ZERO_TIME); }   // WR_DONE
            if ((v & 1u) && in_.empty()) in_listo_ = false;
            return;
        case INT_ENA: ena_ = v & 0xFFFu; evalua(); return;
        case INT_CLR:
            raw_ &= ~(v & 0xFFFu);
            evalua();
            // Mientras quede algo del PC por leer, SERIAL_OUT_RECV_PKT vuelve
            // a ponerse (con un flanco nuevo: baja y sube), y SERIAL_IN_EMPTY
            // mientras el de entrada esté vacío: un borrado que llega tarde no
            // se come un paquete [de modelo; el stub de esptool por USB, con la
            // línea de flanco, se quedaba esperando]
            if (!out_.empty()) raw_ |= I_OUT_RECV;
            if (in_.empty() && !in_listo_) raw_ |= I_IN_EMPTY;
            evalua();
            return;
        case CONF0: conf0_ = v; pullup(); return;
        case MISC_CONF: misc_ = v; return;
        case MEM_CONF: mem_ = v; return;
        default: return;
        }
    }

private:
    analog_net_if &dp_, &dm_;
    int id_pu_ = -1;
    bool alimentado_ = false;
    std::deque<uint8_t> out_;
    std::vector<uint8_t> in_;
    bool in_listo_ = false;
    sc_core::sc_event ev_in_;
    uint32_t raw_ = 0, ena_ = 0, conf0_ = 0, misc_ = 0, mem_ = 0;
    bool irq_ = false;
    uint8_t dir_ = 0, config_ = 0;
    int dir_pend_ = -1;
    uint16_t trama_ = 0;
    bool dtr_ = false, rts_ = false;
    std::vector<uint8_t> lc_{0x00, 0xC2, 0x01, 0x00, 0, 0, 8};      // 115200 8N1
    uint64_t n_out_ = 0, n_in_ = 0, n_jtag_ = 0;
    // El JTAG
    uint8_t bits_ = 0;
    unsigned nbits_ = 0;
    std::deque<uint8_t> tdo_;
    int ultima_clk_ = -1;
    unsigned rep_ = 0, nrep_ = 0;
    // EP0
    struct Ctl {
        std::vector<uint8_t> datos;      // lo que queda por mandar en IN
        bool in = false, out_datos = false, stall = false;
        uint8_t req = 0;
        uint16_t valor = 0;
        std::vector<uint8_t> recibido;
        uint16_t largo = 0;
    } ctl_;

    // La pull-up de 1,5 kOhm en D+: USB_PAD_ENABLE y DP_PULLUP (con
    // PAD_PULL_OVERRIDE, la que diga DP_PULLUP; sin él, puesta)
    void pullup() {
        const bool pad = (conf0_ >> 14) & 1u, ovr = (conf0_ >> 8) & 1u, pu = (conf0_ >> 9) & 1u;
        if (alimentado_ && pad && (!ovr || pu)) dp_.set_drive(id_pu_, 3.3f, 1500.0f);
        else dp_.set_hiz(id_pu_);
    }
    bool conectado() const {
        const bool pad = (conf0_ >> 14) & 1u, ovr = (conf0_ >> 8) & 1u, pu = (conf0_ >> 9) & 1u;
        return pad && (!ovr || pu);
    }
    void evalua() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }

    // --- Los descriptores (de función fija) --------------------------------
    static std::vector<uint8_t> cadena(const std::string& s) {
        std::vector<uint8_t> d{uint8_t(2 + 2 * s.size()), 3};
        for (char ch : s) { d.push_back(uint8_t(ch)); d.push_back(0); }
        return d;
    }
    std::vector<uint8_t> descriptor(uint16_t valor) const {
        const uint8_t tipo = uint8_t(valor >> 8), i = uint8_t(valor);
        if (tipo == 1)
            return {18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, 0x3A, 0x30, 0x01, 0x10, 0x01, 0x01, 1, 2, 3, 1};
        if (tipo == 2) {
            std::vector<uint8_t> c{9, 2, 0, 0, 3, 1, 0, 0x80, 50,
                // IAD: interfaces 0 y 1, CDC-ACM
                8, 0x0B, 0, 2, 0x02, 0x02, 0x00, 0,
                9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,
                5, 0x24, 0x00, 0x10, 0x01,
                5, 0x24, 0x01, 0x03, 0x01,
                4, 0x24, 0x02, 0x02,
                5, 0x24, 0x06, 0x00, 0x01,
                7, 5, 0x82, 3, 64, 0, 1,
                9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
                7, 5, 0x01, 2, 64, 0, 0,
                7, 5, 0x81, 2, 64, 0, 0,
                // El JTAG
                9, 4, 2, 0, 2, 0xFF, 0xFF, 0x01, 0,
                7, 5, 0x02, 2, 64, 0, 0,
                7, 5, 0x83, 2, 64, 0, 0};
            c[2] = uint8_t(c.size()); c[3] = uint8_t(c.size() >> 8);
            return c;
        }
        if (tipo == 3) {
            if (i == 0) return {4, 3, 0x09, 0x04};
            if (i == 1) return cadena("Espressif");
            if (i == 2) return cadena("USB JTAG/serial debug unit");
            if (i == 3) return cadena(serie);
        }
        return {};
    }

    // --- EP0 -----------------------------------------------------------------
    uint8_t ep0(uint8_t pid, const std::vector<uint8_t>& s, std::vector<uint8_t>& e) {
        if (pid == PID_SETUP) {
            if (s.size() < 8) return PID_STALL;
            ctl_ = Ctl();
            const uint8_t tipo = s[0];
            ctl_.req = s[1];
            ctl_.valor = uint16_t(s[2] | (s[3] << 8));
            ctl_.largo = uint16_t(s[6] | (s[7] << 8));
            ctl_.in = tipo & 0x80u;
            if (tipo == 0x80 && ctl_.req == 6) {                        // GET_DESCRIPTOR
                ctl_.datos = descriptor(ctl_.valor);
                if (ctl_.datos.empty()) ctl_.stall = true;
                if (ctl_.datos.size() > ctl_.largo) ctl_.datos.resize(ctl_.largo);
            } else if (tipo == 0x80 && ctl_.req == 0) {                 // GET_STATUS
                ctl_.datos = {0, 0};
            } else if (tipo == 0x80 && ctl_.req == 8) {                 // GET_CONFIGURATION
                ctl_.datos = {config_};
            } else if (tipo == 0x00 && ctl_.req == 5) {                 // SET_ADDRESS
                dir_pend_ = ctl_.valor & 0x7F;
            } else if (tipo == 0x00 && ctl_.req == 9) {                 // SET_CONFIGURATION
                config_ = uint8_t(ctl_.valor);
            } else if (tipo == 0x21 && ctl_.req == 0x20) {              // SET_LINE_CODING
                ctl_.out_datos = true;
            } else if (tipo == 0xA1 && ctl_.req == 0x21) {              // GET_LINE_CODING
                ctl_.datos = lc_;
            } else if (tipo == 0x21 && ctl_.req == 0x22) {              // SET_CONTROL_LINE_STATE
                lineas((ctl_.valor & 1u) != 0, (ctl_.valor & 2u) != 0);
            } else if (tipo == 0x21 && ctl_.req == 0x23) {              // SEND_BREAK
            } else if ((tipo & 0x60) == 0x00 && (ctl_.req == 1 || ctl_.req == 3 || ctl_.req == 11)) {
                // CLEAR/SET_FEATURE, SET_INTERFACE: se aceptan
            } else {
                ctl_.stall = true;
            }
            return PID_ACK;
        }
        if (ctl_.stall) return PID_STALL;
        if (pid == PID_IN) {
            if (ctl_.in) {                                              // datos
                const size_t n = std::min<size_t>(ctl_.datos.size(), 64);
                e.assign(ctl_.datos.begin(), ctl_.datos.begin() + long(n));
                ctl_.datos.erase(ctl_.datos.begin(), ctl_.datos.begin() + long(n));
                return PID_ACK;
            }
            // Estado de una de salida: aquí se aplica SET_ADDRESS
            if (dir_pend_ >= 0) { dir_ = uint8_t(dir_pend_); dir_pend_ = -1; }
            if (ctl_.out_datos && ctl_.recibido.size() >= 7) lc_.assign(ctl_.recibido.begin(), ctl_.recibido.begin() + 7);
            return PID_ACK;
        }
        if (pid == PID_OUT) {
            if (ctl_.out_datos) ctl_.recibido.insert(ctl_.recibido.end(), s.begin(), s.end());
            return PID_ACK;                                             // datos, o el estado de una IN
        }
        return PID_STALL;
    }

    // DTR y RTS [TRM-C3 §30.3.2]: (RTS, DTR) = (1, 0) es el reset; si se
    // llega desde (1, 1), descarga
    void lineas(bool dtr, bool rts) {
        const bool antes = rts_ && !dtr_, ahora = rts && !dtr;
        const bool desde_11 = rts_ && dtr_;
        dtr_ = dtr; rts_ = rts;
        if (ahora && !antes) { descarga_ = desde_11; if (reset_chip) reset_chip(true, descarga_); }
        else if (!ahora && antes) { if (reset_chip) reset_chip(false, descarga_); }
    }
    bool descarga_ = false;

    // --- Las órdenes del JTAG ----------------------------------------------
    void clk(unsigned c) {
        ++n_jtag_;
        const bool cap = c & 4u, tdi = c & 2u, tms = c & 1u;
        const bool usar = tap && (!jtag_activo || jtag_activo());
        const bool tdo = usar && tap->desplazando() ? tap->tdo() : false;
        if (usar) tap->subida(tms, tdi);
        if (cap) {
            bits_ |= uint8_t(tdo) << nbits_;
            if (++nbits_ == 8) { tdo_.push_back(bits_); bits_ = 0; nbits_ = 0; }
        }
    }
    void orden(unsigned n) {
        if ((n & 0xCu) == 0xCu) {                       // CMD_REP
            rep_ |= (n & 3u) << (2 * nrep_);
            ++nrep_;
            return;
        }
        if (nrep_) {                                    // acaba una serie de REP
            if (ultima_clk_ >= 0) for (unsigned k = 0; k < rep_; ++k) clk(unsigned(ultima_clk_));
            rep_ = nrep_ = 0;
        }
        if (!(n & 8u)) { clk(n); ultima_clk_ = int(n); return; }
        if ((n & 0xEu) == 0x8u) { if (reset_chip) reset_chip((n & 1u) != 0, false); return; }   // CMD_RST
        if (n == 0xAu) {                                // CMD_FLUSH
            if (nbits_) { tdo_.push_back(bits_); bits_ = 0; nbits_ = 0; }
            raw_ |= 1u;                                 // JTAG_IN_FLUSH
            evalua();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_USB_SERIAL_JTAG_H
