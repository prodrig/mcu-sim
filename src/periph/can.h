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
#include "can_motor.h"

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
// El bxCAN del F303 (bxcan1_v1_1, la MISMA IP del F407 [PINDATA]): uno solo,
// dueño de sus 14 bancos [1F: RM0316 §31.7.4], con TTCM. Es `caps_can_single`
// con la comunicación disparada por tiempo, que en el silicio sí está.
constexpr CanCaps caps_can_f303() {
    CanCaps c = caps_can_single();
    c.ttcm = true;
    c.kind = "bxCAN del F303 (14 bancos, sin reparto)";
    return c;
}
inline constexpr CanCaps CAPS_CAN_F303 = caps_can_f303();

// =============================================================================
// Implementación común. Los rasgos llegan por el constructor.
// =============================================================================
class BxCanBase : public CanMotor {
public:
    sc_core::sc_out<bool> irq_tx{"irq_tx"}, irq_rx0{"irq_rx0"},
                          irq_rx1{"irq_rx1"}, irq_sce{"irq_sce"};
    // freeze (DBG_CANx_STOP) y los pines CAN_TX/CAN_RX (AF9) son del motor

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
    // Los códigos del último error (LEC_*) son los del motor [IR, §12.12]

    BxCanBase(sc_core::sc_module_name nm, uint32_t base,
              const CanCaps& caps = CAPS_CAN1_F407)
        : CanMotor(nm, base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(BxCanBase);
        SC_THREAD(bit_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(rst_proc);   sensitive << rst_n;
        filters_.fill(0);
    }

    // CAN2 -> los filtros los administra CAN1.
    void bind_filter_master(BxCanBase* m) { filter_master_ = m; }
    const CanCaps& caps() const { return caps_; }

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

    bool o_irq_[4] = {false, false, false, false};

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

    // =======================================================================
    // Los ganchos del motor (can_motor.h): el bxCAN sobre el protocolo
    // =======================================================================
    bool     cm_parado() const override { return (msr_ & S_INAK) || (msr_ & S_SLAK); }
    double   cm_tq() const override { return tq_s(); }
    unsigned cm_ts1() const override { return ts1(); }
    unsigned cm_ts2() const override { return ts2(); }
    bool     cm_bucle() const override { return lbkm(); }
    bool     cm_silencio() const override { return silm(); }
    int      cm_siguiente() const override { return siguiente_buzon(); }
    CanFrame cm_marco(unsigned i) const override { return marco_de_buzon(i); }
    void cm_tx_empieza() override { msr_ |= S_TXM; }
    void cm_rx_empieza() override { msr_ |= S_RXM; }
    void cm_rx_fin() override { msr_ &= ~S_RXM; }
    void cm_reposo_tras_error() override { msr_ &= ~(S_TXM | S_RXM); }
    void cm_arbitraje_perdido(unsigned i) override {
        tsr_ |= 1u << (8 * i + 2);              // ALST
        msr_ &= ~S_TXM;
        actualiza_irq();
    }
    void cm_tx_ok(unsigned i) override {
        tsr_ |= (1u << (8 * i)) | (1u << (8 * i + 1));   // RQCP y TXOK
        tsr_ |= 1u << (26 + i);                          // buzón libre otra vez
        mb_[i].tir &= ~1u;
        msr_ &= ~S_TXM;
        actualiza_code();
        actualiza_irq();
    }
    void cm_tx_error(unsigned i) override {
        tsr_ |= 1u << (8 * i + 3);                    // TERR
        if (nart()) {
            tsr_ |= (1u << (8 * i)) | (1u << (26 + i));  // RQCP y buzón libre
            mb_[i].tir &= ~1u;
            actualiza_code();
        }
    }
    void cm_rx_ok(const CanFrame& f) override {
        unsigned fifo = 0, fmi = 0;
        const bool ok = filtrar(f, fifo, fmi);
        if (ok) guardar(f, fifo, fmi);
        actualiza_irq();
    }
    void cm_eco() override { actualiza_irq(); }
    void cm_error() override { msr_ |= S_ERRI; actualiza_irq(); }
    void cm_bus_off() override {
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
