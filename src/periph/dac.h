// =============================================================================
// dac.h — Convertidor digital-analógico de dos canales [IR, §12.14]
//
// El STM32F407VG lleva UN solo bloque DAC con DOS CANALES, así que aquí la
// pregunta «en qué se diferencian» se responde comparando los dos canales entre
// sí. Y la respuesta es tajante: son SIMÉTRICOS hasta el bit. `DAC_CR` repite
// para el canal 2 exactamente los mismos campos del canal 1 desplazados 16
// bits; `DAC_SR` pone DMAUDR1 en el bit 13 y DMAUDR2 en el 29 —el mismo
// desplazamiento—; y cada registro de datos (`DHR12Rx`, `DHR12Lx`, `DHR8Rx`)
// existe por duplicado. Ni siquiera la tabla de disparos cambia: los dos
// canales eligen entre las mismas ocho fuentes.
//
// Lo único que los distingue es su INTEGRACIÓN: el canal 1 sale por PA4 y el 2
// por PA5, y sus peticiones de DMA van a celdas distintas (DMA1 S5C7 y S6C7).
// Comparten hasta la interrupción, que además NO es suya: el desbordamiento de
// DMA entra por el vector 54, el mismo del TIM6 [IR, §12.14].
//
// Hay una tercera cosa que no es de ningún canal sino DE LA PAREJA: los
// registros DUALES (`DHR12RD`, `DHR12LD`, `DHR8RD`) cargan los dos con una sola
// escritura, que es lo que permite actualizarlos en el mismo instante.
//
// Aun así el modelo está parametrizado con la receta de familia del proyecto:
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using Dac      = DacT<CAPS_DAC_F407>;   // 2 canales, 12 bits
//         using Dac1Ch   = DacT<CAPS_DAC_1CH>;    // un solo canal
//         using DacBasic = DacT<CAPS_DAC_BASIC>;  // 8 bits, sin ondas ni DMA
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         DacBase d{"d", DacCaps{...}};
//
// Los rasgos cubren lo que pide el encargo —número de bits, número de canales,
// buffer de salida, generadores de onda, disparo, DMA y registros duales— y se
// aplican como MÁSCARA DE ESCRITURA de cada registro, de modo que un bit que la
// instancia no implementa lee cero exactamente igual que un bit reservado del
// silicio.
//
// Lado analógico: la salida NO es un número, es un driver Thevenin sobre el
// nodo del pin, en float [IR, §12.14]:
//
//     V = DOR / (2^N - 1) * VREF+       con  R_out según el buffer
//
// y ahí está la diferencia física que más se nota: con el buffer activado la
// salida tiene impedancia baja y puede con una carga, pero no llega a los
// raíles (se queda a 0,2 V de cada extremo); con BOFF la salida es de alta
// impedancia (~15 kohm) y llega de raíl a raíl, pero cualquier carga la hunde.
// El modelo no decide nada de eso: lo resuelve el divisor del nodo.
//
// Implementado en la fase F5:
//   * banco de registros CR/SWTRIGR/DHR12Rx/DHR12Lx/DHR8Rx/DHR12RD/DHR12LD/
//     DHR8RD/DORx/SR [IR, §12.14.2];
//   * los tres formatos de dato (12 bits a la derecha, 12 a la izquierda y 8) y
//     los registros duales de carga simultánea;
//   * transferencia DHR -> DOR: inmediata (un ciclo de APB1) sin disparo, y a
//     los tres ciclos del disparo con TEN = 1;
//   * disparo por software (SWTRIGR) y por TRGO de TIM2/4/5/6/7/8, indexado por
//     TSEL;
//   * generadores de onda: ruido por LFSR de 12 bits y triángulo, con la
//     amplitud de MAMP;
//   * buffer de salida (BOFF) con su impedancia y su recorrido útil, y latencia
//     de estabilización t_SETTLING;
//   * peticiones de DMA por canal, desbordamiento DMAUDRx (w1c) y la IRQ 54,
//     compartida con el TIM6.
// =============================================================================
#ifndef STM32_PERIPH_DAC_H
#define STM32_PERIPH_DAC_H

