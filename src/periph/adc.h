// =============================================================================
// adc.h — ADC1/ADC2/ADC3 y el bloque común [IR, §12.13]
//
// El STM32F407VG lleva TRES convertidores de aproximaciones sucesivas de 12
// bits. A diferencia de los tres I2C —que resultaron ser idénticos— los tres
// ADC SÍ se diferencian, y no solo en su integración:
//
//   * solo ADC1 tiene las ENTRADAS INTERNAS: sensor de temperatura (IN16),
//     VREFINT (IN17) y VBAT/2 (IN18). Los bits TSVREFE y VBATE viven en el
//     registro común, pero lo que habilitan solo llega al ADC1;
//   * solo ADC1 es MAESTRO de los modos dual y triple: en cuanto CCR.MULTI es
//     distinto de cero, ADC2 y ADC3 dejan de obedecer a su propio SWSTART y
//     los arranca ADC1;
//   * los canales externos que llegan de verdad al encapsulado son distintos:
//     IN0-3 e IN10-13 son ADC123, IN4-9 e IN14-15 son ADC12, y en el ADC3 esas
//     mismas entradas van a pines del puerto F que el LQFP100 no tiene.
//
// Por eso el modelo está parametrizado con la receta de familia del proyecto:
//
//   * en TIEMPO DE COMPILACIÓN, con parámetros de plantilla:
//         using AdcBlock = AdcBlockT<CAPS_ADC1, CAPS_ADC23, CAPS_ADC23>;
//
//   * en TIEMPO DE EJECUCIÓN, con parámetros del constructor:
//         AdcBlockBase b{"b", CAPS_ADC1, CAPS_ADC23, caps_adc_basic()};
//
// Los rasgos cubren lo que pide el encargo —número de bits, número de canales,
// entradas internas, secuencia inyectada, perro guardián analógico, DMA— y se
// aplican como MÁSCARA DE ESCRITURA de cada registro, de modo que un bit que
// la instancia no implementa lee cero exactamente igual que un bit reservado
// del silicio.
//
// Lado analógico: la entrada de cada canal externo es el AnalogNet del pin, en
// float, con el pad en modo analógico (buffers y pull desconectados). El
// cuantificador es el de un SAR de verdad:
//
//     code = round( V_in / V_REF+ * (2^N - 1) ),  saturado a [0, 2^N-1]
//
// y el tiempo de cada conversión sale de SMPR (3 a 480 ciclos de muestreo) más
// los ciclos de aproximación de la resolución elegida, contados en ADCCLK =
// PCLK2 / ADCPRE [IR, §12.13.2, §12.13-E].
//
// Implementado en la fase F5:
//   * banco de registros SR/CR1/CR2/SMPR1-2/JOFR1-4/HTR/LTR/SQR1-3/JSQR/
//     JDR1-4/DR por convertidor, y CSR/CCR/CDR comunes;
//   * secuenciador regular (hasta 16 rangos) con SCAN, CONT, DISCEN/DISCNUM y
//     EOCS, y secuenciador inyectado (hasta 4) con JDISCEN, JAUTO y los offsets
//     JOFRx;
//   * resolución de 12, 10, 8 y 6 bits (CR1.RES) y alineación izquierda o
//     derecha (CR2.ALIGN);
//   * disparo por software (SWSTART/JSWSTART) y por TRGO de temporizador, con
//     flanco de subida, de bajada o ambos (EXTEN/JEXTEN);
//   * perro guardián analógico (HTR/LTR, AWDEN/JAWDEN/AWDSGL/AWDCH);
//   * banderas EOC/JEOC/STRT/JSTRT/AWD/OVR con su semántica de borrado, IRQ 18
//     COMPARTIDA por los tres y peticiones de DMA por convertidor;
//   * entradas internas del ADC1 (sensor de temperatura, VREFINT, VBAT/2);
//   * modos dual y triple: regular simultáneo y entrelazado, con el registro
//     común CDR.
// =============================================================================
#ifndef STM32_PERIPH_ADC_H
#define STM32_PERIPH_ADC_H

#include <array>
#include <cmath>
#include <deque>
#include "../common/periph_base.h"
#include "../common/analog_net.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos de un convertidor. Es lo que distingue un ADC1 de un ADC2/3, y lo que
// permitiría describir el mismo bloque tal como aparece en otras familias.
// ---------------------------------------------------------------------------
struct AdcCaps {
    unsigned max_bits       = 12;    // resolución máxima del SAR
    bool     res_selectable = true;  // CR1.RES permite 12/10/8/6 bits
    unsigned n_ext_channels = 16;    // entradas externas IN0..IN(n-1)
    unsigned n_regular      = 16;    // rangos de la secuencia regular (SQR)
    unsigned n_injected     = 4;     // rangos de la secuencia inyectada (JSQR)
    bool     temp_sensor    = false; // IN16: sensor de temperatura (solo ADC1)
    bool     vrefint        = false; // IN17: referencia interna (solo ADC1)
    bool     vbat           = false; // IN18: VBAT/2 (solo ADC1)
    bool     injected       = true;  // grupo inyectado completo
    bool     watchdog       = true;  // perro guardián analógico
    bool     dma            = true;  // CR2.DMA/DDS y la petición
    bool     multi_master   = false; // gobierna CCR.MULTI (solo ADC1)
    double   max_adcclk_hz  = 36.0e6;
    const char* kind        = "ADC";

    // Número total de canales, contando las entradas internas que existan.
    unsigned n_channels() const {
        unsigned n = n_ext_channels;
        if (temp_sensor || vrefint) n = 18;      // IN16 e IN17
        if (vbat)                   n = 19;      // IN18
        return n;
    }
};

