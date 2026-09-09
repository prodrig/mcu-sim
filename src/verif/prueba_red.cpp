// =============================================================================
// prueba_red.cpp — Comprobación de la capa de red, en las tres plataformas
//
// `common/red.h` es el único fichero del modelo que sabe en qué sistema
// operativo corre, y por tanto el único que no se puede dar por bueno
// compilando en una sola plataforma. Esto lo ejercita entero —abrir, aceptar,
// conectar, mandar, recibir, cerrar— sin SystemC de por medio, así que se puede
// compilar y ejecutar en Windows, en Linux y en macOS, y CRUZAR desde Linux a
// Windows con MinGW para comprobar que la rama de Winsock al menos compila y
// enlaza:
//
//   make -f Makefile.stm32 red                       # nativo
//   make -f Makefile.stm32 red PLATAFORMA=windows CXX=x86_64-w64-mingw32-g++   # cruzado
//
// Código de salida 0 si todo va bien.
// =============================================================================
#include "../common/red.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace stm32;

static unsigned g_ok = 0, g_mal = 0;
static bool comprueba(bool c, const char* que) {
    (c ? g_ok : g_mal)++;
    std::printf("  [%s] %s\n", c ? "OK  " : "FALLO", que);
    return c;
}

int main() {
    comprueba(red::arranca(), "la pila de red arranca (WSAStartup en Windows)");

    // Un descriptor invalido tiene que reconocerse como tal. En Windows es un
    // entero SIN SIGNO, asi que `s < 0` seria siempre falso: por eso hay un
    // valido() y no una comparacion.
    comprueba(!red::valido(red::invalido()),
              "el descriptor invalido se reconoce sin comparar con cero");

    // Un puerto alto y poco probable, para no chocar con nada.
    const unsigned PUERTO = 47811;
    red::socket_t srv = red::escucha_local(PUERTO);
    if (!comprueba(red::valido(srv), "se abre un escuchador en localhost")) {
        std::printf("RESULTADO %u ok, %u fallos\n", g_ok, g_mal);
        return 1;
    }

    // Sin cliente todavia: aceptar tiene que devolver invalido y NO bloquear.
    comprueba(!red::valido(red::acepta(srv)),
              "sin cliente, aceptar no bloquea y devuelve invalido");

    red::socket_t cli = red::conecta_local(PUERTO);
    comprueba(red::valido(cli), "un cliente se conecta al escuchador");

    red::socket_t ser = red::invalido();
    for (int i = 0; i < 1000 && !red::valido(ser); ++i) ser = red::acepta(srv);
    comprueba(red::valido(ser), "y el escuchador lo acepta");

    const std::string msj = "$qSupported#37";
    const long e = red::enviar(cli, msj.data(), msj.size());
    comprueba(e == long(msj.size()), "se manda un paquete entero");

    std::string llegado;
    char b[256];
    for (int i = 0; i < 1000 && llegado.size() < msj.size(); ++i) {
        const long r = red::recibir(ser, b, sizeof b);
        if (r > 0) llegado.append(b, size_t(r));
        else if (r < 0 && !red::reintentar()) break;
    }
    comprueba(llegado == msj, "y llega igual por el otro lado");

    // Con el socket en no bloqueante y sin datos, recibir devuelve <0 y
    // `reintentar()` tiene que decir que no es un error de verdad. Es la
    // traduccion de EAGAIN/EWOULDBLOCK a WSAEWOULDBLOCK, y es la parte que mas
    // facilmente se rompe al portar.
    const long r0 = red::recibir(ser, b, sizeof b);
    comprueba(r0 < 0 && red::reintentar(),
              "sin datos, recibir dice 'ahora no' y no 'error'");

    // Cerrado por el otro extremo: recibir devuelve 0, que es distinto de <0.
    red::cerrar(cli);
    comprueba(!red::valido(cli), "cerrar deja el descriptor invalido");
    long rc = -1;
    for (int i = 0; i < 1000; ++i) {
        rc = red::recibir(ser, b, sizeof b);
        if (rc == 0 || (rc < 0 && !red::reintentar())) break;
    }
    comprueba(rc == 0, "y el otro extremo ve el cierre como cero bytes");

    // Escribir en un socket que el otro lado cerro no debe MATAR el proceso.
    // En Linux lo evita MSG_NOSIGNAL; en macOS, SO_NOSIGPIPE; en Windows no hay
    // SIGPIPE. Si esta linea mata el programa, la portabilidad esta mal hecha.
    red::enviar(ser, "x", 1);
    red::enviar(ser, "x", 1);
    comprueba(true, "escribir en un socket cerrado no mata el proceso");

    red::cerrar(ser);
    red::cerrar(srv);
    comprueba(!red::valido(ser) && !red::valido(srv), "todo queda cerrado");

    std::printf("RESULTADO %u ok, %u fallos\n", g_ok, g_mal);
    return g_mal ? 1 : 0;
}
