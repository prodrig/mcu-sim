// =============================================================================
// spi_mem.h — SPI0 y SPI1 (MSPI), los controladores de la Flash de los ESP32
//
// E4 de doc/analisis_esp32_riscv.md. Los dos son el mismo bloque `SPI_MEM`
// [TRM-C3 §25 y spi_mem_reg.h de ESP-IDF]:
//
//   SPI1  el que usa el software para hablar con la Flash: la ROM
//         (esp_rom_spiflash_*), el stub de esptool y el driver de ESP-IDF.
//         Dos maneras de lanzar una orden con SPI_MEM_CMD_REG:
//           * las ÓRDENES FIJAS (bits 31-19 y 17): READ, WREN, WRDI, RDID,
//             RDSR, WRSR, PP, SE, BE, CE, DP, RES, HPM y PE. La dirección va
//             en ADDR[23:0] y, en PP, la longitud en ADDR[31:24]; RDSR deja el
//             estado en RD_STATUS y WRSR lo toma de ahí (16 bits con WRSR_2B);
//             RDID y RES dejan su respuesta en W0;
//           * la orden de USUARIO (USR, bit 18): fases de orden (USER2), de
//             dirección (ADDR, USER1.USR_ADDR_BITLEN), de relleno
//             (USER1.USR_DUMMY_CYCLELEN), de datos que salen (W0-W15, o desde
//             W8 con USR_MOSI_HIGHPART) y de datos que entran (a W0 o a W8).
//         El bit de la orden sigue a 1 mientras dura la transacción en la
//             línea, y el software lo sondea (spimem_flash_ll_cmd_is_done).
//   SPI0  el de la caché: lee líneas de la Flash con su modo (CTRL:
//         FREAD_QIO/DIO/QUAD/DUAL, FASTRD) y su relleno (USER1). `t_lectura`
//         dice lo que tarda leer n bytes, que es el coste de un fallo de caché.
//
// EL TIEMPO. La transacción entera va a la Flash de una vez
// (parts/spi_flash.h: a nivel de transacción, no de bit), pero dura lo que
// duraría en la línea: bits de orden, de dirección, de relleno y de datos,
// cada fase con su número de líneas, al reloj de SPI_MEM_CLOCK:
// CLK_EQU_SYSCLK (bit 31) da el reloj de origen tal cual, y si no se divide
// por CLKCNT_N + 1. El reloj de origen es el del MSPI: 80 MHz con el PLL y
// el XTAL (40 MHz) sin él [TRM-C3 §6.2.3]. CS_SETUP y CS_HOLD: un ciclo cada
// uno. El tiempo de la transacción NO para a la CPU: la orden sigue
// «ocupada» y el software espera sondeando, como en el silicio.
//
// EL CIFRADO (E8): con el XTS_AES soltado, lo que el SPI1 escribe en la Flash
// lo cambia `cifra_escritura` por el texto cifrado (periph/esp/xts_aes_esp.h).
//
// Lo que no se modela: la suspensión automática (FLASH_SUS_CTRL, la que
// permite leer de la caché mientras se borra), la PSRAM (el C3 no tiene) y
// el CRC.
// =============================================================================
#ifndef STM32_PERIPH_ESP_SPI_MEM_H
#define STM32_PERIPH_ESP_SPI_MEM_H

#include <cstdint>
#include <functional>
#include <vector>
#include "../../soc/esp/regs_esp.h"
#include "../../parts/spi_flash.h"

namespace stm32 {

class SpiMemEsp : public BloqueEsp {
public:
    // Desplazamientos [spi_mem_reg.h]
    enum : uint32_t {
        CMD = 0x00, ADDR = 0x04, CTRL = 0x08, CLOCK = 0x14, USER = 0x18, USER1 = 0x1C,
        USER2 = 0x20, MOSI_DLEN = 0x24, MISO_DLEN = 0x28, RD_STATUS = 0x2C, W0 = 0x58,
        W15 = 0x94,
    };
    SpiFlashChip* flash = nullptr;
    // E8: el XTS_AES cambia por el texto cifrado lo que el SPI1 escribe en
    // la Flash (`dir`: la dirección del primer byte de `datos`)
    std::function<void(uint32_t dir, std::vector<uint8_t>& datos)> cifra_escritura;
    // El reloj de origen del MSPI [Hz]
    std::function<double()> hz_origen = [] { return 40e6; };

    template <size_t N>
    SpiMemEsp(sc_core::sc_module_name nm, uint32_t base, const RegEsp (&t)[N])
        : BloqueEsp(nm, base, 0x1000, t) {}

    void reset() { reset_regs(); fin_ = sc_core::SC_ZERO_TIME; }

