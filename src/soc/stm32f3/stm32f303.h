// =============================================================================
// stm32f303.h — EL STM32F303VC (fases S1-S3 del plan del F303)
//
// Un SoC PROPIO y no una derivada de `SocF4`, por lo que dice §3.4 del
// análisis: cambian el RCC, la Flash, el DMA, los buses (GPIO en AHB2, ADC en
// AHB3), la USART, el ADC y la mitad del SYSCFG, y heredar obligaría a tapar
// más de lo que se aprovecha. Lo que SÍ se aprovecha, tal cual:
//
//   el núcleo     `CortexM4F` con 82 IRQ y 4 bits [f3_desc.h], la CCM
//                 EJECUTABLE (`MapaRam::ccm_ejecutable`, rasgo de S0) y sin el
//                 alias de arranque del núcleo: el de 0x0 lo resuelve este SoC
//                 con su propio MEM_MODE
//   los pines     `PinMux` y `PowerPads` de siempre, con el LQFP100 del F303
//                 (generado) y sus funciones alternativas (generadas)
//   el GPIO       `GpioPort` con el rasgo `GPIO_F3`: en AHB2 y con BRR
//   el EXTI       `Exti` con `EXTI_F303`: 36 líneas en dos tandas
//   DBGMCU        el de `DebugSys`, con DEV_ID 0x422 y la tabla de congelación
//                 del F3 como dato
//
// Y desde S5 lo analógico (conecta_s5()): los cuatro ADC de segunda
// generación en dos parejas (`AdcParV3`, periph/adc_v3.h), el DAC1, los siete
// comparadores (periph/comp.h) y los cuatro amplificadores (periph/opamp.h),
// estos dos con sus registros delegados por el SYSCFG. El TSC sigue declarado.
//
// y lo nuevo: `RccF3` (rcc/rcc_f3.h), `FlashF1` (mem/flash_f1.h), el SYSCFG y
// el PWR (syscfg_f3.h) y la memoria de sistema (aquí abajo). Y desde S3 los
// periféricos de segunda generación: `UsartV2` (periph/usart_v2.h), `DmaCanal`
// (periph/dma_canal.h) con la tabla de peticiones y los remapeos de
// SYSCFG_CFGR1, el SPI v3 y el I2S, `FmpI2c` en I2C1/2 con el reloj de
// RCC_CFGR3, los TIM v3 con su red de ITR, IWDG con ventana, WWDG, RTC, CRC
// programable, y Sleep/Stop/Standby en el PWR (conecta_s3()).
//
// LA MATRIZ: decodificadores directos, como los MSP432, y no `AhbMatrix`.
// Decidido en S3 con los DMA dentro (§9, riesgo 6): cada DMA es un iniciador
// con su propio socket al decodificador y factura un ciclo de HCLK por fase,
// sin contención con la CPU. Ningún ejemplo de ST de S3 la nota; la que se
// pierde es la que mediría AhbMatrix.
//
// LO DEMÁS, DECLARADO: ocupa su ventana, avisa una vez con la fase que le toca
// y lee cero (`periph/bloque_declarado.h`). `limitaciones()` lo enumera.
// =============================================================================
#ifndef STM32_SOC_STM32F3_STM32F303_H
#define STM32_SOC_STM32F3_STM32F303_H

#include <systemc>
#include <memory>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>
#include <string>
#include <vector>
#include "f3_desc.h"
#include "syscfg_f3.h"
#include "../../core/cortex_m4f.h"
#include "../../bus/ahb_decoder.h"
#include "../../mem/sram.h"
#include "../../mem/flash_f1.h"
#include "f3_programador.h"
#include "../../rcc/rcc_f3.h"
#include "../../pins/pin_mux.h"
#include "../../pins/power_pads.h"
#include "../../periph/gpio_port.h"
#include "../../periph/exti.h"
#include "../../periph/bloque_declarado.h"
#include "../../periph/usart_v2.h"
#include "../../periph/dma_canal.h"
#include "../../periph/timers.h"
#include "../../periph/spi.h"
#include "../../periph/fmpi2c.h"
#include "../../periph/watchdog.h"
#include "../../periph/rtc.h"
#include "../../periph/crc.h"
#include "../../periph/adc_v3.h"
#include "../../periph/dac.h"
#include "../../periph/comp.h"
#include "../../periph/opamp.h"

namespace stm32 {

// La CCM ejecutable, a 0x1000_0000, 8 KB. Sin SRAM2 ni de respaldo.
inline constexpr MapaRam ram_f303(const F3Caps& c) {
    MapaRam r{ f3::SRAM_B, c.sram_size, 0, 0, f3::CCM_B, c.ccm_size, 0, 0 };
    r.ccm_ejecutable = true;
    return r;
}
// Sin el alias del núcleo: lo hace el SoC (MEM_MODE y el tamaño de cada
// memoria son del F3, no del F4). Sin ventana de fabricante en el PPB: el
// DBGMCU está donde en el F4 y lo atiende `DebugSys`.
inline constexpr IntegracionNucleo INTEGRACION_F303{ false, 0, 0 };

// Las líneas de congelación, en el orden en que este SoC las usa, y de qué bit
// de DBGMCU_APB1_FZ/APB2_FZ sale cada una [CMSIS: DBGMCU_APBx_FZ_*]
enum FzF3 : unsigned {
    FZ3_TIM2, FZ3_TIM3, FZ3_TIM4, FZ3_TIM6, FZ3_TIM7, FZ3_RTC, FZ3_WWDG, FZ3_IWDG,
    FZ3_I2C1, FZ3_I2C2, FZ3_CAN,
    FZ3_TIM1 = 16, FZ3_TIM8, FZ3_TIM15, FZ3_TIM16, FZ3_TIM17
};
inline constexpr int FZ_APB1_F303[16] = {0, 1, 2, 4, 5, 10, 11, 12, 21, 22, 25, -1, -1, -1, -1, -1};
inline constexpr int FZ_APB2_F303[5]  = {0, 1, 2, 3, 4};

// -----------------------------------------------------------------------------
// LA MEMORIA DE SISTEMA: 8 KB en 0x1FFF_D800, de solo lectura
//
// El cargador de ST no se puede distribuir: desde S7 va uno PROPIO con el
// protocolo de AN3155 por USART1 (PA9/PA10) y USART2 (PD5/PD6), cargador/main.c,
// que además deja la marca 'MSYS' en 0x2000_0000. Al final de la ventana, los
// datos de fábrica: el UID, la calibración del sensor de temperatura y de
// VREFINT, y F_SIZE [CMSIS; HAL; DS9118] [valores típicos, f3_desc.h].
// -----------------------------------------------------------------------------
class SysMemF3 : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<SysMemF3> tsk{"tsk"};
    static constexpr uint32_t SIZE = f3::SYSMEM_FIN - f3::SYSMEM_B;

    SysMemF3(sc_core::sc_module_name nm, uint16_t flash_kb)
        : sc_core::sc_module(nm), mem_(SIZE, 0xFF) {
        tsk.register_b_transport(this, &SysMemF3::bt);
        tsk.register_transport_dbg(this, &SysMemF3::dbg);
        static const uint8_t stub[] = {
#include "sysmem.inc"
        };
        std::memcpy(mem_.data(), stub, sizeof stub);
        for (unsigned i = 0; i < 3; ++i) pon32(f3::UID_B + 4 * i, f3::UID[i]);
        pon16(f3::TS_CAL1_A, f3::TS_CAL1);
        pon16(f3::VREFINT_A, f3::VREFINT_CAL);
        pon16(f3::TS_CAL2_A, f3::TS_CAL2);
        pon16(f3::F_SIZE_A, flash_kb);
    }
    uint32_t peek32(uint32_t a) const {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) v |= uint32_t(mem_[a - f3::SYSMEM_B + i]) << (8 * i);
        return v;
    }
    unsigned escrituras() const { return n_esc_; }

private:
    std::vector<uint8_t> mem_;
    unsigned n_esc_ = 0;
    void pon16(uint32_t a, uint16_t v) { mem_[a - f3::SYSMEM_B] = uint8_t(v); mem_[a - f3::SYSMEM_B + 1] = uint8_t(v >> 8); }
    void pon32(uint32_t a, uint32_t v) { pon16(a, uint16_t(v)); pon16(a + 2, uint16_t(v >> 16)); }
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (a < f3::SYSMEM_B || a + n > f3::SYSMEM_FIN) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (gp.is_write()) { ++n_esc_; gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        std::memcpy(gp.get_data_ptr(), &mem_[a - f3::SYSMEM_B], n);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned n = gp.get_data_length();
        if (a < f3::SYSMEM_B || a + n > f3::SYSMEM_FIN || !gp.is_read()) return 0;
        std::memcpy(gp.get_data_ptr(), &mem_[a - f3::SYSMEM_B], n);
        return n;
    }
};

class SocF3 : public sc_core::sc_module {
public:
    const F3Caps mcu;

    PinMux    pinmux;
    PowerPads pwr_pads;
    RccF3     rcc;
    CortexM4F core;

    // --- Memoria -----------------------------------------------------------------
    FlashF1  flash;
    Sram     sram;
    Sram     ccm;
    SysMemF3 sysmem;

    // --- Buses ---------------------------------------------------------------------
    AhbDecoder ahb1{"ahb1"}, ahb2{"ahb2"}, ahb3{"ahb3"}, apb1{"apb1"}, apb2{"apb2"};
    AhbApbBridge br_apb1{"br_apb1"}, br_apb2{"br_apb2"};

    // --- Sistema -------------------------------------------------------------------
    sc_core::sc_vector<GpioPort> gpio;     // A..F
    Exti      exti;
    SyscfgF3  syscfg{"syscfg"};
    PwrF3     pwr{"pwr"};

