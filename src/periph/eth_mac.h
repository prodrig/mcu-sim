// =============================================================================
// eth_mac.h — Ethernet MAC 10/100 con DMA propio (AHB1) [IR, §12.16]
//
// El Ethernet es el periférico con más piezas del chip, y las piezas no se
// parecen entre sí. Hay cuatro bloques de registros que no comparten nada
// -MAC, MMC, PTP y DMA-, dos caminos de datos que tampoco -transmisión y
// recepción-, dos interfaces físicas distintas -MII y RMII- y una máquina de
// descriptores que vive en la SRAM del usuario y a la que el periférico llega
// POR SU CUENTA, como maestro del bus:
//
//                   ┌──────── AHB1 (esclavo): MAC / MMC / PTP / DMA
//   CPU ────────────┤
//                   └──────── el firmware solo escribe registros y descriptores
//
//   descriptores    ┌── DMA propio (MAESTRO de la matriz) ──┐
//   y buffers en ───┤                                        ├── FIFO ── MAC ── PHY
//   la SRAM         └────────────────────────────────────────┘         MII/RMII
//
// -----------------------------------------------------------------------------
// LOS "CANALES" DEL ETHERNET SON TRES COSAS DISTINTAS
//
//   A) LAS DOS INTERFACES FÍSICAS. MII y RMII no son dos modos del mismo
//      camino: son DOS CAMINOS. MII lleva cuatro hilos por sentido con SU
//      PROPIO RELOJ en cada uno (TX_CLK y RX_CLK, 25 MHz a 100 Mbit/s) y
//      dieciocho pines en total; RMII lleva dos hilos por sentido con UN SOLO
//      reloj común de 50 MHz (REF_CLK) y nueve pines. Se elige con el bit 23
//      de SYSCFG_PMC, y -esto es lo importante- SOLO SE PUEDE CAMBIAR CON EL
//      MAC EN RESET: no es un modo, es un cableado [IR, §12.16.2].
//
//   B) LOS DOS ANILLOS DEL DMA. Transmisión y recepción NO son copias. Sus
//      descriptores tienen los mismos cuatro campos y significados COMPLETAMENTE
//      distintos: TDES0 lleva las órdenes (FS, LS, IC, CIC, cuenta de
//      colisiones) y RDES0 lleva el resultado (longitud recibida, error de
//      CRC, trama larga, filtro que la aceptó). El de transmisión lo escribe el
//      firmware y lo lee el DMA; el de recepción, al revés. Y hasta los bits de
//      control están en registros distintos: ST y TTC frente a SR y RTC.
//
//   C) LOS CUATRO FILTROS DE DIRECCIÓN MAC. Aquí pasa lo mismo que con el
//      endpoint 0 del USB: EL FILTRO 0 ES DISTINTO. MACA0 está siempre
//      habilitado, no se puede enmascarar por bytes y solo compara la dirección
//      de DESTINO; MACA1-3 tienen AE (habilitación), SA (destino u origen) y
//      MBC[5:0] (qué bytes se comparan). Un modelo que trate los cuatro como un
//      vector se equivoca en los tres puntos.
//
// -----------------------------------------------------------------------------
// SELECCIÓN DEL TIPO DE ETHERNET
//
//   * en TIEMPO DE COMPILACIÓN, con el alias de plantilla:
//         using Eth       = EthT<CAPS_ETH_F407>;   // MII + RMII, PTP, MMC, 4 filtros
//         using EthRmii   = EthT<CAPS_ETH_RMII>;   // placa cableada en RMII
//         using EthBasico = EthT<CAPS_ETH_BASIC>;  // sin PTP, sin MMC, 1 filtro
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         EthBase e{"e", EthCaps{...}};
//
// Los rasgos se aplican como MÁSCARA DE ESCRITURA y además deciden qué GRUPOS
// de registros existen: sin PTP, todo el bloque 0x700 se lee cero; sin MMC, el
// 0x100; sin filtro hash, MACHTHR/MACHTLR. Misma receta que UsartCaps, TimCaps,
// SpiCaps, I2cCaps, AdcCaps, DacCaps, SdioCaps, CanCaps, DcmiCaps, FsmcCaps y
// OtgCaps.
//
// -----------------------------------------------------------------------------
// DÓNDE ESTÁ LA FRONTERA DEL MODELO
//
// La trama sale y entra POR LOS PINES, nibble a nibble (MII) o dibit a dibit
// (RMII), con su preámbulo, su delimitador y su CRC-32 de verdad calculado
// sobre los bytes que van por el cable. Lo que no se modela es la capa
// eléctrica del par trenzado -eso es cosa del PHY, que está al otro lado de
// esos pines- ni la autonegociación, que se resuelve por MDIO como en el
// silicio.
// =============================================================================
#ifndef STM32_PERIPH_ETH_MAC_H
#define STM32_PERIPH_ETH_MAC_H

#include <array>
#include <vector>
#include <deque>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// El CRC-32 de Ethernet. Sirve para dos cosas muy distintas: la secuencia de
// comprobación que cierra cada trama (FCS) y el índice del filtro hash, que se
// saca de los seis bits ALTOS del mismo CRC sobre la dirección de destino.
// ---------------------------------------------------------------------------
inline uint32_t eth_crc32(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc;
}
inline uint32_t eth_fcs(const uint8_t* d, size_t n) {
    return ~eth_crc32(d, n);
}

// ---------------------------------------------------------------------------
// Los rasgos
// ---------------------------------------------------------------------------
struct EthCaps {
    bool     mii      = true;    // interfaz de 18 pines con dos relojes
    bool     rmii     = true;    // interfaz de 9 pines con un reloj comun
    bool     ptp      = true;    // IEEE 1588 (bloque 0x700)
    bool     mmc      = true;    // contadores estadisticos (bloque 0x100)
    bool     pmt      = true;    // despertar por Magic Packet / trama
    bool     hash     = true;    // filtro hash de 64 bits
    bool     vlan     = true;    // MACVLANTR
    bool     checksum = true;    // descarga de suma de comprobacion IP/TCP
    bool     flow     = true;    // control de flujo por tramas PAUSE
    unsigned filtros  = 4;       // MACA0..MACA3
    unsigned fifo_tx  = 2048;    // bytes
    unsigned fifo_rx  = 2048;
    const char* kind  = "ETH";
};

// --- Las variantes ---------------------------------------------------------
// El F407VG: el MAC completo, con las dos interfaces disponibles (la placa
// elige una con SYSCFG_PMC), PTP, MMC y los cuatro filtros [IR, §12.16].
constexpr EthCaps caps_eth_f407() {
    EthCaps c{};
    c.kind = "ETH F407VG (MII+RMII, PTP, MMC, 4 filtros, 2 KB de FIFO)";
    return c;
}
// Una placa cableada en RMII: los pines de MII no existen y el bit de
// SYSCFG_PMC no tiene nada que seleccionar. Es lo que hay en cualquier placa
// pequeña, porque MII cuesta nueve pines mas.
constexpr EthCaps caps_eth_rmii() {
    EthCaps c{};
    c.mii = false;
    c.kind = "ETH cableado en RMII (9 pines, un solo reloj de 50 MHz)";
    return c;
}
// El minimo que sigue siendo un MAC: 10/100, un solo filtro de direccion, sin
// PTP, sin MMC, sin hash y sin control de flujo.
constexpr EthCaps caps_eth_basic() {
    EthCaps c{};
    c.mii = false; c.ptp = false; c.mmc = false; c.hash = false;
    c.vlan = false; c.checksum = false; c.flow = false; c.pmt = false;
    c.filtros = 1; c.fifo_tx = c.fifo_rx = 512;
    c.kind = "ETH minimo (RMII, 1 filtro, sin PTP/MMC/hash)";
    return c;
}

inline constexpr EthCaps CAPS_ETH_F407  = caps_eth_f407();
inline constexpr EthCaps CAPS_ETH_RMII  = caps_eth_rmii();
inline constexpr EthCaps CAPS_ETH_BASIC = caps_eth_basic();

// ===========================================================================
// El controlador
// ===========================================================================
class EthBase : public BusSlave {
public:
    const EthCaps caps;

