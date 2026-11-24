// =============================================================================
// exporta_mcu.h — `mcu-sim --exporta-mcu TIPO`: lo que el modelo sabe de los
// pines de un chip, en XML
//
// Fase B2 de `doc/analisis_build.md`. El formato y el porqué están en
// `soc/inventario_pines.h`; aquí se construye el chip -sin simularlo: solo la
// elaboración- y se escribe lo que dice:
//
//   * los PADS QUE SALEN a su encapsulado, con el nombre canónico que les da
//     la nomenclatura del chip (`PA5`, `P1.0`, `PN0`, `GPIO5`), y en cada uno
//     las funciones que el modelo simula, de su inventario;
//   * las patillas de ALIMENTACIÓN y control que el chip da de alta como
//     nodos (`VDD`, `NRST`, `DVCC`...);
//   * lo que se elige con un PMAP o una matriz, aparte.
//
// LO QUE SE COMPRUEBA ANTES DE ESCRIBIR, y si no cuadra, `--exporta-mcu`
// falla con el motivo:
//   * que la máscara del encapsulado sea coherente con su recuento de GPIO
//     (lo mismo que T128 en el F405/F407, aquí para los 65);
//   * que cada pad que sale sea un nodo del chip con su nombre canónico: si
//     no, la placa no podría nombrarlo;
//   * que ninguna función del inventario esté en un pad que no existe.
// Una función registrada en un pad que ESTE encapsulado no saca no es un
// error -el silicio la tiene; es el SPI4 de un F446RE- y no se escribe: se
// cuenta en `fuera_del_encapsulado`.
// =============================================================================
#ifndef STM32_TOP_EXPORTA_MCU_H
#define STM32_TOP_EXPORTA_MCU_H

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "../soc/mcu_if.h"
#include "../soc/mcu_desc.h"
#include "../soc/inventario_pines.h"
#include "../pins/nomenclatura.h"
#include "../parts/netlist.h"
#include "../parts/xml_min.h"

