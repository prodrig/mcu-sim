// =============================================================================
// canopen.h — El BUS DE CAMPO: un puerto CAN a nivel de bit para las piezas de
//             fuera del MCU, un esclavo CANopen (CiA 301) y un analizador
//
// (Fase F6 de doc/analisis_cnc.md, §5.5, la segunda capa.)
//
// Un controlador de motor de verdad -el C5-E de Nanotec y sus parientes- lleva
// su propio controlador CAN y su transceptor, y habla CANopen: se le mandan
// estados, consignas y perfiles por el bus en vez de pasos por un hilo. Aquí
// están las tres piezas que hacen falta para eso:
//
//   PuertoCan       un nodo CAN A NIVEL DE BIT sobre un nodo analógico: el
//                   relleno, el CRC15, el arbitraje, el asentimiento y los
//                   reintentos, con las mismas funciones que el bxCAN del MCU
//                   (periph/can_motor.h). Es lo que hace `CanNode`, pero
//                   pensado para ir DENTRO de otra pieza: entrega los marcos a
//                   quien lo lleva, y en reposo no cuesta nada -no se
//                   despierta en cada bit, solo cuando el hilo cambia-. Con
//                   `escucha`, no asiente ni transmite: un analizador.
//   EsclavoCanOpen  lo de CiA 301 que no depende del aparato: el estado NMT,
//                   el arranque (boot-up), el servidor de SDO -expedito-, los
//                   PDO con su mapeo fijo, la SYNC, el heartbeat y los EMCY,
//                   sobre un diccionario de objetos que llena quien lo lleva
//                   (ControladorMotor, con los de CiA 402)
//   AnalizadorCan   una pieza de banco: escucha el hilo sin tocarlo, apunta
//                   cada marco con su instante y lo que significa en CANopen,
//                   y cuenta la carga del bus y los marcos que nadie asiente
//
// EL HILO ES UN NODO ANALÓGICO, como en `CanWire`: el recesivo lo pone el
// terminador, el dominante es un cero de 20 ohm que gana. El puerto lee
// dominante por debajo de `umbral` voltios (1,65: la mitad de los 3,3 del
// recesivo de un CanWire). Así una pieza con su transceptor dentro se cuelga
// del bus por un pin, sin necesitar el objeto del hilo.
//
// LO QUE NO HACE: los marcos de error activos -un error se ve y se espera al
// reposo, como en `CanNode`-, la resincronización dentro del marco -los relojes
// son ideales- y los SDO por segmentos o por bloques: con objetos de cuatro
// bytes como mucho, no hacen falta.
// =============================================================================
#ifndef STM32_PARTS_CANOPEN_H
#define STM32_PARTS_CANOPEN_H

