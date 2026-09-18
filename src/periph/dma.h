// =============================================================================
// dma.h — Controladores DMA1/DMA2 [IR, §11]
//
// Esclavo AHB1 (banco de registros) + dos maestros independientes: puerto de
// memoria y puerto de periféricos. En el silicio los dos puertos pueden operar
// a la vez; en un modelo loosely-timed lo observable es el reparto de ancho de
// banda, que se refleja anotando la latencia de cada acceso.
//
//   * DMA1: el puerto de memoria entra en la matriz (maestro DMA1-M) y el de
//     periféricos ataca APB1 directamente, sin pasar por la matriz, que es la
//     topología del silicio [IR, §11.1.1]. No admite memoria-a-memoria.
//   * DMA2: los dos puertos son maestros de la matriz (DMA2-M y DMA2-P) y
//     admite memoria-a-memoria y acceso a la Flash [IR, §11.1.1].
//
// Las peticiones de hardware entran como 64 líneas (8 streams x 8 canales,
// tablas [IR, §11.4]); el mux CHSEL selecciona una por stream.
//
// Fase F4 — implementado:
//   * banco de registros completo: LISR/HISR/LIFCR/HIFCR y, por stream,
//     SxCR/SxNDTR/SxPAR/SxM0AR/SxM1AR/SxFCR [IR, §11.5, §11.6];
//   * árbitro por prioridad de software (PL) con desempate por número de
//     stream [IR, §11.3.2];
//   * FIFO de 4 palabras por stream con umbrales FTH, modo directo y modo
//     FIFO con empaquetado/desempaquetado entre anchos distintos [IR, §11.3.3];
//   * ráfagas INCR4/8/16 en cada puerto (MBURST/PBURST);
//   * modos circular y doble buffer con conmutación de CT;
//   * banderas TCIF/HTIF/TEIF/DMEIF/FEIF e interrupción por stream;
//   * errores: transferencia (respuesta de error del bus), modo directo
//     (ráfaga configurada en modo directo) y FIFO (umbral incompatible con la
//     ráfaga, o modo directo pedido en memoria-a-memoria) [IR, §11.8].
// =============================================================================
#ifndef STM32_PERIPH_DMA_H
#define STM32_PERIPH_DMA_H

#include "../common/periph_base.h"
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>

namespace stm32 {

class DmaCtrl : public BusSlave {
public:
    static constexpr unsigned N_STREAMS = 8, N_CH = 8;
    static constexpr unsigned FIFO_BYTES = 16;         // 4 palabras de 32 bits

    tlm_utils::simple_initiator_socket<DmaCtrl> mem_m{"mem_m"};      // maestro mem
    tlm_utils::simple_initiator_socket<DmaCtrl> periph_m{"periph_m"};// maestro periph
    // req_in[s*8+c]: petición del periférico mapeado en (stream s, canal c)
    sc_core::sc_vector<sc_core::sc_in<bool>>  req_in;
    sc_core::sc_vector<sc_core::sc_out<bool>> ack_out;               // [64]
    sc_core::sc_vector<sc_core::sc_out<bool>> irq_stream;            // [8]

    // ---- QUE CELDAS DE LA TABLA TIENEN FUENTE EN ESTE MODELO --------------
    //
    // Una máscara de 64 bits, una por (stream, canal), que el top rellena con
    // las celdas que de verdad ha cableado. Por omisión, todas: un `DmaCtrl`
    // suelto no tiene por qué saber nada de esto.
    //
    // Existe porque hay dos motivos MUY distintos para que `req_in[i]` esté
    // permanentemente a cero, y desde fuera se ven igual:
    //
    //   * la celda está RESERVADA en el silicio — no hay periférico ahí, y el
    //     firmware que la elige se ha equivocado;
    //   * la celda existe en el chip y este modelo todavía no la cablea — es
    //     el caso de los bloques que llegaron en la fase 4, cuyo mapa de
    //     canales es el del F407.
    //
    // En los dos casos el stream se arma y no se mueve nunca, y sin esto el
    // fallo es SILENCIOSO: ni una bandera, ni un aviso, ni una transferencia.
    // Con esto, armar un stream sobre una celda sin fuente avisa una vez y
    // dice cuál es. [vs_446re, §20.3]
    uint64_t celdas_con_fuente = ~uint64_t(0);

