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

#include <cstdio>
#include <string>
#include <vector>

using namespace stm32::serie;

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
        { "tcp-cliente:h:3355",  "fase D8" },
        { "rfc2217-cliente:h:1", "fase D8" },
        { "tcp:localhost:3355",  "host" },
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
                                       { "B", parsea("rfc2217:4000") } };
        comprueba(!resuelve(p, {}, nada).errores.empty(),
                  "tcp y rfc2217 en el mismo numero chocan igual");
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
        comprueba(dice(r1.errores, "VCP escucharia en el puerto 3333, que ya es "
                                   "de el GDB de u0"),
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

    std::printf("RESULTADO %u ok, %u fallos\n", g_ok, g_mal);
    return g_mal ? 1 : 0;
}
