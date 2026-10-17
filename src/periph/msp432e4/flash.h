// =============================================================================
// flash.h — La Flash del MSP432E4 y su controlador (T6)
//
// LA MEMORIA: 1 MB en 0x0, en sectores de 16 KB [OO; DS]. Se lee con los
// estados de espera de MEMTIM0.FWS -los que estén EN VIGOR, es decir, los que
// se cargaron con RSCLKCFG.MEMTIMU [sysctl.h]- cada vez que el acceso cae en
// una línea de 256 bits distinta de la última, una para instrucciones y otra
// para datos: es lo que hacen los búferes de prebúsqueda [1F: P-17 f].
// FLASHCONF.FPFOFF los apaga, y entonces se paga en cada acceso.
//
// EL CONTROLADOR, en 0x400F_D000 [H; hw_flash.h; segunda fuente: el algoritmo
// de Flash del E4 que TI escribió para OpenOCD, contrib/loaders/flash/msp432,
// y flash.c de DriverLib]:
//
//   FMA, FMD, FMC   dirección, dato y orden: WRITE (una palabra), ERASE (un
//                   sector de 16 KB), MERASE (toda) y COMT (fijar BOOTCFG, los
//                   USER_REG y la protección). Cada orden lleva la llave en los
//                   16 bits altos: 0xA442 si BOOTCFG.KEY = 1 (de fábrica), o la
//                   de FLPEKEY si no. El bit de la orden sigue a 1 mientras dura
//   FMC2, FWBn      la escritura con búfer: 32 palabras en FWB0..31 y FMC2.WRBUF
//                   las programa en la fila de 128 bytes de FMA; FWBVAL dice
//                   cuáles se han escrito
//   FCRIS/FCIM/FCMISC  fin de programación, acceso a algo protegido (ARIS),
//                   dato inválido (INVDRIS: programar un 1 donde hay un 0)...
//   FLASHPP, SSIZE  1 MB en sectores de 16 KB, 256 KB de SRAM
//   FLASHCONF       la prebúsqueda y el espejo
//
// Y en el espacio del SYSCTL, aunque son de la Flash [hw_flash.h]: RMCTL (el
// alias de la ROM en 0x0 durante el arranque, que se borra escribiendo un 1),
// BOOTCFG, USER_REG0-3 y FMPRE0-15/FMPPE0-15 (protección de lectura y de
// programación, un bit por bloque de 2 KB [1F: P-17 g]). Lo que se escribe en
// ellos vale hasta el siguiente reset; con COMT se queda para siempre, como en
// el chip. «Para siempre» es la vida del modelo: no hay fichero detrás.
//
// PROGRAMAR SOLO BAJA BITS: lo que queda es lo que había Y lo nuevo. Un 1
// donde había un 0 levanta INVDRIS y no se programa. Los tiempos son de
// modelo [P-17 g]: 30 µs por palabra, 15 ms por sector, 15 ms el borrado
// total. Mientras dura una operación, leer la Flash detiene a quien lee.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_FLASH_H
#define STM32_PERIPH_MSP432E4_FLASH_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstring>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../../common/ahb_types.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class FlashCtrlE4;

// -----------------------------------------------------------------------------
// LA MEMORIA
// -----------------------------------------------------------------------------
class FlashE4 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<FlashE4> tsk{"tsk"};
    sc_core::sc_in<double> sys_hz{"sys_hz"};
    // Los estados de espera en vigor y si la prebúsqueda está apagada: los
    // dicen el SYSCTL y el controlador, que pone el SoC
    std::function<unsigned()> fws;
    FlashCtrlE4* ctrl = nullptr;

    FlashE4(sc_core::sc_module_name nm, uint32_t size)
        : sc_core::sc_module(nm), mem_(size, 0xFF) {
        tsk.register_b_transport(this, &FlashE4::bt);
        tsk.register_transport_dbg(this, &FlashE4::dbg);
    }

    uint32_t size() const { return uint32_t(mem_.size()); }
    bool es_flash(uint32_t a) const { return a < mem_.size(); }
    bool poke_byte(uint32_t a, uint8_t v) {
        if (a >= mem_.size()) return false;
        mem_[a] = v;
        return true;
    }
    uint8_t* celda(uint32_t a) { return a < mem_.size() ? &mem_[a] : nullptr; }
    uint32_t peek32(uint32_t a) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i)
            v |= uint32_t(a + i < mem_.size() ? mem_[a + i] : 0xFF) << (8 * i);
        return v;
    }
    void borra_todo() { std::fill(mem_.begin(), mem_.end(), uint8_t(0xFF)); }

    uint64_t esperas_pagadas() const { return n_esperas_; }
    uint64_t lecturas_detenidas() const { return n_detenidas_; }

