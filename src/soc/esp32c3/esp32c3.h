// =============================================================================
// esp32c3.h — EL ESP32-C3 (fase E3 de doc/analisis_esp32_riscv.md)
//
// El núcleo genérico de src/rv (NucleoRv con RV_ESP32C3) con lo que Espressif
// puso alrededor. E3: el firmware se carga en la SRAM y el núcleo arranca en
// su punto de entrada («arranque directo»). E4: la ROM de verdad, la Flash
// SPI (SPI0/SPI1), la caché y la MMU, y lo que toca el arranque de ESP-IDF;
// el chip arranca por la ROM, que lee la Flash como en la placa.
//
//   memoria    SRAM0 (16 KB, bus I, 0x4037_C000) y SRAM1 (384 KB), vista por
//              el bus I en 0x4038_0000 y por el D en 0x3FC8_0000: es la MISMA
//              memoria. Por el bus I los datos van solo en palabras alineadas
//              [TRM-C3 §3.3.1]: un lb o un sh a una dirección de IRAM da un
//              fallo de acceso. Por el D no se ejecuta. RTC FAST (8 KB,
//              0x5000_0000) por los dos. La ROM (R1, soc/esp/esp_rom.h) en
//              0x4000_0000 y, su ROM1, en 0x3FF0_0000. La Flash, por la caché
//              (soc/esp/esp_cache_mmu.h) en 0x4200_0000 y 0x3C00_0000.
//   interrup.  la matriz y el INTC (soc/esp/intc_esp.h): 62 fuentes, 31
//              líneas, sin mie/mip
//   reloj      SYSTEM: XTAL de 40 MHz; tras el reset, la CPU y el APB a
//              20 MHz; con el PLL, la CPU a 80 o 160 y el APB a 80
//   periféric. UART0/1, SYSTIMER, TIMG0/1 (T0 y MWDT), GPIO con la matriz e
//              IO_MUX sobre pads eléctricos, la GPIO dedicada por los CSR,
//              RTC_CNTL (causa de reset, reset por software, STORE, force
//              download, RWDT), eFuse (de un fichero, con la revisión),
//              SYSTEM, SYSCON (con el RNG), SPI0/SPI1, EXTMEM y la MMU, SHA y
//              REGI2C; PMS, ASSIST_DEBUG, el World Controller y la radio
//              guardan; el resto, DECLARADOS (contestan cero y avisan)
//   reset      CHIP_PU: el reset de chip; los pines de arranque (GPIO2, 3, 8
//              y 9) se guardan al soltarlo. Reset de sistema y de CPU por
//              RTC_CNTL y por los MWDT, con su causa en RESET_STATE.
//   CSR        mpcer/mpcmr/mpccr (0x7E0-0x7E2, los contadores del C3) y
//              cpu_gpio_oen/in/out (0x803-0x805, la GPIO dedicada)
// =============================================================================
#ifndef STM32_SOC_ESP32C3_ESP32C3_H
#define STM32_SOC_ESP32C3_ESP32C3_H

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
#include "c3_desc.h"
#include "c3_sistema.h"
#include "../esp/intc_esp.h"
#include "../esp/esp_rom.h"
#include "../esp/esp_cache_mmu.h"
#include "../../periph/esp/spi_mem.h"
#include "../../periph/esp/sha_esp.h"
#include "../../parts/spi_flash.h"
#include "../../rv/hart/nucleo_rv.h"
#include "../../rv/debug/rv_dm.h"
#include "../../rv/debug/rv_dtm_jtag.h"
#include "../../rv/debug/jtag_pines.h"
#include "../../periph/esp/uart_esp.h"
#include "../../periph/esp/systimer_esp.h"
#include "../../periph/esp/timg_esp.h"
#include "../../periph/esp/gpio_esp.h"
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
#include "../../periph/bloque_declarado.h"
#include "../../common/imagen_fw.h"
#include "../../common/analog_net.h"