namespace stm32 {

// El nombre canónico de un pad: el de la nomenclatura del chip, o la regla de
// ST (letra de puerto y número) si no tiene una propia
inline std::string nombre_de_pad(const Nomenclatura* nom, unsigned p, unsigned i) {
    if (nom) return nom->canonico(p, i);
    return std::string("P") + char('A' + p) + std::to_string(i);
}

// Escribe el XML de `tipo` en `xml`. Devuelve el error, o vacío. `inst` es el
// nombre con el que se construye el chip: único en el proceso
inline std::string exporta_mcu(const std::string& tipo, std::string& xml,
                               const std::string& inst = "x") {
    const McuDesc* d = CatalogoMcu::busca(tipo);
    if (!d) return "'" + tipo + "' no es un MCU que este mcu-sim sepa construir. Los que "
                   "sabe: " + CatalogoMcu::como_texto();
    if (!d->enc) return std::string(d->nombre) + ": sin encapsulado en su descriptor";
    const Encapsulado& enc = *d->enc;
    if (!enc.coherente())
        return std::string(d->nombre) + ": la mascara del " + enc.nombre + " tiene " +
               std::to_string(enc.cuenta_gpio()) + " GPIO y su recuento dice " +
               std::to_string(enc.n_gpio);
    const Cableado sin_puentes;
    // Los avisos de SystemC al construir -un nombre de módulo con espacios,
    // W506- salen por la salida estándar, que es donde va el XML. Mientras se
    // construye el chip, callan: no dicen nada de sus pines
    const sc_core::sc_actions antes =
        sc_core::sc_report_handler::set_actions(sc_core::SC_WARNING, sc_core::SC_DO_NOTHING);
    mcu_if* m = FabricaMcu::crea(*d, inst.c_str(), DBG_PINES, sin_puentes);
    sc_core::sc_report_handler::set_actions(sc_core::SC_WARNING, antes);
    if (!m) return std::string(d->nombre) + ": la familia " + d->familia +
                   " no esta registrada en la factoria";
    NodeMap nodos;
    m->registra_nodos("", nodos);
    const Nomenclatura* nom = ContextoNomenclatura::g().de_tipo(d->nombre);

    // Los pads que salen, en orden de puerto y pin
    std::vector<std::pair<unsigned, unsigned>> pads;
    std::set<std::string> nombres_pad;
    for (unsigned p = 0; p < N_PUERTOS_SILICIO; ++p)
        for (unsigned i = 0; i < N_PINES_PUERTO_MAX; ++i)
            if (enc.bonded(p, i)) {
                const std::string n = nombre_de_pad(nom, p, i);
                const Nodo* nd = nodos.busca(n);
                if (!nd || !nd->es_pin)
                    return std::string(d->nombre) + ": el " + enc.nombre + " saca " + n +
                           ", y el chip no tiene ningun nodo de pad con ese nombre";
                pads.emplace_back(p, i);
                nombres_pad.insert(nd->nombre);
            }
    if (pads.size() != enc.n_gpio)
        return std::string(d->nombre) + ": " + std::to_string(pads.size()) + " pads, y el " +
               enc.nombre + " dice " + std::to_string(enc.n_gpio);

    // Las funciones, por pad
    InventarioPines inv = m->inventario_pines();
    std::map<std::pair<unsigned, unsigned>, std::vector<const FuncionDePin*>> por_pad;
    unsigned fuera = 0;
    for (const FuncionDePin& f : inv.fijas) {
        if (f.puerto >= N_PUERTOS_SILICIO || f.pin >= N_PINES_PUERTO_MAX)
            return std::string(d->nombre) + ": el inventario pone una funcion en un pad que "
                   "no existe (" + std::to_string(f.puerto) + ", " + std::to_string(f.pin) + ")";
        if (!enc.bonded(f.puerto, f.pin)) { ++fuera; continue; }
        por_pad[{f.puerto, f.pin}].push_back(&f);
    }
    auto orden = [](const FuncionDePin* a, const FuncionDePin* b) {
        return std::make_tuple(a->analogica, a->modo, a->nombre, a->senal) <
               std::make_tuple(b->analogica, b->modo, b->nombre, b->senal);
    };
    auto funcion = [&](const FuncionDePin& f, const char* sangria) {
        std::string x = std::string(sangria) + "<funcion";
        if (f.modo >= 0) x += " modo=\"" + std::to_string(f.modo) + "\"";
        if (!f.nombre.empty()) x += " nombre=\"" + xml_escapa(f.nombre) + "\"";
        if (!f.senal.empty()) x += " senal=\"" + xml_escapa(f.senal) + "\"";
        if (f.analogica) x += " analogica=\"si\"";
        return x + "/>\n";
    };

    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<!-- Lo que el modelo de mcu-sim sabe de los pines del " + std::string(d->nombre) +
         ":\n     los que salen a su encapsulado y las funciones que SIMULA en cada uno.\n"
         "     Lo genera mcu-sim con su opcion exporta-mcu (doc/analisis_build.md, B2). -->\n";
    x += "<mcu tipo=\"" + xml_escapa(d->nombre) + "\" familia=\"" + xml_escapa(d->familia) +
         "\" encapsulado=\"" + xml_escapa(enc.nombre) + "\" patillas=\"" +
         std::to_string(enc.n_pines) + "\" gpio=\"" + std::to_string(enc.n_gpio) +
         "\" encapsulado_verificado=\"" + (enc.verificado ? "si" : "no") +
         "\" version=\"1\">\n";
    if (!inv.hay)
        x += "  <sin_inventario motivo=\"" + xml_escapa(inv.motivo) + "\"/>\n";
    for (const auto& pi : pads) {
        const std::string n = nombre_de_pad(nom, pi.first, pi.second);
        auto it = por_pad.find(pi);
        if (it == por_pad.end() || it->second.empty()) {
            x += "  <pad nombre=\"" + xml_escapa(n) + "\"/>\n";
            continue;
        }
        std::sort(it->second.begin(), it->second.end(), orden);
        x += "  <pad nombre=\"" + xml_escapa(n) + "\">\n";
        for (const FuncionDePin* f : it->second) x += funcion(*f, "    ");
        x += "  </pad>\n";
    }
    // La alimentación y el control: los nodos del chip que no son pads. Los
    // de pads que este encapsulado no saca tampoco: se dan de alta igual,
    // marcados como no soldados
    nodos.recorre([&](const Nodo& nd) {
        if (nd.es_pin && nombres_pad.count(nd.nombre)) return;
        if (!nd.bonded) return;
        if (nd.es_pin) {
            // ¿Es un pad de puerto, de otro encapsulado? Entonces no es de este
            unsigned p = 0, i = 0;
            if (nom ? nom->pad(nd.nombre, p, i) : (nd.nombre.size() > 2 && nd.nombre[0] == 'P' &&
                                                    nd.nombre[1] >= 'A' && nd.nombre[1] <= 'Z' &&
                                                    std::isdigit(static_cast<unsigned char>(nd.nombre[2]))))
                return;
        }
        x += "  <alimentacion nombre=\"" + xml_escapa(nd.nombre) + "\"/>\n";
    });
    if (!inv.mapeo.empty()) {
        x += "  <mapeo tipo=\"" + xml_escapa(inv.mapeo) + "\"";
        if (!inv.puertos_mapeables.empty())
            x += " puertos=\"" + xml_escapa(inv.puertos_mapeables) + "\"";
        if (!inv.mapeo_inventariado) {
            x += " inventario=\"no\" motivo=\"" + xml_escapa(inv.motivo_mapeo) + "\"/>\n";
        } else {
            x += ">\n";
            std::vector<const FuncionDePin*> v;
            for (const FuncionDePin& f : inv.mapeables) v.push_back(&f);
            std::sort(v.begin(), v.end(), orden);
            for (const FuncionDePin* f : v) x += funcion(*f, "    ");
            x += "  </mapeo>\n";
        }
    }
    if (fuera)
        x += "  <!-- " + std::to_string(fuera) + " funciones mas en pads del silicio que este "
             "encapsulado no saca -->\n";
    x += "</mcu>\n";
    xml = x;
    // El chip se queda construido: una jerarquía de SystemC no se desmonta a
    // medias, y el proceso acaba enseguida
    return std::string();
}

} // namespace stm32
#endif // STM32_TOP_EXPORTA_MCU_H
