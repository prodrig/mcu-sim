// =============================================================================
// spi.h — SPI1/2/3 e I2S2/I2S3 (+ I2S2ext/I2S3ext) [IR, §12.5, §12.7]
//
// El STM32F407VG lleva CINCO instancias del mismo bloque de diseño, con tres
// juegos de recursos distintos:
//
//   SPI1                 SPI puro, en APB2 (SCK hasta 42 MHz [IR, §12.5.1]).
//                        NO tiene modo I2S: en el F407 el audio vive en 2 y 3.
//   SPI2, SPI3           SPI + I2S completos, en APB1 (SCK hasta 21 MHz). El
//                        modo se elige con I2SCFGR.I2SMOD [IR, §12.7].
//   I2S2ext, I2S3ext     bloques de EXTENSIÓN que solo hacen I2S y solo como
//                        esclavo: son la mitad que le falta a I2S2/I2S3 para
//                        ser full-duplex. Comparten el reloj y la palabra de
//                        sincronismo del bloque principal y usan el pin MISO
//                        como SD [IR, §2.1: I2S2ext_SD en PB14/PC2].
//
// El modelo es UNO SOLO y el tipo se selecciona con parámetros, de dos maneras
// equivalentes:
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using Spi     = SpiT<CAPS_SPI_APB2>;    // SPI1
//         using SpiI2s  = SpiT<CAPS_SPI_I2S>;     // SPI2, SPI3
//         using I2sExt  = SpiT<CAPS_I2S_EXT>;     // I2S2ext, I2S3ext
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         SpiBase s{"s", base, SpiCaps{...}};
//         SpiBase t{"t", base, /*i2s=*/true, /*f_max=*/21e6};   // atajo
//
// Los rasgos gobiernan las MÁSCARAS DE ESCRITURA de los registros, de modo que
// en un SPI1 el registro I2SCFGR queda reservado y lee cero, y en un I2S2ext lo
// hace CR1 entero, exactamente igual que en el silicio.
//
// Fase F5 — implementado:
//   * banco de registros CR1/CR2/SR/DR/CRCPR/RXCRCR/TXCRCR/I2SCFGR/I2SPR
//     [IR, §12.5.3];
//   * desplazador full-duplex a NIVEL DE BIT sobre los pines, con las cuatro
//     combinaciones de CPOL/CPHA, tramas de 8 o 16 bits y MSB o LSB primero;
//   * maestro con prescalador BR y esclavo dirigido por los flancos del pin
//     SCK, con la temporización correcta de la primera muestra;
//   * gestión de NSS por hardware y por software (SSM/SSI/SSOE) y fallo de modo
//     (MODF) cuando otro maestro tira de NSS;
//   * modos de conectividad: full-duplex, solo recepción (RXONLY) y
//     bidireccional de un hilo (BIDIMODE/BIDIOE);
//   * CRC de hardware de 8/16 bits con CRCNEXT y la bandera CRCERR;
//   * banderas TXE/RXNE/BSY/OVR/MODF/UDR/CHSIDE con su semántica de borrado,
//     interrupciones (TXEIE/RXNEIE/ERRIE) y peticiones de DMA;
//   * modo I2S: maestro y esclavo, transmisor y receptor, estándares Philips,
//     MSB y LSB justificados, longitudes de dato/canal 16/24/32, generador de
//     reloj de audio con I2SDIV/ODD y salida de reloj maestro MCK.
// =============================================================================
#ifndef STM32_PERIPH_SPI_H
#define STM32_PERIPH_SPI_H

#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos de la instancia. Es lo único que distingue un SPI1 de un I2S2ext.
// ---------------------------------------------------------------------------
struct SpiCaps {
    bool spi_mode   = true;   // el bloque puede funcionar como SPI (CR1 útil)
    bool i2s_mode   = false;  // ... y como I2S (I2SCFGR / I2SPR)
    bool i2s_master = true;   // puede GENERAR el reloj de audio (divisor, MCK)
    bool crc        = true;   // generador de CRC de hardware
    bool ti_mode    = true;   // CR2.FRF: formato de trama TI
    bool nss_pin    = true;   // gestión de NSS por hardware (SSOE, MODF)
    bool sd_on_miso = false;  // el dato de I2S sale/entra por MISO, no por MOSI
    double max_sck_hz = 21.0e6;   // límite de la hoja de características
    const char* kind  = "SPI";
};

// --- Las tres variantes del STM32F407VG ------------------------------------
constexpr SpiCaps caps_spi_apb2() {          // SPI1 [IR, §12.5.2]
    SpiCaps c{};
    c.i2s_mode = false;                      // en el F407 el I2S es de SPI2/3
    c.max_sck_hz = 42.0e6;                   // APB2 a 84 MHz -> SCK <= 42 MHz
    c.kind = "SPI (APB2)";
    return c;
}
constexpr SpiCaps caps_spi_i2s() {           // SPI2, SPI3 [IR, §12.5.2, §12.7]
    SpiCaps c{};
    c.i2s_mode = true;
    c.max_sck_hz = 21.0e6;                   // APB1 a 42 MHz -> SCK <= 21 MHz
    c.kind = "SPI/I2S (APB1)";
    return c;
}
constexpr SpiCaps caps_i2s_ext() {           // I2S2ext, I2S3ext [IR, mapa APB1]
    SpiCaps c{};
    c.spi_mode = false;                      // solo audio
    c.i2s_mode = true;
    c.i2s_master = false;                    // siempre esclavo del bloque padre
    c.crc = false; c.ti_mode = false; c.nss_pin = false;
    c.sd_on_miso = true;                     // su dato va por el pin MISO
    c.max_sck_hz = 21.0e6;
    c.kind = "I2Sxext";
    return c;
}