namespace stm32 {

// La ROM de verdad de cada revisión (R1): soc/esp32c3/rom/gen_rom_c3.py
namespace c3 {
inline const uint32_t ROM_REV3[] = {
#include "rom/rom_c3_rev3.inc"
};
inline const uint32_t ROM_REV101[] = {
#include "rom/rom_c3_rev101.inc"
};
} // namespace c3

// Los CSR propios del C3 [TRM-C3 §1.3, §5.6]: los contadores de rendimiento
// de máquina (mpcer/mpcmr/mpccr, 0x7E0-0x7E2) y su copia de usuario
// (upcer/upcmr/upccr, 0x800-0x802, que la ROM programa al arrancar), la GPIO
// dedicada (0x803-0x805) y `ustatus` (0x000), que existe aunque no haya
// interrupciones de usuario: la ROM lo escribe a cero; lee 0 y no guarda.
class CsrC3 : public rv_csr_ext_if {
public:
    explicit CsrC3(GpioEsp& g) : g_(g) { reset(); }
    bool atiende(uint16_t a) const override {
        return a == 0x000 || (a >= 0x7E0 && a <= 0x7E2) || (a >= 0x800 && a <= 0x805);
    }
    bool lee(RvHart& h, uint16_t a, uint32_t& v) override {
        switch (a) {
        case 0x000: v = 0; return true;
        case 0x7E0: case 0x7E1: case 0x7E2: return pc_[0].lee(h, a - 0x7E0, v);
        case 0x800: case 0x801: case 0x802: return pc_[1].lee(h, a - 0x800, v);
        case 0x803: v = g_.cpu_oen(); return true;
        case 0x804: v = g_.cpu_gpio_in(); return true;
        case 0x805: v = g_.cpu_out(); return true;
        default: return false;
        }
    }
    bool escribe(RvHart& h, uint16_t a, uint32_t v) override {
        switch (a) {
        case 0x000: return true;
        case 0x7E0: case 0x7E1: case 0x7E2: pc_[0].escribe(h, a - 0x7E0, v); return true;
        case 0x800: case 0x801: case 0x802: pc_[1].escribe(h, a - 0x800, v); return true;
        case 0x803: g_.cpu_gpio_oen(uint8_t(v)); return true;
        case 0x804: return true;                         // solo de lectura
        case 0x805: g_.cpu_gpio_out(uint8_t(v)); return true;
        default: return false;
        }
    }
    // mpcer/mpcmr salen del reset contando ciclos: ESP-IDF lee mpccr para la
    // marca de tiempo del registro y para esp_rom_delay_us sin habilitarlo
    // nunca [deducido del código de ESP-IDF; el TRM no da el valor de reset].
    // Los de usuario, parados: la ROM los pone a 1 al arrancar.
    void reset() { pc_[0] = Contador(); pc_[0].er = 1; pc_[0].mr = 1; pc_[1] = Contador(); }
private:
    // mpcer: bit 0 ciclos, bit 1 instrucciones (el resto de sucesos: de modelo,
    // se cuentan como instrucciones). mpcmr bit 0: contar.
    struct Contador {
        uint32_t er = 0, mr = 0, base = 0;
        uint64_t desde = 0;
        static uint64_t fuente(RvHart& h, uint32_t e) { return (e & 1u) ? h.ciclos() : h.instret(); }
        uint32_t cuenta(RvHart& h) const {
            if (!(mr & 1u) || !er) return base;
            return base + uint32_t(fuente(h, er) - desde);
        }
        bool lee(RvHart& h, unsigned i, uint32_t& v) const {
            v = i == 0 ? er : i == 1 ? mr : cuenta(h);
            return true;
        }
        void escribe(RvHart& h, unsigned i, uint32_t v) {
            if (i == 0) { base = cuenta(h); desde = fuente(h, er = v & 0x7FFu); }
            else if (i == 1) { base = cuenta(h); mr = v & 3u; desde = fuente(h, er); }
            else { base = v; desde = fuente(h, er); }
        }
    };
    GpioEsp& g_;
    Contador pc_[2];
};

class Esp32C3 : public sc_core::sc_module {
public:
    const McuCapsC3& mcu;

    // --- Relojes y resets (señales del SoC) -------------------------------------
    // Los escriben el control del reset y el núcleo (al tocar SYSTEM)
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_cpu{"hz_cpu"}, hz_apb{"hz_apb"};
    // rst_cpu_n lo escriben el control del reset y el ndmreset del DM (desde
    // el hilo del depurador)
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_cpu_n{"rst_cpu_n"};
    sc_core::sc_signal<bool> rst_per_n{"rst_per_n"}, siempre{"siempre"}, reloj_falso{"reloj_falso"};

    // --- El bus y el núcleo -----------------------------------------------------------
    tlm_utils::multi_passthrough_target_socket<Esp32C3> bus{"bus"};
    NucleoRv core;

    // --- Periféricos (el orden importa: gpio antes de las UART) --------------------
    GpioEsp        gpio;
    IoMuxEsp       iomux;
    MatrizIntEspV1 intmtx;
    SystemC3       system;
    RtcCntlC3      rtc;
    EfuseC3        efuse;
    UartEsp        uart0, uart1;
    SystimerEsp    systimer;
    TimgEsp        timg0, timg1;
    SpiMemEsp      spi0, spi1;
    CacheMmuEsp    cache;
    ShaEsp         sha;
    RegI2cC3       regi2c;
    SysconC3       syscon;
    BloqueGuardado pms, assist, wcl, efuse_trm;
    // Lo de la radio que el arranque enciende y apaga (FE, FE2, NRX, BB):
    // guardan, sin radio detrás (E12)
    BloqueGuardado fe2, fe, nrx, bb;
    UsbSerialJtagEsp usj;          // E7: D- en GPIO18, D+ en GPIO19
    LedcEsp        ledc;           // E8
    RmtEsp         rmt;
    SarAdcEsp      adc;            // y el sensor de temperatura
    AesEsp         aes;
    I2cEsp         i2c;
    TwaiEsp        twai;
    GdmaEsp        gdma;
    SpiGpEsp       spi2;
    RsaEsp         rsa;
    RomEsp         rom{c3::ROM0_I, c3::ROM_I_FIN - c3::ROM0_I};
    std::vector<std::unique_ptr<BloqueDeclarado>> declarados;

    // CHIP_PU, con una resistencia de subida que hace las veces del RC de la
    // placa mientras hay alimentación
    AnalogNet chip_pu{"chip_pu"};

    // --- La depuración (E6) ---------------------------------------------------------
    // El Debug Module 0.13 de E2 con las capacidades del C3 (RV_ESP32C3.dbg: 8
    // triggers, program buffer de 16, SBA de palabra), su DTM JTAG con el
    // IDCODE del C3 (0x00005C25, el `_CPUTAPID` de target/esp32c3.cfg) y el
    // TAP en los pines MTMS (GPIO4), MTDI (GPIO5), MTCK (GPIO6) y MTDO (GPIO7).
    // Qué llega al TAP lo eligen los eFuse al soltar el reset [TRM-C3 §7.3,
    // «JTAG signal source»]:
    //   DIS_PAD_JTAG, o SOFT_DIS_JTAG con un número impar de unos: los pines no
    //   JTAG_SEL_ENABLE = 0: el USB-Serial-JTAG, salvo DIS_USB_JTAG = 1: los pines
    //   JTAG_SEL_ENABLE = 1: el pin de arranque GPIO10 (1 USB, 0 pines)
    // Un C3 de fábrica (todo a 0) lo tiene en el USB (E7): los pines piden
    // quemar DIS_USB_JTAG, que en el simulador es jtag="pines" en el <mcu>.
    // ndmreset es un reset de la CPU (como el de esp_restart()) que la tiene
    // en reset mientras está puesto: con resethaltreq, `monitor reset halt`
    // para en la primera instrucción de la ROM, que vuelve a arrancar la
    // aplicación. OpenOCD hace además el del sistema, con RTC_CNTL_SW_SYS_RST.
    static constexpr uint32_t IDCODE = 0x00005C25u;
    enum class FuenteJtag { Ninguna, Pines, Usb };
    std::unique_ptr<RvDm> dm;
    std::unique_ptr<RvDtmJtag> tap;
    std::unique_ptr<JtagPines> jtag;
    FuenteJtag fuente_jtag() const { return fuente_jtag_; }

