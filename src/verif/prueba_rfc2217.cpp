// =============================================================================
// prueba_rfc2217.cpp — El códec de Telnet y RFC 2217, sin SystemC
//
// Fase D4 del plan de `doc/analisis_puente_serie.md` §10 (P-14). Comprueba
// `common/telnet2217.h` contra el texto de las RFC 854/855/856/1143/2217 y
// contra una sesión de verdad entre el cliente y el servidor de pySerial,
// grabada en verif/vectores/rfc2217_pyserial.vec por captura_rfc2217.py.
//
//   make -f Makefile.mcu-sim rfc2217
//
// Y un modo más, que no es una prueba sino un banco de interoperabilidad:
//
//   ./build/prueba_rfc2217 --servidor 47356
//
// levanta un servidor RFC 2217 mínimo con este códec y este negociador -eco
// de los datos, y cada orden contestada con el valor que pide- para que un
// cliente de verdad (pySerial, com2tcp-rfc2217, HW VSP3...) pueda comprobar
// que se entiende con él. Atiende a un cliente y termina.
//
// Código de salida 0 si todo va bien.
// =============================================================================
#include "../common/telnet2217.h"
#include "../common/red.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace stm32::telnet;
namespace red = stm32::red;

static unsigned g_ok = 0, g_mal = 0;
static bool comprueba(bool c, const std::string& que) {
    (c ? g_ok : g_mal)++;
    std::printf("  [%s] %s\n", c ? "OK  " : "FALLO", que.c_str());
    return c;
}
static void grupo(const char* t) { std::printf("%s\n", t); }

static std::string bytes(std::initializer_list<int> l) {
    std::string s;
    for (int c : l) s += char(c);
    return s;
}
static std::string hex(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string r;
    for (unsigned char c : s) { r += d[c >> 4]; r += d[c & 15]; }
    return r;
}
static std::string de_hex(const std::string& h) {
    std::string r;
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        r += char(std::stoi(h.substr(i, 2), nullptr, 16));
    return r;
}

// Decodifica un flujo entero de una vez.
struct Salida {
    std::vector<uint8_t> datos;
    std::vector<Suceso>  sucesos;
    std::string texto() const { return std::string(datos.begin(), datos.end()); }
    bool operator==(const Salida& o) const { return datos == o.datos && sucesos == o.sucesos; }
};
static Salida decodifica(const std::string& s, bool binario, size_t trozo = 0) {
    Decodificador d;
    d.set_binario(binario);
    Salida o;
    if (!trozo) { d.alimenta(s, o.datos, o.sucesos); return o; }
    for (size_t i = 0; i < s.size(); i += trozo)
        d.alimenta(s.substr(i, trozo), o.datos, o.sucesos);
    return o;
}

// --- La captura de pySerial --------------------------------------------------
struct Tramo { bool del_cliente; std::string bytes; std::string paso; };
static std::vector<Tramo> lee_captura(const char* ruta) {
    std::vector<Tramo> v;
    std::ifstream f(ruta);
    std::string l, paso;
    while (std::getline(f, l)) {
        if (l.rfind("= paso ", 0) == 0) { paso = l.substr(7); continue; }
        if (l.size() > 3 && (l.rfind("c> ", 0) == 0 || l.rfind("s> ", 0) == 0))
            v.push_back({ l[0] == 'c', de_hex(l.substr(3)), paso });
    }
    return v;
}

// =============================================================================
// El servidor mínimo: la lógica, pura, y el socket, aparte
// =============================================================================
// Todo lo que decide el servidor está en `atiende()`, que recibe bytes y
// devuelve bytes. El modo `--servidor` solo le pone un socket delante, y la
// prueba 10 le da la sesión grabada y exige que conteste EXACTAMENTE lo que
// contestó: así la captura de pySerial contra este servidor es un vector de
// regresión, no una foto.
struct Servidor {
    Decodificador dec;
    Negociador    neg;
    uint32_t baud = 115200;
    uint8_t  datasize = 8, parity = 1, stopsize = 1, control_flujo = 1;
    unsigned n_cpo = 0;