private:
    std::vector<uint8_t> mem_;
    uint64_t n_esperas_ = 0, n_detenidas_ = 0;
    uint32_t linea_[2] = {~0u, ~0u};

    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t);
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (uint64_t(a) + n > mem_.size()) return 0;
        // El depurador lee; escribir la Flash es cosa del programador
        if (gp.is_read()) std::memcpy(gp.get_data_ptr(), &mem_[a], n);
        else              std::memcpy(&mem_[a], gp.get_data_ptr(), n);
        return n;
    }
};

// -----------------------------------------------------------------------------
// EL CONTROLADOR
// -----------------------------------------------------------------------------
class FlashCtrlE4 : public BusSlave {
public:
    enum : uint32_t {
        R_FMA = 0x000, R_FMD = 0x004, R_FMC = 0x008, R_FCRIS = 0x00C,
        R_FCIM = 0x010, R_FCMISC = 0x014, R_FMC2 = 0x020, R_FWBVAL = 0x030,
        R_FLPEKEY = 0x03C, R_FWB = 0x100, R_PP = 0xFC0, R_SSIZE = 0xFC4,
        R_CONF = 0xFC8, R_ROMSWMAP = 0xFCC
    };
    // Los del espacio del SYSCTL, como desplazamiento desde 0x400F_E000
    enum : uint32_t {
        S_RVP = 0x0D4, S_RMCTL = 0x0F0, S_BOOTCFG = 0x1D0, S_USERREG = 0x1E0,
        S_FMPRE = 0x200, S_FMPPE = 0x400
    };
    static constexpr uint32_t FMC_WRITE = 1u, FMC_ERASE = 2u, FMC_MERASE = 4u,
                              FMC_COMT = 8u, FMC2_WRBUF = 1u;
    static constexpr uint32_t RIS_ARIS = 1u << 0, RIS_PRIS = 1u << 1,
                              RIS_ERIS = 1u << 2, RIS_VOLT = 1u << 9,
                              RIS_INVD = 1u << 10, RIS_ERVER = 1u << 11,
                              RIS_PROGVER = 1u << 13;
    static constexpr uint32_t BOOTCFG_FABRICA = 0xFFFFFFFEu;
    // [modelo: P-17 g]
    static constexpr double T_PALABRA_S = 30e-6, T_SECTOR_S = 15e-3, T_TODO_S = 15e-3;

    sc_core::sc_out<bool> irq{"irq"};
    FlashE4* mem = nullptr;
    // El SoC: el alias de arranque, para que lo sepa el enrutado
    bool rmctl_ba = false;

    FlashCtrlE4(sc_core::sc_module_name nm, uint32_t flash_size, uint32_t sram_size)
        : BusSlave(nm, e4::FLASH_CTRL_B, 0x1000), flash_size_(flash_size),
          sram_size_(sram_size) {
        SC_HAS_PROCESS(FlashCtrlE4);
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        // Lo que no se borra con un reset: lo fijado con COMT
        bootcfg_fijo_ = BOOTCFG_FABRICA;
        userreg_fijo_.fill(0xFFFFFFFFu);
        fmpre_fijo_.fill(0xFFFFFFFFu);
        fmppe_fijo_.fill(0xFFFFFFFFu);
        valores_de_reset();
    }

