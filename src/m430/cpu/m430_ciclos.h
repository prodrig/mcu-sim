// =============================================================================
// m430_ciclos.h — Los ciclos de MCLK de cada instrucción: tres tablas
//
// M1 de doc/analisis_msp430.md, §3.2. «El número de ciclos depende del
// formato y de los modos de direccionamiento, no de la instrucción» [UG2
// §3.4.4]. Cada fila de cada tabla está copiada de su fuente, con el número
// de la tabla al lado:
//
//   * CPU    : UG2 (SLAU144K) tablas 3-14, 3-15 y 3-16;
//   * CPUX   : UG2 tablas 4-8, 4-9, 4-10, 4-17, 4-18 y 4-19 (la CPUX de las
//              2xx/4xx grandes);
//   * CPUXV2 : SLAU391F (el capítulo CPUX de SLAU208) tablas 1-8, 1-9, 1-10,
//              1-17, 1-18 y 1-19. Las de SLAU367 (FR5xx) y SLAU445 (FR4xx/
//              FR2xx) son la misma tabla: la CPUXV2 es una.
//
// Los modos de origen, en este orden: Rn, @Rn, @Rn+, #N, X(Rn), EDE (X(PC)),
// &EDE. Un #N del generador de constantes (#0, #1, #2, #4, #8, #-1) cuenta
// como Rn: no lee memoria ni ocupa palabra [UG2 §3.2.4]. Los de destino:
// Rm, PC, X(Rm), EDE, &EDE.
// =============================================================================
#ifndef STM32_M430_CPU_M430_CICLOS_H
#define STM32_M430_CPU_M430_CICLOS_H

#include <cstdint>
#include "m430_caps.h"

