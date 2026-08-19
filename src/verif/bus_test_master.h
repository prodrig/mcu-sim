// =============================================================================
// bus_test_master.h — Maestro de bus para verificación (fase F1)
//
// Iniciador TLM que emite transacciones contra la matriz AHB declarando en la
// extensión AhbExt qué maestro está impersonando. Permite comprobar la máscara
// de conectividad [IR, §6.2], los rangos reservados [IR, §6.5-nota] y el
// comportamiento de memorias y periféricos sin necesidad de la CPU (que llega
// en la fase F2).
//
// No es hardware: se conecta al puerto de verificación `from_tb` de AhbMatrix.
// =============================================================================
#ifndef STM32_VERIF_BUS_TEST_MASTER_H
#define STM32_VERIF_BUS_TEST_MASTER_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstring>
#include "../common/ahb_types.h"

namespace stm32 {

SC_MODULE(BusTestMaster) {
    tlm_utils::simple_initiator_socket<BusTestMaster> isk{"isk"};

    // Maestro que se impersona en la siguiente transacción
    BusMaster master = BusMaster::CORE_SBUS;
    bool      instr  = false;      // HPROT[0]: búsqueda de instrucción
    bool      privileged = true;

    SC_CTOR(BusTestMaster) {}

    // --- Acceso genérico ----------------------------------------------------
    tlm::tlm_response_status access(bool write, uint64_t a,
                                    unsigned char* data, unsigned len,
                                    sc_core::sc_time* elapsed = nullptr) {
        return access_as(master, instr, write, a, data, len, elapsed);
    }

    // Variante que declara el maestro explícitamente: permite que dos procesos
    // del banco de pruebas usen el mismo socket impersonando maestros distintos
    // (necesario para provocar contención en un puerto de esclavo).
    tlm::tlm_response_status access_as(BusMaster m, bool as_instr, bool write,
                                       uint64_t a, unsigned char* data, unsigned len,
                                       sc_core::sc_time* elapsed = nullptr) {
        tlm::tlm_generic_payload gp;
        AhbExt ext;
        ext.master     = m;
        ext.instr      = as_instr;
        ext.privileged = privileged;
        gp_setup(gp, write, a, data, len);
        gp.set_extension(&ext);
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        isk->b_transport(gp, t);
        if (elapsed) *elapsed = t;
        sc_core::wait(t);                       // el maestro consume la latencia
        gp.clear_extension(&ext);
        last_slave_       = ext.slave;
        last_wait_cycles_ = ext.wait_cycles;
        return gp.get_response_status();
    }

    // --- Atajos de 8/16/32 bits --------------------------------------------
    tlm::tlm_response_status write32(uint64_t a, uint32_t v) {
        unsigned char b[4];
        for (unsigned i = 0; i < 4; ++i) b[i] = uint8_t(v >> (8 * i));
        return access(true, a, b, 4);
    }
    tlm::tlm_response_status read32(uint64_t a, uint32_t& v) {
        unsigned char b[4] = {0, 0, 0, 0};
        const auto r = access(false, a, b, 4);
        v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
            (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        return r;
    }
    tlm::tlm_response_status write16(uint64_t a, uint16_t v) {
        unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
        return access(true, a, b, 2);
    }
    tlm::tlm_response_status read16(uint64_t a, uint16_t& v) {
        unsigned char b[2] = {0, 0};
        const auto r = access(false, a, b, 2);
        v = uint16_t(uint16_t(b[0]) | (uint16_t(b[1]) << 8));
        return r;
    }
    tlm::tlm_response_status write8(uint64_t a, uint8_t v) {
        return access(true, a, &v, 1);
    }
    tlm::tlm_response_status read8(uint64_t a, uint8_t& v) {
        return access(false, a, &v, 1);
    }
    // Bloque contiguo (ráfaga INCR modelada como una sola transacción LT)
    tlm::tlm_response_status write_block(uint64_t a, const unsigned char* d, unsigned n) {
        return access(true, a, const_cast<unsigned char*>(d), n);
    }
    tlm::tlm_response_status read_block(uint64_t a, unsigned char* d, unsigned n) {
        return access(false, a, d, n);
    }

    // Lectura conveniente que devuelve el dato (0xDEADBEEF si hay error)
    uint32_t rd32(uint64_t a) {
        uint32_t v = 0;
        return (read32(a, v) == tlm::TLM_OK_RESPONSE) ? v : 0xDEADBEEFu;
    }

    unsigned last_slave()       const { return last_slave_; }
    uint32_t last_wait_cycles() const { return last_wait_cycles_; }

private:
    unsigned last_slave_ = 0;
    uint32_t last_wait_cycles_ = 0;
};

} // namespace stm32
#endif // STM32_VERIF_BUS_TEST_MASTER_H
