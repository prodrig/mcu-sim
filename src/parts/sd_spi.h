// =============================================================================
// sd_spi.h — Una tarjeta microSD (SDHC) en modo SPI, a nivel de pin (M8 de
// doc/analisis_msp430.md: la de la MSP-EXP430FR5994)
//
// La SdCard de ext_parts.h habla el bus SD (CK, CMD, D0-D3); esta habla el
// modo SPI de la misma especificación [SD Physical Layer Simplified
// Specification v6.00, cap. 7], que es el que usa una eUSCI: CS, SCK, MOSI
// (DI) y MISO (DO), modo 0 (CPOL = 0, CPHA = 0), el bit alto primero. Muestrea
// MOSI con la subida de SCK y cambia MISO con la bajada (y al bajar CS); sin
// CS, MISO en alta impedancia.
//
// Las órdenes (6 bytes: 01xxxxxx, el argumento, el CRC7, que no se mira salvo
// el de CMD0 y CMD8 [§7.2.2: en SPI el CRC solo se comprueba en esas dos, y
// aquí ni en esas]); la respuesta, tras un byte (NCR = 1):
//   CMD0  GO_IDLE_STATE       R1 = 0x01
//   CMD8  SEND_IF_COND        R7: 0x01, 00 00 01 y el patrón
//   CMD9  SEND_CSD            R1, 0xFE, el CSD v2.0 (16 bytes) y el CRC16
//   CMD10 SEND_CID            R1, 0xFE, el CID y el CRC16
//   CMD12 STOP_TRANSMISSION   R1b
//   CMD13 SEND_STATUS         R2 (0x00 0x00)
//   CMD16 SET_BLOCKLEN        R1 (solo 512)
//   CMD17 READ_SINGLE_BLOCK   R1, 0xFE, 512 bytes y el CRC16 (direcciones de bloque: SDHC)
//   CMD24 WRITE_BLOCK         R1; luego 0xFE, 512 bytes, el CRC16; la tarjeta
//                             contesta 0x05 (aceptado) y ocupa (0x00) unos bytes
//   CMD55 APP_CMD, ACMD41     SD_SEND_OP_COND: 0x01 la primera vez, 0x00 después
//   CMD58 READ_OCR            R1 y el OCR (0xC0FF8000: listo y CCS = 1)
// El CRC16 de los datos sale bien calculado (CRC-CCITT) y el del host no se
// mira. La memoria, por bloques (los que no se han escrito leen 0x00), con
// la imagen `imagen` cargada desde el bloque 0.
// =============================================================================
#ifndef STM32_PARTS_SD_SPI_H
#define STM32_PARTS_SD_SPI_H

#include <systemc>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include "part_base.h"
#include "../common/memoria_nv.h"

