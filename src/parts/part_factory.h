// =============================================================================
// part_factory.h — La FACTORÍA de piezas externas
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso3.md.)
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
// =============================================================================
#ifndef STM32_PARTS_PART_FACTORY_H
#define STM32_PARTS_PART_FACTORY_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "part_base.h"

namespace stm32 {

struct Instancia;
class  NodeMap;
class  Netlist;

class Fabrica {
public:
    using Creador = std::function<ExtPartBase*(const Instancia&, NodeMap&, Netlist&)>;

    static void registra(const std::string& tipo, Creador c) {
        mapa()[tipo] = std::move(c);
    }
    static const Creador* busca(const std::string& tipo) {
        auto it = mapa().find(tipo);
        return it == mapa().end() ? nullptr : &it->second;
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

private:
    // Mapa con inicialización perezosa: el registro ocurre en tiempo de
    // inicialización estática, y un `static` global de fichero podría no estar
    // construido todavía cuando le llegue el primer registro.
    static std::map<std::string, Creador>& mapa() {
        static std::map<std::string, Creador> m;
        return m;
    }
};

// Auto-registro. Se usa en el ámbito de fichero:
//
//   REGISTRA_PARTE(Led, [](const Instancia& d, NodeMap& n, Netlist&) {
//       return new Led(...);
//   });
//
// El objeto anónimo se construye antes de main y registra su creador. `inline`
// en la variable evita duplicados al incluir la cabecera desde varios sitios,
// que es lo que pasa aquí: sc_main.cpp y sim_main.cpp incluyen lo mismo.
#define REGISTRA_PARTE(TIPO, CREADOR)                                          \
    struct RegistroDe##TIPO {                                                  \
        RegistroDe##TIPO() { ::stm32::Fabrica::registra(#TIPO, CREADOR); }     \
    };                                                                         \
    inline const RegistroDe##TIPO g_registro_##TIPO{}

} // namespace stm32
#endif // STM32_PARTS_PART_FACTORY_H
