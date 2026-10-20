// =============================================================================
// crc.h — Unidad de cálculo CRC (AHB1) [IR, §12.20]
//
// (Separado de `crc_rng.h` en la fase 1 del plan del F446. Los dos bloques
//  vivían en el mismo fichero porque en el F407 van siempre juntos; no es el
//  caso de toda la familia F4 — el F446 lleva CRC y NO lleva RNG—, y un
//  periférico que puede irse solo tiene que poder incluirse solo.)
//
// Es de los dos periféricos más pequeños del dispositivo y, precisamente por
// eso, uno de los dos en los que es más tentador hacer trampa. Aquí no se ha
// hecho: la unidad calcula el polinomio BIT A BIT, no con una tabla ni con una
// llamada a una biblioteca. El resultado tiene que salir de la división
// polinómica, igual que del silicio.
//
// Sobre la parametrización por rasgos que usa el resto del proyecto: aquí NO se
// aplica, y conviene decir por qué. El F407 lleva UNA sola instancia y el
// bloque no tiene ejes de configuración en este silicio: el polinomio está
// cableado (0x04C11DB7), el valor inicial también (0xFFFF FFFF), y no hay
// inversión de bits ni de entrada ni de salida. Montar una `struct` de rasgos
// para describir un espacio con un solo punto sería maquinaria sin
// contrapartida. Lo que sí se ha hecho es dejar el polinomio y el valor inicial
// como constantes con nombre en un solo sitio, que es donde entraría el eje el
// día que haga falta la unidad CRC PROGRAMABLE de las familias posteriores.
//
// Implementado en la fase F5: CRC_DR (acumulación y lectura del resultado),
// CRC_IDR (8 bits, ajeno al cálculo y al RESET del bloque) y CRC_CR.RESET, con
// su coste de un ciclo de HCLK por palabra [IR, §12.20].
// =============================================================================
#ifndef STM32_PERIPH_CRC_H
#define STM32_PERIPH_CRC_H

#include <cstdint>
#include "../common/periph_base.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// El polinomio de Ethernet, tal y como lo cablea el F407 [IR, §12.20.1]:
//   x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5
//        + x^4 + x^2 + x + 1
// Con valor inicial 0xFFFFFFFF, sin inversión de bits y sin XOR final, esto es
// exactamente el CRC-32/MPEG-2. NO es el CRC-32 de zip/Ethernet-FCS, que además
// invierte la entrada, la salida y el resultado; confundirlos es el error
// clásico al comparar el resultado del periférico con el de una biblioteca.
// ---------------------------------------------------------------------------
constexpr uint32_t CRC32_POLY = 0x04C11DB7u;
constexpr uint32_t CRC32_INIT = 0xFFFFFFFFu;

// La división polinómica de una palabra, bit a bit y con el más significativo
// por delante. Es la misma operación que hace el silicio en su ciclo de AHB.
inline uint32_t crc32_word(uint32_t crc, uint32_t data) {
    crc ^= data;
    for (unsigned i = 0; i < 32; ++i)
        crc = (crc & 0x80000000u) ? uint32_t((crc << 1) ^ CRC32_POLY)
                                  : uint32_t(crc << 1);
    return crc;
}

// ---------------------------------------------------------------------------
// EL EJE QUE FALTABA: la unidad PROGRAMABLE (integtest1_v2_0, fase S0 del F303)
//
// Lo que el comentario de arriba anunciaba. En el F303 —y en el F0, el F7, los
// L y los G— la unidad tiene [CMSIS stm32f303xc.h, CRC_TypeDef]:
//
//   CR.POLYSIZE (4:3)  32, 16, 8 o 7 bits
//   CR.REV_IN (6:5)    inversión de bits de la entrada: no, por byte, por
//                      media palabra o por palabra
//   CR.REV_OUT (7)     inversión de bits del resultado
//   INIT (0x10)        valor inicial, que CR.RESET carga en DR
//   POL (0x14)         el polinomio, reset 0x04C11DB7
//
// y DR acepta escrituras de 8, 16 y 32 bits, cada una con SU anchura de dato:
// un byte escrito es un byte de CRC, no una palabra con tres ceros.
//
// La inversión de la salida se hace sobre los POLYSIZE bits del resultado, que
// es lo que da el CRC-16 reflejado leyendo DR como media palabra [1F: RM0316
// §6]. El cálculo sigue siendo bit a bit.
// ---------------------------------------------------------------------------
struct CrcCaps {
    uint32_t base;
    bool     polinomio_programable;
};
inline constexpr CrcCaps CRC_F4{ addr::CRC_B, false };
inline constexpr CrcCaps CRC_F3{ addr::CRC_B, true };

// Un paso general: `w` bits de dato, MSB primero, sobre un CRC de `n` bits.
inline uint32_t crc_bits(uint32_t crc, uint32_t dato, unsigned w, uint32_t pol, unsigned n) {
    const uint32_t m = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
    for (unsigned i = w; i-- > 0;) {
        const bool b = (((crc >> (n - 1)) ^ (dato >> i)) & 1u) != 0;
        crc = (crc << 1) & m;
        if (b) crc ^= pol & m;
    }
    return crc;
}
inline uint32_t invierte_bits(uint32_t v, unsigned n) {
    uint32_t r = 0;
    for (unsigned i = 0; i < n; ++i) if ((v >> i) & 1u) r |= 1u << (n - 1 - i);
    return r;
}

// =============================================================================
// Unidad de cálculo CRC [IR, §12.20]
// =============================================================================
class CrcUnit : public BusSlave {
public:
    enum : uint32_t { R_DR = 0x00, R_IDR = 0x04, R_CR = 0x08,
                      R_INIT = 0x10, R_POL = 0x14 };      // las dos, programable

