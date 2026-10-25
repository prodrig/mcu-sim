// =============================================================================
// puente_usb.h — El USB de un MCU como dispositivo, hasta el ordenador:
//                el puente de clase CDC-ACM (U2 de doc/analisis_puente_usb.md)
//
// El puente hace de PC completo al otro lado del cable: pone sus dos 15 kOhm a
// masa, espera a que el dispositivo ponga su 1,5 kOhm en D+, hace el reset de
// bus, lo ENUMERA como lo haria un sistema operativo (descriptores, direccion,
// configuracion) y, si es un CDC-ACM, lleva sus endpoints de bulk al canal del
// puente serie (parts/canal_host.h): TCP en crudo o RFC 2217, de servidor o de
// cliente, o una cola en memoria para las pruebas. El terminal no sabe que hay
// un USB en medio, y el firmware no sabe que no hay un PC:
//
//   * SET-BAUDRATE, SET-DATASIZE, SET-PARITY y SET-STOPSIZE de RFC 2217 se
//     convierten en un SET_LINE_CODING: el firmware lo recibe en su EP0 como
//     lo recibiria de un `stty` [U2, §6.2 del analisis];
//   * DTR y RTS (SET-CONTROL 8/9 y 11/12) en un SET_CONTROL_LINE_STATE, y el
//     break (5/6) en un SEND_BREAK;
//   * abrir el puerto -que llegue un cliente al TCP- pone DTR y RTS, como hace
//     un sistema al abrir /dev/ttyACM0; cerrarlo los quita.
//
// EL CONTROLADOR DE ANFITRION (`ControladorUsb`) va aparte porque no sabe
// nada de clases: transferencias de control (SETUP, datos, estado, con los NAK
// reintentados y el STALL dicho), de bulk de un paquete, y el SOF. Habla con
// el dispositivo por `usb_dev_if` (periph/otg.h), la frontera de paquetes del
// proyecto, asi que vale para el MUSB del E4 y para el OTG de los STM32.
//
// LO ELECTRICO va por los nodos de D+ y D-, con tensiones: la llegada del
// dispositivo es el divisor de su 1,5 kOhm contra los 15 kOhm del puente; el
// reset es un SE0 de 10 ms; y si el dispositivo quita su 1,5 kOhm (un reset
// del chip, USBDevDisconnect), el puente lo ve irse y lo vuelve a enumerar
// cuando vuelva.
//
// EL TIEMPO: un SOF cada milisegundo simulado y, en cada trama, hasta ocho
// paquetes de bulk en cada sentido, con el tiempo de cable de cada uno a 12
// Mbit/s. Con `--tiempo-real` eso es tiempo de pared.
//
// No es un sc_module: se monta cuando se aplican las opciones de los chips,
// con la simulacion en marcha, y por eso su hilo es un sc_spawn.
//
// LO QUE NO HACE [P-17 s]: otras clases (HID, MSC: U2 completo), varias
// configuraciones o interfaces alternativas, la baja velocidad, los hubs, la
// suspension, y las notificaciones del endpoint de interrupcion del CDC
// (SERIAL_STATE), que no tienen a donde ir en RFC 2217 mas que las lineas de
// modem, que aqui son fijas.
// =============================================================================
#ifndef STM32_PARTS_PUENTE_USB_H
#define STM32_PARTS_PUENTE_USB_H

#include <systemc>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "canal_host.h"
#include "../common/analog_net.h"
#include "../common/serie_destino.h"
#include "../periph/otg.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// El controlador de anfitrion: transferencias sobre `transaccion()`. Todo se
// llama desde un hilo de SystemC (espera el tiempo de cable de cada paquete)
// ---------------------------------------------------------------------------
class ControladorUsb {
public:
    explicit ControladorUsb(usb_dev_if* d = nullptr) : dev_(d) {}
    void dispositivo(usb_dev_if* d) { dev_ = d; }
    // Cuanto espera un NAK en una etapa de control antes de rendirse: el
    // limite de 5 s de USB 2.0 §9.2.6.4 es para un sistema; aqui, 50 ms
    double plazo_control_ms = 50.0;
    // Cuánto deja, como poco, entre el final de una transferencia de control y
    // el SETUP de la siguiente. Un sistema de verdad no las encadena más deprisa
    // que una microtrama (125 us): entre las dos pasan la llamada de vuelta del
    // controlador y la siguiente petición. Con 0 (el de siempre, el del E4), las
    // encadena; el FSDEV del F303 con TinyUSB necesita la pausa, porque el SETUP
    // que llega mientras la pila aún atiende el estado de la anterior se la come
    // (como en el silicio: periph/usb_fs.h)
    double pausa_control_us = 0.0;