// --- Las variantes del F407 ------------------------------------------------
constexpr AdcCaps caps_adc1() {
    AdcCaps c{};
    c.temp_sensor = true; c.vrefint = true; c.vbat = true;
    c.multi_master = true;
    c.kind = "ADC1 (maestro, con entradas internas)";
    return c;
}
constexpr AdcCaps caps_adc23() {
    AdcCaps c{};                                 // sin entradas internas ni
    c.kind = "ADC2/ADC3 (esclavo)";              // gobierno del modo múltiple
    return c;
}
// Variante reducida: el mismo SAR tal como aparece en derivados pequeños, con
// 10 bits fijos, ocho canales y sin grupo inyectado ni perro guardián. No
// existe en el F407; sirve para comprobar que los ejes son independientes.
constexpr AdcCaps caps_adc_basic() {
    AdcCaps c{};
    c.max_bits = 10; c.res_selectable = false;
    c.n_ext_channels = 8; c.n_regular = 8; c.n_injected = 0;
    c.injected = false; c.watchdog = false; c.dma = false;
    c.max_adcclk_hz = 14.0e6;
    c.kind = "ADC basico";
    return c;
}

inline constexpr AdcCaps CAPS_ADC1      = caps_adc1();
inline constexpr AdcCaps CAPS_ADC23     = caps_adc23();
inline constexpr AdcCaps CAPS_ADC_BASIC = caps_adc_basic();

// ---------------------------------------------------------------------------
// Implementación común. Los rasgos de los tres convertidores llegan por el
// constructor: este es el punto de selección en tiempo de ejecución.
// ---------------------------------------------------------------------------
class AdcBlockBase : public BusSlave {
public:
    static constexpr unsigned N_ADC = 3;

    sc_core::sc_out<bool> irq{"irq"};                    // IRQ 18: COMPARTIDA
    sc_core::sc_out<bool> dma_req_adc1{"dma_req_adc1"},
                          dma_req_adc2{"dma_req_adc2"},
                          dma_req_adc3{"dma_req_adc3"};
    sc_core::sc_in<double> vdda{"vdda"};                 // PowerPads
    sc_core::sc_in<double> vref{"vref"};                 // VREF+
    sc_core::sc_in<double> vbat_in{"vbat_in"};           // pin VBAT
    // Disparos externos, INDEXADOS DIRECTAMENTE POR EXTSEL/JEXTSEL: el índice
    // del vector es el valor del campo, así que el netlist del top solo tiene
    // que conectar cada fuente en su hueco de la tabla [IR, §12.13.2].
    sc_core::sc_vector<sc_core::sc_in<bool>> trig_regular;   // [16]
    sc_core::sc_vector<sc_core::sc_in<bool>> trig_injected;  // [16]

    // ---- Offsets ----------------------------------------------------------
    // Cada convertidor ocupa 0x100 (ADC1 en +0x000, ADC2 en +0x100, ADC3 en
    // +0x200) y el bloque común está en +0x300 [IR, §12.13-D, §12.13-E].
    enum : uint32_t { R_SR = 0x00, R_CR1 = 0x04, R_CR2 = 0x08, R_SMPR1 = 0x0C,
                      R_SMPR2 = 0x10, R_JOFR1 = 0x14, R_HTR = 0x24, R_LTR = 0x28,
                      R_SQR1 = 0x2C, R_SQR2 = 0x30, R_SQR3 = 0x34, R_JSQR = 0x38,
                      R_JDR1 = 0x3C, R_DR = 0x4C };
    enum : uint32_t { ADC2_OFF = 0x100, ADC3_OFF = 0x200, COMMON_OFF = 0x300 };
    enum : uint32_t { R_CSR = 0x00, R_CCR = 0x04, R_CDR = 0x08 };

    enum SrBit : uint32_t {
        S_AWD = 1u << 0, S_EOC = 1u << 1, S_JEOC = 1u << 2,
        S_JSTRT = 1u << 3, S_STRT = 1u << 4, S_OVR = 1u << 5
    };

    AdcBlockBase(sc_core::sc_module_name nm,
                 const AdcCaps& c1 = CAPS_ADC1,
                 const AdcCaps& c2 = CAPS_ADC23,
                 const AdcCaps& c3 = CAPS_ADC23)
        : BusSlave(nm, addr::ADC_B, 0x400),
          trig_regular("trig_regular", 16), trig_injected("trig_injected", 16) {
        u_[0].caps = c1; u_[1].caps = c2; u_[2].caps = c3;
        for (unsigned i = 0; i < N_ADC; ++i) { u_[i].idx = i; u_[i].blk = this; }
        for (auto& row : ch_) row.fill(nullptr);
        SC_HAS_PROCESS(AdcBlockBase);
        SC_THREAD(proc0);
        SC_THREAD(proc1);
        SC_THREAD(proc2);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(trig_proc);
        for (unsigned i = 0; i < 16; ++i) sensitive << trig_regular[i] << trig_injected[i];
        dont_initialize();
    }

