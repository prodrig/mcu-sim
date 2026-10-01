// =============================================================================
// prueba_gui_proto.cpp — El transporte entre mcu-sim y mcu-sim-gui, sin SystemC
//
// Fase 2 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`):
// la capa que mueve bytes, probada antes de que ningún mensaje signifique
// nada. Va como `make red`, `make serie` o `make rfc2217`: sin SystemC, en el
// trabajo rápido del CI y en segundos, y sin tocar una cifra de
// `verif/invariantes.txt` (la decisión D-12 del puente UART).
//
// Tres partes:
//
//   P1  el marco, sin red: cabecera byte a byte, mensajes troceados de todas
//       las formas, y los casos feos —magia mala, versión imposible, longitud
//       de 4 GB, tipo del otro sentido, tipo desconocido que se salta—;
//   P2  por un socket de verdad, con DOS HILOS, uno por extremo: los
//       diecinueve tipos de mensaje de ida y vuelta, una placa de 300 kB que
//       llega en muchos `recv`, un mensaje partido en dos `recv` a la fuerza,
//       dos mensajes en un solo `recv`, un tipo desconocido en mitad, y un
//       navegador apuntando al puerto. Y `conecta`/`escucha` con host;
//   P4  el saludo del lado del modelo (fase 3): `gui::ClienteGui` contra una
//       GUI falsa en otro hilo. El camino bueno, `T_PARA`, `--valida`, el
//       `T_PING` mientras espera, y cada forma en que una GUI puede fallar.
//   P3  que las copias de `protocolo.h` y `proto_io.h` que lleva
//       `mcu-sim-gui` sean idénticas byte a byte a las de aquí (el riesgo R-6
//       del plan). Busca el otro repositorio al lado de este, o donde diga
//       `--gui-repo RUTA`; si no se da la ruta y no está, lo dice y no falla,
//       porque quien solo ha clonado `mcu-sim` tiene que poder pasar esto. Si
//       la ruta se da y no está, sí falla: es lo que hace el CI.
//
//   make -f Makefile.mcu-sim gui-proto
//   make -f Makefile.mcu-sim gui-proto GUI_REPO=../../mcu-sim-gui
//
// Código de salida 0 si todo va bien.
// =============================================================================
#include "../common/red.h"
#include "../common/proto_io.h"
#include "../common/gui_cliente.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace stm32;
using namespace mcusim::proto;

