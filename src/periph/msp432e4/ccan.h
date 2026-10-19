// =============================================================================
// ccan.h — El controlador CAN del MSP432E4: el C_CAN de Bosch (T7)
//
// Dos en el chip, CAN0 en 0x4004_0000 y CAN1 en 0x4004_1000, cada uno con su
// bit de RCGCCAN y su línea (38 y 39) [H: msp432e401y.h; hw_can.h; segunda
// fuente de cada secuencia: can.c de DriverLib]. El protocolo -tiempo de bit,
// relleno, CRC, arbitraje, asentimiento, errores- es el motor de
// `periph/can_motor.h`, el mismo del bxCAN de los STM32: aquí solo está lo
// que el C_CAN tiene de suyo, que es mucho y muy distinto.
//
// LOS 32 OBJETOS DE MENSAJE. No hay buzones ni FIFOs: hay una RAM de 32
// objetos, cada uno con su identificador, su máscara, su dirección (DIR = 1
// transmite, 0 recibe), sus bits de control y sus ocho bytes. La CPU no la ve:
// la toca a través de dos INTERFACES de registros iguales (IF1 e IF2), que se
// cargan y se descargan escribiendo el número del objeto en IFnCRQ. Qué partes
// se copian, y en qué sentido, lo dice IFnCMSK (WRNRD, MASK, ARB, CONTROL,
// CLRINTPND, NEWDAT/TXRQST, DATAA, DATAB). DriverLib usa IF1 para escribir e
// IF2 para leer. La copia es instantánea: BUSY nunca se ve a 1 [modelo: en el
// chip son de 3 a 6 ciclos, y DriverLib lo espera igual].
//
//   * Transmite el objeto válido con TXRQST de NÚMERO MÁS BAJO, no el de
//     identificador más bajo: la prioridad entre los propios es el número
//     [1F: Bosch C_CAN User's Manual rev. 1.2, §3.4.1]. Al salir, TXRQST y
//     NEWDAT bajan y, con TXIE, sube INTPND.
//   * Recibe en el primer objeto válido de recepción cuyo identificador casa
//     (con la máscara si UMASK); si ese objeto tiene NEWDAT y no es EOB, sigue
//     al siguiente: así se arman las FIFOs (objetos seguidos, EOB en el
//     último). Si se machaca un NEWDAT, MSGLST. Con RXIE, INTPND.
//   * Una trama remota que casa con un objeto de transmisión con RMTEN le pone
//     TXRQST: la respuesta sale sola.
//   * CANINT dice la causa de mayor prioridad: 0x8000 (estado), o el objeto con
//     INTPND de número más bajo. Leer CANSTS borra la de estado.
//   * Bus-off pone INIT: el bloque se para hasta que el firmware lo quita
//     [Bosch §3.1.2]; aquí la recuperación (128 veces 11 bits recesivos) es
//     instantánea al quitar INIT [modelo].
//   * El modo de prueba (CANCTL.TEST): bucle (LBACK), silencioso (SILENT) y la
//     observación de CANRX en CANTST.RX. El modo básico (BASIC) y el control
//     directo de CANTX (CANTST.TX) se guardan y avisan [P-17 p].
//
// EL TIEMPO DE BIT: CANBIT (BRP, SJW, TSEG1, TSEG2) y CANBRPE, que extiende
// BRP a 10 bits; solo se escriben con INIT y CCE. El reloj es el del sistema.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_CCAN_H
#define STM32_PERIPH_MSP432E4_CCAN_H

#include <array>
#include <cstdio>
#include "../can_motor.h"

namespace stm32 {

class CCanE4 : public CanMotor {
public:
    sc_core::sc_out<bool> irq{"irq"};

