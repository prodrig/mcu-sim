// =============================================================================
// i2c.h — Los I2C del MSP432E4, como maestros (T6, entrega 3)
//
// Diez módulos, I2C0..9 [H; hw_i2c.h; segunda fuente: i2c.c de DriverLib]. El
// maestro se maneja con cuatro registros y una «orden» en MCS:
//
//   MSA     la dirección del esclavo (7 bits) y R/S (1 = leer)
//   MCS     al escribir, la orden: RUN, START, STOP y ACK, que combinados son
//           las de I2CMasterControl (SINGLE_SEND = 0x07, BURST_SEND_START =
//           0x03, _CONT = 0x01, _FINISH = 0x05, _ERROR_STOP = 0x04; las de
//           recibir, con ACK). Al leer, el estado: BUSY, ERROR, ADRACK,
//           DATACK, ARBLST, IDLE, BUSBSY
//   MDR     el dato que sale o el que ha llegado
//   MTPR    la velocidad: SCL = reloj / (2 · (1 + TPR) · 10)
//   MIMR/MRIS/MMIS/MICR   RIS (la orden ha acabado), NACK, START, STOP, ARBLOST
//   MCR     MFE (maestro), SFE (esclavo), LPBK
//   MBMON   el estado de SCL y SDA
//
// LOS PINES van bit a bit, en drenador abierto: el módulo solo tira a cero o
// suelta, y el nivel alto lo pone el pull-up de la placa. Un esclavo puede
// retener SCL (clock stretching) y el maestro espera.
//
// LO QUE NO ESTÁ: el modo esclavo (SOAR, SCSR, SDR: se guardan, pero el módulo
// no contesta), las FIFO y la DMA del I2C, el modo rápido de 3,4 MHz (HS), el
// quick command y el tiempo de espera de SCL baja (MCLKOCNT) [P-17 i].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_I2C_H
#define STM32_PERIPH_MSP432E4_I2C_H

#include <functional>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"
#include "gpio.h"

namespace stm32 {

class I2cE4 : public BusSlave {
public:
    enum : uint32_t {
        R_MSA = 0x000, R_MCS = 0x004, R_MDR = 0x008, R_MTPR = 0x00C, R_MIMR = 0x010,
        R_MRIS = 0x014, R_MMIS = 0x018, R_MICR = 0x01C, R_MCR = 0x020,
        R_MCLKOCNT = 0x024, R_MBMON = 0x02C, R_SOAR = 0x800, R_SCSR = 0x804,
        R_SDR = 0x808, R_SIMR = 0x80C, R_SRIS = 0x810, R_SMIS = 0x814, R_SICR = 0x818,
        R_PP = 0xFC0, R_PC = 0xFC4
    };
    static constexpr uint32_t ST_BUSY = 1u << 0, ST_ERROR = 1u << 1, ST_ADRACK = 1u << 2,
        ST_DATACK = 1u << 3, ST_ARBLST = 1u << 4, ST_IDLE = 1u << 5, ST_BUSBSY = 1u << 6;
    static constexpr uint32_t RIS_RIS = 1u << 0, RIS_NACK = 1u << 4, RIS_START = 1u << 5,
        RIS_STOP = 1u << 6, RIS_ARB = 1u << 7;

    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_signal<bool> scl_o{"scl_o"}, sda_o{"sda_o"}, scl_oe{"scl_oe"}, sda_oe{"sda_oe"},
                             scl_i{"scl_i"}, sda_i{"sda_i"};