    Esp32C3(sc_core::sc_module_name nm, const McuCapsC3& m, const Cableado& cab = Cableado())
        : sc_core::sc_module(nm), mcu(m), core("core", *m.nucleo, 0),
          gpio("gpio", c3::GPIO_B, pines_c3(), bonded(m), cab),
          iomux("io_mux", c3::IO_MUX_B, gpio),
          intmtx("intmtx", c3::INTMTX_B, c3::N_FUENTES),
          system("system", c3::SYSTEM_B), rtc("rtc_cntl", c3::RTC_CNTL_B),
          efuse("efuse", c3::EFUSE_B, m.revision),
          uart0("uart0", c3::UART0_B, gpio.sig_out[c3::SIG_U0TXD], gpio.sig_in[c3::SIG_U0RXD]),
          uart1("uart1", c3::UART1_B, gpio.sig_out[c3::SIG_U1TXD], gpio.sig_in[c3::SIG_U1RXD]),
          systimer("systimer", c3::SYSTIMER_B),
          timg0("timg0", c3::TIMG0_B), timg1("timg1", c3::TIMG1_B),
          spi0("spi0", c3::SPI0_B, c3::REGS_SPI_MEM), spi1("spi1", c3::SPI1_B, c3::REGS_SPI_MEM),
          cache("extmem", c3::EXTMEM_B, c3::REGS_EXTMEM, CACHE_C3, spi0),
          sha("sha", c3::SHA_B), regi2c("regi2c", c3::REGI2C_B),
          syscon("syscon", c3::SYSCON_B), pms("pms", c3::PMS_B, 0x1000),
          assist("assist_debug", c3::ASSIST_B, 0x1000), wcl("world_controller", c3::WCL_B, 0x1000),
          efuse_trm("efuse_tabla_trm", c3::EFUSE_TRM_B, 0x1000),
          fe2("fe2", c3::FE2_B, 0x1000), fe("fe", c3::FE_B, 0x1000), nrx("nrx", c3::NRX_B, 0x400),
          bb("bb", c3::BB_B, 0x1000),
          usj("usb_serial_jtag", c3::USB_SJ_B, gpio.nodo_de(19), gpio.nodo_de(18)),
          ledc("ledc", c3::LEDC_B), rmt("rmt", c3::RMT_B), adc("apb_saradc", c3::ADC_B), aes("aes", c3::AES_B), i2c("i2c", c3::I2C_B), twai("twai", c3::TWAI_B), gdma("gdma", c3::GDMA_B), spi2("spi2", c3::SPI2_B),
          rsa("rsa", c3::RSA_B),
          csr_(gpio), sram_(c3::SRAM_TAM, 0), rtc_fast_(c3::RTC_FAST_TAM, 0) {
        pon_rom_omision();
        bus.register_b_transport(this, &Esp32C3::b_transport);
        bus.register_transport_dbg(this, &Esp32C3::transport_dbg);
        core.ibus.bind(bus);
        core.dbus.bind(bus);
        core.clk_hz(hz_cpu);
        core.rst_n(rst_cpu_n);
        core.quantum = sc_core::sc_time(100, sc_core::SC_NS);
        core.integracion.perif_lo = c3::PERIF;
        core.integracion.perif_hi = c3::PERIF_FIN;
        core.hart.pon_irq(&intmtx);
        core.hart.pon_csr_ext(&csr_);
        intmtx.aviso = [this] { core.nota_irq(); };
        // El reloj del MSPI: 80 MHz con el PLL, el XTAL sin él
        auto hz_mspi = [this] { return system.hz_apb() >= 80e6 ? 80e6 : system.hz_xtal; };
        spi0.hz_origen = hz_mspi;
        spi1.hz_origen = hz_mspi;
        if (m.flash_interna) {
            flash_interna_.reset(new SpiFlashChip(*busca_modelo_flash(m.flash_interna > (4u << 20) ? "GD25Q64" : "GD25Q32"),
                                                  "Flash interna"));
            pon_flash(flash_interna_.get());
        }

        struct D { uint32_t b; const char* n; const char* f; };
        static const D dec[] = {
            {c3::UHCI_B, "UHCI0", "E8"}, 
            {c3::I2S_B, "I2S", "E8"}, 
            {c3::DS_B, "DS", "E8"}, {c3::HMAC_B, "HMAC", "E8"},
            
            {c3::XTS_AES_B, "XTS_AES", "E4"},
        };
        for (const D& d : dec)
            declarados.emplace_back(new BloqueDeclarado(sc_core::sc_gen_unique_name("decl"),
                                                        d.b, 0x1000, d.n, d.f));
        std::vector<BusSlave*> todos{&gpio, &iomux, &intmtx, &system, &rtc, &efuse, &uart0,
                                     &uart1, &systimer, &timg0, &timg1, &spi0, &spi1, &cache, &sha, &regi2c,
                                     &syscon, &pms,
                                     &assist, &wcl, &efuse_trm, &fe2, &fe, &nrx, &bb, &usj, &ledc, &rmt, &adc, &aes, &i2c, &gdma, &spi2, &twai, &rsa};
        for (auto& d : declarados) todos.push_back(d.get());
        for (BusSlave* s : todos) engancha(s);

        // Interrupciones
        uart0.irq = [this](bool v) { intmtx.fuente(c3::F_UART0, v); };
        uart1.irq = [this](bool v) { intmtx.fuente(c3::F_UART1, v); };
        gpio.irq = [this](bool v) { intmtx.fuente(c3::F_GPIO, v); };
        usj.irq = [this](bool v) { intmtx.fuente(c3::F_USB_SJ, v); };
        ledc.irq = [this](bool v) { intmtx.fuente(c3::F_LEDC, v); };
        ledc.salida = [this](unsigned c, bool v) { gpio.sig_out[c3::SIG_LEDC0 + c].write(v); };
        rmt.irq = [this](bool v) { intmtx.fuente(c3::F_RMT, v); };
        rmt.salida = [this](unsigned c, bool v) { gpio.sig_out[c3::SIG_RMT0 + c].write(v); };
        adc.irq = [this](bool v) { intmtx.fuente(c3::F_ADC, v); };
        i2c.irq = [this](bool v) { intmtx.fuente(c3::F_I2C, v); };
        twai.irq = [this](bool v) { intmtx.fuente(c3::F_TWAI, v); };
        rsa.irq = [this](bool v) { intmtx.fuente(c3::F_RSA, v); };
        twai.salida = [this](bool v) { gpio.sig_out[c3::SIG_TWAI].write(v); };
        twai.rx_pin(gpio.sig_in[c3::SIG_TWAI]);
        gdma.irq = [this](unsigned c, bool v) { intmtx.fuente(c3::F_DMA0 + c, v); };
        gdma.lee = [this](uint32_t a, uint8_t& b) { uint8_t* p = mem(a, false); if (!p) return false; b = *p; return true; };
        gdma.escribe32 = [this](uint32_t a, uint32_t v) { poke32(a, v); };
        spi2.gdma = &gdma;
        spi2.irq = [this](bool v) { intmtx.fuente(c3::F_SPI2, v); };
        spi2.salida = [this](bool clk, bool mosi, bool cs) {
            gpio.sig_out[c3::SIG_FSPICLK].write(clk);
            gpio.sig_out[c3::SIG_FSPID].write(mosi);
            gpio.sig_out[c3::SIG_FSPICS0].write(cs);
        };
        spi2.miso = [this] { return gpio.sig_in[c3::SIG_FSPIQ].read(); };
        i2c.lineas = [this](bool scl, bool sda) {
            gpio.sig_out[c3::SIG_I2C_SCL].write(scl);
            gpio.sig_out[c3::SIG_I2C_SDA].write(sda);
        };
        i2c.lee_scl = [this] { return gpio.sig_in[c3::SIG_I2C_SCL].read(); };
        i2c.lee_sda = [this] { return gpio.sig_in[c3::SIG_I2C_SDA].read(); };
        adc.pad = [this](unsigned n) -> analog_net_if* { return n < 22 ? &gpio.nodo_de(n) : nullptr; };
        systimer.irq = [this](unsigned c, bool v) { intmtx.fuente(c3::F_SYSTIMER0 + c, v); };
        timg0.irq = [this](unsigned b, bool v) { intmtx.fuente(b ? c3::F_TG0_WDT : c3::F_TG0_T0, v); };
        timg1.irq = [this](unsigned b, bool v) { intmtx.fuente(b ? c3::F_TG1_WDT : c3::F_TG1_T0, v); };
        system.irq_sw = [this](unsigned n, bool v) { intmtx.fuente(c3::F_SW0 + n, v); };
        // Relojes
        system.al_cambiar_reloj = [this](double cpu, double apb) { pon_relojes(cpu, apb); };
        // Resets
        rtc.reset_sw = [this](bool sis) { pide_reset(sis ? CAUSA_SW_SIS : CAUSA_SW_CPU, sis); };
        timg0.reset_wdt = [this](bool sis) { pide_reset(sis ? CAUSA_MWDT0_SIS : CAUSA_MWDT0_CPU, sis); };
        timg1.reset_wdt = [this](bool sis) { pide_reset(sis ? CAUSA_MWDT1_SIS : CAUSA_MWDT1_CPU, sis); };
        rtc.rwdt = [this](unsigned a) {
            if (a == 2) pide_reset(CAUSA_RWDT_CPU, false);
            else if (a == 3) pide_reset(CAUSA_RWDT_SIS, true);
            else if (a == 4) { rtc_pend_ = true; pide_reset(CAUSA_RWDT_RTC, true); }
            // 1, la interrupción (fuente RTC_CORE): sin modelar
        };
        pon_flashboot(true);

        // La depuración
        dm.reset(new RvDm("dm", std::vector<NucleoRv*>{&core}));
        dm->sba.bind(bus);
        dm->ndmreset = [this](bool v) {
            ndm_ = v;
            if (v) {
                // Un reset de la CPU, como el de esp_restart(): la CPU queda
                // en reset mientras dure (con lo que va con ella: PMS, World
                // Controller, ASSIST_DEBUG), y los periféricos siguen como
                // estaban. openocd-esp32 cuenta con eso: desactiva los perros
                // y luego vuelve a poner ndmreset (esp32c3_soc_reset)
                rst_cpu_n.write(false);
                if (encendido_) { reset_cpu_perif(); csr_.reset(); }
            } else if (encendido_ && !usb_rst_) {
                rst_cpu_n.write(true);
            }
        };
        tap.reset(new RvDtmJtag(*dm, IDCODE, 7));
        jtag.reset(new JtagPines("jtag", *tap, JtagPines::Pines{&gpio.nodo_de(6), &gpio.nodo_de(4),
                                                                 &gpio.nodo_de(5), &gpio.nodo_de(7), nullptr}));
        jtag->activo(false);
        // El USB-Serial-JTAG: su TAP es el mismo, si los eFuse lo eligen; y el
        // reset del chip por DTR/RTS (el de esptool --before usb-reset)
        usj.tap = tap.get();
        usj.jtag_activo = [this] { return fuente_jtag_ == FuenteJtag::Usb; };
        usj.reset_chip = [this](bool activo, bool descarga) {
            usb_rst_ = activo;
            if (activo) {
                // La descarga la pide el periférico al reset que provoca, de
                // una vez (no deja nada en RTC_CNTL: el siguiente arranca
                // normal)
                usb_descarga_ = descarga;
                rtc_pend_ = true;                    // un reset de CHIP: los pines de arranque, otra vez
                pide_reset(CAUSA_USB_UART, true);
            } else if (encendido_ && !ndm_) {
                rst_cpu_n.write(true);
            }
        };

        siempre.write(true);
        rst_per_n.write(true);
        rst_cpu_n.write(false);
        d_pu_ = chip_pu.register_driver("rc_chip_pu");
        chip_pu.set_hiz(d_pu_);
        SC_HAS_PROCESS(Esp32C3);
        SC_THREAD(control_proc);
        SC_METHOD(chip_pu_proc);
        sensitive << chip_pu.value_changed_event();
        dont_initialize();
    }

