// =============================================================================
// hash.h — Procesador de resumen (HASH), AHB2 [RM0090 Rev 22, §25]
//
// El primero de los dos bloques del acelerador criptográfico del F415/F417.
// Calcula MD5 y SHA-1, y los dos HMAC correspondientes. NO calcula SHA-224 ni
// SHA-256: esos son del F42x/F43x, y la tabla 117 —la del F415/417, que es
// OTRA que la 118— lo deja claro con cinco registros de resumen en vez de
// ocho. Pedirle uno de los dos aquí sería pedirle algo que el silicio no tiene.
//
// LO QUE ESTE FICHERO ES: el protocolo. Los registros, la palabra pendiente en
// `HASH_DIN`, el recuento de `NBW`, el intercambio de bytes de `DATATYPE`, el
// recorte de la última palabra con `NBLW`, el disparo con `DCAL`, las cuatro
// fases del HMAC, los 66 o 50 ciclos que el núcleo se pasa ocupado por bloque,
// las dos interrupciones y la petición de DMA. La aritmética NO está aquí: está
// en `hash_algo.h`, y se comprueba aparte contra los vectores del RFC 1321, el
// RFC 3174 y el RFC 2202 (`make hash`).
//
// POR QUÉ ESA FRONTERA. Son dos preguntas distintas y fallan de formas
// distintas. «¿Sale el resumen correcto?» se contesta con vectores y no
// necesita simulación. «¿Sale por donde el firmware lo pide?» necesita el bus,
// y es donde se esconden los errores que de verdad cuestan una tarde: un
// `DATATYPE` mal interpretado da un resumen perfectamente formado y
// equivocado, y no hay forma de distinguirlo de un algoritmo roto si las dos
// cosas están en el mismo sitio.
//
// LO QUE NO ESTÁ MODELADO, dicho aquí y no escondido:
//
//   * El multiple DMA transfer (`MDMAT`) es del F43x y no existe en este
//     registro. No se modela porque no está.
//   * El formato interno de los `HASH_CSRx` es EL DE ESTE MODELO, no el de ST.
//     Lo que el silicio garantiza es que guardar los 51 registros y volverlos a
//     escribir restaura el contexto, y eso es lo que aquí se reproduce de
//     verdad —hay una prueba que intercala dos mensajes—. Un firmware que
//     INTERPRETE el contenido de un CSR no funcionaría; ninguno lo hace, porque
//     ST tampoco documenta el formato.
//   * Escribir `HASH_DIN` con el núcleo ocupado y el búfer lleno avisa una vez
//     por módulo en vez de perder el dato en silencio.
// =============================================================================
#ifndef STM32_PERIPH_HASH_H
#define STM32_PERIPH_HASH_H

#include <cstdint>
#include <cstring>
#include <vector>
#include "../common/periph_base.h"
#include "hash_algo.h"

namespace stm32 {

class Hash : public BusSlave {
public:
    // Comparte la posición 80 con el RNG: en el F415/F417 esa línea es
    // «HASH and Rng global interrupt» [CMSIS, HASH_RNG_IRQn]. El OR lo pone el
    // top en la fase 4; aquí sale la línea propia.
    sc_core::sc_out<bool> irq{"irq"};
    // DMA2, canal 2, stream 7 (`HASH_IN`) [RM0090 Rev 22, tabla 44].
    sc_core::sc_out<bool> dma_req{"dma_req"};

    enum : uint32_t {
        R_CR = 0x00, R_DIN = 0x04, R_STR = 0x08,
        R_HR0 = 0x0C, R_HR4 = 0x1C,
        R_IMR = 0x20, R_SR = 0x24,
        R_CSR0 = 0x0F8, R_CSR50 = 0x1C0,
        R_HR_ALIAS = 0x310            // los mismos HR0..HR4, otra ventana
    };
    enum CrBit : uint32_t {
        CR_INIT = 1u << 2, CR_DMAE = 1u << 3,
        CR_DATATYPE = 3u << 4, CR_MODE = 1u << 6, CR_ALGO = 1u << 7,
        CR_NBW = 0xFu << 8, CR_DINNE = 1u << 12, CR_LKEY = 1u << 16
    };
    enum StrBit : uint32_t { STR_NBLW = 0x1Fu, STR_DCAL = 1u << 8 };
    enum ImrBit : uint32_t { IMR_DINIE = 1u << 0, IMR_DCIE = 1u << 1 };
    enum SrBit  : uint32_t {
        SR_DINIS = 1u << 0, SR_DCIS = 1u << 1, SR_DMAS = 1u << 2, SR_BUSY = 1u << 3
    };

