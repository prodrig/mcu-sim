// =============================================================================
// motor_uart.h — Una línea serie asíncrona a nivel de bit, sobre un AnalogNet
//
// Fase D1 del plan de `doc/analisis_puente_serie.md` §10. Hasta ahora el único
// receptor UART del lado de FUERA del chip vivía dentro de `SwoReceiver`: el
// SWO en modo NRZ es una UART corriente, así que aquel analizador ya sabía
// esperar el bit de arranque, muestrear en el centro y comprobar la parada.
// El puente serie (P-14) necesita lo mismo y bastante más, y copiarlo sería
// tener dos receptores que un día dejan de decir lo mismo. Así que se saca
// aquí, y `SwoReceiver` pasa a usarlo.
//
// EL CRITERIO DE ESTA FASE ES QUE `test407` NO SE ENTERE. El receptor hace, en
// 8N1, EXACTAMENTE las mismas esperas que hacía `SwoReceiver`, con las mismas
// expresiones de tiempo -`sc_time(tb * 1.5, SC_SEC)` y `sc_time(tb, SC_SEC)`,
// con `tb = 1.0 / bitrate`-: la conversión de un `double` de segundos a
// picosegundos redondea, y una forma distinta de escribir la misma cuenta
// podría redondear distinto. Si el invariante del F407 se moviera, esta
// extracción habría cambiado algo, y eso es lo que la fase tiene que demostrar
// que no pasa.
//
// DOS PIEZAS, porque son dos cosas:
//
//   ReceptorUart   lee un nodo. No conduce: no registra driver. Bloquea el
//                  SC_THREAD que lo llama hasta que llega una trama.
//   EmisorUart     conduce un nodo con un Thevenin {V, R}, como `SignalLink`:
//                  reposo en alto, y cada bit con la impedancia de salida que
//                  se le dé.
//
// Ninguna es un `sc_module`: son ayudantes que se usan DENTRO del hilo de una
// pieza. Así una pieza puede tener un receptor, un emisor o los dos, y el
// `SwoReceiver` sigue siendo el mismo módulo con el mismo nombre.
//
// LO QUE NO HACE, dicho: el muestreo triple de la USART (tres muestras en el
// centro y voto, con su bandera NF). Aquí se muestrea UNA vez en el centro, que
// es lo que hacía `SwoReceiver` y lo que hace falta para no mover el
// invariante. Un receptor de terminal no informa de ruido, así que al puente no
// le falta nada; si algún día hiciera falta, va detrás de un parámetro.
// =============================================================================
#ifndef STM32_PARTS_MOTOR_UART_H
#define STM32_PARTS_MOTOR_UART_H

#include <systemc>
#include <cstdint>
#include "../common/analog_net.h"
#include "../common/formato_uart.h"

namespace stm32 {

// Lo que sale de la línea por cada trama.
struct TramaUart {
    uint16_t dato          = 0;      // los `bits` de datos, LSB primero
    bool     error_trama   = false;  // el primer bit de parada llegó a 0
    bool     error_paridad = false;  // la paridad no cuadra (si la hay)
    bool     es_break      = false;  // TODO a cero, parada incluida
    bool ok() const { return !error_trama && !error_paridad; }
};

// ---------------------------------------------------------------------------
// El receptor
// ---------------------------------------------------------------------------
class ReceptorUart {
public:
    ReceptorUart(analog_net_if& net, double bitrate, double vdd = 3.3)
        : net_(&net), tb_(1.0 / bitrate), vdd_(vdd) {}

    void set_bitrate(double b) { tb_ = 1.0 / b; }
    double bitrate() const { return 1.0 / tb_; }
    void set_formato(const FormatoUart& f) { f_ = f; }
    const FormatoUart& formato() const { return f_; }

    // Nivel lógico de la línea. UN solo umbral, a VDD/2, y sin histéresis: es
    // el comparador que tenía `SwoReceiver`, y cambiarlo cambiaría en qué
    // instante se ve cada flanco.
    bool nivel() const { return net_->voltage() > 0.5 * vdd_; }

    // Espera una trama y la devuelve. Bloquea el SC_THREAD que la llama.
    //
    // Primero espera a que la línea esté EN REPOSO (alta). Sin esto, al
    // arrancar la simulación el nodo todavía no lo gobierna nadie y el
    // receptor tomaría el nivel indefinido por un bit de arranque,
    // desincronizando toda la trama. Y después de un break, esto es lo que
    // espera a que la línea vuelva.
    TramaUart recibe() {
        TramaUart t;
        while (!nivel()) sc_core::wait(net_->value_changed_event());
        // Y ahora sí, el flanco de bajada del bit de arranque.
        while (nivel()) sc_core::wait(net_->value_changed_event());
        sc_core::wait(sc_core::sc_time(tb_ * 1.5, sc_core::SC_SEC));   // al centro del bit 0
        bool todo_cero = true;
        for (unsigned i = 0; i < f_.bits; ++i) {
            if (nivel()) { t.dato = uint16_t(t.dato | (1u << i)); todo_cero = false; }
            sc_core::wait(sc_core::sc_time(tb_, sc_core::SC_SEC));
        }
        if (f_.con_paridad()) {
            const bool p = nivel();
            if (p) todo_cero = false;
            t.error_paridad = (p != f_.bit_de_paridad(t.dato));
            sc_core::wait(sc_core::sc_time(tb_, sc_core::SC_SEC));
        }
        // El PRIMER bit de parada, en su centro. Los demás (el medio o el
        // segundo) no se comprueban: un receptor real tampoco lo hace, y es lo
        // que permite que un emisor con dos de parada hable con uno que espera
        // uno.
        if (!nivel()) {
            t.error_trama = true;
            t.es_break = todo_cero;
        }
        ++n_tramas_;
        if (t.error_trama)   ++n_err_trama_;
        if (t.error_paridad) ++n_err_paridad_;
        if (t.es_break)      ++n_breaks_;
        return t;
    }

