// =============================================================================
// rcc_f3.h — El RCC del STM32F303 (fase S1)
//
// El de la familia F0/F1 ampliado, y no el del F4: un PLL con un multiplicador
// entero (×2…×16) en vez de M/N/P/Q, una fuente de PLL que es HSI/2 o
// HSE/PREDIV, y dos registros que el F1 no tiene, CFGR2 (el PREDIV y los
// preescaladores de los ADC) y CFGR3 (de dónde comen las USART, los I²C y los
// TIM1/TIM8) [CMSIS stm32f303xc.h, RCC_TypeDef y RCC_*_Pos].
//
//   0x00 CR       HSION/RDY/TRIM/CAL, HSEON/RDY/BYP, CSSON, PLLON/RDY
//   0x04 CFGR     SW/SWS, HPRE, PPRE1, PPRE2, PLLSRC, PLLXTPRE, PLLMUL, USBPRE,
//                 I2SSRC, MCO, MCOF
//   0x08 CIR      xxxRDYF/IE/C y CSSF/CSSC
//   0x0C APB2RSTR 0x10 APB1RSTR 0x14 AHBENR 0x18 APB2ENR 0x1C APB1ENR
//   0x20 BDCR     LSE, RTC (dominio de respaldo: con PWR_CR.DBP)
//   0x24 CSR      LSI y las banderas de la causa del reset
//   0x28 AHBRSTR  0x2C CFGR2  0x30 CFGR3
//
// Los osciladores son los de siempre (`rcc/osc_pll.h`): el HSE con su cristal
// —o lo que haya en PF0, que en la Discovery es el MCO del ST-LINK— y su modo
// *bypass*, que mide la frecuencia que llega. El PLL es propio, porque el del
// F4 avisaría con los rangos del F4.
//
// LOS AVISOS, CON NOMBRE Y APELLIDOS [doc/analisis_stm32f303.md, §2.5]. El
// silicio no comprueba nada de esto y falla en silencio; el modelo lo dice una
// vez por situación:
//   * SYSCLK, HCLK o PCLK2 por encima de 72 MHz, PCLK1 por encima de 36;
//   * la salida del PLL fuera de 16-72 MHz y su entrada fuera de 1-24 MHz;
//   * el USB encendido sin sus 48 MHz exactos;
//   * el HSE fuera de 4-32 MHz.
// La latencia de la Flash la vigila la Flash, que es quien la conoce
// (`mem/flash_f1.h`).
//
// LO QUE QUEDA FUERA (y `SocF3::limitaciones()` lo dice): el reloj del I²S
// por I2S_CKIN, y el CSS sobre el LSE, que el F303 no tiene.
// =============================================================================
#ifndef STM32_RCC_RCC_F3_H
#define STM32_RCC_RCC_F3_H

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include "../common/periph_base.h"
#include "../common/clock_gen.h"
#include "osc_pll.h"

