// =============================================================================
// puente_ethernet.h — El Ethernet de un MCU, hacia el ordenador (T7 del MSP432)
//
// Fases E3 y E4 de doc/analisis_puente_ethernet.md, sobre la frontera de
// tramas (E1): la pieza se pone al otro lado del cable de un PHY de tramas
// (`eth_cable_if`) y lleva sus tramas a
//
//   memoria                      a ningún sitio: se cuentan y, con `pcap=`, se
//                                guardan para Wireshark
//   qemu:PUERTO                  un servidor TCP en localhost con el protocolo
//                                de `-netdev stream` de QEMU: cada trama, con su
//                                longitud delante en 32 bits big-endian. Lo
//                                hablan gvproxy, passt, socket_vmnet, QEMU y
//                                otro mcu-sim
//   qemu-cliente:HOST:PUERTO     lo mismo, conectándose (y reconectándose cada
//                                segundo) a un servidor que ya está
//
// y `,pcap=FICHERO` detrás de cualquiera de ellos guarda las dos direcciones
// en un fichero pcap, con el tiempo SIMULADO en la marca de tiempo.
//
// El canal es el `CanalTcp` del puente serie (D3, D8): no bloquea nunca, y se
// sondea en tiempo simulado cada 100 µs mientras hay tráfico y cada
// milisegundo en reposo. Con `--tiempo-real` eso es tiempo de pared.
// =============================================================================
#ifndef STM32_PARTS_PUENTE_ETHERNET_H
#define STM32_PARTS_PUENTE_ETHERNET_H

#include <systemc>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "canal_host.h"
#include "../periph/msp432e4/emac.h"

namespace stm32 {

struct DestinoEth {
    enum class Tipo { Memoria, Servidor, Cliente, Malo } tipo = Tipo::Malo;
    std::string host = "127.0.0.1";
    unsigned puerto = 0;
    std::string pcap;
    std::string error;
};

inline DestinoEth parsea_destino_eth(std::string s) {
    DestinoEth d;
    const size_t coma = s.find(",pcap=");
    if (coma != std::string::npos) { d.pcap = s.substr(coma + 6); s = s.substr(0, coma); }
    auto puerto = [&](const std::string& p) {
        char* fin = nullptr;
        const unsigned long v = std::strtoul(p.c_str(), &fin, 10);
        if (p.empty() || *fin || v == 0 || v > 65535) { d.error = "puerto no valido: " + p; return 0u; }
        return unsigned(v);
    };
    if (s == "memoria") { d.tipo = DestinoEth::Tipo::Memoria; return d; }
    if (s.rfind("qemu:", 0) == 0) {
        d.tipo = DestinoEth::Tipo::Servidor;
        d.puerto = puerto(s.substr(5));
    } else if (s.rfind("qemu-cliente:", 0) == 0) {
        const std::string r = s.substr(13);
        const size_t dp = r.rfind(':');
        if (dp == std::string::npos) { d.error = "falta el puerto: qemu-cliente:HOST:PUERTO"; }
        else { d.tipo = DestinoEth::Tipo::Cliente; d.host = r.substr(0, dp); d.puerto = puerto(r.substr(dp + 1)); }
    } else {
        d.error = "destino de Ethernet desconocido: " + s +
                  " (memoria, qemu:PUERTO o qemu-cliente:HOST:PUERTO, y ,pcap=FICHERO)";
    }
    if (!d.error.empty()) d.tipo = DestinoEth::Tipo::Malo;
    return d;
}

// El fichero pcap: la cabecera global y, por trama, su cabecera y sus bytes
class Pcap {
public:
    bool abre(const std::string& ruta) {
        f_ = std::fopen(ruta.c_str(), "wb");
        if (!f_) return false;
        const uint32_t g[6] = {0xA1B2C3D4u, 0x00040002u, 0, 0, 65535u, 1u};   // Ethernet
        std::fwrite(g, 4, 6, f_);
        return true;
    }
    ~Pcap() { if (f_) std::fclose(f_); }
    void trama(const std::vector<uint8_t>& t) {
        if (!f_) return;
        const double s = sc_core::sc_time_stamp().to_seconds();
        const uint32_t h[4] = {uint32_t(s), uint32_t((s - double(uint32_t(s))) * 1e6),
                               uint32_t(t.size()), uint32_t(t.size())};
        std::fwrite(h, 4, 4, f_);
        std::fwrite(t.data(), 1, t.size(), f_);
        std::fflush(f_);
        ++n_;
    }
    unsigned tramas() const { return n_; }
private:
    std::FILE* f_ = nullptr;
    unsigned n_ = 0;
};

// No es un sc_module: se monta cuando se aplican las opciones de los chips,
// que es con la simulacion ya en marcha, y por eso su hilo es un sc_spawn.
class PuenteEthernet : public eth_cable_if {
public:
    PuenteEthernet(EphyE4& phy, const DestinoEth& d) : phy_(phy), d_(d) {
        if (d.tipo == DestinoEth::Tipo::Servidor) canal_.reset(new CanalTcp(d.puerto, 64 * 1024));
        else if (d.tipo == DestinoEth::Tipo::Cliente) canal_.reset(new CanalTcp(d.host, d.puerto, 64 * 1024));
        if (canal_ && !canal_->abierto()) error_ = "no se puede escuchar en el " + std::to_string(d.puerto);
        if (!d.pcap.empty() && !pcap_.abre(d.pcap)) error_ = "no se puede escribir " + d.pcap;
        phy_.conecta_cable(this);
        if (canal_) sc_core::sc_spawn([this] { hilo(); });
    }
    ~PuenteEthernet() override { phy_.conecta_cable(nullptr); }

