// =============================================================================
// telnet2217.h — Telnet y la opción 44 (RFC 2217), sin sockets y sin SystemC
//
// Fase D4 del plan de `doc/analisis_puente_serie.md` §10 (P-14). Es la parte
// del puente que habla el protocolo, y es PURA: se le dan bytes y devuelve
// bytes y sucesos. No abre un socket, no sabe qué es un MCU y no avanza un
// reloj. La usará el canal RFC 2217 de la fase D5; aquí se prueba sola con
// `make rfc2217`, contra el texto de las RFC y contra una sesión de verdad
// grabada entre el cliente y el servidor de pySerial
// (verif/vectores/rfc2217_pyserial.vec).
//
// TRES PIEZAS:
//
//   Decodificador  parte el flujo que llega del cliente en DATOS (lo que va a
//                  la línea serie) y SUCESOS (negociaciones, subopciones y
//                  órdenes sueltas). Aguanta que una orden llegue partida en
//                  cualquier byte: el estado vive entre llamadas.
//   Codificador    las funciones que componen lo que se manda: datos con el
//                  IAC doblado, negociaciones y subopciones.
//   Negociador     la política del SERVIDOR (el «access server» de RFC 2217)
//                  para las opciones: cuáles acepta, cuándo contesta y cuándo
//                  calla para no entrar en un bucle (RFC 1143).
//
// LO QUE HAY QUE SABER DE TELNET PARA LEER ESTO (RFC 854, 855, 856):
//
//   * 0xFF (IAC) abre una orden. Un 0xFF de DATOS va doblado: IAC IAC.
//   * WILL/WONT/DO/DONT <opción> negocian. WILL = «lo hago yo»; DO = «hazlo
//     tú». Cada lado lleva la cuenta de las suyas.
//   * IAC SB <opción> ... IAC SE es una subnegociación. Dentro, un 0xFF
//     también va doblado: un valor de baudios puede contener un 0xFF.
//   * Sin la opción BINARY (0), la línea es un «terminal virtual de red» (NVT):
//     un retorno de carro se manda como CR NUL (o CR LF), y quien recibe tiene
//     que quitar ese NUL. Con BINARY, los bytes van tal cual. Un puente serie
//     quiere BINARY, porque un firmware puede mandar un 0x00 detrás de un 0x0D.
//
// LA POLÍTICA DEL SERVIDOR (decisiones D-2 y D-8, y una nueva de esta fase):
//
//   * PASIVO HASTA QUE EL CLIENTE HABLE TELNET. No manda nada al conectarse:
//     un cliente en crudo que se equivoque de puerto no verá basura.
//   * En cuanto el cliente negocia algo -y con eso demuestra que habla
//     Telnet-, el servidor PIDE BINARY en los dos sentidos, una sola vez. La
//     captura de pySerial enseña por qué hace falta: su cliente no ofrece
//     BINARY por su cuenta, lo acepta cuando se lo piden. Sin pedirlo, la
//     sesión se quedaría en NVT y un CR seguido de un NUL perdería el NUL.
//   * Acepta que el cliente haga BINARY, SGA y COM-PORT (le contesta DO), y
//     hace él BINARY y SGA (contesta WILL). Todo lo demás se rechaza, ECHO
//     incluido: el eco, si lo hay, lo hace el firmware, como en la placa.
//   * No contesta a lo que no cambia nada (RFC 1143): un WILL repetido sobre
//     una opción ya activa no tiene respuesta. Así no hay bucles.
// =============================================================================
#ifndef STM32_COMMON_TELNET2217_H
#define STM32_COMMON_TELNET2217_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stm32 {
namespace telnet {

// --- Órdenes de Telnet [RFC 854] ------------------------------------------
enum : uint8_t {
    SE = 240, NOP = 241, DM = 242, BRK = 243, IP = 244, AO = 245, AYT = 246,
    EC = 247, EL = 248, GA = 249, SB = 250,
    WILL = 251, WONT = 252, DO = 253, DONT = 254, IAC = 255
};

// --- Opciones que importan aquí ---------------------------------------------
enum : uint8_t {
    OPT_BINARY   = 0,     // RFC 856
    OPT_ECHO     = 1,     // RFC 857
    OPT_SGA      = 3,     // RFC 858, suprimir GO AHEAD
    OPT_COM_PORT = 44     // RFC 2217
};

// --- Las órdenes de RFC 2217 (las del cliente; el servidor suma 100) --------
namespace cpo {
enum : uint8_t {
    SIGNATURE = 0, SET_BAUDRATE = 1, SET_DATASIZE = 2, SET_PARITY = 3,
    SET_STOPSIZE = 4, SET_CONTROL = 5, NOTIFY_LINESTATE = 6,
    NOTIFY_MODEMSTATE = 7, FLOWCONTROL_SUSPEND = 8, FLOWCONTROL_RESUME = 9,
    SET_LINESTATE_MASK = 10, SET_MODEMSTATE_MASK = 11, PURGE_DATA = 12
};
inline constexpr uint8_t RESPUESTA = 100;   // lo que suma el servidor
} // namespace cpo

// Un suceso del flujo de entrada.
struct Suceso {
    enum class Tipo : uint8_t {
        negociacion,    // IAC WILL/WONT/DO/DONT <opcion>
        subopcion,      // IAC SB <opcion> <datos> IAC SE
        orden           // IAC <orden suelta>: NOP, BRK, AYT, GA...
    };
    Tipo                 tipo   = Tipo::orden;
    uint8_t              verbo  = 0;     // WILL..DONT, o la orden suelta
    uint8_t              opcion = 0;
    std::vector<uint8_t> datos;          // de la subopción, sin escapes

