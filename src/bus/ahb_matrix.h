// =============================================================================
// ahb_matrix.h — Matriz de interconexión AHB multicapa (8 maestros x 7 esclavos)
//
// Fase F1 (LT con anotación de tiempo, decisión D2-a del plan):
//   * decodificación global de direcciones [IR, §5.1, §6.5];
//   * máscara de conectividad maestro-esclavo [IR, §6.2] — un camino inexistente
//     responde ERROR, igual que un acceso a rango reservado [IR, §6.5-nota];
//   * la CCM RAM NO cuelga de la matriz: cualquier acceso a 0x1000 0000 desde
//     la matriz es un error (solo el D-bus del núcleo la alcanza) [IR, §5.3];
//   * arbitraje por puerto de esclavo: cada esclavo lleva su "ocupado hasta",
//     de modo que dos maestros que coinciden en el mismo esclavo se serializan y
//     el perdedor recibe la penalización en su anotación de tiempo [IR, §6.7];
//     los pares maestro-esclavo disjuntos siguen siendo concurrentes (coste 0).
//   * fase de dirección de 1 ciclo HCLK por transacción [IR, §6.3]; la fase de
//     datos la anota cada esclavo.
// La topología queda preparada para elevar a AT (nb_transport) sin cambios.
// [IR, §6; plan P7/D2]
// =============================================================================
#ifndef STM32_BUS_AHB_MATRIX_H
#define STM32_BUS_AHB_MATRIX_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/ahb_types.h"
#include "../mem/mem_caps.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Decodificación global -> esclavo de la matriz [IR, §5.1, §6.5]
//   -1 : rango reservado / no decodificado (ERROR)
//   -2 : rango de la CCM (no conectada a la matriz)
//
// Va FUERA del módulo, y como función pura, por dos razones que resultaron ser
// la misma: depende solo de dos datos —el mapa de RAM del chip y si lleva bus
// externo—, y así se puede comprobar la decodificación de un chip que no está
// montado sin construir una matriz entera con sus dieciséis sockets. Es lo que
// hace T128 para el LQFP64, que no lleva FSMC.
// ---------------------------------------------------------------------------
inline int decodifica_mapa(const MapaRam& ram, bool hay_fsmc, uint64_t a) {
    using S = BusSlaveId;
    if (a < ram.ccm_base)                                     // 0x0000_0000-0x0FFF_FFFF
        return int(S::FLASH_ICODE);                           //   alias 0x0 + Flash
    if (ram.hay_ccm() && a < uint64_t(ram.ccm_base) + ram.ccm_size)
        return -2;                                            // CCM
    if (a <= addr::CODE_END)                                  // sysmem/OTP/opt bytes
        return int(S::FLASH_ICODE);
    if (a >= ram.sram1_base && a < uint64_t(ram.sram1_base) + ram.sram1_size)
        return int(S::SRAM1);
    if (ram.hay_sram2() &&
        a >= ram.sram2_base && a < uint64_t(ram.sram2_base) + ram.sram2_size)
        return int(S::SRAM2);
    if (a >= addr::SRAM_RGN && a <= addr::SRAM_RGN_END) return -1;  // resto: reservado
    if (a >= 0x40000000ull && a < 0x50000000ull) return int(S::AHB1_SEG);
    if (a >= 0x50000000ull && a < 0x60000000ull) return int(S::AHB2_SEG);
    // Un chip sin bus externo no tiene ahi un periferico apagado: tiene ESPACIO
    // RESERVADO, y tocarlo es un error de bus. Es la diferencia entre modelar
    // la ausencia y esconderla.
    if (hay_fsmc) {
        if (a >= addr::FSMC_MEM && a <= addr::FSMC_MEM_END) return int(S::FSMC_EXT);
        if (a >= addr::FSMC_REGS &&
            a < uint64_t(addr::FSMC_REGS) + addr::FSMC_REGS_SIZE)
            return int(S::FSMC_EXT);
    }
    return -1;
}

