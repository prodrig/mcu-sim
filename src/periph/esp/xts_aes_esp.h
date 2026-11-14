// =============================================================================
// xts_aes_esp.h — El cifrado y descifrado de la Flash (XTS_AES) de los ESP32-C3
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §23; spi_flash_encrypted_ll.h y
// spi_flash_chip_generic.c de ESP-IDF; y, para el orden de los bytes, que el
// TRM no da, espsecure (`_flash_encryption_operation_aes_xts`) y espefuse
// (las claves de XTS se queman al revés) de esptool]. Dos bloques:
//
// EL CIFRADO MANUAL, un periférico:
//   PLAIN_0-7 0x00-0x1C · LINESIZE 0x40 (0: 16 bytes, 1: 32) · DESTINATION
//   0x44 (0: la Flash) · PHYSICAL_ADDRESS 0x48 · TRIGGER 0x4C · RELEASE 0x50 ·
//   DESTROY 0x54 · STATE 0x58 (0 libre, 1 cifrando, 2 hecho, 3 soltado) ·
//   DATE 0x5C (30 bits de lectura y escritura, 0x20200623 de reset: lo de
//   xts_aes_reg.h de ESP-IDF y del esp32c3.svd de espressif/svd, los dos
//   generados de la descripción de los registros; el TRM, y con él el QEMU de
//   Espressif, dicen 0x20200111)
// Con STATE = 3, una escritura del SPI1 en la Flash (PP o la orden de usuario
// con datos) cuyas direcciones caigan en [PHYSICAL_ADDRESS, + tamaño) escribe
// el texto cifrado y no lo que hay en W0-W15 (`cifra_escritura`): es lo que
// hace spi_flash_chip_generic_write_encrypted. Solo funciona si el SoC lo deja
// (`permitido`): en el arranque por SPI, con SYSTEM_ENABLE_SPI_MANUAL_ENCRYPT;
// en el de descarga, con SYSTEM_ENABLE_DOWNLOAD_MANUAL_ENCRYPT y sin
// EFUSE_DIS_DOWNLOAD_MANUAL_ENCRYPT. Si no, TRIGGER no hace nada.
//
// EL DESCIFRADO AUTOMÁTICO, en la caché (`descifra`): lo que lee de la Flash
// sale en claro. Lo enciende el SoC: en el arranque por SPI, con un número
// impar de bits a 1 en EFUSE_SPI_BOOT_CRYPT_CNT (como ESP-IDF; el TRM dice «el
// primero o el tercero»); en el de descarga, con
// SYSTEM_ENABLE_DOWNLOAD_G0CB_DECRYPT.
//
// EL ALGORITMO: XTS-AES-128 (IEEE 1619-2007) con unidades de datos de 128
// bytes. La clave es la del bloque de eFuse con propósito 4
// (XTS_AES_128_KEY) leída al revés (espefuse la quema dada la vuelta), o
// ceros si no hay ninguno: K1 = sus 16 primeros bytes y K2 los 16
// siguientes. Cada unidad, al revés: el bloque de 16 bytes en el
// desplazamiento o de la unidad es el bloque j = 7 - o/16 del estándar, con
// sus bytes también al revés; el «tweak», la dirección de la unidad
// (dirección & ~0x7F) en little endian.
//
// EL TIEMPO [de modelo]: 16 ciclos del APB por bloque de 16 bytes en el
// cifrado manual; el descifrado de la caché no cuesta nada aparte.
// =============================================================================
#ifndef STM32_PERIPH_ESP_XTS_AES_ESP_H
#define STM32_PERIPH_ESP_XTS_AES_ESP_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../cryp_algo.h"

namespace stm32 {

class XtsAesEsp : public BusSlave {
public:
    double hz_apb = 80e6;
    // La clave: false si no hay ningún bloque con propósito 4 (la clave es 0)
    std::function<bool(uint8_t*)> clave;
    // ¿Deja el SoC cifrar a mano?
    std::function<bool()> permitido;

    explicit XtsAesEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() {
        std::memset(plain_, 0, sizeof plain_);
        std::memset(cifr_, 0, sizeof cifr_);
        linesize_ = dest_ = dir_ = 0;
        estado_ = 0;
        date_ = 0x20200623u;
        fin_ = sc_core::SC_ZERO_TIME;
    }
    unsigned cifrados() const { return n_cifr_; }