    uint64_t tramas()          const { return n_tramas_; }
    uint64_t errores_trama()   const { return n_err_trama_; }
    uint64_t errores_paridad() const { return n_err_paridad_; }
    uint64_t breaks()          const { return n_breaks_; }
    void borra_contadores() { n_tramas_ = n_err_trama_ = n_err_paridad_ = n_breaks_ = 0; }

private:
    analog_net_if* net_;
    double      tb_, vdd_;
    FormatoUart f_{};
    uint64_t    n_tramas_ = 0, n_err_trama_ = 0, n_err_paridad_ = 0, n_breaks_ = 0;
};

// ---------------------------------------------------------------------------
// El emisor
// ---------------------------------------------------------------------------
// Conduce el nodo con el driver `id`, que tiene que haber registrado quien lo
// posee (la pieza, con `add_pin`). No se queda con el nodo: lo gobierna.
class EmisorUart {
public:
    EmisorUart(analog_net_if& net, int driver_id, double bitrate,
               double vdd = 3.3, double r_out = 50.0)
        : net_(&net), id_(driver_id), tb_(1.0 / bitrate), vdd_(vdd), r_(r_out) {}

    void set_bitrate(double b) { tb_ = 1.0 / b; }
    double bitrate() const { return 1.0 / tb_; }
    void set_formato(const FormatoUart& f) { f_ = f; }
    const FormatoUart& formato() const { return f_; }

    // La línea en reposo: alta. Es lo primero que hay que hacer, o el
    // receptor del otro lado ve un nodo sin gobierno.
    void reposo() { nivel(true); }
    void suelta() { net_->set_hiz(id_); }

    // Una trama entera: arranque, datos LSB primero, paridad y parada. Bloquea
    // el SC_THREAD que la llama lo que dura la trama, parada incluida.
    void emite(unsigned dato) {
        const sc_core::sc_time tb(tb_, sc_core::SC_SEC);
        nivel(false);                                   // arranque
        sc_core::wait(tb);
        for (unsigned i = 0; i < f_.bits; ++i) {
            nivel(((dato >> i) & 1u) != 0);
            sc_core::wait(tb);
        }
        if (f_.con_paridad()) {
            nivel(f_.bit_de_paridad(dato & f_.mascara()));
            sc_core::wait(tb);
        }
        nivel(true);                                    // parada
        sc_core::wait(sc_core::sc_time(tb_ * 0.5 * f_.medios_de_parada(),
                                       sc_core::SC_SEC));
        ++n_tramas_;
    }

    // Break: la línea a cero `bits_linea` tiempos de bit (por omisión, una
    // trama entera y uno más, que es lo mínimo para que se reconozca), y
    // después a reposo. Lo que la USART del modelo ve como `LBD`.
    void emite_break(unsigned bits_linea = 0) {
        if (!bits_linea) bits_linea = f_.medios_de_trama() / 2u + 1u;
        nivel(false);
        sc_core::wait(sc_core::sc_time(tb_ * bits_linea, sc_core::SC_SEC));
        nivel(true);
        ++n_breaks_;
    }

    // Una trama con la paridad AL REVÉS, para provocar un error de paridad en
    // el receptor. Es una herramienta de pruebas, y se llama así para que no
    // haya duda.
    void emite_con_paridad_mala(unsigned dato) {
        if (!f_.con_paridad()) { emite(dato); return; }   // no hay que estropear
        const FormatoUart f = f_;
        const bool buena = f_.bit_de_paridad(dato & f_.mascara());
        f_.paridad = buena ? Paridad::espacio : Paridad::marca;
        emite(dato);
        f_ = f;
    }

    uint64_t tramas() const { return n_tramas_; }
    uint64_t breaks() const { return n_breaks_; }

private:
    void nivel(bool alto) { net_->set_drive(id_, alto ? float(vdd_) : 0.0f, float(r_)); }

    analog_net_if* net_;
    int         id_;
    double      tb_, vdd_, r_;
    FormatoUart f_{};
    uint64_t    n_tramas_ = 0, n_breaks_ = 0;
};

} // namespace stm32

#endif // STM32_PARTS_MOTOR_UART_H
