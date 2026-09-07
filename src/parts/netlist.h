// =============================================================================
// netlist.h — EL NETLIST DE LA PLACA, en memoria
//
// (Paso 2 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso2.md.)
//
// El paso 1 hizo que cada pieza externa supiera decir por dónde está soldada:
// terminales con nombre y un inventario que se puede volcar. Eso permite mirar
// el modelo YA CONSTRUIDO y describirlo. Este paso hace lo contrario, que es lo
// que de verdad hacía falta: **describir primero y construir después**.
//
// Tres entidades, que son las mismas tres del XML de QtSysC:
//
//   NODO       un punto eléctrico. Los del MCU se llaman por su pad -"PA5",
//              "VDD", "NRST"-; los que no son pines -el hilo de un bus CAN, el
//              nudo entre un LED y su resistencia- se crean aquí. Un nodo es un
//              AnalogNet; nada más.
//   INSTANCIA  un componente: tipo, identificador, parámetros y la lista de sus
//              terminales con el nodo al que va cada uno.
//   CONEXIÓN   el par (terminal, nodo). Nominal, nunca posicional.
//
// LO QUE ESTE FICHERO NO HACE, Y ES DELIBERADO. No hay factoría por cadena: el
// XML dirá tipo="Led" y alguien tendrá que convertir esa cadena en un `new
// Led(...)`, pero eso exige un registro estático con auto-registro por tipo
// -C++ no tiene reflexión- y es el paso 3. Aquí cada instancia lleva su propio
// creador tipado, que le pone quien la declara (véase netlist_parts.h). El
// resultado es que la DECLARACIÓN ya es datos, que es lo que hacía falta para
// que el formato lo defina el modelo y no la especulación.
//
// Y una restricción que manda sobre todo lo demás: la elaboración de SystemC es
// ESTÁTICA. `construye()` tiene que llamarse antes de `sc_start()`, en el mismo
// sitio donde hoy `sc_main.cpp` hace sus `new`. No hay forma de añadir una
// pieza con la simulación en marcha, y por eso una pieza que el SVG no dibuje
// se construirá DESCONECTADA en vez de no construirse.
// =============================================================================
#ifndef STM32_PARTS_NETLIST_H
#define STM32_PARTS_NETLIST_H

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include "part_base.h"
#include "part_factory.h"
#include "xml_min.h"
#include "../pins/pin_mux.h"
#include "../pins/power_pads.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Los NODOS.
//
// Un nodo tiene nombre, un AnalogNet y una etiqueta de procedencia: si es un
// pin del MCU importa saber si el encapsulado lo saca, porque conectar algo a
// un pad no bonded es un error de placa y no de modelo.
// ---------------------------------------------------------------------------
struct Nodo {
    std::string    nombre;
    analog_net_if* net = nullptr;
    bool           es_pin = false;      // pad del MCU (frente a nodo externo)
    bool           bonded = true;       // sale al encapsulado LQFP100
};

class NodeMap {
public:
    // Da de alta un nodo que ya existe en el modelo.
    void registra(const std::string& nombre, analog_net_if& n,
                  bool es_pin = false, bool bonded = true) {
        Nodo nd;
        nd.nombre = nombre; nd.net = &n; nd.es_pin = es_pin; nd.bonded = bonded;
        m_[nombre] = nd;
    }

    // Da de alta TODOS los nodos del encapsulado: los 144 pads con su nombre de
    // esquemático (PA0..PI15) y los diez de alimentación y arranque. Los que el
    // LQFP100 no saca se registran igual, marcados: el netlist tiene que poder
    // decir «has conectado algo a PF3, y PF3 no existe en este encapsulado».
    void registra_mcu(PinMux& pm, PowerPads& pp) {
        char nm[8];
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                std::snprintf(nm, sizeof nm, "P%c%u", char('A' + p), i);
                registra(nm, pm.analog(p, i), true,
                         PinMux::is_bonded_lqfp100(p, i));
            }
        registra("VDD", pp.vdd);       registra("VSS", pp.vss);
        registra("VDDA", pp.vdda);     registra("VSSA", pp.vssa);
        registra("VREF+", pp.vref_p);  registra("VBAT", pp.vbat);
        registra("VCAP1", pp.vcap1);   registra("VCAP2", pp.vcap2);
        registra("NRST", pp.nrst);     registra("BOOT0", pp.boot0);
    }

    // Un nodo que NO es un pin: el hilo de un bus, el nudo entre dos
    // componentes externos. Lo crea y lo posee este mapa. Como todo lo demás,
    // tiene que ocurrir durante la elaboración.
    analog_net_if& externo(const std::string& nombre) {
        auto it = m_.find(nombre);
        if (it != m_.end()) return *it->second.net;
        propios_.emplace_back(new AnalogNet(nombre.c_str()));
        registra(nombre, *propios_.back(), false, true);
        return *propios_.back();
    }

    const Nodo* busca(const std::string& nombre) const {
        auto it = m_.find(nombre);
        return it == m_.end() ? nullptr : &it->second;
    }
    analog_net_if& operator[](const std::string& nombre) const {
        const Nodo* n = busca(nombre);
        if (!n) SC_REPORT_ERROR("netlist", ("nodo desconocido: " + nombre).c_str());
        return *n->net;
    }
    bool existe(const std::string& nombre) const { return busca(nombre) != nullptr; }
    unsigned size() const { return unsigned(m_.size()); }

