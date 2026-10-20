// =============================================================================
// exti.h — Controlador EXTI (APB2; plan P2: fuera del SCB) [IR, §9.4]
//
// 23 líneas de interrupción/evento:
//   0-15  desde los pines GPIO. Qué puerto llega a cada línea lo deciden los
//         registros SYSCFG_EXTICR1-4, que entran por `exticr_sel` [IR, §9.4.3];
//   16    PVD (detector de tensión del PWR)          -> IRQ 1
//   17    RTC Alarm                                  -> IRQ 41
//   18    USB OTG FS Wakeup                          -> IRQ 42
//   19    Ethernet Wakeup                            -> IRQ 62
//   20    USB OTG HS Wakeup                          -> IRQ 76
//   21    RTC Tamper / Timestamp                     -> IRQ 2
//   22    RTC Wakeup                                 -> IRQ 3
// Las líneas 0-4 tienen vector propio (IRQ 6-10); 5-9 comparten el 23 y 10-15
// el 40 [IR, §9.1.2].
//
// Fase F4 — implementado:
//   * banco de registros IMR/EMR/RTSR/FTSR/SWIER/PR [IR, §9.4.2], con PR de
//     tipo rc_w1 (se borra escribiendo un uno);
//   * detector de flanco por línea, de subida (RTSR), de bajada (FTSR) o de
//     ambos, sobre la fuente que selecciona SYSCFG_EXTICR en cada instante;
//   * camino de INTERRUPCIÓN (PR & IMR -> NVIC, con la agrupación 9_5 y 15_10)
//     y camino de EVENTO independiente (EMR -> pulso hacia el núcleo, sin pasar
//     por el NVIC), que es lo que despierta a la CPU de un WFE;
//   * interrupción/evento por software con SWIER;
//   * salida de despertar hacia el PWR para la salida del modo Stop [IR, §14].
//
// El EXTI es ASÍNCRONO: detecta pulsos más cortos que un ciclo de reloj y, por
// eso mismo, sigue funcionando con los relojes parados. En el modelo esto sale
// gratis: el detector es un SC_METHOD sensible a las señales de pin, no a un
// flanco de reloj. El bloque tampoco tiene bit de habilitación en el RCC —el
// que hace falta es el de SYSCFG, y solo para tocar los EXTICR—, así que el top
// le ata `clk_en` a uno.
// =============================================================================
#ifndef STM32_PERIPH_EXTI_H
#define STM32_PERIPH_EXTI_H

#include "../common/periph_base.h"
#include "../pins/pin_mux.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos del EXTI (fase S0 del F303) [doc/analisis_stm32f303.md, §3.2]
//
// El F4 tiene 23 líneas en una sola tanda de registros. El F303 tiene 36, y
// las de la 32 a la 35 viven en una SEGUNDA tanda (IMR2, EMR2, RTSR2, FTSR2,
// SWIER2 y PR2, de 0x20 a 0x34) `[CMSIS]`. Y trae una clase de línea que el F4
// no tiene: las DIRECTAS (I2C, USART y UART que despiertan de Stop), sin
// detector de flanco ni bit de PR: solo enmascaran el despertar, y la
// interrupción es la del propio periférico `[CMSIS: comentarios de IRQn]`.
//
//   base          dirección del bloque
//   n_lineas      23 en el F4, 36 en el F303 (hasta 64)
//   configurables las que tienen flanco y PR; el resto hasta n_lineas son
//                 directas o están reservadas
//   directas      las que solo despiertan
//   imr_reset     IMR al salir de reset: en el F303 las directas vienen
//                 desenmascaradas [1F: RM0316, EXTI_IMR1 = 0x1F80_0000]
//   genericas     las líneas 16 en adelante entran y salen por los vectores
//                 `linea_in`/`linea_irq` en vez de por los puertos con nombre
//                 del F4 (PVD, RTC, OTG, ETH), que en el F303 son otra cosa
// ---------------------------------------------------------------------------
struct ExtiCaps {
    uint32_t base;
    unsigned n_lineas;
    uint64_t configurables;
    uint64_t directas;
    uint64_t imr_reset;
    bool     genericas;
};
inline constexpr ExtiCaps EXTI_F4{ addr::EXTI_B, 23, 0x007FFFFFull, 0, 0, false };
// F303xB/C [CMSIS: stm32f303xc.h]: 0-15 pines, 16 PVD, 17 alarma del RTC,
// 18 USB, 19 tamper y marca de tiempo, 20 despertar del RTC, 21-22 COMP1-2,
// 23-26 I2C1, I2C2, USART1, USART2 (directas), 27 reservada, 28 USART3
// (directa), 29-33 COMP3-7, 34-35 UART4-5 (directas).
inline constexpr ExtiCaps EXTI_F303{
    0x40010400u, 36,
    0x007FFFFFull | (0x1Full << 29),
    (0xFull << 23) | (1ull << 28) | (3ull << 34),
    (0xFull << 23) | (1ull << 28) | (3ull << 34) | (1ull << 27),
    true };

