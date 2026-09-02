// =============================================================================
// pwr.h — Controlador de energía (APB1) [IR, §12.21 y §14]
//
// El PWR es el ÁRBITRO DE LA ENERGÍA del modelo. No es un periférico más: es
// quien decide, a partir de dos señales del núcleo (`sleeping` y `sleepdeep`) y
// de dos bits suyos (PDDS y LPDS), en cuál de los cuatro modos está el MCU, y
// quien se lo dice al resto del sistema.
//
//   Run      — todo en marcha.
//   Sleep    — WFI/WFE con SLEEPDEEP = 0. La CPU se para; los relojes NO. El
//              gating de periféricos pasa a mandarlo RCC_xxxLPENR, no ENR.
//   Stop     — SLEEPDEEP = 1, PDDS = 0. Se paran TODOS los relojes del dominio
//              de 1.2 V; HSI, HSE y PLL se apagan. La SRAM y los registros se
//              conservan. Despierta cualquier línea EXTI, y al despertar el
//              reloj de sistema es HSI: hay que reprogramar el PLL.
//   Standby  — SLEEPDEEP = 1, PDDS = 1. Se APAGA el dominio de 1.2 V: se pierde
//              la SRAM y todos los registros salvo los del PWR y el dominio de
//              backup. Los pines quedan en alta impedancia. Despiertan el pin
//              WKUP, el RTC y NRST, y la salida es un reset como el de POR con
//              SBF = 1 [IR, §14.5.3].
//
// Además vive aquí lo que depende del NIVEL REAL de VDD, porque el PWR está en
// el dominio VDD y sigue vivo cuando el de 1.2 V no:
//   * el PVD, con su umbral programable y su histéresis, hacia la línea EXTI16;
//   * el regulador de backup (BRE/BRR), que es lo que mantiene la BKPSRAM;
//   * y el MODELO DE CONSUMO: el PWR calcula la corriente que el MCU pide por
//     VDD y por VBAT según el modo, la frecuencia de HCLK y cuántos relojes de
//     periférico están abiertos. Esa corriente se la entrega a los pines de
//     alimentación (pins/power_pads.h), que la presentan como carga real sobre
//     el nodo analógico. El consumo deja así de ser una nota al pie y pasa a
//     ser una magnitud medible en float, con su caída de tensión incluida.
// =============================================================================
#ifndef STM32_PERIPH_PWR_H
#define STM32_PERIPH_PWR_H

#include "../common/periph_base.h"

namespace stm32 {

class Pwr : public BusSlave {
public:
    // ---- Entradas ----------------------------------------------------------
    sc_core::sc_in<double> vdd_lvl{"vdd_lvl"};        // PowerPads (nivel real)
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};        // para el modelo de consumo
    sc_core::sc_in<double> rtcclk_hz{"rtcclk_hz"};    // ¿RTC vivo en Standby?
    sc_core::sc_in<bool>   sleeping{"sleeping"};      // CPU (WFI/WFE)
    sc_core::sc_in<bool>   sleepdeep{"sleepdeep"};    // SCB.SCR via CPU
    sc_core::sc_in<bool>   wkup_pin{"wkup_pin"};      // PA0 (EWUP)
    sc_core::sc_in<bool>   exti_wakeup{"exti_wakeup"};// EXTI (salida de Stop)
    // Fuentes de despertar del Standby que no pasan por el pin [IR, §14.8]
    sc_core::sc_in<bool>   rtc_alarm{"rtc_alarm"};    // línea EXTI17
    sc_core::sc_in<bool>   rtc_tamper{"rtc_tamper"};  // línea EXTI21
    sc_core::sc_in<bool>   rtc_wkup{"rtc_wkup"};      // línea EXTI22
    // Relojes de periférico abiertos: lo publica el RCC y es el término que más
    // pesa en el consumo en Run y en Sleep.
    sc_core::sc_in<unsigned> periph_on{"periph_on"};
    // DBGMCU_CR[2:0] = DBG_STANDBY | DBG_STOP | DBG_SLEEP. Con ellos, el MCU
    // ENTRA en el modo pero NO se le quitan los relojes al dominio de
    // depuración: es lo que permite depurar firmware que duerme [IR, §13.5].
    sc_core::sc_in<uint8_t> dbg_lp{"dbg_lp"};

