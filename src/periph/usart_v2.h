// =============================================================================
// usart_v2.h — La USART de segunda generación de ST (fase S3 del F303)
//
// La de los F0, F3, F7, L0, L4, G0 y G4 [RM0316 §29]. Por fuera es «una UART»,
// como la de `usart.h`; por dentro es OTRO banco de registros, y el firmware
// del F4 no funciona en ella sin tocarlo:
//
//   F4 (usart.h)                       aquí
//   SR y DR, uno para todo             ISR (solo lectura), ICR (se borra
//                                      escribiendo un uno), RDR y TDR
//   leer SR y luego DR borra PE...     cada bandera, con su bit en ICR
//   BRR con mantisa y fracción         BRR = USARTDIV (con OVER8, los tres bits
//                                      bajos van desplazados uno)
//   el reloj es PCLK                   el reloj de NÚCLEO, el que diga
//                                      RCC_CFGR3 (PCLK, SYSCLK, LSE o HSI)
//
// y trae lo que el F4 no tiene: TEACK y REACK, inversión de los pines y de los
// datos, MSBFIRST, SWAP (cambiar TX por RX sin tocar el cableado), plazo de
// recepción (RTOR/RTOF), carácter de coincidencia (CMF), BUSY, DE para RS-485
// y una petición de break por RQR en vez de por un bit de CR1.
//
// UN MODELO, TRES VARIANTES, como en `usart.h`: lo que cambia entre la USART1-3
// y la UART4/5 de un F303xC es lo que dice la tabla 160 de RM0316, y va en los
// rasgos (`UsartV2Caps`). Un campo que la variante no tiene es reservado: no se
// escribe y lee cero.
//
// POR QUÉ NO SE HA SACADO EL MOTOR DE LÍNEA DE `usart.h`, como proponía §3.3
// del análisis. Ese motor hace esperas con expresiones concretas, y cualquier
// forma distinta de escribir la misma cuenta puede redondear distinto y mover
// el invariante de test407 —es lo que pasó a medir la fase D1 del puente serie
// con `motor_uart.h`—. Aquí se escribe de nuevo, con las mismas reglas
// (muestreo triple en el centro, por mayoría), y `usart.h` no se toca.
//
// QUÉ HAY MODELADO
//   * el banco entero: CR1, CR2, CR3, BRR, GTPR, RTOR, RQR, ISR, ICR, RDR y
//     TDR, con los campos que solo se escriben con UE = 0;
//   * los baudios desde el reloj de núcleo, con OVER8, y el aviso si BRR es
//     menor que 16 (el silicio no lo admite);
//   * el transmisor y el receptor a nivel de bit: 8 o 9 bits (7 si el rasgo lo
//     dice), paridad par o impar en el bit alto, 0,5/1/1,5/2 bits de parada,
//     MSBFIRST, DATAINV, TXINV, RXINV y SWAP, medio dúplex (HDSEL), y el
//     muestreo triple con NF o el sencillo de ONEBIT;
//   * la trama de reposo al poner TE, TEACK y REACK; TXE, TC (y su «TE a cero
//     sin nada que mandar»), RXNE, ORE (y OVRDIS), FE, PE, IDLE (una vez por
//     cada RXNE), BUSY, CMF, RTOF, LBDF, CTS y CTSIF;
//   * el break por RQR.SBKRQ (con SBKF), RXFRQ y TXFRQ, y el modo mudo (MME,
//     WAKE por línea en reposo o por marca de dirección, ADDM7, MMRQ y RWU);
//   * RTS y CTS por hardware, y DE con DEAT, DEDT y DEP en el pin de RTS;
//   * el modo síncrono maestro (CK con CPOL, CPHA y LBCL);
//   * la interrupción única y las dos peticiones de DMA, con DDRE.
//
// QUÉ NO, y se avisa una vez si el firmware lo enciende: Smartcard (SCEN),
// IrDA (IREN), el auto-baud (ABREN) y despertar del Stop (UESM: los bits se
// guardan, WUF no sube). Los registros están; la línea se comporta como una
// UART corriente.
// =============================================================================
#ifndef STM32_PERIPH_USART_V2_H
#define STM32_PERIPH_USART_V2_H

#include <cstdio>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Los rasgos [RM0316 §29.4, tabla 160]
// ---------------------------------------------------------------------------
struct UsartV2Caps {
    bool sincrono   = true;   // CK: CR2.CLKEN/CPOL/CPHA/LBCL
    bool flujo      = true;   // CTS/RTS: CR3.CTSE/RTSE/CTSIE, ISR.CTS/CTSIF
    bool smartcard  = true;   // CR3.SCEN/NACK/SCARCNT, GTPR.GT, RTOR.BLEN, EOBF
    bool irda       = true;   // CR3.IREN/IRLP, GTPR.PSC
    bool lin        = true;   // CR2.LINEN/LBDL/LBDIE, ISR.LBDF
    bool abr        = true;   // CR2.ABREN/ABRMOD, ISR.ABRE/ABRF
    bool de         = true;   // CR3.DEM/DEP, CR1.DEAT/DEDT (RS-485)
    bool dma        = true;   // CR3.DMAT/DMAR y las dos peticiones
    bool rto        = true;   // RTOR, CR2.RTOEN, CR1.RTOIE, ISR.RTOF
    bool despertar  = true;   // CR1.UESM, CR3.WUS/WUFIE, ISR.WUF
    bool siete_bits = false;  // CR1.M1: palabras de 7 bits (no en el F303xB/C)
    const char* kind = "USART v2";
};

