// =============================================================================
// rcc.h — Reset and Clock Control [IR, §4]
//
// Submódulos: hsi/hse/lsi/lse (Oscillator), pll/plli2s (Pll) y, como procesos
// internos, el árbol de reloj (mux SW + prescalers + MCO + mux RTCCLK) y el
// controlador de reset (POR/BOR/NRST/WWDG/IWDG/SW). Publica los relojes del
// sistema, el gating por periférico (registros ENR) y los resets por periférico
// (registros RSTR). El RTC no es hijo de RCC (plan P4): aquí solo se genera
// RTCCLK.
//
// Fase F1 — implementado:
//   * banco de registros completo [IR, §4.5-4.12] con máscaras de escritura,
//     bits de solo lectura, bits rc_w1 y valores de reset;
//   * cálculo del árbol: SYSCLK (mux SW), HCLK (HPRE), PCLK1/PCLK2 (PPRE1/2),
//     TIMxCLK (regla PCLKx o 2xPCLKx), PLL48CK (PLLQ), RTCCLK (RTCSEL),
//     SysTick externo (HCLK/8), MCO1/MCO2 con sus prescalers;
//   * gating y reset por periférico -> vectores periph_clk_en[] / periph_rst_n[];
//   * controlador de reset con pulso mínimo de 20 us y flags de RCC_CSR.
// Fase F3 — añadido:
//   * arranque condicionado a la presencia eléctrica del cristal o del reloj
//     externo en OSC_IN (HSE y LSE) y medida de la frecuencia en modo bypass;
//   * Clock Security System: fallo del HSE -> conmutación a HSI, CSSF y NMI;
//   * modulación de espectro ensanchado del PLL principal (RCC_SSCGR): se
//     calculan f_Mod y la profundidad, y se aplica el desplazamiento medio;
//   * MCO1 (PA8) y MCO2 (PC9) encaminados a sus pads como AF0 desde el top;
//   * frecuencia de dominio publicada a cada BusSlave (BusSlave::clk_hz).
// =============================================================================
#ifndef STM32_RCC_RCC_H
#define STM32_RCC_RCC_H

#include "../common/periph_base.h"
#include "osc_pll.h"

namespace stm32 {

// Índice único de reloj/reset por periférico (bits ENR/RSTR) [IR, §4.7/4.8]
enum PeriphId : unsigned {
    // AHB1
    P_GPIOA, P_GPIOB, P_GPIOC, P_GPIOD, P_GPIOE, P_GPIOF, P_GPIOG, P_GPIOH,
    P_GPIOI, P_CRC, P_BKPSRAM, P_CCMRAM, P_DMA1, P_DMA2, P_ETHMAC, P_OTGHS,
    // AHB2
    P_DCMI, P_RNG, P_OTGFS,
    // AHB3
    P_FSMC,
    // APB1
    P_TIM2, P_TIM3, P_TIM4, P_TIM5, P_TIM6, P_TIM7, P_TIM12, P_TIM13, P_TIM14,
    P_WWDG, P_SPI2, P_SPI3, P_USART2, P_USART3, P_UART4, P_UART5,
    P_I2C1, P_I2C2, P_I2C3, P_CAN1, P_CAN2, P_PWR, P_DAC,
    // APB2
    P_TIM1, P_TIM8, P_USART1, P_USART6, P_ADC, P_SDIO, P_SPI1, P_SYSCFG,
    P_TIM9, P_TIM10, P_TIM11,
    P_COUNT
};

// Correspondencia (registro, bit) -> periférico, para ENR/RSTR/LPENR.
enum RccRegGroup : uint8_t { G_AHB1 = 0, G_AHB2, G_AHB3, G_APB1, G_APB2 };
struct RccBitMap { uint8_t grp; uint8_t bit; uint16_t id; };

// [IR, §4.7 y §4.8]: mismas posiciones de bit en RSTR y ENR.
static const RccBitMap RCC_BITMAP[] = {
    // ---- AHB1 ----
    {G_AHB1, 0, P_GPIOA}, {G_AHB1, 1, P_GPIOB}, {G_AHB1, 2, P_GPIOC},
    {G_AHB1, 3, P_GPIOD}, {G_AHB1, 4, P_GPIOE}, {G_AHB1, 5, P_GPIOF},
    {G_AHB1, 6, P_GPIOG}, {G_AHB1, 7, P_GPIOH}, {G_AHB1, 8, P_GPIOI},
    {G_AHB1, 12, P_CRC}, {G_AHB1, 18, P_BKPSRAM}, {G_AHB1, 20, P_CCMRAM},
    {G_AHB1, 21, P_DMA1}, {G_AHB1, 22, P_DMA2}, {G_AHB1, 25, P_ETHMAC},
    {G_AHB1, 29, P_OTGHS},
    // ---- AHB2 ----
    {G_AHB2, 0, P_DCMI}, {G_AHB2, 6, P_RNG}, {G_AHB2, 7, P_OTGFS},
    // ---- AHB3 ----
    {G_AHB3, 0, P_FSMC},
    // ---- APB1 ----
    {G_APB1, 0, P_TIM2}, {G_APB1, 1, P_TIM3}, {G_APB1, 2, P_TIM4},
    {G_APB1, 3, P_TIM5}, {G_APB1, 4, P_TIM6}, {G_APB1, 5, P_TIM7},
    {G_APB1, 6, P_TIM12}, {G_APB1, 7, P_TIM13}, {G_APB1, 8, P_TIM14},
    {G_APB1, 11, P_WWDG}, {G_APB1, 14, P_SPI2}, {G_APB1, 15, P_SPI3},
    {G_APB1, 17, P_USART2}, {G_APB1, 18, P_USART3}, {G_APB1, 19, P_UART4},
    {G_APB1, 20, P_UART5}, {G_APB1, 21, P_I2C1}, {G_APB1, 22, P_I2C2},
    {G_APB1, 23, P_I2C3}, {G_APB1, 25, P_CAN1}, {G_APB1, 26, P_CAN2},
    {G_APB1, 28, P_PWR}, {G_APB1, 29, P_DAC},
    // ---- APB2 ----
    {G_APB2, 0, P_TIM1}, {G_APB2, 1, P_TIM8}, {G_APB2, 4, P_USART1},
    {G_APB2, 5, P_USART6}, {G_APB2, 8, P_ADC}, {G_APB2, 11, P_SDIO},
    {G_APB2, 12, P_SPI1}, {G_APB2, 14, P_SYSCFG}, {G_APB2, 16, P_TIM9},
    {G_APB2, 17, P_TIM10}, {G_APB2, 18, P_TIM11}
};
constexpr unsigned RCC_BITMAP_N = sizeof(RCC_BITMAP) / sizeof(RCC_BITMAP[0]);

class Rcc : public BusSlave {
public:
    // ---- Offsets de registro [IR, §4.12] -----------------------------------
    enum : uint32_t {
        R_CR = 0x00, R_PLLCFGR = 0x04, R_CFGR = 0x08, R_CIR = 0x0C,
        R_AHB1RSTR = 0x10, R_AHB2RSTR = 0x14, R_AHB3RSTR = 0x18,
        R_APB1RSTR = 0x20, R_APB2RSTR = 0x24,
        R_AHB1ENR = 0x30, R_AHB2ENR = 0x34, R_AHB3ENR = 0x38,
        R_APB1ENR = 0x40, R_APB2ENR = 0x44,
        R_AHB1LPENR = 0x50, R_AHB2LPENR = 0x54, R_AHB3LPENR = 0x58,
        R_APB1LPENR = 0x60, R_APB2LPENR = 0x64,
        R_BDCR = 0x70, R_CSR = 0x74, R_SSCGR = 0x80, R_PLLI2SCFGR = 0x84
    };

