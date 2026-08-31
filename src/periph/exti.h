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

    // ---- Registros [IR, §9.4.2] -------------------------------------------
    enum : uint32_t { R_IMR = 0x00, R_EMR = 0x04, R_RTSR = 0x08,
                      R_FTSR = 0x0C, R_SWIER = 0x10, R_PR = 0x14 };
    static constexpr unsigned N_LINES   = 23;
    static constexpr uint32_t LINE_MASK = 0x007FFFFFu;      // 23 líneas

    Exti(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::EXTI_B, 0x400),
          gpio_line("gpio_line", N_GPIO_PORTS * N_PORT_PINS),
          exticr_sel("exticr_sel", 16) {
        SC_HAS_PROCESS(Exti);
        // El detector de flanco. Deliberadamente SIN dont_initialize(): la
        // primera activación toma la foto de partida de los niveles, y como
        // RTSR y FTSR valen cero tras el reset no puede generar ningún flanco.
        SC_METHOD(line_proc);
        for (unsigned i = 0; i < gpio_line.size(); ++i) sensitive << gpio_line[i];
        for (unsigned i = 0; i < exticr_sel.size(); ++i) sensitive << exticr_sel[i];
        sensitive << l16_pvd << l17_rtc_alarm << l18_otgfs_wkup << l19_eth_wkup
                  << l20_otghs_wkup << l21_rtc_tamp << l22_rtc_wkup;
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;   dont_initialize();
        SC_THREAD(event_proc);
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    uint32_t pending()      const { return pr_; }
    uint32_t line_levels()  const { return lvl_bits_; }
    uint64_t event_pulses() const { return n_evt_; }
    // Fuente efectiva de una línea 0-15: puerto seleccionado por EXTICR.
    unsigned source_port(unsigned line) const {
        return (line < 16) ? unsigned(exticr_sel[line].read()) : 0xFFu;
    }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_IMR:   return imr_;
            case R_EMR:   return emr_;
            case R_RTSR:  return rtsr_;
            case R_FTSR:  return ftsr_;
            case R_SWIER: return swier_;
            case R_PR:    return pr_;
            default:      return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                // acceso parcial
            const uint32_t cur = reg_read(off);
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        v &= LINE_MASK;                                  // bits 31:23 reservados
        switch (off) {
            case R_IMR:   imr_  = v; break;
            case R_EMR:   emr_  = v; break;
            case R_RTSR:  rtsr_ = v; break;
            case R_FTSR:  ftsr_ = v; break;
            case R_SWIER: write_swier(v); break;
            case R_PR:
                // rc_w1: escribir un uno borra la petición pendiente. Borrar PR
                // borra también el bit de SWIER que la produjo [IR, §9.4.2].
                pr_    &= ~v;
                swier_ &= ~v;
                break;
            default: return;
        }
        update_out();
    }

private:
    // ---- Registros --------------------------------------------------------
    uint32_t imr_ = 0, emr_ = 0, rtsr_ = 0, ftsr_ = 0, swier_ = 0, pr_ = 0;
    // ---- Estado del detector de flanco ------------------------------------
    bool     lvl_[N_LINES] = {};          // último nivel visto en cada línea
    uint32_t lvl_bits_ = 0;               // el mismo, empaquetado (observación)
    uint8_t  sel_[16] = {};               // última selección de puerto vista
    bool     sel_init_ = false;
    uint64_t n_evt_ = 0;
    // ---- Salidas publicadas por un único proceso --------------------------
    bool o_irq_[7] = {};                  // 0-4, 9_5, 15_10
    bool o_int_[7] = {};                  // pvd, rtc_alarm, fs, eth, hs, tamp, rtcwk
    bool o_evt_ = false, o_wkup_ = false;
    sc_core::sc_event pub_ev_, evt_ev_;

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
        uint32_t trig = 0;
        for (unsigned l = 0; l < N_LINES; ++l) {
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
                    if (lv  && ((rtsr_ >> l) & 1u)) trig |= 1u << l;   // subida
                    if (!lv && ((ftsr_ >> l) & 1u)) trig |= 1u << l;   // bajada
                }
                lvl_[l] = lv;
            }
        }
        sel_init_ = true;
        lvl_bits_ = pack_levels();
        if (trig) fire(trig);
        else      update_out();
    }

    uint32_t pack_levels() const {
        uint32_t b = 0;
        for (unsigned l = 0; l < N_LINES; ++l) if (lvl_[l]) b |= 1u << l;
        return b;
    }

    // Un flanco seleccionado levanta la petición pendiente y, si la línea está
    // desenmascarada como evento, dispara el pulso hacia el núcleo. Los dos
    // caminos son INDEPENDIENTES: una línea puede ser solo evento (EMR sin IMR)
    // y entonces despierta de un WFE sin llegar nunca al NVIC.
    void fire(uint32_t trig) {
        pr_ |= trig;
        if (trig & emr_) pulse_event();
        update_out();
    }

    // Interrupción/evento por software. [IR, §9.4.2] describe SWIER como el
    // registro que "dispara IRQ por SW"; siguiendo la descripción del bit en el
    // manual de referencia, la petición solo se levanta si la línea está
    // desenmascarada (IMR o EMR). El bit permanece a uno hasta que se borra el
    // bit correspondiente de PR.
    void write_swier(uint32_t v) {
        const uint32_t set = v & ~swier_;        // flanco 0 -> 1 de cada bit
        swier_ |= v;
        const uint32_t en = set & (imr_ | emr_);
        if (!en) return;
        pr_ |= en & imr_;
        if (en & emr_) pulse_event();
    }

    // =======================================================================
    // Salidas
    // =======================================================================
    void update_out() {
        const uint32_t act = pr_ & imr_;
        for (unsigned l = 0; l < 5; ++l) o_irq_[l] = (act >> l) & 1u;
        o_irq_[5] = (act & 0x000003E0u) != 0;            // líneas 5-9   -> IRQ 23
        o_irq_[6] = (act & 0x0000FC00u) != 0;            // líneas 10-15 -> IRQ 40
        for (unsigned i = 0; i < 7; ++i) o_int_[i] = (act >> (16 + i)) & 1u;
        // Despertar del modo Stop: cualquier petición desenmascarada, sea por el
        // camino de interrupción o por el de evento [IR, §14].
        o_wkup_ = (act != 0) || o_evt_;
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
        event_out.write(o_evt_);
        wakeup.write(o_wkup_);
    }

    void reset_proc() {
        if (rst_n.read()) return;
        imr_ = emr_ = rtsr_ = ftsr_ = swier_ = pr_ = 0;
        n_evt_ = 0;
        o_evt_ = false;
        // Los niveles se vuelven a fotografiar sin generar flancos.
        for (unsigned l = 0; l < N_LINES; ++l) lvl_[l] = line_level(l);
        lvl_bits_ = pack_levels();
        update_out();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_EXTI_H
