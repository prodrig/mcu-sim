// =============================================================================
// dcmi.h — Interfaz de cámara digital (AHB2) [IR, §12.22]
//
// El DCMI es un PUERTO DE ENTRADA SÍNCRONO, no un bus. No pide nada, no
// contesta nada y no puede parar al sensor: se limita a mirar un reloj de pixel
// que le llega de fuera y a coger lo que hay en catorce hilos de datos en el
// flanco que le han dicho. Todo lo que hace el modelo sale de ahí:
//
//   PIXCLK ──►┐ muestreo en el flanco que dice PCKPOL
//   D[13:0] ──┤
//   HSYNC  ──►│ ¿estamos en una línea o en el borrado horizontal?  (HSPOL)
//   VSYNC  ──►┘ ¿estamos en un cuadro o en el borrado vertical?    (VSPOL)
//                    │
//                    ▼
//              recorte (CROP) ─► empaquetado a 32 bits según EDM
//                    │
//                    ▼
//            FIFO de 4 palabras ─► DCMI_DR ─► DMA2 (S1C1 o S7C1)
//
// Y de ahí salen también sus cuatro formas de fallar, que son las que de verdad
// dan trabajo en una placa: que el sensor vaya más rápido de lo que el DMA
// vacía la FIFO (OVR), que los códigos de sincronismo embebido no cuadren
// (ERR), que la ventana de recorte no sea múltiplo de cuatro, y que el ancho de
// bus que se programa no exista en el encapsulado (§ "los catorce canales").
//
// -----------------------------------------------------------------------------
// LOS CATORCE CANALES DE DATOS NO SON IGUALES
//
// El F407 tiene UN SOLO DCMI —no dos, como los CAN o los I2C—, así que la
// pregunta "¿en qué se diferencian los canales?" no se responde comparando
// instancias sino mirando dentro: los catorce hilos D0-D13 son un bus cuyo
// ANCHO SE PROGRAMA (EDM) y cuyos dos hilos más altos NO ESTÁN CABLEADOS en el
// encapsulado LQFP100 del F407VG. La tabla de pines del propio informe
// [IR, cap. 2] lo dice sin decirlo: aparecen DCMI_D0 a DCMI_D11 y no aparece
// ningún DCMI_D12 ni DCMI_D13, porque sus únicas salidas son PF11/PG6 y
// PG7/PI0 y esos puertos no existen en este encapsulado.
//
// Por eso los rasgos separan dos cosas que se confunden con facilidad:
//   * `max_edm`      — lo que el REGISTRO deja programar;
//   * `lineas_pin`   — lo que el ENCAPSULADO tiene soldado.
// En el F407VG valen 3 (hasta 14 bits) y 12. Programar EDM = 11 en este chip
// compila, se escribe y se lee: y captura basura en los dos bits altos, porque
// nadie los conduce. El modelo lo avisa, que es más de lo que hace el silicio.
//
// -----------------------------------------------------------------------------
// SELECCIÓN DEL TIPO DE DCMI
//
//   * en TIEMPO DE COMPILACIÓN, con el alias de plantilla:
//         using Dcmi      = DcmiT<CAPS_DCMI_F407>;    // 14 bits, 12 cableados
//         using DcmiFull  = DcmiT<CAPS_DCMI_144>;     // los 14, encapsulado grande
//         using Dcmi8     = DcmiT<CAPS_DCMI_8BIT>;    // puerto simple de 8 bits
//         using DcmiBsm   = DcmiT<CAPS_DCMI_BSM>;     // con selección byte/línea
//
//   * en TIEMPO DE EJECUCIÓN, con el parámetro del constructor:
//         DcmiBase d{"d", DcmiCaps{...}};
//
// Los rasgos se aplican como MÁSCARA DE ESCRITURA de cada registro: un bit que
// la instancia no implementa lee cero exactamente igual que un bit reservado
// del silicio. Es la misma receta de UsartCaps, TimCaps, SpiCaps, I2cCaps,
// AdcCaps, DacCaps, SdioCaps y CanCaps.
// =============================================================================
#ifndef STM32_PERIPH_DCMI_H
#define STM32_PERIPH_DCMI_H

