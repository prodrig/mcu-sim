// =============================================================================
// e4_programador.h — Cómo programa la Flash del MSP432E4 un depurador (T6)
//
// El `programador_nv_if` del E4 [common/gdb_rsp.h]: lo que hace el stub de GDB
// con un `load`, ejecutado por el DAP con los registros del controlador
// [periph/msp432e4/flash.h], en el mismo orden que el algoritmo de Flash que
// TI escribió para OpenOCD (contrib/loaders/flash/msp432/main_msp432e4x.c,
// BSD-3) y que FlashErase/FlashProgram de DriverLib:
//
//   borrar      por sector de 16 KB: FCMISC a cero, FMA, FMC = llave | ERASE,
//               esperar a que ERASE baje, mirar ARIS (protegido)
//   programar   por filas de 128 bytes con el búfer: FMA = fila, FWBn las
//               palabras del trozo (lo que no es del trozo, a 0xFF, que no
//               cambia nada porque programar solo baja bits), FMC2 = llave |
//               WRBUF, esperar, mirar ARIS e INVDRIS; y al final leer de vuelta
//   terminar    nada
//
// La llave: 0xA442 si BOOTCFG.KEY = 1, que es como sale de fábrica, o la de
// FLPEKEY si no [hw_flash.h].
// =============================================================================
#ifndef STM32_SOC_MSP432E4_E4_PROGRAMADOR_H
#define STM32_SOC_MSP432E4_E4_PROGRAMADOR_H

#include <systemc>
#include <cstdio>
#include "e4_desc.h"
#include "../../common/gdb_rsp.h"
#include "../../periph/msp432e4/flash.h"

namespace stm32 {

class ProgramadorE4 : public programador_nv_if {
public:
    static ProgramadorE4& el() { static ProgramadorE4 p; return p; }
    const char* nombre() const override { return "MSP432E4 (FLASH_CTRL)"; }

    uint32_t flash_size = 0x00100000u;

    int sector_de(uint32_t a) const override {
        return a < flash_size ? int(a / e4::SECTOR) : -1;
    }
    uint32_t tam_sector(int) const override { return e4::SECTOR; }
    unsigned n_sectores() const override { return flash_size / e4::SECTOR; }

    bool borrar(acceso_nv_if& d, uint32_t addr, uint32_t len) override {
        const uint32_t k = llave(d);
        const uint32_t fin = addr + (len ? len : 1u);
        for (uint32_t a = addr & ~(e4::SECTOR - 1u); a < fin; a += e4::SECTOR) {
            if (sector_de(a) < 0) return falla("borrar", a, "no es Flash");
            d.nv_escribir(R(FlashCtrlE4::R_FCMISC), 0xFFFFFFFFu);
            d.nv_escribir(R(FlashCtrlE4::R_FMA), a);
            d.nv_escribir(R(FlashCtrlE4::R_FMC), k | FlashCtrlE4::FMC_ERASE);
            if (!espera(d, FlashCtrlE4::R_FMC, FlashCtrlE4::FMC_ERASE, 400, 100))
                return falla("borrar", a, "el borrado no termina");
            if (d.nv_leer_o(R(FlashCtrlE4::R_FCRIS)) & FlashCtrlE4::RIS_ARIS)
                return falla("borrar", a, "ARIS: el sector esta protegido (FMPPE)");
        }
        return true;
    }

    bool programar(acceso_nv_if& d, uint32_t addr, const uint8_t* dat, unsigned n,
                   unsigned& palabras) override {
        if (n == 0) return true;
        if (sector_de(addr) < 0 || sector_de(addr + n - 1) < 0)
            return falla("programar", addr, "no es Flash");
        const uint32_t k = llave(d);
        const uint32_t fin = addr + n;
        for (uint32_t fila = addr & ~0x7Fu; fila < fin; fila += 0x80u) {
            d.nv_escribir(R(FlashCtrlE4::R_FCMISC), 0xFFFFFFFFu);
            d.nv_escribir(R(FlashCtrlE4::R_FMA), fila);
            for (unsigned j = 0; j < 32; ++j) {
                const uint32_t w0 = fila + 4u * j;
                if (w0 + 4 <= addr || w0 >= fin) continue;
                uint32_t w = 0;
                for (unsigned b = 0; b < 4; ++b) {
                    const uint32_t a = w0 + b;
                    const uint8_t v = (a >= addr && a < fin) ? dat[a - addr] : 0xFFu;
                    w |= uint32_t(v) << (8 * b);
                }
                d.nv_escribir(R(FlashCtrlE4::R_FWB) + 4u * j, w);
                ++palabras;
            }
            d.nv_escribir(R(FlashCtrlE4::R_FMC2), k | FlashCtrlE4::FMC2_WRBUF);
            if (!espera(d, FlashCtrlE4::R_FMC2, FlashCtrlE4::FMC2_WRBUF, 400, 50))
                return falla("programar", fila, "la escritura no termina");
            const uint32_t ris = d.nv_leer_o(R(FlashCtrlE4::R_FCRIS));
            if (ris & FlashCtrlE4::RIS_ARIS) return falla("programar", fila, "ARIS: protegido (FMPPE)");
        }
        for (uint32_t a = addr & ~3u; a < fin; a += 4) {
            const uint32_t w = d.nv_leer_o(a);
            for (unsigned b = 0; b < 4; ++b) {
                const uint32_t x = a + b;
                if (x < addr || x >= fin) continue;
                if (uint8_t(w >> (8 * b)) != dat[x - addr])
                    return falla("programar", x, "lo leido no es lo escrito (sin borrar?)");
            }
        }
        return true;
    }

    void terminar(acceso_nv_if&) override {}
    unsigned fallos() const { return n_fallos_; }

private:
    unsigned n_fallos_ = 0;
    static uint32_t R(uint32_t off) { return e4::FLASH_CTRL_B + off; }
    static uint32_t llave(acceso_nv_if& d) {
        const uint32_t bootcfg = d.nv_leer_o(e4::SYSCTL_B + FlashCtrlE4::S_BOOTCFG);
        const uint32_t k = (bootcfg & (1u << 4)) ? 0xA442u
                         : (d.nv_leer_o(R(FlashCtrlE4::R_FLPEKEY)) & 0xFFFFu);
        return k << 16;
    }
    static bool espera(acceso_nv_if& d, uint32_t reg, uint32_t bit, unsigned n, unsigned us) {
        for (unsigned i = 0; i < n; ++i) {
            if (!(d.nv_leer_o(R(reg)) & bit)) return true;
            sc_core::wait(sc_core::sc_time(us, sc_core::SC_US));
        }
        return false;
    }
    bool falla(const char* que, uint32_t a, const char* porque) {
        ++n_fallos_;
        std::printf("[gdb] MSP432E4: no se puede %s en 0x%08X: %s\n", que, unsigned(a), porque);
        std::fflush(stdout);
        return false;
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP432E4_E4_PROGRAMADOR_H
