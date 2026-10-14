// =============================================================================
// mcu_desc.h — Lo que `sim` necesita saber de un tipo de chip, y nada más
//
// Hasta T0 el catálogo de chips era una lista de `McuCaps`, y `McuCaps` es el
// descriptor de la familia STM32F4: núcleo, Flash con sus sectores, árbol de
// reloj de ST, periféricos de ST, matriz AHB de ST y DBGMCU_IDCODE. Que `sim` y
// `mcu_if` lo exigieran obligaba a cualquier chip de otra familia a rellenar un
// descriptor de ST con ceros, que es justo la «etiqueta falsa» que
// `top/mcu_caps.h` dice que no se debe poner. [doc/analisis_msp432.md, §1.2]
//
// Lo que `sim` usa de un tipo de chip es esto:
//
//   * su NOMBRE, el que se escribe en el XML y en `--mcu`;
//   * su FAMILIA, que es por lo que despacha la factoría (`FabricaMcu`);
//   * su ENCAPSULADO, porque el netlist valida los pads ANTES de que el chip
//     exista: «has conectado algo a P10.3 y ese encapsulado no lo saca»;
//   * y un puntero al descriptor PROPIO de su familia, que solo entiende el
//     creador de esa familia: un `McuCaps` para los STM32F4, otro para los
//     MSP432P4.
//
// Los STM32 se dan de alta en este catálogo desde `top/mcu_caps.h`, con su
// `CATALOGO_MCU` de siempre, que sigue existiendo tal cual para las suites.
// =============================================================================
#ifndef STM32_SOC_MCU_DESC_H
#define STM32_SOC_MCU_DESC_H

#include <string>
#include <vector>
#include "../pins/encapsulado.h"

namespace stm32 {

struct McuDesc {
    const char*        nombre;     // "STM32F407VG", "MSP432P401R"
    const char*        familia;    // "STM32F4", "MSP432P4": la de la factoría
    const Encapsulado* enc;        // qué pads salen al plástico
    const void*        propio;     // el descriptor de SU familia
};

class CatalogoMcu {
public:
    // Antes de `main`. Un nombre repetido no se da de alta dos veces.
    static void registra(const McuDesc& d) {
        for (const McuDesc& x : v())
            if (iguales(x.nombre, d.nombre)) return;
        v().push_back(d);
    }
    // Sin distinguir mayúsculas, como `mcu_por_nombre`.
    static const McuDesc* busca(const std::string& n) {
        for (const McuDesc& x : v())
            if (iguales(x.nombre, n.c_str())) return &x;
        return nullptr;
    }
    static const std::vector<McuDesc>& todos() { return v(); }
    static std::string como_texto() {
        std::string s;
        for (const McuDesc& x : v()) {
            if (!s.empty()) s += ", ";
            s += x.nombre;
        }
        return s;
    }

private:
    static std::vector<McuDesc>& v() {
        static std::vector<McuDesc> c;
        return c;
    }
    static bool iguales(const char* a, const char* b) {
        for (; *a && *b; ++a, ++b) {
            char x = *a, y = *b;
            if (x >= 'a' && x <= 'z') x = char(x - 'a' + 'A');
            if (y >= 'a' && y <= 'z') y = char(y - 'a' + 'A');
            if (x != y) return false;
        }
        return *a == *b;
    }
};

} // namespace stm32
#endif // STM32_SOC_MCU_DESC_H