namespace stm32 {

// Los relojes y resets por periférico del F303 [CMSIS: RCC_AHBENR_*, etc.]
enum PerifF3 : unsigned {
    // AHB
    F3P_DMA1, F3P_DMA2, F3P_SRAM, F3P_FLITF, F3P_CRC,
    F3P_GPIOA, F3P_GPIOB, F3P_GPIOC, F3P_GPIOD, F3P_GPIOE, F3P_GPIOF,
    F3P_TSC, F3P_ADC12, F3P_ADC34,
    // APB2
    F3P_SYSCFG, F3P_TIM1, F3P_SPI1, F3P_TIM8, F3P_USART1, F3P_TIM15, F3P_TIM16, F3P_TIM17,
    // APB1
    F3P_TIM2, F3P_TIM3, F3P_TIM4, F3P_TIM6, F3P_TIM7, F3P_WWDG, F3P_SPI2, F3P_SPI3,
    F3P_USART2, F3P_USART3, F3P_UART4, F3P_UART5, F3P_I2C1, F3P_I2C2, F3P_USB, F3P_CAN,
    F3P_PWR, F3P_DAC1,
    F3P_COUNT
};

// (grupo, bit de ENR, bit de RSTR o -1, periférico). Grupo: 0 AHB, 1 APB2, 2 APB1.
struct BitRccF3 { uint8_t grp; uint8_t bit; int8_t rst; uint8_t id; const char* nombre; };
inline constexpr BitRccF3 RCC_F3_BITS[] = {
    {0,  0, -1, F3P_DMA1, "DMA1"},  {0,  1, -1, F3P_DMA2, "DMA2"},
    {0,  2, -1, F3P_SRAM, "SRAM"},  {0,  4, -1, F3P_FLITF, "FLITF"},
    {0,  6, -1, F3P_CRC, "CRC"},
    {0, 17, 17, F3P_GPIOA, "GPIOA"}, {0, 18, 18, F3P_GPIOB, "GPIOB"},
    {0, 19, 19, F3P_GPIOC, "GPIOC"}, {0, 20, 20, F3P_GPIOD, "GPIOD"},
    {0, 21, 21, F3P_GPIOE, "GPIOE"}, {0, 22, 22, F3P_GPIOF, "GPIOF"},
    {0, 24, 24, F3P_TSC, "TSC"}, {0, 28, 28, F3P_ADC12, "ADC12"}, {0, 29, 29, F3P_ADC34, "ADC34"},
    {1,  0,  0, F3P_SYSCFG, "SYSCFG"}, {1, 11, 11, F3P_TIM1, "TIM1"},
    {1, 12, 12, F3P_SPI1, "SPI1"}, {1, 13, 13, F3P_TIM8, "TIM8"},
    {1, 14, 14, F3P_USART1, "USART1"}, {1, 16, 16, F3P_TIM15, "TIM15"},
    {1, 17, 17, F3P_TIM16, "TIM16"}, {1, 18, 18, F3P_TIM17, "TIM17"},
    {2,  0,  0, F3P_TIM2, "TIM2"}, {2,  1,  1, F3P_TIM3, "TIM3"}, {2,  2,  2, F3P_TIM4, "TIM4"},
    {2,  4,  4, F3P_TIM6, "TIM6"}, {2,  5,  5, F3P_TIM7, "TIM7"}, {2, 11, 11, F3P_WWDG, "WWDG"},
    {2, 14, 14, F3P_SPI2, "SPI2"}, {2, 15, 15, F3P_SPI3, "SPI3"},
    {2, 17, 17, F3P_USART2, "USART2"}, {2, 18, 18, F3P_USART3, "USART3"},
    {2, 19, 19, F3P_UART4, "UART4"}, {2, 20, 20, F3P_UART5, "UART5"},
    {2, 21, 21, F3P_I2C1, "I2C1"}, {2, 22, 22, F3P_I2C2, "I2C2"},
    {2, 23, 23, F3P_USB, "USB"}, {2, 25, 25, F3P_CAN, "CAN"},
    {2, 28, 28, F3P_PWR, "PWR"}, {2, 29, 29, F3P_DAC1, "DAC1"},
};
inline constexpr unsigned RCC_F3_N = sizeof(RCC_F3_BITS) / sizeof(RCC_F3_BITS[0]);

class RccF3 : public BusSlave {
public:
    enum : uint32_t {
        R_CR = 0x00, R_CFGR = 0x04, R_CIR = 0x08, R_APB2RSTR = 0x0C,
        R_APB1RSTR = 0x10, R_AHBENR = 0x14, R_APB2ENR = 0x18, R_APB1ENR = 0x1C,
        R_BDCR = 0x20, R_CSR = 0x24, R_AHBRSTR = 0x28, R_CFGR2 = 0x2C, R_CFGR3 = 0x30
    };
    // Banderas de RCC_CSR [CMSIS]
    static constexpr uint32_t CSR_V18PWRRSTF = 1u << 23, CSR_RMVF = 1u << 24,
        CSR_OBLRSTF = 1u << 25, CSR_PINRSTF = 1u << 26, CSR_PORRSTF = 1u << 27,
        CSR_SFTRSTF = 1u << 28, CSR_IWDGRSTF = 1u << 29, CSR_WWDGRSTF = 1u << 30,
        CSR_LPWRRSTF = 1u << 31;

    // ---- Relojes hacia el SoC -------------------------------------------------
    sc_core::sc_out<bool>   hclk{"hclk"};       sc_core::sc_out<double> hclk_hz{"hclk_hz"};
    sc_core::sc_out<bool>   pclk1{"pclk1"};     sc_core::sc_out<double> pclk1_hz{"pclk1_hz"};
    sc_core::sc_out<bool>   pclk2{"pclk2"};     sc_core::sc_out<double> pclk2_hz{"pclk2_hz"};
    sc_core::sc_out<bool>   timclk1{"timclk1"}; sc_core::sc_out<double> timclk1_hz{"timclk1_hz"};
    sc_core::sc_out<bool>   timclk2{"timclk2"}; sc_core::sc_out<double> timclk2_hz{"timclk2_hz"};
    sc_core::sc_out<bool>   systick_ext{"systick_ext"};    // HCLK/8
    sc_core::sc_out<bool>   rtcclk{"rtcclk"};   sc_core::sc_out<double> rtcclk_hz{"rtcclk_hz"};
    sc_core::sc_out<bool>   lsi_clk{"lsi_clk"}; sc_core::sc_out<double> lsi_hz{"lsi_hz"};

    // ---- Resets -----------------------------------------------------------------
    sc_core::sc_out<bool> sys_rst_n{"sys_rst_n"};
    sc_core::sc_out<bool> bkp_rst_n{"bkp_rst_n"};
    sc_core::sc_out<bool> drive_nrst_low{"drive_nrst_low"};
    sc_core::sc_in<bool>  por_ok{"por_ok"};
    sc_core::sc_in<bool>  nrst_in_n{"nrst_in_n"};
    sc_core::sc_in<bool>  sysresetreq{"sysresetreq"};
    sc_core::sc_in<bool>  iwdg_rst_req{"iwdg_rst_req"};
    sc_core::sc_in<bool>  wwdg_rst_req{"wwdg_rst_req"};
    sc_core::sc_in<bool>  dbp{"dbp"};                     // PWR_CR.DBP
    sc_core::sc_out<bool> nmi_css{"nmi_css"};
    sc_core::sc_out<bool> irq{"irq"};                     // IRQ 5

    // El MCO (PA8, AF0)
    sc_core::sc_signal<bool> mco_sig{"mco_sig"};

