// =============================================================================
// sdio.h — Interfaz para tarjetas SD, SD I/O y MultiMediaCard [IR, §12.17]
//
// El STM32F407VG lleva UN SOLO bloque SDIO: no hay un segundo canal con el que
// compararlo. La pregunta del encargo —«en qué se diferencian los distintos
// canales SDIO»— tiene aquí una respuesta corta: no hay varios. Pero el bloque
// sí tiene ejes de variación reales, y son de dos clases muy distintas:
//
//   1. HACIA AFUERA, el mismo bloque habla con TRES familias de tarjeta que no
//      son el mismo protocolo: MultiMediaCard v4.2, SD Memory v2.0 y SD I/O
//      v2.0. De ahí vienen el bus de 1, 4 u 8 hilos (WIDBUS), el modo de flujo
//      continuo que solo usa la MMC (DTMODE), las funciones propias de SD I/O
//      (SDIOEN, RWSTART/RWSTOP/RWMOD, la interrupción SDIOIT) y las de CE-ATA.
//      El nombre del periférico ya lo dice: SD-I-O, no «SD».
//
//   2. HACIA ADENTRO, el bloque son DOS MÁQUINAS DE ESTADOS INDEPENDIENTES que
//      comparten el mismo reloj y los mismos pines pero corren a la vez:
//        * la CPSM (Command Path State Machine), que manda comandos de 48 bits
//          por CMD y recoge respuestas de 48 o 136;
//        * la DPSM (Data Path State Machine), que mueve bloques por D0-D7.
//      Corren SOLAPADAS, y esa es la parte que cuesta: en una lectura de
//      bloque, el firmware arranca la DPSM ANTES de mandar el comando, porque
//      la tarjeta empieza a soltar datos justo después de responder. Cada una
//      tiene sus banderas, su temporizador y sus errores. Son los dos «canales»
//      del bloque, y no se parecen en nada.
//
// Por eso el modelo está parametrizado con la receta de familia del proyecto:
//
//   * en TIEMPO DE COMPILACIÓN, con el parámetro de plantilla:
//         using Sdio      = SdioT<CAPS_SDIO_F407>;   // 8 hilos, SD I/O, CE-ATA
//         using SdioSd4   = SdioT<CAPS_SDIO_SD4>;    // solo SD, 4 hilos
//         using SdioBasic = SdioT<CAPS_SDIO_BASIC>;  // 1 hilo, sin DMA
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         SdioBase s{"s", SdioCaps{...}};
//
// Los rasgos se aplican como MÁSCARA DE ESCRITURA de cada registro, de modo que
// un bit que la instancia no implementa lee cero exactamente igual que un bit
// reservado del silicio.
//
// Todo el protocolo está modelado A NIVEL DE BIT sobre los pines: el host
// genera SDIO_CK, saca los 48 bits del comando por CMD con su CRC7, y la
// tarjeta contesta por el mismo hilo. Los datos van por D0-D7 con su bit de
// arranque, su CRC16 POR LÍNEA y su bit de parada. Nada de esto se resuelve por
// atajo: si la tarjeta devuelve un CRC malo, el modelo levanta CCRCFAIL o
// DCRCFAIL porque el cálculo no cuadra, no porque alguien lo haya decidido.
//
// Implementado en la fase F5:
//   * banco de registros POWER/CLKCR/ARG/CMD/RESPCMD/RESP1-4/DTIMER/DLEN/
//     DCTRL/DCOUNT/STA/ICR/MASK/FIFOCNT y la ventana de FIFO [IR, §12.17.2];
//   * generador de SDIO_CK con CLKDIV, BYPASS, CLKEN y ahorro de energía;
//   * CPSM completa: comandos de 48 bits con CRC7, respuestas cortas (48) y
//     largas (136), sin respuesta, y los errores CTIMEOUT y CCRCFAIL;
//   * DPSM completa: bloques de 2^DBLOCKSIZE bytes en las dos direcciones, con
//     bus de 1, 4 u 8 hilos, CRC16 por línea, DTIMEOUT, DCRCFAIL y STBITERR;
//   * FIFO de 32 palabras con sus ocho banderas, DCOUNT y FIFOCNT;
//   * interrupción 49 con su máscara y petición de DMA.
// =============================================================================
#ifndef STM32_PERIPH_SDIO_H
#define STM32_PERIPH_SDIO_H

#include <array>
#include <deque>
#include <vector>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Rasgos de la instancia. Aquí no distinguen un canal de otro —solo hay uno—
// sino QUÉ SABE HABLAR el bloque y con cuánto hilo.
// ---------------------------------------------------------------------------
struct SdioCaps {
    unsigned max_bus_width = 8;      // WIDBUS: 1, 4 u 8 hilos
    unsigned fifo_words    = 32;     // profundidad de la FIFO
    double   max_ck_hz     = 48.0e6; // frecuencia máxima de SDIO_CK
    bool     clk_bypass    = true;   // CLKCR.BYPASS (SDIO_CK = SDIOCLK)
    bool     hw_flow_ctrl  = true;   // CLKCR.HWFC_EN
    bool     sdio_card     = true;   // SD I/O: DCTRL.SDIOEN/RWxxx y STA.SDIOIT
    bool     ceata         = true;   // CE-ATA: CMD.ENCMDcompl/nIEN, STA.CEATAEND
    bool     stream_mode   = true;   // DCTRL.DTMODE (flujo continuo de la MMC)
    bool     dma           = true;   // DCTRL.DMAEN y la petición
    const char* kind       = "SDIO";
};

