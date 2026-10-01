// =============================================================================
// enlace_gui.h — La conexión con mcu-sim-gui, con la simulación en marcha
//
// Fase 4 del plan de dos procesos (`mcu-sim-gui/doc/plan_dos_procesos.md`):
// todo el sentido modelo -> pantalla. Un `SC_THREAD` que despierta cada 100 µs
// de tiempo simulado —el mismo patrón que los dos servidores de GDB
// (`common/gdb_rsp.h`)— y en cada vuelta:
//
//   1. lee lo que haya mandado la ventana: T_SUSCRIBE se aplica a la frontera,
//      T_PING se contesta con T_PONG, y lo de las fases 5 y 6 (órdenes y
//      control) se lee entero y se cuenta;
//   2. pasa al búfer de salida los AVISOS pendientes —los de SC_REPORT, que
//      este módulo desvía mientras está activo— y las INSTANTÁNEAS que haya
//      tomado el muestreador de la fase 1;
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
//     un modelo que funciona.
//
// LA TRAMPA DEL INVARIANTE, la misma que en la frontera: este módulo se
// construye siempre —la elaboración de SystemC es estática— y hasta que alguien
// llama a `activa()` su proceso espera un evento que nadie notifica, y el
// manejador de SC_REPORT es el de siempre. Sin `--gui` no existe. `test407`
// lleva uno construido y sin activar, y su invariante es la prueba.
//
// Lo que NO hace: aplicar órdenes ni controlar la simulación (fases 5 y 6), ni
// saber de sockets: habla con un `CanalGui` (`common/gui_mensajes.h`).
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

    // La suscripción que llegó ANTES de T_ARRANCA. Se aplica antes de
    // `sc_start()`, y por eso la secuencia de instantáneas es reproducible.
    void suscripcion_inicial(const std::string& cuerpo) { aplica_suscripcion(cuerpo); }

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
    uint64_t avisos() const { return n_avisos_; }
    uint64_t estados() const { return n_estados_; }
    uint64_t ignorados() const { return n_ignorados_; }
    uint64_t suscripciones() const { return n_subs_; }
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
        const sc_core::sc_time paso(SONDEO_US, sc_core::SC_US);
        while (conectado_) {
            vuelta();
            if (!conectado_) break;
            wait(paso);
        }
        // Sin ventana el proceso termina: no vuelve a despertar nunca.
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
        if (hubo || reloj_() - ultimo_estado_ >= LATIDO_S)
            pon_estado(mcusim::proto::F_CORRIENDO);
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
                case T_PING:     em_.vacio(sal_, T_PONG); break;
                default:         ++n_ignorados_; break;   // fases 5 y 6, y lo desconocido
            }
        }
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
                if (!fr_.catalogo().existe_obs(id)) {
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

    // Avisos e instantáneas al búfer de salida. `todo`: sin mirar el tope, que
    // es lo que se hace al terminar. Devuelve si salió alguna instantánea.
    bool mueve_a_salida(bool todo) {
        using namespace mcusim::proto;
        while (!avisos_.empty() && (todo || sal_.size() < tope_)) {
            const Aviso& a = avisos_.front();
            em_.mensaje(sal_, T_AVISO, cuerpo_aviso(a.nivel, a.origen, a.texto, a.t));
            avisos_.pop_front();
            ++n_avisos_;
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
        return hubo;
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
    void desborda() {
        using namespace mcusim::proto;
        std::string fin;
        em_.pod(fin, T_FIN, Fin{M_ERROR, 0, ahora_ns()});
        sal_.insert(0, fin);
        escribe();
        pierde("la ventana no lee y se han acumulado " + std::to_string(max_avisos_) +
               " avisos sin mandar; antes que tirar uno se cierra la conexion");
    }

    // Se acabó la conexión. La simulación sigue: sin nadie mirando, se deja de
    // muestrear, que no lo va a leer nadie.
    void pierde(const std::string& por) {
        if (!conectado_) return;
        cierre_ = por;
        std::fprintf(stderr, "gui: %s; se sigue simulando\n", por.c_str());
        cierra_conexion();
        fr_.suscribe(0, {});
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
    uint64_t                  n_inst_ = 0, n_avisos_ = 0, n_estados_ = 0,
                              n_ignorados_ = 0, n_subs_ = 0;
    sc_core::sc_report_handler_proc anterior_ = nullptr;
    sc_core::sc_event         ev_activa_;
};

} // namespace gui
} // namespace stm32

#endif // STM32_PARTS_ENLACE_GUI_H