// Las del STM32F303xB/C [RM0316, tabla 160]: la USART1-3 lo tiene todo menos
// las palabras de 7 bits; la UART4 no tiene ni flujo, ni síncrono, ni
// Smartcard, ni auto-baud, ni DE; la UART5, además, no tiene DMA.
constexpr UsartV2Caps caps_f3_usart() { UsartV2Caps c{}; c.kind = "USART v2"; return c; }
constexpr UsartV2Caps caps_f3_uart4() {
    UsartV2Caps c{};
    c.sincrono = false; c.flujo = false; c.smartcard = false; c.abr = false; c.de = false;
    c.kind = "UART v2";
    return c;
}
constexpr UsartV2Caps caps_f3_uart5() {
    UsartV2Caps c = caps_f3_uart4();
    c.dma = false;
    c.kind = "UART v2 (sin DMA)";
    return c;
}
inline constexpr UsartV2Caps CAPS_F3_USART = caps_f3_usart();
inline constexpr UsartV2Caps CAPS_F3_UART4 = caps_f3_uart4();
inline constexpr UsartV2Caps CAPS_F3_UART5 = caps_f3_uart5();

class UsartV2 : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // El reloj de núcleo (RCC_CFGR3.USARTxSW). Es el que da los baudios; el de
    // `clk_hz` (PCLK) solo cuenta para el acceso a los registros.
    sc_core::sc_in<double> ker_clk_hz{"ker_clk_hz"};

    // Los pines. Los dos de datos tienen las dos mitades porque SWAP las
    // cambia y HDSEL escucha por el de TX: quién conduce cuál lo decide el
    // modelo, no el cableado.
    sc_core::sc_signal<bool> tx_out{"tx_out"}, tx_oe{"tx_oe"}, tx_in{"tx_in"};
    sc_core::sc_signal<bool> rx_out{"rx_out"}, rx_oe{"rx_oe"}, rx_in{"rx_in"};
    sc_core::sc_signal<bool> ck_out{"ck_out"}, ck_oe{"ck_oe"};
    sc_core::sc_signal<bool> cts_in{"cts_in"};
    sc_core::sc_signal<bool> rts_out{"rts_out"}, rts_oe{"rts_oe"};   // RTS o DE

    // ---- Offsets [RM0316 §29.8.12, tabla 168] ------------------------------
    enum : uint32_t { R_CR1 = 0x00, R_CR2 = 0x04, R_CR3 = 0x08, R_BRR = 0x0C,
                      R_GTPR = 0x10, R_RTOR = 0x14, R_RQR = 0x18, R_ISR = 0x1C,
                      R_ICR = 0x20, R_RDR = 0x24, R_TDR = 0x28 };
    // ---- ISR [RM0316 §29.8.8] ----------------------------------------------
    static constexpr uint32_t I_PE = 1u << 0, I_FE = 1u << 1, I_NF = 1u << 2,
        I_ORE = 1u << 3, I_IDLE = 1u << 4, I_RXNE = 1u << 5, I_TC = 1u << 6,
        I_TXE = 1u << 7, I_LBDF = 1u << 8, I_CTSIF = 1u << 9, I_CTS = 1u << 10,
        I_RTOF = 1u << 11, I_EOBF = 1u << 12, I_ABRE = 1u << 14, I_ABRF = 1u << 15,
        I_BUSY = 1u << 16, I_CMF = 1u << 17, I_SBKF = 1u << 18, I_RWU = 1u << 19,
        I_WUF = 1u << 20, I_TEACK = 1u << 21, I_REACK = 1u << 22;
    // El valor de reset de ISR que da RM0316 (§29.8.8): 0x0200_00C0. El bit 25
    // está marcado como reservado en la misma página; se reproduce tal cual.
    static constexpr uint32_t ISR_RESET = 0x020000C0u;
    // Las banderas que se borran por ICR (el mismo bit) [RM0316 §29.8.9]
    static constexpr uint32_t ICR_MASK = I_PE | I_FE | I_NF | I_ORE | I_IDLE | I_TC |
        I_LBDF | I_CTSIF | I_RTOF | I_EOBF | I_CMF | I_WUF;

    UsartV2(sc_core::sc_module_name nm, uint32_t base, const UsartV2Caps& caps = CAPS_F3_USART)
        : BusSlave(nm, base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(UsartV2);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(cts_proc);   sensitive << cts_in; dont_initialize();
        SC_METHOD(clk_proc);   sensitive << ker_clk_hz; dont_initialize();
    }

    const UsartV2Caps& caps() const { return caps_; }
    // ---- Para el banco ------------------------------------------------------
    double   baud_hz()   const { return baud_; }
    uint64_t tx_frames() const { return n_tx_; }
    uint64_t rx_frames() const { return n_rx_; }
    uint32_t isr_raw()   const { return isr_; }
    unsigned avisos()    const { return n_avisos_; }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR1:  return cr1_;
            case R_CR2:  return cr2_;
            case R_CR3:  return cr3_;
            case R_BRR:  return brr_;
            case R_GTPR: return gtpr_;
            case R_RTOR: return rtor_;
            case R_ISR:  return isr_ | (caps_.flujo && !linea_cts() ? I_CTS : 0u);
            case R_RDR:  return lee_rdr();
            case R_TDR:  return tdr_;
            default:     return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = (off == R_TDR) ? tdr_ : (off == R_RDR || off == R_ICR || off == R_RQR)
                                                     ? 0u : reg_read(off);
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR1: escribe_cr1(v); break;
            case R_CR2: {
                uint32_t w = mascara_cr2();
                if (ue()) {
                    // Con UE = 1 solo se escriben RTOEN, ABREN, LBDIE y, con el
                    // receptor parado, ADD [RM0316 §29.8.2]
                    w &= (1u << 23) | (1u << 20) | (1u << 6) | (re() ? 0u : 0xFF000000u);
                }
                cr2_ = (cr2_ & ~w) | (v & w);
                if ((cr2_ >> 20) & 1u) avisa(av_abr_, "auto-baud (CR2.ABREN): no esta modelado; "
                                             "los baudios son los de BRR");
                break;
            }
            case R_CR3: {
                uint32_t w = mascara_cr3();
                if (ue()) w &= (1u << 22) | (7u << 17) | (1u << 10) | (1u << 7) | (1u << 6) | 1u;
                cr3_ = (cr3_ & ~w) | (v & w);
                if (scen()) avisa(av_sc_, "modo Smartcard (CR3.SCEN): no esta modelado; la linea "
                                  "funciona como una UART corriente");
                if ((cr3_ >> 1) & 1u) avisa(av_ir_, "IrDA (CR3.IREN): no esta modelado; la linea "
                                            "funciona como una UART corriente");
                break;
            }
            case R_BRR:
                if (!ue()) brr_ = v & 0xFFFFu;
                recalcula_baudios();
                break;
            case R_GTPR:
                if (!ue() && (caps_.smartcard || caps_.irda))
                    gtpr_ = v & ((caps_.smartcard ? 0xFF00u : 0u) | 0xFFu);
                break;
            case R_RTOR: if (caps_.rto) rtor_ = v & (caps_.smartcard ? 0xFFFFFFFFu : 0x00FFFFFFu); break;
            case R_RQR:  escribe_rqr(v); break;
            case R_ICR:  isr_ &= ~(v & ICR_MASK & mascara_isr()); break;
            case R_TDR:  escribe_tdr(uint16_t(v & 0x1FFu)); break;
            default:     return;
        }
        actualiza();
        despierta();
    }

