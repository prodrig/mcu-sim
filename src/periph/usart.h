// =============================================================================
// usart.h — USART y UART [IR, §12.4]
//
// El STM32F407VG lleva seis interfaces serie del mismo bloque de diseño en dos
// variantes: cuatro USART completas (USART1/2/3/6) y dos UART reducidas
// (UART4/5), que no tienen modo síncrono, ni control de flujo por hardware, ni
// modo Smartcard. El modelo es UNO SOLO y la variante se selecciona con
// parámetros, de dos maneras equivalentes:
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using Usart = UsartT<CAPS_USART>;      // USART1/2/3/6
//         using Uart  = UsartT<CAPS_UART>;       // UART4/5
//         UsartT<CAPS_UART> uart4{"uart4", addr::UART4_B};
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor, útil para
//     barrer variantes desde un banco de pruebas o para modelar un derivado
//     con otra combinación de funciones:
//         UsartBase u{"u", base, UsartCaps{...}};
//         UsartBase v{"v", base, /*synchronous=*/false, /*flow_control=*/true};
//
// La diferencia no es cosmética: los rasgos determinan qué bits de CR2 y CR3
// son escribibles y qué recursos existen, de modo que en una UART los campos
// CLKEN/CPOL/CPHA/LBCL, CTSE/RTSE/CTSIE y SCEN/NACK quedan reservados y leen
// cero, igual que en el silicio, y el pin CK nunca se gobierna.
//
// Fase F4 — implementado:
//   * banco de registros SR/DR/BRR/CR1/CR2/CR3/GTPR [IR, §12.4.3];
//   * generador de baudios con divisor fraccionario y sobremuestreo x16/x8;
//   * transmisor y receptor a NIVEL DE BIT sobre los pines: bit de arranque,
//     8/9 bits de datos, paridad par/impar, 0.5/1/1.5/2 bits de parada;
//   * receptor con muestreo triple en el centro del bit y detección de ruido
//     (NF), error de trama (FE), de paridad (PE) y desbordamiento (ORE);
//   * banderas TXE/TC/RXNE/IDLE/CTS/LBD con la semántica de borrado real
//     (leer SR y después DR), e interrupción única por periférico;
//   * peticiones de DMA de transmisión y recepción (CR3.DMAT/DMAR);
//   * control de flujo CTS/RTS, medio dúplex (HDSEL), envío de break (SBK),
//     detección de break LIN y tiempo de guarda de Smartcard.
// =============================================================================
#ifndef STM32_PERIPH_USART_H
#define STM32_PERIPH_USART_H

#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos de la variante. Es lo único que distingue una UART de una USART.
// ---------------------------------------------------------------------------
struct UsartCaps {
    bool synchronous  = true;   // modo síncrono maestro: pin CK, CR2.CLKEN/CPOL/CPHA/LBCL
    bool flow_control = true;   // CTS/RTS por hardware: CR3.CTSE/RTSE, CR3.CTSIE
    bool smartcard    = true;   // CR3.SCEN/NACK y el tiempo de guarda de USART_GTPR
    bool irda         = true;   // CR3.IREN/IRLP (presente también en las UART)
    bool lin          = true;   // CR2.LINEN/LBDL/LBDIE (presente también en las UART)
    bool half_duplex  = true;   // CR3.HDSEL (presente también en las UART)
    const char* kind  = "USART";// etiqueta para avisos y trazas
};

// Las dos variantes del STM32F407VG [IR, §12.4.1]
inline constexpr UsartCaps CAPS_USART{true,  true,  true,  true, true, true, "USART"};
inline constexpr UsartCaps CAPS_UART {false, false, false, true, true, true, "UART"};

