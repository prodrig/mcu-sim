// =============================================================================
// frontera_gui.h — La frontera entre el modelo y mcu-sim-gui, todavía sin socket
//
// Fase 1 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`).
// Es la parte que se reaprovecha entera, dijera lo que dijera el escenario
// elegido, y por eso se escribe y se verifica SIN GUI ninguna. Tres piezas:
//
//   Catalogo     qué se puede ver y qué se puede hacer: los observables y los
//                mandos de cada pieza del inventario, con los identificadores
//                que viajarán por el protocolo. Es una consulta pura: no gasta
//                tiempo simulado y no toca el modelo. De aquí sale el cuerpo de
//                T_CATALOGO (`doc/protocolo.md` §3), y aquí se valida una orden.
//
//   muestreador  un SC_THREAD que despierta cada `periodo_ns` de tiempo
//                simulado, lee los observables suscritos y deja una
//                instantánea en una cola acotada. Si la cola está llena la
//                instantánea se TIRA y se cuenta en `perdidas` de la siguiente:
//                son muestras, la siguiente dice lo mismo y mejor
//                (`doc/protocolo.md` §4.1).
//
//   aplicador    un SC_THREAD que saca órdenes de una cola, espera a su
//                instante y llama a `acciona()` de la pieza. Cada orden deja su
//                eco, con el instante REAL en que se aplicó y el resultado
//                (`doc/protocolo.md` §5).
//
// Las dos colas de salida —instantáneas y ecos de órdenes— las vacía hacia la
// ventana el enlace (`enlace_gui.h`, fases 4 y 5); en el banco `testgui`, las
// de la frontera de G0 a G8 las vacía el propio banco.
//
// LA TRAMPA DEL INVARIANTE, Y CÓMO SE SORTEA. La elaboración de SystemC es
// estática: estos dos procesos se construyen siempre, haya `--gui` o no, y un
// proceso que despierta en el tiempo SÍ mueve el tiempo simulado. La salida ya
// está probada en este proyecto —la fase 4 del plan del F415/F417 metió el
// CRYP y el HASH enteros en la elaboración del F407 sin mover el invariante un
// picosegundo—: un proceso que nunca despierta no cuesta nada. Así que, hasta
// que alguien llame a `activa()`, los dos esperan sobre un `sc_event` que nadie
// notifica. `test407` construye una frontera y no la activa nunca, y su
// invariante es la prueba de que esto es verdad.
//
// LO QUE NO HACE, a propósito: no sabe nada de sockets ni del marco del
// protocolo. Usa sus structs POD (`Muestra`, `Orden`, `OrdenHecha`) porque son
// el contrato, no porque vaya a escribirlas en ningún sitio.
// =============================================================================
#ifndef STM32_PARTS_FRONTERA_GUI_H
#define STM32_PARTS_FRONTERA_GUI_H

#include <systemc>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "part_base.h"
#include "xml_min.h"
#include "../common/protocolo.h"

namespace stm32 {
namespace gui {

// ---------------------------------------------------------------------------
// El catálogo
//
// Se construye sobre una COPIA del inventario de `ExtPartBase`, hecha cuando
// la placa ya está montada. El índice de pieza es la posición en esa copia y
// vale para toda la ejecución: después de la elaboración no se construye
// ninguna pieza. Que tampoco se destruya ninguna es responsabilidad de quien
// monta la placa; `sim` no destruye piezas hasta que termina.
//
// `id_obs` es PLANO y GLOBAL: los observables de todas las piezas, numerados
// seguidos en el orden del inventario. Se descartó empaquetar `(pieza << 6) |
// observable` porque pone un techo que nadie recuerda (`doc/protocolo.md` §3).
// ---------------------------------------------------------------------------
class Catalogo {
public:
    Catalogo() = default;
    explicit Catalogo(const std::vector<ExtPartBase*>& inv) : piezas_(inv) {
        primer_obs_.reserve(piezas_.size());
        unsigned n = 0;
        for (const ExtPartBase* p : piezas_) {
            primer_obs_.push_back(n);
            for (unsigned i = 0; i < p->n_observables(); ++i) {
                obs_pieza_.push_back(unsigned(primer_obs_.size() - 1));
                obs_idx_.push_back(i);
            }
            n += p->n_observables();
        }
    }

    unsigned     n_piezas() const { return unsigned(piezas_.size()); }
    ExtPartBase* pieza(unsigned i) const { return i < piezas_.size() ? piezas_[i] : nullptr; }
    // El índice de una pieza concreta, o -1 si no está en el catálogo.
    int indice_de(const ExtPartBase* p) const {
        for (unsigned i = 0; i < piezas_.size(); ++i) if (piezas_[i] == p) return int(i);
        return -1;
    }

