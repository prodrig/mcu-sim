// =============================================================================
// adc_v3.h — El ADC de la segunda generación de ST: el del STM32F303 (fase S5)
//
// [RM0316 §15; CMSIS stm32f303xc.h, ADC_TypeDef y ADC_Common_TypeDef]
//
// Es OTRO bloque que el del F4 (`adc.h`), no una variante: otro mapa de
// registros, otro ritual de arranque y otro modo de pensar. Lo que cambia:
//
//   * RITUAL DE ARRANQUE: el regulador interno (ADVREGEN 10 -> 00 -> 01 y
//     10 us de espera, T_ADCVREG_STUP), la calibración (ADCAL, con un factor
//     para entradas simples y otro para diferenciales, 112 ciclos), y ADEN
//     hasta ADRDY (t_STAB, 1 us) [§15.3.6-9; DS9118 tabla 64]. El modelo lo
//     sigue paso a paso, y lo que el silicio deja hacer mal lo AVISA: un ADEN
//     con el regulador apagado no hace nada; uno antes de los 10 us, o una
//     conversión sin calibrar, convierten, pero con un error de desplazamiento
//     [de modelo: 16 cuentas] que es lo que se ve en la placa;
//   * ENTRADAS: 18 canales, simples o DIFERENCIALES (DIFSEL: el canal i contra
//     el i+1), con su tiempo de muestreo de 1,5 a 601,5 ciclos y 12,5 de
//     aproximación (menos con RES) [§15.3.7, §15.3.12, tabla 94];
//   * SECUENCIAS: 16 regulares (SQR1-4) y 4 inyectadas (JSQR) con su COLA DE
//     CONTEXTOS de dos (JQM, JQOVF) [§15.3.21]; continuo, discontinuo (DISCEN,
//     DISCNUM, JDISCEN), autoinyección (JAUTO) y AUTDLY;
//   * DATOS: alineación, desplazamientos OFR1-4 con signo, OVR con OVRMOD, DMA
//     de una vez o circular (DMACFG) [§15.3.26];
//   * TRES PERROS: AWD1 a 12 bits sobre uno o todos los canales, AWD2 y AWD3 a
//     8 bits sobre una lista [§15.3.28];
//   * POR PAREJAS: ADC1 y ADC2 (ADC3 y ADC4) comparten registros comunes
//     (CSR, CCR, CDR), reloj y disparos; el maestro es el impar. Los modos
//     duales: simultáneo regular e inyectado, entrelazado con su DELAY,
//     disparo alterno y sus combinaciones, con MDMA y CDR [§15.3.29].
//
// UN BLOQUE ES UNA PAREJA. En 0x000 el maestro, en 0x100 el esclavo y en 0x300
// los registros comunes, como en el silicio [CMSIS: ADC1_BASE, ADC2_BASE,
// ADC1_2_COMMON_BASE]. El SoC instancia dos: ADC12 y ADC34.
//
// LAS ENTRADAS SON FUNCIONES. Cada canal de cada convertidor lee de donde el
// SoC diga: el nodo de un pin, el sensor de temperatura, VREFINT, VBAT/2, la
// referencia de un OPAMP. El bloque no sabe de pines: le dan `fuente(u, ch,
// f)` y la llama en el instante de cerrar el muestreo.
//
// EL RELOJ: CKMODE = 00 toma ADCxy_CK, el que da el RCC desde el PLL (el puerto
// `ker_hz`); 01/10/11, HCLK entre 1, 2 o 4 [§15.3.3]. Sin reloj, el ADC no
// hace nada, y se dice.
//
// LO QUE NO SE MODELA [de modelo]: la latencia de los disparos (2-3 ciclos de
// ADC, se toma 0); la incertidumbre de la resincronización entre dominios; el
// desplazamiento y el ruido del convertidor una vez calibrado (cero); las
// salidas ADCx_AWDy_OUT hacia el ETR de los temporizadores (se calculan, no se
// cablean); el modo MDMA = 11 se empaqueta como dos bytes en la media palabra
// baja de CDR; y las restricciones de §15.3.10 (escribir con el ADC
// convirtiendo) no se comprueban: se aplica lo escrito.
// =============================================================================
#ifndef STM32_PERIPH_ADC_V3_H
#define STM32_PERIPH_ADC_V3_H

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include "../common/periph_base.h"

namespace stm32 {

class AdcParV3 : public BusSlave {
public:
    using Fuente = std::function<double()>;

    // ---- Puertos -------------------------------------------------------------
    sc_core::sc_in<double> ker_hz{"ker_hz"};                // ADCxy_CK, del RCC
    sc_core::sc_vector<sc_core::sc_in<bool>> ext, jext;     // [16] EXT0-15, JEXT0-15
    sc_core::sc_out<bool> irq_m{"irq_m"}, irq_s{"irq_s"};
    sc_core::sc_out<bool> dma_m{"dma_m"}, dma_s{"dma_s"};

    // ---- Registros -----------------------------------------------------------
    enum : uint32_t {
        R_ISR = 0x00, R_IER = 0x04, R_CR = 0x08, R_CFGR = 0x0C, R_SMPR1 = 0x14,
        R_SMPR2 = 0x18, R_TR1 = 0x20, R_TR2 = 0x24, R_TR3 = 0x28, R_SQR1 = 0x30,
        R_SQR4 = 0x3C, R_DR = 0x40, R_JSQR = 0x4C, R_OFR1 = 0x60, R_OFR4 = 0x6C,
        R_JDR1 = 0x80, R_JDR4 = 0x8C, R_AWD2CR = 0xA0, R_AWD3CR = 0xA4,
        R_DIFSEL = 0xB0, R_CALFACT = 0xB4,
        C_CSR = 0x300, C_CCR = 0x308, C_CDR = 0x30C
    };
    enum : uint32_t {
        I_ADRDY = 1u << 0, I_EOSMP = 1u << 1, I_EOC = 1u << 2, I_EOS = 1u << 3,
        I_OVR = 1u << 4, I_JEOC = 1u << 5, I_JEOS = 1u << 6, I_AWD1 = 1u << 7,
        I_AWD2 = 1u << 8, I_AWD3 = 1u << 9, I_JQOVF = 1u << 10, I_TODAS = 0x7FFu
    };
    enum : uint32_t {
        CR_ADEN = 1u << 0, CR_ADDIS = 1u << 1, CR_ADSTART = 1u << 2,
        CR_JADSTART = 1u << 3, CR_ADSTP = 1u << 4, CR_JADSTP = 1u << 5,
        CR_ADCALDIF = 1u << 30, CR_ADCAL = 1u << 31
    };

