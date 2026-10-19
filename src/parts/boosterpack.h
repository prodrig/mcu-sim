// =============================================================================
// boosterpack.h — Las piezas de la BOOSTXL-EDUMKII (fase T3 del MSP432)
//
// Lo que la placa educativa de TI lleva y el catálogo no tenía:
//
//   FuenteAjustable   una tensión que se mueve con un mando: los dos ejes del
//                     joystick, los tres del acelerómetro, el micrófono
//   Zumbador          una carga que escucha el PWM que le llega y dice a qué
//                     frecuencia suena (el CMT-8540S tras su transistor)
//   Opt3001           el sensor de luz ambiental I2C de TI
//   Tmp006            el termómetro de infrarrojos I2C de TI
//
// Los dos sensores hablan I2C DE VERDAD, bit a bit sobre los nodos, como la
// 24Cxx (ext_parts.h, I2cEeprom), con la base común I2cReg16: un byte de
// puntero y registros de 16 bits, el byte alto primero. Sus registros y
// valores de reset son los de sus hojas de datos (OPT3001: SBOS681; TMP006:
// SBOS518) [1F]; los tiempos de conversión también, sin contrastar.
// =============================================================================
#ifndef STM32_PARTS_BOOSTERPACK_H
#define STM32_PARTS_BOOSTERPACK_H

#include <systemc>
#include <cmath>
#include <cstdint>
#include <string>
#include "ext_parts.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Una tensión ajustable: un potenciómetro con el cursor al pin, visto como
// su equivalente de Thevenin. El mando es la tensión.
// ---------------------------------------------------------------------------
class FuenteAjustable : public ExtPart {
public:
    FuenteAjustable(analog_net_if& n, double v = 1.65, double vmin = 0.0,
                    double vmax = 3.3, double r = 100.0, const char* nombre = nullptr)
        : ExtPart(n, "FuenteAjustable", "fuente_ajustable", "pin", nombre),
          v_(v), vmin_(vmin), vmax_(vmax), r_(r) { aplica(); }
    void   pon_tension(double v) { v_ = std::min(std::max(v, vmin_), vmax_); aplica(); }
    double tension() const { return v_; }
    void set_enabled(bool on) override { ExtPartBase::set_enabled(on); if (on) aplica(); }

    unsigned n_mandos() const override { return 1; }
    Mando    mando(unsigned) const override {
        return {"tension", Mando::Continuo, float(vmin_), float(vmax_)};
    }
    float valor_mando(unsigned) const override { return float(v_); }
    void  acciona(unsigned, float v) override { pon_tension(double(v)); }
    unsigned   n_observables() const override { return 1; }
    Observable observable(unsigned) const override {
        return {"tension", "V", float(vmin_), float(vmax_), true};
    }
    float valor_observable(unsigned) const override { return float(v_); }
private:
    void aplica() { if (conectada_) drive(float(v_), float(r_)); else hiz(); }
    double v_, vmin_, vmax_, r_;
};

// ---------------------------------------------------------------------------
// Un zumbador: una carga a masa que mide el periodo entre flancos de subida
// del nodo (umbral de 1,65 V con histéresis) y dice si suena y a cuánto.
// ---------------------------------------------------------------------------
SC_MODULE(Zumbador), public ExtPart {
    Zumbador(sc_core::sc_module_name nm, analog_net_if& n, double r_carga = 1000.0)
        : sc_core::sc_module(nm), ExtPart(n, "Zumbador", "zumbador", "pin", nm), r_(r_carga) {
        drive(0.0f, float(r_));
        SC_HAS_PROCESS(Zumbador);
        SC_METHOD(flanco); sensitive << n.value_changed_event(); dont_initialize();
    }
    // Suena si el último flanco de subida es reciente: menos de tres periodos
    bool   sonando() const {
        if (periodo_ <= sc_core::SC_ZERO_TIME) return false;
        return sc_core::sc_time_stamp() - ultimo_ < 3 * periodo_ + sc_core::sc_time(1, sc_core::SC_MS);
    }
    double frecuencia() const { return sonando() ? 1.0 / periodo_.to_seconds() : 0.0; }
    uint64_t flancos() const { return n_; }
    void set_enabled(bool on) override {
        ExtPartBase::set_enabled(on);
        if (on) drive(0.0f, float(r_)); else hiz();
    }
    unsigned   n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"sonando", "", 0.f, 1.f, true};
        return {"frecuencia", "Hz", 0.f, 10000.f, true};
    }
    float valor_observable(unsigned i) const override {
        return i == 0 ? (sonando() ? 1.f : 0.f) : float(frecuencia());
    }
