// =============================================================================
// part_base.h — Base común de la LIBRERÍA DE COMPONENTES EXTERNOS al MCU
//
// (Paso 1 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso1.md.)
//
// Todo lo que se suelda fuera del encapsulado —un LED, un pulsador, un cristal,
// una SRAM, un PHY de Ethernet— comparte en este proyecto un único contrato
// eléctrico: recibe referencias a los nodos analógicos de los pines
// (analog_net_if) y se registra en ellos como driver Thevenin {V, Rout}. Esa
// uniformidad ya existía; lo que faltaba era hacerla EXPLÍCITA, y eso es lo que
// añade esta base:
//
//   1. TERMINALES CON NOMBRE. Cada pieza declara sus patillas por un nombre
//      nominal ("anodo", "sda", "mdio", "d0"...) y no por la posición de un
//      argumento del constructor. Un netlist externo —XML— tiene que poder
//      decir «el pin sda de U3 va al nodo PB9» sin conocer el orden en que el
//      constructor de esa clase recibe sus referencias.
//
//   2. UN INTERRUPTOR ÚNICO: set_enabled(bool). El proyecto ya tenía la idea,
//      pero repartida en cinco nombres distintos (set_enabled, set_attached,
//      set_conectada, conectar, soldar/soltar). Ahora todos son el mismo
//      método virtual, y los nombres antiguos se conservan como alias para no
//      tocar el banco de pruebas. Importa porque AnalogNet NO SABE DESREGISTRAR
//      drivers, solo ponerlos en alta impedancia [TODO I-03]: una pieza que
//      figure en el XML pero no en el SVG no se deja de construir —la
//      elaboración de SystemC es estática y no se puede— sino que se construye
//      DESCONECTADA, que eléctricamente es lo mismo.
//
//   3. UN INVENTARIO. Toda pieza construida se apunta en una lista global, y de
//      ahí sale el volcado del netlist: componentes, terminales y nodos. Es el
//      grafo que el paso 3 tendrá que validar (dos drivers de baja impedancia
//      con tensiones incompatibles sobre el mismo nodo, un pad no bonded con
//      algo colgado, un nodo sin camino a masa) y que el paso 4 dibujará.
//
// Lo que esta base NO hace, y conviene decirlo: no construye nada. La
// elaboración de SystemC es estática, así que la factoría que convierta una
// cadena "Led" del XML en un `new Led(...)` es trabajo del paso 3 y tiene que
// correr ANTES de sc_start().
// =============================================================================
#ifndef STM32_PARTS_PART_BASE_H
#define STM32_PARTS_PART_BASE_H

#include <systemc>
#include <algorithm>
#include <ostream>
#include <string>
#include <vector>
#include "../common/analog_net.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Un terminal: el nombre nominal de una patilla, el nodo al que va soldada y
// los drivers que la pieza ha registrado sobre ese nodo.
//
// `ids` puede estar VACÍO: es una patilla que solo escucha (la entrada NOE de
// una SRAM, el MDC de un PHY, el origen de una pista unidireccional). Para el
// netlist sigue siendo una conexión de pleno derecho —hay un hilo ahí—, aunque
// eléctricamente no aporte conductancia.
// ---------------------------------------------------------------------------
struct Terminal {
    std::string      nombre;      // "anodo", "sda", "d0"...
    analog_net_if*   net = nullptr;
    std::string      nodo;        // nombre del nodo: "PA5", "vdd", "can_bus"
    std::vector<int> ids;         // drivers de esta pieza sobre ese nodo
    bool             pasivo = false;   // true: solo escucha, no conduce
};

// ---------------------------------------------------------------------------
// Base de toda pieza externa. No es un sc_module: hay piezas que lo son
// (necesitan procesos) y piezas que no (una resistencia no tiene proceso), y
// obligar a las segundas a serlo solo añadiría objetos a la jerarquía.
// ---------------------------------------------------------------------------
class ExtPartBase {
public:
    // `nombre` es el identificador de INSTANCIA, el que usará el netlist para
    // decir «el pin sda de U3». Las piezas que son sc_module lo tienen gratis
    // —su nombre de módulo—; las que no lo son (una resistencia, un LED) pasan
    // nullptr y se les numera por tipo, porque un netlist con dos componentes
    // llamados igual no es un netlist.
    ExtPartBase(const char* tipo, const char* nombre)
        : tipo_(tipo ? tipo : "?") {
        if (nombre && *nombre) pieza_ = nombre;
        else                   pieza_ = tipo_ + "_" + std::to_string(++secuencia());
        inventario_mut().push_back(this);
    }
    virtual ~ExtPartBase() {
        std::vector<ExtPartBase*>& v = inventario_mut();
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
    }
    ExtPartBase(const ExtPartBase&)            = delete;
    ExtPartBase& operator=(const ExtPartBase&) = delete;

    // --- Identidad ----------------------------------------------------------
    const std::string& tipo()  const { return tipo_; }
    const std::string& pieza() const { return pieza_; }