namespace stm32 {

enum M430ModoOrigen : uint8_t { MO_RN = 0, MO_IND, MO_INC, MO_INM, MO_IDX, MO_SIM, MO_ABS };
enum M430ModoDestino : uint8_t { MD_RM = 0, MD_PC, MD_IDX, MD_SIM, MD_ABS };

struct M430Ciclos {
    const char* fuente;
    // MSP430 (sin palabra de extensión)
    uint8_t f1[7][5];          // formato I
    bool    f1_mbc_menos1;     // MOV, BIT y CMP, un ciclo menos con destino en memoria
    uint8_t f2_rot[7];         // RRA, RRC, SWPB, SXT
    uint8_t f2_push[7];
    uint8_t f2_call[7];
    bool    f2_idx_sp_mas1;    // PUSH y CALL X(SP): un ciclo más
    uint8_t reti, irq, salto;
    // MSP430X
    uint8_t x1_bw[7][5], x1_a[7][5];     // formato I extendido; 0 = no existe
    uint8_t x2_rot_bw[7], x2_rot_a[7];   // RRAX, RRCX (y RRUX, SWPBX, SXTX); Rn = 1 + n
    uint8_t x2_push_bw[7], x2_push_a[7]; // PUSHX
    uint8_t calla[7];
    uint8_t mova[7][5];                  // MOVA y BRA
    uint8_t cmpa[2][2];                  // CMPA, ADDA, SUBA: [Rn, #N][Rm, PC]
};

// --- CPU: UG2 cap. 3 -------------------------------------------------------------
inline constexpr M430Ciclos M430_CICLOS_CPU = {
    "SLAU144K tablas 3-14, 3-15 y 3-16",
    {   //  Rm PC X EDE &       (tabla 3-16)
        {1, 2, 4, 4, 4},     // Rn
        {2, 2, 5, 5, 5},     // @Rn
        {2, 3, 5, 5, 5},     // @Rn+
        {2, 3, 5, 5, 5},     // #N
        {3, 3, 6, 6, 6},     // X(Rn)
        {3, 3, 6, 6, 6},     // EDE
        {3, 3, 6, 6, 6},     // &EDE
    },
    false,
    // Rn @Rn @Rn+ #N X EDE &   (tabla 3-15; #N de RRA..SXT: «impredecible», como @Rn+)
    {1, 3, 3, 3, 4, 4, 4},
    {3, 4, 5, 4, 5, 5, 5},
    {4, 4, 5, 5, 5, 5, 5},
    false,
    5, 6, 2,                 // tabla 3-14 y §3.4.4.3
    {}, {}, {}, {}, {}, {}, {}, {}, {},
};

// --- CPUX de 2xx/4xx: UG2 cap. 4 -------------------------------------------------
inline constexpr M430Ciclos M430_CICLOS_CPUX = {
    "SLAU144K tablas 4-8, 4-9, 4-10, 4-17, 4-18 y 4-19",
    {   //  Rm PC X EDE &       (tabla 4-10)
        {1, 2, 4, 4, 4},
        {2, 3, 5, 5, 5},
        {2, 3, 5, 5, 5},
        {2, 3, 5, 5, 5},
        {3, 3, 6, 6, 6},
        {3, 3, 6, 6, 6},
        {3, 3, 6, 6, 6},
    },
    true,
    // (tabla 4-9; X(SP) en PUSH y CALL: 5, nota 2)
    {1, 3, 3, 3, 4, 4, 4},
    {3, 3, 3, 3, 4, 4, 4},
    {3, 4, 4, 4, 4, 4, 4},
    true,
    3, 5, 2,                 // tabla 4-8
    {   // .B/.W (tabla 4-18)
        {2, 3, 5, 5, 5},
        {3, 3, 6, 6, 6},
        {3, 4, 6, 6, 6},
        {3, 4, 6, 6, 6},
        {4, 5, 7, 7, 7},
        {4, 5, 7, 7, 7},
        {4, 5, 7, 7, 7},
    },
    {   // .A
        {2, 3, 7, 7, 7},
        {4, 4, 9, 9, 9},
        {4, 5, 9, 9, 9},
        {3, 4, 8, 8, 8},
        {5, 6, 10, 10, 10},
        {5, 6, 10, 10, 10},
        {5, 6, 10, 10, 10},
    },
    // (tabla 4-17)
    {1, 4, 4, 0, 5, 5, 5},
    {1, 6, 6, 0, 7, 7, 7},
    {4, 4, 4, 4, 5, 5, 5},
    {5, 6, 6, 6, 7, 7, 7},
    {4, 5, 5, 4, 6, 6, 6},
    {   // MOVA (tabla 4-19)
        {1, 2, 4, 4, 4},
        {3, 3, 0, 0, 0},
        {3, 3, 0, 0, 0},
        {2, 3, 0, 0, 0},
        {4, 4, 0, 0, 0},
        {4, 4, 0, 0, 0},
        {4, 4, 0, 0, 0},
    },
    {{1, 2}, {3, 3}},
};

// --- CPUXV2: SLAU391F (SLAU208 cap. 1) ------------------------------------------
inline constexpr M430Ciclos M430_CICLOS_CPUXV2 = {
    "SLAU391F tablas 1-8, 1-9, 1-10, 1-17, 1-18 y 1-19",
    {   //  Rm PC X EDE &       (tabla 1-10)
        {1, 3, 4, 4, 4},
        {2, 4, 5, 5, 5},
        {2, 4, 5, 5, 5},
        {2, 3, 5, 5, 5},
        {3, 5, 6, 6, 6},
        {3, 5, 6, 6, 6},
        {3, 5, 6, 6, 6},
    },
    true,
    // (tabla 1-9)
    {1, 3, 3, 3, 4, 4, 4},
    {3, 3, 3, 3, 4, 4, 4},
    {4, 4, 4, 4, 5, 5, 6},
    false,
    5, 6, 2,                 // tabla 1-8
    {   // .B/.W (tabla 1-18)
        {2, 4, 5, 5, 5},
        {3, 5, 6, 6, 6},
        {3, 5, 6, 6, 6},
        {3, 4, 6, 6, 6},
        {4, 6, 7, 7, 7},
        {4, 6, 7, 7, 7},
        {4, 6, 7, 7, 7},
    },
    {   // .A
        {2, 4, 7, 7, 7},
        {4, 6, 9, 9, 9},
        {4, 6, 9, 9, 9},
        {3, 4, 8, 8, 8},
        {5, 7, 10, 10, 10},
        {5, 7, 10, 10, 10},
        {5, 7, 10, 10, 10},
    },
    // (tabla 1-17)
    {1, 4, 4, 0, 5, 5, 5},
    {1, 6, 6, 0, 7, 7, 7},
    {4, 4, 4, 4, 5, 5, 5},
    {5, 6, 6, 5, 7, 7, 7},
    {5, 6, 6, 5, 5, 7, 7},
    {   // MOVA (tabla 1-19)
        {1, 3, 4, 4, 4},
        {3, 5, 0, 0, 0},
        {3, 5, 0, 0, 0},
        {2, 3, 0, 0, 0},
        {4, 6, 0, 0, 0},
        {4, 6, 0, 0, 0},
        {4, 6, 0, 0, 0},
    },
    {{1, 3}, {3, 3}},
};

inline const M430Ciclos& m430_ciclos(M430Arq a) {
    return a == M430Arq::CPU ? M430_CICLOS_CPU : a == M430Arq::CPUX ? M430_CICLOS_CPUX : M430_CICLOS_CPUXV2;
}

} // namespace stm32
#endif // STM32_M430_CPU_M430_CICLOS_H
