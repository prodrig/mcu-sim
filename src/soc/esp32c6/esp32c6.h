// =============================================================================
// esp32c6.h — EL ESP32-C6 (fase E9 de doc/analisis_esp32_riscv.md)
//
// El núcleo genérico de src/rv con RV_ESP32C6_HP (E9.1) y lo que Espressif
// puso alrededor. Esta entrega (E9.2) es el chip en «arranque directo», como
// el C3 de E3: el firmware se carga en la HP SRAM y el núcleo arranca en su
// punto de entrada, sin la ROM; la ROM de verdad (rev0, la de todos los C6)
// está en su sitio y se lee, pero arrancar por ella, la Flash y la caché son
// de la entrega siguiente.
//
//   memoria    la HP SRAM (512 KB, 0x4080_0000) es una sola para los buses I y
//              D [TRM-C6 §5.3]: se ejecuta y se lee por la misma dirección, en
//              bytes o en palabras. La LP SRAM (16 KB, 0x5000_0000), igual. La
//              ROM en 0x4000_0000-0x4004_FFFF.
//   interrup.  la matriz (77 fuentes) y el INTPRI, con el CLINT de Espressif en
//              0x2000_1800 (soc/esp/intc_esp_v2.h); el núcleo tiene mie/mip y
//              delega a U con mideleg.
//   PMA        16 entradas en los CSR 0xBC0-0xBDF (soc/esp/pma_esp.h): cada
//              acceso del núcleo pasa por ella (los del depurador, no).
//   reloj      el PCR (c6_sistema.h): XTAL de 40 MHz tras el reset; con el
//              PLL, la CPU a 160 (o 120, 80...) y el AHB y el APB a 40
//   periféric. UART0/1 (la UART con la disposición del C6), SYSTIMER, TIMG0/1,
//              GPIO con la matriz e IO_MUX sobre pads eléctricos, la GPIO
//              dedicada por los CSR, LP_AON (STORE, resets por software),
//              LP_CLKRST (la causa), y guardando con sus valores de reset: PMU,
//              HP_SYSTEM, eFuse, SPI0/1, EXTMEM, LP_WDT, LP_TIMER, I2C_ANA_MST,
//              LPPERI, LP_ANALOG, las APM y los TEE, ASSIST_DEBUG y MEM_MONITOR.
//              El resto, DECLARADOS (leen cero y avisan; E9.4).
//   reset      CHIP_PU: el reset de chip, y los cinco pines de arranque
//              (GPIO8, GPIO9, MTMS, MTDI y GPIO15) se guardan al soltarlo.
//              Resets del sistema y de la CPU por software (LP_AON) con su
//              causa en LP_CLKRST.
//   CSR        mpcer/mpcmr/mpccr (0x7E0-0x7E2), cpu_gpio_oen/in/out
//              (0x803-0x805) y la PMA
// =============================================================================
#ifndef STM32_SOC_ESP32C6_ESP32C6_H
#define STM32_SOC_ESP32C6_ESP32C6_H

#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#  error "compila con -DSC_INCLUDE_DYNAMIC_PROCESSES"
#endif
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <memory>
#include <string>
#include <vector>
#include "c6_desc.h"
#include "c6_sistema.h"
#include "../esp/intc_esp_v2.h"
#include "../esp/pma_esp.h"
#include "../esp/esp_rom.h"
#include "../../rv/hart/nucleo_rv.h"
#include "../../periph/esp/uart_esp.h"
#include "../../periph/esp/systimer_esp.h"
#include "../../periph/esp/timg_esp.h"
#include "../../periph/esp/gpio_esp.h"
#include "../../periph/bloque_declarado.h"
#include "../../common/imagen_fw.h"
#include "../../common/analog_net.h"

