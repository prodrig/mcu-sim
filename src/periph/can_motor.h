// =============================================================================
// can_motor.h — El protocolo CAN a nivel de bit, sin el mapa de registros
//
// El motor que hace de verdad el protocolo -el tiempo de bit, el relleno, el
// CRC15, el arbitraje, el asentimiento, la señalización de errores y los
// contadores TEC/REC- es el mismo en cualquier controlador CAN: lo que cambia
// de un fabricante a otro es cómo se le dan los marcos y cómo se recogen. Por
// eso está aquí, separado, y lo usan dos mapas de registros:
//
//   periph/can.h            el bxCAN de los STM32 (buzones, FIFOs, filtros)
//   periph/msp432e4/ccan.h  el C_CAN de Bosch del MSP432E4 (32 objetos de
//                           mensaje y dos interfaces de registros)
//
// El mapa le dice al motor lo que hace falta con unos pocos ganchos (`cm_*`):
// si está parado, el tiempo de bit, el modo de bucle y el silencioso, qué
// marco sale ahora, y qué hacer cuando un marco sale, se pierde, falla o
// llega. El motor no sabe nada de registros.
//
// Se separó del bxCAN en la T7 del MSP432 sin cambiar una coma de su lógica:
// las suites del F407 no se mueven ni un picosegundo.
// =============================================================================
#ifndef STM32_PERIPH_CAN_MOTOR_H
#define STM32_PERIPH_CAN_MOTOR_H

#include <array>
#include <cstdint>
#include <vector>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Un marco CAN, tal y como viaja por el hilo.
// ---------------------------------------------------------------------------
struct CanFrame {
    uint32_t id  = 0;        // 11 bits si !ide, 29 si ide
    bool     ide = false;    // identificador extendido (CAN 2.0B)
    bool     rtr = false;    // trama remota
    uint8_t  dlc = 0;        // 0..8
    std::array<uint8_t, 8> data{};
};

// ---------------------------------------------------------------------------
// CRC-15 del protocolo CAN: x^15 + x^14 + x^10 + x^8 + x^7 + x^4 + x^3 + 1,
// o sea 0x4599, con valor inicial cero, sobre los bits SIN RELLENO desde el
// bit de arranque hasta el último de datos.
// ---------------------------------------------------------------------------
inline uint16_t can_crc15(const std::vector<bool>& bits) {
    uint16_t crc = 0;
    for (bool b : bits) {
        const bool hi = ((crc >> 14) & 1u) != 0;
        crc = uint16_t((crc << 1) & 0x7FFFu);
        if (hi != b) crc ^= 0x4599u;
    }
    return crc;
}

// Los bits de un marco SIN relleno, desde el bit de arranque hasta el último
// de datos. Es lo que entra en el CRC.
inline std::vector<bool> can_frame_bits(const CanFrame& f) {
    std::vector<bool> v;
    auto push = [&v](uint32_t val, unsigned n) {
        for (int i = int(n) - 1; i >= 0; --i) v.push_back(((val >> i) & 1u) != 0);
    };
    v.push_back(false);                              // SOF: dominante
    if (!f.ide) {
        push(f.id & 0x7FFu, 11);                     // identificador de 11 bits
        v.push_back(f.rtr);                          // RTR
        v.push_back(false);                          // IDE = 0
        v.push_back(false);                          // r0
    } else {
        push((f.id >> 18) & 0x7FFu, 11);             // identificador base
        v.push_back(true);                           // SRR: siempre recesivo
        v.push_back(true);                           // IDE = 1
        push(f.id & 0x3FFFFu, 18);                   // extensión de 18 bits
        v.push_back(f.rtr);                          // RTR
        v.push_back(false);                          // r1
        v.push_back(false);                          // r0
    }
    push(f.dlc & 0xFu, 4);                           // DLC
    const unsigned n = (f.dlc > 8) ? 8u : f.dlc;
    if (!f.rtr) for (unsigned i = 0; i < n; ++i) push(f.data[i], 8);
    return v;
}

// Cuántos bits del principio son campo de ARBITRAJE (sin relleno): es donde un
// nodo puede perder la puja sin que eso sea un error.
inline unsigned can_arb_len(bool ide) { return ide ? 1u + 32u : 1u + 12u; }