    // ---- Relojes del sistema hacia el resto del modelo [IR, §4.3/4.4] -----
    sc_core::sc_out<bool>   hclk{"hclk"};        sc_core::sc_out<double> hclk_hz{"hclk_hz"};
    sc_core::sc_out<bool>   pclk1{"pclk1"};      sc_core::sc_out<double> pclk1_hz{"pclk1_hz"};
    sc_core::sc_out<bool>   pclk2{"pclk2"};      sc_core::sc_out<double> pclk2_hz{"pclk2_hz"};
    sc_core::sc_out<bool>   timclk1{"timclk1"};  sc_core::sc_out<double> timclk1_hz{"timclk1_hz"};
    sc_core::sc_out<bool>   timclk2{"timclk2"};  sc_core::sc_out<double> timclk2_hz{"timclk2_hz"};
    sc_core::sc_out<bool>   pll48ck{"pll48ck"};  sc_core::sc_out<double> pll48ck_hz{"pll48ck_hz"};
    sc_core::sc_out<bool>   rtcclk{"rtcclk"};    sc_core::sc_out<double> rtcclk_hz{"rtcclk_hz"};
    // El LSI alimenta al IWDG, que debe seguir contando aunque el reloj de
    // sistema se pare: se saca tambien su frecuencia para que el perro pueda
    // programar sus propios eventos sin depender de la onda cuadrada.
    sc_core::sc_out<bool>   lsi_clk{"lsi_clk"};
    sc_core::sc_out<double> lsi_hz{"lsi_hz"};
    sc_core::sc_out<bool>   systick_ext{"systick_ext"};   // HCLK/8

    // ---- Bajo consumo [IR, §14] -------------------------------------------
    // El PWR es quien manda: dice en qué modo está el MCU y, cuando toca,
    // ordena parar los relojes del dominio de 1,2 V o apagarlo entero.
    sc_core::sc_in<uint8_t> lp_mode{"lp_mode"};        // LpMode
    sc_core::sc_in<bool>    stop_req{"stop_req"};      // Stop: parar relojes
    sc_core::sc_in<bool>    standby_req{"standby_req"};// Standby: apagar 1,2 V
    // Cuántos relojes de periférico están abiertos AHORA (con el criterio del
    // modo: ENR en Run, LPENR en Sleep). Es el término que más pesa en el
    // consumo y por eso lo necesita el modelo de IDD del PWR.
    sc_core::sc_out<unsigned> periph_on{"periph_on"};

    // ---- Gating y reset por periférico (ENR/RSTR) --------------------------
    sc_core::sc_vector<sc_core::sc_out<bool>> periph_clk_en;   // [PeriphId]
    sc_core::sc_vector<sc_core::sc_out<bool>> periph_rst_n;    // [PeriphId]

    // ---- Resets globales ---------------------------------------------------
    sc_core::sc_out<bool> sys_rst_n{"sys_rst_n"};
    sc_core::sc_out<bool> bkp_rst_n{"bkp_rst_n"};
    sc_core::sc_out<bool> drive_nrst_low{"drive_nrst_low"};

    // ---- Entradas de las fuentes de reset [IR, §4.1] -----------------------
    sc_core::sc_in<bool> por_ok{"por_ok"};
    sc_core::sc_in<bool> bor_rst{"bor_rst"};    // la caída la detectó el BOR
    sc_core::sc_in<bool> nrst_in_n{"nrst_in_n"};
    sc_core::sc_in<bool> wwdg_rst_req{"wwdg_rst_req"};
    sc_core::sc_in<bool> iwdg_rst_req{"iwdg_rst_req"};
    // Arrancar el perro independiente ENCIENDE el LSI por hardware, sin que
    // el firmware tenga que tocar RCC_CSR.LSION: si no fuera asi, bastaria
    // con apagar el oscilador para desarmar el vigilante [IR, §12.11].
    sc_core::sc_in<bool> iwdg_lsi_req{"iwdg_lsi_req"};
    sc_core::sc_in<bool> sysresetreq{"sysresetreq"};
    sc_core::sc_out<bool> nmi_css{"nmi_css"};
    sc_core::sc_out<bool> irq{"irq"};                // RCC global (IRQ 5)

    // ---- MCO1 (PA8) / MCO2 (PC9): endpoints digitales hacia pin_mux --------
    sc_core::sc_signal<bool> mco1_sig{"mco1_sig"}, mco2_sig{"mco2_sig"};

    // Estado combinacional del gating por periférico (véase BusSlave::clk_en_live)
    const bool* clk_en_ptr(unsigned id) const { return &o_pcen_[id]; }

    // ---- Osciladores y PLLs ------------------------------------------------
    Oscillator hsi{"hsi"}, hse{"hse"}, lsi{"lsi"}, lse{"lse"};
    Pll        pll{"pll"}, plli2s{"plli2s"};

    // ---- Parámetros temporales del reset [IR, §4.1.1] ----------------------
    sc_core::sc_time t_rst_pulse{20, sc_core::SC_US};   // pulso mínimo interno
    // Temporización total VDD estable -> primera instrucción: 0.5 a 3.0 ms
    // (típico 1.5 ms). Se deja como parámetro para no penalizar la simulación.
    sc_core::sc_time t_rst_release{20, sc_core::SC_US};