namespace {

unsigned g_ok = 0, g_mal = 0;
bool comprueba(bool c, const std::string& que) {
    (c ? g_ok : g_mal)++;
    std::printf("  [%s] %s\n", c ? "OK  " : "FALLO", que.c_str());
    return c;
}
void grupo(const char* t) { std::printf("%s\n", t); }

// Un mensaje leído, copiado fuera del búfer del lector.
struct Copia {
    uint16_t    tipo = 0;
    uint32_t    secuencia = 0;
    std::string cuerpo;
};

// Todos los mensajes completos que tenga el lector, copiados. Se salta los
// desconocidos si `salta`, que es lo que hará quien lea de verdad.
std::vector<Copia> vacia(Lector& L, bool salta = false, unsigned* saltados = nullptr) {
    std::vector<Copia> v;
    Mensaje m;
    while (L.saca(m) == Lector::LEC_MENSAJE) {
        if (salta && !es_conocido(m.tipo)) { if (saltados) ++*saltados; continue; }
        v.push_back({m.tipo, m.secuencia, m.texto()});
    }
    return v;
}

// Lee del socket hasta tener `n` mensajes, un error del lector, el cierre del
// otro extremo o el plazo. Cuenta cuántos `recv` trajeron datos.
struct Lectura {
    std::vector<Copia> msjs;
    bool     cerrado = false;
    unsigned recvs = 0;
    unsigned saltados = 0;
};
Lectura lee(red::socket_t s, Lector& L, std::size_t n, int plazo_ms = 5000, bool salta = false) {
    Lectura r;
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(plazo_ms);
    char b[4096];
    while (r.msjs.size() < n && !L.roto() && std::chrono::steady_clock::now() < fin) {
        if (!red::espera_legible(s, 20)) continue;
        const long k = red::recibir(s, b, sizeof b);
        if (k == 0) { r.cerrado = true; break; }
        if (k < 0) { if (red::reintentar()) continue; r.cerrado = true; break; }
        ++r.recvs;
        L.mete(b, std::size_t(k));
        for (Copia& c : vacia(L, salta, &r.saltados)) r.msjs.push_back(std::move(c));
    }
    return r;
}

// Manda todo, aunque el socket no bloqueante acepte menos de una vez.
bool manda(red::socket_t s, const std::string& d) {
    std::size_t hecho = 0;
    const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (hecho < d.size() && std::chrono::steady_clock::now() < fin) {
        const long k = red::enviar(s, d.data() + hecho, d.size() - hecho);
        if (k > 0) hecho += std::size_t(k);
        else if (k < 0 && red::reintentar()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else return false;
    }
    return hecho == d.size();
}

template <class T> std::string bytes(const T& t) {
    return std::string(reinterpret_cast<const char*>(&t), sizeof t);
}

// Los diecinueve tipos, con un cuerpo representativo de cada uno: el que dice
// `protocolo.h`, con valores que no son ceros para que un byte cambiado se
// note.
struct Caso { uint16_t tipo; std::string cuerpo; };

std::string placa_grande() {
    std::string s = "<placa nombre=\"grande\">\n";
    for (int i = 0; s.size() < 300000; ++i)
        s += "  <componente tipo=\"Led\" id=\"LD" + std::to_string(i) +
             "\" vf=\"2.0\" r=\"330\"><pin nombre=\"anodo\" nodo=\"PD12\"/></componente>\n";
    return s + "</placa>\n";
}

std::vector<Caso> del_modelo() {
    std::vector<Caso> v;
    v.push_back({T_HOLA, "protocolo_max=1\nmcu_sim=0.9.0\npid=48211\nplaca=placas/discovery_min.xml\n"});
    v.push_back({T_PLACA, placa_grande()});
    v.push_back({T_CATALOGO, "<catalogo>\n  <pieza idx=\"0\" id=\"LD4\" tipo=\"Led\">\n"
                             "    <observable idx=\"0\" id_obs=\"0\" nombre=\"encendido\"/>\n"
                             "  </pieza>\n</catalogo>\n"});
    v.push_back({T_LISTO, ""});
    {
        CabInstantanea c{1234567890123ull, 3, 7};
        std::string s = bytes(c);
        for (uint16_t i = 0; i < 3; ++i) s += bytes(Muestra{uint16_t(i + 10), 0, 1.5f * i});
        v.push_back({T_INSTANTANEA, s});
    }
    {
        const std::string origen = "/stm32/no_modelado", texto = "el DMA2D no esta modelado";
        CabAviso c{N_AVISO, uint32_t(origen.size()), 42000000ull};
        v.push_back({T_AVISO, bytes(c) + origen + texto});
    }
    v.push_back({T_ESTADO, bytes(Estado{F_CORRIENDO, 0, 5000000ull, 1.25, 987654ull})});
    v.push_back({T_ORDEN_HECHA, bytes(OrdenHecha{1500000000ull, 3, 0, 1.f, RES_TARDE, 0})});
    v.push_back({T_PONG, ""});
    v.push_back({T_FIN, bytes(Fin{M_PARA, 0, 4220000000ull})});
    return v;
}

std::vector<Caso> de_pantalla() {
    std::vector<Caso> v;
    v.push_back({T_VERSION, "protocolo=1\ngui=0.1.0\n"});
    {
        CabSuscribe c{16666667u, 0u, 2u, 0u};
        v.push_back({T_SUSCRIBE, bytes(c) + bytes(uint16_t(0)) + bytes(uint16_t(5))});
    }
    v.push_back({T_ARRANCA, bytes(Arranca{RIT_REAL, 0.5f, 10000000000ull})});
    v.push_back({T_PAUSA, ""});
    v.push_back({T_SIGUE, ""});
    v.push_back({T_PASO, bytes(Paso{1000000ull})});
    {
        // El ejemplo de doc/protocolo.md §5
        std::string s;
        s += bytes(Orden{1000000000ull, 3, 0, 1.f});
        s += bytes(Orden{ 500000000ull, 3, 0, 0.f});
        s += bytes(Orden{2500000000ull, 3, 0, 1.f});
        s += bytes(Orden{ 220000000ull, 3, 0, 0.f});
        v.push_back({T_ORDENES, s});
    }
    v.push_back({T_PARA, ""});
    v.push_back({T_PING, ""});
    return v;
}

std::string codifica(Emisor& e, const std::vector<Caso>& v) {
    std::string s;
    for (const Caso& c : v) e.mensaje(s, c.tipo, c.cuerpo);
    return s;
}

bool iguales(const std::vector<Copia>& a, const std::vector<Caso>& b, uint32_t sec0 = 0) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].tipo != b[i].tipo || a[i].cuerpo != b[i].cuerpo ||
            a[i].secuencia != sec0 + i) return false;
    return true;
}