    // --- Registro de canales externos (elaboración; lo llama el top) --------
    // Un canal SIN conectar no es un canal a cero: es una entrada que no llega
    // al encapsulado. Se deja a nullptr y el modelo lo trata como tal, que es
    // lo que le pasa al ADC3 con IN4-9 e IN14/15 en el LQFP100.
    void bind_channel(unsigned adc, unsigned ch, analog_net_if& net) {
        if (adc < N_ADC && ch < 16) ch_[adc][ch] = &net;
    }
    // Atajo: el mismo pin llega a los tres convertidores (canales ADC123).
    void bind_channel(unsigned ch, analog_net_if& net) {
        for (unsigned a = 0; a < N_ADC; ++a) bind_channel(a, ch, net);
    }
    // Atajo: el pin llega solo a ADC1 y ADC2 (canales ADC12).
    void bind_channel_12(unsigned ch, analog_net_if& net) {
        bind_channel(0, ch, net); bind_channel(1, ch, net);
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    const AdcCaps& caps(unsigned a) const { return u_[a % N_ADC].caps; }
    double   adcclk_hz() const;
    unsigned bits(unsigned a) const { return res_bits(u_[a % N_ADC]); }
    uint64_t conversions(unsigned a) const { return u_[a % N_ADC].n_conv; }
    uint32_t sr_raw(unsigned a) const { return u_[a % N_ADC].sr; }
    // Temperatura del die, para el canal IN16 del ADC1 [IR, §12.13].
    void   set_die_temp(double celsius) { temp_c_ = celsius; }
    double die_temp() const { return temp_c_; }

protected:
    // «Avisar una vez» de que ADCCLK se pasa del máximo. Es POR INSTANCIA y no
    // un `static` local: con dos MCUs en la placa, una bandera compartida hace
    // que el aviso del segundo chip se lo trague el primero, y entonces el que
    // falla es justo el que no avisa. `mutable` porque quien lo mira es
    // `adcclk_hz() const`. [doc/multi_mcu.md, §7.3]
    mutable bool aviso_adcclk_ = false;

    // =======================================================================
    // Estado de un convertidor
    // =======================================================================
    struct Unit {
        AdcCaps caps{};
        AdcBlockBase*  blk  = nullptr;
        unsigned       idx  = 0;
        // ---- Registros ----
        uint32_t sr = 0, cr1 = 0, cr2 = 0, smpr1 = 0, smpr2 = 0;
        uint32_t jofr[4] = {}, htr = 0x0FFFu, ltr = 0;
        uint32_t sqr1 = 0, sqr2 = 0, sqr3 = 0, jsqr = 0;
        uint32_t jdr[4] = {}, dr = 0;
        // ---- Secuenciador ----
        bool     powered = false;    // ADON aceptado y estabilizado
        bool     stab_done = false;
        unsigned rank = 0;           // siguiente rango regular a convertir
        unsigned jrank = 0;
        bool     start_pend = false, jstart_pend = false;
        bool     eoc_pending = false;// hay dato regular sin leer
        bool     drq = false;        // petición de DMA (nivel)
        bool     dma_stopped = false;// DDS = 0 y la secuencia terminó
        bool     trig_prev = false, jtrig_prev = false;
        uint64_t n_conv = 0;
        sc_core::sc_event ev;
    };
    std::array<Unit, N_ADC> u_{};
    std::array<std::array<analog_net_if*, 16>, N_ADC> ch_{};

    // ---- Registros comunes ----
    uint32_t ccr_ = 0;
    std::deque<uint32_t> cdr_;       // datos de los modos dual/triple
    double   temp_c_ = 25.0;

    // ---- Salidas publicadas por un único proceso ----
    bool o_irq_ = false, o_drq_[N_ADC] = {false, false, false};
    sc_core::sc_event pub_ev_;

    // =======================================================================
    // Campos de los registros
    // =======================================================================
    static bool     adon(const Unit& x)   { return (x.cr2 >> 0) & 1u; }
    static bool     cont(const Unit& x)   { return (x.cr2 >> 1) & 1u; }
    static bool     dmaen(const Unit& x)  { return (x.cr2 >> 8) & 1u; }
    static bool     dds(const Unit& x)    { return (x.cr2 >> 9) & 1u; }
    static bool     eocs(const Unit& x)   { return (x.cr2 >> 10) & 1u; }
    static bool     align(const Unit& x)  { return (x.cr2 >> 11) & 1u; }
    static unsigned jextsel(const Unit& x){ return (x.cr2 >> 16) & 0xFu; }
    static unsigned jexten(const Unit& x) { return (x.cr2 >> 20) & 3u; }
    static unsigned extsel(const Unit& x) { return (x.cr2 >> 24) & 0xFu; }
    static unsigned exten(const Unit& x)  { return (x.cr2 >> 28) & 3u; }
    static unsigned awdch(const Unit& x)  { return x.cr1 & 0x1Fu; }
    static bool     eocie(const Unit& x)  { return (x.cr1 >> 5) & 1u; }
    static bool     awdie(const Unit& x)  { return (x.cr1 >> 6) & 1u; }
    static bool     jeocie(const Unit& x) { return (x.cr1 >> 7) & 1u; }
    static bool     scan(const Unit& x)   { return (x.cr1 >> 8) & 1u; }
    static bool     awdsgl(const Unit& x) { return (x.cr1 >> 9) & 1u; }
    static bool     jauto(const Unit& x)  { return (x.cr1 >> 10) & 1u; }
    static bool     discen(const Unit& x) { return (x.cr1 >> 11) & 1u; }
    static bool     jdiscen(const Unit& x){ return (x.cr1 >> 12) & 1u; }
    static unsigned discnum(const Unit& x){ return ((x.cr1 >> 13) & 7u) + 1u; }
    static bool     jawden(const Unit& x) { return (x.cr1 >> 22) & 1u; }
    static bool     awden(const Unit& x)  { return (x.cr1 >> 23) & 1u; }
    static bool     ovrie(const Unit& x)  { return (x.cr1 >> 26) & 1u; }

    static unsigned res_bits(const Unit& x) {
        if (!x.caps.res_selectable) return x.caps.max_bits;
        static const unsigned tab[4] = {12, 10, 8, 6};
        return tab[(x.cr1 >> 24) & 3u];
    }
    // Longitud de la secuencia regular: SQR1.L[3:0] + 1 [IR, §12.13-D].
    unsigned seq_len(const Unit& x) const {
        const unsigned l = ((x.sqr1 >> 20) & 0xFu) + 1u;
        return l > x.caps.n_regular ? x.caps.n_regular : l;
    }
    // Canal del rango r (0..15) de la secuencia regular.
    static unsigned sq(const Unit& x, unsigned r) {
        if (r < 6)  return (x.sqr3 >> (5 * r)) & 0x1Fu;
        if (r < 12) return (x.sqr2 >> (5 * (r - 6))) & 0x1Fu;
        return (x.sqr1 >> (5 * (r - 12))) & 0x1Fu;
    }
    unsigned jseq_len(const Unit& x) const {
        if (!x.caps.injected) return 0;
        const unsigned l = ((x.jsqr >> 20) & 3u) + 1u;
        return l > x.caps.n_injected ? x.caps.n_injected : l;
    }
    // Canal del rango r de la secuencia INYECTADA. Ojo al detalle del manual:
    // si JL es menor que cuatro, la secuencia NO empieza en JSQ1 sino en
    // JSQ(4-JL), es decir, va alineada por la derecha. Es la trampa clásica del
    // grupo inyectado y aquí está modelada [IR, §12.13-D].
    unsigned jsq(const Unit& x, unsigned r) const {
        const unsigned jl = jseq_len(x);                 // 1..4 conversiones
        const unsigned field = 5u - jl + r;              // JSQ(5-jl) .. JSQ4
        return (x.jsqr >> (5 * (field - 1u))) & 0x1Fu;
    }
    // Tiempo de muestreo del canal, en ciclos de ADCCLK [IR, §12.13.1].
    static unsigned smp_cycles(const Unit& x, unsigned ch) {
        static const unsigned tab[8] = {3, 15, 28, 56, 84, 112, 144, 480};
        const unsigned f = (ch < 10) ? ((x.smpr2 >> (3 * ch)) & 7u)
                                     : ((x.smpr1 >> (3 * (ch - 10))) & 7u);
        return tab[f];
    }
    // Ciclos de aproximación sucesiva según la resolución.
    static unsigned sar_cycles(const Unit& x) {
        switch (res_bits(x)) {
            case 12: return 12; case 10: return 11;
            case 8:  return 9;  default: return 7;
        }
    }

    // ---- Registro común ----
    unsigned adcpre() const   { return (ccr_ >> 16) & 3u; }
    unsigned multi() const    { return ccr_ & 0x1Fu; }
    unsigned mdelay() const   { return ((ccr_ >> 8) & 0xFu) + 5u; }  // 5..20 ciclos
    bool     tsvrefe() const  { return (ccr_ >> 23) & 1u; }
    bool     vbate() const    { return (ccr_ >> 22) & 1u; }
    bool     multi_on() const { return multi() != 0 && u_[0].caps.multi_master; }
    unsigned multi_n() const {                               // participantes
        if (!multi_on()) return 1;
        return ((multi() & 0x10u) != 0) ? 3u : 2u;           // triple : dual
    }
    bool multi_interleaved() const { return (multi() & 0x07u) == 0x07u; }

    // =======================================================================
    // Máscaras de escritura: aquí es donde ACTÚAN los rasgos. Un bit que la
    // instancia no implementa no se guarda, y por tanto lee cero igual que un
    // bit reservado del silicio.
    // =======================================================================
    static uint32_t cr1_mask(const Unit& x) {
        uint32_t m = (1u << 5) | (1u << 8) | (1u << 11) | (7u << 13);  // EOCIE,
                                                     // SCAN, DISCEN, DISCNUM
        if (x.caps.watchdog) m |= 0x1Fu | (1u << 6) | (1u << 9) | (1u << 23);
        if (x.caps.injected) {
            m |= (1u << 7) | (1u << 10) | (1u << 12);
            if (x.caps.watchdog) m |= 1u << 22;
        }
        if (x.caps.res_selectable) m |= 3u << 24;
        if (x.caps.dma) m |= 1u << 26;              // OVRIE solo tiene sentido
        return m;                                    // si hay OVR que vigilar
    }
    static uint32_t cr2_mask(const Unit& x) {
        uint32_t m = (1u << 0) | (1u << 1) | (1u << 10) | (1u << 11) |
                     (0xFu << 24) | (3u << 28) | (1u << 30);
        if (x.caps.dma) m |= (1u << 8) | (1u << 9);
        if (x.caps.injected) m |= (0xFu << 16) | (3u << 20) | (1u << 22);
        return m;
    }
    static uint32_t smpr1_mask(const Unit& x) {
        const unsigned n = x.caps.n_channels();
        if (n <= 10) return 0;
        unsigned k = n - 10;                          // canales 10..18
        return (k >= 9) ? 0x07FFFFFFu : ((1u << (3 * k)) - 1u);
    }
    static uint32_t smpr2_mask(const Unit& x) {
        const unsigned n = x.caps.n_ext_channels < 10 ? x.caps.n_ext_channels : 10;
        return (1u << (3 * n)) - 1u;
    }
    static uint32_t sqr1_mask(const Unit& x) {
        uint32_t m = uint32_t(x.caps.n_regular - 1u) << 20;   // campo L
        m |= 0x000FFFFFu;                                      // SQ13..SQ16
        if (x.caps.n_regular <= 12) m &= ~0x000FFFFFu;
        return m;
    }
    static uint32_t jsqr_mask(const Unit& x) {
        if (!x.caps.injected) return 0;
        return 0x000FFFFFu | (uint32_t(x.caps.n_injected - 1u) << 20);
    }
    static uint32_t sr_w0_mask(const Unit& x) {
        uint32_t m = S_EOC | S_JEOC | S_JSTRT | S_STRT;
        if (x.caps.watchdog) m |= S_AWD;
        if (x.caps.dma)      m |= S_OVR;
        return m;
    }

    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        if (off >= COMMON_OFF) return read_common(off - COMMON_OFF);
        Unit& x = u_[off >> 8];
        return read_unit(x, off & 0xFFu);
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                  // acceso parcial
            uint32_t cur = quiet_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        if (off >= COMMON_OFF) { write_common(off - COMMON_OFF, v); return; }
        write_unit(u_[off >> 8], off & 0xFFu, v);
    }
    uint32_t quiet_read(uint32_t off) const {
        if (off >= COMMON_OFF) {
            switch (off - COMMON_OFF) {
                case R_CSR: return csr();
                case R_CCR: return ccr_;
                default:    return cdr_.empty() ? 0u : cdr_.front();
            }
        }
        const Unit& x = u_[off >> 8];
        switch (off & 0xFFu) {
            case R_SR: return x.sr;   case R_CR1: return x.cr1;
            case R_CR2: return x.cr2; case R_SMPR1: return x.smpr1;
            case R_SMPR2: return x.smpr2; case R_HTR: return x.htr;
            case R_LTR: return x.ltr; case R_SQR1: return x.sqr1;
            case R_SQR2: return x.sqr2; case R_SQR3: return x.sqr3;
            case R_JSQR: return x.jsqr; case R_DR: return x.dr;
            default: return 0;
        }
    }