#include <array>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Los rasgos: los ejes en los que este bloque varía
// ---------------------------------------------------------------------------
struct DcmiCaps {
    // Lo que deja programar el registro: EDM máximo (0=8, 1=10, 2=12, 3=14 bits)
    unsigned max_edm       = 3;
    // Lo que el ENCAPSULADO tiene soldado. No es lo mismo (véase la cabecera).
    unsigned lineas_pin    = 12;
    bool embedded_sync     = true;   // CR.ESS + DCMI_ESCR/ESUR
    bool jpeg              = true;   // CR.JPEG
    bool crop              = true;   // CR.CROP + DCMI_CWSTRT/CWSIZE
    bool frame_rate_ctrl   = true;   // CR.FCRC[1:0]
    bool snapshot          = true;   // CR.CM (un cuadro y para)
    // Selección de byte y de línea (CR.BSM/OEBS/LSM/OELS). NO están en el F407
    // [IR, §12.22.2 solo llega hasta ENABLE]; sí en hermanos posteriores. Es un
    // eje de verdad, y por eso está aquí.
    bool byte_select       = false;
    bool line_select       = false;
    unsigned fifo_words    = 4;      // profundidad de la FIFO [IR, §12.22.1]
    bool dma               = true;   // petición hacia DMA2
    const char* kind       = "DCMI";
};

// --- Las variantes ---------------------------------------------------------
// El F407VG en LQFP100: el bloque completo, pero con doce hilos en el chip.
constexpr DcmiCaps caps_dcmi_f407() {
    DcmiCaps c{};
    c.kind = "DCMI F407VG (registro 14 bits, 12 cableados en LQFP100)";
    return c;
}
// El mismo silicio en un encapsulado con los puertos F, G e I: ahí sí están
// los catorce. Cambia UNA cifra, y es la que decide si EDM = 11 sirve.
constexpr DcmiCaps caps_dcmi_144() {
    DcmiCaps c{};
    c.lineas_pin = 14;
    c.kind = "DCMI 14 bits completos";
    return c;
}
// Puerto de cámara simple: ocho bits, sincronismo por hardware y nada más. Es
// como aparece el bloque en los derivados pequeños de la familia.
constexpr DcmiCaps caps_dcmi_8bit() {
    DcmiCaps c{};
    c.max_edm = 0; c.lineas_pin = 8;
    c.embedded_sync = false; c.jpeg = false; c.crop = false;
    c.frame_rate_ctrl = false;
    c.kind = "DCMI 8 bits, solo sincronismo por hardware";
    return c;
}
// Con selección de byte y de línea, como en los F4x9 y los F7: permite tirar la
// mitad de los datos en el propio periférico (submuestreo).
constexpr DcmiCaps caps_dcmi_bsm() {
    DcmiCaps c{};
    c.lineas_pin = 14;
    c.byte_select = true; c.line_select = true;
    c.kind = "DCMI con seleccion de byte y de linea";
    return c;
}

inline constexpr DcmiCaps CAPS_DCMI_F407 = caps_dcmi_f407();
inline constexpr DcmiCaps CAPS_DCMI_144  = caps_dcmi_144();
inline constexpr DcmiCaps CAPS_DCMI_8BIT = caps_dcmi_8bit();
inline constexpr DcmiCaps CAPS_DCMI_BSM  = caps_dcmi_bsm();