    explicit Rcc(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::RCC_B, 0x400),
          periph_clk_en("periph_clk_en", P_COUNT),
          periph_rst_n("periph_rst_n", P_COUNT),
          g_hclk_("g_hclk"), g_pclk1_("g_pclk1"), g_pclk2_("g_pclk2"),
          g_timclk1_("g_timclk1"), g_timclk2_("g_timclk2"),
          g_rtcclk_("g_rtcclk"), g_stk_("g_stk"),
          g_mco1_("g_mco1"), g_mco2_("g_mco2") {
        SC_HAS_PROCESS(Rcc);
        // Frecuencias nominales y tiempos de arranque [IR, §4.2]
        hsi.nominal_hz = 16e6;    hsi.t_startup_s = 4e-6;
        hse.nominal_hz = 8e6;     hse.t_startup_s = 2e-3;   // según cristal externo
        lsi.nominal_hz = 32e3;    lsi.t_startup_s = 40e-6;
        lse.nominal_hz = 32768.0; lse.t_startup_s = 2.0;
        // HSE y LSE necesitan componente externo en OSC_IN [IR, §4.2]. El top
        // les entrega el nodo analógico del pad (PH0 y PC14).
        hse.needs_source = true;
        lse.needs_source = true;
        bind_internal_();

        SC_THREAD(reset_ctrl_proc);
        SC_THREAD(clock_tree_proc);
        // Un único proceso escribe los puertos de salida: el estado lo pueden
        // actualizar tanto los procesos internos como el b_transport del banco
        // de registros (que corre en el proceso del maestro), y SystemC no
        // admite dos escritores sobre un mismo sc_signal.
        SC_METHOD(publish_proc);     sensitive << pub_ev_;    dont_initialize();
        SC_METHOD(lsi_mirror_proc);  sensitive << s_lsi_clk << s_lsi_hz;
        dont_initialize();
        SC_METHOD(iwdg_lsi_proc);    sensitive << iwdg_lsi_req; dont_initialize();
        SC_THREAD(css_nmi_proc);
        SC_METHOD(rst_src_proc);
        sensitive << por_ok << bor_rst << nrst_in_n << wwdg_rst_req
                  << iwdg_rst_req << sysresetreq << standby_req;
        dont_initialize();
        SC_METHOD(lp_proc);
        sensitive << lp_mode << stop_req << standby_req;
        dont_initialize();
        // Valores de reset iniciales. No se llama a apply_osc_controls() aquí:
        // notificar eventos durante la elaboración no está permitido; el primer
        // reset real lo aplica reset_ctrl_proc en t = 0.
        init_registers();
        bdcr_ = 0x00000000u;
        csr_  = 0x0E000000u;      // PORRSTF | PINRSTF | BORRSTF [IR, §4.10]
    }

    // Onda cuadrada de los relojes internos de alta frecuencia. Apagarla
    // acelera mucho las cargas largas de CPU: los consumidores internos usan
    // la frecuencia (xxx_hz) y programan sus propios eventos. Debe estar
    // encendida cuando algo mida flancos (MCO, GPIO, captura de temporizadores).
    void set_internal_waveforms(bool on) {
        // Los generadores de MCO1/MCO2 entran también: su fuente por defecto
        // (MCO2SEL = 00) es SYSCLK, de modo que a 168 MHz dominan el coste de
        // simulación aunque PA8/PC9 no estén configurados como AF0.
        g_mco1_.set_waveform(on);
        g_mco2_.set_waveform(on);
        g_rtcclk_.set_waveform(on);
        // También las fuentes: la salida P del PLL a 168 MHz es tan cara como
        // el propio HCLK aunque nadie mida sus flancos.
        hsi.set_waveform(on); hse.set_waveform(on);
        lsi.set_waveform(on); lse.set_waveform(on);
        pll.set_waveform(on); plli2s.set_waveform(on);
        g_hclk_.set_waveform(on);
        g_pclk1_.set_waveform(on);
        g_pclk2_.set_waveform(on);
        g_timclk1_.set_waveform(on);
        g_timclk2_.set_waveform(on);
        g_stk_.set_waveform(on);
    }

    // --- Consulta del árbol (verificación y otros módulos) ------------------
    double sysclk_hz() const { return f_sysclk_; }
    double hclk_freq() const { return f_hclk_; }
    double pclk1_freq() const { return f_pclk1_; }
    double pclk2_freq() const { return f_pclk2_; }
    double pll48_freq() const { return f_pll48_; }
    double rtc_freq()   const { return f_rtc_; }
    uint32_t peek_reg(uint32_t off) const { return const_cast<Rcc*>(this)->reg_read(off); }

protected:
    uint32_t reg_read(uint32_t off) override;
    void     reg_write(uint32_t off, uint32_t v, uint32_t be) override;
    bool     responds_without_clock() const override { return true; }

private:
    void bind_internal_();

    // ---- Banco de registros [IR, §4.12] ------------------------------------
    // ⚠ NO DISPONIBLE EN LAS FUENTES: valor de calibración de fábrica HSICAL
    // (el informe lo indica como 'XX'). Se usa 0x10 como valor del modelo.
    static constexpr uint32_t HSICAL_FACTORY = 0x10;
    uint32_t cr_, pllcfgr_, cfgr_, cir_;
    uint32_t ahb1rstr_, ahb2rstr_, ahb3rstr_, apb1rstr_, apb2rstr_;
    uint32_t ahb1enr_, ahb2enr_, ahb3enr_, apb1enr_, apb2enr_;
    uint32_t ahb1lpenr_, ahb2lpenr_, ahb3lpenr_, apb1lpenr_, apb2lpenr_;
    uint32_t bdcr_, csr_, sscgr_, plli2scfgr_;

    // Frecuencias calculadas
    double f_sysclk_ = 0, f_hclk_ = 0, f_pclk1_ = 0, f_pclk2_ = 0;
    double f_pll48_ = 0, f_rtc_ = 0;
    unsigned sws_ = 0;

    // Flags previos de RDY para detectar flancos (RCC_CIR) [IR, §4.6]
    bool prev_rdy_[6] = {};

    ClockGen g_hclk_, g_pclk1_, g_pclk2_, g_timclk1_, g_timclk2_;
    ClockGen g_rtcclk_, g_stk_, g_mco1_, g_mco2_;
    sc_core::sc_event clk_ev_, rst_req_ev_;
    sc_core::sc_event_or_list arbol_ev_;   // armada al arrancar clock_tree_proc

    // -----------------------------------------------------------------------
    // Valores de reset [IR, §4.12]. bkp = true => también el dominio de backup.
    // El reset de sistema NO borra los flags de reset de RCC_CSR ni RCC_BDCR
    // [IR, §4.1.1].
    // -----------------------------------------------------------------------
    void reset_registers(bool bkp) {
        init_registers();
        if (bkp) { bdcr_ = 0x00000000u; csr_ = 0x0E000000u; }
        apply_osc_controls();
    }
    void init_registers() {
        cr_        = 0x00000083u | (HSICAL_FACTORY << 8);
        pllcfgr_   = 0x24003010u;
        cfgr_      = 0x00000000u;
        cir_       = 0x00000000u;
        ahb1rstr_ = ahb2rstr_ = ahb3rstr_ = apb1rstr_ = apb2rstr_ = 0;
        ahb1enr_   = 0x00100000u;   // CCMDATARAMEN = 1
        ahb2enr_ = ahb3enr_ = apb1enr_ = apb2enr_ = 0;
        ahb1lpenr_ = 0x7E6791FFu; ahb2lpenr_ = 0x000000F1u;
        ahb3lpenr_ = 0x00000001u; apb1lpenr_ = 0x36FEC9FFu;
        apb2lpenr_ = 0x00075F33u;
        sscgr_     = 0x00000000u;
        plli2scfgr_= 0x20003000u;
    }