    static std::vector<uint8_t> setup(uint8_t tipo, uint8_t pet, uint16_t valor, uint16_t indice,
                                      uint16_t largo) {
        return {tipo, pet, uint8_t(valor), uint8_t(valor >> 8), uint8_t(indice), uint8_t(indice >> 8),
                uint8_t(largo), uint8_t(largo >> 8)};
    }
    // Un paquete, con su tiempo de cable: sincronismo, PID, CRC, EOP y el
    // apreton de manos, unos 10 bytes mas que los datos, a 12 Mbit/s
    uint8_t paquete(uint8_t pid, uint8_t dir, uint8_t ep, const std::vector<uint8_t>& s,
                    std::vector<uint8_t>& e) {
        e.clear();
        if (!dev_) return PID_NADIE;
        const uint8_t r = dev_->transaccion(pid, dir, ep, s, e);
        ++n_paquetes_;
        if (r == PID_NAK) ++n_nak_;
        if (r == PID_STALL) ++n_stall_;
        const size_t n = 10 + s.size() + e.size();
        sc_core::wait(sc_core::sc_time(double(n) * 8.0 / 12.0, sc_core::SC_US));
        return r;
    }
    // Con los NAK reintentados hasta el plazo
    uint8_t insiste(uint8_t pid, uint8_t dir, uint8_t ep, const std::vector<uint8_t>& s,
                    std::vector<uint8_t>& e) {
        const sc_core::sc_time lim = sc_core::sc_time_stamp() + sc_core::sc_time(plazo_control_ms, sc_core::SC_MS);
        for (;;) {
            const uint8_t r = paquete(pid, dir, ep, s, e);
            if (r != PID_NAK || sc_core::sc_time_stamp() >= lim) return r;
            sc_core::wait(10, sc_core::SC_US);
        }
    }
    // Control de entrada: SETUP, los IN hasta el paquete corto o el largo
    // pedido, y el OUT de cero bytes del estado. Devuelve el PID que la acaba
    uint8_t control_in(uint8_t dir, const std::vector<uint8_t>& st, std::vector<uint8_t>& d,
                       unsigned mps0 = 64) {
        d.clear();
        pausa();
        std::vector<uint8_t> p;
        uint8_t r = paquete(PID_SETUP, dir, 0, st, p);
        if (r != PID_ACK) return r;
        const unsigned largo = unsigned(st[6] | (st[7] << 8));
        while (d.size() < largo) {
            r = insiste(PID_IN, dir, 0, {}, p);
            if (r != PID_ACK) return r;
            d.insert(d.end(), p.begin(), p.end());
            if (p.size() < mps0) break;
        }
        r = insiste(PID_OUT, dir, 0, {}, p);
        t_fin_ = sc_core::sc_time_stamp();
        return r;
    }
    // Control de salida: SETUP, los datos por paquetes y el IN de cero bytes
    uint8_t control_out(uint8_t dir, const std::vector<uint8_t>& st, const std::vector<uint8_t>& d = {},
                        unsigned mps0 = 64) {
        pausa();
        std::vector<uint8_t> p;
        uint8_t r = paquete(PID_SETUP, dir, 0, st, p);
        if (r != PID_ACK) return r;
        for (size_t i = 0; i < d.size(); i += mps0) {
            const std::vector<uint8_t> t(d.begin() + long(i), d.begin() + long(std::min(d.size(), i + mps0)));
            r = insiste(PID_OUT, dir, 0, t, p);
            if (r != PID_ACK) return r;
        }
        r = insiste(PID_IN, dir, 0, {}, p);
        t_fin_ = sc_core::sc_time_stamp();
        return r == PID_ACK && !p.empty() ? uint8_t(PID_NADIE) : r;
    }
    void sof() { if (dev_) dev_->sof(uint16_t(++trama_ & 0x7FFu)); }
    unsigned paquetes() const { return n_paquetes_; }
    unsigned naks() const { return n_nak_; }
    unsigned stalls() const { return n_stall_; }

private:
    usb_dev_if* dev_;
    unsigned trama_ = 0, n_paquetes_ = 0, n_nak_ = 0, n_stall_ = 0;
    sc_core::sc_time t_fin_;
    void pausa() {
        if (pausa_control_us <= 0.0) return;
        const sc_core::sc_time p(pausa_control_us, sc_core::SC_US), d = sc_core::sc_time_stamp() - t_fin_;
        if (d < p) sc_core::wait(p - d);
    }
};