    // --- Lo que hace la placa -------------------------------------------------------
    // Alimentación: con ella, CHIP_PU sube por su RC (salvo que algo lo baje)
    void alimenta(bool si) {
        alimentado_ = si;
        usj.alimenta(si);
        if (si) chip_pu.set_drive(d_pu_, 3.3f, 10e3f);
        else    chip_pu.set_drive(d_pu_, 0.0f, 10e3f);
    }
    bool encendido() const { return encendido_; }
    // Las causas de reset de RESET_STATE [TRM-C3 tabla 6.1-1]
    enum : unsigned {
        CAUSA_POR = 1, CAUSA_SW_SIS = 3, CAUSA_MWDT0_SIS = 7, CAUSA_MWDT1_SIS = 8,
        CAUSA_MWDT0_CPU = 11, CAUSA_SW_CPU = 12, CAUSA_MWDT1_CPU = 17,
        CAUSA_RWDT_SIS = 9, CAUSA_RWDT_CPU = 13, CAUSA_RWDT_RTC = 16,
        CAUSA_USB_UART = 21,
    };

    // --- El firmware ------------------------------------------------------------------
    // Carga un ELF en la SRAM (por cualquiera de sus vistas) y la RTC FAST, y
    // apunta el arranque a su e_entry. "" si bien.
    std::string carga_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::string e = lee_elf(ruta, t, ELF_RISCV);
        if (!e.empty()) return e;
        for (const TrozoFw& x : t)
            for (size_t i = 0; i < x.datos.size(); ++i)
                if (!poke8(uint32_t(x.dir + i), x.datos[i]))
                    return ruta + ": la direccion " + hex(uint32_t(x.dir + i)) +
                           " no es SRAM ni RTC FAST del ESP32-C3 (sin ROM ni Flash todavia: E4)";
        uint32_t ent = 0;
        if (!entrada_elf(ruta, ent)) return ruta + ": sin punto de entrada";
        core.hart.pon_pc_arranque(ent);
        cargado_ = true;
        pon_flashboot(false);
        return "";
    }
    bool cargado() const { return cargado_; }

