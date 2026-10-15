// =============================================================================
// aes256.h — El acelerador AES256 del MSP432P4 (fase T3)
//
// El AES del MSP430FR, con claves de 128, 192 y 256 bits [H: AES256_Type;
// TRM SLAU356 cap. 10; segunda fuente: DriverLib aes256.c, que es la que dice
// la secuencia]:
//
//   CTL0   OP (0 cifrar, 1 descifrar con la clave de cifrado, 2 generar la
//          primera clave de descifrado, 3 descifrar con ella), KL, CM, SWRST,
//          RDYIFG, ERRFG, RDYIE, CMEN
//   STAT   BUSY, KEYWR, DINWR, DOUTRD y los tres contadores de bytes
//   KEY, DIN, XDIN, XIN de escritura; DOUT de lectura; por bytes o medias
//   palabras, el byte bajo primero: en el orden del vector de bytes
//
// LA SECUENCIA. La clave entra por KEY y KEYWR sube al completarla (16, 24 o
// 32 bytes). Con OP = 2 escribirla GENERA la clave de descifrado, que
// tarda 52 ciclos de MCLK [DriverLib: «~52 MCLK»]. Los datos entran por DIN y,
// con el 16.º byte y la clave lista, empieza la operación: BUSY 168, 204 o 234
// ciclos según la clave [DriverLib: «~167 MCLK»; las otras dos, del TRM: 1F],
// y al acabar RDYIFG (con la interrupción si RDYIE) y DOUT lleno. KEYWR también
// se puede poner por software -como hace DriverLib- para usar la clave que ya
// está. XDIN hace XOR con el estado (la última salida) y empieza; XIN hace el
// XOR sin empezar. Escribir mientras BUSY levanta ERRFG.
//
// LA CLAVE DE DESCIFRADO que genera OP = 2 se guarda como las Nk últimas
// palabras de la expansión de la clave; OP = 3 la «invierte» para descifrar.
// Es una forma de decirlo: el silicio no la deja leer, y lo que importa -que
// cifrar con OP = 0 tras generarla da otra cosa, y que OP = 3 descifra- es
// igual.
//
// LO QUE NO ESTÁ: los modos de cifrado con DMA (CMEN, CBC/OFB/CFB, BLKCNT) y
// las peticiones a la uDMA (AESTRIGGER0-2): se guardan y avisan. [P-16 u]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_AES256_H
#define STM32_PERIPH_MSP432P4_AES256_H

#include <cstring>
#include "bus16.h"
#include "../cryp_algo.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class Aes256P4 : public Bus16P4 {
public:
    sc_core::sc_out<bool> irq{"irq"};
    enum : uint16_t {
        OP = 0x0003, KL = 0x000C, CM = 0x0060, SWRST = 0x0080, RDYIFG = 0x0100,
        ERRFG = 0x0800, RDYIE = 0x1000, CMEN = 0x8000,
        BUSY = 0x0001, KEYWR = 0x0002, DINWR = 0x0004, DOUTRD = 0x0008
    };

    explicit Aes256P4(sc_core::sc_module_name nm) : Bus16P4(nm, p4::AES256_B, 0x400) {
        SC_HAS_PROCESS(Aes256P4);
        SC_METHOD(fin_proc); sensitive << fin_ev_; dont_initialize();
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }
    uint64_t bloques() const { return n_bloques_; }
    unsigned avisos() const { return n_avisos_; }

