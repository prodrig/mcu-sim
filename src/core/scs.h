// =============================================================================
// scs.h — System Control Space: SCB + NVIC + SysTick + MPU (+ regs FPU-SCS)
//
// Bloque de 4 KB en 0xE000E000 accesible solo desde el núcleo (PPB) [IR, §10.1].
// EXTI NO está aquí: es un periférico APB2 (plan P2).
//
// Fase F2 — implementado:
//   * NVIC: 82 IRQ con estados inactive/pending/active, ISER/ICER/ISPR/ICPR/
//     IABR/IPR/STIR, prioridades de 4 bits en [7:4] y agrupación PRIGROUP
//     [IR, §9.1.1, §9.2, §9.7];
//   * SCB: CPUID, ICSR, VTOR, AIRCR, SCR, CCR, SHPR1-3, SHCSR, CFSR, HFSR,
//     DFSR, MMFAR, BFAR, CPACR + FPCCR/FPCAR/FPDSCR [IR, §10.2, §10.5, §8.12];
//   * SysTick: contador descendente de 24 bits con COUNTFLAG y selección de
//     reloj (FCLK o HCLK/8) [IR, §10.3];
//   * MPU: 8 regiones con comprobación por acceso (XN, AP, SRD) [IR, §10.4];
//   * arbitraje de excepciones completo, con enmascaramiento por PRIMASK,
//     FAULTMASK y BASEPRI, y escalado a HardFault [IR, §9.5].
// La CPU accede a todo ello por la interfaz core_sys_if.
// =============================================================================
#ifndef STM32_CORE_SCS_H
#define STM32_CORE_SCS_H

#include <algorithm>
#include <vector>
#include "../common/periph_base.h"
#include "cpu_state.h"
#include "core_caps.h"

namespace stm32 {

// Total de excepciones del F407: 16 de sistema + 82 IRQ [IR, §9.1.2].
//
// Sigue existiendo porque es el número de este chip y hay código —y pruebas—
// que lo nombran, pero YA NO ES EL QUE EL NVIC USA: ese sale de los rasgos con
// los que se construye (`CoreCaps::n_excepciones()`), y con los rasgos por
// omisión vale exactamente esto.
constexpr unsigned N_EXCEPTIONS = 16 + N_IRQ;

// Bits de CFSR [IR, §9.6.1]
enum CfsrBits : uint32_t {
    // MMFSR [7:0]
    MM_IACCVIOL = 1u << 0, MM_DACCVIOL = 1u << 1, MM_MUNSTKERR = 1u << 3,
    MM_MSTKERR = 1u << 4, MM_MLSPERR = 1u << 5, MM_MMARVALID = 1u << 7,
    // BFSR [15:8]
    BF_IBUSERR = 1u << 8, BF_PRECISERR = 1u << 9, BF_IMPRECISERR = 1u << 10,
    BF_UNSTKERR = 1u << 11, BF_STKERR = 1u << 12, BF_LSPERR = 1u << 13,
    BF_BFARVALID = 1u << 15,
    // UFSR [31:16]
    UF_UNDEFINSTR = 1u << 16, UF_INVSTATE = 1u << 17, UF_INVPC = 1u << 18,
    UF_NOCP = 1u << 19, UF_UNALIGNED = 1u << 24, UF_DIVBYZERO = 1u << 25
};

// ---------------------------------------------------------------------------
// Interfaz que la CPU usa para todo lo que vive en el SCS
// ---------------------------------------------------------------------------
class core_sys_if : virtual public sc_core::sc_interface {
public:
    // --- Arbitraje de excepciones [IR, §9.3] --------------------------------
    // Excepción pendiente de mayor prioridad que puede tomarse ahora, o -1.
    virtual int  pending_exception(int current_prio, const RegFile& reg) const = 0;
    virtual void ack_exception(int excp)     = 0;   // pending -> active
    virtual void return_exception(int excp)  = 0;   // active  -> inactive
    virtual void set_pending(int excp, bool p) = 0;
    virtual bool is_pending(int excp) const  = 0;
    virtual bool is_active(int excp) const   = 0;
    virtual int  exception_priority(int excp) const = 0;
    // Prioridad de ejecución actual (la más alta entre activas y máscaras)
    virtual int  execution_priority(const RegFile& reg) const = 0;
    // ¿Hay alguna excepción pendiente (para SEVONPEND / WFE)?
    virtual bool any_pending() const = 0;
    // EL SUCESO QUE PERMITE DORMIR DE VERDAD. Se dispara cada vez que algo
    // PUEDE haber quedado pendiente: una linea de interrupcion que sube, el
    // SysTick que vence, o una escritura al SCS -ISPR, STIR, ICSR, SHCSR- que
    // puede venir del depurador mientras el nucleo duerme. Sin el, un nucleo
    // dormido no tiene mas remedio que sondear
    // [vease doc/stm32f407vg_coste_simulacion.md].
    virtual const sc_core::sc_event& pending_ev() const = 0;

    // --- Configuración del SCB que la CPU consulta [IR, §10.2] --------------
    virtual uint32_t vtor() const        = 0;
    virtual bool div_0_trp() const       = 0;   // CCR.DIV_0_TRP
    virtual bool unalign_trp() const     = 0;   // CCR.UNALIGN_TRP
    virtual bool stkalign() const        = 0;   // CCR.STKALIGN
    virtual bool sleeponexit() const     = 0;   // SCR.SLEEPONEXIT
    // SCR.SLEEPDEEP. El núcleo lo lee AQUÍ, no por una señal: el SCB es suyo y
    // lo consulta de forma combinacional en el mismo instante en que ejecuta el
    // WFI/WFE. Por una señal llegaría tarde -el valor no se propaga hasta el
    // siguiente delta- y un firmware que escriba SCR y duerma en la misma
    // ráfaga de instrucciones entraría en Sleep en vez de en Stop [IR, §14.4.1].
    virtual bool scr_sleepdeep() const   = 0;   // SCR.SLEEPDEEP
    virtual bool sevonpend() const       = 0;   // SCR.SEVONPEND
    virtual unsigned cpacr_cp10() const  = 0;   // acceso a la FPU [IR, §8.12.1]
    virtual bool fpccr_aspen() const     = 0;
    virtual bool fpccr_lspen() const     = 0;
    virtual bool fpccr_lspact() const    = 0;
    virtual void set_lspact(bool a, uint32_t fpcar, bool thread, bool user) = 0;
    virtual uint32_t fpcar() const       = 0;
    virtual uint32_t fpdscr() const      = 0;

