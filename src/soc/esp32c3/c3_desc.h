// =============================================================================
// c3_desc.h — El ESP32-C3: su núcleo, su mapa, sus encapsulados y sus nombres
//
// E3 de doc/analisis_esp32_riscv.md. Lo que es del C3 y no del núcleo
// genérico ni de la capa de Espressif.
//
// EL NÚCLEO [TRM-C3 §1]: RV32IMC con M y U, SIN mie/mip (las interrupciones
// las enmascara el INTC por registros), mtvec solo vectorizado con base
// alineada a 256, 16 regiones PMP, sin contadores estándar (los suyos son
// mpcer/mpcmr/mpccr, CSR 0x7E0-0x7E2), desalineados en hardware con la
// errata CPU-863 [ER-C3 §3.1], mvendorid 0x612, marchid 0x8000_0001, mimpid
// 1. Depuración: DM 0.13 con program buffer de 16, 8 triggers y tcontrol.
// Los TIEMPOS por instrucción son de modelo: el TRM no los da (4 etapas en
// orden); la latencia del INTC, 4 ciclos, sí [TRM-C3 §1.5.3.1].
//
// EL MAPA [TRM-C3 §3, tabla 3.3-3]: ver `c3::`.
// =============================================================================
#ifndef STM32_SOC_ESP32C3_C3_DESC_H
#define STM32_SOC_ESP32C3_C3_DESC_H

#include <cstdio>
#include <string>
#include "../mcu_desc.h"
#include "../../pins/encapsulado.h"
#include "../../pins/nomenclatura.h"
#include "../../rv/hart/rv_caps.h"
#include "../../periph/esp/gpio_esp.h"