namespace stm32 {

SC_MODULE(SdSpi), public ExtPartBase, public memoria_nv_if {
    SdSpi(sc_core::sc_module_name nm, analog_net_if& cs, analog_net_if& sck, analog_net_if& mosi, analog_net_if& miso,
          uint32_t mb = 1024, const std::string& imagen = "")
        : sc_core::sc_module(nm), ExtPartBase("SdSpi", nm), cs_(cs), sck_(sck), mosi_(mosi), miso_(miso),
          bloques_(mb * 2048u) {
        add_ref("cs", cs); add_ref("sck", sck); add_ref("mosi", mosi); add_ref("miso", miso);
        drv_ = miso_.register_driver(basename());
        miso_.set_hiz(drv_);
        SC_HAS_PROCESS(SdSpi);
        SC_METHOD(cs_proc); sensitive << cs_.value_changed_event(); dont_initialize();
        SC_METHOD(sck_proc); sensitive << sck_.value_changed_event(); dont_initialize();
        if (!imagen.empty()) carga(imagen);
    }

    // Lo que mira el banco
    unsigned ordenes(unsigned cmd) const { auto i = n_ord_.find(cmd); return i == n_ord_.end() ? 0 : i->second; }
    unsigned bloques_escritos() const { return escritos_; }
    uint8_t byte(uint32_t a) const {
        auto i = mem_.find(a / 512u);
        return i == mem_.end() ? 0 : i->second[a % 512u];
    }

    // --- memoria_nv_if -------------------------------------------------------------
    std::vector<RegionNv> regiones_nv() const override { return {RegionNv::borrada("microSD", 0, bloques_ * 512u, 0x00)}; }
    bool lee_nv(uint32_t a, uint8_t& v) override { if (a / 512u >= bloques_) return false; v = byte(a); return true; }
    bool escribe_nv(uint32_t a, uint8_t v) override {
        if (a / 512u >= bloques_) return false;
        bloque(a / 512u)[a % 512u] = v;
        return true;
    }

private:
    analog_net_if &cs_, &sck_, &mosi_, &miso_;
    int drv_ = -1;
    const uint32_t bloques_;
    std::map<uint32_t, std::array<uint8_t, 512>> mem_;
    std::map<unsigned, unsigned> n_ord_;
    unsigned escritos_ = 0;
    // El desplazamiento
    uint8_t in_ = 0, out_ = 0xFF;
    unsigned nbits_ = 0;
    std::deque<uint8_t> cola_;
    // La orden y la escritura en curso
    uint8_t cmd_[6] = {};
    unsigned ncmd_ = 0;
    bool app_ = false, idle_ = true;
    unsigned acmd41_ = 0;
    enum { NADA, ESPERA_TOKEN, DATOS } esc_ = NADA;
    uint32_t esc_bloque_ = 0;
    unsigned esc_n_ = 0;
    std::array<uint8_t, 512> buf_{};

    static bool alto(const analog_net_if& n) { return !n.floating() && n.voltage() > 1.6f; }
    std::array<uint8_t, 512>& bloque(uint32_t b) {
        auto i = mem_.find(b);
        if (i == mem_.end()) { std::array<uint8_t, 512> z{}; i = mem_.emplace(b, z).first; }
        return i->second;
    }
    void carga(const std::string& ruta) {
        FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) { std::fprintf(stderr, "[%s] no puedo abrir la imagen %s\n", basename(), ruta.c_str()); return; }
        std::array<uint8_t, 512> b{};
        for (uint32_t n = 0; n < bloques_; ++n) {
            const size_t r = std::fread(b.data(), 1, 512, f);
            if (!r) break;
            if (r < 512) std::fill(b.begin() + long(r), b.end(), 0);
            mem_[n] = b;
        }
        std::fclose(f);
    }
    void pon_miso(bool v) { miso_.set_drive(drv_, v ? 3.3f : 0.0f, 50.0f); }
    uint8_t siguiente() {
        if (cola_.empty()) return 0xFF;
        const uint8_t b = cola_.front();
        cola_.pop_front();
        return b;
    }
    void cs_proc() {
        if (alto(cs_)) { miso_.set_hiz(drv_); return; }
        nbits_ = 0; in_ = 0;
        out_ = siguiente();
        pon_miso(out_ & 0x80);
    }
    void sck_proc() {
        if (alto(cs_)) return;
        if (alto(sck_)) {
            in_ = uint8_t((in_ << 1) | (alto(mosi_) ? 1 : 0));
            if (++nbits_ == 8) { recibe(in_); nbits_ = 0; in_ = 0; out_ = siguiente(); }
        } else pon_miso((out_ >> (7 - nbits_)) & 1u);
    }
    static uint16_t crc16(const uint8_t* d, size_t n) {
        uint16_t c = 0;
        for (size_t i = 0; i < n; ++i) {
            c ^= uint16_t(d[i] << 8);
            for (unsigned k = 0; k < 8; ++k) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
        }
        return c;
    }
    void manda_datos(const uint8_t* d, size_t n) {
        cola_.push_back(0xFF);
        cola_.push_back(0xFE);
        for (size_t i = 0; i < n; ++i) cola_.push_back(d[i]);
        const uint16_t c = crc16(d, n);
        cola_.push_back(uint8_t(c >> 8)); cola_.push_back(uint8_t(c));
    }
    uint8_t r1() const { return idle_ ? 0x01 : 0x00; }
    void recibe(uint8_t b) {
        // La escritura de un bloque, tras CMD24
        if (esc_ == ESPERA_TOKEN) { if (b == 0xFE) { esc_ = DATOS; esc_n_ = 0; } return; }
        if (esc_ == DATOS) {
            if (esc_n_ < 512) buf_[esc_n_] = b;
            if (++esc_n_ == 514) {
                if (esc_bloque_ < bloques_) { bloque(esc_bloque_) = buf_; ++escritos_; }
                esc_ = NADA;
                cola_.push_back(0x05);                       // aceptado
                for (unsigned i = 0; i < 4; ++i) cola_.push_back(0x00);   // ocupada
                cola_.push_back(0xFF);
            }
            return;
        }
        if (ncmd_ == 0 && (b & 0xC0) != 0x40) return;
        cmd_[ncmd_++] = b;
        if (ncmd_ < 6) return;
        ncmd_ = 0;
        orden(cmd_[0] & 0x3Fu, uint32_t(cmd_[1]) << 24 | uint32_t(cmd_[2]) << 16 | uint32_t(cmd_[3]) << 8 | cmd_[4]);
    }
    void orden(unsigned c, uint32_t arg) {
        ++n_ord_[app_ ? 100 + c : c];
        const bool app = app_;
        app_ = false;
        cola_.clear();
        cola_.push_back(0xFF);                                    // NCR
        switch (c) {
        case 0: idle_ = true; acmd41_ = 0; cola_.push_back(0x01); break;
        case 8: for (uint8_t x : {uint8_t(r1()), uint8_t(0), uint8_t(0), uint8_t(1), uint8_t(arg)}) cola_.push_back(x); break;
        case 9: {
            // CSD v2.0: C_SIZE = bloques/1024 - 1, READ_BL_LEN = 9
            const uint32_t cs = bloques_ / 1024u - 1u;
            const uint8_t d[16] = {0x40, 0x0E, 0x00, 0x32, 0x5B, 0x59, 0x00, uint8_t((cs >> 16) & 0x3F), uint8_t(cs >> 8),
                                   uint8_t(cs), 0x7F, 0x80, 0x0A, 0x40, 0x00, 0x01};
            cola_.push_back(r1()); manda_datos(d, 16);
            break;
        }
        case 10: {
            const uint8_t d[16] = {0x03, 'S', 'D', 'M', 'C', 'U', 'S', 'I', 0x10, 0x12, 0x34, 0x56, 0x78, 0x01, 0x6A, 0x01};
            cola_.push_back(r1()); manda_datos(d, 16);
            break;
        }
        case 12: cola_.push_back(r1()); cola_.push_back(0x00); cola_.push_back(0xFF); break;
        case 13: cola_.push_back(r1()); cola_.push_back(0x00); break;
        case 16: cola_.push_back(arg == 512 ? r1() : uint8_t(r1() | 0x40)); break;
        case 17:
            if (arg >= bloques_) { cola_.push_back(uint8_t(r1() | 0x40)); break; }       // error de dirección
            cola_.push_back(r1());
            { auto i = mem_.find(arg); std::array<uint8_t, 512> z{}; manda_datos(i == mem_.end() ? z.data() : i->second.data(), 512); }
            break;
        case 24:
            if (arg >= bloques_) { cola_.push_back(uint8_t(r1() | 0x40)); break; }
            cola_.push_back(r1());
            esc_ = ESPERA_TOKEN; esc_bloque_ = arg;
            break;
        case 41:
            if (!app) { cola_.push_back(0x05); break; }             // orden ilegal sin CMD55
            if (++acmd41_ >= 2) idle_ = false;
            cola_.push_back(r1());
            break;
        case 1: if (++acmd41_ >= 2) idle_ = false; cola_.push_back(r1()); break;
        case 55: app_ = true; cola_.push_back(r1()); break;
        case 58: for (uint8_t x : {uint8_t(r1()), uint8_t(idle_ ? 0x40 : 0xC0), uint8_t(0xFF), uint8_t(0x80), uint8_t(0x00)}) cola_.push_back(x); break;
        default: cola_.push_back(uint8_t(r1() | 0x04)); break;      // orden ilegal
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_SD_SPI_H
