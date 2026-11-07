// =============================================================================
// gdb_stub_rv.h — El servidor de GDB de un RISC-V, sobre cualquier dmi_if
//
// E2 de doc/analisis_esp32_riscv.md, §3.6-3.7. El mismo stub para los dos
// caminos que no necesitan un programa de fuera:
//
//   G1   con el DM en directo (`GdbStubRv(nm, dm, 3333)`): una llamada de
//        función por acceso al DM. Es el modo rápido.
//   G2'  con un DTM por los pines (`GdbStubRv(nm, dmi_por_jtag, 3333)`, con
//        verif/sonda_jtag_rv.h): cada acceso al DM son decenas de flancos de
//        TCK, como con una sonda de verdad.
//
// El protocolo es common/gdb_rsp.h; la arquitectura, gdb_rsp_riscv.h.
// =============================================================================
#ifndef STM32_RV_GDB_GDB_STUB_RV_H
#define STM32_RV_GDB_GDB_STUB_RV_H

#include <systemc>
#include "gdb_rsp_riscv.h"

namespace stm32 {

SC_MODULE(GdbStubRv), public GdbRspRiscv {
    GdbStubRv(sc_core::sc_module_name nm, dmi_if& d, unsigned puerto = 3333,
              const char* etiqueta = "gdb-rv")
        : sc_core::sc_module(nm), GdbRspRiscv(d, puerto, etiqueta) {
        SC_HAS_PROCESS(GdbStubRv);
        SC_THREAD(run);
    }

private:
    void run() { servir(); }
};

} // namespace stm32
#endif // STM32_RV_GDB_GDB_STUB_RV_H
