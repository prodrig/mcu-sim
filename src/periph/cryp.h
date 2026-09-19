// =============================================================================
// cryp.h — Procesador criptográfico (CRYP), AHB2 [RM0090 Rev 22, §23]
//
// El segundo bloque del acelerador del F415/F417. Cifra y descifra con **AES de
// 128, 192 y 256 bits en ECB, CBC y CTR**, y con **DES y TDES en ECB y CBC**.
//
// LO QUE NO TIENE, y no es una simplificación de este modelo sino del silicio:
// **GCM y CCM son del F42x/F43x** [§23.2], y con ellos se van los dieciséis
// registros de contexto `CRYP_CSGCMCCM*`/`CRYP_CSGCM*`. La tabla 114 —la del
// F415/417— **termina en el desplazamiento `0x4C`**; la 115, la del F43x, sigue
// hasta `0x8C`. La cabecera `stm32f417xx.h` de ST SÍ declara esos dieciséis,
// porque es una cabecera compartida por toda la familia F4, y escribir el
// modelo desde ella en vez de desde la tabla habría metido en un F417 registros
// que su silicio no tiene. Es la misma trampa que I-44.
//
// EL REPARTO, igual que en el HASH: la aritmética está en `cryp_algo.h` —los
// dos cifradores de bloque, y nada más— y aquí está **todo lo que el bloque
// pone alrededor**: las dos FIFO de ocho palabras, el intercambio de
// `DATATYPE`, el encadenado de CBC, el contador de CTR, la preparación de clave
// para descifrar en AES, `BUSY` y los ciclos de la tabla 111, las dos
// interrupciones de servicio de FIFO y las dos peticiones de DMA.
//
// LOS REGISTROS DE CLAVE SON DE SOLO ESCRITURA, y eso simplifica una cosa que
// parecía complicada. El manual dice que al preparar la clave para descifrar
// (`ALGOMODE = 111`) el resultado «se copia de vuelta en K0..K3»; como esos
// registros no se pueden leer [tabla de §23.6.10, todos los bits marcados `w`],
// esa copia no es observable desde el firmware. El modelo hace lo que SÍ se ve:
// cobra los ciclos, mantiene `BUSY` y deja `CRYPEN` a cero al terminar, que es
// lo que el manual promete y lo que el HAL de ST espera.
// =============================================================================
#ifndef STM32_PERIPH_CRYP_H
#define STM32_PERIPH_CRYP_H

#include <cstdint>
#include <cstring>
#include <deque>
#include "../common/periph_base.h"
#include "cryp_algo.h"

namespace stm32 {

class Cryp : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};          // posición 79, propia del CRYP
    sc_core::sc_out<bool> dma_in{"dma_in"};    // DMA2, canal 2, stream 6
    sc_core::sc_out<bool> dma_out{"dma_out"};  // DMA2, canal 2, stream 5

    enum : uint32_t {
        R_CR = 0x00, R_SR = 0x04, R_DIN = 0x08, R_DOUT = 0x0C,
        R_DMACR = 0x10, R_IMSCR = 0x14, R_RISR = 0x18, R_MISR = 0x1C,
        R_K0LR = 0x20, R_K3RR = 0x3C,
        R_IV0LR = 0x40, R_IV1RR = 0x4C
    };
    enum CrBit : uint32_t {
        CR_ALGODIR = 1u << 2, CR_ALGOMODE = 7u << 3, CR_DATATYPE = 3u << 6,
        CR_KEYSIZE = 3u << 8, CR_FFLUSH = 1u << 14, CR_CRYPEN = 1u << 15
    };
    enum SrBit : uint32_t {
        SR_IFEM = 1u << 0, SR_IFNF = 1u << 1, SR_OFNE = 1u << 2,
        SR_OFFU = 1u << 3, SR_BUSY = 1u << 4
    };
    enum IntBit : uint32_t { I_IN = 1u << 0, I_OUT = 1u << 1 };
    enum DmaBit : uint32_t { DMA_DIEN = 1u << 0, DMA_DOEN = 1u << 1 };

    // Los ocho modos de `ALGOMODE` [RM0090 Rev 22, §23.6.1].
    enum Modo : uint32_t {
        M_TDES_ECB = 0, M_TDES_CBC = 1, M_DES_ECB = 2, M_DES_CBC = 3,
        M_AES_ECB = 4, M_AES_CBC = 5, M_AES_CTR = 6, M_AES_PREP = 7
    };

    // Ciclos de HCLK por bloque [RM0090 Rev 22, tabla 111]. Son la diferencia
    // entre un modelo que dice cuánto tarda y uno que dice que es gratis: a
    // 168 MHz, cifrar un kilobyte en AES-128 son 64 bloques × 14 ciclos.
    static constexpr unsigned CICLOS_AES[3] = { 14, 16, 18 };   // 128/192/256
    static constexpr unsigned CICLOS_DES    = 16;
    static constexpr unsigned CICLOS_TDES   = 48;
    static constexpr unsigned HONDO_FIFO    = 8;                // palabras

    explicit Cryp(sc_core::sc_module_name nm, uint32_t base = addr::CRYP_B)
        : BusSlave(nm, base, 0x400) {
        SC_HAS_PROCESS(Cryp);
        SC_THREAD(nucleo_proc);
        SC_METHOD(rst_proc);  sensitive << rst_n;
        SC_METHOD(pub_proc);  sensitive << pub_ev_;
        dont_initialize();
        reinicia_();
    }

    // --- Ventanas del banco de pruebas --------------------------------------
    uint64_t bloques_procesados() const { return n_bloques_; }
    bool     ocupado() const { return busy_; }

