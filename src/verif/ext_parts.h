// =============================================================================
// ext_parts.h — Circuitería externa al MCU para el banco de pruebas (fase F3)
//
// El contrato eléctrico del modelo es que el exterior del encapsulado son los
// AnalogNet (uno por pin) y que cualquier cosa conectada a un pin se registra
// como driver Thevenin {V, Rout}; la alta impedancia se expresa con Rout muy
// grande. Estas piezas son los componentes típicos de una placa, y sirven para
// comprobar que los pads del MCU se comportan como en el sistema real:
//
//   Crystal    cristal + condensadores de carga en OSC_IN/OSC_OUT (HSE, LSE)
//   ExtClock   reloj externo de onda cuadrada (HSE/LSE en modo bypass, o
//              estímulo digital de una entrada)
//   Resistor   resistencia a VDD o a VSS (pull externo)
//   Led        LED con resistencia en serie (a VSS: activo en alto; a VDD:
//              activo en bajo, típico de las placas Discovery/Nucleo)
//   Button     pulsador a VSS con pull-up externo opcional
//   Driver     driver digital externo genérico (para forzar niveles y probar
//              conflictos con la salida del MCU)
//   SignalLink pista de placa unidireccional entre dos pines (TX -> RX)
//
// Ninguna de estas piezas forma parte del MCU: viven en verif/.
// =============================================================================
#ifndef STM32_VERIF_EXT_PARTS_H
#define STM32_VERIF_EXT_PARTS_H

#include <systemc>
#include <cmath>
#include "../common/analog_net.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Base: cualquier componente conectado a un pin es un driver del nodo.
// ---------------------------------------------------------------------------
class ExtPart {
public:
    ExtPart(analog_net_if& n, const char* nm) : net_(&n) { id_ = net_->register_driver(nm); }
    // Al destruirse, el componente se "desuelda": deja el nodo en alta
    // impedancia. Sin esto, un componente temporal del banco de pruebas
    // seguiría cargando el pin durante el resto de la simulación.
    virtual ~ExtPart() { if (net_ && id_ >= 0) net_->set_hiz(id_); }
    float  pin_voltage() const { return net_->voltage(); }
    float  pin_current() const { return net_->current(id_); }   // >0: entra al nodo
protected:
    void drive(float v, float r) { net_->set_drive(id_, v, r); }
    void hiz()                   { net_->set_hiz(id_); }
    analog_net_if* net_;
    int id_ = -1;
};

// ---------------------------------------------------------------------------
// Cristal de cuarzo con sus condensadores de carga. Eléctricamente, lo que ve
// el pin OSC_IN es la red de polarización del lazo oscilador: una impedancia
// finita hacia VDD/2. Su presencia es la condición para que el HSE/LSE arranque
// [IR, §4.2]. detach() simula que el cristal se rompe o se desuelda, que es lo
// que debe detectar el CSS.
// ---------------------------------------------------------------------------
class Crystal : public ExtPart {
public:
    Crystal(analog_net_if& osc_in, double vdd = 3.3, double r_bias = 1e6)
        : ExtPart(osc_in, "xtal"), vdd_(vdd), r_(r_bias) { attach(); }
    void attach() { drive(float(vdd_ * 0.5), float(r_)); present_ = true; }
    void detach() { hiz(); present_ = false; }
    bool present() const { return present_; }
private:
    double vdd_, r_;
    bool   present_ = false;
};