    uint32_t read_unit(Unit& x, uint32_t off) {
        if (off >= R_JOFR1 && off < R_JOFR1 + 16) {
            return x.caps.injected ? x.jofr[(off - R_JOFR1) / 4] : 0u;
        }
        if (off >= R_JDR1 && off < R_JDR1 + 16) {
            return x.caps.injected ? x.jdr[(off - R_JDR1) / 4] : 0u;
        }
        switch (off) {
            case R_SR:    return x.sr;
            case R_CR1:   return x.cr1;
            case R_CR2:   return x.cr2;
            case R_SMPR1: return x.smpr1;
            case R_SMPR2: return x.smpr2;
            case R_HTR:   return x.caps.watchdog ? x.htr : 0u;
            case R_LTR:   return x.caps.watchdog ? x.ltr : 0u;
            case R_SQR1:  return x.sqr1;
            case R_SQR2:  return x.sqr2;
            case R_SQR3:  return x.sqr3;
            case R_JSQR:  return x.jsqr;
            case R_DR:    return read_dr(x);
            default:      return 0;
        }
    }
    // Leer DR borra EOC y retira la petición de DMA: es lo que impide que el
    // dato siguiente marque OVR [IR, §12.13-B].
    uint32_t read_dr(Unit& x) {
        const uint32_t v = x.dr;
        x.sr &= ~S_EOC;
        x.eoc_pending = false;
        if (x.drq) { x.drq = false; publish(); }
        update_irq();
        return v;
    }

