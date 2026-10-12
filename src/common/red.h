// =============================================================================
// red.h — La ÚNICA parte del modelo que sabe en qué sistema operativo corre
//
// Todo lo demás de `src/` es C++17 y `<systemc>`: se compila igual en Linux, en
// Windows y en macOS sin una sola directiva de preprocesador. Lo que no es
// portable son los sockets TCP, y los sockets están en pocos sitios del
// modelo: los dos servidores de GDB (`common/gdb_rsp.h`) y el cliente de RSP
// con el que la suite se prueba a sí misma (`verif/gdb_client.h`), el puente
// serie (`parts/canal_host.h`) y el transporte con `mcu-sim-gui`.
//
// Este fichero recoge esa diferencia entera para que aquellos dos no tengan que
// enterarse. Berkeley y Winsock se parecen lo bastante como para que la
// traducción quepa aquí, pero no tanto como para poder ignorarla:
//
//   * un descriptor es `int` y el inválido es -1 en POSIX; en Windows es un
//     `SOCKET` SIN SIGNO y el inválido es INVALID_SOCKET, así que `if (s < 0)`
//     no es que falle: es que **siempre es falso**, y el error se traga en
//     silencio. Por eso aquí hay un `valido()` y no una comparación;
//   * se cierra con `close()` o con `closesocket()`;
//   * se pone en no bloqueante con `fcntl(O_NONBLOCK)` o con `ioctlsocket()`;
//   * el error del último intento está en `errno` o en `WSAGetLastError()`, y
//     el «no hay datos ahora mismo» se llama EAGAIN/EWOULDBLOCK o WSAEWOULDBLOCK;
//   * `send`/`recv` toman `void*` y `size_t` o `char*` e `int`;
//   * y Winsock **hay que arrancarlo** con WSAStartup antes de tocar nada.
//
// Y una tercera diferencia que no es de Windows sino de macOS: `MSG_NOSIGNAL`
// no existe allí. Escribir en un socket que el otro extremo ha cerrado manda un
// SIGPIPE que, sin manejar, **mata el proceso**. En macOS eso se evita con la
// opción de socket SO_NOSIGPIPE, que hay que poner al crearlo. Un simulador que
// se muere porque el IDE del alumno se cerró de golpe no es aceptable, así que
// esto no es un detalle cosmético.
//
// El resto del proyecto no incluye este fichero ni lo necesita.
// =============================================================================
#ifndef STM32_COMMON_RED_H
#define STM32_COMMON_RED_H

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX                    // o Windows define min/max como macros
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>

