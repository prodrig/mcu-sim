// =============================================================================
// timers.h — Temporizadores TIM1..TIM14 [IR, §12.1, §12.2, §12.3, §12.8]
//
// El STM32F407VG lleva catorce temporizadores construidos sobre el MISMO bloque
// de diseño, del que cada instancia implementa un subconjunto de funciones:
//
//   TIM1, TIM8      avanzados: 16 bits, 4 canales, 3 salidas complementarias
//                   con tiempo muerto, freno (BDTR), contador de repeticiones
//                   y cuatro vectores de interrupción propios;
//   TIM2, TIM5      propósito general de 32 bits, 4 canales;
//   TIM3, TIM4      propósito general de 16 bits, 4 canales;
//   TIM9, TIM12     16 bits, 2 canales, sin ETR, sin DMA;
//   TIM10, TIM11,
//   TIM13, TIM14    16 bits, 1 canal, sin esclavo, sin DMA;
//   TIM6, TIM7      básicos: base de tiempos y disparo del DAC, sin canales.
//
// El modelo es UNO SOLO y el tipo de temporizador se selecciona con parámetros,
// de dos maneras equivalentes:
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using TimAdvanced = TimT<CAPS_TIM_ADV>;     // TIM1/TIM8
//         using TimGp32     = TimT<CAPS_TIM_GP32>;    // TIM2/TIM5
//         TimT<CAPS_TIM_BASIC> tim6{"tim6", addr::TIM6_B};
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor, útil para
//     barrer variantes desde un banco de pruebas o para modelar un derivado
//     de la familia con otra combinación:
//         TimerBase t{"t", base, TimCaps{...}};
//         TimerBase u{"u", base, /*bits=*/32, /*canales=*/2};   // atajo
//
// Los rasgos no son cosméticos: gobiernan las MÁSCARAS DE ESCRITURA de todos
// los registros y la anchura de CNT/ARR/CCRx, de modo que en un TIM10 los
// campos DIR, CMS, OPM, SMS, TS, CCxNE o BDTR quedan reservados y leen cero,
// exactamente igual que en el silicio, y CNT desborda a los 16 bits mientras
// que en TIM2/TIM5 lo hace a los 32.
//
// Fase F4 — implementado:
//   * banco de registros completo CR1/CR2/SMCR/DIER/SR/EGR/CCMR1/CCMR2/CCER/
//     CNT/PSC/ARR/RCR/CCR1..4/BDTR/DCR/DMAR [IR, §12.1.4-D];
//   * base de tiempos con prescaler, auto-recarga con precarga (ARPE), modos
//     ascendente, descendente y alineado al centro, y contador de repeticiones;
//   * comparación de salida: modos congelado/activo/inactivo/conmutar/forzado y
//     PWM 1 y 2, con precarga de CCRx (OCxPE) y polaridad CCxP;
//   * salidas complementarias con MOE/OSSI/OSSR/OISx y tiempo muerto DTG;
//   * entrada de freno BKIN con polaridad BKP y rearme automático AOE;
//   * captura de entrada con prescaler ICxPSC, polaridad, y banderas CCxOF;
//   * controlador de esclavo: modos reset, gated, trigger, reloj externo 1 y 2
//     (ETR) y codificador incremental en sus tres modos;
//   * salida TRGO (MMS) hacia ADC, DAC y la cadena ITRx entre temporizadores;
//   * interrupciones (una global o los cuatro vectores de los avanzados) y
//     peticiones de DMA, incluido el modo ráfaga DCR/DMAR;
//   * congelación por el depurador (DBGMCU_APBx_FZ).
//
// NOTA DE MODELADO — el contador no se evalúa flanco a flanco de TIMCLK, sino
// que SALTA AL SIGUIENTE SUCESO (comparación o desbordamiento) calculando el
// tiempo que falta a partir de la frecuencia del dominio. El valor de CNT se
// interpola en cualquier instante intermedio, de modo que una lectura del
// registro devuelve lo mismo que devolvería el silicio. Esto hace que el coste
// de simulación dependa del número de sucesos y no de la frecuencia del reloj,
// y que el temporizador siga funcionando cuando el banco de pruebas suprime las
// formas de onda de los relojes internos para acelerar la simulación.
// =============================================================================
#ifndef STM32_PERIPH_TIMERS_H
#define STM32_PERIPH_TIMERS_H

#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos del temporizador. Es lo único que distingue un TIM1 de un TIM14.
// ---------------------------------------------------------------------------
struct TimCaps {
    unsigned width_bits    = 16;  // anchura de CNT/ARR/CCRx: 16 o 32
    unsigned channels      = 4;   // canales de captura/comparación: 0..4
    unsigned comp_channels = 0;   // de ellos, cuántos tienen salida OCxN
    bool bdtr           = false;  // registro BDTR: MOE/AOE/BKE/BKP/OSSR/OSSI/DTG
    bool repetition     = false;  // registro RCR (contador de repeticiones)
    bool slave_mode     = true;   // SMCR.SMS/TS/MSM y entradas ITRx
    bool ext_trigger    = true;   // SMCR.ETF/ETPS/ECE/ETP y entrada ETR
    bool encoder        = true;   // modos de codificador incremental
    bool hall           = true;   // CR2.TI1S (XOR de las tres entradas)
    bool dma            = true;   // DIER.UDE/CCxDE/COMDE/TDE y peticiones
    bool dma_burst      = true;   // registros DCR/DMAR (modo ráfaga)
    bool trgo           = true;   // CR2.MMS y salida TRGO
    bool center_aligned = true;   // CR1.CMS (modos alineados al centro)
    bool down_count     = true;   // CR1.DIR (conteo descendente)
    bool one_pulse      = true;   // CR1.OPM
    bool clock_division = true;   // CR1.CKD
    bool split_irq      = false;  // 4 vectores (BRK/UP/TRG_COM/CC) o uno solo
    const char* kind    = "TIM";  // etiqueta para trazas y avisos
};

