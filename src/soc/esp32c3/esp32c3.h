// =============================================================================
// esp32c3.h — EL ESP32-C3 (fase E3 de doc/analisis_esp32_riscv.md)
//
// El núcleo genérico de src/rv (NucleoRv con RV_ESP32C3) con lo que Espressif
// puso alrededor, SIN ROM: el firmware se carga en la SRAM y el núcleo arranca
// en su punto de entrada («arranque directo»). La ROM, la Flash y la caché
// llegan en E4.
//
//   memoria    SRAM0 (16 KB, bus I, 0x4037_C000) y SRAM1 (384 KB), vista por
//              el bus I en 0x4038_0000 y por el D en 0x3FC8_0000: es la MISMA
//              memoria. Por el bus I los datos van solo en palabras alineadas
//              [TRM-C3 §3.3.1]: un lb o un sh a una dirección de IRAM da un
//              fallo de acceso. Por el D no se ejecuta. RTC FAST (8 KB,
//              0x5000_0000) por los dos. La ROM: sin contenido (lee cero y
//              avisa una vez). La Flash (0x3C00_0000 y 0x4200_0000): fallo.
//   interrup.  la matriz y el INTC (soc/esp/intc_esp.h): 62 fuentes, 31
//              líneas, sin mie/mip
//   reloj      SYSTEM: XTAL de 40 MHz; tras el reset, la CPU y el APB a
//              20 MHz; con el PLL, la CPU a 80 o 160 y el APB a 80
//   periféric. UART0/1, SYSTIMER, TIMG0/1 (T0 y MWDT), GPIO con la matriz e
//              IO_MUX sobre pads eléctricos, la GPIO dedicada por los CSR,
//              RTC_CNTL (causa de reset, reset por software, STORE, force
//              download), eFuse (de un fichero) y SYSTEM; SYSCON, PMS,
//              ASSIST_DEBUG y el World Controller guardan; el resto,
//              DECLARADOS (contestan cero y avisan una vez)
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
#include "../../rv/hart/nucleo_rv.h"
#include "../../periph/esp/uart_esp.h"
#include "../../periph/esp/systimer_esp.h"
#include "../../periph/esp/timg_esp.h"
#include "../../periph/esp/gpio_esp.h"
#include "../../periph/bloque_declarado.h"
#include "../../common/imagen_fw.h"
#include "../../common/analog_net.h"

namespace stm32 {

// Los CSR propios del C3 [TRM-C3 §1.3, §5.6]
class CsrC3 : public rv_csr_ext_if {
public:
    explicit CsrC3(GpioEsp& g) : g_(g) {}
    bool atiende(uint16_t a) const override {
        return (a >= 0x7E0 && a <= 0x7E2) || (a >= 0x803 && a <= 0x805);
    }
    bool lee(RvHart& h, uint16_t a, uint32_t& v) override {
        switch (a) {
        case 0x7E0: v = mpcer_; return true;
        case 0x7E1: v = mpcmr_; return true;
        case 0x7E2: v = cuenta(h); return true;
        case 0x803: v = g_.cpu_oen(); return true;
        case 0x804: v = g_.cpu_gpio_in(); return true;
        case 0x805: v = g_.cpu_out(); return true;
        default: return false;
        }
    }
    bool escribe(RvHart& h, uint16_t a, uint32_t v) override {
        switch (a) {
        case 0x7E0: base_ = cuenta(h); desde_ = fuente(h, mpcer_ = v & 0x7FFu); return true;
        case 0x7E1: base_ = cuenta(h); mpcmr_ = v & 3u; desde_ = fuente(h, mpcer_); return true;
        case 0x7E2: base_ = v; desde_ = fuente(h, mpcer_); return true;
        case 0x803: g_.cpu_gpio_oen(uint8_t(v)); return true;
        case 0x804: return true;                         // solo de lectura
        case 0x805: g_.cpu_gpio_out(uint8_t(v)); return true;
        default: return false;
        }
    }
private:
    GpioEsp& g_;
    uint32_t mpcer_ = 0, mpcmr_ = 0, base_ = 0;
    uint64_t desde_ = 0;
    // mpcer: bit 0 ciclos, bit 1 instrucciones (el resto de sucesos: de modelo,
    // se cuentan como instrucciones). mpcmr bit 0: contar.
    static uint64_t fuente(RvHart& h, uint32_t er) { return (er & 1u) ? h.ciclos() : h.instret(); }
    uint32_t cuenta(RvHart& h) const {
        if (!(mpcmr_ & 1u) || !mpcer_) return base_;
        return base_ + uint32_t(fuente(h, mpcer_) - desde_);
    }
};

class Esp32C3 : public sc_core::sc_module {
public:
    const McuCapsC3& mcu;

