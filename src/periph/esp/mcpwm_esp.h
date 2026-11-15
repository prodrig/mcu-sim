// =============================================================================
// mcpwm_esp.h — El PWM para motores (MCPWM) del ESP32-C6: un grupo
//
// E11 de doc/analisis_esp32_riscv.md [TRM-C6 §35; mcpwm_reg.h y mcpwm_ll.h de
// ESP-IDF]. Lo que hace un grupo:
//
//   * EL RELOJ: el del PCR (PCR_PWM_CLK_CONF: 1 PLL_F160M, 2 XTAL, 3
//     RC_FAST, entre DIV_NUM + 1; `hz_pcr`), entre CLK_CFG.PRESCALE + 1: el
//     reloj del grupo. Cada temporizador lo divide otra vez (TIMERn_PRESCALE);
//     el temporizador de captura cuenta con el del grupo;
//   * TRES TEMPORIZADORES de 16 bits (0x04 + 0x10·n): PERIOD con su sombra
//     (PERIOD_UPMETHOD: 0 en el acto, bit 0 en TEZ, bit 1 en la sincronía),
//     el modo (MOD: 0 quieto, 1 sube, 2 baja, 3 sube y baja) y el arranque
//     (START: 0 y 1 paran en el siguiente TEZ o TEP, 2 libre, 3 y 4 arrancan
//     y paran en el siguiente TEZ o TEP). TEZ es la cuenta a 0 y TEP la
//     cuenta en PERIOD. Subiendo, un periodo es PERIOD + 1 cuentas; subiendo
//     y bajando, 2·PERIOD;
//   * LA SINCRONÍA: cada temporizador elige su entrada (TIMER_SYNCI_CFG:
//     1-3 la salida de sincronía de otro temporizador, 4-6 las entradas
//     SYNC0-2 de la matriz -señales 87-89, con su inversión-) y, con
//     SYNCI_EN, al llegar carga PHASE y PHASE_DIRECTION. Su salida
//     (SYNCO_SEL): 0 la entrada que le llega, 1 TEZ, 2 TEP, 3 nada. SYNC_SW
//     (al cambiar) es una sincronía por software del propio temporizador;
//   * TRES OPERADORES (0x3C + 0x38·n), cada uno con su temporizador
//     (OPERATOR_TIMERSEL): dos comparadores (TSTMP_A/B, con sombra: bit 0
//     TEZ, bit 1 TEP, bit 2 sincronía, bit 3 nunca), y dos generadores, A y
//     B, con sus ACCIONES (GENn_A/B: 2 bits por suceso y sentido -UTEZ, UTEP,
//     UTEA, UTEB, UT0, UT1 y los D al bajar-: 1 baja, 2 sube, 3 invierte).
//     Si coinciden varios sucesos, gana T1, luego T0, TEB, TEA, TEP y TEZ. T0
//     y T1 son un fallo (GEN_CFG0.T0/T1_SEL 0-2) o la sincronía del
//     temporizador (3). El forzado: continuo (CNTUFORCE_MODE: 1 bajo, 2
//     alto; con su método de actualización) y de una vez (NCIFORCE al
//     cambiar, con NCIFORCE_MODE);
//   * EL TIEMPO MUERTO (DTn_CFG, FED y RED: el retraso es el registro + 1
//     ciclos): RED retrasa los flancos de subida de su entrada (RED_INSEL: A o B) y FED los de bajada (FED_INSEL;
//     con DEB_MODE, la salida de RED); cada uno se puede invertir
//     (RED/FED_OUTINVERT). Sin BYPASS, A sale de RED y B de FED; OUTSWAP las
//     cruza. El reloj del retraso: el del grupo (CLK_SEL 0) o el del
//     temporizador (1). Un pulso más corto que el retraso desaparece;
//   * LOS FALLOS: tres entradas (F0-2, señales 90-92) con su polaridad
//     (FAULT_DETECT); cada operador (FHn_CFG0) elige cuáles le paran del
//     todo (OST, hasta CLR_OST) o ciclo a ciclo (CBC: se mira otra vez en TEZ
//     o TEP, según CBCPULSE), y lo que hace entonces cada salida, subiendo y
//     bajando (1 baja, 2 sube, 3 invierte). FORCE_CBC y FORCE_OST, por
//     software (con SW_CBC y SW_OST). FHn_STATUS: CBC_ON y OST_ON están a 1
//     mientras dura la parada Y su fallo sigue activo (así lo lee
//     mcpwm_operator_recover_from_fault antes de CLR_OST);
//   * LA CAPTURA: un temporizador de 32 bits (CAP_TIMER_CFG, con su
//     sincronía y CAP_PHASE) y tres canales (CAP_CHn_CFG; entradas 93-95):
//     en el flanco que diga MODE (bit 0 bajada, bit 1 subida), cada PRESCALE
//     + 1 flancos, copia la cuenta en CAP_CHn y el flanco en CAP_STATUS. SW,
//     una captura por software;
//   * UPDATE_CFG: GLOBAL_UP_EN y OPn_UP_EN dejan pasar las sombras; los
//     FORCE_UP (al cambiar), ya;
//   * las 30 interrupciones (INT_RAW, en el orden de mcpwm_ll.h), a la
//     fuente 61; y el ETM (E11): los sucesos de EVT_EN (MCPWM_EVT_*, 58 + el
//     bit) y las tareas de TASK_EN (MCPWM_TASK_*, 102 + el bit).
//
// LO QUE NO ESTÁ: el troceador (CARRIERn_CFG): con CHOPPER_EN la salida pasa
// sin trocear, y se dice una vez; y la tarea GEN_STOP del ETM.
// =============================================================================
#ifndef STM32_PERIPH_ESP_MCPWM_ESP_H
#define STM32_PERIPH_ESP_MCPWM_ESP_H