#include <systemc>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "ext_parts.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// EL PUERTO: un nodo CAN a nivel de bit
// ---------------------------------------------------------------------------
SC_MODULE(PuertoCan) {
    // `id` es el driver del dueño sobre el nodo del hilo (add_pin)
    PuertoCan(sc_core::sc_module_name nm, analog_net_if& bus, int id, double bitrate,
              double umbral = 1.65, bool escucha = false)
        : sc_core::sc_module(nm), bus_(&bus), id_(id), tb_(1.0 / bitrate), umbral_(umbral),
          escucha_(escucha) {
        bus_->set_hiz(id_);
        SC_HAS_PROCESS(PuertoCan);
        SC_THREAD(run);
    }

    // Un marco bien recibido de otro nodo, con el instante de su bit de arranque
    std::function<void(const CanFrame&, double)> al_recibir;
    // Un marco propio que ha salido, asentido
    std::function<void(const CanFrame&, double)> al_enviar;
    // Lo que ve un analizador: cada marco entero, de quien sea, con su instante,
    // si alguien lo ha asentido y cuántos bits ha ocupado en el hilo
    std::function<void(const CanFrame&, double, bool, unsigned)> al_ver;
    // Un error en el hilo: "relleno", "crc", "forma", "bit" o "asentimiento"
    std::function<void(const char*, double)> al_error;

    // Encenderlo: escucha once bits recesivos antes de meterse. Apagarlo: suelta
    // el hilo y olvida lo que tenía que mandar
    void enciende(bool on) {
        if (on == on_) return;
        on_ = on;
        if (!on) { cola_.clear(); bus_->set_hiz(id_); }
        else { tec_ = rec_ = 0; bus_off_ = false; entrando_ = true; }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool encendido() const { return on_; }
    void envia(const CanFrame& f) {
        if (!on_ || escucha_ || bus_off_) return;
        if (cola_.size() >= 64) { ++perdidos_; return; }
        cola_.push_back(f);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void vacia() { cola_.clear(); }
    bool ocupado() const { return !cola_.empty() || tx_; }
    void pon_bitrate(double b) { tb_ = 1.0 / b; }
    double bitrate() const { return 1.0 / tb_; }

    // Lo que ha pasado
    unsigned recibidos() const { return n_rx_; }
    unsigned enviados() const { return n_tx_; }
    unsigned errores() const { return n_err_; }
    unsigned sin_asentir() const { return n_noack_; }
    unsigned arbitrajes_perdidos() const { return n_alst_; }
    unsigned perdidos() const { return perdidos_; }
    unsigned tec() const { return tec_; }
    unsigned rec() const { return rec_; }
    bool pasivo() const { return tec_ >= 128 || rec_ >= 128; }
    bool bus_off() const { return bus_off_; }

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    void espera(double bits) { wait(sc_core::sc_time(tb_ * bits, sc_core::SC_SEC)); }
    void dominante() { bus_->set_drive(id_, 0.0f, 20.0f); }
    void recesivo() { bus_->set_hiz(id_); }
    void poner(bool nivel) { if (nivel) recesivo(); else dominante(); }
    bool leer() const { return bus_->voltage() >= umbral_; }   // 1 = recesivo
    void error(const char* que, double t0) {
        ++n_err_;
        if (al_error) al_error(que, t0);
    }

    void run() {
        recesivo();
        for (;;) {
            if (!on_) { recesivo(); wait(ev_); continue; }
            // Recién encendido: once bits recesivos seguidos antes de meterse
            if (entrando_) { esperar_libre(11.0); entrando_ = false; continue; }
            recesivo();
            if (cola_.empty() || escucha_ || bus_off_) {
                // En reposo no se despierta en cada bit: solo si el hilo cambia
                if (leer()) { wait(ev_ | bus_->value_changed_event()); continue; }
                recibir();
                continue;
            }
            // Con algo que mandar, la intermisión entera: once bits recesivos
            // desde que se soltó el hilo
            if (leer()) {
                const double falta = t_rec_ + 11.0 * tb_ - ahora();
                if (falta >= 2e-12) {
                    wait(sc_core::sc_time(falta, sc_core::SC_SEC), ev_ | bus_->value_changed_event());
                    if (!leer()) recibir();
                    continue;
                }
            }
            // Si el hilo está dominante, es el bit de arranque de otro que ha
            // empezado A LA VEZ, y toca pujar
            transmitir();
        }
    }

    // --- Transmisión, con arbitraje ------------------------------------------
    void transmitir() {
        const CanFrame f = cola_.front();
        std::vector<bool> flags;
        unsigned nst = 0, arb = 0;
        const std::vector<bool> bits = can_wire_bits(f, &flags, &nst, &arb);
        const double t0 = ahora();
        tx_ = true;
        bool ack = false;
        for (size_t i = 0; i < bits.size(); ++i) {
            poner(bits[i]);
            espera(0.75);
            const bool visto = leer();
            if (i == nst + 1u) {
                ack = !visto;                      // la ranura de asentimiento
            } else if (visto != bits[i]) {
                if (i < arb && bits[i] && !flags[i]) {
                    // Ha perdido la puja: suelta el hilo y sigue escuchando el
                    // marco del que ha ganado, sin perder el suyo
                    ++n_alst_;
                    recesivo();
                    tx_ = false;
                    espera(0.25);
                    // Los bits que ya han pasado, sin relleno, y la racha del
                    // flujo con relleno: el último, el dominante que ha ganado
                    std::vector<bool> sr;
                    unsigned run = 0;
                    bool last = true;
                    for (size_t k = 0; k <= i; ++k) {
                        const bool b = k < i ? bits[k] : false;
                        run = (k > 0 && b == last) ? run + 1 : 1;
                        last = b;
                        if (k < i && flags[k]) continue;
                        sr.push_back(b);
                    }
                    decodificar(sr, run, last, t0);
                    return;
                }
                recesivo();
                tx_ = false;
                error("bit", t0);
                sube_tec(8);
                espera(0.25 + 11.0);
                return;
            }
            espera(0.25);
        }
        recesivo();
        tx_ = false;
        t_rec_ = ahora() - 11.0 * tb_;          // desde el delimitador de ACK
        if (ack) {
            ++n_tx_;
            if (tec_ > 0) --tec_;
            cola_.pop_front();
            if (al_ver) al_ver(f, t0, true, unsigned(bits.size()));
            if (al_enviar) al_enviar(f, t0);
        } else {
            ++n_noack_;
            error("asentimiento", t0);
            // Sin nadie que asienta, un nodo error pasivo deja de subir su TEC
            // (ISO 11898-1, la excepción del error de asentimiento)
            if (tec_ < 128) sube_tec(8);
            if (pasivo()) espera(8.0);             // la suspensión del error pasivo
        }
    }
    void sube_tec(unsigned n) {
        tec_ += n;
        if (tec_ >= 256) {
            bus_off_ = true;
            cola_.clear();
            recesivo();
        }
    }

    // --- Recepción, con asentimiento -----------------------------------------
    void recibir() {
        const double t0 = ahora();
        // El bit de arranque ya está en el hilo
        espera(1.0);
        std::vector<bool> sr{false};
        decodificar(sr, 1, false, t0);
    }

    // `sr`: los bits ya vistos, sin relleno; `run` y `last`, la racha del flujo
    // con relleno. Se entra al principio de un tiempo de bit
    void decodificar(std::vector<bool> sr, unsigned run, bool last, double t0) {
        unsigned total = 0, bits_hilo = unsigned(sr.size());
        bool completo = false;
        for (unsigned k = 0; k < 200 && !completo; ++k) {
            espera(0.75);
            const bool b = leer();
            espera(0.25);
            ++bits_hilo;
            if (run == 5) {
                if (b == last) {
                    // Seis dominantes desde el bit de arranque: no es un marco,
                    // es la bandera de un marco de error
                    bool todo_dominante = !b;
                    for (bool x : sr) todo_dominante = todo_dominante && !x;
                    error(todo_dominante && sr.size() <= 6 ? "marco de error" : "relleno", t0);
                    fallo_rx();
                    return;
                }
                last = b;
                run = 1;
                continue;
            }
            sr.push_back(b);
            if (b == last) ++run; else run = 1;
            last = b;
            total = longitud(sr);
            if (total && sr.size() >= total) completo = true;
        }
        if (!completo) { error("forma", t0); fallo_rx(); return; }
        // Un bit de relleno tras el último del CRC sigue en la zona con relleno
        if (run == 5) {
            espera(0.75);
            const bool sb = leer();
            espera(0.25);
            ++bits_hilo;
            if (sb == last) { error("relleno", t0); fallo_rx(); return; }
        }
        const size_t n = sr.size();
        const std::vector<bool> cuerpo(sr.begin(), sr.end() - 15);
        uint16_t crc = 0;
        for (size_t i = n - 15; i < n; ++i) crc = uint16_t((crc << 1) | (sr[i] ? 1u : 0u));
        const bool crc_ok = can_crc15(cuerpo) == crc;
        // El delimitador del CRC
        espera(0.75);
        const bool delim = leer();
        espera(0.25);
        // La ranura de asentimiento: con el marco bien, a dominante; el
        // analizador no la toca, pero mira si alguien lo ha hecho
        bool asentido = false;
        bool bandera = false;
        if (escucha_) {
            espera(0.75);
            asentido = !leer();
            espera(0.25);
            // El delimitador de ACK: dominante, es la bandera de error del que
            // transmitía -sin asentimiento, sale ahí-
            espera(0.75);
            bandera = !leer();
            espera(0.25);
        } else {
            if (crc_ok && delim) dominante(); else recesivo();
            espera(1.0);
            recesivo();
            asentido = crc_ok && delim;
        }
        bits_hilo += 2 + 1 + 7 + 3;   // delimitador, ranura, delimitador, fin, intermisión
        if (!crc_ok) { error("crc", t0); fallo_rx(); return; }
        if (!delim) { error("forma", t0); fallo_rx(); return; }
        const CanFrame f = armar(sr);
        if (rec_ > 0) --rec_;
        if (!asentido) ++n_noack_;
        const double t_bandera = ahora() - tb_;
        esperar_libre();
        ++n_rx_;
        if (al_ver) al_ver(f, t0, asentido, bits_hilo);
        if (bandera) error("marco de error", t_bandera);
        if (!escucha_ && al_recibir) al_recibir(f, t0);
    }
    void fallo_rx() {
        if (rec_ < 255) ++rec_;
        esperar_libre();
    }

    // Cuántos bits sin relleno tiene el marco entero, con el CRC; 0 si aún no
    // se sabe
    static unsigned longitud(const std::vector<bool>& b) {
        if (b.size() < 14) return 0;
        const bool ide = b[13];
        const unsigned cab = ide ? 39u : 19u;
        if (b.size() < cab) return 0;
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (b[cab - 4 + i] ? 1u : 0u);
        if (dlc > 8) dlc = 8;
        const bool rtr = ide ? b[32] : b[12];
        return cab + (rtr ? 0u : dlc * 8u) + 15u;
    }
    static CanFrame armar(const std::vector<bool>& b) {
        CanFrame f;
        f.ide = b[13];
        unsigned p = 1;
        uint32_t base = 0;
        for (unsigned i = 0; i < 11; ++i) base = (base << 1) | (b[p++] ? 1u : 0u);
        if (!f.ide) {
            f.rtr = b[12];
            f.id = base;
            p = 15;
        } else {
            p = 14;
            uint32_t ext = 0;
            for (unsigned i = 0; i < 18; ++i) ext = (ext << 1) | (b[p++] ? 1u : 0u);
            f.id = (base << 18) | ext;
            f.rtr = b[p];
            p += 3;
        }
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (b[p++] ? 1u : 0u);
        f.dlc = uint8_t(dlc > 8 ? 8 : dlc);
        if (!f.rtr)
            for (unsigned k = 0; k < f.dlc; ++k) {
                uint32_t by = 0;
                for (unsigned i = 0; i < 8; ++i) by = (by << 1) | (b[p++] ? 1u : 0u);
                f.data[k] = uint8_t(by);
            }
        return f;
    }

    // El fin de un marco: `bits` recesivos seguidos, contados desde el flanco
    // en que el hilo se ha soltado. Para RECIBIR bastan ocho -el delimitador
    // de ACK y el fin de trama, o el delimitador de un marco de error-: lo que
    // venga después es el siguiente marco. Para TRANSMITIR hacen falta once,
    // con los tres de la intermisión (run). Por eventos, y no muestreando: un
    // muestreo en el borde del bit, que es donde cambia el hilo, se pierde el
    // bit de arranque del que reintenta justo después.
    //
    void esperar_libre(double bits = 8.0) {
        recesivo();
        t_rec_ = leer() ? ahora() : -1.0;
        for (;;) {
            if (!on_) return;
            if (t_rec_ < 0.0) {                        // dominante: a que se suelte
                wait(bus_->value_changed_event() | ev_);
                if (leer()) t_rec_ = ahora();
                continue;
            }
            // (Por debajo de la resolución, 1 ps, una espera es de tiempo cero)
            const double falta = t_rec_ + bits * tb_ - ahora();
            if (falta < 2e-12) return;
            wait(sc_core::sc_time(falta, sc_core::SC_SEC), bus_->value_changed_event() | ev_);
            if (!leer()) t_rec_ = -1.0;
        }
    }

    analog_net_if* bus_;
    int id_;
    double tb_, umbral_;
    bool escucha_;
    bool on_ = false, entrando_ = false, tx_ = false, bus_off_ = false;
    double t_rec_ = -1.0;                       // desde cuándo está recesivo el hilo
    std::deque<CanFrame> cola_;
    unsigned n_rx_ = 0, n_tx_ = 0, n_err_ = 0, n_noack_ = 0, n_alst_ = 0, perdidos_ = 0;
    unsigned tec_ = 0, rec_ = 0;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// LO QUE SIGNIFICA UN MARCO EN CANOPEN, para el analizador y los avisos
// ---------------------------------------------------------------------------
namespace canopen {

enum Nmt : uint8_t { Inicializando = 0, Parado = 4, Operacional = 5, PreOperacional = 127 };

inline const char* texto_nmt(unsigned e) {
    switch (e) {
        case Inicializando:  return "inicializando";
        case Parado:         return "parado";
        case Operacional:    return "operacional";
        case PreOperacional: return "pre-operacional";
        default:             return "?";
    }
}

// Los códigos de aborto de un SDO (CiA 301, 7.2.4.3.17)
enum Aborto : uint32_t {
    SdoBien = 0,
    SdoComando = 0x05040001,       // especificador de comando no válido
    SdoSoloEscritura = 0x06010001, // se lee un objeto que solo se escribe
    SdoSoloLectura = 0x06010002,   // se escribe un objeto que solo se lee
    SdoNoExiste = 0x06020000,      // el objeto no está en el diccionario
    SdoLongitud = 0x06070010,      // el tamaño no cuadra
    SdoSubindice = 0x06090011,     // el subíndice no existe
    SdoRango = 0x06090030,         // el valor se sale del rango
    SdoEstado = 0x08000022,        // no se puede en el estado en que está
};
inline const char* texto_aborto(uint32_t c) {
    switch (c) {
        case SdoComando:       return "comando no valido";
        case SdoSoloEscritura: return "solo se escribe";
        case SdoSoloLectura:   return "solo se lee";
        case SdoNoExiste:      return "el objeto no existe";
        case SdoLongitud:      return "el tamano no cuadra";
        case SdoSubindice:     return "el subindice no existe";
        case SdoRango:         return "valor fuera de rango";
        case SdoEstado:        return "no se puede en este estado";
        default:               return "otro";
    }
}
// Los de emergencia que usa el controlador (CiA 301 y CiA 402)
inline const char* texto_emcy(uint16_t c) {
    switch (c) {
        case 0x0000: return "sin error: se ha quitado";
        case 0x3210: return "sobretension en el bus";
        case 0x3220: return "subtension en el bus";
        case 0x7300: return "fallo de un sensor de posicion";
        case 0x8611: return "error de seguimiento";
        case 0x8130: return "se ha perdido el heartbeat";
        default:     return "";
    }
}

inline uint32_t le32(const CanFrame& f, unsigned i, unsigned n) {
    uint32_t v = 0;
    for (unsigned k = 0; k < n && i + k < 8; ++k) v |= uint32_t(f.data[i + k]) << (8 * k);
    return v;
}

// Una línea legible: qué es el marco en CANopen
inline std::string describe(const CanFrame& f) {
    char b[200];
    const unsigned id = f.id & 0x7FF, nodo = id & 0x7F, fn = id & 0x780;
    if (f.ide) return "identificador extendido: no es CANopen";
    if (f.rtr) { std::snprintf(b, sizeof b, "trama remota"); return b; }
    if (id == 0x000 && f.dlc >= 2) {
        const char* c = f.data[0] == 1 ? "arranca" : f.data[0] == 2 ? "para"
                      : f.data[0] == 0x80 ? "a pre-operacional" : f.data[0] == 0x81 ? "reinicia"
                      : f.data[0] == 0x82 ? "reinicia la comunicacion" : "?";
        if (f.data[1]) std::snprintf(b, sizeof b, "NMT: %s el nodo %u", c, unsigned(f.data[1]));
        else std::snprintf(b, sizeof b, "NMT: %s todos los nodos", c);
        return b;
    }
    if (id == 0x080) return f.dlc ? "SYNC, cuenta " + std::to_string(f.data[0]) : "SYNC";
    if (fn == 0x080 && nodo) {
        const uint16_t c = uint16_t(le32(f, 0, 2));
        std::snprintf(b, sizeof b, "EMCY del nodo %u: %04X %s, registro %02X", nodo, c,
                      texto_emcy(c), unsigned(f.data[2]));
        return b;
    }
    static const char* pdo[] = {"TPDO1", "RPDO1", "TPDO2", "RPDO2", "TPDO3", "RPDO3", "TPDO4",
                                "RPDO4"};
    if (fn >= 0x180 && fn <= 0x500 && nodo) {
        std::snprintf(b, sizeof b, "%s del nodo %u", pdo[(fn - 0x180) / 0x80], nodo);
        return b;
    }
    if ((fn == 0x580 || fn == 0x600) && nodo && f.dlc == 8) {
        const bool pide = fn == 0x600;
        const unsigned cs = f.data[0], idx = unsigned(le32(f, 1, 2)), sub = f.data[3];
        const uint32_t v = le32(f, 4, 4);
        if (cs == 0x80) {
            std::snprintf(b, sizeof b, "SDO %s del nodo %u: ABORTA %04X:%02X, %08X (%s)",
                          pide ? "al" : "", nodo, idx, sub, unsigned(v), texto_aborto(v));
            return b;
        }
        if (pide && cs == 0x40) {
            std::snprintf(b, sizeof b, "SDO al nodo %u: lee %04X:%02X", nodo, idx, sub);
            return b;
        }
        if (pide && (cs & 0xE0) == 0x20) {
            const unsigned n = (cs & 1) ? 4 - ((cs >> 2) & 3) : 4;
            std::snprintf(b, sizeof b, "SDO al nodo %u: escribe %04X:%02X = %u (0x%X)", nodo, idx,
                          sub, unsigned(le32(f, 4, n)), unsigned(le32(f, 4, n)));
            return b;
        }
        if (!pide && cs == 0x60) {
            std::snprintf(b, sizeof b, "SDO del nodo %u: escrito %04X:%02X", nodo, idx, sub);
            return b;
        }
        if (!pide && (cs & 0xE0) == 0x40) {
            const unsigned n = (cs & 1) ? 4 - ((cs >> 2) & 3) : 4;
            std::snprintf(b, sizeof b, "SDO del nodo %u: %04X:%02X vale %u (0x%X)", nodo, idx,
                          sub, unsigned(le32(f, 4, n)), unsigned(le32(f, 4, n)));
            return b;
        }
        std::snprintf(b, sizeof b, "SDO %s nodo %u, comando %02X", pide ? "al" : "del", nodo, cs);
        return b;
    }
    if (fn == 0x700 && nodo && f.dlc >= 1) {
        if (f.data[0] == 0) std::snprintf(b, sizeof b, "boot-up del nodo %u", nodo);
        else std::snprintf(b, sizeof b, "heartbeat del nodo %u: %s", nodo,
                           texto_nmt(f.data[0] & 0x7F));
        return b;
    }
    return "";
}

} // namespace canopen

// ---------------------------------------------------------------------------
// EL ESCLAVO CANOPEN: lo de CiA 301 que no depende del aparato
//
// Lo lleva una pieza con su PuertoCan. Ella le da el diccionario -cada objeto
// con su tamaño, su acceso y cómo se lee y se escribe- y le llama:
//
//   enciende(t) / apaga()   con la alimentación: el arranque dura arranque_s y
//                           acaba con el boot-up, en pre-operacional
//   recibe(f, t)            cada marco del hilo (al_recibir del puerto)
//   tick(t)                 a menudo -cada lazo-: el heartbeat, los TPDO por
//                           evento y su temporizador
//   proximo()               cuándo quiere que le vuelvan a llamar
//   emcy(codigo, registro)  un mensaje de emergencia
//
// LOS PDO van con su mapeo FIJO -el de omisión, que se lee en 0x1600, 0x1A00 y
// los suyos, y no se cambia-: lo que sí se cambia es el COB-ID (el bit 31, que
// lo apaga) y el tipo de transmisión. Un RPDO de tipo 0 a 240 se aplica en la
// SYNC siguiente; uno de 254 o 255, al llegar. Un TPDO de tipo 1 a 240 sale
// cada tantas SYNC; uno de 254 o 255, cuando cambia lo que lleva -respetando
// su tiempo de inhibición- y cada `event timer` ms si lo tiene.
// ---------------------------------------------------------------------------
class EsclavoCanOpen {
public:
    struct Objeto {
        uint8_t bytes = 4;
        char acceso = 'w';                     // 'r' solo lectura, 'w' lectura y escritura
        std::function<uint32_t()> lee;
        std::function<uint32_t(uint32_t)> escribe;   // devuelve un código de aborto
    };
    struct Pdo {
        bool rx = false;
        unsigned n = 0;                        // 0..3
        uint32_t cob = 0, cob_omision = 0, cob_inicial = 0;
        uint8_t tipo = 255;
        uint16_t inhibe_100us = 0, evento_ms = 0;
        std::vector<std::pair<uint32_t, uint8_t>> mapa;   // (índice<<8 | sub, bytes)
        // estado
        std::vector<uint8_t> pendiente;        // un RPDO síncrono por aplicar
        std::vector<uint8_t> enviado;
        double t_enviado = -1.0;
        unsigned syncs = 0;
        bool valido() const { return !(cob & 0x80000000u); }
    };

    EsclavoCanOpen(PuertoCan& p, uint8_t nodo, const std::string& pieza, double arranque_s)
        : p_(&p), nodo_(nodo), pieza_(pieza), arranque_s_(arranque_s) {
        p_->al_recibir = [this](const CanFrame& f, double t) { recibe(f, t); };
        comunes();
    }

    // Lo que avisa al dueño: después de escribir algo (por SDO o PDO), y al
    // reiniciar el nodo
    std::function<void()> al_cambiar;
    std::function<void()> al_reiniciar;
    std::function<void(const std::string&, const std::string&)> avisa;

    void define(uint16_t idx, uint8_t sub, uint8_t bytes, char acceso, std::function<uint32_t()> lee,
                std::function<uint32_t(uint32_t)> escribe = nullptr) {
        Objeto o;
        o.bytes = bytes;
        o.acceso = acceso;
        o.lee = std::move(lee);
        o.escribe = std::move(escribe);
        od_[clave(idx, sub)] = std::move(o);
    }
    // Un número guardado en el propio esclavo, con su valor de omisión
    void define_valor(uint16_t idx, uint8_t sub, uint8_t bytes, char acceso, uint32_t* v) {
        define(idx, sub, bytes, acceso, [v]() { return *v; },
               [v](uint32_t x) { *v = x; return uint32_t(canopen::SdoBien); });
    }
    // Un PDO, con su mapeo fijo: (índice, subíndice, bytes)
    void pdo(bool rx, unsigned n, std::vector<std::pair<uint32_t, uint8_t>> mapa, uint8_t tipo,
             bool encendido = true) {
        Pdo d;
        d.rx = rx;
        d.n = n;
        d.cob_omision = (rx ? 0x200u : 0x180u) + 0x100u * n + nodo_;
        d.cob_inicial = d.cob_omision | (encendido ? 0u : 0x80000000u);
        d.cob = d.cob_inicial;
        d.tipo = tipo;
        d.mapa = std::move(mapa);
        pdos_.push_back(d);
        tipos_omision_.push_back(tipo);
        const size_t k = pdos_.size() - 1;
        const uint16_t com = uint16_t((rx ? 0x1400 : 0x1800) + n), map = uint16_t((rx ? 0x1600 : 0x1A00) + n);
        define(com, 0, 1, 'r', [rx]() { return rx ? 2u : 5u; });
        define(com, 1, 4, 'w', [this, k]() { return pdos_[k].cob; },
               [this, k](uint32_t v) {
                   // Solo el bit 31 -apagado o encendido-: el COB-ID es el de omisión
                   if ((v & 0x7FFu) != (pdos_[k].cob_omision & 0x7FFu)) return uint32_t(canopen::SdoRango);
                   pdos_[k].cob = v & 0x800007FFu;
                   return uint32_t(canopen::SdoBien);
               });
        define(com, 2, 1, 'w', [this, k]() { return pdos_[k].tipo; },
               [this, k](uint32_t v) {
                   if (v > 240 && v < 254) return uint32_t(canopen::SdoRango);
                   pdos_[k].tipo = uint8_t(v);
                   pdos_[k].syncs = 0;
                   return uint32_t(canopen::SdoBien);
               });
        if (!rx) {
            define(com, 3, 2, 'w', [this, k]() { return pdos_[k].inhibe_100us; },
                   [this, k](uint32_t v) { pdos_[k].inhibe_100us = uint16_t(v); return 0u; });
            define(com, 5, 2, 'w', [this, k]() { return pdos_[k].evento_ms; },
                   [this, k](uint32_t v) { pdos_[k].evento_ms = uint16_t(v); return 0u; });
        }
        define(map, 0, 1, 'r', [this, k]() { return uint32_t(pdos_[k].mapa.size()); });
        for (size_t j = 0; j < pdos_[k].mapa.size(); ++j)
            define(map, uint8_t(j + 1), 4, 'r', [this, k, j]() {
                return (pdos_[k].mapa[j].first << 8) | uint32_t(pdos_[k].mapa[j].second * 8u);
            });
    }

    // --- La vida del nodo -----------------------------------------------------
    void enciende(double t) {
        p_->enciende(true);
        estado_ = canopen::Inicializando;
        t_bootup_ = t + arranque_s_;
    }
    void apaga() {
        p_->enciende(false);
        estado_ = canopen::Inicializando;
        t_bootup_ = -1.0;
    }
    uint8_t estado() const { return estado_; }
    uint8_t nodo() const { return nodo_; }
    uint8_t registro_error() const { return registro_; }
    void pon_registro(uint8_t r) { registro_ = r; }
    unsigned syncs() const { return n_sync_; }
    unsigned sdos() const { return n_sdo_; }
    unsigned abortos() const { return n_abortos_; }
    unsigned emcys() const { return n_emcy_; }
    unsigned rpdos() const { return n_rpdo_; }
    unsigned tpdos() const { return n_tpdo_; }

    // Cuándo quiere que le llamen a tick(): un instante absoluto, o < 0
    double proximo(double t) const {
        double p = -1.0;
        auto mira = [&](double x) { if (x >= 0.0 && (p < 0.0 || x < p)) p = x; };
        if (estado_ == canopen::Inicializando) mira(t_bootup_);
        if (estado_ != canopen::Inicializando && heartbeat_ms_ > 0)
            mira(t_hb_ + heartbeat_ms_ * 1e-3);
        if (estado_ == canopen::Operacional)
            for (const Pdo& d : pdos_)
                if (!d.rx && d.valido() && d.tipo >= 254 && d.evento_ms > 0)
                    mira(std::max(t, d.t_enviado + d.evento_ms * 1e-3));
        (void)t;
        return p;
    }

    void tick(double t) {
        if (estado_ == canopen::Inicializando) {
            if (t_bootup_ >= 0.0 && t >= t_bootup_ - 1e-12) bootup(t);
            return;
        }
        if (heartbeat_ms_ > 0 && t >= t_hb_ + heartbeat_ms_ * 1e-3 - 1e-12) {
            CanFrame f;
            f.id = 0x700u + nodo_;
            f.dlc = 1;
            f.data[0] = estado_;
            p_->envia(f);
            t_hb_ = t;
        }
        if (estado_ != canopen::Operacional) return;
        // Los TPDO por evento: si ha cambiado lo que llevan, tras su inhibición;
        // y por su temporizador. Se miran cada milisegundo como mucho
        if (t < t_eventos_ + 1e-3 - 1e-12) return;
        t_eventos_ = t;
        for (Pdo& d : pdos_) {
            if (d.rx || !d.valido() || d.tipo < 254) continue;
            const std::vector<uint8_t> v = carga(d);
            const bool cambio = v != d.enviado;
            const bool inhibido = d.t_enviado >= 0.0 && t < d.t_enviado + d.inhibe_100us * 1e-4 - 1e-12;
            const bool tempo = d.evento_ms > 0 && t >= d.t_enviado + d.evento_ms * 1e-3 - 1e-12;
            if ((cambio && !inhibido) || tempo) manda(d, v, t);
        }
    }

    void emcy(uint16_t codigo, uint8_t registro) {
        registro_ = registro;
        if (estado_ == canopen::Inicializando || estado_ == canopen::Parado) return;
        CanFrame f;
        f.id = 0x080u + nodo_;
        f.dlc = 8;
        f.data[0] = uint8_t(codigo);
        f.data[1] = uint8_t(codigo >> 8);
        f.data[2] = registro;
        p_->envia(f);
        ++n_emcy_;
    }

    // --- Los marcos que llegan ------------------------------------------------
    void recibe(const CanFrame& f, double t) {
        if (f.ide || f.rtr || estado_ == canopen::Inicializando) return;
        const unsigned id = f.id & 0x7FF;
        if (id == 0x000) { nmt(f, t); return; }
        if (id == 0x080) { sync(t); return; }
        if (estado_ == canopen::Parado) return;
        if (id == 0x600u + nodo_) { sdo(f); return; }
        if (estado_ != canopen::Operacional) return;
        for (Pdo& d : pdos_) {
            if (!d.rx || !d.valido() || id != (d.cob & 0x7FF)) continue;
            unsigned largo = 0;
            for (auto& m : d.mapa) largo += m.second;
            if (f.dlc < largo) {
                if (avisa) avisa("rpdo_corto", pieza_ + ": llega un RPDO de " + std::to_string(f.dlc) +
                                 " bytes, y su mapeo lleva " + std::to_string(largo) + ": se tira");
                return;
            }
            ++n_rpdo_;
            std::vector<uint8_t> v(f.data.begin(), f.data.begin() + f.dlc);
            if (d.tipo <= 240) d.pendiente = v;
            else aplica(d, v);
        }
    }

private:
    static uint32_t clave(uint16_t idx, uint8_t sub) { return (uint32_t(idx) << 8) | sub; }

    void comunes() {
        define(0x1001, 0, 1, 'r', [this]() { return uint32_t(registro_); });
        define(0x1017, 0, 2, 'w', [this]() { return uint32_t(heartbeat_ms_); },
               [this](uint32_t v) { heartbeat_ms_ = uint16_t(v); return 0u; });
    }

    void bootup(double t) {
        estado_ = canopen::PreOperacional;
        t_hb_ = t;
        CanFrame f;
        f.id = 0x700u + nodo_;
        f.dlc = 1;
        f.data[0] = 0;
        p_->envia(f);
    }

    void nmt(const CanFrame& f, double t) {
        if (f.dlc < 2 || (f.data[1] != 0 && f.data[1] != nodo_)) return;
        switch (f.data[0]) {
            case 0x01: estado_ = canopen::Operacional; break;
            case 0x02: estado_ = canopen::Parado; break;
            case 0x80: estado_ = canopen::PreOperacional; break;
            case 0x81:                                  // reinicia el nodo
                if (al_reiniciar) al_reiniciar();
                [[fallthrough]];
            case 0x82:                                 // reinicia la comunicación
                for (size_t k = 0; k < pdos_.size(); ++k) {
                    pdos_[k].cob = pdos_[k].cob_inicial;
                    pdos_[k].tipo = tipos_omision_[k];
                    pdos_[k].inhibe_100us = pdos_[k].evento_ms = 0;
                    pdos_[k].enviado.clear();
                    pdos_[k].pendiente.clear();
                    pdos_[k].t_enviado = -1.0;
                }
                heartbeat_ms_ = 0;
                p_->vacia();
                bootup(t);
                break;
            default: break;
        }
    }

    void sync(double t) {
        ++n_sync_;
        if (estado_ != canopen::Operacional) return;
        // Primero lo que manda el maestro, después lo que se le contesta
        for (Pdo& d : pdos_)
            if (d.rx && d.valido() && d.tipo <= 240 && !d.pendiente.empty()) {
                aplica(d, d.pendiente);
                d.pendiente.clear();
            }
        for (Pdo& d : pdos_) {
            if (d.rx || !d.valido() || d.tipo == 0 || d.tipo > 240) continue;
            if (++d.syncs >= d.tipo) {
                d.syncs = 0;
                manda(d, carga(d), t);
            }
        }
    }

    std::vector<uint8_t> carga(const Pdo& d) {
        std::vector<uint8_t> v;
        for (auto& m : d.mapa) {
            auto it = od_.find(m.first);
            const uint32_t x = (it != od_.end() && it->second.lee) ? it->second.lee() : 0u;
            for (unsigned k = 0; k < m.second; ++k) v.push_back(uint8_t(x >> (8 * k)));
        }
        return v;
    }
    void manda(Pdo& d, const std::vector<uint8_t>& v, double t) {
        CanFrame f;
        f.id = d.cob & 0x7FF;
        f.dlc = uint8_t(v.size());
        for (size_t k = 0; k < v.size() && k < 8; ++k) f.data[k] = v[k];
        p_->envia(f);
        d.enviado = v;
        d.t_enviado = t;
        ++n_tpdo_;
    }
    void aplica(Pdo& d, const std::vector<uint8_t>& v) {
        size_t i = 0;
        for (auto& m : d.mapa) {
            uint32_t x = 0;
            for (unsigned k = 0; k < m.second; ++k) x |= uint32_t(v[i + k]) << (8 * k);
            i += m.second;
            auto it = od_.find(m.first);
            if (it != od_.end() && it->second.escribe) it->second.escribe(x);
        }
        if (al_cambiar) al_cambiar();
    }

    // El servidor de SDO, expedito
    void sdo(const CanFrame& f) {
        if (f.dlc != 8) return;
        ++n_sdo_;
        const unsigned cs = f.data[0];
        const uint16_t idx = uint16_t(f.data[1] | (f.data[2] << 8));
        const uint8_t sub = f.data[3];
        CanFrame r;
        r.id = 0x580u + nodo_;
        r.dlc = 8;
        r.data[1] = f.data[1];
        r.data[2] = f.data[2];
        r.data[3] = sub;
        auto aborta = [&](uint32_t c) {
            ++n_abortos_;
            r.data[0] = 0x80;
            for (unsigned k = 0; k < 4; ++k) r.data[4 + k] = uint8_t(c >> (8 * k));
            p_->envia(r);
            if (avisa) {
                char b[200];
                std::snprintf(b, sizeof b, "%s: un SDO a %04X:%02X se aborta con %08X: %s",
                              pieza_.c_str(), idx, sub, unsigned(c), canopen::texto_aborto(c));
                avisa("sdo_" + std::to_string(c) + "_" + std::to_string(idx), b);
            }
        };
        auto it = od_.find(clave(idx, sub));
        if (it == od_.end()) {
            bool hay = false;
            for (auto& o : od_) if ((o.first >> 8) == idx) { hay = true; break; }
            aborta(hay ? canopen::SdoSubindice : canopen::SdoNoExiste);
            return;
        }
        Objeto& o = it->second;
        if (cs == 0x40) {                                   // subir: leer
            const uint32_t v = o.lee ? o.lee() : 0u;
            r.data[0] = uint8_t(0x43 | ((4 - o.bytes) << 2));
            for (unsigned k = 0; k < 4; ++k) r.data[4 + k] = k < o.bytes ? uint8_t(v >> (8 * k)) : 0;
            p_->envia(r);
            return;
        }
        if ((cs & 0xE0) == 0x20) {                          // bajar: escribir
            if (!(cs & 0x02)) { aborta(canopen::SdoComando); return; }   // por segmentos
            const unsigned n = (cs & 0x01) ? 4 - ((cs >> 2) & 3) : o.bytes;
            if (n != o.bytes) { aborta(canopen::SdoLongitud); return; }
            if (o.acceso == 'r' || !o.escribe) { aborta(canopen::SdoSoloLectura); return; }
            uint32_t v = 0;
            for (unsigned k = 0; k < n; ++k) v |= uint32_t(f.data[4 + k]) << (8 * k);
            const uint32_t c = o.escribe(v);
            if (c) { aborta(c); return; }
            r.data[0] = 0x60;
            p_->envia(r);
            if (al_cambiar) al_cambiar();
            return;
        }
        aborta(canopen::SdoComando);
    }

    PuertoCan* p_;
    uint8_t nodo_;
    std::string pieza_;
    double arranque_s_;
    uint8_t estado_ = canopen::Inicializando;
    uint8_t registro_ = 0;
    uint16_t heartbeat_ms_ = 0;
    double t_bootup_ = -1.0, t_hb_ = 0.0, t_eventos_ = -1.0;
    std::map<uint32_t, Objeto> od_;
    std::vector<Pdo> pdos_;
    std::vector<uint8_t> tipos_omision_;
    unsigned n_sync_ = 0, n_sdo_ = 0, n_abortos_ = 0, n_emcy_ = 0, n_rpdo_ = 0, n_tpdo_ = 0;
};

// ---------------------------------------------------------------------------
// EL ANALIZADOR: escucha el hilo sin tocarlo
//
// No asiente ni transmite: es lo que se cuelga de un bus para ver qué pasa
// por él sin cambiarlo. Apunta cada marco -su instante, su identificador, sus
// bytes y lo que significa en CANopen- en `traza=`, y cuenta los marcos, los
// errores, los que nadie ha asentido y la CARGA del bus: los bits que ocupan
// sus marcos, de media en cada `ventana_ms`, contra los que caben.
// ---------------------------------------------------------------------------
SC_MODULE(AnalizadorCan), public ExtPartBase {
    AnalizadorCan(sc_core::sc_module_name nm, analog_net_if& bus, double bitrate,
                  const std::string& traza, double ventana_ms, double umbral)
        : sc_core::sc_module(nm), ExtPartBase("AnalizadorCan", nm), bitrate_(bitrate),
          ventana_(ventana_ms * 1e-3) {
        add_ref("bus", bus);
        // Un driver que nunca tira del hilo: el puerto en escucha no lo usa
        const int id = bus.register_driver("analizador_can");
        puerto_.reset(new PuertoCan("puerto", bus, id, bitrate, umbral, true));
        puerto_->al_ver = [this](const CanFrame& f, double t, bool ack, unsigned bits) {
            ve(f, t, ack, bits);
        };
        puerto_->al_error = [this](const char* que, double t) { falla(que, t); };
        puerto_->enciende(true);
        if (!traza.empty()) {
            traza_.open(traza);
            if (!traza_)
                SC_REPORT_WARNING("analizador_can", (pieza() + ": no puedo escribir " + traza).c_str());
            else
                traza_ << "# " << pieza() << ": cada marco del bus CAN a " << bitrate_ / 1e3
                       << " kbit/s\n#   ms  id  dlc  bytes  -  lo que es en CANopen\n";
        }
    }
    void set_enabled(bool on) override {
        conectada_ = on;
        puerto_->enciende(on);
    }

    unsigned tramas() const { return n_; }
    unsigned errores() const { return n_err_; }
    unsigned sin_asentir() const { return n_noack_; }
    double carga() const { return carga_; }               // %, en la última ventana
    double carga_maxima() const { return carga_max_; }
    double carga_media() const {
        const double t = sc_core::sc_time_stamp().to_seconds();
        return t > 0.0 ? 100.0 * bits_ / (bitrate_ * t) : 0.0;
    }
    unsigned por_tipo(const std::string& tipo) const {
        auto it = tipos_.find(tipo);
        return it == tipos_.end() ? 0u : it->second;
    }
    const std::map<std::string, unsigned>& tipos() const { return tipos_; }
    unsigned ultimo_id() const { return ultimo_; }

    unsigned n_observables() const override { return 5; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return {"tramas", "", 0.f, 1e6f, true};
            case 1: return {"carga", "%", 0.f, 100.f, true};
            case 2: return {"errores", "", 0.f, 1e6f, false};
            case 3: return {"sin_asentir", "", 0.f, 1e6f, false};
            default: return {"ultimo_id", "", 0.f, 2047.f, false};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(n_);
            case 1: return float(carga_);
            case 2: return float(n_err_);
            case 3: return float(n_noack_);
            default: return float(ultimo_);
        }
    }

    // El tipo CANopen de un identificador, para las cuentas
    static std::string tipo_de(const CanFrame& f) {
        const unsigned id = f.id & 0x7FF, fn = id & 0x780, nodo = id & 0x7F;
        if (f.ide) return "otro";
        if (id == 0) return "NMT";
        if (id == 0x080) return "SYNC";
        if (fn == 0x080) return "EMCY";
        if (fn >= 0x180 && fn <= 0x500 && nodo) return ((fn - 0x180) / 0x80) % 2 ? "RPDO" : "TPDO";
        if (fn == 0x580 && nodo) return "SDO-resp";
        if (fn == 0x600 && nodo) return "SDO-pide";
        if (fn == 0x700 && nodo) return f.dlc && f.data[0] == 0 ? "boot-up" : "heartbeat";
        return "otro";
    }

private:
    void ventana(double t) {
        while (t >= t_ventana_ + ventana_) {
            carga_ = 100.0 * bits_ventana_ / (bitrate_ * ventana_);
            carga_max_ = std::max(carga_max_, carga_);
            bits_ventana_ = 0;
            t_ventana_ += ventana_;
        }
    }
    void ve(const CanFrame& f, double t, bool ack, unsigned bits) {
        ventana(t);
        ++n_;
        if (!ack) ++n_noack_;
        bits_ += bits;
        bits_ventana_ += bits;
        ultimo_ = f.id & 0x7FF;
        ++tipos_[tipo_de(f)];
        if (!traza_) return;
        char b[160];
        int k = std::snprintf(b, sizeof b, "%11.4f  %03X  %u ", t * 1e3, unsigned(f.id & 0x7FF),
                              unsigned(f.dlc));
        for (unsigned i = 0; i < 8; ++i)
            k += std::snprintf(b + k, sizeof b - size_t(k), i < f.dlc ? " %02X" : "   ",
                               unsigned(f.data[i]));
        traza_ << b << "  " << canopen::describe(f) << (ack ? "" : "  [NADIE LO ASIENTE]") << "\n";
    }
    void falla(const char* que, double t) {
        ++n_err_;
        if (traza_) {
            char b[100];
            if (std::string(que) == "marco de error")
                std::snprintf(b, sizeof b, "%11.4f  ---  un marco de error\n", t * 1e3);
            else
                std::snprintf(b, sizeof b, "%11.4f  ---  error de %s en el hilo\n", t * 1e3, que);
            traza_ << b;
        }
    }

    double bitrate_, ventana_;
    std::unique_ptr<PuertoCan> puerto_;
    std::ofstream traza_;
    unsigned n_ = 0, n_err_ = 0, n_noack_ = 0, ultimo_ = 0;
    double bits_ = 0.0, bits_ventana_ = 0.0, t_ventana_ = 0.0, carga_ = 0.0, carga_max_ = 0.0;
    std::map<std::string, unsigned> tipos_;
};

} // namespace stm32

#endif // STM32_PARTS_CANOPEN_H
