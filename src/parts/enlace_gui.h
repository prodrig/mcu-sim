// =============================================================================
// enlace_gui.h — La conexión con mcu-sim-gui, con la simulación en marcha
//
// Fases 4, 5 y 6 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`):
// el sentido modelo -> pantalla entero, las órdenes y el control. Un
// `SC_THREAD` que despierta cada 100 µs de tiempo simulado —el mismo patrón que
// los dos servidores de GDB (`common/gdb_rsp.h`)— y en cada vuelta:
//
//   1. lee lo que haya mandado la ventana: T_SUSCRIBE se aplica a la frontera,
//      T_ORDENES se encola en ella (relativo a AHORA, que es cuando el modelo
//      «la saca de la cola», `doc/protocolo.md` §5), T_PING se contesta con
//      T_PONG, y T_PAUSA, T_SIGUE, T_PASO y T_PARA controlan la simulación;
//   2. pasa al búfer de salida los AVISOS pendientes —los de SC_REPORT, que
//      este módulo desvía mientras está activo—, los ECOS de las órdenes que
//      haya aplicado la frontera (T_ORDEN_HECHA, con el instante real), y las
//      INSTANTÁNEAS que haya tomado el muestreador de la fase 1;
//   3. detrás de cada tanda de instantáneas, un T_ESTADO con los dos relojes;
//      y si en 250 ms de reloj de pared no ha salido ninguna, uno suelto. Es lo
//      que contesta a «¿se ha colgado?» cuando la respuesta es no;
//   4. escribe en el canal todo lo que el canal acepte, sin esperar.
//
// LA CONTRAPRESIÓN, decidida antes de escribir esto (`doc/protocolo.md` §4.1):
//
//   * si la ventana no lee, el búfer de salida no pasa de TOPE_SALIDA. Las
//     instantáneas se quedan entonces en la cola de la frontera, que cuando se
//     llena TIRA LAS NUEVAS y las cuenta. Son muestras: la siguiente dice lo
//     mismo y mejor, y la primera que pasa lleva en `perdidas` cuántas faltan;
//   * los avisos NO SE TIRAN NUNCA. Esperan en su propia cola, y si esa cola
//     llega a MAX_AVISOS se manda T_FIN con M_ERROR, se cierra la conexión y
//     SE SIGUE SIMULANDO, como cuando la ventana se va en marcha. Perder un
//     aviso es peor que perder la conexión: un aviso perdido se parece mucho a
//     un modelo que funciona;
//   * los ecos de las órdenes, tampoco: son lo único que dice CUÁNDO se aplicó
//     de verdad una orden, y es lo que se graba para repetir una sesión. Se
//     tratan como los avisos, con el mismo máximo en su cola (la de la
//     frontera). Avisos y ecos salen mezclados por su instante simulado; a
//     igual instante, primero el eco: el aviso suele ser su consecuencia.
//
// LAS ÓRDENES, en tres reglas (`doc/protocolo.md` §5):
//
//   * un T_ORDENES que no mide un múltiplo de 16 bytes, o que no trae ninguna,
//     no se aplica NI A MEDIAS: un T_AVISO dice por qué;
//   * cada orden se valida al aplicarla —pieza, mando, rango— y su eco lleva
//     el resultado. Las que se salen de rango se recortan, se aplican y,
//     además del eco con RES_RANGO, generan un T_AVISO que dice qué se pidió
//     y qué se aplicó: el silencio es lo que convierte un error de la GUI en
//     una tarde mirando el modelo;
//   * las que lleguen ANTES de T_ARRANCA las encola `sim` con `ordenes(...,
//     false)` antes de `sc_start()`: instantes absolutos, reproducibles al
//     picosegundo.
//
// EL CONTROL (fase 6), y la trampa que tenía, que es la de `doc/protocolo.md`
// §4.2: este proceso solo corre si el tiempo simulado avanza, así que una
// pausa que dejase de avanzarlo dejaría al modelo SORDO, sin oír el T_SIGUE.
// La salida es que todos los procesos de SystemC comparten UN hilo del sistema:
// mientras este proceso no llame a `wait()`, no corre ningún otro y el tiempo
// simulado no se mueve. Así que la pausa es un bucle EN TIEMPO DE PARED dentro
// del propio proceso: duerme en el canal hasta que llega algo (sin gastar
// CPU), contesta T_PING, acepta suscripciones y órdenes, manda un T_ESTADO
// con F_PAUSADA cada 250 ms, y vuelve cuando llega T_SIGUE. El modelo, entre
// tanto, está exactamente donde estaba: ni un picosegundo, ni un delta.
//
//   * T_PAUSA: a la pausa en la vuelta que lo lee. Si ya lo está, nada;
//   * T_SIGUE: sale de la pausa. Con RIT_DEMANDA no: allí solo se avanza con
//     T_PASO, y un T_AVISO lo dice;
//   * T_PASO: solo con RIT_DEMANDA (con otro ritmo, T_AVISO y se ignora, como
//     dice el protocolo). Avanza `ns` y vuelve a la pausa EXACTAMENTE en
//     t + ns: el proceso acorta su última espera para despertar justo ahí.
//     Uno que llegue con otro paso en curso se suma al final de aquel;
//   * T_PARA: `sc_stop()`, que es definitivo. Quien llamó a `sc_start()` ve
//     `parada()` y termina con T_FIN de motivo M_PARA.
//
// Si la ventana se va con la simulación en pausa, la pausa se quita y se
// sigue simulando hasta la ventana de tiempo —nadie va a decir «sigue»—. Y si
// la ventana de tiempo es indefinida (`para_al_perderse`), se para: ya no
// queda nadie que pueda pararla.
//
// LA TRAMPA DEL INVARIANTE, la misma que en la frontera: este módulo se
// construye siempre —la elaboración de SystemC es estática— y hasta que alguien
// llama a `activa()` su proceso espera un evento que nadie notifica, y el
// manejador de SC_REPORT es el de siempre. Sin `--gui` no existe. `test407`
// lleva uno construido y sin activar, y su invariante es la prueba.
//
// Lo que NO hace: el ritmo —el freno de tiempo real es de quien conduce la
// simulación (`sim_main.cpp`); aquí solo se sabe si es RIT_DEMANDA—, ni aplicar
// las órdenes él mismo —eso es el aplicador de la frontera—, ni saber de
// sockets: habla con un `CanalGui` (`common/gui_mensajes.h`).
// =============================================================================
#ifndef STM32_PARTS_ENLACE_GUI_H
#define STM32_PARTS_ENLACE_GUI_H

