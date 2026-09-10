// =============================================================================
// gdb_stub.h — Servidor GDB/RSP sobre TCP, enganchado a los pines de depuración
//
// Es la pieza que convierte el modelo en un objetivo de depuración de verdad:
// por un lado habla el protocolo SWD sobre SWCLK/SWDIO (PA14/PA13), y por el
// otro escucha en un puerto TCP el Remote Serial Protocol de GDB. Con eso,
// Eclipse CDT, STM32CubeIDE o un `arm-none-eabi-gdb` a pelo se conectan con
//
//     target extended-remote localhost:3333
//
// y descargan, ejecutan, paran, ponen puntos de ruptura y miran variables
// exactamente igual que contra una placa con un ST-LINK.
//
// NO es parte del MCU. Es una SONDA: vive en el banco de pruebas, se suelda a
// dos pines y no ve nada del modelo que no pase por ellos. Todo lo que hace
// -leer un registro del núcleo, escribir memoria, programar la Flash- lo
// consigue mandando paquetes SWD, uno detrás de otro, como haría el firmware de
// un ST-LINK.
//
// El PROTOCOLO de GDB no está aquí: está en common/gdb_rsp.h, compartido con el
// segundo stub (core/gdb_stub_dap.h). Este fichero es solo el TRANSPORTE: qué
// significa "leer una palabra" cuando lo único que tienes son dos pines.
//
// -----------------------------------------------------------------------------
// LAS PECULIARIDADES DE ARM SOBRE JTAG/SWD QUE ESTE STUB RESPETA
//
// Un servidor GDB para un Cortex-M no es un traductor de paquetes: tiene que
// conocer el modelo de depuración de ARM (ADIv5) y sus trampas. Todas están
// tratadas, y en `swd_port.h` está el detalle:
//
//   1. ENGANCHE. Reset de línea (≥50 unos), secuencia de conmutación JTAG→SWD
//      (0xE79E), otro reset de línea y lectura del IDCODE. Sin eso el objetivo
//      ni contesta.
//   2. ENCENDIDO DEL DAP. Antes de tocar el AP hay que pedir CDBGPWRUPREQ y
//      CSYSPWRUPREQ en CTRL/STAT y ESPERAR sus acuses.
//   3. WAIT y FAULT. Un ACK de espera se reintenta; uno de fallo obliga a leer
//      CTRL/STAT y limpiar los bits pegajosos por ABORT. Hasta que no se hace,
//      el DAP está mudo.
//   4. LECTURA APLAZADA DEL AP. El dato de una lectura llega en la transacción
//      SIGUIENTE, o en RDBUFF.
//   5. FRONTERA DE 1 KiB. El auto-incremento de TAR no la cruza.
//   6. EL PPB NO ES MEMORIA NORMAL. Parar el núcleo, leer sus registros o poner
//      un punto de ruptura no son operaciones de memoria: son escrituras en
//      DHCSR, DCRSR/DCRDR, FP_COMPn y DWT_COMPn, con sus llaves (0xA05F en
//      DHCSR, KEY en FP_CTRL) y su orden. De eso se ocupa el motor común.
//   7. LA FLASH NO SE ESCRIBE ESCRIBIENDO. Un `load` de GDB sobre 0x0800 0000
//      no puede ser una escritura al bus: hay que desbloquear el controlador
//      con FLASH_KEYR, borrar el sector, poner PG y programar palabra a
//      palabra esperando BSY. El stub lo hace por SWD, igual que un
//      programador real.
// =============================================================================
#ifndef STM32_VERIF_GDB_STUB_H
#define STM32_VERIF_GDB_STUB_H

#include <systemc>
#include <cstdint>
#include "../common/gdb_rsp.h"
#include "swd_port.h"

namespace stm32 {

SC_MODULE(GdbStub), public GdbRsp {
    // -----------------------------------------------------------------------
    // Se construye con los dos pines y el puerto. Nada más: el stub no tiene
    // acceso privilegiado a nada del modelo.
    // -----------------------------------------------------------------------
    GdbStub(sc_core::sc_module_name nm, analog_net_if& swclk, analog_net_if& swdio,
            unsigned puerto = 3333, double swd_hz = 10e6)
        : sc_core::sc_module(nm), GdbRsp(puerto, "gdb"),
          swd_(swclk, swdio, swd_hz) {
        SC_HAS_PROCESS(GdbStub);
        SC_THREAD(run);
    }

    // Ventana al coste real del transporte: cuántos paquetes SWD ha costado la
    // sesión. Es la cifra que hace falta para comparar con el stub interno.
    uint64_t swd_esperas() const { return swd_.esperas(); }
    // Suelta los dos pines. Hace falta cuando otra sonda del banco quiere
    // usarlos: dos maestros sobre SWCLK son una pelea de pads, no un bus.
    void soltar_pines() { swd_.desconectar(); }

private:
    // =======================================================================
    // EL TRANSPORTE: bits por dos pines
    // =======================================================================
    bool dap_enganchar() override {
        const uint32_t id = swd_.conectar_swd();
        return (id == 0x2BA01477u) && swd_.encendido();
    }
    bool dap_leer(uint32_t a, uint32_t& v) override  { return swd_.mem_read32(a, v); }
    bool dap_escribir(uint32_t a, uint32_t v) override { return swd_.mem_write32(a, v); }
    // Los registros PROPIOS del AP. En SWD la direccion del AP son solo dos
    // bits (A[3:2]): el resto sale del campo APBANKSEL de SELECT, asi que para
    // llegar a 0xF8 hay que cambiar de banco, leer, y DEJARLO COMO ESTABA. Lo
    // ultimo no es cortesia: todo el acceso a memoria vive en el banco 0, y
    // dejarlo en otro deja al depurador leyendo basura sin decir nada.
    // Y la lectura del AP va con un ciclo de retraso, como en el silicio: el
    // dato se recoge en RDBUFF.
    bool dap_leer_ap(unsigned ap, unsigned reg, uint32_t& v) override {
        uint32_t basura = 0;
        if (!swd_.escribir_dp(0x8, (uint32_t(ap) << 24) | (reg & 0xF0u)))
            return false;
        const bool ok = swd_.leer_ap(reg & 0x0Cu, basura) && swd_.leer_dp(0xC, v);
        swd_.escribir_dp(0x8, 0);
        return ok;
    }
    // Ráfaga con auto-incremento de TAR: un solo TAR para hasta 1 KiB, que es
    // la única forma de que un `load` no tarde una eternidad.
    unsigned dap_leer_bloque(uint32_t a, uint32_t* w, unsigned n) override {
        return swd_.mem_read_block(a, w, n);
    }

    void run() { servir(); }

    SwdProbe swd_;
};

} // namespace stm32
#endif // STM32_VERIF_GDB_STUB_H