    CrcUnit(sc_core::sc_module_name nm, const CrcCaps& caps = CRC_F4)
        : BusSlave(nm, caps.base, 0x400), caps_(caps) {
        SC_HAS_PROCESS(CrcUnit);
        SC_METHOD(rst_proc); sensitive << rst_n;
    }
    const CrcCaps& caps() const { return caps_; }

    // Ventanas para el banco de pruebas (no son registros del dispositivo).
    uint32_t peek_dr()  const { return dr_; }
    uint64_t words()    const { return n_words_; }

protected:
    CrcCaps  caps_;
    uint32_t dr_ = CRC32_INIT;
    uint8_t  idr_ = 0;
    uint64_t n_words_ = 0;
    // La unidad programable
    uint32_t cr_ = 0, init_ = CRC32_INIT, pol_ = CRC32_POLY;
    unsigned polysize() const {
        switch ((cr_ >> 3) & 3u) { case 1: return 16; case 2: return 8; case 3: return 7; default: return 32; }
    }
    uint32_t mascara() const { const unsigned n = polysize(); return n >= 32 ? ~0u : ((1u << n) - 1u); }
    // REV_IN: invierte los bits dentro de cada byte, media palabra o palabra
    uint32_t rev_in(uint32_t v, unsigned w) const {
        switch ((cr_ >> 5) & 3u) {
            case 1: { uint32_t r = 0;
                      for (unsigned b = 0; b < w / 8; ++b)
                          r |= invierte_bits((v >> (8 * b)) & 0xFFu, 8) << (8 * b);
                      return r; }
            case 2: if (w < 16) return invierte_bits(v, w);
                    { uint32_t r = 0;
                      for (unsigned h = 0; h < w / 16; ++h)
                          r |= invierte_bits((v >> (16 * h)) & 0xFFFFu, 16) << (16 * h);
                      return r; }
            case 3: return invierte_bits(v, w);
            default: return v;
        }
    }
    uint32_t lee_dr() const {
        if (!caps_.polinomio_programable) return dr_;
        return (cr_ & (1u << 7)) ? invierte_bits(dr_ & mascara(), polysize()) : dr_;
    }

    // Un ciclo de HCLK por palabra [IR, §12.20.2]. Es la razón de ser del
    // bloque: un CRC-32 por software cuesta decenas de instrucciones por
    // palabra, y aquí cuesta un acceso al bus.
    unsigned access_cycles(bool /*write*/) const override { return 1; }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_DR:  return lee_dr();
            case R_IDR: return idr_;
            // CRC_CR.RESET es de SOLO ESCRITURA y se autoborra: el registro
            // entero se lee como cero [IR, §12.20.2]. En la programable, el
            // resto de CR sí se lee.
            case R_CR:  return caps_.polinomio_programable ? (cr_ & 0xF8u) : 0u;
            case R_INIT: return caps_.polinomio_programable ? init_ : 0u;
            case R_POL:  return caps_.polinomio_programable ? pol_ : 0u;
            default:    return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (caps_.polinomio_programable) { escribe_programable(off, v, be); return; }
        switch (off) {
            case R_DR:
                // El camino de datos del bloque es de 32 bits: cada escritura
                // consume UNA palabra completa. Un acceso más estrecho no
                // gobierna los bytes que no selecciona, y esos entran como
                // ceros; el firmware debe escribir palabras alineadas.
                if (be != 0xFu) {
                    uint32_t m = 0;
                    for (unsigned b = 0; b < 4; ++b)
                        if (be & (1u << b)) m |= 0xFFu << (8 * b);
                    v &= m;
                }
                dr_ = crc32_word(dr_, v);
                ++n_words_;
                return;

            case R_IDR:
                // Ocho bits y nada más: es un registro de recado, ajeno al
                // cálculo. Lo que sobra del acceso se pierde [IR, §12.20.2].
                if (be & 1u) idr_ = uint8_t(v);
                return;

            case R_CR:
                // Escribir RESET devuelve CRC_DR a su valor inicial. Y solo a
                // él: CRC_IDR NO se toca, que es justamente para lo que sirve
                // —guardar un dato entre dos cálculos— [IR, §12.20.2].
                if ((be & 1u) && (v & 1u)) { dr_ = CRC32_INIT; n_words_ = 0; }
                return;

            default: return;
        }
    }

    // La unidad programable: la anchura del dato es la del acceso.
    void escribe_programable(uint32_t off, uint32_t v, uint32_t be) {
        switch (off) {
            case R_DR: {
                const unsigned w = be == 0x1u ? 8u : (be == 0x3u ? 16u : 32u);
                const uint32_t d = rev_in(w == 32 ? v : (v & ((1u << w) - 1u)), w);
                dr_ = crc_bits(dr_ & mascara(), d, w, pol_, polysize());
                ++n_words_;
                return;
            }
            case R_IDR: if (be & 1u) idr_ = uint8_t(v); return;
            case R_CR:
                if (!(be & 1u)) return;
                cr_ = v & 0xF8u;
                if (v & 1u) { dr_ = init_ & mascara(); n_words_ = 0; }
                return;
            case R_INIT: init_ = v; return;            // DR la carga CR.RESET [1F]
            case R_POL:  pol_ = v; return;
            default: return;
        }
    }

    void rst_proc() {
        if (rst_n.read()) return;
        // El reset del sistema sí se lleva los dos registros por delante.
        dr_ = CRC32_INIT;
        idr_ = 0;
        n_words_ = 0;
        cr_ = 0; init_ = CRC32_INIT; pol_ = CRC32_POLY;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_CRC_H