    // --- Decodificación de divisores [IR, §4.5.3] ---------------------------
    static unsigned hpre_div(unsigned h) {
        static const unsigned d[8] = {2, 4, 8, 16, 64, 128, 256, 512};
        return (h & 0x8u) ? d[h & 0x7u] : 1u;
    }
    static unsigned ppre_div(unsigned p) {
        static const unsigned d[4] = {2, 4, 8, 16};
        return (p & 0x4u) ? d[p & 0x3u] : 1u;
    }
    static unsigned mcopre_div(unsigned p) {
        static const unsigned d[4] = {2, 3, 4, 5};
        return (p & 0x4u) ? d[p & 0x3u] : 1u;
    }
    static unsigned pllp_div(unsigned p) { return 2u * (p + 1u); }  // 2,4,6,8

    // --- Propaga los bits de control a osciladores y PLLs -------------------
    void apply_osc_controls() {
        // En Stop y en Standby se apagan HSI, HSE y los PLL; el LSI y el LSE
        // NO, porque están fuera del dominio de 1,2 V y son los que mantienen
        // vivos el perro independiente y el RTC, que es justo lo que se espera
        // de ellos mientras el MCU duerme [IR, §14.4.2].
        const bool parado = relojes_parados_;
        hsi.enable(!parado && ((cr_ >> 0) & 1u));
        hse.bypass = ((cr_ >> 18) & 1u) != 0;
        hse.enable(!parado && ((cr_ >> 16) & 1u));
        lsi.enable((csr_ & 1u) || iwdg_lsi_req.read());
        lse.bypass = ((bdcr_ >> 2) & 1u) != 0;
        lse.enable(bdcr_ & 1u);

        const unsigned m = pllcfgr_ & 0x3Fu;
        const unsigned n = (pllcfgr_ >> 6) & 0x1FFu;
        const unsigned p = pllp_div((pllcfgr_ >> 16) & 0x3u);
        const unsigned q = (pllcfgr_ >> 24) & 0xFu;
        const bool src_hse = (pllcfgr_ >> 22) & 1u;
        const double ref = src_hse ? hse.out_hz() : hsi.out_hz();
        pll_ref_hz_ = ref;                    // referencia para el SSCGR
        pll.configure(m, n, p, q);
        pll.set_ref_hz(ref);
        pll.enable(!parado && ((cr_ >> 24) & 1u));

        const unsigned i2sn = (plli2scfgr_ >> 6) & 0x1FFu;
        const unsigned i2sr = (plli2scfgr_ >> 28) & 0x7u;
        plli2s.configure(m, i2sn, i2sr ? i2sr : 2u, 1u);
        plli2s.set_ref_hz(ref);
        plli2s.enable(!parado && ((cr_ >> 26) & 1u));
    }

    // --- Recalcula todo el árbol y reprograma los generadores ---------------
    void update_clocks() {
        const double f_hsi = hsi.out_hz();
        const double f_hse = hse.out_hz();
        // El espectro ensanchado del PLL principal desplaza la frecuencia
        // media de salida [IR, §4.11.1]; véase ss_factor().
        const double f_pll = pll.out_p_hz() * ss_factor();

        // Mux SW: si la fuente pedida no está lista, se mantiene la anterior
        // (el hardware no conmuta hasta que SWS refleja la nueva) [IR, §4.5.3].
        const unsigned sw = cfgr_ & 0x3u;
        double f_sys = f_hsi;
        unsigned sws = 0;
        if      (sw == 1 && f_hse > 0.0) { f_sys = f_hse; sws = 1; }
        else if (sw == 2 && f_pll > 0.0) { f_sys = f_pll; sws = 2; }
        else if (sw == 0 && f_hsi > 0.0) { f_sys = f_hsi; sws = 0; }
        else { f_sys = f_sysclk_; sws = sws_; }        // fuente no disponible
        // Con el dominio de 1,2 V parado no hay SYSCLK que valga: no es que la
        // fuente no esté lista, es que no hay reloj. Sin esta línea la regla de
        // "si la fuente pedida no está, se mantiene la anterior" dejaría el
        // árbol corriendo dentro del modo Stop.
        if (relojes_parados_) { f_sys = 0.0; sws = sws_; }
        f_sysclk_ = f_sys; sws_ = sws;

        const unsigned hd  = hpre_div((cfgr_ >> 4) & 0xFu);
        const unsigned p1d = ppre_div((cfgr_ >> 10) & 0x7u);
        const unsigned p2d = ppre_div((cfgr_ >> 13) & 0x7u);
        f_hclk_  = clk_divide(f_sysclk_, hd);
        f_pclk1_ = clk_divide(f_hclk_, p1d);
        f_pclk2_ = clk_divide(f_hclk_, p2d);
        // TIMxCLK = PCLKx si el prescaler APB es 1, si no 2*PCLKx [IR, §4.4]
        const double f_tim1 = (p1d == 1) ? f_pclk1_ : 2.0 * f_pclk1_;
        const double f_tim2 = (p2d == 1) ? f_pclk2_ : 2.0 * f_pclk2_;
        f_pll48_ = pll.out_q_hz();

        // RTCCLK [IR, §4.9]: 01 LSE, 10 LSI, 11 HSE/RTCPRE, 00 sin reloj
        const unsigned rtcsel = (bdcr_ >> 8) & 0x3u;
        const unsigned rtcpre = (cfgr_ >> 16) & 0x1Fu;
        switch (rtcsel) {
            case 1:  f_rtc_ = lse.out_hz(); break;
            case 2:  f_rtc_ = lsi.out_hz(); break;
            case 3:  f_rtc_ = (rtcpre >= 2) ? clk_divide(hse.out_hz(), rtcpre) : 0.0; break;
            default: f_rtc_ = 0.0; break;
        }
        if (!((bdcr_ >> 15) & 1u)) f_rtc_ = 0.0;       // RTCEN

        // Avisos de límites de dominio [IR, §4.4]
        if (f_hclk_  > F_HCLK_MAX)  SC_REPORT_WARNING("rcc", "HCLK > 168 MHz [IR, 4.4]");
        if (f_pclk1_ > F_PCLK1_MAX) SC_REPORT_WARNING("rcc", "PCLK1 > 42 MHz [IR, 4.4]");
        if (f_pclk2_ > F_PCLK2_MAX) SC_REPORT_WARNING("rcc", "PCLK2 > 84 MHz [IR, 4.4]");

        g_hclk_.set_freq(f_hclk_);
        g_pclk1_.set_freq(f_pclk1_);
        g_pclk2_.set_freq(f_pclk2_);
        g_timclk1_.set_freq(f_tim1);
        g_timclk2_.set_freq(f_tim2);
        g_rtcclk_.set_freq(f_rtc_);
        g_stk_.set_freq(clk_divide(f_hclk_, 8));       // SysTick externo = HCLK/8

        // MCO1 (PA8) [IR, §4.5.3]: 00 HSI, 01 LSE, 10 HSE, 11 PLL
        const unsigned mco1 = (cfgr_ >> 21) & 0x3u;
        const unsigned mco1p = mcopre_div((cfgr_ >> 24) & 0x7u);
        double f_mco1 = 0.0;
        switch (mco1) { case 0: f_mco1 = f_hsi; break; case 1: f_mco1 = lse.out_hz(); break;
                        case 2: f_mco1 = f_hse; break; default: f_mco1 = f_pll; }
        g_mco1_.set_freq(clk_divide(f_mco1, mco1p));
        // MCO2 (PC9): 00 SYSCLK, 01 PLLI2S, 10 HSE, 11 PLL
        const unsigned mco2 = (cfgr_ >> 30) & 0x3u;
        const unsigned mco2p = mcopre_div((cfgr_ >> 27) & 0x7u);
        double f_mco2 = 0.0;
        switch (mco2) { case 0: f_mco2 = f_sysclk_; break; case 1: f_mco2 = plli2s.out_p_hz(); break;
                        case 2: f_mco2 = f_hse; break; default: f_mco2 = f_pll; }
        g_mco2_.set_freq(clk_divide(f_mco2, mco2p));

        set_domain_hz(f_hclk_);
        update_cir_flags();
    }

