// =============================================================================
// gui_cliente.h — El lado de mcu-sim del saludo con mcu-sim-gui
//
// Fase 3 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`):
// conectar, presentarse, mandar la placa y el catálogo, y quedarse esperando a
// que la ventana diga «arranca». Todo esto ocurre ANTES de `sc_start()`, con
// lecturas bloqueantes normales: no hay un solo proceso de SystemC
// involucrado, y por eso este fichero no incluye `<systemc>` y se prueba sin él
// (`make gui-proto`, parte P4).
//
// La secuencia es la de `doc/protocolo.md` §3:
//
//   mcu-sim                        mcu-sim-gui
//      |---------- T_HOLA ------------>|   quién soy, qué versión hablo
//      |<-------- T_VERSION -----------|   la que elige la GUI
//      |---------- T_PLACA ----------->|   la placa declarada, en XML
//      |-------- T_CATALOGO ---------->|   observables y mandos
//      |---------- T_LISTO ----------->|   construido y esperando
//      |<--- (T_SUSCRIBE, T_ORDENES) --|   se leen y, en esta fase, se ignoran
//      |<-------- T_ARRANCA -----------|   ¡ahora!
//
// Mientras espera `T_ARRANCA` contesta `T_PING` con `T_PONG`, y un `T_PARA`
// termina el saludo sin simular nada. La espera NO tiene plazo —es el «la
// simulación no empieza hasta que la GUI lo diga»— y no gasta CPU: es un
// `select` que duerme hasta que llega algo.
//
// Con `--valida` el saludo se corta tras el catálogo, sin `T_LISTO`: la GUI
// recibe la placa para enseñarla y el modelo termina sin esperar a nadie.
//
// Lo que NO hace: nada en marcha. Instantáneas, avisos, órdenes y control son
// de las fases 4 a 6; aquí solo se deja el socket abierto y el `T_FIN` final.
// =============================================================================
#ifndef STM32_COMMON_GUI_CLIENTE_H
#define STM32_COMMON_GUI_CLIENTE_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include "red.h"
#include "proto_io.h"
#include "gui_destino.h"

namespace stm32 {
namespace gui {

// Cuánto se espera a que la GUI conteste a T_HOLA. La espera de T_ARRANCA no
// tiene plazo; esta sí, porque una GUI que no contesta al saludo no va a
// contestar a nada, y un `mcu-sim` colgado sin decir por qué es peor que uno
// que termina diciéndolo.
inline constexpr int PLAZO_VERSION_MS = 10000;

class ClienteGui {
public:
    enum class Desenlace {
        Arranca,     // llegó T_ARRANCA: a simular
        Para,        // llegó T_PARA antes de arrancar: se termina sin simular
        Valida,      // --valida: placa y catálogo mandados, y se termina
        Error        // y `error()` dice qué
    };

    ClienteGui() = default;
    ~ClienteGui() { red::cerrar(s_); }
    ClienteGui(const ClienteGui&) = delete;
    ClienteGui& operator=(const ClienteGui&) = delete;

    // Se conecta a la ventana. false si no hay nadie escuchando.
    bool conecta(const Destino& d, int plazo_ms = 5000) {
        s_ = red::conecta(d.host.c_str(), d.puerto, plazo_ms);
        if (!red::valido(s_)) {
            error_ = "no hay nadie escuchando en " + como_texto(d) +
                     ": mcu-sim-gui tiene que estar abierta antes";
            return false;
        }
        return true;
    }
    bool conectado() const { return red::valido(s_); }

