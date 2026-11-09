// =============================================================================
// congela.h — El tiempo simulado frente al de pared cuando hay un depurador
//
// Dos cosas, las dos para los servidores que atienden a un programa de fuera
// (OpenOCD por remote_bitbang o CMSIS-DAP, GDB, el JTAG del USB):
//
// 1. RITMO REAL (`RitmoReal`). Con el núcleo parado y el depurador callado, el
//    modelo corre muy por delante del reloj de pared: con el ESP32-C3 parado,
//    unas 120 veces el tiempo real. Las herramientas cuentan con el reloj de
//    pared: OpenOCD tarda hasta 100 ms (poll_period) en ver una parada y
//    apagar los perros guardianes, y en ese tiempo el perro de interrupciones
//    de ESP-IDF (300 ms) ya ha reseteado el chip (el fallo intermitente de O5
//    de openocd_c3.py, doc/todo.md). Mientras haya un programa conectado,
//    cada paso ocioso del servidor dura al menos lo mismo en el reloj de
//    pared: el modelo no adelanta al tiempo real (si va más lento, no gana
//    crédito). La espera se hace sobre el socket, así que una orden la corta.
//    Lo enciende `sim` (`congela::estado().ritmo_real`); los bancos de
//    pruebas no, y su tiempo simulado no cambia.
//
// 2. CONGELAR (`--stopped-core-stops-sim`). Con un núcleo parado por la
//    depuración -cualquiera, de cualquier MCU de la placa-, el tiempo
//    simulado no avanza: ni los periféricos ni las piezas. El `Congelador`
//    lo consigue sin tocar ningún modelo: mientras haya un núcleo parado,
//    se queda en el mismo instante dando vueltas de ciclo delta, y nada que
//    espere a un instante futuro llega a ejecutarse. Lo que sí tiene que
//    seguir es el camino del depurador, y por eso sus esperas pasan por
//    aquí: `espera(t)` es `wait(t)`, salvo congelado, que son dos deltas (un
//    bit de JTAG o un acceso al bus no cuestan tiempo); `reposo(t, ev)` es la
//    espera ociosa de un servidor, que congelado despierta en cada vuelta.
//    Las esperas temporizadas despiertan también con la `ronda()`, para que
//    una que estaba a medias cuando el núcleo se paró no se quede colgada.
//    Sin la opción, `espera` y `reposo` son exactamente `wait`.
//
//    El Congelador duerme en el reloj de pared cuando nadie ha hecho nada en
//    tres vueltas seguidas (de 50 us a 1 ms), para no gastar un núcleo del
//    anfitrión. Es «todo parado» (all-stop): un núcleo parado congela la
//    placa entera, aunque haya otros en marcha.
// =============================================================================
#ifndef STM32_COMMON_CONGELA_H
#define STM32_COMMON_CONGELA_H

#include <systemc>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "red.h"

