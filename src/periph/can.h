// =============================================================================
// can.h — bxCAN (Controller Area Network) [IR, §12.12]
//
// El STM32F407VG lleva DOS bxCAN, y a primera vista son el mismo bloque dos
// veces: el mismo mapa de registros, los mismos tres buzones de transmisión,
// las mismas dos FIFOs de tres marcos, el mismo temporizador de bit. Pero NO
// son intercambiables, y la diferencia no es de matiz:
//
//   * CAN1 es el DUEÑO DE LOS FILTROS. Los 28 bancos son un recurso único del
//     dispositivo y viven físicamente en el espacio de CAN1 (0x4000 6600). En
//     el de CAN2 esa ventana está RESERVADA: escribir allí no configura nada.
//   * El reparto lo decide CAN2SB, en CAN_FMR de CAN1: los bancos 0..CAN2SB-1
//     son de CAN1 y CAN2SB..27 de CAN2. Al reset vale 14, mitad y mitad.
//   * De ahí sale la consecuencia práctica que más quebraderos de cabeza da en
//     una placa real: PARA USAR CAN2 HAY QUE ENCENDER EL RELOJ DE CAN1. Si no,
//     sus filtros no se pueden tocar y el bloque no recibe nada, aunque el
//     firmware de CAN2 esté impecable.
//   * Y los vectores son distintos: CAN1 usa 19/20/21/22 y CAN2 63/64/65/66.
//
// Esa asimetría es exactamente el tipo de diferencia que el proyecto modela con
// su receta de familia, así que aquí se aplica igual que en el resto:
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using Can1      = BxCanT<CAPS_CAN1_F407>;   // maestro de 28 bancos
//         using Can2      = BxCanT<CAPS_CAN2_F407>;   // esclavo, sin filtros
//         using CanSingle = BxCanT<CAPS_CAN_SINGLE>;  // bxCAN único, 14 bancos
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         BxCanBase c{"c", base, CanCaps{...}};
//
// Los rasgos se aplican como MÁSCARA DE ESCRITURA de cada registro, de modo que
// un bit que la instancia no implementa lee cero exactamente igual que un bit
// reservado del silicio: en CAN2, la ventana de filtros entera lee cero.
//
// El protocolo está modelado A NIVEL DE BIT sobre los pines. El nodo genera su
// propio tiempo de bit a partir de BTR, saca la trama bit a bit por CAN_TX con
// su RELLENO DE BITS y su CRC15, y muestrea CAN_RX en el punto de muestreo. De
// ahí salen solos —no de un `if`— el arbitraje (quien manda recesivo y lee
// dominante ha perdido), el asentimiento, los errores de bit, de relleno, de
// forma y de CRC, y el recuento TEC/REC que lleva al bloque a bus-off.
//
// El bus en sí es un CABLE EN Y, y así está modelado en el banco: los
// transceptores se conectan a un nodo analógico con su terminador, y el estado
// dominante gana al recesivo porque un cero de baja impedancia gana a una
// resistencia de subida. El arbitraje no se decide con un `&&` en C++, se
// decide por superposición de conductancias.
// =============================================================================
#ifndef STM32_PERIPH_CAN_H
#define STM32_PERIPH_CAN_H

#include <array>
#include <cstdint>
#include <deque>
#include <vector>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos de la instancia. Describen en qué se diferencian los dos bxCAN del
// dispositivo, que es justo lo que pedía el análisis.
// ---------------------------------------------------------------------------
struct CanCaps {
    bool     filter_master  = true;  // ¿tiene la ventana de filtros?
    unsigned filter_banks   = 28;    // bancos que administra
    bool     shared_filters = true;  // ¿los reparte con otro bxCAN (CAN2SB)?
    unsigned tx_mailboxes   = 3;     // buzones de transmisión
    unsigned rx_fifos       = 2;     // FIFOs de recepción
    unsigned fifo_depth     = 3;     // marcos por FIFO
    bool     ext_id         = true;  // CAN 2.0B activo (identificador extendido)
    bool     ttcm           = true;  // comunicación disparada por tiempo
    const char* kind        = "bxCAN";
};

// --- Las variantes ---------------------------------------------------------
// CAN1: el maestro. Administra los 28 bancos y los reparte con CAN2.
constexpr CanCaps caps_can1_f407() {
    CanCaps c{};
    c.kind = "bxCAN maestro (28 bancos, comparte con CAN2)";
    return c;
}
// CAN2: el mismo bloque, pero SIN ventana de filtros propia. Los suyos se
// configuran desde CAN1 [IR, §12.12-integración].
constexpr CanCaps caps_can2_f407() {
    CanCaps c{};
    c.filter_master = false;
    c.filter_banks  = 0;
    c.kind = "bxCAN esclavo (filtros en CAN1)";
    return c;
}
// Un dispositivo con un SOLO bxCAN: dueño de todo el banco, sin nadie con
// quien repartirlo, y por tanto sin CAN2SB. No existe en el F407; sirve para
// comprobar que los ejes son independientes entre sí.
constexpr CanCaps caps_can_single() {
    CanCaps c{};
    c.filter_banks   = 14;
    c.shared_filters = false;
    c.ttcm = false;
    c.kind = "bxCAN unico (14 bancos, sin reparto)";
    return c;
}

inline constexpr CanCaps CAPS_CAN1_F407  = caps_can1_f407();
inline constexpr CanCaps CAPS_CAN2_F407  = caps_can2_f407();
inline constexpr CanCaps CAPS_CAN_SINGLE = caps_can_single();

// ---------------------------------------------------------------------------
// Un marco CAN, tal y como viaja por el hilo.
// ---------------------------------------------------------------------------
struct CanFrame {
    uint32_t id  = 0;        // 11 bits si !ide, 29 si ide
    bool     ide = false;    // identificador extendido (CAN 2.0B)
    bool     rtr = false;    // trama remota
    uint8_t  dlc = 0;        // 0..8
    std::array<uint8_t, 8> data{};
};

// ---------------------------------------------------------------------------
// CRC-15 del protocolo CAN: x^15 + x^14 + x^10 + x^8 + x^7 + x^4 + x^3 + 1,
// o sea 0x4599, con valor inicial cero, sobre los bits SIN RELLENO desde el
// bit de arranque hasta el último de datos.
// ---------------------------------------------------------------------------
inline uint16_t can_crc15(const std::vector<bool>& bits) {
    uint16_t crc = 0;
    for (bool b : bits) {
        const bool hi = ((crc >> 14) & 1u) != 0;
        crc = uint16_t((crc << 1) & 0x7FFFu);
        if (hi != b) crc ^= 0x4599u;
    }
    return crc;
}

