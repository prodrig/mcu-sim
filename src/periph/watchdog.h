// =============================================================================
// watchdog.h — Los dos perros guardianes: IWDG y WWDG [IR, §12.10, §12.11]
//
// El dispositivo lleva DOS perros guardianes que se parecen poco: no son dos
// instancias de un bloque, son dos bloques distintos que resuelven el mismo
// problema de formas opuestas, y esa es la razón de que existan los dos.
//
//   IWDG — perro INDEPENDIENTE. Cuenta con el LSI (~32 kHz), un oscilador que
//     no depende del árbol de reloj del sistema. Por eso sigue vigilando aunque
//     el firmware estropee el PLL, se quede sin HSE o pare el SYSCLK: es el que
//     salva de un fallo de reloj. Solo sabe hacer una cosa —resetear— y no
//     genera interrupción. Se maneja con LLAVES escritas en KR, no con bits.
//
//   WWDG — perro de VENTANA. Cuenta con PCLK1/4096, así que se para con el
//     reloj de sistema y no sirve contra un fallo del árbol de reloj. A cambio
//     vigila algo que el otro no puede: que el refresco llegue NI TARDE NI
//     PRONTO. Refrescar antes de que el contador baje de la ventana W es tan
//     grave como no refrescar, y provoca reset igual. Eso detecta un programa
//     que se ha ido por una rama equivocada y da vueltas de más. Además avisa
//     antes de morir (EWI, IRQ 0), lo que da una última oportunidad de guardar
//     el estado.
//
// Modelado EVENTO A EVENTO, como los temporizadores de la fase F4: ninguno de
// los dos cuenta flanco a flanco. Cada uno calcula CUÁNDO vencerá y programa un
// solo evento; el contador se interpola al leerlo. Eso los hace baratos y, en
// el caso del IWDG, INMUNES a que se apague la onda cuadrada de los relojes
// internos, que es justo lo que hay que exigirle: el perro independiente tiene
// que seguir contando cuando todo lo demás se para [IR, §12.11].
//
// Los dos piden el reset al RCC por rst_req; el RCC lo convierte en reset de
// sistema y deja constancia en RCC_CSR (IWDGRSTF, WWDGRSTF) [IR, §4.1].
// =============================================================================
#ifndef STM32_PERIPH_WATCHDOG_H
#define STM32_PERIPH_WATCHDOG_H

#include "../common/periph_base.h"
#include "../common/guarda_tick.h"

namespace stm32 {

// ===========================================================================
// WWDG — perro guardián de ventana [IR, §12.10]
// ===========================================================================
class Wwdg : public BusSlave {
public:
    sc_core::sc_out<bool> irq_ewi{"irq_ewi"};       // IRQ 0 (aviso temprano)
    sc_core::sc_out<bool> rst_req{"rst_req"};       // -> RCC
    sc_core::sc_in<bool>  freeze{"freeze"};         // DBGMCU

    enum : uint32_t { R_CR = 0x00, R_CFR = 0x04, R_SR = 0x08 };
    static constexpr uint32_t CR_WDGA = 1u << 7;
    static constexpr uint32_t CFR_EWI = 1u << 9;
    static constexpr uint32_t SR_EWIF = 1u << 0;

