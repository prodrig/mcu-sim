// =============================================================================
// prueba_serie.cpp — El destino de un PuenteSerie, sin SystemC
//
// Fase D0 del plan de `doc/analisis_puente_serie.md` §10. Comprueba
// `common/serie_destino.h`, que es aritmética de cadenas: no abre un socket, no
// construye una pieza y no avanza un reloj.
//
// POR QUÉ ES UN PROGRAMA APARTE Y NO UN GRUPO DEL BANCO. T130 comprobó el
// argumento `--gui` DENTRO de `test407`, y costó mover el recuento de las
// comprobaciones. Esto no necesita nada de lo que el banco monta, así que va
// como `make red` o `make hash`: sin SystemC, en el trabajo rápido del CI, en
// segundos y en cualquier máquina. Las tres cifras de `verif/invariantes.txt`
// -comprobaciones y picosegundos- no se enteran de que existe.
//
//   make -f Makefile.mcu-sim serie
//
// Código de salida 0 si todo va bien.
// =============================================================================
#include "../common/serie_destino.h"
#include "../common/formato_uart.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace stm32::serie;
using stm32::FormatoUart;
using stm32::Paridad;
using stm32::Parada;

static unsigned g_ok = 0, g_mal = 0;
static bool comprueba(bool c, const std::string& que) {
    (c ? g_ok : g_mal)++;
    std::printf("  [%s] %s\n", c ? "OK  " : "FALLO", que.c_str());
    return c;
}
static void grupo(const char* t) { std::printf("%s\n", t); }

// ¿Algún error de la lista contiene este texto?
static bool dice(const std::vector<std::string>& e, const std::string& t) {
    for (const std::string& s : e) if (s.find(t) != std::string::npos) return true;
    return false;
}