    // --- Observables, por su identificador global ---------------------------
    unsigned n_observables() const { return unsigned(obs_pieza_.size()); }
    // -1 si la pieza o el observable no existen.
    int id_obs(unsigned pieza, unsigned obs) const {
        if (pieza >= piezas_.size() || obs >= piezas_[pieza]->n_observables()) return -1;
        return int(primer_obs_[pieza] + obs);
    }
    bool existe_obs(uint16_t id) const { return id < obs_pieza_.size(); }
    unsigned pieza_de_obs(uint16_t id) const { return obs_pieza_[id]; }
    unsigned indice_de_obs(uint16_t id) const { return obs_idx_[id]; }
    float valor(uint16_t id) const {
        return piezas_[obs_pieza_[id]]->valor_observable(obs_idx_[id]);
    }

    // --- Validación de una orden -------------------------------------------
    // Dice qué pasaría si se aplicase, SIN aplicarla. `valor` sale recortado
    // al rango del mando. El orden de los resultados es el de su gravedad: una
    // pieza que no existe no tiene mandos que mirar, y un mando que no existe
    // no tiene rango.
    mcusim::proto::Resultado valida(const mcusim::proto::Orden& o, float& valor) const {
        using namespace mcusim::proto;
        valor = o.valor;
        if (o.pieza >= piezas_.size())                    return RES_PIEZA;
        const ExtPartBase* p = piezas_[o.pieza];
        if (o.mando >= p->n_mandos())                     return RES_MANDO;
        const Mando m = p->mando(o.mando);
        // Un NaN no está en ningún rango: se lleva al mínimo, que es el reposo.
        if (!(o.valor >= m.min)) { valor = m.min; return RES_RANGO; }
        if (o.valor > m.max)     { valor = m.max; return RES_RANGO; }
        return RES_OK;
    }