    Wwdg(sc_core::sc_module_name nm) : BusSlave(nm, addr::WWDG_B, 0x400) {
        SC_HAS_PROCESS(Wwdg);
        SC_THREAD(count_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(freeze_proc); sensitive << freeze; dont_initialize();
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    unsigned counter() const { return t_now(); }
    double   period_s() const { return tick_period(); }
    bool     active()  const { return (cr_ & CR_WDGA) != 0; }

protected:
    uint32_t cr_ = 0x7Fu, cfr_ = 0x7Fu, sr_ = 0;
    unsigned t0_ = 0x7Fu;                     // contador en t_ref_
    sc_core::sc_time t_ref_{sc_core::SC_ZERO_TIME};
    bool     o_irq_ = false, o_rst_ = false;
    sc_core::sc_event ev_, pub_ev_;

    unsigned wdgtb() const { return (cfr_ >> 7) & 3u; }
    unsigned window() const { return cfr_ & 0x7Fu; }
    // t_WWDG = t_PCLK1 * 4096 * 2^WDGTB por cuenta [IR, §12.10.2]
    double tick_period() const {
        const double f = clk_hz.read();
        if (f <= 0.0) return 0.0;
        return 4096.0 * double(1u << wdgtb()) / f;
    }
    // Contador interpolado: nadie cuenta flanco a flanco, se calcula.
    unsigned t_now() const {
        if (!(cr_ & CR_WDGA) || frozen_) return t0_;
        const double p = tick_period();
        if (p <= 0.0) return t0_;
        const double dt = (sc_core::sc_time_stamp() - t_ref_).to_seconds();
        const unsigned n = unsigned(dt / p + GUARDA_TICK);
        return (n >= t0_) ? 0u : (t0_ - n);
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return (cr_ & CR_WDGA) | (t_now() & 0x7Fu);
            case R_CFR: return cfr_;
            case R_SR:  return sr_;
            default:    return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR: {
                const bool was_active = (cr_ & CR_WDGA) != 0;
                const unsigned t_new = v & 0x7Fu;
                // WDGA es "rs": una vez puesto solo lo baja el reset.
                cr_ = (cr_ | (v & CR_WDGA)) & (CR_WDGA | 0x7Fu);
                cr_ = (cr_ & CR_WDGA) | t_new;
                if (was_active) {
                    // LA VENTANA: refrescar DEMASIADO PRONTO es tan grave como
                    // no refrescar. Si el contador todavía está por encima de
                    // W, el refresco provoca reset [IR, §12.10.1].
                    if (t_now() > window()) { trip(); return; }
                    // Y recargar por debajo de 0x40 tampoco salva: el bit 6 ya
                    // se ha perdido.
                    if (t_new < 0x40u) { trip(); return; }
                }
                t0_ = t_new;
                t_ref_ = sc_core::sc_time_stamp();
                if (cr_ & CR_WDGA) arm();
                return;
            }
            case R_CFR: {
                cfr_ = v & 0x3FFu;
                t0_ = t_now(); t_ref_ = sc_core::sc_time_stamp();
                if (cr_ & CR_WDGA) arm();
                return;
            }
            case R_SR:
                // EWIF es rc_w0: escribir cero lo borra [IR, §12.10.2]
                sr_ &= v & SR_EWIF;
                if (!(sr_ & SR_EWIF) && o_irq_) { o_irq_ = false; publish(); }
                return;
            default: return;
        }
    }
    unsigned access_cycles(bool) const override { return 1; }

    void arm() { ev_.notify(sc_core::SC_ZERO_TIME); }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() { irq_ewi.write(o_irq_); rst_req.write(o_rst_); }
    void freeze_proc() {
        // El depurador congela la cuenta: sin esto, parar en un punto de
        // interrupción reiniciaría el dispositivo [IR, §12.10; DBGMCU].
        if (freeze.read()) { t0_ = t_now(); frozen_ = true; }
        else { frozen_ = false; t_ref_ = sc_core::sc_time_stamp(); }
        arm();
    }
    void trip() {
        o_rst_ = true;
        publish();
        // El reset del sistema retira la petición: se deja un pulso.
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void count_proc() {
        for (;;) {
            if (o_rst_) {                       // pulso de petición de reset
                wait(sc_core::sc_time(4, sc_core::SC_US));
                o_rst_ = false; publish();
                continue;
            }
            const double p = tick_period();
            if (!(cr_ & CR_WDGA) || frozen_ || p <= 0.0) { wait(ev_); continue; }
            const unsigned t = t_now();
            // Dos citas: cuando el contador llegue a 0x40 salta el aviso
            // temprano, y una cuenta después llega el reset [IR, §12.10.1].
            if (t > 0x40u) {
                wait(sc_core::sc_time((double(t - 0x40u)) * p, sc_core::SC_SEC), ev_);
                continue;
            }
            if (t == 0x40u) {
                if (cfr_ & CFR_EWI) {
                    sr_ |= SR_EWIF;
                    o_irq_ = true;
                    publish();
                }
                wait(sc_core::sc_time(p, sc_core::SC_SEC), ev_);
                if (t_now() > 0x40u) continue;      // lo han refrescado a tiempo
                continue;
            }
            // T ha pasado de 0x40 a 0x3F: se acabó.
            trip();
        }
    }
    void reset_proc() {
        if (rst_n.read()) return;
        cr_ = 0x7Fu; cfr_ = 0x7Fu; sr_ = 0;
        t0_ = 0x7Fu; t_ref_ = sc_core::sc_time_stamp();
        frozen_ = false;
        o_irq_ = false; o_rst_ = false;
        publish();
        arm();
    }
    bool frozen_ = false;
};

// ===========================================================================
// IWDG — perro guardián independiente [IR, §12.11]
// ===========================================================================
class Iwdg : public BusSlave {
public:
    sc_core::sc_in<bool>   lsi_clk{"lsi_clk"};      // reloj propio (LSI)
    sc_core::sc_in<double> lsi_hz{"lsi_hz"};        // y su frecuencia
    sc_core::sc_out<bool>  rst_req{"rst_req"};      // -> RCC
    // Arrancar el perro enciende el LSI por hardware: el firmware no tiene
    // que acordarse, y sobre todo no puede desarmarlo apagando el oscilador.
    sc_core::sc_out<bool>  lsi_on_req{"lsi_on_req"};
    sc_core::sc_in<bool>   freeze{"freeze"};
    sc_core::sc_in<bool>   hw_start{"hw_start"};    // option bit WDG_SW = 0

    enum : uint32_t { R_KR = 0x00, R_PR = 0x04, R_RLR = 0x08, R_SR = 0x0C };
    static constexpr uint32_t SR_PVU = 1u << 0, SR_RVU = 1u << 1;
    // Las tres llaves. No hay bits de control: el IWDG se maneja escribiendo
    // valores mágicos en KR, y eso es deliberado —hace falta una escritura muy
    // concreta para tocarlo, así que un puntero desbocado no lo desarma
    // [IR, §12.11].
    static constexpr uint32_t KEY_RELOAD = 0xAAAAu;
    static constexpr uint32_t KEY_ACCESS = 0x5555u;
    static constexpr uint32_t KEY_START  = 0xCCCCu;

    Iwdg(sc_core::sc_module_name nm) : BusSlave(nm, addr::IWDG_B, 0x400) {
        SC_HAS_PROCESS(Iwdg);
        SC_THREAD(count_proc);
        SC_METHOD(pub_proc);    sensitive << pub_ev_;
        SC_METHOD(reset_proc);  sensitive << rst_n;
        SC_METHOD(start_proc);  sensitive << hw_start; dont_initialize();
        SC_METHOD(freeze_proc); sensitive << freeze;   dont_initialize();
        SC_METHOD(clk_proc);    sensitive << lsi_hz;   dont_initialize();
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    bool     running() const { return run_; }
    double   timeout_s() const { return double(rlr_ + 1u) * tick_period(); }
    unsigned counter() const { return cnt_now(); }

protected:
    uint32_t pr_ = 0, rlr_ = 0x0FFFu, sr_ = 0;
    unsigned cnt0_ = 0x0FFFu;
    sc_core::sc_time t_ref_{sc_core::SC_ZERO_TIME};
    sc_core::sc_time t_sync_{sc_core::SC_ZERO_TIME};
    bool run_ = false, unlocked_ = false, frozen_ = false, o_rst_ = false;
    sc_core::sc_event ev_, pub_ev_;

    // t_IWDG = t_LSI * 4 * 2^PR por cuenta [IR, §12.11]
    double tick_period() const {
        const double f = lsi_hz.read();
        if (f <= 0.0) return 0.0;
        const unsigned pr = (pr_ > 6u) ? 6u : pr_;      // 110 y 111 son /256
        return 4.0 * double(1u << pr) / f;
    }
    unsigned cnt_now() const {
        if (!run_ || frozen_) return cnt0_;
        const double p = tick_period();
        if (p <= 0.0) return cnt0_;
        const double dt = (sc_core::sc_time_stamp() - t_ref_).to_seconds();
        const unsigned n = unsigned(dt / p + GUARDA_TICK);
        return (n >= cnt0_) ? 0u : (cnt0_ - n);
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_KR:  return 0;                        // solo escritura
            case R_PR:  return pr_;
            case R_RLR: return rlr_;
            case R_SR:  return sr_;
            default:    return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_KR: {
                const uint32_t k = v & 0xFFFFu;
                if (k == KEY_ACCESS) { unlocked_ = true; return; }
                unlocked_ = false;                        // cualquier otra cierra
                if (k == KEY_START)  { run_ = true; publish(); reload(); }
                else if (k == KEY_RELOAD) reload();
                return;
            }
            case R_PR:
                // PR y RLR solo se dejan tocar tras la llave 0x5555, y el
                // cambio tarda unos ciclos del LSI en cruzar al otro dominio:
                // eso es lo que anuncian PVU y RVU [IR, §12.11].
                if (!unlocked_) return;
                pr_ = v & 7u;
                sr_ |= SR_PVU;
                resync();
                return;
            case R_RLR:
                if (!unlocked_) return;
                rlr_ = v & 0x0FFFu;
                sr_ |= SR_RVU;
                resync();
                return;
            default: return;                              // SR es de solo lectura
        }
    }
    unsigned access_cycles(bool) const override { return 1; }

    void reload() {
        cnt0_ = rlr_;
        t_ref_ = sc_core::sc_time_stamp();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void resync() {
        t_sync_ = sc_core::sc_time_stamp();
        // El contador sigue con lo que ya tenía; el valor nuevo entra al
        // recargar. Se reprograma la cita porque el periodo puede haber
        // cambiado.
        cnt0_ = cnt_now();
        t_ref_ = sc_core::sc_time_stamp();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() { rst_req.write(o_rst_); lsi_on_req.write(run_); }
    void clk_proc()   { cnt0_ = cnt_now(); t_ref_ = sc_core::sc_time_stamp();
                        ev_.notify(sc_core::SC_ZERO_TIME); }
    void start_proc() { if (hw_start.read()) { run_ = true; publish(); reload(); } }
    void freeze_proc() {
        if (freeze.read()) { cnt0_ = cnt_now(); frozen_ = true; }
        else { frozen_ = false; t_ref_ = sc_core::sc_time_stamp(); }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void count_proc() {
        for (;;) {
            if (o_rst_) {
                wait(sc_core::sc_time(4, sc_core::SC_US));
                o_rst_ = false; publish();
                continue;
            }
            const double p = tick_period();
            // La sincronización de dominio ocurre AUNQUE EL PERRO ESTÉ PARADO:
            // el valor nuevo tiene que cruzar al dominio del LSI igual, y PVU y
            // RVU se bajan solas unas cuentas después [IR, §12.11].
            if ((sr_ & (SR_PVU | SR_RVU)) && p > 0.0) {
                // Se espera hasta el PLAZO, no "tres cuentas desde que me
                // despierten": si entre medias llega otra escritura, el hilo se
                // despierta antes y las banderas no deben caerse por eso.
                const sc_core::sc_time fin =
                    t_sync_ + sc_core::sc_time(3.0 * p, sc_core::SC_SEC);
                if (sc_core::sc_time_stamp() < fin) {
                    wait(fin - sc_core::sc_time_stamp(), ev_);
                    continue;
                }
                sr_ &= ~(SR_PVU | SR_RVU);
                continue;
            }
            if (!run_ || frozen_ || p <= 0.0) { wait(ev_); continue; }
            const unsigned c = cnt_now();
            if (c > 0) {
                // Una sola cita: cuándo llegará a cero. Nada de contar flancos,
                // y por eso el perro sigue vivo aunque se apague la onda
                // cuadrada de los relojes internos.
                wait(sc_core::sc_time(double(c) * p, sc_core::SC_SEC), ev_);
                continue;
            }
            o_rst_ = true;                       // se acabó el plazo
            publish();
        }
    }
    void reset_proc() {
        if (rst_n.read()) return;
        // OJO: el reset de sistema NO para al perro independiente. En el
        // silicio, una vez arrancado con la llave 0xCCCC solo lo detiene un
        // reset de alimentación, y esa es justo su gracia: si el firmware se
        // reinicia en bucle, el IWDG sigue contando. Aquí se conserva el estado
        // de marcha y solo se recarga el contador [IR, §12.11].
        sr_ = 0; unlocked_ = false;
        o_rst_ = false;
        cnt0_ = rlr_; t_ref_ = sc_core::sc_time_stamp();
        publish();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_WATCHDOG_H
