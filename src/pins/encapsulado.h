// =============================================================================
// encapsulado.h — El ENCAPSULADO como dato: qué pads salen de verdad al plástico
//
// El silicio del STM32F405/407 tiene nueve puertos de dieciséis pines. Lo que
// llega al exterior depende del encapsulado, y no es un detalle cosmético: un
// firmware que configura PE2 funciona en un LQFP100 y **no tiene dónde salir**
// en un LQFP64. Esa es la clase de error que el alumno no ve en el depurador y
// que en la tarjeta se manifiesta como «no hace nada».
//
// Hasta ahora esto era una FUNCIÓN ESTÁTICA, `PinMux::is_bonded_lqfp100()`,
// y estaba señalado como obstáculo desde el análisis de varios MCUs
// [doc/stm32f407vg_multi_mcu.md, §6.3]: mientras hubiera un solo encapsulado
// daba igual; con dos chips de encapsulados distintos en la misma placa, no.
// Ahora es un dato de instancia, que es lo que ese análisis pedía.
//
// CÓMO SE GUARDA: una máscara de 16 bits por puerto. Es exacto —dice pin a pin
// qué sale y qué no—, cabe en dieciocho bytes y se consulta con un AND.
//
// LA COMPROBACIÓN QUE LO SOSTIENE: cada encapsulado lleva además el número de
// GPIO que la tabla 2 del DS8626 le asigna, y la suite comprueba que
// **la máscara tiene exactamente esos bits** (T128). Un mapa mal copiado deja
// de cuadrar y la prueba falla; sin ese contraste, una máscara equivocada sería
// indetectable.
//
// Fuente: DS8626 (STM32F405xx/407xx), tabla 2 «Features and peripheral counts»
// para los recuentos, y tabla 7 «pin and ball definitions» para el reparto.
// =============================================================================
#ifndef STM32_PINS_ENCAPSULADO_H
#define STM32_PINS_ENCAPSULADO_H

#include <cstdint>
#include "../common/ahb_types.h"

namespace stm32 {

// Los nueve puertos del silicio, A..I. No depende del encapsulado: es lo que
// hay dentro del chip, y el encapsulado decide cuánto de ello se ve.
constexpr unsigned N_PUERTOS_SILICIO = 9;

struct Encapsulado {
    const char* nombre;        // "LQFP100", "UFBGA176"...
    unsigned    n_pines;       // patillas o bolas TOTALES, alimentación incluida
    unsigned    n_gpio;        // los que dice la tabla 2 del DS8626
    uint16_t    puerto[N_PUERTOS_SILICIO];   // máscara de pines soldados
    // ¿El reparto pin a pin se ha contrastado con la tabla 7 del datasheet, o
    // es una reconstrucción a partir del recuento? Un `false` aquí NO es una
    // pieza a medias: es un aviso de que el recuento cuadra pero la identidad
    // de cada pad no está comprobada, y `sim` lo dice al montar la placa. Más
    // vale eso que un mapa que parece exacto y no lo es.
    bool        verificado;