// ---------------------------------------------------------------------------
// La clase base, que toma los rasgos POR VALOR
// ---------------------------------------------------------------------------
class DcmiBase : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};                     // IRQ 78
    sc_core::sc_out<bool> dma_req{"dma_req"};             // DMA2 S1C1 / S7C1

    // Las entradas del sensor (AF13). Son ENTRADAS: el DCMI no conduce nada.
    sc_core::sc_vector<sc_core::sc_signal<bool>> d_in;    // [14]
    sc_core::sc_signal<bool> hsync_in{"hsync_in"}, vsync_in{"vsync_in"},
                             pixclk_in{"pixclk_in"};

    // ---- Mapa de registros [IR, §12.22.2] ---------------------------------
    enum : uint32_t {
        R_CR = 0x00, R_SR = 0x04, R_RIS = 0x08, R_IER = 0x0C, R_MIS = 0x10,
        R_ICR = 0x14, R_ESCR = 0x18, R_ESUR = 0x1C, R_CWSTRT = 0x20,
        R_CWSIZE = 0x24, R_DR = 0x28
    };
    // DCMI_CR
    enum : uint32_t {
        CR_CAPTURE = 1u << 0, CR_CM = 1u << 1, CR_CROP = 1u << 2,
        CR_JPEG = 1u << 3, CR_ESS = 1u << 4, CR_PCKPOL = 1u << 5,
        CR_HSPOL = 1u << 6, CR_VSPOL = 1u << 7, CR_ENABLE = 1u << 14,
        CR_BSM = 3u << 16, CR_OEBS = 1u << 18, CR_LSM = 1u << 19,
        CR_OELS = 1u << 20
    };
    // Banderas (mismo orden en RIS, IER, MIS e ICR) [IR, §12.22.2]
    enum : uint32_t {
        F_FRAME = 1u << 0, F_OVR = 1u << 1, F_ERR = 1u << 2,
        F_VSYNC = 1u << 3, F_LINE = 1u << 4
    };
    // DCMI_SR
    enum : uint32_t { SR_HSYNC = 1u << 0, SR_VSYNC = 1u << 1, SR_FNE = 1u << 2 };

    DcmiBase(sc_core::sc_module_name nm, const DcmiCaps& c)
        : BusSlave(nm, addr::DCMI_B, 0x400), d_in("d_in", 14), caps(c) {
        SC_HAS_PROCESS(DcmiBase);
        // El muestreo es por FLANCO DE PIXCLK, no por HCLK: el sensor manda.
        SC_METHOD(pixclk_proc);  sensitive << pixclk_in;  dont_initialize();
        // HSYNC y VSYNC se miran aparte porque marcan los bordes de línea y de
        // cuadro aunque no haya flanco de reloj en ese instante.
        SC_METHOD(sync_proc);    sensitive << hsync_in << vsync_in;
        dont_initialize();
        SC_METHOD(pub_proc);     sensitive << pub_ev_;
        SC_METHOD(rst_proc);     sensitive << rst_n;
    }

    const DcmiCaps caps;

    // ---- Ventanas para el banco de pruebas ---------------------------------
    unsigned ancho_bits() const { return 8u + 2u * edm(); }
    unsigned bits_utiles() const {                 // los que de verdad llegan
        return ancho_bits() < caps.lineas_pin ? ancho_bits() : caps.lineas_pin;
    }
    uint64_t palabras_capturadas() const { return n_pal_; }
    uint64_t cuadros() const { return n_cuadros_; }
    uint64_t desbordes() const { return n_ovr_; }
    unsigned fifo_ocupada() const { return n_fifo_; }
    uint32_t peek(uint32_t off) { return reg_read(off); }
    bool dbg_armado() const { return armado_; }
    bool dbg_capturando() const { return capturando_; }
    unsigned dbg_linea() const { return linea_; }
    unsigned dbg_pix() const { return pix_; }