#include <array>
#include "../common/periph_base.h"
#include "../common/analog_net.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos del bloque. Es lo que permitiría describir el mismo DAC tal como
// aparece en otras familias de STM32 (las hay de un solo canal, de 8 bits, o
// sin generador de ondas).
// ---------------------------------------------------------------------------
struct DacCaps {
    unsigned n_channels  = 2;      // canales de salida
    unsigned bits        = 12;     // resolución del convertidor
    bool     fmt_left    = true;   // registros DHR12Lx (alineado a la izquierda)
    bool     fmt_8bit    = true;   // registros DHR8Rx
    bool     dual        = true;   // DHR12RD/DHR12LD/DHR8RD (carga simultánea)
    bool     buffer      = true;   // CR.BOFFx y el amplificador de salida
    bool     noise       = true;   // WAVE = 01, LFSR
    bool     triangle    = true;   // WAVE = 1x
    bool     trigger     = true;   // CR.TENx/TSELx y SWTRIGR
    bool     dma         = true;   // CR.DMAENx, SR.DMAUDRx
    double   t_settle_us = 3.0;    // latencia de estabilización [IR, §12.14]
    double   r_buffered  = 15.0;   // impedancia de salida con buffer [ohm]
    double   r_open      = 15.0e3; // impedancia de salida con BOFF [ohm]
    double   v_margin    = 0.2;    // el buffer no llega a los raíles [V]
    const char* kind     = "DAC";
};

// --- Las variantes ---------------------------------------------------------
// El F407: dos canales de 12 bits con todo [IR, §12.14.1].
constexpr DacCaps caps_dac_f407() {
    DacCaps c{};
    c.kind = "DAC de 2 canales y 12 bits";
    return c;
}
// Un solo canal: existe así en varios derivados pequeños. Sin canal 2 no hay
// registros duales que valgan.
constexpr DacCaps caps_dac_1ch() {
    DacCaps c{};
    c.n_channels = 1; c.dual = false;
    c.kind = "DAC de 1 canal";
    return c;
}
// Variante reducida: 8 bits, un canal, sin buffer, sin ondas, sin disparo y
// sin DMA. No existe en el F407; sirve para comprobar que los ejes son
// independientes entre sí.
constexpr DacCaps caps_dac_basic() {
    DacCaps c{};
    c.n_channels = 1; c.bits = 8;
    c.fmt_left = false; c.dual = false;
    c.buffer = false; c.noise = false; c.triangle = false;
    c.trigger = false; c.dma = false;
    c.kind = "DAC basico";
    return c;
}

inline constexpr DacCaps CAPS_DAC_F407  = caps_dac_f407();
inline constexpr DacCaps CAPS_DAC_1CH   = caps_dac_1ch();
inline constexpr DacCaps CAPS_DAC_BASIC = caps_dac_basic();

// ---------------------------------------------------------------------------
// Implementación común. Los rasgos llegan por el constructor: este es el punto
// de selección en tiempo de ejecución.
// ---------------------------------------------------------------------------
class DacBase : public BusSlave {
public:
    static constexpr unsigned N_CH = 2;

    sc_core::sc_out<bool> irq{"irq"};                    // IRQ 54, con el TIM6
    sc_core::sc_out<bool> dma_req_ch1{"dma_req_ch1"}, dma_req_ch2{"dma_req_ch2"};
    sc_core::sc_in<double> vref{"vref"};                 // VREF+
    // Disparos externos INDEXADOS POR TSEL: 0 = TIM6_TRGO, 1 = TIM8_TRGO,
    // 2 = TIM7_TRGO, 3 = TIM5_TRGO, 4 = TIM2_TRGO, 5 = TIM4_TRGO, 6 = EXTI9,
    // 7 = software (no llega por aquí: lo da SWTRIGR) [IR, §12.14.2-B].
    sc_core::sc_vector<sc_core::sc_in<bool>> trig;       // [8]

