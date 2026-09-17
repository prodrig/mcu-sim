// =============================================================================
// crc_rng.h — COMPATIBILIDAD: el CRC y el RNG, que ya viven en ficheros aparte
//
// Los dos bloques estuvieron juntos aquí mientras el proyecto modelaba un solo
// chip, porque en el F407 van siempre juntos. Dejaron de ir juntos en cuanto
// hubo que mirar al F446, que lleva CRC y NO lleva RNG: un periférico que puede
// irse solo tiene que poder incluirse solo.
//
// Esta cabecera se queda para que nada que la incluyera se rompa, y no tiene
// nada más dentro. Lo nuevo debería incluir `crc.h` o `rng.h`, el que necesite.
// =============================================================================
#ifndef STM32_PERIPH_CRC_RNG_H
#define STM32_PERIPH_CRC_RNG_H

#include "crc.h"
#include "rng.h"

#endif // STM32_PERIPH_CRC_RNG_H