    // --- Flags de interrupción por estabilización [IR, §4.6] ----------------
    void update_cir_flags() {
        const bool rdy[6] = { lsi.is_ready(), lse.is_ready(), hsi.is_ready(),
                              hse.is_ready(), pll.is_ready(), plli2s.is_ready() };
        for (unsigned i = 0; i < 6; ++i) {
            if (rdy[i] && !prev_rdy_[i]) cir_ |= (1u << (1 + i));   // xxxRDYF
            prev_rdy_[i] = rdy[i];
        }
        const uint32_t f  = cir_ & 0x000000FEu;          // flags 1..7
        const uint32_t ie = (cir_ >> 8) & 0x000000FEu;   // IE en 9..15
        o_irq_ = (f & ie) != 0;
        publish();
    }

    // --- Gating y reset por periférico --------------------------------------
    uint32_t grp_enr(uint8_t g) const {
        switch (g) { case G_AHB1: return ahb1enr_; case G_AHB2: return ahb2enr_;
                     case G_AHB3: return ahb3enr_; case G_APB1: return apb1enr_;
                     default:     return apb2enr_; }
    }
    uint32_t grp_rstr(uint8_t g) const {
        switch (g) { case G_AHB1: return ahb1rstr_; case G_AHB2: return ahb2rstr_;
                     case G_AHB3: return ahb3rstr_; case G_APB1: return apb1rstr_;
                     default:     return apb2rstr_; }
    }
    uint32_t grp_lpenr(uint8_t g) const {
        switch (g) { case G_AHB1: return ahb1lpenr_; case G_AHB2: return ahb2lpenr_;
                     case G_AHB3: return ahb3lpenr_; case G_APB1: return apb1lpenr_;
                     default:     return apb2lpenr_; }
    }
    // -----------------------------------------------------------------------
    // QUÉ REGISTRO GOBIERNA EL GATING, según el modo [IR, §4.8, §14.3]
    //
    //   Run            -> RCC_xxxENR
    //   Sleep          -> RCC_xxxENR **y** RCC_xxxLPENR
    //   Stop / Standby -> ninguno: no hay reloj que repartir
    //
    // DECISIÓN, porque el manual admite dos lecturas. La descripción de cada
    // bit LPEN es incondicional ("0: reloj del periférico desactivado durante
    // el modo Sleep"), y leída al pie de la letra diría que en Sleep manda
    // SOLO el LPENR. Como el valor de reset de los LPENR es "todo a uno", eso
    // significaría que al dormirse el MCU se le encienden los relojes a
    // periféricos que el firmware nunca habilitó.
    //
    // Aquí se modela la puerta como ENR **AND** LPENR, que es la lectura
    // física: el gate de Run está aguas arriba en el árbol de reloj, y el de
    // Sleep lo estrecha todavía más. Los dos criterios solo se diferencian en
    // el caso EN=0 y LPEN=1, que en silicio únicamente se puede observar
    // metiendo un maestro ajeno a la CPU -el DMA o el AHB-AP del depurador- en
    // un periférico que nadie habilitó.
    //
    // Lo que NO cambia es para lo que sirven los LPENR: limpiar el LPEN de un
    // periférico habilitado le quita el reloj al dormir, y eso se mide aquí en
    // el consumo (T100).
    // -----------------------------------------------------------------------
    uint32_t grp_gate(uint8_t g) const {
        // Lo que decide NO es el modo arquitectonico sino si de verdad hay
        // relojes: con los bits de DBGMCU puestos, el MCU esta en Stop o en
        // Standby y sin embargo el arbol sigue girando, y entonces los
        // perifericos siguen recibiendo su reloj -atenuado por los LPENR, que
        // es lo que corresponde a un MCU dormido- [IR, §13.9, §14.3].
        if (relojes_parados_)        return 0;
        if (modo_lp_ != LP_RUN)      return grp_enr(g) & grp_lpenr(g);
        return grp_enr(g);
    }
    void refresh_periph(bool sys_reset_active) {
        unsigned n = 0;
        for (unsigned i = 0; i < P_COUNT; ++i) {
            o_pcen_[i]  = false;
            o_prstn_[i] = !sys_reset_active;
        }
        for (unsigned i = 0; i < RCC_BITMAP_N; ++i) {
            const RccBitMap& e = RCC_BITMAP[i];
            const bool en  = (grp_gate(e.grp) >> e.bit) & 1u;
            const bool rst = (grp_rstr(e.grp) >> e.bit) & 1u;
            o_pcen_[e.id]  = en && !sys_reset_active;
            o_prstn_[e.id] = !rst && !sys_reset_active;
            if (o_pcen_[e.id]) ++n;
        }
        o_periph_on_ = n;
        publish();
    }

    // Publicación de los puertos de salida desde un único proceso.
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void publish_proc() {
        sys_rst_n.write(o_sys_rst_n_);
        bkp_rst_n.write(o_bkp_rst_n_);
        drive_nrst_low.write(o_drive_nrst_);
        nmi_css.write(o_nmi_css_);
        irq.write(o_irq_);
        periph_on.write(o_periph_on_);
        for (unsigned i = 0; i < P_COUNT; ++i) {
            periph_clk_en[i].write(o_pcen_[i]);
            periph_rst_n[i].write(o_prstn_[i]);
        }
    }

    // -----------------------------------------------------------------------
    // Procesos
    // -----------------------------------------------------------------------
    void lsi_mirror_proc() { lsi_clk.write(s_lsi_clk.read()); lsi_hz.write(s_lsi_hz.read()); }
    void iwdg_lsi_proc() { apply_osc_controls(); update_clocks(); }

