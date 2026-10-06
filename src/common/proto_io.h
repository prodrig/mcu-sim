// =============================================================================
// proto_io.h — El marco del protocolo entre mcu-sim y mcu-sim-gui: cómo se
// escribe y cómo se lee
//
// ESTE FICHERO VIVE EN DOS REPOSITORIOS Y TIENE QUE SER EL MISMO EN LOS DOS,
// igual que `protocolo.h`, del que depende. La copia de referencia es la de
// `mcu-sim-gui/src/proto_io.h`; la de `mcu-sim/src/common/proto_io.h` es una
// copia vendida, y `make gui-proto` falla si dejan de coincidir byte a byte.
// Así los dos extremos no solo comparten la definición del marco: comparten
// el CÓDIGO que lo lee, y un error de lectura no puede estar en uno solo.
//
// QUÉ HACE: convierte mensajes en bytes y bytes en mensajes. Nada más. No sabe
// de sockets —quien lo usa le mete lo que haya recibido, venga de `recv()` o
// de un `QTcpSocket`— ni de lo que significa un mensaje: la fase 2 del plan
// mueve bytes, y el significado es de la 3 en adelante.
//
// QUÉ COMPRUEBA al leer, que es lo que `doc/protocolo.md` §2 manda comprobar:
//
//   * la MAGIA. Otra cosa es que alguien apuntó otro programa al puerto, y no
//     se intenta resincronizar: se para y se dice;
//   * la VERSIÓN. Antes de negociarla vale la 1, que es la del saludo (véase
//     abajo); después, la negociada. Otra, se para;
//   * la LONGITUD, con el techo de CUERPO_MAX, y se comprueba con la cabecera
//     sola: una longitud de 4 GB se rechaza sin esperar a que lleguen 4 GB;
//   * el SENTIDO. Un tipo del rango propio quiere decir que hay dos programas
//     del mismo lado hablándose, y eso es un error de conexión.
//
// Un tipo DESCONOCIDO no es un error: se entrega igual, con su cuerpo, y
// `es_conocido()` dice que no lo es. Saltarlo es cosa de quien lee, y es lo que
// hace posible añadir mensajes sin subir la versión.
//
// LA VERSIÓN ANTES DEL SALUDO. Mientras no se ha negociado, los mensajes van y
// se aceptan con versión 1, que es el marco en el que viajan T_HOLA y
// T_VERSION. Así un extremo nuevo puede ofrecer `protocolo_max=2` a uno viejo
// sin que el viejo rechace el mensaje que lo ofrece por traer un 2 en la
// cabecera. Negociada, cada extremo llama a `fija_version()`.
//
// MEMORIA. Ni el emisor ni el lector reservan nada por mensaje: los dos
// trabajan sobre un búfer que crece hasta el mensaje más grande que hayan visto
// y luego se reutiliza. Un cuerpo leído es un puntero DENTRO de ese búfer, y
// vale hasta la siguiente llamada a `mete()`; quien quiera guardarlo, que lo
// copie.
//
// Little-endian, como dice `protocolo.h`: la cabecera se copia tal cual, y en
// un extremo big-endian esto habría que arreglarlo aquí y solo aquí.
// =============================================================================
#ifndef MCU_SIM_PROTO_IO_H
#define MCU_SIM_PROTO_IO_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "protocolo.h"

namespace mcusim {
namespace proto {

// La versión en la que viaja el saludo, antes de negociar ninguna.
inline constexpr uint16_t VERSION_SALUDO = 1;

// ¿Es un tipo que esta versión del protocolo sabe qué significa?
inline bool es_conocido(uint16_t t) {
    switch (t) {
        case T_HOLA: case T_PLACA: case T_CATALOGO: case T_LISTO: case T_ILUSTRACION:
        case T_INSTANTANEA: case T_AVISO: case T_ESTADO: case T_ORDEN_HECHA:
        case T_PONG: case T_FIN:
        case T_VERSION: case T_SUSCRIBE: case T_ARRANCA: case T_PAUSA:
        case T_SIGUE: case T_PASO: case T_ORDENES: case T_PARA: case T_PING:
            return true;
        default:
            return false;
    }
}

// Quién manda lo que se va a LEER. El modelo lee lo que manda la pantalla, y
// la pantalla lo que manda el modelo.
enum class Origen { Modelo, Pantalla };

// ---------------------------------------------------------------------------
// Escribir
// ---------------------------------------------------------------------------
class Emisor {
public:
    // Añade un mensaje entero -cabecera y cuerpo- al final de `sal`. Devuelve
    // false, sin añadir nada, si el cuerpo pasa de CUERPO_MAX: un mensaje así
    // el otro extremo lo rechazaría y cerraría, y es mejor saberlo aquí.
    bool mensaje(std::string& sal, uint16_t tipo, const void* cuerpo, std::size_t n) {
        if (n > CUERPO_MAX) return false;
        Cabecera c{};
        c.magia     = MAGIA;
        c.version   = version_;
        c.tipo      = tipo;
        c.longitud  = uint32_t(n);
        c.secuencia = secuencia_++;
        const std::size_t ini = sal.size();
        sal.resize(ini + sizeof c + n);
        std::memcpy(&sal[ini], &c, sizeof c);
        if (n) std::memcpy(&sal[ini + sizeof c], cuerpo, n);
        return true;
    }
    bool mensaje(std::string& sal, uint16_t tipo, const std::string& cuerpo) {
        return mensaje(sal, tipo, cuerpo.data(), cuerpo.size());
    }
    bool vacio(std::string& sal, uint16_t tipo) { return mensaje(sal, tipo, nullptr, 0); }
    // Un cuerpo que es una struct POD de `protocolo.h`.
    template <class T>
    bool pod(std::string& sal, uint16_t tipo, const T& t) {
        return mensaje(sal, tipo, &t, sizeof t);
    }

