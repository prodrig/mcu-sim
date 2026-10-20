// =============================================================================
// f3_programador.h — Cómo programa la Flash del STM32F303 un depurador (S2)
//
// El `programador_nv_if` del F303 [common/gdb_rsp.h, T0]: lo que hace el stub
// de GDB cuando le llega un `load`. Como el del F407, no escribe la Flash «a
// pelo» -eso no programa nada, en el silicio ni aquí-, sino que ejecuta por el
// DAP la secuencia del FPEC [mem/flash_f1.h], la misma que el driver stm32f1x
// de OpenOCD cuando no usa su algoritmo en la SRAM:
//
//   borrar      por página de 2 KB: PER, AR, PER|STRT, esperar BSY, mirar
//               WRPRTERR; y CR a cero
//   programar   PG y media palabra a media palabra (CSW.Size = 01: el FPEC da
//               error de bus con una palabra), esperando BSY y mirando PGERR y
//               WRPRTERR; los bytes que no son del trozo van a 0xFF. Al final
//               lee de vuelta lo escrito
//   terminar    LOCK
//
// Las llaves solo se escriben si CR está cerrado: una llave con CR abierto lo
// bloquearía hasta el siguiente reset [RM0316 §4.2.2].
// =============================================================================
#ifndef STM32_SOC_STM32F3_F3_PROGRAMADOR_H
#define STM32_SOC_STM32F3_F3_PROGRAMADOR_H

#include <systemc>
#include <cstdio>
#include "../../common/gdb_rsp.h"

namespace stm32 {

class ProgramadorF3 : public programador_nv_if {
public:
    static ProgramadorF3& el() { static ProgramadorF3 p; return p; }
    const char* nombre() const override { return "STM32F303 (FPEC)"; }

    static constexpr uint32_t FLASH_R = 0x40022000u;
    static constexpr uint32_t F_KEYR = 0x04, F_SR = 0x0C, F_CR = 0x10, F_AR = 0x14;
    static constexpr uint32_t SR_BSY = 1u << 0, SR_PGERR = 1u << 2, SR_WRPERR = 1u << 4,
                              SR_EOP = 1u << 5;
    static constexpr uint32_t CR_PG = 1u << 0, CR_PER = 1u << 1, CR_STRT = 1u << 6,
                              CR_LOCK = 1u << 7;
    static constexpr uint32_t BASE = 0x08000000u, PAGINA = 0x800u;

    // El tamaño de la Flash: 256 KB en el F303VC
    uint32_t tam = 0x00040000u;

    int sector_de(uint32_t a) const override {
        if (a < BASE || a >= BASE + tam) return -1;
        return int((a - BASE) / PAGINA);
    }
    uint32_t tam_sector(int) const override { return PAGINA; }
    unsigned n_sectores() const override { return tam / PAGINA; }

    bool borrar(acceso_nv_if& d, uint32_t addr, uint32_t len) override {
        abre(d);
        if (!espera(d, 100)) return falla("borrar", addr, "el FPEC sigue ocupado");
        d.nv_escribir(R(F_SR), SR_PGERR | SR_WRPERR | SR_EOP);
        const uint32_t fin = addr + (len ? len : 1u);
        for (uint32_t a = addr & ~(PAGINA - 1u); a < fin; a += PAGINA) {
            if (sector_de(a) < 0) { d.nv_escribir(R(F_CR), 0); return falla("borrar", a, "no es Flash"); }
            d.nv_escribir(R(F_CR), CR_PER);
            d.nv_escribir(R(F_AR), a);
            d.nv_escribir(R(F_CR), CR_PER | CR_STRT);
            const bool hecho = espera(d, 100);               // hasta 100 ms
            const uint32_t sr = d.nv_leer_o(R(F_SR));
            d.nv_escribir(R(F_SR), SR_PGERR | SR_WRPERR | SR_EOP);
            if (!hecho) { d.nv_escribir(R(F_CR), 0); return falla("borrar", a, "el borrado no termina"); }
            if (sr & SR_WRPERR) { d.nv_escribir(R(F_CR), 0); return falla("borrar", a, "WRPRTERR: pagina protegida"); }
        }
        d.nv_escribir(R(F_CR), 0);
        return true;
    }