class Exti : public BusSlave {
public:
    // ---- Entradas ---------------------------------------------------------
    // Pines: las 16 líneas de cada uno de los 9 puertos.
    sc_core::sc_vector<sc_core::sc_in<bool>> gpio_line;     // [9*16]
    // Selección de puerto por línea 0-15 (SYSCFG_EXTICRx) [IR, §9.4.3]
    sc_core::sc_vector<sc_core::sc_in<uint8_t>> exticr_sel; // [16] 0=A..8=I
    // Líneas internas 16..22
    sc_core::sc_in<bool> l16_pvd{"l16_pvd"}, l17_rtc_alarm{"l17_rtc_alarm"},
                         l18_otgfs_wkup{"l18_otgfs_wkup"}, l19_eth_wkup{"l19_eth_wkup"},
                         l20_otghs_wkup{"l20_otghs_wkup"}, l21_rtc_tamp{"l21_rtc_tamp"},
                         l22_rtc_wkup{"l22_rtc_wkup"};

    // ---- Salidas de interrupción hacia el NVIC ----------------------------
    sc_core::sc_out<bool> irq_exti0{"irq_exti0"}, irq_exti1{"irq_exti1"},
                          irq_exti2{"irq_exti2"}, irq_exti3{"irq_exti3"},
                          irq_exti4{"irq_exti4"}, irq_exti9_5{"irq_exti9_5"},
                          irq_exti15_10{"irq_exti15_10"};
    sc_core::sc_out<bool> irq_pvd{"irq_pvd"}, irq_rtc_alarm{"irq_rtc_alarm"},
                          irq_otgfs_wkup{"irq_otgfs_wkup"}, irq_eth_wkup{"irq_eth_wkup"},
                          irq_otghs_wkup{"irq_otghs_wkup"}, irq_tamp{"irq_tamp"},
                          irq_rtc_wkup{"irq_rtc_wkup"};
    // Eventos y wakeup (WFE / salida de Stop) [IR, §14]
    sc_core::sc_out<bool> event_out{"event_out"};
    sc_core::sc_out<bool> wakeup{"wakeup"};

    // Las líneas 16 en adelante de un EXTI con `genericas` (F303): la entrada
    // de cada una y su salida hacia el NVIC. En el F4 están vacíos.
    sc_core::sc_vector<sc_core::sc_in<bool>>  linea_in;      // [n_lineas - 16]
    sc_core::sc_vector<sc_core::sc_out<bool>> linea_irq;     // [n_lineas - 16]

    // ---- Registros [IR, §9.4.2] -------------------------------------------
    enum : uint32_t { R_IMR = 0x00, R_EMR = 0x04, R_RTSR = 0x08,
                      R_FTSR = 0x0C, R_SWIER = 0x10, R_PR = 0x14,
                      // La segunda tanda (F303) [CMSIS]
                      R_IMR2 = 0x20, R_EMR2 = 0x24, R_RTSR2 = 0x28,
                      R_FTSR2 = 0x2C, R_SWIER2 = 0x30, R_PR2 = 0x34 };
    static constexpr unsigned N_LINES   = 23;               // las del F4
    static constexpr uint32_t LINE_MASK = 0x007FFFFFu;      // 23 líneas
    static constexpr unsigned MAX_LINEAS = 64;