// ---------------------------------------------------------------------------
// Reloj externo de onda cuadrada (oscilador de encapsulado o generador).
// ---------------------------------------------------------------------------
SC_MODULE(ExtClock), public ExtPart {
    ExtClock(sc_core::sc_module_name nm, analog_net_if& n, double hz,
             double vdd = 3.3, double r_out = 50.0)
        : sc_core::sc_module(nm), ExtPart(n, "extclk"),
          hz_(hz), vdd_(vdd), r_(r_out) {
        SC_HAS_PROCESS(ExtClock);
        SC_THREAD(run);
    }
    void set_freq(double hz) { hz_ = hz; ev_.notify(sc_core::SC_ZERO_TIME); }
    void stop()              { set_freq(0.0); }
private:
    void run() {
        for (;;) {
            if (hz_ <= 0.0) { hiz(); wait(ev_); continue; }
            const sc_core::sc_time half(0.5e12 / hz_, sc_core::SC_PS);
            drive(lvl_ ? float(vdd_) : 0.0f, float(r_));
            lvl_ = !lvl_;
            wait(half, ev_);
        }
    }
    double hz_, vdd_, r_;
    bool   lvl_ = false;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// Resistencia externa a VDD (pull-up) o a VSS (pull-down).
// ---------------------------------------------------------------------------
class Resistor : public ExtPart {
public:
    Resistor(analog_net_if& n, double to_volts, double ohms)
        : ExtPart(n, "resistor") { drive(float(to_volts), float(ohms)); }
};

// ---------------------------------------------------------------------------
// LED con resistencia en serie. Con to_vss = true el LED se enciende cuando el
// pin está alto (ánodo al pin); con to_vss = false el ánodo va a VDD y el LED
// se enciende cuando el pin baja, que es el montaje habitual en las placas de
// evaluación de ST.
//
// El diodo no es lineal: mientras la tensión aplicada no supera Vf no conduce.
// Se modela con dos estados —conduciendo (equivalente Thevenin {Vf, R}) o en
// corte (alta impedancia)— reevaluados cada vez que cambia la tensión del pin.
// ---------------------------------------------------------------------------
SC_MODULE(Led), public ExtPart {
    Led(sc_core::sc_module_name nm, analog_net_if& n, bool to_vss = true,
        double vf = 2.0, double r_series = 330.0, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPart(n, "led"),
          to_vss_(to_vss), vf_(vf), r_(r_series), vdd_(vdd) {
        SC_HAS_PROCESS(Led);
        SC_THREAD(run);
    }
    bool   on()      const { return on_; }
    double current() const { return std::fabs(double(pin_current())); }
private:
    void run() {
        hiz();
        for (;;) {
            wait(net_->value_changed_event());
            const double v = net_->voltage();
            // Conducción: pin -> LED -> R -> VSS, o VDD -> LED -> R -> pin
            const bool cond = to_vss_ ? (v > vf_) : (v < vdd_ - vf_);
            if (cond) drive(float(to_vss_ ? vf_ : vdd_ - vf_), float(r_));
            else      hiz();
            on_ = cond;
        }
    }
    bool to_vss_; double vf_, r_, vdd_;
    bool on_ = false;
};

// ---------------------------------------------------------------------------
// Pulsador a VSS. Sin pulsar deja el pin abierto (el MCU debe aportar su
// pull-up interno o habrá un nivel indeterminado); pulsado lo lleva a 0 V.
// ---------------------------------------------------------------------------
class Button : public ExtPart {
public:
    Button(analog_net_if& n, double r_closed = 10.0)
        : ExtPart(n, "button"), r_(r_closed) { release(); }
    void press()   { drive(0.0f, float(r_)); down_ = true; }
    void release() { hiz(); down_ = false; }
    bool pressed() const { return down_; }
private:
    double r_; bool down_ = false;
};

// ---------------------------------------------------------------------------
// Pista de placa que une dos pines: lo que hay entre el TX de un puerto serie
// y el RX del otro. Observa la tensión del pin de origen, la digitaliza con el
// mismo umbral que un pad y la reproduce en el pin de destino con una
// impedancia de salida pequeña.
//
// Es UNIDIRECCIONAL por construcción (origen -> destino), que es lo que hace
// falta para un enlace serie full-duplex, donde cada hilo tiene un único
// emisor. Un hilo compartido de verdad (medio dúplex, bus open-drain) se
// modela conectando los dos pines al MISMO AnalogNet, no con esta pieza.
// ---------------------------------------------------------------------------
SC_MODULE(SignalLink), public ExtPart {
    SignalLink(sc_core::sc_module_name nm, analog_net_if& from, analog_net_if& to,
               double vdd = 3.3, double r_out = 50.0)
        : sc_core::sc_module(nm), ExtPart(to, "link"),
          from_(&from), vdd_(vdd), r_(r_out) {
        SC_HAS_PROCESS(SignalLink);
        SC_THREAD(run);
    }
    bool level() const { return lvl_; }
private:
    void run() {
        for (;;) {
            const double v = from_->voltage();
            const bool  fl = from_->floating();
            // Umbral con histéresis, como el trigger de entrada de un pad
            if (!fl) lvl_ = lvl_ ? (v > 0.45 * vdd_) : (v >= 0.55 * vdd_);
            drive(lvl_ ? float(vdd_) : 0.0f, float(r_));
            wait(from_->value_changed_event());
        }
    }
    analog_net_if* from_;
    double vdd_, r_;
    bool   lvl_ = true;
};

// ---------------------------------------------------------------------------
// Driver digital externo genérico: permite forzar un nivel sobre un pin (por
// ejemplo para comprobar el conflicto con una salida push-pull del MCU, o el
// comportamiento correcto de un bus open-drain).
// ---------------------------------------------------------------------------
class Driver : public ExtPart {
public:
    Driver(analog_net_if& n, double vdd = 3.3, double r_out = 25.0)
        : ExtPart(n, "extdrv"), vdd_(vdd), r_(r_out) { hiz(); }
    void set(bool level) { drive(level ? float(vdd_) : 0.0f, float(r_)); }
    void set_volts(double v, double r) { drive(float(v), float(r)); }
    void release()       { hiz(); }
private:
    double vdd_, r_;
};

} // namespace stm32
#endif // STM32_VERIF_EXT_PARTS_H
