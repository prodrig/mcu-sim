// =============================================================================
// rsa_esp.h — El acelerador RSA de los ESP32-C3
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §20; hwcrypto_reg.h y mpi_ll.h de
// ESP-IDF]. Tres operaciones sobre números de n palabras de 32 bits
// (n = RSA_MODE + 1, hasta 96: 3072 bits), palabra menos significativa
// primero:
//
//   M_MEM 0x000  Z_MEM 0x200  Y_MEM 0x400  X_MEM 0x600  (96 palabras cada una)
//   M_PRIME 0x800 (M' = -M^-1 mod 2^32)  MODE 0x804  CLEAN 0x808 (1: memorias
//   listas)  MODEXP_START 0x80C  MODMULT_START 0x810  MULT_START 0x814
//   IDLE 0x818  CLEAR_INTERRUPT 0x81C  CONSTANT_TIME 0x820 (1 de reset: sin
//   acelerar)  SEARCH_ENABLE 0x824  SEARCH_POS 0x828  INTERRUPT_ENA 0x82C
//   (1 de reset)  DATE 0x830 (0x20200618; ESP-IDF la pone en 0x82C, que es
//   INTERRUPT_ENA: vale la del TRM)
//
//   MODEXP   Z = X^Y mod M, con r = R^2 mod M en Z_MEM. Deja X_MEM y Z_MEM
//            cambiadas (X en forma de Montgomery y el resultado); M, Y, M' y
//            MODE no.
//   MODMULT  Z = X·Y mod M, también con r en Z_MEM.
//   MULT     Z = X·Y (2n palabras): MODE = 2n - 1, X en X_MEM e Y en
//            Z_MEM[n..2n-1].
//
// Es Montgomery de verdad (bignum_esp.h): el resultado sale bien solo si r y
// M' están bien calculados, como en el silicio.
//
// EL TIEMPO [de modelo]: n·(n + 6) ciclos del APB por producto de Montgomery.
// Con 80 MHz da los tiempos de la tabla 20.3-1 del TRM para 3072 bits e
// Y = 65537: 753 ms sin acelerar (dato: 752,81), 4,4 ms con SEARCH (4,52) y
// 2,7 ms con CONSTANT_TIME a 0 (2,41). El producto entero, n² ciclos. IDLE
// vale 0 ese tiempo y la interrupción (fuente 47) sube al acabar, si
// INTERRUPT_ENA, hasta CLEAR_INTERRUPT.
//
// LO QUE NO SE MODELA: la inicialización de las memorias al encender
// (SYSTEM_RSA_MEM_PD; CLEAN vale 1 siempre).
// =============================================================================
#ifndef STM32_PERIPH_ESP_RSA_ESP_H
#define STM32_PERIPH_ESP_RSA_ESP_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"
#include "bignum_esp.h"

namespace stm32 {

class RsaEsp : public BusSlave {
public:
    double hz_apb = 80e6;
    std::function<void(bool)> irq;
    enum : uint32_t { M_MEM = 0x000, Z_MEM = 0x200, Y_MEM = 0x400, X_MEM = 0x600, PALABRAS = 96 };

    explicit RsaEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        reset();
        SC_HAS_PROCESS(RsaEsp);
        SC_METHOD(fin_proc);
        sensitive << ev_fin_;
        dont_initialize();
    }
    void reset() {
        for (auto* b : {m_, z_, y_, x_}) for (unsigned i = 0; i < PALABRAS; ++i) b[i] = 0;
        mp_ = modo_ = 0; cte_ = 1; busca_ = 0; pos_ = 0; ena_ = 1; date_ = 0x20200618u;
        int_ = false; fin_ = sc_core::SC_ZERO_TIME;
        if (irq) irq(false);
    }
    unsigned operaciones() const { return n_ops_; }
    bool ocupado() const { return sc_core::sc_time_stamp() < fin_; }