    // ---- Offsets [IR, §11.5, §11.6] ---------------------------------------
    enum : uint32_t { LISR = 0x00, HISR = 0x04, LIFCR = 0x08, HIFCR = 0x0C,
                      S0_BASE = 0x10, S_STRIDE = 0x18 };
    enum : uint32_t { SxCR = 0x00, SxNDTR = 0x04, SxPAR = 0x08,
                      SxM0AR = 0x0C, SxM1AR = 0x10, SxFCR = 0x14 };
    // Posición de cada bandera dentro del grupo de 6 bits de su stream
    enum FlagBit : unsigned { F_FE = 0, F_DME = 2, F_TE = 3, F_HT = 4, F_TC = 5 };

    DmaCtrl(sc_core::sc_module_name nm, uint32_t base, bool is_dma2)
        : BusSlave(nm, base, 0x400),
          req_in("req_in", N_STREAMS * N_CH), ack_out("ack_out", N_STREAMS * N_CH),
          irq_stream("irq_stream", N_STREAMS), is_dma2_(is_dma2) {
        SC_HAS_PROCESS(DmaCtrl);
        reset_state();
        SC_THREAD(engine_proc);
        SC_METHOD(req_watch_proc);
        for (unsigned i = 0; i < N_STREAMS * N_CH; ++i) sensitive << req_in[i];
        dont_initialize();
        // Un único proceso escribe los puertos de salida: el estado lo cambian
        // tanto el motor como el b_transport del banco de registros.
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }

