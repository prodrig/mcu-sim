// =============================================================================
// stm32f446.h — EL STM32F446
//
// «El F407 menos lo que no tiene, más su árbol de reloj.» La fase 2 puso lo
// primero [doc/stm32f407vg_vs_446re.md, §17] —sin Ethernet, sin RNG, sin CCM,
// sin los bloques de extensión del I2S, con 512 KB de Flash, con el LQFP64 del
// F446 y con las 97 posiciones de vector— y la fase 3 lo segundo (§18): el
// tercer PLL, el divisor R, los nueve selectores de RCC_DCKCFGR y DCKCFGR2, y
// el over-drive del PWR con el que llega a 180 MHz.
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
// Lo que NO cabía en un descriptor era el árbol de reloj, y esa es la razón de
// que esta clase exista. La fase 3 lo ha escrito, y el matiz importa: lo que
// hace que ahora SÍ quepa en el descriptor el rasgo `ArbolReloj` es que el
// modelo IMPLEMENTA cada uno de sus cinco puntos. Un booleano que dijera «este
// chip tiene DCKCFGR» sin que nadie decodificara el registro sería justo la
// mentira que este proyecto lleva toda su vida quitando; un booleano que
// enciende un registro que existe, con sus nueve selectores cambiando
// frecuencias observables, es un rasgo.
//
// Y LO QUE SIGUE FALTANDO, LO DICE. `limitaciones()` lo enumera y `sim` lo
// imprime cada vez que monta una placa con un F446. Un modelo incompleto no es
// un problema; uno que no lo dice, sí.
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
            "los escalones de tension (PWR_CR.VOS) se leen y se escriben, pero "
            "el modelo NO limita la frecuencia por escala: en el silicio, la "
            "escala 2 y la 3 bajan el techo de SYSCLK, y aqui el unico techo "
            "que se mueve es el del over-drive",

            "RCC_CKGATENR se guarda y se devuelve, y no hace nada: son ocho "
            "bits de gating fino cuyo unico efecto observable es el consumo",

            "durante la conmutacion del over-drive (ODSWEN) el silicio PARA el "
            "reloj de sistema unos ciclos; el modelo espera el tiempo pero no "
            "lo para. Se nota solo si algo cuenta ciclos de HCLK a caballo de "
            "esa conmutacion",

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