protected:
    // ---- Banco de registros ------------------------------------------------
    uint32_t cr_ = 0, ris_ = 0, ier_ = 0, escr_ = 0, esur_ = 0;
    uint32_t cwstrt_ = 0, cwsize_ = 0;
    // ---- FIFO --------------------------------------------------------------
    std::array<uint32_t, 8> fifo_{};
    unsigned n_fifo_ = 0, rd_ = 0, wr_ = 0;
    // ---- Estado de la captura ---------------------------------------------
    bool     capturando_ = false;   // dentro de un cuadro que se está guardando
    bool     armado_ = false;       // CAPTURE puesto, esperando el próximo cuadro
    bool     parado_ovr_ = false;   // tras un OVR no se guarda hasta el cuadro siguiente
    bool     prev_clk_ = false, prev_vs_ = false, prev_hs_ = false;
    uint32_t acum_ = 0;             // palabra en construcción
    unsigned n_acum_ = 0;           // cuántos trozos lleva
    unsigned linea_ = 0, pix_ = 0;  // posición dentro del cuadro
    unsigned salta_ = 0;            // cuadros que quedan por saltar (FCRC)
    // ---- Sincronismo embebido ---------------------------------------------
    bool     es_en_cuadro_ = false, es_en_linea_ = false;
    // ---- Estadísticas ------------------------------------------------------
    uint64_t n_pal_ = 0, n_cuadros_ = 0, n_ovr_ = 0;
    bool     o_irq_ = false, o_drq_ = false;
    bool     aviso_ancho_ = false;
    sc_core::sc_event pub_ev_;

    // ---- Campos del CR -----------------------------------------------------
    bool en()      const { return (cr_ & CR_ENABLE) != 0; }
    bool captura() const { return (cr_ & CR_CAPTURE) != 0; }
    bool snap()    const { return (cr_ & CR_CM) != 0; }
    bool crop()    const { return (cr_ & CR_CROP) != 0; }
    bool jpeg()    const { return (cr_ & CR_JPEG) != 0; }
    bool ess()     const { return (cr_ & CR_ESS) != 0; }
    bool pckpol()  const { return (cr_ & CR_PCKPOL) != 0; }
    bool hspol()   const { return (cr_ & CR_HSPOL) != 0; }
    bool vspol()   const { return (cr_ & CR_VSPOL) != 0; }
    unsigned edm() const { return (cr_ >> 10) & 3u; }
    unsigned fcrc()const { return (cr_ >> 8) & 3u; }
    // Ventana de recorte [IR, §12.22.2]
    unsigned vst()     const { return (cwstrt_ >> 16) & 0x1FFFu; }
    unsigned hoffcnt() const { return cwstrt_ & 0x3FFFu; }
    unsigned vline()   const { return (cwsize_ >> 16) & 0x3FFFu; }
    unsigned capcnt()  const { return cwsize_ & 0x3FFFu; }

    // ¿Está la línea/el cuadro en BORRADO? El bit de polaridad dice cuál es el
    // nivel ACTIVO de la señal de sincronismo, y sincronismo activo significa
    // que NO hay datos válidos [IR, §12.22.2].
    bool en_borrado_v() const { return vsync_in.read() == vspol(); }
    bool en_borrado_h() const { return hsync_in.read() == hspol(); }

    // =======================================================================
    // Máscaras de escritura: aquí es donde los rasgos dejan de ser una tabla
    // =======================================================================
    uint32_t mask_cr() const {
        uint32_t m = CR_CAPTURE | CR_PCKPOL | CR_HSPOL | CR_VSPOL | CR_ENABLE;
        m |= uint32_t(caps.max_edm) << 10;          // EDM hasta donde llegue
        if (caps.snapshot)        m |= CR_CM;
        if (caps.crop)            m |= CR_CROP;
        if (caps.jpeg)            m |= CR_JPEG;
        if (caps.embedded_sync)   m |= CR_ESS;
        if (caps.frame_rate_ctrl) m |= 3u << 8;
        if (caps.byte_select)     m |= CR_BSM | CR_OEBS;
        if (caps.line_select)     m |= CR_LSM | CR_OELS;
        return m;
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:   return cr_;
            case R_SR:   return sr();
            case R_RIS:  return ris_;
            case R_IER:  return ier_;
            case R_MIS:  return ris_ & ier_;
            case R_ICR:  return 0;                       // solo escritura
            case R_ESCR: return caps.embedded_sync ? escr_ : 0u;
            case R_ESUR: return caps.embedded_sync ? esur_ : 0u;
            case R_CWSTRT: return caps.crop ? cwstrt_ : 0u;
            case R_CWSIZE: return caps.crop ? cwsize_ : 0u;
            case R_DR:   return pop();
            default:     return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = (off == R_DR) ? 0u : reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR: {
                const bool antes = captura();
                cr_ = v & mask_cr();
                // Poner CAPTURE no captura: ARMA. La captura empieza con el
                // cuadro siguiente, nunca a mitad de uno [IR, §12.22.1].
                if (!antes && captura() && en()) {
                    armado_ = true;
                    salta_ = 0;
                }
                if (!captura() || !en()) {
                    armado_ = false; capturando_ = false;
                    n_acum_ = 0; acum_ = 0;
                }
                if (!en()) vaciar_fifo();
                // EL DETECTOR DE FLANCOS SE REFERENCIA AQUI, y no es un
                // detalle: lo que el DCMI llama "borrado" depende de VSPOL y
                // de HSPOL, asi que al escribir el registro puede cambiar el
                // SIGNIFICADO del nivel que ya hay en el pin sin que el pin se
                // mueva. Si no se relee, el bloque se queda esperando un flanco
                // que ya ocurrio -o que nunca ocurrira- y no captura nada.
                // Es tambien lo que pasa al ENCENDER el periferico con el
                // sensor ya emitiendo.
                prev_vs_ = en_borrado_v();
                prev_hs_ = en_borrado_h();
                comprueba_ancho();
                publish();
                return;
            }
            case R_IER: ris_ &= 0x1Fu; ier_ = v & 0x1Fu; publish(); return;
            case R_ICR: ris_ &= ~(v & 0x1Fu); publish(); return;
            case R_ESCR: if (caps.embedded_sync) escr_ = v; return;
            case R_ESUR: if (caps.embedded_sync) esur_ = v; return;
            case R_CWSTRT:
                if (caps.crop) cwstrt_ = v & 0x1FFF3FFFu;
                return;
            case R_CWSIZE:
                if (caps.crop) {
                    cwsize_ = v & 0x3FFF3FFFu;
                    // CAPCNT tiene que ser múltiplo de 4: la ventana se recorta
                    // en palabras, no en píxeles sueltos [IR, §12.22.2].
                    if (capcnt() % 4u)
                        SC_REPORT_WARNING("dcmi",
                            "DCMI_CWSIZE.CAPCNT no es multiplo de 4 [IR, 12.22.2]");
                }
                return;
            default: return;                              // SR/RIS/MIS/DR: r
        }
    }
    bool responds_without_clock() const override { return false; }

    uint32_t sr() const {
        uint32_t v = 0;
        if (en_borrado_h()) v |= SR_HSYNC;
        if (en_borrado_v()) v |= SR_VSYNC;
        if (n_fifo_)        v |= SR_FNE;
        return v;
    }

    // =======================================================================
    // La FIFO
    // =======================================================================
    void vaciar_fifo() { n_fifo_ = rd_ = wr_ = 0; }
    void push(uint32_t w) {
        if (n_fifo_ >= caps.fifo_words) {
            // Desbordamiento: el sensor no se puede parar, así que lo que llega
            // se pierde. El silicio deja de guardar hasta el cuadro siguiente,
            // y avisa por OVR [IR, §12.22.2].
            ++n_ovr_;
            ris_ |= F_OVR;
            parado_ovr_ = true;
            publish();
            return;
        }
        fifo_[wr_] = w;
        wr_ = (wr_ + 1u) % caps.fifo_words;
        ++n_fifo_; ++n_pal_;
        publish();
    }
    uint32_t pop() {
        if (!n_fifo_) return 0;
        const uint32_t w = fifo_[rd_];
        rd_ = (rd_ + 1u) % caps.fifo_words;
        --n_fifo_;
        publish();
        return w;
    }

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        o_irq_ = (ris_ & ier_ & 0x1Fu) != 0;
        o_drq_ = caps.dma && n_fifo_ > 0;
        irq.write(o_irq_);
        dma_req.write(o_drq_);
    }
    void rst_proc() {
        if (rst_n.read()) return;
        cr_ = ris_ = ier_ = escr_ = esur_ = cwstrt_ = cwsize_ = 0;
        vaciar_fifo();
        capturando_ = armado_ = parado_ovr_ = false;
        acum_ = 0; n_acum_ = 0; linea_ = pix_ = 0; salta_ = 0;
        es_en_cuadro_ = es_en_linea_ = false;
        n_pal_ = n_cuadros_ = n_ovr_ = 0;
        aviso_ancho_ = false;
        publish();
    }

    // Programar más bits de los que hay soldados no es un error del registro:
    // es un error de PLACA. El modelo lo dice una vez, porque el silicio no
    // dice nada y el resultado son dos bits altos de basura.
    void comprueba_ancho() {
        if (aviso_ancho_ || !en()) return;
        if (ancho_bits() > caps.lineas_pin) {
            aviso_ancho_ = true;
            SC_REPORT_WARNING("dcmi",
                "EDM pide mas bits de los que el encapsulado tiene cableados "
                "[IR, cap. 2: en LQFP100 no hay DCMI_D12/D13]");
        }
    }

    // =======================================================================
    // Bordes de cuadro y de línea (sincronismo por HARDWARE)
    // =======================================================================
    void sync_proc() {
        if (!en() || ess()) { prev_vs_ = en_borrado_v(); prev_hs_ = en_borrado_h(); return; }
        const bool bv = en_borrado_v();
        const bool bh = en_borrado_h();

        if (bv != prev_vs_) {
            if (bv) fin_de_cuadro();          // entra en borrado vertical
            else    principio_de_cuadro();    // sale: empieza un cuadro
            prev_vs_ = bv;
        }
        if (bh != prev_hs_) {
            if (bh && capturando_) fin_de_linea();
            prev_hs_ = bh;
        }
        publish();
    }

    void principio_de_cuadro() {
        linea_ = 0; pix_ = 0;
        n_acum_ = 0; acum_ = 0;
        parado_ovr_ = false;
        // Control de cadencia: capturar uno de cada 1, 2 o 4 cuadros
        // [IR, §12.22.2, FCRC]. La condicion es el BIT CAPTURE, no un estado
        // interno: un cuadro saltado no puede apagar la captura para siempre,
        // que es justo lo que pasaria si se mirase "estaba capturando".
        if (captura()) {
            if (salta_) { --salta_; capturando_ = false; }
            else        { capturando_ = true; armado_ = false; }
        } else capturando_ = false;
    }
    void fin_de_cuadro() {
        if (capturando_) {
            volcar_resto();
            ++n_cuadros_;
            ris_ |= F_FRAME;
            // Instantánea: un cuadro y se acabó. El hardware limpia CAPTURE, y
            // que lo haga el HARDWARE es justo lo que permite al firmware saber
            // que ya está sin mirar el reloj [IR, §12.22.2, CM].
            if (snap() && caps.snapshot) { cr_ &= ~CR_CAPTURE; capturando_ = false; }
            else if (caps.frame_rate_ctrl) salta_ = fcrc();
            capturando_ = false;               // el cuadro se ha cerrado
        }
        ris_ |= F_VSYNC;                       // sincronismo de cuadro
        linea_ = 0; pix_ = 0;
    }
    void fin_de_linea() {
        ris_ |= F_LINE;
        ++linea_;
        pix_ = 0;
        // Una línea nueva empieza en palabra nueva SOLO en modo recorte: fuera
        // de él el flujo es continuo y una línea impar se lleva su último byte
        // a la palabra de la siguiente, que es lo que hace el silicio.
        if (crop() && caps.crop) { n_acum_ = 0; acum_ = 0; }
    }

    // =======================================================================
    // El muestreo, en el flanco de PIXCLK que diga PCKPOL
    // =======================================================================
    void pixclk_proc() {
        const bool c = pixclk_in.read();
        const bool activo = (c != prev_clk_) && (c == pckpol());
        prev_clk_ = c;
        if (!activo || !en()) return;
        if (!clock_enabled()) return;          // sin DCMIEN no hay periferico

        const uint32_t dato = leer_lineas();

        if (ess() && caps.embedded_sync) { muestra_embebida(dato); return; }
        if (en_borrado_v() || en_borrado_h()) return;   // borrado: no hay dato
        if (!capturando_ || parado_ovr_) { ++pix_; return; }
        guarda_pixel(dato);
        ++pix_;
    }

    // Los catorce hilos, recortados a lo que EDM pide. Los que el encapsulado
    // no tiene soldados llegan como los deje el pad: en el modelo, lo que haya
    // en el nodo. No se inventa un cero, porque el silicio tampoco.
    uint32_t leer_lineas() const {
        uint32_t v = 0;
        const unsigned n = ancho_bits();
        for (unsigned i = 0; i < n && i < 14u; ++i)
            if (d_in[i].read()) v |= 1u << i;
        return v;
    }

    // ¿Cae este píxel dentro de la ventana de recorte? [IR, §12.22.2]
    bool dentro_ventana() const {
        if (!crop() || !caps.crop || jpeg()) return true;
        if (linea_ < vst() || linea_ >= vst() + vline()) return false;
        if (pix_ < hoffcnt() || pix_ >= hoffcnt() + capcnt()) return false;
        return true;
    }

    // El empaquetado a 32 bits: en 8 bits caben cuatro muestras por palabra; en
    // 10, 12 y 14 cada muestra ocupa media palabra con los bits altos a cero
    // [IR, §12.22.1: "los datos se empaquetan en palabras de 32 bits"].
    void guarda_pixel(uint32_t dato) {
        if (!dentro_ventana()) return;
        if (edm() == 0) {
            acum_ |= (dato & 0xFFu) << (8u * n_acum_);
            if (++n_acum_ == 4u) { push(acum_); acum_ = 0; n_acum_ = 0; }
        } else {
            const uint32_t m = (1u << ancho_bits()) - 1u;
            acum_ |= (dato & m) << (16u * n_acum_);
            if (++n_acum_ == 2u) { push(acum_); acum_ = 0; n_acum_ = 0; }
        }
    }
    // Al cerrar el cuadro, lo que quede a medias se escribe igual: si no, el
    // último byte de una imagen impar no llegaría nunca a la memoria.
    void volcar_resto() {
        if (n_acum_) { push(acum_); acum_ = 0; n_acum_ = 0; }
    }

    // =======================================================================
    // Sincronismo EMBEBIDO: los códigos viajan en el propio flujo de datos
    //
    // Sin HSYNC ni VSYNC: cuatro códigos de un byte (inicio y fin de cuadro,
    // inicio y fin de línea) comparados con DCMI_ESCR a través de la máscara de
    // DCMI_ESUR. Es el modo de los sensores BT.656, y solo existe con ocho bits
    // [IR, §12.22.1].
    // =======================================================================
    void muestra_embebida(uint32_t dato) {
        const uint32_t b = dato & 0xFFu;
        auto coincide = [&](unsigned k) {
            const uint32_t cod = (escr_ >> (8u * k)) & 0xFFu;
            const uint32_t msk = (esur_ >> (8u * k)) & 0xFFu;
            return ((b ^ cod) & msk) == 0u;
        };
        // FSC(0) LSC(1) LEC(2) FEC(3) [IR, §12.22.2]
        if (coincide(0)) {                                  // inicio de cuadro
            if (es_en_cuadro_) ris_ |= F_ERR;
            es_en_cuadro_ = true; es_en_linea_ = false;
            linea_ = 0; pix_ = 0; n_acum_ = 0; acum_ = 0;
            parado_ovr_ = false;
            if (captura()) {
                if (salta_) { --salta_; capturando_ = false; }
                else        { capturando_ = true; armado_ = false; }
            } else capturando_ = false;
            publish();
            return;
        }
        if (coincide(1)) {                                  // inicio de línea
            if (!es_en_cuadro_ || es_en_linea_) ris_ |= F_ERR;
            es_en_linea_ = true; pix_ = 0;
            if (crop() && caps.crop) { n_acum_ = 0; acum_ = 0; }
            publish();
            return;
        }
        if (coincide(2)) {                                  // fin de línea
            if (!es_en_linea_) ris_ |= F_ERR;
            es_en_linea_ = false;
            if (capturando_) { ris_ |= F_LINE; ++linea_; }
            publish();
            return;
        }
        if (coincide(3)) {                                  // fin de cuadro
            if (!es_en_cuadro_) ris_ |= F_ERR;
            es_en_cuadro_ = false; es_en_linea_ = false;
            if (capturando_) {
                volcar_resto();
                ++n_cuadros_;
                ris_ |= F_FRAME;
                if (snap() && caps.snapshot) cr_ &= ~CR_CAPTURE;
                else if (caps.frame_rate_ctrl) salta_ = fcrc();
                capturando_ = false;
            }
            ris_ |= F_VSYNC;
            publish();
            return;
        }
        // Un byte cualquiera: solo cuenta dentro de una línea de un cuadro.
        if (es_en_cuadro_ && es_en_linea_ && capturando_ && !parado_ovr_) {
            guarda_pixel(b);
            ++pix_;
        }
    }
};

