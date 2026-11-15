// =============================================================================
// m430_errata.h — Las erratas de CPU de los MSP430 (M1, §3.3 de
// doc/analisis_msp430.md)
//
// devices.csv dice qué erratas de CPU tiene cada chip (CPU_Bugs; en el
// descriptor, `errata`). Las de este plan son tres, en todas las CPUXV2:
//
//   CPU21  POPM con el SR (o el PC) entre los registros que restaura: el
//          chip puede quedarse colgado [SLAZ* de las FR y la F5529]. AVISA
//          una vez; no se cuelga [declarado].
//   CPU22  Modo indirecto con el PC como origen (@PC, As = 10): resultado
//          inesperado. AVISA una vez; se ejecuta como dice la guía (lee la
//          palabra siguiente sin avanzar) [declarado].
//   CPU40  Un salto seguido de datos con ciertos códigos puede corromper el
//          PC. No se modela: depende de lo que haya detrás del salto en la
//          memoria, que el compilador ya evita [declarado].
//
// La F5529 tiene además CPU23, CPU27, CPU29 y CPU30 (de la palabra de
// extensión, los modos de bajo consumo y el TA): ninguna cambia lo que
// computa un programa bien escrito: se documentan y no se modelan.
//
// Todas se pueden apagar desde el descriptor (quitando el bit), como en el C3.
// =============================================================================
#ifndef STM32_M430_CPU_M430_ERRATA_H
#define STM32_M430_CPU_M430_ERRATA_H

#include <cstdint>
#include <cstdio>
#include "../../soc/msp430/desc/desc430.h"

namespace stm32 {

struct M430Errata {
    uint32_t activas = 0;
    uint32_t avisadas = 0;
    // Avisa una vez por errata (si está activa). true si está activa.
    bool avisa(uint32_t bit, const char* que, uint32_t pc) {
        if (!(activas & bit)) return false;
        if (!(avisadas & bit)) {
            avisadas |= bit;
            std::fprintf(stderr, "[m430] errata %s en pc=0x%05X (aviso una vez)\n", que, unsigned(pc));
        }
        return true;
    }
};

} // namespace stm32
#endif // STM32_M430_CPU_M430_ERRATA_H