private:
    void flanco() {
        if (!conectada_) return;
        const double v = net_->voltage();
        const bool alto = alto_ ? v > 1.4 : v > 1.9;
        if (alto && !alto_) {
            const sc_core::sc_time t = sc_core::sc_time_stamp();
            if (n_ > 0) periodo_ = t - ultimo_;
            ultimo_ = t;
            ++n_;
        }
        alto_ = alto;
    }
    double r_;
    bool alto_ = false;
    uint64_t n_ = 0;
    sc_core::sc_time ultimo_, periodo_;
};

// ---------------------------------------------------------------------------
// La base de los dos sensores: un esclavo I2C con un byte de puntero y
// registros de 16 bits (el byte alto primero). Escribir: [dir|W][puntero]
// [alto][bajo]...; leer: [dir|R] y el registro del puntero, una y otra vez.
// La máquina de bits es la de I2cEeprom.
// ---------------------------------------------------------------------------
class I2cReg16 : public sc_core::sc_module, public I2cPart {
public:
    I2cReg16(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
             uint8_t dir, const char* tipo, double vdd = 3.3)
        : sc_core::sc_module(nm), I2cPart(scl, sda, tipo, "i2c_sensor", nm, vdd), dir_(dir) {
        SC_HAS_PROCESS(I2cReg16);
        SC_METHOD(edge_proc);
        sensitive << scl_->value_changed_event() << sda_->value_changed_event();
        dont_initialize();
    }
    uint8_t direccion() const { return dir_; }
    unsigned escrituras() const { return n_wr_; }
    unsigned lecturas() const { return n_rd_; }
protected:
    virtual uint16_t lee_reg(uint8_t r) = 0;
    virtual void escribe_reg(uint8_t r, uint16_t v) = 0;
    // Se ha leído el registro entero (los dos bytes): algunos bits se borran
    virtual void leido(uint8_t) {}

