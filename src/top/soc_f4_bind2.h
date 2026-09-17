// =============================================================================
// soc_f4_bind2.h — Continuación del netlist del top (incluido desde
// soc_f4.h): periféricos, IRQs [IR, §9.1.2] y peticiones DMA [IR, §11.4].
// =============================================================================
#ifndef STM32_TOP_SOC_F4_BIND2_H
#define STM32_TOP_SOC_F4_BIND2_H

namespace stm32 {

// ---------------------------------------------------------------------------
// Matriz de triggers internos entre temporizadores (qué TRGO llega a cada ITRx).
// No consta en [IR]: es la tabla del F407 y se cablea aquí, en el netlist, de
// modo que el modelo del temporizador sigue siendo genérico —para él, ITR0..3
// son cuatro entradas cualesquiera—. Índices de s_trgo:
//   0=TIM1 1=TIM2 2=TIM3 3=TIM4 4=TIM5 5=TIM6 6=TIM7 7=TIM8 ; -1 = sin conectar
// ---------------------------------------------------------------------------
static const int ITR_NONE[4] = {-1, -1, -1, -1};
static const int ITR_T1[4]   = { 4,  1,  2,  3};   // TIM5, TIM2, TIM3, TIM4
static const int ITR_T2[4]   = { 0,  7,  2,  3};   // TIM1, TIM8, TIM3, TIM4
static const int ITR_T3[4]   = { 0,  1,  4,  3};   // TIM1, TIM2, TIM5, TIM4
static const int ITR_T4[4]   = { 0,  1,  2,  7};   // TIM1, TIM2, TIM3, TIM8
static const int ITR_T5[4]   = { 1,  2,  3,  7};   // TIM2, TIM3, TIM4, TIM8
static const int ITR_T8[4]   = { 0,  1,  3,  4};   // TIM1, TIM2, TIM4, TIM5
static const int ITR_T9[4]   = { 1,  2, -1, -1};   // TIM2, TIM3, (TIM10/11 OC)
static const int ITR_T12[4]  = { 3,  4, -1, -1};   // TIM4, TIM5, (TIM13/14 OC)

// ===========================================================================
// Relojes/reset/gating y conexiones específicas de cada periférico
// ===========================================================================
inline void SocF4::bind_periph_common() {
    // -----------------------------------------------------------------------
    // Temporizadores. Cada uno recibe su PCLK (para el bus), su TIMCLK y la
    // frecuencia de éste —el contador cuenta en TIMCLK, que es PCLKx o 2*PCLKx
    // según el prescaler del APB [IR, §4.4, §12.1.2]—, su bit de congelación
    // del depurador, su salida TRGO y las cuatro entradas de trigger interno.
    //
    // s_trgo: 0=TIM1 1=TIM2 2=TIM3 3=TIM4 4=TIM5 5=TIM6 6=TIM7 7=TIM8; la
    // matriz ITRx está declarada arriba, al principio de este fichero.
    auto bind_tim = [&](TimerBase& t, sc_core::sc_signal<bool>& pclk,
                        sc_core::sc_signal<bool>& tclk,
                        sc_core::sc_signal<double>& tclk_hz, PeriphId id, int fz,
                        sc_core::sc_signal<bool>& trgo_sig,
                        const int itrs[4] = ITR_NONE) {
        bind_bus_slave(t, pclk, id);
        t.timclk(tclk);
        t.timclk_hz(tclk_hz);
        t.freeze(fz >= 0 ? s_freeze[unsigned(fz)] : s_false);
        for (unsigned i = 0; i < 4; ++i)
            t.itr[i](itrs[i] >= 0 ? s_trgo[unsigned(itrs[i])] : s_false);
        t.trgo(trgo_sig);
    };
    bind_tim(tim1, s_pclk2, s_timclk2, s_timclk2_hz, P_TIM1, FZ_TIM1, s_trgo[0], ITR_T1);
    bind_tim(tim2, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM2, FZ_TIM2, s_trgo[1], ITR_T2);
    bind_tim(tim3, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM3, FZ_TIM3, s_trgo[2], ITR_T3);
    bind_tim(tim4, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM4, FZ_TIM4, s_trgo[3], ITR_T4);
    bind_tim(tim5, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM5, FZ_TIM5, s_trgo[4], ITR_T5);
    bind_tim(tim6, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM6, FZ_TIM6, s_trgo[5]);
    bind_tim(tim7, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM7, FZ_TIM7, s_trgo[6]);
    bind_tim(tim8, s_pclk2, s_timclk2, s_timclk2_hz, P_TIM8, FZ_TIM8, s_trgo[7], ITR_T8);
    bind_tim(tim9,  s_pclk2, s_timclk2, s_timclk2_hz, P_TIM9,  FZ_TIM9,  s_nc[nc()], ITR_T9);
    bind_tim(tim10, s_pclk2, s_timclk2, s_timclk2_hz, P_TIM10, FZ_TIM10, s_nc[nc()]);
    bind_tim(tim11, s_pclk2, s_timclk2, s_timclk2_hz, P_TIM11, FZ_TIM11, s_nc[nc()]);
    bind_tim(tim12, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM12, FZ_TIM12, s_nc[nc()], ITR_T12);
    bind_tim(tim13, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM13, FZ_TIM13, s_nc[nc()]);
    bind_tim(tim14, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM14, FZ_TIM14, s_nc[nc()]);

    bind_bus_slave(usart1, s_pclk2, P_USART1);
    bind_bus_slave(usart2, s_pclk1, P_USART2);
    bind_bus_slave(usart3, s_pclk1, P_USART3);
    bind_bus_slave(uart4,  s_pclk1, P_UART4);
    bind_bus_slave(uart5,  s_pclk1, P_UART5);
    bind_bus_slave(usart6, s_pclk2, P_USART6);

    // ---- SPI e I2S ---------------------------------------------------------
    // El reloj de audio de los bloques con I2S es la salida R del PLLI2S; se
    // entrega la onda y su frecuencia, como en el resto del modelo [IR, §12.7].
    // Los bloques de extensión I2SxEXT comparten reloj y gating con su SPI padre.
    bind_bus_slave(spi1, s_pclk2, P_SPI1);
    bind_bus_slave(spi2, s_pclk1, P_SPI2);
    bind_bus_slave(spi3, s_pclk1, P_SPI3);
    bind_bus_slave(i2s2ext, s_pclk1, P_SPI2);
    bind_bus_slave(i2s3ext, s_pclk1, P_SPI3);
    spi1.i2s_ext_clk(s_false);         // SPI1 no tiene modo I2S en el F407
    spi1.i2s_clk_hz(s_zero_hz);
    spi2.i2s_ext_clk(rcc.s_i2s_clk);   spi2.i2s_clk_hz(rcc.s_i2s_hz);
    spi3.i2s_ext_clk(rcc.s_i2s_clk);   spi3.i2s_clk_hz(rcc.s_i2s_hz);
    i2s2ext.i2s_ext_clk(rcc.s_i2s_clk); i2s2ext.i2s_clk_hz(rcc.s_i2s_hz);
    i2s3ext.i2s_ext_clk(rcc.s_i2s_clk); i2s3ext.i2s_clk_hz(rcc.s_i2s_hz);
    // Un I2SxEXT no tiene pines de reloj ni de sincronismo: cuelga de los mismos
    // hilos que su bloque padre. Se le entrega la entrada del pad de esos pines,
    // que es exactamente lo que ve el silicio [IR, §2.1].
    i2s2ext.ext_ck(pinmux.pad_din[1 * N_PORT_PINS + 13]);   // PB13 I2S2_CK
    i2s2ext.ext_ws(pinmux.pad_din[1 * N_PORT_PINS + 12]);   // PB12 I2S2_WS
    i2s3ext.ext_ck(pinmux.pad_din[2 * N_PORT_PINS + 10]);   // PC10 I2S3_CK
    i2s3ext.ext_ws(pinmux.pad_din[0 * N_PORT_PINS + 15]);   // PA15 I2S3_WS
    spi1.ext_ck(s_false); spi1.ext_ws(s_false);
    spi2.ext_ck(s_false); spi2.ext_ws(s_false);
    spi3.ext_ck(s_false); spi3.ext_ws(s_false);

    bind_bus_slave(i2c1, s_pclk1, P_I2C1);
    bind_bus_slave(i2c2, s_pclk1, P_I2C2);
    bind_bus_slave(i2c3, s_pclk1, P_I2C3);

    bind_bus_slave(can1, s_pclk1, P_CAN1);
    bind_bus_slave(can2, s_pclk1, P_CAN2);
    can1.freeze(s_freeze[FZ_CAN1]);
    can2.freeze(s_freeze[FZ_CAN2]);
    can2.bind_filter_master(&can1);    // filtros compartidos [IR, §12.12]

    // ---- ADC1/2/3 y bloque comun [IR, 12.13] ------------------------------
    // Los vectores de disparo estan indexados POR EL VALOR DE EXTSEL/JEXTSEL,
    // asi que cada fuente se conecta en su hueco de la tabla del manual. Del
    // F407 se modelan las salidas TRGO de los temporizadores; los disparos por
    // evento de captura/comparacion (TIMx_CHy) y por EXTI11/EXTI15 quedan a
    // cero porque el modelo del temporizador no exporta el evento CC en crudo
    // -solo su peticion de DMA, que esta condicionada por DIER-. Vease
    // doc/stm32f407vg_fase5_adc.md, seccion 6.
    bind_bus_slave(adc, s_pclk2, P_ADC);
    adc.vdda(s_vdda);
    adc.vref(s_vdda);                  // VREF+ unido a VDDA en la placa tipica
    adc.vbat_in(s_vbat);
    // EXTSEL:  0110 = TIM2_TRGO, 1000 = TIM3_TRGO, 1110 = TIM8_TRGO
    // JEXTSEL: 0001 = TIM1_TRGO, 0011 = TIM2_TRGO, 1001 = TIM4_TRGO,
    //          1011 = TIM5_TRGO
    static const int EXTSEL_SRC[16]  = {-1,-1,-1,-1,-1,-1, 1,-1,
                                         2,-1,-1,-1,-1,-1, 7,-1};
    static const int JEXTSEL_SRC[16] = {-1, 0,-1, 1,-1,-1,-1,-1,
                                        -1, 3,-1, 4,-1,-1,-1,-1};
    for (unsigned i = 0; i < 16; ++i) {
        adc.trig_regular[i](EXTSEL_SRC[i]  >= 0 ? s_trgo[unsigned(EXTSEL_SRC[i])]  : s_false);
        adc.trig_injected[i](JEXTSEL_SRC[i] >= 0 ? s_trgo[unsigned(JEXTSEL_SRC[i])] : s_false);
    }

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
    iwdg.lsi_clk(s_lsiclk); iwdg.lsi_hz(s_lsi_hz); iwdg.freeze(s_freeze[FZ_IWDG]);
    iwdg.hw_start(s_false);            // TODO(F2): option bit WDG_SW
    iwdg.rst_req(s_iwdg_rr);
    iwdg.lsi_on_req(s_iwdg_lsi);
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
    // --- Bajo consumo [IR, §14] --------------------------------------------
    pwr.hclk_hz(s_hclk_hz);                 // términos del modelo de consumo
    pwr.rtcclk_hz(s_rtcclk_hz);
    pwr.periph_on(s_periph_on);
    pwr.dbg_lp(s_dbg_lp);
    // Las tres fuentes de despertar del Standby que no son el pin [IR, §14.8]
    pwr.rtc_alarm(s_rtc_l17);
    pwr.rtc_tamper(s_rtc_l21);
    pwr.rtc_wkup(s_rtc_l22);
    pwr.standby_req(s_standby);             // -> RCC: apagar el dominio 1,2 V
    pwr.stop_req(s_stop_req);               // -> RCC: parar los relojes
    pwr.lp_mode(s_lp_mode);
    pwr.ewup(s_ewup);
    pwr.bre(s_bre);
    pwr.idd(s_idd);                         // -> pines de alimentación
    pwr.ibat(s_ibat);
    pwr.vos_rdy(s_nc[nc()]);

    bind_bus_slave(crc, s_hclk, P_CRC);
    bind_bus_slave(rng, s_hclk, P_RNG);
    // El RNG cuelga del PLL48CK, no de HCLK: su vigilancia de reloj compara las
    // DOS frecuencias, y por eso necesita las dos [IR, §12.19].
    rng.pll48ck(s_pll48); rng.pll48ck_hz(s_pll48_hz); rng.hclk_hz(s_hclk_hz);
    bind_bus_slave(sdio, s_pclk2, P_SDIO);
    sdio.sdioclk(s_pll48);
    sdio.sdioclk_hz(s_pll48_hz);
    bind_bus_slave(dcmi, s_hclk, P_DCMI);
    bind_bus_slave(eth, s_hclk, P_ETHMAC);
    eth.mii_rmii_sel(s_mii);
    eth.wkup_line(s_ethwk_l19);
    bind_bus_slave(otg_fs, s_hclk, P_OTGFS);
    otg_fs.clk48(s_pll48);   otg_fs.clk48_hz(s_pll48_hz);
    otg_fs.wkup_line(s_fswk_l18);
    otg_fs.irq_wkup(s_nc[nc()]);            // la IRQ 42 real llega vía EXTI18
    otg_fs.irq_ep1_out(s_nc[nc()]);         // el FS no tiene IRQ de EP1
    otg_fs.irq_ep1_in(s_nc[nc()]);
    bind_bus_slave(otg_hs, s_hclk, P_OTGHS);
    otg_hs.clk48(s_pll48);   otg_hs.clk48_hz(s_pll48_hz);
    otg_hs.wkup_line(s_hswk_l20);
    otg_hs.irq_wkup(s_nc[nc()]);
    bind_bus_slave(dma1, s_hclk, P_DMA1);
    bind_bus_slave(dma2, s_hclk, P_DMA2);
}

// ===========================================================================
// Interrupciones -> NVIC (numeración exacta [IR, §9.1.2])
// ===========================================================================
inline void SocF4::bind_irqs() {
    // Todos los temporizadores salen del mismo modelo y por tanto tienen las
    // cinco salidas de interrupción; los rasgos deciden CUÁLES se activan (los
    // avanzados usan los cuatro vectores separados y el resto el global), así
    // que aquí se deja sin conectar la que cada familia no usa.
    auto tim_split_unused = [&](TimerBase& t) {
        t.irq_up(s_nc[nc()]); t.irq_cc(s_nc[nc()]);
        t.irq_trg_com(s_nc[nc()]); t.irq_brk(s_nc[nc()]);
    };
    TimerBase* t_simple[12] = {&tim2, &tim3, &tim4, &tim5, &tim6, &tim7,
                               &tim9, &tim10, &tim11, &tim12, &tim13, &tim14};
    for (TimerBase* t : t_simple) tim_split_unused(*t);

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
    spi1.irq(s_irq[35]);
    // I2S2ext e I2S3ext comparten el vector de su SPI padre [IR, §12.5.3-D]
    spi2.irq(s_or_in[14]);    i2s2ext.irq(s_or_in[15]);
    or_spi2.a(s_or_in[14]); or_spi2.b(s_or_in[15]); or_spi2.y(s_irq[36]);
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
    spi3.irq(s_or_in[16]);    i2s3ext.irq(s_or_in[17]);
    or_spi3.a(s_or_in[16]); or_spi3.b(s_or_in[17]); or_spi3.y(s_irq[51]);
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
inline void SocF4::bind_dma_requests() {
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
    // [IR] no recoge las celdas de DMA de los bloques de extension del I2S:
    // sus lineas quedan al aire hasta disponer de la tabla (véase el informe).
    i2s2ext.dma_req_rx(s_nc[nc()]); i2s2ext.dma_req_tx(s_nc[nc()]);
    i2s3ext.dma_req_rx(s_nc[nc()]); i2s3ext.dma_req_tx(s_nc[nc()]);
    i2c1.dma_req_rx(q_i2c1_rx); i2c1.dma_req_tx(q_i2c1_tx);
    i2c2.dma_req_rx(q_i2c2_rx); i2c2.dma_req_tx(q_i2c2_tx);
    i2c3.dma_req_rx(q_i2c3_rx); i2c3.dma_req_tx(q_i2c3_tx);
    adc.dma_req_adc1(q_adc1); adc.dma_req_adc2(q_adc2); adc.dma_req_adc3(q_adc3);
    dac.dma_req_ch1(q_dac1);  dac.dma_req_ch2(q_dac2);
    sdio.dma_req(q_sdio);     dcmi.dma_req(q_dcmi);
    // q_tim_up / q_tim_cc: 0=TIM1 1=TIM2 2=TIM3 3=TIM4 4=TIM5 5=TIM8
    tim6.dma_up(q_tim6_up);   tim7.dma_up(q_tim7_up);
    tim1.dma_up(q_tim_up[0]); tim1.dma_trig(q_tim1_trig);
    for (unsigned i = 0; i < 4; ++i) tim1.dma_cc[i](q_tim_cc[0 * 4 + i]);
    tim8.dma_up(q_tim_up[5]); tim8.dma_trig(q_tim8_trig);
    for (unsigned i = 0; i < 4; ++i) tim8.dma_cc[i](q_tim_cc[5 * 4 + i]);
    TimerBase* tg[4] = {&tim2, &tim3, &tim4, &tim5};
    for (unsigned t = 0; t < 4; ++t) {
        tg[t]->dma_up(q_tim_up[1 + t]);
        for (unsigned i = 0; i < 4; ++i) tg[t]->dma_cc[i](q_tim_cc[(1 + t) * 4 + i]);
    }
    // TIM6/TIM7 no tienen canales, y TIM9-14 no tienen peticiones de DMA en el
    // F407 [IR, §11.4]: sus líneas de captura/comparación quedan al aire.
    for (unsigned i = 0; i < 4; ++i) { tim6.dma_cc[i](s_nc[nc()]);
                                       tim7.dma_cc[i](s_nc[nc()]); }
    TimerBase* tn[6] = {&tim9, &tim10, &tim11, &tim12, &tim13, &tim14};
    for (TimerBase* t : tn) {
        t->dma_up(s_nc[nc()]);
        for (unsigned i = 0; i < 4; ++i) t->dma_cc[i](s_nc[nc()]);
    }
    // TRIG de los de propósito general y COM de todos: el modelo publica la
    // línea, pero la tabla de [IR, §11.4] no le asigna celda propia en el F407.
    TimerBase* tall[14] = {&tim1, &tim2, &tim3, &tim4, &tim5, &tim6, &tim7,
                           &tim8, &tim9, &tim10, &tim11, &tim12, &tim13, &tim14};
    for (TimerBase* t : tall) {
        if (t != &tim1 && t != &tim8) t->dma_trig(s_nc[nc()]);
        t->dma_com(s_nc[nc()]);
    }

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
inline void SocF4::bind_analog() {
    // Canales ADC externos [IR, §12.13-E]. La distincion ADC123 / ADC12 es
    // REAL y es la principal diferencia entre las tres instancias: IN0-3 e
    // IN10-13 llegan a los tres convertidores, mientras que IN4-9 e IN14/15
    // solo llegan al ADC1 y al ADC2. En el ADC3 esas mismas entradas van a
    // pines del puerto F, que el encapsulado LQFP100 no tiene: se quedan sin
    // conectar, y el modelo las trata como lo que son [IR, §2.1, §12.13-E].
    for (unsigned i = 0; i < 4; ++i)
        adc.bind_channel(i, pinmux.analog(0, i));           // PA0-PA3  ADC123_IN0-3
    for (unsigned i = 4; i < 8; ++i)
        adc.bind_channel_12(i, pinmux.analog(0, i));        // PA4-PA7  ADC12_IN4-7
    adc.bind_channel_12(8, pinmux.analog(1, 0));            // PB0      ADC12_IN8
    adc.bind_channel_12(9, pinmux.analog(1, 1));            // PB1      ADC12_IN9
    for (unsigned i = 0; i < 4; ++i)
        adc.bind_channel(10 + i, pinmux.analog(2, i));      // PC0-PC3  ADC123_IN10-13
    adc.bind_channel_12(14, pinmux.analog(2, 4));           // PC4      ADC12_IN14
    adc.bind_channel_12(15, pinmux.analog(2, 5));           // PC5      ADC12_IN15
    // DAC [IR, §12.14]
    dac.bind_out(0, pinmux.analog(0, 4));        // PA4
    dac.bind_out(1, pinmux.analog(0, 5));        // PA5
    // El Ethernet (AF11) se registra con el resto del mapa de AF, en
    // `soc/f4_mapa_af.h`. Es el unico numero de AF que se VACIA en un F446,
    // asi que conviene tenerlo localizado y no enredado aqui.

    // USB OTG_FS: PHY integrado en PA11 (DM) y PA12 (DP), sensado de VBUS en
    // PA9 y pin ID en PA10 [IR, §12.15.4]. Los cuatro son RUTA ANALOGICA, no
    // funcion alternativa digital: por D+ y D- no van unos y ceros, van
    // tensiones, y de ellas salen la conexion y la velocidad.
    otg_fs.bind_phy(pinmux.analog(0, 11), pinmux.analog(0, 12));
    otg_fs.bind_vbus(pinmux.analog(0, 9));
    otg_fs.bind_id(pinmux.analog(0, 10));
    // USB OTG_HS: en este encapsulado el nucleo HS tiene DOS caminos posibles.
    //   * su transceptor FS integrado, por PB14 (DM) y PB15 (DP);
    //   * o el PHY externo ULPI (AF10), que SI esta completo en el LQFP100.
    // VBUS en PB13 y ID en PB12 [IR, cap. 2].
    otg_hs.bind_phy(pinmux.analog(1, 14), pinmux.analog(1, 15));
    otg_hs.bind_vbus(pinmux.analog(1, 13));
    otg_hs.bind_id(pinmux.analog(1, 12));
    // El ULPI del OTG HS (AF10) se registra con el resto del mapa de AF,
    // en `soc/f4_mapa_af.h`: aqui solo queda el enganche ELECTRICO del PHY.
    // Osciladores externos: HSE en PH0/PH1 (OSC_IN/OSC_OUT) y LSE en PC14/PC15
    // (OSC32_IN/OSC32_OUT) [IR, §2.1]. El oscilador solo arranca si hay algo
    // conectado eléctricamente al nodo de OSC_IN; en modo bypass mide además la
    // frecuencia del reloj inyectado a partir del pad [IR, §4.2].
    rcc.hse.xtal_in = &pinmux.analog(7, 0);      // PH0-OSC_IN
    rcc.hse.ext_in  = &pinmux.pad_din[7 * N_PORT_PINS + 0];
    rcc.lse.xtal_in = &pinmux.analog(2, 14);     // PC14-OSC32_IN
    rcc.lse.ext_in  = &pinmux.pad_din[2 * N_PORT_PINS + 14];
    // La tabla de funciones alternativas de los periféricos de comunicación
    // vive en `soc/f4_mapa_af.h`. No es una separación cosmética: es EL MAPA
    // DE LA FAMILIA, y lo que decide si un puerto serie sale por PA9 o por
    // PB6. Estaba aquí dentro, en una función llamada `bind_analog`, que es
    // el último sitio donde nadie lo buscaría.
    bind_mapa_af();
}

} // namespace stm32
#include "../soc/f4_mapa_af.h"   // el mapa de AF, que era este fichero

#endif // STM32_TOP_SOC_F4_BIND2_H