    // --- Los periféricos de segunda generación (S3) --------------------------------
    DmaCanal  dma1{"dma1", f3::DMA1_B, 7}, dma2{"dma2", f3::DMA2_B, 5};
    CrcUnit   crc{"crc", CRC_F3};
    // Los diez temporizadores, del MISMO modelo con los rasgos v3 [periph/timers.h]
    TimerBase tim1{"tim1", f3::TIM1_B, CAPS_F3_TIM_ADV}, tim8{"tim8", f3::TIM8_B, CAPS_F3_TIM_ADV};
    TimerBase tim2{"tim2", f3::TIM2_B, CAPS_F3_TIM_GP32};
    TimerBase tim3{"tim3", f3::TIM3_B, CAPS_F3_TIM_GP16}, tim4{"tim4", f3::TIM4_B, CAPS_F3_TIM_GP16};
    TimerBase tim6{"tim6", f3::TIM6_B, CAPS_F3_TIM_BASIC}, tim7{"tim7", f3::TIM7_B, CAPS_F3_TIM_BASIC};
    TimerBase tim15{"tim15", f3::TIM15_B, CAPS_F3_TIM15};
    TimerBase tim16{"tim16", f3::TIM16_B, CAPS_F3_TIM16_RMP}, tim17{"tim17", f3::TIM17_B, CAPS_F3_TIM16};
    UsartV2   usart1{"usart1", f3::USART1_B, CAPS_F3_USART}, usart2{"usart2", f3::USART2_B, CAPS_F3_USART};
    UsartV2   usart3{"usart3", f3::USART3_B, CAPS_F3_USART};
    UsartV2   uart4{"uart4", f3::UART4_B, CAPS_F3_UART4}, uart5{"uart5", f3::UART5_B, CAPS_F3_UART5};
    SpiBase   spi1{"spi1", f3::SPI1_B, CAPS_F3_SPI1};
    SpiBase   spi2{"spi2", f3::SPI2_B, CAPS_F3_SPI_I2S}, spi3{"spi3", f3::SPI3_B, CAPS_F3_SPI_I2S};
    FmpI2c    i2c1{"i2c1", f3::I2C1_B}, i2c2{"i2c2", f3::I2C2_B};
    Iwdg      iwdg{"iwdg", IWDG_F3};
    Wwdg      wwdg{"wwdg"};
    Rtc       rtc{"rtc", RTC_F3};
    // --- Lo analógico (S5) -------------------------------------------------------------
    AdcParV3  adc12{"adc12", f3::ADC12_B, "ADC1", "ADC2"}, adc34{"adc34", f3::ADC34_B, "ADC3", "ADC4"};
    DacBase   dac1{"dac1", CAPS_DAC_F303};
    CompF3    comp{"comp", [this](unsigned p, unsigned i) -> analog_net_if& { return pinmux.analog(p, i); }};
    OpampF3   opamp{"opamp", [this](unsigned p, unsigned i) -> analog_net_if& { return pinmux.analog(p, i); }};
    // La temperatura del chip, la que lee el sensor (la pone el banco) [°C]
    double temp_c = 25.0;
    void set_die_temp(double c) { temp_c = c; }
    std::vector<BloqueDeclarado*> declarados;
    static constexpr unsigned N_PUERTOS = 6;

    tlm_utils::simple_target_socket_optional<SocF3> from_tb{"from_tb"};

    SocF3(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
          const Cableado& cab = Cableado(), const F3Caps& caps = MCU_STM32F303VC)
        : sc_core::sc_module(nm), mcu(caps),
          pinmux("pinmux", cab, caps.enc),
          pwr_pads("pwr_pads", &cab),
          rcc("rcc"),
          core("core", dbg, caps.nucleo, ram_f303(caps), INTEGRACION_F303),
          flash("flash", f3::FLASH_R, f3::FLASH_B, caps.flash_size, f3::PAGINA, f3::OB_B),
          sram("sram", f3::SRAM_B, caps.sram_size),
          ccm("ccm", f3::CCM_B, caps.ccm_size),
          sysmem("sysmem", uint16_t(caps.flash_size / 1024u)),
          gpio("gpio", N_PUERTOS, [](const char* n, size_t i) {
                   return new GpioPort(n, unsigned(i), GPIO_F3); }),
          exti("exti", EXTI_F303),
          s_irq_("s_irq", caps.nucleo.n_irq),
          s_exti_gpio_("s_exti_gpio", N_GPIO_PORTS * N_PORT_PINS),
          s_exticr_("s_exticr", 16),
          s_exti_in_("s_exti_in", EXTI_F303.n_lineas - 16),
          s_exti_lin_("s_exti_lin", EXTI_F303.n_lineas - 16),
          s_freeze_("s_freeze", FZ_COUNT) {
        SC_HAS_PROCESS(SocF3);
        conecta_relojes();
        conecta_nucleo();
        conecta_bus();
        conecta_pines();
        conecta_irq();
        conecta_s3();
        conecta_s5();
        // Cada escritura a un periferico, en su instante (S4: el CS del
        // giroscopo entre dos lecturas del BSP dura menos que un quantum)
        core.cpu.sync_escritura_perif = true;
        conecta_pines_s3();
        SC_METHOD(boot_proc);
        sensitive << s_boot0_ << s_sysrst_n_ << boot_ev_;
        SC_METHOD(irq_exti_proc);
        for (unsigned i = 0; i < s_exti_lin_.size(); ++i) sensitive << s_exti_lin_[i];
        dont_initialize();
        SC_METHOD(mco_proc); sensitive << mco_ev_;
        SC_METHOD(irq_tim_proc);
        for (unsigned k = 0; k < N_TIM; ++k) for (unsigned j = 0; j < 5; ++j) sensitive << s_tirq_[k][j];
        sensitive << s_dac_irq_;
        dont_initialize();
        SC_METHOD(dma_req_proc);
        sensitive << s_cfgr1_;
        for (unsigned k = 0; k < N_TIM; ++k) for (unsigned j = 0; j < 7; ++j) sensitive << q_t_[k][j];
        for (auto& q : q_u_) sensitive << q[0] << q[1];
        for (auto& q : q_spi_) sensitive << q[0] << q[1];
        for (auto& q : q_i2c_) sensitive << q[0] << q[1];
        for (auto& q : q_adc_) sensitive << q;
        for (auto& q : q_dac_) sensitive << q;
        dont_initialize();
        SC_METHOD(irq_adc_proc); sensitive << s_adc12_irq_[0] << s_adc12_irq_[1];
        dont_initialize();
        SC_METHOD(exti_ln_proc);
        for (unsigned i = 0; i < s_exti_gpio_.size(); ++i) sensitive << s_exti_gpio_[i];
        for (unsigned l : {2u, 9u, 11u, 15u}) sensitive << s_exticr_[l];
        SC_METHOD(dac_trig_proc); sensitive << s_cfgr1_ << s_trgo_[T3] << s_trgo_[T8];
        SC_METHOD(comp_proc);
        for (unsigned i = 0; i < CompF3::N; ++i) sensitive << comp.out[i];
        sensitive << comp.cfg_ev;
        dont_initialize();
        SC_METHOD(itr3_proc); sensitive << s_cfgr1_ << s_trgo_[T4] << tim17.ch_out[0];
        SC_METHOD(irtim_proc); sensitive << tim16.ch_out[0] << tim17.ch_out[0];
        // El TI1 del TIM16 desde dentro: el MCO (OR.TI1_RMP = 11) [HAL: TIM_TIM16_MCO]
        SC_METHOD(ti16_proc); sensitive << rcc.mco_sig;
        SC_METHOD(iwdg_proc); sensitive << s_iwdg_lsi_ << s_sysrst_n_;
        // Sin las ondas de los relojes internos, como el E4: lo que cuenta
        // ciclos trabaja con la frecuencia (common/clock_gen.h)
        // El `load` de GDB programa esta Flash con su FPEC (S2)
        ProgramadorF3::el().tam = mcu.flash_size;
        if (core.gdb) core.gdb->pon_programador(&ProgramadorF3::el());
        set_ondas(false);
    }
    ~SocF3() override {
        for (BloqueDeclarado* b : declarados) delete b;
        for (BloqueAusente* b : ausentes) delete b;
    }

    // --- Lo que mira el adaptador y el banco ------------------------------------------
    double f_sysclk() const { return rcc.f_sysclk(); }
    double f_hclk()   const { return rcc.f_hclk(); }
    bool   en_reset() const { return rcc.en_reset(); }
    uint8_t mem_mode() const { return syscfg.mem_mode(); }
    unsigned avisos() const { return rcc.avisos() + flash.avisos(); }
    void set_ondas(bool on) { rcc.set_internal_waveforms(on); }
    // Escribir un byte de firmware donde vaya: Flash, SRAM o CCM
    bool poke8(uint32_t a, uint8_t v) {
        if (flash.poke8(a, v)) return true;
        if (a >= f3::SRAM_B && a < f3::SRAM_B + sram.size()) { uint8_t b = v; return sram.load(&b, 1, a - f3::SRAM_B); }
        if (a >= f3::CCM_B && a < f3::CCM_B + ccm.size()) { uint8_t b = v; return ccm.load(&b, 1, a - f3::CCM_B); }
        return false;
    }
    unsigned escrituras_ccm_protegidas() const { return n_ccm_prot_; }
    // Los bloques que este miembro de la familia no tiene (S9)
    const std::vector<BloqueAusente*>& bloques_ausentes() const { return ausentes; }