    enum St { IDLE, ADDR, ACK_HOLD, RX, TX, MACK };
    void edge_proc() {
        const bool c = scl(), d = sda();
        const bool cp = c_prev_, dp = d_prev_;
        c_prev_ = c; d_prev_ = d;
        if (c && cp) {
            if (dp && !d) { st_ = ADDR; bit_ = 0; sh_ = 0; nrx_ = 0; release_sda(); return; }
            if (!dp && d) { st_ = IDLE; release_sda(); return; }
        }
        if (c && !cp) {
            switch (st_) {
                case ADDR:
                    sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                    if (++bit_ == 8) {
                        if ((sh_ >> 1) == dir_) { lee_ = (sh_ & 1u) != 0; ack_next_ = true; }
                        else { st_ = IDLE; ack_next_ = false; }
                    }
                    break;
                case RX:
                    sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                    if (++bit_ == 8) {
                        if (nrx_ == 0) ptr_ = sh_;
                        else if (nrx_ % 2 == 1) alto_ = sh_;
                        else { escribe_reg(ptr_, uint16_t((alto_ << 8) | sh_)); ++n_wr_; }
                        ++nrx_;
                        ack_next_ = true;
                    }
                    break;
                case ACK_HOLD:
                    if (lee_) { st_ = TX; bit_ = 0; sh_ = siguiente_tx(); }
                    else      { st_ = RX; bit_ = 0; sh_ = 0; }
                    break;
                case TX:
                    if (bit_ >= 8) st_ = MACK;
                    break;
                case MACK:
                    if (d) { st_ = IDLE; release_sda(); }
                    else   { st_ = TX; bit_ = 0; sh_ = siguiente_tx(); }
                    break;
                default: break;
            }
            return;
        }
        if (!c && cp) {
            if (ack_next_) { ack_next_ = false; pull_sda(); st_ = ACK_HOLD; ntx_ = 0; return; }
            switch (st_) {
                case RX:  if (bit_ == 0) release_sda(); break;
                case TX:  if (bit_ < 8) { drive_sda((sh_ >> (7 - bit_)) & 1u); ++bit_; } break;
                case MACK: release_sda(); break;
                default: break;
            }
        }
    }
    uint8_t siguiente_tx() {
        if (ntx_ % 2 == 0) { reg_tx_ = lee_reg(ptr_); ++ntx_; return uint8_t(reg_tx_ >> 8); }
        ++ntx_; ++n_rd_;
        leido(ptr_);
        return uint8_t(reg_tx_);
    }
    uint8_t dir_;
    St st_ = IDLE;
    unsigned bit_ = 0, nrx_ = 0, ntx_ = 0, n_wr_ = 0, n_rd_ = 0;
    uint8_t sh_ = 0, ptr_ = 0, alto_ = 0;
    uint16_t reg_tx_ = 0;
    bool c_prev_ = true, d_prev_ = true, lee_ = false, ack_next_ = false;
};

// ---------------------------------------------------------------------------
// OPT3001 [SBOS681]: luz ambiental en lux, 0,01·2^E·M con M de 12 bits.
//   0x00 RESULT   E[15:12] M[11:0]
//   0x01 CONFIG   RN[15:12] CT M[10:9] OVF CRF FH FL L POL ME FC[1:0]; 0xC810
//   0x02 LOW LIMIT, 0x03 HIGH LIMIT (0xC000 y 0xBFFF), 0x7E 0x5449, 0x7F 0x3001
// Conversión de 100 ms (CT = 0) u 800 ms; M = 01 una vez, 10/11 continuo.
// RN = 1100 es la escala automática: el E más pequeño en que M cabe. Al
// acabar: CRF, y FH/FL contra los límites; INT (drenador abierto) baja con
// FH o FL, y con L = 1 se queda hasta leer CONFIG.
// ---------------------------------------------------------------------------
class Opt3001 : public I2cReg16 {
public:
    Opt3001(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
            analog_net_if* intn = nullptr, uint8_t dir = 0x44, double lux = 300.0)
        : I2cReg16(nm, scl, sda, dir, "Opt3001"), lux_(lux), int_(intn) {
        if (int_) { id_int_ = add_pin("int", *int_, "opt3001_int"); int_->set_hiz(id_int_); }
        SC_HAS_PROCESS(Opt3001);
        SC_THREAD(conv_proc);
    }
    void   pon_lux(double l) { lux_ = std::max(0.0, std::min(l, 83865.6)); }
    double lux() const { return lux_; }
    unsigned conversiones() const { return n_conv_; }
    static double a_lux(uint16_t r) { return 0.01 * double(1u << (r >> 12)) * double(r & 0xFFFu); }

    unsigned n_mandos() const override { return 1; }
    Mando    mando(unsigned) const override { return {"lux", Mando::Continuo, 0.f, 83865.f}; }
    float valor_mando(unsigned) const override { return float(lux_); }
    void  acciona(unsigned, float v) override { pon_lux(double(v)); }
    unsigned   n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"lux", "lx", 0.f, 2000.f, true}; }
    float valor_observable(unsigned) const override { return float(lux_); }