// Aplica el RELLENO DE BITS: tras cinco bits iguales seguidos se inserta uno
// del valor contrario. `stuffed_flag` marca cuáles son de relleno, que es lo
// que permite distinguir una pérdida de arbitraje de un error de bit.
inline std::vector<bool> can_stuff(const std::vector<bool>& in,
                                   std::vector<bool>* stuffed_flag = nullptr) {
    std::vector<bool> out;
    if (stuffed_flag) stuffed_flag->clear();
    unsigned run = 0; bool last = false; bool first = true;
    for (bool b : in) {
        if (!first && b == last) ++run; else run = 1;
        last = b; first = false;
        out.push_back(b);
        if (stuffed_flag) stuffed_flag->push_back(false);
        if (run == 5) {
            out.push_back(!b);
            if (stuffed_flag) stuffed_flag->push_back(true);
            last = !b; run = 1;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// La trama COMPLETA que sale por el hilo: la parte con relleno (arranque,
// arbitraje, control, datos y CRC) seguida del delimitador de CRC, la ranura
// de asentimiento, su delimitador, el fin de trama y el espacio entre tramas.
// `n_stuffed` devuelve cuántos bits ocupa la parte sujeta a relleno.
// ---------------------------------------------------------------------------
inline std::vector<bool> can_wire_bits(const CanFrame& f,
                                       std::vector<bool>* stuffed_flag,
                                       unsigned* n_stuffed,
                                       unsigned* arb_bits) {
    std::vector<bool> sin_relleno = can_frame_bits(f);
    const uint16_t crc = can_crc15(sin_relleno);
    for (int i = 14; i >= 0; --i) sin_relleno.push_back(((crc >> i) & 1u) != 0);
    std::vector<bool> flags;
    std::vector<bool> out = can_stuff(sin_relleno, &flags);
    if (n_stuffed) *n_stuffed = unsigned(out.size());
    // Dónde acaba el arbitraje una vez insertados los bits de relleno.
    if (arb_bits) {
        const unsigned arb = can_arb_len(f.ide);
        unsigned reales = 0, i = 0;
        for (; i < out.size() && reales < arb; ++i) if (!flags[i]) ++reales;
        *arb_bits = i;
    }
    // El resto no lleva relleno.
    out.push_back(true);  flags.push_back(false);   // delimitador de CRC
    out.push_back(true);  flags.push_back(false);   // ranura de asentimiento
    out.push_back(true);  flags.push_back(false);   // delimitador de ACK
    for (unsigned i = 0; i < 7; ++i) { out.push_back(true); flags.push_back(false); }
    for (unsigned i = 0; i < 3; ++i) { out.push_back(true); flags.push_back(false); }
    if (stuffed_flag) *stuffed_flag = flags;
    return out;
}

// =============================================================================
// El motor
// =============================================================================
class CanMotor : public BusSlave {
public:
    sc_core::sc_in<bool>  freeze{"freeze"};              // parada por el depurador
    // Los dos pines digitales del controlador: TX (salida) y RX (entrada). El
    // hilo del bus, con su cableado en Y, está fuera del MCU.
    sc_core::sc_signal<bool> tx_out{"tx_out"}, tx_oe{"tx_oe"}, rx_in{"rx_in"};

    // Códigos del último error: los mismos en el bxCAN (ESR.LEC) y en el
    // C_CAN (CANSTS.LEC)
    enum Lec : uint32_t {
        LEC_NONE = 0, LEC_STUFF = 1, LEC_FORM = 2, LEC_ACK = 3,
        LEC_BIT1 = 4, LEC_BIT0 = 5, LEC_CRC = 6
    };

    CanMotor(sc_core::sc_module_name nm, uint32_t base, uint32_t size)
        : BusSlave(nm, base, size) {}

    // ---- Ventanas del banco de pruebas ------------------------------------
    uint64_t frames_tx() const { return n_tx_; }
    uint64_t frames_rx() const { return n_rx_; }
    unsigned tec() const { return tec_; }
    unsigned rec() const { return rec_; }
    double   bit_time() const { return bit_time_s(); }

protected:
    // ---- Los ganchos del mapa de registros -------------------------------
    virtual bool     cm_parado() const = 0;          // inicialización, sueño
    virtual double   cm_tq() const = 0;              // un cuanto de tiempo, en s
    virtual unsigned cm_ts1() const = 0;             // cuantos antes del muestreo
    virtual unsigned cm_ts2() const = 0;             // y después
    virtual bool     cm_bucle() const = 0;           // bucle cerrado
    virtual bool     cm_silencio() const = 0;        // solo escucha
    virtual int      cm_siguiente() const = 0;       // el que sale, o -1
    virtual CanFrame cm_marco(unsigned i) const = 0;
    virtual void cm_tx_empieza() {}
    virtual void cm_rx_empieza() {}
    virtual void cm_rx_fin() {}
    virtual void cm_reposo_tras_error() {}
    virtual void cm_arbitraje_perdido(unsigned i) = 0;
    virtual void cm_tx_ok(unsigned i) = 0;
    virtual void cm_tx_error(unsigned i) = 0;        // con TEC ya sumado
    virtual void cm_rx_ok(const CanFrame& f) = 0;    // un marco ajeno, completo
    virtual void cm_eco() = 0;                       // el propio, sin bucle
    virtual void cm_error() = 0;                     // tras cualquier error
    virtual void cm_bus_off() = 0;                   // TEC llega a 255

    unsigned tec_ = 0, rec_ = 0;
    uint64_t n_tx_ = 0, n_rx_ = 0;
    bool o_tx_ = true, o_oe_ = false;
    sc_core::sc_event pub_ev_, ev_;
    sc_core::sc_event_or_list parado_ev_;   // armada al arrancar bit_proc

    double bit_time_s() const { return cm_tq() * double(1u + cm_ts1() + cm_ts2()); }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }

    // =======================================================================
    // El motor de bit. Es quien hace de verdad el protocolo.
    // =======================================================================
    uint32_t lec_ = 0;
    bool     boff_ = false;
    int      tx_activo_ = -1;
    bool     soy_emisor_ = false;

    bool en_marcha() const {
        return clock_enabled() && !cm_parado() &&
               !freeze.read() && bit_time_s() > 0.0 && !boff_;
    }
    // Lo que ve el nodo en el hilo. En bucle cerrado el bloque se oye a sí
    // mismo sin salir al pin, que es para lo que sirve ese modo.
    bool leer_bus() const { return cm_bucle() ? o_tx_ : rx_in.read(); }
    void poner_bus(bool nivel) {
        // En modo silencioso el nodo NUNCA manda dominante: solo escucha
        // [IR, §12.12]. Es lo que permite engancharse a un bus sin perturbarlo.
        const bool real = (cm_silencio() && !cm_bucle()) ? true : nivel;
        if (real != o_tx_ || !o_oe_) { o_tx_ = real; o_oe_ = true; publish(); }
    }

    void bit_proc() {
        // Igual que en el RCC: la lista de espera del estado «parado» se arma
        // una vez y no en cada vuelta. Ni reserva por despertar, ni una lista
        // perdida si la simulación termina con el proceso suspendido aquí.
        parado_ev_ |= ev_ | rx_in.value_changed_event()
                    | rst_n.value_changed_event() | freeze.value_changed_event()
                    | clk_hz.value_changed_event();
        for (;;) {
            if (!en_marcha()) {
                if (o_tx_ != true || o_oe_) { o_tx_ = true; o_oe_ = false; publish(); }
                wait(parado_ev_);
                continue;
            }
            const double tb = bit_time_s();
            const double t_muestra = tb * double(1u + cm_ts1()) / double(1u + cm_ts1() + cm_ts2());

            // ---- Reposo: se espera a que haya algo que hacer -----------------
            if (estado_ == IDLE) {
                poner_bus(true);
                const bool quiero = (cm_siguiente() >= 0);
                if (!quiero && leer_bus()) {
                    wait(sc_core::sc_time(tb, sc_core::SC_SEC),
                         ev_ | rx_in.value_changed_event());
                    // Sincronización dura: un flanco a dominante en reposo es
                    // el bit de arranque de otro nodo, y el tiempo de bit se
                    // reengancha a él.
                    if (!leer_bus()) { empezar_recepcion(); }
                    continue;
                }
                // Con un buzón pidiendo, el nodo TRANSMITE aunque el hilo se
                // acabe de poner dominante. No es atropellar a nadie: estando
                // en reposo, ese dominante es el bit de arranque de otro nodo
                // que ha empezado EN EL MISMO BIT, y lo que toca entonces es
                // pujar, no callarse. De ahí sale el arbitraje.
                if (quiero) empezar_transmision();
                else        empezar_recepcion();
            }

            // ---- Un tiempo de bit -------------------------------------------
            poner_bus(bit_a_emitir());
            wait(sc_core::sc_time(t_muestra, sc_core::SC_SEC));
            const bool visto = leer_bus();
            procesar_bit(visto);
            wait(sc_core::sc_time(tb - t_muestra, sc_core::SC_SEC));
        }
    }

    // ---- Estado del motor --------------------------------------------------
    enum Estado { IDLE, TX, RX, ERROR_FLAG, INTERMISSION };
    Estado estado_ = IDLE;
    std::vector<bool> tx_bits_, tx_stuff_;
    unsigned tx_i_ = 0, tx_arb_ = 0, tx_nstuff_ = 0;
    unsigned err_i_ = 0, recesivos_ = 0;
    bool     ack_visto_ = false;

    // Receptor
    std::vector<bool> rx_bits_;          // bits SIN relleno
    unsigned rx_run_ = 0; bool rx_last_ = false; bool rx_first_ = true;
    unsigned rx_esperados_ = 0;          // bits que faltan hasta el CRC
    unsigned rx_fase_ = 0;
    CanFrame rx_f_{};
    unsigned post_ = 0;                  // bits tras el CRC ya recorridos
    bool     pend_relleno_ = false;      // queda un bit de relleno tras el CRC
    bool     ack_pendiente_ = false;     // el marco esta bien: hay que asentirlo

    void empezar_transmision() {
        const int i = cm_siguiente();
        if (i < 0) { estado_ = IDLE; return; }
        tx_activo_ = i;
        const CanFrame f = cm_marco(unsigned(i));
        tx_bits_ = can_wire_bits(f, &tx_stuff_, &tx_nstuff_, &tx_arb_);
        tx_i_ = 0;
        ack_visto_ = false;
        estado_ = TX;
        soy_emisor_ = true;
        cm_tx_empieza();
        empezar_recepcion_interna();
    }
    void empezar_recepcion() {
        estado_ = RX;
        soy_emisor_ = false;
        cm_rx_empieza();
        empezar_recepcion_interna();
        // El bit de arranque TODAVIA no se ha muestreado: lo hara el propio
        // motor dentro de este mismo tiempo de bit, y el receptor lo recogera
        // en su fase 0. Contabilizarlo aqui desplazaria el marco entero.
    }
    void empezar_recepcion_interna() {
        rx_bits_.clear(); rx_run_ = 0; rx_first_ = true;
        rx_fase_ = 0; post_ = 0; pend_relleno_ = false;
        ack_pendiente_ = false; rx_f_ = CanFrame{};
    }

    bool bit_a_emitir() const {
        if (estado_ == ERROR_FLAG) return err_i_ >= 6;   // 6 dominantes y suelta
        if (estado_ == TX && tx_i_ < tx_bits_.size()) return tx_bits_[tx_i_];
        // EL ASENTIMIENTO. Un receptor que ha visto el marco entero y con el
        // CRC correcto pone la ranura de asentimiento a DOMINANTE. Es su unica
        // intervencion en un marco ajeno, y sin ella el emisor da el marco por
        // no entregado y lo repite indefinidamente [IR, 12.12].
        if (estado_ == RX && rx_fase_ == 2 && !pend_relleno_ &&
            post_ == 1 && ack_pendiente_) return false;
        return true;
    }

    // Cada bit muestreado pasa por DOS caminos: el del transmisor, que comprueba
    // que el hilo lleva lo que el puso, y el del receptor, que va armando el
    // marco. Los dos a la vez, porque un nodo CAN SIEMPRE escucha lo que
    // transmite: es de ahi de donde salen el arbitraje y el asentimiento.
    void procesar_bit(bool visto) {
        if (estado_ == ERROR_FLAG) {
            ++err_i_;
            if (err_i_ >= 6) { if (visto) ++recesivos_; else recesivos_ = 0; }
            if (recesivos_ >= 8) { estado_ = IDLE; tx_activo_ = -1;
                                   cm_reposo_tras_error(); publish(); }
            return;
        }
        if (estado_ == TX) {
            const bool emitido = tx_bits_[tx_i_];
            const bool en_ack = (tx_i_ == tx_nstuff_ + 1u);
            if (en_ack) {
                // La ranura de asentimiento: el emisor manda recesivo y espera
                // que ALGUIEN la ponga a dominante. Si nadie lo hace, es un
                // error de asentimiento y el marco no ha llegado a nadie.
                //
                // En BUCLE CERRADO el bloque se asiente a si mismo: no hay bus,
                // y el modo existe justamente para probar el nodo solo, sin
                // nadie mas conectado [IR, 12.12].
                ack_visto_ = cm_bucle() ? true : !visto;
            } else if (visto != emitido) {
                if (tx_i_ < tx_arb_ && emitido && !tx_stuff_[tx_i_]) {
                    // Se ha perdido el arbitraje: otro nodo manda un
                    // identificador de prioridad mayor. NO es un error: el
                    // buzón se queda pidiendo y se reintenta.
                    perder_arbitraje();
                    // Y ESTE bit, el que delata la derrota, es del ganador: hay
                    // que darselo al receptor o el marco entero se desplaza un
                    // bit y acaba en un CRC que no cuadra.
                    recibir_bit(visto);
                    return;
                }
                // Fuera del arbitraje, leer distinto de lo emitido es un error
                // de bit: alguien está peleando por el hilo.
                error(emitido ? LEC_BIT1 : LEC_BIT0, true);
                return;
            }
            ++tx_i_;
            // El receptor sigue el marco en paralelo. Si mas adelante se pierde
            // el arbitraje, ya lleva los bits correctos y puede continuar sin
            // haberse perdido el principio.
            recibir_bit(visto);
            if (estado_ == TX && tx_i_ >= tx_bits_.size()) fin_transmision();
            return;
        }
        // ---- Receptor -------------------------------------------------------
        recibir_bit(visto);
    }

    void perder_arbitraje() {
        const unsigned i = unsigned(tx_activo_);
        tx_activo_ = -1;
        // El nodo pasa a RECEPTOR del marco que ha ganado. No hay que empezar de
        // cero: su receptor venia siguiendo el hilo bit a bit, y hasta la puja
        // lo que habia en el hilo era identico a lo suyo.
        estado_ = RX;
        soy_emisor_ = false;
        cm_arbitraje_perdido(i);
    }

    void fin_transmision() {
        const unsigned i = unsigned(tx_activo_);
        if (!ack_visto_) {
            // Nadie ha asentido. El marco se da por no entregado.
            error(LEC_ACK, true);
            return;
        }
        ++n_tx_;
        if (tec_ > 0) --tec_;
        tx_activo_ = -1;
        estado_ = IDLE;
        cm_tx_ok(i);
        publish();
    }

    // El receptor: quita el relleno y va armando el marco.
    void recibir_bit(bool b) {
        if (rx_fase_ == 0) {                     // esperando el bit de arranque
            if (b) return;
            rx_bits_.clear();
            rx_bits_.push_back(false);
            rx_run_ = 1; rx_last_ = false; rx_first_ = false;
            rx_fase_ = 1;
            cm_rx_empieza();
            return;
        }
        if (rx_fase_ == 1) {                     // zona con relleno
            if (rx_run_ == 5) {
                // Este bit es de relleno: tiene que ser el contrario.
                if (b == rx_last_) { error(LEC_STUFF, false); return; }
                rx_last_ = b; rx_run_ = 1;
                return;                          // no entra en el marco
            }
            rx_bits_.push_back(b);
            if (b == rx_last_) ++rx_run_; else rx_run_ = 1;
            rx_last_ = b;
            // Si el ULTIMO bit del CRC completa una racha de cinco, todavia
            // viene un bit de relleno detras: sigue estando dentro de la zona
            // con relleno, y sin tragarselo el delimitador de CRC se muestrea
            // un bit antes de tiempo.
            if (marco_completo()) {
                rx_fase_ = 2;
                pend_relleno_ = (rx_run_ == 5);
                // El CRC se comprueba AQUI, en cuanto llega su ultimo bit,
                // porque de el depende si este nodo va a asentir el marco dos
                // bits mas adelante.
                ack_pendiente_ = crc_cuadra();
            }
            return;
        }
        // ---- Zona sin relleno: delimitador, ACK, fin de trama ---------------
        if (pend_relleno_) {
            pend_relleno_ = false;
            if (b == rx_last_) { error(LEC_STUFF, false); return; }
            return;
        }
        ++post_;
        if (post_ == 1) {                        // delimitador de CRC
            if (!b) { error(LEC_FORM, false); return; }
            return;
        }
        if (post_ == 2) return;                  // ranura de ACK (ya emitida)
        if (post_ == 3) {                        // delimitador de ACK
            if (!b) { error(LEC_FORM, false); return; }
            return;
        }
        if (post_ <= 10) {                       // 7 bits de fin de trama
            if (!b) { error(LEC_FORM, false); return; }
            if (post_ == 10) entregar();
            return;
        }
        // El receptor NO puede sacar al motor del estado de transmision: el
        // transmisor manda hasta su ultimo bit, y es el quien da el marco por
        // entregado.
        if (post_ >= 13 && estado_ == RX) { estado_ = IDLE; cm_rx_fin(); }
    }

    // ¿Están ya todos los bits del marco (hasta el CRC incluido)?
    bool marco_completo() {
        const size_t n = rx_bits_.size();
        // Se necesita saber IDE para conocer la longitud, y luego DLC.
        if (n < 14) return false;
        const bool ide = rx_bits_[13];
        const unsigned cab = ide ? 39u : 19u;    // hasta el final de DLC
        if (n < cab) return false;
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (rx_bits_[cab - 4 + i] ? 1u : 0u);
        if (dlc > 8) dlc = 8;
        const bool rtr = ide ? rx_bits_[32] : rx_bits_[12];
        const unsigned total = cab + (rtr ? 0u : dlc * 8u) + 15u;
        return n >= total;
    }

    // ¿Cuadra el CRC15 de lo recibido con el que viaja en el marco?
    bool crc_cuadra() const {
        const size_t n = rx_bits_.size();
        if (n < 16) return false;
        std::vector<bool> cuerpo(rx_bits_.begin(), rx_bits_.end() - 15);
        uint16_t crc_rx = 0;
        for (size_t i = n - 15; i < n; ++i)
            crc_rx = uint16_t((crc_rx << 1) | (rx_bits_[i] ? 1u : 0u));
        return can_crc15(cuerpo) == crc_rx;
    }

    void entregar() {
        if (!ack_pendiente_) { error(LEC_CRC, false); return; }

        // Se arma el marco a partir de los bits.
        CanFrame f;
        f.ide = rx_bits_[13];
        unsigned p = 1;
        uint32_t base = 0;
        for (unsigned i = 0; i < 11; ++i) base = (base << 1) | (rx_bits_[p++] ? 1u : 0u);
        if (!f.ide) {
            f.rtr = rx_bits_[12];
            f.id = base;
            p = 15;                              // tras IDE y r0
        } else {
            p = 14;                              // tras SRR e IDE
            uint32_t ext = 0;
            for (unsigned i = 0; i < 18; ++i) ext = (ext << 1) | (rx_bits_[p++] ? 1u : 0u);
            f.id = (base << 18) | ext;
            f.rtr = rx_bits_[p];
            p += 3;                              // RTR, r1, r0
        }
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (rx_bits_[p++] ? 1u : 0u);
        f.dlc = uint8_t(dlc > 8 ? 8 : dlc);
        if (!f.rtr)
            for (unsigned k = 0; k < f.dlc; ++k) {
                uint32_t byte = 0;
                for (unsigned i = 0; i < 8; ++i) byte = (byte << 1) | (rx_bits_[p++] ? 1u : 0u);
                f.data[k] = uint8_t(byte);
            }

        if (rec_ > 0) --rec_;
        lec_ = LEC_NONE;
        // Un nodo NO se recibe a si mismo. La excepcion es el bucle cerrado,
        // que existe justamente para que el bloque se oiga solo [IR, 12.12].
        if (soy_emisor_ && !cm_bucle()) { cm_eco(); return; }
        cm_rx_ok(f);
    }

    // Señalización de error: seis bits dominantes seguidos, que es lo que
    // destruye el marco para todos y obliga a repetirlo.
    void error(uint32_t codigo, bool del_emisor) {
        lec_ = codigo;
        if (del_emisor) {
            tec_ += 8;
            const unsigned i = unsigned(tx_activo_ >= 0 ? tx_activo_ : 0);
            cm_tx_error(i);
            tx_activo_ = -1;
        } else {
            rec_ += 8;
        }
        if (tec_ > 255) tec_ = 255;
        if (rec_ > 255) rec_ = 255;
        if (tec_ >= 255) entrar_bus_off();
        estado_ = ERROR_FLAG;
        err_i_ = 0; recesivos_ = 0;
        cm_error();
        publish();
    }

    void entrar_bus_off() {
        boff_ = true;
        cm_bus_off();
    }

};

} // namespace stm32
#endif // STM32_PERIPH_CAN_MOTOR_H