private:
    std::map<std::string, Nodo>             m_;
    std::vector<std::unique_ptr<AnalogNet>> propios_;
};

// ---------------------------------------------------------------------------
// Una CONEXIÓN y una INSTANCIA.
// ---------------------------------------------------------------------------
struct Conexion {
    std::string pin;      // nombre nominal del terminal: "anodo", "sda", "d0"
    std::string nodo;
};

class Netlist;

struct Instancia {
    std::string tipo;     // "Led", "CanTransceiver"... el nombre de clase
    std::string id;       // identificador único de instancia
    std::vector<Conexion> pines;
    // Los parámetros van como TEXTO a propósito: un atributo de XML no tiene
    // tipo, y quien los lea tendrá que convertirlos igual. Mejor sufrir eso
    // ahora, con el compilador delante, que descubrirlo en el paso 3.
    std::map<std::string, std::string> params;
    // REFERENCIAS A OTRAS INSTANCIAS. No todo lo que une dos componentes es un
    // nodo: un transceptor CAN necesita el objeto del hilo, no solo el punto
    // eléctrico. Un netlist tiene que saber expresarlo, y además tiene que
    // saber que el referido se construye ANTES que quien lo refiere.
    std::map<std::string, std::string> refs;
    bool conectada = true;

    // El creador. En el paso 3 lo pondrá una factoría a partir de `tipo`; aquí
    // lo pone, tipado, quien declara la instancia.
    std::function<ExtPartBase*(const Instancia&, NodeMap&, Netlist&)> crea;

    // Relleno por Netlist::construye().
    ExtPartBase* pieza = nullptr;

    // --- Declaración fluida -------------------------------------------------
    Instancia& pin(const char* nombre, const std::string& nodo) {
        pines.push_back(Conexion{nombre, nodo});
        return *this;
    }
    Instancia& par(const char* clave, const std::string& valor) {
        params[clave] = valor;
        return *this;
    }
    Instancia& par(const char* clave, double valor) {
        std::ostringstream os; os << valor;
        params[clave] = os.str();
        return *this;
    }
    Instancia& ref(const char* clave, const std::string& id_instancia) {
        refs[clave] = id_instancia;
        return *this;
    }
    Instancia& desconectada() { conectada = false; return *this; }

    // --- Consulta -----------------------------------------------------------
    const std::string& nodo_de(const std::string& p) const {
        static const std::string vacio;
        for (const Conexion& c : pines) if (c.pin == p) return c.nodo;
        return vacio;
    }
    // Terminales INDEXADOS: d0, d1, d2... o rxd0, rxd1... Es como se describen
    // los buses en un netlist, y hay piezas —la SRAM, el sensor, el PHY— cuya
    // mitad de las patillas son de esta forma.
    unsigned n_indexados(const char* prefijo) const {
        unsigned k = 0;
        char t[24];
        for (;; ++k) {
            std::snprintf(t, sizeof t, "%s%u", prefijo, k);
            if (nodo_de(t).empty()) break;
        }
        return k;
    }
    double num(const char* clave, double omision) const {
        auto it = params.find(clave);
        return it == params.end() ? omision : std::atof(it->second.c_str());
    }
    std::string ref_de(const char* clave) const {
        auto it = refs.find(clave);
        return it == refs.end() ? std::string() : it->second;
    }
    std::string txt(const char* clave, const char* omision = "") const {
        auto it = params.find(clave);
        return it == params.end() ? std::string(omision) : it->second;
    }
    bool si(const char* clave, bool omision) const {
        auto it = params.find(clave);
        if (it == params.end()) return omision;
        return it->second == "si" || it->second == "1" || it->second == "true";
    }
};