    bool programar(acceso_nv_if& d, uint32_t addr, const uint8_t* dat, unsigned n,
                   unsigned& palabras) override {
        if (n == 0) return true;
        if (sector_de(addr) < 0) return falla("programar", addr, "no es Flash");
        abre(d);
        if (!espera(d, 100)) return falla("programar", addr, "el FPEC sigue ocupado");
        d.nv_escribir(R(F_SR), SR_PGERR | SR_WRPERR | SR_EOP);
        d.nv_escribir(R(F_CR), CR_PG);
        const uint32_t fin = addr + n;
        bool ok = true;
        for (uint32_t a = addr & ~1u; ok && a < fin; a += 2) {
            uint16_t h = 0;
            for (unsigned b = 0; b < 2; ++b) {
                const uint32_t x = a + b;
                const uint8_t v = (x >= addr && x < fin) ? dat[x - addr] : 0xFFu;
                h |= uint16_t(v << (8 * b));
            }
            if (h == 0xFFFFu) continue;                  // borrado ya es 0xFFFF
            if (!d.nv_escribir16(a, h)) {
                ok = falla("programar", a, "el transporte no sabe escribir medias palabras");
                break;
            }
            ++palabras;
            if (!espera_us(d, 1000)) { ok = falla("programar", a, "la programacion no termina"); break; }
            const uint32_t sr = d.nv_leer_o(R(F_SR));
            if (sr & (SR_PGERR | SR_WRPERR)) {
                d.nv_escribir(R(F_SR), SR_PGERR | SR_WRPERR | SR_EOP);
                ok = falla("programar", a, (sr & SR_WRPERR) ? "WRPRTERR: pagina protegida"
                                                           : "PGERR: no estaba borrada");
            }
        }
        d.nv_escribir(R(F_SR), SR_EOP);
        d.nv_escribir(R(F_CR), 0);
        if (!ok) return false;
        // Leer de vuelta lo escrito
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

    void terminar(acceso_nv_if& d) override {
        d.nv_escribir(R(F_CR), d.nv_leer_o(R(F_CR)) | CR_LOCK);
    }

    unsigned fallos() const { return n_fallos_; }

private:
    unsigned n_fallos_ = 0;
    static uint32_t R(uint32_t off) { return FLASH_R + off; }
    void abre(acceso_nv_if& d) {
        if (!(d.nv_leer_o(R(F_CR)) & CR_LOCK)) return;      // ya abierta
        d.nv_escribir(R(F_KEYR), 0x45670123u);
        d.nv_escribir(R(F_KEYR), 0xCDEF89ABu);
    }
    // BSY, en pasos de 1 ms (borrados) o de 10 us (medias palabras)
    bool espera(acceso_nv_if& d, unsigned ms) {
        for (unsigned i = 0; i <= ms; ++i) {
            if (!(d.nv_leer_o(R(F_SR)) & SR_BSY)) return true;
            sc_core::wait(sc_core::sc_time(1, sc_core::SC_MS));
        }
        return false;
    }
    bool espera_us(acceso_nv_if& d, unsigned us) {
        for (unsigned i = 0; i <= us / 10; ++i) {
            if (!(d.nv_leer_o(R(F_SR)) & SR_BSY)) return true;
            sc_core::wait(sc_core::sc_time(10, sc_core::SC_US));
        }
        return false;
    }
    bool falla(const char* que, uint32_t a, const char* por) {
        ++n_fallos_;
        std::fprintf(stderr, "  [flash F303] %s en 0x%08X: %s\n", que, unsigned(a), por);
        return false;
    }
};

} // namespace stm32
#endif // STM32_SOC_STM32F3_F3_PROGRAMADOR_H