// ---------------------------------------------------------------------------
// Implementación común. Recibe los rasgos por el constructor: este es el punto
// de selección en tiempo de ejecución.
// ---------------------------------------------------------------------------
class UsartBase : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // Señales de función alternativa (el top registra los endpoints en pin_mux)
    sc_core::sc_signal<bool> tx_out{"tx_out"}, tx_oe{"tx_oe"};
    sc_core::sc_signal<bool> rx_in{"rx_in"};
    sc_core::sc_signal<bool> ck_out{"ck_out"};                  // solo si synchronous
    sc_core::sc_signal<bool> ck_oe{"ck_oe"};
    sc_core::sc_signal<bool> cts_in{"cts_in"}, rts_out{"rts_out"}, rts_oe{"rts_oe"};

    // ---- Offsets y bits [IR, §12.4.3] -------------------------------------
    enum : uint32_t { SR = 0x00, DR = 0x04, BRR = 0x08, CR1 = 0x0C,
                      CR2 = 0x10, CR3 = 0x14, GTPR = 0x18 };
    enum SrBit : uint32_t {
        S_PE = 1u << 0, S_FE = 1u << 1, S_NF = 1u << 2, S_ORE = 1u << 3,
        S_IDLE = 1u << 4, S_RXNE = 1u << 5, S_TC = 1u << 6, S_TXE = 1u << 7,
        S_LBD = 1u << 8, S_CTS = 1u << 9
    };

    // --- Constructor principal: los rasgos como parámetro ------------------
    UsartBase(sc_core::sc_module_name nm, uint32_t base,
              const UsartCaps& caps = CAPS_USART)
        : BusSlave(nm, base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(UsartBase);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(cts_proc);   sensitive << cts_in; dont_initialize();
        // Un cambio de PCLK cambia el baudrate sin que el firmware toque nada
        SC_METHOD(clk_proc);   sensitive << clk_hz;  dont_initialize();
    }
    // --- Atajo de selección en tiempo de ejecución -------------------------
    UsartBase(sc_core::sc_module_name nm, uint32_t base,
              bool synchronous, bool flow_control)
        : UsartBase(nm, base, UsartCaps{synchronous, flow_control, synchronous,
                                        true, true, true,
                                        synchronous ? "USART" : "UART"}) {}

    const UsartCaps& caps() const { return caps_; }

    // ---- Observación desde el banco de pruebas ----------------------------
    double   baud_hz()   const { return baud_; }
    uint64_t tx_frames() const { return n_tx_; }
    uint64_t ck_pulses() const { return n_ck_; }   // pulsos de CK emitidos
    uint64_t rx_frames() const { return n_rx_; }
    uint32_t sr_raw()    const { return sr_; }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case SR:   sr_read_ = true; return sr_;
            case DR:   return read_dr();
            case BRR:  return brr_;
            case CR1:  return cr1_;
            case CR2:  return cr2_;
            case CR3:  return cr3_;
            case GTPR: return caps_.smartcard ? gtpr_ : 0u;
            default:   return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = (off == DR) ? uint32_t(tdr_) : reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case SR:
                // Bits rc_w0: escribir 0 borra RXNE, TC, LBD y CTS.
                sr_ &= (v | ~(S_RXNE | S_TC | S_LBD | S_CTS));
                break;
            case DR:   write_dr(uint16_t(v & 0x1FFu)); break;
            case BRR:  if (!ue()) brr_ = v & 0xFFFFu; recompute_baud(); break;
            case CR1: {
                const uint32_t old = cr1_;
                cr1_ = v & 0xBFFFu;
                if ((old ^ cr1_) & (1u << 15)) recompute_baud();   // OVER8
                if (!(old & (1u << 13)) && ue()) start_up();       // UE 0 -> 1
                if ((old & (1u << 13)) && !ue()) shut_down();      // UE 1 -> 0
                break;
            }
            case CR2:  cr2_ = v & cr2_mask(); break;
            case CR3:  cr3_ = v & cr3_mask(); break;
            case GTPR: if (caps_.smartcard) gtpr_ = v & 0xFFFFu; break;
            default:   return;
        }
        update_irq();
        wake();
    }

