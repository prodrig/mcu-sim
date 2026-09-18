// =============================================================================
// i2c.h — I2C1/I2C2/I2C3 con SMBus/PMBus [IR, §12.6]
//
// El STM32F407VG lleva TRES instancias del mismo bloque de diseño y —a
// diferencia de lo que ocurre con los temporizadores, los puertos serie o el
// SPI— las tres son FUNCIONALMENTE IDÉNTICAS: mismo banco de registros, mismas
// velocidades (Sm 100 kHz y Fm 400 kHz), direccionamiento de 7 y 10 bits,
// dirección dual, llamada general, SMBus/PMBus con PEC, filtro analógico y
// digital, y DMA. Las tres cuelgan además del mismo bus (APB1) y del mismo
// reloj (PCLK1), así que ni siquiera se diferencian en la frecuencia máxima,
// como sí pasaba entre SPI1 y SPI2/3. Lo único que las distingue es su
// INTEGRACIÓN: dirección base, vectores de interrupción, celdas de DMA y qué
// pines del encapsulado pueden usar. Todo eso vive en el netlist del top, no en
// el modelo. El análisis completo está en doc/stm32f4xx/stm32f407vg_fase5_i2c.md §1.
//
// Aun así el modelo está parametrizado con la misma receta que el resto del
// proyecto, por dos razones: el bloque SÍ varía entre familias de STM32 (hay
// derivados sin SMBus, sin dirección dual o sin filtro digital) y, sobre todo,
// porque tener los ejes explícitos es lo que permite comprobar por el bus que
// las tres instancias del F407 son iguales en vez de suponerlo.
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using I2c      = I2cT<CAPS_I2C_FULL>;    // I2C1, I2C2, I2C3
//         using I2cBasic = I2cT<CAPS_I2C_BASIC>;   // variante reducida
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         I2cBase b{"b", base, I2cCaps{...}};
//         I2cBase c{"c", base, /*smbus=*/false, /*f_max=*/100e3};   // atajo
//
// Fase F5 — implementado:
//   * banco de registros CR1/CR2/OAR1/OAR2/DR/SR1/SR2/CCR/TRISE/FLTR
//     [IR, §12.6.3];
//   * generador de reloj de Sm y Fm con las dos relaciones de ciclo de trabajo
//     (2:1 y 16:9) y el tiempo de subida de TRISE;
//   * MAESTRO a nivel de bit sobre los pines open-drain: START, START repetido,
//     STOP, direccionamiento de 7 y 10 bits, transmisión y recepción con ACK y
//     NACK, y la secuencia de banderas EV5-EV8 del manual;
//   * ESCLAVO dirigido por los flancos del bus: reconocimiento de START y STOP,
//     comparación con OAR1, con OAR2 (dirección dual) y con la llamada general,
//     transmisión y recepción, y estiramiento del reloj (NOSTRETCH);
//   * multimaestro: pérdida de arbitraje (ARLO) al ver la línea en cero cuando
//     se la deja libre, y error de bus (BERR) por START/STOP fuera de sitio;
//   * SMBus: PEC (CRC-8 de polinomio 0x07) en transmisión y recepción, PECERR,
//     pin de alerta SMBA y bandera SMBALERT;
//   * banderas SB/ADDR/BTF/TxE/RxNE/STOPF/AF/OVR con la semántica de borrado
//     real (leer SR1 y después SR2, o escribir DR, o escribir CR1);
//   * interrupciones de evento y de error separadas (ITEVTEN/ITBUFEN/ITERREN) y
//     peticiones de DMA con el bit LAST.
//
// El bus es ABIERTO EN COLECTOR: el periférico nunca fuerza un uno. Pone su
// salida a cero para tirar de la línea y a uno para soltarla, y es el pad —con
// OTYPER = open-drain— quien deja el pin en alta impedancia. El nivel alto lo
// tienen que dar las resistencias de pull-up de la placa, igual que en el
// sistema real; si el firmware olvida configurar open-drain, en el modelo se ve
// exactamente el mismo conflicto que en el silicio.
// =============================================================================
#ifndef STM32_PERIPH_I2C_H
#define STM32_PERIPH_I2C_H

#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos de la instancia.
// ---------------------------------------------------------------------------
struct I2cCaps {
    bool smbus          = true;   // CR1.SMBUS/SMBTYPE/ENARP/ALERT y el pin SMBA
    bool pec            = true;   // CR1.ENPEC/PEC, SR2.PEC[15:8] y PECERR
    bool dual_addr      = true;   // OAR2 y ENDUAL (segunda dirección de esclavo)
    bool general_call   = true;   // CR1.ENGC y SR2.GENCALL
    bool ten_bit        = true;   // OAR1.ADDMODE y el encabezado de 10 bits
    bool digital_filter = true;   // registro FLTR (ANOFF, DNF), propio de la F4
    bool dma            = true;   // CR2.DMAEN/LAST y las peticiones
    bool fast_mode      = true;   // CCR.F/S y DUTY (400 kHz)
    double max_scl_hz   = 400.0e3;
    const char* kind    = "I2C";
};

// --- Las variantes ---------------------------------------------------------
// En el F407 las TRES instancias usan la misma: son idénticas [IR, §12.6].
constexpr I2cCaps caps_i2c_full() {
    I2cCaps c{};
    c.kind = "I2C completo";
    return c;
}
// Variante reducida: el mismo bloque tal como aparece en derivados sin SMBus.
// No existe en el F407; sirve para comprobar que los ejes son independientes.
constexpr I2cCaps caps_i2c_basic() {
    I2cCaps c{};
    c.smbus = false; c.pec = false; c.dual_addr = false;
    c.general_call = false; c.ten_bit = false; c.digital_filter = false;
    c.fast_mode = false;
    c.max_scl_hz = 100.0e3;
    c.kind = "I2C basico";
    return c;
}

