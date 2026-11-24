// =============================================================================
// esp32c6.h — EL ESP32-C6 (fase E9 de doc/analisis_esp32_riscv.md)
//
// El núcleo genérico de src/rv con RV_ESP32C6_HP (E9.1) y lo que Espressif
// puso alrededor. E9.2: el chip en «arranque directo», como el C3 de E3: el
// firmware se carga en la HP SRAM y el núcleo arranca en su punto de entrada,
// sin la ROM. E9.3: la ROM de verdad (rev0, la de todos los C6), la Flash por
// SPI0/SPI1, la caché y su MMU (c6_cache.h), el eFuse, el SHA, el RNG y el
// I2C_ANA_MST: el chip arranca por la ROM, que lee la Flash como en la placa,
// y ESP-IDF corre (hello_world, con FreeRTOS y esp_restart). E10: el núcleo LP
// (c6_lp.h), un segundo NucleoRv, con el PMU, el LP timer, el LP IO, la LP
// UART y el LP I2C, el sueño del HP y un DM con los dos harts. E11: el PCNT,
// el MCPWM y el ETM.
//
//   memoria    la HP SRAM (512 KB, 0x4080_0000) es una sola para los buses I y
//              D [TRM-C6 §5.3]: se ejecuta y se lee por la misma dirección, en
//              bytes o en palabras. La LP SRAM (16 KB, 0x5000_0000), igual. La
//              ROM en 0x4000_0000-0x4004_FFFF. La Flash, por la caché, en la
//              ventana única 0x4200_0000-0x42FF_FFFF.
//   interrup.  la matriz (77 fuentes) y el INTPRI, que es también el PLIC de
//              la región de la CPU (MX en 0x2000_1000, UX en 0x2000_1400, el que
//              usa ESP-IDF), con el CLINT de Espressif en 0x2000_1800
//              (soc/esp/intc_esp_v2.h); el núcleo tiene mie/mip y delega a U
//              con mideleg.
//   PMA        16 entradas en los CSR 0xBC0-0xBDF (soc/esp/pma_esp.h): cada
//              acceso del núcleo pasa por ella (los del depurador, no).
//   reloj      el PCR (c6_sistema.h): XTAL de 40 MHz tras el reset; con el
//              PLL, la CPU a 160 (o 120, 80...) y el AHB y el APB a 40
//   periféric. UART0/1 (la UART con la disposición del C6), SYSTIMER, TIMG0/1,
//              GPIO con la matriz e IO_MUX sobre pads eléctricos, la GPIO
//              dedicada por los CSR, LP_AON (STORE, resets por software),
//              LP_CLKRST (la causa), eFuse (de lectura), SPI0/SPI1 con la MMU,
//              EXTMEM, LPPERI (el RNG), I2C_ANA_MST; y los del C3 con las
//              diferencias del C6 (E9.4): LEDC (el fundido por la RAM gamma),
//              RMT, I2C, TWAI0 y TWAI1, I2S, UHCI, el SAR ADC y el sensor de
//              temperatura, la GDMA (sus interrupciones de entrada y salida
//              separadas), GP-SPI2, AES y SHA con su DMA, RSA, HMAC, DS,
//              XTS_AES (dentro del SPI1) y el USB-Serial-JTAG, con sus relojes
//              del PCR; E10: el PMU, el LP timer, el LP IO, la LP UART y el LP
//              I2C (c6_lp.h); E11: el PCNT, el MCPWM y el ETM (periph/esp); y
//              guardando con sus valores de reset: HP_SYSTEM, LP_WDT,
//              LP_ANALOG, las APM y los TEE, ASSIST_DEBUG, MEM_MONITOR (el
//              monitor de memoria: sin vigilar), MODEM_SYSCON, MODEM_LPCON, el
//              coexist y el radio. El resto, DECLARADOS (leen cero y avisan):
//              PARL_IO, ECC, el esclavo SDIO, la traza...
//   reset      CHIP_PU: el reset de chip, y los cinco pines de arranque
//              (GPIO8, GPIO9, MTMS, MTDI y GPIO15) se guardan al soltarlo.
//              Resets del sistema y de la CPU por software (LP_AON) con su
//              causa en LP_CLKRST.
//   CSR        mpcer/mpcmr/mpccr (0x7E0-0x7E2) y su copia de usuario
//              (0x800-0x802), cpu_gpio_oen/in/out (0x803-0x805) y la PMA
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
#include "c6_cache.h"
#include "c6_lp.h"
#include "../../parts/spi_flash.h"
#include "../esp/intc_esp_v2.h"
#include "../esp/pma_esp.h"
#include "../esp/esp_rom.h"
#include "../../rv/hart/nucleo_rv.h"
#include "../../rv/debug/rv_dm.h"
#include "../../rv/debug/rv_dtm_jtag.h"
#include "../../rv/debug/jtag_pines.h"
#include "../../periph/esp/uart_esp.h"
#include "../../periph/esp/systimer_esp.h"
#include "../../periph/esp/timg_esp.h"
#include "../../periph/esp/gpio_esp.h"
#include "../../periph/esp/sha_esp.h"
#include "../../periph/esp/usb_serial_jtag.h"
#include "../../periph/esp/ledc_esp.h"
#include "../../periph/esp/rmt_esp.h"
#include "../../periph/esp/saradc_esp.h"
#include "../../periph/esp/aes_esp.h"
#include "../../periph/esp/i2c_esp.h"
#include "../../periph/esp/twai_esp.h"
#include "../../periph/esp/gdma_esp.h"
#include "../../periph/esp/spi_gp_esp.h"
#include "../../periph/esp/rsa_esp.h"
#include "../../periph/esp/hmac_esp.h"
#include "../../periph/esp/ds_esp.h"
#include "../../periph/esp/xts_aes_esp.h"
#include "../../periph/esp/i2s_esp.h"
#include "../../periph/esp/uhci_esp.h"
#include "../../periph/esp/pcnt_esp.h"
#include "../../periph/esp/mcpwm_esp.h"
#include "../../periph/esp/etm_esp.h"
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
// rendimiento de máquina (mpcer/mpcmr/mpccr, 0x7E0-0x7E2), la GPIO dedicada
// (0x803-0x805) y la PMA (0xBC0-0xBDF). Y la copia de usuario de los
// contadores (upcer/upcmr/upccr, 0x800-0x802), que el TRM no lista pero la
// ROM escribe al arrancar (`csrw 0x800, 1`, `csrw 0x801, 1`), como la del C3.
class CsrC6 : public rv_csr_ext_if {
public:
    PmaEsp pma;
    explicit CsrC6(GpioEsp& g) : g_(g) { reset(); }
    bool atiende(uint16_t a) const override {
        return (a >= 0x7E0 && a <= 0x7E2) || (a >= 0x800 && a <= 0x805) || PmaEsp::es_suyo(a);
    }
    bool lee(RvHart& h, uint16_t a, uint32_t& v) override {
        if (a >= 0x800 && a <= 0x802) { v = pc_[1].lee(h, a - 0x800); return true; }
        if (h.modo() != RvModo::M) return false;
        if (PmaEsp::es_suyo(a)) return pma.lee(a, v);
        switch (a) {
        case 0x7E0: case 0x7E1: case 0x7E2: v = pc_[0].lee(h, a - 0x7E0); return true;
        case 0x803: v = g_.cpu_oen(); return true;
        case 0x804: v = g_.cpu_gpio_in(); return true;
        case 0x805: v = g_.cpu_out(); return true;
        default: return false;
        }
    }
    bool escribe(RvHart& h, uint16_t a, uint32_t v) override {
        if (a >= 0x800 && a <= 0x802) { pc_[1].escribe(h, a - 0x800, v); return true; }
        if (h.modo() != RvModo::M) return false;
        if (PmaEsp::es_suyo(a)) { pma.escribe(a, v); return true; }
        switch (a) {
        case 0x7E0: case 0x7E1: case 0x7E2: pc_[0].escribe(h, a - 0x7E0, v); return true;
        case 0x803: g_.cpu_gpio_oen(uint8_t(v)); return true;
        case 0x804: return true;                         // solo de lectura
        case 0x805: g_.cpu_gpio_out(uint8_t(v)); return true;
        default: return false;
        }
    }
    // Como en el C3: los de máquina salen del reset contando ciclos
    // [deducido de ESP-IDF, que lee mpccr sin habilitarlo]; los de usuario,
    // parados. La PMA, a su reset.
    void reset() { pc_[0] = Contador(); pc_[0].er = 1; pc_[0].mr = 1; pc_[1] = Contador(); pma.reset(); }
private:
    struct Contador {
        uint32_t er = 0, mr = 0, base = 0;
        uint64_t desde = 0;
        static uint64_t fuente(RvHart& h, uint32_t e) { return (e & 1u) ? h.ciclos() : h.instret(); }
        uint32_t cuenta(RvHart& h) const {
            if (!(mr & 1u) || !er) return base;
            return base + uint32_t(fuente(h, er) - desde);
        }
        uint32_t lee(RvHart& h, unsigned i) const { return i == 0 ? er : i == 1 ? mr : cuenta(h); }
        void escribe(RvHart& h, unsigned i, uint32_t v) {
            if (i == 0) { base = cuenta(h); desde = fuente(h, er = v & 0x7FFu); }
            else if (i == 1) { base = cuenta(h); mr = v & 3u; desde = fuente(h, er); }
            else { base = v; desde = fuente(h, er); }
        }
    };
    GpioEsp& g_;
    Contador pc_[2];
};

