// =============================================================================
// gui_destino.h — El argumento `--gui host:puerto`, y nada más
//
// La fase 0 del plan de `mcu-sim-gui` (véase `mcu-sim-gui/doc/plan_dos_procesos.md`)
// pide exactamente esto y ni una línea más: que el simulador RECONOZCA el
// argumento y sepa a dónde apuntaría. Ni socket, ni proceso nuevo, ni un
// picosegundo distinto de tiempo simulado.
//
// POR QUÉ ESTO ES UN FICHERO Y NO CUATRO LÍNEAS EN `sim_main.cpp`. Porque así
// se puede probar. El parseo de una cadena es una función PURA: se le da texto
// y devuelve un destino o un error, sin tocar la red, sin abrir nada y sin
// avanzar el reloj. Metido en el cuerpo de `sc_main` no habría forma de
// comprobar las formas raras —el puerto 0, el 65536, el IPv6 sin corchetes—
// más que ejecutando el programa entero y mirando lo que imprime. Aquí lo
// comprueba **T130** del banco, y no cuesta tiempo simulado.
//
// LA POLÍTICA DEL HOST, que es una decisión y no un descuido. Los dos
// servidores de GDB de este proyecto escuchan SOLO en la interfaz de bucle
// local, a propósito: son un depurador con acceso total a la memoria del
// objetivo. Este socket no es tan grave —deja ver el estado de la simulación y
// accionar sus mandos— pero tampoco es inocuo, y NO está autenticado. Así que
// se permite apuntar a otra máquina, porque hace falta para una GUI remota,
// pero `es_bucle_local()` está aquí para que quien lo use pueda AVISAR. En
// silencio, no.
// =============================================================================
#ifndef STM32_COMMON_GUI_DESTINO_H
#define STM32_COMMON_GUI_DESTINO_H

#include <cctype>
#include <string>

#include "protocolo.h"

namespace stm32 {
namespace gui {

// A dónde se conectaría `mcu-sim` para hablar con la ventana.
struct Destino {
    std::string host   = "localhost";
    unsigned    puerto = mcusim::proto::PUERTO_OMISION;
    bool        valido = true;
    std::string error;                 // vacío si `valido`
};

// ¿Esta dirección es de la propia máquina? Se mira por el NOMBRE, no
// resolviéndola: resolver un nombre es una operación de red, y este fichero no
// hace red. La consecuencia está dicha: un alias del `hosts` que apunte a
// 127.0.0.1 no se reconoce y se avisa de más. Avisar de más es el lado bueno
// en el que equivocarse.
inline bool es_bucle_local(const std::string& h) {
    if (h == "localhost" || h == "::1" || h == "[::1]") return true;
    if (h == "0:0:0:0:0:0:0:1" || h == "[0:0:0:0:0:0:0:1]") return true;
    // Todo 127.0.0.0/8, no solo el 127.0.0.1.
    return h.rfind("127.", 0) == 0;
}

namespace detalle {

inline bool solo_digitos(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Un puerto de verdad: 1..65535. El 0 NO vale —es «dame cualquiera», que aquí
// no significa nada— y tampoco un número con letras dentro.
inline bool puerto_valido(const std::string& s, unsigned& out) {
    if (!solo_digitos(s) || s.size() > 5) return false;
    const unsigned long v = std::stoul(s);
    if (v == 0 || v > 65535) return false;
    out = static_cast<unsigned>(v);
    return true;
}

inline Destino malo(const std::string& porque) {
    Destino d;
    d.valido = false;
    d.error  = porque;
    return d;
}

} // namespace detalle

// Las formas admitidas, y son todas las que el plan enumera:
//
//     ""                     ->  localhost:3344      (`--gui` a secas)
//     "7000"                 ->  localhost:7000
//     "maquina"              ->  maquina:3344
//     "maquina:7000"         ->  maquina:7000
//     "[::1]:7000"           ->  [::1]:7000          (IPv6, con corchetes)
//     "[::1]"                ->  [::1]:3344
//
// El IPv6 EXIGE corchetes, y no por capricho: `::1:7000` es una dirección IPv6
// perfectamente válida, así que sin corchetes no hay forma de saber si los dos
// últimos puntos separan un puerto o son parte de la dirección. Se dice en el
// error, que es lo que uno quiere leer cuando se equivoca.
inline Destino parsea(const std::string& s) {
    Destino d;
    if (s.empty()) return d;                        // `--gui` a secas

    // --- IPv6 entre corchetes ---------------------------------------------
    if (s[0] == '[') {
        const std::string::size_type cierre = s.find(']');
        if (cierre == std::string::npos)
            return detalle::malo("falta el corchete de cierre en '" + s + "'");
        if (cierre == 1)
            return detalle::malo("direccion IPv6 vacia en '" + s + "'");
        d.host = s.substr(0, cierre + 1);           // se guardan los corchetes
        const std::string resto = s.substr(cierre + 1);
        if (resto.empty()) return d;                // sin puerto: el de omision
        if (resto[0] != ':')
            return detalle::malo("sobra '" + resto + "' despues de los corchetes");
        const std::string p = resto.substr(1);
        if (p.empty()) return detalle::malo("falta el puerto despues de ':'");
        if (!detalle::puerto_valido(p, d.puerto))
            return detalle::malo("'" + p + "' no es un puerto (1..65535)");
        return d;
    }

    // --- Solo un numero: es el puerto, no el host --------------------------
    if (detalle::solo_digitos(s)) {
        if (!detalle::puerto_valido(s, d.puerto))
            return detalle::malo("'" + s + "' no es un puerto (1..65535)");
        return d;                                   // host: el de omision
    }

    // --- host[:puerto] -----------------------------------------------------
    const std::string::size_type dp = s.rfind(':');
    if (dp == std::string::npos) {                  // solo el host
        d.host = s;
        return d;
    }
    if (s.find(':') != dp)                          // hay mas de uno
        return detalle::malo("'" + s + "' parece IPv6: hacen falta corchetes, "
                             "como en [::1]:3344");
    const std::string h = s.substr(0, dp);
    const std::string p = s.substr(dp + 1);
    if (h.empty()) return detalle::malo("falta el host antes de ':'");
    if (p.empty()) return detalle::malo("falta el puerto despues de ':'");
    if (!detalle::puerto_valido(p, d.puerto))
        return detalle::malo("'" + p + "' no es un puerto (1..65535)");
    d.host = h;
    return d;
}

// Como se escribe un destino para enseñarselo a alguien.
inline std::string como_texto(const Destino& d) {
    return d.host + ":" + std::to_string(d.puerto);
}

} // namespace gui
} // namespace stm32

#endif // STM32_COMMON_GUI_DESTINO_H