int main() {
    // --- 1. Las formas buenas ----------------------------------------------
    grupo("1. Los tres modos, bien escritos");
    {
        const Destino d = parsea("rfc2217:3355");
        comprueba(d.valido && d.modo == Modo::rfc2217 && d.puerto == 3355,
                  "rfc2217:3355");
    }
    {
        const Destino d = parsea("tcp:7000");
        comprueba(d.valido && d.modo == Modo::tcp && d.puerto == 7000, "tcp:7000");
    }
    {
        const Destino d = parsea("memoria");
        comprueba(d.valido && d.modo == Modo::memoria, "memoria, sin puerto");
    }
    {
        const Destino d = por_omision();
        comprueba(d.valido && d.modo == Modo::rfc2217 && d.puerto == 3355,
                  "sin host= en el XML: RFC 2217 en el 3355");
    }
    comprueba(PUERTO_OMISION == 3355,
              "el puerto de omision es el 3355: vecino del 3333 (GDB) y del "
              "3344 (GUI), y lejos del 5000 del AirPlay de macOS");

    // --- 2. Los límites del puerto -----------------------------------------
    grupo("2. Los limites del puerto");
    comprueba(parsea("tcp:1").valido && parsea("tcp:1").puerto == 1, "el 1 vale");
    comprueba(parsea("tcp:65535").valido && parsea("tcp:65535").puerto == 65535,
              "el 65535 vale: es el ultimo");
    comprueba(!parsea("tcp:0").valido,
              "el 0 NO vale: un puerto que cambia en cada ejecucion no se puede "
              "escribir en el terminal del alumno");
    comprueba(!parsea("rfc2217:65536").valido, "el 65536 se sale por arriba");
    comprueba(!parsea("tcp:123456").valido, "seis cifras no son un puerto");
    comprueba(!parsea("tcp:33x5").valido, "un puerto con una letra dentro no lo es");
    comprueba(!parsea("tcp:-1").valido, "ni uno negativo");

    // --- 3. Las formas malas, y que el mensaje diga algo --------------------
    grupo("3. Las formas malas, y que el error diga por que");
    struct Caso { const char* texto; const char* debe_mencionar; };
    static const Caso malos[] = {
        { "",                    "falta el destino" },
        { "tcp",                 "falta el puerto" },
        { "rfc2217:",            "falta el puerto" },
        { "memoria:3355",        "no lleva puerto" },
        { "udp:3355",            "modo desconocido" },
        { "RFC2217:3355",        "se escribe 'rfc2217'" },
        { "Tcp:3355",            "se escribe 'tcp'" },
        { "tcp-cliente",         "falta el host o el puerto" },
        { "tcp-cliente:3355",    "falta el host o el puerto" },
        { "tcp-cliente:h:",      "no es un puerto" },
        { "tcp-cliente::3355",   "no es un host" },
        { "rfc2217-cliente:h:0", "no es un puerto" },
        { "tcp-cliente:a_b:1",   "no es un host" },
        { "tcp-cliente:::1:22",  "IPv6" },
        { "TCP-cliente:h:1",     "se escribe 'tcp-cliente'" },
        { "tcp:localhost:3355",  "tcp-cliente:HOST:PUERTO" },
        { "COM7",                "puerto serie del sistema" },
        { "com12",               "puerto serie del sistema" },
        { "/dev/ttyUSB0",        "herramienta externa" },
        { "/dev/cu.usbserial-1", "herramienta externa" },
        { "\\\\.\\COM20",        "herramienta externa" },
        { "pty",                 "puerto serie del sistema" }
    };
    for (const Caso& c : malos) {
        const Destino d = parsea(c.texto);
        const std::string nom = std::string("'") + c.texto + "'";
        comprueba(!d.valido, nom + " se rechaza");
        comprueba(d.error.find(c.debe_mencionar) != std::string::npos,
                  "  ...y el error menciona '" + std::string(c.debe_mencionar) + "'");
    }
    // `commodore` empieza por `com` y no es un puerto del sistema: el error
    // tiene que ser el de modo desconocido, no el que manda a otra herramienta.
    comprueba(parsea("commodore").error.find("modo desconocido") != std::string::npos,
              "'commodore' no se toma por un COM: la regla es COM + cifras");

    // --- 4. Ida y vuelta ----------------------------------------------------
    grupo("4. Como se escribe de vuelta");
    for (const char* t : { "memoria", "tcp:1", "tcp:3355", "rfc2217:65535" })
        comprueba(como_texto(parsea(t)) == t,
                  std::string("'") + t + "' se lee y se vuelve a escribir igual");
    comprueba(describe(parsea("rfc2217:3355")) == "RFC 2217 en localhost:3355",
              "la descripcion dice el protocolo y que es esta maquina");
    comprueba(describe(parsea("tcp:7000")) == "TCP en crudo en localhost:7000",
              "y la del modo en crudo tambien");
    comprueba(!usa_puerto(Modo::memoria) && usa_puerto(Modo::tcp) &&
              usa_puerto(Modo::rfc2217),
              "solo memoria no escucha en ningun puerto");

    // --- 5. `--serie ID=DESTINO` --------------------------------------------
    grupo("5. El argumento --serie ID=DESTINO");
    {
        const Asignacion a = parsea_asignacion("VCP=tcp:7000");
        comprueba(a.valido && a.id == "VCP" && a.destino.modo == Modo::tcp &&
                  a.destino.puerto == 7000, "VCP=tcp:7000");
    }
    {
        const Asignacion a = parsea_asignacion("u0.vcp-2=memoria");
        comprueba(a.valido && a.id == "u0.vcp-2",
                  "un id con punto, guion y cifras es un id");
    }
    {
        const Asignacion a = parsea_asignacion("N/VCP=memoria");
        const Asignacion b = parsea_asignacion("N/x/VCP=memoria");
        const Asignacion c = parsea_asignacion("/VCP=memoria");
        comprueba(a.valido && a.id == "N/VCP" && !b.valido && !c.valido,
                  "en un sistema, la placa y una barra delante: N/VCP; dos barras, o una "
                  "al principio, no");
    }
    {
        const Asignacion a = parsea_asignacion("VCP");
        comprueba(!a.valido && a.error.find("falta '='") != std::string::npos,
                  "sin '=' se rechaza, y el error enseña la forma");
    }
    {
        const Asignacion a = parsea_asignacion("=tcp:7000");
        comprueba(!a.valido && a.error.find("identificador") != std::string::npos,
                  "sin id se rechaza");
    }
    {
        const Asignacion a = parsea_asignacion("V CP=tcp:7000");
        comprueba(!a.valido && a.error.find("no es un identificador") != std::string::npos,
                  "un id con un espacio no es un id");
    }
    {
        const Asignacion a = parsea_asignacion("VCP=COM7");
        comprueba(!a.valido && a.error.find("VCP: ") == 0 &&
                  a.error.find("herramienta externa") != std::string::npos,
                  "un destino malo se rechaza nombrando el puente y el motivo");
    }
    {
        const Asignacion a = parsea_asignacion("VCP=tcp:7000=x");
        comprueba(!a.valido, "un segundo '=' va al destino, y el destino lo rechaza");
    }

    // --- 6. La placa, la línea de órdenes y los puertos cogidos --------------
    grupo("6. Aplicar --serie sobre la placa");
    const std::vector<Pieza> dos = {
        { "VCP",  parsea("rfc2217:3355") },
        { "AUX",  parsea("tcp:3356") }
    };
    const std::vector<Ocupado> nada;
    {
        const Resultado r = resuelve(dos, {}, nada);
        comprueba(r.errores.empty() && r.piezas.size() == 2,
                  "sin argumentos, la placa tal cual");
    }
    {
        const Resultado r = resuelve(dos, { parsea_asignacion("VCP=tcp:7000") }, nada);
        comprueba(r.errores.empty() && r.piezas[0].destino.modo == Modo::tcp &&
                  r.piezas[0].destino.puerto == 7000 &&
                  r.piezas[1].destino.puerto == 3356,
                  "--serie manda sobre el XML, y solo sobre el puente que nombra");
    }
    {
        const Resultado r = resuelve({}, { parsea_asignacion("VCP=tcp:7000") }, nada);
        comprueba(dice(r.errores, "la placa no tiene ningun PuenteSerie"),
                  "un --serie en una placa sin puentes se rechaza diciendolo");
    }
    {
        const Resultado r = resuelve(dos, { parsea_asignacion("vcp=tcp:7000") }, nada);
        comprueba(dice(r.errores, "se escribe 'VCP'"),
                  "si solo difiere en mayusculas, se dice como se escribe");
    }
    {
        const Resultado r = resuelve(dos, { parsea_asignacion("OTRO=tcp:7000") }, nada);
        comprueba(dice(r.errores, "los que hay son: VCP, AUX"),
                  "un id que no existe se rechaza enumerando los que hay");
    }
    {
        const Resultado r = resuelve(dos, { parsea_asignacion("VCP=tcp:7000"),
                                            parsea_asignacion("VCP=tcp:7001") }, nada);
        comprueba(dice(r.errores, "aparece dos veces"),
                  "el mismo puente dos veces en la linea de ordenes se rechaza");
    }
    {
        const Resultado r = resuelve(dos, { parsea_asignacion("AUX=rfc2217:3355") }, nada);
        comprueba(dice(r.errores, "VCP y AUX escucharian los dos en el puerto 3355"),
                  "dos puentes en el mismo puerto se rechazan nombrando a los dos");
    }
    {
        // tcp y rfc2217 en el mismo número también chocan: es el mismo puerto.
        const std::vector<Pieza> p = { { "A", parsea("tcp:4000") },
                                       { "B", parsea("rfc2217:4001") } };
        comprueba(!resuelve(p, { parsea_asignacion("B=rfc2217:4000") }, nada).errores.empty(),
                  "tcp y rfc2217 en el mismo numero chocan igual");
    }
    {
        // Dos placas iguales en un sistema: el mismo puerto LO DICEN SUS
        // FICHEROS, y nadie lo ha decidido. El segundo se corre al siguiente
        // libre -saltando el que ya tiene otro y el de un GDB- y se dice.
        const std::vector<Pieza> p = { { "A/VCP", parsea("rfc2217:3355") },
                                       { "B/VCP", parsea("rfc2217:3355") },
                                       { "F/U1",  parsea("rfc2217:3356") } };
        const std::vector<Ocupado> gdb = { { 3357, "el GDB de A/u0" } };
        const Resultado r = resuelve(p, {}, gdb);
        comprueba(r.errores.empty() && r.piezas[0].destino.puerto == 3355 &&
                  r.piezas[1].destino.puerto == 3358 && r.piezas[2].destino.puerto == 3356 &&
                  r.notas.size() == 1 &&
                  r.notas[0] == "B/VCP: el puerto 3355 es tambien el de A/VCP en su "
                                "fichero; escucha en el 3358",
                  "dos puentes que piden el mismo puerto en su fichero: el segundo se "
                  "corre al siguiente libre (ni el 3356 de F/U1 ni el 3357 del GDB), "
                  "y se dice");
    }
    {
        const std::vector<Pieza> p = { { "A", parsea("memoria") },
                                       { "B", parsea("memoria") } };
        comprueba(resuelve(p, {}, nada).errores.empty(),
                  "dos puentes en memoria no chocan: no abren nada");
    }
    {
        const std::vector<Ocupado> gdb = { { 3333, "el GDB de u0" },
                                           { 3344, "mcu-sim-gui" } };
        const Resultado r1 = resuelve(dos, { parsea_asignacion("VCP=tcp:3333") }, gdb);
        comprueba(dice(r1.errores, "VCP escucharia en el puerto 3333, que ya lo "
                                   "usa el GDB de u0"),
                  "un puente en el puerto de un GDB se rechaza nombrando al GDB");
        const Resultado r2 = resuelve(dos, { parsea_asignacion("AUX=tcp:3344") }, gdb);
        comprueba(dice(r2.errores, "mcu-sim-gui"),
                  "y en el de la GUI, nombrando a la GUI");
    }
    {
        const std::vector<Pieza> p = { { "VCP", parsea("COM7") } };
        const Resultado r = resuelve(p, {}, nada);
        comprueba(dice(r.errores, "VCP: host="),
                  "un host= malo en el XML se rechaza nombrando la pieza y el "
                  "atributo");
    }
    {
        // Un host= malo en el XML se puede ARREGLAR desde la línea de órdenes:
        // manda el argumento, y el error del XML ya no aplica... salvo que el
        // mensaje lo diga igual. Se decide que NO: el XML sigue mal escrito y
        // eso se dice, porque el siguiente que lo lance sin --serie se lo va
        // a encontrar.
        const std::vector<Pieza> p = { { "VCP", parsea("COM7") } };
        const Resultado r = resuelve(p, { parsea_asignacion("VCP=tcp:7000") }, nada);
        comprueba(dice(r.errores, "VCP: host="),
                  "un host= malo en el XML se dice aunque --serie lo tape");
    }

    // --- 7. El formato de trama (fase D1) ------------------------------------
    grupo("7. El formato de trama: 8N1 y compania");
    {
        FormatoUart f;
        comprueba(stm32::parsea_formato("8N1", f).empty() && f.bits == 8 &&
                  f.paridad == Paridad::ninguna && f.parada == Parada::uno,
                  "8N1: ocho bits, sin paridad, uno de parada");
        comprueba(f.medios_de_trama() == 20u, "8N1 ocupa diez bits de linea");
    }
    {
        FormatoUart f;
        comprueba(stm32::parsea_formato("7E1", f).empty() && f.bits == 7 &&
                  f.paridad == Paridad::par, "7E1: siete bits y paridad par");
        comprueba(f.medios_de_trama() == 20u,
                  "7E1 tambien ocupa diez: los bits NO cuentan la paridad");
    }
    {
        FormatoUart f;
        comprueba(stm32::parsea_formato("8N1.5", f).empty() &&
                  f.parada == Parada::uno_y_medio && f.medios_de_trama() == 21u,
                  "8N1.5: diez bits y medio");
        comprueba(stm32::parsea_formato("9O2", f).empty() &&
                  f.medios_de_trama() == 2u * (1 + 9 + 1) + 4u,
                  "9O2: arranque, nueve, paridad y dos de parada = 13 bits");
        comprueba(stm32::parsea_formato("8n1", f).empty(),
                  "la paridad admite minuscula: 8n1");
    }
    for (const char* t : { "8N1", "7E1", "8O2", "9N1", "5M1", "6S2", "8N1.5" }) {
        FormatoUart f;
        stm32::parsea_formato(t, f);
        comprueba(stm32::como_texto(f) == t,
                  std::string("'") + t + "' se lee y se vuelve a escribir igual");
    }
    {
        struct Caso { const char* t; const char* dice; };
        static const Caso malos[] = {
            { "",      "no es un formato" },
            { "8N",    "no es un formato" },
            { "4N1",   "de 5 a 9" },
            { "0N1",   "de 5 a 9" },
            { "8X1",   "no es una paridad" },
            { "8N3",   "no son bits de parada" },
            { "8N1,5", "con punto" },
            { "8N0.5", "no son bits de parada" }
        };
        for (const Caso& c : malos) {
            FormatoUart f;
            const std::string e = stm32::parsea_formato(c.t, f);
            comprueba(!e.empty() && e.find(c.dice) != std::string::npos,
                      std::string("'") + c.t + "' se rechaza diciendo '" + c.dice + "'");
        }
        FormatoUart f;  f.bits = 7;
        const FormatoUart antes = f;
        stm32::parsea_formato("8X1", f);
        comprueba(f == antes, "un formato malo no toca el que habia");
    }
    {
        // La paridad, contra la definicion: par = numero TOTAL de unos par.
        FormatoUart p;  stm32::parsea_formato("8E1", p);
        FormatoUart i;  stm32::parsea_formato("8O1", i);
        comprueba(!p.bit_de_paridad(0x00) &&  p.bit_de_paridad(0x01) &&
                  !p.bit_de_paridad(0x03) &&  p.bit_de_paridad(0x07),
                  "paridad par: el bit completa un numero par de unos");
        comprueba( i.bit_de_paridad(0x00) && !i.bit_de_paridad(0x01) &&
                   i.bit_de_paridad(0x03) && !i.bit_de_paridad(0x07),
                  "paridad impar: lo contrario");
        FormatoUart m;  stm32::parsea_formato("8M1", m);
        FormatoUart e;  stm32::parsea_formato("8S1", e);
        comprueba(m.bit_de_paridad(0x00) && m.bit_de_paridad(0xFF) &&
                  !e.bit_de_paridad(0x00) && !e.bit_de_paridad(0xFF),
                  "marca siempre 1 y espacio siempre 0, sea cual sea el dato");
        FormatoUart s7; stm32::parsea_formato("7E1", s7);
        comprueba(!s7.bit_de_paridad(0x80),
                  "con siete bits, el octavo no cuenta para la paridad");
        comprueba(s7.mascara() == 0x7Fu, "y la mascara de siete bits es 0x7F");
    }

    // --- 8. Baudios y guion (fase D2) ----------------------------------------
    grupo("8. Los baudios y el guion de la pieza");
    {
        double b = 0; bool h = true;
        comprueba(parsea_baudios("115200", b, h).empty() && b == 115200.0 && !h,
                  "115200 son 115200 baudios");
        comprueba(parsea_baudios("host", b, h).empty() && h && b == 115200.0,
                  "'host': los fija el terminal, y mientras tanto 115200");
        comprueba(parsea_baudios("50", b, h).empty() &&
                  parsea_baudios("10500000", b, h).empty(),
                  "50 y 10500000 son los limites, y valen");
        comprueba(!parsea_baudios("49", b, h).empty() &&
                  !parsea_baudios("10500001", b, h).empty(),
                  "49 y 10500001 no");
        comprueba(parsea_baudios("9600.5", b, h).find("no son baudios") != std::string::npos &&
                  !parsea_baudios("", b, h).empty() &&
                  !parsea_baudios("Host", b, h).empty(),
                  "ni un decimal, ni nada, ni 'Host' con mayuscula");
    }
    {
        std::string g;
        comprueba(desescapa("hola\\r\\n", g).empty() && g == "hola\r\n",
                  "\\r y \\n");
        comprueba(desescapa("a\\tb\\\\c", g).empty() && g == "a\tb\\c",
                  "\\t y la barra doble");
        comprueba(desescapa("\\x13\\xFf\\x00", g).empty() && g.size() == 3 &&
                  g[0] == '\x13' && g[1] == '\xFF' && g[2] == '\0',
                  "\\xNN en mayuscula o minuscula, y un cero de verdad dentro");
        comprueba(desescapa("\\0", g).empty() && g.size() == 1 && g[0] == '\0',
                  "\\0");
        comprueba(desescapa("", g).empty() && g.empty(), "el guion vacio vale");
        const std::string antes = "intacto";
        g = antes;
        comprueba(desescapa("\\d", g).find("no es un escape") != std::string::npos &&
                  g == antes, "\\d se rechaza, y no toca la salida");
        comprueba(!desescapa("a\\", g).empty(), "una barra al final se rechaza");
        comprueba(desescapa("\\x4", g).find("dos cifras") != std::string::npos,
                  "\\x con una sola cifra se rechaza");
        comprueba(desescapa("\\xG1", g).find("dos cifras") != std::string::npos,
                  "\\x con una letra que no es hexadecimal se rechaza");
    }

    // --- 9. Los clientes (fase D8) -------------------------------------------
    grupo("9. El modo cliente: tcp-cliente y rfc2217-cliente");
    {
        const Destino d = parsea("tcp-cliente:127.0.0.1:7000");
        comprueba(d.valido && d.modo == Modo::tcp_cliente && d.host == "127.0.0.1" &&
                  d.puerto == 7000, "tcp-cliente:127.0.0.1:7000");
        const Destino e = parsea("rfc2217-cliente:ser2net.lab-3.upv.es:2001");
        comprueba(e.valido && e.modo == Modo::rfc2217_cliente &&
                  e.host == "ser2net.lab-3.upv.es" && e.puerto == 2001,
                  "rfc2217-cliente con un nombre de maquina, con puntos y guion");
        comprueba(es_cliente(d.modo) && es_cliente(e.modo) && !es_cliente(Modo::tcp) &&
                  !es_cliente(Modo::memoria), "es_cliente distingue los dos");
        comprueba(!usa_puerto(Modo::tcp_cliente) && !usa_puerto(Modo::rfc2217_cliente) &&
                  por_red(Modo::tcp_cliente) && !por_red(Modo::memoria),
                  "un cliente no escucha en ningun puerto, pero va por la red");
    }
    for (const char* t : { "tcp-cliente:localhost:1", "rfc2217-cliente:10.0.0.7:65535" })
        comprueba(como_texto(parsea(t)) == t,
                  std::string("'") + t + "' se lee y se vuelve a escribir igual");
    comprueba(describe(parsea("rfc2217-cliente:h:2217")) ==
              "RFC 2217 hacia h:2217 (cliente)",
              "la descripcion dice a donde y que es el cliente");
    comprueba(describe(parsea("tcp-cliente:h:9")) == "TCP en crudo hacia h:9 (cliente)",
              "tambien en crudo");
    {
        // Un cliente al puerto de un GDB, o al mismo que otro puente de la
        // placa, NO choca: no escucha. Dos puentes de la misma placa unidos
        // por TCP, uno escuchando y el otro conectandose, es un montaje que
        // tiene sentido: las UART de dos MCU por un cable virtual.
        const std::vector<Pieza> p = {
            { "A", parsea("tcp:4000") },
            { "B", parsea("tcp-cliente:127.0.0.1:4000") } };
        const std::vector<Ocupado> gdb = { { 4000, "el GDB" } };
        comprueba(resuelve(p, {}, {}).errores.empty(),
                  "un puente escuchando y otro conectandose a el: vale");
        const Resultado r = resuelve({ { "B", parsea("tcp-cliente:127.0.0.1:3333") } },
                                     {}, { { 3333, "el GDB" } });
        comprueba(r.errores.empty(), "un cliente al puerto de un GDB no choca");
        comprueba(!resuelve(p, {}, gdb).errores.empty(),
                  "pero el que escucha sigue chocando con el GDB");
    }
    {
        const Asignacion a = parsea_asignacion("VCP=rfc2217-cliente:127.0.0.1:2217");
        comprueba(a.valido && a.destino.modo == Modo::rfc2217_cliente &&
                  a.destino.host == "127.0.0.1", "--serie VCP=rfc2217-cliente:127.0.0.1:2217");
    }

    std::printf("RESULTADO %u ok, %u fallos\n", g_ok, g_mal);
    return g_mal ? 1 : 0;
}