class Esp32C6 : public sc_core::sc_module {
public:
    const McuCapsC6& mcu;

    // --- Relojes y resets -----------------------------------------------------------
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_cpu{"hz_cpu"}, hz_apb{"hz_apb"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_cpu_n{"rst_cpu_n"};
    // E10: el núcleo LP, su reset y su reloj (el LP_FAST)
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_lp_n{"rst_lp_n"};
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_lp{"hz_lp"};
    sc_core::sc_signal<bool> rst_per_n{"rst_per_n"}, siempre{"siempre"}, reloj_falso{"reloj_falso"};

    // --- El bus y el núcleo -----------------------------------------------------------
    tlm_utils::multi_passthrough_target_socket<Esp32C6> bus{"bus"};
    NucleoRv core;
    NucleoRv core_lp;              // E10: el núcleo LP (hart 1)

    // --- La depuración (E9.6) ----------------------------------------------------
    // El Debug Module 0.13 de E2 con las capacidades del C6 (RV_ESP32C6_HP.dbg:
    // 4 triggers, program buffer de 16), su DTM JTAG con el IDCODE del C6
    // (0x0000DC25, el `_CPUTAPID` de target/esp32c6.cfg: UN TAP, el del HP;
    // el LP, E10) y el TAP en MTMS (GPIO4), MTDI (GPIO5), MTCK (GPIO6) y MTDO
    // (GPIO7). La fuente la eligen los eFuse al soltar el reset de chip, con
    // los mismos bits que el C3 en RD_REPEAT_DATA0 [TRM-C6 §7.3]:
    //   DIS_PAD_JTAG, o SOFT_DIS_JTAG impar: los pines no
    //   JTAG_SEL_ENABLE = 0: el USB-Serial-JTAG, salvo DIS_USB_JTAG = 1: los pines
    //   JTAG_SEL_ENABLE = 1: el pin de arranque GPIO15 (1 USB, 0 pines)
    // De fábrica, el USB; los pines, con jtag="pines" (DIS_USB_JTAG quemado).
    // ndmreset: un reset de la CPU que la tiene parada mientras dura.
    static constexpr uint32_t IDCODE = 0x0000DC25u;
    enum class FuenteJtag { Ninguna, Pines, Usb };
    std::unique_ptr<RvDm> dm;
    std::unique_ptr<RvDtmJtag> tap;
    std::unique_ptr<JtagPines> jtag;
    FuenteJtag fuente_jtag() const { return fuente_jtag_; }
    // jtag="pines": como quemar EFUSE_DIS_USB_JTAG (vale en el próximo reset de chip)
    void jtag_en_pines(bool si) { efuse.pon_bit(0x30, 9, si); }

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
    EfuseC6        efuse;
    Spi0C6         spi0;
    SpiMemEsp      spi1;
    CacheC6        cache;
    ShaEsp         sha;
    I2cAnaMstC6    i2c_ana_mst;
    LpPeriC6       lpperi;
    // E9.4: los periféricos que el C6 tiene en común con el C3, con los
    // cambios del C6 (y el segundo TWAI)
    UsbSerialJtagEsp usj;          // D- en GPIO12, D+ en GPIO13
    LedcEsp        ledc;
    RmtEsp         rmt;
    SarAdcEsp      adc;            // y el sensor de temperatura
    AesEsp         aes;
    I2cEsp         i2c;
    TwaiEsp        twai0, twai1;
    GdmaEsp        gdma;
    SpiGpEsp       spi2;
    RsaEsp         rsa;
    HmacEsp        hmac;
    DsEsp          ds;
    XtsAesEsp      xts;            // dentro del SPI1, en 0x6000_3300
    I2sEsp         i2s;
    UhciEsp        uhci;
    // E11: los periféricos propios del C6
    PcntEsp        pcnt;           // el contador de pulsos
    McpwmEsp       mcpwm;          // el PWM para motores (un grupo)
    EtmEsp         etm;            // la matriz de sucesos y tareas
    PmuC6          pmu;            // E10: el núcleo LP y el sueño del HP
    LpTimerC6      lp_timer;       // E10: el RTC timer
    LpIoC6         lp_io;          // E10: LP_GPIO0-7
    // E10: la LP UART (TX en LP_GPIO5, RX en LP_GPIO4) y el LP I2C (SDA 6,
    // SCL 7), por la función 1 del LP IO; los modelos de la UART y el I2C
    GpioEsp::Senal lp_tx{"lp_tx"}, lp_rx{"lp_rx"};
    UartEsp        lp_uart;
    I2cEsp         lp_i2c;
    BloqueEsp      hp_sys, lp_wdt,
                   lp_ana, hp_apm, lp_apm, lp_apm0, tee, lp_tee, assist, mem_mon, modem_syscon, modem_lpcon;
    // Los registros de la coexistencia Wi-Fi/BT/802.15.4 (0x600A_F400), que la
    // biblioteca de coexistencia de ESP-IDF borra al arrancar: guardan
    BloqueGuardado modem_coex{"modem_coex", 0x600AF400u, 0x400};
    // La radio del módem (0x600A_0000-0x600A_97FF: el front-end y PWDET,
    // el 802.15.4...): guardan. De aquí lee y escribe el ADC PWDET_CONF
    // (0x600A_0810), que da la alimentación del SAR [sar_ctrl_ll.h]
    BloqueGuardado modem_radio{"modem_radio", 0x600A0000u, 0x9800};
    RomEsp         rom{c6::ROM_B, c6::ROM_FIN - c6::ROM_B};
    std::vector<std::unique_ptr<BloqueDeclarado>> declarados;

    AnalogNet chip_pu{"chip_pu"};

    Esp32C6(sc_core::sc_module_name nm, const McuCapsC6& m, const Cableado& cab = Cableado())
        : sc_core::sc_module(nm), mcu(m), core("core", *m.nucleo, 0), core_lp("core_lp", RV_ESP32C6_LP, 1),
          gpio("gpio", c6::GPIO_B, pines_c6(), bonded(m), cab),
          iomux("io_mux", c6::IO_MUX_B, gpio),
          intmtx("intmtx", c6::INTMTX_B, c6::N_FUENTES), intpri("intpri", c6::INTPRI_B),
          pcr("pcr", c6::PCR_B), lp_aon("lp_aon", c6::LP_AON_B), lp_clkrst("lp_clkrst", c6::LP_CLKRST_B),
          uart0("uart0", c6::UART0_B, gpio.sig_out[c6::SIG_U0TXD], gpio.sig_in[c6::SIG_U0RXD]),
          uart1("uart1", c6::UART1_B, gpio.sig_out[c6::SIG_U1TXD], gpio.sig_in[c6::SIG_U1RXD]),
          systimer("systimer", c6::SYSTIMER_B),
          timg0("timg0", c6::TIMG0_B), timg1("timg1", c6::TIMG1_B),
          efuse("efuse", c6::EFUSE_B, m.revision, m.flash_interna ? 1u : 0u, m.flash_interna >> 20),
          spi0("spi0", c6::SPI0_B), spi1("spi1", c6::SPI1_B, c6::REGS_SPI_MEM),
          cache("extmem", c6::EXTMEM_B, spi0), sha("sha", c6::SHA_B, 0x20201229u),
          i2c_ana_mst("i2c_ana_mst", c6::I2C_ANA_MST_B), lpperi("lpperi", c6::LPPERI_B),
          usj("usb_serial_jtag", c6::USB_SJ_B, gpio.nodo_de(13), gpio.nodo_de(12)),
          ledc("ledc", c6::LEDC_B), rmt("rmt", c6::RMT_B), adc("apb_saradc", c6::ADC_B), aes("aes", c6::AES_B),
          i2c("i2c", c6::I2C_B), twai0("twai0", c6::TWAI0_B), twai1("twai1", c6::TWAI1_B),
          gdma("gdma", c6::GDMA_B), spi2("spi2", c6::SPI2_B), rsa("rsa", c6::RSA_B), hmac("hmac", c6::HMAC_B),
          ds("ds", c6::DS_B), xts("xts_aes", c6::SPI1_B + 0x300, 0x100), i2s("i2s", c6::I2S_B), uhci("uhci0", c6::UHCI_B),
          pcnt("pcnt", c6::PCNT_B), mcpwm("mcpwm", c6::MCPWM_B), etm("soc_etm", c6::ETM_B),
          pmu("pmu", c6::PMU_B), lp_timer("lp_timer", c6::LP_TIMER_B), lp_io("lp_io", c6::LP_IO_B),
          lp_uart("lp_uart", c6::LP_UART_B, lp_tx, lp_rx, 0x400), lp_i2c("lp_i2c", c6::LP_I2C_B, 0x400),
          hp_sys("hp_system", c6::HP_SYS_B, 0x1000, c6::REGS_HP_SYSTEM),
          lp_wdt("lp_wdt", c6::LP_WDT_B, 0x400, c6::REGS_LP_WDT),
          lp_ana("lp_analog_peri", c6::LP_ANA_B, 0x400, c6::REGS_LP_ANA_PERI),
          hp_apm("hp_apm", c6::HP_APM_B, 0x800, c6::REGS_HP_APM),
          lp_apm("lp_apm", c6::LP_APM_B, 0x400, c6::REGS_LP_APM),
          lp_apm0("lp_apm0", c6::LP_APM0_B, 0x800, c6::REGS_LP_APM0),
          tee("tee", c6::TEE_B, 0x1000, c6::REGS_TEE), lp_tee("lp_tee", c6::LP_TEE_B, 0x400, c6::REGS_LP_TEE),
          assist("assist_debug", c6::ASSIST_B, 0x1000, c6::REGS_ASSIST_DEBUG),
          mem_mon("mem_monitor", c6::MEM_MON_B, 0x1000, c6::REGS_MEM_MONITOR),
          modem_syscon("modem_syscon", c6::MODEM_SYSCON_B, 0x400, c6::REGS_MODEM_SYSCON),
          modem_lpcon("modem_lpcon", c6::MODEM_LPCON_B, 0x400, c6::REGS_MODEM_LPCON),
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
        // Los accesos a la ROM y a la HP SRAM, sin el bus: lo mismo que
        // `acceso` (la PMA, sin tiempo), sin construir la transacción
        core.integracion.acceso_directo = [this](uint32_t a, unsigned n, uint32_t& v, bool escr, RvAcceso t) {
            uint8_t* q = nullptr;
            if (a >= c6::HP_SRAM && a + n <= c6::HP_SRAM + c6::HP_SRAM_TAM) q = &sram_[a - c6::HP_SRAM];
            else if (!escr && a >= c6::ROM_B && a + n <= c6::ROM_FIN && rom.hay()) q = const_cast<uint8_t*>(rom.byte(a));
            if (!q || !csr_.pma.permite(a, n, t)) return false;
            if (escr) { for (unsigned i = 0; i < n; ++i) q[i] = uint8_t(v >> (8 * i)); core_lp.hart.rompe_reserva(a, n); }
            else { v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(q[i]) << (8 * i); }
            return true;
        };
        core.hart.pon_irq(&intpri);
        conecta_lp();
        core.hart.pon_csr_ext(&csr_);
        core.hart.permisos_extra = [this](uint32_t a) { return csr_.pma.firma(a); };
        // El reloj del MSPI: 80 MHz con el PLL, el XTAL sin él
        auto hz_mspi = [this] { return pcr.soc_clk_sel() == 1 ? 80e6 : pcr.hz_xtal; };
        spi0.hz_origen = hz_mspi;
        spi1.hz_origen = hz_mspi;
        if (m.flash_interna) {
            flash_interna_.reset(new SpiFlashChip(*busca_modelo_flash(m.flash_interna > (4u << 20) ? "GD25Q64" : "GD25Q32"),
                                                  "Flash interna"));
            pon_flash(flash_interna_.get());
        }

        // Los bloques de la entrega siguiente: declarados, para que el mapa
        // sea el del chip
        struct D { const char* n; uint32_t b, t; const char* e; };
        static const D DECL[] = {
            // Los que el C3 no tiene: fuera de E9 [doc/analisis_esp32_riscv.md]
            {"atomic", c6::ATOMIC_B, 0x1000, "una fase posterior"},
            // E11 los deja declarados: el PARL_IO, el ECC, el esclavo SDIO (no
            // hay pieza que haga de anfitrión), el codificador de traza y el
            // monitor de memoria
            {"parl_io", c6::PARL_IO_B, 0x1000, "-"}, {"ecc", c6::ECC_B, 0x1000, "-"},
            {"hinf", c6::HINF_B, 0x1000, "-"}, {"slc", c6::SLC_B, 0x1000, "-"},
            {"slchost", c6::SLCHOST_B, 0x1000, "-"}, {"pvt", c6::PVT_B, 0x1000, "una fase posterior"},
            {"pau", c6::PAU_B, 0x1000, "una fase posterior"}, {"misc", c6::MISC_B, 0x1000, "una fase posterior"},
            {"otp_debug", c6::OTP_DBG_B, 0x400, "una fase posterior"}, {"trace", c6::TRACE_B, 0x1000, "-"},
            // El núcleo LP y lo suyo
{"lp_i2c_ana_mst", c6::LP_I2C_ANA_B, 0x400, "E10"},
        };
        for (const D& d : DECL)
            declarados.emplace_back(new BloqueDeclarado(d.n, d.b, d.t, d.n,
                                                        (std::string(d.e)[0] == '-'
                                                             ? std::string("En el ESP32-C6 de mcu-sim, declarado (E11).")
                                                             : std::string("En el ESP32-C6 llega en ") + d.e + ".").c_str()));

        std::vector<BusSlave*> todos{&gpio, &iomux, &intmtx, &intpri, &pcr, &lp_aon, &lp_clkrst, &uart0, &uart1, &lp_io, &lp_uart, &lp_i2c,
                                     &systimer, &timg0, &timg1, &efuse, &spi0, &spi1, &cache, &pmu, &hp_sys,
                                     &lp_wdt, &lp_timer, &i2c_ana_mst, &lpperi, &lp_ana, &hp_apm, &lp_apm,
                                     &lp_apm0, &tee, &lp_tee, &assist, &mem_mon, &sha, &modem_syscon, &modem_lpcon,
                                     &modem_coex, &modem_radio, &usj, &ledc, &rmt, &adc, &aes, &i2c, &twai0, &twai1, &gdma, &spi2, &rsa,
                                     &hmac, &ds, &i2s, &uhci, &pcnt, &mcpwm, &etm};
        // El XTS_AES antes que el SPI1, que lo contiene
        todos.insert(std::find(todos.begin(), todos.end(), static_cast<BusSlave*>(&spi1)), &xts);
        for (auto& d : declarados) todos.push_back(d.get());
        for (BusSlave* s : todos) engancha(s, (s == &twai0 || s == &twai1) ? hz_xtal_ : hz_apb);
        conecta_perifericos();

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
        // La calibración del RC_FAST (RTCCALICFG.CLK_SEL 1): en el C6 no hay
        // RC_FAST_D256; desde la v0.1 se calibra el RC_FAST entre 32 (el
        // REF_TICK) y, en la v0.0, sin dividir [rtc_time.c de ESP-IDF:
        // CLK_LL_RC_FAST_CALIB_TICK_DIV_BITS]
        for (TimgEsp* t : {&timg0, &timg1}) t->hz_rc_fast_d256 = 17.5e6 / (m.revision >= 1 ? 32.0 : 1.0);
        timg0.irq = [this](unsigned b, bool v) { intmtx.fuente(b ? c6::F_TG0_WDT : c6::F_TG0_T0, v); };
        timg1.irq = [this](unsigned b, bool v) { intmtx.fuente(b ? c6::F_TG1_WDT : c6::F_TG1_T0, v); };
        // Relojes
        pcr.al_cambiar_reloj = [this](double cpu, double, double apb) { pon_relojes(cpu, apb); };
        pcr.al_cambiar_timg = [this](unsigned g, double hz) {
            TimgEsp& t = g ? timg1 : timg0;
            t.hz_apb = t.hz_xtal = hz;
        };
        // Resets
        pcr.al_escribir = [this](uint32_t off) { relojes_perifericos(off); };
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
        usj.alimenta(si);
        if (si) chip_pu.set_drive(d_pu_, 3.3f, 10e3f);
        else    chip_pu.set_drive(d_pu_, 0.0f, 10e3f);
    }
    bool encendido() const { return encendido_; }
    // Las causas de reset [reset_reasons.h de ESP-IDF]
    enum : unsigned {
        CAUSA_POR = 1, CAUSA_SW_SIS = 3, CAUSA_DEEP_SLEEP = 5, CAUSA_MWDT0_SIS = 7, CAUSA_MWDT1_SIS = 8,
        CAUSA_MWDT0_CPU = 11, CAUSA_SW_CPU = 12, CAUSA_MWDT1_CPU = 17, CAUSA_USB_UART = 21,
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
        timg0.flashboot = false;
        cargado_ = true;
        return "";
    }
    bool cargado() const { return cargado_; }