    enum : uint32_t {
        R_CTL = 0x00, R_STS = 0x04, R_ERR = 0x08, R_BIT = 0x0C, R_INT = 0x10, R_TST = 0x14,
        R_BRPE = 0x18, R_IF1 = 0x20, R_IF2 = 0x80, R_TXRQ1 = 0x100, R_NWDA1 = 0x120,
        R_MSG1INT = 0x140, R_MSG1VAL = 0x160
    };
    enum : uint32_t {
        CTL_INIT = 0x01, CTL_IE = 0x02, CTL_SIE = 0x04, CTL_EIE = 0x08, CTL_DAR = 0x20,
        CTL_CCE = 0x40, CTL_TEST = 0x80,
        STS_TXOK = 0x08, STS_RXOK = 0x10, STS_EPASS = 0x20, STS_EWARN = 0x40, STS_BOFF = 0x80,
        TST_BASIC = 0x04, TST_SILENT = 0x08, TST_LBACK = 0x10, TST_TX = 0x60, TST_RX = 0x80,
        CMSK_WRNRD = 0x80, CMSK_MASK = 0x40, CMSK_ARB = 0x20, CMSK_CONTROL = 0x10,
        CMSK_CLRINTPND = 0x08, CMSK_TXRQST = 0x04, CMSK_DATAA = 0x02, CMSK_DATAB = 0x01,
        MSK2_MXTD = 0x8000, MSK2_MDIR = 0x4000,
        ARB2_MSGVAL = 0x8000, ARB2_XTD = 0x4000, ARB2_DIR = 0x2000,
        MCTL_NEWDAT = 0x8000, MCTL_MSGLST = 0x4000, MCTL_INTPND = 0x2000, MCTL_UMASK = 0x1000,
        MCTL_TXIE = 0x0800, MCTL_RXIE = 0x0400, MCTL_RMTEN = 0x0200, MCTL_TXRQST = 0x0100,
        MCTL_EOB = 0x0080
    };