protected:
    uint16_t ctl0_ = 0, ctl1_ = 0;
    uint8_t clave_[32] = {}, din_[16] = {}, estado_[16] = {};
    unsigned n_clave_ = 0, n_din_ = 0, n_dout_ = 0;
    bool keywr_ = false, busy_ = false, pendiente_ = false;
    bool clave_desc_ = false;          // la clave del registro es la de descifrado
    enum class Tarea { Nada, Bloque, Clave } tarea_ = Tarea::Nada;
    sc_core::sc_event fin_ev_, irq_ev_;
    uint64_t n_bloques_ = 0;
    unsigned n_avisos_ = 0;
    bool av_cm_ = false;

    void valores_de_reset() {
        ctl0_ = ctl1_ = 0;
        std::memset(clave_, 0, sizeof clave_); std::memset(din_, 0, sizeof din_);
        std::memset(estado_, 0, sizeof estado_);
        n_clave_ = n_din_ = n_dout_ = 0;
        keywr_ = busy_ = pendiente_ = clave_desc_ = false;
        tarea_ = Tarea::Nada;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        fin_ev_.cancel();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void irq_proc() { irq.write((ctl0_ & RDYIFG) && (ctl0_ & RDYIE)); }

    unsigned nbytes_clave() const {
        switch ((ctl0_ >> 2) & 3u) { case 0: return 16; case 1: return 24; default: return 32; }
    }
    unsigned ciclos() const {
        switch ((ctl0_ >> 2) & 3u) { case 0: return 168; case 1: return 204; default: return 234; }
    }

    // --- La clave de descifrado: las Nk últimas palabras de la expansión ------
    static void palabras(const uint8_t* k, unsigned nb, uint8_t w[60][4]) {
        const uint8_t* S = cajas_aes().s;
        const unsigned nk = nb / 4, total = 4 * (nk + 7);
        for (unsigned i = 0; i < nk; ++i) for (unsigned j = 0; j < 4; ++j) w[i][j] = k[4 * i + j];
        uint8_t rcon = 1;
        for (unsigned i = nk; i < total; ++i) {
            uint8_t t[4] = {w[i-1][0], w[i-1][1], w[i-1][2], w[i-1][3]};
            if (i % nk == 0) {
                const uint8_t x = t[0];
                t[0] = uint8_t(S[t[1]] ^ rcon); t[1] = S[t[2]]; t[2] = S[t[3]]; t[3] = S[x];
                rcon = xtime(rcon);
            } else if (nk > 6 && i % nk == 4) {
                for (unsigned j = 0; j < 4; ++j) t[j] = S[t[j]];
            }
            for (unsigned j = 0; j < 4; ++j) w[i][j] = uint8_t(w[i - nk][j] ^ t[j]);
        }
    }
    static void a_descifrado(const uint8_t* k, unsigned nb, uint8_t* out) {
        uint8_t w[60][4];
        palabras(k, nb, w);
        const unsigned nk = nb / 4, total = 4 * (nk + 7);
        for (unsigned i = 0; i < nk; ++i)
            for (unsigned j = 0; j < 4; ++j) out[4 * i + j] = w[total - nk + i][j];
    }
    // Y al revés: de las Nk últimas palabras, la clave
    static void de_descifrado(const uint8_t* d, unsigned nb, uint8_t* out) {
        const uint8_t* S = cajas_aes().s;
        const unsigned nk = nb / 4, total = 4 * (nk + 7);
        uint8_t w[60][4];
        for (unsigned i = 0; i < nk; ++i)
            for (unsigned j = 0; j < 4; ++j) w[total - nk + i][j] = d[4 * i + j];
        uint8_t rc[16]; rc[1] = 1;
        for (unsigned r = 2; r < 16; ++r) rc[r] = xtime(rc[r - 1]);
        for (unsigned i = total - 1; i >= nk; --i) {
            uint8_t t[4] = {w[i-1][0], w[i-1][1], w[i-1][2], w[i-1][3]};
            if (i % nk == 0) {
                const uint8_t x = t[0];
                t[0] = uint8_t(S[t[1]] ^ rc[i / nk]); t[1] = S[t[2]]; t[2] = S[t[3]]; t[3] = S[x];
            } else if (nk > 6 && i % nk == 4) {
                for (unsigned j = 0; j < 4; ++j) t[j] = S[t[j]];
            }
            for (unsigned j = 0; j < 4; ++j) w[i - nk][j] = uint8_t(w[i][j] ^ t[j]);
        }
        for (unsigned i = 0; i < nk; ++i) for (unsigned j = 0; j < 4; ++j) out[4 * i + j] = w[i][j];
    }

    // --- Las operaciones -----------------------------------------------------------
    void empieza(Tarea t, unsigned n_ciclos) {
        busy_ = true; tarea_ = t;
        fin_ev_.notify(cycles(n_ciclos));
    }
    void intenta_bloque() {
        if (busy_ || n_din_ < 16 || !keywr_) return;
        empieza(Tarea::Bloque, ciclos());
    }
    void fin_proc() {
        busy_ = false;
        const unsigned nb = nbytes_clave();
        if (tarea_ == Tarea::Clave) {
            uint8_t d[32];
            a_descifrado(clave_, nb, d);
            std::memcpy(clave_, d, nb);
            clave_desc_ = true;
            keywr_ = true;
        } else if (tarea_ == Tarea::Bloque) {
            const unsigned op = ctl0_ & OP;
            uint8_t k[32];
            std::memcpy(k, clave_, nb);
            // Con la clave del registro tal como está: si es la de descifrado,
            // OP = 3 la invierte; OP = 0 y 1 la usan como clave sin más
            if (op == 3) de_descifrado(clave_, nb, k);
            ClaveAes ke;
            expande_aes(k, nb, ke);
            if (op == 0) cifra_aes(ke, din_, estado_);
            else         descifra_aes(ke, din_, estado_);
            n_din_ = 0; n_dout_ = 0;
            ++n_bloques_;
            ctl0_ |= RDYIFG;
            irq_ev_.notify(sc_core::SC_ZERO_TIME);
        }
        tarea_ = Tarea::Nada;
    }

    uint16_t lee16(uint32_t off, bool dbg) override {
        switch (off) {
            case 0x00: return ctl0_;
            case 0x02: return ctl1_;
            case 0x04: return uint16_t((busy_ ? BUSY : 0) | (keywr_ ? KEYWR : 0) |
                                       (n_din_ >= 16 ? DINWR : 0) | (n_dout_ >= 16 ? DOUTRD : 0) |
                                       ((n_clave_ & 0xFu) << 4) | ((n_din_ & 0xFu) << 8) |
                                       ((n_dout_ & 0xFu) << 12));
            case 0x0A: {                                               // DOUT
                if (busy_) return 0;
                const unsigned i = n_dout_ & 15u;
                const uint16_t v = uint16_t(estado_[i] | (estado_[(i + 1) & 15u] << 8));
                if (!dbg) {
                    n_dout_ += 2;
                    if (ctl0_ & RDYIFG) { ctl0_ &= ~RDYIFG; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
                }
                return v;
            }
            default: return 0;
        }
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        auto bytes = [&](uint8_t* dst, unsigned& n, unsigned tope, bool xorea) {
            for (unsigned k = 0; k < 2; ++k) {
                if (!(m & (0xFFu << (8 * k)))) continue;
                const uint8_t b = uint8_t(v >> (8 * k));
                if (n < tope) { dst[n] = xorea ? uint8_t(dst[n] ^ b) : b; }
                ++n;
            }
        };
        switch (off) {
            case 0x00: {
                const uint16_t w = uint16_t((ctl0_ & ~m) | (v & m));
                if (w & SWRST) { valores_de_reset(); fin_ev_.cancel(); irq_ev_.notify(sc_core::SC_ZERO_TIME); return; }
                if ((w & CMEN) && !av_cm_) {
                    av_cm_ = true; ++n_avisos_;
                    SC_REPORT_WARNING("msp432p4/aes256", "AESCMEN (los modos de cifrado con "
                        "la uDMA) no se modela: el AES va bloque a bloque");
                }
                if (((w ^ ctl0_) & KL) && !busy_) { n_clave_ = 0; keywr_ = false; }
                if (busy_) {                       // con BUSY solo cambian los flags
                    ctl0_ = uint16_t((ctl0_ & ~(RDYIFG | ERRFG | RDYIE)) | (w & (RDYIFG | ERRFG | RDYIE)));
                } else {
                    ctl0_ = uint16_t(w & ~SWRST);
                }
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x02: ctl1_ = uint16_t((ctl1_ & ~m) | (v & m & 0xFFu)); return;
            case 0x04:                                                  // STAT
                if ((v & m) & KEYWR) { keywr_ = true; intenta_bloque(); }
                return;
            case 0x06: {                                                // KEY
                if (busy_) { ctl0_ |= ERRFG; return; }
                // El primer byte de una clave nueva invalida la anterior; al
                // completarla, KEYCNT vuelve a 0 y KEYWR sube
                if (n_clave_ == 0) { keywr_ = false; clave_desc_ = false; }
                bytes(clave_, n_clave_, 32, false);
                ctl0_ &= ~RDYIFG;
                if (n_clave_ >= nbytes_clave()) {
                    n_clave_ = 0;
                    const unsigned op = ctl0_ & OP;
                    if (op == 2) empieza(Tarea::Clave, 52);
                    else { keywr_ = true; clave_desc_ = (op == 3); }
                }
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x08: case 0x0C: case 0x0E: {                          // DIN, XDIN, XIN
                if (busy_) { ctl0_ |= ERRFG; return; }
                if (n_din_ >= 16) n_din_ = 0;
                if (off == 0x08) bytes(din_, n_din_, 16, false);
                else {
                    // XOR con el estado (la última salida)
                    if (n_din_ == 0) std::memcpy(din_, estado_, 16);
                    bytes(din_, n_din_, 16, true);
                }
                ctl0_ &= ~RDYIFG;
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                if (n_din_ >= 16) {
                    if (off == 0x0E) { std::memcpy(estado_, din_, 16); n_dout_ = 0; }   // XIN: sin empezar
                    else intenta_bloque();
                }
                return;
            }
            default: return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_AES256_H
