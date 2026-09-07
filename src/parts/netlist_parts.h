// =============================================================================
// netlist_parts.h — Los CONSTRUCTORES TIPADOS del netlist
//
// (Paso 2 de la ruta de adopción del esquema XML+SVG de QtSysC.)
//
// `netlist.h` sabe de nodos, instancias y conexiones, y no conoce ni una sola
// clase de pieza: es la máquina. Este fichero es la otra mitad, la que sabe que
// un `Led` se construye con un nodo y un `CanTransceiver` con dos nodos y una
// referencia a otra instancia.
//
// POR QUÉ ESTÁN SEPARADOS. En el paso 3, cuando exista la factoría que convierte
// la cadena "Led" del XML en un `new Led(...)`, lo que se reescribe es ESTE
// fichero; `netlist.h` no se toca. Y mientras tanto, cada ayudante de aquí
// cumple la misma función que cumplirá la entrada de la factoría: declarar los
// terminales por su nombre y saber montar la pieza a partir de ellos. La
// diferencia es que hoy lo hace con el compilador comprobándolo.
//
// Cada ayudante hace DOS cosas, y hacerlas juntas es lo que evita que se
// separen: declara los terminales con su nombre nominal Y pone el creador que
// los usa. Si un día alguien añade un terminal y se olvida de conectarlo en el
// constructor, la comprobación de ida y vuelta de la suite lo caza.
// =============================================================================
#ifndef STM32_PARTS_NETLIST_PARTS_H
#define STM32_PARTS_NETLIST_PARTS_H

#include "netlist.h"
#include "ext_parts.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Piezas de una patilla
// ---------------------------------------------------------------------------

// Un LED con su resistencia en serie. `a_vss` distingue el montaje: ánodo al
// pin (se enciende en alto) o cátodo al pin (se enciende en bajo, que es lo que
// hacen las placas de evaluación de ST).
inline Instancia& led(Netlist& nl, const char* id, const std::string& nodo,
                      bool a_vss = true, double vf = 2.0, double r = 330.0) {
    Instancia& i = nl.add("Led", id);
    i.pin("anodo", nodo).par("a_vss", a_vss ? "si" : "no").par("vf", vf).par("r", r);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Led(d.id.c_str(), n[d.nodo_de("anodo")], d.si("a_vss", true),
                       d.num("vf", 2.0), d.num("r", 330.0));
    };
    return i;
}

inline Instancia& pulsador(Netlist& nl, const char* id, const std::string& nodo,
                           double r_cerrado = 10.0) {
    Instancia& i = nl.add("Button", id);
    i.pin("pin", nodo).par("r_cerrado", r_cerrado);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Button(n[d.nodo_de("pin")], d.num("r_cerrado", 10.0));
    };
    return i;
}

inline Instancia& cristal(Netlist& nl, const char* id, const std::string& nodo,
                          double vdd = 3.3) {
    Instancia& i = nl.add("Crystal", id);
    i.pin("osc_in", nodo).par("vdd", vdd);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Crystal(n[d.nodo_de("osc_in")], d.num("vdd", 3.3));
    };
    return i;
}

inline Instancia& resistencia(Netlist& nl, const char* id, const std::string& nodo,
                              double a_voltios, double ohmios) {
    Instancia& i = nl.add("Resistor", id);
    i.pin("a", nodo).par("v", a_voltios).par("r", ohmios);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Resistor(n[d.nodo_de("a")], d.num("v", 3.3), d.num("r", 10e3));
    };
    return i;
}

inline Instancia& driver(Netlist& nl, const char* id, const std::string& nodo,
                         double vdd = 3.3, double r_out = 25.0) {
    Instancia& i = nl.add("Driver", id);
    i.pin("pin", nodo).par("vdd", vdd).par("r_out", r_out);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new Driver(n[d.nodo_de("pin")], d.num("vdd", 3.3), d.num("r_out", 25.0));
    };
    return i;
}

// Una pista de placa entre dos pines. El origen solo se lee: por eso en el
// netlist sale marcado `pasivo` y aquí se declara igual que el destino.
inline Instancia& pista(Netlist& nl, const char* id, const std::string& origen,
                        const std::string& destino) {
    Instancia& i = nl.add("SignalLink", id);
    i.pin("origen", origen).pin("destino", destino);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        return new SignalLink(d.id.c_str(), n[d.nodo_de("origen")],
                                            n[d.nodo_de("destino")]);
    };
    return i;
}

