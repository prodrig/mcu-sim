// =============================================================================
// cliente_2217.h — Un cliente RFC 2217 mínimo, para el banco del puente UART
//
// Fase D5 (P-14, doc/analisis_puente_serie.md §10). Hace de terminal del
// alumno en `testserie`: se conecta, negocia como lo hace el cliente de
// pySerial (WILL SGA, DO SGA, WILL COM-PORT, y acepta BINARY cuando se lo
// piden), manda datos con el IAC doblado y manda órdenes de la opción 44
// esperando su respuesta.
//
// NO ESPERA POR SU CUENTA EN EL SOCKET. Cada espera llama a `bomba`, que el
// banco pone a «sondea el canal del puente»: así lo que el cliente manda está
// dentro del puente, y lo que el puente contesta está fuera, SIN que avance el
// tiempo simulado. Es la misma regla que hace determinista el banco de la D3.
//
// Y ESPERA A QUE SE LO HAYAN DICHO TODO (`sincroniza`). No basta con esperar a
// lo que se busca: una respuesta del cliente -el WILL BINARY que contesta a un
// DO- puede seguir en el núcleo cuando la condición ya se cumple, y entrar en el
// puente en un sondeo en tiempo simulado u otro según lo rápido que sea el
// loopback. En Linux daba igual; en los macOS del CI, no (fase D5).
// `sincroniza` espera a que los bytes que ha mandado cada lado sean los que ha
// recibido el otro, contados por los dos.
//
// Usa el códec de common/telnet2217.h, que se probó en la D4 contra el texto de
// las RFC y contra pySerial; aquí no se prueba el códec, se prueba el puente.
// =============================================================================
#ifndef STM32_VERIF_CLIENTE_2217_H
#define STM32_VERIF_CLIENTE_2217_H

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "../common/red.h"
#include "../common/telnet2217.h"
#include "../parts/canal_host.h"

namespace stm32 {

class Cliente2217 {
public:
    std::function<void()> bomba = [] {};
    // Un loopback lento, en el otro sentido: con esto, `espera` solo recoge el
    // socket una vez de cada cuatro. Lo pone el banco con TESTSERIE_RUIDO.
    bool ruido = false;

    ~Cliente2217() { cierra(); }

    bool conecta(unsigned puerto) {
        cierra();
        s_ = red::conecta_local(puerto);
        dec_ = telnet::Decodificador{};
        datos_.clear(); sucesos_.clear();
        bin_sal_ = bin_ent_ = com_port_ = false;
        n_bytes_ = n_enviados_ = 0;
        crudo_.clear();
        return red::valido(s_);
    }
    void cierra() { red::cerrar(s_); }
    bool conectado() const { return red::valido(s_); }

    // Lo que hace el cliente de pySerial al abrir, y espera a que el servidor
    // haya aceptado COM-PORT y BINARY en los dos sentidos.
    bool negocia() {
        using namespace telnet;
        manda_crudo(negociacion(WILL, OPT_SGA) + negociacion(DO, OPT_SGA) +
                    negociacion(WILL, OPT_COM_PORT));
        return espera([&] { return com_port_ && bin_sal_ && bin_ent_; });
    }

    // Datos hacia el MCU: con el IAC doblado y, si todavía no se manda en
    // BINARY, con el CR NUL del NVT.
    bool datos(const std::string& s) { return manda_crudo(telnet::escapa(s, bin_sal_)); }

    // Una orden de la opción 44 y su respuesta. Devuelve false si no llega en
    // dos segundos de reloj; en `r` queda el valor que contesta el servidor.
    bool orden(uint8_t cod, const std::vector<uint8_t>& v, std::vector<uint8_t>& r) {
        manda_crudo(telnet::orden_2217(cod, v));
        const uint8_t esp = uint8_t(cod + telnet::cpo::RESPUESTA);
        return espera([&] { return saca(esp, r); });
    }
    // La misma, cuando el valor que se espera es un byte.
    int orden1(uint8_t cod, uint8_t v) {
        std::vector<uint8_t> r;
        if (!orden(cod, {v}, r) || r.size() != 1) return -1;
        return r[0];
    }
    // Sin esperar respuesta (SUSPEND, RESUME, una firma propia, o una orden
    // mandada a propósito sin haber negociado).
    bool orden_sin_respuesta(uint8_t cod, const std::vector<uint8_t>& v) {
        return manda_crudo(telnet::orden_2217(cod, v));
    }

