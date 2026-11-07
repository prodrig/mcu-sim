// =============================================================================
// rv_ifaces.h — Las interfaces por las que un hart RISC-V habla con lo que no es él
//
// E1 de doc/analisis_esp32_riscv.md, §3.3-§3.5. El hart (rv_hart.h) es C++
// puro: no sabe de SystemC, ni de TLM, ni de qué chip lo ha integrado. Todo lo
// que necesita de fuera entra por aquí:
//
//   rv_mem_if       la memoria: leer y escribir 1, 2 o 4 bytes. Quien lo
//                   integra en SystemC (NucleoRv) lo convierte en b_transport;
//                   una prueba de C++ lo convierte en un vector.
//   rv_irq_if       el controlador de interrupciones: qué líneas hay pedidas,
//                   cuál gana entre varias y qué hacer cuando el núcleo toma
//                   una. El del laboratorio es el estándar (mip/mie con MEIP,
//                   MSIP y MTIP); el del ESP32-C3 es otro, y el del C6, otro.
//   rv_csr_ext_if   los CSR propios del fabricante (los contadores de
//                   Espressif en 0x7E0, su GPIO dedicada en 0x803...).
//   rv_ext_if       las instrucciones propias, en los huecos custom-0..3.
//
// NADA DE ESTE DIRECTORIO SABE DE ESPRESSIF.
// =============================================================================
#ifndef STM32_RV_HART_RV_IFACES_H
#define STM32_RV_HART_RV_IFACES_H

#include <cstdint>
#include "rv_caps.h"

namespace stm32 {

class RvHart;

// Qué tipo de acceso es, para el bus y para la protección
enum class RvAcceso : uint8_t { Fetch, Load, Store, Amo };

// -----------------------------------------------------------------------------
// La memoria. `false` = error de bus (el hart lo convierte en un fallo de
// acceso: excepción 1, 5 o 7). `modo` es el privilegio EFECTIVO del acceso
// (MPRV incluido), por si el bus filtra por él (el PMS del ESP32-C3 lo hace).
// -----------------------------------------------------------------------------
class rv_mem_if {
public:
    virtual ~rv_mem_if() = default;
    virtual bool lee(uint32_t dir, unsigned bytes, uint32_t& v, RvAcceso a, RvModo modo) = 0;
    virtual bool escribe(uint32_t dir, unsigned bytes, uint32_t v, RvAcceso a, RvModo modo) = 0;
};

// -----------------------------------------------------------------------------
// El controlador de interrupciones, visto desde el núcleo.
//
// `pendientes()` es lo que el hardware pone en `mip`, un bit por ID (que es
// también el código de `mcause`). En un núcleo SIN `mie`/`mip` (el C3) el
// controlador ya ha aplicado habilitación, prioridad y umbral, y lo que
// devuelve son las que pueden entrar.
//
// `elige()` decide cuál entra entre varias: el orden estándar es MEI, MSI,
// MTI; el del INTPRI del C6, por prioridad y luego por ID. `reclamada()`
// avisa de que el núcleo la ha tomado, para los controladores que tienen que
// hacer algo entonces (borrar un flanco, guardar el nivel).
// -----------------------------------------------------------------------------
class rv_irq_if {
public:
    virtual ~rv_irq_if() = default;
    virtual uint32_t pendientes() = 0;
    virtual bool elige(uint32_t candidatas, unsigned& id) = 0;
    virtual void reclamada(unsigned /*id*/) {}
};

// El orden estándar de [Privileged 1.12, §3.1.9]: MEI, MSI, MTI, SEI, SSI,
// STI, y después las de plataforma de la más alta a la más baja.
inline bool rv_elige_estandar(uint32_t c, unsigned& id) {
    static const unsigned orden[] = {11, 3, 7, 9, 1, 5, 8, 0, 4};
    for (unsigned i : orden) if (c & (1u << i)) { id = i; return true; }
    for (int i = 31; i >= 16; --i) if (c & (1u << unsigned(i))) { id = unsigned(i); return true; }
    for (unsigned i = 0; i < 16; ++i) if (c & (1u << i)) { id = i; return true; }
    return false;
}

// -----------------------------------------------------------------------------
// Los CSR propios. El hart pregunta primero aquí; si `atiende(csr)` dice que
// sí, los accesos van a `lee`/`escribe`, que devuelven false para provocar
// una instrucción ilegal (por ejemplo, por privilegio).
// -----------------------------------------------------------------------------
class rv_csr_ext_if {
public:
    virtual ~rv_csr_ext_if() = default;
    virtual bool atiende(uint16_t csr) const = 0;
    virtual bool lee(RvHart& h, uint16_t csr, uint32_t& v) = 0;
    virtual bool escribe(RvHart& h, uint16_t csr, uint32_t v) = 0;
};

// -----------------------------------------------------------------------------
// Las instrucciones propias, en los cuatro huecos que la ISA reserva para
// ello (custom-0..3: 0x0B, 0x2B, 0x5B, 0x7B). `false` = no es de las suyas,
// y el hart la trata como ilegal. `ciclos` es lo que cuesta.
// -----------------------------------------------------------------------------
class rv_ext_if {
public:
    virtual ~rv_ext_if() = default;
    virtual bool ejecuta(RvHart& h, uint32_t insn, unsigned& ciclos) = 0;
};

} // namespace stm32
#endif // STM32_RV_HART_RV_IFACES_H