// ---------------------------------------------------------------------------
// El bus CAN
//
// Es el grupo que obligó a que el netlist supiera hacer tres cosas que las
// piezas de una patilla no exigen: NODOS QUE NO SON PINES (el hilo),
// REFERENCIAS ENTRE INSTANCIAS (un transceptor necesita su hilo, no solo el
// nodo) y ORDEN DE CONSTRUCCIÓN (el hilo antes que quien se cuelga de él).
// ---------------------------------------------------------------------------

// El hilo. Su terminal `bus` NO es un pin del MCU: es un nodo externo, y el
// creador lo da de alta en el mapa si no existía.
inline Instancia& hilo_can(Netlist& nl, const char* id, const std::string& nodo,
                           double vdd = 3.3, double r_term = 1000.0) {
    nl.nodo_externo(nodo);           // el hilo no es un pin: hay que crearlo
    Instancia& i = nl.add("CanWire", id);
    i.pin("bus", nodo).par("vdd", vdd).par("r_term", r_term);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist&) -> ExtPartBase* {
        // El nodo lo ha creado ya el netlist: el hilo solo pone su terminador
        // encima. Esa es la diferencia con el constructor histórico, y es lo
        // que permite que otro componente se refiera al mismo nodo por nombre.
        return new CanWire(n[d.nodo_de("bus")], d.num("vdd", 3.3),
                           d.num("r_term", 1000.0), d.id.c_str());
    };
    return i;
}

// El transceptor. `hilo` es el IDENTIFICADOR de la instancia del hilo, no un
// nodo: es una referencia entre componentes, que un netlist tiene que admitir
// igual que admite conexiones a nodos.
inline Instancia& transceptor_can(Netlist& nl, const char* id,
                                  const std::string& txd, const std::string& rxd,
                                  const char* hilo, double vdd = 3.3) {
    // El hilo se declara ANTES, y de él sale el nodo al que va el terminal
    // `bus` del transceptor: la declaración queda completa —los tres terminales
    // con su nodo— aunque para construirlo haga falta además el OBJETO del hilo.
    const Instancia* w = nl.busca(hilo);
    Instancia& i = nl.add("CanTransceiver", id);
    i.pin("txd", txd).pin("rxd", rxd);
    if (w) i.pin("bus", w->nodo_de("bus"));
    i.ref("hilo", hilo).par("vdd", vdd);
    i.crea = [](const Instancia& d, NodeMap& n, Netlist& red) -> ExtPartBase* {
        CanWire* w = red.como<CanWire>(d.ref_de("hilo"));
        if (!w) {
            SC_REPORT_ERROR("netlist",
                (d.id + ": el hilo CAN '" + d.ref_de("hilo") +
                 "' no existe o todavia no se ha construido").c_str());
            return nullptr;
        }
        return new CanTransceiver(d.id.c_str(), n[d.nodo_de("txd")],
                                  n[d.nodo_de("rxd")], *w, d.num("vdd", 3.3));
    };
    return i;
}

// Otro controlador colgado del mismo hilo. No tiene ningún pin del MCU: todos
// sus terminales son del nodo externo. Un netlist tiene que poder describir eso
// —hay componentes de placa que el MCU ni ve— y por eso el `bus` va como
// conexión y no como parámetro.
inline Instancia& nodo_can(Netlist& nl, const char* id, const std::string& nodo,
                           const char* hilo, double bitrate = 500e3) {
    Instancia& i = nl.add("CanNode", id);
    i.pin("bus", nodo).ref("hilo", hilo).par("bitrate", bitrate);
    i.crea = [](const Instancia& d, NodeMap&, Netlist& red) -> ExtPartBase* {
        CanWire* w = red.como<CanWire>(d.ref_de("hilo"));
        if (!w) {
            SC_REPORT_ERROR("netlist",
                (d.id + ": el hilo CAN '" + d.ref_de("hilo") +
                 "' no existe o todavia no se ha construido").c_str());
            return nullptr;
        }
        return new CanNode(d.id.c_str(), *w, d.num("bitrate", 500e3));
    };
    return i;
}

} // namespace stm32
#endif // STM32_PARTS_NETLIST_PARTS_H