protected:
    uint32_t cr_ = 0, imscr_ = 0, dmacr_ = 0;
    uint32_t k_[8] = {0};                    // K0LR..K3RR, de solo escritura
    uint32_t iv_[4] = {0};                   // IV0LR..IV1RR
    std::deque<uint32_t> fin_, fout_;
    bool     busy_ = false;
    bool     prep_pendiente_ = false;        // hay una preparación de clave
    uint64_t n_bloques_ = 0;
    bool     aviso_lleno_ = false;
    bool     o_irq_ = false, o_din_ = false, o_dout_ = false;
    uint8_t  enc_[16] = {0};                 // estado de encadenado: IV, C-1 o contador
    bool     enc_cargado_ = false;
    sc_core::sc_event pub_ev_, trabajo_ev_;

    // --- Lo que dicen los bits ----------------------------------------------
    Modo     modo()  const { return Modo((cr_ & CR_ALGOMODE) >> 3); }
    bool     descifra() const { return (cr_ & CR_ALGODIR) != 0; }
    unsigned keysize() const { return (cr_ & CR_KEYSIZE) >> 8; }   // 0/1/2
    bool     crypen() const { return (cr_ & CR_CRYPEN) != 0; }
    bool     es_aes() const { return modo() >= M_AES_ECB; }
    unsigned palabras_bloque() const { return es_aes() ? 4u : 2u; }

    // -----------------------------------------------------------------------
    // El intercambio de `DATATYPE`, idéntico al del HASH y por el mismo motivo:
    // el núcleo trabaja sobre un bloque big-endian y el bus entrega palabras
    // little-endian. Las cuatro transformaciones son INVOLUCIONES —aplicarlas
    // dos veces devuelve el original—, así que la misma función vale para la
    // entrada y para la salida. [RM0090 Rev 22, §23.3.3]
    // -----------------------------------------------------------------------
    static uint32_t invierte_bits_(uint32_t w) {
        uint32_t r = 0;
        for (unsigned i = 0; i < 32; ++i) if ((w >> i) & 1u) r |= 1u << (31 - i);
        return r;
    }
    uint32_t reordena_(uint32_t w) const {
        switch ((cr_ & CR_DATATYPE) >> 6) {
            case 0: return w;
            case 1: return (w >> 16) | (w << 16);
            case 2: return __builtin_bswap32(w);
            default: return invierte_bits_(w);
        }
    }

    // -----------------------------------------------------------------------
    // La clave, sacada de los ocho registros según el modo y `KEYSIZE`
    //
    // Los ocho registros son un espacio de 256 bits, b255..b0, y cada algoritmo
    // usa el TROZO BAJO que necesita: AES-128 coge b127..b0 —es decir K2 y K3—,
    // AES-192 desde K1, AES-256 los ocho. En TDES, K1, K2 y K3 son las tres
    // claves de 64 bits y K0 no se usa; en DES, solo K1. [§23.6.10]
    // -----------------------------------------------------------------------
    unsigned bytes_clave_() const {
        if (!es_aes()) return (modo() == M_DES_ECB || modo() == M_DES_CBC) ? 8u : 24u;
        return 16u + 8u * (keysize() > 2 ? 0u : keysize());     // 16, 24 o 32
    }
    void clave_(uint8_t* out) const {
        const unsigned n = bytes_clave_();
        const unsigned desde = (n == 8) ? 2u : (8u - n / 4u);   // K1LR para DES
        for (unsigned i = 0; i < n / 4; ++i) {
            const uint32_t w = k_[desde + i];
            out[4*i+0] = uint8_t(w >> 24); out[4*i+1] = uint8_t(w >> 16);
            out[4*i+2] = uint8_t(w >> 8);  out[4*i+3] = uint8_t(w);
        }
    }

    void carga_encadenado_() {
        const unsigned n = es_aes() ? 4u : 2u;
        for (unsigned i = 0; i < n; ++i) {
            enc_[4*i+0] = uint8_t(iv_[i] >> 24); enc_[4*i+1] = uint8_t(iv_[i] >> 16);
            enc_[4*i+2] = uint8_t(iv_[i] >> 8);  enc_[4*i+3] = uint8_t(iv_[i]);
        }
        enc_cargado_ = true;
    }

    unsigned ciclos_bloque_() const {
        if (es_aes()) return CICLOS_AES[keysize() > 2 ? 0 : keysize()];
        return (modo() == M_DES_ECB || modo() == M_DES_CBC) ? CICLOS_DES : CICLOS_TDES;
    }

    // -----------------------------------------------------------------------
    // Un bloque: saca las palabras de la FIFO de entrada, lo procesa y mete el
    // resultado en la de salida. El ENCADENADO se hace aquí, alrededor del
    // núcleo, igual que en el silicio: `cryp_algo.h` solo sabe de bloques
    // sueltos.
    // -----------------------------------------------------------------------
    void procesa_bloque_() {
        const unsigned np = palabras_bloque();
        uint8_t in[16], out[16];
        for (unsigned i = 0; i < np; ++i) {
            const uint32_t w = reordena_(fin_.front()); fin_.pop_front();
            in[4*i+0] = uint8_t(w >> 24); in[4*i+1] = uint8_t(w >> 16);
            in[4*i+2] = uint8_t(w >> 8);  in[4*i+3] = uint8_t(w);
        }
        if (!enc_cargado_) carga_encadenado_();
        uint8_t clave[32]; clave_(clave);

        if (es_aes()) {
            ClaveAes k; expande_aes(clave, bytes_clave_(), k);
            if (modo() == M_AES_CTR) {
                // Cifrar y descifrar son la MISMA operación: se cifra el
                // contador y se hace XOR. Por eso `ALGODIR` da igual aquí, y el
                // manual lo dice con todas sus letras.
                uint8_t ks[16];
                cifra_aes(k, enc_, ks);
                for (unsigned i = 0; i < 16; ++i) out[i] = uint8_t(in[i] ^ ks[i]);
                for (int i = 15; i >= 0; --i) if (++enc_[i]) break;
            } else if (modo() == M_AES_ECB) {
                if (descifra()) descifra_aes(k, in, out); else cifra_aes(k, in, out);
            } else {                                     // AES-CBC
                if (!descifra()) {
                    uint8_t x[16];
                    for (unsigned i = 0; i < 16; ++i) x[i] = uint8_t(in[i] ^ enc_[i]);
                    cifra_aes(k, x, out);
                    std::memcpy(enc_, out, 16);
                } else {
                    uint8_t x[16];
                    descifra_aes(k, in, x);
                    for (unsigned i = 0; i < 16; ++i) out[i] = uint8_t(x[i] ^ enc_[i]);
                    std::memcpy(enc_, in, 16);
                }
            }
        } else {
            const bool solo_des = (modo() == M_DES_ECB || modo() == M_DES_CBC);
            const bool cbc      = (modo() == M_DES_CBC || modo() == M_TDES_CBC);
            des::Subclaves k1, k2, k3;
            des::expande(u64_(clave), k1);
            if (!solo_des) { des::expande(u64_(clave + 8), k2); des::expande(u64_(clave + 16), k3); }
            const uint64_t m = u64_(in);
            uint64_t prev = u64_(enc_), r;
            if (!cbc) {
                r = solo_des ? des::bloque(k1, m, descifra())
                             : cifra_tdes(k1, k2, k3, m, descifra());
            } else if (!descifra()) {
                const uint64_t x = m ^ prev;
                r = solo_des ? des::bloque(k1, x, false) : cifra_tdes(k1, k2, k3, x, false);
                prev = r;
            } else {
                const uint64_t x = solo_des ? des::bloque(k1, m, true)
                                            : cifra_tdes(k1, k2, k3, m, true);
                r = x ^ prev;
                prev = m;
            }
            de_u64_(prev, enc_);
            de_u64_(r, out);
        }

        for (unsigned i = 0; i < np; ++i) {
            const uint32_t w = (uint32_t(out[4*i]) << 24) | (uint32_t(out[4*i+1]) << 16)
                             | (uint32_t(out[4*i+2]) << 8) | uint32_t(out[4*i+3]);
            fout_.push_back(reordena_(w));
        }
        ++n_bloques_;
    }

    static uint64_t u64_(const uint8_t* p) {
        uint64_t v = 0;
        for (unsigned i = 0; i < 8; ++i) v = (v << 8) | p[i];
        return v;
    }
    static void de_u64_(uint64_t v, uint8_t* p) {
        for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(v >> (56 - 8*i));
    }

    // -----------------------------------------------------------------------
    // El núcleo, con su tiempo
    // -----------------------------------------------------------------------
    void nucleo_proc() {
        for (;;) {
            const double f = clk_hz.read();
            if (f <= 0.0) { sc_core::wait(trabajo_ev_); continue; }

            if (prep_pendiente_) {
                // Preparación de clave para descifrar en AES. El manual no le da
                // un número propio en la tabla 111, así que se cobra lo mismo
                // que una ronda de su tamaño de clave —es lo que es— y se dice.
                prep_pendiente_ = false;
                busy_ = true; publica_();
                sc_core::wait(double(ciclos_bloque_()) / f, sc_core::SC_SEC);
                busy_ = false;
                cr_ &= ~CR_CRYPEN;          // el hardware lo baja al acabar
                publica_();
                continue;
            }
            if (crypen() && fin_.size() >= palabras_bloque() &&
                fout_.size() + palabras_bloque() <= HONDO_FIFO) {
                const unsigned n = ciclos_bloque_();
                busy_ = true; publica_();
                sc_core::wait(double(n) / f, sc_core::SC_SEC);
                procesa_bloque_();
                busy_ = false;
                publica_();
                continue;
            }
            sc_core::wait(trabajo_ev_);
        }
    }

    // -----------------------------------------------------------------------
    // El bus
    // -----------------------------------------------------------------------
    uint32_t sr_() const {
        uint32_t v = 0;
        if (fin_.empty())                    v |= SR_IFEM;
        if (fin_.size() < HONDO_FIFO)        v |= SR_IFNF;
        if (!fout_.empty())                  v |= SR_OFNE;
        if (fout_.size() >= HONDO_FIFO)      v |= SR_OFFU;
        if (busy_)                           v |= SR_BUSY;
        return v;
    }
    // Servicio de FIFO. El manual es LITERAL y conviene copiarlo bien, porque
    // la frontera está a un dato de distancia: la de entrada pide «cuando hay
    // MENOS DE CUATRO palabras en ella», y se calla en cuanto tiene cuatro o
    // más; la de salida, «en cuanto hay una o más», que es seguir a `OFNE`.
    // [RM0090 Rev 22, §23.5]
    //
    // La primera versión de este modelo puso «cuando le caben cuatro», que es
    // lo mismo en siete de los nueve tamaños posibles y distinto justo en el
    // que importa. Lo cazó el banco.
    uint32_t risr_() const {
        uint32_t v = 0;
        if (fin_.size() < 4)   v |= I_IN;
        if (!fout_.empty())    v |= I_OUT;
        return v;
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:    return cr_ & ~CR_FFLUSH;      // FFLUSH lee siempre cero
            case R_SR:    return sr_();
            case R_DIN:   return 0;                     // de solo escritura
            case R_DOUT:
                if (fout_.empty()) return 0;
                else {
                    const uint32_t v = fout_.front(); fout_.pop_front();
                    trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
                    publica_();
                    return v;
                }
            case R_DMACR: return dmacr_;
            case R_IMSCR: return imscr_;
            case R_RISR:  return risr_();
            case R_MISR:  return risr_() & imscr_ & (crypen() ? ~0u : ~uint32_t(I_IN));
            default:      return 0;                     // claves e IV: solo escritura
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t /*be*/) override {
        if (off >= R_K0LR && off <= R_K3RR) { k_[(off - R_K0LR) / 4] = v; return; }
        if (off >= R_IV0LR && off <= R_IV1RR) {
            iv_[(off - R_IV0LR) / 4] = v;
            enc_cargado_ = false;            // un IV nuevo reinicia el encadenado
            return;
        }
        switch (off) {
            case R_CR: {
                const bool antes = crypen();
                // `FFLUSH` solo vale con el núcleo parado, y entonces vacía las
                // dos FIFO. Con `CRYPEN = 1` no hace nada. [§23.6.1]
                if ((v & CR_FFLUSH) && !(v & CR_CRYPEN)) { fin_.clear(); fout_.clear(); }
                cr_ = v & ~CR_FFLUSH;
                if (!antes && crypen()) {
                    enc_cargado_ = false;    // al arrancar se toma el IV vigente
                    if (modo() == M_AES_PREP) prep_pendiente_ = true;
                }
                trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
                publica_();
                break;
            }
            case R_DIN:
                if (fin_.size() >= HONDO_FIFO) {
                    if (!aviso_lleno_) {
                        aviso_lleno_ = true;
                        SC_REPORT_WARNING("cryp", "escritura en CRYP_DIN con la "
                                          "FIFO de entrada llena: el firmware "
                                          "deberia mirar IFNF antes");
                    }
                    break;                   // el dato se pierde, como en el silicio
                }
                fin_.push_back(v);
                trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
                publica_();
                break;
            case R_DMACR: dmacr_ = v & (DMA_DIEN | DMA_DOEN); publica_(); break;
            case R_IMSCR: imscr_ = v & (I_IN | I_OUT); publica_(); break;
            default: break;                  // SR, RISR y MISR son de solo lectura
        }
    }

    void reinicia_() {
        cr_ = 0; imscr_ = 0; dmacr_ = 0;
        fin_.clear(); fout_.clear();
        busy_ = false; prep_pendiente_ = false; n_bloques_ = 0;
        enc_cargado_ = false;
        for (unsigned i = 0; i < 8; ++i) k_[i] = 0;
        for (unsigned i = 0; i < 4; ++i) iv_[i] = 0;
        publica_();
    }
    void rst_proc() { if (!rst_n.read()) reinicia_(); }

    void publica_() {
        const uint32_t m = risr_() & imscr_;
        const bool irq = ((m & I_IN) && crypen()) || (m & I_OUT);
        const bool din  = (dmacr_ & DMA_DIEN) && crypen() && fin_.size() < 4;
        const bool dout = (dmacr_ & DMA_DOEN) && !fout_.empty();
        if (irq != o_irq_ || din != o_din_ || dout != o_dout_) {
            o_irq_ = irq; o_din_ = din; o_dout_ = dout;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
    void pub_proc() { irq.write(o_irq_); dma_in.write(o_din_); dma_out.write(o_dout_); }
};

} // namespace stm32
#endif // STM32_PERIPH_CRYP_H
