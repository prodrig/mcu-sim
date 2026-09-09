// =============================================================================
// red.h — La ÚNICA parte del modelo que sabe en qué sistema operativo corre
//
// Todo lo demás de `src/` es C++17 y `<systemc>`: se compila igual en Linux, en
// Windows y en macOS sin una sola directiva de preprocesador. Lo que no es
// portable son los sockets TCP, y los sockets están en un solo sitio del
// modelo: los dos servidores de GDB (`common/gdb_rsp.h`) y el cliente de RSP
// con el que la suite se prueba a sí misma (`verif/gdb_client.h`).
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
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#endif

#include <cstddef>
#include <cstdint>

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

} // namespace red
} // namespace stm32
#endif // STM32_COMMON_RED_H