// ---------------------------------------------------------------------------
// El puente de clase CDC-ACM
// ---------------------------------------------------------------------------
class PuenteUsbCdc : public LineaSerie {
public:
    enum class Estado { SinDispositivo, Enumerando, Listo, NoSirve };

    PuenteUsbCdc(analog_net_if& dp, analog_net_if& dm, usb_dev_if* dev, const serie::Destino& d,
                 std::size_t cola = 4096)
        : dp_(dp), dm_(dm), hc_(dev), d_(d) {
        if (!d.valido) { error_ = d.error; return; }
        switch (d.modo) {
            case serie::Modo::rfc2217: tcp_ = new CanalRfc2217(d.puerto, cola, this); break;
            case serie::Modo::tcp: tcp_ = new CanalTcp(d.puerto, cola); break;
            case serie::Modo::tcp_cliente: tcp_ = new CanalTcp(d.host, d.puerto, cola); break;
            case serie::Modo::rfc2217_cliente: tcp_ = new CanalRfc2217Cliente(d.host, d.puerto, cola, this); break;
            case serie::Modo::memoria: break;
        }
        if (tcp_) {
            canal_.reset(tcp_);
            if (!tcp_->abierto())
                error_ = "no se puede escuchar en localhost:" + std::to_string(d.puerto) +
                         ": ¿lo tiene otro programa?";
        } else {
            mem_ = new CanalMemoria(cola);
            canal_.reset(mem_);
        }
        id_pd_dp_ = dp_.register_driver("usbpc_pd_dp");
        id_pd_dm_ = dm_.register_driver("usbpc_pd_dm");
        id_dp_ = dp_.register_driver("usbpc_dp");
        id_dm_ = dm_.register_driver("usbpc_dm");
        dp_.set_hiz(id_dp_); dm_.set_hiz(id_dm_);
        pon_pulldowns(true);
        sc_core::sc_spawn([this] { hilo(); });
    }