    // ---- Offsets [IR, §12.14.2-C] -----------------------------------------
    enum : uint32_t {
        R_CR = 0x00, R_SWTRIGR = 0x04,
        R_DHR12R1 = 0x08, R_DHR12L1 = 0x0C, R_DHR8R1 = 0x10,
        R_DHR12R2 = 0x14, R_DHR12L2 = 0x18, R_DHR8R2 = 0x1C,
        R_DHR12RD = 0x20, R_DHR12LD = 0x24, R_DHR8RD = 0x28,
        R_DOR1 = 0x2C, R_DOR2 = 0x30, R_SR = 0x34
    };
    // SR: una bandera por canal, con el mismo desplazamiento de 16 bits que
    // usa CR. Se borran escribiendo UNO (w1c), no cero.
    static constexpr uint32_t S_DMAUDR1 = 1u << 13, S_DMAUDR2 = 1u << 29;

    DacBase(sc_core::sc_module_name nm, const DacCaps& caps = CAPS_DAC_F407)
        : BusSlave(nm, addr::DAC_B, 0x400), trig("trig", 8), caps_(caps) {
        SC_HAS_PROCESS(DacBase);
        SC_THREAD(out_proc0);
        SC_THREAD(out_proc1);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(trig_proc);
        for (unsigned i = 0; i < 8; ++i) sensitive << trig[i];
        dont_initialize();
    }

