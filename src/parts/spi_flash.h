// =============================================================================
// spi_flash.h — Una Flash SPI NOR (W25Q, GD25Q): la pieza `SpiFlash`
//
// E4 de doc/analisis_esp32_riscv.md, §4. La memoria de programa de los ESP32,
// dentro del encapsulado (ESP32-C3FH4) o en la placa (ESP32-C3), y la que un
// día colgará del QUADSPI del F446.
//
// SE MODELA A NIVEL DE TRANSACCIÓN, NO DE BIT. Un controlador (el SPI0/SPI1
// del C3, periph/esp/spi_mem.h) le da a la Flash una transacción entera
// -orden, dirección, ciclos de relleno, datos que salen- y recibe los datos
// que vuelven. Llevar bit a bit por los pads una línea de caché de 32 bytes a
// 80 MHz costaría cientos de sucesos por instrucción, y nada del firmware
// depende de ver los flancos. Lo que SÍ sale de la Flash es el TIEMPO: lo
// que tarda cada transacción (lo calcula el controlador con su reloj y sus
// líneas) y lo que tardan programar y borrar (aquí, con los tiempos típicos de
// la hoja de datos), durante los que el bit WIP del estado está a 1.
//
// Las órdenes [hojas de datos de W25Q32JV y GD25Q32C]:
//   9F RDID, 90 REMS, AB RES (y salir de DP), 4B el ID único, 5A SFDP;
//   05/35/15 leer el estado 1/2/3, 01 escribirlo (1 o 2 bytes), 31/11
//   escribir el 2/3, 06 WREN, 04 WRDI, 50 escribir el estado volátil;
//   03 READ, 0B FAST, 3B DOUT, BB DIO, 6B QOUT, EB QIO (todas leen lo mismo:
//   el modo solo cambia el tiempo, que lo cuenta el controlador);
//   02 PP (y 32 QPP), 20 SE 4 KB, 52 BE 32 KB, D8 BE 64 KB, C7/60 CE;
//   B9 DP, 75/B0 suspender, 7A/30 reanudar, 66/99 reset, A3 HPM, FF salir de
//   modo continuo.
// Programar solo pone ceros (AND), y nada de esto va sin WEL (WREN antes),
// que se baja al acabar. Lo que NO se modela: la protección por BP/CMP/SRP
// (se guarda en el estado y no protege; lo dice `limitaciones`), los
// registros de seguridad (OTP) y el modo continuo (lo que pide el bit M de
// los modos DIO/QIO: el controlador del C3 no lo usa por omisión).
//
// La pieza en una placa: CS, CLK, DI (IO0), DO (IO1), WP (IO2) y HOLD (IO3) a
// los pads del controlador. El chip la encuentra por el nodo de su CS
// (`RegistroFlashSpi`). Con `contenido=` se guarda entre simulaciones.
// =============================================================================
#ifndef STM32_PARTS_SPI_FLASH_H
#define STM32_PARTS_SPI_FLASH_H

#include <systemc>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "part_base.h"
#include "../common/memoria_nv.h"