    // --- Notificación de faults [IR, §9.5, §9.6] ----------------------------
    // Registra la causa y devuelve la excepción que debe tomarse (con escalado
    // a HardFault si el manejador específico está deshabilitado).
    virtual int raise_fault(int excp, uint32_t cfsr_bits, bool addr_valid,
                            uint32_t addr, const RegFile& reg) = 0;
    virtual void set_hfsr(uint32_t bits) = 0;
    // Causa de la ultima parada de depuracion (SCB_DFSR) [IR, §13.4]
    virtual void set_dfsr(uint32_t bits) = 0;

    // --- MPU ----------------------------------------------------------------
    virtual bool mpu_check(uint32_t addr, bool write, bool instr, bool priv) const = 0;

    // La CPU publica la excepción activa para ICSR.VECTACTIVE [IR, §10.2.2]
    virtual void set_vectactive(unsigned excp) = 0;
};

// ===========================================================================
// SysTick [IR, §10.3]
// ===========================================================================
// El contador se modela por EVENTOS a partir de la frecuencia del dominio, no
// contando flancos: además de ser mucho más rápido, evita el aliasing de un
// contador de 24 bits muestreado a 168 MHz. La onda de reloj no es observable
// desde el SysTick, de modo que el resultado es idéntico al del contador real.
SC_MODULE(SysTick) {
    sc_core::sc_in<bool>   proc_clk{"proc_clk"};    // FCLK (se mantiene por netlist)
    sc_core::sc_in<bool>   ext_clk{"ext_clk"};      // HCLK/8 (idem)
    sc_core::sc_in<double> clk_hz{"clk_hz"};        // frecuencia de FCLK
    sc_core::sc_in<bool>   rst_n{"rst_n"};
    // El núcleo detenido por el depurador. [ARMv7-M, B3.3.1] El contador del
    // SysTick NO decrementa mientras el procesador está parado en Debug state:
    // es parte del núcleo, no un periférico, y se para con él. Sin esto, mirar
    // una variable durante un rato deja una interrupción esperando SIEMPRE al
    // reanudar —y en el simulador es peor que en el silicio, porque con el
    // núcleo parado no queda casi nada que simular y el tiempo simulado vuela—.
    sc_core::sc_in<bool>   parado{"parado"};
    sc_core::sc_out<bool>  tick_irq{"tick_irq"};    // excepción 15

    enum : uint32_t { CSR = 0x00, RVR = 0x04, CVR = 0x08, CALIB = 0x0C };

    SC_CTOR(SysTick) {
        SC_THREAD(tick_proc);
        SC_METHOD(reset_proc);  sensitive << rst_n;   dont_initialize();
        SC_METHOD(freq_proc);   sensitive << clk_hz;  dont_initialize();
        SC_METHOD(halt_proc);   sensitive << parado;  dont_initialize();
        SC_METHOD(irq_proc);    sensitive << irq_ev_; dont_initialize();
    }

    uint32_t reg_read(uint32_t off) {
        switch (off) {
            case CSR: { const uint32_t v = csr_; csr_ &= ~(1u << 16); return v; } // COUNTFLAG rc_r
            case RVR:   return rvr_ & 0x00FFFFFFu;
            case CVR:   return cvr_now() & 0x00FFFFFFu;
            // ⚠ NO DISPONIBLE EN LAS FUENTES: valor de SYST_CALIB del F407.
            // TENMS = 0 y NOREF/SKEW a 0 indican "no calibrado" [IR, §10.3].
            case CALIB: return 0x00000000u;
            default:    return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v) {
        switch (off) {
            case CSR:
                cvr_ = cvr_now();
                csr_ = (csr_ & (1u << 16)) | (v & 0x7u);
                rebase();
                break;
            case RVR:
                cvr_ = cvr_now();
                rvr_ = v & 0x00FFFFFFu;
                rebase();
                break;
            case CVR:                                  // cualquier escritura limpia
                cvr_ = 0;
                csr_ &= ~(1u << 16);
                rebase();
                break;
            default: break;
        }
    }
    bool enabled() const   { return csr_ & 1u; }
    bool tickint() const   { return (csr_ >> 1) & 1u; }
    bool clksource() const { return (csr_ >> 2) & 1u; }

private:
    uint32_t csr_ = 0, rvr_ = 0, cvr_ = 0;
    sc_core::sc_time t_base_{sc_core::SC_ZERO_TIME};   // instante de cvr_
    bool o_irq_ = false;
    sc_core::sc_event irq_ev_, resched_ev_;

    // Frecuencia efectiva: FCLK si CLKSOURCE = 1, FCLK/8 si es 0 [IR, §10.3]
    double tick_hz() const {
        const double f = clk_hz.read();
        return clksource() ? f : f / 8.0;
    }
    sc_core::sc_time tick_period() const {
        const double f = tick_hz();
        return f > 0.0 ? sc_core::sc_time(1.0e12 / f, sc_core::SC_PS)
                       : sc_core::SC_ZERO_TIME;
    }

    // Ticks transcurridos desde t_base_, MIRE O NO si el núcleo está parado.
    // La distinción hace falta de verdad: `halt_proc()` corre cuando la señal
    // `parado` YA vale true, y justo ahí hay que calcular el valor con los
    // ticks que sí pasaron antes de la parada. Usar la versión que respeta el
    // paro devolvería el valor viejo y el contador retrocedería.
    uint64_t elapsed_ticks_brutos() const {
        const double f = tick_hz();
        if (!enabled() || f <= 0.0) return 0;
        const double dt = (sc_core::sc_time_stamp() - t_base_).to_seconds();
        return uint64_t(dt * f + 0.5e-9);
    }
    uint64_t elapsed_ticks() const {
        return parado.read() ? 0 : elapsed_ticks_brutos();
    }
    // Valor del contador dados unos ticks [IR, §10.3]: decrementa hasta 0 y
    // recarga RVR
    uint32_t cvr_con(uint64_t k) const {
        if (k == 0) return cvr_;
        const uint64_t per = uint64_t(rvr_ & 0x00FFFFFFu) + 1u;
        if (cvr_ == 0) return uint32_t(rvr_ - ((k - 1) % per));
        if (k <= cvr_) return uint32_t(cvr_ - k);
        return uint32_t(rvr_ - ((k - cvr_ - 1) % per));
    }
    uint32_t cvr_now() const { return cvr_con(elapsed_ticks()); }
    void rebase() {
        t_base_ = sc_core::sc_time_stamp();
        resched_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void freq_proc() { cvr_ = cvr_now(); rebase(); }
    // Al PARAR se congela el valor que hubiera; al REANUDAR se vuelve a contar
    // desde ese mismo valor. El tiempo que el núcleo pasó detenido, sencillamente
    // no existe para este contador.
    void halt_proc() {
        if (parado.read()) cvr_ = cvr_con(elapsed_ticks_brutos());
        rebase();
    }
    void reset_proc() {
        if (!rst_n.read()) {
            csr_ = 0; rvr_ = 0; cvr_ = 0;
            o_irq_ = false; irq_ev_.notify(sc_core::SC_ZERO_TIME);
            rebase();
        }
    }
    void irq_proc() { tick_irq.write(o_irq_); }

    // Espera al siguiente instante en que el contador alcanza cero
    void tick_proc() {
        for (;;) {
            const sc_core::sc_time per = tick_period();
            if (!enabled() || per == sc_core::SC_ZERO_TIME) { wait(resched_ev_); continue; }
            // Con el núcleo parado no hay cuenta y no hay vencimiento: se espera
            // a que algo cambie -reanudar, reprogramar, cambiar de frecuencia-.
            if (parado.read()) { wait(resched_ev_); continue; }
            const uint32_t c = cvr_now();
            // Ticks hasta el próximo cero: si ya está en 0, recarga y RVR+1 más
            const uint64_t n = (c == 0) ? (uint64_t(rvr_ & 0x00FFFFFFu) + 1u) : c;
            wait(per * double(n), resched_ev_);
            if (!enabled()) continue;
            if (cvr_now() != 0) continue;         // reprogramado por software
            csr_ |= (1u << 16);                   // COUNTFLAG
            if (tickint()) {                      // pulso hacia el NVIC
                o_irq_ = true;  irq_ev_.notify(sc_core::SC_ZERO_TIME);
                wait(sc_core::sc_time(1, sc_core::SC_NS));
                o_irq_ = false; irq_ev_.notify(sc_core::SC_ZERO_TIME);
            }
        }
    }
};

// ===========================================================================
// MPU [IR, §10.4]
// ===========================================================================
SC_MODULE(Mpu) {
    enum : uint32_t { TYPE = 0x00, CTRL = 0x04, RNR = 0x08, RBAR = 0x0C, RASR = 0x10,
                      RBAR_A1 = 0x14, RASR_A1 = 0x18, RBAR_A2 = 0x1C, RASR_A2 = 0x20,
                      RBAR_A3 = 0x24, RASR_A3 = 0x28 };
    // Las regiones son un dato del chip, no de la arquitectura: ARMv7-M admite
    // 0, 8 o 16, y el F4 lleva 8. El indice se recorta con una MASCARA, que es
    // lo que hace el silicio, y por eso el numero tiene que ser potencia de dos.
    static constexpr unsigned N_REGIONS = 8;     // el valor por omision, el del F4

    explicit Mpu(sc_core::sc_module_name nm, unsigned regiones = N_REGIONS)
        : sc_core::sc_module(nm),
          n_reg_(regiones ? regiones : 1u), mask_(n_reg_ - 1u),
          rbar_(n_reg_, 0u), rasr_(n_reg_, 0u), hay_(regiones != 0) {}

    unsigned regiones() const { return hay_ ? n_reg_ : 0u; }
    bool     presente() const { return hay_; }

    void reset() {
        ctrl_ = 0; rnr_ = 0;
        std::fill(rbar_.begin(), rbar_.end(), 0u);
        std::fill(rasr_.begin(), rasr_.end(), 0u);
    }

    uint32_t reg_read(uint32_t off) {
        // Un nucleo SIN MPU no es uno con las regiones a cero: es uno cuyo
        // MPU_TYPE lee 0, que es como CMSIS y los depuradores averiguan que no
        // lo hay. Los demas registros leen cero y no guardan nada.
        if (!hay_) return 0;
        switch (off) {
            case TYPE: return uint32_t(n_reg_) << 8;   // DREGION, SEPARATE = 0
            case CTRL: return ctrl_;
            case RNR:  return rnr_;
            case RBAR: case RBAR_A1: case RBAR_A2: case RBAR_A3:
                return (rbar_[rnr_ & mask_] & ~0x1Fu) | (rnr_ & mask_);
            case RASR: case RASR_A1: case RASR_A2: case RASR_A3:
                return rasr_[rnr_ & mask_];
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v) {
        if (!hay_) return;
        switch (off) {
            case CTRL: ctrl_ = v & 0x7u; break;
            case RNR:  rnr_ = v & mask_; break;
            case RBAR: case RBAR_A1: case RBAR_A2: case RBAR_A3: {
                unsigned r = rnr_ & mask_;
                if (v & (1u << 4)) { r = v & 0xFu; rnr_ = r & mask_; }   // VALID
                rbar_[r & mask_] = v & ~0x1Fu;
                break;
            }
            case RASR: case RASR_A1: case RASR_A2: case RASR_A3:
                rasr_[rnr_ & mask_] = v;
                break;
            default: break;
        }
    }

    bool enabled() const     { return ctrl_ & 1u; }
    bool hfnmiena() const    { return (ctrl_ >> 1) & 1u; }
    bool privdefena() const  { return (ctrl_ >> 2) & 1u; }

    // Comprobación de un acceso. Devuelve true si está permitido.
    bool check(uint32_t addr, bool write, bool instr, bool priv) const {
        if (!hay_ || !enabled()) return true;
        // El espacio privado del procesador nunca lo cubre la MPU
        if (addr >= addr::PPB_BASE) return true;
        int hit = -1;
        for (int i = int(n_reg_) - 1; i >= 0; --i) {      // la región alta gana
            const uint32_t rasr = rasr_[i];
            if (!(rasr & 1u)) continue;                          // ENABLE
            const unsigned size = (rasr >> 1) & 0x1Fu;
            if (size < 4) continue;                              // mínimo 32 bytes
            const uint64_t len = uint64_t(1) << (size + 1);
            const uint32_t base = uint32_t(rbar_[i] & ~uint32_t(len - 1));
            if (addr < base || uint64_t(addr) >= uint64_t(base) + len) continue;
            // Subregiones (solo para regiones de 256 bytes o más)
            if (len >= 256) {
                const unsigned sub = unsigned((uint64_t(addr - base) * 8) / len);
                if ((rasr >> (8 + sub)) & 1u) continue;           // SRD: deshabilitada
            }
            hit = i;
            break;
        }
        if (hit < 0) return priv && privdefena();      // mapa por defecto
        const uint32_t rasr = rasr_[hit];
        if (instr && ((rasr >> 28) & 1u)) return false;           // XN
        switch ((rasr >> 24) & 0x7u) {                            // AP [IR, §10.4.4]
            case 0: return false;                                 // sin acceso
            case 1: return priv;                                  // priv RW
            case 2: return priv || !write;                        // priv RW / user RO
            case 3: return true;                                  // RW total
            case 5: return priv && !write;                        // priv RO
            case 6: case 7: return !write;                        // RO
            default: return false;
        }
    }

private:
    unsigned n_reg_, mask_;
    uint32_t ctrl_ = 0, rnr_ = 0;
    std::vector<uint32_t> rbar_, rasr_;
    bool hay_;
};

// ===========================================================================
// SCS: contenedor con SCB + NVIC + SysTick + MPU
// ===========================================================================
class Scs : public sc_core::sc_module, public core_sys_if {
public:
    // Los rasgos del núcleo, LO PRIMERO que se construye: de aquí salen el
    // tamaño del vector de entradas, el de los arrays de estado, las regiones
    // del MPU y la máscara de prioridad. Por omisión son los del F407, de modo
    // que un `Scs` construido como siempre es el de siempre: 82 líneas, 4 bits
    // de prioridad y 8 regiones. [core/core_caps.h]
    const CoreCaps caps;

    tlm_utils::simple_target_socket<Scs> ppb{"ppb"};   // desde el router del núcleo
    sc_core::sc_export<core_sys_if> cpu_if{"cpu_if"};

    sc_core::sc_vector<sc_core::sc_in<bool>> irq_in;   // [caps.n_irq] [IR, §9.1.2]
    sc_core::sc_in<bool>  nmi_in{"nmi_in"};
    sc_core::sc_in<bool>  rst_n{"rst_n"};
    sc_core::sc_out<bool> sysresetreq{"sysresetreq"};  // AIRCR -> RCC [IR, §4.1]
    sc_core::sc_out<bool> sleepdeep{"sleepdeep"};      // SCR -> CPU/PWR

    SysTick systick{"systick"};
    Mpu     mpu;

    // Offsets del SCB [IR, §10.2, §10.5]
    enum : uint32_t {
        CPUID = 0x00, ICSR = 0x04, VTOR_ = 0x08, AIRCR = 0x0C, SCR = 0x10,
        CCR = 0x14, SHPR1 = 0x18, SHPR2 = 0x1C, SHPR3 = 0x20, SHCSR = 0x24,
        CFSR = 0x28, HFSR = 0x2C, DFSR = 0x30, MMFAR = 0x34, BFAR = 0x38,
        AFSR = 0x3C, CPACR = 0x88
    };

    explicit Scs(sc_core::sc_module_name nm, CoreCaps c = CORE_STM32F407VG)
        : sc_core::sc_module(nm), caps(c), irq_in("irq_in", c.n_irq),
          mpu("mpu", c.mpu_regiones),
          enabled_(c.n_irq, false), ipr_(c.n_irq, 0),
          pending_(c.n_excepciones(), false), active_(c.n_excepciones(), false),
          prev_irq_(c.n_irq, false) {
        cpu_if(*this);
        ppb.register_b_transport(this, &Scs::bt);
        SC_HAS_PROCESS(Scs);
        SC_METHOD(sample_proc);
        for (unsigned i = 0; i < caps.n_irq; ++i) sensitive << irq_in[i];
        sensitive << nmi_in << s_tick_;
        dont_initialize();
        SC_METHOD(reset_proc);  sensitive << rst_n;   dont_initialize();
        SC_METHOD(publish_proc); sensitive << pub_ev_; dont_initialize();
        systick.tick_irq(s_tick_);
        reset_state();
    }

    // ---- Señal interna del SysTick (la conecta el contenedor del núcleo) ---
    sc_core::sc_signal<bool> s_tick_{"s_tick"};

    // =======================================================================
    // core_sys_if — arbitraje
    // =======================================================================
    int exception_priority(int e) const override {
        switch (e) {
            case EXC_RESET:     return PRIO_RESET;
            case EXC_NMI:       return PRIO_NMI;
            case EXC_HARDFAULT: return PRIO_HARDFAULT;
            case EXC_MEMMANAGE:  return int(shpr_[0] & 0xFFu);
            case EXC_BUSFAULT:   return int((shpr_[0] >> 8) & 0xFFu);
            case EXC_USAGEFAULT: return int((shpr_[0] >> 16) & 0xFFu);
            case EXC_SVCALL:     return int((shpr_[1] >> 24) & 0xFFu);
            case EXC_DEBUGMON:   return int(shpr_[2] & 0xFFu);
            case EXC_PENDSV:     return int((shpr_[2] >> 16) & 0xFFu);
            case EXC_SYSTICK:    return int((shpr_[2] >> 24) & 0xFFu);
            default:
                if (e >= EXC_IRQ0 && e < int(caps.n_excepciones())) return int(ipr_[e - EXC_IRQ0]);
                return PRIO_NONE;
        }
    }

    // Prioridad efectiva de grupo según PRIGROUP [IR, §9.7]: las excepciones
    // solo se anidan si su prioridad de grupo es estrictamente mayor.
    int group_priority(int e) const {
        const int p = exception_priority(e);
        if (p < 0) return p;                       // Reset/NMI/HardFault
        const unsigned pg = prigroup();
        const unsigned nsub = pg + 1;              // bits de subprioridad
        return (nsub >= 8) ? 0 : ((p >> nsub) << nsub);
    }

    int execution_priority(const RegFile& reg) const override {
        int p = PRIO_NONE;
        for (unsigned e = 1; e < caps.n_excepciones(); ++e)
            if (active_[e]) { const int q = group_priority(int(e)); if (q < p) p = q; }
        if (reg.faultmask) p = std::min(p, PRIO_HARDFAULT);     // eleva a -1
        if (reg.primask)   p = std::min(p, 0);                  // bloquea 0..255
        if (reg.basepri) {
            const unsigned pg = prigroup();
            const unsigned nsub = pg + 1;
            const int bp = (nsub >= 8) ? 0 : ((int(reg.basepri) >> nsub) << nsub);
            p = std::min(p, bp);
        }
        return p;
    }

    int pending_exception(int current_prio, const RegFile& reg) const override {
        (void)reg;
        int best = -1, best_prio = current_prio;
        for (unsigned e = 1; e < caps.n_excepciones(); ++e) {
            if (!pending_[e]) continue;
            if (e >= EXC_IRQ0 && !enabled_[e - EXC_IRQ0]) continue;
            if (!sys_exc_enabled(int(e))) continue;
            const int gp = group_priority(int(e));
            if (gp < best_prio || (gp == best_prio && best < 0 && false)) {
                best = int(e); best_prio = gp;
            }
        }
        return best;
    }

    void ack_exception(int e) override {
        if (e <= 0 || e >= int(caps.n_excepciones())) return;
        pending_[e] = false;
        active_[e]  = true;
        sync_shcsr_from_state();
        publish();
    }
    void return_exception(int e) override {
        if (e <= 0 || e >= int(caps.n_excepciones())) return;
        active_[e] = false;
        sync_shcsr_from_state();
        publish();
    }
    void set_pending(int e, bool p) override {
        if (e <= 0 || e >= int(caps.n_excepciones())) return;
        pending_[e] = p;
        sync_shcsr_from_state();
        publish();
        if (p) pend_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool is_pending(int e) const override {
        return (e > 0 && e < int(caps.n_excepciones())) ? pending_[e] : false;
    }
    bool is_active(int e) const override {
        return (e > 0 && e < int(caps.n_excepciones())) ? active_[e] : false;
    }
    bool any_pending() const override {
        for (unsigned e = 1; e < caps.n_excepciones(); ++e)
            if (pending_[e]) return true;
        return false;
    }
    const sc_core::sc_event& pending_ev() const override { return pend_ev_; }

    // =======================================================================
    // core_sys_if — configuración
    // =======================================================================
    uint32_t vtor() const override        { return vtor_ & 0x3FFFFF80u; }
    bool div_0_trp() const override       { return (ccr_ >> 4) & 1u; }
    bool unalign_trp() const override     { return (ccr_ >> 3) & 1u; }
    bool stkalign() const override        { return (ccr_ >> 9) & 1u; }
    bool sleeponexit() const override     { return (scr_ >> 1) & 1u; }
    bool scr_sleepdeep() const override   { return (scr_ >> 2) & 1u; }
    bool sevonpend() const override       { return (scr_ >> 4) & 1u; }
    unsigned cpacr_cp10() const override  { return (cpacr_ >> 20) & 0x3u; }
    bool fpccr_aspen() const override     { return (fpccr_ >> 31) & 1u; }
    bool fpccr_lspen() const override     { return (fpccr_ >> 30) & 1u; }
    bool fpccr_lspact() const override    { return fpccr_ & 1u; }
    uint32_t fpcar() const override       { return fpcar_; }
    uint32_t fpdscr() const override      { return fpdscr_; }
    void set_lspact(bool a, uint32_t car, bool thread, bool user) override {
        fpccr_ = (fpccr_ & ~0x0000000Bu) | (a ? 1u : 0u)
               | (thread ? (1u << 3) : 0u) | (user ? (1u << 1) : 0u);
        if (a) fpcar_ = car;
    }

    bool mpu_check(uint32_t a, bool w, bool i, bool p) const override {
        return mpu.check(a, w, i, p);
    }

    // =======================================================================
    // core_sys_if — faults [IR, §9.5]
    // =======================================================================
    int raise_fault(int excp, uint32_t bits, bool addr_valid, uint32_t a,
                    const RegFile& reg) override {
        cfsr_ |= bits;
        if (addr_valid) {
            if (excp == EXC_MEMMANAGE) { mmfar_ = a; cfsr_ |= MM_MMARVALID; }
            if (excp == EXC_BUSFAULT)  { bfar_  = a; cfsr_ |= BF_BFARVALID; }
        }
        // Escalado: si el manejador está deshabilitado o su prioridad no puede
        // apropiarse de la ejecución actual, se fuerza HardFault [IR, §9.5].
        const bool ena = sys_exc_enabled(excp);
        const int  gp  = group_priority(excp);
        const int  cur = execution_priority(reg);
        if (!ena || gp >= cur) {
            hfsr_ |= (1u << 30);              // FORCED
            return EXC_HARDFAULT;
        }
        return excp;
    }
    void set_hfsr(uint32_t bits) override { hfsr_ |= bits; }
    void set_dfsr(uint32_t bits) override { dfsr_ |= bits; }

    // Estado observable por el banco de pruebas
    uint32_t reg_cfsr() const { return cfsr_; }
    uint32_t reg_hfsr() const { return hfsr_; }
    uint32_t reg_icsr() const { return const_cast<Scs*>(this)->icsr_value(); }
    unsigned prigroup() const { return (aircr_ >> 8) & 0x7u; }
    // La mascara del byte `n` de un SHPR, con los bits de prioridad que este
    // nucleo implementa de verdad.
    uint32_t m_pri(unsigned n) const { return uint32_t(caps.prio_mask()) << (8 * n); }
    bool     irq_enabled(unsigned n) const { return n < caps.n_irq && enabled_[n]; }
    void     force_nmi() { set_pending(EXC_NMI, true); }

private:
    // ---- Estado ------------------------------------------------------------
    // Vectores y no arrays de tamano fijo: el numero de lineas es un rasgo del
    // chip, no de la arquitectura. Se dimensionan en el CONSTRUCTOR y no se
    // vuelven a tocar, asi que no hay ni una reserva de memoria con la
    // simulacion en marcha. `std::vector<bool>` esta a proposito: empaqueta a
    // bits, y 98 bits de pendientes caben en dos palabras.
    std::vector<bool>    enabled_;
    std::vector<uint8_t> ipr_;
    uint32_t shpr_[3] = {0, 0, 0};
    uint32_t vtor_ = 0, aircr_ = 0xFA050000u, scr_ = 0, ccr_ = 0x00000200u;
    uint32_t shcsr_ = 0, cfsr_ = 0, hfsr_ = 0, dfsr_ = 0;
    uint32_t mmfar_ = 0, bfar_ = 0, afsr_ = 0, cpacr_ = 0;
    uint32_t fpccr_ = 0xC0000000u, fpcar_ = 0, fpdscr_ = 0;
    std::vector<bool> pending_, active_;
    std::vector<bool> prev_irq_;
    bool prev_nmi_ = false, prev_tick_ = false;
    bool o_sysreset_ = false, o_sleepdeep_ = false;
    sc_core::sc_event pub_ev_;
    sc_core::sc_event pend_ev_;      // "puede haber algo pendiente": despierta al nucleo

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void publish_proc() {
        sysresetreq.write(o_sysreset_);
        sleepdeep.write(o_sleepdeep_);
    }

    void reset_state() {
        for (unsigned i = 0; i < caps.n_excepciones(); ++i) { pending_[i] = active_[i] = false; }
        for (unsigned i = 0; i < caps.n_irq; ++i) { enabled_[i] = false; ipr_[i] = 0; }
        shpr_[0] = shpr_[1] = shpr_[2] = 0;
        vtor_ = 0; aircr_ = 0xFA050000u; scr_ = 0; ccr_ = 0x00000200u;
        shcsr_ = 0; cfsr_ = 0; hfsr_ = 0; dfsr_ = 0;
        mmfar_ = 0; bfar_ = 0; afsr_ = 0; cpacr_ = 0;
        fpccr_ = 0xC0000000u; fpcar_ = 0; fpdscr_ = 0;
        mpu.reset();
        o_sysreset_ = false; o_sleepdeep_ = false;
    }
    void reset_proc() { if (!rst_n.read()) { reset_state(); publish(); } }

    // ¿Está habilitada la excepción de sistema? [IR, §10.5.3]
    bool sys_exc_enabled(int e) const {
        switch (e) {
            case EXC_MEMMANAGE:  return (shcsr_ >> 16) & 1u;
            case EXC_BUSFAULT:   return (shcsr_ >> 17) & 1u;
            case EXC_USAGEFAULT: return (shcsr_ >> 18) & 1u;
            default: return true;      // el resto no tiene bit de habilitación
        }
    }

    // Refleja el estado de excepciones de sistema en SHCSR [IR, §10.5.3]
    void sync_shcsr_from_state() {
        auto set = [&](unsigned bit, bool v) {
            shcsr_ = v ? (shcsr_ | (1u << bit)) : (shcsr_ & ~(1u << bit));
        };
        set(0,  active_[EXC_MEMMANAGE]);
        set(1,  active_[EXC_BUSFAULT]);
        set(3,  active_[EXC_USAGEFAULT]);
        set(7,  active_[EXC_SVCALL]);
        set(8,  active_[EXC_DEBUGMON]);
        set(10, active_[EXC_PENDSV]);
        set(11, active_[EXC_SYSTICK]);
        set(12, pending_[EXC_USAGEFAULT]);
        set(13, pending_[EXC_MEMMANAGE]);
        set(14, pending_[EXC_BUSFAULT]);
        set(15, pending_[EXC_SVCALL]);
    }

    // Muestreo de las líneas de interrupción: el NVIC latchea por flanco de
    // subida (las fuentes mantienen el nivel hasta que el manejador lo limpia).
    void sample_proc() {
        for (unsigned i = 0; i < caps.n_irq; ++i) {
            const bool v = irq_in[i].read();
            if (v && !prev_irq_[i]) pending_[EXC_IRQ0 + i] = true;
            prev_irq_[i] = v;
        }
        const bool nmi = nmi_in.read();
        if (nmi && !prev_nmi_) pending_[EXC_NMI] = true;
        prev_nmi_ = nmi;
        const bool tk = s_tick_.read();
        if (tk && !prev_tick_) pending_[EXC_SYSTICK] = true;
        prev_tick_ = tk;
        pend_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    uint32_t icsr_value() {
        uint32_t v = 0;
        // VECTACTIVE lo escribe la CPU vía set_vectactive()
        v |= vectactive_ & 0x1FFu;
        int pend = -1, pend_prio = PRIO_NONE;
        bool isr_pending = false;
        for (unsigned e = 1; e < caps.n_excepciones(); ++e) {
            if (!pending_[e]) continue;
            if (e >= EXC_IRQ0) { if (!enabled_[e - EXC_IRQ0]) continue; isr_pending = true; }
            const int gp = group_priority(int(e));
            if (gp < pend_prio) { pend_prio = gp; pend = int(e); }
        }
        if (pend > 0) v |= uint32_t(pend & 0x3FFu) << 12;
        if (isr_pending) v |= (1u << 22);
        if (pending_[EXC_PENDSV]) v |= (1u << 28);
        if (pending_[EXC_SYSTICK]) v |= (1u << 26);
        if (pending_[EXC_NMI]) v |= (1u << 31);
        unsigned n_active = 0;
        for (unsigned e = 1; e < caps.n_excepciones(); ++e) if (active_[e]) ++n_active;
        if (n_active <= 1) v |= (1u << 11);       // RETTOBASE
        return v;
    }

public:
    void set_vectactive(unsigned e) override { vectactive_ = e; }
private:
    unsigned vectactive_ = 0;

    // =======================================================================
    // Decodificación del bloque 0xE000E000-0xE000EFFF [IR, §10.1]
    // =======================================================================
    uint32_t read_word(uint32_t off) {
        if (off >= 0x010 && off < 0x100) return systick.reg_read(off - 0x010);
        if (off >= 0x100 && off < 0x180) {              // NVIC_ISER
            return iser_word((off - 0x100) / 4);
        }
        if (off >= 0x180 && off < 0x200) return iser_word((off - 0x180) / 4);
        if (off >= 0x200 && off < 0x280) return ispr_word((off - 0x200) / 4);
        if (off >= 0x280 && off < 0x300) return ispr_word((off - 0x280) / 4);
        if (off >= 0x300 && off < 0x380) return iabr_word((off - 0x300) / 4);
        if (off >= 0x400 && off < 0x4F0) {              // NVIC_IPR
            const unsigned base = (off - 0x400);
            uint32_t v = 0;
            for (unsigned i = 0; i < 4; ++i)
                if (base + i < caps.n_irq) v |= uint32_t(ipr_[base + i]) << (8 * i);
            return v;
        }
        if (off >= 0xD00 && off < 0xD90) return scb_read(off - 0xD00);
        if (off >= 0xD90 && off < 0xDF0) return mpu.reg_read(off - 0xD90);
        if (off >= 0xF30 && off < 0xF40) {              // FPU
            switch (off) {
                case 0xF34: return fpccr_;
                case 0xF38: return fpcar_;
                case 0xF3C: return fpdscr_;
                default: return 0;
            }
        }
        return 0;
    }

    // Envoltorio de la escritura: cualquier registro del SCS puede dejar una
    // excepcion pendiente -ISPR, STIR, ICSR con PENDSVSET o NMIPENDSET, SHCSR-
    // y esas escrituras pueden venir del DEPURADOR con el nucleo dormido. En
    // vez de enumerar los casos uno a uno -y olvidarse de alguno-, se avisa
    // siempre: una escritura al SCS es un suceso raro comparado con sondear.
    void write_word(uint32_t off, uint32_t v) {
        write_word_impl(off, v);
        pend_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void write_word_impl(uint32_t off, uint32_t v) {
        if (off >= 0x010 && off < 0x100) { systick.reg_write(off - 0x010, v); return; }
        if (off >= 0x100 && off < 0x180) {              // ISER: 1 habilita
            const unsigned b = ((off - 0x100) / 4) * 32;
            for (unsigned i = 0; i < 32 && b + i < caps.n_irq; ++i)
                if ((v >> i) & 1u) enabled_[b + i] = true;
            publish(); return;
        }
        if (off >= 0x180 && off < 0x200) {              // ICER: 1 deshabilita
            const unsigned b = ((off - 0x180) / 4) * 32;
            for (unsigned i = 0; i < 32 && b + i < caps.n_irq; ++i)
                if ((v >> i) & 1u) enabled_[b + i] = false;
            publish(); return;
        }
        if (off >= 0x200 && off < 0x280) {              // ISPR
            const unsigned b = ((off - 0x200) / 4) * 32;
            for (unsigned i = 0; i < 32 && b + i < caps.n_irq; ++i)
                if ((v >> i) & 1u) pending_[EXC_IRQ0 + b + i] = true;
            publish(); return;
        }
        if (off >= 0x280 && off < 0x300) {              // ICPR
            const unsigned b = ((off - 0x280) / 4) * 32;
            for (unsigned i = 0; i < 32 && b + i < caps.n_irq; ++i)
                if ((v >> i) & 1u) pending_[EXC_IRQ0 + b + i] = false;
            publish(); return;
        }
        if (off >= 0x400 && off < 0x4F0) {              // IPR
            const unsigned base = (off - 0x400);
            for (unsigned i = 0; i < 4; ++i)
                if (base + i < caps.n_irq)
                    ipr_[base + i] = uint8_t((v >> (8 * i)) & caps.prio_mask());
            return;
        }
        if (off == 0xF00) {                             // STIR [IR, §9.2.2]
            const unsigned n = v & 0x1FFu;
            if (n < caps.n_irq) { pending_[EXC_IRQ0 + n] = true; publish(); }
            return;
        }
        if (off >= 0xD00 && off < 0xD90) { scb_write(off - 0xD00, v); return; }
        if (off >= 0xD90 && off < 0xDF0) { mpu.reg_write(off - 0xD90, v); return; }
        if (off >= 0xF30 && off < 0xF40) {
            switch (off) {
                case 0xF34: fpccr_ = (fpccr_ & 0x0000000Bu) | (v & 0xC0000100u); break;
                case 0xF38: fpcar_ = v & ~7u; break;
                case 0xF3C: fpdscr_ = v & 0x07C00000u; break;
                default: break;
            }
            return;
        }
    }

    uint32_t iser_word(unsigned w) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 32 && w * 32 + i < caps.n_irq; ++i)
            if (enabled_[w * 32 + i]) v |= (1u << i);
        return v;
    }
    uint32_t ispr_word(unsigned w) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 32 && w * 32 + i < caps.n_irq; ++i)
            if (pending_[EXC_IRQ0 + w * 32 + i]) v |= (1u << i);
        return v;
    }
    uint32_t iabr_word(unsigned w) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 32 && w * 32 + i < caps.n_irq; ++i)
            if (active_[EXC_IRQ0 + w * 32 + i]) v |= (1u << i);
        return v;
    }

    uint32_t scb_read(uint32_t off) {
        switch (off) {
            // Lo que el firmware lee para saber QUE NUCLEO tiene debajo. Es
            // un rasgo, no una constante: un M3 y un M4 no dan lo mismo aqui,
            // y CMSIS y los depuradores lo miran. [IR, §10.2.1]
            case CPUID: return caps.cpuid;
            case ICSR:  return icsr_value();
            case VTOR_: return vtor_;
            case AIRCR: return (aircr_ & 0x00000700u) | 0xFA050000u;
            case SCR:   return scr_;
            case CCR:   return ccr_;
            case SHPR1: return shpr_[0];
            case SHPR2: return shpr_[1];
            case SHPR3: return shpr_[2];
            case SHCSR: return shcsr_;
            case CFSR:  return cfsr_;
            case HFSR:  return hfsr_;
            case DFSR:  return dfsr_;
            case MMFAR: return mmfar_;
            case BFAR:  return bfar_;
            case AFSR:  return afsr_;
            case CPACR: return cpacr_;
            default:    return 0;
        }
    }

    void scb_write(uint32_t off, uint32_t v) {
        switch (off) {
            case ICSR:
                if (v & (1u << 31)) pending_[EXC_NMI] = true;       // NMIPENDSET
                if (v & (1u << 28)) pending_[EXC_PENDSV] = true;    // PENDSVSET
                if (v & (1u << 27)) pending_[EXC_PENDSV] = false;   // PENDSVCLR
                if (v & (1u << 26)) pending_[EXC_SYSTICK] = true;   // PENDSTSET
                if (v & (1u << 25)) pending_[EXC_SYSTICK] = false;  // PENDSTCLR
                publish();
                break;
            case VTOR_: vtor_ = v & 0x3FFFFF80u; break;
            case AIRCR:
                if ((v >> 16) != 0x05FAu) break;                    // VECTKEY
                aircr_ = (aircr_ & ~0x00000700u) | (v & 0x00000700u);
                if (v & (1u << 2)) {                                // SYSRESETREQ
                    o_sysreset_ = true; publish();
                }
                break;
            case SCR:
                scr_ = v & 0x16u;
                o_sleepdeep_ = (scr_ >> 2) & 1u;
                publish();
                break;
            case CCR:   ccr_ = (ccr_ & 0x00000200u) | (v & 0x0000021Bu); break;
            // Los tres SHPR guardan CUATRO prioridades de byte cada uno, y
            // solo los bits IMPLEMENTADOS se quedan: con cuatro bits la
            // mascara de cada byte es 0xF0, con tres 0xE0. Los bytes que no
            // corresponden a ningun manejador se descartan enteros
            // [ARMv7-M, B3.2.10]:
            //   SHPR1 -> MemManage[7:0], BusFault[15:8], UsageFault[23:16]
            //   SHPR2 -> SVCall[31:24]
            //   SHPR3 -> DebugMon[7:0], PendSV[23:16], SysTick[31:24]
            case SHPR1: shpr_[0] = v & (m_pri(0) | m_pri(1) | m_pri(2)); break;
            case SHPR2: shpr_[1] = v &  m_pri(3);                        break;
            case SHPR3: shpr_[2] = v & (m_pri(0) | m_pri(2) | m_pri(3)); break;
            case SHCSR: {
                shcsr_ = (shcsr_ & ~0x0007F000u) | (v & 0x0007F000u);
                pending_[EXC_USAGEFAULT] = (v >> 12) & 1u;
                pending_[EXC_MEMMANAGE]  = (v >> 13) & 1u;
                pending_[EXC_BUSFAULT]   = (v >> 14) & 1u;
                pending_[EXC_SVCALL]     = (v >> 15) & 1u;
                publish();
                break;
            }
            case CFSR:  cfsr_ &= ~v; break;                         // rc_w1
            case HFSR:  hfsr_ &= ~v; break;                         // rc_w1
            case DFSR:  dfsr_ &= ~v; break;                         // rc_w1
            case MMFAR: mmfar_ = v; break;
            case BFAR:  bfar_ = v; break;
            case AFSR:  afsr_ = v; break;
            // CPACR gobierna el acceso a CP10/CP11, que SON la FPU. En un
            // nucleo sin ella los dos campos leen cero hagas lo que hagas, y
            // es asi como el codigo de arranque de CMSIS averigua que no debe
            // habilitarla. [IR, §8.12.1]
            case CPACR: cpacr_ = caps.hay_fpu() ? (v & 0x00F00000u) : 0u; break;
            default: break;
        }
    }

    // Transporte TLM: el SCS solo admite accesos de palabra alineados
    // [IR, §10.1].
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint32_t a = uint32_t(gp.get_address());
        const uint32_t off = a - 0xE000E000u;
        const unsigned len = gp.get_data_length();
        uint8_t* d = gp.get_data_ptr();
        if (gp.is_read()) {
            const uint32_t w = read_word(off & ~3u);
            for (unsigned i = 0; i < len; ++i) d[i] = uint8_t(w >> (8 * ((a + i) & 3u)));
        } else {
            uint32_t w = 0;
            if (len < 4) {
                w = read_word(off & ~3u);
                for (unsigned i = 0; i < len; ++i) {
                    const unsigned b = (a + i) & 3u;
                    w = (w & ~(0xFFu << (8 * b))) | (uint32_t(d[i]) << (8 * b));
                }
            } else {
                for (unsigned i = 0; i < 4; ++i) w |= uint32_t(d[i]) << (8 * i);
            }
            write_word(off & ~3u, w);
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace stm32
#endif // STM32_CORE_SCS_H