    // El reloj de la línea SPI [Hz]
    double hz_spi() {
        const uint32_t c = r(CLOCK);
        const double o = hz_origen();
        if (c & (1u << 31)) return o;
        return o / double(((c >> 16) & 0xFFu) + 1u);
    }
    // Lo que tarda la caché en leer `n` bytes (SPI0): orden, dirección,
    // relleno y datos, con el modo de lectura de CTRL
    sc_core::sc_time t_lectura(unsigned n) {
        const uint32_t c = r(CTRL), u = r(USER), u1 = r(USER1);
        unsigned l_dir = 1, l_dat = 1, rel = 0;
        if (c & (1u << 24))      { l_dir = 4; l_dat = 4; }           // QIO
        else if (c & (1u << 23)) { l_dir = 2; l_dat = 2; }           // DIO
        else if (c & (1u << 20)) { l_dat = 4; }                      // QOUT
        else if (c & (1u << 14)) { l_dat = 2; }                      // DOUT
        if ((c & (1u << 13)) || (u & (1u << 29))) rel = (u1 & 0x3Fu) + 1u;
        const unsigned l_cmd = (c & (1u << 8)) ? 4 : (c & (1u << 7)) ? 2 : 1;
        const unsigned bits_dir = ((u1 >> 26) & 0x3Fu) + 1u;
        const double ciclos = 2.0 + 8.0 / l_cmd + double(bits_dir) / l_dir + rel + 8.0 * n / l_dat;
        return sc_core::sc_time(ciclos / hz_spi(), sc_core::SC_SEC);
    }
    bool ocupado() const { return sc_core::sc_time_stamp() < fin_; }
    unsigned ordenes() const { return n_ordenes_; }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off == CMD) { v = ocupado() ? cmd_ : 0u; return true; }
        return false;
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off != CMD) return false;
        v &= m;
        const uint32_t ord = v & 0xFFFE0000u;
        if (!ord) return true;
        if (ocupado()) {                                     // el silicio la ignora
            SC_REPORT_WARNING("spi_mem", "orden con la anterior en curso: se ignora");
            return true;
        }
        cmd_ = ord;
        ++n_ordenes_;
        const double ciclos = (v & (1u << 18)) ? usuario() : fija(ord);
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time(ciclos / hz_spi(), sc_core::SC_SEC);
        return true;
    }

