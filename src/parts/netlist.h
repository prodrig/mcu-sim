// =============================================================================
// netlist.h — EL NETLIST DE LA PLACA, en memoria
//
// (Paso 2 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f4xx/stm32f407vg_parts_paso2.md.)
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
#include <set>
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
// ---------------------------------------------------------------------------
// LOS NODOS DE ALIMENTACIÓN Y ARRANQUE de un MCU por su nombre: `VDD`,
// `u0.NRST`, `A/u0.BOOT0`. Son los diez que `NodeMap::registra_mcu()` da de
// alta además de los pads de puerto. Distingue mayúsculas, como el resto de
// nombres de nodo: `vdd` es un hilo de la placa, no la patilla del chip.
// ---------------------------------------------------------------------------
inline bool alim_desde_nombre(const std::string& s, std::string& mcu,
                              std::string& alim) {
    static const char* const ALIM[] = {"VDD", "VSS", "VDDA", "VSSA", "VREF+",
                                       "VBAT", "VCAP1", "VCAP2", "NRST", "BOOT0"};
    const size_t p = s.rfind('.');
    const std::string n = p == std::string::npos ? s : s.substr(p + 1);
    for (const char* a : ALIM)
        if (n == a) {
            if (p != std::string::npos && p == 0) return false;
            mcu  = p == std::string::npos ? std::string() : s.substr(0, p);
            alim = n;
            return true;
        }
    return false;
}

// ---------------------------------------------------------------------------
// LA GEOMETRÍA DE UN CONECTOR: filas x columnas, y cómo se numeran.
//
//   zigzag  el 1 y el 2 enfrentados, impares en una fila y pares en la otra:
//           un IDC, una cabecera de Raspberry Pi, un morpho de las Nucleo
//   filas   la primera fila entera (1..columnas), luego la segunda
//
// Solo importa al acoplar EN ESPEJO -dos placas cara a cara-, que da la
// vuelta a las filas (el 1 cae sobre el 2 en un 2xN) o, si solo hay una, a
// las columnas (el 1 cae sobre el último).
//
// LOS NOMBRES DE LOS PINES (`nombres="COM D1 D2 ..."`), si los tiene: el del
// pin k, o vacío si ese pin se sigue llamando por su número. Un pin con
// nombre es `ID.nombre` -`P1.COM`- y SOLO así: `P1.1` sería el pad PB1. El
// número sigue mandando en la geometría -el 1 es el de la izquierda, y al
// acoplar el 1 va con el 1-, y el nombre es como se le llama.
//
// UN JUMPER es un conector con un puente puesto (`puente="5V VCC"`): dos pines
// vecinos que la pieza de plástico une. Son el mismo nodo, como si un <hilo>
// los uniera dentro de la placa; `puente_a` y `puente_b` dicen cuáles (0 si
// no tiene puente).
// ---------------------------------------------------------------------------
inline bool es_conector(const std::string& tipo) {
    return tipo == "Conector" || tipo == "Jumper";
}

struct GeomConector {
    unsigned filas = 1, columnas = 0;
    bool     zigzag = true;
    std::vector<std::string> nombres;     // vacío: todos por número
    unsigned puente_a = 0, puente_b = 0;  // un Jumper: los dos pines unidos
    // El puente de un Jumper escrito como lo lleva el dibujo: `5V-VCC`, por
    // orden de pin; `no` sin puente (vease Netlist::variante)
    std::string puente() const {
        return puente_a ? pin_k(puente_a) + "-" + pin_k(puente_b) : std::string("no");
    }
    unsigned n() const { return filas * columnas; }
    // Cómo se llama el pin k: su nombre, o su número
    std::string pin_k(unsigned k) const {
        return k >= 1 && k <= nombres.size() && !nombres[k - 1].empty()
                   ? nombres[k - 1] : std::to_string(k);
    }
    // El nodo del pin k del conector `id`: `CN7.17` o `P1.COM`
    std::string nodo(const std::string& id, unsigned k) const { return id + "." + pin_k(k); }
    void fila_col(unsigned k, unsigned& f, unsigned& c) const {
        if (zigzag) { f = (k - 1) % filas;    c = (k - 1) / filas; }
        else        { f = (k - 1) / columnas; c = (k - 1) % columnas; }
    }
    unsigned pin(unsigned f, unsigned c) const {
        return zigzag ? c * filas + f + 1 : f * columnas + c + 1;
    }
    unsigned espejo(unsigned k) const {
        unsigned f = 0, c = 0;
        fila_col(k, f, c);
        if (filas >= 2) f = filas - 1 - f;
        else            c = columnas - 1 - c;
        return pin(f, c);
    }
};

// Un entero sin signo escrito entero: "17" sí, "17a", "-1" o "" no.
inline bool entero_estricto(const std::string& s, unsigned& v) {
    if (s.empty() || s.size() > 6) return false;
    unsigned long x = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        x = x * 10 + unsigned(c - '0');
    }
    v = unsigned(x);
    return true;
}

struct Nodo {
    std::string    nombre;
    analog_net_if* net = nullptr;
    bool           es_pin = false;      // pad del MCU (frente a nodo externo)
    bool           bonded = true;       // ¿sale al encapsulado de SU chip?
    // El nombre de ese encapsulado, para que el error pueda decir cuál. Con
    // varios MCU en la placa no tiene por qué ser el mismo para todos, así que
    // va por nodo y no en una constante global.
    const char*    enc = nullptr;
};

class NodeMap {
public:
    // Da de alta un nodo que ya existe en el modelo.
    //
    // Dar de alta DOS VECES el mismo nombre con dos AnalogNet distintos es un
    // error, y hasta ahora era un error MUDO: el segundo pisaba al primero y
    // todo seguía. Es exactamente lo que pasaría llamando dos veces a
    // `registra_mcu()` sin prefijo —los 154 nodos del segundo MCU tapando los
    // del primero—, y por eso conviene que suene. Volver a registrar el mismo
    // nombre con el MISMO nodo sí vale: es idempotente, y ocurre de verdad
    // cuando un pad forma parte de un nodo compartido que ya estaba dado de
    // alta por su nombre de placa. [doc/multi_mcu.md, §7.2]
    void registra(const std::string& nom, analog_net_if& n,
                  bool es_pin = false, bool bonded = true,
                  const char* enc = nullptr) {
        const std::string nombre = nombre_canonico_pad(nom);
        const auto it = m_.find(nombre);
        if (it != m_.end() && it->second.net != &n) {
            SC_REPORT_ERROR("netlist",
                ("nodo duplicado: '" + nombre + "' ya esta dado de alta con otro "
                 "AnalogNet. Si la placa lleva mas de un MCU, registralos con "
                 "prefijos distintos: registra_mcu(\"u0\", ...)").c_str());
            return;
        }
        Nodo nd;
        nd.nombre = nombre; nd.net = &n; nd.es_pin = es_pin; nd.bonded = bonded;
        nd.enc = enc;
        m_[nombre] = nd;
    }

