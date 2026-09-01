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
#include <utility>
#include "../common/analog_net.h"
#include "../periph/sdio.h"   // sd_crc7 y SdCrc16: el protocolo es el mismo

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
SC_MODULE(SdCard) {
    SdCard(sc_core::sc_module_name nm, analog_net_if& ck, analog_net_if& cmd,
           analog_net_if* d0, analog_net_if* d1, analog_net_if* d2,
           analog_net_if* d3, double vdd = 3.3)
        : sc_core::sc_module(nm), ck_(&ck), cmd_(&cmd), vdd_(vdd) {
        dat_[0] = d0; dat_[1] = d1; dat_[2] = d2; dat_[3] = d3;
        id_cmd_ = cmd_->register_driver("sdcard_cmd");
        // El pull-up de la placa: CMD y las líneas de datos reposan en alto,
        // como en cualquier zócalo de tarjeta.
        id_cmd_pu_ = cmd_->register_driver("sd_pu_cmd");
        cmd_->set_drive(id_cmd_pu_, float(vdd_), 47e3f);
        cmd_->set_hiz(id_cmd_);
        for (unsigned i = 0; i < 4; ++i) {
            if (!dat_[i]) continue;
            id_dat_[i] = dat_[i]->register_driver("sdcard_dat");
            id_dat_pu_[i] = dat_[i]->register_driver("sd_pu_dat");
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