    std::vector<std::string> limitaciones() const {
        std::vector<std::string> v = {
            "fases S1, S2, S3 y S5 del plan del F303: nucleo, reloj, Flash (lectura y FPEC), SRAM, "
            "CCM ejecutable, memoria de sistema, GPIO, EXTI, SYSCFG, PWR con Sleep, Stop y "
            "Standby, DMA1/2, USART1-3 y UART4/5 (v2), SPI1-3 (v3), I2C1/2, TIM1/2/3/4/6/7/8/15/16/17 "
            "e IRTIM, IWDG con ventana, WWDG, RTC, CRC programable, ADC1-4, DAC1, COMP1-7 y "
            "OPAMP1-4. El resto (TSC, USB, CAN) esta DECLARADO: contesta, lee cero y avisa una vez",
            "la proteccion de lectura (RDP) esta a medias: el nivel se carga en FLASH_OBR, "
            "volver al 0 borra la Flash y el 2 bloquea los bytes de opcion, pero el 1 no "
            "cierra la Flash al depurador ni el 2 apaga la depuracion; la Flash se programa "
            "con los tiempos de DS9118 (53,5 us por media palabra, 20 ms por pagina)",
            "el NMI del CSS es un pulso por fallo, no se repite hasta CSSC; el HSE que "
            "falla no llega a la entrada de freno de los TIM, ni el PVD ni el LOCKUP",
            "los DMA no pasan por una matriz: cada acceso factura un ciclo de HCLK por fase, "
            "sin contencion con la CPU; no ven la CCM, como en el silicio",
            "las USART v2 no modelan Smartcard, IrDA, el auto-baud ni el despertar del Stop "
            "(UESM); los I2C tampoco despiertan del Stop (WUPEN), ni calculan el PEC",
            "el I2S en duplex completo (I2S2ext, I2S3ext) esta declarado; el I2S_CKIN no "
            "tiene reloj; el ITR de los TIM15 desde el TIM16/17 es la salida del pin (OC1), "
            "no OC1REF",
            "lo analogico (S5): los ADC no modelan la latencia de los disparos, el ruido ni el "
            "error de un ADC calibrado, ni sacan AWDx_OUT a los temporizadores; VREF+ es VDDA; "
            "los comparadores cambian en un delta y no hacen BLANKING ni van a capturas ni "
            "a OCREF_CLR; los OPAMP no conmutan con TCM_EN; el TSC esta DECLARADO",
            "USB y CAN: fase S6",
            "la memoria de sistema no lleva el cargador de ST sino uno propio con el "
            "protocolo de AN3155 por USART1/USART2 (sin I2C, SPI, CAN ni el DFU por USB); "
            "WRITE PROTECT y READOUT PROTECT contestan NACK",
            "la calibracion de fabrica (TS_CAL1/2, VREFINT_CAL) y el UID son valores "
            "tipicos del modelo, no de un chip",
            "sin modelo de corriente en los modos de bajo consumo; en Standby el IWDG se "
            "para (el silicio lo deja contando)",
            "la linea 81 de la FPU (FPU_IE) no llega al NVIC, como en el F4",
        };
        // S9: lo que este miembro de la familia no tiene, dicho
        if (*mcu.sin || mcu.ccm_size == 0) {
            std::string t = std::string(mcu.nombre) + ", del mismo die que el F303xC, no tiene";
            if (*mcu.sin) t += std::string(" ") + mcu.sin + " [PINDATA]";
            if (mcu.ccm_size == 0) t += std::string(*mcu.sin ? " ni" : "") + " la CCM [CMSIS: stm32f302xc.h]";
            t += ": sus ventanas son reservadas, leen cero y avisan una vez";
            v.push_back(t);
        }
        if (mcu.ausentes & F3A_USB)
            v.push_back("el F358 se alimenta de 1,65 a 1,95 V [PINDATA], sin regulador, y tiene NPOR "
                        "donde el F303 tiene PB2; el modelo lo alimenta y lo arranca como al F303");
        return v;
    }

private:
    // --- Señales -------------------------------------------------------------------
    sc_core::sc_signal<bool>   s_hclk_{"s_hclk"}, s_pclk1_{"s_pclk1"}, s_pclk2_{"s_pclk2"};
    sc_core::sc_signal<bool>   s_tim1_{"timclk1"}, s_tim2_{"timclk2"}, s_stk_{"stkclk"};
    sc_core::sc_signal<bool>   s_rtc_{"rtcclk"}, s_lsi_{"lsiclk"};
    sc_core::sc_signal<double> s_hclk_hz_{"s_hclk_hz"}, s_pclk1_hz_{"s_pclk1_hz"},
                               s_pclk2_hz_{"s_pclk2_hz"}, s_tim1_hz_{"timclk1_hz"},
                               s_tim2_hz_{"timclk2_hz"}, s_rtc_hz_{"rtcclk_hz"},
                               s_lsi_hz_{"s_lsi_hz"};
    sc_core::sc_signal<bool>   s_sysrst_n_{"s_sysrst_n"}, s_bkprst_n_{"s_bkprst_n"},
                               s_nrst_drv_{"s_nrst_drv"}, s_por_ok_{"s_por_ok"},
                               s_nrst_n_{"s_nrst_n"}, s_boot0_{"s_boot0"},
                               s_bor_trip_{"s_bor_trip"}, s_dbp_{"s_dbp"};
    sc_core::sc_signal<double> s_vdd_{"s_vdd"}, s_vdda_{"s_vdda"}, s_vbat_{"s_vbat"};
    sc_core::sc_signal<double> s_idd_{"s_idd"}, s_ibat_{"s_ibat"};
    sc_core::sc_signal<uint8_t> s_bor_lev_{"s_bor_lev"}, s_boot_{"s_boot"},
                                s_boot_core_{"s_boot_core"}, s_dbg_lp_{"s_dbg_lp"};
    sc_core::sc_signal<bool>   s_true_{"s_true"}, s_false_{"s_false"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_irq_;
    sc_core::sc_signal<bool>   s_nmi_{"s_nmi"}, s_sysresetreq_{"s_sysresetreq"},
                               s_sleeping_{"s_sleeping"}, s_sleepdeep_{"s_sleepdeep"},
                               s_evt_in_{"s_evt_in"}, s_evt_out_{"s_evt_out"},
                               s_exti_wakeup_{"s_exti_wakeup"};
    sc_core::sc_signal<bool>   s_iwdg_rr_{"s_iwdg_rr"}, s_wwdg_rr_{"s_wwdg_rr"};
    sc_core::sc_vector<sc_core::sc_signal<bool>>    s_exti_gpio_;
    sc_core::sc_vector<sc_core::sc_signal<uint8_t>> s_exticr_;
    sc_core::sc_vector<sc_core::sc_signal<bool>>    s_exti_in_, s_exti_lin_;
    sc_core::sc_vector<sc_core::sc_signal<bool>>    s_freeze_;
    sc_core::sc_signal<bool> s_swdio_o_{"s_swdio_o"}, s_swdio_oe_{"s_swdio_oe"},
                             s_jtdo_{"s_jtdo"}, s_dbg_swclk_{"s_dbg_swclk"},
                             s_dbg_swdio_i_{"s_dbg_swdio_i"}, s_dbg_jtdi_{"s_dbg_jtdi"},
                             s_dbg_njtrst_{"s_dbg_njtrst"}, s_mco_oe_{"s_mco_oe"};
    sc_core::sc_event mco_ev_, boot_ev_;
    unsigned n_ccm_prot_ = 0;
    bool avisado_ccm_ = false;

    tlm_utils::simple_target_socket<SocF3> t_i_{"t_i"}, t_d_{"t_d"}, t_s_{"t_s"}, t_ccm_{"t_ccm"};
    tlm_utils::simple_initiator_socket<SocF3> a_flash_{"a_flash"}, a_ob_{"a_ob"},
        a_sram_{"a_sram"}, a_ccm_{"a_ccm"}, a_sysmem_{"a_sysmem"}, a_apb1_{"a_apb1"},
        a_apb2_{"a_apb2"}, a_ahb1_{"a_ahb1"}, a_ahb2_{"a_ahb2"}, a_ahb3_{"a_ahb3"};

    // --- Construcción --------------------------------------------------------------------
    void conecta_relojes() {
        rcc.hclk(s_hclk_);       rcc.hclk_hz(s_hclk_hz_);
        rcc.pclk1(s_pclk1_);     rcc.pclk1_hz(s_pclk1_hz_);
        rcc.pclk2(s_pclk2_);     rcc.pclk2_hz(s_pclk2_hz_);
        rcc.timclk1(s_tim1_);    rcc.timclk1_hz(s_tim1_hz_);
        rcc.timclk2(s_tim2_);    rcc.timclk2_hz(s_tim2_hz_);
        rcc.systick_ext(s_stk_);
        rcc.rtcclk(s_rtc_);      rcc.rtcclk_hz(s_rtc_hz_);
        rcc.lsi_clk(s_lsi_);     rcc.lsi_hz(s_lsi_hz_);
        rcc.sys_rst_n(s_sysrst_n_); rcc.bkp_rst_n(s_bkprst_n_);
        rcc.drive_nrst_low(s_nrst_drv_);
        rcc.por_ok(s_por_ok_);   rcc.nrst_in_n(s_nrst_n_);
        rcc.sysresetreq(s_sysresetreq_);
        rcc.iwdg_rst_req(s_iwdg_rr_); rcc.wwdg_rst_req(s_wwdg_rr_);
        rcc.dbp(s_dbp_);
        rcc.nmi_css(s_nmi_);
        rcc.irq(s_irq_[f3::IRQ_RCC]);
        rcc.clk(s_hclk_); rcc.clk_hz(s_hclk_hz_); rcc.rst_n(s_true_); rcc.clk_en(s_true_);
        // La alimentación y el pin de reset
        pwr_pads.por_ok(s_por_ok_);
        pwr_pads.bor_trip(s_bor_trip_);
        pwr_pads.bor_lev(s_bor_lev_);         // el F303 no tiene BOR programable: 11 (apagado)
        pwr_pads.nrst_in_n(s_nrst_n_);
        pwr_pads.boot0_lvl(s_boot0_);
        pwr_pads.vdd_lvl(s_vdd_);
        pwr_pads.vdda_lvl(s_vdda_);
        pwr_pads.vbat_lvl(s_vbat_);
        pwr_pads.drive_nrst_low(s_nrst_drv_);
        pwr_pads.idd_req(s_idd_);
        pwr_pads.ibat_req(s_ibat_);
        pinmux.standby(s_stby_pines_);
        // EWUP1 deja PA0 vivo en Standby: si no, el pad se apaga como los
        // demas y WKUP1 no despierta [RM0316 §7.2.5, §7.3.6]
        pinmux.wkup_en(s_ewup1_);
        pinmux.dbg_pins(s_false_);
        // El HSE por PF0-OSC_IN, el LSE por PC14-OSC32_IN [PINDATA]
        rcc.hse.xtal_in = &pinmux.analog(5, 0);
        rcc.hse.ext_in  = &pinmux.pad_din[5 * N_PORT_PINS + 0];
        rcc.lse.xtal_in = &pinmux.analog(2, 14);
        rcc.lse.ext_in  = &pinmux.pad_din[2 * N_PORT_PINS + 14];
        rcc.al_cambiar = [this] { flash.revisa(); mco_ev_.notify(sc_core::SC_ZERO_TIME); };
        rcc.al_cargar_opciones = [this] { flash.recarga_opciones(); boot_ev_.notify(sc_core::SC_ZERO_TIME); };
        s_bor_lev_.write(3);
        s_true_.write(true);
    }

    void conecta_nucleo() {
        core.icode.bind(t_i_); core.dcode.bind(t_d_); core.sbus.bind(t_s_);
        core.ccm.bind(t_ccm_);
        core.fclk(s_hclk_); core.fclk_hz(s_hclk_hz_);
        core.systick_ext(s_stk_); core.rst_n(s_sysrst_n_);
        for (unsigned i = 0; i < mcu.nucleo.n_irq; ++i) core.irq_in[i](s_irq_[i]);
        core.nmi_in(s_nmi_);
        core.sysresetreq(s_sysresetreq_);
        core.sleeping(s_sleeping_); core.sleepdeep(s_sleepdeep_);
        core.event_in(s_evt_in_); core.event_out(s_evt_out_);
        core.boot_mode(s_boot_core_);
        core.debug.set_idcode(mcu.idcode());
        core.debug.set_mapa_congelacion(FZ_APB1_F303, FZ_APB2_F303);
        core.debug.swclk_tck(s_dbg_swclk_);
        core.debug.swdio_in(s_dbg_swdio_i_);
        core.debug.swdio_out(s_swdio_o_);
        core.debug.swdio_oe(s_swdio_oe_);
        core.debug.jtdi(s_dbg_jtdi_);
        core.debug.jtdo_swo(s_jtdo_);
        core.debug.njtrst(s_dbg_njtrst_);
        core.debug.dbg_lp(s_dbg_lp_);
        for (unsigned i = 0; i < FZ_COUNT; ++i) core.debug.freeze[i](s_freeze_[i]);
    }

    template <class B>
    void engancha(B& b, AhbDecoder& d, sc_core::sc_signal<bool>& clk,
                  sc_core::sc_signal<double>& hz, unsigned id) {
        d.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
        b.clk(clk); b.clk_hz(hz);
        b.rst_n(rcc.s_rst_n[id]); b.clk_en(rcc.s_en[id]);
        b.clk_en_live = rcc.clk_en_ptr(id);
        // Sin reloj, el acceso se pierde sin error, como en el silicio [RM0316
        // §9.4.6-9.4.8: «registers read or write access is not supported»]
        b.sin_reloj_ignora = true;
    }
    template <class B>
    void engancha_sin_reloj(B& b, AhbDecoder& d, sc_core::sc_signal<bool>& clk,
                            sc_core::sc_signal<double>& hz) {
        d.add_slave((std::string("s_") + b.basename()).c_str(), b.base(), b.size())->bind(b.tsk);
        b.clk(clk); b.clk_hz(hz); b.rst_n(s_sysrst_n_); b.clk_en(s_true_);
    }
    // Un bloque que este miembro de la familia no tiene (S9): su ventana
    // reservada, sin reloj ni reset
    std::vector<BloqueAusente*> ausentes;
    void ausente(const char* n, uint32_t base, uint32_t size, AhbDecoder& d,
                 sc_core::sc_signal<bool>& clk, sc_core::sc_signal<double>& hz) {
        BloqueAusente* b = new BloqueAusente(n, base, size, mcu.nombre, n);
        engancha_sin_reloj(*b, d, clk, hz);
        ausentes.push_back(b);
    }
    // Lo mismo para uno que el SoC lleva igual -un TIM-: se conecta como
    // siempre, pero no al bus, y su ventana es la reservada
    std::vector<std::unique_ptr<tlm_utils::simple_initiator_socket<SocF3>>> i_aus_;

    void declara(const char* n, uint32_t base, uint32_t size, AhbDecoder& d,
                 sc_core::sc_signal<bool>& clk, sc_core::sc_signal<double>& hz,
                 int id, const char* cuando) {
        BloqueDeclarado* b = new BloqueDeclarado(n, base, size, n, cuando);
        if (id >= 0) engancha(*b, d, clk, hz, unsigned(id));
        else engancha_sin_reloj(*b, d, clk, hz);
        declarados.push_back(b);
    }

    void conecta_bus() {
        for (auto* s : {&t_i_, &t_d_, &t_s_}) {
            s->register_b_transport(this, &SocF3::bt);
            s->register_transport_dbg(this, &SocF3::dbg);
        }
        t_ccm_.register_b_transport(this, &SocF3::bt_ccm);
        t_ccm_.register_transport_dbg(this, &SocF3::dbg_ccm);
        from_tb.register_b_transport(this, &SocF3::bt);
        from_tb.register_transport_dbg(this, &SocF3::dbg);
        a_flash_.bind(flash.mem);  a_ob_.bind(flash.ob);
        a_sram_.bind(sram.tsk);    a_ccm_.bind(ccm.tsk);
        a_sysmem_.bind(sysmem.tsk);
        a_apb1_.bind(br_apb1.ahb); a_apb2_.bind(br_apb2.ahb);
        a_ahb1_.bind(ahb1.tsk);    a_ahb2_.bind(ahb2.tsk);   a_ahb3_.bind(ahb3.tsk);
        br_apb1.pclk(s_pclk1_); br_apb1.pclk_hz(s_pclk1_hz_); br_apb1.apb.bind(apb1.tsk);
        br_apb2.pclk(s_pclk2_); br_apb2.pclk_hz(s_pclk2_hz_); br_apb2.apb.bind(apb2.tsk);
        sram.hclk(s_hclk_); sram.rst_n(s_sysrst_n_);
        ccm.hclk(s_hclk_);  ccm.rst_n(s_sysrst_n_);
        flash.irq(s_irq_[f3::IRQ_FLASH]);
        flash.al_obl = [this] { rcc.pide_reset_obl(); };
        flash.latencia_minima = [](double f) { return f3::latencia_minima(f); };

        const char* S5 = "Es de la fase S5 del plan (doc/analisis_stm32f303.md, S5).";
        const char* S6 = "Es de la fase S6 del plan (doc/analisis_stm32f303.md, S6).";
        auto& H = s_hclk_;  auto& Hz = s_hclk_hz_;
        auto& P1 = s_pclk1_; auto& P1z = s_pclk1_hz_;
        auto& P2 = s_pclk2_; auto& P2z = s_pclk2_hz_;
        // --- AHB1 --------------------------------------------------------------------
        ahb1.add_slave("s_rcc", rcc.base(), rcc.size())->bind(rcc.tsk);   // sus relojes, arriba
        engancha(flash, ahb1, H, Hz, F3P_FLITF);
        declara("tsc", f3::TSC_B, 0x400, ahb1, H, Hz, F3P_TSC, S5);
        // --- AHB2: los GPIO ------------------------------------------------------------
        for (unsigned p = 0; p < N_PUERTOS; ++p) engancha(gpio[p], ahb2, H, Hz, F3P_GPIOA + p);
        // --- AHB3: los ADC -------------------------------------------------------------
        engancha(adc12, ahb3, H, Hz, F3P_ADC12);
        if (mcu.tiene(F3A_ADC34)) {
            engancha(adc34, ahb3, H, Hz, F3P_ADC34);
        } else {
            // Como un TIM ausente: conectado, pero fuera del bus (S9)
            i_aus_.emplace_back(new tlm_utils::simple_initiator_socket<SocF3>("i_aus_adc34"));
            i_aus_.back()->bind(adc34.tsk);
            adc34.clk(H); adc34.clk_hz(Hz);
            adc34.rst_n(rcc.s_rst_n[F3P_ADC34]); adc34.clk_en(rcc.s_en[F3P_ADC34]);
            adc34.clk_en_live = rcc.clk_en_ptr(F3P_ADC34);
            ausente("ADC3/4", f3::ADC34_B, 0x400, ahb3, H, Hz);
        }
        // --- APB1 ----------------------------------------------------------------------
        // Los bloques de extension del I2S (el duplex completo): la base de pines
        // de ST no les da pin propio -comparten el MISO del SPI-, y quedan
        // declarados hasta que algo los pida
        const char* EXT = "El I2S en duplex completo (I2SxEXT) no esta modelado: comparte el pin de "
                          "MISO del SPI, y la base de pines de ST no lo lista.";
        declara("i2s2ext", f3::I2S2EXT_B, 0x400, apb1, P1, P1z, F3P_SPI2, EXT);
        declara("i2s3ext", f3::I2S3EXT_B, 0x400, apb1, P1, P1z, F3P_SPI3, EXT);
        if (mcu.tiene(F3A_USB)) {
            declara("usb", f3::USB_B, 0x400, apb1, P1, P1z, F3P_USB, S6);
            declara("usb_pma", f3::USB_PMA_B, 0x400, apb1, P1, P1z, F3P_USB, S6);
        } else {
            ausente("USB", f3::USB_B, 0x400, apb1, P1, P1z);
            ausente("la memoria de paquetes del USB", f3::USB_PMA_B, 0x400, apb1, P1, P1z);
        }
        declara("can", f3::CAN_B, 0x400, apb1, P1, P1z, F3P_CAN, S6);
        engancha(pwr, apb1, P1, P1z, F3P_PWR);
        engancha(dac1, apb1, P1, P1z, F3P_DAC1);
        // --- APB2 ----------------------------------------------------------------------
        engancha(syscfg, apb2, P2, P2z, F3P_SYSCFG);
        engancha_sin_reloj(exti, apb2, P2, P2z);
        // El PWR abre el dominio de respaldo
        pwr.dbp(s_dbp_);
        // El SYSCFG: EXTICR hacia el EXTI, y los pines de arranque
        syscfg.boot(s_boot_);
        for (unsigned l = 0; l < 16; ++l) {
            syscfg.exticr_sel[l](s_exticr_[l]);
            exti.exticr_sel[l](s_exticr_[l]);
        }
    }

    void conecta_pines() {
        for (unsigned p = 0; p < N_PUERTOS; ++p) {
            gpio[p].mux = &pinmux;
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                const unsigned k = p * N_PORT_PINS + i;
                gpio[p].pad_drive[i](pinmux.gpio_drive[k]);
                gpio[p].pad_din[i](pinmux.pad_din[k]);
                gpio[p].exti_line[i](s_exti_gpio_[k]);
            }
        }
        for (unsigned k = 0; k < N_GPIO_PORTS * N_PORT_PINS; ++k) exti.gpio_line[k](s_exti_gpio_[k]);
        auto af = [](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                     sc_core::sc_signal<bool>* i, bool idle = true) {
            AfEndpoint ep; ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = idle; return ep;
        };
        // AF0, el sistema [f3_pines.inc: SYS_*, RCC_MCO]
        pinmux.connect_af(0, 13, 0, af(&s_swdio_o_, &s_swdio_oe_, &s_dbg_swdio_i_, false));
        pinmux.connect_af(0, 14, 0, af(nullptr, nullptr, &s_dbg_swclk_, false));
        pinmux.connect_af(0, 15, 0, af(nullptr, nullptr, &s_dbg_jtdi_, false));
        pinmux.connect_af(1,  3, 0, af(&s_jtdo_, &s_true_, nullptr));
        pinmux.connect_af(1,  4, 0, af(nullptr, nullptr, &s_dbg_njtrst_, true));
        pinmux.connect_af(0,  8, 0, af(&rcc.mco_sig, &s_true_, nullptr));
        // EVENTOUT (AF15), en los pines en que la base de pines la pone
        for (const PinAf& x : tabla_af())
            if (x.af == 15 && std::strcmp(x.senal, "EVENTOUT") == 0)
                pinmux.connect_af(x.puerto, x.pin, 15, af(&s_evt_out_, &s_true_, nullptr));
    }