    // --- Terminales ---------------------------------------------------------
    const std::vector<Terminal>& terminales() const { return term_; }
    const Terminal* terminal(const std::string& n) const {
        for (const Terminal& t : term_) if (t.nombre == n) return &t;
        return nullptr;
    }
    analog_net_if* nodo_de(const std::string& n) const {
        const Terminal* t = terminal(n);
        return t ? t->net : nullptr;
    }

    // --- Conexión -----------------------------------------------------------
    // Desconectar una pieza es DESOLDARLA: todos sus drivers quedan en alta
    // impedancia y el nodo se comporta como si no estuviera. Las piezas con
    // proceso propio deben además dejar de conducir en su bucle; para eso
    // pueden esperar sobre evento_conexion().
    bool conectada() const { return conectada_; }
    virtual void set_enabled(bool on) {
        conectada_ = on;
        if (!on) hiz_todo();
        ev_conex_.notify(sc_core::SC_ZERO_TIME);
    }
    const sc_core::sc_event& evento_conexion() const { return ev_conex_; }

    // --- Inventario y volcado del netlist -----------------------------------
    static const std::vector<ExtPartBase*>& inventario() { return inventario_mut(); }

    // Vuelca el grafo componente–terminal–nodo en el XML que propone el
    // esquema de QtSysC. Es la semilla del paso 2: el formato lo define lo que
    // el modelo realmente tiene, no la especulación.
    static void volcar_netlist(std::ostream& os) {
        os << "<placa>\n";
        for (const ExtPartBase* p : inventario()) {
            os << "  <componente tipo=\"" << p->tipo_ << "\" id=\"" << p->pieza_
               << "\" conectada=\"" << (p->conectada_ ? "si" : "no") << "\">\n";
            for (const Terminal& t : p->term_) {
                os << "    <pin nombre=\"" << t.nombre << "\" nodo=\"" << t.nodo
                   << "\"";
                if (t.pasivo) os << " pasivo=\"si\"";
                os << "/>\n";
            }
            os << "  </componente>\n";
        }
        os << "</placa>\n";
    }

protected:
    // Declara una patilla que CONDUCE: registra un driver sobre el nodo y
    // devuelve su identificador, para que el cuerpo de la pieza siga usándolo
    // exactamente igual que antes.
    int add_pin(const std::string& nombre, analog_net_if& n, const char* drv) {
        Terminal* t = busca_o_crea(nombre, n);
        const int id = n.register_driver(drv);
        t->ids.push_back(id);
        t->pasivo = false;
        return id;
    }
    // Un driver ADICIONAL sobre una patilla ya declarada: el pull-up de la
    // placa junto al driver de la pieza, por ejemplo.
    int add_drv(const std::string& nombre, const char* drv) {
        Terminal* t = busca(nombre);
        if (!t) return -1;
        const int id = t->net->register_driver(drv);
        t->ids.push_back(id);
        return id;
    }
    // Declara una patilla que SOLO ESCUCHA: no registra driver ninguno.
    void add_ref(const std::string& nombre, analog_net_if& n) {
        Terminal* t = busca_o_crea(nombre, n);
        if (t->ids.empty()) t->pasivo = true;
    }
    // Igual, pero admite nullptr: una patilla que este encapsulado no saca.
    void add_ref_opt(const std::string& nombre, analog_net_if* n) {
        if (n) add_ref(nombre, *n);
    }

    // Alta impedancia en todos los drivers de la pieza.
    void hiz_todo() {
        for (Terminal& t : term_)
            for (int id : t.ids) if (id >= 0) t.net->set_hiz(id);
    }

    // El nombre del nodo, tal y como lo verá el XML. Los nodos de pin se llaman
    // net_A5 dentro del modelo; fuera son PA5, que es como los llama el
    // esquemático y como los llamará el netlist.
    static std::string nombre_nodo(const analog_net_if& n) {
        const sc_core::sc_object* o = dynamic_cast<const sc_core::sc_object*>(&n);
        if (!o) return "?";
        std::string b = o->basename();
        if (b.rfind("net_", 0) == 0) return "P" + b.substr(4);
        return b;
    }

    bool conectada_ = true;

private:
    Terminal* busca(const std::string& nombre) {
        for (Terminal& t : term_) if (t.nombre == nombre) return &t;
        return nullptr;
    }
    Terminal* busca_o_crea(const std::string& nombre, analog_net_if& n) {
        Terminal* t = busca(nombre);
        if (t) return t;
        Terminal nuevo;
        nuevo.nombre = nombre;
        nuevo.net    = &n;
        nuevo.nodo   = nombre_nodo(n);
        term_.push_back(nuevo);
        return &term_.back();
    }
    static std::vector<ExtPartBase*>& inventario_mut() {
        static std::vector<ExtPartBase*> v;
        return v;
    }
    static unsigned& secuencia() { static unsigned n = 0; return n; }

    std::string           tipo_, pieza_;
    std::vector<Terminal> term_;
    sc_core::sc_event     ev_conex_;
};

} // namespace stm32
#endif // STM32_PARTS_PART_BASE_H