namespace stm32 {
namespace red {

// ---------------------------------------------------------------------------
// El descriptor, y cómo se pregunta si vale
// ---------------------------------------------------------------------------
#if defined(_WIN32)
using socket_t = SOCKET;
inline socket_t invalido() { return INVALID_SOCKET; }
inline bool     valido(socket_t s) { return s != INVALID_SOCKET; }
#else
using socket_t = int;
inline socket_t invalido() { return -1; }
inline bool     valido(socket_t s) { return s >= 0; }
#endif

// ---------------------------------------------------------------------------
// Arranque de la pila. En POSIX no hace nada; en Windows, WSAStartup una vez y
// WSACleanup al terminar el proceso. El objeto estático de función es
// deliberado: se inicializa la primera vez que alguien abre un socket, no antes,
// y se limpia solo. Llamarla de más es gratis.
// ---------------------------------------------------------------------------
inline bool arranca() {
#if defined(_WIN32)
    struct Winsock {
        bool ok = false;
        Winsock()  { WSADATA d; ok = (::WSAStartup(MAKEWORD(2, 2), &d) == 0); }
        ~Winsock() { if (ok) ::WSACleanup(); }
    };
    static Winsock w;
    return w.ok;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Las operaciones, con el mismo nombre en las tres plataformas
// ---------------------------------------------------------------------------
inline void cerrar(socket_t& s) {
    if (!valido(s)) return;
#if defined(_WIN32)
    ::closesocket(s);
#else
    ::close(s);
#endif
    s = invalido();
}

inline bool no_bloqueante(socket_t s) {
#if defined(_WIN32)
    u_long m = 1;
    return ::ioctlsocket(s, FIONBIO, &m) == 0;
#else
    const int f = ::fcntl(s, F_GETFL, 0);
    return f >= 0 && ::fcntl(s, F_SETFL, f | O_NONBLOCK) == 0;
#endif
}

// ¿El último error solo quiere decir «ahora mismo no hay nada»? Si es que sí,
// no es un error: es un socket no bloqueante haciendo su trabajo.
inline bool reintentar() {
#if defined(_WIN32)
    const int e = ::WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

// Sin el algoritmo de Nagle: el RSP son paquetes cortos y con respuesta, que es
// justo el caso que Nagle empeora.
inline void sin_nagle(socket_t s) {
#if defined(_WIN32)
    const char uno = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno);
#else
    const int uno = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno);
#endif
}

// Reutilizar la dirección al volver a escuchar. Solo en POSIX: ahí evita el
// TIME_WAIT que impide relanzar el simulador enseguida, que es lo que uno hace
// veinte veces por sesión. En Windows SO_REUSEADDR significa otra cosa —permite
// que otro proceso se apodere del puerto— y no hace falta, porque allí el
// escuchador no arrastra TIME_WAIT.
inline void reusar_direccion(socket_t s) {
#if !defined(_WIN32)
    const int uno = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
#else
    (void)s;
#endif
}

// Que escribir en un socket cerrado por el otro lado NO mate el proceso.
// Windows no manda SIGPIPE; Linux lo evita con MSG_NOSIGNAL en cada envío;
// macOS no tiene MSG_NOSIGNAL y lo resuelve con esta opción de socket.
inline void sin_sigpipe(socket_t s) {
#if defined(SO_NOSIGPIPE)
    const int uno = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &uno, sizeof uno);
#else
    (void)s;
#endif
}

#if defined(MSG_NOSIGNAL)
inline constexpr int BANDERAS_ENVIO = MSG_NOSIGNAL;
#else
inline constexpr int BANDERAS_ENVIO = 0;      // Windows y macOS: véase arriba
#endif

// Devuelven >0 bytes, 0 si el otro extremo cerró, <0 si error (y entonces
// `reintentar()` dice si el error era de verdad).
inline long enviar(socket_t s, const char* b, std::size_t n) {
#if defined(_WIN32)
    return ::send(s, b, int(n), BANDERAS_ENVIO);
#else
    return long(::send(s, b, n, BANDERAS_ENVIO));
#endif
}

inline long recibir(socket_t s, char* b, std::size_t n) {
#if defined(_WIN32)
    return ::recv(s, b, int(n), 0);
#else
    return long(::recv(s, b, n, 0));
#endif
}

// ---------------------------------------------------------------------------
// Los dos montajes completos, que son los únicos que el proyecto hace: escuchar
// en un puerto de la interfaz de bucle local, y conectarse a uno.
//
// Solo en `localhost` a propósito: esto es un depurador con acceso total a la
// memoria del objetivo, y no tiene por qué ser accesible desde la red.
// ---------------------------------------------------------------------------
inline socket_t escucha_local(unsigned puerto) {
    if (!arranca()) return invalido();
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!valido(s)) return invalido();
    reusar_direccion(s);
    sin_sigpipe(s);
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(uint16_t(puerto));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, (sockaddr*)&a, sizeof a) != 0) { cerrar(s); return invalido(); }
    if (::listen(s, 1) != 0)                     { cerrar(s); return invalido(); }
    if (!no_bloqueante(s))                       { cerrar(s); return invalido(); }
    return s;
}

inline socket_t acepta(socket_t servidor) {
    socket_t c = ::accept(servidor, nullptr, nullptr);
    if (!valido(c)) return invalido();
    no_bloqueante(c);
    sin_nagle(c);
    sin_sigpipe(c);
    return c;
}

inline socket_t conecta_local(unsigned puerto) {
    if (!arranca()) return invalido();
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!valido(s)) return invalido();
    sin_sigpipe(s);
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(uint16_t(puerto));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, (sockaddr*)&a, sizeof a) != 0) { cerrar(s); return invalido(); }
    if (!no_bloqueante(s))                          { cerrar(s); return invalido(); }
    sin_nagle(s);
    return s;
}