    // --- La Flash -----------------------------------------------------------------
    // La del encapsulado (C3FH4...) o la de la placa, en el CS del SPI0/SPI1
    void pon_flash(SpiFlashChip* f) { spi0.flash = spi1.flash = f; }
    SpiFlashChip* flash() const { return spi0.flash; }
    SpiFlashChip* flash_interna() const { return flash_interna_.get(); }
    // Una imagen en la Flash desde `off` (un .bin de esptool: el bootloader,
    // la tabla de particiones, la aplicación o una imagen completa) y el
    // arranque por la ROM. "" si bien.
    std::string carga_imagen(const std::string& ruta, uint32_t off) {
        if (!flash()) return "el ESP32-C3 no tiene Flash: ni en el encapsulado ni en la placa";
        if (!rom.hay()) return "con rom=ninguna no se arranca desde la Flash";
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
        pon_flashboot(true);
        cargado_ = true;
    }

    // --- La ROM (R1/R3) ----------------------------------------------------------
    // La de la revisión del descriptor: v0.3/v0.4 -> esp32c3_rev3, v1.1 -> rev101
    void pon_rom_omision() {
        if (mcu.revision >= 100) rom.pon_empotrada(c3::ROM_REV101, sizeof c3::ROM_REV101 / 4, 101, "esp32c3_rev101_rom.elf");
        else rom.pon_empotrada(c3::ROM_REV3, sizeof c3::ROM_REV3 / 4, 3, "esp32c3_rev3_rom.elf");
    }
    // `rom=`: "ninguna" (R3), "omision", "rev3" o "rev101" (las que van
    // dentro), o un ELF de esp-rom-elfs. "" si bien.
    std::string pon_rom(const std::string& v) {
        if (v == "ninguna") { rom.quita(); return ""; }
        if (v == "omision" || v.empty()) { pon_rom_omision(); return ""; }
        if (v == "rev3") { rom.pon_empotrada(c3::ROM_REV3, sizeof c3::ROM_REV3 / 4, 3, "esp32c3_rev3_rom.elf"); return ""; }
        if (v == "rev101") { rom.pon_empotrada(c3::ROM_REV101, sizeof c3::ROM_REV101 / 4, 101, "esp32c3_rev101_rom.elf"); return ""; }
        return rom.carga_elf(v);
    }
    // La revisión de ROM que corresponde a la del chip
    unsigned rev_rom_chip() const { return mcu.revision >= 100 ? 101u : 3u; }
    // "" si la ROM casa con la revisión del chip; si no, el aviso
    std::string aviso_rom() const {
        if (!rom.hay() || rom.revision() == RomEsp::REV_DESCONOCIDA) return "";
        if (rom.revision() == rev_rom_chip()) return "";
        return "la ROM es la de la revision " + std::to_string(rom.revision()) +
               " y el chip es v" + std::to_string(mcu.revision / 100) + "." +
               std::to_string(mcu.revision % 100) + ": esa pareja no existe en el silicio (cada "
               "revision trae su ROM, con funciones en otras direcciones); lo que dependa de "
               "la ROM no se parecera a ninguna placa";
    }
    bool poke8(uint32_t a, uint8_t v) {
        uint8_t* p = mem(a, false);
        if (!p) return false;
        *p = v;
        return true;
    }
    bool peek8(uint32_t a, uint8_t& v) {
        uint8_t* p = mem(a, false);
        if (!p) return false;
        v = *p;
        return true;
    }
    uint32_t peek32(uint32_t a) {
        uint32_t v = 0; uint8_t b;
        for (unsigned i = 0; i < 4; ++i) if (peek8(a + i, b)) v |= uint32_t(b) << (8 * i);
        return v;
    }
    void poke32(uint32_t a, uint32_t v) { for (unsigned i = 0; i < 4; ++i) poke8(a + i, uint8_t(v >> (8 * i))); }