    // --- La Flash y el arranque por la ROM -------------------------------------
    void pon_flash(SpiFlashChip* f) { spi0.flash = spi1.flash = f; }
    SpiFlashChip* flash() const { return spi0.flash; }
    SpiFlashChip* flash_interna() const { return flash_interna_.get(); }
    // Una imagen en la Flash desde `off` (un .bin de esptool) y el arranque
    // por la ROM. "" si bien.
    std::string carga_imagen(const std::string& ruta, uint32_t off) {
        if (!flash()) return "el ESP32-C6 no tiene Flash: ni en el encapsulado ni en la placa";
        std::FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) return "no se puede leer " + ruta;
        std::vector<uint8_t> d;
        unsigned char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
        std::fclose(f);
        if (uint64_t(off) + d.size() > flash()->tam())
            return ruta + ": no cabe en la Flash (" + std::to_string(flash()->tam() >> 10) + " KB)";
        flash()->escribe_directo(off, d.data(), d.size());
        arranca_por_rom();
        return "";
    }
    // El arranque normal: la CPU sale del reset en la ROM
    void arranca_por_rom() {
        core.hart.pon_pc_arranque(mcu.nucleo->pc_reset);
        timg0.flashboot = true;
        cargado_ = true;
    }

    // Sin firmware y sin ROM que arrancar: el núcleo, en un wfi en la HP SRAM
    void aparca_en_wfi() {
        poke32(c6::HP_SRAM, 0x10500073u);           // wfi
        poke32(c6::HP_SRAM + 4, 0xFFDFF06Fu);       // j .-4
        core.hart.pon_pc_arranque(c6::HP_SRAM);
        timg0.flashboot = false;
        cargado_ = true;
    }

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
            "la ROM de verdad (rev0) arranca desde la Flash (SPI0/SPI1, la cache de 32 KB y la MMU de 256 "
            "paginas) o espera en el modo descarga; un ELF bare-metal se carga en la HP SRAM y arranca SIN "
            "ROM (el arranque directo de E9.2)",
            "el PCR: la CPU, el AHB y el APB y el reloj de cada periferico; los CLK_EN se guardan sin "
            "cortar, y los RST_EN solo resetean el PCNT, el ETM y el MCPWM",
            "el PLL, el FOSC y el XTAL estan siempre y la calibracion del BBPLL (I2C_ANA_MST) acaba en el "
            "acto; el PMU (E10) hace el sueno del nucleo LP y el profundo y el ligero del HP, con sus "
            "fuentes de despertar (el LP timer, el LP IO y el nucleo LP), pero no corta dominios de "
            "alimentacion: la HP SRAM no se pierde en el sueno profundo [de modelo]",
            "eFuse: solo lectura (de un fichero o la de omision, con la revision, el encapsulado y la "
            "Flash de dentro); programar no programa",
            "el RNG (LPPERI_RNG_DATA) es un xorshift32 de semilla fija: igual en cada simulacion",
            "el RWDT (LP_WDT) y el super watchdog guardan sin contar",
            "LEDC (con el fundido por la RAM gamma), RMT, I2C, TWAI0 y TWAI1, I2S, UHCI, el SAR ADC y el "
            "sensor de temperatura, la GDMA, GP-SPI2, AES y SHA (con su DMA), RSA, HMAC, DS, XTS_AES y el "
            "USB-Serial-JTAG: los modelos del C3 con las diferencias del C6 (E9.4); sus relojes, del PCR",
            "E11: el PCNT (cuatro unidades, filtro, limites y umbrales), el MCPWM (tres temporizadores con "
            "sincronia, tres operadores con tiempo muerto y paradas por fallo, y la captura; sin el "
            "troceador) y el ETM (el GPIO, los TIMG y el MCPWM dan sucesos y hacen tareas; las del ETM "
            "pasan en el acto, sin los ciclos del chip)",
            "PARL_IO, ECC, el esclavo SDIO y el codificador de traza: declarados; el monitor de memoria, el "
            "radio y el coexist guardan sin hacer nada",
            "E10: el nucleo LP (RV32IMAC, solo M, en la LP SRAM a 0x5000_0080), su interrupcion (la 30, "
            "de LPPERI_INTERRUPT_SOURCE), el LP timer, el LP IO, la LP UART y el LP I2C; las atomicas y "
            "LR/SC entre los dos nucleos; sin la cache ni el bus del LP (su memoria, en el acto)",
            "la depuracion: el DM 0.13 y el TAP (IDCODE 0x0000DC25) en el USB-Serial-JTAG (de fabrica) o en "
            "los pines MTMS-MTDO si los eFuse los eligen (jtag=\"pines\" en el <mcu>, o JTAG_SEL_ENABLE y "
            "GPIO15); los dos harts, el LP «unavail» hasta LPPERI_CPU, y el disparo cruzado; sin los grupos "
            "de parada (dmcs2): OpenOCD para los dos harts uno a uno",
        };
    }

