// =============================================================================
// bsl_p4.h — El BSL del MSP432P4 y el código de arranque de TI que lo llama
//            (T5, D4)
//
// EL BSL vive en la Flash de información, en el segundo banco:
// 0x0020_2000-0x0020_3FFF, 8 KB [OO: «los sectores 2 y 3 de la información
// son el BSL»; segunda fuente: el guion de enlazado del BSL a medida de TI,
// MSP432BSL_1_01_00_00, BSL_START en 0x0020_2000]. En 0x0020_2000 hay UNA
// palabra: la dirección de la función de entrada, BSL432_BSLentry(params).
// Detrás, en 0x0020_2010, cinco medias palabras de versión (vendedor,
// intérprete, API, periférico, compilación) [mismo guion]. Es lo que llama el
// código de arranque y lo que llama una aplicación que quiere entrar en el BSL:
//
//     ((void (*)(uint32_t))(*(uint32_t *)0x00202000))(0xFC48FFFF);
//
// LOS PARÁMETROS (r0): los bits 15:13 eligen la interfaz (7 automática,
// 6 UART, 5 SPI, 4 I2C) y los 25:16 la dirección de I2C [el BSL de TI,
// BSL432_PI_init]. El arranque pasa 0xFC48FFFF: interfaz automática y
// dirección 0x48, lo mismo que los ejemplos de TI para entrar desde una
// aplicación [1F: P-16 bb].
//
// TRES CONTENIDOS, como la ROM:
//
//   B1  el BSL SUSTITUTO, el de omisión: uno propio (verif/fw/msp432p4/bsl_b1,
//       AGPLv3), con el protocolo de TI por la UART del backchannel
//   B2  el de TI, de un fichero (`--bsl FICHERO`): el BSL432_MSP432P401.txt
//       (TI-TXT) o .out del paquete MSP432BSL de TI. Su licencia («TI Text
//       File License»: solo con dispositivos de TI, sin desensamblar) no deja
//       versionarlo ni llevarlo dentro: lo trae quien lo tenga
//   B3  sin BSL (`--bsl ninguno`): el sector en blanco
//
// EL CÓDIGO DE ARRANQUE de TI corre en el chip antes del vector de reset, en
// cada reset. El modelo lo hace de alto nivel, por `Cpu::arranque_fab`: si la
// palabra de 0x4 está en blanco (0xFFFFFFFF: un chip sin programar) y hay
// BSL, el núcleo sale del reset en la entrada del BSL, con r0 = 0xFC48FFFF y
// la pila en lo alto de la SRAM. Lo demás del código de arranque -el buzón de
// la Flash, la invocación por un pin, el bloqueo de JTAG y las zonas IP- no
// está [P-16 bb].
// =============================================================================
#ifndef STM32_SOC_MSP432P4_BSL_P4_H
#define STM32_SOC_MSP432P4_BSL_P4_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "p4_desc.h"
#include "../../common/imagen_fw.h"

namespace stm32 {
namespace p4 {
constexpr uint32_t BSL_B         = 0x00202000u;
constexpr uint32_t BSL_SIZE      = 0x00002000u;
constexpr uint32_t BSL_PARAMS    = 0xFC48FFFFu;   // automática, I2C 0x48
}

namespace bsl_b1 {
// La imagen del BSL B1: las palabras de 0x0020_2000 en adelante (lo que no
// lleva queda en 0xFF). La genera el Makefile de verif/fw/msp432p4/bsl_b1; su
// huella, la de los 8 KB, está en verif/fw/huellas.txt.
inline const uint32_t IMAGEN[] = {
#include "bsl_b1.inc"
};
static_assert(sizeof(IMAGEN) == 4u * BSL_B1_PALABRAS, "bsl_b1.inc mal generado");
static_assert(sizeof(IMAGEN) <= p4::BSL_SIZE, "el BSL B1 no cabe en sus 8 KB");
inline constexpr uint32_t FNV = BSL_B1_FNV;
inline constexpr bool HAY = true;
} // namespace bsl_b1

// Lo que se pone en el sector del BSL: los 8 KB, de una imagen o de un
// fichero. Devuelve el error, o "".
inline std::string bsl_de_fichero(const std::string& ruta, std::vector<uint8_t>& sector) {
    std::vector<TrozoFw> t;
    std::string e;
    switch (formato_fw(ruta)) {
        case FormatoFw::Elf:      e = lee_elf(ruta, t);   break;
        case FormatoFw::TiTxt:    e = lee_titxt(ruta, t); break;
        case FormatoFw::IntelHex: e = lee_ihex(ruta, t);  break;
        default: return ruta + ": el BSL va en TI-TXT, Intel HEX o ELF (el .txt o el "
                               ".out del paquete MSP432BSL de TI)";
    }
    if (!e.empty()) return e;
    sector.assign(p4::BSL_SIZE, 0xFF);
    bool entrada = false;
    for (const TrozoFw& tr : t) {
        // Lo que va a la SRAM (.data, .bss, .vtable de un .out) lo pone su
        // propio arranque: no es del sector
        if (tr.dir >= 0x20000000u && tr.dir < 0x40000000u) continue;
        if (tr.dir < p4::BSL_B || uint64_t(tr.dir) + tr.datos.size() > uint64_t(p4::BSL_B) + p4::BSL_SIZE)
            return ruta + ": un trozo cae fuera del sector del BSL (0x0020_2000-0x0020_3FFF); "
                          "no es un BSL del MSP432P4";
        std::memcpy(sector.data() + (tr.dir - p4::BSL_B), tr.datos.data(), tr.datos.size());
        if (tr.dir <= p4::BSL_B && tr.dir + tr.datos.size() >= p4::BSL_B + 4) entrada = true;
    }
    if (!entrada) return ruta + ": no lleva la palabra de entrada de 0x0020_2000";
    return "";
}

} // namespace stm32
#endif // STM32_SOC_MSP432P4_BSL_P4_H
