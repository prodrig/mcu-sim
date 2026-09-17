// =============================================================================
// stm32f446.h — EL STM32F446, fase 2: el esqueleto
//
// «El F407 menos lo que no tiene.» Eso es literalmente lo que es hoy, y lo que
// la fase 2 del plan pedía que fuera [doc/stm32f407vg_vs_446re.md, §12]: sin
// Ethernet, sin RNG, sin CCM, sin los bloques de extensión del I2S, con 512 KB
// de Flash, con el LQFP64 del F446 —que no es el del F405— y con las 97
// posiciones de vector.
//
// POR QUÉ ES UNA CLASE Y NO UNA FILA MÁS DEL CATÁLOGO. Es la pregunta que este
// fichero tiene que contestar, porque la respuesta fácil —«un descriptor más y
// ya»— es la que el plan marcó como el tercer riesgo del puerto.
//
// Casi todo lo que distingue a un F446 de un F407 SÍ cabe en un descriptor: la
// Flash, la RAM, los pads, las posiciones de vector, qué bloques faltan, el
// IDCODE, cuántos maestros tiene la matriz. Todo eso está en `MCU_STM32F446RE`
// y no hay una línea de código nueva para ello: lo construye `SocF4`, el mismo
// die de siempre, leyendo el descriptor.
//
// Lo que NO cabe es el árbol de reloj. El F446 tiene un tercer PLL, un divisor
// R que el F407 no tiene, un PLLI2S con M/P/Q propios y **cinco registros de
// selección de reloj que en el F407 no existen** (`RCC_DCKCFGR`,
// `RCC_DCKCFGR2`). Un descriptor no puede describir un registro que no está: si
// se intentara, el modelo aceptaría que el firmware escribiera en `DCKCFGR`, no
// haría nada con él y no lo diría — que es exactamente la clase de mentira que
// este proyecto lleva toda su vida quitando. De ahí esta clase: es el sitio
// donde la fase 3 pondrá ese árbol de reloj.
//
// Y MIENTRAS TANTO, LO DICE. Hoy esta clase todavía no lo ha hecho, así que su
// `limitaciones()` lo declara en voz alta y `sim` lo imprime cada vez que monta
// una placa con un F446. Un modelo incompleto no es un problema; un modelo
// incompleto que no lo dice, sí.
// =============================================================================
#ifndef STM32_SOC_STM32F446_H
#define STM32_SOC_STM32F446_H

#include "stm32f4_mcu.h"

namespace stm32 {

class Stm32F446 : public SocF4 {
public:
    explicit Stm32F446(sc_core::sc_module_name nm, DebugCaps dbg = DBG_PINES,
                       const Cableado& cab = Cableado(),
                       McuCaps caps = MCU_STM32F446RE)
        : SocF4(nm, dbg, cab, caps) {}

    // -----------------------------------------------------------------------
    // La deuda, enumerada. Cada línea es una cosa que el silicio hace y este
    // modelo todavía no, y cada una tiene su fase en el plan.
    //
    // No están aquí las diferencias que el modelo SÍ cubre —la Flash de 512 KB,
    // la ausencia de CCM, de Ethernet, de RNG y de los I2Sext, el LQFP64 propio,
    // los siete maestros de la matriz, las 97 posiciones de vector, el IDCODE—,
    // porque decir de una cosa terminada que falta es tan inexacto como callar
    // una que falta de verdad.
    // -----------------------------------------------------------------------
    std::vector<std::string> limitaciones() const override {
        return {
            "el arbol de reloj es todavia el del F407: no hay tercer PLL "
            "(PLLSAI), ni divisor R, ni M/P/Q propios del PLLI2S, y "
            "RCC_DCKCFGR y RCC_DCKCFGR2 no existen en el modelo. Un firmware "
            "que los programe no obtendra el reloj que pide [fase 3]",

            "los topes de 180/45/90 MHz no EXIGEN la secuencia de over-drive "
            "del PWR: el modelo llega a 180 MHz sin pasar por ODRDY, y el "
            "silicio no [fase 3]",

            "los seis perifericos que el F446 tiene y el F407 no -SAI1, SAI2, "
            "SPDIF-RX, QUADSPI, FMPI2C1 y HDMI-CEC- no estan modelados, ni "
            "tampoco SPI4. Sus ventanas son espacio reservado, asi que "
            "tocarlas da error de bus en vez de contestar [fase 4]",

            "OJO con 0x4000_4000: en el F407 es el I2S3ext y en el F446 es el "
            "SPDIF-RX. Aqui, hoy, no hay ninguno de los dos, que es lo unico "
            "honesto que se puede hacer mientras el segundo no este [fase 4]",

            "el FMC no esta modelado como tal (el modelo lleva el FSMC del "
            "F407, con otros desplazamientos de registro por banco). En un "
            "F446RE da igual: el datasheet dice que ese encapsulado no saca el "
            "bus externo, asi que su ventana es espacio reservado [fase 4]",

            "las posiciones de vector 84 a 96 existen en el NVIC -se pueden "
            "habilitar y priorizar- pero hoy no hay ningun periferico que las "
            "levante, porque son las de esos seis bloques [fase 4]"
        };
    }
};

// La segunda familia. A partir de aqui `tipo=` en el XML DESPACHA de verdad:
// `STM32F407VG` y `STM32F446RE` no son el mismo objeto de C++.
REGISTRA_MCU(STM32F446, [](const char* nm, DebugCaps d, const Cableado& c,
                           const McuCaps& caps) -> mcu_if* {
    return new AdaptadorF4<Stm32F446>(nm, d, c, caps);
});

} // namespace stm32
#endif // STM32_SOC_STM32F446_H