inline constexpr SpiCaps CAPS_SPI_APB2 = caps_spi_apb2();
inline constexpr SpiCaps CAPS_SPI_I2S  = caps_spi_i2s();
inline constexpr SpiCaps CAPS_I2S_EXT  = caps_i2s_ext();

// ---------------------------------------------------------------------------
// Implementación común. Recibe los rasgos por el constructor: este es el punto
// de selección en tiempo de ejecución.
// ---------------------------------------------------------------------------
class SpiBase : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // Señales de función alternativa (el top registra los endpoints en pin_mux).
    // En modo I2S los mismos pines son CK (SCK), WS (NSS) y SD (MOSI, o MISO en
    // los bloques de extensión); MCK tiene pin propio.
    sc_core::sc_signal<bool> sck_out{"sck_out"}, sck_oe{"sck_oe"}, sck_in{"sck_in"};
    sc_core::sc_signal<bool> miso_out{"miso_out"}, miso_oe{"miso_oe"}, miso_in{"miso_in"};
    sc_core::sc_signal<bool> mosi_out{"mosi_out"}, mosi_oe{"mosi_oe"}, mosi_in{"mosi_in"};
    sc_core::sc_signal<bool> nss_out{"nss_out"}, nss_oe{"nss_oe"}, nss_in{"nss_in"};
    sc_core::sc_signal<bool> mck_out{"mck_out"}, mck_oe{"mck_oe"};
    // Reloj de audio: onda y frecuencia (PLLI2S R, o I2S_CKIN) [IR, §12.7]
    sc_core::sc_in<bool>   i2s_ext_clk{"i2s_ext_clk"};
    sc_core::sc_in<double> i2s_clk_hz{"i2s_clk_hz"};
    // CK y WS COMPARTIDOS con el bloque principal. Un I2SxEXT no tiene pines de
    // reloj ni de sincronismo propios: cuelga de los mismos hilos que su SPI
    // padre y solo aporta su pin de datos [IR, §2.1]. El top los ata a la
    // entrada del pad de esos pines; en el resto de variantes quedan a cero.
    sc_core::sc_in<bool> ext_ck{"ext_ck"}, ext_ws{"ext_ws"};

    // ---- Offsets [IR, §12.5.3] --------------------------------------------
    enum : uint32_t { R_CR1 = 0x00, R_CR2 = 0x04, R_SR = 0x08, R_DR = 0x0C,
                      R_CRCPR = 0x10, R_RXCRCR = 0x14, R_TXCRCR = 0x18,
                      R_I2SCFGR = 0x1C, R_I2SPR = 0x20 };
    enum SrBit : uint32_t {
        S_RXNE = 1u << 0, S_TXE = 1u << 1, S_CHSIDE = 1u << 2, S_UDR = 1u << 3,
        S_CRCERR = 1u << 4, S_MODF = 1u << 5, S_OVR = 1u << 6, S_BSY = 1u << 7,
        S_FRE = 1u << 8
    };

    // --- Constructor principal: los rasgos como parámetro ------------------
    SpiBase(sc_core::sc_module_name nm, uint32_t base,
            const SpiCaps& caps = CAPS_SPI_I2S)
        : BusSlave(nm, base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(SpiBase);
        SC_THREAD(master_proc);
        SC_METHOD(pub_proc);    sensitive << pub_ev_;
        SC_METHOD(reset_proc);  sensitive << rst_n;
        // El esclavo lo dirigen los flancos del pin, no un reloj propio: por eso
        // funciona aunque el maestro externo vaya a una frecuencia cualquiera.
        SC_METHOD(slave_sck_proc);
        sensitive << sck_in << nss_in << ext_ck << ext_ws;
        dont_initialize();
        SC_METHOD(nss_proc);       sensitive << nss_in;  dont_initialize();
        SC_METHOD(clk_proc);       sensitive << clk_hz << i2s_clk_hz;
                                   dont_initialize();
    }
    // --- Atajo de selección en tiempo de ejecución -------------------------
    SpiBase(sc_core::sc_module_name nm, uint32_t base, bool i2s, double f_max)
        : SpiBase(nm, base, runtime_caps(i2s, f_max)) {}

    static SpiCaps runtime_caps(bool i2s, double f_max) {
        SpiCaps c{};
        c.i2s_mode = i2s;
        c.max_sck_hz = f_max;
        c.kind = "a medida";
        return c;
    }

    const SpiCaps& caps() const { return caps_; }

    // ---- Observación desde el banco de pruebas ----------------------------
    double   sck_hz()    const { return sck_hz_; }
    double   i2s_fs_hz() const { return i2s_fs_; }
    uint64_t frames()    const { return n_frames_; }
    uint32_t sr_raw()    const { return sr_; }
    bool     i2s_active()const { return caps_.i2s_mode && i2smod() && i2se(); }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR1:     return cr1_;
            case R_CR2:     return cr2_;
            case R_SR:      sr_read_ = true; return sr_;
            case R_DR:      return read_dr();
            case R_CRCPR:   return caps_.crc ? crcpr_ : 0u;
            case R_RXCRCR:  return caps_.crc ? rxcrc_ : 0u;
            case R_TXCRCR:  return caps_.crc ? txcrc_ : 0u;
            case R_I2SCFGR: return caps_.i2s_mode ? i2scfgr_ : 0u;
            case R_I2SPR:   return caps_.i2s_mode ? i2spr_ : 0u;
            default:        return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                  // acceso parcial
            uint32_t cur = (off == R_DR) ? uint32_t(tdr_) : reg_read_quiet(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR1: {
                const uint32_t old = cr1_;
                cr1_ = v & cr1_mask();
                if ((old ^ cr1_) & (1u << 6)) {             // SPE 0<->1
                    if (spe()) start_up(); else shut_down();
                }
                if ((old ^ cr1_) & 0x0038u) recompute_sck();   // BR
                break;
            }
            case R_CR2:  cr2_ = v & cr2_mask(); break;
            case R_SR:
                // MODF, OVR, CRCERR, UDR se borran con secuencias de lectura;
                // solo CRCERR admite el borrado escribiendo cero [IR, §12.5.3-B].
                if (caps_.crc && !(v & S_CRCERR)) sr_ &= ~S_CRCERR;
                break;
            case R_DR:   write_dr(uint16_t(v & 0xFFFFu)); break;
            case R_CRCPR: if (caps_.crc && !spe()) crcpr_ = v & 0xFFFFu; break;
            case R_I2SCFGR: {
                if (!caps_.i2s_mode) return;
                const uint32_t old = i2scfgr_;
                i2scfgr_ = v & i2scfgr_mask();
                if ((old ^ i2scfgr_) & (1u << 10)) {        // I2SE 0<->1
                    if (i2se()) start_up(); else shut_down();
                }
                recompute_i2s();
                break;
            }
            case R_I2SPR:
                if (!caps_.i2s_mode) return;
                i2spr_ = v & i2spr_mask();
                recompute_i2s();
                break;
            default: return;
        }
        update_irq();
        wake();
    }