SC_MODULE(AhbMatrix) {
    static constexpr unsigned NM = unsigned(BusMaster::N_MASTERS);   // 8
    static constexpr unsigned NS = unsigned(BusSlaveId::N_SLAVES);   // 7

    // El mapa de las RAM de ESTE chip, lo primero que se construye: de él salen
    // los rangos que la matriz decodifica hacia la SRAM1, la SRAM2 y la CCM.
    // Por omisión el del F407. [mem/mem_caps.h]
    const MapaRam ram;
    // ¿Este miembro de la familia lleva bus externo? En el LQFP64 no hay dónde
    // sacarlo, y entonces los 0x6000_0000-0x9FFF_FFFF y la ventana de registros
    // del FSMC son espacio RESERVADO: tocarlos da error de bus, no un acceso
    // silencioso a un controlador que el chip no tiene.
    const bool hay_fsmc;

    sc_core::sc_vector<tlm_utils::simple_target_socket_tagged<AhbMatrix>>
        from_master;                       // [BusMaster]
    sc_core::sc_vector<tlm_utils::simple_initiator_socket_tagged<AhbMatrix>>
        to_slave;                          // [BusSlaveId]
    // Puerto de verificación (fase F1): permite a un banco de pruebas inyectar
    // transacciones impersonando cualquier maestro (el id se toma de AhbExt).
    // No corresponde a hardware: queda sin bindear en un uso normal.
    tlm_utils::simple_target_socket_optional<AhbMatrix> from_tb{"from_tb"};

    sc_core::sc_in<bool>   hclk{"hclk"};
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};
    sc_core::sc_in<bool>   rst_n{"rst_n"};

    // --- Estadísticas (observabilidad y verificación) -----------------------
    uint64_t n_xfer[NM][NS] = {};        // transacciones concedidas
    uint64_t n_err_conn     = 0;         // rechazos por máscara de conectividad
    uint64_t n_err_decode   = 0;         // rechazos por rango reservado
    uint64_t n_err_ccm      = 0;         // intentos de alcanzar la CCM
    uint64_t n_contention   = 0;         // transacciones que esperaron a otra

    explicit AhbMatrix(sc_core::sc_module_name nm, MapaRam r = RAM_STM32F407VG,
                       bool fsmc = true)
        : sc_core::sc_module(nm), ram(r), hay_fsmc(fsmc),
          from_master("from_master", NM), to_slave("to_slave", NS) {
        SC_HAS_PROCESS(AhbMatrix);
        for (unsigned m = 0; m < NM; ++m) {
            from_master[m].register_b_transport(this, &AhbMatrix::bt_tagged, m);
            from_master[m].register_transport_dbg(this, &AhbMatrix::dbg_tagged, m);
        }
        from_tb.register_b_transport(this, &AhbMatrix::bt_tb);
        from_tb.register_transport_dbg(this, &AhbMatrix::dbg_tb);
        build_connectivity();
        for (unsigned s = 0; s < NS; ++s) busy_until_[s] = sc_core::SC_ZERO_TIME;
    }

    // Consulta de la máscara (la usa el banco de pruebas para comprobarla).
    bool connected(BusMaster m, BusSlaveId s) const {
        return conn_[unsigned(m)][unsigned(s)];
    }
    // Decodificación pública (verificación y trazas).
    int decode_addr(uint64_t a) const { return decode(a); }