// Los bits de un marco SIN relleno, desde el bit de arranque hasta el último
// de datos. Es lo que entra en el CRC.
inline std::vector<bool> can_frame_bits(const CanFrame& f) {
    std::vector<bool> v;
    auto push = [&v](uint32_t val, unsigned n) {
        for (int i = int(n) - 1; i >= 0; --i) v.push_back(((val >> i) & 1u) != 0);
    };
    v.push_back(false);                              // SOF: dominante
    if (!f.ide) {
        push(f.id & 0x7FFu, 11);                     // identificador de 11 bits
        v.push_back(f.rtr);                          // RTR
        v.push_back(false);                          // IDE = 0
        v.push_back(false);                          // r0
    } else {
        push((f.id >> 18) & 0x7FFu, 11);             // identificador base
        v.push_back(true);                           // SRR: siempre recesivo
        v.push_back(true);                           // IDE = 1
        push(f.id & 0x3FFFFu, 18);                   // extensión de 18 bits
        v.push_back(f.rtr);                          // RTR
        v.push_back(false);                          // r1
        v.push_back(false);                          // r0
    }
    push(f.dlc & 0xFu, 4);                           // DLC
    const unsigned n = (f.dlc > 8) ? 8u : f.dlc;
    if (!f.rtr) for (unsigned i = 0; i < n; ++i) push(f.data[i], 8);
    return v;
}

// Cuántos bits del principio son campo de ARBITRAJE (sin relleno): es donde un
// nodo puede perder la puja sin que eso sea un error.
inline unsigned can_arb_len(bool ide) { return ide ? 1u + 32u : 1u + 12u; }