namespace stm32 {
namespace congela {

// Un núcleo que la depuración puede parar
// `parado`: parado por la depuración y NO en reset (un reset pedido con el
// núcleo parado tiene que poder avanzar). `cambios`: los eventos que pueden
// cambiar `parado` (si falta, se sondea cada 10 us simulados)
struct Nucleo {
    std::string nombre;
    std::function<bool()> parado;
    std::function<void(sc_core::sc_event_or_list&)> cambios;
};

struct Estado {
    bool ritmo_real = false;      // `sim`: el modelo no adelanta al reloj de pared con un depurador
    bool habilitado = false;      // --stopped-core-stops-sim
    bool congelado = false;       // ahora mismo, un núcleo parado
    bool actividad = false;       // alguien ha hecho algo en esta vuelta
    volatile std::sig_atomic_t* senal = nullptr;   // Ctrl-C: se sale aunque esté congelado
    uint64_t n_congelaciones = 0;
};
inline Estado& estado() { static Estado e; return e; }
inline bool activo() { return estado().congelado; }
inline void actividad() { estado().actividad = true; }
inline sc_core::sc_event& ronda() {
    static sc_core::sc_event* e = new sc_core::sc_event("congela_ronda");
    return *e;
}
// Una vez, al empezar cada congelación: lo que tenga un retardo en camino
// (el de propagación de un pad) lo aplica ya
inline sc_core::sc_event& inicio() {
    static sc_core::sc_event* e = new sc_core::sc_event("congela_inicio");
    return *e;
}

// Una espera del camino del depurador (un medio bit, un acceso al bus)
constexpr unsigned DELTAS = 16;
inline void espera(const sc_core::sc_time& t) {
    Estado& e = estado();
    if (!e.habilitado) { sc_core::wait(t); return; }
    if (e.congelado) {
        // Sin tiempo, pero con deltas de sobra para que lo que se acaba de
        // poner en un pin llegue por la cadena de procesos (el DAP, la
        // matriz de pines, el nodo) antes de que nadie lo lea
        for (unsigned i = 0; i < DELTAS; ++i) {
            e.actividad = true;
            sc_core::wait(sc_core::SC_ZERO_TIME);
        }
        return;
    }
    if (t == sc_core::SC_ZERO_TIME) { sc_core::wait(sc_core::SC_ZERO_TIME); return; }
    sc_core::wait(t, ronda());
}
inline void espera(double v, sc_core::sc_time_unit u) { espera(sc_core::sc_time(v, u)); }
// La espera ociosa de un servidor: congelado, hasta la vuelta siguiente
inline void reposo(const sc_core::sc_time& t, const sc_core::sc_event& ev) {
    Estado& e = estado();
    if (!e.habilitado) { sc_core::wait(t, ev); return; }
    if (e.congelado) { sc_core::wait(ronda() | ev); return; }
    sc_core::wait(t, ronda() | ev);
}
inline void reposo(const sc_core::sc_time& t) {
    Estado& e = estado();
    if (!e.habilitado) { sc_core::wait(t); return; }
    if (e.congelado) { sc_core::wait(ronda()); return; }
    sc_core::wait(t, ronda());
}

// ---------------------------------------------------------------------------
// El ritmo real de un servidor: llamar a `paso(socket, dt)` antes de cada
// avance ocioso de `dt` con un programa conectado; `suelta()` al desconectar.
// ---------------------------------------------------------------------------
class RitmoReal {
public:
    void paso(red::socket_t s, const sc_core::sc_time& dt) {
        const Estado& e = estado();
        if (!e.ritmo_real || e.congelado) { ancla_ = false; return; }
        const auto ahora_w = std::chrono::steady_clock::now();
        const sc_core::sc_time ahora_s = sc_core::sc_time_stamp();
        if (!ancla_) { aw_ = ahora_w; as_ = ahora_s; ancla_ = true; }
        const double ew = std::chrono::duration<double>(ahora_w - aw_).count();
        const double es = (ahora_s - as_).to_seconds() + dt.to_seconds();
        if (es <= ew) { aw_ = ahora_w; as_ = ahora_s; return; }   // va más lento: sin crédito
        const double adelanto = es - ew;
        if (adelanto < 1e-3) return;                            // de ms en ms
        const long us = long(adelanto * 1e6);
        if (red::valido(s)) red::espera_legible_us(s, us);
        else std::this_thread::sleep_for(std::chrono::microseconds(us));
    }
    void suelta() { ancla_ = false; }

private:
    bool ancla_ = false;
    std::chrono::steady_clock::time_point aw_;
    sc_core::sc_time as_;
};

// ---------------------------------------------------------------------------
// El Congelador: lo crea `sim` con --stopped-core-stops-sim
// ---------------------------------------------------------------------------
SC_MODULE(Congelador) {
    std::vector<Nucleo> nucleos;

    SC_CTOR(Congelador) {
        SC_THREAD(run);
    }

private:
    bool alguno(std::string* quien = nullptr) const {
        for (const Nucleo& n : nucleos)
            if (n.parado && n.parado()) { if (quien) *quien = n.nombre; return true; }
        return false;
    }
    void run() {
        Estado& e = estado();
        if (nucleos.empty()) return;
        bool sondeo = false;
        sc_core::sc_event_or_list cambios;
        for (const Nucleo& n : nucleos) {
            if (n.cambios) n.cambios(cambios);
            else sondeo = true;
        }
        for (;;) {
            if (!alguno()) {
                if (sondeo) sc_core::wait(sc_core::sc_time(10, sc_core::SC_US), cambios);
                else sc_core::wait(cambios);
                continue;
            }
            std::string quien;
            alguno(&quien);
            e.congelado = true;
            ++e.n_congelaciones;
            inicio().notify(sc_core::SC_ZERO_TIME);
            std::printf("[congela] %s parado: el tiempo simulado se detiene en t = %.6f ms\n",
                        quien.c_str(), sc_core::sc_time_stamp().to_seconds() * 1e3);
            std::fflush(stdout);
            long dormir_us = 0;
            bool primera = true;
            unsigned quietas = 0;
            while (alguno()) {
                if (e.senal && *e.senal) { e.congelado = false; sc_core::sc_stop(); return; }
                // Dentro de un delta el orden de los procesos no está dicho:
                // la marca de quien trabaja puede llegar justo después de
                // mirarla. Se duerme solo tras tres vueltas seguidas calladas
                if (e.actividad) { dormir_us = 0; quietas = 0; }
                else if (++quietas >= 3) {
                    dormir_us = dormir_us ? std::min(dormir_us * 2, 1000L) : 50L;
                    std::this_thread::sleep_for(std::chrono::microseconds(dormir_us));
                }
                // Los servidores ociosos miran sus sockets en las vueltas en
                // las que nadie hace nada (y en la primera, que corta las
                // esperas temporizadas que había a medias)
                const bool callados = !e.actividad;
                e.actividad = false;
                if (callados || primera) ronda().notify(sc_core::SC_ZERO_TIME);
                primera = false;
                sc_core::wait(sc_core::SC_ZERO_TIME);
            }
            e.congelado = false;
            std::printf("[congela] en marcha otra vez en t = %.6f ms\n",
                        sc_core::sc_time_stamp().to_seconds() * 1e3);
            std::fflush(stdout);
            ronda().notify(sc_core::SC_ZERO_TIME);
        }
    }
};

} // namespace congela
} // namespace stm32
#endif // STM32_COMMON_CONGELA_H