// ---------------------------------------------------------------------------
// El NETLIST.
// ---------------------------------------------------------------------------
class Netlist {
public:
    Netlist() = default;
    // Las piezas se destruyen en orden INVERSO al de construcción. No es un
    // detalle: un transceptor guarda un puntero a su hilo de bus, y destruir
    // el hilo antes que el transceptor sería un uso después de liberar.
    ~Netlist() { libera(); }

    // Destruye las piezas AHORA. Hace falta cuando el netlist es miembro de un
    // módulo que en su destructor borra el MCU: las piezas guardan punteros a
    // los AnalogNet de los pines y los sueltan al morir, así que tienen que
    // morir antes que ellos. El destructor de un miembro corre DESPUÉS del
    // cuerpo del destructor que lo contiene, que es demasiado tarde.
    void libera() {
        for (size_t i = piezas_.size(); i-- > 0;) delete piezas_[i];
        piezas_.clear();
        for (Instancia& i : inst_) i.pieza = nullptr;
    }
    Netlist(const Netlist&)            = delete;
    Netlist& operator=(const Netlist&) = delete;

    // --- Declaración --------------------------------------------------------
    // Un nodo que NO es un pin del MCU y que, por tanto, hay que crear: el hilo
    // de un bus, el nudo entre dos componentes externos. Es el `<nodo id="..."/>`
    // del XML. Los pines no hace falta declararlos: ya existen.
    Netlist& nodo_externo(const std::string& nombre) {
        for (const std::string& n : externos_) if (n == nombre) return *this;
        externos_.push_back(nombre);
        return *this;
    }
    // Un nodo donde VARIOS componentes conducen a la vez y eso es correcto: un
    // bus de colector abierto, un cable en Y. Hay que decirlo, porque la
    // validación eléctrica no puede distinguir sola un bus de un cortocircuito
    // —en los dos casos hay dos piezas tirando del mismo punto— y callarse
    // ante los dos la dejaría sin servir para nada.
    Netlist& nodo_bus(const std::string& nombre) {
        for (const std::string& n : buses_) if (n == nombre) return *this;
        buses_.push_back(nombre);
        return *this;
    }
    bool es_bus(const std::string& nombre) const {
        for (const std::string& n : buses_) if (n == nombre) return true;
        return false;
    }
    bool es_externo(const std::string& nombre) const {
        for (const std::string& n : externos_) if (n == nombre) return true;
        return false;
    }
    const std::vector<std::string>& externos() const { return externos_; }

    // El creador lo pone la FACTORÍA a partir del nombre del tipo. Da igual
    // que la instancia venga de un ayudante tipado o de un fichero XML: por
    // aquí pasan las dos, y las dos salen sabiendo construirse. Si el tipo no
    // se conoce, `crea` se queda vacío y `valida()` lo dice con nombres.
    Instancia& add(const char* tipo, const char* id) {
        inst_.emplace_back();
        inst_.back().tipo = tipo;
        inst_.back().id   = id;
        if (const Fabrica::Creador* c = Fabrica::busca(tipo)) inst_.back().crea = *c;
        return inst_.back();
    }

    const std::list<Instancia>& instancias() const { return inst_; }
    Instancia* busca(const std::string& id) {
        for (Instancia& i : inst_) if (i.id == id) return &i;
        return nullptr;
    }
    const Instancia* busca(const std::string& id) const {
        for (const Instancia& i : inst_) if (i.id == id) return &i;
        return nullptr;
    }

    // --- Construcción (ANTES de sc_start) -----------------------------------
    // Devuelve cuántas piezas ha creado. El orden es el de declaración, que es
    // el que hace falta: una instancia puede referirse a otra ya construida
    // (un transceptor a su hilo de bus), y declararlas al revés es un error del
    // netlist que `valida()` detecta.
    unsigned construye(NodeMap& nodos) {
        // Primero los nodos que hay que crear; después las piezas que se
        // cuelgan de ellos. Al revés no puede ser.
        for (const std::string& e : externos_) nodos.externo(e);
        unsigned n = 0;
        for (Instancia& i : inst_) {
            if (i.pieza || !i.crea) continue;
            i.pieza = i.crea(i, nodos, *this);
            if (!i.pieza) continue;
            piezas_.push_back(i.pieza);
            i.pieza->set_enabled(i.conectada);
            ++n;
        }
        return n;
    }