// ---------------------------------------------------------------------------
// La misma clase, con los rasgos fijados en tiempo de compilación
// ---------------------------------------------------------------------------
template <const DcmiCaps& C>
class DcmiT : public DcmiBase {
public:
    explicit DcmiT(sc_core::sc_module_name nm) : DcmiBase(nm, C) {
        static_assert(C.max_edm <= 3, "EDM solo tiene cuatro valores");
        static_assert(C.lineas_pin >= 8 && C.lineas_pin <= 14,
                      "el bus del DCMI va de 8 a 14 hilos [IR, 12.22.1]");
        static_assert(C.fifo_words >= 1 && C.fifo_words <= 8,
                      "la FIFO del DCMI es de 4 palabras [IR, 12.22.1]");
        // El sincronismo embebido solo existe con ocho bits: si la variante lo
        // ofrece, tiene que poder programar EDM = 00.
        static_assert(!C.embedded_sync || C.lineas_pin >= 8, "");
    }
};

using Dcmi     = DcmiT<CAPS_DCMI_F407>;   // el del F407VG
using DcmiFull = DcmiT<CAPS_DCMI_144>;    // los catorce hilos cableados
using Dcmi8    = DcmiT<CAPS_DCMI_8BIT>;   // puerto simple de ocho bits
using DcmiBsm  = DcmiT<CAPS_DCMI_BSM>;    // con seleccion de byte y de linea

} // namespace stm32
#endif // STM32_PERIPH_DCMI_H
