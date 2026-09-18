// =============================================================================
// sai.h — SAI1 y SAI2: el interfaz de audio serie (fase 4 del plan del F446)
//
// SAI es *Serial Audio Interface*, y la palabra que lo explica es la primera:
// **serie**. Donde el I2S del F407 sabía hacer I2S —y poco más—, el SAI es un
// serializador genérico al que se le describe la TRAMA y con eso habla I2S,
// PCM, TDM, AC'97 o SPDIF. La diferencia se ve en los registros: aquí no hay un
// bit «modo I2S», hay `FRCR` —cuántos bits tiene la trama, cuánto dura el
// sincronismo, con qué polaridad— y `SLOTR` —cuántas ranuras, de qué tamaño,
// cuáles están activas—.
//
// DOS BLOQUES POR INSTANCIA, Y ESO ES LO QUE DESPISTA. Un SAI no es un
// periférico: son dos, A y B, cada uno con su juego completo de registros y su
// dirección. Pueden trabajar por separado —uno hablando con un códec y el otro
// con otro— o **sincronizados**: uno es el maestro que genera el reloj y la
// trama, y el otro se cuelga de él (`CR1.SYNCEN`). Esa es la configuración
// normal para full-duplex: A transmite, B recibe, los dos sobre el mismo reloj.
//
// Cada bloque es, por separado:
//   * MAESTRO o ESCLAVO (`MODE[1]`) — quién pone SCK y FS;
//   * TRANSMISOR o RECEPTOR (`MODE[0]`) — hacia dónde va el dato;
// lo que da las cuatro combinaciones que el registro numera del 0 al 3.
//
// QUÉ HAY MODELADO
//   * los dos bloques completos: CR1, CR2, FRCR, SLOTR, IMR, SR, CLRFR y DR,
//     más el `GCR` del bloque padre;
//   * la FIFO de OCHO PALABRAS por bloque, con su nivel en `SR.FLVL` y el
//     umbral de `CR2.FTH` decidiendo cuándo pide datos;
//   * el reloj de la trama calculado de verdad: del reloj del SAI —el que
//     elige `RCC_DCKCFGR.SAIxSRC`, que es lo que la fase 3 dejó listo— salen
//     MCLK dividiendo por `MCKDIV`, y de ahí la frecuencia de muestreo con la
//     longitud de trama de `FRCR`. Cambiar el selector del RCC cambia la
//     frecuencia de muestreo, y eso se puede medir;
//   * las banderas `OVRUDR` —el desbordamiento, que en audio es LA avería
//     típica: el firmware no llegó a tiempo—, `FREQ`, `WCKCFG`, `MUTEDET`,
//     `CNRDY`, `AFSDET` y `LFSDET`, con su máscara en `IMR` y su borrado por
//     `CLRFR`;
//   * los pines SD, SCK, FS y MCLK de cada bloque, con el maestro moviéndolos.
//
// QUÉ NO, y está dicho en `limitaciones()`: no hay compander (µ-law/A-law), ni
// la detección de silencio por cuenta de tramas, ni peticiones de DMA. La
// serialización bit a bit de cada ranura sobre SD tampoco está: el modelo
// mueve SCK y FS con la temporización correcta y entrega/acepta las palabras
// completas por la FIFO, que es lo que el firmware ve. Un códec externo
// conectado bit a bit es trabajo de otra fase.
// =============================================================================
#ifndef STM32_PERIPH_SAI_H
#define STM32_PERIPH_SAI_H