    // Los ciclos de HCLK que el núcleo se pasa ocupado por cada bloque de 512
    // bits [RM0090 Rev 22, §25.3.1]. No son un adorno: son la diferencia entre
    // un modelo que dice cuánto tarda y uno que dice que es gratis.
    static constexpr unsigned CICLOS_SHA1 = 66;
    static constexpr unsigned CICLOS_MD5  = 50;
    static constexpr unsigned N_CSR       = 51;   // CSR0..CSR50, 0x0F8..0x1C0

    explicit Hash(sc_core::sc_module_name nm, uint32_t base = addr::HASH_B)
        : BusSlave(nm, base, 0x400) {
        SC_HAS_PROCESS(Hash);
        SC_THREAD(trabajo_proc);
        SC_METHOD(rst_proc);  sensitive << rst_n;
        SC_METHOD(pub_proc);  sensitive << pub_ev_;
        dont_initialize();
        reinicia_todo_();
    }

    // --- Ventanas para el banco de pruebas ----------------------------------
    uint64_t bloques_procesados() const { return n_bloques_; }
    bool     ocupado() const { return busy_; }

protected:
    // ---- Estado de los registros -------------------------------------------
    uint32_t cr_ = 0, imr_ = 0, sr_ = SR_DINIS, nblw_ = 0;
    uint32_t hr_[5] = {0,0,0,0,0};
    uint32_t csr_[N_CSR] = {0};

    // ---- Estado del núcleo --------------------------------------------------
    // La fase dice qué se está metiendo por `HASH_DIN`. En modo hash solo
    // existe `MENSAJE`; el HMAC recorre las tres. [RM0090 Rev 22, §25.3.6]
    enum class Fase { MENSAJE, CLAVE_INT, CLAVE_EXT, HECHO };

    Resumen  res_;
    AlgoHash algo_ = AlgoHash::Sha1;
    bool     hmac_ = false, lkey_ = false;
    Fase     fase_ = Fase::MENSAJE;
    unsigned nbw_ = 0;                       // palabras metidas en el bloque
    bool     busy_ = false;
    uint64_t n_bloques_ = 0;                 // total, para el banco
    uint64_t bloques_vistos_ = 0;            // para detectar los nuevos
    unsigned ciclos_debe_ = 0;               // ciclos que faltan por cobrar
    bool     aviso_lleno_ = false;
    bool     pend_dcis_ = false;             // el resumen esta lanzado y no listo
    bool     o_irq_ = false, o_dma_ = false;
    // La clave del HMAC, que NO va al acumulador: el núcleo la necesita entera
    // para hacer el XOR con ipad/opad.
    std::vector<uint8_t> clave_;
    uint32_t clave_pend_ = 0;
    bool     hay_clave_pend_ = false;
    uint8_t  dig_int_[20] = {0};
    unsigned n_dig_int_ = 0;

    sc_core::sc_event pub_ev_, trabajo_ev_;

    // -----------------------------------------------------------------------
    // EL INTERCAMBIO DE `DATATYPE`, que es media docena de líneas y la mitad de
    // los errores posibles de este bloque.
    //
    // El procesador trabaja sobre un BIT-STRING big-endian; el bus entrega
    // palabras little-endian. `DATATYPE` dice cómo estaba organizado el
    // original -palabras, medias palabras, bytes o bits sueltos- y el bloque
    // reordena en consecuencia. [RM0090 Rev 22, §25.3.2, figura 235]
    // -----------------------------------------------------------------------
    static uint32_t invierte_bits_(uint32_t w) {
        uint32_t r = 0;
        for (unsigned i = 0; i < 32; ++i) if ((w >> i) & 1u) r |= 1u << (31 - i);
        return r;
    }
    uint32_t reordena_(uint32_t w) const {
        switch ((cr_ & CR_DATATYPE) >> 4) {
            case 0: return w;                                   // 32 bits
            case 1: return (w >> 16) | (w << 16);               // medias palabras
            case 2: return __builtin_bswap32(w);                // bytes
            default: return invierte_bits_(w);                  // bits
        }
    }

