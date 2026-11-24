// =============================================================================
// inventario_pines.h — Lo que el MODELO sabe de los pines de un chip:
// `--exporta-mcu TIPO`
//
// Fase B2 de `doc/analisis_build.md`. mcu-sim-build necesita, de cada MCU, su
// patillaje y lo que puede hacer cada pin (§4.3 del análisis). Eso sale de
// fuera -STM32 Open Pin Data, SysConfig, los datasheets: fases B3 y B4-,
// porque el modelo no sabe en qué patilla del plástico cae cada pad. Lo que
// SÍ sabe, y nadie más, es qué funciones SIMULA. Eso es lo que se vuelca aquí,
// para que B3 y B4 lo contrasten con su fuente: una función que el modelo
// registra y el datasheet no tiene -o con otro número de AF- es un error de
// uno de los dos, y una que el datasheet tiene y el modelo no se enseñará en
// gris («existe y no se simula»).
//
// QUÉ SE SABE DE CADA FAMILIA. Depende de cómo registra su modelo las
// funciones de sus pines, y por eso cada `mcu_if` rellena su inventario:
//
//   STM32F4, F446, F3  la tabla del `PinMux`: (pad, AF) -> señales. El F3 sabe
//                      además cómo se llama cada función -su tabla sale de
//                      Open Pin Data-; el F4, no: la clave para contrastar es
//                      el número de AF, y la señal dice de qué periférico es
//   MSP432P4           las funciones fijas del DIO por (pad, PxSEL) y las
//                      mapeables por el PMAP, cada una con su nombre
//   MSP432E4           las del GPIO por (pad, PCTL), y las analógicas
//   ESP32-C3/C6        la función 0 del IO_MUX de cada pin. La MATRIZ no:
//                      sus señales no están en ninguna tabla del modelo
//   MSP430             nada todavía: cada SoC decide la función de un pin con
//                      su código (PxSEL, PxSEL2...), sin tabla que recorrer.
//                      Su inventario dice por qué (`motivo`), y B4 lo resuelve
//
// EL FORMATO (`version="1"`):
//
//   <mcu tipo="STM32F446RE" familia="STM32F446" encapsulado="LQFP64"
//        patillas="64" gpio="50" encapsulado_verificado="si" version="1">
//     <pad nombre="PA5">
//       <funcion modo="1" senal="tim2.ch1_out"/>
//       <funcion modo="5" nombre="SPI1_SCK" senal="spi1.sck_out"/>
//     </pad>
//     <alimentacion nombre="VDD"/>
//     <mapeo tipo="pmap" puertos="P2 P3 P7">
//       <funcion modo="3" nombre="UCA0TXD" senal="eusci_a0.simo_out"/>
//     </mapeo>
//     <sin_inventario motivo="..."/>          (si la familia no lo tiene)
//   </mcu>
//
// `modo` es el número con el que el firmware elige la función: la AF de un
// STM32, el PxSEL de un MSP432P4 (1 primaria, 2 secundaria, 3 terciaria), el
// PCTL de un MSP432E4, la función del IO_MUX de un ESP32, o el valor de
// PxMAPy de una mapeable. `senal` es la señal de SystemC del modelo, sin el
// nombre del chip delante. `analogica="si"`, una función analógica.
//
// SOLO LOS PADS QUE SALEN al encapsulado: los que dice su `Encapsulado`. Y lo
// comprueba: que sean tantos como `n_gpio`, y que cada uno sea un nodo del
// chip. Si no cuadra, `--exporta-mcu` falla: un patillaje que no cuadra con
// el modelo no se puede contrastar con nada.
// =============================================================================
#ifndef STM32_SOC_INVENTARIO_PINES_H
#define STM32_SOC_INVENTARIO_PINES_H

#include <string>
#include <vector>
#include <systemc>

namespace stm32 {

// Una función que el modelo simula en un pin
struct FuncionDePin {
    unsigned    puerto = 0, pin = 0;   // el pad (en las mapeables, no se usa)
    int         modo = -1;             // AF, PxSEL, PCTL, IO_MUX o PxMAPy
    std::string nombre;                // como la llama el manual, si se sabe
    std::string senal;                 // la señal de SystemC del modelo
    bool        analogica = false;
};

struct InventarioPines {
    bool        hay = false;           // false: la familia no tiene inventario
    std::string motivo;                // por qué no
    std::vector<FuncionDePin> fijas;
    // Las funciones que no van a un pin fijo: las del PMAP o de una matriz
    std::string mapeo;                 // "", "pmap" o "matriz"
    std::string puertos_mapeables;     // "P2 P3 P7"
    bool        mapeo_inventariado = true;
    std::string motivo_mapeo;          // si no lo está, por qué
    std::vector<FuncionDePin> mapeables;
};

// El nombre de una señal de SystemC sin el del chip delante: con el chip
// construido como «x», «x.usart2.tx_out» -> «usart2.tx_out»
inline std::string senal_sin_chip(const sc_core::sc_object* s, const std::string& chip) {
    if (!s) return std::string();
    std::string n = s->name();
    const std::string pre = chip + ".";
    if (n.compare(0, pre.size(), pre) == 0) n.erase(0, pre.size());
    return n;
}

// La primera señal que haya de las tres de una función: la de salida, la de
// entrada, la de habilitación
template <class F>
inline std::string senal_de(const F& f, const std::string& chip) {
    if (f.out) return senal_sin_chip(f.out, chip);
    if (f.in)  return senal_sin_chip(f.in, chip);
    if (f.oe)  return senal_sin_chip(f.oe, chip);
    return std::string();
}

} // namespace stm32
#endif // STM32_SOC_INVENTARIO_PINES_H