    void write_unit(Unit& x, uint32_t off, uint32_t v) {
        if (off >= R_JOFR1 && off < R_JOFR1 + 16) {
            if (x.caps.injected) x.jofr[(off - R_JOFR1) / 4] = v & 0x0FFFu;
            return;
        }
        if (off >= R_JDR1 && off < R_JDR1 + 16) return;    // solo lectura
        switch (off) {
            case R_SR:
                // Banderas rc_w0: escribir cero borra [IR, §12.13-B]
                x.sr &= (v | ~sr_w0_mask(x));
                if (!(x.sr & S_EOC)) x.eoc_pending = false;
                update_irq();
                return;
            case R_CR1: x.cr1 = v & cr1_mask(x); update_irq(); break;
            case R_CR2: {
                const bool was_on = adon(x);
                const uint32_t sw  = v & (1u << 30);
                const uint32_t jsw = v & (1u << 22);
                x.cr2 = v & cr2_mask(x);
                x.cr2 &= ~((1u << 30) | (1u << 22));   // SWSTART/JSWSTART no se
                                                       // guardan: son órdenes
                if (!was_on && adon(x)) {              // despertar del power-down
                    x.stab_done = false;
                    x.powered = true;
                } else if (was_on && !adon(x)) {
                    stop_unit(x);
                }
                if (sw && adon(x))  request_regular(x);
                if (jsw && adon(x) && x.caps.injected) request_injected(x);
                break;
            }
            case R_SMPR1: x.smpr1 = v & smpr1_mask(x); break;
            case R_SMPR2: x.smpr2 = v & smpr2_mask(x); break;
            case R_HTR:   if (x.caps.watchdog) x.htr = v & 0x0FFFu; break;
            case R_LTR:   if (x.caps.watchdog) x.ltr = v & 0x0FFFu; break;
            case R_SQR1:  x.sqr1 = v & sqr1_mask(x); break;
            case R_SQR2:  x.sqr2 = v & (x.caps.n_regular > 6 ? 0x3FFFFFFFu : 0u); break;
            case R_SQR3:  x.sqr3 = v & 0x3FFFFFFFu; break;
            case R_JSQR:  x.jsqr = v & jsqr_mask(x); break;
            default: return;
        }
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }

