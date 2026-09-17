// =============================================================================
// core_caps.h — Los RASGOS del núcleo: lo que cambia de un Cortex-M al de al lado
//
// Un Cortex-M4F no es un chip: es una licencia. Lo que ST, NXP o Microchip
// compran es el mismo núcleo, y lo que cada uno decide al integrarlo son cuatro
// números:
//
//   * CUÁNTAS LÍNEAS DE INTERRUPCIÓN saca el NVIC. El F407 lleva 82; un F401
//     lleva 62 y un M4 de otro fabricante puede llevar 32 o 240, que es el
//     máximo de la arquitectura;
//   * CUÁNTOS BITS DE PRIORIDAD implementa de los ocho que el registro enseña.
//     Esto no es cosmética: `NVIC_SetPriority(irq, 5)` escribe cosas distintas
//     y anida distinto según haya tres bits o cuatro, y es de los detalles que
//     más desconciertan a quien porta firmware de una familia a otra;
//   * CUÁNTAS REGIONES tiene el MPU, o si lo hay;
//   * SI HAY FPU, y si es de simple o doble precisión.
//
// Todo lo demás del núcleo —el juego de instrucciones, el modelo de
// excepciones, el mapa del PPB, la pila al entrar en un manejador— es idéntico,
// y por eso está escrito una sola vez y no se parametriza.
//
// POR QUÉ UN STRUCT Y NO UNA PLANTILLA. Es la misma receta que el resto del
// modelo lleva usando desde F4 (`UsartCaps`, `TimCaps`, `SpiCaps`, `DebugCaps`):
// un `struct` `constexpr`, unas instancias con nombre y unas clases que lo toman
// POR VALOR en el constructor. Así la elección se puede hacer en tiempo de
// compilación —con el alias de plantilla del final— o en tiempo de ejecución,
// que es lo que hace falta cuando el tipo de MCU viene de un fichero XML o de
// `--mcu`. Una plantilla obligaría a lo primero y cerraría lo segundo.
//
// EL COSTE DE HACERLO CONFIGURABLE, dicho sin adornos: los arrays de tamaño fijo
// del NVIC (`bool enabled_[82]`) pasan a ser `std::vector`, y eso es una
// indirección más por acceso. Se ha medido —suite completa, mismo tiempo
// simulado al picosegundo— y no se nota: el NVIC se consulta una vez por
// instrucción en el peor caso y el vector está siempre en caché. Lo que sí se
// ha cuidado es que no haya ninguna reserva de memoria en marcha: todo se
// dimensiona en el constructor, antes de `sc_start`.
// =============================================================================
#ifndef STM32_CORE_CORE_CAPS_H
#define STM32_CORE_CORE_CAPS_H

#include <cstdint>
#include "../common/ahb_types.h"

namespace stm32 {

// Precisión de la unidad de coma flotante.
enum class FpuKind : uint8_t {
    Ninguna,     // Cortex-M3, M0+: no hay CP10/CP11 y el CPACR no hace nada
    SimplePrec,  // FPv4-SP, el del Cortex-M4F
    DoblePrec    // FPv5-DP, el del Cortex-M7
};

struct CoreCaps {
    // --- El NVIC ------------------------------------------------------------
    unsigned n_irq;        // líneas externas. ARMv7-M admite hasta 240
    unsigned prio_bits;    // bits de prioridad IMPLEMENTADOS, de 8. Los de
                           // menos peso se leen SIEMPRE COMO CERO, que es lo
                           // que hace que `prioridad 5` no signifique lo mismo
                           // en dos chips con distinto número de bits

    // --- El MPU -------------------------------------------------------------
    unsigned mpu_regiones; // 0 = no hay MPU

    // --- La FPU -------------------------------------------------------------
    FpuKind  fpu;

    // --- Identificación -----------------------------------------------------
    // Lo que el firmware lee en SCB->CPUID. No es adorno: CMSIS y los
    // depuradores lo miran para decidir de qué núcleo se trata.
    uint32_t cpuid;