    // --- Relojes y resets (señales del SoC) -------------------------------------
    // Los escriben el control del reset y el núcleo (al tocar SYSTEM)
    sc_core::sc_signal<double, sc_core::SC_MANY_WRITERS> hz_cpu{"hz_cpu"}, hz_apb{"hz_apb"};
    sc_core::sc_signal<bool> rst_cpu_n{"rst_cpu_n"}, rst_per_n{"rst_per_n"},
                             siempre{"siempre"}, reloj_falso{"reloj_falso"};

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
    BloqueGuardado syscon, pms, assist, wcl, efuse_trm;
    std::vector<std::unique_ptr<BloqueDeclarado>> declarados;

    // CHIP_PU, con una resistencia de subida que hace las veces del RC de la
    // placa mientras hay alimentación
    AnalogNet chip_pu{"chip_pu"};

    Esp32C3(sc_core::sc_module_name nm, const McuCapsC3& m, const Cableado& cab = Cableado())
        : sc_core::sc_module(nm), mcu(m), core("core", *m.nucleo, 0),
          gpio("gpio", c3::GPIO_B, pines_c3(), bonded(m), cab),
          iomux("io_mux", c3::IO_MUX_B, gpio),
          intmtx("intmtx", c3::INTMTX_B, c3::N_FUENTES),
          system("system", c3::SYSTEM_B), rtc("rtc_cntl", c3::RTC_CNTL_B),
          efuse("efuse", c3::EFUSE_B),
          uart0("uart0", c3::UART0_B, gpio.sig_out[c3::SIG_U0TXD], gpio.sig_in[c3::SIG_U0RXD]),
          uart1("uart1", c3::UART1_B, gpio.sig_out[c3::SIG_U1TXD], gpio.sig_in[c3::SIG_U1RXD]),
          systimer("systimer", c3::SYSTIMER_B),
          timg0("timg0", c3::TIMG0_B), timg1("timg1", c3::TIMG1_B),
          syscon("syscon", c3::SYSCON_B, 0x1000), pms("pms", c3::PMS_B, 0x1000),
          assist("assist_debug", c3::ASSIST_B, 0x1000), wcl("world_controller", c3::WCL_B, 0x1000),
          efuse_trm("efuse_tabla_trm", c3::EFUSE_TRM_B, 0x1000),
          csr_(gpio), sram_(c3::SRAM_TAM, 0), rtc_fast_(c3::RTC_FAST_TAM, 0) {
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

        struct D { uint32_t b; const char* n; const char* f; };
        static const D dec[] = {
            {c3::SPI1_B, "SPI1", "La Flash: E4"}, {c3::SPI0_B, "SPI0", "La Flash y la cache: E4"},
            {c3::I2C_B, "I2C", "E8"}, {c3::UHCI_B, "UHCI0", "E8"}, {c3::RMT_B, "RMT", "E8"},
            {c3::LEDC_B, "LEDC", "E8"}, {c3::SPI2_B, "GP-SPI2", "E8"}, {c3::TWAI_B, "TWAI", "E8"},
            {c3::I2S_B, "I2S", "E8"}, {c3::AES_B, "AES", "E4/E8"}, {c3::SHA_B, "SHA", "E4"},
            {c3::RSA_B, "RSA", "E8"}, {c3::DS_B, "DS", "E8"}, {c3::HMAC_B, "HMAC", "E8"},
            {c3::GDMA_B, "GDMA", "E8"}, {c3::ADC_B, "APB_SARADC", "E8"},
            {c3::USB_SJ_B, "USB_SERIAL_JTAG", "E7"}, {c3::XTS_AES_B, "XTS_AES", "E4"},
        };
        for (const D& d : dec)
            declarados.emplace_back(new BloqueDeclarado(sc_core::sc_gen_unique_name("decl"),
                                                        d.b, 0x1000, d.n, d.f));
        std::vector<BusSlave*> todos{&gpio, &iomux, &intmtx, &system, &rtc, &efuse, &uart0,
                                     &uart1, &systimer, &timg0, &timg1, &syscon, &pms,
                                     &assist, &wcl, &efuse_trm};
        for (auto& d : declarados) todos.push_back(d.get());
        for (BusSlave* s : todos) engancha(s);

        // Interrupciones
        uart0.irq = [this](bool v) { intmtx.fuente(c3::F_UART0, v); };
        uart1.irq = [this](bool v) { intmtx.fuente(c3::F_UART1, v); };
        gpio.irq = [this](bool v) { intmtx.fuente(c3::F_GPIO, v); };
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
        if (si) chip_pu.set_drive(d_pu_, 3.3f, 10e3f);
        else    chip_pu.set_drive(d_pu_, 0.0f, 10e3f);
    }
    bool encendido() const { return encendido_; }
    // Las causas de reset de RESET_STATE [TRM-C3 tabla 6.1-1]
    enum : unsigned {
        CAUSA_POR = 1, CAUSA_SW_SIS = 3, CAUSA_MWDT0_SIS = 7, CAUSA_MWDT1_SIS = 8,
        CAUSA_MWDT0_CPU = 11, CAUSA_SW_CPU = 12, CAUSA_MWDT1_CPU = 17,
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
        return "";
    }
    bool cargado() const { return cargado_; }
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
    }

