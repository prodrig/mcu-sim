// =============================================================================
// syscfg_f3.h — El SYSCFG del STM32F303, y el PWR básico (fase S1)
//
// SYSCFG, en 0x4001_0000 [CMSIS: SYSCFG_TypeDef y SYSCFG_*_Pos]:
//
//   0x00 CFGR1   MEM_MODE (1:0): lo que hay en 0x0 —00 la Flash, 01 la memoria
//                de sistema, 11 la SRAM—, que sale de reset con lo que dijeron
//                BOOT0 y nBOOT1 y luego cambia el firmware; los remapeos (USB_IT,
//                TIM1_ITR3, DAC1_TRIG1, DMA), los Fm+ de los I²C y FPU_IE (31:26)
//   0x04 RCR     protección de escritura de la CCM, por páginas de 1 KB
//   0x08..0x14   EXTICR1-4: qué puerto (0 = A .. 5 = F) llega a cada línea
//   0x18 CFGR2   los cerrojos (LOCKUP, paridad de la SRAM, PVD) y SRAM_PE
//
// y en la misma ventana, aunque son otros bloques, los registros de los siete
// comparadores (0x1C-0x34) y los cuatro amplificadores (0x38-0x44) [CMSIS:
// COMPx_BASE, OPAMPx_BASE]. Desde S5 los atienden los bloques de verdad
// (periph/comp.h, periph/opamp.h): el SYSCFG se los DELEGA por `analogico`
// (índice 0-6 los comparadores, 7-10 los amplificadores). Sin delegado, guardan
// lo que se escribe y avisan una vez, como en S1-S4.
//
// PWR, en 0x4000_7000 [CMSIS: PWR_TypeDef]: CR (LPDS, PDDS, CWUF, CSBF, PVDE,
// PLS, DBP) y CSR (WUF, SBF, PVDO, VREFINTRDYF, EWUP1-3); los modos de bajo
// consumo, desde S3 (abajo).
// =============================================================================
#ifndef STM32_SOC_STM32F3_SYSCFG_F3_H
#define STM32_SOC_STM32F3_SYSCFG_F3_H

#include <cstdio>
#include <functional>
#include "../../common/periph_base.h"
#include "f3_desc.h"

namespace stm32 {

class SyscfgF3 : public BusSlave {
public:
    enum : uint32_t { R_CFGR1 = 0x00, R_RCR = 0x04, R_EXTICR1 = 0x08, R_CFGR2 = 0x18,
                      R_COMP1 = 0x1C, R_OPAMP4 = 0x44 };
    // MEM_MODE [CMSIS: SYSCFG_CFGR1_MEM_MODE]
    enum MemMode : uint8_t { MEM_FLASH = 0, MEM_SISTEMA = 1, MEM_SRAM = 3 };

    sc_core::sc_vector<sc_core::sc_out<uint8_t>> exticr_sel;   // [16] -> EXTI
    sc_core::sc_in<uint8_t> boot{"boot"};                      // lo muestreado al salir de reset
    // CFGR1 hacia el SoC (S3): los remapeos del DMA y el ITR3 del TIM1
    sc_core::sc_out<uint32_t> cfgr1_sig{"cfgr1_sig"};

    explicit SyscfgF3(sc_core::sc_module_name nm)
        : BusSlave(nm, f3::SYSCFG_B, 0x400), exticr_sel("exticr_sel", 16) {
        SC_HAS_PROCESS(SyscfgF3);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n << boot;
    }