// ---------------------------------------------------------------------------
// Y un tercer montaje, para el modo cliente del puente serie (P-14, fase D8):
// conectarse a un HOST cualquiera SIN BLOQUEAR. `conecta_local` bloquea en el
// `connect`, y contra 127.0.0.1 eso es instantaneo -se acepta o se rechaza en
// el acto-; contra otra maquina puede tardar lo que tarde el TCP en rendirse,
// con la simulacion entera parada. Aqui se empieza la conexion y se pregunta
// despues si ha terminado, en cada sondeo.
//
// Solo IPv4, como el resto del fichero.
// ---------------------------------------------------------------------------
inline bool direccion_ipv4(const char* host, unsigned puerto, sockaddr_in& a) {
    if (!arranca()) return false;
    addrinfo pista{};
    pista.ai_family   = AF_INET;
    pista.ai_socktype = SOCK_STREAM;
    addrinfo* r = nullptr;
    if (::getaddrinfo(host, nullptr, &pista, &r) != 0 || !r) return false;
    a = *reinterpret_cast<const sockaddr_in*>(r->ai_addr);
    a.sin_port = htons(uint16_t(puerto));
    ::freeaddrinfo(r);
    return true;
}

// Empieza a conectarse. Devuelve el socket, ya no bloqueante, con la conexion
// EN CURSO (o ya hecha), o invalido() si ni siquiera se pudo empezar.
inline socket_t empieza_conexion(const sockaddr_in& a) {
    if (!arranca()) return invalido();
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!valido(s)) return invalido();
    sin_sigpipe(s);
    if (!no_bloqueante(s)) { cerrar(s); return invalido(); }
    if (::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0) return s;
#if defined(_WIN32)
    const bool en_curso = ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
    const bool en_curso = errno == EINPROGRESS || errno == EINTR;
#endif
    if (!en_curso) { cerrar(s); return invalido(); }
    return s;
}

// ¿Ha terminado la conexion que empezo `empieza_conexion`? 1 si esta hecha, 0
// si sigue en curso y -1 si ha fallado (y entonces hay que cerrar el socket).
// Sin esperar: un `select` con tiempo cero. Windows avisa del fallo por el
// conjunto de excepciones, y POSIX marcandolo como escribible con SO_ERROR.
inline int estado_conexion(socket_t s) {
    fd_set esc, exc;
    FD_ZERO(&esc);
    FD_ZERO(&exc);
    FD_SET(s, &esc);
    FD_SET(s, &exc);
    timeval cero{0, 0};
#if defined(_WIN32)
    const int n = ::select(0, nullptr, &esc, &exc, &cero);     // nfds se ignora
#else
    const int n = ::select(s + 1, nullptr, &esc, &exc, &cero);
#endif
    if (n < 0) return -1;
    if (n == 0) return 0;
    if (FD_ISSET(s, &exc)) return -1;
    int err = 0;
    socklen_t l = sizeof err;
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &l) != 0 ||
        err != 0) return -1;
    sin_nagle(s);
    return 1;
}

// ---------------------------------------------------------------------------
// Y los dos montajes CON HOST, para `mcu-sim-gui` (P-12, fase 2 de su plan).
//
// Van AL LADO de `escucha_local` y `conecta_local`, no en su lugar, y es a
// propósito: los servidores de GDB siguen usando las locales, porque un
// depurador con acceso total a la memoria del objetivo no tiene por qué ser
// accesible desde la red. Que la ventana pueda estar en otra máquina es una
// decisión distinta, tomada para la GUI y solo para ella, y tiene que verse en
// el código como una llamada distinta, no heredarse cambiando una función que
// usan los dos. Quien conecta a un host que no es de bucle local además lo
// avisa por la salida de error (`common/gui_destino.h`, `es_bucle_local`).
//
// A diferencia del resto del fichero, estas dos hablan IPv4 E IPv6:
// `--gui [::1]:3344` es una forma que el argumento ya acepta, y el host se
// resuelve con `getaddrinfo` sin fijar la familia. Los corchetes del IPv6 se
// quitan aquí; vienen de la línea de órdenes, no son parte de la dirección.
// ---------------------------------------------------------------------------
namespace detalle {
// "[::1]" -> "::1". Lo demás, tal cual.
inline std::string sin_corchetes(const char* host) {
    std::string h = host ? host : "";
    if (h.size() >= 2 && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
    return h;
}
// Espera, como mucho `ms` milisegundos, a que el socket se pueda escribir o
// tenga una excepción. Lo que hace falta para terminar un `connect` no
// bloqueante con un plazo.
inline int espera_escribible(socket_t s, int ms) {
    fd_set esc, exc;
    FD_ZERO(&esc);
    FD_ZERO(&exc);
    FD_SET(s, &esc);
    FD_SET(s, &exc);
    timeval t{ ms / 1000, (ms % 1000) * 1000 };
#if defined(_WIN32)
    return ::select(0, nullptr, &esc, &exc, &t);
#else
    return ::select(s + 1, nullptr, &esc, &exc, &t);
#endif
}
} // namespace detalle

// Escucha en `host:puerto`. Con el puerto 0 el sistema elige uno libre, y
// `puerto_local()` dice cuál: es lo que permite a las pruebas no pisarse con
// nada que haya en la máquina. El socket sale no bloqueante, como el de
// `escucha_local`, y se acepta con el mismo `acepta()`.
inline socket_t escucha(const char* host, unsigned puerto) {
    if (!arranca()) return invalido();
    const std::string h = detalle::sin_corchetes(host);
    addrinfo pista{};
    pista.ai_family   = AF_UNSPEC;
    pista.ai_socktype = SOCK_STREAM;
    pista.ai_flags    = AI_PASSIVE;
    addrinfo* r = nullptr;
    if (::getaddrinfo(h.c_str(), std::to_string(puerto).c_str(), &pista, &r) != 0 || !r)
        return invalido();
    socket_t s = invalido();
    for (addrinfo* a = r; a && !valido(s); a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (!valido(s)) continue;
        reusar_direccion(s);
        sin_sigpipe(s);
        if (::bind(s, a->ai_addr, socklen_t(a->ai_addrlen)) != 0 ||
            ::listen(s, 1) != 0 || !no_bloqueante(s))
            cerrar(s);
    }
    ::freeaddrinfo(r);
    return s;
}

// El puerto en el que de verdad escucha un socket, o 0 si no se puede saber.
inline unsigned puerto_local(socket_t s) {
    sockaddr_storage a{};
    socklen_t l = sizeof a;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&a), &l) != 0) return 0;
    if (a.ss_family == AF_INET)
        return ntohs(reinterpret_cast<const sockaddr_in*>(&a)->sin_port);
    if (a.ss_family == AF_INET6)
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&a)->sin6_port);
    return 0;
}

