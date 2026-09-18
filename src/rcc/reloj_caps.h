// =============================================================================
// reloj_caps.h — Los RASGOS del árbol de reloj: qué tiene el de este chip
//
// Mismo sitio en el diseño que `core/core_caps.h`, `mem/mem_caps.h`,
// `pins/encapsulado.h` y `bus/conectividad.h`: los rasgos de un subsistema
// viven con el subsistema, y `McuCaps` los junta. Aquí hay dos cosas.
//
// LOS TOPES, que ya existían, y que en el F446 **dependen del estado**. Esa es
// la novedad y no es menor: hasta ahora `LimitesReloj` era una constante del
// chip —un F407 no pasa de 168 MHz y punto—, y en el F446 los topes son 168 /
// 42 / 84 **sin** over-drive y 180 / 45 / 90 **con** él [DS10693, tabla 16]. El
// PWR decide cuál rige, y el RCC lo pregunta. Un modelo que usara siempre los
// altos aceptaría en silencio un árbol que el silicio no arranca.
//
// EL ÁRBOL, que es nuevo. Cinco rasgos, y cada uno gobierna código de verdad:
// un registro que se decodifica o no, un PLL que existe o no, un divisor que
// está o no está. No son etiquetas: si `dckcfgr` fuera `true` sin que el modelo
// implementara los selectores, el firmware escribiría `RCC_DCKCFGR` y el modelo
// no haría nada con él — que es exactamente la mentira que este proyecto
// persigue [doc/stm32f407vg_vs_446re.md, §13].
//
// POR QUÉ RASGOS Y NO DOS CLASES DE RCC. Porque lo que cambia entre el árbol
// del F407 y el del F446 son **añadidos**, no otra cosa: los mismos cuatro
// osciladores, el mismo mux de SYSCLK, los mismos prescalers de AHB y APB, el
// mismo gating por periférico y los mismos flags de RCC_CIR. Duplicar 930
// líneas para añadir un PLL y cinco selectores es la receta para que las dos
// copias se separen. Lo que sí hace falta —y es lo que la fase 3 hace— es que
// **cada rasgo apague o encienda algo que se puede observar**.
// =============================================================================
#ifndef STM32_RCC_RELOJ_CAPS_H
#define STM32_RCC_RELOJ_CAPS_H

#include "../common/ahb_types.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Los topes por dominio
//
// `*_od` son los topes CON over-drive. A cero significa que este chip no lo
// tiene, y entonces rigen siempre los de siempre. No es un caso especial en el
// código: `tope_hclk(false)` y `tope_hclk(true)` devuelven lo mismo cuando no
// hay over-drive, que es justo lo que pasa en un F407.
// ---------------------------------------------------------------------------
struct LimitesReloj {
    double sysclk_max, hclk_max, pclk1_max, pclk2_max;
    double sysclk_max_od, hclk_max_od, pclk1_max_od, pclk2_max_od;

    bool hay_over_drive() const { return hclk_max_od > 0.0; }
    double tope_sysclk(bool od) const {
        return (od && sysclk_max_od > 0.0) ? sysclk_max_od : sysclk_max;
    }
    double tope_hclk(bool od) const {
        return (od && hclk_max_od > 0.0) ? hclk_max_od : hclk_max;
    }
    double tope_pclk1(bool od) const {
        return (od && pclk1_max_od > 0.0) ? pclk1_max_od : pclk1_max;
    }
    double tope_pclk2(bool od) const {
        return (od && pclk2_max_od > 0.0) ? pclk2_max_od : pclk2_max;
    }
};

// ---------------------------------------------------------------------------
// Qué tiene el árbol de ESTE chip
//
//   pll_r          El PLL principal saca una tercera frecuencia, R. En el F446
//                  alimenta al SPDIF-RX, al I2S y al SAI. [PLLCFGR bits 30:28]
//   plli2s_propio  El PLLI2S tiene M, P, Q y R PROPIOS. En el F407 comparte la
//                  M del PLL principal y solo saca R, lo que ata las dos VCO:
//                  cambiar la M del PLL le cambia la frecuencia al I2S.
//   pllsai         Hay un TERCER PLL, con M, N, P y Q. (Sin R: el F446 no lleva
//                  LTDC, que es quien la usaría.)
//   dckcfgr        Existen RCC_DCKCFGR (0x8C), RCC_CKGATENR (0x90) y
//                  RCC_DCKCFGR2 (0x94), y con ellos los nueve selectores que
//                  deciden de dónde come cada periférico.
//   over_drive     El PWR tiene over-drive, y con él los topes suben.
//
// Ninguno de los cinco es independiente de los demás EN EL SILICIO —los llevan
// juntos las mismas piezas— pero se declaran por separado porque cada uno
// gobierna un trozo distinto del modelo, y así se ve en el código qué línea
// depende de qué. Las dos instancias con nombre, abajo, son las que existen.
// ---------------------------------------------------------------------------
struct ArbolReloj {
    bool pll_r;
    bool plli2s_propio;
    bool pllsai;
    bool dckcfgr;
    bool over_drive;
};

// El F405/407: dos PLL, sin selectores dedicados y sin over-drive.
inline constexpr ArbolReloj ARBOL_STM32F4 { false, false, false, false, false };
// El F446: tres PLL, divisor R, los dos DCKCFGR y over-drive.
inline constexpr ArbolReloj ARBOL_STM32F446 { true, true, true, true, true };

inline constexpr LimitesReloj RELOJ_STM32F407VG {
    F_SYSCLK_MAX, F_HCLK_MAX, F_PCLK1_MAX, F_PCLK2_MAX,
    0, 0, 0, 0                      // sin over-drive
};

// El F446, con las dos columnas de [DS10693, tabla 16]: sin over-drive es un
// F407 —168 / 42 / 84— y con él sube a 180 / 45 / 90.
inline constexpr LimitesReloj RELOJ_STM32F446 {
    168e6, 168e6, 42e6, 84e6,
    180e6, 180e6, 45e6, 90e6
};

} // namespace stm32
#endif // STM32_RCC_RELOJ_CAPS_H
