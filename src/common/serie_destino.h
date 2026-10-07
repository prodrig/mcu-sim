// =============================================================================
// serie_destino.h — A dónde da un `PuenteSerie`, y nada más
//
// Fase D0 del plan de `doc/analisis_puente_serie.md` §10: que el simulador
// RECONOZCA el destino de un puente UART -en el atributo `host=` de la pieza y
// en la opción `--serie ID=DESTINO`- y sepa decir si choca con algo. Ni socket,
// ni pieza, ni un picosegundo de tiempo simulado.
//
// POR QUÉ ESTO ES UN FICHERO Y NO UNAS LÍNEAS EN `sim_main.cpp`. Por lo mismo
// que `gui_destino.h`: el parseo es una función PURA, y así se puede probar sin
// ejecutar el programa entero. Lo prueba `make serie` (verif/prueba_serie.cpp),
// que no necesita SystemC y corre en el trabajo rápido del CI en segundos.
//
// LOS MODOS, y son todos los de la arquitectura D:
//
//     memoria            una cola en proceso, para las pruebas. Sin red
//     tcp:PUERTO         TCP en crudo: los bytes del socket son los de la línea
//     rfc2217:PUERTO     Telnet con la opción 44 (RFC 2217): los datos, más
//                        baudios, formato, líneas de módem y break
//
// Y sus dos CLIENTES (fase D8), para cuando al otro lado hay un programa que
// solo sabe ser servidor -ser2tcp, tio, `rfc2217_server.py` de pySerial, un
// ser2net con un puerto de verdad-:
//
//     tcp-cliente:HOST:PUERTO       TCP en crudo, conectándose a HOST
//     rfc2217-cliente:HOST:PUERTO   RFC 2217 del lado del terminal: el puente
//                                   CONFIGURA el puerto remoto con sus baudios
//                                   y su formato
//
// Un cliente no escucha, así que no choca con nadie por puerto; y se conecta
// a donde se le diga, porque es él quien sale (D-7 va de quién puede ENTRAR).
//
// Y lo que NO es un destino, dicho en el error en vez de callado:
//
//   * un puerto serie del sistema (`COM7`, `/dev/ttyUSB0`, `pty`). En la
//     arquitectura D `mcu-sim` no abre ninguno: lo pone una herramienta externa
//     conectada por TCP (§7 del análisis). Quien escribe `COM7` aquí no se ha
//     equivocado de sintaxis sino de idea, y el mensaje se lo dice;
//   * un host en un modo servidor. El puente escucha SIEMPRE en esta máquina,
//     como los stubs de GDB (decisión D-7). `tcp:localhost:3355` se rechaza
//     nombrando el motivo, y diciendo que si lo que se quería era CONECTARSE a
//     ese host, eso es `tcp-cliente:`.
//
// EL PUERTO POR OMISIÓN ES EL 3355, y no el 5000 de los ejemplos del análisis.
// Sigue la serie del proyecto -3333 los dos GDB, 3344 la GUI- y esquiva el 5000,
// que en macOS desde Monterey es del receptor de AirPlay: el alumno de Mac se
// encontraría el puerto ocupado sin haber abierto nada.
// =============================================================================
#ifndef STM32_COMMON_SERIE_DESTINO_H
#define STM32_COMMON_SERIE_DESTINO_H

#include <cctype>
#include <string>
#include <vector>