    // ---- Salidas -----------------------------------------------------------
    sc_core::sc_out<bool>    irq_pvd{"irq_pvd"};      // via EXTI16
    sc_core::sc_out<bool>    dbp{"dbp"};              // acceso dominio backup
    sc_core::sc_out<bool>    standby_req{"standby_req"};// -> RCC (apagar 1.2V)
    sc_core::sc_out<bool>    stop_req{"stop_req"};    // -> RCC (parar relojes)
    sc_core::sc_out<bool>    vos_rdy{"vos_rdy"};
    sc_core::sc_out<uint8_t> lp_mode{"lp_mode"};      // LpMode, para todos
    sc_core::sc_out<double>  idd{"idd"};              // consumo por VDD  [A]
    sc_core::sc_out<double>  ibat{"ibat"};            // consumo por VBAT [A]
    // Dos bits del CSR que necesita el resto del modelo: el pin WKUP sigue
    // vivo en Standby si EWUP, y la BKPSRAM solo conserva sus datos si el
    // regulador de backup está encendido [IR, §14.5.2, §14.7].
    sc_core::sc_out<bool>    ewup{"ewup"};
    sc_core::sc_out<bool>    bre{"bre"};

    enum : uint32_t { R_CR = 0x00, R_CSR = 0x04 };
    // ---- PWR_CR [IR, §14.6.1] ---------------------------------------------
    static constexpr uint32_t CR_LPDS = 1u << 0, CR_PDDS = 1u << 1,
                              CR_CWUF = 1u << 2, CR_CSBF = 1u << 3,
                              CR_PVDE = 1u << 4, CR_DBP  = 1u << 8,
                              CR_FPDS = 1u << 9;
    // ---- PWR_CSR [IR, §14.6.2] --------------------------------------------
    static constexpr uint32_t CSR_WUF = 1u << 0, CSR_SBF = 1u << 1,
                              CSR_PVDO = 1u << 2, CSR_BRR = 1u << 3,
                              CSR_EWUP = 1u << 8, CSR_BRE = 1u << 9,
                              CSR_VOSRDY = 1u << 14;

    // =======================================================================
    // Parámetros temporales [IR, §14 — ⚠ NO DISPONIBLE EN LAS FUENTES con
    // valor numérico: el informe no da los tiempos de despertar. Se exponen
    // como parámetros con los valores típicos del dispositivo.]
    // =======================================================================
    sc_core::sc_time t_wu_stop_mr{13, sc_core::SC_US};   // Stop, regulador ON
    sc_core::sc_time t_wu_stop_lp{40, sc_core::SC_US};   // Stop, regulador LP
    sc_core::sc_time t_wu_flash{7, sc_core::SC_US};      // suplemento por FPDS
    sc_core::sc_time t_wu_standby{375, sc_core::SC_US};  // Standby -> reset
    sc_core::sc_time t_bkp_reg{1, sc_core::SC_MS};       // BRE -> BRR

    // =======================================================================
    // Umbrales del PVD (PLS[2:0]) e histéresis
    // ⚠ NO DISPONIBLE EN LAS FUENTES: el informe remite al datasheet. Valores
    // típicos del STM32F405/407, como parámetros.
    // =======================================================================
    double v_pls[8] = {2.0, 2.1, 2.3, 2.5, 2.6, 2.7, 2.8, 2.9};
    double v_pvd_hyst = 0.10;

    // =======================================================================
    // Modelo de consumo (IDD). ⚠ NO DISPONIBLE EN LAS FUENTES: el informe no
    // da corrientes. Son los valores típicos del datasheet a 3,3 V y 25 °C,
    // expuestos como parámetros para que quien los necesite exactos los ajuste.
    //
    //   Run   = i_run0 + k_run*f[MHz]   + n_periph*i_per_run
    //   Sleep = i_slp0 + k_sleep*f[MHz] + n_periph*i_per_slp
    //   Stop  = i_stop_mr o i_stop_lp, menos lo que ahorra FPDS
    //   Stby  = i_stby + (RTC vivo ? i_stby_rtc : 0) + (BRE ? i_bkpreg : 0)
    //
    // A 168 MHz salen ~60 mA sin periféricos y ~87 mA con todos, que son las
    // dos cifras que da el datasheet; en Sleep, 15 mA y 39 mA.
    // =======================================================================
    double i_run0 = 5.0e-3,  k_run   = 0.330e-3;     // A y A/MHz
    double i_slp0 = 5.0e-3,  k_sleep = 0.060e-3;
    double i_per_run = 0.54e-3, i_per_slp = 0.48e-3; // por reloj de periférico
    double i_stop_mr = 420e-6, i_stop_lp = 290e-6, i_stop_fpds = 60e-6;
    double i_stby = 2.4e-6, i_stby_rtc = 0.6e-6, i_bkpreg = 1.2e-6;
    double i_vbat = 1.29e-6;                         // dominio de backup por VBAT
    double k_vos[4] = {1.0, 0.80, 0.90, 1.0};        // escalas 3, 2 y 1 de VOS