    // Para el DS (rsa_ds): la misma exponenciación, con los mismos tiempos,
    // sobre operandos que no pasan por las memorias del bus. Devuelve lo que
    // tarda. Mientras el DS la usa, el RSA está ocupado.
    sc_core::sc_time exp_ds(const bignum::Num& x, const bignum::Num& y, const bignum::Num& m,
                            const bignum::Num& r, uint32_t mp, unsigned n, bignum::Num& z) {
        bignum::Num xm;
        const unsigned k = bignum::modexp(x, y, m, r, mp, n, int(32 * n) - 1, false, z, xm);
        const sc_core::sc_time t((double(k) * n * (n + 6)) / hz_apb, sc_core::SC_SEC);
        fin_ = sc_core::sc_time_stamp() + t;
        ++n_ops_;
        return t;
    }

protected:
    uint32_t* mem(uint32_t off) {
        if (off >= 0x800 || (off & 0x1FFu) >= 4 * PALABRAS) return nullptr;
        uint32_t* b = off < 0x200 ? m_ : off < 0x400 ? z_ : off < 0x600 ? y_ : x_;
        return &b[(off & 0x1FFu) / 4];
    }
    uint32_t reg_read(uint32_t off) override {
        if (uint32_t* p = mem(off)) return *p;
        switch (off) {
        case 0x800: return mp_;
        case 0x804: return modo_;
        case 0x808: return 1u;
        case 0x818: return ocupado() ? 0u : 1u;
        case 0x820: return cte_;
        case 0x824: return busca_;
        case 0x828: return pos_;
        case 0x82C: return ena_;
        case 0x830: return date_;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (uint32_t* p = mem(off)) { *p = v; return; }
        switch (off) {
        case 0x800: mp_ = v; return;
        case 0x804: modo_ = v & 0x7Fu; return;
        case 0x80C: if (v & 1u) arranca(0); return;
        case 0x810: if (v & 1u) arranca(1); return;
        case 0x814: if (v & 1u) arranca(2); return;
        case 0x81C: if (v & 1u) { int_ = false; if (irq) irq(false); } return;
        case 0x820: cte_ = v & 1u; return;
        case 0x824: busca_ = v & 1u; return;
        case 0x828: pos_ = v & 0xFFFu; return;
        case 0x82C: ena_ = v & 1u; if (irq) irq(int_ && ena_); return;
        case 0x830: date_ = v & 0x3FFFFFFFu; return;
        default: return;
        }
    }

private:
    uint32_t m_[PALABRAS], z_[PALABRAS], y_[PALABRAS], x_[PALABRAS];
    uint32_t mp_ = 0, modo_ = 0, cte_ = 1, busca_ = 0, pos_ = 0, ena_ = 1, date_ = 0;
    bool int_ = false;
    unsigned n_ops_ = 0;
    sc_core::sc_time fin_;
    sc_core::sc_event ev_fin_;

    static bignum::Num num(const uint32_t* p, unsigned n) { return bignum::Num(p, p + n); }
    static void pon_num(uint32_t* p, const bignum::Num& v) { for (size_t i = 0; i < v.size(); ++i) p[i] = v[i]; }

    void arranca(unsigned op) {
        if (ocupado()) {
            SC_REPORT_WARNING("rsa", "operacion con la anterior en curso: el silicio da basura; se ignora");
            return;
        }
        double ciclos;
        if (op == 2) {
            const unsigned n = (modo_ + 1u) / 2u;
            if (n == 0 || n > PALABRAS / 2) { SC_REPORT_WARNING("rsa", "MULT: longitud fuera de rango"); return; }
            const bignum::Num z = bignum::mul(num(x_, n), num(z_ + n, n), n);
            pon_num(z_, z);
            ciclos = double(n) * n;
        } else {
            const unsigned n = modo_ + 1u;
            if (n > PALABRAS) { SC_REPORT_WARNING("rsa", "MODE: mas de 96 palabras"); return; }
            const bignum::Num x = num(x_, n), y = num(y_, n), m = num(m_, n), r = num(z_, n);
            bignum::Num z, xm;
            unsigned k;
            if (op == 1) {
                xm = bignum::mont(x, r, m, mp_, n);
                z = bignum::mont(xm, y, m, mp_, n);
                k = 2;
            } else {
                const int desde = busca_ ? int(std::min<uint32_t>(pos_, 32 * n - 1)) : int(32 * n) - 1;
                k = bignum::modexp(x, y, m, r, mp_, n, desde, cte_ != 0, z, xm);
                pon_num(x_, xm);
            }
            pon_num(z_, z);
            ciclos = double(k) * n * (n + 6);
        }
        ++n_ops_;
        const sc_core::sc_time t(ciclos / hz_apb, sc_core::SC_SEC);
        fin_ = sc_core::sc_time_stamp() + t;
        ev_fin_.notify(t);
    }
    void fin_proc() {
        if (ocupado()) return;
        int_ = true;
        if (irq) irq(ena_ != 0);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_RSA_ESP_H