protected:
    uint16_t lee_reg(uint8_t r) override {
        switch (r) {
            case 0x00: return result_;
            case 0x01: return cfg_;
            case 0x02: return lo_;
            case 0x03: return hi_;
            case 0x7E: return 0x5449;
            case 0x7F: return 0x3001;
            default:   return 0;
        }
    }
    void leido(uint8_t r) override {
        if (r == 0x01) {
            cfg_ &= uint16_t(~0x0080u);                       // CRF
            if (cfg_ & 0x0010u) { cfg_ &= uint16_t(~0x0060u); pinta_int(); }   // L: FH/FL
        }
    }
    void escribe_reg(uint8_t r, uint16_t v) override {
        switch (r) {
            case 0x01:
                cfg_ = uint16_t((cfg_ & 0x01E0u) | (v & 0xFE1Fu));
                ev_.notify(sc_core::SC_ZERO_TIME);
                break;
            case 0x02: lo_ = v; break;
            case 0x03: hi_ = v; break;
            default: break;
        }
    }
private:
    double lux_;
    analog_net_if* int_;
    int id_int_ = -1;
    uint16_t result_ = 0, cfg_ = 0xC810, lo_ = 0xC000, hi_ = 0xBFFF;
    unsigned n_conv_ = 0;
    sc_core::sc_event ev_;

    unsigned modo() const { return (cfg_ >> 9) & 3u; }
    uint16_t codifica(double l) const {
        const unsigned rn = cfg_ >> 12;
        unsigned e = 0;
        if (rn == 0xC) {
            while (e < 11 && l / (0.01 * double(1u << e)) > 4095.0) ++e;
        } else e = rn > 11 ? 11 : rn;
        double m = std::floor(l / (0.01 * double(1u << e)));
        if (m > 4095.0) { m = 4095.0; }
        return uint16_t((e << 12) | unsigned(m));
    }
    void pinta_int() {
        if (!int_) return;
        const bool pol = (cfg_ & 0x0008u) != 0;               // 1: activo alto
        const bool activo = (cfg_ & 0x0060u) != 0;
        if (activo != pol) int_->set_drive(id_int_, 0.0f, 30.0f);   // tirar a 0
        else int_->set_hiz(id_int_);
    }
    void conv_proc() {
        for (;;) {
            while (modo() == 0) wait(ev_);
            const sc_core::sc_time t((cfg_ & 0x0800u) ? 800.0 : 100.0, sc_core::SC_MS);
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            wait(t, ev_);
            if (modo() == 0 || sc_core::sc_time_stamp() - t0 < t) continue;   // reconfigurado
            result_ = codifica(lux_);
            ++n_conv_;
            cfg_ |= 0x0080u;                                      // CRF
            if (lux_ > a_lux(hi_)) cfg_ |= 0x0040u;               // FH
            else if (lux_ < a_lux(lo_)) cfg_ |= 0x0020u;          // FL
            else if (!(cfg_ & 0x0010u)) cfg_ &= uint16_t(~0x0060u);
            pinta_int();
            if (modo() == 1) cfg_ = uint16_t(cfg_ & ~0x0600u);   // una vez: a apagado
        }
    }
};

