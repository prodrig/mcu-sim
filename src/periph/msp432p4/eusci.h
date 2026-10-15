// =============================================================================
// eusci.h — La eUSCI del MSP432P4: UART, SPI e I2C (fase T3)
//
// La misma interfaz serie del MSP430FR, en dos sabores [H: EUSCI_A_Type y
// EUSCI_B_Type; TRM SLAU356, cap. 24-26]:
//
//   eUSCI_A0..A3   UART (con los modos multiprocesador) y SPI
//   eUSCI_B0..B3   SPI e I2C
//
// Una sola clase con un parámetro, como la USART de los STM32. Lo que cambia
// entre A y B son los desplazamientos de la mitad de los registros, y qué
// modos existen. Las patillas son cuatro, con los nombres de cada modo:
//
//   «simo»   UCAxTXD / UCxSIMO / UCBxSDA
//   «somi»   UCAxRXD / UCxSOMI / UCBxSCL
//   «clk»    UCxCLK
//   «ste»    UCxSTE
//
// y de cada una el módulo da out/oe y recibe in: la eUSCI decide la dirección
// de sus pines, no PxDIR [FuncionP4 en dio.h].
//
// UART, A NIVEL DE BIT sobre los pines, con el generador de baudios del eUSCI:
//
//   UCOS16 = 0   cada bit dura UCBRx ciclos de BRCLK, más uno si el bit del
//                patrón UCBRSx que le toca es 1
//   UCOS16 = 1   cada bit dura 16·UCBRx + UCBRFx ciclos, más uno según UCBRSx
//
// El patrón UCBRSx se aplica bit a bit desde el de arranque -el bit 0 del
// patrón para el de arranque- y vuelve a empezar en cada carácter. [TRM
// 24.3.10; segunda fuente para los valores: la tabla de Tock, uart.rs; el
// orden de aplicación es [1F]: P-16 q]. 7 u 8 bits, LSB o MSB primero,
// paridad, uno o dos de parada; break en los dos sentidos; los modos
// multiprocesador de línea ociosa y de bit de dirección con UCDORM; errores
// de trama, paridad y desbordamiento, con UCRXEIE decidiendo si un carácter
// con error llega a RXBUF; UCLISTEN; y las cuatro banderas con su IV.
//
// SPI maestro y esclavo, de 3 y de 4 hilos, con CKPH/CKPL, 7/8 bits y
// MSB/LSB. En maestro, STE es salida con UCSTEM = 1 (se activa mientras hay
// transferencia) y entrada de conflicto con UCSTEM = 0.
//
// LO QUE NO ESTÁ: la detección automática de baudios (UCMODE = 3 en la A) e
// IrDA (UCAxIRCTL): se guardan y avisan una vez. El filtro de glitches
// (UCGLIT) no filtra nada. [P-16 q]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_EUSCI_H
#define STM32_PERIPH_MSP432P4_EUSCI_H

