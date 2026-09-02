// =============================================================================
// debug_if.h — El contrato entre el núcleo y el subsistema de depuración
//
// La depuración de un Cortex-M no es un periférico más: es una capa que
// INTERCEPTA el ciclo de instrucción [IR, §13-Implicaciones]. El FPB mira cada
// búsqueda de instrucción, el DWT mira cada acceso a datos, y el Core Debug
// puede detener el hilo de ejecución entre dos instrucciones.
//
// Eso obliga a que la CPU llame al subsistema de depuración, y no al revés. Se
// hace por esta interfaz abstracta —igual que la CPU habla con el SCS por
// `core_sys_if`— para que `cpu.h` no tenga que conocer `debug.h`: son dos
// módulos que se incluyen en el orden contrario.
//
// Con el puntero a nulo (que es como queda si nadie lo enlaza) el núcleo se
// comporta exactamente como antes de la fase F6: ni un `if` de más en el camino
// caliente que no sea la comprobación del propio puntero.
// =============================================================================
#ifndef STM32_CORE_DEBUG_IF_H
#define STM32_CORE_DEBUG_IF_H

#include <cstdint>
#include <systemc>

namespace stm32 {

// Causas de parada. Son literalmente los bits de SCB_DFSR [IR, §13.4].
enum DfsrBits : uint32_t {
    DFSR_HALTED   = 1u << 0,   // parada por petición del depurador o paso a paso
    DFSR_BKPT     = 1u << 1,   // instrucción BKPT o comparador del FPB
    DFSR_DWTTRAP  = 1u << 2,   // comparador del DWT (watchpoint)
    DFSR_VCATCH   = 1u << 3,   // captura de vector (DEMCR.VC_*)
    DFSR_EXTERNAL = 1u << 4    // petición externa de parada
};

// Qué debe hacer el núcleo con una búsqueda de instrucción, según el FPB.
enum FetchAction {
    FETCH_NORMAL = 0,   // seguir su camino
    FETCH_SUBST  = 1,   // usar la media palabra que devuelve el FPB (BKPT)
    FETCH_REMAP  = 2    // buscarla en la dirección remapeada
};

class core_debug_if {
public:
    virtual ~core_debug_if() {}

    // --- Control de ejecución ----------------------------------------------
    // ¿Debe estar parado el núcleo ahora mismo?
    virtual bool dbg_halt_now() const = 0;
    // DHCSR.C_DEBUGEN: sin él, un BKPT escala a HardFault en vez de parar.
    virtual bool dbg_enabled() const = 0;
    // El núcleo se para POR SU CUENTA: BKPT, watchpoint o captura de vector.
    virtual void dbg_request_halt(uint32_t dfsr_bits) = 0;
    // Consume una petición de paso a paso (DHCSR.C_STEP). Devuelve true una
    // sola vez por petición.
    virtual bool dbg_take_step() = 0;
    // Evento con el que despertar al núcleo detenido.
    virtual sc_core::sc_event& dbg_wake() = 0;

    // --- FPB: intercepta la búsqueda de instrucción [IR, §13.7] -------------
    virtual int dbg_fetch(uint32_t addr, uint32_t& hw, uint32_t& alt) = 0;

    // --- DWT: mira los accesos a datos y cuenta [IR, §13.5] -----------------
    virtual void dbg_data(uint32_t addr, unsigned size, bool write, uint32_t v) = 0;
    virtual void dbg_cycles(unsigned cycles, unsigned lsu_extra) = 0;
    virtual void dbg_exception(int excp, bool entry) = 0;
    virtual void dbg_sleep(unsigned cycles) = 0;

    // --- Captura de vectores y reset ----------------------------------------
    virtual bool dbg_vector_catch(int excp) = 0;
    virtual void dbg_reset() = 0;
};

} // namespace stm32
#endif // STM32_CORE_DEBUG_IF_H