private:
    UsartV2Caps caps_;
    uint32_t cr1_ = 0, cr2_ = 0, cr3_ = 0, brr_ = 0, gtpr_ = 0, rtor_ = 0;
    uint32_t isr_ = ISR_RESET;
    uint16_t tdr_ = 0, rdr_ = 0;
    bool     tdr_lleno_ = false;
    bool     te_ant_ = false, reposo_pend_ = false;
    bool     idle_armado_ = false;
    bool     transmitiendo_ = false;
    double   baud_ = 0.0;
    uint64_t n_tx_ = 0, n_rx_ = 0;
    unsigned n_avisos_ = 0;
    bool     av_abr_ = false, av_sc_ = false, av_ir_ = false, av_wu_ = false, av_brr_ = false;
    sc_core::sc_time fin_ultimo_ = sc_core::SC_ZERO_TIME;   // fin de la última parada
    bool     rto_armado_ = false;
    // Salidas, publicadas por un solo proceso
    bool o_irq_ = false, o_drx_ = false, o_dtx_ = false;
    bool o_txp_ = true, o_txp_oe_ = false, o_rxp_ = true, o_rxp_oe_ = false;
    bool o_ck_ = false, o_ck_oe_ = false, o_rts_ = true, o_rts_oe_ = false;
    sc_core::sc_event pub_ev_, wake_ev_;

    // ---- Campos ---------------------------------------------------------------
    bool ue()  const { return cr1_ & 1u; }
    bool re()  const { return (cr1_ >> 2) & 1u; }
    bool te()  const { return (cr1_ >> 3) & 1u; }
    bool pce() const { return (cr1_ >> 10) & 1u; }
    bool ps()  const { return (cr1_ >> 9) & 1u; }
    bool wake_dir() const { return (cr1_ >> 11) & 1u; }
    bool mme() const { return (cr1_ >> 13) & 1u; }
    bool over8() const { return (cr1_ >> 15) & 1u; }
    unsigned bits_palabra() const {
        const bool m0 = (cr1_ >> 12) & 1u, m1 = caps_.siete_bits && ((cr1_ >> 28) & 1u);
        return m1 ? 7u : (m0 ? 9u : 8u);
    }
    bool addm7() const { return (cr2_ >> 4) & 1u; }
    bool lbdl()  const { return (cr2_ >> 5) & 1u; }
    bool lbcl()  const { return (cr2_ >> 8) & 1u; }
    bool cpha()  const { return (cr2_ >> 9) & 1u; }
    bool cpol()  const { return (cr2_ >> 10) & 1u; }
    bool clken() const { return caps_.sincrono && ((cr2_ >> 11) & 1u); }
    unsigned stop_code() const { return (cr2_ >> 12) & 3u; }
    bool linen() const { return caps_.lin && ((cr2_ >> 14) & 1u); }
    bool swap()  const { return (cr2_ >> 15) & 1u; }
    bool rxinv() const { return (cr2_ >> 16) & 1u; }
    bool txinv() const { return (cr2_ >> 17) & 1u; }
    bool datainv() const { return (cr2_ >> 18) & 1u; }
    bool msbfirst() const { return (cr2_ >> 19) & 1u; }
    bool rtoen() const { return caps_.rto && ((cr2_ >> 23) & 1u); }
    uint8_t add() const { return uint8_t(cr2_ >> 24); }
    bool hdsel() const { return (cr3_ >> 3) & 1u; }
    bool scen()  const { return caps_.smartcard && ((cr3_ >> 5) & 1u); }
    bool dmar()  const { return caps_.dma && ((cr3_ >> 6) & 1u); }
    bool dmat()  const { return caps_.dma && ((cr3_ >> 7) & 1u); }
    bool rtse()  const { return caps_.flujo && ((cr3_ >> 8) & 1u); }
    bool ctse()  const { return caps_.flujo && ((cr3_ >> 9) & 1u); }
    bool onebit() const { return (cr3_ >> 11) & 1u; }
    bool ovrdis() const { return (cr3_ >> 12) & 1u; }
    bool ddre()  const { return (cr3_ >> 13) & 1u; }
    bool dem()   const { return caps_.de && ((cr3_ >> 14) & 1u); }
    bool dep()   const { return (cr3_ >> 15) & 1u; }
    unsigned deat() const { return (cr1_ >> 21) & 0x1Fu; }
    unsigned dedt() const { return (cr1_ >> 16) & 0x1Fu; }

    // ---- Máscaras según la variante -----------------------------------------------
    uint32_t mascara_cr1() const {
        uint32_t m = 0x0000FFFDu;                       // UE..OVER8 menos UESM
        if (caps_.despertar) m |= 1u << 1;              // UESM
        if (caps_.de)        m |= 0x03FF0000u;          // DEAT, DEDT
        if (caps_.rto)       m |= 1u << 26;             // RTOIE
        if (caps_.smartcard) m |= 1u << 27;             // EOBIE
        if (caps_.siete_bits) m |= 1u << 28;            // M1
        return m;
    }
    uint32_t mascara_cr2() const {
        uint32_t m = 0xFF0F3010u;                       // ADD, MSBFIRST..SWAP, STOP, ADDM7
        if (caps_.rto)      m |= 1u << 23;
        if (caps_.abr)      m |= 7u << 20;              // ABRMOD, ABREN
        if (caps_.lin)      m |= (1u << 14) | (1u << 6) | (1u << 5);
        if (caps_.sincrono) m |= 0xFu << 8;             // CLKEN, CPOL, CPHA, LBCL
        return m;
    }
    uint32_t mascara_cr3() const {
        uint32_t m = (1u << 13) | (1u << 12) | (1u << 11) | (1u << 3) | 1u;   // DDRE OVRDIS ONEBIT HDSEL EIE
        if (caps_.despertar) m |= 7u << 20;             // WUFIE, WUS
        if (caps_.smartcard) m |= (7u << 17) | (1u << 5) | (1u << 4);
        if (caps_.de)        m |= 3u << 14;
        if (caps_.flujo)     m |= 7u << 8;
        if (caps_.dma)       m |= 3u << 6;
        if (caps_.irda)      m |= 3u << 1;
        return m;
    }
    uint32_t mascara_isr() const {
        uint32_t m = I_PE | I_FE | I_NF | I_ORE | I_IDLE | I_RXNE | I_TC | I_TXE | I_BUSY |
                     I_CMF | I_SBKF | I_RWU | I_TEACK | I_REACK;
        if (caps_.lin) m |= I_LBDF;
        if (caps_.flujo) m |= I_CTSIF;
        if (caps_.rto) m |= I_RTOF;
        if (caps_.smartcard) m |= I_EOBF;
        if (caps_.abr) m |= I_ABRE | I_ABRF;
        if (caps_.despertar) m |= I_WUF;
        return m;
    }

    void avisa(bool& ya, const char* m) {
        if (ya) return;
        ya = true; ++n_avisos_;
        char t[260];
        std::snprintf(t, sizeof t, "%s: %s", name(), m);
        SC_REPORT_WARNING("usart_v2", t);
    }

    void escribe_cr1(uint32_t v) {
        uint32_t w = mascara_cr1();
        // M0, M1, OVER8, PCE, PS, WAKE, DEAT y DEDT solo con UE = 0 [RM0316 §29.8.1]
        if (ue()) w &= ~((1u << 12) | (1u << 28) | (1u << 15) | (1u << 10) | (1u << 9) |
                         (1u << 11) | 0x03FF0000u);
        const uint32_t ant = cr1_;
        cr1_ = (cr1_ & ~w) | (v & w);
        if ((cr1_ >> 1) & 1u) avisa(av_wu_, "despertar del Stop (CR1.UESM): no esta modelado; "
                                    "WUF no sube y la USART no despierta al MCU");
        if (((ant ^ cr1_) & (1u << 15)) || (!(ant & 1u) && ue())) recalcula_baudios();
        if ((ant & 1u) && !ue()) {
            // UE a cero: las banderas vuelven a su valor de reset, y las
            // peticiones de DMA se caen [RM0316 §29.8.1]
            isr_ = ISR_RESET;
            tdr_lleno_ = false; transmitiendo_ = false; reposo_pend_ = false;
            te_ant_ = false;
        }
        if (ue()) {
            // TEACK y REACK siguen a TE y RE en cuanto la USART los ve
            if (te()) {
                if (!te_ant_) reposo_pend_ = true;        // la trama de reposo
                isr_ |= I_TEACK;
            } else {
                isr_ &= ~I_TEACK;
                if (!transmitiendo_ && !tdr_lleno_) isr_ |= I_TC;
            }
            te_ant_ = te();
            if (re()) isr_ |= I_REACK; else isr_ &= ~(I_REACK | I_BUSY);
        }
    }

    void escribe_rqr(uint32_t v) {
        if (!ue()) return;
        if (v & 1u) {                                   // ABRRQ
            if (caps_.abr) isr_ &= ~(I_ABRE | I_ABRF);
        }
        if (v & 2u) isr_ |= I_SBKF;                     // SBKRQ
        if (v & 4u) { if (mme()) isr_ |= I_RWU; }       // MMRQ
        if (v & 8u) isr_ &= ~I_RXNE;                    // RXFRQ
        if ((v & 16u) && caps_.smartcard) { tdr_lleno_ = false; isr_ |= I_TXE; }   // TXFRQ
    }

    uint32_t lee_rdr() {
        const uint32_t v = rdr_;
        isr_ &= ~I_RXNE;
        actualiza();
        despierta();
        return v;
    }
    void escribe_tdr(uint16_t v) {
        tdr_ = v;
        tdr_lleno_ = true;
        isr_ &= ~(I_TXE | I_TC);
    }

    // ---- Baudios [RM0316 §29.5.4] -------------------------------------------------
    // 16x: baud = fck / BRR.   8x: baud = 2 fck / USARTDIV, con
    // USARTDIV[15:4] = BRR[15:4] y USARTDIV[3:0] = BRR[2:0] << 1
    void clk_proc() { recalcula_baudios(); despierta(); }
    void recalcula_baudios() {
        const double f = ker_clk_hz.read();
        double div = 0, num = f;
        if (over8()) { div = double((brr_ & 0xFFF0u) | ((brr_ & 7u) << 1)); num = 2.0 * f; }
        else div = double(brr_);
        baud_ = (f > 0.0 && div > 0.0) ? num / div : 0.0;
        if (ue() && brr_ != 0 && brr_ < 16u)
            avisa(av_brr_, "BRR < 16: el silicio no lo admite [RM0316 §29.8.4]");
    }
    sc_core::sc_time tbit() const {
        return baud_ > 0.0 ? sc_core::sc_time(1.0e12 / baud_, sc_core::SC_PS)
                           : sc_core::sc_time(1, sc_core::SC_MS);
    }
    double bits_parada() const {
        switch (stop_code()) { case 1: return 0.5; case 2: return 2.0; case 3: return 1.5; default: return 1.0; }
    }
    double bits_trama() const { return 1.0 + bits_palabra() + bits_parada(); }

    // =======================================================================
    // Interrupción y DMA [RM0316 §29.7]
    // =======================================================================
    void actualiza() {
        const uint32_t i = isr_;
        auto c1 = [&](unsigned b) { return (cr1_ >> b) & 1u; };
        bool q = false;
        if (c1(8) && (i & I_PE)) q = true;
        if (c1(7) && (i & I_TXE)) q = true;
        if (c1(6) && (i & I_TC)) q = true;
        if (c1(5) && (i & (I_RXNE | I_ORE))) q = true;
        if (c1(4) && (i & I_IDLE)) q = true;
        if (c1(14) && (i & I_CMF)) q = true;
        if (caps_.rto && c1(26) && (i & I_RTOF)) q = true;
        if (caps_.smartcard && c1(27) && (i & I_EOBF)) q = true;
        if (caps_.lin && ((cr2_ >> 6) & 1u) && (i & I_LBDF)) q = true;
        if (caps_.flujo && ((cr3_ >> 10) & 1u) && (i & I_CTSIF)) q = true;
        if ((cr3_ & 1u) && (i & (I_FE | I_ORE | I_NF))) q = true;
        if (caps_.despertar && ((cr3_ >> 22) & 1u) && (i & I_WUF)) q = true;
        o_irq_ = ue() && q;
        // Con DDRE, un error de recepción tapa la petición hasta que se borre
        const bool tapa = ddre() && (i & (I_PE | I_FE | I_NF | I_ORE));
        o_drx_ = ue() && dmar() && (i & I_RXNE) && !tapa;
        o_dtx_ = ue() && dmat() && te() && (i & I_TXE);
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        irq.write(o_irq_);
        dma_req_rx.write(o_drx_);
        dma_req_tx.write(o_dtx_);
        tx_out.write(o_txp_);  tx_oe.write(o_txp_oe_);
        rx_out.write(o_rxp_);  rx_oe.write(o_rxp_oe_);
        ck_out.write(o_ck_);   ck_oe.write(o_ck_oe_);
        rts_out.write(o_rts_); rts_oe.write(o_rts_oe_);
    }
    void despierta() { wake_ev_.notify(sc_core::SC_ZERO_TIME); }

    // CTS: ISR.CTS es la copia invertida del pin; CTSIF, cada cambio con CTSE
    bool linea_cts() const { return cts_in.read(); }
    void cts_proc() {
        if (ctse()) { isr_ |= I_CTSIF; actualiza(); }
        despierta();
    }

    void reset_proc() {
        if (rst_n.read()) return;
        cr1_ = cr2_ = cr3_ = brr_ = gtpr_ = rtor_ = 0;
        isr_ = ISR_RESET;
        tdr_ = rdr_ = 0; tdr_lleno_ = false; te_ant_ = false; reposo_pend_ = false;
        idle_armado_ = false; transmitiendo_ = false; rto_armado_ = false;
        baud_ = 0;
        o_txp_ = o_rxp_ = true; o_txp_oe_ = o_rxp_oe_ = false;
        o_ck_ = false; o_ck_oe_ = false; o_rts_ = true; o_rts_oe_ = false;
        actualiza();
        despierta();
    }

    // =======================================================================
    // La línea. Nivel LÓGICO: 1 es reposo (marca). El físico sale de TXINV y
    // RXINV; el pin, de SWAP y HDSEL.
    // =======================================================================
    void conduce(bool nivel, bool oe = true) {
        const bool p = txinv() ? !nivel : nivel;
        if (swap() && !hdsel()) { o_rxp_ = p; o_rxp_oe_ = oe; o_txp_oe_ = false; }
        else { o_txp_ = p; o_txp_oe_ = oe; o_rxp_oe_ = false; }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void suelta() { o_txp_oe_ = false; o_rxp_oe_ = false; o_txp_ = o_rxp_ = true; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    const sc_core::sc_signal<bool>& pin_rx() const {
        if (hdsel()) return tx_in;
        return swap() ? tx_in : rx_in;
    }
    bool lee_linea() const { const bool p = pin_rx().read(); return rxinv() ? !p : p; }
    const sc_core::sc_event& flanco_linea() const { return pin_rx().value_changed_event(); }

    // El DE de RS-485, en el pin de RTS: activo alto con DEP = 0
    void pon_de(bool activo) {
        o_rts_ = dep() ? !activo : activo;
        o_rts_oe_ = true;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // El RTS: bajo (listo) mientras no haya un dato por leer
    void pon_rts() {
        if (dem()) return;
        o_rts_oe_ = rtse() && ue();
        o_rts_ = (isr_ & I_RXNE) != 0;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    bool paridad(uint16_t v, unsigned n) const {
        unsigned unos = 0;
        for (unsigned i = 0; i < n; ++i) unos += (v >> i) & 1u;
        return ps() ? ((unos & 1u) == 0u) : ((unos & 1u) != 0u);   // impar : par
    }
    static uint16_t invierte(uint16_t v, unsigned n) {
        uint16_t r = 0;
        for (unsigned i = 0; i < n; ++i) if ((v >> i) & 1u) r |= uint16_t(1u << (n - 1 - i));
        return r;
    }

    // Un bit de la trama, con el pulso de CK si toca [RM0316 §29.5.11]
    void bit(bool nivel, const sc_core::sc_time& tb, bool con_ck) {
        conduce(nivel, true);
        if (!con_ck) { wait(tb); return; }
        if (!cpha()) {
            wait(tb / 2.0); o_ck_ = !cpol(); pub_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(tb / 2.0); o_ck_ = cpol();  pub_ev_.notify(sc_core::SC_ZERO_TIME);
        } else {
            o_ck_ = !cpol(); pub_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(tb / 2.0); o_ck_ = cpol(); pub_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(tb / 2.0);
        }
    }

    // =======================================================================
    // El transmisor
    // =======================================================================
    void tx_proc() {
        for (;;) {
            if (!ue() || !clock_enabled() || ker_clk_hz.read() <= 0.0 || baud_ <= 0.0) {
                suelta();
                o_ck_oe_ = false; pub_ev_.notify(sc_core::SC_ZERO_TIME);
                if (dem()) pon_de(false); else pon_rts();
                wait(wake_ev_ | rst_n.value_changed_event() | ker_clk_hz.value_changed_event());
                continue;
            }
            o_ck_oe_ = clken();
            if (!transmitiendo_) o_ck_ = clken() && cpol();
            if (!te()) {
                // Sin transmisor la línea queda libre (el pad sigue su GPIO o su pull)
                suelta();
                if (dem()) pon_de(false); else pon_rts();
                wait(wake_ev_ | rst_n.value_changed_event());
                continue;
            }
            const sc_core::sc_time tb = tbit();
            if (reposo_pend_) {
                // Poner TE manda una trama de reposo [RM0316 §29.5.2]
                reposo_pend_ = false;
                conduce(true, !hdsel());
                wait(tb * bits_trama());
                continue;
            }
            if (isr_ & I_SBKF) {
                // El break: toda la trama a cero y dos bits de parada; SBKF
                // baja durante la parada [RM0316 §29.5.2]
                transmitiendo_ = true;
                if (dem()) { pon_de(true); wait(tb * (deat() / (over8() ? 8.0 : 16.0))); }
                conduce(false);
                wait(tb * (1.0 + bits_palabra() + 1.0));
                isr_ &= ~I_SBKF; actualiza();
                conduce(true);
                wait(tb * 2.0);
                transmitiendo_ = false;
                continue;
            }
            if (!tdr_lleno_) {
                conduce(true, !hdsel());
                if (dem()) pon_de(false);
                wait(wake_ev_ | rst_n.value_changed_event());
                continue;
            }
            if (ctse() && cts_in.read()) {            // CTS alto: espera
                wait(cts_in.value_changed_event() | wake_ev_);
                continue;
            }
            // --- La trama ---------------------------------------------------------
            const unsigned nb = bits_palabra();
            const bool par = pce();
            const unsigned nd = par ? nb - 1u : nb;
            uint16_t d = tdr_ & uint16_t((1u << nd) - 1u);
            tdr_lleno_ = false;
            transmitiendo_ = true;
            isr_ |= I_TXE;
            isr_ &= ~I_TC;
            actualiza();
            if (dem()) { pon_de(true); wait(tb * (deat() / (over8() ? 8.0 : 16.0))); }
            bool pb = paridad(d, nd);
            if (msbfirst()) d = invierte(d, nd);
            if (datainv()) { d = uint16_t(~d) & uint16_t((1u << nd) - 1u); pb = !pb; }
            const bool sync = clken();
            unsigned k = 0;
            bit(false, tb, false);                         // arranque
            for (unsigned i = 0; i < nd; ++i, ++k) bit((d >> i) & 1u, tb, sync && (lbcl() || k + 1 < nb));
            if (par) { bit(pb, tb, sync && lbcl()); ++k; }
            conduce(true);
            wait(tb * bits_parada());                      // parada
            ++n_tx_;
            transmitiendo_ = false;
            if (dem() && !tdr_lleno_) { wait(tb * (dedt() / (over8() ? 8.0 : 16.0))); pon_de(false); }
            if (!tdr_lleno_) { isr_ |= I_TC; actualiza(); }
        }
    }

    // =======================================================================
    // El receptor
    // =======================================================================
    // Avanza un bit y lo muestrea: tres muestras en el centro, por mayoría
    // (una sola con ONEBIT) [RM0316 §29.5.3]
    bool muestra(const sc_core::sc_time& tb, bool& ruido) {
        const double paso = over8() ? 1.0 / 8.0 : 1.0 / 16.0;
        if (onebit()) { wait(tb); return lee_linea(); }
        wait(tb * (1.0 - 2.0 * paso));
        const bool a = lee_linea();
        wait(tb * paso);
        const bool b = lee_linea();
        wait(tb * paso);
        const bool c = lee_linea();
        if (a != b || b != c) ruido = true;
        return (unsigned(a) + unsigned(b) + unsigned(c)) >= 2u;
    }

    void rx_proc() {
        for (;;) {
            if (!ue() || !re() || baud_ <= 0.0 || !clock_enabled() || ker_clk_hz.read() <= 0.0) {
                wait(wake_ev_ | rst_n.value_changed_event() | ker_clk_hz.value_changed_event());
                continue;
            }
            pon_rts();
            const sc_core::sc_time tb = tbit();
            if (!lee_linea()) {                       // línea abajo (break en curso)
                wait(flanco_linea() | wake_ev_);
                continue;
            }
            // --- Esperando el arranque: IDLE y el plazo RTO ------------------------
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            sc_core::sc_time limite = sc_core::SC_ZERO_TIME;
            bool hay_limite = false, es_idle = false;
            if (idle_armado_) {
                limite = fin_ultimo_ + tb * bits_trama();
                hay_limite = true; es_idle = true;
            }
            if (rto_armado_ && rtoen() && (rtor_ & 0xFFFFFFu)) {
                const sc_core::sc_time r = fin_ultimo_ + tb * double(rtor_ & 0xFFFFFFu);
                if (!hay_limite || r < limite) { limite = r; es_idle = false; }
                hay_limite = true;
            }
            if (hay_limite) {
                if (limite > ahora) wait(limite - ahora, flanco_linea() | wake_ev_);
                if (sc_core::sc_time_stamp() >= limite && lee_linea()) {
                    if (es_idle) {
                        idle_armado_ = false;
                        // En el modo mudo por reposo, la línea en reposo despierta
                        if (mme() && !wake_dir() && (isr_ & I_RWU)) isr_ &= ~I_RWU;
                        else if (!(isr_ & I_RWU)) isr_ |= I_IDLE;
                    } else {
                        rto_armado_ = false;
                        isr_ |= I_RTOF;
                    }
                    actualiza();
                    continue;
                }
                if (lee_linea()) continue;            // un cambio de configuración
            } else {
                wait(flanco_linea() | wake_ev_);
                if (lee_linea()) continue;
            }
            // --- El arranque: centro del bit -----------------------------------------
            const unsigned nb = bits_palabra();
            const bool par = pce();
            const unsigned nd = par ? nb - 1u : nb;
            const double paso = over8() ? 1.0 / 8.0 : 1.0 / 16.0;
            bool ruido = false;
            isr_ |= I_BUSY;
            if (onebit()) {
                wait(tb * 0.5);
                if (lee_linea()) { isr_ &= ~I_BUSY; continue; }
            } else {
                wait(tb * (0.5 - paso));
                const bool s0 = lee_linea(); wait(tb * paso);
                const bool s1 = lee_linea(); wait(tb * paso);
                const bool s2 = lee_linea();
                if (s0 && s1 && s2) { isr_ &= ~I_BUSY; continue; }   // arranque falso
                if (s0 || s1 || s2) {
                    if (unsigned(s0) + unsigned(s1) + unsigned(s2) >= 2u) { isr_ &= ~I_BUSY; continue; }
                    ruido = true;
                }
            }
            // --- Datos, paridad y parada ---------------------------------------------
            uint16_t v = 0;
            for (unsigned i = 0; i < nd; ++i) if (muestra(tb, ruido)) v |= uint16_t(1u << i);
            bool prx = false;
            if (par) prx = muestra(tb, ruido);
            const bool parada = muestra(tb, ruido);
            // RXNE sube en el centro del bit de parada; la trama acaba medio
            // bit después, y desde ahí cuentan IDLE y el plazo RTO
            isr_ &= ~I_BUSY;
            fin_ultimo_ = sc_core::sc_time_stamp() + tb * (onebit() ? 0.5 : 0.5 - paso);
            if (datainv()) { v = uint16_t(~v) & uint16_t((1u << nd) - 1u); prx = !prx; }
            if (msbfirst()) v = invierte(v, nd);
            const bool err_par = par && (prx != paridad(v, nd));
            const uint16_t dato = par ? uint16_t(v | (uint16_t(prx) << nd)) : v;
            ++n_rx_;
            rto_armado_ = true;
            // --- El modo mudo ----------------------------------------------------------
            if (mme() && wake_dir()) {
                const bool es_dir = (dato >> (nb - 1)) & 1u;
                if (es_dir) {
                    const uint8_t a = addm7() ? uint8_t(dato & 0x7Fu) : uint8_t(dato & 0x0Fu);
                    const uint8_t b = addm7() ? uint8_t(add() & 0x7Fu) : uint8_t(add() & 0x0Fu);
                    if (a == b) isr_ &= ~I_RWU;
                    else { isr_ |= I_RWU; actualiza(); continue; }
                } else if (isr_ & I_RWU) { continue; }
            } else if (isr_ & I_RWU) {
                continue;                              // mudo: no se recibe nada
            }
            // --- Banderas ----------------------------------------------------------------
            if (!parada) {
                isr_ |= I_FE;
                if (linen() && v == 0 && !prx) isr_ |= I_LBDF;
            }
            if (err_par) isr_ |= I_PE;
            if (ruido && !onebit()) isr_ |= I_NF;
            if ((dato & 0xFFu) == add()) isr_ |= I_CMF;
            const bool error = !parada || err_par || (ruido && !onebit());
            if (isr_ & I_RXNE) {
                if (!ovrdis()) isr_ |= I_ORE;         // el dato nuevo se pierde
                else rdr_ = dato;
            } else if (error && dmar() && !ddre()) {
                // Sin DDRE, con DMA, el dato con error no se entrega: RXNE
                // sigue a cero y el DMA no lo ve [RM0316 §29.8.3, DDRE]
            } else {
                rdr_ = dato;
                isr_ |= I_RXNE;
            }
            // IDLE no vuelve a subir hasta que haya llegado un dato [RM0316 §29.8.8]
            idle_armado_ = true;
            actualiza();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_USART_V2_H
