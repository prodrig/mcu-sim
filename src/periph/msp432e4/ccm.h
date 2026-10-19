// =============================================================================
// ccm.h — El CCM del MSP432E4: CRC, SHA/MD5, AES y DES (T7)
//
// Un solo bloque de 40 KB en 0x4403_0000 con un solo bit de reloj (RCGCCCM)
// [H: msp432e401y.h, CCM0_BASE; hw_ccm.h, hw_shamd5.h, hw_aes.h, hw_des.h;
// segunda fuente de cada secuencia: crc.c, shamd5.c, aes.c y des.c de
// DriverLib]. Los cifradores y resúmenes son los de `cryp_algo.h` y
// `hash_algo.h`, los mismos del F415/F417: aquí solo está el periférico.
//
//   0x0000  los registros de uDMA de los tres (DMAIM/RIS/MIS/IC)
//   0x0400  CRC      CRCCTRL, CRCSEED, CRCDIN, CRCRSLTPP
//   0x4000  SHA/MD5  ODIGEST A-H, IDIGEST A-H, DIGEST_COUNT, MODE, LENGTH,
//                    DATA_0..15_IN, SYSCONFIG/SYSSTATUS, IRQSTATUS/ENABLE
//   0x6000  AES      KEY2/KEY1 (en un orden raro: la palabra 0 de KEY1 está
//                    en 0x38, la 1 en 0x3C, la 2 en 0x30...), IV_IN, CTRL,
//                    C_LENGTH, AUTH_LENGTH, DATA_IN (la palabra 0 del bloque
//                    en 0x6C y la 3 en 0x60), TAG_OUT
//   0x8000  DES      KEY3/2/1 (L y H), IV, CTRL, LENGTH, DATA_L/H
//
// LOS BYTES. Todo -claves, IV, datos, resúmenes- va en palabras de 32 bits
// con los bytes en el orden canónico en LITTLE-ENDIAN: la palabra que
// DriverLib lee de un `uint32_t[]` en memoria. En el SHA eso quiere decir que
// el registro de resumen NO es la palabra de estado del algoritmo sino sus
// bytes: SHA-1 y SHA-2 publican en big-endian, así que el registro es la
// palabra de estado con los bytes dados la vuelta; MD5 publica en
// little-endian y el registro es la palabra tal cual [1F: el controlador
// omap-sham de Linux, la misma IP; P-17 o].
//
// EL TIEMPO: cada bloque se calcula al escribir su última palabra y está
// listo en el acto [modelo: P-17 o]. Los bits de «listo» son los de verdad,
// y DriverLib los espera igual.
//
//   AES   ECB, CBC, CFB (de 128), CTR (contador de 32 a 128 bits), ICM y GCM
//         (el modo autónomo: H e Y0 los calcula el motor; el IV es J0); claves
//         de 128, 192 y 256. CCM, CBC-MAC, XTS, f8 y f9 se guardan y avisan
//   DES   DES y TDES (EDE con tres claves), ECB, CBC y CFB de 64
//   SHA   MD5, SHA-1, SHA-224 y SHA-256, con el resumen partido en varias
//         llamadas (sin CLOSE_HASH, el estado queda en IDIGEST y la cuenta en
//         DIGEST_COUNT), HMAC con la clave (KEY_PROC) y con la clave ya
//         procesada (SHAMD5HMACPPKeyGenerate/Set)
//   CRC   los polinomios 0x8005, 0x1021, 0x04C11DB7 y 0x1EDC6F41 y la suma
//         de TCP; entrada de 8 o 32 bits con su orden de bytes, inversión de
//         bits a la entrada y a la salida, e inversión del resultado
//
// LO QUE NO ESTÁ: la uDMA de los tres (los bits de SYSCONFIG y DMAIM se
// guardan), sus interrupciones (el E401Y no las tiene en el NVIC), y los
// modos de AES de arriba [P-17 o].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_CCM_H
#define STM32_PERIPH_MSP432E4_CCM_H

#include <array>
#include <cstdio>
#include <cstring>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"
#include "../cryp_algo.h"
#include "../hash_algo.h"