    bool operator==(const Suceso& o) const {
        return tipo == o.tipo && verbo == o.verbo && opcion == o.opcion &&
               datos == o.datos;
    }
};

// ---------------------------------------------------------------------------
// El decodificador
// ---------------------------------------------------------------------------
class Decodificador {
public:
    // Una subopción más larga que esto se corta y se cuenta. RFC 2217 no pasa
    // de la firma, y un cliente que mande kilobytes dentro de un SB está roto
    // o buscando algo: no hay que guardarle la memoria.
    static constexpr std::size_t MAX_SB = 256;

    // ¿El cliente manda en BINARY? Lo decide el Negociador; hasta entonces,
    // NVT, y un CR NUL de entrada es un CR.
    void set_binario(bool b) { bin_ = b; }
    bool binario() const { return bin_; }

    void alimenta(const uint8_t* b, std::size_t n, std::vector<uint8_t>& datos,
                  std::vector<Suceso>& sucesos) {
        for (std::size_t i = 0; i < n; ++i) byte(b[i], datos, sucesos);
    }
    void alimenta(const std::string& s, std::vector<uint8_t>& datos,
                  std::vector<Suceso>& sucesos) {
        alimenta(reinterpret_cast<const uint8_t*>(s.data()), s.size(), datos, sucesos);
    }

    uint64_t subopciones_cortadas() const { return n_cortadas_; }
    uint64_t subopciones_mal_cerradas() const { return n_mal_cerradas_; }

private:
    enum class E : uint8_t { datos, cr, iac, verbo, sb_opcion, sb_datos, sb_iac };