    // Sin firmware: un bucle de wfi al principio de la SRAM1
    void aparca_en_wfi() {
        poke32(c3::SRAM1_I, 0x10500073u);           // wfi
        poke32(c3::SRAM1_I + 4, 0xFFDFF06Fu);       // j .-4
        core.hart.pon_pc_arranque(c3::SRAM1_I);
        cargado_ = true;
        pon_flashboot(false);
    }

    uint64_t resets() const { return n_resets_; }
    uint64_t accesos_rom() const { return n_rom_; }
    uint64_t fallos_bus() const { return n_fallos_; }
    // Cada acceso que acaba en error de bus (dirección, bytes, escritura)
    std::function<void(uint32_t, unsigned, bool)> al_fallar;

    std::vector<std::string> limitaciones() const {
        return {
            "la ROM es la de verdad (R1) y la Flash va por SPI0/SPI1 y la cache (E4); un ELF "
            "bare-metal se carga en la SRAM y arranca SIN ROM (el arranque directo de E3)",
            "la cache: lineas y fallos de verdad (16 KB, 8 vias); sin bloqueo de lineas, "
            "precarga, cifrado ni PMS de las ventanas",
            "con un ELF en la SRAM (arranque directo) el MWDT0 y el RWDT salen apagados; "
            "arrancando por la ROM, en flashboot como en el silicio",
            "PERIP_CLK_EN0/1 se guardan pero no cortan el reloj de los perifericos",
            "el super watchdog (SWD), el brownout, el BBPLL (REGI2C) y los modos de bajo "
            "consumo: guardan, sin modelar",
            "eFuse: solo lectura (de un fichero o la de omision, con la revision del chip); "
            "programar no programa",
            "el RNG (SYSCON_RND_DATA) es un xorshift32 de semilla fija: igual en cada simulacion",
            "el LEDC (E8): 6 canales y 4 temporizadores, con el fundido; sin la fracción del ciclo",
            "el RMT (E8): transmisión por los canales 0 y 1; sin recepción, portadora, envoltorio de la memoria ni FIFO",
            "el SAR ADC (E8): lecturas sueltas de ADC1 y ADC2 y el sensor de temperatura; sin el modo continuo (DMA) ni calibración",
            "el AES (E8): modo típico, AES-128 y AES-256; sin DMA",
            "el GP-SPI2 (E8): maestro, una línea, modos 0 y 3, de W0-W15 o de la GDMA; sin esclavo, dual/quad ni CS 1-5",
            "la GDMA (E8): canales de salida con sus descriptores; sin los de entrada",
            "el I2C (E8): maestro con su lista de órdenes, bit a bit y con clock stretching; sin esclavo, arbitraje ni plazos de SCL",
            "el TWAI (E8): el SJA1000 en PeliCAN sobre el motor CAN común, con la FIFO de 64 bytes y los dos modos de filtro; "
            "sin SELF_TEST (sin asentimiento), ARB_LOST_CAP exacto ni la recuperación de 128 x 11 bits",
            "el RSA (E8): MODEXP, MODMULT y MULT hasta 3072 bits, por Montgomery con r y M', con SEARCH y "
            "CONSTANT_TIME; los tiempos, de modelo (ajustados a la tabla 20.3-1 del TRM)",
            "declarados: UHCI, I2S, DS, HMAC "
            "y XTS_AES (E8); la radio (FE, NRX, BB) guarda sin hacer nada",
            "el USB-Serial-JTAG (E7): CDC-ACM y JTAG, con el reset por DTR/RTS; sin bajo consumo, "
            "sin EXCHG_PINS ni PHY de fuera",
            "la depuracion: el DM 0.13 y el TAP (IDCODE 0x00005C25) en los pines MTMS-MTDO si "
            "los eFuse los eligen (jtag=\"pines\" en el <mcu>; de fabrica van al USB-Serial-JTAG, "
            "E7); la region del DM en 0x2000_0000 (el program buffer en 0x2000_0340, de modelo)",
        };
    }