    bool bonded(unsigned p, unsigned i) const {
        return p < N_PUERTOS_SILICIO && i < 16 &&
               ((puerto[p] >> i) & 1u) != 0;
    }
    // Los GPIO que la máscara dice de verdad. Tiene que coincidir con `n_gpio`;
    // que coincidan es lo que comprueba la suite.
    unsigned cuenta_gpio() const {
        unsigned n = 0;
        for (unsigned p = 0; p < N_PUERTOS_SILICIO; ++p)
            for (unsigned i = 0; i < 16; ++i) n += (puerto[p] >> i) & 1u;
        return n;
    }
    bool coherente() const { return cuenta_gpio() == n_gpio; }
    // ¿Sale algún pin de este puerto? Lo pregunta el top para no cablear las
    // funciones alternativas de un puerto que no existe.
    bool hay_puerto(unsigned p) const {
        return p < N_PUERTOS_SILICIO && puerto[p] != 0;
    }
};

// Atajos para escribir las máscaras sin contar ceros a mano.
constexpr uint16_t TODOS = 0xFFFFu;   // los dieciséis pines del puerto
constexpr uint16_t NINGUNO = 0x0000u;
constexpr uint16_t hasta(unsigned n) {          // pines 0..n-1
    return uint16_t((n >= 16) ? 0xFFFFu : ((1u << n) - 1u));
}

// ---------------------------------------------------------------------------
// LQFP64 — 51 GPIO [DS8626, tabla 2]
//
// El puerto D sale ENTERO... salvo que no: sale UN pin, PD2, que es SDIO_CMD y
// UART5_RX. Es el caso que enseña por qué la máscara es por pin y no por
// puerto: «el puerto D existe a medias» no se puede decir con un booleano.
// ---------------------------------------------------------------------------
inline constexpr Encapsulado ENC_LQFP64 {
    "LQFP64", 64, 51,
    { TODOS, TODOS, TODOS,          // A, B, C completos           48
      1u << 2,                      // D: solo PD2                  1
      NINGUNO, NINGUNO, NINGUNO,    // E, F, G: nada
      hasta(2),                     // H: PH0/PH1 (OSC_IN/OUT)      2
      NINGUNO },                    // I: nada
    true
};

// ---------------------------------------------------------------------------
// WLCSP90 — 72 GPIO [DS8626, tabla 2]
//
// ⚠ EL RECUENTO ESTÁ VERIFICADO; EL REPARTO NO. La tabla 2 del datasheet dice
// 72 GPIO y esta máscara tiene 72 bits, pero la tabla 7 —la que dice bola a
// bola cuál es cuál— no se ha podido consultar al escribir esto. El reparto de
// aquí es la reconstrucción razonable (A, B, C y D completos, PE0-PE5 y los dos
// del oscilador), y puede no ser el de ST.
//
// Se marca `verificado = false` a propósito, y `sim` avisa al montar una placa
// con este encapsulado. Un mapa que parece exacto y no lo es sería peor que
// este aviso: el alumno conectaría a una bola que no existe y el modelo se lo
// aceptaría sin decir nada. Para cerrarlo hace falta la tabla 7 del DS8626.
// ---------------------------------------------------------------------------
inline constexpr Encapsulado ENC_WLCSP90 {
    "WLCSP90", 90, 72,
    { TODOS, TODOS, TODOS, TODOS,   // A, B, C, D completos        64
      hasta(6),                     // E: PE0..PE5                  6
      NINGUNO, NINGUNO,             // F, G: nada
      hasta(2),                     // H: PH0/PH1                   2
      NINGUNO },
    false                           // <- el reparto NO esta contrastado
};

// ---------------------------------------------------------------------------
// LQFP100 — 82 GPIO [DS8626, tabla 2]. El de la STM32F4-Discovery, y el que
// este modelo lleva desde el principio.
// ---------------------------------------------------------------------------
inline constexpr Encapsulado ENC_LQFP100 {
    "LQFP100", 100, 82,
    { TODOS, TODOS, TODOS, TODOS, TODOS,   // A..E completos        80
      NINGUNO, NINGUNO,                    // F, G: nada
      hasta(2),                            // H: PH0/PH1             2
      NINGUNO },
    true
};

// ---------------------------------------------------------------------------
// LQFP144 — 114 GPIO [DS8626, tabla 2]. Aparecen F y G enteros, que es lo que
// da el bus externo completo del FSMC.
// ---------------------------------------------------------------------------
inline constexpr Encapsulado ENC_LQFP144 {
    "LQFP144", 144, 114,
    { TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, TODOS,  // A..G     112
      hasta(2),                                         // H          2
      NINGUNO },
    true
};

// ---------------------------------------------------------------------------
// LQFP176 y UFBGA176 — 140 GPIO [DS8626, tabla 2]
//
// Los dos encapsulados de 176 tienen el MISMO reparto de pads y se distinguen
// solo en la forma; se declaran por separado porque el nombre es lo que el
// usuario escribe y lo que sale en los avisos. Aquí es donde aparecen el puerto
// H entero y los doce primeros del I.
// ---------------------------------------------------------------------------
inline constexpr uint16_t PUERTOS_176[N_PUERTOS_SILICIO] = {
    TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, TODOS,   // A..G      112
    TODOS,                                             // H          16
    hasta(12)                                          // I: PI0..PI11 12
};
inline constexpr Encapsulado ENC_LQFP176 {
    "LQFP176", 176, 140,
    { TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, hasta(12) },
    true
};
inline constexpr Encapsulado ENC_UFBGA176 {
    "UFBGA176", 176, 140,
    { TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, TODOS, hasta(12) },
    true
};

} // namespace stm32
#endif // STM32_PINS_ENCAPSULADO_H
