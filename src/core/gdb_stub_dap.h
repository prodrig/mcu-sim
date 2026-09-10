// =============================================================================
// gdb_stub_dap.h — El mismo depurador, pero enchufado al DAP por dentro
//
// Este es el SEGUNDO stub. Habla exactamente el mismo GDB/RSP que el de los
// pines -es literalmente el mismo código, common/gdb_rsp.h- y se diferencia en
// una sola cosa: cómo llega al espacio de direcciones del objetivo.
//
//   Sonda (verif/gdb_stub.h)  : escribir una palabra = ~100 flancos de SWCLK,
//                               con su cabecera, su turnaround, su paridad y
//                               sus posibles WAIT. Cada flanco es un evento de
//                               SystemC, y una descarga de 64 KiB son millones.
//   Este (core/gdb_stub_dap.h): escribir una palabra = una llamada a
//                               DebugSys::ap_write32, es decir, UNA transacción
//                               TLM. Dos o tres órdenes de magnitud menos de
//                               eventos simulados.
//
// -----------------------------------------------------------------------------
// ¿POR QUÉ ESTO NO ES HACER TRAMPA?
//
// Porque no se salta el modelo de depuración, solo el CABLE. Todo lo que hay
// del AHB-AP hacia dentro sigue siendo el mismo camino que usa la sonda:
//
//   * el AHB-AP entra por `ahb_ap`, que el router del núcleo trata igual que
//     el S-bus, con su MPU, su remapeo de 0x0 y su arbitraje en la matriz;
//   * parar el núcleo sigue siendo escribir DHCSR con la llave 0xA05F;
//   * los puntos de ruptura siguen siendo comparadores del FPB;
//   * la Flash sigue programándose por FLASH_KEYR/CR/SR, palabra a palabra,
//     esperando BSY, con su 1 ms de borrado de sector [IR, §5.5-5.9].
//
// Lo que desaparece es la capa SW-DP: el reset de línea, la conmutación
// JTAG->SWD, el IDCODE, los ACK, la paridad y los bits pegajosos. Eso es
// PROTOCOLO DE TRANSPORTE, no comportamiento del MCU. Si lo que se quiere
// verificar es precisamente ese protocolo -que un ST-LINK real se entienda con
// el modelo-, hay que usar la sonda; para todo lo demás, esto da el mismo
// resultado mucho antes.
//
// -----------------------------------------------------------------------------
// LOS PINES SE RESERVAN, AUNQUE NO SE USEN
//
// Cuando el núcleo se construye con DebugAttach::Interno, PA13, PA14, PA15,
// PB3 y PB4 SIGUEN siendo del puerto de depuración: la función AF0 sigue
// registrada en el mux, así que el firmware que quiera usarlos como GPIO tiene
// que desactivarlos igual que en el silicio, y quien los mire desde fuera los
// ve en su nivel de reposo. Lo único que cambia es que el frente SWD del
// DebugSys deja de escuchar los flancos y el SWO deja de emitir: los pines
// están reservados, pero mudos. Así el firmware que se depure se comporta
// igual con un stub que con el otro, que es la condición para que la elección
// sea solo una cuestión de velocidad.
// =============================================================================
#ifndef STM32_CORE_GDB_STUB_DAP_H
#define STM32_CORE_GDB_STUB_DAP_H

#include <systemc>
#include <cstdint>
#include "../common/gdb_rsp.h"
#include "debug.h"

namespace stm32 {

SC_MODULE(GdbStubDap), public GdbRsp {
    // Se construye con el subsistema de depuración al que se pega. No necesita
    // pines porque no los usa: ese es todo el asunto.
    GdbStubDap(sc_core::sc_module_name nm, DebugSys& dbg, unsigned puerto = 3333)
        : sc_core::sc_module(nm), GdbRsp(puerto, "gdb-dap"), dbg_(dbg) {
        SC_HAS_PROCESS(GdbStubDap);
        SC_THREAD(run);
        // Sin bits que serializar, el sondeo del socket puede ser mucho más
        // fino sin que cueste nada: mejora la sensación de interactividad en
        // el IDE sin añadir eventos de simulación apreciables.
        set_poll(sc_core::sc_time(20, sc_core::SC_US));
    }

private:
    // =======================================================================
    // EL TRANSPORTE: una llamada de función
    // =======================================================================
    // No hay nada que enganchar: el AP está aquí al lado y siempre alimentado.
    // El equivalente de CDBGPWRUPREQ/CSYSPWRUPREQ es implícito, porque este
    // stub vive DENTRO del dominio de depuración.
    bool dap_enganchar() override { return true; }

    bool dap_leer(uint32_t a, uint32_t& v) override {
        return dbg_.ap_read32(a, v) == tlm::TLM_OK_RESPONSE;
    }
    bool dap_escribir(uint32_t a, uint32_t v) override {
        return dbg_.ap_write32(a, v) == tlm::TLM_OK_RESPONSE;
    }
    // Los registros propios del AP, sin banco que conmutar ni RDBUFF que
    // recoger: aqui el AP esta al lado y se le pregunta.
    bool dap_leer_ap(unsigned ap, unsigned reg, uint32_t& v) override {
        return dbg_.ap_registro(ap, reg, v);
    }
    // El bloque también es palabra a palabra, pero cada palabra es una
    // transacción, no un paquete de 46 bits: no hay frontera de 1 KiB que
    // respetar porque no hay TAR que se auto-incremente.
    unsigned dap_leer_bloque(uint32_t a, uint32_t* w, unsigned n) override {
        for (unsigned i = 0; i < n; ++i)
            if (!dap_leer(a + 4 * i, w[i])) return i;
        return n;
    }

    void run() { servir(); }

    DebugSys& dbg_;
};

} // namespace stm32
#endif // STM32_CORE_GDB_STUB_DAP_H