// ===========================================================================
// P1 — El marco, sin red
// ===========================================================================
void p1_marco() {
    grupo("P1 El marco, sin red");

    // --- La cabecera, byte a byte ------------------------------------------
    {
        Emisor e;
        std::string s;
        e.mensaje(s, T_PLACA, "abc", 3);
        const unsigned char esp[19] = {
            'M', 'S', 'G', '1',            // la magia se lee en un volcado
            0x01, 0x00,                    // version 1, la del saludo
            0x02, 0x00,                    // T_PLACA
            0x03, 0x00, 0x00, 0x00,        // longitud 3
            0x00, 0x00, 0x00, 0x00,        // secuencia 0
            'a', 'b', 'c' };
        comprueba(s.size() == 19 && std::memcmp(s.data(), esp, 19) == 0,
                  "la cabecera son 16 bytes little-endian: 'MSG1', version, tipo, "
                  "longitud, secuencia, y detras el cuerpo");
        e.vacio(s, T_LISTO);
        comprueba(s.size() == 35 && s[16 + 3 + 12] == 1, "la secuencia sube de uno en uno");
        comprueba(e.version() == VERSION_SALUDO && VERSION_SALUDO == 1,
                  "antes de negociar se escribe con la version 1, la del saludo");
        std::string grande;
        comprueba(!e.mensaje(grande, T_PLACA, nullptr, std::size_t(CUERPO_MAX) + 1) &&
                  grande.empty(),
                  "un cuerpo de mas de 8 MiB no se escribe: el otro lado lo rechazaria");
    }

    // --- Ida y vuelta, en todos los trozos posibles ------------------------
    const std::vector<Caso> casos = del_modelo();
    Emisor e;
    const std::string todo = codifica(e, casos);
    {
        Lector L(Origen::Modelo);
        L.mete(todo.data(), todo.size());
        comprueba(iguales(vacia(L), casos),
                  "los diez tipos del modelo, de un golpe: los mismos bytes, en orden");
        comprueba(L.pendientes() == 0 && L.leidos() == casos.size() && !L.roto(),
                  "y no sobra ni falta nada");
    }
    {
        Lector L(Origen::Modelo);
        std::vector<Copia> v;
        bool falta_a_medias = true;
        for (char c : todo) {
            L.mete(&c, 1);
            Mensaje m;
            Lector::Estado st;
            while ((st = L.saca(m)) == Lector::LEC_MENSAJE) v.push_back({m.tipo, m.secuencia, m.texto()});
            if (st != Lector::LEC_FALTA) falta_a_medias = false;
        }
        comprueba(iguales(v, casos) && falta_a_medias,
                  "y metidos de byte en byte: un mensaje a medias es FALTA, no un error");
    }
    {
        // Trozos de tamaños primos, que caen en cualquier sitio
        Lector L(Origen::Modelo);
        std::vector<Copia> v;
        const std::size_t t[] = {3, 7, 13, 101, 4099, 65537};
        std::size_t i = 0, k = 0;
        while (i < todo.size()) {
            const std::size_t n = std::min(t[k++ % 6], todo.size() - i);
            L.mete(todo.data() + i, n);
            i += n;
            for (Copia& c : vacia(L)) v.push_back(std::move(c));
        }
        comprueba(iguales(v, casos), "y en trozos de 3, 7, 13, 101, 4099 y 65537 bytes");
    }
    {
        Lector L(Origen::Pantalla);
        Emisor ep;
        const std::vector<Caso> p = de_pantalla();
        const std::string s = codifica(ep, p);
        L.mete(s.data(), s.size());
        comprueba(iguales(vacia(L), p), "los nueve tipos de la pantalla, de vuelta");
    }
    {
        Lector L(Origen::Modelo);
        std::string s;
        Emisor e2;
        e2.pod(s, T_ESTADO, Estado{F_PAUSADA, 0, 77ull, 2.5, 3ull});
        L.mete(s.data(), s.size());
        Mensaje m;
        Estado est{};
        CabInstantanea mal{};
        comprueba(L.saca(m) == Lector::LEC_MENSAJE && m.como(est) && est.fase == F_PAUSADA &&
                  est.t_sim_ns == 77 && est.t_pared_s == 2.5 && !m.como(mal),
                  "un cuerpo POD se recupera con como(), que se niega si no mide lo que debe");
    }

    // --- Los casos feos ----------------------------------------------------
    auto cabecera = [](uint32_t magia, uint16_t ver, uint16_t tipo, uint32_t lon) {
        Cabecera c{magia, ver, tipo, lon, 0};
        return bytes(c);
    };
    {
        Lector L(Origen::Modelo);
        const std::string get = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        L.mete(get.data(), get.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_ERROR && L.error().find("magia") != std::string::npos,
                  "un navegador apuntado al puerto: magia mala, y el error lo dice");
        const std::string bueno = codifica(e, {{T_PONG, ""}});
        L.mete(bueno.data(), bueno.size());
        comprueba(L.saca(m) == Lector::LEC_ERROR && L.roto(),
                  "y no intenta resincronizar: roto se queda, aunque llegue algo bueno");
    }
    {
        Lector L(Origen::Modelo);
        const std::string c = cabecera(MAGIA, 0, T_PONG, 0);
        L.mete(c.data(), c.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_ERROR && L.error().find("version") != std::string::npos,
                  "version 0: imposible");
    }
    {
        Lector L(Origen::Modelo);
        const std::string c = cabecera(MAGIA, 2, T_HOLA, 0);
        L.mete(c.data(), c.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_ERROR,
                  "version 2 antes de negociar: el saludo va siempre en la 1");
    }
    {
        Lector L(Origen::Modelo);
        L.fija_version(1);
        const std::string c = cabecera(MAGIA, 1, T_PONG, 0);
        L.mete(c.data(), c.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_MENSAJE, "con la 1 negociada, la 1 vale");
        Lector L2(Origen::Modelo);
        L2.fija_version(2);
        L2.mete(c.data(), c.size());
        comprueba(L2.saca(m) == Lector::LEC_ERROR,
                  "y con otra negociada, un mensaje en la 1 ya no: va la que esta en uso");
    }
    {
        Lector L(Origen::Modelo);
        const std::string c = cabecera(MAGIA, 1, T_PLACA, 0xFFFFFFFFu);
        L.mete(c.data(), c.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_ERROR && L.pendientes() == 16 &&
                  L.error().find("techo") != std::string::npos,
                  "una longitud de 4 GB se rechaza con la cabecera sola, sin esperar el cuerpo");
    }
    {
        Lector L(Origen::Modelo);
        const std::string c = cabecera(MAGIA, 1, T_PLACA, CUERPO_MAX);
        L.mete(c.data(), c.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_FALTA && !L.roto(),
                  "y una de 8 MiB justos es legal: se espera a que llegue");
    }
    {
        Lector L(Origen::Modelo);
        Emisor ep;
        const std::string s = codifica(ep, {{T_VERSION, "protocolo=1\n"}});
        L.mete(s.data(), s.size());
        Mensaje m;
        comprueba(L.saca(m) == Lector::LEC_ERROR && L.error().find("pantallas") != std::string::npos,
                  "un tipo del otro sentido: dos pantallas hablandose, y se dice");
    }
    {
        Lector L(Origen::Modelo);
        std::string s;
        Emisor e2;
        e2.mensaje(s, T_HOLA, "hola");
        e2.mensaje(s, 0x0042, "algo que esta version no conoce");
        e2.vacio(s, T_LISTO);
        L.mete(s.data(), s.size());
        unsigned saltados = 0;
        const std::vector<Copia> v = vacia(L, true, &saltados);
        comprueba(v.size() == 2 && v[0].tipo == T_HOLA && v[1].tipo == T_LISTO &&
                  saltados == 1 && !L.roto(),
                  "un tipo desconocido se entrega, se salta por su longitud y se sigue");
        comprueba(!es_conocido(0x0042) && es_conocido(T_PING) && es_conocido(T_FIN),
                  "es_conocido() distingue los diecinueve de los demas");
    }
    {
        Lector L(Origen::Modelo);
        std::string s;
        Emisor e2;
        e2.vacio(s, T_PONG);              // 0
        e2.vacio(s, T_PONG);              // 1
        Emisor e3;
        e3.vacio(s, T_PONG);              // 0 otra vez: no es la siguiente
        L.mete(s.data(), s.size());
        vacia(L);
        comprueba(L.saltos_de_secuencia() == 1, "un salto de secuencia se cuenta");
    }
}