private:
    SpiCaps caps_;
    // ---- Registros --------------------------------------------------------
    uint32_t cr1_ = 0, cr2_ = 0, sr_ = S_TXE, crcpr_ = 0x0007u;
    uint32_t rxcrc_ = 0, txcrc_ = 0, i2scfgr_ = 0, i2spr_ = 0x0002u;
    uint16_t tdr_ = 0, rdr_ = 0;
    bool     tdr_full_ = false;            // hay dato pendiente de transmitir
    bool     sr_read_  = false;            // se ha leído SR (borrado de flags)
    bool     crc_next_pending_ = false;    // el siguiente marco es el CRC
    double   sck_hz_ = 0.0, i2s_fs_ = 0.0, i2s_bit_hz_ = 0.0;
    uint64_t n_frames_ = 0;
    // ---- Desplazador del esclavo (dirigido por los flancos del pin) -------
    uint16_t sh_tx_ = 0, sh_rx_ = 0;
    unsigned bit_i_ = 0;
    bool     slave_armed_ = false;
    bool     ws_prev_ = false;
    // ---- Salidas publicadas por un único proceso --------------------------
    bool o_irq_ = false, o_drq_rx_ = false, o_drq_tx_ = false;
    bool o_sck_ = false, o_sck_oe_ = false;
    bool o_miso_ = false, o_miso_oe_ = false;
    bool o_mosi_ = false, o_mosi_oe_ = false;
    bool o_nss_ = true,  o_nss_oe_ = false;
    bool o_mck_ = false, o_mck_oe_ = false;
    sc_core::sc_event pub_ev_, wake_ev_;

    // =======================================================================
    // Campos de los registros
    // =======================================================================
    bool     cpha()     const { return cr1_ & 1u; }
    bool     cpol()     const { return (cr1_ >> 1) & 1u; }
    bool     mstr()     const { return (cr1_ >> 2) & 1u; }
    unsigned br()       const { return (cr1_ >> 3) & 7u; }
    bool     spe()      const { return (cr1_ >> 6) & 1u; }
    bool     lsbfirst() const { return (cr1_ >> 7) & 1u; }
    bool     ssi()      const { return (cr1_ >> 8) & 1u; }
    bool     ssm()      const { return (cr1_ >> 9) & 1u; }
    bool     rxonly()   const { return (cr1_ >> 10) & 1u; }
    bool     dff()      const { return (cr1_ >> 11) & 1u; }
    bool     crcnext()  const { return caps_.crc && ((cr1_ >> 12) & 1u); }
    bool     crcen()    const { return caps_.crc && ((cr1_ >> 13) & 1u); }
    bool     bidioe()   const { return (cr1_ >> 14) & 1u; }
    bool     bidimode() const { return (cr1_ >> 15) & 1u; }
    bool     rxdmaen()  const { return cr2_ & 1u; }
    bool     txdmaen()  const { return (cr2_ >> 1) & 1u; }
    bool     ssoe()     const { return caps_.nss_pin && ((cr2_ >> 2) & 1u); }
    bool     frf()      const { return caps_.ti_mode && ((cr2_ >> 4) & 1u); }
    bool     errie()    const { return (cr2_ >> 5) & 1u; }
    bool     rxneie()   const { return (cr2_ >> 6) & 1u; }
    bool     txeie()    const { return (cr2_ >> 7) & 1u; }
    // I2S [IR, §12.5.3-D]
    bool     chlen32()  const { return i2scfgr_ & 1u; }
    unsigned datlen()   const { return (i2scfgr_ >> 1) & 3u; }   // 00:16 01:24 10:32
    bool     ckpol()    const { return (i2scfgr_ >> 3) & 1u; }
    unsigned i2sstd()   const { return (i2scfgr_ >> 4) & 3u; }   // 00 Philips
    bool     pcmsync()  const { return (i2scfgr_ >> 7) & 1u; }
    unsigned i2scfg()   const { return (i2scfgr_ >> 8) & 3u; }   // 00 sT 01 sR 10 mT 11 mR
    bool     i2se()     const { return (i2scfgr_ >> 10) & 1u; }
    bool     i2smod()   const { return (i2scfgr_ >> 11) & 1u; }
    unsigned i2sdiv()   const { return i2spr_ & 0xFFu; }
    bool     i2sodd()   const { return (i2spr_ >> 8) & 1u; }
    bool     mckoe()    const { return caps_.i2s_master && ((i2spr_ >> 9) & 1u); }
    bool     i2s_is_master() const { return i2scfg() >= 2u; }
    bool     i2s_is_tx()     const { return (i2scfg() & 1u) == 0u; }

    // Número de bits por marco: SPI 8/16, I2S la longitud de canal
    unsigned frame_bits() const {
        if (i2s_active()) return chlen32() ? 32u : 16u;
        return dff() ? 16u : 8u;
    }
    // Bits significativos del dato en I2S (DATLEN)
    unsigned data_bits() const {
        switch (datlen()) { case 1: return 24u; case 2: return 32u; default: return 16u; }
    }

    // =======================================================================
    // Máscaras de escritura: aquí los rasgos se vuelven observables
    // =======================================================================
    uint32_t cr1_mask() const {
        if (!caps_.spi_mode) return 0u;              // I2Sxext: CR1 reservado
        uint32_t m = 0xFFFFu;
        if (!caps_.crc) m &= ~((1u << 13) | (1u << 12));    // CRCEN, CRCNEXT
        return m;
    }
    uint32_t cr2_mask() const {
        uint32_t m = (1u << 7) | (1u << 6) | (1u << 5);     // TXEIE, RXNEIE, ERRIE
        m |= (1u << 1) | (1u << 0);                         // TXDMAEN, RXDMAEN
        if (caps_.spi_mode && caps_.ti_mode) m |= 1u << 4;  // FRF
        if (caps_.spi_mode && caps_.nss_pin) m |= 1u << 2;  // SSOE
        return m;
    }
    uint32_t i2scfgr_mask() const {
        if (!caps_.i2s_mode) return 0u;
        uint32_t m = 0x0FBFu;              // I2SMOD, I2SE, I2SCFG, I2SSTD, CKPOL,
                                           // DATLEN, CHLEN y PCMSYNC
        if (!caps_.i2s_master) m &= ~(1u << 9);   // solo esclavo: I2SCFG[1] fijo
        return m;
    }
    uint32_t i2spr_mask() const {
        if (!caps_.i2s_mode) return 0u;
        // El divisor y MCK solo tienen sentido en un maestro de audio.
        return caps_.i2s_master ? 0x03FFu : 0x0000u;
    }

    // =======================================================================
    // Relojes
    // =======================================================================
    void clk_proc() { recompute_sck(); recompute_i2s(); wake(); }

    // f_SCK = f_PCLK / 2^(BR+1) [IR, §12.5.3-A]
    void recompute_sck() {
        const double f = clk_hz.read();
        sck_hz_ = (f > 0.0) ? f / double(1u << (br() + 1u)) : 0.0;
        if (sck_hz_ > caps_.max_sck_hz * 1.001)
            SC_REPORT_WARNING("spi", "SCK por encima del maximo de la variante");
    }
    // I2S: con MCKOE = 0, f_CK = I2SCLK / (2*I2SDIV + ODD); la frecuencia de
    // muestreo sale de dividir por los bits de las dos ranuras del canal.
    // Con MCKOE = 1 el reloj maestro es 256*F_S [IR, §12.7].
    void recompute_i2s() {
        const double f = i2s_clk_hz.read();
        const double div = double(2u * i2sdiv() + (i2sodd() ? 1u : 0u));
        if (f <= 0.0 || div <= 0.0 || i2sdiv() < 2u) { i2s_bit_hz_ = i2s_fs_ = 0.0; return; }
        const double slot = double(chlen32() ? 32u : 16u);
        if (mckoe()) {
            i2s_fs_     = f / (256.0 * div);
            i2s_bit_hz_ = i2s_fs_ * 2.0 * slot;
        } else {
            i2s_bit_hz_ = f / div;
            i2s_fs_     = i2s_bit_hz_ / (2.0 * slot);
        }
    }
    sc_core::sc_time bit_time() const {
        const double f = i2s_active() ? i2s_bit_hz_ : sck_hz_;
        return (f > 0.0) ? sc_core::sc_time(1.0e12 / f, sc_core::SC_PS)
                         : sc_core::sc_time(1, sc_core::SC_MS);
    }

    // =======================================================================
    // Registro de datos
    // =======================================================================
    uint32_t reg_read_quiet(uint32_t off) {            // sin efectos laterales
        switch (off) {
            case R_CR1: return cr1_;   case R_CR2: return cr2_;
            case R_SR:  return sr_;    case R_DR:  return rdr_;
            case R_CRCPR: return crcpr_;
            case R_I2SCFGR: return i2scfgr_; case R_I2SPR: return i2spr_;
            default: return 0;
        }
    }
    uint32_t read_dr() {
        const uint32_t v = rdr_;
        sr_ &= ~S_RXNE;
        if (sr_read_) sr_ &= ~S_OVR;                   // leer SR y luego DR
        sr_read_ = false;
        update_irq();
        wake();
        return v;
    }
    void write_dr(uint16_t v) {
        tdr_ = v;
        tdr_full_ = true;
        sr_ &= ~S_TXE;
        if (sr_read_) sr_ &= ~S_UDR;
        sr_read_ = false;
        // Un esclavo entre marcos ya está enganchado a la línea de reloj: el
        // dato recién escrito tiene que entrar en el desplazador AHORA y, con
        // CPHA = 0, salir ya el primer bit al pin, porque el maestro puede
        // empezar a dar flancos en cualquier momento.
        if (!i2s_active() && caps_.spi_mode && spe() && !mstr() && bit_i_ == 0)
            start_slave_frame();
        update_irq();
        wake();
    }

    // =======================================================================
    // NSS y fallo de modo
    // =======================================================================
    // Nivel efectivo de la selección de esclavo: con SSM manda SSI, si no el pin.
    bool nss_level() const {
        if (!caps_.nss_pin) return false;              // sin pin: siempre activo
        return ssm() ? ssi() : nss_in.read();
    }
    bool selected() const { return !nss_level(); }     // NSS es activo en bajo

    void nss_proc() { check_nss(); }
    void check_nss() {
        if (!caps_.spi_mode || !spe()) return;
        if (mstr() && !ssm() && !ssoe() && !nss_in.read()) {
            // Otro maestro ha tomado el bus: fallo de modo [IR, §12.5.3-B]
            sr_ |= S_MODF;
            cr1_ &= ~((1u << 6) | (1u << 2));          // el hardware borra SPE y MSTR
            shut_down();
            update_irq();
            return;
        }
        if (!mstr()) { start_slave_frame(); wake(); }
    }

    // =======================================================================
    // Salidas
    // =======================================================================
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void wake()    { wake_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq.write(o_irq_);
        dma_req_rx.write(o_drq_rx_);
        dma_req_tx.write(o_drq_tx_);
        sck_out.write(o_sck_);   sck_oe.write(o_sck_oe_);
        miso_out.write(o_miso_); miso_oe.write(o_miso_oe_);
        mosi_out.write(o_mosi_); mosi_oe.write(o_mosi_oe_);
        nss_out.write(o_nss_);   nss_oe.write(o_nss_oe_);
        mck_out.write(o_mck_);   mck_oe.write(o_mck_oe_);
    }

    void update_irq() {
        bool i = false;
        if (txeie()  && (sr_ & S_TXE))  i = true;
        if (rxneie() && (sr_ & S_RXNE)) i = true;
        if (errie() && (sr_ & (S_OVR | S_MODF | S_CRCERR | S_UDR | S_FRE))) i = true;
        o_irq_ = enabled() && i;
        // Peticiones de DMA: de nivel, las retira el acceso a DR
        o_drq_rx_ = enabled() && rxdmaen() && (sr_ & S_RXNE);
        o_drq_tx_ = enabled() && txdmaen() && (sr_ & S_TXE);
        publish();
    }

    bool enabled() const {
        if (!clock_enabled() || !rst_n.read()) return false;
        return i2s_active() || (caps_.spi_mode && spe() && !i2smod());
    }

    // CK y WS efectivos: los del bloque de extensión vienen del bloque padre
    bool ck_level() const { return caps_.sd_on_miso ? ext_ck.read() : sck_in.read(); }
    bool ws_level() const { return caps_.sd_on_miso ? ext_ws.read() : nss_in.read(); }

    // Dirección efectiva del pin de datos de salida
    void drive_data(bool level) {
        if (caps_.sd_on_miso) { o_miso_ = level; }
        else if (i2s_active()) { o_mosi_ = level; }
        else if (mstr())       { o_mosi_ = level; }
        else                   { o_miso_ = level; }
        publish();
    }
    bool read_data() const {
        if (caps_.sd_on_miso) return miso_in.read();
        if (i2s_active())     return mosi_in.read();
        return mstr() ? miso_in.read() : mosi_in.read();
    }

    void start_up() {
        recompute_sck();
        recompute_i2s();
        bit_i_ = 0; slave_armed_ = false; align_ = 0;
        // La referencia del detector de flancos del esclavo es el NIVEL DE
        // REPOSO que hay en el pin al habilitarse; sin esto, con CPOL = 1 el
        // primer flanco de captura se perdería.
        sck_prev_ = sck_in.read();
        ws_prev_  = ws_level();
        if (i2s_active()) {
            const bool master = i2s_is_master();
            const bool tx = i2s_is_tx();
            o_sck_oe_ = master; o_nss_oe_ = master;
            o_sck_ = ckpol(); o_nss_ = false;
            o_mck_oe_ = master && mckoe();
            if (caps_.sd_on_miso) { o_miso_oe_ = tx; o_mosi_oe_ = false; }
            else                  { o_mosi_oe_ = tx; o_miso_oe_ = false; }
        } else {
            o_sck_oe_  = mstr();
            o_sck_     = cpol();
            o_nss_oe_  = mstr() && ssoe();
            o_nss_     = false;                        // NSS activo mientras SPE
            o_mosi_oe_ = mstr() && !(rxonly() || (bidimode() && !bidioe()));
            o_miso_oe_ = !mstr() && !(bidimode() && !bidioe());
            if (!mstr()) start_slave_frame();
            else         check_nss();       // el fallo de modo es por NIVEL
        }
        sr_ |= S_TXE;
        publish();
        wake();
    }
    void shut_down() {
        o_sck_oe_ = o_miso_oe_ = o_mosi_oe_ = o_nss_oe_ = o_mck_oe_ = false;
        sr_ &= ~S_BSY;
        // El desplazador queda vacío: la próxima habilitación empieza un marco
        // nuevo y no continúa el que se quedó a medias.
        bit_i_ = 0; sh_rx_ = 0; align_ = 0; slave_armed_ = false;
        publish();
        wake();
    }

    void reset_proc() {
        if (rst_n.read()) return;
        cr1_ = cr2_ = 0; sr_ = S_TXE; crcpr_ = 0x0007u;
        rxcrc_ = txcrc_ = 0; i2scfgr_ = 0; i2spr_ = 0x0002u;
        tdr_ = rdr_ = 0; tdr_full_ = false; sr_read_ = false;
        crc_next_pending_ = false;
        sh_tx_ = sh_rx_ = 0; bit_i_ = 0; slave_armed_ = false;
        n_frames_ = 0; sck_hz_ = i2s_fs_ = i2s_bit_hz_ = 0.0;
        o_sck_ = o_miso_ = o_mosi_ = o_mck_ = false; o_nss_ = true;
        o_sck_oe_ = o_miso_oe_ = o_mosi_oe_ = o_nss_oe_ = o_mck_oe_ = false;
        update_irq();
        wake();
    }

    // =======================================================================
    // CRC de hardware [IR, §12.5.1]
    // =======================================================================
    // Polinomio de CRCPR aplicado bit a bit, sobre 8 o 16 bits según DFF.
    static uint32_t crc_step(uint32_t crc, uint32_t data, uint32_t poly, unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t top = 1u << (n - 1);
            const bool xr = ((crc ^ (data << 0)) & top) != 0;
            crc = ((crc << 1) & ((1u << n) - 1u));
            if (xr) crc ^= poly;
            data = (data << 1) & ((1u << n) - 1u);
        }
        return crc;
    }
    void crc_accumulate(uint16_t tx, uint16_t rx) {
        if (!crcen()) return;
        const unsigned n = dff() ? 16u : 8u;
        const uint32_t poly = crcpr_ & ((1u << n) - 1u);
        txcrc_ = crc_step(txcrc_, tx & ((1u << n) - 1u), poly, n);
        rxcrc_ = crc_step(rxcrc_, rx & ((1u << n) - 1u), poly, n);
    }

    // =======================================================================
    // Fin de marco: común al maestro y al esclavo
    // =======================================================================
    void frame_done(uint16_t sent, uint16_t got) {
        ++n_frames_;
        if (i2s_active()) {
            // El canal alterna con WS; CHSIDE dice cuál se acaba de mover
            if (!i2s_is_tx()) {
                if (sr_ & S_RXNE) sr_ |= S_OVR;
                else { rdr_ = got; sr_ |= S_RXNE; }
            } else {
                if (!tdr_full_) sr_ |= S_UDR;          // subdesbordamiento
                sr_ |= S_TXE;
            }
            update_irq();
            return;
        }
        // --- SPI ---------------------------------------------------------
        if (crc_next_pending_) {
            // El marco que acaba de llegar era el CRC del otro extremo
            crc_next_pending_ = false;
            cr1_ &= ~(1u << 12);                       // el hardware borra CRCNEXT
            const unsigned n = dff() ? 16u : 8u;
            const uint16_t mine = uint16_t(rxcrc_ & ((1u << n) - 1u));
            if (got != mine) sr_ |= S_CRCERR;
            rxcrc_ = txcrc_ = 0;
            sr_ |= S_TXE;
            update_irq();
            return;
        }
        crc_accumulate(sent, got);
        if (!(bidimode() && bidioe())) {               // hay recepción
            if (sr_ & S_RXNE) sr_ |= S_OVR;            // no se leyó el anterior
            else { rdr_ = got; sr_ |= S_RXNE; }
        }
        sr_ |= S_TXE;
        update_irq();
    }

    // Toma el siguiente dato a transmitir; devuelve false si no hay ninguno.
    bool fetch_tx(uint16_t& out) {
        if (crcen() && crcnext() && !crc_next_pending_) {
            const unsigned n = dff() ? 16u : 8u;
            out = uint16_t(txcrc_ & ((1u << n) - 1u));  // se envía el CRC
            crc_next_pending_ = true;
            return true;
        }
        if (!tdr_full_) return false;
        out = tdr_;
        tdr_full_ = false;
        sr_ |= S_TXE;
        update_irq();
        return true;
    }

    // Orden de bits: MSB primero salvo LSBFIRST (que el I2S no tiene)
    bool bit_of(uint16_t v, unsigned i, unsigned nbits) const {
        if (!i2s_active() && lsbfirst()) return (v >> i) & 1u;
        return (v >> (nbits - 1u - i)) & 1u;
    }
    void put_bit(uint16_t& v, unsigned i, unsigned nbits, bool b) const {
        if (!i2s_active() && lsbfirst()) { if (b) v = uint16_t(v | (1u << i)); }
        else if (b) v = uint16_t(v | (1u << (nbits - 1u - i)));
    }

    // =======================================================================
    // MAESTRO: genera SCK (o CK/WS en I2S) y marca el ritmo
    // =======================================================================
    void master_proc() {
        for (;;) {
            if (!enabled()) {
                wait(wake_ev_ | rst_n.value_changed_event() |
                     clk_hz.value_changed_event() | i2s_clk_hz.value_changed_event());
                continue;
            }
            if (i2s_active()) {
                if (!i2s_is_master()) { wait(wake_ev_); continue; }
                i2s_master_frame();
                continue;
            }
            if (!caps_.spi_mode || !mstr()) { wait(wake_ev_); continue; }
            if (sck_hz_ <= 0.0) { wait(wake_ev_); continue; }
            uint16_t tx = 0;
            const bool have = fetch_tx(tx);
            // En solo-recepción y en bidireccional-entrada el maestro sigue
            // generando reloj aunque no haya nada que enviar [IR, §12.5.1].
            const bool rx_driven = rxonly() || (bidimode() && !bidioe());
            if (!have && !rx_driven) {
                sr_ &= ~S_BSY; publish();
                wait(wake_ev_ | rst_n.value_changed_event());
                continue;
            }
            spi_master_frame(tx);
        }
    }

    void spi_master_frame(uint16_t tx) {
        const unsigned nb = frame_bits();
        const sc_core::sc_time h = bit_time() / 2.0;
        sr_ |= S_BSY;
        o_nss_ = false;                                 // NSS activo (SSOE)
        publish();
        uint16_t rx = 0;
        bool aborted = false;
        for (unsigned i = 0; i < nb; ++i) {
            if (!cpha()) {
                drive_data(bit_of(tx, i, nb));
                wait(h);
                o_sck_ = !cpol(); publish();            // flanco de captura
                if (read_data()) put_bit(rx, i, nb, true);
                wait(h);
                o_sck_ = cpol(); publish();
            } else {
                o_sck_ = !cpol();                        // flanco de preparación
                drive_data(bit_of(tx, i, nb));
                publish();
                wait(h);
                o_sck_ = cpol(); publish();              // flanco de captura
                if (read_data()) put_bit(rx, i, nb, true);
                wait(h);
            }
            if (!enabled()) { aborted = true; break; }
        }
        sr_ &= ~S_BSY;
        // Apagar el SPI a media trama la ABORTA: el marco incompleto se
        // descarta en vez de entregar medio dato, como haría el silicio.
        if (!aborted) frame_done(tx, rx);
        publish();
    }

    // --- I2S maestro: genera CK, WS y, si procede, MCK -------------------
    void i2s_master_frame() {
        if (i2s_bit_hz_ <= 0.0) { wait(wake_ev_); return; }
        const unsigned slot = chlen32() ? 32u : 16u;
        const unsigned nd   = data_bits() > slot ? slot : data_bits();
        const sc_core::sc_time h = bit_time() / 2.0;
        const bool tx_mode = i2s_is_tx();
        // WS: canal izquierdo primero (nivel bajo en Philips) [IR, §12.7]
        const bool ws_left = (i2sstd() == 0) ? false : true;
        for (unsigned ch = 0; ch < 2; ++ch) {
            const bool left = (ch == 0);
            if (i2sstd() != 0) o_nss_ = left ? ws_left : !ws_left;
            if (!left) sr_ |= S_CHSIDE; else sr_ &= ~S_CHSIDE;
            uint16_t tx = 0;
            if (tx_mode) { if (!fetch_tx(tx)) { sr_ |= S_UDR; update_irq(); } }
            uint16_t rx = 0;
            publish();
            for (unsigned i = 0; i < slot; ++i) {
                // El estándar Philips adelanta el flanco de WS un ciclo de CK
                // respecto al dato; los demás lo alinean con la ranura.
                if (i2sstd() == 0 && i + 1u == slot)
                    o_nss_ = (ch == 0) ? !ws_left : ws_left;
                const bool bit = (i < nd) ? bit_of(tx, i, nd) : false;
                if (tx_mode) drive_data(bit);
                wait(h);
                o_sck_ = !ckpol(); publish();
                if (!tx_mode && i < nd && read_data()) put_bit(rx, i, nd, true);
                wait(h);
                o_sck_ = ckpol(); publish();
                if (!enabled()) return;
            }
            frame_done(tx, rx);
        }
    }

    // =======================================================================
    // ESCLAVO: dirigido por los flancos del pin SCK/CK
    // =======================================================================
    void start_slave_frame() {
        if (i2s_active()) return;
        bit_i_ = 0; sh_rx_ = 0;
        if (!fetch_tx(sh_tx_)) sh_tx_ = 0;
        slave_armed_ = true;
        if (!cpha() && selected()) {
            drive_data(bit_of(sh_tx_, 0, frame_bits()));   // el primer bit, antes del flanco
        }
    }

    void slave_sck_proc() {
        if (!enabled()) return;
        if (i2s_active()) { i2s_slave_edge(); return; }
        if (!caps_.spi_mode || mstr() || !selected()) return;
        if (!sck_changed()) return;                    // solo los flancos de SCK
        if (!slave_armed_) start_slave_frame();
        const unsigned nb = frame_bits();
        const bool lv = sck_in.read();
        const bool leading = (lv != cpol());           // reposo -> activo
        if (!cpha()) {
            if (leading) {                             // captura
                if (read_data()) put_bit(sh_rx_, bit_i_, nb, true);
                ++bit_i_;
                if (bit_i_ >= nb) { end_slave_frame(nb); return; }
            } else {                                   // preparación del siguiente
                drive_data(bit_of(sh_tx_, bit_i_, nb));
            }
        } else {
            if (leading) {                             // preparación
                drive_data(bit_of(sh_tx_, bit_i_, nb));
            } else {                                   // captura
                if (read_data()) put_bit(sh_rx_, bit_i_, nb, true);
                ++bit_i_;
                if (bit_i_ >= nb) { end_slave_frame(nb); return; }
            }
        }
        publish();
    }

    // El proceso es sensible a varias señales; aquí se filtra el flanco de SCK.
    bool sck_changed() {
        const bool lv = sck_in.read();
        if (lv == sck_prev_) return false;
        sck_prev_ = lv;
        return true;
    }
    bool sck_prev_ = false;

    void end_slave_frame(unsigned nb) {
        (void)nb;
        const uint16_t sent = sh_tx_, got = sh_rx_;
        bit_i_ = 0; sh_rx_ = 0; slave_armed_ = false;
        frame_done(sent, got);
        start_slave_frame();                           // encadena el siguiente
        publish();
    }

    // --- I2S esclavo: WS marca el canal, CK los bits ---------------------
    // El esclavo se sincroniza con WS y cuenta los flancos activos de CK. En
    // el estándar Philips el flanco de WS va UN ciclo por delante del dato, así
    // que marca "queda un bit para cerrar el hueco actual"; en los estándares
    // justificados el flanco ES el principio del hueco.
    void i2s_slave_edge() {
        const bool ws = ws_level();
        const bool philips = (i2sstd() == 0);
        const unsigned slot = chlen32() ? 32u : 16u;
        const unsigned nd   = data_bits() > slot ? slot : data_bits();
        if (ws != ws_prev_) {
            ws_prev_ = ws;
            if (philips) align_ = 1u;          // el hueco acaba en un ciclo
            else         begin_slot(ws);
        }
        const bool capture = (ck_level() != ckpol());
        if (capture) {
            if (!i2s_is_tx() && bit_i_ < nd && read_data())
                put_bit(sh_rx_, bit_i_, nd, true);
            ++bit_i_;
            bool close = false;
            if (align_)                 close = (--align_ == 0);
            else if (bit_i_ >= slot)    close = true;
            if (close) {
                frame_done(sh_tx_, sh_rx_);
                begin_slot(ws_prev_);
            }
        } else if (i2s_is_tx()) {
            drive_data(bit_i_ < nd ? bit_of(sh_tx_, bit_i_, nd) : false);
        }
        publish();
    }
    void begin_slot(bool ws) {
        bit_i_ = 0; sh_rx_ = 0;
        if (i2s_is_tx() && !fetch_tx(sh_tx_)) { sh_tx_ = 0; sr_ |= S_UDR; }
        const bool ws_left = (i2sstd() == 0) ? false : true;
        if (ws == !ws_left) sr_ |= S_CHSIDE; else sr_ &= ~S_CHSIDE;
    }
    unsigned align_ = 0;
};

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN. El parámetro de plantilla es una
// referencia a los rasgos: el tipo expresa la variante, de modo que Spi,
// SpiI2s e I2sExt son tipos distintos que no se pueden confundir en el netlist.
// ---------------------------------------------------------------------------
template <const SpiCaps& Caps>
class SpiT : public SpiBase {
public:
    SpiT(sc_core::sc_module_name nm, uint32_t base) : SpiBase(nm, base, Caps) {}
    static constexpr const SpiCaps& variant() { return Caps; }
    static constexpr bool has_i2s()  { return Caps.i2s_mode; }
    static constexpr bool has_spi()  { return Caps.spi_mode; }
    static constexpr bool has_crc()  { return Caps.crc; }
    static constexpr double max_sck(){ return Caps.max_sck_hz; }
};

using Spi    = SpiT<CAPS_SPI_APB2>;   // SPI1
using SpiI2s = SpiT<CAPS_SPI_I2S>;    // SPI2, SPI3
using I2sExt = SpiT<CAPS_I2S_EXT>;    // I2S2ext, I2S3ext

static_assert(!Spi::has_i2s(),    "en el F407 el SPI1 no tiene modo I2S");
static_assert(SpiI2s::has_i2s(),  "SPI2 y SPI3 si lo tienen");
static_assert(!I2sExt::has_spi(), "los bloques de extension solo hacen audio");
static_assert(Spi::max_sck() > SpiI2s::max_sck(),
              "SPI1 esta en APB2 y admite el doble de SCK [IR, 12.5.1]");

} // namespace stm32
#endif // STM32_PERIPH_SPI_H
