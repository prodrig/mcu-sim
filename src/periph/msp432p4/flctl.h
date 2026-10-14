// =============================================================================
// flctl.h — La Flash del MSP432P4, su controlador (FLCTL) y el TLV
//
// T1 modela el LADO DE LECTURA, que es lo que necesita un programa para correr:
//
//   * las dos memorias, la principal (en 0x0, dos bancos) y la de información
//     (0x0020_0000, dos bancos de 8 KB), con el Device Descriptor (TLV) dentro;
//   * los ESTADOS DE ESPERA de cada banco (BANKx_RDCTL.WAIT) y los búferes de
//     lectura (BUFI/BUFD), que deciden cuánto cuesta cada acceso;
//   * el resto de registros del FLCTL, que se guardan y se leen.
//
// Lo que NO hace todavía, y lo dice: PROGRAMAR y BORRAR. Eso es T2, junto con
// el `load` de GDB. Una escritura en la Flash se ignora -no se pierde en
// silencio: avisa la primera vez- en vez de hacer como que funciona, que sería
// ser más permisivo que el silicio. [doc/analisis_msp432.md, §8 T1 y T2]
//
// EL COSTE DE UN ACCESO. La Flash lee una línea de 128 bits. Con el búfer
// encendido (BUFI para instrucciones, BUFD para datos), un acceso que cae en
// la misma línea que el anterior del mismo tipo y el mismo banco no vuelve a
// la matriz de la Flash y no paga estados de espera; uno que cae en otra línea,
// los paga. Sin búfer, los paga siempre. Es una aproximación: el silicio tiene
// además un prefetch que aquí no está. [1F: TRM, «Flash Read Buffering»; P-16 e]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_FLCTL_H
#define STM32_PERIPH_MSP432P4_FLCTL_H

#include <cstring>
#include <functional>
#include <vector>
#include <tlm_utils/simple_target_socket.h>
#include "../../common/periph_base.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

// -----------------------------------------------------------------------------
// EL FLCTL: los registros [H, FLCTL_Type]
// -----------------------------------------------------------------------------
class FlctlP4 : public BusSlave {
public:
    enum : uint32_t {
        R_POWER_STAT = 0x000, R_BANK0_RDCTL = 0x010, R_BANK1_RDCTL = 0x014,
        R_RDBRST_CTLSTAT = 0x020, R_RDBRST_STARTADDR = 0x024, R_RDBRST_LEN = 0x028,
        R_RDBRST_FAILADDR = 0x03C, R_RDBRST_FAILCNT = 0x040,
        R_PRG_CTLSTAT = 0x050, R_PRGBRST_CTLSTAT = 0x054, R_PRGBRST_STARTADDR = 0x058,
        R_PRGBRST_DATA = 0x060,          // 16 palabras, hasta 0x09C
        R_ERASE_CTLSTAT = 0x0A0, R_ERASE_SECTADDR = 0x0A4,
        R_BANK0_INFO_WEPROT = 0x0B0, R_BANK0_MAIN_WEPROT = 0x0B4,
        R_BANK1_INFO_WEPROT = 0x0C0, R_BANK1_MAIN_WEPROT = 0x0C4,
        R_BMRK_CTLSTAT = 0x0D0, R_BMRK_IFETCH = 0x0D4, R_BMRK_DREAD = 0x0D8,
        R_BMRK_CMP = 0x0DC,
        R_IFG = 0x0F0, R_IE = 0x0F4, R_CLRIFG = 0x0F8, R_SETIFG = 0x0FC,
        R_TIMCTL0 = 0x100, R_TIMCTL_FIN = 0x124
    };
    static constexpr uint32_t RDCTL_BUFI = 1u << 4, RDCTL_BUFD = 1u << 5;

    // Quien quiere saber que han cambiado los estados de espera: el vigilante
    // de topes del SoC, que avisa si no bastan para la frecuencia de MCLK.
    std::function<void()> al_cambiar;

    explicit FlctlP4(sc_core::sc_module_name nm)
        : BusSlave(nm, p4::FLCTL_B, 0x400) {
        SC_HAS_PROCESS(FlctlP4);
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }

    unsigned wait_banco(unsigned b) const { return (rdctl_[b & 1u] >> 12) & 0xFu; }
    bool     buf_i(unsigned b) const { return (rdctl_[b & 1u] & RDCTL_BUFI) != 0; }
    bool     buf_d(unsigned b) const { return (rdctl_[b & 1u] & RDCTL_BUFD) != 0; }
    // El MÁS LENTO de los dos bancos manda: el programa vive en los dos.
    unsigned wait_minimo() const {
        const unsigned a = wait_banco(0), b = wait_banco(1);
        return a < b ? a : b;
    }

protected:
    uint32_t rdctl_[2] = {0, 0};
    uint32_t regs_[0x124 / 4 + 1] = {};