    // Vigilancia de las fuentes de reset [IR, §4.1.1]
    void rst_src_proc() {
        uint32_t flag = 0;
        // Un fallo de alimentación se atribuye al BOR o al POR/PDR según quién
        // lo haya detectado [IR, §4.10].
        if (!por_ok.read())
            flag |= bor_rst.read() ? (1u << 25) : (1u << 27);  // BORRSTF/PORRSTF
        // El nivel bajo del pin solo cuenta como reset externo si no somos
        // nosotros quienes lo estamos forzando (NRST es open-drain, el reset
        // interno también lo lleva a 0) [IR, §4.1.1].
        if (!nrst_in_n.read() && !driving_nrst_) flag |= (1u << 26);   // PINRSTF
        if (wwdg_rst_req.read())   flag |= (1u << 30);   // WWDGRSTF
        if (iwdg_rst_req.read())   flag |= (1u << 29);   // IWDGRSTF
        if (sysresetreq.read())    flag |= (1u << 28);   // SFTRSTF
        if (flag) { pending_flags_ |= flag; rst_req_ev_.notify(sc_core::SC_ZERO_TIME); }
        // La entrada en Standby apaga el dominio de 1,2 V: para el modelo es un
        // reset que se MANTIENE hasta que el PWR encuentra un despertador. No
        // deja flag en RCC_CSR -de eso se encarga SBF en PWR_CSR-, que es lo
        // que distingue esta salida de un POR de verdad [IR, §14.5.3].
        const bool sb = standby_req.read();
        if (sb && !standby_prev_) rst_req_ev_.notify(sc_core::SC_ZERO_TIME);
        standby_prev_ = sb;
    }

    void reset_ctrl_proc() {
        for (;;) {
            // ---------------- reset activo ----------------------------------
            csr_ |= pending_flags_;  pending_flags_ = 0;
            reset_registers(false);            // CSR y BDCR sobreviven
            o_sys_rst_n_ = false;
            // El DOMINIO DE BACKUP no se va con un reset de sistema. Solo lo
            // borran BDRST o la pérdida de la alimentación: esa es la razón de
            // que existan el RTC, la BKPSRAM y los veinte registros de backup
            // [IR, §4.1.3, §12.9.1]. Un NRST, un perro guardián o un
            // SYSRESETREQ los dejan intactos.
            if (!por_ok.read()) o_bkp_rst_n_ = false;
            bdrst_       = false;
            driving_nrst_ = true;
            o_drive_nrst_ = true;
            refresh_periph(true);              // publica también los resets
            update_clocks();

            wait(t_rst_pulse);                 // pulso mínimo de 20 us
            o_drive_nrst_ = false;  publish();
            wait(sc_core::sc_time(1, sc_core::SC_NS));   // el pad se recupera
            driving_nrst_ = false;
            while (!por_ok.read() || !nrst_in_n.read() || standby_req.read())
                wait(por_ok.value_changed_event() | nrst_in_n.value_changed_event() |
                     standby_req.value_changed_event());
            wait(t_rst_release);

            // ---------------- salida de reset --------------------------------
            pending_flags_ = 0;
            css_tripped_ = false;              // el CSS se rearma con el reset
            hse.clear_failed();
            o_sys_rst_n_ = true;
            o_bkp_rst_n_ = true;
            refresh_periph(false);
            apply_osc_controls();              // HSION=1 tras reset [IR, §4.5.1]
            update_clocks();

            wait(rst_req_ev_);                 // hasta la próxima causa de reset
        }
    }

    // -----------------------------------------------------------------------
    // Bajo consumo: lo que el RCC tiene que hacer en cada modo [IR, §14]
    // -----------------------------------------------------------------------
    void lp_proc() {
        const uint8_t m  = lp_mode.read();
        const bool    st = stop_req.read() || standby_req.read();
        const bool cambia_gate = (m != modo_lp_);
        modo_lp_ = m;
        if (st != relojes_parados_) {
            relojes_parados_ = st;
            if (st) {
                // Lo que hace el hardware al entrar: apaga HSE, los PLL y el
                // CSS, y deja seleccionado el HSI para la vuelta. Por eso al
                // despertar de Stop se corre a 16 MHz y hay que reprogramar el
                // PLL: no es un olvido del firmware, es el silicio [IR, §14.4.3].
                cr_   &= ~((1u << 16) | (1u << 19) | (1u << 24) | (1u << 26));
                cr_   |= 1u;                    // HSION
                cfgr_ &= ~0x3u;                 // SW = HSI
            }
            apply_osc_controls();
            update_clocks();
        }
        // El reparto de relojes depende del modo Y de si estan parados, asi que
        // se rehace siempre que cambie cualquiera de los dos.
        (void)cambia_gate;
        refresh_periph(!o_sys_rst_n_);
    }

    void clock_tree_proc() {
        // Los siete eventos del árbol de reloj no cambian nunca: son los de los
        // seis osciladores/PLL y el aviso de escritura en un registro. Armar la
        // lista aquí, una vez, en vez de dentro del bucle, ahorra una reserva
        // por cada despertar —y este proceso despierta en cada escritura al
        // RCC— y evita que la lista se pierda al terminar la simulación: un
        // SC_THREAD suspendido en `wait()` no desenrolla su pila, así que sus
        // locales no se destruyen nunca.
        arbol_ev_ |= hsi.state_event() | hse.state_event() | lsi.state_event()
                   | lse.state_event() | pll.state_event() | plli2s.state_event()
                   | clk_ev_;
        for (;;) {
            wait(arbol_ev_);
            check_css();                       // fallo del HSE [IR, §4.2]
            apply_osc_controls();              // la referencia del PLL cambia
            update_clocks();
        }
    }

    // -----------------------------------------------------------------------
    // Clock Security System [IR, §4.2]
    //
    // Con CSSON=1, si el HSE falla el hardware: apaga HSEON/HSERDY, conmuta
    // SYSCLK a HSI (también cuando el PLL alimentado por HSE era la fuente),
    // activa CSSF en RCC_CIR y genera una NMI. El flag y la NMI se limpian
    // escribiendo CSSC. El HSE no se puede volver a arrancar hasta un reset del
    // sistema, que es lo que hace el silicio.
    // -----------------------------------------------------------------------
    void check_css() {
        const bool csson = (cr_ >> 19) & 1u;
        if (!csson || !hse.failed() || css_tripped_) return;
        css_tripped_ = true;
        hse.enable(false);
        cr_ &= ~((1u << 16) | (1u << 19));          // HSEON, CSSON
        // La fuente de SYSCLK vuelve a HSI y el PLL se apaga si dependía del HSE
        if (sws_ == 1 || (sws_ == 2 && ((pllcfgr_ >> 22) & 1u))) {
            cfgr_ &= ~0x3u;                          // SW = HSI
            if ((pllcfgr_ >> 22) & 1u) { pll.enable(false); cr_ &= ~(1u << 24); }
            hsi.enable(true); cr_ |= 1u;
        }
        cir_ |= (1u << 7);                           // CSSF
        o_nmi_css_ = true;                           // NMI (no enmascarable)
        publish();
        css_nmi_ev_.notify(sc_core::SC_ZERO_TIME);
        SC_REPORT_WARNING("rcc", "fallo del HSE detectado por el CSS: SYSCLK -> HSI [IR, 4.2]");
    }
    // La NMI del CSS es un pulso: el flag CSSF permanece hasta que se escribe
    // CSSC, pero la línea debe volver a 0 para poder señalar futuros eventos.
    void css_nmi_proc() {
        for (;;) {
            wait(css_nmi_ev_);
            wait(sc_core::sc_time(1, sc_core::SC_NS));
            o_nmi_css_ = false;
            publish();
        }
    }