#include <deque>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// UN BLOQUE. Es la unidad de verdad: tiene su dirección, su reloj y su FIFO.
// ---------------------------------------------------------------------------
class SaiBlock : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_out<bool> dma_req{"dma_req"};
    // Pines. En un bloque esclavo, SCK y FS son entradas; el modelo los saca
    // igual y deja que sea `oe` quien diga quién conduce.
    sc_core::sc_signal<bool> sd_out{"sd_out"}, sd_oe{"sd_oe"}, sd_in{"sd_in"};
    sc_core::sc_signal<bool> sck_out{"sck_out"}, sck_oe{"sck_oe"}, sck_in{"sck_in"};
    sc_core::sc_signal<bool> fs_out{"fs_out"}, fs_oe{"fs_oe"}, fs_in{"fs_in"};
    sc_core::sc_signal<bool> mclk_out{"mclk_out"}, mclk_oe{"mclk_oe"};
    // El reloj del bloque, que NO es PCLK: es el que elige RCC_DCKCFGR.SAIxSRC
    // entre el PLLSAI, el PLLI2S, PLL_R y el pin externo [RM0390, §6.3.24].
    sc_core::sc_in<double> sai_clk_hz{"sai_clk_hz"};

    enum : uint32_t {
        R_CR1 = 0x00, R_CR2 = 0x04, R_FRCR = 0x08, R_SLOTR = 0x0C,
        R_IMR = 0x10, R_SR = 0x14, R_CLRFR = 0x18, R_DR = 0x1C
    };
    static constexpr uint32_t C1_SAIEN = 1u << 16, C1_DMAEN = 1u << 17,
        C1_NODIV = 1u << 19, C1_SYNCEN = 3u << 10, C1_MONO = 1u << 12;
    static constexpr uint32_t S_OVRUDR = 1u << 0, S_MUTEDET = 1u << 1,
        S_WCKCFG = 1u << 2, S_FREQ = 1u << 3, S_CNRDY = 1u << 4,
        S_AFSDET = 1u << 5, S_LFSDET = 1u << 6;

    // Los registros del bloque empiezan en el offset 0x04 de su SAI, pero cada
    // bloque se da de alta en el decodificador con SU dirección, así que aquí
    // la base ya viene calculada.
    SaiBlock(sc_core::sc_module_name nm, uint32_t base)
        : BusSlave(nm, base, 0x20) {
        SC_HAS_PROCESS(SaiBlock);
        SC_THREAD(audio_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_; dont_initialize();
        SC_METHOD(rst_proc); sensitive << rst_n;   dont_initialize();
    }

    // --- Consulta para la verificación --------------------------------------
    bool     habilitado() const { return (cr1_ & C1_SAIEN) != 0; }
    bool     maestro()    const { return ((cr1_ & 3u) >> 1) == 0; }
    bool     transmite()  const { return (cr1_ & 1u) == 0; }
    unsigned bits_dato()  const {                    // DS[2:0] -> 8..32
        static const unsigned d[8] = {8, 8, 8, 8, 10, 16, 20, 24};
        const unsigned ds = (cr1_ >> 5) & 7u;
        return (ds == 7) ? 32u : d[ds];
    }
    // MCLK = reloj del SAI / (MCKDIV * 2), con NODIV a cero [RM0390, §29.3.5].
    double mclk_hz() const {
        const unsigned div = (cr1_ >> 20) & 0xFu;
        const double f = sai_clk_hz.read();
        if (f <= 0.0) return 0.0;
        if (cr1_ & C1_NODIV) return f;
        return div ? f / (double(div) * 2.0) : f;
    }
    // La frecuencia de muestreo: MCLK entre 256, que es la relación que el
    // silicio fija cuando NODIV = 0 y la razón de que los códecs de audio
    // pidan «256 fs».
    double fs_hz() const { return (cr1_ & C1_NODIV) ? sck_hz() / marco_bits()
                                                    : mclk_hz() / 256.0; }
    double sck_hz() const { return fs_hz() * double(marco_bits()); }
    unsigned marco_bits() const { return ((frcr_ & 0xFFu) + 1u); }
    unsigned nivel_fifo() const { return unsigned(fifo_.size()); }
    uint32_t peek_sr() const { return sr_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR1:  return cr1_;
            case R_CR2:  return cr2_;
            case R_FRCR: return frcr_;
            case R_SLOTR:return slotr_;
            case R_IMR:  return imr_;
            case R_SR:   return sr_ | (nivel_codificado() << 16);
            case R_CLRFR:return 0;
            case R_DR: {
                if (transmite()) return 0;       // en transmisión, DR es de escritura
                uint32_t v = 0;
                if (!fifo_.empty()) { v = fifo_.front(); fifo_.pop_front(); }
                else                { sr_ |= S_OVRUDR; }   // leer de vacío
                actualiza();
                return v;
            }
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR1: {
                const bool antes = habilitado();
                cr1_ = v & 0x0FFFB7FFu;
                if (!antes && habilitado()) {
                    // Arrancar: la trama empieza. Si la configuración no cuadra
                    // -sin reloj, o con una trama imposible- el silicio levanta
                    // WCKCFG y NO arranca, y eso es lo que el firmware tiene que
                    // ver en vez de un silencio inexplicable.
                    if (sai_clk_hz.read() <= 0.0 && maestro()) {
                        sr_ |= S_WCKCFG;
                        cr1_ &= ~C1_SAIEN;
                    } else {
                        audio_ev_.notify(sc_core::SC_ZERO_TIME);
                    }
                }
                if (antes && !habilitado()) { fifo_.clear(); }
                actualiza();
                break;
            }
            case R_CR2:
                cr2_ = v & 0x0000FFFFu;
                if (v & (1u << 3)) fifo_.clear();          // FFLUSH
                actualiza();
                break;
            case R_FRCR:  frcr_  = v & 0x0007FFFFu; break;
            case R_SLOTR: slotr_ = v & 0xFFFF0FDFu; break;
            case R_IMR:   imr_   = v & 0x0000007Fu; actualiza(); break;
            case R_CLRFR: sr_ &= ~(v & 0x0000007Fu); actualiza(); break;
            case R_DR:
                if (!transmite()) break;                   // en recepción es lectura
                if (fifo_.size() >= 8) sr_ |= S_OVRUDR;    // FIFO llena
                else fifo_.push_back(v);
                actualiza();
                break;
            default: break;
        }
    }
    bool responds_without_clock() const override { return false; }