// Se conecta a `host:puerto` y devuelve el socket ya NO BLOQUEANTE, sin Nagle
// y sin SIGPIPE, o invalido() si no hay nadie. Prueba las direcciones del host
// en el orden en que las da el sistema -`localhost` suele ser primero ::1 y
// luego 127.0.0.1-, y cada una con un plazo de `plazo_ms`: un `connect`
// bloqueante contra una máquina que no contesta puede tardar minutos en
// rendirse, y quien lo llama, `mcu-sim` antes de arrancar, no debe quedarse
// colgado tanto. Contra el bucle local el rechazo es inmediato.
inline socket_t conecta(const char* host, unsigned puerto, int plazo_ms = 5000) {
    if (!arranca()) return invalido();
    const std::string h = detalle::sin_corchetes(host);
    addrinfo pista{};
    pista.ai_family   = AF_UNSPEC;
    pista.ai_socktype = SOCK_STREAM;
    addrinfo* r = nullptr;
    if (::getaddrinfo(h.c_str(), std::to_string(puerto).c_str(), &pista, &r) != 0 || !r)
        return invalido();
    socket_t s = invalido();
    for (addrinfo* a = r; a && !valido(s); a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (!valido(s)) continue;
        sin_sigpipe(s);
        if (!no_bloqueante(s)) { cerrar(s); continue; }
        if (::connect(s, a->ai_addr, socklen_t(a->ai_addrlen)) == 0) break;
#if defined(_WIN32)
        const bool en_curso = ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
        const bool en_curso = errno == EINPROGRESS || errno == EINTR;
#endif
        if (!en_curso || detalle::espera_escribible(s, plazo_ms) <= 0) { cerrar(s); continue; }
        int err = 0;
        socklen_t l = sizeof err;
        if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &l) != 0 ||
            err != 0) { cerrar(s); continue; }
    }
    ::freeaddrinfo(r);
    if (valido(s)) sin_nagle(s);
    return s;
}

// Espera, como mucho `ms` milisegundos, a que haya algo que leer (o el cierre
// del otro extremo, que también se lee). true si lo hay. Es lo que necesita
// quien lee un socket no bloqueante sin girar en vacío: el saludo de la fase 3
// y las pruebas.
inline bool espera_legible(socket_t s, int ms) {
    fd_set lec;
    FD_ZERO(&lec);
    FD_SET(s, &lec);
    timeval t{ ms / 1000, (ms % 1000) * 1000 };
#if defined(_WIN32)
    return ::select(0, &lec, nullptr, nullptr, &t) > 0;
#else
    return ::select(s + 1, &lec, nullptr, nullptr, &t) > 0;
#endif
}

} // namespace red
} // namespace stm32
#endif // STM32_COMMON_RED_H