    std::string atiende(const std::string& entrada) {
        std::vector<uint8_t> datos;
        std::vector<Suceso> ev;
        std::string out;
        // Byte a byte: el negociador puede cambiar el modo BINARY a mitad de un
        // trozo, y el decodificador tiene que enterarse antes del byte siguiente.
        // Y en ORDEN: el eco de un dato sale en su sitio, entre las respuestas,
        // no todo al final. Si entra de golpe una sesion entera, lo que sale
        // tiene que ser lo mismo que si hubiera entrado a trozos.
        for (char ch : entrada) {
            ev.clear();
            datos.clear();
            const uint8_t b = uint8_t(ch);
            dec.alimenta(&b, 1, datos, ev);
            escapa(datos.data(), datos.size(), neg.binario_salida(), out);
            for (const Suceso& s : ev) out += suceso(s);
        }
        return out;
    }

    std::string suceso(const Suceso& s) {
        if (s.tipo == Suceso::Tipo::negociacion) {
            const std::string r = neg.recibe(s.verbo, s.opcion);
            dec.set_binario(neg.binario_entrada());
            return r;
        }
        OrdenCpo o;
        if (!es_cpo(s, o) || !neg.com_port()) return std::string();
        ++n_cpo;
        std::vector<uint8_t> r;
        switch (o.cod) {
        case cpo::SIGNATURE: {
            const std::string f = "prueba_rfc2217 de mcu-sim";
            r.assign(f.begin(), f.end());
            break;
        }
        case cpo::SET_BAUDRATE: {
            uint32_t v = 0;
            if (lee_u32_red(o.valor, 0, v) && v) baud = v;
            r = u32_red(baud);
            break;
        }
        case cpo::SET_DATASIZE: if (!o.valor.empty() && o.valor[0]) datasize = o.valor[0];
                                r = { datasize }; break;
        case cpo::SET_PARITY:   if (!o.valor.empty() && o.valor[0]) parity = o.valor[0];
                                r = { parity }; break;
        case cpo::SET_STOPSIZE: if (!o.valor.empty() && o.valor[0]) stopsize = o.valor[0];
                                r = { stopsize }; break;
        case cpo::SET_CONTROL:
            if (!o.valor.empty() && o.valor[0] >= 1 && o.valor[0] <= 3)
                control_flujo = o.valor[0];
            r = { o.valor.empty() || o.valor[0] == 0 ? control_flujo : o.valor[0] };
            break;
        default:
            r = o.valor;
            break;
        }
        return orden_2217(uint8_t(o.cod + cpo::RESPUESTA), r);
    }
};