    ExtPartBase* pieza(const std::string& id) const {
        const Instancia* i = busca(id);
        return i ? i->pieza : nullptr;
    }
    // Recupera una pieza con su tipo real. Devuelve nullptr si no está o si el
    // tipo no es el que se pide, que es justo lo que se quiere saber.
    template <class T> T* como(const std::string& id) const {
        return dynamic_cast<T*>(pieza(id));
    }

    // --- Validación de la DECLARACIÓN ---------------------------------------
    // Antes de construir, y sin simular. Devuelve la lista de problemas; vacía
    // si el netlist está bien. Es la semilla de lo que el paso 3 ampliará a la
    // validación eléctrica.
    std::vector<std::string> valida(const NodeMap& nodos) const {
        std::vector<std::string> err;
        std::map<std::string, unsigned> vistos;
        for (const Instancia& i : inst_) {
            if (i.id.empty()) { err.push_back("instancia sin identificador"); continue; }
            if (++vistos[i.id] > 1)
                err.push_back("identificador repetido: " + i.id);
            if (i.tipo.empty())
                err.push_back(i.id + ": instancia sin tipo");
            if (!i.crea)
                err.push_back(i.id + ": tipo desconocido '" + i.tipo +
                              "'. La fabrica conoce: " + Fabrica::tipos_como_texto());
            for (const auto& r : i.refs) {
                const Instancia* dest = busca(r.second);
                if (!dest) {
                    err.push_back(i.id + "." + r.first +
                                  ": referencia a un componente que no existe: " + r.second);
                    continue;
                }
                // El orden importa: `construye()` va en orden de declaracion, y
                // referirse a algo que todavia no se ha construido es un error
                // del netlist, no del modelo.
                if (!antes(r.second, i.id))
                    err.push_back(i.id + "." + r.first + ": " + r.second +
                                  " se declara despues, y hace falta antes");
            }
            std::map<std::string, unsigned> pins;
            for (const Conexion& c : i.pines) {
                if (++pins[c.pin] > 1)
                    err.push_back(i.id + ": terminal repetido: " + c.pin);
                const Nodo* nd = nodos.busca(c.nodo);
                if (!nd) {
                    // Todavía no existe: solo vale si está declarado como nodo
                    // externo, porque entonces lo creará `construye()`.
                    if (!es_externo(c.nodo))
                        err.push_back(i.id + "." + c.pin + ": nodo desconocido: " + c.nodo);
                    continue;
                }
                // Conectar algo a un pad que este encapsulado no saca es un
                // error de PLACA. El modelo lo toleraría —el AnalogNet existe—,
                // y por eso hay que decirlo aquí.
                if (nd->es_pin && !nd->bonded)
                    err.push_back(i.id + "." + c.pin + ": el pad " + c.nodo +
                                  " no sale al encapsulado LQFP100");
            }
        }
        return err;
    }