    // ---- Instrumentación de verificación -----------------------------------
    // Fuerza la petición del canal seleccionado por un stream. Es lo que hará
    // un periférico real cuando esté implementado (F4-periféricos y F5); hasta
    // entonces permite ejercitar los caminos P->M y M->P desde el banco de
    // pruebas. No corresponde a ningún registro del silicio.
    void tb_set_request(unsigned stream, bool on) {
        if (stream >= N_STREAMS) return;
        sw_req_[stream] = on;
        kick();
    }
    // Estado observable para el banco de pruebas
    uint32_t ndtr_of(unsigned s) const { return ndt_items(st_[s]); }
    unsigned fifo_level(unsigned s) const { return st_[s].flev; }
    uint64_t beats_read()  const { return n_rd_; }
    uint64_t beats_write() const { return n_wr_; }

protected:
    // =======================================================================
    // Banco de registros
    // =======================================================================
    uint32_t reg_read(uint32_t off) override {
        if (off == LISR) return lisr_;
        if (off == HISR) return hisr_;
        if (off == LIFCR || off == HIFCR) return 0;      // de solo escritura
        if (off < S0_BASE) return 0;
        const uint32_t k = (off - S0_BASE) / S_STRIDE;
        if (k >= N_STREAMS) return 0;
        const Stream& s = st_[k];
        switch ((off - S0_BASE) % S_STRIDE) {
            case SxCR:   return s.cr;
            case SxNDTR: return ndt_items(s);
            case SxPAR:  return s.par;
            case SxM0AR: return s.m0ar;
            case SxM1AR: return s.m1ar;
            case SxFCR:  return (s.fcr & 0x87u) | (fifo_status(s) << 3);
            default:     return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                                 // acceso parcial
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        if (off == LIFCR) { lisr_ &= ~(v & FLAG_MASK); update_irq(); return; }
        if (off == HIFCR) { hisr_ &= ~(v & FLAG_MASK); update_irq(); return; }
        if (off < S0_BASE) return;                        // LISR/HISR de solo lectura
        const uint32_t k = (off - S0_BASE) / S_STRIDE;
        if (k >= N_STREAMS) return;
        Stream& s = st_[k];
        const bool en = (s.cr & 1u) != 0;
        switch ((off - S0_BASE) % S_STRIDE) {
            case SxCR: {
                if (!en) {                                // stream parado: todo rw
                    s.cr = v & 0x0FEFFFFFu;
                    if (v & 1u) start_stream(k);
                } else {
                    // Con EN=1 solo son modificables las habilitaciones de
                    // interrupción, CT (en doble buffer) y el propio EN
                    // [IR, §11.7]. El resto se ignora.
                    const uint32_t keep = 0x0FEFFFE0u;    // config
                    s.cr = (s.cr & keep) | (v & 0x0008001Eu) | (v & 1u);
                    if (!(v & 1u)) stop_stream(k);
                }
                break;
            }
            case SxNDTR:
                if (!en) { s.ndtr = v & 0xFFFFu; s.bytes_total = 0; }
                break;                                    // ignorado con EN=1
            case SxPAR:  if (!en) s.par  = v; break;
            case SxM0AR: if (!en) s.m0ar = v; break;
            case SxM1AR: if (!en || (s.cr >> 18) & 1u) s.m1ar = v; break;
            case SxFCR:  s.fcr = (s.fcr & ~0x87u) | (v & 0x87u); break;
            default: break;
        }
        update_irq();
        kick();
    }

    unsigned access_cycles(bool) const override { return 1; }

private:
    // =======================================================================
    // Estado por stream
    // =======================================================================
    struct Stream {
        uint32_t cr = 0, ndtr = 0, par = 0, m0ar = 0, m1ar = 0, fcr = 0x21;
        // --- estado de ejecución ---
        uint32_t pa = 0, ma = 0;              // punteros actuales
        unsigned src_sz = 1, dst_sz = 1;      // anchos vigentes (bytes)
        uint64_t bytes_total = 0;             // bytes que hay que mover en total
        uint64_t bytes_rd = 0, bytes_wr = 0;  // leídos de origen / escritos a destino
        uint8_t  fifo[FIFO_BYTES] = {};
        unsigned flev = 0;                    // bytes válidos en la FIFO
        bool     run = false;                 // el motor lo está sirviendo
        bool     half_done = false;
    };

    static constexpr uint32_t FLAG_MASK = 0x0F7D0F7Du;    // bits válidos de LISR/HISR
    Stream   st_[N_STREAMS];
    uint32_t lisr_ = 0, hisr_ = 0;
    bool     is_dma2_;
    bool     sw_req_[N_STREAMS] = {};
    // Un aviso por stream y no uno por arranque: el resto seria ruido.
    bool     avisado_celda_[N_STREAMS] = {};
    bool     o_irq_[N_STREAMS] = {};
    bool     o_ack_[N_STREAMS * N_CH] = {};
    uint64_t n_rd_ = 0, n_wr_ = 0;            // estadísticas de beats
    sc_core::sc_event work_ev_, pub_ev_;
    AhbExt   ext_;                            // extensión reutilizada por beat

    // ---- Campos de SxCR ----------------------------------------------------
    static unsigned f_chsel (const Stream& s) { return (s.cr >> 25) & 7u; }
    static unsigned f_mburst(const Stream& s) { return (s.cr >> 23) & 3u; }
    static unsigned f_pburst(const Stream& s) { return (s.cr >> 21) & 3u; }
    static unsigned f_ct    (const Stream& s) { return (s.cr >> 19) & 1u; }
    static unsigned f_dbm   (const Stream& s) { return (s.cr >> 18) & 1u; }
    static unsigned f_pl    (const Stream& s) { return (s.cr >> 16) & 3u; }
    static unsigned f_pincos(const Stream& s) { return (s.cr >> 15) & 1u; }
    static unsigned f_msize (const Stream& s) { return (s.cr >> 13) & 3u; }
    static unsigned f_psize (const Stream& s) { return (s.cr >> 11) & 3u; }
    static unsigned f_minc  (const Stream& s) { return (s.cr >> 10) & 1u; }
    static unsigned f_pinc  (const Stream& s) { return (s.cr >>  9) & 1u; }
    static unsigned f_circ  (const Stream& s) { return (s.cr >>  8) & 1u; }
    static unsigned f_dir   (const Stream& s) { return (s.cr >>  6) & 3u; }
    static unsigned f_pfctrl(const Stream& s) { return (s.cr >>  5) & 1u; }
    static bool     f_dmdis (const Stream& s) { return (s.fcr >> 2) & 1u; }
    static unsigned f_fth   (const Stream& s) { return s.fcr & 3u; }
    static unsigned size_bytes(unsigned code) { return (code >= 3) ? 4u : (1u << code); }
    static unsigned burst_beats(unsigned code) { return 1u << (code + (code ? 1 : 0)); }

    // Tamaño de origen y destino de este stream (en modo directo, el ancho de
    // memoria lo fuerza el hardware al del periférico [IR, §11.3.3]).
    unsigned src_size(const Stream& s) const {
        return (f_dir(s) == 1) ? mem_size(s) : size_bytes(f_psize(s));
    }
    unsigned dst_size(const Stream& s) const {
        return (f_dir(s) == 1) ? size_bytes(f_psize(s)) : mem_size(s);
    }
    unsigned mem_size(const Stream& s) const {
        return f_dmdis(s) ? size_bytes(f_msize(s)) : size_bytes(f_psize(s));
    }
    // Umbral de la FIFO en bytes
    static unsigned fifo_thr_bytes(const Stream& s) { return (f_fth(s) + 1u) * 4u; }

    // Elementos que quedan por transferir, en unidades del ancho de origen.
    // El contador baja cuando el dato correspondiente ha quedado escrito en el
    // destino, que es la fase de escritura de [IR, §11.3.1]; con PSIZE = MSIZE
    // (el caso habitual) equivale a "uno por cada escritura".
    static uint32_t ndt_items(const Stream& s) {
        if (!s.bytes_total) return s.ndtr;
        const unsigned ssz = s.src_sz ? s.src_sz : 1u;
        const uint64_t left = (s.bytes_total > s.bytes_wr) ? (s.bytes_total - s.bytes_wr) : 0;
        return uint32_t((left + ssz - 1) / ssz);
    }

    static unsigned fifo_status(const Stream& s) {
        if (s.flev == 0) return 4u;                       // vacía
        if (s.flev >= FIFO_BYTES) return 5u;              // llena
        return s.flev / 4u;                               // 0-1/4, 1/4-1/2, ...
    }

    // =======================================================================
    // Banderas e interrupciones [IR, §11.5]
    // =======================================================================
    static unsigned flag_shift(unsigned s, unsigned bit) {
        static const unsigned base[4] = {0, 6, 16, 22};
        return base[s & 3u] + bit;
    }
    void set_flag(unsigned s, unsigned bit) {
        uint32_t& r = (s < 4) ? lisr_ : hisr_;
        r |= 1u << flag_shift(s, bit);
        update_irq();
    }
    bool get_flag(unsigned s, unsigned bit) const {
        const uint32_t r = (s < 4) ? lisr_ : hisr_;
        return (r >> flag_shift(s, bit)) & 1u;
    }
    void update_irq() {
        for (unsigned s = 0; s < N_STREAMS; ++s) {
            const Stream& x = st_[s];
            const bool irq =
                (get_flag(s, F_TC)  && ((x.cr >> 4) & 1u)) ||
                (get_flag(s, F_HT)  && ((x.cr >> 3) & 1u)) ||
                (get_flag(s, F_TE)  && ((x.cr >> 2) & 1u)) ||
                (get_flag(s, F_DME) && ((x.cr >> 1) & 1u)) ||
                (get_flag(s, F_FE)  && ((x.fcr >> 7) & 1u));
            o_irq_[s] = irq;
        }
        publish();
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        for (unsigned s = 0; s < N_STREAMS; ++s) irq_stream[s].write(o_irq_[s]);
        for (unsigned i = 0; i < N_STREAMS * N_CH; ++i) ack_out[i].write(o_ack_[i]);
    }
    void kick() { work_ev_.notify(sc_core::SC_ZERO_TIME); }
    void req_watch_proc() { kick(); }
    void reset_proc() { if (!rst_n.read()) { reset_state(); update_irq(); } }

    void reset_state() {
        for (unsigned s = 0; s < N_STREAMS; ++s) { st_[s] = Stream(); sw_req_[s] = false; }
        lisr_ = hisr_ = 0;
        for (unsigned s = 0; s < N_STREAMS; ++s) o_irq_[s] = false;
        for (unsigned i = 0; i < N_STREAMS * N_CH; ++i) o_ack_[i] = false;
    }

    // =======================================================================
    // Arranque y parada de un stream [IR, §11.7, §11.8]
    // =======================================================================
    void start_stream(unsigned k) {
        Stream& s = st_[k];
        const unsigned dir = f_dir(s);

        // --- Comprobaciones de configuración --------------------------------
        // DMA1 no tiene ruta memoria-a-memoria [IR, §11.1.1].
        if (dir == 2 && !is_dma2_) {
            SC_REPORT_WARNING("dma", "memoria-a-memoria no existe en DMA1 [IR, 11.1.1]");
            s.cr &= ~1u; set_flag(k, F_TE); return;
        }
        // El modo directo no admite ráfagas ni memoria-a-memoria [IR, §11.3.3].
        if (!f_dmdis(s) && (f_mburst(s) || f_pburst(s))) {
            s.cr &= ~1u; set_flag(k, F_DME); return;
        }
        if (!f_dmdis(s) && dir == 2) {
            s.cr &= ~1u; set_flag(k, F_FE); return;
        }
        // En modo FIFO, el umbral tiene que ser múltiplo exacto de lo que
        // consume una ráfaga del puerto de memoria [IR, §11.8].
        if (f_dmdis(s) && f_mburst(s)) {
            const unsigned need = size_bytes(f_msize(s)) * burst_beats(f_mburst(s));
            if (need == 0 || fifo_thr_bytes(s) % need != 0 || need > FIFO_BYTES) {
                s.cr &= ~1u; set_flag(k, F_FE); return;
            }
        }
        if (s.ndtr == 0 && !f_pfctrl(s)) { s.cr &= ~1u; return; }

        // La celda elegida, ¿tiene quien pida? En memoria a memoria no hay
        // periférico que pedir, así que la pregunta no se hace. En los otros
        // dos sentidos, un stream armado sobre una celda sin fuente se queda
        // esperando para siempre, y eso hay que DECIRLO: es la diferencia
        // entre un fallo que se diagnostica en un minuto y uno que se lleva
        // una tarde.
        if (dir != 2) {
            const unsigned celda = k * N_CH + f_chsel(s);
            if (!((celdas_con_fuente >> celda) & 1u) && !avisado_celda_[k]) {
                avisado_celda_[k] = true;
                char m[192];
                std::snprintf(m, sizeof m,
                    "stream %u, canal %u (celda %u): este modelo no tiene "
                    "ningun periferico cableado ahi, asi que la peticion no "
                    "va a llegar nunca y el stream se quedara esperando",
                    k, f_chsel(s), celda);
                SC_REPORT_WARNING("dma-celda", m);
            }
        }

        // --- Estado inicial de la transferencia -----------------------------
        s.src_sz     = src_size(s);
        s.dst_sz     = dst_size(s);
        s.pa         = s.par;
        s.ma         = f_ct(s) ? s.m1ar : s.m0ar;
        s.bytes_total= f_pfctrl(s) ? ~uint64_t(0) : uint64_t(s.ndtr) * s.src_sz;
        s.bytes_rd   = 0;
        s.bytes_wr   = 0;
        s.flev       = 0;
        s.half_done  = false;
        s.run        = true;
        kick();
    }

    void stop_stream(unsigned k) {
        Stream& s = st_[k];
        s.run = false;
        s.cr &= ~1u;
        s.flev = 0;
        clear_ack(k);
    }

    void clear_ack(unsigned k) {
        for (unsigned c = 0; c < N_CH; ++c) o_ack_[k * N_CH + c] = false;
        publish();
    }

    // =======================================================================
    // Motor: árbitro + servicio
    // =======================================================================
    bool stream_ready(unsigned k) const {
        const Stream& s = st_[k];
        if (!s.run) return false;
        if (f_dir(s) == 2) return true;                   // mem-a-mem: sin petición
        return sw_req_[k] || req_in[k * N_CH + f_chsel(s)].read();
    }

    // Prioridad de software y, en caso de empate, el stream de número más bajo
    // [IR, §11.3.2].
    int arbitrate() const {
        int best = -1; unsigned best_pl = 0;
        for (unsigned k = 0; k < N_STREAMS; ++k) {
            if (!stream_ready(k)) continue;
            const unsigned pl = f_pl(st_[k]);
            if (best < 0 || pl > best_pl) { best = int(k); best_pl = pl; }
        }
        return best;
    }

    void engine_proc() {
        for (;;) {
            if (!rst_n.read() || !clock_enabled() || domain_hz() <= 0.0) {
                // Sin reloj o en reset el controlador no mueve datos.
                wait(rst_n.value_changed_event() | clk_hz.value_changed_event() |
                     work_ev_);
                continue;
            }
            const int k = arbitrate();
            if (k < 0) { wait(work_ev_ | rst_n.value_changed_event()); continue; }
            service(unsigned(k));
        }
    }

    // Un ciclo del reloj del dominio (HCLK)
    sc_core::sc_time hclk() const {
        const double hz = domain_hz();
        return (hz > 0.0) ? sc_core::sc_time(1.0e12 / hz, sc_core::SC_PS)
                          : sc_core::sc_time(6, sc_core::SC_NS);
    }

    // -----------------------------------------------------------------------
    // Acceso al bus por uno de los dos puertos maestros
    // -----------------------------------------------------------------------
    bool bus_beat(bool mem_port, bool write, uint32_t addr, unsigned char* d,
                  unsigned len, unsigned hburst, sc_core::sc_time& t) {
        tlm::tlm_generic_payload gp;
        ext_ = AhbExt();
        // El puerto de periféricos de DMA1 no cruza la matriz (ataca APB1
        // directamente), así que no necesita un identificador de maestro propio.
        ext_.master = mem_port ? (is_dma2_ ? BusMaster::DMA2_MEM : BusMaster::DMA1_MEM)
                               : (is_dma2_ ? BusMaster::DMA2_PERIPH : BusMaster::DMA1_MEM);
        ext_.hburst = uint8_t(hburst);
        gp_setup(gp, write, addr, d, len);
        gp.set_extension(&ext_);
        sc_core::sc_time local = sc_core::SC_ZERO_TIME;
        if (mem_port) mem_m->b_transport(gp, local);
        else          periph_m->b_transport(gp, local);
        gp.clear_extension(&ext_);
        t += local;
        (write ? n_wr_ : n_rd_)++;
        return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
    }

    // ¿Sale la fase de origen/destino por el puerto de memoria?
    // DIR: 00 P->M (origen periférico), 01 M->P (origen memoria),
    //      10 M->M (origen por el puerto de periféricos, destino por el de
    //      memoria: el DMA lee de SxPAR y escribe en SxM0AR) [IR, §11.6.1].
    static bool src_is_mem(const Stream& s) { return f_dir(s) == 1; }
    static bool dst_is_mem(const Stream& s) { return f_dir(s) != 1; }

    void advance_src(Stream& s) {
        if (src_is_mem(s)) { if (f_minc(s)) s.ma += s.src_sz; }
        else if (f_pinc(s)) s.pa += f_pincos(s) ? 4u : s.src_sz;
    }
    void advance_dst(Stream& s) {
        if (dst_is_mem(s)) { if (f_minc(s)) s.ma += s.dst_sz; }
        else if (f_pinc(s)) s.pa += f_pincos(s) ? 4u : s.dst_sz;
    }
    static uint32_t src_addr(const Stream& s) { return src_is_mem(s) ? s.ma : s.pa; }
    static uint32_t dst_addr(const Stream& s) { return dst_is_mem(s) ? s.ma : s.pa; }

    // -----------------------------------------------------------------------
    // Servicio de una concesión del árbitro
    // -----------------------------------------------------------------------
    void service(unsigned k) {
        Stream& s = st_[k];
        sc_core::sc_time t = hclk();                      // arbitraje y fase de dirección
        // Reconocimiento de la petición hacia el periférico
        if (f_dir(s) != 2) { o_ack_[k * N_CH + f_chsel(s)] = true; publish(); }

        const bool direct = !f_dmdis(s);
        bool ok = true;

        if (direct) {
            ok = beat_direct(k, s, t);
        } else {
            ok = beat_fifo(k, s, t);
        }

        if (f_dir(s) != 2) { o_ack_[k * N_CH + f_chsel(s)] = false; publish(); }
        if (t == sc_core::SC_ZERO_TIME) t = hclk();       // el tiempo siempre avanza
        wait(t);

        if (!ok) {                                        // error de bus [IR, §11.8]
            set_flag(k, F_TE);
            stop_stream(k);
            return;
        }
        check_completion(k, s);
    }

    // Modo directo: un elemento por petición, sin FIFO ni ráfagas.
    bool beat_direct(unsigned k, Stream& s, sc_core::sc_time& t) {
        (void)k;
        const unsigned sz = s.src_sz;                     // = dst_sz en modo directo
        unsigned char buf[4] = {0, 0, 0, 0};
        if (!bus_beat(src_is_mem(s), false, src_addr(s), buf, sz, 0, t)) return false;
        advance_src(s);
        s.bytes_rd += sz;
        if (!bus_beat(dst_is_mem(s), true, dst_addr(s), buf, sz, 0, t)) return false;
        advance_dst(s);
        s.bytes_wr += sz;
        return true;
    }

    // Modo FIFO: llenado desde el origen y vaciado al destino cuando se alcanza
    // el umbral, con empaquetado si los anchos difieren [IR, §11.3.3].
    bool beat_fifo(unsigned k, Stream& s, sc_core::sc_time& t) {
        (void)k;
        const unsigned ssz = s.src_sz, dsz = s.dst_sz;
        const unsigned s_beats = burst_beats(src_is_mem(s) ? f_mburst(s) : f_pburst(s));
        const unsigned d_beats = burst_beats(dst_is_mem(s) ? f_mburst(s) : f_pburst(s));
        const unsigned s_hburst = src_is_mem(s) ? f_mburst(s) : f_pburst(s);
        const unsigned d_hburst = dst_is_mem(s) ? f_mburst(s) : f_pburst(s);

        // ---- Fase de llenado ----------------------------------------------
        const uint64_t left_rd = (s.bytes_total > s.bytes_rd) ? (s.bytes_total - s.bytes_rd) : 0;
        unsigned want = s_beats * ssz;
        if (want > FIFO_BYTES - s.flev) want = ((FIFO_BYTES - s.flev) / ssz) * ssz;
        if (uint64_t(want) > left_rd) want = unsigned(left_rd);
        for (unsigned done = 0; done < want; done += ssz) {
            if (!bus_beat(src_is_mem(s), false, src_addr(s), s.fifo + s.flev, ssz,
                          s_hburst, t)) return false;
            s.flev += ssz;
            s.bytes_rd += ssz;
            advance_src(s);
        }

        // ---- Fase de vaciado ----------------------------------------------
        // Se vuelca cuando la FIFO alcanza el umbral, o cuando ya no queda nada
        // por leer (vaciado final de la cola). Una vez empezado, se vacía hasta
        // que no quede una ráfaga completa: dejar datos por debajo del umbral
        // con la FIFO llena bloquearía el stream.
        const bool flush     = (s.bytes_rd >= s.bytes_total);
        const bool no_room   = (want == 0 && s.flev + ssz > FIFO_BYTES);
        if (s.flev >= fifo_thr_bytes(s) || no_room || (flush && s.flev >= dsz)) {
            for (;;) {
                unsigned beats = d_beats;
                if (s.flev < beats * dsz) {
                    if (!flush && !no_room) break;        // se espera a una ráfaga entera
                    beats = s.flev / dsz;
                }
                if (beats == 0) break;
                for (unsigned b = 0; b < beats; ++b) {
                    if (!bus_beat(dst_is_mem(s), true, dst_addr(s), s.fifo, dsz,
                                  d_hburst, t)) return false;
                    std::memmove(s.fifo, s.fifo + dsz, FIFO_BYTES - dsz);
                    s.flev -= dsz;
                    s.bytes_wr += dsz;
                    advance_dst(s);
                }
                if (s.flev < dsz) break;
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Banderas de progreso y final de transferencia [IR, §11.3.1, §11.6.1]
    // -----------------------------------------------------------------------
    void check_completion(unsigned k, Stream& s) {
        if (f_pfctrl(s)) return;                          // el periférico manda
        if (!s.half_done && s.bytes_wr * 2 >= s.bytes_total) {
            s.half_done = true;
            set_flag(k, F_HT);
        }
        if (s.bytes_wr < s.bytes_total) return;

        set_flag(k, F_TC);
        if (f_dbm(s)) {
            // Doble buffer: se conmuta el objetivo y se recarga [IR, §11.6.1]
            s.cr ^= (1u << 19);
            s.ma = f_ct(s) ? s.m1ar : s.m0ar;
            s.pa = s.par;
            s.bytes_rd = s.bytes_wr = 0;
            s.half_done = false;
        } else if (f_circ(s)) {
            s.ma = s.m0ar;
            s.pa = s.par;
            s.bytes_rd = s.bytes_wr = 0;
            s.half_done = false;
        } else {
            stop_stream(k);                               // el hardware borra EN
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_DMA_H