    CCanE4(sc_core::sc_module_name nm, uint32_t base) : CanMotor(nm, base, 0x1000) {
        SC_HAS_PROCESS(CCanE4);
        SC_THREAD(bit_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(rst_proc);   sensitive << rst_n;
        reinicia();
    }
    unsigned avisos() const { return n_avisos_; }

    // Para el banco: un objeto entero, sin pasar por las interfaces
    struct Objeto { uint16_t msk1 = 0xFFFF, msk2 = 0xFFFF, arb1 = 0, arb2 = 0, mctl = 0,
                    d[4] = {0, 0, 0, 0}; };
    const Objeto& objeto(unsigned n) const { return obj_[(n - 1) & 31u]; }

protected:
    struct Interfaz { uint16_t crq = 1, cmsk = 0, msk1 = 0xFFFF, msk2 = 0xFFFF, arb1 = 0, arb2 = 0,
                      mctl = 0, d[4] = {0, 0, 0, 0}; };
    uint32_t ctl_ = CTL_INIT, sts_ = 0, bit_ = 0x2301, tst_ = 0, brpe_ = 0;
    bool int_estado_ = false;          // la causa 0x8000 de CANINT
    Interfaz if_[2];
    std::array<Objeto, 32> obj_{};
    bool o_irq_ = false;
    unsigned n_avisos_ = 0;
    bool avisado_basico_ = false, avisado_tx_ = false;

    void aviso(const char* m) {
        ++n_avisos_;
        char t[200];
        std::snprintf(t, sizeof t, "%s: %s", basename(), m);
        SC_REPORT_WARNING("msp432e4/can", t);
    }

    void reinicia() {
        ctl_ = CTL_INIT; sts_ = 0; bit_ = 0x2301; tst_ = 0; brpe_ = 0; int_estado_ = false;
        if_[0] = if_[1] = Interfaz{};
        obj_.fill(Objeto{});
        tec_ = rec_ = 0; lec_ = 0; boff_ = false;
        estado_ = IDLE; tx_activo_ = -1;
    }
    void rst_proc() {
        if (rst_n.read()) return;
        reinicia();
        n_tx_ = n_rx_ = 0;
        o_tx_ = true; o_oe_ = false;
        o_irq_ = false;
        publish();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        irq.write(o_irq_);
        tx_out.write(o_tx_); tx_oe.write(o_oe_);
    }

    // ---- Campos ---------------------------------------------------------
    static bool valido(const Objeto& o) { return o.arb2 & ARB2_MSGVAL; }
    static bool transmite(const Objeto& o) { return o.arb2 & ARB2_DIR; }
    static uint32_t id29(const Objeto& o) { return (uint32_t(o.arb2 & 0x1FFFu) << 16) | o.arb1; }
    static uint32_t msk29(const Objeto& o) { return (uint32_t(o.msk2 & 0x1FFFu) << 16) | o.msk1; }
    unsigned brp() const { return (bit_ & 0x3Fu) | ((brpe_ & 0xFu) << 6); }

    // ---- Los ganchos del motor --------------------------------------------
    bool     cm_parado() const override { return ctl_ & CTL_INIT; }
    double   cm_tq() const override {
        const double f = clk_hz.read();
        return f > 0.0 ? double(brp() + 1u) / f : 0.0;
    }
    unsigned cm_ts1() const override { return ((bit_ >> 8) & 0xFu) + 1u; }
    unsigned cm_ts2() const override { return ((bit_ >> 12) & 0x7u) + 1u; }
    bool     cm_bucle() const override { return (ctl_ & CTL_TEST) && (tst_ & TST_LBACK); }
    bool     cm_silencio() const override { return (ctl_ & CTL_TEST) && (tst_ & TST_SILENT); }
    int cm_siguiente() const override {
        for (unsigned i = 0; i < 32; ++i)
            if (valido(obj_[i]) && (obj_[i].mctl & MCTL_TXRQST)) return int(i);
        return -1;
    }
    CanFrame cm_marco(unsigned i) const override {
        const Objeto& o = obj_[i];
        CanFrame f;
        f.ide = o.arb2 & ARB2_XTD;
        f.id = f.ide ? id29(o) : ((o.arb2 & 0x1FFFu) >> 2);
        // Un objeto de RECEPCION con TXRQST pide el marco: sale una remota
        f.rtr = !transmite(o);
        f.dlc = uint8_t(o.mctl & 0xFu);
        if (f.dlc > 8) f.dlc = 8;
        for (unsigned k = 0; k < 4; ++k) {
            f.data[2 * k] = uint8_t(o.d[k]);
            f.data[2 * k + 1] = uint8_t(o.d[k] >> 8);
        }
        return f;
    }
    void cm_arbitraje_perdido(unsigned i) override {
        // Sin retransmision automatica (DAR), el intento cuenta como hecho
        if (ctl_ & CTL_DAR) obj_[i].mctl &= uint16_t(~MCTL_TXRQST);
        actualiza_irq();
    }
    void cm_tx_ok(unsigned i) override {
        Objeto& o = obj_[i];
        o.mctl &= uint16_t(~(MCTL_TXRQST | MCTL_NEWDAT));
        if (o.mctl & MCTL_TXIE) o.mctl |= MCTL_INTPND;
        lec_ = LEC_NONE;
        sts_ |= STS_TXOK;
        if (ctl_ & CTL_SIE) int_estado_ = true;
        actualiza_irq();
    }
    void cm_tx_error(unsigned i) override {
        if (ctl_ & CTL_DAR) obj_[i].mctl &= uint16_t(~MCTL_TXRQST);
    }
    bool casa(const Objeto& o, const CanFrame& f) const {
        const bool umask = o.mctl & MCTL_UMASK;
        const bool xtd = o.arb2 & ARB2_XTD;
        if ((!umask || (o.msk2 & MSK2_MXTD)) && xtd != f.ide) return false;
        const uint32_t fid = f.ide ? (f.id & 0x1FFFFFFFu) : ((f.id & 0x7FFu) << 18);
        const uint32_t util = f.ide ? 0x1FFFFFFFu : 0x1FFC0000u;
        const uint32_t m = umask ? msk29(o) : 0x1FFFFFFFu;
        return ((fid ^ id29(o)) & m & util) == 0;
    }
    void cm_rx_ok(const CanFrame& f) override {
        sts_ |= STS_RXOK;
        if (ctl_ & CTL_SIE) int_estado_ = true;
        if (f.rtr) {
            // Una remota: la contesta el objeto de transmision que case
            for (unsigned i = 0; i < 32; ++i) {
                Objeto& o = obj_[i];
                if (!valido(o) || !transmite(o) || !casa(o, f)) continue;
                if (o.mctl & MCTL_RMTEN) { o.mctl |= MCTL_TXRQST; ev_.notify(sc_core::SC_ZERO_TIME); }
                break;
            }
            actualiza_irq();
            return;
        }
        for (unsigned i = 0; i < 32; ++i) {
            Objeto& o = obj_[i];
            if (!valido(o) || transmite(o) || !casa(o, f)) continue;
            // Una FIFO: el objeto ocupado que no es el ultimo deja pasar al siguiente
            if ((o.mctl & MCTL_NEWDAT) && !(o.mctl & MCTL_EOB)) continue;
            if (o.mctl & MCTL_NEWDAT) o.mctl |= MCTL_MSGLST;
            if (o.mctl & MCTL_UMASK) {
                // Con mascara, el identificador que llego queda en el objeto
                const uint32_t id = f.ide ? f.id : ((f.id & 0x7FFu) << 18);
                o.arb1 = uint16_t(id);
                o.arb2 = uint16_t((o.arb2 & ~0x1FFFu) | ((id >> 16) & 0x1FFFu));
            }
            o.mctl = uint16_t((o.mctl & ~0xFu) | (f.dlc & 0xFu) | MCTL_NEWDAT);
            // Un objeto de recepcion que pidio con una remota deja de pedir
            o.mctl &= uint16_t(~MCTL_TXRQST);
            for (unsigned k = 0; k < 4; ++k)
                o.d[k] = uint16_t(f.data[2 * k] | (f.data[2 * k + 1] << 8));
            if (o.mctl & MCTL_RXIE) o.mctl |= MCTL_INTPND;
            ++n_rx_;
            break;
        }
        actualiza_irq();
    }
    void cm_eco() override { actualiza_irq(); }
    void cm_error() override {
        if (ctl_ & CTL_SIE) int_estado_ = true;
        actualiza_irq();
    }
    void cm_bus_off() override {
        // El C_CAN se para solo: INIT a 1 [Bosch §3.1.2]
        ctl_ |= CTL_INIT;
        if (ctl_ & CTL_EIE) int_estado_ = true;
    }

    // ---- Interrupciones -------------------------------------------------
    uint32_t causa() const {
        if (int_estado_) return 0x8000u;
        for (unsigned i = 0; i < 32; ++i) if (obj_[i].mctl & MCTL_INTPND) return i + 1;
        return 0;
    }
    void actualiza_irq() {
        const bool n = (ctl_ & CTL_IE) && causa() != 0;
        if (n != o_irq_) { o_irq_ = n; publish(); }
    }
    uint32_t sts() const {
        uint32_t s = (sts_ & (STS_TXOK | STS_RXOK)) | (lec_ & 7u);
        if (tec_ >= 128 || rec_ >= 128) s |= STS_EPASS;
        if (tec_ >= 96 || rec_ >= 96) s |= STS_EWARN;
        if (boff_) s |= STS_BOFF;
        return s;
    }

    // ---- Las interfaces --------------------------------------------------
    void transfiere(Interfaz& f) {
        const unsigned n = f.crq & 0x3Fu;
        if (n < 1 || n > 32) return;
        Objeto& o = obj_[n - 1];
        if (f.cmsk & CMSK_WRNRD) {
            if (f.cmsk & CMSK_MASK) { o.msk1 = f.msk1; o.msk2 = f.msk2; }
            if (f.cmsk & CMSK_ARB) { o.arb1 = f.arb1; o.arb2 = f.arb2; }
            if (f.cmsk & CMSK_CONTROL) o.mctl = f.mctl;
            if (f.cmsk & CMSK_TXRQST) o.mctl |= MCTL_TXRQST;
            if (f.cmsk & CMSK_DATAA) { o.d[0] = f.d[0]; o.d[1] = f.d[1]; }
            if (f.cmsk & CMSK_DATAB) { o.d[2] = f.d[2]; o.d[3] = f.d[3]; }
            if (o.mctl & MCTL_TXRQST) ev_.notify(sc_core::SC_ZERO_TIME);
        } else {
            if (f.cmsk & CMSK_MASK) { f.msk1 = o.msk1; f.msk2 = o.msk2; }
            if (f.cmsk & CMSK_ARB) { f.arb1 = o.arb1; f.arb2 = o.arb2; }
            if (f.cmsk & CMSK_CONTROL) f.mctl = o.mctl;
            if (f.cmsk & CMSK_DATAA) { f.d[0] = o.d[0]; f.d[1] = o.d[1]; }
            if (f.cmsk & CMSK_DATAB) { f.d[2] = o.d[2]; f.d[3] = o.d[3]; }
            if (f.cmsk & CMSK_CLRINTPND) o.mctl &= uint16_t(~MCTL_INTPND);
            if (f.cmsk & CMSK_TXRQST) o.mctl &= uint16_t(~MCTL_NEWDAT);
        }
        actualiza_irq();
    }
    uint32_t lee_if(const Interfaz& f, uint32_t o) const {
        switch (o) {
            case 0x00: return f.crq & 0x3Fu;                // BUSY siempre a 0
            case 0x04: return f.cmsk;
            case 0x08: return f.msk1;
            case 0x0C: return f.msk2;
            case 0x10: return f.arb1;
            case 0x14: return f.arb2;
            case 0x18: return f.mctl;
            case 0x1C: case 0x20: case 0x24: case 0x28: return f.d[(o - 0x1C) / 4];
            default: return 0;
        }
    }
    void escribe_if(Interfaz& f, uint32_t o, uint32_t v) {
        switch (o) {
            case 0x00: f.crq = uint16_t(v & 0x3Fu); transfiere(f); break;
            case 0x04: f.cmsk = uint16_t(v & 0xFFu); break;
            case 0x08: f.msk1 = uint16_t(v); break;
            case 0x0C: f.msk2 = uint16_t(v & 0xDFFFu); break;
            case 0x10: f.arb1 = uint16_t(v); break;
            case 0x14: f.arb2 = uint16_t(v); break;
            case 0x18: f.mctl = uint16_t(v & 0xFF8Fu); break;
            case 0x1C: case 0x20: case 0x24: case 0x28: f.d[(o - 0x1C) / 4] = uint16_t(v); break;
            default: break;
        }
    }
    uint32_t bits(uint16_t Objeto::*campo, uint16_t mascara, unsigned desde) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 16; ++i) if (obj_[desde + i].*campo & mascara) v |= 1u << i;
        return v;
    }