    Pwr(sc_core::sc_module_name nm) : BusSlave(nm, addr::PWR_B, 0x400) {
        SC_HAS_PROCESS(Pwr);
        SC_THREAD(power_fsm);
        SC_THREAD(bkp_reg_proc);
        SC_METHOD(sense_proc);
        sensitive << sleeping << sleepdeep << exti_wakeup << wkup_pin << vdd_lvl
                  << rtc_alarm << rtc_tamper << rtc_wkup << dbg_lp;
        dont_initialize();
        SC_METHOD(idd_proc);
        sensitive << hclk_hz << periph_on << rtcclk_hz << idd_ev_;
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(rst_proc); sensitive << rst_n;
    }

    // ---- Ventanas para el banco de pruebas ---------------------------------
    LpMode   modo() const { return modo_; }
    uint32_t peek_cr() const  { return cr_; }
    uint32_t peek_csr() const { return csr_; }
    double   consumo() const { return i_dd_; }
    unsigned entradas_stop() const { return n_stop_; }
    unsigned entradas_standby() const { return n_stby_; }
    // Cuántas veces ha vuelto el MCU a Run desde un modo de bajo consumo. Hace
    // falta porque un despertar puede durar menos que la resolución con la que
    // mira el banco: el firmware se vuelve a dormir enseguida.
    unsigned despertares() const { return n_desp_; }

protected:
    // El PWR responde aunque el reloj del sistema esté parado: está en el
    // dominio VDD, no en el de 1,2 V. Lo que sí exige es su bit PWREN.
    uint32_t cr_ = 0x0000C000u;      // VOS = escala 1 tras reset
    uint32_t csr_ = CSR_VOSRDY;
    LpMode   modo_ = LP_RUN;
    bool     o_dbp_ = false, o_vos_ = true, o_stop_ = false, o_stby_ = false;
    bool     o_pvd_ = false, pvdo_ = false;
    bool     wkup_prev_ = false;
    double   i_dd_ = 0.0, i_bat_ = 0.0;
    unsigned n_stop_ = 0, n_stby_ = 0, n_desp_ = 0;
    sc_core::sc_event pub_ev_, fsm_ev_, idd_ev_, bkp_ev_;