    uint64_t resets() const { return n_resets_; }
    uint64_t accesos_rom() const { return n_rom_; }
    uint64_t fallos_bus() const { return n_fallos_; }

    std::vector<std::string> limitaciones() const {
        return {
            "sin ROM: el firmware se carga en la SRAM y arranca en su punto de entrada (E4)",
            "sin Flash, MSPI ni cache: 0x3C00_0000 y 0x4200_0000 dan fallo de acceso (E4)",
            "los MWDT y el RWDT salen del reset apagados (en el silicio, el del grupo 0 y el "
            "RWDT van encendidos y los apaga el bootloader)",
            "PERIP_CLK_EN0/1 se guardan pero no cortan el reloj de los perifericos",
            "el RWDT, el SWD, el brownout y los modos de bajo consumo: sin modelar",
            "eFuse: solo lectura (de un fichero o la MAC de omision); ni programar ni "
            "la revision del chip",
            "declarados: SPI0/1/2, I2C, UHCI, RMT, LEDC, TWAI, I2S, AES, SHA, RSA, DS, HMAC, "
            "GDMA, SAR ADC, USB-Serial-JTAG y XTS_AES (E4, E7, E8)",
            "el JTAG en pines (MTMS-MTDO) y la depuracion: E6",
        };
    }

private:
    CsrC3 csr_;
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
    }

    // La memoria: puntero al byte, o nullptr. `fetch`: por el bus I.
    uint8_t* mem(uint32_t a, bool fetch) {
        (void)fetch;
        if (a >= c3::SRAM0_I && a < c3::SRAM_I_FIN) return &sram_[a - c3::SRAM0_I];
        if (a >= c3::SRAM1_D && a < c3::SRAM1_D_FIN) return &sram_[a - c3::SRAM1_D + 16 * 1024];
        if (a >= c3::RTC_FAST && a < c3::RTC_FAST + c3::RTC_FAST_TAM) return &rtc_fast_[a - c3::RTC_FAST];
        return nullptr;
    }

    void b_transport(int, tlm::tlm_generic_payload& gp, sc_core::sc_time& d) {
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
            if (!aviso_rom_) {
                aviso_rom_ = true;
                SC_REPORT_WARNING("esp32c3", "acceso a la ROM: este C3 no la tiene todavia (E4); lee cero");
            }
            if (gp.is_write()) { ++n_fallos_; return; }
            for (unsigned i = 0; i < n; ++i) p[i] = 0;
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
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
        systimer.reset(); timg0.reset(); timg1.reset();
    }

    void control_proc() {
        pon_relojes(20e6, 20e6);
        for (unsigned i = 0; i < 8; ++i) gpio.sig_oe[c3::SIG_CPU_GPIO0 + i].write(false);
        gpio.sig_oe[c3::SIG_U0TXD].write(true);
        gpio.sig_oe[c3::SIG_U1TXD].write(true);
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
            encendido_ = true;
            rst_cpu_n.write(true);
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
                        if (rtc.force_download()) gpio.fuerza_descarga();
                    }
                    rtc.anota_causa(c);
                    wait(100, sc_core::SC_NS);
                    rst_cpu_n.write(true);
                }
            }
        }
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP32C3_ESP32C3_H