    const std::string& error() const { return error_; }
    std::string describir() const {
        std::string s = "CDC-ACM -> " + canal_->describir();
        return s;
    }
    Estado estado() const { return estado_; }
    bool listo() const { return estado_ == Estado::Listo; }
    unsigned enumeraciones() const { return n_enum_; }
    const std::string& producto() const { return producto_; }
    uint16_t vid() const { return vid_; }
    uint16_t pid() const { return pid_; }
    ControladorUsb& controlador() { return hc_; }
    CanalMemoria* memoria() { return mem_; }
    uint64_t hacia_host() const { return n_hacia_; }
    uint64_t desde_host() const { return n_desde_; }
    // Desenchufar y enchufar el cable: sin los 15 kOhm no hay anfitrion
    void enchufa(bool on) {
        enchufado_ = on;
        pon_pulldowns(on);
        if (!on) { dp_.set_hiz(id_dp_); dm_.set_hiz(id_dm_); estado_ = Estado::SinDispositivo; }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // ---- LineaSerie: lo que pide el terminal por RFC 2217 -----------------
    // Todo se acepta: es el firmware quien decide que hace con ello, como con
    // un CDC de verdad. Se manda en la siguiente trama
    uint32_t pide_baudios(uint32_t v) override {
        if (v && v != baudios_) { baudios_ = v; linea_pend_ = true; }
        return baudios_;
    }
    uint8_t pide_datos(uint8_t v) override {
        if (v >= 5 && v <= 8 && v != datos_) { datos_ = v; linea_pend_ = true; }
        return datos_;
    }
    // RFC 2217: 1 N, 2 O, 3 E, 4 M, 5 S. CDC: 0 N, 1 O, 2 E, 3 M, 4 S
    uint8_t pide_paridad(uint8_t v) override {
        if (v >= 1 && v <= 5 && uint8_t(v - 1) != paridad_) { paridad_ = uint8_t(v - 1); linea_pend_ = true; }
        return uint8_t(paridad_ + 1);
    }
    // RFC 2217: 1 uno, 2 dos, 3 uno y medio. CDC: 0 uno, 1 uno y medio, 2 dos
    uint8_t pide_parada(uint8_t v) override {
        static const uint8_t A_CDC[4] = {0, 0, 2, 1};
        static const uint8_t A_RFC[3] = {1, 3, 2};
        if (v >= 1 && v <= 3 && A_CDC[v] != parada_) { parada_ = A_CDC[v]; linea_pend_ = true; }
        return A_RFC[parada_ < 3 ? parada_ : 0];
    }
    uint8_t pide_control(uint8_t v) override {
        switch (v) {
            case 4: return break_ ? 5 : 6;
            case 5: case 6: break_ = v == 5; break_pend_ = true; return v;
            case 7: return (lineas_ & 1u) ? 8 : 9;
            case 8: case 9: pon_lineas(uint8_t(v == 8 ? lineas_ | 1u : lineas_ & ~1u)); return v;
            case 10: return (lineas_ & 2u) ? 11 : 12;
            case 11: case 12: pon_lineas(uint8_t(v == 11 ? lineas_ | 2u : lineas_ & ~2u)); return v;
            case 13: case 14: case 15: case 16: case 18: return 14;
            default: return 1;                         // sin control de flujo
        }
    }
    uint8_t estado_modem() const override {
        return listo() ? uint8_t(CanalRfc2217::MODEM_DSR | CanalRfc2217::MODEM_DCD | CanalRfc2217::MODEM_CTS) : 0;
    }
    std::string firma() const override { return "mcu-sim PuenteUsbCdc"; }

    uint32_t baudios() const { return baudios_; }
    uint8_t lineas() const { return lineas_; }

private:
    analog_net_if &dp_, &dm_;
    ControladorUsb hc_;
    serie::Destino d_;
    std::unique_ptr<CanalHost> canal_;
    CanalTcp* tcp_ = nullptr;
    CanalMemoria* mem_ = nullptr;
    std::string error_;
    int id_pd_dp_ = -1, id_pd_dm_ = -1, id_dp_ = -1, id_dm_ = -1;
    sc_core::sc_event ev_;
    bool enchufado_ = true;
    Estado estado_ = Estado::SinDispositivo;
    unsigned n_enum_ = 0;
    // Lo que se sabe del dispositivo
    uint8_t dir_ = 0, mps0_ = 8, conf_ = 1, iface_ = 0, ep_in_ = 0, ep_out_ = 0;
    uint16_t mps_in_ = 64, mps_out_ = 64, vid_ = 0, pid_ = 0;
    std::string producto_;
    // La linea, como la quiere el terminal
    uint32_t baudios_ = 115200;
    uint8_t datos_ = 8, paridad_ = 0, parada_ = 0, lineas_ = 0;
    bool break_ = false, linea_pend_ = true, lineas_pend_ = true, break_pend_ = false, cliente_ = false;
    std::deque<uint8_t> hacia_mcu_;
    uint64_t n_hacia_ = 0, n_desde_ = 0;

    void pon_pulldowns(bool on) {
        dp_.set_drive(id_pd_dp_, 0.0f, on ? 15.0e3f : R_HIZ);
        dm_.set_drive(id_pd_dm_, 0.0f, on ? 15.0e3f : R_HIZ);
    }
    void pon_lineas(uint8_t l) { if (l != lineas_) { lineas_ = l; lineas_pend_ = true; } }
    bool hay_dispositivo() const { return dp_.voltage() > 2.0f && dm_.voltage() < 0.8f; }
    void avisa(const std::string& s) {
        std::fflush(stdout);
        std::printf("  [usb] %s\n", s.c_str());
        std::fflush(stdout);
    }

    void hilo() {
        for (;;) {
            if (!enchufado_) { estado_ = Estado::SinDispositivo; sc_core::wait(ev_); continue; }
            // Esperar al 1,5 kOhm, y el rebote de USB 2.0 §7.1.7.3 (100 ms)
            if (!hay_dispositivo()) {
                estado_ = Estado::SinDispositivo;
                sc_core::wait(dp_.value_changed_event() | ev_);
                continue;
            }
            sc_core::wait(sc_core::sc_time(100, sc_core::SC_MS), ev_);
            if (!enchufado_ || !hay_dispositivo()) continue;
            estado_ = Estado::Enumerando;
            if (!enumera()) {
                if (estado_ != Estado::NoSirve) {
                    avisa("el dispositivo no se deja enumerar; se vuelve a intentar al reconectarlo");
                    estado_ = Estado::NoSirve;
                }
                while (enchufado_ && hay_dispositivo()) { trama(); sc_core::wait(1, sc_core::SC_MS); }
                continue;
            }
            estado_ = Estado::Listo;
            ++n_enum_;
            linea_pend_ = lineas_pend_ = true;
            while (enchufado_ && hay_dispositivo() && estado_ == Estado::Listo) {
                trama();
                datos();
                sc_core::wait(1, sc_core::SC_MS);
            }
            if (enchufado_) avisa("el dispositivo se ha ido del bus");
        }
    }
    void trama() { hc_.sof(); }

    // ---- La enumeracion, como la hace un sistema -------------------------
    bool reset_bus() {
        dp_.set_drive(id_dp_, 0.0f, 45.0f);
        dm_.set_drive(id_dm_, 0.0f, 45.0f);
        sc_core::wait(10, sc_core::SC_MS);
        dp_.set_hiz(id_dp_); dm_.set_hiz(id_dm_);
        sc_core::wait(10, sc_core::SC_US);
        // La recuperacion tras el reset: 10 ms con SOF
        for (int i = 0; i < 10; ++i) { trama(); sc_core::wait(1, sc_core::SC_MS); }
        return hay_dispositivo();
    }
    bool enumera() {
        if (!reset_bus()) return false;
        std::vector<uint8_t> d;
        // Los 8 primeros bytes del descriptor, para saber el tamaño del EP0
        if (hc_.control_in(0, ControladorUsb::setup(0x80, 6, 0x0100, 0, 8), d, 8) != PID_ACK || d.size() < 8)
            return false;
        mps0_ = d[7] ? d[7] : 8;
        if (!reset_bus()) return false;
        dir_ = 1;
        if (hc_.control_out(0, ControladorUsb::setup(0x00, 5, dir_, 0, 0)) != PID_ACK) return false;
        for (int i = 0; i < 2; ++i) { trama(); sc_core::wait(1, sc_core::SC_MS); }
        if (hc_.control_in(dir_, ControladorUsb::setup(0x80, 6, 0x0100, 0, 18), d, mps0_) != PID_ACK ||
            d.size() < 18)
            return false;
        vid_ = uint16_t(d[8] | (d[9] << 8));
        pid_ = uint16_t(d[10] | (d[11] << 8));
        const uint8_t i_prod = d[15];
        if (hc_.control_in(dir_, ControladorUsb::setup(0x80, 6, 0x0200, 0, 9), d, mps0_) != PID_ACK || d.size() < 9)
            return false;
        const uint16_t total = uint16_t(d[2] | (d[3] << 8));
        if (hc_.control_in(dir_, ControladorUsb::setup(0x80, 6, 0x0200, 0, total), d, mps0_) != PID_ACK ||
            d.size() < total)
            return false;
        conf_ = d[5];
        producto_.clear();
        if (i_prod) {
            std::vector<uint8_t> c;
            if (hc_.control_in(dir_, ControladorUsb::setup(0x80, 6, uint16_t(0x0300 | i_prod), 0x0409, 255), c,
                               mps0_) == PID_ACK)
                for (size_t i = 2; i + 1 < c.size(); i += 2) producto_ += c[i] >= 0x20 && c[i] < 0x7F ? char(c[i]) : '?';
        }
        char id[16];
        std::snprintf(id, sizeof id, "%04X:%04X", vid_, pid_);
        if (!busca_cdc(d)) {
            avisa(std::string("el dispositivo ") + id + " (" + producto_ + ") no es un CDC-ACM: "
                  "este puente solo lleva esa clase");
            estado_ = Estado::NoSirve;
            return false;
        }
        if (hc_.control_out(dir_, ControladorUsb::setup(0x00, 9, conf_, 0, 0)) != PID_ACK) return false;
        avisa(std::string("enumerado ") + id + " \"" + producto_ + "\": CDC-ACM, bulk EP" +
              std::to_string(ep_out_) + " OUT y EP" + std::to_string(ep_in_) + " IN -> " +
              canal_->describir());
        return true;
    }
    // Recorre la configuracion: la interfaz de comunicaciones (clase 2,
    // subclase 2: ACM) y la de datos (clase 0x0A) con sus dos de bulk
    bool busca_cdc(const std::vector<uint8_t>& c) {
        bool acm = false, en_datos = false;
        ep_in_ = ep_out_ = 0;
        for (size_t i = 0; i + 1 < c.size() && c[i] >= 2; i += c[i]) {
            const uint8_t tipo = c[i + 1];
            if (tipo == 4 && i + 7 < c.size()) {
                en_datos = c[i + 5] == 0x0A;
                if (c[i + 5] == 0x02 && c[i + 6] == 0x02) { acm = true; iface_ = c[i + 2]; }
            } else if (tipo == 5 && en_datos && i + 6 < c.size() && (c[i + 3] & 3u) == 2) {
                const uint16_t mps = uint16_t(c[i + 4] | (c[i + 5] << 8));
                if (c[i + 2] & 0x80) { ep_in_ = c[i + 2] & 0x0Fu; mps_in_ = mps; }
                else { ep_out_ = c[i + 2] & 0x0Fu; mps_out_ = mps; }
            }
        }
        return acm && ep_in_ && ep_out_;
    }

    // ---- Una trama de datos -----------------------------------------------
    void datos() {
        canal_->sondear();
        // Abrir y cerrar el puerto mueve DTR y RTS
        const bool cli = canal_->conectado();
        if (cli != cliente_) { cliente_ = cli; pon_lineas(cli ? 3 : 0); }
        if (linea_pend_) {
            const std::vector<uint8_t> lc = {uint8_t(baudios_), uint8_t(baudios_ >> 8), uint8_t(baudios_ >> 16),
                                             uint8_t(baudios_ >> 24), parada_, paridad_, datos_};
            if (hc_.control_out(dir_, ControladorUsb::setup(0x21, 0x20, 0, iface_, 7), lc, mps0_) == PID_NADIE) {
                estado_ = Estado::SinDispositivo; return;
            }
            linea_pend_ = false;
        }
        if (lineas_pend_) {
            if (hc_.control_out(dir_, ControladorUsb::setup(0x21, 0x22, lineas_, iface_, 0), {}, mps0_) == PID_NADIE) {
                estado_ = Estado::SinDispositivo; return;
            }
            lineas_pend_ = false;
        }
        if (break_pend_) {
            hc_.control_out(dir_, ControladorUsb::setup(0x21, 0x23, break_ ? 0xFFFF : 0, iface_, 0), {}, mps0_);
            break_pend_ = false;
        }
        // Del dispositivo al terminal: hasta ocho paquetes, o el primer NAK
        std::vector<uint8_t> p;
        for (int k = 0; k < 8; ++k) {
            const uint8_t r = hc_.paquete(PID_IN, dir_, ep_in_, {}, p);
            if (r == PID_NADIE) { estado_ = Estado::SinDispositivo; return; }
            if (r != PID_ACK) break;
            if (!p.empty()) { canal_->escribir(p.data(), p.size()); n_hacia_ += p.size(); }
            if (p.size() < mps_in_) break;
        }
        // Del terminal al dispositivo: hasta ocho paquetes, o el primer NAK
        uint8_t b[512];
        while (hacia_mcu_.size() < 8u * mps_out_) {
            const std::size_t n = canal_->leer(b, sizeof b);
            if (!n) break;
            hacia_mcu_.insert(hacia_mcu_.end(), b, b + n);
        }
        for (int k = 0; k < 8 && !hacia_mcu_.empty(); ++k) {
            const size_t n = std::min<size_t>(hacia_mcu_.size(), mps_out_);
            const std::vector<uint8_t> s(hacia_mcu_.begin(), hacia_mcu_.begin() + long(n));
            const uint8_t r = hc_.paquete(PID_OUT, dir_, ep_out_, s, p);
            if (r == PID_NADIE) { estado_ = Estado::SinDispositivo; return; }
            if (r != PID_ACK) break;
            hacia_mcu_.erase(hacia_mcu_.begin(), hacia_mcu_.begin() + long(n));
            n_desde_ += n;
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_PUENTE_USB_H