private:
    // -----------------------------------------------------------------------
    // Tabla maestro x esclavo [IR, §6.2]. true = camino existente.
    // -----------------------------------------------------------------------
    bool conn_[NM][NS] = {};

    void build_connectivity() {
        auto allow = [&](BusMaster m, std::initializer_list<BusSlaveId> ss) {
            for (auto s : ss) conn_[unsigned(m)][unsigned(s)] = true;
        };
        using S = BusSlaveId; using M = BusMaster;
        // Filas exactas de la tabla [IR, §6.2]:
        allow(M::CORE_IBUS,   {S::FLASH_ICODE, S::SRAM1, S::SRAM2, S::FSMC_EXT});
        allow(M::CORE_DBUS,   {S::FLASH_DCODE, S::SRAM1, S::SRAM2, S::FSMC_EXT});
        allow(M::CORE_SBUS,   {S::SRAM1, S::SRAM2, S::AHB1_SEG, S::AHB2_SEG, S::FSMC_EXT});
        allow(M::DMA1_MEM,    {S::SRAM1, S::SRAM2, S::AHB1_SEG, S::AHB2_SEG, S::FSMC_EXT});
        // DMA2 alcanza además la Flash por el bus DCode. La tabla de §6.2 pone
        // "No" en esa celda, pero §11.1.1 del mismo informe dice explícitamente
        // que DMA2 "soporta transferencias memoria-a-memoria y acceso a la
        // memoria Flash", que es lo que hace el silicio y de lo que depende el
        // caso de uso clásico Flash -> SRAM. Se resuelve la contradicción a
        // favor de §11.1.1 (véase doc/stm32f407vg_fase4_dma.md, §9).
        allow(M::DMA2_MEM,    {S::FLASH_DCODE, S::SRAM1, S::SRAM2, S::AHB1_SEG,
                               S::AHB2_SEG, S::FSMC_EXT});
        allow(M::DMA2_PERIPH, {S::FLASH_DCODE, S::SRAM1, S::SRAM2, S::AHB1_SEG,
                               S::AHB2_SEG, S::FSMC_EXT});
        allow(M::ETH_DMA,     {S::SRAM1, S::SRAM2, S::AHB1_SEG});
        allow(M::OTG_HS_DMA,  {S::SRAM1, S::SRAM2, S::AHB1_SEG});
        // La columna "CCM RAM" de la tabla solo tiene 'Sí' en el D-Bus y la CCM
        // no es esclavo de la matriz: se resuelve en el router del núcleo.
    }

    // La decodificación, que vive fuera del módulo (véase `decodifica_mapa`).
    int decode(uint64_t a) const { return decodifica_mapa(ram, hay_fsmc, a); }

    // -----------------------------------------------------------------------
    // Transporte
    // -----------------------------------------------------------------------
    void bt_tagged(int m, tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        route(BusMaster(m), gp, t);
    }
    void bt_tb(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        route(ahb_ext(gp).master, gp, t);      // el maestro lo declara el TB
    }

    void route(BusMaster mid, tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const unsigned m = unsigned(mid);
        AhbExt* ext = gp.get_extension<AhbExt>();
        int s = decode(gp.get_address());

        if (s == -2) {                                    // CCM desde la matriz
            ++n_err_ccm;
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (s < 0) {                                      // rango reservado
            ++n_err_decode;
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        // Región Code: el I-bus va a Flash-ICode, cualquier acceso de dato va a
        // Flash-DCode [IR, §5.1, §7.6].
        if (s == int(BusSlaveId::FLASH_ICODE) && mid != BusMaster::CORE_IBUS)
            s = int(BusSlaveId::FLASH_DCODE);
        if (s == int(BusSlaveId::FLASH_ICODE) && ext && !ext->instr)
            s = int(BusSlaveId::FLASH_DCODE);

        if (!conn_[m][s]) {                               // camino inexistente
            ++n_err_conn;
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (ext) { ext->slave = uint8_t(s); ext->wait_cycles = 0; }

        // --- Arbitraje del puerto de esclavo --------------------------------
        const sc_core::sc_time t_req = sc_core::sc_time_stamp() + t;
        if (busy_until_[s] > t_req) {                     // colisión: esperar
            const sc_core::sc_time pen = busy_until_[s] - t_req;
            t += pen;
            ++n_contention;
            if (ext) ext->wait_cycles = uint32_t(pen / cycle());
        }
        last_granted_[s] = uint8_t(m);                    // puntero round-robin
        t += cycle();                                     // fase de dirección

        to_slave[unsigned(s)]->b_transport(gp, t);        // fase de datos

        busy_until_[s] = sc_core::sc_time_stamp() + t;
        ++n_xfer[m][s];
    }

    unsigned dbg_tagged(int m, tlm::tlm_generic_payload& gp) {
        return dbg_route(BusMaster(m), gp);
    }
    unsigned dbg_tb(tlm::tlm_generic_payload& gp) {
        return dbg_route(ahb_ext(gp).master, gp);
    }
    unsigned dbg_route(BusMaster mid, tlm::tlm_generic_payload& gp) {
        int s = decode(gp.get_address());
        if (s < 0) return 0;
        if (s == int(BusSlaveId::FLASH_ICODE) && mid != BusMaster::CORE_IBUS)
            s = int(BusSlaveId::FLASH_DCODE);
        if (!conn_[unsigned(mid)][s]) return 0;
        return to_slave[unsigned(s)]->transport_dbg(gp);
    }

    sc_core::sc_time cycle() const {
        const double f = hclk_hz.read();
        return f > 0.0 ? sc_core::sc_time(1.0e12 / f, sc_core::SC_PS)
                       : sc_core::SC_ZERO_TIME;
    }

    sc_core::sc_time busy_until_[NS];
    uint8_t          last_granted_[NS] = {};
};

} // namespace stm32
#endif // STM32_BUS_AHB_MATRIX_H
