// =============================================================================
// gui_mensajes.h — Lo que el saludo y el enlace con mcu-sim-gui comparten, sin
// sockets y sin SystemC
//
// Dos cosas:
//
//   * CanalGui: por donde salen y entran los bytes una vez la simulación está
//     en marcha. En `sim` es el socket de la conexión con la ventana; en el
//     banco `testgui`, un par de búferes en memoria. Así el enlace —el
//     `SC_THREAD` que atiende la conexión en marcha (`parts/enlace_gui.h`)— se
//     prueba entero, contrapresión incluida, sin un solo socket y de forma
//     determinista. Es la misma idea que el canal en memoria del puente serie;
//   * los cuerpos de T_AVISO, T_SUSCRIBE y T_ORDENES, que hay que escribir o
//     leer en más de un sitio.
//
// No va en `proto_io.h` porque aquel fichero es el mismo en los dos
// repositorios, y esto solo lo usa el lado del modelo.
// =============================================================================
#ifndef STM32_COMMON_GUI_MENSAJES_H
#define STM32_COMMON_GUI_MENSAJES_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "protocolo.h"

namespace stm32 {
namespace gui {

// ---------------------------------------------------------------------------
// El canal
// ---------------------------------------------------------------------------
class CanalGui {
public:
    virtual ~CanalGui() = default;
    // >0: bytes leídos. 0: el otro extremo cerró. -1: ahora no hay nada.
    virtual long recibe(char* b, std::size_t n) = 0;
    // >=0: bytes aceptados (0 quiere decir «ahora no cabe nada»). -1: cerrado.
    virtual long envia(const char* b, std::size_t n) = 0;
    // Espera, como mucho `ms`, a poder escribir. Solo se usa al TERMINAR, fuera
    // de la simulación, para vaciar lo pendiente. Por omisión no espera.
    virtual void espera_escritura(int ms) { (void)ms; }
    virtual void cierra() = 0;
};

// ---------------------------------------------------------------------------
// T_AVISO: CabAviso + el origen + el texto
// ---------------------------------------------------------------------------
inline std::string cuerpo_aviso(uint32_t nivel, const std::string& origen,
                                const std::string& texto, uint64_t t_sim_ns) {
    mcusim::proto::CabAviso c{nivel, uint32_t(origen.size()), t_sim_ns};
    std::string s(reinterpret_cast<const char*>(&c), sizeof c);
    return s + origen + texto;
}

// ---------------------------------------------------------------------------
// T_SUSCRIBE: CabSuscribe + n x uint16_t. false si no mide lo que dice.
// ---------------------------------------------------------------------------
inline bool lee_suscripcion(const std::string& cuerpo, uint64_t& periodo_ns,
                            std::vector<uint16_t>& ids) {
    mcusim::proto::CabSuscribe c{};
    if (cuerpo.size() < sizeof c) return false;
    std::memcpy(&c, cuerpo.data(), sizeof c);
    if (cuerpo.size() != sizeof c + std::size_t(c.n) * 2u) return false;
    periodo_ns = uint64_t(c.periodo_ns_lo) | (uint64_t(c.periodo_ns_hi) << 32);
    ids.resize(c.n);
    for (uint32_t i = 0; i < c.n; ++i)
        std::memcpy(&ids[i], cuerpo.data() + sizeof c + 2u * i, 2);
    return true;
}

inline std::string cuerpo_suscripcion(uint64_t periodo_ns, const std::vector<uint16_t>& ids) {
    mcusim::proto::CabSuscribe c{uint32_t(periodo_ns & 0xFFFFFFFFu),
                                 uint32_t(periodo_ns >> 32), uint32_t(ids.size()), 0u};
    std::string s(reinterpret_cast<const char*>(&c), sizeof c);
    for (uint16_t id : ids) s.append(reinterpret_cast<const char*>(&id), 2);
    return s;
}

// ---------------------------------------------------------------------------
// T_ORDENES: una o más `Orden` de 16 bytes, sin cabecera. false si está vacío
// o no es un múltiplo de 16: un mensaje así no se aplica ni a medias.
// ---------------------------------------------------------------------------
inline bool lee_ordenes(const std::string& cuerpo, std::vector<mcusim::proto::Orden>& v) {
    const std::size_t n = sizeof(mcusim::proto::Orden);
    v.clear();
    if (cuerpo.empty() || cuerpo.size() % n != 0) return false;
    v.resize(cuerpo.size() / n);
    std::memcpy(v.data(), cuerpo.data(), cuerpo.size());
    return true;
}

inline std::string cuerpo_ordenes(const std::vector<mcusim::proto::Orden>& v) {
    return std::string(reinterpret_cast<const char*>(v.data()),
                       v.size() * sizeof(mcusim::proto::Orden));
}

} // namespace gui
} // namespace stm32

#endif // STM32_COMMON_GUI_MENSAJES_H