namespace stm32 {

// La ROM de verdad (esp32c6_rev0_rom.elf): soc/esp32c6/rom/gen_rom_c6.py
namespace c6 {
inline const uint32_t ROM_REV0[] = {
#include "rom/rom_c6_rev0.inc"
};
} // namespace c6

// Los CSR propios del C6 HP [TRM-C6 §1.3, tabla 1.3-1]: los contadores de
// rendimiento de máquina (mpcer/mpcmr/mpccr, 0x7E0-0x7E2; sin la copia de
// usuario del C3), la GPIO dedicada (0x803-0x805) y la PMA (0xBC0-0xBDF).
class CsrC6 : public rv_csr_ext_if {
public:
    PmaEsp pma;
    explicit CsrC6(GpioEsp& g) : g_(g) { reset(); }
    bool atiende(uint16_t a) const override {
        return (a >= 0x7E0 && a <= 0x7E2) || (a >= 0x803 && a <= 0x805) || PmaEsp::es_suyo(a);
    }
    bool lee(RvHart& h, uint16_t a, uint32_t& v) override {
        if (h.modo() != RvModo::M) return false;
        if (PmaEsp::es_suyo(a)) return pma.lee(a, v);
        switch (a) {
        case 0x7E0: v = er_; return true;
        case 0x7E1: v = mr_; return true;
        case 0x7E2: v = cuenta(h); return true;
        case 0x803: v = g_.cpu_oen(); return true;
        case 0x804: v = g_.cpu_gpio_in(); return true;
        case 0x805: v = g_.cpu_out(); return true;
        default: return false;
        }
    }
    bool escribe(RvHart& h, uint16_t a, uint32_t v) override {
        if (h.modo() != RvModo::M) return false;
        if (PmaEsp::es_suyo(a)) { pma.escribe(a, v); return true; }
        switch (a) {
        case 0x7E0: base_ = cuenta(h); er_ = v & 0x7FFu; desde_ = fuente(h); return true;
        case 0x7E1: base_ = cuenta(h); mr_ = v & 3u; desde_ = fuente(h); return true;
        case 0x7E2: base_ = v; desde_ = fuente(h); return true;
        case 0x803: g_.cpu_gpio_oen(uint8_t(v)); return true;
        case 0x804: return true;                         // solo de lectura
        case 0x805: g_.cpu_gpio_out(uint8_t(v)); return true;
        default: return false;
        }
    }
    // Como en el C3: salen del reset contando ciclos [deducido de ESP-IDF,
    // que lee mpccr sin habilitarlo]. La PMA, a su reset.
    void reset() { er_ = 1; mr_ = 1; base_ = 0; desde_ = 0; pma.reset(); }
private:
    GpioEsp& g_;
    uint32_t er_ = 1, mr_ = 1, base_ = 0;
    uint64_t desde_ = 0;
    uint64_t fuente(RvHart& h) const { return (er_ & 1u) ? h.ciclos() : h.instret(); }
    uint32_t cuenta(RvHart& h) const {
        if (!(mr_ & 1u) || !er_) return base_;
        return base_ + uint32_t(fuente(h) - desde_);
    }
};

class Esp32C6 : public sc_core::sc_module {
public:
    const McuCapsC6& mcu;

