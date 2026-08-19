// =============================================================================
// stm32f407vg.h — TOP: instancia y conexiona todo el MCU
//
// Este fichero es el CONTRATO DE INTEGRACIÓN del modelo: aquí se materializan
// las interconexiones del plan (doc/smt32f407vg_diseño.md §4): matriz 8x7,
// decodificadores AHB1/AHB2, puentes APB, relojes, resets, 82 IRQs, líneas
// EXTI, peticiones DMA (tablas [IR, §11.4]) y pines (pads + pin_mux).
// La frontera externa del MCU son los AnalogNet de pin_mux/power_pads.
// =============================================================================
#ifndef STM32_TOP_STM32F407VG_H
#define STM32_TOP_STM32F407VG_H

#include "../common/ahb_types.h"
#include "../common/clock_gen.h"
#include "../pins/pin_mux.h"
#include "../pins/power_pads.h"
#include "../bus/ahb_matrix.h"
#include "../bus/ahb_decoder.h"
#include "../mem/flash_if.h"
#include "../mem/sram.h"
#include "../rcc/rcc.h"
#include "../core/cortex_m4f.h"
#include "../periph/gpio_port.h"
#include "../periph/timers.h"
#include "../periph/usart.h"
#include "../periph/spi.h"
#include "../periph/i2c.h"
#include "../periph/can.h"
#include "../periph/adc.h"
#include "../periph/dac.h"
#include "../periph/watchdog.h"
#include "../periph/rtc.h"
#include "../periph/exti.h"
#include "../periph/syscfg.h"
#include "../periph/pwr.h"
#include "../periph/crc_rng.h"
#include "../periph/sdio.h"
#include "../periph/fsmc.h"
#include "../periph/dcmi.h"
#include "../periph/eth_mac.h"
#include "../periph/otg.h"
#include "../periph/dma.h"

