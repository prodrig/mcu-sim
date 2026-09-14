// =============================================================================
// ext_parts.h — LIBRERÍA DE COMPONENTES EXTERNOS al MCU
//
// (Nació en fase F3 como «circuitería del banco de pruebas» y vivía en verif/.
//  Desde el paso 1 de la ruta de adopción del esquema XML+SVG vive en parts/,
//  porque ya no es solo verificación: es la librería de piezas de placa. Todas
//  derivan de ExtPartBase —parts/part_base.h—, declaran sus terminales por
//  NOMBRE y se conectan y desconectan con un único set_enabled(bool).)
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
//   Rpull      resistencia de pull a una tensión cualquiera (3,3 V, 5 V, masa)
//   Led        LED con resistencia en serie (a VSS: activo en alto; a VDD:
//              activo en bajo, típico de las placas Discovery/Nucleo)
//   Button     pulsador a VSS con pull-up externo opcional
//   Driver     driver digital externo genérico (para forzar niveles y probar
//              conflictos con la salida del MCU)
//   SignalLink pista de placa unidireccional entre dos pines (TX -> RX)
//
// Ninguna de estas piezas forma parte del MCU: viven en parts/.
// =============================================================================
#ifndef STM32_PARTS_EXT_PARTS_H
#define STM32_PARTS_EXT_PARTS_H

#include <systemc>
#include <cmath>
#include <cstdio>
#include <vector>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include "../common/analog_net.h"
#include "part_base.h"        // ExtPartBase: terminales con nombre y set_enabled
#include "../periph/sdio.h"   // sd_crc7 y SdCrc16: el protocolo es el mismo
#include "../periph/can.h"    // can_crc15 y el relleno de bits: idem
#include "../periph/otg.h"    // usb_dev_if y los PID: el aparejo habla lo mismo
#include "../periph/eth_mac.h" // eth_fcs: el CRC del cable lo calculan los dos igual
#include "../verif/swd_port.h" // el maestro SWD a nivel de bit, compartido con el stub GDB