    uint32_t csr() const {
        // Copia de las banderas de los tres, para que el manejador de la IRQ
        // 18 —que es UNA SOLA para los tres convertidores— sepa quién ha sido.
        uint32_t v = 0;
        for (unsigned i = 0; i < N_ADC; ++i) v |= (u_[i].sr & 0x3Fu) << (8 * i);
        return v;
    }
    uint32_t read_common(uint32_t off) {
        switch (off) {
            case R_CSR: return csr();
            case R_CCR: return ccr_;
            case R_CDR: {
                if (cdr_.empty()) return 0u;
                const uint32_t v = cdr_.front();
                cdr_.pop_front();
                if (cdr_.empty()) { for (auto& x : u_) if (x.drq) { x.drq = false; } publish(); }
                return v;
            }
            default: return 0;
        }
    }
    void write_common(uint32_t off, uint32_t v) {
        if (off != R_CCR) return;                       // CSR y CDR son de solo lectura
        uint32_t m = (3u << 16) | (0xFu << 8) | (3u << 14) | (1u << 13);
        if (u_[0].caps.multi_master) m |= 0x1Fu;       // MULTI solo si hay maestro
        if (u_[0].caps.temp_sensor || u_[0].caps.vrefint) m |= 1u << 23;  // TSVREFE
        if (u_[0].caps.vbat) m |= 1u << 22;                                // VBATE
        ccr_ = v & m;
        for (auto& x : u_) x.ev.notify(sc_core::SC_ZERO_TIME);
    }

    // =======================================================================
    // Lado analógico
    // =======================================================================
    double vref_volts() const {
        const double v = vref.read();
        return (v > 0.5) ? v : ((vdda.read() > 0.5) ? vdda.read() : 3.3);
    }
    // Tensión presente en un canal. Un canal externo sin conectar (el caso del
    // ADC3 con IN4-9 en el LQFP100) y un nodo flotante son la misma cosa: no
    // hay nada que medir. Se devuelve 0 V y se avisa una vez.
    double channel_volts(const Unit& x, unsigned ch) {
        if (ch < 16) {
            analog_net_if* n = ch_[x.idx][ch];
            if (!n) return 0.0;
            bool fl = n->floating();
            return fl ? 0.0 : double(n->voltage());
        }
        // Entradas internas: existen solo si la instancia las tiene Y el bit
        // del registro común las ha conectado [IR, §12.13-E].
        if (ch == 16 && x.caps.temp_sensor && tsvrefe()) {
            // V_sense = V_25 + (T - 25) * pendiente media (2,5 mV/oC)
            return 0.76 + (temp_c_ - 25.0) * 0.0025;
        }
        if (ch == 17 && x.caps.vrefint && tsvrefe()) return 1.21;
        if (ch == 18 && x.caps.vbat && vbate()) {
            const double vb = vbat_in.read();
            return (vb > 0.0 ? vb : 3.0) * 0.5;         // el divisor por dos
        }
        return 0.0;
    }
    // El cuantificador del SAR.
    uint32_t quantise(const Unit& x, double v) const {
        const unsigned n = res_bits(x);
        const double full = double((1u << n) - 1u);
        const double vr = vref_volts();
        if (vr <= 0.0) return 0;
        double code = std::floor(v / vr * full + 0.5);
        if (code < 0.0) code = 0.0;
        if (code > full) code = full;
        return uint32_t(code);
    }
    // Alineación del dato regular [IR, §12.13.2, ALIGN].
    uint32_t align_regular(const Unit& x, uint32_t code) const {
        return align(x) ? (code << (16u - res_bits(x))) : code;
    }
    // El dato inyectado lleva restado su offset y es un valor CON SIGNO de 16
    // bits [IR, §12.13-D, JOFRx].
    uint32_t align_injected(const Unit& x, uint32_t code, unsigned r) const {
        const int32_t d = int32_t(code) - int32_t(x.jofr[r] & 0x0FFFu);
        const int32_t s = align(x) ? (d << (16 - int(res_bits(x)))) : d;
        return uint32_t(s) & 0xFFFFu;
    }

    // =======================================================================
    // Secuenciadores
    // =======================================================================
    sc_core::sc_time cycles(unsigned n) const {
        const double f = adcclk_hz();
        if (f <= 0.0) return sc_core::sc_time(1, sc_core::SC_US);
        return sc_core::sc_time(double(n) / f, sc_core::SC_SEC);
    }