// --- Las variantes ---------------------------------------------------------
// El F407: el bloque completo, con los tres protocolos [IR, §12.17.1].
constexpr SdioCaps caps_sdio_f407() {
    SdioCaps c{};
    c.kind = "SDIO completo (MMC, SD, SD I/O)";
    return c;
}
// Un controlador solo de tarjeta SD de memoria: cuatro hilos, sin las
// funciones de SD I/O, sin CE-ATA y sin el flujo continuo de la MMC. Es como
// aparece el mismo bloque en varios derivados.
constexpr SdioCaps caps_sdio_sd4() {
    SdioCaps c{};
    c.max_bus_width = 4;
    c.sdio_card = false; c.ceata = false; c.stream_mode = false;
    c.kind = "SDIO solo SD, 4 hilos";
    return c;
}
// Variante reducida: un hilo, FIFO de 16 palabras, sin DMA ni bypass. No existe
// en el F407; sirve para comprobar que los ejes son independientes.
constexpr SdioCaps caps_sdio_basic() {
    SdioCaps c{};
    c.max_bus_width = 1; c.fifo_words = 16;
    c.max_ck_hz = 25.0e6;
    c.clk_bypass = false; c.hw_flow_ctrl = false;
    c.sdio_card = false; c.ceata = false; c.stream_mode = false;
    c.dma = false;
    c.kind = "SDIO basico";
    return c;
}

inline constexpr SdioCaps CAPS_SDIO_F407  = caps_sdio_f407();
inline constexpr SdioCaps CAPS_SDIO_SD4   = caps_sdio_sd4();
inline constexpr SdioCaps CAPS_SDIO_BASIC = caps_sdio_basic();

// ---------------------------------------------------------------------------
// CRC del protocolo SD. El de comandos es de 7 bits sobre los 40 primeros; el
// de datos es un CCITT de 16 bits POR CADA LÍNEA de datos, no sobre el byte.
// ---------------------------------------------------------------------------
inline uint8_t sd_crc7(const uint8_t* d, unsigned n) {
    uint8_t crc = 0;
    for (unsigned i = 0; i < n; ++i) {
        uint8_t b = d[i];
        for (unsigned k = 0; k < 8; ++k) {
            const bool bit = ((b >> 7) & 1u) != 0;
            b = uint8_t(b << 1);
            const bool hi = ((crc >> 6) & 1u) != 0;
            crc = uint8_t((crc << 1) & 0x7Fu);
            if (hi != bit) crc ^= 0x09u;          // x^7 + x^3 + 1
        }
    }
    return crc;
}
struct SdCrc16 {
    uint16_t v = 0;
    void bit(bool b) {
        const bool hi = ((v >> 15) & 1u) != 0;
        v = uint16_t(v << 1);
        if (hi != b) v ^= 0x1021u;                // x^16 + x^12 + x^5 + 1
    }
    void reset() { v = 0; }
};