    // Tiempos [DS9118 tabla 64; RM0316 §15.3.6]
    static constexpr double T_VREG_S = 10e-6;      // T_ADCVREG_STUP
    static constexpr double T_STAB_S = 1e-6;       // t_STAB
    static constexpr unsigned CICLOS_CAL = 112;    // t_CAL
    static constexpr int ERROR_SIN_CAL = 16;       // [de modelo] cuentas de 12 bits

    AdcParV3(sc_core::sc_module_name nm, uint32_t base, const char* nom_m, const char* nom_s)
        : BusSlave(nm, base, 0x400), ext("ext", 16), jext("jext", 16) {
        u_[0].nombre = nom_m; u_[1].nombre = nom_s;
        u_[0].idx = 0; u_[1].idx = 1;
        for (U& u : u_) reinicia(u);
        SC_HAS_PROCESS(AdcParV3);
        SC_THREAD(motor0);
        SC_THREAD(motor1);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(disparo_proc); dont_initialize();
        for (unsigned i = 0; i < 16; ++i) sensitive << ext[i] << jext[i];
    }

    // ---- Lo que cablea el SoC (elaboración) ------------------------------------
    // La entrada `ch` (1..18) del convertidor `u` (0 maestro, 1 esclavo)
    void fuente(unsigned u, unsigned ch, Fuente f) { if (u < 2 && ch >= 1 && ch <= 18) u_[u].in[ch] = std::move(f); }
    // VREF+ (= VDDA en el LQFP100)
    std::function<double()> vref = [] { return 3.3; };
    // El DMA terminó su última transferencia (modo de una vez, DMACFG = 0): el
    // ADC deja de pedir hasta el próximo ADSTART; si sigue convirtiendo (CONT),
    // los datos que nadie lee acaban en OVR [RM0316 §15.3.26]
    void dma_fin(unsigned u) {
        if (u >= 2) return;
        U& x = u_[u];
        const bool por_mdma = dual() != 0 && mdma() != 0;
        // Solo si este ADC pedía por ese canal: el canal se comparte con otros
        if (por_mdma ? u != 0 : !(x.cfgr & 1u)) return;
        const bool dmacfg = por_mdma ? ((ccr_ >> 13) & 1u) : ((x.cfgr >> 1) & 1u);
        if (dmacfg) return;                         // circular: sigue pidiendo
        x.dma_hecho = true;
        actualiza();
    }

    // ---- Observación -----------------------------------------------------------
    uint32_t ccr() const { return ccr_; }
    uint64_t conversiones(unsigned u) const { return u < 2 ? u_[u].n_conv : 0; }
    uint32_t dr(unsigned u) const { return u < 2 ? u_[u].dr : 0; }
    bool listo(unsigned u) const { return u < 2 && u_[u].listo; }
    bool awd_out(unsigned u, unsigned w) const { return u < 2 && w < 3 && u_[u].awd_out[w]; }
    double f_adc() const {
        const unsigned ck = (ccr_ >> 16) & 3u;
        if (ck == 0) return ker_hz.read();
        const double h = clk_hz.read();
        return h / (ck == 1 ? 1.0 : ck == 2 ? 2.0 : 4.0);
    }

protected:
    // =========================================================================
    // Un convertidor
    // =========================================================================
    struct U {
        const char* nombre = "ADC";
        unsigned idx = 0;
        uint32_t isr = 0, ier = 0, cr = 0, cfgr = 0, smpr1 = 0, smpr2 = 0;
        uint32_t tr1 = 0, tr2 = 0, tr3 = 0, sqr[4] = {}, dr = 0, jdr[4] = {};
        uint32_t ofr[4] = {}, awd2cr = 0, awd3cr = 0, difsel = 0, calfact = 0;
        std::deque<uint32_t> jq;                   // la cola de contextos (el activo, delante)
        Fuente in[19];
        // Estado
        sc_core::sc_time t_vreg;                   // cuándo se encendió el regulador
        bool vreg = false, vreg_pronto = false, listo = false;
        bool cal_s = false, cal_d = false;
        bool pide_cal = false, pide_en = false, pide_dis = false;
        bool reg_armado = false, inj_armado = false;
        bool reg_pend = false, inj_pend = false;
        bool aborta_reg = false, aborta_inj = false;
        unsigned rpos = 0, rdisc = 0, jpos = 0;    // dónde va cada secuencia
        bool dma_hecho = false;
        bool awd_out[3] = {false, false, false};
        bool o_irq = false, o_dma = false;
        bool convirtiendo = false;
        uint64_t n_conv = 0;
        sc_core::sc_event ev, leido_ev;
        bool av_vreg = false, av_cal = false, av_reloj = false, av_int = false;
        bool av_ts = false;
        sc_core::sc_time retardo_inter;            // entrelazado: lo que espera el esclavo
        bool relanza_tras_jauto = false;
    };
    U u_[2];
    uint32_t ccr_ = 0;
    bool alterno_ = false;                         // el próximo disparo alterno, al esclavo
    sc_core::sc_event pub_ev_;