    explicit I2cE4(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(I2cE4);
        SC_THREAD(maestro_proc);
        SC_METHOD(bus_proc); sensitive << scl_i << sda_i; dont_initialize();
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    FuncionE4 fn_scl() { FuncionE4 f; f.out = &scl_o; f.oe = &scl_oe; f.in = &scl_i; f.idle_in = true; f.nombre = "I2CnSCL"; return f; }
    FuncionE4 fn_sda() { FuncionE4 f; f.out = &sda_o; f.oe = &sda_oe; f.in = &sda_i; f.idle_in = true; f.nombre = "I2CnSDA"; return f; }
    unsigned bytes() const { return n_bytes_; }

protected:
    uint32_t msa_ = 0, mdr_tx_ = 0, mdr_rx_ = 0, mtpr_ = 1, mimr_ = 0, mris_ = 0, mcr_ = 0,
             st_ = 0, soar_ = 0, simr_ = 0, clkocnt_ = 0;
    uint32_t orden_ = 0;
    bool pendiente_ = false, en_bus_ = false, bbusy_ = false, b_scl_ = true, b_sda_ = true;
    bool o_scl_ = true, o_sda_ = true;
    unsigned n_bytes_ = 0;
    sc_core::sc_event ev_, pub_ev_;

    bool maestro() const { return (mcr_ & 0x10u) != 0; }
    sc_core::sc_time cuarto() const {
        const double f = domain_hz();
        const double per = f > 0 ? 2.0 * (1.0 + double(mtpr_ & 0x7Fu)) * 10.0 / f : 10e-6;
        return sc_core::sc_time(per / 4.0, sc_core::SC_SEC);
    }
    void sda_(bool v) { o_sda_ = v; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void scl_(bool v) { o_scl_ = v; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        // Drenador abierto: el pin se conduce solo para bajarlo
        if (scl_o.read()) scl_o.write(false);
        if (sda_o.read()) sda_o.write(false);
        if (scl_oe.read() != !o_scl_) scl_oe.write(!o_scl_);
        if (sda_oe.read() != !o_sda_) sda_oe.write(!o_sda_);
        irq.write((mris_ & mimr_) != 0);
    }
    void marca(uint32_t b) { mris_ |= b; pub_ev_.notify(sc_core::SC_ZERO_TIME); }

    void reset_proc() {
        if (rst_n.read()) return;
        msa_ = mdr_tx_ = mdr_rx_ = mimr_ = mris_ = mcr_ = st_ = soar_ = simr_ = clkocnt_ = 0;
        mtpr_ = 1; orden_ = 0; pendiente_ = false; en_bus_ = false;
        o_scl_ = o_sda_ = true;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // START y STOP los vea quien los vea: BUSBSY
    void bus_proc() {
        const bool c = scl_i.read(), d = sda_i.read();
        const bool cp = b_scl_, dp = b_sda_;
        b_scl_ = c; b_sda_ = d;
        if (c && cp && dp && !d) bbusy_ = true;
        if (c && cp && !dp && d) bbusy_ = false;
    }
    bool sube_scl() {
        scl_(true);
        wait(sc_core::SC_ZERO_TIME); wait(sc_core::SC_ZERO_TIME);
        while (!scl_i.read()) {
            wait(scl_i.value_changed_event() | ev_);
            if (!maestro()) return false;
        }
        return true;
    }
    bool bit_(bool v, bool& leido, bool arbitra) {
        const sc_core::sc_time q = cuarto();
        wait(q); sda_(v);
        wait(q);
        if (!sube_scl()) return false;
        wait(q);
        leido = sda_i.read();
        if (arbitra && v && !leido) {
            st_ |= ST_ARBLST | ST_ERROR;
            en_bus_ = false;
            sda_(true); scl_(true);
            marca(RIS_ARB);
            return false;
        }
        wait(q); scl_(false);
        return true;
    }
    bool envia_byte(uint8_t b, bool& ack) {
        bool l = false;
        for (unsigned i = 0; i < 8; ++i) if (!bit_((b >> (7 - i)) & 1u, l, true)) return false;
        if (!bit_(true, l, false)) return false;
        ack = !l;
        return true;
    }
    bool recibe_byte(bool ack, uint8_t& v) {
        bool l = false;
        v = 0;
        for (unsigned i = 0; i < 8; ++i) {
            if (!bit_(true, l, false)) return false;
            v = uint8_t((v << 1) | (l ? 1u : 0u));
        }
        return bit_(!ack, l, false);              // ACK = SDA a 0
    }
    void stop_() {
        const sc_core::sc_time q = cuarto();
        wait(q); sda_(false);
        wait(q); sube_scl();
        wait(2 * q); sda_(true);
        wait(2 * q);
        en_bus_ = false;
        marca(RIS_STOP);
    }
    void start_() {
        const sc_core::sc_time q = cuarto();
        if (en_bus_) {                              // START repetido
            wait(q); sda_(true);
            wait(q); sube_scl();
            wait(q);
        }
        sda_(false);
        wait(2 * q);
        scl_(false);
        en_bus_ = true;
        marca(RIS_START);
    }

    void maestro_proc() {
        for (;;) {
            if (!pendiente_) { wait(ev_); continue; }
            pendiente_ = false;
            const uint32_t o = orden_;
            const bool run = o & 1u, start = o & 2u, stop = o & 4u, ack = o & 8u;
            const bool leer = msa_ & 1u;
            st_ &= ~(ST_ERROR | ST_ADRACK | ST_DATACK | ST_ARBLST);
            bool ok = true;
            if (!maestro()) { st_ &= ~ST_BUSY; marca(RIS_RIS); continue; }
            if (start && run) {
                while (bbusy_ && !en_bus_) wait(sda_i.value_changed_event() | scl_i.value_changed_event());
                start_();
                bool a = false;
                ok = envia_byte(uint8_t(msa_), a);
                if (ok && !a) { st_ |= ST_ADRACK | ST_ERROR; marca(RIS_NACK); }
            }
            if (ok && run && !(st_ & ST_ERROR)) {
                if (leer) {
                    uint8_t v = 0;
                    ok = recibe_byte(ack, v);
                    if (ok) { mdr_rx_ = v; ++n_bytes_; }
                } else {
                    bool a = false;
                    ok = envia_byte(uint8_t(mdr_tx_), a);
                    if (ok) ++n_bytes_;
                    if (ok && !a) { st_ |= ST_DATACK | ST_ERROR; marca(RIS_NACK); }
                }
            }
            if (ok && en_bus_ && (stop || (st_ & ST_ERROR && stop))) stop_();
            if (ok && en_bus_ && !run && stop) stop_();
            st_ &= ~ST_BUSY;
            marca(RIS_RIS);
        }
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_MSA: return msa_;
            case R_MCS: {
                uint32_t s = st_;
                if (!en_bus_ && !(st_ & ST_BUSY)) s |= ST_IDLE;
                if (bbusy_) s |= ST_BUSBSY;
                return s;
            }
            case R_MDR: return mdr_rx_;
            case R_MTPR: return mtpr_;
            case R_MIMR: return mimr_;
            case R_MRIS: return mris_;
            case R_MMIS: return mris_ & mimr_;
            case R_MCR: return mcr_;
            case R_MCLKOCNT: return clkocnt_;
            case R_MBMON: return (scl_i.read() ? 1u : 0u) | (sda_i.read() ? 2u : 0u);
            case R_SOAR: return soar_;
            case R_SIMR: return simr_;
            case R_PP: return 0x1u;                    // HS [1F]
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
            case R_MSA: msa_ = v & 0xFFu; break;
            case R_MCS:
                if (st_ & ST_BUSY) break;
                orden_ = v & 0x7Fu;
                if (orden_ & 0x7u) { st_ |= ST_BUSY; pendiente_ = true; ev_.notify(sc_core::SC_ZERO_TIME); }
                break;
            case R_MDR: mdr_tx_ = v & 0xFFu; break;
            case R_MTPR: mtpr_ = v & 0x700FFu; break;
            case R_MIMR: mimr_ = v & 0xFFFu; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_MICR: mris_ &= ~v; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_MCR: mcr_ = v & 0x31u; ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_MCLKOCNT: clkocnt_ = v & 0xFFu; break;
            case R_SOAR: soar_ = v & 0x7Fu; break;
            case R_SIMR: simr_ = v; break;
            default: break;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_I2C_H
