// =============================================================================
// herramientas.h — Lo que hace OpenOCD por su cuenta con cada chip, para que
//                  los stubs internos de GDB lo hagan igual (herramienta_if)
//
// Sacado de los guiones de OpenOCD (openocd-esp32 v0.12.0-esp32-20260831, que
// trae los de OpenOCD de arriba), que es la herramienta con la que se depuran
// estas placas de verdad:
//
//   STM32F4 (F405/407/417/446)  target/stm32f4x.cfg, evento examine-end
//   STM32F3                     target/stm32f3x.cfg, evento examine-end
//       DBGMCU_CR      |= DBG_STANDBY | DBG_STOP | DBG_SLEEP  (0xE0042004, 0x7)
//       DBGMCU_APB1_FZ |= DBG_IWDG_STOP | DBG_WWDG_STOP       (0xE0042008, 0x1800)
//
//   ESP32-C3   target/esp32c3.cfg y esp_common.cfg
//       halted:      esp32c3_wdt_disable (MWDT0, MWDT1, RWDT y SWD apagados)
//       gdb-attach:  con la protección de memoria echada (PMS de IRAM0 o
//                    DRAM0 bloqueado), `reset halt`
//       reset:       esp32c3_soc_reset: RTC_CNTL_SW_SYS_RST, y
//                    TIMG_RTCCALICFG a 0
//
//   MSP432P4   target/ti/msp432.cfg y MSP432E4 target/ti/stellaris.cfg: no
//              tocan los perros ni el depurador del chip. El WDT_A del P4 se
//              para solo con el núcleo (SYSCTL_PERIHALT_CTL.HALT_WDT, a 1 de
//              reset); el del E4 sigue salvo que el firmware ponga STALL. No
//              hay herramienta que imitar: nullptr.
// =============================================================================
#ifndef STM32_SOC_HERRAMIENTAS_H
#define STM32_SOC_HERRAMIENTAS_H

#include <cstdint>
#include "../common/gdb_rsp.h"

namespace stm32 {

class HerramientaStm32 : public herramienta_if {
public:
    explicit HerramientaStm32(const char* guion) : guion_(guion) {}
    const char* nombre() const override { return guion_; }
    void al_conectar(acceso_nv_if& d) override {
        uint32_t v = 0;
        if (d.nv_leer(DBGMCU_CR, v)) d.nv_escribir(DBGMCU_CR, v | 0x7u);
        if (d.nv_leer(DBGMCU_APB1_FZ, v)) d.nv_escribir(DBGMCU_APB1_FZ, v | 0x1800u);
    }
    static HerramientaStm32& f4() { static HerramientaStm32 h("OpenOCD target/stm32f4x.cfg"); return h; }
    static HerramientaStm32& f3() { static HerramientaStm32 h("OpenOCD target/stm32f3x.cfg"); return h; }

private:
    static constexpr uint32_t DBGMCU_CR = 0xE0042004u, DBGMCU_APB1_FZ = 0xE0042008u;
    const char* guion_;
};

class HerramientaEsp32C3 : public herramienta_if {
public:
    const char* nombre() const override { return "openocd-esp32 target/esp32c3.cfg"; }
    void al_parar(acceso_nv_if& d) override {          // esp32c3_wdt_disable
        d.nv_escribir(0x6001F064u, 0x50D83AA1u);         // TG0: WKEY
        d.nv_escribir(0x6001F048u, 0);                   //      WDTCONFIG0
        d.nv_escribir(0x6001F07Cu, 0x2u);                //      INT_CLR (WDT)
        d.nv_escribir(0x60020064u, 0x50D83AA1u);         // TG1
        d.nv_escribir(0x60020048u, 0);
        d.nv_escribir(0x6002007Cu, 0x2u);
        d.nv_escribir(0x600080A8u, 0x50D83AA1u);         // RTC WDT: WPROTECT
        d.nv_escribir(0x60008090u, 0);                   //          WDTCONFIG0
        d.nv_escribir(0x600080B0u, 0x8F1D312Au);         // SWD: WPROTECT
        d.nv_escribir(0x600080ACu, 0x84B00000u);         //      SWD_CONF (DISABLE)
        d.nv_escribir(0x6000804Cu, 0x8008u);             // RTC INT_CLR: WDT y SWD
    }
    bool reset_al_conectar(acceso_nv_if& d) override {  // esp32c3_memprot_is_enabled
        return (d.nv_leer_o(0x600C10A8u) & 1u) || (d.nv_leer_o(0x600C10C0u) & 1u);
    }
    bool tiene_reset_soc() const override { return true; }
    void reset_soc(acceso_nv_if& d) override {          // esp32c3_soc_reset
        d.nv_escribir(0x60008000u, 0x9C00A000u);         // RTC_CNTL_OPTIONS0: SW_SYS_RST
        d.nv_escribir(0x6001F068u, 0);                   // TIMG_RTCCALICFG
    }
    static HerramientaEsp32C3& el() { static HerramientaEsp32C3 h; return h; }
};

} // namespace stm32
#endif // STM32_SOC_HERRAMIENTAS_H
