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
#include <vector>
#include <cstdio>
#include <utility>
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
    // Soldar o quitar la pista. Sin ella el pin de destino queda como estaba:
    // así una misma placa sirve para pruebas que necesitan el enlace y para
    // otras que usan ese pin para otra cosa.
    void set_enabled(bool on) { on_ = on; ev_.notify(sc_core::SC_ZERO_TIME); }
private:
    void run() {
        for (;;) {
            if (!on_) { hiz(); wait(ev_); continue; }
            const double v = from_->voltage();
            const bool  fl = from_->floating();
            // Umbral con histéresis, como el trigger de entrada de un pad
            if (!fl) lvl_ = lvl_ ? (v > 0.45 * vdd_) : (v >= 0.55 * vdd_);
            drive(lvl_ ? float(vdd_) : 0.0f, float(r_));
            wait(from_->value_changed_event() | ev_);
        }
    }
    analog_net_if* from_;
    double vdd_, r_;
    bool   lvl_ = true;
    bool   on_  = true;
    sc_core::sc_event ev_;
};

// ===========================================================================
// Piezas de un bus I2C. El bus es de COLECTOR ABIERTO: nadie fuerza un uno,
// solo se tira de la línea a cero o se suelta, y el nivel alto lo dan las
// resistencias de pull-up. Todo eso ocurre de verdad en el nodo analógico: el
// cero gana porque su resistencia de salida es mil veces menor que la del
// pull-up, no porque el modelo lo decida.
// ===========================================================================

// Base común: una pieza conectada a las dos líneas del bus, capaz de tirar de
// cada una a cero o soltarla, y de leerlas con el umbral de un receptor I2C.
class I2cPart {
public:
    I2cPart(analog_net_if& scl, analog_net_if& sda, const char* nm, double vdd = 3.3)
        : scl_(&scl), sda_(&sda), vdd_(vdd) {
        id_scl_ = scl_->register_driver(nm);
        id_sda_ = sda_->register_driver(nm);
        release_scl(); release_sda();
    }
    virtual ~I2cPart() { scl_->set_hiz(id_scl_); sda_->set_hiz(id_sda_); }
    bool scl() const { return scl_->voltage() > 0.5 * vdd_; }
    bool sda() const { return sda_->voltage() > 0.5 * vdd_; }
protected:
    void pull_scl()    { scl_->set_drive(id_scl_, 0.0f, 30.0f); }
    void release_scl() { scl_->set_hiz(id_scl_); }
    void pull_sda()    { sda_->set_drive(id_sda_, 0.0f, 30.0f); }
    void release_sda() { sda_->set_hiz(id_sda_); }
    void drive_sda(bool level) { if (level) release_sda(); else pull_sda(); }
    analog_net_if *scl_, *sda_;
    double vdd_;
    int id_scl_ = -1, id_sda_ = -1;
};