    // --- Relojes y resets -----------------------------------------------------------
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_cpu{"hz_cpu"}, hz_apb{"hz_apb"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_cpu_n{"rst_cpu_n"};
    sc_core::sc_signal<bool> rst_per_n{"rst_per_n"}, siempre{"siempre"}, reloj_falso{"reloj_falso"};

    // --- El bus y el núcleo -----------------------------------------------------------
    tlm_utils::multi_passthrough_target_socket<Esp32C6> bus{"bus"};
    NucleoRv core;

    // --- Periféricos (el orden importa: gpio antes de las UART) ---------------------
    GpioEsp        gpio;
    IoMuxEsp       iomux;
    MatrizIntEspV2 intmtx;
    IntPriEsp      intpri;
    ClintEsp       clint{"clint"};
    PcrC6          pcr;
    LpAonC6        lp_aon;
    LpClkRstC6     lp_clkrst;
    UartEsp        uart0, uart1;
    SystimerEsp    systimer;
    TimgEsp        timg0, timg1;
    BloqueEsp      pmu, hp_sys, efuse, spi0, spi1, extmem, lp_wdt, lp_timer, i2c_ana_mst, lpperi,
                   lp_ana, hp_apm, lp_apm, lp_apm0, tee, lp_tee, assist, mem_mon;
    RomEsp         rom{c6::ROM_B, c6::ROM_FIN - c6::ROM_B};
    std::vector<std::unique_ptr<BloqueDeclarado>> declarados;

    AnalogNet chip_pu{"chip_pu"};

    Esp32C6(sc_core::sc_module_name nm, const McuCapsC6& m, const Cableado& cab = Cableado())
        : sc_core::sc_module(nm), mcu(m), core("core", *m.nucleo, 0),
          gpio("gpio", c6::GPIO_B, pines_c6(), bonded(m), cab),
          iomux("io_mux", c6::IO_MUX_B, gpio),
          intmtx("intmtx", c6::INTMTX_B, c6::N_FUENTES), intpri("intpri", c6::INTPRI_B),
          pcr("pcr", c6::PCR_B), lp_aon("lp_aon", c6::LP_AON_B), lp_clkrst("lp_clkrst", c6::LP_CLKRST_B),
          uart0("uart0", c6::UART0_B, gpio.sig_out[c6::SIG_U0TXD], gpio.sig_in[c6::SIG_U0RXD]),
          uart1("uart1", c6::UART1_B, gpio.sig_out[c6::SIG_U1TXD], gpio.sig_in[c6::SIG_U1RXD]),
          systimer("systimer", c6::SYSTIMER_B),
          timg0("timg0", c6::TIMG0_B), timg1("timg1", c6::TIMG1_B),
          pmu("pmu", c6::PMU_B, 0x400, c6::REGS_PMU),
          hp_sys("hp_system", c6::HP_SYS_B, 0x1000, c6::REGS_HP_SYSTEM),
          efuse("efuse", c6::EFUSE_B, 0x400, c6::REGS_EFUSE),
          spi0("spi0", c6::SPI0_B, 0x1000, c6::REGS_SPI_MEM), spi1("spi1", c6::SPI1_B, 0x1000, c6::REGS_SPI_MEM),
          extmem("extmem", c6::EXTMEM_B, 0x1000, c6::REGS_EXTMEM),
          lp_wdt("lp_wdt", c6::LP_WDT_B, 0x400, c6::REGS_LP_WDT),
          lp_timer("lp_timer", c6::LP_TIMER_B, 0x400, c6::REGS_LP_TIMER),
          i2c_ana_mst("i2c_ana_mst", c6::I2C_ANA_MST_B, 0x800, c6::REGS_I2C_ANA_MST),
          lpperi("lpperi", c6::LPPERI_B, 0x400, c6::REGS_LPPERI),
          lp_ana("lp_analog_peri", c6::LP_ANA_B, 0x400, c6::REGS_LP_ANA_PERI),
          hp_apm("hp_apm", c6::HP_APM_B, 0x800, c6::REGS_HP_APM),
          lp_apm("lp_apm", c6::LP_APM_B, 0x400, c6::REGS_LP_APM),
          lp_apm0("lp_apm0", c6::LP_APM0_B, 0x800, c6::REGS_LP_APM0),
          tee("tee", c6::TEE_B, 0x1000, c6::REGS_TEE), lp_tee("lp_tee", c6::LP_TEE_B, 0x400, c6::REGS_LP_TEE),
          assist("assist_debug", c6::ASSIST_B, 0x1000, c6::REGS_ASSIST_DEBUG),
          mem_mon("mem_monitor", c6::MEM_MON_B, 0x1000, c6::REGS_MEM_MONITOR),
          csr_(gpio), sram_(c6::HP_SRAM_TAM, 0), lp_sram_(c6::LP_SRAM_TAM, 0) {
        gpio.pon_c6();
        rom.pon_empotrada(c6::ROM_REV0, sizeof c6::ROM_REV0 / 4, 0, "esp32c6_rev0_rom.elf");
        bus.register_b_transport(this, &Esp32C6::b_transport);
        bus.register_transport_dbg(this, &Esp32C6::transport_dbg);
        core.ibus.bind(bus);
        core.dbus.bind(bus);
        core.clk_hz(hz_cpu);
        core.rst_n(rst_cpu_n);
        core.quantum = sc_core::sc_time(100, sc_core::SC_NS);
        core.integracion.perif_lo = c6::PERIF;
        core.integracion.perif_hi = c6::PERIF_FIN;
        core.hart.pon_irq(&intpri);
        core.hart.pon_csr_ext(&csr_);
        core.hart.permisos_extra = [this](uint32_t a) { return csr_.pma.firma(a); };

        // Los bloques de la entrega siguiente: declarados, para que el mapa
        // sea el del chip
        struct D { const char* n; uint32_t b, t; const char* e; };
        static const D DECL[] = {
            {"i2c", c6::I2C_B, 0x1000, "E9.4"}, {"uhci0", c6::UHCI_B, 0x1000, "E9.4"},
            {"rmt", c6::RMT_B, 0x1000, "E9.4"}, {"ledc", c6::LEDC_B, 0x1000, "E9.4"},
            {"twai0", c6::TWAI0_B, 0x1000, "E9.4"}, {"i2s", c6::I2S_B, 0x1000, "E9.4"},
            {"twai1", c6::TWAI1_B, 0x1000, "E9.4"}, {"apb_saradc", c6::ADC_B, 0x1000, "E9.4"},
            {"usb_serial_jtag", c6::USB_SJ_B, 0x1000, "E9.5"}, {"atomic", c6::ATOMIC_B, 0x1000, "E9.4"},
            {"pcnt", c6::PCNT_B, 0x1000, "E9.4"}, {"soc_etm", c6::ETM_B, 0x1000, "E9.4"},
            {"mcpwm", c6::MCPWM_B, 0x1000, "E9.4"}, {"parl_io", c6::PARL_IO_B, 0x1000, "E9.4"},
            {"hinf", c6::HINF_B, 0x1000, "E9.4"}, {"slc", c6::SLC_B, 0x1000, "E9.4"},
            {"slchost", c6::SLCHOST_B, 0x1000, "E9.4"}, {"pvt", c6::PVT_B, 0x1000, "E9.4"},
            {"gdma", c6::GDMA_B, 0x1000, "E9.4"}, {"spi2", c6::SPI2_B, 0x1000, "E9.4"},
            {"aes", c6::AES_B, 0x1000, "E9.4"}, {"sha", c6::SHA_B, 0x1000, "E9.4"},
            {"rsa", c6::RSA_B, 0x1000, "E9.4"}, {"ecc", c6::ECC_B, 0x1000, "E9.4"},
            {"ds", c6::DS_B, 0x1000, "E9.4"}, {"hmac", c6::HMAC_B, 0x1000, "E9.4"},
            {"pau", c6::PAU_B, 0x1000, "E9.4"}, {"misc", c6::MISC_B, 0x1000, "E9.4"},
            {"lp_uart", c6::LP_UART_B, 0x400, "E10"}, {"lp_i2c", c6::LP_I2C_B, 0x400, "E10"},
            {"lp_io", c6::LP_IO_B, 0x400, "E10"}, {"lp_i2c_ana_mst", c6::LP_I2C_ANA_B, 0x400, "E10"},
            {"otp_debug", c6::OTP_DBG_B, 0x400, "E9.4"}, {"trace", c6::TRACE_B, 0x1000, "E9.4"},
        };
        for (const D& d : DECL)
            declarados.emplace_back(new BloqueDeclarado(d.n, d.b, d.t, d.n,
                                                        (std::string("En el ESP32-C6 llega en ") + d.e + ".").c_str()));

        std::vector<BusSlave*> todos{&gpio, &iomux, &intmtx, &intpri, &pcr, &lp_aon, &lp_clkrst, &uart0, &uart1,
                                     &systimer, &timg0, &timg1, &pmu, &hp_sys, &efuse, &spi0, &spi1, &extmem,
                                     &lp_wdt, &lp_timer, &i2c_ana_mst, &lpperi, &lp_ana, &hp_apm, &lp_apm,
                                     &lp_apm0, &tee, &lp_tee, &assist, &mem_mon};
        for (auto& d : declarados) todos.push_back(d.get());
        for (BusSlave* s : todos) engancha(s);

        // Interrupciones: la matriz da líneas al INTPRI; el INTPRI y el CLINT,
        // al núcleo
        intmtx.lineas_cambian = [this](uint32_t l) { intpri.lineas(l); };
        intpri.clint = &clint;
        intpri.aviso = [this] { core.nota_irq(); };
        intpri.desde_cpu = [this](unsigned n, bool v) { intmtx.fuente(c6::F_FROM_CPU0 + n, v); };
        clint.aviso = [this] { core.nota_irq(); };
        clint.hz = [this] { const double f = hz_cpu.read(); return f > 0 ? f : 40e6; };
        uart0.irq = [this](bool v) { intmtx.fuente(c6::F_UART0, v); };
        uart1.irq = [this](bool v) { intmtx.fuente(c6::F_UART1, v); };
        uart0.pon_c6([this] { return pcr.uart_sclk(0); });
        uart1.pon_c6([this] { return pcr.uart_sclk(1); });
        gpio.irq = [this](bool v) { intmtx.fuente(c6::F_GPIO, v); };
        systimer.irq = [this](unsigned c, bool v) { intmtx.fuente(c6::F_SYSTIMER0 + c, v); };
        timg0.irq = [this](unsigned b, bool v) { intmtx.fuente(b ? c6::F_TG0_WDT : c6::F_TG0_T0, v); };
        timg1.irq = [this](unsigned b, bool v) { intmtx.fuente(b ? c6::F_TG1_WDT : c6::F_TG1_T0, v); };
        // Relojes
        pcr.al_cambiar_reloj = [this](double cpu, double, double apb) { pon_relojes(cpu, apb); };
        pcr.al_cambiar_timg = [this](unsigned g, double hz) {
            TimgEsp& t = g ? timg1 : timg0;
            t.hz_apb = t.hz_xtal = hz;
        };
        // Resets
        lp_aon.reset_sw = [this](bool sis) { pide_reset(sis ? CAUSA_SW_SIS : CAUSA_SW_CPU, sis); };
        timg0.reset_wdt = [this](bool sis) { pide_reset(sis ? CAUSA_MWDT0_SIS : CAUSA_MWDT0_CPU, sis); };
        timg1.reset_wdt = [this](bool sis) { pide_reset(sis ? CAUSA_MWDT1_SIS : CAUSA_MWDT1_CPU, sis); };
        timg0.flashboot = false;

        siempre.write(true);
        rst_per_n.write(true);
        rst_cpu_n.write(false);
        d_pu_ = chip_pu.register_driver("rc_chip_pu");
        chip_pu.set_hiz(d_pu_);
        SC_HAS_PROCESS(Esp32C6);
        SC_THREAD(control_proc);
        SC_METHOD(chip_pu_proc);
        sensitive << chip_pu.value_changed_event();
        dont_initialize();
    }

    // --- Lo que hace la placa -------------------------------------------------------
    void alimenta(bool si) {
        if (si) chip_pu.set_drive(d_pu_, 3.3f, 10e3f);
        else    chip_pu.set_drive(d_pu_, 0.0f, 10e3f);
    }
    bool encendido() const { return encendido_; }
    // Las causas de reset [reset_reasons.h de ESP-IDF]
    enum : unsigned {
        CAUSA_POR = 1, CAUSA_SW_SIS = 3, CAUSA_MWDT0_SIS = 7, CAUSA_MWDT1_SIS = 8,
        CAUSA_MWDT0_CPU = 11, CAUSA_SW_CPU = 12, CAUSA_MWDT1_CPU = 17,
    };

    // --- El firmware ------------------------------------------------------------------
    // Carga un ELF en la HP SRAM y la LP SRAM y apunta el arranque a su
    // e_entry («arranque directo»). "" si bien.
    std::string carga_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::string e = lee_elf(ruta, t, ELF_RISCV);
        if (!e.empty()) return e;
        for (const TrozoFw& x : t)
            for (size_t i = 0; i < x.datos.size(); ++i)
                if (!poke8(uint32_t(x.dir + i), x.datos[i]))
                    return ruta + ": la direccion " + hex(uint32_t(x.dir + i)) +
                           " no es HP SRAM ni LP SRAM del ESP32-C6";
        uint32_t ent = 0;
        if (!entrada_elf(ruta, ent)) return ruta + ": sin punto de entrada";
        core.hart.pon_pc_arranque(ent);
        cargado_ = true;
        return "";
    }
    bool cargado() const { return cargado_; }