    // =======================================================================
    // S3: LOS PERIFÉRICOS DE SEGUNDA GENERACIÓN
    // =======================================================================
    enum : unsigned { T1, T2, T3, T4, T6, T7, T8, T15, T16, T17, N_TIM };
    TimerBase* tim_[N_TIM] = {};
    sc_core::sc_signal<bool> s_trgo_[N_TIM];
    sc_core::sc_signal<bool> q_t_[N_TIM][7];        // TimerBase::DmaLine
    sc_core::sc_signal<bool> s_tirq_[N_TIM][5];     // global, up, cc, trg_com, brk
    sc_core::sc_signal<bool> q_u_[5][2], q_spi_[3][2], q_i2c_[2][2];   // rx, tx
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_dreq1_{"s_dreq1", 7}, s_dreq2_{"s_dreq2", 5};
    sc_core::sc_signal<uint32_t> s_cfgr1_{"s_cfgr1"};
    sc_core::sc_signal<bool> s_t1_itr3_{"s_t1_itr3"}, s_irout_{"s_irout"};
    sc_core::sc_signal<bool> s_iwdg_lsi_{"s_iwdg_lsi"}, s_iwdg_hw_{"s_iwdg_hw"};
    sc_core::sc_signal<bool> s_stby_pines_{"s_stby_pines"};
    sc_core::sc_signal<bool> s_ewup1_{"s_ewup1"};
    sc_core::sc_signal<double> s_cero_hz_{"s_cero_hz"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_nc_{"s_nc", 96};
    unsigned nc_ = 0;
    sc_core::sc_signal<bool>& nc() { return s_nc_[nc_++]; }
    tlm_utils::simple_target_socket<SocF3> t_dma1_{"t_dma1"}, t_dma2_{"t_dma2"};
    // S5
    sc_core::sc_signal<bool> q_adc_[4], q_dac_[2];            // ADC1..4; DAC canal 1, 2
    sc_core::sc_signal<bool> s_adc12_irq_[2], s_dac_irq_{"s_dac_irq"};
    sc_core::sc_signal<bool> s_exti_ln_[4];                   // EXTI2, 9, 11, 15 (disparos)
    sc_core::sc_signal<bool> s_dac_t8_{"s_dac_t8"};           // TSEL 1: TIM8 o TIM3 TRGO
    bool ana_falta_[11] = {};                                  // COMP1-7, OPAMP1-4 que no hay
    bool av_ana_falta_ = false;

    void tim(TimerBase& t, unsigned k, AhbDecoder& d, sc_core::sc_signal<bool>& pclk,
             sc_core::sc_signal<double>& pclk_hz, unsigned id, sc_core::sc_signal<bool>& tclk,
             sc_core::sc_signal<double>& tclk_hz, unsigned fz,
             std::initializer_list<sc_core::sc_signal<bool>*> itr) {
        tim_[k] = &t;
        const bool falta = (k == T7 && !mcu.tiene(F3A_TIM7)) || (k == T8 && !mcu.tiene(F3A_TIM8));
        if (!falta) {
            engancha(t, d, pclk, pclk_hz, id);
        } else {
            i_aus_.emplace_back(new tlm_utils::simple_initiator_socket<SocF3>(
                (std::string("i_aus_") + t.basename()).c_str()));
            i_aus_.back()->bind(t.tsk);
            t.clk(pclk); t.clk_hz(pclk_hz);
            t.rst_n(rcc.s_rst_n[id]); t.clk_en(rcc.s_en[id]);
            t.clk_en_live = rcc.clk_en_ptr(id);
            ausente(k == T7 ? "TIM7" : "TIM8", t.base(), t.size(), d, pclk, pclk_hz);
        }
        t.timclk(tclk); t.timclk_hz(tclk_hz);
        t.freeze(s_freeze_[fz]);
        unsigned i = 0;
        for (auto* x : itr) t.itr[i++](*x);
        for (; i < 4; ++i) t.itr[i](s_false_);
        t.trgo(s_trgo_[k]);
        t.irq_global(s_tirq_[k][0]); t.irq_up(s_tirq_[k][1]); t.irq_cc(s_tirq_[k][2]);
        t.irq_trg_com(s_tirq_[k][3]); t.irq_brk(s_tirq_[k][4]);
        t.dma_up(q_t_[k][TimerBase::D_UP]); t.dma_trig(q_t_[k][TimerBase::D_TRIG]);
        t.dma_com(q_t_[k][TimerBase::D_COM]);
        for (unsigned c = 0; c < 4; ++c) t.dma_cc[c](q_t_[k][TimerBase::D_CC1 + c]);
    }
    void usart(UsartV2& u, unsigned n, AhbDecoder& d, sc_core::sc_signal<bool>& pclk,
               sc_core::sc_signal<double>& pclk_hz, unsigned id, unsigned irq) {
        engancha(u, d, pclk, pclk_hz, id);
        u.ker_clk_hz(rcc.s_ker[RccF3::K_USART1 + n]);
        u.irq(s_irq_[irq]);
        u.dma_req_rx(q_u_[n][0]); u.dma_req_tx(q_u_[n][1]);
    }
    void spi(SpiBase& x, unsigned n, AhbDecoder& d, sc_core::sc_signal<bool>& pclk,
             sc_core::sc_signal<double>& pclk_hz, unsigned id, unsigned irq) {
        engancha(x, d, pclk, pclk_hz, id);
        x.irq(s_irq_[irq]);
        x.dma_req_rx(q_spi_[n][0]); x.dma_req_tx(q_spi_[n][1]);
        x.i2s_ext_clk(s_false_);
        if (n == 0) x.i2s_clk_hz(s_cero_hz_); else x.i2s_clk_hz(rcc.s_ker[RccF3::K_I2S]);
        x.ext_ck(s_false_); x.ext_ws(s_false_);
    }
    void i2c(FmpI2c& x, unsigned n, unsigned id, unsigned irq_ev) {
        engancha(x, apb1, s_pclk1_, s_pclk1_hz_, id);
        x.ker_clk_hz(rcc.s_ker[RccF3::K_I2C1 + n]);
        x.irq_ev(s_irq_[irq_ev]); x.irq_er(s_irq_[irq_ev + 1]);
        x.dma_req_rx(q_i2c_[n][0]); x.dma_req_tx(q_i2c_[n][1]);
    }

    void conecta_s3() {
        auto& H = s_hclk_;  auto& Hz = s_hclk_hz_;
        auto& P1 = s_pclk1_; auto& P1z = s_pclk1_hz_;
        auto& P2 = s_pclk2_; auto& P2z = s_pclk2_hz_;
        // --- Los DMA: esclavos de AHB y maestros del bus ------------------------------
        engancha(dma1, ahb1, H, Hz, F3P_DMA1);
        engancha(dma2, ahb1, H, Hz, F3P_DMA2);
        for (auto* t : {&t_dma1_, &t_dma2_}) {
            t->register_b_transport(this, &SocF3::bt_dma);
            t->register_transport_dbg(this, &SocF3::dbg);
        }
        dma1.m.bind(t_dma1_); dma2.m.bind(t_dma2_);
        for (unsigned c = 0; c < 7; ++c) { dma1.req[c](s_dreq1_[c]); dma1.irq[c](s_irq_[f3::IRQ_DMA1_CH1 + c]); }
        for (unsigned c = 0; c < 5; ++c) { dma2.req[c](s_dreq2_[c]); dma2.irq[c](s_irq_[f3::IRQ_DMA2_CH1 + c]); }
        syscfg.cfgr1_sig(s_cfgr1_);
        engancha(crc, ahb1, H, Hz, F3P_CRC);
        // --- Los temporizadores y su interconexión [RM0316, tablas 121, 125 y 129] ----
        tim(tim1, T1, apb2, P2, P2z, F3P_TIM1, s_tim2_, rcc.s_ker[RccF3::K_TIM1], FZ3_TIM1,
            {&s_trgo_[T15], &s_trgo_[T2], &s_trgo_[T3], &s_t1_itr3_});
        tim(tim8, T8, apb2, P2, P2z, F3P_TIM8, s_tim2_, rcc.s_ker[RccF3::K_TIM8], FZ3_TIM8,
            {&s_trgo_[T1], &s_trgo_[T2], &s_trgo_[T4], &s_trgo_[T3]});
        tim(tim2, T2, apb1, P1, P1z, F3P_TIM2, s_tim1_, s_tim1_hz_, FZ3_TIM2,
            {&s_trgo_[T1], &s_trgo_[T8], &s_trgo_[T3], &s_trgo_[T4]});
        tim(tim3, T3, apb1, P1, P1z, F3P_TIM3, s_tim1_, s_tim1_hz_, FZ3_TIM3,
            {&s_trgo_[T1], &s_trgo_[T2], &s_trgo_[T15], &s_trgo_[T4]});
        tim(tim4, T4, apb1, P1, P1z, F3P_TIM4, s_tim1_, s_tim1_hz_, FZ3_TIM4,
            {&s_trgo_[T1], &s_trgo_[T2], &s_trgo_[T3], &s_trgo_[T8]});
        tim(tim6, T6, apb1, P1, P1z, F3P_TIM6, s_tim1_, s_tim1_hz_, FZ3_TIM6, {});
        tim(tim7, T7, apb1, P1, P1z, F3P_TIM7, s_tim1_, s_tim1_hz_, FZ3_TIM7, {});
        tim(tim15, T15, apb2, P2, P2z, F3P_TIM15, s_tim2_, s_tim2_hz_, FZ3_TIM15,
            {&s_trgo_[T2], &s_trgo_[T3], &tim16.ch_out[0], &tim17.ch_out[0]});
        tim(tim16, T16, apb2, P2, P2z, F3P_TIM16, s_tim2_, s_tim2_hz_, FZ3_TIM16, {});
        tim(tim17, T17, apb2, P2, P2z, F3P_TIM17, s_tim2_, s_tim2_hz_, FZ3_TIM17, {});
        // --- Las USART, con su reloj de núcleo [RCC_CFGR3] -------------------------------
        usart(usart1, 0, apb2, P2, P2z, F3P_USART1, f3::IRQ_USART1);
        usart(usart2, 1, apb1, P1, P1z, F3P_USART2, f3::IRQ_USART2);
        usart(usart3, 2, apb1, P1, P1z, F3P_USART3, f3::IRQ_USART3);
        usart(uart4, 3, apb1, P1, P1z, F3P_UART4, f3::IRQ_UART4);
        usart(uart5, 4, apb1, P1, P1z, F3P_UART5, f3::IRQ_UART5);
        // --- Los SPI v3 y los I²C de segunda generación ------------------------------------
        spi(spi1, 0, apb2, P2, P2z, F3P_SPI1, f3::IRQ_SPI1);
        spi(spi2, 1, apb1, P1, P1z, F3P_SPI2, f3::IRQ_SPI2);
        spi(spi3, 2, apb1, P1, P1z, F3P_SPI3, f3::IRQ_SPI3);
        i2c(i2c1, 0, F3P_I2C1, f3::IRQ_I2C1_EV);
        i2c(i2c2, 1, F3P_I2C2, f3::IRQ_I2C2_EV);
        // --- Los perros y el RTC -----------------------------------------------------------
        engancha(wwdg, apb1, P1, P1z, F3P_WWDG);
        wwdg.irq_ewi(s_irq_[f3::IRQ_WWDG]);
        wwdg.rst_req(s_wwdg_rr_);
        wwdg.freeze(s_freeze_[FZ3_WWDG]);
        // El IWDG y el RTC no tienen bit de reloj: el IWDG va al LSI, y el RTC al
        // RTCCLK, con el dominio de respaldo
        engancha_sin_reloj(iwdg, apb1, P1, P1z);
        iwdg.lsi_clk(s_lsi_); iwdg.lsi_hz(s_lsi_hz_);
        iwdg.rst_req(s_iwdg_rr_);
        iwdg.lsi_on_req(s_iwdg_lsi_);
        iwdg.freeze(s_freeze_[FZ3_IWDG]);
        iwdg.hw_start(s_iwdg_hw_);
        engancha_sin_reloj(rtc, apb1, P1, P1z);
        rtc.rtcclk(s_rtc_); rtc.rtcclk_hz(s_rtc_hz_);
        rtc.bkp_rst_n(s_bkprst_n_); rtc.dbp(s_dbp_);
        // En el F3 la alarma va a la línea 17, el sello y la intrusión a la 19 y el
        // despertador a la 20 [RM0316 §14.2.5, tabla 33]
        rtc.exti17_alarm(s_exti_in_[17 - 16]);
        rtc.exti21_tamp_ts(s_exti_in_[19 - 16]);
        rtc.exti22_wakeup(s_exti_in_[20 - 16]);
        // --- El PWR: el bajo consumo -------------------------------------------------------
        pwr.pvd(s_exti_in_[16 - 16]);
        pwr.standby_pines(s_stby_pines_);
        pwr.ewup1(s_ewup1_);
        pwr.vdd_lvl(s_vdd_);
        pwr.sleeping(s_sleeping_); pwr.sleepdeep(s_sleepdeep_);
        pwr.exti_wakeup(s_exti_wakeup_);
        pwr.nrst_n(s_nrst_n_);
        pwr.dbg_lp(s_dbg_lp_);
        pwr.wkup[0](pinmux.pad_din[0 * N_PORT_PINS + 0]);     // WKUP1: PA0
        pwr.wkup[1](pinmux.pad_din[2 * N_PORT_PINS + 13]);    // WKUP2: PC13
        pwr.wkup[2](pinmux.pad_din[4 * N_PORT_PINS + 6]);     // WKUP3: PE6
        pwr.rtc_ev[0](s_exti_in_[17 - 16]);
        pwr.rtc_ev[1](s_exti_in_[19 - 16]);
        pwr.rtc_ev[2](s_exti_in_[20 - 16]);
        pwr.al_parar = [this](bool on) { rcc.para(on); };
        pwr.al_standby = [this](bool on) { rcc.standby(on); };
        pwr.reset_bajo_consumo = [this] { rcc.reset_bajo_consumo(); };
        pwr.nrst_stop = [this] { return flash.nrst_stop(); };
        pwr.nrst_stdby = [this] { return flash.nrst_stdby(); };
        s_cero_hz_.write(0.0);
    }

    // =======================================================================
    // S5: LO ANALÓGICO
    // =======================================================================
    double vdda_v() const { const double v = s_vdda_.read(); return v > 0 ? v : 3.3; }
    // Las fuentes de disparo de los ADC y el DAC, por su nombre [RM0316 tablas
    // 89-92]. TIMx_CCy es el OCyREF del canal [de modelo]
    sc_core::sc_signal<bool>& disparo(const std::string& n) {
        static const char* tn[N_TIM] = {"TIM1_", "TIM2_", "TIM3_", "TIM4_", "TIM6_", "TIM7_",
                                        "TIM8_", "TIM15_", "TIM16_", "TIM17_"};
        if (n == "EXTI2")  return s_exti_ln_[0];
        if (n == "EXTI9")  return s_exti_ln_[1];
        if (n == "EXTI11") return s_exti_ln_[2];
        if (n == "EXTI15") return s_exti_ln_[3];
        for (unsigned k = 0; k < N_TIM; ++k) {
            const std::string p = tn[k];
            if (n.compare(0, p.size(), p) != 0) continue;
            const std::string f = n.substr(p.size());
            if (f == "TRGO")  return s_trgo_[k];
            if (f == "TRGO2") return tim_[k]->trgo2_sig;
            if (f.size() == 3 && f.compare(0, 2, "CC") == 0) return tim_[k]->ocref[f[2] - '1'];
        }
        SC_REPORT_FATAL("stm32f303", ("disparo desconocido: " + n).c_str());
        return s_false_;
    }
    void conecta_s5() {
        static const char* const EXT12[16] = {
            "TIM1_CC1", "TIM1_CC2", "TIM1_CC3", "TIM2_CC2", "TIM3_TRGO", "TIM4_CC4", "EXTI11",
            "TIM8_TRGO", "TIM8_TRGO2", "TIM1_TRGO", "TIM1_TRGO2", "TIM2_TRGO", "TIM4_TRGO",
            "TIM6_TRGO", "TIM15_TRGO", "TIM3_CC4"};
        static const char* const JEXT12[16] = {
            "TIM1_TRGO", "TIM1_CC4", "TIM2_TRGO", "TIM2_CC1", "TIM3_CC4", "TIM4_TRGO", "EXTI15",
            "TIM8_CC4", "TIM1_TRGO2", "TIM8_TRGO", "TIM8_TRGO2", "TIM3_CC3", "TIM3_TRGO",
            "TIM3_CC1", "TIM6_TRGO", "TIM15_TRGO"};
        static const char* const EXT34[16] = {
            "TIM3_CC1", "TIM2_CC3", "TIM1_CC3", "TIM8_CC1", "TIM8_TRGO", "EXTI2", "TIM4_CC1",
            "TIM2_TRGO", "TIM8_TRGO2", "TIM1_TRGO", "TIM1_TRGO2", "TIM3_TRGO", "TIM4_TRGO",
            "TIM7_TRGO", "TIM15_TRGO", "TIM2_CC1"};
        static const char* const JEXT34[16] = {
            "TIM1_TRGO", "TIM1_CC4", "TIM4_CC3", "TIM8_CC2", "TIM8_CC4", "TIM4_CC3", "TIM4_CC4",
            "TIM4_TRGO", "TIM1_TRGO2", "TIM8_TRGO", "TIM8_TRGO2", "TIM1_CC3", "TIM3_TRGO",
            "TIM2_TRGO", "TIM7_TRGO", "TIM15_TRGO"};
        for (unsigned i = 0; i < 16; ++i) {
            adc12.ext[i](disparo(EXT12[i]));  adc12.jext[i](disparo(JEXT12[i]));
            adc34.ext[i](disparo(EXT34[i]));  adc34.jext[i](disparo(JEXT34[i]));
        }
        // Los relojes: ADCxy_CK del RCC (CKMODE = 00) o HCLK
        adc12.ker_hz(rcc.s_ker[RccF3::K_ADC12]);
        adc34.ker_hz(rcc.s_ker[RccF3::K_ADC34]);
        // Las interrupciones: ADC1 y ADC2 comparten la 18 [CMSIS: ADC1_2_IRQn]
        adc12.irq_m(s_adc12_irq_[0]); adc12.irq_s(s_adc12_irq_[1]);
        adc34.irq_m(s_irq_[f3::IRQ_ADC3]); adc34.irq_s(s_irq_[f3::IRQ_ADC4]);
        adc12.dma_m(q_adc_[0]); adc12.dma_s(q_adc_[1]);
        adc34.dma_m(q_adc_[2]); adc34.dma_s(q_adc_[3]);
        // El fin de bloque del DMA: el ADC de una vez deja de pedir
        dma1.al_terminar = [this](unsigned c) { if (c == 0) adc12.dma_fin(0); };
        dma2.al_terminar = [this](unsigned c) {
            const bool r24 = (s_cfgr1_.read() >> 8) & 1u;
            if (c == (r24 ? 2u : 0u)) adc12.dma_fin(1);
            if (c == (r24 ? 3u : 1u)) adc34.dma_fin(1);
            if (c == 4) adc34.dma_fin(0);
        };
        adc12.vref = [this] { return vdda_v(); };
        adc34.vref = [this] { return vdda_v(); };
        // Las entradas externas [f3_analogico.inc, generado]
        struct Ain { uint8_t adc, canal, puerto, pin; };
        static const Ain ain[] = {
#include "f3_analogico.inc"
        };
        for (const Ain& a : ain) {
            analog_net_if* n = &pinmux.analog(a.puerto, a.pin);
            AdcParV3& par = a.adc <= 2 ? adc12 : adc34;
            par.fuente((a.adc - 1u) & 1u, a.canal, [n] { return double(n->voltage()); });
        }
        // Las internas [RM0316 §15.3.4, tablas 85-86; DS9118 tablas 66 y 79]
        auto vrefint = [this](const AdcParV3& p) { return ((p.ccr() >> 22) & 1u) ? f3::V_REFINT : 0.0; };
        adc12.fuente(0, 15, [this] { return opamp.vrefopamp(0); });
        adc12.fuente(0, 16, [this] {
            if (!((adc12.ccr() >> 23) & 1u)) return 0.0;                    // TSEN
            return f3::V25_TS - (temp_c - 25.0) * f3::AVG_SLOPE_TS;
        });
        adc12.fuente(0, 17, [this] { return ((adc12.ccr() >> 24) & 1u) ? s_vbat_.read() / 2.0 : 0.0; });
        adc12.fuente(0, 18, [this, vrefint] { return vrefint(adc12); });
        adc12.fuente(1, 17, [this] { return opamp.vrefopamp(1); });
        adc12.fuente(1, 18, [this, vrefint] { return vrefint(adc12); });
        adc34.fuente(0, 17, [this] { return opamp.vrefopamp(2); });
        adc34.fuente(0, 18, [this, vrefint] { return vrefint(adc34); });
        adc34.fuente(1, 17, [this] { return opamp.vrefopamp(3); });
        adc34.fuente(1, 18, [this, vrefint] { return vrefint(adc34); });
        // El DAC1: PA4 y PA5, y sus disparos [RM0316 tabla 97]
        dac1.irq(s_dac_irq_);
        dac1.dma_req_ch1(q_dac_[0]); dac1.dma_req_ch2(q_dac_[1]);
        dac1.vref(s_vdda_);
        dac1.trig[0](s_trgo_[T6]); dac1.trig[1](s_dac_t8_); dac1.trig[2](s_trgo_[T7]);
        dac1.trig[3](s_trgo_[T15]); dac1.trig[4](s_trgo_[T2]); dac1.trig[5](s_trgo_[T4]);
        dac1.trig[6](s_exti_ln_[1]); dac1.trig[7](s_false_);
        dac1.bind_out(0, pinmux.analog(0, 4));
        dac1.bind_out(1, pinmux.analog(0, 5));
        // Los comparadores y los amplificadores, con los registros en el SYSCFG
        comp.vdda = [this] { return vdda_v(); };
        comp.vrefint = [] { return f3::V_REFINT; };
        opamp.vdda = [this] { return vdda_v(); };
        // Los que la línea no tiene (el F302: COMP3/5/7 y OPAMP3/4 [PINDATA]) son
        // registros reservados: leen cero, no se escriben y se dice una vez
        for (unsigned i = 0; i < CompF3::N + OpampF3::N; ++i) {
            const std::string n = i < CompF3::N ? "COMP" + std::to_string(i + 1)
                                                : "OPAMP" + std::to_string(i - CompF3::N + 1);
            const std::string sin = std::string(" ") + mcu.sin + " ";
            ana_falta_[i] = sin.find(" " + n + " ") != std::string::npos;
        }
        syscfg.analogico.lee = [this](unsigned i) -> uint32_t {
            if (i < 11 && ana_falta_[i]) return 0;
            return i < CompF3::N ? comp.lee(i) : opamp.lee(i - CompF3::N);
        };
        syscfg.analogico.escribe = [this](unsigned i, uint32_t v) {
            if (i < 11 && ana_falta_[i]) {
                if (!av_ana_falta_) {
                    av_ana_falta_ = true;
                    const std::string m = std::string(mcu.nombre) + " no tiene COMP3/5/7 ni OPAMP3/4 "
                                          "[PINDATA]: su registro es reservado";
                    SC_REPORT_WARNING("stm32f303", m.c_str());
                }
                return;
            }
            if (i < CompF3::N) comp.escribe(i, v); else opamp.escribe(i - CompF3::N, v);
        };
        syscfg.analogico.reinicia = [this] { comp.reinicia(); opamp.reinicia(); };
    }
    void irq_adc_proc() { s_irq_[f3::IRQ_ADC1_2].write(s_adc12_irq_[0].read() || s_adc12_irq_[1].read()); }
    // La línea n del EXTI antes de la máscara: el pin que elige EXTICR
    void exti_ln_proc() {
        static const unsigned l[4] = {2, 9, 11, 15};
        for (unsigned k = 0; k < 4; ++k) {
            const unsigned port = s_exticr_[l[k]].read();
            const unsigned i = port * N_PORT_PINS + l[k];
            s_exti_ln_[k].write(i < s_exti_gpio_.size() && s_exti_gpio_[i].read());
        }
    }
    // TSEL = 001 del DAC1: el TRGO del TIM8 o, con DAC1_TRIG1_RMP (CFGR1 bit 7), el del TIM3
    void dac_trig_proc() {
        s_dac_t8_.write(((s_cfgr1_.read() >> 7) & 1u) ? s_trgo_[T3].read() : s_trgo_[T8].read());
    }
    // Los comparadores: al EXTI (21, 22, 29-33) y a los frenos de TIM1/TIM8
    // (OUTSEL 0001-0101) [RM0316 §17.3.4, tabla 33]
    void comp_proc() {
        static const unsigned linea[CompF3::N] = {21, 22, 29, 30, 31, 32, 33};
        bool b1 = false, b1_2 = false, b8 = false, b8_2 = false;
        for (unsigned i = 0; i < CompF3::N; ++i) {
            const bool o = comp.out[i].read();
            s_exti_in_[linea[i] - 16].write(o);
            if (!o || !comp.activo(i)) continue;
            switch (comp.outsel(i)) {
                case 1: b1 = true; break;
                case 2: b1_2 = true; break;
                case 3: b8 = true; break;
                case 4: b8_2 = true; break;
                case 5: b1_2 = true; b8_2 = true; break;
                default: break;
            }
        }
        tim1.brk_acth.write(b1); tim1.brk2_acth.write(b1_2);
        tim8.brk_acth.write(b8); tim8.brk2_acth.write(b8_2);
    }

    // Las interrupciones compartidas de los temporizadores [CMSIS: IRQn_Type]
    void irq_tim_proc() {
        auto g = [&](unsigned k, unsigned j) { return s_tirq_[k][j].read(); };
        s_irq_[f3::IRQ_TIM1_BRK_TIM15].write(g(T1, 4) || g(T15, 0));
        s_irq_[f3::IRQ_TIM1_UP_TIM16].write(g(T1, 1) || g(T16, 0));
        s_irq_[f3::IRQ_TIM1_TRG_COM_TIM17].write(g(T1, 3) || g(T17, 0));
        s_irq_[f3::IRQ_TIM1_CC].write(g(T1, 2));
        s_irq_[f3::IRQ_TIM2].write(g(T2, 0));
        s_irq_[f3::IRQ_TIM3].write(g(T3, 0));
        s_irq_[f3::IRQ_TIM4].write(g(T4, 0));
        s_irq_[f3::IRQ_TIM8_BRK].write(g(T8, 4));
        s_irq_[f3::IRQ_TIM8_UP].write(g(T8, 1));
        s_irq_[f3::IRQ_TIM8_TRG_COM].write(g(T8, 3));
        s_irq_[f3::IRQ_TIM8_CC].write(g(T8, 2));
        s_irq_[f3::IRQ_TIM6_DAC].write(g(T6, 0) || s_dac_irq_.read());
        s_irq_[f3::IRQ_TIM7].write(g(T7, 0));
    }

    // Las peticiones de DMA, sumadas por canal, con los remapeos de
    // SYSCFG_CFGR1 [RM0316 §13.3.2, tablas 76 y 78]
    void dma_req_proc() {
        const uint32_t c = s_cfgr1_.read();
        const bool r16 = (c >> 11) & 1u, r17 = (c >> 12) & 1u;
        const bool r6 = (c >> 13) & 1u, r7 = (c >> 14) & 1u;
        enum { UP = 0, CC1, CC2, CC3, CC4, COM, TRIG };
        auto t = [&](unsigned k, unsigned l) { return q_t_[k][l].read(); };
        auto u = [&](unsigned n, unsigned d) { return q_u_[n][d].read(); };
        auto sp = [&](unsigned n, unsigned d) { return q_spi_[n][d].read(); };
        auto ic = [&](unsigned n, unsigned d) { return q_i2c_[n][d].read(); };
        const bool t16 = t(T16, CC1) || t(T16, UP), t17 = t(T17, CC1) || t(T17, UP);
        // Los ADC y el DAC1 (S5): ADC24_DMA_RMP (bit 8) lleva el ADC2 y el ADC4 a los
        // canales 3 y 4 del DMA2; TIM6DAC1 y TIM7DAC2 (13, 14), el DAC al DMA1
        const bool r24 = (c >> 8) & 1u;
        const bool a1 = q_adc_[0].read(), a2 = q_adc_[1].read(), a3 = q_adc_[2].read(), a4 = q_adc_[3].read();
        const bool d1 = q_dac_[0].read(), d2 = q_dac_[1].read();
        // DMA1
        s_dreq1_[0].write(a1 || t(T2, CC3) || t(T4, CC1) || (!r17 && t17));
        s_dreq1_[1].write(sp(0, 0) || u(2, 1) || t(T1, CC1) || t(T2, UP) || t(T3, CC3));
        s_dreq1_[2].write(sp(0, 1) || u(2, 0) || t(T1, CC2) || t(T3, CC4) || t(T3, UP) ||
                          (r6 && (t(T6, UP) || d1)) || (!r16 && t16));
        s_dreq1_[3].write(sp(1, 0) || u(0, 1) || ic(1, 1) || t(T1, CC4) || t(T1, TRIG) ||
                          t(T1, COM) || t(T4, CC2) || (r7 && (t(T7, UP) || d2)));
        s_dreq1_[4].write(sp(1, 1) || u(0, 0) || ic(1, 0) || t(T1, UP) || t(T2, CC1) ||
                          t(T4, CC3) || t(T15, CC1) || t(T15, UP) || t(T15, TRIG) || t(T15, COM));
        s_dreq1_[5].write(u(1, 0) || ic(0, 1) || t(T1, CC3) || t(T3, CC1) || t(T3, TRIG) ||
                          (r16 && t16));
        s_dreq1_[6].write(u(1, 1) || ic(0, 0) || t(T2, CC2) || t(T2, CC4) || t(T4, UP) ||
                          (r17 && t17));
        // DMA2
        s_dreq2_[0].write(sp(2, 0) || t(T8, CC3) || t(T8, UP) || (!r24 && a2));
        s_dreq2_[1].write(sp(2, 1) || t(T8, CC4) || t(T8, TRIG) || t(T8, COM) || (!r24 && a4));
        s_dreq2_[2].write(u(3, 0) || (!r6 && (t(T6, UP) || d1)) || t(T8, CC1) || (r24 && a2));
        s_dreq2_[3].write((!r7 && (t(T7, UP) || d2)) || (r24 && a4));
        s_dreq2_[4].write(u(3, 1) || t(T8, CC2) || a3);
    }
    // El ITR3 del TIM1: el TRGO del TIM4 o, con TIM1_ITR3_RMP, el OC1 del TIM17
    void itr3_proc() {
        s_t1_itr3_.write(((s_cfgr1_.read() >> 6) & 1u) ? tim17.ch_out[0].read() : s_trgo_[T4].read());
    }
    // El IRTIM: la portadora del TIM17 modulada por la envolvente del TIM16
    // [RM0316 §24]
    void ti16_proc() { tim16.ti1_rmp[2].write(rcc.mco_sig.read()); }
    void irtim_proc() { s_irout_.write(tim16.ch_out[0].read() && tim17.ch_out[0].read()); }
    // El IWDG pide el LSI; y al salir de reset, WDG_SW = 0 lo arranca solo
    void iwdg_proc() {
        rcc.pide_lsi(s_iwdg_lsi_.read());
        if (s_sysrst_n_.read()) s_iwdg_hw_.write(!flash.wdg_sw());
        else s_iwdg_hw_.write(false);
    }

    // Los pines de las funciones alternativas de S3, por su nombre en la base
    // de pines de ST [f3_pines.inc]
    bool endpoint(const char* n, AfEndpoint& ep) {
        auto E = [&](sc_core::sc_signal<bool>* o, sc_core::sc_signal<bool>* e,
                     sc_core::sc_signal<bool>* i, bool idle) {
            ep.out = o; ep.oe = e; ep.in = i; ep.idle_in = idle; return true;
        };
        std::string s(n);
        auto empieza = [&](const char* p) { return s.compare(0, std::strlen(p), p) == 0; };
        // USART y UART
        UsartV2* us[5] = {&usart1, &usart2, &usart3, &uart4, &uart5};
        const char* un[5] = {"USART1_", "USART2_", "USART3_", "UART4_", "UART5_"};
        for (unsigned k = 0; k < 5; ++k) {
            if (!empieza(un[k])) continue;
            UsartV2& u = *us[k];
            const std::string f = s.substr(std::strlen(un[k]));
            if (f == "TX")  return E(&u.tx_out, &u.tx_oe, &u.tx_in, true);
            if (f == "RX")  return E(&u.rx_out, &u.rx_oe, &u.rx_in, true);
            if (f == "CK")  return E(&u.ck_out, &u.ck_oe, nullptr, true);
            if (f == "CTS") return E(nullptr, nullptr, &u.cts_in, true);
            if (f == "RTS" || f == "DE") return E(&u.rts_out, &u.rts_oe, nullptr, true);
            return false;
        }
        // SPI e I2S
        SpiBase* sps[3] = {&spi1, &spi2, &spi3};
        for (unsigned k = 0; k < 3; ++k) {
            const std::string ps = "SPI" + std::to_string(k + 1) + "_", pi = "I2S" + std::to_string(k + 1) + "_";
            SpiBase& x = *sps[k];
            if (s == ps + "SCK" || s == pi + "CK")   return E(&x.sck_out, &x.sck_oe, &x.sck_in, false);
            if (s == ps + "MISO")                     return E(&x.miso_out, &x.miso_oe, &x.miso_in, false);
            if (s == ps + "MOSI" || s == pi + "SD")   return E(&x.mosi_out, &x.mosi_oe, &x.mosi_in, false);
            if (s == ps + "NSS" || s == pi + "WS")    return E(&x.nss_out, &x.nss_oe, &x.nss_in, false);
            if (s == pi + "MCK")                      return E(&x.mck_out, &x.mck_oe, nullptr, false);
        }
        // I²C, en colector abierto (lo pone el GPIO: AF_OD)
        if (s == "I2C1_SCL") return E(&i2c1.scl_out, &i2c1.scl_oe, &i2c1.scl_in, true);
        if (s == "I2C1_SDA") return E(&i2c1.sda_out, &i2c1.sda_oe, &i2c1.sda_in, true);
        if (s == "I2C2_SCL") return E(&i2c2.scl_out, &i2c2.scl_oe, &i2c2.scl_in, true);
        if (s == "I2C2_SDA") return E(&i2c2.sda_out, &i2c2.sda_oe, &i2c2.sda_in, true);
        // Las salidas de los comparadores (S5)
        if (s.size() == 9 && empieza("COMP") && s.compare(5, 4, "_OUT") == 0) {
            const unsigned k = unsigned(s[4] - '1');
            if (k < CompF3::N) return E(&comp.out[k], &s_true_, nullptr, false);
        }
        // Los temporizadores
        if (s == "IR_OUT") return E(&s_irout_, &s_true_, nullptr, false);
        static const char* tn[N_TIM] = {"TIM1_", "TIM2_", "TIM3_", "TIM4_", "TIM6_", "TIM7_",
                                        "TIM8_", "TIM15_", "TIM16_", "TIM17_"};
        for (unsigned k = 0; k < N_TIM; ++k) {
            if (!empieza(tn[k])) continue;
            TimerBase& t = *tim_[k];
            const std::string f = s.substr(std::strlen(tn[k]));
            if (f.size() == 3 && f.compare(0, 2, "CH") == 0) {
                const unsigned c = unsigned(f[2] - '1');
                if (c < 4) return E(&t.ch_out[c], &t.ch_oe[c], &t.ch_in[c], false);
            }
            if (f.size() == 4 && f.compare(0, 2, "CH") == 0 && f[3] == 'N') {
                const unsigned c = unsigned(f[2] - '1');
                if (c < 3) return E(&t.chn_out[c], &t.chn_oe[c], nullptr, false);
            }
            if (f == "ETR")   return E(nullptr, nullptr, &t.etr_in, false);
            if (f == "BKIN")  return E(nullptr, nullptr, &t.bkin_in, true);
            if (f == "BKIN2") return E(nullptr, nullptr, &t.bkin2_in, true);
            return false;
        }
        return false;
    }
    void conecta_pines_s3() {
        for (const PinAf& x : tabla_af()) {
            AfEndpoint ep;
            if (endpoint(x.senal, ep)) pinmux.connect_af(x.puerto, x.pin, x.af, ep);
        }
    }

    // El camino de los DMA: lo de la CPU, menos la CCM, que solo ve el núcleo
    // [RM0316 §3.1, figura 2]
    void bt_dma(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (a >= f3::CCM_B && a < uint64_t(f3::CCM_B) + ccm.size()) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        bt(gp, t);
    }

public:
    // La tabla de funciones alternativas del encapsulado, generada [f3_pines.inc]
    struct PinAf { uint8_t puerto, pin, af; const char* senal; };
    static const std::vector<PinAf>& tabla_af() {
        static const std::vector<PinAf> t = {
#include "f3_pines.inc"
        };
        return t;
    }

private:
    void conecta_irq() {
        exti.irq_exti0(s_irq_[f3::IRQ_EXTI0]);
        exti.irq_exti1(s_irq_[f3::IRQ_EXTI1]);
        exti.irq_exti2(s_irq_[f3::IRQ_EXTI2_TSC]);
        exti.irq_exti3(s_irq_[f3::IRQ_EXTI3]);
        exti.irq_exti4(s_irq_[f3::IRQ_EXTI4]);
        exti.irq_exti9_5(s_irq_[f3::IRQ_EXTI9_5]);
        exti.irq_exti15_10(s_irq_[f3::IRQ_EXTI15_10]);
        for (unsigned i = 0; i < s_exti_lin_.size(); ++i) {
            exti.linea_in[i](s_exti_in_[i]);
            exti.linea_irq[i](s_exti_lin_[i]);
        }
        exti.event_out(s_evt_in_);
        exti.wakeup(s_exti_wakeup_);
    }
    // Las líneas 16-35 del EXTI hacia el NVIC, con las que comparten vector
    // [CMSIS: comentarios de IRQn_Type]. Las directas no tienen salida.
    void irq_exti_proc() {
        auto l = [&](unsigned n) { return n >= 16 && n - 16 < s_exti_lin_.size() && s_exti_lin_[n - 16].read(); };
        s_irq_[f3::IRQ_PVD].write(l(16));
        s_irq_[f3::IRQ_RTC_ALARM].write(l(17));
        s_irq_[f3::IRQ_USB_WAKEUP].write(l(18));
        s_irq_[f3::IRQ_TAMP_STAMP].write(l(19));
        s_irq_[f3::IRQ_RTC_WKUP].write(l(20));
        s_irq_[f3::IRQ_COMP1_2_3].write(l(21) || l(22) || l(29));
        s_irq_[f3::IRQ_COMP4_5_6].write(l(30) || l(31) || l(32));
        s_irq_[f3::IRQ_COMP7].write(l(33));
    }

    // BOOT0 y nBOOT1 (bit 4 del byte USER, cargado en FLASH_OBR) deciden
    // MEM_MODE [RM0316 §3.5, tabla 6, y §12.1.1]. El silicio los muestrea en
    // el cuarto flanco de SYSCLK tras el reset; aqui, mientras dura:
    //   BOOT0 = 0                 la Flash
    //   BOOT0 = 1, nBOOT1 = 1     la memoria de sistema
    //   BOOT0 = 1, nBOOT1 = 0     la SRAM
    void boot_proc() {
        if (s_sysrst_n_.read()) return;
        uint8_t m = SyscfgF3::MEM_FLASH;
        if (s_boot0_.read()) m = flash.nboot1() ? SyscfgF3::MEM_SISTEMA : SyscfgF3::MEM_SRAM;
        s_boot_.write(m);
    }
    // La onda del MCO solo cuando PA8 la saca de verdad: es cara
    void mco_proc() { rcc.mco_onda(rcc.f_mco() > 0.0); }

    // --- El bus -------------------------------------------------------------------------
    // El alias de 0x0: la memoria que dice MEM_MODE, hasta su tamaño
    bool alias(uint64_t& a) const {
        if (a >= 0x08000000ull) return true;
        switch (syscfg.mem_mode()) {
            case SyscfgF3::MEM_SISTEMA:
                if (a >= SysMemF3::SIZE) return false;
                a += f3::SYSMEM_B; return true;
            case SyscfgF3::MEM_SRAM:
                if (a >= sram.size()) return false;
                a += f3::SRAM_B; return true;
            default:
                if (a >= flash.size()) return false;
                a += f3::FLASH_B; return true;
        }
    }
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a0 = gp.get_address();
        uint64_t a = a0;
        if (!alias(a)) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        gp.set_address(a);
        enruta(gp, t);
        gp.set_address(a0);
    }
    void enruta(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (flash.es_flash(uint32_t(a)) && a < 0x10000000ull) { a_flash_->b_transport(gp, t); return; }
        if (a >= f3::CCM_B && a < uint64_t(f3::CCM_B) + ccm.size()) { bt_ccm(gp, t); return; }
        if (a >= f3::SYSMEM_B && a < f3::SYSMEM_FIN) { a_sysmem_->b_transport(gp, t); return; }
        if (a >= f3::OB_B && a < f3::OB_B + f3::OB_SIZE) { a_ob_->b_transport(gp, t); return; }
        if (a >= f3::SRAM_B && a < uint64_t(f3::SRAM_B) + sram.size()) { a_sram_->b_transport(gp, t); return; }
        if (a >= f3::APB1_B && a < f3::APB1_FIN) { a_apb1_->b_transport(gp, t); return; }
        if (a >= f3::APB2_B && a < f3::APB2_FIN) { a_apb2_->b_transport(gp, t); return; }
        if (a >= f3::AHB1_B && a < f3::AHB1_FIN) { a_ahb1_->b_transport(gp, t); return; }
        if (a >= f3::AHB2_B && a < f3::AHB2_FIN) { a_ahb2_->b_transport(gp, t); return; }
        if (a >= f3::AHB3_B && a < f3::AHB3_FIN) { a_ahb3_->b_transport(gp, t); return; }
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint64_t a0 = gp.get_address();
        uint64_t a = a0;
        if (!alias(a)) return 0;
        gp.set_address(a);
        unsigned n = 0;
        if (flash.es_flash(uint32_t(a)) && a < 0x10000000ull) n = a_flash_->transport_dbg(gp);
        else if (a >= f3::CCM_B && a < uint64_t(f3::CCM_B) + ccm.size()) n = a_ccm_->transport_dbg(gp);
        else if (a >= f3::SYSMEM_B && a < f3::SYSMEM_FIN) n = a_sysmem_->transport_dbg(gp);
        else if (a >= f3::OB_B && a < f3::OB_B + f3::OB_SIZE) n = a_ob_->transport_dbg(gp);
        else if (a >= f3::SRAM_B && a < uint64_t(f3::SRAM_B) + sram.size()) n = a_sram_->transport_dbg(gp);
        else if (a >= f3::APB1_B && a < f3::APB1_FIN) n = a_apb1_->transport_dbg(gp);
        else if (a >= f3::APB2_B && a < f3::APB2_FIN) n = a_apb2_->transport_dbg(gp);
        else if (a >= f3::AHB1_B && a < f3::AHB1_FIN) n = a_ahb1_->transport_dbg(gp);
        else if (a >= f3::AHB2_B && a < f3::AHB2_FIN) n = a_ahb2_->transport_dbg(gp);
        else if (a >= f3::AHB3_B && a < f3::AHB3_FIN) n = a_ahb3_->transport_dbg(gp);
        gp.set_address(a0);
        return n;
    }
    // La CCM: lo que protege SYSCFG_RCR no se escribe [RM0316 §3.3.2 y §12.1.2],
    // y el intento es un error de bus [de modelo: RM0316 no dice que pasa]
    void bt_ccm(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint32_t off = uint32_t(gp.get_address() - f3::CCM_B);
        if (gp.is_write() && syscfg.ccm_protegida(off)) {
            ++n_ccm_prot_;
            if (!avisado_ccm_) {
                avisado_ccm_ = true;
                SC_REPORT_WARNING("stm32f303", "escritura en una pagina de la CCM protegida "
                                  "por SYSCFG_RCR: error de bus (la proteccion solo se quita "
                                  "con un reset)");
            }
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        a_ccm_->b_transport(gp, t);
    }
    unsigned dbg_ccm(tlm::tlm_generic_payload& gp) { return a_ccm_->transport_dbg(gp); }
};

} // namespace stm32
#endif // STM32_SOC_STM32F3_STM32F303_H