namespace stm32 {

// --- Los modelos -------------------------------------------------------------------
struct ModeloFlashSpi {
    const char* nombre;
    uint8_t     jedec[3];        // fabricante, tipo, capacidad
    uint32_t    tam;             // bytes
    // Tiempos típicos [s]
    double      t_pp, t_se, t_be32, t_be64, t_ce, t_wrsr;
};
inline const ModeloFlashSpi MODELOS_FLASH_SPI[] = {
    {"W25Q16",  {0xEF, 0x40, 0x15}, 2u << 20,  0.4e-3, 45e-3, 120e-3, 150e-3, 5.0, 10e-3},
    {"W25Q32",  {0xEF, 0x40, 0x16}, 4u << 20,  0.4e-3, 45e-3, 120e-3, 150e-3, 10.0, 10e-3},
    {"W25Q64",  {0xEF, 0x40, 0x17}, 8u << 20,  0.4e-3, 45e-3, 120e-3, 150e-3, 20.0, 10e-3},
    {"W25Q128", {0xEF, 0x40, 0x18}, 16u << 20, 0.4e-3, 45e-3, 120e-3, 150e-3, 40.0, 10e-3},
    {"GD25Q16", {0xC8, 0x40, 0x15}, 2u << 20,  0.6e-3, 50e-3, 150e-3, 250e-3, 7.0, 2e-3},
    {"GD25Q32", {0xC8, 0x40, 0x16}, 4u << 20,  0.6e-3, 50e-3, 150e-3, 250e-3, 10.0, 2e-3},
    {"GD25Q64", {0xC8, 0x40, 0x17}, 8u << 20,  0.6e-3, 50e-3, 150e-3, 250e-3, 20.0, 2e-3},
};
inline const ModeloFlashSpi* busca_modelo_flash(const std::string& n) {
    for (const ModeloFlashSpi& m : MODELOS_FLASH_SPI) if (n == m.nombre) return &m;
    return nullptr;
}
inline std::string modelos_flash_spi() {
    std::string s;
    for (const ModeloFlashSpi& m : MODELOS_FLASH_SPI) s += (s.empty() ? "" : ", ") + std::string(m.nombre);
    return s;
}

// Una transacción, como la ve la Flash
struct TransSpi {
    uint8_t  orden = 0;
    bool     con_dir = false;
    uint32_t dir = 0;                    // ya alineada a la derecha
    unsigned bits_dir = 24;
    std::vector<uint8_t> mosi;           // lo que sale del controlador tras dir/relleno
    unsigned n_miso = 0;                 // bytes que se leen
};

// --- El chip ------------------------------------------------------------------
class SpiFlashChip : public memoria_nv_if {
public:
    explicit SpiFlashChip(const ModeloFlashSpi& m, std::string nombre = "Flash SPI")
        : m_(m), nombre_(std::move(nombre)), mem_(m.tam, 0xFF) {}

    const ModeloFlashSpi& modelo() const { return m_; }
    uint32_t tam() const { return m_.tam; }
    // Contenido directo (cargar imágenes, el banco)
    uint8_t* datos() { return mem_.data(); }
    void escribe_directo(uint32_t a, const uint8_t* d, size_t n) {
        for (size_t i = 0; i < n; ++i) mem_[(a + i) % m_.tam] = d[i];
    }
    void borra_todo() { std::fill(mem_.begin(), mem_.end(), 0xFF); }
    // Los tiempos de programar y borrar, por un factor (1: los de la hoja de
    // datos). `--flash-rapida` los deja en el 1 %: un depurador de fuera
    // (OpenOCD) programa con plazos de reloj de pared, y la simulación no va a
    // tiempo real (E6)
    void pon_escala_tiempos(double e) { escala_tiempos_ = e; }
    double escala_tiempos() const { return escala_tiempos_; }
    uint8_t sr(unsigned i) const { return sr_[i]; }
    bool ocupada(double ahora) const { return ahora < fin_ocupada_; }
    // Lectura de memoria directa (la de la caché por SPI0): lo que devuelve un
    // READ. Si está programando o borrando, la Flash no contesta datos (lee
    // 0xFF) salvo que esté suspendida.
    void lee(uint32_t a, uint8_t* d, size_t n, double ahora) {
        if (ocupada(ahora) && !suspendida_) { std::memset(d, 0xFF, n); ++lecturas_ocupada_; return; }
        for (size_t i = 0; i < n; ++i) d[i] = mem_[(a + i) % m_.tam];
    }
    unsigned lecturas_ocupada() const { return lecturas_ocupada_; }

