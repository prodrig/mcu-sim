// =============================================================================
// p4_programador.h — Cómo programa la Flash del MSP432P4 un depurador (T2)
//
// Es lo que hace el stub de GDB cuando le llega un `load`: el `programador_nv_if`
// del P4 [common/gdb_rsp.h, T0]. Igual que el del F407, no escribe la Flash «a
// pelo» -eso no programa nada, en el silicio ni aquí-, sino que ejecuta por el
// DAP la secuencia del controlador, con los mismos registros que usa el
// algoritmo de Flash que TI escribió para OpenOCD (contrib/loaders/flash/
// msp432, BSD-3): desproteger, borrar el sector, programar en RÁFAGA y volver
// a proteger.
//
//   borrar      por sector de 4 KB: quita su bit de BANKx_*_WEPROT, pone
//               ERASE_SECTADDR, ERASE_CTLSTAT.START, espera STATUS = 3, mira
//               ADDR_ERR, CLR_STAT y vuelve a proteger
//   programar   en ráfagas de hasta cuatro líneas de 128 bits, alineadas: los
//               bytes que no son del trozo van a 0xFF, que no cambia nada
//               porque programar solo baja bits; espera BURST_STATUS = 7, mira
//               los errores y el PRG_ERR del IFG, y lee de vuelta lo escrito
//   terminar    nada: cada operación deja su sector protegido como lo encontró
//
// Sin verificación automática (AUTO_PST): una línea a medias, que GDB manda en
// dos paquetes, tiene ya programados los bytes del primero, y el relleno a
// 0xFF del segundo daría un falso error. Se verifica leyendo los bytes que se
// han escrito, que es lo que importa.
//
// Los sectores: 0-63 los de la principal (32 por banco) y 64-67 los de la
// información. El 65 es el TLV: borrarlo da ADDR_ERR, y un `load` que lo toque
// falla diciéndolo, como en la placa.
// =============================================================================
#ifndef STM32_SOC_MSP432P4_P4_PROGRAMADOR_H
#define STM32_SOC_MSP432P4_P4_PROGRAMADOR_H

#include <systemc>
#include <cstdio>
#include "p4_desc.h"
#include "../../common/gdb_rsp.h"
#include "../../periph/msp432p4/flctl.h"

namespace stm32 {

class ProgramadorP4 : public programador_nv_if {
public:
    static ProgramadorP4& el() { static ProgramadorP4 p; return p; }
    const char* nombre() const override { return "MSP432P4 (FLCTL)"; }

    // El tamaño de la principal: 256 KB en el P401R
    uint32_t main_size = 0x00040000u;

    int sector_de(uint32_t a) const override {
        if (a < main_size) return int(a / 0x1000u);
        if (a >= p4::INFO_B && a < p4::INFO_B + p4::INFO_SIZE)
            return int(main_size / 0x1000u) + int((a - p4::INFO_B) / 0x1000u);
        return -1;
    }
    uint32_t tam_sector(int) const override { return 0x1000u; }
    unsigned n_sectores() const override { return main_size / 0x1000u + 4u; }