    // Los relojes y resets de cada periférico. Son señales del RCC: el SoC ata
    // a ellas los `clk_en`/`rst_n` de cada bloque.
    sc_core::sc_vector<sc_core::sc_signal<bool>> s_en, s_rst_n;
    const bool* clk_en_ptr(unsigned id) const { return &o_en_[id]; }

    Oscillator hsi{"hsi"}, hse{"hse"}, lsi{"lsi"}, lse{"lse"};

    sc_core::sc_time t_rst_pulse{20, sc_core::SC_US};
    sc_core::sc_time t_rst_release{20, sc_core::SC_US};
    double t_pll_lock_s = 200e-6;       // [1F: DS9118, t_LOCK]

    // Al ENTRAR en un reset de alimentación o de OBL_LAUNCH: es cuando el chip
    // vuelve a cargar los bytes de opción (lo hace el SoC con la Flash)
    std::function<void()> al_cargar_opciones;

    explicit RccF3(sc_core::sc_module_name nm)
        : BusSlave(nm, 0x40021000u, 0x400),
          s_en("s_en", F3P_COUNT), s_rst_n("s_rst_n", F3P_COUNT),
          g_hclk_("g_hclk"), g_pclk1_("g_pclk1"), g_pclk2_("g_pclk2"),
          g_tim1_("g_timclk1"), g_tim2_("g_timclk2"), g_stk_("g_stk"),
          g_rtc_("g_rtc"), g_mco_("g_mco"), g_lsi_("g_lsi") {
        SC_HAS_PROCESS(RccF3);
        hsi.nominal_hz = 8e6;      hsi.t_startup_s = 2e-6;      // [1F: DS9118]
        hse.nominal_hz = 8e6;      hse.t_startup_s = 2e-3;
        lsi.nominal_hz = 40e3;     lsi.t_startup_s = 85e-6;     // [1F: DS9118]
        lse.nominal_hz = 32768.0;  lse.t_startup_s = 2.0;
        hse.needs_source = true;
        lse.needs_source = true;
        g_hclk_.clk(hclk);       g_hclk_.freq_hz(hclk_hz);
        g_pclk1_.clk(pclk1);     g_pclk1_.freq_hz(pclk1_hz);
        g_pclk2_.clk(pclk2);     g_pclk2_.freq_hz(pclk2_hz);
        g_tim1_.clk(timclk1);    g_tim1_.freq_hz(timclk1_hz);
        g_tim2_.clk(timclk2);    g_tim2_.freq_hz(timclk2_hz);
        g_stk_.clk(systick_ext); g_stk_.freq_hz(s_stk_hz_);
        g_rtc_.clk(rtcclk);      g_rtc_.freq_hz(rtcclk_hz);
        g_mco_.clk(mco_sig);     g_mco_.freq_hz(s_mco_hz_);
        g_lsi_.clk(lsi_clk);     g_lsi_.freq_hz(lsi_hz);
        for (Oscillator* o : {&hsi, &hse, &lsi, &lse}) {
            sc_core::sc_signal<bool>* r = new sc_core::sc_signal<bool>();
            sc_core::sc_signal<bool>* c = new sc_core::sc_signal<bool>();
            sc_core::sc_signal<double>* f = new sc_core::sc_signal<double>();
            o->ready(*r); o->clk(*c); o->freq_hz(*f);
            propias_b_.push_back(r); propias_b_.push_back(c); propias_d_.push_back(f);
        }
        SC_THREAD(reset_proc);
        SC_THREAD(arbol_proc);
        SC_THREAD(pll_proc);
        SC_THREAD(css_nmi_proc);
        SC_METHOD(publica_proc); sensitive << pub_ev_; dont_initialize();
        SC_METHOD(revisa_topes); sensitive << topes_ev_; dont_initialize();
        SC_METHOD(fuentes_proc);
        sensitive << por_ok << nrst_in_n << sysresetreq << iwdg_rst_req << wwdg_rst_req;
        dont_initialize();
        valores_reset(true);
        csr_ = CSR_PORRSTF | CSR_PINRSTF;
    }
    ~RccF3() override {
        for (auto* p : propias_b_) delete p;
        for (auto* p : propias_d_) delete p;
    }

    // ---- Para el SoC, la Flash y el banco ------------------------------------------
    double f_sysclk() const { return f_sys_; }
    double f_hclk()   const { return f_hclk_; }
    double f_pclk1()  const { return f_pclk1_; }
    double f_pclk2()  const { return f_pclk2_; }
    double f_pll()    const { return pll_listo_ ? f_pll_ : 0.0; }
    double f_mco()    const { return g_mco_.freq(); }
    double f_rtc()    const { return f_rtc_; }
    unsigned sws()    const { return sws_; }
    // Los relojes de núcleo de CFGR3, CFGR2 y CFGR (los consumen los
    // periféricos de S3 y S5; aquí ya son números correctos)
    double f_usart(unsigned n) const;   // 1..5
    double f_i2c(unsigned n) const;     // 1, 2
    double f_tim18(unsigned n) const;   // 1 o 8
    double f_adc12() const { return f_adc(pre_adc12()); }
    double f_adc34() const { return f_adc(pre_adc34()); }
    double f_usb() const {
        const double p = f_pll();
        return ((cfgr_ >> 22) & 1u) ? p : p / 1.5;      // USBPRE: 1 = /1, 0 = /1,5
    }
    bool habilitado(unsigned id) const { return id < F3P_COUNT && o_en_[id]; }
    uint32_t peek(uint32_t off) { return reg_read(off); }
    unsigned avisos() const { return n_avisos_; }
    bool en_reset() const { return !o_sys_rst_; }
    // El OBL_LAUNCH de la Flash (S2): un reset de sistema con OBLRSTF
    void pide_reset_obl() { pend_ |= CSR_OBLRSTF; ev_rst_.notify(sc_core::SC_ZERO_TIME); }