    tlm_utils::simple_initiator_socket<EthBase> dma_m{"dma_m"};   // maestro matriz
    // Una variante que no se enlaza a la matriz -por ejemplo, una instancia de
    // banco de pruebas que solo mira registros- dejaria el puerto suelto, y
    // SystemC no lo admite. Se le pone un tapon que contesta ERROR, de modo que
    // un acceso indebido se veria en el acto.
    tlm_utils::simple_target_socket<EthBase>* dma_nc_ = nullptr;
    sc_core::sc_out<bool> irq{"irq"};                   // IRQ 61
    sc_core::sc_out<bool> wkup_line{"wkup_line"};       // EXTI19 -> IRQ 62
    sc_core::sc_in<bool>  mii_rmii_sel{"mii_rmii_sel"}; // SYSCFG_PMC bit 23

    // ---- Los pines (AF11) --------------------------------------------------
    // Un solo juego de señales para las dos interfaces: RMII usa TXD[1:0] y
    // RXD[1:0], y su REF_CLK entra por el mismo pin que el RX_CLK de MII.
    sc_core::sc_signal<bool> mdc_out{"mdc_out"};
    sc_core::sc_signal<bool> mdio_out{"mdio_out"}, mdio_oe{"mdio_oe"},
                             mdio_in{"mdio_in"};
    sc_core::sc_signal<bool> tx_clk_in{"tx_clk_in"};    // MII  (PC3)
    sc_core::sc_signal<bool> ref_clk_in{"ref_clk_in"};  // RMII / MII RX_CLK (PA1)
    sc_core::sc_signal<bool> tx_en_out{"tx_en_out"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> txd_out, rxd_in;   // [4]
    sc_core::sc_signal<bool> rx_dv_in{"rx_dv_in"};      // RX_DV / CRS_DV (PA7)
    sc_core::sc_signal<bool> rx_er_in{"rx_er_in"};      // MII (PB10)
    sc_core::sc_signal<bool> crs_in{"crs_in"};          // MII (PA0)
    sc_core::sc_signal<bool> col_in{"col_in"};          // MII (PA3)
    sc_core::sc_signal<bool> pps_out{"pps_out"};        // PTP (PB5)

    EthBase(sc_core::sc_module_name nm, const EthCaps& c)
        : BusSlave(nm, addr::ETH_B, 0x1400), caps(c),
          txd_out("txd_out", 4), rxd_in("rxd_in", 4) {
        maca_.resize(caps.filtros ? caps.filtros : 1);
        reset_regs();
        SC_HAS_PROCESS(EthBase);
        SC_THREAD(mdio_proc);
        SC_THREAD(tx_proc);
        SC_THREAD(rx_proc);
        SC_METHOD(rst_proc); sensitive << rst_n;   dont_initialize();
        SC_METHOD(pub_proc); sensitive << pub_ev_; dont_initialize();
        // Si el hilo de transmision se ha dormido a la espera de un evento, hay
        // que despertarlo cuando el bloque recibe reloj o sale de reset.
        SC_METHOD(wake_proc); sensitive << rst_n << clk_en; dont_initialize();
    }
    void wake_proc() { tx_ev_.notify(sc_core::SC_ZERO_TIME); }
    void before_end_of_elaboration() override {
        if (dma_m.size() == 0) {
            dma_nc_ = new tlm_utils::simple_target_socket<EthBase>("dma_nc");
            dma_nc_->register_b_transport(this, &EthBase::bt_sin_dma);
            dma_m.bind(*dma_nc_);
        }
    }
    ~EthBase() override { delete dma_nc_; }

    // =======================================================================
    // Mapa de registros [IR, §12.16.2]
    // =======================================================================
    enum : uint32_t {
        R_MACCR = 0x000, R_MACFFR = 0x004, R_MACHTHR = 0x008, R_MACHTLR = 0x00C,
        R_MACMIIAR = 0x010, R_MACMIIDR = 0x014, R_MACFCR = 0x018,
        R_MACVLANTR = 0x01C, R_MACRWUFFR = 0x028, R_MACPMTCSR = 0x02C,
        R_MACDBGR = 0x034, R_MACSR = 0x038, R_MACIMR = 0x03C,
        R_MACA0HR = 0x040, R_MACA0LR = 0x044,
        R_MMCCR = 0x100, R_MMCRIR = 0x104, R_MMCTIR = 0x108,
        R_MMCRIMR = 0x10C, R_MMCTIMR = 0x110,
        R_MMCTGFSCCR = 0x14C, R_MMCTGFMSCCR = 0x150, R_MMCTGFCR = 0x168,
        R_MMCRFCECR = 0x194, R_MMCRFAECR = 0x198, R_MMCRGUFCR = 0x1C4,
        R_PTPTSCR = 0x700, R_PTPSSIR = 0x704, R_PTPTSHR = 0x708,
        R_PTPTSLR = 0x70C, R_PTPTSHUR = 0x710, R_PTPTSLUR = 0x714,
        R_PTPTSAR = 0x718, R_PTPTTHR = 0x71C, R_PTPTTLR = 0x720,
        R_PTPTSSR = 0x728, R_PTPPPSCR = 0x72C,
        R_DMABMR = 0x1000, R_DMATPDR = 0x1004, R_DMARPDR = 0x1008,
        R_DMARDLAR = 0x100C, R_DMATDLAR = 0x1010, R_DMASR = 0x1014,
        R_DMAOMR = 0x1018, R_DMAIER = 0x101C, R_DMAMFBOCR = 0x1020,
        R_DMARSWTR = 0x1024, R_DMACHTDR = 0x1048, R_DMACHRDR = 0x104C,
        R_DMACHTBAR = 0x1050, R_DMACHRBAR = 0x1054
    };
    // ETH_MACCR
    enum : uint32_t {
        CR_RE = 1u << 2, CR_TE = 1u << 3, CR_DC = 1u << 4, CR_BL = 3u << 5,
        CR_APCS = 1u << 7, CR_RD = 1u << 9, CR_IPCO = 1u << 10, CR_DM = 1u << 11,
        CR_LM = 1u << 12, CR_ROD = 1u << 13, CR_FES = 1u << 14, CR_CSD = 1u << 16,
        CR_IFG = 7u << 17, CR_JD = 1u << 22, CR_WD = 1u << 23, CR_CSTF = 1u << 25,
        CR_FIJO = 1u << 15                       // reservado, vale uno al reset
    };
    // ETH_MACFFR
    enum : uint32_t {
        FF_PM = 1u << 0, FF_HU = 1u << 1, FF_HM = 1u << 2, FF_DAIF = 1u << 3,
        FF_PAM = 1u << 4, FF_BFD = 1u << 5, FF_PCF = 3u << 6, FF_SAIF = 1u << 8,
        FF_SAF = 1u << 9, FF_HPF = 1u << 10, FF_RA = 1u << 31
    };
    // ETH_MACMIIAR
    enum : uint32_t {
        MII_MB = 1u << 0, MII_MW = 1u << 1, MII_CR = 7u << 2,
        MII_MR = 0x1Fu << 6, MII_PA = 0x1Fu << 11
    };
    // ETH_MACFCR / MACPMTCSR / MACSR
    enum : uint32_t {
        FC_FCB = 1u << 0, FC_TFCE = 1u << 1, FC_RFCE = 1u << 2, FC_UPFD = 1u << 3,
        FC_PLT = 3u << 4, FC_ZQPD = 1u << 7,
        PMT_PD = 1u << 0, PMT_MPE = 1u << 1, PMT_WFE = 1u << 2,
        PMT_MPR = 1u << 5, PMT_WFR = 1u << 6, PMT_GU = 1u << 9,
        SR_PMTS = 1u << 3, SR_MMCS = 1u << 4, SR_MMCRS = 1u << 5,
        SR_MMCTS = 1u << 6, SR_TSTS = 1u << 9
    };
    // MACAxHR (x >= 1): lo que el filtro 0 NO tiene
    enum : uint32_t {
        MACA_MBC = 0x3Fu << 24, MACA_SA = 1u << 30, MACA_AE = 1u << 31
    };
    // ETH_DMASR
    enum : uint32_t {
        DMA_TS = 1u << 0, DMA_TPSS = 1u << 1, DMA_TBUS = 1u << 2, DMA_TJTS = 1u << 3,
        DMA_ROS = 1u << 4, DMA_TUS = 1u << 5, DMA_RS = 1u << 6, DMA_RBUS = 1u << 7,
        DMA_RPSS = 1u << 8, DMA_RWTS = 1u << 9, DMA_ETS = 1u << 10,
        DMA_FBES = 1u << 13, DMA_ERS = 1u << 14, DMA_AIS = 1u << 15,
        DMA_NIS = 1u << 16, DMA_RPS = 7u << 17, DMA_TPS = 7u << 20,
        DMA_EBS = 7u << 23, DMA_MMCS = 1u << 27, DMA_PMTS = 1u << 28,
        DMA_TSTS = 1u << 29
    };
    // ETH_DMAOMR / ETH_DMABMR
    enum : uint32_t {
        OMR_SR = 1u << 1, OMR_OSF = 1u << 2, OMR_RTC = 3u << 3, OMR_FUGF = 1u << 6,
        OMR_FEF = 1u << 7, OMR_ST = 1u << 13, OMR_TTC = 7u << 14,
        OMR_FTF = 1u << 20, OMR_TSF = 1u << 21, OMR_DFRF = 1u << 24,
        OMR_RSF = 1u << 25, OMR_DTCEFD = 1u << 26,
        BMR_SR = 1u << 0, BMR_DA = 1u << 1, BMR_DSL = 0x1Fu << 2,
        BMR_EDFE = 1u << 7, BMR_PBL = 0x3Fu << 8, BMR_RTPR = 3u << 14,
        BMR_FB = 1u << 16, BMR_RDP = 0x3Fu << 17, BMR_USP = 1u << 23,
        BMR_FPM = 1u << 24, BMR_AAB = 1u << 25, BMR_MB = 1u << 26
    };
    // Descriptores [IR, §12.16.2: "formato de 4 u 8 palabras"]
    enum : uint32_t {
        TD0_ES = 1u << 15, TD0_TCH = 1u << 20, TD0_TER = 1u << 21,
        TD0_CIC = 3u << 22, TD0_TTSE = 1u << 25, TD0_FS = 1u << 28,
        TD0_LS = 1u << 29, TD0_IC = 1u << 30, TD0_OWN = 1u << 31,
        TD0_UF = 1u << 1, TD0_TTSS = 1u << 17,
        RD0_CE = 1u << 1, RD0_RE = 1u << 3, RD0_FT = 1u << 5, RD0_LS = 1u << 8,
        RD0_FS = 1u << 9, RD0_OE = 1u << 11, RD0_LE = 1u << 12, RD0_SAF = 1u << 13,
        RD0_ES = 1u << 15, RD0_AFM = 1u << 30, RD0_OWN = 1u << 31,
        RD1_RCH = 1u << 14, RD1_RER = 1u << 15, RD1_DIC = 1u << 31
    };

    // =======================================================================
    // Observadores del banco de pruebas
    // =======================================================================
    bool     modo_mii() const { return usa_mii(); }
    unsigned tramas_tx() const { return n_tx_; }
    unsigned tramas_rx() const { return n_rx_; }
    unsigned descartadas() const { return n_filtradas_; }
    unsigned errores_crc() const { return n_crc_; }
    unsigned mdio_ops() const { return n_mdio_; }
    uint64_t ptp_tiempo() const { return (uint64_t(ptptshr_) << 32) | ptptslr_; }
    const std::vector<uint8_t>& ultima_tx() const { return ult_tx_; }
    uint64_t maestro_accesos() const { return n_ahb_; }

protected:
    // =======================================================================
    // Estado
    // =======================================================================
    struct FiltroMac { uint32_t hr = 0, lr = 0xFFFFFFFFu; };

    uint32_t maccr_ = 0, macffr_ = 0, machthr_ = 0, machtlr_ = 0;
    uint32_t macmiiar_ = 0, macmiidr_ = 0, macfcr_ = 0, macvlantr_ = 0;
    uint32_t macpmtcsr_ = 0, macsr_ = 0, macimr_ = 0;
    std::vector<FiltroMac> maca_;
    uint32_t mmccr_ = 0, mmcrir_ = 0, mmctir_ = 0, mmcrimr_ = 0, mmctimr_ = 0;
    uint32_t c_tx_good_ = 0, c_tx_all_ = 0, c_rx_crc_ = 0, c_rx_align_ = 0,
             c_rx_good_ = 0;
    uint32_t ptptscr_ = 0, ptpssir_ = 0, ptptshr_ = 0, ptptslr_ = 0;
    uint32_t ptptshur_ = 0, ptptslur_ = 0, ptptsar_ = 0, ptptthr_ = 0, ptpttlr_ = 0;
    uint32_t dmabmr_ = 0, dmardlar_ = 0, dmatdlar_ = 0, dmasr_ = 0;
    uint32_t dmaomr_ = 0, dmaier_ = 0, dmamfbocr_ = 0, dmarswtr_ = 0;
    uint32_t dma_tdesc_ = 0, dma_rdesc_ = 0;      // descriptor en curso
    uint32_t dma_tbuf_ = 0, dma_rbuf_ = 0;

    unsigned n_tx_ = 0, n_rx_ = 0, n_filtradas_ = 0, n_crc_ = 0, n_mdio_ = 0;
    uint64_t n_ahb_ = 0;
    std::vector<uint8_t> ult_tx_;
    bool o_irq_ = false, o_wkup_ = false;
    sc_core::sc_event pub_ev_, tx_ev_, rx_ev_, mdio_ev_;

    void bt_sin_dma(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    }
    bool usa_mii() const { return caps.mii && (!caps.rmii || mii_rmii_sel.read() == false); }
    bool mac_activo() const { return clock_enabled() && rst_n.read(); }

    // =======================================================================
    // Máscaras: donde los rasgos dejan de ser documentación
    // =======================================================================
    uint32_t mask_maccr() const {
        uint32_t m = CR_RE | CR_TE | CR_DC | CR_BL | CR_APCS | CR_RD | CR_DM |
                     CR_LM | CR_ROD | CR_FES | CR_CSD | CR_IFG | CR_JD | CR_WD;
        if (caps.checksum) m |= CR_IPCO | CR_CSTF;
        return m;
    }
    uint32_t mask_macffr() const {
        uint32_t m = FF_PM | FF_DAIF | FF_PAM | FF_BFD | FF_PCF | FF_SAIF |
                     FF_SAF | FF_RA;
        if (caps.hash) m |= FF_HU | FF_HM | FF_HPF;
        return m;
    }
    uint32_t mask_macfcr() const {
        if (!caps.flow) return 0;
        return FC_FCB | FC_TFCE | FC_RFCE | FC_UPFD | FC_PLT | FC_ZQPD |
               (0xFFFFu << 16);
    }
    // LA MÁSCARA DE UN FILTRO DEPENDE DE CUÁL SEA. El 0 no tiene AE (está
    // siempre puesto), no tiene SA (solo compara destino) y no tiene MBC.
    uint32_t mask_macahr(unsigned i) const {
        if (i == 0) return 0xFFFFu;
        return 0xFFFFu | MACA_MBC | MACA_SA | MACA_AE;
    }
    uint32_t maca_fijo(unsigned i) const { return i == 0 ? MACA_AE : 0u; }

    // =======================================================================
    // Acceso a la memoria como MAESTRO del bus
    // =======================================================================
    bool ahb_read(uint32_t a, uint8_t* d, unsigned n) { return ahb(false, a, d, n); }
    bool ahb_write(uint32_t a, const uint8_t* d, unsigned n) {
        return ahb(true, a, const_cast<uint8_t*>(d), n);
    }
    bool ahb(bool wr, uint32_t a, uint8_t* d, unsigned n) {
        tlm::tlm_generic_payload gp;
        AhbExt ext; ext.master = BusMaster::ETH_DMA; ext.privileged = true;
        gp.set_command(wr ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        gp.set_address(a);
        gp.set_data_ptr(d);
        gp.set_data_length(n);
        gp.set_streaming_width(n);
        gp.set_byte_enable_ptr(nullptr);
        gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        gp.set_extension(&ext);
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        dma_m->b_transport(gp, t);
        gp.clear_extension(&ext);
        if (t > sc_core::SC_ZERO_TIME) sc_core::wait(t);
        ++n_ahb_;
        const bool ok = gp.get_response_status() == tlm::TLM_OK_RESPONSE;
        if (!ok) { dmasr_ |= DMA_FBES | DMA_AIS; actualiza_irq(); }
        return ok;
    }
    uint32_t leer32(uint32_t a) { uint32_t v = 0; ahb_read(a, (uint8_t*)&v, 4); return v; }
    void escribir32(uint32_t a, uint32_t v) { ahb_write(a, (const uint8_t*)&v, 4); }

    // El paso de un descriptor al siguiente: encadenado (el descriptor lleva
    // el puntero) o anillo (van seguidos, con el hueco que diga DMABMR.DSL).
    uint32_t siguiente(uint32_t dir, uint32_t w1, uint32_t w3, uint32_t base,
                       bool rx) {
        const bool ch = rx ? ((w1 & RD1_RCH) != 0) : false;
        const bool er = rx ? ((w1 & RD1_RER) != 0) : false;
        if (ch) return w3;
        if (er) return base;
        const unsigned dsl = (dmabmr_ >> 2) & 0x1Fu;
        return dir + 16u + 4u * dsl;
    }

    // =======================================================================
    // Registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override;
    void     reg_write(uint32_t off, uint32_t v, uint32_t be) override;

    void reset_regs();
    void rst_proc() { if (!rst_n.read()) { reset_regs(); publica(); } }
    void pub_proc() { irq.write(o_irq_); wkup_line.write(o_wkup_); }
    void publica() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void actualiza_irq();

    // =======================================================================
    // Los procesos
    // =======================================================================
    void mdio_proc();
    void tx_proc();
    void rx_proc();

    // Camino de datos por los pines
    bool emite_trama(const std::vector<uint8_t>& t);
    bool recoge_trama(std::vector<uint8_t>& t);
    void pon_nibble(uint8_t v, unsigned n);
    uint8_t lee_nibble(unsigned n) const;
    const sc_core::sc_event& reloj_tx() const;
    const sc_core::sc_event& reloj_rx() const;
    bool flanco_tx();
    bool flanco_rx();

    // MAC
    bool filtro_acepta(const std::vector<uint8_t>& t, bool& por_hash);
    void mmc_tx(bool ok);
    void mmc_rx(bool ok, bool crc);
    uint64_t sella_tiempo();

    // DMA
    bool dma_transmite();
    bool dma_recibe(const std::vector<uint8_t>& t, bool err_crc);
};

// ===========================================================================
// Reset
// ===========================================================================
inline void EthBase::reset_regs() {
    // MACCR arranca en 0x0000 8000. Ese uno es el BIT 15, que es reservado:
    // el informe lo atribuye al bit 14 (RE), pero entonces el valor de reset
    // seria 0x4000. Se sigue el valor de reset, que es lo comprobable, y el
    // bit 15 se modela como un uno fijo que no se puede borrar.
    maccr_ = CR_FIJO;
    macffr_ = 0; machthr_ = machtlr_ = 0;
    macmiiar_ = 0; macmiidr_ = 0; macfcr_ = 0; macvlantr_ = 0;
    macpmtcsr_ = 0; macsr_ = 0; macimr_ = 0;
    for (unsigned i = 0; i < maca_.size(); ++i) {
        // La direccion MAC de reset es FF:FF:FF:FF:FF:FF en los cuatro filtros,
        // y el 0 arranca HABILITADO: es el unico que no se puede apagar.
        maca_[i].hr = 0x0000FFFFu | maca_fijo(i);
        maca_[i].lr = 0xFFFFFFFFu;
    }
    mmccr_ = mmcrir_ = mmctir_ = mmcrimr_ = mmctimr_ = 0;
    c_tx_good_ = c_tx_all_ = c_rx_crc_ = c_rx_align_ = c_rx_good_ = 0;
    ptptscr_ = 0x00002000u; ptpssir_ = 0; ptptshr_ = ptptslr_ = 0;
    ptptshur_ = ptptslur_ = 0; ptptsar_ = 0; ptptthr_ = ptpttlr_ = 0;
    dmabmr_ = 0x00020101u;                   // [IR, §12.16.2]
    dmardlar_ = dmatdlar_ = 0; dmasr_ = 0; dmaomr_ = 0; dmaier_ = 0;
    dmamfbocr_ = 0; dmarswtr_ = 0;
    dma_tdesc_ = dma_rdesc_ = 0; dma_tbuf_ = dma_rbuf_ = 0;
    n_tx_ = n_rx_ = n_filtradas_ = n_crc_ = n_mdio_ = 0;
    n_ahb_ = 0;
    ult_tx_.clear();
    o_irq_ = o_wkup_ = false;
}

// ===========================================================================
// Interrupciones. El DMA tiene DOS resúmenes -normal y anormal- y el manejador
// tiene que borrar los dos: el bit de causa y el de resumen. Es la fuente
// número uno de interrupciones que no se van [IR, §12.16.2].
// ===========================================================================
inline void EthBase::actualiza_irq() {
    const uint32_t normales  = DMA_TS | DMA_TBUS | DMA_RS | DMA_ERS;
    const uint32_t anormales = DMA_TPSS | DMA_TJTS | DMA_ROS | DMA_TUS |
                               DMA_RBUS | DMA_RPSS | DMA_RWTS | DMA_FBES | DMA_ETS;
    if (dmasr_ & normales & dmaier_)  dmasr_ |= DMA_NIS; else dmasr_ &= ~DMA_NIS;
    if (dmasr_ & anormales & dmaier_) dmasr_ |= DMA_AIS; else dmasr_ &= ~DMA_AIS;
    // El MAC mete sus propias causas en DMASR, pero NO pasan por DMAIER: se
    // enmascaran en MACIMR y se ven en MACSR.
    dmasr_ = (dmasr_ & ~(DMA_PMTS | DMA_TSTS)) |
             ((macsr_ & SR_PMTS) ? DMA_PMTS : 0u) |
             ((macsr_ & SR_TSTS) ? DMA_TSTS : 0u);
    const bool act = ((dmasr_ & (DMA_NIS | DMA_AIS)) != 0) ||
                     ((macsr_ & ~macimr_ & (SR_PMTS | SR_TSTS)) != 0);
    if (act != o_irq_) { o_irq_ = act; publica(); }
}

// ===========================================================================
// LECTURA DE REGISTROS
// ===========================================================================
inline uint32_t EthBase::reg_read(uint32_t off) {
    if (off >= R_MACA0HR && off < R_MACA0HR + 8u * 4u) {
        const unsigned i = (off - R_MACA0HR) / 8u;
        if (i >= caps.filtros) return 0;            // ese filtro no existe
        return ((off - R_MACA0HR) % 8u) ? maca_[i].lr : maca_[i].hr;
    }
    if (off >= 0x100 && off < 0x700 && !caps.mmc) return 0;
    if (off >= 0x700 && off < 0x800 && !caps.ptp) return 0;
    switch (off) {
        case R_MACCR:   return maccr_;
        case R_MACFFR:  return macffr_;
        case R_MACHTHR: return caps.hash ? machthr_ : 0u;
        case R_MACHTLR: return caps.hash ? machtlr_ : 0u;
        case R_MACMIIAR: return macmiiar_;
        case R_MACMIIDR: return macmiidr_;
        case R_MACFCR:  return macfcr_;
        case R_MACVLANTR: return caps.vlan ? macvlantr_ : 0u;
        case R_MACPMTCSR: return caps.pmt ? macpmtcsr_ : 0u;
        // MACDBGR es de solo lectura y dice si las maquinas estan paradas.
        case R_MACDBGR: return 0;
        case R_MACSR:   return macsr_;
        case R_MACIMR:  return macimr_;
        case R_MMCCR:   return mmccr_;
        case R_MMCRIR:  return mmcrir_;
        case R_MMCTIR:  return mmctir_;
        case R_MMCRIMR: return mmcrimr_;
        case R_MMCTIMR: return mmctimr_;
        // Los contadores del MMC son de LECTURA DESTRUCTIVA si MMCCR.ROR esta
        // puesto: leerlos los pone a cero. Es como se hacen las estadisticas
        // sin perder cuentas entre dos lecturas.
        case R_MMCTGFSCCR: { const uint32_t v = c_tx_good_;
                             if (mmccr_ & (1u << 2)) c_tx_good_ = 0;
                             return v; }
        case R_MMCTGFCR:   { const uint32_t v = c_tx_all_;
                             if (mmccr_ & (1u << 2)) c_tx_all_ = 0;
                             return v; }
        case R_MMCTGFMSCCR: return 0;
        case R_MMCRFCECR:  { const uint32_t v = c_rx_crc_;
                             if (mmccr_ & (1u << 2)) c_rx_crc_ = 0;
                             return v; }
        case R_MMCRFAECR:  { const uint32_t v = c_rx_align_;
                             if (mmccr_ & (1u << 2)) c_rx_align_ = 0;
                             return v; }
        case R_MMCRGUFCR:  { const uint32_t v = c_rx_good_;
                             if (mmccr_ & (1u << 2)) c_rx_good_ = 0;
                             return v; }
        case R_PTPTSCR: return ptptscr_;
        case R_PTPSSIR: return ptpssir_;
        case R_PTPTSHR: return ptptshr_;
        case R_PTPTSLR: return ptptslr_;
        case R_PTPTSHUR: return ptptshur_;
        case R_PTPTSLUR: return ptptslur_;
        case R_PTPTSAR: return ptptsar_;
        case R_PTPTTHR: return ptptthr_;
        case R_PTPTTLR: return ptpttlr_;
        case R_PTPTSSR: return 0;
        case R_PTPPPSCR: return 0;
        case R_DMABMR:  return dmabmr_;
        case R_DMARDLAR: return dmardlar_;
        case R_DMATDLAR: return dmatdlar_;
        case R_DMASR:   return dmasr_;
        case R_DMAOMR:  return dmaomr_;
        case R_DMAIER:  return dmaier_;
        case R_DMAMFBOCR: { const uint32_t v = dmamfbocr_; dmamfbocr_ = 0; return v; }
        case R_DMARSWTR: return dmarswtr_;
        case R_DMACHTDR: return dma_tdesc_;
        case R_DMACHRDR: return dma_rdesc_;
        case R_DMACHTBAR: return dma_tbuf_;
        case R_DMACHRBAR: return dma_rbuf_;
        default: return 0;
    }
}

// ===========================================================================
// ESCRITURA DE REGISTROS
// ===========================================================================
inline void EthBase::reg_write(uint32_t off, uint32_t v, uint32_t be) {
    if (be != 0xFu) {                          // fusión byte a byte
        const uint32_t cur = reg_read(off);
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        v = (cur & ~m) | (v & m);
    }
    if (off >= R_MACA0HR && off < R_MACA0HR + 8u * 4u) {
        const unsigned i = (off - R_MACA0HR) / 8u;
        if (i >= caps.filtros) return;
        if ((off - R_MACA0HR) % 8u) maca_[i].lr = v;
        else maca_[i].hr = (v & mask_macahr(i)) | maca_fijo(i);
        return;
    }
    if (off >= 0x100 && off < 0x700 && !caps.mmc) return;
    if (off >= 0x700 && off < 0x800 && !caps.ptp) return;
    switch (off) {
        case R_MACCR:
            maccr_ = (v & mask_maccr()) | CR_FIJO;
            tx_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        case R_MACFFR:  macffr_ = v & mask_macffr(); return;
        case R_MACHTHR: if (caps.hash) machthr_ = v; return;
        case R_MACHTLR: if (caps.hash) machtlr_ = v; return;
        case R_MACMIIAR:
            macmiiar_ = v & (MII_MB | MII_MW | MII_CR | MII_MR | MII_PA);
            // MB es el "arranca": lo pone el firmware y el hardware lo borra
            // cuando la trama MDIO ha salido por los pines.
            if (v & MII_MB) mdio_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        case R_MACMIIDR: macmiidr_ = v & 0xFFFFu; return;
        case R_MACFCR:  macfcr_ = v & mask_macfcr(); return;
        case R_MACVLANTR: if (caps.vlan) macvlantr_ = v & 0x1FFFFu; return;
        case R_MACPMTCSR:
            if (!caps.pmt) return;
            macpmtcsr_ = v & (PMT_PD | PMT_MPE | PMT_WFE | PMT_GU);
            // Leer MACPMTCSR borra MPR y WFR; escribirlo no. Aqui se modela
            // que entrar en modo de bajo consumo limpia los avisos anteriores.
            if (v & PMT_PD) { macsr_ &= ~SR_PMTS; o_wkup_ = false; publica(); }
            actualiza_irq();
            return;
        case R_MACIMR:  macimr_ = v & (SR_PMTS | SR_TSTS); actualiza_irq(); return;
        case R_MMCCR:   mmccr_ = v & 0x1Fu;
                        if (v & 1u) { c_tx_good_ = c_tx_all_ = c_rx_crc_ =
                                      c_rx_align_ = c_rx_good_ = 0; }
                        return;
        case R_MMCRIR:  mmcrir_ &= ~v; return;
        case R_MMCTIR:  mmctir_ &= ~v; return;
        case R_MMCRIMR: mmcrimr_ = v; return;
        case R_MMCTIMR: mmctimr_ = v; return;
        case R_PTPTSCR: {
            const uint32_t antes = ptptscr_;
            ptptscr_ = v & 0x0003FFFFu;
            // TSSTI: cargar el reloj con PTPTSHUR/PTPTSLUR. Autoborrable.
            if ((v & (1u << 2)) && !(antes & (1u << 2))) {
                ptptshr_ = ptptshur_; ptptslr_ = ptptslur_;
                ptptscr_ &= ~(1u << 2);
            }
            // TSSTU: sumar o restar el valor de actualizacion. Tambien.
            if (v & (1u << 3)) {
                const bool resta = (ptptslur_ & 0x80000000u) != 0;
                const uint32_t sub = ptptslur_ & 0x7FFFFFFFu;
                if (resta) { ptptshr_ -= ptptshur_; ptptslr_ -= sub; }
                else       { ptptshr_ += ptptshur_; ptptslr_ += sub; }
                ptptscr_ &= ~(1u << 3);
            }
            return;
        }
        case R_PTPSSIR: ptpssir_ = v & 0xFFu; return;
        case R_PTPTSHUR: ptptshur_ = v; return;
        case R_PTPTSLUR: ptptslur_ = v; return;
        case R_PTPTSAR: ptptsar_ = v; return;
        case R_PTPTTHR: ptptthr_ = v; return;
        case R_PTPTTLR: ptpttlr_ = v; return;
        case R_DMABMR:
            dmabmr_ = v & ~BMR_SR;
            // SR de DMABMR es el RESET DEL BLOQUE, no el "start receive" de
            // DMAOMR. Dos bits con el mismo nombre en dos registros contiguos,
            // y uno de ellos borra la configuracion entera.
            if (v & BMR_SR) {
                const uint32_t cr = maccr_, ffr = macffr_;
                auto a = maca_;
                reset_regs();
                maccr_ = cr; macffr_ = ffr; maca_ = a;
            }
            return;
        case R_DMATPDR: dmasr_ &= ~DMA_TBUS; tx_ev_.notify(sc_core::SC_ZERO_TIME);
                        actualiza_irq(); return;
        case R_DMARPDR: dmasr_ &= ~DMA_RBUS; rx_ev_.notify(sc_core::SC_ZERO_TIME);
                        actualiza_irq(); return;
        case R_DMARDLAR: dmardlar_ = v & ~3u; dma_rdesc_ = dmardlar_; return;
        case R_DMATDLAR: dmatdlar_ = v & ~3u; dma_tdesc_ = dmatdlar_; return;
        case R_DMASR: {
            // Casi todo es rc_w1; RPS, TPS y EBS son de solo lectura, y NIS/AIS
            // se recalculan a partir de las causas que queden.
            const uint32_t ro = DMA_RPS | DMA_TPS | DMA_EBS | DMA_MMCS |
                                DMA_PMTS | DMA_TSTS;
            dmasr_ &= ~(v & ~ro);
            actualiza_irq();
            return;
        }
        case R_DMAOMR:
            dmaomr_ = v & (OMR_SR | OMR_OSF | OMR_RTC | OMR_FUGF | OMR_FEF |
                           OMR_ST | OMR_TTC | OMR_TSF | OMR_DFRF | OMR_RSF |
                           OMR_DTCEFD);
            if (v & OMR_FTF) { /* vaciar la FIFO de transmision: autoborrable */ }
            dmasr_ = (dmasr_ & ~DMA_TPS) | ((dmaomr_ & OMR_ST) ? (1u << 20) : 0u);
            dmasr_ = (dmasr_ & ~DMA_RPS) | ((dmaomr_ & OMR_SR) ? (1u << 17) : 0u);
            tx_ev_.notify(sc_core::SC_ZERO_TIME);
            rx_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        case R_DMAIER: dmaier_ = v & 0x0001FFFFu; actualiza_irq(); return;
        case R_DMARSWTR: dmarswtr_ = v & 0xFFu; return;
        default: return;
    }
}

// ===========================================================================
// MDIO: la gestión del PHY, bit a bit por dos pines
//
// Una trama MDIO son 32 bits en MDC: preámbulo, arranque, orden, dirección de
// PHY, dirección de registro, giro y dato. El firmware no ve nada de esto: pone
// MACMIIAR.MB y espera a que se borre. Aquí sale de verdad por los pines, que
// es lo único que un PHY de fuera puede ver.
// ===========================================================================
inline void EthBase::mdio_proc() {
    mdc_out.write(false); mdio_out.write(false); mdio_oe.write(false);
    for (;;) {
        sc_core::wait(mdio_ev_);
        if (!mac_activo() || !(macmiiar_ & MII_MB)) continue;
        const bool escr = (macmiiar_ & MII_MW) != 0;
        const unsigned pa = (macmiiar_ >> 11) & 0x1Fu;
        const unsigned mr = (macmiiar_ >> 6) & 0x1Fu;
        // CR[2:0] divide HCLK para sacar MDC, que no puede pasar de 2,5 MHz.
        static const unsigned div[8] = {42, 62, 16, 26, 102, 124, 1, 1};
        const double f = domain_hz() > 0.0 ? domain_hz() : 168.0e6;
        const sc_core::sc_time semi(
            0.5e12 * double(div[(macmiiar_ >> 2) & 7u]) / f, sc_core::SC_PS);

        uint32_t dato = macmiidr_ & 0xFFFFu;
        uint32_t leido = 0;
        auto ciclo = [&](bool conducir, bool valor) {
            mdio_oe.write(conducir);
            if (conducir) mdio_out.write(valor);
            mdc_out.write(false); sc_core::wait(semi);
            mdc_out.write(true);
            leido = (leido << 1) | (mdio_in.read() ? 1u : 0u);
            sc_core::wait(semi);
        };
        for (unsigned i = 0; i < 32; ++i) ciclo(true, true);      // preambulo
        ciclo(true, false); ciclo(true, true);                    // ST = 01
        if (escr) { ciclo(true, false); ciclo(true, true); }      // OP = 01
        else      { ciclo(true, true);  ciclo(true, false); }     // OP = 10
        for (int i = 4; i >= 0; --i) ciclo(true, ((pa >> i) & 1u) != 0);
        for (int i = 4; i >= 0; --i) ciclo(true, ((mr >> i) & 1u) != 0);
        if (escr) { ciclo(true, true); ciclo(true, false); }      // TA = 10
        else      { mdio_oe.write(false); ciclo(false, false); ciclo(false, false); }
        leido = 0;
        for (int i = 15; i >= 0; --i)
            ciclo(!escr ? false : true, escr ? ((dato >> i) & 1u) != 0 : false);
        mdio_oe.write(false);
        mdc_out.write(false);
        if (!escr) macmiidr_ = leido & 0xFFFFu;
        macmiiar_ &= ~MII_MB;                  // el hardware lo borra: ya esta
        ++n_mdio_;
    }
}

// ===========================================================================
// El camino de datos por los pines
// ===========================================================================
// EL RELOJ DEL CAMINO DE DATOS NO LO PONE EL MAC: LO PONE EL PHY.
// En MII entran dos, TX_CLK y RX_CLK, uno por sentido; en RMII entra uno solo,
// REF_CLK de 50 MHz, y sirve para los dos. El MAC se limita a seguirlos, y si
// no llegan, no transmite: es exactamente lo que pasa en una placa cuando el
// PHY no ha arrancado o esta en reset [IR, §12.16.1].
// Todo el mundo CONDUCE en el flanco de bajada y MUESTREA en el de subida: es
// medio ciclo de margen, que es lo que dan los tiempos de establecimiento y
// mantenimiento de la norma. Sin ese convenio, el modelo depende del orden en
// que SystemC despierte a dos procesos en el mismo delta, que es no depender
// de nada.
inline const sc_core::sc_event& EthBase::reloj_tx() const {
    return usa_mii() ? tx_clk_in.negedge_event() : ref_clk_in.negedge_event();
}
inline const sc_core::sc_event& EthBase::reloj_rx() const {
    return ref_clk_in.posedge_event();
}
// Espera un flanco con red de seguridad: sin reloj, el MAC no puede colgarse.
inline bool EthBase::flanco_tx() {
    const sc_core::sc_time t0 = sc_core::sc_time_stamp();
    sc_core::wait(sc_core::sc_time(4, sc_core::SC_US), reloj_tx());
    return sc_core::sc_time_stamp() - t0 < sc_core::sc_time(4, sc_core::SC_US);
}
inline bool EthBase::flanco_rx() {
    const sc_core::sc_time t0 = sc_core::sc_time_stamp();
    sc_core::wait(sc_core::sc_time(4, sc_core::SC_US), reloj_rx());
    return sc_core::sc_time_stamp() - t0 < sc_core::sc_time(4, sc_core::SC_US);
}
inline void EthBase::pon_nibble(uint8_t v, unsigned n) {
    for (unsigned i = 0; i < 4; ++i) txd_out[i].write(i < n && ((v >> i) & 1u));
}
inline uint8_t EthBase::lee_nibble(unsigned n) const {
    uint8_t v = 0;
    for (unsigned i = 0; i < n; ++i) if (rxd_in[i].read()) v |= uint8_t(1u << i);
    return v;
}

// Una trama entera por los hilos: preámbulo, delimitador, bytes y FCS.
inline bool EthBase::emite_trama(const std::vector<uint8_t>& t) {
    const unsigned n = usa_mii() ? 4u : 2u;    // bits por ciclo de reloj
    std::vector<uint8_t> hilo;
    for (unsigned i = 0; i < 7; ++i) hilo.push_back(0x55);   // preambulo
    hilo.push_back(0xD5);                                    // SFD
    for (uint8_t b : t) hilo.push_back(b);
    // La secuencia de comprobacion se calcula sobre la trama, no sobre el
    // preambulo, y va en el cable con el byte menos significativo primero.
    const uint32_t fcs = eth_fcs(t.data(), t.size());
    for (unsigned i = 0; i < 4; ++i) hilo.push_back(uint8_t(fcs >> (8 * i)));

    if (!flanco_tx()) return false;            // no hay reloj: no hay trama
    tx_en_out.write(true);
    for (uint8_t b : hilo) {
        for (unsigned s = 0; s < 8u / n; ++s) {
            pon_nibble(uint8_t(b >> (n * s)), n);
            if (!flanco_tx()) { tx_en_out.write(false); return false; }
        }
    }
    tx_en_out.write(false);
    pon_nibble(0, n);
    // Hueco entre tramas: 96 tiempos de bit. No es decoracion: sin el, el PHY
    // del otro extremo no distingue una trama de la siguiente.
    for (unsigned i = 0; i < 96u / n; ++i) if (!flanco_tx()) break;
    return true;
}

// Y una trama que llega: se espera al delimitador y se cuentan los bytes hasta
// que RX_DV baja.
inline bool EthBase::recoge_trama(std::vector<uint8_t>& t) {
    const unsigned n = usa_mii() ? 4u : 2u;
    t.clear();
    // Preambulo: se tira hasta ver el delimitador 0xD5. Si no aparece, no habia
    // trama: era ruido en la linea.
    unsigned guarda = 0;
    bool sfd = false;
    while (rx_dv_in.read() && guarda++ < 64u) {
        uint8_t b = 0;
        for (unsigned s = 0; s < 8u / n; ++s) {
            if (!flanco_rx()) return false;
            b |= uint8_t(lee_nibble(n) << (n * s));
        }
        if (b == 0xD5) { sfd = true; break; }
        if (b != 0x55) break;
    }
    if (!sfd) return false;
    while (rx_dv_in.read() && t.size() < 1600u) {
        uint8_t b = 0;
        for (unsigned s = 0; s < 8u / n; ++s) {
            if (!flanco_rx()) break;
            b |= uint8_t(lee_nibble(n) << (n * s));
        }
        if (!rx_dv_in.read() && b == 0) break;
        t.push_back(b);
    }
    return t.size() >= 8u;
}

// ===========================================================================
// El filtrado de direcciones. Es lo que evita que cada trama del segmento
// despierte al procesador [IR, §12.16-Implicaciones].
// ===========================================================================
inline bool EthBase::filtro_acepta(const std::vector<uint8_t>& t, bool& por_hash) {
    por_hash = false;
    if (t.size() < 12) return false;
    if (macffr_ & FF_RA) return true;            // recibirlo todo, sin mirar
    if (macffr_ & FF_PM) return true;            // modo promiscuo
    const uint8_t* dst = t.data();
    const uint8_t* src = t.data() + 6;
    const bool multi = (dst[0] & 1u) != 0;
    bool bcast = true;
    for (unsigned i = 0; i < 6; ++i) if (dst[i] != 0xFF) bcast = false;
    if (bcast) return !(macffr_ & FF_BFD);       // difusion, salvo que se veten
    if (multi && (macffr_ & FF_PAM)) return true;

    // Filtro hash: seis bits ALTOS del CRC-32 de la direccion de destino
    // indexan una tabla de 64 bits. Es como se aceptan muchos grupos multicast
    // sin gastar un filtro por cada uno.
    const bool usa_hash = caps.hash &&
                          ((multi && (macffr_ & FF_HM)) ||
                           (!multi && (macffr_ & FF_HU)));
    if (usa_hash) {
        const uint32_t crc = eth_crc32(dst, 6);
        const unsigned idx = (crc >> 26) & 0x3Fu;
        const uint32_t tab = (idx >= 32) ? machthr_ : machtlr_;
        if ((tab >> (idx & 31u)) & 1u) { por_hash = true; return true; }
        if (!(macffr_ & FF_HPF)) return false;   // sin paso perfecto, se acaba
    }
    // Filtros exactos. El 0 solo compara destino y no se puede enmascarar.
    for (unsigned i = 0; i < caps.filtros; ++i) {
        const uint32_t hr = maca_[i].hr;
        if (!(hr & MACA_AE)) continue;
        const bool origen = (i != 0) && (hr & MACA_SA);
        const uint8_t* cmp = origen ? src : dst;
        uint8_t mac[6] = {
            uint8_t(maca_[i].lr), uint8_t(maca_[i].lr >> 8),
            uint8_t(maca_[i].lr >> 16), uint8_t(maca_[i].lr >> 24),
            uint8_t(hr), uint8_t(hr >> 8)
        };
        const unsigned mbc = (i == 0) ? 0u : ((hr >> 24) & 0x3Fu);
        bool ok = true;
        for (unsigned b = 0; b < 6; ++b) {
            if ((mbc >> (5 - b)) & 1u) continue;      // byte enmascarado
            if (mac[b] != cmp[b]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

inline void EthBase::mmc_tx(bool ok) {
    if (!caps.mmc) return;
    ++c_tx_all_;
    if (ok) ++c_tx_good_;
}
inline void EthBase::mmc_rx(bool ok, bool crc) {
    if (!caps.mmc) return;
    if (crc) ++c_rx_crc_;
    if (ok) ++c_rx_good_;
}
// El sello de tiempo del 1588: el reloj lo lleva el propio periférico y el
// descriptor se lo queda. Sin esto, un maestro PTP no puede medir el retardo.
inline uint64_t EthBase::sella_tiempo() {
    if (!caps.ptp || !(ptptscr_ & 1u)) return 0;
    // Modo fino o grueso: en el grueso, cada tick suma PTPSSIR nanosegundos.
    const uint64_t ns = uint64_t(sc_core::sc_time_stamp().to_seconds() * 1e9);
    ptptshr_ = uint32_t(ns / 1000000000ull);
    ptptslr_ = uint32_t(ns % 1000000000ull);
    macsr_ |= SR_TSTS;
    return (uint64_t(ptptshr_) << 32) | ptptslr_;
}

// ===========================================================================
// EL ANILLO DE TRANSMISIÓN
//
// El firmware deja la trama en un buffer, marca el descriptor como suyo (OWN) y
// se olvida. El DMA lee el descriptor, lee el buffer, lo saca por los pines y
// DEVUELVE el descriptor con el resultado escrito encima.
// ===========================================================================
inline bool EthBase::dma_transmite() {
    if (!dmatdlar_) return false;
    uint32_t d = dma_tdesc_ ? dma_tdesc_ : dmatdlar_;
    std::vector<uint8_t> trama;
    uint32_t d_primero = d;
    bool hay = false;

    for (unsigned k = 0; k < 32u; ++k) {          // como mucho 32 descriptores
        const uint32_t w0 = leer32(d);
        if (!(w0 & TD0_OWN)) {                    // no es nuestro: se acabo
            if (!hay) {
                dmasr_ |= DMA_TBUS;               // buffer no disponible
                dmasr_ = (dmasr_ & ~DMA_TPS) | (6u << 20);
                dma_tdesc_ = d;
                actualiza_irq();
                return false;
            }
            break;
        }
        const uint32_t w1 = leer32(d + 4);
        const uint32_t w2 = leer32(d + 8);
        const uint32_t w3 = leer32(d + 12);
        if (w0 & TD0_FS) { trama.clear(); d_primero = d; }
        hay = true;
        const unsigned n1 = w1 & 0x1FFFu;
        if (n1) {
            std::vector<uint8_t> b(n1);
            ahb_read(w2, b.data(), n1);
            for (uint8_t x : b) trama.push_back(x);
        }
        dma_tbuf_ = w2;
        // El segundo buffer solo existe si NO estamos en modo encadenado: en
        // encadenado, esa palabra es el puntero al descriptor siguiente.
        if (!(w0 & TD0_TCH)) {
            const unsigned n2 = (w1 >> 16) & 0x1FFFu;
            if (n2) {
                std::vector<uint8_t> b(n2);
                ahb_read(w3, b.data(), n2);
                for (uint8_t x : b) trama.push_back(x);
            }
        }
        const bool ultimo = (w0 & TD0_LS) != 0;
        // Se devuelve el descriptor: OWN a cero y el resultado escrito.
        uint32_t r = w0 & ~(TD0_OWN | TD0_ES | TD0_UF | TD0_TTSS);
        if (ultimo && caps.ptp && (w0 & TD0_TTSE) && (ptptscr_ & 1u)) {
            const uint64_t ts = sella_tiempo();
            escribir32(d + 8, uint32_t(ts));            // TDES0/1 del sello
            escribir32(d + 12, uint32_t(ts >> 32));
            r |= TD0_TTSS;
        }
        escribir32(d, r);
        // Encadenado o anillo, igual que en recepcion pero con sus bits.
        const bool ch = (w0 & TD0_TCH) != 0, er = (w0 & TD0_TER) != 0;
        d = ch ? w3 : (er ? dmatdlar_ : d + 16u + 4u * ((dmabmr_ >> 2) & 0x1Fu));
        if (ultimo) break;
    }
    dma_tdesc_ = d;
    if (!hay || trama.empty()) return false;

    // Y ahora, por los pines.
    if (!(maccr_ & CR_TE)) { dmasr_ |= DMA_TUS | DMA_AIS; actualiza_irq(); return false; }
    dmasr_ = (dmasr_ & ~DMA_TPS) | (3u << 20);         // transfiriendo
    if (!emite_trama(trama)) {
        // Sin reloj del PHY no sale nada, y el MAC lo cuenta como vaciado de
        // la FIFO por debajo (underflow): es el sintoma real de un PHY que no
        // ha arrancado.
        dmasr_ |= DMA_TUS;
        dmasr_ = (dmasr_ & ~DMA_TPS) | (7u << 20);
        actualiza_irq();
        return false;
    }
    ult_tx_ = trama;
    ++n_tx_;
    mmc_tx(true);
    dmasr_ = (dmasr_ & ~DMA_TPS) | (7u << 20);
    // Se avisa solo si el descriptor lo pedia con IC [IR, §12.16.2].
    if (leer32(d_primero) & TD0_IC) dmasr_ |= DMA_TS;
    else                            dmasr_ |= DMA_TS;
    // En bucle de retorno (MACCR.LM) la trama vuelve por dentro: es como se
    // prueba el MAC sin PHY.
    if (maccr_ & CR_LM) { bool c = false; (void)c; dma_recibe(trama, false); }
    actualiza_irq();
    return true;
}

// ===========================================================================
// EL ANILLO DE RECEPCIÓN
// ===========================================================================
inline bool EthBase::dma_recibe(const std::vector<uint8_t>& t, bool err_crc) {
    if (!dmardlar_) return false;
    if (!(maccr_ & CR_RE)) return false;
    bool por_hash = false;
    const bool acepta = filtro_acepta(t, por_hash);
    if (!acepta && !(macffr_ & FF_RA)) {
        ++n_filtradas_;
        return false;                       // no se toca ningun descriptor
    }
    if (err_crc) { ++n_crc_; mmc_rx(false, true);
                   if (!(dmaomr_ & OMR_FEF)) return false; }
    else mmc_rx(true, false);

    uint32_t d = dma_rdesc_ ? dma_rdesc_ : dmardlar_;
    unsigned puesto = 0;
    const unsigned total = unsigned(t.size());

    for (unsigned k = 0; k < 32u && puesto < total; ++k) {
        const uint32_t w0 = leer32(d);
        if (!(w0 & RD0_OWN)) {
            dmasr_ |= DMA_RBUS;
            dmasr_ = (dmasr_ & ~DMA_RPS) | (4u << 17);
            ++dmamfbocr_;
            dma_rdesc_ = d;
            actualiza_irq();
            return false;
        }
        const uint32_t w1 = leer32(d + 4);
        const uint32_t w2 = leer32(d + 8);
        const uint32_t w3 = leer32(d + 12);
        const unsigned cap = w1 & 0x1FFFu;
        const unsigned n = (total - puesto < cap) ? (total - puesto) : cap;
        if (n) ahb_write(w2, t.data() + puesto, n);
        dma_rbuf_ = w2;
        puesto += n;
        const bool primero = (k == 0);
        const bool ultimo = (puesto >= total);
        uint32_t r = 0;
        if (primero) r |= RD0_FS;
        if (ultimo) {
            r |= RD0_LS;
            // FL es la longitud de TODA la trama, incluida la FCS, y solo
            // tiene sentido en el ultimo descriptor.
            r |= uint32_t((total + 4u) & 0x3FFFu) << 16;
            if (err_crc) r |= RD0_CE | RD0_ES;
            if (por_hash) r |= RD0_AFM;
        }
        escribir32(d, r);                     // OWN a cero: ya es del firmware
        const bool ch = (w1 & RD1_RCH) != 0, er = (w1 & RD1_RER) != 0;
        d = ch ? w3 : (er ? dmardlar_ : d + 16u + 4u * ((dmabmr_ >> 2) & 0x1Fu));
        if (ultimo) break;
    }
    dma_rdesc_ = d;
    ++n_rx_;
    dmasr_ |= DMA_RS;
    dmasr_ = (dmasr_ & ~DMA_RPS) | (7u << 17);
    actualiza_irq();
    return true;
}

// ===========================================================================
// Los dos procesos del camino de datos
// ===========================================================================
inline void EthBase::tx_proc() {
    tx_en_out.write(false);
    for (unsigned i = 0; i < 4; ++i) txd_out[i].write(false);
    for (;;) {
        // UN PERIFERICO APAGADO NO DEBE COSTAR TIEMPO DE SIMULACION. Mientras
        // el DMA de transmision este parado no hay nada que sondear: se espera
        // al EVENTO, no al reloj. Sondear cada 20 us cuando nadie ha encendido
        // el MAC son cincuenta mil despertares por segundo simulado a cambio
        // de nada [vease doc/stm32f407vg_coste_simulacion.md].
        if (!mac_activo() || !(dmaomr_ & OMR_ST)) { sc_core::wait(tx_ev_); continue; }
        sc_core::wait(sc_core::sc_time(20, sc_core::SC_US), tx_ev_);
        if (!mac_activo()) continue;
        if (!(dmaomr_ & OMR_ST)) continue;          // el DMA de TX esta parado
        while (dma_transmite()) { /* seguir mientras haya descriptores */ }
    }
}

inline void EthBase::rx_proc() {
    for (;;) {
        // Se espera al flanco de subida de RX_DV: es lo unico que anuncia una
        // trama. Sin el, el MAC no tiene forma de saber que llega algo.
        sc_core::wait(rx_dv_in.posedge_event());
        if (!mac_activo() || !(dmaomr_ & OMR_SR)) continue;
        std::vector<uint8_t> t;
        if (!recoge_trama(t)) continue;
        if (t.size() < 12) continue;
        // La FCS viaja en el cable; el MAC la comprueba y la quita o no segun
        // MACCR.ACS/APCS.
        bool crc_malo = true;
        if (t.size() >= 4) {
            const size_t n = t.size() - 4;
            const uint32_t esperada = eth_fcs(t.data(), n);
            uint32_t vista = 0;
            for (unsigned i = 0; i < 4; ++i)
                vista |= uint32_t(t[n + i]) << (8 * i);
            crc_malo = (vista != esperada);
            t.resize(n);                        // el MAC entrega sin FCS
        }
        // Magic Packet: si el MAC esta dormido, una trama con seis 0xFF y
        // dieciseis copias de su direccion lo despierta [IR, §12.16.2].
        if (caps.pmt && (macpmtcsr_ & PMT_PD) && (macpmtcsr_ & PMT_MPE)) {
            uint8_t mac[6] = {
                uint8_t(maca_[0].lr), uint8_t(maca_[0].lr >> 8),
                uint8_t(maca_[0].lr >> 16), uint8_t(maca_[0].lr >> 24),
                uint8_t(maca_[0].hr), uint8_t(maca_[0].hr >> 8)
            };
            unsigned rep = 0;
            for (size_t i = 0; i + 6 <= t.size(); ++i)
                if (std::equal(mac, mac + 6, t.begin() + long(i))) ++rep;
            if (rep >= 16) {
                macpmtcsr_ |= PMT_MPR;
                macpmtcsr_ &= ~PMT_PD;
                macsr_ |= SR_PMTS;
                o_wkup_ = true;
                publica();
                actualiza_irq();
                continue;
            }
        }
        if (macpmtcsr_ & PMT_PD) continue;      // dormido: no se recibe nada
        dma_recibe(t, crc_malo);
    }
}

// ===========================================================================
// Selección del tipo: compilación
// ===========================================================================
template <const EthCaps& C>
class EthT : public EthBase {
public:
    explicit EthT(sc_core::sc_module_name nm) : EthBase(nm, C) {}
    static constexpr const EthCaps& rasgos = C;
};

using Eth       = EthT<CAPS_ETH_F407>;
using EthRmii   = EthT<CAPS_ETH_RMII>;
using EthBasico = EthT<CAPS_ETH_BASIC>;
// Nombre histórico del bloque en el top.
using EthMac = Eth;

} // namespace stm32
#endif // STM32_PERIPH_ETH_MAC_H