    // El saludo entero. `hola` es el cuerpo de T_HOLA (líneas clave=valor);
    // `placa` y `catalogo`, los dos XML. Con `solo_valida` se para tras el
    // catálogo. En `arr` queda el cuerpo de T_ARRANCA si llega.
    Desenlace saluda(const std::string& hola, const std::string& placa,
                     const std::string& catalogo, bool solo_valida,
                     mcusim::proto::Arranca& arr,
                     int plazo_version_ms = PLAZO_VERSION_MS) {
        using namespace mcusim::proto;
        if (!conectado()) return falla("no hay conexion con la GUI");

        // --- T_HOLA y T_VERSION --------------------------------------------
        std::string sal;
        em_.mensaje(sal, T_HOLA, hola);
        if (!manda(sal)) return falla("la GUI cerro la conexion al recibir T_HOLA");
        Msj m;
        if (!lee(m, plazo_version_ms)) {
            if (error_.empty())
                error_ = "la GUI no contesto a T_HOLA en " +
                         (plazo_version_ms % 1000 == 0
                              ? std::to_string(plazo_version_ms / 1000) + " s"
                              : std::to_string(plazo_version_ms) + " ms");
            return Desenlace::Error;
        }
        if (m.tipo != T_VERSION)
            return falla("lo primero que manda la GUI tiene que ser T_VERSION, y ha "
                         "mandado el tipo 0x" + hex(m.tipo));
        const long v = valor_entero(m.cuerpo, "protocolo");
        if (v < 0)
            return falla("la GUI ha contestado a T_HOLA sin decir que version "
                         "elige: falta la linea protocolo=N");
        if (v == 0)
            return falla("la GUI no habla ninguna version del protocolo que este "
                         "mcu-sim conozca (ofrecia hasta la " +
                         std::to_string(VERSION_PROTO) + ")");
        if (v < 1 || v > long(VERSION_PROTO))
            return falla("la GUI ha elegido la version " + std::to_string(v) +
                         ", y este mcu-sim solo ofrecia hasta la " +
                         std::to_string(VERSION_PROTO));
        version_ = uint16_t(v);
        em_.fija_version(version_);
        lec_.fija_version(version_);

        // --- La placa y el catálogo ----------------------------------------
        sal.clear();
        em_.mensaje(sal, T_PLACA, placa);
        em_.mensaje(sal, T_CATALOGO, catalogo);
        if (!solo_valida) em_.vacio(sal, T_LISTO);
        if (!manda(sal)) return falla("la GUI cerro la conexion durante el saludo");
        if (solo_valida) return Desenlace::Valida;

        // --- Y a esperar, sin plazo ----------------------------------------
        for (;;) {
            if (!lee(m, -1)) {
                if (error_.empty()) error_ = "la GUI cerro la conexion antes de arrancar";
                return Desenlace::Error;
            }
            switch (m.tipo) {
                case T_ARRANCA:
                    if (m.cuerpo.size() != sizeof arr)
                        return falla("T_ARRANCA con un cuerpo de " +
                                     std::to_string(m.cuerpo.size()) + " bytes; son " +
                                     std::to_string(sizeof arr));
                    std::memcpy(&arr, m.cuerpo.data(), sizeof arr);
                    return Desenlace::Arranca;
                case T_PARA:
                    return Desenlace::Para;
                case T_PING: {
                    sal.clear();
                    em_.vacio(sal, T_PONG);
                    if (!manda(sal)) return falla("la GUI cerro la conexion");
                    break;
                }
                default:
                    // T_SUSCRIBE, T_ORDENES y el control: son de las fases 4 a 6.
                    // Se leen enteros y se cuentan; no se pierde la sincronía.
                    ++ignorados_;
                    break;
            }
        }
    }

    // T_FIN, y se cierra. Si la GUI ya no está, no pasa nada: escribir en un
    // socket cerrado no mata el proceso (`red.h`), y no hay nadie a quien
    // decírselo.
    void fin(uint32_t motivo, int32_t codigo, uint64_t t_sim_ns) {
        if (!conectado()) return;
        std::string sal;
        em_.pod(sal, mcusim::proto::T_FIN, mcusim::proto::Fin{motivo, codigo, t_sim_ns});
        manda(sal);
        red::cerrar(s_);
    }

