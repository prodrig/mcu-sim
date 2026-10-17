// =============================================================================
// watchdog.h — Los dos perros del MSP432E4 (T6)
//
// WDT0 en 0x4000_0000, con el reloj del sistema, y WDT1 en 0x4000_1000, con
// PIOSC [H; hw_watchdog.h; segunda fuente: watchdog.c de DriverLib]. AL
// CONTRARIO QUE EL WDT_A DEL P4, ARRANCAN APAGADOS: un perro del E4 no muerde
// hasta que el firmware pone INTEN.
//
//   LOAD    el valor desde el que cuenta hacia abajo
//   VALUE   lo que lleva
//   CTL     INTEN (pone en marcha; una vez a 1 no se puede bajar), RESEN (el
//           segundo vencimiento resetea), INTTYPE (NMI en vez de interrupción)
//           y WRC (WDT1: la escritura se ha sincronizado con PIOSC)
//   ICR     cualquier escritura borra la interrupción y recarga LOAD
//   RIS/MIS la interrupción en bruto y enmascarada
//   TEST    STALL: se para con el núcleo parado por el depurador
//   LOCK    0x1ACCE551 abre; cualquier otra cosa cierra; se lee 1 cerrado
//
// AL VENCER: si la interrupción ya estaba puesta (no se atendió), y RESEN,
// reset con la causa en RESC (WDT0 o WDT1); si no, se pone y recarga.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_WATCHDOG_H
#define STM32_PERIPH_MSP432E4_WATCHDOG_H

#include <functional>
#include "../../common/periph_base.h"
#include "../../common/guarda_tick.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class WatchdogE4 : public BusSlave {
public:
    enum : uint32_t { R_LOAD = 0x000, R_VALUE = 0x004, R_CTL = 0x008, R_ICR = 0x00C,
                      R_RIS = 0x010, R_MIS = 0x014, R_TEST = 0x418, R_LOCK = 0xC00 };
    static constexpr uint32_t LLAVE = 0x1ACCE551u;

    sc_core::sc_out<bool> irq{"irq"};
    std::function<double()> f_reloj;
    std::function<bool()> nucleo_parado;
    std::function<void()> pide_reset;
    std::function<void()> pide_nmi;

    WatchdogE4(sc_core::sc_module_name nm, uint32_t base, bool es_wdt1)
        : BusSlave(nm, base, 0x1000), wdt1_(es_wdt1) {
        SC_HAS_PROCESS(WatchdogE4);
        SC_THREAD(cuenta_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    void recalcula() { ancla(); ev_.notify(sc_core::SC_ZERO_TIME); }
    bool en_marcha() const { return ctl_ & 1u; }
    unsigned vencimientos() const { return n_venc_; }
    unsigned resets_pedidos() const { return n_res_; }

protected:
    const bool wdt1_;
    uint32_t load_ = 0xFFFFFFFFu, ctl_ = 0, test_ = 0;
    bool ris_ = false, cerrado_ = false;
    double cuenta0_ = double(0xFFFFFFFFu);     // lo que queda en t_ref_
    sc_core::sc_time t_ref_;
    double f_ref_ = 0.0;
    bool parado_ref_ = false;
    unsigned n_venc_ = 0, n_res_ = 0;
    sc_core::sc_event ev_, pub_ev_;

    bool corre() const { return (ctl_ & 1u) && !parado_ref_ && f_ref_ > 0.0; }
    double queda() const {
        if (!corre()) return cuenta0_;
        const double q = cuenta0_ - (sc_core::sc_time_stamp() - t_ref_).to_seconds() * f_ref_;
        return q < 0 ? 0 : q;
    }
    void ancla() {
        cuenta0_ = queda();
        t_ref_ = sc_core::sc_time_stamp();
        f_ref_ = f_reloj ? f_reloj() : 0.0;
        parado_ref_ = (test_ & (1u << 8)) && nucleo_parado && nucleo_parado();
    }
    void reset_proc() {
        if (rst_n.read()) return;
        load_ = 0xFFFFFFFFu; ctl_ = wdt1_ ? (1u << 31) : 0u; test_ = 0;
        ris_ = false; cerrado_ = false;
        cuenta0_ = double(load_);
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        recalcula();
    }
    void pub_proc() { irq.write(ris_ && (ctl_ & 1u)); }

    void cuenta_proc() {
        for (;;) {
            if (!corre()) { wait(ev_); continue; }
            const double falta = queda();
            if (falta > GUARDA_TICK * f_ref_) {
                wait(sc_core::sc_time(falta / f_ref_, sc_core::SC_SEC), ev_);
                continue;
            }
            ++n_venc_;
            if (ris_ && (ctl_ & 2u)) {                // segundo: reset
                ++n_res_;
                if (pide_reset) pide_reset();
                wait(ev_);                            // el reset lo reanima
                continue;
            }
            ris_ = true;
            if ((ctl_ & 4u) && pide_nmi) pide_nmi();
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
            cuenta0_ = double(load_);
            t_ref_ = sc_core::sc_time_stamp();
            if (load_ == 0) wait(ev_);
        }
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_LOAD:  return load_;
            case R_VALUE: return uint32_t(queda());
            case R_CTL:   return ctl_ | (wdt1_ ? (1u << 31) : 0u);   // WRC: siempre listo
            case R_RIS:   return ris_ ? 1u : 0u;
            case R_MIS:   return (ris_ && (ctl_ & 1u)) ? 1u : 0u;
            case R_TEST:  return test_;
            case R_LOCK:  return cerrado_ ? 1u : 0u;
            // PrimeCell [H]
            case 0xFE0: return 0x05; case 0xFE4: return 0x18; case 0xFE8: return 0x18;
            case 0xFEC: return 0x01; case 0xFF0: return 0x0D; case 0xFF4: return 0xF0;
            case 0xFF8: return 0x06; case 0xFFC: return 0xB1;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off == R_LOCK) { cerrado_ = (v != LLAVE); return; }
        if (cerrado_) return;                         // cerrado: nada se escribe
        ancla();
        switch (off) {
            case R_LOAD: load_ = v; cuenta0_ = double(v); break;
            case R_CTL:  ctl_ = (ctl_ & 1u) | (v & 0x7u); break;   // INTEN no baja
            case R_ICR:  ris_ = false; cuenta0_ = double(load_); break;
            case R_TEST: test_ = v & (1u << 8); break;
            default: break;
        }
        ancla();
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_WATCHDOG_H