private:
    uint32_t cr1_ = 0, cr2_ = 0, frcr_ = 0x07u, slotr_ = 0, imr_ = 0, sr_ = 0;
    std::deque<uint32_t> fifo_;
    bool o_irq_ = false, o_drq_ = false;
    bool o_sck_ = false, o_fs_ = false, o_mclk_ = false;
    sc_core::sc_event pub_ev_, audio_ev_;

    unsigned fth() const { return cr2_ & 7u; }
    // SR.FLVL: 000 vacía, 001 menos de 1/4, ... 101 llena [RM0390, §29.5.6]
    uint32_t nivel_codificado() const {
        const size_t n = fifo_.size();
        if (n == 0) return 0;
        if (n >= 8) return 5;
        if (n <= 2) return 1;
        if (n <= 4) return 2;
        if (n <= 6) return 3;
        return 4;
    }
    void publica() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        sd_out.write(false); sd_oe.write(transmite() && habilitado());
        sck_out.write(o_sck_); sck_oe.write(maestro() && habilitado());
        fs_out.write(o_fs_);   fs_oe.write(maestro() && habilitado());
        mclk_out.write(o_mclk_); mclk_oe.write(maestro() && habilitado() &&
                                               !(cr1_ & C1_NODIV));
        irq.write(o_irq_);
        dma_req.write(o_drq_);
    }
    void rst_proc() {
        if (rst_n.read()) return;
        cr1_ = cr2_ = slotr_ = imr_ = sr_ = 0;
        frcr_ = 0x07u;
        fifo_.clear();
        o_irq_ = o_drq_ = false;
        publica();
    }
    void actualiza() {
        // FREQ: hay hueco (transmisor) o hay dato (receptor) según el umbral.
        const size_t n = fifo_.size();
        bool freq = false;
        const size_t umbral = (fth() == 0) ? 1u : (fth() >= 4 ? 8u : 2u * fth());
        if (transmite()) freq = (n <= umbral);
        else             freq = (n >= umbral);
        if (freq) sr_ |= S_FREQ; else sr_ &= ~S_FREQ;
        o_irq_ = (sr_ & imr_ & 0x7Fu) != 0;
        o_drq_ = habilitado() && (cr1_ & C1_DMAEN) && freq;
        publica();
    }

    // -----------------------------------------------------------------------
    // El motor de audio: marca el ritmo de la trama.
    //
    // Cada trama consume una palabra de la FIFO (transmisor) o entrega una
    // (receptor). No se serializa bit a bit sobre SD -eso queda dicho en las
    // limitaciones-, pero el TIEMPO es el de verdad: una trama dura lo que
    // dura, y por eso el OVRUDR de un firmware que no atiende aparece cuando
    // aparecería en la placa.
    // -----------------------------------------------------------------------
    void audio_proc() {
        for (;;) {
            if (!habilitado()) { wait(audio_ev_); continue; }
            const double f = fs_hz();
            if (f <= 0.0) { wait(audio_ev_); continue; }
            const sc_core::sc_time t_trama(1.0 / f, sc_core::SC_SEC);
            // FS marca el principio de la trama.
            o_fs_ = true; publica();
            wait(t_trama / 8.0);
            o_fs_ = false; publica();
            wait(t_trama * (7.0 / 8.0), audio_ev_);
            if (!habilitado()) continue;
            if (transmite()) {
                if (fifo_.empty()) sr_ |= S_OVRUDR;   // underrun: no llegó a tiempo
                else fifo_.pop_front();
            } else {
                if (fifo_.size() >= 8) sr_ |= S_OVRUDR;   // overrun: nadie lee
                else fifo_.push_back(0);                  // sin códec: silencio
            }
            actualiza();
        }
    }
};

// ---------------------------------------------------------------------------
// EL SAI: los dos bloques y el registro global que los une.
//
// `GCR` es el único registro del padre, y sirve para lo que el bloque no puede
// decidir solo: de dónde sale el reloj cuando los dos bloques van
// sincronizados con OTRO SAI (`SYNCIN`) y qué saca este hacia fuera
// (`SYNCOUT`). Con un SAI suelto, cero.
// ---------------------------------------------------------------------------
class Sai : public BusSlave {
public:
    SaiBlock a, b;

    explicit Sai(sc_core::sc_module_name nm, uint32_t base)
        : BusSlave(nm, base, 0x04),
          a("a", base + 0x04), b("b", base + 0x24) {}

protected:
    uint32_t reg_read(uint32_t off) override {
        return (off == 0) ? gcr_ : 0u;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off == 0) gcr_ = v & 0x00000033u;
    }
    bool responds_without_clock() const override { return false; }

private:
    uint32_t gcr_ = 0;
};

} // namespace stm32
#endif // STM32_PERIPH_SAI_H