    // El SPI1 escribe `d` en la Flash a partir de `dir`: con el texto cifrado
    // soltado, cambia los bytes que caen en su bloque
    void cifra_escritura(uint32_t dir, std::vector<uint8_t>& d) {
        if (estado() != 3) return;
        const unsigned tam = linesize_ ? 32u : 16u;
        for (size_t i = 0; i < d.size(); ++i) {
            const uint32_t a = (dir + uint32_t(i)) & 0xFFFFFFu;
            if (a >= dir_ && a < dir_ + tam) d[i] = cifr_[a - dir_];
        }
    }
    // Lo que la caché lee de la Flash (`p`, desde la dirección física `fis`),
    // en claro. `lee` trae bytes de la Flash.
    void descifra(uint32_t fis, uint8_t* p, unsigned n, const std::function<void(uint32_t, uint8_t*, unsigned)>& lee) {
        Claves k;
        prepara(k);
        for (unsigned i = 0; i < n;) {
            const uint32_t a = fis + i, b = a & ~15u;
            uint8_t blq[16];
            lee(b, blq, 16);
            xts(k, b, blq, false);
            const unsigned resto = std::min<unsigned>(n - i, 16 - (a & 15u));
            std::memcpy(p + i, blq + (a & 15u), resto);
            i += resto;
        }
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x20) return plain_[off / 4];
        switch (off) {
        case 0x40: return linesize_;
        case 0x44: return dest_;
        case 0x48: return dir_;
        case 0x58: return estado();
        case 0x5C: return date_;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t m) override {
        if (off < 0x20) {
            uint32_t& w = plain_[off / 4];
            for (unsigned k = 0; k < 4; ++k) if (m & (1u << k)) w = (w & ~(0xFFu << (8 * k))) | (v & (0xFFu << (8 * k)));
            return;
        }
        switch (off) {
        case 0x40: linesize_ = v & 1u; return;
        case 0x44: dest_ = v & 1u; return;
        case 0x48: dir_ = v & 0xFFFFFFu; return;
        case 0x4C: if (v & 1u) dispara(); return;
        case 0x50: if ((v & 1u) && estado() == 2) estado_ = 3; return;
        case 0x54: if (v & 1u) { std::memset(cifr_, 0, sizeof cifr_); estado_ = 0; } return;
        case 0x5C: date_ = v & 0x3FFFFFFFu; return;
        default: return;
        }
    }

private:
    struct Claves { ClaveAes k1, k2; };
    uint32_t plain_[8];
    uint8_t cifr_[32];
    uint32_t linesize_ = 0, dest_ = 0, dir_ = 0, estado_ = 0, date_ = 0x20200623u;
    unsigned n_cifr_ = 0;
    bool avisado_ = false;
    sc_core::sc_time fin_;

    uint32_t estado() const { return (estado_ == 2 && sc_core::sc_time_stamp() < fin_) ? 1u : estado_; }
    void prepara(Claves& c) const {
        uint8_t e[32] = {}, k[32];
        if (clave) clave(e);
        for (unsigned i = 0; i < 32; ++i) k[i] = e[31 - i];             // espefuse la quema al revés
        expande_aes(k, 16, c.k1);
        expande_aes(k + 16, 16, c.k2);
    }
    // Un bloque de 16 bytes en la dirección `a` (alineada), en el sitio
    static void xts(const Claves& c, uint32_t a, uint8_t b[16], bool cifra) {
        uint8_t tw[16] = {}, t[16], x[16];
        const uint32_t u = a & ~0x7Fu;
        for (unsigned i = 0; i < 4; ++i) tw[i] = uint8_t(u >> (8 * i));
        cifra_aes(c.k2, tw, t);
        const unsigned j = 7 - ((a & 0x7Fu) >> 4);
        for (unsigned r = 0; r < j; ++r) {                          // T · alfa^j
            unsigned acarreo = 0;
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned s = (unsigned(t[i]) << 1) | acarreo;
                acarreo = s >> 8;
                t[i] = uint8_t(s);
            }
            if (acarreo) t[0] ^= 0x87;
        }
        for (unsigned i = 0; i < 16; ++i) x[i] = uint8_t(b[15 - i] ^ t[i]);
        uint8_t y[16];
        if (cifra) cifra_aes(c.k1, x, y); else descifra_aes(c.k1, x, y);
        for (unsigned i = 0; i < 16; ++i) b[15 - i] = uint8_t(y[i] ^ t[i]);
    }
    void dispara() {
        if (estado() != 0) return;
        if (permitido && !permitido()) {
            if (!avisado_) {
                avisado_ = true;
                SC_REPORT_WARNING("xts_aes", "TRIGGER sin el cifrado manual permitido (SYSTEM_EXTERNAL_DEVICE_ENCRYPT_DECRYPT_CONTROL): no cifra");
            }
            return;
        }
        const unsigned tam = linesize_ ? 32u : 16u;
        if (dir_ % tam) SC_REPORT_WARNING("xts_aes", "PHYSICAL_ADDRESS sin alinear al tamano: se alinea");
        dir_ &= ~(tam - 1u);
        Claves c;
        prepara(c);
        for (unsigned i = 0; i < tam; ++i) cifr_[i] = uint8_t(plain_[(dir_ % 32u + i) / 4 % 8] >> (8 * ((dir_ + i) & 3u)));
        for (unsigned b = 0; b < tam; b += 16) xts(c, dir_ + b, cifr_ + b, true);
        ++n_cifr_;
        estado_ = 2;
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time(16.0 * (tam / 16) / hz_apb, sc_core::SC_SEC);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_XTS_AES_ESP_H