namespace stm32 {

// La multiplicación de GCM en GF(2^128), con el bit 0 a la izquierda
// [NIST SP 800-38D, §6.3, algoritmo 1]
inline void gf128_mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]) {
    uint8_t v[16], r[16] = {0};
    std::memcpy(v, y, 16);
    for (unsigned i = 0; i < 128; ++i) {
        if ((x[i / 8] >> (7 - i % 8)) & 1u)
            for (unsigned k = 0; k < 16; ++k) r[k] ^= v[k];
        const bool lsb = v[15] & 1u;
        for (int k = 15; k > 0; --k) v[k] = uint8_t((v[k] >> 1) | (v[k - 1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1;
    }
    std::memcpy(z, r, 16);
}

class CcmE4 : public BusSlave {
public:
    // Desplazamientos dentro del bloque (base 0x4403_0000)
    enum : uint32_t { CRC_O = 0x0400, SHA_O = 0x4000, AES_O = 0x6000, DES_O = 0x8000 };

    explicit CcmE4(sc_core::sc_module_name nm) : BusSlave(nm, e4::CCM_B, e4::CCM_FIN - e4::CCM_B) {
        SC_HAS_PROCESS(CcmE4);
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    unsigned avisos() const { return n_avisos_; }
    unsigned bloques_aes() const { return n_aes_; }
    unsigned bloques_des() const { return n_des_; }
    unsigned bloques_sha() const { return n_sha_; }

protected:
    unsigned n_avisos_ = 0, n_aes_ = 0, n_des_ = 0, n_sha_ = 0;
    uint32_t dma_[16] = {0};
    void aviso(const char* m) { ++n_avisos_; SC_REPORT_WARNING("msp432e4/ccm", m); }

    static void a_bytes(uint32_t w, uint8_t* b) {
        b[0] = uint8_t(w); b[1] = uint8_t(w >> 8); b[2] = uint8_t(w >> 16); b[3] = uint8_t(w >> 24);
    }
    static uint32_t de_bytes(const uint8_t* b) {
        return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    }

    void reset_proc() {
        if (rst_n.read()) return;
        std::memset(dma_, 0, sizeof dma_);
        crc_reset(); sha_reset(); aes_reset(); des_reset();
    }

    // ======================================================================
    // CRC
    // ======================================================================
    uint32_t crcctrl_ = 0, crc_ = 0;
    void crc_reset() { crcctrl_ = 0; crc_ = 0; tcp_medio_ = false; }
    unsigned crc_ancho() const {
        const unsigned t = crcctrl_ & 0xFu;
        return (t == 2 || t == 3) ? 32u : 16u;
    }
    uint32_t crc_poli() const {
        switch (crcctrl_ & 0xFu) {
            case 0: return 0x8005u; case 1: return 0x1021u;
            case 2: return 0x04C11DB7u; case 3: return 0x1EDC6F41u;
            default: return 0;
        }
    }
    static uint8_t refleja8(uint8_t b) {
        uint8_t r = 0;
        for (unsigned i = 0; i < 8; ++i) if (b & (1u << i)) r = uint8_t(r | (0x80u >> i));
        return r;
    }
    static uint32_t refleja(uint32_t v, unsigned n) {
        uint32_t r = 0;
        for (unsigned i = 0; i < n; ++i) if (v & (1u << i)) r |= 1u << (n - 1 - i);
        return r;
    }
    void crc_byte(uint8_t b) {
        if (crcctrl_ & 0x80u) b = refleja8(b);
        const unsigned w = crc_ancho();
        const uint32_t msk = w == 32 ? 0xFFFFFFFFu : 0xFFFFu, top = 1u << (w - 1);
        uint32_t c = crc_ & msk;
        c ^= uint32_t(b) << (w - 8);
        for (unsigned i = 0; i < 8; ++i) c = (c & top) ? ((c << 1) ^ crc_poli()) : (c << 1);
        crc_ = c & msk;
    }
    // La suma de TCP va de 16 en 16 bits: el primer byte que entra es el alto
    bool tcp_medio_ = false;
    uint8_t tcp_alto_ = 0;
    void tcp_byte(uint8_t b) {
        if (!tcp_medio_) { tcp_alto_ = b; tcp_medio_ = true; return; }
        tcp_medio_ = false;
        const uint32_t s = (crc_ & 0xFFFFu) + ((uint32_t(tcp_alto_) << 8) | b);
        crc_ = (s & 0xFFFFu) + (s >> 16);
    }
    void crc_din(uint32_t v) {
        const bool tcp = (crcctrl_ & 0xFu) == 8;
        if (crcctrl_ & 0x1000u) { tcp ? tcp_byte(uint8_t(v)) : crc_byte(uint8_t(v)); return; }
        // ENDIAN: en que orden entran los cuatro bytes de la palabra; sin
        // cambiar, el mas significativo primero [H: hw_ccm.h, CCM_CRCCTRL_ENDIAN]
        const uint8_t B[4] = { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) };
        static const uint8_t ORDEN[4][4] = { {3, 2, 1, 0}, {2, 3, 0, 1}, {1, 0, 3, 2}, {0, 1, 2, 3} };
        const unsigned e = (crcctrl_ >> 4) & 3u;
        for (unsigned i = 0; i < 4; ++i) tcp ? tcp_byte(B[ORDEN[e][i]]) : crc_byte(B[ORDEN[e][i]]);
    }
    uint32_t crc_pp() const {
        const unsigned w = crc_ancho();
        const uint32_t msk = w == 32 ? 0xFFFFFFFFu : 0xFFFFu;
        uint32_t r = crc_ & msk;
        if (crcctrl_ & 0x100u) r = refleja(r, w);
        if (crcctrl_ & 0x200u) r = ~r & msk;
        return r;
    }

    // ======================================================================
    // SHA/MD5
    // ======================================================================
    uint32_t odig_[8] = {0}, idig_[8] = {0}, cuenta_ = 0, mode_ = 0, length_ = 0;
    uint32_t sha_sysconfig_ = 0, sha_irqen_ = 0;
    bool sha_activo_ = false, sha_salida_ = false;
    AlgoHash sha_algo_ = AlgoHash::Md5;
    uint32_t hin_[8] = {0}, hout_[8] = {0};     // los estados interior y exterior
    uint64_t sha_total_ = 0;                    // bytes ya en el estado interior
    uint32_t sha_recibidos_ = 0;                // de esta operación
    uint8_t blk_[64] = {0};
    unsigned blk_n_ = 0;                        // bytes en el bloque

    void sha_reset() {
        std::memset(odig_, 0, sizeof odig_); std::memset(idig_, 0, sizeof idig_);
        cuenta_ = mode_ = length_ = sha_sysconfig_ = sha_irqen_ = 0;
        sha_activo_ = sha_salida_ = false; blk_n_ = 0; sha_recibidos_ = 0; sha_total_ = 0;
    }
    AlgoHash algo_de(uint32_t m) const {
        switch (m & 7u) {
            case 2: return AlgoHash::Sha1; case 4: return AlgoHash::Sha224;
            case 6: return AlgoHash::Sha256; default: return AlgoHash::Md5;
        }
    }
    // Registro <-> palabra de estado (véase arriba: los bytes del resumen)
    uint32_t a_reg(uint32_t h) const { return sha_algo_ == AlgoHash::Md5 ? h : __builtin_bswap32(h); }
    uint32_t de_reg(uint32_t r) const { return a_reg(r); }
    void sha_estado_a_regs(const uint32_t* h, uint32_t* regs) const {
        for (unsigned i = 0; i < 8; ++i) regs[i] = a_reg(h[i]);
    }
    void sha_bloque(uint32_t* h, const uint8_t* b) { bloque_hash(sha_algo_, h, b); ++n_sha_; }
    // Cierra un estado con el relleno: `n` bytes pendientes en `b`, `total`
    // bytes en el mensaje
    void sha_cierra(uint32_t* h, uint8_t* b, unsigned n, uint64_t total) {
        b[n++] = 0x80;
        if (n > 56) { while (n < 64) b[n++] = 0; sha_bloque(h, b); n = 0; }
        while (n < 56) b[n++] = 0;
        const uint64_t bits = total * 8;
        for (unsigned i = 0; i < 8; ++i)
            b[56 + i] = sha_algo_ == AlgoHash::Md5 ? uint8_t(bits >> (8 * i)) : uint8_t(bits >> (56 - 8 * i));
        sha_bloque(h, b);
    }
    unsigned sha_palabras() const { return sha_algo_ == AlgoHash::Sha224 ? 7u : palabras_estado(sha_algo_); }

    // LENGTH: empieza una operación de `length_` bytes
    void sha_empieza() {
        sha_algo_ = algo_de(mode_);
        const bool constantes = mode_ & 0x08u, cierra = mode_ & 0x10u,
                   clave = mode_ & 0x20u, exterior = mode_ & 0x80u;
        blk_n_ = 0; sha_recibidos_ = 0; sha_salida_ = false; sha_activo_ = true;
        if (clave) {
            // HMAC: la clave (64 bytes) está en ODIGEST e IDIGEST
            uint8_t k[64];
            for (unsigned i = 0; i < 8; ++i) { a_bytes(odig_[i], k + 4 * i); a_bytes(idig_[i], k + 32 + 4 * i); }
            uint8_t ip[64], op[64];
            for (unsigned i = 0; i < 64; ++i) { ip[i] = uint8_t(k[i] ^ 0x36); op[i] = uint8_t(k[i] ^ 0x5C); }
            estado_inicial(sha_algo_, hin_); sha_bloque(hin_, ip);
            estado_inicial(sha_algo_, hout_); sha_bloque(hout_, op);
            sha_total_ = 64;
        } else if (exterior) {
            // La clave ya procesada: interior en IDIGEST, exterior en ODIGEST
            for (unsigned i = 0; i < 8; ++i) { hin_[i] = de_reg(idig_[i]); hout_[i] = de_reg(odig_[i]); }
            sha_total_ = cuenta_;
        } else if (constantes) {
            estado_inicial(sha_algo_, hin_);
            sha_total_ = 0;
        } else {
            for (unsigned i = 0; i < 8; ++i) hin_[i] = de_reg(idig_[i]);
            sha_total_ = cuenta_;
        }
        (void)cierra;
        if (length_ == 0) sha_termina();
    }
    void sha_dato(unsigned idx, uint32_t v) {
        if (!sha_activo_) return;
        uint8_t b[4]; a_bytes(v, b);
        for (unsigned k = 0; k < 4 && sha_recibidos_ < length_; ++k) {
            const unsigned pos = idx * 4 + k;
            if (pos != blk_n_) continue;                 // fuera de orden: no cuenta
            blk_[blk_n_++] = b[k];
            ++sha_recibidos_;
            if (blk_n_ == 64) {
                if (sha_recibidos_ < length_ || !(mode_ & 0x10u)) {
                    sha_bloque(hin_, blk_); sha_total_ += 64; blk_n_ = 0;
                }
            }
        }
        if (sha_recibidos_ >= length_) sha_termina();
    }
    void sha_termina() {
        const bool cierra = mode_ & 0x10u, clave = mode_ & 0x20u, exterior = mode_ & 0x80u;
        if (cierra) {
            if (blk_n_ == 64) { sha_bloque(hin_, blk_); sha_total_ += 64; blk_n_ = 0; }
            const uint64_t total = sha_total_ + blk_n_;
            sha_cierra(hin_, blk_, blk_n_, total);
            if (exterior) {
                uint8_t d[32] = {0};
                uint32_t tmp[8];
                sha_estado_a_regs(hin_, tmp);
                const unsigned nb = sha_palabras() * 4;
                for (unsigned i = 0; i < 8; ++i) a_bytes(tmp[i], d + 4 * i);
                uint8_t b[64]; std::memcpy(b, d, nb);
                sha_cierra(hout_, b, nb, 64 + nb);
                sha_estado_a_regs(hout_, idig_);
            } else {
                sha_estado_a_regs(hin_, idig_);
            }
            cuenta_ = uint32_t(total);
        } else if (clave && length_ == 0) {
            // Generar la clave procesada: exterior en ODIGEST, interior en IDIGEST
            sha_estado_a_regs(hout_, odig_);
            sha_estado_a_regs(hin_, idig_);
            cuenta_ = 64;
        } else {
            // Un trozo sin cerrar: el estado y la cuenta, para seguir
            if (blk_n_ != 0) aviso("SHA/MD5 sin CLOSE_HASH con un trozo que no es multiplo "
                                   "de 64 bytes: los ultimos se pierden");
            sha_estado_a_regs(hin_, idig_);
            cuenta_ = uint32_t(sha_total_);
        }
        sha_activo_ = false;
        sha_salida_ = true;
    }
    uint32_t sha_irqstatus() const {
        uint32_t s = 0;
        if (!sha_activo_) s |= 0x08u;                    // CONTEXT_READY
        if (sha_activo_) s |= 0x02u;                     // INPUT_READY
        if (sha_salida_) s |= 0x01u;                     // OUTPUT_READY
        return s;
    }
    uint32_t sha_read(uint32_t o) {
        if (o < 0x20) return odig_[o / 4];
        if (o < 0x40) return idig_[(o - 0x20) / 4];
        switch (o) {
            case 0x40: return cuenta_;
            case 0x44: return mode_;
            case 0x48: return length_;
            case 0x100: return 0x00000001u;             // REVISION [modelo]
            case 0x110: return sha_sysconfig_;
            case 0x114: return 1u;                      // RESETDONE
            case 0x118: return sha_irqstatus();
            case 0x11C: return sha_irqen_;
            default: return 0;
        }
    }
    void sha_write(uint32_t o, uint32_t v) {
        if (o < 0x20) { odig_[o / 4] = v; return; }
        if (o < 0x40) { idig_[(o - 0x20) / 4] = v; return; }
        if (o >= 0x80 && o < 0xC0) { sha_dato((o - 0x80) / 4, v); return; }
        switch (o) {
            case 0x40: cuenta_ = v; break;
            case 0x44: mode_ = v & 0xBFu; sha_salida_ = false; break;
            case 0x48: length_ = v; sha_empieza(); break;
            case 0x110:
                sha_sysconfig_ = v & 0x3Cu;
                if (v & 0x2u) sha_reset();
                if (v & 0x8u) aviso("SHA/MD5 con uDMA: no esta modelada");
                break;
            case 0x11C: sha_irqen_ = v & 0xFu; break;
            default: break;
        }
    }

    // ======================================================================
    // AES
    // ======================================================================
    uint32_t key1_[8] = {0}, key2_[8] = {0}, aiv_[4] = {0}, actrl_ = 0, alen0_ = 0, alen1_ = 0,
             authlen_ = 0, din_[4] = {0}, dout_[4] = {0}, tag_[4] = {0}, asys_ = 0, airqen_ = 0;
    unsigned din_mask_ = 0, dout_mask_ = 0;
    bool aes_salida_ = false, aes_ctx_ = false, aes_empezado_ = false;
    ClaveAes clave_;
    uint8_t ghash_[16] = {0}, h_[16] = {0}, j0_[16] = {0}, ctr_[16] = {0}, cbc_[16] = {0};
    uint64_t aad_hecho_ = 0, dat_hecho_ = 0;
    bool avisado_modo_ = false;

    void aes_reset() {
        std::memset(key1_, 0, sizeof key1_); std::memset(key2_, 0, sizeof key2_);
        std::memset(aiv_, 0, sizeof aiv_); actrl_ = alen0_ = alen1_ = authlen_ = asys_ = airqen_ = 0;
        din_mask_ = dout_mask_ = 0; aes_salida_ = aes_ctx_ = aes_empezado_ = false;
    }
    // La palabra i de la clave: 0x38, 0x3C, 0x30, 0x34, 0x28, 0x2C, 0x20, 0x24
    static int key1_idx(uint32_t o) {
        static const uint32_t OFF[8] = { 0x38, 0x3C, 0x30, 0x34, 0x28, 0x2C, 0x20, 0x24 };
        for (int i = 0; i < 8; ++i) if (OFF[i] == o) return i;
        return -1;
    }
    unsigned aes_nk() const {
        const unsigned k = (actrl_ >> 3) & 3u;
        return k == 1 ? 16u : k == 2 ? 24u : 32u;
    }
    bool aes_gcm() const { return (actrl_ & 0x00030000u) == 0x00030000u; }
    bool aes_ctr() const { return actrl_ & 0x40u; }
    bool aes_cbc() const { return actrl_ & 0x20u; }
    bool aes_cfb() const { return actrl_ & 0x400u; }
    bool aes_icm() const { return actrl_ & 0x200u; }
    bool aes_cifra() const { return actrl_ & 0x4u; }
    uint64_t aes_len() const { return (uint64_t(alen1_) << 32) | alen0_; }

    void aes_empieza() {
        uint8_t k[32];
        for (unsigned i = 0; i < 8; ++i) a_bytes(key1_[i], k + 4 * i);
        expande_aes(k, aes_nk(), clave_);
        uint8_t iv[16];
        for (unsigned i = 0; i < 4; ++i) a_bytes(aiv_[i], iv + 4 * i);
        std::memcpy(ctr_, iv, 16); std::memcpy(cbc_, iv, 16); std::memcpy(j0_, iv, 16);
        std::memset(ghash_, 0, 16);
        if (aes_gcm()) { const uint8_t cero[16] = {0}; cifra_aes(clave_, cero, h_); }
        aad_hecho_ = dat_hecho_ = 0;
        aes_ctx_ = false;
        aes_empezado_ = true;
        const uint32_t raros = actrl_ & 0x0004F800u;   // CCM, CBC-MAC, F9, F8, XTS
        if (((actrl_ & 0x00030000u) && !aes_gcm()) || raros) {
            if (!avisado_modo_) {
                avisado_modo_ = true;
                aviso("AES en CCM, CBC-MAC, XTS, f8, f9 o GCM con H o Y0 cargados: no "
                      "estan modelados; lo que salga no es lo del chip");
            }
        }
    }
    void incrementa_ctr(uint8_t c[16], unsigned bits) {
        for (int i = 15; i >= int(16 - bits / 8); --i) if (++c[i] != 0) break;
    }
    void ghash_bloque(const uint8_t b[16]) {
        uint8_t x[16];
        for (unsigned i = 0; i < 16; ++i) x[i] = uint8_t(ghash_[i] ^ b[i]);
        gf128_mul(x, h_, ghash_);
    }
    void aes_cierra() {
        if (aes_gcm()) {
            uint8_t l[16] = {0};
            const uint64_t a = uint64_t(authlen_) * 8, c = aes_len() * 8;
            for (unsigned i = 0; i < 8; ++i) { l[i] = uint8_t(a >> (56 - 8 * i)); l[8 + i] = uint8_t(c >> (56 - 8 * i)); }
            ghash_bloque(l);
            uint8_t s[16]; cifra_aes(clave_, j0_, s);
            for (unsigned i = 0; i < 16; ++i) s[i] ^= ghash_[i];
            for (unsigned i = 0; i < 4; ++i) tag_[i] = de_bytes(s + 4 * i);
        }
        const uint8_t* iv = aes_ctr() ? ctr_ : cbc_;
        for (unsigned i = 0; i < 4; ++i) aiv_[i] = de_bytes(iv + 4 * i);
        aes_ctx_ = true;
    }
    void aes_procesa() {
        if (!aes_empezado_) aes_empieza();
        uint8_t in[16], out[16];
        for (unsigned i = 0; i < 4; ++i) a_bytes(din_[i], in + 4 * i);
        ++n_aes_;
        if (aes_gcm() && aad_hecho_ < authlen_) {
            const uint64_t n = std::min<uint64_t>(16, authlen_ - aad_hecho_);
            uint8_t b[16] = {0}; std::memcpy(b, in, size_t(n));
            ghash_bloque(b);
            aad_hecho_ += 16;
            if (aad_hecho_ >= authlen_ && aes_len() == 0) aes_cierra();
            return;                                  // sin salida
        }
        const uint64_t len = aes_len();
        const unsigned n = len ? unsigned(std::min<uint64_t>(16, len - std::min(len, dat_hecho_))) : 16u;
        if (aes_gcm() || aes_ctr() || aes_icm()) {
            const unsigned bits = aes_gcm() ? 32u : aes_icm() ? 128u : 32u * (((actrl_ >> 7) & 3u) + 1u);
            if (aes_gcm()) incrementa_ctr(ctr_, 32);
            uint8_t ks[16]; cifra_aes(clave_, ctr_, ks);
            for (unsigned i = 0; i < 16; ++i) out[i] = uint8_t(in[i] ^ ks[i]);
            for (unsigned i = n; i < 16; ++i) out[i] = 0;
            if (aes_gcm()) {
                uint8_t c[16] = {0};
                std::memcpy(c, aes_cifra() ? out : in, n);
                ghash_bloque(c);
            } else {
                incrementa_ctr(ctr_, bits);
            }
        } else if (aes_cfb()) {
            uint8_t ks[16]; cifra_aes(clave_, cbc_, ks);
            for (unsigned i = 0; i < 16; ++i) out[i] = uint8_t(in[i] ^ ks[i]);
            std::memcpy(cbc_, aes_cifra() ? out : in, 16);
        } else if (aes_cbc()) {
            if (aes_cifra()) {
                uint8_t x[16];
                for (unsigned i = 0; i < 16; ++i) x[i] = uint8_t(in[i] ^ cbc_[i]);
                cifra_aes(clave_, x, out);
                std::memcpy(cbc_, out, 16);
            } else {
                descifra_aes(clave_, in, out);
                for (unsigned i = 0; i < 16; ++i) out[i] ^= cbc_[i];
                std::memcpy(cbc_, in, 16);
            }
        } else {
            if (aes_cifra()) cifra_aes(clave_, in, out); else descifra_aes(clave_, in, out);
        }
        for (unsigned i = 0; i < 4; ++i) dout_[i] = de_bytes(out + 4 * i);
        dat_hecho_ += 16;
        aes_salida_ = true;
        if (len && dat_hecho_ >= len) aes_cierra();
    }
    uint32_t aes_ctrl() const {
        uint32_t c = actrl_;
        if (aes_ctx_) c |= 0x40000000u;                 // SVCTXTRDY
        if (!aes_empezado_ || aes_ctx_) c |= 0x80000000u;   // CTXTRDY
        if (!aes_salida_ && !aes_ctx_) c |= 0x2u;          // INPUT_READY
        if (aes_salida_) c |= 0x1u;                        // OUTPUT_READY
        return c;
    }
    uint32_t aes_read(uint32_t o) {
        const int k = key1_idx(o);
        if (o < 0x20) return 0;                          // las claves no se leen
        if (k >= 0) return 0;
        if (o >= 0x40 && o < 0x50) return aiv_[(o - 0x40) / 4];
        if (o >= 0x60 && o < 0x70) {
            const unsigned i = (0x6C - o) / 4;           // la palabra 0 en 0x6C
            dout_mask_ |= 1u << i;
            if (dout_mask_ == 0xFu) { dout_mask_ = 0; aes_salida_ = false; }
            return dout_[i];
        }
        if (o >= 0x70 && o < 0x80) return tag_[(o - 0x70) / 4];
        switch (o) {
            case 0x50: return aes_ctrl();
            case 0x54: return alen0_;
            case 0x58: return alen1_;
            case 0x5C: return authlen_;
            case 0x80: return 0x00000001u;              // REVISION [modelo]
            case 0x84: return asys_;
            case 0x88: return 1u;                       // RESETDONE
            case 0x8C: return (aes_salida_ ? 0x4u : 0u) | (!aes_salida_ ? 0x2u : 0u);
            case 0x90: return airqen_;
            default: return 0;
        }
    }
    void aes_write(uint32_t o, uint32_t v) {
        if (o < 0x20) { key2_[(o / 4) ^ 0] = v; return; }
        const int k = key1_idx(o);
        if (k >= 0) { key1_[k] = v; aes_empezado_ = false; return; }
        if (o >= 0x40 && o < 0x50) { aiv_[(o - 0x40) / 4] = v; aes_empezado_ = false; return; }
        if (o >= 0x60 && o < 0x70) {
            const unsigned i = (0x6C - o) / 4;
            din_[i] = v;
            din_mask_ |= 1u << i;
            if (din_mask_ == 0xFu) { din_mask_ = 0; aes_procesa(); }
            return;
        }
        switch (o) {
            case 0x50:
                actrl_ = v & 0x21FFFFFCu;
                aes_empezado_ = false; aes_salida_ = false; aes_ctx_ = false;
                din_mask_ = dout_mask_ = 0;
                break;
            case 0x54:
                // Una longitud nueva empieza otra operacion con el contexto
                // que haya (el IV que dejo la anterior, si no se ha cambiado)
                alen0_ = v; aes_empezado_ = false; aes_ctx_ = false; aes_salida_ = false;
                din_mask_ = dout_mask_ = 0;
                break;
            case 0x58: alen1_ = v & 0x1FFFFFFFu; break;
            case 0x5C: authlen_ = v; break;
            case 0x84:
                asys_ = v & 0x1FECu;
                if (v & 0x2u) aes_reset();
                if (v & 0x1E0u) aviso("AES con uDMA: no esta modelada");
                break;
            case 0x90: airqen_ = v & 0xFu; break;
            default: break;
        }
    }

    // ======================================================================
    // DES
    // ======================================================================
    uint32_t dkey_[6] = {0}, div_[2] = {0}, dctrl_ = 0, dlen_ = 0, ddin_[2] = {0}, ddout_[2] = {0},
             dsys_ = 0, dirqen_ = 0;
    unsigned ddin_mask_ = 0, ddout_mask_ = 0;
    bool des_salida_ = false;
    uint64_t des_iv_ = 0;
    bool des_iv_vivo_ = false;

    void des_reset() {
        std::memset(dkey_, 0, sizeof dkey_); div_[0] = div_[1] = 0;
        dctrl_ = dlen_ = dsys_ = dirqen_ = 0; ddin_mask_ = ddout_mask_ = 0;
        des_salida_ = false; des_iv_vivo_ = false;
    }
    static uint64_t a64(uint32_t l, uint32_t h) {
        uint8_t b[8]; a_bytes(l, b); a_bytes(h, b + 4);
        uint64_t v = 0;
        for (unsigned i = 0; i < 8; ++i) v = (v << 8) | b[i];
        return v;
    }
    static void de64(uint64_t v, uint32_t& l, uint32_t& h) {
        uint8_t b[8];
        for (unsigned i = 0; i < 8; ++i) b[i] = uint8_t(v >> (56 - 8 * i));
        l = de_bytes(b); h = de_bytes(b + 4);
    }
    void des_procesa() {
        ++n_des_;
        const uint64_t in = a64(ddin_[0], ddin_[1]);
        // KEY1 en 0x10/0x14, KEY2 en 0x08/0x0C, KEY3 en 0x00/0x04
        des::Subclaves k1, k2, k3;
        des::expande(a64(dkey_[4], dkey_[5]), k1);
        const bool tdes = dctrl_ & 0x8u, cifra = dctrl_ & 0x4u;
        if (tdes) { des::expande(a64(dkey_[2], dkey_[3]), k2); des::expande(a64(dkey_[0], dkey_[1]), k3); }
        auto E = [&](uint64_t x) {
            return tdes ? des::bloque(k3, des::bloque(k2, des::bloque(k1, x, false), true), false)
                        : des::bloque(k1, x, false);
        };
        auto D = [&](uint64_t x) {
            return tdes ? des::bloque(k1, des::bloque(k2, des::bloque(k3, x, true), false), true)
                        : des::bloque(k1, x, true);
        };
        if (!des_iv_vivo_) { des_iv_ = a64(div_[0], div_[1]); des_iv_vivo_ = true; }
        uint64_t out;
        switch ((dctrl_ >> 4) & 3u) {
            case 1:                                       // CBC
                if (cifra) { out = E(in ^ des_iv_); des_iv_ = out; }
                else { out = D(in) ^ des_iv_; des_iv_ = in; }
                break;
            case 2:                                       // CFB de 64
                out = in ^ E(des_iv_);
                des_iv_ = cifra ? out : in;
                break;
            default:                                      // ECB
                out = cifra ? E(in) : D(in);
        }
        de64(out, ddout_[0], ddout_[1]);
        de64(des_iv_, div_[0], div_[1]);
        des_salida_ = true;
    }
    uint32_t des_read(uint32_t o) {
        switch (o) {
            case 0x18: return div_[0];
            case 0x1C: return div_[1];
            case 0x20: return dctrl_ | 0x80000000u | (des_salida_ ? 0x1u : 0x2u);
            case 0x24: return dlen_;
            case 0x28: case 0x2C: {
                const unsigned i = (o - 0x28) / 4;
                ddout_mask_ |= 1u << i;
                if (ddout_mask_ == 3u) { ddout_mask_ = 0; des_salida_ = false; }
                return ddout_[i];
            }
            case 0x30: return 0x00000001u;
            case 0x34: return dsys_;
            case 0x38: return 1u;
            case 0x3C: return des_salida_ ? 0x4u : 0x2u;
            case 0x40: return dirqen_;
            default: return 0;
        }
    }
    void des_write(uint32_t o, uint32_t v) {
        if (o < 0x18) { dkey_[o / 4] = v; return; }
        switch (o) {
            case 0x18: div_[0] = v; des_iv_vivo_ = false; break;
            case 0x1C: div_[1] = v; des_iv_vivo_ = false; break;
            case 0x20: dctrl_ = v & 0x3Cu; des_salida_ = false; ddin_mask_ = ddout_mask_ = 0;
                       des_iv_vivo_ = false; break;
            case 0x24: dlen_ = v; break;
            case 0x28: case 0x2C: {
                const unsigned i = (o - 0x28) / 4;
                ddin_[i] = v; ddin_mask_ |= 1u << i;
                if (ddin_mask_ == 3u) { ddin_mask_ = 0; des_procesa(); }
                break;
            }
            case 0x34:
                dsys_ = v & 0xECu;
                if (v & 0x2u) des_reset();
                if (v & 0xE0u) aviso("DES con uDMA: no esta modelada");
                break;
            case 0x40: dirqen_ = v & 0xFu; break;
            default: break;
        }
    }

    // ======================================================================
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x40) return dma_[off / 4];
        if (off >= CRC_O && off < CRC_O + 0x20) {
            switch (off - CRC_O) {
                case 0x00: return crcctrl_;
                case 0x10: return crc_;
                case 0x18: return crc_pp();
                default: return 0;
            }
        }
        if (off >= SHA_O && off < SHA_O + 0x200) return sha_read(off - SHA_O);
        if (off >= AES_O && off < AES_O + 0x100) return aes_read(off - AES_O);
        if (off >= DES_O && off < DES_O + 0x100) return des_read(off - DES_O);
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off < 0x40) { dma_[off / 4] = v; return; }
        if (off >= CRC_O && off < CRC_O + 0x20) {
            switch (off - CRC_O) {
                case 0x00:
                    crcctrl_ = v & 0x73BFu;
                    tcp_medio_ = false;
                    if (((v >> 13) & 3u) == 2u) crc_ = 0;
                    else if (((v >> 13) & 3u) == 3u) crc_ = 0xFFFFFFFFu;
                    break;
                case 0x10: crc_ = v; break;
                case 0x14: crc_din(v); break;
                default: break;
            }
            return;
        }
        if (off >= SHA_O && off < SHA_O + 0x200) { sha_write(off - SHA_O, v); return; }
        if (off >= AES_O && off < AES_O + 0x100) { aes_write(off - AES_O, v); return; }
        if (off >= DES_O && off < DES_O + 0x100) { des_write(off - DES_O, v); return; }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_CCM_H
