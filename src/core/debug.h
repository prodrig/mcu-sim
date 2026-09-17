// =============================================================================
// debug.h — Subsistema de depuración CoreSight + DBGMCU [IR, cap. 13]
//
// Es la infraestructura PARALELA al flujo de ejecución: no se ve desde el mapa
// de memoria del sistema, no la puede tocar el DMA, y sin embargo puede parar
// el núcleo entre dos instrucciones, cambiarle los registros y leer toda la
// memoria. Aquí está entera y llega hasta los pines:
//
//   SWJ-DP    protocolo SWD A NIVEL DE BIT sobre SWCLK/SWDIO (PA14/PA13), con
//             su secuencia de conmutación desde JTAG, sus paquetes de petición,
//             su turnaround, su ACK y su paridad. El TAP JTAG se reconoce y se
//             conmuta, pero quien habla es el SWD, que es lo que usa cualquier
//             sonda moderna.
//   AHB-AP    CSW/TAR/DRW/BDn/IDR sobre el router del núcleo: el depurador ve
//             el mismo mapa que la CPU, PPB incluido [IR, §13.2].
//   Core Debug DHCSR/DCRSR/DCRDR/DEMCR: parada, reanudación, paso a paso,
//             acceso a los registros del núcleo y captura de vectores.
//   FPB       6 comparadores de instrucción y 2 literales, con inyección de
//             BKPT y remapeado a SRAM [IR, §13.7].
//   DWT       CYCCNT y los cinco contadores de perfil, y 4 comparadores de
//             watchpoint con sus máscaras [IR, §13.5].
//   ITM       32 puertos de estímulo con su empaquetado CoreSight [IR, §13.6].
//   TPIU      formateador y serializador hacia el pin SWO (PB3), en NRZ, con
//             su divisor ACPR [IR, §13.8].
//   ROM table auto-descubrimiento de todo lo anterior [IR, §13.3].
//   DBGMCU    IDCODE del dispositivo y las líneas de congelación de
//             temporizadores y perros guardianes [IR, §13.9].
//
// La pieza que hace que todo esto sea depuración de verdad y no un banco de
// registros más es `core_debug_if`: la CPU llama aquí en cada búsqueda de
// instrucción, en cada acceso a datos y en cada excepción.
// =============================================================================
#ifndef STM32_CORE_DEBUG_H
#define STM32_CORE_DEBUG_H

#include <array>
#include <deque>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/ahb_types.h"
#include "cpu.h"
#include "debug_if.h"

namespace stm32 {

// Nº de líneas de congelación DBGMCU_APB1_FZ/APB2_FZ que reparte el top
enum FreezeId : unsigned {
    FZ_TIM2, FZ_TIM3, FZ_TIM4, FZ_TIM5, FZ_TIM6, FZ_TIM7, FZ_TIM12, FZ_TIM13,
    FZ_TIM14, FZ_WWDG, FZ_IWDG, FZ_I2C1, FZ_I2C2, FZ_I2C3, FZ_CAN1, FZ_CAN2,
    FZ_TIM1, FZ_TIM8, FZ_TIM9, FZ_TIM10, FZ_TIM11, FZ_COUNT
};

SC_MODULE(DebugSys), public core_debug_if {
    // Interfaz física (via pin_mux AF0: PA13/PA14/PA15/PB3/PB4) [IR, §13.1]
    sc_core::sc_in<bool>  swclk_tck{"swclk_tck"};
    sc_core::sc_in<bool>  swdio_in{"swdio_in"};
    sc_core::sc_out<bool> swdio_out{"swdio_out"};
    sc_core::sc_out<bool> swdio_oe{"swdio_oe"};
    sc_core::sc_in<bool>  jtdi{"jtdi"};
    sc_core::sc_out<bool> jtdo_swo{"jtdo_swo"};       // JTDO o SWO (traza async)
    sc_core::sc_in<bool>  njtrst{"njtrst"};

    // Maestro AHB-AP hacia el router del núcleo
    tlm_utils::simple_initiator_socket<DebugSys> ahb_ap{"ahb_ap"};
    // Esclavo PPB: todo lo que no es SCS
    tlm_utils::simple_target_socket<DebugSys> ppb{"ppb"};

    // Control del núcleo y congelación de periféricos
    sc_core::sc_out<bool> halt_req{"halt_req"};
    sc_core::sc_in<bool>  halted{"halted"};
    sc_core::sc_vector<sc_core::sc_out<bool>> freeze;
    // DBGMCU_CR[2:0] = DBG_STANDBY | DBG_STOP | DBG_SLEEP hacia el PWR y el
    // RCC: con ellos el MCU entra igual en el modo, pero no se le quita el
    // reloj al dominio de depuracion ni se apaga el de 1,2 V, que es lo que
    // permite depurar firmware que duerme [IR, §13.5, §14].
    sc_core::sc_out<uint8_t> dbg_lp{"dbg_lp"};

    // El núcleo. Los fija CortexM4F durante la elaboración.
    RegFile* cpu_reg = nullptr;
    Cpu*     cpu     = nullptr;
    // Frecuencia de HCLK, que es la referencia del SWO y del CYCCNT.
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};

    // ---- Mapa del PPB de depuración [IR, §13.3] ----------------------------
    enum : uint32_t {
        B_ITM = 0xE0000000u, B_DWT = 0xE0001000u, B_FPB = 0xE0002000u,
        B_SCS = 0xE000E000u, B_TPIU = 0xE0040000u, B_ETM = 0xE0041000u,
        B_DBGMCU = 0xE0042000u, B_ROM = 0xE00FF000u
    };
    // Los dos registros CONSTANTES del AHB-AP. Estaban escritos a mano dentro
    // del camino de bits del SW-DP; con nombre se pueden servir también por
    // llamada de función, que es lo que necesitan el stub interno y las
    // órdenes `monitor ReadAPEx` con las que los IDE de ST identifican el chip.
    enum : uint32_t {
        AP_BASE = 0xE00FF003u,      // puntero a la ROM table (con los bits de
                                    // formato y de presencia)
        AP_IDR  = 0x24770011u       // identificador del AHB-AP del Cortex-M4
    };
    // Core debug, dentro del SCS pero atendido aquí [IR, §13.4]
    enum : uint32_t {
        R_DHCSR = 0xE000EDF0u, R_DCRSR = 0xE000EDF4u,
        R_DCRDR = 0xE000EDF8u, R_DEMCR = 0xE000EDFCu
    };
    enum DhcsrBits : uint32_t {
        C_DEBUGEN = 1u << 0, C_HALT = 1u << 1, C_STEP = 1u << 2,
        C_MASKINTS = 1u << 3, C_SNAPSTALL = 1u << 5,
        S_REGRDY = 1u << 16, S_HALT = 1u << 17, S_SLEEP = 1u << 18,
        S_LOCKUP = 1u << 19, S_RETIRE_ST = 1u << 24, S_RESET_ST = 1u << 25
    };
    static constexpr uint32_t DBGKEY = 0xA05Fu;

    SC_CTOR(DebugSys) : freeze("freeze", FZ_COUNT) {
        ppb.register_b_transport(this, &DebugSys::bt);
        SC_THREAD(swd_proc);
        SC_THREAD(swo_proc);
        SC_METHOD(pub_proc);    sensitive << pub_ev_;
        SC_METHOD(freeze_proc); sensitive << halted << fz_ev_;
    }

