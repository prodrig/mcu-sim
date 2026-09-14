// =============================================================================
// mcu_caps.h — El DESCRIPTOR de un microcontrolador
//
// Junta en una sola pieza lo que distingue a un chip de otro de la misma
// familia: los rasgos del núcleo (core/core_caps.h), los del mapa de memoria
// (mem/mem_caps.h) y los límites de reloj. Es lo que hasta ahora estaba
// repartido entre constantes globales de `common/ahb_types.h`, argumentos de
// constructor y números escritos al vuelo.
//
// LO QUE ESTO ES Y LO QUE NO ES. Con un descriptor se construye un STM32F407VG,
// y con otro se construiría un F405 —el mismo silicio sin Ethernet ni cámara—,
// un F415 o un F407 de otro encapsulado. Lo que NO se construye cambiando
// números es un chip de otra familia: un F446 tiene otro árbol de reloj y
// periféricos que aquí no existen, y describirlo con un `McuCaps` distinto no
// lo convertiría en un F446, lo convertiría en un F407 con etiqueta falsa. La
// frontera exacta está en el campo `familia`: dos chips con la misma familia se
// distinguen con este struct; dos familias distintas necesitan modelo nuevo.
//
// Por eso este fichero declara UN descriptor completo —el del F407VG— y dos de
// laboratorio que la suite usa para comprobar que las piezas se recombinan
// (T127). No hay aquí ningún chip que el proyecto afirme modelar y no modele.
// =============================================================================
#ifndef STM32_TOP_MCU_CAPS_H
#define STM32_TOP_MCU_CAPS_H

#include "../core/core_caps.h"
#include "../mem/mem_caps.h"

namespace stm32 {

// Los topes de reloj por dominio. No son decoración: el RCC avisa cuando el
// firmware programa un árbol que los pasa, y ese aviso es de los que ahorran
// una tarde —un F401 a 168 MHz no existe, y el modelo tiene que decirlo—.
struct LimitesReloj {
    double sysclk_max, hclk_max, pclk1_max, pclk2_max;
};

struct McuCaps {
    const char*  nombre;       // "STM32F407VG", el que se escribe en el XML
    const char*  familia;      // "STM32F4": la frontera de lo recombinable
    CoreCaps     nucleo;
    MemCaps      memoria;
    LimitesReloj reloj;
};

inline constexpr LimitesReloj RELOJ_STM32F407VG {
    F_SYSCLK_MAX, F_HCLK_MAX, F_PCLK1_MAX, F_PCLK2_MAX
};

// --- El chip que este proyecto modela ---------------------------------------
inline constexpr McuCaps MCU_STM32F407VG {
    "STM32F407VG", "STM32F4",
    CORE_STM32F407VG,
    MEM_STM32F407VG,
    RELOJ_STM32F407VG
};

} // namespace stm32
#endif // STM32_TOP_MCU_CAPS_H