    Exti(sc_core::sc_module_name nm, const ExtiCaps& caps = EXTI_F4)
        : BusSlave(nm, caps.base, 0x400),
          gpio_line("gpio_line", N_GPIO_PORTS * N_PORT_PINS),
          exticr_sel("exticr_sel", 16),
          linea_in("linea_in", caps.genericas ? caps.n_lineas - 16 : 0),
          linea_irq("linea_irq", caps.genericas ? caps.n_lineas - 16 : 0),
          caps_(caps), n_(caps.n_lineas > MAX_LINEAS ? MAX_LINEAS : caps.n_lineas),
          implementadas_(caps.configurables | caps.directas),
          imr_(caps.imr_reset) {
        SC_HAS_PROCESS(Exti);
        // Con líneas genéricas, los puertos con nombre del F4 no significan
        // nada: se atan aquí dentro, a un cero y a un sumidero, para que el
        // SoC no tenga que saber que existen.
        if (caps_.genericas) {
            sc_core::sc_in<bool>* in[7] = {&l16_pvd, &l17_rtc_alarm, &l18_otgfs_wkup,
                                           &l19_eth_wkup, &l20_otghs_wkup,
                                           &l21_rtc_tamp, &l22_rtc_wkup};
            for (auto* p : in) (*p)(s_cero_);
            sc_core::sc_out<bool>* out[7] = {&irq_pvd, &irq_rtc_alarm, &irq_otgfs_wkup,
                                             &irq_eth_wkup, &irq_otghs_wkup, &irq_tamp,
                                             &irq_rtc_wkup};
            for (unsigned i = 0; i < 7; ++i) (*out[i])(s_nc_[i]);
        }
        // El detector de flanco. Deliberadamente SIN dont_initialize(): la
        // primera activación toma la foto de partida de los niveles, y como
        // RTSR y FTSR valen cero tras el reset no puede generar ningún flanco.
        SC_METHOD(line_proc);
        for (unsigned i = 0; i < gpio_line.size(); ++i) sensitive << gpio_line[i];
        for (unsigned i = 0; i < exticr_sel.size(); ++i) sensitive << exticr_sel[i];
        sensitive << l16_pvd << l17_rtc_alarm << l18_otgfs_wkup << l19_eth_wkup
                  << l20_otghs_wkup << l21_rtc_tamp << l22_rtc_wkup;
        for (unsigned i = 0; i < linea_in.size(); ++i) sensitive << linea_in[i];
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;   dont_initialize();
        SC_THREAD(event_proc);
    }

    const ExtiCaps& caps() const { return caps_; }

    // ---- Observación desde el banco de pruebas ----------------------------
    // Las 32 primeras, como siempre; `pending64` y `levels64`, todas.
    uint32_t pending()      const { return uint32_t(pr_); }
    uint32_t line_levels()  const { return uint32_t(lvl_bits_); }
    uint64_t pending64()    const { return pr_; }
    uint64_t levels64()     const { return lvl_bits_; }
    uint64_t event_pulses() const { return n_evt_; }
    // Fuente efectiva de una línea 0-15: puerto seleccionado por EXTICR.
    unsigned source_port(unsigned line) const {
        return (line < 16) ? unsigned(exticr_sel[line].read()) : 0xFFu;
    }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    // La tanda (0 o 1) y el registro dentro de ella, o -1 si no es de nadie.
    // La segunda tanda solo existe si hay más de 32 líneas.
    int tanda_de(uint32_t off, uint32_t& reg) const {
        if (off <= R_PR) { reg = off; return 0; }
        if (n_ > 32 && off >= R_IMR2 && off <= R_PR2) { reg = off - R_IMR2; return 1; }
        return -1;
    }
    static uint32_t mitad(uint64_t v, int t) { return uint32_t(v >> (32 * t)); }