// ===========================================================================
// P2 — Por un socket de verdad, con dos hilos
// ===========================================================================
void p2_socket() {
    grupo("P2 Por un socket de verdad, con dos hilos");

    red::socket_t srv = red::escucha("127.0.0.1", 0);
    const unsigned puerto = red::valido(srv) ? red::puerto_local(srv) : 0;
    if (!comprueba(red::valido(srv) && puerto != 0,
                   "escucha(\"127.0.0.1\", 0): el sistema elige el puerto y se sabe cual"))
        return;

    // El hilo del MODELO: se conecta, como hara mcu-sim con --gui, manda lo
    // suyo y lee lo de la pantalla. Lo que comprueba lo guarda; los
    // `comprueba` se hacen en el hilo principal, que es el que imprime.
    std::atomic<int>  paso{0};
    std::vector<Copia> recibido_modelo;
    bool modelo_conecto = false, modelo_vio_cierre = false;
    std::thread modelo([&] {
        red::socket_t s = red::conecta("localhost", puerto);
        modelo_conecto = red::valido(s);
        if (!modelo_conecto) { paso = -1; return; }
        Emisor e;
        manda(s, codifica(e, del_modelo()));                       // 1: todo
        Lector L(Origen::Pantalla);
        recibido_modelo = lee(s, L, de_pantalla().size()).msjs;   // 2: lo de la pantalla
        // 3: un mensaje partido en dos `recv`, a la fuerza: la primera mitad,
        // esperar a que el otro lado la haya leido, y la segunda
        std::string p;
        e.mensaje(p, T_CATALOGO, std::string(1000, 'x'));
        while (paso.load() < 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        manda(s, p.substr(0, 500));
        while (paso.load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        manda(s, p.substr(500));
        // 4: dos mensajes en un solo send, que llegan en un solo recv
        while (paso.load() < 3) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::string dos;
        e.vacio(dos, T_PONG);
        e.mensaje(dos, T_AVISO, bytes(CabAviso{N_INFO, 0, 1}) + "hola");
        manda(s, dos);
        paso = 4;
        // 5: un desconocido en mitad
        while (paso.load() < 5) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::string d;
        e.mensaje(d, 0x0077, std::string(300, 'z'));
        e.vacio(d, T_PONG);
        manda(s, d);
        // 6: la pantalla cierra; el modelo lo ve como cero bytes
        char b[64];
        const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < fin) {
            if (!red::espera_legible(s, 20)) continue;
            const long k = red::recibir(s, b, sizeof b);
            if (k == 0 || (k < 0 && !red::reintentar())) { modelo_vio_cierre = true; break; }
        }
        // Y escribir despues del cierre no lo mata (SIGPIPE)
        red::enviar(s, "x", 1);
        red::enviar(s, "x", 1);
        red::cerrar(s);
    });

    // El hilo principal hace de PANTALLA: acepta y lee.
    red::socket_t c = red::invalido();
    for (int i = 0; i < 500 && !red::valido(c) && paso.load() >= 0; ++i) {
        c = red::acepta(srv);
        if (!red::valido(c)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!comprueba(red::valido(c), "conecta(\"localhost\", ...) llega y se acepta")) {
        modelo.join();
        red::cerrar(srv);
        return;
    }

    Lector L(Origen::Modelo);
    const std::vector<Caso> dm = del_modelo();
    Lectura r = lee(c, L, dm.size());
    comprueba(iguales(r.msjs, dm),
              "los diez tipos del modelo cruzan el socket, byte a byte y en orden");
    comprueba(r.recvs > 20, "y la placa de 300 kB llego en muchos recv (" +
                            std::to_string(r.recvs) + "), que es como llega de verdad");

    Emisor ep;
    manda(c, codifica(ep, de_pantalla()));

    // 3: partido en dos recv
    paso = 1;
    Lectura r1 = lee(c, L, 1, 300);
    const bool a_medias = r1.msjs.empty() && L.pendientes() == 500 && !L.roto();
    paso = 2;
    Lectura r2 = lee(c, L, 1);
    comprueba(a_medias && r2.msjs.size() == 1 && r2.msjs[0].tipo == T_CATALOGO &&
              r2.msjs[0].cuerpo == std::string(1000, 'x'),
              "un mensaje partido en dos recv: la primera mitad espera, la segunda lo completa");

    // 4: dos en un recv
    paso = 3;
    while (paso.load() < 4) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Lectura r3 = lee(c, L, 2);
    comprueba(r3.msjs.size() == 2 && r3.recvs == 1 && r3.msjs[0].tipo == T_PONG &&
              r3.msjs[1].tipo == T_AVISO,
              "dos mensajes en un solo recv: salen los dos");

    // 5: desconocido en mitad
    paso = 5;
    Lectura r4 = lee(c, L, 1, 5000, true);
    comprueba(r4.msjs.size() == 1 && r4.msjs[0].tipo == T_PONG && r4.saltados == 1 && !L.roto(),
              "un tipo desconocido de 300 bytes se salta y el siguiente llega entero");
    comprueba(L.saltos_de_secuencia() == 0,
              "y la secuencia no tiene huecos: no se ha perdido nada");

    // 6: cierre
    red::cerrar(c);
    modelo.join();
    comprueba(modelo_conecto, "el modelo se conecto");
    comprueba(iguales(recibido_modelo, de_pantalla()),
              "y recibio los nueve tipos de la pantalla, byte a byte");
    comprueba(modelo_vio_cierre, "cuando la pantalla cierra, el modelo lo ve como cero bytes");
    comprueba(true, "y escribir despues no ha matado el proceso");

    // --- Un navegador apuntado al puerto ------------------------------------
    {
        red::socket_t nav = red::conecta("127.0.0.1", puerto);
        red::socket_t a = red::invalido();
        for (int i = 0; i < 500 && !red::valido(a); ++i) {
            a = red::acepta(srv);
            if (!red::valido(a)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        manda(nav, "GET / HTTP/1.1\r\nHost: localhost:3344\r\n\r\n");
        Lector Ln(Origen::Modelo);
        lee(a, Ln, 1, 2000);
        comprueba(Ln.roto() && Ln.error().find("magia") != std::string::npos,
                  "un navegador en el puerto: se ve en los primeros 16 bytes");
        red::cerrar(a);
        red::cerrar(nav);
    }
    red::cerrar(srv);

    // --- conecta y escucha con host -----------------------------------------
    {
        const auto t0 = std::chrono::steady_clock::now();
        red::socket_t nadie = red::conecta("127.0.0.1", puerto);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        // Sin agotar el plazo, y no «en el acto»: en Linux y macOS el rechazo
        // es inmediato, pero Windows reintenta el SYN tras el RST y tarda
        // unos dos segundos en rendirse aun contra el bucle local.
        comprueba(!red::valido(nadie) && ms < 4500,
                  "sin nadie escuchando, conecta() falla sin agotar el plazo de 5 s (" +
                  std::to_string(ms) + " ms)");
        comprueba(!red::valido(red::conecta("no-existe.invalid", 3344)),
                  "y con un host que no resuelve, tambien");
    }
    {
        red::socket_t s6 = red::escucha("[::1]", 0);
        if (!red::valido(s6)) {
            std::printf("  [SALTA] esta maquina no tiene IPv6 en el bucle local\n");
        } else {
            const unsigned p6 = red::puerto_local(s6);
            red::socket_t c6 = red::conecta("[::1]", p6);
            red::socket_t a6 = red::invalido();
            for (int i = 0; i < 500 && !red::valido(a6) && red::valido(c6); ++i) {
                a6 = red::acepta(s6);
                if (!red::valido(a6)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Emisor e6;
            std::string m6;
            e6.vacio(m6, T_LISTO);
            manda(c6, m6);
            Lector L6(Origen::Modelo);
            const Lectura r6 = lee(a6, L6, 1);
            comprueba(red::valido(c6) && r6.msjs.size() == 1 && r6.msjs[0].tipo == T_LISTO,
                      "IPv6: escucha(\"[::1]\") y conecta(\"[::1]\"), con los corchetes de "
                      "--gui, y un mensaje que cruza");
            red::cerrar(a6);
            red::cerrar(c6);
            red::cerrar(s6);
        }
    }
    {
        // Las de siempre siguen ahi y siguen siendo de bucle local
        red::socket_t l = red::escucha_local(0);
        comprueba(red::valido(l) && red::puerto_local(l) != 0,
                  "escucha_local sigue existiendo para los servidores de GDB");
        red::cerrar(l);
    }
}


// ===========================================================================
// P4 — El saludo, del lado del modelo
// ===========================================================================

// Una GUI falsa: escucha en un puerto libre y, en otro hilo, acepta y sigue
// el `guion`. Mientras, el hilo principal hace de modelo con un ClienteGui.
struct GuiFalsa {
    red::socket_t srv = red::invalido();
    unsigned      puerto = 0;
    std::thread   hilo;
    template <class F> explicit GuiFalsa(F guion) {
        srv = red::escucha("127.0.0.1", 0);
        puerto = red::puerto_local(srv);
        hilo = std::thread([this, guion] {
            red::socket_t c = red::invalido();
            for (int i = 0; i < 500 && !red::valido(c); ++i) {
                c = red::acepta(srv);
                if (!red::valido(c)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!red::valido(c)) return;
            Lector  L(Origen::Modelo);
            Emisor  e;
            guion(c, L, e);
            red::cerrar(c);
        });
    }
    ~GuiFalsa() { if (hilo.joinable()) hilo.join(); red::cerrar(srv); }
    gui::Destino destino() const {
        gui::Destino d;
        d.valido = true; d.host = "127.0.0.1"; d.puerto = uint16_t(puerto);
        return d;
    }
};

// Lo que la GUI falsa manda y recibe, en una línea.
void manda_gui(red::socket_t c, Emisor& e, uint16_t tipo, const std::string& cuerpo = "") {
    std::string s;
    e.mensaje(s, tipo, cuerpo);
    manda(c, s);
}
std::vector<Copia> recibe_gui(red::socket_t c, Lector& L, std::size_t n, int ms = 3000) {
    return lee(c, L, n, ms).msjs;
}

const std::string HOLA = "protocolo_max=1\nmcu_sim=prueba\n";
const std::string PLACA = "<placa nombre=\"p\">\n</placa>\n";
const std::string CATALOGO = "<catalogo>\n</catalogo>\n";

void p4_saludo() {
    grupo("P4 El saludo, del lado del modelo");
    using gui::ClienteGui;
    using D = ClienteGui::Desenlace;

    // --- El camino bueno, con todo lo que puede llegar mientras se espera ---
    {
        std::vector<Copia> vio, final;
        bool vio_cierre = false;
        GuiFalsa g([&](red::socket_t c, Lector& L, Emisor& e) {
            vio = recibe_gui(c, L, 1);                         // T_HOLA
            manda_gui(c, e, T_VERSION, "protocolo=1\ngui=prueba\n");
            L.fija_version(1); e.fija_version(1);
            for (Copia& x : recibe_gui(c, L, 3)) vio.push_back(x);   // PLACA, CATALOGO, LISTO
            manda_gui(c, e, T_SUSCRIBE, bytes(CabSuscribe{1000000u, 0u, 0u, 0u}));
            manda_gui(c, e, T_SUSCRIBE, gui::cuerpo_suscripcion(2000000ull, {3, 5}));
            manda_gui(c, e, T_ORDENES, bytes(Orden{1, 0, 0, 1.f}));
            manda_gui(c, e, T_PAUSA);
            manda_gui(c, e, T_ORDENES, gui::cuerpo_ordenes({{5, 2, 1, 0.5f}, {0, 2, 1, 0.f}}));
            manda_gui(c, e, 0x8077, "un tipo que esta version no conoce");
            manda_gui(c, e, T_PING);
            for (Copia& x : recibe_gui(c, L, 1)) vio.push_back(x);   // T_PONG
            manda_gui(c, e, T_ARRANCA, bytes(Arranca{RIT_LIBRE, 1.f, 5000000ull}));
            final = recibe_gui(c, L, 1);                       // T_FIN
            Lectura r = lee(c, L, 1, 2000);
            vio_cierre = r.cerrado;
        });
        ClienteGui m;
        Arranca arr{};
        const bool con = m.conecta(g.destino());
        const D d = m.saluda(HOLA, PLACA, CATALOGO, false, arr);
        comprueba(con && d == D::Arranca, "conecta, saluda y llega a T_ARRANCA");
        comprueba(arr.ritmo == RIT_LIBRE && arr.ventana_ns == 5000000ull,
                  "con el cuerpo de T_ARRANCA intacto");
        comprueba(m.version() == 1, "con la version que eligio la GUI");
        comprueba(m.hay_suscripcion() &&
                  m.suscripcion() == gui::cuerpo_suscripcion(2000000ull, {3, 5}),
                  "de dos T_SUSCRIBE antes de arrancar se guarda el ultimo, entero: "
                  "sim lo aplica antes de sc_start (fase 4)");
        comprueba(m.ordenes_previas().size() == 2 &&
                  m.ordenes_previas()[0] == bytes(Orden{1, 0, 0, 1.f}) &&
                  m.ordenes_previas()[1] ==
                      gui::cuerpo_ordenes({{5, 2, 1, 0.5f}, {0, 2, 1, 0.f}}),
                  "los dos T_ORDENES de antes de arrancar se guardan TODOS, en orden "
                  "y enteros: sim los encola antes de sc_start (fase 5)");
        comprueba(m.ignorados() == 1,
                  "T_PAUSA antes de arrancar no quiere decir nada -no hay nada que "
                  "pausar-: se lee y se ignora; y el desconocido se salta sin contarlo");
        m.fin(M_VENTANA, 0, 123456789ull);
        g.hilo.join();
        comprueba(vio.size() == 5 && vio[0].tipo == T_HOLA && vio[0].cuerpo == HOLA &&
                  vio[1].tipo == T_PLACA && vio[1].cuerpo == PLACA &&
                  vio[2].tipo == T_CATALOGO && vio[2].cuerpo == CATALOGO &&
                  vio[3].tipo == T_LISTO && vio[4].tipo == T_PONG,
                  "la GUI ve T_HOLA, T_PLACA, T_CATALOGO, T_LISTO y, al T_PING, T_PONG");
        Fin f{};
        comprueba(final.size() == 1 && final[0].tipo == T_FIN &&
                  final[0].cuerpo.size() == sizeof f &&
                  (std::memcpy(&f, final[0].cuerpo.data(), sizeof f), true) &&
                  f.motivo == M_VENTANA && f.t_sim_ns == 123456789ull && vio_cierre,
                  "y al final un T_FIN con su motivo y su instante, y el cierre");
    }

    // --- T_PARA antes de arrancar --------------------------------------------
    {
        GuiFalsa g([&](red::socket_t c, Lector& L, Emisor& e) {
            recibe_gui(c, L, 1);
            manda_gui(c, e, T_VERSION, "protocolo=1\n");
            L.fija_version(1); e.fija_version(1);
            recibe_gui(c, L, 3);
            manda_gui(c, e, T_PARA);
            recibe_gui(c, L, 1);
        });
        ClienteGui m;
        Arranca arr{};
        m.conecta(g.destino());
        comprueba(m.saluda(HOLA, PLACA, CATALOGO, false, arr) == D::Para,
                  "un T_PARA antes de arrancar termina el saludo sin simular");
        m.fin(M_PARA, 0, 0);
    }

    // --- --valida: placa y catálogo, sin T_LISTO y sin esperar ---------------
    {
        std::vector<Copia> vio;
        GuiFalsa g([&](red::socket_t c, Lector& L, Emisor& e) {
            recibe_gui(c, L, 1);
            manda_gui(c, e, T_VERSION, "protocolo=1\n");
            L.fija_version(1); e.fija_version(1);
            vio = recibe_gui(c, L, 3);
        });
        ClienteGui m;
        Arranca arr{};
        m.conecta(g.destino());
        const D d = m.saluda(HOLA, PLACA, CATALOGO, true, arr);
        m.fin(M_VENTANA, 0, 0);
        g.hilo.join();
        comprueba(d == D::Valida && vio.size() == 3 && vio[0].tipo == T_PLACA &&
                  vio[1].tipo == T_CATALOGO && vio[2].tipo == T_FIN,
                  "con --valida: T_PLACA, T_CATALOGO y T_FIN, sin T_LISTO ni espera");
    }

    // --- Los avisos de placa, entre T_CATALOGO y T_LISTO (fase 4) ------------
    {
        std::vector<Copia> vio;
        GuiFalsa g([&](red::socket_t c, Lector& L, Emisor& e) {
            recibe_gui(c, L, 1);
            manda_gui(c, e, T_VERSION, "protocolo=1\n");
            L.fija_version(1); e.fija_version(1);
            vio = recibe_gui(c, L, 5);
            manda_gui(c, e, T_PARA);
            recibe_gui(c, L, 1);
        });
        ClienteGui m;
        Arranca arr{};
        m.avisos_de_placa({"nodo X: conducen a la vez A.a y B.a", "otro"});
        m.conecta(g.destino());
        const D d = m.saluda(HOLA, PLACA, CATALOGO, false, arr);
        m.fin(M_PARA, 0, 0);
        g.hilo.join();
        CabAviso ca{};
        const bool forma = vio.size() == 5 && vio[2].cuerpo.size() >= sizeof ca &&
                           (std::memcpy(&ca, vio[2].cuerpo.data(), sizeof ca), true);
        comprueba(d == D::Para && vio.size() == 5 && vio[0].tipo == T_PLACA &&
                  vio[1].tipo == T_CATALOGO && vio[2].tipo == T_AVISO &&
                  vio[3].tipo == T_AVISO && vio[4].tipo == T_LISTO,
                  "los avisos de placa van como T_AVISO entre T_CATALOGO y T_LISTO");
        comprueba(forma && ca.nivel == N_AVISO && ca.origen_len == 5 && ca.t_sim_ns == 0 &&
                  vio[2].cuerpo.substr(sizeof ca) ==
                      "placanodo X: conducen a la vez A.a y B.a",
                  "con nivel aviso, origen 'placa', t = 0 y el texto entero");
    }

    // --- Las formas de fallar -----------------------------------------------
    struct Caso { const char* que; std::function<void(red::socket_t, Lector&, Emisor&)> guion;
                  const char* dice; int plazo; };
    const Caso casos[] = {
        { "la GUI no habla ninguna version: protocolo=0",
          [](red::socket_t c, Lector& L, Emisor& e) {
              recibe_gui(c, L, 1); manda_gui(c, e, T_VERSION, "protocolo=0\n");
              std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
          "ninguna version", 3000 },
        { "la GUI elige una version que no se le ofrecio",
          [](red::socket_t c, Lector& L, Emisor& e) {
              recibe_gui(c, L, 1); manda_gui(c, e, T_VERSION, "protocolo=2\n");
              std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
          "version 2", 3000 },
        { "la GUI contesta sin decir version",
          [](red::socket_t c, Lector& L, Emisor& e) {
              recibe_gui(c, L, 1); manda_gui(c, e, T_VERSION, "gui=muda\n");
              std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
          "protocolo=N", 3000 },
        { "la GUI no contesta a T_HOLA: se acaba el plazo",
          [](red::socket_t c, Lector& L, Emisor&) {
              recibe_gui(c, L, 1);
              std::this_thread::sleep_for(std::chrono::milliseconds(600)); },
          "no contesto", 300 },
        { "lo primero que manda la GUI no es T_VERSION",
          [](red::socket_t c, Lector& L, Emisor& e) {
              recibe_gui(c, L, 1); manda_gui(c, e, T_PING);
              std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
          "T_VERSION", 3000 },
        { "la GUI cierra antes de arrancar",
          [](red::socket_t c, Lector& L, Emisor& e) {
              recibe_gui(c, L, 1); manda_gui(c, e, T_VERSION, "protocolo=1\n");
              L.fija_version(1); recibe_gui(c, L, 3); },
          "cerro", 3000 },
        { "la GUI manda algo que no es el protocolo",
          [](red::socket_t c, Lector& L, Emisor&) {
              recibe_gui(c, L, 1); manda(c, "HTTP/1.1 400 Bad Request\r\n\r\n");
              std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
          "no es el protocolo", 3000 },
        { "T_ARRANCA con un cuerpo que no mide lo que debe",
          [](red::socket_t c, Lector& L, Emisor& e) {
              recibe_gui(c, L, 1); manda_gui(c, e, T_VERSION, "protocolo=1\n");
              L.fija_version(1); e.fija_version(1); recibe_gui(c, L, 3);
              manda_gui(c, e, T_ARRANCA, "corto");
              std::this_thread::sleep_for(std::chrono::milliseconds(100)); },
          "T_ARRANCA", 3000 },
    };
    for (const Caso& k : casos) {
        GuiFalsa g(k.guion);
        ClienteGui m;
        Arranca arr{};
        m.conecta(g.destino());
        const D d = m.saluda(HOLA, PLACA, CATALOGO, false, arr, k.plazo);
        comprueba(d == D::Error && m.error().find(k.dice) != std::string::npos,
                  std::string(k.que) + ": error, y lo dice (\"" + m.error() + "\")");
    }

    // --- Sin nadie escuchando ------------------------------------------------
    {
        red::socket_t s = red::escucha("127.0.0.1", 0);
        const unsigned p = red::puerto_local(s);
        red::cerrar(s);                              // y ya no hay nadie
        gui::Destino d;
        d.valido = true; d.host = "127.0.0.1"; d.puerto = uint16_t(p);
        ClienteGui m;
        comprueba(!m.conecta(d) && m.error().find("no hay nadie escuchando") != std::string::npos,
                  "sin GUI escuchando, conecta() falla y lo dice");
    }

    // --- El cuerpo de texto de T_VERSION -------------------------------------
    comprueba(ClienteGui::valor_entero("gui=0.1\nprotocolo=1\n", "protocolo") == 1 &&
              ClienteGui::valor_entero("protocolo=12\r\n", "protocolo") == 12 &&
              ClienteGui::valor_entero("protocolo_max=3\n", "protocolo") == -1 &&
              ClienteGui::valor_entero("protocolo=x\n", "protocolo") == -1 &&
              ClienteGui::valor_entero("", "protocolo") == -1,
              "clave=valor: la clave exacta, con o sin \\r, y nada que no sea un numero");

    // --- El cuerpo de T_ORDENES (fase 5) -------------------------------------
    {
        const std::vector<Orden> v{{1000000000ull, 3, 0, 1.f}, {500000000ull, 3, 0, 0.f}};
        std::vector<Orden> w;
        const std::string c = gui::cuerpo_ordenes(v);
        comprueba(c.size() == 32 && gui::lee_ordenes(c, w) && w.size() == 2 &&
                  w[0].t_sim_ns == 1000000000ull && w[0].pieza == 3 && w[1].valor == 0.f &&
                  w[1].t_sim_ns == 500000000ull,
                  "T_ORDENES: dos ordenes son 32 bytes y vuelven a ser las mismas");
        comprueba(!gui::lee_ordenes("", w) && w.empty() &&
                  !gui::lee_ordenes(c.substr(0, 31), w) && w.empty() &&
                  !gui::lee_ordenes(c + "x", w),
                  "un T_ORDENES vacio o que no es multiplo de 16 no se lee ni a medias");
    }
}

// ===========================================================================
// P3 — Las copias compartidas con mcu-sim-gui (R-6)
// ===========================================================================
bool lee_fichero(const std::string& ruta, std::string& d) {
    std::ifstream f(ruta, std::ios::binary);
    if (!f) return false;
    std::ostringstream o;
    o << f.rdbuf();
    d = o.str();
    return true;
}

void p3_copias(const std::string& gui, bool dada) {
    grupo("P3 Las copias compartidas con mcu-sim-gui (R-6)");
    std::string x;
    if (!lee_fichero(gui + "/src/protocolo.h", x)) {
        if (dada) {
            comprueba(false, "no encuentro mcu-sim-gui en " + gui);
        } else {
            std::printf("  [SALTA] no hay un mcu-sim-gui en %s; se comprueba con "
                        "GUI_REPO=ruta, y el CI lo hace\n", gui.c_str());
        }
        return;
    }
    for (const char* f : {"protocolo.h", "proto_io.h"}) {
        std::string aqui, alli;
        const bool a = lee_fichero(std::string("common/") + f, aqui);
        const bool b = lee_fichero(gui + "/src/" + f, alli);
        comprueba(a && b && aqui == alli,
                  std::string(f) + ": la copia de mcu-sim y la de mcu-sim-gui son "
                  "identicas byte a byte");
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string gui = "../../mcu-sim-gui";
    bool dada = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--gui-repo" && i + 1 < argc) { gui = argv[++i]; dada = true; }
    }
    if (!red::arranca()) { std::printf("no arranca la pila de red\n"); return 1; }
    p1_marco();
    p2_socket();
    p4_saludo();
    p3_copias(gui, dada);
    std::printf("RESULTADO %u ok, %u fallos\n", g_ok, g_mal);
    return g_mal ? 1 : 0;
}