    // Da de alta TODOS los nodos del encapsulado: los 144 pads con su nombre de
    // esquemático (PA0..PI15) y los diez de alimentación y arranque. Los que el
    // LQFP100 no saca se registran igual, marcados: el netlist tiene que poder
    // decir «has conectado algo a PF3, y PF3 no existe en este encapsulado».
    //
    // `prefijo` es el identificador del MCU. Vacío —el caso de siempre, y el de
    // una placa con un solo chip— deja los nombres desnudos: `PD12`. Con
    // "u0" salen `u0.PD12`, que es lo que hace falta en cuanto hay dos y el
    // nombre desnudo deja de designar un pin concreto.
    // [doc/multi_mcu.md, §3]
    void registra_mcu(const std::string& prefijo, PinMux& pm, PowerPads& pp) {
        const std::string pre = prefijo.empty() ? std::string() : prefijo + ".";
        // Este chip se sube a una placa. Es aqui -y no al construirlo- donde
        // empieza a poder hacer ambiguo el nombre `PA5`, y por eso es aqui
        // donde se cuenta. [common/nombres_nodo.h]
        apunta_mcu_en_placa(&pm);
        char nm[8];
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                std::snprintf(nm, sizeof nm, "P%c%u", char('A' + p), i);
                // Quien sabe qué sale al plástico es el mux del propio chip,
                // no una función global: dos MCU de la misma placa pueden
                // llevar encapsulados distintos.
                registra(pre + nm, pm.analog(p, i), true,
                         pm.encapsulado().bonded(p, i),
                         pm.encapsulado().nombre);
            }
        registra(pre + "VDD", pp.vdd);       registra(pre + "VSS", pp.vss);
        registra(pre + "VDDA", pp.vdda);     registra(pre + "VSSA", pp.vssa);
        registra(pre + "VREF+", pp.vref_p);  registra(pre + "VBAT", pp.vbat);
        registra(pre + "VCAP1", pp.vcap1);   registra(pre + "VCAP2", pp.vcap2);
        registra(pre + "NRST", pp.nrst);     registra(pre + "BOOT0", pp.boot0);
    }
    void registra_mcu(PinMux& pm, PowerPads& pp) {
        registra_mcu(std::string(), pm, pp);
    }

    // Un nodo que NO es un pin: el hilo de un bus, el nudo entre dos
    // componentes externos. Lo crea y lo posee este mapa. Como todo lo demás,
    // tiene que ocurrir durante la elaboración.
    analog_net_if& externo(const std::string& nom) {
        const std::string nombre = nombre_canonico_pad(nom);
        auto it = m_.find(nombre);
        if (it != m_.end()) return *it->second.net;
        // El punto es el separador de jerarquía de SystemC: `CN7.17` se crea
        // como `CN7_17` y se apunta su nombre de verdad (AnalogNet::nombre_esq)
        std::string sc = nombre;
        for (char& c : sc) if (c == '.') c = '_';
        propios_.emplace_back(new AnalogNet(sc.c_str()));
        if (sc != nombre) propios_.back()->nombre_esq = nombre;
        registra(nombre, *propios_.back(), false, true);
        return *propios_.back();
    }

    const Nodo* busca(const std::string& nom) const {
        auto it = m_.find(nombre_canonico_pad(nom));
        return it == m_.end() ? nullptr : &it->second;
    }
    analog_net_if& operator[](const std::string& nombre) const {
        const Nodo* n = busca(nombre);
        if (!n) SC_REPORT_ERROR("netlist", ("nodo desconocido: " + nombre).c_str());
        return *n->net;
    }
    bool existe(const std::string& nombre) const { return busca(nombre) != nullptr; }
    // Cuántos NOMBRES hay dados de alta.
    unsigned size() const { return unsigned(m_.size()); }
    // Cuántos nodos ELÉCTRICOS distintos. No es lo mismo: un mismo AnalogNet
    // puede tener varios nombres —el desnudo y el cualificado cuando hay un
    // solo MCU, o un puente y cada uno de los pads que lo forman—, y decir
    // «308 nodos» de una placa que tiene 154 es mentir con una cifra exacta.
    unsigned n_nodos() const {
        std::set<const analog_net_if*> v;
        for (const auto& kv : m_) v.insert(kv.second.net);
        return unsigned(v.size());
    }

private:
    std::map<std::string, Nodo>             m_;
    std::vector<std::unique_ptr<AnalogNet>> propios_;
};

// ---------------------------------------------------------------------------
// UN MCU DE LA PLACA.
//
// Es la cuarta entidad del formato, junto a `<nodo>`, `<componente>` y `<ref>`,
// y es deliberadamente distinta de las otras: un componente SE CONECTA a nodos y
// un MCU LOS APORTA. Declarar sus 144 pads como `<pin>` sería absurdo, así que
// el MCU no es un componente más [doc/multi_mcu.md, §9].
//
// Como todo lo demás en este fichero, aquí solo hay DATOS: cadenas y números.
// Convertir `tipo="STM32F407VG"` en un objeto es trabajo de quien construye
// —hoy `sim_main.cpp`, mañana una factoría de MCUs paralela a la de piezas— y
// tiene que ocurrir en la elaboración.
// ---------------------------------------------------------------------------
struct DeclMcu {
    std::string tipo;              // "STM32F407VG"
    std::string id;                // "u0": prefijo de sus nodos y nombre de módulo
    std::string firmware;          // imagen que se le carga (vacío: ninguna)
    // Cómo se llega a su DAP:
    //   "pines" (omisión) el núcleo EXPONE SWCLK/SWDIO y el stub se cuelga de
    //                     ellos por fuera, como un ST-LINK. Es el modo fiel.
    //   "dap"             el núcleo RESERVA los cinco pines de depuración y
    //                     crea dentro un stub que habla con el DAP por llamada
    //                     de función. Es el modo rápido.
    std::string depuracion = "pines";
    unsigned    puerto_gdb = 0;    // 0: no se abre ningún puerto TCP
    // El ENCAPSULADO de este chip, que es lo que decide qué pads salen de
    // verdad. Lo rellena quien resuelve el `tipo` contra el catálogo de MCUs
    // —antes de validar, porque de él depende el error «ese pad no sale»— y por
    // omisión es el LQFP100, el del F407VG.
    const Encapsulado* enc = &ENC_LQFP100;
};

// ---------------------------------------------------------------------------
// UNA PLACA DE UN <sistema>: su id -el prefijo de todo lo suyo, `A/LD2`-, su
// nombre y, si no va escrita dentro, el fichero del que salió.
// ---------------------------------------------------------------------------
// EL DIBUJO DE UNA PLACA (doc/analisis-uso-ilustraciones.md). La placa dice
// cuál es -`ilustracion="x.svg"`, relativo a su fichero- o, si no lo dice, es
// el SVG que se llame como ella y esté a su lado. Y, si el dibujo no se quiere
// tocar, una TABLA DE ENLACES: qué elemento del SVG es cada pieza. Aquí no se
// lee el SVG: eso es de la ventana; aquí solo se sabe dónde está y se manda.
struct EnlaceIlustracion {
    std::string pieza;        // el nombre en SU placa: "LD2", nunca "N/LD2"
    std::string elemento;     // el id en el SVG
    std::string efecto;       // "", "brillo", "hundido", "giro" o "ninguno"
};
struct Ilustracion {
    std::string declarada;    // lo que dice el XML, tal cual; vacío si no dice
    std::string ruta;         // dónde se busca, ya resuelta; vacía si en ningún sitio
    std::vector<EnlaceIlustracion> enlaces;
};

struct PlacaDeSistema {
    std::string id, nombre, fichero;
    Ilustracion ilustracion;
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
        // Canonizado a la ENTRADA: `PD12`, `PD.12`, `P3.12` y `P312` son el
        // mismo punto, y a partir de aqui se llama `PD12` y nada mas.
        pines.push_back(Conexion{nombre, nombre_canonico_pad(nodo)});
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

    // --- Los MCUs de la placa -----------------------------------------------
    // Se declaran ANTES que nada, porque son quienes aportan los nodos de pin.
    // Ninguno declarado significa «un STM32F407VG implícito con nombres de nodo
    // desnudos», que es el comportamiento de siempre y lo que hace que las
    // placas escritas hasta hoy sigan valiendo sin tocarlas.
    Netlist& add_mcu(const DeclMcu& m) { mcus_.push_back(m); return *this; }
    const std::vector<DeclMcu>& mcus() const { return mcus_; }
    // Le pone a un chip declarado el encapsulado que le corresponde por su
    // `tipo`. Lo llama quien resuelve el tipo contra el catálogo, y tiene que
    // hacerlo ANTES de validar, porque de aquí sale el error «ese pad no sale
    // al encapsulado»: con el encapsulado equivocado ese error se daría al
    // revés, que es la peor manera posible de equivocarse.
    void fija_encapsulado(const std::string& id, const Encapsulado* e) {
        for (DeclMcu& m : mcus_) if (m.id == id) m.enc = e;
    }
    // Y el del MCU IMPLÍCITO: el que se monta cuando la placa no declara
    // ninguno. Sin esto, `sim placa.xml --mcu STM32F405RG` validaría los pads
    // contra el LQFP100 y aceptaría un PE2 que en un LQFP64 no existe.
    void fija_encapsulado_implicito(const Encapsulado* e) {
        if (e) enc_implicito_ = e;
    }
    const Encapsulado& encapsulado_implicito() const { return *enc_implicito_; }
    const DeclMcu* mcu(const std::string& id) const {
        for (const DeclMcu& m : mcus_) if (m.id == id) return &m;
        return nullptr;
    }
    // Para que el <sistema> diga el firmware o el puerto de un chip de una
    // placa sin tocar el fichero de la placa (<mcu ref="A/u0" firmware=...>).
    DeclMcu* mcu_mut(const std::string& id) {
        for (DeclMcu& m : mcus_) if (m.id == id) return &m;
        return nullptr;
    }