    void stop_unit(Unit& x) {
        x.powered = false; x.stab_done = false;
        x.rank = x.jrank = 0;
        x.start_pend = x.jstart_pend = false;
        x.eoc_pending = false; x.dma_stopped = false;
        if (x.drq) { x.drq = false; publish(); }
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    void request_regular(Unit& x) {
        // En modo múltiple los esclavos NO obedecen a su propio SWSTART: los
        // arranca el maestro [IR, §12.13-E, MULTI].
        if (multi_on() && x.idx != 0) return;
        x.start_pend = true;
        x.dma_stopped = false;
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }
    void request_injected(Unit& x) {
        if (multi_on() && x.idx != 0) return;
        x.jstart_pend = true;
        x.ev.notify(sc_core::SC_ZERO_TIME);
    }

    // Vigilancia de los disparos externos. El vector está indexado por el campo
    // EXTSEL/JEXTSEL, así que basta con mirar el hueco que el firmware ha
    // seleccionado y detectar el flanco que pide EXTEN/JEXTEN.
    void trig_proc() {
        for (auto& x : u_) {
            if (!adon(x)) { x.trig_prev = x.jtrig_prev = false; continue; }
            const bool r = trig_regular[extsel(x)].read();
            if (edge(exten(x), x.trig_prev, r)) request_regular(x);
            x.trig_prev = r;
            if (x.caps.injected) {
                const bool j = trig_injected[jextsel(x)].read();
                if (edge(jexten(x), x.jtrig_prev, j)) request_injected(x);
                x.jtrig_prev = j;
            }
        }
    }
    static bool edge(unsigned en, bool prev, bool now) {
        switch (en) {
            case 1: return !prev && now;                 // subida
            case 2: return prev && !now;                 // bajada
            case 3: return prev != now;                  // ambos
            default: return false;                       // disparo deshabilitado
        }
    }

    void proc0() { unit_proc(u_[0]); }
    void proc1() { unit_proc(u_[1]); }
    void proc2() { unit_proc(u_[2]); }

    void unit_proc(Unit& x) {
        for (;;) {
            if (!adon(x) || !clock_enabled() || adcclk_hz() <= 0.0) {
                wait(x.ev | rst_n.value_changed_event() | clk_hz.value_changed_event());
                continue;
            }
            if (!x.stab_done) {
                // Tiempo de estabilización tras salir del power-down: el manual
                // obliga a esperarlo antes de la primera conversión.
                wait(sc_core::sc_time(3, sc_core::SC_US));
                x.stab_done = true;
                continue;
            }
            if (x.jstart_pend) { x.jstart_pend = false; run_injected(x); continue; }
            if (x.start_pend)  { x.start_pend = false;  run_regular(x);  continue; }
            wait(x.ev | rst_n.value_changed_event());
        }
    }

    // --- Grupo regular ------------------------------------------------------
    void run_regular(Unit& x) {
        const unsigned len = seq_len(x);
        do {
            // Con DISCEN se convierte solo un trozo de la secuencia por disparo;
            // sin SCAN, solo el primer rango [IR, §12.13-C].
            unsigned n = len;
            if (!scan(x)) n = 1;
            else if (discen(x)) n = discnum(x);
            if (!scan(x)) x.rank = 0;

            x.sr |= S_STRT;
            update_irq();
            for (unsigned k = 0; k < n && x.rank < len; ++k, ++x.rank) {
                if (!adon(x)) return;
                if (multi_on() && x.idx == 0) { if (!convert_multi(x)) return; }
                else                          { if (!convert_regular(x)) return; }
                const bool last = (x.rank + 1u >= len);
                if (eocs(x) || last) { x.sr |= S_EOC; x.eoc_pending = true; }
                if (last && !dds(x)) x.dma_stopped = true;
                update_irq();
                if (last) {
                    x.rank = 0;
                    // JAUTO: el grupo inyectado va detrás del regular, sin que
                    // el firmware tenga que pedirlo [IR, §12.13-C].
                    if (jauto(x) && x.caps.injected) run_injected(x);
                    break;
                }
            }
            if (discen(x) && scan(x) && x.rank < len) return;  // espera otro disparo
        } while (cont(x) && adon(x));
    }

    // Una conversión regular en un convertidor. Devuelve false si se apaga.
    bool convert_regular(Unit& x) {
        const unsigned ch = sq(x, x.rank);
        const uint32_t code = do_one(x, ch);
        if (!adon(x)) return false;
        // OVR: el dato anterior seguía sin leerse cuando llega el nuevo. La
        // condición del manual es EXACTAMENTE que EOC siguiera puesto.
        if ((x.sr & S_EOC) && x.caps.dma) { x.sr |= S_OVR; }
        x.dr = align_regular(x, code);
        check_watchdog(x, ch, code, /*injected=*/false);
        if (dmaen(x) && !x.dma_stopped) { x.drq = true; publish(); }
        return true;
    }

    // --- Grupo inyectado ----------------------------------------------------
    void run_injected(Unit& x) {
        const unsigned len = jseq_len(x);
        if (len == 0) return;
        x.sr |= S_JSTRT;
        update_irq();
        unsigned n = jdiscen(x) ? 1u : len;
        for (unsigned k = 0; k < n && x.jrank < len; ++k, ++x.jrank) {
            if (!adon(x)) return;
            const unsigned ch = jsq(x, x.jrank);
            const uint32_t code = do_one(x, ch);
            if (!adon(x)) return;
            x.jdr[x.jrank] = align_injected(x, code, x.jrank);
            check_watchdog(x, ch, code, /*injected=*/true);
        }
        if (x.jrank >= len) {
            x.jrank = 0;
            x.sr |= S_JEOC;          // JEOC se levanta al final del GRUPO
            update_irq();
        }
    }

    // Muestreo + aproximación sucesiva de un canal, con su tiempo real.
    uint32_t do_one(Unit& x, unsigned ch) {
        wait(cycles(smp_cycles(x, ch)));                 // ventana de muestreo
        const double v = channel_volts(x, ch);           // se captura al final
        wait(cycles(sar_cycles(x)));                     // aproximaciones
        ++x.n_conv;
        return quantise(x, v);
    }

    void check_watchdog(Unit& x, unsigned ch, uint32_t code, bool injected) {
        if (!x.caps.watchdog) return;
        const bool en = injected ? jawden(x) : awden(x);
        if (!en) return;
        if (awdsgl(x) && awdch(x) != ch) return;         // vigila un solo canal
        if (code > x.htr || code < x.ltr) {
            x.sr |= S_AWD;
            update_irq();
        }
    }

    // --- Modos dual y triple ------------------------------------------------
    // Los arranca ADC1 y convierten a la vez (simultáneo) o escalonados
    // (entrelazado); el resultado se empaqueta en el registro común CDR.
    bool convert_multi(Unit& m) {
        const unsigned n = multi_n();
        const unsigned bits = res_bits(m);
        uint32_t code[N_ADC] = {0, 0, 0};
        if (multi_interleaved()) {
            // Entrelazado: cada convertidor arranca DELAY ciclos después del
            // anterior, que es de donde sale el aumento de la cadencia.
            for (unsigned i = 0; i < n; ++i) {
                Unit& s = u_[i];
                const unsigned ch = sq(s, m.rank);
                code[i] = do_one(s, ch);
                if (!adon(m)) return false;
                if (i + 1 < n) wait(cycles(mdelay()));
            }
        } else {
            // Simultáneo: todos muestrean el mismo instante. Se cobra el tiempo
            // del más lento, que es lo que ocurre en el silicio.
            unsigned worst = 0;
            for (unsigned i = 0; i < n; ++i) {
                Unit& s = u_[i];
                const unsigned ch = sq(s, m.rank);
                const unsigned t = smp_cycles(s, ch) + sar_cycles(s);
                if (t > worst) worst = t;
            }
            double v[N_ADC] = {0.0, 0.0, 0.0};
            for (unsigned i = 0; i < n; ++i) v[i] = channel_volts(u_[i], sq(u_[i], m.rank));
            wait(cycles(worst));
            if (!adon(m)) return false;
            for (unsigned i = 0; i < n; ++i) { code[i] = quantise(u_[i], v[i]); ++u_[i].n_conv; }
        }
        // Cada esclavo actualiza su propio DR y sus banderas...
        for (unsigned i = 1; i < n; ++i) {
            Unit& s = u_[i];
            s.dr = align_regular(s, code[i]);
            s.sr |= S_EOC | S_STRT;
            check_watchdog(s, sq(s, m.rank), code[i], false);
        }
        m.dr = align_regular(m, code[0]);
        check_watchdog(m, sq(m, m.rank), code[0], false);
        // ...y además el dato va al registro COMÚN. En modo dual caben los dos
        // en una palabra; en triple hacen falta lecturas sucesivas de CDR.
        if (n == 2 && bits <= 16) {
            cdr_.push_back((code[1] << 16) | (code[0] & 0xFFFFu));
        } else {
            for (unsigned i = 0; i < n; ++i) cdr_.push_back(code[i] & 0xFFFFu);
        }
        while (cdr_.size() > 8) cdr_.pop_front();
        if (dmaen(m) && !m.dma_stopped) { m.drq = true; publish(); }
        return true;
    }

    // =======================================================================
    // Reloj, interrupción y publicación
    // =======================================================================
    void update_irq() {
        bool irq = false;
        for (const Unit& x : u_) {
            if ((x.sr & S_EOC)  && eocie(x))  irq = true;
            if ((x.sr & S_JEOC) && jeocie(x)) irq = true;
            if ((x.sr & S_AWD)  && awdie(x))  irq = true;
            if ((x.sr & S_OVR)  && ovrie(x))  irq = true;
        }
        if (irq != o_irq_) { o_irq_ = irq; publish(); }
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq.write(o_irq_);
        dma_req_adc1.write(u_[0].drq);
        dma_req_adc2.write(u_[1].drq);
        dma_req_adc3.write(u_[2].drq);
    }
    // Valores de reset del manual: todo a cero salvo HTR, que arranca a plena
    // escala para que el perro guardián no salte solo [IR, §12.13-D].
    static void reset_unit(Unit& x) {
        x.sr = x.cr1 = x.cr2 = x.smpr1 = x.smpr2 = 0;
        for (unsigned i = 0; i < 4; ++i) { x.jofr[i] = 0; x.jdr[i] = 0; }
        x.htr = 0x0FFFu; x.ltr = 0;
        x.sqr1 = x.sqr2 = x.sqr3 = x.jsqr = 0;
        x.dr = 0;
        x.powered = x.stab_done = false;
        x.rank = x.jrank = 0;
        x.start_pend = x.jstart_pend = false;
        x.eoc_pending = false; x.drq = false; x.dma_stopped = false;
        x.trig_prev = x.jtrig_prev = false;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        for (auto& x : u_) reset_unit(x);
        ccr_ = 0; cdr_.clear();
        o_irq_ = false;
        publish();
        for (auto& x : u_) x.ev.notify(sc_core::SC_ZERO_TIME);
    }

    unsigned access_cycles(bool) const override { return 2; }
};

// ADCCLK = PCLK2 / ADCPRE, con ADCPRE = 2, 4, 6 u 8 y un máximo de 36 MHz
// [IR, §12.13-E]. Si el firmware se pasa, el modelo avisa pero convierte: es
// lo que hace el silicio, con precisión degradada.
inline double AdcBlockBase::adcclk_hz() const {
    const double f = clk_hz.read();
    if (f <= 0.0) return 0.0;
    const double div = 2.0 * double(adcpre() + 1u);
    const double a = f / div;
    if (a > u_[0].caps.max_adcclk_hz && !aviso_adcclk_) {
        aviso_adcclk_ = true;
        SC_REPORT_WARNING("adc", "ADCCLK por encima del maximo de 36 MHz [IR, 12.13]");
    }
    return a;
}

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN. Cada combinación de rasgos es un tipo
// distinto, así que confundir un ADC1 con un ADC2 es un error de compilación.
// ---------------------------------------------------------------------------
template <const AdcCaps& C1, const AdcCaps& C2, const AdcCaps& C3>
class AdcBlockT : public AdcBlockBase {
public:
    explicit AdcBlockT(sc_core::sc_module_name nm) : AdcBlockBase(nm, C1, C2, C3) {
        static_assert(C1.max_bits <= 16, "el dato no cabe en el registro DR");
        static_assert(C1.n_regular >= 1 && C1.n_regular <= 16, "SQR admite 1..16 rangos");
        static_assert(C1.n_injected <= 4, "JSQR admite como mucho 4 rangos");
        static_assert(!C2.multi_master && !C3.multi_master,
                      "en el modo multiple el maestro es el ADC1");
    }
};

// El STM32F407VG: ADC1 con entradas internas y gobierno del modo múltiple,
// ADC2 y ADC3 como esclavos [IR, §12.13].
using AdcBlock = AdcBlockT<CAPS_ADC1, CAPS_ADC23, CAPS_ADC23>;

} // namespace stm32
#endif // STM32_PERIPH_ADC_H