#include <cstdio>
#include <functional>
#include <string>
#include "bus16.h"
#include "dio.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class EusciP4 : public Bus16P4 {
public:
    sc_core::sc_out<bool> irq{"irq"};
    // Las cuatro patillas: lo que saca el módulo y lo que le llega
    sc_core::sc_signal<bool> simo_out{"simo_out"}, simo_oe{"simo_oe"}, simo_in{"simo_in"};
    sc_core::sc_signal<bool> somi_out{"somi_out"}, somi_oe{"somi_oe"}, somi_in{"somi_in"};
    sc_core::sc_signal<bool> clk_out{"clk_out"},   clk_oe{"clk_oe"},   clk_in{"clk_in"};
    sc_core::sc_signal<bool> ste_out{"ste_out"},   ste_oe{"ste_oe"},   ste_in{"ste_in"};
    // Las peticiones de DMA: el nivel de RXIFG(0) y TXIFG(0)
    sc_core::sc_signal<bool> dma_rx{"dma_rx"}, dma_tx{"dma_tx"};

    // La frecuencia de BRCLK según UCSSEL (0 UCLK, 1 ACLK, 2-3 SMCLK). La da el SoC.
    std::function<double(unsigned)> f_brclk;

    // --- Registros [H] ---------------------------------------------------------
    enum : uint16_t {
        SWRST = 0x0001, SYNC = 0x0100,
        // UART
        TXBRK = 0x0002, TXADDR = 0x0004, DORM = 0x0008, BRKIE = 0x0010, RXEIE = 0x0020,
        SPB = 0x0800, SEVENBIT = 0x1000, MSB = 0x2000, PAR = 0x4000, PEN = 0x8000,
        // SPI
        STEM = 0x0002, MST = 0x0800, CKPL = 0x4000, CKPH = 0x8000,
        // STATW
        ST_BUSY = 0x0001, ST_ADDR_IDLE = 0x0002, ST_RXERR = 0x0004, ST_BRK = 0x0008,
        ST_PE = 0x0010, ST_OE = 0x0020, ST_FE = 0x0040, ST_LISTEN = 0x0080,
        // IFG / IE (UART y SPI)
        RXIFG = 0x0001, TXIFG = 0x0002, STTIFG = 0x0004, TXCPTIFG = 0x0008,
    };

    EusciP4(sc_core::sc_module_name nm, uint32_t base, bool es_b)
        : Bus16P4(nm, base, 0x400), es_b_(es_b) {
        SC_HAS_PROCESS(EusciP4);
        SC_THREAD(uart_tx_proc);
        SC_THREAD(uart_rx_proc);
        SC_THREAD(spi_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }

    bool es_b() const { return es_b_; }
    void recalcula() { despierta(); }

    // Las funciones de pin, para el DIO
    FuncionP4 fn_simo() { return f(&simo_out, &simo_oe, &simo_in, "simo"); }
    FuncionP4 fn_somi() { return f(&somi_out, &somi_oe, &somi_in, "somi"); }
    FuncionP4 fn_clk()  { return f(&clk_out,  &clk_oe,  &clk_in,  "clk"); }
    FuncionP4 fn_ste()  { return f(&ste_out,  &ste_oe,  &ste_in,  "ste"); }

    // --- Lo que mira el banco ------------------------------------------------
    uint16_t ctlw0() const { return ctlw0_; }
    uint16_t ifg()   const { return ifg_; }
    uint16_t statw() const { return statw_lee(); }
    double   baudios() const;
    uint64_t tramas_tx() const { return n_tx_; }
    uint64_t tramas_rx() const { return n_rx_; }
    uint64_t bytes_spi() const { return n_spi_; }
    unsigned avisos() const { return n_avisos_; }

protected:
    const bool es_b_;
    // Registros comunes
    uint16_t ctlw0_ = SWRST, ctlw1_ = 0x0003, brw_ = 0, mctlw_ = 0, statw_ = 0;
    uint16_t rxbuf_ = 0, txbuf_ = 0, abctl_ = 0, irctl_ = 0, ie_ = 0, ifg_ = TXIFG;
    // De la B (I2C)
    uint16_t tbcnt_ = 0, oa_[4] = {0, 0, 0, 0}, addrx_ = 0, addmask_ = 0x03FF, i2csa_ = 0;

    bool txlleno_ = false;          // hay un carácter en TXBUF esperando
    bool tx_ocupado_ = false, rx_ocupado_ = false;
    bool siguiente_es_dir_ = false; // UART, línea ociosa: el próximo carácter es dirección
    // El final del último carácter recibido, o el momento en que se soltó
    // UCSWRST: la línea ociosa se cuenta desde ahí
    sc_core::sc_time fin_rx_{sc_core::SC_ZERO_TIME};
    bool repulso_ = false, o_irq_ = false;
    // Lo que saca el módulo, publicado por un único proceso
    bool o_simo_ = true, o_simo_oe_ = false, o_somi_ = true, o_somi_oe_ = false,
         o_clk_ = false, o_clk_oe_ = false, o_ste_ = true, o_ste_oe_ = false;
    sc_core::sc_signal<bool> tx_interno_{"tx_interno"};   // UCLISTEN
    sc_core::sc_event pub_ev_, ev_;
    uint64_t n_tx_ = 0, n_rx_ = 0, n_spi_ = 0;
    unsigned n_avisos_ = 0;
    bool avisado_ab_ = false, avisado_ir_ = false, avisado_i2c_ = false, avisado_uclk_ = false;

    static FuncionP4 f(sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                       sc_core::sc_signal<bool>* i, const char* n) {
        FuncionP4 r; r.out = o; r.oe = e; r.in = i; r.idle_in = true; r.nombre = n;
        return r;
    }

    // --- Modo ---------------------------------------------------------------
    bool en_reset() const { return (ctlw0_ & SWRST) != 0; }
    bool sincrono() const { return (ctlw0_ & SYNC) != 0; }
    unsigned ucmode() const { return (ctlw0_ >> 9) & 3u; }
    bool uart() const { return !es_b_ && !sincrono(); }
    bool i2c()  const { return es_b_ && sincrono() && ucmode() == 3; }
    bool spi()  const { return sincrono() && !i2c(); }
    bool listen() const { return (statw_ & ST_LISTEN) != 0; }
    double brclk() const {
        const unsigned s = (ctlw0_ >> 6) & 3u;
        return f_brclk ? f_brclk(s) : 0.0;
    }

    // --- Desplazamientos: los de la A y los de la B ---------------------------
    enum Reg { R_CTLW0, R_CTLW1, R_BRW, R_MCTLW, R_STATW, R_TBCNT, R_RXBUF, R_TXBUF,
               R_ABCTL, R_IRCTL, R_OA0, R_OA1, R_OA2, R_OA3, R_ADDRX, R_ADDMASK,
               R_I2CSA, R_IE, R_IFG, R_IV, R_NADA };
    Reg cual(uint32_t off) const {
        switch (off) {
            case 0x00: return R_CTLW0;
            case 0x02: return R_CTLW1;
            case 0x06: return R_BRW;
            case 0x0C: return R_RXBUF;
            case 0x0E: return R_TXBUF;
            default: break;
        }
        if (!es_b_) {
            switch (off) {
                case 0x08: return R_MCTLW;  case 0x0A: return R_STATW;
                case 0x10: return R_ABCTL;  case 0x12: return R_IRCTL;
                case 0x1A: return R_IE;     case 0x1C: return R_IFG;
                case 0x1E: return R_IV;     default: return R_NADA;
            }
        }
        switch (off) {
            case 0x08: return R_STATW;  case 0x0A: return R_TBCNT;
            case 0x14: return R_OA0;    case 0x16: return R_OA1;
            case 0x18: return R_OA2;    case 0x1A: return R_OA3;
            case 0x1C: return R_ADDRX;  case 0x1E: return R_ADDMASK;
            case 0x20: return R_I2CSA;  case 0x2A: return R_IE;
            case 0x2C: return R_IFG;    case 0x2E: return R_IV;
            default: return R_NADA;
        }
    }

    void valores_de_reset() {
        ctlw0_ = SWRST; ctlw1_ = 0x0003; brw_ = 0; mctlw_ = 0; statw_ = 0;
        rxbuf_ = 0; txbuf_ = 0; abctl_ = 0; irctl_ = 0; ie_ = 0; ifg_ = TXIFG;
        tbcnt_ = 0; for (uint16_t& o : oa_) o = 0; addrx_ = 0; addmask_ = 0x03FF; i2csa_ = 0;
        txlleno_ = false; siguiente_es_dir_ = false;
        lineas_en_reposo();
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        actualiza_irq();
        despierta();
    }
    // Las patillas, según el modo. En UART el módulo saca TXD siempre (alto en
    // reposo); en SPI maestro, SIMO y CLK; en esclavo, SOMI.
    void lineas_en_reposo() {
        o_simo_ = true; o_somi_ = true; o_ste_ = true;
        o_simo_oe_ = o_somi_oe_ = o_clk_oe_ = o_ste_oe_ = false;
        o_clk_ = (ctlw0_ & CKPL) != 0 && spi();
        if (uart()) { o_simo_oe_ = true; }
        else if (spi()) {
            const bool mst = (ctlw0_ & MST) != 0;
            if (mst) {
                o_simo_oe_ = true; o_clk_oe_ = true;
                const bool cuatro = ucmode() == 1 || ucmode() == 2;
                if (cuatro && (ctlw0_ & STEM)) { o_ste_oe_ = true; o_ste_ = ste_inactivo(); }
            }
        }
        publica();
    }
    bool ste_inactivo() const { return ucmode() == 2; }   // modo 2: STE activo bajo

    // --- Interrupciones -------------------------------------------------------
    uint16_t pendientes() const { return uint16_t(ifg_ & ie_); }
    void actualiza_irq() {
        const bool quiere = pendientes() != 0;
        o_irq_ = quiere;
        publica();
    }
    // Algo se ha borrado: si sigue habiendo pendientes, la línea baja un
    // delta y vuelve a subir, para que el NVIC -que latchea por flanco- vea
    // que queda trabajo (lo mismo que hace el DIO con su IV)
    void borrado() {
        if (pendientes()) repulso_ = true;
        actualiza_irq();
    }
    void publica() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void despierta() { ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        if (repulso_ && o_irq_) {
            repulso_ = false;
            irq.write(false);
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
        } else {
            repulso_ = false;
            irq.write(o_irq_);
        }
        simo_out.write(o_simo_); simo_oe.write(o_simo_oe_);
        somi_out.write(o_somi_); somi_oe.write(o_somi_oe_);
        clk_out.write(o_clk_);   clk_oe.write(o_clk_oe_);
        ste_out.write(o_ste_);   ste_oe.write(o_ste_oe_);
        tx_interno_.write(o_simo_);
        dma_rx.write((ifg_ & RXIFG) != 0 && !en_reset());
        dma_tx.write((ifg_ & TXIFG) != 0 && !en_reset());
    }
    // El IV: el pendiente de mayor prioridad, y leerlo lo borra
    uint16_t iv(bool dbg) {
        const uint16_t p = pendientes();
        if (!es_b_ || !i2c()) {
            static const uint16_t orden[4] = {RXIFG, TXIFG, STTIFG, TXCPTIFG};
            for (unsigned k = 0; k < 4; ++k)
                if (p & orden[k]) {
                    if (!dbg) { ifg_ &= uint16_t(~orden[k]); borrado(); }
                    return uint16_t(2u * (k + 1u));
                }
            return 0;
        }
        // I2C: AL, NACK, STT, STP, RX3, TX3, RX2, TX2, RX1, TX1, RX0, TX0, BCNT, CLTO, BIT9
        static const uint16_t orden[15] = {0x0010, 0x0020, 0x0004, 0x0008, 0x1000, 0x2000,
                                           0x0400, 0x0800, 0x0100, 0x0200, 0x0001, 0x0002,
                                           0x0040, 0x0080, 0x4000};
        for (unsigned k = 0; k < 15; ++k)
            if (p & orden[k]) {
                if (!dbg) { ifg_ &= uint16_t(~orden[k]); borrado(); }
                return uint16_t(2u * (k + 1u));
            }
        return 0;
    }

    uint16_t statw_lee() const {
        uint16_t v = statw_;
        if (tx_ocupado_ || rx_ocupado_ || txlleno_) v |= ST_BUSY; else v &= uint16_t(~ST_BUSY);
        return v;
    }

    // --- Los registros ----------------------------------------------------------
    uint16_t lee16(uint32_t off, bool dbg) override {
        switch (cual(off)) {
            case R_CTLW0:  return ctlw0_;
            case R_CTLW1:  return ctlw1_;
            case R_BRW:    return brw_;
            case R_MCTLW:  return mctlw_;
            case R_STATW:  return statw_lee();
            case R_TBCNT:  return tbcnt_;
            case R_RXBUF: {
                const uint16_t v = rxbuf_;
                if (!dbg) {
                    ifg_ &= uint16_t(~RXIFG);
                    statw_ &= uint16_t(~(ST_RXERR | ST_BRK | ST_PE | ST_OE | ST_FE |
                                         (uart() ? ST_ADDR_IDLE : 0)));
                    borrado();
                }
                return v;
            }
            case R_TXBUF:  return txbuf_;
            case R_ABCTL:  return abctl_;
            case R_IRCTL:  return irctl_;
            case R_OA0: case R_OA1: case R_OA2: case R_OA3:
                return oa_[cual(off) - R_OA0];
            case R_ADDRX:  return addrx_;
            case R_ADDMASK:return addmask_;
            case R_I2CSA:  return i2csa_;
            case R_IE:     return ie_;
            case R_IFG:    return ifg_;
            case R_IV:     return iv(dbg);
            default:       return 0;
        }
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        switch (cual(off)) {
            case R_CTLW0: escribe_ctlw0(mz(ctlw0_)); return;
            case R_CTLW1: ctlw1_ = uint16_t(mz(ctlw1_) & (es_b_ ? 0x01FFu : 0x0003u)); return;
            case R_BRW:   brw_ = mz(brw_); despierta(); return;
            case R_MCTLW: mctlw_ = uint16_t(mz(mctlw_) & 0xFFF1u); despierta(); return;
            case R_STATW: statw_ = uint16_t((statw_ & ~ST_LISTEN) | (mz(statw_) & ST_LISTEN));
                          despierta(); return;
            case R_TBCNT: tbcnt_ = uint16_t(mz(tbcnt_) & 0xFFu); return;
            case R_TXBUF: escribe_txbuf(uint16_t(mz(txbuf_) & 0xFFu)); return;
            case R_ABCTL: abctl_ = uint16_t(mz(abctl_) & 0x0037u);
                          if ((abctl_ & 1u) && !avisado_ab_) {
                              avisado_ab_ = true; ++n_avisos_;
                              SC_REPORT_WARNING("msp432p4/eusci", "deteccion automatica de "
                                  "baudios (UCABDEN): no se modela; los baudios son los de UCAxBRW");
                          }
                          return;
            case R_IRCTL: irctl_ = mz(irctl_);
                          if ((irctl_ & 1u) && !avisado_ir_) {
                              avisado_ir_ = true; ++n_avisos_;
                              SC_REPORT_WARNING("msp432p4/eusci", "IrDA (UCIREN): no se modela; "
                                  "la linea sale como una UART normal");
                          }
                          return;
            case R_OA0: case R_OA1: case R_OA2: case R_OA3:
                oa_[cual(off) - R_OA0] = uint16_t(mz(oa_[cual(off) - R_OA0]) & 0x87FFu); return;
            case R_ADDMASK: addmask_ = uint16_t(mz(addmask_) & 0x03FFu); return;
            case R_I2CSA:   i2csa_ = uint16_t(mz(i2csa_) & 0x03FFu); return;
            case R_IE:  ie_ = mz(ie_); actualiza_irq(); return;
            case R_IFG: {
                const uint16_t antes = ifg_;
                ifg_ = mz(ifg_);
                if (ifg_ & ~antes) actualiza_irq(); else borrado();
                return;
            }
            default: return;
        }
    }

    void escribe_ctlw0(uint16_t v) {
        const bool estaba = en_reset();
        if (!estaba) {
            // Con el módulo en marcha solo cambian los bits de control
            const uint16_t vivos = uint16_t(SWRST | (uart() ? (TXBRK | TXADDR | DORM | BRKIE | RXEIE) :
                                            i2c() ? 0x003Eu : 0u));
            v = uint16_t((ctlw0_ & ~vivos) | (v & vivos));
        }
        ctlw0_ = v;
        if (estaba && !en_reset()) fin_rx_ = sc_core::sc_time_stamp();
        if (!estaba && en_reset()) {
            // UCSWRST = 1: lo que se está transmitiendo o recibiendo se pierde,
            // las banderas se borran y TXIFG se pone [TRM 24.3.1; P-16 q]
            ifg_ = es_b_ ? uint16_t(TXIFG) : uint16_t(TXIFG);
            statw_ &= ST_LISTEN;
            txlleno_ = false;
            siguiente_es_dir_ = false;
        }
        if (uart() && ucmode() == 3 && !avisado_ab_) {
            avisado_ab_ = true; ++n_avisos_;
            SC_REPORT_WARNING("msp432p4/eusci", "UART con deteccion automatica de baudios "
                "(UCMODE = 3): no se modela; se usan los baudios de UCAxBRW");
        }
        if (i2c() && !avisado_i2c_) {
            avisado_i2c_ = true; ++n_avisos_;
            SC_REPORT_WARNING("msp432p4/eusci", "I2C (UCMODE = 3): todavia no se modela");
        }
        lineas_en_reposo();
        actualiza_irq();
        despierta();
    }
    void escribe_txbuf(uint16_t v) {
        txbuf_ = v;
        if (en_reset()) return;
        txlleno_ = true;
        ifg_ &= uint16_t(~(TXIFG | TXCPTIFG));
        borrado();
        despierta();
    }

    // =========================================================================
    // UART
    // =========================================================================
    // Lo que dura el bit `k` del carácter, en ciclos de BRCLK
    double ciclos_bit(unsigned k) const {
        const unsigned br = brw_ ? brw_ : 1u;
        const bool os16 = (mctlw_ & 1u) != 0;
        const unsigned brf = (mctlw_ >> 4) & 0xFu;
        const unsigned brs = (mctlw_ >> 8) & 0xFFu;
        const double base = os16 ? 16.0 * br + brf : double(br);
        return base + double((brs >> (k & 7u)) & 1u);
    }
    sc_core::sc_time t_bit(unsigned k, double f) const {
        return sc_core::sc_time(ciclos_bit(k) * 1e12 / f, sc_core::SC_PS);
    }
    unsigned bits_datos() const { return (ctlw0_ & SEVENBIT) ? 7u : 8u; }
    bool aviso_uclk(double f) {
        if (f > 0.0) return false;
        if (!avisado_uclk_ && ((ctlw0_ >> 6) & 3u) == 0) {
            avisado_uclk_ = true; ++n_avisos_;
            SC_REPORT_WARNING("msp432p4/eusci", "UCSSEL = 0 (UCLK, el reloj externo del pin "
                "CLK): no se modela; la eUSCI no avanza");
        }
        return true;
    }

    // Los bits de un carácter, en el orden en que salen tras el de arranque
    void bits_trama(uint16_t dato, bool dir_bit, std::vector<bool>& b) const {
        const unsigned n = bits_datos();
        const bool msb = (ctlw0_ & MSB) != 0;
        unsigned unos = 0;
        for (unsigned i = 0; i < n; ++i) {
            const unsigned k = msb ? (n - 1u - i) : i;
            const bool v = (dato >> k) & 1u;
            unos += v;
            b.push_back(v);
        }
        if (ucmode() == 2) { b.push_back(dir_bit); unos += dir_bit; }
        if (ctlw0_ & PEN) {
            const bool par = (ctlw0_ & PAR) != 0;            // 1: par
            b.push_back(par ? (unos & 1u) : !(unos & 1u));
        }
    }

    void uart_tx_proc() {
        for (;;) {
            const double f = brclk();
            if (!uart() || en_reset() || !txlleno_ || f <= 0.0) {
                if (uart() && txlleno_ && !en_reset()) aviso_uclk(f);
                wait(ev_);
                continue;
            }
            // El carácter pasa al registro de desplazamiento: TXIFG
            const uint16_t dato = txbuf_;
            const bool brk = (ctlw0_ & TXBRK) != 0;
            const bool dir = (ctlw0_ & TXADDR) != 0;
            txlleno_ = false;
            tx_ocupado_ = true;
            ifg_ |= TXIFG;
            actualiza_irq();
            // Línea ociosa: TXADDR manda once bits de reposo antes del carácter
            if (ucmode() == 1 && dir) {
                for (unsigned k = 0; k < 11; ++k) wait(t_bit(k, f));
                ctlw0_ &= uint16_t(~TXADDR);
            }
            std::vector<bool> bits;
            bits_trama(dato, dir, bits);
            const unsigned n_par = (ctlw0_ & SPB) ? 2u : 1u;
            if (brk) {
                // El break: todo a cero, paradas incluidas, y luego reposo
                o_simo_ = false; publica();
                for (unsigned k = 0; k < 1u + bits.size() + n_par; ++k) wait(t_bit(k, f));
                ctlw0_ &= uint16_t(~TXBRK);
            } else {
                o_simo_ = false; publica();                  // arranque
                wait(t_bit(0, f));
                for (unsigned k = 0; k < bits.size(); ++k) {
                    o_simo_ = bits[k]; publica();
                    wait(t_bit(k + 1u, f));
                    if (en_reset()) break;
                }
                if (ucmode() == 2) ctlw0_ &= uint16_t(~TXADDR);
            }
            o_simo_ = true; publica();
            if (!en_reset())
                for (unsigned k = 0; k < n_par; ++k) wait(t_bit(1u + bits.size() + k, f));
            tx_ocupado_ = false;
            if (!en_reset()) {
                ++n_tx_;
                if (!txlleno_) { ifg_ |= TXCPTIFG; actualiza_irq(); }
            }
        }
    }

    bool linea_rx() const { return listen() ? tx_interno_.read() : somi_in.read(); }
    const sc_core::sc_event& flanco_rx() const {
        return listen() ? tx_interno_.value_changed_event() : somi_in.value_changed_event();
    }

    void uart_rx_proc() {
        for (;;) {
            const double f = brclk();
            if (!uart() || en_reset() || f <= 0.0) { wait(ev_ | flanco_rx()); continue; }
            if (!linea_rx()) {
                // Línea baja (break en curso o arranque a medias): esperar a que suba
                wait(flanco_rx() | ev_);
                continue;
            }
            wait(flanco_rx() | ev_);
            if (linea_rx() || !uart() || en_reset()) continue;
            // Flanco de bajada: el bit de arranque
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            const double fr = brclk();
            if (fr <= 0.0) continue;
            // Línea ociosa: diez bits o más de reposo antes de este carácter
            sc_core::sc_time diez = sc_core::SC_ZERO_TIME;
            for (unsigned k = 0; k < 10; ++k) diez += t_bit(k, fr);
            const bool tras_reposo = (t0 - fin_rx_) >= diez;
            ifg_ |= STTIFG;
            actualiza_irq();
            rx_ocupado_ = true;
            // Centro del de arranque
            wait(t_bit(0, fr) / 2.0);
            if (linea_rx()) { rx_ocupado_ = false; continue; }       // falso arranque
            const unsigned n = bits_datos();
            const bool msb = (ctlw0_ & MSB) != 0;
            const bool pen = (ctlw0_ & PEN) != 0;
            const bool m2 = ucmode() == 2;
            const unsigned n_bits = n + (m2 ? 1u : 0u) + (pen ? 1u : 0u);
            std::vector<bool> b;
            sc_core::sc_time resto = t_bit(0, fr) / 2.0;
            for (unsigned k = 0; k < n_bits + 1u; ++k) {           // + la parada
                const sc_core::sc_time tb = t_bit(k + 1u, fr);
                wait(resto + tb / 2.0);
                resto = tb / 2.0;
                b.push_back(linea_rx());
                if (en_reset()) break;
            }
            if (en_reset()) { rx_ocupado_ = false; continue; }
            // El final del carácter: medio bit de parada más
            wait(resto);
            fin_rx_ = sc_core::sc_time_stamp();
            rx_ocupado_ = false;
            uint16_t dato = 0;
            unsigned unos = 0;
            for (unsigned i = 0; i < n; ++i) {
                if (b[i]) { dato = uint16_t(dato | (1u << (msb ? (n - 1u - i) : i))); ++unos; }
            }
            const bool bit_dir = m2 ? b[n] : false;
            if (m2) unos += bit_dir;
            bool err_par = false;
            if (pen) {
                const bool par = (ctlw0_ & PAR) != 0;
                const bool esperado = par ? (unos & 1u) : !(unos & 1u);
                err_par = b[n + (m2 ? 1u : 0u)] != esperado;
            }
            const bool parada = b.back();
            bool todo_cero = !parada;
            for (bool v : b) if (v) todo_cero = false;
            ++n_rx_;
            // El break
            if (todo_cero) {
                statw_ |= uint16_t(ST_BRK | ST_FE | ST_RXERR);
                rxbuf_ = 0;
                if (ctlw0_ & BRKIE) ifg_ |= RXIFG;
                actualiza_irq();
                continue;
            }
            const bool es_dir = (ucmode() == 1 && tras_reposo) || (m2 && bit_dir);
            // Dormido: solo las direcciones llegan [TRM 24.3.3]
            if ((ctlw0_ & DORM) && (ucmode() == 1 || m2) && !es_dir) continue;
            const bool err = !parada || err_par;
            if (!parada) statw_ |= ST_FE;
            if (err_par) statw_ |= ST_PE;
            if (err) statw_ |= ST_RXERR;
            if (err && !(ctlw0_ & RXEIE)) { actualiza_irq(); continue; }   // no llega
            if (ifg_ & RXIFG) statw_ |= uint16_t(ST_OE | ST_RXERR);        // el anterior se pierde
            rxbuf_ = dato;
            if (ucmode() == 1 || m2) {
                if (es_dir) statw_ |= ST_ADDR_IDLE; else statw_ &= uint16_t(~ST_ADDR_IDLE);
            }
            ifg_ |= RXIFG;
            actualiza_irq();
        }
    }

    // =========================================================================
    // SPI
    // =========================================================================
    bool ste_activo_entrada() const {
        if (ucmode() == 0) return true;                       // 3 hilos: siempre
        const bool v = ste_in.read();
        return ucmode() == 1 ? v : !v;                        // 1: activo alto; 2: activo bajo
    }
    void spi_proc() {
        for (;;) {
            if (!spi() || en_reset()) { wait(ev_); continue; }
            if (ctlw0_ & MST) spi_maestro(); else spi_esclavo();
        }
    }
    void spi_maestro() {
        const double f = brclk();
        if (!txlleno_ || f <= 0.0) {
            if (txlleno_) aviso_uclk(f);
            wait(ev_);
            return;
        }
        const bool cuatro = ucmode() == 1 || ucmode() == 2;
        // Conflicto: con UCSTEM = 0, otro maestro activa STE
        if (cuatro && !(ctlw0_ & STEM) && ste_activo_entrada()) {
            statw_ |= ST_FE;
            o_simo_oe_ = false; o_clk_oe_ = false; publica();
            wait(ste_in.value_changed_event() | ev_);
            lineas_en_reposo();
            return;
        }
        const unsigned br = brw_ ? brw_ : 1u;
        const sc_core::sc_time medio(double(br) * 1e12 / f / 2.0, sc_core::SC_PS);
        const unsigned n = bits_datos();
        const bool msb = (ctlw0_ & MSB) != 0;
        const bool ckph = (ctlw0_ & CKPH) != 0;
        const bool ckpl = (ctlw0_ & CKPL) != 0;
        uint16_t dato = txbuf_;
        txlleno_ = false;
        tx_ocupado_ = true;
        ifg_ |= TXIFG;
        actualiza_irq();
        if (cuatro && (ctlw0_ & STEM)) { o_ste_ = !ste_inactivo(); publica(); }
        uint16_t rx = 0;
        // CKPH = 1: el dato se captura en el primer flanco y cambia en el
        // siguiente; CKPH = 0: cambia en el primero y se captura en el segundo
        // [TRM 25.3.6]. Con CKPH = 1 el primer bit sale antes del primer flanco.
        for (unsigned i = 0; i < n; ++i) {
            const unsigned k = msb ? (n - 1u - i) : i;
            const bool bit = (dato >> k) & 1u;
            if (ckph) {
                o_simo_ = bit; publica();
                wait(medio);
                o_clk_ = !ckpl; publica();                       // captura
                wait(sc_core::SC_ZERO_TIME);
                if (spi_rx_linea()) rx = uint16_t(rx | (1u << k));
                wait(medio);
                o_clk_ = ckpl; publica();
            } else {
                o_clk_ = !ckpl; o_simo_ = bit; publica();        // cambia
                wait(medio);
                o_clk_ = ckpl; publica();                        // captura
                wait(sc_core::SC_ZERO_TIME);
                if (spi_rx_linea()) rx = uint16_t(rx | (1u << k));
                wait(medio);
            }
            if (en_reset() || !spi()) { tx_ocupado_ = false; lineas_en_reposo(); return; }
        }
        ++n_spi_;
        if (ifg_ & RXIFG) statw_ |= ST_OE;
        rxbuf_ = rx;
        ifg_ |= RXIFG;
        tx_ocupado_ = false;
        if (cuatro && (ctlw0_ & STEM) && !txlleno_) { o_ste_ = ste_inactivo(); publica(); }
        actualiza_irq();
    }
    bool spi_rx_linea() const { return listen() ? tx_interno_.read() : somi_in.read(); }

    // Esclavo: el reloj lo pone el maestro; con STE inactivo, SOMI suelto
    void spi_esclavo() {
        const unsigned n = bits_datos();
        const bool msb = (ctlw0_ & MSB) != 0;
        const bool ckph = (ctlw0_ & CKPH) != 0;
        const bool ckpl = (ctlw0_ & CKPL) != 0;
        if (!ste_activo_entrada()) {
            o_somi_oe_ = false; publica();
            wait(ste_in.value_changed_event() | ev_);
            return;
        }
        // El dato a sacar: el de TXBUF si lo hay; si no, el último
        uint16_t dato = txbuf_;
        if (txlleno_) { txlleno_ = false; ifg_ |= TXIFG; actualiza_irq(); }
        o_somi_oe_ = true;
        unsigned i = 0;
        uint16_t rx = 0;
        auto saca = [&](unsigned j) {
            const unsigned k = msb ? (n - 1u - j) : j;
            o_somi_ = (dato >> k) & 1u; publica();
        };
        if (ckph) saca(0);
        while (i < n) {
            wait(clk_in.value_changed_event() | ste_in.value_changed_event() | ev_);
            if (en_reset() || !spi() || (ctlw0_ & MST)) return;
            if (!ste_activo_entrada()) { o_somi_oe_ = false; publica(); return; }
            if (!clk_in.event()) continue;
            const bool primero = clk_in.read() != ckpl;          // sale del reposo
            const bool captura = ckph ? primero : !primero;
            const unsigned k = msb ? (n - 1u - i) : i;
            if (captura) {
                rx_ocupado_ = true;
                if (simo_in.read()) rx = uint16_t(rx | (1u << k));
                ++i;
                if (ckph && i < n) { /* el siguiente sale en el flanco de cambio */ }
            } else {
                if (ckph) { if (i < n) saca(i); }
                else saca(i);
            }
        }
        rx_ocupado_ = false;
        ++n_spi_;
        if (ifg_ & RXIFG) statw_ |= ST_OE;
        rxbuf_ = rx;
        ifg_ |= RXIFG;
        actualiza_irq();
    }
};

inline double EusciP4::baudios() const {
    const double f = f_brclk ? f_brclk((ctlw0_ >> 6) & 3u) : 0.0;
    if (f <= 0.0) return 0.0;
    double c = 0.0;
    for (unsigned k = 0; k < 8; ++k) c += ciclos_bit(k);
    return f / (c / 8.0);
}

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_EUSCI_H