    void     fija_version(uint16_t v) { version_ = v; }
    uint16_t version() const { return version_; }
    uint32_t secuencia() const { return secuencia_; }

private:
    uint16_t version_   = VERSION_SALUDO;
    uint32_t secuencia_ = 0;
};

// ---------------------------------------------------------------------------
// Leer
// ---------------------------------------------------------------------------
struct Mensaje {
    uint16_t       tipo      = 0;
    uint16_t       version   = 0;
    uint32_t       secuencia = 0;
    uint32_t       longitud  = 0;
    const uint8_t* cuerpo    = nullptr;   // vale hasta la siguiente mete()

    std::string texto() const {           // para los cuerpos que son texto
        return std::string(reinterpret_cast<const char*>(cuerpo), longitud);
    }
    // Copia un cuerpo POD. false si la longitud no es la de la struct.
    template <class T>
    bool como(T& t) const {
        if (longitud != sizeof t) return false;
        std::memcpy(&t, cuerpo, sizeof t);
        return true;
    }
};

class Lector {
public:
    // Con prefijo, y feo a propósito: `ERROR` a secas es una macro de
    // <windows.h> (wingdi.h la define a 0), y un `namespace` no protege de
    // una macro. Es la regla que cuenta `protocolo.h`.
    enum Estado { LEC_FALTA, LEC_MENSAJE, LEC_ERROR };

    explicit Lector(Origen de) : de_(de) {}

    // Lo que haya llegado, tal cual, en los trozos que sea.
    void mete(const void* datos, std::size_t n) {
        if (roto_ || n == 0) return;
        if (ini_ > 0) {                    // compactar: lo ya leído sobra
            buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(ini_));
            ini_ = 0;
        }
        const uint8_t* p = static_cast<const uint8_t*>(datos);
        buf_.insert(buf_.end(), p, p + n);
    }

    // El siguiente mensaje completo, si lo hay. Después de un LEC_ERROR el lector
    // se queda roto y no entrega nada más: el protocolo dice que se cierra.
    Estado saca(Mensaje& m) {
        if (roto_) return LEC_ERROR;
        const std::size_t hay = buf_.size() - ini_;
        if (hay < sizeof(Cabecera)) return LEC_FALTA;
        Cabecera c;
        std::memcpy(&c, &buf_[ini_], sizeof c);
        if (c.magia != MAGIA)
            return rompe("magia 0x" + hex(c.magia) + ": esto no es mcu-sim ni mcu-sim-gui");
        if (c.version != version_)
            return rompe("version " + std::to_string(c.version) + " en un mensaje; en esta "
                         "conexion va la " + std::to_string(version_));
        if (c.longitud > CUERPO_MAX)
            return rompe("un cuerpo de " + std::to_string(c.longitud) + " bytes pasa del "
                         "techo de " + std::to_string(CUERPO_MAX));
        const bool del_modelo = es_del_modelo(c.tipo);
        if (del_modelo != (de_ == Origen::Modelo))
            return rompe("el tipo 0x" + hex(c.tipo) + " es del otro sentido: hay dos " +
                         (del_modelo ? "modelos" : "pantallas") + " hablandose");
        if (hay < sizeof c + c.longitud) return LEC_FALTA;

        m.tipo      = c.tipo;
        m.version   = c.version;
        m.secuencia = c.secuencia;
        m.longitud  = c.longitud;
        m.cuerpo    = buf_.data() + ini_ + sizeof c;
        ini_       += sizeof c + c.longitud;
        if (c.secuencia != sec_esperada_) ++saltos_;
        sec_esperada_ = c.secuencia + 1;
        ++leidos_;
        return LEC_MENSAJE;
    }

    void fija_version(uint16_t v) { version_ = v; }
    uint16_t version() const { return version_; }

    bool               roto() const { return roto_; }
    const std::string& error() const { return error_; }
    std::size_t        pendientes() const { return buf_.size() - ini_; }
    uint64_t           leidos() const { return leidos_; }
    // Mensajes cuya `secuencia` no era la siguiente a la del anterior. No se
    // usa para nada en marcha: es lo que contesta «¿se perdió algo?» cuando
    // dos trazas no cuadran.
    uint64_t           saltos_de_secuencia() const { return saltos_; }

private:
    Estado rompe(std::string e) {
        roto_  = true;
        error_ = std::move(e);
        return LEC_ERROR;
    }
    static std::string hex(uint32_t v) {
        static const char d[] = "0123456789ABCDEF";
        std::string s;
        for (int i = 28; i >= 0; i -= 4) s += d[(v >> i) & 0xF];
        while (s.size() > 4 && s[0] == '0') s.erase(0, 1);
        return s;
    }

    Origen               de_;
    uint16_t             version_ = VERSION_SALUDO;
    std::vector<uint8_t> buf_;
    std::size_t          ini_ = 0;
    bool                 roto_ = false;
    std::string          error_;
    uint32_t             sec_esperada_ = 0;
    uint64_t             saltos_ = 0, leidos_ = 0;
};

} // namespace proto
} // namespace mcusim

#endif // MCU_SIM_PROTO_IO_H