private:
    CsrC6 csr_;
    std::unique_ptr<SpiFlashChip> flash_interna_;
    bool aviso_ventana_ = false;
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

    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_xtal_{"hz_xtal"};
    void engancha(BusSlave* s, sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS>& hz) {
        s->clk(reloj_falso);
        s->rst_n(rst_per_n);
        s->clk_en(siempre);
        s->clk_hz(hz);
        isk_.emplace_back(new tlm_utils::simple_initiator_socket<Esp32C6>(sc_core::sc_gen_unique_name("isk")));
        isk_.back()->bind(s->tsk);
        slaves_.push_back(s);
    }

    // --- E10: el núcleo LP y su dominio -------------------------------------------
    LpIrqC6 lp_irq_;
    uint32_t lp_src_ = 0;
    bool lp_scl_ = true, lp_sda_ = true;
    void lp_tx_proc() { gpio.refresca(); }
    bool hp_profundo_ = false;
    void fuente_lp_irq(unsigned b, bool v) {
        const uint32_t n = v ? (lp_src_ | (1u << b)) : (lp_src_ & ~(1u << b));
        if (n != lp_src_) { lp_src_ = n; core_lp.nota_irq(); }
    }
    // Una escritura en la memoria: la reserva del otro núcleo se pierde
    void escrito(int hart, uint32_t a, unsigned n) {
        if (hart != 0) core.hart.rompe_reserva(a, n);
        if (hart != 1) core_lp.hart.rompe_reserva(a, n);
    }
    void conecta_lp() {
        core_lp.ibus.bind(bus);
        core_lp.dbus.bind(bus);
        core_lp.clk_hz(hz_lp);
        core_lp.rst_n(rst_lp_n);
        core_lp.quantum = sc_core::sc_time(1, sc_core::SC_US);
        core_lp.integracion.perif_lo = c6::PERIF;
        core_lp.integracion.perif_hi = c6::PERIF_FIN;
        // La LP SRAM y la HP SRAM, sin el bus ni la PMA (no la tiene)
        core_lp.integracion.acceso_directo = [this](uint32_t a, unsigned n, uint32_t& v, bool escr, RvAcceso) {
            uint8_t* q = nullptr;
            if (a >= c6::LP_SRAM && a + n <= c6::LP_SRAM + c6::LP_SRAM_TAM) q = &lp_sram_[a - c6::LP_SRAM];
            else if (a >= c6::HP_SRAM && a + n <= c6::HP_SRAM + c6::HP_SRAM_TAM) q = &sram_[a - c6::HP_SRAM];
            if (!q) return false;
            if (escr) { for (unsigned i = 0; i < n; ++i) q[i] = uint8_t(v >> (8 * i)); core.hart.rompe_reserva(a, n); }
            else { v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(q[i]) << (8 * i); }
            return true;
        };
        lp_irq_.fuentes = [this] { return lp_src_; };
        core_lp.hart.pon_irq(&lp_irq_);
        lpperi.fuentes = [this] { return lp_src_; };
        lp_clkrst.al_escribir = [this] {
            hz_lp.write(lp_clkrst.hz_lp_fast());
            lp_uart.hz_xtal = lp_clkrst.hz_lp_uart();
            lp_i2c.hz_xtal = lp_clkrst.hz_lp_i2c();
        };
        // El PMU: el núcleo LP duerme y despierta
        pmu.lp_duerme = [this](bool reset) {
            if (reset) { rst_lp_n.write(false); core_lp.detiene(false); }
            else core_lp.detiene(true);
        };
        pmu.lp_despierta = [this](bool reset) {
            if (reset) { core_lp.detiene(false); if (encendido_) rst_lp_n.write(true); }
            else core_lp.detiene(false);
        };
        // ...y el HP: profundo (LP_AON STORE9[0]) o ligero
        pmu.hp_duerme = [this] {
            hp_profundo_ = lp_aon.store(9) & 1u;
            // El sueño profundo apaga el dominio HP: la CPU y sus periféricos
            // (los perros de TIMG incluidos), hasta el reset del despertar
            if (hp_profundo_) { rst_cpu_n.write(false); reset_perifericos(); timg0.pon_modo_flash(false); }
            else core.detiene(true);
        };
        pmu.hp_despierta = [this] {
            if (hp_profundo_) { hp_profundo_ = false; pide_reset(CAUSA_DEEP_SLEEP, true); }
            else core.detiene(false);
        };
        pmu.irq_hp = [this](bool v) { intmtx.fuente(c6::F_PMU, v); };
        pmu.irq_lp = [this](bool v) { fuente_lp_irq(c6::LPI_PMU, v); };
        // El LP timer: sus dos alarmas
        lp_timer.irq_hp = [this](bool v) { intmtx.fuente(c6::F_LP_RTC_TIMER, v); };
        lp_timer.irq_lp = [this](bool v) { fuente_lp_irq(c6::LPI_TIMER, v); };
        lp_timer.alarma = [this](unsigned t) {
            if (t == 0) pmu.fuente_hp(c6::DHP_TIMER);
            else pmu.fuente_lp(c6::DLP_TIMER);
        };
        // El LP IO: los pads 0-7 que le da LP_AON_GPIO_MUX
        lp_io.mux = [this] { return lp_aon.r(0x28) & 0xFFu; };
        lp_io.nivel = [this](unsigned p) { return gpio.nivel(p); };
        lp_io.cambia = [this] { gpio.refresca(); };
        lp_io.irq = [this](bool v) { fuente_lp_irq(c6::LPI_IO, v); };
        lp_io.despierta = [this] { pmu.fuente_lp(c6::DLP_IO); pmu.fuente_hp(c6::DHP_GPIO); };
        lp_aon.al_cambiar_mux = [this] { gpio.refresca(); };
        gpio.pad_externo = [this](unsigned p, PadDrive& d) { return lp_io.pad(p, d); };
        gpio.al_cambiar_entradas = [this] {
            lp_io.entradas();
            const bool rx = lp_io.funcion(4, 1) ? gpio.nivel(4) : true;
            if (lp_rx.read() != rx) lp_rx.write(rx);
        };
        // Lo que sacan la LP UART y el LP I2C por la función 1
        lp_io.periferico = [this](unsigned p, bool& o, bool& e) {
            switch (p) {
            case 5: o = lp_tx.read(); e = true; return true;
            case 6: o = lp_sda_; e = true; return true;
            case 7: o = lp_scl_; e = true; return true;
            default: return false;
            }
        };
        // La LP UART: su SCLK es el LP_FAST (LP_CLKRST_LPPERI.LP_UART_CLK_SEL
        // a 0: el RC_FAST o el XTAL_D2, según FAST_CLK_SEL) o el XTAL_D2 (1),
        // sin divisor [uart_ll.h: lp_uart_ll_set_baudrate]
        lp_uart.pon_c6([] { return 3u << 20; });
        lp_uart.hz_xtal = lp_clkrst.hz_lp_uart();
        lp_uart.irq = [this](bool v) { fuente_lp_irq(c6::LPI_UART, v); intmtx.fuente(c6::F_LP_UART, v); };
        // El LP I2C: el LP_FAST o el XTAL_D2 (LP_I2C_CLK_SEL), sin divisor
        lp_i2c.sclk_conf = [] { return 0u; };
        lp_i2c.hz_xtal = lp_clkrst.hz_lp_i2c();
        lp_i2c.irq = [this](bool v) { fuente_lp_irq(c6::LPI_I2C, v); intmtx.fuente(c6::F_LP_I2C, v); };
        lp_i2c.lineas = [this](bool scl, bool sda) {
            if (scl != lp_scl_ || sda != lp_sda_) { lp_scl_ = scl; lp_sda_ = sda; gpio.refresca(); }
        };
        lp_i2c.lee_scl = [this] { return gpio.nivel(7); };
        lp_i2c.lee_sda = [this] { return gpio.nivel(6); };
        lp_tx.write(true);
        lp_rx.write(true);
        SC_HAS_PROCESS(Esp32C6);
        SC_METHOD(lp_tx_proc);
        sensitive << lp_tx;
        dont_initialize();
    }

    // --- E11: el PCNT, el MCPWM y el ETM ----------------------------------------------
    void conecta_e11() {
        // PCNT: la unidad u, canal c -> PCNT_SIG_CHc_INu (101 + 4·u + c) y
        // PCNT_CTRL_CHc_INu (103 + 4·u + c)
        pcnt.entrada = [this](unsigned u, unsigned c, bool ctrl) -> const sc_core::sc_signal_in_if<bool>& {
            return gpio.sig_in[c6::SIG_PCNT + 4 * u + (ctrl ? 2 : 0) + c];
        };
        pcnt.irq = [this](bool v) { intmtx.fuente(c6::F_PCNT, v); };
        // MCPWM: SYNC0-2, F0-2 y CAP0-2 (87-95); PWM0_OUTnA/B (87-92)
        mcpwm.entrada = [this](unsigned k) -> const sc_core::sc_signal_in_if<bool>& { return gpio.sig_in[c6::SIG_PWM0 + k]; };
        mcpwm.salida = [this](unsigned op, unsigned g, bool v) { gpio.sig_out[c6::SIG_PWM0 + 2 * op + g].write(v); };
        mcpwm.irq = [this](bool v) { intmtx.fuente(c6::F_MCPWM, v); };
        mcpwm.hz_pcr = [this] { return pcr.hz_pwm(); };
        // ETM: quién da sucesos y quién hace las tareas [soc_etm_source.h]
        mcpwm.etm = [this](unsigned id) { etm.suceso(id); };
        gpio.etm = [this](unsigned id) { etm.suceso(id); };
        timg0.etm_alarma = [this] { etm.suceso(48); };
        timg1.etm_alarma = [this] { etm.suceso(49); };
        etm.tarea = [this](unsigned id) {
            if (id >= 1 && id <= 24) gpio.tarea_etm(id);
            else if (id >= 88 && id <= 97) ((id - 88) % 2 ? timg1 : timg0).tarea_etm((id - 88) / 2);
            else if (id >= 102 && id <= 123) mcpwm.tarea_etm(id - 102);
        };
    }

    // --- E9.4: los periféricos comunes con el C3 -----------------------------------
    void conecta_perifericos() {
        gdma.pon_c6();
        ledc.pon_c6([this] { return pcr.ledc_sclk(); });
        rmt.sclk_conf = [this] { return pcr.rmt_sclk(); };
        i2c.sclk_conf = [this] { return pcr.i2c_sclk(); };
        i2s.clkm_ext = [this](bool rx, bool div) { return pcr.i2s_clkm(rx, div); };
        i2s.S_SD1 = c6::SIG_I2SO_SD1;
        usj.c6 = true;
        // Las fechas de cada uno [*_reg.h de ESP-IDF para el C6]
        rmt.date = 0x2108213u; i2c.date = 0x2201172u; spi2.date = 0x2201300u; adc.date = 0x2206240u;
        usj.date = 0x2109220u; uhci.date = 0x2201100u; i2s.date = 0x2201070u;
        usj.serie = "40:4C:CA:00:C6:01";
        // Interrupciones
        usj.irq = [this](bool v) { intmtx.fuente(c6::F_USB_SJ, v); };
        ledc.irq = [this](bool v) { intmtx.fuente(c6::F_LEDC, v); };
        ledc.salida = [this](unsigned c, bool v) { gpio.sig_out[c6::SIG_LEDC0 + c].write(v); };
        rmt.irq = [this](bool v) { intmtx.fuente(c6::F_RMT, v); };
        rmt.salida = [this](unsigned c, bool v) { gpio.sig_out[c6::SIG_RMT0 + c].write(v); };
        conecta_e11();
        adc.irq = [this](bool v) { intmtx.fuente(c6::F_ADC, v); };
        adc.pad = [this](unsigned n) -> analog_net_if* { return n < 7 ? &gpio.nodo_de(n) : nullptr; };
        i2c.irq = [this](bool v) { intmtx.fuente(c6::F_I2C, v); };
        i2c.lineas = [this](bool scl, bool sda) {
            gpio.sig_out[c6::SIG_I2C_SCL].write(scl);
            gpio.sig_out[c6::SIG_I2C_SDA].write(sda);
        };
        i2c.lee_scl = [this] { return gpio.sig_in[c6::SIG_I2C_SCL].read(); };
        i2c.lee_sda = [this] { return gpio.sig_in[c6::SIG_I2C_SDA].read(); };
        twai0.irq = [this](bool v) { intmtx.fuente(c6::F_TWAI0, v); };
        twai1.irq = [this](bool v) { intmtx.fuente(c6::F_TWAI1, v); };
        twai0.salida = [this](bool v) { gpio.sig_out[c6::SIG_TWAI0].write(v); };
        twai1.salida = [this](bool v) { gpio.sig_out[c6::SIG_TWAI1].write(v); };
        twai0.rx_pin(gpio.sig_in[c6::SIG_TWAI0]);
        twai1.rx_pin(gpio.sig_in[c6::SIG_TWAI1]);
        rsa.irq = [this](bool v) { intmtx.fuente(c6::F_RSA, v); };
        hmac.clave = [this](unsigned n, uint8_t* k) { efuse.clave(n, k); };
        hmac.proposito = [this](unsigned n) { return efuse.proposito(n); };
        ds.clave = [this](uint8_t* k) { return hmac.clave_ds(k); };
        xts.clave = [this](uint8_t* k) {
            for (unsigned n = 0; n < 6; ++n)
                if (efuse.proposito(n) == 4) { efuse.clave(n, k); return true; }
            std::memset(k, 0, 32);
            return false;
        };
        xts.permitido = [] { return true; };
        // La GDMA: la HP SRAM, y sus seis fuentes
        gdma.irq_c6 = [this](unsigned c, bool sal, bool v) { intmtx.fuente((sal ? c6::F_DMA_OUT0 : c6::F_DMA_IN0) + c, v); };
        gdma.lee = [this](uint32_t a, uint8_t& b) { uint8_t* p = mem(a); if (!p) return false; b = *p; return true; };
        gdma.escribe32 = [this](uint32_t a, uint32_t v) { poke32(a, v); };
        gdma.escribe8 = [this](uint32_t a, uint8_t b) { return poke8(a, b); };
        // El AES y el SHA en modo DMA
        aes.gdma = &gdma;
        sha.gdma = &gdma;
        aes.irq = [this](bool v) { intmtx.fuente(c6::F_AES, v); };
        sha.irq = [this](bool v) { intmtx.fuente(c6::F_SHA, v); };
        // El I2S
        i2s.gdma = &gdma;
        i2s.irq = [this](bool v) { intmtx.fuente(c6::F_I2S, v); };
        i2s.pon = [this](unsigned s, bool v) { gpio.sig_out[s].write(v); };
        i2s.lee = [this](unsigned s) { return gpio.sig_in[s].read(); };
        i2s.ev_in = [this](unsigned s) -> const sc_core::sc_event& { return gpio.sig_in[s].value_changed_event(); };
        i2s.usada = [this](unsigned s) { return gpio.salida_usada(s); };
        i2s.ev_matriz = [this]() -> const sc_core::sc_event& { return gpio.ev_config(); };
        i2s.oe = [this](unsigned s, bool v) { gpio.sig_oe[s].write(v); };
        // El UHCI, con las dos UART
        uhci.gdma = &gdma;
        uhci.uart[0] = &uart0; uhci.uart[1] = &uart1;
        uhci.irq = [this](bool v) { intmtx.fuente(c6::F_UHCI0, v); };
        for (unsigned n = 0; n < 2; ++n) {
            UartEsp& u = n ? uart1 : uart0;
            u.toma_tx = [this, n](uint8_t& b) { return uhci.toma_tx(n, b); };
            u.da_rx = [this, n](uint8_t b) { return uhci.da_rx(n, b); };
            u.brk_rx = [this, n] { uhci.brk_rx(n); };
        }
        gdma.al_arrancar = [this](unsigned peri) { if (peri == UhciEsp::PERI_GDMA) uhci.despierta(); };
        gdma.al_arrancar_ent = [this](unsigned peri) { if (peri == UhciEsp::PERI_GDMA) uhci.vacia_rx(); };
        // El GP-SPI2
        spi2.gdma = &gdma;
        spi2.irq = [this](bool v) { intmtx.fuente(c6::F_SPI2, v); };
        spi2.salida = [this](bool clk, bool mosi, bool cs) {
            gpio.sig_out[c6::SIG_FSPICLK].write(clk);
            gpio.sig_out[c6::SIG_FSPID].write(mosi);
            gpio.sig_out[c6::SIG_FSPICS0].write(cs);
        };
        spi2.miso = [this] { return gpio.sig_in[c6::SIG_FSPIQ].read(); };
        // La depuración
        dm.reset(new RvDm("dm", std::vector<NucleoRv*>{&core, &core_lp}));
        dm->sba.bind(bus);
        // E10: el LP, «unavail» hasta que LPPERI_CPU lo deja (y dormido en
        // reset tampoco: lo dice el DM con en_reset)
        dm->disponible = [this](unsigned h) { return h == 0 || lpperi.lp_depurable(); };
        dm->ndmreset = [this](bool v) {
            ndm_ = v;
            if (v) {
                rst_cpu_n.write(false);
                if (encendido_) csr_.reset();
            } else if (encendido_ && !usb_rst_) {
                rst_cpu_n.write(true);
            }
        };
        tap.reset(new RvDtmJtag(*dm, IDCODE, 7));
        jtag.reset(new JtagPines("jtag", *tap, JtagPines::Pines{&gpio.nodo_de(6), &gpio.nodo_de(4),
                                                                 &gpio.nodo_de(5), &gpio.nodo_de(7), nullptr}));
        jtag->activo(false);
        hmac.jtag = [this](bool) { aplica_soft_jtag(); };
        // El USB-Serial-JTAG: su TAP es el mismo, si los eFuse lo eligen; y el
        // reset del chip por DTR/RTS
        usj.tap = tap.get();
        usj.jtag_activo = [this] { return fuente_jtag_ == FuenteJtag::Usb; };
        usj.reset_chip = [this](bool activo, bool descarga) {
            if (activo) { usb_descarga_ = descarga; pide_reset(CAUSA_USB_UART, true); usb_rst_ = true; }
            else { usb_rst_ = false; if (encendido_ && !ndm_) rst_cpu_n.write(true); }
        };
    }
    void relojes_perifericos(uint32_t off) {
        spi2.hz_apb = pcr.hz_spi2();
        if (off >= 0x70 && off <= 0x7C) i2s.reloj_cambia();
        // E11: RST_EN (bit 1) del PCNT, el ETM y el MCPWM: su reset de bloque
        // (los controladores de ESP-IDF lo dan al empezar)
        if ((off == 0x94 || off == 0x98 || off == 0x9C) && ((pcr.r(off) >> 1) & 1u)) {
            if (off == 0x94) pcnt.reset();
            else if (off == 0x98) etm.reset();
            else mcpwm.reset();
        }
    }
    bool usb_descarga_ = false, usb_rst_ = false, ndm_ = false, solo_soft_ = false;
    FuenteJtag fuente_jtag_ = FuenteJtag::Usb;
    void elige_jtag() {
        const uint32_t r0 = efuse.rd(0x30);
        const bool dis_usb = (r0 >> 9) & 1u, sel = (r0 >> 15) & 1u, dis_pad = (r0 >> 19) & 1u;
        const unsigned soft = (r0 >> 16) & 7u;
        const bool soft_dis = ((soft & 1u) + ((soft >> 1) & 1u) + ((soft >> 2) & 1u)) & 1u;
        const bool usb = sel ? (gpio.strap() & 0x10u) != 0 : !dis_usb;
        if (usb) fuente_jtag_ = dis_usb ? FuenteJtag::Ninguna : FuenteJtag::Usb;
        else fuente_jtag_ = (dis_pad || soft_dis) ? FuenteJtag::Ninguna : FuenteJtag::Pines;
        solo_soft_ = !usb && !dis_pad && soft_dis;
        aplica_soft_jtag();
    }
    // Cerrado solo por SOFT_DIS_JTAG: lo reabre el HMAC con la clave de JTAG
    void aplica_soft_jtag() {
        if (solo_soft_) fuente_jtag_ = hmac.jtag_abierto() ? FuenteJtag::Pines : FuenteJtag::Ninguna;
        jtag->activo(fuente_jtag_ == FuenteJtag::Pines);
    }

    void pon_relojes(double cpu, double apb) {
        hz_cpu.write(cpu);
        hz_apb.write(apb);
        clint.reloj_cambia();
        // El SCLK de las UART con «1» es el PLL_F80M, fijo [TRM-C6 §8]
        uart0.hz_apb = uart1.hz_apb = pcr.hz_pll_f80m;
        uart0.hz_xtal = uart1.hz_xtal = pcr.hz_xtal;
        sha.hz_apb = apb;
        hz_xtal_.write(pcr.hz_xtal);
        // Los de E9.4: el reloj de bus y, con «1», el PLL_F80M
        ledc.hz_apb = rmt.hz_apb = pcr.hz_pll_f80m;
        ledc.hz_xtal = rmt.hz_xtal = i2c.hz_xtal = i2s.hz_xtal = pcr.hz_xtal;
        ledc.hz_rc_fast = rmt.hz_rc_fast = i2c.hz_rc_fast = pcr.hz_rc_fast;
        aes.hz_apb = rsa.hz_apb = hmac.hz_apb = ds.hz_apb = xts.hz_apb = uhci.hz_apb = apb;
        spi2.hz_apb = pcr.hz_spi2();
        pcnt.hz_apb = apb;
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
        // (la del HP: el núcleo LP no tiene PMA)
        const bool lp = ext && ext->hart == 1;
        if (ext && !ext->depurador && !lp && !csr_.pma.permite(a, n, ext->acceso)) { ++n_pma_; ++n_fallos_; return; }
        if (a >= c6::HP_SRAM && a + n <= c6::HP_SRAM + c6::HP_SRAM_TAM) {
            copia(gp, &sram_[a - c6::HP_SRAM], n, p);
            if (gp.is_write()) escrito(ext ? int(ext->hart) : -1, a, n);
            return;
        }
        if (a >= c6::LP_SRAM && a + n <= c6::LP_SRAM + c6::LP_SRAM_TAM) {
            copia(gp, &lp_sram_[a - c6::LP_SRAM], n, p);
            if (gp.is_write()) escrito(ext ? int(ext->hart) : -1, a, n);
            // La latencia cruzada: el HP llega a la LP SRAM por el puente
            // (2 ciclos del APB) [de modelo]; el LP, en su ciclo
            if (!lp) d += sc_core::sc_time(2.0 / (hz_apb.read() > 0 ? hz_apb.read() : 40e6), sc_core::SC_SEC);
            return;
        }
        if (a >= c6::ROM_B && a + n <= c6::ROM_FIN) {
            if (gp.is_write() || !rom.hay()) { ++n_fallos_; return; }
            std::copy(rom.byte(a), rom.byte(a) + n, p);
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (CacheC6::es_ventana(a)) {
            std::string pq;
            if (gp.is_write()) pq = "escritura en la ventana de la Flash (la cache es de solo lectura)";
            else if (cache.lee_ventana(a, p, n, fetch, d, pq)) { gp.set_response_status(tlm::TLM_OK_RESPONSE); return; }
            if (!aviso_ventana_) {
                aviso_ventana_ = true;
                SC_REPORT_WARNING("esp32c6", (hex(a) + ": " + pq).c_str());
            }
            ++n_fallos_;
            return;
        }
        if (a >= c6::PLIC_B && a + n <= c6::CLINT_B && !fetch) {
            intpri.acceso_plic(gp, a - c6::PLIC_B);
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
        if (CacheC6::es_ventana(a) && !gp.is_write() && flash()) {
            for (unsigned i = 0; i < n; ++i) {
                uint32_t fis = 0;
                if (!cache.traduce(a + i, fis)) return i;
                gp.get_data_ptr()[i] = flash()->datos()[fis % flash()->tam()];
            }
            return n;
        }
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
        efuse.reset(); spi0.reset(); spi1.reset(); cache.reset(); sha.reset();
        gdma.reset(); spi2.reset(); ledc.reset(); rmt.reset(); adc.reset(); aes.reset(); i2c.reset();
        twai0.reset(); twai1.reset(); rsa.reset(); hmac.reset(); ds.reset(); xts.reset(); i2s.reset(); uhci.reset();
        pcnt.reset(); mcpwm.reset(); etm.reset();
        i2c_ana_mst.reset();
        for (BloqueEsp* b : {&hp_sys, &hp_apm, &lp_apm0, &tee, &assist, &mem_mon, &modem_syscon, &modem_lpcon})
            b->reset_regs();
    }
    // E10: el dominio LP, solo con el reset de chip
    void reset_lp() {
        rst_lp_n.write(false);
        core_lp.detiene(false);
        pmu.reset_chip(); lp_timer.reset_chip(); lp_io.reset_chip(); lpperi.reset();
        lp_uart.reset(); lp_i2c.reset();
        lp_tx.write(true); lp_rx.write(true); lp_scl_ = lp_sda_ = true;
        for (BloqueEsp* b : {&lp_wdt, &lp_ana, &lp_apm, &lp_tee}) b->reset_regs();
        lp_src_ = 0;
        hp_profundo_ = false;
    }

    void control_proc() {
        pon_relojes(40e6, 40e6);
        for (unsigned i = 0; i < 8; ++i) gpio.sig_oe[28 + i].write(false);       // la GPIO dedicada
        // Las salidas de la matriz que el modelo conecta: las de su tabla
        // (c6::SENALES, la que vuelca --exporta-mcu)
        for (const SenalMatriz& s : c6::SENALES)
            if (s.salida) gpio.sig_oe[s.indice].write(true);
        for (;;) {
            encendido_ = false;
            rst_cpu_n.write(false);
            rst_lp_n.write(false);
            while (!pu_alto()) wait(ev_pu_);
            // El reset de chip: todo, los STORE de LP_AON incluidos
            ++n_resets_;
            lp_aon.reset_chip();
            lp_clkrst.reset_chip();
            reset_perifericos();
            reset_lp();
            wait(1, sc_core::SC_US);
            if (!pu_alto()) continue;
            gpio.latch_arranque();
            elige_jtag();
            timg0.pon_modo_flash((gpio.strap() & 8u) != 0);
            csr_.reset();
            encendido_ = true;
            rst_cpu_n.write(!ndm_ && !usb_rst_);
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
                        // La descarga forzada (FORCE_DOWNLOAD_BOOT, o el
                        // USB-Serial-JTAG con DTR/RTS) vale para ESTE
                        // arranque: los pines no se vuelven a leer en un
                        // reset del sistema, pero lo forzado no se queda
                        gpio.vuelve_a_pines();
                        if (lp_aon.force_download() || usb_descarga_) gpio.fuerza_descarga();
                        usb_descarga_ = false;
                        timg0.pon_modo_flash((gpio.strap() & 8u) != 0);
                    }
                    lp_clkrst.anota_causa(c);
                    csr_.reset();
                    wait(100, sc_core::SC_NS);
                    rst_cpu_n.write(!ndm_ && !usb_rst_);
                }
            }
        }
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_ESP32C6_H