    void valores_de_reset() {
        for (uint32_t& r : regs_) r = 0;
        rdctl_[0] = rdctl_[1] = 0;
        // Todo protegido contra escritura y borrado al salir del reset: hay
        // que desprotegerlo a propósito. [1F: TRM; P-16 e]
        regs_[R_BANK0_MAIN_WEPROT / 4] = 0xFFFFFFFFu;
        regs_[R_BANK1_MAIN_WEPROT / 4] = 0xFFFFFFFFu;
        regs_[R_BANK0_INFO_WEPROT / 4] = 0x3u;
        regs_[R_BANK1_INFO_WEPROT / 4] = 0x3u;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        if (al_cambiar) al_cambiar();
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            // La Flash está encendida y lista: PSTAT = activa y los cuatro
            // bits de que sus referencias están estables. [1F; P-16 e]
            case R_POWER_STAT:  return 0x0000007Bu;
            case R_BANK0_RDCTL: return rdctl_[0] | ((rdctl_[0] & 0xFu) << 16);
            case R_BANK1_RDCTL: return rdctl_[1] | ((rdctl_[1] & 0xFu) << 16);
            case R_CLRIFG: case R_SETIFG: return 0;
            default:
                return off < sizeof(regs_) ? regs_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        auto mezcla = [&](uint32_t cur) { return (cur & ~m) | (v & m); };
        switch (off) {
            case R_POWER_STAT: return;                       // solo lectura
            case R_BANK0_RDCTL: case R_BANK1_RDCTL: {
                uint32_t& r = rdctl_[off == R_BANK1_RDCTL ? 1 : 0];
                r = mezcla(r) & 0x0000F03Fu;
                if (al_cambiar) al_cambiar();
                return;
            }
            case R_CLRIFG: regs_[R_IFG / 4] &= ~(v & m); return;
            case R_SETIFG: regs_[R_IFG / 4] |= (v & m);  return;
            default:
                if (off >= R_TIMCTL0) return;                // solo lectura
                if (off < sizeof(regs_)) regs_[off / 4] = mezcla(regs_[off / 4]);
                return;
        }
    }
};

// -----------------------------------------------------------------------------
// EL TLV SINTÉTICO
//
// El Device Descriptor es lo que el firmware de TI consulta para saber qué chip
// es y, sobre todo, cómo están CALIBRADOS sus relojes y su ADC. En un chip de
// verdad lo escribe TI en fábrica, distinto en cada pieza. Aquí se rellena con
// valores TÍPICOS -inventados de forma razonable donde no hay dato público-, y
// `limitaciones()` lo dice. La ESTRUCTURA sí es la de verdad: palabra a palabra
// la de `TLV_Type` [H], con las etiquetas de driverlib/sysctl.h (CS = 3,
// FLASHCTL = 4, ADC14 = 5, REF = 8, DEVINFO = 11, DIEREC = 12, RANDNUM = 13,
// BSL = 15, fin = 0x0BD0E11D) [TI, SDK 3.40, sysctl.h].
//
// Las constantes del DCO están elegidas para que un paso de DCOTUNE mueva la
// frecuencia algo menos de un 0,1 %: con FCAL = 400 y K = 1/632,
// 1/K + 768 - FCAL = 1000. [sintético; P-16 f]
// -----------------------------------------------------------------------------
inline std::vector<uint32_t> tlv_sintetico(uint32_t device_id, uint32_t hwrev) {
    auto f = [](float x) { uint32_t u; std::memcpy(&u, &x, 4); return u; };
    std::vector<uint32_t> t;
    t.push_back(0);                                  // TLV_CHECKSUM [sintético]
    t.push_back(11); t.push_back(4);                 // DEVINFO
    t.push_back(device_id); t.push_back(hwrev);
    t.push_back(0x00000001u);                        // BCREV
    t.push_back(0x00000001u);                        // ROM_DRVLIB_REV
    t.push_back(12); t.push_back(8);                 // DIEREC
    for (int i = 0; i < 8; ++i) t.push_back(0);
    t.push_back(3); t.push_back(16);                 // CS
    t.push_back(400); t.push_back(400);              // DCOIR_FCAL_RSEL04, _RSEL5
    for (int i = 0; i < 4; ++i) t.push_back(0);
    t.push_back(f(1.0f / 632.0f)); t.push_back(f(1.0f / 632.0f));   // DCOIR_CONSTK
    t.push_back(400); t.push_back(400);              // DCOER_FCAL
    for (int i = 0; i < 4; ++i) t.push_back(0);
    t.push_back(f(1.0f / 632.0f)); t.push_back(f(1.0f / 632.0f));   // DCOER_CONSTK
    t.push_back(5); t.push_back(24);                 // ADC14
    t.push_back(0x00008000u); t.push_back(0);        // ganancia 1,0 y offset 0
    for (int i = 0; i < 16; ++i) t.push_back(0);
    for (int i = 0; i < 6; ++i) t.push_back(0);      // sensor de temperatura
    t.push_back(8); t.push_back(3);                  // REF
    t.push_back(0); t.push_back(0); t.push_back(0);
    t.push_back(4); t.push_back(2);                  // FLASHCTL
    t.push_back(5); t.push_back(50);                 // pulsos max. de programa y borrado
    t.push_back(13); t.push_back(4);                 // RANDNUM
    t.push_back(0x5EED0001u); t.push_back(0x5EED0002u);
    t.push_back(0x5EED0003u); t.push_back(0x5EED0004u);
    t.push_back(15); t.push_back(4);                 // BSL
    for (int i = 0; i < 4; ++i) t.push_back(0xFFFFFFFFu);
    t.push_back(0x0BD0E11Du);                        // TLV_END
    return t;
}