    bool en_clave_() const { return fase_ == Fase::CLAVE_INT || fase_ == Fase::CLAVE_EXT; }

    // -----------------------------------------------------------------------
    // Una palabra por `HASH_DIN`
    // -----------------------------------------------------------------------
    void escribe_din_(uint32_t v) {
        const uint32_t w = reordena_(v);
        if (en_clave_()) {
            if (hay_clave_pend_) mete_clave_(clave_pend_, 32);
            clave_pend_ = w; hay_clave_pend_ = true;
        } else {
            res_.palabra(w);
        }
        // `NBW` cuenta las palabras del bloque en curso y vuelve a cero cuando
        // el bloque se procesa o cuando se escribe `INIT`.
        nbw_ = (nbw_ + 1) & 0xFu;
        cobra_bloques_();
        publica_();
    }

    void mete_clave_(uint32_t w, unsigned nbits) {
        if (nbits % 8 != 0) {
            // El silicio admite una clave que no acabe en byte entero; el HMAC
            // de la RFC 2104 no sabe qué hacer con ella, y el HAL de ST nunca
            // la genera. Se dice y se recorta, en vez de inventarse un relleno.
            SC_REPORT_WARNING("hash", "clave de HMAC con un numero de bits que "
                              "no es multiplo de 8: se recorta al byte");
        }
        for (unsigned i = 0; i + 8 <= nbits; i += 8)
            clave_.push_back(uint8_t(w >> (24 - i)));
    }

