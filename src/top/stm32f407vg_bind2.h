// =============================================================================
// stm32f407vg_bind2.h — Continuación del netlist del top (incluido desde
// stm32f407vg.h): periféricos, IRQs [IR, §9.1.2] y peticiones DMA [IR, §11.4].
// =============================================================================
#ifndef STM32_TOP_STM32F407VG_BIND2_H
#define STM32_TOP_STM32F407VG_BIND2_H

namespace stm32 {

// ===========================================================================
// Relojes/reset/gating y conexiones específicas de cada periférico
// ===========================================================================
inline void Stm32F407VG::bind_periph_common() {
    auto bind_tim = [&](TimerBase& t, sc_core::sc_signal<bool>& pclk,
                        sc_core::sc_signal<bool>& tclk, PeriphId id, int fz,
                        sc_core::sc_signal<bool>& trgo_sig) {
        bind_bus_slave(t, pclk, id);
        t.timclk(tclk);
        t.freeze(fz >= 0 ? s_freeze[unsigned(fz)] : s_false);
        for (unsigned i = 0; i < 4; ++i) t.itr[i](s_false);   // TODO(F4): cadena ITRx
        t.trgo(trgo_sig);
    };
    // s_trgo: 0=TIM1 1=TIM2 2=TIM3 3=TIM4 4=TIM5 5=TIM6 6=TIM7 7=TIM8
    bind_tim(tim1, s_pclk2, s_timclk2, P_TIM1, FZ_TIM1, s_trgo[0]);
    bind_tim(tim2, s_pclk1, s_timclk1, P_TIM2, FZ_TIM2, s_trgo[1]);
    bind_tim(tim3, s_pclk1, s_timclk1, P_TIM3, FZ_TIM3, s_trgo[2]);
    bind_tim(tim4, s_pclk1, s_timclk1, P_TIM4, FZ_TIM4, s_trgo[3]);
    bind_tim(tim5, s_pclk1, s_timclk1, P_TIM5, FZ_TIM5, s_trgo[4]);
    bind_tim(tim6, s_pclk1, s_timclk1, P_TIM6, FZ_TIM6, s_trgo[5]);
    bind_tim(tim7, s_pclk1, s_timclk1, P_TIM7, FZ_TIM7, s_trgo[6]);
    bind_tim(tim8, s_pclk2, s_timclk2, P_TIM8, FZ_TIM8, s_trgo[7]);
    bind_tim(tim9,  s_pclk2, s_timclk2, P_TIM9,  FZ_TIM9,  s_nc[nc()]);
    bind_tim(tim10, s_pclk2, s_timclk2, P_TIM10, FZ_TIM10, s_nc[nc()]);
    bind_tim(tim11, s_pclk2, s_timclk2, P_TIM11, FZ_TIM11, s_nc[nc()]);
    bind_tim(tim12, s_pclk1, s_timclk1, P_TIM12, FZ_TIM12, s_nc[nc()]);
    bind_tim(tim13, s_pclk1, s_timclk1, P_TIM13, FZ_TIM13, s_nc[nc()]);
    bind_tim(tim14, s_pclk1, s_timclk1, P_TIM14, FZ_TIM14, s_nc[nc()]);

    bind_bus_slave(usart1, s_pclk2, P_USART1);
    bind_bus_slave(usart2, s_pclk1, P_USART2);
    bind_bus_slave(usart3, s_pclk1, P_USART3);
    bind_bus_slave(uart4,  s_pclk1, P_UART4);
    bind_bus_slave(uart5,  s_pclk1, P_UART5);
    bind_bus_slave(usart6, s_pclk2, P_USART6);

    bind_bus_slave(spi1, s_pclk2, P_SPI1);
    bind_bus_slave(spi2, s_pclk1, P_SPI2);
    bind_bus_slave(spi3, s_pclk1, P_SPI3);
    spi1.i2s_ext_clk(s_false);
    spi2.i2s_ext_clk(rcc.s_i2s_clk);   // PLLI2S R [IR, §12.7]
    spi3.i2s_ext_clk(rcc.s_i2s_clk);

    bind_bus_slave(i2c1, s_pclk1, P_I2C1);
    bind_bus_slave(i2c2, s_pclk1, P_I2C2);
    bind_bus_slave(i2c3, s_pclk1, P_I2C3);

    bind_bus_slave(can1, s_pclk1, P_CAN1);
    bind_bus_slave(can2, s_pclk1, P_CAN2);
    can1.freeze(s_freeze[FZ_CAN1]);
    can2.freeze(s_freeze[FZ_CAN2]);
    can2.bind_filter_master(&can1);    // filtros compartidos [IR, §12.12]

    bind_bus_slave(adc, s_pclk2, P_ADC);
    adc.vdda(s_vdda);
    adc.vref(s_vdda);                  // TODO(F5): nodo VREF+ independiente
    adc.trig_regular[0](s_trgo[0]); adc.trig_regular[1](s_trgo[1]);
    adc.trig_regular[2](s_trgo[2]); adc.trig_regular[3](s_trgo[3]);
    adc.trig_regular[4](s_trgo[4]); adc.trig_regular[5](s_trgo[7]);
    adc.trig_regular[6](s_false);   adc.trig_regular[7](s_false); // EXTI11/SW TODO
    for (unsigned i = 0; i < 8; ++i) adc.trig_injected[i](s_false); // TODO(F5)

    bind_bus_slave(dac, s_pclk1, P_DAC);
    dac.vref(s_vdda);
    // TSEL: TIM6, TIM8, TIM7, TIM5, TIM2, TIM4, EXTI9, SW [IR, §12.14]
    dac.trig[0](s_trgo[5]); dac.trig[1](s_trgo[7]); dac.trig[2](s_trgo[6]);
    dac.trig[3](s_trgo[4]); dac.trig[4](s_trgo[1]); dac.trig[5](s_trgo[3]);
    dac.trig[6](s_false);   dac.trig[7](s_false);

    bind_bus_slave(wwdg, s_pclk1, P_WWDG);
    wwdg.freeze(s_freeze[FZ_WWDG]);
    wwdg.rst_req(s_wwdg_rr);
    // IWDG y RTC: sin bit ENR (siempre accesibles); RTC en dominio backup
    iwdg.clk(s_pclk1); iwdg.clk_hz(s_pclk1_hz); iwdg.rst_n(s_sysrst_n); iwdg.clk_en(s_true);
    iwdg.lsi_clk(s_lsiclk); iwdg.freeze(s_freeze[FZ_IWDG]);
    iwdg.hw_start(s_false);            // TODO(F2): option bit WDG_SW
    iwdg.rst_req(s_iwdg_rr);
    rtc.clk(s_pclk1); rtc.clk_hz(s_pclk1_hz); rtc.rst_n(s_sysrst_n); rtc.clk_en(s_true);
    rtc.rtcclk(s_rtcclk); rtc.rtcclk_hz(s_rtcclk_hz);
    rtc.bkp_rst_n(s_bkprst_n); rtc.dbp(s_dbp);
    rtc.exti17_alarm(s_rtc_l17); rtc.exti21_tamp_ts(s_rtc_l21);
    rtc.exti22_wakeup(s_rtc_l22);

    // EXTI y SYSCFG (EXTI sin gating propio)
    exti.clk(s_pclk2); exti.clk_hz(s_pclk2_hz); exti.rst_n(s_sysrst_n); exti.clk_en(s_true);
    exti.l16_pvd(s_pvd_line);
    exti.l17_rtc_alarm(s_rtc_l17);   exti.l18_otgfs_wkup(s_fswk_l18);
    exti.l19_eth_wkup(s_ethwk_l19);  exti.l20_otghs_wkup(s_hswk_l20);
    exti.l21_rtc_tamp(s_rtc_l21);    exti.l22_rtc_wkup(s_rtc_l22);
    exti.event_out(s_evt_in);        // evento hacia el núcleo (WFE)
    exti.wakeup(s_exti_wakeup);
    bind_bus_slave(syscfg, s_pclk2, P_SYSCFG);
    syscfg.mii_rmii_sel(s_mii);

    bind_bus_slave(pwr, s_pclk1, P_PWR);
    pwr.vdd_lvl(s_vdd);
    pwr.sleeping(s_sleeping); pwr.sleepdeep(s_sleepdeep);
    pwr.wkup_pin(pinmux.pad_din[0]);        // PA0-WKUP [IR, §2.1]
    pwr.exti_wakeup(s_exti_wakeup);
    pwr.irq_pvd(s_pvd_line);                // PVD -> línea EXTI16
    pwr.dbp(s_dbp);
    pwr.standby_req(s_nc[nc()]);            // TODO(F7) -> RCC
    pwr.stop_req(s_nc[nc()]);
    pwr.vos_rdy(s_nc[nc()]);

    bind_bus_slave(crc, s_hclk, P_CRC);
    bind_bus_slave(rng, s_hclk, P_RNG);
    rng.pll48ck(s_pll48); rng.hclk_hz(s_hclk_hz);
    bind_bus_slave(sdio, s_pclk2, P_SDIO);
    sdio.sdioclk(s_pll48);
    bind_bus_slave(dcmi, s_hclk, P_DCMI);
    bind_bus_slave(eth, s_hclk, P_ETHMAC);
    eth.mii_rmii_sel(s_mii);
    eth.wkup_line(s_ethwk_l19);
    bind_bus_slave(otg_fs, s_hclk, P_OTGFS);
    otg_fs.clk48(s_pll48);
    otg_fs.wkup_line(s_fswk_l18);
    otg_fs.irq_wkup(s_nc[nc()]);            // la IRQ 42 real llega vía EXTI18
    bind_bus_slave(otg_hs, s_hclk, P_OTGHS);
    otg_hs.clk48(s_pll48);
    otg_hs.wkup_line(s_hswk_l20);
    bind_bus_slave(dma1, s_hclk, P_DMA1);
    bind_bus_slave(dma2, s_hclk, P_DMA2);
}

// ===========================================================================
// Interrupciones -> NVIC (numeración exacta [IR, §9.1.2])
// ===========================================================================
inline void Stm32F407VG::bind_irqs() {
    wwdg.irq_ewi(s_irq[0]);
    // 1,2,3: PVD / TAMP_STAMP / RTC_WKUP a través de EXTI
    exti.irq_pvd(s_irq[1]);
    exti.irq_tamp(s_irq[2]);
    exti.irq_rtc_wkup(s_irq[3]);
    // 4 flash (en bind_core), 5 rcc (en bind_clocks_resets)
    exti.irq_exti0(s_irq[6]);   exti.irq_exti1(s_irq[7]);
    exti.irq_exti2(s_irq[8]);   exti.irq_exti3(s_irq[9]);
    exti.irq_exti4(s_irq[10]);
    for (unsigned s = 0; s < 7; ++s) dma1.irq_stream[s](s_irq[11 + s]);
    adc.irq(s_irq[18]);
    can1.irq_tx(s_irq[19]);  can1.irq_rx0(s_irq[20]);
    can1.irq_rx1(s_irq[21]); can1.irq_sce(s_irq[22]);
    exti.irq_exti9_5(s_irq[23]);
    // IRQs compartidas TIMx [IR, §9.1.2]
    tim1.irq_brk(s_or_in[0]);     tim9.irq_global(s_or_in[1]);
    or_irq24.a(s_or_in[0]);  or_irq24.b(s_or_in[1]);  or_irq24.y(s_irq[24]);
    tim1.irq_up(s_or_in[2]);      tim10.irq_global(s_or_in[3]);
    or_irq25.a(s_or_in[2]);  or_irq25.b(s_or_in[3]);  or_irq25.y(s_irq[25]);
    tim1.irq_trg_com(s_or_in[4]); tim11.irq_global(s_or_in[5]);
    or_irq26.a(s_or_in[4]);  or_irq26.b(s_or_in[5]);  or_irq26.y(s_irq[26]);
    tim1.irq_cc(s_irq[27]);
    tim2.irq_global(s_irq[28]);
    tim3.irq_global(s_irq[29]);
    tim4.irq_global(s_irq[30]);
    i2c1.irq_ev(s_irq[31]); i2c1.irq_er(s_irq[32]);
    i2c2.irq_ev(s_irq[33]); i2c2.irq_er(s_irq[34]);
    spi1.irq(s_irq[35]);    spi2.irq(s_irq[36]);
    usart1.irq(s_irq[37]);  usart2.irq(s_irq[38]);  usart3.irq(s_irq[39]);
    exti.irq_exti15_10(s_irq[40]);
    exti.irq_rtc_alarm(s_irq[41]);
    exti.irq_otgfs_wkup(s_irq[42]);
    tim8.irq_brk(s_or_in[6]);     tim12.irq_global(s_or_in[7]);
    or_irq43.a(s_or_in[6]);  or_irq43.b(s_or_in[7]);  or_irq43.y(s_irq[43]);
    tim8.irq_up(s_or_in[8]);      tim13.irq_global(s_or_in[9]);
    or_irq44.a(s_or_in[8]);  or_irq44.b(s_or_in[9]);  or_irq44.y(s_irq[44]);
    tim8.irq_trg_com(s_or_in[10]); tim14.irq_global(s_or_in[11]);
    or_irq45.a(s_or_in[10]); or_irq45.b(s_or_in[11]); or_irq45.y(s_irq[45]);
    tim8.irq_cc(s_irq[46]);
    dma1.irq_stream[7](s_irq[47]);
    fsmc.irq(s_irq[48]);
    sdio.irq(s_irq[49]);
    tim5.irq_global(s_irq[50]);
    spi3.irq(s_irq[51]);
    uart4.irq(s_irq[52]);  uart5.irq(s_irq[53]);
    tim6.irq_global(s_or_in[12]); dac.irq(s_or_in[13]);
    or_irq54.a(s_or_in[12]); or_irq54.b(s_or_in[13]); or_irq54.y(s_irq[54]);
    tim7.irq_global(s_irq[55]);
    for (unsigned s = 0; s < 5; ++s) dma2.irq_stream[s](s_irq[56 + s]);
    eth.irq(s_irq[61]);
    exti.irq_eth_wkup(s_irq[62]);
    can2.irq_tx(s_irq[63]);  can2.irq_rx0(s_irq[64]);
    can2.irq_rx1(s_irq[65]); can2.irq_sce(s_irq[66]);
    otg_fs.irq_global(s_irq[67]);
    for (unsigned s = 5; s < 8; ++s) dma2.irq_stream[s](s_irq[63 + s]); // 68..70
    usart6.irq(s_irq[71]);
    i2c3.irq_ev(s_irq[72]); i2c3.irq_er(s_irq[73]);
    otg_hs.irq_ep1_out(s_irq[74]); otg_hs.irq_ep1_in(s_irq[75]);
    exti.irq_otghs_wkup(s_irq[76]);
    otg_hs.irq_global(s_irq[77]);
    dcmi.irq(s_irq[78]);
    // 79 = CRYP: reservado en el F407 (sin driver)
    rng.irq(s_irq[80]);
    // 81 = FPU: TODO(F2) conectar core.fpu (señal interna del núcleo) a s_irq[81]
    // IRQs no usadas de TIM1/TIM8 (la global de los avanzados no existe)
    tim1.irq_global(s_nc[nc()]);
    tim8.irq_global(s_nc[nc()]);
}

// ===========================================================================
// Peticiones DMA: tablas corregidas [IR, §11.4.1/11.4.2]
// celda = stream*8 + canal; varias celdas pueden leer la misma señal q_*
// ===========================================================================
inline void Stm32F407VG::bind_dma_requests() {
    // ---- Salidas de petición de los periféricos -> señales q_* -------------
    usart1.dma_req_rx(q_u1_rx); usart1.dma_req_tx(q_u1_tx);
    usart2.dma_req_rx(q_u2_rx); usart2.dma_req_tx(q_u2_tx);
    usart3.dma_req_rx(q_u3_rx); usart3.dma_req_tx(q_u3_tx);
    uart4.dma_req_rx(q_u4_rx);  uart4.dma_req_tx(q_u4_tx);
    uart5.dma_req_rx(q_u5_rx);  uart5.dma_req_tx(q_u5_tx);
    usart6.dma_req_rx(q_u6_rx); usart6.dma_req_tx(q_u6_tx);
    spi1.dma_req_rx(q_spi1_rx); spi1.dma_req_tx(q_spi1_tx);
    spi2.dma_req_rx(q_spi2_rx); spi2.dma_req_tx(q_spi2_tx);
    spi3.dma_req_rx(q_spi3_rx); spi3.dma_req_tx(q_spi3_tx);
    i2c1.dma_req_rx(q_i2c1_rx); i2c1.dma_req_tx(q_i2c1_tx);
    i2c2.dma_req_rx(q_i2c2_rx); i2c2.dma_req_tx(q_i2c2_tx);
    i2c3.dma_req_rx(q_i2c3_rx); i2c3.dma_req_tx(q_i2c3_tx);
    adc.dma_req_adc1(q_adc1); adc.dma_req_adc2(q_adc2); adc.dma_req_adc3(q_adc3);
    dac.dma_req_ch1(q_dac1);  dac.dma_req_ch2(q_dac2);
    sdio.dma_req(q_sdio);     dcmi.dma_req(q_dcmi);
    tim6.dma_up(q_tim6_up);   tim7.dma_up(q_tim7_up);
    // q_tim_up / q_tim_cc: 0=TIM1 1=TIM2 2=TIM3 3=TIM4 4=TIM5 5=TIM8
    tim1.dma_up(q_tim_up[0]); tim1.dma_trig(q_tim1_trig);
    for (unsigned i = 0; i < 4; ++i) tim1.dma_cc[i](q_tim_cc[0 * 4 + i]);
    tim8.dma_up(q_tim_up[5]); tim8.dma_trig(q_tim8_trig);
    for (unsigned i = 0; i < 4; ++i) tim8.dma_cc[i](q_tim_cc[5 * 4 + i]);
    TimGeneral* tg[4] = {&tim2, &tim3, &tim4, &tim5};
    for (unsigned t = 0; t < 4; ++t) {
        tg[t]->dma_up(q_tim_up[1 + t]);
        for (unsigned i = 0; i < 4; ++i) tg[t]->dma_cc[i](q_tim_cc[(1 + t) * 4 + i]);
    }
    // TIM9-14: sin DMA en el F407
    tim9.dma_up(s_nc[nc()]);  for (unsigned i = 0; i < 2; ++i) tim9.dma_cc[i](s_nc[nc()]);
    tim10.dma_up(s_nc[nc()]); tim10.dma_cc[0](s_nc[nc()]);
    tim11.dma_up(s_nc[nc()]); tim11.dma_cc[0](s_nc[nc()]);
    tim12.dma_up(s_nc[nc()]); for (unsigned i = 0; i < 2; ++i) tim12.dma_cc[i](s_nc[nc()]);
    tim13.dma_up(s_nc[nc()]); tim13.dma_cc[0](s_nc[nc()]);
    tim14.dma_up(s_nc[nc()]); tim14.dma_cc[0](s_nc[nc()]);

    // ---- Multiplexado (stream, canal) -> señal [IR, §11.4] -----------------
    auto C = [](unsigned s, unsigned c) { return s * 8 + c; };
    std::map<unsigned, sc_core::sc_signal<bool>*> m1, m2;
    // DMA1 (celdas I2S*_EXT_* sin modelo propio -> quedan en s_false)
    m1[C(0,0)] = &q_spi3_rx;  m1[C(2,0)] = &q_spi3_rx;  m1[C(3,0)] = &q_spi2_rx;
    m1[C(4,0)] = &q_spi2_tx;  m1[C(5,0)] = &q_spi3_tx;  m1[C(7,0)] = &q_spi3_tx;
    m1[C(0,1)] = &q_i2c1_rx;  m1[C(5,1)] = &q_i2c1_rx;  m1[C(6,1)] = &q_i2c1_tx;
    m1[C(7,1)] = &q_i2c1_tx;  m1[C(2,1)] = &q_tim7_up;  m1[C(4,1)] = &q_tim7_up;
    m1[C(0,2)] = &q_tim_cc[3*4+0]; m1[C(3,2)] = &q_tim_cc[3*4+1];
    m1[C(6,2)] = &q_tim_up[3];     m1[C(7,2)] = &q_tim_cc[3*4+2];
    m1[C(1,3)] = &q_tim_up[1];     m1[C(2,3)] = &q_i2c3_rx;
    m1[C(4,3)] = &q_i2c3_tx;       m1[C(5,3)] = &q_tim_cc[1*4+0];
    m1[C(6,3)] = &q_tim_cc[1*4+1]; m1[C(7,3)] = &q_tim_up[1];
    m1[C(0,4)] = &q_u5_rx;  m1[C(1,4)] = &q_u3_rx;  m1[C(2,4)] = &q_u4_rx;
    m1[C(3,4)] = &q_u3_tx;  m1[C(4,4)] = &q_u4_tx;  m1[C(5,4)] = &q_u2_rx;
    m1[C(6,4)] = &q_u2_tx;  m1[C(7,4)] = &q_u5_tx;
    m1[C(2,5)] = &q_tim_up[2];     m1[C(4,5)] = &q_tim_cc[2*4+0];
    m1[C(5,5)] = &q_tim_cc[2*4+1]; m1[C(7,5)] = &q_tim_cc[2*4+2];
    m1[C(0,6)] = &q_tim_cc[4*4+2]; m1[C(1,6)] = &q_tim_cc[4*4+3];
    m1[C(2,6)] = &q_tim_cc[4*4+0]; m1[C(3,6)] = &q_tim_cc[4*4+3];
    m1[C(4,6)] = &q_tim_cc[4*4+1]; m1[C(6,6)] = &q_tim_up[4];
    m1[C(1,7)] = &q_tim6_up; m1[C(2,7)] = &q_i2c2_rx; m1[C(3,7)] = &q_i2c2_rx;
    m1[C(4,7)] = &q_u3_tx;   m1[C(5,7)] = &q_dac1;    m1[C(6,7)] = &q_dac2;
    m1[C(7,7)] = &q_i2c2_tx;
    // DMA2 (celdas SAI/SPI4-6/CRYP/HASH: no existen en el F407 -> s_false)
    m2[C(0,0)] = &q_adc1;    m2[C(4,0)] = &q_adc1;
    m2[C(2,0)] = &q_tim_cc[5*4+0];  m2[C(6,0)] = &q_tim_cc[0*4+0];
    m2[C(1,1)] = &q_dcmi;    m2[C(7,1)] = &q_dcmi;
    m2[C(2,1)] = &q_adc2;    m2[C(3,1)] = &q_adc2;
    m2[C(0,2)] = &q_adc3;    m2[C(1,2)] = &q_adc3;
    m2[C(0,3)] = &q_spi1_rx; m2[C(2,3)] = &q_spi1_rx;
    m2[C(3,3)] = &q_spi1_tx; m2[C(5,3)] = &q_spi1_tx;
    m2[C(2,4)] = &q_u1_rx;   m2[C(5,4)] = &q_u1_rx;   m2[C(7,4)] = &q_u1_tx;
    m2[C(3,4)] = &q_sdio;    m2[C(6,4)] = &q_sdio;
    m2[C(1,5)] = &q_u6_rx;   m2[C(2,5)] = &q_u6_rx;
    m2[C(6,5)] = &q_u6_tx;   m2[C(7,5)] = &q_u6_tx;
    m2[C(0,6)] = &q_tim1_trig;      m2[C(1,6)] = &q_tim_cc[0*4+0];
    m2[C(2,6)] = &q_tim_cc[0*4+1];  m2[C(3,6)] = &q_tim_cc[0*4+0];
    m2[C(4,6)] = &q_tim_cc[0*4+3];  m2[C(5,6)] = &q_tim_up[0];
    m2[C(6,6)] = &q_tim_cc[0*4+2];
    m2[C(1,7)] = &q_tim_up[5];      m2[C(2,7)] = &q_tim_cc[5*4+0];
    m2[C(3,7)] = &q_tim_cc[5*4+1];  m2[C(4,7)] = &q_tim_cc[5*4+2];
    m2[C(7,7)] = &q_tim_cc[5*4+3];

    for (unsigned i = 0; i < 64; ++i) {
        dma1.req_in[i](m1.count(i) ? *m1[i] : s_false);
        dma2.req_in[i](m2.count(i) ? *m2[i] : s_false);
        dma1.ack_out[i](s_dma1_ack[i]);
        dma2.ack_out[i](s_dma2_ack[i]);
        // TODO(F4): rutar ack de vuelta a los periféricos que lo usan (SDIO
        //           con control de flujo por periférico).
    }
}

// ===========================================================================
// Rutas analógicas y funciones alternativas de ejemplo
// ===========================================================================
inline void Stm32F407VG::bind_analog() {
    // Canales ADC externos [IR, §12.13-E]
    for (unsigned i = 0; i < 8; ++i) adc.bind_channel(i, pinmux.analog(0, i)); // PA0-7
    adc.bind_channel(8, pinmux.analog(1, 0));    // PB0
    adc.bind_channel(9, pinmux.analog(1, 1));    // PB1
    for (unsigned i = 0; i < 6; ++i) adc.bind_channel(10 + i, pinmux.analog(2, i)); // PC0-5
    // DAC [IR, §12.14]
    dac.bind_out(0, pinmux.analog(0, 4));        // PA4
    dac.bind_out(1, pinmux.analog(0, 5));        // PA5
    // USB FS PHY integrado (PA11=DM, PA12=DP)
    otg_fs.bind_phy(pinmux.analog(0, 11), pinmux.analog(0, 12));
    // Osciladores externos: HSE en PH0/PH1 (OSC_IN/OSC_OUT) y LSE en PC14/PC15
    // (OSC32_IN/OSC32_OUT) [IR, §2.1]. El oscilador solo arranca si hay algo
    // conectado eléctricamente al nodo de OSC_IN; en modo bypass mide además la
    // frecuencia del reloj inyectado a partir del pad [IR, §4.2].
    rcc.hse.xtal_in = &pinmux.analog(7, 0);      // PH0-OSC_IN
    rcc.hse.ext_in  = &pinmux.pad_din[7 * N_PORT_PINS + 0];
    rcc.lse.xtal_in = &pinmux.analog(2, 14);     // PC14-OSC32_IN
    rcc.lse.ext_in  = &pinmux.pad_din[2 * N_PORT_PINS + 14];

    // Funciones alternativas — EJEMPLOS de registro de periféricos de F4/F5; la
    // tabla del sistema (AF0: SWD/JTAG y MCO1/2; AF15: EVENTOUT) se registra en
    // bind_gpio_pins. El resto se completa al implementar cada periférico.
    pinmux.connect_af(0, 2, 7, AfEndpoint{&usart2.tx_out, &usart2.tx_oe, nullptr}); // PA2 USART2_TX
    pinmux.connect_af(0, 3, 7, AfEndpoint{nullptr, nullptr, &usart2.rx_in});        // PA3 USART2_RX
    pinmux.connect_af(0, 9, 7, AfEndpoint{&usart1.tx_out, &usart1.tx_oe, nullptr}); // PA9 USART1_TX
    pinmux.connect_af(0, 10, 7, AfEndpoint{nullptr, nullptr, &usart1.rx_in});       // PA10 USART1_RX
    pinmux.connect_af(1, 6, 4, AfEndpoint{&i2c1.scl_out, &i2c1.scl_oe, &i2c1.scl_in}); // PB6 I2C1_SCL
    pinmux.connect_af(1, 7, 4, AfEndpoint{&i2c1.sda_out, &i2c1.sda_oe, &i2c1.sda_in}); // PB7 I2C1_SDA
    pinmux.connect_af(0, 5, 5, AfEndpoint{&spi1.sck_out, &spi1.sck_oe, &spi1.sck_in}); // PA5 SPI1_SCK
    pinmux.connect_af(0, 6, 5, AfEndpoint{&spi1.miso_out, &spi1.miso_oe, &spi1.miso_in});
    pinmux.connect_af(0, 7, 5, AfEndpoint{&spi1.mosi_out, &spi1.mosi_oe, &spi1.mosi_in});
    // TODO(F4/F5): resto de la tabla AF (TIM CHx, CAN, SDIO, FSMC, ETH, ULPI,
    //           DCMI, RTC_AF1...) conforme se implemente cada periférico.
}

} // namespace stm32
#endif // STM32_TOP_STM32F407VG_BIND2_H
