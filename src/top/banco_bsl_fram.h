// =============================================================================
// banco_bsl_fram.h — Un anfitrión del BSL de las FRAM para los bancos (M10):
// el protocolo de [BSLFRAM SLAU550AB §4.1] por UART (un PuenteSerie en
// memoria en los pines del BSL, 8E1) o por I2C (un I2cExtMaster en P1.2/P1.3
// con sus pull-ups), y la secuencia de TEST y RST.
// =============================================================================
#ifndef STM32_TOP_BANCO_BSL_FRAM_H
#define STM32_TOP_BANCO_BSL_FRAM_H
#include <systemc>
#include <memory>
#include <string>
#include <vector>
#include "parts/puente_serie.h"
#include "parts/ext_parts.h"
#include "common/imagen_fw.h"
#include <functional>

namespace banco_bsl {
using stm32::analog_net_if;

inline uint16_t crc_ccitt(const std::vector<uint8_t>& v) {
    uint16_t c = 0xFFFF;
    for (uint8_t b : v) { uint16_t x = uint16_t((c >> 8) ^ b); x ^= x >> 4; c = uint16_t((c << 8) ^ (x << 12) ^ (x << 5) ^ x); }
    return c;
}
// 0x80, NL, NH, el núcleo y su CRC
inline std::vector<uint8_t> trama(const std::vector<uint8_t>& nucleo) {
    std::vector<uint8_t> t{0x80, uint8_t(nucleo.size()), uint8_t(nucleo.size() >> 8)};
    t.insert(t.end(), nucleo.begin(), nucleo.end());
    const uint16_t k = crc_ccitt(nucleo);
    t.push_back(uint8_t(k)); t.push_back(uint8_t(k >> 8));
    return t;
}
inline std::vector<uint8_t> contrasena(uint8_t relleno) {
    std::vector<uint8_t> v{0x11};
    for (int i = 0; i < 32; ++i) v.push_back(relleno);
    return v;
}

// La secuencia de entrada [BSLFRAM §3.3.2, figura 3-2]
inline void entra(analog_net_if& rst, analog_net_if& test, int drst, int dtest) {
    using namespace sc_core;
    auto r = [&](bool v) { rst.set_drive(drst, v ? 3.3f : 0.0f, 10.0f); };
    auto t = [&](bool v) { test.set_drive(dtest, v ? 3.3f : 0.0f, 10.0f); };
    t(false); r(false); wait(1, SC_MS);
    t(true); wait(100, SC_US); t(false); wait(100, SC_US);
    t(true); wait(100, SC_US);
    r(true); wait(100, SC_US);
    t(false); wait(20, SC_MS);
}

// Por UART: rx = el TXD del BSL, tx = su RXD
struct Uart {
    std::unique_ptr<stm32::PuenteSerie> p;
    Uart(const char* nombre, analog_net_if& rx, analog_net_if& tx) {
        stm32::PuenteSerie::Config c;
        c.destino = stm32::serie::parsea("memoria");
        c.baudios = 9600;
        stm32::parsea_formato("8E1", c.formato);
        p.reset(new stm32::PuenteSerie(nombre, &rx, &tx, nullptr, nullptr, nullptr, c));
        p->set_enabled(false);                              // conectado solo en su grupo
    }
    void conecta(bool on) { p->set_enabled(on); if (on) p->set_baudios(9600); }
    // Una orden: el ACK (-1 sin respuesta) y, si se pide, el núcleo de la respuesta
    int orden(const std::vector<uint8_t>& nucleo, std::vector<uint8_t>* resp, int ms = 300) {
        using namespace sc_core;
        p->borra_recibido();
        wait(1200, SC_US);                                  // [§3.1.1: 1,2 ms tras lo último del BSL]
        const std::vector<uint8_t> t = trama(nucleo);
        p->envia(std::string(t.begin(), t.end()));
        const std::string& u = p->recibido();
        auto b = [&](size_t i) { return uint8_t(u[i]); };
        int i = 0;
        for (; i < ms * 10 && u.empty(); ++i) wait(100, SC_US);
        if (u.empty()) return -1;
        const int ack = b(0);
        if (!resp || ack) return ack;
        resp->clear();
        for (; i < ms * 10 && u.size() < 4; ++i) wait(100, SC_US);
        if (u.size() < 4 || b(1) != 0x80) return -2;
        const size_t n = size_t(b(2) | (b(3) << 8));
        for (; i < ms * 10 && u.size() < 6 + n; ++i) wait(100, SC_US);
        if (u.size() < 6 + n) return -3;
        for (size_t j = 0; j < n; ++j) resp->push_back(b(4 + j));
        if (crc_ccitt(*resp) != uint16_t(b(4 + n) | (b(5 + n) << 8))) return -4;
        return 0;
    }
    void baudios(double b) { p->set_baudios(b); }
};

// Por I2C: un maestro de 100 kHz en SDA/SCL con 4k7 a 3,3 V [BSLFRAM §4.1.2]
struct I2c {
    std::unique_ptr<stm32::Rpull> pu_scl, pu_sda;
    std::unique_ptr<stm32::I2cExtMaster> m;
    I2c(const char* nombre, analog_net_if& scl, analog_net_if& sda) {
        pu_scl.reset(new stm32::Rpull(scl, 3.3, 4700.0));
        pu_sda.reset(new stm32::Rpull(sda, 3.3, 4700.0));
        m.reset(new stm32::I2cExtMaster(nombre, scl, sda, 100e3));
        conecta(false);
    }
    void conecta(bool on) { pu_scl->set_enabled(on); pu_sda->set_enabled(on); m->set_enabled(on); }
    bool espera() {
        using namespace sc_core;
        for (int i = 0; i < 20000 && !m->done(); ++i) wait(10, SC_US);
        return m->done();
    }
    bool lee(uint8_t dir, unsigned n, std::vector<uint8_t>& v) {
        m->request_read(dir, n);
        if (!espera() || !m->addr_acked()) return false;
        v.clear();
        for (unsigned i = 0; i < n; ++i) v.push_back(m->rx(i));
        return true;
    }
    // Escribe la trama; lee el ACK y, si se pide, la respuesta (cabecera, y
    // luego el resto: tres lecturas). -1: nadie contesta a la dirección
    int orden(uint8_t dir, const std::vector<uint8_t>& nucleo, std::vector<uint8_t>* resp, int ms_proceso = 20) {
        using namespace sc_core;
        const std::vector<uint8_t> t = trama(nucleo);
        m->request_write(dir, t.data(), unsigned(t.size()));
        if (!espera() || !m->addr_acked()) return -1;
        wait(ms_proceso, SC_MS);
        std::vector<uint8_t> a, c, r;
        if (!lee(dir, 1, a)) return -1;
        if (!resp || a[0]) return a[0];
        if (!lee(dir, 3, c) || c[0] != 0x80) return -2;
        const unsigned n = unsigned(c[1] | (c[2] << 8));
        if (!lee(dir, n + 2, r)) return -3;
        resp->assign(r.begin(), r.begin() + n);
        if (crc_ccitt(*resp) != uint16_t(r[n] | (r[n + 1] << 8))) return -4;
        return 0;
    }
};

// Un programa por el BSL: RX Data Block en trozos de 240 (lo de desde
// `desde`: el ELF lleva sus cabeceras delante), y luego CRC Check de cada
// trozo. Devuelve el vector de reset y, en `pw`, los 32 bytes de 0xFFE0 (la
// contraseña que tendrá el chip).
using Orden = std::function<int(const std::vector<uint8_t>&, std::vector<uint8_t>*)>;
inline bool programa(const Orden& orden, const std::string& elf, uint32_t desde, uint16_t& entrada,
                     std::vector<uint8_t>* pw = nullptr, bool con_crc = true) {
    std::vector<stm32::TrozoFw> t;
    if (!stm32::lee_elf(elf, t, stm32::ELF_MSP430).empty() || t.empty()) return false;
    std::vector<uint8_t> img(0x10000, 0xFF);
    bool ok = true;
    entrada = 0;
    for (stm32::TrozoFw& x : t) {
        if (x.dir < desde) {                                // el ELF lleva sus cabeceras delante del código
            const size_t q = std::min<size_t>(x.datos.size(), desde - x.dir);
            x.datos.erase(x.datos.begin(), x.datos.begin() + long(q));
            x.dir += uint32_t(q);
        }
        if (x.datos.empty() || x.dir >= 0x10000) continue;
        for (size_t i = 0; i < x.datos.size() && x.dir + i < 0x10000; ++i) img[x.dir + i] = x.datos[i];
        for (size_t o = 0; ok && o < x.datos.size(); o += 240) {
            const uint32_t a = x.dir + uint32_t(o);
            const size_t n = std::min<size_t>(240, x.datos.size() - o);
            std::vector<uint8_t> c{0x10, uint8_t(a), uint8_t(a >> 8), uint8_t(a >> 16)};
            c.insert(c.end(), x.datos.begin() + long(o), x.datos.begin() + long(o + n));
            std::vector<uint8_t> r;
            ok = orden(c, &r) == 0 && r == std::vector<uint8_t>({0x3B, 0x00});
            if (!ok) break;
            if (!con_crc) continue;
            std::vector<uint8_t> d(x.datos.begin() + long(o), x.datos.begin() + long(o + n));
            std::vector<uint8_t> k{0x16, uint8_t(a), uint8_t(a >> 8), uint8_t(a >> 16), uint8_t(n), uint8_t(n >> 8)};
            const uint16_t e = crc_ccitt(d);
            ok = orden(k, &r) == 0 && r == std::vector<uint8_t>({0x3A, uint8_t(e), uint8_t(e >> 8)});
        }
    }
    entrada = uint16_t(img[0xFFFE] | (img[0xFFFF] << 8));
    if (pw) { pw->assign({0x11}); pw->insert(pw->end(), img.begin() + 0xFFE0, img.end()); }
    return ok && entrada != 0xFFFF;
}

} // namespace banco_bsl
#endif