    // --- Las placas de un <sistema> -----------------------------------------
    // Vacío en una placa suelta, que es la forma de saber si esto es un
    // sistema. Solo lo usan el volcado y los mensajes: lo demás ve un netlist
    // plano con los nombres ya cualificados (`A/LD2`).
    void pon_placas(std::vector<PlacaDeSistema> v) { placas_ = std::move(v); }
    // El dibujo de una placa suelta; en un sistema, el de cada placa va en
    // su PlacaDeSistema
    void pon_ilustracion(Ilustracion i) { ilustracion_ = std::move(i); }
    const Ilustracion& ilustracion() const { return ilustracion_; }
    const std::vector<PlacaDeSistema>& placas() const { return placas_; }
    bool es_sistema() const { return !placas_.empty(); }
    // Cuántos hay DE VERDAD: ninguno declarado es uno implícito -el de
    // `--mcu`-, salvo que la placa vaya SIN MCU, que entonces son cero.
    unsigned n_mcus_efectivos() const {
        if (sin_mcu_) return 0u;
        return mcus_.empty() ? 1u : unsigned(mcus_.size());
    }
    // Una placa SIN MCU: ni <mcu> ni `--mcu`. No hay pads; todo nodo es de la
    // placa, y un nombre de pad es un error (vease `sim_main.cpp`). Por eso
    // cada <nodo> declarado pasa a ser externo sin tener que decirlo: no hay
    // nada mas que pueda ser, y exigir `externo="si"` en todos seria ruido.
    void pon_sin_mcu(bool s = true) {
        sin_mcu_ = s;
        if (s) for (const std::string& n : declarados_) nodo_externo(n);
    }
    bool sin_mcu() const { return sin_mcu_; }
    // `--mcu` manda sobre el tipo de un <mcu> declarado: se cambia aquí para
    // que la placa que se vuelca -a la ventana, en T_PLACA- diga lo mismo que
    // lo que se monta.
    void cambia_tipo_mcu(const std::string& id, const std::string& tipo) {
        for (DeclMcu& m : mcus_) if (m.id == id) m.tipo = tipo;
    }

    // --- Declaración --------------------------------------------------------
    // Un nodo que NO es un pin del MCU y que, por tanto, hay que crear: el hilo
    // de un bus, el nudo entre dos componentes externos. Es el `<nodo id="..."/>`
    // del XML. Los pines no hace falta declararlos: ya existen.
    // Todo <nodo> que aparece en el XML, externo o no. Con MCU solo es
    // documentacion; sin MCU, es la lista de nodos de la placa.
    Netlist& nodo_declarado(const std::string& nom) {
        const std::string nombre = nombre_canonico_pad(nom);
        for (const std::string& n : declarados_) if (n == nombre) return *this;
        declarados_.push_back(nombre);
        return *this;
    }
    Netlist& nodo_externo(const std::string& nom) {
        const std::string nombre = nombre_canonico_pad(nom);
        for (const std::string& n : externos_) if (n == nombre) return *this;
        externos_.push_back(nombre);
        return *this;
    }
    // Un nodo donde VARIOS componentes conducen a la vez y eso es correcto: un
    // bus de colector abierto, un cable en Y. Hay que decirlo, porque la
    // validación eléctrica no puede distinguir sola un bus de un cortocircuito
    // —en los dos casos hay dos piezas tirando del mismo punto— y callarse
    // ante los dos la dejaría sin servir para nada.
    Netlist& nodo_bus(const std::string& nom) {
        const std::string nombre = nombre_canonico_pad(nom);
        for (const std::string& n : buses_) if (n == nombre) return *this;
        buses_.push_back(nombre);
        return *this;
    }
    // Un nodo que ES varios pads a la vez: un puente de placa entre dos pines,
    // o un hilo que comparten dos MCUs. No es un componente ni un acoplador:
    // es UN AnalogNet con los dos pads registrados en él, de modo que la
    // superposición los resuelve juntos, sin retardo y en los dos sentidos.
    //
    // Tiene que declararse porque el pad no puede enterarse después: `Pad::net`
    // es un `sc_port` y se ata en el constructor del MCU. Por eso este dato lo
    // consume `cableado_desde_netlist()` ANTES de construir nada.
    //
    // Un nodo con `une` es siempre externo: no lo crea ningún pad, lo crea la
    // placa. [doc/multi_mcu.md, §4.3]
    Netlist& nodo_une(const std::string& nom,
                      const std::vector<std::string>& pads) {
        const std::string nombre = nombre_canonico_pad(nom);
        std::vector<std::string> c;
        c.reserve(pads.size());
        for (const std::string& s : pads) c.push_back(nombre_canonico_pad(s));
        uniones_[nombre] = c;
        nodo_externo(nombre);
        return *this;
    }
    const std::map<std::string, std::vector<std::string>>& uniones() const {
        return uniones_;
    }
    const std::vector<std::string>* union_de(const std::string& nom) const {
        const auto it = uniones_.find(nombre_canonico_pad(nom));
        return it == uniones_.end() ? nullptr : &it->second;
    }
    bool es_bus(const std::string& nom) const {
        const std::string nombre = nombre_canonico_pad(nom);
        for (const std::string& n : buses_) if (n == nombre) return true;
        return false;
    }
    // Un nodo compartido puede estar declarado como bus por su nombre de placa
    // o por el de cualquiera de los pads que lo forman. Los dos quieren decir
    // lo mismo, y quien escribe la placa no tiene por qué adivinar cuál mira la
    // validación.
    bool es_bus_efectivo(const std::string& nombre) const {
        if (es_bus(nombre)) return true;
        if (const std::vector<std::string>* u = union_de(nombre))
            for (const std::string& s : *u) if (es_bus(s)) return true;
        return false;
    }
    bool es_externo(const std::string& nom) const {
        const std::string nombre = nombre_canonico_pad(nom);
        for (const std::string& n : externos_) if (n == nombre) return true;
        return false;
    }
    const std::vector<std::string>& externos() const { return externos_; }

    // --- NODOS QUE SON EL MISMO -------------------------------------------
    // Un HILO dice que dos nombres de nodo son el mismo punto eléctrico: el pin
    // 3 de un conector y el 5 del de enfrente cuando un cable los cruza, o dos
    // pads de dos placas. Un ACOPLE es la versión al por mayor: dos conectores
    // del mismo número de pines enchufados, el pin k de uno sobre el k del
    // otro -o, en espejo, sobre el que le cae enfrente-. Los dos son
    // DECLARACIÓN: no construyen nada hasta que `resuelve_alias()` los
    // convierte en nodos, y eso tiene que pasar antes de construir el MCU por
    // lo mismo que `une` [cableado_desde_netlist].
    //
    // Un acople puede juntar MÁS DE DOS conectores: es una PILA, como la de
    // PC/104, donde cada placa lleva un conector pasante y el pin k es el mismo
    // en todas. En espejo solo dos: tres placas no pueden estar cara a cara.
    struct Acople {
        std::vector<std::string> conectores;   // dos o más: "A/J1", "B/J1"...
        bool espejo = false;
        // "A/J1 con B/J1", o "A/J1, B/J1 y C/J1", para los mensajes
        std::string texto() const {
            if (conectores.size() == 2) return conectores[0] + " con " + conectores[1];
            std::string t;
            for (size_t k = 0; k < conectores.size(); ++k)
                t += (k ? (k + 1 == conectores.size() ? " y " : ", ") : "") + conectores[k];
            return t;
        }
    };
    Netlist& hilo(const std::string& a, const std::string& b) {
        hilos_.emplace_back(nombre_canonico_pad(a), nombre_canonico_pad(b));
        return *this;
    }
    Netlist& acopla(const std::string& a, const std::string& b, bool espejo) {
        acoples_.push_back(Acople{{a, b}, espejo});
        return *this;
    }
    Netlist& acopla(const std::vector<std::string>& conectores, bool espejo = false) {
        acoples_.push_back(Acople{conectores, espejo});
        return *this;
    }
    const std::vector<std::pair<std::string, std::string>>& hilos() const { return hilos_; }
    const std::vector<Acople>& acoples() const { return acoples_; }
    // ¿Hay algo que resolver? Sin conectores, hilos ni acoples, nada cambia, y
    // `resuelve_alias()` no toca la placa: las de siempre salen idénticas.
    bool hay_alias() const {
        if (!hilos_.empty() || !acoples_.empty()) return true;
        for (const Instancia& i : inst_) if (es_conector(i.tipo)) return true;
        return false;
    }
    // El nombre con el que quedó un nodo tras resolver: el de su clase.
    std::string canonico(const std::string& nom) const {
        const std::string n = nombre_canonico_pad(nom);
        const auto it = alias_.find(n);
        return it == alias_.end() ? n : it->second;
    }