namespace stm32 {
namespace serie {

// El vecino de 3333 (GDB) y 3344 (GUI).
inline constexpr unsigned PUERTO_OMISION = 3355;

enum class Modo { memoria, tcp, rfc2217, tcp_cliente, rfc2217_cliente };

inline const char* nombre_modo(Modo m) {
    switch (m) {
    case Modo::memoria:         return "memoria";
    case Modo::tcp:             return "tcp";
    case Modo::rfc2217:         return "rfc2217";
    case Modo::tcp_cliente:     return "tcp-cliente";
    case Modo::rfc2217_cliente: return "rfc2217-cliente";
    }
    return "?";
}

// ¿Este modo ESCUCHA en un puerto de esta máquina? Es lo que puede chocar con
// otro puente, con un GDB o con la GUI. Ni `memoria` ni los clientes.
inline bool usa_puerto(Modo m) { return m == Modo::tcp || m == Modo::rfc2217; }
// ¿Se conecta él a otro?
inline bool es_cliente(Modo m) { return m == Modo::tcp_cliente || m == Modo::rfc2217_cliente; }
// ¿Va por la red, en un sentido u otro?
inline bool por_red(Modo m) { return m != Modo::memoria; }

struct Destino {
    Modo        modo   = Modo::rfc2217;
    unsigned    puerto = PUERTO_OMISION;   // sin sentido en `memoria`
    std::string host;                      // solo en los clientes
    bool        valido = true;
    std::string error;                     // vacío si `valido`
};

// Lo que una pieza tiene cuando su XML no dice nada: RFC 2217 en el 3355, que
// es lo que entiende la mayoría de los redirectores del §7.3 y también un
// cliente en crudo que no negocie nada (el servidor es pasivo, decisión D-2).
inline Destino por_omision() { return Destino{}; }

namespace detalle {

inline bool solo_digitos(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// 1..65535. El 0 NO vale: «dame cualquiera» obligaría a leer el puerto después
// para decírselo al alumno, y un puerto que cambia en cada ejecución no se puede
// escribir en la configuración de su terminal.
inline bool puerto_valido(const std::string& s, unsigned& out) {
    if (!solo_digitos(s) || s.size() > 5) return false;
    const unsigned long v = std::stoul(s);
    if (v == 0 || v > 65535) return false;
    out = static_cast<unsigned>(v);
    return true;
}

inline std::string minus(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Un nombre de máquina o una IPv4: letras, cifras, puntos y guiones. Nada de
// IPv6 literal, que lleva `:` y chocaría con el separador del puerto -y el
// resto del proyecto tampoco habla IPv6-.
inline bool host_valido(const std::string& h) {
    if (h.empty() || h.size() > 253) return false;
    for (char c : h) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '.' || c == '-')) return false;
    }
    return h.front() != '-' && h.front() != '.';
}

inline Destino malo(const std::string& porque) {
    Destino d;
    d.valido = false;
    d.error  = porque;
    return d;
}

// ¿Tiene pinta de puerto serie del sistema? Se mira la forma, no si existe:
// esto no toca el sistema de ficheros.
inline bool parece_puerto_del_sistema(const std::string& s) {
    const std::string m = minus(s);
    if (m == "pty" || m == "tty") return true;
    if (m.rfind("/dev/", 0) == 0) return true;
    if (m.rfind("\\\\.\\", 0) == 0 || m.rfind("//./", 0) == 0) return true;
    if (m.size() > 3 && m.compare(0, 3, "com") == 0 && solo_digitos(m.substr(3)))
        return true;
    return false;
}

} // namespace detalle

// Un destino escrito como texto: `memoria`, `tcp:PUERTO` o `rfc2217:PUERTO`.
//
// Los nombres de modo DISTINGUEN MAYÚSCULAS, como los tipos de componente: esto
// describe una placa, y lo que describe una placa se escribe de una manera.
// Pero el error dice cómo se escribe, que es lo que uno quiere leer.
inline Destino parsea(const std::string& s) {
    using detalle::malo;
    static const char* const LOS_QUE_HAY =
        "memoria, tcp:PUERTO, rfc2217:PUERTO, tcp-cliente:HOST:PUERTO o "
        "rfc2217-cliente:HOST:PUERTO";

    if (s.empty())
        return malo(std::string("falta el destino: ") + LOS_QUE_HAY);

    if (detalle::parece_puerto_del_sistema(s))
        return malo("'" + s + "' es un puerto serie del sistema, y mcu-sim no "
                    "abre ninguno: lo pone una herramienta externa conectada "
                    "por TCP (doc/analisis_puente_serie.md, 7). Aqui va "
                    "tcp:PUERTO o rfc2217:PUERTO (o, si esa herramienta hace "
                    "de servidor, tcp-cliente:HOST:PUERTO)");

    const std::string::size_type dp = s.find(':');
    const std::string modo  = s.substr(0, dp);
    const bool        hay_p = dp != std::string::npos;
    const std::string resto = hay_p ? s.substr(dp + 1) : std::string();

    Destino d;
    if (modo == "memoria") {
        if (hay_p) return malo("'memoria' no lleva puerto: no abre nada");
        d.modo = Modo::memoria;
        d.puerto = 0;
        return d;
    }
    if (modo == "tcp")                  d.modo = Modo::tcp;
    else if (modo == "rfc2217")         d.modo = Modo::rfc2217;
    else if (modo == "tcp-cliente")     d.modo = Modo::tcp_cliente;
    else if (modo == "rfc2217-cliente") d.modo = Modo::rfc2217_cliente;
    else {
        const std::string m = detalle::minus(modo);
        if (m == "memoria" || m == "tcp" || m == "rfc2217" ||
            m == "tcp-cliente" || m == "rfc2217-cliente")
            return malo("se escribe '" + m + "': el modo distingue mayusculas");
        return malo("modo desconocido '" + modo + "': los que hay son " +
                    LOS_QUE_HAY);
    }

    if (es_cliente(d.modo)) {
        // HOST:PUERTO, los dos obligatorios: un cliente sin a dónde ir no es
        // nada, y suponer `localhost` escondería el error de quien se lo dejó.
        const std::string::size_type dp2 = resto.rfind(':');
        if (!hay_p || resto.empty() || dp2 == std::string::npos)
            return malo("falta el host o el puerto: se escribe " + modo +
                        ":HOST:PUERTO, como en " + modo + ":127.0.0.1:3355");
        const std::string host = resto.substr(0, dp2);
        const std::string pto  = resto.substr(dp2 + 1);
        if (host.find(':') != std::string::npos)
            return malo("'" + host + "' no es un host: IPv6 no esta soportado; "
                        "un nombre o una IPv4, como 127.0.0.1");
        if (!detalle::host_valido(host))
            return malo("'" + host + "' no es un host: un nombre o una IPv4, "
                        "como 127.0.0.1");
        if (!detalle::puerto_valido(pto, d.puerto))
            return malo("'" + pto + "' no es un puerto (1..65535)");
        d.host = host;
        return d;
    }

    if (!hay_p || resto.empty())
        return malo("falta el puerto: se escribe " + modo + ":PUERTO, como en " +
                    modo + ":" + std::to_string(PUERTO_OMISION));
    if (resto.find(':') != std::string::npos)
        return malo("'" + s + "' lleva un host, y el puente no lo admite: "
                    "escucha siempre en esta maquina, como los stubs de GDB. "
                    "Se escribe " + modo + ":PUERTO; y si lo que quieres es "
                    "conectarte a ese host, " + modo + "-cliente:HOST:PUERTO");
    if (!detalle::puerto_valido(resto, d.puerto))
        return malo("'" + resto + "' no es un puerto (1..65535)");
    return d;
}

// Cómo se escribe de vuelta, en la misma forma en que se lee.
inline std::string como_texto(const Destino& d) {
    if (d.modo == Modo::memoria) return "memoria";
    if (es_cliente(d.modo))
        return std::string(nombre_modo(d.modo)) + ":" + d.host + ":" +
               std::to_string(d.puerto);
    return std::string(nombre_modo(d.modo)) + ":" + std::to_string(d.puerto);
}

// Y cómo se le cuenta a una persona.
inline std::string describe(const Destino& d) {
    switch (d.modo) {
    case Modo::memoria: return "en memoria (sin red)";
    case Modo::tcp:     return "TCP en crudo en localhost:" + std::to_string(d.puerto);
    case Modo::rfc2217: return "RFC 2217 en localhost:" + std::to_string(d.puerto);
    case Modo::tcp_cliente:
        return "TCP en crudo hacia " + d.host + ":" + std::to_string(d.puerto) + " (cliente)";
    case Modo::rfc2217_cliente:
        return "RFC 2217 hacia " + d.host + ":" + std::to_string(d.puerto) + " (cliente)";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// `--serie ID=DESTINO`
// ---------------------------------------------------------------------------
struct Asignacion {
    std::string id;
    Destino     destino;
    bool        valido = true;
    std::string error;
};

// Los caracteres de un identificador de componente. No se valida contra la
// placa aquí -eso es `resuelve`-: solo que la cadena pueda ser uno. En un
// <sistema>, el de la pieza lleva delante su placa y una barra, `N/VCP`: una
// barra, y no al principio ni al final.
inline bool id_valido(const std::string& id) {
    if (id.empty() || id.front() == '/' || id.back() == '/') return false;
    unsigned barras = 0;
    for (char c : id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '/') { ++barras; continue; }
        if (!(std::isalnum(u) || c == '_' || c == '-' || c == '.')) return false;
    }
    return barras <= 1;
}

inline Asignacion parsea_asignacion(const std::string& s) {
    Asignacion a;
    auto mala = [&](const std::string& porque) {
        a.valido = false;
        a.error  = porque;
        return a;
    };
    const std::string::size_type ig = s.find('=');
    if (ig == std::string::npos)
        return mala("falta '=': se escribe ID=DESTINO, como en VCP=rfc2217:" +
                    std::to_string(PUERTO_OMISION));
    a.id = s.substr(0, ig);
    if (a.id.empty()) return mala("falta el identificador del puente antes de '='");
    if (!id_valido(a.id))
        return mala("'" + a.id + "' no es un identificador de componente");
    a.destino = parsea(s.substr(ig + 1));
    if (!a.destino.valido) return mala(a.id + ": " + a.destino.error);
    return a;
}

// ---------------------------------------------------------------------------
// La placa, la línea de órdenes y los puertos que ya están cogidos
// ---------------------------------------------------------------------------

// Un `PuenteSerie` de la placa, con el destino que dice su XML.
struct Pieza {
    std::string id;
    Destino     destino;
};

// Un puerto que otra parte del programa ya va a usar: un stub de GDB, la GUI.
struct Ocupado {
    unsigned    puerto;
    std::string quien;         // «el GDB de u0», para el mensaje
};

struct Resultado {
    std::vector<Pieza>       piezas;   // con la línea de órdenes aplicada
    std::vector<std::string> errores;  // vacío si todo cuadra
    std::vector<std::string> notas;    // lo que se ha decidido sin preguntar
};

// Aplica `--serie` sobre lo que declara la placa y comprueba que el resultado
// se puede montar. Manda la línea de órdenes, como en el resto de `sim`: el
// fichero es la configuración y el argumento es la intención inmediata.
//
// Se rechaza, nombrándolo:
//   * un `--serie` para un puente que no existe (y si solo difiere en
//     mayúsculas, se dice cómo se escribe);
//   * el mismo puente dos veces en la línea de órdenes;
//   * un destino inválido en el XML;
//   * dos puentes en el mismo puerto, o un puente en el puerto de un GDB o de
//     la GUI. Esto es lo que de verdad ahorra tiempo: sin ello el segundo en
//     abrir falla en marcha, y el mensaje sale donde nadie mira. SALVO dos que
//     lo piden en su fichero, sin `--serie`: dos placas iguales en un sistema.
//     Ahí el segundo se corre al siguiente puerto libre, y va a `notas`.
inline Resultado resuelve(const std::vector<Pieza>& placa,
                          const std::vector<Asignacion>& args,
                          const std::vector<Ocupado>& ocupados) {
    Resultado r;
    r.piezas = placa;

    for (const Pieza& p : r.piezas)
        if (!p.destino.valido)
            r.errores.push_back(p.id + ": host=" + p.destino.error);

    std::vector<std::string> vistos;
    std::vector<bool> dado(r.piezas.size(), false);   // por --serie
    for (const Asignacion& a : args) {
        if (!a.valido) { r.errores.push_back("--serie " + a.error); continue; }
        bool repetido = false;
        for (const std::string& v : vistos) if (v == a.id) repetido = true;
        if (repetido) {
            r.errores.push_back("--serie " + a.id + " aparece dos veces");
            continue;
        }
        vistos.push_back(a.id);

        Pieza* dst = nullptr;
        for (size_t k = 0; k < r.piezas.size(); ++k)
            if (r.piezas[k].id == a.id) { dst = &r.piezas[k]; dado[k] = true; }
        if (dst) { dst->destino = a.destino; continue; }

        if (placa.empty()) {
            r.errores.push_back("--serie " + a.id +
                                ": la placa no tiene ningun PuenteSerie");
            continue;
        }
        std::string casi, lista;
        for (const Pieza& p : placa) {
            if (detalle::minus(p.id) == detalle::minus(a.id)) casi = p.id;
            if (!lista.empty()) lista += ", ";
            lista += p.id;
        }
        if (!casi.empty())
            r.errores.push_back("--serie " + a.id + ": no hay ningun "
                                "PuenteSerie '" + a.id + "'; se escribe '" +
                                casi + "': el identificador distingue mayusculas");
        else
            r.errores.push_back("--serie " + a.id + ": no hay ningun "
                                "PuenteSerie con ese id; los que hay son: " + lista);
    }

    // DOS PLACAS IGUALES EN UN SISTEMA -dos Nucleo, cada una con el VCP de su
    // ST-LINK en el 3355- piden el mismo puerto sin que nadie lo haya
    // decidido: lo dicen sus ficheros. Ahí el segundo se corre al siguiente
    // libre, y se dice. Lo que pide la línea de órdenes, en cambio, es una
    // decisión, y si choca es un error, como siempre.
    const size_t n = r.piezas.size();
    auto escucha = [&](size_t k) {
        return r.piezas[k].destino.valido && usa_puerto(r.piezas[k].destino.modo);
    };
    for (size_t j = 0; j < n; ++j) {
        if (dado[j] || !escucha(j)) continue;
        Pieza& q = r.piezas[j];
        size_t choca = n;
        for (size_t i = 0; i < j && choca == n; ++i)
            if (!dado[i] && escucha(i) && r.piezas[i].destino.puerto == q.destino.puerto)
                choca = i;
        if (choca == n) continue;
        auto libre = [&](unsigned x) {
            for (size_t k = 0; k < n; ++k)
                if (k != j && escucha(k) && r.piezas[k].destino.puerto == x) return false;
            for (const Ocupado& o : ocupados) if (o.puerto == x) return false;
            return true;
        };
        unsigned p = q.destino.puerto;
        do ++p; while (p <= 65535u && !libre(p));
        if (p > 65535u) continue;                  // que lo diga el error de abajo
        r.notas.push_back(q.id + ": el puerto " + std::to_string(q.destino.puerto) +
                          " es tambien el de " + r.piezas[choca].id +
                          " en su fichero; escucha en el " + std::to_string(p));
        q.destino.puerto = p;
    }

    // Los puertos, una vez aplicado todo.
    for (size_t i = 0; i < r.piezas.size(); ++i) {
        const Pieza& p = r.piezas[i];
        if (!p.destino.valido || !usa_puerto(p.destino.modo)) continue;
        for (size_t j = i + 1; j < r.piezas.size(); ++j) {
            const Pieza& q = r.piezas[j];
            if (!q.destino.valido || !usa_puerto(q.destino.modo)) continue;
            if (p.destino.puerto == q.destino.puerto)
                r.errores.push_back(p.id + " y " + q.id + " escucharian los dos "
                                    "en el puerto " +
                                    std::to_string(p.destino.puerto));
        }
        for (const Ocupado& o : ocupados)
            if (o.puerto == p.destino.puerto)
                r.errores.push_back(p.id + " escucharia en el puerto " +
                                    std::to_string(o.puerto) + ", que ya lo usa " +
                                    o.quien);
    }
    return r;
}


// ---------------------------------------------------------------------------
// Los otros dos atributos de texto de la pieza (fase D2)
// ---------------------------------------------------------------------------

// `baudios="115200"` o `baudios="host"`. Con `host` la velocidad de la línea
// la fija el terminal del alumno por RFC 2217 (decisión D-3); hasta que diga
// algo -o si el destino no es RFC 2217- vale `BAUDIOS_OMISION`.
inline constexpr double BAUDIOS_OMISION = 115200.0;

// Los límites, que usa también la pieza cuando es el terminal quien los pide.
inline constexpr unsigned long BAUDIOS_MIN = 50, BAUDIOS_MAX = 10500000;

inline std::string parsea_baudios(const std::string& s, double& out, bool& host) {
    if (s == "host") { host = true; out = BAUDIOS_OMISION; return std::string(); }
    if (!detalle::solo_digitos(s) || s.size() > 8)
        return "'" + s + "' no son baudios: un numero entero, como 115200, o "
               "'host' para que los fije el terminal";
    const unsigned long v = std::stoul(s);
    // Por abajo, 50 baudios es lo mas lento que ofrece un terminal; por arriba,
    // la USART del F407 no pasa de 10,5 Mbit/s (PCLK2 = 84 MHz y sobremuestreo
    // por 8). Fuera de eso no hay nada al otro lado que lo entienda.
    if (v < BAUDIOS_MIN || v > BAUDIOS_MAX)
        return "'" + s + "' baudios esta fuera de 50..10500000";
    host = false;
    out = double(v);
    return std::string();
}

// El `guion=` del destino `memoria`: lo que el «terminal» teclea al arrancar.
// Admite los escapes de C que hacen falta para hablar con un firmware:
// \r \n \t \\ \0 y \xNN (dos cifras hexadecimales, exactamente). Un escape que
// no es ninguno de esos se rechaza en vez de pasarse tal cual: `\d` no es un
// carácter, es una equivocación.
inline std::string desescapa(const std::string& s, std::string& out) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { r += s[i]; continue; }
        if (i + 1 >= s.size()) return "el guion acaba en '\\' sin nada detras";
        const char c = s[++i];
        switch (c) {
        case 'r':  r += '\r'; break;
        case 'n':  r += '\n'; break;
        case 't':  r += '\t'; break;
        case '0':  r += '\0'; break;
        case '\\': r += '\\'; break;
        case 'x': {
            const std::string h = s.substr(i + 1, 2);
            if (h.size() != 2 || !std::isxdigit(static_cast<unsigned char>(h[0])) ||
                !std::isxdigit(static_cast<unsigned char>(h[1])))
                return "'\\x" + h + "': \\x necesita dos cifras hexadecimales";
            r += char(std::stoi(h, nullptr, 16));
            i += 2;
            break;
        }
        default:
            return std::string("'\\") + c + "' no es un escape: los que hay son "
                   "\\r \\n \\t \\0 \\\\ y \\xNN";
        }
    }
    out = r;
    return std::string();
}

} // namespace serie
} // namespace stm32

#endif // STM32_COMMON_SERIE_DESTINO_H
