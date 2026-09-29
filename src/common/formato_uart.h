// =============================================================================
// formato_uart.h — El formato de una trama UART, y cómo se escribe
//
// Fase D1 del plan de `doc/analisis_puente_serie.md` §10. Es la parte PURA del
// motor UART: qué es un `8N1`, cuántos bits de línea ocupa y cómo se lee de un
// atributo de XML. Sin SystemC, así que lo prueba `make serie` junto al destino
// del puente (`common/serie_destino.h`), en segundos y en cualquier máquina.
//
// LA NOTACIÓN es la de siempre: bits de datos, paridad y bits de parada.
//
//     8N1    ocho bits, sin paridad, uno de parada (lo que usa casi todo)
//     7E1    siete bits, paridad par, uno de parada
//     8O2    ocho bits, paridad impar, dos de parada
//     9N1    nueve bits (el modo multiprocesador de la USART, M = 1)
//     8M1    paridad de MARCA (el bit de paridad siempre a 1)
//     8S1    paridad de ESPACIO (siempre a 0)
//     8N1.5  uno y medio de parada. Se escribe con PUNTO, no con coma: esto va
//            en un atributo de XML y en una línea de órdenes, no en un texto
//
// Los BITS DE DATOS NO CUENTAN la paridad, que va aparte. Es la convención de
// un terminal —y la de RFC 2217, SET-DATASIZE— y NO la del registro de la
// USART, donde `M` cuenta la paridad dentro: un `7E1` de terminal es, en la
// USART del F407, `M = 0` con `PCE = 1`. Quien configure el puente mirando el
// registro tiene que hacer esa cuenta, y esta nota es para que la haga.
//
// De 5 a 9 bits. El 5 y el 6 no los puede generar la USART del F407 -su
// mínimo es 7 con paridad-, pero RFC 2217 los contempla y un terminal los
// puede pedir: el motor los acepta para poder responder con la verdad.
// =============================================================================
#ifndef STM32_COMMON_FORMATO_UART_H
#define STM32_COMMON_FORMATO_UART_H

#include <string>

namespace stm32 {

enum class Paridad { ninguna, impar, par, marca, espacio };
enum class Parada  { uno, uno_y_medio, dos };

struct FormatoUart {
    unsigned bits    = 8;               // 5..9, SIN contar la paridad
    Paridad  paridad = Paridad::ninguna;
    Parada   parada  = Parada::uno;

    bool con_paridad() const { return paridad != Paridad::ninguna; }

    // Los bits de parada, en MEDIOS bits: 2, 3 o 4. En medios para que 1,5
    // sea un entero y la duración de una trama se pueda calcular sin coma.
    unsigned medios_de_parada() const {
        return parada == Parada::uno ? 2u : parada == Parada::dos ? 4u : 3u;
    }

    // Lo que ocupa una trama en la línea, en MEDIOS bits: arranque, datos,
    // paridad si la hay y parada. Un 8N1 son 20 medios bits, o sea 10 bits.
    unsigned medios_de_trama() const {
        return 2u * (1u + bits + (con_paridad() ? 1u : 0u)) + medios_de_parada();
    }

    // El bit de paridad que corresponde a un dato, con los `bits` de abajo.
    bool bit_de_paridad(unsigned dato) const {
        unsigned unos = 0;
        for (unsigned i = 0; i < bits; ++i) unos += (dato >> i) & 1u;
        switch (paridad) {
        case Paridad::par:     return (unos & 1u) != 0;   // total de unos, par
        case Paridad::impar:   return (unos & 1u) == 0;   // total de unos, impar
        case Paridad::marca:   return true;
        case Paridad::espacio: return false;
        case Paridad::ninguna: return false;
        }
        return false;
    }

    // Los bits de dato que caben: 0x1F para 5 bits, 0x1FF para 9.
    unsigned mascara() const { return (1u << bits) - 1u; }

    bool operator==(const FormatoUart& o) const {
        return bits == o.bits && paridad == o.paridad && parada == o.parada;
    }
    bool operator!=(const FormatoUart& o) const { return !(*this == o); }
};

// Cómo se escribe: `8N1`, `7E1`, `8N1.5`.
inline std::string como_texto(const FormatoUart& f) {
    std::string s(1, char('0' + f.bits));
    switch (f.paridad) {
    case Paridad::ninguna: s += 'N'; break;
    case Paridad::impar:   s += 'O'; break;
    case Paridad::par:     s += 'E'; break;
    case Paridad::marca:   s += 'M'; break;
    case Paridad::espacio: s += 'S'; break;
    }
    s += f.parada == Parada::uno ? "1" : f.parada == Parada::dos ? "2" : "1.5";
    return s;
}

// Y cómo se lee. Devuelve cadena vacía si está bien, y el motivo si no.
//
// La letra de la paridad admite MINÚSCULA (`8n1`): es la forma en que la
// escribe medio mundo y no hay ambigüedad posible. Lo que no se admite es
// adivinar: `8N` sin parada, `81` sin paridad o `8X1` se rechazan diciendo qué
// falta.
inline std::string parsea_formato(const std::string& s, FormatoUart& out) {
    const std::string forma = "se escribe como 8N1: bits (5..9), paridad "
                              "(N, E, O, M o S) y parada (1, 1.5 o 2)";
    if (s.size() < 3) return "'" + s + "' no es un formato: " + forma;
    FormatoUart f;
    if (s[0] < '5' || s[0] > '9')
        return "'" + s + "': los bits de datos van de 5 a 9, y sin contar la "
               "paridad";
    f.bits = unsigned(s[0] - '0');
    switch (s[1]) {
    case 'N': case 'n': f.paridad = Paridad::ninguna; break;
    case 'E': case 'e': f.paridad = Paridad::par;     break;
    case 'O': case 'o': f.paridad = Paridad::impar;   break;
    case 'M': case 'm': f.paridad = Paridad::marca;   break;
    case 'S': case 's': f.paridad = Paridad::espacio; break;
    default:
        return "'" + s + "': '" + std::string(1, s[1]) + "' no es una paridad; " +
               forma;
    }
    const std::string p = s.substr(2);
    if (p == "1")        f.parada = Parada::uno;
    else if (p == "2")   f.parada = Parada::dos;
    else if (p == "1.5") f.parada = Parada::uno_y_medio;
    else if (p == "1,5")
        return "'" + s + "': el uno y medio se escribe con punto, 1.5";
    else
        return "'" + s + "': '" + p + "' no son bits de parada; " + forma;
    out = f;
    return std::string();
}

} // namespace stm32

#endif // STM32_COMMON_FORMATO_UART_H