    bool manda_crudo(const std::string& m) {
        std::size_t hecho = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (hecho < m.size()) {
            const long n = red::enviar(s_, m.data() + hecho, m.size() - hecho);
            if (n > 0) { hecho += std::size_t(n); n_enviados_ += uint64_t(n); continue; }
            if (n < 0 && red::reintentar() && segundos(t0) < 2.0) { bomba(); continue; }
            return false;
        }
        bomba();
        return true;
    }

    // Recoge lo que haya en el socket y lo pasa por el decodificador,
    // contestando a las negociaciones del servidor.
    void recoge() {
        char b[1024];
        for (;;) {
            const long n = red::recibir(s_, b, sizeof b);
            if (n <= 0) return;
            n_bytes_ += uint64_t(n);
            crudo_.append(b, std::size_t(n));
            for (long i = 0; i < n; ++i) {
                std::vector<uint8_t> d;
                std::vector<telnet::Suceso> ev;
                dec_.alimenta(reinterpret_cast<const uint8_t*>(b + i), 1, d, ev);
                datos_.append(d.begin(), d.end());
                for (auto& e : ev) atiende(e);
            }
        }
    }

    // Espera, en tiempo de PARED y bombeando, a que se cumpla algo.
    bool espera(const std::function<bool()>& c, double seg = 2.0) {
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            bomba();
            if (!ruido || (++n_esperas_ % 4u) == 0u) recoge();
            if (c()) return true;
            if (segundos(t0) > seg) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // Espera a que el cliente y el canal se lo hayan dicho TODO, en los dos
    // sentidos: lo que ha salido de uno ha llegado al otro. Con las respuestas
    // que el cliente da solo (a las negociaciones), hasta que nadie mande nada.
    bool sincroniza(const CanalTcp& c) {
        return espera([&] {
            return n_bytes_ == c.enviados_conexion() && n_enviados_ == c.recibidos_conexion();
        });
    }
    // Deja pasar un rato de pared bombeando: para comprobar que NO llega nada.
    void calma(double seg = 0.05) { espera([] { return false; }, seg); }

    // Los datos que han llegado (sin Telnet), y los sucesos que no se han
    // consumido como respuesta de una orden.
    std::string& recibido() { return datos_; }
    const std::string& crudo() const { return crudo_; }
    void borra() { datos_.clear(); crudo_.clear(); }

    // Las notificaciones (u otras respuestas) de un código, en orden de
    // llegada, y se sacan de la lista.
    std::vector<std::vector<uint8_t>> saca_todas(uint8_t cod) {
        std::vector<std::vector<uint8_t>> r;
        std::vector<uint8_t> v;
        while (saca(cod, v)) r.push_back(v);
        return r;
    }
    std::size_t sucesos_pendientes() const { return sucesos_.size(); }

    bool binario_salida()  const { return bin_sal_; }
    bool binario_entrada() const { return bin_ent_; }
    bool com_port()        const { return com_port_; }
    uint64_t bytes() const { return n_bytes_; }
    uint64_t enviados() const { return n_enviados_; }

private:
    static double segundos(std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    void atiende(const telnet::Suceso& e) {
        using namespace telnet;
        if (e.tipo == Suceso::Tipo::negociacion) {
            if (e.verbo == DO && e.opcion == OPT_BINARY && !bin_sal_) {
                bin_sal_ = true;
                manda_crudo(negociacion(WILL, OPT_BINARY));
            } else if (e.verbo == WILL && e.opcion == OPT_BINARY && !bin_ent_) {
                bin_ent_ = true;
                dec_.set_binario(true);
                manda_crudo(negociacion(DO, OPT_BINARY));
            } else if (e.verbo == DO && e.opcion == OPT_COM_PORT) {
                com_port_ = true;
            }
            return;
        }
        OrdenCpo o;
        if (es_cpo(e, o)) sucesos_.push_back(o);
    }

    bool saca(uint8_t cod, std::vector<uint8_t>& v) {
        for (auto it = sucesos_.begin(); it != sucesos_.end(); ++it)
            if (it->cod == cod) { v = it->valor; sucesos_.erase(it); return true; }
        return false;
    }

    red::socket_t              s_ = red::invalido();
    telnet::Decodificador      dec_;
    std::string                datos_, crudo_;
    std::vector<telnet::OrdenCpo> sucesos_;
    bool                       bin_sal_ = false, bin_ent_ = false, com_port_ = false;
    uint64_t                   n_bytes_ = 0, n_enviados_ = 0;
    unsigned                   n_esperas_ = 0;
};

} // namespace stm32

#endif // STM32_VERIF_CLIENTE_2217_H