    bool poke8(uint32_t a, uint8_t v) { uint8_t* p = mem(a); if (!p) return false; *p = v; return true; }
    bool peek8(uint32_t a, uint8_t& v) { uint8_t* p = mem(a); if (!p) return false; v = *p; return true; }
    uint32_t peek32(uint32_t a) {
        uint32_t v = 0; uint8_t b;
        for (unsigned i = 0; i < 4; ++i) if (peek8(a + i, b)) v |= uint32_t(b) << (8 * i);
        return v;
    }
    void poke32(uint32_t a, uint32_t v) { for (unsigned i = 0; i < 4; ++i) poke8(a + i, uint8_t(v >> (8 * i))); }

    uint64_t resets() const { return n_resets_; }
    uint64_t fallos_bus() const { return n_fallos_; }
    uint64_t negadas_pma() const { return n_pma_; }
    std::function<void(uint32_t, unsigned, bool)> al_fallar;

    std::vector<std::string> limitaciones() const {
        return {
            "E9.2: el arranque directo (un ELF en la HP SRAM, sin pasar por la ROM); la ROM se lee, "
            "pero el arranque por ella, la Flash por SPI0/SPI1, la cache y la MMU, el eFuse con "
            "comportamiento y ESP-IDF son de la entrega siguiente",
            "el PCR: la CPU, el AHB y el APB, el SCLK de las UART y el reloj de los TIMG; los CLK_EN y "
            "RST_EN de cada periferico se guardan sin cortar ni resetear",
            "el PLL, el FOSC y el XTAL estan siempre: el PMU y el I2C_ANA_MST guardan sin hacer nada",
            "LEDC, RMT, I2C, TWAI0/1, I2S, el SAR ADC, GDMA, GP-SPI2, las de cifrado, PCNT, MCPWM, "
            "PARL_IO, ETM, SDIO y el USB-Serial-JTAG: declarados (E9.4, E9.5)",
            "el nucleo LP y sus perifericos (LP_UART, LP_I2C, LP_IO): declarados (E10)",
            "la depuracion (el DM, el TAP y GDB): E9.6",
        };
    }

private:
    CsrC6 csr_;
    std::vector<uint8_t> sram_, lp_sram_;
    std::vector<std::unique_ptr<tlm_utils::simple_initiator_socket<Esp32C6>>> isk_;
    std::vector<BusSlave*> slaves_;
    int d_pu_ = -1;
    bool encendido_ = false, cargado_ = false;
    uint64_t n_resets_ = 0, n_fallos_ = 0, n_pma_ = 0;
    sc_core::sc_event ev_reset_, ev_pu_;
    unsigned causa_pend_ = 0;
    bool sis_pend_ = false;