    // --- El cuerpo de T_CATALOGO --------------------------------------------
    // El formato es el de `doc/protocolo.md` §3. Las piezas que no declaran
    // nada salen igual, sin hijos: el `idx` de pieza es el del inventario
    // entero, y saltarse las mudas haría que dejase de serlo.
    std::string xml() const {
        std::string s = "<catalogo>\n";
        for (unsigned i = 0; i < piezas_.size(); ++i) {
            const ExtPartBase* p = piezas_[i];
            s += "  <pieza idx=\"" + std::to_string(i) + "\" id=\"" +
                 xml_escapa(p->pieza()) + "\" tipo=\"" + xml_escapa(p->tipo()) + "\"";
            if (p->n_observables() == 0 && p->n_mandos() == 0) { s += "/>\n"; continue; }
            s += ">\n";
            for (unsigned k = 0; k < p->n_observables(); ++k) {
                const Observable o = p->observable(k);
                s += "    <observable idx=\"" + std::to_string(k) +
                     "\" id_obs=\"" + std::to_string(primer_obs_[i] + k) +
                     "\" nombre=\"" + xml_escapa(o.nombre) +
                     "\" unidad=\"" + xml_escapa(o.unidad) +
                     "\" min=\"" + num(o.min) + "\" max=\"" + num(o.max) +
                     "\" interesante=\"" + (o.interesante ? "si" : "no") + "\"" +
                     (o.alarma ? " alarma=\"si\"" : "") + "/>\n";
            }
            for (unsigned k = 0; k < p->n_mandos(); ++k) {
                const Mando m = p->mando(k);
                s += "    <mando idx=\"" + std::to_string(k) +
                     "\" nombre=\"" + xml_escapa(m.nombre) +
                     "\" tipo=\"" + nombre_tipo_mando(m.tipo) +
                     "\" min=\"" + num(m.min) + "\" max=\"" + num(m.max) +
                     "\" valor=\"" + num(p->valor_mando(k)) + "\"/>\n";
            }
            s += "  </pieza>\n";
        }
        s += "</catalogo>\n";
        return s;
    }

private:
    // `%g` y no `to_string`: 25 se escribe "25" y no "25.000000", y el XML de
    // ejemplo del protocolo es así.
    static std::string num(float f) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", double(f));
        return b;
    }
    std::vector<ExtPartBase*> piezas_;
    std::vector<unsigned>     primer_obs_;           // por pieza
    std::vector<unsigned>     obs_pieza_, obs_idx_;  // por id_obs
};

// Una instantánea ya tomada: la cabecera del mensaje y sus muestras.
struct Instantanea {
    mcusim::proto::CabInstantanea         cab;
    std::vector<mcusim::proto::Muestra>   muestras;
};

// ---------------------------------------------------------------------------
// Los dos procesos
// ---------------------------------------------------------------------------
SC_MODULE(FronteraGui) {
    // Cuántas instantáneas caben en la cola antes de empezar a tirarlas. A 60
    // por segundo son dos segundos de pantalla atascada: más que eso ya no es
    // un atasco, es una pantalla que no está leyendo.
    static constexpr std::size_t CAPACIDAD_OMISION = 128;

    explicit FronteraGui(sc_core::sc_module_name nm,
                         std::size_t capacidad = CAPACIDAD_OMISION)
        : sc_core::sc_module(nm), capacidad_(capacidad ? capacidad : 1) {
        SC_HAS_PROCESS(FronteraGui);
        SC_THREAD(muestreador);
        SC_THREAD(aplicador);
    }

    // --- Activación ----------------------------------------------------------
    // Sin esto, los dos procesos no despiertan nunca y la frontera no cuesta
    // nada. Se llama UNA vez, con la placa ya montada; el catálogo se copia.
    void activa(const Catalogo& c) {
        if (activa_) return;
        cat_    = c;
        activa_ = true;
        notifica(ev_activa_);
    }
    bool activa() const { return activa_; }
    const Catalogo& catalogo() const { return cat_; }

    // --- T_SUSCRIBE ----------------------------------------------------------
    // Reemplaza a la anterior. Con periodo 0 o sin ids no se muestrea. Un id
    // que no existe hace que se rechace ENTERA y que la anterior siga en pie:
    // a medias es peor que nada, porque la pantalla creería ver lo que pidió.
    // Antes de activar no se acepta ninguna: no hay catálogo contra el que
    // comprobarla.
    //
    // Las instantáneas caen en los múltiplos del periodo contados desde t = 0,
    // no desde el instante de la suscripción. Así dos ejecuciones con la misma
    // suscripción muestrean en los mismos instantes aunque la suscripción
    // llegue en momentos distintos, y una instantánea se puede comparar con la
    // de otra ejecución sin más cuenta.
    bool suscribe(uint64_t periodo_ns, const std::vector<uint16_t>& ids) {
        if (!activa_) return false;
        for (uint16_t id : ids) if (!cat_.existe_obs(id)) return false;
        periodo_ns_  = periodo_ns;
        subs_        = ids;
        cambio_subs_ = true;
        notifica(ev_subs_);
        return true;
    }
    uint64_t periodo_ns() const { return periodo_ns_; }
    const std::vector<uint16_t>& suscritos() const { return subs_; }

    // --- T_ORDENES -----------------------------------------------------------
    // La semántica de `doc/protocolo.md` §5: la primera orden lleva un instante
    // ABSOLUTO si el mensaje llegó antes de arrancar (`en_marcha = false`) o
    // RELATIVO al instante en que se saca de la cola si la simulación ya
    // corre; las siguientes, el tiempo transcurrido desde la anterior. Aquí se
    // convierten todas a instantes absolutos en el momento de encolarlas, que
    // es el «sacarla de la cola» del protocolo: el enlace (`enlace_gui.h`)
    // llama a esto en el instante en que lee el mensaje del socket.
    //
    // Varias órdenes en el MISMO instante se aplican en el orden en que
    // llegaron, también entre mensajes distintos.
    //
    // Devuelve false, y no encola nada, si la frontera no está activa.
    bool encola(const std::vector<mcusim::proto::Orden>& v, bool en_marcha) {
        if (!activa_) return false;
        uint64_t t = en_marcha ? ahora_ns() : 0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            t += v[i].t_sim_ns;
            pendientes_.emplace(t, v[i]);
        }
        if (!v.empty()) notifica(ev_orden_);
        return true;
    }
    std::size_t ordenes_pendientes() const { return pendientes_.size(); }

    // --- Lo que sale: lo que el enlace vacía hacia el socket ----------------
    std::deque<Instantanea>               instantaneas;
    std::deque<mcusim::proto::OrdenHecha> hechas;
    // Contadores de por vida, para quien quiera saber si esto se ha movido.
    uint64_t tomadas()  const { return tomadas_; }
    uint64_t perdidas() const { return perdidas_total_; }
    uint64_t aplicadas() const { return aplicadas_; }

