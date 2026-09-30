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
#include <vector>
#include "../common/red.h"         // CanalTcp: la unica dependencia del SO
#include "../common/telnet2217.h"  // CanalRfc2217: el protocolo, puro

namespace stm32 {

// ---------------------------------------------------------------------------
// Lo que el anfitrión puede pedirle a la LÍNEA (fase D5)
// ---------------------------------------------------------------------------
// RFC 2217 deja que el terminal cambie los baudios, el formato, el control de
// flujo y las líneas de módem. Eso no es del canal, es de la pieza: el canal
// traduce el protocolo y la pieza decide. Cada `pide_*` recibe lo que manda el
// cliente -con los códigos de la RFC, que son los del cable- y devuelve lo que
// ha quedado APLICADO, que es lo que el servidor confirma. El valor 0 es
// siempre «¿qué tienes?». Así, un puente con los baudios fijados en el XML
// contesta con los suyos y el terminal se entera de que no ha colado.
class LineaSerie {
public:
    virtual ~LineaSerie() = default;
    virtual uint32_t pide_baudios(uint32_t v) = 0;   // 0 = consulta
    virtual uint8_t  pide_datos(uint8_t v) = 0;      // 5..8; 0 = consulta
    virtual uint8_t  pide_paridad(uint8_t v) = 0;    // 1 N, 2 O, 3 E, 4 M, 5 S
    virtual uint8_t  pide_parada(uint8_t v) = 0;     // 1 uno, 2 dos, 3 uno y medio
    virtual uint8_t  pide_control(uint8_t v) = 0;    // SET-CONTROL, 0..19
    // CTS (bit 4), DSR (bit 5), RI (bit 6) y DCD (bit 7), como en MODEMSTATE,
    // sin los bits de cambio: esos los pone el canal.
    virtual uint8_t  estado_modem() const = 0;
    virtual std::string firma() const = 0;
};

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