    void byte(uint8_t c, std::vector<uint8_t>& datos, std::vector<Suceso>& ev) {
        switch (e_) {
        case E::cr:
            // NVT: detrás de un CR puede venir el NUL de relleno, que no es un
            // dato. Cualquier otra cosa se trata como si el CR no estuviera.
            e_ = E::datos;
            if (c == 0) return;
            [[fallthrough]];
        case E::datos:
            if (c == IAC) { e_ = E::iac; return; }
            datos.push_back(c);
            if (c == '\r' && !bin_) e_ = E::cr;
            return;
        case E::iac:
            if (c == IAC) { datos.push_back(IAC); e_ = E::datos; return; }
            if (c >= WILL) { verbo_ = c; e_ = E::verbo; return; }
            if (c == SB) { e_ = E::sb_opcion; return; }
            if (c == SE) { e_ = E::datos; return; }     // SE suelto: se ignora
            {
                Suceso s;                                // NOP, BRK, AYT, GA...
                s.tipo = Suceso::Tipo::orden;
                s.verbo = c;
                ev.push_back(s);
            }
            e_ = E::datos;
            return;
        case E::verbo: {
            Suceso s;
            s.tipo = Suceso::Tipo::negociacion;
            s.verbo = verbo_;
            s.opcion = c;
            ev.push_back(s);
            e_ = E::datos;
            return;
        }
        case E::sb_opcion:
            sb_ = Suceso{};
            sb_.tipo = Suceso::Tipo::subopcion;
            sb_.opcion = c;
            cortada_ = false;
            e_ = E::sb_datos;
            return;
        case E::sb_datos:
            if (c == IAC) { e_ = E::sb_iac; return; }
            guarda_sb(c);
            return;
        case E::sb_iac:
            if (c == IAC) { guarda_sb(IAC); e_ = E::sb_datos; return; }
            if (c == SE) { ev.push_back(sb_); e_ = E::datos; return; }
            // IAC seguido de otra cosa dentro de un SB: el cliente se ha dejado
            // el SE. Se da la subopción por cerrada y ese byte se procesa como
            // lo que es, una orden. Tirar la subopción entera sería perder un
            // SET-BAUDRATE por un cliente descuidado.
            ++n_mal_cerradas_;
            ev.push_back(sb_);
            e_ = E::iac;
            byte(c, datos, ev);
            return;
        }
    }

    void guarda_sb(uint8_t c) {
        if (sb_.datos.size() < MAX_SB) { sb_.datos.push_back(c); return; }
        if (!cortada_) { cortada_ = true; ++n_cortadas_; }
    }