    // Aplica UNA orden ya, en el instante actual, y devuelve su eco. Es lo
    // que hace el aplicador al llegar el instante de cada una, y es pública
    // porque es la parte que se puede probar sin gastar tiempo simulado. Con
    // la frontera sin activar devuelve RES_PIEZA: no hay catálogo, luego no
    // hay pieza ninguna.
    mcusim::proto::OrdenHecha aplica(const mcusim::proto::Orden& o,
                                     bool tarde = false) {
        using namespace mcusim::proto;
        OrdenHecha h{};
        h.t_sim_ns = ahora_ns();
        h.pieza    = o.pieza;
        h.mando    = o.mando;
        float v    = o.valor;
        Resultado r = cat_.valida(o, v);
        h.valor    = v;
        // Si además de llegar tarde se salía de rango, se dice lo segundo:
        // un campo, un resultado, y el rango es lo que la GUI puede corregir.
        if (r == RES_OK || r == RES_RANGO) {
            cat_.pieza(o.pieza)->acciona(o.mando, v);
            ++aplicadas_;
            if (r == RES_OK && tarde) r = RES_TARDE;
        }
        h.resultado = r;
        return h;
    }

private:
    static uint64_t ahora_ns() {
        // El modelo no programa nada por debajo del nanosegundo que se vaya a
        // mezclar con esto, pero se trunca explícitamente: el protocolo habla
        // en nanosegundos y un instante de 1,5 ns no se puede escribir en él.
        return uint64_t(sc_core::sc_time_stamp().value() /
                        sc_core::sc_time(1, sc_core::SC_NS).value());
    }
    // Notificar solo tiene sentido con la simulación en marcha. Antes de
    // `sc_start()` los procesos aún no han corrido, y cuando corran por
    // primera vez miran el estado antes de esperar: no se pierde nada.
    static void notifica(sc_core::sc_event& e) {
        if (sc_core::sc_is_running()) e.notify(sc_core::SC_ZERO_TIME);
    }

    void muestreador() {
        if (!activa_) wait(ev_activa_);      // sin --gui: aquí se queda para siempre
        for (;;) {
            cambio_subs_ = false;
            if (periodo_ns_ == 0 || subs_.empty()) { wait(ev_subs_); continue; }
            const uint64_t ahora = ahora_ns();
            const uint64_t prox  = (ahora / periodo_ns_ + 1) * periodo_ns_;
            wait(sc_core::sc_time(double(prox - ahora), sc_core::SC_NS), ev_subs_);
            if (ahora_ns() != prox) continue;   // cambió la suscripción: se recalcula
            // UN DELTA DE CORTESÍA. Las órdenes de este mismo instante las
            // aplica el otro proceso en el primer delta de `prox`, y el orden
            // en que SystemC despierta a dos procesos en el mismo delta no lo
            // fija la norma. Con esta espera la muestra de `prox` ve SIEMPRE
            // las órdenes de `prox`, sea cual sea la versión de SystemC. Lo que
            // no ve necesariamente es su consecuencia ELÉCTRICA -un LED que
            // se enciende porque se pulsó un botón tarda deltas en enterarse-;
            // eso lo verá la siguiente.
            wait(sc_core::SC_ZERO_TIME);
            if (cambio_subs_) continue;
            toma(prox);
        }
    }

    void toma(uint64_t t) {
        Instantanea in;
        in.cab.t_sim_ns = t;
        in.cab.n        = uint32_t(subs_.size());
        in.cab.perdidas = perdidas_pend_;
        in.muestras.reserve(subs_.size());
        for (uint16_t id : subs_) {
            mcusim::proto::Muestra m{};
            m.id    = id;
            m.valor = cat_.valor(id);
            in.muestras.push_back(m);
        }
        ++tomadas_;
        if (instantaneas.size() >= capacidad_) {   // atascada: se tira la nueva
            ++perdidas_pend_;
            ++perdidas_total_;
            return;
        }
        perdidas_pend_ = 0;
        instantaneas.push_back(std::move(in));
    }

    void aplicador() {
        if (!activa_) wait(ev_activa_);      // sin --gui: aquí se queda para siempre
        for (;;) {
            if (pendientes_.empty()) { wait(ev_orden_); continue; }
            const uint64_t t     = pendientes_.begin()->first;
            const uint64_t ahora = ahora_ns();
            if (t > ahora) {
                // Puede llegar otra orden ANTERIOR mientras se espera a esta:
                // por eso se espera también al evento y se vuelve a mirar.
                wait(sc_core::sc_time(double(t - ahora), sc_core::SC_NS), ev_orden_);
                continue;
            }
            const mcusim::proto::Orden o = pendientes_.begin()->second;
            pendientes_.erase(pendientes_.begin());
            hechas.push_back(aplica(o, t < ahora));
        }
    }

    Catalogo cat_;
    bool     activa_ = false;
    std::size_t capacidad_;

    uint64_t              periodo_ns_ = 0;
    std::vector<uint16_t> subs_;
    bool                  cambio_subs_ = false;
    uint32_t              perdidas_pend_ = 0;
    uint64_t              tomadas_ = 0, perdidas_total_ = 0, aplicadas_ = 0;

    // Instante absoluto en ns -> orden. Un multimap conserva el orden de
    // inserción entre claves iguales ([associative.reqmts]: `insert` pone
    // la nueva al final de las equivalentes), que es lo que hace
    // falta para «en el mismo instante, en el orden en que vienen».
    std::multimap<uint64_t, mcusim::proto::Orden> pendientes_;

    sc_core::sc_event ev_activa_, ev_subs_, ev_orden_;
};

} // namespace gui
} // namespace stm32

#endif // STM32_PARTS_FRONTERA_GUI_H