namespace stm32 {

namespace c3 {
// --- Memoria --------------------------------------------------------------------
constexpr uint32_t ROM0_I = 0x40000000u, ROM_I_FIN = 0x40060000u;   // ROM0+ROM1 (bus I)
constexpr uint32_t ROM1_D = 0x3FF00000u, ROM1_D_FIN = 0x3FF20000u;  // ROM1 (bus D)
constexpr uint32_t SRAM0_I = 0x4037C000u;                           // 16 KB, solo I
constexpr uint32_t SRAM1_I = 0x40380000u, SRAM_I_FIN = 0x403E0000u; // 384 KB
constexpr uint32_t SRAM1_D = 0x3FC80000u, SRAM1_D_FIN = 0x3FCE0000u;
constexpr uint32_t SRAM_TAM = 400u * 1024u;                         // 16 + 384 KB
constexpr uint32_t RTC_FAST = 0x50000000u, RTC_FAST_TAM = 8u * 1024u;
constexpr uint32_t FLASH_D = 0x3C000000u, FLASH_I = 0x42000000u;    // E4
constexpr uint32_t PERIF = 0x60000000u, PERIF_FIN = 0x600D1000u;
// --- Periféricos -----------------------------------------------------------------
constexpr uint32_t UART0_B = 0x60000000u, SPI1_B = 0x60002000u, SPI0_B = 0x60003000u,
                   GPIO_B = 0x60004000u, RTC_CNTL_B = 0x60008000u, EFUSE_B = 0x60008800u,
                   REGI2C_B = 0x6000E000u, FE2_B = 0x60005000u, FE_B = 0x60006000u,
                   NRX_B = 0x6001CC00u, BB_B = 0x6001D000u,
                   IO_MUX_B = 0x60009000u, UART1_B = 0x60010000u, I2C_B = 0x60013000u,
                   UHCI_B = 0x60014000u, RMT_B = 0x60016000u, LEDC_B = 0x60019000u,
                   // La tabla 3.3-3 del TRM pone el eFuse en 0x6001_A000; ESP-IDF
                   // (soc/reg_base.h) en 0x6000_8800, dentro de la ventana de
                   // RTC_CNTL. Se atiende el de ESP-IDF; el otro solo guarda.
                   EFUSE_TRM_B = 0x6001A000u,
                   TIMG0_B = 0x6001F000u, TIMG1_B = 0x60020000u, SYSTIMER_B = 0x60023000u,
                   SPI2_B = 0x60024000u, SYSCON_B = 0x60026000u, TWAI_B = 0x6002B000u,
                   I2S_B = 0x6002D000u, AES_B = 0x6003A000u, SHA_B = 0x6003B000u,
                   RSA_B = 0x6003C000u, DS_B = 0x6003D000u, HMAC_B = 0x6003E000u,
                   GDMA_B = 0x6003F000u, ADC_B = 0x60040000u, USB_SJ_B = 0x60043000u,
                   SYSTEM_B = 0x600C0000u, PMS_B = 0x600C1000u, INTMTX_B = 0x600C2000u,
                   EXTMEM_B = 0x600C4000u,
                   XTS_AES_B = 0x600CC000u, ASSIST_B = 0x600CE000u, WCL_B = 0x600D0000u;
// --- Fuentes de interrupción [TRM-C3 tabla 8.3-1] ----------------------------------
constexpr unsigned N_FUENTES = 62;
// (contrastadas con ETS_*_INTR_SOURCE de soc/interrupts.h de ESP-IDF: el ADC es
// la 43 y la GDMA la 44-46; antes de 2026-11-09 estaban en 46 y 47, que son
// la GDMA 2 y el RSA, sin que ninguna prueba lo notara)
constexpr unsigned F_UHCI0 = 15, F_GPIO = 16, F_SPI2 = 19, F_I2S = 20, F_UART0 = 21, F_UART1 = 22,
                   F_LEDC = 23, F_TWAI = 25, F_USB_SJ = 26, F_RMT = 28, F_I2C = 29,
                   F_TG0_T0 = 32, F_TG0_WDT = 33, F_TG1_T0 = 34, F_TG1_WDT = 35, F_SYSTIMER0 = 37,
                   F_ADC = 43, F_DMA0 = 44, F_RSA = 47, F_AES = 48, F_SHA = 49, F_SW0 = 50;
// --- Señales de la matriz GPIO [TRM-C3 tabla 5.11-1] --------------------------------
constexpr unsigned SIG_TWAI = 74, SIG_LEDC0 = 45, SIG_RMT0 = 51, SIG_I2C_SCL = 53, SIG_I2C_SDA = 54,
                   SIG_FSPICLK = 63, SIG_FSPIQ = 64, SIG_FSPID = 65, SIG_FSPICS0 = 68;
constexpr unsigned SIG_U0RXD = 6, SIG_U0TXD = 6, SIG_U1RXD = 9, SIG_U1TXD = 9,
                   SIG_CPU_GPIO0 = 28;
constexpr unsigned N_GPIO = 22;
} // namespace c3

// --- El núcleo -------------------------------------------------------------------
// [de modelo] salvo lat_irq: 4 etapas, carga de 1 ciclo más el bus, salto
// tomado vacía el cauce, división iterativa
inline constexpr RvTiempos RV_TIEMPOS_C3{1, 1, 1, 1, 3, 2, 3, 1, 17, 1, 1, 3, 3, 4};
// El DM: 8 triggers, tcontrol, program buffer de 16 palabras y 2 datos (lo
// que dice openocd-esp32 de un C3 de verdad: «datacount=2 progbufsize=16»),
// SBA de palabra, y su región en el mapa de la CPU, 0x2000_0000-0x27FF_FFFF
// [TRM-C3 tabla 1.3-1; SOC_DEBUG_LOW/HIGH de ESP-IDF]
inline constexpr RvDebugCaps RV_DBG_C3{8, true, 16, 2, false, true, 0x4, 0x20000000u, 0x08000000u};
inline constexpr RvCaps RV_ESP32C3{
    "ESP32-C3", 32,
    RV_EXT_I | RV_EXT_M | RV_EXT_C | RV_EXT_U,
    RV_Z_ICSR | RV_Z_IFENCEI,
    false, 0u, false, 0, RV_MTVEC_VECTORIZADO, 256,
    0x40000000u, 0x00000001u, 16, false, RvDesalineado::HardwareCpu863,
    0x612u, 0x80000001u, 1u, false, RV_DBG_C3, &RV_TIEMPOS_C3
};

// --- Los pines: función 0 y configuración tras el reset [TRM-C3 tabla 5.12-1] ------
// GPIO19 sale con pull-up: el del USB (USB_SERIAL_JTAG_DP_PULLUP), a 1 tras
// el reset. MTCK con pull-up mientras DIS_PAD_JTAG = 0.
inline const std::vector<PinEsp>& pines_c3() {
    using T = PinEsp;
    static const std::vector<PinEsp> v{
        {"GPIO0", T::Gpio, -1, false, false, false, 2},
        {"GPIO1", T::Gpio, -1, false, false, false, 2},
        {"GPIO2", T::Gpio, -1, true, false, false, 2},
        {"GPIO3", T::Gpio, -1, true, false, false, 2},
        {"MTMS", T::NoModelada, -1, true, false, false, 2},
        {"MTDI", T::NoModelada, -1, true, false, false, 2},
        {"MTCK", T::NoModelada, -1, true, true, false, 2},
        {"MTDO", T::NoModelada, -1, true, false, false, 2},
        {"GPIO8", T::Gpio, -1, true, false, false, 2},
        {"GPIO9", T::Gpio, -1, true, true, false, 2},
        {"GPIO10", T::Gpio, -1, true, false, false, 2},
        {"GPIO11", T::Gpio, -1, false, false, false, 2},
        {"SPIHD", T::NoModelada, -1, true, true, false, 2},
        {"SPIWP", T::NoModelada, -1, true, true, false, 2},
        {"SPICS0", T::NoModelada, -1, true, true, false, 2},
        {"SPICLK", T::NoModelada, -1, true, true, false, 2},
        {"SPID", T::NoModelada, -1, true, true, false, 2},
        {"SPIQ", T::NoModelada, -1, true, true, false, 2},
        {"GPIO18", T::Gpio, -1, false, false, false, 3},
        {"GPIO19", T::Gpio, -1, false, true, false, 3},
        {"U0RXD", T::EntradaDirecta, int(c3::SIG_U0RXD), true, true, false, 2},
        {"U0TXD", T::SalidaDirecta, int(c3::SIG_U0TXD), false, true, false, 2},
    };
    return v;
}

// --- Encapsulados [DS-C3 tabla 1-1] ---------------------------------------------------
// QFN32 de 5x5. Con la Flash dentro (C3FH4) siguen saliendo los 22; las «X»
// (C3FH4X) no sacan GPIO12-17, que van a la Flash interna.
inline constexpr Encapsulado ENC_C3_QFN32{"QFN32", 33, 22, {0x003FFFFFu}, true};
inline constexpr Encapsulado ENC_C3_QFN32_X{"QFN32", 33, 16, {0x003C0FFFu}, true};

// --- Nomenclatura: GPIOn (canónica), IOn, y los nombres de función -----------------
namespace c3 {
inline bool pad_c3(const std::string& t, unsigned& puerto, unsigned& pin) {
    std::string s = t;
    for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    unsigned n = 0;
    auto num = [&](size_t i) {
        if (i >= s.size() || s.size() - i > 2) return false;
        if (s.size() - i > 1 && s[i] == '0') return false;
        n = 0;
        for (size_t k = i; k < s.size(); ++k) {
            if (s[k] < '0' || s[k] > '9') return false;
            n = n * 10 + unsigned(s[k] - '0');
        }
        return n < N_GPIO;
    };
    if ((s.compare(0, 4, "GPIO") == 0 && num(4)) || (s.compare(0, 2, "IO") == 0 && num(2))) {
        puerto = 0; pin = n; return true;
    }
    static const char* const alias[][2] = {
        {"MTMS", "4"}, {"MTDI", "5"}, {"MTCK", "6"}, {"MTDO", "7"},
        {"U0RXD", "20"}, {"U0TXD", "21"},
        {"SPIHD", "12"}, {"SPIWP", "13"}, {"SPICS0", "14"}, {"SPICLK", "15"}, {"SPID", "16"},
        {"SPIQ", "17"},
    };
    for (const auto& a : alias)
        if (s == a[0]) { puerto = 0; pin = unsigned(std::stoul(a[1])); return true; }
    return false;
}
inline std::string canonico_c3(unsigned, unsigned pin) { return "GPIO" + std::to_string(pin); }
inline const char* const ALIM_C3[] = {
    "VDD3P3", "VDD3P3_RTC", "VDD_SPI", "VDDA", "GND", "CHIP_PU", "XTAL_P", "XTAL_N", nullptr
};
} // namespace c3

inline const Nomenclatura NOM_ESP32C3{"ESP32C3", &c3::pad_c3, &c3::canonico_c3, c3::ALIM_C3};
// Por prefijo del tipo: «ESP32-C3», «ESP32-C3FH4»... (y sin el guion)
struct RegistroNomC3 {
    RegistroNomC3() {
        ContextoNomenclatura::g().registra_tipo("ESP32-C3", &NOM_ESP32C3);
        ContextoNomenclatura::g().registra_tipo("ESP32C3", &NOM_ESP32C3);
    }
};
inline const RegistroNomC3 g_registro_nom_c3{};

// --- El descriptor de un miembro de la familia --------------------------------------
struct McuCapsC3 {
    const char* nombre;
    const RvCaps* nucleo;
    uint32_t flash_interna;      // 0: la Flash va en la placa
    unsigned revision;           // 4 = v0.4, 101 = v1.1
    Encapsulado enc;
};
inline constexpr McuCapsC3 MCU_ESP32C3{"ESP32-C3", &RV_ESP32C3, 0, 4, ENC_C3_QFN32};
inline constexpr McuCapsC3 MCU_ESP32C3FH4{"ESP32-C3FH4", &RV_ESP32C3, 4u << 20, 4, ENC_C3_QFN32};
// El de los módulos ESP32-C3-MINI-1 (la DevKitM-1): 4 MB dentro, 85 °C
inline constexpr McuCapsC3 MCU_ESP32C3FN4{"ESP32-C3FN4", &RV_ESP32C3, 4u << 20, 4, ENC_C3_QFN32};
inline constexpr McuCapsC3 MCU_ESP32C3FH4X{"ESP32-C3FH4X", &RV_ESP32C3, 4u << 20, 101, ENC_C3_QFN32_X};
inline constexpr McuCapsC3 MCU_ESP32C3FH8X{"ESP32-C3FH8X", &RV_ESP32C3, 8u << 20, 101, ENC_C3_QFN32_X};

struct RegistroCatalogoC3 {
    RegistroCatalogoC3() {
        for (const McuCapsC3* m : {&MCU_ESP32C3, &MCU_ESP32C3FH4, &MCU_ESP32C3FN4, &MCU_ESP32C3FH4X,
                                   &MCU_ESP32C3FH8X})
            CatalogoMcu::registra(McuDesc{m->nombre, "ESP32C3", &m->enc, m});
    }
};
inline const RegistroCatalogoC3 g_registro_catalogo_c3{};

} // namespace stm32
#endif // STM32_SOC_ESP32C3_C3_DESC_H