    void set_internal_waveforms(bool on) {
        ondas_ = on;
        for (ClockGen* g : {&g_hclk_, &g_pclk1_, &g_pclk2_, &g_tim1_, &g_tim2_, &g_stk_, &g_rtc_})
            g->set_waveform(on);
        hsi.set_waveform(on); hse.set_waveform(on); lse.set_waveform(on);
    }
    // La onda del MCO se enciende sola cuando PA8 la saca (la pide el SoC)
    void mco_onda(bool on) { g_mco_.set_waveform(on); }

protected:
    bool responds_without_clock() const override { return true; }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR: {
                uint32_t v = cr_ & ~((1u << 1) | (1u << 17) | (1u << 25));
                if (hsi.is_ready()) v |= 1u << 1;
                if (hse.is_ready()) v |= 1u << 17;
                if (pll_listo_)     v |= 1u << 25;
                return v;
            }
            case R_CFGR: {
                uint32_t v = cfgr_ & ~(3u << 2);
                return v | (sws_ << 2);
            }
            case R_CIR:      return cir_;
            case R_APB2RSTR: return apb2rstr_;
            case R_APB1RSTR: return apb1rstr_;
            case R_AHBENR:   return ahbenr_;
            case R_APB2ENR:  return apb2enr_;
            case R_APB1ENR:  return apb1enr_;
            case R_BDCR: {
                uint32_t v = bdcr_ & ~(1u << 1);
                if (lse.is_ready()) v |= 1u << 1;
                return v;
            }
            case R_CSR: {
                uint32_t v = csr_ & ~(1u << 1);
                if (lsi.is_ready()) v |= 1u << 1;
                return v;
            }
            case R_AHBRSTR:  return ahbrstr_;
            case R_CFGR2:    return cfgr2_;
            case R_CFGR3:    return cfgr3_;
            default:         return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (reg_read(off) & ~m) | (v & m);
        }
        switch (off) {
            case R_CR: {
                // HSITRIM (7:3) y los bits de control; HSICAL y los RDY, no
                const uint32_t w = (1u << 0) | (0x1Fu << 3) | (1u << 16) | (1u << 18) |
                                   (1u << 19) | (1u << 24);
                uint32_t nv = (cr_ & ~w) | (v & w);
                // HSEBYP solo se cambia con el HSE apagado [1F: RM0316 §9.4.1]
                if ((cr_ & (1u << 16)) && ((nv ^ cr_) & (1u << 18))) nv = (nv & ~(1u << 18)) | (cr_ & (1u << 18));
                // El PLL no se apaga si es SYSCLK; el HSI tampoco si lo es él
                // o la fuente del PLL en marcha [1F: RM0316 §9.4.1]
                if (sws_ == 2 && !(nv & (1u << 24))) nv |= 1u << 24;
                if (sws_ == 0 && !(nv & 1u)) nv |= 1u;
                cr_ = nv;
                break;
            }
            case R_CFGR: {
                // PLLSRC, PLLXTPRE y PLLMUL solo con el PLL apagado
                uint32_t w = 0x3u | (0xFu << 4) | (7u << 8) | (7u << 11) | (1u << 22) |
                             (1u << 23) | (7u << 24);
                if (!(cr_ & (1u << 24))) w |= (1u << 16) | (1u << 17) | (0xFu << 18);
                cfgr_ = (cfgr_ & ~w) | (v & w);
                // PLLXTPRE es el bit 0 de PREDIV [CMSIS: RCC_CFGR_PLLXTPRE]
                if (!(cr_ & (1u << 24))) cfgr2_ = (cfgr2_ & ~1u) | ((cfgr_ >> 17) & 1u);
                break;
            }
            case R_CIR: {
                cir_ = (cir_ & ~(0x1Fu << 8)) | (v & (0x1Fu << 8));     // IE
                cir_ &= ~((v >> 16) & 0x1Fu);                           // xxxRDYC
                if (v & (1u << 23)) { cir_ &= ~(1u << 7); o_nmi_ = false; }   // CSSC
                break;
            }
            case R_APB2RSTR: apb2rstr_ = v & masc_rst_[1]; break;
            case R_APB1RSTR: apb1rstr_ = v & masc_rst_[2]; break;
            case R_AHBRSTR:  ahbrstr_  = v & masc_rst_[0]; break;
            case R_AHBENR:   ahbenr_   = v & masc_en_[0];  break;
            case R_APB2ENR:  apb2enr_  = v & masc_en_[1];  break;
            case R_APB1ENR:  apb1enr_  = v & masc_en_[2];  break;
            case R_BDCR: {
                // El dominio de respaldo: sin DBP no se deja escribir
                // [1F: RM0316 §9.4.9]
                if (!dbp.read()) { avisa_una(av_dbp_, "RCC_BDCR escrito sin PWR_CR.DBP: el "
                                      "dominio de respaldo esta protegido y no cambia nada. "
                                      "Pon DBP antes (HAL_PWR_EnableBkUpAccess)"); return; }
                const uint32_t w = 1u | (1u << 2) | (3u << 3) | (3u << 8) | (1u << 15) | (1u << 16);
                uint32_t nv = (bdcr_ & ~w) | (v & w);
                // RTCSEL no se cambia una vez elegido, salvo con BDRST
                if ((bdcr_ >> 8) & 3u) nv = (nv & ~(3u << 8)) | (bdcr_ & (3u << 8));
                if (nv & (1u << 16)) nv = 0x18u | (1u << 16);              // BDRST
                bdcr_ = nv;
                break;
            }
            case R_CSR:
                csr_ = (csr_ & ~1u) | (v & 1u);                             // LSION
                if (v & CSR_RMVF) csr_ &= ~0xFF800000u;                    // RMVF
                break;
            case R_CFGR2:
                if (!(cr_ & (1u << 24))) cfgr2_ = v & 0x3FFFu;
                else cfgr2_ = (cfgr2_ & 0xFu) | (v & 0x3FF0u);             // PREDIV, PLL apagado
                if (!(cr_ & (1u << 24))) cfgr_ = (cfgr_ & ~(1u << 17)) | ((cfgr2_ & 1u) << 17);
                break;
            case R_CFGR3: cfgr3_ = v & 0x00FF0333u; break;
            default: return;
        }
        // El gating es COMBINACIONAL: la instrucción siguiente ya lo ve
        refresca_periferia();
        clk_ev_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    // --- Registros ------------------------------------------------------------------
    static constexpr uint32_t HSICAL = 0x10;     // valor del modelo [1F]
    uint32_t cr_ = 0, cfgr_ = 0, cir_ = 0, apb2rstr_ = 0, apb1rstr_ = 0, ahbenr_ = 0;
    uint32_t apb2enr_ = 0, apb1enr_ = 0, bdcr_ = 0, csr_ = 0, ahbrstr_ = 0;
    uint32_t cfgr2_ = 0, cfgr3_ = 0;
    uint32_t masc_en_[3] = {}, masc_rst_[3] = {};
    // --- Estado ------------------------------------------------------------------------
    double f_sys_ = 0, f_hclk_ = 0, f_pclk1_ = 0, f_pclk2_ = 0, f_rtc_ = 0, f_pll_ = 0;
    unsigned sws_ = 0;
    bool pll_listo_ = false, pll_on_ = false;
    double pll_ref_ = 0;
    unsigned pll_mul_ = 0;
    bool o_en_[F3P_COUNT] = {}, o_rst_n_[F3P_COUNT] = {};
    bool o_sys_rst_ = false, o_bkp_rst_ = false, o_drv_nrst_ = false, o_nmi_ = false, o_irq_ = false;
    bool prev_rdy_[5] = {};
    bool css_ = false, ondas_ = true;
    uint32_t pend_ = 0;
    unsigned n_avisos_ = 0;
    bool av_dbp_ = false, av_sys_ = false, av_p1_ = false, av_p2_ = false, av_pll_ = false,
         av_pllin_ = false, av_usb_ = false, av_hse_ = false;
    ClockGen g_hclk_, g_pclk1_, g_pclk2_, g_tim1_, g_tim2_, g_stk_, g_rtc_, g_mco_, g_lsi_;
    sc_core::sc_signal<double> s_stk_hz_{"s_stk_hz"}, s_mco_hz_{"s_mco_hz"};
    std::vector<sc_core::sc_signal<bool>*> propias_b_;
    std::vector<sc_core::sc_signal<double>*> propias_d_;
    sc_core::sc_event pub_ev_, clk_ev_, ev_rst_, pll_ev_, css_ev_, topes_ev_;