    static void reinicia(U& x) {
        x.isr = 0; x.ier = 0; x.cr = 0x20000000u; x.cfgr = 0; x.smpr1 = 0; x.smpr2 = 0;
        x.tr1 = 0x0FFF0000u; x.tr2 = 0x00FF0000u; x.tr3 = 0x00FF0000u;
        for (uint32_t& q : x.sqr) q = 0;
        x.dr = 0;
        for (uint32_t& q : x.jdr) q = 0;
        for (uint32_t& q : x.ofr) q = 0;
        x.awd2cr = 0; x.awd3cr = 0; x.difsel = 0; x.calfact = 0;
        x.jq.clear();
        x.vreg = false; x.vreg_pronto = false; x.listo = false; x.cal_s = false; x.cal_d = false;
        x.pide_cal = x.pide_en = x.pide_dis = false;
        x.reg_armado = x.inj_armado = x.reg_pend = x.inj_pend = false;
        x.aborta_reg = x.aborta_inj = false;
        x.rpos = x.rdisc = x.jpos = 0;
        x.dma_hecho = false;
        x.retardo_inter = sc_core::SC_ZERO_TIME;
        x.relanza_tras_jauto = false;
        for (bool& a : x.awd_out) a = false;
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }

    // ---- Campos -----------------------------------------------------------------
    unsigned dual() const { return ccr_ & 0x1Fu; }
    unsigned mdma() const { return (ccr_ >> 14) & 3u; }
    static unsigned res(const U& x) { return (x.cfgr >> 3) & 3u; }
    static unsigned bits(const U& x) { return 12u - 2u * res(x); }
    static bool alin_izq(const U& x) { return (x.cfgr >> 5) & 1u; }
    static unsigned extsel(const U& x) { return (x.cfgr >> 6) & 0xFu; }
    static unsigned exten(const U& x) { return (x.cfgr >> 10) & 3u; }
    static bool ovrmod(const U& x) { return (x.cfgr >> 12) & 1u; }
    static bool cont(const U& x) { return (x.cfgr >> 13) & 1u; }
    static bool autdly(const U& x) { return (x.cfgr >> 14) & 1u; }
    static bool discen(const U& x) { return (x.cfgr >> 16) & 1u; }
    static unsigned discnum(const U& x) { return ((x.cfgr >> 17) & 7u) + 1u; }
    static bool jdiscen(const U& x) { return (x.cfgr >> 20) & 1u; }
    static bool jqm(const U& x) { return (x.cfgr >> 21) & 1u; }
    static bool awd1sgl(const U& x) { return (x.cfgr >> 22) & 1u; }
    static bool awd1en(const U& x) { return (x.cfgr >> 23) & 1u; }
    static bool jawd1en(const U& x) { return (x.cfgr >> 24) & 1u; }
    static bool jauto(const U& x) { return (x.cfgr >> 25) & 1u; }
    static unsigned awd1ch(const U& x) { return (x.cfgr >> 26) & 0x1Fu; }
    static unsigned lreg(const U& x) { return (x.sqr[0] & 0xFu) + 1u; }
    static unsigned sq(const U& x, unsigned i) {      // i = 0..15
        if (i < 4) return (x.sqr[0] >> (6 + 6 * i)) & 0x1Fu;
        if (i < 9) return (x.sqr[1] >> (6 * (i - 4))) & 0x1Fu;
        if (i < 14) return (x.sqr[2] >> (6 * (i - 9))) & 0x1Fu;
        return (x.sqr[3] >> (6 * (i - 14))) & 0x1Fu;
    }
    static unsigned smp(const U& x, unsigned ch) {
        if (ch >= 1 && ch <= 9) return (x.smpr1 >> (3 * ch)) & 7u;
        if (ch >= 10 && ch <= 18) return (x.smpr2 >> (3 * (ch - 10))) & 7u;
        return 0;
    }
    static double ciclos_smp(unsigned s) {
        static const double t[8] = {1.5, 2.5, 4.5, 7.5, 19.5, 61.5, 181.5, 601.5};
        return t[s & 7u];
    }
    // El contexto inyectado activo (0 si la cola está vacía)
    static uint32_t jctx(const U& x) { return x.jq.empty() ? 0u : x.jq.front(); }
    static unsigned jl(uint32_t j) { return (j & 3u) + 1u; }
    static unsigned jextsel(uint32_t j) { return (j >> 2) & 0xFu; }
    static unsigned jexten(uint32_t j) { return (j >> 6) & 3u; }
    static unsigned jsq(uint32_t j, unsigned i) {
        static const unsigned sh[4] = {8, 14, 20, 26};
        return (j >> sh[i & 3u]) & 0x1Fu;
    }
    bool es_maestro(const U& x) const { return x.idx == 0; }
    U& otro(const U& x) { return u_[x.idx ^ 1u]; }
    // ¿Un modo dual con simultáneo regular / entrelazado / inyectado / alterno?
    bool d_reg_sim() const { const unsigned d = dual(); return d == 1 || d == 2 || d == 6; }
    bool d_inter() const { return dual() == 7; }
    bool d_inj_sim() const { const unsigned d = dual(); return d == 1 || d == 5; }
    bool d_alt() const { const unsigned d = dual(); return d == 2 || d == 9; }