    uint8_t mem_mode() const { return uint8_t(cfgr1_ & 3u); }
    // ¿Se puede escribir esta dirección de la CCM? (RCR, un bit por KB)
    bool ccm_protegida(uint32_t off) const { return (rcr_ >> (off / 0x400u)) & 1u; }
    uint32_t cfgr1() const { return cfgr1_; }
    unsigned avisos_comp() const { return n_comp_; }
    // S5: quién atiende COMP1-7_CSR y OPAMP1-4_CSR (índice 0..10), y su reset
    struct Analogico {
        std::function<uint32_t(unsigned)> lee;
        std::function<void(unsigned, uint32_t)> escribe;
        std::function<void()> reinicia;
    } analogico;

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off >= R_EXTICR1 && off < R_CFGR2) return exticr_[(off - R_EXTICR1) / 4];
        if (off >= R_COMP1 && off <= R_OPAMP4) {
            if (analogico.lee) return analogico.lee((off - R_COMP1) / 4);
            return compopamp_[(off - R_COMP1) / 4];
        }
        switch (off) {
            case R_CFGR1: return cfgr1_;
            case R_RCR:   return rcr_;
            case R_CFGR2: return cfgr2_;
            default:      return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (reg_read(off) & ~m) | (v & m);
        }
        if (off >= R_EXTICR1 && off < R_CFGR2) {
            exticr_[(off - R_EXTICR1) / 4] = v & 0xFFFFu;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        if (off >= R_COMP1 && off <= R_OPAMP4 && analogico.escribe) {
            analogico.escribe((off - R_COMP1) / 4, v);
            return;
        }
        if (off >= R_COMP1 && off <= R_OPAMP4) {
            // Los bits de bloqueo (31) de COMPx_CSR y OPAMPx_CSR: una vez a uno,
            // el registro no cambia hasta el reset [CMSIS: COMP_CSR_COMPxLOCK]
            uint32_t& r = compopamp_[(off - R_COMP1) / 4];
            if (!(r & 0x80000000u)) r = v;
            if (!avisado_comp_) {
                avisado_comp_ = true;
                ++n_comp_;
                SC_REPORT_WARNING("syscfg_f3", "COMPx_CSR / OPAMPx_CSR: los comparadores y "
                                  "los amplificadores estan DECLARADOS (fase S5): guardan lo "
                                  "escrito y no hacen nada");
            }
            return;
        }
        switch (off) {
            case R_CFGR1:
                // MEM_MODE, los remapeos, los Fm+, ENCODER_MODE y FPU_IE
                cfgr1_ = v & 0xFCFF79E3u;      // [RM0316 §12.1.1]
                break;
            case R_RCR: rcr_ = (rcr_ | v) & 0xFFu; break;     // solo se ponen, no se quitan
            case R_CFGR2:
                // Los tres cerrojos se ponen y no se quitan; SRAM_PE es rc_w1
                cfgr2_ = (cfgr2_ & 0x7u) | (v & 0x17u);
                if (v & (1u << 8)) cfgr2_ &= ~(1u << 8);
                break;
            default: return;
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    uint32_t cfgr1_ = 0x7C000000u, rcr_ = 0, cfgr2_ = 0;
    uint32_t exticr_[4] = {};
    uint32_t compopamp_[11] = {};
    bool avisado_comp_ = false;
    unsigned n_comp_ = 0;
    sc_core::sc_event pub_ev_;

    void pub_proc() {
        cfgr1_sig.write(cfgr1_);
        for (unsigned l = 0; l < 16; ++l)
            exticr_sel[l].write(uint8_t((exticr_[l / 4] >> (4 * (l % 4))) & 0xFu));
    }
    // En reset, MEM_MODE sigue a los pines de arranque, de modo que al soltarlo
    // ya vale lo que dijeron; FPU_IE vale 011111 [RM0316 §12.1.1]. Fuera
    // de reset, lo que cambie el firmware.
    void reset_proc() {
        if (rst_n.read()) return;
        cfgr1_ = 0x7C000000u | (boot.read() & 3u);
        rcr_ = 0; cfgr2_ = 0;
        for (uint32_t& e : exticr_) e = 0;
        for (uint32_t& c : compopamp_) c = 0;
        if (analogico.reinicia) analogico.reinicia();
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

// -----------------------------------------------------------------------------
// EL PWR (fase S3): los cuatro modos, el PVD y los pines de despertar
//
//   Run      todo en marcha
//   Sleep    WFI/WFE con SLEEPDEEP = 0: se para el núcleo, los relojes no
//   Stop     SLEEPDEEP = 1 y PDDS = 0: se paran todos los relojes del dominio
//            de 1,8 V, y el HSI, el HSE y el PLL se apagan; la SRAM y los
//            registros se conservan. Despierta cualquier línea del EXTI, y al
//            despertar SYSCLK es el HSI: hay que volver a poner el PLL
//            [RM0316 §7.3.5]
//   Standby  SLEEPDEEP = 1 y PDDS = 1: se apaga el dominio de 1,8 V. Despiertan
//            un flanco de subida en un WKUP habilitado (PA0, PC13, PE6), la
//            alarma, el despertador, el sello de tiempo y la intrusión del RTC,
//            y NRST; la salida es un reset de alimentación, con SBF en CSR
//            [RM0316 §7.3.6]
//
// Con nRST_STOP o nRST_STDBY a cero en los bytes de opción, entrar en el modo
// es un reset (LPWRRSTF) en vez de dormir [RM0316 §9.1.2]. Con DBG_STOP o
// DBG_STANDBY (DBGMCU_CR) se entra en el modo pero no se quitan los relojes,
// para que la sonda no pierda el chip [RM0316 §33.16.3].
//
// Los tiempos de despertar son los típicos de DS9118 a 3,3 V (tabla 39). El
// PVD compara el nivel REAL de VDD con PLS, con los umbrales y la histéresis
// de la tabla 27, y va a la línea 16 del EXTI.
//
// LO QUE NO HAY: modelo de corriente (s_idd sigue a cero) y despertar desde
// Stop por las USART o los I²C (UESM, WUPEN).
// -----------------------------------------------------------------------------
class PwrF3 : public BusSlave {
public:
    enum : uint32_t { R_CR = 0x00, R_CSR = 0x04 };
    static constexpr uint32_t CR_LPDS = 1u << 0, CR_PDDS = 1u << 1, CR_CWUF = 1u << 2,
        CR_CSBF = 1u << 3, CR_PVDE = 1u << 4, CR_DBP = 1u << 8;
    static constexpr uint32_t CSR_WUF = 1u << 0, CSR_SBF = 1u << 1, CSR_PVDO = 1u << 2,
        CSR_VREFINTRDYF = 1u << 3, CSR_EWUP1 = 1u << 8;

    sc_core::sc_out<bool> dbp{"dbp"};
    sc_core::sc_out<bool> pvd{"pvd"};                     // -> línea 16 del EXTI
    sc_core::sc_out<bool> standby_pines{"standby_pines"}; // -> los pads en alta impedancia
    sc_core::sc_out<bool> ewup1{"ewup1"};                 // -> PA0 sigue vivo en Standby
    sc_core::sc_in<double> vdd_lvl{"vdd_lvl"};
    sc_core::sc_in<bool> sleeping{"sleeping"}, sleepdeep{"sleepdeep"};
    sc_core::sc_in<bool> exti_wakeup{"exti_wakeup"};
    sc_core::sc_in<bool> nrst_n{"nrst_n"};
    sc_core::sc_in<uint8_t> dbg_lp{"dbg_lp"};             // DBGMCU_CR[2:0]
    sc_core::sc_vector<sc_core::sc_in<bool>> wkup;        // [3] PA0, PC13, PE6
    sc_core::sc_vector<sc_core::sc_in<bool>> rtc_ev;      // alarma, sello/intrusión, despertador

    // Lo que el SoC hace al entrar y salir: parar los relojes (el RCC), el
    // dominio de 1,8 V (el RCC, como un reset que dura), y las opciones
    std::function<void(bool)> al_parar, al_standby;
    std::function<void()> reset_bajo_consumo;             // nRST_STOP / nRST_STDBY a cero
    std::function<bool()> nrst_stop, nrst_stdby;          // los bits de opción (1 = duerme)

    sc_core::sc_time t_wu_stop_mr{3600, sc_core::SC_NS};   // [DS9118, tabla 39, 3,3 V]
    sc_core::sc_time t_wu_stop_lp{5400, sc_core::SC_NS};
    sc_core::sc_time t_wu_standby{51700, sc_core::SC_NS};

    explicit PwrF3(sc_core::sc_module_name nm)
        : BusSlave(nm, f3::PWR_B, 0x400), wkup("wkup", 3), rtc_ev("rtc_ev", 3) {
        SC_HAS_PROCESS(PwrF3);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(vigila_proc);
        sensitive << vdd_lvl << sleeping << sleepdeep << exti_wakeup << nrst_n;
        for (unsigned i = 0; i < 3; ++i) sensitive << wkup[i] << rtc_ev[i];
        dont_initialize();
        SC_THREAD(fsm);
    }
    uint32_t cr() const { return cr_; }
    LpMode modo() const { return modo_; }
    unsigned entradas_stop() const { return n_stop_; }
    unsigned entradas_standby() const { return n_stby_; }
    unsigned despertares() const { return n_desp_; }

protected:
    // El PWR está en el dominio de VDD: responde aunque su reloj esté cortado
    // (PWREN solo hace falta para escribir; aquí, como en el F4, se pide)
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return cr_;
            // VREFINTRDYF: la referencia está lista en cuanto hay tensión
            case R_CSR: return csr_ | CSR_VREFINTRDYF;
            default:    return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (reg_read(off) & ~m) | (v & m);
        }
        switch (off) {
            case R_CR:
                if (v & CR_CWUF) csr_ &= ~CSR_WUF;
                if (v & CR_CSBF) csr_ &= ~CSR_SBF;
                cr_ = v & 0x1F3u;                          // LPDS, PDDS, PVDE, PLS, DBP
                actualiza_pvd();
                break;
            case R_CSR: csr_ = (csr_ & 0x7u) | (v & 0x700u); break;   // EWUP1-3
            default: return;
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        fsm_ev_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    uint32_t cr_ = 0, csr_ = 0;
    LpMode modo_ = LP_RUN;
    bool pvdo_ = false, o_stby_pines_ = false;
    bool wk_ant_[3] = {};
    unsigned n_stop_ = 0, n_stby_ = 0, n_desp_ = 0;
    sc_core::sc_event pub_ev_, fsm_ev_;

    bool dbg_stop() const { return (dbg_lp.read() & 2u) != 0; }
    bool dbg_standby() const { return (dbg_lp.read() & 4u) != 0; }

    void pub_proc() {
        dbp.write((cr_ >> 8) & 1u);
        pvd.write(pvdo_);
        standby_pines.write(o_stby_pines_);
        ewup1.write((csr_ & CSR_EWUP1) != 0);
    }
    // Un reset del sistema -incluida la salida del Standby- no borra WUF, SBF
    // ni EWUPx: son los que le dicen al firmware por qué ha arrancado. Solo
    // la pérdida de VDD los borra [RM0316 §7.4.2]
    void reset_proc() {
        if (rst_n.read()) return;
        const bool vdd_ok = vdd_lvl.read() > 1.7;
        cr_ = 0;
        csr_ = vdd_ok ? (csr_ & (CSR_WUF | CSR_SBF | 0x700u)) : 0u;
        actualiza_pvd();
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // PLS: umbral de bajada y, 100 mV por encima, el de subida [DS9118, tabla 27]
    void actualiza_pvd() {
        const double v = vdd_lvl.read();
        if (!(cr_ & CR_PVDE) || v < 0.3) { pvdo_ = false; csr_ &= ~CSR_PVDO; return; }
        static const double baja[8] = {2.08, 2.18, 2.28, 2.38, 2.48, 2.58, 2.68, 2.78};
        const double u = baja[(cr_ >> 5) & 7u];
        if (pvdo_) { if (v > u + 0.1) pvdo_ = false; }
        else       { if (v < u) pvdo_ = true; }
        if (pvdo_) csr_ |= CSR_PVDO; else csr_ &= ~CSR_PVDO;
    }

    void vigila_proc() {
        actualiza_pvd();
        // Un flanco de subida en un WKUP habilitado pone WUF, esté el chip donde esté
        for (unsigned i = 0; i < 3; ++i) {
            const bool w = wkup[i].read();
            if (w && !wk_ant_[i] && (csr_ & (CSR_EWUP1 << i))) csr_ |= CSR_WUF;
            wk_ant_[i] = w;
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        fsm_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool despierta_standby() const {
        for (unsigned i = 0; i < 3; ++i)
            if ((csr_ & (CSR_EWUP1 << i)) && wkup[i].read()) return true;
        for (unsigned i = 0; i < 3; ++i) if (rtc_ev[i].read()) return true;
        return !nrst_n.read();
    }
    void modo(LpMode m) {
        if (modo_ == m) return;
        if (m == LP_RUN) ++n_desp_;
        modo_ = m;
    }

    void fsm() {
        for (;;) {
            while (!sleeping.read()) wait(fsm_ev_);
            if (!sleepdeep.read()) {
                // ---------------- Sleep ---------------------------------------------
                modo(LP_SLEEP);
                while (sleeping.read()) wait(fsm_ev_);
                modo(LP_RUN);
                continue;
            }
            if (!(cr_ & CR_PDDS)) {
                // ---------------- Stop ----------------------------------------------
                if (nrst_stop && !nrst_stop()) {
                    if (reset_bajo_consumo) reset_bajo_consumo();
                    while (sleeping.read()) wait(fsm_ev_);
                    continue;
                }
                ++n_stop_;
                modo(LP_STOP);
                const bool para = !dbg_stop();
                if (para && al_parar) al_parar(true);
                while (sleeping.read() && !exti_wakeup.read()) wait(fsm_ev_);
                if (para && al_parar) al_parar(false);
                wait((cr_ & CR_LPDS) ? t_wu_stop_lp : t_wu_stop_mr);
                modo(LP_RUN);
                // El sistema ha salido; el núcleo, todavía no: hasta que vea su
                // despertador sigue con SLEEPDEEP, y sin esta espera volvería a
                // entrar en el mismo instante
                while (sleeping.read()) wait(fsm_ev_);
                continue;
            }
            // ---------------- Standby -----------------------------------------------
            // Con WUF puesto no se entra: se despierta enseguida [RM0316 §7.3.6]
            if (csr_ & CSR_WUF) { wait(fsm_ev_); continue; }
            if (nrst_stdby && !nrst_stdby()) {
                if (reset_bajo_consumo) reset_bajo_consumo();
                while (sleeping.read()) wait(fsm_ev_);
                continue;
            }
            ++n_stby_;
            csr_ |= CSR_SBF;
            modo(LP_STANDBY);
            if (dbg_standby()) {
                // Con DBG_STANDBY el dominio de 1,8 V sigue vivo: sin reset
                while (sleeping.read() && !despierta_standby()) wait(fsm_ev_);
                modo(LP_RUN);
                while (sleeping.read()) wait(fsm_ev_);
                continue;
            }
            o_stby_pines_ = true; pub_ev_.notify(sc_core::SC_ZERO_TIME);
            if (al_standby) al_standby(true);
            // Lo que despierta ya ha podido llegar: espera a que se vaya y
            // vuelva, salvo el NRST, que es un nivel
            while (!despierta_standby()) wait(fsm_ev_);
            wait(t_wu_standby);
            o_stby_pines_ = false; pub_ev_.notify(sc_core::SC_ZERO_TIME);
            if (al_standby) al_standby(false);
            modo(LP_RUN);
            while (sleeping.read()) wait(fsm_ev_);
        }
    }
};

} // namespace stm32
#endif // STM32_SOC_STM32F3_SYSCFG_F3_H