inline constexpr I2cCaps CAPS_I2C_FULL  = caps_i2c_full();
inline constexpr I2cCaps CAPS_I2C_BASIC = caps_i2c_basic();

// ---------------------------------------------------------------------------
// Implementación común. Recibe los rasgos por el constructor: este es el punto
// de selección en tiempo de ejecución.
// ---------------------------------------------------------------------------
class I2cBase : public BusSlave {
public:
    sc_core::sc_out<bool> irq_ev{"irq_ev"}, irq_er{"irq_er"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // Señales de función alternativa. Convenio de colector abierto: out = 0
    // tira de la línea, out = 1 la suelta (el pad en OTYPER = OD la deja en
    // alta impedancia). oe vale 1 mientras el periférico está habilitado.
    sc_core::sc_signal<bool> scl_out{"scl_out"}, scl_oe{"scl_oe"}, scl_in{"scl_in"};
    sc_core::sc_signal<bool> sda_out{"sda_out"}, sda_oe{"sda_oe"}, sda_in{"sda_in"};
    sc_core::sc_signal<bool> smba_out{"smba_out"}, smba_oe{"smba_oe"},
                             smba_in{"smba_in"};

    // ---- Offsets [IR, §12.6.3] --------------------------------------------
    enum : uint32_t { R_CR1 = 0x00, R_CR2 = 0x04, R_OAR1 = 0x08, R_OAR2 = 0x0C,
                      R_DR = 0x10, R_SR1 = 0x14, R_SR2 = 0x18, R_CCR = 0x1C,
                      R_TRISE = 0x20, R_FLTR = 0x24 };
    enum Sr1Bit : uint32_t {
        S_SB = 1u << 0, S_ADDR = 1u << 1, S_BTF = 1u << 2, S_ADD10 = 1u << 3,
        S_STOPF = 1u << 4, S_RXNE = 1u << 6, S_TXE = 1u << 7, S_BERR = 1u << 8,
        S_ARLO = 1u << 9, S_AF = 1u << 10, S_OVR = 1u << 11, S_PECERR = 1u << 12,
        S_TIMEOUT = 1u << 14, S_SMBALERT = 1u << 15
    };
    enum Sr2Bit : uint32_t {
        S2_MSL = 1u << 0, S2_BUSY = 1u << 1, S2_TRA = 1u << 2,
        S2_GENCALL = 1u << 4, S2_SMBDEFAULT = 1u << 5, S2_SMBHOST = 1u << 6,
        S2_DUALF = 1u << 7
    };

    // --- Constructor principal: los rasgos como parámetro ------------------
    I2cBase(sc_core::sc_module_name nm, uint32_t base,
            const I2cCaps& caps = CAPS_I2C_FULL)
        : BusSlave(nm, base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(I2cBase);
        SC_THREAD(master_proc);
        SC_METHOD(pub_proc);    sensitive << pub_ev_;
        SC_METHOD(reset_proc);  sensitive << rst_n;
        // El esclavo lo dirigen los flancos del bus, no un reloj propio.
        SC_METHOD(bus_proc);    sensitive << scl_in << sda_in; dont_initialize();
        SC_METHOD(alert_proc);  sensitive << smba_in;          dont_initialize();
        SC_METHOD(clk_proc);    sensitive << clk_hz;           dont_initialize();
    }
    // --- Atajo de selección en tiempo de ejecución -------------------------
    I2cBase(sc_core::sc_module_name nm, uint32_t base, bool smbus, double f_max)
        : I2cBase(nm, base, runtime_caps(smbus, f_max)) {}

    static I2cCaps runtime_caps(bool smbus, double f_max) {
        I2cCaps c{};
        c.smbus = smbus; c.pec = smbus;
        c.fast_mode = (f_max > 100.0e3);
        c.max_scl_hz = f_max;
        c.kind = "a medida";
        return c;
    }

    const I2cCaps& caps() const { return caps_; }

    // ---- Observación desde el banco de pruebas ----------------------------
    double   scl_hz()   const { return scl_hz_; }
    uint64_t bytes_tx() const { return n_tx_; }
    uint64_t bytes_rx() const { return n_rx_; }
    uint32_t sr1_raw()  const { return sr1_; }
    uint32_t sr2_raw()  const { return sr2_; }
    bool     is_master()const { return (sr2_ & S2_MSL) != 0; }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR1:   return cr1_;
            case R_CR2:   return cr2_;
            case R_OAR1:  return oar1_;
            case R_OAR2:  return caps_.dual_addr ? oar2_ : 0u;
            case R_DR:    return read_dr();
            case R_SR1:   sr1_read_ = true; return sr1_;
            case R_SR2:   return read_sr2();
            case R_CCR:   return ccr_;
            case R_TRISE: return trise_;
            case R_FLTR:  return caps_.digital_filter ? fltr_ : 0u;
            default:      return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                  // acceso parcial
            uint32_t cur = quiet_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR1: {
                const uint32_t old = cr1_;
                cr1_ = v & cr1_mask();
                if (cr1_ & (1u << 15)) { do_swrst(); return; }   // SWRST
                if ((old ^ cr1_) & 1u) { if (pe()) start_up(); else shut_down(); }
                // Escribir CR1 es la segunda mitad de la secuencia que borra STOPF
                if (sr1_read_ && (sr1_ & S_STOPF)) { sr1_ &= ~S_STOPF; sr1_read_ = false; }
                break;
            }
            case R_CR2:   cr2_ = v & cr2_mask(); break;
            case R_OAR1:  oar1_ = v & oar1_mask(); break;
            case R_OAR2:  if (caps_.dual_addr) oar2_ = v & 0x00FFu; break;
            case R_DR:    write_dr(uint8_t(v & 0xFFu)); break;
            case R_SR1:
                // Bits rc_w0: escribir cero borra los de error [IR, §12.6.3-C]
                sr1_ &= (v | ~sr1_w0_mask());
                break;
            case R_CCR:   if (!pe()) ccr_ = v & ccr_mask(); recompute_scl(); break;
            case R_TRISE: if (!pe()) trise_ = v & 0x3Fu; recompute_scl(); break;
            case R_FLTR:  if (caps_.digital_filter && !pe()) fltr_ = v & 0x1Fu; break;
            default:      return;
        }
        update_irq();
        wake();
    }