// ---------------------------------------------------------------------------
// Hilo de bus: une varios pines en un mismo nodo eléctrico y les pone el
// pull-up. Cada pin conserva su propio AnalogNet, así que el hilo propaga el
// cero de uno a los demás; para decidir quién está tirando de verdad consulta
// la tensión de cada nodo EXCLUYENDO su propia aportación
// (analog_net_if::voltage_excluding), que es lo que evita que la propagación se
// realimente y se quede enganchada.
// ---------------------------------------------------------------------------
SC_MODULE(I2cWire) {
    I2cWire(sc_core::sc_module_name nm, std::vector<analog_net_if*> nets,
            double vdd = 3.3, double r_pull = 4700.0)
        : sc_core::sc_module(nm), nets_(std::move(nets)), vdd_(vdd) {
        for (analog_net_if* n : nets_) {
            id_pu_.push_back(n->register_driver("i2c_pullup"));
            id_pd_.push_back(n->register_driver("i2c_wire"));
            n->set_drive(id_pu_.back(), float(vdd), float(r_pull));
            n->set_hiz(id_pd_.back());
        }
        SC_HAS_PROCESS(I2cWire);
        SC_THREAD(run);
    }
    void set_enabled(bool on) { on_ = on; ev_.notify(sc_core::SC_ZERO_TIME); }
private:
    void run() {
        for (;;) {
            const size_t n = nets_.size();
            std::vector<bool> low(n, false);
            for (size_t i = 0; i < n; ++i) {
                bool fl = false;
                const float v = nets_[i]->voltage_excluding(id_pd_[i], fl);
                low[i] = on_ && !fl && (v < 0.3 * vdd_);
            }
            for (size_t j = 0; j < n; ++j) {
                bool any = false;
                for (size_t i = 0; i < n; ++i) if (i != j && low[i]) any = true;
                if (any) nets_[j]->set_drive(id_pd_[j], 0.0f, 20.0f);
                else     nets_[j]->set_hiz(id_pd_[j]);
                nets_[j]->set_drive(id_pu_[j], float(vdd_), on_ ? 4700.0f : R_HIZ);
            }
            sc_core::sc_event_or_list any_change;
            for (analog_net_if* p : nets_) any_change |= p->value_changed_event();
            wait(any_change | ev_);
        }
    }
    std::vector<analog_net_if*> nets_;
    std::vector<int> id_pu_, id_pd_;
    double vdd_;
    bool on_ = true;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// Memoria EEPROM serie de la familia 24Cxx: el esclavo I2C típico de una placa.
// Protocolo: [START][dir|W][ADDR][dato]...[STOP] para escribir y
// [START][dir|W][ADDR][RESTART][dir|R][dato]...[NACK][STOP] para leer.
// Puede estirar el reloj tras el byte de dirección, que es lo que obliga al
// maestro a respetar el clock stretching.
// ---------------------------------------------------------------------------
SC_MODULE(I2cEeprom), public I2cPart {
    I2cEeprom(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
              uint8_t dev_addr = 0x50, double vdd = 3.3)
        : sc_core::sc_module(nm), I2cPart(scl, sda, "eeprom", vdd), addr_(dev_addr) {
        for (unsigned i = 0; i < sizeof mem_; ++i) mem_[i] = uint8_t(0xA0 + i);
        SC_HAS_PROCESS(I2cEeprom);
        SC_METHOD(edge_proc);
        sensitive << scl_->value_changed_event() << sda_->value_changed_event();
        dont_initialize();
        SC_METHOD(unstretch); sensitive << stretch_ev_; dont_initialize();
    }
    uint8_t  peek(unsigned i) const { return mem_[i % sizeof mem_]; }
    void     poke(unsigned i, uint8_t v) { mem_[i % sizeof mem_] = v; }
    unsigned bytes_written() const { return n_wr_; }
    unsigned bytes_read()    const { return n_rd_; }
    // Estiramiento del reloj: microsegundos que retiene SCL tras la dirección
    void set_stretch_us(double us) { stretch_us_ = us; }
    void set_ack(bool on) { do_ack_ = on; }      // para provocar un NACK
private:
    // Estados. Un ACK ocupa un bit completo: se tira de SDA en el flanco de
    // bajada, el maestro lo muestrea en el de subida y solo entonces se suelta
    // la línea. Confundir esos dos flancos deja SDA clavada a cero y el maestro
    // pierde el arbitraje, que es justo lo que pasa en una placa real.
    enum St { IDLE, ADDR, ACK_HOLD, RX, TX, MACK };
    void edge_proc() {
        const bool c = scl(), d = sda();
        const bool cp = c_prev_, dp = d_prev_;
        c_prev_ = c; d_prev_ = d;

        if (c && cp) {                                   // START / STOP
            if (dp && !d) { st_ = ADDR; bit_ = 0; sh_ = 0; release_sda(); return; }
            if (!dp && d) { st_ = IDLE; release_sda(); release_scl(); return; }
        }
        if (c && !cp) {                                  // flanco de subida
            switch (st_) {
                case ADDR:
                    sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                    if (++bit_ == 8) {
                        if ((sh_ >> 1) == addr_ && do_ack_) {
                            reading_ = (sh_ & 1u) != 0;
                            got_ptr_ = reading_ ? got_ptr_ : false;
                            ack_next_ = true;            // reconoceremos
                        } else { st_ = IDLE; ack_next_ = false; }
                    }
                    break;
                case RX:
                    sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                    if (++bit_ == 8) {
                        if (!got_ptr_) { ptr_ = sh_; got_ptr_ = true; }
                        else { mem_[ptr_++ % sizeof mem_] = sh_; ++n_wr_; }
                        ack_next_ = true;
                    }
                    break;
                case ACK_HOLD:                           // el maestro nos ha leído
                    if (reading_) { st_ = TX; bit_ = 0;
                                    sh_ = mem_[ptr_++ % sizeof mem_]; ++n_rd_; }
                    else          { st_ = RX; bit_ = 0; sh_ = 0; }
                    break;
                case TX:
                    // El octavo bit se pone en el flanco de bajada, pero el
                    // maestro lo muestrea en ESTE de subida: solo después de
                    // muestrearlo empieza el bit de reconocimiento.
                    if (bit_ >= 8) st_ = MACK;
                    break;
                case MACK:                               // leemos el ACK del maestro
                    if (d) { st_ = IDLE; release_sda(); }        // NACK: fin
                    else   { st_ = TX; bit_ = 0;
                             sh_ = mem_[ptr_++ % sizeof mem_]; ++n_rd_; }
                    break;
                default: break;
            }
            return;
        }
        if (!c && cp) {                                  // flanco de bajada
            if (ack_next_) {                             // toca reconocer
                ack_next_ = false;
                pull_sda();
                st_ = ACK_HOLD;
                if (stretch_us_ > 0.0) {                 // retener el reloj
                    pull_scl();
                    stretch_ev_.notify(stretch_us_, sc_core::SC_US);
                }
                return;
            }
            switch (st_) {
                case RX:
                    if (bit_ == 0) release_sda();        // soltar tras el ACK
                    break;
                case TX:
                    if (bit_ < 8) { drive_sda((sh_ >> (7 - bit_)) & 1u); ++bit_; }
                    break;
                case MACK:
                    release_sda();                       // dejar contestar
                    break;
                default: break;
            }
        }
    }
    void unstretch() { release_scl(); }
    uint8_t addr_;
    uint8_t mem_[64] = {};
    St   st_ = IDLE;
    unsigned bit_ = 0, ptr_ = 0, n_wr_ = 0, n_rd_ = 0;
    uint8_t  sh_ = 0;
    bool c_prev_ = true, d_prev_ = true, reading_ = false, got_ptr_ = false;
    bool do_ack_ = true, ack_next_ = false;
    double stretch_us_ = 0.0;
    sc_core::sc_event stretch_ev_;
};

// ---------------------------------------------------------------------------
// Maestro I2C externo: otro microcontrolador en la misma placa. Sirve para
// probar el MCU como ESCLAVO y para provocar la pérdida de arbitraje.
// ---------------------------------------------------------------------------
SC_MODULE(I2cExtMaster), public I2cPart {
    I2cExtMaster(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
                 double f_scl = 100e3, double vdd = 3.3)
        : sc_core::sc_module(nm), I2cPart(scl, sda, "extmaster", vdd),
          half_(sc_core::sc_time(0.5e12 / f_scl, sc_core::SC_PS)) {
        SC_HAS_PROCESS(I2cExtMaster);
        SC_THREAD(run);
    }
    // Programa una transacción; el hilo la ejecuta y publica el resultado.
    void request_write(uint8_t addr7, const uint8_t* d, unsigned n) {
        addr_ = addr7; nw_ = (n > 8 ? 8 : n); nr_ = 0;
        for (unsigned i = 0; i < nw_; ++i) buf_[i] = d[i];
        busy_ = true; done_ = false; go_.notify(sc_core::SC_ZERO_TIME);
    }
    void request_read(uint8_t addr7, unsigned n) {
        addr_ = addr7; nw_ = 0; nr_ = (n > 8 ? 8 : n);
        busy_ = true; done_ = false; go_.notify(sc_core::SC_ZERO_TIME);
    }
    bool done()      const { return done_; }
    bool addr_acked()const { return acked_; }
    uint8_t rx(unsigned i) const { return buf_[i % 8]; }
    unsigned n_acked() const { return n_acked_; }
private:
    void bit_out(bool b) {
        pull_scl(); wait(half_ / 2.0);
        drive_sda(b); wait(half_ / 2.0);
        release_scl(); wait_scl(); wait(half_);
        pull_scl();
    }
    bool bit_in() {
        pull_scl(); wait(half_ / 2.0);
        release_sda(); wait(half_ / 2.0);
        release_scl(); wait_scl(); wait(half_ / 2.0);
        const bool b = sda();
        wait(half_ / 2.0);
        pull_scl();
        return b;
    }
    void wait_scl() {                       // respeta el estiramiento del esclavo
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        while (!scl() && sc_core::sc_time_stamp() - t0 < half_ * 500.0)
            wait(half_ / 4.0, scl_->value_changed_event());
    }
    bool byte_out(uint8_t v) {
        for (int i = 7; i >= 0; --i) bit_out((v >> i) & 1u);
        return !bit_in();                   // ACK = línea a cero
    }
    uint8_t byte_in(bool ack) {
        uint8_t v = 0;
        for (unsigned i = 0; i < 8; ++i) v = uint8_t((v << 1) | (bit_in() ? 1u : 0u));
        bit_out(!ack);
        return v;
    }
    void run() {
        release_scl(); release_sda();
        for (;;) {
            wait(go_);
            // START
            release_sda(); release_scl(); wait(half_);
            pull_sda(); wait(half_);
            pull_scl();
            n_acked_ = 0;
            acked_ = byte_out(uint8_t((addr_ << 1) | (nr_ ? 1u : 0u)));
            if (acked_) {
                if (nr_) for (unsigned i = 0; i < nr_; ++i)
                             buf_[i] = byte_in(i + 1 < nr_);
                else     for (unsigned i = 0; i < nw_; ++i)
                             if (byte_out(buf_[i])) ++n_acked_;
            }
            // STOP
            pull_scl(); wait(half_ / 2.0);
            pull_sda(); wait(half_ / 2.0);
            release_scl(); wait_scl(); wait(half_);
            release_sda(); wait(half_);
            busy_ = false; done_ = true;
        }
    }
    sc_core::sc_time half_;
    uint8_t addr_ = 0, buf_[8] = {};
    unsigned nw_ = 0, nr_ = 0, n_acked_ = 0;
    bool busy_ = false, done_ = false, acked_ = false;
    sc_core::sc_event go_;
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
