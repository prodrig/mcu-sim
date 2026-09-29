// =============================================================================
// canal_host.h — El otro lado de un PuenteSerie: por dónde llegan y se van
//                los bytes del ordenador
//
// Fase D2 del plan de `doc/analisis_puente_serie.md` §10. La pieza habla con
// los pines por un lado (el motor UART) y con el anfitrión por el otro, y este
// es el segundo lado. Es una INTERFAZ porque va a haber tres:
//
//   CanalMemoria   una cola en proceso. Para las pruebas, para `guion=` y para
//                  ver en la consola lo que imprime el firmware.     fase D2
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
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <string>
#include "../common/red.h"   // CanalTcp: la unica dependencia del SO

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

    // Para los canales sin aviso: mirar el sistema operativo -aceptar, leer lo
    // que haya, mandar lo pendiente- sin bloquear. La pieza lo llama en tiempo
    // simulado; un banco de pruebas puede llamarlo tambien, en tiempo de pared,
    // para que lo que hace el anfitrion este DENTRO antes de dejar avanzar la
    // simulacion. Eso es lo que hace que el tiempo simulado no dependa de lo
    // rapida que sea la maquina.
    virtual void sondear() {}

    // Bytes del anfitrion que esperan a ir al MCU.
    virtual std::size_t pendientes() const = 0;

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
    std::size_t pendientes() const override { return entrada_.size(); }
    uint64_t rechazados() const { return n_rechazados_; }

private:
    std::size_t         max_;
    std::deque<uint8_t> entrada_;
    std::string         salida_;
    sc_core::sc_event   llega_;
    uint64_t            n_rechazados_ = 0;
    bool                avisado_ = false;
};

// ---------------------------------------------------------------------------
// El canal TCP en crudo (fase D3)
// ---------------------------------------------------------------------------
// Un servidor en localhost -solo ahí, como los stubs de GDB (decisión D-7)- en
// el que los bytes del socket son los de la línea. Todo lo que es del sistema
// operativo va por common/red.h, que sigue siendo el único fichero que lo sabe.
//
// UN CLIENTE, Y EL NUEVO SUSTITUYE AL VIEJO (D-5). Los redirectores de COM se
// reconectan solos cuando el alumno cierra y abre su terminal, y a veces el
// cierre del anterior no ha llegado todavía: rechazar al nuevo dejaría al
// alumno sin puerto hasta que caducara una conexión muerta.
//
// CONTROL DE FLUJO GRATIS HACIA EL MCU. Cuando la cola hacia el MCU está llena
// se deja de leer el socket, y lo que el anfitrión siga mandando se queda en el
// búfer del núcleo; cuando ese se llena, TCP para al emisor. No se tira nada.
// Hacia el anfitrión no hay esa suerte: si no hay cliente, o si el cliente no
// lee y se acumulan 64 KiB, lo que manda el MCU se descarta y se cuenta.
class CanalTcp : public CanalHost {
public:
    static constexpr std::size_t MAX_SALIDA = 64 * 1024;

    explicit CanalTcp(unsigned puerto, std::size_t cola = 4096)
        : puerto_(puerto), max_(cola) {
        srv_ = red::escucha_local(puerto_);
    }
    ~CanalTcp() override { red::cerrar(cli_); red::cerrar(srv_); }

    // ¿Se pudo abrir el puerto? Si no, lo más probable es que lo tenga otro
    // programa: otro mcu-sim, un GDB, un servidor cualquiera.
    bool abierto() const { return red::valido(srv_); }
    unsigned puerto() const { return puerto_; }

    void sondear() override {
        if (!red::valido(srv_)) return;
        // Aceptar, y si ya había alguien, sustituirlo.
        for (;;) {
            red::socket_t c = red::acepta(srv_);
            if (!red::valido(c)) break;
            if (red::valido(cli_)) {
                suelta_cliente();
                ++n_sustituidos_;
                if (!avisado_sust_) {
                    avisado_sust_ = true;
                    std::fflush(stdout);
                    std::fprintf(stderr, "  [serie] localhost:%u: un cliente nuevo "
                                 "sustituye al anterior\n", puerto_);
                }
            }
            cli_ = c;
            ++n_conexiones_;
        }
        if (!red::valido(cli_)) return;
        // Leer lo que haya, mientras quepa.
        char b[512];
        while (entrada_.size() < max_) {
            const std::size_t hueco = std::min(sizeof b, max_ - entrada_.size());
            const long n = red::recibir(cli_, b, hueco);
            if (n > 0) {
                for (long i = 0; i < n; ++i) entrada_.push_back(uint8_t(b[i]));
                n_recibidos_ += uint64_t(n);
                continue;
            }
            if (n < 0 && red::reintentar()) break;          // no hay más, por ahora
            suelta_cliente();                              // cerró, o se rompió
            return;
        }
        vacia_salida();
    }

    std::size_t leer(uint8_t* b, std::size_t n) override {
        std::size_t k = 0;
        while (k < n && !entrada_.empty()) { b[k++] = entrada_.front(); entrada_.pop_front(); }
        return k;
    }
    void escribir(const uint8_t* b, std::size_t n) override {
        if (!red::valido(cli_)) { n_descartados_ += n; return; }
        for (std::size_t i = 0; i < n; ++i) {
            if (salida_.size() >= MAX_SALIDA) { ++n_descartados_; continue; }
            salida_ += char(b[i]);
        }
        vacia_salida();
    }
    bool conectado() const override { return red::valido(cli_); }
    std::string describir() const override {
        return "TCP en crudo en localhost:" + std::to_string(puerto_);
    }
    std::size_t pendientes() const override { return entrada_.size(); }

    uint64_t conexiones()  const { return n_conexiones_; }
    uint64_t sustituidos() const { return n_sustituidos_; }
    uint64_t recibidos()   const { return n_recibidos_; }

private:
    void vacia_salida() {
        while (red::valido(cli_) && !salida_.empty()) {
            const long n = red::enviar(cli_, salida_.data(),
                                       std::min<std::size_t>(salida_.size(), 1024));
            if (n > 0) { salida_.erase(0, std::size_t(n)); continue; }
            if (n < 0 && red::reintentar()) return;        // el núcleo está lleno
            suelta_cliente();
            return;
        }
    }
    // Lo que quedara por mandar a un cliente que se va, se pierde y se cuenta.
    void suelta_cliente() {
        red::cerrar(cli_);
        n_descartados_ += salida_.size();
        salida_.clear();
    }

    unsigned            puerto_;
    std::size_t         max_;
    red::socket_t       srv_ = red::invalido();
    red::socket_t       cli_ = red::invalido();
    std::deque<uint8_t> entrada_;
    std::string         salida_;
    uint64_t            n_conexiones_ = 0, n_sustituidos_ = 0, n_recibidos_ = 0;
    bool                avisado_sust_ = false;
};

} // namespace stm32

#endif // STM32_PARTS_CANAL_HOST_H
