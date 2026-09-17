// =============================================================================
// ahb_types.h — Tipos comunes del bus, extensión TLM AHB y mapa de memoria
// Modelo SystemC del STM32F407VG. Referencias: doc/informe_revisado.md §5, §6.
//
// Fase F1: se completa el mapa de memoria (regiones ARMv7-M [IR, §5.1], sectores
// de Flash [IR, §5.2.1], tabla de estados de espera [IR, §5.2.2], regiones de
// bit-banding [IR, §5.4]) y se añaden utilidades de payload TLM que usan la
// matriz, los decodificadores y todos los esclavos.
// =============================================================================
#ifndef STM32_COMMON_AHB_TYPES_H
#define STM32_COMMON_AHB_TYPES_H

#include <systemc>
#include <tlm>
#include <cstdint>
#include <cstddef>

namespace stm32 {

// ---------------------------------------------------------------------------
// Identificadores de los 8 maestros de la matriz AHB [IR, §6.1.1]
// ---------------------------------------------------------------------------
enum class BusMaster : uint8_t {
    CORE_IBUS = 0,   // Cortex-M4 I-bus
    CORE_DBUS,       // Cortex-M4 D-bus
    CORE_SBUS,       // Cortex-M4 S-bus (y AHB-AP de debug)
    DMA1_MEM,        // DMA1 puerto de memoria
    DMA2_MEM,        // DMA2 puerto de memoria
    DMA2_PERIPH,     // DMA2 puerto de periféricos
    ETH_DMA,         // DMA del Ethernet MAC
    OTG_HS_DMA,      // DMA del USB OTG HS
    N_MASTERS
};

// Los 7 esclavos de la matriz [IR, §6.1.2]
enum class BusSlaveId : uint8_t {
    FLASH_ICODE = 0, FLASH_DCODE, SRAM1, SRAM2, AHB1_SEG, AHB2_SEG, FSMC_EXT,
    N_SLAVES
};

inline const char* master_name(BusMaster m) {
    switch (m) {
        case BusMaster::CORE_IBUS:   return "I-bus";
        case BusMaster::CORE_DBUS:   return "D-bus";
        case BusMaster::CORE_SBUS:   return "S-bus";
        case BusMaster::DMA1_MEM:    return "DMA1-M";
        case BusMaster::DMA2_MEM:    return "DMA2-M";
        case BusMaster::DMA2_PERIPH: return "DMA2-P";
        case BusMaster::ETH_DMA:     return "ETH-DMA";
        case BusMaster::OTG_HS_DMA:  return "OTGHS-DMA";
        default:                     return "?";
    }
}

inline const char* slave_name(BusSlaveId s) {
    switch (s) {
        case BusSlaveId::FLASH_ICODE: return "Flash-I";
        case BusSlaveId::FLASH_DCODE: return "Flash-D";
        case BusSlaveId::SRAM1:       return "SRAM1";
        case BusSlaveId::SRAM2:       return "SRAM2";
        case BusSlaveId::AHB1_SEG:    return "AHB1";
        case BusSlaveId::AHB2_SEG:    return "AHB2";
        case BusSlaveId::FSMC_EXT:    return "FSMC";
        default:                      return "?";
    }
}

// ---------------------------------------------------------------------------
// Tipos de transferencia y ráfaga AHB-Lite [IR, §6.3]
// ---------------------------------------------------------------------------
// Los cuatro modos de energía del MCU [IR, §14.2]. Vive aquí, con los tipos
// del sistema, porque lo mira medio modelo: el RCC para decidir qué reloj
// reparte, los pines para saber si tienen que quedarse en alta impedancia y el
// propio PWR, que es quien lo decide.
enum LpMode : uint8_t { LP_RUN = 0, LP_SLEEP = 1, LP_STOP = 2, LP_STANDBY = 3 };

enum class HTrans : uint8_t { IDLE = 0, BUSY = 1, NONSEQ = 2, SEQ = 3 };
enum class HBurst : uint8_t {
    SINGLE = 0, INCR = 1, WRAP4 = 2, INCR4 = 3,
    WRAP8  = 4, INCR8 = 5, WRAP16 = 6, INCR16 = 7
};

// ---------------------------------------------------------------------------
// Extensión TLM con la semántica AHB que el payload genérico no cubre
// (id de maestro para el arbitraje, HPROT, ráfagas, exclusivos LDREX/STREX)
// ---------------------------------------------------------------------------
struct AhbExt : tlm::tlm_extension<AhbExt> {
    BusMaster master     = BusMaster::CORE_SBUS;
    bool      privileged = true;    // HPROT[1]
    bool      instr      = false;   // HPROT[0] == 0 (fetch)
    bool      bufferable = false;   // HPROT[2]
    bool      cacheable  = false;   // HPROT[3]
    uint8_t   hburst     = uint8_t(HBurst::SINGLE);
    bool      exclusive  = false;   // acceso LDREX/STREX
    bool      excl_ok    = false;   // resultado del monitor (respuesta STREX)
    // Contabilidad de la matriz (F1): esclavo resuelto y ciclos de espera que
    // el arbitraje ha añadido a esta transacción. Útil para trazas y tests.
    uint8_t   slave      = uint8_t(BusSlaveId::N_SLAVES);
    uint32_t  wait_cycles = 0;