    bool pdds() const { return (cr_ & CR_PDDS) != 0; }
    bool lpds() const { return (cr_ & CR_LPDS) != 0; }
    bool fpds() const { return (cr_ & CR_FPDS) != 0; }
    bool dbg_sleep()   const { return (dbg_lp.read() & 1u) != 0; }
    bool dbg_stop()    const { return (dbg_lp.read() & 2u) != 0; }
    bool dbg_standby() const { return (dbg_lp.read() & 4u) != 0; }
    unsigned vos() const { return (cr_ >> 14) & 3u; }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return cr_;
            case R_CSR: return csr_;
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
            case R_CR:
                cr_ = v & 0x0000C3FFu;
                // CWUF y CSBF son ÓRDENES DE BORRADO, no bits guardados: se
                // leen siempre como 0 [IR, §14.6.1].
                if (v & CR_CWUF) csr_ &= ~CSR_WUF;
                if (v & CR_CSBF) csr_ &= ~CSR_SBF;
                cr_ &= ~(CR_CWUF | CR_CSBF);
                actualiza_pvd();
                publish();
                fsm_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            case R_CSR: {
                // Solo EWUP y BRE son de escritura; el resto son banderas.
                const uint32_t antes = csr_;
                csr_ = (csr_ & ~(CSR_EWUP | CSR_BRE)) | (v & (CSR_EWUP | CSR_BRE));
                if ((antes ^ csr_) & CSR_BRE) bkp_ev_.notify(sc_core::SC_ZERO_TIME);
                idd_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            default: return;
        }
    }

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        dbp.write((cr_ & CR_DBP) != 0);
        vos_rdy.write((csr_ & CSR_VOSRDY) != 0);
        irq_pvd.write(o_pvd_);
        stop_req.write(o_stop_);
        standby_req.write(o_stby_);
        lp_mode.write(uint8_t(modo_));
        ewup.write((csr_ & CSR_EWUP) != 0);
        bre.write((csr_ & CSR_BRE) != 0);
        idd.write(i_dd_);
        ibat.write(i_bat_);
    }

    // -----------------------------------------------------------------------
    // Reset. El PWR está en el dominio VDD: un reset de sistema -incluido el
    // que provoca la salida de Standby- NO se lleva por delante WUF, SBF, EWUP
    // ni BRE, que es justo lo que permite al firmware saber POR QUÉ acaba de
    // arrancar. Solo la pérdida de VDD los borra [IR, §14.5.3].
    // -----------------------------------------------------------------------
    void rst_proc() {
        if (rst_n.read()) return;
        const bool vdd_ok = vdd_lvl.read() > 1.7;
        const uint32_t guarda = vdd_ok ? (csr_ & (CSR_WUF | CSR_SBF | CSR_EWUP |
                                                  CSR_BRE | CSR_BRR)) : 0u;
        cr_  = 0x0000C000u;
        csr_ = CSR_VOSRDY | guarda;
        if (!vdd_ok) { modo_ = LP_RUN; o_stop_ = o_stby_ = false; }
        actualiza_pvd();
        publish();
    }

    // =======================================================================
    // El PVD: comparador sobre el nivel REAL de VDD, con histéresis
    // =======================================================================
    void actualiza_pvd() {
        const double v = vdd_lvl.read();
        if (!(cr_ & CR_PVDE) || v < 0.3) {    // apagado o sin alimentación
            pvdo_ = false; csr_ &= ~CSR_PVDO; o_pvd_ = false;
            return;
        }
        const double u = v_pls[(cr_ >> 5) & 7u];
        if (pvdo_) { if (v > u + v_pvd_hyst) pvdo_ = false; }
        else       { if (v < u)              pvdo_ = true;  }
        if (pvdo_) csr_ |= CSR_PVDO; else csr_ &= ~CSR_PVDO;
        o_pvd_ = pvdo_;                       // -> línea EXTI16
    }

    // El regulador de backup no está listo al instante: hay que esperar a BRR
    // antes de fiarse de la BKPSRAM [IR, §14.7].
    void bkp_reg_proc() {
        for (;;) {
            wait(bkp_ev_);
            if (csr_ & CSR_BRE) {
                wait(t_bkp_reg);
                if (csr_ & CSR_BRE) { csr_ |= CSR_BRR; publish(); }
            } else {
                csr_ &= ~CSR_BRR; publish();
            }
        }
    }

    // =======================================================================
    // Vigilancia: PVD, pin WKUP y despertadores del Standby
    // =======================================================================
    void sense_proc() {
        actualiza_pvd();
        // Flanco de subida en PA0-WKUP con EWUP = 1 [IR, §14.8]. El flag WUF
        // se pone SIEMPRE que el pin está habilitado, esté el MCU donde esté:
        // por eso hay que borrarlo antes de entrar en Standby.
        const bool w = wkup_pin.read();
        if (w && !wkup_prev_ && (csr_ & CSR_EWUP)) csr_ |= CSR_WUF;
        wkup_prev_ = w;
        publish();
        fsm_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    bool hay_despertador_standby() const {
        return ((csr_ & CSR_EWUP) && wkup_pin.read()) ||
               rtc_alarm.read() || rtc_tamper.read() || rtc_wkup.read();
    }

    // =======================================================================
    // La máquina de los cuatro modos [IR, §14.2-14.5]
    // =======================================================================
    void power_fsm() {
        for (;;) {
            // ---------------------------- Run -------------------------------
            while (!sleeping.read()) wait(fsm_ev_);

            if (!sleepdeep.read()) {
                // ------------------------ Sleep -----------------------------
                // El núcleo está parado; los relojes siguen. El único cambio
                // visible desde fuera es que el gating pasa a los LPENR, y de
                // eso se ocupa el RCC al ver el modo.
                cambia_modo(LP_SLEEP);
                while (sleeping.read()) wait(fsm_ev_);
                cambia_modo(LP_RUN);
                continue;
            }

            if (!pdds()) {
                // ------------------------ Stop ------------------------------
                ++n_stop_;
                cambia_modo(LP_STOP);
                // Con DBG_STOP el modo se entra igual, pero los relojes NO se
                // paran: si se pararan, la sonda perdería el objetivo en cuanto
                // el firmware ejecutara su primer WFI [IR, §13.5].
                if (!dbg_stop()) { o_stop_ = true; publish(); }
                // Despierta cualquier línea EXTI desenmascarada. También se
                // sale si el núcleo deja de estar dormido por cualquier otra
                // razón (por ejemplo, una parada del depurador).
                while (sleeping.read() && !exti_wakeup.read()) wait(fsm_ev_);
                // Vuelta del regulador y rearranque del HSI. El RCC hace el
                // resto: HSI como SYSCLK, PLL apagado [IR, §14.4.3].
                o_stop_ = false; publish();
                wait(t_wu_stop());
                cambia_modo(LP_RUN);
                // Y AHORA hay que esperar a que el núcleo se despierte de
                // verdad. El sistema ya ha salido del Stop, pero el núcleo
                // sigue en su WFI/WFE hasta que le vuelve el reloj y ve su
                // despertador. Sin esta espera se volvería a entrar en Stop en
                // el mismo instante -`sleeping` y `sleepdeep` siguen a uno- y
                // el MCU no saldría nunca. Si el núcleo NO se despierta (un WFI
                // al que solo llegó un evento), el sistema se queda en Run: que
                // es exactamente lo que hace el silicio.
                while (sleeping.read()) wait(fsm_ev_);
                continue;
            }

            // ------------------------- Standby ------------------------------
            // Si WUF sigue puesto, el MCU NO entra: despierta inmediatamente.
            // Es la razón por la que la secuencia canónica escribe CWUF justo
            // antes del WFI [IR, §14.5.1].
            if (csr_ & CSR_WUF) { wait(fsm_ev_); continue; }
            ++n_stby_;
            csr_ |= CSR_SBF;                  // lo leerá el firmware al volver
            cambia_modo(LP_STANDBY);
            if (dbg_standby()) {
                // Con DBG_STANDBY el dominio de 1.2 V se mantiene: no hay
                // pérdida de estado ni reset, y el depurador sigue vivo.
                while (sleeping.read() && !hay_despertador_standby()) wait(fsm_ev_);
                cambia_modo(LP_RUN);
                continue;
            }
            o_stby_ = true; publish();        // -> RCC: apagar el dominio 1.2 V
            while (!hay_despertador_standby()) wait(fsm_ev_);
            csr_ |= CSR_WUF;
            wait(t_wu_standby);               // arranque del regulador
            o_stby_ = false; publish();       // -> RCC: reset tipo POR
            cambia_modo(LP_RUN);
        }
    }

    sc_core::sc_time t_wu_stop() const {
        sc_core::sc_time t = lpds() ? t_wu_stop_lp : t_wu_stop_mr;
        if (fpds()) t += t_wu_flash;          // la Flash también tiene que volver
        return t;
    }

    void cambia_modo(LpMode m) {
        if (modo_ == m) return;
        if (m == LP_RUN && modo_ != LP_RUN) ++n_desp_;
        modo_ = m;
        publish();
        idd_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // =======================================================================
    // El modelo de consumo
    //
    // No es una tabla de cuatro números: en Run y en Sleep el consumo depende
    // de la frecuencia y de cuántos relojes de periférico haya abiertos, que
    // es exactamente la palanca que tiene el firmware para gastar menos sin
    // dormirse. Por eso el RCC publica la cuenta.
    // =======================================================================
    void idd_proc() {
        const double v = vdd_lvl.read();
        const double f_mhz = hclk_hz.read() / 1e6;
        const unsigned np = periph_on.read();
        const double kv = k_vos[vos() & 3u];
        double i = 0.0, ib = 0.0;
        const bool rtc_vivo = rtcclk_hz.read() > 0.0;

        switch (modo_) {
            case LP_RUN:
                i = kv * (i_run0 + k_run * f_mhz) + np * i_per_run;
                break;
            case LP_SLEEP:
                i = kv * (i_slp0 + k_sleep * f_mhz) + np * i_per_slp;
                break;
            case LP_STOP:
                // Con DBG_STOP no hay tal ahorro: los relojes siguen dando
                // vueltas para que el depurador no se caiga.
                if (dbg_stop()) i = kv * (i_slp0 + k_sleep * f_mhz) + np * i_per_slp;
                else {
                    i = lpds() ? i_stop_lp : i_stop_mr;
                    if (fpds()) i -= i_stop_fpds;
                }
                break;
            case LP_STANDBY:
                i = i_stby + (rtc_vivo ? i_stby_rtc : 0.0)
                           + ((csr_ & CSR_BRE) ? i_bkpreg : 0.0);
                break;
        }
        // Sin alimentación no hay consumo por VDD; el dominio de backup pasa a
        // tirar de VBAT, que es la razón de que exista ese pin [IR, §14.1].
        if (v < 1.7) { ib = rtc_vivo ? i_vbat : 0.3 * i_vbat; i = 0.0; }
        i_dd_ = i; i_bat_ = ib;
        publish();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_PWR_H