// ---------------------------------------------------------------------------
// Implementación común. Los rasgos llegan por el constructor.
// ---------------------------------------------------------------------------
class SdioBase : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};                        // IRQ 49
    sc_core::sc_out<bool> dma_req{"dma_req"};                // DMA2 S3C4/S6C4
    sc_core::sc_in<bool>  sdioclk{"sdioclk"};                // PLL48CK
    sc_core::sc_in<double> sdioclk_hz{"sdioclk_hz"};
    // AF12: CK (salida), CMD (bidireccional) y D0-D7 (bidireccionales)
    sc_core::sc_signal<bool> ck_out{"ck_out"}, ck_oe{"ck_oe"}, ck_in{"ck_in"};
    sc_core::sc_signal<bool> cmd_out{"cmd_out"}, cmd_oe{"cmd_oe"}, cmd_in{"cmd_in"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> d_out, d_oe, d_in;   // [8]

    // ---- Offsets [IR, §12.17.2] -------------------------------------------
    enum : uint32_t {
        R_POWER = 0x00, R_CLKCR = 0x04, R_ARG = 0x08, R_CMD = 0x0C,
        R_RESPCMD = 0x10, R_RESP1 = 0x14, R_RESP2 = 0x18, R_RESP3 = 0x1C,
        R_RESP4 = 0x20, R_DTIMER = 0x24, R_DLEN = 0x28, R_DCTRL = 0x2C,
        R_DCOUNT = 0x30, R_STA = 0x34, R_ICR = 0x38, R_MASK = 0x3C,
        R_FIFOCNT = 0x48, R_FIFO = 0x80
    };
    enum StaBit : uint32_t {
        S_CCRCFAIL = 1u << 0,  S_DCRCFAIL = 1u << 1,  S_CTIMEOUT = 1u << 2,
        S_DTIMEOUT = 1u << 3,  S_TXUNDERR = 1u << 4,  S_RXOVERR  = 1u << 5,
        S_CMDREND  = 1u << 6,  S_CMDSENT  = 1u << 7,  S_DATAEND  = 1u << 8,
        S_STBITERR = 1u << 9,  S_DBCKEND  = 1u << 10, S_CMDACT   = 1u << 11,
        S_TXACT    = 1u << 12, S_RXACT    = 1u << 13, S_TXFIFOHE = 1u << 14,
        S_RXFIFOHF = 1u << 15, S_TXFIFOF  = 1u << 16, S_RXFIFOF  = 1u << 17,
        S_TXFIFOE  = 1u << 18, S_RXFIFOE  = 1u << 19, S_TXDAVL   = 1u << 20,
        S_RXDAVL   = 1u << 21, S_SDIOIT   = 1u << 22, S_CEATAEND = 1u << 23
    };

    SdioBase(sc_core::sc_module_name nm, const SdioCaps& caps = CAPS_SDIO_F407)
        : BusSlave(nm, addr::SDIO_B, 0x400),
          d_out("d_out", 8), d_oe("d_oe", 8), d_in("d_in", 8), caps_(caps) {
        SC_HAS_PROCESS(SdioBase);
        SC_THREAD(ck_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(clk_proc);   sensitive << sdioclk_hz; dont_initialize();
    }

    // ---- Observación desde el banco de pruebas ----------------------------
    const SdioCaps& caps() const { return caps_; }
    double   ck_hz() const;
    unsigned bus_width() const {
        static const unsigned w[4] = {1, 4, 8, 1};
        const unsigned s = w[(clkcr_ >> 11) & 3u];
        return (s > caps_.max_bus_width) ? 1u : s;
    }
    uint64_t commands() const { return n_cmd_; }
    uint64_t blocks()   const { return n_blk_; }
    uint32_t sta_raw()  const { return sta_; }

protected:
    SdioCaps caps_;
    // «Avisar una vez» de que SDIO_CK se pasa del máximo, POR INSTANCIA y no en
    // un `static` local: con dos MCUs, una bandera compartida hace que el aviso
    // del segundo se lo trague el primero. `mutable` porque quien lo mira es
    // `ck_hz() const`. [doc/stm32f4xx/stm32f407vg_multi_mcu.md, §7.3]
    mutable bool aviso_ck_ = false;

    // ---- Registros ----
    uint32_t power_ = 0, clkcr_ = 0, arg_ = 0, cmd_ = 0;
    uint32_t respcmd_ = 0, dtimer_ = 0, dlen_ = 0, dctrl_ = 0;
    uint32_t sta_ = 0, mask_ = 0, dcount_ = 0;
    std::array<uint32_t, 4> resp_{};
    std::deque<uint32_t> fifo_;

    // ---- Máquina de comandos (CPSM) ----
    enum CpsmSt { C_IDLE, C_SEND, C_WAIT, C_RECV };
    CpsmSt   cst_ = C_IDLE;
    uint8_t  ctx_[6] = {};            // los 48 bits que salen por CMD
    unsigned cbit_ = 0, cwait_ = 0, crx_len_ = 0;
    // El firmware escribe SDIO_CMD en cualquier momento del ciclo de SDIO_CK.
    // Sin este cerrojo, si la escritura cae entre el flanco de bajada y el de
    // subida, la maquina contaria un bit que todavia no ha puesto en el hilo y
    // se perderia el bit de arranque de la trama.
    bool     c_armed_ = false;
    std::vector<bool> crx_;

    // ---- Máquina de datos (DPSM) ----
    enum DpsmSt { D_IDLE, D_WAIT, D_RECV, D_SEND };
    DpsmSt   dst_ = D_IDLE;
    unsigned dbit_ = 0, dwait_ = 0, dblk_left_ = 0, dgap_ = 0;
    bool     d_armed_ = false;
    std::vector<uint8_t> dbuf_;       // bloque en curso
    std::array<SdCrc16, 8> dcrc_{};
    std::array<uint16_t, 8> dcrc_rx_{};

    uint64_t n_cmd_ = 0, n_blk_ = 0;
    bool o_irq_ = false, o_drq_ = false;
    // Salidas hacia los pines. SystemC solo admite UN escritor por senal, asi
    // que se guardan como miembros y las publica un unico proceso, igual que en
    // el resto de los perifericos del proyecto.
    bool o_ck_ = false, o_ck_oe_ = false;
    bool o_cmd_ = true, o_cmd_oe_ = false;
    std::array<bool, 8> o_d_{}, o_d_oe_{};
    sc_core::sc_event ev_, pub_ev_;

    // =======================================================================
    // Campos de los registros
    // =======================================================================
    bool     powered()  const { return (power_ & 3u) == 3u; }
    unsigned clkdiv()   const { return clkcr_ & 0xFFu; }
    bool     clken()    const { return (clkcr_ >> 8) & 1u; }
    bool     bypass()   const { return caps_.clk_bypass && ((clkcr_ >> 10) & 1u); }
    unsigned cmdindex() const { return cmd_ & 0x3Fu; }
    unsigned waitresp() const { return (cmd_ >> 6) & 3u; }
    bool     cpsmen()   const { return (cmd_ >> 10) & 1u; }
    bool     dten()     const { return dctrl_ & 1u; }
    bool     dtdir()    const { return (dctrl_ >> 1) & 1u; }   // 1: tarjeta -> host
    bool     dtmode()   const { return caps_.stream_mode && ((dctrl_ >> 2) & 1u); }
    bool     dmaen()    const { return caps_.dma && ((dctrl_ >> 3) & 1u); }
    unsigned dblocksize() const { return 1u << ((dctrl_ >> 4) & 0xFu); }

    // =======================================================================
    // Máscaras de escritura: aquí ACTÚAN los rasgos.
    // =======================================================================
    uint32_t clkcr_mask() const {
        uint32_t m = 0xFFu | (1u << 8) | (1u << 9) | (1u << 13);  // CLKDIV,
                                              // CLKEN, PWRSAV, NEGEDGE
        if (caps_.clk_bypass)   m |= 1u << 10;
        if (caps_.hw_flow_ctrl) m |= 1u << 14;
        // WIDBUS: los valores que la instancia no admite no se guardan
        if (caps_.max_bus_width >= 4) m |= 1u << 11;
        if (caps_.max_bus_width >= 8) m |= 1u << 12;
        return m;
    }
    uint32_t cmd_mask() const {
        uint32_t m = 0x3Fu | (3u << 6) | (1u << 8) | (1u << 9) | (1u << 10) |
                     (1u << 11);                          // hasta SDIOSuspend
        if (!caps_.sdio_card) m &= ~(1u << 11);
        if (caps_.ceata) m |= (1u << 12) | (1u << 13) | (1u << 14);
        return m;
    }
    uint32_t dctrl_mask() const {
        uint32_t m = 0x3u | (0xFu << 4);                  // DTEN, DTDIR, BLOCK
        if (caps_.stream_mode) m |= 1u << 2;
        if (caps_.dma)         m |= 1u << 3;
        if (caps_.sdio_card)   m |= (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);
        return m;
    }
    uint32_t sta_mask() const {
        uint32_t m = 0x007FFFFFu & ~((1u << 22) | (1u << 23));
        if (caps_.sdio_card) m |= 1u << 22;               // SDIOIT
        if (caps_.ceata)     m |= 1u << 23;               // CEATAEND
        return m;
    }
    // ICR borra las banderas "estáticas": los errores y los fines de suceso.
    uint32_t icr_mask() const {
        uint32_t m = 0x7FFu;                              // bits 0..10
        if (caps_.sdio_card) m |= 1u << 22;
        if (caps_.ceata)     m |= 1u << 23;
        return m;
    }

    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        if (off >= R_FIFO && off < R_FIFO + 0x80u) return read_fifo();
        switch (off) {
            case R_POWER:  return power_;
            case R_CLKCR:  return clkcr_;
            case R_ARG:    return arg_;
            case R_CMD:    return cmd_;
            case R_RESPCMD: return respcmd_;
            case R_RESP1:  return resp_[0];
            case R_RESP2:  return resp_[1];
            case R_RESP3:  return resp_[2];
            case R_RESP4:  return resp_[3];
            case R_DTIMER: return dtimer_;
            case R_DLEN:   return dlen_;
            case R_DCTRL:  return dctrl_;
            case R_DCOUNT: return dcount_;
            // Con el bloque apagado (PWRCTRL != 11) el adaptador no tiene reloj:
            // ni la FIFO ni las maquinas de estado dan senal, y SDIO_STA se lee
            // como cero, que es su valor de reset [IR, §12.17.2].
            case R_STA:    return powered() ? (sta_ & sta_mask()) : 0u;
            case R_ICR:    return 0;                      // solo escritura
            case R_MASK:   return mask_;
            case R_FIFOCNT: return uint32_t(fifo_.size());
            default:       return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = (off >= R_FIFO) ? 0u : reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        if (off >= R_FIFO && off < R_FIFO + 0x80u) { write_fifo(v); return; }
        switch (off) {
            case R_POWER: {
                const bool was = powered();
                power_ = v & 3u;
                if (!was && powered()) { sta_ = 0; fifo_.clear(); update_fifo_flags(); }
                if (was && !powered()) shut_down();
                kick();
                return;
            }
            case R_CLKCR: clkcr_ = v & clkcr_mask(); kick(); return;
            case R_ARG:   arg_ = v; return;
            case R_CMD: {
                cmd_ = v & cmd_mask();
                // Escribir CMD con CPSMEN es lo que LANZA el comando. La CPSM
                // arranca aquí y sigue sola sobre los pines [IR, §12.17.2].
                if (cpsmen() && powered()) start_command();
                kick();
                return;
            }
            case R_DTIMER: dtimer_ = v; return;
            case R_DLEN:   dlen_ = v & 0x01FFFFFFu; return;
            case R_DCTRL: {
                const bool was = dten();
                dctrl_ = v & dctrl_mask();
                if (!was && dten() && powered()) start_data();
                if (was && !dten()) stop_data();
                kick();
                return;
            }
            case R_ICR:
                // Las banderas estáticas se borran escribiendo UNO aquí; las
                // dinámicas (CMDACT, TXACT, las de FIFO) no se tocan: las lleva
                // el hardware [IR, §12.17.2].
                sta_ &= ~(v & icr_mask());
                update_irq();
                return;
            case R_MASK: mask_ = v; update_irq(); return;
            default: return;                              // RESPx y DCOUNT son r
        }
    }
    unsigned access_cycles(bool) const override { return 2; }

    // =======================================================================
    // FIFO
    // =======================================================================
    uint32_t read_fifo() {
        if (fifo_.empty()) return 0;
        const uint32_t v = fifo_.front();
        fifo_.pop_front();
        update_fifo_flags();
        return v;
    }
    void write_fifo(uint32_t v) {
        if (fifo_.size() >= caps_.fifo_words) { sta_ |= S_TXUNDERR; update_irq(); return; }
        fifo_.push_back(v);
        update_fifo_flags();
    }
    void update_fifo_flags() {
        const size_t n = fifo_.size(), cap = caps_.fifo_words;
        uint32_t s = sta_ & ~(S_TXFIFOE | S_TXFIFOF | S_TXFIFOHE |
                              S_RXFIFOE | S_RXFIFOF | S_RXFIFOHF |
                              S_TXDAVL | S_RXDAVL);
        if (n == 0)          s |= S_TXFIFOE | S_RXFIFOE;
        if (n >= cap)        s |= S_TXFIFOF | S_RXFIFOF;
        if (n <= cap / 2)    s |= S_TXFIFOHE;
        if (n >= cap / 2)    s |= S_RXFIFOHF;
        if (n < cap)         s |= S_TXDAVL;
        if (n > 0)           s |= S_RXDAVL;
        sta_ = s;
        // La petición de DMA es de nivel: hay sitio para escribir (transmisión)
        // o hay dato que leer (recepción).
        const bool drq = dmaen() && (dst_ != D_IDLE) &&
                         (dtdir() ? (n > 0) : (n < cap));
        if (drq != o_drq_) { o_drq_ = drq; publish(); }
        update_irq();
    }

    // =======================================================================
    // Reloj SDIO_CK
    // =======================================================================
    double half_period() const {
        const double f = ck_hz();
        return (f > 0.0) ? 0.5 / f : 0.0;
    }
    void kick() { ev_.notify(sc_core::SC_ZERO_TIME); }
    void clk_proc() { kick(); }

    // =======================================================================
    // CPSM — máquina de comandos
    // =======================================================================
    void start_command() {
        // Trama de 48 bits: 0, 1, índice(6), argumento(32), CRC7, 1
        ctx_[0] = uint8_t(0x40u | (cmdindex() & 0x3Fu));
        ctx_[1] = uint8_t(arg_ >> 24); ctx_[2] = uint8_t(arg_ >> 16);
        ctx_[3] = uint8_t(arg_ >> 8);  ctx_[4] = uint8_t(arg_);
        ctx_[5] = uint8_t((sd_crc7(ctx_, 5) << 1) | 1u);
        cbit_ = 0;
        c_armed_ = false;
        cst_ = C_SEND;
        sta_ = (sta_ & ~(S_CMDSENT | S_CMDREND | S_CTIMEOUT | S_CCRCFAIL)) | S_CMDACT;
        ++n_cmd_;
        update_irq();
    }
    void cpsm_falling() {
        if (cst_ == C_SEND) {
            const unsigned by = cbit_ / 8, bi = 7u - (cbit_ % 8);
            o_cmd_ = ((ctx_[by] >> bi) & 1u) != 0;
            o_cmd_oe_ = true;
            c_armed_ = true;
            publish();
        } else if (cst_ != C_IDLE) {
            o_cmd_oe_ = false;                   // el hilo es de la tarjeta
            publish();
        }
    }
    void cpsm_rising() {
        switch (cst_) {
            case C_SEND:
                if (!c_armed_) return;        // aun no se ha puesto este bit
                if (++cbit_ < 48) return;
                o_cmd_oe_ = false; publish();
                if (waitresp() == 0u || waitresp() == 2u) {
                    sta_ = (sta_ & ~S_CMDACT) | S_CMDSENT;
                    cst_ = C_IDLE;
                    update_irq();
                } else {
                    // El manual fija un plazo de 64 ciclos de SDIO_CK para que
                    // la tarjeta empiece a contestar [IR, §12.17.2].
                    cst_ = C_WAIT; cwait_ = 64;
                    crx_.clear();
                    crx_len_ = (waitresp() == 3u) ? 136u : 48u;
                }
                return;
            case C_WAIT:
                if (!cmd_in.read()) { cst_ = C_RECV; crx_.push_back(false); return; }
                if (--cwait_ == 0) {
                    sta_ = (sta_ & ~S_CMDACT) | S_CTIMEOUT;
                    cst_ = C_IDLE;
                    update_irq();
                }
                return;
            case C_RECV:
                crx_.push_back(cmd_in.read());
                if (crx_.size() >= crx_len_) finish_response();
                return;
            default: return;
        }
    }
    void finish_response() {
        // Se rearma la trama recibida en bytes para comprobar el CRC7 igual que
        // lo haría el silicio: sobre los bits que llegaron, no sobre lo que
        // «deberia» haber llegado.
        const unsigned nb = crx_len_ / 8;
        std::vector<uint8_t> b(nb, 0);
        for (unsigned i = 0; i < crx_len_; ++i)
            if (crx_[i]) b[i / 8] = uint8_t(b[i / 8] | (0x80u >> (i % 8)));
        bool crc_ok;
        if (crx_len_ == 48) {
            respcmd_ = b[0] & 0x3Fu;
            resp_[0] = (uint32_t(b[1]) << 24) | (uint32_t(b[2]) << 16) |
                       (uint32_t(b[3]) << 8)  |  uint32_t(b[4]);
            resp_[1] = resp_[2] = resp_[3] = 0;
            const uint8_t crc = sd_crc7(b.data(), 5);
            // R3 (respuesta de OCR) viaja SIN CRC válido: el silicio lo sabe y
            // no marca error, porque la tarjeta manda unos [IR, §12.17].
            const bool no_crc = ((b[0] & 0x3Fu) == 0x3Fu) && ((b[5] >> 1) == 0x7Fu);
            crc_ok = no_crc || (((b[5] >> 1) & 0x7Fu) == crc);
        } else {
            respcmd_ = 0x3Fu;
            for (unsigned r = 0; r < 4; ++r)
                resp_[r] = (uint32_t(b[1 + 4 * r]) << 24) |
                           (uint32_t(b[2 + 4 * r]) << 16) |
                           (uint32_t(b[3 + 4 * r]) << 8)  |
                            uint32_t(b[4 + 4 * r]);
            crc_ok = true;                       // el CRC largo lo cubre la CID
        }
        sta_ &= ~S_CMDACT;
        sta_ |= crc_ok ? S_CMDREND : S_CCRCFAIL;
        cst_ = C_IDLE;
        update_irq();
    }

    // =======================================================================
    // DPSM — máquina de datos
    // =======================================================================
    void start_data() {
        dcount_ = dlen_;
        dbuf_.clear();
        dbit_ = 0; dgap_ = 2;
        for (auto& c : dcrc_) c.reset();
        dblk_left_ = dblocksize();
        // El temporizador de datos cuenta ciclos de SDIO_CK esperando a la
        // tarjeta [IR, §12.17.2].
        dwait_ = dtimer_ ? dtimer_ : 0xFFFFFFu;
        d_armed_ = false;
        if (dtdir()) { dst_ = D_WAIT; sta_ |= S_RXACT; }
        else         { dst_ = D_SEND; sta_ |= S_TXACT; }
        update_fifo_flags();
    }
    // Al terminar una transferencia el hardware BAJA DTEN. Sin eso, el
    // firmware que escribe DCTRL para la transferencia siguiente no produce el
    // flanco de arranque y la maquina de datos se queda parada [IR, §12.17.2].
    void dpsm_done() { dst_ = D_IDLE; dctrl_ &= ~1u; }
    void stop_data() {
        dst_ = D_IDLE;
        sta_ &= ~(S_RXACT | S_TXACT);
        o_d_oe_.fill(false); publish();
        if (o_drq_) { o_drq_ = false; publish(); }
        update_irq();
    }
    unsigned nlines() const { return bus_width(); }
    // Se asegura de que el byte pedido ya está sacado de la FIFO. Si la FIFO se
    // ha quedado seca antes de tiempo, eso es un DESBORDAMIENTO POR DEFECTO:
    // el bloque ya está saliendo por los hilos y no se puede parar [IR, §12.17].
    unsigned dload_ = 0;
    void ensure_loaded(unsigned byte_idx) {
        while (dload_ <= byte_idx && dload_ < dbuf_.size()) {
            uint32_t w = 0;
            if (!fifo_.empty()) { w = fifo_.front(); fifo_.pop_front(); }
            else { sta_ |= S_TXUNDERR; }
            for (unsigned k = 0; k < 4 && dload_ < dbuf_.size(); ++k)
                dbuf_[dload_++] = uint8_t(w >> (8 * k));
            update_fifo_flags();
        }
    }

    void dpsm_falling() {
        if (dst_ != D_SEND) { o_d_oe_.fill(false); publish(); return; }
        const unsigned nl = nlines();
        // Los dos ciclos de guarda antes del bloque se cuentan en el flanco de
        // SUBIDA: si se descontaran aqui, el flanco de subida de este mismo
        // ciclo daria por puesto un bit que todavia no ha salido.
        if (dgap_ > 0) { o_d_oe_.fill(false); publish(); return; }
        d_armed_ = true;
        for (unsigned i = 0; i < nl; ++i) o_d_oe_[i] = true;
        const unsigned nbits = dblocksize() * 8u / nl;   // ciclos de datos
        if (dbit_ == 0) {                                 // bit de arranque
            for (unsigned i = 0; i < nl; ++i) o_d_[i] = false;
            for (auto& c : dcrc_) c.reset();
            // La FIFO se vacía SEGUN HACE FALTA, no de golpe: un bloque de
            // 512 bytes no cabe en 32 palabras, y el software va rellenando.
            dbuf_.assign(dblocksize(), 0);
            dload_ = 0;
            ensure_loaded(0);
        } else if (dbit_ <= nbits) {
            const unsigned c = dbit_ - 1u;
            ensure_loaded((c * nl) / 8u);
            for (unsigned l = 0; l < nl; ++l) {
                const bool b = data_bit(dbuf_, c, l, nl);
                o_d_[l] = b;
                dcrc_[l].bit(b);
            }
        } else if (dbit_ <= nbits + 16u) {               // CRC16 por línea
            const unsigned k = dbit_ - nbits - 1u;
            for (unsigned l = 0; l < nl; ++l)
                o_d_[l] = ((dcrc_[l].v >> (15u - k)) & 1u) != 0;
        } else {                                          // bit de parada
            for (unsigned l = 0; l < nl; ++l) o_d_[l] = true;
        }
    }
    void dpsm_rising() {
        const unsigned nl = nlines();
        const unsigned nbits = dblocksize() * 8u / nl;
        switch (dst_) {
            case D_WAIT:
                // Se espera el bit de arranque de la tarjeta en D0.
                if (!d_in[0].read()) {
                    dst_ = D_RECV; dbit_ = 0;
                    dbuf_.assign(dblocksize(), 0);
                    for (auto& c : dcrc_) c.reset();
                    return;
                }
                if (dwait_ == 0 || --dwait_ == 0) {
                    sta_ = (sta_ & ~S_RXACT) | S_DTIMEOUT;
                    dpsm_done();
                    update_irq();
                }
                return;
            case D_RECV:
                if (dbit_ < nbits) {
                    for (unsigned l = 0; l < nl; ++l) {
                        const bool b = d_in[l].read();
                        set_data_bit(dbuf_, dbit_, l, nl, b);
                        dcrc_[l].bit(b);
                    }
                    ++dbit_;
                    // La FIFO se llena PALABRA A PALABRA, según van entrando
                    // los bits, no de golpe al final del bloque: si no, un
                    // bloque de 512 bytes desbordaría siempre una FIFO de 32
                    // palabras y el software no tendría ocasión de vaciarla.
                    if ((dbit_ * nl) % 32u == 0u) {
                        const unsigned by = (dbit_ * nl) / 8u - 4u;
                        uint32_t w = 0;
                        for (unsigned k = 0; k < 4; ++k)
                            w |= uint32_t(dbuf_[by + k]) << (8 * k);
                        if (fifo_.size() < caps_.fifo_words) fifo_.push_back(w);
                        else sta_ |= S_RXOVERR;
                        update_fifo_flags();
                    }
                    return;
                }
                if (dbit_ < nbits + 16u) {
                    const unsigned k = dbit_ - nbits;
                    for (unsigned l = 0; l < nl; ++l)
                        if (d_in[l].read()) dcrc_rx_[l] = uint16_t(dcrc_rx_[l] | (0x8000u >> k));
                    if (++dbit_ == nbits + 16u) finish_rx_block();
                    return;
                }
                return;
            case D_SEND:
                if (dgap_ > 0) { --dgap_; return; }
                if (!d_armed_) return;
                if (dbit_ <= nbits + 16u) { ++dbit_; return; }
                finish_tx_block();
                return;
            default: return;
        }
    }
    void finish_rx_block() {
        bool crc_ok = true;
        for (unsigned l = 0; l < nlines(); ++l) {
            if (dcrc_rx_[l] != dcrc_[l].v) crc_ok = false;
            dcrc_rx_[l] = 0;
        }
        ++n_blk_;
        if (!crc_ok) sta_ |= S_DCRCFAIL;
        sta_ |= S_DBCKEND;
        dcount_ = (dcount_ > dblocksize()) ? (dcount_ - dblocksize()) : 0u;
        if (dcount_ == 0 || !crc_ok) {
            sta_ &= ~S_RXACT;
            if (crc_ok) sta_ |= S_DATAEND;
            dpsm_done();
        } else {
            dst_ = D_WAIT;
            dwait_ = dtimer_ ? dtimer_ : 0xFFFFFFu;
        }
        update_fifo_flags();
    }
    void finish_tx_block() {
        ++n_blk_;
        sta_ |= S_DBCKEND;
        dcount_ = (dcount_ > dblocksize()) ? (dcount_ - dblocksize()) : 0u;
        if (dcount_ == 0) {
            sta_ = (sta_ & ~S_TXACT) | S_DATAEND;
            dpsm_done();
            o_d_oe_.fill(false);
        } else {
            dbit_ = 0; dgap_ = 2;
        }
        update_fifo_flags();
    }

    // Colocación de un bit de datos según el ancho del bus. En cuatro hilos
    // cada byte viaja en dos mordiscos, el alto primero, y D3 lleva el bit más
    // significativo de cada uno [IR, §12.17.1].
    static bool data_bit(const std::vector<uint8_t>& b, unsigned c,
                         unsigned line, unsigned nl) {
        const unsigned bitpos = c * nl + (nl - 1u - line);
        const unsigned by = bitpos / 8, bi = 7u - (bitpos % 8);
        return by < b.size() && ((b[by] >> bi) & 1u) != 0;
    }
    static void set_data_bit(std::vector<uint8_t>& b, unsigned c,
                             unsigned line, unsigned nl, bool v) {
        const unsigned bitpos = c * nl + (nl - 1u - line);
        const unsigned by = bitpos / 8, bi = 7u - (bitpos % 8);
        if (by < b.size() && v) b[by] = uint8_t(b[by] | (1u << bi));
    }

    // =======================================================================
    // El hilo del reloj: es quien mueve las dos máquinas
    // =======================================================================
    void ck_proc() {
        for (;;) {
            const double h = half_period();
            if (!powered() || !clken() || h <= 0.0) {
                o_ck_ = false; o_ck_oe_ = powered();
                o_d_oe_.fill(false); o_cmd_oe_ = false;
                publish();
                wait(ev_ | rst_n.value_changed_event());
                continue;
            }
            o_ck_oe_ = true;
            // FLANCO DE BAJADA: todo el mundo pone su bit.
            o_ck_ = false;
            cpsm_falling();
            dpsm_falling();
            publish();
            wait(sc_core::sc_time(h, sc_core::SC_SEC));
            // FLANCO DE SUBIDA: todo el mundo muestrea.
            o_ck_ = true; publish();
            wait(sc_core::sc_time(h / 4.0, sc_core::SC_SEC));  // deja llegar el nivel
            cpsm_rising();
            dpsm_rising();
            wait(sc_core::sc_time(h * 3.0 / 4.0, sc_core::SC_SEC));
        }
    }

    void shut_down() {
        cst_ = C_IDLE; dst_ = D_IDLE;
        sta_ = 0; fifo_.clear();
        o_cmd_oe_ = false; o_d_oe_.fill(false);
        if (o_drq_) { o_drq_ = false; }
        o_irq_ = false;
        publish();
    }
    void update_irq() {
        const bool i = (sta_ & mask_ & sta_mask()) != 0;
        if (i != o_irq_) { o_irq_ = i; publish(); }
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        irq.write(o_irq_); dma_req.write(o_drq_);
        ck_out.write(o_ck_);   ck_oe.write(o_ck_oe_);
        cmd_out.write(o_cmd_); cmd_oe.write(o_cmd_oe_);
        for (unsigned i = 0; i < 8; ++i) {
            d_out[i].write(o_d_[i]); d_oe[i].write(o_d_oe_[i]);
        }
    }
    void reset_proc() {
        if (rst_n.read()) return;
        power_ = clkcr_ = arg_ = cmd_ = 0;
        respcmd_ = dtimer_ = dlen_ = dctrl_ = 0;
        sta_ = mask_ = dcount_ = 0;
        resp_.fill(0);
        fifo_.clear();
        n_cmd_ = n_blk_ = 0;
        shut_down();
        update_fifo_flags();
        kick();
    }
};

// SDIO_CK = SDIOCLK / (CLKDIV + 2), o el propio SDIOCLK en modo BYPASS
// [IR, §12.17.2]. Si el firmware se pasa del máximo, el modelo avisa pero
// sigue: es lo que hace el silicio, con la integridad de señal degradada.
inline double SdioBase::ck_hz() const {
    const double f = sdioclk_hz.read();
    if (f <= 0.0) return 0.0;
    const double r = bypass() ? f : f / double(clkdiv() + 2u);
    if (r > caps_.max_ck_hz && !aviso_ck_) {
        aviso_ck_ = true;
        SC_REPORT_WARNING("sdio", "SDIO_CK por encima del maximo [IR, 12.17]");
    }
    return r;
}

// ---------------------------------------------------------------------------
// Selección en TIEMPO DE COMPILACIÓN.
// ---------------------------------------------------------------------------
template <const SdioCaps& C>
class SdioT : public SdioBase {
public:
    explicit SdioT(sc_core::sc_module_name nm) : SdioBase(nm, C) {
        static_assert(C.max_bus_width == 1 || C.max_bus_width == 4 ||
                      C.max_bus_width == 8, "el bus SD es de 1, 4 u 8 hilos");
        static_assert(C.fifo_words >= 4 && C.fifo_words <= 32,
                      "la FIFO del bloque llega a 32 palabras");
    }
    static constexpr unsigned bus_width_max() { return C.max_bus_width; }
};

// El STM32F407VG: el bloque completo [IR, §12.17.1].
using Sdio      = SdioT<CAPS_SDIO_F407>;
using SdioSd4   = SdioT<CAPS_SDIO_SD4>;
using SdioBasic = SdioT<CAPS_SDIO_BASIC>;

} // namespace stm32
#endif // STM32_PERIPH_SDIO_H