    tlm_extension_base* clone() const override { return new AhbExt(*this); }
    void copy_from(const tlm_extension_base& e) override {
        *this = static_cast<const AhbExt&>(e);
    }
};

// Devuelve la extensión AHB del payload; si no existe, una por defecto estática
// (permite que cualquier iniciador simple funcione sin construir la extensión).
inline AhbExt& ahb_ext(tlm::tlm_generic_payload& gp) {
    AhbExt* e = gp.get_extension<AhbExt>();
    if (e) return *e;
    static AhbExt dflt;      // maestro S-bus, privilegiado, dato, SINGLE
    dflt = AhbExt();
    return dflt;
}

// ---------------------------------------------------------------------------
// Mapa de memoria [IR, §5.1, §6.5, §15.1]
// ---------------------------------------------------------------------------
namespace addr {
// --- Regiones ARMv7-M [IR, §5.1] -------------------------------------------
constexpr uint32_t CODE_BASE    = 0x00000000, CODE_END    = 0x1FFFFFFF;
constexpr uint32_t SRAM_RGN     = 0x20000000, SRAM_RGN_END= 0x3FFFFFFF;
constexpr uint32_t PERIPH_RGN   = 0x40000000, PERIPH_RGN_END = 0x5FFFFFFF;
constexpr uint32_t EXTRAM_RGN   = 0x60000000, EXTRAM_RGN_END = 0x9FFFFFFF;
constexpr uint32_t EXTDEV_RGN   = 0xA0000000, EXTDEV_RGN_END = 0xDFFFFFFF;
constexpr uint32_t PPB_BASE     = 0xE0000000, PPB_SIZE    = 0x00100000;

// --- Memorias [IR, §5.2, §5.3, §15.1] --------------------------------------
constexpr uint32_t FLASH_BASE   = 0x08000000, FLASH_SIZE   = 0x00100000; // 1 MB
constexpr uint32_t SYSMEM_BASE  = 0x1FFF0000, SYSMEM_SIZE  = 0x00007800; // 30 KB
constexpr uint32_t OTP_BASE     = 0x1FFF7800, OTP_SIZE     = 0x210;      // 528 B
constexpr uint32_t OPT_BASE     = 0x1FFFC000, OPT_SIZE     = 0x10;       // 16 B
constexpr uint32_t CCM_BASE     = 0x10000000, CCM_SIZE     = 0x00010000; // 64 KB
constexpr uint32_t SRAM1_BASE   = 0x20000000, SRAM1_SIZE   = 0x0001C000; // 112 KB
constexpr uint32_t SRAM2_BASE   = 0x2001C000, SRAM2_SIZE   = 0x00004000; // 16 KB
constexpr uint32_t BKPSRAM_BASE = 0x40024000, BKPSRAM_SIZE = 0x00001000; // 4 KB
constexpr uint32_t PERIPH_BASE  = 0x40000000;
constexpr uint32_t AHB1_BASE    = 0x40020000;
constexpr uint32_t AHB2_BASE    = 0x50000000, AHB2_END     = 0x50060BFF;
constexpr uint32_t FSMC_MEM     = 0x60000000; // bancos externos 0x6000_0000-0x9FFF_FFFF
constexpr uint32_t FSMC_MEM_END = 0x9FFFFFFF;
constexpr uint32_t FSMC_REGS    = 0xA0000000, FSMC_REGS_SIZE = 0x1000;

// --- Bit-banding [IR, §5.4] -------------------------------------------------
constexpr uint32_t BB_SRAM_BASE   = 0x20000000, BB_SRAM_LEN   = 0x00100000; // 1 MB
constexpr uint32_t BB_SRAM_ALIAS  = 0x22000000, BB_SRAM_ALEN  = 0x02000000; // 32 MB
constexpr uint32_t BB_PERIPH_BASE = 0x40000000, BB_PERIPH_LEN = 0x00100000;
constexpr uint32_t BB_PERIPH_ALIAS= 0x42000000, BB_PERIPH_ALEN= 0x02000000;

// --- Y AQUI TERMINA LO QUE ES ARQUITECTURA -----------------------------------
//
// Lo que sigue -donde esta cada periferico, la geometria de la Flash, cuantas
// lineas de interrupcion hay y los topes de frecuencia- es DE LA FAMILIA, no
// de la arquitectura, y vive aparte. Se incluye aqui, con el `namespace addr`
// todavia abierto, para que ningun fichero de los que ya usaban `addr::TIM2_B`
// haya tenido que cambiar un include. Es el punto exacto donde se elegiria
// otro mapa. [doc/stm32f407vg_vs_446re.md, fase 1]
#include "../soc/f4_mapa_perif.h"

// ---------------------------------------------------------------------------
// Modo de arranque / aliasing de 0x0000 0000 [IR, §2.3, §12.21.2]
// ---------------------------------------------------------------------------
enum MemMode : uint8_t {
    MEM_MODE_FLASH  = 0,   // Flash principal en 0x0
    MEM_MODE_SYSTEM = 1,   // System memory (bootloader) en 0x0
    MEM_MODE_FSMC   = 2,   // FSMC banco 1 en 0x0
    MEM_MODE_SRAM1  = 3    // SRAM1 en 0x0
};

// Traduce una dirección del alias de arranque (0x0000 0000-0x000F FFFF) al
// destino físico según MEM_MODE. Fuera del alias devuelve la dirección tal cual.
inline uint32_t apply_boot_alias(uint32_t a, uint8_t mem_mode) {
    if (a >= 0x00100000u) return a;             // fuera del espejo de 1 MB
    switch (mem_mode) {
        case MEM_MODE_SYSTEM: return addr::SYSMEM_BASE + (a & 0x7FFFu);
        case MEM_MODE_FSMC:   return addr::FSMC_MEM    + a;
        case MEM_MODE_SRAM1:  return addr::SRAM1_BASE  + (a % addr::SRAM1_SIZE);
        case MEM_MODE_FLASH:
        default:              return addr::FLASH_BASE  + a;
    }
}

// ---------------------------------------------------------------------------
// Utilidades de payload TLM
// ---------------------------------------------------------------------------
// Rellena un payload de lectura/escritura de N bytes sobre un buffer del
// llamante. No reserva memoria (los tests y modelos usan buffers de pila).
inline void gp_setup(tlm::tlm_generic_payload& gp, bool write, uint64_t address,
                     unsigned char* data, unsigned len) {
    gp.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
    gp.set_address(address);
    gp.set_data_ptr(data);
    gp.set_data_length(len);
    gp.set_streaming_width(len);
    gp.set_byte_enable_ptr(nullptr);
    gp.set_byte_enable_length(0);
    gp.set_dmi_allowed(false);
    gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
}

} // namespace stm32
#endif // STM32_COMMON_AHB_TYPES_H
