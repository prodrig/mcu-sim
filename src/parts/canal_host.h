// =============================================================================
// canal_host.h — El otro lado de un PuenteSerie: por dónde llegan y se van
//                los bytes del ordenador
//
// Fase D2 del plan de `doc/analisis_puente_serie.md` §10. La pieza habla con
// los pines por un lado (el motor UART) y con el anfitrión por el otro, y este
// es el segundo lado. Es una INTERFAZ porque va a haber tres:
//
//   CanalMemoria   una cola en proceso. Para las pruebas, para `guion=` y para
//                  ver en la consola lo que imprime el firmware. Esta fase
//   CanalTcp       TCP en crudo sobre common/red.h                   fase D3
//   CanalRfc2217   TCP con Telnet y la opción 44                     fase D5
//
// LAS DOS REGLAS DEL CONTRATO, que valen para los tres:
//
//   * NADIE BLOQUEA. `leer()` devuelve lo que haya, aunque sea cero bytes, y
//     `escribir()` acepta siempre: si al otro lado no hay nadie, o no cabe, se
//     descarta y se cuenta. El simulador no puede quedarse parado porque el
//     terminal del alumno esté cerrado (§4.2 del análisis).
//   * EL AVISO ES OPCIONAL. Un canal que sabe cuándo llegan datos -el de
//     memoria, porque se los mete el propio proceso- ofrece un `sc_event` y la
//     pieza duerme sobre él. Uno que no lo sabe -un socket- devuelve nullptr y
//     la pieza lo sondea en tiempo simulado, como hacen los stubs de GDB
//     (decisión D-9).
// =============================================================================
#ifndef STM32_PARTS_CANAL_HOST_H
#define STM32_PARTS_CANAL_HOST_H

#include <systemc>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <string>

namespace stm32 {

class CanalHost {
public:
    virtual ~CanalHost() = default;

    // Hacia el MCU: hasta `n` bytes de lo que haya mandado el anfitrión.
    // Devuelve cuántos ha puesto en `b`; cero si no hay nada. No bloquea.
    virtual std::size_t leer(uint8_t* b, std::size_t n) = 0;

    // Hacia el anfitrión: lo que ha mandado el MCU. No bloquea y no falla: lo
    // que no se puede entregar se descarta y se cuenta en `descartados()`.
    virtual void escribir(const uint8_t* b, std::size_t n) = 0;

    // ¿Hay alguien al otro lado?
    virtual bool conectado() const = 0;

    // Para el mensaje de arranque: «en memoria (sin red)», «RFC 2217 en...».
    virtual std::string describir() const = 0;

    // El evento que avisa de que hay algo que leer, o nullptr si hay que
    // sondear. Véase la cabecera.
    virtual const sc_core::sc_event* aviso() const { return nullptr; }

    // Bytes hacia el anfitrión que se han tirado porque no había a quién
    // dárselos o no cabían.
    uint64_t descartados() const { return n_descartados_; }

protected:
    uint64_t n_descartados_ = 0;
};

// ---------------------------------------------------------------------------
// El canal en memoria
// ---------------------------------------------------------------------------
// Lo que el anfitrión «teclea» entra por `empuja()`, con un límite: la cola
// hacia el MCU es de 4 KiB por omisión (§4.2), y lo que no cabe se rechaza y
// se dice una vez. Lo que manda el MCU se guarda entero, porque en memoria no
// hay terminal que lo consuma: quien lo quiera lo lee con `recibido()`.
class CanalMemoria : public CanalHost {
public:
    explicit CanalMemoria(std::size_t cola = 4096) : max_(cola) {}

    std::size_t leer(uint8_t* b, std::size_t n) override {
        std::size_t k = 0;
        while (k < n && !entrada_.empty()) { b[k++] = entrada_.front(); entrada_.pop_front(); }
        return k;
    }
    void escribir(const uint8_t* b, std::size_t n) override {
        salida_.append(reinterpret_cast<const char*>(b), n);
    }
    bool conectado() const override { return true; }
    std::string describir() const override { return "en memoria (sin red)"; }
    const sc_core::sc_event* aviso() const override { return &llega_; }

    // --- Lo que usa el banco de pruebas, o la propia pieza con `guion=` ----
    // Mete bytes hacia el MCU. Devuelve cuántos han cabido.
    std::size_t empuja(const std::string& s) {
        std::size_t k = 0;
        for (char c : s) {
            if (entrada_.size() >= max_) {
                ++n_rechazados_;
                if (!avisado_) {
                    avisado_ = true;
                    std::fflush(stdout);        // que el aviso salga en su sitio
                    std::fprintf(stderr, "  [serie] la cola hacia el MCU esta llena "
                                 "(%zu bytes): lo que no cabe se tira\n", max_);
                }
                continue;
            }
            entrada_.push_back(uint8_t(c));
            ++k;
        }
        if (k) llega_.notify(sc_core::SC_ZERO_TIME);
        return k;
    }
    // Vacía la cola hacia el MCU (lo que hará PURGE-DATA en la fase D5).
    void purga_entrada() { entrada_.clear(); }

    const std::string& recibido() const { return salida_; }
    void borra_recibido() { salida_.clear(); }
    std::size_t pendientes() const { return entrada_.size(); }
    uint64_t rechazados() const { return n_rechazados_; }

private:
    std::size_t         max_;
    std::deque<uint8_t> entrada_;
    std::string         salida_;
    sc_core::sc_event   llega_;
    uint64_t            n_rechazados_ = 0;
    bool                avisado_ = false;
};

} // namespace stm32

#endif // STM32_PARTS_CANAL_HOST_H