// --- Las seis variantes del STM32F407VG ------------------------------------
// Se construyen con funciones constexpr porque C++17 no admite inicializadores
// designados: así cada variante dice EXPLÍCITAMENTE en qué se aparta del bloque
// completo, que es la forma legible de expresar una familia de periféricos.
constexpr TimCaps caps_advanced() {           // TIM1, TIM8 [IR, §12.1]
    TimCaps c{};
    c.width_bits = 16; c.channels = 4; c.comp_channels = 3;
    c.bdtr = true; c.repetition = true; c.split_irq = true;
    c.kind = "avanzado";
    return c;
}
constexpr TimCaps caps_gp32() {               // TIM2, TIM5 [IR, §12.2]
    TimCaps c{};
    c.width_bits = 32; c.channels = 4;
    c.kind = "GP 32 bits";
    return c;
}
constexpr TimCaps caps_gp16() {               // TIM3, TIM4 [IR, §12.2]
    TimCaps c{};
    c.width_bits = 16; c.channels = 4;
    c.kind = "GP 16 bits";
    return c;
}
constexpr TimCaps caps_gp2ch() {              // TIM9, TIM12 [IR, §12.8]
    TimCaps c{};
    c.width_bits = 16; c.channels = 2;
    c.ext_trigger = false;                    // no tienen entrada ETR
    c.dma = false; c.dma_burst = false;       // sin DMA en el F407 [IR, §11.4]
    c.center_aligned = false; c.down_count = false;   // solo ascendente
    c.hall = false;
    c.kind = "GP 2 canales";
    return c;
}
constexpr TimCaps caps_gp1ch() {              // TIM10, TIM11, TIM13, TIM14
    TimCaps c{};
    c.width_bits = 16; c.channels = 1;
    c.slave_mode = false; c.ext_trigger = false; c.encoder = false;
    c.hall = false; c.trgo = false;
    c.dma = false; c.dma_burst = false;
    c.center_aligned = false; c.down_count = false; c.one_pulse = false;
    c.kind = "GP 1 canal";
    return c;
}
constexpr TimCaps caps_basic() {              // TIM6, TIM7 [IR, §12.3]
    TimCaps c{};
    c.width_bits = 16; c.channels = 0;
    c.slave_mode = false; c.ext_trigger = false; c.encoder = false;
    c.hall = false; c.dma_burst = false;
    c.center_aligned = false; c.down_count = false;
    c.clock_division = false;                 // CR1: solo ARPE/OPM/URS/UDIS/CEN
    c.kind = "basico";
    return c;
}

inline constexpr TimCaps CAPS_TIM_ADV   = caps_advanced();
inline constexpr TimCaps CAPS_TIM_GP32  = caps_gp32();
inline constexpr TimCaps CAPS_TIM_GP16  = caps_gp16();
inline constexpr TimCaps CAPS_TIM_GP2CH = caps_gp2ch();
inline constexpr TimCaps CAPS_TIM_GP1CH = caps_gp1ch();
inline constexpr TimCaps CAPS_TIM_BASIC = caps_basic();

// ---------------------------------------------------------------------------
// Implementación común. Recibe los rasgos por el constructor: este es el punto
// de selección en tiempo de ejecución.
// ---------------------------------------------------------------------------
class TimerBase : public BusSlave {
public:
    // ---- Puertos ----------------------------------------------------------
    sc_core::sc_in<bool>   timclk{"timclk"};        // TIMCLK1 o TIMCLK2 [IR, §4.4]
    sc_core::sc_in<double> timclk_hz{"timclk_hz"};  // su frecuencia
    sc_core::sc_in<bool>   freeze{"freeze"};        // DBGMCU_APBx_FZ
    sc_core::sc_vector<sc_core::sc_in<bool>> itr;   // [4] triggers internos ITRx

    sc_core::sc_out<bool> irq_global{"irq_global"};       // TIM2..TIM14
    sc_core::sc_out<bool> irq_up{"irq_up"}, irq_cc{"irq_cc"},
                          irq_trg_com{"irq_trg_com"}, irq_brk{"irq_brk"};  // TIM1/8
    sc_core::sc_out<bool> trgo{"trgo"};                   // hacia ADC/DAC/ITRx
    sc_core::sc_out<bool> dma_up{"dma_up"}, dma_trig{"dma_trig"},
                          dma_com{"dma_com"};
    sc_core::sc_vector<sc_core::sc_out<bool>> dma_cc;     // [4]

    // ---- Señales de función alternativa (el top las registra en pin_mux) ---
    sc_core::sc_vector<sc_core::sc_signal<bool>> ch_out, ch_oe, ch_in;  // [4]
    sc_core::sc_vector<sc_core::sc_signal<bool>> chn_out, chn_oe;       // [3]
    sc_core::sc_signal<bool> etr_in{"etr_in"}, bkin_in{"bkin_in"};

    // ---- Offsets [IR, §12.1.4-D] ------------------------------------------
    enum : uint32_t {
        R_CR1 = 0x00, R_CR2 = 0x04, R_SMCR = 0x08, R_DIER = 0x0C, R_SR = 0x10,
        R_EGR = 0x14, R_CCMR1 = 0x18, R_CCMR2 = 0x1C, R_CCER = 0x20,
        R_CNT = 0x24, R_PSC = 0x28, R_ARR = 0x2C, R_RCR = 0x30,
        R_CCR1 = 0x34, R_CCR2 = 0x38, R_CCR3 = 0x3C, R_CCR4 = 0x40,
        R_BDTR = 0x44, R_DCR = 0x48, R_DMAR = 0x4C
    };
    enum SrBit : uint32_t {
        S_UIF = 1u << 0, S_CC1IF = 1u << 1, S_CC2IF = 1u << 2, S_CC3IF = 1u << 3,
        S_CC4IF = 1u << 4, S_COMIF = 1u << 5, S_TIF = 1u << 6, S_BIF = 1u << 7,
        S_CC1OF = 1u << 9, S_CC2OF = 1u << 10, S_CC3OF = 1u << 11,
        S_CC4OF = 1u << 12
    };
    // Líneas de petición de DMA
    enum DmaLine : unsigned { D_UP = 0, D_CC1, D_CC2, D_CC3, D_CC4, D_COM,
                              D_TRIG, D_COUNT };

    // --- Constructor principal: los rasgos como parámetro ------------------
    TimerBase(sc_core::sc_module_name nm, uint32_t base,
              const TimCaps& caps = CAPS_TIM_GP16)
        : BusSlave(nm, base, 0x400), itr("itr", 4), dma_cc("dma_cc", 4),
          ch_out("ch_out", 4), ch_oe("ch_oe", 4), ch_in("ch_in", 4),
          chn_out("chn_out", 3), chn_oe("chn_oe", 3), caps_(caps) {
        arr_ = arr_act_ = cnt_mask();          // reset de ARR = 0xFFFF(FFFF)
        SC_HAS_PROCESS(TimerBase);
        SC_THREAD(tick_proc);
        SC_THREAD(dma_proc);
        SC_THREAD(trgo_proc);
        SC_THREAD(dtg_proc);
        SC_METHOD(pub_proc);    sensitive << pub_ev_;
        SC_METHOD(reset_proc);  sensitive << rst_n;
        SC_METHOD(cap_proc);    dont_initialize();
        for (unsigned c = 0; c < 4; ++c) sensitive << ch_in[c];
        SC_METHOD(trig_proc);   dont_initialize();
        for (unsigned i = 0; i < 4; ++i) sensitive << itr[i];
        sensitive << ch_in[0] << ch_in[1] << etr_in;
        SC_METHOD(brk_proc);    sensitive << bkin_in; dont_initialize();
    }
    // --- Atajo de selección en tiempo de ejecución -------------------------
    // Los dos ejes que más distinguen a un temporizador de otro: la anchura del
    // contador y el número de canales. El resto se deduce de ellos.
    TimerBase(sc_core::sc_module_name nm, uint32_t base, unsigned width_bits,
              unsigned channels)
        : TimerBase(nm, base, runtime_caps(width_bits, channels)) {}