    // Un suceso de la línea que el anfitrión puede querer saber: los bits de
    // LINESTATE de RFC 2217 (bit 2 paridad, bit 3 trama, bit 4 break). Solo lo
    // usa el canal RFC 2217; los demás no tienen por dónde contarlo.
    virtual void notifica_linea(uint8_t) {}
    // Ha cambiado alguna de las líneas de módem que ve el anfitrión (CTS...).
    virtual void notifica_modem() {}

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
            al_conectar();
        }
        if (!red::valido(cli_)) return;
        // Leer lo que haya, mientras quepa.
        char b[512];
        while (entrada_.size() < max_) {
            const std::size_t hueco = std::min(sizeof b, max_ - entrada_.size());
            const long n = red::recibir(cli_, b, hueco);
            if (n > 0) {
                n_recibidos_ += uint64_t(n);
                recibidos(reinterpret_cast<const uint8_t*>(b), std::size_t(n));
                if (!red::valido(cli_)) return;            // lo ha echado el protocolo
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

protected:
    // ---- Lo que cambia un canal que habla un protocolo encima --------------
    // Llegan `n` bytes del cliente. En crudo son datos, tal cual.
    virtual void recibidos(const uint8_t* b, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) entrada_.push_back(b[i]);
    }
    // Hay un cliente nuevo (o uno nuevo que sustituye al anterior).
    virtual void al_conectar() {}
    // ¿Se puede mandar lo que hay en `salida_`? Un cliente puede pedir que no.
    virtual bool puede_mandar() const { return true; }

    // Mete bytes YA COMPUESTOS en la salida, con el mismo límite que los datos.
    // Si no caben, se descartan enteros: media orden de Telnet sería peor que
    // ninguna.
    void encola_crudo(const std::string& s) {
        if (!red::valido(cli_) || s.empty()) return;
        if (salida_.size() + s.size() > MAX_SALIDA) { n_descartados_ += s.size(); return; }
        salida_ += s;
    }
    bool hay_cliente() const { return red::valido(cli_); }

    void vacia_salida() {
        if (!puede_mandar()) return;
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

    std::deque<uint8_t> entrada_;
    std::string         salida_;

private:
    unsigned            puerto_;
    std::size_t         max_;
    red::socket_t       srv_ = red::invalido();
    red::socket_t       cli_ = red::invalido();
    uint64_t            n_conexiones_ = 0, n_sustituidos_ = 0, n_recibidos_ = 0;
    bool                avisado_sust_ = false;
};

// ---------------------------------------------------------------------------
// El canal RFC 2217 (fase D5)
// ---------------------------------------------------------------------------
// Es el canal TCP con Telnet encima y la opción 44. El protocolo -partir el
// flujo, escapar, negociar- es common/telnet2217.h, que se probó solo en la
// fase D4; aquí se le da el socket y se le conecta a la LÍNEA (`LineaSerie`),
// que es la pieza.
//
// LO QUE HACE CON CADA ORDEN DEL CLIENTE (los códigos van del cliente; el
// servidor contesta sumando 100):
//
//   0  SIGNATURE          vacía: contesta con la firma. Con texto: es la del
//                         cliente, se guarda y no se contesta.
//   1..5                  se le pasan a la línea y se contesta lo APLICADO.
//   6, 7                  el cliente pregunta: LINESTATE 0 (los errores son
//                         sucesos, no estado) y el MODEMSTATE actual.
//   8, 9  SUSPEND/RESUME  no se contestan (RFC 2217). Mientras dure, no se le
//                         manda nada: los datos del MCU se guardan, hasta
//                         64 KiB, y lo demás espera en la cola de salida.
//   10, 11                las máscaras; se contesta con la que queda.
//   12    PURGE-DATA      1 = lo que el servidor ha RECIBIDO de la línea y no
//                         ha mandado aún (del MCU al anfitrión); 2 = lo que
//                         tiene para TRANSMITIR a la línea (del anfitrión al
//                         MCU); 3 = las dos.
//
// Ninguna orden de la opción 44 se atiende si el cliente no ha negociado antes
// WILL COM-PORT: la RFC lo exige, y un cliente que no lo hace es un Telnet
// corriente al que no hay que cambiarle los baudios por un SB suelto.
//
// POR CONEXIÓN. Cada cliente nuevo empieza de cero: su negociación, sus
// máscaras y su suspensión. Lo que la línea tiene configurado (baudios,
// formato) se QUEDA: es la placa, no la sesión.
class CanalRfc2217 : public CanalTcp {
public:
    static constexpr uint8_t LINEA_PARIDAD = 0x04, LINEA_TRAMA = 0x08,
                             LINEA_BREAK   = 0x10;
    static constexpr uint8_t MODEM_CTS = 0x10, MODEM_DSR = 0x20, MODEM_RI = 0x40,
                             MODEM_DCD = 0x80;

    CanalRfc2217(unsigned puerto, std::size_t cola = 4096, LineaSerie* linea = nullptr)
        : CanalTcp(puerto, cola), linea_(linea) {}

    void set_linea(LineaSerie* l) { linea_ = l; }

    std::string describir() const override {
        return "RFC 2217 en localhost:" + std::to_string(puerto());
    }

    // Del MCU al anfitrión: con el IAC doblado, y en NVT (si el cliente no ha
    // aceptado BINARY) con el CR NUL. Durante un SUSPEND, se guarda sin
    // escapar: el escape se decide al mandarlo, que es cuando se sabe cómo.
    void escribir(const uint8_t* b, std::size_t n) override {
        if (!hay_cliente()) { n_descartados_ += n; return; }
        if (suspendido_) {
            for (std::size_t i = 0; i < n; ++i) {
                if (retenido_.size() >= MAX_SALIDA) { ++n_descartados_; continue; }
                retenido_ += char(b[i]);
            }
            return;
        }
        std::string e;
        telnet::escapa(b, n, neg_.binario_salida(), e);
        // Aquí sí se va byte a byte contra el límite: son datos, y un dato
        // que no cabe se cuenta como uno.
        for (std::size_t i = 0; i < e.size(); ++i) {
            if (salida_.size() >= MAX_SALIDA) {
                ++n_descartados_;
                if (uint8_t(e[i]) == telnet::IAC && i + 1 < e.size()) ++i;
                continue;
            }
            salida_ += e[i];
        }
        vacia_salida();
    }

    // Un error o un break en lo que llega del MCU. Solo sale si el cliente lo
    // ha pedido con su máscara, que empieza a cero (RFC 2217).
    void notifica_linea(uint8_t bits) override {
        if (!hay_cliente() || !neg_.com_port()) return;
        const uint8_t v = uint8_t(bits & mascara_linea_);
        if (!v) return;
        responde(telnet::cpo::NOTIFY_LINESTATE, {v});
        ++n_linestate_;
    }

    // Han cambiado las líneas de módem. Se calcula qué ha cambiado respecto a
    // lo último que se mandó y, si la máscara lo deja pasar, se manda.
    void notifica_modem() override {
        if (!hay_cliente() || !neg_.com_port() || !linea_) return;
        const uint8_t ahora = uint8_t(linea_->estado_modem() & 0xF0);
        if (ahora == ultimo_modem_) return;
        uint8_t v = ahora;
        const uint8_t delta = uint8_t(ahora ^ ultimo_modem_);
        if (delta & MODEM_CTS) v |= 0x01;
        if (delta & MODEM_DSR) v |= 0x02;
        if (delta & MODEM_RI)  v |= 0x04;
        if (delta & MODEM_DCD) v |= 0x08;
        ultimo_modem_ = ahora;
        if (v & mascara_modem_) {
            responde(telnet::cpo::NOTIFY_MODEMSTATE, {uint8_t(v & mascara_modem_)});
            ++n_modemstate_;
            vacia_salida();
        }
    }

    // --- Lo que mira un banco de pruebas --------------------------------------
    const telnet::Negociador& negociador() const { return neg_; }
    bool     suspendido()        const { return suspendido_; }
    std::size_t retenidos()      const { return retenido_.size(); }
    uint8_t  mascara_linea()     const { return mascara_linea_; }
    uint8_t  mascara_modem()     const { return mascara_modem_; }
    const std::string& firma_cliente() const { return firma_cliente_; }
    uint64_t ordenes()           const { return n_ordenes_; }
    uint64_t ordenes_ignoradas() const { return n_ignoradas_; }
    uint64_t notificaciones_linea() const { return n_linestate_; }
    uint64_t notificaciones_modem() const { return n_modemstate_; }

protected:
    void al_conectar() override {
        dec_ = telnet::Decodificador{};
        neg_ = telnet::Negociador{};
        mascara_linea_ = 0;
        mascara_modem_ = 255;
        ultimo_modem_  = 0;
        suspendido_    = false;
        retenido_.clear();
        firma_cliente_.clear();
    }
    bool puede_mandar() const override { return !suspendido_; }

    // Byte a byte: una negociación cambia cómo se leen los datos que vienen
    // DETRÁS de ella (BINARY), así que no se puede alimentar el bloque entero
    // y luego mirar los sucesos.
    void recibidos(const uint8_t* b, std::size_t n) override {
        std::vector<uint8_t> datos;
        std::vector<telnet::Suceso> ev;
        for (std::size_t i = 0; i < n; ++i) {
            datos.clear();
            ev.clear();
            dec_.alimenta(b + i, 1, datos, ev);
            for (uint8_t c : datos) entrada_.push_back(c);
            for (const auto& s : ev) atiende(s);
        }
        vacia_salida();
    }

private:
    void atiende(const telnet::Suceso& s) {
        using telnet::Suceso;
        if (s.tipo == Suceso::Tipo::negociacion) {
            const bool antes = neg_.com_port();
            encola_crudo(neg_.recibe(s.verbo, s.opcion));
            dec_.set_binario(neg_.binario_entrada());
            // Recién negociado COM-PORT: el estado de las líneas de módem, como
            // hace el servidor de pySerial, para que el cliente no tenga que
            // preguntarlo antes de poder leer el CTS.
            if (!antes && neg_.com_port()) { ultimo_modem_ = 0; notifica_modem(); }
            return;
        }
        telnet::OrdenCpo o;
        if (!telnet::es_cpo(s, o)) return;        // otra subopción, u orden suelta
        if (!neg_.com_port()) { ++n_ignoradas_; return; }
        ++n_ordenes_;
        namespace cpo = telnet::cpo;
        const uint8_t v1 = o.valor.empty() ? 0 : o.valor[0];
        switch (o.cod) {
        case cpo::SIGNATURE:
            if (o.valor.empty()) {
                const std::string f = linea_ ? linea_->firma() : std::string("mcu-sim");
                responde(cpo::SIGNATURE, std::vector<uint8_t>(f.begin(), f.end()));
            } else {
                firma_cliente_.assign(o.valor.begin(), o.valor.end());
            }
            break;
        case cpo::SET_BAUDRATE: {
            uint32_t v = 0;
            if (!telnet::lee_u32_red(o.valor, 0, v)) { ++n_ignoradas_; break; }
            const uint32_t r = linea_ ? linea_->pide_baudios(v) : v;
            responde(cpo::SET_BAUDRATE, telnet::u32_red(r));
            break;
        }
        case cpo::SET_DATASIZE:
            responde(o.cod, {linea_ ? linea_->pide_datos(v1) : uint8_t(8)});
            break;
        case cpo::SET_PARITY:
            responde(o.cod, {linea_ ? linea_->pide_paridad(v1) : uint8_t(1)});
            break;
        case cpo::SET_STOPSIZE:
            responde(o.cod, {linea_ ? linea_->pide_parada(v1) : uint8_t(1)});
            break;
        case cpo::SET_CONTROL:
            responde(o.cod, {linea_ ? linea_->pide_control(v1) : uint8_t(1)});
            break;
        case cpo::NOTIFY_LINESTATE:
            responde(o.cod, {0});
            break;
        case cpo::NOTIFY_MODEMSTATE: {
            const uint8_t m = linea_ ? uint8_t(linea_->estado_modem() & 0xF0) : 0;
            ultimo_modem_ = m;
            responde(o.cod, {uint8_t(m & mascara_modem_)});
            break;
        }
        case cpo::FLOWCONTROL_SUSPEND:
            suspendido_ = true;
            break;
        case cpo::FLOWCONTROL_RESUME:
            if (suspendido_) {
                suspendido_ = false;
                std::string r;
                r.swap(retenido_);
                escribir(reinterpret_cast<const uint8_t*>(r.data()), r.size());
            }
            break;
        case cpo::SET_LINESTATE_MASK:
            mascara_linea_ = v1;
            responde(o.cod, {mascara_linea_});
            break;
        case cpo::SET_MODEMSTATE_MASK:
            mascara_modem_ = v1;
            responde(o.cod, {mascara_modem_});
            break;
        case cpo::PURGE_DATA:
            if (v1 < 1 || v1 > 3) { ++n_ignoradas_; break; }
            if (v1 & 1) retenido_.clear();        // del MCU, sin mandar
            if (v1 & 2) entrada_.clear();         // hacia el MCU, sin transmitir
            responde(o.cod, {v1});
            break;
        default:
            ++n_ignoradas_;
            break;
        }
    }

    void responde(uint8_t cod, const std::vector<uint8_t>& valor) {
        encola_crudo(telnet::orden_2217(uint8_t(cod + telnet::cpo::RESPUESTA), valor));
    }

    LineaSerie*           linea_;
    telnet::Decodificador dec_;
    telnet::Negociador    neg_;
    uint8_t               mascara_linea_ = 0, mascara_modem_ = 255, ultimo_modem_ = 0;
    bool                  suspendido_ = false;
    std::string           retenido_;
    std::string           firma_cliente_;
    uint64_t              n_ordenes_ = 0, n_ignoradas_ = 0, n_linestate_ = 0,
                          n_modemstate_ = 0;
};

} // namespace stm32

#endif // STM32_PARTS_CANAL_HOST_H