    uint32_t reg_read(uint32_t off) override {
        uint32_t r = 0;
        const int t = tanda_de(off, r);
        if (t < 0) return 0;
        switch (r) {
            // Los bits de IMR sin línea leen uno en el F303, que es lo que dice
            // su valor de reset (EXTI_IMR2 = 0xFFFF_FFFC) [1F: RM0316]. En el
            // F4 no hay ninguno: sus 23 líneas son todas configurables.
            case R_IMR:   return mitad(imr_ | (caps_.genericas ? ~implementadas_ : 0), t);
            case R_EMR:   return mitad(emr_, t);
            case R_RTSR:  return mitad(rtsr_, t);
            case R_FTSR:  return mitad(ftsr_, t);
            case R_SWIER: return mitad(swier_, t);
            case R_PR:    return mitad(pr_, t);
            default:      return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t r = 0;
        const int t = tanda_de(off, r);
        if (t < 0) return;
        if (be != 0xFu) {                                // acceso parcial
            const uint32_t cur = reg_read(off);
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        const unsigned sh = 32u * unsigned(t);
        const uint64_t tanda = 0xFFFFFFFFull << sh;
        // Lo que se puede escribir en cada registro: IMR y EMR, todas las
        // líneas que existen; los flancos, SWIER y PR, solo las configurables
        // (una directa no tiene detector ni petición pendiente).
        const uint64_t w = uint64_t(v) << sh;
        const uint64_t todas = implementadas_ & tanda;
        const uint64_t conf  = caps_.configurables & tanda;
        switch (r) {
            case R_IMR:   imr_  = (imr_  & ~todas) | (w & todas); break;
            case R_EMR:   emr_  = (emr_  & ~todas) | (w & todas); break;
            case R_RTSR:  rtsr_ = (rtsr_ & ~conf)  | (w & conf);  break;
            case R_FTSR:  ftsr_ = (ftsr_ & ~conf)  | (w & conf);  break;
            case R_SWIER: write_swier(w & conf); break;
            case R_PR:
                // rc_w1: escribir un uno borra la petición pendiente. Borrar PR
                // borra también el bit de SWIER que la produjo [IR, §9.4.2].
                pr_    &= ~(w & conf);
                swier_ &= ~(w & conf);
                break;
            default: return;
        }
        update_out();
    }

private:
    ExtiCaps caps_;
    unsigned n_;                          // líneas de verdad
    uint64_t implementadas_;              // configurables | directas
    // ---- Registros --------------------------------------------------------
    uint64_t imr_ = 0, emr_ = 0, rtsr_ = 0, ftsr_ = 0, swier_ = 0, pr_ = 0;
    // ---- Estado del detector de flanco ------------------------------------
    bool     lvl_[MAX_LINEAS] = {};       // último nivel visto en cada línea
    uint64_t lvl_bits_ = 0;               // el mismo, empaquetado (observación)
    uint8_t  sel_[16] = {};               // última selección de puerto vista
    bool     sel_init_ = false;
    uint64_t n_evt_ = 0;
    // ---- Salidas publicadas por un único proceso --------------------------
    bool o_irq_[7] = {};                  // 0-4, 9_5, 15_10
    bool o_int_[7] = {};                  // pvd, rtc_alarm, fs, eth, hs, tamp, rtcwk
    bool o_lin_[MAX_LINEAS - 16] = {};    // genéricas: la salida de la línea 16+i
    bool o_evt_ = false, o_wkup_ = false;
    sc_core::sc_event pub_ev_, evt_ev_;
    // Lo que atan los puertos con nombre cuando las líneas son genéricas
    sc_core::sc_signal<bool> s_cero_{"s_cero"};
    sc_core::sc_signal<bool> s_nc_[7];

    // =======================================================================
    // Fuente de cada línea
    // =======================================================================
    // Para las líneas 0-15, el multiplexor de SYSCFG. Un selector fuera de
    // rango (0x9..0xF, reservado en [IR, §9.4.3]) no conecta nada: la línea se
    // queda a cero, que es lo que hace un multiplexor sin entrada seleccionada.
    bool line_level(unsigned l) const {
        if (l < 16) {
            const unsigned p = unsigned(exticr_sel[l].read());
            if (p >= N_GPIO_PORTS) return false;
            return gpio_line[p * N_PORT_PINS + l].read();
        }
        if (caps_.genericas) return l - 16 < linea_in.size() && linea_in[l - 16].read();
        switch (l) {
            case 16: return l16_pvd.read();
            case 17: return l17_rtc_alarm.read();
            case 18: return l18_otgfs_wkup.read();
            case 19: return l19_eth_wkup.read();
            case 20: return l20_otghs_wkup.read();
            case 21: return l21_rtc_tamp.read();
            default: return l22_rtc_wkup.read();
        }
    }

    // =======================================================================
    // Detector de flanco
    // =======================================================================
    void line_proc() {
        uint64_t trig = 0;
        for (unsigned l = 0; l < n_; ++l) {
            // Cambiar SYSCFG_EXTICR cambia la FUENTE de la línea, no su valor:
            // el modelo resincroniza el nivel sin generar flanco, de modo que
            // reconfigurar el multiplexor con los dos puertos a niveles
            // distintos no deje una petición espuria pendiente. Es una decisión
            // deliberada; véase el informe de la fase.
            bool resync = !sel_init_;
            if (l < 16) {
                const uint8_t s = exticr_sel[l].read();
                if (s != sel_[l]) { sel_[l] = s; resync = true; }
            }
            const bool lv = line_level(l);
            if (lv != lvl_[l]) {
                if (!resync) {
                    if (lv  && ((rtsr_ >> l) & 1u)) trig |= 1ull << l;   // subida
                    if (!lv && ((ftsr_ >> l) & 1u)) trig |= 1ull << l;   // bajada
                }
                lvl_[l] = lv;
            }
        }
        sel_init_ = true;
        lvl_bits_ = pack_levels();
        if (trig) fire(trig);
        else      update_out();
    }

    uint64_t pack_levels() const {
        uint64_t b = 0;
        for (unsigned l = 0; l < n_; ++l) if (lvl_[l]) b |= 1ull << l;
        return b;
    }

    // Un flanco seleccionado levanta la petición pendiente y, si la línea está
    // desenmascarada como evento, dispara el pulso hacia el núcleo. Los dos
    // caminos son INDEPENDIENTES: una línea puede ser solo evento (EMR sin IMR)
    // y entonces despierta de un WFE sin llegar nunca al NVIC.
    void fire(uint64_t trig) {
        pr_ |= trig;
        if (trig & emr_) pulse_event();
        update_out();
    }

    // Interrupción/evento por software. [IR, §9.4.2] describe SWIER como el
    // registro que "dispara IRQ por SW"; siguiendo la descripción del bit en el
    // manual de referencia, la petición solo se levanta si la línea está
    // desenmascarada (IMR o EMR). El bit permanece a uno hasta que se borra el
    // bit correspondiente de PR.
    void write_swier(uint64_t v) {
        const uint64_t set = v & ~swier_;        // flanco 0 -> 1 de cada bit
        swier_ |= v;
        const uint64_t en = set & (imr_ | emr_);
        if (!en) return;
        pr_ |= en & imr_;
        if (en & emr_) pulse_event();
    }

    // =======================================================================
    // Salidas
    // =======================================================================
    void update_out() {
        const uint64_t act = pr_ & imr_;
        for (unsigned l = 0; l < 5; ++l) o_irq_[l] = (act >> l) & 1u;
        o_irq_[5] = (act & 0x000003E0u) != 0;            // líneas 5-9   -> IRQ 23
        o_irq_[6] = (act & 0x0000FC00u) != 0;            // líneas 10-15 -> IRQ 40
        for (unsigned i = 0; i < 7; ++i) o_int_[i] = !caps_.genericas && ((act >> (16 + i)) & 1u);
        if (caps_.genericas)
            for (unsigned l = 16; l < n_; ++l) o_lin_[l - 16] = (act >> l) & 1u;
        // Una línea directa no tiene PR: despierta mientras su periférico la
        // tenga levantada y IMR la deje pasar.
        const uint64_t dir = caps_.directas & imr_ & lvl_bits_;
        // Despertar del modo Stop: cualquier petición desenmascarada, sea por el
        // camino de interrupción o por el de evento [IR, §14].
        o_wkup_ = (act != 0) || dir != 0 || o_evt_;
        publish();
    }

    // El evento es un PULSO, no un nivel: el núcleo lo usa para armar su
    // registro de evento y salir del WFE. Dura un ciclo del bus del bloque.
    void pulse_event() { ++n_evt_; evt_ev_.notify(sc_core::SC_ZERO_TIME); }
    void event_proc() {
        for (;;) {
            wait(evt_ev_);
            o_evt_ = true; o_wkup_ = true; publish();
            const double hz = domain_hz();
            wait(hz > 0.0 ? sc_core::sc_time(1.0e12 / hz, sc_core::SC_PS)
                          : sc_core::sc_time(10, sc_core::SC_NS));
            o_evt_ = false;
            update_out();
        }
    }

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq_exti0.write(o_irq_[0]); irq_exti1.write(o_irq_[1]);
        irq_exti2.write(o_irq_[2]); irq_exti3.write(o_irq_[3]);
        irq_exti4.write(o_irq_[4]);
        irq_exti9_5.write(o_irq_[5]); irq_exti15_10.write(o_irq_[6]);
        irq_pvd.write(o_int_[0]);          irq_rtc_alarm.write(o_int_[1]);
        irq_otgfs_wkup.write(o_int_[2]);   irq_eth_wkup.write(o_int_[3]);
        irq_otghs_wkup.write(o_int_[4]);   irq_tamp.write(o_int_[5]);
        irq_rtc_wkup.write(o_int_[6]);
        for (unsigned i = 0; i < linea_irq.size(); ++i) linea_irq[i].write(o_lin_[i]);
        event_out.write(o_evt_);
        wakeup.write(o_wkup_);
    }

    void reset_proc() {
        if (rst_n.read()) return;
        emr_ = rtsr_ = ftsr_ = swier_ = pr_ = 0;
        imr_ = caps_.imr_reset;
        n_evt_ = 0;
        o_evt_ = false;
        // Los niveles se vuelven a fotografiar sin generar flancos.
        for (unsigned l = 0; l < n_; ++l) lvl_[l] = line_level(l);
        lvl_bits_ = pack_levels();
        update_out();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_EXTI_H