    // --- Derivados, para no repetir la cuenta en seis sitios ----------------
    // Total de excepciones = 16 de sistema + las IRQ.
    unsigned n_excepciones() const { return 16u + n_irq; }
    // La máscara que deja pasar solo los bits implementados: con 4 bits,
    // 0xF0; con 3, 0xE0. Escribir fuera de ella no tiene efecto y leer
    // devuelve cero, igual que en el silicio.
    uint8_t  prio_mask() const {
        return prio_bits >= 8 ? 0xFFu
                              : uint8_t(0xFFu << (8u - prio_bits));
    }
    // Cuántos niveles de prioridad distintos hay de verdad. Con 4 bits son 16,
    // con 3 son 8. Es el número que hay que saber para escribir firmware
    // portable, y el que casi nadie recuerda.
    unsigned n_niveles() const { return 1u << prio_bits; }
    bool     hay_mpu() const { return mpu_regiones > 0; }
    bool     hay_fpu() const { return fpu != FpuKind::Ninguna; }
};

// ---------------------------------------------------------------------------
// Las instancias con nombre
//
// `M4F_STM32F4` son los rasgos del núcleo tal y como ST lo integró en la
// familia F4 ENTERA: 4 bits de prioridad, 8 regiones de MPU y FPU de simple
// precisión son comunes a todos ellos. Lo único que cambia de un F4 a otro es
// el número de líneas, y por eso va como argumento.
//
// Los otros dos no son chips que este proyecto modele: son los rasgos que
// tendría el mismo núcleo integrado de otra manera, y están aquí porque son la
// demostración de que la parametrización sirve para algo —y porque la suite los
// usa para comprobarlo (T127)—. Que existan estas líneas no significa que haya
// un Cortex-M3 modelado; significa que el NVIC de este proyecto se puede
// construir con tres bits de prioridad y se comporta como debe.
// ---------------------------------------------------------------------------
constexpr uint32_t CPUID_CORTEX_M4  = 0x410FC241u;   // r0p1, PARTNO 0xC24
constexpr uint32_t CPUID_CORTEX_M3  = 0x410FC231u;   // r0p1, PARTNO 0xC23

constexpr CoreCaps core_m4f_st(unsigned n_irq) {
    return CoreCaps{ n_irq, 4, 8, FpuKind::SimplePrec, CPUID_CORTEX_M4 };
}

// El del STM32F407VG: 82 líneas [IR, §9.1.2]. Es el valor por omisión de todo
// el modelo, y por eso está aquí y no repartido por seis constructores.
inline constexpr CoreCaps CORE_STM32F407VG = core_m4f_st(N_IRQ);

// El del STM32F446: **97 posiciones de vector, 0 a 96**, de las cuales 86
// implementadas y once reservadas —61, 62, 79, 80, 82, 83, 85, 86, 88, 89 y
// 90— [RM0390, §10.1.3, leída fila a fila en la fase 0; vs_446re §15.3].
//
// El número que va aquí es el de POSICIONES y no el de líneas implementadas, y
// la diferencia importa: el NVIC dimensiona sus vectores con él, y una posición
// reservada no necesita nada especial —una línea que nadie gobierna nunca se
// pone pendiente, que es justo lo que hace un hueco de la tabla—. Dimensionar
// por las 86 implementadas dejaría al SPI4 (posición 84) fuera del NVIC.
//
// Los dos números de portada de ST no coinciden ni con la tabla ni entre sí
// —el RM dice 96 canales enmascarables y el datasheet dice «hasta 91»—, así que
// el modelo se fía de la tabla, que es el único documento autoconsistente y el
// que cuadra con el `IRQn_Type` de la cabecera de ST.
inline constexpr CoreCaps CORE_STM32F446 = core_m4f_st(97);

// Rasgos de laboratorio, para la verificación (véase el comentario de arriba).
inline constexpr CoreCaps CORE_M4F_MINIMO {
    32, 3, 8, FpuKind::SimplePrec, CPUID_CORTEX_M4
};
inline constexpr CoreCaps CORE_M3_SIN_FPU {
    48, 4, 0, FpuKind::Ninguna, CPUID_CORTEX_M3
};

} // namespace stm32
#endif // STM32_CORE_CORE_CAPS_H