namespace stm32 {

// ---------------------------------------------------------------------------
// Base de las piezas de UNA SOLA PATILLA: un LED, un pulsador, una resistencia,
// un cristal. Es una comodidad sobre ExtPartBase —declara el terminal y guarda
// el nodo y el identificador del driver a mano— porque el grueso de la librería
// tiene una patilla y nada más.
//
// El nombre del terminal se puede dar: para el netlist no es lo mismo "anodo"
// que "osc_in", aunque eléctricamente sean la misma clase de conexión.
// ---------------------------------------------------------------------------
class ExtPart : public ExtPartBase {
public:
    // `tipo` es el nombre de la clase tal y como lo escribirá el XML; `drv` es
    // la etiqueta con la que la pieza se apunta como driver del nodo (aparece
    // en los avisos de sobrecorriente del pad); `term`, el nombre nominal de la
    // patilla. El identificador de instancia lo pone la base.
    ExtPart(analog_net_if& n, const char* tipo, const char* drv,
            const char* term = "pin", const char* nombre = nullptr)
        : ExtPartBase(tipo, nombre), net_(&n) { id_ = add_pin(term, n, drv); }
    // Al destruirse, el componente se "desuelda": deja el nodo en alta
    // impedancia. Sin esto, un componente temporal del banco de pruebas
    // seguiría cargando el pin durante el resto de la simulación.
    ~ExtPart() override { if (net_ && id_ >= 0) net_->set_hiz(id_); }
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
        : ExtPart(osc_in, "Crystal", "xtal", "osc_in"), vdd_(vdd), r_(r_bias) { attach(); }
    void attach() { drive(float(vdd_ * 0.5), float(r_)); present_ = true; conectada_ = true; }
    void detach() { hiz(); present_ = false; conectada_ = false; }
    bool present() const { return present_; }
    // Desoldar el cristal es exactamente detach(): el interruptor común de la
    // librería y el que ya tenía esta pieza son la misma operación.
    void set_enabled(bool on) override { if (on) attach(); else detach(); }
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
        : sc_core::sc_module(nm), ExtPart(n, "ExtClock", "extclk", "out", nm),
          hz_(hz), vdd_(vdd), r_(r_out) {
        SC_HAS_PROCESS(ExtClock);
        SC_THREAD(run);
    }
    void set_freq(double hz) { hz_ = hz; ev_.notify(sc_core::SC_ZERO_TIME); }
    void stop()              { set_freq(0.0); }
    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
private:
    void run() {
        for (;;) {
            if (hz_ <= 0.0 || !conectada_) { hiz(); wait(ev_); continue; }
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
// Resistencia de pull: une el nodo con una tensión FIJA a través de un valor
// dado. Es un equivalente Thevenin {V, R} y nada más, así que la tensión puede
// ser cualquiera y no tiene por qué existir en el MCU:
//
//   {3,3 V, 4k7}   pull-up al mismo raíl que el chip
//   {5 V,   4k7}   pull-up a un raíl de 5 V, como el de un bus I2C mixto
//   {0 V,   4k7}   pull-down
//   {1,8 V, 10k}   polarización a media escala para una entrada de ADC
//   {0 V,   1k}    una carga con la que medir cuánta corriente entrega un pad
//
// Los dos últimos enseñan por qué la pieza no se llamó `PullUp`: lo que hace es
// una rama resistiva a un potencial, y de ahí salen tanto los pulls como las
// cargas de prueba.
// ---------------------------------------------------------------------------
class Rpull : public ExtPart {
public:
    Rpull(analog_net_if& n, double to_volts, double ohms)
        : ExtPart(n, "Rpull", "rpull", "a"), v_(to_volts), r_(ohms) {
        drive(float(v_), float(r_));
    }
    void set_enabled(bool on) override {
        conectada_ = on;
        if (on) drive(float(v_), float(r_)); else hiz();
    }
private:
    double v_, r_;
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
    // `vdd` es la tensión del OTRO extremo de la rama, la que no toca el pin.
    // Con to_vss = true ese extremo es masa y `vdd` no interviene; con
    // to_vss = false es la alimentación contra la que se enciende el LED, y NO
    // tiene por qué ser la del MCU: un LED azul de 3,0 V no luce con 3,3 V, así
    // que en una placa real se cuelga de los 5 V con el cátodo al pin.
    //
    // `term` es cómo se llama la patilla que va al pin. Con el ánodo al pin es
    // el ánodo; con el cátodo al pin, el cátodo. El netlist usa ese nombre.
    Led(sc_core::sc_module_name nm, analog_net_if& n, bool to_vss = true,
        double vf = 2.0, double r_series = 330.0, double vdd = 3.3,
        const char* term = "anodo")
        : sc_core::sc_module(nm), ExtPart(n, "Led", "led", term, nm),
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
            // Se evalua ANTES de esperar, no despues. Parece un detalle de
            // estilo y no lo es: esperar primero solo funciona si el nodo se
            // mueve, y hay montajes donde no se mueve nunca -un LED sujeto por
            // una resistencia de pull, con el pin en entrada-. Ahi el LED
            // luciria de verdad y este modelo se quedaba a oscuras porque nadie
            // le habia avisado de nada.
            if (!conectada_) { hiz(); on_ = false; }
            else {
                const double v = net_->voltage();
                // Conducción: pin -> LED -> R -> VSS, o VDD -> LED -> R -> pin
                const bool cond = to_vss_ ? (v > vf_) : (v < vdd_ - vf_);
                if (cond) drive(float(to_vss_ ? vf_ : vdd_ - vf_), float(r_));
                else      hiz();
                on_ = cond;
            }
            wait(net_->value_changed_event() | evento_conexion());
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
    // `v_cerrado` es la tension a la que el pulsador lleva el pin al cerrarse.
    // Por omision 0 V -pulsador a masa, el montaje de siempre-, pero NO todos
    // son asi: en la STM32F4-Discovery el boton de usuario lleva PA0 a VDD y es
    // una resistencia externa la que lo sujeta abajo, por eso el codigo que
    // genera CubeMX para esa placa espera flanco de SUBIDA.
    //
    // `nc` es el REPOSO del contacto, que es cosa distinta de la tension:
    //
    //   nc = false  NORMALMENTE ABIERTO: suelto no conduce, pulsado conduce.
    //               Es el pulsador de toda la vida y el valor por omision.
    //   nc = true   NORMALMENTE CERRADO: suelto CONDUCE, y pulsarlo lo ABRE.
    //               Es lo que hay en un final de carrera de seguridad, en la
    //               seta de emergencia y en cualquier detector cableado para
    //               que un cable cortado se note: si el hilo se rompe, el nodo
    //               queda como si estuviera pulsado, y el sistema para. Un NC
    //               descrito como NA parecerA que funciona hasta el dia en que
    //               se corta el cable, que es justo el dia que importa.
    //
    // Lo que conduce, entonces, no es "pulsado" sino "pulsado XOR normalmente
    // cerrado". Y un pulsador DESOLDADO no conduce nunca, sea del tipo que sea:
    // si no esta, no hay contacto que cerrar.
    Button(analog_net_if& n, double r_closed = 10.0, double v_closed = 0.0,
           bool nc = false)
        : ExtPart(n, "Button", "button", "pin"),
          r_(r_closed), v_(v_closed), nc_(nc) {
        aplica();                 // un NC conduce ya, desde que se construye
    }
    void press()   { down_ = true;  aplica(); }
    void release() { down_ = false; aplica(); }
    // `pressed()` es lo que hace el DEDO; `cerrado()` es lo que hace el
    // CONTACTO. En un NC son opuestos, y confundirlos es el error facil.
    bool pressed() const { return down_; }
    bool cerrado() const { return conectada_ && (down_ != nc_); }
    bool normalmente_cerrado() const { return nc_; }
    double v_cerrado() const { return v_; }
    // Al desoldar se abre el contacto; al volver a soldar, vuelve a su reposo,
    // que en un NC es conduciendo.
    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (!on) down_ = false;
        aplica();
    }
private:
    void aplica() {
        if (cerrado()) drive(float(v_), float(r_));
        else           hiz();
    }
    double r_, v_; bool nc_ = false, down_ = false;
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
        : sc_core::sc_module(nm), ExtPart(to, "SignalLink", "link", "destino", nm),
          from_(&from), vdd_(vdd), r_(r_out) {
        add_ref("origen", from);      // el origen solo se lee: no lleva driver
        SC_HAS_PROCESS(SignalLink);
        SC_THREAD(run);
    }
    bool level() const { return lvl_; }
    // Soldar o quitar la pista. Sin ella el pin de destino queda como estaba:
    // así una misma placa sirve para pruebas que necesitan el enlace y para
    // otras que usan ese pin para otra cosa.
    void set_enabled(bool on) override {
        conectada_ = on; on_ = on; ev_.notify(sc_core::SC_ZERO_TIME);
    }
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
class I2cPart : public ExtPartBase {
public:
    I2cPart(analog_net_if& scl, analog_net_if& sda, const char* tipo,
            const char* drv, const char* nombre, double vdd = 3.3)
        : ExtPartBase(tipo, nombre), scl_(&scl), sda_(&sda), vdd_(vdd) {
        id_scl_ = add_pin("scl", scl, drv);
        id_sda_ = add_pin("sda", sda, drv);
        release_scl(); release_sda();
    }
    ~I2cPart() override { scl_->set_hiz(id_scl_); sda_->set_hiz(id_sda_); }
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
SC_MODULE(I2cWire), public ExtPartBase {
    I2cWire(sc_core::sc_module_name nm, std::vector<analog_net_if*> nets,
            double vdd = 3.3, double r_pull = 4700.0)
        : sc_core::sc_module(nm), ExtPartBase("I2cWire", nm),
          nets_(std::move(nets)), vdd_(vdd) {
        char t[16];
        for (size_t i = 0; i < nets_.size(); ++i) {
            std::snprintf(t, sizeof t, "l%u", unsigned(i));
            // Dos drivers por línea: el pull-up de la placa y el hilo que
            // propaga el cero de los demás.
            id_pu_.push_back(add_pin(t, *nets_[i], "i2c_pullup"));
            id_pd_.push_back(add_drv(t, "i2c_wire"));
            nets_[i]->set_drive(id_pu_.back(), float(vdd), float(r_pull));
            nets_[i]->set_hiz(id_pd_.back());
        }
        // La lista de espera y el vector de trabajo se arman UNA VEZ, aquí, y
        // no en cada vuelta del bucle. Dos motivos, y el segundo es el bueno:
        //
        //   * las líneas del hilo no cambian después de la elaboración, así que
        //     rearmar la lista en cada despertar es trabajo repetido en el
        //     camino caliente de todas las transacciones del bus;
        //   * y un objeto declarado dentro del bucle de un SC_THREAD vive a
        //     través del `wait()`. Cuando la simulación termina, el hilo se
        //     queda suspendido y su pila NO SE DESENROLLA: los destructores de
        //     sus locales no llegan a correr nunca. Lo que hayan reservado se
        //     pierde. Es una fuga acotada —una por proceso— pero ensucia el
        //     informe de LeakSanitizer, y un informe sucio es un informe que
        //     nadie mira.
        for (analog_net_if* p : nets_) espera_ |= p->value_changed_event();
        espera_ |= ev_;
        bajo_.assign(nets_.size(), false);
        SC_HAS_PROCESS(I2cWire);
        SC_THREAD(run);
    }
    void set_enabled(bool on) override {
        conectada_ = on; on_ = on; ev_.notify(sc_core::SC_ZERO_TIME);
    }
private:
    void run() {
        const size_t n = nets_.size();
        for (;;) {
            for (size_t i = 0; i < n; ++i) {
                bool fl = false;
                const float v = nets_[i]->voltage_excluding(id_pd_[i], fl);
                bajo_[i] = on_ && !fl && (v < 0.3 * vdd_);
            }
            for (size_t j = 0; j < n; ++j) {
                bool any = false;
                for (size_t i = 0; i < n; ++i) if (i != j && bajo_[i]) any = true;
                if (any) nets_[j]->set_drive(id_pd_[j], 0.0f, 20.0f);
                else     nets_[j]->set_hiz(id_pd_[j]);
                nets_[j]->set_drive(id_pu_[j], float(vdd_), on_ ? 4700.0f : R_HIZ);
            }
            wait(espera_);      // armada en el constructor: ni reserva ni fuga
        }
    }
    std::vector<analog_net_if*> nets_;
    std::vector<int> id_pu_, id_pd_;
    std::vector<bool> bajo_;             // quién está tirando de su línea a cero
    double vdd_;
    bool on_ = true;
    sc_core::sc_event ev_;
    sc_core::sc_event_or_list espera_;   // las líneas, más ev_
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
        : sc_core::sc_module(nm), I2cPart(scl, sda, "I2cEeprom", "eeprom", nm, vdd), addr_(dev_addr) {
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


// ===========================================================================
// Tarjeta SD de memoria, a nivel de PIN [IR, §12.17]
//
// Es el equivalente de la EEPROM del bus I2C: una pieza de la placa que habla
// el protocolo de verdad, bit a bit, con su CRC7 en los comandos y su CRC16 por
// cada línea de datos. No entiende de registros del MCU; solo ve CK, CMD y
// D0-D3, y contesta lo que contestaría una tarjeta.
//
// Implementa el arranque completo de una tarjeta SD v2.0:
//   CMD0  GO_IDLE_STATE          (sin respuesta)
//   CMD8  SEND_IF_COND           R7: eco de la tensión y del patrón
//   CMD55 APP_CMD                R1
//   ACMD41 SD_SEND_OP_COND       R3: el OCR, con el bit de "listo"
//   CMD2  ALL_SEND_CID           R2: los 128 bits del CID
//   CMD3  SEND_RELATIVE_ADDR     R6: la dirección que se queda la tarjeta
//   CMD9  SEND_CSD               R2
//   CMD7  SELECT_CARD            R1
//   CMD16 SET_BLOCKLEN           R1
//   ACMD6 SET_BUS_WIDTH          R1: es lo que pone el bus a cuatro hilos
//   CMD17 READ_SINGLE_BLOCK      R1 + un bloque por las líneas de datos
//   CMD24 WRITE_BLOCK            R1 + un bloque que llega por las líneas
//
// Puede además ESTROPEAR a propósito el CRC de la respuesta o el de los datos,
// que es como se comprueba que el host levanta CCRCFAIL y DCRCFAIL de verdad.
// ===========================================================================
SC_MODULE(SdCard), public ExtPartBase {
    SdCard(sc_core::sc_module_name nm, analog_net_if& ck, analog_net_if& cmd,
           analog_net_if* d0, analog_net_if* d1, analog_net_if* d2,
           analog_net_if* d3, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("SdCard", nm),
          ck_(&ck), cmd_(&cmd), vdd_(vdd) {
        dat_[0] = d0; dat_[1] = d1; dat_[2] = d2; dat_[3] = d3;
        add_ref("ck", ck);                    // el reloj lo pone el host
        id_cmd_ = add_pin("cmd", cmd, "sdcard_cmd");
        // El pull-up de la placa: CMD y las líneas de datos reposan en alto,
        // como en cualquier zócalo de tarjeta.
        id_cmd_pu_ = add_drv("cmd", "sd_pu_cmd");
        cmd_->set_drive(id_cmd_pu_, float(vdd_), 47e3f);
        cmd_->set_hiz(id_cmd_);
        char t[8];
        for (unsigned i = 0; i < 4; ++i) {
            if (!dat_[i]) continue;
            std::snprintf(t, sizeof t, "dat%u", i);
            id_dat_[i] = add_pin(t, *dat_[i], "sdcard_dat");
            id_dat_pu_[i] = add_drv(t, "sd_pu_dat");
            dat_[i]->set_drive(id_dat_pu_[i], float(vdd_), 47e3f);
            dat_[i]->set_hiz(id_dat_[i]);
        }
        for (unsigned i = 0; i < sizeof mem_; ++i) mem_[i] = uint8_t(i * 7u + 3u);
        SC_HAS_PROCESS(SdCard);
        SC_METHOD(edge_proc);
        sensitive << ck_->value_changed_event();
        dont_initialize();
    }
    ~SdCard() override {
        cmd_->set_hiz(id_cmd_);
        for (unsigned i = 0; i < 4; ++i) if (dat_[i]) dat_[i]->set_hiz(id_dat_[i]);
    }

    // Sacar la tarjeta del zócalo: se van con ella los pull-ups, que es
    // precisamente lo que distingue un zócalo vacío de uno ocupado.
    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (!on) { st_ = RX_CMD; return; }
        cmd_->set_drive(id_cmd_pu_, float(vdd_), 47e3f);
        for (unsigned i = 0; i < 4; ++i)
            if (dat_[i]) dat_[i]->set_drive(id_dat_pu_[i], float(vdd_), 47e3f);
    }

    // ---- Observación y control desde el banco ----------------------------
    uint8_t  peek(unsigned i) const { return mem_[i % sizeof mem_]; }
    void     poke(unsigned i, uint8_t v) { mem_[i % sizeof mem_] = v; }
    unsigned commands() const { return n_cmd_; }
    unsigned last_cmd() const { return last_cmd_; }
    uint32_t last_arg() const { return last_arg_; }
    unsigned bus_width() const { return width_; }
    bool     selected() const { return selected_; }
    unsigned blocks_read()    const { return n_rd_; }
    unsigned blocks_written() const { return n_wr_; }
    // Averías a propósito, para comprobar que el host las detecta
    void break_resp_crc(bool on) { bad_resp_crc_ = on; }
    void break_data_crc(bool on) { bad_data_crc_ = on; }
    void set_mute(bool on)       { mute_ = on; }     // no contesta: CTIMEOUT
    void set_block_len(unsigned n) { blen_ = n; }

private:
    enum St { RX_CMD, TX_RESP, TX_GAP, TX_DATA, RX_DATA_WAIT, RX_DATA };

    bool ck() const { return ck_->voltage() > 0.5 * vdd_; }
    bool cmd_level() const { return cmd_->voltage() > 0.5 * vdd_; }
    bool dat_level(unsigned i) const {
        return dat_[i] && dat_[i]->voltage() > 0.5 * vdd_;
    }
    void drive_cmd(bool v) { cmd_->set_drive(id_cmd_, v ? float(vdd_) : 0.0f, 30.0f); }
    void release_cmd()     { cmd_->set_hiz(id_cmd_); }
    void drive_dat(unsigned i, bool v) {
        if (dat_[i]) dat_[i]->set_drive(id_dat_[i], v ? float(vdd_) : 0.0f, 30.0f);
    }
    void release_dat() {
        for (unsigned i = 0; i < 4; ++i) if (dat_[i]) dat_[i]->set_hiz(id_dat_[i]);
    }

    void edge_proc() {
        if (!conectada_) return;              // tarjeta fuera del zócalo
        const bool c = ck();
        const bool prev = ck_prev_;
        ck_prev_ = c;
        if (!c && prev) falling();
        else if (c && !prev) rising();
    }

    // El flanco de BAJADA es cuando la tarjeta pone su bit, igual que el host.
    void falling() {
        switch (st_) {
            case TX_RESP:
                if (rbit_ < rlen_) {
                    drive_cmd(((resp_[rbit_ / 8] >> (7u - rbit_ % 8)) & 1u) != 0);
                } else {
                    release_cmd();
                    // Tras la respuesta, la tarjeta se coloca donde toque: a
                    // soltar un bloque (lectura), a esperarlo (escritura) o a
                    // escuchar el comando siguiente.
                    st_ = pending_read_  ? TX_GAP
                        : pending_write_ ? RX_DATA_WAIT : RX_CMD;
                    gap_ = 8;
                    cbit_ = 0;
                }
                return;
            case TX_GAP:
                release_dat();
                if (gap_ > 0) { --gap_; return; }
                st_ = TX_DATA; dbit_ = 0;
                for (auto& x : crc_) x.reset();
                return;
            case TX_DATA: {
                const unsigned nl = width_;
                const unsigned nbits = blen_ * 8u / nl;
                if (dbit_ == 0) {
                    for (unsigned l = 0; l < nl; ++l) drive_dat(l, false);   // arranque
                } else if (dbit_ <= nbits) {
                    const unsigned k = dbit_ - 1u;
                    for (unsigned l = 0; l < nl; ++l) {
                        const bool b = bit_of(&mem_[addr_ % sizeof mem_], k, l, nl);
                        drive_dat(l, b);
                        crc_[l].bit(b);
                    }
                } else if (dbit_ <= nbits + 16u) {
                    const unsigned k = dbit_ - nbits - 1u;
                    for (unsigned l = 0; l < nl; ++l) {
                        uint16_t v = crc_[l].v;
                        if (bad_data_crc_) v = uint16_t(v ^ 0x8000u);
                        drive_dat(l, ((v >> (15u - k)) & 1u) != 0);
                    }
                } else if (dbit_ == nbits + 17u) {
                    for (unsigned l = 0; l < nl; ++l) drive_dat(l, true);    // parada
                } else {
                    release_dat();
                    ++n_rd_;
                    pending_read_ = false;
                    st_ = RX_CMD; cbit_ = 0;
                    return;
                }
                ++dbit_;
                return;
            }
            default:
                release_cmd();
                return;
        }
    }

    // El flanco de SUBIDA es cuando la tarjeta muestrea.
    void rising() {
        switch (st_) {
            case RX_CMD: {
                const bool b = cmd_level();
                if (cbit_ == 0 && b) return;              // esperando el arranque
                if (cbit_ == 0) { for (auto& x : rx_) x = 0; }
                if (b) rx_[cbit_ / 8] = uint8_t(rx_[cbit_ / 8] | (0x80u >> (cbit_ % 8)));
                if (++cbit_ >= 48) { cbit_ = 0; handle_command(); }
                return;
            }
            case TX_RESP:
                ++rbit_;
                return;
            case RX_DATA_WAIT:
                if (!dat_level(0)) { st_ = RX_DATA; dbit_ = 0;
                                     for (auto& x : crc_) x.reset();
                                     for (auto& x : wbuf_) x = 0; }
                return;
            case RX_DATA: {
                const unsigned nl = width_;
                const unsigned nbits = blen_ * 8u / nl;
                if (dbit_ < nbits) {
                    for (unsigned l = 0; l < nl; ++l) {
                        const bool b = dat_level(l);
                        set_bit_of(wbuf_, dbit_, l, nl, b);
                        crc_[l].bit(b);
                    }
                    ++dbit_;
                } else if (dbit_ < nbits + 16u) {
                    ++dbit_;
                } else {
                    for (unsigned i = 0; i < blen_ && i < sizeof wbuf_; ++i)
                        mem_[(addr_ + i) % sizeof mem_] = wbuf_[i];
                    ++n_wr_;
                    pending_write_ = false;
                    st_ = RX_CMD; cbit_ = 0;
                }
                return;
            }
            default: return;
        }
    }

    // --- El juego de comandos ---------------------------------------------
    void handle_command() {
        const unsigned idx = rx_[0] & 0x3Fu;
        const uint32_t arg = (uint32_t(rx_[1]) << 24) | (uint32_t(rx_[2]) << 16) |
                             (uint32_t(rx_[3]) << 8)  |  uint32_t(rx_[4]);
        last_cmd_ = idx; last_arg_ = arg; ++n_cmd_;
        // El CRC7 del comando tiene que cuadrar: si no, la tarjeta calla, y el
        // host acaba marcando CTIMEOUT. Es lo que pasa en la placa.
        if (sd_crc7_ext(rx_, 5) != ((rx_[5] >> 1) & 0x7Fu)) { st_ = RX_CMD; return; }
        if (mute_) { st_ = RX_CMD; return; }

        const bool app = app_cmd_;
        app_cmd_ = false;
        pending_read_ = false; pending_write_ = false;

        if (app && idx == 41u) { resp_r3(ocr_ | 0x80000000u); return; }   // ACMD41
        if (app && idx == 6u)  {                                         // ACMD6
            width_ = ((arg & 3u) == 2u) ? 4u : 1u;
            resp_r1(idx); return;
        }
        switch (idx) {
            case 0:  st_ = RX_CMD; selected_ = false; return;   // CMD0: sin respuesta
            case 8:  resp_r1_arg(idx, arg & 0xFFFu); return;    // CMD8: eco
            case 55: app_cmd_ = true; resp_r1(idx); return;
            case 2:  resp_r2(cid_); return;
            case 3:  resp_r1_arg(idx, (uint32_t(rca_) << 16) | 0x0500u); return;
            case 9:  resp_r2(csd_); return;
            case 7:  selected_ = ((arg >> 16) == rca_); resp_r1(idx); return;
            case 16: blen_ = arg ? arg : 512u; resp_r1(idx); return;
            case 17: addr_ = arg; pending_read_ = true; resp_r1(idx); return;
            case 24: addr_ = arg; pending_write_ = true; resp_r1(idx); return;
            case 12: resp_r1(idx); return;                       // STOP_TRANSMISSION
            default: resp_r1(idx); return;
        }
    }
    void begin_resp(unsigned len) {
        rlen_ = len; rbit_ = 0; st_ = TX_RESP;
    }
    void resp_r1(unsigned idx) { resp_r1_arg(idx, card_status()); }
    void resp_r1_arg(unsigned idx, uint32_t v) {
        resp_[0] = uint8_t(idx & 0x3Fu);
        resp_[1] = uint8_t(v >> 24); resp_[2] = uint8_t(v >> 16);
        resp_[3] = uint8_t(v >> 8);  resp_[4] = uint8_t(v);
        uint8_t crc = sd_crc7_ext(resp_, 5);
        if (bad_resp_crc_) crc = uint8_t(crc ^ 0x55u);
        resp_[5] = uint8_t((crc << 1) | 1u);
        begin_resp(48);
    }
    // R3 (el OCR) viaja con el índice a unos y SIN CRC: la tarjeta manda unos.
    void resp_r3(uint32_t v) {
        resp_[0] = 0x3Fu;
        resp_[1] = uint8_t(v >> 24); resp_[2] = uint8_t(v >> 16);
        resp_[3] = uint8_t(v >> 8);  resp_[4] = uint8_t(v);
        resp_[5] = 0xFFu;
        begin_resp(48);
    }
    // R2: 136 bits con los 128 del registro pedido.
    void resp_r2(const uint8_t* reg16) {
        resp_[0] = 0x3Fu;
        for (unsigned i = 0; i < 16; ++i) resp_[1 + i] = reg16[i];
        resp_[17] = 0xFFu;
        begin_resp(136);
    }
    uint32_t card_status() const {
        // READY_FOR_DATA (bit 8) y estado TRAN (4) o STBY (3) en [12:9]
        return (1u << 8) | ((selected_ ? 4u : 3u) << 9);
    }

    static uint8_t sd_crc7_ext(const uint8_t* d, unsigned n) { return sd_crc7(d, n); }
    static bool bit_of(const uint8_t* b, unsigned c, unsigned line, unsigned nl) {
        const unsigned p = c * nl + (nl - 1u - line);
        return ((b[p / 8] >> (7u - p % 8)) & 1u) != 0;
    }
    static void set_bit_of(uint8_t* b, unsigned c, unsigned line, unsigned nl, bool v) {
        const unsigned p = c * nl + (nl - 1u - line);
        if (v) b[p / 8] = uint8_t(b[p / 8] | (1u << (7u - p % 8)));
    }

    analog_net_if *ck_, *cmd_;
    analog_net_if *dat_[4] = {nullptr, nullptr, nullptr, nullptr};
    double vdd_;
    int id_cmd_ = -1, id_cmd_pu_ = -1;
    int id_dat_[4] = {-1, -1, -1, -1}, id_dat_pu_[4] = {-1, -1, -1, -1};

    St       st_ = RX_CMD;
    bool     ck_prev_ = false;
    unsigned cbit_ = 0, rbit_ = 0, rlen_ = 0, dbit_ = 0, gap_ = 0;
    uint8_t  rx_[6] = {}, resp_[18] = {};
    uint8_t  mem_[2048] = {}, wbuf_[512] = {};
    SdCrc16  crc_[4];
    unsigned width_ = 1, blen_ = 512, n_cmd_ = 0, n_rd_ = 0, n_wr_ = 0;
    unsigned last_cmd_ = 0;
    uint32_t last_arg_ = 0, addr_ = 0;
    uint16_t rca_ = 0x0002u;
    uint32_t ocr_ = 0x00FF8000u;
    bool     app_cmd_ = false, selected_ = false, pending_read_ = false;
    bool     bad_resp_crc_ = false, bad_data_crc_ = false, mute_ = false;
    bool     pending_write_ = false;
    // Identificación de la tarjeta: 128 bits cada uno [IR, §12.17]
    uint8_t  cid_[16] = {0x02,0x54,0x4D,0x53,0x41,0x30,0x34,0x47,
                         0x00,0x11,0x22,0x33,0x44,0x01,0x2A,0x00};
    uint8_t  csd_[16] = {0x40,0x0E,0x00,0x32,0x5B,0x59,0x00,0x00,
                         0x1D,0x8A,0x7F,0x80,0x0A,0x40,0x00,0x00};
};

// ---------------------------------------------------------------------------
// Maestro I2C externo: otro microcontrolador en la misma placa. Sirve para
// probar el MCU como ESCLAVO y para provocar la pérdida de arbitraje.
// ---------------------------------------------------------------------------
SC_MODULE(I2cExtMaster), public I2cPart {
    I2cExtMaster(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
                 double f_scl = 100e3, double vdd = 3.3)
        : sc_core::sc_module(nm), I2cPart(scl, sda, "I2cExtMaster", "extmaster", nm, vdd),
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
            if (!conectada_) { busy_ = false; continue; }   // maestro desoldado
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
        : ExtPart(n, "Driver", "extdrv", "pin"), vdd_(vdd), r_(r_out) { hiz(); }
    void set(bool level) { if (conectada_) drive(level ? float(vdd_) : 0.0f, float(r_)); }
    void set_volts(double v, double r) { if (conectada_) drive(float(v), float(r)); }
    void release()       { hiz(); }
private:
    double vdd_, r_;
};

// ===========================================================================
// EL BUS CAN
//
// Un bus CAN NO es una senal: es un CABLE EN Y. El estado dominante gana al
// recesivo porque un cero de baja impedancia gana a una resistencia de subida,
// y de ahi -no de un `&&` en C++- salen el arbitraje y el asentimiento.
//
// La topologia es la de una placa de verdad:
//
//   pin CAN_TX (push-pull) --> [transceptor] --> nodo del bus (cable en Y)
//   pin CAN_RX (entrada)   <-- [transceptor] <-- nodo del bus
//
// El nodo del bus es un AnalogNet mas, con su terminador haciendo de pull-up.
// Cada transceptor tira de el a cero cuando su entrada TXD esta a cero, y lo
// suelta cuando esta a uno. La resolucion la hace la superposicion de
// conductancias del propio canal.
// ===========================================================================

// El hilo comun: un nodo analogico con su terminador. Recesivo = alto.
class CanWire : public ExtPartBase {
public:
    // El hilo CON SU PROPIO NODO. Es el uso histórico del banco de pruebas:
    // quien crea el bus crea también el punto eléctrico.
    explicit CanWire(double vdd = 3.3, double r_term = 1000.0,
                     const char* nm = "can_bus")
        : ExtPartBase("CanWire", nm), propio_(new AnalogNet(nm)),
          net_(propio_.get()), vdd_(vdd), r_term_(r_term) { pon_terminador(); }
    // El hilo sobre un nodo QUE YA EXISTE. Es lo que hace el netlist, y es la
    // forma correcta: un nodo es del circuito, no del componente que se cuelga
    // de él. Si el hilo se trae su propio nodo, nadie más puede referirse a él
    // por su nombre, que es justo lo que un netlist necesita poder hacer.
    CanWire(analog_net_if& n, double vdd, double r_term, const char* nm)
        : ExtPartBase("CanWire", nm), net_(&n), vdd_(vdd), r_term_(r_term) {
        pon_terminador();
    }
    analog_net_if& net() { return *net_; }
    double vdd() const { return vdd_; }
    bool dominant() const { return net_->voltage() < 0.5 * vdd_; }
    float voltage() const { return net_->voltage(); }
    // Desconectar el terminador deja el hilo flotando: es lo que se ve al
    // desenchufar el cable.
    void set_terminated(bool on) {
        conectada_ = on;
        net_->set_drive(id_term_, float(vdd_), on ? float(r_term_) : R_HIZ);
    }
    void set_enabled(bool on) override { set_terminated(on); }
private:
    void pon_terminador() {
        id_term_ = add_pin("bus", *net_, "terminador");
        net_->set_drive(id_term_, float(vdd_), float(r_term_));
    }
    std::unique_ptr<AnalogNet> propio_;      // solo si el hilo crea su nodo
    analog_net_if* net_ = nullptr;
    double vdd_, r_term_;
    int id_term_ = -1;
};

// El transceptor: convierte los dos pines digitales del MCU en el estado del
// hilo, y al reves. Es el chip que va soldado al lado del microcontrolador.
SC_MODULE(CanTransceiver), public ExtPartBase {
    CanTransceiver(sc_core::sc_module_name nm, analog_net_if& tx_pin,
                   analog_net_if& rx_pin, CanWire& bus, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("CanTransceiver", nm),
          tx_(&tx_pin), rx_(&rx_pin), bus_(&bus), vdd_(vdd) {
        // La entrada TXD del transceptor lleva PULL-UP, como los chips de
        // verdad. No es un adorno: es lo que garantiza que un TXD flotante
        // -el MCU todavia sin configurar, o el pin en reset- deje el hilo
        // RECESIVO en vez de atascar el bus entero en dominante.
        id_txd_ = add_pin("txd", *tx_, "xcvr_txd_pu");
        id_rxd_ = add_pin("rxd", *rx_, "xcvr_rxd");
        id_bus_ = add_pin("bus", bus_->net(), "xcvr_bus");
        SC_HAS_PROCESS(CanTransceiver);
        SC_THREAD(run);
        set_attached(attached_);
    }
    // Un transceptor en reposo (STB) deja de gobernar el hilo, pero sigue
    // escuchando: es lo que hacen los de verdad en bajo consumo. Ojo: esto NO
    // es lo mismo que quitarlo de la placa, y por eso el interruptor de la
    // librería —set_enabled— es el segundo, no el primero.
    void set_standby(bool activo) { on_ = activo; ev_.notify(sc_core::SC_ZERO_TIME); }
    // Soldarlo o quitarlo de la placa. Sin el, sus dos pines quedan libres
    // para lo que quiera hacer el resto del banco de pruebas.
    void set_enabled(bool on) override { set_attached(on); }
    void set_attached(bool on) {
        attached_ = on;
        conectada_ = on;
        if (!on) {
            tx_->set_hiz(id_txd_);
            rx_->set_hiz(id_rxd_);
            bus_->net().set_hiz(id_bus_);
        } else {
            tx_->set_drive(id_txd_, float(vdd_), 10000.0f);   // pull-up de TXD
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
private:
    void run() {
        for (;;) {
            if (!attached_) { wait(ev_); continue; }
            const bool txd = tx_->voltage() > 0.5 * vdd_;   // 1 = recesivo
            if (on_ && !txd) bus_->net().set_drive(id_bus_, 0.0f, 20.0f);
            else             bus_->net().set_hiz(id_bus_);
            // RXD sale push-pull hacia el pin del MCU.
            const bool dom = bus_->dominant();
            rx_->set_drive(id_rxd_, dom ? 0.0f : float(vdd_), 50.0f);
            wait(tx_->value_changed_event() | bus_->net().value_changed_event() | ev_);
        }
    }
    analog_net_if *tx_, *rx_;
    CanWire* bus_;
    double vdd_;
    bool on_ = true, attached_ = false;
    int id_txd_ = -1, id_rxd_ = -1, id_bus_ = -1;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// Un nodo CAN externo: otro controlador colgado del mismo hilo. Habla el
// protocolo de verdad -relleno de bits, CRC15, asentimiento- reutilizando las
// mismas funciones que el periferico del MCU, igual que la tarjeta SD reutiliza
// los CRC del SDIO.
//
// Sirve para tres cosas que sin el no se pueden probar:
//   * ASENTIR los marcos del MCU (sin nadie que asienta, un bus CAN no
//     entrega nada: es el error mas comun al montar el primer nodo);
//   * MANDAR marcos al MCU, para ejercitar los filtros;
//   * COMPETIR en el arbitraje, que es lo que decide quien manda cuando dos
//     nodos empiezan a la vez.
// ---------------------------------------------------------------------------
SC_MODULE(CanNode), public ExtPartBase {
    CanNode(sc_core::sc_module_name nm, CanWire& bus, double bitrate = 500e3,
            double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("CanNode", nm),
          bus_(&bus), tb_(1.0 / bitrate), vdd_(vdd) {
        id_ = add_pin("bus", bus_->net(), "nodo_can");
        bus_->net().set_hiz(id_);
        SC_HAS_PROCESS(CanNode);
        SC_THREAD(run);
    }

    // --- Mandos del banco de pruebas ---------------------------------------
    void set_enabled(bool on) override {
        conectada_ = on; on_ = on; ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void set_ack(bool on)     { ack_ = on; }        // deja de asentir: error de ACK
    void set_bitrate(double b){ tb_ = 1.0 / b; }
    // Vacia la cola y los contadores. Un nodo CAN reintenta indefinidamente un
    // marco que nadie le asiente, asi que sin esto un marco pendiente de una
    // prueba monopoliza el hilo en la siguiente.
    void flush() {
        cola_.clear();
        n_rx_ = n_tx_ = n_alst_ = n_err_ = 0;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // Encola un marco para transmitir en cuanto el hilo quede libre.
    void send(const CanFrame& f) { cola_.push_back(f); ev_.notify(sc_core::SC_ZERO_TIME); }
    bool sending() const { return !cola_.empty() || tx_activo_; }

    // --- Lo que ha visto ----------------------------------------------------
    unsigned received() const { return n_rx_; }
    unsigned sent()     const { return n_tx_; }
    unsigned lost_arb() const { return n_alst_; }
    unsigned errors()   const { return n_err_; }
    const CanFrame& last() const { return ultimo_; }

private:
    void dominante() { bus_->net().set_drive(id_, 0.0f, 20.0f); }
    void recesivo()  { bus_->net().set_hiz(id_); }
    void poner(bool nivel) { if (nivel) recesivo(); else dominante(); }
    bool leer() const { return !bus_->dominant(); }

    void run() {
        recesivo();
        for (;;) {
            if (!on_) { recesivo(); wait(ev_); continue; }
            // Reposo: se espera a tener algo que mandar o a ver un bit de
            // arranque de otro. La sincronizacion es dura, sobre el flanco.
            recesivo();
            if (cola_.empty() && leer()) {
                wait(sc_core::sc_time(tb_, sc_core::SC_SEC),
                     ev_ | bus_->net().value_changed_event());
                continue;
            }
            // Si el hilo se pone dominante Y este nodo tenia algo que mandar,
            // NO se limita a escuchar: SE SUMA A LA PUJA desde ese mismo bit de
            // arranque. Es lo que hace un nodo de verdad cuando dos empiezan a
            // la vez, y es la unica forma de provocar un arbitraje autentico.
            if (!leer() && cola_.empty()) { recibir(); continue; }
            transmitir();
        }
    }

    // --- Transmision, con arbitraje ----------------------------------------
    void transmitir() {
        const CanFrame f = cola_.front();
        std::vector<bool> flags; unsigned nst = 0, arb = 0;
        std::vector<bool> bits = can_wire_bits(f, &flags, &nst, &arb);
        tx_activo_ = true;
        bool ack = false;
        for (size_t i = 0; i < bits.size(); ++i) {
            poner(bits[i]);
            wait(sc_core::sc_time(tb_ * 0.75, sc_core::SC_SEC));
            const bool visto = leer();
            if (i == nst + 1u) {
                ack = !visto;                       // ranura de asentimiento
            } else if (visto != bits[i]) {
                if (i < arb && bits[i] && !flags[i]) {
                    // Ha perdido la puja: suelta el hilo y pasa a escuchar el
                    // marco del que ha ganado, sin perder el suyo.
                    ++n_alst_;
                    recesivo();
                    tx_activo_ = false;
                    seguir_recibiendo(i);
                    return;
                }
                ++n_err_;
                recesivo();
                tx_activo_ = false;
                wait(sc_core::sc_time(tb_ * 11.0, sc_core::SC_SEC));
                return;
            }
            wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
        }
        recesivo();
        tx_activo_ = false;
        if (ack) { ++n_tx_; cola_.pop_front(); }
        else     { ++n_err_; }
    }

    // --- Recepcion, con asentimiento ---------------------------------------
    void recibir() { decodificar(0); }
    void seguir_recibiendo(size_t ya) { decodificar(ya); }

    // `ya` = bits del marco que ya han pasado por el hilo (los que este nodo
    // creia estar transmitiendo antes de perder el arbitraje).
    void decodificar(size_t ya) {
        std::vector<bool> sinrelleno;
        unsigned run = 0; bool last = false; bool first = true;
        // El bit de arranque ya esta en el hilo cuando se entra aqui.
        if (ya == 0) {
            sinrelleno.push_back(false);
            run = 1; last = false; first = false;
            wait(sc_core::sc_time(tb_ * 0.75, sc_core::SC_SEC));
            // ese bit ya se ha muestreado implicitamente al detectar el flanco
            wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
        } else {
            // Se reconstruyen los bits ya vistos: son los que este nodo
            // acababa de emitir, y coincidian con el hilo hasta la puja.
            (void)first;
            sinrelleno.push_back(false);
            run = 1; last = false; first = false;
        }
        bool completo = false;
        unsigned total = 0;
        // Zona con relleno
        for (unsigned k = 0; k < 200 && !completo; ++k) {
            wait(sc_core::sc_time(tb_ * 0.75, sc_core::SC_SEC));
            const bool b = leer();
            wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
            if (run == 5) {
                if (b == last) { ++n_err_; esperar_libre(); return; }
                last = b; run = 1;
                continue;
            }
            sinrelleno.push_back(b);
            if (b == last) ++run; else run = 1;
            last = b;
            total = longitud(sinrelleno);
            if (total && sinrelleno.size() >= total) completo = true;
        }
        if (!completo) { esperar_libre(); return; }
        // Si el ULTIMO bit del CRC completa una racha de cinco, el emisor
        // inserta un bit de relleno DESPUES de el: sigue estando dentro de la
        // zona con relleno. Hay que tragarselo, o el delimitador de CRC se
        // muestrea un bit antes de tiempo y todo el final del marco se
        // desplaza -que es exactamente el fallo que costo encontrar aqui-.
        if (run == 5) {
            wait(sc_core::sc_time(tb_ * 0.75, sc_core::SC_SEC));
            const bool sb = leer();
            wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
            if (sb == last) { ++n_err_; esperar_libre(); return; }
        }

        // CRC
        const size_t n = sinrelleno.size();
        std::vector<bool> cuerpo(sinrelleno.begin(), sinrelleno.end() - 15);
        uint16_t crc = 0;
        for (size_t i = n - 15; i < n; ++i) crc = uint16_t((crc << 1) | (sinrelleno[i] ? 1u : 0u));
        const bool crc_ok = (can_crc15(cuerpo) == crc);

        // Delimitador de CRC
        wait(sc_core::sc_time(tb_ * 0.75, sc_core::SC_SEC));
        const bool delim = leer();
        wait(sc_core::sc_time(tb_ * 0.25, sc_core::SC_SEC));
        // Ranura de asentimiento: si el marco esta bien, este nodo la pone a
        // DOMINANTE. Es su unica intervencion en un marco ajeno, y sin ella el
        // emisor da el marco por no entregado.
        if (crc_ok && delim && ack_) dominante(); else recesivo();
        wait(sc_core::sc_time(tb_, sc_core::SC_SEC));
        recesivo();
        if (!crc_ok) { ++n_err_; esperar_libre(); return; }
        ultimo_ = armar(sinrelleno);
        ++n_rx_;
        esperar_libre();
    }

    // Cuantos bits (sin relleno) tiene el marco entero, incluido el CRC. Cero
    // mientras todavia no se sabe.
    static unsigned longitud(const std::vector<bool>& b) {
        if (b.size() < 14) return 0;
        const bool ide = b[13];
        const unsigned cab = ide ? 39u : 19u;
        if (b.size() < cab) return 0;
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (b[cab - 4 + i] ? 1u : 0u);
        if (dlc > 8) dlc = 8;
        const bool rtr = ide ? b[32] : b[12];
        return cab + (rtr ? 0u : dlc * 8u) + 15u;
    }

    static CanFrame armar(const std::vector<bool>& b) {
        CanFrame f;
        f.ide = b[13];
        unsigned p = 1; uint32_t base = 0;
        for (unsigned i = 0; i < 11; ++i) base = (base << 1) | (b[p++] ? 1u : 0u);
        if (!f.ide) { f.rtr = b[12]; f.id = base; p = 15; }
        else {
            p = 14; uint32_t ext = 0;
            for (unsigned i = 0; i < 18; ++i) ext = (ext << 1) | (b[p++] ? 1u : 0u);
            f.id = (base << 18) | ext;
            f.rtr = b[p]; p += 3;
        }
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (b[p++] ? 1u : 0u);
        f.dlc = uint8_t(dlc > 8 ? 8 : dlc);
        if (!f.rtr)
            for (unsigned k = 0; k < f.dlc; ++k) {
                uint32_t by = 0;
                for (unsigned i = 0; i < 8; ++i) by = (by << 1) | (b[p++] ? 1u : 0u);
                f.data[k] = uint8_t(by);
            }
        return f;
    }

    // Espera al espacio entre tramas antes de volver a pujar por el hilo.
    void esperar_libre() {
        recesivo();
        // Tras la ranura de asentimiento quedan el delimitador de ACK, los siete
        // bits de fin de trama y los tres de intermision: once bits recesivos.
        // Empezar antes seria pisarle al emisor su ultimo bit, y eso el emisor
        // lo ve como un error de bit -que es exactamente lo que pasaba aqui-.
        unsigned rec = 0;
        for (unsigned k = 0; k < 40 && rec < 11; ++k) {
            wait(sc_core::sc_time(tb_, sc_core::SC_SEC));
            if (leer()) ++rec; else rec = 0;
        }
    }

    CanWire* bus_;
    double tb_, vdd_;
    int id_ = -1;
    bool on_ = true, ack_ = true, tx_activo_ = false;
    std::deque<CanFrame> cola_;
    CanFrame ultimo_{};
    unsigned n_rx_ = 0, n_tx_ = 0, n_alst_ = 0, n_err_ = 0;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// Receptor de traza SWO: un analizador colgado de PB3.
//
// En modo NRZ el pin es una linea serie asincrona corriente, asi que esto es
// literalmente un receptor de UART: espera el bit de arranque, muestrea ocho
// bits en el centro y comprueba el de parada. Encima va el desempaquetado del
// protocolo ITM, que es lo que convierte los bytes en mensajes.
// ---------------------------------------------------------------------------
SC_MODULE(SwoReceiver), public ExtPart {
    SwoReceiver(sc_core::sc_module_name nm, analog_net_if& swo, double bitrate,
                double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPart(swo, "SwoReceiver", "swo_rx", "swo", nm), tb_(1.0 / bitrate),
          vdd_(vdd) {
        hiz();                                        // solo escucha
        SC_HAS_PROCESS(SwoReceiver);
        SC_THREAD(run);
    }
    void set_bitrate(double b) { tb_ = 1.0 / b; }
    void clear() { bytes_.clear(); msg_.clear(); puerto_.clear();
                   pend_ = 0; k_ = 0; acc_ = 0; }
    unsigned bytes() const { return unsigned(bytes_.size()); }
    unsigned mensajes() const { return unsigned(msg_.size()); }
    uint32_t mensaje(unsigned i) const { return i < msg_.size() ? msg_[i] : 0; }
    unsigned puerto(unsigned i) const { return i < puerto_.size() ? puerto_[i] : 0; }
    // El texto que ha llegado por un puerto, byte a byte: el printf del ITM.
    std::string texto(unsigned p) const {
        std::string t;
        for (size_t i = 0; i < msg_.size(); ++i)
            if (puerto_[i] == p) t.push_back(char(msg_[i] & 0xFFu));
        return t;
    }
private:
    bool nivel() const { return net_->voltage() > 0.5 * vdd_; }
    void run() {
        for (;;) {
            // Primero, esperar a que la linea este EN REPOSO (alta). Sin esto,
            // al arrancar la simulacion el nodo todavia no lo gobierna nadie y
            // el receptor tomaria el nivel indefinido por un bit de arranque,
            // desincronizando toda la trama.
            while (!nivel()) wait(net_->value_changed_event());
            // Y ahora si, el flanco de bajada del bit de arranque.
            while (nivel()) wait(net_->value_changed_event());
            wait(sc_core::sc_time(tb_ * 1.5, sc_core::SC_SEC));   // al centro del bit 0
            uint8_t b = 0;
            for (unsigned i = 0; i < 8; ++i) {
                if (nivel()) b = uint8_t(b | (1u << i));
                wait(sc_core::sc_time(tb_, sc_core::SC_SEC));
            }
            if (!nivel()) continue;                   // bit de parada malo
            bytes_.push_back(b);
            desempaqueta();
        }
    }
    // El desempaquetado del ITM: cabecera con puerto y tamano, y detras la
    // carga con el byte menos significativo por delante [IR, §13.6.1].
    void desempaqueta() {
        if (pend_ == 0) {
            const uint8_t h = bytes_.back();
            if ((h & 7u) == 0) return;                // paquete de protocolo
            p_ = (h >> 3) & 0x1Fu;
            const unsigned s = h & 3u;
            pend_ = (s == 3) ? 4u : s;
            acc_ = 0; k_ = 0;
            return;
        }
        acc_ |= uint32_t(bytes_.back()) << (8 * k_);
        ++k_; --pend_;
        if (pend_ == 0) { msg_.push_back(acc_); puerto_.push_back(p_); }
    }
    double tb_, vdd_;
    std::vector<uint8_t> bytes_;
    std::vector<uint32_t> msg_;
    std::vector<unsigned> puerto_;
    unsigned pend_ = 0, p_ = 0, k_ = 0;
    uint32_t acc_ = 0;
};

// ===========================================================================
// UN SENSOR DE IMAGEN CMOS, en los pines
//
// Es la otra mitad del DCMI, y sin ella el periferico no se puede probar: el
// DCMI no pide datos, los RECIBE, asi que hace falta alguien que los ponga en
// catorce hilos al ritmo de su propio reloj. Este sensor hace exactamente lo
// que hace un OV7670 o un MT9V034 en una placa:
//
//   * genera PIXCLK a la frecuencia que se le pida y conduce los datos en el
//     flanco CONTRARIO al que muestrea el DCMI, que es como se cumple el
//     tiempo de establecimiento;
//   * marca los bordes con VSYNC y HSYNC, con las polaridades que se le
//     configuren -o, si se le pide, SIN ellas, metiendo los codigos de
//     sincronismo en el propio flujo de datos (BT.656);
//   * emite un patron reproducible, de modo que el banco puede comprobar
//     PIXEL A PIXEL lo que ha llegado a la memoria;
//   * y no se puede parar. Esa es la caracteristica esencial: si el DCMI no
//     vacia su FIFO a tiempo, los datos se pierden. Un modelo de sensor que
//     esperase seria un modelo inutil.
// ===========================================================================
SC_MODULE(CameraSensor), public ExtPartBase {
    // Los tres pines de control mas hasta catorce de datos. Los que no existan
    // en el encapsulado se pasan como nullptr: el sensor no los conduce, y el
    // DCMI lee lo que haya, que es justo lo que pasa en la placa.
    CameraSensor(sc_core::sc_module_name nm, analog_net_if& pixclk,
                 analog_net_if& hsync, analog_net_if& vsync,
                 const std::vector<analog_net_if*>& d, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("CameraSensor", nm),
          pclk_(&pixclk), hs_(&hsync), vs_(&vsync), d_(d), vdd_(vdd) {
        id_pclk_ = add_pin("pixclk", pixclk, "cam_pixclk");
        id_hs_   = add_pin("hsync",  hsync,  "cam_hsync");
        id_vs_   = add_pin("vsync",  vsync,  "cam_vsync");
        char t[8];
        for (size_t i = 0; i < d_.size(); ++i) {
            std::snprintf(t, sizeof t, "d%u", unsigned(i));
            id_d_.push_back(d_[i] ? add_pin(t, *d_[i], "cam_d") : -1);
        }
        SC_HAS_PROCESS(CameraSensor);
        SC_THREAD(run);
        soltar();
    }

    // --- Configuracion del sensor -------------------------------------------
    void set_formato(unsigned ancho, unsigned alto, unsigned bits) {
        ancho_ = ancho; alto_ = alto; bits_ = bits;
    }
    void set_pixclk(double hz)          { f_pix_ = hz; }
    // Polaridades TAL Y COMO LAS VE EL DCMI: nivel activo de cada sincronismo,
    // que es el nivel de BORRADO [IR, §12.22.2].
    void set_polaridad(bool vs_act_alto, bool hs_act_alto, bool datos_en_subida) {
        vs_alto_ = vs_act_alto; hs_alto_ = hs_act_alto; subida_ = datos_en_subida;
    }
    void set_blanking(unsigned h_pix, unsigned v_lin) { hb_ = h_pix; vb_ = v_lin; }
    // Sincronismo embebido: en vez de mover HSYNC y VSYNC, mete los cuatro
    // codigos en el flujo (BT.656). Los pines de sincronismo quedan quietos.
    void set_embebido(bool on, uint8_t fs = 0xFF, uint8_t ls = 0xFE,
                      uint8_t le = 0xFD, uint8_t fe = 0xFC) {
        emb_ = on; c_fs_ = fs; c_ls_ = ls; c_le_ = le; c_fe_ = fe;
    }
    // El patron. Por omision, una rampa que depende de la posicion: cada pixel
    // vale algo distinto y comprobable.
    void set_patron(unsigned p) { patron_ = p; }
    uint32_t pixel_esperado(unsigned x, unsigned y) const {
        const uint32_t m = (1u << bits_) - 1u;
        switch (patron_) {
            case 1:  return (x ^ y) & m;                 // tablero
            case 2:  return (0xA5A5u + 7u * y) & m;      // constante por linea
            default: return (x + 3u * y) & m;            // rampa
        }
    }

    // --- Mando --------------------------------------------------------------
    void emitir(unsigned n_cuadros) { pedidos_ += n_cuadros; ev_.notify(sc_core::SC_ZERO_TIME); }
    void parar() { pedidos_ = 0; }
    unsigned emitidos() const { return n_emitidos_; }
    // Desuelda el sensor: deja los diecisiete pines en alta impedancia. Hace
    // falta porque en el LQFP100 estos pines los comparten otras funciones.
    void soltar() {
        pclk_->set_hiz(id_pclk_); hs_->set_hiz(id_hs_); vs_->set_hiz(id_vs_);
        for (size_t i = 0; i < d_.size(); ++i)
            if (d_[i] && id_d_[i] >= 0) d_[i]->set_hiz(id_d_[i]);
        soldado_ = false;
        conectada_ = false;
    }
    void soldar() {
        soldado_ = true;
        conectada_ = true;
        nivel(*vs_, id_vs_, vs_alto_);      // en reposo: borrado vertical
        nivel(*hs_, id_hs_, hs_alto_);
        nivel(*pclk_, id_pclk_, false);
    }
    // soldar()/soltar() son el interruptor de la librería con otro nombre.
    void set_enabled(bool on) override { if (on) soldar(); else soltar(); }

private:
    void nivel(analog_net_if& n, int id, bool alto) {
        if (id >= 0) n.set_drive(id, alto ? float(vdd_) : 0.0f, 25.0f);
    }
    void datos(uint32_t v) {
        for (size_t i = 0; i < d_.size(); ++i)
            if (d_[i] && id_d_[i] >= 0) nivel(*d_[i], id_d_[i], (v >> i) & 1u);
    }
    // Un ciclo de pixel entero: el dato se pone con el reloj en reposo y se
    // mantiene durante el flanco activo, que es lo que hace un sensor real.
    void ciclo(uint32_t dato) {
        const sc_core::sc_time t(0.5e12 / f_pix_, sc_core::SC_PS);
        datos(dato);
        nivel(*pclk_, id_pclk_, !subida_);      // nivel de reposo
        wait(t);
        nivel(*pclk_, id_pclk_, subida_);       // flanco que muestrea el DCMI
        wait(t);
    }

    void run() {
        for (;;) {
            if (!pedidos_ || !soldado_) { wait(ev_); continue; }
            --pedidos_;
            emitir_cuadro();
            ++n_emitidos_;
        }
    }

    void emitir_cuadro() {
        if (emb_) { emitir_cuadro_embebido(); return; }
        // Sale del borrado vertical: empieza el cuadro.
        nivel(*vs_, id_vs_, !vs_alto_);
        for (unsigned y = 0; y < alto_; ++y) {
            nivel(*hs_, id_hs_, !hs_alto_);            // linea activa
            for (unsigned x = 0; x < ancho_; ++x) ciclo(pixel_esperado(x, y));
            nivel(*hs_, id_hs_, hs_alto_);             // borrado horizontal
            for (unsigned k = 0; k < hb_; ++k) ciclo(0);
        }
        nivel(*vs_, id_vs_, vs_alto_);                 // borrado vertical
        for (unsigned k = 0; k < vb_; ++k) ciclo(0);
    }

    // BT.656: los sincronismos van EN LOS DATOS. Los pines HSYNC y VSYNC no se
    // mueven, y el DCMI se entera de todo por los cuatro codigos.
    void emitir_cuadro_embebido() {
        ciclo(c_fs_);
        for (unsigned y = 0; y < alto_; ++y) {
            ciclo(c_ls_);
            for (unsigned x = 0; x < ancho_; ++x) ciclo(pixel_esperado(x, y));
            ciclo(c_le_);
        }
        ciclo(c_fe_);
    }

    analog_net_if *pclk_, *hs_, *vs_;
    std::vector<analog_net_if*> d_;
    std::vector<int> id_d_;
    int id_pclk_ = -1, id_hs_ = -1, id_vs_ = -1;
    double vdd_, f_pix_ = 6.0e6;
    unsigned ancho_ = 16, alto_ = 8, bits_ = 8;
    unsigned hb_ = 4, vb_ = 2, patron_ = 0;
    bool vs_alto_ = false, hs_alto_ = false, subida_ = true;
    bool emb_ = false, soldado_ = false;
    uint8_t c_fs_ = 0xFF, c_ls_ = 0xFE, c_le_ = 0xFD, c_fe_ = 0xFC;
    unsigned pedidos_ = 0, n_emitidos_ = 0;
    sc_core::sc_event ev_;
};

// ===========================================================================
// UNA SRAM ASINCRONA EN EL BUS EXTERNO
//
// Es la memoria que se suelda al FSMC: dieciseis hilos de datos, los de
// direccion que haya, y cuatro senales de control. No tiene reloj y no negocia
// nada; solo obedece:
//
//   * si NE y NOE estan a cero, conduce el dato de la direccion que le hayan
//     puesto -y lo suelta en cuanto NOE sube-;
//   * si NE esta a cero y NWE sube, guarda lo que hubiera en los datos, byte a
//     byte segun NBL0/NBL1;
//   * y si esta en modo MULTIPLEXADO, coge la parte baja de la direccion de los
//     PROPIOS HILOS DE DATOS en el flanco de subida de NL. Sin eso, en un
//     encapsulado sin A0-A15 no habria forma de decirle que palabra se quiere.
//
// Puede ademas pedir tiempo por NWAIT, que es lo unico del bus externo que no
// decide el controlador.
// ===========================================================================
SC_MODULE(ExtSram), public ExtPartBase {
    ExtSram(sc_core::sc_module_name nm,
            const std::vector<analog_net_if*>& d,     // D0..D15
            const std::vector<analog_net_if*>& a,     // A16..A23 (las que haya)
            analog_net_if& ne, analog_net_if& noe, analog_net_if& nwe,
            analog_net_if& nl, analog_net_if* nbl0, analog_net_if* nbl1,
            analog_net_if* nwait = nullptr, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("ExtSram", nm),
          d_(d), a_(a), ne_(&ne), noe_(&noe), nwe_(&nwe),
          nl_(&nl), nbl0_(nbl0), nbl1_(nbl1), nwait_(nwait), vdd_(vdd) {
        char t[8];
        for (size_t i = 0; i < d_.size(); ++i) {
            std::snprintf(t, sizeof t, "d%u", unsigned(i));
            id_d_.push_back(add_pin(t, *d_[i], "sram_d"));
        }
        // Las direcciones y las señales de control solo se LEEN: las gobierna
        // el FSMC, y la memoria se limita a obedecer.
        for (size_t i = 0; i < a_.size(); ++i) {
            std::snprintf(t, sizeof t, "a%u", unsigned(i + 16));
            add_ref(t, *a_[i]);
        }
        add_ref("ne", ne); add_ref("noe", noe); add_ref("nwe", nwe);
        add_ref("nl", nl); add_ref_opt("nbl0", nbl0); add_ref_opt("nbl1", nbl1);
        if (nwait_) {
            id_wait_ = add_pin("nwait", *nwait_, "sram_wait");
            nwait_->set_hiz(id_wait_);
        }
        mem_.assign(64u * 1024u, 0);
        for (size_t i = 0; i < mem_.size(); ++i) mem_[i] = uint8_t(0xA0u + (i & 0x3Fu));
        SC_HAS_PROCESS(ExtSram);
        SC_METHOD(ctrl_proc);
        sensitive << ne_->value_changed_event() << noe_->value_changed_event()
                  << nwe_->value_changed_event() << nl_->value_changed_event();
        dont_initialize();
        soltar();
    }

    // --- Configuracion de la placa ------------------------------------------
    void set_mux(bool on)        { mux_ = on; }
    void set_ancho(unsigned b)   { ancho_ = b; }
    void set_enabled(bool on) override { puesta_ = on; conectada_ = on; if (!on) soltar(); }
    void set_conectada(bool on)  { set_enabled(on); }      // nombre histórico
    void escribe(uint32_t off, uint8_t v) { if (off < mem_.size()) mem_[off] = v; }
    uint8_t lee(uint32_t off) const { return off < mem_.size() ? mem_[off] : 0u; }
    uint32_t lee16(uint32_t off) const {
        return uint32_t(lee(off)) | (uint32_t(lee(off + 1)) << 8);
    }
    unsigned lecturas() const { return n_rd_; }
    unsigned escrituras() const { return n_wr_; }
    uint32_t ultima_dir() const { return dir_; }

private:
    bool nivel(analog_net_if* n) const { return n && n->voltage() > 0.5 * vdd_; }
    void soltar() { for (size_t i = 0; i < d_.size(); ++i) d_[i]->set_hiz(id_d_[i]); }
    void conduce(uint32_t v) {
        for (size_t i = 0; i < d_.size(); ++i)
            d_[i]->set_drive(id_d_[i], ((v >> i) & 1u) ? float(vdd_) : 0.0f, 30.0f);
    }
    uint32_t datos_leidos() const {
        uint32_t v = 0;
        for (size_t i = 0; i < d_.size(); ++i)
            if (d_[i]->voltage() > 0.5 * vdd_) v |= 1u << i;
        return v;
    }
    uint32_t dir_alta() const {
        uint32_t v = 0;
        for (size_t i = 0; i < a_.size(); ++i)
            if (a_[i]->voltage() > 0.5 * vdd_) v |= 1u << i;
        return v << 16;                       // A16 en adelante
    }

    void ctrl_proc() {
        if (!puesta_) return;
        const bool sel = !nivel(ne_);         // NE activo a cero
        const bool oe  = !nivel(noe_);
        const bool we  = !nivel(nwe_);
        const bool adv = !nivel(nl_);

        // 1. Enganche de la direccion baja: el flanco de SUBIDA de NL.
        if (mux_ && prev_adv_ && !adv && sel) dir_baja_ = datos_leidos() & 0xFFFFu;
        prev_adv_ = adv;

        if (!sel) { soltar(); prev_oe_ = prev_we_ = false; return; }
        dir_ = mux_ ? (dir_alta() | dir_baja_) : dir_alta();

        // 2. Lectura: mientras NOE este abajo, la memoria conduce.
        if (oe && !we) {
            const uint32_t off = dir_ * (ancho_ == 16 ? 2u : 1u);
            conduce(ancho_ == 16 ? lee16(off) : lee(off));
            if (!prev_oe_) ++n_rd_;
        } else if (!oe) {
            soltar();
        }
        // 3. Escritura: se guarda en el flanco de SUBIDA de NWE, que es cuando
        //    el controlador garantiza que los datos son validos.
        if (prev_we_ && !we) {
            const uint32_t v = datos_leidos();
            const uint32_t off = dir_ * (ancho_ == 16 ? 2u : 1u);
            const bool b0 = !nivel(nbl0_), b1 = !nivel(nbl1_);
            if (off < mem_.size() && (b0 || ancho_ == 8)) mem_[off] = uint8_t(v);
            if (ancho_ == 16 && b1 && off + 1 < mem_.size())
                mem_[off + 1] = uint8_t(v >> 8);
            ++n_wr_;
        }
        prev_oe_ = oe; prev_we_ = we;
    }

    std::vector<analog_net_if*> d_, a_;
    std::vector<int> id_d_;
    analog_net_if *ne_, *noe_, *nwe_, *nl_, *nbl0_, *nbl1_, *nwait_;
    int id_wait_ = -1;
    double vdd_;
    std::vector<uint8_t> mem_;
    uint32_t dir_ = 0, dir_baja_ = 0;
    unsigned ancho_ = 16, n_rd_ = 0, n_wr_ = 0;
    bool mux_ = true, puesta_ = false;
    bool prev_oe_ = false, prev_we_ = false, prev_adv_ = false;
};

// ===========================================================================
// UNA NAND FLASH EN EL BUS EXTERNO
//
// Una NAND no tiene bus de direcciones: tiene ocho hilos por los que van
// mandatos, direcciones y datos, y dos senales -CLE y ALE- que dicen cual de
// las tres cosas viaja en cada ciclo. El FSMC saca CLE y ALE por A16 y A17, de
// modo que ESCRIBIR EN UNA DIRECCION U OTRA del banco es lo que elige el tipo
// de ciclo.
//
// Entiende los cuatro mandatos que hacen falta para que la cosa sea util:
// leer identificacion (0x90), leer pagina (0x00 ... 0x30), programar
// (0x80 ... 0x10) y leer estado (0x70).
// ===========================================================================
SC_MODULE(ExtNand), public ExtPartBase {
    ExtNand(sc_core::sc_module_name nm, const std::vector<analog_net_if*>& d,
            analog_net_if& cle, analog_net_if& ale, analog_net_if& nce,
            analog_net_if& noe, analog_net_if& nwe, analog_net_if* rb = nullptr,
            double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("ExtNand", nm),
          d_(d), cle_(&cle), ale_(&ale), nce_(&nce),
          noe_(&noe), nwe_(&nwe), rb_(rb), vdd_(vdd) {
        char t[8];
        for (size_t i = 0; i < d_.size(); ++i) {
            std::snprintf(t, sizeof t, "d%u", unsigned(i));
            id_d_.push_back(add_pin(t, *d_[i], "nand_d"));
        }
        add_ref("cle", cle); add_ref("ale", ale); add_ref("nce", nce);
        add_ref("noe", noe); add_ref("nwe", nwe);
        if (rb_) { id_rb_ = add_pin("rb", *rb_, "nand_rb"); }
        mem_.assign(PAGINAS * PAGINA, 0xFFu);
        SC_HAS_PROCESS(ExtNand);
        SC_METHOD(ctrl_proc);
        sensitive << nce_->value_changed_event() << noe_->value_changed_event()
                  << nwe_->value_changed_event();
        dont_initialize();
        soltar();
    }
    static constexpr unsigned PAGINA = 512, PAGINAS = 16;

    void set_enabled(bool on) override {
        puesta_ = on;
        conectada_ = on;
        if (!on) { soltar(); if (rb_) rb_->set_hiz(id_rb_); }
        else if (rb_) rb_->set_drive(id_rb_, float(vdd_), 1000.0f);  // listo
    }
    void set_conectada(bool on) { set_enabled(on); }       // nombre histórico
    void escribe(uint32_t off, uint8_t v) { if (off < mem_.size()) mem_[off] = v; }
    uint8_t lee(uint32_t off) const { return off < mem_.size() ? mem_[off] : 0xFFu; }
    unsigned mandatos() const { return n_cmd_; }
    unsigned bytes_leidos() const { return n_rd_; }
    unsigned bytes_escritos() const { return n_wr_; }

private:
    bool nivel(analog_net_if* n) const { return n && n->voltage() > 0.5 * vdd_; }
    void soltar() { for (size_t i = 0; i < d_.size(); ++i) d_[i]->set_hiz(id_d_[i]); }
    void conduce(uint8_t v) {
        for (size_t i = 0; i < d_.size(); ++i)
            d_[i]->set_drive(id_d_[i], ((v >> i) & 1u) ? float(vdd_) : 0.0f, 30.0f);
    }
    uint8_t datos_leidos() const {
        uint32_t v = 0;
        for (size_t i = 0; i < d_.size() && i < 8; ++i)
            if (d_[i]->voltage() > 0.5 * vdd_) v |= 1u << i;
        return uint8_t(v);
    }

    void ctrl_proc() {
        if (!puesta_) return;
        const bool sel = !nivel(nce_);
        const bool oe  = !nivel(noe_);
        const bool we  = !nivel(nwe_);
        if (!sel) { soltar(); prev_we_ = prev_oe_ = false; return; }

        // Ciclo de escritura: el dato se toma en el flanco de subida de NWE.
        if (prev_we_ && !we) {
            const uint8_t v = datos_leidos();
            if (nivel(cle_))      manda(v);
            else if (nivel(ale_)) direcciona(v);
            else                  programa(v);
        }
        // Ciclo de lectura: mientras NOE este abajo, la NAND conduce.
        if (oe) {
            conduce(dato_saliente());
            if (!prev_oe_) ++n_rd_;
        } else if (prev_oe_) {
            soltar();
        }
        prev_we_ = we; prev_oe_ = oe;
    }

    void manda(uint8_t c) {
        ++n_cmd_;
        cmd_ = c; n_dir_ = 0; dir_ = 0;
        if (c == 0x90u) { modo_ = ID;      idx_ = 0; }
        if (c == 0x70u) { modo_ = ESTADO;  }
        if (c == 0x00u) { modo_ = LEER;    }
        if (c == 0x30u) { modo_ = LEER;    idx_ = dir_; }
        if (c == 0x80u) { modo_ = ESCRIBIR; }
        if (c == 0x10u) { modo_ = ESTADO;  }     // fin de programacion
        if (c == 0xFFu) { modo_ = ESTADO; dir_ = 0; }
    }
    void direcciona(uint8_t v) {
        dir_ |= uint32_t(v) << (8u * n_dir_);
        ++n_dir_;
        idx_ = dir_ & (PAGINAS * PAGINA - 1u);
    }
    void programa(uint8_t v) {
        if (modo_ != ESCRIBIR) return;
        if (idx_ < mem_.size()) mem_[idx_] &= v;   // programar solo baja bits
        ++idx_; ++n_wr_;
    }
    uint8_t dato_saliente() {
        switch (modo_) {
            case ID: {
                static const uint8_t id[4] = {0x20, 0x33, 0x00, 0x00};
                return id[(idx_++) & 3u];
            }
            case ESTADO: return 0xC0u;                 // listo y sin error
            case LEER:   return (idx_ < mem_.size()) ? mem_[idx_++] : 0xFFu;
            default:     return 0xFFu;
        }
    }

    enum Modo { NADA, ID, ESTADO, LEER, ESCRIBIR };
    std::vector<analog_net_if*> d_;
    std::vector<int> id_d_;
    analog_net_if *cle_, *ale_, *nce_, *noe_, *nwe_, *rb_;
    int id_rb_ = -1;
    double vdd_;
    std::vector<uint8_t> mem_;
    Modo modo_ = NADA;
    uint8_t cmd_ = 0;
    uint32_t dir_ = 0, idx_ = 0;
    unsigned n_dir_ = 0, n_cmd_ = 0, n_rd_ = 0, n_wr_ = 0;
    bool puesta_ = false, prev_we_ = false, prev_oe_ = false;
};

// ===========================================================================
// EL OTRO EXTREMO DEL CABLE USB
//
// Dos aparejos simetricos, porque el OTG es de doble rol y hay que probar los
// dos lados:
//
//   * UsbHostRig    - un PC: da los 5 V de VBUS, pone los dos 15 kohm a masa,
//                     hace el reset de bus con un SE0 largo y manda testigos.
//   * UsbDeviceRig  - un pendrive: pone su 1,5 kohm en D+ cuando lo enchufan,
//                     contesta a los testigos y se entera del reset por el
//                     cable, no porque nadie se lo diga.
//
// Todo lo ELECTRICO va por los nodos analogicos con tensiones de verdad: la
// conexion, la velocidad y el reset salen del divisor resistivo, no de una
// variable booleana. Los paquetes cruzan como paquetes (vease la frontera del
// modelo en periph/otg.h).
// ===========================================================================
SC_MODULE(UsbHostRig), public ExtPartBase {
    UsbHostRig(sc_core::sc_module_name nm, analog_net_if& dm, analog_net_if& dp,
               analog_net_if& vbus, analog_net_if& id, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("UsbHostRig", nm),
          dm_(&dm), dp_(&dp), vbus_(&vbus), id_(&id), vdd_(vdd) {
        id_dm_ = add_pin("dm", dm, "host_dm");
        id_dp_ = add_pin("dp", dp, "host_dp");
        id_pd_dm_ = add_drv("dm", "host_pd_dm");     // los dos 15 kohm a masa
        id_pd_dp_ = add_drv("dp", "host_pd_dp");
        id_vb_ = add_pin("vbus", vbus, "host_vbus");
        id_id_ = add_pin("id", id, "host_id");
        conectada_ = false;                          // nace desenchufado
        soltar();
    }

    // --- La placa ------------------------------------------------------------
    void set_enabled(bool on) override { conectar(on); }
    void conectar(bool on) {
        puesto_ = on;
        conectada_ = on;
        // Los dos 15 kohm a masa son lo que convierte a este extremo en
        // anfitrion: sin ellos, el cable no tiene referencia.
        dm_->set_drive(id_pd_dm_, 0.0f, on ? 15.0e3f : R_HIZ);
        dp_->set_drive(id_pd_dp_, 0.0f, on ? 15.0e3f : R_HIZ);
        if (!on) soltar();
    }
    void set_vbus(bool on) {
        vbus_->set_drive(id_vb_, on ? 5.0f : 0.0f, on ? 0.5f : 1.0e6f);
    }
    // ID a masa = cable A = el que lo tiene enchufado es el anfitrion.
    void set_id_a(bool a) {
        if (a) id_->set_drive(id_id_, 0.0f, 10.0f);
        else   id_->set_hiz(id_id_);
    }
    void conectar_dispositivo(usb_dev_if* d) { dev_ = d; }

    // --- El cable ------------------------------------------------------------
    void reposo() {                                    // estado J de Full Speed
        dp_->set_drive(id_dp_, float(vdd_), 45.0f);
        dm_->set_drive(id_dm_, 0.0f, 45.0f);
    }
    void soltar() { dp_->set_hiz(id_dp_); dm_->set_hiz(id_dm_); }
    // El reset de bus: los dos hilos a cero durante 10 ms. No hay registro que
    // lo mande; es esto.
    void reset_bus(double ms = 10.0) {
        dp_->set_drive(id_dp_, 0.0f, 45.0f);
        dm_->set_drive(id_dm_, 0.0f, 45.0f);
        sc_core::wait(ms, sc_core::SC_MS);
        soltar();
        sc_core::wait(10, sc_core::SC_US);
    }
    // Reanudacion: una K de 20 ms para despertar a un dispositivo suspendido.
    void resume(double ms = 20.0) {
        dp_->set_drive(id_dp_, 0.0f, 45.0f);
        dm_->set_drive(id_dm_, float(vdd_), 45.0f);
        sc_core::wait(ms, sc_core::SC_MS);
        soltar();
        sc_core::wait(10, sc_core::SC_US);
    }

    // --- Los testigos --------------------------------------------------------
    uint8_t setup(uint8_t addr, const std::vector<uint8_t>& d) {
        std::vector<uint8_t> e;
        return llamar(PID_SETUP, addr, 0, d, e);
    }
    uint8_t in(uint8_t addr, uint8_t ep, std::vector<uint8_t>& d) {
        static const std::vector<uint8_t> vacio;
        d.clear();
        return llamar(PID_IN, addr, ep, vacio, d);
    }
    uint8_t out(uint8_t addr, uint8_t ep, const std::vector<uint8_t>& d) {
        std::vector<uint8_t> e;
        return llamar(PID_OUT, addr, ep, d, e);
    }
    // El latido de 1 ms. Sin el, todo dispositivo se suspende.
    void sofs(unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            if (dev_) dev_->sof(uint16_t(++trama_ & 0x3FFFu));
            sc_core::wait(1, sc_core::SC_MS);
        }
    }
    unsigned acks() const { return n_ack_; }
    unsigned naks() const { return n_nak_; }
    unsigned stalls() const { return n_stall_; }

private:
    uint8_t llamar(uint8_t pid, uint8_t addr, uint8_t ep,
                   const std::vector<uint8_t>& s, std::vector<uint8_t>& e) {
        if (!dev_) return PID_NADIE;
        const uint8_t r = dev_->transaccion(pid, addr, ep, s, e);
        if (r == PID_ACK) ++n_ack_;
        else if (r == PID_NAK) ++n_nak_;
        else if (r == PID_STALL) ++n_stall_;
        sc_core::wait(1, sc_core::SC_US);
        return r;
    }
    analog_net_if *dm_, *dp_, *vbus_, *id_;
    int id_dm_ = -1, id_dp_ = -1, id_pd_dm_ = -1, id_pd_dp_ = -1;
    int id_vb_ = -1, id_id_ = -1;
    double vdd_;
    bool puesto_ = false;
    usb_dev_if* dev_ = nullptr;
    unsigned trama_ = 0, n_ack_ = 0, n_nak_ = 0, n_stall_ = 0;
};

// ---------------------------------------------------------------------------
// Un dispositivo USB de verdad al otro lado: contesta a los testigos y se
// entera del reset porque VE el SE0, no porque nadie se lo cuente.
// ---------------------------------------------------------------------------
SC_MODULE(UsbDeviceRig), public ExtPartBase, public usb_dev_if {
    UsbDeviceRig(sc_core::sc_module_name nm, analog_net_if& dm, analog_net_if& dp,
                 analog_net_if& vbus, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("UsbDeviceRig", nm),
          dm_(&dm), dp_(&dp), vbus_(&vbus), vdd_(vdd) {
        id_pu_ = add_pin("dp", dp, "dev_pullup");
        id_pu_lo_ = add_pin("dm", dm, "dev_pullup_ls");
        // El interruptor de 5 V de la placa. No lo da el MCU -PB13 es una
        // ENTRADA de sensado-, lo da un conmutador externo que el firmware
        // gobierna por un GPIO cualquiera; aqui lo maneja la prueba.
        id_vb_ = add_pin("vbus", vbus, "placa_vbus");
        conectada_ = false;                          // nace desenchufado
        dp_->set_hiz(id_pu_); dm_->set_hiz(id_pu_lo_);
        SC_HAS_PROCESS(UsbDeviceRig);
        SC_METHOD(linea_proc);
        sensitive << dp_->value_changed_event() << dm_->value_changed_event()
                  << vbus_->value_changed_event();
        dont_initialize();
        // Un descriptor de dispositivo de los de verdad: 18 bytes.
        desc_ = {0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
                 0x83, 0x04, 0x40, 0x57, 0x00, 0x02, 0x01, 0x02,
                 0x03, 0x01};
    }

    // Enchufar el cable: aparece el 1,5 kohm de D+ y el anfitrion lo ve. Es el
    // interruptor de la librería: un dispositivo desenchufado no existe.
    void set_enabled(bool on) override { enchufar(on); }
    void enchufar(bool on) {
        puesto_ = on;
        conectada_ = on;
        actualiza();
    }
    void alimentacion_placa(bool on) {
        vbus_->set_drive(id_vb_, on ? 5.0f : 0.0f, on ? 0.5f : 1.0e6f);
        actualiza();
    }
    void set_baja_velocidad(bool ls) { ls_ = ls; actualiza(); }
    uint8_t direccion() const { return dir_; }
    unsigned resets() const { return n_rst_; }
    const std::vector<uint8_t>& recibido() const { return rx_; }
    void set_stall(bool s) { stall_ = s; }

    uint8_t transaccion(uint8_t pid, uint8_t addr, uint8_t ep,
                        const std::vector<uint8_t>& salida,
                        std::vector<uint8_t>& entrada) override {
        if (!puesto_ || vbus_->voltage() < 3.0f) return PID_NADIE;
        if (addr != dir_) return PID_NADIE;
        if (stall_ && ep != 0) return PID_STALL;
        if (pid == PID_SETUP) {
            if (salida.size() < 8) return PID_NADIE;
            const uint8_t breq = salida[1];
            const uint16_t wval = uint16_t(salida[2] | (salida[3] << 8));
            const uint16_t wlen = uint16_t(salida[6] | (salida[7] << 8));
            tx_.clear(); tx_i_ = 0;
            if (breq == 0x06 && (wval >> 8) == 0x01) {          // GET_DESCRIPTOR
                tx_ = desc_;
                if (wlen < tx_.size()) tx_.resize(wlen);
            } else if (breq == 0x05) {                          // SET_ADDRESS
                dir_pend_ = uint8_t(wval & 0x7Fu);
            }
            ++n_setup_;
            return PID_ACK;
        }
        if (pid == PID_IN) {
            const unsigned n = tx_.size() - tx_i_ < 8 ? unsigned(tx_.size() - tx_i_) : 8u;
            entrada.assign(tx_.begin() + long(tx_i_), tx_.begin() + long(tx_i_ + n));
            tx_i_ += n;
            // Una IN de longitud cero es la fase de estado de un control OUT:
            // es AHI donde se aplica de verdad la direccion nueva.
            if (n == 0 && dir_pend_ != 0xFF) { dir_ = dir_pend_; dir_pend_ = 0xFF; }
            return PID_ACK;
        }
        if (pid == PID_OUT) {
            for (uint8_t b : salida) rx_.push_back(b);
            if (salida.empty() && dir_pend_ != 0xFF) { dir_ = dir_pend_; dir_pend_ = 0xFF; }
            return PID_ACK;
        }
        return PID_NADIE;
    }
    void sof(uint16_t t) override { trama_ = t; ++n_sof_; }
    unsigned tramas() const { return n_sof_; }
    unsigned setups() const { return n_setup_; }

private:
    void actualiza() {
        const bool alim = vbus_->voltage() > 3.0f;
        const bool on = puesto_ && alim;
        // EL 1,5 KOHM ES LA DECLARACION DE EXISTENCIA. Y en cual de los dos
        // hilos se pone es lo que dice la velocidad: D+ para Full Speed, D-
        // para Low Speed. No hay ningun otro sitio donde eso se diga.
        dp_->set_drive(id_pu_, (on && !ls_) ? float(vdd_) : 0.0f,
                       (on && !ls_) ? 1.5e3f : R_HIZ);
        dm_->set_drive(id_pu_lo_, (on && ls_) ? float(vdd_) : 0.0f,
                       (on && ls_) ? 1.5e3f : R_HIZ);
    }
    void linea_proc() {
        actualiza();
        const bool se0 = dp_->voltage() < 1.6f && dm_->voltage() < 1.6f;
        if (se0 && !se0_) t_se0_ = sc_core::sc_time_stamp();
        if (!se0 && se0_ && puesto_ &&
            sc_core::sc_time_stamp() - t_se0_ >
                sc_core::sc_time(2500, sc_core::SC_NS)) {
            dir_ = 0; dir_pend_ = 0xFF; tx_.clear(); tx_i_ = 0; ++n_rst_;
        }
        se0_ = se0;
    }
    analog_net_if *dm_, *dp_, *vbus_;
    double vdd_;
    int id_pu_ = -1, id_pu_lo_ = -1, id_vb_ = -1;
    bool puesto_ = false, ls_ = false, se0_ = false, stall_ = false;
    uint8_t dir_ = 0, dir_pend_ = 0xFF;
    std::vector<uint8_t> desc_, tx_, rx_;
    size_t tx_i_ = 0;
    unsigned n_rst_ = 0, n_sof_ = 0, n_setup_ = 0;
    uint16_t trama_ = 0;
    sc_core::sc_time t_se0_{sc_core::SC_ZERO_TIME};
};

// ===========================================================================
// EL PHY DE ETHERNET
//
// Es el integrado que hay entre el MAC y el conector RJ45, y hace tres cosas
// que el MAC no puede hacer solo:
//
//   * PONE LOS RELOJES. En MII saca TX_CLK y RX_CLK (25 MHz a 100 Mbit/s, o
//     2,5 a 10); en RMII, un unico REF_CLK de 50 MHz que sirve para los dos
//     sentidos. El MAC no genera nada: los sigue.
//   * habla por MDIO, que es un bus serie de dos hilos con su propia trama de
//     32 bits, y en el viven los registros del PHY (BMCR, BMSR, identificacion
//     y autonegociacion);
//   * y convierte nibbles o dibits en pulsos por el par trenzado, que es lo
//     unico que este modelo NO simula: aqui el "cable" es un buzon de tramas.
//
// Se conecta a los nodos analogicos de los pines, como todo lo que se suelda a
// la placa en este proyecto.
// ===========================================================================
SC_MODULE(EthPhy), public ExtPartBase {
    // Los pines, en el orden en que salen del encapsulado.
    EthPhy(sc_core::sc_module_name nm,
           analog_net_if& mdc, analog_net_if& mdio,
           analog_net_if& tx_clk, analog_net_if& rx_clk,
           analog_net_if& tx_en, const std::vector<analog_net_if*>& txd,
           const std::vector<analog_net_if*>& rxd,
           analog_net_if& rx_dv, analog_net_if& rx_er,
           analog_net_if& crs, analog_net_if& col, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("EthPhy", nm),
          mdc_(&mdc), mdio_(&mdio), txclk_(&tx_clk),
          rxclk_(&rx_clk), txen_(&tx_en), txd_(txd), rxd_(rxd), rxdv_(&rx_dv),
          rxer_(&rx_er), crs_(&crs), col_(&col), vdd_(vdd) {
        add_ref("mdc", mdc);                  // el reloj del MDIO lo pone el MAC
        id_mdio_ = add_pin("mdio", mdio, "phy_mdio");
        // La RESISTENCIA DE PULL-UP del MDIO, que en la placa son 1,5 a 10 kohm
        // y sin la cual el bus no tiene estado de reposo. Es lo que hace que
        // preguntarle a una direccion donde no hay nadie devuelva TODO UNOS en
        // vez de un valor cualquiera.
        id_mdio_pu_ = add_drv("mdio", "mdio_pullup");
        id_txclk_ = add_pin("tx_clk", tx_clk, "phy_txclk");
        id_rxclk_ = add_pin("rx_clk", rx_clk, "phy_rxclk");
        id_rxdv_ = add_pin("rx_dv", rx_dv, "phy_rxdv");
        id_rxer_ = add_pin("rx_er", rx_er, "phy_rxer");
        id_crs_ = add_pin("crs", crs, "phy_crs");
        id_col_ = add_pin("col", col, "phy_col");
        char t[8];
        for (size_t i = 0; i < rxd_.size(); ++i) {
            std::snprintf(t, sizeof t, "rxd%u", unsigned(i));
            id_rxd_.push_back(add_pin(t, *rxd_[i], "phy_rxd"));
        }
        // Lo que sale del MAC solo se escucha: TX_EN y los TXD los gobierna el.
        add_ref("tx_en", tx_en);
        for (size_t i = 0; i < txd_.size(); ++i) {
            std::snprintf(t, sizeof t, "txd%u", unsigned(i));
            add_ref(t, *txd_[i]);
        }
        conectada_ = false;                   // nace sin soldar
        // Registros del PHY: los cuatro primeros son los de la norma.
        reg_.assign(32, 0);
        reg_[0] = 0x3100;                 // BMCR: 100 Mbit/s, full duplex, ANEG
        reg_[1] = 0x786D;                 // BMSR: enlace arriba, ANEG completa
        reg_[2] = 0x0007;                 // PHYID1  (identificacion de fabrica)
        reg_[3] = 0xC0F1;                 // PHYID2
        reg_[4] = 0x01E1;                 // ANAR
        reg_[5] = 0x0000;                 // ANLPAR
        SC_HAS_PROCESS(EthPhy);
        SC_THREAD(reloj_proc);
        SC_METHOD(mdio_proc);
        sensitive << mdc_->value_changed_event();
        dont_initialize();
        SC_METHOD(tx_proc);
        sensitive << txclk_->value_changed_event() << rxclk_->value_changed_event();
        dont_initialize();
        soltar();
    }

    // --- La placa ------------------------------------------------------------
    void set_enabled(bool on) override { conectar(on); }
    void conectar(bool on) {
        puesto_ = on;
        conectada_ = on;
        mdio_->set_drive(id_mdio_pu_, on ? float(vdd_) : 0.0f,
                         on ? 10.0e3f : R_HIZ);
        if (!on) soltar();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void set_rmii(bool r)   { rmii_ = r; }
    void set_cien(bool c)   { cien_ = c; }
    void set_enlace(bool e) { reg_[1] = uint16_t(e ? 0x786D : 0x7809); }
    void set_dir(unsigned a) { dir_ = a & 0x1Fu; }

    // --- El cable ------------------------------------------------------------
    // Lo que el MAC ha sacado por los pines (con FCS, sin preambulo).
    const std::deque<std::vector<uint8_t>>& recibidas() const { return rx_; }
    unsigned n_recibidas() const { return n_rx_; }
    void limpiar() { rx_.clear(); n_rx_ = 0; }
    // Y una trama que llega del segmento hacia el MAC. Se le anade el
    // preambulo, el delimitador y la FCS, como haria el PHY de verdad.
    void enviar(const std::vector<uint8_t>& t, bool fcs_mala = false) {
        pend_.push_back(std::make_pair(t, fcs_mala));
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    unsigned mdio_lecturas() const { return n_mdio_rd_; }
    unsigned mdio_escrituras() const { return n_mdio_wr_; }
    uint16_t reg(unsigned r) const { return r < reg_.size() ? reg_[r] : 0; }
    void set_reg(unsigned r, uint16_t v) { if (r < reg_.size()) reg_[r] = v; }

private:
    bool nivel(analog_net_if* n) const { return n->voltage() > 0.5 * vdd_; }
    void pon(analog_net_if* n, int id, bool v) {
        n->set_drive(id, v ? float(vdd_) : 0.0f, 30.0f);
    }
    void soltar() {
        mdio_->set_hiz(id_mdio_);
        txclk_->set_hiz(id_txclk_); rxclk_->set_hiz(id_rxclk_);
        rxdv_->set_hiz(id_rxdv_); rxer_->set_hiz(id_rxer_);
        crs_->set_hiz(id_crs_); col_->set_hiz(id_col_);
        for (size_t i = 0; i < rxd_.size(); ++i) rxd_[i]->set_hiz(id_rxd_[i]);
    }

    // --- Los relojes y la inyeccion de tramas --------------------------------
    // Un solo hilo lleva el reloj Y la trama de recepcion, porque la trama va
    // sincronizada con el: son la misma cosa vista desde dos sitios.
    void reloj_proc() {
        for (;;) {
            if (!puesto_) { sc_core::wait(ev_); continue; }
            // 25 MHz de nibble en MII a 100 Mbit/s; 50 MHz de dibit en RMII.
            const double ns = rmii_ ? (cien_ ? 10.0 : 100.0)
                                    : (cien_ ? 20.0 : 200.0);
            const sc_core::sc_time semi(ns, sc_core::SC_NS);
            // Flanco de bajada: es donde el MAC muestrea, asi que los datos se
            // ponen aqui y llegan estables al flanco de subida.
            pon(txclk_, id_txclk_, false);
            pon(rxclk_, id_rxclk_, false);
            paso_rx();
            sc_core::wait(semi);
            pon(txclk_, id_txclk_, true);
            pon(rxclk_, id_rxclk_, true);
            sc_core::wait(semi);
        }
    }

    // Un ciclo de la maquina de inyeccion: saca el nibble/dibit que toque.
    void paso_rx() {
        const unsigned n = rmii_ ? 2u : 4u;
        if (hilo_.empty()) {
            if (pend_.empty()) {
                pon(rxdv_, id_rxdv_, false);
                pon(rxer_, id_rxer_, false);
                pon(crs_, id_crs_, false);
                for (size_t i = 0; i < rxd_.size(); ++i) pon(rxd_[i], id_rxd_[i], false);
                return;
            }
            const std::vector<uint8_t>& t = pend_.front().first;
            const bool mala = pend_.front().second;
            for (unsigned i = 0; i < 7; ++i) hilo_.push_back(0x55);
            hilo_.push_back(0xD5);
            for (uint8_t b : t) hilo_.push_back(b);
            uint32_t fcs = eth_fcs(t.data(), t.size());
            if (mala) fcs ^= 0xA5A5A5A5u;      // una trama con el CRC roto
            for (unsigned i = 0; i < 4; ++i) hilo_.push_back(uint8_t(fcs >> (8 * i)));
            pend_.pop_front();
            idx_ = 0;
        }
        pon(rxdv_, id_rxdv_, true);
        pon(crs_, id_crs_, true);
        const uint8_t b = hilo_[idx_ / (8u / n)];
        const unsigned s = idx_ % (8u / n);
        for (size_t i = 0; i < rxd_.size(); ++i)
            pon(rxd_[i], id_rxd_[i], i < n && (((b >> (n * s)) >> i) & 1u));
        if (++idx_ >= hilo_.size() * (8u / n)) { hilo_.clear(); idx_ = 0; }
    }

    // --- Captura de lo que transmite el MAC ----------------------------------
    void tx_proc() {
        if (!puesto_) return;
        // Se muestrea en el flanco de SUBIDA, medio ciclo despues de que el MAC
        // haya puesto el dato: es el margen de establecimiento de siempre.
        analog_net_if* clk = rmii_ ? rxclk_ : txclk_;
        const bool alto = nivel(clk);
        if (!alto || alto == prev_clk_) { prev_clk_ = alto; return; }
        prev_clk_ = alto;
        const unsigned n = rmii_ ? 2u : 4u;
        if (!nivel(txen_)) {
            if (!tx_bytes_.empty()) {
                // Se quita el preambulo y el delimitador; lo demas es la trama.
                size_t i = 0;
                while (i < tx_bytes_.size() && tx_bytes_[i] == 0x55) ++i;
                if (i < tx_bytes_.size() && tx_bytes_[i] == 0xD5) ++i;
                if (i < tx_bytes_.size()) {
                    rx_.push_back(std::vector<uint8_t>(tx_bytes_.begin() + long(i),
                                                       tx_bytes_.end()));
                    ++n_rx_;
                }
                tx_bytes_.clear();
                tx_acc_ = 0; tx_n_ = 0;
            }
            return;
        }
        uint8_t v = 0;
        for (size_t i = 0; i < txd_.size() && i < n; ++i)
            if (nivel(txd_[i])) v |= uint8_t(1u << i);
        tx_acc_ |= uint8_t(v << (n * tx_n_));
        if (++tx_n_ >= 8u / n) { tx_bytes_.push_back(tx_acc_); tx_acc_ = 0; tx_n_ = 0; }
    }

    // --- MDIO: la trama de 32 bits, bit a bit --------------------------------
    void mdio_proc() {
        if (!puesto_) return;
        const bool c = nivel(mdc_);
        if (c == prev_mdc_) return;
        prev_mdc_ = c;
        if (!c) {                                 // flanco de bajada
            // Si toca conducir, se pone el bit aqui para que el MAC lo lea en
            // el flanco de subida.
            if (fase_ == LEER_DATO && bit_ < 16) {
                pon(mdio_, id_mdio_, ((dato_ >> (15 - bit_)) & 1u) != 0);
            }
            return;
        }
        // Flanco de subida: se muestrea lo que conduce el MAC.
        const bool b = nivel(mdio_);
        switch (fase_) {
            case PREAMBULO:
                if (b) { if (++unos_ >= 32) { fase_ = ST; bit_ = 0; sr_ = 0; } }
                else if (unos_ >= 2) { fase_ = ST; bit_ = 1; sr_ = 0; unos_ = 0; }
                else unos_ = 0;
                return;
            case ST:
                sr_ = uint32_t((sr_ << 1) | (b ? 1u : 0u));
                if (++bit_ >= 2) { fase_ = OP; bit_ = 0; st_ = sr_ & 3u; sr_ = 0; }
                return;
            case OP:
                sr_ = uint32_t((sr_ << 1) | (b ? 1u : 0u));
                if (++bit_ >= 2) { op_ = sr_ & 3u; fase_ = PA; bit_ = 0; sr_ = 0; }
                return;
            case PA:
                sr_ = uint32_t((sr_ << 1) | (b ? 1u : 0u));
                if (++bit_ >= 5) { pa_ = sr_ & 0x1Fu; fase_ = RA; bit_ = 0; sr_ = 0; }
                return;
            case RA:
                sr_ = uint32_t((sr_ << 1) | (b ? 1u : 0u));
                if (++bit_ >= 5) {
                    ra_ = sr_ & 0x1Fu; bit_ = 0; sr_ = 0;
                    if (op_ == 2u && pa_ == dir_) {      // lectura
                        dato_ = reg_[ra_];
                        fase_ = TA_LEER;
                    } else {
                        fase_ = (op_ == 1u) ? TA_ESCR : IGNORA;
                    }
                }
                return;
            case TA_LEER:
                if (++bit_ >= 2) { fase_ = LEER_DATO; bit_ = 0; }
                return;
            case TA_ESCR:
                if (++bit_ >= 2) { fase_ = ESCR_DATO; bit_ = 0; sr_ = 0; }
                return;
            case LEER_DATO:
                if (++bit_ >= 16) {
                    mdio_->set_hiz(id_mdio_);
                    ++n_mdio_rd_;
                    fase_ = PREAMBULO; unos_ = 0; bit_ = 0;
                }
                return;
            case ESCR_DATO:
                sr_ = uint32_t((sr_ << 1) | (b ? 1u : 0u));
                if (++bit_ >= 16) {
                    if (pa_ == dir_) {
                        reg_[ra_] = uint16_t(sr_);
                        // BMCR.RESET (bit 15) se autoborra, como en el silicio.
                        if (ra_ == 0 && (reg_[0] & 0x8000u)) reg_[0] &= 0x7FFFu;
                        ++n_mdio_wr_;
                    }
                    fase_ = PREAMBULO; unos_ = 0; bit_ = 0;
                }
                return;
            default:
                if (++bit_ >= 18) { fase_ = PREAMBULO; unos_ = 0; bit_ = 0; }
                return;
        }
    }

    enum Fase { PREAMBULO, ST, OP, PA, RA, TA_LEER, TA_ESCR, LEER_DATO,
                ESCR_DATO, IGNORA };

    analog_net_if *mdc_, *mdio_, *txclk_, *rxclk_, *txen_;
    std::vector<analog_net_if*> txd_, rxd_;
    analog_net_if *rxdv_, *rxer_, *crs_, *col_;
    std::vector<int> id_rxd_;
    int id_mdio_ = -1, id_mdio_pu_ = -1, id_txclk_ = -1, id_rxclk_ = -1, id_rxdv_ = -1;
    int id_rxer_ = -1, id_crs_ = -1, id_col_ = -1;
    double vdd_;
    bool puesto_ = false, rmii_ = true, cien_ = true;
    unsigned dir_ = 0;
    std::vector<uint16_t> reg_;
    // Transmision del MAC hacia aqui
    std::vector<uint8_t> tx_bytes_;
    uint8_t tx_acc_ = 0; unsigned tx_n_ = 0;
    bool prev_clk_ = false;
    std::deque<std::vector<uint8_t>> rx_;
    unsigned n_rx_ = 0;
    // Inyeccion hacia el MAC
    std::deque<std::pair<std::vector<uint8_t>, bool>> pend_;
    std::vector<uint8_t> hilo_;
    size_t idx_ = 0;
    // MDIO
    Fase fase_ = PREAMBULO;
    bool prev_mdc_ = false;
    unsigned unos_ = 0, bit_ = 0, st_ = 0, op_ = 0, pa_ = 0, ra_ = 0;
    uint32_t sr_ = 0;
    uint16_t dato_ = 0;
    unsigned n_mdio_rd_ = 0, n_mdio_wr_ = 0;
    sc_core::sc_event ev_;
};

} // namespace stm32
#endif // STM32_VERIF_EXT_PARTS_H