    // --- Validación ELÉCTRICA -----------------------------------------------
    // Esta corre DESPUÉS de construir, porque necesita saber qué terminal de
    // cada pieza conduce y cuál solo escucha, y eso lo sabe la pieza, no la
    // declaración. Sigue sin simular: es un recorrido del grafo.
    //
    // Detecta las dos formas en que un nodo puede estar mal montado:
    //
    //   CONFLICTO   dos o más piezas conectadas y CONDUCIENDO sobre el mismo
    //               nodo, sin que ese nodo esté declarado como bus. Es el
    //               cortocircuito de placa, y es la familia de fallos que más
    //               tiempo ha costado en este proyecto: aparecía como un aviso
    //               de sobrecorriente del pad en mitad de una simulación, y
    //               había que rastrearlo hacia atrás.
    //
    //   FLOTANTE    un nodo EXTERNO con piezas colgadas pero ninguna que
    //               conduzca. Su tensión no está definida, y como un AnalogNet
    //               conserva la última resuelta, lo que se lea de él será lo que
    //               dejó otro —que es exactamente por lo que un host de USB
    //               llegó a "ver" un dispositivo que no estaba enchufado—. En un
    //               PIN esto no es un aviso: al otro lado está el pad del MCU.
    //
    // El pad del MCU NO cuenta como conductor aquí: si conduce o no lo decide
    // el firmware en tiempo de ejecución, y este análisis es estático. Lo que
    // se comprueba es lo que la PLACA impone.
    std::vector<std::string> valida_electrica(const NodeMap& nodos) const {
        std::vector<std::string> err;
        // nodo -> piezas conectadas que conducen, y total de piezas colgadas
        std::map<std::string, std::vector<std::string>> activos;
        std::map<std::string, unsigned> colgados;
        for (const Instancia& i : inst_) {
            if (!i.pieza) continue;
            for (const Terminal& t : i.pieza->terminales()) {
                ++colgados[t.nodo];
                if (t.pasivo || t.ids.empty()) continue;
                if (!i.pieza->conectada()) continue;    // desoldada: no cuenta
                activos[t.nodo].push_back(i.id + "." + t.nombre);
            }
        }
        for (const auto& kv : activos) {
            if (kv.second.size() < 2 || es_bus(kv.first)) continue;
            std::string quien;
            for (const std::string& q : kv.second) {
                if (!quien.empty()) quien += " y ";
                quien += q;
            }
            err.push_back("nodo " + kv.first + ": conducen a la vez " + quien +
                          ". Si es un bus, declaralo con nodo_bus()");
        }
        for (const auto& kv : colgados) {
            if (activos.count(kv.first)) continue;
            // Un PIN no puede quedar flotante por culpa de la placa: al otro
            // lado esta el pad del MCU, que conduce o no segun lo que mande el
            // firmware. Que ninguna pieza externa lo gobierne es lo normal en
            // una entrada. El aviso solo tiene sentido en los nodos que no son
            // pines: ahi no hay nadie mas, y si nadie conduce, nadie conduce.
            const Nodo* nd = nodos.busca(kv.first);
            if (nd && nd->es_pin) continue;
            err.push_back("nodo " + kv.first + ": " + std::to_string(kv.second) +
                          " terminal(es) colgados y ninguno conduce; su tension "
                          "no esta definida");
        }
        return err;
    }

    // ¿`a` se declara antes que `b`?
    bool antes(const std::string& a, const std::string& b) const {
        for (const Instancia& i : inst_) {
            if (i.id == a) return true;
            if (i.id == b) return false;
        }
        return false;
    }

    // --- Volcado a XML ------------------------------------------------------
    // Escribe la DECLARACIÓN, no el modelo: esto es lo que un día leerá el
    // paso 3, y por eso lleva los nodos y los parámetros, que el volcado del
    // inventario (ExtPartBase::volcar_netlist) no puede conocer.
    void volcar_xml(std::ostream& os, const char* nombre_placa = "placa") const {
        os << "<placa nombre=\"" << xml_escapa(nombre_placa) << "\">\n";
        // Los nodos externos primero: son los que el lector tendrá que crear.
        std::map<std::string, bool> usados;
        for (const Instancia& i : inst_)
            for (const Conexion& c : i.pines) usados[c.nodo] = true;
        for (const auto& kv : usados) {
            os << "  <nodo id=\"" << kv.first << "\"";
            if (es_externo(kv.first)) os << " externo=\"si\"";
            if (es_bus(kv.first))     os << " bus=\"si\"";
            os << "/>\n";
        }
        for (const Instancia& i : inst_) {
            os << "  <componente tipo=\"" << i.tipo << "\" id=\"" << i.id << "\"";
            for (const auto& p : i.params)
                os << " " << p.first << "=\"" << xml_escapa(p.second) << "\"";
            if (!i.conectada) os << " conectada=\"no\"";
            os << ">\n";
            for (const Conexion& c : i.pines)
                os << "    <pin nombre=\"" << c.pin << "\" nodo=\"" << c.nodo
                   << "\"/>\n";
            for (const auto& r : i.refs)
                os << "    <ref nombre=\"" << r.first << "\" componente=\""
                   << r.second << "\"/>\n";
            os << "  </componente>\n";
        }
        os << "</placa>\n";
    }

private:
    // Lista y no vector: add() devuelve una referencia con la que se sigue
    // declarando (`.pin(...).par(...)`), y un vector que se reubica al crecer
    // la dejaría colgando. Una reserva suficiente bastaría; la lista lo
    // garantiza sin números mágicos.
    std::list<Instancia>       inst_;
    std::vector<std::string>   externos_;
    std::vector<std::string>   buses_;
    std::vector<ExtPartBase*>  piezas_;
};

} // namespace stm32
#endif // STM32_PARTS_NETLIST_H