    // jtag="pines": como quemar EFUSE_DIS_USB_JTAG (la fuente se elige en el
    // próximo reset de chip)
    void jtag_en_pines(bool si) { efuse.pon_bit(0x30, 9, si); }

private:
    CsrC3 csr_;
    bool rtc_pend_ = false, ndm_ = false, usb_rst_ = false, usb_descarga_ = false;
    FuenteJtag fuente_jtag_ = FuenteJtag::Usb;
    void elige_jtag() {
        const uint32_t r0 = efuse.rd(0x30);
        const bool dis_usb = (r0 >> 9) & 1u, sel = (r0 >> 15) & 1u, dis_pad = (r0 >> 19) & 1u;
        const unsigned soft = (r0 >> 16) & 7u;
        const bool soft_dis = ((soft & 1u) + ((soft >> 1) & 1u) + ((soft >> 2) & 1u)) & 1u;
        const bool g10 = gpio.nodo_de(10).voltage() > 1.65f;
        const bool usb = sel ? g10 : !dis_usb;
        if (usb) fuente_jtag_ = dis_usb ? FuenteJtag::Ninguna : FuenteJtag::Usb;
        else fuente_jtag_ = (dis_pad || soft_dis) ? FuenteJtag::Ninguna : FuenteJtag::Pines;
        jtag->activo(fuente_jtag_ == FuenteJtag::Pines);
    }
    // Los perros guardianes del arranque desde la Flash (RWDT y MWDT0 en
    // flashboot): como en el silicio cuando arranca la ROM; apagados con un
    // ELF en la SRAM (E3)
    void pon_flashboot(bool si) { timg0.flashboot = si; rtc.flashboot = si; }
    // GPIO_STRAP[3] (GPIO9) a 1: arranque desde la Flash
    void modo_flash() {
        const bool f = (gpio.strap() & 8u) != 0;
        timg0.pon_modo_flash(f);
        rtc.pon_modo_flash(f);
    }
    std::unique_ptr<SpiFlashChip> flash_interna_;
    bool aviso_ventana_ = false;
    std::vector<uint8_t> sram_, rtc_fast_;
    std::vector<std::unique_ptr<tlm_utils::simple_initiator_socket<Esp32C3>>> isk_;
    std::vector<BusSlave*> slaves_;
    int d_pu_ = -1;
    bool alimentado_ = false, encendido_ = false, cargado_ = false;
    bool aviso_rom_ = false;
    uint64_t n_resets_ = 0, n_rom_ = 0, n_fallos_ = 0;
    sc_core::sc_event ev_reset_, ev_pu_;
    unsigned causa_pend_ = 0;
    bool sis_pend_ = false;