static int servidor(unsigned puerto) {
    red::socket_t srv = red::escucha_local(puerto);
    if (!red::valido(srv)) {
        std::fprintf(stderr, "no se puede escuchar en localhost:%u\n", puerto);
        return 1;
    }
    std::printf("servidor RFC 2217 minimo en localhost:%u (un cliente)\n", puerto);
    std::fflush(stdout);
    red::socket_t c = red::invalido();
    while (!red::valido(c)) {
        c = red::acepta(srv);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Servidor sv;
    char b[512];
    for (;;) {
        const long n = red::recibir(c, b, sizeof b);
        if (n == 0 || (n < 0 && !red::reintentar())) break;
        if (n < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        const std::string out = sv.atiende(std::string(b, size_t(n)));
        size_t hecho = 0;
        while (hecho < out.size()) {
            const long k = red::enviar(c, out.data() + hecho, out.size() - hecho);
            if (k > 0) hecho += size_t(k);
            else if (k < 0 && red::reintentar()) continue;
            else break;
        }
    }
    std::printf("cliente fuera: com_port=%d binario=%d/%d, %u ordenes RFC 2217, "
                "baudios %u, %u bits, paridad %u, parada %u\n",
                sv.neg.com_port(), sv.neg.binario_entrada(), sv.neg.binario_salida(),
                sv.n_cpo, sv.baud, sv.datasize, sv.parity, sv.stopsize);
    red::cerrar(c);
    red::cerrar(srv);
    return 0;
}

// =============================================================================
// Las pruebas
// =============================================================================
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--servidor")
        return servidor(unsigned(std::atoi(argv[2])));

    // --- 1. Datos y el IAC doblado ------------------------------------------
    grupo("1. Los datos, y el 0xFF doblado");
    comprueba(decodifica("Hola", true).texto() == "Hola", "los datos pasan tal cual");
    comprueba(decodifica(bytes({'a', 255, 255, 'b'}), true).texto() == bytes({'a', 255, 'b'}),
              "IAC IAC es un 0xFF de datos");
    comprueba(decodifica(bytes({255, 255, 255, 255}), true).texto() == bytes({255, 255}),
              "y dos seguidos son dos");
    comprueba(decodifica(std::string(1, '\0'), true).texto() == std::string(1, '\0'),
              "un 0x00 es un dato");

    // --- 2. NVT: el CR NUL ----------------------------------------------------
    grupo("2. Sin BINARY, el CR NUL del terminal virtual");
    comprueba(decodifica(bytes({'\r', 0, 'x'}), false).texto() == "\rx",
              "sin BINARY, CR NUL es un CR: el NUL es relleno");
    comprueba(decodifica("\r\n", false).texto() == "\r\n", "CR LF es CR LF");
    comprueba(decodifica(bytes({'\r', '\r', 0}), false).texto() == "\r\r",
              "CR CR NUL son dos CR");
    comprueba(decodifica(bytes({'\r', 0, 'x'}), true).texto() == bytes({'\r', 0, 'x'}),
              "con BINARY, el NUL es un dato");
    comprueba(decodifica(bytes({'\r', 255, 255}), false).texto() == bytes({'\r', 255}),
              "un IAC IAC justo detras de un CR sigue siendo un 0xFF");

    // --- 3. Negociaciones y órdenes sueltas -----------------------------------
    grupo("3. Negociaciones y ordenes sueltas");
    {
        const Salida o = decodifica(bytes({255, WILL, 44, 'a', 255, DO, 0}), true);
        comprueba(o.sucesos.size() == 2 && o.sucesos[0].verbo == WILL &&
                  o.sucesos[0].opcion == 44 && o.sucesos[1].verbo == DO &&
                  o.sucesos[1].opcion == 0,
                  "IAC WILL 44 e IAC DO 0 son dos negociaciones");
        comprueba(o.texto() == "a", "y lo de en medio, un dato");
    }
    {
        const Salida o = decodifica(bytes({'x', 255, NOP, 'y', 255, AYT, 255, SE, 'z'}), true);
        comprueba(o.texto() == "xyz", "NOP, AYT y un SE suelto no se meten en los datos");
        comprueba(o.sucesos.size() == 2 && o.sucesos[0].verbo == NOP &&
                  o.sucesos[1].verbo == AYT,
                  "NOP y AYT salen como ordenes; el SE suelto se ignora");
    }

    // --- 4. Subopciones ---------------------------------------------------------
    grupo("4. Subopciones, con el IAC doblado dentro");
    {
        const Salida o = decodifica(bytes({255, SB, 44, 1, 0, 1, 0xC2, 0, 255, SE}), true);
        OrdenCpo c;
        uint32_t v = 0;
        comprueba(o.sucesos.size() == 1 && es_cpo(o.sucesos[0], c) &&
                  c.cod == cpo::SET_BAUDRATE && lee_u32_red(c.valor, 0, v) && v == 115200,
                  "SET-BAUDRATE 115200, en cuatro bytes en orden de red");
    }
    {
        // 65535 baudios: 00 00 FF FF, que dentro de un SB va como 00 00 FF FF FF FF
        const std::string s = orden_2217(cpo::SET_BAUDRATE, u32_red(65535));
        comprueba(hex(s) == "fffa2c010000ffffffff" "fff0",
                  "un valor con 0xFF dentro se codifica con el IAC doblado");
        const Salida o = decodifica(s, true);
        OrdenCpo c;
        uint32_t v = 0;
        comprueba(o.sucesos.size() == 1 && es_cpo(o.sucesos[0], c) &&
                  lee_u32_red(c.valor, 0, v) && v == 65535,
                  "y se decodifica de vuelta a 65535");
    }
    {
        OrdenCpo c;
        Suceso s; s.tipo = Suceso::Tipo::subopcion; s.opcion = 24; s.datos = {1};
        comprueba(!es_cpo(s, c), "una subopcion de otra opcion no es una orden COM-PORT");
        s.opcion = 44; s.datos.clear();
        comprueba(!es_cpo(s, c), "ni una COM-PORT vacia");
        uint32_t v;
        comprueba(!lee_u32_red({1, 2, 3}, 0, v), "tres bytes no son unos baudios");
    }

    // --- 5. Llega troceado ---------------------------------------------------------
    grupo("5. El flujo llega partido en cualquier byte");
    {
        const std::string s = bytes({'a', 255, 255, 'b', 255, WILL, 44, 255, SB, 44, 1,
                                     0, 0, 255, 255, 255, 255, 255, SE, '\r', 0, 'c',
                                     255, NOP, 'd'});
        const Salida entero = decodifica(s, false);
        bool todos = true;
        for (size_t corte = 1; corte < s.size(); ++corte) {
            Decodificador d;
            Salida o;
            d.alimenta(s.substr(0, corte), o.datos, o.sucesos);
            d.alimenta(s.substr(corte), o.datos, o.sucesos);
            if (!(o == entero)) todos = false;
        }
        comprueba(todos, "cortado en cada uno de sus " + std::to_string(s.size() - 1) +
                         " puntos, sale lo mismo que entero");
        comprueba(decodifica(s, false, 1) == entero, "y byte a byte, tambien");
        comprueba(entero.texto() == bytes({'a', 255, 'b', '\r', 'c', 'd'}) &&
                  entero.sucesos.size() == 3,
                  "y lo que sale es lo que tiene que salir");
    }

    // --- 6. Clientes descuidados ---------------------------------------------------
    grupo("6. Clientes descuidados: nada desincroniza");
    {
        Decodificador d;
        Salida o;
        // Un SB al que le falta el SE, seguido de un NOP y de datos.
        d.alimenta(bytes({255, SB, 44, 5, 1, 255, NOP, 'o', 'k'}), o.datos, o.sucesos);
        comprueba(o.sucesos.size() == 2 && o.sucesos[0].tipo == Suceso::Tipo::subopcion &&
                  o.sucesos[0].datos == std::vector<uint8_t>({5, 1}) &&
                  o.sucesos[1].verbo == NOP,
                  "un SB sin SE se da por cerrado en el siguiente IAC, y el NOP se ve");
        comprueba(o.texto() == "ok" && d.subopciones_mal_cerradas() == 1,
                  "los datos de detras llegan, y el descuido se cuenta");
    }
    {
        Decodificador d;
        Salida o;
        std::string s = bytes({255, SB, 44, 0});
        s += std::string(1000, 'A');
        s += bytes({255, SE, 'z'});
        d.alimenta(s, o.datos, o.sucesos);
        comprueba(o.sucesos.size() == 1 && o.sucesos[0].datos.size() == Decodificador::MAX_SB &&
                  d.subopciones_cortadas() == 1 && o.texto() == "z",
                  "una subopcion de mil bytes se corta en 256, se cuenta y no se "
                  "come lo de detras");
    }

    // --- 7. El codificador ---------------------------------------------------------
    grupo("7. Lo que manda el servidor");
    comprueba(escapa(bytes({'a', 255, 'b'}), true) == bytes({'a', 255, 255, 'b'}),
              "el 0xFF de datos sale doblado");
    comprueba(escapa("\r\n", false) == bytes({'\r', 0, '\n'}),
              "sin BINARY, el CR lleva su NUL");
    comprueba(escapa("\r\n", true) == "\r\n", "con BINARY, no");
    comprueba(negociacion(DO, 44) == bytes({255, DO, 44}), "IAC DO 44");
    {
        bool ida_vuelta = true;
        std::string todo;
        for (int v = 0; v < 256; ++v) todo += char(v);
        for (bool bin : { false, true })
            if (decodifica(escapa(todo, bin), bin).texto() != todo) ida_vuelta = false;
        comprueba(ida_vuelta, "los 256 valores, codificados y decodificados, vuelven "
                              "iguales, con BINARY y sin el");
    }

    // --- 8. El negociador ----------------------------------------------------------
    grupo("8. La politica del servidor");
    {
        Negociador n;
        comprueba(!n.despierto() && !n.com_port(),
                  "al conectarse no ha pasado nada: el servidor es pasivo");
        const std::string r = n.recibe(WILL, 44);
        comprueba(r == negociacion(DO, 44) + negociacion(DO, 0) + negociacion(WILL, 0),
                  "WILL COM-PORT: DO COM-PORT, y ahora si pide BINARY en los dos "
                  "sentidos (" + hex(r) + ")");
        comprueba(n.com_port(), "el cliente habla RFC 2217");
        comprueba(n.recibe(WILL, 44).empty(), "un WILL COM-PORT repetido no tiene respuesta");
        comprueba(n.recibe(WILL, 0).empty() && n.binario_entrada(),
                  "WILL BINARY contesta a nuestro DO: silencio, y la entrada es binaria");
        comprueba(n.recibe(DO, 0).empty() && n.binario_salida(),
                  "DO BINARY contesta a nuestro WILL: silencio, y la salida es binaria");
        comprueba(n.recibe(DO, 1) == negociacion(WONT, 1),
                  "DO ECHO: WONT. El eco lo hace el firmware, si lo hace");
        comprueba(n.recibe(DO, 3) == negociacion(WILL, 3), "DO SGA: WILL SGA");
        comprueba(n.recibe(WILL, 3) == negociacion(DO, 3), "WILL SGA: DO SGA");
        comprueba(n.recibe(WILL, 24) == negociacion(DONT, 24),
                  "WILL de una opcion que no se conoce (TERMINAL-TYPE): DONT");
        comprueba(n.recibe(DONT, 1).empty(), "DONT de algo que no se hace: silencio");
        comprueba(n.recibe(WONT, 44) == negociacion(DONT, 44) && !n.com_port(),
                  "WONT COM-PORT: DONT, y se acaba el RFC 2217");
    }
    {
        Negociador n;
        comprueba(n.recibe(WONT, 0) == negociacion(DO, 0) + negociacion(WILL, 0),
                  "cualquier negociacion despierta al servidor, aunque sea un WONT");
        comprueba(n.recibe(WONT, 0).empty() && !n.binario_entrada(),
                  "WONT BINARY contesta a nuestro DO: el cliente no quiere, y no se insiste");
        comprueba(n.recibe(DONT, 0).empty() && !n.binario_salida(),
                  "DONT BINARY contesta a nuestro WILL: tampoco");
    }

    // --- 9. La sesión de pySerial ---------------------------------------------------
    grupo("9. Una sesion de verdad: la de pySerial (rfc2217_pyserial.vec)");
    const std::vector<Tramo> cap = lee_captura("verif/vectores/rfc2217_pyserial.vec");
    comprueba(cap.size() > 50, "la captura se lee: " + std::to_string(cap.size()) + " tramos");
    {
        // Lo que manda el CLIENTE, como lo veria nuestro servidor, que decide
        // BINARY con su negociador y se lo dice al decodificador.
        Decodificador d;
        Negociador n;
        Salida o;
        std::vector<OrdenCpo> ordenes;
        std::string respuestas;
        for (const Tramo& t : cap) {
            if (!t.del_cliente) continue;
            std::vector<Suceso> ev;
            d.alimenta(t.bytes, o.datos, ev);
            for (const Suceso& s : ev) {
                if (s.tipo == Suceso::Tipo::negociacion) {
                    respuestas += n.recibe(s.verbo, s.opcion);
                    d.set_binario(n.binario_entrada());
                }
                OrdenCpo c;
                if (es_cpo(s, c)) ordenes.push_back(c);
            }
        }
        comprueba(o.texto() == bytes({'H', 'o', 'l', 'a', '\r', 255, 0, 'f', 'i', 'n'}),
                  "los unicos datos son los que el cliente mando, con el 0xFF y el "
                  "NUL detras del CR intactos");
        comprueba(n.com_port() && n.binario_entrada(),
                  "con nuestro negociador, el cliente queda en RFC 2217 y en BINARY");
        std::string cods;
        for (const OrdenCpo& c : ordenes) cods += std::to_string(c.cod) + " ";
        comprueba(cods == "1 2 3 4 5 5 5 12 12 1 2 3 4 5 1 2 3 4 5 1 2 3 4 5 1 2 3 4 5 "
                          "5 5 5 5 12 12 ",
                  "las 35 ordenes, en su orden: " + cods);
        // Si no han llegado las 35, lo de abajo no se puede mirar: se dice y
        // se sigue, en vez de leer fuera de la lista.
        if (ordenes.size() != 35) ordenes.resize(35);
        for (OrdenCpo& c : ordenes) if (c.valor.empty()) c.valor.push_back(0xEE);
        uint32_t v0 = 0, v1 = 0;
        comprueba(lee_u32_red(ordenes[0].valor, 0, v0) && v0 == 115200 &&
                  lee_u32_red(ordenes[9].valor, 0, v1) && v1 == 9600,
                  "abre a 115200 y cambia a 9600");
        comprueba(ordenes[15].valor == std::vector<uint8_t>({7}) &&
                  ordenes[21].valor == std::vector<uint8_t>({3}) &&
                  ordenes[27].valor == std::vector<uint8_t>({2}),
                  "7 bits, paridad par (3) y dos de parada (2)");
        comprueba(ordenes[29].valor[0] == 12 && ordenes[30].valor[0] == 9 &&
                  ordenes[31].valor[0] == 5 && ordenes[32].valor[0] == 6,
                  "RTS a 0 (12), DTR a 0 (9), break ON (5) y OFF (6)");
        comprueba(ordenes[33].valor[0] == 1 && ordenes[34].valor[0] == 2,
                  "y purga la recepcion (1) y la transmision (2)");
    }
    {
        // Lo que manda el SERVIDOR de pySerial, visto desde el cliente: sus
        // respuestas tienen que ser exactamente lo que nuestro codificador
        // compone para las mismas ordenes.
        std::string todo_s;
        for (const Tramo& t : cap) if (!t.del_cliente) todo_s += t.bytes;
        comprueba(todo_s.find(orden_2217(101, u32_red(115200))) != std::string::npos,
                  "la respuesta a SET-BAUDRATE 115200 es byte a byte la que compone "
                  "este codificador: " + hex(orden_2217(101, u32_red(115200))));
        comprueba(todo_s.find(orden_2217(101, u32_red(9600))) != std::string::npos &&
                  todo_s.find(orden_2217(cpo::SET_DATASIZE + cpo::RESPUESTA, {7})) != std::string::npos &&
                  todo_s.find(orden_2217(cpo::SET_PARITY + cpo::RESPUESTA, {3})) != std::string::npos,
                  "y las de 9600, 7 bits y paridad par, tambien");
        Decodificador d;
        d.set_binario(true);
        Salida o;
        d.alimenta(todo_s, o.datos, o.sucesos);
        unsigned modem = 0;
        for (const Suceso& s : o.sucesos) {
            OrdenCpo c;
            if (es_cpo(s, c) && c.cod == cpo::NOTIFY_MODEMSTATE + cpo::RESPUESTA) ++modem;
        }
        comprueba(o.texto() == bytes({'H', 'o', 'l', 'a', '\r', 255, 0, 'f', 'i', 'n'}),
                  "el eco del servidor se decodifica igual");
        comprueba(modem == 4,
                  "y trae cuatro NOTIFY-MODEMSTATE que nadie pidio: el servidor "
                  "avisa de las lineas por su cuenta, como manda la RFC");
    }

    // --- 10. pySerial contra este codigo ------------------------------------------
    grupo("10. pySerial contra este servidor (rfc2217_pyserial_mcusim.vec)");
    {
        const std::vector<Tramo> v =
            lee_captura("verif/vectores/rfc2217_pyserial_mcusim.vec");
        comprueba(v.size() > 30, "la captura se lee: " + std::to_string(v.size()) + " tramos");
        std::string de_cliente, de_servidor;
        for (const Tramo& t : v) (t.del_cliente ? de_cliente : de_servidor) += t.bytes;
        // El servidor es PASIVO: el primer tramo de la sesion es del cliente.
        comprueba(!v.empty() && v.front().del_cliente,
                  "el primero en hablar es el cliente: el servidor no manda nada al "
                  "conectarse");
        // Rehacer la sesion: lo que manda el cliente, por el mismo Servidor.
        // Se le da en los mismos trozos en que llego, por si el troceado
        // importara -y no debe-.
        Servidor sv;
        std::string contesta;
        for (const Tramo& t : v) if (t.del_cliente) contesta += sv.atiende(t.bytes);
        comprueba(contesta == de_servidor,
                  "con lo que mando el cliente, el servidor contesta EXACTAMENTE lo "
                  "grabado: " + std::to_string(de_servidor.size()) + " bytes");
        Servidor sv2;
        comprueba(sv2.atiende(de_cliente) == de_servidor,
                  "y de un solo trozo, lo mismo");
        comprueba(sv.neg.com_port() && sv.neg.binario_entrada() && sv.neg.binario_salida(),
                  "el cliente de pySerial acaba en RFC 2217 y en BINARY en los dos "
                  "sentidos: acepta el BINARY que se le pide");
        comprueba(sv.n_cpo == 35 && sv.baud == 9600 && sv.datasize == 7 &&
                  sv.parity == 3 && sv.stopsize == 2,
                  "las 35 ordenes, y la linea queda en 9600 7E2");
        comprueba(de_servidor.find(bytes({'H', 'o', 'l', 'a', '\r', 255, 255, 0,
                                          'f', 'i', 'n'})) != std::string::npos,
                  "y el eco lleva el 0xFF doblado y el NUL detras del CR");
    }

    // --- 11. El negociador del cliente (fase D8) ------------------------------
    grupo("11. El negociador del CLIENTE, contra el del servidor");
    {
        // Se hablan el uno al otro hasta que ninguno tenga nada que decir, con
        // un tope de vueltas: si hubiera un bucle de negociacion, no pararia.
        using namespace stm32::telnet;
        NegociadorCliente cli;
        Negociador srv;
        Decodificador dc, ds;
        std::string a_srv = cli.inicio(), a_cli;
        comprueba(a_srv == negociacion(WILL, OPT_BINARY) + negociacion(DO, OPT_BINARY) +
                           negociacion(WILL, OPT_SGA) + negociacion(DO, OPT_SGA) +
                           negociacion(WILL, OPT_COM_PORT),
                  "al conectarse ofrece BINARY, SGA y COM-PORT, y pide BINARY y SGA");
        int vueltas = 0;
        while ((!a_srv.empty() || !a_cli.empty()) && vueltas < 20) {
            ++vueltas;
            std::vector<uint8_t> d; std::vector<Suceso> ev;
            ds.alimenta(a_srv, d, ev);
            a_srv.clear();
            for (const Suceso& e : ev) a_cli += srv.recibe(e.verbo, e.opcion);
            d.clear(); ev.clear();
            dc.alimenta(a_cli, d, ev);
            a_cli.clear();
            for (const Suceso& e : ev) a_srv += cli.recibe(e.verbo, e.opcion);
        }
        comprueba(vueltas < 20, "la negociacion termina (" + std::to_string(vueltas) +
                  " vueltas): no hay bucle");
        comprueba(cli.com_port() && srv.com_port(), "COM-PORT, aceptado por los dos");
        comprueba(cli.binario_salida() && srv.binario_entrada() &&
                  cli.binario_entrada() && srv.binario_salida(),
                  "BINARY en los dos sentidos, y los dos lo saben");
        comprueba(cli.ellos(OPT_SGA) && cli.nosotros(OPT_SGA), "SGA en los dos sentidos");
    }
    {
        // Contra un servidor que ofrece de mas, como el de pySerial: WILL ECHO
        // se rechaza, y lo ya pedido no se vuelve a contestar.
        using namespace stm32::telnet;
        NegociadorCliente cli;
        cli.inicio();
        comprueba(cli.recibe(WILL, OPT_ECHO) == negociacion(DONT, OPT_ECHO),
                  "WILL ECHO del servidor: DONT ECHO (el eco lo hace el firmware)");
        comprueba(cli.recibe(DO, OPT_COM_PORT).empty() && cli.com_port(),
                  "DO COM-PORT a lo que se ofrecio: se toma, sin contestar");
        comprueba(cli.recibe(DO, OPT_COM_PORT).empty(), "repetido: silencio");
        comprueba(cli.recibe(DO, 24) == negociacion(WONT, 24),
                  "DO TERMINAL-TYPE: WONT");
        comprueba(cli.recibe(DONT, OPT_COM_PORT) == negociacion(WONT, OPT_COM_PORT) &&
                  !cli.com_port(), "DONT COM-PORT lo apaga, y se confirma");
    }

    std::printf("RESULTADO %u ok, %u fallos\n", g_ok, g_mal);
    return g_mal ? 1 : 0;
}
