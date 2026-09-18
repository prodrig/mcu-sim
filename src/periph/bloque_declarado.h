// =============================================================================
// bloque_declarado.h — UN PERIFÉRICO QUE ESTÁ Y NO ESTÁ MODELADO
//
// Hay tres maneras de tratar un bloque que el silicio tiene y el modelo no:
//
//   1. NO DECODIFICAR SU VENTANA. Tocarla da error de bus. Es lo correcto
//      cuando el chip **no lleva** el bloque —un Ethernet en un F405—, y es lo
//      que el modelo hace en ese caso. Aquí sería mentir al revés: decir que
//      no hay nada donde el silicio tiene un periférico entero.
//   2. CONTESTAR COMO SI ESTUVIERA. Guardar lo que escriban, devolverlo al
//      leer, y no hacer nada. Es la peor de las tres: el firmware configura,
//      lee de vuelta lo que escribió, se cree configurado y luego no pasa
//      nada. Un modelo que funciona y miente.
//   3. CONTESTAR Y DECIRLO. Es esto.
//
// Un `BloqueDeclarado` ocupa su ventana —de modo que el mapa de memoria del
// modelo es el del chip—, **avisa la primera vez que alguien lo toca** con el
// nombre del bloque y la fase del plan en la que le toca, y devuelve ceros. Un
// firmware que lo use se quedará esperando una bandera que no va a subir, que
// es un fallo ruidoso y rastreable; y el aviso, que sale por el informe de
// SystemC, dice exactamente por qué.
//
// Lo usan el SPDIF-RX y el HDMI-CEC del F446, que el plan de la fase 4 marcó
// como «los de menos recorrido; se pueden dejar declarados y sin modelar,
// diciéndolo». Esto es el «diciéndolo».
// =============================================================================
#ifndef STM32_PERIPH_BLOQUE_DECLARADO_H
#define STM32_PERIPH_BLOQUE_DECLARADO_H

#include <string>
#include "../common/periph_base.h"

namespace stm32 {

class BloqueDeclarado : public BusSlave {
public:
    BloqueDeclarado(sc_core::sc_module_name nm, uint32_t base, uint32_t size,
                    const char* bloque, const char* porque)
        : BusSlave(nm, base, size), bloque_(bloque), porque_(porque) {}

    unsigned accesos() const { return n_; }

protected:
    uint32_t reg_read(uint32_t) override  { avisa(); return 0; }
    void reg_write(uint32_t, uint32_t, uint32_t) override { avisa(); }
    bool responds_without_clock() const override { return false; }

private:
    const std::string bloque_, porque_;
    unsigned n_ = 0;
    bool     avisado_ = false;

    void avisa() {
        ++n_;
        if (avisado_) return;                // una vez basta; el resto es ruido
        avisado_ = true;
        const std::string m = bloque_ + ": el bloque esta DECLARADO pero NO "
                              "MODELADO. Sus registros leen cero y escribir en "
                              "ellos no hace nada. " + porque_;
        SC_REPORT_WARNING("bloque-declarado", m.c_str());
    }
};

} // namespace stm32
#endif // STM32_PERIPH_BLOQUE_DECLARADO_H