private:
    uint32_t cmd_ = 0;
    sc_core::sc_time fin_ = sc_core::SC_ZERO_TIME;
    unsigned n_ordenes_ = 0;

    double ahora() const { return sc_core::sc_time_stamp().to_seconds(); }
    void pon_w(unsigned base_w, const std::vector<uint8_t>& d) {
        for (size_t i = 0; i < d.size() && base_w + i / 4 < 16; i += 4) {
            uint32_t w = 0;
            for (unsigned k = 0; k < 4 && i + k < d.size(); ++k) w |= uint32_t(d[i + k]) << (8 * k);
            pon(W0 + 4 * uint32_t(base_w + i / 4), w);
        }
    }
    std::vector<uint8_t> saca_w(unsigned base_w, unsigned n) const {
        std::vector<uint8_t> d(n);
        for (unsigned i = 0; i < n; ++i) {
            const unsigned w = base_w + i / 4;
            d[i] = w < 16 ? uint8_t(r(W0 + 4 * w) >> (8 * (i & 3))) : 0;
        }
        return d;
    }
    void trans(TransSpi& t, std::vector<uint8_t>& miso) {
        if (flash) flash->transaccion(t, miso, ahora());
        else miso.assign(t.n_miso, 0xFF);               // nada en la línea: MISO en alto
    }

    // Las órdenes fijas: devuelve los ciclos de reloj SPI
    double fija(uint32_t ord) {
        TransSpi t;
        std::vector<uint8_t> miso;
        const uint32_t a = r(ADDR);
        double bits = 8;
        auto dir = [&](uint32_t d) { t.con_dir = true; t.dir = d & 0xFFFFFFu; bits += 24; };
        if (ord & (1u << 31)) {                                   // READ
            t.orden = 0x03; dir(a);
            t.n_miso = std::min<unsigned>(64, ((r(MISO_DLEN) & 0x3FFu) + 1u) / 8u);
            trans(t, miso); pon_w(0, miso); bits += 8.0 * t.n_miso;
        } else if (ord & (1u << 30)) { t.orden = 0x06; trans(t, miso); }
        else if (ord & (1u << 29)) { t.orden = 0x04; trans(t, miso); }
        else if (ord & (1u << 28)) {                              // RDID
            t.orden = 0x9F; t.n_miso = 3; trans(t, miso); pon_w(0, miso); bits += 24;
        } else if (ord & (1u << 27)) {                            // RDSR
            t.orden = 0x05; t.n_miso = 1; trans(t, miso);
            pon(RD_STATUS, (r(RD_STATUS) & ~0xFFu) | miso[0]); bits += 8;
        } else if (ord & (1u << 26)) {                            // WRSR
            t.orden = 0x01;
            const uint32_t s = r(RD_STATUS);
            t.mosi.push_back(uint8_t(s));
            if (r(CTRL) & (1u << 22)) t.mosi.push_back(uint8_t(s >> 8));
            trans(t, miso); bits += 8.0 * t.mosi.size();
        } else if (ord & (1u << 25)) {                            // PP
            t.orden = 0x02; dir(a);
            const unsigned n = std::min<unsigned>(64, (a >> 24) & 0xFFu);
            t.mosi = saca_w(0, n);
            if (cifra_escritura) cifra_escritura(t.dir, t.mosi);
            trans(t, miso); bits += 8.0 * n;
        } else if (ord & (1u << 24)) { t.orden = 0x20; dir(a); trans(t, miso); }
        else if (ord & (1u << 23)) { t.orden = 0xD8; dir(a); trans(t, miso); }
        else if (ord & (1u << 22)) { t.orden = 0x60; trans(t, miso); }
        else if (ord & (1u << 21)) { t.orden = 0xB9; trans(t, miso); }
        else if (ord & (1u << 20)) {                              // RES
            t.orden = 0xAB; t.n_miso = 3; trans(t, miso); pon_w(0, miso); bits += 24;
        } else if (ord & (1u << 19)) { t.orden = 0xA3; trans(t, miso); bits += 24; }
        else if (ord & (1u << 17)) {                              // PE: borrar página (256)
            t.orden = 0xDB; dir(a); trans(t, miso);
        }
        return bits + 2;                                          // CS_SETUP y CS_HOLD
    }

    // La orden de usuario
    double usuario() {
        const uint32_t u = r(USER), u1 = r(USER1), u2 = r(USER2), c = r(CTRL);
        TransSpi t;
        std::vector<uint8_t> miso;
        double ciclos = 2;
        // Líneas de cada fase: las de escritura las da USER (FWRITE_*), las de
        // lectura CTRL (FREAD_*)
        unsigned l_dir = 1, l_mosi = 1, l_miso = 1;
        if (u & (1u << 15)) { l_dir = 4; l_mosi = 4; }
        else if (u & (1u << 14)) { l_dir = 2; l_mosi = 2; }
        else if (u & (1u << 13)) l_mosi = 4;
        else if (u & (1u << 12)) l_mosi = 2;
        if (c & (1u << 24)) { l_dir = 4; l_miso = 4; }
        else if (c & (1u << 23)) { l_dir = 2; l_miso = 2; }
        else if (c & (1u << 20)) l_miso = 4;
        else if (c & (1u << 14)) l_miso = 2;
        if (u & (1u << 31)) {
            const unsigned bl = ((u2 >> 28) & 0xFu) + 1u;
            t.orden = uint8_t(u2 & 0xFFu);
            if (bl > 8) t.orden = uint8_t((u2 & 0xFFFFu) >> (bl - 8));
            ciclos += double(bl) / ((c & (1u << 8)) ? 4 : (c & (1u << 7)) ? 2 : 1);
        }
        if (u & (1u << 30)) {
            const unsigned bl = ((u1 >> 26) & 0x3Fu) + 1u;
            t.con_dir = true;
            t.bits_dir = bl;
            t.dir = bl >= 32 ? r(ADDR) : (r(ADDR) & ((1u << bl) - 1u));
            ciclos += double(bl) / l_dir;
        }
        if (u & (1u << 29)) ciclos += (u1 & 0x3Fu) + 1u;
        if (u & (1u << 27)) {
            const unsigned n = std::min<unsigned>(64, ((r(MOSI_DLEN) & 0x3FFu) + 8u) / 8u);
            t.mosi = saca_w((u & (1u << 25)) ? 8 : 0, n);
            if (cifra_escritura && t.con_dir) cifra_escritura(t.dir & 0xFFFFFFu, t.mosi);
            ciclos += 8.0 * n / l_mosi;
        }
        if (u & (1u << 28)) {
            t.n_miso = std::min<unsigned>(64, ((r(MISO_DLEN) & 0x3FFu) + 8u) / 8u);
            ciclos += 8.0 * t.n_miso / l_miso;
        }
        trans(t, miso);
        if (t.n_miso) pon_w((u & (1u << 24)) ? 8 : 0, miso);
        return ciclos;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_SPI_MEM_H