    void avisa_una(bool& ya, const std::string& m) {
        if (ya) return;
        ya = true;
        ++n_avisos_;
        SC_REPORT_WARNING("rcc_f3", m.c_str());
    }

    void valores_reset(bool respaldo) {
        cr_ = 0x00000083u | (HSICAL << 8);            // HSION, HSIRDY, HSITRIM = 16
        cfgr_ = 0; cir_ = 0;
        apb2rstr_ = apb1rstr_ = ahbrstr_ = 0;
        ahbenr_ = (1u << 2) | (1u << 4);              // SRAMEN, FLITFEN [1F: RM0316]
        apb2enr_ = apb1enr_ = 0;
        cfgr2_ = 0; cfgr3_ = 0;
        if (respaldo) bdcr_ = 0x00000018u;           // LSEDRV = 11 [1F: RM0316 §9.4.9]
        for (unsigned g = 0; g < 3; ++g) { masc_en_[g] = 0; masc_rst_[g] = 0; }
        for (const BitRccF3& b : RCC_F3_BITS) {
            masc_en_[b.grp] |= 1u << b.bit;
            if (b.rst >= 0) masc_rst_[b.grp] |= 1u << unsigned(b.rst);
        }
    }

    uint32_t enr(unsigned g) const { return g == 0 ? ahbenr_ : g == 1 ? apb2enr_ : apb1enr_; }
    uint32_t rstr(unsigned g) const { return g == 0 ? ahbrstr_ : g == 1 ? apb2rstr_ : apb1rstr_; }
    void refresca_periferia() {
        for (const BitRccF3& b : RCC_F3_BITS) {
            o_en_[b.id] = o_sys_rst_ && ((enr(b.grp) >> b.bit) & 1u);
            o_rst_n_[b.id] = o_sys_rst_ && !(b.rst >= 0 && ((rstr(b.grp) >> unsigned(b.rst)) & 1u));
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- Los osciladores y el PLL ------------------------------------------------------
    void aplica_osciladores() {
        hsi.enable(cr_ & 1u);
        hse.bypass = (cr_ >> 18) & 1u;
        if (!css_) hse.enable((cr_ >> 16) & 1u);
        lsi.enable(csr_ & 1u);
        lse.bypass = (bdcr_ >> 2) & 1u;
        lse.enable(bdcr_ & 1u);
        // El PLL: entrada HSI/2 o HSE/PREDIV, por PLLMUL [CMSIS: RCC_CFGR_PLLMUL]
        const unsigned mul = std::min(16u, ((cfgr_ >> 18) & 0xFu) + 2u);
        const double ref = ((cfgr_ >> 16) & 1u)
                               ? clk_divide(hse.out_hz(), (cfgr2_ & 0xFu) + 1u)
                               : hsi.out_hz() / 2.0;
        const bool on = (cr_ >> 24) & 1u;
        if (on != pll_on_ || ref != pll_ref_ || mul != pll_mul_) {
            pll_on_ = on; pll_ref_ = ref; pll_mul_ = mul;
            if (pll_listo_) { pll_listo_ = false; f_pll_ = 0; }
            pll_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
    void pll_proc() {
        for (;;) {
            wait(pll_ev_);
            if (!pll_on_ || pll_ref_ <= 0.0) continue;
            const double f = pll_ref_ * pll_mul_;
            if (pll_ref_ < 1e6 || pll_ref_ > 24e6)
                avisa_una(av_pllin_, "la entrada del PLL (" + mhz(pll_ref_) + ") esta fuera de "
                          "1-24 MHz [1F: DS9118]");
            if (f > 72e6 * (1 + 1e-9) || f < 16e6)
                avisa_una(av_pll_, "la salida del PLL (" + mhz(f) + " = " + mhz(pll_ref_) +
                          " x " + std::to_string(pll_mul_) + ") esta fuera de 16-72 MHz: en el "
                          "silicio no engancha bien o el chip va fuera de especificacion "
                          "[1F: DS9118]");
            wait(sc_core::sc_time(t_pll_lock_s, sc_core::SC_SEC), pll_ev_);
            if (pll_ev_.triggered()) { pll_ev_.notify(sc_core::SC_ZERO_TIME); continue; }
            if (!pll_on_ || pll_ref_ <= 0.0) continue;
            f_pll_ = f;
            pll_listo_ = true;
            clk_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
    static std::string mhz(double f) {
        char b[32]; std::snprintf(b, sizeof b, "%.3g MHz", f / 1e6); return b;
    }

    static unsigned div_hpre(unsigned h) {
        static const unsigned d[8] = {2, 4, 8, 16, 64, 128, 256, 512};
        return (h & 8u) ? d[h & 7u] : 1u;
    }
    static unsigned div_ppre(unsigned p) { return (p & 4u) ? (2u << (p & 3u)) : 1u; }
    unsigned pre_adc12() const { return (cfgr2_ >> 4) & 0x1Fu; }
    unsigned pre_adc34() const { return (cfgr2_ >> 9) & 0x1Fu; }
    double f_adc(unsigned pre) const {
        // 0xxxx: el reloj del ADC no sale del PLL (el ADC usa el de AHB)
        if (!(pre & 0x10u)) return 0.0;
        static const unsigned d[16] = {1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256, 256, 256, 256, 256};
        return clk_divide(f_pll(), d[pre & 0xFu]);
    }

    void recalcula() {
        const double fh = hsi.out_hz(), fe = hse.out_hz(), fp = f_pll();
        const unsigned sw = cfgr_ & 3u;
        // Si la fuente pedida no está lista, SYSCLK se queda en la anterior
        if      (sw == 0 && fh > 0) { f_sys_ = fh; sws_ = 0; }
        else if (sw == 1 && fe > 0) { f_sys_ = fe; sws_ = 1; }
        else if (sw == 2 && fp > 0) { f_sys_ = fp; sws_ = 2; }
        else if (sws_ == 0 && fh <= 0) { f_sys_ = 0; }
        const unsigned hd = div_hpre((cfgr_ >> 4) & 0xFu);
        const unsigned p1 = div_ppre((cfgr_ >> 8) & 7u), p2 = div_ppre((cfgr_ >> 11) & 7u);
        f_hclk_ = clk_divide(f_sys_, hd);
        f_pclk1_ = clk_divide(f_hclk_, p1);
        f_pclk2_ = clk_divide(f_hclk_, p2);
        const double t1 = p1 == 1 ? f_pclk1_ : 2.0 * f_pclk1_;
        const double t2 = p2 == 1 ? f_pclk2_ : 2.0 * f_pclk2_;
        switch ((bdcr_ >> 8) & 3u) {
            case 1: f_rtc_ = lse.out_hz(); break;
            case 2: f_rtc_ = lsi.out_hz(); break;
            case 3: f_rtc_ = fe / 32.0; break;
            default: f_rtc_ = 0; break;
        }
        if (!((bdcr_ >> 15) & 1u)) f_rtc_ = 0;
        // MCO (CFGR.MCO, 26:24): 010 LSI, 011 LSE, 100 SYSCLK, 101 HSI, 110 HSE,
        // 111 PLL/2 [CMSIS: RCC_CFGR_MCO_*]
        double fm = 0;
        switch ((cfgr_ >> 24) & 7u) {
            case 2: fm = lsi.out_hz(); break;
            case 3: fm = lse.out_hz(); break;
            case 4: fm = f_sys_; break;
            case 5: fm = fh; break;
            case 6: fm = fe; break;
            case 7: fm = fp / 2.0; break;
            default: fm = 0; break;
        }
        // Los topes se miran VEINTE MICROSEGUNDOS DESPUES (revisa_topes): la HAL
        // de ST cambia SYSCLK antes que PPRE1 (HAL_RCC_ClockConfig), y durante
        // unas instrucciones APB1 va a 72 MHz. Eso pasa en todas las placas y
        // no es lo que hay que avisar; lo que se queda, si.
        if (f_sys_ > f3hz_max_ || f_pclk1_ > 36e6 * (1 + 1e-9) || f_pclk2_ > f3hz_max_)
            topes_ev_.notify(sc_core::sc_time(20, sc_core::SC_US));
        if (fe > 0 && (fe < 4e6 || fe > 32e6))
            avisa_una(av_hse_, "el HSE va a " + mhz(fe) + ", fuera de 4-32 MHz [1F: DS9118]");
        if (((apb1enr_ >> 23) & 1u) && std::fabs(f_usb() - 48e6) > 48e6 * 0.0025)
            avisa_una(av_usb_, "el USB tiene reloj (USBEN) a " + mhz(f_usb()) + ": necesita 48 MHz "
                      "exactos (PLL a 72 MHz con USBPRE = /1,5, o a 48 con /1)");
        g_hclk_.set_freq(f_hclk_);
        g_pclk1_.set_freq(f_pclk1_);
        g_pclk2_.set_freq(f_pclk2_);
        g_tim1_.set_freq(t1);
        g_tim2_.set_freq(t2);
        g_stk_.set_freq(clk_divide(f_hclk_, 8));
        g_rtc_.set_freq(f_rtc_);
        g_mco_.set_freq(fm);
        g_lsi_.set_freq(lsi.out_hz());
        set_domain_hz(f_hclk_);
        // CIR: los RDYF suben en el flanco
        const bool rdy[5] = { lsi.is_ready(), lse.is_ready(), hsi.is_ready(), hse.is_ready(), pll_listo_ };
        for (unsigned i = 0; i < 5; ++i) {
            if (rdy[i] && !prev_rdy_[i]) cir_ |= 1u << i;
            prev_rdy_[i] = rdy[i];
        }
        o_irq_ = ((cir_ & 0x1Fu) & ((cir_ >> 8) & 0x1Fu)) != 0;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        if (al_cambiar) al_cambiar();
    }
public:
    std::function<void()> al_cambiar;     // el SoC: la Flash y la SysTick
private:
    static constexpr double f3hz_max_ = 72e6 * (1 + 1e-9);

    // --- El CSS: el HSE falla -> SYSCLK al HSI, CSSF y NMI [1F: RM0316 §9.2.7] ---
    void css() {
        if (!((cr_ >> 19) & 1u) || !hse.failed() || css_) return;
        css_ = true;
        hse.enable(false);
        cr_ &= ~((1u << 16) | (1u << 19));
        if (sws_ == 1 || (sws_ == 2 && ((cfgr_ >> 16) & 1u))) {
            cfgr_ &= ~3u;
            if ((cfgr_ >> 16) & 1u) cr_ &= ~(1u << 24);
            cr_ |= 1u;
        }
        cir_ |= 1u << 7;
        o_nmi_ = true;
        css_ev_.notify(sc_core::SC_ZERO_TIME);
        SC_REPORT_WARNING("rcc_f3", "fallo del HSE detectado por el CSS: SYSCLK -> HSI");
        ++n_avisos_;
    }
    void revisa_topes() {
        if (f_sys_ > f3hz_max_) avisa_una(av_sys_, "SYSCLK = " + mhz(f_sys_) + " > 72 MHz, el "
                                          "maximo del STM32F303");
        if (f_pclk1_ > 36e6 * (1 + 1e-9))
            avisa_una(av_p1_, "PCLK1 = " + mhz(f_pclk1_) + " > 36 MHz, el maximo de APB1: pon "
                      "PPRE1 a /2 (RCC_HCLK_DIV2)");
        if (f_pclk2_ > f3hz_max_) avisa_una(av_p2_, "PCLK2 = " + mhz(f_pclk2_) + " > 72 MHz");
    }
    void css_nmi_proc() {
        for (;;) {
            wait(css_ev_);
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(sc_core::sc_time(1, sc_core::SC_NS));
            o_nmi_ = false;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void arbol_proc() {
        sc_core::sc_event_or_list l;
        l |= hsi.state_event(); l |= hse.state_event(); l |= lsi.state_event();
        l |= lse.state_event(); l |= clk_ev_;
        for (;;) {
            wait(l);
            css();
            aplica_osciladores();
            recalcula();
        }
    }

    // --- Los resets ------------------------------------------------------------------------
    // Las fuentes: POR, el pin, SYSRESETREQ, los dos perros y OBL_LAUNCH. Cada
    // una deja su bandera en CSR [CMSIS: RCC_CSR_*RSTF]
    bool prev_srr_ = false, prev_iw_ = false, prev_ww_ = false;
    void fuentes_proc() {
        const bool srr = sysresetreq.read(), iw = iwdg_rst_req.read(), ww = wwdg_rst_req.read();
        if (srr && !prev_srr_) pend_ |= CSR_SFTRSTF;
        if (iw && !prev_iw_) pend_ |= CSR_IWDGRSTF;
        if (ww && !prev_ww_) pend_ |= CSR_WWDGRSTF;
        prev_srr_ = srr; prev_iw_ = iw; prev_ww_ = ww;
        if (!por_ok.read()) pend_ |= CSR_PORRSTF;
        if (!nrst_in_n.read() && !o_drv_nrst_) pend_ |= CSR_PINRSTF;
        if (pend_) ev_rst_.notify(sc_core::SC_ZERO_TIME);
    }

    void reset_proc() {
        bool primera = true;
        for (;;) {
            // --- en reset ------------------------------------------------------------
            const bool por = primera || !por_ok.read() || (pend_ & CSR_PORRSTF);
            const bool obl = (pend_ & CSR_OBLRSTF) != 0;
            if (por) { csr_ = (csr_ & 0x1u) | CSR_PORRSTF | CSR_PINRSTF; }
            else csr_ |= pend_ | ((pend_ & ~CSR_PINRSTF) ? CSR_PINRSTF : 0u);
            pend_ = 0;
            primera = false;
            valores_reset(por);
            if (por) { bdcr_ = 0x18u; csr_ &= ~1u; }
            css_ = false;
            hse.clear_failed();
            o_sys_rst_ = false;
            if (por) o_bkp_rst_ = false;
            o_drv_nrst_ = true;
            refresca_periferia();
            aplica_osciladores();
            recalcula();
            if ((por || obl) && al_cargar_opciones) al_cargar_opciones();
            wait(t_rst_pulse);
            o_drv_nrst_ = false;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
            wait(sc_core::sc_time(1, sc_core::SC_NS));
            // Un corte de alimentacion con el pin todavia abajo vuelve a ser un
            // POR entero: con su carga de los bytes de opcion
            bool corte = false;
            while (!por_ok.read() || !nrst_in_n.read()) {
                if (!por_ok.read()) corte = true;
                wait(por_ok.value_changed_event() | nrst_in_n.value_changed_event());
            }
            if (corte) { pend_ |= CSR_PORRSTF; continue; }
            wait(t_rst_release);
            pend_ &= ~(CSR_PINRSTF | CSR_PORRSTF);
            // --- fuera ---------------------------------------------------------------
            o_sys_rst_ = true;
            o_bkp_rst_ = true;
            refresca_periferia();
            aplica_osciladores();
            recalcula();
            wait(ev_rst_);
            // Un reset que no es de alimentación no borra el dominio de respaldo
        }
    }

    void publica_proc() {
        sys_rst_n.write(o_sys_rst_);
        bkp_rst_n.write(o_bkp_rst_);
        drive_nrst_low.write(o_drv_nrst_);
        nmi_css.write(o_nmi_);
        irq.write(o_irq_);
        for (unsigned i = 0; i < F3P_COUNT; ++i) { s_en[i].write(o_en_[i]); s_rst_n[i].write(o_rst_n_[i]); }
    }
};

inline double RccF3::f_usart(unsigned n) const {
    // USART1SW (1:0); USART2SW (17:16); USART3SW (19:18); UART4SW (21:20);
    // UART5SW (23:22): 00 PCLK, 01 SYSCLK, 10 LSE, 11 HSI [CMSIS]
    static const unsigned sh[6] = {0, 0, 16, 18, 20, 22};
    if (n < 1 || n > 5) return 0;
    switch ((cfgr3_ >> sh[n]) & 3u) {
        case 1: return f_sys_;
        case 2: return lse.out_hz();
        case 3: return hsi.out_hz();
        default: return n == 1 ? f_pclk2_ : f_pclk1_;
    }
}
inline double RccF3::f_i2c(unsigned n) const {
    // I2C1SW (4), I2C2SW (5): 0 HSI, 1 SYSCLK
    if (n < 1 || n > 2) return 0;
    return ((cfgr3_ >> (3 + n)) & 1u) ? f_sys_ : hsi.out_hz();
}
inline double RccF3::f_tim18(unsigned n) const {
    // TIM1SW (8), TIM8SW (9): 0 PCLK2 (x2 si se divide), 1 PLL x 2 [CMSIS]
    const unsigned b = n == 8 ? 9u : 8u;
    if ((cfgr3_ >> b) & 1u) return 2.0 * f_pll();
    const unsigned p2 = div_ppre((cfgr_ >> 11) & 7u);
    return p2 == 1 ? f_pclk2_ : 2.0 * f_pclk2_;
}

} // namespace stm32
#endif // STM32_RCC_RCC_F3_H