    bool borrar(acceso_nv_if& d, uint32_t addr, uint32_t len) override {
        const uint32_t fin = addr + (len ? len : 1u);
        for (uint32_t a = addr & ~0xFFFu; a < fin; a += 0x1000u) {
            if (sector_de(a) < 0) return falla("borrar", a, "no es Flash");
            const bool info = a >= p4::INFO_B;
            Prot p = desprotege(d, a);
            d.nv_escribir(R(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_CLR_STAT);
            d.nv_escribir(R(FlctlP4::R_ERASE_SECTADDR), a);
            d.nv_escribir(R(FlctlP4::R_ERASE_CTLSTAT),
                          FlctlP4::ERASE_START | (info ? (1u << 2) : 0u));
            uint32_t st = 0;
            bool hecho = false;
            for (unsigned i = 0; i < 2000 && !hecho; ++i) {      // hasta 200 ms
                st = d.nv_leer_o(R(FlctlP4::R_ERASE_CTLSTAT));
                hecho = ((st >> 16) & 3u) == 3u;
                if (!hecho) sc_core::wait(sc_core::sc_time(100, sc_core::SC_US));
            }
            d.nv_escribir(R(FlctlP4::R_ERASE_CTLSTAT), FlctlP4::ERASE_CLR_STAT);
            protege(d, p);
            if (!hecho) return falla("borrar", a, "el borrado no termina");
            if (st & FlctlP4::ERASE_ADDR_ERR)
                return falla("borrar", a, "ADDR_ERR: sector reservado (el TLV es de TI)");
        }
        return true;
    }

    bool programar(acceso_nv_if& d, uint32_t addr, const uint8_t* dat, unsigned n,
                   unsigned& palabras) override {
        if (n == 0) return true;
        if (sector_de(addr) < 0) return falla("programar", addr, "no es Flash");
        const bool info = addr >= p4::INFO_B;
        Prot p = desprotege(d, addr);
        bool ok = true;
        uint32_t lin = addr & ~15u;
        const uint32_t fin = addr + n;
        while (ok && lin < fin) {
            // Hasta cuatro líneas seguidas, sin salirse del sector
            unsigned k = 0;
            while (k < 4 && lin + 16u * k < fin &&
                   ((lin + 16u * k) & ~0xFFFu) == (lin & ~0xFFFu)) ++k;
            for (unsigned j = 0; j < 4 * k; ++j) {
                uint32_t w = 0;
                for (unsigned b = 0; b < 4; ++b) {
                    const uint32_t a = lin + 4u * j + b;
                    const uint8_t v = (a >= addr && a < fin) ? dat[a - addr] : 0xFFu;
                    w |= uint32_t(v) << (8 * b);
                }
                d.nv_escribir(R(FlctlP4::R_PRGBRST_DATA) + 4u * j, w);
            }
            d.nv_escribir(R(FlctlP4::R_PRGBRST_CTLSTAT), FlctlP4::BRST_CLR_STAT);
            d.nv_escribir(R(FlctlP4::R_CLRIFG), FlctlP4::IFG_PRG_ERR | FlctlP4::IFG_PRGB);
            d.nv_escribir(R(FlctlP4::R_PRGBRST_STARTADDR), lin);
            d.nv_escribir(R(FlctlP4::R_PRGBRST_CTLSTAT),
                          FlctlP4::BRST_START | (info ? (1u << 1) : 0u) | (k << 3));
            uint32_t st = 0;
            bool hecho = false;
            for (unsigned i = 0; i < 400 && !hecho; ++i) {       // hasta 2 ms
                st = d.nv_leer_o(R(FlctlP4::R_PRGBRST_CTLSTAT));
                hecho = ((st >> 16) & 7u) == 7u;
                if (!hecho) sc_core::wait(sc_core::sc_time(5, sc_core::SC_US));
            }
            const uint32_t ifg = d.nv_leer_o(R(FlctlP4::R_IFG));
            d.nv_escribir(R(FlctlP4::R_PRGBRST_CTLSTAT), FlctlP4::BRST_CLR_STAT);
            if (!hecho) ok = falla("programar", lin, "la rafaga no termina");
            else if (st & FlctlP4::BRST_ADDR_ERR) ok = falla("programar", lin, "ADDR_ERR");
            else if (ifg & FlctlP4::IFG_PRG_ERR) ok = falla("programar", lin, "PRG_ERR: protegido");
            palabras += 4 * k;
            lin += 16u * k;
        }
        protege(d, p);
        if (!ok) return false;
        // Leer de vuelta lo escrito: programar solo baja bits, así que si
        // había ceros donde ahora se quieren unos, aquí se ve.
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
    static uint32_t R(uint32_t off) { return p4::FLCTL_B + off; }
    struct Prot { uint32_t reg = 0, bit = 0; bool estaba = false; };

    Prot desprotege(acceso_nv_if& d, uint32_t a) const {
        Prot p;
        if (a < main_size) {
            const uint32_t mitad = main_size / 2;
            p.reg = R(a < mitad ? FlctlP4::R_BANK0_MAIN_WEPROT : FlctlP4::R_BANK1_MAIN_WEPROT);
            p.bit = (a % mitad) / 0x1000u;
        } else {
            const uint32_t o = a - p4::INFO_B;
            p.reg = R(o < p4::INFO_SIZE / 2 ? FlctlP4::R_BANK0_INFO_WEPROT
                                            : FlctlP4::R_BANK1_INFO_WEPROT);
            p.bit = (o % (p4::INFO_SIZE / 2)) / 0x1000u;
        }
        const uint32_t v = d.nv_leer_o(p.reg);
        p.estaba = (v >> p.bit) & 1u;
        if (p.estaba) d.nv_escribir(p.reg, v & ~(1u << p.bit));
        return p;
    }
    void protege(acceso_nv_if& d, const Prot& p) const {
        if (!p.estaba) return;
        d.nv_escribir(p.reg, d.nv_leer_o(p.reg) | (1u << p.bit));
    }
    bool falla(const char* que, uint32_t a, const char* porque) {
        ++n_fallos_;
        std::printf("[gdb] MSP432P4: no se puede %s en 0x%08X: %s\n", que, unsigned(a), porque);
        std::fflush(stdout);
        return false;
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP432P4_P4_PROGRAMADOR_H
