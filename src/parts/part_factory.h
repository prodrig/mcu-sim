// =============================================================================
// part_factory.h — La FACTORÍA de piezas externas
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f4xx/stm32f407vg_parts_paso3.md.)
//
// El paso 2 dejó la placa descrita como datos, pero con una trampa: cada
// instancia llevaba su creador puesto a mano, en C++, por quien la declaraba.
// Eso vale mientras la declaración esté en el propio programa. En cuanto viene
// de un fichero, alguien tiene que convertir la CADENA "Led" en un
// `new Led(...)`, y C++ no tiene reflexión con la que hacerlo.
//
// La solución es la de siempre: un registro estático de cadena a creador, que
// se puebla solo antes de main con una macro de auto-registro. La factoría es
// el único punto del proyecto donde un nombre de tipo escrito por un humano se
// convierte en un objeto.
//
// DOS CONSECUENCIAS QUE IMPORTAN:
//
//   * `Netlist::add()` consulta la factoría por su cuenta, así que una
//     instancia leída del XML sale con su creador puesto sin que nadie lo
//     ponga. Los ayudantes tipados de netlist_parts.h se quedan en lo que
//     siempre debieron ser: declaración de terminales y parámetros, nada más.
//
//   * Un tipo que la factoría no conoce deja de ser un fallo de enlazado y
//     pasa a ser un ERROR DE DATOS con un mensaje decente, que es lo que hace
//     falta cuando el que se equivoca escribiendo es una persona con un
//     editor de texto delante.
//
// Y una tercera, que llegó después: el registro es también EL SITIO DONDE VIVE
// LA AYUDA. Cada entrada lleva su `Ayuda` -qué hace la pieza, qué terminales
// tiene, qué atributos admite- y es la que imprime `sim --help COMPONENTE`. Al
// ir en la misma llamada que el creador no hay dos sitios que puedan
// discrepar, y como la macro EXIGE ese argumento, una pieza nueva no se puede
// dar de alta sin explicarse. El porqué está en `part_help.h`.
// =============================================================================
#ifndef STM32_PARTS_PART_FACTORY_H
#define STM32_PARTS_PART_FACTORY_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "part_base.h"
#include "part_help.h"

namespace stm32 {

struct Instancia;
class  NodeMap;
class  Netlist;

class Fabrica {
public:
    using Creador = std::function<ExtPartBase*(const Instancia&, NodeMap&, Netlist&)>;

    struct Entrada {
        Creador creador;
        Ayuda   ayuda;
    };

    static void registra(const std::string& tipo, Ayuda a, Creador c) {
        Entrada& e = mapa()[tipo];
        e.creador = std::move(c);
        e.ayuda   = std::move(a);
    }
    static const Creador* busca(const std::string& tipo) {
        auto it = mapa().find(tipo);
        return it == mapa().end() ? nullptr : &it->second.creador;
    }
    static const Ayuda* ayuda(const std::string& tipo) {
        auto it = mapa().find(tipo);
        return it == mapa().end() ? nullptr : &it->second.ayuda;
    }
    static bool conoce(const std::string& tipo) { return busca(tipo) != nullptr; }
    // Los tipos que se saben construir, en orden alfabético. Sirve para el
    // mensaje de error de un tipo desconocido: decir «no sé qué es "Lde"» sin
    // decir qué sí se sabe es la mitad de un diagnóstico.
    static std::vector<std::string> tipos() {
        std::vector<std::string> v;
        for (const auto& kv : mapa()) v.push_back(kv.first);
        return v;
    }
    static std::string tipos_como_texto() {
        std::string s;
        for (const std::string& t : tipos()) { if (!s.empty()) s += ", "; s += t; }
        return s;
    }

    // El nombre del tipo tal y como está registrado, buscándolo SIN distinguir
    // mayúsculas. El XML sí las distingue —`LED` no es `Led`, y eso se queda
    // así: un fichero es un fichero—, pero quien escribe `sim --help led` en
    // una consola está preguntando, no describiendo una placa, y hacerle
    // deletrear el tipo para poder leer qué es sería un chiste malo. Devuelve
    // la cadena vacía si no hay ninguno.
    static std::string busca_laxo(const std::string& tipo) {
        for (const auto& kv : mapa())
            if (igual_sin_mayusculas(kv.first, tipo)) return kv.first;
        return std::string();
    }

    // Las piezas registradas cuya ayuda no dice nada. Debería estar siempre
    // vacía —la macro obliga a pasar una `Ayuda`, pero no puede juzgar si dice
    // algo—, y de que lo esté se encarga la suite (T126). Es la red de
    // seguridad de «todo componente que se añada en el futuro se explica
    // solo»: la macro lo hace estructuralmente imposible de olvidar, y esto
    // caza al que pase una ayuda de adorno para salir del paso.
    static std::vector<std::string> sin_documentar() {
        std::vector<std::string> v;
        for (const auto& kv : mapa())
            if (!kv.second.ayuda.completa()) v.push_back(kv.first);
        return v;
    }

private:
    static bool igual_sin_mayusculas(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    }

    // Mapa con inicialización perezosa: el registro ocurre en tiempo de
    // inicialización estática, y un `static` global de fichero podría no estar
    // construido todavía cuando le llegue el primer registro.
    static std::map<std::string, Entrada>& mapa() {
        static std::map<std::string, Entrada> m;
        return m;
    }
};

// Auto-registro. Se usa en el ámbito de fichero, y lleva TRES argumentos:
//
//   REGISTRA_PARTE(Led,
//       Ayuda("Diodo con su resistencia en serie. No es lineal: ...")
//         .pin("anodo", "uno de los dos", "La patilla que va al pin...")
//         .atr("vf", "2.0", "Tension directa del diodo, en voltios..."),
//       [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
//           return new Led(...);
//       });
//
// El del medio no es decoración y no es opcional: es lo que imprime
// `sim --help Led`, y al exigirlo la macro, UNA PIEZA NUEVA NO SE PUEDE DAR DE
// ALTA SIN EXPLICARSE. No hace falta acordarse de documentarla en otro
// fichero, porque no hay otro fichero; y si alguien pasa una ayuda vacía para
// salir del paso, `sin_documentar()` la enumera y la suite falla.
//
// El creador va el último y entra por `__VA_ARGS__` a propósito: es el
// argumento que lleva un cuerpo con llaves, y dentro de unas llaves las comas
// de nivel superior SÍ parten un argumento de macro. La ayuda no tiene ese
// problema porque se escribe encadenando, y ahí todas las comas viven dentro
// de paréntesis.
//
// El objeto anónimo se construye antes de main y registra las dos cosas.
// `inline` en la variable evita duplicados al incluir la cabecera desde varios
// sitios, que es lo que pasa aquí: sc_main.cpp y sim_main.cpp incluyen lo
// mismo.
#define REGISTRA_PARTE(TIPO, AYUDA, ...)                                       \
    struct RegistroDe##TIPO {                                                  \
        RegistroDe##TIPO() {                                                   \
            ::stm32::Fabrica::registra(#TIPO, (AYUDA), __VA_ARGS__);           \
        }                                                                      \
    };                                                                         \
    inline const RegistroDe##TIPO g_registro_##TIPO{}

} // namespace stm32
#endif // STM32_PARTS_PART_FACTORY_H