    static std::vector<bool> bonded(const McuCapsC6& m) {
        std::vector<bool> b(c6::N_GPIO);
        for (unsigned i = 0; i < c6::N_GPIO; ++i) b[i] = m.enc.bonded(0, i);
        return b;
    }
    static std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "0x%08X", v); return b; }

    void engancha(BusSlave* s) {
        s->clk(reloj_falso);
        s->rst_n(rst_per_n);
        s->clk_en(siempre);
        s->clk_hz(hz_apb);
        isk_.emplace_back(new tlm_utils::simple_initiator_socket<Esp32C6>(sc_core::sc_gen_unique_name("isk")));
        isk_.back()->bind(s->tsk);
        slaves_.push_back(s);
    }

    void pon_relojes(double cpu, double apb) {
        hz_cpu.write(cpu);
        hz_apb.write(apb);
        clint.reloj_cambia();
        // El SCLK de las UART con «1» es el PLL_F80M, fijo [TRM-C6 §8]
        uart0.hz_apb = uart1.hz_apb = pcr.hz_pll_f80m;
        uart0.hz_xtal = uart1.hz_xtal = pcr.hz_xtal;
    }

    uint8_t* mem(uint32_t a) {
        if (a >= c6::HP_SRAM && a < c6::HP_SRAM + c6::HP_SRAM_TAM) return &sram_[a - c6::HP_SRAM];
        if (a >= c6::LP_SRAM && a < c6::LP_SRAM + c6::LP_SRAM_TAM) return &lp_sram_[a - c6::LP_SRAM];
        if (a >= c6::ROM_B && a < c6::ROM_FIN) return rom.byte(a);
        return nullptr;
    }

    void b_transport(int i, tlm::tlm_generic_payload& gp, sc_core::sc_time& d) {
        acceso(i, gp, d);
        if (al_fallar && gp.get_response_status() != tlm::TLM_OK_RESPONSE)
            al_fallar(uint32_t(gp.get_address()), gp.get_data_length(), gp.is_write());
    }
    void acceso(int, tlm::tlm_generic_payload& gp, sc_core::sc_time& d) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        unsigned char* p = gp.get_data_ptr();
        RvExt* ext = nullptr;
        gp.get_extension(ext);
        const bool fetch = ext && ext->acceso == RvAcceso::Fetch;
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        // La PMA mira todo lo que hace el núcleo
        if (ext && !ext->depurador && !csr_.pma.permite(a, n, ext->acceso)) { ++n_pma_; ++n_fallos_; return; }
        if (a >= c6::HP_SRAM && a + n <= c6::HP_SRAM + c6::HP_SRAM_TAM) {
            copia(gp, &sram_[a - c6::HP_SRAM], n, p);
            return;
        }
        if (a >= c6::LP_SRAM && a + n <= c6::LP_SRAM + c6::LP_SRAM_TAM) {
            copia(gp, &lp_sram_[a - c6::LP_SRAM], n, p);
            d += sc_core::sc_time(2.0 / (hz_apb.read() > 0 ? hz_apb.read() : 40e6), sc_core::SC_SEC);
            return;
        }
        if (a >= c6::ROM_B && a + n <= c6::ROM_FIN) {
            if (gp.is_write() || !rom.hay()) { ++n_fallos_; return; }
            std::copy(rom.byte(a), rom.byte(a) + n, p);
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (a >= c6::CLINT_B && a + n <= c6::CLINT_FIN && !fetch) {
            clint.acceso(gp, a - c6::CLINT_B);
            return;
        }
        if (a >= c6::PERIF && a < c6::PERIF_FIN && !fetch) {
            for (size_t i = 0; i < slaves_.size(); ++i) {
                BusSlave* s = slaves_[i];
                if (a >= s->base() && a + n <= s->base() + s->size()) {
                    (*isk_[i])->b_transport(gp, d);
                    if (gp.get_response_status() != tlm::TLM_OK_RESPONSE) ++n_fallos_;
                    return;
                }
            }
        }
        ++n_fallos_;
    }
    static void copia(tlm::tlm_generic_payload& gp, uint8_t* m, unsigned n, unsigned char* p) {
        if (gp.is_write()) std::copy(p, p + n, m);
        else std::copy(m, m + n, p);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned transport_dbg(int, tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        for (unsigned i = 0; i < n; ++i) {
            uint8_t* m = mem(a + i);
            if (!m) return i;
            if (gp.is_write()) *m = gp.get_data_ptr()[i]; else gp.get_data_ptr()[i] = *m;
        }
        return n;
    }

    // --- Resets ---------------------------------------------------------------------
    void pide_reset(unsigned causa, bool sistema) {
        causa_pend_ = causa;
        sis_pend_ = sistema;
        ev_reset_.notify(sc_core::SC_ZERO_TIME);
    }
    void chip_pu_proc() { ev_pu_.notify(sc_core::SC_ZERO_TIME); }
    bool pu_alto() const { return chip_pu.voltage() > 0.75f * 3.3f; }

    void reset_perifericos() {
        gpio.reset(); intmtx.reset(); intpri.reset(); clint.reset(); pcr.reset();
        uart0.reset(); uart1.reset(); systimer.reset(); timg0.reset(); timg1.reset();
        for (BloqueEsp* b : {&pmu, &hp_sys, &efuse, &spi0, &spi1, &extmem, &lp_wdt, &lp_timer, &i2c_ana_mst,
                             &lpperi, &lp_ana, &hp_apm, &lp_apm, &lp_apm0, &tee, &lp_tee, &assist, &mem_mon})
            b->reset_regs();
    }

    void control_proc() {
        pon_relojes(40e6, 40e6);
        for (unsigned i = 0; i < 8; ++i) gpio.sig_oe[28 + i].write(false);       // la GPIO dedicada
        gpio.sig_oe[c6::SIG_U0TXD].write(true);
        gpio.sig_oe[c6::SIG_U1TXD].write(true);
        for (;;) {
            encendido_ = false;
            rst_cpu_n.write(false);
            while (!pu_alto()) wait(ev_pu_);
            // El reset de chip: todo, los STORE de LP_AON incluidos
            ++n_resets_;
            lp_aon.reset_chip();
            lp_clkrst.reset_chip();
            reset_perifericos();
            wait(1, sc_core::SC_US);
            if (!pu_alto()) continue;
            gpio.latch_arranque();
            csr_.reset();
            encendido_ = true;
            rst_cpu_n.write(true);
            for (;;) {
                wait(ev_pu_ | ev_reset_);
                if (!pu_alto()) break;
                if (causa_pend_) {
                    const unsigned c = causa_pend_;
                    const bool sis = sis_pend_;
                    causa_pend_ = 0;
                    ++n_resets_;
                    rst_cpu_n.write(false);
                    if (sis) {
                        reset_perifericos();
                        if (lp_aon.force_download()) gpio.fuerza_descarga();
                    }
                    lp_clkrst.anota_causa(c);
                    csr_.reset();
                    wait(100, sc_core::SC_NS);
                    rst_cpu_n.write(true);
                }
            }
        }
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_ESP32C6_H