private:
    I2cCaps caps_;
    // ---- Registros --------------------------------------------------------
    uint32_t cr1_ = 0, cr2_ = 0, oar1_ = 0, oar2_ = 0;
    uint32_t sr1_ = 0, sr2_ = 0, ccr_ = 0, trise_ = 0x0002u, fltr_ = 0;
    uint8_t  dr_tx_ = 0, dr_rx_ = 0, pec_ = 0;
    bool     tx_full_ = false, rx_full_ = false;
    bool     sr1_read_ = false;
    double   scl_hz_ = 0.0;
    uint64_t n_tx_ = 0, n_rx_ = 0;
    // ---- Estado del maestro ------------------------------------------------
    bool     want_start_ = false, want_stop_ = false;
    bool     addr_sent_ = false;      // fase de dirección resuelta
    bool     m_reading_ = false;      // el maestro está en recepción
    bool     pending_pec_ = false;    // el próximo byte transmitido es el PEC
    // ---- Estado del esclavo (dirigido por los flancos del bus) -------------
    // SL_ACK_HOLD: el bit de reconocimiento ya está en SDA y esperamos a que
    // el maestro lo muestree en el flanco de subida. Sin ese estado el
    // esclavo se comería el flanco del ACK como si fuera el primer bit de
    // datos y, peor aún, dejaría SDA clavada a cero.
    enum SlState { SL_IDLE, SL_ADDR, SL_ADDR10, SL_RX, SL_TX,
                   SL_ACK_RX, SL_ACK_TX, SL_ACK_HOLD };
    SlState  sl_ = SL_IDLE;
    unsigned sl_bit_ = 0;
    uint8_t  sl_sh_ = 0;
    bool     sl_scl_prev_ = true, sl_sda_prev_ = true;
    bool     sl_selected_ = false, sl_tx_mode_ = false;
    bool     stretching_ = false;
    // ---- Salidas publicadas por un único proceso --------------------------
    bool o_scl_ = true, o_sda_ = true, o_oe_ = false;
    bool o_smba_ = true, o_smba_oe_ = false;
    bool o_irq_ev_ = false, o_irq_er_ = false;
    bool o_drq_rx_ = false, o_drq_tx_ = false;
    sc_core::sc_event pub_ev_, wake_ev_;

    // =======================================================================
    // Campos de los registros
    // =======================================================================
    bool     pe()        const { return cr1_ & 1u; }
    bool     smbus_on()  const { return caps_.smbus && ((cr1_ >> 1) & 1u); }
    bool     smbtype()   const { return caps_.smbus && ((cr1_ >> 3) & 1u); }
    bool     enarp()     const { return caps_.smbus && ((cr1_ >> 4) & 1u); }
    bool     enpec()     const { return caps_.pec && ((cr1_ >> 5) & 1u); }
    bool     engc()      const { return caps_.general_call && ((cr1_ >> 6) & 1u); }
    bool     nostretch() const { return (cr1_ >> 7) & 1u; }
    bool     start_bit() const { return (cr1_ >> 8) & 1u; }
    bool     stop_bit()  const { return (cr1_ >> 9) & 1u; }
    bool     ack()       const { return (cr1_ >> 10) & 1u; }
    bool     pos()       const { return (cr1_ >> 11) & 1u; }
    bool     pec_next()  const { return caps_.pec && ((cr1_ >> 12) & 1u); }
    bool     alert()     const { return caps_.smbus && ((cr1_ >> 13) & 1u); }
    unsigned freq()      const { return cr2_ & 0x3Fu; }
    bool     iterren()   const { return (cr2_ >> 8) & 1u; }
    bool     itevten()   const { return (cr2_ >> 9) & 1u; }
    bool     itbufen()   const { return (cr2_ >> 10) & 1u; }
    bool     dmaen()     const { return caps_.dma && ((cr2_ >> 11) & 1u); }
    bool     dma_last()  const { return caps_.dma && ((cr2_ >> 12) & 1u); }
    bool     addmode()   const { return caps_.ten_bit && ((oar1_ >> 15) & 1u); }
    unsigned own1()      const { return addmode() ? (oar1_ & 0x3FFu)
                                                  : ((oar1_ >> 1) & 0x7Fu); }
    bool     endual()    const { return caps_.dual_addr && (oar2_ & 1u); }
    unsigned own2()      const { return (oar2_ >> 1) & 0x7Fu; }
    bool     fast()      const { return caps_.fast_mode && ((ccr_ >> 15) & 1u); }
    bool     duty()      const { return caps_.fast_mode && ((ccr_ >> 14) & 1u); }
    unsigned ccr_val()   const { return ccr_ & 0xFFFu; }

    // =======================================================================
    // Máscaras de escritura: aquí los rasgos se vuelven observables
    // =======================================================================
    uint32_t cr1_mask() const {
        // PE, NOSTRETCH, START, STOP, ACK y POS existen siempre; el resto
        // depende de los rasgos de la instancia.
        uint32_t m = (1u << 0) | (1u << 7) | (1u << 8) | (1u << 9) |
                     (1u << 10) | (1u << 11);
        m |= 1u << 15;                              // SWRST
        if (caps_.smbus)        m |= (1u << 1) | (1u << 3) | (1u << 4) | (1u << 13);
        if (caps_.pec)          m |= (1u << 5) | (1u << 12);
        if (caps_.general_call) m |= 1u << 6;
        return m;
    }
    uint32_t cr2_mask() const {
        uint32_t m = 0x003Fu | (1u << 8) | (1u << 9) | (1u << 10);  // FREQ, ITxxEN
        if (caps_.dma) m |= (1u << 11) | (1u << 12);                // DMAEN, LAST
        return m;
    }
    uint32_t oar1_mask() const {
        // El bit 14 debe mantenerse a uno por software [IR, §12.6.3-E]
        uint32_t m = 0x40FFu | (1u << 8) | (1u << 9);
        if (caps_.ten_bit) m |= 1u << 15;           // ADDMODE
        return m;
    }
    uint32_t ccr_mask() const {
        uint32_t m = 0x0FFFu;
        if (caps_.fast_mode) m |= (1u << 15) | (1u << 14);   // F/S, DUTY
        return m;
    }
    uint32_t sr1_w0_mask() const {
        uint32_t m = S_BERR | S_ARLO | S_AF | S_OVR;
        if (caps_.pec)   m |= S_PECERR;
        if (caps_.smbus) m |= S_TIMEOUT | S_SMBALERT;
        return m;
    }

    // =======================================================================
    // Reloj del bus [IR, §12.6.3-D]
    // =======================================================================
    void clk_proc() { recompute_scl(); wake(); }
    void recompute_scl() {
        const double f = clk_hz.read();
        const unsigned c = ccr_val();
        if (f <= 0.0 || c == 0) { scl_hz_ = 0.0; return; }
        // Sm: T_high = T_low = CCR * T_PCLK1   -> f = f_PCLK1 / (2*CCR)
        // Fm: 2:1  -> f = f_PCLK1 / (3*CCR) ;  16:9 -> f = f_PCLK1 / (25*CCR)
        const double k = !fast() ? 2.0 : (duty() ? 25.0 : 3.0);
        scl_hz_ = f / (k * double(c));
        if (scl_hz_ > caps_.max_scl_hz * 1.01)
            SC_REPORT_WARNING("i2c", "SCL por encima del maximo de la variante");
    }
    // Semiperiodos bajo y alto, con el tiempo de subida de TRISE
    sc_core::sc_time t_low() const {
        if (scl_hz_ <= 0.0) return sc_core::sc_time(10, sc_core::SC_US);
        const double per = 1.0 / scl_hz_;
        const double frac = !fast() ? 0.5 : (duty() ? 16.0 / 25.0 : 2.0 / 3.0);
        return sc_core::sc_time(per * frac * 1.0e12, sc_core::SC_PS);
    }
    sc_core::sc_time t_high() const {
        if (scl_hz_ <= 0.0) return sc_core::sc_time(10, sc_core::SC_US);
        const double per = 1.0 / scl_hz_;
        const double frac = !fast() ? 0.5 : (duty() ? 9.0 / 25.0 : 1.0 / 3.0);
        return sc_core::sc_time(per * frac * 1.0e12, sc_core::SC_PS);
    }

    // =======================================================================
    // Registro de datos y secuencias de borrado de banderas
    // =======================================================================
    uint32_t quiet_read(uint32_t off) {
        switch (off) {
            case R_CR1: return cr1_;   case R_CR2:  return cr2_;
            case R_OAR1: return oar1_; case R_OAR2: return oar2_;
            case R_DR:  return dr_tx_; case R_SR1:  return sr1_;
            case R_SR2: return sr2_;   case R_CCR:  return ccr_;
            case R_TRISE: return trise_; case R_FLTR: return fltr_;
            default: return 0;
        }
    }
    uint32_t read_sr2() {
        // Leer SR1 y después SR2 borra ADDR [IR, §12.6.3-C]
        uint32_t v = sr2_;
        if (caps_.pec) v |= uint32_t(pec_) << 8;
        if (sr1_read_ && (sr1_ & S_ADDR)) {
            sr1_ &= ~S_ADDR;
            sr1_read_ = false;
            release_stretch();
            update_irq();
            wake();
        }
        return v;
    }
    uint32_t read_dr() {
        const uint32_t v = dr_rx_;
        if (rx_full_) {
            rx_full_ = false;
            sr1_ &= ~S_RXNE;
            sr1_ &= ~S_BTF;
            release_stretch();
            update_irq();
            wake();
        }
        return v;
    }
    void write_dr(uint8_t v) {
        dr_tx_ = v;
        tx_full_ = true;
        sr1_ &= ~S_TXE;
        sr1_ &= ~S_BTF;
        if (sr1_read_ && (sr1_ & S_SB)) { sr1_ &= ~S_SB; sr1_read_ = false; }
        release_stretch();
        update_irq();
        wake();
    }

    // =======================================================================
    // Salidas
    // =======================================================================
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void wake()    { wake_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        scl_out.write(o_scl_); scl_oe.write(o_oe_);
        sda_out.write(o_sda_); sda_oe.write(o_oe_);
        smba_out.write(o_smba_); smba_oe.write(o_smba_oe_);
        irq_ev.write(o_irq_ev_); irq_er.write(o_irq_er_);
        dma_req_rx.write(o_drq_rx_); dma_req_tx.write(o_drq_tx_);
    }
    void set_scl(bool v) { o_scl_ = v; publish(); }
    void set_sda(bool v) { o_sda_ = v; publish(); }

    void update_irq() {
        // Interrupción de EVENTO [IR, §12.6.3]
        bool ev = false;
        if (sr1_ & (S_SB | S_ADDR | S_ADD10 | S_STOPF | S_BTF)) ev = true;
        bool buf = false;
        if (sr1_ & S_TXE) buf = true;
        if (sr1_ & S_RXNE) buf = true;
        o_irq_ev_ = pe() && itevten() && (ev || (itbufen() && buf));
        // Interrupción de ERROR
        const uint32_t errs = S_BERR | S_ARLO | S_AF | S_OVR |
                              (caps_.pec ? S_PECERR : 0u) |
                              (caps_.smbus ? (S_TIMEOUT | S_SMBALERT) : 0u);
        o_irq_er_ = pe() && iterren() && (sr1_ & errs);
        // Peticiones de DMA: de nivel, las retira el acceso a DR
        o_drq_tx_ = pe() && dmaen() && (sr1_ & S_TXE) && (sr2_ & S2_TRA);
        o_drq_rx_ = pe() && dmaen() && (sr1_ & S_RXNE) && !(sr2_ & S2_TRA);
        publish();
    }

    void start_up() {
        recompute_scl();
        o_oe_ = true; o_scl_ = true; o_sda_ = true;     // líneas liberadas
        o_smba_oe_ = smbus_on() && smbtype();
        sl_ = SL_IDLE; sl_selected_ = false; stretching_ = false;
        sl_scl_prev_ = scl_in.read(); sl_sda_prev_ = sda_in.read();
        publish();
        wake();
    }
    void shut_down() {
        o_oe_ = false; o_scl_ = true; o_sda_ = true; o_smba_oe_ = false;
        sr1_ = 0; sr2_ = 0;
        sl_ = SL_IDLE; sl_selected_ = false; stretching_ = false;
        tx_full_ = rx_full_ = false;
        want_start_ = want_stop_ = false;
        publish();
        wake();
    }
    void do_swrst() {
        // SWRST deja el periférico como tras el reset mientras esté a uno
        const uint32_t keep = cr1_ & (1u << 15);
        reset_state();
        cr1_ = keep;
        publish();
        wake();
    }
    void reset_state() {
        cr1_ = cr2_ = oar1_ = oar2_ = 0;
        sr1_ = sr2_ = 0; ccr_ = 0; trise_ = 0x0002u; fltr_ = 0;
        dr_tx_ = dr_rx_ = pec_ = 0;
        tx_full_ = rx_full_ = false; sr1_read_ = false;
        want_start_ = want_stop_ = addr_sent_ = m_reading_ = false;
        pending_pec_ = false;
        sl_ = SL_IDLE; sl_bit_ = 0; sl_sh_ = 0;
        sl_selected_ = false; sl_tx_mode_ = false; stretching_ = false;
       
        scl_hz_ = 0.0; n_tx_ = n_rx_ = 0;
        o_scl_ = o_sda_ = true; o_oe_ = false;
        o_smba_ = true; o_smba_oe_ = false;
        update_irq();
    }
    void reset_proc() { if (!rst_n.read()) { reset_state(); publish(); wake(); } }

    // El pin de alerta de SMBus levanta SMBALERT [IR, §12.6.1]
    void alert_proc() {
        if (!smbus_on() || !alert()) return;
        if (smba_in.read()) return;                    // activo en bajo
        sr1_ |= S_SMBALERT;
        update_irq();
    }

    // =======================================================================
    // PEC: CRC-8 de polinomio x^8 + x^2 + x + 1 [IR, §12.6.1]
    // =======================================================================
    static uint8_t crc8(uint8_t crc, uint8_t data) {
        crc ^= data;
        for (unsigned i = 0; i < 8; ++i)
            crc = uint8_t((crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1));
        return crc;
    }
    void pec_add(uint8_t b) { if (enpec()) pec_ = crc8(pec_, b); }

    // =======================================================================
    // MAESTRO: genera el reloj y marca el ritmo del bus
    // =======================================================================
    // Suelta SCL y espera a que la línea suba de verdad: cualquier esclavo
    // puede estirar el reloj manteniéndola baja [IR, §12.6.1].
    bool release_scl_and_wait() {
        set_scl(true);
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        const sc_core::sc_time limit = t_low() * 200.0;
        while (!scl_in.read()) {
            if (sc_core::sc_time_stamp() - t0 > limit) {
                if (caps_.smbus && smbus_on()) { sr1_ |= S_TIMEOUT; update_irq(); }
                return false;
            }
            wait(t_high() / 4.0, scl_in.value_changed_event());
        }
        return true;
    }
    // Un bit hacia el bus. Devuelve false si se pierde el arbitraje.
    bool m_bit_out(bool b) {
        set_scl(false);
        wait(t_low() / 2.0);
        set_sda(b);
        wait(t_low() / 2.0);
        if (!release_scl_and_wait()) return false;
        wait(t_high() / 2.0);
        // Arbitraje: si soltamos la línea y alguien la tiene baja, la perdemos
        if (b && !sda_in.read()) {
            sr1_ |= S_ARLO;
            lose_arbitration();
            return false;
        }
        wait(t_high() / 2.0);
        set_scl(false);
        return true;
    }
    bool m_bit_in() {
        set_scl(false);
        wait(t_low() / 2.0);
        set_sda(true);                                 // soltar para leer
        wait(t_low() / 2.0);
        if (!release_scl_and_wait()) return true;
        wait(t_high() / 2.0);
        const bool b = sda_in.read();
        wait(t_high() / 2.0);
        set_scl(false);
        return b;
    }
    void lose_arbitration() {
        set_sda(true); set_scl(true);
        sr2_ &= ~(S2_MSL | S2_BUSY | S2_TRA);
        addr_sent_ = false; want_start_ = false; want_stop_ = false;
        update_irq();
    }
    // Devuelve true si el receptor ha reconocido el byte
    bool m_send_byte(uint8_t v, bool& lost) {
        lost = false;
        for (int i = 7; i >= 0; --i)
            if (!m_bit_out((v >> i) & 1u)) { lost = true; return false; }
        const bool nak = m_bit_in();
        return !nak;
    }
    // Tras el bit de reconocimiento hay que SOLTAR SDA de inmediato: el esclavo
    // pone su siguiente bit en el mismo flanco de bajada, y si el maestro sigue
    // tirando de la línea lo enmascara. Es el error clásico de un maestro I2C
    // escrito a mano, y aquí lo destapó la EEPROM del banco de pruebas.
    void m_release_sda() { set_sda(true); }
    uint8_t m_recv_byte(bool send_ack, bool& lost) {
        lost = false;
        uint8_t v = 0;
        for (unsigned i = 0; i < 8; ++i) v = uint8_t((v << 1) | (m_bit_in() ? 1u : 0u));
        if (!m_bit_out(!send_ack)) lost = true;         // ACK = línea a cero
        m_release_sda();
        return v;
    }
    void m_start(bool repeated) {
        if (repeated) {                                 // START repetido
            set_scl(false); wait(t_low() / 2.0);
            set_sda(true);  wait(t_low() / 2.0);
            release_scl_and_wait();
            wait(t_high());
        } else {
            set_sda(true); set_scl(true);
            wait(t_high());
        }
        set_sda(false);                                 // SDA baja con SCL alto
        wait(t_high());
        set_scl(false);
        sr2_ |= S2_MSL | S2_BUSY;
        sr1_ |= S_SB;
        addr_sent_ = false;
        if (enpec()) pec_ = 0;
        update_irq();
    }
    void m_stop() {
        set_scl(false); wait(t_low() / 2.0);
        set_sda(false); wait(t_low() / 2.0);
        release_scl_and_wait();
        wait(t_high());
        set_sda(true);                                  // SDA sube con SCL alto
        wait(t_high());
        sr2_ &= ~(S2_MSL | S2_BUSY | S2_TRA);
        cr1_ &= ~(1u << 9);                             // el hardware borra STOP
        addr_sent_ = false;
        sr1_ &= ~(S_TXE | S_BTF);
        tx_full_ = false;
        update_irq();
    }
    // Espera a que el firmware haga algo (escribir DR, leer SR2, pedir STOP...)
    bool m_wait_sw(sc_core::sc_time limit = sc_core::sc_time(50, sc_core::SC_MS)) {
        const sc_core::sc_time t0 = sc_core::sc_time_stamp();
        wait(limit - (sc_core::sc_time_stamp() - t0), wake_ev_);
        return sc_core::sc_time_stamp() - t0 < limit;
    }

    void master_proc() {
        for (;;) {
            if (!pe() || !clock_enabled() || scl_hz_ <= 0.0) {
                wait(wake_ev_ | rst_n.value_changed_event() |
                     clk_hz.value_changed_event());
                continue;
            }
            // START pedido por software: solo se acepta si el bus está libre
            if (start_bit() && !(sr2_ & S2_MSL)) {
                if (!sda_in.read() || !scl_in.read()) {  // bus ocupado por otro
                    wait(t_low(), wake_ev_);
                    continue;
                }
                cr1_ &= ~(1u << 8);                      // el hardware borra START
                m_start(false);
                continue;
            }
            if (!(sr2_ & S2_MSL)) { wait(wake_ev_); continue; }

            // --- Fase de dirección --------------------------------------
            if (!addr_sent_) {
                if (start_bit()) {                       // START repetido
                    cr1_ &= ~(1u << 8);
                    m_start(true);
                    continue;
                }
                if (!tx_full_) {
                    // El firmware puede ABANDONAR aquí: tras un fallo de
                    // reconocimiento lo normal es pedir STOP en vez de escribir
                    // otra dirección. Si no se atendiera, la petición quedaría
                    // pendiente y cerraría la transferencia SIGUIENTE
                    // [IR, §12.6.1]; es un fallo real y difícil de ver.
                    if (stop_bit()) { m_stop(); continue; }
                    if (!m_wait_sw()) { m_stop(); }
                    continue;
                }
                const uint8_t a = dr_tx_;
                tx_full_ = false;
                sr1_ &= ~S_SB;
                bool lost = false;
                const bool acked = m_send_byte(a, lost);
                if (lost) continue;
                if (!acked) {                            // nadie responde
                    sr1_ |= S_AF;
                    update_irq();
                    if (!m_wait_sw()) { }
                    if (stop_bit()) m_stop();
                    continue;
                }
                pec_add(a);
                m_reading_ = (a & 1u) != 0;
                addr_sent_ = true;
                if (m_reading_) sr2_ &= ~S2_TRA; else sr2_ |= S2_TRA;
                sr1_ |= S_ADDR;
                if (!m_reading_) { sr1_ |= S_TXE; }
                update_irq();
                // El bus se estira hasta que el firmware borre ADDR leyendo
                // SR1 y luego SR2 [IR, §12.6.3-C]
                while ((sr1_ & S_ADDR) && pe()) if (!m_wait_sw()) break;
                continue;
            }

            // --- Transferencia ------------------------------------------
            // START REPETIDO en mitad de la transferencia: es el patrón que usa
            // todo driver de EEPROM para girar de escritura a lectura sin
            // soltar el bus. La petición llega con la dirección ya enviada, así
            // que hay que atenderla aquí y no solo en la fase de dirección
            // [IR, §12.6.1].
            if (start_bit()) {
                cr1_ &= ~(1u << 8);
                addr_sent_ = false;
                tx_full_ = false;
                sr1_ &= ~(S_TXE | S_BTF);
                m_start(true);
                continue;
            }
            if (m_reading_) {
                if (rx_full_) {                          // esperar a que lean DR
                    if (!m_wait_sw()) { }
                    continue;
                }
                // El firmware borra ACK y pide STOP ANTES de que llegue el
                // último byte: ese byte se recibe, se contesta con NACK y solo
                // entonces se genera el STOP [IR, §12.6.1].
                bool lost = false;
                const bool send_ack = ack();
                const uint8_t v = m_recv_byte(send_ack, lost);
                if (lost) continue;
                pec_add(v);
                ++n_rx_;
                if (rx_full_) sr1_ |= S_OVR;
                dr_rx_ = v; rx_full_ = true;
                sr1_ |= S_RXNE;
                update_irq();
                if (!send_ack && stop_bit()) m_stop();
            } else {
                if (!tx_full_) {
                    if (stop_bit()) { m_stop(); continue; }
                    if (pec_next() && enpec() && !pending_pec_) {
                        pending_pec_ = true;
                        dr_tx_ = pec_; tx_full_ = true;  // el PEC va como un byte
                    } else {
                        sr1_ |= S_TXE;
                        update_irq();
                        if (!m_wait_sw()) { }
                        continue;
                    }
                }
                const uint8_t v = dr_tx_;
                tx_full_ = false;
                sr1_ |= S_TXE;
                update_irq();
                bool lost = false;
                const bool acked = m_send_byte(v, lost);
                if (lost) continue;
                if (pending_pec_) { pending_pec_ = false; cr1_ &= ~(1u << 12); }
                else pec_add(v);
                ++n_tx_;
                if (!acked) { sr1_ |= S_AF; update_irq(); }
                if (!tx_full_) { sr1_ |= S_BTF; update_irq(); }
            }
        }
    }

    // =======================================================================
    // ESCLAVO: dirigido por los flancos del bus
    // =======================================================================
    void release_stretch() {
        if (!stretching_) return;
        stretching_ = false;
        o_scl_ = true;
        publish();
    }
    void hold_scl() {
        if (nostretch()) return;
        stretching_ = true;
        o_scl_ = false;
        publish();
    }
    // ¿La dirección recibida es para nosotros?
    bool addr_match(uint8_t b, bool& dual, bool& gc) {
        dual = false; gc = false;
        const unsigned a = (b >> 1) & 0x7Fu;
        if (caps_.general_call && engc() && b == 0x00u) { gc = true; return true; }
        if (!addmode() && a == own1()) return true;
        if (endual() && a == own2()) { dual = true; return true; }
        if (addmode()) {                                 // encabezado de 10 bits
            const unsigned hdr = 0x78u | ((own1() >> 8) & 3u);
            if ((b >> 1) == hdr) return true;
        }
        return false;
    }

    void bus_proc() {
        if (!pe() || !clock_enabled()) return;
        const bool scl = scl_in.read(), sda = sda_in.read();
        const bool scl_prev = sl_scl_prev_, sda_prev = sl_sda_prev_;
        sl_scl_prev_ = scl; sl_sda_prev_ = sda;

        // --- Condiciones de START y STOP (cambio de SDA con SCL alto) -----
        if (scl && scl_prev) {
            if (sda_prev && !sda) {                      // START
                if (sr2_ & S2_MSL) return;               // el maestro somos nosotros
                sl_ = SL_ADDR; sl_bit_ = 0; sl_sh_ = 0;
                sl_selected_ = false;
                sr2_ |= S2_BUSY;
                if (enpec()) pec_ = 0;
                update_irq();
                return;
            }
            if (!sda_prev && sda) {                      // STOP
                if (sl_selected_) { sr1_ |= S_STOPF; update_irq(); }
                sl_ = SL_IDLE; sl_selected_ = false;
                sr2_ &= ~(S2_BUSY | S2_TRA | S2_GENCALL | S2_DUALF);
                set_sda(true);
                update_irq();
                return;
            }
        }
        if (sr2_ & S2_MSL) return;                       // como maestro, nada más

        // --- Flanco de subida de SCL: se muestrea el bus ------------------
        if (scl && !scl_prev) {
            switch (sl_) {
                case SL_ADDR:
                    sl_sh_ = uint8_t((sl_sh_ << 1) | (sda ? 1u : 0u));
                    if (++sl_bit_ == 8) sl_addr_complete();
                    break;
                case SL_RX:
                    sl_sh_ = uint8_t((sl_sh_ << 1) | (sda ? 1u : 0u));
                    if (++sl_bit_ == 8) sl_rx_complete();
                    break;
                case SL_ACK_HOLD:                        // ACK ya muestreado
                    sl_ = sl_tx_mode_ ? SL_TX : SL_RX;
                    sl_bit_ = 0; sl_sh_ = 0;
                    if (sl_tx_mode_) sl_load_tx();
                    break;
                case SL_TX:                              // último bit muestreado
                    if (sl_bit_ >= 8) sl_ = SL_ACK_RX;
                    break;
                case SL_ACK_RX:                          // leemos el ACK del maestro
                    sl_tx_ack(sda);
                    break;
                default: break;
            }
            return;
        }

        // --- Flanco de bajada de SCL: se prepara el siguiente bit ---------
        if (!scl && scl_prev) {
            // ¿Es el flanco que sigue al pulso de reconocimiento?
            const bool tras_ack = (sl_bit_ == 0) && (sl_ == SL_RX || sl_ == SL_TX);
            switch (sl_) {
                case SL_ACK_TX:                          // toca reconocer
                    if (sl_selected_) set_sda(false);    // ACK
                    sl_ = SL_ACK_HOLD;
                    break;
                case SL_RX:
                    if (sl_bit_ == 0) set_sda(true);     // soltar tras el ACK
                    break;
                case SL_TX:
                    if (sl_bit_ < 8) {
                        set_sda((sl_sh_ >> (7 - sl_bit_)) & 1u);
                        ++sl_bit_;
                    }
                    break;
                case SL_ACK_RX:
                    set_sda(true);                       // dejar contestar al maestro
                    break;
                default: break;
            }
            // El noveno pulso ya ha pasado: ahora sí se puede retener el reloj,
            // pero solo si la causa SIGUE viva. El firmware puede haber borrado
            // ADDR o leído DR entre medias, y retener el reloj por una condición
            // ya atendida deja el bus colgado para siempre.
            if (tras_ack && sl_selected_ && ((sr1_ & S_ADDR) || rx_full_))
                hold_scl();
        }
    }

    void sl_addr_complete() {
        bool dual = false, gc = false;
        if (!addr_match(sl_sh_, dual, gc)) {             // no es para nosotros
            sl_ = SL_IDLE; sl_selected_ = false;
            set_sda(true);
            return;
        }
        sl_selected_ = true;
        sl_tx_mode_ = (sl_sh_ & 1u) != 0;
        pec_add(sl_sh_);
        sr1_ |= S_ADDR;
        if (dual) sr2_ |= S2_DUALF; else sr2_ &= ~S2_DUALF;
        if (gc)   sr2_ |= S2_GENCALL;
        if (sl_tx_mode_) sr2_ |= S2_TRA; else sr2_ &= ~S2_TRA;
        sl_ = SL_ACK_TX;
        // El estiramiento empieza DESPUÉS del noveno pulso, no antes: si se
        // tirara de SCL aquí se truncaría el bit en curso y el maestro leería
        // el reconocimiento fuera de sitio [IR, §12.6.1].
        update_irq();                                    // borre ADDR
    }
    void sl_load_tx() {
        if (tx_full_) { sl_sh_ = dr_tx_; tx_full_ = false; sr1_ |= S_TXE; }
        else          { sl_sh_ = 0xFFu; }                // sin dato: unos
        update_irq();
    }
    void sl_rx_complete() {
        ++n_rx_;
        if (rx_full_) sr1_ |= S_OVR;
        dr_rx_ = sl_sh_; rx_full_ = true;
        sr1_ |= S_RXNE;
        pec_add(sl_sh_);
        sl_ = SL_ACK_TX;                                 // reconoceremos el byte
        sl_tx_mode_ = false;
        update_irq();
    }
    void sl_tx_ack(bool nak) {
        ++n_tx_;
        if (nak) {                                       // el maestro no quiere más
            sr1_ |= S_AF;
            sl_ = SL_IDLE;
            set_sda(true);
        } else {
            sl_ = SL_TX; sl_bit_ = 0;
            sl_load_tx();
        }
        update_irq();
    }
};

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN.
// ---------------------------------------------------------------------------
template <const I2cCaps& Caps>
class I2cT : public I2cBase {
public:
    I2cT(sc_core::sc_module_name nm, uint32_t base) : I2cBase(nm, base, Caps) {}
    static constexpr const I2cCaps& variant() { return Caps; }
    static constexpr bool has_smbus()   { return Caps.smbus; }
    static constexpr bool has_dual()    { return Caps.dual_addr; }
    static constexpr bool has_ten_bit() { return Caps.ten_bit; }
    static constexpr double max_scl()   { return Caps.max_scl_hz; }
};

using I2c      = I2cT<CAPS_I2C_FULL>;    // I2C1, I2C2 e I2C3: las tres iguales
using I2cBasic = I2cT<CAPS_I2C_BASIC>;   // variante reducida (no existe en el F407)

static_assert(I2c::has_smbus(),      "las tres I2C del F407 soportan SMBus/PMBus");
static_assert(I2c::has_dual(),       "y direccionamiento dual");
static_assert(I2c::has_ten_bit(),    "y de 10 bits");
static_assert(!I2cBasic::has_smbus(), "la variante reducida no");
static_assert(I2c::max_scl() > I2cBasic::max_scl(), "modo rapido frente a estandar");

} // namespace stm32
#endif // STM32_PERIPH_I2C_H