    // La geometría de un conector declarado, o "" y el problema.
    static std::string geometria(const Instancia& i, GeomConector& g) {
        unsigned f = 1, c = 0;
        const std::string tf = i.txt("filas", "1"), tc = i.txt("columnas");
        if (!entero_estricto(tf, f) || f < 1)
            return "filas=\"" + tf + "\" no vale: un entero, 1 o mas";
        if (tc.empty()) return "falta columnas=\"N\"";
        if (!entero_estricto(tc, c) || c < 1)
            return "columnas=\"" + tc + "\" no vale: un entero, 1 o mas";
        if (f * c > 1000)
            return std::to_string(f * c) + " pines son demasiados (el tope es 1000)";
        const std::string nu = i.txt("numeracion", "zigzag");
        if (nu != "zigzag" && nu != "filas")
            return "numeracion=\"" + nu + "\" no vale: zigzag o filas";
        g.filas = f; g.columnas = c; g.zigzag = (nu == "zigzag");
        // Los nombres: uno por pin, en orden, y `-` para el que no lo lleva
        g.nombres.clear();
        if (i.params.count("nombres")) {
            std::istringstream is(i.txt("nombres"));
            std::string t;
            std::set<std::string> vistos;
            while (is >> t) {
                if (t == "-") { g.nombres.push_back(std::string()); continue; }
                // Letras, cifras, '_', '.' y '+', con al menos una letra -un
                // numero solo seria otro pin- y sin un '.' en los extremos:
                // `COM`, `D1`, `5V`, `3.3V`, `+3V3`
                bool bien = t.front() != '.' && t.back() != '.', letra = false;
                for (char ch : t) {
                    const bool l = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
                    letra = letra || l;
                    bien = bien && (l || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
                                    ch == '+');
                }
                if (!bien || !letra)
                    return "nombres: '" + t + "' no vale: letras, cifras, '_', '.' o '+', "
                           "con al menos una letra (y '-' para un pin sin nombre)";
                if (!vistos.insert(t).second)
                    return "nombres: '" + t + "' esta dos veces";
                g.nombres.push_back(t);
            }
            if (g.nombres.size() != g.n())
                return "nombres: hay " + std::to_string(g.nombres.size()) + " y el "
                       "conector tiene " + std::to_string(g.n()) + " pines; uno por pin, "
                       "y '-' para el que no lo lleva";
        }
        // El puente de un Jumper: dos pines vecinos, por nombre o por número,
        // o "no". Es obligatorio: un jumper sin decir como esta puesto es la
        // mitad de la placa sin decir
        g.puente_a = g.puente_b = 0;
        if (i.tipo == "Jumper") {
            if (!i.params.count("puente"))
                return "falta puente=\"A B\": los dos pines que une el puente (o "
                       "puente=\"no\" si no lleva)";
            const std::string pt = i.txt("puente");
            if (pt != "no") {
                std::istringstream is(pt);
                std::vector<std::string> ps;
                std::string t;
                while (is >> t) ps.push_back(t);
                if (ps.size() != 2)
                    return "puente=\"" + pt + "\" no vale: dos pines, \"A B\", o \"no\"";
                unsigned k[2] = {0, 0};
                for (int j = 0; j < 2; ++j) {
                    for (unsigned q = 1; q <= g.n() && !k[j]; ++q)
                        if (g.pin_k(q) == ps[j]) k[j] = q;
                    if (!k[j])
                        return "puente: '" + ps[j] + "' no es ningun pin del jumper";
                }
                if (k[0] == k[1]) return "puente: un pin no se une consigo mismo";
                unsigned f0 = 0, c0 = 0, f1 = 0, c1 = 0;
                g.fila_col(k[0], f0, c0);
                g.fila_col(k[1], f1, c1);
                const bool vecinos = (f0 == f1 && (c0 + 1 == c1 || c1 + 1 == c0)) ||
                                     (c0 == c1 && (f0 + 1 == f1 || f1 + 1 == f0));
                if (!vecinos)
                    return "puente: " + ps[0] + " y " + ps[1] + " no estan uno al lado "
                           "del otro, y un puente solo une dos pines vecinos";
                g.puente_a = std::min(k[0], k[1]);
                g.puente_b = std::max(k[0], k[1]);
            }
        }
        return std::string();
    }

    // LA VARIANTE de una pieza: lo que su dibujo tiene que enseñar segun como
    // esta montada. Hoy solo la tiene un Jumper -su puente, `5V-VCC` o `no`-,
    // y la usa quien manda el dibujo a la ventana: de los elementos
    // `ID@valor` del SVG deja solo el de la variante (svg_variantes.h).
    // Vacia: la pieza no tiene variantes.
    static std::string variante(const Instancia& i) {
        if (i.tipo != "Jumper") return std::string();
        GeomConector g;
        if (!geometria(i, g).empty()) return std::string();
        return g.puente();
    }

    // EL PAD FÍSICO detrás de un nombre: "u0:37" para un pad de puerto,
    // "u0:VDD" para uno de alimentación, "" si no es un pad. Dos nombres de un
    // mismo pad -`PA5` y `u0.PA5` con un solo MCU- dan la misma clave, que es
    // lo que hace falta para no unir un pad consigo mismo.
    std::string clave_pad(const std::string& s, bool& alim, std::string& err) const {
        std::string pref, id;
        unsigned p = 0, i = 0;
        alim = false;
        if (pad_desde_nombre(s, pref, p, i)) {
            err = resuelve_pad(s, id, p, i);
            return err.empty() ? id + ":" + std::to_string(p * N_PORT_PINS + i)
                               : std::string();
        }
        std::string a;
        if (!alim_desde_nombre(s, pref, a)) return std::string();
        alim = true;
        err = resuelve_alim(s, id, a);
        return err.empty() ? id + ":" + a : std::string();
    }
    // `VDD` o `u0.NRST` -> a qué MCU y qué pad de alimentación. Las mismas
    // reglas que `resuelve_pad`. Devuelve "" si vale, o el problema.
    std::string resuelve_alim(const std::string& s, std::string& id_mcu,
                              std::string& alim) const {
        std::string pref;
        if (!alim_desde_nombre(s, pref, alim))
            return "'" + s + "' no es un pad de alimentacion ni de arranque del MCU";
        if (sin_mcu_)
            return "'" + s + "' es una patilla de MCU, y la placa no lleva ninguno";
        if (pref.empty()) {
            if (n_mcus_efectivos() > 1)
                return "'" + s + "' es ambiguo, hay " + std::to_string(mcus_.size()) +
                       " MCUs. Escribe " + lista_cualificada(s);
            id_mcu = mcus_.empty() ? std::string() : mcus_.front().id;
        } else {
            if (!mcu(pref))
                return "'" + s + "': no hay ningun MCU llamado '" + pref +
                       "'. La placa declara: " + lista_mcus();
            id_mcu = pref;
        }
        return std::string();
    }