    // =========================================================================
    // El bus
    // =========================================================================
    uint32_t reg_read(uint32_t off) override {
        if (off >= C_CSR) return lee_comun(off);
        if (off >= 0x200) return 0;
        U& x = u_[off >= 0x100 ? 1 : 0];
        const uint32_t o = off & 0xFFu;
        if (o >= R_SQR1 && o <= R_SQR4) return x.sqr[(o - R_SQR1) / 4];
        if (o >= R_OFR1 && o <= R_OFR4) return x.ofr[(o - R_OFR1) / 4];
        if (o >= R_JDR1 && o <= R_JDR4) {
            // Leer un JDRy borra JEOC [§15.3.23]
            x.isr &= ~I_JEOC;
            actualiza();
            x.leido_ev.notify(sc_core::SC_ZERO_TIME);
            return x.jdr[(o - R_JDR1) / 4];
        }
        switch (o) {
            case R_ISR:     return x.isr;
            case R_IER:     return x.ier;
            case R_CR:      return x.cr;
            case R_CFGR:    return x.cfgr;
            case R_SMPR1:   return x.smpr1;
            case R_SMPR2:   return x.smpr2;
            case R_TR1:     return x.tr1;
            case R_TR2:     return x.tr2;
            case R_TR3:     return x.tr3;
            case R_DR:
                // Leer DR borra EOC [§15.3.23]
                x.isr &= ~I_EOC;
                actualiza();
                x.leido_ev.notify(sc_core::SC_ZERO_TIME);
                return x.dr;
            case R_JSQR:    return jctx(x);
            case R_AWD2CR:  return x.awd2cr;
            case R_AWD3CR:  return x.awd3cr;
            case R_DIFSEL:  return x.difsel;
            case R_CALFACT: return x.calfact;
            default:        return 0;
        }
    }
    uint32_t lee_comun(uint32_t off) {
        switch (off) {
            case C_CSR: return (u_[0].isr & I_TODAS) | ((u_[1].isr & I_TODAS) << 16);
            case C_CCR: return ccr_;
            case C_CDR: {
                // Leer CDR borra los dos EOC (el DMA de MDMA lee aquí) [§15.3.29]
                const uint32_t m = u_[0].dr & 0xFFFFu, s = u_[1].dr & 0xFFFFu;
                uint32_t v = (s << 16) | m;
                if (mdma() == 3) v = ((s & 0xFFu) << 8) | (m & 0xFFu);
                u_[0].isr &= ~I_EOC; u_[1].isr &= ~I_EOC;
                actualiza();
                u_[0].leido_ev.notify(sc_core::SC_ZERO_TIME);
                u_[1].leido_ev.notify(sc_core::SC_ZERO_TIME);
                return v;
            }
            default: return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            // El ISR es de «escribir un 1 borra»: los bytes no escritos, a cero
            const uint32_t base = ((off & 0xFFu) == R_ISR && off < 0x200) ? 0u : reg_read_crudo(off);
            v = (base & ~m) | (v & m);
        }
        if (off >= C_CSR) { escribe_comun(off, v); return; }
        if (off >= 0x200) return;
        U& x = u_[off >= 0x100 ? 1 : 0];
        const uint32_t o = off & 0xFFu;
        if (o >= R_SQR1 && o <= R_SQR4) {
            static const uint32_t m[4] = {0x1F7DF7CFu, 0x1F7DF7DFu, 0x1F7DF7DFu, 0x000007DFu};
            x.sqr[(o - R_SQR1) / 4] = v & m[(o - R_SQR1) / 4];
            return;
        }
        if (o >= R_OFR1 && o <= R_OFR4) { x.ofr[(o - R_OFR1) / 4] = v & 0xFC000FFFu; return; }
        switch (o) {
            case R_ISR:
                x.isr &= ~(v & I_TODAS);
                x.leido_ev.notify(sc_core::SC_ZERO_TIME);
                actualiza();
                return;
            case R_IER:    x.ier = v & I_TODAS; actualiza(); return;
            case R_CR:     escribe_cr(x, v); return;
            case R_CFGR:   x.cfgr = v & 0x7FFF7FFBu; return;
            case R_SMPR1:  x.smpr1 = v & 0x3FFFFFF8u; return;
            case R_SMPR2:  x.smpr2 = v & 0x07FFFFFFu; return;
            case R_TR1:    x.tr1 = v & 0x0FFF0FFFu; return;
            case R_TR2:    x.tr2 = v & 0x00FF00FFu; return;
            case R_TR3:    x.tr3 = v & 0x00FF00FFu; return;
            case R_JSQR:   escribe_jsqr(x, v); return;
            case R_AWD2CR: x.awd2cr = v & 0x0007FFFEu; return;
            case R_AWD3CR: x.awd3cr = v & 0x0007FFFEu; return;
            case R_DIFSEL:
                // Solo con el ADC apagado; 16-18 son internos y siempre simples [§15.3.7]
                if (!(x.cr & CR_ADEN)) x.difsel = v & 0x0000FFFEu;
                return;
            case R_CALFACT: x.calfact = v & 0x007F007Fu; x.cal_s = x.cal_d = true; return;
            default: return;
        }
    }
    uint32_t reg_read_crudo(uint32_t off) {
        if (off >= C_CSR) return off == C_CCR ? ccr_ : 0;
        U& x = u_[off >= 0x100 ? 1 : 0];
        const uint32_t o = off & 0xFFu;
        if (o >= R_SQR1 && o <= R_SQR4) return x.sqr[(o - R_SQR1) / 4];
        if (o >= R_OFR1 && o <= R_OFR4) return x.ofr[(o - R_OFR1) / 4];
        switch (o) {
            case R_IER: return x.ier;  case R_CR: return x.cr;  case R_CFGR: return x.cfgr;
            case R_SMPR1: return x.smpr1;  case R_SMPR2: return x.smpr2;
            case R_TR1: return x.tr1;  case R_TR2: return x.tr2;  case R_TR3: return x.tr3;
            case R_JSQR: return jctx(x);  case R_AWD2CR: return x.awd2cr;
            case R_AWD3CR: return x.awd3cr;  case R_DIFSEL: return x.difsel;
            case R_CALFACT: return x.calfact;
            default: return 0;
        }
    }
    void escribe_comun(uint32_t off, uint32_t v) {
        if (off == C_CCR) {
            ccr_ = v & 0x01C3EF1Fu;
            alterno_ = false;
        }
    }