    const std::string& error() const { return error_; }
    bool abierto() const { return !canal_ || canal_->abierto(); }
    std::string describir() const {
        std::string s;
        switch (d_.tipo) {
            case DestinoEth::Tipo::Memoria: s = "en memoria (sin red)"; break;
            case DestinoEth::Tipo::Servidor: s = "tramas por TCP (protocolo de QEMU) en localhost:" +
                                                 std::to_string(d_.puerto); break;
            case DestinoEth::Tipo::Cliente: s = "tramas por TCP (protocolo de QEMU) hacia " + d_.host +
                                                ":" + std::to_string(d_.puerto); break;
            default: s = "?";
        }
        if (!d_.pcap.empty()) s += ", captura en " + d_.pcap;
        return s;
    }
    unsigned hacia_host() const { return n_hacia_; }
    unsigned desde_host() const { return n_desde_; }
    unsigned capturadas() const { return pcap_.tramas(); }

    // ---- eth_cable_if: lo que sale del MCU ----------------------------------
    void trama_del_cable(const std::vector<uint8_t>& t) override {
        ++n_hacia_;
        pcap_.trama(t);
        if (!canal_) return;
        std::vector<uint8_t> m(4 + t.size());
        m[0] = uint8_t(t.size() >> 24); m[1] = uint8_t(t.size() >> 16);
        m[2] = uint8_t(t.size() >> 8);  m[3] = uint8_t(t.size());
        std::copy(t.begin(), t.end(), m.begin() + 4);
        canal_->escribir(m.data(), m.size());
        trafico_ = true;
    }

private:
    EphyE4& phy_;
    DestinoEth d_;
    std::unique_ptr<CanalTcp> canal_;
    Pcap pcap_;
    std::string error_;
    std::vector<uint8_t> buf_;
    unsigned n_hacia_ = 0, n_desde_ = 0;
    bool trafico_ = false;

    // El sondeo: lo que haya en el socket, troceado en tramas
    void hilo() {
        for (;;) {
            canal_->sondear();
            uint8_t b[2048];
            for (;;) {
                const std::size_t n = canal_->leer(b, sizeof b);
                if (!n) break;
                buf_.insert(buf_.end(), b, b + n);
                trafico_ = true;
            }
            while (buf_.size() >= 4) {
                const uint32_t len = (uint32_t(buf_[0]) << 24) | (uint32_t(buf_[1]) << 16) |
                                     (uint32_t(buf_[2]) << 8) | buf_[3];
                if (len > 65535) { buf_.clear(); break; }          // desincronizado
                if (buf_.size() < 4 + len) break;
                std::vector<uint8_t> t(buf_.begin() + 4, buf_.begin() + 4 + long(len));
                buf_.erase(buf_.begin(), buf_.begin() + 4 + long(len));
                ++n_desde_;
                pcap_.trama(t);
                phy_.de_cable(t);
            }
            const bool hubo = trafico_;
            trafico_ = false;
            sc_core::wait(sc_core::sc_time(hubo ? 100 : 1000, sc_core::SC_US));
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_PUENTE_ETHERNET_H
