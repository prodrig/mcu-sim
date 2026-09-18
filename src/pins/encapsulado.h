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
// [doc/multi_mcu.md, §6.3]: mientras hubiera un solo encapsulado
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
// WLCSP90 — 72 GPIO [DS8626, tabla 2 y FIGURA 17 «STM32F40xxx WLCSP90 ballout»]
//
// EL ÚNICO QUE NO ES REGULAR, y por eso es el que más justifica que esto sea
// una máscara y no una regla. Los otros cinco encapsulados se describen con
// «estos puertos enteros más el oscilador»; este no:
//
//   * al puerto C le faltan TRES bolas sueltas —PC1, PC4 y PC5—, que son
//     precisamente ADC123_IN11, ADC12_IN14 y ADC12_IN15;
//   * al D le faltan PD3 y PD13;
//   * del E solo sale LA MITAD ALTA, PE7..PE15, y no la baja;
//   * y aparecen PI0 y PI1, que no salen ni en el LQFP100 ni en el LQFP144 y
//     aquí sí: en un encapsulado de 90 bolas con 72 E/S, ST sacó dos pines del
//     puerto I que en un LQFP de 144 patillas no están.
//
// Ese último detalle es el que hacía falta ver. La reconstrucción que había
// aquí antes —A, B, C y D completos más PE0..PE5— **daba 72 y era falsa en
// casi todo**: ni C ni D están completos, el rango de E es el contrario, y PI
// no se contemplaba. El recuento cuadraba por casualidad, que es exactamente
// la razón por la que se marcó como no verificada en vez de darla por buena.
//
// VERIFICADO POR DOS FUENTES DE ST QUE COINCIDEN BOLA A BOLA:
//   * DS8626 figura 17, el diagrama de bolas del encapsulado;
//   * STM32_open_pin_data de ST (la base de CubeMX), `STM32F405O(E-G)Yx.xml`,
//     que declara `<IONb>72</IONb>`.
// Los recuentos por puerto de las dos —16/16/13/14/9/2/2— son idénticos.
// [doc/todo.md, I-40]
// ---------------------------------------------------------------------------
inline constexpr Encapsulado ENC_WLCSP90 {
    "WLCSP90", 90, 72,
    { TODOS,                        // A: PA0..PA15                 16
      TODOS,                        // B: PB0..PB15                 16
      0xFFCDu,                      // C: sin PC1, PC4 ni PC5       13
      0xDFF7u,                      // D: sin PD3 ni PD13           14
      0xFF80u,                      // E: PE7..PE15, la mitad alta   9
      NINGUNO, NINGUNO,             // F, G: nada
      hasta(2),                     // H: PH0/PH1                    2
      hasta(2) },                   // I: PI0/PI1, solo en este      2
    true
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

// ---------------------------------------------------------------------------
// LQFP64 del STM32F446 — 50 GPIO [DS10693, tabla 2; PINDATA STM32F446R(C-E)Tx]
//
// MISMO ENCAPSULADO, OTRO REPARTO, y de ahí que sea una constante aparte: el
// LQFP64 del F405RG saca 51 E/S y el del F446RE saca 50. **La diferencia es
// PB11**, que en el F446 no tiene patilla.
//
// Es un pin, y es justo la clase de detalle que decide si un modelo sirve o
// engaña: un alumno que configure PB11 en un F446RE no está encendiendo un LED
// que no se ve, está escribiendo en un pad que en su placa no existe. Y trae
// cola: **ULPI_D4 está en PB11 en el F407 y en PB2 en el F446RE** precisamente
// porque PB11 no sale [vs_446re, §9.2].
//
// El recuento cuadra: 16 + 15 + 16 + 1 + 2 = 50 ✓, y `coherente()` lo comprueba.
// Los puertos D (salvo PD2), E, F y G **existen en el die** —se pueden encender
// desde el RCC y sus registros responden— pero no tienen pines aquí, que es
// exactamente la distinción entre die y encapsulado que este fichero modela.
// El F446 no tiene puerto I en ningún encapsulado [CMSIS].
// ---------------------------------------------------------------------------
inline constexpr Encapsulado ENC_LQFP64_F446 {
    "LQFP64", 64, 50,
    { TODOS,                        // A: PA0..PA15                 16
      0xF7FFu,                      // B: sin PB11                  15
      TODOS,                        // C: PC0..PC15                 16
      1u << 2,                      // D: solo PD2                   1
      NINGUNO, NINGUNO, NINGUNO,    // E, F, G: nada
      hasta(2),                     // H: PH0/PH1 (OSC_IN/OUT)       2
      NINGUNO },                    // I: el F446 no tiene puerto I
    true
};

// ---------------------------------------------------------------------------
// LOS OTROS TRES ENCAPSULADOS DEL F446, y por qué no son «el de siempre menos
// pines».
//
// Las máscaras salen de la base de pines de ST —un fichero por referencia,
// `STM32F446M(C-E)Yx.xml`, `...V(C-E)Tx.xml` y `...Z(C-E)Tx.xml`— leídas por
// máquina, y el recuento de cada una **coincide con la Tabla 2 del DS10693
// Rev 11**: 63, 81 y 114 E/S. Dos fuentes de ST que dicen lo mismo, que es el
// listón que este proyecto se puso desde la fase 0.
//
// LO QUE SE APRENDE MIRÁNDOLAS JUNTAS, y que no es lo que uno supondría:
//
//   * **PB11 solo existe en el LQFP144/UFBGA144.** No sale en el LQFP64, ni en
//     el WLCSP81, ni en el LQFP100 —donde el datasheet dice, con esas palabras,
//     «PB11 not available anymore, replaced by VCAP1»—. Es el pin que ya separó
//     el LQFP64 del F446 del LQFP64 del F405 [vs_446re, §9.2], y resulta que la
//     historia se repite un encapsulado más arriba;
//   * **el WLCSP81 no es un LQFP100 recortado.** Tiene 63 E/S y se las reparte
//     de forma propia: le faltan PC1 y PC5 —que el LQFP64, con menos patillas,
//     sí saca— y de los puertos D y E saca un puñado salteado. Es el mismo
//     patrón que destapó I-40 en el WLCSP90 del F407: a más bolas no
//     corresponde, sin más, un superconjunto de pines.
// ---------------------------------------------------------------------------

// WLCSP81 (letra M). 63 E/S: PA completo, PB sin PB11, PC sin PC1 ni PC5, y
// nueve de PD y siete de PE, salteados.
inline constexpr Encapsulado ENC_WLCSP81_F446 {
    "WLCSP81", 81, 63,
    { TODOS,                        // A: PA0..PA15                 16
      0xF7FFu,                      // B: sin PB11                  15
      0xFFDDu,                      // C: sin PC1 ni PC5            14
      0x38D7u,                      // D: 0,1,2,4,6,7,11,12,13       9
      0x079Cu,                      // E: 2,3,4,7,8,9,10             7
      NINGUNO, NINGUNO,             // F, G: nada
      hasta(2),                     // H: PH0/PH1                    2
      NINGUNO },                    // I: el F446 no tiene puerto I
    true
};

// LQFP100 (letra V). 81 E/S: A, C, D y E completos, B sin PB11, y PH0/PH1.
// Aquí SÍ hay bus externo, pero con una limitación que el datasheet pone en la
// nota 1 de su Tabla 2 y que `Stm32F446::limitaciones()` repite: solo el banco 1
// del FMC, solo NOR/PSRAM multiplexada y solo con NE1 — y sin línea de
// interrupción, porque el puerto G no sale.
inline constexpr Encapsulado ENC_LQFP100_F446 {
    "LQFP100", 100, 81,
    { TODOS,                        // A                            16
      0xF7FFu,                      // B: sin PB11                  15
      TODOS,                        // C                            16
      TODOS,                        // D                            16
      TODOS,                        // E                            16
      NINGUNO, NINGUNO,             // F, G: nada
      hasta(2),                     // H: PH0/PH1                    2
      NINGUNO },
    true
};

// LQFP144 y UFBGA144 (letra Z). 114 E/S: A a G completos —**incluido PB11**— y
// PH0/PH1. Los dos encapsulados de 144 tienen el mismo reparto de puertos, así
// que comparten descriptor de máscara; lo que cambia entre ellos es la forma del
// paquete, que este modelo no simula.
inline constexpr Encapsulado ENC_LQFP144_F446 {
    "LQFP144", 144, 114,
    { TODOS, TODOS, TODOS, TODOS,   // A, B (con PB11), C, D        64
      TODOS, TODOS, TODOS,          // E, F, G                      48
      hasta(2),                     // H: PH0/PH1                    2
      NINGUNO },                    // I: el F446 no tiene puerto I
    true
};

} // namespace stm32
#endif // STM32_PINS_ENCAPSULADO_H