// -----------------------------------------------------------------------------
// LA MEMORIA: principal + información, con los estados de espera
// -----------------------------------------------------------------------------
class FlashP4 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<FlashP4> tsk{"tsk"};
    sc_core::sc_in<double> mclk_hz{"mclk_hz"};
    const FlctlP4* flctl = nullptr;

    FlashP4(sc_core::sc_module_name nm, uint32_t main_size,
            uint32_t device_id, uint32_t hwrev)
        : sc_core::sc_module(nm), main_(main_size, 0xFF), info_(p4::INFO_SIZE, 0xFF) {
        tsk.register_b_transport(this, &FlashP4::bt);
        tsk.register_transport_dbg(this, &FlashP4::dbg);
        const std::vector<uint32_t> t = tlv_sintetico(device_id, hwrev);
        std::memcpy(info_.data() + (p4::TLV_B - p4::INFO_B), t.data(), t.size() * 4);
    }

    uint32_t main_size() const { return uint32_t(main_.size()); }
    bool es_principal(uint32_t a) const { return a < main_.size(); }
    bool es_info(uint32_t a) const {
        return a >= p4::INFO_B && a < p4::INFO_B + p4::INFO_SIZE;
    }
    // Para el cargador y las pruebas: escribe sin pasar por el controlador,
    // que es lo que hace un programador de fábrica.
    bool poke_byte(uint32_t a, uint8_t v) {
        uint8_t* p = celda(a);
        if (!p) return false;
        *p = v;
        return true;
    }
    uint32_t peek32(uint32_t a) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t* p = const_cast<FlashP4*>(this)->celda(a + i);
            v |= uint32_t(p ? *p : 0xFF) << (8 * i);
        }
        return v;
    }
    // Cuántas veces se ha intentado escribir sin programador (T2).
    unsigned escrituras_ignoradas() const { return n_escr_; }
    // Estadística de los estados de espera pagados: lo mira el banco.
    uint64_t esperas_pagadas() const { return n_esperas_; }

private:
    std::vector<uint8_t> main_, info_;
    unsigned n_escr_ = 0;
    uint64_t n_esperas_ = 0;
    bool     avisado_ = false;
    // La última línea de 128 bits leída por cada banco y tipo de acceso.
    uint32_t linea_[2][2] = {{~0u, ~0u}, {~0u, ~0u}};

    uint8_t* celda(uint32_t a) {
        if (es_principal(a)) return &main_[a];
        if (es_info(a))      return &info_[a - p4::INFO_B];
        return nullptr;
    }
    // Banco 0 = la primera mitad de cada memoria.
    unsigned banco_de(uint32_t a) const {
        if (es_principal(a)) return a < main_.size() / 2 ? 0u : 1u;
        return (a - p4::INFO_B) < p4::INFO_SIZE / 2 ? 0u : 1u;
    }

    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t a   = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        if (!celda(a) || !celda(a + len - 1) ||
            (es_principal(a) != es_principal(a + len - 1))) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.is_write()) {
            // T2. Ignorada y dicha: un programa que escribe aquí sin programar
            // la Flash por su controlador no escribe nada en el silicio.
            ++n_escr_;
            if (!avisado_) {
                avisado_ = true;
                SC_REPORT_WARNING("msp432p4/flash",
                    "escritura en la Flash ignorada: la programacion por el "
                    "FLCTL llega en T2 (doc/analisis_msp432.md, P-16 a)");
            }
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < len; ++i) d[i] = *celda(a + i);
        // Estados de espera
        if (flctl) {
            const unsigned b = banco_de(a);
            const AhbExt* e = gp.get_extension<AhbExt>();
            const bool instr = e && e->instr;
            const bool buf = instr ? flctl->buf_i(b) : flctl->buf_d(b);
            const uint32_t linea = a >> 4;
            uint32_t& ult = linea_[b][instr ? 0 : 1];
            const bool acierto = buf && ult == linea;
            ult = linea;
            const unsigned w = flctl->wait_banco(b);
            const double f = mclk_hz.read();
            if (!acierto && w && f > 0.0) {
                n_esperas_ += w;
                t += sc_core::sc_time(double(w) * 1.0e12 / f, sc_core::SC_PS);
            }
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        for (unsigned i = 0; i < len; ++i) {
            uint8_t* p = celda(a + i);
            if (!p) return i;
            if (gp.is_read()) d[i] = *p;
            else              *p = d[i];
        }
        return len;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_FLCTL_H