private:
    UsartCaps caps_;
    // ---- Registros --------------------------------------------------------
    uint32_t sr_ = S_TXE | S_TC;                 // reset 0x00C0 [IR, §12.4.3-A]
    uint32_t brr_ = 0, cr1_ = 0, cr2_ = 0, cr3_ = 0, gtpr_ = 0;
    uint16_t tdr_ = 0, rdr_ = 0;
    bool     tdr_full_ = false;                  // hay dato pendiente de enviar
    bool     sr_read_  = false;                  // se ha leído SR (borrado de flags)
    double   baud_ = 0.0;
    uint64_t n_tx_ = 0, n_rx_ = 0, n_ck_ = 0;
    // ---- Salidas publicadas por un único proceso --------------------------
    bool o_irq_ = false, o_drq_rx_ = false, o_drq_tx_ = false;
    bool o_tx_ = true, o_tx_oe_ = false, o_rts_ = true, o_rts_oe_ = false;
    bool o_ck_ = false, o_ck_oe_ = false;
    sc_core::sc_event pub_ev_, wake_ev_;

    // ---- Campos de configuración ------------------------------------------
    bool ue()      const { return (cr1_ >> 13) & 1u; }
    bool te()      const { return (cr1_ >> 3)  & 1u; }
    bool re()      const { return (cr1_ >> 2)  & 1u; }
    bool m9()      const { return (cr1_ >> 12) & 1u; }   // longitud de palabra
    bool pce()     const { return (cr1_ >> 10) & 1u; }
    bool ps_odd()  const { return (cr1_ >> 9)  & 1u; }
    bool over8()   const { return (cr1_ >> 15) & 1u; }
    bool sbk()     const { return (cr1_ >> 0)  & 1u; }
    unsigned stop_code() const { return (cr2_ >> 12) & 3u; }
    bool linen()   const { return caps_.lin && ((cr2_ >> 14) & 1u); }
    bool lbdl11()  const { return (cr2_ >> 5) & 1u; }
    bool clken()   const { return caps_.synchronous && ((cr2_ >> 11) & 1u); }
    // Modo síncrono: polaridad, fase y pulso del último bit [IR, §12.4.3-E]
    bool cpol_ck() const { return (cr2_ >> 10) & 1u; }
    bool cpha_ck() const { return (cr2_ >> 9)  & 1u; }
    bool lbcl()    const { return (cr2_ >> 8)  & 1u; }
    bool ctse()    const { return caps_.flow_control && ((cr3_ >> 9) & 1u); }
    bool rtse()    const { return caps_.flow_control && ((cr3_ >> 8) & 1u); }
    bool dmat()    const { return (cr3_ >> 7) & 1u; }
    bool dmar()    const { return (cr3_ >> 6) & 1u; }
    bool hdsel()   const { return caps_.half_duplex && ((cr3_ >> 3) & 1u); }
    bool onebit()  const { return (cr3_ >> 11) & 1u; }
    bool scen()    const { return caps_.smartcard && ((cr3_ >> 5) & 1u); }

    // Máscaras de escritura de CR2 y CR3 según la variante. Los campos que la
    // variante no tiene son reservados: no se escriben y leen cero.
    uint32_t cr2_mask() const {
        uint32_t m = 0x0000300Fu;                         // STOP[13:12], ADD[3:0]
        if (caps_.synchronous) m |= 0x00000F00u;          // CLKEN, CPOL, CPHA, LBCL
        if (caps_.lin)         m |= 0x00004060u;          // LINEN, LBDIE, LBDL
        return m;
    }
    uint32_t cr3_mask() const {
        uint32_t m = 0x00000841u;                         // ONEBIT, EIE, y hueco
        m |= 0x000000C0u;                                 // DMAT, DMAR
        if (caps_.flow_control) m |= 0x00000700u;         // CTSIE, CTSE, RTSE
        if (caps_.smartcard)    m |= 0x00000030u;         // SCEN, NACK
        if (caps_.irda)         m |= 0x00000006u;         // IREN, IRLP
        if (caps_.half_duplex)  m |= 0x00000008u;         // HDSEL
        return m;
    }

    // ---- Baudios [IR, §12.4.3-C] -------------------------------------------
    // USARTDIV = mantisa + fracción / (8 * (2 - OVER8))
    // baud     = f_PCLK / (8 * (2 - OVER8) * USARTDIV)
    void clk_proc() { recompute_baud(); wake(); }

    void recompute_baud() {
        // Se lee el puerto y no domain_hz(): los dos procesos son sensibles a
        // clk_hz y el orden entre ellos no está garantizado.
        const double f = clk_hz.read();
        const unsigned mant = (brr_ >> 4) & 0xFFFu;
        const unsigned frac = over8() ? (brr_ & 0x7u) : (brr_ & 0xFu);
        const double   den  = 8.0 * (2.0 - (over8() ? 1.0 : 0.0));
        const double   div  = double(mant) + double(frac) / den;
        baud_ = (f > 0.0 && div > 0.0) ? f / (den * div) : 0.0;
        if (baud_ > 10.5e6)
            SC_REPORT_WARNING("usart", "baudios por encima de 10.5 Mbit/s [IR, 12.4.1]");
    }
    sc_core::sc_time bit_time() const {
        return (baud_ > 0.0) ? sc_core::sc_time(1.0e12 / baud_, sc_core::SC_PS)
                             : sc_core::sc_time(1, sc_core::SC_MS);
    }
    // Número de bits del marco: arranque + datos (+ paridad) + parada
    unsigned data_bits() const { return m9() ? 9u : 8u; }
    double   stop_bits() const {
        switch (stop_code()) { case 1: return 0.5; case 2: return 2.0;
                               case 3: return 1.5; default: return 1.0; }
    }

    // =======================================================================
    // Acceso al registro de datos
    // =======================================================================
    uint32_t read_dr() {
        const uint32_t v = rdr_;
        sr_ &= ~S_RXNE;
        if (sr_read_) sr_ &= ~(S_PE | S_FE | S_NF | S_ORE | S_IDLE);
        sr_read_ = false;
        update_irq();
        wake();
        return v;
    }
    void write_dr(uint16_t v) {
        tdr_ = v;
        tdr_full_ = true;
        sr_ &= ~S_TXE;
        if (sr_read_) sr_ &= ~S_TC;
        sr_read_ = false;
        update_irq();
        wake();
    }

    // =======================================================================
    // Interrupciones y peticiones de DMA
    // =======================================================================
    void update_irq() {
        const bool pe_ie   = (cr1_ >> 8) & 1u;
        const bool txe_ie  = (cr1_ >> 7) & 1u;
        const bool tc_ie   = (cr1_ >> 6) & 1u;
        const bool rxne_ie = (cr1_ >> 5) & 1u;
        const bool idle_ie = (cr1_ >> 4) & 1u;
        const bool lbd_ie  = caps_.lin && ((cr2_ >> 6) & 1u);
        const bool cts_ie  = caps_.flow_control && ((cr3_ >> 10) & 1u);
        const bool eie     = (cr3_ >> 0) & 1u;
        bool i = false;
        if (txe_ie  && (sr_ & S_TXE))  i = true;
        if (tc_ie   && (sr_ & S_TC))   i = true;
        if (rxne_ie && (sr_ & (S_RXNE | S_ORE))) i = true;
        if (idle_ie && (sr_ & S_IDLE)) i = true;
        if (pe_ie   && (sr_ & S_PE))   i = true;
        if (lbd_ie  && (sr_ & S_LBD))  i = true;
        if (cts_ie  && (sr_ & S_CTS))  i = true;
        // EIE solo genera interrupción de error cuando el DMA está activo
        if (eie && dmar() && (sr_ & (S_FE | S_ORE | S_NF))) i = true;
        o_irq_ = ue() && i;
        // Peticiones de DMA: son de nivel y las retira la lectura/escritura de DR
        o_drq_rx_ = ue() && dmar() && (sr_ & S_RXNE);
        o_drq_tx_ = ue() && dmat() && (sr_ & S_TXE) && te();
        publish();
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq.write(o_irq_);
        dma_req_rx.write(o_drq_rx_);
        dma_req_tx.write(o_drq_tx_);
        tx_out.write(o_tx_);
        tx_oe.write(o_tx_oe_);
        rts_out.write(o_rts_);
        rts_oe.write(o_rts_oe_);
        ck_out.write(o_ck_);
        ck_oe.write(o_ck_oe_);
    }
    void wake() { wake_ev_.notify(sc_core::SC_ZERO_TIME); }

    // El cambio de nivel de CTS levanta la bandera correspondiente [IR, §12.4.3-F]
    void cts_proc() {
        if (!caps_.flow_control || !ctse()) return;
        sr_ |= S_CTS;
        update_irq();
        wake();
    }

    void reset_proc() {
        if (rst_n.read()) return;
        sr_ = S_TXE | S_TC;
        brr_ = cr1_ = cr2_ = cr3_ = gtpr_ = 0;
        tdr_ = rdr_ = 0; tdr_full_ = false; sr_read_ = false;
        baud_ = 0.0; n_ck_ = 0;
        o_tx_ = true; o_tx_oe_ = false; o_rts_ = true; o_rts_oe_ = false;
        o_ck_ = false; o_ck_oe_ = false;
        update_irq();
        wake();
    }

    void start_up() {
        recompute_baud();
        o_rts_oe_ = rtse();
        o_ck_oe_  = clken();
        o_ck_     = clken() ? cpol_ck() : false;    // reposo del reloj de datos
        publish();
    }

    // ---- Modo síncrono: un pulso de CK por cada bit de DATOS --------------
    // El reloj lo genera SIEMPRE el USART (es el maestro) y NO acompaña ni al
    // bit de arranque ni a los de parada. Con CPHA = 0 el flanco de captura es
    // el primero del bit y con CPHA = 1 el segundo; CPOL fija el nivel de
    // reposo, y LBCL decide si se emite el pulso del último bit de datos
    // [IR, §12.4.3-E]. Es la misma temporización que la del SPI, con el que
    // comparte silicio conceptual (véase periph/spi.h).
    void send_bit(bool level, const sc_core::sc_time& tb, bool with_clock) {
        drive_tx(level);
        if (!with_clock) { wait(tb); return; }
        ++n_ck_;
        if (!cpha_ck()) {
            wait(tb / 2.0);
            o_ck_ = !cpol_ck(); publish();          // flanco de captura
            wait(tb / 2.0);
            o_ck_ = cpol_ck();  publish();
        } else {
            o_ck_ = !cpol_ck(); publish();          // flanco de preparación
            wait(tb / 2.0);
            o_ck_ = cpol_ck();  publish();          // flanco de captura
            wait(tb / 2.0);
        }
    }
    void shut_down() {
        o_tx_oe_ = false; o_rts_oe_ = false; o_ck_oe_ = false;
        publish();
    }

    // =======================================================================
    // Transmisor: nivel de bit sobre el pin
    // =======================================================================
    // Paridad par/impar calculada sobre los bits de datos [IR, §12.4.3-D]
    bool parity_of(uint16_t v, unsigned nbits) const {
        unsigned ones = 0;
        for (unsigned i = 0; i < nbits; ++i) if ((v >> i) & 1u) ++ones;
        return ps_odd() ? ((ones & 1u) == 0u) : ((ones & 1u) != 0u);
    }

    void drive_tx(bool level) {
        o_tx_ = level;
        o_tx_oe_ = true;
        publish();
    }

    void tx_proc() {
        for (;;) {
            // Reposo: línea a 1 (o liberada si el transmisor está apagado)
            if (!ue() || !te() || !clock_enabled() || domain_hz() <= 0.0) {
                o_tx_oe_ = false; o_tx_ = true; publish();
                wait(wake_ev_ | rst_n.value_changed_event() |
                     clk_hz.value_changed_event());
                continue;
            }
            if (!tdr_full_ && !sbk()) {
                // En medio dúplex el pin se libera cuando no se transmite
                o_tx_oe_ = !hdsel();
                o_tx_ = true;
                publish();
                wait(wake_ev_ | rst_n.value_changed_event());
                continue;
            }
            // Control de flujo: con CTSE el transmisor espera a CTS activo (bajo)
            if (ctse() && cts_in.read()) {
                wait(cts_in.value_changed_event() | wake_ev_);
                continue;
            }
            if (baud_ <= 0.0) { wait(wake_ev_); continue; }

            const sc_core::sc_time tb = bit_time();
            if (sbk()) {
                // Carácter de break: la línea baja durante todo el marco,
                // INCLUIDO el bit de parada (10 bits con M=0, 11 con M=1), y
                // después el delimitador a nivel alto. Es lo que hace que el
                // receptor levante un error de trama [IR, §12.4.3-A].
                drive_tx(false);
                wait(tb * (2 + data_bits()));
                cr1_ &= ~1u;                               // el hardware borra SBK
                drive_tx(true);
                wait(tb * stop_bits());
                continue;
            }

            // --- Marco: arranque, datos, paridad y parada ------------------
            const uint16_t data  = tdr_;
            const unsigned nb    = data_bits();
            const bool     par   = pce();
            const double   nstop = stop_bits();
            tdr_full_ = false;
            sr_ |=  S_TXE;                                 // el dato pasa al registro
            sr_ &= ~S_TC;
            update_irq();

            // En modo síncrono el reloj de datos acompaña a los bits de datos
            // (paridad incluida), nunca al arranque ni a la parada. El pulso
            // del último bit solo se emite si LBCL = 1.
            const bool sync = clken();
            const unsigned n_data = par ? nb - 1u : nb;    // la paridad ocupa el MSB
            const unsigned n_ck   = nb;                    // datos + paridad
            unsigned k = 0;
            send_bit(false, tb, false);                    // bit de arranque
            for (unsigned i = 0; i < n_data; ++i, ++k)
                send_bit((data >> i) & 1u, tb,
                         sync && (lbcl() || k + 1u < n_ck));
            if (par) {
                send_bit(parity_of(data, n_data), tb,
                         sync && (lbcl() || k + 1u < n_ck));
                ++k;
            }
            drive_tx(true);                                // bits de parada
            wait(tb * nstop);
            ++n_tx_;

            // Tiempo de guarda de Smartcard: retrasa TC [IR, §12.4.3-G]
            if (scen() && (gtpr_ >> 8) != 0) wait(tb * double((gtpr_ >> 8) & 0xFFu));

            if (!tdr_full_) { sr_ |= S_TC; update_irq(); }
        }
    }

    // =======================================================================
    // Receptor: detección del arranque y muestreo en el centro del bit
    // =======================================================================
    // Avanza exactamente un tiempo de bit tomando tres muestras alrededor del
    // centro (una sola si CR3.ONEBIT). Devuelve el nivel por mayoría.
    bool sample_next_bit(const sc_core::sc_time& tb, bool& noise) {
        if (onebit()) { wait(tb); return rx_in.read(); }
        wait(tb * (14.0 / 16.0));
        const bool a = rx_in.read();
        wait(tb / 16.0);
        const bool b = rx_in.read();
        wait(tb / 16.0);
        const bool c = rx_in.read();
        if (a != b || b != c) noise = true;
        return (unsigned(a) + unsigned(b) + unsigned(c)) >= 2u;
    }

    void rx_proc() {
        for (;;) {
            if (!ue() || !re() || baud_ <= 0.0 || !clock_enabled()) {
                wait(wake_ev_ | rst_n.value_changed_event());
                continue;
            }
            // Con RTS por hardware, la línea se activa (nivel bajo) cuando el
            // receptor puede aceptar un dato [IR, §12.4.3-F].
            if (rtse()) { o_rts_ = (sr_ & S_RXNE) != 0; publish(); }

            // Espera al flanco de bajada del bit de arranque. Si la línea está
            // ya baja (break en curso) se espera primero a que suba.
            if (!rx_in.read()) {
                wait(rx_in.posedge_event() | wake_ev_);
                continue;
            }
            if ((sr_ & S_IDLE) || n_rx_ == 0) {
                // Ya se ha señalado el reposo (o no ha llegado nada todavía):
                // no hace falta seguir contando marcos vacíos.
                wait(rx_in.negedge_event() | wake_ev_);
                if (rx_in.read()) continue;
            } else {
                const double idle_bits = 1.0 + data_bits() + stop_bits();
                wait(bit_time() * idle_bits, rx_in.negedge_event());
                if (rx_in.read()) {                        // línea en reposo un marco
                    sr_ |= S_IDLE; update_irq();
                    continue;
                }
            }
            // A PARTIR DE AQUÍ la configuración del marco queda congelada: el
            // receptor la muestrea al detectar el bit de arranque, no antes.
            // Leerla antes de esperar el flanco haría que una reconfiguración
            // producida mientras el hilo espera se aplicase al marco anterior.
            const sc_core::sc_time tb = bit_time();
            const unsigned nb  = data_bits();
            const bool     par = pce();

            // --- Centro del bit de arranque -------------------------------
            bool noise = false;
            wait(tb * (7.0 / 16.0));
            const bool s0 = rx_in.read();
            wait(tb / 16.0);
            const bool s1 = rx_in.read();
            wait(tb / 16.0);
            const bool s2 = rx_in.read();
            if (s0 || s1 || s2) {                          // arranque falso
                if (!(s0 && s1 && s2)) { sr_ |= S_NF; update_irq(); }
                continue;
            }

            // --- Bits de datos y paridad ----------------------------------
            uint16_t v = 0;
            const unsigned n_data = par ? nb - 1u : nb;
            for (unsigned i = 0; i < n_data; ++i)
                if (sample_next_bit(tb, noise)) v |= uint16_t(1u << i);
            bool par_rx = false;
            if (par) par_rx = sample_next_bit(tb, noise);

            // --- Bit de parada --------------------------------------------
            const bool stop = sample_next_bit(tb, noise);

            // --- Banderas -------------------------------------------------
            if (noise && !onebit()) sr_ |= S_NF;
            if (!stop) {
                sr_ |= S_FE;
                // Detección de break LIN: todo el marco a cero [IR, §12.4.3-E]
                if (linen() && v == 0 && !par_rx) sr_ |= S_LBD;
            }
            if (par && par_rx != parity_of(v, n_data)) sr_ |= S_PE;

            if (sr_ & S_RXNE) {
                sr_ |= S_ORE;                              // no se leyó el anterior
            } else {
                rdr_ = par ? uint16_t(v | (uint16_t(par_rx) << n_data)) : v;
                sr_ |= S_RXNE;
            }
            sr_ &= ~S_IDLE;
            ++n_rx_;
            update_irq();
        }
    }
};

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN. El parámetro de plantilla es una
// referencia a los rasgos, que quedan disponibles como constante de la clase:
// el tipo expresa la variante y `Uart` y `Usart` son tipos distintos.
// ---------------------------------------------------------------------------
template <const UsartCaps& Caps>
class UsartT : public UsartBase {
public:
    UsartT(sc_core::sc_module_name nm, uint32_t base)
        : UsartBase(nm, base, Caps) {}
    static constexpr const UsartCaps& variant() { return Caps; }
    static constexpr bool is_synchronous() { return Caps.synchronous; }
};

using Usart = UsartT<CAPS_USART>;   // USART1, USART2, USART3, USART6
using Uart  = UsartT<CAPS_UART>;    // UART4, UART5

static_assert(Usart::is_synchronous(),  "USART debe tener modo sincrono");
static_assert(!Uart::is_synchronous(),  "UART no tiene modo sincrono");

} // namespace stm32
#endif // STM32_PERIPH_USART_H