    // Una transacción completa. `ahora` es el tiempo simulado [s]. Devuelve
    // los bytes leídos en `miso`.
    void transaccion(const TransSpi& t, std::vector<uint8_t>& miso, double ahora) {
        actualiza(ahora);
        miso.assign(t.n_miso, 0xFF);
        ++ordenes_[t.orden];
        if (dormida_ && t.orden != 0xAB) return;                 // en DP solo oye RES
        const bool wip = sr_[0] & 1u;
        auto lee_n = [&](uint32_t a) { lee(a, miso.data(), miso.size(), ahora); };
        switch (t.orden) {
        case 0x9F: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = i < 3 ? m_.jedec[i] : 0; break;
        case 0x90: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = m_.jedec[(i & 1) ? 2 : 0] - ((i & 1) ? 0x01 : 0); break;
        case 0xAB:
            dormida_ = false;
            for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = uint8_t(m_.jedec[2] - 1);
            break;
        case 0x4B: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = uint8_t(0xC3 ^ (i * 0x11)); break;
        case 0x5A: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = 0xFF; break;     // SFDP: no
        case 0x05: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = sr_[0]; break;
        case 0x35: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = sr_[1]; break;
        case 0x15: for (unsigned i = 0; i < t.n_miso; ++i) miso[i] = sr_[2]; break;
        case 0x06: if (!wip) sr_[0] |= 2u; break;
        case 0x04: if (!wip) sr_[0] &= uint8_t(~2u); break;
        case 0x50: vol_ = !wip; break;
        case 0x01: case 0x31: case 0x11: {
            if (wip || (!(sr_[0] & 2u) && !vol_)) break;
            const unsigned k0 = t.orden == 0x01 ? 0 : t.orden == 0x31 ? 1 : 2;
            for (size_t i = 0; i < t.mosi.size() && k0 + i < 3; ++i) {
                uint8_t v = t.mosi[i];
                if (k0 + i == 0) v &= 0xFC;                                  // WIP y WEL no
                sr_[k0 + i] = v;
            }
            if (!vol_) ocupa(ahora, m_.t_wrsr);
            vol_ = false;
            break;
        }
        case 0x03: case 0x0B: case 0x3B: case 0xBB: case 0x6B: case 0xEB: case 0xE7: case 0xE3:
            lee_n(t.dir);
            break;
        case 0x02: case 0x32: {
            if (wip || !(sr_[0] & 2u)) break;
            const uint32_t pag = t.dir & ~0xFFu;
            for (size_t i = 0; i < t.mosi.size(); ++i) {
                const uint32_t a = pag | ((t.dir + uint32_t(i)) & 0xFFu);   // da la vuelta en la página
                mem_[a % m_.tam] &= t.mosi[i];
            }
            ++programas_;
            ocupa(ahora, m_.t_pp);
            break;
        }
        case 0x20: borra(t.dir, 4096, m_.t_se, ahora); break;
        case 0x52: borra(t.dir, 32768, m_.t_be32, ahora); break;
        case 0xD8: borra(t.dir, 65536, m_.t_be64, ahora); break;
        case 0xC7: case 0x60: borra(0, m_.tam, m_.t_ce, ahora); break;
        case 0xB9: if (!wip) dormida_ = true; break;
        case 0x75: case 0xB0:
            if (wip && !suspendida_) { suspendida_ = true; resto_ = fin_ocupada_ - ahora; fin_ocupada_ = 0;
                                       sr_[0] &= uint8_t(~1u); sr_[1] |= 0x80u; }
            break;
        case 0x7A: case 0x30:
            if (suspendida_) { suspendida_ = false; sr_[1] &= uint8_t(~0x80u); ocupa(ahora, resto_); }
            break;
        case 0x66: reset_hab_ = true; return;
        case 0x99:
            if (reset_hab_) { sr_[0] &= 0x7C; fin_ocupada_ = 0; suspendida_ = false; dormida_ = false; }
            break;
        default: break;              // A3 HPM, FF y las desconocidas: nada
        }
        reset_hab_ = false;
    }