    // --- RESOLVER: de conectores, hilos y acoples a nodos -------------------
    // Junta en CLASES los nombres que son el mismo punto -cada pin de conector
    // con el nodo al que va soldado, cada pin con el de enfrente, cada hilo-
    // y deja la placa escrita con UN nombre por clase:
    //
    //   ningún pad      el nombre declarado (`vcc`), o el del primer pin de
    //                   conector; el netlist lo crea como nodo externo
    //   un pad          ese pad: todo lo demás pasa a llamarse `PA5`
    //   dos o más pads  un nodo compartido, igual que un `<nodo une>`: de
    //                   puerto o de alimentación y arranque (VDD, VSS,
    //                   NRST, BOOT0...), que `PowerPads` ata con une_alim
    //
    // Reescribe las patillas de todas las piezas, los nodos externos, los de
    // bus y los compartidos. Un pin de conector al aire se queda con su
    // nombre, `CN7.17`, y es un nodo externo más. Devuelve los problemas;
    // vacío si todo bien. Sin conectores, hilos ni acoples no hace NADA.
    std::vector<std::string> resuelve_alias() {
        std::vector<std::string> err;
        if (!hay_alias()) return err;
        std::vector<std::pair<std::string, std::string>> pares;
        std::map<std::string, GeomConector> conectores;
        std::set<std::string> pines_conector;

        // 1. Cada conector: sus N pines, con su nodo o al aire
        for (Instancia& i : inst_) {
            if (!es_conector(i.tipo)) continue;
            GeomConector g;
            const std::string e = geometria(i, g);
            if (!e.empty()) { err.push_back(i.id + ": " + e); continue; }
            const unsigned n = g.n();
            {
                // Que ningun pin se llame como un pad o una patilla de
                // alimentacion: `P1.1` es PB1, y `P1.VDD` el VDD de un MCU P1.
                // Se mira cada uno, por su numero o por su nombre.
                std::string pr, al, malo;
                unsigned pp = 0, qq = 0;
                for (unsigned k = 1; k <= n && malo.empty(); ++k) {
                    const std::string nm = g.nodo(i.id, k);
                    if (pad_desde_nombre(nm, pr, pp, qq))
                        malo = "'" + nm + "' seria el nombre de un pad (" +
                               nombre_canonico_pad(nm) + ")";
                    else if (alim_desde_nombre(nm, pr, al))
                        malo = "'" + nm + "' seria la patilla " + al + " de un MCU '" +
                               pr + "'";
                }
                if (!malo.empty()) {
                    err.push_back(i.id + ": un conector no puede llamarse asi: " + malo +
                                  (g.nombres.empty() ? std::string()
                                                     : ". Cambia el id o ese nombre"));
                    continue;
                }
            }
            // Un pin se suelda por su numero o, si lo tiene, por su nombre
            std::map<std::string, unsigned> por_nombre;
            for (unsigned k = 1; k <= n; ++k)
                if (g.pin_k(k) != std::to_string(k)) por_nombre[g.pin_k(k)] = k;
            std::map<unsigned, std::string> dado;
            bool mal = false;
            for (const Conexion& c : i.pines) {
                unsigned k = 0;
                const auto pn = por_nombre.find(c.pin);
                if (pn != por_nombre.end()) k = pn->second;
                else if (!entero_estricto(c.pin, k) || k < 1 || k > n) {
                    std::string cuales = "los de este conector van del 1 al " +
                                         std::to_string(n);
                    if (!por_nombre.empty()) {
                        cuales += ", o por su nombre:";
                        for (unsigned j = 1; j <= n; ++j)
                            if (g.pin_k(j) != std::to_string(j)) cuales += " " + g.pin_k(j);
                    }
                    err.push_back(i.id + ": el pin '" + c.pin + "' no existe; " + cuales);
                    mal = true;
                    continue;
                }
                if (dado.count(k)) {
                    err.push_back(i.id + ": el pin " + c.pin + " aparece dos veces");
                    mal = true;
                } else {
                    dado[k] = c.nodo;
                }
            }
            if (mal) continue;
            conectores[i.id] = g;
            std::vector<Conexion> todos;
            for (unsigned k = 1; k <= n; ++k) {
                const std::string nm = g.nodo(i.id, k);
                pines_conector.insert(nm);
                const auto it = dado.find(k);
                if (it != dado.end() && it->second != nm) pares.emplace_back(nm, it->second);
                todos.push_back(Conexion{g.pin_k(k), nm});
            }
            // El puente de un Jumper: sus dos pines, el mismo nodo
            if (g.puente_a) pares.emplace_back(g.nodo(i.id, g.puente_a), g.nodo(i.id, g.puente_b));
            i.pines = todos;
        }

        // 2. Los acoples: pin a pin, en espejo, o una pila de varios
        std::map<std::string, std::string> acoplado;   // conector -> su acople
        for (const Acople& a : acoples_) {
            const std::string que = "acopla " + a.texto() + ": ";
            const std::vector<std::string>& cs = a.conectores;
            bool bien = true;
            if (cs.size() < 2) {
                err.push_back(que + "hacen falta al menos dos conectores");
                continue;
            }
            if (a.espejo && cs.size() != 2) {
                err.push_back(que + "en espejo, dos y solo dos: tres placas no pueden "
                              "estar cara a cara");
                continue;
            }
            std::set<std::string> vistos;
            for (const std::string& x : cs) {
                if (!vistos.insert(x).second) {
                    err.push_back(que + x + " aparece dos veces: un conector no se "
                                  "acopla consigo mismo");
                    bien = false;
                    continue;
                }
                if (conectores.count(x)) continue;
                const Instancia* i = busca(x);
                err.push_back(que + (i ? "'" + x + "' es un " + i->tipo + ", no un Conector"
                                       : "no hay ningun conector llamado '" + x + "'"));
                bien = false;
            }
            if (!bien) continue;
            for (const std::string& x : cs) {
                const auto it = acoplado.find(x);
                if (it != acoplado.end()) {
                    err.push_back(que + x + " ya esta en otro acople (" + it->second +
                                  "). Para enchufar varias placas a la vez, como en una "
                                  "pila PC/104, van todas en el mismo <acopla "
                                  "conectores=\"...\">");
                    bien = false;
                }
            }
            if (!bien) continue;
            const GeomConector& g0 = conectores[cs[0]];
            for (size_t k = 1; k < cs.size() && bien; ++k) {
                const GeomConector& gk = conectores[cs[k]];
                if (gk.n() != g0.n()) {
                    err.push_back(que + "no tienen los mismos pines (" + cs[0] + " tiene " +
                                  std::to_string(g0.n()) + " y " + cs[k] + " " +
                                  std::to_string(gk.n()) + ")");
                    bien = false;
                } else if (a.espejo && (gk.filas != g0.filas || gk.columnas != g0.columnas ||
                                        gk.zigzag != g0.zigzag)) {
                    err.push_back(que + "en espejo hace falta la misma forma en los dos "
                                  "(filas, columnas y numeracion)");
                    bien = false;
                }
            }
            if (!bien) continue;
            for (const std::string& x : cs) acoplado[x] = a.texto();
            for (size_t c = 1; c < cs.size(); ++c)
                for (unsigned k = 1; k <= g0.n(); ++k)
                    pares.emplace_back(g0.nodo(cs[0], k),
                                       conectores[cs[c]].nodo(cs[c], a.espejo ? g0.espejo(k) : k));
        }

        // 3. Los hilos: los dos extremos tienen que ser nodos de verdad
        std::set<std::string> conocidos(pines_conector.begin(), pines_conector.end());
        for (const std::string& n : declarados_) conocidos.insert(n);
        for (const std::string& n : externos_)   conocidos.insert(n);
        for (const auto& u : uniones_)           conocidos.insert(u.first);
        for (const Instancia& i : inst_)
            for (const Conexion& c : i.pines) conocidos.insert(c.nodo);
        for (const auto& h : hilos_) {
            bool bien = true;
            for (const std::string* x : {&h.first, &h.second}) {
                bool al = false;
                std::string e;
                const std::string k = clave_pad(*x, al, e);
                if (!e.empty()) { err.push_back("hilo " + h.first + " - " + h.second + ": " + e); bien = false; }
                else if (k.empty() && !conocidos.count(*x)) {
                    err.push_back("hilo " + h.first + " - " + h.second + ": '" + *x +
                                  "' no es ningun nodo: ni un pin de MCU, ni un pin de "
                                  "conector, ni un nodo declarado o usado por una pieza");
                    bien = false;
                }
            }
            if (bien && h.first == h.second)
                err.push_back("hilo " + h.first + " - " + h.second + ": un hilo de un "
                              "nodo a si mismo no une nada");
            else if (bien) pares.push_back(h);
        }

        // 4. Los nodos compartidos de siempre entran en el mismo juego
        for (const auto& u : uniones_)
            for (const std::string& p : u.second) pares.emplace_back(u.first, p);
        if (!err.empty()) return err;

        // 5. Las clases: unión-búsqueda sobre los nombres
        std::map<std::string, std::string> padre;
        std::vector<std::string> orden;
        std::function<std::string(const std::string&)> raiz = [&](const std::string& x) {
            std::string r = x;
            while (padre[r] != r) r = padre[r];
            std::string y = x;
            while (padre[y] != r) { const std::string z = padre[y]; padre[y] = r; y = z; }
            return r;
        };
        auto alta = [&](const std::string& x) {
            if (padre.count(x)) return;
            padre[x] = x;
            orden.push_back(x);
        };
        for (const auto& pr : pares) {
            alta(pr.first);
            alta(pr.second);
            const std::string ra = raiz(pr.first), rb = raiz(pr.second);
            if (ra != rb) padre[rb] = ra;
        }
        std::map<std::string, std::vector<std::string>> clases;
        std::vector<std::string> orden_clases;
        for (const std::string& n : orden) {
            const std::string r = raiz(n);
            if (!clases.count(r)) orden_clases.push_back(r);
            clases[r].push_back(n);
        }
        const std::set<std::string> declarados(declarados_.begin(), declarados_.end());
        std::map<std::string, std::string> rep_de;
        std::map<std::string, std::vector<std::string>> nuevas_uniones;
        std::vector<std::string> nuevos_externos;
        for (const std::string& r : orden_clases) {
            const std::vector<std::string>& c = clases[r];
            std::vector<std::string> pads;
            std::set<std::string> claves;
            bool mal = false;
            for (const std::string& n : c) {
                bool al = false;
                std::string e;
                const std::string k = clave_pad(n, al, e);
                if (!e.empty()) { err.push_back("nodo " + n + ": " + e); mal = true; continue; }
                if (k.empty()) continue;
                if (claves.insert(k).second) pads.push_back(n);
            }
            if (mal) continue;
            std::string rep;
            if (pads.size() == 1) {
                rep = pads[0];
            } else {
                // Un nombre de la placa: primero uno declarado, luego cualquier
                // otro que no sea un pin de conector, y si no, el primer pin.
                for (int pasada = 0; pasada < 3 && rep.empty(); ++pasada)
                    for (const std::string& n : c) {
                        bool al = false;
                        std::string e;
                        if (!clave_pad(n, al, e).empty()) continue;
                        const bool es_pin = pines_conector.count(n) != 0;
                        if ((pasada == 0 && declarados.count(n) && !es_pin) ||
                            (pasada == 1 && !es_pin) || pasada == 2) { rep = n; break; }
                    }
                if (rep.empty()) rep = "une:" + pads[0];
                // Pads de puerto o de alimentación, da igual: todos van al
                // mismo nodo compartido (Cableado::une y une_alim).
                if (pads.size() >= 2) nuevas_uniones[rep] = pads;
                nuevos_externos.push_back(rep);
            }
            for (const std::string& n : c) rep_de[n] = rep;
        }
        if (!err.empty()) return err;

        // 6. Reescribir la placa con un nombre por clase
        auto rp = [&](const std::string& n) {
            const auto it = rep_de.find(n);
            return it == rep_de.end() ? n : it->second;
        };
        for (Instancia& i : inst_)
            for (Conexion& c : i.pines) c.nodo = rp(c.nodo);
        std::vector<std::string> ex;
        auto mete = [](std::vector<std::string>& v, const std::string& x) {
            for (const std::string& y : v) if (y == x) return;
            v.push_back(x);
        };
        for (const std::string& e : externos_) if (!rep_de.count(e)) mete(ex, e);
        for (const std::string& e : nuevos_externos) mete(ex, e);
        for (const std::string& n : pines_conector) if (!rep_de.count(n)) mete(ex, n);
        externos_ = ex;
        std::vector<std::string> v;
        for (const std::string& d : declarados_) mete(v, rp(d));
        declarados_ = v;
        v.clear();
        for (const std::string& b : buses_) mete(v, rp(b));
        buses_ = v;
        uniones_ = nuevas_uniones;
        alias_ = rep_de;
        return err;
    }

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
            i.pieza->pon_id(i.id);       // el nombre de la placa, no `Crystal_1`
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
        // Los MCUs, primero de todo: son quienes aportan los nodos, así que un
        // <mcu> mal escrito invalida todo lo que venga detrás.
        {
            std::map<std::string, unsigned> ids;
            std::map<unsigned, std::string> puertos;
            for (const DeclMcu& m : mcus_) {
                if (m.id.empty()) { err.push_back("<mcu> sin identificador"); continue; }
                if (++ids[m.id] > 1)
                    err.push_back("mcu " + m.id + ": identificador repetido");
                if (m.tipo.empty())
                    err.push_back("mcu " + m.id + ": sin tipo");
                if (m.depuracion != "pines" && m.depuracion != "dap")
                    err.push_back("mcu " + m.id + ": depuracion debe ser 'pines' "
                                  "o 'dap', no '" + m.depuracion + "'");
                // Dos stubs en el mismo puerto TCP no es un aviso: es que el
                // segundo no llega a escuchar y el fallo aparece más tarde,
                // como un GDB que se conecta al chip equivocado.
                if (m.puerto_gdb) {
                    const auto it = puertos.find(m.puerto_gdb);
                    if (it != puertos.end())
                        err.push_back("mcu " + m.id + ": el puerto de GDB " +
                                      std::to_string(m.puerto_gdb) +
                                      " ya lo usa " + it->second);
                    else puertos[m.puerto_gdb] = m.id;
                }
            }
        }
        // Los nodos compartidos: un `une` mal escrito no se manifiesta como un
        // error sino como un puente que no está, así que hay que comprobarlo.
        std::map<std::string, std::string> pad_de;   // pad -> nodo que lo reclama
        for (const auto& u : uniones_) {
            if (u.second.size() < 2)
                err.push_back("nodo " + u.first + ": une necesita al menos dos "
                              "pads; con uno solo el nodo ya es del pad");
            for (const std::string& s : u.second) {
                // La clave es el pad FÍSICO, no como esté escrito: con un solo
                // MCU llamado u0, `PB9` y `u0.PB9` son el mismo pad y ponerlos
                // en dos puentes distintos tiene que seguir siendo un error. Un
                // pad de alimentación o de arranque (`u0.NRST`) vale igual.
                bool al = false;
                std::string e;
                const std::string clave = clave_pad(s, al, e);
                if (clave.empty() && e.empty())
                    e = "'" + s + "' no es un pad del MCU (se esperaba algo como "
                        "PD12, u0.PD12 o u0.NRST)";
                if (!e.empty()) { err.push_back("nodo " + u.first + ": " + e); continue; }
                const auto it = pad_de.find(clave);
                if (it == pad_de.end())      pad_de[clave] = u.first;
                else if (it->second == u.first)
                    err.push_back("nodo " + u.first + ": el pad " + s +
                                  " aparece dos veces; unirlo consigo mismo no "
                                  "une nada");
                else
                    err.push_back("el pad " + s + " esta en dos nodos a la vez: " +
                                  it->second + " y " + u.first);
            }
        }
        std::map<std::string, unsigned> vistos;
        for (const Instancia& i : inst_) {
            if (i.id.empty()) { err.push_back("instancia sin identificador"); continue; }
            if (++vistos[i.id] > 1)
                err.push_back("identificador repetido: " + i.id);
            if (i.tipo.empty())
                err.push_back(i.id + ": instancia sin tipo");
            if (!i.crea) {
                // Y si lo unico que falla son las mayusculas -`led` por
                // `Led`-, decirlo, que es el error de verdad frecuente. El
                // resto de la lista sigue saliendo: el que se equivoca aqui
                // tiene un editor de texto delante y necesita ver que hay.
                std::string m = i.id + ": tipo desconocido '" + i.tipo + "'.";
                const std::string cerca = Fabrica::busca_laxo(i.tipo);
                if (!cerca.empty())
                    m += " Se escribe '" + cerca + "': el tipo distingue "
                         "mayusculas.";
                m += " La fabrica conoce: " + Fabrica::tipos_como_texto() +
                     ". `sim --help TIPO` cuenta lo que hace cada uno.";
                err.push_back(m);
            }
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
                    if (es_externo(c.nodo)) continue;
                    // Y si lo que ha escrito TIENE FORMA DE PAD, el aviso útil
                    // no es «no existe» sino POR QUÉ no existe: con dos MCUs,
                    // `PD12` a secas ya no designa un pin concreto, y `u2.PA0`
                    // nombra un chip que la placa no lleva. Para cualquier otra
                    // cosa —un hilo que nadie declaró— el aviso de siempre.
                    std::string pref, id_mcu;
                    unsigned pp = 0, qq = 0;
                    if (pad_desde_nombre(c.nodo, pref, pp, qq)) {
                        const std::string e = resuelve_pad(c.nodo, id_mcu, pp, qq);
                        if (!e.empty()) { err.push_back(i.id + "." + c.pin + ": " + e); continue; }
                    }
                    err.push_back(i.id + "." + c.pin + ": nodo desconocido: " + c.nodo);
                    continue;
                }
                // Conectar algo a un pad que este encapsulado no saca es un
                // error de PLACA. El modelo lo toleraría —el AnalogNet existe—,
                // y por eso hay que decirlo aquí.
                if (nd->es_pin && !nd->bonded)
                    err.push_back(i.id + "." + c.pin + ": el pad " + c.nodo +
                                  " no sale al encapsulado " +
                                  (nd->enc ? nd->enc : "de este MCU"));
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
        // nodo -> piezas conectadas que conducen, y total de piezas colgadas.
        //
        // EL NODO ES EL HILO, NO SU NOMBRE. Se agrupa por el AnalogNet al que
        // va cada terminal, y el nombre solo se usa para decirlo. Agrupando por
        // nombre, dos chips con su pulsador de RESET -dos Nucleo en un
        // sistema- daban «nodo nrst: conducen a la vez A/B2.pin y B/B2.pin»:
        // los pads de alimentacion de los dos se llaman `nrst` por dentro,
        // pero son dos nodos que no se tocan.
        struct Clave {
            const void* net;
            std::string nombre;
            bool operator<(const Clave& o) const {
                return net != o.net ? net < o.net : nombre < o.nombre;
            }
        };
        std::map<Clave, std::vector<std::string>> activos_c, rieles_c;
        std::map<Clave, unsigned> colgados_c;
        for (const Instancia& i : inst_) {
            if (!i.pieza) continue;
            for (const Terminal& t : i.pieza->terminales()) {
                if (t.paso) continue;                   // un pin de conector
                const Clave k{t.net, t.net ? std::string() : t.nodo};
                ++colgados_c[k];
                if (t.pasivo || t.ids.empty()) continue;
                if (!i.pieza->conectada()) continue;    // desoldada: no cuenta
                activos_c[k].push_back(i.id + "." + t.nombre);
                if (t.riel) rieles_c[k].push_back(i.id + "." + t.nombre);
            }
        }
        // De vuelta a nombres, para decirlo: el de cualquiera de sus terminales.
        // Dos hilos con el mismo nombre se quedan separados.
        std::map<const void*, std::string> nombre_de_net;
        for (const Instancia& i : inst_)
            if (i.pieza)
                for (const Terminal& t : i.pieza->terminales())
                    if (t.net && !nombre_de_net.count(t.net)) nombre_de_net[t.net] = t.nodo;
        auto nombre = [&](const Clave& k) {
            return k.net ? nombre_de_net[k.net] : k.nombre;
        };
        // Y los hilos que son bus, por su hilo tambien: un bus declarado en
        // una placa -el comun de una barra, `P1.COM`- que un <hilo> lleva a
        // VDD queda dentro del nodo del pad, que por dentro se llama `vdd`, y
        // por el nombre no se encontraba.
        std::set<const void*> nets_bus;
        for (const std::string& b : buses_)
            if (const Nodo* nd = nodos.busca(b))
                if (nd->net) nets_bus.insert(nd->net);
        // UN RAÍL NO ES UN CORTOCIRCUITO. Un nodo con una fuente (Fuente, Gnd)
        // y lo que cuelga de ella -LEDs, resistencias, pulsadores- es lo
        // normal: la fuente lo sostiene y los demás tiran de ella. Dos fuentes
        // en el mismo nodo, en cambio, se pelean aunque sea un bus.
        for (const auto& kv : rieles_c) {
            if (kv.second.size() < 2) continue;
            std::string quien;
            for (const std::string& q : kv.second) {
                if (!quien.empty()) quien += " y ";
                quien += q;
            }
            err.push_back("nodo " + nombre(kv.first) + ": dos fuentes a la vez, " + quien +
                          ". Una fuente sostiene un nodo; dos se pelean por el");
        }
        for (const auto& kv : activos_c) {
            if (kv.second.size() < 2 || es_bus_efectivo(nombre(kv.first)) ||
                (kv.first.net && nets_bus.count(kv.first.net)))
                continue;
            if (rieles_c.count(kv.first)) continue;     // un raíl con sus cargas
            std::string quien;
            for (const std::string& q : kv.second) {
                if (!quien.empty()) quien += " y ";
                quien += q;
            }
            err.push_back("nodo " + nombre(kv.first) + ": conducen a la vez " + quien +
                          ". Si es un bus, declaralo con nodo_bus()");
        }
        for (const auto& kv : colgados_c) {
            if (activos_c.count(kv.first)) continue;
            const std::string nom = nombre(kv.first);
            // Un PIN no puede quedar flotante por culpa de la placa: al otro
            // lado esta el pad del MCU, que conduce o no segun lo que mande el
            // firmware. Que ninguna pieza externa lo gobierne es lo normal en
            // una entrada. El aviso solo tiene sentido en los nodos que no son
            // pines: ahi no hay nadie mas, y si nadie conduce, nadie conduce.
            const Nodo* nd = nodos.busca(nom);
            if (nd && nd->es_pin) continue;
            // Un nodo COMPARTIDO tampoco puede quedar flotante por culpa de la
            // placa: es un pad -o dos-, y quien conduce ahi lo decide el
            // firmware. Se registra con su nombre de placa y no como pin, asi
            // que hay que reconocerlo por la declaracion.
            if (union_de(nom)) continue;
            err.push_back("nodo " + nom + ": " + std::to_string(kv.second) +
                          " terminal(es) colgados y ninguno conduce; su tension "
                          "no esta definida");
        }
        return err;
    }

    // --- Resolución de un nombre de PAD -------------------------------------
    // `PD12` o `u0.PD12` -> a qué MCU y a qué (puerto, pin). Devuelve "" si el
    // nombre vale, o el problema explicado. Es el sitio donde vive la regla de
    // compatibilidad entera [doc/multi_mcu.md, §3]:
    //
    //   ningún <mcu>   -> uno implícito; solo valen los nombres desnudos
    //   un <mcu>       -> valen los dos, `PD12` y `u0.PD12`
    //   dos o más      -> solo con prefijo; el desnudo es un error que dice
    //                     cuáles son los candidatos, que es la mitad útil del
    //                     aviso
    std::string resuelve_pad(const std::string& s, std::string& id_mcu,
                             unsigned& port, unsigned& pin) const {
        std::string pref;
        if (!pad_desde_nombre(s, pref, port, pin))
            return "'" + s + "' no es un pad del MCU (se esperaba algo como "
                   "PD12 o u0.PD12)";
        if (sin_mcu_)
            return "'" + s + "' es un pin de MCU, y la placa no lleva ninguno: "
                   "declara <mcu tipo=\"...\" id=\"u0\"/> o pasa --mcu";
        if (pref.empty()) {
            if (n_mcus_efectivos() > 1)
                return "'" + s + "' es ambiguo, hay " +
                       std::to_string(mcus_.size()) + " MCUs. Escribe " +
                       lista_cualificada(s);
            id_mcu = mcus_.empty() ? std::string() : mcus_.front().id;
        } else {
            if (!mcu(pref))
                return "'" + s + "': no hay ningun MCU llamado '" + pref +
                       "'. La placa declara: " + lista_mcus();
            id_mcu = pref;
        }
        // El encapsulado del MCU al que pertenece ESE pad, que con varios
        // chips en la placa no tiene por qué ser el mismo para todos.
        const DeclMcu* d = id_mcu.empty() ? nullptr : mcu(id_mcu);
        const Encapsulado& e = (d && d->enc) ? *d->enc : *enc_implicito_;
        if (!e.bonded(port, pin))
            return "el pad " + s + " no sale al encapsulado " +
                   std::string(e.nombre) + " de " +
                   (d ? d->tipo : std::string("STM32F407VG"));
        return std::string();
    }
    std::string lista_mcus() const {
        if (sin_mcu_) return "(ninguno: la placa no lleva MCU)";
        if (mcus_.empty()) return "(ninguno; hay uno implicito)";
        std::string s;
        for (const DeclMcu& m : mcus_) { if (!s.empty()) s += ", "; s += m.id; }
        return s;
    }
    // "u0.PD12 o u1.PD12", para el error de ambigüedad.
    std::string lista_cualificada(const std::string& pad) const {
        std::string s;
        for (size_t k = 0; k < mcus_.size(); ++k) {
            if (k) s += (k + 1 == mcus_.size()) ? " o " : ", ";
            s += mcus_[k].id + "." + pad;
        }
        return s;
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
    //
    // Un <sistema> se vuelca APLANADO: el sistema con sus placas, y detrás todo
    // lo de todas con sus nombres cualificados (`A/LD2`, `A/u0.PA5`) y los
    // conectores ya resueltos -cada pin con el nodo que le tocó-. Los acoples
    // y los hilos salen al final, para que quien lo lea sepa qué se enchufó
    // con qué; ya están aplicados. Es lo que recibe mcu-sim-gui en T_PLACA
    // desde la versión 2 del protocolo. Con `como_sistema` a false sale lo
    // mismo con la raíz de siempre, <placa>, y sin lo que solo tiene un
    // sistema: es lo que se manda a una ventana de la versión 1.
    //
    // CADA PLACA SE DESCRIBE ENTERA en su <placa id>, para que quien lo lea
    // pueda DIBUJAR el sistema sin deducir nada de los prefijos: cuántas
    // piezas lleva, sus chips y sus conectores con su forma, y en cada acople
    // e hilo, qué placas une. Nada de eso cambia la simulación; es la
    // descripción del montaje.
    std::string placa_de(const std::string& id) const {
        const size_t b = id.find('/');
        if (b == std::string::npos) return std::string();
        const std::string p = id.substr(0, b);
        for (const PlacaDeSistema& x : placas_) if (x.id == p) return p;
        return std::string();
    }
    void volcar_xml(std::ostream& os, const char* nombre_placa = "placa",
                    bool como_sistema = true) const {
        const bool sis = como_sistema && es_sistema();
        // El dibujo, como se escribe en la placa: `ilustracion="x.svg"` si lo
        // declara, y la tabla de enlaces si la tiene. Solo lo DECLARADO: el
        // fichero que se encuentra sin decirlo no es de la placa, y el volcado
        // de una placa se vuelve a leer como placa.
        auto tabla = [&](const Ilustracion& il, const char* sangria) {
            if (il.enlaces.empty()) return;
            os << sangria << "<ilustracion>\n";
            for (const EnlaceIlustracion& e : il.enlaces) {
                os << sangria << "  <enlace pieza=\"" << xml_escapa(e.pieza)
                   << "\" elemento=\"" << xml_escapa(e.elemento) << "\"";
                if (!e.efecto.empty()) os << " efecto=\"" << xml_escapa(e.efecto) << "\"";
                os << "/>\n";
            }
            os << sangria << "</ilustracion>\n";
        };
        os << (sis ? "<sistema" : "<placa") << " nombre=\"" << xml_escapa(nombre_placa)
           << "\"";
        if (!es_sistema() && !ilustracion_.declarada.empty())
            os << " ilustracion=\"" << xml_escapa(ilustracion_.declarada) << "\"";
        os << ">\n";
        if (!es_sistema()) tabla(ilustracion_, "  ");
        if (sis)
            for (const PlacaDeSistema& p : placas_) {
                unsigned n = 0;
                for (const Instancia& i : inst_) n += placa_de(i.id) == p.id ? 1u : 0u;
                os << "  <placa id=\"" << xml_escapa(p.id) << "\" nombre=\""
                   << xml_escapa(p.nombre) << "\"";
                if (!p.fichero.empty()) os << " fichero=\"" << xml_escapa(p.fichero) << "\"";
                if (!p.ilustracion.declarada.empty())
                    os << " ilustracion=\"" << xml_escapa(p.ilustracion.declarada) << "\"";
                os << " piezas=\"" << n << "\">\n";
                tabla(p.ilustracion, "    ");
                for (const DeclMcu& m : mcus_)
                    if (placa_de(m.id) == p.id)
                        os << "    <mcu ref=\"" << xml_escapa(m.id) << "\" tipo=\""
                           << xml_escapa(m.tipo) << "\"/>\n";
                for (const Instancia& i : inst_) {
                    if (!es_conector(i.tipo) || placa_de(i.id) != p.id) continue;
                    GeomConector g;
                    if (!geometria(i, g).empty()) continue;
                    os << "    <conector ref=\"" << xml_escapa(i.id) << "\" filas=\""
                       << g.filas << "\" columnas=\"" << g.columnas << "\" numeracion=\""
                       << (g.zigzag ? "zigzag" : "filas") << "\"";
                    if (!g.nombres.empty()) {
                        os << " nombres=\"";
                        for (unsigned k = 1; k <= g.n(); ++k)
                            os << (k > 1 ? " " : "")
                               << (g.nombres[k - 1].empty() ? "-" : g.nombres[k - 1]);
                        os << "\"";
                    }
                    for (size_t k = 0; k < acoples_.size(); ++k)
                        for (const std::string& c : acoples_[k].conectores)
                            if (c == i.id) os << " acople=\"" << k << "\"";
                    os << "/>\n";
                }
                os << "  </placa>\n";
            }
        // Los MCUs primero: son quienes aportan los nodos, y el lector los
        // necesita antes de poder resolver un solo nombre de pin.
        for (const DeclMcu& m : mcus_) {
            os << "  <mcu tipo=\"" << xml_escapa(m.tipo) << "\" id=\""
               << xml_escapa(m.id) << "\"";
            if (!m.firmware.empty())
                os << " firmware=\"" << xml_escapa(m.firmware) << "\"";
            if (m.depuracion != "pines")
                os << " depuracion=\"" << xml_escapa(m.depuracion) << "\"";
            if (m.puerto_gdb) os << " puerto_gdb=\"" << m.puerto_gdb << "\"";
            os << "/>\n";
        }
        // Los nodos externos después: son los que el lector tendrá que crear.
        std::map<std::string, bool> usados;
        for (const Instancia& i : inst_)
            for (const Conexion& c : i.pines) usados[c.nodo] = true;
        // Los nodos compartidos salen SIEMPRE, aunque no haya ninguna pieza
        // colgada de ellos: un puente entre dos pines es placa aunque no lleve
        // nada soldado encima, y sin esta línea el fichero releído no lo tendría.
        for (const auto& u : uniones_) usados[u.first] = true;
        for (const auto& kv : usados) {
            os << "  <nodo id=\"" << kv.first << "\"";
            if (es_externo(kv.first)) os << " externo=\"si\"";
            if (es_bus(kv.first))     os << " bus=\"si\"";
            if (const std::vector<std::string>* u = union_de(kv.first)) {
                os << " une=\"";
                for (size_t k = 0; k < u->size(); ++k)
                    os << (k ? " " : "") << (*u)[k];
                os << "\"";
            }
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
        if (sis) {
            // Un acople, con sus conectores y las placas que une. Con dos,
            // además a= y b=, que es como lo lee una ventana de la primera
            // versión de los sistemas.
            for (size_t k = 0; k < acoples_.size(); ++k) {
                const Acople& a = acoples_[k];
                std::string cs, ps;
                for (const std::string& c : a.conectores) {
                    cs += (cs.empty() ? "" : " ") + c;
                    ps += (ps.empty() ? "" : " ") + placa_de(c);
                }
                os << "  <acopla n=\"" << k << "\" conectores=\"" << xml_escapa(cs)
                   << "\" placas=\"" << xml_escapa(ps) << "\"";
                if (a.conectores.size() == 2)
                    os << " a=\"" << xml_escapa(a.conectores[0]) << "\" b=\""
                       << xml_escapa(a.conectores[1]) << "\"";
                os << (a.espejo ? " espejo=\"si\"" : "") << "/>\n";
            }
            for (const auto& h : hilos_)
                os << "  <hilo a=\"" << xml_escapa(h.first) << "\" b=\""
                   << xml_escapa(h.second) << "\" placas=\""
                   << xml_escapa(placa_de(h.first) + " " + placa_de(h.second)) << "\"/>\n";
        }
        os << (sis ? "</sistema>\n" : "</placa>\n");
    }

private:
    // Lista y no vector: add() devuelve una referencia con la que se sigue
    // declarando (`.pin(...).par(...)`), y un vector que se reubica al crecer
    // la dejaría colgando. Una reserva suficiente bastaría; la lista lo
    // garantiza sin números mágicos.
    std::list<Instancia>       inst_;
    std::vector<std::string>   externos_;
    std::vector<std::string>   declarados_;
    std::vector<std::string>   buses_;
    // nodo compartido -> los pads que LO SON. Mapa y no lista porque lo
    // recorren el volcado y el cableado, y los dos tienen que salir en el mismo
    // orden en dos ejecuciones distintas.
    std::map<std::string, std::vector<std::string>> uniones_;
    std::vector<DeclMcu>       mcus_;
    // Lo que `resuelve_alias()` une: hilos y acoples, y lo que deja, el nombre
    // que le tocó a cada nodo.
    std::vector<std::pair<std::string, std::string>> hilos_;
    std::vector<Acople>        acoples_;
    std::map<std::string, std::string> alias_;
    std::vector<PlacaDeSistema> placas_;
    Ilustracion ilustracion_;
    bool                       sin_mcu_ = false;
    const Encapsulado*         enc_implicito_ = &ENC_LQFP100;
    std::vector<ExtPartBase*>  piezas_;
};

// ---------------------------------------------------------------------------
// DE LA DECLARACIÓN AL CABLEADO, que es el único paso que ocurre ANTES del MCU.
//
// El orden de montaje de una placa con nodos compartidos no es el de siempre.
// Antes era:
//
//     construir el MCU  ->  leer el fichero  ->  construir las piezas
//
// y con `une` pasa a ser:
//
//     leer el fichero  ->  crear los nodos compartidos  ->  construir el MCU
//                      ->  registrar los nodos  ->  construir las piezas
//
// Es viable porque LEER NO CONSTRUYE: `netlist_desde_fichero` devuelve datos, y
// eso permite tomar la decisión de qué pads comparten nodo antes de que exista
// un solo `sc_port` que atar.
//
// Esta función es ese tercer paso: crea en el `NodeMap` un AnalogNet por cada
// nodo compartido y devuelve, en `cab`, qué pad usa cada uno. Devuelve "" si
// todo está bien, o el primer problema. Los nodos los posee el `NodeMap`, que
// tiene que sobrevivir al MCU: los pads guardan un `sc_port` hacia ellos.
// ---------------------------------------------------------------------------
// `cabs` sale indexado por IDENTIFICADOR DE MCU —cadena vacía para el MCU
// implícito de una placa que no declara ninguno—, porque un puente puede unir
// dos pines del mismo chip o un pin de `u0` con uno de `u1`, y cada constructor
// necesita el suyo. Un MCU que no aparezca en el mapa se construye con un
// cableado vacío, que es exactamente lo de siempre.
inline std::string cableado_desde_netlist(const Netlist& nl, NodeMap& nodos,
                                          std::map<std::string, Cableado>& cabs) {
    std::map<std::string, std::string> pad_de;   // pad físico -> nodo que lo reclama
    for (const auto& u : nl.uniones()) {
        if (u.second.size() < 2)
            return "nodo " + u.first + ": une necesita al menos dos pads";
        analog_net_if& n = nodos.externo(u.first);
        for (const std::string& s : u.second) {
            std::string id_mcu, alim;
            unsigned p = 0, i = 0;
            // Un pad de alimentación o de arranque va a `une_alim`
            const bool es_alim = nl.resuelve_alim(s, id_mcu, alim).empty();
            if (!es_alim) {
                const std::string e = nl.resuelve_pad(s, id_mcu, p, i);
                if (!e.empty()) return "nodo " + u.first + ": " + e;
            }
            const std::string clave = id_mcu + ":" +
                (es_alim ? alim : std::to_string(p * N_PORT_PINS + i));
            const auto it = pad_de.find(clave);
            if (it != pad_de.end())
                return it->second == u.first
                     ? "nodo " + u.first + ": el pad " + s + " aparece dos veces"
                     : "el pad " + s + " esta en dos nodos a la vez: " +
                       it->second + " y " + u.first;
            pad_de[clave] = u.first;
            if (es_alim) cabs[id_mcu].une_alim(alim, n);
            else         cabs[id_mcu].une(p, i, n);
        }
    }
    return std::string();
}

} // namespace stm32
#endif // STM32_PARTS_NETLIST_H