    E        e_ = E::datos;
    bool     bin_ = false;
    uint8_t  verbo_ = 0;
    Suceso   sb_;
    bool     cortada_ = false;
    uint64_t n_cortadas_ = 0, n_mal_cerradas_ = 0;
};

// ---------------------------------------------------------------------------
// El codificador
// ---------------------------------------------------------------------------

// Datos hacia el cliente. El IAC siempre se dobla. Sin BINARY, además, un CR
// va seguido de NUL: es lo que manda el NVT, y el que recibe lo quita. Un CR LF
// sale como CR NUL LF, que el otro lado lee como CR LF.
inline void escapa(const uint8_t* b, std::size_t n, bool binario, std::string& out) {
    for (std::size_t i = 0; i < n; ++i) {
        out += char(b[i]);
        if (b[i] == IAC) out += char(IAC);
        else if (b[i] == '\r' && !binario) out += '\0';
    }
}
inline std::string escapa(const std::string& s, bool binario) {
    std::string r;
    escapa(reinterpret_cast<const uint8_t*>(s.data()), s.size(), binario, r);
    return r;
}

inline std::string negociacion(uint8_t verbo, uint8_t opcion) {
    return std::string{char(IAC), char(verbo), char(opcion)};
}

// IAC SB <opcion> <datos con el IAC doblado> IAC SE
inline std::string subopcion(uint8_t opcion, const std::vector<uint8_t>& datos) {
    std::string r{char(IAC), char(SB), char(opcion)};
    for (uint8_t c : datos) { r += char(c); if (c == IAC) r += char(IAC); }
    r += char(IAC);
    r += char(SE);
    return r;
}

// Una orden de RFC 2217 ya compuesta: `cod` es el código tal cual va en el
// cable (1..12 del cliente, 101..112 o 100 del servidor).
inline std::string orden_2217(uint8_t cod, const std::vector<uint8_t>& valor) {
    std::vector<uint8_t> d;
    d.reserve(valor.size() + 1);
    d.push_back(cod);
    d.insert(d.end(), valor.begin(), valor.end());
    return subopcion(OPT_COM_PORT, d);
}

// Los baudios van en cuatro bytes, en orden de red [RFC 2217, SET-BAUDRATE].
inline std::vector<uint8_t> u32_red(uint32_t v) {
    return { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
}
inline bool lee_u32_red(const std::vector<uint8_t>& d, std::size_t desde, uint32_t& v) {
    if (d.size() < desde + 4) return false;
    v = (uint32_t(d[desde]) << 24) | (uint32_t(d[desde + 1]) << 16) |
        (uint32_t(d[desde + 2]) << 8) | uint32_t(d[desde + 3]);
    return true;
}

// Una subopción COM-PORT desmontada: el código y lo que viene detrás.
struct OrdenCpo {
    uint8_t              cod = 0;
    std::vector<uint8_t> valor;
};
inline bool es_cpo(const Suceso& s, OrdenCpo& o) {
    if (s.tipo != Suceso::Tipo::subopcion || s.opcion != OPT_COM_PORT ||
        s.datos.empty()) return false;
    o.cod = s.datos[0];
    o.valor.assign(s.datos.begin() + 1, s.datos.end());
    return true;
}

// ---------------------------------------------------------------------------
// El negociador del servidor
// ---------------------------------------------------------------------------
class Negociador {
public:
    // Lo que el servidor acepta hacer él (contesta WILL a un DO)...
    static bool hacemos(uint8_t op) { return op == OPT_BINARY || op == OPT_SGA; }
    // ...y lo que acepta que haga el cliente (contesta DO a un WILL).
    static bool aceptamos(uint8_t op) {
        return op == OPT_BINARY || op == OPT_SGA || op == OPT_COM_PORT;
    }

    // Una negociación que llega. Devuelve lo que hay que contestar, que puede
    // ser nada. La primera vez que el cliente negocia algo, añade además la
    // petición de BINARY en los dos sentidos (véase la cabecera).
    std::string recibe(uint8_t verbo, uint8_t op) {
        std::string r;
        switch (verbo) {
        case WILL:
            if (pido_do_[op]) { pido_do_[op] = false; ellos_[op] = true; break; }
            if (ellos_[op]) break;                        // ya estaba: silencio
            if (aceptamos(op)) { ellos_[op] = true; r = negociacion(DO, op); }
            else r = negociacion(DONT, op);
            break;
        case WONT:
            if (pido_do_[op]) { pido_do_[op] = false; ellos_[op] = false; break; }
            if (!ellos_[op]) break;
            ellos_[op] = false;
            r = negociacion(DONT, op);
            break;
        case DO:
            if (pido_will_[op]) { pido_will_[op] = false; nosotros_[op] = true; break; }
            if (nosotros_[op]) break;
            if (hacemos(op)) { nosotros_[op] = true; r = negociacion(WILL, op); }
            else r = negociacion(WONT, op);
            break;
        case DONT:
            if (pido_will_[op]) { pido_will_[op] = false; nosotros_[op] = false; break; }
            if (!nosotros_[op]) break;
            nosotros_[op] = false;
            r = negociacion(WONT, op);
            break;
        default:
            return r;
        }
        if (!despierto_) {
            despierto_ = true;
            if (!ellos_[OPT_BINARY] && !pido_do_[OPT_BINARY]) {
                pido_do_[OPT_BINARY] = true;
                r += negociacion(DO, OPT_BINARY);
            }
            if (!nosotros_[OPT_BINARY] && !pido_will_[OPT_BINARY]) {
                pido_will_[OPT_BINARY] = true;
                r += negociacion(WILL, OPT_BINARY);
            }
        }
        return r;
    }

    // ¿El cliente habla RFC 2217? Es lo que decide si las subopciones 44 se
    // atienden: RFC 2217 exige que la opción esté negociada antes.
    bool com_port()        const { return ellos_[OPT_COMPORT_IDX]; }
    bool binario_entrada() const { return ellos_[OPT_BINARY]; }   // el cliente manda binario
    bool binario_salida()  const { return nosotros_[OPT_BINARY]; } // el servidor manda binario
    bool despierto()       const { return despierto_; }
    bool ellos(uint8_t op)    const { return ellos_[op]; }
    bool nosotros(uint8_t op) const { return nosotros_[op]; }

private:
    static constexpr uint8_t OPT_COMPORT_IDX = OPT_COM_PORT;
    bool ellos_[256]     = {};    // opciones que el cliente hace
    bool nosotros_[256]  = {};    // opciones que el servidor hace
    bool pido_do_[256]   = {};    // el servidor ha pedido DO y espera respuesta
    bool pido_will_[256] = {};    // el servidor ha ofrecido WILL y espera respuesta
    bool despierto_ = false;      // el cliente ya ha negociado algo
};

} // namespace telnet
} // namespace stm32

#endif // STM32_COMMON_TELNET2217_H