    // -----------------------------------------------------------------------
    // API directa para el banco de pruebas y el cargador de imágenes. Es el
    // mismo camino que usa el AHB-AP, sin pasar por los pines.
    // -----------------------------------------------------------------------
    void set_halt(bool h) {
        if (h) dhcsr_ |= C_HALT | C_DEBUGEN; else dhcsr_ &= ~C_HALT;
        actualiza_halt();
    }
    bool is_halted() const { return halted.read(); }

    // -----------------------------------------------------------------------
    // Pines de depuracion EXPUESTOS o RESERVADOS.
    //
    // Con `false` los cinco pines (PA13/14/15, PB3/PB4) siguen asignados al
    // puerto de depuracion en el mux -no los puede usar nadie mas, igual que
    // en el silicio- pero el frente SWD deja de escuchar y el SWO deja de
    // emitir. Es lo que hace el nucleo cuando se construye con el stub
    // interno: reservados, aunque sin usar [véase doc/..._fase6_gdb2.md].
    // -----------------------------------------------------------------------
    void set_pines_debug(bool expuestos) {
        pines_dbg_ = expuestos;
        if (!expuestos) { o_swdio_oe_ = false; o_swo_ = true; publish(); }
        swo_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool pines_debug() const { return pines_dbg_; }

    // -----------------------------------------------------------------------
    // EL IDENTIFICADOR DEL DISPOSITIVO, como dato
    //
    // `DBGMCU_IDCODE` es lo primero que lee un depurador para saber con qué
    // está hablando, y es lo ÚNICO del subsistema de depuración que cambia de
    // un chip a otro: el SWJ-DP, el AHB-AP, el Core Debug, el FPB, el DWT, el
    // ITM/TPIU y la tabla ROM son idénticos en las dos piezas, y los dos
    // manuales remiten al mismo PM0214 sin declarar particularidades.
    //
    // Que sea un dato importa más de lo que parece: con el valor equivocado,
    // STM32CubeIDE no da un error claro, da un «Could not verify ST device».
    // Por omisión, el de la familia F405/407/415/417.
    // [RM0090 §32.6.1 = 0x1001 6413; RM0390 §33.6.1 = 0x1000 0421]
    // -----------------------------------------------------------------------
    void set_idcode(uint32_t v) { idcode_ = v; }
    uint32_t idcode() const { return idcode_; }

    tlm::tlm_response_status ap_access(bool write, uint64_t a,
                                       unsigned char* d, unsigned len) {
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        const auto r = ap_access_nb(write, a, d, len, t);
        sc_core::wait(t);
        return r;
    }
    // La misma transaccion, pero SIN consumir el tiempo anotado: devuelve
    // cuanto ha costado. Es lo que necesita la maquina del SWD, que no puede
    // quedarse bloqueada en mitad de un paquete mientras el bus trabaja: en el
    // silicio el DAP POSPONE el acceso y contesta WAIT a lo que llegue mientras
    // tanto, y eso es justo lo que se modela con ese tiempo.
    tlm::tlm_response_status ap_access_nb(bool write, uint64_t a,
                                          unsigned char* d, unsigned len,
                                          sc_core::sc_time& t) {
        tlm::tlm_generic_payload gp;
        AhbExt ext;
        ext.master     = BusMaster::CORE_SBUS;   // el AP se presenta como S-bus
        ext.privileged = true;
        gp_setup(gp, write, a, d, len);
        gp.set_extension(&ext);
        ahb_ap->b_transport(gp, t);
        gp.clear_extension(&ext);
        return gp.get_response_status();
    }
    tlm::tlm_response_status ap_write32(uint64_t a, uint32_t v) {
        unsigned char b[4];
        for (unsigned i = 0; i < 4; ++i) b[i] = uint8_t(v >> (8 * i));
        return ap_access(true, a, b, 4);
    }
    tlm::tlm_response_status ap_read32(uint64_t a, uint32_t& v) {
        unsigned char b[4] = {0, 0, 0, 0};
        const auto r = ap_access(false, a, b, 4);
        v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
            (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        return r;
    }

    // Los registros PROPIOS del AP —CSW, TAR, BASE, IDR—, que NO son la memoria
    // que hay detrás de él. Un depurador los lee para saber qué tiene delante
    // antes de tocar nada: BASE le da la ROM table y con ella identifica el
    // núcleo y sus unidades. `false` significa "ese AP o ese registro no
    // existe", que es información, no un error.
    bool ap_registro(unsigned ap, unsigned reg, uint32_t& v) const {
        v = 0;
        // Este chip tiene UN solo AP. Un AP que no existe no da error: da
        // ceros, y esa es justamente la forma en que una sonda descubre
        // cuantos hay —los recorre leyendo IDR hasta que sale 0—. Devolver
        // aqui el IDR del AP 0 para todos haria que esa cuenta no terminara
        // nunca. Un registro no implementado, lo mismo.
        if (ap != 0) return true;
        switch (reg & 0xFCu) {
            case 0x00: v = ap_csw_; break;
            case 0x04: v = ap_tar_; break;
            case 0xF8: v = AP_BASE; break;
            case 0xFC: v = AP_IDR;  break;
            default:   break;
        }
        return true;
    }

    // Ventanas del banco de pruebas
    uint32_t dhcsr() const { return leer_dhcsr(); }
    uint64_t swo_bytes() const { return n_swo_; }
    uint64_t swd_packets() const { return n_swd_; }
    uint64_t swd_waits() const { return n_wait_; }
    unsigned itm_fifo() const { return unsigned(tpiu_fifo_.size()); }

    // =======================================================================
    // core_debug_if — el contrato con el núcleo
    // =======================================================================
    bool dbg_halt_now() const override {
        return (dhcsr_ & C_DEBUGEN) && ((dhcsr_ & C_HALT) != 0);
    }
    bool dbg_mask_ints() const override {
        return (dhcsr_ & C_MASKINTS) && (dhcsr_ & C_DEBUGEN);
    }
    bool dbg_enabled() const override { return (dhcsr_ & C_DEBUGEN) != 0; }

    void dbg_request_halt(uint32_t bits) override {
        dfsr_local_ |= bits;
        // La causa de la parada vive en SCB_DFSR, que es del SCS: el
        // subsistema de depuracion la deposita alli [IR, §13.4].
        if (cpu) cpu->sys->set_dfsr(bits);
        dhcsr_ |= C_HALT;
        actualiza_halt();
    }
    bool dbg_take_step() override {
        if (!paso_) return false;
        paso_ = false;
        return true;
    }
    sc_core::sc_event& dbg_wake() override { return wake_; }

    // ---- FPB [IR, §13.7] ---------------------------------------------------
    int dbg_fetch(uint32_t addr, uint32_t& hw, uint32_t& alt) override {
        if (!(fp_ctrl_ & 1u)) return FETCH_NORMAL;      // unidad deshabilitada
        // El FPB solo actúa sobre el espacio Code [IR, §13.7.1].
        if (addr >= 0x20000000u) return FETCH_NORMAL;
        for (unsigned n = 0; n < N_CODE; ++n) {
            const uint32_t c = fp_comp_[n];
            if (!(c & 1u)) continue;
            if ((c & 0x1FFFFFFCu) != (addr & 0x1FFFFFFCu)) continue;
            const unsigned rep = (c >> 30) & 3u;
            const bool alta = (addr & 2u) != 0;
            if (rep == 0) {                            // remapeado
                // La palabra remapeada vive en FP_REMAP + 4*n [IR, §13.7.2].
                alt = (fp_remap_ & 0x3FFFFFE0u) + 4u * n + (addr & 2u);
                ++n_remap_;
                return FETCH_REMAP;
            }
            // BKPT en la media palabra baja, en la alta, o en las dos.
            if ((rep == 1 && !alta) || (rep == 2 && alta) || rep == 3) {
                hw = 0xBE00u;                          // BKPT #0
                ++n_bkpt_;
                return FETCH_SUBST;
            }
        }
        return FETCH_NORMAL;
    }

    // ---- DWT: comparadores de datos [IR, §13.5.3] --------------------------
    void dbg_data(uint32_t addr, unsigned size, bool write, uint32_t v) override {
        if (!trcena()) return;
        ++lsu_pend_;
        for (unsigned n = 0; n < N_DWT; ++n) {
            const unsigned f = dwt_func_[n] & 0xFu;
            if (f < 4 || f > 6) continue;              // 4: R/W, 5: R, 6: W
            if (f == 5 && write) continue;
            if (f == 6 && !write) continue;
            const uint32_t m = dwt_mask_[n] & 0x1Fu;
            const uint32_t mask = (m >= 32) ? 0xFFFFFFFFu : ((1u << m) - 1u);
            if (((addr ^ dwt_comp_[n]) & ~mask) != 0) continue;
            (void)size; (void)v;
            dwt_func_[n] |= 1u << 24;                  // MATCHED
            if (dbg_enabled()) dbg_request_halt(DFSR_DWTTRAP);
            return;
        }
    }

    // ---- DWT: contadores de perfil [IR, §13.5.2] ---------------------------
    void dbg_cycles(unsigned cycles, unsigned lsu_extra) override {
        if (!trcena()) { lsu_pend_ = 0; return; }
        if (dwt_ctrl_ & 1u) cyccnt_ += cycles;         // CYCCNTENA
        // CPICNT cuenta los ciclos DE MAS de una instruccion sobre el primero,
        // descontando los de acceso a memoria, que van a LSUCNT.
        const unsigned lsu = (lsu_pend_ > 0) ? (lsu_pend_ - 1u) : 0u;
        const unsigned extra = (cycles > 1u) ? (cycles - 1u) : 0u;
        if (dwt_ctrl_ & (1u << 17)) suma8(cpicnt_, extra > lsu ? extra - lsu : 0u);
        if (dwt_ctrl_ & (1u << 20)) suma8(lsucnt_, lsu + lsu_extra);
        lsu_pend_ = 0;
    }
    void dbg_exception(int excp, bool entry) override {
        (void)excp;
        if (!trcena()) return;
        if (entry && (dwt_ctrl_ & (1u << 18))) suma8(exccnt_, 12u);
    }
    void dbg_sleep(unsigned cycles) override {
        if (trcena() && (dwt_ctrl_ & (1u << 19))) suma8(sleepcnt_, cycles);
    }

    // ---- Captura de vectores y reset [IR, §13.4.3] -------------------------
    bool dbg_vector_catch(int excp) override {
        if (excp == 3 && (demcr_ & (1u << 10))) return true;   // VC_HARDERR
        if (excp == 6 && (demcr_ & (1u << 8)))  return true;   // VC_STATERR
        if (excp == 4 && (demcr_ & (1u << 4)))  return true;   // VC_MMERR
        if (excp == 5 && (demcr_ & (1u << 9)))  return true;   // VC_BUSERR
        return false;
    }
    void dbg_reset() override {
        dhcsr_ |= S_RESET_ST_SHADOW;                   // bandera pegajosa
        if (demcr_ & 1u) {                             // VC_CORERESET
            dfsr_local_ |= DFSR_VCATCH;
            dhcsr_ |= C_HALT | C_DEBUGEN;
            actualiza_halt();
        }
    }

private:
    static constexpr uint32_t S_RESET_ST_SHADOW = 1u << 30;   // uso interno
    static constexpr unsigned N_CODE = 6;   // comparadores de instrucción
    static constexpr unsigned N_LIT  = 2;   // comparadores literales
    static constexpr unsigned N_DWT  = 4;   // comparadores de watchpoint

    // ---- Core debug --------------------------------------------------------
    uint32_t dhcsr_ = 0, dcrsr_ = 0, dcrdr_ = 0, demcr_ = 0;
    uint32_t dfsr_local_ = 0;
    bool     paso_ = false;
    sc_core::sc_event wake_, pub_ev_, fz_ev_, swo_ev_;
    bool     o_halt_ = false;

    // ---- FPB ---------------------------------------------------------------
    uint32_t fp_ctrl_ = 0x00000260u;                   // 6 code + 2 literal
    uint32_t fp_remap_ = 0x20000000u;
    std::array<uint32_t, N_CODE + N_LIT> fp_comp_{};
    uint64_t n_bkpt_ = 0, n_remap_ = 0;

    // ---- DWT ---------------------------------------------------------------
    uint32_t dwt_ctrl_ = 0x40000000u;                  // NUMCOMP = 4
    uint32_t cyccnt_ = 0;
    uint32_t cpicnt_ = 0, exccnt_ = 0, sleepcnt_ = 0, lsucnt_ = 0, foldcnt_ = 0;
    std::array<uint32_t, N_DWT> dwt_comp_{}, dwt_mask_{}, dwt_func_{};
    unsigned lsu_pend_ = 0;
    static void suma8(uint32_t& c, unsigned n) { c = (c + n) & 0xFFu; }

    // ---- ITM / TPIU --------------------------------------------------------
    uint32_t itm_ter_ = 0, itm_tpr_ = 0, itm_tcr_ = 0, itm_lar_ = 0;
    bool     itm_unlocked_ = false;
    uint32_t tpiu_acpr_ = 0, tpiu_sppr_ = 1, tpiu_ffcr_ = 0x0102u, tpiu_cspsr_ = 1;
    std::deque<uint8_t> tpiu_fifo_;
    uint64_t n_swo_ = 0;
    bool     o_swo_ = true;

    // ---- DBGMCU ------------------------------------------------------------
    uint32_t dbgmcu_cr_ = 0, dbg_apb1_fz_ = 0, dbg_apb2_fz_ = 0;

    // ---- SWJ-DP ------------------------------------------------------------
    uint64_t n_swd_ = 0;
    bool     o_swdio_ = true, o_swdio_oe_ = false;
    // Pines expuestos (sonda externa) o reservados sin usar (stub interno).
    uint32_t idcode_ = 0x10016413u;   // F405/407/415/417, rev. 1001
    bool     pines_dbg_ = true;

    bool trcena() const { return (demcr_ & (1u << 24)) != 0; }

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        dbg_lp.write(uint8_t(dbgmcu_cr_ & 0x7u));
        halt_req.write(o_halt_);
        swdio_out.write(o_swdio_);
        swdio_oe.write(o_swdio_oe_);
        jtdo_swo.write(o_swo_);
    }
    void actualiza_halt() {
        const bool h = dbg_halt_now();
        if (h != o_halt_) { o_halt_ = h; publish(); }
        wake_.notify(sc_core::SC_ZERO_TIME);
    }

    // -----------------------------------------------------------------------
    // DBGMCU: la congelación de periféricos [IR, §13.9]
    //
    // Es lo que evita que un temporizador o un perro guardián sigan corriendo
    // mientras el depurador tiene el núcleo parado, y por tanto lo que evita
    // que un timeout salte artificialmente en mitad de una sesión.
    // -----------------------------------------------------------------------
    void freeze_proc() {
        const bool h = halted.read();
        static const int apb1[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12,
                                   21, 22, 23, 25, 26};
        static const int apb2[] = {0, 1, 16, 17, 18};
        for (unsigned i = 0; i < 16; ++i)
            freeze[i].write(h && ((dbg_apb1_fz_ >> apb1[i]) & 1u));
        for (unsigned i = 0; i < 5; ++i)
            freeze[16 + i].write(h && ((dbg_apb2_fz_ >> apb2[i]) & 1u));
    }

    // =======================================================================
    // Banco de registros del PPB de depuración
    // =======================================================================
    uint32_t leer_dhcsr() const {
        uint32_t v = dhcsr_ & 0x0000002Fu;
        v |= S_REGRDY;                                 // el acceso es inmediato
        if (halted.read()) v |= S_HALT;
        if (cpu && cpu->halted_on_lockup) v |= S_LOCKUP;
        if (dhcsr_ & S_RESET_ST_SHADOW) v |= S_RESET_ST;
        return v;
    }

    // --- Acceso a los registros del núcleo por DCRSR/DCRDR [IR, §13.4.2] ----
    uint32_t leer_reg_nucleo(unsigned sel) const {
        if (!cpu_reg) return 0;
        const RegFile& r = *cpu_reg;
        if (sel <= 12) return r.r[sel];
        switch (sel) {
            case 13: return r.r[13];                   // SP en uso
            case 14: return r.r[14];
            case 15: return cpu ? cpu->dbg_pc() : r.r[15];
            case 16: return r.xpsr;
            case 17: return r.msp;
            case 18: return r.psp;
            case 20: return (uint32_t(r.control) << 24) | (uint32_t(r.faultmask) << 16) |
                            (uint32_t(r.basepri) << 8) | uint32_t(r.primask);
            default: return 0;
        }
    }
    void escribir_reg_nucleo(unsigned sel, uint32_t v) {
        if (!cpu_reg) return;
        RegFile& r = *cpu_reg;
        if (sel <= 12) { r.r[sel] = v; return; }
        switch (sel) {
            case 13: r.r[13] = v & ~3u; break;
            case 14: r.r[14] = v; break;
            case 15: if (cpu) cpu->dbg_set_pc(v); break;
            case 16: r.xpsr = v; break;
            case 17: r.msp = v & ~3u; break;
            case 18: r.psp = v & ~3u; break;
            case 20: r.control  = uint8_t(v >> 24);
                     r.faultmask = uint8_t(v >> 16) & 1u;
                     r.basepri  = uint8_t(v >> 8);
                     r.primask  = uint8_t(v) & 1u;
                     break;
            default: break;
        }
    }

    uint32_t leer(uint32_t a) {
        // --- Core debug ------------------------------------------------------
        if (a == R_DHCSR) {
            const uint32_t v = leer_dhcsr();
            dhcsr_ &= ~S_RESET_ST_SHADOW;              // S_RESET_ST se borra al leer
            return v;
        }
        if (a == R_DCRSR) return 0;                    // solo escritura
        if (a == R_DCRDR) return dcrdr_;
        if (a == R_DEMCR) return demcr_;

        // --- ITM -------------------------------------------------------------
        if (a >= B_ITM && a < B_ITM + 0x1000u) {
            const uint32_t o = a - B_ITM;
            if (o < 0x80u) return itm_listo() ? 1u : 0u;   // FIFO lista
            switch (o) {
                case 0xE00: return itm_ter_;
                case 0xE40: return itm_tpr_;
                case 0xE80: return itm_tcr_ | (tpiu_fifo_.empty() ? 0u : (1u << 23));
                case 0xFB0: return 0;
                case 0xFB4: return itm_unlocked_ ? 0x00000001u : 0x00000003u;
                case 0xFCC: return 0x0000000Bu;
                default:    return id_componente(o, 0x11, 0x00, 0x14);
            }
        }
        // --- DWT -------------------------------------------------------------
        if (a >= B_DWT && a < B_DWT + 0x1000u) {
            const uint32_t o = a - B_DWT;
            switch (o) {
                case 0x00: return dwt_ctrl_;
                case 0x04: return cyccnt_;
                case 0x08: return cpicnt_;
                case 0x0C: return exccnt_;
                case 0x10: return sleepcnt_;
                case 0x14: return lsucnt_;
                case 0x18: return foldcnt_;
                case 0x1C: return cpu ? cpu->dbg_pc() : 0;   // PCSR
                default: break;
            }
            if (o >= 0x20 && o < 0x20 + 0x10 * N_DWT) {
                const unsigned n = (o - 0x20) / 0x10, k = (o - 0x20) % 0x10;
                if (k == 0) return dwt_comp_[n];
                if (k == 4) return dwt_mask_[n];
                if (k == 8) return dwt_func_[n];
                return 0;
            }
            return id_componente(o, 0x02, 0x00, 0x14);
        }
        // --- FPB -------------------------------------------------------------
        if (a >= B_FPB && a < B_FPB + 0x1000u) {
            const uint32_t o = a - B_FPB;
            if (o == 0x00) return fp_ctrl_;
            if (o == 0x04) return fp_remap_ | (1u << 29);    // RMPSPT
            if (o >= 0x08 && o < 0x08 + 4 * (N_CODE + N_LIT))
                return fp_comp_[(o - 0x08) / 4];
            return id_componente(o, 0x03, 0x00, 0x14);
        }
        // --- TPIU ------------------------------------------------------------
        if (a >= B_TPIU && a < B_TPIU + 0x1000u) {
            const uint32_t o = a - B_TPIU;
            switch (o) {
                case 0x000: return 0x0000000Fu;        // SSPSR: anchos 1..4
                case 0x004: return tpiu_cspsr_;
                case 0x010: return tpiu_acpr_;
                case 0x0F0: return tpiu_sppr_;
                case 0x304: return tpiu_ffcr_;
                default:    return id_componente(o, 0x41, 0x09, 0x14);
            }
        }
        // --- ETM (presente en la ROM table, sin implementar) ------------------
        if (a >= B_ETM && a < B_ETM + 0x1000u)
            return id_componente(a - B_ETM, 0x42, 0x09, 0x14);
        // --- DBGMCU ----------------------------------------------------------
        if (a >= B_DBGMCU && a < B_DBGMCU + 0x1000u) {
            switch (a - B_DBGMCU) {
                case 0x00: return idcode_;             // quien es este chip
                case 0x04: return dbgmcu_cr_;
                case 0x08: return dbg_apb1_fz_;
                case 0x0C: return dbg_apb2_fz_;
                default:   return 0;
            }
        }
        // --- ROM table [IR, §13.3] -------------------------------------------
        if (a >= B_ROM && a < B_ROM + 0x1000u) return rom_table(a - B_ROM);
        return 0;
    }

    // Los cuatro registros de identificación que toda tabla ROM de CoreSight
    // espera encontrar al final de cada bloque de 4 KiB.
    static uint32_t id_componente(uint32_t o, uint8_t part_lo, uint8_t part_hi,
                                  uint8_t clase) {
        switch (o) {
            case 0xFE0: return part_lo;                // PID0
            case 0xFE4: return uint32_t(0xE0u | part_hi);   // PID1: JEP106 ARM
            case 0xFE8: return 0x0000000Bu;            // PID2
            case 0xFEC: return 0x00000000u;            // PID3
            case 0xFD0: return 0x00000004u;            // PID4
            case 0xFF0: return 0x0000000Du;            // CID0
            case 0xFF4: return uint32_t(clase) << 0;   // CID1 (clase 9 o 14)
            case 0xFF8: return 0x00000005u;            // CID2
            case 0xFFC: return 0x000000B1u;            // CID3
            default:    return 0;
        }
    }
    // La tabla ROM: una entrada por componente, con su desplazamiento relativo
    // a la propia tabla y el bit de presencia [IR, §13.3].
    static uint32_t rom_table(uint32_t o) {
        switch (o) {
            case 0x000: return 0xFFF0F003u;            // SCS
            case 0x004: return 0xFFF02003u;            // DWT
            case 0x008: return 0xFFF03003u;            // FPB
            case 0x00C: return 0xFFF01003u;            // ITM
            case 0x010: return 0xFFF41003u;            // TPIU
            case 0x014: return 0xFFF42003u;            // ETM
            case 0x018: return 0x00000000u;            // fin de la tabla
            case 0xFCC: return 0x00000001u;            // MEMTYPE: hay memoria
            default:    return id_componente(o, 0x00, 0x00, 0x10);
        }
    }

    void escribir(uint32_t a, uint32_t v) {
        // --- Core debug ------------------------------------------------------
        if (a == R_DHCSR) {
            // La LLAVE: sin 0xA05F en la parte alta, la escritura se ignora
            // entera. Es lo que impide que un firmware descarrilado se pare a
            // si mismo por accidente [IR, §13.4.1].
            if ((v >> 16) != DBGKEY) return;
            const bool antes = dbg_halt_now();
            dhcsr_ = (dhcsr_ & ~0x0000002Fu) | (v & 0x0000002Fu);
            // C_STEP con C_HALT a cero: una instruccion y vuelta a parar.
            if ((v & C_STEP) && (v & C_DEBUGEN) && !(v & C_HALT)) {
                paso_ = true;
                dhcsr_ |= C_HALT;                      // sigue formalmente parado
                dfsr_local_ |= DFSR_HALTED;
                if (cpu) cpu->sys->set_dfsr(DFSR_HALTED);
            }
            if (!antes && dbg_halt_now()) {
                dfsr_local_ |= DFSR_HALTED;
                if (cpu) cpu->sys->set_dfsr(DFSR_HALTED);
            }
            actualiza_halt();
            return;
        }
        if (a == R_DCRSR) {
            dcrsr_ = v;
            const unsigned sel = v & 0x7Fu;
            if (v & (1u << 16)) escribir_reg_nucleo(sel, dcrdr_);
            else                dcrdr_ = leer_reg_nucleo(sel);
            return;
        }
        if (a == R_DCRDR) { dcrdr_ = v; return; }
        if (a == R_DEMCR) {
            demcr_ = v & 0x010F07F1u;
            if (!trcena()) { tpiu_fifo_.clear(); }
            return;
        }

        // --- ITM -------------------------------------------------------------
        if (a >= B_ITM && a < B_ITM + 0x1000u) {
            const uint32_t o = a - B_ITM;
            if (o < 0x80u) { itm_estimulo(o / 4u, v, ultimo_tam_); return; }
            switch (o) {
                case 0xE00: if (itm_unlocked_) itm_ter_ = v; return;
                case 0xE40: if (itm_unlocked_) itm_tpr_ = v & 0xFu; return;
                case 0xE80: if (itm_unlocked_) itm_tcr_ = v & 0x0001007Fu; return;
                // El candado CoreSight: hasta que no se escribe 0xC5ACCE55 en
                // LAR, el ITM no acepta configuracion. Es lo primero que hace
                // cualquier driver de traza.
                case 0xFB0: itm_lar_ = v; itm_unlocked_ = (v == 0xC5ACCE55u); return;
                default: return;
            }
        }
        // --- DWT -------------------------------------------------------------
        if (a >= B_DWT && a < B_DWT + 0x1000u) {
            const uint32_t o = a - B_DWT;
            switch (o) {
                case 0x00: dwt_ctrl_ = (dwt_ctrl_ & 0xF0000000u) | (v & 0x0FFFFFFFu); return;
                case 0x04: cyccnt_ = v; return;
                case 0x08: cpicnt_ = v & 0xFFu; return;
                case 0x0C: exccnt_ = v & 0xFFu; return;
                case 0x10: sleepcnt_ = v & 0xFFu; return;
                case 0x14: lsucnt_ = v & 0xFFu; return;
                case 0x18: foldcnt_ = v & 0xFFu; return;
                default: break;
            }
            if (o >= 0x20 && o < 0x20 + 0x10 * N_DWT) {
                const unsigned n = (o - 0x20) / 0x10, k = (o - 0x20) % 0x10;
                if (k == 0) dwt_comp_[n] = v;
                else if (k == 4) dwt_mask_[n] = v & 0x1Fu;
                else if (k == 8) dwt_func_[n] = v & 0x0FFFFFFFu;
            }
            return;
        }
        // --- FPB -------------------------------------------------------------
        if (a >= B_FPB && a < B_FPB + 0x1000u) {
            const uint32_t o = a - B_FPB;
            if (o == 0x00) {
                // Otra llave: ENABLE solo cambia si se escribe KEY a la vez
                // [IR, §13.7.2].
                if (v & 2u) fp_ctrl_ = (fp_ctrl_ & ~1u) | (v & 1u);
                return;
            }
            // REMAP son los bits [28:5] de la direccion de destino, y el bit
            // 29 es RMPSPT de solo lectura. Como la tabla de parcheo vive en
            // la SRAM -que empieza justo en 2^29- el modelo guarda la
            // direccion ENTERA alineada a 32 bytes: es lo mismo que el campo
            // del silicio mas el bit de region, y evita el absurdo de un campo
            // que no alcanza la memoria a la que apunta [IR, §13.7.2].
            if (o == 0x04) { fp_remap_ = v & 0x3FFFFFE0u; return; }
            if (o >= 0x08 && o < 0x08 + 4 * (N_CODE + N_LIT)) {
                fp_comp_[(o - 0x08) / 4] = v & 0xDFFFFFFFu;
                return;
            }
            return;
        }
        // --- TPIU ------------------------------------------------------------
        if (a >= B_TPIU && a < B_TPIU + 0x1000u) {
            switch (a - B_TPIU) {
                case 0x004: tpiu_cspsr_ = v; return;
                case 0x010: tpiu_acpr_ = v & 0xFFFFu; return;
                case 0x0F0: tpiu_sppr_ = v & 3u; return;
                case 0x304: tpiu_ffcr_ = v & 0x0000030Au; return;
                default: return;
            }
        }
        // --- DBGMCU ----------------------------------------------------------
        if (a >= B_DBGMCU && a < B_DBGMCU + 0x1000u) {
            switch (a - B_DBGMCU) {
                case 0x04: dbgmcu_cr_ = v & 0x000000E7u; publish(); return;
                case 0x08: dbg_apb1_fz_ = v; fz_ev_.notify(sc_core::SC_ZERO_TIME); return;
                case 0x0C: dbg_apb2_fz_ = v; fz_ev_.notify(sc_core::SC_ZERO_TIME); return;
                default: return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // ITM: el empaquetado CoreSight [IR, §13.6]
    //
    // Un paquete de fuente software lleva una cabecera con el numero de puerto
    // y el tamano, seguida de la carga con el byte menos significativo por
    // delante. Es lo que un decodificador de SWO espera encontrar, y por eso
    // se genera literalmente asi.
    // -----------------------------------------------------------------------
    unsigned ultimo_tam_ = 4;
    bool itm_listo() const {
        return (itm_tcr_ & 1u) && trcena() && tpiu_fifo_.size() < 64;
    }
    void itm_estimulo(unsigned puerto, uint32_t v, unsigned tam) {
        if (!(itm_tcr_ & 1u) || !trcena()) return;     // ITMENA y TRCENA
        if (puerto >= 32 || !((itm_ter_ >> puerto) & 1u)) return;
        const unsigned s = (tam == 1) ? 1u : (tam == 2) ? 2u : 3u;
        tpiu_fifo_.push_back(uint8_t((puerto << 3) | s));
        const unsigned nb = (s == 3) ? 4u : s;
        for (unsigned i = 0; i < nb; ++i) tpiu_fifo_.push_back(uint8_t(v >> (8 * i)));
        swo_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // -----------------------------------------------------------------------
    // TPIU: el serializador hacia SWO [IR, §13.8]
    //
    // En modo NRZ (SPPR = 10) el pin SWO es literalmente una linea serie
    // asincrona: bit de arranque, ocho de dato con el menos significativo por
    // delante y bit de parada, a f_SWO = HCLK/(ACPR+1). Es lo que decodifica
    // cualquier analizador de traza.
    // -----------------------------------------------------------------------
    void swo_proc() {
        o_swo_ = true; publish();
        for (;;) {
            // Con los pines RESERVADOS (stub interno) el SWO no emite: el pin
            // sigue siendo del puerto de depuracion, pero en reposo. La FIFO
            // del TPIU se vacia igualmente para que el ITM no se atasque.
            if (!pines_dbg_) {
                if (!o_swo_) { o_swo_ = true; publish(); }
                tpiu_fifo_.clear();
                wait(swo_ev_);
                continue;
            }
            if (tpiu_fifo_.empty() || tpiu_sppr_ != 2u || !trcena()) {
                if (!o_swo_) { o_swo_ = true; publish(); }
                wait(swo_ev_);
                continue;
            }
            const double f = hclk_hz.read();
            if (f <= 0.0) { wait(hclk_hz.value_changed_event() | swo_ev_); continue; }
            const double tb = double(tpiu_acpr_ + 1u) / f;
            const uint8_t b = tpiu_fifo_.front();
            tpiu_fifo_.pop_front();
            auto bit = [&](bool nivel) {
                o_swo_ = nivel; publish();
                wait(sc_core::sc_time(tb, sc_core::SC_SEC));
            };
            bit(false);                                // arranque
            for (unsigned i = 0; i < 8; ++i) bit(((b >> i) & 1u) != 0);
            bit(true);                                 // parada
            ++n_swo_;
        }
    }

    // =======================================================================
    // Transporte del PPB
    // =======================================================================
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        if (gp.is_read()) {
            unsigned i = 0;
            while (i < len) {
                const uint32_t wa = (a + i) & ~3u;
                const uint32_t w = leer(wa);
                while (i < len && ((a + i) & ~3u) == wa) {
                    d[i] = uint8_t(w >> (8u * ((a + i) & 3u)));
                    ++i;
                }
            }
        } else {
            unsigned i = 0;
            while (i < len) {
                const uint32_t wa = (a + i) & ~3u;
                uint32_t v = 0; unsigned n = 0, first = i;
                while (i < len && ((a + i) & ~3u) == wa) {
                    v |= uint32_t(d[i]) << (8u * ((a + i) & 3u));
                    ++n; ++i;
                }
                // El ITM distingue el TAMAÑO del acceso: escribir un byte, una
                // media palabra o una palabra en un puerto de estimulo genera
                // paquetes distintos [IR, §13.6.1].
                ultimo_tam_ = n;
                if (n < 4) v >>= 8u * ((a + first) & 3u);
                escribir(wa, v);
            }
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // =======================================================================
    // SWJ-DP: el protocolo SWD, bit a bit sobre los pines [IR, §13.1, §13.2]
    //
    // El anfitrion gobierna SWDIO en el flanco de BAJADA de SWCLK y el objetivo
    // la muestrea en el de SUBIDA; cuando contesta, los papeles se invierten y
    // en medio hay un ciclo de TURNAROUND en el que nadie gobierna la linea.
    // Toda la secuencia esta aqui: reset de linea, conmutacion desde JTAG,
    // paquete de peticion con su paridad, ACK y palabra de datos.
    // =======================================================================
    enum SwdSt { SW_IDLE, SW_REQ, SW_ACK, SW_RD, SW_WR };
    SwdSt    sw_st_ = SW_IDLE;
    unsigned sw_n_ = 0;                    // flancos de subida dentro del paquete
    unsigned sw_unos_ = 0;                 // racha de unos: reset de linea
    uint16_t sw_switch_ = 0;               // registro de la secuencia 0xE79E
    bool     sw_modo_swd_ = true;
    // Tras cada paquete hace falta ver al menos un ciclo de REPOSO (linea a
    // cero) antes de admitir el bit de arranque del siguiente. Sin esto, el
    // uno que deja el pull-up durante el turnaround se tomaria por un arranque
    // y toda la sesion se desincronizaria.
    bool     sw_espera_idle_ = true;
    uint8_t  sw_req_ = 0;
    uint32_t sw_dato_ = 0;
    uint8_t  sw_ack_ = 1;                  // OK
    bool     sw_par_ = false;
    bool     sw_lectura_ = false, sw_ap_ = false;
    unsigned sw_addr_ = 0;

    // Registros del DP y del AHB-AP
    uint32_t dp_select_ = 0, dp_ctrl_ = 0, dp_rdbuff_ = 0;
    uint32_t ap_csw_ = 0x23000042u, ap_tar_ = 0, ap_drw_ = 0;
    bool     ap_pend_ = false;             // hay una lectura de AP en el buffer

    static bool paridad(uint32_t v) {
        unsigned n = 0;
        for (unsigned i = 0; i < 32; ++i) n += (v >> i) & 1u;
        return (n & 1u) != 0;
    }

    void swd_proc() {
        bool prev = swclk_tck.read();
        for (;;) {
            wait(swclk_tck.value_changed_event());
            const bool c = swclk_tck.read();
            // Pines reservados pero no usados: el frente SWD no escucha. Lo
            // que llegue por SWCLK/SWDIO se ignora y SWDIO se queda en alta
            // impedancia, como si no hubiera nadie soldado.
            if (!pines_dbg_) { prev = c; continue; }
            if (c && !prev)      swd_subida();
            else if (!c && prev) swd_bajada();
            prev = c;
        }
    }

    // El objetivo MUESTREA en el flanco de subida.
    void swd_subida() {
        const bool b = swdio_in.read();
        // Reset de linea: cincuenta o mas unos seguidos.
        if (b) { if (sw_unos_ < 100) ++sw_unos_; }
        else   { if (sw_unos_ >= 50) { sw_st_ = SW_IDLE; sw_n_ = 0; sw_espera_idle_ = false; }
                 sw_unos_ = 0; }
        // La secuencia de conmutacion JTAG->SWD es la palabra 0xE79E enviada
        // entre dos resets de linea [IR, §13.2].
        sw_switch_ = uint16_t((sw_switch_ >> 1) | (b ? 0x8000u : 0u));
        if (sw_switch_ == 0xE79Eu) {
            sw_modo_swd_ = true; sw_st_ = SW_IDLE; sw_n_ = 0; sw_espera_idle_ = true;
        }

        switch (sw_st_) {
            case SW_IDLE:
                if (!b) { sw_espera_idle_ = false; return; }
                // El bit de arranque de un paquete es un uno tras el reposo.
                if (!sw_espera_idle_ && sw_unos_ == 1) {
                    sw_st_ = SW_REQ; sw_n_ = 1; sw_req_ = 1;
                }
                return;
            case SW_REQ:
                ++sw_n_;
                sw_req_ = uint8_t(sw_req_ | (b ? (1u << (sw_n_ - 1)) : 0u));
                if (sw_n_ == 8) decodifica_peticion();
                return;
            case SW_ACK:
                ++sw_n_;                                // el anfitrion escucha
                return;
            case SW_RD:
                ++sw_n_;
                return;
            case SW_WR:
                ++sw_n_;
                // r = 14..45: los 32 bits de datos; r = 46: la paridad.
                if (sw_n_ >= 14 && sw_n_ <= 45) {
                    if (b) sw_dato_ |= 1u << (sw_n_ - 14);
                } else if (sw_n_ == 46) {
                    if (paridad(sw_dato_) == b) ejecuta_escritura();
                    sw_st_ = SW_IDLE; sw_n_ = 0; sw_espera_idle_ = true;
                }
                return;
        }
    }

    // El objetivo GOBIERNA en el flanco de bajada, preparando el bit que el
    // anfitrion muestreara en la subida siguiente.
    void swd_bajada() {
        const unsigned r = sw_n_ + 1;                  // proximo flanco de subida
        switch (sw_st_) {
            case SW_ACK:
                // r = 9: turnaround (nadie gobierna); r = 10..12: el ACK.
                if (r >= 10 && r <= 12) drive(( (sw_ack_ >> (r - 10)) & 1u) != 0);
                else if (r == 13) {
                    // Con un ACK que no sea OK, la FASE DE DATOS NO OCURRE: el
                    // paquete termina en el propio ACK. Seguir soltando bits
                    // seria pelearse con el anfitrion, que ya ha dado la
                    // transaccion por terminada [ADIv5].
                    if (sw_ack_ != 1u) {
                        suelta(); sw_st_ = SW_IDLE; sw_n_ = 0; sw_espera_idle_ = true;
                    } else if (sw_lectura_) {
                        drive(((sw_dato_ >> 0) & 1u) != 0); sw_st_ = SW_RD;
                    } else {
                        suelta(); sw_st_ = SW_WR;
                    }
                } else suelta();
                return;
            case SW_RD:
                // r = 13..44: datos; r = 45: paridad; r = 46: turnaround.
                if (r >= 13 && r <= 44) drive(((sw_dato_ >> (r - 13)) & 1u) != 0);
                else if (r == 45) drive(paridad(sw_dato_));
                else { suelta(); sw_st_ = SW_IDLE; sw_n_ = 0; sw_espera_idle_ = true; }
                return;
            default:
                suelta();
                return;
        }
    }
    void drive(bool v) {
        if (!o_swdio_oe_ || o_swdio_ != v) { o_swdio_oe_ = true; o_swdio_ = v; publish(); }
    }
    void suelta() {
        if (o_swdio_oe_) { o_swdio_oe_ = false; publish(); }
    }

    // -----------------------------------------------------------------------
    // El paquete de peticion: arranque, APnDP, RnW, A[2:3], paridad, parada y
    // park. Si la paridad no cuadra, el objetivo no contesta.
    // -----------------------------------------------------------------------
    void decodifica_peticion() {
        const bool apndp = (sw_req_ >> 1) & 1u;
        const bool rnw   = (sw_req_ >> 2) & 1u;
        const unsigned a = (sw_req_ >> 3) & 3u;        // A[2:3]
        const bool par   = (sw_req_ >> 5) & 1u;
        unsigned n = 0;
        for (unsigned i = 1; i <= 4; ++i) n += (sw_req_ >> i) & 1u;
        if (((n & 1u) != 0) != par || !sw_modo_swd_) {
            sw_st_ = SW_IDLE; sw_n_ = 0; sw_espera_idle_ = true; suelta(); return;
        }
        sw_ap_ = apndp; sw_lectura_ = rnw; sw_addr_ = a * 4u;
        sw_ack_ = 1;                                   // OK
        sw_dato_ = 0;
        ++n_swd_;
        // La lectura se resuelve YA, porque de ella puede salir un FAULT que
        // hay que anunciar en el propio ACK. En una escritura el ACK va por
        // delante del dato, asi que el bloqueo se comprueba aqui.
        if (rnw) sw_dato_ = ejecuta_lectura();
        else if (sw_ap_ && ap_bloqueado()) sw_ack_ = 4;
        else if (sw_ap_ && ap_ocupado()) { sw_ack_ = 2; ++n_wait_; }
        sw_st_ = SW_ACK;
    }

    // --- Registros del DP y del AHB-AP --------------------------------------
    //
    // Las tres peculiaridades de ADIv5 que una sonda TIENE que respetar y que
    // por tanto estan modeladas:
    //   1. Sin las peticiones de encendido (CDBGPWRUPREQ y CSYSPWRUPREQ) y sus
    //      acuses, el AP NO responde: contesta FAULT.
    //   2. Un acceso del AP que falla en el bus deja PEGADO el bit STICKYERR de
    //      CTRL/STAT, y a partir de ahi TODOS los accesos al AP contestan FAULT
    //      hasta que se limpia escribiendo en ABORT.
    //   3. El auto-incremento de TAR no cruza la frontera de 1 KiB.
    static constexpr uint32_t ST_STICKYORUN = 1u << 1;
    static constexpr uint32_t ST_STICKYCMP  = 1u << 4;
    static constexpr uint32_t ST_STICKYERR  = 1u << 5;
    static constexpr uint32_t ST_READOK     = 1u << 6;
    static constexpr uint32_t ST_WDATAERR   = 1u << 7;
    bool ap_encendido() const {
        // CDBGPWRUPREQ (28) y CSYSPWRUPREQ (30) pedidos.
        return (dp_ctrl_ & (1u << 28)) && (dp_ctrl_ & (1u << 30));
    }
    bool ap_bloqueado() const {
        return !ap_encendido() || (dp_ctrl_ & ST_STICKYERR) != 0;
    }
    // Mientras el acceso anterior sigue en vuelo por el bus, el DAP contesta
    // WAIT. Es la razon de ser de ese ACK, y lo que obliga a toda sonda a
    // reintentar en vez de darse por vencida.
    bool ap_ocupado() const { return sc_core::sc_time_stamp() < ap_libre_; }
    sc_core::sc_time ap_libre_{sc_core::SC_ZERO_TIME};
    uint64_t n_wait_ = 0;

    // Un acceso del AP a 32 bits que NO bloquea el hilo del SWD: apunta cuando
    // quedara libre y sigue.
    tlm::tlm_response_status ap_nb(bool write, uint32_t a, uint32_t& v) {
        unsigned char b[4];
        if (write) for (unsigned i = 0; i < 4; ++i) b[i] = uint8_t(v >> (8 * i));
        else       for (unsigned i = 0; i < 4; ++i) b[i] = 0;
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        const auto r = ap_access_nb(write, a, b, 4, t);
        ap_libre_ = sc_core::sc_time_stamp() + t;
        if (!write) v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
                        (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        return r;
    }

    uint32_t ejecuta_lectura() {
        if (!sw_ap_) {
            switch (sw_addr_) {
                case 0x0: return 0x2BA01477u;          // IDCODE del SW-DP del M4
                // Los acuses de encendido (bits 29 y 31) siguen a sus
                // peticiones (28 y 30): es lo primero que comprueba una sonda.
                case 0x4: return dp_ctrl_ | ((dp_ctrl_ & 0x50000000u) << 1);
                case 0x8: return dp_rdbuff_;           // RESEND
                default:  return dp_rdbuff_;           // RDBUFF
            }
        }
        if (ap_bloqueado()) { sw_ack_ = 4; return dp_rdbuff_; }   // FAULT
        if (ap_ocupado()) { sw_ack_ = 2; ++n_wait_; return dp_rdbuff_; }  // WAIT
        // Las lecturas del AP van con un ciclo de retraso: la que se pide ahora
        // se recoge en la siguiente, o en RDBUFF. Es el comportamiento real del
        // DAP, y el que espera cualquier sonda.
        const uint32_t previo = dp_rdbuff_;
        const uint32_t reg = (dp_select_ & 0xF0u) | sw_addr_;
        uint32_t v = 0;
        // APSEL. Solo hay un AP; los demas leen ceros, que es como una sonda
        // averigua cuantos hay. Sin esto, el AP 7 contestaba lo mismo que el 0
        // y una enumeracion no terminaria nunca.
        if ((dp_select_ >> 24) != 0) {
            dp_rdbuff_ = 0;
            ap_pend_ = true;
            return previo;
        }
        switch (reg) {
            case 0x00: v = ap_csw_; break;
            case 0x04: v = ap_tar_; break;
            case 0x0C: {
                uint32_t d = 0;
                const auto r = ap_nb(false, ap_tar_, d);
                if (r != tlm::TLM_OK_RESPONSE) dp_ctrl_ |= ST_STICKYERR;
                v = d;
                incrementa_tar();
                break;
            }
            case 0xF8: v = AP_BASE; break;             // ROM table base
            case 0xFC: v = AP_IDR;  break;             // IDR del AHB-AP
            default:   v = 0; break;
        }
        dp_rdbuff_ = v;
        ap_pend_ = true;
        return previo;
    }

    void ejecuta_escritura() {
        if (!sw_ap_) {
            switch (sw_addr_) {
                case 0x0:
                    // ABORT: es el UNICO camino para limpiar los bits pegajosos
                    // de CTRL/STAT. Sin el, un acceso fallido deja al DAP mudo
                    // para siempre, y esa es la trampa clasica de una sonda mal
                    // escrita.
                    if (sw_dato_ & (1u << 1)) dp_ctrl_ &= ~ST_STICKYCMP;
                    if (sw_dato_ & (1u << 2)) dp_ctrl_ &= ~ST_STICKYERR;
                    if (sw_dato_ & (1u << 3)) dp_ctrl_ &= ~ST_WDATAERR;
                    if (sw_dato_ & (1u << 4)) dp_ctrl_ &= ~ST_STICKYORUN;
                    return;
                case 0x4:
                    // Los bits pegajosos NO se escriben desde aqui.
                    dp_ctrl_ = (dp_ctrl_ & 0x000000F2u) |
                               (sw_dato_ & ~0x000000F2u);
                    return;
                case 0x8: dp_select_ = sw_dato_; return;
                default:  return;
            }
        }
        if (ap_bloqueado()) { sw_ack_ = 4; return; }               // FAULT
        if (ap_ocupado()) { sw_ack_ = 2; ++n_wait_; return; }       // WAIT
        const uint32_t reg = (dp_select_ & 0xF0u) | sw_addr_;
        if ((dp_select_ >> 24) != 0) return;       // APSEL: no hay mas APs
        switch (reg) {
            case 0x00: ap_csw_ = (ap_csw_ & 0xFFFFFF00u) | (sw_dato_ & 0xFFu); return;
            case 0x04: ap_tar_ = sw_dato_; return;
            case 0x0C: {
                uint32_t d = sw_dato_;
                const auto r = ap_nb(true, ap_tar_, d);
                if (r != tlm::TLM_OK_RESPONSE) dp_ctrl_ |= ST_STICKYERR;
                incrementa_tar();
                return;
            }
            default:   return;
        }
    }
    // CSW.AddrInc: el auto-incremento de TAR es lo que permite volcar un bloque
    // de memoria sin reescribir la direccion en cada palabra. Y su limite: NO
    // cruza la frontera de 1 KiB, asi que una sonda tiene que reescribir TAR en
    // cada una. Es de las cosas que mas quebraderos de cabeza dan al escribir
    // un programador desde cero.
    void incrementa_tar() {
        const unsigned inc = (ap_csw_ >> 4) & 3u;
        if (inc != 1) return;
        const uint32_t paso = 1u << (ap_csw_ & 3u);
        const uint32_t sig = ap_tar_ + paso;
        if ((sig & ~0x3FFu) != (ap_tar_ & ~0x3FFu)) return;   // frontera de 1 KiB
        ap_tar_ = sig;
    }

public:
    // Reset del subsistema: lo llama el top con el reset del sistema.
    void reset_debug() {
        dhcsr_ = 0; dcrsr_ = 0; dcrdr_ = 0; demcr_ = 0;
        fp_ctrl_ = 0x00000260u; fp_remap_ = 0x20000000u; fp_comp_.fill(0);
        dwt_ctrl_ = 0x40000000u; cyccnt_ = 0;
        cpicnt_ = exccnt_ = sleepcnt_ = lsucnt_ = foldcnt_ = 0;
        dwt_comp_.fill(0); dwt_mask_.fill(0); dwt_func_.fill(0);
        itm_ter_ = itm_tpr_ = itm_tcr_ = 0; itm_unlocked_ = false;
        tpiu_acpr_ = 0; tpiu_sppr_ = 1; tpiu_ffcr_ = 0x0102u;
        tpiu_fifo_.clear();
        dbgmcu_cr_ = dbg_apb1_fz_ = dbg_apb2_fz_ = 0;
        o_halt_ = false; paso_ = false;
        publish();
        fz_ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_CORE_DEBUG_H