    static std::vector<bool> bonded(const McuCapsC3& m) {
        std::vector<bool> b(c3::N_GPIO);
        for (unsigned i = 0; i < c3::N_GPIO; ++i) b[i] = m.enc.bonded(0, i);
        return b;
    }
    static std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "0x%08X", v); return b; }

    void engancha(BusSlave* s) {
        s->clk(reloj_falso);
        s->rst_n(rst_per_n);
        s->clk_en(siempre);
        s->clk_hz(hz_apb);
        isk_.emplace_back(new tlm_utils::simple_initiator_socket<Esp32C3>(
            sc_core::sc_gen_unique_name("isk")));
        isk_.back()->bind(s->tsk);
        slaves_.push_back(s);
    }

    void pon_relojes(double cpu, double apb) {
        hz_cpu.write(cpu);
        hz_apb.write(apb);
        uart0.hz_apb = uart1.hz_apb = apb;
        timg0.hz_apb = timg1.hz_apb = apb;
        sha.hz_apb = apb;
        ledc.hz_apb = rmt.hz_apb = aes.hz_apb = spi2.hz_apb = rsa.hz_apb = apb;
    }

    // La memoria: puntero al byte, o nullptr. `fetch`: por el bus I.
    uint8_t* mem(uint32_t a, bool fetch) {
        (void)fetch;
        if (a >= c3::SRAM0_I && a < c3::SRAM_I_FIN) return &sram_[a - c3::SRAM0_I];
        if (a >= c3::SRAM1_D && a < c3::SRAM1_D_FIN) return &sram_[a - c3::SRAM1_D + 16 * 1024];
        if (a >= c3::RTC_FAST && a < c3::RTC_FAST + c3::RTC_FAST_TAM) return &rtc_fast_[a - c3::RTC_FAST];
        if (a >= c3::ROM0_I && a < c3::ROM_I_FIN) return rom.byte(a);
        if (a >= c3::ROM1_D && a < c3::ROM1_D_FIN) return rom.byte(a - c3::ROM1_D + 0x40040000u);
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
        // SRAM por el bus I: datos solo en palabras alineadas
        if (a >= c3::SRAM0_I && a + n <= c3::SRAM_I_FIN) {
            if (!fetch && !(ext && ext->depurador) && (n != 4 || (a & 3u))) { ++n_fallos_; return; }
            copia(gp, &sram_[a - c3::SRAM0_I], n, p);
            return;
        }
        if (a >= c3::SRAM1_D && a + n <= c3::SRAM1_D_FIN) {
            if (fetch) { ++n_fallos_; return; }
            copia(gp, &sram_[a - c3::SRAM1_D + 16 * 1024], n, p);
            return;
        }
        if (a >= c3::RTC_FAST && a + n <= c3::RTC_FAST + c3::RTC_FAST_TAM) {
            copia(gp, &rtc_fast_[a - c3::RTC_FAST], n, p);
            d += sc_core::sc_time(2.0 / (hz_apb.read() > 0 ? hz_apb.read() : 20e6), sc_core::SC_SEC);
            return;
        }
        if ((a >= c3::ROM0_I && a + n <= c3::ROM_I_FIN) || (a >= c3::ROM1_D && a + n <= c3::ROM1_D_FIN)) {
            ++n_rom_;
            if (rom.hay()) {
                if (gp.is_write()) { ++n_fallos_; return; }
                const bool drom = a < c3::ROM1_D_FIN;
                if (drom && fetch) { ++n_fallos_; return; }
                const uint32_t ai = drom ? a - c3::ROM1_D + 0x40040000u : a;
                std::copy(rom.byte(ai), rom.byte(ai) + n, p);
                gp.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            if (!aviso_rom_) {
                aviso_rom_ = true;
                SC_REPORT_WARNING("esp32c3", "acceso a la ROM con rom=ninguna (R3): lee cero");
            }
            if (gp.is_write()) { ++n_fallos_; return; }
            for (unsigned i = 0; i < n; ++i) p[i] = 0;
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (cache.es_ventana(a)) {
            std::string pq;
            if (gp.is_write()) pq = "escritura en la ventana de la Flash (la cache es de solo lectura)";
            else if (cache.lee_ventana(a, p, n, fetch, d, pq)) { gp.set_response_status(tlm::TLM_OK_RESPONSE); return; }
            if (!aviso_ventana_) {
                aviso_ventana_ = true;
                SC_REPORT_WARNING("esp32c3", (hex(a) + ": " + pq).c_str());
            }
            ++n_fallos_;
            return;
        }
        if (a >= c3::PERIF && a < c3::PERIF_FIN && !fetch) {
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
        if (cache.es_ventana(a) && !gp.is_write() && flash()) {
            for (unsigned i = 0; i < n; ++i) {
                const uint32_t va = a + i;
                const uint32_t base = va >= CACHE_C3.base_i ? CACHE_C3.base_i : CACHE_C3.base_d;
                const uint32_t e = cache.entrada(((va - base) >> 16) % CACHE_C3.entradas);
                if (e & 0x100u) return i;
                gp.get_data_ptr()[i] = flash()->datos()[((e & 0xFFu) * CACHE_C3.pagina + (va & 0xFFFFu)) % flash()->tam()];
            }
            return n;
        }
        for (unsigned i = 0; i < n; ++i) {
            uint8_t* m = mem(a + i, false);
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
        gpio.reset(); intmtx.reset(); system.reset_regs(); uart0.reset(); uart1.reset();
        systimer.reset(); timg0.reset(); timg1.reset(); spi0.reset(); spi1.reset(); cache.reset();
        sha.reset(); regi2c.reset(); gdma.reset(); spi2.reset(); ledc.reset(); rmt.reset(); adc.reset(); aes.reset(); i2c.reset(); twai.reset();
        efuse.reset(); syscon.reset(); efuse_trm.reset();
        fe2.reset(); fe.reset(); nrx.reset(); bb.reset();
        reset_cpu_perif();
    }
    // Lo que va con la CPU y vuelve a cero también en un reset de la CPU: la
    // protección de memoria (PMS, con sus bloqueos), el World Controller y
    // ASSIST_DEBUG. ESP-IDF lo da por hecho: si al arrancar la app los
    // bloqueos del PMS siguen puestos, reinicia («Memprot feature locked
    // after the system reset»), y esp_restart() es un reset de la CPU
    // [deducido de ESP-IDF; el TRM no lo dice].
    void reset_cpu_perif() {
        pms.reset(); wcl.reset(); assist.reset();
    }

    void control_proc() {
        pon_relojes(20e6, 20e6);
        for (unsigned i = 0; i < 8; ++i) gpio.sig_oe[c3::SIG_CPU_GPIO0 + i].write(false);
        gpio.sig_oe[c3::SIG_U0TXD].write(true);
        gpio.sig_oe[c3::SIG_U1TXD].write(true);
        for (unsigned c = 0; c < LedcEsp::N_CANALES; ++c) gpio.sig_oe[c3::SIG_LEDC0 + c].write(true);
        for (unsigned c = 0; c < 2; ++c) gpio.sig_oe[c3::SIG_RMT0 + c].write(true);
        gpio.sig_oe[c3::SIG_I2C_SCL].write(true);
        gpio.sig_oe[c3::SIG_I2C_SDA].write(true);
        gpio.sig_oe[c3::SIG_TWAI].write(true);
        for (unsigned g : {c3::SIG_FSPICLK, c3::SIG_FSPID, c3::SIG_FSPICS0}) gpio.sig_oe[g].write(true);
        for (;;) {
            // Apagado o CHIP_PU bajo: todo quieto
            encendido_ = false;
            rst_cpu_n.write(false);
            while (!pu_alto()) wait(ev_pu_);
            // El reset de chip: todo, los STORE de RTC_CNTL incluidos
            ++n_resets_;
            rtc.reset_chip();
            reset_perifericos();
            // Los pines de arranque se guardan cuando CHIP_PU ya está alto y
            // los pads, asentados [TRM-C3 §7.1]
            wait(1, sc_core::SC_US);
            if (!pu_alto()) continue;
            gpio.latch_arranque();
            elige_jtag();
            modo_flash();
            csr_.reset();
            encendido_ = true;
            rst_cpu_n.write(!ndm_ && !usb_rst_);
            // Encendido: hasta que CHIP_PU baje o se pida un reset
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
                        if (rtc_pend_) { rtc.reset_chip(); gpio.latch_arranque(); }
                        if (rtc.force_download() || usb_descarga_) gpio.fuerza_descarga();
                        usb_descarga_ = false;
                        modo_flash();
                        rtc_pend_ = false;
                    }
                    if (!sis) reset_cpu_perif();
                    rtc.anota_causa(c);
                    csr_.reset();
                    wait(100, sc_core::SC_NS);
                    rst_cpu_n.write(!ndm_ && !usb_rst_);
                }
            }
        }
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP32C3_ESP32C3_H