// Aplica el RELLENO DE BITS: tras cinco bits iguales seguidos se inserta uno
// del valor contrario. `stuffed_flag` marca cuáles son de relleno, que es lo
// que permite distinguir una pérdida de arbitraje de un error de bit.
inline std::vector<bool> can_stuff(const std::vector<bool>& in,
                                   std::vector<bool>* stuffed_flag = nullptr) {
    std::vector<bool> out;
    if (stuffed_flag) stuffed_flag->clear();
    unsigned run = 0; bool last = false; bool first = true;
    for (bool b : in) {
        if (!first && b == last) ++run; else run = 1;
        last = b; first = false;
        out.push_back(b);
        if (stuffed_flag) stuffed_flag->push_back(false);
        if (run == 5) {
            out.push_back(!b);
            if (stuffed_flag) stuffed_flag->push_back(true);
            last = !b; run = 1;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// La trama COMPLETA que sale por el hilo: la parte con relleno (arranque,
// arbitraje, control, datos y CRC) seguida del delimitador de CRC, la ranura
// de asentimiento, su delimitador, el fin de trama y el espacio entre tramas.
// `n_stuffed` devuelve cuántos bits ocupa la parte sujeta a relleno.
// ---------------------------------------------------------------------------
inline std::vector<bool> can_wire_bits(const CanFrame& f,
                                       std::vector<bool>* stuffed_flag,
                                       unsigned* n_stuffed,
                                       unsigned* arb_bits) {
    std::vector<bool> sin_relleno = can_frame_bits(f);
    const uint16_t crc = can_crc15(sin_relleno);
    for (int i = 14; i >= 0; --i) sin_relleno.push_back(((crc >> i) & 1u) != 0);
    std::vector<bool> flags;
    std::vector<bool> out = can_stuff(sin_relleno, &flags);
    if (n_stuffed) *n_stuffed = unsigned(out.size());
    // Dónde acaba el arbitraje una vez insertados los bits de relleno.
    if (arb_bits) {
        const unsigned arb = can_arb_len(f.ide);
        unsigned reales = 0, i = 0;
        for (; i < out.size() && reales < arb; ++i) if (!flags[i]) ++reales;
        *arb_bits = i;
    }
    // El resto no lleva relleno.
    out.push_back(true);  flags.push_back(false);   // delimitador de CRC
    out.push_back(true);  flags.push_back(false);   // ranura de asentimiento
    out.push_back(true);  flags.push_back(false);   // delimitador de ACK
    for (unsigned i = 0; i < 7; ++i) { out.push_back(true); flags.push_back(false); }
    for (unsigned i = 0; i < 3; ++i) { out.push_back(true); flags.push_back(false); }
    if (stuffed_flag) *stuffed_flag = flags;
    return out;
}

// =============================================================================
// Implementación común. Los rasgos llegan por el constructor.
// =============================================================================
class BxCanBase : public BusSlave {
public:
    sc_core::sc_out<bool> irq_tx{"irq_tx"}, irq_rx0{"irq_rx0"},
                          irq_rx1{"irq_rx1"}, irq_sce{"irq_sce"};
    sc_core::sc_in<bool>  freeze{"freeze"};              // DBG_CANx_STOP
    // AF9: CAN_TX (salida push-pull) y CAN_RX (entrada). El hilo del bus, con
    // su cableado en Y, está fuera del MCU: aquí solo hay dos pines digitales.
    sc_core::sc_signal<bool> tx_out{"tx_out"}, tx_oe{"tx_oe"}, rx_in{"rx_in"};

    // ---- Desplazamientos [IR, §12.12] --------------------------------------
    enum : uint32_t {
        R_MCR = 0x000, R_MSR = 0x004, R_TSR = 0x008,
        R_RF0R = 0x00C, R_RF1R = 0x010, R_IER = 0x014,
        R_ESR = 0x018, R_BTR = 0x01C,
        R_TI0R = 0x180, R_RI0R = 0x1B0,
        R_FMR = 0x200, R_FM1R = 0x204, R_FS1R = 0x20C,
        R_FFA1R = 0x214, R_FA1R = 0x21C, R_F0R1 = 0x240
    };
    enum McrBit : uint32_t {
        M_INRQ = 1u << 0, M_SLEEP = 1u << 1, M_TXFP = 1u << 2, M_RFLM = 1u << 3,
        M_NART = 1u << 4, M_AWUM = 1u << 5, M_ABOM = 1u << 6, M_TTCM = 1u << 7,
        M_RESET = 1u << 15, M_DBF = 1u << 16
    };
    enum MsrBit : uint32_t {
        S_INAK = 1u << 0, S_SLAK = 1u << 1, S_ERRI = 1u << 2, S_WKUI = 1u << 3,
        S_SLAKI = 1u << 4, S_TXM = 1u << 8, S_RXM = 1u << 9, S_SAMP = 1u << 10,
        S_RX = 1u << 11
    };
    enum EsrBit : uint32_t {
        E_EWGF = 1u << 0, E_EPVF = 1u << 1, E_BOFF = 1u << 2
    };
    // Códigos del último error [IR, §12.12]
    enum Lec : uint32_t {
        LEC_NONE = 0, LEC_STUFF = 1, LEC_FORM = 2, LEC_ACK = 3,
        LEC_BIT1 = 4, LEC_BIT0 = 5, LEC_CRC = 6
    };

    BxCanBase(sc_core::sc_module_name nm, uint32_t base,
              const CanCaps& caps = CAPS_CAN1_F407)
        : BusSlave(nm, base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(BxCanBase);
        SC_THREAD(bit_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(rst_proc);   sensitive << rst_n;
        filters_.fill(0);
    }

    // CAN2 -> los filtros los administra CAN1.
    void bind_filter_master(BxCanBase* m) { filter_master_ = m; }
    const CanCaps& caps() const { return caps_; }

    // ---- Ventanas del banco de pruebas ------------------------------------
    uint64_t frames_tx() const { return n_tx_; }
    uint64_t frames_rx() const { return n_rx_; }
    unsigned tec() const { return tec_; }
    unsigned rec() const { return rec_; }
    double   bit_time() const { return bit_time_s(); }

protected:
    CanCaps    caps_;
    BxCanBase* filter_master_ = nullptr;

    // ---- Registros ---------------------------------------------------------
    uint32_t mcr_ = M_SLEEP | M_DBF, msr_ = S_SLAK | 0x0C00u;
    uint32_t tsr_ = 0x1C000000u, rf_[2] = {0, 0}, ier_ = 0, btr_ = 0x01230000u;
    uint32_t fmr_ = 0x2A1C0E01u, fm1r_ = 0, fs1r_ = 0, ffa1r_ = 0, fa1r_ = 0;
    std::array<uint32_t, 56> filters_{};          // 28 bancos x 2 registros

    struct TxMb { uint32_t tir = 0, tdtr = 0, tdlr = 0, tdhr = 0; };
    std::array<TxMb, 3> mb_{};
    struct RxMsg { uint32_t rir = 0, rdtr = 0, rdlr = 0, rdhr = 0; };
    std::deque<RxMsg> fifo_[2];

    unsigned tec_ = 0, rec_ = 0;
    uint64_t n_tx_ = 0, n_rx_ = 0;

    bool o_tx_ = true, o_oe_ = false;
    bool o_irq_[4] = {false, false, false, false};
    sc_core::sc_event pub_ev_, ev_;

    // =======================================================================
    // Campos de los registros
    // =======================================================================
    unsigned brp() const { return btr_ & 0x3FFu; }
    unsigned ts1() const { return ((btr_ >> 16) & 0xFu) + 1u; }
    unsigned ts2() const { return ((btr_ >> 20) & 0x7u) + 1u; }
    bool     lbkm() const { return (btr_ >> 30) & 1u; }
    bool     silm() const { return (btr_ >> 31) & 1u; }
    bool     nart() const { return (mcr_ & M_NART) != 0; }
    bool     txfp() const { return (mcr_ & M_TXFP) != 0; }
    bool     rflm() const { return (mcr_ & M_RFLM) != 0; }
    bool     abom() const { return (mcr_ & M_ABOM) != 0; }

    // Un cuanto de tiempo y un tiempo de bit, tal y como los define BTR:
    //   t_q = (BRP+1)/PCLK1 ; t_bit = (1 + TS1 + TS2) * t_q  [IR, §12.12]
    double tq_s() const {
        const double f = clk_hz.read();
        return (f > 0.0) ? (double(brp() + 1u) / f) : 0.0;
    }
    double bit_time_s() const { return tq_s() * double(1u + ts1() + ts2()); }

    // =======================================================================
    // Máscaras de escritura: aquí ACTÚAN los rasgos
    // =======================================================================
    uint32_t mcr_mask() const {
        uint32_t m = 0x0001007Fu | M_RESET;          // INRQ..ABOM, RESET, DBF
        if (caps_.ttcm) m |= M_TTCM;
        return m;
    }
    uint32_t banks() const { return caps_.filter_banks; }
    uint32_t bank_mask() const {
        return (banks() >= 32) ? 0xFFFFFFFFu : ((1u << banks()) - 1u);
    }
    uint32_t fmr_mask() const {
        uint32_t m = 1u;                             // FINIT
        if (caps_.shared_filters) m |= 0x3F00u;      // CAN2SB
        return m;
    }
    // Los buzones que la variante no tiene no aparecen en TSR.
    uint32_t tsr_mask() const {
        uint32_t m = 0;
        for (unsigned i = 0; i < caps_.tx_mailboxes; ++i) {
            m |= 0xFFu << (8 * i);                   // RQCP/TXOK/ALST/TERR/ABRQ
            m |= (1u << (26 + i)) | (1u << (29 + i));// TME y LOW
        }
        m |= 3u << 24;                               // CODE
        return m;
    }

    // ¿Cae este desplazamiento en la ventana de filtros?
    static bool es_filtro(uint32_t off) { return off >= R_FMR && off < 0x320u; }

    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        // La ventana de filtros SOLO existe en el maestro. En CAN2 está
        // reservada y lee cero, igual que en el silicio [IR, §12.12].
        if (es_filtro(off)) {
            if (!caps_.filter_master) return 0;
            return leer_filtro(off);
        }
        if (off >= R_TI0R && off < R_TI0R + 0x30u) {
            const unsigned i = (off - R_TI0R) / 0x10u;
            if (i >= caps_.tx_mailboxes) return 0;
            switch ((off - R_TI0R) % 0x10u) {
                case 0x0: return mb_[i].tir;
                case 0x4: return mb_[i].tdtr;
                case 0x8: return mb_[i].tdlr;
                default:  return mb_[i].tdhr;
            }
        }
        if (off >= R_RI0R && off < R_RI0R + 0x20u) {
            const unsigned f = (off - R_RI0R) / 0x10u;
            if (f >= caps_.rx_fifos || fifo_[f].empty()) return 0;
            const RxMsg& m = fifo_[f].front();
            switch ((off - R_RI0R) % 0x10u) {
                case 0x0: return m.rir;
                case 0x4: return m.rdtr;
                case 0x8: return m.rdlr;
                default:  return m.rdhr;
            }
        }
        switch (off) {
            case R_MCR:  return mcr_ & mcr_mask();
            case R_MSR:  return msr_;
            case R_TSR:  return tsr_ & tsr_mask();
            case R_RF0R: return rf_[0];
            case R_RF1R: return (caps_.rx_fifos > 1) ? rf_[1] : 0;
            case R_IER:  return ier_;
            case R_ESR:  return esr();
            case R_BTR:  return btr_;
            default:     return 0;
        }
    }

    uint32_t esr() const {
        uint32_t v = uint32_t(rec_ & 0xFFu) << 24 | uint32_t(tec_ & 0xFFu) << 16;
        v |= (lec_ & 7u) << 4;
        if (tec_ >= 96 || rec_ >= 96)  v |= E_EWGF;
        if (tec_ >= 128 || rec_ >= 128) v |= E_EPVF;
        if (boff_) v |= E_BOFF;
        return v;
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        if (es_filtro(off)) { if (caps_.filter_master) escribir_filtro(off, v); return; }

        if (off >= R_TI0R && off < R_TI0R + 0x30u) {
            const unsigned i = (off - R_TI0R) / 0x10u;
            if (i >= caps_.tx_mailboxes) return;
            switch ((off - R_TI0R) % 0x10u) {
                case 0x0: {
                    // Escribir TIxR con TXRQ es lo que PIDE el envío. A partir
                    // de ahí el buzón deja de estar libre y es del hardware.
                    uint32_t nv = v;
                    if (!caps_.ext_id) nv &= ~(1u << 2);      // sin IDE
                    mb_[i].tir = nv;
                    if (nv & 1u) pedir_envio(i);
                    return;
                }
                case 0x4: mb_[i].tdtr = v & 0xFFFF010Fu; return;
                case 0x8: mb_[i].tdlr = v; return;
                default:  mb_[i].tdhr = v; return;
            }
        }
        if (off >= R_RI0R && off < R_RI0R + 0x20u) return;    // solo lectura

        switch (off) {
            case R_MCR: {
                const uint32_t nuevo = v & mcr_mask();
                if (nuevo & M_RESET) { reset_bloque(); return; }
                const bool inrq_antes = (mcr_ & M_INRQ) != 0;
                mcr_ = nuevo & ~M_RESET;
                // INRQ/INAK y SLEEP/SLAK: el bloque confirma con su bandera.
                if ((mcr_ & M_INRQ) && !inrq_antes) msr_ |= S_INAK;
                if (!(mcr_ & M_INRQ)) msr_ &= ~S_INAK;
                if (mcr_ & M_SLEEP) { msr_ |= S_SLAK; }
                else                { msr_ &= ~S_SLAK; }
                ev_.notify(sc_core::SC_ZERO_TIME);
                actualiza_irq();
                return;
            }
            case R_MSR:
                // Solo ERRI, WKUI y SLAKI son escribibles, y son w1c.
                if (v & S_ERRI)  msr_ &= ~S_ERRI;
                if (v & S_WKUI)  msr_ &= ~S_WKUI;
                if (v & S_SLAKI) msr_ &= ~S_SLAKI;
                actualiza_irq();
                return;
            case R_TSR: {
                for (unsigned i = 0; i < caps_.tx_mailboxes; ++i) {
                    // RQCP es w1c y arrastra a TXOK/ALST/TERR, como en el
                    // silicio: una sola escritura limpia el buzón entero.
                    if (v & (1u << (8 * i))) tsr_ &= ~(0xFu << (8 * i));
                    // ABRQ: aborta la petición si todavía no está en el hilo.
                    if (v & (1u << (8 * i + 7))) abortar(i);
                }
                actualiza_irq();
                return;
            }
            case R_RF0R: liberar_fifo(0, v); return;
            case R_RF1R: if (caps_.rx_fifos > 1) liberar_fifo(1, v); return;
            case R_IER:  ier_ = v & 0x00038F7Fu; actualiza_irq(); return;
            case R_ESR:
                // Solo LEC es escribible, y para ponerlo a cero o a 7.
                lec_ = (v >> 4) & 7u;
                return;
            case R_BTR:
                // BTR SOLO se deja escribir en modo de inicialización, igual
                // que en el silicio: cambiar el tiempo de bit con el bloque en
                // marcha desincronizaría a todo el bus [IR, §12.12].
                if (msr_ & S_INAK) btr_ = v & 0xC37F03FFu;
                return;
            default: return;
        }
    }

    // =======================================================================
    // Los filtros: 28 bancos, y el reparto entre CAN1 y CAN2
    // =======================================================================
    uint32_t leer_filtro(uint32_t off) const {
        switch (off) {
            case R_FMR:   return fmr_ & (fmr_mask() | 0x2A1C0000u);
            case R_FM1R:  return fm1r_ & bank_mask();
            case R_FS1R:  return fs1r_ & bank_mask();
            case R_FFA1R: return ffa1r_ & bank_mask();
            case R_FA1R:  return fa1r_ & bank_mask();
            default:
                if (off >= R_F0R1 && off < R_F0R1 + 8u * banks())
                    return filters_[(off - R_F0R1) / 4u];
                return 0;
        }
    }
    void escribir_filtro(uint32_t off, uint32_t v) {
        switch (off) {
            case R_FMR:   fmr_ = (fmr_ & ~fmr_mask()) | (v & fmr_mask()); return;
            // Los bancos SOLO se dejan tocar con FINIT puesto: es el modo de
            // inicialización del filtrado [IR, §12.12].
            case R_FM1R:  if (finit()) fm1r_  = v & bank_mask(); return;
            case R_FS1R:  if (finit()) fs1r_  = v & bank_mask(); return;
            case R_FFA1R: if (finit()) ffa1r_ = v & bank_mask(); return;
            case R_FA1R:  fa1r_ = v & bank_mask(); return;
            default:
                if (off >= R_F0R1 && off < R_F0R1 + 8u * banks()) {
                    const unsigned i = (off - R_F0R1) / 4u;
                    // Un banco activo no se puede modificar sin desactivarlo o
                    // sin FINIT: si no, un marco a medio filtrar vería el banco
                    // cambiando bajo sus pies.
                    const unsigned b = i / 2u;
                    if (finit() || !((fa1r_ >> b) & 1u)) filters_[i] = v;
                }
                return;
        }
    }
    bool     finit()  const { return (fmr_ & 1u) != 0; }
    unsigned can2sb() const {
        if (!caps_.shared_filters) return banks();
        const unsigned s = (fmr_ >> 8) & 0x3Fu;
        return (s > banks()) ? banks() : s;
    }
    // Los bancos que le tocan a ESTA instancia.
    void rango_bancos(unsigned& lo, unsigned& hi) const {
        const BxCanBase* m = caps_.filter_master ? this : filter_master_;
        if (!m) { lo = hi = 0; return; }
        if (caps_.filter_master) { lo = 0; hi = m->can2sb(); }
        else                     { lo = m->can2sb(); hi = m->banks(); }
    }

    // =======================================================================
    // El filtrado de un marco recibido.
    //
    // Devuelve true si algún banco lo acepta, y deja en `fifo` la FIFO de
    // destino y en `fmi` el índice del filtro que casó, que es lo que el
    // firmware lee en RDTxR para saber POR QUÉ le ha llegado el marco.
    // =======================================================================
    bool filtrar(const CanFrame& f, unsigned& fifo, unsigned& fmi) const {
        const BxCanBase* m = caps_.filter_master ? this : filter_master_;
        if (!m) return false;
        unsigned lo = 0, hi = 0;
        rango_bancos(lo, hi);
        // El registro de identificador tal y como lo colocan los filtros.
        const uint32_t idr = id_a_registro(f);
        // La numeración de filtros (FMI) es POR FIFO y sigue el orden de los
        // bancos; cada banco aporta tantos números como filtros contenga.
        unsigned idx[2] = {0, 0};
        for (unsigned b = 0; b < m->banks(); ++b) {
            const bool activo = ((m->fa1r_ >> b) & 1u) != 0;
            const bool lista  = ((m->fm1r_ >> b) & 1u) != 0;   // 1: lista de IDs
            const bool ancho  = ((m->fs1r_ >> b) & 1u) != 0;   // 1: 32 bits
            const unsigned dest = ((m->ffa1r_ >> b) & 1u) ? 1u : 0u;
            const uint32_t r1 = m->filters_[2 * b], r2 = m->filters_[2 * b + 1];
            const unsigned n = ancho ? (lista ? 2u : 1u) : (lista ? 4u : 2u);
            const bool mio = (b >= lo && b < hi);
            if (!activo || !mio) { if (activo) idx[dest] += n; continue; }
            bool casa = false; unsigned k = 0;
            if (ancho && lista) {                     // dos identificadores de 32
                if (idr == r1) { casa = true; k = 0; }
                else if (idr == r2) { casa = true; k = 1; }
            } else if (ancho && !lista) {             // identificador y máscara
                if (((idr ^ r1) & r2) == 0) { casa = true; k = 0; }
            } else if (!ancho && lista) {             // cuatro de 16 bits
                const uint16_t c = corto(f);
                const uint16_t v[4] = {uint16_t(r1), uint16_t(r1 >> 16),
                                       uint16_t(r2), uint16_t(r2 >> 16)};
                for (unsigned i = 0; i < 4; ++i)
                    if (!casa && c == v[i]) { casa = true; k = i; }
            } else {                                  // dos de 16 con máscara
                const uint16_t c = corto(f);
                if (((c ^ uint16_t(r1)) & uint16_t(r1 >> 16)) == 0) { casa = true; k = 0; }
                else if (((c ^ uint16_t(r2)) & uint16_t(r2 >> 16)) == 0) { casa = true; k = 1; }
            }
            if (casa) { fifo = dest; fmi = idx[dest] + k; return true; }
            idx[dest] += n;
        }
        return false;
    }
    static uint32_t id_a_registro(const CanFrame& f) {
        uint32_t v = 0;
        if (f.ide) v = (f.id << 3) | (1u << 2);
        else       v = ((f.id & 0x7FFu) << 21);
        if (f.rtr) v |= 1u << 1;
        return v;
    }
    // La versión de 16 bits que usan los bancos estrechos: STID en 15:5,
    // RTR en 4, IDE en 3 y los tres bits altos de la extensión en 2:0.
    static uint16_t corto(const CanFrame& f) {
        uint16_t v = 0;
        const uint32_t stid = f.ide ? ((f.id >> 18) & 0x7FFu) : (f.id & 0x7FFu);
        v = uint16_t(stid << 5);
        if (f.rtr) v |= 1u << 4;
        if (f.ide) v = uint16_t(v | (1u << 3) | ((f.id >> 15) & 7u));
        return v;
    }

    // =======================================================================
    // Buzones de transmisión
    // =======================================================================
    void pedir_envio(unsigned i) {
        tsr_ &= ~(1u << (26 + i));                  // el buzón deja de estar libre
        actualiza_code();
        ev_.notify(sc_core::SC_ZERO_TIME);
        actualiza_irq();
    }
    void abortar(unsigned i) {
        if (tx_activo_ == int(i)) return;           // ya está en el hilo: tarde
        if (!(mb_[i].tir & 1u)) return;
        mb_[i].tir &= ~1u;
        tsr_ |= (1u << (26 + i)) | (1u << (8 * i)); // libre y RQCP
        actualiza_code();
        actualiza_irq();
    }
    bool libre(unsigned i) const { return (tsr_ >> (26 + i)) & 1u; }

    // CODE apunta al primer buzón libre; TME dice cuáles lo están.
    void actualiza_code() {
        unsigned c = 0;
        for (unsigned i = 0; i < caps_.tx_mailboxes; ++i)
            if (libre(i)) { c = i; break; }
        tsr_ = (tsr_ & ~(3u << 24)) | ((c & 3u) << 24);
    }

    // Elige el buzón que sale al hilo. Con TXFP manda el ORDEN DE PETICIÓN
    // (FIFO); sin él, la PRIORIDAD DEL IDENTIFICADOR, que es lo natural en CAN:
    // el identificador más bajo gana [IR, §12.12].
    int siguiente_buzon() const {
        int mejor = -1;
        uint32_t mejor_id = 0xFFFFFFFFu;
        for (unsigned i = 0; i < caps_.tx_mailboxes; ++i) {
            if (libre(i) || !(mb_[i].tir & 1u)) continue;
            if (txfp()) { if (mejor < 0 || orden_[i] < orden_[unsigned(mejor)]) mejor = int(i); }
            else {
                const uint32_t id = clave_prioridad(mb_[i].tir);
                if (id < mejor_id) { mejor_id = id; mejor = int(i); }
            }
        }
        return mejor;
    }
    static uint32_t clave_prioridad(uint32_t tir) {
        // Un identificador estándar y uno extendido se comparan por los 11 bits
        // de base primero, que es lo que decide el arbitraje en el hilo.
        return (tir >> 3) & 0x1FFFFFFFu;
    }
    std::array<uint64_t, 3> orden_{};
    uint64_t n_peticion_ = 0;

    CanFrame marco_de_buzon(unsigned i) const {
        CanFrame f;
        const uint32_t t = mb_[i].tir;
        f.ide = ((t >> 2) & 1u) != 0;
        f.rtr = ((t >> 1) & 1u) != 0;
        f.id  = f.ide ? (t >> 3) : ((t >> 21) & 0x7FFu);
        f.dlc = uint8_t(mb_[i].tdtr & 0xFu);
        if (f.dlc > 8) f.dlc = 8;
        for (unsigned k = 0; k < 4; ++k) {
            f.data[k]     = uint8_t(mb_[i].tdlr >> (8 * k));
            f.data[k + 4] = uint8_t(mb_[i].tdhr >> (8 * k));
        }
        return f;
    }

    // =======================================================================
    // FIFOs de recepción
    // =======================================================================
    void guardar(const CanFrame& f, unsigned fifo, unsigned fmi) {
        if (fifo >= caps_.rx_fifos) return;
        if (fifo_[fifo].size() >= caps_.fifo_depth) {
            // FIFO llena. Con RFLM el marco nuevo SE PIERDE; sin él, machaca
            // al último. En los dos casos se marca desbordamiento.
            rf_[fifo] |= 1u << 4;                    // FOVR
            if (rflm()) { actualiza_irq(); return; }
            fifo_[fifo].pop_back();
        }
        RxMsg m;
        m.rir  = id_a_registro(f);
        m.rdtr = uint32_t(f.dlc & 0xFu) | (uint32_t(fmi & 0xFFu) << 8);
        m.rdlr = m.rdhr = 0;
        for (unsigned k = 0; k < 4; ++k) {
            m.rdlr |= uint32_t(f.data[k]) << (8 * k);
            m.rdhr |= uint32_t(f.data[k + 4]) << (8 * k);
        }
        fifo_[fifo].push_back(m);
        ++n_rx_;
        actualiza_fifo(fifo);
        actualiza_irq();
    }
    void liberar_fifo(unsigned f, uint32_t v) {
        if (v & (1u << 5)) { if (!fifo_[f].empty()) fifo_[f].pop_front(); }  // RFOM
        if (v & (1u << 4)) rf_[f] &= ~(1u << 4);                             // FOVR w1c
        if (v & (1u << 3)) rf_[f] &= ~(1u << 3);                             // FULL w1c
        actualiza_fifo(f);
        actualiza_irq();
    }
    void actualiza_fifo(unsigned f) {
        const uint32_t n = uint32_t(fifo_[f].size());
        rf_[f] = (rf_[f] & ~3u) | (n & 3u);
        if (n >= caps_.fifo_depth) rf_[f] |= 1u << 3;                        // FULL
    }

    // =======================================================================
    // Interrupciones: cuatro vectores, cada uno con su parte de IER
    // =======================================================================
    void actualiza_irq() {
        const bool tx = (ier_ & 1u) &&
            ((tsr_ & 1u) || (tsr_ & (1u << 8)) || (tsr_ & (1u << 16)));
        auto rxirq = [&](unsigned f) {
            const unsigned sh = (f == 0) ? 1u : 4u;
            bool i = false;
            if ((ier_ >> sh) & 1u)       i = i || (rf_[f] & 3u) != 0;   // FMPIE
            if ((ier_ >> (sh + 1)) & 1u) i = i || (rf_[f] & (1u << 3)); // FFIE
            if ((ier_ >> (sh + 2)) & 1u) i = i || (rf_[f] & (1u << 4)); // FOVIE
            return i;
        };
        const uint32_t e = esr();
        bool sce = false;
        if ((ier_ >> 8) & 1u) sce = sce || (e & E_EWGF);
        if ((ier_ >> 9) & 1u) sce = sce || (e & E_EPVF);
        if ((ier_ >> 10) & 1u) sce = sce || (e & E_BOFF);
        if ((ier_ >> 11) & 1u) sce = sce || ((e >> 4) & 7u) != 0;
        sce = ((ier_ >> 15) & 1u) && sce;                    // ERRIE es la llave
        if (sce) msr_ |= S_ERRI;
        if (((ier_ >> 16) & 1u) && (msr_ & S_WKUI)) sce = true;
        if (((ier_ >> 17) & 1u) && (msr_ & S_SLAKI)) sce = true;
        const bool n[4] = {tx, rxirq(0), caps_.rx_fifos > 1 && rxirq(1), sce};
        bool cambio = false;
        for (unsigned i = 0; i < 4; ++i)
            if (n[i] != o_irq_[i]) { o_irq_[i] = n[i]; cambio = true; }
        if (cambio) pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        irq_tx.write(o_irq_[0]);  irq_rx0.write(o_irq_[1]);
        irq_rx1.write(o_irq_[2]); irq_sce.write(o_irq_[3]);
        tx_out.write(o_tx_);      tx_oe.write(o_oe_);
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }

    // =======================================================================
    // El motor de bit. Es quien hace de verdad el protocolo.
    // =======================================================================
    uint32_t lec_ = 0;
    bool     boff_ = false;
    int      tx_activo_ = -1;
    bool     soy_emisor_ = false;

    bool en_marcha() const {
        return clock_enabled() && !(msr_ & S_INAK) && !(msr_ & S_SLAK) &&
               !freeze.read() && bit_time_s() > 0.0 && !boff_;
    }
    // Lo que ve el nodo en el hilo. En bucle cerrado el bloque se oye a sí
    // mismo sin salir al pin, que es para lo que sirve ese modo.
    bool leer_bus() const { return lbkm() ? o_tx_ : rx_in.read(); }
    void poner_bus(bool nivel) {
        // En modo silencioso el nodo NUNCA manda dominante: solo escucha
        // [IR, §12.12]. Es lo que permite engancharse a un bus sin perturbarlo.
        const bool real = (silm() && !lbkm()) ? true : nivel;
        if (real != o_tx_ || !o_oe_) { o_tx_ = real; o_oe_ = true; publish(); }
    }

    void bit_proc() {
        for (;;) {
            if (!en_marcha()) {
                if (o_tx_ != true || o_oe_) { o_tx_ = true; o_oe_ = false; publish(); }
                wait(ev_ | rx_in.value_changed_event() | rst_n.value_changed_event() |
                     freeze.value_changed_event() | clk_hz.value_changed_event());
                continue;
            }
            const double tb = bit_time_s();
            const double t_muestra = tb * double(1u + ts1()) / double(1u + ts1() + ts2());

            // ---- Reposo: se espera a que haya algo que hacer -----------------
            if (estado_ == IDLE) {
                poner_bus(true);
                const bool quiero = (siguiente_buzon() >= 0);
                if (!quiero && leer_bus()) {
                    wait(sc_core::sc_time(tb, sc_core::SC_SEC),
                         ev_ | rx_in.value_changed_event());
                    // Sincronización dura: un flanco a dominante en reposo es
                    // el bit de arranque de otro nodo, y el tiempo de bit se
                    // reengancha a él.
                    if (!leer_bus()) { empezar_recepcion(); }
                    continue;
                }
                // Con un buzón pidiendo, el nodo TRANSMITE aunque el hilo se
                // acabe de poner dominante. No es atropellar a nadie: estando
                // en reposo, ese dominante es el bit de arranque de otro nodo
                // que ha empezado EN EL MISMO BIT, y lo que toca entonces es
                // pujar, no callarse. De ahí sale el arbitraje.
                if (quiero) empezar_transmision();
                else        empezar_recepcion();
            }

            // ---- Un tiempo de bit -------------------------------------------
            poner_bus(bit_a_emitir());
            wait(sc_core::sc_time(t_muestra, sc_core::SC_SEC));
            const bool visto = leer_bus();
            procesar_bit(visto);
            wait(sc_core::sc_time(tb - t_muestra, sc_core::SC_SEC));
        }
    }

    // ---- Estado del motor --------------------------------------------------
    enum Estado { IDLE, TX, RX, ERROR_FLAG, INTERMISSION };
    Estado estado_ = IDLE;
    std::vector<bool> tx_bits_, tx_stuff_;
    unsigned tx_i_ = 0, tx_arb_ = 0, tx_nstuff_ = 0;
    unsigned err_i_ = 0, recesivos_ = 0;
    bool     ack_visto_ = false;

    // Receptor
    std::vector<bool> rx_bits_;          // bits SIN relleno
    unsigned rx_run_ = 0; bool rx_last_ = false; bool rx_first_ = true;
    unsigned rx_esperados_ = 0;          // bits que faltan hasta el CRC
    unsigned rx_fase_ = 0;
    CanFrame rx_f_{};
    unsigned post_ = 0;                  // bits tras el CRC ya recorridos
    bool     pend_relleno_ = false;      // queda un bit de relleno tras el CRC
    bool     ack_pendiente_ = false;     // el marco esta bien: hay que asentirlo

    void empezar_transmision() {
        const int i = siguiente_buzon();
        if (i < 0) { estado_ = IDLE; return; }
        tx_activo_ = i;
        const CanFrame f = marco_de_buzon(unsigned(i));
        tx_bits_ = can_wire_bits(f, &tx_stuff_, &tx_nstuff_, &tx_arb_);
        tx_i_ = 0;
        ack_visto_ = false;
        estado_ = TX;
        soy_emisor_ = true;
        msr_ |= S_TXM;
        empezar_recepcion_interna();
    }
    void empezar_recepcion() {
        estado_ = RX;
        soy_emisor_ = false;
        msr_ |= S_RXM;
        empezar_recepcion_interna();
        // El bit de arranque TODAVIA no se ha muestreado: lo hara el propio
        // motor dentro de este mismo tiempo de bit, y el receptor lo recogera
        // en su fase 0. Contabilizarlo aqui desplazaria el marco entero.
    }
    void empezar_recepcion_interna() {
        rx_bits_.clear(); rx_run_ = 0; rx_first_ = true;
        rx_fase_ = 0; post_ = 0; pend_relleno_ = false;
        ack_pendiente_ = false; rx_f_ = CanFrame{};
    }

    bool bit_a_emitir() const {
        if (estado_ == ERROR_FLAG) return err_i_ >= 6;   // 6 dominantes y suelta
        if (estado_ == TX && tx_i_ < tx_bits_.size()) return tx_bits_[tx_i_];
        // EL ASENTIMIENTO. Un receptor que ha visto el marco entero y con el
        // CRC correcto pone la ranura de asentimiento a DOMINANTE. Es su unica
        // intervencion en un marco ajeno, y sin ella el emisor da el marco por
        // no entregado y lo repite indefinidamente [IR, 12.12].
        if (estado_ == RX && rx_fase_ == 2 && !pend_relleno_ &&
            post_ == 1 && ack_pendiente_) return false;
        return true;
    }

    // Cada bit muestreado pasa por DOS caminos: el del transmisor, que comprueba
    // que el hilo lleva lo que el puso, y el del receptor, que va armando el
    // marco. Los dos a la vez, porque un nodo CAN SIEMPRE escucha lo que
    // transmite: es de ahi de donde salen el arbitraje y el asentimiento.
    void procesar_bit(bool visto) {
        if (estado_ == ERROR_FLAG) {
            ++err_i_;
            if (err_i_ >= 6) { if (visto) ++recesivos_; else recesivos_ = 0; }
            if (recesivos_ >= 8) { estado_ = IDLE; tx_activo_ = -1;
                                   msr_ &= ~(S_TXM | S_RXM); publish(); }
            return;
        }
        if (estado_ == TX) {
            const bool emitido = tx_bits_[tx_i_];
            const bool en_ack = (tx_i_ == tx_nstuff_ + 1u);
            if (en_ack) {
                // La ranura de asentimiento: el emisor manda recesivo y espera
                // que ALGUIEN la ponga a dominante. Si nadie lo hace, es un
                // error de asentimiento y el marco no ha llegado a nadie.
                //
                // En BUCLE CERRADO el bloque se asiente a si mismo: no hay bus,
                // y el modo existe justamente para probar el nodo solo, sin
                // nadie mas conectado [IR, 12.12].
                ack_visto_ = lbkm() ? true : !visto;
            } else if (visto != emitido) {
                if (tx_i_ < tx_arb_ && emitido && !tx_stuff_[tx_i_]) {
                    // Se ha perdido el arbitraje: otro nodo manda un
                    // identificador de prioridad mayor. NO es un error: el
                    // buzón se queda pidiendo y se reintenta.
                    perder_arbitraje();
                    // Y ESTE bit, el que delata la derrota, es del ganador: hay
                    // que darselo al receptor o el marco entero se desplaza un
                    // bit y acaba en un CRC que no cuadra.
                    recibir_bit(visto);
                    return;
                }
                // Fuera del arbitraje, leer distinto de lo emitido es un error
                // de bit: alguien está peleando por el hilo.
                error(emitido ? LEC_BIT1 : LEC_BIT0, true);
                return;
            }
            ++tx_i_;
            // El receptor sigue el marco en paralelo. Si mas adelante se pierde
            // el arbitraje, ya lleva los bits correctos y puede continuar sin
            // haberse perdido el principio.
            recibir_bit(visto);
            if (estado_ == TX && tx_i_ >= tx_bits_.size()) fin_transmision();
            return;
        }
        // ---- Receptor -------------------------------------------------------
        recibir_bit(visto);
    }

    void perder_arbitraje() {
        const unsigned i = unsigned(tx_activo_);
        tsr_ |= 1u << (8 * i + 2);              // ALST
        tx_activo_ = -1;
        msr_ &= ~S_TXM;
        // El nodo pasa a RECEPTOR del marco que ha ganado. No hay que empezar de
        // cero: su receptor venia siguiendo el hilo bit a bit, y hasta la puja
        // lo que habia en el hilo era identico a lo suyo.
        estado_ = RX;
        soy_emisor_ = false;
        actualiza_irq();
    }

    void fin_transmision() {
        const unsigned i = unsigned(tx_activo_);
        if (!ack_visto_) {
            // Nadie ha asentido. El marco se da por no entregado.
            error(LEC_ACK, true);
            return;
        }
        tsr_ |= (1u << (8 * i)) | (1u << (8 * i + 1));   // RQCP y TXOK
        tsr_ |= 1u << (26 + i);                          // buzón libre otra vez
        mb_[i].tir &= ~1u;
        ++n_tx_;
        if (tec_ > 0) --tec_;
        tx_activo_ = -1;
        estado_ = IDLE;
        msr_ &= ~S_TXM;
        actualiza_code();
        actualiza_irq();
        publish();
    }

    // El receptor: quita el relleno y va armando el marco.
    void recibir_bit(bool b) {
        if (rx_fase_ == 0) {                     // esperando el bit de arranque
            if (b) return;
            rx_bits_.clear();
            rx_bits_.push_back(false);
            rx_run_ = 1; rx_last_ = false; rx_first_ = false;
            rx_fase_ = 1;
            msr_ |= S_RXM;
            return;
        }
        if (rx_fase_ == 1) {                     // zona con relleno
            if (rx_run_ == 5) {
                // Este bit es de relleno: tiene que ser el contrario.
                if (b == rx_last_) { error(LEC_STUFF, false); return; }
                rx_last_ = b; rx_run_ = 1;
                return;                          // no entra en el marco
            }
            rx_bits_.push_back(b);
            if (b == rx_last_) ++rx_run_; else rx_run_ = 1;
            rx_last_ = b;
            // Si el ULTIMO bit del CRC completa una racha de cinco, todavia
            // viene un bit de relleno detras: sigue estando dentro de la zona
            // con relleno, y sin tragarselo el delimitador de CRC se muestrea
            // un bit antes de tiempo.
            if (marco_completo()) {
                rx_fase_ = 2;
                pend_relleno_ = (rx_run_ == 5);
                // El CRC se comprueba AQUI, en cuanto llega su ultimo bit,
                // porque de el depende si este nodo va a asentir el marco dos
                // bits mas adelante.
                ack_pendiente_ = crc_cuadra();
            }
            return;
        }
        // ---- Zona sin relleno: delimitador, ACK, fin de trama ---------------
        if (pend_relleno_) {
            pend_relleno_ = false;
            if (b == rx_last_) { error(LEC_STUFF, false); return; }
            return;
        }
        ++post_;
        if (post_ == 1) {                        // delimitador de CRC
            if (!b) { error(LEC_FORM, false); return; }
            return;
        }
        if (post_ == 2) return;                  // ranura de ACK (ya emitida)
        if (post_ == 3) {                        // delimitador de ACK
            if (!b) { error(LEC_FORM, false); return; }
            return;
        }
        if (post_ <= 10) {                       // 7 bits de fin de trama
            if (!b) { error(LEC_FORM, false); return; }
            if (post_ == 10) entregar();
            return;
        }
        // El receptor NO puede sacar al motor del estado de transmision: el
        // transmisor manda hasta su ultimo bit, y es el quien da el marco por
        // entregado.
        if (post_ >= 13 && estado_ == RX) { estado_ = IDLE; msr_ &= ~S_RXM; }
    }

    // ¿Están ya todos los bits del marco (hasta el CRC incluido)?
    bool marco_completo() {
        const size_t n = rx_bits_.size();
        // Se necesita saber IDE para conocer la longitud, y luego DLC.
        if (n < 14) return false;
        const bool ide = rx_bits_[13];
        const unsigned cab = ide ? 39u : 19u;    // hasta el final de DLC
        if (n < cab) return false;
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (rx_bits_[cab - 4 + i] ? 1u : 0u);
        if (dlc > 8) dlc = 8;
        const bool rtr = ide ? rx_bits_[32] : rx_bits_[12];
        const unsigned total = cab + (rtr ? 0u : dlc * 8u) + 15u;
        return n >= total;
    }

    // ¿Cuadra el CRC15 de lo recibido con el que viaja en el marco?
    bool crc_cuadra() const {
        const size_t n = rx_bits_.size();
        if (n < 16) return false;
        std::vector<bool> cuerpo(rx_bits_.begin(), rx_bits_.end() - 15);
        uint16_t crc_rx = 0;
        for (size_t i = n - 15; i < n; ++i)
            crc_rx = uint16_t((crc_rx << 1) | (rx_bits_[i] ? 1u : 0u));
        return can_crc15(cuerpo) == crc_rx;
    }

    void entregar() {
        if (!ack_pendiente_) { error(LEC_CRC, false); return; }

        // Se arma el marco a partir de los bits.
        CanFrame f;
        f.ide = rx_bits_[13];
        unsigned p = 1;
        uint32_t base = 0;
        for (unsigned i = 0; i < 11; ++i) base = (base << 1) | (rx_bits_[p++] ? 1u : 0u);
        if (!f.ide) {
            f.rtr = rx_bits_[12];
            f.id = base;
            p = 15;                              // tras IDE y r0
        } else {
            p = 14;                              // tras SRR e IDE
            uint32_t ext = 0;
            for (unsigned i = 0; i < 18; ++i) ext = (ext << 1) | (rx_bits_[p++] ? 1u : 0u);
            f.id = (base << 18) | ext;
            f.rtr = rx_bits_[p];
            p += 3;                              // RTR, r1, r0
        }
        unsigned dlc = 0;
        for (unsigned i = 0; i < 4; ++i) dlc = (dlc << 1) | (rx_bits_[p++] ? 1u : 0u);
        f.dlc = uint8_t(dlc > 8 ? 8 : dlc);
        if (!f.rtr)
            for (unsigned k = 0; k < f.dlc; ++k) {
                uint32_t byte = 0;
                for (unsigned i = 0; i < 8; ++i) byte = (byte << 1) | (rx_bits_[p++] ? 1u : 0u);
                f.data[k] = uint8_t(byte);
            }

        if (rec_ > 0) --rec_;
        lec_ = LEC_NONE;
        // Un nodo NO se recibe a si mismo. La excepcion es el bucle cerrado,
        // que existe justamente para que el bloque se oiga solo [IR, 12.12].
        if (soy_emisor_ && !lbkm()) { actualiza_irq(); return; }
        unsigned fifo = 0, fmi = 0;
        const bool ok = filtrar(f, fifo, fmi);
        if (ok) guardar(f, fifo, fmi);
        actualiza_irq();
    }

    // Señalización de error: seis bits dominantes seguidos, que es lo que
    // destruye el marco para todos y obliga a repetirlo.
    void error(uint32_t codigo, bool del_emisor) {
        lec_ = codigo;
        if (del_emisor) {
            tec_ += 8;
            const unsigned i = unsigned(tx_activo_ >= 0 ? tx_activo_ : 0);
            tsr_ |= 1u << (8 * i + 3);                    // TERR
            if (nart()) {
                tsr_ |= (1u << (8 * i)) | (1u << (26 + i));  // RQCP y buzón libre
                mb_[i].tir &= ~1u;
                actualiza_code();
            }
            tx_activo_ = -1;
        } else {
            rec_ += 8;
        }
        if (tec_ > 255) tec_ = 255;
        if (rec_ > 255) rec_ = 255;
        if (tec_ >= 255) entrar_bus_off();
        estado_ = ERROR_FLAG;
        err_i_ = 0; recesivos_ = 0;
        msr_ |= S_ERRI;
        actualiza_irq();
        publish();
    }

    void entrar_bus_off() {
        boff_ = true;
        // Con ABOM el bloque vuelve solo tras la secuencia de recuperación; sin
        // él hay que pedirlo a mano entrando y saliendo de inicialización.
        if (abom()) { boff_ = false; tec_ = 0; rec_ = 0; }
    }

    void reset_bloque() {
        mcr_ = M_SLEEP | M_DBF;
        msr_ = S_SLAK | 0x0C00u;
        tsr_ = 0x1C000000u;
        rf_[0] = rf_[1] = 0; ier_ = 0; btr_ = 0x01230000u;
        for (auto& m : mb_) m = TxMb{};
        fifo_[0].clear(); fifo_[1].clear();
        tec_ = rec_ = 0; lec_ = 0; boff_ = false;
        estado_ = IDLE; tx_activo_ = -1;
        actualiza_irq();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void rst_proc() {
        if (rst_n.read()) return;
        reset_bloque();
        fmr_ = 0x2A1C0E01u; fm1r_ = fs1r_ = ffa1r_ = fa1r_ = 0;
        filters_.fill(0);
        n_tx_ = n_rx_ = 0;
        o_tx_ = true; o_oe_ = false;
        for (bool& b : o_irq_) b = false;
        publish();
    }
};

// =============================================================================
// La variante fijada EN EL TIPO.
// =============================================================================
template <const CanCaps& C>
class BxCanT : public BxCanBase {
public:
    BxCanT(sc_core::sc_module_name nm, uint32_t base) : BxCanBase(nm, base, C) {}
    static constexpr const CanCaps& caps_estaticos = C;
};

using Can1      = BxCanT<CAPS_CAN1_F407>;
using Can2      = BxCanT<CAPS_CAN2_F407>;
using CanSingle = BxCanT<CAPS_CAN_SINGLE>;

} // namespace stm32
#endif // STM32_PERIPH_CAN_H