    // CR: los bits son órdenes; el hardware los baja [§15.5.3]
    void escribe_cr(U& x, uint32_t v) {
        // El regulador: 10 -> 00 -> 01 [§15.3.6]
        const uint32_t vr = (v >> 28) & 3u, vr_ant = (x.cr >> 28) & 3u;
        if (vr != vr_ant) {
            x.cr = (x.cr & ~(3u << 28)) | (vr << 28);
            if (vr == 1) {
                if (vr_ant != 0) aviso_unico(x.av_vreg, x, "ADVREGEN pasa a 01 sin pasar por 00: el "
                                             "silicio pide 10 -> 00 -> 01 [RM0316 §15.3.6]");
                x.vreg = true; x.t_vreg = sc_core::sc_time_stamp();
            } else {
                x.vreg = false;
            }
        }
        x.cr = (x.cr & ~CR_ADCALDIF) | (v & CR_ADCALDIF);
        const bool en = x.cr & CR_ADEN;
        if ((v & CR_ADCAL) && !en && !(x.cr & CR_ADCAL)) {
            x.cr |= CR_ADCAL;
            x.pide_cal = true;
        }
        if ((v & CR_ADEN) && !en && !(x.cr & CR_ADCAL)) {
            x.cr |= CR_ADEN;
            x.pide_en = true;
        }
        if ((v & CR_ADDIS) && en) {
            x.cr |= CR_ADDIS;
            x.pide_dis = true;
        }
        if (en && !(x.cr & CR_ADDIS) && x.listo) {
            if (v & CR_ADSTART) arranca_reg(x, true);
            if (v & CR_JADSTART) arranca_inj(x, true);
            if ((v & CR_ADSTP) && (x.cr & CR_ADSTART)) para_reg(x, true);
            if ((v & CR_JADSTP) && (x.cr & CR_JADSTART)) para_inj(x, true);
        }
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    // ADSTART: con disparo por software, a convertir ya; con uno externo, a
    // esperarlo. En dual simultáneo o entrelazado, el del maestro arma también
    // al esclavo [§15.3.29]
    void arranca_reg(U& x, bool propaga) {
        x.cr |= CR_ADSTART;
        x.reg_armado = true;
        x.dma_hecho = false;
        if (!es_maestro(x) && (d_reg_sim() || d_inter())) { x.ev.notify(sc_core::SC_ZERO_TIME); return; }
        if (propaga && es_maestro(x) && (d_reg_sim() || d_inter())) {
            U& s = u_[1];
            s.cr |= CR_ADSTART; s.reg_armado = true; s.dma_hecho = false;
            s.rpos = 0; s.rdisc = 0;
            s.ev.notify(sc_core::SC_ZERO_TIME);
        }
        if (exten(x) == 0) {
            x.reg_pend = true;
            if (es_maestro(x) && (d_reg_sim() || d_inter())) dispara_esclavo_reg();
        }
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    void arranca_inj(U& x, bool propaga) {
        if (jctx(x) == 0 && x.jq.empty()) {
            // Cola vacía con JQM = 1: no hay contexto, el disparo no vale [§15.3.21]
            if (jqm(x)) return;
        }
        x.cr |= CR_JADSTART;
        x.inj_armado = true;
        if (propaga && es_maestro(x) && (d_inj_sim() || d_alt())) {
            U& s = u_[1];
            s.cr |= CR_JADSTART; s.inj_armado = true;
            s.ev.notify(sc_core::SC_ZERO_TIME);
        }
        if (!es_maestro(x) && (d_inj_sim() || d_alt())) { x.ev.notify(sc_core::SC_ZERO_TIME); return; }
        if (jexten(jctx(x)) == 0 && !jauto(x)) {
            x.inj_pend = true;
            if (es_maestro(x) && d_inj_sim()) { u_[1].inj_pend = true; u_[1].ev.notify(sc_core::SC_ZERO_TIME); }
        }
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    void para_reg(U& x, bool propaga) {
        x.cr |= CR_ADSTP;
        x.aborta_reg = true;
        if (jauto(x)) x.aborta_inj = true;           // en JAUTO, ADSTP para los dos
        if (propaga && es_maestro(x) && (d_reg_sim() || d_inter())) para_reg(u_[1], false);
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    void para_inj(U& x, bool propaga) {
        x.cr |= CR_JADSTP;
        x.aborta_inj = true;
        if (propaga && es_maestro(x) && (d_inj_sim() || d_alt())) para_inj(u_[1], false);
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    // La cola de contextos: el activo delante; hasta dos [§15.3.21]
    void escribe_jsqr(U& x, uint32_t v) {
        v &= 0x7DF7DFFFu;
        if (x.jq.size() >= 2) {
            x.isr |= I_JQOVF;                       // la escritura se pierde
            actualiza();
            return;
        }
        x.jq.push_back(v);
    }

    // =========================================================================
    // Los disparos externos: EXTi y JEXTi, compartidos por la pareja [fig. 63]
    // =========================================================================
    bool ext_ant_[16] = {}, jext_ant_[16] = {};
    static bool flanco(unsigned pol, bool ant, bool ahora) {
        if (ant == ahora) return false;
        if (pol == 1) return ahora;
        if (pol == 2) return !ahora;
        return pol == 3;
    }
    void disparo_proc() {
        bool e[16], j[16];
        for (unsigned i = 0; i < 16; ++i) { e[i] = ext[i].read(); j[i] = jext[i].read(); }
        for (unsigned k = 0; k < 2; ++k) {
            U& x = u_[k];
            const bool esclavo_dual = k == 1 && dual() != 0;
            // Regular
            if (!esclavo_dual || !(d_reg_sim() || d_inter())) {
                const unsigned s = extsel(x);
                if (x.reg_armado && exten(x) != 0 && flanco(exten(x), ext_ant_[s], e[s]) &&
                    !x.convirtiendo && !x.reg_pend) {
                    x.reg_pend = true;
                    x.ev.notify(sc_core::SC_ZERO_TIME);
                    if (k == 0 && (d_reg_sim() || d_inter())) dispara_esclavo_reg();
                }
            }
            // Inyectado, con el contexto activo
            if (!esclavo_dual || !(d_inj_sim() || d_alt())) {
                const uint32_t c = jctx(x);
                const unsigned s = jextsel(c);
                if (x.inj_armado && !x.jq.empty() && jexten(c) != 0 &&
                    flanco(jexten(c), jext_ant_[s], j[s]) && !x.inj_pend) {
                    if (k == 0 && d_alt()) {
                        U& dst = alterno_ ? u_[1] : u_[0];
                        alterno_ = !alterno_;
                        dst.inj_pend = true;
                        dst.ev.notify(sc_core::SC_ZERO_TIME);
                    } else {
                        x.inj_pend = true;
                        x.ev.notify(sc_core::SC_ZERO_TIME);
                        if (k == 0 && d_inj_sim()) { u_[1].inj_pend = true; u_[1].ev.notify(sc_core::SC_ZERO_TIME); }
                    }
                }
            }
        }
        for (unsigned i = 0; i < 16; ++i) { ext_ant_[i] = e[i]; jext_ant_[i] = j[i]; }
    }
    // En entrelazado, el esclavo empieza cuando el maestro ha muestreado y
    // DELAY + 1 ciclos más [§15.3.29]
    void dispara_esclavo_reg() {
        U& s = u_[1];
        if (!s.reg_armado) return;
        if (d_inter()) {
            const double f = f_adc();
            if (f <= 0) return;
            const unsigned ch = sq(u_[0], u_[0].rpos);
            const double ciclos = ciclos_smp(smp(u_[0], ch)) + double(((ccr_ >> 8) & 0xFu) + 1u);
            s.retardo_inter = sc_core::sc_time(ciclos / f, sc_core::SC_SEC);
        } else {
            s.retardo_inter = sc_core::SC_ZERO_TIME;
        }
        s.reg_pend = true;
        s.ev.notify(sc_core::SC_ZERO_TIME);
    }

    // =========================================================================
    // El motor de cada convertidor
    // =========================================================================
    void motor0() { motor(u_[0]); }
    void motor1() { motor(u_[1]); }

    sc_core::sc_time ciclos(double n) const {
        const double f = f_adc();
        return f > 0 ? sc_core::sc_time(n / f, sc_core::SC_SEC) : sc_core::sc_time(1, sc_core::SC_US);
    }
    bool hay_reloj(U& x) {
        if (f_adc() > 0) return true;
        aviso_unico(x.av_reloj, x, "sin reloj de ADC: CKMODE = 00 y el RCC no da ADCxy_CK "
                    "(RCC_CFGR2.ADCxyPRES = 0xxxx), o HCLK parado. El ADC no hace nada");
        return false;
    }

    void motor(U& x) {
        for (;;) {
            if (!rst_n.read()) { wait(x.ev | rst_n.value_changed_event()); continue; }
            // Calibrar [§15.3.8]
            if (x.pide_cal) {
                x.pide_cal = false;
                if (!x.vreg || !hay_reloj(x)) {
                    if (!x.vreg)
                        aviso_unico(x.av_vreg, x, "ADCAL con el regulador apagado (ADVREGEN != 01): "
                                    "la calibracion no termina [RM0316 §15.3.8]");
                    continue;
                }
                comprueba_vreg(x);
                wait(ciclos(CICLOS_CAL));
                const bool dif = x.cr & CR_ADCALDIF;
                // [de modelo] los factores: los de un chip cualquiera
                if (dif) { x.calfact = (x.calfact & 0x7Fu) | (0x3Bu << 16); x.cal_d = true; }
                else     { x.calfact = (x.calfact & 0x7F0000u) | 0x40u;      x.cal_s = true; }
                x.cr &= ~CR_ADCAL;
                actualiza();
                continue;
            }
            // Encender [§15.3.9]
            if (x.pide_en) {
                x.pide_en = false;
                if (!x.vreg) {
                    aviso_unico(x.av_vreg, x, "ADEN con el regulador apagado (ADVREGEN != 01): el "
                                "ADC no arranca [RM0316 §15.3.6]");
                    x.cr &= ~CR_ADEN;
                    continue;
                }
                if (!hay_reloj(x)) { x.cr &= ~CR_ADEN; continue; }
                comprueba_vreg(x);
                wait(sc_core::sc_time(T_STAB_S, sc_core::SC_SEC));
                if (!(x.cr & CR_ADEN)) continue;
                x.listo = true;
                x.isr |= I_ADRDY;
                actualiza();
                continue;
            }
            // Apagar: aborta todo y vacía la cola según JQM [§15.3.9, §15.3.21]
            if (x.pide_dis) {
                x.pide_dis = false;
                x.listo = false;
                x.reg_armado = x.inj_armado = x.reg_pend = x.inj_pend = false;
                x.cr &= ~(CR_ADEN | CR_ADDIS | CR_ADSTART | CR_JADSTART | CR_ADSTP | CR_JADSTP);
                vacia_cola(x);
                for (bool& a : x.awd_out) a = false;
                actualiza();
                continue;
            }
            if (x.aborta_inj) {
                x.aborta_inj = false;
                x.inj_pend = false; x.inj_armado = false; x.jpos = 0;
                x.cr &= ~(CR_JADSTART | CR_JADSTP);
                vacia_cola(x);
                if (jauto(x)) { x.aborta_reg = true; }
                actualiza();
                continue;
            }
            if (x.aborta_reg) {
                x.aborta_reg = false;
                x.reg_pend = false; x.rpos = 0; x.rdisc = 0;
                if (x.cr & CR_ADSTP) { x.reg_armado = false; x.cr &= ~(CR_ADSTART | CR_ADSTP); }
                actualiza();
                continue;
            }
            if (!x.listo) { wait(x.ev | rst_n.value_changed_event()); continue; }
            // Inyectados primero: interrumpen a los regulares [§15.3.19]
            if (x.inj_pend) { secuencia_inj(x); continue; }
            if (x.reg_pend) { conversion_reg(x); continue; }
            wait(x.ev | rst_n.value_changed_event());
        }
    }

    void comprueba_vreg(U& x) {
        if (sc_core::sc_time_stamp() - x.t_vreg < sc_core::sc_time(T_VREG_S, sc_core::SC_SEC)) {
            x.vreg_pronto = true;
            aviso_unico(x.av_vreg, x, "ADCAL o ADEN antes de los 10 us del regulador "
                        "(T_ADCVREG_STUP): en el silicio, conversiones erroneas [RM0316 §15.3.6]");
        }
    }
    void vacia_cola(U& x) {
        if (jqm(x)) x.jq.clear();
        else while (x.jq.size() > 1) x.jq.pop_back();
    }

    // Una conversión regular, de la secuencia; con AUTDLY espera a que se lea
    void conversion_reg(U& x) {
        x.convirtiendo = true;
        // El esclavo de un modo dual consume aquí el disparo del maestro: el
        // siguiente puede llegar mientras convierte, y no se pierde
        const bool esc_dual = x.idx == 1 && dual() != 0 && (d_reg_sim() || d_inter());
        if (esc_dual && x.rpos == 0) x.reg_pend = false;
        if (x.retardo_inter > sc_core::SC_ZERO_TIME) {
            const sc_core::sc_time r = x.retardo_inter;
            x.retardo_inter = sc_core::SC_ZERO_TIME;
            wait(r);
        }
        const unsigned ch = sq(x, x.rpos);
        bool ok = true;
        const uint32_t raw = convierte(x, ch, true, ok);
        x.convirtiendo = false;
        if (!ok) return;                             // abortada: se repite al volver
        // El dato
        if (x.isr & I_EOC) {
            x.isr |= I_OVR;
            if (ovrmod(x)) x.dr = formatea(x, ch, raw);
        } else {
            x.dr = formatea(x, ch, raw);
        }
        x.isr |= I_EOC;
        perros(x, ch, raw, false);
        ++x.n_conv;
        // ¿Fin de secuencia, o de subgrupo?
        ++x.rpos; ++x.rdisc;
        bool fin_sec = x.rpos >= lreg(x);
        bool fin_sub = discen(x) && !cont(x) && x.rdisc >= discnum(x);
        if (fin_sec) {
            x.isr |= I_EOS;
            x.rpos = 0;
        }
        if (fin_sec || fin_sub) x.rdisc = 0;
        if (esc_dual && !fin_sec) x.reg_pend = true;      // el resto de su secuencia
        actualiza();
        // AUTDLY: no sigue hasta que se haya leído el dato [§15.3.27]
        if (autdly(x)) {
            while ((x.isr & I_EOC) && x.listo && !x.aborta_reg) wait(x.leido_ev | x.ev);
        }
        // JAUTO: tras la secuencia regular, la inyectada [§15.3.19]
        if (fin_sec && jauto(x) && !x.jq.empty()) {
            x.inj_pend = true;
            x.reg_pend = false;
            if (cont(x) && (x.cr & CR_ADSTART)) x.relanza_tras_jauto = true;
            return;
        }
        if (fin_sec || fin_sub) {
            // En dual, el esclavo no tiene disparo ni CONT propios: lo lleva el maestro
            const bool esclavo = x.idx == 1 && dual() != 0 && (d_reg_sim() || d_inter());
            const bool sw = exten(x) == 0 && !esclavo;
            if (!esclavo && cont(x) && !discen(x) && (x.cr & CR_ADSTART)) {
                x.reg_pend = true;                   // continuo: vuelve a empezar
                if (es_maestro(x) && (d_reg_sim() || d_inter())) dispara_esclavo_reg();
            } else {
                if (!esclavo) x.reg_pend = false;
                if (sw && !cont(x)) {
                    x.cr &= ~CR_ADSTART;
                    x.reg_armado = false;
                    if (es_maestro(x) && (d_reg_sim() || d_inter())) {
                        u_[1].cr &= ~CR_ADSTART; u_[1].reg_armado = false;
                    }
                }
            }
        }
    }

    // La secuencia inyectada entera (o un canal, con JDISCEN) [§15.3.19-21]
    void secuencia_inj(U& x) {
        const uint32_t c = jctx(x);
        x.inj_pend = false;
        if (x.jq.empty()) return;
        const unsigned n = jl(c);
        bool ok = true;
        x.convirtiendo = true;
        do {
            const unsigned ch = jsq(c, x.jpos);
            const uint32_t raw = convierte(x, ch, false, ok);
            if (!ok) break;
            x.jdr[x.jpos] = formatea(x, ch, raw);
            x.isr |= I_JEOC;
            perros(x, ch, raw, true);
            ++x.n_conv;
            ++x.jpos;
            actualiza();
        } while (ok && x.jpos < n && !jdiscen(x));
        x.convirtiendo = false;
        if (!ok) { x.jpos = 0; return; }
        if (x.jpos >= n) {
            x.jpos = 0;
            x.isr |= I_JEOS;
            // La cola se consume al acabar la secuencia
            if (x.jq.size() > 1) x.jq.pop_front();
            else if (jqm(x)) x.jq.clear();
            if (jexten(c) == 0 && !jauto(x)) { x.cr &= ~CR_JADSTART; x.inj_armado = false; }
            actualiza();
            if (autdly(x)) {
                while ((x.isr & I_JEOS) && x.listo && !x.aborta_inj) wait(x.leido_ev | x.ev);
            }
            if (jauto(x)) {
                if (x.relanza_tras_jauto && (x.cr & CR_ADSTART)) { x.reg_pend = true; }
                else {
                    x.cr &= ~CR_ADSTART; x.reg_armado = false;
                }
                x.relanza_tras_jauto = false;
            }
        }
        actualiza();
    }

    // Una conversión: muestreo, aproximación y, si algo la aborta, false
    uint32_t convierte(U& x, unsigned ch, bool regular, bool& ok) {
        ok = true;
        const double f = f_adc();
        if (f <= 0) { ok = false; hay_reloj(x); return 0; }
        const unsigned s = smp(x, ch);
        // Los canales internos piden su tiempo de muestreo [DS9118: TS_temp
        // 2,2 us, TS_vrefint 2,2 us, TS_vbat]
        if ((ch == 16 || ch >= 17) && ciclos_smp(s) / f < 2.2e-6 && x.idx == 0 && ch == 16)
            aviso_unico(x.av_ts, x, "el sensor de temperatura con menos de 2,2 us de muestreo "
                        "(TS_temp, DS9118 tabla 79): en el silicio, una lectura baja");
        const sc_core::sc_time tm = ciclos(ciclos_smp(s));
        const sc_core::sc_time ta = ciclos(12.5 - 2.0 * res(x));
        if (!espera(x, tm, regular)) { ok = false; return 0; }
        const double v = muestra(x, ch);
        if (regular) { x.isr |= I_EOSMP; actualiza(); }
        if (!espera(x, ta, regular)) { ok = false; return 0; }
        return cuantifica(x, ch, v);
    }
    // Esperar; una regular la abortan un disparo inyectado, ADSTP o ADDIS
    bool espera(U& x, sc_core::sc_time t, bool regular) {
        const sc_core::sc_time fin = sc_core::sc_time_stamp() + t;
        while (sc_core::sc_time_stamp() < fin) {
            wait(fin - sc_core::sc_time_stamp(), x.ev);
            if (x.pide_dis || !rst_n.read()) return false;
            if (regular && (x.inj_pend || x.aborta_reg)) return false;
            if (!regular && x.aborta_inj) return false;
        }
        return true;
    }
    // La tensión en la entrada, simple o diferencial (i contra i+1)
    double muestra(U& x, unsigned ch) {
        auto lee = [&](unsigned c) -> double {
            if (c < 1 || c > 18 || !x.in[c]) return 0.0;
            return x.in[c]();
        };
        const double vp = lee(ch);
        if (ch <= 15 && ((x.difsel >> ch) & 1u)) return vp - lee(ch + 1);
        return vp;
    }
    // El SAR: 12 bits a la izquierda (lo que comparan los perros), con el
    // error de desplazamiento de lo no calibrado
    uint32_t cuantifica(U& x, unsigned ch, double v) {
        const double vr = vref();
        const bool dif = ch <= 15 && ((x.difsel >> ch) & 1u);
        double code;
        if (dif) code = (v / vr + 1.0) * 0.5 * 4095.0;    // -VREF..+VREF -> 0..4095 [§15.3.7]
        else     code = v / vr * 4095.0;
        code = std::floor(code + 0.5);
        const bool cal = dif ? x.cal_d : x.cal_s;
        if (!cal) {
            code += ERROR_SIN_CAL;
            aviso_unico(x.av_cal, x, dif ? "conversion diferencial sin calibrar (ADCAL con "
                                           "ADCALDIF = 1): el resultado lleva el desplazamiento "
                                           "del convertidor [RM0316 §15.3.8]"
                                         : "conversion sin calibrar (ADCAL): el resultado lleva "
                                           "el desplazamiento del convertidor [RM0316 §15.3.8]");
        }
        if (code < 0) code = 0;
        if (code > 4095) code = 4095;
        uint32_t c12 = uint32_t(code);
        const unsigned b = bits(x);
        c12 &= ~((1u << (12 - b)) - 1u);                  // lo que la resolución no da
        return c12;
    }
    // Al registro de datos: desplazamiento con signo y alineación [tabla 95]
    uint32_t formatea(U& x, unsigned ch, uint32_t raw12) {
        const unsigned b = bits(x);
        int off = -1;
        for (unsigned i = 0; i < 4; ++i)
            if ((x.ofr[i] >> 31) && ((x.ofr[i] >> 26) & 0x1Fu) == ch) { off = int(x.ofr[i] & 0xFFFu); break; }
        if (off < 0) {
            const uint32_t d = raw12 >> (12 - b);
            if (!alin_izq(x)) return d;
            return b == 6 ? (d << 2) : (d << (16 - b));
        }
        const int sd = (int(raw12) - off) >> int(12 - b);  // con signo, en b bits
        if (!alin_izq(x)) return uint32_t(sd) & 0xFFFFu;    // SEXT en los altos
        // A la izquierda: el signo en el bit 15 (en el 7 con 6 bits)
        const int sh = b == 6 ? 1 : int(15 - b);
        return uint32_t(sd * (1 << sh)) & 0xFFFFu;
    }

    // Los tres perros [§15.3.28]
    void perros(U& x, unsigned ch, uint32_t raw12, bool inyectado) {
        // AWD1
        const bool en1 = inyectado ? jawd1en(x) : awd1en(x);
        if (en1 && (!awd1sgl(x) || awd1ch(x) == ch)) {
            const uint32_t lt = x.tr1 & 0xFFFu, ht = (x.tr1 >> 16) & 0xFFFu;
            const bool fuera = raw12 < lt || raw12 > ht;
            if (fuera) x.isr |= I_AWD1;
            x.awd_out[0] = fuera;
        }
        // AWD2 y AWD3: ocho bits, sobre una lista de canales
        const uint32_t r8 = raw12 >> 4;
        const uint32_t trx[2] = {x.tr2, x.tr3}, crx[2] = {x.awd2cr, x.awd3cr};
        for (unsigned w = 0; w < 2; ++w) {
            if (!((crx[w] >> ch) & 1u)) continue;
            const uint32_t lt = trx[w] & 0xFFu, ht = (trx[w] >> 16) & 0xFFu;
            const bool fuera = r8 < lt || r8 > ht;
            if (fuera) x.isr |= w == 0 ? I_AWD2 : I_AWD3;
            x.awd_out[w + 1] = fuera;
        }
    }

    // =========================================================================
    // Salidas
    // =========================================================================
    void actualiza() {
        for (U& x : u_) {
            x.o_irq = (x.isr & x.ier & I_TODAS) != 0;
            bool d = false;
            if (dual() != 0 && mdma() != 0) {
                // MDMA: una petición cuando están los dos datos, en el canal
                // del maestro [§15.3.29]
                if (x.idx == 0) d = (u_[0].isr & I_EOC) && (u_[1].isr & I_EOC) &&
                                    !(u_[0].isr & I_OVR) && !u_[0].dma_hecho;
            } else {
                d = (x.cfgr & 1u) && (x.isr & I_EOC) && !(x.isr & I_OVR) && !x.dma_hecho;
            }
            x.o_dma = d;
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        irq_m.write(u_[0].o_irq); irq_s.write(u_[1].o_irq);
        dma_m.write(u_[0].o_dma); dma_s.write(u_[1].o_dma);
    }
    void reset_proc() {
        if (rst_n.read()) return;
        for (U& x : u_) reinicia(x);
        ccr_ = 0;
        alterno_ = false;
        actualiza();
    }

    void aviso_unico(bool& ya, U& x, const char* q) {
        if (ya) return;
        ya = true;
        const std::string m = std::string(x.nombre) + ": " + q;
        SC_REPORT_WARNING("adc_v3", m.c_str());
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ADC_V3_H
