// =============================================================================
// nomenclatura.h — CÓMO SE LLAMAN LOS PINES de cada chip
//
// Hasta T0 había una sola regla, la de ST, y era global: `pad_desde_nombre()`
// convertía cualquier nombre con forma de pin en (puerto, pin) sin saber de
// qué chip era. Admitía cuatro escrituras —`PD12`, `PD.12`, `P3.12`, `P312`—
// con una convención: EL NÚMERO DE PUERTO ES EL ÍNDICE, así que `P3.12` es
// `PD12` y `P1.0` es `PB0` (T125).
//
// Esa convención es la correcta para un STM32 y la equivocada para un MSP432,
// donde `P1.0` es el PRIMER puerto, no hay `P0`, existen `P10.x` y existe
// `PJ.x` —que con la regla de ST chocaría con `P9.x`, porque la J es la
// décima letra—. No hay una regla global que valga para los dos: el nombre lo
// tiene que interpretar el chip. [doc/analisis_msp432.md, §1.2]
//
// LO QUE HACE ESTE FICHERO. Cada familia que no sigue la regla de ST registra
// su `Nomenclatura` para los tipos de chip que le tocan (`MSP432P4…`). Al leer
// una placa, el netlist mira sus `<mcu>` y fija en un CONTEXTO qué nomenclatura
// usa cada prefijo (`u0`, `A/u0`) y la de los nombres desnudos.
//
// LO QUE NO CAMBIA, y es la condición de T0: si ninguna placa lleva un chip
// con nomenclatura propia, el contexto está INACTIVO y todas las funciones de
// nombres hacen exactamente lo que hacían, por el mismo camino de código. Los
// STM32 no se enteran: T125 sigue intacta y ningún invariante se mueve.
//
// Por qué un contexto global y no un parámetro. Porque los nombres se
// canonizan A LA ENTRADA —al leer el XML, al registrar un nodo, al buscarlo—
// en veinte sitios del netlist (`nombre_canonico_pad`), y ninguno de ellos
// sabe qué chip hay. Pasar la nomenclatura por todos sería tocar veinte
// firmas para que diecinueve pasen lo mismo. El contexto se fija una vez por
// lectura de placa, que es exactamente cuando se sabe qué chips hay.
// =============================================================================
#ifndef STM32_PINS_NOMENCLATURA_H
#define STM32_PINS_NOMENCLATURA_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace stm32 {

struct Nomenclatura {
    const char* nombre;        // "MSP432P4": para los mensajes
    // Un nombre SIN prefijo de MCU -`P1.0`, `PJ.4`- a (puerto, pin). Los
    // índices son los del chip: los que entiende su `Encapsulado` y su modelo.
    bool (*pad)(const std::string& local, unsigned& puerto, unsigned& pin);
    // Y al revés: la forma CANÓNICA, la que sale en volcados y mensajes.
    std::string (*canonico)(unsigned puerto, unsigned pin);
    // Los pines de alimentación y control que registra el chip además de los
    // de puerto: `DVCC`, `RSTN`... Terminada en nullptr.
    const char* const* alim;
};

class ContextoNomenclatura {
public:
    static ContextoNomenclatura& g() {
        static ContextoNomenclatura c;
        return c;
    }

    // --- El registro: qué tipos de chip usan qué nomenclatura ---------------
    // Por PREFIJO del nombre del tipo, en mayúsculas: "MSP432P4" cubre el
    // MSP432P401R y el P401M. Lo hace cada familia, antes de `main`.
    void registra_tipo(const std::string& prefijo_tipo, const Nomenclatura* n) {
        for (auto& kv : tipos_)
            if (kv.first == prefijo_tipo) { kv.second = n; return; }
        tipos_.emplace_back(prefijo_tipo, n);
    }
    // nullptr: ese tipo sigue la regla de siempre, la de ST.
    const Nomenclatura* de_tipo(const std::string& tipo) const {
        std::string t = tipo;
        for (char& c : t) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        for (const auto& kv : tipos_)
            if (t.compare(0, kv.first.size(), kv.first) == 0) return kv.second;
        return nullptr;
    }

    // `--mcu TIPO`: manda sobre el tipo de una placa con uno o ningún <mcu>,
    // así que también sobre cómo se leen sus nombres.
    void pon_tipo_por_omision(const std::string& tipo) { omision_ = tipo; }
    const std::string& tipo_por_omision() const { return omision_; }

    // --- El contexto de la lectura en curso ---------------------------------
    void limpia() { prefijos_.clear(); desnudo_ = nullptr; }
    void fija(const std::string& prefijo_mcu, const Nomenclatura* n) {
        if (n) prefijos_[prefijo_mcu] = n;
    }
    void fija_desnudo(const Nomenclatura* n) { desnudo_ = n; }

    // ¿Hay algún chip con nomenclatura propia en la placa que se lee? Si no,
    // todo va por el camino de siempre.
    bool activo() const { return desnudo_ || !prefijos_.empty(); }
    // La de un prefijo; "" = los nombres desnudos. nullptr = la de ST.
    const Nomenclatura* para(const std::string& prefijo) const {
        if (prefijo.empty()) return desnudo_;
        const auto it = prefijos_.find(prefijo);
        return it == prefijos_.end() ? nullptr : it->second;
    }

private:
    std::vector<std::pair<std::string, const Nomenclatura*>> tipos_;
    std::map<std::string, const Nomenclatura*> prefijos_;
    const Nomenclatura* desnudo_ = nullptr;
    std::string omision_;
};

// Registro antes de `main`, en el ámbito de fichero:
//   REGISTRA_NOMENCLATURA(MSP432P4, &NOM_MSP432P4);
#define REGISTRA_NOMENCLATURA(PREFIJO, NOM)                                    \
    struct RegistroNom##PREFIJO {                                              \
        RegistroNom##PREFIJO() {                                               \
            ::stm32::ContextoNomenclatura::g().registra_tipo(#PREFIJO, NOM);   \
        }                                                                      \
    };                                                                         \
    inline const RegistroNom##PREFIJO g_registro_nom_##PREFIJO{}

} // namespace stm32
#endif // STM32_PINS_NOMENCLATURA_H