#include <systemc>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "frontera_gui.h"
#include "../common/gui_mensajes.h"
#include "../common/proto_io.h"

namespace stm32 {
namespace gui {

SC_MODULE(EnlaceGui) {
    // Cuántos avisos pueden esperar a una ventana que no lee antes de darla
    // por perdida. Mil avisos sin leer no son un atasco: son una ventana que
    // ya no está mirando.
    static constexpr std::size_t MAX_AVISOS = 1000;
    // Lo que se deja acumular en el búfer de salida antes de dejar de pasarle
    // instantáneas. A 60 Hz y una docena de observables son decenas de
    // segundos de pantalla: lo que sobre ya no es un atasco puntual.
    static constexpr std::size_t TOPE_SALIDA = 256u * 1024u;
    // Cada cuánto se sondea, en tiempo SIMULADO. El de los servidores de GDB.
    static constexpr double SONDEO_US = 100.0;
    // Un T_ESTADO suelto si en este tiempo de PARED no ha salido ninguno.
    static constexpr double LATIDO_S = 0.25;
    // En pausa, lo más que se duerme de una vez esperando a la ventana. Es la
    // resolución del latido en pausa, no la latencia: un mensaje despierta
    // la espera en cuanto llega.
    static constexpr int ESPERA_PAUSA_MS = 50;

    using Reloj = std::function<double()>;     // segundos de pared, monótonos

    // El tope y el máximo se pueden cambiar al construirlo: el banco `testgui`
    // los pone pequeños para provocar el atasco sin esperar segundos.
    EnlaceGui(sc_core::sc_module_name nm, FronteraGui& f,
              std::size_t tope_salida = TOPE_SALIDA, std::size_t max_avisos = MAX_AVISOS)
        : sc_core::sc_module(nm), fr_(f), tope_(tope_salida), max_avisos_(max_avisos) {
        SC_HAS_PROCESS(EnlaceGui);
        SC_THREAD(atiende);
    }
    ~EnlaceGui() override { suelta_manejador(); }

    // --- Activación -----------------------------------------------------------
    // Se llama UNA vez, con el saludo hecho: el canal, y el emisor y el lector
    // del saludo, que siguen la conexión donde la dejó (la versión negociada,
    // la secuencia, y lo que hubiera llegado detrás de T_ARRANCA). El reloj de
    // pared se puede sustituir para las pruebas.
    void activa(CanalGui* c, const mcusim::proto::Emisor& e,
                const mcusim::proto::Lector& l, Reloj reloj = Reloj()) {
        if (activo_) return;
        canal_ = c;
        em_ = e;
        lec_ = l;
        reloj_ = reloj ? std::move(reloj) : Reloj(&pared);
        t0_pared_ = reloj_();
        ultimo_estado_ = t0_pared_;
        activo_ = true;
        conectado_ = c != nullptr;
        // El manejador de SC_REPORT, encadenado al que hubiera: lo que hoy
        // sale por la consola sigue saliendo.
        if (!instancia()) {
            instancia() = this;
            anterior_ = sc_core::sc_report_handler::set_handler(&manejador);
        }
        if (sc_core::sc_is_running()) ev_activa_.notify(sc_core::SC_ZERO_TIME);
    }
    bool activo() const { return activo_; }
    bool conectado() const { return conectado_; }

    // --- El control (fase 6) --------------------------------------------------
    // El ritmo de T_ARRANCA. Lo único que le importa al enlace es si es
    // RIT_DEMANDA: entonces empieza EN PAUSA, y solo T_PASO lo hace avanzar.
    // Se llama antes de `sc_start()`.
    void ritmo(uint32_t r) {
        ritmo_ = r;
        if (r == mcusim::proto::RIT_DEMANDA) pausado_ = true;
    }
    uint32_t ritmo() const { return ritmo_; }
    // Con una ventana de tiempo indefinida, perder la conexión para la
    // simulación: sin ventana no queda nadie que pueda pararla.
    void para_al_perderse(bool si) { para_al_perderse_ = si; }
    // Lo que se hace al salir de una pausa, ANTES de que el tiempo vuelva a
    // correr. `sim` lo usa para echar el ancla del freno de tiempo real: si no,
    // el freno cree que va con retraso y corre para recuperarlo.
    void al_seguir(std::function<void()> f) { al_seguir_ = std::move(f); }
    bool pausado() const { return pausado_; }
    // true si la simulación se paró por un T_PARA de la ventana.
    bool parada() const { return parada_; }
    uint64_t pausas() const { return n_pausas_; }
    uint64_t pasos() const { return n_pasos_; }

    // La suscripción que llegó ANTES de T_ARRANCA. Se aplica antes de
    // `sc_start()`, y por eso la secuencia de instantáneas es reproducible.
    void suscripcion_inicial(const std::string& cuerpo) { aplica_suscripcion(cuerpo); }

    // Un T_ORDENES. `en_marcha` = false para los que llegaron antes de
    // T_ARRANCA (su primera orden es un instante absoluto); true para los que
    // lee el propio enlace (relativa al instante actual). Devuelve si se
    // encoló; si no, ya ha salido un T_AVISO diciendo por qué.
    bool ordenes(const std::string& cuerpo, bool en_marcha) {
        std::vector<mcusim::proto::Orden> v;
        if (!lee_ordenes(cuerpo, v)) {
            encola_aviso(mcusim::proto::N_AVISO, "mcu-sim/gui",
                         "T_ORDENES de " + std::to_string(cuerpo.size()) +
                         " bytes: no es un numero entero de ordenes de " +
                         std::to_string(sizeof(mcusim::proto::Orden)) +
                         " bytes; no se aplica ninguna");
            return false;
        }
        if (!fr_.encola(v, en_marcha)) return false;
        ++n_msj_ordenes_;
        n_ordenes_ += v.size();
        return true;
    }

    // Un aviso del propio mcu-sim, no de SC_REPORT.
    void avisa(uint32_t nivel, const std::string& origen, const std::string& texto) {
        if (conectado_) encola_aviso(nivel, origen, texto);
    }

    // --- Terminar --------------------------------------------------------------
    // Vacía lo pendiente —avisos, instantáneas, un T_ESTADO final— y manda
    // T_FIN. Se llama al acabar, dentro o fuera de la simulación: fuera, que es
    // lo normal, espera a que el canal acepte, como mucho `plazo_ms`.
    void termina(uint32_t motivo, int32_t codigo, int plazo_ms = 5000) {
        if (!activo_ || !conectado_) { suelta_manejador(); return; }
        mueve_a_salida(true);
        pon_estado(mcusim::proto::F_TERMINADA);
        em_.pod(sal_, mcusim::proto::T_FIN, mcusim::proto::Fin{motivo, codigo, ahora_ns()});
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(plazo_ms);
        while (!sal_.empty() && conectado_ && std::chrono::steady_clock::now() < fin) {
            if (escribe() == 0) canal_->espera_escritura(50);
        }
        cierra_conexion();
        suelta_manejador();
    }

    // --- Lo que ha pasado, para las pruebas y para quien quiera saberlo --------
    uint64_t instantaneas() const { return n_inst_; }
    uint64_t imagenes() const { return n_img_; }                   // T_IMAGEN mandados
    uint64_t avisos() const { return n_avisos_; }
    uint64_t estados() const { return n_estados_; }
    uint64_t ignorados() const { return n_ignorados_; }
    uint64_t suscripciones() const { return n_subs_; }
    uint64_t mensajes_ordenes() const { return n_msj_ordenes_; }   // T_ORDENES aceptados
    uint64_t ordenes() const { return n_ordenes_; }                // órdenes que traían
    uint64_t ecos() const { return n_ecos_; }                      // T_ORDEN_HECHA mandados
    std::size_t pendientes_salida() const { return sal_.size(); }
    std::size_t avisos_en_cola() const { return avisos_.size(); }
    // Por qué se cerró la conexión, si se cerró: vacío si sigue abierta.
    const std::string& cierre() const { return cierre_; }

private:
    struct Aviso { uint32_t nivel; std::string origen, texto; uint64_t t; };

    static double pared() {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static uint64_t ahora_ns() {
        return uint64_t(sc_core::sc_time_stamp().value() /
                        sc_core::sc_time(1, sc_core::SC_NS).value());
    }

    // --- El proceso -------------------------------------------------------------
    void atiende() {
        if (!activo_) wait(ev_activa_);      // sin --gui: aquí se queda para siempre
        while (conectado_) {
            // Un paso que llega a su final: a la pausa, justo aquí
            if (hay_objetivo_ && ahora_ns() >= objetivo_) {
                hay_objetivo_ = false;
                pausado_ = true;
            }
            vuelta();
            if (parar_) { detiene(); return; }
            if (!conectado_) break;
            if (pausado_) {
                en_pausa();
                if (parar_) { detiene(); return; }
                if (!conectado_) break;
            }
            wait(siguiente());
        }
        // Sin ventana el proceso termina: no vuelve a despertar nunca.
    }

    // La próxima vuelta: dentro de SONDEO_US, o antes si ahí acaba un paso.
    sc_core::sc_time siguiente() const {
        const uint64_t sondeo = uint64_t(SONDEO_US * 1000.0);
        if (hay_objetivo_) {
            const uint64_t ahora = ahora_ns();
            if (objetivo_ > ahora && objetivo_ - ahora < sondeo)
                return sc_core::sc_time(double(objetivo_ - ahora), sc_core::SC_NS);
        }
        return sc_core::sc_time(double(sondeo), sc_core::SC_NS);
    }

    // LA PAUSA: un bucle de reloj de pared, con todo el modelo quieto porque
    // este proceso no cede el hilo. Véase la cabecera.
    void en_pausa() {
        using namespace mcusim::proto;
        ++n_pausas_;
        pon_estado(F_PAUSADA);
        escribe();
        while (conectado_ && pausado_ && !parar_) {
            canal_->espera_lectura(ESPERA_PAUSA_MS);
            lee();
            if (!conectado_ || parar_) break;
            escribe();
            mueve_a_salida(false);
            if (!pausado_ || cambio_ || reloj_() - ultimo_estado_ >= LATIDO_S)
                pon_estado(pausado_ ? F_PAUSADA : F_CORRIENDO);
            cambio_ = false;
            escribe();
        }
        if (al_seguir_) al_seguir_();
    }

    // T_PARA: definitivo. Lo que queda por mandar lo manda `termina()`, que
    // llama quien hizo `sc_start()` al ver `parada()`.
    void detiene() {
        parada_ = true;
        pausado_ = false;
        sc_core::sc_stop();
    }

    void vuelta() {
        lee();
        if (!conectado_) return;
        // Primero lo que se quedó atascado, y LUEGO lo nuevo: si no, tras un
        // atasco la salida está llena justo cuando se mira, y lo que espera
        // —avisos incluidos— se queda una vuelta más sin salir.
        escribe();
        if (!conectado_) return;
        const bool hubo = mueve_a_salida(false);
        // Los ecos esperan en la cola de la frontera; con la ventana sin leer,
        // la misma regla que los avisos.
        if (fr_.hechas.size() >= max_avisos_) { desborda("ecos de ordenes"); return; }
        // En pausa el T_ESTADO lo pone `en_pausa()`, con su fase
        if (!pausado_ && (hubo || cambio_ || reloj_() - ultimo_estado_ >= LATIDO_S))
            pon_estado(mcusim::proto::F_CORRIENDO);
        cambio_ = false;
        escribe();
    }

    // Lo que haya mandado la ventana.
    void lee() {
        using namespace mcusim::proto;
        char b[4096];
        for (;;) {
            const long k = canal_->recibe(b, sizeof b);
            if (k == 0) { pierde("la ventana cerro la conexion"); return; }
            if (k < 0) break;
            lec_.mete(b, std::size_t(k));
        }
        Mensaje m;
        for (;;) {
            const Lector::Estado e = lec_.saca(m);
            if (e == Lector::LEC_FALTA) break;
            if (e == Lector::LEC_ERROR) {
                pierde("la ventana manda algo que no es el protocolo: " + lec_.error());
                return;
            }
            switch (m.tipo) {
                case T_SUSCRIBE: aplica_suscripcion(m.texto()); break;
                case T_ORDENES:  ordenes(m.texto(), true); break;
                case T_PING:     em_.vacio(sal_, T_PONG); break;
                case T_PAUSA:
                    // Una pausa cancela el paso en curso: se queda donde está
                    hay_objetivo_ = false;
                    pausado_ = true;
                    break;
                case T_SIGUE:
                    if (ritmo_ == RIT_DEMANDA)
                        encola_aviso(N_AVISO, "mcu-sim/gui",
                                     "T_SIGUE con ritmo a demanda: aqui solo se avanza "
                                     "con T_PASO; se ignora");
                    else if (pausado_) { pausado_ = false; cambio_ = true; }
                    break;
                case T_PASO:     paso(m); break;
                case T_PARA:     parar_ = true; break;
                default:         ++n_ignorados_; break;   // lo desconocido
            }
            if (parar_) return;      // lo que venga detrás de T_PARA ya no cuenta
        }
    }

    void paso(const mcusim::proto::Mensaje& m) {
        using namespace mcusim::proto;
        Paso p{};
        if (ritmo_ != RIT_DEMANDA) {
            encola_aviso(N_AVISO, "mcu-sim/gui",
                         "T_PASO solo tiene sentido con ritmo a demanda; se ignora");
            return;
        }
        if (!m.como(p)) {
            encola_aviso(N_AVISO, "mcu-sim/gui",
                         "T_PASO de " + std::to_string(m.longitud) + " bytes; son " +
                         std::to_string(sizeof p) + ": se ignora");
            return;
        }
        ++n_pasos_;
        if (p.ns == 0) { cambio_ = true; return; }   // nada que avanzar
        // Detrás del paso en curso, si lo hay; si no, desde ahora
        objetivo_ = (hay_objetivo_ ? objetivo_ : ahora_ns()) + p.ns;
        hay_objetivo_ = true;
        pausado_ = false;
    }

    void aplica_suscripcion(const std::string& cuerpo) {
        uint64_t periodo = 0;
        std::vector<uint16_t> ids;
        if (!lee_suscripcion(cuerpo, periodo, ids)) {
            encola_aviso(mcusim::proto::N_AVISO, "mcu-sim/gui",
                         "T_SUSCRIBE de " + std::to_string(cuerpo.size()) +
                         " bytes no mide lo que dice: se ignora y sigue la anterior");
            return;
        }
        if (!fr_.suscribe(periodo, ids)) {
            std::string malos;
            for (uint16_t id : ids)
                if (!fr_.catalogo().existe_id(id)) {
                    if (!malos.empty()) malos += ", ";
                    malos += std::to_string(id);
                }
            encola_aviso(mcusim::proto::N_AVISO, "mcu-sim/gui",
                         "suscripcion rechazada: no existe el observable " + malos +
                         " (hay " + std::to_string(fr_.catalogo().n_observables()) +
                         "); sigue la anterior");
            return;
        }
        ++n_subs_;
    }

    // Avisos, ecos e instantáneas al búfer de salida. `todo`: sin mirar el
    // tope, que es lo que se hace al terminar. Devuelve si salió alguna
    // instantánea.
    bool mueve_a_salida(bool todo) {
        using namespace mcusim::proto;
        for (;;) {
            if (!todo && sal_.size() >= tope_) break;
            const bool hay_a = !avisos_.empty(), hay_e = !fr_.hechas.empty();
            if (!hay_a && !hay_e) break;
            // Por instante; a igual instante, primero el eco.
            if (hay_e && (!hay_a || fr_.hechas.front().t_sim_ns <= avisos_.front().t)) {
                const OrdenHecha h = fr_.hechas.front();
                fr_.hechas.pop_front();
                em_.pod(sal_, T_ORDEN_HECHA, h);
                ++n_ecos_;
                // El aviso del recorte va justo detrás de su eco, sin pasar por
                // la cola: no tiene sentido el uno sin el otro.
                if (h.resultado == RES_RANGO) {
                    em_.mensaje(sal_, T_AVISO,
                                cuerpo_aviso(N_AVISO, "mcu-sim/gui", texto_rango(h), h.t_sim_ns));
                    ++n_avisos_;
                }
            } else {
                const Aviso& a = avisos_.front();
                em_.mensaje(sal_, T_AVISO, cuerpo_aviso(a.nivel, a.origen, a.texto, a.t));
                avisos_.pop_front();
                ++n_avisos_;
            }
        }
        bool hubo = false;
        while (!fr_.instantaneas.empty() && (todo || sal_.size() < tope_)) {
            const Instantanea& in = fr_.instantaneas.front();
            std::string c(reinterpret_cast<const char*>(&in.cab), sizeof in.cab);
            c.append(reinterpret_cast<const char*>(in.muestras.data()),
                     in.muestras.size() * sizeof(Muestra));
            em_.mensaje(sal_, T_INSTANTANEA, c);
            fr_.instantaneas.pop_front();
            ++n_inst_;
            hubo = true;
        }
        // Las imágenes, detrás: con la salida atascada esperan en su cola, y
        // el muestreador deja de tomar mientras haya dos esperando
        while (!fr_.imagenes.empty() && (todo || sal_.size() < tope_)) {
            const ImagenTomada& im = fr_.imagenes.front();
            std::string c(reinterpret_cast<const char*>(&im.cab), sizeof im.cab);
            c += im.pix;
            em_.mensaje(sal_, T_IMAGEN, c);
            fr_.imagenes.pop_front();
            ++n_img_;
            hubo = true;
        }
        return hubo;
    }

    // «orden fuera de rango para B1.pulsar en t = 1000000000 ns: lo pedido
    // pasa del maximo; se aplica 1 (rango 0 a 1)». El valor pedido no viaja en
    // el eco —el eco dice lo que se APLICÓ—, pero de qué lado se salió sí se
    // sabe: si se aplicó el máximo, se pidió más; si no, menos o un NaN.
    std::string texto_rango(const mcusim::proto::OrdenHecha& h) const {
        const ExtPartBase* p = fr_.catalogo().pieza(h.pieza);
        std::string nombre = "pieza " + std::to_string(h.pieza) + ", mando " +
                             std::to_string(h.mando);
        std::string lado = "se sale del rango", rango;
        if (p && h.mando < p->n_mandos()) {
            const Mando m = p->mando(h.mando);
            nombre = p->pieza() + "." + m.nombre;
            rango  = " (rango " + num(m.min) + " a " + num(m.max) + ")";
            lado   = (h.valor == m.max && m.max != m.min) ? "pasa del maximo"
                                                          : "no llega al minimo o no es un numero";
        }
        return "orden fuera de rango para " + nombre + " en t = " +
               std::to_string(h.t_sim_ns) + " ns: lo pedido " + lado +
               "; se aplica " + num(h.valor) + rango;
    }
    static std::string num(float v) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", double(v));
        return b;
    }

    void pon_estado(uint32_t fase) {
        using namespace mcusim::proto;
        const double p = reloj_();
        em_.pod(sal_, T_ESTADO, Estado{fase, 0, ahora_ns(), p - t0_pared_,
                                       uint64_t(sc_core::sc_delta_count())});
        ultimo_estado_ = p;
        ++n_estados_;
    }

    // Lo que el canal acepte, sin esperar. Devuelve cuántos bytes salieron.
    std::size_t escribe() {
        std::size_t hecho = 0;
        while (hecho < sal_.size()) {
            const long k = canal_->envia(sal_.data() + hecho, sal_.size() - hecho);
            if (k < 0) { sal_.erase(0, hecho); pierde("la ventana cerro la conexion"); return hecho; }
            if (k == 0) break;
            hecho += std::size_t(k);
        }
        sal_.erase(0, hecho);
        return hecho;
    }

    void encola_aviso(uint32_t nivel, const std::string& origen, const std::string& texto) {
        if (!conectado_) return;
        if (avisos_.size() >= max_avisos_) { desborda(); return; }
        avisos_.push_back({nivel, origen, texto, ahora_ns()});
    }

    // La ventana no lee y los avisos se acumulan: no se tira ninguno, se
    // pierde la conexión. T_FIN con M_ERROR va DELANTE de lo que no ha salido,
    // y se intenta escribir una vez; luego se cierra. El código es 0 porque el
    // proceso sigue: la simulación continúa hasta su ventana.
    void desborda(const char* que = "avisos") {
        using namespace mcusim::proto;
        std::string fin;
        em_.pod(fin, T_FIN, Fin{M_ERROR, 0, ahora_ns()});
        sal_.insert(0, fin);
        escribe();
        pierde("la ventana no lee y se han acumulado " + std::to_string(max_avisos_) +
               " " + que + " sin mandar; antes que tirar uno se cierra la conexion");
    }

    // Se acabó la conexión. La simulación sigue: sin nadie mirando, se deja de
    // muestrear, que no lo va a leer nadie. Las órdenes ya encoladas SÍ se
    // siguen aplicando —se aceptaron, y el modelo hace lo que se le dijo—,
    // pero sus ecos ya no van a ninguna parte.
    void pierde(const std::string& por) {
        if (!conectado_) return;
        cierre_ = por;
        std::fprintf(stderr, "gui: %s; se sigue simulando\n", por.c_str());
        cierra_conexion();
        fr_.suscribe(0, {});
        fr_.hechas.clear();
        // Nadie va a decir «sigue»: fuera la pausa y el paso
        pausado_ = false;
        hay_objetivo_ = false;
        // Y con una ventana de tiempo indefinida, nadie va a poder pararla
        if (para_al_perderse_ && sc_core::sc_get_status() == sc_core::SC_RUNNING) {
            std::fprintf(stderr, "gui: sin ventana de tiempo y sin ventana que mire, "
                                 "no queda quien la pare: se para\n");
            sc_core::sc_stop();
        }
    }

    // Sin conexión, el manejador de SC_REPORT vuelve a ser el de antes: lo
    // que se informe desde aquí va a la consola, como sin --gui.
    void cierra_conexion() {
        if (conectado_ && canal_) canal_->cierra();
        conectado_ = false;
        avisos_.clear();
        sal_.clear();
        suelta_manejador();
    }

    // --- El desvío de SC_REPORT -------------------------------------------------
    static EnlaceGui*& instancia() { static EnlaceGui* p = nullptr; return p; }

    void suelta_manejador() {
        if (instancia() == this) {
            sc_core::sc_report_handler::set_handler(anterior_);
            instancia() = nullptr;
        }
    }

    // Va a la ventana lo que iría a la consola: lo que lleva SC_DISPLAY, y los
    // errores y fatales, que la consola enseña al recoger la excepción. Lo que
    // está silenciado (SC_DO_NOTHING) sigue silenciado. Y una excepción
    // deliberada: los informativos del propio núcleo de SystemC —«Simulation
    // stopped by user»— no son del modelo, y para eso está T_FIN.
    static void manejador(const sc_core::sc_report& r, const sc_core::sc_actions& a) {
        EnlaceGui* e = instancia();
        if (e && e->conectado_) {
            const bool se_ve = (a & sc_core::SC_DISPLAY) ||
                               (r.get_severity() >= sc_core::SC_ERROR &&
                                a != sc_core::SC_DO_NOTHING);
            const bool nucleo = r.get_severity() == sc_core::SC_INFO &&
                                std::string(r.get_msg_type()).rfind("/OSCI/SystemC", 0) == 0;
            if (se_ve && !nucleo) {
                uint32_t n = mcusim::proto::N_INFO;
                switch (r.get_severity()) {
                    case sc_core::SC_WARNING: n = mcusim::proto::N_AVISO; break;
                    case sc_core::SC_ERROR:   n = mcusim::proto::N_ERROR; break;
                    case sc_core::SC_FATAL:   n = mcusim::proto::N_FATAL; break;
                    default: break;
                }
                e->encola_aviso(n, r.get_msg_type(), r.get_msg());
            }
        }
        if (e && e->anterior_) e->anterior_(r, a);
        else sc_core::sc_report_handler::default_handler(r, a);
    }

    FronteraGui&              fr_;
    std::size_t               tope_, max_avisos_;
    CanalGui*                 canal_ = nullptr;
    mcusim::proto::Emisor     em_;
    mcusim::proto::Lector     lec_{mcusim::proto::Origen::Pantalla};
    Reloj                     reloj_;
    bool                      activo_ = false, conectado_ = false;
    double                    t0_pared_ = 0, ultimo_estado_ = 0;
    std::string               sal_;
    std::deque<Aviso>         avisos_;
    std::string               cierre_;
    uint32_t                  ritmo_ = mcusim::proto::RIT_LIBRE;
    bool                      pausado_ = false, parar_ = false, parada_ = false,
                              cambio_ = false, hay_objetivo_ = false,
                              para_al_perderse_ = false;
    uint64_t                  objetivo_ = 0, n_pausas_ = 0, n_pasos_ = 0;
    std::function<void()>     al_seguir_;
    uint64_t                  n_inst_ = 0, n_img_ = 0, n_avisos_ = 0, n_estados_ = 0,
                              n_ignorados_ = 0, n_subs_ = 0,
                              n_msj_ordenes_ = 0, n_ordenes_ = 0, n_ecos_ = 0;
    sc_core::sc_report_handler_proc anterior_ = nullptr;
    sc_core::sc_event         ev_activa_;
};

} // namespace gui
} // namespace stm32

#endif // STM32_PARTS_ENLACE_GUI_H