// ---------------------------------------------------------------------------
// TMP006 [SBOS518]: termómetro de termopila.
//   0x00 VOBJECT  la tensión del sensor, 156,25 nV/LSB, con signo
//   0x01 TAMBIENT la del chip, 1/32 C/LSB en [15:2]
//   0x02 CONFIG   RST MOD[14:12] CR[11:9] EN DRDY; 0x7400
//   0xFE 0x5449, 0xFF 0x0067
// Conversiones cada 0,25·2^CR s, con MOD = 111. Al acabar, DRDY (y la patilla
// DRDY, activa baja, si EN); leer un resultado lo baja. La tensión es la de
// la ecuación de la hoja de datos sin los términos de segundo orden:
// V = S0·(Tobj^4 - Tdie^4), con S0 = 6,4e-14 (la calibración típica), en K.
// ---------------------------------------------------------------------------
class Tmp006 : public I2cReg16 {
public:
    Tmp006(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda,
           analog_net_if* drdy = nullptr, uint8_t dir = 0x40,
           double t_chip = 25.0, double t_obj = 25.0)
        : I2cReg16(nm, scl, sda, dir, "Tmp006"), t_chip_(t_chip), t_obj_(t_obj), drdy_(drdy) {
        if (drdy_) { id_drdy_ = add_pin("drdy", *drdy_, "tmp006_drdy"); drdy_->set_hiz(id_drdy_); }
        SC_HAS_PROCESS(Tmp006);
        SC_THREAD(conv_proc);
    }
    void pon_t_chip(double t) { t_chip_ = t; }
    void pon_t_objeto(double t) { t_obj_ = t; }
    unsigned conversiones() const { return n_conv_; }
    static double v_sensor(double t_obj, double t_chip) {
        const double to = t_obj + 273.15, td = t_chip + 273.15;
        return 6.4e-14 * (to * to * to * to - td * td * td * td);
    }

    unsigned n_mandos() const override { return 2; }
    Mando    mando(unsigned i) const override {
        return i == 0 ? Mando{"t_objeto", Mando::Continuo, -40.f, 125.f}
                      : Mando{"t_chip", Mando::Continuo, -40.f, 125.f};
    }
    float valor_mando(unsigned i) const override { return float(i == 0 ? t_obj_ : t_chip_); }
    void  acciona(unsigned i, float v) override { if (i == 0) t_obj_ = v; else t_chip_ = v; }
    unsigned   n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"t_objeto", "C", -40.f, 125.f, true}; }
    float valor_observable(unsigned) const override { return float(t_obj_); }

protected:
    uint16_t lee_reg(uint8_t r) override {
        switch (r) {
            case 0x00: return vobj_;
            case 0x01: return tamb_;
            case 0x02: return cfg_;
            case 0xFE: return 0x5449;
            case 0xFF: return 0x0067;
            default:   return 0;
        }
    }
    void leido(uint8_t r) override {
        if (r == 0x00 || r == 0x01) { cfg_ &= uint16_t(~0x0080u); pinta(); }
    }
    void escribe_reg(uint8_t r, uint16_t v) override {
        if (r != 0x02) return;
        if (v & 0x8000u) { cfg_ = 0x7400; vobj_ = tamb_ = 0; }
        else cfg_ = uint16_t((cfg_ & 0x0080u) | (v & 0x7F00u));
        pinta();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
private:
    double t_chip_, t_obj_;
    analog_net_if* drdy_;
    int id_drdy_ = -1;
    uint16_t vobj_ = 0, tamb_ = 0, cfg_ = 0x7400;
    unsigned n_conv_ = 0;
    sc_core::sc_event ev_;
    void pinta() {
        if (!drdy_) return;
        if ((cfg_ & 0x0100u) && (cfg_ & 0x0080u)) drdy_->set_drive(id_drdy_, 0.0f, 30.0f);
        else drdy_->set_hiz(id_drdy_);
    }
    void conv_proc() {
        for (;;) {
            while (((cfg_ >> 12) & 7u) != 7u) wait(ev_);
            const sc_core::sc_time t(0.25 * double(1u << ((cfg_ >> 9) & 7u)), sc_core::SC_SEC);
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            wait(t, ev_);
            if (((cfg_ >> 12) & 7u) != 7u || sc_core::sc_time_stamp() - t0 < t) continue;
            const double v = v_sensor(t_obj_, t_chip_) / 156.25e-9;
            const long vi = std::lround(std::max(-32768.0, std::min(32767.0, v)));
            vobj_ = uint16_t(int16_t(vi));
            const long ti = std::lround(t_chip_ * 32.0);
            tamb_ = uint16_t(int16_t(ti * 4));             // ti puede ser negativo
            ++n_conv_;
            cfg_ |= 0x0080u;
            pinta();
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_BOOSTERPACK_H