    static TimCaps runtime_caps(unsigned width_bits, unsigned channels) {
        TimCaps c{};
        c.width_bits = (width_bits >= 32) ? 32u : 16u;
        c.channels   = (channels > 4) ? 4u : channels;
        if (c.channels < 4) { c.dma_burst = false; c.center_aligned = false; }
        if (c.channels < 2) { c.slave_mode = false; c.encoder = false;
                              c.ext_trigger = false; }
        if (c.channels == 0) { c.hall = false; }
        c.kind = "a medida";
        return c;
    }

    const TimCaps& caps() const { return caps_; }

    // ---- Observación desde el banco de pruebas ----------------------------
    uint32_t counter()      const { return cnt_now(); }
    bool     counting_down()const { return dir_down_; }
    uint64_t update_events()const { return n_uev_; }
    uint64_t captures()     const { return n_cap_; }
    double   tick_hz()      const { const double f = tim_hz();
                                    return f > 0.0 ? f / double(psc_act_ + 1) : 0.0; }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        if (off == R_DMAR) return read_dmar();
        return reg_read_inner(off);
    }

    uint32_t reg_read_inner(uint32_t off) {
        switch (off) {
            case R_CR1:   return cr1_;
            case R_CR2:   return cr2_;
            case R_SMCR:  return smcr_;
            case R_DIER:  return dier_;
            case R_SR:    return sr_;
            case R_EGR:   return 0;                      // solo escritura
            case R_CCMR1: return ccmr_[0];
            case R_CCMR2: return ccmr_[1];
            case R_CCER:  return ccer_;
            case R_CNT:   return cnt_now();
            case R_PSC:   return psc_;
            case R_ARR:   return arr_;
            case R_RCR:   return caps_.repetition ? rcr_ : 0u;
            case R_CCR1: case R_CCR2: case R_CCR3: case R_CCR4: {
                const unsigned c = (off - R_CCR1) / 4;
                // Leer CCRx en modo captura borra CCxIF [IR, §12.1.4-B]
                if (c < caps_.channels && is_input(c) && (sr_ & (1u << (1 + c)))) {
                    sr_ &= ~(1u << (1 + c));
                    update_irq();
                }
                return ccr_[c];
            }
            case R_BDTR:  return caps_.bdtr ? bdtr_ : 0u;
            case R_DCR:   return caps_.dma_burst ? dcr_ : 0u;
            default:      return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                // acceso parcial
            uint32_t cur = (off == R_EGR || off == R_DMAR) ? 0u : reg_read_inner(off);
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR1: {
                const uint32_t old = cr1_;
                cr1_ = v & cr1_mask();
                // DIR es de lectura/escritura y manda sobre el sentido en cuanto
                // se escribe (salvo en modo alineado al centro, donde es de solo
                // lectura y la maneja el propio contador) [IR, §12.1.4-A].
                if (!cms() && dir_bit() != dir_down_ && !cen()) dir_down_ = dir_bit();
                if (!(old & 1u) && (cr1_ & 1u)) start_counting();
                if ((old ^ cr1_) & (1u << 7)) { if (!arpe()) arr_act_ = arr_ & cnt_mask(); }
                break;
            }
            case R_CR2:   cr2_ = v & cr2_mask(); break;
            case R_SMCR:  smcr_ = v & smcr_mask(); sample_trigger(); break;
            case R_DIER:  dier_ = v & dier_mask(); break;
            case R_SR:                                   // rc_w0
                sr_ &= (v | ~sr_rc_mask());
                break;
            case R_EGR:   write_egr(v & egr_mask()); break;
            case R_CCMR1: ccmr_[0] = v & ccmr_mask(0); refresh_shadows(); break;
            case R_CCMR2: ccmr_[1] = v & ccmr_mask(1); refresh_shadows(); break;
            case R_CCER:  ccer_ = v & ccer_mask(); break;
            case R_CNT:   set_counter(v & cnt_mask()); break;
            case R_PSC:   psc_ = v & 0xFFFFu; break;     // siempre con precarga
            case R_ARR:
                arr_ = v & cnt_mask();
                if (!arpe()) arr_act_ = arr_;            // sin precarga: inmediato
                break;
            case R_RCR:   if (caps_.repetition) rcr_ = v & 0xFFu; break;
            case R_CCR1: case R_CCR2: case R_CCR3: case R_CCR4: {
                const unsigned c = (off - R_CCR1) / 4;
                if (c >= caps_.channels) return;
                ccr_[c] = v & cnt_mask();
                if (!ocpe(c) || is_input(c)) ccr_act_[c] = ccr_[c];
                break;
            }
            case R_BDTR:  if (caps_.bdtr) { bdtr_ = v & 0xFFFFu; } break;
            case R_DCR:   if (caps_.dma_burst) dcr_ = v & 0x1F1Fu; break;
            case R_DMAR:  write_dmar(v); return;         // ya publica por dentro
            default:      return;
        }
        update_outputs();
        update_trgo();
        update_irq();
        wake();
    }

    // El bloque APB responde en un ciclo de PCLK como el resto de periféricos.
    unsigned access_cycles(bool) const override { return 1; }