#include <systemc>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class McpwmEsp : public BusSlave {
public:
    std::function<void(bool)> irq;
    // Las salidas: (operador, generador) -> PWMn_OUTA/B (señal 87 + 2·op + g)
    std::function<void(unsigned op, unsigned g, bool v)> salida;
    // Las entradas de la matriz: 0-2 SYNC, 3-5 FAULT, 6-8 CAP
    std::function<const sc_core::sc_signal_in_if<bool>&(unsigned)> entrada;
    // El reloj del PCR (ya dividido por DIV_NUM + 1)
    std::function<double()> hz_pcr;
    // El ETM: un suceso que sale
    std::function<void(unsigned id)> etm;
    uint32_t date = 0x2201240u;

    explicit McpwmEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(McpwmEsp);
        for (unsigned i = 0; i < 3; ++i) {
            sc_core::sc_spawn([this, i] { tim_proc(i); });
            sc_core::sc_spawn([this, i] { dt_proc(i); });
        }
        reset_regs();
    }
    void end_of_elaboration() override {
        if (!entrada) return;
        for (unsigned k = 0; k < 9; ++k) sc_core::sc_spawn([this, k] { entrada_proc(k); });
    }
    void reset() {
        reset_regs();
        for (unsigned i = 0; i < 3; ++i) { tim_[i].ev.notify(sc_core::SC_ZERO_TIME); salida_op(i); }
        publica();
    }

    // --- Lo que el banco quiere saber -------------------------------------------
    uint32_t valor(unsigned t) const { return t < 3 ? actual(t).v : 0; }
    bool corre(unsigned t) const { return t < 3 && tim_[t].corre; }
    bool nivel(unsigned op, unsigned g) const { return op < 3 && g < 2 && op_[op].out[g]; }
    uint64_t sucesos_etm() const { return n_etm_; }
    uint32_t captura(unsigned c) const { return c < 3 ? cap_val_[c] : 0; }

    // --- El ETM: una tarea que llega (MCPWM_TASK_*, el número del bit) ----------
    void tarea_etm(unsigned bit) {
        if (!((task_en_ >> bit) & 1u)) return;
        if (bit < 6) { Op& o = op_[bit % 3]; actualiza(bit < 3 ? o.cmp[0] : o.cmp[1]); salida_op(bit % 3); return; }
        if (bit == 6) return;                                         // GEN_STOP: sin modelar
        if (bit < 10) { sincroniza(bit - 7, 0); return; }
        if (bit < 13) { Tim& t = tim_[bit - 10]; rebasa(bit - 10); actualiza(t.per); t.ev.notify(sc_core::SC_ZERO_TIME); return; }
        if (bit < 16) { dispara_tz(bit - 13, true, true); return; }
        if (bit < 19) { quita_ost(bit - 16); return; }
        if (bit < 22) { captura_ya(bit - 19, true); return; }
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == 0x00) return clk_cfg_;
        if (off >= 0x04 && off < 0x34) {
            const unsigned i = (off - 0x04) / 0x10;
            const Tim& t = tim_[i];
            switch ((off - 0x04) % 0x10) {
            case 0x0: return (t.cfg0 & 0xFFu) | ((t.per.sh & 0xFFFFu) << 8) | (t.cfg0 & (3u << 24));
            case 0x4: return t.cfg1;
            case 0x8: return t.sync;
            default: { const Estado e = actual(i); return e.v | (e.baja ? 1u << 16 : 0u); }
            }
        }
        if (off == 0x34) return synci_cfg_;
        if (off == 0x38) return timersel_;
        if (off >= 0x3C && off < 0x3C + 3 * 0x38) {
            const Op& o = op_[(off - 0x3C) / 0x38];
            switch ((off - 0x3C) % 0x38) {
            case 0x00: return o.stmp_cfg | (o.cmp[0].full ? 1u << 8 : 0u) | (o.cmp[1].full ? 1u << 9 : 0u);
            case 0x04: return o.cmp[0].sh;
            case 0x08: return o.cmp[1].sh;
            case 0x0C: return o.gen_cfg0;
            case 0x10: return o.force;
            case 0x14: return o.gen[0].sh;
            case 0x18: return o.gen[1].sh;
            case 0x1C: return o.dt_cfg;
            case 0x20: return o.fed.sh;
            case 0x24: return o.red.sh;
            case 0x28: return o.carrier;
            case 0x2C: return o.fh0;
            case 0x30: return o.fh1;
            // CBC_ON y OST_ON: la parada sigue y su fallo también (ESP-IDF los
            // lee para saber si el fallo se ha ido antes de CLR_OST)
            case 0x34: { const unsigned n = (off - 0x3C) / 0x38;
                         return (o.cbc && cbc_activo(n) ? 1u : 0u) | (o.ost && ost_activo(n) ? 2u : 0u); }
            }
        }
        if (off >= 0xF0 && off < 0xFC) return cap_cfg_[(off - 0xF0) / 4];
        if (off >= 0xFC && off < 0x108) return cap_val_[(off - 0xFC) / 4];
        switch (off) {
        case 0xE4: return (fault_det_ & 0x3Fu) | (fev_ << 6);
        case 0xE8: return cap_timer_cfg_;
        case 0xEC: return cap_phase_;
        case 0x108: return cap_status_;
        case 0x10C: return update_cfg_;
        case 0x110: return ena_;
        case 0x114: return raw_;
        case 0x118: return raw_ & ena_;
        case 0x120: return evt_en_;
        case 0x124: return task_en_;
        case 0x128: return clk_;
        case 0x12C: return date;
        default: return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        auto w = [&](uint32_t& r) { const uint32_t a = r; r = (r & ~m) | (v & m); return a; };
        if (off == 0x00) { for (unsigned i = 0; i < 3; ++i) rebasa(i); w(clk_cfg_); despierta(); return; }
        if (off >= 0x04 && off < 0x34) {
            const unsigned i = (off - 0x04) / 0x10;
            Tim& t = tim_[i];
            rebasa(i);
            switch ((off - 0x04) % 0x10) {
            case 0x0: {
                uint32_t c = (t.cfg0 & 0x30000FFu) | ((t.per.sh & 0xFFFFu) << 8);
                c = (c & ~m) | (v & m);
                t.cfg0 = c & 0x30000FFu;
                escribe(t.per, (c >> 8) & 0xFFFFu, (c >> 24) & 3u ? 1u : 0u, true);
                break;
            }
            case 0x4: {
                w(t.cfg1);
                t.cfg1 &= 0x1Fu;
                const unsigned st = t.cfg1 & 7u;
                if (!t.corre && st >= 2 && st <= 4 && ((t.cfg1 >> 3) & 3u)) { t.corre = true; t.t_v = ahora(); }
                break;
            }
            case 0x8: {
                const uint32_t a = w(t.sync);
                t.sync &= 0x1FFFFFu;
                if ((a ^ t.sync) & 2u) sincroniza(i, 0, true);            // SYNC_SW, al cambiar
                break;
            }
            default: break;
            }
            t.ev.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        if (off == 0x34) { w(synci_cfg_); synci_cfg_ &= 0xFFFu; return; }
        if (off == 0x38) { for (unsigned i = 0; i < 3; ++i) rebasa(i); w(timersel_); timersel_ &= 0x3Fu; despierta(); return; }
        if (off >= 0x3C && off < 0x3C + 3 * 0x38) {
            const unsigned n = (off - 0x3C) / 0x38;
            Op& o = op_[n];
            rebasa(timer_de(n));
            switch ((off - 0x3C) % 0x38) {
            case 0x00: w(o.stmp_cfg); o.stmp_cfg &= 0xFFu; break;
            case 0x04: escribe(o.cmp[0], v & 0xFFFFu, o.stmp_cfg & 0xFu, puede(n)); break;
            case 0x08: escribe(o.cmp[1], v & 0xFFFFu, (o.stmp_cfg >> 4) & 0xFu, puede(n)); break;
            case 0x0C: w(o.gen_cfg0); o.gen_cfg0 &= 0x3FFu; break;
            case 0x10: {
                const uint32_t a = w(o.force);
                o.force &= 0xFFFFu;
                o.cntu.sh = (o.force >> 6) & 0xFu;
                if ((o.force & 0x3Fu) == 0) { o.cntu.act = o.cntu.sh; }
                else o.cntu.full = true;
                for (unsigned g = 0; g < 2; ++g)                          // NCIFORCE, al cambiar
                    if (((a ^ o.force) >> (10 + 3 * g)) & 1u) {
                        const unsigned md = (o.force >> (11 + 3 * g)) & 3u;
                        if (md == 1) o.gen_v[g] = false;
                        else if (md == 2) o.gen_v[g] = true;
                    }
                break;
            }
            case 0x14: escribe(o.gen[0], v & 0xFFFFFFu, o.gen_cfg0 & 0xFu, puede(n)); break;
            case 0x18: escribe(o.gen[1], v & 0xFFFFFFu, o.gen_cfg0 & 0xFu, puede(n)); break;
            case 0x1C: w(o.dt_cfg); o.dt_cfg &= 0x3FFFFu; break;
            case 0x20: escribe(o.fed, v & 0xFFFFu, o.dt_cfg & 0xFu, puede(n)); break;
            case 0x24: escribe(o.red, v & 0xFFFFu, (o.dt_cfg >> 4) & 0xFu, puede(n)); break;
            case 0x28:
                w(o.carrier); o.carrier &= 0x3FFFu;
                if ((o.carrier & 1u) && !aviso_chopper_) {
                    aviso_chopper_ = true;
                    std::printf("[%s] AVISO: el troceador (CARRIER) no está modelado: la salida sale sin trocear\n", name());
                }
                break;
            case 0x2C: w(o.fh0); o.fh0 &= 0xFFFFFFu; mira_fallos(); break;
            case 0x30: {
                const uint32_t a = w(o.fh1);
                o.fh1 &= 0x1Fu;
                if (!(a & 1u) && (o.fh1 & 1u)) quita_ost(n);                         // CLR_OST, al subir
                if (((a ^ o.fh1) >> 3) & 1u) { if (o.fh0 & 1u) dispara_tz(n, false, false); }   // FORCE_CBC
                if (((a ^ o.fh1) >> 4) & 1u) { if ((o.fh0 >> 4) & 1u) dispara_tz(n, true, false); } // FORCE_OST
                break;
            }
            default: break;
            }
            salida_op(n);
            tim_[timer_de(n)].ev.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        if (off >= 0xF0 && off < 0xFC) {
            const unsigned c = (off - 0xF0) / 4;
            w(cap_cfg_[c]);
            if ((cap_cfg_[c] >> 12) & 1u) captura_ya(c, true);
            cap_cfg_[c] &= 0xFFFu;
            return;
        }
        switch (off) {
        case 0xE4: w(fault_det_); fault_det_ &= 0x3Fu; mira_fallos(); return;
        case 0xE8: {
            const uint32_t a = cap_timer_cfg_;
            const uint32_t c = cuenta_cap();
            cap_base_ = c; cap_t_ = ahora();
            w(cap_timer_cfg_);
            if ((cap_timer_cfg_ >> 5) & 1u) { cap_base_ = cap_phase_; cap_t_ = ahora(); }
            cap_timer_cfg_ &= 0x1Fu;
            (void)a;
            return;
        }
        case 0xEC: w(cap_phase_); return;
        case 0x10C: {
            const uint32_t a = w(update_cfg_);
            update_cfg_ &= 0xFFu;
            if (((a ^ update_cfg_) >> 1) & 1u) {                                     // GLOBAL_FORCE_UP
                for (unsigned i = 0; i < 3; ++i) { rebasa(i); actualiza(tim_[i].per); tim_[i].ev.notify(sc_core::SC_ZERO_TIME); }
                for (unsigned n = 0; n < 3; ++n) actualiza_op(n, ~0u);
            }
            for (unsigned n = 0; n < 3; ++n)
                if (((a ^ update_cfg_) >> (3 + 2 * n)) & 1u) actualiza_op(n, ~0u);   // OPn_FORCE_UP
            return;
        }
        case 0x110: w(ena_); ena_ &= 0x3FFFFFFFu; publica(); return;
        case 0x11C: raw_ &= ~(v & m); publica(); return;
        case 0x120: w(evt_en_); return;
        case 0x124: w(task_en_); return;
        case 0x128: w(clk_); return;
        case 0x12C: date = v; return;
        default: return;
        }
    }

private:
    // Un registro con sombra
    struct Sombra { uint32_t act = 0, sh = 0; bool full = false; };
    struct Tim {
        uint32_t cfg0 = 0, cfg1 = 0, sync = 0;
        Sombra per;
        bool corre = false, baja = false;
        uint32_t v = 0;
        sc_core::sc_time t_v;
        sc_core::sc_event ev;
    };
    struct Op {
        uint32_t stmp_cfg = 0, gen_cfg0 = 0, force = 0x20u, dt_cfg = 0x18000u, carrier = 0, fh0 = 0, fh1 = 0;
        Sombra cmp[2], gen[2], fed, red, cntu;
        bool gen_v[2] = {}, out[2] = {}, cbc = false, ost = false;
        // El tiempo muerto
        bool red_out = false, fed_out = false, x_r = false, x_f = false, fed_vio = false;
        sc_core::sc_time t_sube_r, t_baja_f;
        sc_core::sc_event ev_dt;
    };
    struct Estado { uint32_t v; bool baja; };

    Tim tim_[3];
    Op op_[3];
    uint32_t clk_cfg_ = 0, synci_cfg_ = 0, timersel_ = 0, fault_det_ = 0, fev_ = 0, cap_timer_cfg_ = 0,
             cap_phase_ = 0, cap_cfg_[3] = {}, cap_val_[3] = {}, cap_status_ = 0, cap_pre_[3] = {},
             update_cfg_ = 0x55u, ena_ = 0, raw_ = 0, evt_en_ = 0, task_en_ = 0, clk_ = 0;
    uint32_t cap_base_ = 0;
    sc_core::sc_time cap_t_;
    bool irq_ = false, aviso_chopper_ = false;
    bool ent_[9] = {};
    unsigned prof_sync_ = 0;
    uint64_t n_etm_ = 0;

    static sc_core::sc_time ahora() { return sc_core::sc_time_stamp(); }

    void reset_regs() {
        for (Tim& t : tim_) {
            t.cfg0 = 0; t.cfg1 = 0; t.sync = 0;
            t.per = Sombra(); t.per.act = t.per.sh = 0xFFu;
            t.corre = false; t.baja = false; t.v = 0; t.t_v = ahora();
        }
        for (Op& o : op_) {
            o.stmp_cfg = 0; o.gen_cfg0 = 0; o.force = 0x20u; o.dt_cfg = 0x18000u; o.carrier = 0; o.fh0 = o.fh1 = 0;
            for (Sombra* s : {&o.cmp[0], &o.cmp[1], &o.gen[0], &o.gen[1], &o.fed, &o.red, &o.cntu}) *s = Sombra();
            o.gen_v[0] = o.gen_v[1] = false; o.cbc = o.ost = false;
            o.red_out = o.fed_out = o.x_r = o.x_f = false;
            o.t_sube_r = o.t_baja_f = ahora();
            o.ev_dt.notify(sc_core::SC_ZERO_TIME);
        }
        clk_cfg_ = synci_cfg_ = timersel_ = fault_det_ = fev_ = cap_timer_cfg_ = cap_phase_ = 0;
        for (unsigned c = 0; c < 3; ++c) cap_cfg_[c] = cap_val_[c] = cap_pre_[c] = 0;
        cap_status_ = 0; update_cfg_ = 0x55u; ena_ = raw_ = evt_en_ = task_en_ = clk_ = 0;
        cap_base_ = 0; cap_t_ = ahora();
    }
    void publica() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    // Una interrupción (su bit) y el suceso del ETM que va con ella, si lo hay
    void suceso(unsigned bit_int, int bit_etm = -1) {
        raw_ |= 1u << bit_int;
        publica();
        if (bit_etm >= 0 && ((evt_en_ >> bit_etm) & 1u) && etm) { ++n_etm_; etm(58u + unsigned(bit_etm)); }
    }

    // --- Relojes ----------------------------------------------------------------
    double hz_grupo() const {
        const double f = hz_pcr ? hz_pcr() : 160e6;
        return f / double((clk_cfg_ & 0xFFu) + 1u);
    }
    double hz_tim(unsigned i) const { return hz_grupo() / double((tim_[i].cfg0 & 0xFFu) + 1u); }
    unsigned timer_de(unsigned op) const { return (timersel_ >> (2 * op)) & 3u; }

    // --- Las sombras ------------------------------------------------------------
    bool puede(unsigned op) const { return (update_cfg_ & 1u) && ((update_cfg_ >> (2 + 2 * op)) & 1u); }
    // Escribir: con método 0 (y las actualizaciones permitidas), en el acto
    void escribe(Sombra& s, uint32_t v, unsigned metodo, bool permitido) {
        s.sh = v;
        if (metodo == 0 && permitido) { s.act = v; s.full = false; }
        else s.full = true;
    }
    static void actualiza(Sombra& s) { if (s.full) { s.act = s.sh; s.full = false; } }
    // Las sombras de un operador en un suceso: `que` 1 TEZ, 2 TEP, 4 sincronía
    void actualiza_op(unsigned n, unsigned que) {
        Op& o = op_[n];
        const bool forzado = que == ~0u;
        if (!forzado && !puede(n)) return;
        auto toca = [&](unsigned metodo) { return forzado || (!((metodo >> 3) & 1u) && (metodo & que)); };
        if (toca(o.stmp_cfg & 0xFu)) actualiza(o.cmp[0]);
        if (toca((o.stmp_cfg >> 4) & 0xFu)) actualiza(o.cmp[1]);
        if (toca(o.gen_cfg0 & 0xFu)) { actualiza(o.gen[0]); actualiza(o.gen[1]); }
        if (toca(o.dt_cfg & 0xFu)) actualiza(o.fed);
        if (toca((o.dt_cfg >> 4) & 0xFu)) actualiza(o.red);
        // El forzado continuo: bits 0 TEZ, 1 TEP (2 TEA, 3 TEB, 4 sincronía), 5 nunca
        const unsigned mf = o.force & 0x3Fu;
        if (forzado || (!(mf & 0x20u) && ((que & 1u && mf & 1u) || (que & 2u && mf & 2u) || (que & 4u && mf & 0x10u))))
            actualiza(o.cntu);
        if (forzado) salida_op(n);
    }

    // --- El temporizador --------------------------------------------------------
    unsigned modo(unsigned i) const { return (tim_[i].cfg1 >> 3) & 3u; }
    // Avanzar k cuentas desde (v, baja)
    Estado avanza(unsigned i, uint32_t v, bool baja, uint64_t k) const {
        const uint32_t P = tim_[i].per.act;
        switch (modo(i)) {
        case 1: { const uint64_t L = uint64_t(P) + 1; return {uint32_t((std::min(v, P) + k) % L), false}; }
        case 2: { const uint64_t L = uint64_t(P) + 1; const uint64_t x = (std::min(v, P) + L - (k % L)) % L; return {uint32_t(x), true}; }
        case 3: {
            if (P == 0) return {0, false};
            const uint64_t L = 2ull * P;
            const uint64_t pos = baja ? L - std::min(v, P) : std::min(v, P);
            const uint64_t q = (pos + k) % L;
            return q < P ? Estado{uint32_t(q), false} : Estado{uint32_t(L - q), true};
        }
        default: return {v, baja};
        }
    }
    uint64_t cuentas_hasta(unsigned i, const sc_core::sc_time& t) const {
        const Tim& x = tim_[i];
        if (!x.corre || modo(i) == 0 || t <= x.t_v) return 0;
        const double n = (t - x.t_v).to_seconds() * hz_tim(i);
        return uint64_t(std::floor(n + 1e-6));
    }
    Estado actual(unsigned i) const { const Tim& x = tim_[i]; return avanza(i, x.v, x.baja, cuentas_hasta(i, ahora())); }
    // Antes de cambiar algo que mueva el tiempo: la cuenta de ahora (la de la
    // cuenta anterior si justo toca una, para que su suceso no se pierda)
    void rebasa(unsigned i) {
        Tim& x = tim_[i];
        if (!x.corre || modo(i) == 0) { x.t_v = ahora(); return; }
        const double f = hz_tim(i);
        if (f <= 0) { x.t_v = ahora(); return; }
        const double n = (ahora() - x.t_v).to_seconds() * f;
        uint64_t k = uint64_t(std::max(0.0, std::ceil(n - 1e-6) - 1.0));
        if (k == 0) return;
        const Estado e = avanza(i, x.v, x.baja, k);
        x.v = e.v; x.baja = e.baja;
        x.t_v = x.t_v + sc_core::sc_time(double(k) / f, sc_core::SC_SEC);
    }
    void despierta() { for (Tim& t : tim_) t.ev.notify(sc_core::SC_ZERO_TIME); }

    // El siguiente valor «interesante» y cuántas cuentas faltan
    void siguiente(unsigned i, uint32_t& x, uint64_t& k) const {
        const Tim& t = tim_[i];
        const uint32_t P = t.per.act, v = t.v;
        uint32_t cand[8]; unsigned nc = 0;
        cand[nc++] = 0; cand[nc++] = P;
        for (unsigned n = 0; n < 3; ++n)
            if (timer_de(n) == i) { cand[nc++] = op_[n].cmp[0].act; cand[nc++] = op_[n].cmp[1].act; }
        auto arriba = [&](uint32_t desde, uint32_t& r) {     // el menor > desde, <= P
            bool hay = false;
            for (unsigned c = 0; c < nc; ++c) if (cand[c] > desde && cand[c] <= P && (!hay || cand[c] < r)) { r = cand[c]; hay = true; }
            return hay;
        };
        auto abajo = [&](uint32_t desde, uint32_t& r) {      // el mayor < desde
            bool hay = false;
            for (unsigned c = 0; c < nc; ++c) if (cand[c] < desde && (!hay || cand[c] > r)) { r = cand[c]; hay = true; }
            return hay;
        };
        switch (modo(i)) {
        case 1:
            if (v < P && arriba(v, x)) { k = x - v; return; }
            x = 0; k = 1; return;
        case 2:
            if (v > 0 && v <= P && abajo(v, x)) { k = v - x; return; }
            x = P; k = v > P ? 1 : (v == 0 ? 1 : v); return;
        default:
            if (P == 0) { x = 0; k = 1; return; }
            if (!t.baja && v < P) { arriba(v, x); k = x - v; return; }
            if (!t.baja) { abajo(P, x); k = P - x; return; }           // en P, a bajar
            if (v > 0 && abajo(v, x)) { k = v - x; return; }
            arriba(0, x); k = x; return;                               // en 0, a subir
        }
    }

    void tim_proc(unsigned i) {
        Tim& t = tim_[i];
        for (;;) {
            const double f = hz_tim(i);
            if (!t.corre || modo(i) == 0 || f <= 0) { wait(t.ev); t.t_v = ahora(); continue; }
            uint32_t x = 0; uint64_t k = 1;
            siguiente(i, x, k);
            const sc_core::sc_time obj = t.t_v + sc_core::sc_time(double(k) / f, sc_core::SC_SEC);
            if (obj > ahora()) {
                wait(obj - ahora(), t.ev);
                if (ahora() < obj) continue;                         // ha cambiado algo: otra vez
            }
            const Estado e = avanza(i, t.v, t.baja, k);
            t.v = e.v; t.baja = e.baja; t.t_v = obj;
            if (modo(i) == 3) { if (t.v == 0) t.baja = false; else if (t.v == t.per.act) t.baja = true; }
            procesa(i);
        }
    }

    // Los sucesos de un temporizador en su valor de ahora
    void procesa(unsigned i) {
        Tim& t = tim_[i];
        const uint32_t P0 = t.per.act;
        const bool tez = t.v == 0, tep = t.v == P0;
        const bool ud = modo(i) == 3;
        // La dirección de los sucesos: subiendo y bajando, TEZ es de subida y TEP de bajada
        bool baja = modo(i) == 2 || (ud && t.baja);
        if (ud && tez) baja = false;
        if (ud && tep) baja = true;
        if (tez || tep) {
            const uint32_t pm = (t.cfg0 >> 24) & 3u;
            if (tez && (pm & 1u) && (update_cfg_ & 1u)) actualiza(t.per);
            for (unsigned n = 0; n < 3; ++n) if (timer_de(n) == i) actualiza_op(n, (tez ? 1u : 0u) | (tep ? 2u : 0u));
        }
        if (tez) suceso(3 + i, int(3 + i));
        if (tep) suceso(6 + i, int(6 + i));
        const unsigned so = (t.sync >> 2) & 3u;
        if ((so == 1 && tez) || (so == 2 && tep)) dispara_sync(1 + i);
        for (unsigned n = 0; n < 3; ++n) {
            if (timer_de(n) != i) continue;
            Op& o = op_[n];
            const bool tea = t.v == o.cmp[0].act, teb = t.v == o.cmp[1].act;
            // El forzado continuo en TEA/TEB
            const unsigned mf = o.force & 0x3Fu;
            if (!(mf & 0x20u) && ((tea && (mf & 4u)) || (teb && (mf & 8u))) && puede(n)) actualiza(o.cntu);
            acciones(n, baja, tez, tep, tea, teb, false, false);
            if (tea) suceso(15 + n, int(9 + n));
            if (teb) suceso(18 + n, int(12 + n));
            // CBC: se mira otra vez en TEZ/TEP
            const unsigned cp = (o.fh1 >> 1) & 3u;
            if (o.cbc && ((tez && (cp & 1u)) || (tep && (cp & 2u))) && !cbc_activo(n)) o.cbc = false;
            salida_op(n);
        }
        // ¿Para?
        const unsigned st = t.cfg1 & 7u;
        if ((tez && (st == 0 || st == 3)) || (tep && (st == 1 || st == 4))) {
            t.corre = false;
            if (st == 3) t.cfg1 &= ~7u;
            if (st == 4) t.cfg1 = (t.cfg1 & ~7u) | 1u;
            suceso(i, int(i));
        }
    }

    // Las acciones de los dos generadores de un operador, por prioridad
    void acciones(unsigned n, bool baja, bool tez, bool tep, bool tea, bool teb, bool t0, bool t1) {
        Op& o = op_[n];
        const bool hay[6] = {tez, tep, tea, teb, t0, t1};
        for (unsigned g = 0; g < 2; ++g) {
            const uint32_t r = o.gen[g].act;
            for (unsigned e = 0; e < 6; ++e) {
                if (!hay[e]) continue;
                const unsigned a = (r >> ((baja ? 12 : 0) + 2 * e)) & 3u;
                if (a == 1) o.gen_v[g] = false;
                else if (a == 2) o.gen_v[g] = true;
                else if (a == 3) o.gen_v[g] = !o.gen_v[g];
            }
        }
    }

    // --- La sincronía -----------------------------------------------------------
    // Llega una sincronía de la fuente `src` (1-3 un temporizador, 4-6 SYNCn)
    void dispara_sync(unsigned src) {
        if (prof_sync_ > 4) return;
        ++prof_sync_;
        for (unsigned i = 0; i < 3; ++i)
            if (((synci_cfg_ >> (3 * i)) & 7u) == src) sincroniza(i, src);
        if ((cap_timer_cfg_ >> 1) & 1u && ((cap_timer_cfg_ >> 2) & 7u) == src) { cap_base_ = cap_phase_; cap_t_ = ahora(); }
        --prof_sync_;
    }
    void sincroniza(unsigned i, unsigned src, bool sw = false) {
        Tim& t = tim_[i];
        (void)src;
        if ((t.sync & 1u) || sw) {
            if (t.sync & 1u) {
                rebasa(i);
                t.v = (t.sync >> 4) & 0xFFFFu;
                t.baja = (t.sync >> 20) & 1u;
                t.t_v = ahora();
                if ((((t.cfg0 >> 24) & 3u) & 2u) && (update_cfg_ & 1u)) actualiza(t.per);
                for (unsigned n = 0; n < 3; ++n) {
                    if (timer_de(n) != i) continue;
                    actualiza_op(n, 4u);
                    const bool t0 = ((op_[n].gen_cfg0 >> 4) & 7u) == 3, t1 = ((op_[n].gen_cfg0 >> 7) & 7u) == 3;
                    if (t0 || t1) { acciones(n, modo(i) == 2 || (modo(i) == 3 && t.baja), false, false, false, false, t0, t1); salida_op(n); }
                }
                t.ev.notify(sc_core::SC_ZERO_TIME);
            }
        }
        if (((t.sync >> 2) & 3u) == 0) dispara_sync(1 + i);
    }

    // --- Los fallos -------------------------------------------------------------
    bool ost_activo(unsigned n) const {
        const uint32_t c = op_[n].fh0;
        for (unsigned f = 0; f < 3; ++f) if (((fev_ >> f) & 1u) && ((c >> (7 - f)) & 1u)) return true;
        return false;
    }
    bool cbc_activo(unsigned n) const {
        const uint32_t c = op_[n].fh0;
        for (unsigned f = 0; f < 3; ++f) if (((fev_ >> f) & 1u) && ((c >> (3 - f)) & 1u)) return true;
        return false;
    }
    void mira_fallos() {
        uint32_t nuevo = 0;
        for (unsigned f = 0; f < 3; ++f)
            if (((fault_det_ >> f) & 1u) && ent_[3 + f] == bool((fault_det_ >> (3 + f)) & 1u)) nuevo |= 1u << f;
        const uint32_t sube = nuevo & ~fev_, baja = ~nuevo & fev_ & 7u;
        fev_ = nuevo;
        for (unsigned f = 0; f < 3; ++f) {
            if ((sube >> f) & 1u) {
                suceso(9 + f, int(15 + f));
                for (unsigned n = 0; n < 3; ++n) {
                    Op& o = op_[n];
                    if ((o.fh0 >> (7 - f)) & 1u) dispara_tz(n, true, false);
                    if ((o.fh0 >> (3 - f)) & 1u) dispara_tz(n, false, false);
                    const bool t0 = ((o.gen_cfg0 >> 4) & 7u) == f, t1 = ((o.gen_cfg0 >> 7) & 7u) == f;
                    if (t0 || t1) {
                        const unsigned i = timer_de(n);
                        acciones(n, modo(i) == 2 || (modo(i) == 3 && tim_[i].baja), false, false, false, false, t0, t1);
                        salida_op(n);
                    }
                }
            }
            if ((baja >> f) & 1u) suceso(12 + f, int(18 + f));
        }
    }
    void dispara_tz(unsigned n, bool ost, bool por_etm) {
        Op& o = op_[n];
        (void)por_etm;
        if (ost) { if (!o.ost) { o.ost = true; suceso(24 + n, int(24 + n)); } }
        else if (!o.cbc) { o.cbc = true; suceso(21 + n, int(21 + n)); }
        salida_op(n);
    }
    void quita_ost(unsigned n) { op_[n].ost = false; salida_op(n); }

    // --- La salida de un operador: generadores, tiempo muerto y fallos ---------
    // Lo que sale de los generadores (con el forzado continuo)
    bool gen_salida(unsigned n, unsigned g) const {
        const Op& o = op_[n];
        const unsigned md = (o.cntu.act >> (2 * g)) & 3u;
        if (md == 1) return false;
        if (md == 2) return true;
        return o.gen_v[g];
    }
    void salida_op(unsigned n) { op_[n].ev_dt.notify(sc_core::SC_ZERO_TIME); }
    double hz_dt(unsigned n) const { return ((op_[n].dt_cfg >> 17) & 1u) ? hz_tim(timer_de(n)) : hz_grupo(); }

    void dt_proc(unsigned n) {
        Op& o = op_[n];
        for (;;) {
            const uint32_t c = o.dt_cfg;
            const bool ga = gen_salida(n, 0), gb = gen_salida(n, 1);
            const double f = hz_dt(n);
            // El retraso es el registro + 1 (mcpwm_ll_deadtime_set_rising_delay escribe ticks - 1)
            const sc_core::sc_time dr = f > 0 ? sc_core::sc_time(double(o.red.act + 1u) / f, sc_core::SC_SEC) : sc_core::SC_ZERO_TIME;
            const sc_core::sc_time df = f > 0 ? sc_core::sc_time(double(o.fed.act + 1u) / f, sc_core::SC_SEC) : sc_core::SC_ZERO_TIME;
            const sc_core::sc_time t = ahora();
            // RED: la subida, con retraso
            const bool xr = ((c >> 11) & 1u) ? gb : ga;
            if (xr && !o.x_r) o.t_sube_r = t;
            o.x_r = xr;
            const bool red = xr && t >= o.t_sube_r + dr;
            o.red_out = red;
            // FED: la bajada, con retraso (con DEB_MODE, detrás de RED)
            const bool xf = ((c >> 8) & 1u) ? red : (((c >> 12) & 1u) ? gb : ga);
            if (!xf && o.x_f) o.t_baja_f = t;
            o.x_f = xf;
            const bool fed = xf || t < o.t_baja_f + df;
            o.fed_out = fed;
            const bool pa = ((c >> 15) & 1u) ? ga : (red != bool((c >> 13) & 1u));
            const bool pb = ((c >> 16) & 1u) ? gb : (fed != bool((c >> 14) & 1u));
            bool out[2] = {((c >> 9) & 1u) ? pb : pa, ((c >> 10) & 1u) ? pa : pb};
            // Los fallos: OST antes que CBC; la acción según el sentido
            if (o.ost || o.cbc) {
                const unsigned i = timer_de(n);
                const bool baja = modo(i) == 2 || (modo(i) == 3 && tim_[i].baja);
                for (unsigned g = 0; g < 2; ++g) {
                    const unsigned sh = 8 + 8 * g + (o.ost ? 4 : 0) + (baja ? 0 : 2);
                    const unsigned a = (o.fh0 >> sh) & 3u;
                    if (a == 1) out[g] = false;
                    else if (a == 2) out[g] = true;
                    else if (a == 3) out[g] = !out[g];
                }
            }
            for (unsigned g = 0; g < 2; ++g)
                if (out[g] != o.out[g]) { o.out[g] = out[g]; if (salida) salida(n, g, out[g]); }
            // ¿Cuándo vuelve a cambiar algo por sí solo?
            sc_core::sc_time prox = sc_core::SC_ZERO_TIME;
            bool hay = false;
            if (xr && !red) { prox = o.t_sube_r + dr - t; hay = true; }
            if (!xf && fed) { const sc_core::sc_time d = o.t_baja_f + df - t; if (!hay || d < prox) prox = d; hay = true; }
            if (hay && prox > sc_core::SC_ZERO_TIME) wait(prox, o.ev_dt);
            else wait(o.ev_dt);
        }
    }

    // --- La captura -------------------------------------------------------------
    uint32_t cuenta_cap() const {
        if (!(cap_timer_cfg_ & 1u)) return cap_base_;
        const double n = (ahora() - cap_t_).to_seconds() * hz_grupo();
        return cap_base_ + uint32_t(uint64_t(std::floor(n + 1e-6)));
    }
    void captura_ya(unsigned c, bool sw, bool sube = true) {
        cap_val_[c] = cuenta_cap();
        if (!sw) cap_status_ = (cap_status_ & ~(1u << c)) | (sube ? 0u : 1u << c);
        suceso(27 + c, int(27 + c));
    }

    // --- Las entradas de la matriz ------------------------------------------------
    void entrada_proc(unsigned k) {
        const sc_core::sc_signal_in_if<bool>& s = entrada(k);
        ent_[k] = s.read();
        for (;;) {
            wait(s.value_changed_event());
            const bool v = s.read();
            if (v == ent_[k]) continue;
            ent_[k] = v;
            if (k < 3) {                                       // SYNCn: el flanco de subida
                const bool inv = (synci_cfg_ >> (9 + k)) & 1u;
                if (v != inv) dispara_sync(4 + k);
            } else if (k < 6) {
                mira_fallos();
            } else {
                const unsigned c = k - 6;
                const uint32_t cf = cap_cfg_[c];
                if (!(cf & 1u)) continue;
                const bool x = v != bool((cf >> 11) & 1u);
                const unsigned md = (cf >> 1) & 3u;
                if (!((x && (md & 2u)) || (!x && (md & 1u)))) continue;
                if (cap_pre_[c]++ < ((cf >> 3) & 0xFFu)) continue;
                cap_pre_[c] = 0;
                captura_ya(c, false, x);
            }
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_MCPWM_ESP_H