    // -----------------------------------------------------------------------
    // Modulación de espectro ensanchado del PLL principal [IR, §4.11.1]
    //
    // A partir de MODPER e INCSTEP se recuperan las magnitudes físicas con las
    // fórmulas de programación del manual de referencia:
    //   MODPER  = round(f_PLL_IN / (4 * f_Mod))
    //   INCSTEP = round(((2^15-1) * md * PLLN) / (100 * 5 * MODPER))
    // El modelo calcula f_Mod y la profundidad md, y aplica el desplazamiento
    // MEDIO de frecuencia que produce la modulación (0 en center spread,
    // -md/2 en down spread). No se modela el jitter instantáneo: este modelo no
    // representa fase ciclo a ciclo y hacerlo multiplicaría el número de
    // eventos sin que ningún consumidor del modelo pueda observarlo.
    // -----------------------------------------------------------------------
    double ss_mod_hz() const {
        const unsigned modper = sscgr_ & 0x1FFFu;
        const double f_in = pll_ref_hz_;
        return (modper && f_in > 0.0) ? f_in / (4.0 * modper) : 0.0;
    }
    double ss_depth_pct() const {
        const unsigned modper  = sscgr_ & 0x1FFFu;
        const unsigned incstep = (sscgr_ >> 13) & 0x7FFFu;
        const unsigned plln    = (pllcfgr_ >> 6) & 0x1FFu;
        if (!modper || !plln) return 0.0;
        return double(incstep) * 100.0 * 5.0 * double(modper) / (32767.0 * double(plln));
    }
    bool ss_enabled()    const { return (sscgr_ >> 31) & 1u; }
    bool ss_down_spread() const { return (sscgr_ >> 30) & 1u; }
    double ss_factor() const {
        if (!ss_enabled()) return 1.0;
        return ss_down_spread() ? (1.0 - 0.005 * ss_depth_pct()) : 1.0;
    }

    uint32_t pending_flags_ = 0;
    double   pll_ref_hz_    = 0.0;     // referencia de entrada al PLL (SSCGR)
    bool     css_tripped_   = false;   // el CSS ya ha actuado (hasta el reset)
    sc_core::sc_event css_nmi_ev_;
    bool     driving_nrst_  = false;
    bool     bdrst_         = false;
    // Estado deseado de los puertos de salida (lo escribe publish_proc)
    bool o_sys_rst_n_ = false, o_bkp_rst_n_ = false, o_drive_nrst_ = true;
    bool o_irq_ = false, o_nmi_css_ = false;
    bool o_pcen_[P_COUNT] = {}, o_prstn_[P_COUNT] = {};
    unsigned o_periph_on_ = 0;
    // Estado de bajo consumo visto desde el RCC
    uint8_t  modo_lp_ = LP_RUN;
    bool     relojes_parados_ = false;   // Stop o Standby (sin DBGMCU)
    bool     standby_prev_ = false;
    sc_core::sc_event pub_ev_;
    // Frecuencias de los generadores sin puerto externo (SysTick ext, MCO1/2)
    sc_core::sc_signal<double> s_stk_hz_{"s_stk_hz"},
                               s_mco1_hz_{"s_mco1_hz"}, s_mco2_hz_{"s_mco2_hz"};

public:
    // Señales internas osciladores -> árbol (bindeadas en bind_internal_)
    sc_core::sc_signal<bool>   s_hsi_rdy{"s_hsi_rdy"}, s_hse_rdy{"s_hse_rdy"},
                               s_lsi_rdy{"s_lsi_rdy"}, s_lse_rdy{"s_lse_rdy"},
                               s_pll_rdy{"s_pll_rdy"}, s_plli2s_rdy{"s_plli2s_rdy"};
    sc_core::sc_signal<bool>   s_hsi_clk{"s_hsi_clk"}, s_hse_clk{"s_hse_clk"},
                               s_lsi_clk{"s_lsi_clk"}, s_lse_clk{"s_lse_clk"},
                               s_pllp_clk{"s_pllp_clk"},
                               s_i2s_clk{"s_i2s_clk"}, s_i2sq_clk{"s_i2sq_clk"};
    sc_core::sc_signal<double> s_hsi_hz{"s_hsi_hz"}, s_hse_hz{"s_hse_hz"},
                               s_lsi_hz{"s_lsi_hz"}, s_lse_hz{"s_lse_hz"},
                               s_pllp_hz{"s_pllp_hz"},
                               s_i2s_hz{"s_i2s_hz"}, s_i2sq_hz{"s_i2sq_hz"};
};

// ---------------------------------------------------------------------------
inline void Rcc::bind_internal_() {
    hsi.ready(s_hsi_rdy); hsi.clk(s_hsi_clk); hsi.freq_hz(s_hsi_hz);
    hse.ready(s_hse_rdy); hse.clk(s_hse_clk); hse.freq_hz(s_hse_hz);
    lsi.ready(s_lsi_rdy); lsi.clk(s_lsi_clk); lsi.freq_hz(s_lsi_hz);
    lse.ready(s_lse_rdy); lse.clk(s_lse_clk); lse.freq_hz(s_lse_hz);
    // La salida P del PLL principal es SYSCLK (a través del mux SW, que el
    // árbol resuelve por frecuencia) y la Q es directamente PLL48CK [IR, §4.3].
    pll.ready(s_pll_rdy); pll.clk_p(s_pllp_clk); pll.clk_p_hz(s_pllp_hz);
    pll.clk_q(pll48ck);   pll.clk_q_hz(pll48ck_hz);
    plli2s.ready(s_plli2s_rdy); plli2s.clk_p(s_i2s_clk); plli2s.clk_p_hz(s_i2s_hz);
    plli2s.clk_q(s_i2sq_clk);   plli2s.clk_q_hz(s_i2sq_hz);
    // Generadores del árbol -> puertos de salida del RCC
    g_hclk_.clk(hclk);         g_hclk_.freq_hz(hclk_hz);
    g_pclk1_.clk(pclk1);       g_pclk1_.freq_hz(pclk1_hz);
    g_pclk2_.clk(pclk2);       g_pclk2_.freq_hz(pclk2_hz);
    g_timclk1_.clk(timclk1);   g_timclk1_.freq_hz(timclk1_hz);
    g_timclk2_.clk(timclk2);   g_timclk2_.freq_hz(timclk2_hz);
    g_rtcclk_.clk(rtcclk);     g_rtcclk_.freq_hz(rtcclk_hz);
    g_stk_.clk(systick_ext);   g_stk_.freq_hz(s_stk_hz_);
    g_mco1_.clk(mco1_sig);     g_mco1_.freq_hz(s_mco1_hz_);
    g_mco2_.clk(mco2_sig);     g_mco2_.freq_hz(s_mco2_hz_);
}

// ---------------------------------------------------------------------------
// Lectura de registros [IR, §4.5-4.12]
// ---------------------------------------------------------------------------
inline uint32_t Rcc::reg_read(uint32_t off) {
    switch (off) {
        case R_CR: {
            uint32_t v = cr_ & ~0x0A020002u;      // limpiar los RDY calculados
            if (hsi.is_ready())    v |= (1u << 1);
            if (hse.is_ready())    v |= (1u << 17);
            if (pll.is_ready())    v |= (1u << 25);
            if (plli2s.is_ready()) v |= (1u << 27);
            return v;
        }
        case R_PLLCFGR:  return pllcfgr_;
        case R_CFGR:     return (cfgr_ & ~0xCu) | ((sws_ & 3u) << 2);
        case R_CIR:      return cir_ & 0x0000FEFEu;   // los bits C son solo escritura
        case R_AHB1RSTR: return ahb1rstr_;
        case R_AHB2RSTR: return ahb2rstr_;
        case R_AHB3RSTR: return ahb3rstr_;
        case R_APB1RSTR: return apb1rstr_;
        case R_APB2RSTR: return apb2rstr_;
        case R_AHB1ENR:  return ahb1enr_;
        case R_AHB2ENR:  return ahb2enr_;
        case R_AHB3ENR:  return ahb3enr_;
        case R_APB1ENR:  return apb1enr_;
        case R_APB2ENR:  return apb2enr_;
        case R_AHB1LPENR:return ahb1lpenr_;
        case R_AHB2LPENR:return ahb2lpenr_;
        case R_AHB3LPENR:return ahb3lpenr_;
        case R_APB1LPENR:return apb1lpenr_;
        case R_APB2LPENR:return apb2lpenr_;
        case R_BDCR:     return (bdcr_ & ~2u) | (lse.is_ready() ? 2u : 0u);
        case R_CSR:      return (csr_ & ~2u) | (lsi.is_ready() ? 2u : 0u);
        case R_SSCGR:    return sscgr_;
        case R_PLLI2SCFGR: return plli2scfgr_;
        default:         return 0;
    }
}

// ---------------------------------------------------------------------------
// Escritura de registros
// ---------------------------------------------------------------------------
inline void Rcc::reg_write(uint32_t off, uint32_t v, uint32_t be) {
    // Combinación byte a byte sobre el valor actual (accesos de 8/16 bits)
    if (be != 0xFu) {
        uint32_t cur = reg_read(off), mask = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) mask |= 0xFFu << (8 * b);
        v = (cur & ~mask) | (v & mask);
    }
    bool clocks = false, periph = false;