namespace stm32 {

// Puerta OR de 2 entradas para IRQs compartidas (TIM1/TIM9..., TIM6/DAC)
SC_MODULE(Or2) {
    sc_core::sc_in<bool> a{"a"}, b{"b"};
    sc_core::sc_out<bool> y{"y"};
    SC_CTOR(Or2) { SC_METHOD(run); sensitive << a << b; }
    void run() { y.write(a.read() || b.read()); }
};

SC_MODULE(Stm32F407VG) {
    // =========================== Subcomponentes =============================
    PinMux    pinmux{"pinmux"};
    PowerPads pwr_pads{"pwr_pads"};
    Rcc       rcc{"rcc"};
    CortexM4F core{"core"};
    AhbMatrix matrix{"matrix"};

    FlashIf flash{"flash"};
    Sram    sram1{"sram1", addr::SRAM1_BASE, addr::SRAM1_SIZE};
    Sram    sram2{"sram2", addr::SRAM2_BASE, addr::SRAM2_SIZE};
    BkpSram bkpsram{"bkpsram"};
    Ccm     ccm{"ccm"};
    Sram    ext_ram_stub{"ext_ram_stub", addr::FSMC_MEM, 0x100000}; // TB la sustituye

    AhbDecoder   ahb1_dec{"ahb1_dec"}, ahb2_dec{"ahb2_dec"};
    AhbDecoder   apb1_dec{"apb1_dec"}, apb2_dec{"apb2_dec"};
    AhbApbBridge br_apb1{"br_apb1"}, br_apb2{"br_apb2"};

    sc_core::sc_vector<GpioPort> gpio;   // A..I

    DmaCtrl dma1{"dma1", addr::DMA1_B, false};
    DmaCtrl dma2{"dma2", addr::DMA2_B, true};
    EthMac  eth{"eth"};
    OtgHs   otg_hs{"otg_hs"};
    OtgFs   otg_fs{"otg_fs"};
    Dcmi    dcmi{"dcmi"};
    Rng     rng{"rng"};
    CrcUnit crc{"crc"};
    Fsmc    fsmc{"fsmc"};

    TimAdvanced tim1{"tim1", addr::TIM1_B}, tim8{"tim8", addr::TIM8_B};
    TimGeneral  tim2{"tim2", addr::TIM2_B, 4, true},  tim3{"tim3", addr::TIM3_B, 4, false};
    TimGeneral  tim4{"tim4", addr::TIM4_B, 4, false}, tim5{"tim5", addr::TIM5_B, 4, true};
    TimGeneral  tim9{"tim9", addr::TIM9_B, 2, false}, tim10{"tim10", addr::TIM10_B, 1, false};
    TimGeneral  tim11{"tim11", addr::TIM11_B, 1, false}, tim12{"tim12", addr::TIM12_B, 2, false};
    TimGeneral  tim13{"tim13", addr::TIM13_B, 1, false}, tim14{"tim14", addr::TIM14_B, 1, false};
    TimBasic    tim6{"tim6", addr::TIM6_B}, tim7{"tim7", addr::TIM7_B};

    Usart usart1{"usart1", addr::USART1_B}, usart2{"usart2", addr::USART2_B};
    Usart usart3{"usart3", addr::USART3_B};
    Usart uart4{"uart4", addr::UART4_B, false}, uart5{"uart5", addr::UART5_B, false};
    Usart usart6{"usart6", addr::USART6_B};
    Spi   spi1{"spi1", addr::SPI1_B, false}, spi2{"spi2", addr::SPI2_B, true};
    Spi   spi3{"spi3", addr::SPI3_B, true};
    I2c   i2c1{"i2c1", addr::I2C1_B}, i2c2{"i2c2", addr::I2C2_B}, i2c3{"i2c3", addr::I2C3_B};
    BxCan can1{"can1", addr::CAN1_B, true}, can2{"can2", addr::CAN2_B, false};

    AdcBlock adc{"adc"};
    Dac      dac{"dac"};
    Wwdg     wwdg{"wwdg"};
    Iwdg     iwdg{"iwdg"};
    Rtc      rtc{"rtc"};
    Exti     exti{"exti"};
    Syscfg   syscfg{"syscfg"};
    Pwr      pwr{"pwr"};
    Sdio     sdio{"sdio"};

    // ============================== Señales =================================
    // Relojes
    sc_core::sc_signal<bool>   s_hclk{"s_hclk"}, s_pclk1{"s_pclk1"}, s_pclk2{"s_pclk2"};
    sc_core::sc_signal<bool>   s_timclk1{"s_timclk1"}, s_timclk2{"s_timclk2"};
    sc_core::sc_signal<bool>   s_pll48{"s_pll48"}, s_rtcclk{"s_rtcclk"};
    sc_core::sc_signal<bool>   s_lsiclk{"s_lsiclk"}, s_stk_ext{"s_stk_ext"};
    sc_core::sc_signal<double> s_hclk_hz{"s_hclk_hz"}, s_pclk1_hz{"s_pclk1_hz"};
    sc_core::sc_signal<double> s_pclk2_hz{"s_pclk2_hz"}, s_timclk1_hz{"s_timclk1_hz"};
    sc_core::sc_signal<double> s_timclk2_hz{"s_timclk2_hz"}, s_pll48_hz{"s_pll48_hz"};
    sc_core::sc_signal<double> s_rtcclk_hz{"s_rtcclk_hz"};
    // Resets y gating
    sc_core::sc_signal<bool> s_sysrst_n{"s_sysrst_n"}, s_bkprst_n{"s_bkprst_n"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_prst{"s_prst", P_COUNT};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_pcen{"s_pcen", P_COUNT};
    // IRQ / NMI / eventos
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_irq{"s_irq", N_IRQ};
    sc_core::sc_signal<bool> s_nmi{"s_nmi"}, s_evt_in{"s_evt_in"}, s_evt_out{"s_evt_out"};
    // Núcleo / sistema
    sc_core::sc_signal<bool> s_sysresetreq{"s_sysresetreq"};
    sc_core::sc_signal<bool> s_sleeping{"s_sleeping"}, s_sleepdeep{"s_sleepdeep"};
    sc_core::sc_signal<uint8_t> s_boot{"s_boot"}, s_memmode{"s_memmode"};
    // PowerPads / PWR
    sc_core::sc_signal<bool>   s_por_ok{"s_por_ok"}, s_nrst_n{"s_nrst_n"};
    sc_core::sc_signal<bool>   s_boot0{"s_boot0"}, s_nrst_drv{"s_nrst_drv"};
    sc_core::sc_signal<double> s_vdd{"s_vdd"}, s_vdda{"s_vdda"};
    sc_core::sc_signal<bool>   s_dbp{"s_dbp"}, s_pvd_line{"s_pvd_line"};
    sc_core::sc_signal<bool>   s_wwdg_rr{"s_wwdg_rr"}, s_iwdg_rr{"s_iwdg_rr"};
    // EXTI
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_exti_gpio{"s_exti_gpio",
                                                             N_GPIO_PORTS * N_PORT_PINS};
    sc_core::sc_vector<sc_core::sc_signal<uint8_t>> s_exticr{"s_exticr", 16};
    sc_core::sc_signal<bool> s_rtc_l17{"s_rtc_l17"}, s_rtc_l21{"s_rtc_l21"},
                             s_rtc_l22{"s_rtc_l22"}, s_fswk_l18{"s_fswk_l18"},
                             s_ethwk_l19{"s_ethwk_l19"}, s_hswk_l20{"s_hswk_l20"};
    sc_core::sc_signal<bool> s_exti_wakeup{"s_exti_wakeup"};
    // Debug / freeze
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_freeze{"s_freeze", FZ_COUNT};
    sc_core::sc_signal<bool> s_swdio_o{"s_swdio_o"}, s_swdio_oe{"s_swdio_oe"},
                             s_jtdo{"s_jtdo"};
    // DMA req/ack
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_dma1_ack{"s_dma1_ack", 64};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_dma2_ack{"s_dma2_ack", 64};
    // Peticiones con nombre (una señal por fuente; varias celdas pueden leerla)
    sc_core::sc_signal<bool>
        q_spi2_rx{"q_spi2_rx"}, q_spi2_tx{"q_spi2_tx"},
        q_spi3_rx{"q_spi3_rx"}, q_spi3_tx{"q_spi3_tx"},
        q_spi1_rx{"q_spi1_rx"}, q_spi1_tx{"q_spi1_tx"},
        q_i2c1_rx{"q_i2c1_rx"}, q_i2c1_tx{"q_i2c1_tx"},
        q_i2c2_rx{"q_i2c2_rx"}, q_i2c2_tx{"q_i2c2_tx"},
        q_i2c3_rx{"q_i2c3_rx"}, q_i2c3_tx{"q_i2c3_tx"},
        q_u1_rx{"q_u1_rx"}, q_u1_tx{"q_u1_tx"}, q_u2_rx{"q_u2_rx"}, q_u2_tx{"q_u2_tx"},
        q_u3_rx{"q_u3_rx"}, q_u3_tx{"q_u3_tx"}, q_u4_rx{"q_u4_rx"}, q_u4_tx{"q_u4_tx"},
        q_u5_rx{"q_u5_rx"}, q_u5_tx{"q_u5_tx"}, q_u6_rx{"q_u6_rx"}, q_u6_tx{"q_u6_tx"},
        q_adc1{"q_adc1"}, q_adc2{"q_adc2"}, q_adc3{"q_adc3"},
        q_dac1{"q_dac1"}, q_dac2{"q_dac2"}, q_sdio{"q_sdio"}, q_dcmi{"q_dcmi"},
        q_tim6_up{"q_tim6_up"}, q_tim7_up{"q_tim7_up"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> q_tim_cc{"q_tim_cc", 6 * 4}; // tim1/2/3/4/5/8 x ch
    sc_core::sc_signal<bool> q_tim_up[6];   // tim1/2/3/4/5/8 UP
    sc_core::sc_signal<bool> q_tim1_trig{"q_tim1_trig"}, q_tim8_trig{"q_tim8_trig"};
    // Triggers / trgo
    sc_core::sc_signal<bool> s_trgo[8];     // tim1,2,3,4,5,6,7,8
    sc_core::sc_signal<bool> s_mii{"s_mii"};
    // Constantes y sumidero de no-conectados
    sc_core::sc_signal<bool> s_false{"s_false"}, s_true{"s_true"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_nc{"s_nc", 64};
    unsigned nc_i_ = 0;
    // OR de IRQs compartidas
    Or2 or_irq24{"or_irq24"}, or_irq25{"or_irq25"}, or_irq26{"or_irq26"};
    Or2 or_irq43{"or_irq43"}, or_irq44{"or_irq44"}, or_irq45{"or_irq45"};
    Or2 or_irq54{"or_irq54"};
    sc_core::sc_signal<bool> s_or_in[14];

    // ============================ Construcción ==============================
    SC_CTOR(Stm32F407VG) : gpio("gpio", N_GPIO_PORTS, [](const char* nm, size_t i) {
                                   return new GpioPort(nm, unsigned(i)); }) {
        bind_clocks_resets();
        bind_bus();
        bind_core();
        bind_gpio_pins();
        bind_periph_common();
        bind_irqs();
        bind_dma_requests();
        bind_analog();
        SC_THREAD(init_proc);
    }

    // Muestreo de los pines de arranque: BOOT0 (pin dedicado) y BOOT1 (PB2) se
    // capturan en el 4º flanco ascendente de SYSCLK tras la salida de reset y
    // conservan su valor hasta el siguiente reset [IR, §2.3].
    void init_proc() {
        s_true.write(true); s_false.write(false);
        s_boot.write(0);
        for (;;) {
            // Espera a que el reset de sistema esté activo y luego se libere
            while (s_sysrst_n.read()) wait(s_sysrst_n.value_changed_event());
            while (!s_sysrst_n.read()) wait(s_sysrst_n.value_changed_event());
            for (unsigned i = 0; i < 4; ++i) wait(s_hclk.posedge_event());
            const uint8_t b0 = s_boot0.read() ? 1u : 0u;
            const uint8_t b1 = pinmux.pad_din[1 * N_PORT_PINS + 2].read() ? 2u : 0u;
            s_boot.write(uint8_t(b1 | b0));
        }
    }

private:
    unsigned nc() { return nc_i_++; }   // siguiente señal de no-conectado

    void bind_clocks_resets();
    void bind_bus();
    void bind_core();
    void bind_gpio_pins();
    void bind_periph_common();
    void bind_irqs();
    void bind_dma_requests();
    void bind_analog();

    // Ayudas de binding uniforme
    void bind_bus_slave(BusSlave& p, sc_core::sc_signal<bool>& clk, PeriphId id) {
        p.clk(clk); p.rst_n(s_prst[id]); p.clk_en(s_pcen[id]);
    }
};

// ===========================================================================
// Relojes, resets y RCC
// ===========================================================================
inline void Stm32F407VG::bind_clocks_resets() {
    rcc.hclk(s_hclk);       rcc.hclk_hz(s_hclk_hz);
    rcc.pclk1(s_pclk1);     rcc.pclk1_hz(s_pclk1_hz);
    rcc.pclk2(s_pclk2);     rcc.pclk2_hz(s_pclk2_hz);
    rcc.timclk1(s_timclk1); rcc.timclk1_hz(s_timclk1_hz);
    rcc.timclk2(s_timclk2); rcc.timclk2_hz(s_timclk2_hz);
    rcc.pll48ck(s_pll48);   rcc.pll48ck_hz(s_pll48_hz);
    rcc.rtcclk(s_rtcclk);   rcc.rtcclk_hz(s_rtcclk_hz);
    rcc.lsi_clk(s_lsiclk);  rcc.systick_ext(s_stk_ext);
    for (unsigned i = 0; i < P_COUNT; ++i) {
        rcc.periph_clk_en[i](s_pcen[i]);
        rcc.periph_rst_n[i](s_prst[i]);
    }
    rcc.sys_rst_n(s_sysrst_n);
    rcc.bkp_rst_n(s_bkprst_n);
    rcc.drive_nrst_low(s_nrst_drv);
    rcc.por_ok(s_por_ok);
    rcc.nrst_in_n(s_nrst_n);
    rcc.wwdg_rst_req(s_wwdg_rr);
    rcc.iwdg_rst_req(s_iwdg_rr);
    rcc.sysresetreq(s_sysresetreq);
    rcc.nmi_css(s_nmi);
    rcc.irq(s_irq[5]);
    // RCC como esclavo de su propio dominio AHB1 (gating siempre activo)
    rcc.clk(s_hclk); rcc.rst_n(s_sysrst_n); rcc.clk_en(s_true);

    pwr_pads.por_ok(s_por_ok);
    pwr_pads.nrst_in_n(s_nrst_n);
    pwr_pads.boot0_lvl(s_boot0);
    pwr_pads.vdd_lvl(s_vdd);
    pwr_pads.vdda_lvl(s_vdda);
    pwr_pads.drive_nrst_low(s_nrst_drv);
    // Cristales externos hacia HSE/LSE por ruta analógica [plan P4]
    rcc.hse.xtal_in = &pinmux.analog(7, 0);    // PH0
    rcc.lse.xtal_in = &pinmux.analog(2, 14);   // PC14
}

// ===========================================================================
// Matriz, decodificadores y puentes
// ===========================================================================
inline void Stm32F407VG::bind_bus() {
    matrix.hclk(s_hclk); matrix.hclk_hz(s_hclk_hz); matrix.rst_n(s_sysrst_n);
    // Maestros [IR, §6.1.1]
    core.icode.bind(matrix.from_master[unsigned(BusMaster::CORE_IBUS)]);
    core.dcode.bind(matrix.from_master[unsigned(BusMaster::CORE_DBUS)]);
    core.sbus.bind(matrix.from_master[unsigned(BusMaster::CORE_SBUS)]);
    dma1.mem_m.bind(matrix.from_master[unsigned(BusMaster::DMA1_MEM)]);
    dma2.mem_m.bind(matrix.from_master[unsigned(BusMaster::DMA2_MEM)]);
    dma2.periph_m.bind(matrix.from_master[unsigned(BusMaster::DMA2_PERIPH)]);
    eth.dma_m.bind(matrix.from_master[unsigned(BusMaster::ETH_DMA)]);
    otg_hs.dma_m.bind(matrix.from_master[unsigned(BusMaster::OTG_HS_DMA)]);
    // DMA1 puerto de periféricos: directo a APB1 (sin matriz) [IR, §11.1.1]
    dma1.periph_m.bind(apb1_dec.tsk2);
    // Esclavos [IR, §6.1.2]
    matrix.to_slave[unsigned(BusSlaveId::FLASH_ICODE)].bind(flash.icode);
    matrix.to_slave[unsigned(BusSlaveId::FLASH_DCODE)].bind(flash.dcode);
    matrix.to_slave[unsigned(BusSlaveId::SRAM1)].bind(sram1.tsk);
    matrix.to_slave[unsigned(BusSlaveId::SRAM2)].bind(sram2.tsk);
    matrix.to_slave[unsigned(BusSlaveId::AHB1_SEG)].bind(ahb1_dec.tsk);
    matrix.to_slave[unsigned(BusSlaveId::AHB2_SEG)].bind(ahb2_dec.tsk);
    matrix.to_slave[unsigned(BusSlaveId::FSMC_EXT)].bind(fsmc.mem);

    // Segmento AHB1 [IR, §6.5]
    for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
        ahb1_dec.add_slave(("to_gpio" + std::string(1, 'a' + p)).c_str(),
                           addr::GPIOA_B + 0x400 * p, 0x400)->bind(gpio[p].tsk);
    ahb1_dec.add_slave("to_crc", addr::CRC_B, 0x400)->bind(crc.tsk);
    ahb1_dec.add_slave("to_rcc", addr::RCC_B, 0x400)->bind(rcc.tsk);
    ahb1_dec.add_slave("to_flashif", addr::FLASHIF_B, 0x400)->bind(flash.regs);
    ahb1_dec.add_slave("to_bkpsram", addr::BKPSRAM_BASE, addr::BKPSRAM_SIZE)
        ->bind(bkpsram.tsk);
    ahb1_dec.add_slave("to_dma1", addr::DMA1_B, 0x400)->bind(dma1.tsk);
    ahb1_dec.add_slave("to_dma2", addr::DMA2_B, 0x400)->bind(dma2.tsk);
    ahb1_dec.add_slave("to_eth", addr::ETH_B, 0x1400)->bind(eth.tsk);
    ahb1_dec.add_slave("to_otghs", addr::OTG_HS_B, 0x40000)->bind(otg_hs.tsk);
    ahb1_dec.add_slave("to_apb1", 0x40000000, 0x8000)->bind(br_apb1.ahb);
    ahb1_dec.add_slave("to_apb2", 0x40010000, 0x5800)->bind(br_apb2.ahb);
    // Segmento AHB2
    ahb2_dec.add_slave("to_otgfs", addr::OTG_FS_B, 0x40000)->bind(otg_fs.tsk);
    ahb2_dec.add_slave("to_dcmi", addr::DCMI_B, 0x400)->bind(dcmi.tsk);
    ahb2_dec.add_slave("to_rng", addr::RNG_B, 0x400)->bind(rng.tsk);
    // Puentes y decodificadores APB
    br_apb1.pclk(s_pclk1); br_apb1.pclk_hz(s_pclk1_hz); br_apb1.apb.bind(apb1_dec.tsk);
    br_apb2.pclk(s_pclk2); br_apb2.pclk_hz(s_pclk2_hz); br_apb2.apb.bind(apb2_dec.tsk);
    // APB1 [IR, §6.5]
    apb1_dec.add_slave("to_tim2", addr::TIM2_B, 0x400)->bind(tim2.tsk);
    apb1_dec.add_slave("to_tim3", addr::TIM3_B, 0x400)->bind(tim3.tsk);
    apb1_dec.add_slave("to_tim4", addr::TIM4_B, 0x400)->bind(tim4.tsk);
    apb1_dec.add_slave("to_tim5", addr::TIM5_B, 0x400)->bind(tim5.tsk);
    apb1_dec.add_slave("to_tim6", addr::TIM6_B, 0x400)->bind(tim6.tsk);
    apb1_dec.add_slave("to_tim7", addr::TIM7_B, 0x400)->bind(tim7.tsk);
    apb1_dec.add_slave("to_tim12", addr::TIM12_B, 0x400)->bind(tim12.tsk);
    apb1_dec.add_slave("to_tim13", addr::TIM13_B, 0x400)->bind(tim13.tsk);
    apb1_dec.add_slave("to_tim14", addr::TIM14_B, 0x400)->bind(tim14.tsk);
    apb1_dec.add_slave("to_rtc", addr::RTC_B, 0x400)->bind(rtc.tsk);
    apb1_dec.add_slave("to_wwdg", addr::WWDG_B, 0x400)->bind(wwdg.tsk);
    apb1_dec.add_slave("to_iwdg", addr::IWDG_B, 0x400)->bind(iwdg.tsk);
    apb1_dec.add_slave("to_spi2", addr::SPI2_B, 0x400)->bind(spi2.tsk);
    apb1_dec.add_slave("to_spi3", addr::SPI3_B, 0x400)->bind(spi3.tsk);
    apb1_dec.add_slave("to_usart2", addr::USART2_B, 0x400)->bind(usart2.tsk);
    apb1_dec.add_slave("to_usart3", addr::USART3_B, 0x400)->bind(usart3.tsk);
    apb1_dec.add_slave("to_uart4", addr::UART4_B, 0x400)->bind(uart4.tsk);
    apb1_dec.add_slave("to_uart5", addr::UART5_B, 0x400)->bind(uart5.tsk);
    apb1_dec.add_slave("to_i2c1", addr::I2C1_B, 0x400)->bind(i2c1.tsk);
    apb1_dec.add_slave("to_i2c2", addr::I2C2_B, 0x400)->bind(i2c2.tsk);
    apb1_dec.add_slave("to_i2c3", addr::I2C3_B, 0x400)->bind(i2c3.tsk);
    apb1_dec.add_slave("to_can1", addr::CAN1_B, 0x400)->bind(can1.tsk);
    apb1_dec.add_slave("to_can2", addr::CAN2_B, 0x400)->bind(can2.tsk);
    apb1_dec.add_slave("to_pwr", addr::PWR_B, 0x400)->bind(pwr.tsk);
    apb1_dec.add_slave("to_dac", addr::DAC_B, 0x400)->bind(dac.tsk);
    // APB2
    apb2_dec.add_slave("to_tim1", addr::TIM1_B, 0x400)->bind(tim1.tsk);
    apb2_dec.add_slave("to_tim8", addr::TIM8_B, 0x400)->bind(tim8.tsk);
    apb2_dec.add_slave("to_usart1", addr::USART1_B, 0x400)->bind(usart1.tsk);
    apb2_dec.add_slave("to_usart6", addr::USART6_B, 0x400)->bind(usart6.tsk);
    apb2_dec.add_slave("to_adc", addr::ADC_B, 0x400)->bind(adc.tsk);
    apb2_dec.add_slave("to_sdio", addr::SDIO_B, 0x400)->bind(sdio.tsk);
    apb2_dec.add_slave("to_spi1", addr::SPI1_B, 0x400)->bind(spi1.tsk);
    apb2_dec.add_slave("to_syscfg", addr::SYSCFG_B, 0x400)->bind(syscfg.tsk);
    apb2_dec.add_slave("to_exti", addr::EXTI_B, 0x400)->bind(exti.tsk);
    apb2_dec.add_slave("to_tim9", addr::TIM9_B, 0x400)->bind(tim9.tsk);
    apb2_dec.add_slave("to_tim10", addr::TIM10_B, 0x400)->bind(tim10.tsk);
    apb2_dec.add_slave("to_tim11", addr::TIM11_B, 0x400)->bind(tim11.tsk);
    // FSMC (los registros 0xA000_0000 se decodifican dentro de fsmc.mem)
    fsmc.ext_mem.bind(ext_ram_stub.tsk);
    fsmc.hclk(s_hclk); fsmc.rst_n(s_prst[P_FSMC]); fsmc.clk_en(s_pcen[P_FSMC]);
}

// ===========================================================================
// Núcleo
// ===========================================================================
inline void Stm32F407VG::bind_core() {
    core.fclk(s_hclk); core.fclk_hz(s_hclk_hz);
    core.systick_ext(s_stk_ext); core.rst_n(s_sysrst_n);
    for (unsigned i = 0; i < N_IRQ; ++i) core.irq_in[i](s_irq[i]);
    core.nmi_in(s_nmi);
    core.sysresetreq(s_sysresetreq);
    core.sleeping(s_sleeping); core.sleepdeep(s_sleepdeep);
    core.event_in(s_evt_in);   core.event_out(s_evt_out);
    core.boot_mode(s_memmode);
    core.ccm.bind(ccm.tsk);
    // Debug: pines AF0 (PA13/14/15, PB3/PB4) [IR, §13.1]
    core.debug.swclk_tck(pinmux.pad_din[0 * 16 + 14]);   // PA14
    core.debug.swdio_in(pinmux.pad_din[0 * 16 + 13]);    // PA13
    core.debug.swdio_out(s_swdio_o);
    core.debug.swdio_oe(s_swdio_oe);
    core.debug.jtdi(pinmux.pad_din[0 * 16 + 15]);        // PA15
    core.debug.jtdo_swo(s_jtdo);
    core.debug.njtrst(pinmux.pad_din[1 * 16 + 4]);       // PB4
    for (unsigned i = 0; i < FZ_COUNT; ++i) core.debug.freeze[i](s_freeze[i]);
    // TODO(F3): registrar en pin_mux los endpoints AF0 de swdio_o/oe y jtdo.
    // (debug.ahb_ap queda enlazado al router dentro de CortexM4F)

    ccm.hclk(s_hclk); ccm.rst_n(s_sysrst_n);
    sram1.hclk(s_hclk); sram1.rst_n(s_sysrst_n);
    sram2.hclk(s_hclk); sram2.rst_n(s_sysrst_n);
    bkpsram.hclk(s_hclk); bkpsram.rst_n(s_bkprst_n);
    ext_ram_stub.hclk(s_hclk); ext_ram_stub.rst_n(s_sysrst_n);
    flash.hclk(s_hclk); flash.hclk_hz(s_hclk_hz); flash.rst_n(s_sysrst_n);
    flash.irq(s_irq[4]);
}

// ===========================================================================
// GPIO <-> pads y líneas EXTI
// ===========================================================================
inline void Stm32F407VG::bind_gpio_pins() {
    for (unsigned p = 0; p < N_GPIO_PORTS; ++p) {
        bind_bus_slave(gpio[p], s_hclk, PeriphId(P_GPIOA + p));
        for (unsigned i = 0; i < N_PORT_PINS; ++i) {
            const unsigned k = p * N_PORT_PINS + i;
            gpio[p].pad_drive[i](pinmux.gpio_drive[k]);
            gpio[p].pad_din[i](pinmux.pad_din[k]);
            gpio[p].exti_line[i](s_exti_gpio[k]);
            exti.gpio_line[k](s_exti_gpio[k]);
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        syscfg.exticr_sel[i](s_exticr[i]);
        exti.exticr_sel[i](s_exticr[i]);
    }
    syscfg.boot_pins(s_boot);
    syscfg.mem_mode(s_memmode);
}

} // namespace stm32

#include "stm32f407vg_bind2.h"   // continuación del netlist (periph/irq/dma)

#endif // STM32_TOP_STM32F407VG_H