    uint32_t reg_read(uint32_t off) override {
        if (off >= R_IF1 && off < R_IF1 + 0x2C) return lee_if(if_[0], off - R_IF1);
        if (off >= R_IF2 && off < R_IF2 + 0x2C) return lee_if(if_[1], off - R_IF2);
        switch (off) {
            case R_CTL: return ctl_;
            case R_STS: {
                // Leer CANSTS borra la causa de estado de CANINT
                const uint32_t s = sts();
                if (int_estado_) { int_estado_ = false; actualiza_irq(); }
                return s;
            }
            case R_ERR: return (tec_ & 0xFFu) | ((rec_ & 0x7Fu) << 8) | (rec_ >= 128 ? 0x8000u : 0u);
            case R_BIT: return bit_;
            case R_INT: return causa();
            case R_TST: return (ctl_ & CTL_TEST) ? ((tst_ & 0x7Cu) | (rx_in.read() ? TST_RX : 0u)) : 0u;
            case R_BRPE: return brpe_;
            case R_TXRQ1: return bits(&Objeto::mctl, MCTL_TXRQST, 0);
            case R_TXRQ1 + 4: return bits(&Objeto::mctl, MCTL_TXRQST, 16);
            case R_NWDA1: return bits(&Objeto::mctl, MCTL_NEWDAT, 0);
            case R_NWDA1 + 4: return bits(&Objeto::mctl, MCTL_NEWDAT, 16);
            case R_MSG1INT: return bits(&Objeto::mctl, MCTL_INTPND, 0);
            case R_MSG1INT + 4: return bits(&Objeto::mctl, MCTL_INTPND, 16);
            case R_MSG1VAL: return bits(&Objeto::arb2, ARB2_MSGVAL, 0);
            case R_MSG1VAL + 4: return bits(&Objeto::arb2, ARB2_MSGVAL, 16);
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off >= R_IF1 && off < R_IF1 + 0x2C) { escribe_if(if_[0], off - R_IF1, v); return; }
        if (off >= R_IF2 && off < R_IF2 + 0x2C) { escribe_if(if_[1], off - R_IF2, v); return; }
        switch (off) {
            case R_CTL: {
                const bool init_antes = ctl_ & CTL_INIT;
                ctl_ = v & 0xEFu;
                // Quitar INIT tras un bus-off es la recuperacion [modelo: sin
                // esperar los 128 x 11 bits recesivos]
                if (init_antes && !(ctl_ & CTL_INIT) && boff_) { boff_ = false; tec_ = rec_ = 0; }
                if (!(ctl_ & CTL_TEST)) tst_ = 0;
                ev_.notify(sc_core::SC_ZERO_TIME);
                actualiza_irq();
                break;
            }
            case R_STS:
                // TXOK y RXOK se borran escribiendo 0; LEC se escribe (7: sin suceso)
                sts_ = (sts_ & ~(STS_TXOK | STS_RXOK)) | (v & sts_ & (STS_TXOK | STS_RXOK));
                lec_ = v & 7u;
                break;
            case R_BIT:
                if ((ctl_ & CTL_INIT) && (ctl_ & CTL_CCE)) bit_ = v & 0x7FFFu;
                break;
            case R_TST:
                if (!(ctl_ & CTL_TEST)) break;
                tst_ = v & 0x7Cu;
                if ((tst_ & TST_BASIC) && !avisado_basico_) {
                    avisado_basico_ = true; aviso("CANTST.BASIC (modo basico): no esta modelado");
                }
                if ((tst_ & TST_TX) && !avisado_tx_) {
                    avisado_tx_ = true; aviso("CANTST.TX (control directo de CANTX): no esta modelado");
                }
                ev_.notify(sc_core::SC_ZERO_TIME);
                break;
            case R_BRPE:
                if ((ctl_ & CTL_INIT) && (ctl_ & CTL_CCE)) brpe_ = v & 0xFu;
                break;
            default: break;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_CCAN_H
