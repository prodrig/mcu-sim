// =============================================================================
// f446_mapa_perif.h — DÓNDE ESTÁ LO QUE SOLO TIENE EL F446
//
// El hermano de `f4_mapa_perif.h`, y por la misma razón: una dirección es un
// rasgo del chip, no de la arquitectura. Aquí están las siete que el F407 no
// tiene, y solo esas — todo lo demás del mapa es idéntico en las dos piezas,
// cosa que se comprobó macro a macro en la fase 0 [vs_446re, §5.3].
//
// TODAS VERIFICADAS CONTRA `stm32f446xx.h`, que es código del fabricante, y no
// contra la tabla de fronteras del reference manual: la revisión 4 de esa tabla
// **no lista el FMPI2C1** —deja sin nombrar el rango donde la cabecera de ST lo
// pone— y el capítulo 23 del mismo manual describe el periférico entero. Cuando
// dos documentos de ST no coinciden, este modelo se fía del que es código.
//
// Y LOS VECTORES, que van aquí por lo mismo: son posiciones de la tabla de este
// chip. Ninguna de ellas existe en el F407, que se acaba en la 81.
// =============================================================================
#ifndef STM32_SOC_F446_MAPA_PERIF_H
#define STM32_SOC_F446_MAPA_PERIF_H

#include "../common/ahb_types.h"

namespace stm32 {
namespace addr446 {

// --- APB1 -------------------------------------------------------------------
// OJO CON ESTA: 0x4000_4000 es el I2S3ext en el F407 y el SPDIF-RX aquí. Es la
// única dirección de todo el mapa que cambia de dueño entre las dos piezas, y
// el documento la marcó como «el error más caro que puede colarse en el
// puerto», porque no falla: contesta otro periférico.
constexpr uint32_t SPDIFRX_B = 0x40004000;
constexpr uint32_t FMPI2C1_B = 0x40006000;
constexpr uint32_t CEC_B     = 0x40006C00;
// --- APB2 -------------------------------------------------------------------
constexpr uint32_t SPI4_B    = 0x40013400;
constexpr uint32_t SAI1_B    = 0x40015800;
constexpr uint32_t SAI2_B    = 0x40015C00;
// --- AHB3 -------------------------------------------------------------------
// El QUADSPI comparte el puerto de esclavo de la matriz con el FMC: en el F446
// el séptimo esclavo es «FMC / QUADSPI» y no dos [RM0390, §2.1; vs_446re §15.1].
constexpr uint32_t QUADSPI_B   = 0xA0001000;
constexpr uint32_t QUADSPI_MEM = 0x90000000;   // la ventana mapeada en memoria
constexpr uint32_t QUADSPI_MEM_SIZE = 0x10000000u;

// --- Las posiciones de vector [RM0390, §10.1.3; CMSIS IRQn_Type] ------------
// De la 84 a la 96. El F407 llega a la 81, así que ninguna se solapa: el enum
// de interrupciones se EXTIENDE, no se reescribe.
constexpr unsigned IRQ_SPI4        = 84;
constexpr unsigned IRQ_SAI1        = 87;
constexpr unsigned IRQ_SAI2        = 91;
constexpr unsigned IRQ_QUADSPI     = 92;
constexpr unsigned IRQ_CEC         = 93;
constexpr unsigned IRQ_SPDIFRX     = 94;
constexpr unsigned IRQ_FMPI2C1_EV  = 95;
constexpr unsigned IRQ_FMPI2C1_ER  = 96;

} // namespace addr446
} // namespace stm32
#endif // STM32_SOC_F446_MAPA_PERIF_H