private:
    TimCaps caps_;

    // ---- Registros --------------------------------------------------------
    uint32_t cr1_ = 0, cr2_ = 0, smcr_ = 0, dier_ = 0, sr_ = 0;
    uint32_t ccmr_[2] = {0, 0}, ccer_ = 0;
    uint32_t psc_ = 0, arr_ = 0xFFFFu, rcr_ = 0, bdtr_ = 0, dcr_ = 0;
    uint32_t ccr_[4] = {0, 0, 0, 0};
    // ---- Copias activas (registros con precarga / sombra) -----------------
    uint32_t psc_act_ = 0, arr_act_ = 0xFFFFu, ccr_act_[4] = {0, 0, 0, 0};
    uint32_t rep_ = 0;                     // contador de repeticiones actual
    uint32_t burst_i_ = 0;                 // índice del modo ráfaga DMAR

    // ---- Estado del contador ----------------------------------------------
    uint32_t cnt_ = 0;                     // valor en t_anchor_
    bool     dir_down_ = false;
    bool     ticking_ = false;
    uint64_t steps_ = 0;                   // pasos hasta el próximo suceso
    sc_core::sc_time t_anchor_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_time t_tick_   = sc_core::SC_ZERO_TIME;
    uint64_t n_uev_ = 0, n_cap_ = 0;

    // ---- Estado de las entradas -------------------------------------------
    bool ti_prev_[4] = {false, false, false, false};
    bool trgi_prev_ = false;
    unsigned ic_div_[4] = {0, 0, 0, 0};    // divisor de captura ICxPSC
    bool slave_gate_ = true;               // nivel de puerta en modo gated

    // ---- Salidas publicadas por un único proceso --------------------------
    bool ref_[4] = {false, false, false, false};       // OCxREF
    bool o_ch_[4] = {false, false, false, false}, o_che_[4] = {false, false, false, false};
    bool o_chn_[3] = {false, false, false}, o_chne_[3] = {false, false, false};
    bool o_irq_g_ = false, o_irq_up_ = false, o_irq_cc_ = false;
    bool o_irq_tc_ = false, o_irq_brk_ = false;
    bool o_trgo_ = false;
    unsigned o_dma_ = 0, dma_pend_ = 0;
    unsigned dead_mask_ = 0;               // canales en tiempo muerto
    sc_core::sc_event pub_ev_, wake_ev_, dma_ev_, trgo_ev_, dtg_ev_;

    // =======================================================================
    // Campos de los registros
    // =======================================================================
    uint32_t cnt_mask() const { return caps_.width_bits >= 32 ? 0xFFFFFFFFu : 0xFFFFu; }
    bool     cen()   const { return cr1_ & 1u; }
    bool     udis()  const { return (cr1_ >> 1) & 1u; }
    bool     urs()   const { return (cr1_ >> 2) & 1u; }
    bool     opm()   const { return caps_.one_pulse && ((cr1_ >> 3) & 1u); }
    bool     dir_bit() const { return caps_.down_count && ((cr1_ >> 4) & 1u); }
    unsigned cms()   const { return caps_.center_aligned ? ((cr1_ >> 5) & 3u) : 0u; }
    bool     arpe()  const { return (cr1_ >> 7) & 1u; }
    unsigned ckd()   const { return caps_.clock_division ? ((cr1_ >> 8) & 3u) : 0u; }
    unsigned mms()   const { return caps_.trgo ? ((cr2_ >> 4) & 7u) : 0u; }
    bool     ccds()  const { return (cr2_ >> 3) & 1u; }
    bool     ti1s()  const { return caps_.hall && ((cr2_ >> 7) & 1u); }
    unsigned sms()   const { return caps_.slave_mode ? (smcr_ & 7u) : 0u; }
    unsigned ts()    const { return (smcr_ >> 4) & 7u; }
    bool     ece()   const { return caps_.ext_trigger && ((smcr_ >> 14) & 1u); }
    bool     etp()   const { return caps_.ext_trigger && ((smcr_ >> 15) & 1u); }
    bool     moe()   const { return !caps_.bdtr || ((bdtr_ >> 15) & 1u); }
    bool     aoe()   const { return (bdtr_ >> 14) & 1u; }
    bool     bkp()   const { return (bdtr_ >> 13) & 1u; }
    bool     bke()   const { return caps_.bdtr && ((bdtr_ >> 12) & 1u); }
    bool     ossr()  const { return (bdtr_ >> 11) & 1u; }
    bool     ossi()  const { return (bdtr_ >> 10) & 1u; }
    unsigned dtg()   const { return bdtr_ & 0xFFu; }
    unsigned dba()   const { return dcr_ & 0x1Fu; }
    unsigned dbl()   const { return (dcr_ >> 8) & 0x1Fu; }
    // Campos por canal (CCMRx dividido en dos mitades de 8 bits)
    unsigned ccs(unsigned c)  const { return (ccmr_[c / 2] >> (8 * (c & 1))) & 3u; }
    bool     is_input(unsigned c) const { return ccs(c) != 0; }
    unsigned ocm(unsigned c)  const { return (ccmr_[c / 2] >> (8 * (c & 1) + 4)) & 7u; }
    bool     ocpe(unsigned c) const { return (ccmr_[c / 2] >> (8 * (c & 1) + 3)) & 1u; }
    unsigned icpsc(unsigned c)const { return (ccmr_[c / 2] >> (8 * (c & 1) + 2)) & 3u; }
    bool     cce(unsigned c)  const { return (ccer_ >> (4 * c)) & 1u; }
    bool     ccp(unsigned c)  const { return (ccer_ >> (4 * c + 1)) & 1u; }
    bool     ccne(unsigned c) const { return (ccer_ >> (4 * c + 2)) & 1u; }
    bool     ccnp(unsigned c) const { return (ccer_ >> (4 * c + 3)) & 1u; }
    bool     ois(unsigned c)  const { return (cr2_ >> (8 + 2 * c)) & 1u; }
    bool     oisn(unsigned c) const { return (cr2_ >> (9 + 2 * c)) & 1u; }

    // =======================================================================
    // Máscaras de escritura: aquí es donde los rasgos se vuelven observables.
    // Un campo que la variante no tiene es RESERVADO: no se escribe y lee cero.
    // =======================================================================
    uint32_t cr1_mask() const {
        uint32_t m = 0x0087u;                              // CEN, UDIS, URS, ARPE
        if (caps_.one_pulse)      m |= 1u << 3;            // OPM
        if (caps_.down_count)     m |= 1u << 4;            // DIR
        if (caps_.center_aligned) m |= 3u << 5;            // CMS
        if (caps_.clock_division) m |= 3u << 8;            // CKD
        return m;
    }
    uint32_t cr2_mask() const {
        uint32_t m = 0;
        if (caps_.trgo)          m |= 7u << 4;             // MMS
        if (caps_.dma)           m |= 1u << 3;             // CCDS
        if (caps_.hall)          m |= 1u << 7;             // TI1S
        if (caps_.comp_channels) m |= (1u << 0) | (1u << 2);   // CCPC, CCUS
        if (caps_.bdtr) {                                  // OISx / OISxN
            for (unsigned c = 0; c < caps_.channels; ++c) m |= 1u << (8 + 2 * c);
            for (unsigned c = 0; c < caps_.comp_channels; ++c) m |= 1u << (9 + 2 * c);
        }
        return m;
    }
    uint32_t smcr_mask() const {
        uint32_t m = 0;
        if (caps_.slave_mode)  m |= 0x00F7u;               // SMS, TS, MSM
        if (caps_.ext_trigger) m |= 0xFF00u;               // ETF, ETPS, ECE, ETP
        return m;
    }
    uint32_t dier_mask() const {
        uint32_t m = 1u << 0;                              // UIE
        for (unsigned c = 0; c < caps_.channels; ++c) m |= 1u << (1 + c);
        if (caps_.comp_channels) m |= 1u << 5;             // COMIE
        if (caps_.slave_mode)    m |= 1u << 6;             // TIE
        if (caps_.bdtr)          m |= 1u << 7;             // BIE
        if (caps_.dma) {
            m |= 1u << 8;                                  // UDE
            for (unsigned c = 0; c < caps_.channels; ++c) m |= 1u << (9 + c);
            if (caps_.comp_channels) m |= 1u << 13;        // COMDE
            if (caps_.slave_mode)    m |= 1u << 14;        // TDE
        }
        return m;
    }
    uint32_t sr_rc_mask() const {                          // bits borrables
        uint32_t m = 1u << 0;
        for (unsigned c = 0; c < caps_.channels; ++c) m |= (1u << (1 + c)) | (1u << (9 + c));
        if (caps_.comp_channels) m |= 1u << 5;
        if (caps_.slave_mode)    m |= 1u << 6;
        if (caps_.bdtr)          m |= 1u << 7;
        return m;
    }
    uint32_t egr_mask() const {
        uint32_t m = 1u << 0;                              // UG
        for (unsigned c = 0; c < caps_.channels; ++c) m |= 1u << (1 + c);
        if (caps_.comp_channels) m |= 1u << 5;             // COMG
        if (caps_.slave_mode)    m |= 1u << 6;             // TG
        if (caps_.bdtr)          m |= 1u << 7;             // BG
        return m;
    }
    uint32_t ccmr_mask(unsigned half) const {
        uint32_t m = 0;
        for (unsigned k = 0; k < 2; ++k) {
            const unsigned c = 2 * half + k;
            if (c < caps_.channels) m |= 0xFFu << (8 * k);
        }
        return m;
    }
    uint32_t ccer_mask() const {
        uint32_t m = 0;
        for (unsigned c = 0; c < caps_.channels; ++c) {
            m |= (1u << (4 * c)) | (1u << (4 * c + 1));    // CCxE, CCxP
            m |= 1u << (4 * c + 3);                        // CCxNP
            if (c < caps_.comp_channels) m |= 1u << (4 * c + 2);   // CCxNE
        }
        return m;
    }

    // =======================================================================
    // Base de tiempos
    // =======================================================================
    double tim_hz() const { return timclk_hz.read(); }

    // Periodo de un paso del contador: (PSC+1) / f_TIMCLK.
    sc_core::sc_time tick_time() const {
        const double f = tim_hz();
        if (f <= 0.0) return sc_core::SC_ZERO_TIME;
        return sc_core::sc_time(double(psc_act_ + 1) * 1.0e12 / f, sc_core::SC_PS);
    }

    // Modos en los que el contador NO avanza con el tiempo, sino por flancos.
    bool ext_clocked() const {
        return (caps_.slave_mode && sms() == 7) || ece();
    }
    bool encoder_mode() const {
        return caps_.encoder && caps_.slave_mode && sms() >= 1 && sms() <= 3;
    }
    bool gated_mode() const { return caps_.slave_mode && sms() == 5; }

    bool tick_enabled() const {
        if (!cen() || !rst_n.read() || !clock_enabled()) return false;
        if (freeze.read()) return false;                   // depurador [IR, §13]
        if (ext_clocked() || encoder_mode()) return false;
        if (gated_mode() && !slave_gate_) return false;
        return true;
    }

    // Valor del contador en el instante actual, interpolado desde el ancla.
    uint32_t cnt_now() const {
        if (!ticking_ || t_tick_ == sc_core::SC_ZERO_TIME) return cnt_;
        const double d = (sc_core::sc_time_stamp() - t_anchor_) / t_tick_;
        uint64_t n = (d > 0.0) ? uint64_t(d + 1e-9) : 0u;
        if (n > steps_) n = steps_;                        // nunca pasa del suceso
        if (!dir_down_) {
            const uint64_t v = uint64_t(cnt_) + n;
            return uint32_t(v > arr_act_ ? arr_act_ : v);
        }
        return uint32_t(n >= cnt_ ? 0u : cnt_ - n);
    }

    void set_counter(uint32_t v) {
        cnt_ = v;
        t_anchor_ = sc_core::sc_time_stamp();
        steps_ = 0;                      // hasta que el proceso recalcule
        wake();
    }
    void start_counting() {
        cnt_ = cnt_now();
        t_anchor_ = sc_core::sc_time_stamp();
        steps_ = 0;
        dir_down_ = dir_bit();
        wake();
    }

    // Pasos hasta el siguiente suceso (comparación o desbordamiento). >= 1.
    uint64_t steps_to_event() const {
        uint64_t best;
        if (cms()) {
            best = dir_down_ ? uint64_t(cnt_) : uint64_t(arr_act_ - min32(cnt_, arr_act_));
            if (best == 0) best = 1;
        } else if (!dir_down_) {
            best = (cnt_ <= arr_act_) ? uint64_t(arr_act_ - cnt_) + 1u : 1u;
        } else {
            best = uint64_t(cnt_) + 1u;
        }
        for (unsigned c = 0; c < caps_.channels; ++c) {
            if (is_input(c)) continue;
            const uint32_t r = ccr_act_[c];
            if (r > arr_act_) continue;
            uint64_t d;
            if (!dir_down_) { if (r <= cnt_) continue; d = uint64_t(r) - cnt_; }
            else            { if (r >= cnt_) continue; d = uint64_t(cnt_) - r; }
            if (d < best) best = d;
        }
        return best ? best : 1u;
    }
    static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

    // -----------------------------------------------------------------------
    // Proceso de cuenta: espera exactamente hasta el próximo suceso.
    // -----------------------------------------------------------------------
    void tick_proc() {
        for (;;) {
            if (!tick_enabled() || tim_hz() <= 0.0) {
                if (ticking_) { cnt_ = cnt_now(); ticking_ = false; }
                wait(wake_ev_ | rst_n.value_changed_event() |
                     freeze.value_changed_event() | timclk_hz.value_changed_event());
                continue;
            }
            if (ticking_) cnt_ = cnt_now();
            t_anchor_ = sc_core::sc_time_stamp();
            t_tick_   = tick_time();
            ticking_  = true;
            steps_    = steps_to_event();
            const sc_core::sc_time dt = t_tick_ * double(steps_);
            wait(dt, wake_ev_ | rst_n.value_changed_event() |
                     freeze.value_changed_event() | timclk_hz.value_changed_event());
            if (sc_core::sc_time_stamp() < t_anchor_ + dt) continue;   // interrumpido
            arrive(steps_);
        }
    }

    // Llegada al suceso: avanza el contador y dispara lo que corresponda.
    void arrive(uint64_t n) {
        t_anchor_ = sc_core::sc_time_stamp();
        bool uev = false;
        if (cms()) {                                       // alineado al centro
            if (!dir_down_) {
                cnt_ = uint32_t(cnt_ + n);
                if (cnt_ >= arr_act_) { cnt_ = arr_act_; dir_down_ = true; uev = true; }
            } else {
                cnt_ = (n >= cnt_) ? 0u : uint32_t(cnt_ - n);
                if (cnt_ == 0) { dir_down_ = false; uev = true; }
            }
        } else if (!dir_down_) {                           // ascendente
            if (uint64_t(cnt_) + n > arr_act_) { cnt_ = 0; uev = true; }
            else cnt_ = uint32_t(cnt_ + n);
        } else {                                           // descendente
            if (n > cnt_) { cnt_ = arr_act_; uev = true; }
            else cnt_ = uint32_t(cnt_ - n);
        }
        if (uev) update_event(false);
        compare_match();
        update_outputs(true);
        update_trgo();
        update_irq();
    }

    // Un solo paso del contador, para los modos de reloj externo/codificador.
    void step_counter(bool down) {
        const bool saved = dir_down_;
        dir_down_ = down;
        cnt_ = cnt_now();
        t_anchor_ = sc_core::sc_time_stamp();
        arrive(1);
        if (!cms()) dir_down_ = saved;                     // la dirección la manda el eje
        wake();
    }

    // -----------------------------------------------------------------------
    // Evento de update (desbordamiento, subdesbordamiento o UG por software)
    // -----------------------------------------------------------------------
    void update_event(bool sw) {
        if (udis()) return;                                // UEV inhibido
        if (caps_.repetition && !sw) {
            if (rep_ > 0) { --rep_; return; }              // aún no toca
            rep_ = rcr_;
        }
        // Recarga de los registros con precarga [IR, §12.1.4-A: ARPE]
        psc_act_ = psc_;
        if (arpe()) arr_act_ = arr_ & cnt_mask();
        for (unsigned c = 0; c < caps_.channels; ++c)
            if (!is_input(c) && ocpe(c)) ccr_act_[c] = ccr_[c] & cnt_mask();
        if (sw) {
            cnt_ = dir_down_ ? arr_act_ : 0u;
            t_anchor_ = sc_core::sc_time_stamp();
            steps_ = 0;
            rep_ = rcr_;
        }
        ++n_uev_;
        if (!(sw && urs())) {                              // URS: solo desbordes
            sr_ |= S_UIF;
            req_dma(D_UP);
        }
        if (mms() == 2) pulse_trgo();
        if (opm() && !sw) { cr1_ &= ~1u; ticking_ = false; }   // un solo pulso
    }

    // Comparación: banderas CCxIF al alcanzar CCRx en modo salida.
    void compare_match() {
        for (unsigned c = 0; c < caps_.channels; ++c) {
            if (is_input(c)) continue;
            if (cnt_ != ccr_act_[c]) continue;
            if (cms()) {                                   // CMS filtra el flanco
                if (cms() == 1 && !dir_down_) continue;
                if (cms() == 2 && dir_down_)  continue;
            }
            sr_ |= 1u << (1 + c);
            req_dma(D_CC1 + c);
            if (mms() == 3 && c == 0) pulse_trgo();        // TRGO = pulso de CC1
        }
    }

    void refresh_shadows() {
        for (unsigned c = 0; c < caps_.channels; ++c)
            if (is_input(c) || !ocpe(c)) ccr_act_[c] = ccr_[c] & cnt_mask();
    }

    // EGR: generación de sucesos por software [IR, §12.1.4-D]
    void write_egr(uint32_t v) {
        if (v & (1u << 0)) update_event(true);             // UG
        for (unsigned c = 0; c < caps_.channels; ++c)
            if (v & (1u << (1 + c))) { sr_ |= 1u << (1 + c); req_dma(D_CC1 + c); }
        if (v & (1u << 5)) { sr_ |= S_COMIF; req_dma(D_COM); }
        if (v & (1u << 6)) { sr_ |= S_TIF;   req_dma(D_TRIG); }
        if (v & (1u << 7)) { sr_ |= S_BIF; if (caps_.bdtr) bdtr_ &= ~(1u << 15); }
        if (mms() == 0 && (v & 1u)) pulse_trgo();          // TRGO = reset (UG)
    }

    // =======================================================================
    // Salidas de comparación, complementarias y tiempo muerto
    // =======================================================================
    // cv: valor del contador con el que se evalúa; at_event: la evaluación se
    // hace en el instante del suceso (solo entonces los modos "al igualar"
    // conmutan la referencia; en una lectura o escritura de registro no).
    bool oc_ref(unsigned c, uint32_t cv, bool at_event) const {
        const uint32_t r = ccr_act_[c];
        const bool eq = at_event && (cv == r);
        switch (ocm(c)) {
            case 0: return ref_[c];                              // congelado
            case 1: return eq ? true  : ref_[c];                 // activo al igualar
            case 2: return eq ? false : ref_[c];                 // inactivo al igualar
            case 3: return eq ? !ref_[c] : ref_[c];              // conmutar
            case 4: return false;                                // forzado inactivo
            case 5: return true;                                 // forzado activo
            case 6: return dir_down_ ? (cv <= r) : (cv < r);     // PWM modo 1
            case 7: return !(dir_down_ ? (cv <= r) : (cv < r));  // PWM modo 2
            default: return ref_[c];
        }
    }

    // Tiempo muerto: DTG[7:0] codifica el retardo en pasos de t_DTS [IR, §12.1.4-C]
    sc_core::sc_time dead_time() const {
        const double f = tim_hz();
        if (f <= 0.0 || !caps_.bdtr) return sc_core::SC_ZERO_TIME;
        const double t_dts = double(1u << ckd()) / f;             // CKD divide t_DTS
        const unsigned d = dtg();
        double n;
        if      ((d & 0x80u) == 0x00u) n = d & 0x7Fu;
        else if ((d & 0xC0u) == 0x80u) n = (64.0 + (d & 0x3Fu)) * 2.0;
        else if ((d & 0xE0u) == 0xC0u) n = (32.0 + (d & 0x1Fu)) * 8.0;
        else                           n = (32.0 + (d & 0x1Fu)) * 16.0;
        return sc_core::sc_time(n * t_dts * 1.0e12, sc_core::SC_PS);
    }

    void update_outputs(bool at_event = false) {
        const uint32_t cv = at_event ? cnt_ : cnt_now();
        unsigned changed = 0;
        for (unsigned c = 0; c < caps_.channels; ++c) {
            if (is_input(c)) { o_che_[c] = false; continue; }
            const bool nr = oc_ref(c, cv, at_event);
            if (nr != ref_[c]) changed |= 1u << c;
            ref_[c] = nr;
        }
        // Un cambio en un canal con salida complementaria abre el tiempo muerto
        if (caps_.comp_channels && dtg() != 0) {
            unsigned m = changed & ((1u << caps_.comp_channels) - 1u);
            if (m) { dead_mask_ |= m; dtg_ev_.notify(sc_core::SC_ZERO_TIME); }
        }
        publish_pins();
    }

    void publish_pins() {
        for (unsigned c = 0; c < caps_.channels; ++c) {
            if (is_input(c)) { o_che_[c] = false; if (c < 3) o_chne_[c] = false; continue; }
            const bool dead = (dead_mask_ >> c) & 1u;
            const bool act  = moe() ? (dead ? false : ref_[c]) : ois(c);
            o_ch_[c]  = act ^ ccp(c);
            o_che_[c] = cce(c) && (moe() || ossi());
            if (c < caps_.comp_channels) {
                const bool actn = moe() ? (dead ? false : !ref_[c]) : oisn(c);
                o_chn_[c]  = actn ^ ccnp(c);
                o_chne_[c] = ccne(c) && (moe() || ossi());
            }
        }
        publish();
    }

    // Inserción del tiempo muerto: las dos salidas del par quedan inactivas
    // durante t_DTG antes de que se active la que corresponde [IR, §12.1.1].
    void dtg_proc() {
        for (;;) {
            wait(dtg_ev_);
            if (!dead_mask_) continue;
            publish_pins();
            const sc_core::sc_time td = dead_time();
            if (td > sc_core::SC_ZERO_TIME) wait(td);
            dead_mask_ = 0;
            publish_pins();
        }
    }

    // Entrada de freno: fuerza MOE a 0 y levanta BIF [IR, §12.1.4-C]
    void brk_proc() {
        if (!bke()) return;
        if (bkin_in.read() != bkp()) return;               // polaridad BKP
        bdtr_ &= ~(1u << 15);                              // MOE = 0
        sr_ |= S_BIF;
        publish_pins();
        update_irq();
        if (aoe()) { /* el rearme automático ocurre en el siguiente UEV */ }
    }

    // =======================================================================
    // Captura de entrada
    // =======================================================================
    // Nivel de la entrada TIx tras la selección CCxS y el XOR de Hall (TI1S).
    bool ti_level(unsigned c) const {
        if (c == 0 && ti1s())
            return ch_in[0].read() ^ ch_in[1].read() ^ ch_in[2].read();
        return ch_in[c].read();
    }
    // Fuente física del canal c según CCxS: 01 = TIx directa, 10 = TI vecina.
    unsigned ic_source(unsigned c) const {
        const unsigned s = ccs(c);
        if (s == 1) return c;
        if (s == 2) return c ^ 1u;                         // CH1<->CH2, CH3<->CH4
        return c;                                          // 11 = TRC (aprox.)
    }

    void cap_proc() {
        bool now[4];
        for (unsigned c = 0; c < 4; ++c) now[c] = ti_level(c);
        for (unsigned c = 0; c < caps_.channels; ++c) {
            if (!is_input(c)) continue;
            const unsigned src = ic_source(c);
            const bool lv = now[src], pv = ti_prev_[src];
            if (lv == pv) continue;
            // Polaridad: CCxP=0 subida, CCxP=1 bajada, CCxP+CCxNP ambos flancos
            const bool both = ccp(c) && ccnp(c);
            if (!both && (lv != !ccp(c))) continue;
            if (++ic_div_[c] < (1u << icpsc(c))) continue; // prescaler ICxPSC
            ic_div_[c] = 0;
            if (sr_ & (1u << (1 + c))) sr_ |= 1u << (9 + c);   // CCxOF
            ccr_[c] = ccr_act_[c] = cnt_now();
            sr_ |= 1u << (1 + c);
            ++n_cap_;
            req_dma(D_CC1 + c);
        }
        for (unsigned c = 0; c < 4; ++c) ti_prev_[c] = now[c];
        if (encoder_mode()) encoder_step(now);
        update_irq();
        publish();
    }

    // Codificador incremental: cuenta por los flancos de TI1 y/o TI2 [RM, §18.3.12]
    void encoder_step(const bool now[4]) {
        const bool ti1 = now[0] ^ ccp(0), ti2 = now[1] ^ ccp(1);
        const bool e1 = (now[0] != enc_prev_[0]), e2 = (now[1] != enc_prev_[1]);
        enc_prev_[0] = now[0]; enc_prev_[1] = now[1];
        const unsigned m = sms();
        bool step = false, down = false;
        if (e1 && (m == 2 || m == 3)) {                    // cuenta por TI1
            const bool rising = ti1;
            down = !(rising ^ ti2);
            step = true;
        }
        if (e2 && (m == 1 || m == 3)) {                    // cuenta por TI2
            const bool rising = ti2;
            down = (rising ^ ti1);
            step = true;
        }
        if (step && cen() && clock_enabled() && !freeze.read()) step_counter(down);
    }
    bool enc_prev_[2] = {false, false};

    // =======================================================================
    // Controlador de esclavo y triggers
    // =======================================================================
    bool trgi_level() const {
        switch (ts()) {
            case 0: case 1: case 2: case 3: return itr[ts()].read();
            case 4: return ti_level(0);                    // TI1F_ED (ambos flancos)
            case 5: return ti_level(0) ^ ccp(0);           // TI1FP1
            case 6: return ti_level(1) ^ ccp(1);           // TI2FP2
            default: return etr_in.read() ^ etp();         // ETRF
        }
    }
    void sample_trigger() { trgi_prev_ = trgi_level(); slave_gate_ = trgi_prev_; }

    void trig_proc() {
        const bool lv = trgi_level();
        const bool changed = (lv != trgi_prev_);
        const bool rising  = lv && !trgi_prev_;
        trgi_prev_ = lv;
        slave_gate_ = lv;

        // Modo de reloj externo 2 (ECE): ETRF cuenta directamente, sea cual sea
        // el modo de esclavo [IR, §12.1.4-D: SMCR.ECE].
        if (ece()) {
            const bool e = etr_in.read() ^ etp();
            if (e && !etr_prev_) { if (cen()) step_counter(dir_bit()); }
            etr_prev_ = e;
        }
        if (!caps_.slave_mode) { update_irq(); return; }

        const unsigned m = sms();
        const bool edge = (ts() == 4) ? changed : rising;  // TI1F_ED: ambos flancos
        switch (m) {
            case 4:                                        // reset
                if (edge) {
                    cnt_ = dir_down_ ? arr_act_ : 0u;
                    t_anchor_ = sc_core::sc_time_stamp();
                    rep_ = rcr_;
                    sr_ |= S_TIF; req_dma(D_TRIG);
                    if (!udis() && !urs()) { sr_ |= S_UIF; req_dma(D_UP); }
                    wake();
                }
                break;
            case 5:                                        // gated
                if (edge) { sr_ |= S_TIF; req_dma(D_TRIG); }
                wake();                                    // la puerta abre/cierra
                break;
            case 6:                                        // trigger
                if (edge && !cen()) {
                    cr1_ |= 1u; start_counting();
                    sr_ |= S_TIF; req_dma(D_TRIG);
                }
                break;
            case 7:                                        // reloj externo modo 1
                if (edge && cen()) step_counter(dir_bit());
                break;
            default: break;
        }
        update_irq();
        publish();
    }
    bool etr_prev_ = false;

    // =======================================================================
    // TRGO, interrupciones y peticiones de DMA
    // =======================================================================
    void update_trgo() {
        if (!caps_.trgo) { o_trgo_ = false; return; }
        switch (mms()) {
            case 1: o_trgo_ = cen(); break;                // enable
            case 4: case 5: case 6: case 7: {
                const unsigned c = mms() - 4;
                o_trgo_ = (c < caps_.channels) ? ref_[c] : false;
                break;
            }
            default: break;                                // 0/2/3: son pulsos
        }
    }
    void pulse_trgo() { if (caps_.trgo) trgo_ev_.notify(sc_core::SC_ZERO_TIME); }
    // Un pulso de TRGO dura un ciclo de cuenta, como en el silicio; el ADC y el
    // DAC lo detectan por flanco.
    void trgo_proc() {
        for (;;) {
            wait(trgo_ev_);
            o_trgo_ = true; publish();
            sc_core::sc_time w = t_tick_;
            if (w == sc_core::SC_ZERO_TIME) w = sc_core::sc_time(10, sc_core::SC_NS);
            wait(w);
            o_trgo_ = false; publish();
        }
    }

    void update_irq() {
        const bool up  = (sr_ & S_UIF) && (dier_ & 1u);
        bool cc = false;
        for (unsigned c = 0; c < caps_.channels; ++c)
            if ((sr_ & (1u << (1 + c))) && (dier_ & (1u << (1 + c)))) cc = true;
        const bool com = (sr_ & S_COMIF) && (dier_ & (1u << 5));
        const bool trg = (sr_ & S_TIF)   && (dier_ & (1u << 6));
        const bool brk = (sr_ & S_BIF)   && (dier_ & (1u << 7));
        if (caps_.split_irq) {
            o_irq_up_ = up; o_irq_cc_ = cc; o_irq_tc_ = com || trg; o_irq_brk_ = brk;
            o_irq_g_  = false;
        } else {
            o_irq_g_ = up || cc || com || trg || brk;
            o_irq_up_ = o_irq_cc_ = o_irq_tc_ = o_irq_brk_ = false;
        }
        publish();
    }

    // Bit de habilitación de DMA de cada línea en DIER
    uint32_t de_bit(unsigned line) const {
        switch (line) {
            case D_UP:   return 1u << 8;
            case D_COM:  return 1u << 13;
            case D_TRIG: return 1u << 14;
            default:     return 1u << (9 + (line - D_CC1));
        }
    }
    void req_dma(unsigned line) {
        if (!caps_.dma) return;
        if (!(dier_ & de_bit(line))) return;
        dma_pend_ |= 1u << line;
        dma_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // La petición del temporizador al DMA es un PULSO (en el silicio la retira
    // el reconocimiento del controlador). El modelo del DMA muestrea el nivel,
    // así que el pulso se emite con la anchura mínima que garantiza un único
    // servicio: el controlador arbitra en el mismo instante y su transferencia
    // ya consume tiempo de simulación, de modo que en el siguiente arbitraje la
    // línea está de nuevo baja.
    void dma_proc() {
        for (;;) {
            wait(dma_ev_);
            while (dma_pend_) {
                o_dma_ = dma_pend_;
                dma_pend_ = 0;
                publish();
                wait(sc_core::sc_time(1, sc_core::SC_PS));
                o_dma_ = 0;
                publish();
            }
        }
    }

    // =======================================================================
    // Modo ráfaga de DMA: DCR/DMAR [IR, §12.1.4-D]
    // =======================================================================
    uint32_t burst_off() const { return (dba() + burst_i_) * 4u; }
    void     burst_next() { burst_i_ = (burst_i_ >= dbl()) ? 0u : burst_i_ + 1u; }
    uint32_t read_dmar() {
        if (!caps_.dma_burst) return 0;
        const uint32_t v = reg_read_inner(burst_off());
        burst_next();
        return v;
    }
    void write_dmar(uint32_t v) {
        if (!caps_.dma_burst) return;
        const uint32_t off = burst_off();
        burst_next();
        if (off != R_DMAR) reg_write(off, v, 0xFu);        // sin recursión
    }

    // =======================================================================
    // Publicación (regla de un solo escritor por señal)
    // =======================================================================
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void wake()    { wake_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq_global.write(o_irq_g_);
        irq_up.write(o_irq_up_);
        irq_cc.write(o_irq_cc_);
        irq_trg_com.write(o_irq_tc_);
        irq_brk.write(o_irq_brk_);
        trgo.write(o_trgo_);
        dma_up.write((o_dma_ >> D_UP) & 1u);
        dma_trig.write((o_dma_ >> D_TRIG) & 1u);
        dma_com.write((o_dma_ >> D_COM) & 1u);
        for (unsigned c = 0; c < 4; ++c) dma_cc[c].write((o_dma_ >> (D_CC1 + c)) & 1u);
        for (unsigned c = 0; c < 4; ++c) {
            ch_out[c].write(o_ch_[c]);
            ch_oe[c].write(o_che_[c]);
        }
        for (unsigned c = 0; c < 3; ++c) {
            chn_out[c].write(o_chn_[c]);
            chn_oe[c].write(o_chne_[c]);
        }
    }

    void reset_proc() {
        if (rst_n.read()) return;
        cr1_ = cr2_ = smcr_ = dier_ = sr_ = ccer_ = 0;
        ccmr_[0] = ccmr_[1] = 0;
        psc_ = psc_act_ = 0; rcr_ = rep_ = 0; bdtr_ = 0; dcr_ = 0; burst_i_ = 0;
        arr_ = arr_act_ = cnt_mask();
        for (unsigned c = 0; c < 4; ++c) { ccr_[c] = ccr_act_[c] = 0; ic_div_[c] = 0;
                                           ref_[c] = false; }
        cnt_ = 0; dir_down_ = false; ticking_ = false; steps_ = 0;
        t_anchor_ = sc_core::sc_time_stamp();
        n_uev_ = n_cap_ = 0;
        dead_mask_ = 0; o_dma_ = 0; dma_pend_ = 0; o_trgo_ = false;
        for (unsigned c = 0; c < 4; ++c) { o_ch_[c] = false; o_che_[c] = false; }
        for (unsigned c = 0; c < 3; ++c) { o_chn_[c] = false; o_chne_[c] = false; }
        update_irq();
        publish();
        wake();
    }
};

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN. El parámetro de plantilla es una
// referencia a los rasgos, que quedan disponibles como constante de la clase:
// el tipo expresa la variante y TimAdvanced, TimGp32 y TimBasic son tipos
// distintos que no se pueden confundir en el netlist del top.
// ---------------------------------------------------------------------------
template <const TimCaps& Caps>
class TimT : public TimerBase {
public:
    TimT(sc_core::sc_module_name nm, uint32_t base) : TimerBase(nm, base, Caps) {}
    static constexpr const TimCaps& variant() { return Caps; }
    static constexpr unsigned width()    { return Caps.width_bits; }
    static constexpr unsigned channels() { return Caps.channels; }
    static constexpr bool has_bdtr()     { return Caps.bdtr; }
    static constexpr bool is_32bit()     { return Caps.width_bits >= 32; }
};