    // --- Lo que mira la memoria, el SoC y el banco -------------------------------
    sc_core::sc_time ocupado_hasta() const { return ocupado_; }
    bool prebusqueda() const { return !(conf_ & (1u << 16)); }      // FPFOFF
    uint32_t bootcfg() const { return bootcfg_; }
    uint32_t userreg(unsigned n) const { return n < 4 ? userreg_[n] : 0; }
    bool protegido_programar(uint32_t a) const {
        const unsigned b = a / 0x800u;
        return b < 512 && !((fmppe_[b / 32] >> (b % 32)) & 1u);
    }
    bool protegido_leer(uint32_t a) const {
        const unsigned b = a / 0x800u;
        return b < 512 && !((fmpre_[b / 32] >> (b % 32)) & 1u);
    }
    unsigned operaciones() const { return n_ops_; }
    // Lo que marca otro: el fin de una operación de la EEPROM (ERIS)
    void marca(uint32_t ris) { fcris_ |= ris; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
    unsigned rechazadas() const { return n_rech_; }
    // Escribir la Flash por el bus no programa nada: error de bus [1F]
    void escritura_directa() {
        if (avisado_dir_) return;
        avisado_dir_ = true;
        SC_REPORT_WARNING("msp432e4/flash",
            "escritura directa en la Flash: no programa nada (error de bus). Se "
            "programa con FMA/FMD/FMC o el bufer FWBn (FlashProgram de DriverLib)");
    }

    // Los registros de la Flash que viven en el espacio del SYSCTL (los
    // atiende el SYSCTL llamando aquí). `off` desde 0x400F_E000.
    static bool es_suyo(uint32_t off) {
        return off == S_RVP || off == S_RMCTL || (off >= S_BOOTCFG && off < S_USERREG + 16) ||
               (off >= S_FMPRE && off < S_FMPRE + 64) || (off >= S_FMPPE && off < S_FMPPE + 64);
    }
    uint32_t lee_sysctl(uint32_t off) const {
        if (off == S_RVP) return mem ? mem->peek32(4) : 0xFFFFFFFFu;
        if (off == S_RMCTL) return rmctl_ba ? 1u : 0u;
        if (off == S_BOOTCFG) return bootcfg_;
        if (off >= S_USERREG && off < S_USERREG + 16) return userreg_[(off - S_USERREG) / 4];
        if (off >= S_FMPRE && off < S_FMPRE + 64) return fmpre_[(off - S_FMPRE) / 4];
        if (off >= S_FMPPE && off < S_FMPPE + 64) return fmppe_[(off - S_FMPPE) / 4];
        return 0;
    }
    void escribe_sysctl(uint32_t off, uint32_t v) {
        if (off == S_RMCTL) { if (v & 1u) rmctl_ba = false; return; }   // W1C
        // Lo que no está fijado se puede cambiar, y solo a 0 (como en el chip)
        if (off == S_BOOTCFG) { if (bootcfg_fijo_ & (1u << 31)) bootcfg_ = v | 0x7FFF0000u; return; }
        if (off >= S_USERREG && off < S_USERREG + 16) { userreg_[(off - S_USERREG) / 4] = v; return; }
        if (off >= S_FMPRE && off < S_FMPRE + 64) { auto& r = fmpre_[(off - S_FMPRE) / 4]; r &= v; return; }
        if (off >= S_FMPPE && off < S_FMPPE + 64) { auto& r = fmppe_[(off - S_FMPPE) / 4]; r &= v; return; }
    }
    // Al reset, lo fijado vuelve a valer
    void reset_fijos() {
        bootcfg_ = bootcfg_fijo_;
        userreg_ = userreg_fijo_;
        fmpre_ = fmpre_fijo_;
        fmppe_ = fmppe_fijo_;
    }

protected:
    const uint32_t flash_size_, sram_size_;
    uint32_t fma_ = 0, fmd_ = 0, fcris_ = 0, fcim_ = 0, fwbval_ = 0, flpekey_ = 0xFFFF,
             conf_ = 0;
    std::array<uint32_t, 32> fwb_{};
    uint32_t orden_ = 0, orden2_ = 0;        // la que está en curso (FMC, FMC2)
    sc_core::sc_time ocupado_ = sc_core::SC_ZERO_TIME, t_acc_;
    uint32_t bootcfg_ = BOOTCFG_FABRICA, bootcfg_fijo_;
    std::array<uint32_t, 4>  userreg_{}, userreg_fijo_;
    std::array<uint32_t, 16> fmpre_{}, fmppe_{}, fmpre_fijo_, fmppe_fijo_;
    unsigned n_ops_ = 0, n_rech_ = 0;
    bool avisado_dir_ = false, avisado_llave_ = false;
    sc_core::sc_event irq_ev_;

    void valores_de_reset() {
        fma_ = fmd_ = fcris_ = fcim_ = fwbval_ = 0;
        flpekey_ = 0xFFFF; conf_ = 0;
        fwb_.fill(0xFFFFFFFFu);
        orden_ = orden2_ = 0;
        reset_fijos();
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void irq_proc() { irq.write((fcris_ & fcim_) != 0); }
    bool ocupado() const { return t_acc_ < ocupado_; }

    uint32_t llave() const { return (bootcfg_ & (1u << 4)) ? 0xA442u : (flpekey_ & 0xFFFFu); }

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        t_acc_ = sc_core::sc_time_stamp() + t;
        BusSlave::b_transport(gp, t);
    }

    uint32_t reg_read(uint32_t off) override {
        if (off >= R_FWB && off < R_FWB + 128) return fwb_[(off - R_FWB) / 4];
        switch (off) {
            case R_FMA: return fma_;
            case R_FMD: return fmd_;
            case R_FMC: return ocupado() ? orden_ : 0u;
            case R_FCRIS: return fcris_;
            case R_FCIM: return fcim_;
            case R_FCMISC: return fcris_ & fcim_;
            case R_FMC2: return ocupado() ? orden2_ : 0u;
            case R_FWBVAL: return fwbval_;
            case R_FLPEKEY: return flpekey_;
            // PFC, FMM, DFA; EEPROM de 4 KB por sector; 16 KB; 1 MB [hw_flash.h]
            case R_PP: return 0x40000000u | 0x00100000u | 0x00040000u |
                              ((flash_size_ / 2048u) - 1u);
            case R_SSIZE: return (sram_size_ / 256u) - 1u;
            case R_CONF: return conf_;
            default: return 0;
        }
    }

    void termina(double s, uint32_t ris) {
        ocupado_ = t_acc_ + sc_core::sc_time(s, sc_core::SC_SEC);
        fcris_ |= ris;
        irq_ev_.notify(ocupado_ - sc_core::sc_time_stamp());
        ++n_ops_;
    }
    bool comprueba_llave(uint32_t v) {
        if ((v >> 16) == llave()) return true;
        ++n_rech_;
        if (!avisado_llave_) {
            avisado_llave_ = true;
            SC_REPORT_WARNING("msp432e4/flash",
                "FMC/FMC2 sin la llave (WRKEY = 0xA442 en los 16 bits altos): la "
                "orden se ignora");
        }
        return false;
    }
    void programa_palabra(uint32_t a, uint32_t d, bool& ok) {
        a &= ~3u;
        if (a >= flash_size_ || protegido_programar(a)) { fcris_ |= RIS_ARIS; ok = false; return; }
        const uint32_t viejo = mem->peek32(a);
        if (d & ~viejo) fcris_ |= RIS_INVD;          // un 1 donde ya hay un 0
        const uint32_t nuevo = viejo & d;
        for (unsigned i = 0; i < 4; ++i) mem->poke_byte(a + i, uint8_t(nuevo >> (8 * i)));
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t ben) override {
        (void)ben;
        if (off >= R_FWB && off < R_FWB + 128) {
            const unsigned k = (off - R_FWB) / 4;
            fwb_[k] = v; fwbval_ |= 1u << k;
            return;
        }
        switch (off) {
            case R_FMA: fma_ = v; return;
            case R_FMD: fmd_ = v; return;
            case R_FCIM: fcim_ = v & 0x2E07u; irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_FCMISC: fcris_ &= ~v; irq_ev_.notify(sc_core::SC_ZERO_TIME); return;
            case R_FLPEKEY: flpekey_ = v & 0xFFFFu; return;
            case R_CONF: conf_ = v & ~(1u << 20); return;          // CLRTV no se queda
            case R_FMC: {
                if (ocupado() || !mem) return;
                if (!(v & 0xFu)) return;
                if (!comprueba_llave(v)) return;
                orden_ = v & 0xFu;
                bool ok = true;
                if (v & FMC_WRITE) {
                    programa_palabra(fma_, fmd_, ok);
                    termina(T_PALABRA_S, ok ? RIS_PRIS : 0u);
                } else if (v & FMC_ERASE) {
                    const uint32_t s = fma_ & ~(e4::SECTOR - 1u);
                    bool prot = s >= flash_size_;
                    for (uint32_t a = s; a < s + e4::SECTOR && !prot; a += 0x800u)
                        prot = protegido_programar(a);
                    if (prot) { fcris_ |= RIS_ARIS; termina(0, 0); }
                    else {
                        for (uint32_t a = s; a < s + e4::SECTOR; ++a) mem->poke_byte(a, 0xFF);
                        termina(T_SECTOR_S, RIS_PRIS);
                    }
                } else if (v & FMC_MERASE) {
                    bool prot = false;
                    for (uint32_t a = 0; a < flash_size_ && !prot; a += 0x800u) prot = protegido_programar(a);
                    if (prot) { fcris_ |= RIS_ARIS; termina(0, 0); }
                    else { mem->borra_todo(); termina(T_TODO_S, RIS_PRIS); }
                } else if (v & FMC_COMT) {
                    commit(fma_, fmd_);
                    termina(T_PALABRA_S, RIS_PRIS);
                }
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case R_FMC2: {
                if (ocupado() || !mem) return;
                if (!(v & FMC2_WRBUF)) return;
                if (!comprueba_llave(v)) return;
                orden2_ = FMC2_WRBUF;
                const uint32_t fila = fma_ & ~0x7Fu;
                bool ok = true;
                unsigned n = 0;
                for (unsigned k = 0; k < 32; ++k)
                    if (fwbval_ & (1u << k)) { programa_palabra(fila + 4 * k, fwb_[k], ok); ++n; }
                fwbval_ = 0;
                fwb_.fill(0xFFFFFFFFu);
                termina(T_PALABRA_S * (n ? n : 1), ok ? RIS_PRIS : 0u);
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            default: return;
        }
    }
    // COMT: FMA dice qué [1F para las direcciones de BOOTCFG; DL para las demás]
    void commit(uint32_t fma, uint32_t fmd) {
        (void)fmd;
        if (fma == 0x75100000u) { bootcfg_fijo_ = bootcfg_ & 0x7FFFFFFFu; bootcfg_ = bootcfg_fijo_; return; }
        if (fma >= 0x80000000u && fma <= 0x80000003u) {
            const unsigned n = fma & 3u;
            userreg_fijo_[n] = userreg_[n];
            return;
        }
        // 0..15: FMPREn/FMPPEn a la vez (FlashProtectSave)
        if (fma < 16) { fmpre_fijo_[fma] = fmpre_[fma]; fmppe_fijo_[fma] = fmppe_[fma]; }
    }
    bool responds_without_clock() const override { return true; }
};

inline void FlashE4::bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
    const uint32_t a   = uint32_t(gp.get_address());
    const unsigned len = gp.get_data_length();
    if (uint64_t(a) + len > mem_.size()) {
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }
    if (gp.is_write()) {
        if (ctrl) ctrl->escritura_directa();
        gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }
    // Con una operación en curso, quien lee espera
    if (ctrl) {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp() + t;
        const sc_core::sc_time fin = ctrl->ocupado_hasta();
        if (fin > ahora) { t += fin - ahora; ++n_detenidas_; }
        if (ctrl->protegido_leer(a)) {
            const AhbExt* e = gp.get_extension<AhbExt>();
            if (!(e && e->instr)) {              // solo ejecución: leer como dato falla
                gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
                return;
            }
        }
    }
    std::memcpy(gp.get_data_ptr(), &mem_[a], len);
    const AhbExt* e = gp.get_extension<AhbExt>();
    const bool instr = e && e->instr;
    const uint32_t linea = a >> 5;
    uint32_t& ult = linea_[instr ? 0 : 1];
    const bool acierto = ctrl && ctrl->prebusqueda() && ult == linea;
    ult = linea;
    const unsigned w = fws ? fws() : 0;
    const double f = sys_hz.read();
    if (!acierto && w && f > 0.0) {
        n_esperas_ += w;
        t += sc_core::sc_time(double(w) * 1.0e12 / f, sc_core::SC_PS);
    }
    gp.set_response_status(tlm::TLM_OK_RESPONSE);
}

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_FLASH_H