    std::map<uint8_t, unsigned> ordenes() const { return ordenes_; }
    unsigned programas() const { return programas_; }
    unsigned borrados() const { return borrados_; }

    // --- memoria_nv_if: toda la Flash, desde 0, borrada a 0xFF -------------------
    std::vector<RegionNv> regiones_nv() const override { return {RegionNv::borrada(nombre_, 0, m_.tam)}; }
    bool lee_nv(uint32_t a, uint8_t& v) override { if (a >= m_.tam) return false; v = mem_[a]; return true; }
    bool escribe_nv(uint32_t a, uint8_t v) override { if (a >= m_.tam) return false; mem_[a] = v; return true; }

private:
    const ModeloFlashSpi& m_;
    std::string nombre_;
    std::vector<uint8_t> mem_;
    uint8_t sr_[3] = {0, 0, 0x60};
    double fin_ocupada_ = 0, resto_ = 0;
    bool dormida_ = false, suspendida_ = false, vol_ = false, reset_hab_ = false;
    unsigned programas_ = 0, borrados_ = 0, lecturas_ocupada_ = 0;
    std::map<uint8_t, unsigned> ordenes_;

    void actualiza(double ahora) {
        if ((sr_[0] & 1u) && ahora >= fin_ocupada_) sr_[0] &= uint8_t(~3u);   // acaba: WIP y WEL a 0
    }
    double escala_tiempos_ = 1.0;
    void ocupa(double ahora, double t) { sr_[0] |= 1u; fin_ocupada_ = ahora + t * escala_tiempos_; }
    void borra(uint32_t a, uint32_t n, double t, double ahora) {
        if ((sr_[0] & 1u) || !(sr_[0] & 2u)) return;
        const uint32_t b = (a % m_.tam) & ~(n - 1);
        std::fill(mem_.begin() + b, mem_.begin() + std::min<uint32_t>(b + n, m_.tam), uint8_t(0xFF));
        ++borrados_;
        ocupa(ahora, t);
    }
};

// --- Quién está en cada CS -------------------------------------------------------
// La pieza se apunta aquí con el nodo de su CS; el chip que la controla la
// busca con el nodo de su pad SPICS0.
class RegistroFlashSpi {
public:
    static std::map<const analog_net_if*, SpiFlashChip*>& mapa() {
        static std::map<const analog_net_if*, SpiFlashChip*> m;
        return m;
    }
    static void apunta(const analog_net_if* cs, SpiFlashChip* f) { mapa()[cs] = f; }
    static void quita(SpiFlashChip* f) {
        for (auto i = mapa().begin(); i != mapa().end();) i = i->second == f ? mapa().erase(i) : std::next(i);
    }
    static SpiFlashChip* busca(const analog_net_if* cs) {
        auto i = mapa().find(cs);
        return i == mapa().end() ? nullptr : i->second;
    }
};

// --- La pieza ------------------------------------------------------------------
class SpiFlash : public ExtPartBase {
public:
    SpiFlash(const char* nombre, const ModeloFlashSpi& m, analog_net_if& cs, analog_net_if* clk,
             analog_net_if* di, analog_net_if* dout, analog_net_if* wp, analog_net_if* hold)
        : ExtPartBase("SpiFlash", nombre), chip_(m, std::string("Flash ") + m.nombre) {
        add_ref("cs", cs);
        add_ref_opt("clk", clk); add_ref_opt("di", di); add_ref_opt("do", dout);
        add_ref_opt("wp", wp); add_ref_opt("hold", hold);
        RegistroFlashSpi::apunta(&cs, &chip_);
    }
    ~SpiFlash() override { RegistroFlashSpi::quita(&chip_); }
    SpiFlashChip& chip() { return chip_; }
    memoria_nv_if* memoria_nv() override { return &chip_; }

private:
    SpiFlashChip chip_;
};

} // namespace stm32
#endif // STM32_PARTS_SPI_FLASH_H