    switch (off) {
        case R_CR: {
            // Bits rw: PLLI2SON(26), PLLON(24), CSSON(19), HSEBYP(18), HSEON(16),
            // HSITRIM[7:3], HSION(0). Los RDY y HSICAL son de solo lectura.
            const uint32_t wmask = 0x050D00F9u;
            // No se puede apagar la fuente que alimenta SYSCLK [IR, §4.5.1]
            uint32_t nv = (cr_ & ~wmask) | (v & wmask);
            if (sws_ == 0) nv |= 1u;                    // HSI en uso
            if (sws_ == 1) nv |= (1u << 16);            // HSE en uso
            if (sws_ == 2) nv |= (1u << 24);            // PLL en uso
            cr_ = nv;
            clocks = true;
            break;
        }
        case R_PLLCFGR:
            if (!pll.enabled()) {   // solo modificable con el PLL apagado
                pllcfgr_ = (pllcfgr_ & 0xF0BC8000u) | (v & 0x0F437FFFu);
                clocks = true;
            }
            break;
        case R_CFGR:
            cfgr_ = (cfgr_ & 0x0000000Cu) | (v & 0xFFFFFFF3u);
            clocks = true;
            break;
        case R_CIR: {
            const uint32_t ie   = v & 0x00007E00u;      // xxxRDYIE (bits 9..14)
            const uint32_t clr  = (v >> 16) & 0x000000FEu; // bits C (17..23)
            cir_ = (cir_ & ~0x00007E00u) | ie;
            cir_ &= ~clr;                               // limpiar flags
            if (v & (1u << 23)) cir_ &= ~(1u << 7);     // CSSC -> CSSF
            update_cir_flags();
            break;
        }
        case R_AHB1RSTR: ahb1rstr_ = v & 0x22600FFFu; periph = true; break;
        case R_AHB2RSTR: ahb2rstr_ = v & 0x000000F1u; periph = true; break;
        case R_AHB3RSTR: ahb3rstr_ = v & 0x00000001u; periph = true; break;
        case R_APB1RSTR: apb1rstr_ = v & 0x36FEC9FFu; periph = true; break;
        case R_APB2RSTR: apb2rstr_ = v & 0x00075F33u; periph = true; break;
        case R_AHB1ENR:  ahb1enr_  = v & 0x7E6791FFu; periph = true; break;
        case R_AHB2ENR:  ahb2enr_  = v & 0x000000F1u; periph = true; break;
        case R_AHB3ENR:  ahb3enr_  = v & 0x00000001u; periph = true; break;
        case R_APB1ENR:  apb1enr_  = v & 0x36FEC9FFu; periph = true; break;
        case R_APB2ENR:  apb2enr_  = v & 0x00075F33u; periph = true; break;
        // Los LPENR mandan mientras el MCU duerme, asi que tocarlos tambien
        // reparte relojes: es la palanca para gastar menos en Sleep [IR, §4.8].
        case R_AHB1LPENR:ahb1lpenr_= v & 0x7E6791FFu; periph = true; break;
        case R_AHB2LPENR:ahb2lpenr_= v & 0x000000F1u; periph = true; break;
        case R_AHB3LPENR:ahb3lpenr_= v & 0x00000001u; periph = true; break;
        case R_APB1LPENR:apb1lpenr_= v & 0x36FEC9FFu; periph = true; break;
        case R_APB2LPENR:apb2lpenr_= v & 0x00075F33u; periph = true; break;
        case R_BDCR:
            // BDRST es un nivel: mientras está a 1 el dominio de backup (RTC y
            // el propio RCC_BDCR) permanece en reset [IR, §4.1.3, §4.9].
            bdrst_ = ((v >> 16) & 1u) != 0;
            bdcr_  = bdrst_ ? 0u : (v & 0x0000830Du);
            o_bkp_rst_n_ = !bdrst_;
            publish();
            clocks = true;
            break;
        case R_CSR:
            if (v & (1u << 24)) csr_ &= ~0xFE000000u;   // RMVF limpia los flags
            csr_ = (csr_ & ~1u) | (v & 1u);             // LSION
            clocks = true;
            break;
        case R_SSCGR:
            // La modulación solo debe programarse con el PLL apagado; escribirla
            // con el PLL en marcha no tiene efecto definido en el silicio.
            if (pll.enabled())
                SC_REPORT_WARNING("rcc",
                    "RCC_SSCGR escrito con el PLL activo [IR, 4.11.1]");
            sscgr_ = v & 0xCFFFFFFFu;
            clocks = true;
            break;
        case R_PLLI2SCFGR:
            if (!plli2s.enabled()) {
                plli2scfgr_ = v & 0x70007FC0u;
                clocks = true;
            }
            break;
        default: break;
    }
    if (periph) refresh_periph(false);
    if (clocks) { apply_osc_controls(); update_clocks(); }
}

} // namespace stm32
#endif // STM32_RCC_RCC_H