    // Cobra los ciclos de los bloques que el acumulador haya cerrado.
    void cobra_bloques_() {
        const uint64_t b = res_.bloques();
        if (b > bloques_vistos_) {
            const unsigned n = unsigned(b - bloques_vistos_);
            bloques_vistos_ = b;
            n_bloques_ += n;
            ciclos_debe_ += n * (algo_ == AlgoHash::Sha1 ? CICLOS_SHA1 : CICLOS_MD5);
            nbw_ = 0;
            // Mientras el nucleo mastica, NO pide datos: es lo que el firmware
            // espera cuando sondea `DINIS`, y lo que hace que un mensaje largo
            // se entregue a bloques en vez de de golpe.
            sr_ &= ~SR_DINIS;
            trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    // -----------------------------------------------------------------------
    // `DCAL`: cierra la fase en curso
    // -----------------------------------------------------------------------
    void dcal_() {
        if (!hmac_) {
            res_.final(nblw_);
            cobra_bloques_();
            guarda_digest_();
            fase_ = Fase::HECHO;
            fin_digest_();
            return;
        }
        switch (fase_) {
            case Fase::CLAVE_INT: {
                if (hay_clave_pend_) {
                    mete_clave_(clave_pend_, nblw_ == 0 ? 32u : nblw_);
                    hay_clave_pend_ = false;
                }
                arranca_interno_();
                fase_ = Fase::MENSAJE;
                listo_para_datos_();
                break;
            }
            case Fase::MENSAJE: {
                res_.final(nblw_);
                cobra_bloques_();
                n_dig_int_ = res_.tam_digest();
                res_.digest(dig_int_);
                clave_.clear(); hay_clave_pend_ = false;
                fase_ = Fase::CLAVE_EXT;
                listo_para_datos_();
                break;
            }
            case Fase::CLAVE_EXT: {
                if (hay_clave_pend_) {
                    mete_clave_(clave_pend_, nblw_ == 0 ? 32u : nblw_);
                    hay_clave_pend_ = false;
                }
                arranca_externo_();
                guarda_digest_();
                fase_ = Fase::HECHO;
                fin_digest_();
                break;
            }
            default: break;      // un `DCAL` con el resumen ya cerrado no hace nada
        }
    }

    // La clave rellenada a 64 bytes -o su resumen, si `LKEY`- y el primer
    // bloque del hash interno: (K' XOR ipad).
    void arranca_interno_() {
        uint8_t kp[64];
        // `LKEY` lo dice el firmware; que la clave mida más de 64 bytes lo dice
        // la clave. El silicio obedece al bit, así que el modelo también: si
        // están en desacuerdo, manda `LKEY` y se avisa.
        const bool larga = clave_.size() > 64;
        if (larga != lkey_)
            SC_REPORT_WARNING("hash", "LKEY no concuerda con el tamano real de "
                              "la clave; manda LKEY, como en el silicio");
        if (lkey_) {
            Resumen r; r.inicia(algo_);
            r.bytes(clave_.data(), clave_.size());
            r.termina();
            std::memset(kp, 0, 64);
            r.digest(kp);
            n_bloques_ += r.bloques() + 1;    // el resumen de la clave se paga
            ciclos_debe_ += unsigned(r.bloques() + 1) *
                            (algo_ == AlgoHash::Sha1 ? CICLOS_SHA1 : CICLOS_MD5);
        } else {
            std::memset(kp, 0, 64);
            for (size_t i = 0; i < clave_.size() && i < 64; ++i) kp[i] = clave_[i];
        }
        std::memcpy(kp_, kp, 64);
        uint8_t ipad[64];
        for (unsigned i = 0; i < 64; ++i) ipad[i] = uint8_t(kp[i] ^ 0x36u);
        res_.inicia(algo_);
        bloques_vistos_ = 0;
        res_.bytes(ipad, 64);
        cobra_bloques_();
        nbw_ = 0;
    }

    void arranca_externo_() {
        // El silicio vuelve a leer la clave en esta fase: normalmente es la
        // misma, pero nada obliga a que lo sea, y usar la de antes seria hacer
        // trampa. Si esta fase no trajo clave, se usa la de la fase interna.
        uint8_t kp[64];
        if (clave_.empty()) {
            std::memcpy(kp, kp_, 64);
        } else if (lkey_) {
            Resumen r; r.inicia(algo_);
            r.bytes(clave_.data(), clave_.size());
            r.termina();
            std::memset(kp, 0, 64);
            r.digest(kp);
            n_bloques_ += r.bloques() + 1;
            ciclos_debe_ += unsigned(r.bloques() + 1) *
                            (algo_ == AlgoHash::Sha1 ? CICLOS_SHA1 : CICLOS_MD5);
        } else {
            std::memset(kp, 0, 64);
            for (size_t i = 0; i < clave_.size() && i < 64; ++i) kp[i] = clave_[i];
        }
        uint8_t opad[64];
        for (unsigned i = 0; i < 64; ++i) opad[i] = uint8_t(kp[i] ^ 0x5Cu);
        Resumen r; r.inicia(algo_);
        r.bytes(opad, 64);
        r.bytes(dig_int_, n_dig_int_);
        r.termina();
        n_bloques_ += r.bloques();
        ciclos_debe_ += unsigned(r.bloques()) *
                        (algo_ == AlgoHash::Sha1 ? CICLOS_SHA1 : CICLOS_MD5);
        res_ = r;
        bloques_vistos_ = res_.bloques();
    }

    void guarda_digest_() {
        for (unsigned i = 0; i < 5; ++i) hr_[i] = (i < res_.palabras()) ? res_.hr(i) : 0u;
    }
    // -----------------------------------------------------------------------
    // EL FINAL NO ES CUANDO SE PIDE, ES CUANDO ESTA
    //
    // `DCAL` no termina el resumen: lo lanza. `DCIS` se levanta cuando el
    // nucleo ha acabado de moler el ultimo bloque, y eso son 66 o 50 ciclos
    // despues. Ponerlo a uno en el mismo instante del `DCAL` haria que el
    // firmware leyera el resumen antes de que existiera -y, peor, que el
    // modelo pareciera instantaneo y fuera acumulando una deuda de ciclos que
    // se pagaba en el peor momento-.
    // -----------------------------------------------------------------------
    void fin_digest_() {
        pend_dcis_ = true;
        sr_ &= ~SR_DINIS;
        nbw_ = 0;
        if (ciclos_debe_ == 0) resuelve_();
        else trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
        publica_();
    }
    void listo_para_datos_() {
        nbw_ = 0;
        if (ciclos_debe_ == 0) resuelve_();
        else trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
        publica_();
    }
    // Lo que pasa cuando el nucleo se queda sin trabajo pendiente.
    void resuelve_() {
        if (pend_dcis_) { pend_dcis_ = false; sr_ |= SR_DCIS; sr_ &= ~SR_DINIS; }
        else            { sr_ |= SR_DINIS; }
    }

    // -----------------------------------------------------------------------
    // `INIT`: el núcleo empieza de cero
    // -----------------------------------------------------------------------
    void init_() {
        algo_ = (cr_ & CR_ALGO) ? AlgoHash::Md5 : AlgoHash::Sha1;
        hmac_ = (cr_ & CR_MODE) != 0;
        lkey_ = (cr_ & CR_LKEY) != 0;
        res_.inicia(algo_);
        bloques_vistos_ = 0;
        clave_.clear(); hay_clave_pend_ = false; clave_pend_ = 0;
        n_dig_int_ = 0;
        std::memset(kp_, 0, 64);
        fase_ = hmac_ ? Fase::CLAVE_INT : Fase::MENSAJE;
        nbw_ = 0;
        sr_ = (sr_ & ~SR_DCIS) | SR_DINIS;
        for (unsigned i = 0; i < 5; ++i) hr_[i] = 0;
        publica_();
    }

    void reinicia_todo_() {
        cr_ = 0; imr_ = 0; nblw_ = 0;
        sr_ = SR_DINIS;
        busy_ = false; ciclos_debe_ = 0; n_bloques_ = 0; pend_dcis_ = false;
        for (unsigned i = 0; i < N_CSR; ++i) csr_[i] = 0;
        csr_[0] = 0x00000002u;              // [RM0090 Rev 22, tabla 117]
        init_();
    }

    // -----------------------------------------------------------------------
    // EL CONTEXTO (`HASH_CSRx`)
    //
    // El formato es el de este modelo y se dice en la cabecera del fichero. Lo
    // que importa -y lo que el silicio promete- es que leer los 51 y volverlos
    // a escribir devuelve el bloque al estado en que estaba. Eso aquí es verdad
    // de verdad: hay una prueba que intercala dos mensajes.
    // -----------------------------------------------------------------------
    void serializa_ctx_() {
        // 0: banderas; 1..: el acumulador en bruto.
        csr_[0] = 0x00000002u | (uint32_t(algo_ == AlgoHash::Md5) << 8)
                | (uint32_t(hmac_) << 9) | (uint32_t(lkey_) << 10)
                | (uint32_t(unsigned(fase_)) << 12) | (uint32_t(nbw_) << 16);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&res_);
        static_assert(sizeof(Resumen) <= (N_CSR - 1) * 4,
                      "el acumulador no cabe en los CSR");
        for (unsigned i = 0; i < N_CSR - 1; ++i) {
            uint32_t w = 0;
            for (unsigned b = 0; b < 4; ++b) {
                const size_t k = i * 4 + b;
                if (k < sizeof(Resumen)) w |= uint32_t(p[k]) << (8 * b);
            }
            csr_[1 + i] = w;
        }
    }
    void deserializa_ctx_() {
        algo_ = ((csr_[0] >> 8) & 1u) ? AlgoHash::Md5 : AlgoHash::Sha1;
        hmac_ = ((csr_[0] >> 9) & 1u) != 0;
        lkey_ = ((csr_[0] >> 10) & 1u) != 0;
        fase_ = Fase((csr_[0] >> 12) & 3u);
        nbw_  = (csr_[0] >> 16) & 0xFu;
        uint8_t* p = reinterpret_cast<uint8_t*>(&res_);
        for (size_t k = 0; k < sizeof(Resumen); ++k)
            p[k] = uint8_t(csr_[1 + k / 4] >> (8 * (k % 4)));
        bloques_vistos_ = res_.bloques();
    }

    // -----------------------------------------------------------------------
    // El bus
    // -----------------------------------------------------------------------
    uint32_t reg_read(uint32_t off) override {
        if (off >= R_CSR0 && off <= R_CSR50) {
            serializa_ctx_();
            return csr_[(off - R_CSR0) / 4];
        }
        if (off >= R_HR_ALIAS && off <= R_HR_ALIAS + 0x10) return hr_[(off - R_HR_ALIAS) / 4];
        switch (off) {
            case R_CR:
                return (cr_ & (CR_DMAE | CR_DATATYPE | CR_MODE | CR_ALGO | CR_LKEY))
                     | (uint32_t(nbw_) << 8)
                     | (res_.hay_pendiente() || hay_clave_pend_ ? CR_DINNE : 0u);
            case R_DIN:  return 0;               // el búfer no se lee
            case R_STR:  return nblw_;           // `DCAL` lee siempre cero
            case R_IMR:  return imr_;
            case R_SR:   return sr_ | (busy_ ? SR_BUSY : 0u)
                              | ((cr_ & CR_DMAE) ? SR_DMAS : 0u);
            default:
                if (off >= R_HR0 && off <= R_HR4) return hr_[(off - R_HR0) / 4];
                return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t /*be*/) override {
        if (off >= R_CSR0 && off <= R_CSR50) {
            csr_[(off - R_CSR0) / 4] = v;
            if (off == R_CSR50) deserializa_ctx_();     // la última cierra la restauración
            return;
        }
        switch (off) {
            case R_CR: {
                cr_ = v & (CR_DMAE | CR_DATATYPE | CR_MODE | CR_ALGO | CR_LKEY);
                if (v & CR_INIT) init_();
                publica_();
                break;
            }
            case R_DIN:
                if (busy_ && nbw_ == 0xFu && !aviso_lleno_) {
                    aviso_lleno_ = true;
                    SC_REPORT_WARNING("hash", "escritura en HASH_DIN con el "
                                      "nucleo ocupado y el bufer lleno: el "
                                      "firmware deberia esperar a DINIS");
                }
                escribe_din_(v);
                break;
            case R_STR:
                nblw_ = v & STR_NBLW;
                if (v & STR_DCAL) dcal_();
                break;
            case R_IMR: imr_ = v & (IMR_DINIE | IMR_DCIE); publica_(); break;
            case R_SR:
                // `DCIS` y `DINIS` son rc_w0: escribir cero los baja.
                if (!(v & SR_DCIS))  sr_ &= ~SR_DCIS;
                if (!(v & SR_DINIS)) sr_ &= ~SR_DINIS;
                publica_();
                break;
            default: break;                    // HR y el alias son de solo lectura
        }
    }

    // -----------------------------------------------------------------------
    // Los ciclos que el núcleo se pasa ocupado
    // -----------------------------------------------------------------------
    void trabajo_proc() {
        for (;;) {
            if (ciclos_debe_ == 0) { sc_core::wait(trabajo_ev_); continue; }
            const unsigned n = ciclos_debe_; ciclos_debe_ = 0;
            const double f = clk_hz.read();
            if (f <= 0.0) { sc_core::wait(trabajo_ev_); continue; }
            busy_ = true; publica_();
            sc_core::wait(double(n) / f, sc_core::SC_SEC);
            busy_ = false;
            if (ciclos_debe_ == 0) resuelve_();
            publica_();
        }
    }

    void rst_proc() {
        if (!rst_n.read()) { reinicia_todo_(); publica_(); }
    }

    void publica_() {
        const bool irq = ((sr_ & SR_DINIS) && (imr_ & IMR_DINIE)) ||
                         ((sr_ & SR_DCIS)  && (imr_ & IMR_DCIE));
        const bool dma = (cr_ & CR_DMAE) && !busy_ && fase_ != Fase::HECHO;
        if (irq != o_irq_ || dma != o_dma_) {
            o_irq_ = irq; o_dma_ = dma;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
    void pub_proc() { irq.write(o_irq_); dma_req.write(o_dma_); }

    uint8_t kp_[64] = {0};
};

} // namespace stm32
#endif // STM32_PERIPH_HASH_H