    const std::string& error() const { return error_; }
    uint16_t           version() const { return version_; }
    unsigned           ignorados() const { return ignorados_; }

    // El valor numérico de `clave=` en un cuerpo de texto de líneas clave=valor,
    // o -1 si no está o no es un número. Público porque también lo usan las
    // pruebas.
    static long valor_entero(const std::string& t, const std::string& clave) {
        std::size_t i = 0;
        while (i < t.size()) {
            std::size_t f = t.find('\n', i);
            if (f == std::string::npos) f = t.size();
            std::string l = t.substr(i, f - i);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (l.compare(0, clave.size() + 1, clave + "=") == 0) {
                const std::string v = l.substr(clave.size() + 1);
                if (v.empty() || v.size() > 9) return -1;
                long n = 0;
                for (char c : v) {
                    if (c < '0' || c > '9') return -1;
                    n = n * 10 + (c - '0');
                }
                return n;
            }
            i = f + 1;
        }
        return -1;
    }

private:
    struct Msj { uint16_t tipo = 0; std::string cuerpo; };

    Desenlace falla(const std::string& e) { error_ = e; return Desenlace::Error; }

    static std::string hex(uint32_t v) {
        static const char d[] = "0123456789ABCDEF";
        std::string s;
        for (int i = 12; i >= 0; i -= 4) s += d[(v >> i) & 0xF];
        return s;
    }

    // Todo, aunque el socket no bloqueante acepte menos de una vez.
    bool manda(const std::string& d) {
        std::size_t hecho = 0;
        const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (hecho < d.size()) {
            const long k = red::enviar(s_, d.data() + hecho, d.size() - hecho);
            if (k > 0) { hecho += std::size_t(k); continue; }
            if (k < 0 && red::reintentar() && std::chrono::steady_clock::now() < fin) {
                red::detalle::espera_escribible(s_, 100);
                continue;
            }
            return false;
        }
        return true;
    }

    // El siguiente mensaje que esta versión conoce; los desconocidos se
    // saltan. `plazo_ms` < 0: sin plazo. false si se acaba el plazo (con
    // `error_` vacío), si se cierra la conexión o si llega algo que no es el
    // protocolo (con `error_` diciendo qué).
    bool lee(Msj& m, int plazo_ms) {
        using namespace mcusim::proto;
        const auto t0 = std::chrono::steady_clock::now();
        char b[4096];
        for (;;) {
            Mensaje x;
            const Lector::Estado e = lec_.saca(x);
            if (e == Lector::LEC_ERROR) { error_ = "la GUI manda algo que no es el protocolo: " +
                                                   lec_.error(); return false; }
            if (e == Lector::LEC_MENSAJE) {
                if (!es_conocido(x.tipo)) continue;
                m.tipo = x.tipo;
                m.cuerpo = x.texto();
                return true;
            }
            int espera = 1000;
            if (plazo_ms >= 0) {
                const long pasado = long(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - t0).count());
                if (pasado >= plazo_ms) return false;
                espera = int(std::min<long>(1000, plazo_ms - pasado));
            }
            if (!red::espera_legible(s_, espera)) continue;
            const long k = red::recibir(s_, b, sizeof b);
            if (k == 0) { error_ = "la GUI cerro la conexion"; return false; }
            if (k < 0) {
                if (red::reintentar()) continue;
                error_ = "error al leer de la GUI";
                return false;
            }
            lec_.mete(b, std::size_t(k));
        }
    }

    red::socket_t          s_ = red::invalido();
    mcusim::proto::Emisor  em_;
    mcusim::proto::Lector  lec_{mcusim::proto::Origen::Pantalla};
    std::string            error_;
    uint16_t               version_ = 0;
    unsigned               ignorados_ = 0;
};

} // namespace gui
} // namespace stm32

#endif // STM32_COMMON_GUI_CLIENTE_H