using TimAdvanced = TimT<CAPS_TIM_ADV>;    // TIM1, TIM8
using TimGp32     = TimT<CAPS_TIM_GP32>;   // TIM2, TIM5
using TimGp16     = TimT<CAPS_TIM_GP16>;   // TIM3, TIM4
using TimGp2Ch    = TimT<CAPS_TIM_GP2CH>;  // TIM9, TIM12
using TimGp1Ch    = TimT<CAPS_TIM_GP1CH>;  // TIM10, TIM11, TIM13, TIM14
using TimBasic    = TimT<CAPS_TIM_BASIC>;  // TIM6, TIM7

static_assert(TimAdvanced::has_bdtr(),  "los avanzados tienen freno y tiempo muerto");
static_assert(!TimGp32::has_bdtr(),     "TIM2/TIM5 no tienen BDTR");
static_assert(TimGp32::is_32bit(),      "TIM2/TIM5 son de 32 bits");
static_assert(!TimGp16::is_32bit(),     "TIM3/TIM4 son de 16 bits");
static_assert(TimGp2Ch::channels() == 2, "TIM9/TIM12 tienen dos canales");
static_assert(TimGp1Ch::channels() == 1, "TIM10/11/13/14 tienen un canal");
static_assert(TimBasic::channels() == 0, "TIM6/TIM7 no tienen canales");

} // namespace stm32
#endif // STM32_PERIPH_TIMERS_H