    // Salida analógica de un canal (elaboración; lo llama el top): PA4 y PA5.
    void bind_out(unsigned ch, analog_net_if& net) {
        if (ch >= N_CH) return;
        net_[ch] = &net;
        id_[ch] = net.register_driver(ch == 0 ? "dac1" : "dac2");
        net.set_hiz(id_[ch]);
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    const DacCaps& caps() const { return caps_; }
    uint32_t dor(unsigned ch) const { return ch < N_CH ? ch_[ch].dor : 0u; }
    uint64_t updates(unsigned ch) const { return ch < N_CH ? ch_[ch].n_upd : 0u; }
    // Tensión que el bloque está imponiendo, antes de que la resuelva el nodo.
    double   drive_volts(unsigned ch) const { return ch < N_CH ? ch_[ch].v_drv : 0.0; }

protected:
    DacCaps caps_;

    struct Channel {
        uint32_t dhr = 0;          // dato pendiente (siempre a 12 bits)
        uint32_t dor = 0;          // dato en la salida
        uint32_t lfsr = 0x0AAAu;   // generador de ruido [IR, §12.14.2-B]
        unsigned tri = 0;          // contador del triángulo
        bool     tri_up = true;
        bool     trig_prev = false;
        bool     drq = false;      // petición de DMA (nivel)
        bool     dma_pending = false;
        double   v_drv = 0.0;      // tensión aplicada al nodo
        uint64_t n_upd = 0;
        sc_core::sc_event ev;      // «hay un DOR nuevo que sacar»
    };
    std::array<Channel, N_CH> ch_{};
    analog_net_if* net_[N_CH] = {nullptr, nullptr};
    int id_[N_CH] = {-1, -1};

    uint32_t cr_ = 0, sr_ = 0;
    bool o_irq_ = false;
    sc_core::sc_event pub_ev_;

    // =======================================================================
    // Campos de CR. La simetría de los dos canales se aprovecha aquí: un solo
    // juego de accesores con el desplazamiento de 16 bits como parámetro.
    // =======================================================================
    static unsigned sh(unsigned c) { return 16u * c; }
    bool     en(unsigned c)    const { return (cr_ >> (sh(c) + 0)) & 1u; }
    bool     boff(unsigned c)  const { return (cr_ >> (sh(c) + 1)) & 1u; }
    bool     ten(unsigned c)   const { return (cr_ >> (sh(c) + 2)) & 1u; }
    unsigned tsel(unsigned c)  const { return (cr_ >> (sh(c) + 3)) & 7u; }
    unsigned wave(unsigned c)  const { return (cr_ >> (sh(c) + 6)) & 3u; }
    unsigned mamp(unsigned c)  const { return (cr_ >> (sh(c) + 8)) & 0xFu; }
    bool     dmaen(unsigned c) const { return (cr_ >> (sh(c) + 12)) & 1u; }
    bool     udrie(unsigned c) const { return (cr_ >> (sh(c) + 13)) & 1u; }
    static uint32_t udr_bit(unsigned c) { return 1u << (sh(c) + 13); }

    unsigned full_scale() const { return (1u << caps_.bits) - 1u; }

    // =======================================================================
    // Máscaras de escritura: aquí es donde ACTÚAN los rasgos.
    // =======================================================================
    uint32_t cr_mask() const {
        uint32_t one = 1u;                                   // ENx siempre
        if (caps_.buffer)   one |= 1u << 1;                  // BOFFx
        if (caps_.trigger)  one |= (1u << 2) | (7u << 3);    // TENx, TSELx
        if (caps_.noise || caps_.triangle) {
            one |= 3u << 6;                                  // WAVEx
            one |= 0xFu << 8;                                // MAMPx
        }
        if (caps_.dma)      one |= (1u << 12) | (1u << 13);  // DMAENx, DMAUDRIEx
        uint32_t m = one;
        if (caps_.n_channels > 1) m |= one << 16;
        return m;
    }
    uint32_t sr_mask() const {
        if (!caps_.dma) return 0;
        uint32_t m = S_DMAUDR1;
        if (caps_.n_channels > 1) m |= S_DMAUDR2;
        return m;
    }
    uint32_t swtrig_mask() const {
        if (!caps_.trigger) return 0;
        return caps_.n_channels > 1 ? 3u : 1u;
    }

    // =======================================================================
    // Banco de registros
    // =======================================================================
    bool has_ch(unsigned c) const { return c < caps_.n_channels; }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return cr_;
            case R_SR:  return sr_;
            case R_DOR1: return has_ch(0) ? ch_[0].dor : 0u;
            case R_DOR2: return has_ch(1) ? ch_[1].dor : 0u;
            // Los registros de datos son de lectura/escritura y devuelven lo
            // último cargado, en el formato con el que se pregunta.
            case R_DHR12R1: return has_ch(0) ? ch_[0].dhr : 0u;
            case R_DHR12R2: return has_ch(1) ? ch_[1].dhr : 0u;
            case R_DHR12L1: return (has_ch(0) && caps_.fmt_left) ? (ch_[0].dhr << 4) : 0u;
            case R_DHR12L2: return (has_ch(1) && caps_.fmt_left) ? (ch_[1].dhr << 4) : 0u;
            case R_DHR8R1:  return (has_ch(0) && caps_.fmt_8bit) ? (ch_[0].dhr >> 4) : 0u;
            case R_DHR8R2:  return (has_ch(1) && caps_.fmt_8bit) ? (ch_[1].dhr >> 4) : 0u;
            case R_DHR12RD: return caps_.dual ? (ch_[0].dhr | (ch_[1].dhr << 16)) : 0u;
            case R_DHR12LD: return caps_.dual ? ((ch_[0].dhr << 4) | (ch_[1].dhr << 20)) : 0u;
            case R_DHR8RD:  return caps_.dual ? ((ch_[0].dhr >> 4) | ((ch_[1].dhr >> 4) << 8)) : 0u;
            case R_SWTRIGR: return 0;                    // solo escritura
            default: return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                // acceso parcial
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR: {
                const uint32_t old = cr_;
                cr_ = v & cr_mask();
                for (unsigned c = 0; c < caps_.n_channels; ++c) {
                    const bool was = (old >> (sh(c) + 0)) & 1u;
                    if (!was && en(c)) { ch_[c].n_upd = 0; load_dor(c); }
                    if (was && !en(c)) shut_channel(c);
                    // Cambiar BOFF cambia la ETAPA DE SALIDA, no el dato: hay
                    // que volver a aplicarla aunque DOR no se haya movido, o el
                    // pin se quedaría con la impedancia y el recorrido de la
                    // configuración anterior.
                    else if (en(c) && ((old ^ cr_) & (1u << (sh(c) + 1))))
                        ch_[c].ev.notify(sc_core::SC_ZERO_TIME);
                    // Deshabilitar el DMA retira la petición pendiente
                    if (!dmaen(c) && ch_[c].drq) { ch_[c].drq = false; publish(); }
                }
                update_irq();
                return;
            }
            case R_SWTRIGR: {
                // Disparo por software. Es un registro de solo escritura: el
                // bit no se guarda, dispara y desaparece [IR, §12.14.2-C].
                const uint32_t t = v & swtrig_mask();
                for (unsigned c = 0; c < caps_.n_channels; ++c)
                    if ((t >> c) & 1u) fire(c, /*from_sw=*/true);
                return;
            }
            case R_DHR12R1: set_dhr(0, v & 0x0FFFu); return;
            case R_DHR12R2: set_dhr(1, v & 0x0FFFu); return;
            case R_DHR12L1: if (caps_.fmt_left) set_dhr(0, (v >> 4) & 0x0FFFu); return;
            case R_DHR12L2: if (caps_.fmt_left) set_dhr(1, (v >> 4) & 0x0FFFu); return;
            case R_DHR8R1:  if (caps_.fmt_8bit) set_dhr(0, (v & 0xFFu) << 4); return;
            case R_DHR8R2:  if (caps_.fmt_8bit) set_dhr(1, (v & 0xFFu) << 4); return;
            // Registros DUALES: una sola escritura carga los dos canales, que
            // es lo que permite actualizarlos en el MISMO instante.
            case R_DHR12RD: if (caps_.dual) { set_dhr(0, v & 0x0FFFu);
                                              set_dhr(1, (v >> 16) & 0x0FFFu); } return;
            case R_DHR12LD: if (caps_.dual) { set_dhr(0, (v >> 4) & 0x0FFFu);
                                              set_dhr(1, (v >> 20) & 0x0FFFu); } return;
            case R_DHR8RD:  if (caps_.dual) { set_dhr(0, (v & 0xFFu) << 4);
                                              set_dhr(1, ((v >> 8) & 0xFFu) << 4); } return;
            case R_SR:
                // DMAUDRx se borra escribiendo UNO, no cero: es de las pocas
                // banderas w1c del dispositivo [IR, §12.14.2].
                sr_ &= ~(v & sr_mask());
                update_irq();
                return;
            default: return;
        }
    }
    unsigned access_cycles(bool) const override { return 1; }

    // =======================================================================
    // Camino del dato: DHR -> DOR -> pin
    // =======================================================================
    void set_dhr(unsigned c, uint32_t v) {
        if (!has_ch(c)) return;
        ch_[c].dhr = v;
        // Sin disparo habilitado, el dato pasa a DOR en un ciclo de APB1; con
        // TEN = 1 se queda esperando al disparo [IR, §12.14.1].
        if (!ten(c) || !caps_.trigger) { if (en(c)) load_dor(c); }
        // Escribir el dato es lo que atiende la petición de DMA pendiente.
        if (ch_[c].dma_pending) {
            ch_[c].dma_pending = false;
            if (ch_[c].drq) { ch_[c].drq = false; publish(); }
        }
    }

    // Un disparo: mueve DHR (más la onda) a DOR y pide el dato siguiente.
    void fire(unsigned c, bool from_sw) {
        if (!has_ch(c) || !en(c)) return;
        if (!caps_.trigger) return;
        // Con TEN = 0 el canal no atiende disparos; el software es la fuente
        // TSEL = 111, así que solo cuenta si esa es la seleccionada.
        if (!ten(c)) return;
        if (from_sw && tsel(c) != 7u) return;
        advance_wave(c);
        load_dor(c);
        if (caps_.dma && dmaen(c)) {
            // Desbordamiento: llega un disparo y el DMA todavía no ha servido
            // el dato anterior [IR, §12.14.2]. El silicio marca DMAUDRx y deja
            // de pedir para ese canal.
            if (ch_[c].dma_pending) {
                sr_ |= udr_bit(c);
                ch_[c].dma_pending = false;
                if (ch_[c].drq) { ch_[c].drq = false; publish(); }
                update_irq();
            } else {
                ch_[c].dma_pending = true;
                ch_[c].drq = true;
                publish();
            }
        }
    }

    // Valor que sale de verdad: el dato más la onda, saturado a fondo de escala.
    uint32_t out_value(unsigned c) const {
        uint32_t v = ch_[c].dhr;
        const unsigned w = wave(c);
        if (w == 1u && caps_.noise)        v += (ch_[c].lfsr & amp_mask(c));
        else if (w >= 2u && caps_.triangle) v += (ch_[c].tri & amp_mask(c));
        const uint32_t fs = full_scale() << (12u - caps_.bits);
        return v > fs ? fs : v;
    }
    // Amplitud de MAMP: 2^(MAMP+1) - 1, saturada a 12 bits [IR, §12.14.2-B].
    uint32_t amp_mask(unsigned c) const {
        const unsigned m = mamp(c);
        return (m >= 11u) ? 0x0FFFu : ((1u << (m + 1u)) - 1u);
    }
    void advance_wave(unsigned c) {
        const unsigned w = wave(c);
        if (w == 1u && caps_.noise) {
            // LFSR de 12 bits, valor de arranque 0xAAA [IR, §12.14.2-B]. El
            // juego exacto de tomas no consta en las fuentes: se usa el
            // polinomio documentado x^12 + x^6 + x^4 + x + 1.
            const uint32_t l = ch_[c].lfsr;
            const uint32_t bit = ((l >> 0) ^ (l >> 1) ^ (l >> 3) ^ (l >> 5)) & 1u;
            ch_[c].lfsr = ((l >> 1) | (bit << 11)) & 0x0FFFu;
        } else if (w >= 2u && caps_.triangle) {
            const uint32_t top = amp_mask(c);
            if (ch_[c].tri_up) {
                if (ch_[c].tri >= top) { ch_[c].tri_up = false; if (ch_[c].tri) --ch_[c].tri; }
                else ++ch_[c].tri;
            } else {
                if (ch_[c].tri == 0) { ch_[c].tri_up = true; ++ch_[c].tri; }
                else --ch_[c].tri;
            }
        }
    }

    void load_dor(unsigned c) {
        const uint32_t v = out_value(c);
        if (v == ch_[c].dor && ch_[c].n_upd != 0) return;
        ch_[c].dor = v;
        ++ch_[c].n_upd;
        ch_[c].ev.notify(sc_core::SC_ZERO_TIME);
    }
    void shut_channel(unsigned c) {
        ch_[c].dor = 0;
        ch_[c].dma_pending = false;
        if (ch_[c].drq) { ch_[c].drq = false; publish(); }
        ch_[c].ev.notify(sc_core::SC_ZERO_TIME);
    }

    // =======================================================================
    // Lado analógico: un hilo por canal, que es además el ÚNICO escritor de su
    // nodo. La latencia de estabilización se modela como retardo de transporte:
    // el pin conserva el valor anterior hasta que vence t_SETTLING y entonces
    // toma el nuevo [IR, §12.14; nota del plan sobre t_SETTLING].
    // =======================================================================
    void out_proc0() { out_proc(0); }
    void out_proc1() { out_proc(1); }
    void out_proc(unsigned c) {
        for (;;) {
            wait(ch_[c].ev | rst_n.value_changed_event());
            if (!net_[c] || !has_ch(c)) continue;
            if (!en(c) || !clock_enabled() || !rst_n.read()) {
                net_[c]->set_hiz(id_[c]);            // canal apagado: pin libre
                ch_[c].v_drv = 0.0;
                continue;
            }
            wait(sc_core::sc_time(caps_.t_settle_us, sc_core::SC_US));
            if (!en(c)) { net_[c]->set_hiz(id_[c]); ch_[c].v_drv = 0.0; continue; }
            apply_out(c);
        }
    }
    void apply_out(unsigned c) {
        const double vr = (vref.read() > 0.5) ? vref.read() : 3.3;
        const uint32_t fs = full_scale();
        const uint32_t code = ch_[c].dor >> (12u - caps_.bits);
        double v = double(code) / double(fs) * vr;
        double r = caps_.r_open;
        if (caps_.buffer && !boff(c)) {
            // Con el amplificador puesto, la salida tiene impedancia baja pero
            // NO llega a los raíles: se queda a v_margin de cada extremo. Es
            // una limitación real del seguidor, no del convertidor.
            r = caps_.r_buffered;
            const double lo = caps_.v_margin, hi = vr - caps_.v_margin;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
        }
        ch_[c].v_drv = v;
        net_[c]->set_drive(id_[c], float(v), float(r));
    }

    // =======================================================================
    // Disparos externos, interrupción y publicación
    // =======================================================================
    void trig_proc() {
        if (!caps_.trigger) return;
        for (unsigned c = 0; c < caps_.n_channels; ++c) {
            const unsigned s = tsel(c);
            const bool now = trig[s].read();
            // La fuente 111 es el software: no llega por el vector de entrada.
            if (s != 7u && !ch_[c].trig_prev && now) fire(c, /*from_sw=*/false);
            ch_[c].trig_prev = now;
        }
    }
    void update_irq() {
        bool i = false;
        for (unsigned c = 0; c < caps_.n_channels; ++c)
            if ((sr_ & udr_bit(c)) && udrie(c)) i = true;
        if (i != o_irq_) { o_irq_ = i; publish(); }
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq.write(o_irq_);
        dma_req_ch1.write(ch_[0].drq);
        dma_req_ch2.write(ch_[1].drq);
    }
    void reset_proc() {
        if (rst_n.read()) return;
        cr_ = 0; sr_ = 0; o_irq_ = false;
        for (unsigned c = 0; c < N_CH; ++c) {
            ch_[c].dhr = 0; ch_[c].dor = 0;
            ch_[c].lfsr = 0x0AAAu; ch_[c].tri = 0; ch_[c].tri_up = true;
            ch_[c].trig_prev = false; ch_[c].drq = false;
            ch_[c].dma_pending = false; ch_[c].v_drv = 0.0; ch_[c].n_upd = 0;
            if (net_[c]) net_[c]->set_hiz(id_[c]);
        }
        publish();
    }
};

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN. Cada combinación de rasgos es un tipo
// distinto, así que confundir variantes es un error de compilación.
// ---------------------------------------------------------------------------
template <const DacCaps& C>
class DacT : public DacBase {
public:
    explicit DacT(sc_core::sc_module_name nm) : DacBase(nm, C) {
        static_assert(C.n_channels >= 1 && C.n_channels <= 2,
                      "el bloque DAC tiene uno o dos canales");
        static_assert(C.bits >= 8 && C.bits <= 12, "el dato cabe en DHR de 12 bits");
        static_assert(!C.dual || C.n_channels == 2,
                      "los registros duales exigen los dos canales");
    }
    static constexpr unsigned channels() { return C.n_channels; }
    static constexpr unsigned bits()     { return C.bits; }
};

// El STM32F407VG: dos canales de 12 bits con todo [IR, §12.14.1].
using Dac      = DacT<CAPS_DAC_F407>;
using Dac1Ch   = DacT<CAPS_DAC_1CH>;
using DacBasic = DacT<CAPS_DAC_BASIC>;

} // namespace stm32
#endif // STM32_PERIPH_DAC_H
